#requires -Version 5.1
[CmdletBinding()]
param(
    [switch]$Launch,
    [switch]$DryRun,
    [string]$ProjectRoot = '',
    [string]$OutputRoot = '',
    [string]$RuntimeExe = '',
    [string]$RuntimeConfig = '',
    [string]$OverridesPath = ''
)
$ErrorActionPreference = 'Stop'
function Full-TaskPath([string]$Value, [string]$Base) {
    if ([IO.Path]::IsPathRooted($Value)) { return [IO.Path]::GetFullPath($Value) }
    return [IO.Path]::GetFullPath([IO.Path]::Combine($Base, $Value))
}
function Quote-TaskWindowsArgument([string]$Value) {
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $taskQuoted = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $taskQuoted = [regex]::Replace($taskQuoted, '(\\+)$', '$1$1')
    return '"' + $taskQuoted + '"'
}
if (-not $ProjectRoot) { $ProjectRoot = [IO.Path]::Combine($PSScriptRoot, '../..') }
$taskProject = [IO.Path]::GetFullPath($ProjectRoot)
$taskRules = [IO.Path]::Combine($taskProject, 'local_data', 'rulesmd.ini')
if (-not [IO.File]::Exists($taskRules)) { throw 'Original YR rules are required.' }
if (-not $OverridesPath) { $OverridesPath = [IO.Path]::Combine($PSScriptRoot, 'rules-overrides.ini') }
$taskOverrides = Full-TaskPath $OverridesPath $taskProject
if (-not [IO.File]::Exists($taskOverrides)) { throw 'The explicit sample overrides file is missing.' }
if (-not $OutputRoot) {
    $taskRound = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-manual-' + [Guid]::NewGuid().ToString('N')
    $OutputRoot = [IO.Path]::Combine($taskProject, 'build', 'smoke', 'mod-extensions', $taskRound, 'demo')
}
$taskOutput = Full-TaskPath $OutputRoot $taskProject
$taskDestination = [IO.Path]::Combine($taskOutput, 'rulesmd.ini')
if ([string]::Equals($taskDestination, $taskRules, [StringComparison]::OrdinalIgnoreCase) -or
    [string]::Equals($taskDestination, $taskOverrides, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The generated file must be separate from both source inputs.'
}
if (-not $RuntimeExe) { $RuntimeExe = [IO.Path]::Combine($taskProject, 'bin', 'concord_runtime.exe') }
if (-not $RuntimeConfig) { $RuntimeConfig = [IO.Path]::Combine($taskProject, 'local_data', 'runtime_config.ini') }
$taskGame = Full-TaskPath $RuntimeExe $taskProject
$taskConfig = Full-TaskPath $RuntimeConfig $taskProject
if ($Launch -or $DryRun) {
    if (-not [IO.File]::Exists($taskGame)) { throw 'The explicit runtime executable is missing.' }
    if (-not [IO.File]::Exists($taskConfig)) { throw 'The explicit runtime configuration is missing.' }
}
# Preserve original text decoding, single CRLF separator and UTF-8/no-BOM output.
$taskSeparator = [string][char]13 + [string][char]10
$taskContent = [IO.File]::ReadAllText($taskRules) + $taskSeparator + [IO.File]::ReadAllText($taskOverrides)
$taskUtf8 = [Text.UTF8Encoding]::new($false)
$taskBytes = $taskUtf8.GetBytes($taskContent)
$taskHasher = [Security.Cryptography.SHA256]::Create()
try { $taskHash = [BitConverter]::ToString($taskHasher.ComputeHash($taskBytes)).Replace('-', '') }
finally { $taskHasher.Dispose() }
$taskArguments = @('--config', $taskConfig, '--resource-root', [IO.Path]::Combine($taskProject, 'RedAlert2'), '--local-data-root', [IO.Path]::Combine($taskProject, 'local_data'), '--mod-root', $taskOutput)
$taskPlan = [ordered]@{
    mode = $(if ($DryRun) { 'dry-run' } else { 'prepared' })
    projectRoot = $taskProject
    outputDirectory = $taskOutput
    rulesFile = $taskDestination
    sourceRules = $taskRules
    sourceOverrides = $taskOverrides
    generatedBytes = $taskBytes.Length
    generatedSha256 = $taskHash
    runtimeExecutable = $taskGame
    runtimeConfiguration = $taskConfig
    workingDirectory = $taskProject
    arguments = $taskArguments
    launchRequested = [bool]$Launch
    launchPerformed = $false
    generatedFileCreated = $false
}
if ($DryRun) {
    [pscustomobject]$taskPlan | ConvertTo-Json -Depth 6
    return
}
if ([IO.File]::Exists($taskDestination)) { throw 'Refusing to overwrite an existing generated rules file.' }
[void][IO.Directory]::CreateDirectory($taskOutput)
$taskStream = [IO.File]::Open($taskDestination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
try { $taskStream.Write($taskBytes, 0, $taskBytes.Length) }
finally { $taskStream.Dispose() }
$taskPlan.generatedFileCreated = $true
if ($Launch) {
    # Explicit -Launch requests the visible interactive game window.
    $taskStart = [Diagnostics.ProcessStartInfo]::new()
    $taskStart.FileName = $taskGame
    $taskStart.WorkingDirectory = $taskProject
    $taskStart.UseShellExecute = $false
    $taskStart.CreateNoWindow = $false
    $taskStart.Arguments = (($taskArguments | ForEach-Object { Quote-TaskWindowsArgument $_ }) -join ' ')
    $taskProcess = [Diagnostics.Process]::Start($taskStart)
    $taskPlan.launchPerformed = $true
    $taskPlan['processId'] = $taskProcess.Id
    $taskProcess.Dispose()
}
[pscustomobject]$taskPlan | ConvertTo-Json -Depth 6
