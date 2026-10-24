#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$Configuration,
    [ValidateSet('MSVC','Clang')][string]$Compiler = 'MSVC',
    [Parameter(Mandatory)][string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Compiler = if ($Compiler -ieq 'Clang') { 'Clang' } else { 'MSVC' }
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$utf8 = [Text.UTF8Encoding]::new($false)

function Assert-OrdinaryLocalPath([string]$Path, [string]$Description) {
    $ancestor = [IO.Path]::GetFullPath($Path)
    while ($ancestor -and $ancestor.Length -ge $projectRoot.Length) {
        if ((Test-Path -LiteralPath $ancestor) -and
            ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "$Description uses a junction/symlink: $ancestor. Select an ordinary project-local directory."
        }
        $ancestor = Split-Path -Parent $ancestor
    }
}

function Get-LocalPath([string]$Value, [string]$Description) {
    if (-not [IO.Path]::IsPathRooted($Value)) {
        throw "$Description must be an absolute path: $Value"
    }
    $full = [IO.Path]::GetFullPath($Value)
    $prefix = $projectRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description must be inside this project: $full"
    }
    Assert-OrdinaryLocalPath $full $Description
    $resolved = (Resolve-Path -LiteralPath $full).ProviderPath
    if (-not [IO.Directory]::Exists($resolved)) { throw "$Description is not a directory: $resolved" }
    return [IO.Path]::GetFullPath($resolved)
}

function Write-Json([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 100), $utf8)
}

function Get-FileSha256([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '') }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}

function Invoke-Recorded([string]$Executable, [string[]]$Arguments, [string]$Name) {
    $stdoutFile = Join-Path $outputRoot "$Name.stdout.log"
    $stderrFile = Join-Path $outputRoot "$Name.stderr.log"
    $recordFile = Join-Path $outputRoot "$Name.result.json"
    foreach ($path in @($stdoutFile, $stderrFile, $recordFile)) {
        if ([IO.File]::Exists($path)) { throw "Evidence already exists; select a new output directory: $path" }
    }
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Executable
    $info.WorkingDirectory = $projectRoot
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    foreach ($key in @($info.Environment.Keys)) { if ($key -match '^CONCORD_') { [void]$info.Environment.Remove($key) } }
    $started = [DateTimeOffset]::UtcNow
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    try {
        if (-not $process.Start()) { throw "Could not start $Executable" }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        [IO.File]::WriteAllText($stdoutFile, $stdoutTask.GetAwaiter().GetResult(), $utf8)
        [IO.File]::WriteAllText($stderrFile, $stderrTask.GetAwaiter().GetResult(), $utf8)
        $record = [ordered]@{
            command = [ordered]@{ executable = $Executable; executableSha256 = Get-FileSha256 $Executable; arguments = @($Arguments); workingDirectory = $projectRoot; environment = [ordered]@{ clearedInherited = 'CONCORD_*' } }
            startedAtUtc = $started.ToString('o')
            finishedAtUtc = [DateTimeOffset]::UtcNow.ToString('o')
            exitCode = $process.ExitCode
            stdoutFile = $stdoutFile
            stdoutSha256 = Get-FileSha256 $stdoutFile
            stderrFile = $stderrFile
            stderrSha256 = Get-FileSha256 $stderrFile
        }
        Write-Json $recordFile $record
        return $record
    }
    finally { $process.Dispose() }
}

function Get-AbsolutePath([string]$Path, [string]$Base) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $Base $Path))
}

