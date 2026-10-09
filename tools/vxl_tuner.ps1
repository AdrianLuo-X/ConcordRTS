# ConcordRTS VXL tuner.
# Run with:
#   powershell -ExecutionPolicy Bypass -File .\tools\vxl_tuner.ps1
#
# This tool does not edit source files. It launches concord_runtime.exe with
# temporary environment variables, renders a smoke-test preview, and displays
# the generated BMP in the panel on the right.

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$ErrorActionPreference = "Stop"

$script:RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$script:RuntimeExe = Join-Path $script:RepoRoot "build\Debug\concord_runtime.exe"
$script:DefaultMap = Join-Path $script:RepoRoot "local_data\resources\mix_exports\multimd\2peaks.map"
$script:ResourceRoot = Join-Path $script:RepoRoot "RedAlert2"
$script:LocalDataRoot = Join-Path $script:RepoRoot "local_data"
$script:PreviewBmp = Join-Path $script:RepoRoot "build\vxl_tuner_preview.bmp"
$script:IsRendering = $false

function Add-LabeledTrackBar {
    param(
        [System.Windows.Forms.Control] $Parent,
        [string] $Text,
        [int] $Minimum,
        [int] $Maximum,
        [int] $Value,
        [int] $Top,
        [scriptblock] $Format
    )

    $label = New-Object System.Windows.Forms.Label
    $label.Text = $Text
    $label.Left = 10
    $label.Top = $Top + 5
    $label.Width = 145
    $Parent.Controls.Add($label)

    $valueLabel = New-Object System.Windows.Forms.Label
    $valueLabel.Left = 255
    $valueLabel.Top = $Top + 5
    $valueLabel.Width = 62
    $Parent.Controls.Add($valueLabel)

    $track = New-Object System.Windows.Forms.TrackBar
    $track.Left = 150
    $track.Top = $Top
    $track.Width = 105
    $track.Minimum = $Minimum
    $track.Maximum = $Maximum
    $track.Value = $Value
    $track.TickStyle = [System.Windows.Forms.TickStyle]::None
    $track.SmallChange = 1
    $track.LargeChange = [Math]::Max(1, [int](($Maximum - $Minimum) / 10))
    $Parent.Controls.Add($track)

    $update = {
        $valueLabel.Text = (& $Format $track.Value)
    }.GetNewClosure()
    & $update
    $track.Add_Scroll($update)

    return @{
        Track = $track
        Label = $valueLabel
        Format = $Format
    }
}

function BoolEnv {
    param([bool] $Value)
    if ($Value) {
        return "1"
    }
    return "0"
}

function TrackValue {
    param($TrackInfo)
    return $TrackInfo.Track.Value
}

function TrackDecimal {
    param($TrackInfo, [double] $Divisor)
    return ([double](TrackValue $TrackInfo) / $Divisor).ToString("0.###", [Globalization.CultureInfo]::InvariantCulture)
}

function Load-ImageUnlocked {
    param([string] $Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $stream = New-Object System.IO.MemoryStream(,$bytes)
    $temp = [System.Drawing.Image]::FromStream($stream)
    $bitmap = New-Object System.Drawing.Bitmap $temp
    $temp.Dispose()
    $stream.Dispose()
    return $bitmap
}

function Make-FacingObjectList {
    param([string] $UnitId)
    $facings = @(0, 32, 64, 96, 128, 160, 192, 224)
    return ($facings | ForEach-Object { "$UnitId@$_" }) -join ","
}

function Invoke-ProcessCapture {
    param(
        [string] $FileName,
        [string] $Arguments,
        [string] $WorkingDirectory,
        [hashtable] $Environment
    )

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FileName
    $psi.Arguments = $Arguments
    $psi.WorkingDirectory = $WorkingDirectory
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    if ($Environment -ne $null) {
        foreach ($key in $Environment.Keys) {
            $psi.Environment[$key] = [string]$Environment[$key]
        }
    }

    $process = [System.Diagnostics.Process]::Start($psi)
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()

    return @{
        ExitCode = $process.ExitCode
        Stdout = $stdout
        Stderr = $stderr
    }
}

function Summarize-RuntimeOutput {
    param([string] $Text)

    $lines = @()
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match "Rendered:|Injected render objects:|Voxel sprites:|Runtime Render Smoke timings") {
            $lines += $line
        }
    }
    return ($lines -join "`r`n")
}

$form = New-Object System.Windows.Forms.Form
$form.Text = "ConcordRTS VXL Tuner"
$form.Width = 1220
$form.Height = 820
$form.StartPosition = "CenterScreen"

