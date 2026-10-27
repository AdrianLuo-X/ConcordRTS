# Shared by the explicit Clang build and its deployment-contract checks.
# Windows PowerShell 5.1 compatible; never changes persistent environment.
function Get-ClangSha([string]$Path) {
    $s=[IO.File]::OpenRead($Path);$h=[Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($h.ComputeHash($s)).Replace('-','') } finally {$s.Dispose();$h.Dispose()}
}
function Get-ClangLocalPath([string]$Path) {
    if (!$Path) {$Path='build/clang-cl-x64-release'}
    $full=[IO.Path]::GetFullPath($(if([IO.Path]::IsPathRooted($Path)){$Path}else{Join-Path $projectRoot $Path}))
    $prefix=(Join-Path $projectRoot 'build')+[IO.Path]::DirectorySeparatorChar
    if (!$full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) {throw 'Clang output must be inside this project build/ directory.'}
    Assert-ClangOrdinaryPath $full
    return $full
}
function Assert-ClangOrdinaryPath([string]$Path) {
    $probe=[IO.Path]::GetFullPath($Path)
    while ($probe -and $probe.Length -ge $projectRoot.Length) {
        if ((Test-Path -LiteralPath $probe) -and ((Get-Item -LiteralPath $probe -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {throw "Linked output path refused: $probe"}
        if ($probe -ieq $projectRoot) {break};$probe=[IO.Path]::GetDirectoryName($probe)
    }
}
function Read-ClangCache([string]$Directory) {
    $cache=@{};$file=Join-Path $Directory 'CMakeCache.txt'
    if (Test-Path -LiteralPath $file) {foreach($line in [IO.File]::ReadLines($file)){if($line -match '^([^#/:][^:]*):[^=]*=(.*)$'){$cache[$Matches[1]]=$Matches[2]}}}
    return $cache
}
function Assert-ClangCache($Cache,[string]$Directory,[string]$Source,[string]$Architecture,[string]$Config) {
    foreach($pair in @(@('CMAKE_GENERATOR','Ninja'),@('CONCORD_CPU_ARCH',$Architecture),@('CMAKE_BUILD_TYPE',$Config))) {
        if (!$Cache.ContainsKey($pair[0]) -or $Cache[$pair[0]] -cne $pair[1]) {throw "Cache $($pair[0]) differs from explicit Clang selection in $Directory"}
    }
    foreach($pair in @(@('CMAKE_HOME_DIRECTORY',$Source),@('CMAKE_CACHEFILE_DIR',$Directory))) {
        if (!$Cache.ContainsKey($pair[0]) -or [IO.Path]::GetFullPath($Cache[$pair[0]]) -ine [IO.Path]::GetFullPath($pair[1])) {throw "Cache source/output mismatch: $Directory"}
    }
    if (!$Cache.CONCORD_CONFIGURED_TOOLCHAIN_IDENTITY) {throw 'Not an owned Clang toolchain cache.'}
    $flags="$($Cache.CMAKE_CXX_FLAGS) $($Cache.CMAKE_CXX_FLAGS_RELEASE) $($Cache.CMAKE_EXE_LINKER_FLAGS)"
    if ($flags -notmatch '/EHsc' -or $flags -notmatch '/O2' -or $flags -notmatch '/DNDEBUG' -or $flags -notmatch '-fno-fast-math' -or $flags -notmatch '-ffp-contract=off' -or
        $flags -match '(?i)(?:/arch:AVX|(?:-m|/clang:-m)(?:avx|arch=native)|(?<!no-)fast-math|ffp-contract=(?:on|fast)|/fp:fast|[/-]EH\S*-|fno-exceptions|fno-cxx-exceptions|[/-]U\s*NDEBUG)') {throw 'Clang EH/optimization/FP/baseline ISA contract differs.'}
    $optimization=[regex]::Matches("$($Cache.CMAKE_CXX_FLAGS) $($Cache.CMAKE_CXX_FLAGS_RELEASE)",'(?<!\S)[/-]O[12dx](?!\S)')
    if(!$optimization.Count -or $optimization[$optimization.Count-1].Value -notmatch 'O2$'){throw 'Final Release optimization must remain /O2.'}
    $expected=Join-Path $Cache.CONCORD_LLVM_ROOT 'bin/clang-cl.exe'
    $compilerText=Get-Content -LiteralPath (Join-Path $Directory "compiler-contract-$Config.txt") -Raw
    if($compilerText -notmatch '(?m)^compiler=([^\r\n]+)' -or [IO.Path]::GetFullPath($Matches[1]) -ine [IO.Path]::GetFullPath($expected) -or $compilerText -notmatch '(?m)^compiler_id=Clang\r?$' -or $compilerText -notmatch '(?m)^frontend=MSVC\r?$'){throw 'Generated compiler/frontend is not selected clang-cl.'}
    foreach($pair in @(@('CMAKE_LINKER',(Join-Path $Cache.CONCORD_LLVM_ROOT 'bin/lld-link.exe')),@('CMAKE_AR',(Join-Path $Cache.CONCORD_LLVM_ROOT 'bin/llvm-lib.exe')),
        @('CMAKE_MAKE_PROGRAM',$Cache.CONCORD_NINJA_EXECUTABLE),
        @('CMAKE_MT',(Join-Path $Cache.CONCORD_WINDOWS_SDK_ROOT "bin/$($Cache.CONCORD_WINDOWS_SDK_VERSION)/x64/mt.exe")))) {
        if (!$Cache[$pair[0]] -or [IO.Path]::GetFullPath($Cache[$pair[0]]) -ine [IO.Path]::GetFullPath($pair[1]) -or !(Test-Path -LiteralPath $pair[1] -PathType Leaf)) {throw "Cache tool mismatch/missing: $($pair[0])"}
    }
    $rc=Join-Path $Cache.CONCORD_WINDOWS_SDK_ROOT "bin/$($Cache.CONCORD_WINDOWS_SDK_VERSION)/x64/rc.exe"
    $rcMetadata=@(Get-ChildItem -LiteralPath (Join-Path $Directory 'CMakeFiles') -Filter CMakeRCCompiler.cmake -Recurse -File)
    if(!(Test-Path -LiteralPath $rc) -or !$rcMetadata.Count -or (Get-Content -LiteralPath $rcMetadata[0].FullName -Raw) -notmatch [regex]::Escape($rc.Replace('\','/'))){throw 'Generated RC identity differs.'}
    foreach($dir in @((Join-Path $Cache.CONCORD_MSVC_ROOT 'include'),(Join-Path $Cache.CONCORD_MSVC_ROOT "lib/$Architecture"),
        (Join-Path $Cache.CONCORD_WINDOWS_SDK_ROOT "Include/$($Cache.CONCORD_WINDOWS_SDK_VERSION)/ucrt"),(Join-Path $Cache.CONCORD_WINDOWS_SDK_ROOT "Lib/$($Cache.CONCORD_WINDOWS_SDK_VERSION)/um/$Architecture"))) {
        if (!(Test-Path -LiteralPath $dir -PathType Container)) {throw "Missing selected headers/libraries: $dir"}
    }
}
function Assert-ClangCommands([string]$Directory,[bool]$Helper,[string]$Config='Release') {
    $file=Join-Path $Directory 'compile_commands.json'
    if (!(Test-Path -LiteralPath $file)) {throw "Missing compilation metadata: $file"}
    $commands=Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
    $target=if($Helper){'BinkFrameExtractor'}else{'concord_runtime'}
    $cache=Read-ClangCache $Directory
    $compiler=[IO.Path]::GetFullPath((Join-Path $cache.CONCORD_LLVM_ROOT 'bin/clang-cl.exe'))
    $selected=@($commands | Where-Object {$_.command -match ('CMakeFiles[/\\]'+$target+'\.dir')})
    if (!$selected.Count) {throw "No actual commands for $target"}
    foreach($command in $selected) {
        $text=$command.command
        $triple=if($Helper){'i686-pc-windows-msvc'}else{'x86_64-pc-windows-msvc'}
        $standard=if($Helper){'[/-]std:c\+\+17'}else{'[/-]std:c\+\+20'}
        $crt=if($Helper){'[/-]MT'}else{'[/-]MD'}
        if ($text -notmatch [regex]::Escape($triple) -or $text -notmatch $standard -or $text -notmatch ('(?<!\S)'+$crt+'(?!\S)') -or $text -notmatch '/EHsc' -or $text -notmatch '-ffp-contract=off' -or
            $text -match '(?i)(/arch:AVX|-mavx|-march=native|-ffast-math|ffp-contract=(on|fast)|[/-]EH\S*-|fno-exceptions|fno-cxx-exceptions|[/-]U\s*NDEBUG)') {throw "Generated $target compile contract differs: $($command.file)"}
        if(!$text.TrimStart('"').StartsWith($compiler,[StringComparison]::OrdinalIgnoreCase) -or $text -notmatch [regex]::Escape('-fms-compatibility-version='+$cache.CONCORD_MSVC_COMPATIBILITY_VERSION)){throw 'Generated compiler/ABI compatibility identity differs.'}
        $crts=[regex]::Matches($text,'(?<!\S)[/-]M[DT]d?(?!\S)')
        foreach($item in $crts){if($item.Value -notmatch ('^'+$crt+'$')){throw 'Generated CRT overrides the target contract.'}}
        if($Config -eq 'Release'){
            $opts=[regex]::Matches($text,'(?<!\S)[/-]O[12dx](?!\S)')
            if(!$opts.Count -or $opts[$opts.Count-1].Value -notmatch 'O2$' -or $text -notmatch '[/-]DNDEBUG(?!\w)'){throw 'Generated Release optimization/NDEBUG differs.'}
        }
        if (!$Helper -and $text -notmatch 'CONCORD_RULES_ABI_REVISION=2026092202') {throw 'Runtime ABI revision differs.'}
    }
}
function Quote-ClangArgument([string]$Value) {
    # CommandLineToArgvW escaping, including quotes and trailing backslashes.
    return '"'+[regex]::Replace([regex]::Replace($Value,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'
}
function Invoke-ClangTool([string]$Name,[string]$Program,[string[]]$Arguments,[string]$Directory) {
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName=$Program;$info.Arguments=(@($Arguments | ForEach-Object {Quote-ClangArgument $_}) -join ' ');$info.WorkingDirectory=$Directory
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    foreach($key in @($info.EnvironmentVariables.Keys)){if($key -like 'CONCORD_*' -or $key -in @('INCLUDE','LIB','LIBPATH','CL','_CL_','LINK','_LINK_','VCINSTALLDIR','VCToolsInstallDir','WindowsSdkDir')){$info.EnvironmentVariables.Remove($key)}}
    $info.EnvironmentVariables['PATH']=$env:SystemRoot+'\System32;'+$env:SystemRoot
    $info.EnvironmentVariables['TMP']=$script:clangRecordRoot;$info.EnvironmentVariables['TEMP']=$script:clangRecordRoot
    $timer=[Diagnostics.Stopwatch]::StartNew();$process=[Diagnostics.Process]::Start($info)
    try {
        $out=$process.StandardOutput.ReadToEndAsync();$err=$process.StandardError.ReadToEndAsync();$process.WaitForExit();$timer.Stop()
        $stdout=$out.GetAwaiter().GetResult();$stderr=$err.GetAwaiter().GetResult()
        $outFile=Join-Path $script:clangRecordRoot "$Name.stdout.log";$errFile=Join-Path $script:clangRecordRoot "$Name.stderr.log"
        [IO.File]::WriteAllText($outFile,$stdout);[IO.File]::WriteAllText($errFile,$stderr)
        $record=[ordered]@{name=$Name;executable=$Program;executableSha256=Get-ClangSha $Program;arguments=$Arguments;cwd=$Directory;exitCode=$process.ExitCode;seconds=$timer.Elapsed.TotalSeconds;
            environment=@{PATH=$info.EnvironmentVariables['PATH'];TMP=$script:clangRecordRoot;TEMP=$script:clangRecordRoot;cleared='developer flags/includes/libraries and CONCORD_*'};stdoutSha256=Get-ClangSha $outFile;stderrSha256=Get-ClangSha $errFile}
        $script:clangCommands.Add($record);$record|ConvertTo-Json -Depth 10|Set-Content (Join-Path $script:clangRecordRoot "$Name.command.json") -Encoding UTF8
        Write-Host "$Name`: exit $($process.ExitCode) ($([Math]::Round($timer.Elapsed.TotalSeconds,2))s)"
        if($process.ExitCode){throw "$Name failed: exit $($process.ExitCode); see $script:clangRecordRoot"}
        return $stdout
    } finally {$process.Dispose()}
}
function Get-ClangArtifact([string]$Directory,[string]$Target) {
    $contract=Join-Path $Directory 'compiler-contract-Release.txt'
    $text=Get-Content -LiteralPath $contract -Raw
    if($text -notmatch ('(?ms)^target='+[regex]::Escape($Target)+'\r?\n.*?^output=([^\r\n]+)')){throw "No generated artifact path for $Target"}
    $path=[IO.Path]::GetFullPath($Matches[1]);Assert-ClangOrdinaryPath $path
    if (!$path.StartsWith($Directory.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing/outside artifact: $path"}
    return $path
}
function Get-ClangPEMachine([string]$Path) {
    $s=[IO.File]::OpenRead($Path);$r=New-Object IO.BinaryReader($s)
    try {if($r.ReadUInt16() -ne 0x5a4d){throw "Not PE: $Path"};$s.Position=0x3c;$offset=$r.ReadInt32();$s.Position=$offset;if($r.ReadUInt32() -ne 0x4550){throw 'Invalid PE signature'};return $r.ReadUInt16()}finally{$r.Dispose();$s.Dispose()}
}
function New-ClangStage([string]$Stage,[string]$Runtime,[string]$Helper,[string]$CRT,[string]$ReadObj) {
    $sources=@{'concord_runtime.exe'=$Runtime;'BinkFrameExtractor.exe'=$Helper}
    $edges=New-Object 'Collections.Generic.List[object]';$inspected=@{};$pending=New-Object 'Collections.Generic.Queue[string]'
    $pending.Enqueue('concord_runtime.exe');$pending.Enqueue('BinkFrameExtractor.exe')
    # P11 is optional for the reference backend. When explicitly compiled in,
    # stage its independently built C DLL and inspect its real imports too.
    $sdl=$null
    $productCache=Read-ClangCache (Split-Path -Parent $Runtime)
    if($productCache.CONCORD_ENABLE_SDL3_WINDOW -eq 'ON'){
        $dependencyFile=Join-Path (Split-Path -Parent $Runtime) 'sdl3-dependency.json'
        if(!(Test-Path -LiteralPath $dependencyFile)){throw 'Build the pinned optional SDL3 dependency first with tools/dependencies/Build-Sdl3.ps1'}
        $sdl=Get-Content -LiteralPath $dependencyFile -Raw|ConvertFrom-Json
        if($sdl.version -ne '3.4.16' -or $sdl.archiveSha256 -ne '5F399FDFBC040169EF4418A3CB99CCC5A0641681F7CCC1DA66EEEC3C97FA4076' -or (Get-ClangSha $sdl.dll) -ne $sdl.sha256 -or
            (Get-ClangSha $sdl.archive) -ne $sdl.archiveSha256 -or (Get-ClangSha $sdl.license) -ne $sdl.licenseSha256 -or !$sdl.notices -or (Get-ClangSha $sdl.notices) -ne $sdl.noticesSha256){throw 'SDL dependency identity changed'}
        foreach($file in $sdl.sourceHashes){if((Get-ClangSha $file.path) -ne $file.sha256){throw ('SDL dependency source changed: '+$file.path)}}
        $sources['SDL3.dll']=$sdl.dll;$pending.Enqueue('SDL3.dll')
    }
    while($pending.Count){
        $name=$pending.Dequeue();if($inspected.ContainsKey($name)){continue};$inspected[$name]=$true
        $source=$sources[$name];$machine=Get-ClangPEMachine $source
        $expected=if($name -ieq 'BinkFrameExtractor.exe'){0x14c}else{0x8664}
        if($machine -ne $expected){throw "Wrong PE architecture: $source"}
        $raw=Invoke-ClangTool ('imports-'+$name) $ReadObj @('--coff-imports',$source) $projectRoot
        foreach($match in [regex]::Matches($raw,'(?m)^\s+Name: ([^\r\n]+)')){
            $dll=$match.Groups[1].Value.Trim();$crtPath=Join-Path $CRT $dll
            if($dll -match '(?i)^(msvcp|vcruntime|concrt)\d.*\.dll$'){
                if($machine -ne 0x8664 -or !(Test-Path -LiteralPath $crtPath -PathType Leaf)){throw "Unresolved/incorrect CRT dependency: $dll"}
                $sources[$dll]=$crtPath;$pending.Enqueue($dll);$kind='Microsoft-redistributable';$resolved=$crtPath
            }elseif($dll -match '(?i)^api-ms-|^ext-ms-'){$kind='Windows-API-set';$resolved='Windows loader API-set contract'}
            else {
                $osDir=if($machine -eq 0x14c){'SysWOW64'}else{'System32'};$resolved=Join-Path (Join-Path $env:SystemRoot $osDir) $dll
                if(!(Test-Path -LiteralPath $resolved -PathType Leaf)){throw "Unresolved dependency $dll; it is neither selected CRT nor Windows system DLL."}
                if((Get-ClangPEMachine $resolved) -ne $machine){throw "Wrong architecture Windows dependency: $resolved"};$kind='Windows-system'
            }
            $edges.Add([ordered]@{from=$name;architecture=$(if($machine -eq 0x14c){'x86'}else{'x64'});import=$dll;kind=$kind;resolved=$resolved;distributed=($kind -eq 'Microsoft-redistributable')})
        }
    }
    $files=@(foreach($name in ($sources.Keys|Sort-Object)){
        $dest=Join-Path $Stage $name;Copy-Item -LiteralPath $sources[$name] -Destination $dest
        $sha=Get-ClangSha $sources[$name];if($sha -ne (Get-ClangSha $dest)){throw "Stage hash mismatch: $name"}
        [ordered]@{name=$name;source=$sources[$name];staged=$dest;sha256=$sha;machine=Get-ClangPEMachine $dest}
    })
    if($sdl){
        $licenseName='SDL3-LICENSE.txt';$dest=Join-Path $Stage $licenseName
        Copy-Item -LiteralPath $sdl.license -Destination $dest
        if((Get-ClangSha $dest) -ne $sdl.licenseSha256){throw 'SDL license stage mismatch'}
        $files+=@([ordered]@{name=$licenseName;source=$sdl.license;staged=$dest;sha256=$sdl.licenseSha256;machine=0})
        $noticeName='SDL3-NOTICES.txt';$noticeDest=Join-Path $Stage $noticeName
        Copy-Item -LiteralPath $sdl.notices -Destination $noticeDest
        if((Get-ClangSha $noticeDest) -ne $sdl.noticesSha256){throw 'SDL third-party notice stage mismatch'}
        $files+=@([ordered]@{name=$noticeName;source=$sdl.notices;staged=$noticeDest;sha256=$sdl.noticesSha256;machine=0})
        Copy-Item -LiteralPath $dependencyFile -Destination (Join-Path $Stage 'sdl3-dependency.json')
    }
    $bink=Join-Path $projectRoot 'RedAlert2/GameMD/Binkw32.dll'
    $dynamic=@([ordered]@{name='Binkw32.dll';architecture='x86';kind='user-game-asset';source=$bink;available=(Test-Path -LiteralPath $bink);sha256=$(if(Test-Path -LiteralPath $bink){Get-ClangSha $bink});distributed=$false},
        [ordered]@{name='xaudio2_9.dll';architecture='x64';kind='Windows-system-dynamic';source=(Join-Path $env:SystemRoot 'System32/xaudio2_9.dll');distributed=$false})
    if($sdl){$dynamic+=@([ordered]@{name='SDL3.dll';architecture='x64';kind='optional-window-provider';source=$sdl.dll;sha256=$sdl.sha256;version=$sdl.version;archiveSha256=$sdl.archiveSha256;licenseSha256=$sdl.licenseSha256;noticesSha256=$sdl.noticesSha256;distributed=$true;load='adjacent executable directory only; no Win32 reference dependency'})}
    return [ordered]@{schema=2;toolchain='clang-cl';builtAtUtc=[DateTime]::UtcNow.ToString('o');sourceRoot=$projectRoot;buildDirectory='';configuration='Release';stage=$Stage;parallel=1;
        files=$files;imports=@($edges.ToArray());dynamicDependencies=$dynamic;components=@{};cacheSha256='';helperCacheSha256='';commands=@();
        sourceHashes=@(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'src'),(Join-Path $projectRoot 'cmake') -File -Recurse | Sort-Object FullName | ForEach-Object {[ordered]@{path=$_.FullName;sha256=Get-ClangSha $_.FullName}})}
}
function Publish-ClangStage([string]$Stage,$Manifest) {
    $bin=Join-Path $projectRoot 'bin';Assert-ClangOrdinaryPath $bin
    foreach($file in $Manifest.files){Assert-ClangOrdinaryPath (Join-Path $bin $file.name);if((Get-ClangSha (Join-Path $Stage $file.name)) -ne $file.sha256){throw "Stage changed before deployment: $($file.name)"}}
    foreach($file in $Manifest.sourceHashes){if((Get-ClangSha $file.path) -ne $file.sha256){throw "Source changed before deployment: $($file.path)"}}
    [void][IO.Directory]::CreateDirectory($bin)
    $backup=Join-Path $Stage 'previous-bin';[void][IO.Directory]::CreateDirectory($backup)
    $names=@($Manifest.files|ForEach-Object {$_.name})+@('build-provenance.json')
    $before=@(foreach($name in $names){$path=Join-Path $bin $name;if(Test-Path -LiteralPath $path){$sha=Get-ClangSha $path;Copy-Item -LiteralPath $path -Destination (Join-Path $backup $name);if((Get-ClangSha (Join-Path $backup $name)) -ne $sha -or (Get-ClangSha $path) -ne $sha){throw "Unverified/changing previous package: $name"};[ordered]@{name=$name;sha256=$sha}}})
    $before|ConvertTo-Json -Depth 5|Set-Content (Join-Path $Stage 'previous-bin.json') -Encoding UTF8
    try {
        foreach($file in $Manifest.files){$dest=Join-Path $bin $file.name;Copy-Item -LiteralPath (Join-Path $Stage $file.name) -Destination $dest -Force;if((Get-ClangSha $dest) -ne $file.sha256){throw "Deployment hash mismatch: $($file.name)"}}
        # Provenance is the final commit marker for the whole verified group.
        Copy-Item -LiteralPath (Join-Path $Stage 'build-provenance.json') -Destination (Join-Path $bin 'build-provenance.json') -Force
        foreach($file in $Manifest.files){if((Get-ClangSha (Join-Path $bin $file.name)) -ne $file.sha256){throw 'Final deployed group differs.'}}
        [ordered]@{status='passed';bin=$bin;files=$Manifest.files;previous=$before}|ConvertTo-Json -Depth 12|Set-Content (Join-Path $Stage 'deployment.json') -Encoding UTF8
    }catch{
        $failure=$_.Exception.Message;$restoreErrors=New-Object 'Collections.Generic.List[string]'
        foreach($file in $before){try{$dest=Join-Path $bin $file.name;if(!(Test-Path -LiteralPath $dest) -or (Get-ClangSha $dest) -ne $file.sha256){Copy-Item -LiteralPath (Join-Path $backup $file.name) -Destination $dest -Force};if((Get-ClangSha $dest) -ne $file.sha256){throw 'Rollback hash mismatch'}}catch{$restoreErrors.Add($file.name+': '+$_.Exception.Message)}}
        [ordered]@{status='failed';error=$failure;previousRestored=($restoreErrors.Count -eq 0);restoreErrors=@($restoreErrors.ToArray());retainedNewFiles='No files removed; previous complete group/provenance restored when present.'}|ConvertTo-Json -Depth 10|Set-Content (Join-Path $Stage 'deployment.json') -Encoding UTF8
        throw "Deployment failed: $failure. Rollback errors: $($restoreErrors -join '; '); see $Stage/deployment.json"
    }
}