function Get-CompilerMetadata([string]$Path) {
    if (-not [IO.File]::Exists($Path)) { throw "CMake compiler metadata is missing: $Path" }
    $values = [ordered]@{}
    foreach ($line in [IO.File]::ReadLines($Path)) {
        if ($line -match '^set\(((?:CMAKE_CXX_[A-Z0-9_]+|CMAKE_(?:AR|LINKER|LINKER_LINK|RANLIB)|MSVC_CXX_ARCHITECTURE_ID))\s+(.*)\)$') {
            $values[$Matches[1]] = $Matches[2].Trim().Trim('"')
        }
    }
    foreach ($key in @('CMAKE_CXX_COMPILER','CMAKE_CXX_COMPILER_ID','CMAKE_CXX_COMPILER_VERSION','CMAKE_CXX_COMPILER_FRONTEND_VARIANT')) {
        if (-not $values.Contains($key) -or [string]::IsNullOrWhiteSpace($values[$key])) { throw "Compiler metadata has no $key : $Path" }
    }
    if ($values.CMAKE_CXX_COMPILER_ID -cne $Compiler -or $values.CMAKE_CXX_COMPILER_FRONTEND_VARIANT -cne 'MSVC') {
        throw "Selected compiler '$Compiler' differs from CMake compiler identity/frontend '$($values.CMAKE_CXX_COMPILER_ID)/$($values.CMAKE_CXX_COMPILER_FRONTEND_VARIANT)'."
    }
    if (-not [IO.Path]::IsPathRooted($values.CMAKE_CXX_COMPILER) -or -not [IO.File]::Exists($values.CMAKE_CXX_COMPILER)) {
        throw "Compiler executable is not an available absolute path: $($values.CMAKE_CXX_COMPILER)"
    }
    return [ordered]@{ file = $Path; sha256 = Get-FileSha256 $Path; values = $values }
}

function Get-CompilerIdentity($Metadata) {
    return [ordered]@{
        selected = $Compiler
        id = $Metadata.values.CMAKE_CXX_COMPILER_ID
        frontendVariant = $Metadata.values.CMAKE_CXX_COMPILER_FRONTEND_VARIANT
        version = $Metadata.values.CMAKE_CXX_COMPILER_VERSION
        executable = Get-AbsolutePath $Metadata.values.CMAKE_CXX_COMPILER $projectRoot
        executableSha256 = Get-FileSha256 $Metadata.values.CMAKE_CXX_COMPILER
        metadataFile = $Metadata.file
        metadataSha256 = $Metadata.sha256
    }
}

$buildRoot = Get-LocalPath $BuildDirectory 'BuildDirectory'
$outputRoot = Get-LocalPath $OutputDirectory 'OutputDirectory'
$cacheFile = Join-Path $buildRoot 'CMakeCache.txt'
if (-not [IO.File]::Exists($cacheFile)) { throw "Configured CMake cache is missing: $cacheFile" }
$cacheSha256Before = Get-FileSha256 $cacheFile
$cache = @{}
foreach ($line in [IO.File]::ReadLines($cacheFile)) {
    if ($line -match '^([^#/:][^:]*):[^=]*=(.*)$') { $cache[$Matches[1]] = $Matches[2] }
}
foreach ($key in @('CMAKE_COMMAND', 'CMAKE_CTEST_COMMAND', 'CMAKE_HOME_DIRECTORY')) {
    if (-not $cache.ContainsKey($key)) { throw "Required cache entry is missing: $key" }
}
if ((Get-AbsolutePath $cache.CMAKE_HOME_DIRECTORY $projectRoot) -ne $projectRoot) {
    throw "BuildDirectory belongs to another source tree: $($cache.CMAKE_HOME_DIRECTORY)"
}
if ($cache.ContainsKey('CMAKE_CONFIGURATION_TYPES') -and -not [string]::IsNullOrWhiteSpace($cache.CMAKE_CONFIGURATION_TYPES)) {
    if ($Configuration -cnotin ($cache.CMAKE_CONFIGURATION_TYPES -split ';')) {
        throw "Configuration '$Configuration' is not registered in CMAKE_CONFIGURATION_TYPES."
    }
}
elseif (-not $cache.ContainsKey('CMAKE_BUILD_TYPE') -or $cache.CMAKE_BUILD_TYPE -cne $Configuration) {
    throw "Configuration '$Configuration' differs from the single-configuration cache."
}
foreach ($key in @('CMAKE_COMMAND', 'CMAKE_CTEST_COMMAND')) {
    if (-not [IO.Path]::IsPathRooted($cache[$key]) -or -not [IO.File]::Exists($cache[$key])) { throw "Configured tool is not an available absolute path: $key=$($cache[$key])" }
}
foreach ($variable in @('CL','_CL_','LINK','_LINK_')) {
    if (-not [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($variable,'Process'))) { throw "Environment variable $variable injects unrecorded compiler/linker options. Use a clean command process." }
}
# CMake's metadata (not the compiler filename) proves the requested identity before reconfigure.
$cacheVersion = "$($cache.CMAKE_CACHE_MAJOR_VERSION).$($cache.CMAKE_CACHE_MINOR_VERSION).$($cache.CMAKE_CACHE_PATCH_VERSION)"
$metadataCandidates = @(Get-ChildItem -LiteralPath (Join-Path $buildRoot 'CMakeFiles') -Directory |
    Where-Object { $_.Name -match ('^' + [regex]::Escape($cacheVersion) + '(?:$|[-+])') } |
    ForEach-Object { Join-Path $_.FullName 'CMakeCXXCompiler.cmake' } | Where-Object { [IO.File]::Exists($_) })
