param(
    [Parameter(Mandatory)][string]$BaselineDirectory,
    [Parameter(Mandatory)][string]$CandidateDirectory,
    [Parameter(Mandatory)][string]$OutputPath,
    [ValidateRange(1,6)][int[]]$ExpectedSpeeds = @(1,2,3,4,5,6)
)
$ErrorActionPreference = 'Stop'
$taskExpectedSpeeds = @($ExpectedSpeeds | Sort-Object -Unique)
if (!$taskExpectedSpeeds.Count) { throw 'ExpectedSpeeds must not be empty.' }

function Get-Value($Record, [string]$Name, $Default = $null) {
    $taskProperty = $Record.PSObject.Properties[$Name]
    if ($null -eq $taskProperty -or $null -eq $taskProperty.Value) { return $Default }
    return $taskProperty.Value
}
function Get-Required($Record, [string]$Name, [string]$Context) {
    $taskValue = Get-Value $Record $Name
    if ($null -eq $taskValue -or [string]::IsNullOrWhiteSpace([string]$taskValue)) {
        throw "Missing required field: $Context $Name"
    }
    return $taskValue
}
function Get-Metric($Record, [string]$Name, [string]$Context, [switch]$Optional) {
    $taskValue = Get-Value $Record.metrics $Name
    if ($null -eq $taskValue -or [string]::IsNullOrWhiteSpace([string]$taskValue)) {
        if ($Optional) { return $null }
        throw "Missing required metric: $Context $Name"
    }
    $taskNumber = 0.0
    if (![double]::TryParse([string]$taskValue, [Globalization.NumberStyles]::Float,
        [Globalization.CultureInfo]::InvariantCulture, [ref]$taskNumber) -or
        ![double]::IsFinite($taskNumber) -or $taskNumber -lt 0) {
        throw "Invalid metric: $Context $Name=$taskValue"
    }
    return $taskNumber
}
function Get-Counter($Record, [string]$Name, [string]$Context) {
    $taskNumber = Get-Metric $Record $Name $Context -Optional
    if ($null -eq $taskNumber) { return $null }
    if ($taskNumber -ne [Math]::Truncate($taskNumber) -or $taskNumber -gt [long]::MaxValue) {
        throw "Invalid counter: $Context $Name=$taskNumber"
    }
    return [long]$taskNumber
}
function Read-Workload([string]$Log, [string]$Prefix) {
    $taskLine = [IO.File]::ReadAllLines($Log) | Where-Object { $_.StartsWith($Prefix) } | Select-Object -Last 1
    if (!$taskLine) { throw "Missing required workload summary: $Log $Prefix" }
    $taskValues = @{}
    foreach ($taskMatch in [regex]::Matches($taskLine,'([A-Za-z][A-Za-z0-9]*)=([^\s]+)')) {
        $taskValues[$taskMatch.Groups[1].Value] = $taskMatch.Groups[2].Value
    }
    return $taskValues
}
function Get-History($Record, [string]$Context) {
    $taskValue = Get-Value $Record 'historicalObjects' 0
    $taskHistory = 0
    if (![int]::TryParse([string]$taskValue, [ref]$taskHistory) -or $taskHistory -lt 0) {
        throw "Invalid history workload: $Context"
    }
    return $taskHistory
}
function Get-Sustain($Record, [string]$Context) {
    $taskValue = Get-Value $Record 'sustainMovement' $false
    if ($taskValue -isnot [bool]) { throw "Invalid sustainMovement workload: $Context" }
    return $taskValue
}
function Assert-PlayerFixtureEvidence($Record, [string]$Path) {
    foreach ($taskKey in @('playerScene','mixedRoster','playerMining')) {
        if ((Get-Value $Record $taskKey $false) -isnot [bool]) { throw "Invalid $taskKey type: $Path" }
    }
    $taskSide = 0
    if (![int]::TryParse([string](Get-Value $Record 'fixtureMapSide' 0), [ref]$taskSide) -or
        $taskSide -lt 0 -or $taskSide -gt 480 -or ($taskSide -gt 0 -and $taskSide -lt 96)) {
        throw "Invalid fixtureMapSide: $Path"
    }
    $taskPlayer = Get-Value $Record 'playerScene' $false
    $taskRosterRequested = Get-Value $Record 'mixedRoster' $false
    $taskMiningRequested = Get-Value $Record 'playerMining' $false
    if (($taskSide -gt 0 -or $taskPlayer) -and $Record.scenario -ne 'move' -or
        $taskPlayer -and ($taskSide -eq 0 -or $taskSide -gt 256) -or
        ($taskRosterRequested -or $taskMiningRequested) -and !$taskPlayer -or
        $taskMiningRequested -and $Record.ticks -lt 2400) { throw "Invalid player fixture combination: $Path" }
    $taskLogPath = [IO.Path]::ChangeExtension($Path,'.log')
    if ($taskSide -gt 0) {
        $taskTerrain = Read-Workload $taskLogPath 'Live movement terrain fixture:'
        $taskJsonTerrain = Get-Required $Record 'terrainFixture' $Path
        foreach ($taskKey in @('width','height','terrainCells','playerScene','mixedRoster','playerMining','vision','radar')) {
            if (!$taskTerrain.ContainsKey($taskKey) -or
                [string]$taskTerrain[$taskKey] -ne [string]$taskJsonTerrain.$taskKey) {
                throw "Terrain log/JSON identity differs: $Path $taskKey"
            }
        }
    }
    if ($taskRosterRequested) {
        $taskRoster = Read-Workload $taskLogPath 'Live movement roster fixture:'
        if ($taskRoster.units -ne $Record.units -or $taskRoster.distinctTypes -ne 12) { throw "Invalid native roster census: $Path" }
        $taskTotal = 0
        foreach ($taskType in @('MTNK','SREF','TTNK','FV','SCHP','HTNK','E1','CLEG','JUMPJET','SNIPE','GHOST','E2')) {
            $taskCount = 0
            if (![int]::TryParse([string]$taskRoster[$taskType],[ref]$taskCount) -or $taskCount -lt 1) {
                throw "Missing native roster type: $Path $taskType"
            }
            $taskTotal += $taskCount
        }
        if ($taskTotal -ne $Record.units) { throw "Native roster census sum differs: $Path" }
    }
    if ($taskMiningRequested) {
        $taskMine = Read-Workload $taskLogPath 'Live player mining benchmark:'
        if ($taskMine.passed -ne 'yes' -or $taskMine.nativeApiFixture -ne 'yes' -or $taskMine.seededOnce -ne 'yes' -or
            $taskMine.simplifiedIncomeEverEnabled -ne 'no' -or $taskMine.samples -ne $Record.ticks + 60 -or
            [long]$taskMine.nativeUnloads -lt 1 -or [long]$taskMine.seededNativePickups -lt 20 -or
            $taskMine.minerAlive -ne 'yes' -or $taskMine.failure -ne 'none' -or $taskMine.capacity -ne 20 -or
            $taskMine.peakCargoUnits -ne $taskMine.capacity -or [long]$taskMine.oreFinalBails -ge [long]$taskMine.oreInitialBails -or
            [long]$taskMine.tripsFinal - [long]$taskMine.tripsInitial -ne [long]$taskMine.nativeUnloads -or
            [long]$taskMine.creditsFinal - [long]$taskMine.creditsInitial -ne [long]$taskMine.nativeUnloadCredits -or
            [long]$taskMine.nativeUnloadCredits -le 0) { throw "Incomplete native mining evidence: $Path" }
    }
}
function Assert-WorkloadEvidence($Record, [hashtable]$Movement, [string]$Context) {
    $taskHistory = Get-History $Record $Context
    if ($Movement.ContainsKey('historyObjects')) {
        $taskLoggedHistory = 0
        if (![int]::TryParse($Movement.historyObjects, [ref]$taskLoggedHistory) -or
            $taskLoggedHistory -ne $taskHistory) { throw "History log disagrees with workload: $Context" }
    } elseif ($taskHistory -gt 0) { throw "Missing actual history workload evidence: $Context" }
    $taskSustain = Get-Sustain $Record $Context
    if ($Movement.ContainsKey('sustainedMovement')) {
        $taskExpectedSustain = if ($taskSustain) { 'yes' } else { 'no' }
        if ($Movement.sustainedMovement -ne $taskExpectedSustain) {
            throw "Sustained movement log disagrees with workload: $Context"
        }
    } elseif ($taskSustain) { throw "Missing actual sustained movement evidence: $Context" }
}