$left = New-Object System.Windows.Forms.Panel
$left.Left = 0
$left.Top = 0
$left.Width = 350
$left.Height = 780
$left.Anchor = "Top,Bottom,Left"
$left.AutoScroll = $true
$form.Controls.Add($left)

$right = New-Object System.Windows.Forms.Panel
$right.Left = 350
$right.Top = 0
$right.Width = 850
$right.Height = 780
$right.Anchor = "Top,Bottom,Left,Right"
$form.Controls.Add($right)

$picture = New-Object System.Windows.Forms.PictureBox
$picture.Left = 10
$picture.Top = 10
$picture.Width = 820
$picture.Height = 650
$picture.Anchor = "Top,Bottom,Left,Right"
$picture.BorderStyle = [System.Windows.Forms.BorderStyle]::FixedSingle
$picture.SizeMode = [System.Windows.Forms.PictureBoxSizeMode]::Zoom
$right.Controls.Add($picture)

$statusBox = New-Object System.Windows.Forms.TextBox
$statusBox.Left = 10
$statusBox.Top = 670
$statusBox.Width = 820
$statusBox.Height = 80
$statusBox.Anchor = "Bottom,Left,Right"
$statusBox.Multiline = $true
$statusBox.ScrollBars = "Vertical"
$statusBox.ReadOnly = $true
$right.Controls.Add($statusBox)

$unitLabel = New-Object System.Windows.Forms.Label
$unitLabel.Text = "Unit / object id"
$unitLabel.Left = 10
$unitLabel.Top = 12
$unitLabel.Width = 120
$left.Controls.Add($unitLabel)

$unitCombo = New-Object System.Windows.Forms.ComboBox
$unitCombo.Left = 130
$unitCombo.Top = 8
$unitCombo.Width = 200
$unitCombo.DropDownStyle = [System.Windows.Forms.ComboBoxStyle]::DropDown
@(
    "MTNK", "FV", "ROBO", "HTNK", "APOC", "LTNK", "TNKD", "TTNK", "YTNK",
    "AMCV", "SMCV", "PCV", "CMIN", "HARV", "SMIN",
    "SREF", "BFRT", "MGTK", "V3", "DTRUCK", "DRON", "HTK", "FTRK",
    "UTNK", "TELE", "MIND", "DISK",
    "GTGCAN", "NASAM", "YAGGUN", "NAFLAK", "TESLA", "GAPILL", "NALASR"
) | ForEach-Object { [void]$unitCombo.Items.Add($_) }
$unitCombo.Text = "MTNK"
$left.Controls.Add($unitCombo)

$customLabel = New-Object System.Windows.Forms.Label
$customLabel.Text = "Custom list"
$customLabel.Left = 10
$customLabel.Top = 42
$customLabel.Width = 120
$left.Controls.Add($customLabel)

$customText = New-Object System.Windows.Forms.TextBox
$customText.Left = 130
$customText.Top = 38
$customText.Width = 200
$customText.Text = ""
$left.Controls.Add($customText)

$eightDirections = New-Object System.Windows.Forms.CheckBox
$eightDirections.Text = "8 facings for selected id"
$eightDirections.Left = 10
$eightDirections.Top = 66
$eightDirections.Width = 220
$eightDirections.Checked = $true
$left.Controls.Add($eightDirections)

$autoRender = New-Object System.Windows.Forms.CheckBox
$autoRender.Text = "Auto render after slider change"
$autoRender.Left = 10
$autoRender.Top = 90
$autoRender.Width = 230
$autoRender.Checked = $false
$left.Controls.Add($autoRender)

$useVpl = New-Object System.Windows.Forms.CheckBox
$useVpl.Text = "Use voxels.vpl"
$useVpl.Left = 10
$useVpl.Top = 116
$useVpl.Width = 130
$useVpl.Checked = $true
$left.Controls.Add($useVpl)

$detailPoints = New-Object System.Windows.Forms.CheckBox
$detailPoints.Text = "Detail points"
$detailPoints.Left = 145
$detailPoints.Top = 116
$detailPoints.Width = 120
$detailPoints.Checked = $true
$left.Controls.Add($detailPoints)

$soften = New-Object System.Windows.Forms.CheckBox
$soften.Text = "Soften"
$soften.Left = 10
$soften.Top = 140
$soften.Width = 80
$soften.Checked = $false
$left.Controls.Add($soften)

$edgeDarken = New-Object System.Windows.Forms.CheckBox
$edgeDarken.Text = "Edge darken"
$edgeDarken.Left = 145
$edgeDarken.Top = 140
$edgeDarken.Width = 120
$edgeDarken.Checked = $true
$left.Controls.Add($edgeDarken)

