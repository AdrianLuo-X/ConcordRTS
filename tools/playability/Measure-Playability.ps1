param(
    [string]$Executable = '',
    [string]$Label = 'candidate',
    [ValidateSet('map','move','battle')][string]$Scenario = 'map',
    [int[]]$Speeds = @(1,2,3,4,5,6),
    [ValidateRange(320,7680)][int]$Width = 1024,
    [ValidateRange(240,4320)][int]$Height = 640,
    [ValidateSet(48,128,256)][int]$Units = 256,
    [ValidateRange(0,480)][int]$FixtureMapSide = 0,
    [switch]$PlayerScene,
    [switch]$MixedRoster,
    [switch]$PlayerMining,
    [string]$AuthoredMapPath = '',
    [switch]$KeepAuthoredWorld,
    [ValidateRange(60,216000)][int]$Ticks = 600,
    [switch]$Trace,
    [switch]$RenderDetail,
    [switch]$DenseBuildingProfile,
    [ValidateSet('CONCORD_VISION_SOURCE_REFERENCE','CONCORD_SHROUD_PERFORMANCE_REFERENCE',
        'CONCORD_OPPORTUNITY_INDEX_REFERENCE','CONCORD_SELECTION_BATCH_REFERENCE',
        'CONCORD_PAINT_GEOMETRY_REFERENCE','CONCORD_PAINT_TECH_REFERENCE',
        'CONCORD_DEPARTURE_BATCH_REFERENCE','CONCORD_EMPTY_BUILDING_DEPTH_REFERENCE',
        'CONCORD_SHADOW_SPAN_REFERENCE','CONCORD_SIM_QUERY_REFERENCE',
        'CONCORD_PAINT_BUILDING_DEPTH_REFERENCE','CONCORD_PRESENTATION_BUDGET_REFERENCE',
        'CONCORD_BUILDING_DEPTH_BUFFER_REFERENCE','CONCORD_BUILDING_DEPTH_RASTER_REFERENCE',
        'CONCORD_PAINT_TERRAIN_TILE_REFERENCE','CONCORD_HISTORY_QUERY_REFERENCE',
        'CONCORD_COMBAT_RANGE_LOS_REFERENCE','CONCORD_BUILDING_DEPTH_PIXEL_COPY_REFERENCE',
        'CONCORD_COMPONENT_PASSABILITY_REFERENCE','CONCORD_COMPONENT_DENSE_REFERENCE',
        'CONCORD_RADAR_TERRAIN_COLOR_REFERENCE','CONCORD_CHRONO_MOVE_TERRAIN_REFERENCE',
        'CONCORD_COMPONENT_KERNEL_REFERENCE','CONCORD_RADAR_RULES_COLOR_REFERENCE',
        'CONCORD_RADAR_CELL_COLORS_REFERENCE','CONCORD_PATH_QUEUE_LAYOUT_REFERENCE',
        'CONCORD_INI_TEXT_READ_REFERENCE','CONCORD_PATH_SNAPSHOT_OCCUPANCY_QUERY_REFERENCE',
        'CONCORD_PATH_PROFILE_HASH_REFERENCE','CONCORD_INFANTRY_WATER_CONTEXT_REFERENCE',
        'CONCORD_RADAR_PIXEL_STORE_REFERENCE','CONCORD_COMPONENT_TERRAIN_QUERY_REFERENCE',
        'CONCORD_INFANTRY_WATER_BATCH_CONTEXT_REFERENCE','CONCORD_PATH_SNAPSHOT_METADATA_QUERY_REFERENCE',
        'CONCORD_VOXEL_FACE_PREPARATION_REFERENCE',
        'CONCORD_COMPONENT_QUERY_LEASE_REFERENCE','CONCORD_COMPONENT_EMPTY_CENSUS_REFERENCE',
        'CONCORD_COMPONENT_CRUSH_ALIAS_REFERENCE','CONCORD_VOXEL_CORNER_PROJECTION_REFERENCE',
        'CONCORD_VISION_TERRAIN_COORDINATE_REFERENCE',
        'CONCORD_PATH_PROFILE_SPARSE_BYTES_REFERENCE','CONCORD_VOXEL_LOADING_PREWARM_REFERENCE',
        'CONCORD_VOXEL_LOADING_PREWARM_V1_REFERENCE',
        'CONCORD_SPY_DETECTION_NOOP_REFERENCE',
        'CONCORD_GROUND_SHADOW_ELIGIBILITY_REFERENCE',
        'CONCORD_VISION_LIVE_SOURCES_REFERENCE',
        'CONCORD_FUTURE_VOXEL_BOOTSTRAP_REFERENCE',
        'CONCORD_PATH_PROFILE_ZERO_RUNS_REFERENCE',
        'CONCORD_PATH_PROFILE_FIXED_SUFFIX_REFERENCE',
        'CONCORD_CAMERA_BOUNDS_PASS_REFERENCE',
        'CONCORD_VISION_TERRAIN_BATCH_REFERENCE',
        'CONCORD_COMPONENT_INDEXED_TOPOLOGY_REFERENCE',
        'CONCORD_COMPONENT_COLD_LOOKUP_REFERENCE',
        'CONCORD_PATH_QUEUE_TERRAIN_VIEW_REFERENCE',
        'CONCORD_CAMERA_GEOMETRY_MEMO_REFERENCE',
        'CONCORD_PATH_DYNAMIC_SPARSE_QUERIES_REFERENCE',
        'CONCORD_TERRAIN_VIEWPORT_FILL_REFERENCE',
        'CONCORD_PATH_DYNAMIC_CELL_BATCH_REFERENCE',
        'CONCORD_REPAIR_DEPTH_RECORDS_REFERENCE',
        'CONCORD_COMPONENT_METADATA_BORROW_REFERENCE',
        'CONCORD_SIMULATION_STAGE_ADMISSION_REFERENCE','CONCORD_PAINT_PROJECTION_SIGNATURE_REFERENCE','CONCORD_PAINT_SECRET_LAB_LIST_REFERENCE',
        'CONCORD_CELL_GATE_SCRATCH_REFERENCE','CONCORD_RADAR_SAMPLED_TERRAIN_REFERENCE','CONCORD_PATH_DYNAMIC_RESOURCE_FUSION_REFERENCE','CONCORD_MOVE_STRICT_SLOT_CERTIFICATE_REFERENCE','CONCORD_MOVE_LOCAL_COMPONENT_REFERENCE','CONCORD_MOVE_KNOWN_UNMAPPED_REFERENCE','CONCORD_MOVE_BEST_FRONTIER_CLOSURE_REFERENCE','CONCORD_PSYCHIC_DETECTION_NO_SENSOR_REFERENCE','CONCORD_PAINT_SPECIAL_OBJECT_ID_REFERENCE','CONCORD_PATH_QUEUE_WARM_ADMISSION_REFERENCE',
        'CONCORD_PRESENTATION_REMAINDER_REFERENCE',
        'CONCORD_PATH_DYNAMIC_WORKSET_REFERENCE','CONCORD_PATH_DYNAMIC_PREPARATION_REFERENCE','CONCORD_PATH_DENSE_AUTHORED_LAND_REFERENCE','CONCORD_LIGHTING_AVX2_REFERENCE',
        'CONCORD_PATH_QUEUE_LAYOUT_VIEW_REFERENCE',
        'CONCORD_COMPONENT_EMPTY_SUBCELL_BOOLEAN_REFERENCE')][string[]]$ReferenceKeys = @(),
    [ValidateSet(0,1000,10000)][int]$History = 0,
    [switch]$SustainMovement,
    [switch]$RequireFrameReport,
    [switch]$DefaultFrameReport
)
$ErrorActionPreference = 'Stop'
if ($SustainMovement -and $Scenario -ne 'move') { throw 'SustainMovement requires the move scenario.' }
if ($History -gt 0 -and $Scenario -eq 'map') { throw 'History requires the move or battle scenario.' }
if (($FixtureMapSide -gt 0 -or $PlayerScene) -and $Scenario -ne 'move') {
    throw 'Large-map/player scene fixtures require movement.'
}
if ($FixtureMapSide -gt 0 -and $FixtureMapSide -lt 96) { throw 'FixtureMapSide must be zero or at least 96.' }
if ($PlayerScene -and $FixtureMapSide -eq 0) { throw 'PlayerScene requires an explicit square map side.' }
if ($PlayerScene -and $FixtureMapSide -gt 256) { throw 'PlayerScene must fit the native diamond coordinate bounds: maximum side 256.' }
if (($MixedRoster -or $PlayerMining) -and !$PlayerScene -and !$KeepAuthoredWorld) { throw 'MixedRoster and PlayerMining require a PlayerScene.' }
if ($KeepAuthoredWorld) {
    if ($Scenario -ne 'move' -or $Units -ne 48 -or !$MixedRoster -or !$AuthoredMapPath -or
        $FixtureMapSide -ne 0 -or $PlayerScene -or $PlayerMining -or $History -ne 0) {
        throw 'KeepAuthoredWorld requires move/48/MixedRoster/AuthoredMapPath/H0 without synthetic player/mining/map flags.'
    }
    $AuthoredMapPath=(Resolve-Path -LiteralPath $AuthoredMapPath).Path
} elseif ($AuthoredMapPath) { throw 'AuthoredMapPath requires KeepAuthoredWorld.' }
if ($PlayerMining -and $Ticks -lt 2400) { throw 'PlayerMining requires at least 2400 ticks to validate a normal full cargo trip.' }

