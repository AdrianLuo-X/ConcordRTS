param(
    [string]$BuildDirectory = 'build\msvc-reference',
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$binRoot = Join-Path $projectRoot 'bin'
$oldUserRootDir = $env:UserRootDir

function Get-NormalPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path.Replace('/', '\')).TrimEnd('\')
}

function Read-CMakeCache([string]$Directory) {
    $cache = @{}
    $path = Join-Path $Directory 'CMakeCache.txt'
    if (Test-Path -LiteralPath $path) {
        foreach ($line in Get-Content -LiteralPath $path) {
            if ($line -match '^([^#/:][^:]*):[^=]+=(.*)$') { $cache[$Matches[1]] = $Matches[2] }
        }
    }
    return $cache
}

function Assert-Cache([hashtable]$Cache, [string]$Directory, [string]$Source, [string]$Platform) {
    if ($Cache.Count -eq 0) { throw "No usable CMake cache in '$Directory'. Configure a new project-local directory first." }
    $reason = $null
    if (-not $Cache.CMAKE_HOME_DIRECTORY -or (Get-NormalPath $Cache.CMAKE_HOME_DIRECTORY) -ne (Get-NormalPath $Source)) {
        $reason = 'the cache belongs to a different source directory'
    } elseif (-not $Cache.CMAKE_CACHEFILE_DIR -or (Get-NormalPath $Cache.CMAKE_CACHEFILE_DIR) -ne (Get-NormalPath $Directory)) {
        $reason = 'the cache was moved from another build directory'
    } elseif ($Cache.CMAKE_GENERATOR -notmatch '^Visual Studio (17 2022|18 2026)$') {
        $reason = 'the generator is not a supported MSVC reference generator (Clang uses separate T-step directories)'
    } elseif ($Cache.CMAKE_GENERATOR_PLATFORM -ne $Platform) {
        $reason = "the cached platform is not $Platform"
    } elseif ($Cache.CMAKE_GENERATOR_TOOLSET -match 'Clang|LLVM') {
        $reason = 'the cached toolset is not MSVC'
    } elseif ([string]::IsNullOrWhiteSpace($Cache.CMAKE_CXX_FLAGS) -or $Cache.CMAKE_CXX_FLAGS -notmatch '(?<!\S)[/-]EHsc(?!\S)') {
        $reason = 'CMAKE_CXX_FLAGS is empty or does not enable /EHsc'
    } else {
        $releaseFlags = "$($Cache.CMAKE_CXX_FLAGS) $($Cache.CMAKE_CXX_FLAGS_RELEASE)"
        $optimization = [regex]::Matches($releaseFlags, '(?<!\S)[/-]O[12dx](?!\S)')
        if ($optimization.Count -eq 0 -or $optimization[$optimization.Count - 1].Value -match 'Od$') {
            $reason = 'Release flags do not select optimization (/O1, /O2 or /Ox), or end in /Od'
        } elseif ($releaseFlags -notmatch '(?<!\S)[/-]D\s*"?NDEBUG(?:=[^"\s]+)?"?(?!\S)') {
            $reason = 'Release flags do not define NDEBUG'
        }
    }
    if ($reason) {
        throw "Refusing cache '$Directory': $reason. Existing flags and files were left intact. Use -BuildDirectory build\msvc-reference or another new directory inside this project; do not delete the old cache."
    }
}

function Get-CompileSettings([string]$Project, [string]$Configuration, [string]$Platform) {
    if (-not (Test-Path -LiteralPath $Project)) { throw "Generated project is missing: $Project" }
    [xml]$xml = Get-Content -LiteralPath $Project -Raw
    $groups = @($xml.Project.ItemDefinitionGroup | Where-Object { $_.Condition -match "=='$Configuration\|$Platform'$" })
    if ($groups.Count -ne 1) { throw "Cannot identify $Configuration|$Platform compile settings in '$Project'." }
    return $groups[0].ClCompile
}

function Assert-GeneratedProjects([string]$Directory, [switch]$IncludeHelper) {
    $release = Get-CompileSettings (Join-Path $Directory 'concord_runtime.vcxproj') 'Release' 'x64'
    $debug = Get-CompileSettings (Join-Path $Directory 'concord_runtime.vcxproj') 'Debug' 'x64'
    $abiText = Get-Content -LiteralPath (Join-Path $projectRoot 'cmake\CompilerAndPlatform.cmake') -Raw
    if ($abiText -notmatch 'set\(CONCORD_RULES_ABI_REVISION\s+(\d+)\)') { throw 'Cannot read the source ABI revision.' }
    $abi = $Matches[1]
    if ($release.ExceptionHandling -ne 'Sync' -or $release.Optimization -notin @('MaxSpeed', 'MinSpace', 'Full')) {
        throw 'Generated Release runtime must enable /EHsc and optimization. Cached custom flags were not rewritten.'
    }
    foreach ($settings in @($release, $debug)) {
        if ($settings.RuntimeLibrary -ne 'MultiThreadedDLL' -or $settings.PreprocessorDefinitions -notmatch "(^|;)CONCORD_RULES_ABI_REVISION=$abi(;|$)") {
            throw 'Generated runtime CRT (/MD) or CONCORD_RULES_ABI_REVISION does not match the source contract.'
        }
    }
    if ($release.PreprocessorDefinitions -notmatch '(^|;)NDEBUG(?:=[^;]*)?(;|$)' -or $debug.PreprocessorDefinitions -notmatch '(^|;)_ITERATOR_DEBUG_LEVEL=0(;|$)') {
        throw 'Generated Release NDEBUG or Debug iterator ABI settings do not match the source contract.'
    }
    if ($debug.ExceptionHandling -ne 'Sync' -or $debug.Optimization -ne 'MaxSpeed' -or $debug.InlineFunctionExpansion -ne 'AnySuitable' -or $debug.BasicRuntimeChecks -ne 'Default') {
        throw 'Generated Debug runtime must retain /EHsc, /O2, /Ob2 and no /RTC checks. This validation does not build Debug.'
    }
    if ($IncludeHelper) {
        $helper = Get-CompileSettings (Join-Path $Directory 'bink_decoder32\BinkFrameExtractor.vcxproj') 'Release' 'Win32'
        if ($helper.RuntimeLibrary -ne 'MultiThreaded' -or $helper.ExceptionHandling -ne 'Sync' -or $helper.Optimization -notin @('MaxSpeed', 'MinSpace', 'Full')) {
            throw 'Generated Win32 helper must use /MT, /EHsc and Release optimization.'
        }
    }
}

function Assert-PEMachine([string]$Path, [int]$Expected) {
    $stream = [System.IO.File]::OpenRead($Path)
    $reader = New-Object System.IO.BinaryReader($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "Not a PE executable: $Path" }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne $Expected) { throw "Unexpected executable platform: $Path" }
    } finally { $reader.Dispose(); $stream.Dispose() }
}