$scale = Add-LabeledTrackBar $left "Scale" 50 140 96 174 { param($v) ($v / 100.0).ToString("0.00") }
$vplSection = Add-LabeledTrackBar $left "VPL neutral" 0 31 13 214 { param($v) "$v" }
$normalDetail = Add-LabeledTrackBar $left "Normal detail" 0 100 0 254 { param($v) ($v / 100.0).ToString("0.00") }
$brightness = Add-LabeledTrackBar $left "Brightness lift" 0 32 0 294 { param($v) "$v" }
$contrast = Add-LabeledTrackBar $left "Contrast" 85 135 104 334 { param($v) ($v / 100.0).ToString("0.00") }
$saturation = Add-LabeledTrackBar $left "Saturation" 80 155 108 374 { param($v) ($v / 100.0).ToString("0.00") }
$valueLift = Add-LabeledTrackBar $left "Value lift" -8 16 0 414 { param($v) ($v / 100.0).ToString("0.00") }
$lightContrast = Add-LabeledTrackBar $left "Light contrast" 70 175 124 454 { param($v) ($v / 100.0).ToString("0.00") }
$metal = Add-LabeledTrackBar $left "Metal highlight" 0 32 19 494 { param($v) ($v / 100.0).ToString("0.00") }
$remapMetal = Add-LabeledTrackBar $left "Remap highlight" 0 24 10 534 { param($v) ($v / 100.0).ToString("0.00") }
$pixelFootprint = Add-LabeledTrackBar $left "Pixel footprint" 1 3 1 574 { param($v) "$v" }
$turretOffset = Add-LabeledTrackBar $left "Turret offset" 0 100 0 614 { param($v) ($v / 100.0).ToString("0.00") }
$detailStrength = Add-LabeledTrackBar $left "Detail strength" 0 200 160 654 { param($v) ($v / 100.0).ToString("0.00") }
$detailDepth = Add-LabeledTrackBar $left "Detail depth" 0 1200 500 694 { param($v) ($v / 100.0).ToString("0.00") }
$lightX = Add-LabeledTrackBar $left "Light X" -100 100 -62 734 { param($v) ($v / 100.0).ToString("0.00") }
$lightY = Add-LabeledTrackBar $left "Light Y" -100 100 -28 774 { param($v) ($v / 100.0).ToString("0.00") }
$lightZ = Add-LabeledTrackBar $left "Light Z" 5 100 73 814 { param($v) ($v / 100.0).ToString("0.00") }
$lightGain = Add-LabeledTrackBar $left "Light gain" 60 450 255 854 { param($v) ($v / 100.0).ToString("0.00") }
$lightBias = Add-LabeledTrackBar $left "Light bias" -200 200 35 894 { param($v) ($v / 100.0).ToString("0.00") }

$debugLabel = New-Object System.Windows.Forms.Label
$debugLabel.Text = "Debug mode"
$debugLabel.Left = 10
$debugLabel.Top = 938
$debugLabel.Width = 120
$left.Controls.Add($debugLabel)

$debugCombo = New-Object System.Windows.Forms.ComboBox
$debugCombo.Left = 130
$debugCombo.Top = 934
$debugCombo.Width = 200
$debugCombo.DropDownStyle = [System.Windows.Forms.ComboBoxStyle]::DropDown
@("", "bounds", "anchor", "pivot", "section", "flat", "depth") |
    ForEach-Object { [void]$debugCombo.Items.Add($_) }
$debugCombo.Text = ""
$left.Controls.Add($debugCombo)

$renderButton = New-Object System.Windows.Forms.Button
$renderButton.Text = "Render preview"
$renderButton.Left = 10
$renderButton.Top = 970
$renderButton.Width = 150
$left.Controls.Add($renderButton)

$buildButton = New-Object System.Windows.Forms.Button
$buildButton.Text = "Build runtime"
$buildButton.Left = 180
$buildButton.Top = 970
$buildButton.Width = 150
$left.Controls.Add($buildButton)

$group1 = New-Object System.Windows.Forms.Button
$group1.Text = "MCV + miners"
$group1.Left = 10
$group1.Top = 1008
$group1.Width = 100
$left.Controls.Add($group1)

$group2 = New-Object System.Windows.Forms.Button
$group2.Text = "Tanks"
$group2.Left = 120
$group2.Top = 1008
$group2.Width = 90
$left.Controls.Add($group2)

$group3 = New-Object System.Windows.Forms.Button
$group3.Text = "Defense"
$group3.Left = 220
$group3.Top = 1008
$group3.Width = 110
$left.Controls.Add($group3)

