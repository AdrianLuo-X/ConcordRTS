param([string]$BuildDirectory = "")

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot 'build'
if (-not [string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $buildRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
    $projectPrefix = [System.IO.Path]::GetFullPath($projectRoot).TrimEnd('\') + '\'
    if (-not $buildRoot.StartsWith($projectPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'The build directory must stay inside this project.'
    }
}
$binRoot = Join-Path $projectRoot 'bin'

try {
    $vswherePath = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswherePath)) {
        throw 'Install Visual Studio 2022 or newer with Desktop development with C++ and a Windows SDK.'
    }
    $vsHome = (& $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
    $vsVersion = (& $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion | Select-Object -First 1)
    if (-not $vsHome) { throw 'No Visual Studio C++ toolchain was found.' }
    if ([version]$vsVersion -ge [version]'18.0') {
        $generator = 'Visual Studio 18 2026'
    } elseif ([version]$vsVersion -ge [version]'17.0') {
        $generator = 'Visual Studio 17 2022'
    } else {
        throw 'Visual Studio 2022 or newer is required.'
    }
    $cmakePath = Join-Path $vsHome 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (-not (Test-Path -LiteralPath $cmakePath)) {
        $cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
        if (-not $cmakeCommand) { throw 'CMake was not found. Enable the C++ CMake tools component in Visual Studio.' }
        $cmakePath = $cmakeCommand.Source
    }
    New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
    $portableUserProps = Join-Path $buildRoot 'portable_msbuild_user'
    New-Item -ItemType Directory -Path $portableUserProps -Force | Out-Null
    $env:UserRootDir = $portableUserProps + '\'
    & $cmakePath -S $projectRoot -B $buildRoot -G $generator -A x64 "-DCMAKE_GENERATOR_INSTANCE=$vsHome"
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & $cmakePath --build $buildRoot --config Release --target concord_runtime --parallel 1
    if ($LASTEXITCODE -ne 0) { throw 'Game compilation failed.' }

    $redistRoot = Join-Path $vsHome 'VC\Redist\MSVC'
    $redistVersions = @(Get-ChildItem -LiteralPath $redistRoot -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | Sort-Object { [version]$_.Name } -Descending)
    $crtRoot = $null
    foreach ($versionDir in $redistVersions) {
        $x64Root = Join-Path $versionDir.FullName 'x64'
        if (-not (Test-Path -LiteralPath $x64Root)) { continue }
        $crtDir = Get-ChildItem -LiteralPath $x64Root -Directory | Where-Object { $_.Name -like 'Microsoft.VC*.CRT' } | Select-Object -First 1
        if ($crtDir) { $crtRoot = $crtDir.FullName; break }
    }
    if (-not $crtRoot) { throw 'The Visual Studio x64 CRT redistributable files were not found.' }

    New-Item -ItemType Directory -Path $binRoot -Force | Out-Null
    $dllNames = @('msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')
    foreach ($dllName in $dllNames) {
        $dllSource = Join-Path $crtRoot $dllName
        if (-not (Test-Path -LiteralPath $dllSource)) { throw "Required runtime file was not found: $dllName" }
    }
    foreach ($dllName in $dllNames) {
        Copy-Item -LiteralPath (Join-Path $crtRoot $dllName) -Destination (Join-Path $binRoot $dllName) -Force
    }
    Copy-Item -LiteralPath (Join-Path $buildRoot 'Release\BinkFrameExtractor.exe') -Destination (Join-Path $binRoot 'BinkFrameExtractor.exe') -Force
    Copy-Item -LiteralPath (Join-Path $buildRoot 'Release\concord_runtime.exe') -Destination (Join-Path $binRoot 'concord_runtime.exe') -Force
    Write-Host 'Build complete. Start the game with the launch script in the project folder.'
    exit 0
} catch {
    Write-Error $_.Exception.Message -ErrorAction Continue
    exit 1
}
