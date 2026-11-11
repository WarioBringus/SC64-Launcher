# SC64 Launcher - Music converter. Opened by double-clicking "Music converter.bat".
# Turns MP3 and WAV files into the launcher's music format (.wav64) and copies them to the SD card's menu\music
# folder. Nothing to install: it uses Windows' own window toolkit and libdragon's audioconv64 (tools\bin).
#
# Tracks are VADPCM without Huffman packing, at 32 kHz: Huffman makes files ~20-30% smaller, but the N64 then
# spends about 40% of its CPU unpacking them while they play, enough to hold the menu below 60 fps.

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

$here = Split-Path -Parent $MyInvocation.MyCommand.Path      # tools\scripts
$tool = Join-Path (Split-Path -Parent $here) "bin\audioconv64.exe"

$form = New-Object System.Windows.Forms.Form
$form.Text = "SC64 Launcher - Music converter"
$form.Size = New-Object System.Drawing.Size(620, 470)
$form.MinimumSize = $form.Size
$form.StartPosition = "CenterScreen"
$form.Font = New-Object System.Drawing.Font("Segoe UI", 10)

$hint = New-Object System.Windows.Forms.Label
$hint.Text = "Drop MP3 or WAV files in the box below (or click Add songs), pick your SD card, then press Convert."
$hint.Location = New-Object System.Drawing.Point(14, 12)
$hint.Size = New-Object System.Drawing.Size(580, 40)
$hint.Anchor = "Top, Left, Right"
$form.Controls.Add($hint)

$list = New-Object System.Windows.Forms.ListBox
$list.Location = New-Object System.Drawing.Point(14, 56)
$list.Size = New-Object System.Drawing.Size(450, 250)
$list.Anchor = "Top, Bottom, Left, Right"
$list.SelectionMode = "MultiExtended"
$list.AllowDrop = $true
$list.HorizontalScrollbar = $true
$form.Controls.Add($list)

function Add-Songs([string[]]$paths) {
    foreach ($p in $paths) {
        if (Test-Path -LiteralPath $p -PathType Container) {
            Add-Songs (Get-ChildItem -LiteralPath $p -File | Where-Object { $_.Extension -match '^\.(mp3|wav)$' } |
                ForEach-Object { $_.FullName })
        } elseif ($p -match '\.(mp3|wav)$' -and -not $list.Items.Contains($p)) {
            [void]$list.Items.Add($p)
        }
    }
}

$list.Add_DragEnter({ if ($_.Data.GetDataPresent([Windows.Forms.DataFormats]::FileDrop)) { $_.Effect = "Copy" } })
$list.Add_DragDrop({ Add-Songs $_.Data.GetData([Windows.Forms.DataFormats]::FileDrop) })

function New-Button([string]$text, [int]$y) {
    $b = New-Object System.Windows.Forms.Button
    $b.Text = $text
    $b.Location = New-Object System.Drawing.Point(476, $y)
    $b.Size = New-Object System.Drawing.Size(118, 32)
    $b.Anchor = "Top, Right"
    $form.Controls.Add($b)
    return $b
}
$add = New-Button "Add songs..." 56
$remove = New-Button "Remove" 94
$clear = New-Button "Clear" 132

$add.Add_Click({
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Filter = "Music (*.mp3;*.wav)|*.mp3;*.wav"
    $dlg.Multiselect = $true
    if ($dlg.ShowDialog() -eq "OK") { Add-Songs $dlg.FileNames }
})
$remove.Add_Click({ @($list.SelectedItems) | ForEach-Object { $list.Items.Remove($_) } })
$clear.Add_Click({ $list.Items.Clear() })

$cardLabel = New-Object System.Windows.Forms.Label
$cardLabel.Text = "SD card:"
$cardLabel.Location = New-Object System.Drawing.Point(14, 322)
$cardLabel.Size = New-Object System.Drawing.Size(70, 26)
$cardLabel.Anchor = "Bottom, Left"
$form.Controls.Add($cardLabel)

$drives = New-Object System.Windows.Forms.ComboBox
$drives.DropDownStyle = "DropDownList"
$drives.Location = New-Object System.Drawing.Point(86, 318)
$drives.Size = New-Object System.Drawing.Size(250, 28)
$drives.Anchor = "Bottom, Left"
$form.Controls.Add($drives)