$group1.Add_Click({
    $eightDirections.Checked = $false
    $customText.Text = "AMCV,SMCV,PCV,CMIN,HARV,SMIN"
})
$group2.Add_Click({
    $eightDirections.Checked = $false
    $customText.Text = "MTNK,FV,ROBO,HTNK,APOC,LTNK,TNKD,TTNK,YTNK"
})
$group3.Add_Click({
    $eightDirections.Checked = $false
    $customText.Text = "GTGCAN,NASAM,YAGGUN,NAFLAK,TESLA,GAPILL,NALASR"
})

$autoTimer = New-Object System.Windows.Forms.Timer
$autoTimer.Interval = 900

function Request-AutoRender {
    if ($autoRender.Checked) {
        $autoTimer.Stop()
        $autoTimer.Start()
    }
}

foreach ($trackInfo in @(
    $scale, $vplSection, $normalDetail, $brightness, $contrast, $saturation, $valueLift,
    $lightContrast, $metal, $remapMetal, $pixelFootprint, $turretOffset,
    $detailStrength, $detailDepth, $lightX, $lightY, $lightZ, $lightGain, $lightBias
)) {
    $trackInfo.Track.Add_Scroll({ Request-AutoRender })
}
foreach ($check in @($useVpl, $detailPoints, $soften, $edgeDarken, $eightDirections)) {
    $check.Add_CheckedChanged({ Request-AutoRender })
}
$unitCombo.Add_SelectedIndexChanged({
    $customText.Text = ""
    $eightDirections.Checked = $true
    Request-AutoRender
})
$unitCombo.Add_TextChanged({ Request-AutoRender })
$debugCombo.Add_TextChanged({ Request-AutoRender })
$customText.Add_TextChanged({
    if (-not $eightDirections.Checked) {
        Request-AutoRender
    }
})

function Start-Render {
    if ($script:IsRendering) {
        return
    }
    if (-not (Test-Path $script:RuntimeExe)) {
        [System.Windows.Forms.MessageBox]::Show(
            "Missing runtime exe. Click 'Build runtime' first.`n$script:RuntimeExe",
            "ConcordRTS VXL Tuner"
        ) | Out-Null
        return
    }

    $unitId = $unitCombo.Text.Trim().ToUpperInvariant()
    $objectList = $customText.Text.Trim()
    if ($eightDirections.Checked -or [string]::IsNullOrWhiteSpace($objectList)) {
        if ([string]::IsNullOrWhiteSpace($unitId)) {
            return
        }
        $objectList = Make-FacingObjectList $unitId
    }

    $script:IsRendering = $true
    $renderButton.Enabled = $false
    $statusBox.Text = "Rendering..."

    $envMap = @{
        "CONCORD_RENDER_SMOKE_OBJECTS" = $objectList
        "CONCORD_RENDER_SMOKE_CELL" = "80,56"
        "CONCORD_RENDER_SMOKE_COLUMNS" = "4"
        "CONCORD_RENDER_SMOKE_SPACING_X" = "4"
        "CONCORD_RENDER_SMOKE_SPACING_Y" = "4"
        "CONCORD_RENDER_SMOKE_BMP" = $script:PreviewBmp
        "CONCORD_VXL_SCALE" = (TrackDecimal $scale 100.0)
        "CONCORD_VXL_VPL_NEUTRAL_SECTION" = (TrackValue $vplSection).ToString()
        "CONCORD_VXL_VPL_NORMAL_DETAIL" = (TrackDecimal $normalDetail 100.0)
        "CONCORD_VXL_DETAIL_POINTS" = (BoolEnv $detailPoints.Checked)
        "CONCORD_VXL_SOFTEN" = (BoolEnv $soften.Checked)
        "CONCORD_VXL_EDGE_DARKEN" = (BoolEnv $edgeDarken.Checked)
        "CONCORD_VXL_USE_VPL" = (BoolEnv $useVpl.Checked)
        "CONCORD_VXL_BRIGHTNESS_LIFT" = (TrackValue $brightness).ToString()
        "CONCORD_VXL_CONTRAST" = (TrackDecimal $contrast 100.0)
        "CONCORD_VXL_SATURATION" = (TrackDecimal $saturation 100.0)
        "CONCORD_VXL_VALUE_LIFT" = (TrackDecimal $valueLift 100.0)
        "CONCORD_VXL_LIGHT_CONTRAST" = (TrackDecimal $lightContrast 100.0)
        "CONCORD_VXL_METAL_HIGHLIGHT" = (TrackDecimal $metal 100.0)
        "CONCORD_VXL_REMAP_METAL_HIGHLIGHT" = (TrackDecimal $remapMetal 100.0)
        "CONCORD_VXL_PIXEL_FOOTPRINT" = (TrackValue $pixelFootprint).ToString()
        "CONCORD_VXL_TURRET_OFFSET_SCALE" = (TrackDecimal $turretOffset 100.0)
        "CONCORD_VXL_DETAIL_STRENGTH" = (TrackDecimal $detailStrength 100.0)
        "CONCORD_VXL_DETAIL_DEPTH_TOLERANCE" = (TrackDecimal $detailDepth 100.0)
        "CONCORD_VXL_LIGHT_X" = (TrackDecimal $lightX 100.0)
        "CONCORD_VXL_LIGHT_Y" = (TrackDecimal $lightY 100.0)
        "CONCORD_VXL_LIGHT_Z" = (TrackDecimal $lightZ 100.0)
        "CONCORD_VXL_LIGHT_GAIN" = (TrackDecimal $lightGain 100.0)
        "CONCORD_VXL_LIGHT_BIAS" = (TrackDecimal $lightBias 100.0)
    }
    if (-not [string]::IsNullOrWhiteSpace($debugCombo.Text)) {
        $envMap["CONCORD_VXL_DEBUG_MODE"] = $debugCombo.Text.Trim()
    }

    try {
        [System.Windows.Forms.Application]::DoEvents()
        $arguments =
            "--render-smoke-test --resource-root `"$script:ResourceRoot`" " +
            "--local-data-root `"$script:LocalDataRoot`" --map `"$script:DefaultMap`""
        $result = Invoke-ProcessCapture `
            -FileName $script:RuntimeExe `
            -Arguments $arguments `
            -WorkingDirectory $script:RepoRoot `
            -Environment $envMap

        $summary = Summarize-RuntimeOutput $result.Stdout
        $statusBox.Text = "ExitCode: $($result.ExitCode)`r`n$summary"
        if (-not [string]::IsNullOrWhiteSpace($result.Stderr)) {
            $statusBox.AppendText("`r`nSTDERR:`r`n$($result.Stderr)")
        }
        if ((Test-Path $script:PreviewBmp) -and $result.ExitCode -eq 0) {
            if ($picture.Image -ne $null) {
                $old = $picture.Image
                $picture.Image = $null
                $old.Dispose()
            }
            $picture.Image = Load-ImageUnlocked $script:PreviewBmp
        }
    } catch {
        $statusBox.Text = "Render failed:`r`n$($_.Exception.Message)"
    } finally {
        $script:IsRendering = $false
        $renderButton.Enabled = $true
    }
}

