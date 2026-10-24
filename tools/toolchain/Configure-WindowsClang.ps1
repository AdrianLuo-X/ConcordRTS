param(
    [Parameter(Mandatory=$true)][string]$CMakeExecutable,
    [Parameter(Mandatory=$true)][string]$NinjaExecutable,
    [Parameter(Mandatory=$true)][string]$LLVMRoot,
    [Parameter(Mandatory=$true)][string]$MSVCRoot,
    [Parameter(Mandatory=$true)][string]$WindowsSDKRoot,
    [Parameter(Mandatory=$true)][string]$WindowsSDKVersion,
    [ValidateSet('x64','x86')][string]$Architecture='x64',
    [ValidateSet('Release','Debug')][string]$Configuration='Release',
    [string]$BuildDirectory,
    [string]$MSVCCompatibilityVersion,
    [switch]$HelperOnly,
    [switch]$PortableCore
)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$BuildDirectory){
    $suffix=if($HelperOnly){'-helper'}else{''}
    $BuildDirectory='build/clang-cl-'+$Architecture+'-'+$Configuration.ToLowerInvariant()+$suffix
}
if($PortableCore -and $HelperOnly){throw 'PortableCore and HelperOnly are different capabilities'}
if($HelperOnly -and $Architecture -ne 'x86'){throw 'HelperOnly requires Architecture x86'}
if(![IO.Path]::IsPathRooted($BuildDirectory)){$BuildDirectory=Join-Path $projectRoot $BuildDirectory}
$buildPath=[IO.Path]::GetFullPath($BuildDirectory)
$allowedPrefix=[IO.Path]::GetFullPath((Join-Path $projectRoot 'build'))+[IO.Path]::DirectorySeparatorChar
if(!$buildPath.StartsWith($allowedPrefix,[StringComparison]::OrdinalIgnoreCase)){
    throw 'The Clang build directory must be inside this project build/ directory'
}
$ancestor=$buildPath
while($ancestor -and $ancestor.Length -ge $projectRoot.Length){
    if((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Linked build path refused: $ancestor"}
    if($ancestor -ieq $projectRoot){break};$ancestor=[IO.Path]::GetDirectoryName($ancestor)
}
foreach($file in @($CMakeExecutable,$NinjaExecutable,(Join-Path $LLVMRoot 'bin/clang-cl.exe'),(Join-Path $LLVMRoot 'bin/lld-link.exe'))){
    if(!(Test-Path -LiteralPath $file -PathType Leaf)){throw "Missing tool: $file"}
}
$cache=Join-Path $buildPath 'CMakeCache.txt'
if(Test-Path -LiteralPath $cache){
    $text=Get-Content -LiteralPath $cache -Raw
    $cacheIsCore=$text -match '(?m)^CONCORD_PORTABLE_CORE:BOOL=ON\s*$'
    if($cacheIsCore -ne $PortableCore.IsPresent){throw 'Existing cache product/core profile differs; select a new directory'}

    if($text -notmatch '(?m)^CMAKE_GENERATOR:INTERNAL=Ninja\s*$' -or
       $text -notmatch '(?m)^CONCORD_CONFIGURED_TOOLCHAIN_IDENTITY:INTERNAL='){
        throw 'Existing cache is not an owned Clang/Ninja directory; select a new directory'
    }
    foreach($entry in @(@('CONCORD_CPU_ARCH',$Architecture),@('CMAKE_BUILD_TYPE',$Configuration))){
        if($text -notmatch ('(?m)^'+$entry[0]+':[^=]*=([^\r\n]+)') -or $Matches[1] -cne $entry[1]){
            throw ('Existing cache '+$entry[0]+' differs; select a new directory')
        }
    }
    $expectedSource=if($HelperOnly){Join-Path $projectRoot 'tools'}else{$projectRoot}
    if($text -notmatch '(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=([^\r\n]+)' -or
       [IO.Path]::GetFullPath($Matches[1]) -ine [IO.Path]::GetFullPath($expectedSource)){
        throw 'Existing cache belongs to a different source project; select a new directory'
    }
}
function ConvertTo-CMakeArgumentPath([string]$Path) {
    # Avoid Windows PowerShell 5.1 native quoting of a trailing backslash.
    # Preserve drive roots: C:\ becomes C:/, never drive-relative C:.
    return $Path.Replace('\','/')
}
$source=if($HelperOnly){Join-Path $projectRoot 'tools'}else{$projectRoot}
$arguments=@('-S',(ConvertTo-CMakeArgumentPath $source),'-B',(ConvertTo-CMakeArgumentPath $buildPath),'-G','Ninja',
    ('-DCMAKE_TOOLCHAIN_FILE='+ (ConvertTo-CMakeArgumentPath (Join-Path $projectRoot 'cmake/toolchains/windows-clang-cl.cmake'))),
    ('-DCMAKE_BUILD_TYPE='+$Configuration),'-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
    ('-DCONCORD_CPU_ARCH='+$Architecture),('-DCONCORD_LLVM_ROOT='+ (ConvertTo-CMakeArgumentPath $LLVMRoot)),
    ('-DCONCORD_MSVC_ROOT='+ (ConvertTo-CMakeArgumentPath $MSVCRoot)),('-DCONCORD_WINDOWS_SDK_ROOT='+ (ConvertTo-CMakeArgumentPath $WindowsSDKRoot)),
    ('-DCONCORD_WINDOWS_SDK_VERSION='+$WindowsSDKVersion),('-DCONCORD_NINJA_EXECUTABLE='+ (ConvertTo-CMakeArgumentPath $NinjaExecutable)))
$arguments+=('-DCONCORD_PORTABLE_CORE='+$(if($PortableCore){'ON'}else{'OFF'}))
if($PortableCore){$arguments+='-DCMAKE_CXX_STANDARD_LIBRARIES=kernel32.lib'}
if($MSVCCompatibilityVersion){$arguments+=('-DCONCORD_MSVC_COMPATIBILITY_VERSION='+$MSVCCompatibilityVersion)}
Write-Output ('Executable: '+$CMakeExecutable)
Write-Output 'Arguments (one per line):'
$arguments | Write-Output
& $CMakeExecutable @arguments
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
Write-Output ('Configured '+$Architecture+' '+$Configuration+' in '+$buildPath)
Write-Output ('Build serially: '+$CMakeExecutable+' --build "'+$buildPath+'" --parallel 1 --verbose --target <explicit-target>')

