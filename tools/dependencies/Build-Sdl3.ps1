# Build an already supplied, unmodified official source tree. No downloading,
# system installation, global environment changes, cleanup or parallel build.
param([string]$SourceDirectory='', [string]$GameBuildDirectory='build/clang-cl-x64-release')
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $PSScriptRoot '../toolchain/ClangBuildSupport.ps1')
$game=Get-ClangLocalPath $GameBuildDirectory
$cache=Read-ClangCache $game
Assert-ClangCache $cache $game $projectRoot 'x64' 'Release'
if(!$SourceDirectory){$SourceDirectory=Join-Path $projectRoot 'build/dependencies/sdl3-3.4.16/SDL3-3.4.16'}
$source=[IO.Path]::GetFullPath($SourceDirectory)
if(!$source.StartsWith($projectRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Dependency source must be inside this project'}
$archive=Join-Path (Split-Path -Parent $source) 'SDL3-3.4.16.zip'
if(!(Test-Path -LiteralPath $archive) -or (Get-ClangSha $archive) -ne '5F399FDFBC040169EF4418A3CB99CCC5A0641681F7CCC1DA66EEEC3C97FA4076'){throw 'Supply the verified official SDL3-3.4.16.zip next to its extracted source tree'}
# Reject locally changed source instead of labelling it as the pinned release.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::OpenRead($archive)
$official=@{}
$digest=[Security.Cryptography.SHA256]::Create()
try {
 foreach($entry in $zip.Entries){
  if($entry.FullName.EndsWith('/')){continue}
  $relative=$entry.FullName.Substring('SDL3-3.4.16/'.Length)
  if($relative -match '(^|/)\.\.(/|$)' -or [IO.Path]::IsPathRooted($relative)){throw 'Invalid official source entry'}
  $actual=Join-Path $source $relative
  $stream=$entry.Open()
  try{$expected=([BitConverter]::ToString($digest.ComputeHash($stream))).Replace('-','')}finally{$stream.Dispose()}
  if(!(Test-Path -LiteralPath $actual -PathType Leaf) -or (Get-ClangSha $actual) -ne $expected){throw ('Pinned SDL source differs: '+$relative)}
  $official[$relative.Replace('/','\')]=$true
 }
 foreach($file in Get-ChildItem -LiteralPath $source -File -Recurse){
  $relative=$file.FullName.Substring($source.Length+1)
  if(!$official.ContainsKey($relative)){throw ('Unexpected file in pinned SDL source: '+$relative)}
 }
}finally{$digest.Dispose();$zip.Dispose()}
$build=Join-Path $game 'sdl3-3.4.16-release'
$run=Join-Path $build ('evidence-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
[void][IO.Directory]::CreateDirectory($run)
$script:clangRecordRoot=$run
$script:clangCommands=New-Object 'Collections.Generic.List[object]'
$arguments=@('-S',$source.Replace('\','/'),'-B',$build.Replace('\','/'),'-G','Ninja',
 ('-DCMAKE_TOOLCHAIN_FILE='+ (Join-Path $projectRoot 'cmake/dependencies/sdl3-clang-cl.cmake').Replace('\','/')),
 '-DCMAKE_BUILD_TYPE=Release','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DCONCORD_CPU_ARCH=x64',
 '-DSDL_SHARED=ON','-DSDL_STATIC=OFF','-DSDL_TESTS=OFF','-DSDL_TEST_LIBRARY=OFF','-DSDL_EXAMPLES=OFF','-DSDL_INSTALL=OFF',
 '-DSDL_AUDIO=OFF','-DSDL_CAMERA=OFF','-DSDL_JOYSTICK=OFF','-DSDL_HAPTIC=OFF','-DSDL_SENSOR=OFF',
 '-DSDL_RENDER=OFF','-DSDL_GPU=OFF','-DSDL_OPENGL=OFF','-DSDL_OPENGLES=OFF','-DSDL_VULKAN=OFF','-DSDL_DIRECTX=OFF',
 '-DSDL_FORCE_STATIC_VCRT=OFF')
foreach($name in @('CONCORD_LLVM_ROOT','CONCORD_MSVC_ROOT','CONCORD_WINDOWS_SDK_ROOT','CONCORD_WINDOWS_SDK_VERSION','CONCORD_NINJA_EXECUTABLE','CONCORD_MSVC_COMPATIBILITY_VERSION')){
 if(!$cache[$name]){throw "Missing game component $name"};$arguments+=('-D'+$name+'='+$cache[$name].Replace('\','/'))
}
Invoke-ClangTool 'configure' $cache.CMAKE_COMMAND $arguments $projectRoot | Out-Null
Invoke-ClangTool 'build' $cache.CMAKE_COMMAND @('--build',$build,'--target','SDL3-shared','--parallel','1','--verbose','--','-d','keeprsp') $projectRoot | Out-Null
$dll=Join-Path $build 'SDL3.dll'
if((Get-ClangPEMachine $dll) -ne 0x8664){throw 'SDL DLL is not x64'}
Copy-Item -LiteralPath $dll -Destination (Join-Path $game 'SDL3.dll')
if((Get-ClangSha (Join-Path $game 'SDL3.dll')) -ne (Get-ClangSha $dll)){throw 'Build-directory SDL DLL copy mismatch'}
# Preserve third-party notices carried by the actual compiled translation units
# and their recorded SDL headers, including nested-header license blocks.
$dependencies=Invoke-ClangTool 'compile-dependencies' $cache.CONCORD_NINJA_EXECUTABLE @('-C',$build,'-t','deps') $projectRoot
$noticeInputs=@{}
foreach($command in (Get-Content -LiteralPath (Join-Path $build 'compile_commands.json') -Raw|ConvertFrom-Json)){
 $file=[IO.Path]::GetFullPath($command.file)
 if($file.StartsWith($source+'\',[StringComparison]::OrdinalIgnoreCase)){$noticeInputs[$file]=$true}
}
foreach($line in ($dependencies -split "`r?`n")){
 if($line -notmatch '^\s+(.+)$'){continue}
 $file=$Matches[1].Trim()
 if(![IO.Path]::IsPathRooted($file)){continue}
 $file=[IO.Path]::GetFullPath($file)
 if($file.StartsWith($source+'\',[StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $file -PathType Leaf)){$noticeInputs[$file]=$true}
}
$yuvLicense=Join-Path $source 'src/video/yuv2rgb/LICENSE'
$noticeInputs[$yuvLicense]=$true
$notices=New-Object Text.StringBuilder
[void]$notices.AppendLine('SDL3 3.4.16: unmodified notices from the selected source build and its recorded source/header dependencies.')
[void]$notices.AppendLine('The SDL license is provided separately as SDL3-LICENSE.txt. This file also preserves the yuv2rgb binary redistribution notice.')
$blocks=@{};$noticeSources=@()
foreach($file in ($noticeInputs.Keys|Sort-Object)){
 $text=[IO.File]::ReadAllText($file)
 $comments=if($file -eq $yuvLicense){@($text)}else{@([regex]::Matches($text,'/\*[\s\S]*?\*/')|ForEach-Object {$_.Value}|Where-Object {$_ -match '(?i)copyright|public domain|permission|licens|redistribut'})}
 foreach($comment in $comments){
  if($blocks.ContainsKey($comment)){continue};$blocks[$comment]=$true
  [void]$notices.AppendLine();[void]$notices.AppendLine(('--- '+$file.Substring($source.Length+1).Replace('\','/')+' ---'))
  [void]$notices.AppendLine($comment)
 }
 $noticeSources+=@([ordered]@{path=$file;sha256=Get-ClangSha $file})
}
if($noticeSources.Count -lt 227 -or !$blocks.ContainsKey([IO.File]::ReadAllText($yuvLicense))){throw 'SDL third-party notice input closure is incomplete'}
$noticeFile=Join-Path $run 'SDL3-NOTICES.txt'
[IO.File]::WriteAllText($noticeFile,$notices.ToString(),(New-Object Text.UTF8Encoding($false)))
$sources=@(Get-ChildItem -LiteralPath (Join-Path $source 'src'),(Join-Path $source 'include'),(Join-Path $source 'cmake') -File -Recurse | Sort-Object FullName | ForEach-Object {[ordered]@{path=$_.FullName;sha256=Get-ClangSha $_.FullName}})
$sources+=@([ordered]@{path=(Join-Path $source 'CMakeLists.txt');sha256=Get-ClangSha (Join-Path $source 'CMakeLists.txt')})
$manifest=[ordered]@{version='3.4.16';source=$source;archive=$archive;archiveSha256=Get-ClangSha $archive;dll=$dll;sha256=Get-ClangSha $dll;license=(Join-Path $source 'LICENSE.txt');licenseSha256=Get-ClangSha (Join-Path $source 'LICENSE.txt');sourceHashes=$sources;commands=@($script:clangCommands.ToArray());buildDirectory=$build;scope='video/events/software surface only; C ABI; x64 clang-cl /MD; optional adjacent DLL'}
$manifest.notices=$noticeFile;$manifest.noticesSha256=Get-ClangSha $noticeFile;$manifest.noticeSources=$noticeSources
$manifest|ConvertTo-Json -Depth 15|Set-Content -LiteralPath (Join-Path $run 'sdl3-dependency.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $run 'sdl3-dependency.json') -Destination (Join-Path $game 'sdl3-dependency.json')
Write-Output ('Built optional SDL3 3.4.16: '+$manifest.sha256)
