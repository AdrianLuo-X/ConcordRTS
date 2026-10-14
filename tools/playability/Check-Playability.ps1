#requires -Version 7.0
<#
.SYNOPSIS
Legacy Debug-only candidate checks. Use tools/testing/Run-LocalTests.ps1 for the
reviewed, built-first validation profiles and machine-readable semantic results.
.EXAMPLE
pwsh -File tools/playability/Check-Playability.ps1 -Executable build/Debug/concord_runtime.exe -Label candidate
.EXAMPLE
pwsh -File tools/playability/Check-Playability.ps1 -Executable build/Debug/concord_candidate.exe -Label candidate -Tests runtime_vision_contract_test,playability_native_shroud,playability_departure_batch
.NOTES
CTest metadata supplies real commands, working directories, environments and
timeouts. Standalone smoke binaries keep their configured paths. Only the
concord_runtime.exe command is replaced. Custom rows below use verified runtime
selectors and configured command/interaction arguments; they are not CTest names.
This script does not build, delete files or edit the user's runtime configuration.
#>
param(
    [string]$Executable = '',
    [string]$Label = 'candidate',
    [string[]]$Tests = @(),
    [string]$BuildDirectory = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Tests = @($Tests | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$taskProject = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ($Label -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Label must contain only letters, numbers, underscores and hyphens.' }
if (!$Executable) { $Executable = Join-Path $taskProject 'build/Debug/concord_runtime.exe' }
if (!$BuildDirectory) { $BuildDirectory = Join-Path $taskProject 'build' }
$Executable = (Resolve-Path -LiteralPath $Executable).ProviderPath
$BuildDirectory = (Resolve-Path -LiteralPath $BuildDirectory).ProviderPath
if (!(Test-Path -LiteralPath $Executable -PathType Leaf)) { throw 'Executable must be a file.' }
# This historical runner replaces only runtime, while standalone commands stay
# Debug. Reject Release/other-directory overrides before creating any outputs.
$taskDebugDirectory = [IO.Path]::GetFullPath((Join-Path $BuildDirectory 'Debug')).TrimEnd('\')
if ([IO.Path]::GetDirectoryName($Executable).TrimEnd('\') -ine $taskDebugDirectory) {
    throw 'Check-Playability is Debug-only: Executable must be in BuildDirectory/Debug. Use tools/testing/Run-LocalTests.ps1 with explicit BuildDirectory and Configuration for Release validation.'
}
$taskCachePath = Join-Path $BuildDirectory 'CMakeCache.txt'
if (!(Test-Path -LiteralPath $taskCachePath -PathType Leaf)) { throw "No CMake cache: $taskCachePath" }
$taskCTestMatch = Select-String -LiteralPath $taskCachePath -Pattern '^CMAKE_CTEST_COMMAND:INTERNAL=(.+)$' | Select-Object -First 1
if ($null -ne $taskCTestMatch) { $taskCTest = $taskCTestMatch.Matches[0].Groups[1].Value }
else { $taskCTest = (Get-Command ctest -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source }
$taskCTest = (Resolve-Path -LiteralPath $taskCTest).ProviderPath
$taskRunId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$taskOutput = Join-Path $taskProject "build/playability/$Label/checks/$taskRunId"
[void](New-Item -ItemType Directory -Path $taskOutput -Force)
$taskResults = [Collections.Generic.List[object]]::new()
$taskVersions = @{}
$taskStarted = [DateTime]::UtcNow
$taskActualConfig = Join-Path $taskProject 'local_data/runtime_config.ini'
$taskConfigHashBefore = if (Test-Path -LiteralPath $taskActualConfig -PathType Leaf) {
    (Get-FileHash -LiteralPath $taskActualConfig -Algorithm SHA256).Hash
} else { $null }

function Get-TaskVersion([string]$Path) {
    $taskResolved = (Resolve-Path -LiteralPath $Path).ProviderPath
    if (!$taskVersions.ContainsKey($taskResolved)) {
        $taskFile = Get-Item -LiteralPath $taskResolved
        $taskVersions[$taskResolved] = [ordered]@{
            executable = $taskResolved
            sha256 = (Get-FileHash -LiteralPath $taskResolved -Algorithm SHA256).Hash
            bytes = $taskFile.Length
            lastWriteUtc = $taskFile.LastWriteTimeUtc.ToString('o')
            fileVersion = $taskFile.VersionInfo.FileVersion
            productVersion = $taskFile.VersionInfo.ProductVersion
        }
    }
    return $taskVersions[$taskResolved]
}

function Get-TaskProperty($Test, [string]$Name) {
    $taskProperty = @($Test.properties | Where-Object { $_.name -eq $Name })
    if ($taskProperty.Count) { return $taskProperty[0].value }
    return $null
}

function Invoke-TaskProcess([string]$Program, [string[]]$Arguments, [string]$WorkingDirectory,
    [hashtable]$Environment, [double]$TimeoutSeconds) {
    $taskStart = [Diagnostics.ProcessStartInfo]::new()
    $taskStart.FileName = $Program
    $taskStart.WorkingDirectory = $WorkingDirectory
    $taskStart.UseShellExecute = $false
    $taskStart.CreateNoWindow = $true
    $taskStart.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $taskStart.RedirectStandardOutput = $true
    $taskStart.RedirectStandardError = $true
    foreach ($taskArgument in $Arguments) { $taskStart.ArgumentList.Add($taskArgument) }
    $taskStart.Environment.Clear()
    foreach ($taskEntry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
        if ($taskEntry.Key -notmatch '^CONCORD_' -and $taskEntry.Key -ine 'Path') {
            $taskStart.Environment[$taskEntry.Key] = $taskEntry.Value
        }
    }
    # Windows environment keys are case insensitive. Insert exactly one Path,
    # then apply the test's explicit environment after clearing inherited modes.
    $taskStart.Environment['Path'] = [Environment]::GetEnvironmentVariable('Path')
    foreach ($taskEntry in $Environment.GetEnumerator()) {
        if ($taskEntry.Key -ieq 'Path') { $taskStart.Environment['Path'] = $taskEntry.Value }
        else { $taskStart.Environment[$taskEntry.Key] = $taskEntry.Value }
    }
    $taskTimer = [Diagnostics.Stopwatch]::StartNew()
    $taskProcess = [Diagnostics.Process]::Start($taskStart)
    try {
        $taskStdout = $taskProcess.StandardOutput.ReadToEndAsync()
        $taskStderr = $taskProcess.StandardError.ReadToEndAsync()
        $taskCompleted = $false
        while ($taskTimer.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
            if ($taskProcess.WaitForExit(1000)) { $taskCompleted = $true; break }
        }
        if (!$taskCompleted) {
            # Stop only the child started for this exact test and its descendants.
            try { $taskProcess.Kill($true) } catch { if (!$taskProcess.HasExited) { throw } }
            $taskProcess.WaitForExit()
        }
        return [ordered]@{
            completed = $taskCompleted
            exitCode = $taskProcess.ExitCode
            elapsedSeconds = $taskTimer.Elapsed.TotalSeconds
            stdout = $taskStdout.GetAwaiter().GetResult()
            stderr = $taskStderr.GetAwaiter().GetResult()
        }
    } finally { $taskProcess.Dispose() }
}

function Write-TaskSummary([string]$SetupError = '') {
    $taskConfigHashAfter = if (Test-Path -LiteralPath $taskActualConfig -PathType Leaf) {
        (Get-FileHash -LiteralPath $taskActualConfig -Algorithm SHA256).Hash
    } else { $null }
    $taskFailed = @($taskResults | Where-Object { $_.status -eq 'Failed' }).Count
    $taskSkipped = @($taskResults | Where-Object { $_.status -eq 'Skipped' }).Count
    [ordered]@{
        label = $Label; configuration = 'Debug'; buildDirectory = $BuildDirectory
        executable = $Executable; executableVersion = (Get-TaskVersion $Executable)
        ctest = $taskCTest; startedUtc = $taskStarted.ToString('o'); updatedUtc = [DateTime]::UtcNow.ToString('o')
        outputDirectory = $taskOutput; requestedTests = $Tests
        setupError = $SetupError; testsRun = $taskResults.Count
        passedTests = @($taskResults | Where-Object { $_.status -eq 'Passed' }).Count
        failedTests = $taskFailed; skippedTests = $taskSkipped
        allPassed = !$SetupError -and $taskResults.Count -gt 0 -and !$taskFailed -and !$taskSkipped -and $taskConfigHashBefore -eq $taskConfigHashAfter
        runtimeConfig = $taskActualConfig; runtimeConfigSha256Before = $taskConfigHashBefore
        runtimeConfigSha256After = $taskConfigHashAfter
        runtimeConfigUnchanged = $taskConfigHashBefore -eq $taskConfigHashAfter
        versions = @($taskVersions.Values); tests = @($taskResults.ToArray())
    } | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath (Join-Path $taskOutput 'summary.json') -Encoding utf8
}

try {
    [void](Get-TaskVersion $Executable)
    $taskMetadataRun = Invoke-TaskProcess $taskCTest @('--show-only=json-v1', '-C', 'Debug') $BuildDirectory @{} 60
    [IO.File]::WriteAllText((Join-Path $taskOutput 'ctest-metadata.json'), $taskMetadataRun.stdout)
    [IO.File]::WriteAllText((Join-Path $taskOutput 'ctest-metadata.stderr.log'), $taskMetadataRun.stderr)
    if (!$taskMetadataRun.completed -or $taskMetadataRun.exitCode -ne 0) { throw 'CTest Debug metadata query failed.' }
    $taskMetadata = $taskMetadataRun.stdout | ConvertFrom-Json
    $taskByName = @{}
    foreach ($taskTest in $taskMetadata.tests) {
        if (!($taskTest.PSObject.Properties.Name -contains 'command') -or !@($taskTest.command).Count) { continue }
        $taskProperties = @{}
        foreach ($taskProperty in $taskTest.properties) { $taskProperties[$taskProperty.name] = $taskProperty.value }
        $taskEnvironment = @{}
        if ($taskProperties.ContainsKey('ENVIRONMENT')) {
            $taskEntries = $taskProperties['ENVIRONMENT']
            if ($taskEntries -is [string]) { $taskEntries = $taskEntries -split ';' }
            foreach ($taskEntry in $taskEntries) {
                $taskEquals = $taskEntry.IndexOf('=')
                if ($taskEquals -lt 1) { throw "Invalid CTest environment for $($taskTest.name): $taskEntry" }
                $taskEnvironment[$taskEntry.Substring(0, $taskEquals)] = $taskEntry.Substring($taskEquals + 1)
            }
        }
        $taskWorkingDirectory = if ($taskProperties.ContainsKey('WORKING_DIRECTORY')) {
            [string]$taskProperties['WORKING_DIRECTORY']
        } else { $BuildDirectory }
        $taskTimeout = if ($taskProperties.ContainsKey('TIMEOUT') -and [double]$taskProperties['TIMEOUT'] -gt 0) {
            [double]$taskProperties['TIMEOUT']
        } else { 300.0 }
        $taskByName[$taskTest.name] = [ordered]@{
            name = $taskTest.name; source = 'ctest'; sourceTest = $taskTest.name
            command = @($taskTest.command); workingDirectory = $taskWorkingDirectory
            environment = $taskEnvironment; timeoutSeconds = $taskTimeout
            configuredTimeout = Get-TaskProperty $taskTest 'TIMEOUT'
            properties = $taskProperties
        }
    }
    # The water selector now retains its original contracts and also checks
    # three independent four-actor authorities at 15/30/60 Hz. Allow that
    # additional work to finish without changing any success assertion.
    if ($taskByName.ContainsKey('runtime_infantry_water_sound_smoke_test')) {
        $taskByName['runtime_infantry_water_sound_smoke_test'].timeoutSeconds =
            [Math]::Max(600.0, $taskByName['runtime_infantry_water_sound_smoke_test'].timeoutSeconds)
    }
    # These names were read from this project's CMake/CTest configuration.
    # Missing optional standalone targets are omitted from the default list;
    # an explicitly requested absent name is always an error.
    $taskDefaultNames = @(
        'runtime_vision_contract_test', 'runtime_render_smoke_test',
        'runtime_shadow_opaque_span_test', 'runtime_water_wake_history_test',
        'runtime_frame_diagnostics_test', 'runtime_game_speed_timing_test',
        'runtime_presentation_work_budget_test','runtime_simulation_query_reuse_test',
        'runtime_history_owner_census_test','runtime_mod_history_noop_test',
        'runtime_subcell_requested_index_smoke_test',
        'runtime_building_depth_core_test', 'runtime_naval_scene_depth_core_test',
        'runtime_lighting_lookup_raster_smoke_test', 'runtime_body_tint_context_smoke_test',
        'runtime_combat_details_smoke_test', 'runtime_movement_classification_smoke_test',
        'runtime_vehicle_arrival_motion_smoke_test', 'runtime_vehicle_arrival_integration_smoke_test',
        'runtime_guard_rotation_smoke_test', 'runtime_stock_movement_cadence_smoke_test',
        'runtime_terrain_movement_smoke_test', 'runtime_subcell_selective_batch_smoke_test',
        'runtime_subcell_selective_batch_reference_smoke_test', 'path_request_queue_performance_smoke_test',
        'game_world_deterministic_hash_smoke_test', 'runtime_multiplayer_authoritative_hash_smoke_test'
    )
    # The selectors are real handlers in RuntimeShell.cpp. Clone configured
    # arguments, replace conflicting base selectors with this row's one mode.
    $taskCustomRows = @(
        @('playability_paint_secret_lab_list', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_PAINT_SECRET_LAB_LIST_ONLY'),
        @('playability_frame_overlay', 'runtime_interaction_all01umd_smoke_test', 'CONCORD_INTERACTION_SMOKE_FRAME_OVERLAY_ONLY'),
        @('playability_camera_bounds', 'runtime_interaction_all01umd_smoke_test', 'CONCORD_INTERACTION_SMOKE_CAMERA_BOUNDS_ONLY'),
        @('playability_camera_geometry_memo', 'runtime_interaction_all01umd_smoke_test', 'CONCORD_INTERACTION_SMOKE_CAMERA_GEOMETRY_MEMO_ONLY'),
        @('playability_radar_colors', 'runtime_render_smoke_test', 'CONCORD_RENDER_SMOKE_RADAR_COLOR_ONLY'),
        @('playability_radar_rules_colors', 'runtime_render_smoke_test', 'CONCORD_RENDER_SMOKE_RADAR_RULES_COLOR_ONLY'),
        @('playability_voxel_face_preparation', 'runtime_render_smoke_test', 'CONCORD_RENDER_SMOKE_VOXEL_FACE_PREPARATION_ONLY'),
        @('playability_voxel_loading_prewarm', 'runtime_render_smoke_test', 'CONCORD_RENDER_SMOKE_VOXEL_LOADING_PREWARM_ONLY'),
        @('playability_voxel_loading_world_save', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_VOXEL_LOADING_PREWARM_WORLD_ONLY'),
        @('playability_shadow_contract', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_FRAME_RENDER_ONLY'),
        @('playability_native_shroud', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_NATIVE_SHROUD_ONLY'),
        @('playability_combat_opportunity', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_COMBAT_OPPORTUNITY_ONLY'),
        @('playability_departure_batch', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_DEPARTURE_BATCH_ONLY'),
        @('playability_simulation_query_reuse', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_SIM_QUERY_REUSE_ONLY'),
        @('playability_component_passability', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_COMPONENT_PASSABILITY_ONLY'),
        @('playability_component_query_lease', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_COMPONENT_QUERY_LEASE_ONLY'),
        @('playability_vision_terrain_coordinate', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_VISION_TERRAIN_COORDINATE_ONLY'),
        @('playability_vision_terrain_batch', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_VISION_TERRAIN_BATCH_ONLY'),
        @('playability_component_terrain_queries', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_COMPONENT_TERRAIN_QUERY_ONLY'),
        @('playability_component_metadata_borrow', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_COMPONENT_METADATA_BORROW_ONLY'),
        @('playability_cell_gate_scratch', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_CELL_GATE_SCRATCH_ONLY'),
        @('playability_strict_slot_vehicle', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_STRICT_SLOT_VEHICLE_ONLY'),
        @('playability_mixed_local_component', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_MIXED_LOCAL_COMPONENT_ONLY'),
        @('playability_psychic_no_sensor', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_PSYCHIC_DETECTION_NO_SENSOR_ONLY'),
        @('playability_simulation_stage_admission', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_SIMULATION_STAGE_ADMISSION_ONLY'),
        @('playability_component_indexed_topology', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_COMPONENT_INDEXED_TOPOLOGY_ONLY'),
        @('playability_component_cold_lookup', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_COMPONENT_COLD_LOOKUP_ONLY'),
        @('playability_path_snapshot_metadata_queries', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_PATH_SNAPSHOT_METADATA_QUERY_ONLY'),
        @('playability_path_dynamic_workset', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_PATH_DYNAMIC_WORKSET_ONLY'),
        @('playability_component_empty_subcell_boolean', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_COMPONENT_EMPTY_SUBCELL_BOOLEAN_ONLY'),
        @('playability_path_dynamic_resource_fusion', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_PATH_DYNAMIC_RESOURCE_FUSION_ONLY'),
        @('playability_path_dynamic_preparation', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_PATH_DYNAMIC_PREPARATION_ONLY'),
        @('playability_path_dense_authored_land', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_PATH_DENSE_AUTHORED_LAND_ONLY'),
        @('playability_shipyard_movement_load', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_SHIPYARD_DEPTH_ONLY'),
        @('playability_barracks_crowd_load', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_BARRACKS_CROWD_ONLY'),
        @('playability_manual_formation_mapped', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_MANUAL_FORMATION_MAPPED_ONLY'),
        @('playability_chrono_move_terrain', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_CHRONO_MOVE_TERRAIN_ONLY'),
        @('playability_history_queries', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_HISTORY_QUERIES_ONLY'),
        @('playability_grinder_animation', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_GRINDER_ANIMATION_ONLY'),
        @('playability_building_sale', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_BUILDING_SALE_ONLY'),
        @('playability_ground_occupancy', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_GROUND_OCCUPANCY_ONLY'),
        @('playability_path_snapshot', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_PATH_SNAPSHOT_ONLY'),
        @('playability_movement_empty_proof', 'runtime_guard_rotation_smoke_test', 'CONCORD_COMMAND_SMOKE_MOVEMENT_EMPTY_PROOF_ONLY')
    )
    foreach ($taskRow in $taskCustomRows) {
        if (!$taskByName.ContainsKey($taskRow[1])) { continue }
        $taskBase = $taskByName[$taskRow[1]]
        $taskByName[$taskRow[0]] = [ordered]@{
            name = $taskRow[0]; source = 'verified-runtime-selector'; sourceTest = $taskRow[1]
            command = @($taskBase.command); workingDirectory = $taskBase.workingDirectory
            environment = @{ $taskRow[2] = '1' }; timeoutSeconds = $(if ($taskRow[0] -eq 'playability_mixed_local_component') { 7200.0 } elseif ($taskRow[0] -in @('playability_paint_secret_lab_list','playability_simulation_stage_admission','playability_mixed_local_component','playability_path_dynamic_resource_fusion','playability_component_metadata_borrow','playability_cell_gate_scratch','playability_path_dynamic_workset','playability_component_empty_subcell_boolean','playability_path_dynamic_preparation','playability_path_dense_authored_land')) { 1200.0 } elseif ($taskRow[0] -in @('playability_component_passability','playability_simulation_query_reuse','playability_component_terrain_queries','playability_component_indexed_topology','playability_component_cold_lookup','playability_path_snapshot_metadata_queries','playability_strict_slot_vehicle')) { 600.0 } else { 300.0 })
            configuredTimeout = $null; properties = @{}
        }
        if ($taskRow[0] -eq 'playability_shipyard_movement_load') {
            $taskByName[$taskRow[0]].environment['CONCORD_SHIPYARD_LIFECYCLE_ONLY'] = '1'
        }
        if ($taskRow[0] -eq 'playability_barracks_crowd_load') {
            # This existing fixture cuts the authored land at (92..140,
            # 198..244) from concord_flat_lake; dryheat has no such exit.
            $taskMapIndex = [Array]::IndexOf($taskByName[$taskRow[0]].command, '--map')
            if ($taskMapIndex -lt 0 -or $taskMapIndex + 1 -ge $taskByName[$taskRow[0]].command.Count) {
                throw 'Barracks crowd source command has no replaceable map argument.'
            }
            $taskByName[$taskRow[0]].command[$taskMapIndex + 1] =
                Join-Path $taskProject 'local_data/maps/concord_flat_lake.map'
        }
        $taskDefaultNames += $taskRow[0]
    }
    $taskAvailableNames = @($taskByName.Keys | Sort-Object)
    $taskAvailableNames | Set-Content -LiteralPath (Join-Path $taskOutput 'available-tests.txt') -Encoding utf8
    if ($Tests.Count) {
        foreach ($taskName in $Tests) {
            if (!$taskByName.ContainsKey($taskName)) { throw "Unknown or unavailable test '$taskName'; see available-tests.txt." }
        }
        $taskSelected = @($Tests | Select-Object -Unique)
    } else {
        $taskSelected = @($taskDefaultNames | Where-Object { $taskByName.ContainsKey($_) })
    }
    if (!$taskSelected.Count) { throw 'No applicable tests are configured.' }
    $taskSelected | Set-Content -LiteralPath (Join-Path $taskOutput 'selected-tests.txt') -Encoding utf8
    foreach ($taskName in $taskSelected) {
        $taskTest = $taskByName[$taskName]
        $taskLogPath = Join-Path $taskOutput "$taskName.log"
        $taskCommand = @($taskTest.command)
        $taskProgram = [string]$taskCommand[0]
        if ([IO.Path]::GetFileName($taskProgram) -ieq 'concord_runtime.exe') { $taskProgram = $Executable }
        $taskArguments = if ($taskCommand.Count -gt 1) { @($taskCommand[1..($taskCommand.Count - 1)]) } else { @() }
        $taskResult = [ordered]@{
            name = $taskName; source = $taskTest.source; sourceTest = $taskTest.sourceTest
            executable = $taskProgram; command = @($taskProgram) + $taskArguments
            workingDirectory = $taskTest.workingDirectory; environment = $taskTest.environment
            timeoutSeconds = $taskTest.timeoutSeconds; configuredTimeout = $taskTest.configuredTimeout
            status = 'Failed'; reason = ''; completed = $false; exitCode = $null
            elapsedSeconds = 0.0; log = $taskLogPath; executableSha256 = ''
            repairRecordsCompletedTick = $null
        }
        try {
            $taskProgram = (Resolve-Path -LiteralPath $taskProgram).ProviderPath
            $taskResult.executable = $taskProgram
            $taskResult.executableSha256 = (Get-TaskVersion $taskProgram).sha256
            $taskRun = Invoke-TaskProcess $taskProgram $taskArguments $taskTest.workingDirectory $taskTest.environment $taskTest.timeoutSeconds
            $taskResult.completed = $taskRun.completed; $taskResult.exitCode = $taskRun.exitCode
            $taskResult.elapsedSeconds = $taskRun.elapsedSeconds
            $taskLog = $taskRun.stdout + "`n[stderr]`n" + $taskRun.stderr
            [IO.File]::WriteAllText($taskLogPath, $taskLog)
            $taskPassed = $taskRun.completed -and $taskRun.exitCode -eq 0
            if ($taskTest.properties.ContainsKey('WILL_FAIL') -and $taskTest.properties['WILL_FAIL']) {
                $taskPassed = $taskRun.completed -and $taskRun.exitCode -ne 0
            }
            if (!$taskRun.completed) { $taskResult.reason = 'timeout' }
            elseif (!$taskPassed) { $taskResult.reason = 'unexpected exit code' }
            foreach ($taskPattern in @($taskTest.properties['FAIL_REGULAR_EXPRESSION'])) {
                if ($taskPattern -and $taskLog -match $taskPattern) { $taskPassed = $false; $taskResult.reason = 'CTest failure expression matched' }
            }
            $taskPassExpressions = @($taskTest.properties['PASS_REGULAR_EXPRESSION']) | Where-Object { $_ }
            if ($taskPassExpressions) {
                $taskMatched = @($taskPassExpressions | Where-Object { $taskLog -match $_ }).Count -gt 0
                if (!$taskMatched) { $taskPassed = $false; $taskResult.reason = 'CTest pass expression missing' }
            }
            if ($taskPassed -and $taskName -eq 'path_request_queue_performance_smoke_test') {
                $taskQueueGold = [regex]::Matches($taskLog, '(?m)^ordinary=12452642976107788296 blocked=6314123529439397177 reversed=1587240991571403433 irregular=7135364384096626644\r?$').Count
                $taskQueueGoldFooters = [regex]::Matches($taskLog, '(?m)^ordinary=').Count
                $taskQueueReadOnly = [regex]::Matches($taskLog, '(?m)^readonly terrain complete oracle passed constructions=3648 positiveBorrows=112 completeResultChecks=88658 ordinaryCtorOwned=yes copyOwns=yes temporaryRejected=yes reversedUnique=yes permutedUnique=yes nativeCells=114960\r?$').Count
                $taskQueueReadOnlyFooters = [regex]::Matches($taskLog, '(?m)^readonly terrain complete oracle').Count
                $taskQueueShared = [regex]::Matches($taskLog, '(?m)^shared layout complete oracle passed constructions=336 positiveShares=6 completeResultChecks=10139 cacheClearSwapRebuild=yes cacheDestroyed=yes copyOwns=yes moveRetainsOwner=yes reversedUnique=yes permutedUnique=yes nativeCells=114960 oldGoldUnchanged=yes benchmarkPrepared=no\r?$').Count
                $taskQueueSharedFooters = [regex]::Matches($taskLog, '(?m)^shared layout complete oracle').Count
                $taskQueueProfile = [regex]::Matches($taskLog, '(?m)^profile hash queue equivalence passed layoutHashCombinations=16 constructions=960 completeResultChecks=39552 completeStats=yes queueOwned=yes originalProfileReference=yes zeroRunFlagBoth=yes admissionFieldPerturbations=13\r?$').Count
                $taskQueueProfileFooters = [regex]::Matches($taskLog, '(?m)^profile hash queue equivalence').Count
                if ($taskQueueGold -ne 1 -or $taskQueueGoldFooters -ne 1 -or
                    $taskQueueReadOnly -ne 2 -or $taskQueueReadOnlyFooters -ne 2 -or
                    $taskQueueShared -ne 1 -or $taskQueueSharedFooters -ne 1 -or
                    $taskQueueProfile -ne 1 -or $taskQueueProfileFooters -ne 1) {
                    $taskPassed = $false; $taskResult.reason = 'Queue original gold, both readonly reference matrices, immutable shared layout lifetime/complete state and profile reference completion records missing'
                }
            }
            # Older runtimes ignore unknown environment selectors. Require the
            # two independent completion records so that running a different
            # command smoke cannot be mistaken for this new contract.
            if ($taskPassed -and $taskName -eq 'playability_camera_bounds' -and
                $taskLog -notmatch '(?m)^Camera combined bounds: actualLoadedMap=yes syntheticFixtures=17 boundsPairs=104 boundsFields=5 revealPairs=104 revealFields=4 clampPairs=5824 reference/TLS/mutations/oddZeroViewports=exact passed=yes\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Camera bounds five-field/reveal/clamp/reference completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_camera_geometry_memo' -and
                $taskLog -notmatch '(?m)^Camera geometry memo actual: actualLoadedTerrain=yes combinedClampPairs=5824 envelopePairs=65 envelopeFields=10 coldMisses=31 hotHits=34 geometryMisses=8 projectionMisses=12 dimensionMisses=6 irrelevantHits=3 copyMoveChecks=9 referenceChecks=4 fullSave/WorldHash/TLS=exact passed=yes\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Camera geometry memo actual completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_component_indexed_topology' -and
                $taskLog -notmatch '(?m)^Planner indexed topology geometry: exactOrderedTransitions=745920 exactGeometryPayloads=2592 completeOrderedComponents=168 boundaryCases=19 actualNative240Commands=18 perTickFullSaveHashes=96 geometryPairs=[1-9][0-9]* H0/H1000/H10000=yes originalDynamicOrderAndGlobalTie=yes pass\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Indexed topology ordered component and actual command completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_component_cold_lookup' -and
                $taskLog -notmatch '(?m)^Planner cold scalar lookups: exactTypedPointers=4096 boundaryCases=37 exactOrderedTransitions=100800 completeOrderedComponents=96 actualNative240Commands=18 perTickFullSaveHashes=96 lookupViews=[1-9][0-9]* factsQueries=[1-9][0-9]* stateTerrainQueries=[1-9][0-9]* suppliedTerrainQueries=[1-9][0-9]* metadataQueries=[1-9][0-9]* H0/H1000/H10000=yes validationOnceAndOriginalDynamicOrder=yes pass\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Cold scalar typed lookup, ordered component and actual command completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_path_dynamic_resource_fusion') {
                $taskResourceMarker = [regex]::Match($taskLog, '(?m)^Path dynamic resource fusion: syntheticCases=324 nativeScenarios=8 fields=12 fullFieldCells=[1-9][0-9]* queueResults=[1-9][0-9]* queueStats=[1-9][0-9]* nativeCommands=24 nativeTickTriplets=64 positiveNative=8 nativeResourceRows=(?<native>[1-9][0-9]*) resourceRows=(?<rows>[1-9][0-9]*) resourceVisits=(?<visits>[1-9][0-9]*) bodyVisits=[1-9][0-9]* observerFallbacks=4 earlyLate=2 nonfiniteFallbacks=3 currentLandValues=2 simulationOwners=[1-9][0-9]* simulationResources=[1-9][0-9]* simulationBodyVisits=[1-9][0-9]* original/C35/fusion/order/profile/cache/paths/fullWriter/resource70/resource90/native240/native256/H0/H10000=exact passed=yes\r?$')
                if (-not $taskResourceMarker.Success -or [regex]::Matches($taskLog, '(?m)^Path dynamic resource fusion:').Count -ne 1 -or
                    $taskResourceMarker.Groups['rows'].Value -cne $taskResourceMarker.Groups['visits'].Value -or
                    [uint64]$taskResourceMarker.Groups['native'].Value -lt 80000) {
                    $taskPassed = $false; $taskResult.reason = 'Resource fusion original math/all fields/Queue/full writer/native >=10k resource rows completion proof missing'
                }
            }
            if ($taskPassed -and $taskName -eq 'playability_path_dense_authored_land') {
                $taskDenseLandMarker=[regex]::Match($taskLog,'(?m)^Dense authored land actual: cases=4 actors=608 stockTypes=48 warmTickPairs=240 commands=8 completedTickPairs=64 trajectoryPairs=304 snapshotPairs=108 fields=12 fullFieldCells=[1-9][0-9]* denseAdmitted=108 pointerReferenceOwners=108 warmCachePairs=108 pureWriterPairs=216 denseCandidates=[1-9][0-9]* denseOmitted=[1-9][0-9]* valueWrites=16 structureWrites=8 copyPairs=4 loadPairs=2 postLoadSnapshots=4 queueResults=[1-9][0-9]* queueStats=[1-9][0-9]* enabled-current/aliases/unknown127/holes/copy/load/paths/fullWriter=yes passed=yes\r?$')
                if (-not $taskDenseLandMarker.Success -or [regex]::Matches($taskLog,'(?m)^Dense authored land actual:').Count -ne 1) {
                    $taskPassed=$false; $taskResult.reason='Dense authored land actual dispatched AND admitted route/full fields/retained writes/structure/normal load/Queue/full writer proof missing'
                }
            }
            if ($taskPassed -and $taskName -eq 'playability_path_dense_authored_land') {
                if ($taskLog -notmatch '(?m)^Saved psychic complete-world regression: countBoundaries=6 originalReportPairs=32 malformedTailRecords=12 invalidBudgets=4 largeActiveStatusCases=2 customLimitPairs=2 genericInputCap=4096 savedWorldCap=100000 allRows/duplicates/negativeIDs/type/owner/missing/deadSource/deadTarget/actionOrder/statuses/hitEvents/textLimits/persistentHash=exact passed=yes\r?$' -or
                    $taskLog -notmatch '(?m)^Saved psychic public-load atomic rejection: nativeWorldObjects=10057 malformedFiles=2 duplicateIDs=1 negativeIDs=1 completeWriterComparisons=2 sourceFilePreserved=yes liveStateUnchanged=yes passed=yes\r?$') {
                    $taskPassed = $false; $taskResult.reason = 'Saved psychic complete-world cap, unchanged independent limits, full original reports and H10000 malformed public-load atomic writer proof missing'
                }
            }
            if ($taskPassed -and $taskName -eq 'playability_path_dynamic_preparation') {
                $taskPreparationMarker = [regex]::Match($taskLog, '(?m)^Path dynamic preparation: syntheticCases=252 nativeScenarios=8 fullFieldCells=[1-9][0-9]* fields=12 queueResults=[1-9][0-9]* queueStats=[1-9][0-9]* nativeCommands=24 nativeTickTriplets=64 nativeAdmitted=8 nativeCandidates=(?<candidates>[1-9][0-9]*) nativeOmitted=[1-9][0-9]* nativeBodyVisits=(?<visits>[1-9][0-9]*) observerFallbacks=2 independentFallbacks=6 currentValueBuilds=2 nativeSimulationOwners=[1-9][0-9]* nativeSimulationAdmitted=[1-9][0-9]* nativeSimulationOmitted=[1-9][0-9]* preparationOwners=786 earlyOwners=[1-9][0-9]* boundsRowsAvoided=[1-9][0-9]* infantryRangeEntries=(?<range>[1-9][0-9]*) infantryOriginalRangeEntries=(?<original>[1-9][0-9]*) infantryDestinations=[1-9][0-9]* infantryRangeReduced=yes original/C33/preparation/order/profile/cache/paths/fullWriter/subcells/native240/native256/H0/H10000=exact passed=yes\r?$')
                if (-not $taskPreparationMarker.Success -or [regex]::Matches($taskLog, '(?m)^Path dynamic preparation:').Count -ne 1 -or
                    $taskPreparationMarker.Groups['candidates'].Value -ne $taskPreparationMarker.Groups['visits'].Value -or
                    [uint64]$taskPreparationMarker.Groups['range'].Value -ge [uint64]$taskPreparationMarker.Groups['original'].Value) {
                    $taskPassed = $false; $taskResult.reason = 'Path preparation exact bounds/ordered infantry ranges/full fields/native commands/full-save triplets completion record missing'
                }
            }
            if ($taskPassed -and $taskName -in @('playability_path_dynamic_workset','playability_path_dynamic_preparation','playability_path_dynamic_resource_fusion')) {
                $taskWorksetMarker = [regex]::Match($taskLog, '(?m)^Path dynamic workset: syntheticCases=180 nativeScenarios=8 fullFieldCells=[1-9][0-9]* fields=12 queueResults=[1-9][0-9]* queueStats=[1-9][0-9]* nativeCommands=24 nativeTickTriplets=64 nativeAdmitted=8 nativeCandidates=(?<candidates>[1-9][0-9]*) nativeOmitted=[1-9][0-9]* nativeBodyVisits=(?<visits>[1-9][0-9]*) observerFallbacks=2 independentFallbacks=6 currentValueBuilds=2 nativeSimulationOwners=[1-9][0-9]* nativeSimulationAdmitted=[1-9][0-9]* nativeSimulationOmitted=[1-9][0-9]* original/C31/workset/order/profile/cache/paths/fullWriter/native240/native256/H0/H10000=exact passed=yes\r?$')
                $taskWorksetFooterCount = [regex]::Matches($taskLog, '(?m)^Path dynamic workset:').Count
                if ($taskWorksetFooterCount -ne 1 -or !$taskWorksetMarker.Success -or $taskWorksetMarker.Groups['candidates'].Value -cne $taskWorksetMarker.Groups['visits'].Value) {
                    $taskPassed = $false; $taskResult.reason = 'Path workset ordered twelve fields, full Queue/profile/cache, original body counts and native completed tick full-save contract completion record missing'
                }
            }
            if ($taskPassed -and $taskName -eq 'playability_component_empty_subcell_boolean') {
                $taskEmptyMarker = [regex]::Match($taskLog, '(?m)^Planner empty subcell boolean: nativeCases=4 completeGraphs=8 nativeCommands=8 perTickFullSavePairs=32 observerFallbacks=2 nativePositiveCalls=[1-9][0-9]* fullSaveChecks=[1-9][0-9]* originalDirected/order/fields/paths/fullWriter/subcells/native240/native256/H0/H10000=exact passed=yes\r?$')
                if ([regex]::Matches($taskLog, '(?m)^Planner empty subcell boolean:').Count -ne 1 -or !$taskEmptyMarker.Success) {
                    $taskPassed = $false; $taskResult.reason = 'Empty subcell boolean actual complete ordered graphs, commands, completed ticks and old observer fallback proof missing'
                }
            }
            if ($taskPassed -and $taskName -eq 'playability_manual_formation_mapped' -and
                ($taskLog -notmatch '(?m)^Manual formation native256: oldAccepted=255 oldIllegal=CLEG211 fixedAccepted=256 fixedAllLandingsLegal=yes fullsave=yes originalRadius8=yes tick=61\r?$' -or
                 $taskLog -notmatch '(?m)^Manual formation Mapped contract passed: actualQueries=[1-9][0-9]* actualCommands=[1-9][0-9]* fullSaveChecks=[1-9][0-9]* stockNative256=yes airSpecial=yes slaveSpecial=yes automaticDefaults=yes reShroud=yes SeeHiddenObjectsVsMapped=yes\r?$')) {
                $taskPassed = $false; $taskResult.reason = 'Manual Mapped native256 bug witness and complete contract completion records missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_paint_secret_lab_list' -and (
                [regex]::Matches($taskLog, '(?m)^Paint secret lab list:').Count -ne 1 -or
                $taskLog -notmatch '(?m)^Paint secret lab list: boundaries=29 uniqueQueries=56 pairs=4 originalScansOmitted=52 positiveLabs=2 fullWriterCaptures=12 completedTick=0 actualSidebarPairs=4 actualSidebarHits=[1-9][0-9]* originalPublicAPI/raw32/353stats/deepResources/radar=exact passed=yes\r?$')) {
                $taskPassed = $false; $taskResult.reason = 'Secret lab readonly phase requires original complete API lists, actual fourteen unique queries, four real sidebar raw32/353/deep-resource/radar pairs and twelve completed-tick full writers'
            }
            if ($taskPassed -and $taskName -eq 'playability_frame_overlay' -and (
                [regex]::Matches($taskLog, '(?m)^Paint projection signature:').Count -ne 1 -or
                $taskLog -notmatch '(?m)^Paint projection signature: boundaries=28 liveFields=64 pairs=12 actualHits=8 originalComputationsOmitted=8 fullSignature/raw32/353stats/deepResources/pointerOrder/world/vision=exact passed=yes\r?$')) {
                $taskPassed = $false; $taskResult.reason = 'Paint projection signature requires complete original hash, 28 lease/mutation boundaries, 64 live fields and twelve raw32/full353/deep-resource pairs with eight actual capture reuses'
            }

            if ($taskPassed -and $taskName -eq 'playability_component_query_lease' -and
                ($taskLog -notmatch '(?m)^Planner query lease/census: .+ pass\r?$' -or
                 $taskLog -notmatch '(?m)^Planner query lease/census actual stock commands=.+ pass\r?$')) {
                $taskPassed = $false; $taskResult.reason = 'Lease/census contract completion records missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_simulation_stage_admission' -and (
                $taskLog -notmatch '(?m)^Simulation stage admission original integrations: aircraftRoutes=2 subcellRoutes=2 unchangedContracts=yes passed=yes\r?$' -or
                $taskLog -notmatch '(?m)^Simulation stage admission reserve: sizes=8 completePairs=8 growthCases=4 eagerCap=256 nestedReferenceRestore=yes orderedValues/membership/unlimitedGrowth=exact passed=yes\r?$' -or
                $taskLog -notmatch '(?m)^Simulation stage admission subcell: cases=24 legacyComparisons=48 reassignmentWitness=[1-9][0-9]* needsMoveOutWitness=[1-9][0-9]* deadCargo/duplicates/negativeIDs/health0/air/mission/bridge/gate/terrain/reservations/fullData/actions/order=exact passed=yes\r?$' -or
                $taskLog -notmatch '(?m)^Simulation stage admission aircraft: cases=16 legacyComparisons=32 noHostCases=7 callbackEvents=[1-9][0-9]* actionEvents=[1-9][0-9]* mutableCallbackComparisons=1 staleBindings/deadFirst/negativeID/duplicates/air/dockLimits/allCallbacks/fullSave/report/actions/tickClock/order=exact passed=yes\r?$' -or
                $taskLog -notmatch '(?m)^Simulation stage admission actual: nativeCases=4 nativeActors=608 stockTypeReadbacks=48 historyObjects=20000 completedTickFullWriterPairs=128 completedTrajectoryPairs=128 commands=16 genericOriginalOrderedPairs=256 deadStockHostFullWriterPairs=4 literalCloakFullWriterPairs=4 aircraftCalls=[1-9][0-9]* nativeNoHostAdmissions=[0-9]+ subcellCalls=[1-9][0-9]* fusedCalls=[1-9][0-9]* cloakCalls=[1-9][0-9]* tickOccupancyCalls=[1-9][0-9]* cargoInfRangeSlotsOmitted=[1-9][0-9]* H0/H10000/native48/native256/map240/map256/mixed12/originalGenericDefault/growth/commands/paths/fullWriter=exact passed=yes\r?$' -or
                $taskLog -notmatch '(?m)^Runtime simulation stage admission check passed\r?$')) {
                $taskPassed = $false; $taskResult.reason = 'Simulation stage admission requires original integration and frozen legacy data, callback order, four real stock callsites, separate noHost, full completed writers/trajectories and unchanged generic reserve/order'
            }
            if ($taskPassed -and $taskName -eq 'playability_psychic_no_sensor' -and (
                $taskLog -notmatch '(?m)^Psychic no-sensor primitive: cases=24 routeCalls=72 snapshotComparisons=48 callbackComparisons=48 legacyMutableCallbackRoutes=2 legacyComparisons=1 noSensorOmissions=6 positiveCases=6 firstID/deadDuplicate/negativeID/targetDead/targetMissing/alliance/eligibility/completeSnapshotFields/order/callbackOrder=exact passed=yes\r?$' -or
                $taskLog -notmatch '(?m)^Psychic no-sensor actual: nativeCases=3 nativeActors=144 historyObjects=11000 refreshCalls=9 snapshotComparisons=6 completedFullWriterComparisons=9 actualBuildingReadbacks=27 actualWorldSourceEntries=11171 originalIndexConstructions=6 originalIndexScanEntries=22342 defaultNoSensorOmissions=3 defaultIndexConstructions=0 defaultIndexScanEntries=0 defaultIndexInsertionAttempts=0 defaultUniqueIndexEntries=0 stockNAPSISRadius=[1-9][0-9]* Native48/H0/H1000/H10000/stockZeroSensorWorld/completeSpecialSnapshotFields/order/fullWriter=exact passed=yes\r?$')) {
                $taskPassed = $false; $taskResult.reason = 'Psychic no-sensor requires original first-ID/full snapshot/callback sequence, actual stock zero-sensor H10000 world, zero ID index construction/scan and complete serialized authority proof'
            }
            if ($taskPassed -and $taskName -eq 'playability_mixed_local_component') {
                $localMarker = [regex]::Match($taskLog, '(?m)^Mixed local component certificate: probeSchedules=12 boundaries=31 nativeCases=5 originalWholeMaps=15 nativeCommands=45 firstAdmittedCommands=10 actualOutsideFallbacks=4 actualNearestFallbacks=2 completedTickFullSaveTriplets=160 nativeActors=240 nativeTypeReadbacks=60 certifiedPoints=[1-9][0-9]* connectedGoalHits=[1-9][0-9]* walkSteps=[1-9][0-9]* orderedLateMaps=(?<late>[1-9][0-9]*) lateChronoWitness=(?<chrono>[1-9][0-9]*) lateAirWitness=(?<air>[1-9][0-9]*) native240/native256/H0/H10000/stock12types/originalLabels/orderedWholeFallback/Best/Nearest/commands/paths/fullWriter=exact passed=yes\r?$')
                if (!$localMarker.Success -or
                    [decimal]$localMarker.Groups['late'].Value -lt 6 -or
                    [decimal]$localMarker.Groups['chrono'].Value -lt 4 -or
                    [decimal]$localMarker.Groups['air'].Value -lt 4) {
                    $taskPassed = $false; $taskResult.reason = 'Mixed SCC actual stock, complete original ordered late maps after Chrono/Air, Nearest, single-use and completed full-writer proof missing'
                }
            }
            if ($taskPassed -and $taskName -eq 'playability_mixed_local_component') {
                $warmedMarker = [regex]::Match($taskLog, '(?m)^Mixed local warmed performance coverage: nativeCases=6 nativeActors=704 nativeTypeReadbacks=72 warmupTicks=360 originalCommands=18 nativeCommands=54 firstAdmittedCommands=12 forcedLateMaps=6 preCommandFullSaveTriplets=6 completedFullSaveTriplets=384 completedTrajectoryTriplets=3336 idleReadbacks=[1-9][0-9]* firstIdleAdmissions=[1-9][0-9]* recoveryQualifiedCommands=[1-9][0-9]* firstSeedVetoes=[1-9][0-9]* firstDetourExpansions=[1-9][0-9]* orderedLateMaps=(?<late>[1-9][0-9]*) laterAdmitted=[0-9]+ laterGuardFallbacks=[0-9]+ laterWalkFallbacks=[0-9]+ goals=far/near/far ticks=61/301/541 native48/native256/H0/H10000/Idle/ChronoRecovery/originalSCC/orderedWholeFallback/commands/paths/fullWriter=exact passed=yes\r?$')
                if (!$warmedMarker.Success -or [decimal]$warmedMarker.Groups['late'].Value -lt 6) {
                    $taskPassed = $false; $taskResult.reason = 'Mixed SCC true60warmup, far-first actual48/256, narrow recovery, detour connected-only publication and completed trajectory/full-writer proof missing'
                }
            }
            if ($taskPassed -and $taskName -eq 'playability_mixed_local_component') {
                $leaderMarker = [regex]::Match($taskLog, '(?m)^Mixed local stock leader coverage: nativeCases=4 nativeActors=192 nativeTypeReadbacks=48 leaderReadbacks=12 warmupTicks=240 nativeCommands=12 referenceCommands=4 admittedCommands=8 forcedLateMaps=4 preCommandFullSaveTriplets=4 completedFullSaveTriplets=16 completedTrajectoryTriplets=256 originalSccWitnesses=4 originalOrderedMaps=8 certifiedPoints=[1-9][0-9]* idleReadbacks=[1-9][0-9]* leaders=SREF/TTNK/FV/HTNK originalLabels/orderedLateWhole/acceptance/destinations/paths/fullWriter=exact passed=yes\r?$')
                if (!$leaderMarker.Success) {
                    $taskPassed = $false; $taskResult.reason = 'Mixed SCC normally hydrated SREF/TTNK/FV/HTNK warm native48, original component labels/ordered late maps and completed full writer proof missing'
                }
            }
            if ($taskPassed -and $taskName -eq 'playability_mixed_local_component') {
                $unmappedMarker = [regex]::Match($taskLog, '(?m)^Mixed local known-unmapped certificate: boundaries=14 originalWholeMaps=15 seedOracles=12 incomingOracles=96 memoWitnesses=1 crushVetoes=2 filterChecks=6 nativeCases=2 nativeCommands=6 admittedCommands=2 warmupTicks=120 preCommandFullSaveTriplets=2 completedFullSaveTriplets=8 completedTrajectoryTriplets=128 ghostPredicateReadbacks=2 publishedKeysChecked=[1-9][0-9]* actualUnmappedProofs=(?<proofs>[1-9][0-9]*) actualIncomingQueries=[1-9][0-9]* actualPublishedKeysChecked=(?<checked>[1-9][0-9]*) seedFalse/eightIncomingFalse/independentDirectedCache/BestComponentMask/negativeComponentBypass/native240/native256/originalMaps/commands/paths/fullWriter=exact passed=yes\r?$')
                if (!$unmappedMarker.Success -or
                    [decimal]$unmappedMarker.Groups['proofs'].Value -ne [decimal]$unmappedMarker.Groups['checked'].Value) {
                    $taskPassed = $false; $taskResult.reason = 'Known-unmapped original complete-map proof for every published key, directed crush veto, Best mask/bypass and completed native full writer missing'
                }
            }
            if ($taskPassed -and $taskName -eq 'playability_mixed_local_component') {
                $defaultKnownWarm = [regex]::Match($taskLog, '(?m)^Known-unmapped default warmed native: nativeCases=6 nativeActors=704 nativeTypeReadbacks=72 warmupTicks=360 nativeCommands=54 defaultCommands=36 referenceCommands=18 flagChecks=54 firstAdmittedCommands=12 forcedLateMaps=6 preCommandFullSaveTriplets=6 completedFullSaveTriplets=384 completedTrajectoryTriplets=3336 directPublishedKeysChecked=[0-9]+ actualUnmappedProofs=(?<proofs>[0-9]+) actualPublishedKeysChecked=(?<checked>[0-9]+) orderedLateMaps=(?:[6-9]|[1-9][0-9]+) featureReference=false goals=far/near/far ticks=61/301/541 native48/native256/H0/H10000/originalAbsentKeys/Best/Nearest/commands/paths/fullWriter=exact passed=yes\r?$')
                $defaultKnownLeaders = [regex]::Match($taskLog, '(?m)^Known-unmapped default stock leaders: nativeCases=4 nativeActors=192 nativeTypeReadbacks=48 warmupTicks=240 nativeCommands=12 defaultCommands=8 referenceCommands=4 flagChecks=12 admittedCommands=8 forcedLateMaps=4 preCommandFullSaveTriplets=4 completedFullSaveTriplets=16 completedTrajectoryTriplets=256 actualUnmappedProofs=(?<proofs>[0-9]+) actualPublishedKeysChecked=(?<checked>[0-9]+) featureReference=false SREF/TTNK/FV/HTNK/originalAbsentKeys/orderedWholeFallback/commands/paths/fullWriter=exact passed=yes\r?$')
                if (!$defaultKnownWarm.Success -or !$defaultKnownLeaders.Success -or
                    [decimal]$defaultKnownWarm.Groups['proofs'].Value -ne [decimal]$defaultKnownWarm.Groups['checked'].Value -or
                    [decimal]$defaultKnownLeaders.Groups['proofs'].Value -ne [decimal]$defaultKnownLeaders.Groups['checked'].Value) {
                    $taskPassed = $false; $taskResult.reason = 'Known-unmapped DEFAULT feature branch requires all six actual warmed 48/256/H0/H10000 worlds through three commands and completed tick556, all four stock leaders, and original-map proof for every published absence'
                }
            }
            if ($taskPassed -and $taskName -eq 'playability_mixed_local_component') {
                $frontierClosureMarker = [regex]::Match($taskLog, '(?m)^Mixed local Best frontier closure: nativeCases=3 unlistedCentres=3 referenceVetoes=6 membershipAdmissions=3 memoWitnesses=3 incompleteFrontierVetoes=3 canonicalVetoes=3 zeroBudgetVetoes=3 frozenProofChecks=3 actualCommandAdmissions=3 actualClosureAdmissions=(?<admissions>[1-9][0-9]*) actualClosurePoints=(?<points>[1-9][0-9]*) baselineActualClosureAdmissions=(?<baseline>[1-9][0-9]*) historyActualClosureAdmissions=(?<history>[1-9][0-9]*) predicateQueriesAdded=0 wholeMaps=0 originalMasks/completeFrontiers/frozenMembership/memo/reference/budget/native240/native256/H10000/commands/paths/fullWriter=exact passed=yes\r?$')
                $frontierHistoryMarker = [regex]::Match($taskLog, '(?m)^Mixed local Best frontier closure H10000 coverage: nativeCases=1 nativeWorldObjects=10057 nativeAliveObjects=57 finalArmy=48 initialPopulationReadbacks=3 finalPopulationReadbacks=3 stockTypeReadbacks=36 nativeCommands=3 admittedCommands=1 warmupTicks=60 preCommandFullSaveTriplets=1 completedFullSaveTriplets=4 completedTrajectoryTriplets=64 directOriginalWholeMaps=1 publishedKeysChecked=[1-9][0-9]* actualUnmappedProofs=(?<proofs>[1-9][0-9]*) actualIncomingQueries=[1-9][0-9]* actualPublishedKeysChecked=(?<checked>[1-9][0-9]*) actualClosureAdmissions=(?<admissions>[1-9][0-9]*) actualClosurePoints=(?<points>[1-9][0-9]*) original/reference/default/native240/H10000/stock12types/absence/wholeMaps0/commands/paths/fullWriter=exact passed=yes\r?$')
                $frontierDefaultMarker = [regex]::Match($taskLog, '(?m)^Mixed local Best frontier closure default corpus: warmedNativeCases=6 leaderCases=4 nativeCommands=66 completedFullSaveTriplets=400 completedTrajectoryTriplets=3592 featureReference=false originalDefaultAssertions=retained passed=yes\r?$')
                if (!$frontierClosureMarker.Success -or !$frontierHistoryMarker.Success -or !$frontierDefaultMarker.Success -or
                    [decimal]$frontierClosureMarker.Groups['admissions'].Value -lt 2 -or
                    [decimal]$frontierClosureMarker.Groups['points'].Value -ne 252 * [decimal]$frontierClosureMarker.Groups['admissions'].Value -or
                    [decimal]$frontierClosureMarker.Groups['admissions'].Value -ne
                        ([decimal]$frontierClosureMarker.Groups['baseline'].Value + [decimal]$frontierClosureMarker.Groups['history'].Value) -or
                    [decimal]$frontierClosureMarker.Groups['history'].Value -ne [decimal]$frontierHistoryMarker.Groups['admissions'].Value -or
                    [decimal]$frontierHistoryMarker.Groups['proofs'].Value -ne [decimal]$frontierHistoryMarker.Groups['checked'].Value -or
                    [decimal]$frontierHistoryMarker.Groups['points'].Value -ne 252 * [decimal]$frontierHistoryMarker.Groups['admissions'].Value) {
                    $taskPassed = $false; $taskResult.reason = 'Best frontier closure requires at least two actual command admissions, genuine H10000 population/writer/trajectory proof, original two-case controls, strict wholeMaps=0 and the complete default warmed/leader corpus'
                }
            }
            if ($taskPassed -and $taskName -eq 'playability_strict_slot_vehicle' -and
                $taskLog -notmatch '(?m)^Strict-slot vehicle certificate: boundaries=27 originalWholeMaps=4 nativeCommands=24 admittedCommands=8 referenceCommands=12 actualFallbackCommands=4 completedTickFullSavePairs=96 nativeActors=192 actualMTNK=96 actualHTNK=96 certifiedPoints=[1-9][0-9]* proofVertices=[1-9][0-9]* orientedEdges=[1-9][0-9]* native240/native256/H0/H10000/originalLabels/strictWinner/commands/paths/fullWriter=exact passed=yes\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Strict-slot real stock vehicle positive/fallback commands, original component labels and completed-tick full-writer proof missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_cell_gate_scratch') {
                $scratchMarker = [regex]::Match($taskLog, '(?m)^Cell gate scratch: boundaries=16 exactScalarPairs=15360 exactOccupancyTailPairs=1280 exactDirectedEdges=98304 completeOrderedSyntheticMaps=48 nativeWholeMaps=16 nativeCommands=24 nativeTickFullSavePairs=128 nativeGroundFastMaps=4 nativeMixedFastCommands=12 oldGateIntegrationRoutes=2 oldDirectedRoutes=2 exactMisses=(?<misses>[1-9][0-9]*) referenceOriginal=[1-9][0-9]* indexedOriginal=[1-9][0-9]* omittedContainerConstructions=(?<containers>[1-9][0-9]*) currentGateKey/soft/crush/allowed/terrainNegative/metadata/orderedBFS/globalTie/native240/native256/H0/H10000/fullWriter=exact passed=yes\r?$')
                if (!$scratchMarker.Success -or
                    [decimal]$scratchMarker.Groups['containers'].Value -ne 2 * [decimal]$scratchMarker.Groups['misses'].Value -or
                    ([regex]::Matches($taskLog,'(?m)^Gate integration smoke passed: footprint, planning/commit, vehicle/infantry/attack, occupancy, cache, save/hash\.\r?$')).Count -ne 2 -or
                    ([regex]::Matches($taskLog,'(?m)^Planner terrain transition: exactDirectedEdges=287496 completeOrderedComponents=6 allAuthoritiesAndFullSave=yes pass\r?$')).Count -ne 2) {
                    $taskPassed = $false; $taskResult.reason = 'Gate scratch exact scalar tail, both old gate/ordered graph routes, native completed commands/full writer or source container witness missing'
                }
            }
            if ($taskPassed -and $taskName -eq 'playability_component_metadata_borrow' -and
                $taskLog -notmatch '(?m)^Component metadata borrow: structuralWriters=24 nodeAddresses=[1-9][0-9]* allFieldCopies=[1-9][0-9]* nativeWholeMaps=16 nativeCommands=24 nativeTickFullSavePairs=128 nativeBorrowedOwners=20 originalDirected/orderedBFS/globalTie/fullWriter/native240/native256=exact passed=yes\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Component metadata borrow complete structural, ordered BFS, native command and completed tick full-save contract completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_history_queries' -and
                $taskLog -notmatch '(?m)^Spy idle detection context: fullWorld/save/spy/actions/statistics=27 detectionUpdates=39 oldOwnerContextBuilds=24 optimizedOwnerContextSkips=24 histories=0/1000/10000 passed\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Spy idle detection full-state contract completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_history_queries' -and
                $taskLog -notmatch '(?m)^Spy stock detector oracle: dogPersonalOracleChecks=10 poweredNAPSISPairChecks=10 blackout101/recovery102=yes\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Stock DOG/NAPSIS detector and blackout recovery completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_path_snapshot_metadata_queries' -and
                $taskLog -notmatch '(?m)^Path conditional pure lookups: fullSnapshotCases=184 fullFieldCells=[1-9][0-9]* fields=12 AirUnusedFindsSkipped=[1-9][0-9]* blockedUnusedFindsSkipped=[1-9][0-9]* completeWorld/fullSave/special/repair/bridge/resource/radiation/sourceMutation=native passed=yes\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Conditional path pure lookup full-world/full-save completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_path_snapshot_metadata_queries' -and
                $taskLog -notmatch '(?m)^Path dynamic sparse queries: primitiveExact=524328 fullSnapshotCases=337 fullFieldCells=[1-9][0-9]* fields=12 actualCommands=18 perTickFullSaves=96 enabled=[1-9][0-9]* representativeSkipped=[1-9][0-9]* repairSkipped=[1-9][0-9]* scalarFallbackProofs=[1-9][0-9]* actualEnabled=[1-9][0-9]* actualRepresentativeSkipped=[1-9][0-9]* actualRepairSkipped=[1-9][0-9]* representativeBytesAvoided=[1-9][0-9]* ordered/world/fullSave/repair/gates/subcells/bridge/resource/radiation/aliases/history=native passed=yes\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Sparse dynamic path full-field/actual-command/full-save contract completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_path_snapshot_metadata_queries' -and
                $taskLog -notmatch '(?m)^Path dynamic cell batch: oppositeLoopSnapshots=674 originalSnapshotCases=337 originalCommands=18 originalTickPairs=96 nativeLargeSnapshotCases=6 nativeGroundEnabled=4 nativeAirRejected=2 nativeThreatBuilders=0 nativeRadiationBuilders=0 nativeLargeCommands=12 nativeLargeTickPairs=64 ordered12Fields/fullWriter/paths/acceptance/normalNoStats=exact passed=yes\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Whole-loop/single-cell reference, native large admission and normal actual-command full-save completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_vision_terrain_coordinate' -and
                $taskLog -notmatch '(?m)^Vision terrain-coordinate lease: exact complete grids/stats/revisions/epochs; 64 actual per-tick full-save/hash comparisons, 8 native commands, 16 actual commit/rollback routes, [1-9][0-9]* repeated original terrain hashes reused\.\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Vision terrain-coordinate contract completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_vision_terrain_batch' -and
                $taskLog -notmatch '(?m)^Vision terrain-coordinate batch: 16 primitive borrowed invocations; 64 actual per-tick full-save/hash/grid comparisons, 8 native commands, 4 bounded batches, 4 original hashes, [1-9][0-9]* reuses, 16 actual commit/rollback routes; H0/H10000 exact\.\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Bounded vision batch full-world/grid/commit completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_vision_terrain_coordinate' -and
                $taskLog -notmatch '(?m)^Vision live source update: fullWorld/fullSave/publicGrids/stats/revisions/epochs=24 actualUpdates=[1-9][0-9]* deadFilterRangeEntriesSaved=[1-9][0-9]* allCargoHostVisits=[1-9][0-9]* histories=0/1000/10000 passed\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Vision live source full-state contract completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_voxel_face_preparation' -and
                $taskLog -notmatch '(?m)^Ground shadow eligibility: actual stock full raw32 DIB passes=27 savedHistoryQueries=30000 non-timing stats/resources exact passed\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Ground shadow full-image contract completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_voxel_face_preparation' -and
                $taskLog -notmatch '(?m)^Terrain viewport fill contract passed: primitive=20 fullMapPairs=27 positiveElisions=6 raw32=yes stats353=yes cache=yes brush=yes\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Terrain viewport full-map/raw32/cache/DC contract completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_voxel_loading_prewarm' -and
                $taskLog -notmatch '(?m)^Voxel loading prewarm: actual stock full image/aux/key probes=[1-9][0-9]* animated=[1-9][0-9]* shadow=[1-9][0-9]* warmHits=[1-9][0-9]* coldFallbacks=[0-9]+ routes=2 totalNew=[1-9][0-9]* new=[1-9][0-9]* addedPayload=[1-9][0-9]* budgets/nested/foreign/highHVA/loadingUI passed\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Voxel loading prewarm asset completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_voxel_loading_world_save' -and
                $taskLog -notmatch '(?m)^Voxel loading prewarm world: actualMixed48 fullWorldAndSave=yes newEntries=[1-9][0-9]* payloadBytes=[1-9][0-9]* pass\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Voxel loading prewarm full-world/full-save completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_voxel_loading_world_save' -and
                $taskLog -notmatch '(?m)^\[RenderSmokeFutureVoxelBootstrap\] bodyFacings=32 completeEntries=[1-9][0-9]* actualParts=[0-9]+ partHits=[0-9]+ shadowFacings=[1-9][0-9]* nativeSources=[1-9][0-9]* boundedProgress=1 coldFallback=yes negativeFirstWriter=yes unknownEarlierRefused=yes\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Future SREF full-entry/facing/original-fallback completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_voxel_loading_world_save' -and
                $taskLog -notmatch '(?m)^Future voxel native production: stockSREFAbsent=yes potentialWithoutGATECH=yes normalPrewarmFullSave=yes legalQueue=yes paidOnce=yes originalSimulationSpawn=yes GAWEAPExit=yes fullDIBs=2 renderPairs=2 pairedTicks=[1-9][0-9]* completeWorldSaveVision=[1-9][0-9]* bootstrapNewSources=[1-9][0-9]* bootstrapNewEntries=[1-9][0-9]* completeRenderStats=353 cacheCountersOnlyDiffer=yes passed=yes\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Future SREF legal-production/full-save/full-DIB completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_voxel_face_preparation' -and
                $taskLog -notmatch '(?m)^Repair records memo: actual stock raw32/stats353/cache pairs=12 savedObjectVisits=[1-9][0-9]* positiveOriginalAndObserverFallback=yes pass\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Repair records stock full-image/stats/cache positive-borrow and observer-fallback completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_voxel_loading_world_save' -and
                $taskLog -notmatch '(?m)^Repair records world: actual complete draw raw32/stats353/cache pairs=2 savedObjectVisits=[0-9]+ authorityHash=yes pass\r?$') {
                $taskPassed = $false; $taskResult.reason = 'Repair records actual-world full-image/stats/cache/authority completion record missing'
            }
            if ($taskPassed -and $taskName -eq 'playability_voxel_loading_world_save') {
                # The producer prints the actual completed state tick. Require
                # exactly one canonical nonnegative int and retain its value;
                # zero is a valid completed initial-world boundary.
                $taskTickMatches = [regex]::Matches($taskLog,
                    '(?m)^Repair records world: actualMixed48 fullWorldAndSave=yes completedTick=(?<completedTick>0|[1-9][0-9]*) pass\r?$')
                $taskTickRecords = [regex]::Matches($taskLog, '(?m)^Repair records world: actualMixed48 [^\r\n]*\r?$')
                $taskTickValue = 0
                if ($taskTickRecords.Count -ne 1 -or $taskTickMatches.Count -ne 1 -or
                    ![int]::TryParse($taskTickMatches[0].Groups['completedTick'].Value, [ref]$taskTickValue)) {
                    $taskPassed = $false; $taskResult.reason = 'Repair records complete-world/save completion record missing, ambiguous or outside the actual tick int range'
                } else {
                    $taskResult.repairRecordsCompletedTick = $taskTickValue
                }
            }
            if ($taskPassed) { $taskResult.status = 'Passed' }
            if ($taskTest.properties.ContainsKey('SKIP_RETURN_CODE') -and $taskRun.exitCode -eq $taskTest.properties['SKIP_RETURN_CODE']) {
                $taskResult.status = 'Skipped'; $taskResult.reason = 'CTest skip return code'
            }
        } catch {
            $taskResult.reason = $_.Exception.Message
            [IO.File]::AppendAllText($taskLogPath, "`n[runner error]`n" + $_.Exception.Message + "`n")
        }
        $taskResults.Add($taskResult)
        Write-TaskSummary
        Write-Output "$($taskResult.status): $taskName ($([Math]::Round($taskResult.elapsedSeconds, 3)) seconds)"
    }
    Write-TaskSummary
} catch {
    Write-TaskSummary $_.Exception.Message
    Write-Output "Report: $(Join-Path $taskOutput 'summary.json')"
    throw
}
Write-Output "Report: $(Join-Path $taskOutput 'summary.json')"
$taskFailedResults = @($taskResults | Where-Object { $_.status -ne 'Passed' })
if ($taskFailedResults.Count) { throw "$($taskFailedResults.Count) playability checks did not pass; outputs are preserved." }
$taskFinalConfigHash = if (Test-Path -LiteralPath $taskActualConfig -PathType Leaf) {
    (Get-FileHash -LiteralPath $taskActualConfig -Algorithm SHA256).Hash
} else { $null }
if ($taskConfigHashBefore -ne $taskFinalConfigHash) { throw 'Runtime config changed during the checks; see the recorded hashes. The runner did not edit or restore it.' }