function Fill-Drives {
    $drives.Items.Clear()
    $pick = -1
    foreach ($d in [System.IO.DriveInfo]::GetDrives()) {
        if (-not $d.IsReady -or $d.DriveType -eq "CDRom") { continue }
        $has = Test-Path -LiteralPath (Join-Path $d.RootDirectory "sc64menu.n64")
        $name = $d.Name.TrimEnd('\') + "  " + $d.VolumeLabel + $(if ($has) { "  (has the launcher)" } else { "" })
        $i = $drives.Items.Add($name)
        if ($has -and $pick -lt 0) { $pick = $i }
    }
    if ($pick -ge 0) { $drives.SelectedIndex = $pick } elseif ($drives.Items.Count) { $drives.SelectedIndex = 0 }
}
Fill-Drives

$refresh = New-Object System.Windows.Forms.Button
$refresh.Text = "Refresh"
$refresh.Location = New-Object System.Drawing.Point(346, 317)
$refresh.Size = New-Object System.Drawing.Size(90, 30)
$refresh.Anchor = "Bottom, Left"
$refresh.Add_Click({ Fill-Drives })
$form.Controls.Add($refresh)

$go = New-Object System.Windows.Forms.Button
$go.Text = "Convert and copy to SD card"
$go.Location = New-Object System.Drawing.Point(14, 358)
$go.Size = New-Object System.Drawing.Size(250, 36)
$go.Anchor = "Bottom, Left"
$form.Controls.Add($go)

$bar = New-Object System.Windows.Forms.ProgressBar
$bar.Location = New-Object System.Drawing.Point(276, 364)
$bar.Size = New-Object System.Drawing.Size(318, 24)
$bar.Anchor = "Bottom, Left, Right"
$form.Controls.Add($bar)

$status = New-Object System.Windows.Forms.Label
$status.Location = New-Object System.Drawing.Point(14, 400)
$status.Size = New-Object System.Drawing.Size(580, 24)
$status.Anchor = "Bottom, Left, Right"
$form.Controls.Add($status)

$go.Add_Click({
    if (-not $list.Items.Count) { $status.Text = "Add some songs first."; return }
    if ($drives.SelectedIndex -lt 0) { $status.Text = "Plug in your SD card, then click Refresh."; return }
    if (-not (Test-Path $tool)) { $status.Text = "audioconv64.exe is missing from tools\bin."; return }
    $root = ($drives.SelectedItem -split '\s+')[0] + "\"
    $musicDir = Join-Path $root "menu\music"
    New-Item -ItemType Directory -Force $musicDir | Out-Null

    $go.Enabled = $false
    $bar.Maximum = $list.Items.Count
    $bar.Value = 0
    $done = 0; $failed = @()
    # audioconv64 mishandles some Windows paths, so each song is converted in a temporary folder under a plain name.
    $work = Join-Path $env:TEMP ("sc64-music-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force (Join-Path $work "out") | Out-Null
    try {
        foreach ($src in @($list.Items)) {
            $base = [IO.Path]::GetFileNameWithoutExtension($src)
            $safe = (($base.Normalize([Text.NormalizationForm]::FormD) -replace '\p{Mn}', '') -replace '[^A-Za-z0-9 _\-\(\)\.]', '_').Trim()
            if (-not $safe) { $safe = "track" }
            $status.Text = "Converting $base ..."
            [System.Windows.Forms.Application]::DoEvents()
            $tmp = Join-Path $work ($safe + [IO.Path]::GetExtension($src).ToLower())
            Copy-Item -LiteralPath $src $tmp -Force
            $p = Start-Process -FilePath $tool -ArgumentList @('--wav-compress', 'vadpcm,huffman=false', '--wav-resample', '32000',
                '-o', 'out', ('"' + (Split-Path -Leaf $tmp) + '"')) -WorkingDirectory $work -WindowStyle Hidden -PassThru -Wait
            $result = Join-Path $work "out\$safe.wav64"
            if ($p.ExitCode -eq 0 -and (Test-Path -LiteralPath $result)) {
                Copy-Item -LiteralPath $result (Join-Path $musicDir "$safe.wav64") -Force
                $done++
            } else {
                $failed += $base
            }
            Remove-Item -LiteralPath $tmp, $result -Force -ErrorAction SilentlyContinue
            $bar.Value++
            [System.Windows.Forms.Application]::DoEvents()
        }
    } finally {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
        $go.Enabled = $true
    }
    if ($failed.Count) {
        $status.Text = "$done copied to $musicDir. Couldn't convert: " + ($failed -join ", ")
    } else {
        $status.Text = "Done: $done song(s) copied to $musicDir. Eject the card safely before removing it."
        $list.Items.Clear()
    }
})

[void]$form.ShowDialog()
