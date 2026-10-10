#requires -Version 7.0
<#
.SYNOPSIS
Validates and summarizes six completed formal playability comparisons.
.EXAMPLE
pwsh -File tools/playability/Summarize-PlayabilityMatrix.ps1 -Label candidate10-final
.NOTES
Reads existing results only. Raw metric strings and hashes are retained without
rounding. Paint throughput is CPU presentation work, not measured display FPS.
The map fixture does not seed the declared 256 units.
#>
param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Label,
    [string]$MeasurementsRoot = (Join-Path ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))) 'build/playability'),
    [string]$OutputPath,
    [ValidateRange(1,6)][int[]]$ExpectedSpeeds = @(1,2,3,4,5,6)
)
$ErrorActionPreference = 'Stop'

function Get-TaskValue($Record, [string]$Name, $Default = $null) {
    $taskProperty = $Record.PSObject.Properties[$Name]
    if ($null -eq $taskProperty -or $null -eq $taskProperty.Value) { return $Default }
    return $taskProperty.Value
}
function Get-TaskRequired($Record, [string]$Name, [string]$Context) {
    $taskValue = Get-TaskValue $Record $Name
    if ($null -eq $taskValue -or [string]::IsNullOrWhiteSpace([string]$taskValue)) {
        throw "Missing required field: $Context $Name"
    }
    return $taskValue
}
function Get-TaskNumber($Record, [string]$Name, [string]$Context, [switch]$Optional) {
    $taskValue = Get-TaskValue $Record $Name
    if ($null -eq $taskValue -or [string]::IsNullOrWhiteSpace([string]$taskValue)) {
        if ($Optional) { return $null }
        throw "Missing required numeric field: $Context $Name"
    }
    $taskNumber = 0.0
    if (![double]::TryParse([string]$taskValue,[Globalization.NumberStyles]::Float,
        [Globalization.CultureInfo]::InvariantCulture,[ref]$taskNumber) -or
        ![double]::IsFinite($taskNumber) -or $taskNumber -lt 0) {
        throw "Invalid numeric field: $Context $Name=$taskValue"
    }
    return $taskNumber
}
function Get-TaskInteger($Record, [string]$Name, [string]$Context, $Default = $null) {
    $taskValue = Get-TaskValue $Record $Name $Default
    $taskNumber = 0L
    if ($null -eq $taskValue -or ![long]::TryParse([string]$taskValue,
        [Globalization.NumberStyles]::Integer,[Globalization.CultureInfo]::InvariantCulture,
        [ref]$taskNumber) -or $taskNumber -lt 0) {
        throw "Missing or invalid integer field: $Context $Name"
    }
    return $taskNumber
}
function Read-TaskJson([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing evidence file: $Path" }
    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
}
function Read-TaskWorkload([string]$Log, [string]$Prefix) {
    if (!(Test-Path -LiteralPath $Log -PathType Leaf)) { throw "Missing workload log: $Log" }
    $taskLine = [IO.File]::ReadAllLines($Log) |
        Where-Object { $_.StartsWith($Prefix,[StringComparison]::Ordinal) } | Select-Object -Last 1
    if (!$taskLine) { throw "Missing workload summary: $Log $Prefix" }
    $taskValues = [ordered]@{}
    foreach ($taskMatch in [regex]::Matches($taskLine,'([A-Za-z][A-Za-z0-9]*)=([^\s]+)')) {
        $taskValues[$taskMatch.Groups[1].Value] = $taskMatch.Groups[2].Value
    }
    return $taskValues
}
function Assert-TaskMeasurement($Record, [string]$Context) {
    if ((Get-TaskValue $Record 'completed') -isnot [bool] -or $Record.completed -ne $true -or
        (Get-TaskInteger $Record 'exitCode' $Context) -ne 0) { throw "Incomplete run: $Context" }
    foreach ($taskFlag in @('invalid','failed','cancelled','canceled')) {
        $taskValue = Get-TaskValue $Record $taskFlag $false
        if ($taskValue -isnot [bool] -or $taskValue) { throw "Invalid or cancelled run: $Context $taskFlag" }
    }
    $taskEvidencePassed = Get-TaskValue $Record 'workloadEvidencePassed' $true
    if ($taskEvidencePassed -isnot [bool] -or !$taskEvidencePassed -or
        @(Get-TaskValue $Record 'workloadEvidenceErrors' @()).Count) { throw "Failed workload evidence: $Context" }
    foreach ($taskFlag in @('trace','renderDetail','denseBuildingProfile','sustainMovement')) {
        $taskValue = Get-TaskValue $Record $taskFlag $false
        if ($taskValue -isnot [bool] -or $taskValue) { throw "Non-formal workload: $Context $taskFlag" }
    }
    if ((Get-TaskInteger $Record 'historicalObjects' $Context 0) -ne 0 -or
        @(Get-TaskValue $Record 'referenceKeys' @()).Count) { throw "Non-formal history/reference workload: $Context" }
    $taskSha = [string](Get-TaskRequired $Record 'executableSha256' $Context)
    if ($taskSha -cnotmatch '^[A-Fa-f0-9]{64}$') { throw "Invalid executable SHA-256: $Context" }
    $taskMetrics = Get-TaskRequired $Record 'metrics' $Context
    if ((Get-TaskRequired $taskMetrics 'passed' $Context) -cne 'yes' -or
        (Get-TaskRequired $taskMetrics 'deadlineExceeded' $Context) -cne 'no' -or
        (Get-TaskRequired $taskMetrics 'sequentialTicks' $Context) -cne 'yes' -or
        (Get-TaskInteger $taskMetrics 'measuredTicks' $Context) -ne (Get-TaskInteger $Record 'ticks' $Context)) {
        throw "Failed or truncated benchmark: $Context"
    }
    [void](Get-TaskRequired $taskMetrics 'worldHash' $Context)
    if ((Get-TaskValue $Record 'displayFpsMeasured' $false) -ne $false -or
        (Get-TaskValue $taskMetrics 'displayFpsMeasured' 'no') -cne 'no') {
        throw "Unexpected display FPS measurement semantics: $Context"
    }
}
function Assert-TaskSpeedSet([object[]]$Rows, [int[]]$Speeds, [string]$Context) {
    if ($Rows.Count -ne $Speeds.Count) { throw "Unexpected row count: $Context expected=$($Speeds.Count) actual=$($Rows.Count)" }
    foreach ($taskSpeed in $Speeds) {
        if (@($Rows | Where-Object { (Get-TaskInteger $_ 'speed' $Context) -eq $taskSpeed }).Count -ne 1) {
            throw "Missing or duplicate speed $taskSpeed : $Context"
        }
    }
}
function Read-TaskMeasurements([string]$Directory, [int[]]$Speeds) {
    if (!(Test-Path -LiteralPath $Directory -PathType Container)) { throw "Missing formal directory: $Directory" }
    if (Test-Path -LiteralPath (Join-Path $Directory 'cancellation.json') -PathType Leaf) {
        throw "Cancelled formal directory: $Directory"
    }
    $taskFiles = @(Get-ChildItem -LiteralPath $Directory -File -Filter '*.json' |
        Where-Object { $_.BaseName -match '-speed[1-6]$' })
    $taskEntries = @(foreach ($taskFile in $taskFiles) {
        $taskRecord = Read-TaskJson $taskFile.FullName
        [pscustomobject]@{ file=$taskFile.FullName; name=$taskFile.Name; record=$taskRecord }
    })
    Assert-TaskSpeedSet @($taskEntries.record) $Speeds $Directory
    return $taskEntries
}
function Assert-TaskWorkload($Before, $After, [string]$Prefix, [int]$Units, [string]$Scenario, [string]$Context) {
    $taskKeys = if ($Prefix -eq 'Live movement benchmark:') {
        @('passed','units','mixed','combat','battle','choke','commands','movedUnique','peakMoving',
            'meanMoving','finalAlive','finalArrived','attacks','targetOriginalStrength','targetMaxHealth',
            'targetInitialHealth','targetFinalHealth','targetAlive','targetDamage','lastAttackTick','trajectoryHash')
    } else {
        @('passed','units','buildings','samples','acceptedMoves','acceptedAttacks','aliveA','aliveB','deathsA','deathsB',
            'buildingsA','buildingsB','shotsA','shotsB','damageA','damageB','firstFireA','firstFireB',
            'lastFireA','lastFireB','lastAttackSample','attacks','trajectoryHash')
    }
    foreach ($taskValues in @($Before,$After)) {
        foreach ($taskKey in $taskKeys) {
            if (!$taskValues.Contains($taskKey) -or [string]::IsNullOrWhiteSpace($taskValues[$taskKey])) {
                throw "Missing game result: $Context $Prefix $taskKey"
            }
        }
        if ($taskValues.passed -cne 'yes' -or $taskValues.units -ne [string]$Units) {
            throw "Failed or mismatched game workload: $Context $Prefix"
        }
        if ($Prefix -eq 'Live movement benchmark:') {
            $taskExpectedBattle = if ($Scenario -eq 'battle') { 'yes' } else { 'no' }
            if ($taskValues.battle -cne $taskExpectedBattle -or
                ($taskValues.Contains('historyObjects') -and $taskValues.historyObjects -ne '0') -or
                ($taskValues.Contains('sustainedMovement') -and $taskValues.sustainedMovement -cne 'no')) {
                throw "Non-formal or mismatched actual movement: $Context"
            }
        }
    }
    foreach ($taskKey in $taskKeys) {
        if ($Before[$taskKey] -cne $After[$taskKey]) { throw "Game result differs: $Context $Prefix $taskKey" }
    }
}
function Assert-TaskComparison($Comparison, $Before, $After, [string]$Context) {
    foreach ($taskFlag in @('invalid','failed','cancelled','canceled')) {
        $taskValue = Get-TaskValue $Comparison $taskFlag $false
        if ($taskValue -isnot [bool] -or $taskValue) { throw "Invalid or cancelled comparison: $Context $taskFlag" }
    }
    if ((Get-TaskValue $Comparison 'gameplayEquivalent') -isnot [bool] -or
        !$Comparison.gameplayEquivalent -or (Get-TaskValue $Comparison 'displayFpsMeasured') -ne $false) {
        throw "Comparison does not establish gameplay equivalence: $Context"
    }
    foreach ($taskKey in @('scenario','units','width','height','speed','ticks')) {
        if ((Get-TaskRequired $Comparison $taskKey $Context) -cne [string]$After.$taskKey) {
            throw "Comparison workload is stale or mismatched: $Context $taskKey"
        }
    }
    foreach ($taskPair in @(@('baselineSha256',$Before.executableSha256),@('candidateSha256',$After.executableSha256),
        @('worldHash',$After.metrics.worldHash))) {
        if ([string](Get-TaskRequired $Comparison $taskPair[0] $Context) -cne [string]$taskPair[1]) {
            throw "Comparison identity is stale: $Context $($taskPair[0])"
        }
    }
    if ((Get-TaskInteger $Comparison 'history' $Context 0) -ne 0 -or
        (Get-TaskValue $Comparison 'sustainMovement' $false) -ne $false -or
        @(Get-TaskValue $Comparison 'baselineReferenceKeys' @()).Count -or
        @(Get-TaskValue $Comparison 'candidateReferenceKeys' @()).Count) {
        throw "Non-formal comparison: $Context"
    }
    $taskMetricPairs = [ordered]@{
        measuredSeconds='measuredSeconds'; paintFps='paintFps'; tickHz='actualTickHz'; simMeanMs='simMeanMs';
        paintMeanMs='paintMeanMs'; paintP95Ms='paintP95Ms'; paintMaxMs='paintMaxMs';
        gapP95Ms='frameGapP95Ms'; gapP99Ms='frameGapP99Ms'; gapMaxMs='frameGapMaxMs';
        gap50='gapsOver50Ms'; gap100='gapsOver100Ms'
    }
    foreach ($taskKey in $taskMetricPairs.Keys) {
        $taskOptional = $taskKey -in @('gapP99Ms','gap50','gap100')
        foreach ($taskSide in @('Before','After')) {
            $taskRecord = if ($taskSide -eq 'Before') { $Before } else { $After }
            $taskMetric = Get-TaskNumber $taskRecord.metrics $taskMetricPairs[$taskKey] $Context -Optional:$taskOptional
            $taskComparisonValue = Get-TaskNumber $Comparison "$taskKey$taskSide" $Context -Optional:$taskOptional
            if ($taskMetric -ne $taskComparisonValue) { throw "Comparison metric is stale: $Context $taskKey$taskSide" }
        }
    }
}

$taskSpeeds = @($ExpectedSpeeds | Sort-Object -Unique)
if (!$taskSpeeds.Count -or $taskSpeeds.Count -ne $ExpectedSpeeds.Count) { throw 'ExpectedSpeeds must be a nonempty distinct set.' }
$taskRoot = [IO.Path]::GetFullPath($MeasurementsRoot)
$taskSpecs = @(
    @{ suffix='map'; scenario='map'; width=1024; height=640 },
    @{ suffix='move'; scenario='move'; width=1920; height=1080 },
    @{ suffix='battle'; scenario='battle'; width=1920; height=1080 },
    @{ suffix='fullscreen'; scenario='map'; width=2256; height=1504 },
    @{ suffix='fullscreen-move'; scenario='move'; width=2256; height=1504 },
    @{ suffix='fullscreen-battle'; scenario='battle'; width=2256; height=1504 }
)
$taskRows = [Collections.Generic.List[object]]::new()
$taskComparisons = [Collections.Generic.List[object]]::new()
$taskReports = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($taskSpec in $taskSpecs) {
    $taskBaselineDir = Join-Path $taskRoot "baseline-$($taskSpec.suffix)"
    $taskCandidateDir = Join-Path $taskRoot "$Label-$($taskSpec.suffix)"
    $taskComparisonPath = Join-Path $taskCandidateDir 'comparison.json'
    $taskComparisonRows = @(Read-TaskJson $taskComparisonPath)
    Assert-TaskSpeedSet $taskComparisonRows $taskSpeeds $taskComparisonPath
    $taskBeforeEntries = @(Read-TaskMeasurements $taskBaselineDir $taskSpeeds)
    $taskAfterEntries = @(Read-TaskMeasurements $taskCandidateDir $taskSpeeds)
    foreach ($taskSpeed in $taskSpeeds) {
        $taskBeforeEntry = $taskBeforeEntries | Where-Object { $_.record.speed -eq $taskSpeed }
        $taskAfterEntry = $taskAfterEntries | Where-Object { $_.record.speed -eq $taskSpeed }
        if ($taskBeforeEntry.name -cne $taskAfterEntry.name) { throw "Source filenames differ: $taskCandidateDir speed=$taskSpeed" }
        $taskBefore = $taskBeforeEntry.record
        $taskAfter = $taskAfterEntry.record
        $taskContext = $taskAfterEntry.file
        foreach ($taskRecord in @($taskBefore,$taskAfter)) {
            Assert-TaskMeasurement $taskRecord $taskContext
            if ($taskRecord.scenario -cne $taskSpec.scenario -or
                (Get-TaskInteger $taskRecord 'units' $taskContext) -ne 256 -or
                (Get-TaskInteger $taskRecord 'width' $taskContext) -ne $taskSpec.width -or
                (Get-TaskInteger $taskRecord 'height' $taskContext) -ne $taskSpec.height -or
                (Get-TaskInteger $taskRecord 'ticks' $taskContext) -ne 600) {
                throw "Formal fixture differs: $taskContext"
            }
            $taskApplied = Get-TaskValue $taskRecord 'unitsFixtureApplied' ($taskSpec.scenario -ne 'map')
            if ($taskApplied -isnot [bool] -or $taskApplied -ne ($taskSpec.scenario -ne 'map')) {
                throw "Unit fixture marker disagrees with scenario: $taskContext"
            }
        }
        if ([string]$taskBefore.metrics.worldHash -cne [string]$taskAfter.metrics.worldHash) { throw "World differs: $taskContext" }
        $taskBeforeLog = [IO.Path]::ChangeExtension($taskBeforeEntry.file,'.log')
        $taskAfterLog = [IO.Path]::ChangeExtension($taskAfterEntry.file,'.log')
        $taskBeforeTrajectory = $null
        $taskAfterTrajectory = $null
        $taskBeforeMovement = $null
        $taskAfterMovement = $null
        $taskBeforeBattle = $null
        $taskAfterBattle = $null
        if ($taskSpec.scenario -ne 'map') {
            $taskBeforeMovement = Read-TaskWorkload $taskBeforeLog 'Live movement benchmark:'
            $taskAfterMovement = Read-TaskWorkload $taskAfterLog 'Live movement benchmark:'
            Assert-TaskWorkload $taskBeforeMovement $taskAfterMovement 'Live movement benchmark:' 256 $taskSpec.scenario $taskContext
            $taskBeforeTrajectory = $taskBeforeMovement.trajectoryHash
            $taskAfterTrajectory = $taskAfterMovement.trajectoryHash
            if ($taskSpec.scenario -eq 'battle') {
                $taskBeforeBattle = Read-TaskWorkload $taskBeforeLog 'Live battle result:'
                $taskAfterBattle = Read-TaskWorkload $taskAfterLog 'Live battle result:'
                Assert-TaskWorkload $taskBeforeBattle $taskAfterBattle 'Live battle result:' 256 $taskSpec.scenario $taskContext
                if ($taskBeforeBattle.trajectoryHash -cne $taskBeforeTrajectory -or
                    $taskAfterBattle.trajectoryHash -cne $taskAfterTrajectory) { throw "Battle/movement trajectory differs: $taskContext" }
            }
        } elseif (!(Test-Path -LiteralPath $taskBeforeLog -PathType Leaf) -or
            !(Test-Path -LiteralPath $taskAfterLog -PathType Leaf)) { throw "Missing map source log: $taskContext" }
        $taskComparison = $taskComparisonRows | Where-Object { $_.speed -eq $taskSpeed }
        Assert-TaskComparison $taskComparison $taskBefore $taskAfter $taskContext
        $taskReport = [string](Get-TaskRequired $taskAfter 'frameReport' $taskContext)
        if ((Get-TaskValue $taskAfter 'frameReportExists') -isnot [bool] -or !$taskAfter.frameReportExists -or
            !(Test-Path -LiteralPath $taskReport -PathType Leaf) -or (Get-Item -LiteralPath $taskReport).Length -le 0 -or
            [IO.Path]::GetFileName($taskReport) -notlike 'session-*.txt' -or !$taskReports.Add([IO.Path]::GetFullPath($taskReport))) {
            throw "Missing, empty or reused session report: $taskContext"
        }
        if ([string](Get-TaskRequired $taskComparison 'candidateFrameReport' $taskContext) -cne $taskReport -or
            (Get-TaskValue $taskComparison 'candidateFrameReportExists') -isnot [bool] -or
            !$taskComparison.candidateFrameReportExists) { throw "Comparison report is stale: $taskContext" }
        $taskRows.Add([pscustomobject][ordered]@{
            group="$Label-$($taskSpec.suffix)"; scenario=$taskAfter.scenario; units=$taskAfter.units;
            unitsFixtureApplied=($taskSpec.scenario -ne 'map'); width=$taskAfter.width; height=$taskAfter.height;
            speed=$taskAfter.speed; ticks=$taskAfter.ticks; historicalObjects=0; sustainMovement=$false;
            trace=$false; renderDetail=$false; denseBuildingProfile=$false; displayFpsMeasured=$false;
            completed=$taskAfter.completed; exitCode=$taskAfter.exitCode; gameplayEquivalent=$true;
            baselineSha256=$taskBefore.executableSha256; candidateSha256=$taskAfter.executableSha256;
            baselineWorldHash=$taskBefore.metrics.worldHash; candidateWorldHash=$taskAfter.metrics.worldHash;
            baselineTrajectoryHash=$taskBeforeTrajectory; candidateTrajectoryHash=$taskAfterTrajectory;
            baselineMetrics=$taskBefore.metrics; candidateMetrics=$taskAfter.metrics;
            baselineMovementMetrics=$taskBeforeMovement; candidateMovementMetrics=$taskAfterMovement;
            baselineBattleMetrics=$taskBeforeBattle; candidateBattleMetrics=$taskAfterBattle;
            sourceBaselineJson=$taskBeforeEntry.file; sourceCandidateJson=$taskAfterEntry.file;
            sourceBaselineLog=$taskBeforeLog; sourceCandidateLog=$taskAfterLog; comparisonPath=$taskComparisonPath;
            sourceBaselineJsonSha256=(Get-FileHash -LiteralPath $taskBeforeEntry.file -Algorithm SHA256).Hash;
            sourceCandidateJsonSha256=(Get-FileHash -LiteralPath $taskAfterEntry.file -Algorithm SHA256).Hash;
            sourceBaselineLogSha256=(Get-FileHash -LiteralPath $taskBeforeLog -Algorithm SHA256).Hash;
            sourceCandidateLogSha256=(Get-FileHash -LiteralPath $taskAfterLog -Algorithm SHA256).Hash;
            comparisonSha256=(Get-FileHash -LiteralPath $taskComparisonPath -Algorithm SHA256).Hash;
            frameReport=$taskReport; frameReportExists=$true;
            frameReportSha256=(Get-FileHash -LiteralPath $taskReport -Algorithm SHA256).Hash
        })
    }
    $taskComparisons.Add([pscustomobject][ordered]@{
        group="$Label-$($taskSpec.suffix)"; baselineDirectory=$taskBaselineDir; candidateDirectory=$taskCandidateDir;
        comparisonPath=$taskComparisonPath; rows=$taskComparisonRows.Count
    })
}
$taskBaselineShas = @($taskRows.baselineSha256 | Sort-Object -Unique)
$taskCandidateShas = @($taskRows.candidateSha256 | Sort-Object -Unique)
if ($taskBaselineShas.Count -ne 1 -or $taskCandidateShas.Count -ne 1) { throw 'Matrix contains mixed executable versions.' }
$taskResult = [pscustomobject][ordered]@{
    label=$Label; expectedSpeeds=$taskSpeeds; rowCount=$taskRows.Count; comparisonCount=$taskComparisons.Count;
    baselineSha256=$taskBaselineShas[0]; candidateSha256=$taskCandidateShas[0]; displayFpsMeasured=$false;
    metricsCopiedWithoutRounding=$true; completeExpectedMatrix=$true;
    comparisons=$taskComparisons.ToArray(); rows=@($taskRows | Sort-Object group,speed)
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $taskEvidenceDir = Join-Path $taskRoot 'final-evidence'
    $OutputPath = Join-Path $taskEvidenceDir "$($Label -replace '-final$','')-matrix.json"
}
$taskOutput = [IO.Path]::GetFullPath($OutputPath)
if ([IO.Path]::GetFileName($taskOutput) -match '-speed[1-6]\.json$' -or
    [IO.Path]::GetFileName($taskOutput) -in @('comparison.json','cancellation.json') -or
    $taskOutput -in @($taskRows.sourceBaselineJson + $taskRows.sourceCandidateJson + $taskRows.comparisonPath +
        $taskRows.sourceBaselineLog + $taskRows.sourceCandidateLog + $taskRows.frameReport)) {
    throw 'OutputPath must not overwrite input evidence.'
}
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($taskOutput))
ConvertTo-Json -InputObject $taskResult -Depth 20 | Set-Content -LiteralPath $taskOutput -Encoding utf8
[pscustomobject]@{ outputPath=$taskOutput; rows=$taskResult.rowCount; comparisons=$taskResult.comparisonCount;
    candidateSha256=$taskResult.candidateSha256; gameplayEquivalent=$true; displayFpsMeasured=$false }
