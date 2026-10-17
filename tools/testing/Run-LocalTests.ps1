#requires -Version 7.0
<#
.SYNOPSIS
Build and run an exact, source-reviewed Windows test set. Every run is retained.
.EXAMPLE
pwsh -File tools/testing/Run-LocalTests.ps1 -BuildDirectory build/msvc-reference -Configuration Release -Profile windows-minimal
.EXAMPLE
pwsh -File tools/testing/Run-LocalTests.ps1 -Compiler Clang -BuildDirectory build/clang-cl-x64-release -Configuration Release -Profile windows-minimal
#>
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][ValidateSet('Debug','Release','RelWithDebInfo','MinSizeRel')][string]$Configuration,
    [ValidateSet('MSVC','Clang')][string]$Compiler = 'MSVC',
    [ValidateSet('windows-minimal','windows-local-assets')][string]$Profile = 'windows-minimal',
    [string]$TestPattern = '.*',
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Compiler=if($Compiler -ieq 'Clang'){'Clang'}else{'MSVC'}
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $PSScriptRoot 'TestResult.ps1')
function Get-LocalPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($(if([IO.Path]::IsPathRooted($Path)){$Path}else{Join-Path $taskRoot $Path}))
    if (!$full.StartsWith($taskRoot + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw "Path must be inside this project: $Path" }
    $probe=$full
    while($probe -and $probe.Length -ge $taskRoot.Length){
        if(Test-Path -LiteralPath $probe){if((Get-Item -LiteralPath $probe -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Linked/reparse path is not allowed: $probe"}}
        if($probe -ieq $taskRoot){break};$probe=[IO.Path]::GetDirectoryName($probe)
    }
    return $full
}
$taskBuild = Get-LocalPath $BuildDirectory
if (!$OutputDirectory) { $OutputDirectory = 'build/local-tests/' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8) }
$taskOutput = Get-LocalPath $OutputDirectory
if (Test-Path -LiteralPath $taskOutput) { throw "OutputDirectory already exists; choose a new directory: $taskOutput" }
[void][IO.Directory]::CreateDirectory($taskOutput)
$taskTemp = Join-Path $taskOutput 'temp'
[void][IO.Directory]::CreateDirectory($taskTemp)
$taskResults = [Collections.Generic.List[object]]::new()
$taskStages = [Collections.Generic.List[object]]::new()
$taskInventory = $null
$taskCompilerIdentity = $null
$taskError = ''
$taskStarted = [DateTime]::UtcNow.ToString('o')
$taskConfigPath = Join-Path $taskRoot 'local_data/runtime_config.ini'
$taskConfigBefore = if(Test-Path -LiteralPath $taskConfigPath){(Get-FileHash -LiteralPath $taskConfigPath).Hash}else{$null}

function Invoke-Recorded([string]$Name,[string]$Program,[string[]]$Arguments,[int]$Timeout=3600) {
    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName=$Program; $psi.WorkingDirectory=$taskBuild
    $psi.UseShellExecute=$false; $psi.CreateNoWindow=$true
    $psi.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    $psi.RedirectStandardOutput=$true; $psi.RedirectStandardError=$true
    foreach($arg in $Arguments){[void]$psi.ArgumentList.Add($arg)}
    foreach($key in @($psi.Environment.Keys)){if($key -match '^CONCORD_'){[void]$psi.Environment.Remove($key)}}
    $psi.Environment['TEMP']=$taskTemp; $psi.Environment['TMP']=$taskTemp
    $psi.Environment['UserRootDir']=(Join-Path $taskBuild 'portable_msbuild_user') + '\'
    $timer=[Diagnostics.Stopwatch]::StartNew()
    $proc=[Diagnostics.Process]::Start($psi)
    try {
        $out=$proc.StandardOutput.ReadToEndAsync(); $err=$proc.StandardError.ReadToEndAsync()
        $completed=$proc.WaitForExit($Timeout*1000)
        if(!$completed){$proc.Kill($true);$proc.WaitForExit()}
        $stdout=$out.GetAwaiter().GetResult();$stderr=$err.GetAwaiter().GetResult()
        [IO.File]::WriteAllText((Join-Path $taskOutput "$Name.stdout.log"),$stdout)
        [IO.File]::WriteAllText((Join-Path $taskOutput "$Name.stderr.log"),$stderr)
        $result=[ordered]@{name=$Name;executable=$Program;executableSha256=(Get-FileHash -LiteralPath $Program).Hash;arguments=$Arguments;workingDirectory=$taskBuild;environment=@{TEMP=$taskTemp;TMP=$taskTemp;UserRootDir=$psi.Environment['UserRootDir'];clearedInherited='CONCORD_*'};status=$(if($completed -and !$proc.ExitCode){'passed'}else{'failed'});exitCode=$proc.ExitCode;completed=$completed;elapsedSeconds=$timer.Elapsed.TotalSeconds;stdoutFile="$Name.stdout.log";stdoutSha256=(Get-FileHash -LiteralPath (Join-Path $taskOutput "$Name.stdout.log")).Hash;stderrFile="$Name.stderr.log";stderrSha256=(Get-FileHash -LiteralPath (Join-Path $taskOutput "$Name.stderr.log")).Hash}
        $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskOutput "$Name.command.json") -Encoding utf8
        return [pscustomobject]@{record=$result;stdout=$stdout;stderr=$stderr}
    } finally {$proc.Dispose()}
}
function Expand-ReviewPath([string]$Path) { $Path.Replace('{source}',$taskRoot).Replace('{build}',$taskBuild).Replace('{temp}',$taskTemp) }
function Write-Report {
    $gitCommand=Get-Command git.exe -ErrorAction SilentlyContinue
    $reportHead=$null
    if($gitCommand){$reportHead=& $gitCommand.Source -C $taskRoot rev-parse HEAD}
    $counts=[ordered]@{}
    foreach($status in @('passed','failed','skipped','unsupported','not-built','not-run')){$counts[$status]=@($taskResults | Where-Object status -EQ $status).Count}
    $configAfter=if(Test-Path -LiteralPath $taskConfigPath){(Get-FileHash -LiteralPath $taskConfigPath).Hash}else{$null}
    $exit=if($taskError -or $counts.passed -ne $taskResults.Count -or !$taskResults.Count -or $configAfter -ne $taskConfigBefore){1}else{0}
    [ordered]@{schemaVersion=2;startedUtc=$taskStarted;finishedUtc=[DateTime]::UtcNow.ToString('o');projectRoot=$taskRoot;compiler=$Compiler;compilerIdentity=$taskCompilerIdentity;buildDirectory=$taskBuild;configuration=$Configuration;profile=$Profile;testPattern=$TestPattern;operatingSystem=[Environment]::OSVersion.VersionString;identity=$(if($IsWindows){[Security.Principal.WindowsIdentity]::GetCurrent().Name}else{[Environment]::UserName});head=$reportHead;gitAvailable=[bool]$gitCommand;sourceState='Uncommitted working tree; source identity in inventory targetSources.sha256.csv';outputDirectory=$taskOutput;setupError=$taskError;exitCode=$exit;counts=$counts;stages=@($taskStages.ToArray());playerConfig=@{path=$taskConfigPath;before=$taskConfigBefore;after=$configAfter;unchanged=($taskConfigBefore -eq $configAfter)};tests=@($taskResults.ToArray())} | ConvertTo-Json -Depth 25 | Set-Content -LiteralPath (Join-Path $taskOutput 'results.json') -Encoding utf8
    Write-Host "Results: $taskOutput/results.json; passed=$($counts.passed), failed=$($counts.failed), skipped=$($counts.skipped), unsupported=$($counts.unsupported), not-built=$($counts['not-built']), not-run=$($counts['not-run']); exit=$exit"
    return $exit
}

try {
    $reviews=(Get-Content -LiteralPath (Join-Path $PSScriptRoot 'test-profiles.json') -Raw | ConvertFrom-Json).tests
    $selected=@($reviews | Where-Object { $Profile -in $_.profiles -and $_.name -match $TestPattern })
    if(!$selected.Count){throw "0 tests match profile '$Profile' and pattern '$TestPattern'."}
    foreach($review in $selected){
        $inputs=@($review.inputs | ForEach-Object {Expand-ReviewPath $_})
        $taskResults.Add([pscustomobject][ordered]@{name=$review.name;target=$review.target;status='not-run';reason='not executed';inputs=$inputs;inputHashes=@($inputs | Where-Object {Test-Path -LiteralPath $_ -PathType Leaf} | ForEach-Object {@{path=$_;sha256=(Get-FileHash -LiteralPath $_).Hash}});outputs=@($review.outputs | ForEach-Object {Expand-ReviewPath $_});conditions=$review.conditions;artifact=$null;artifactSha256=$null;ctestExitCode=$null;ctestStatus=$null;command=$null;properties=$null})
    }
    if(!$IsWindows){foreach($row in $taskResults){$row.status='unsupported';$row.reason='S03 profiles require Windows; P10 owns platform-core profiles.'};throw 'Windows profile unsupported on this platform.'}
    foreach($variable in @('CL','_CL_','LINK','_LINK_')) {
        if(![string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($variable,'Process'))){throw "Environment variable $variable injects unrecorded compiler/linker options. Use a clean command process."}
    }
    $taskCache=@{}
    foreach($line in Get-Content -LiteralPath (Join-Path $taskBuild 'CMakeCache.txt')) {if($line -match '^([^#/:][^:]*):[^=]+=(.*)$'){$taskCache[$Matches[1]]=$Matches[2]}}
    if([IO.Path]::GetFullPath($taskCache.CMAKE_HOME_DIRECTORY) -ine $taskRoot){throw 'Build cache belongs to another project.'}
    if($taskCache.ContainsKey('CMAKE_CONFIGURATION_TYPES') -and ![string]::IsNullOrWhiteSpace($taskCache.CMAKE_CONFIGURATION_TYPES)) {
        if($Configuration -cnotin ($taskCache.CMAKE_CONFIGURATION_TYPES -split ';')){throw 'Configuration is not defined by this build cache.'}
    } elseif(!$taskCache.ContainsKey('CMAKE_BUILD_TYPE') -or $taskCache.CMAKE_BUILD_TYPE -cne $Configuration) {
        throw 'Configuration differs from the single-configuration CMAKE_BUILD_TYPE; select the matching build directory.'
    }
    # Toolchain-specific read-only validation rejects incompatible caches before configure.
    $toolchain=if($Compiler -eq 'Clang'){'clang-cl'}else{'msvc'}
    $validation=Invoke-Recorded 'validate-build' (Join-Path $PSHOME 'pwsh.exe') @('-NoProfile','-File',(Join-Path $taskRoot 'tools/Build-Game.ps1'),'-Toolchain',$toolchain,'-BuildDirectory',$taskBuild,'-Configuration',$Configuration,'-ValidateOnly') 60
    $taskStages.Add($validation.record)
    if($validation.record.exitCode){throw 'Build-directory contract validation failed; see validate-build logs.'}
    $inventoryDir=Join-Path $taskOutput 'inventory'
    [void][IO.Directory]::CreateDirectory($inventoryDir)
    & (Join-Path $PSScriptRoot 'Get-TestInventory.ps1') -Compiler $Compiler -BuildDirectory $taskBuild -Configuration $Configuration -OutputDirectory $inventoryDir
    $taskInventory=Get-Content -LiteralPath (Join-Path $inventoryDir 'inventory.json') -Raw | ConvertFrom-Json
    $taskCompilerIdentity=$taskInventory.toolchain.compilerIdentity
    if($taskCompilerIdentity.id -cne $Compiler){throw 'Inventory compiler identity differs from the selected compiler.'}
    $byName=@{};foreach($test in $taskInventory.tests){$byName[$test.name]=$test}
    @($taskInventory.tests | Where-Object { $_.name -in $selected.name } | ForEach-Object { $_.targetMetadata.sources } | Sort-Object -Unique | Where-Object {Test-Path -LiteralPath $_ -PathType Leaf} | ForEach-Object { [pscustomobject]@{path=$_;sha256=(Get-FileHash -LiteralPath $_).Hash} }) | Export-Csv -LiteralPath (Join-Path $inventoryDir 'targetSources.sha256.csv') -NoTypeInformation
    $targets=[Collections.Generic.List[string]]::new()
    foreach($row in $taskResults){
        $missing=@($row.inputs | Where-Object {!(Test-Path -LiteralPath $_ -PathType Leaf)})
        if($missing.Count){$row.status='skipped';$row.reason='Missing input: ' + ($missing -join ', ');continue}
        if(!$byName.ContainsKey($row.name)){throw "Reviewed test not registered with available inputs: $($row.name)"}
        $test=$byName[$row.name]
        if($test.target -ne $row.target){throw "Registration/target mismatch: $($row.name)"}
        if($Profile -notin $test.labels){throw "Missing verified profile label: $($row.name)"}
        if(!@($test.targetMetadata.artifacts).Count){throw "Missing File API artifact: $($row.name)"}
        $row.artifact=$test.targetMetadata.executableArtifact
        if(!$row.artifact){throw "Missing exact File API executable artifact: $($row.name)"}
        $row.properties=$test.properties
        if(!$targets.Contains($row.target)){$targets.Add($row.target)}
    }
    if($targets.Count){
        Write-Host "Building $($targets.Count) targets serially ($Compiler $Configuration)."
        $build=Invoke-Recorded 'build-selected' $taskCache.CMAKE_COMMAND (@('--build',$taskBuild,'--config',$Configuration,'--target') + @($targets.ToArray()) + @('--parallel','1','--verbose'))
        $taskStages.Add($build.record)
        if($build.record.exitCode){
            foreach($row in $taskResults | Where-Object status -EQ 'not-run'){$row.status=if(Test-Path -LiteralPath $row.artifact){'not-run'}else{'not-built'};$row.reason='Selected target build failed; stale artifacts were not executed.'}
            throw 'Selected target build failed; see build-selected logs.'
        }
    }
    $meta=Invoke-Recorded 'ctest-after-build' $taskCache.CMAKE_CTEST_COMMAND @('--test-dir',$taskBuild,'-C',$Configuration,'--show-only=json-v1') 60
    $taskStages.Add($meta.record)
    if($meta.record.exitCode){throw 'Post-build CTest query failed.'}
    $ctestTests=($meta.stdout | ConvertFrom-Json).tests
    $ordinal=0
    foreach($row in $taskResults){
        if($row.status -ne 'not-run'){continue}
        $ordinal++
        $test=@($ctestTests | Where-Object name -EQ $row.name)
        if($test.Count -ne 1 -or !$test[0].PSObject.Properties['command'] -or !(Test-Path -LiteralPath $row.artifact -PathType Leaf)){$row.status='not-built';$row.reason='Build returned success but command/artifact is unavailable.';continue}
        $command=@($test[0].command)
        if(!$command.Count -or ![IO.Path]::IsPathRooted($command[0]) -or [IO.Path]::GetFullPath($command[0]) -ine [IO.Path]::GetFullPath($row.artifact)) {
            $row.status='failed';$row.reason='CTest command does not match the exact File API executable artifact for the selected configuration.'
            $row.command=$command
            throw "CTest executable/artifact mismatch: $($row.name). No mismatched executable was run."
        }
        $row.artifactSha256=(Get-FileHash -LiteralPath $row.artifact).Hash
        $row.command=$test[0].command
        $junit=Join-Path $taskOutput "test-$ordinal.junit.xml"
        $pattern='^' + [regex]::Escape($row.name) + '$'
        Write-Host "Running $($row.name)."
        $run=Invoke-Recorded "test-$ordinal" $taskCache.CMAKE_CTEST_COMMAND @('--test-dir',$taskBuild,'-C',$Configuration,'--no-tests=error','--output-on-failure','--verbose','--output-junit',$junit,'-R',$pattern) ([int]$byName[$row.name].timeout + 30)
        $row.ctestExitCode=$run.record.exitCode
        $xml=$null
        if(Test-Path -LiteralPath $junit){
            [xml]$xml=Get-Content -LiteralPath $junit -Raw
        }
        $outcome=Get-LocalTestOutcome $xml $row.name $run.record.exitCode $run.record.completed ($run.stdout + $run.stderr)
        $row.status=$outcome.status;$row.reason=$outcome.reason;$row.ctestStatus=$outcome.ctestStatus
    }
} catch {$taskError=$_.Exception.Message;foreach($row in $taskResults | Where-Object status -EQ 'not-run'){$row.reason='Execution stopped: ' + $taskError}; Write-Warning $taskError}
$taskExit=Write-Report
exit $taskExit