# Summary fields retain their original strings. New per-segment records use
# numbers for plotting, while hashes remain strings to preserve all 64 bits.
function Get-TaskLogRecords([string]$Log, [string]$Prefix, [switch]$Numeric) {
    foreach ($taskLine in ($Log -split '\r?\n')) {
        if (!$taskLine.StartsWith($Prefix)) { continue }
        $taskFields = [ordered]@{}
        foreach ($taskMatch in [regex]::Matches($taskLine,'([A-Za-z][A-Za-z0-9]*)=([^\s]+)')) {
            $taskName = $taskMatch.Groups[1].Value
            $taskValue = $taskMatch.Groups[2].Value
            $taskParsed = $taskValue
            if ($Numeric -and $taskName -notmatch 'Hash$') {
                $taskInteger = 0L
                $taskReal = 0.0
                if ($taskValue -match '^-?\d+$' -and [long]::TryParse($taskValue,[ref]$taskInteger)) {
                    $taskParsed = $taskInteger
                } elseif ([double]::TryParse($taskValue,[Globalization.NumberStyles]::Float,
                    [Globalization.CultureInfo]::InvariantCulture,[ref]$taskReal) -and [double]::IsFinite($taskReal)) {
                    $taskParsed = $taskReal
                }
            }
            $taskFields[$taskName] = $taskParsed
        }
        $taskFields
    }
}

