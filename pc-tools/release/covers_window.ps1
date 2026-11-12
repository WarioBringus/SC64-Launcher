# SC64 Launcher - Get covers and game info. Opened by double-clicking "Get covers and game info.bat".
# A window around get_covers.py: you pick the SD card (it suggests drives that have the launcher on them), it checks
# that it really is the card and counts your games, says exactly where things will be saved, then runs the tool
# with the folder's own Python and shows its progress. Nothing to install.

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

# Runs a program and collects its output lines from another thread, for the window's timer to pick up.
Add-Type -TypeDefinition @"
using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Text;
public class Sc64Runner {
    public ConcurrentQueue<string> Lines = new ConcurrentQueue<string>();
    public Process Proc;
    public void Start(string exe, string args) {
        ProcessStartInfo si = new ProcessStartInfo(exe, args);
        si.UseShellExecute = false;
        si.CreateNoWindow = true;
        si.RedirectStandardOutput = true;
        si.RedirectStandardError = true;
        si.StandardOutputEncoding = Encoding.UTF8;
        si.StandardErrorEncoding = Encoding.UTF8;
        si.EnvironmentVariables["PYTHONIOENCODING"] = "utf-8";
        Proc = new Process();
        Proc.StartInfo = si;
        Proc.OutputDataReceived += (s, e) => { if (e.Data != null) Lines.Enqueue(e.Data); };
        Proc.ErrorDataReceived += (s, e) => { if (e.Data != null) Lines.Enqueue(e.Data); };
        Proc.Start();
        Proc.BeginOutputReadLine();
        Proc.BeginErrorReadLine();
    }
}
"@

$here = Split-Path -Parent $MyInvocation.MyCommand.Path          # tools\scripts
$tools = Split-Path -Parent $here
$python = Join-Path $tools "python\python.exe"
$script = Join-Path $here "get_covers.py"
$dataDir = Join-Path $tools "data"
$romExt = '^\.(z64|n64|v64)$'

$form = New-Object System.Windows.Forms.Form
$form.Text = "SC64 Launcher - Get covers and game info"
$form.Size = New-Object System.Drawing.Size(760, 720)
$form.MinimumSize = $form.Size
$form.StartPosition = "CenterScreen"
$form.Font = New-Object System.Drawing.Font("Segoe UI", 10)

function New-Label([string]$text, [int]$x, [int]$y, [int]$w, [int]$h) {
    $l = New-Object System.Windows.Forms.Label
    $l.Text = $text
    $l.Location = New-Object System.Drawing.Point($x, $y)
    $l.Size = New-Object System.Drawing.Size($w, $h)
    $l.Anchor = "Top, Left, Right"
    $form.Controls.Add($l)
    return $l
}

$intro = New-Label ("Adds box art, game details and covers for the games on your SD card.`n" +
    "Before this: put the launcher and your games on the SD card (steps 1 and 2 of the README).`n" +
    "Added or removed games later? Come back here and press the button again.") 16 12 710 64

# 1. The SD card
$step1 = New-Label "1.  Choose your SD card" 16 84 710 24
$step1.Font = New-Object System.Drawing.Font("Segoe UI", 11, [System.Drawing.FontStyle]::Bold)

$drives = New-Object System.Windows.Forms.ComboBox
$drives.DropDownStyle = "DropDownList"
$drives.Location = New-Object System.Drawing.Point(20, 114)
$drives.Size = New-Object System.Drawing.Size(430, 28)
$form.Controls.Add($drives)

$browse = New-Object System.Windows.Forms.Button
$browse.Text = "Browse..."
$browse.Location = New-Object System.Drawing.Point(460, 112)
$browse.Size = New-Object System.Drawing.Size(110, 32)
$form.Controls.Add($browse)

$refresh = New-Object System.Windows.Forms.Button
$refresh.Text = "Refresh"
$refresh.Location = New-Object System.Drawing.Point(580, 112)
$refresh.Size = New-Object System.Drawing.Size(110, 32)
$form.Controls.Add($refresh)

$check = New-Label "" 20 152 710 48
$games = New-Label "" 20 202 710 44

