#requires -Version 5.1
<#
.SYNOPSIS
Check source repository candidates without changing files or Git state.
.DESCRIPTION
Checks indexed blobs and, by default, untracked files not excluded by Git.
Rejects local assets, build outputs, known local backup directories and
indexed blobs or working files over 100 MiB. Warns above 50 MiB.
Deleted working files still receive the index size check. Does not inspect
secrets or Git history; ordinary fixture archives are not rejected by extension.
.EXAMPLE
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Check-SourceRepository.ps1
.EXAMPLE
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Check-SourceRepository.ps1 -TrackedOnly
#>
[CmdletBinding()]
param(
    [string]$ProjectRoot,
    [switch]$TrackedOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$root = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd([char[]]@('\', '/'))

function Read-Git([string[]]$Arguments, [string]$InputText = '') {
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = (Get-Command git -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.RedirectStandardInput = $true
    $utf8 = New-Object Text.UTF8Encoding($false)
    $start.StandardOutputEncoding = $utf8
    $start.StandardErrorEncoding = $utf8
    $start.EnvironmentVariables['GIT_OPTIONAL_LOCKS'] = '0'
    # All arguments below are script-owned constants, not shell commands.
    $start.Arguments = (($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' ')
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $start
    try {
        [void]$process.Start()
        $outputTask = $process.StandardOutput.ReadToEndAsync()
        $errorTask = $process.StandardError.ReadToEndAsync()
        if ($InputText.Length -gt 0) {
            # Raw UTF-8 input also works on the Framework used by PowerShell 5.1,
            # where ProcessStartInfo.StandardInputEncoding is unavailable.
            $inputBytes = $utf8.GetBytes($InputText)
            $process.StandardInput.BaseStream.Write($inputBytes, 0, $inputBytes.Length)
            $process.StandardInput.BaseStream.Flush()
        }
        $process.StandardInput.Close()
        $process.WaitForExit()
        $outputText = $outputTask.GetAwaiter().GetResult()
        $errorText = $errorTask.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) { throw "Git check failed: $errorText" }
        return $outputText
    } finally { $process.Dispose() }
}

function Read-WorkingPath([string]$Path) {
    $relative = $Path.Replace('/', [IO.Path]::DirectorySeparatorChar)
    $fullPath = [IO.Path]::GetFullPath((Join-Path $root $relative))
    $rootPrefix = $root + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        return [pscustomobject]@{ State = 'Outside'; Path = $fullPath }
    }
    # Inspect each ancestor before entering it, so a linked parent does not
    # cause later file metadata reads outside the project directory.
    $current = $root
    foreach ($part in $fullPath.Substring($rootPrefix.Length).Split([IO.Path]::DirectorySeparatorChar)) {
        $current = Join-Path $current $part
        try { $attributes = [IO.File]::GetAttributes($current) }
        catch [IO.FileNotFoundException] { return [pscustomobject]@{ State = 'Missing'; Path = $fullPath } }
        catch [IO.DirectoryNotFoundException] { return [pscustomobject]@{ State = 'Missing'; Path = $fullPath } }
        if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            return [pscustomobject]@{ State = 'Linked'; Path = $current }
        }
    }
    if (($attributes -band [IO.FileAttributes]::Directory) -ne 0) {
        return [pscustomobject]@{ State = 'Directory'; Path = $fullPath }
    }
    return [pscustomobject]@{ State = 'File'; Path = $fullPath }
}

try {
    $gitRoot = (Read-Git @('rev-parse', '--show-toplevel')).Trim()
    if (-not [string]::Equals([IO.Path]::GetFullPath($gitRoot).TrimEnd('\'), $root,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'ProjectRoot must be the Git repository root, not a subdirectory.'
    }
    $indexEntries = New-Object 'System.Collections.Generic.List[object]'
    foreach ($record in (Read-Git @('ls-files', '--stage', '-z')).Split([char]0)) {
        if ($record.Length -eq 0) { continue }
        $tab = $record.IndexOf([char]9)
        if ($tab -lt 0 -or $record.Substring(0, $tab) -notmatch '^([0-7]+) ([0-9a-f]+) ([0-3])$') {
            throw 'Cannot parse a Git index entry; size check did not complete.'
        }
        $indexEntries.Add([pscustomobject]@{
            Mode = $Matches[1]; Object = $Matches[2]; Stage = [int]$Matches[3]
            Path = $record.Substring($tab + 1)
        })
    }
    # A gitlink is a submodule commit, not a file blob in this repository.
    $blobEntries = @($indexEntries | Where-Object { $_.Mode -ne '160000' })
    $blobSizes = @{}
    if ($blobEntries.Count -gt 0) {
        $objectIds = @($blobEntries | ForEach-Object { $_.Object } | Sort-Object -Unique)
        $metadata = Read-Git @('cat-file', '--batch-check=%(objectname) %(objecttype) %(objectsize)') (($objectIds -join "`n") + "`n")
        foreach ($record in $metadata -split "`n") {
            $record = $record.TrimEnd("`r")
            if ($record.Length -eq 0) { continue }
            if ($record -notmatch '^([0-9a-f]+) blob (\d+)$') {
                throw "Cannot read an indexed blob size: $record"
            }
            $blobSizes[$Matches[1]] = [long]$Matches[2]
        }
        if ($blobSizes.Count -ne $objectIds.Count) { throw 'Indexed blob metadata is incomplete.' }
    }
    $candidatePaths = @($indexEntries | ForEach-Object { $_.Path })
    if (-not $TrackedOnly) {
        $candidatePaths += @((Read-Git @('ls-files', '--others', '--exclude-standard', '-z')).Split([char]0) | Where-Object { $_.Length -gt 0 })
    }
    $paths = @($candidatePaths | Sort-Object -Unique)
    $failures = New-Object 'System.Collections.Generic.List[string]'
    $large = New-Object 'System.Collections.Generic.List[string]'
    $checked = 0
    $missing = 0
    foreach ($entry in $blobEntries) {
        $size = $blobSizes[$entry.Object]
        $label = $entry.Path.Replace('\', '/')
        if ($entry.Stage -ne 0) { $label += " (index stage $($entry.Stage))" }
        if ($size -gt 100MB) {
            $failures.Add(('Indexed blob exceeds 100 MiB: {0} ({1:N2} MiB)' -f $label, ($size / 1MB)))
        } elseif ($size -gt 50MB) {
            $large.Add(('Indexed blob exceeds 50 MiB: {0} ({1:N2} MiB)' -f $label, ($size / 1MB)))
        }
    }
    foreach ($path in $paths) {
        $normalized = $path.Replace('\', '/')
        $leaf = [IO.Path]::GetFileName($normalized)
        $localDirectory = $normalized -match '^(RedAlert2|local_data|build|bin|out|obj|development_records|\.external|%SystemDrive%)/'
        $localDirectory = $localDirectory -or $normalized -like 'Git*_20??????/*' -or
            $normalized -match '^\.codex_(tmp_|recovery_|task)'
        $localFile = $normalized -match '^runtime_.*_smoke\.ini$' -or
            $normalized -match '\.(exe|dll|lib|obj|pdb|ilk|dmp|community\.state)$' -or
            ($leaf -like '.env*' -and $leaf -ne '.env.example')
        if ($localDirectory -or $localFile) {
            $failures.Add("Local-only file is a Git candidate: $normalized")
        }
        $workingPath = Read-WorkingPath $path
        if ($workingPath.State -eq 'Outside') {
            $failures.Add("Git candidate path is outside the project: $normalized")
            continue
        }
        if ($workingPath.State -eq 'Linked') {
            $failures.Add("Review linked path before sharing: $normalized")
            continue
        }
        if ($workingPath.State -ne 'File') {
            $missing++
            continue # The index blob was checked even for an unstaged deletion.
        }
        $file = New-Object IO.FileInfo($workingPath.Path)
        $checked++
        if ($file.Length -gt 100MB) {
            $failures.Add(('Working file exceeds 100 MiB: {0} ({1:N2} MiB)' -f $normalized, ($file.Length / 1MB)))
        } elseif ($file.Length -gt 50MB) {
            $large.Add(('Working file exceeds 50 MiB: {0} ({1:N2} MiB)' -f $normalized, ($file.Length / 1MB)))
        }
    }
    foreach ($message in $large) { Write-Warning $message }
    Write-Host "Checked $($blobEntries.Count) indexed blob entries and $checked existing source candidates; $missing absent candidate paths still received their index check."
    if ($failures.Count -gt 0) {
        foreach ($message in $failures) { Write-Host "ERROR: $message" }
        throw "Repository candidate check found $($failures.Count) issue(s). No files or Git state were changed."
    }
    Write-Host 'Source candidate check passed. Secrets and historical contents require separate review before sharing.'
    exit 0
} catch {
    Write-Error $_.Exception.Message -ErrorAction Continue
    exit 1
}
