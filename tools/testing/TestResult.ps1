# Semantic result classification is separate from the process exit code.
function Get-LocalTestOutcome([xml]$JUnit,[string]$Name,[int]$ExitCode,[bool]$Completed,[string]$Output) {
    $result=[ordered]@{status='failed';ctestStatus='unknown';reason='CTest failed or did not produce one matching result.'}
    if($null -eq $JUnit){return $result}
    $cases=@($JUnit.SelectNodes('//testcase'))
    if($cases.Count -ne 1 -or $cases[0].GetAttribute('name') -ne $Name){return $result}
    $case=$cases[0];$result.ctestStatus=$case.GetAttribute('status')
    if($case.SelectSingleNode('skipped')){$result.status='skipped';$result.reason='CTest reported skipped.';return $result}
    if(!$Completed -or $ExitCode -ne 0 -or $case.SelectSingleNode('failure') -or $case.SelectSingleNode('error') -or $result.ctestStatus -ne 'run'){return $result}
    $result.status='passed';$result.reason='Built and executed; CTest assertions passed.'
    # These exact messages come from the audited non-Windows branches. A
    # successful negative assertion mentioning unsupported is not a skip.
    if($Output -match '(?im)^\s*(?:\d+:\s*)?(?:Runtime Internet (?:TLS|service transport|service host) smoke|Full-stack Internet service lobby E2E) skipped: Windows only\s*$'){
        $result.status='unsupported';$result.reason='Program reports Windows-only skip despite exit 0; raw CTest result retained.'
    } elseif($Output -match '(?im)^\s*(?:\d+:\s*)?[^\r\n]+ (?:smoke|test) skipped:\s*[^\r\n]+$'){
        $result.status='skipped';$result.reason='Program reports an explicit test skip despite exit 0; raw CTest result retained.'
    }
    return $result
}