function Start-BuildRuntime {
    if ($script:IsRendering) {
        return
    }
    $script:IsRendering = $true
    $renderButton.Enabled = $false
    $buildButton.Enabled = $false
    $statusBox.Text = "Building concord_runtime..."

    try {
        $cmakeCommand = Get-Command cmake.exe -CommandType Application -ErrorAction SilentlyContinue
        if ($cmakeCommand) {
            $cmake = $cmakeCommand.Source
        } else {
            $vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
            if (-not (Test-Path -LiteralPath $vswhere)) {
                throw 'CMake was not found. Install CMake or Visual Studio C++ CMake tools.'
            }
            $vsInstallation = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
            if (-not $vsInstallation) {
                throw 'No Visual Studio C++ toolchain was found.'
            }
            $cmake = Join-Path $vsInstallation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (-not (Test-Path -LiteralPath $cmake -PathType Leaf)) {
                throw 'Enable the C++ CMake tools component in Visual Studio.'
            }
        }
        [System.Windows.Forms.Application]::DoEvents()
        $result = Invoke-ProcessCapture `
            -FileName $cmake `
            -Arguments "--build build --config Debug --target concord_runtime -- /m:1" `
            -WorkingDirectory $script:RepoRoot `
            -Environment $null
        $statusBox.Text = "Build exit: $($result.ExitCode)`r`n$($result.Stdout)`r`n$($result.Stderr)"
    } catch {
        $statusBox.Text = "Build failed:`r`n$($_.Exception.Message)"
    } finally {
        $script:IsRendering = $false
        $renderButton.Enabled = $true
        $buildButton.Enabled = $true
    }
}

$renderButton.add_Click({ Start-Render })
$buildButton.add_Click({ Start-BuildRuntime })
$autoTimer.add_Tick({
    $autoTimer.Stop()
    Start-Render
})

$form.Add_Shown({ Start-Render })
[void]$form.ShowDialog()