# 2. Where things go
$step2 = New-Label "2.  What will be saved, and where" 16 254 710 24
$step2.Font = $step1.Font
$where = New-Label "" 20 282 710 118

# 3. Go
$step3 = New-Label "3.  Get covers and game info" 16 404 710 24
$step3.Font = $step1.Font
$go = New-Object System.Windows.Forms.Button
$go.Text = "Get covers and game info"
$go.Location = New-Object System.Drawing.Point(20, 434)
$go.Size = New-Object System.Drawing.Size(260, 40)
$go.Enabled = $false
$form.Controls.Add($go)

$bar = New-Object System.Windows.Forms.ProgressBar
$bar.Location = New-Object System.Drawing.Point(294, 442)
$bar.Size = New-Object System.Drawing.Size(436, 24)
$bar.Anchor = "Top, Left, Right"
$form.Controls.Add($bar)

$status = New-Label "" 20 482 710 24

$log = New-Object System.Windows.Forms.TextBox
$log.Multiline = $true
$log.ReadOnly = $true
$log.ScrollBars = "Vertical"
$log.Font = New-Object System.Drawing.Font("Consolas", 9)
$log.Location = New-Object System.Drawing.Point(20, 510)
$log.Size = New-Object System.Drawing.Size(710, 156)
$log.Anchor = "Top, Bottom, Left, Right"
$form.Controls.Add($log)

$script:card = $null
$script:runner = $null

function Set-Card([string]$path) {
    $script:card = $null
    $go.Enabled = $false
    $games.Text = ""
    $where.Text = ""
    if (-not $path) {
        $check.ForeColor = [System.Drawing.Color]::DarkRed
        $check.Text = "Choose your SD card above (or click Browse and pick the top level of the card)."
        return
    }
    $root = $path.TrimEnd('\') + '\'
    if (-not (Test-Path -LiteralPath (Join-Path $root "sc64menu.n64"))) {
        $check.ForeColor = [System.Drawing.Color]::DarkRed
        $check.Text = "X  $root is not your SD card: sc64menu.n64 isn't there. Pick the top level of the card, where you " +
            "copied the README's ""SD card"" folder (step 1)."
        return
    }
    $kind = ""
    try {
        $info = New-Object System.IO.DriveInfo($root.Substring(0, 1))
        if ($info.DriveType -ne "Removable") { $kind = "  (Note: Windows lists this as a $($info.DriveType) drive, not a removable card.)" }
    } catch {}
    $check.ForeColor = [System.Drawing.Color]::DarkGreen
    $check.Text = "OK  This is your SD card: the launcher (sc64menu.n64) is at $root$kind"

    # The games, folder by folder (the folders directly on the card).
    $counts = @()
    $total = 0
    foreach ($dir in Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue) {
        if ($dir.Name -in @("menu", "System Volume Information")) { continue }
        $n = @(Get-ChildItem -LiteralPath $dir.FullName -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Extension -match $romExt }).Count
        if ($n -gt 0) { $counts += "$($dir.Name): $n"; $total += $n }
    }
    if ($total -eq 0) {
        $games.ForeColor = [System.Drawing.Color]::DarkRed
        $games.Text = "No games found on the card yet. Copy your games into its folders (N64 USA, N64 Japan...) first, " +
            "then click Refresh."
        return
    }
    $games.ForeColor = [System.Drawing.Color]::Black
    $games.Text = "Games found: $total   (" + ($counts -join ",  ") + ")"

    $where.Text = "Box art and game details  ->  ${root}menu\metadata`n" +
        "Covers the launcher uses  ->  ${root}menu\cache\launcher`n" +
        "Downloads kept on this PC for next time  ->  $dataDir`n" +
        "The first time, the complete box-art library (about 2,200 small images, ~60 MB) is copied to the card, so games " +
        "you add later already have art. Large covers are downloaded only for your own games. Nothing else on the card is changed."
    $script:card = $root
    $go.Enabled = $true
}