function Get-Sha256([string]$Path) {
    # Windows PowerShell may inherit a PSModulePath from another host/runtime.
    # Keep deployment verification independent of module auto-discovery.
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $stream = [System.IO.File]::OpenRead($Path)
        try { return [System.BitConverter]::ToString($sha256.ComputeHash($stream)).Replace('-', '') }
        finally { $stream.Dispose() }
    } finally { $sha256.Dispose() }
}

try {
    if ([string]::IsNullOrWhiteSpace($BuildDirectory)) { $BuildDirectory = 'build\msvc-reference' }
    if ([System.IO.Path]::IsPathRooted($BuildDirectory)) { $buildRoot = Get-NormalPath $BuildDirectory }
    else { $buildRoot = Get-NormalPath (Join-Path $projectRoot $BuildDirectory) }
    $projectPrefix = $projectRoot.TrimEnd('\') + '\'
    if (-not $buildRoot.StartsWith($projectPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'The build directory must stay inside this project.'
    }
    # A junction must not turn a project-local spelling into writes to another copy.
    $ancestor = $buildRoot
    while ($ancestor -and $ancestor.Length -gt $projectRoot.Length) {
        if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor).Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
            throw "Build directory uses a junction/symlink: $ancestor. Select an ordinary project-local directory."
        }
        $ancestor = Split-Path -Parent $ancestor
    }
    foreach ($variable in @('CL', '_CL_', 'LINK', '_LINK_')) {
        if (-not [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($variable, 'Process'))) {
            throw "Environment variable $variable injects options outside the recorded CMake configuration. Use a clean command process; no environment settings were changed."
        }
    }
    $cache = Read-CMakeCache $buildRoot
    if (Test-Path -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt')) { Assert-Cache $cache $buildRoot $projectRoot 'x64' }
    if ($ValidateOnly -and $cache.Count -eq 0) { throw "-ValidateOnly needs an existing configured directory: $buildRoot" }

    $vswherePath = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswherePath)) { throw 'Existing VS-family C++ tools could not be discovered: vswhere.exe is missing. This entry does not install tools; a full IDE is not required.' }
    $instances = @((& $vswherePath -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json) | Where-Object { $_.isComplete -and [version]$_.installationVersion -ge [version]'17.0' })
    if ($LASTEXITCODE -ne 0) { throw 'vswhere could not inspect the existing C++ tools.' }
    if ($cache.Count -gt 0) {
        if (-not $cache.CMAKE_GENERATOR_INSTANCE) { throw 'The cache has no fixed VS instance. Select a new project-local reference directory.' }
        $instance = $instances | Where-Object { (Get-NormalPath $_.installationPath) -eq (Get-NormalPath $cache.CMAKE_GENERATOR_INSTANCE) } | Select-Object -First 1
        if (-not $instance) { throw "Cached VS instance is not an available complete C++ installation: $($cache.CMAKE_GENERATOR_INSTANCE). Select a new directory instead of changing this cache." }
    } else {
        $instance = $instances | Sort-Object { [version]$_.installationVersion } -Descending | Select-Object -First 1
        if (-not $instance) { throw 'No existing VS-family x64/Win32 C++ tools were found. Tool installation is outside this build step; a full IDE is not required.' }
    }
    $vsHome = $instance.installationPath
    $vsVersion = [version]$instance.installationVersion
    if ($vsVersion.Major -eq 18) { $generator = 'Visual Studio 18 2026' }
    elseif ($vsVersion.Major -eq 17) { $generator = 'Visual Studio 17 2022' }
    else { throw "VS tool version $vsVersion needs a separately verified generator; this reference entry supports VS 17/18." }
    if ($cache.Count -gt 0 -and $cache.CMAKE_GENERATOR -ne $generator) { throw 'Cached generator and fixed VS instance disagree. Select a new project-local directory.' }
    $cmakePath = Join-Path $vsHome 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (-not (Test-Path -LiteralPath $cmakePath)) {
        $cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
        if (-not $cmakeCommand) { throw 'No existing CMake was found in the selected C++ tools or current command environment.' }
        $cmakePath = $cmakeCommand.Source
    }
    $helperRoot = Join-Path $buildRoot 'bink_decoder32'
    $helperCache = Read-CMakeCache $helperRoot
    if (Test-Path -LiteralPath (Join-Path $helperRoot 'CMakeCache.txt')) {
        Assert-Cache $helperCache $helperRoot (Join-Path $projectRoot 'tools') 'Win32'
        if (-not $helperCache.CMAKE_GENERATOR_INSTANCE -or (Get-NormalPath $helperCache.CMAKE_GENERATOR_INSTANCE) -ne (Get-NormalPath $vsHome)) {
            throw 'Win32 helper cache uses a different VS instance. Select a new project-local reference directory.'
        }
    }
    Write-Host "Reference directory: $buildRoot"
    Write-Host "Existing tools: $vsHome ($vsVersion); generator: $generator; core x64 / helper Win32; Release; parallel 1"
    if ($ValidateOnly) {
        Assert-GeneratedProjects $buildRoot -IncludeHelper:($helperCache.Count -gt 0)
        Write-Host 'Cache and generated configuration validation passed. No configure, build or deployment was run.'
        exit 0
    }

    New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
    $portableUserProps = Join-Path $buildRoot 'portable_msbuild_user'
    New-Item -ItemType Directory -Path $portableUserProps -Force | Out-Null
    $env:UserRootDir = $portableUserProps + '\'
    Write-Host "Command: `"$cmakePath`" -S `"$projectRoot`" -B `"$buildRoot`" -G `"$generator`" -A x64 `"-DCMAKE_GENERATOR_INSTANCE=$vsHome`""
    & $cmakePath -S $projectRoot -B $buildRoot -G $generator -A x64 "-DCMAKE_GENERATOR_INSTANCE=$vsHome"
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed (exit $LASTEXITCODE)." }
    $cache = Read-CMakeCache $buildRoot
    Assert-Cache $cache $buildRoot $projectRoot 'x64'
    Assert-GeneratedProjects $buildRoot
    Write-Host "Command: `"$cmakePath`" --build `"$buildRoot`" --config Release --target concord_runtime --parallel 1 --verbose"
    & $cmakePath --build $buildRoot --config Release --target concord_runtime --parallel 1 --verbose
    if ($LASTEXITCODE -ne 0) { throw "Game compilation failed (exit $LASTEXITCODE)." }
    $helperCache = Read-CMakeCache $helperRoot
    Assert-Cache $helperCache $helperRoot (Join-Path $projectRoot 'tools') 'Win32'
    if ((Get-NormalPath $helperCache.CMAKE_GENERATOR_INSTANCE) -ne (Get-NormalPath $vsHome)) { throw 'Built Win32 helper used a different VS instance; deployment refused.' }
    Assert-GeneratedProjects $buildRoot -IncludeHelper
    Assert-PEMachine (Join-Path $buildRoot 'Release\concord_runtime.exe') 0x8664
    Assert-PEMachine (Join-Path $buildRoot 'Release\BinkFrameExtractor.exe') 0x014C
    $helperHash = Get-Sha256 (Join-Path $helperRoot 'Release\BinkFrameExtractor.exe')
    if ($helperHash -ne (Get-Sha256 (Join-Path $buildRoot 'Release\BinkFrameExtractor.exe'))) { throw 'Staged Win32 helper differs from its build product; deployment refused.' }

    $redistRoot = Join-Path $vsHome 'VC\Redist\MSVC'
    $redistVersions = @(Get-ChildItem -LiteralPath $redistRoot -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | Sort-Object { [version]$_.Name } -Descending)
    $crtRoot = $null
    foreach ($versionDir in $redistVersions) {
        $x64Root = Join-Path $versionDir.FullName 'x64'
        if (-not (Test-Path -LiteralPath $x64Root)) { continue }
        $crtDir = Get-ChildItem -LiteralPath $x64Root -Directory | Where-Object { $_.Name -like 'Microsoft.VC*.CRT' } | Select-Object -First 1
        if ($crtDir) { $crtRoot = $crtDir.FullName; break }
    }
    if (-not $crtRoot) { throw 'The selected C++ tools have no x64 CRT redistributable files.' }
    $sources = @{}
    foreach ($dllName in @('msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
        $sources[$dllName] = Join-Path $crtRoot $dllName
    }
    foreach ($exeName in @('BinkFrameExtractor.exe', 'concord_runtime.exe')) { $sources[$exeName] = Join-Path (Join-Path $buildRoot 'Release') $exeName }
    foreach ($source in $sources.Values) { if (-not (Test-Path -LiteralPath $source)) { throw "Required deployment file is missing: $source" } }
    New-Item -ItemType Directory -Path $binRoot -Force | Out-Null
    $deployedFiles = @(foreach ($name in ($sources.Keys | Sort-Object)) {
        $destination = Join-Path $binRoot $name
        Copy-Item -LiteralPath $sources[$name] -Destination $destination -Force
        $hash = Get-Sha256 $sources[$name]
        if ($hash -ne (Get-Sha256 $destination)) { throw "Deployment hash mismatch: $name" }
        [ordered]@{ name = $name; source = $sources[$name]; deployed = $destination; sha256 = $hash }
    })
    $manifest = [ordered]@{
        schema = 1; builtAtUtc = [DateTime]::UtcNow.ToString('o'); sourceRoot = $projectRoot
        buildDirectory = $buildRoot; configuration = 'Release'; generator = $generator
        vsInstance = $vsHome; vsVersion = $vsVersion.ToString(); cmake = $cmakePath
        corePlatform = 'x64'; helperPlatform = 'Win32'; parallel = 1
        cxxFlags = $cache.CMAKE_CXX_FLAGS; releaseFlags = $cache.CMAKE_CXX_FLAGS_RELEASE
        debugFlags = $cache.CMAKE_CXX_FLAGS_DEBUG; files = $deployedFiles
        cacheSha256 = Get-Sha256 (Join-Path $buildRoot 'CMakeCache.txt')
        helperCacheSha256 = Get-Sha256 (Join-Path $helperRoot 'CMakeCache.txt')
    }
    $manifestJson = $manifest | ConvertTo-Json -Depth 6
    $manifestJson | Set-Content -LiteralPath (Join-Path $buildRoot 'build-provenance.json') -Encoding UTF8
    $manifestJson | Set-Content -LiteralPath (Join-Path $binRoot 'build-provenance.json') -Encoding UTF8
    Write-Host "Build and deployment complete. SHA-256 source record: $(Join-Path $binRoot 'build-provenance.json')"
    exit 0
} catch {
    Write-Error $_.Exception.Message -ErrorAction Continue
    exit 1
} finally {
    $env:UserRootDir = $oldUserRootDir
}