function Get-TaskProcessMemorySample($Process, $Timer) {
    try {
        $Process.Refresh()
        if ($Process.HasExited) { return $null }
        $taskSample = [ordered]@{
            elapsedSeconds=$Timer.Elapsed.TotalSeconds;
            workingSetBytes=$Process.WorkingSet64;
            privateBytes=$Process.PrivateMemorySize64;
            cpuSeconds=$Process.TotalProcessorTime.TotalSeconds
        }
        if ($Process.HasExited) { return $null }
        return $taskSample
    } catch {
        # Only a confirmed child exit turns a racing property read into a
        # normal completion. Access/other errors are still reported.
        if ($Process.HasExited) { return $null }
        throw
    }
}

function Get-TaskEvidenceErrors($Metrics, $Movement, [object[]]$Segments,
    [string]$Scenario, [int]$Ticks, [int]$Units, [int]$History, [bool]$Sustain) {
    $taskErrors = [Collections.Generic.List[string]]::new()
    if ($Scenario -ne 'map') {
        if ($null -eq $Movement -or $Movement['passed'] -ne 'yes' -or $Movement['units'] -ne $Units) {
            $taskErrors.Add('Movement summary is missing, failed, or has a different unit count.')
        } else {
            if ($Movement.Contains('historyObjects')) {
                if ($Movement['historyObjects'] -ne $History) { $taskErrors.Add('Actual history count differs from the requested workload.') }
            } elseif ($History -gt 0) { $taskErrors.Add('The executable did not report the requested history workload.') }
            if ($Sustain -and ($Movement['sustainedMovement'] -ne 'yes' -or
                $Movement['commands'] -ne [int][Math]::Ceiling($Ticks / 240.0))) {
                $taskErrors.Add('Sustained movement did not report every required cyclic order.')
            }
        }
    }
    if (!$Segments.Count) {
        # Frozen original binaries have no segment output. Their ordinary
        # short comparison remains supported; a soak must provide segments.
        if ($Sustain -or $Ticks -gt 600) { $taskErrors.Add('Long/sustained workload has no per-segment evidence.') }
        return $taskErrors
    }
    $taskSegmentTicks = if ($Scenario -eq 'battle') { 120 } else { 600 }
    if ($Segments.Count -ne [int][Math]::Ceiling($Ticks / [double]$taskSegmentTicks)) {
        $taskErrors.Add('Unexpected number of completed frame segments.')
    }
    $taskTickTotal = 0L; $taskPaintTotal = 0L; $taskGap50Total = 0L; $taskGap100Total = 0L
    $taskPreviousWallEnd = 0.0
    for ($taskIndex = 0; $taskIndex -lt $Segments.Count; ++$taskIndex) {
        $taskSegment = $Segments[$taskIndex]
        $taskRequired = @('index','fromTick','throughTick','measuredTicks','wallBeginSeconds','wallEndSeconds',
            'wallSeconds','paintCount','gapsOver50Ms','gapsOver100Ms','workloadTicks','workloadSampled')
        if (@($taskRequired | Where-Object { !$taskSegment.Contains($_) }).Count) {
            $taskErrors.Add("Segment $taskIndex is missing required fields.")
            continue
        }
        $taskExpectedTicks = [Math]::Min($taskSegmentTicks,$Ticks - $taskIndex * $taskSegmentTicks)
        $taskFromTick = [long]$Metrics['warmupEndTick'] + $taskIndex * $taskSegmentTicks + 1
        if ($taskSegment['index'] -ne $taskIndex -or $taskSegment['measuredTicks'] -ne $taskExpectedTicks -or
            $taskSegment['fromTick'] -ne $taskFromTick -or
            $taskSegment['throughTick'] -ne $taskFromTick + $taskExpectedTicks - 1) {
            $taskErrors.Add("Segment $taskIndex tick interval is inconsistent.")
        }
        if ([Math]::Abs([double]$taskSegment['wallBeginSeconds'] - $taskPreviousWallEnd) -gt 0.003 -or
            [double]$taskSegment['wallEndSeconds'] -lt [double]$taskSegment['wallBeginSeconds'] -or
            [Math]::Abs(([double]$taskSegment['wallEndSeconds'] - [double]$taskSegment['wallBeginSeconds']) -
                [double]$taskSegment['wallSeconds']) -gt 0.003) {
            $taskErrors.Add("Segment $taskIndex wall interval is inconsistent.")
        }
        if ($Scenario -ne 'map' -and ($taskSegment['workloadSampled'] -ne 'yes' -or
            $taskSegment['workloadTicks'] -ne $taskSegment['measuredTicks'])) {
            $taskErrors.Add("Segment $taskIndex did not sample the mobile workload every tick.")
        }
        $taskPreviousWallEnd = [double]$taskSegment['wallEndSeconds']
        $taskTickTotal += [long]$taskSegment['measuredTicks']; $taskPaintTotal += [long]$taskSegment['paintCount']
        $taskGap50Total += [long]$taskSegment['gapsOver50Ms']; $taskGap100Total += [long]$taskSegment['gapsOver100Ms']
    }
    if ($taskTickTotal -ne $Ticks -or $taskPaintTotal -ne $Metrics['paintCount'] -or
        $taskGap50Total -ne $Metrics['gapsOver50Ms'] -or $taskGap100Total -ne $Metrics['gapsOver100Ms'] -or
        [Math]::Abs($taskPreviousWallEnd - [double]$Metrics['measuredSeconds']) -gt 0.003) {
        $taskErrors.Add('Segment totals do not agree with the session summary.')
    }
    return $taskErrors
}

