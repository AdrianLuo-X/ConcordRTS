param(
    [Parameter(Mandatory)][string]$Executable,
    [string]$Label = 'candidate-final'
)
$ErrorActionPreference = 'Stop'
if ($Label -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Invalid matrix label.' }
$taskProject = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$taskRows = @(
    @{ suffix='map'; scenario='map'; width=1024; height=640; baseline='baseline-map' },
    @{ suffix='fullscreen'; scenario='map'; width=2256; height=1504; baseline='baseline-fullscreen' },
    @{ suffix='move'; scenario='move'; width=1920; height=1080; baseline='baseline-move' },
    @{ suffix='battle'; scenario='battle'; width=1920; height=1080; baseline='baseline-battle' },
    @{ suffix='fullscreen-move'; scenario='move'; width=2256; height=1504; baseline='baseline-fullscreen-move' },
    @{ suffix='fullscreen-battle'; scenario='battle'; width=2256; height=1504; baseline='baseline-fullscreen-battle' }
)
foreach ($taskRow in $taskRows) {
    $taskRunLabel = "$Label-$($taskRow.suffix)"
    & (Join-Path $PSScriptRoot 'Measure-Playability.ps1') -Executable $Executable -Label $taskRunLabel `
        -Scenario $taskRow.scenario -Width $taskRow.width -Height $taskRow.height -RequireFrameReport
    $taskDirectory = Join-Path $taskProject "build/playability/$taskRunLabel"
    & (Join-Path $PSScriptRoot 'Compare-Playability.ps1') `
        -BaselineDirectory (Join-Path $taskProject "build/playability/$($taskRow.baseline)") `
        -CandidateDirectory $taskDirectory -OutputPath (Join-Path $taskDirectory 'comparison.json')
}