function Fill-Drives {
    $drives.Items.Clear()
    $withLauncher = @()
    foreach ($d in [System.IO.DriveInfo]::GetDrives()) {
        if (-not $d.IsReady -or $d.DriveType -eq "CDRom") { continue }
        $has = Test-Path -LiteralPath (Join-Path $d.RootDirectory "sc64menu.n64")
        $label = $d.Name + "   " + $d.VolumeLabel + $(if ($has) { "   (has the launcher on it)" } else { "" })
        $i = $drives.Items.Add($label)
        if ($has) { $withLauncher += $i }
    }
    # One drive with the launcher: suggested (still shown and checked below). Several or none: you choose.
    if ($withLauncher.Count -eq 1) { $drives.SelectedIndex = $withLauncher[0] } else { Set-Card $null }
}

$drives.Add_SelectedIndexChanged({
    if ($drives.SelectedIndex -ge 0) { Set-Card (($drives.SelectedItem -split '\s+')[0]) }
})
$refresh.Add_Click({
    if ($script:card) { Set-Card $script:card } else { Fill-Drives }
})
$browse.Add_Click({
    $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
    $dlg.Description = "Pick the top level of your SD card (where sc64menu.n64 is)."
    if ($dlg.ShowDialog() -eq "OK") { $drives.SelectedIndex = -1; Set-Card $dlg.SelectedPath }
})

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 200
$timer.Add_Tick({
    $r = $script:runner
    if (-not $r) { return }
    $line = $null
    $added = New-Object System.Text.StringBuilder
    while ($r.Lines.TryDequeue([ref]$line)) {
        if ($line -match '^@@ (\d+) (.*)$') {
            $bar.Value = [Math]::Min(100, [int]$Matches[1])
            $status.Text = $Matches[2]
        } else {
            [void]$added.AppendLine($line)
        }
    }
    if ($added.Length) { $log.AppendText($added.ToString()) }
    if ($r.Proc.HasExited -and $r.Lines.IsEmpty) {
        $timer.Stop()
        $code = $r.Proc.ExitCode
        $script:runner = $null
        $go.Enabled = $true; $drives.Enabled = $true; $browse.Enabled = $true; $refresh.Enabled = $true
        if ($code -eq 0) {
            $bar.Value = 100
            $status.ForeColor = [System.Drawing.Color]::DarkGreen
            $status.Text = "Done! Eject the SD card safely, put it in your SummerCart64 and turn the console on."
            [System.Windows.Forms.MessageBox]::Show(
                "Covers and game info are on your SD card.`n`nEject the card safely, put it in your SummerCart64 and turn " +
                "the console on.`n`nAdded or removed games later? Open this again and press the button.",
                "All done", "OK", "Information") | Out-Null
        } else {
            $status.ForeColor = [System.Drawing.Color]::DarkRed
            $status.Text = "Something went wrong (see the details below). Check your internet connection and the card, then try again."
        }
    }
})

$go.Add_Click({
    if (-not $script:card) { return }
    if (-not (Test-Path -LiteralPath $python)) {
        $status.ForeColor = [System.Drawing.Color]::DarkRed
        $status.Text = "tools\python is missing: keep the tools folder next to this program."
        return
    }
    $go.Enabled = $false; $drives.Enabled = $false; $browse.Enabled = $false; $refresh.Enabled = $false
    $log.Clear()
    $bar.Value = 0
    $status.ForeColor = [System.Drawing.Color]::Black
    $status.Text = "Starting..."
    $cardArg = $script:card.Replace('\', '/')       # "E:/": a trailing backslash would escape the closing quote
    $script:runner = New-Object Sc64Runner
    $script:runner.Start($python, "-u `"$script`" --card `"$cardArg`" --gui")
    $timer.Start()
})

$form.Add_FormClosing({
    if ($script:runner -and -not $script:runner.Proc.HasExited) {
        $answer = [System.Windows.Forms.MessageBox]::Show("Still working. Stop and close?", "Get covers and game info",
            "YesNo", "Question")
        if ($answer -ne "Yes") { $_.Cancel = $true } else { try { $script:runner.Proc.Kill() } catch {} }
    }
})

Fill-Drives
[void]$form.ShowDialog()