$taskProject = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (!$Executable) { $Executable = Join-Path $taskProject 'build/Debug/concord_runtime.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if ($Label -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Invalid label.' }
foreach ($taskSpeed in $Speeds) {
    if ($taskSpeed -lt 0 -or $taskSpeed -gt 6) { throw 'Speed must be between zero and six.' }
}
$taskOutput = Join-Path $taskProject "build/playability/$Label"
[void](New-Item -ItemType Directory -Path $taskOutput -Force)
$taskOriginalConfig = [IO.File]::ReadAllText((Join-Path $taskProject 'local_data/runtime_config.ini'))
$taskExecutableHash = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash
$taskDiagnosticsDirectory = if ($DefaultFrameReport) { Join-Path $taskProject 'local_data/performance' }
    else { Join-Path $taskOutput 'reports' }
foreach ($taskSpeed in $Speeds) {
    $taskStem = "$Scenario-$Units-${Width}x$Height-speed$taskSpeed"
    $taskConfigPath = Join-Path $taskOutput "$taskStem.ini"
    $taskConfig = $taskOriginalConfig -replace '(?m)^GameSpeed=\d+', "GameSpeed=$taskSpeed"
    $taskConfig = $taskConfig -replace '(?m)^WindowWidth=\d+', "WindowWidth=$Width"
    $taskConfig = $taskConfig -replace '(?m)^WindowHeight=\d+', "WindowHeight=$Height"
    # The benchmark is unattended. Keep all graphics/gameplay settings while
    # avoiding audible device playback; audio correctness is tested separately.
    $taskConfig = $taskConfig -replace '(?m)^InGameMusic=yes', 'InGameMusic=no'
    $taskConfig = $taskConfig -replace '(?m)^SoundEnabled=yes', 'SoundEnabled=no'
    [IO.File]::WriteAllText($taskConfigPath, $taskConfig)
    $taskStart = [Diagnostics.ProcessStartInfo]::new()
    $taskStart.FileName = $Executable
    $taskStart.WorkingDirectory = $taskProject
    $taskStart.UseShellExecute = $false
    $taskStart.CreateNoWindow = $true
    $taskStart.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $taskStart.RedirectStandardOutput = $true
    $taskStart.RedirectStandardError = $true
    $taskStart.Environment.Clear()
    foreach ($taskEntry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
        if ($taskEntry.Key -notmatch '^CONCORD_' -and $taskEntry.Key -ine 'PATH') {
            $taskStart.Environment[$taskEntry.Key] = $taskEntry.Value
        }
    }
    $taskStart.Environment['Path'] = $env:PATH
    $taskStart.Environment['CONCORD_LIVE_BENCHMARK'] = '1'
    if (!$DefaultFrameReport) { $taskStart.Environment['CONCORD_FRAME_DIAGNOSTICS_DIR'] = $taskDiagnosticsDirectory }
    $taskStart.Environment['CONCORD_LIVE_BENCHMARK_TICKS'] = $Ticks.ToString()
    if ($FixtureMapSide -gt 0) { $taskStart.Environment['CONCORD_LIVE_BENCHMARK_MAP_SIDE'] = $FixtureMapSide.ToString() }
    if ($KeepAuthoredWorld) { $taskStart.Environment['CONCORD_LIVE_BENCHMARK_KEEP_AUTHORED_WORLD'] = '1' }
    if ($PlayerScene) { $taskStart.Environment['CONCORD_LIVE_BENCHMARK_PLAYER_SCENE'] = '1' }
    if ($MixedRoster) { $taskStart.Environment['CONCORD_LIVE_BENCHMARK_MIXED_ROSTER'] = '1' }
    if ($PlayerMining) { $taskStart.Environment['CONCORD_LIVE_BENCHMARK_PLAYER_MINING'] = '1' }
    if ($SustainMovement) { $taskStart.Environment['CONCORD_LIVE_BENCHMARK_SUSTAIN_MOVEMENT'] = '1' }
    $taskStart.Environment['CONCORD_LIVE_BENCHMARK_TIMEOUT_SECONDS'] = ([int][Math]::Min(3600,[Math]::Ceiling($Ticks / 10.0) + 90)).ToString()
    $taskStart.Environment['CONCORD_RENDER_DETAIL_STATS'] = if ($RenderDetail) { '1' } else { '0' }
    if ($DenseBuildingProfile) { $taskStart.Environment['CONCORD_DENSE_BUILDING_PROFILE'] = '1' }
    foreach ($taskReferenceKey in $ReferenceKeys) { $taskStart.Environment[$taskReferenceKey] = '1' }
    if ($Trace) {
        $taskStart.Environment['CONCORD_RUNTIME_PERF_TRACE'] = '1'
        $taskStart.Environment['CONCORD_RUNTIME_PERF_AGGREGATE'] = '1'
    }
    $taskMap = Join-Path $taskProject 'local_data/maps/concord_flat_lake.map'
    if ($Scenario -ne 'map') {
        $taskMap = if ($KeepAuthoredWorld) { $AuthoredMapPath }
            else { Join-Path $taskProject 'local_data/resources/mix_exports/multimd/2peaks.map' }
        $taskStart.Environment['CONCORD_LIVE_BENCHMARK_MOVEMENT'] = $Units.ToString()
        $taskStart.Environment['CONCORD_LIVE_BENCHMARK_MIXED'] = '1'
        $taskStart.Environment['CONCORD_LIVE_BENCHMARK_HISTORY'] = $History.ToString()
        if ($Scenario -eq 'battle') { $taskStart.Environment['CONCORD_LIVE_BENCHMARK_BATTLE'] = '1' }
    }
    foreach ($taskArgument in @('--config', $taskConfigPath, '--skip-main-menu', '--skip-skirmish-menu',
        '--resource-root', (Join-Path $taskProject 'RedAlert2'), '--local-data-root',
        (Join-Path $taskProject 'local_data'), '--map', $taskMap)) {
        $taskStart.ArgumentList.Add($taskArgument)
    }
    $taskActualMapBeforeSha256 = if ($KeepAuthoredWorld) { (Get-FileHash -LiteralPath $taskMap -Algorithm SHA256).Hash } else { $null }
    $taskTimer = [Diagnostics.Stopwatch]::StartNew()
    $taskRunStartedAt = [datetime]::UtcNow
    $taskProcess = [Diagnostics.Process]::Start($taskStart)
    $taskProcessId = $taskProcess.Id
    try {
    $taskStdout = $taskProcess.StandardOutput.ReadToEndAsync()
    $taskStderr = $taskProcess.StandardError.ReadToEndAsync()
    $taskDeadlineMs = [int][Math]::Min(7200000,[Math]::Max(180000,$Ticks * 120 + 90000))
    $taskMemorySamples = [Collections.Generic.List[object]]::new()
    $taskCompleted = $false
    while ($taskTimer.ElapsedMilliseconds -lt $taskDeadlineMs) {
        if ($taskProcess.WaitForExit(5000)) { $taskCompleted = $true; break }
        $taskMemorySample = Get-TaskProcessMemorySample $taskProcess $taskTimer
        if ($null -eq $taskMemorySample) { $taskCompleted = $true; break }
        $taskMemorySamples.Add($taskMemorySample)
    }
    if (!$taskCompleted) {
        if ($taskProcess.HasExited) { $taskCompleted = $true }
        else {
            try { $taskProcess.Kill() } catch { if (!$taskProcess.HasExited) { throw } }
            $taskProcess.WaitForExit()
        }
    }
    $taskProcessExitedElapsedSeconds = $taskTimer.Elapsed.TotalSeconds
    $taskLog = $taskStdout.GetAwaiter().GetResult() + $taskStderr.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $taskOutput "$taskStem.log"), $taskLog)
    $taskSummary = ($taskLog -split '\r?\n' | Where-Object { $_ -match '^Live frame benchmark:' } | Select-Object -Last 1)
    $taskFrameRecords = @(Get-TaskLogRecords $taskLog 'Live frame benchmark:')
    $taskMetrics = if ($taskFrameRecords.Count) { $taskFrameRecords[-1] } else { [ordered]@{} }
    $taskMovementRecords = @(Get-TaskLogRecords $taskLog 'Live movement benchmark:' -Numeric)
    $taskMovementMetrics = if ($taskMovementRecords.Count) { $taskMovementRecords[-1] } else { $null }
    $taskFrameSegments = @(Get-TaskLogRecords $taskLog 'Live frame segment:' -Numeric)
    $taskTerrainFixtures = @(Get-TaskLogRecords $taskLog 'Live movement terrain fixture:' -Numeric)
    $taskTerrainFixture = if ($taskTerrainFixtures.Count) { $taskTerrainFixtures[-1] } else { $null }
    $taskMiningRecords = @(Get-TaskLogRecords $taskLog 'Live player mining benchmark:' -Numeric)
    $taskMiningMetrics = if ($taskMiningRecords.Count) { $taskMiningRecords[-1] } else { $null }
    $taskRosterRecords = @(Get-TaskLogRecords $taskLog 'Live movement roster fixture:' -Numeric)
    $taskRosterFixture = if ($taskRosterRecords.Count) { $taskRosterRecords[-1] } else { $null }
    $taskPathLayoutRecords = @(Get-TaskLogRecords $taskLog 'Live path layout fixture:' -Numeric)
    $taskPathLayoutFixture = if ($taskPathLayoutRecords.Count) { $taskPathLayoutRecords[-1] } else { $null }
    $taskAuthoredRecords = @(Get-TaskLogRecords $taskLog 'Live authored world fixture:' -Numeric)
    $taskAuthoredFixture = if ($taskAuthoredRecords.Count) { $taskAuthoredRecords[-1] } else { $null }
    $taskActualMapAfterSha256 = if ($KeepAuthoredWorld) { (Get-FileHash -LiteralPath $taskMap -Algorithm SHA256).Hash } else { $null }
    if ($KeepAuthoredWorld -and ($taskActualMapBeforeSha256 -cne $taskActualMapAfterSha256 -or
        $null -eq $taskAuthoredFixture -or $taskAuthoredRecords.Count -ne 1 -or
        $taskAuthoredFixture.keepAuthoredWorld -ne 'yes' -or $taskAuthoredFixture.hydration -ne 'normal' -or
        $taskAuthoredFixture.worldObjects -ne 65 -or $taskAuthoredFixture.originalNeutral -ne 8 -or
        $taskAuthoredFixture.baseObjects -ne 9 -or $taskAuthoredFixture.armyUnits -ne 48 -or
        $taskAuthoredFixture.distinctTypes -ne 12 -or $taskAuthoredFixture.vision -ne 'yes' -or
        $taskAuthoredFixture.radar -ne 'yes' -or $taskAuthoredFixture.fullReveal -ne 'no' -or
        $taskAuthoredFixture.automaticHarvestPaused -ne 'no' -or $taskAuthoredFixture.passed -ne 'yes' -or
        $taskAuthoredFixture.normalStartupCompletedTick -ne 0 -or
        $taskMovementMetrics.fixtureWorldObjects -ne 65 -or $taskMovementMetrics.fixtureAliveObjects -ne 65)) {
        throw 'KeepAuthoredWorld normal hydration/geometry/census/radar/shroud/map identity evidence inconsistent.'
    }
    $taskExpectedTerrainCells = if ($PlayerScene) { 2 * $FixtureMapSide * $FixtureMapSide - $FixtureMapSide } else { $FixtureMapSide * $FixtureMapSide }
    if ($FixtureMapSide -gt 0 -and ($null -eq $taskTerrainFixture -or
        $taskTerrainFixture['width'] -ne $FixtureMapSide -or
        $taskTerrainFixture['height'] -ne $FixtureMapSide -or
        $taskTerrainFixture['terrainCells'] -ne $taskExpectedTerrainCells)) {
        throw 'Executable did not instantiate the requested large terrain fixture.'
    }
    if ($PlayerScene -and ($null -eq $taskTerrainFixture -or
        $taskTerrainFixture['playerScene'] -ne 'yes' -or $taskTerrainFixture['vision'] -ne 'yes' -or
        $taskTerrainFixture['radar'] -ne 'yes' -or
        $taskMovementMetrics['fixtureWorldObjects'] -ne $Units + 9 + $History + [int]$PlayerMining.IsPresent)) {
        throw 'Player scene did not exercise the requested radar/vision/building population.'
    }
    if ($PlayerScene -and ($taskTerrainFixture['mixedRoster'] -ne $(if ($MixedRoster) { 'yes' } else { 'no' }) -or
        $taskTerrainFixture['playerMining'] -ne $(if ($PlayerMining) { 'yes' } else { 'no' }))) {
        throw 'Actual roster/mining switches disagree with the requested player fixture.'
    }
    if ($PlayerMining -and ($null -eq $taskMiningMetrics -or $taskMiningMetrics['passed'] -ne 'yes' -or
        $taskMiningMetrics['nativeApiFixture'] -ne 'yes' -or $taskMiningMetrics['seededOnce'] -ne 'yes' -or
        $taskMiningMetrics['simplifiedIncomeEverEnabled'] -ne 'no' -or $taskMiningMetrics['samples'] -ne $Ticks + 60 -or
        $taskMiningMetrics['nativeUnloads'] -lt 1 -or $taskMiningMetrics['seededNativePickups'] -lt 20)) {
        throw 'Native mining did not finish an actual seeded-ore cargo/unload trip with every tick observed.'
    }
    if ($MixedRoster) {
        if ($null -eq $taskRosterFixture -or $taskRosterFixture['units'] -ne $Units -or
            $taskRosterFixture['distinctTypes'] -ne 12) { throw 'Native mixed roster did not hydrate the requested population.' }
        $taskRosterTotal = 0
        foreach ($taskType in @('MTNK','SREF','TTNK','FV','SCHP','HTNK','E1','CLEG','JUMPJET','SNIPE','GHOST','E2')) {
            if ($taskRosterFixture[$taskType] -lt 1) { throw "Native roster is missing $taskType." }
            $taskRosterTotal += $taskRosterFixture[$taskType]
        }
        if ($taskRosterTotal -ne $Units) { throw 'Native roster census differs from the total army population.' }
    }
    $taskEvidenceArguments = @{
        Metrics=$taskMetrics; Movement=$taskMovementMetrics; Segments=$taskFrameSegments;
        Scenario=$Scenario; Ticks=$Ticks; Units=$Units; History=$History; Sustain=$SustainMovement.IsPresent
    }
    $taskEvidenceErrors = @(Get-TaskEvidenceErrors @taskEvidenceArguments)
    $taskLatestFrameReport = Join-Path $taskDiagnosticsDirectory 'latest.txt'
    $taskSessionFrameReport = Get-ChildItem -LiteralPath $taskDiagnosticsDirectory -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "session-*-p$taskProcessId-*.txt" -and $_.LastWriteTimeUtc -ge $taskRunStartedAt } |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    $taskFrameReport = if ($taskSessionFrameReport) { $taskSessionFrameReport.FullName } else { $null }
    $taskFrameReportExists = $null -ne $taskSessionFrameReport
    [ordered]@{
        executable=$Executable; executableSha256=$taskExecutableHash; config=$taskConfigPath;
        scenario=$Scenario; units=$Units; speed=$taskSpeed; width=$Width; height=$Height;
        ticks=$Ticks; trace=$Trace.IsPresent; renderDetail=$RenderDetail.IsPresent;
        denseBuildingProfile=$DenseBuildingProfile.IsPresent;
        fixtureMapSide=$FixtureMapSide; playerScene=$PlayerScene.IsPresent;
        mixedRoster=$MixedRoster.IsPresent; playerMining=$PlayerMining.IsPresent;
        referenceKeys=$ReferenceKeys;
        historicalObjects=$History;
        sustainMovement=$SustainMovement.IsPresent;
        unitsFixtureApplied=($Scenario -ne 'map');
        keepAuthoredWorld=$KeepAuthoredWorld.IsPresent; authoredMapPath=$AuthoredMapPath;
        actualMapPath=$taskMap; actualMapSha256=$taskActualMapBeforeSha256;
        actualMapAfterSha256=$taskActualMapAfterSha256; authoredWorldFixture=$taskAuthoredFixture;
        processId=$taskProcessId; runStartedUtc=$taskRunStartedAt.ToString('o');
        processExitedElapsedSeconds=$taskProcessExitedElapsedSeconds;
        exitCode=$taskProcess.ExitCode; completed=$taskCompleted;
        elapsedSeconds=$taskTimer.Elapsed.TotalSeconds; cpuSeconds=$taskProcess.TotalProcessorTime.TotalSeconds;
        memorySamples=$taskMemorySamples;
        displayFpsMeasured=$false; metrics=$taskMetrics
        movementMetrics=$taskMovementMetrics; frameSegments=$taskFrameSegments;
        terrainFixture=$taskTerrainFixture;
        miningMetrics=$taskMiningMetrics;
        rosterFixture=$taskRosterFixture;
        pathLayoutFixture=$taskPathLayoutFixture;
        segmentEvidenceAvailable=($taskFrameSegments.Count -gt 0);
        workloadEvidencePassed=($taskEvidenceErrors.Count -eq 0); workloadEvidenceErrors=$taskEvidenceErrors;
        frameReport=$taskFrameReport; frameReportExists=$taskFrameReportExists; latestFrameReport=$taskLatestFrameReport
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskOutput "$taskStem.json") -Encoding utf8
    Write-Output "$Label $taskStem"
    Write-Output $taskSummary
    if (!$taskCompleted -or $taskProcess.ExitCode -ne 0 -or $taskSummary -notmatch 'passed=yes') {
        throw "Playability benchmark failed; inspect $taskStem.log."
    }
    if ($Scenario -ne 'map' -and $taskLog -notmatch 'Live movement benchmark: passed=yes') {
        throw 'Movement workload failed.'
    }
    if ($Scenario -eq 'battle' -and $taskLog -notmatch 'Live battle result: passed=yes') {
        throw 'Battle workload failed.'
    }
    if ($RequireFrameReport -and (!$taskFrameReportExists -or
        (Get-Item -LiteralPath $taskFrameReport).Length -eq 0)) {
        throw 'Completed game did not save the required frame diagnostics report.'
    }
    if ($taskEvidenceErrors.Count) { throw ('Benchmark evidence is inconsistent: ' + ($taskEvidenceErrors -join ' ')) }
    } finally { $taskProcess.Dispose() }
}
