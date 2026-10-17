#requires -Version 7.0
param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskOutput=[IO.Path]::GetFullPath((Join-Path $taskRoot $OutputDirectory))
if(!$taskOutput.StartsWith($taskRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $taskOutput)){throw 'Choose a new project-local output directory.'}
$taskProbe=$taskOutput
while($taskProbe -and $taskProbe.Length -ge $taskRoot.Length){
    if((Test-Path -LiteralPath $taskProbe) -and ((Get-Item -LiteralPath $taskProbe -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Output directory must not use a junction/symlink.'}
    $taskProbe=[IO.Path]::GetDirectoryName($taskProbe)
}
[void][IO.Directory]::CreateDirectory($taskOutput)
. (Join-Path $PSScriptRoot 'TestResult.ps1')
$taskChecks=[Collections.Generic.List[object]]::new()
function Assert-Outcome([string]$Name,[string]$Xml,[int]$ExitCode,[bool]$Complete,[string]$Stdout,[string]$Expected){
    $actual=Get-LocalTestOutcome ([xml]$Xml) 'example' $ExitCode $Complete $Stdout
    $taskChecks.Add(@{case=$Name;expected=$Expected;actual=$actual.status;passed=($actual.status -eq $Expected)})
}
$pass='<testsuite><testcase name="example" status="run"/></testsuite>'
Assert-Outcome 'executed assertion pass' $pass 0 $true 'smoke passed' 'passed'
Assert-Outcome 'process failure' $pass 8 $true '' 'failed'
Assert-Outcome 'timeout' $pass 0 $false '' 'failed'
Assert-Outcome 'JUnit failure' '<testsuite><testcase name="example" status="run"><failure/></testcase></testsuite>' 0 $true '' 'failed'
Assert-Outcome 'JUnit error' '<testsuite><testcase name="example" status="run"><error/></testcase></testsuite>' 0 $true '' 'failed'
Assert-Outcome 'JUnit skip' '<testsuite><testcase name="example" status="notrun"><skipped/></testcase></testsuite>' 0 $true '' 'skipped'
Assert-Outcome 'wrong selected test' '<testsuite><testcase name="other" status="run"/></testsuite>' 0 $true '' 'failed'
Assert-Outcome 'zero cases' '<testsuite/>' 0 $true '' 'failed'
Assert-Outcome 'unexpected fixture test' '<testsuite><testcase name="example" status="run"/><testcase name="extra" status="run"/></testsuite>' 0 $true '' 'failed'
Assert-Outcome 'JUnit nonexecution' '<testsuite><testcase name="example" status="notrun"/></testsuite>' 0 $true '' 'failed'
Assert-Outcome 'unsupported negative assertion' $pass 0 $true '1: malformed unsupported version rejected=yes' 'passed'
Assert-Outcome 'zero skipped counter' $pass 0 $true '1: Skipped: 0' 'passed'
Assert-Outcome 'explicit generic skip' $pass 0 $true '1: Example smoke skipped: missing fixture' 'skipped'
foreach($message in @('Runtime Internet TLS smoke','Runtime Internet service transport smoke','Runtime Internet service host smoke','Full-stack Internet service lobby E2E')){
    Assert-Outcome $message $pass 0 $true "1: $message skipped: Windows only" 'unsupported'
}
function Assert-Rejection([string]$Name,[string[]]$Arguments,[string]$Diagnostic){
    $psi=[Diagnostics.ProcessStartInfo]::new()
    $psi.FileName=Join-Path $PSHOME 'pwsh.exe';$psi.WorkingDirectory=$taskRoot
    $psi.UseShellExecute=$false;$psi.CreateNoWindow=$true;$psi.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    $psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
    foreach($arg in (@('-NoProfile','-File')+$Arguments)){[void]$psi.ArgumentList.Add($arg)}
    $proc=[Diagnostics.Process]::Start($psi)
    try{
        $out=$proc.StandardOutput.ReadToEndAsync();$err=$proc.StandardError.ReadToEndAsync();$proc.WaitForExit()
        $text=$out.GetAwaiter().GetResult()+$err.GetAwaiter().GetResult()
        [IO.File]::WriteAllText((Join-Path $taskOutput "$Name.log"),$text)
        $taskChecks.Add(@{case=$Name;expected='exit != 0 and expected diagnostic';actualExitCode=$proc.ExitCode;arguments=$Arguments;passed=($proc.ExitCode -ne 0 -and $text -match $Diagnostic)})
    }finally{$proc.Dispose()}
}
$runner=Join-Path $PSScriptRoot 'Run-LocalTests.ps1'
$base=@($runner,'-Compiler','MSVC','-BuildDirectory','build/msvc-reference','-Configuration','Release')
Assert-Rejection 'zero-match' ($base+@('-TestPattern','^does_not_exist$','-OutputDirectory',"$OutputDirectory/zero-match")) '0 tests match'
Assert-Rejection 'old-cache' @($runner,'-BuildDirectory','build','-Configuration','Release','-OutputDirectory',"$OutputDirectory/old-cache") 'Build-directory contract validation failed'
Assert-Rejection 'outside-project' @($runner,'-BuildDirectory',([IO.Path]::GetDirectoryName($taskRoot)),'-Configuration','Release','-OutputDirectory',"$OutputDirectory/outside") 'inside this project'
Assert-Rejection 'configuration-required' @($runner,'-BuildDirectory','build/msvc-reference','-Configuration','invalid') 'ValidateSet|validation|验证'
Assert-Rejection 'compiler-required-identity' @($runner,'-Compiler','invalid','-BuildDirectory','build/msvc-reference','-Configuration','Release') 'ValidateSet|validation|验证'
Assert-Rejection 'compiler-cache-mismatch' @($runner,'-Compiler','Clang','-BuildDirectory','build/msvc-reference','-Configuration','Release','-OutputDirectory',"$OutputDirectory/compiler-mismatch") 'Build-directory contract validation failed'
Assert-Rejection 'single-config-mismatch' @($runner,'-Compiler','Clang','-BuildDirectory','build/clang-cl-x64-release','-Configuration','Debug','-OutputDirectory',"$OutputDirectory/single-config-mismatch") 'single-configuration CMAKE_BUILD_TYPE'
Assert-Rejection 'output-overwrite' ($base+@('-OutputDirectory',$OutputDirectory)) 'already exists'
Assert-Rejection 'legacy-release-mixing' @((Join-Path $taskRoot 'tools/playability/Check-Playability.ps1'),'-Executable','build/msvc-reference/Release/concord_runtime.exe','-BuildDirectory','build/msvc-reference') 'Debug-only'
$exit=if(@($taskChecks | Where-Object passed -EQ $false).Count){1}else{0}
@{checks=@($taskChecks.ToArray());exitCode=$exit} | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $taskOutput 'contract-results.json') -Encoding utf8
Write-Host "$($taskChecks.Count) entry/result contract checks; exit=$exit"
exit $exit
