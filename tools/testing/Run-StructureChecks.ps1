#requires -Version 7.0
# S29 static checks. Select an explicit verified Python 3.10+; no package install.
param(
 [Parameter(Mandatory)][string]$BuildDirectory,
 [ValidateSet('Release')][string]$Configuration='Release',
 [Parameter(Mandatory)][string]$PythonExecutable,
 [string]$OutputDirectory=''
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
function LocalPath([string]$Value,[switch]$Evidence){
 $path=[IO.Path]::GetFullPath($(if([IO.Path]::IsPathRooted($Value)){$Value}else{Join-Path $root $Value}))
 if(-not $path.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Build and output paths must be project-local.'}
 $probe=$path
 while($probe -and $probe.Length -ge $root.Length){
  if((Test-Path -LiteralPath $probe) -and ((Get-Item -LiteralPath $probe -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Linked paths are not allowed.'}
  if($probe -ieq $root){break};$probe=[IO.Path]::GetDirectoryName($probe)
 }
 return $path
}
if(-not [IO.Path]::IsPathRooted($PythonExecutable) -or -not [IO.File]::Exists($PythonExecutable)){throw 'PythonExecutable must be a verified available absolute path.'}
$python=(Resolve-Path -LiteralPath $PythonExecutable).ProviderPath
$build=LocalPath $BuildDirectory
$cache=@{}
foreach($line in [IO.File]::ReadLines((Join-Path $build 'CMakeCache.txt'))){if($line -match '^([^#/:][^:]*):[^=]*=(.*)$'){$cache[$Matches[1]]=$Matches[2]}}
if($cache.CMAKE_BUILD_TYPE -cne 'Release' -or $cache.CMAKE_GENERATOR -cne 'Ninja' -or [IO.Path]::GetFullPath($cache.CMAKE_HOME_DIRECTORY) -ine $root){throw 'Select this project''s verified Ninja Release cache.'}
if(-not $OutputDirectory){$OutputDirectory='build/local-tests/structure-'+[DateTimeOffset]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)}
$output=LocalPath $OutputDirectory -Evidence
if(Test-Path -LiteralPath $output){throw 'OutputDirectory already exists; choose a new directory.'}
[IO.Directory]::CreateDirectory($output)|Out-Null
$temp=Join-Path $output 'temp';[IO.Directory]::CreateDirectory($temp)|Out-Null
$records=[Collections.Generic.List[object]]::new()
function Proof([string]$Path){$f=Get-Item -LiteralPath $Path;[ordered]@{path=$f.FullName;bytes=$f.Length;sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash}}
function Json([string]$Path,$Value){
 $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
 try{$bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 100));$stream.Write($bytes)}finally{$stream.Dispose()}
}
function InvokeStep([string]$Name,[string]$Exe,[string[]]$Arguments,[int]$Timeout=1200000){
 $psi=[Diagnostics.ProcessStartInfo]::new()
 $psi.FileName=$Exe;$psi.WorkingDirectory=$root;$psi.UseShellExecute=$false;$psi.CreateNoWindow=$true
 $psi.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
 $psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
 foreach($arg in $Arguments){$psi.ArgumentList.Add($arg)}
 foreach($key in @($psi.Environment.Keys)){if($key -like 'CONCORD_*' -or $key -in @('CL','_CL_','LINK','_LINK_','INCLUDE','LIB','LIBPATH')){$psi.Environment.Remove($key)|Out-Null}}
 $psi.Environment['TEMP']=$temp;$psi.Environment['TMP']=$temp
 $environment=[ordered]@{};foreach($key in @($psi.Environment.Keys|Sort-Object)){$environment[$key]=$psi.Environment[$key]}
 $started=[DateTimeOffset]::UtcNow;$exitCode=$null;$timedOut=$false;$launchError=$null
 $proc=[Diagnostics.Process]::new();$proc.StartInfo=$psi
 $stdoutPath=Join-Path $output ($Name+'.stdout.log');$stderrPath=Join-Path $output ($Name+'.stderr.log')
 $o=[IO.File]::Open($stdoutPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
 $e=[IO.File]::Open($stderrPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
 try{
  if(-not $proc.Start()){throw 'Child did not start'}
  $a=$proc.StandardOutput.BaseStream.CopyToAsync($o);$b=$proc.StandardError.BaseStream.CopyToAsync($e)
  if(-not $proc.WaitForExit($Timeout)){$timedOut=$true;$proc.Kill($true);$proc.WaitForExit()}
  [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($a,$b));$exitCode=$proc.ExitCode
 }catch{$launchError=$_.Exception.ToString()}finally{$o.Dispose();$e.Dispose();$proc.Dispose()}
 $row=[ordered]@{schema='S29StructureCommand/v1';id=$Name;executable=(Proof $Exe);arguments=$Arguments;cwd=$root;environment=$environment;TEMP=$temp;TMP=$temp;startedUtc=$started.ToString('o');finishedUtc=[DateTimeOffset]::UtcNow.ToString('o');exitCode=$exitCode;timedOut=$timedOut;launchError=$launchError;stdout=(Proof $stdoutPath);stderr=(Proof $stderrPath)}
 Json (Join-Path $output ($Name+'.command.json')) $row
 $records.Add($row)
 if($timedOut -or $launchError -or $null -eq $exitCode){throw ('Command did not complete: '+$Name)}
 return [pscustomobject]$row
}
$failure=$null;$status='failed';$metadataStatus=$null;$buildLock=$null
try{
 $buildLock=[IO.File]::Open((Join-Path $build '.s29-structure.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 $versionCode='import sys,json; print(json.dumps({"executable":sys.executable,"version":sys.version,"prefix":sys.prefix,"base_prefix":sys.base_prefix})); sys.exit(0 if sys.version_info >= (3,10) else 1)'
 $version=InvokeStep 'python-identity' $python @('-I','-B','-c',$versionCode)
 if($version.exitCode -ne 0){throw 'Python 3.10 or newer is required.'}
 $validate=InvokeStep 'validate-cache' (Join-Path $PSHOME 'pwsh.exe') @('-NoProfile','-File',(Join-Path $root 'tools/Build-Game.ps1'),'-Toolchain','clang-cl','-BuildDirectory',$build,'-Configuration',$Configuration,'-ValidateOnly')
 if($validate.exitCode -ne 0){throw 'Build cache validation failed.'}
 $invDir=Join-Path $output 'inventory';[IO.Directory]::CreateDirectory($invDir)|Out-Null
 $inventoryRun=InvokeStep 'current-inventory' (Join-Path $PSHOME 'pwsh.exe') @('-NoProfile','-File',(Join-Path $PSScriptRoot 'Get-TestInventory.ps1'),'-Compiler','Clang','-BuildDirectory',$build,'-Configuration',$Configuration,'-OutputDirectory',$invDir)
 $inventoryPath=Join-Path $invDir 'inventory.json'
 if(-not [IO.File]::Exists($inventoryPath)){throw 'Complete current inventory was not produced.'}
 $inventory=[IO.File]::ReadAllText($inventoryPath)|ConvertFrom-Json -Depth 100
 if($inventory.configure.exitCode -ne 0 -or $inventory.ctest.exitCode -ne 0 -or $inventory.toolchain.compilerIdentity.id -cne 'Clang' -or $inventory.toolchain.compilerIdentity.frontendVariant -cne 'MSVC' -or $inventory.summary.registeredTests -lt 1){throw 'Inventory configure/CTest/identity is incomplete.'}
 # Keep the legacy producer's real nonzero mapping result. Complete metadata
 # still exposes unmapped/command-mismatch facts to the exact reviewed ledger.
 if($inventoryRun.exitCode -ne 0 -and $inventory.summary.unresolvedTests.Count -eq 0 -and $inventory.summary.commandArtifactMismatches.Count -eq 0){throw 'Inventory failed for an unclassified reason.'}
 $metadataStatus=if($inventoryRun.exitCode -eq 0){'producer-passed'}else{'producer-failed-mapping;complete-metadata-read;not-reclassified'}
 $api=Join-Path $output 'file-api';[IO.Directory]::CreateDirectory($api)|Out-Null
 $sourceModel=$inventory.fileApi.codemodelFile
 $model=[IO.File]::ReadAllText($sourceModel)|ConvertFrom-Json -Depth 100
 $reply=[IO.Path]::GetDirectoryName($sourceModel)
 $references=@([IO.Path]::GetFileName($sourceModel))+@($model.configurations|Where-Object name -CEQ $Configuration|ForEach-Object {$_.targets.jsonFile})
 $frozenApi=@()
 foreach($name in @($references|Sort-Object -Unique)){
  if([IO.Path]::GetFileName($name) -cne $name){throw 'File API reference must be a single filename.'}
  $source=Join-Path $reply $name;$destination=Join-Path $api $name
  $bytes=[IO.File]::ReadAllBytes($source)
  $stream=[IO.File]::Open($destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
  try{$stream.Write($bytes)}finally{$stream.Dispose()}
  $frozenApi+=,[ordered]@{source=$source;completeFrozen=(Proof $destination)}
 }
 foreach($test in $inventory.tests){
  if($test.targetMetadata){
   $file=Join-Path $api ([IO.Path]::GetFileName($test.targetMetadata.fileApiFile))
   if((Proof $file).sha256 -cne $test.targetMetadata.fileApiSha256){throw 'File API changed during snapshot.'}
  }
 }
 Json (Join-Path $api 'manifest.json') $frozenApi
 $inventory.fileApi.codemodelFile=Join-Path $api ([IO.Path]::GetFileName($sourceModel))
 $inventoryPath=Join-Path $output 'frozen-inventory.json'
 Json $inventoryPath $inventory
 $fixtures=InvokeStep 'fixtures' $python @('-I','-B',(Join-Path $root 'tools/structure/test_structure_guard.py'),'--output',(Join-Path $output 'fixtures'))
 if($fixtures.exitCode -ne 0){throw 'Structural fixture tests failed.'}
 $scan=InvokeStep 'structure' $python @('-I','-B',(Join-Path $root 'tools/structure/structure_guard.py'),'--root',$root,'--build',$build,'--inventory',$inventoryPath,'--trace',(Join-Path $invDir 'configure-trace.jsonl'),'--baseline',(Join-Path $root 'tools/structure/structure-baseline.json'),'--output',(Join-Path $output 'scan'))
 if($scan.exitCode -ne 0){throw 'Structural regression or exception tightening required; see scan/result.json.'}
 $status='passed'
}catch{$failure=$_.Exception.Message}finally{if($buildLock){$buildLock.Dispose()}}
$inputs=@();foreach($path in @($PSCommandPath,(Join-Path $root 'tools/structure/structure_guard.py'),(Join-Path $root 'tools/structure/test_structure_guard.py'),(Join-Path $root 'tools/structure/structure-baseline.json'))){if([IO.File]::Exists($path)){$inputs+=,(Proof $path)}}
Json (Join-Path $output 'structure-entry-result.json') ([ordered]@{schema='ConcordStructureEntry/v1';status=$status;failure=$failure;inputs=$inputs;buildDirectory=$build;configuration=$Configuration;inventoryProducer=$metadataStatus;commands=@($records.ToArray());productionBehavior='not-run';GUI='not-run';secondSystem='not-run';ownFutureExit=$null})
Write-Host ("Structure checks: "+$status+"; "+$output)
if($failure){Write-Error $failure -ErrorAction Continue}
exit $(if($status -ceq 'passed'){0}else{1})