if ($metadataCandidates.Count -ne 1) { throw "Expected exactly one compiler metadata file for cache version '$cacheVersion'." }
$compilerBefore = Get-CompilerIdentity (Get-CompilerMetadata $metadataCandidates[0])

$inventoryFile = Join-Path $outputRoot 'inventory.json'
$traceFile = Join-Path $outputRoot 'configure-trace.jsonl'
foreach ($path in @($inventoryFile, $traceFile, (Join-Path $outputRoot 'ctest.json'))) {
    if ([IO.File]::Exists($path)) { throw "Evidence already exists; select a new output directory: $path" }
}
# This query records target identity and artifact paths even when no executable has been built.
$queryRoot = Join-Path $buildRoot '.cmake/api/v1/query/client-s03-inventory'
Assert-OrdinaryLocalPath $queryRoot 'File API query directory'
[IO.Directory]::CreateDirectory($queryRoot) | Out-Null
$queryFile = Join-Path $queryRoot 'codemodel-v2'
Assert-OrdinaryLocalPath $queryFile 'File API query file'
if (-not [IO.File]::Exists($queryFile)) { [IO.File]::WriteAllText($queryFile, '', $utf8) }
$configureArguments = [Collections.Generic.List[string]]::new()
foreach ($argument in @('-S', $projectRoot, '-B', $buildRoot, '--trace-expand', '--trace-format=json-v1', "--trace-redirect=$traceFile")) {
    $configureArguments.Add($argument)
}
$configureArguments.Add("--trace-source=$(Join-Path $projectRoot 'CMakeLists.txt')")
foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $projectRoot 'cmake') -Filter '*.cmake' -File -Recurse | Sort-Object FullName)) {
    $configureArguments.Add("--trace-source=$($file.FullName)")
}
$configure = Invoke-Recorded $cache.CMAKE_COMMAND $configureArguments.ToArray() 'configure'
$configure.traceFile = $traceFile
if ($configure.exitCode -ne 0) { throw "CMake configure failed with exit $($configure.exitCode); see $($configure.stderrFile)" }

$replyRoot = Join-Path $buildRoot '.cmake/api/v1/reply'
$indexFile = Get-ChildItem -LiteralPath $replyRoot -Filter 'index-*.json' -File |
    Sort-Object LastWriteTimeUtc, Name -Descending | Select-Object -First 1
