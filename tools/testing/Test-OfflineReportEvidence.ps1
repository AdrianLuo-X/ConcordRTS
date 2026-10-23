#requires -Version 7.0
[CmdletBinding()]
param(
 [Parameter(Mandatory)][string]$Executable,
 [Parameter(Mandatory)][string]$OutputDirectory,
 [string]$InputDirectory=(Join-Path $PSScriptRoot '../../tests/fixtures/offline_report_evidence'),
 [string]$AssetRoot,
 [string]$BaselineManifest
)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$Executable=[IO.Path]::GetFullPath($Executable);$InputDirectory=[IO.Path]::GetFullPath($InputDirectory)
if(-not $OutputDirectory.StartsWith($project+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'OutputDirectory must stay inside this project'}
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a new output directory; existing evidence is never overwritten'}
[IO.Directory]::CreateDirectory($OutputDirectory)|Out-Null
$checks=0;$cases=[Collections.Generic.List[object]]::new()
function Expect($condition,[string]$why){$script:checks++;if(-not $condition){throw $why}}
function Proof([string]$p){$f=Get-Item -LiteralPath $p;[ordered]@{path=$f.FullName;bytes=$f.Length;sha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash}}
function Json([string]$p){[IO.File]::ReadAllText($p)|ConvertFrom-Json -Depth 100 -DateKind String}
function WriteNew([string]$p,[string]$s){$f=[IO.File]::Open($p,[IO.FileMode]::CreateNew);try{$f.Write([Text.UTF8Encoding]::new($false).GetBytes($s))}finally{$f.Dispose()}}
$exeProof=Proof $Executable
function Native([string]$name,[string[]]$argv,[int]$expected=0,[hashtable]$envs=@{},[string]$discovered=''){
 $cwd=Join-Path $OutputDirectory $name;[IO.Directory]::CreateDirectory($cwd)|Out-Null
 $temp=Join-Path $cwd 'temp';[IO.Directory]::CreateDirectory($temp)|Out-Null
 if($discovered){[IO.File]::Copy($discovered,(Join-Path $cwd 'runtime_replay_events_command.txt'),$false)}
 $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$Executable;$psi.WorkingDirectory=$cwd;$psi.UseShellExecute=$false;$psi.CreateNoWindow=$true;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
 foreach($a in $argv){$psi.ArgumentList.Add($a)}
 foreach($key in @($psi.Environment.Keys)){if($key -like 'CONCORD_*' -or $key -in @('CL','_CL_','LINK','_LINK_','INCLUDE','LIB','LIBPATH')){$null=$psi.Environment.Remove($key)}}
 $psi.Environment['TEMP']=$temp;$psi.Environment['TMP']=$temp;foreach($key in $envs.Keys){$psi.Environment[$key]=$envs[$key]}
 $snapshot=[ordered]@{};foreach($key in @($psi.Environment.Keys|Sort-Object)){$snapshot[$key]=$psi.Environment[$key]}
 $stdout=Join-Path $cwd 'stdout.log';$stderr=Join-Path $cwd 'stderr.log';$out=[IO.File]::Open($stdout,[IO.FileMode]::CreateNew);$err=[IO.File]::Open($stderr,[IO.FileMode]::CreateNew)
 $process=[Diagnostics.Process]::new();$process.StartInfo=$psi;$start=[DateTimeOffset]::UtcNow;$exitCode=$null;$timedOut=$false;$launchError=$null;$nativeId=$null
 try{
  if(-not $process.Start()){throw 'Native process did not start'}
  $nativeId=$process.Id;$ot=$process.StandardOutput.BaseStream.CopyToAsync($out);$et=$process.StandardError.BaseStream.CopyToAsync($err)
  if(-not $process.WaitForExit(120000)){$timedOut=$true;$process.Kill($true);$process.WaitForExit()}
  [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($ot,$et));$exitCode=$process.ExitCode
 }catch{$launchError=$_.Exception.ToString()}finally{$out.Dispose();$err.Dispose();$process.Dispose()}
 $r=[ordered]@{schema='OfflineReportEvidenceTestCommand/v1';name=$name;processId=$nativeId;executable=$exeProof;arguments=$argv;cwd=$cwd;environment=$snapshot;TEMP=$temp;TMP=$temp;startedUtc=$start.ToString('o');finishedUtc=[DateTimeOffset]::UtcNow.ToString('o');exitCode=$exitCode;expectedExit=$expected;timedOut=$timedOut;launchError=$launchError;stdout=(Proof $stdout);stderr=(Proof $stderr)}
 $rp=Join-Path $cwd 'command.json';WriteNew $rp ($r|ConvertTo-Json -Depth 100);$script:cases.Add([ordered]@{name=$name;receipt=(Proof $rp)})
 Expect ($null -ne $exitCode -and $exitCode -eq $expected -and -not $timedOut -and -not $launchError) ("Unexpected native exit: "+$rp)
 return [pscustomobject]$r
}
function CheckSource($p,[string]$kind){
 Expect ($p.kind -ceq $kind) 'Wrong report kind'
 Expect ($p.productionRuntimeValidated -eq $false -and $p.productionRuntimeValidation -ceq 'not-run') 'Offline result promoted to production acceptance'
 Expect ($p.producerVerified -eq $false -and $null -eq $p.producerUtc) 'Input must not attest producer or invent UTC'
 Expect ($p.unknowns.Count -gt 0 -and $p.coverage) 'Coverage or unknowns missing'
}
$rules=Join-Path $InputDirectory 'rules.ini';$presence=Join-Path $OutputDirectory 'presence-assets';[IO.Directory]::CreateDirectory($presence)|Out-Null
foreach($e in @('mix','pal','shp','vxl','hva','tmp','wav','aud')){WriteNew (Join-Path $presence ('dummy.'+$e)) 'Presence only; deliberately not a valid decoded asset.'}
$base=@('--rules',$rules,'--replay-sandbox','--attacker','TESTA','--defender','TESTB','--ticks','80','--distance','3','--seed','12345');$results=@{}
foreach($name in @('none','partial','complete','empty','invalid','missing-events','desync')){
 $cwd=Join-Path $OutputDirectory $name;$json=Join-Path $cwd 'report.json';$legacy=Join-Path $cwd 'replay.txt';$argv=$base+@('--output',$legacy,'--report-json',$json);$expected=0
 if($name -in @('partial','complete','empty','invalid')){$argv+=@('--runtime-events',(Join-Path $InputDirectory ($name+'-events.txt')))}
 if($name -eq 'missing-events'){$argv+=@('--runtime-events',(Join-Path $InputDirectory 'absent-events.txt'))}
 if($name -in @('invalid','missing-events')){$expected=1}
 if($name -eq 'desync'){$argv+=@('--desync-at','40')}
 $v=Native $name $argv $expected;$j=Json $json;CheckSource $j.provenance 'offline-replay-sandbox'
 Expect ([IO.File]::ReadAllText($v.stdout.path).Contains('production runtime acceptance: not-run')) 'Text scope missing'
 if($expected -eq 0){
  Expect ($j.success) 'Selected offline case failed'
  $text=[IO.File]::ReadAllText($legacy);Expect ($text.StartsWith('ConcordRTS Replay v1'+[char]10) -and $text.Contains('[EvidenceProvenance]')) 'Original header or additive section lost'
  $embedded=($text -split 'json=',2)[1]|ConvertFrom-Json -Depth 100
  Expect (($embedded|ConvertTo-Json -Depth 100 -Compress) -ceq ($j.provenance|ConvertTo-Json -Depth 100 -Compress)) 'Text/JSON evidence disagree'
  Expect ($text.Contains('finalHash='+$j.finalStateHash) -and $text.Contains('commandIntentHash='+$j.commandIntentHash)) 'Legacy/JSON digests differ'
  $section=($text -split '\[Hashes\]\r?\n',2)[1] -split '\r?\n\[EvidenceProvenance\]',2;$rows=@($section[0] -split '\r?\n'|Where-Object {$_})
  Expect ($rows.Count -eq $j.hashFrames.Count) 'Hash rows lost'
  for($i=0;$i -lt $rows.Count;$i++){$f=$j.hashFrames[$i];Expect ($rows[$i] -ceq (@($f.tick,$f.commandsApplied)+@($f.peerHashes) -join ',')) 'Full hash row mismatch'}
 }else{
  Expect (-not $j.success -and $j.provenance.conclusionSource -ceq 'not-evaluated' -and $j.provenance.input.status -ceq 'load-failed') 'Input failure presented as evaluation'
  Expect (-not(Test-Path -LiteralPath $legacy)) 'Failed input exported completed replay'
 }
 $results[$name]=[ordered]@{receipt=$v;json=$j;legacy=$legacy}
}
Expect ($results.none.json.runtimeEventSource.eventCount -eq 0 -and $results.none.json.legacyFieldFlags.capabilityRuntimeFieldCount -eq 0) 'No-event fallback changed'
Expect ($results.partial.json.runtimeEventSource.eventCount -eq 1 -and $results.partial.json.legacyFieldFlags.capabilitySandboxFallbackFieldCount -gt 0) 'Partial fallback lost'
Expect ($results.complete.json.runtimeEventSource.eventCount -eq 12 -and $results.complete.json.legacyFieldFlags.capabilitySandboxFallbackFieldCount -eq 0 -and -not $results.complete.json.provenance.productionRuntimeValidated) 'Complete fields must remain unverified production evidence'
Expect ($results.desync.json.desyncDetected -and -not $results.desync.json.replayVerified) 'Expected desync contract changed'
$names=@('presence','missing');if($AssetRoot){$names+=,'real-assets'}
foreach($name in $names){
 $cwd=Join-Path $OutputDirectory ('status-'+$name);$json=Join-Path $cwd 'report.json';$asset=if($name -eq 'presence'){$presence}elseif($name -eq 'real-assets'){$AssetRoot}else{Join-Path $OutputDirectory 'absent-assets'}
 $v=Native ('status-'+$name) @('--rules',$rules,'--asset-root',$asset,'--engine-status','--report-json',$json);$j=Json $json;CheckSource $j.provenance 'offline-engine-status'
 Expect ($j.totalCount -eq $j.subsystems.Count -and $j.readyCount+$j.partialCount+$j.missingCount -eq $j.totalCount) 'Inventory totals changed'
 foreach($s in $j.subsystems){
  Expect ($s.evidence.Count -eq $s.evidenceSources.Count -and $s.gapSource -ceq 'unverified-legacy-assessment') 'Row or gap sources missing'
  foreach($source in $s.evidenceSources){Expect ($source -in @('asset-file-presence','configured-input-path','parsed-input-data','asset-discovery-and-parsed-input-data','source-capability-declaration')) 'Unrecognized evidence source'}
 }
 $a=@($j.subsystems|Where-Object name -ceq 'Asset')[0]
 if($name -eq 'presence'){Expect ($a.state -ceq 'Ready' -and -not $j.provenance.productionRuntimeValidated) 'Presence state must not prove decoding/behavior'}
 if($name -eq 'missing'){Expect ($a.state -ceq 'Partial' -and $j.provenance.input.status -ceq 'path-absent-or-unreadable') 'Missing assets promoted'}
 $results['status-'+$name]=[ordered]@{receipt=$v;json=$j}
}
foreach($name in @('environment-complete','environment-invalid','auto-complete','auto-invalid','explicit-empty-with-auto')){
 $json=Join-Path $OutputDirectory ($name+'/report.json');$argv=$base+@('--report-json',$json);$envs=@{};$discovered=''
 $file=Join-Path $InputDirectory $(if($name.EndsWith('invalid')){'invalid-events.txt'}else{'complete-events.txt'})
 if($name.StartsWith('environment')){$envs['CONCORD_REPLAY_RUNTIME_EVENTS']=$file}else{$discovered=$file}
 if($name -eq 'explicit-empty-with-auto'){$argv+=@('--runtime-events',(Join-Path $InputDirectory 'empty-events.txt'))}
 $v=Native $name $argv 0 $envs $discovered;$j=Json $json;CheckSource $j.provenance 'offline-replay-sandbox'
 Expect ($j.provenance.input.kind -ceq $(if($name.StartsWith('environment')){'environment-event-file'}else{'auto-discovered-event-file'})) 'Actual file source missing'
 if($name.EndsWith('invalid')){Expect ($j.provenance.input.status -ceq 'load-failed' -and $j.runtimeEventSource.eventCount -eq 0 -and $j.provenance.input.error) 'Ignored legacy input error hidden'}else{Expect ($j.runtimeEventSource.eventCount -eq 12) 'Automatic import not exercised'}
}
$escaped='D:\missing\"quoted'+[char]1+[char]9+[char]10+[char]13+'\tail';$json=Join-Path $OutputDirectory 'escaped-path/report.json'
$v=Native 'escaped-path' @('--rules',$rules,'--asset-root',$escaped,'--engine-status','--report-json',$json)
Expect ((Json $json).provenance.input.path -ceq $escaped) 'JSON control/quote/slash escaping failed'
$v=Native 'wrong-mode' @('--rules',$rules,'--summary','--report-json',(Join-Path $OutputDirectory 'wrong-mode.json')) 1
Expect (-not(Test-Path -LiteralPath (Join-Path $OutputDirectory 'wrong-mode.json'))) 'Wrong mode accepted JSON'
$existing=Join-Path $OutputDirectory 'existing.json';WriteNew $existing 'existing user output'
$v=Native 'existing-json' @('--rules',$rules,'--engine-status','--report-json',$existing) 1
Expect ([IO.File]::ReadAllText($existing) -ceq 'existing user output') 'Unrequested overwrite'
$alias=Join-Path $OutputDirectory 'alias.txt';$v=Native 'same-output' ($base+@('--output',$alias,'--report-json',$alias)) 1
Expect ([IO.File]::ReadAllText($alias).StartsWith('ConcordRTS Replay v1')) 'JSON overwrote v1'
$v=Native 'missing-parent' @('--rules',$rules,'--engine-status','--report-json',(Join-Path $OutputDirectory 'absent-parent/report.json')) 1
if($BaselineManifest){
 $old=Json $BaselineManifest
 foreach($case in $old.cases){
  Expect ($results.ContainsKey($case.name)) ('Original comparison missing '+$case.name);$now=$results[$case.name]
  if($case.legacyOutput){
   $a=[IO.File]::ReadAllBytes($case.legacyOutput.path);$b=[IO.File]::ReadAllBytes($now.legacy)
   Expect ($b.Length -gt $a.Length) 'Additive bytes missing';$prefix=[byte[]]::new($a.Length);[Array]::Copy($b,$prefix,$a.Length)
   Expect ([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($prefix)) -ceq $case.legacyOutput.sha256) ('Complete original v1 bytes changed: '+$case.name)
  }
  if($case.name.StartsWith('status-')){
   $r=Json $case.receipt.path;$a=[IO.File]::ReadAllText($r.stdout.path);$b=[IO.File]::ReadAllText($now.receipt.stdout.path)
   foreach($pattern in @('(?m)^\[[^\]]+\] (Ready|Partial|Missing)\r?$','(?m)^(Ready|Partial|Missing|Total):[^\r\n]*','(?m)^- [^\r\n]*')){Expect (([regex]::Matches($a,$pattern).Value -join [char]10) -ceq ([regex]::Matches($b,$pattern).Value -join [char]10)) ('Original status/evidence/gaps changed: '+$case.name)}
  }
 }
}
WriteNew (Join-Path $OutputDirectory 'result.json') ([ordered]@{status='passed';checks=$checks;nativeCases=$cases.Count;cases=$cases.ToArray();binary=$exeProof;baseline=$BaselineManifest;testScript=(Proof $PSCommandPath);syntheticInputs='not captured production events';productionRuntimeAcceptance='not-run';ownFutureExit=$null}|ConvertTo-Json -Depth 100)
Write-Output ("Offline report evidence: "+$checks+" checks, "+$cases.Count+" actual native cases passed; production runtime acceptance remains not-run.")