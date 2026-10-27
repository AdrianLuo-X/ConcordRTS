param([Parameter(Mandatory)][string]$RuntimeExecutable,[Parameter(Mandatory)][string]$OutputRoot)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output=[IO.Path]::GetFullPath($OutputRoot)
if(!$output.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output must be inside this project'}
$run=Join-Path $output ('run-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
[void][IO.Directory]::CreateDirectory($run)
$env:CONCORD_SECOND_WINDOW_CONTRACT_OUTPUT=Join-Path $run 'samples'
& $RuntimeExecutable --config (Join-Path $run 'private.ini') --skirmish-ui-smoke-test (Join-Path $run 'menu.bmp') --resource-root (Join-Path $root 'RedAlert2') --local-data-root (Join-Path $root 'local_data')
exit $LASTEXITCODE