if ($null -eq $indexFile) { throw "CMake did not produce a File API reply: $replyRoot" }
$index = Get-Content -LiteralPath $indexFile.FullName -Raw | ConvertFrom-Json -AsHashtable
$codemodelReference = @($index.objects | Where-Object { $_.kind -eq 'codemodel' -and $_.version.major -eq 2 })
if ($codemodelReference.Count -ne 1) { throw 'Expected exactly one codemodel-v2 object in the File API reply.' }
$codemodelFile = Join-Path $replyRoot $codemodelReference[0].jsonFile
$codemodel = Get-Content -LiteralPath $codemodelFile -Raw | ConvertFrom-Json -AsHashtable
$modelConfigurations = @($codemodel.configurations | Where-Object { $_.name -ceq $Configuration })
if ($modelConfigurations.Count -ne 1) { throw "File API configuration '$Configuration' was not found exactly once." }
$targets = @{}
$artifactsToTargets = @{}
foreach ($reference in $modelConfigurations[0].targets) {
    $targetFile = Join-Path $replyRoot $reference.jsonFile
    $target = Get-Content -LiteralPath $targetFile -Raw | ConvertFrom-Json -AsHashtable
    $artifacts = @()
    if ($target.ContainsKey('artifacts')) {
        $artifacts = @($target.artifacts | ForEach-Object { Get-AbsolutePath $_.path $codemodel.paths.build })
    }
    $sources = @()
    if ($target.ContainsKey('sources')) {
        $sources = @($target.sources | ForEach-Object {
            $base = if ($_.ContainsKey('isGenerated') -and $_.isGenerated) { $codemodel.paths.build } else { $codemodel.paths.source }
            Get-AbsolutePath $_.path $base
        })
    }
    $executableArtifact = $null
    if ($target.type -eq 'EXECUTABLE' -and $target.ContainsKey('nameOnDisk')) {
        $primaryArtifacts = @($artifacts | Where-Object { [IO.Path]::GetFileName($_) -ceq $target.nameOnDisk })
        if ($primaryArtifacts.Count -eq 1) { $executableArtifact = $primaryArtifacts[0] }
    }
    $targets[$target.name] = [ordered]@{
        id = $target.id
        type = $target.type
        nameOnDisk = if ($target.ContainsKey('nameOnDisk')) { $target.nameOnDisk } else { $null }
        executableArtifact = $executableArtifact
        sources = $sources
        sourceRecords = @(if ($target.ContainsKey('sources')) { $target.sources })
        artifacts = $artifacts
        artifactRecords = @(if ($target.ContainsKey('artifacts')) { $target.artifacts })
        compileGroups = @(if ($target.ContainsKey('compileGroups')) { $target.compileGroups })
        link = if ($target.ContainsKey('link')) { $target.link } else { $null }
        dependencies = @(if ($target.ContainsKey('dependencies')) { $target.dependencies })
        backtraceGraph = if ($target.ContainsKey('backtraceGraph')) { $target.backtraceGraph } else { $null }
        fileApiFile = $targetFile
        fileApiSha256 = Get-FileSha256 $targetFile
    }
    foreach ($artifact in $artifacts) {
        if (-not $artifactsToTargets.ContainsKey($artifact)) { $artifactsToTargets[$artifact] = [Collections.Generic.List[string]]::new() }
        $artifactsToTargets[$artifact].Add($target.name)
    }
}

$registrations = @{}
foreach ($line in [IO.File]::ReadLines($traceFile)) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    $entry = $line | ConvertFrom-Json -AsHashtable
    if (-not $entry.ContainsKey('cmd') -or $entry.cmd -ne 'add_test') { continue }
    $arguments = @($entry.args)
    if ($arguments.Count -eq 0) { continue }
    $testName = $null
    $commandTokens = @()
    if ($arguments[0] -ceq 'NAME') {
        if ($arguments.Count -lt 4 -or $arguments[2] -cne 'COMMAND') { throw "Unsupported add_test declaration at $($entry.file):$($entry.line)" }
        $testName = $arguments[1]
        $end = $arguments.Count
        for ($i = 4; $i -lt $arguments.Count; $i++) {
            if ($arguments[$i] -cin @('CONFIGURATIONS', 'WORKING_DIRECTORY', 'COMMAND_EXPAND_LISTS')) { $end = $i; break }
        }
        $commandTokens = @($arguments[3..($end - 1)])
    }
    elseif ($arguments.Count -ge 2) {
        $testName = $arguments[0]
        $commandTokens = @($arguments[1..($arguments.Count - 1)])
    }
    if ($null -eq $testName) { continue }
    if ($registrations.ContainsKey($testName)) { throw "Duplicate traced test registration: $testName" }
    $registrations[$testName] = [ordered]@{ file = $entry.file; line = $entry.line; commandTokens = $commandTokens; expandedArguments = $arguments }
}

$ctestArguments = @('--test-dir', $buildRoot, '-C', $Configuration, '--show-only=json-v1')
$ctest = Invoke-Recorded $cache.CMAKE_CTEST_COMMAND $ctestArguments 'ctest'
if ($ctest.exitCode -ne 0) { throw "CTest inventory failed with exit $($ctest.exitCode); see $($ctest.stderrFile)" }
$ctestFile = Join-Path $outputRoot 'ctest.json'
[IO.File]::WriteAllText($ctestFile, [IO.File]::ReadAllText($ctest.stdoutFile), $utf8)
$ctest.jsonFile = $ctestFile
$ctestData = Get-Content -LiteralPath $ctestFile -Raw | ConvertFrom-Json -AsHashtable

