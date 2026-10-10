#requires -Version 5.1
<#
.SYNOPSIS
Read-only project size and Git object inventory.
.DESCRIPTION
Reads file names, lengths and Git metadata. Does not read game asset contents,
delete or move files, or change the Git index, refs or configuration.
Directory junctions and symbolic links are not traversed. Directories named
.git are excluded from the workspace scan; -IncludeGitDirectory measures the
root repository metadata separately. -IncludeGitHistory examines objects
reachable from local refs and reflogs (not an entire recovery backup).
.EXAMPLE
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Get-ProjectInventory.ps1
.EXAMPLE
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Get-ProjectInventory.ps1 -IncludeGitDirectory -IncludeGitHistory -MarkdownPath .\build\project-inventory.md
#>
[CmdletBinding()]
param(
    [string]$ProjectRoot,
    [string]$MarkdownPath,
    [ValidateRange(1, 100)][int]$Top = 20,
    [switch]$IncludeGitDirectory,
    [switch]$IncludeGitHistory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$root = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd([char[]]@('\', '/'))
if (-not [IO.Directory]::Exists($root)) { throw "Project directory not found: $root" }
$rootPrefix = $root + [IO.Path]::DirectorySeparatorChar
$scanNotes = New-Object 'System.Collections.Generic.List[string]'
$largestFiles = New-Object 'System.Collections.Generic.List[object]'

function Format-Bytes([long]$Bytes) {
    if ($Bytes -ge 1GB) { return ('{0:N2} GiB' -f ($Bytes / 1GB)) }
    if ($Bytes -ge 1MB) { return ('{0:N2} MiB' -f ($Bytes / 1MB)) }
    if ($Bytes -ge 1KB) { return ('{0:N2} KiB' -f ($Bytes / 1KB)) }
    return "$Bytes B"
}

function Format-Cell([string]$Value) {
    return $Value.Replace('|', '\|').Replace("`r", '').Replace("`n", ' ')
}

function Add-LargeFile([string]$Path, [long]$Bytes) {
    $largestFiles.Add([pscustomobject]@{ Path = $Path; Bytes = $Bytes })
    if ($largestFiles.Count -gt ($Top * 2)) {
        $keep = @($largestFiles | Sort-Object Bytes -Descending | Select-Object -First $Top)
        $largestFiles.Clear()
        foreach ($item in $keep) { $largestFiles.Add($item) }
    }
}

function Measure-Directory([string]$Path, [bool]$CollectLargest, [bool]$SkipGit) {
    $pending = New-Object 'System.Collections.Generic.Stack[string]'
    $pending.Push($Path)
    [long]$bytes = 0
    [long]$files = 0
    [long]$directories = 0
    while ($pending.Count -gt 0) {
        $current = $pending.Pop()
        try {
            # Enumerating one directory at a time avoids following reparse points.
            $entries = (New-Object IO.DirectoryInfo($current)).GetFileSystemInfos()
            foreach ($entry in $entries) {
                if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                    $scanNotes.Add("Skipped reparse point: $($entry.FullName)")
                    continue
                }
                if (($entry.Attributes -band [IO.FileAttributes]::Directory) -ne 0) {
                    if ($SkipGit -and $entry.Name -eq '.git') {
                        $scanNotes.Add("Excluded Git directory: $($entry.FullName)")
                        continue
                    }
                    $directories++
                    $pending.Push($entry.FullName)
                }
                else {
                    $bytes += $entry.Length
                    $files++
                    if ($CollectLargest) {
                        Add-LargeFile $entry.FullName.Substring($rootPrefix.Length).Replace('\', '/') $entry.Length
                    }
                }
            }
        }
        catch { $scanNotes.Add("Incomplete directory scan: $current -- $($_.Exception.Message)") }
    }
    return [pscustomobject]@{ Bytes = $bytes; Files = $files; Directories = $directories }
}

function Get-Category([string]$Name) {
    switch -Wildcard ($Name) {
        'src' { return '源码，适合源码仓库' }
        'tests' { return '测试，适合源码仓库' }
        'tools' { return '编译与检查工具，适合源码仓库' }
        'cmake' { return '分模块的 CMake 配置，适合源码仓库' }
        '.github' { return '协作模板与工作流，适合源码仓库' }
        'docs' { return '项目文档，适合源码仓库' }
        'samples' { return '示例，适合源码仓库' }
        'resources' { return '工程自带资源，发布前核对来源' }
        'RedAlert2' { return '原游戏素材，本地提供，不随源码仓库发布' }
        'local_data' { return '本地素材与数据，不随源码仓库发布' }
        'build' { return '可再生成的编译产物，本地保留' }
        'bin' { return '可再生成的运行程序，本地保留' }
        'development_records' { return '历史核验记录，本地完整保留' }
        'Git*备份_*' { return '原始 Git 与未提交工作副本备份，本地完整保留' }
        '%SystemDrive%' { return '名称是字面量，来源待人工核对，保持原状' }
        '(root files)' { return '工程入口与配置，逐文件判断是否适合源码仓库' }
        default { return '其他本地文件，需按用途核对' }
    }
}

function Invoke-GitRead([string[]]$Arguments, [string]$InputText = '') {
    # Explicit UTF-8 preserves non-ASCII names in Windows PowerShell 5.1.
    $gitCommand = Get-Command git -CommandType Application -ErrorAction Stop | Select-Object -First 1
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $gitCommand.Source
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.RedirectStandardInput = $true
    $utf8 = New-Object Text.UTF8Encoding($false)
    $start.StandardOutputEncoding = $utf8
    $start.StandardErrorEncoding = $utf8
    # These controlled Git arguments contain no embedded quotes or backslashes.
    $start.Arguments = (($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' ')
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $start
    try {
        [void]$process.Start()
        $outputTask = $process.StandardOutput.ReadToEndAsync()
        $errorTask = $process.StandardError.ReadToEndAsync()
        if ($InputText.Length -gt 0) {
            # Framework used by Windows PowerShell lacks StandardInputEncoding.
            # Write UTF-8 directly so historical Chinese paths remain intact.
            $inputBytes = $utf8.GetBytes($InputText)
            $process.StandardInput.BaseStream.Write($inputBytes, 0, $inputBytes.Length)
            $process.StandardInput.BaseStream.Flush()
        }
        $process.StandardInput.Close()
        $process.WaitForExit()
        $outputText = $outputTask.GetAwaiter().GetResult()
        $errorText = $errorTask.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) { throw "Git read failed ($($process.ExitCode)): $errorText" }
        return $outputText
    }
    finally { $process.Dispose() }
}

$groups = New-Object 'System.Collections.Generic.List[object]'
[long]$rootFileBytes = 0
[long]$rootFileCount = 0
foreach ($entry in (New-Object IO.DirectoryInfo($root)).GetFileSystemInfos()) {
    if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        $scanNotes.Add("Skipped root reparse point: $($entry.FullName)")
        continue
    }
    if (($entry.Attributes -band [IO.FileAttributes]::Directory) -ne 0) {
        if ($entry.Name -eq '.git') { continue }
        $measurement = Measure-Directory $entry.FullName $true $true
        $groups.Add([pscustomobject]@{
            Name = $entry.Name
            Bytes = $measurement.Bytes
            Files = $measurement.Files
            Category = Get-Category $entry.Name
        })
    }
    else {
        $rootFileBytes += $entry.Length
        $rootFileCount++
        Add-LargeFile $entry.Name $entry.Length
    }
}
$groups.Add([pscustomobject]@{
    Name = '(root files)'; Bytes = $rootFileBytes; Files = $rootFileCount
    Category = Get-Category '(root files)'
})
[long]$workspaceBytes = ($groups | Measure-Object Bytes -Sum).Sum
[long]$workspaceFiles = ($groups | Measure-Object Files -Sum).Sum
$gitPhysical = $null
if ($IncludeGitDirectory -and [IO.Directory]::Exists((Join-Path $root '.git'))) {
    $gitPhysical = Measure-Directory (Join-Path $root '.git') $false $false
}

$indexEntries = @()
$indexBlobs = @()
$historyBlobs = @()
$gitStorage = ''
$gitFailure = ''
$commitCount = ''
try {
    $gitStorage = Invoke-GitRead @('count-objects', '-v')
    $indexText = Invoke-GitRead @('-c', 'core.quotepath=false', 'ls-files', '--stage')
    $indexEntries = @($indexText -split "`n" | Where-Object { $_ -match '^\d+ ([0-9a-f]+) ([0-3])\t(.+)$' } | ForEach-Object {
        [pscustomobject]@{ Object = $Matches[1]; Stage = [int]$Matches[2]; Path = $Matches[3].TrimEnd("`r") }
    })
    if ($indexEntries.Count -gt 0) {
        $indexInput = (($indexEntries | ForEach-Object { $_.Object + ' ' + $_.Path }) -join "`n") + "`n"
        $indexMeta = Invoke-GitRead @('cat-file', '--batch-check=%(objectname) %(objecttype) %(objectsize) %(rest)') $indexInput
        $indexBlobs = @($indexMeta -split "`n" | Where-Object { $_ -match '^([0-9a-f]+) blob (\d+) (.*)$' } | ForEach-Object {
            [pscustomobject]@{ Object = $Matches[1]; Bytes = [long]$Matches[2]; Path = $Matches[3].TrimEnd("`r") }
        })
    }
    if ($IncludeGitHistory) {
        $commitCount = (Invoke-GitRead @('rev-list', '--count', '--all', '--reflog')).Trim()
        $objectNames = Invoke-GitRead @('-c', 'core.quotepath=false', 'rev-list', '--objects', '--all', '--reflog')
        $historyMeta = Invoke-GitRead @('cat-file', '--batch-check=%(objectname) %(objecttype) %(objectsize) %(rest)') $objectNames
        $historyBlobs = @($historyMeta -split "`n" | Where-Object { $_ -match '^([0-9a-f]+) blob (\d+) ?(.*)$' } | ForEach-Object {
            [pscustomobject]@{ Object = $Matches[1]; Bytes = [long]$Matches[2]; Path = $Matches[3].TrimEnd("`r") }
        })
    }
}
catch { $gitFailure = $_.Exception.Message }

$report = New-Object 'System.Collections.Generic.List[string]'
$report.Add('# 项目体积与 Git 跟踪清单')
$report.Add('')
$report.Add('生成时间：' + [DateTimeOffset]::UtcNow.ToOffset([TimeSpan]::FromHours(8)).ToString('yyyy-MM-dd HH:mm:ss zzz') + '（Asia/Shanghai）')
$report.Add('')
$report.Add('审计目录：`' + $root.Replace('\', '/') + '`')
$report.Add('')
$report.Add('本报告只读取文件名、文件长度和 Git 对象元数据；没有打开素材内容，没有移动或删除文件，没有改变 Git 状态。体积为文件长度之和，不是文件系统实际占用。工作区扫描默认跳过 `.git` 目录、符号链接与目录联接；活动仓库的根 `.git` 可单独统计。其他开发者同时写入文件时，数量和体积会变化，请将结果视为生成时附近的快照。')
$report.Add('')
$report.Add('## 工作区目录')
$report.Add('')
$report.Add(('排除 Git 元数据后的合计：**{0}**，**{1:N0} 个文件**。' -f (Format-Bytes $workspaceBytes), $workspaceFiles))
if ($null -ne $gitPhysical) {
    $report.Add(('活动仓库根 `.git` 单独统计：**{0}**，**{1:N0} 个文件**。' -f (Format-Bytes $gitPhysical.Bytes), $gitPhysical.Files))
}
$report.Add('')
$report.Add('| 根目录 / 文件组 | 体积 | 文件数 | 用途与保存位置 |')
$report.Add('| --- | ---: | ---: | --- |')
foreach ($group in ($groups | Sort-Object Bytes -Descending)) {
    $report.Add(('| `{0}` | {1} | {2:N0} | {3} |' -f (Format-Cell $group.Name), (Format-Bytes $group.Bytes), $group.Files, $group.Category))
}
$report.Add('')
$report.Add('## 工作区最大文件')
$report.Add('')
$report.Add('| 路径 | 体积 |')
$report.Add('| --- | ---: |')
foreach ($file in ($largestFiles | Sort-Object Bytes -Descending | Select-Object -First $Top)) {
    $report.Add(('| `{0}` | {1} |' -f (Format-Cell $file.Path), (Format-Bytes $file.Bytes)))
}
$report.Add('')
$report.Add('## Git 索引与本地历史')
$report.Add('')
if ($gitFailure) {
    $report.Add('Git 元数据扫描未完成：' + (Format-Cell $gitFailure))
}
else {
    [long]$indexBytes = ($indexBlobs | Measure-Object Bytes -Sum).Sum
    $indexOver50 = @($indexBlobs | Where-Object { $_.Bytes -gt 50MB })
    $indexOver100 = @($indexBlobs | Where-Object { $_.Bytes -gt 100MB })
    $report.Add(('当前索引：**{0:N0} 项**，文件对象逻辑内容 **{1}**；超过 50 MiB 的索引项 **{2}** 个，超过 100 MiB 的索引项 **{3}** 个。此处统计 Git 索引中的对象，包含工作区已删除但尚未提交删除的文件，不包含新建且未跟踪的文件。' -f $indexEntries.Count, (Format-Bytes $indexBytes), $indexOver50.Count, $indexOver100.Count))
    $report.Add('')
    $report.Add('索引中最大的文件：')
    $report.Add('')
    $report.Add('| 被跟踪路径 | 文件对象大小 |')
    $report.Add('| --- | ---: |')
    foreach ($file in ($indexBlobs | Sort-Object Bytes -Descending | Select-Object -First $Top)) {
        $report.Add(('| `{0}` | {1} |' -f (Format-Cell $file.Path), (Format-Bytes $file.Bytes)))
    }
    if ($IncludeGitHistory) {
        [long]$historyBytes = ($historyBlobs | Measure-Object Bytes -Sum).Sum
        $historyOver50 = @($historyBlobs | Where-Object { $_.Bytes -gt 50MB })
        $historyOver100 = @($historyBlobs | Where-Object { $_.Bytes -gt 100MB })
        $report.Add('')
        $report.Add(('本地所有引用与 reflog 可达的历史：**{0} 个提交**，**{1:N0} 个不同文件对象**，解压后的文件逻辑内容 **{2}**；超过 50 MiB 的对象 **{3}** 个，超过 100 MiB 的对象 **{4}** 个。历史对象的路径是遍历中发现的一个名称，不代表该对象出现过的全部路径；重复路径对应不同历史版本。' -f $commitCount, $historyBlobs.Count, (Format-Bytes $historyBytes), $historyOver50.Count, $historyOver100.Count))
        $report.Add('')
        $report.Add('| 历史文件路径 | 文件对象大小 | 对象 ID（前 12 位） |')
        $report.Add('| --- | ---: | --- |')
        foreach ($file in ($historyBlobs | Sort-Object Bytes -Descending | Select-Object -First $Top)) {
            $report.Add(('| `{0}` | {1} | `{2}` |' -f (Format-Cell $file.Path), (Format-Bytes $file.Bytes), $file.Object.Substring(0, 12)))
        }
    }
    else {
        $report.Add('')
        $report.Add('未执行历史对象扫描。加 `-IncludeGitHistory` 可检查本地引用与 reflog 可达的对象。')
    }
    $report.Add('')
    $report.Add('Git 存储元数据（size、size-pack 和 size-garbage 的单位是 KiB；pack 为压缩后体积，与历史逻辑内容合计不同）：')
    $report.Add('')
    $report.Add('```text')
    $report.Add($gitStorage.Trim())
    $report.Add('```')
}
$report.Add('')
$report.Add('## 源码仓库与本地归档建议')
$report.Add('')
$report.Add('- 源码、测试、编译脚本、示例、经过整理的文档，以及确认归属的工程资源适合进入源码仓库。')
$report.Add('- 原游戏素材与本地数据由开发者另行准备。只克隆源码仓库，不能直接得到所有运行所需素材。')
$report.Add('- 编译产物与运行程序可在本地再生成；核验记录和历史恢复备份应排除在源码仓库外。现有目录和归档全部保留原位置，今后是否移动或删除由用户另行手动决定。忽略规则不会自动移除已被跟踪的文件。')
$report.Add('- 不能仅因历史逻辑内容合计较大就重写历史。应先看单个对象大小和 Git 压缩后存储体积。原始历史备份属于普通工作区文件，不是活动仓库当前历史。')
$report.Add('- `%SystemDrive%` 是实际存在的字面量目录名，只记录大小，保持原状；后续先人工核对来源，不能将它当成已展开的系统盘路径。')
$report.Add('- 保留完整本地备份。本报告不执行或授权清理、历史重写、Git LFS 迁移或第三方素材发布。报告仅针对指定工程目录。')
$report.Add('')
$report.Add('## 扫描限制')
$report.Add('')
if ($scanNotes.Count -eq 0) { $report.Add('没有遇到不可访问的目录或 reparse point。根 `.git` 默认排除，只有显式要求才单独统计。') }
else { foreach ($note in $scanNotes) { $report.Add('- ' + (Format-Cell $note)) } }
$report.Add('')
$report.Add('在项目根目录重新生成本报告（覆盖此报告文件，不改变其他文件）：')
$report.Add('')
$report.Add('```powershell')
$report.Add('powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Get-ProjectInventory.ps1 -IncludeGitDirectory -IncludeGitHistory -MarkdownPath .\build\project-inventory.md')
$report.Add('```')
$markdown = ($report -join "`r`n") + "`r`n"
if ($MarkdownPath) {
    if (-not [IO.Path]::IsPathRooted($MarkdownPath)) { $MarkdownPath = Join-Path $root $MarkdownPath }
    $outputPath = [IO.Path]::GetFullPath($MarkdownPath)
    if ([IO.Path]::GetExtension($outputPath) -ne '.md') { throw 'MarkdownPath must be a .md report file.' }
    if (-not $outputPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'MarkdownPath must stay within the project directory.'
    }
    $relativeOutput = $outputPath.Substring($rootPrefix.Length)
    if (($relativeOutput -split '[\\/]') -contains '.git') {
        throw 'MarkdownPath must not write into Git metadata.'
    }
    $outputDirectory = Split-Path -Parent $outputPath
    if (-not [IO.Directory]::Exists($outputDirectory)) { throw "Output directory not found: $outputDirectory" }
    $ancestor = New-Object IO.DirectoryInfo($outputDirectory)
    while ($null -ne $ancestor -and $ancestor.FullName.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
        if (($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'MarkdownPath must not traverse directory junctions or symbolic links.'
        }
        if ($ancestor.FullName -eq $root) { break }
        $ancestor = $ancestor.Parent
    }
    if ([IO.File]::Exists($outputPath) -and (([IO.File]::GetAttributes($outputPath) -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
        throw 'MarkdownPath must not be a symbolic link.'
    }
    [IO.File]::WriteAllText($outputPath, $markdown, (New-Object Text.UTF8Encoding($false)))
    Write-Output "Inventory saved: $outputPath"
}
else { Write-Output $markdown }
