param(
    [string]$Executable = '',
    [string]$Config = '',
    [string]$Label = 'movement',
    [ValidateSet(128, 256)][int]$Units = 128,
    [ValidateSet('open', 'retarget', 'choke', 'combat', 'battle')][string]$Scenario = 'open',
    [ValidateSet('Live', 'Simulation')][string]$Mode = 'Live',
    [ValidateRange(60, 3600)][int]$Ticks = 600,
    [switch]$TanksOnly,
    [switch]$Reference,
    [switch]$FrameReference,
    [switch]$Trace,
    [switch]$AggregateTrace,
    [switch]$RenderDetail,
    [ValidateRange(0, 1000)][double]$TraceMinMs = 5,
    [string]$GoldenIn = '',
    [string]$GoldenOut = ''
)
$ErrorActionPreference = 'Stop'
# Mixed formations need time to approach and sustain fire. Explicit shorter
# samples remain available for diagnosis and must still pass workload checks.
if (!$PSBoundParameters.ContainsKey('Ticks') -and $Scenario -eq 'combat') { $Ticks = 1800 }
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$Config) { $Config = Join-Path $projectRoot 'tests/fixtures/runtime_live_performance.ini' }
$Config = (Resolve-Path -LiteralPath $Config).Path
if (!$Executable) { $Executable = Join-Path $projectRoot 'build/Debug/concord_runtime.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if ($Label -notmatch '^[A-Za-z0-9_-]+$') { throw 'Label must contain only letters, digits, underscores and hyphens.' }
if ($Mode -eq 'Live' -and $Scenario -eq 'retarget') { throw 'Live open/choke runs already change orders at ticks 301 and 541.' }
if ($Mode -eq 'Live' -and ($GoldenIn -or $GoldenOut)) { throw 'GoldenIn and GoldenOut require Simulation mode.' }
$outputRoot = Join-Path $projectRoot 'build/movement'
[void](New-Item -ItemType Directory -Path $outputRoot -Force)
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $Executable
$start.WorkingDirectory = $projectRoot
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.Environment.Clear()
foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
    $key = $entry.Key.ToUpperInvariant()
    if ($key -notmatch '^CONCORD_(LIVE_BENCHMARK|MASS_MOVEMENT|MOVEMENT_REFERENCE|FRAME_SCALING_REFERENCE|RUNTIME_PERF_TRACE|RUNTIME_PERF_AGGREGATE)') {
        $start.Environment[$key] = $entry.Value
    }
}
$start.Environment['CONCORD_RENDER_DETAIL_STATS'] = if ($RenderDetail) { '1' } else { '0' }
if ($Reference) { $start.Environment['CONCORD_MOVEMENT_REFERENCE'] = '1' }
if ($FrameReference) { $start.Environment['CONCORD_FRAME_SCALING_REFERENCE'] = '1' }
if ($Trace -or $AggregateTrace) {
    $start.Environment['CONCORD_RUNTIME_PERF_TRACE'] = '1'
    $start.Environment['CONCORD_RUNTIME_PERF_TRACE_MIN_MS'] = $TraceMinMs.ToString([Globalization.CultureInfo]::InvariantCulture)
}
if ($AggregateTrace) { $start.Environment['CONCORD_RUNTIME_PERF_AGGREGATE'] = '1' }
if ($Mode -eq 'Live') {
    $start.Environment['CONCORD_LIVE_BENCHMARK'] = '1'
    $start.Environment['CONCORD_LIVE_BENCHMARK_TICKS'] = $Ticks.ToString()
    $start.Environment['CONCORD_LIVE_BENCHMARK_TIMEOUT_SECONDS'] = ([int][Math]::Ceiling($Ticks / 10.0) + 60).ToString()
    $start.Environment['CONCORD_LIVE_BENCHMARK_MOVEMENT'] = $Units.ToString()
    if (!$TanksOnly) { $start.Environment['CONCORD_LIVE_BENCHMARK_MIXED'] = '1' }
    if ($Scenario -eq 'choke') { $start.Environment['CONCORD_LIVE_BENCHMARK_CHOKE'] = '1' }
    if ($Scenario -eq 'combat') { $start.Environment['CONCORD_LIVE_BENCHMARK_COMBAT'] = '1' }
    if ($Scenario -eq 'battle') { $start.Environment['CONCORD_LIVE_BENCHMARK_BATTLE'] = '1' }
} else {
    $start.Environment['CONCORD_MASS_MOVEMENT_BENCHMARK'] = '1'
    $start.Environment['CONCORD_MASS_MOVEMENT_UNITS'] = $Units.ToString()
    $start.Environment['CONCORD_MASS_MOVEMENT_TICKS'] = $Ticks.ToString()
    $start.Environment['CONCORD_MASS_MOVEMENT_SCENARIO'] = $Scenario
    if (!$TanksOnly) { $start.Environment['CONCORD_MASS_MOVEMENT_MIXED'] = '1' }
    if ($GoldenIn) { $start.Environment['CONCORD_MASS_MOVEMENT_GOLDEN_IN'] = [IO.Path]::GetFullPath($GoldenIn) }
    if ($GoldenOut) { $start.Environment['CONCORD_MASS_MOVEMENT_GOLDEN_OUT'] = [IO.Path]::GetFullPath($GoldenOut) }
    $start.ArgumentList.Add('--command-smoke-test')
}
foreach ($argument in @('--config', $Config,
    '--skip-main-menu', '--skip-skirmish-menu', '--resource-root', (Join-Path $projectRoot 'RedAlert2'),
    '--local-data-root', (Join-Path $projectRoot 'local_data'), '--map',
    (Join-Path $projectRoot 'local_data/resources/mix_exports/multimd/2peaks.map'))) {
    $start.ArgumentList.Add($argument)
}
$processTimer = [Diagnostics.Stopwatch]::StartNew()
$process = [Diagnostics.Process]::Start($start)
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
$completed = $process.WaitForExit([Math]::Max(300000, $Ticks * 300))
if (!$completed) { $process.Kill(); $process.WaitForExit() }
$log = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
$stem = "$Label-$Mode-$Scenario-$Units"
$logPath = Join-Path $outputRoot "$stem.log"
[IO.File]::WriteAllText($logPath, $log)
[ordered]@{
    executable = $Executable
    config = $Config
    executableSha256 = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash
    scenario = $Scenario; mode = $Mode; units = $Units; mixed = !$TanksOnly
    reference = $Reference.IsPresent; frameReference = $FrameReference.IsPresent; ticks = $Ticks
    trace = ($Trace.IsPresent -or $AggregateTrace.IsPresent); aggregateTrace = $AggregateTrace.IsPresent
    renderDetail = $RenderDetail.IsPresent
    exitCode = $process.ExitCode; completed = $completed
    processWallSeconds = $processTimer.Elapsed.TotalSeconds
    processCpuSeconds = $process.TotalProcessorTime.TotalSeconds
    displayFpsMeasured = $false
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputRoot "$stem.json")
$log -split '\r?\n' | Where-Object { $_ -match '^(Live (movement|benchmark context|frame|battle)|Mass (movement|battle))' }
if (!$completed -or $process.ExitCode -ne 0) { throw "Movement benchmark failed; see $logPath" }
if ($Mode -eq 'Live' -and ($log -notmatch 'Live movement benchmark: passed=yes' -or
    $log -notmatch 'Live frame benchmark: passed=yes')) { throw "Live movement did not pass; see $logPath" }
if ($Mode -eq 'Live' -and $Scenario -eq 'combat' -and $log -notmatch 'Live movement benchmark:[^\r\n]* combat=yes') {
    throw "Executable did not enable the requested combat workload; see $logPath"
}
if ($Scenario -eq 'combat' -and $log -notmatch 'targetFinalHealth=[1-9][0-9]* targetAlive=yes') {
    throw "Combat benchmark did not prove its target survived; see $logPath"
}
if ($Mode -eq 'Live' -and $Scenario -eq 'battle' -and ($log -notmatch 'Live movement benchmark:[^\r\n]* battle=yes' -or
    $log -notmatch 'Live battle result: passed=yes')) {
    throw "Executable did not pass the requested two-sided battle workload; see $logPath"
}