$reviews = @{}
$reviewFile = Join-Path $PSScriptRoot 'test-profiles.json'
if ([IO.File]::Exists($reviewFile)) {
    $profileData = Get-Content -LiteralPath $reviewFile -Raw | ConvertFrom-Json -AsHashtable
    if ($profileData.ContainsKey('tests')) {
        if ($profileData.tests -is [Collections.IDictionary]) {
            foreach ($name in $profileData.tests.Keys) { $reviews[$name] = $profileData.tests[$name] }
        }
        else {
            foreach ($review in $profileData.tests) {
                if ($reviews.ContainsKey($review.name)) { throw "Duplicate reviewed test: $($review.name)" }
                $reviews[$review.name] = $review
            }
        }
    }
}

$rows = [Collections.Generic.List[object]]::new()
$unresolved = [Collections.Generic.List[string]]::new()
$commandMismatches = [Collections.Generic.List[string]]::new()
foreach ($test in $ctestData.tests) {
    $registration = if ($registrations.ContainsKey($test.name)) { $registrations[$test.name] } else { $null }
    $command = $null
    if ($test.ContainsKey('command')) { $command = @($test.command) }
    $targetName = $null
    $targetEvidence = $null
    if ($null -ne $registration -and $registration.commandTokens.Count -gt 0) {
        $firstToken = $registration.commandTokens[0]
        if ($targets.ContainsKey($firstToken)) {
            $targetName = $firstToken
            $targetEvidence = 'expanded-add_test-target'
        }
        elseif ($firstToken -match '^\$<TARGET_FILE:([^>]+)>$' -and $targets.ContainsKey($Matches[1])) {
            $targetName = $Matches[1]
            $targetEvidence = 'expanded-add_test-TARGET_FILE'
        }
    }
    if ($null -eq $targetName -and $null -ne $command -and $command.Count -gt 0 -and [IO.Path]::IsPathRooted($command[0])) {
        $artifact = [IO.Path]::GetFullPath($command[0])
        if ($artifactsToTargets.ContainsKey($artifact) -and $artifactsToTargets[$artifact].Count -eq 1) {
            $targetName = $artifactsToTargets[$artifact][0]
            $targetEvidence = 'ctest-command-exact-file-api-artifact'
        }
    }
    # W013: the reviewed native runners invoke one exact game artifact through
    # CMake. Prove both the declared TARGET_FILE and the actual -D argument;
    # an arbitrary wrapper or mismatched executable must still fail inventory.
    $wrappedExecutableArtifact = $null
    if ($null -eq $targetName -and $null -ne $registration -and $null -ne $command -and $command.Count -gt 3 -and
        [IO.Path]::IsPathRooted($command[0]) -and
        (Get-AbsolutePath $command[0] $buildRoot) -ieq (Get-AbsolutePath $cache.CMAKE_COMMAND $buildRoot)) {
        $runnerPositions = @(for ($i=1; $i -lt $command.Count; $i++) { if ($command[$i] -ceq '-P') { $i } })
        $knownNativeRunners = @((Join-Path $projectRoot 'cmake/RunCursorNative.cmake'),
            (Join-Path $projectRoot 'cmake/RunInputCancellationNative.cmake'))
        if ($runnerPositions.Count -eq 1 -and $runnerPositions[0] -eq ($command.Count - 2) -and
            [IO.Path]::IsPathRooted($command[-1]) -and
            (Get-AbsolutePath $command[-1] $buildRoot) -in $knownNativeRunners) {
            $declaredGames = @($registration.commandTokens | Where-Object { $_ -cmatch '^-DGAME_EXECUTABLE=\$<TARGET_FILE:([^>]+)>$' })
            $actualGames = @($command | Where-Object { $_ -cmatch '^-DGAME_EXECUTABLE=(.+)$' })
            if ($declaredGames.Count -eq 1 -and $actualGames.Count -eq 1) {
                $declaredTarget = [regex]::Match($declaredGames[0], '^\-DGAME_EXECUTABLE=\$<TARGET_FILE:([^>]+)>$').Groups[1].Value
                $actualGame = $actualGames[0].Substring('-DGAME_EXECUTABLE='.Length)
                if ($targets.ContainsKey($declaredTarget) -and $targets[$declaredTarget].type -ceq 'EXECUTABLE' -and
                    [IO.Path]::IsPathRooted($actualGame)) {
                    $targetName = $declaredTarget
                    $wrappedExecutableArtifact = Get-AbsolutePath $actualGame $buildRoot
                    $targetEvidence = 'reviewed-native-cmake-runner-and-exact-TARGET_FILE'
                }
            }
        }
    }
    if ($null -eq $targetName) { $unresolved.Add($test.name) }
    $properties = [ordered]@{}
    foreach ($property in $test.properties) { $properties[$property.name] = $property.value }
    $review = if ($reviews.ContainsKey($test.name)) { $reviews[$test.name] } else { $null }
    $inputs = $null
    $outputs = $null
    $conditions = [ordered]@{ reviewStatus = 'unreviewed'; environmentRequirements = 'unknown' }
    $profiles = @()
    if ($null -ne $review) {
        if ($review.ContainsKey('inputs')) { $inputs = @($review.inputs) }
        if ($review.ContainsKey('outputs')) { $outputs = @($review.outputs) }
        if ($review.ContainsKey('conditions')) { $conditions = $review.conditions }
        if ($review.ContainsKey('profiles')) { $profiles = @($review.profiles) }
    }
    $metadata = if ($null -ne $targetName) { $targets[$targetName] } else { $null }
    $commandArtifactMatches = $null
    if ($null -ne $command -and $command.Count -gt 0 -and $null -ne $metadata -and $null -ne $metadata.executableArtifact) {
        $executedArtifact = if ($null -ne $wrappedExecutableArtifact) { $wrappedExecutableArtifact } else { $command[0] }
        $commandArtifactMatches = [IO.Path]::IsPathRooted($executedArtifact) -and
            (Get-AbsolutePath $executedArtifact $buildRoot) -ieq (Get-AbsolutePath $metadata.executableArtifact $buildRoot)
        if (-not $commandArtifactMatches) { $commandMismatches.Add($test.name) }
    }
    $artifactExists = $null
    $allArtifactsExist = $null
    $artifactPresence = @()
    if ($null -ne $metadata -and $metadata.artifacts.Count -gt 0) {
        $artifactPresence = @($metadata.artifacts | ForEach-Object { [ordered]@{ path = $_; exists = [IO.File]::Exists($_) } })
        $allArtifactsExist = @($artifactPresence | Where-Object { -not $_.exists }).Count -eq 0
        $artifactExists = if ($null -ne $metadata.executableArtifact) { [IO.File]::Exists($metadata.executableArtifact) } else { $allArtifactsExist }
    }
    $rows.Add([ordered]@{
        name = $test.name
        target = $targetName
        targetEvidence = $targetEvidence
        registration = $registration
        targetMetadata = $metadata
        configuration = $Configuration
        command = $command
        wrappedExecutableArtifact = $wrappedExecutableArtifact
        commandArtifactMatches = $commandArtifactMatches
        artifactExists = $artifactExists
        allArtifactsExist = $allArtifactsExist
        artifactPresence = $artifactPresence
        state = if ($artifactExists -eq $false) { 'not-built' } else { 'not-run' }
        properties = $properties
        labels = @(if ($properties.Contains('LABELS')) { $properties.LABELS })
        timeout = if ($properties.Contains('TIMEOUT')) { $properties.TIMEOUT } else { $null }
        reviewStatus = if ($null -ne $review) { 'reviewed' } else { 'unreviewed' }
        inputs = $inputs
        outputs = $outputs
        conditions = $conditions
        profiles = $profiles
        review = $review
    })
}
$reviewedCount = @($rows | Where-Object { $_.reviewStatus -eq 'reviewed' }).Count
$finalCache = @{}
foreach ($line in [IO.File]::ReadLines($cacheFile)) {
    if ($line -match '^([^#/:][^:]*):[^=]*=(.*)$') { $finalCache[$Matches[1]] = $Matches[2] }
}
$cacheEntries = [ordered]@{}
$cxxFlags = [ordered]@{}
foreach ($key in ($finalCache.Keys | Sort-Object)) {
    if ($key -match '^CMAKE_(COMMAND|CTEST_COMMAND|GENERATOR.*|VS.*|MAKE_PROGRAM|CXX_COMPILER.*|CXX_FLAGS.*|EXE_LINKER_FLAGS.*|SHARED_LINKER_FLAGS.*|MSVC_RUNTIME_LIBRARY|CONFIGURATION_TYPES|BUILD_TYPE|SYSTEM_VERSION)$') {
        $cacheEntries[$key] = $finalCache[$key]
    }
    if ($key -match '^CMAKE_CXX_FLAGS(?:_[A-Z0-9_]+)?$') { $cxxFlags[$key] = $finalCache[$key] }
}
$compilerMetadataFile = Join-Path $buildRoot "CMakeFiles/$($index.cmake.version.string)/CMakeCXXCompiler.cmake"
$compilerMetadata = Get-CompilerMetadata $compilerMetadataFile
$compilerIdentity = Get-CompilerIdentity $compilerMetadata
if ($compilerIdentity.executable -ine $compilerBefore.executable -or $compilerIdentity.executableSha256 -cne $compilerBefore.executableSha256 -or
    $compilerIdentity.id -cne $compilerBefore.id -or $compilerIdentity.frontendVariant -cne $compilerBefore.frontendVariant) {
    throw 'Compiler identity changed during inventory configure; no test was run.'
}
$inventory = [ordered]@{
    schemaVersion = 2
    generatedAtUtc = [DateTimeOffset]::UtcNow.ToString('o')
    projectRoot = $projectRoot
    buildDirectory = $buildRoot
    configuration = $Configuration
    compiler = $Compiler
    cacheFile = $cacheFile
    cacheSha256 = Get-FileSha256 $cacheFile
    cacheSha256BeforeConfigure = $cacheSha256Before
    toolchain = [ordered]@{
        CMAKE_COMMAND = $finalCache.CMAKE_COMMAND
        CMAKE_CTEST_COMMAND = $finalCache.CMAKE_CTEST_COMMAND
        generator = $finalCache.CMAKE_GENERATOR
        platform = if ($finalCache.ContainsKey('CMAKE_GENERATOR_PLATFORM')) { $finalCache.CMAKE_GENERATOR_PLATFORM } else { $null }
        instance = if ($finalCache.ContainsKey('CMAKE_GENERATOR_INSTANCE')) { $finalCache.CMAKE_GENERATOR_INSTANCE } else { $null }
        toolset = if ($finalCache.ContainsKey('CMAKE_GENERATOR_TOOLSET')) { $finalCache.CMAKE_GENERATOR_TOOLSET } else { $null }
        cxxFlags = $cxxFlags
        cacheEntries = $cacheEntries
        compilerMetadata = $compilerMetadata
        compilerIdentity = $compilerIdentity
        fileApiCmake = $index.cmake
    }
    configure = $configure
    ctest = $ctest
    fileApi = [ordered]@{ indexFile = $indexFile.FullName; codemodelFile = $codemodelFile; configuration = $Configuration }
    reviewFile = if ([IO.File]::Exists($reviewFile)) { $reviewFile } else { $null }
    summary = [ordered]@{
        registeredTests = $rows.Count
        mappedTests = $rows.Count - $unresolved.Count
        unresolvedTests = @($unresolved.ToArray())
        commandArtifactMismatches = @($commandMismatches.ToArray())
        reviewedTests = $reviewedCount
        unreviewedTests = $rows.Count - $reviewedCount
    }
    tests = @($rows.ToArray())
}
Write-Json $inventoryFile $inventory
Write-Host "Inventory: $($rows.Count) registered, $($rows.Count - $unresolved.Count) mapped, $reviewedCount reviewed. $inventoryFile"
if ($rows.Count -eq 0) { throw 'CTest registered zero tests; an empty inventory is not a successful verification.' }
if ($unresolved.Count -gt 0) { throw "Test targets could not be proven: $($unresolved -join ', '). Complete inventory was written for review." }
if ($commandMismatches.Count -gt 0) { throw "CTest commands differ from the exact File API executable artifacts for '$Configuration': $($commandMismatches -join ', '). Complete inventory was written for review; no test was run." }
