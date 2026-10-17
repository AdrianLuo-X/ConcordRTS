param(
    [string]$BuildDirectory = 'build/clang-cl-x64-release',
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [string]$CMakeExecutable, [string]$NinjaExecutable, [string]$LLVMRoot,
    [string]$MSVCRoot, [string]$WindowsSDKRoot, [string]$WindowsSDKVersion,
    [string]$MSVCCompatibilityVersion, [string]$CRTRoot,
    [switch]$StageOnly, [switch]$ValidateOnly
)
$ErrorActionPreference = 'Stop'
if ($Configuration -ne 'Release' -and !$ValidateOnly) { throw 'Daily Clang product builds remain Release; Debug tests use a separate directory.' }
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $PSScriptRoot 'ClangBuildSupport.ps1')
$buildRoot = Get-ClangLocalPath $BuildDirectory
foreach ($name in @('CL','_CL_','LINK','_LINK_')) {
    if ([Environment]::GetEnvironmentVariable($name,'Process')) { throw "Option injection $name is set; use a clean process." }
}
$cache = Read-ClangCache $buildRoot
# Existing directories pin all components. New directories accept explicit full
# paths or tools discoverable in an ordinary terminal; nothing is installed.
foreach ($pair in @(@('CMakeExecutable','CMAKE_COMMAND'),@('NinjaExecutable','CONCORD_NINJA_EXECUTABLE'),
    @('LLVMRoot','CONCORD_LLVM_ROOT'),@('MSVCRoot','CONCORD_MSVC_ROOT'),
    @('WindowsSDKRoot','CONCORD_WINDOWS_SDK_ROOT'),@('WindowsSDKVersion','CONCORD_WINDOWS_SDK_VERSION'),
    @('MSVCCompatibilityVersion','CONCORD_MSVC_COMPATIBILITY_VERSION'))) {
    $value = Get-Variable -Name $pair[0] -ValueOnly
    if (!$value -and $cache.ContainsKey($pair[1])) { Set-Variable -Name $pair[0] -Value $cache[$pair[1]] }
    elseif ($value -and $cache.ContainsKey($pair[1]) -and $value.Replace('\','/').TrimEnd('/') -ine $cache[$pair[1]].Replace('\','/').TrimEnd('/')) {
        throw "Component $($pair[0]) conflicts with this cache; select a fresh project-local directory."
    }
}
foreach ($pair in @(@('CMakeExecutable','cmake.exe'),@('NinjaExecutable','ninja.exe'))) {
    if (!(Get-Variable $pair[0] -ValueOnly)) {
        $found = Get-Command $pair[1] -ErrorAction SilentlyContinue
        if (!$found) { throw "Missing $($pair[1]); pass -$($pair[0]) with a verified full path. No fallback or installation was performed." }
        Set-Variable $pair[0] $found.Source
    }
}
if (!$LLVMRoot) {
    $found = Get-Command clang-cl.exe -ErrorAction SilentlyContinue
    if (!$found) { throw 'Missing LLVM; pass -LLVMRoot. No MSVC fallback was performed.' }
    $LLVMRoot = Split-Path -Parent (Split-Path -Parent $found.Source)
}
foreach ($name in @('MSVCRoot','WindowsSDKRoot','WindowsSDKVersion','MSVCCompatibilityVersion')) {
    if (!(Get-Variable $name -ValueOnly)) { throw "Pass -$name explicitly for a new directory (Microsoft STL/CRT and SDK remain required)." }
}
foreach ($path in @($CMakeExecutable,$NinjaExecutable,(Join-Path $LLVMRoot 'bin/clang-cl.exe'),(Join-Path $LLVMRoot 'bin/lld-link.exe'),(Join-Path $LLVMRoot 'bin/llvm-readobj.exe'))) {
    if (![IO.Path]::IsPathRooted($path) -or !(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing absolute tool path: $path" }
}
if ($cache.Count) { Assert-ClangCache $cache $buildRoot $projectRoot 'x64' $Configuration; Assert-ClangCommands $buildRoot $false $Configuration }
if ($ValidateOnly) {
    if (!$cache.Count) { throw '-ValidateOnly requires an existing Clang cache.' }
    $helperRoot = Join-Path $buildRoot 'bink_decoder32-x86'
    $helperCache = Read-ClangCache $helperRoot
    if ($helperCache.Count) { Assert-ClangCache $helperCache $helperRoot (Join-Path $projectRoot 'tools') 'x86' $Configuration; Assert-ClangCommands $helperRoot $true $Configuration }
    Write-Host 'Clang cache, compiler, flags and helper contracts passed; no configure/build/deployment.'
    exit 0
}
if (!$CRTRoot) {
    $vcRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MSVCRoot))
    $redist = Join-Path $vcRoot 'Redist/MSVC'
    if (!(Test-Path -LiteralPath $redist)) { throw 'Pass -CRTRoot for the licensed x64 Microsoft CRT redistributable directory.' }
    $version = Get-ChildItem -LiteralPath $redist -Directory | Where-Object Name -Match '^\d+\.\d+\.\d+$' | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    $crt = if ($version) { Get-ChildItem -LiteralPath (Join-Path $version.FullName 'x64') -Directory | Where-Object Name -Like 'Microsoft.VC*.CRT' | Select-Object -First 1 }
    if (!$crt) { throw 'Missing x64 redistributable source; pass -CRTRoot.' }
    $CRTRoot = $crt.FullName
}
if (![IO.Path]::IsPathRooted($CRTRoot) -or !(Test-Path -LiteralPath $CRTRoot -PathType Container)) { throw 'CRTRoot must be an existing absolute redistributable directory.' }
$runRoot = Join-Path $buildRoot ('deployment-stage/' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
[void][IO.Directory]::CreateDirectory($runRoot)
$script:clangCommands = New-Object 'Collections.Generic.List[object]'
$script:clangRecordRoot = $runRoot
$lock = $null
try {
    $lock = [IO.File]::Open((Join-Path $projectRoot 'build/clang-build.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    $sourceBefore=@(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'src'),(Join-Path $projectRoot 'cmake') -File -Recurse | ForEach-Object {[ordered]@{path=$_.FullName;sha256=Get-ClangSha $_.FullName}})
    $sourceBefore+=@([ordered]@{path=(Join-Path $projectRoot 'CMakeLists.txt');sha256=Get-ClangSha (Join-Path $projectRoot 'CMakeLists.txt')},[ordered]@{path=(Join-Path $projectRoot 'tools/BinkFrameExtractor.cpp');sha256=Get-ClangSha (Join-Path $projectRoot 'tools/BinkFrameExtractor.cpp')})
    $sourceBefore+=@([ordered]@{path=(Join-Path $projectRoot 'tools/CMakeLists.txt');sha256=Get-ClangSha (Join-Path $projectRoot 'tools/CMakeLists.txt')})
    foreach ($pair in @(@('cmake',$CMakeExecutable),@('ninja',$NinjaExecutable),@('clang',(Join-Path $LLVMRoot 'bin/clang-cl.exe')),@('lld',(Join-Path $LLVMRoot 'bin/lld-link.exe')))) {
        Invoke-ClangTool ($pair[0]+'-version') $pair[1] @('--version') $projectRoot | Out-Null
    }
    $arguments = @('-CMakeExecutable',$CMakeExecutable,'-NinjaExecutable',$NinjaExecutable,'-LLVMRoot',$LLVMRoot,'-MSVCRoot',$MSVCRoot,
        '-WindowsSDKRoot',$WindowsSDKRoot,'-WindowsSDKVersion',$WindowsSDKVersion,'-MSVCCompatibilityVersion',$MSVCCompatibilityVersion,
        '-Architecture','x64','-Configuration',$Configuration,'-BuildDirectory',$buildRoot)
    $hostExe = (Get-Process -Id $PID).Path
    Invoke-ClangTool 'configure' $hostExe (@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'Configure-WindowsClang.ps1')) + $arguments) $projectRoot | Out-Null
    Assert-ClangCache (Read-ClangCache $buildRoot) $buildRoot $projectRoot 'x64' $Configuration
    Assert-ClangCommands $buildRoot $false
    Invoke-ClangTool 'build' $CMakeExecutable @('--build',$buildRoot,'--parallel','1','--verbose','--target','concord_runtime','--','-d','keeprsp') $projectRoot | Out-Null
    $helperRoot = Join-Path $buildRoot 'bink_decoder32-x86'
    Assert-ClangCache (Read-ClangCache $helperRoot) $helperRoot (Join-Path $projectRoot 'tools') 'x86' $Configuration
    Assert-ClangCommands $helperRoot $true
    foreach($file in $sourceBefore){if((Get-ClangSha $file.path) -ne $file.sha256){throw "Source changed during build; deployment refused: $($file.path)"}}
    $runtime = Get-ClangArtifact $buildRoot 'concord_runtime'
    $helper = Get-ClangArtifact $helperRoot 'BinkFrameExtractor'
    $stagedHelper = Join-Path (Split-Path -Parent $runtime) 'BinkFrameExtractor.exe'
    if ((Get-ClangSha $helper) -ne (Get-ClangSha $stagedHelper)) { throw 'Helper stage differs from the x86 build product.' }
    $manifest = New-ClangStage $runRoot $runtime $helper $CRTRoot (Join-Path $LLVMRoot 'bin/llvm-readobj.exe')
    $manifest.sourceHashes=$sourceBefore
    foreach($file in @('compile_commands.json','compiler-contract-Release.txt')){Copy-Item -LiteralPath (Join-Path $buildRoot $file) -Destination (Join-Path $runRoot $file);Copy-Item -LiteralPath (Join-Path $helperRoot $file) -Destination (Join-Path $runRoot ('helper-'+$file))}
    $manifest.buildDirectory = $buildRoot
    $manifest.configuration = $Configuration
    $manifest.components = [ordered]@{cmake=$CMakeExecutable;ninja=$NinjaExecutable;llvm=$LLVMRoot;msvc=$MSVCRoot;sdk=$WindowsSDKRoot;sdkVersion=$WindowsSDKVersion;compatibility=$MSVCCompatibilityVersion;crt=$CRTRoot}
    $manifest.cacheSha256 = Get-ClangSha (Join-Path $buildRoot 'CMakeCache.txt')
    $manifest.helperCacheSha256 = Get-ClangSha (Join-Path $helperRoot 'CMakeCache.txt')
    $manifest.commands = @($script:clangCommands.ToArray())
    $manifest | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $runRoot 'build-provenance.json') -Encoding UTF8
    if (!$StageOnly) { Publish-ClangStage $runRoot $manifest }
    Write-Host "Clang complete; stage/manifest: $runRoot; deployed=$(!$StageOnly)."
    exit 0
} catch {
    [ordered]@{status='failed';error=$_.Exception.Message;commands=@($script:clangCommands.ToArray());stage=$runRoot} | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $runRoot 'failure.json') -Encoding UTF8
    Write-Error $_ -ErrorAction Continue
    exit 1
} finally { if ($lock) { $lock.Dispose() } }
