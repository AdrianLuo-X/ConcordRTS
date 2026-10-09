#requires -Version 7.0
<#
.SYNOPSIS
Summarizes a completed Measure-Playability JSON without running the game.
.EXAMPLE
pwsh -File tools/Summarize-PlayabilitySoak.ps1 -InputPath build/playability/candidate8-sustain-probe/move-256-2256x1504-speed6.json
.NOTES
Performance windows retain whole segments and use their actual summed wall
time. Their duration can exceed the requested 120/300 seconds by one segment.
Per-segment P95/P99 values are retained, never averaged into a window quantile.
Memory uses process time, independently of the post-warmup benchmark clock.
#>
param(
    [Parameter(Mandatory)][Alias('InputJson')][string]$InputPath,
    [ValidateRange(1,3600)][int[]]$WindowSeconds = @(120,300),
    [ValidateRange(0,3600)][double]$MemoryWarmupSeconds = 120,
    [ValidateRange(5,10)][double]$ExitTailSeconds = 10
)
$ErrorActionPreference = 'Stop'

function Get-TaskValue($Record, [string]$Name, $Default = $null) {
    $taskProperty = $Record.PSObject.Properties[$Name]
    if ($null -eq $taskProperty -or $null -eq $taskProperty.Value) { return $Default }
    return $taskProperty.Value
}
function Get-TaskNumber($Record, [string]$Name) {
    $taskValue = Get-TaskValue $Record $Name
    $taskNumber = 0.0
    if ($null -eq $taskValue -or ![double]::TryParse([string]$taskValue,
        [Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,
        [ref]$taskNumber) -or ![double]::IsFinite($taskNumber)) {
        throw "Missing or invalid numeric field: $Name"
    }
    return $taskNumber
}
function Get-TaskSum([object[]]$Rows, [string]$Name) {
    $taskSum = 0.0
    foreach ($taskRow in $Rows) { $taskSum += Get-TaskNumber $taskRow $Name }
    return $taskSum
}
function Get-TaskExtent([object[]]$Rows, [string]$Name, [switch]$Maximum) {
    if (!$Rows.Count) { return $null }
    $taskValues = @($Rows | ForEach-Object { Get-TaskNumber $_ $Name })
    if ($Maximum) { return ($taskValues | Measure-Object -Maximum).Maximum }
    return ($taskValues | Measure-Object -Minimum).Minimum
}
function Get-TaskWeightedMean([object[]]$Rows, [string]$Name, [string]$Weight) {
    $taskTotal = 0.0; $taskWeights = 0.0
    foreach ($taskRow in $Rows) {
        $taskWeight = Get-TaskNumber $taskRow $Weight
        $taskTotal += (Get-TaskNumber $taskRow $Name) * $taskWeight
        $taskWeights += $taskWeight
    }
    if ($taskWeights -le 0) { return $null }
    return $taskTotal / $taskWeights
}
function Get-TaskPerformanceWindow([object[]]$Rows, [int]$TargetSeconds, [int]$Index) {
    $taskWall = Get-TaskSum $Rows 'wallSeconds'
    $taskTicks = [long](Get-TaskSum $Rows 'measuredTicks')
    $taskPaints = [long](Get-TaskSum $Rows 'paintCount')
    $taskGap50 = [long](Get-TaskSum $Rows 'gapsOver50Ms')
    $taskGap100 = [long](Get-TaskSum $Rows 'gapsOver100Ms')
    $taskWorkloadTicks = [long](Get-TaskSum $Rows 'workloadTicks')
    $taskMovingTicks = [long](Get-TaskSum $Rows 'movingTicks')
    $taskPainted = @($Rows | Where-Object { $_.frameGapMeasured -eq 'yes' })
    $taskPopulation = @($Rows | Where-Object { $_.populationSampled -eq 'yes' })
    $taskQuantiles = @($Rows | ForEach-Object {
        [pscustomobject][ordered]@{
            index=$_.index; wallBeginSeconds=$_.wallBeginSeconds; wallEndSeconds=$_.wallEndSeconds;
            frameGapMeasured=$_.frameGapMeasured; paintCount=$_.paintCount;
            frameGapP95Ms=$_.frameGapP95Ms; frameGapP99Ms=$_.frameGapP99Ms;
            paintP95Ms=$_.paintP95Ms; paintP99Ms=$_.paintP99Ms;
            quantileSampled=$_.quantileSampled; gapQuantileSamples=$_.gapQuantileSamples
        }
    })
    [pscustomobject][ordered]@{
        index=$Index; targetWindowSeconds=$TargetSeconds;
        windowMeasured=($taskWall -gt 0); partial=($taskWall -lt $TargetSeconds);
        segmentCount=$Rows.Count; firstSegment=$Rows[0].index; lastSegment=$Rows[-1].index;
        fromTick=$Rows[0].fromTick; throughTick=$Rows[-1].throughTick;
        wallBeginSeconds=$Rows[0].wallBeginSeconds; wallEndSeconds=$Rows[-1].wallEndSeconds;
        summedSegmentWallSeconds=$taskWall;
        wallSpanSeconds=([double]$Rows[-1].wallEndSeconds - [double]$Rows[0].wallBeginSeconds);
        measuredTicks=$taskTicks; paintCount=$taskPaints;
        actualTickHz=$(if ($taskWall -gt 0) { $taskTicks / $taskWall } else { $null });
        paintFps=$(if ($taskWall -gt 0) { $taskPaints / $taskWall } else { $null });
        gapsOver50Ms=$taskGap50; gapsOver100Ms=$taskGap100;
        gapsOver50PerSecond=$(if ($taskWall -gt 0) { $taskGap50 / $taskWall } else { $null });
        gapsOver100PerSecond=$(if ($taskWall -gt 0) { $taskGap100 / $taskWall } else { $null });
        simMeanMs=(Get-TaskWeightedMean $Rows 'simMeanMs' 'measuredTicks');
        paintMeanMs=(Get-TaskWeightedMean $Rows 'paintMeanMs' 'paintCount');
        maxFrameGapMs=(Get-TaskExtent $taskPainted 'frameGapMaxMs' -Maximum);
        maxSegmentGapP95Ms=(Get-TaskExtent $taskPainted 'frameGapP95Ms' -Maximum);
        maxSegmentGapP99Ms=(Get-TaskExtent $taskPainted 'frameGapP99Ms' -Maximum);
        windowGapP95Ms=$null; windowGapP99Ms=$null; segmentQuantiles=$taskQuantiles;
        workloadTicks=$taskWorkloadTicks;
        aliveMobileMin=$(if ($taskWorkloadTicks) { Get-TaskExtent $Rows 'aliveMobileMin' } else { $null });
        aliveMobileMean=(Get-TaskWeightedMean $Rows 'aliveMobileMean' 'workloadTicks');
        aliveMobileEnd=$(if ($taskWorkloadTicks) { $Rows[-1].aliveMobileEnd } else { $null });
        movingActiveMean=(Get-TaskWeightedMean $Rows 'movingMean' 'workloadTicks');
        movingActivePeak=$(if ($taskWorkloadTicks) { Get-TaskExtent $Rows 'movingPeak' -Maximum } else { $null });
        movingActiveTicks=$taskMovingTicks;
        movingActiveTickFraction=$(if ($taskWorkloadTicks) { $taskMovingTicks / [double]$taskWorkloadTicks } else { $null });
        populationSampleCount=$taskPopulation.Count;
        worldObjectsFirst=$(if ($taskPopulation.Count) { $taskPopulation[0].objects } else { $null });
        worldObjectsLast=$(if ($taskPopulation.Count) { $taskPopulation[-1].objects } else { $null });
        worldObjectsMin=(Get-TaskExtent $taskPopulation 'objects'); worldObjectsMax=(Get-TaskExtent $taskPopulation 'objects' -Maximum);
        aliveObjectsLast=$(if ($taskPopulation.Count) { $taskPopulation[-1].alive } else { $null });
        voxelCacheFirst=$(if ($taskPopulation.Count) { $taskPopulation[0].voxels } else { $null });
        voxelCacheLast=$(if ($taskPopulation.Count) { $taskPopulation[-1].voxels } else { $null });
        voxelCachePeak=(Get-TaskExtent $taskPopulation 'voxels' -Maximum);
        bodyPoseCacheFirst=$(if ($taskPopulation.Count) { $taskPopulation[0].bodyPoses } else { $null });
        bodyPoseCacheLast=$(if ($taskPopulation.Count) { $taskPopulation[-1].bodyPoses } else { $null });
        bodyPoseCachePeak=(Get-TaskExtent $taskPopulation 'bodyPoses' -Maximum)
    }
}
function Get-TaskPerformanceSeries([object[]]$Segments, [int]$Seconds) {
    $taskWindows = [Collections.Generic.List[object]]::new()
    $taskBucket = [Collections.Generic.List[object]]::new()
    $taskBucketSeconds = 0.0
    foreach ($taskSegment in $Segments) {
        $taskBucket.Add($taskSegment)
        $taskBucketSeconds += Get-TaskNumber $taskSegment 'wallSeconds'
        if ($taskBucketSeconds -ge $Seconds) {
            $taskWindows.Add((Get-TaskPerformanceWindow $taskBucket.ToArray() $Seconds $taskWindows.Count))
            $taskBucket.Clear(); $taskBucketSeconds = 0.0
        }
    }
    if ($taskBucket.Count) {
        $taskWindows.Add((Get-TaskPerformanceWindow $taskBucket.ToArray() $Seconds $taskWindows.Count))
    }
    [pscustomobject][ordered]@{
        targetWindowSeconds=$Seconds;
        construction='Consecutive whole segments; rates divide summed counts by summed reported wall seconds.';
        fullWindowCount=@($taskWindows | Where-Object { !$_.partial }).Count;
        partialWindowCount=@($taskWindows | Where-Object partial).Count;
        insufficientForFullWindow=(@($taskWindows | Where-Object { !$_.partial }).Count -eq 0);
        windows=$taskWindows.ToArray()
    }
}

function Get-TaskMemoryStatistics([object[]]$Samples) {
    if (!$Samples.Count) { return $null }
    $taskFirst = $Samples[0]; $taskLast = $Samples[-1]
    $taskSpan = [double]$taskLast.elapsedSeconds - [double]$taskFirst.elapsedSeconds
    $taskPrivateDelta = [long]$taskLast.privateBytes - [long]$taskFirst.privateBytes
    $taskWorkingSetDelta = [long]$taskLast.workingSetBytes - [long]$taskFirst.workingSetBytes
    $taskSlope = $null
    if ($Samples.Count -ge 2 -and $taskSpan -gt 0) {
        $taskMeanX = (Get-TaskSum $Samples 'elapsedSeconds') / $Samples.Count
        $taskMeanY = (Get-TaskSum $Samples 'privateBytes') / $Samples.Count
        $taskCross = 0.0; $taskSquare = 0.0
        foreach ($taskSample in $Samples) {
            $taskX = [double]$taskSample.elapsedSeconds - $taskMeanX
            $taskCross += $taskX * ([double]$taskSample.privateBytes - $taskMeanY)
            $taskSquare += $taskX * $taskX
        }
        if ($taskSquare -gt 0) { $taskSlope = $taskCross / $taskSquare }
    }
    [pscustomobject][ordered]@{
        sampleCount=$Samples.Count; sampleBeginSeconds=$taskFirst.elapsedSeconds; sampleEndSeconds=$taskLast.elapsedSeconds;
        observedSampleSpanSeconds=$taskSpan; trendMeasured=($Samples.Count -ge 2 -and $taskSpan -gt 0);
        privateFirstBytes=$taskFirst.privateBytes; privateLastBytes=$taskLast.privateBytes;
        privateMinBytes=(Get-TaskExtent $Samples 'privateBytes'); privateHighWaterBytes=(Get-TaskExtent $Samples 'privateBytes' -Maximum);
        privateDeltaBytes=$taskPrivateDelta;
        privateEndpointBytesPerSecond=$(if ($taskSpan -gt 0) { $taskPrivateDelta / $taskSpan } else { $null });
        privateLeastSquaresBytesPerSecond=$taskSlope;
        privateLeastSquaresMiBPerMinute=$(if ($null -ne $taskSlope) { $taskSlope * 60 / 1048576 } else { $null });
        workingSetFirstBytes=$taskFirst.workingSetBytes; workingSetLastBytes=$taskLast.workingSetBytes;
        workingSetMinBytes=(Get-TaskExtent $Samples 'workingSetBytes'); workingSetHighWaterBytes=(Get-TaskExtent $Samples 'workingSetBytes' -Maximum);
        workingSetDeltaBytes=$taskWorkingSetDelta
    }
}
function Get-TaskMemorySeries([object[]]$Samples, [int]$Seconds, [double]$Begin, [double]$End) {
    $taskWindows = [Collections.Generic.List[object]]::new()
    if ($End -gt $Begin) {
        $taskCount = [int][Math]::Ceiling(($End - $Begin) / $Seconds)
        for ($taskIndex = 0; $taskIndex -lt $taskCount; ++$taskIndex) {
            $taskFrom = $Begin + $taskIndex * $Seconds
            $taskTo = [Math]::Min($End,$taskFrom + $Seconds)
            $taskRows = @($Samples | Where-Object {
                $_.elapsedSeconds -ge $taskFrom -and ($_.elapsedSeconds -lt $taskTo -or
                    ($taskIndex -eq $taskCount - 1 -and $_.elapsedSeconds -le $taskTo))
            })
            $taskWindows.Add([pscustomobject][ordered]@{
                index=$taskIndex; targetWindowSeconds=$Seconds;
                nominalBeginSeconds=$taskFrom; nominalEndSeconds=$taskTo;
                partial=($taskTo - $taskFrom -lt $Seconds);
                statistics=(Get-TaskMemoryStatistics $taskRows)
            })
        }
    }
    [pscustomobject][ordered]@{targetWindowSeconds=$Seconds;windows=$taskWindows.ToArray()}
}

$InputPath = (Resolve-Path -LiteralPath $InputPath).ProviderPath
$taskParseParameters = @{InputObject=[IO.File]::ReadAllText($InputPath)}
if ((Get-Command ConvertFrom-Json).Parameters.ContainsKey('DateKind')) { $taskParseParameters.DateKind = 'String' }
$taskRecord = ConvertFrom-Json @taskParseParameters
if ($taskRecord.completed -ne $true -or $taskRecord.exitCode -ne 0 -or $taskRecord.metrics.passed -ne 'yes') {
    throw 'Input must be a completed, successful Measure-Playability run.'
}
if ((Get-TaskValue $taskRecord 'workloadEvidencePassed' $true) -ne $true) {
    throw 'The input workload evidence checks did not pass.'
}
$taskSegments = @($taskRecord.frameSegments)
if (!$taskSegments.Count -or $null -eq $taskSegments[0]) { throw 'Input has no frameSegments; use the updated Measure-Playability tool.' }
$taskDuration = Get-TaskNumber $taskRecord.metrics 'measuredSeconds'
$taskTickSum = 0L; $taskPaintSum = 0L; $taskGap50Sum = 0L; $taskGap100Sum = 0L
$taskPreviousEnd = 0.0
for ($taskIndex = 0; $taskIndex -lt $taskSegments.Count; ++$taskIndex) {
    $taskSegment = $taskSegments[$taskIndex]
    if ($taskSegment.index -ne $taskIndex -or
        [Math]::Abs((Get-TaskNumber $taskSegment 'wallBeginSeconds') - $taskPreviousEnd) -gt 0.003 -or
        (Get-TaskNumber $taskSegment 'wallEndSeconds') -lt (Get-TaskNumber $taskSegment 'wallBeginSeconds')) {
        throw "Non-contiguous segment evidence at index $taskIndex."
    }
    $taskPreviousEnd = Get-TaskNumber $taskSegment 'wallEndSeconds'
    $taskTickSum += [long](Get-TaskNumber $taskSegment 'measuredTicks')
    $taskPaintSum += [long](Get-TaskNumber $taskSegment 'paintCount')
    $taskGap50Sum += [long](Get-TaskNumber $taskSegment 'gapsOver50Ms')
    $taskGap100Sum += [long](Get-TaskNumber $taskSegment 'gapsOver100Ms')
}
if ($taskTickSum -ne $taskRecord.ticks -or $taskPaintSum -ne $taskRecord.metrics.paintCount -or
    $taskGap50Sum -ne $taskRecord.metrics.gapsOver50Ms -or $taskGap100Sum -ne $taskRecord.metrics.gapsOver100Ms -or
    [Math]::Abs($taskPreviousEnd - $taskDuration) -gt 0.003) { throw 'Segment totals do not match the source session.' }
$taskWindowSizes = @($WindowSeconds | Sort-Object -Unique)
if (!$taskWindowSizes.Count) { throw 'WindowSeconds must not be empty.' }
$taskPerformanceSeries = @($taskWindowSizes | ForEach-Object { Get-TaskPerformanceSeries $taskSegments $_ })

$taskMemorySamples = @(Get-TaskValue $taskRecord 'memorySamples' @() | Sort-Object elapsedSeconds)
foreach ($taskSample in $taskMemorySamples) {
    foreach ($taskName in @('elapsedSeconds','privateBytes','workingSetBytes')) {
        if ((Get-TaskNumber $taskSample $taskName) -lt 0) { throw "Invalid memory sample: $taskName" }
    }
}
$taskExitElapsed = Get-TaskValue $taskRecord 'processExitedElapsedSeconds'
$taskExitSource = 'processExitedElapsedSeconds'
if ($null -eq $taskExitElapsed) {
    $taskExitElapsed = Get-TaskNumber $taskRecord 'elapsedSeconds'
    $taskExitSource = 'elapsedSeconds fallback includes parent postprocessing; exit trim is approximate'
}
$taskMemoryEnd = [double]$taskExitElapsed - $ExitTailSeconds
$taskTrimmedMemory = @($taskMemorySamples | Where-Object {
    $_.elapsedSeconds -ge $MemoryWarmupSeconds -and $_.elapsedSeconds -le $taskMemoryEnd
})
$taskMemorySeries = @($taskWindowSizes | ForEach-Object {
    Get-TaskMemorySeries $taskTrimmedMemory $_ $MemoryWarmupSeconds $taskMemoryEnd
})
$taskMemoryReason = if ($taskMemoryEnd -le $MemoryWarmupSeconds) { 'Session too short for the requested warmup and exit exclusions.' }
    elseif ($taskTrimmedMemory.Count -lt 2) { 'Fewer than two retained memory samples.' } else { '' }
$taskOutputPath = Join-Path (Split-Path -Parent $InputPath) ([IO.Path]::GetFileNameWithoutExtension($InputPath) + '-soak-summary.json')
$taskOutput = [ordered]@{
    sourceJson=$InputPath; sourceJsonSha256=(Get-FileHash -LiteralPath $InputPath -Algorithm SHA256).Hash;
    executable=$taskRecord.executable; executableSha256=$taskRecord.executableSha256;
    processId=(Get-TaskValue $taskRecord 'processId'); runStartedUtc=(Get-TaskValue $taskRecord 'runStartedUtc');
    scenario=$taskRecord.scenario; units=$taskRecord.units; width=$taskRecord.width; height=$taskRecord.height;
    speed=$taskRecord.speed; ticks=$taskRecord.ticks; historicalObjects=(Get-TaskValue $taskRecord 'historicalObjects' 0);
    sustainMovement=(Get-TaskValue $taskRecord 'sustainMovement' $false); displayFpsMeasured=$false;
    trace=(Get-TaskValue $taskRecord 'trace' $false); renderDetail=(Get-TaskValue $taskRecord 'renderDetail' $false);
    denseBuildingProfile=(Get-TaskValue $taskRecord 'denseBuildingProfile' $false);
    referenceKeys=@(Get-TaskValue $taskRecord 'referenceKeys' @());
    measuredSeconds=$taskRecord.metrics.measuredSeconds; meets20MinuteMeasuredDuration=($taskDuration -ge 1200);
    frameReport=(Get-TaskValue $taskRecord 'frameReport'); sourceSessionMetrics=$taskRecord.metrics;
    movementMetrics=(Get-TaskValue $taskRecord 'movementMetrics');
    performance=[ordered]@{
        clock='Seconds since measured gameplay begins, after the engine 60-tick warmup.';
        windows=$taskPerformanceSeries; sourceSegments=$taskSegments;
        notes=@(
            'Window rates use sum(ticks or paints or stalls) / sum(segment wall seconds), not a mean of segment rates.',
            'Whole segments are not split; requested window duration can be exceeded by one segment. Final windows may be partial.',
            'Window P95/P99 are unavailable. Original per-segment quantiles remain separate; maxSegmentGapP95 is a maximum of segment P95 values.',
            'Weighted means use the reported segment means, which were already rounded in the game log.',
            'movingActive counts renderMoveActive flags, not units whose coordinates actually changed. Population/cache counts are segment-end samples.',
            'An entire paint gap belongs to the segment containing its completed paint; it can cross a segment boundary.'
        )
    };
    memory=[ordered]@{
        clock='Seconds since process launch; no exact clock anchor aligns these samples to frame-segment time.';
        rawSampleCount=$taskMemorySamples.Count; retainedSampleCount=$taskTrimmedMemory.Count;
        warmupExcludedSeconds=$MemoryWarmupSeconds; exitTailExcludedSeconds=$ExitTailSeconds;
        processExitElapsedSeconds=$taskExitElapsed; processExitTimeSource=$taskExitSource;
        retainedIntervalBeginSeconds=$MemoryWarmupSeconds; retainedIntervalEndSeconds=$taskMemoryEnd;
        insufficientForTrimmedTrend=($taskTrimmedMemory.Count -lt 2); insufficientReason=$taskMemoryReason;
        trimmedStatistics=(Get-TaskMemoryStatistics $taskTrimmedMemory); windows=$taskMemorySeries;
        untrimmedExploratoryStatistics=(Get-TaskMemoryStatistics $taskMemorySamples);
        rawSamples=$taskMemorySamples;
        notes=@(
            'When the session is too short, the exclusions are not relaxed; trimmed trend is unavailable and untrimmed statistics are exploratory only.',
            'Private bytes are child-process private commit; working set is OS-managed resident memory. Parent parsing buffers are outside this measurement.',
            'Endpoint changes and least-squares slopes are descriptive. Private high-water, allocator retention, or a plateau does not prove or disprove a leak.',
            'Compare memory with workload, object and cache populations; this fixed scene does not represent every spawning, destruction or resource-loading scenario.'
        )
    }
}
$taskOutput | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $taskOutputPath -Encoding utf8
Write-Output $taskOutputPath
$taskPerformanceSeries | Select-Object targetWindowSeconds,fullWindowCount,partialWindowCount,insufficientForFullWindow | Format-Table
Write-Output ("Memory retained samples=$($taskTrimmedMemory.Count); trimmed trend available=" + ($taskTrimmedMemory.Count -ge 2))
