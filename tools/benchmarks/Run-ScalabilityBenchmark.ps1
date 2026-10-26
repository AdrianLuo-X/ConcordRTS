param(
    [string]$Executable = '',
    [string]$Config = '',
    [string]$Label = 'candidate',
    [ValidateRange(0, 4096)][int]$Units = 1024,
    [ValidateRange(0, 256)][int]$Buildings = 256,
    [ValidateRange(60, 28800)][int]$Ticks = 600,
    [switch]$Trace,
    [switch]$FrameReference,
    [switch]$Hover,
    [string]$OutputDirectory = '',
    [switch]$Visible,
    [switch]$RawSamples,
    [string]$CameraCell = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (!$Executable) { $Executable = Join-Path $projectRoot 'build/Debug/concord_runtime.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (!$Config) { $Config = Join-Path $projectRoot 'tests/fixtures/runtime_live_performance.ini' }
$Config = (Resolve-Path -LiteralPath $Config).Path
if ($Label -notmatch '^[A-Za-z0-9_-]+$') { throw 'Label must contain only letters, numbers, underscores and hyphens.' }
if ($CameraCell -and $CameraCell -notmatch '^\d+,\d+$') { throw 'CameraCell must be x,y with nonnegative integers.' }
$outputRoot = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $projectRoot 'build/scalability' }
if ($OutputDirectory -and (!$outputRoot.StartsWith($projectRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $outputRoot))) {
    throw 'Explicit OutputDirectory must be a new project-local directory.'
}
[void](New-Item -ItemType Directory -Path $outputRoot -Force)
$sourceMap = Join-Path $projectRoot 'local_data/resources/mix_exports/multimd/2peaks.map'
$mapText = [IO.File]::ReadAllText($sourceMap)
# Keep the user's locally installed terrain data out of the source tree. This
# synthetic population exercises steady-state updates and drawing; movement,
# combat and path determinism are covered by the separate command/path tests.
foreach ($section in @('Units', 'Infantry', 'Aircraft', 'Structures', 'Digest')) {
    $mapText = [regex]::Replace($mapText, "(?ms)^\[$section\]\s*\r?\n.*?(?=^\[|\z)", '')
}
$population = [Text.StringBuilder]::new()
[void]$population.AppendLine('[Units]')
for ($index = 0; $index -lt $Units; ++$index) {
    # 1024 unique cells south of the building block; larger optional loads
    # deliberately stack units and are marked by stackedUnits in the report.
    $x = 60 + ($index % 32)
    $y = 110 + ([int][Math]::Floor(($index % 1024) / 32))
    $facing = ($index % 8) * 32
    [void]$population.AppendLine("$index=Americans,MTNK,256,$x,$y,$facing,Guard,None,0,-1,0,-1,1,1")
}
[void]$population.AppendLine('[Structures]')
for ($index = 0; $index -lt $Buildings; ++$index) {
    $x = 60 + ($index % 16) * 3
    $y = 60 + ([int][Math]::Floor($index / 16)) * 3
    [void]$population.AppendLine("$index=Americans,GAPOWR,256,$x,$y,0,None,1,0,1,0,0,None,None,None,1,0")
}
$mapPath = Join-Path $outputRoot "population-$Units-$Buildings.map"
[IO.File]::WriteAllText($mapPath, $mapText + "`r`n" + $population.ToString())
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $Executable
$start.WorkingDirectory = $projectRoot
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.WindowStyle = if ($Visible) { [Diagnostics.ProcessWindowStyle]::Normal } else { [Diagnostics.ProcessWindowStyle]::Hidden }
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.Environment.Clear()
foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
    $key = $entry.Key.ToUpperInvariant()
    if ($key -notmatch '^CONCORD_(LIVE_BENCHMARK|MOVEMENT_REFERENCE|FRAME_SCALING_REFERENCE|RUNTIME_PERF_TRACE|RUNTIME_PERF_AGGREGATE)') {
        $start.Environment[$key] = $entry.Value
    }
}
if ($FrameReference) { $start.Environment['CONCORD_FRAME_SCALING_REFERENCE'] = '1' }
$start.Environment['CONCORD_LIVE_BENCHMARK'] = '1'
$start.Environment['CONCORD_LIVE_BENCHMARK_TICKS'] = $Ticks.ToString()
if ($Visible) { $start.Environment['CONCORD_LIVE_BENCHMARK_VISIBLE'] = '1' }
if ($RawSamples) { $start.Environment['CONCORD_LIVE_BENCHMARK_RAW_SAMPLES'] = '1' }
if ($CameraCell) { $start.Environment['CONCORD_LIVE_BENCHMARK_CELL'] = $CameraCell }
$start.Environment['CONCORD_RENDER_DETAIL_STATS'] = '0'
[void]$start.Environment.Remove('CONCORD_LIVE_BENCHMARK_HOVER')
if ($Hover) { $start.Environment['CONCORD_LIVE_BENCHMARK_HOVER'] = '1' }
[void]$start.Environment.Remove('CONCORD_RUNTIME_PERF_TRACE')
if ($Trace) { $start.Environment['CONCORD_RUNTIME_PERF_TRACE'] = '1' }
foreach ($argument in @('--config', $Config,
    '--skip-main-menu', '--skip-skirmish-menu', '--resource-root', (Join-Path $projectRoot 'RedAlert2'),
    '--local-data-root', (Join-Path $projectRoot 'local_data'), '--map', $mapPath)) {
    $start.ArgumentList.Add($argument)
}
$process = [Diagnostics.Process]::Start($start)
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
$completed = $process.WaitForExit([Math]::Max(300000, ($Ticks * 100 + 60000)))
if (!$completed) {
    $process.Kill()
    $process.WaitForExit()
}
$stdoutText = $stdout.GetAwaiter().GetResult()
$stderrText = $stderr.GetAwaiter().GetResult()
$log = $stdoutText + $stderrText
$logPath = Join-Path $outputRoot "$Label-$Units-$Buildings.log"
[IO.File]::WriteAllText($logPath, $log)
[IO.File]::WriteAllText((Join-Path $outputRoot "$Label-$Units-$Buildings.stdout.log"), $stdoutText)
[IO.File]::WriteAllText((Join-Path $outputRoot "$Label-$Units-$Buildings.stderr.log"), $stderrText)
$metadata = [ordered]@{
    executable = $Executable
    executableSha256 = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash
    config = $Config; frameReference = $FrameReference.IsPresent
    sourceMapSha256 = (Get-FileHash -LiteralPath $sourceMap -Algorithm SHA256).Hash
    mapSha256 = (Get-FileHash -LiteralPath $mapPath -Algorithm SHA256).Hash
    units = $Units; buildings = $Buildings; stackedUnits = [Math]::Max(0, $Units - 1024)
    ticks = $Ticks; exitCode = $process.ExitCode; completed = $completed
    processId = $process.Id
    displayFpsMeasured = $false; scenario = 'stationary population on original terrain'
    hoverRequested = $Hover.IsPresent
    visibleRequested = $Visible.IsPresent
    rawSamples = $RawSamples.IsPresent
    arguments = @($start.ArgumentList)
    environmentOverrides = @($start.Environment.GetEnumerator() | Where-Object Key -Like 'CONCORD_*' | ForEach-Object { @{ name = $_.Key; value = $_.Value } })
}
if ($log -match 'Live benchmark context:[^\r\n]* objects=(\d+)') {
    $metadata.initialObjects = [int]$Matches[1]
    $metadata.startupObjects = $metadata.initialObjects - $Units - $Buildings
}
$metadata | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputRoot "$Label-$Units-$Buildings.json")
$log -split '\r?\n' | Where-Object { $_ -match '^Live (benchmark context|frame benchmark|frame segment):' }
if (!$completed -or $process.ExitCode -ne 0 -or $log -notmatch 'Live frame benchmark: passed=yes') {
    throw "Scalability benchmark failed; see $logPath"
}
if ($Hover -and $log -notmatch 'Live benchmark interaction: hover=yes') {
    throw "This executable did not enable the requested hover workload; see $logPath"
}
$expectedObjects = $Units + $Buildings
# Normal startup may supply its player placeholder. Require the authored map
# counts and every initial object to survive, instead of assuming no startup.
if ($log -notmatch "(?m)^Structures:\s+$Buildings\s*$" -or
    $log -notmatch "(?m)^Units:\s+$Units\s*$" -or
    !$metadata.Contains('initialObjects') -or $metadata.initialObjects -lt $expectedObjects -or
    $log -notmatch "populationSampled=yes objects=$($metadata.initialObjects) alive=$($metadata.initialObjects)(?: |$)") {
    throw "Synthetic population changed unexpectedly; see $logPath"
}
