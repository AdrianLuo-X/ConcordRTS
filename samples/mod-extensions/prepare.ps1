param([switch]$Launch)
$ErrorActionPreference='Stop'
$taskProject=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskOutput=Join-Path $taskProject 'build/smoke/feature-set-11/16-mod-extensions/demo'
$taskRules=Join-Path $taskProject 'local_data/rulesmd.ini'
$taskOverrides=Join-Path $PSScriptRoot 'rules-overrides.ini'
if (-not [IO.File]::Exists($taskRules)) {throw 'Original YR rules are required.'}
[IO.Directory]::CreateDirectory($taskOutput) | Out-Null
$taskContent=[IO.File]::ReadAllText($taskRules)+"`r`n"+[IO.File]::ReadAllText($taskOverrides)
[IO.File]::WriteAllText((Join-Path $taskOutput 'rulesmd.ini'),$taskContent,[Text.UTF8Encoding]::new($false))
Write-Output "Validation mod prepared: $taskOutput"
if ($Launch) {
    $taskGame=Join-Path $taskProject 'bin/concord_runtime.exe'
    if (-not [IO.File]::Exists($taskGame)) {throw 'Build the game first with tools/Build-Game.ps1.'}
    # This is the requested interactive game window, not a background helper.
    Start-Process -FilePath $taskGame -WorkingDirectory $taskProject -ArgumentList @('--mod-root',('"'+$taskOutput+'"'))
}