# Measure-Playability emits one *-speedN.json per run. Ignore aggregate comparison
# JSON when the output is saved alongside these measurements.
$taskOldFiles = @(Get-ChildItem -LiteralPath $BaselineDirectory -Filter '*.json' -File |
    Where-Object { $_.BaseName -match '-speed[0-6]$' })
$taskNewFiles = @(Get-ChildItem -LiteralPath $CandidateDirectory -Filter '*.json' -File |
    Where-Object { $_.BaseName -match '-speed[0-6]$' })
if (!$taskOldFiles.Count -or !$taskNewFiles.Count) { throw 'Both input directories must contain measurement JSON files.' }
$taskSelectedOld = @($taskOldFiles | ForEach-Object {
    $taskRecord = Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json
    $taskSpeed = [int](Get-Required $taskRecord 'speed' $_.FullName)
    if ($taskSpeed -in $taskExpectedSpeeds) { [pscustomobject]@{ file=$_; record=$taskRecord } }
})
$taskSelectedNew = @($taskNewFiles | ForEach-Object {
    $taskRecord = Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json
    $taskSpeed = [int](Get-Required $taskRecord 'speed' $_.FullName)
    if ($taskSpeed -in $taskExpectedSpeeds) { [pscustomobject]@{ file=$_; record=$taskRecord } }
})
foreach ($taskSpeed in $taskExpectedSpeeds) {
    if (!@($taskSelectedOld | Where-Object { $_.record.speed -eq $taskSpeed }).Count -or
        !@($taskSelectedNew | Where-Object { $_.record.speed -eq $taskSpeed }).Count) {
        throw "Missing expected speed: $taskSpeed"
    }
}
foreach ($taskEntry in $taskSelectedNew) {
    if ($taskEntry.file.Name -notin $taskSelectedOld.file.Name) {
        throw "Candidate has no baseline counterpart: $($taskEntry.file.Name)"
    }
}
$taskRows = @(
foreach ($taskEntry in $taskSelectedOld) {
    $taskFile = $taskEntry.file
    $taskNewFile = Join-Path $CandidateDirectory $taskFile.Name
    if (!(Test-Path -LiteralPath $taskNewFile -PathType Leaf)) { throw "Missing counterpart: $taskNewFile" }
    $taskOld = $taskEntry.record
    $taskNew = Get-Content -LiteralPath $taskNewFile -Raw | ConvertFrom-Json
    Assert-PlayerFixtureEvidence $taskOld $taskFile.FullName
    Assert-PlayerFixtureEvidence $taskNew $taskNewFile
    foreach ($taskKey in @('scenario','units','speed','width','height','ticks')) {
        $taskBefore = Get-Required $taskOld $taskKey $taskFile.FullName
        $taskAfter = Get-Required $taskNew $taskKey $taskNewFile
        if ($taskBefore -ne $taskAfter) { throw "Workload differs: $($taskFile.Name) $taskKey" }
    }
    foreach ($taskKey in @('fixtureMapSide','playerScene','mixedRoster','playerMining')) {
        $taskDefault = if ($taskKey -eq 'fixtureMapSide') { 0 } else { $false }
        if ((Get-Value $taskOld $taskKey $taskDefault) -ne (Get-Value $taskNew $taskKey $taskDefault)) {
            throw "Workload differs: $($taskFile.Name) $taskKey"
        }
    }
    $taskMapSide = Get-Value $taskNew 'fixtureMapSide' 0
    $taskPlayerScene = Get-Value $taskNew 'playerScene' $false
    $taskMixedRoster = Get-Value $taskNew 'mixedRoster' $false
    $taskPlayerMining = Get-Value $taskNew 'playerMining' $false
    if ($taskMapSide -gt 0) {
        $taskExpectedCells = if ($taskPlayerScene) { 2 * $taskMapSide * $taskMapSide - $taskMapSide }
            else { $taskMapSide * $taskMapSide }
        foreach ($taskRecord in @($taskOld,$taskNew)) {
            $taskTerrain = Get-Required $taskRecord 'terrainFixture' $taskFile.Name
            if ($taskTerrain.width -ne $taskMapSide -or $taskTerrain.height -ne $taskMapSide -or
                $taskTerrain.terrainCells -ne $taskExpectedCells) {
                throw "Actual terrain differs from requested workload: $($taskFile.Name)"
            }
            if ($taskPlayerScene -and ($taskTerrain.playerScene -ne 'yes' -or
                $taskTerrain.vision -ne 'yes' -or $taskTerrain.radar -ne 'yes' -or
                $taskRecord.movementMetrics.fixtureWorldObjects -ne $taskRecord.units + 9 + (Get-History $taskRecord $taskFile.Name) + [int]$taskPlayerMining -or
                $taskTerrain.mixedRoster -ne $(if ($taskMixedRoster) { 'yes' } else { 'no' }) -or
                $taskTerrain.playerMining -ne $(if ($taskPlayerMining) { 'yes' } else { 'no' }))) {
                throw "Actual player scene population/radar differs: $($taskFile.Name)"
            }
        }
    }
    if ($taskOld.scenario -notin @('map','move','battle')) { throw "Unknown scenario: $($taskFile.Name)" }
    if ((Get-History $taskOld $taskFile.FullName) -ne (Get-History $taskNew $taskNewFile) -or
        (Get-Sustain $taskOld $taskFile.FullName) -ne (Get-Sustain $taskNew $taskNewFile)) {
        throw "Workload differs: $($taskFile.Name) history/sustained movement"
    }
    foreach ($taskKey in @('trace','renderDetail','denseBuildingProfile')) {
        if ((Get-Value $taskOld $taskKey $false) -ne (Get-Value $taskNew $taskKey $false)) {
            throw "Instrumentation differs: $($taskFile.Name) $taskKey"
        }
    }
    foreach ($taskRecord in @($taskOld,$taskNew)) {
        [void](Get-Required $taskRecord 'executableSha256' $taskFile.Name)
        [void](Get-Required $taskRecord 'metrics' $taskFile.Name)
        foreach ($taskKey in @('passed','measuredTicks','sequentialTicks','worldHash','deadlineExceeded')) {
            [void](Get-Required $taskRecord.metrics $taskKey $taskFile.Name)
        }
        if ($taskRecord.completed -ne $true -or $taskRecord.exitCode -ne 0 -or
            $taskRecord.metrics.passed -ne 'yes' -or $taskRecord.metrics.deadlineExceeded -ne 'no' -or
            $taskRecord.metrics.measuredTicks -ne $taskRecord.ticks -or $taskRecord.metrics.sequentialTicks -ne 'yes') {
            throw "Incomplete workload: $($taskFile.Name)"
        }
    }
    if ($taskOld.metrics.worldHash -ne $taskNew.metrics.worldHash) { throw "World differs: $($taskFile.Name)" }
    $taskPrefixes = @(if ($taskOld.scenario -eq 'battle') { 'Live movement benchmark:'; 'Live battle result:' }
        elseif ($taskOld.scenario -eq 'move') { 'Live movement benchmark:' })
    if ($taskMixedRoster) { $taskPrefixes += 'Live movement roster fixture:' }
    if ($taskPlayerMining) { $taskPrefixes += @('Live player mining benchmark:','Live player mining phases:') }
    foreach ($taskPrefix in $taskPrefixes) {
        $taskBefore = Read-Workload ([IO.Path]::ChangeExtension($taskFile.FullName,'.log')) $taskPrefix
        $taskAfter = Read-Workload ([IO.Path]::ChangeExtension($taskNewFile,'.log')) $taskPrefix
        $taskKeys = if ($taskPrefix -eq 'Live movement benchmark:') {
            @('passed','units','mixed','combat','battle','choke','commands','movedUnique','peakMoving',
                'meanMoving','finalAlive','finalArrived','attacks','targetOriginalStrength','targetMaxHealth',
                'targetInitialHealth','targetFinalHealth','targetAlive','targetDamage','lastAttackTick','trajectoryHash')
        } elseif ($taskPrefix -eq 'Live movement roster fixture:') {
            @('units','distinctTypes','MTNK','SREF','TTNK','FV','SCHP','HTNK','E1','CLEG','JUMPJET','SNIPE','GHOST','E2')
        } elseif ($taskPrefix -eq 'Live player mining benchmark:') {
            @('passed','nativeApiFixture','seededOnce','simplifiedIncomeEverEnabled','oreScope','harvesterId','refineryId',
                'startTick','endTick','samples','minerAlive','minerStart','minerFinal','oreInitialBails','oreFinalBails',
                'oreMinimumBails','nativePickups','seededNativePickups','capacity','peakCargoUnits','peakCargoValue','creditsInitial',
                'creditsFinal','nativeUnloadCredits','tripsInitial','tripsFinal','nativeUnloads','teleportsInitial',
                'teleportsFinal','sourceRulesHash','trajectoryHash','failure')
        } elseif ($taskPrefix -eq 'Live player mining phases:') {
            @('atomicOreUnload','IdleTicks','IdleFirstTick','SeekingOreTicks','SeekingOreFirstTick','MovingToOreTicks',
                'MovingToOreFirstTick','LoadingTicks','LoadingFirstTick','ReturningToRefineryTicks','ReturningToRefineryFirstTick',
                'WaitingForRefineryDockTicks','WaitingForRefineryDockFirstTick','DockingTicks','DockingFirstTick',
                'PreparingToUnloadTicks','PreparingToUnloadFirstTick','UnloadingTicks','UnloadingFirstTick','oreCells')
        } else {
            @('passed','units','buildings','samples','acceptedMoves','acceptedAttacks','aliveA','aliveB','deathsA','deathsB',
                'buildingsA','buildingsB','shotsA','shotsB','damageA','damageB','firstFireA','firstFireB',
                'lastFireA','lastFireB','lastAttackSample','attacks','trajectoryHash')
        }
        if ($taskMixedRoster -and $taskPrefix -eq 'Live movement benchmark:') {
            $taskKeys += @('acceptedMovesTotal','rejectedMovesTotal')
        }
        foreach ($taskValues in @($taskBefore,$taskAfter)) {
            foreach ($taskKey in $taskKeys) {
                if (!$taskValues.ContainsKey($taskKey) -or [string]::IsNullOrWhiteSpace($taskValues[$taskKey])) {
                    throw "Missing required game result: $($taskFile.Name) $taskPrefix $taskKey"
                }
            }
            if ('passed' -in $taskKeys -and $taskValues.passed -ne 'yes' -or
                'units' -in $taskKeys -and $taskValues.units -ne $taskOld.units) {
                throw "Failed or mismatched game workload: $($taskFile.Name) $taskPrefix"
            }
        }
        if ($taskPrefix -eq 'Live movement benchmark:') {
            Assert-WorkloadEvidence $taskOld $taskBefore $taskFile.FullName
            Assert-WorkloadEvidence $taskNew $taskAfter $taskNewFile
            $taskExpectedBattle = if ($taskOld.scenario -eq 'battle') { 'yes' } else { 'no' }
            if ($taskBefore.battle -ne $taskExpectedBattle -or $taskAfter.battle -ne $taskExpectedBattle) {
                throw "Scenario disagrees with movement log: $($taskFile.Name)"
            }
        }
        foreach ($taskKey in $taskKeys) {
            if ($taskBefore[$taskKey] -ne $taskAfter[$taskKey]) {
                throw "Game result differs: $($taskFile.Name) $taskPrefix $taskKey before=$($taskBefore[$taskKey]) after=$($taskAfter[$taskKey])"
            }
        }
    }
    if ($taskOld.scenario -eq 'map' -and ((Get-History $taskOld $taskFile.FullName) -gt 0 -or
        (Get-Sustain $taskOld $taskFile.FullName))) { throw "Map workload does not apply history/sustained movement: $($taskFile.Name)" }
    [pscustomobject][ordered]@{
        scenario=$taskNew.scenario; units=$taskNew.units; width=$taskNew.width; height=$taskNew.height; speed=$taskNew.speed;
        fixtureMapSide=$taskMapSide; playerScene=$taskPlayerScene; terrainFixture=(Get-Value $taskNew 'terrainFixture');
        mixedRoster=$taskMixedRoster; playerMining=$taskPlayerMining;
        baselineSha256=$taskOld.executableSha256; candidateSha256=$taskNew.executableSha256;
        baselineReferenceKeys=@(Get-Value $taskOld 'referenceKeys' @()); candidateReferenceKeys=@(Get-Value $taskNew 'referenceKeys' @());
        history=(Get-History $taskNew $taskNewFile); sustainMovement=(Get-Sustain $taskNew $taskNewFile);
        ticks=$taskNew.ticks; worldHash=$taskNew.metrics.worldHash; gameplayEquivalent=$true; displayFpsMeasured=$false;
        measuredSecondsBefore=(Get-Metric $taskOld 'measuredSeconds' $taskFile.Name); measuredSecondsAfter=(Get-Metric $taskNew 'measuredSeconds' $taskFile.Name);
        paintFpsBefore=(Get-Metric $taskOld 'paintFps' $taskFile.Name); paintFpsAfter=(Get-Metric $taskNew 'paintFps' $taskFile.Name);
        tickHzBefore=(Get-Metric $taskOld 'actualTickHz' $taskFile.Name); tickHzAfter=(Get-Metric $taskNew 'actualTickHz' $taskFile.Name);
        simMeanMsBefore=(Get-Metric $taskOld 'simMeanMs' $taskFile.Name); simMeanMsAfter=(Get-Metric $taskNew 'simMeanMs' $taskFile.Name);
        paintMeanMsBefore=(Get-Metric $taskOld 'paintMeanMs' $taskFile.Name); paintMeanMsAfter=(Get-Metric $taskNew 'paintMeanMs' $taskFile.Name);
        paintP95MsBefore=(Get-Metric $taskOld 'paintP95Ms' $taskFile.Name); paintP95MsAfter=(Get-Metric $taskNew 'paintP95Ms' $taskFile.Name);
        paintMaxMsBefore=(Get-Metric $taskOld 'paintMaxMs' $taskFile.Name); paintMaxMsAfter=(Get-Metric $taskNew 'paintMaxMs' $taskFile.Name);
        gapP95MsBefore=(Get-Metric $taskOld 'frameGapP95Ms' $taskFile.Name); gapP95MsAfter=(Get-Metric $taskNew 'frameGapP95Ms' $taskFile.Name);
        gapP99MsBefore=(Get-Metric $taskOld 'frameGapP99Ms' $taskFile.Name -Optional); gapP99MsAfter=(Get-Metric $taskNew 'frameGapP99Ms' $taskFile.Name -Optional);
        gapMaxMsBefore=(Get-Metric $taskOld 'frameGapMaxMs' $taskFile.Name); gapMaxMsAfter=(Get-Metric $taskNew 'frameGapMaxMs' $taskFile.Name);
        gap50Before=(Get-Counter $taskOld 'gapsOver50Ms' $taskFile.Name); gap50After=(Get-Counter $taskNew 'gapsOver50Ms' $taskFile.Name);
        gap100Before=(Get-Counter $taskOld 'gapsOver100Ms' $taskFile.Name); gap100After=(Get-Counter $taskNew 'gapsOver100Ms' $taskFile.Name);
        quantileSampledBefore=(Get-Value $taskOld.metrics 'quantileSampled'); quantileSampledAfter=(Get-Value $taskNew.metrics 'quantileSampled');
        candidateFrameReport=(Get-Value $taskNew 'frameReport'); candidateFrameReportExists=(Get-Value $taskNew 'frameReportExists')
    }
})
# A partially populated resolution/scenario must not borrow speed coverage from another workload.
$taskGroups = $taskRows | Group-Object scenario,units,width,height,fixtureMapSide,playerScene,mixedRoster,playerMining,history,sustainMovement,ticks
foreach ($taskGroup in $taskGroups) {
    foreach ($taskSpeed in $taskExpectedSpeeds) {
        if (@($taskGroup.Group | Where-Object { $_.speed -eq $taskSpeed }).Count -ne 1) {
            throw "Missing or duplicate expected speed $taskSpeed for workload $($taskGroup.Name)"
        }
    }
}
$taskSortedRows = @($taskRows | Sort-Object scenario,width,height,speed)
ConvertTo-Json -InputObject $taskSortedRows -Depth 5 | Set-Content -LiteralPath $OutputPath -Encoding utf8
$taskSortedRows | Select-Object speed,paintFpsBefore,paintFpsAfter,tickHzBefore,tickHzAfter,gapP95MsBefore,gapP95MsAfter,gap100Before,gap100After | Format-Table
