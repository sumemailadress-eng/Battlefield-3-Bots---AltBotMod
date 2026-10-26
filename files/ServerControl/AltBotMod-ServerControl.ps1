# AltBotMod Server Control - one window for every BF3 server instance on this PC (the _StartServer*.bat files):
# what each is playing, change map / mode now or next, skip / restart / end the round, edit the rotation, bot count,
# bot difficulty, ticket %, chat, players, start / stop. Talks to each server over its own RCON port (127.0.0.1:479xx).
# Run it with Server-Control.bat (Windows PowerShell 5.1, no install).
param([string]$ServerDir, [switch]$Launch, [string]$Handoff)   # -Launch: start BF-Portable (one click) as the window opens
# AltBotMod V14: anything that stops this window before it opens goes to Launcher.log and a message box, not nowhere
# (the window's PowerShell is hidden, so an error there was never seen).
$script:StartupDone = $false
trap {
    if (-not $script:StartupDone) {
        $why = 'Server Control stopped while opening: ' + $_.Exception.Message + ' ' + ($_.InvocationInfo.PositionMessage -replace '\s+', ' ')
        try { [IO.File]::AppendAllText((Join-Path $PSScriptRoot 'Launcher.log'), (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + '  ' + $why + "`r`n") } catch { }
        try { Add-Type -AssemblyName System.Windows.Forms; [Windows.Forms.MessageBox]::Show($why, 'AltBotMod Server Control', 'OK', 'Error') | Out-Null } catch { }
        exit 1
    }
    break
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
. (Join-Path $PSScriptRoot 'ServerControl-Core.ps1')
. (Join-Path $PSScriptRoot 'BFPortable-Core.ps1')
. (Join-Path $PSScriptRoot 'LiveMap-Core.ps1')
. (Join-Path $PSScriptRoot 'Learning-Core.ps1')   # AltBotMod V21: what the bots learned (BotMod_learned)
. (Join-Path $PSScriptRoot 'ServerControl-Theme.ps1')   # AltBotMod V16.2: the BF3 look
Set-StrictMode -Off   # (WinForms event handlers)

$SettingsPath = Join-Path $PSScriptRoot 'ServerControl.json'
function Load-Settings { try { if (Test-Path -LiteralPath $SettingsPath) { return (Get-Content -Raw -LiteralPath $SettingsPath | ConvertFrom-Json) } } catch { }; return $null }
# Window preferences (server folder, one-click choices, Battlelog address), kept next to this script.
$script:Prefs = @{ ServerDir = ''; LaunchServers = @(1, 2, 3, 4); OpenBattlelog = $true; StartDns = $false; LaunchClient = $false; BattlelogUrl = '' }
$loaded = Load-Settings
if ($loaded) { foreach ($k in @($script:Prefs.Keys)) { if ($loaded.PSObject.Properties[$k] -and $null -ne $loaded.$k) { $script:Prefs[$k] = $loaded.$k } } }
function Save-Settings($dir) { if ($dir) { $script:Prefs.ServerDir = $dir }; try { $script:Prefs | ConvertTo-Json | Set-Content -LiteralPath $SettingsPath -Encoding UTF8 } catch { } }

# ---- which server ----
$dir = Resolve-BmServerDir $ServerDir
if (-not $dir) { $s = Load-Settings; if ($s -and $s.ServerDir) { $dir = Resolve-BmServerDir $s.ServerDir } }
if (-not $dir) { $dir = Find-BmServerDir $PSScriptRoot }
function Ask-ServerDir {
    $dlg = New-Object Windows.Forms.FolderBrowserDialog
    $dlg.Description = 'Where are your BF3 server files? Pick the folder with bf3_server_Final.exe (or the BF-Portable folder above it).'
    if ($dlg.ShowDialog() -ne 'OK') { return $null }
    $r = Resolve-BmServerDir $dlg.SelectedPath
    if (-not $r) { [Windows.Forms.MessageBox]::Show("No bf3_server_Final.exe in or under`r`n$($dlg.SelectedPath)", 'AltBotMod Server Control', 'OK', 'Warning') | Out-Null }
    return $r
}
if (-not $dir) { $dir = Ask-ServerDir }
if (-not $dir) { exit 1 }
Save-Settings $dir
# AltBotMod V14: the one click runs everything as administrator, as PortableBF.exe does (Launcher settings: AsAdmin).
$script:BpConfigPath = Join-Path $PSScriptRoot 'Launcher.json'
# V14 fix (2026-10-11): asked from the open window, not before it - a hidden window's request for administrator rights
# can end up as a flashing taskbar button instead of the Windows prompt, and the one click then sat waiting unseen.
# The new window says it has opened (Launcher.handoff); without that within 45 s this window carries on itself.
$script:HandoffPath = Join-Path $PSScriptRoot 'Launcher.handoff'; $script:HandoffWait = 45
function Restart-Elevated([bool]$withLaunch) {
    $token = [guid]::NewGuid().ToString('N')
    try { Remove-Item -LiteralPath $script:HandoffPath -Force -ErrorAction SilentlyContinue } catch { }
    $a = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-STA', '-File', ('"' + $PSCommandPath + '"'), '-ServerDir', ('"' + $dir + '"'), '-Handoff', $token)
    if ($withLaunch) { $a += '-Launch' }
    Write-BpLog 'Asking Windows for administrator rights (the one click runs everything as administrator, like PortableBF.exe)'
    try { Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -Verb RunAs -ArgumentList $a -WindowStyle Hidden }
    catch { Write-BpLog "Administrator rights not given ($($_.Exception.Message))"; return $false }
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.Elapsed.TotalSeconds -lt $script:HandoffWait) {
        try { if ((Test-Path -LiteralPath $script:HandoffPath) -and ([IO.File]::ReadAllText($script:HandoffPath)).Trim() -eq $token) { return $true } } catch { }
        try { [Windows.Forms.Application]::DoEvents() } catch { }; Start-Sleep -Milliseconds 200
    }
    Write-BpLog "The administrator window did not open within $($script:HandoffWait) s - carrying on in this window without administrator rights"
    return $false
}
$script:ElevationNote = ''
if ($Handoff) {   # the window opened with administrator rights: tell the one that asked
    try { [IO.File]::WriteAllText($script:HandoffPath, $Handoff) } catch { }
    Write-BpLog ('Administrator window open' + $(if (Test-BpElevated) { '' } else { ' - but without administrator rights' }))
}
$Instances = Get-BmInstances $dir
if (-not $Instances.Count) {
    [Windows.Forms.MessageBox]::Show("No _StartServer*.bat with a GAMEPORT and REMOTEADMINPORT in`r`n$dir", 'AltBotMod Server Control', 'OK', 'Warning') | Out-Null
    exit 1
}

# ---- connections (one per instance, re-made when a server comes back) ----
$Conn = @{}; $NextTry = @{}
function Get-Conn($inst) {
    $key = $inst.Number
    if ($Conn[$key]) { return $Conn[$key] }
    $now = [Environment]::TickCount
    if ($NextTry[$key] -and $now - $NextTry[$key] -lt 0) { return $null }
    $c = Connect-BmRcon $inst 600
    if ($c) { $Conn[$key] = $c } else { $NextTry[$key] = $now + 8000 }
    return $c
}
function Drop-Conn($inst) { Disconnect-BmRcon $Conn[$inst.Number]; $Conn[$inst.Number] = $null }
# Runs an RCON command; on a dead connection reconnects once.
function Rcon($inst, [string[]]$words) {
    $c = Get-Conn $inst
    if (-not $c) { return $null }
    $r = Invoke-BmRcon $c $words
    if ($null -eq $r) { Drop-Conn $inst; $NextTry[$inst.Number] = 0; $c = Get-Conn $inst; if ($c) { $r = Invoke-BmRcon $c $words } }
    return $r
}

# ---- look (AltBotMod V16.2: colours and fonts in ServerControl-Theme.ps1) ----

$form = New-Object Windows.Forms.Form
$form.Text = 'AltBotMod Server Control'
$form.Size = New-Object Drawing.Size(1040, 780)
$form.MinimumSize = New-Object Drawing.Size(1040, 780)
$form.StartPosition = 'CenterScreen'
$form.Font = $Font

$top = New-Object Windows.Forms.Label
$top.Text = "Server: $dir"; $top.AutoSize = $true; $top.Location = New-Object Drawing.Point(12, 60); $top.ForeColor = $Grey
$form.Controls.Add($top)
$banner = New-BmBanner "AltBotMod V34"; $form.Controls.Add($banner)   # AltBotMod V16.2
$status = New-Object Windows.Forms.StatusStrip
$statusText = New-Object Windows.Forms.ToolStripStatusLabel
$statusText.Text = 'Ready'
[void]$status.Items.Add($statusText)
$form.Controls.Add($status)
function Say-Status([string]$t, [bool]$bad = $false) { $statusText.Text = (Get-Date -Format 'HH:mm:ss') + '  ' + $t; $statusText.ForeColor = $(if ($bad) { $Red } else { $Fg }) }

$tabs = New-Object Windows.Forms.TabControl
$tabs.Location = New-Object Drawing.Point(10, 82); $tabs.Size = New-Object Drawing.Size(1004, 630); $tabs.Anchor = 'Top,Left,Right,Bottom'
$form.Controls.Add($tabs)

function New-Button($text, $x, $y, $w = 120, $h = 28) {
    $b = New-Object Windows.Forms.Button; $b.Text = $text; $b.Location = New-Object Drawing.Point($x, $y); $b.Size = New-Object Drawing.Size($w, $h); return $b
}
function New-Label($text, $x, $y, $font = $null) {
    $l = New-Object Windows.Forms.Label; $l.Text = $text; $l.AutoSize = $true; $l.Location = New-Object Drawing.Point($x, $y); if ($font) { $l.Font = $font }; return $l
}
function New-Group($text, $x, $y, $w, $h) {
    $g = New-Object Windows.Forms.GroupBox; $g.Text = $text; $g.Location = New-Object Drawing.Point($x, $y); $g.Size = New-Object Drawing.Size($w, $h); return $g
}
function Map-Display([string]$code, [string]$mode) {
    if (-not $code) { return '-' }
    return "{0} - {1}" -f (Get-BmMapName $code), (Get-BmModeName $mode)
}

# ---- BF-Portable tab: one-click start / stop of everything BF3 needs ----
$Root = Get-BpRoot $dir
$Services = @(Get-BpServices $dir)
$tabBp = New-Object Windows.Forms.TabPage; $tabBp.Text = 'BF-Portable'
$tabs.TabPages.Add($tabBp)
$bpStart = New-Button 'Start BF-Portable (one click)' 10 10 260 44; $bpStart.Font = $Bold
$bpStop = New-Button 'Stop everything' 280 10 160 44
$bpShortcut = New-Button 'Desktop shortcut (one click)' 450 10 220 44
$bpClient = New-Button 'Play: open BF Launcher' 680 10 150 44
$bpCfg = New-Button 'Launcher settings...' 836 10 150 44
$tabBp.Controls.AddRange(@($bpStart, $bpStop, $bpShortcut, $bpClient, $bpCfg))
$gOpt = New-Group 'One click starts' 10 62 976 64
$gOpt.Controls.Add((New-Label 'Redirector, database, web server, BF3 Blaze emulator, then servers:' 12 28))
$bpServerChecks = @{}; $x0 = 410
foreach ($inst in $Instances) {
    $cb = New-Object Windows.Forms.CheckBox; $cb.Text = $inst.Label; $cb.AutoSize = $true; $cb.Location = New-Object Drawing.Point($x0, 26)
    $cb.Checked = @($script:Prefs.LaunchServers) -contains $inst.Number; $bpServerChecks[$inst.Number] = $cb; $gOpt.Controls.Add($cb); $x0 += 95
}
$bpDns = New-Object Windows.Forms.CheckBox; $bpDns.Text = 'LAN DNS'; $bpDns.AutoSize = $true; $bpDns.Location = New-Object Drawing.Point(($x0 + 10), 26); $bpDns.Checked = [bool]$script:Prefs.StartDns
$bpWeb = New-Object Windows.Forms.CheckBox; $bpWeb.Text = 'then show Battlelog'; $bpWeb.AutoSize = $true; $bpWeb.Location = New-Object Drawing.Point(($x0 + 100), 26); $bpWeb.Checked = [bool]$script:Prefs.OpenBattlelog
$gOpt.Controls.AddRange(@($bpDns, $bpWeb))
$lvBp = New-Object Windows.Forms.ListView
$lvBp.View = 'Details'; $lvBp.FullRowSelect = $true; $lvBp.GridLines = $true
$lvBp.Location = New-Object Drawing.Point(10, 134); $lvBp.Size = New-Object Drawing.Size(976, 200); $lvBp.Anchor = 'Top,Left,Right'
foreach ($c in @(@('Service', 230), @('Status', 110), @('Port', 60), @('Program', 560))) { [void]$lvBp.Columns.Add($c[0], $c[1]) }
function Fill-ServiceList {
    $lvBp.BeginUpdate(); $lvBp.Items.Clear()
    foreach ($svc in $Services) {
        $it = New-Object Windows.Forms.ListViewItem($svc.Label + $(if (-not $svc.Enabled -and $svc.Key -ne 'dns') { '  (not in the one click)' } else { '' }))
        [void]$it.SubItems.Add(''); [void]$it.SubItems.Add([string]$svc.Port); [void]$it.SubItems.Add((($svc.Exe + ' ' + $svc.Args).Trim())); $it.Tag = $svc; [void]$lvBp.Items.Add($it)
    }
    foreach ($inst in $Instances) { $it = New-Object Windows.Forms.ListViewItem("BF3 server: $($inst.Label)"); [void]$it.SubItems.Add(''); [void]$it.SubItems.Add([string]$inst.GamePort); [void]$it.SubItems.Add($inst.Bat); $it.Tag = $inst; [void]$lvBp.Items.Add($it) }
    $lvBp.EndUpdate()
}
Fill-ServiceList
$tabBp.Controls.Add($lvBp)
$bpOne = New-Button 'Start selected' 10 340 140; $bpOneStop = New-Button 'Stop selected' 160 340 140
# AltBotMod V16.2: network (IP / DNS) and accounts (log in and join a server)
$bpNet = New-Button 'Network: IP and DNS...' 330 340 170; $bpAcc = New-Button 'Accounts and quick join...' 506 340 180
$bpLan = New-Button 'LAN servers...' 692 340 150; $bpJoin = New-Button 'Join server now' 848 340 138   # (V21.1: LAN servers)
$tabBp.Controls.AddRange(@($bpNet, $bpAcc, $bpLan, $bpJoin))
$tabBp.Controls.AddRange(@($bpOne, $bpOneStop))
$bpLog = New-Object Windows.Forms.TextBox; $bpLog.Multiline = $true; $bpLog.ReadOnly = $true; $bpLog.ScrollBars = 'Vertical'
$bpLog.Location = New-Object Drawing.Point(10, 376); $bpLog.Size = New-Object Drawing.Size(976, 205); $bpLog.Anchor = 'Top,Bottom,Left,Right'
$tabBp.Controls.Add($bpLog)
function Bp-Log([string]$t) { $bpLog.AppendText((Get-Date -Format 'HH:mm:ss') + '  ' + $t + "`r`n"); Write-BpLog $t }   # (V14: also BotMod\ServerControl\Launcher.log)

# ---- Battlelog tab: the BF-Portable Battlelog site inside this window ----
$tabWeb = New-Object Windows.Forms.TabPage; $tabWeb.Text = 'Battlelog'
$tabs.TabPages.Add($tabWeb)
$webBar = New-Object Windows.Forms.Panel; $webBar.Dock = 'Top'; $webBar.Height = 36
$wBack = New-Button '<' 4 4 34 28; $wFwd = New-Button '>' 40 4 34 28; $wReload = New-Button 'Reload' 78 4 70 28; $wHome = New-Button 'Home' 152 4 64 28
$wUrl = New-Object Windows.Forms.TextBox; $wUrl.Location = New-Object Drawing.Point(222, 8); $wUrl.Width = 430; $wUrl.Text = (Get-BpBattlelogUrl $script:Prefs.BattlelogUrl)
$wGo = New-Button 'Go' 658 4 44 28; $wEdge = New-Button 'Open in Edge window' 708 4 150 28; $wExt = New-Button 'Open in browser' 862 4 120 28
$webBar.Controls.AddRange(@($wBack, $wFwd, $wReload, $wHome, $wUrl, $wGo, $wEdge, $wExt))
$script:Web = $null
function Ensure-Web {
    if ($script:Web) { return $script:Web }
    [void](Set-BpBrowserMode ([IO.Path]::GetFileName([Diagnostics.Process]::GetCurrentProcess().MainModule.FileName)))
    $w = New-Object Windows.Forms.WebBrowser; $w.Dock = 'Fill'; $w.ScriptErrorsSuppressed = $true
    $w.Add_Navigated({ param($sender, $e) $wUrl.Text = [string]$e.Url })
    $tabWeb.Controls.Add($w); $w.BringToFront(); $script:Web = $w
    return $w
}
function Show-Battlelog([string]$url) {
    if (-not $url) { $url = Get-BpBattlelogUrl $script:Prefs.BattlelogUrl }
    $w = Ensure-Web; try { $w.Navigate($url) } catch { Say-Status "Battlelog: $($_.Exception.Message)" $true }
}
$tabWeb.Controls.Add($webBar)
$wBack.Add_Click({ if ($script:Web -and $script:Web.CanGoBack) { [void]$script:Web.GoBack() } })
$wFwd.Add_Click({ if ($script:Web -and $script:Web.CanGoForward) { [void]$script:Web.GoForward() } })
$wReload.Add_Click({ if ($script:Web) { $script:Web.Refresh() } else { Show-Battlelog $null } })
$wHome.Add_Click({ Show-Battlelog $null })
$wGo.Add_Click({ $t = $wUrl.Text.Trim(); if ($t -and $t -notmatch '^https?://') { $t = 'http://' + $t }
    if ($t -match '^https?://[^/]+/?$' -or $t -match '^https?://') { $script:Prefs.BattlelogUrl = $t; Save-Settings $null }; Show-Battlelog $t })
$wUrl.Add_KeyDown({ param($sender, $e) if ($e.KeyCode -eq 'Enter') { $e.SuppressKeyPress = $true; $wGo.PerformClick() } })
$wEdge.Add_Click({ $edge = Find-BpEdge; $url = $wUrl.Text
    if ($edge) { Start-Process -FilePath $edge -ArgumentList @("--app=$url", '--new-window') } else { Start-Process $url } })
$wExt.Add_Click({ Start-Process $wUrl.Text })

# ---- overview tab ----
$tabAll = New-Object Windows.Forms.TabPage; $tabAll.Text = 'All servers'
$tabs.TabPages.Add($tabAll)
$lvAll = New-Object Windows.Forms.ListView
$lvAll.View = 'Details'; $lvAll.FullRowSelect = $true; $lvAll.GridLines = $true
$lvAll.Location = New-Object Drawing.Point(10, 10); $lvAll.Size = New-Object Drawing.Size(976, 470); $lvAll.Anchor = 'Top,Left,Right,Bottom'
foreach ($c in @(@('Server', 90), @('Port', 60), @('Status', 80), @('Map', 170), @('Mode', 150), @('Players', 70), @('US / RU tickets', 120), @('Round', 70), @('Bots wanted', 90))) {
    [void]$lvAll.Columns.Add($c[0], $c[1])
}
foreach ($inst in $Instances) {
    $it = New-Object Windows.Forms.ListViewItem($inst.Label)
    foreach ($k in 1..8) { [void]$it.SubItems.Add('') }
    $it.SubItems[1].Text = [string]$inst.GamePort
    [void]$lvAll.Items.Add($it)
}
$tabAll.Controls.Add($lvAll)
$bAllNext = New-Button 'Next map on all' 10 495 150
$bAllRestart = New-Button 'Restart round on all' 170 495 150
$bAllStart = New-Button 'Start stopped servers' 330 495 160
$bAllSettings = New-Button 'Bot settings for all servers...' 500 495 230
$tabAll.Controls.AddRange(@($bAllNext, $bAllRestart, $bAllStart, $bAllSettings))
$tabAll.Controls.Add((New-Label 'Double-click a server to open its page. Map changes go through each server''s own RCON; bots follow the new map by themselves.' 10 535))

# ---- one tab per instance ----
$Ui = @{}
foreach ($inst in $Instances) {
    $page = New-Object Windows.Forms.TabPage; $page.Text = "$($inst.Label)  ($($inst.GamePort))"; $page.Tag = $inst.Number
    $tabs.TabPages.Add($page)
    $u = @{ Inst = $inst; Page = $page }

    $g1 = New-Group 'Now playing' 10 8 470 150
    $u.Status = New-Label 'checking...' 14 24 $Bold
    $u.Map = New-Label '-' 14 50 $Big
    $u.Info = New-Label '' 14 82
    $u.Score = New-Label '' 14 104
    $u.Start = New-Button 'Start server' 330 22 120
    $u.Stop = New-Button 'Stop server' 330 56 120
    $u.Setup = New-Button 'Server setup...' 330 90 120   # (AltBotMod V12: name, players, game rules)
    $g1.Controls.AddRange(@($u.Status, $u.Map, $u.Info, $u.Score, $u.Start, $u.Stop, $u.Setup))

    $g2 = New-Group 'Change map' 10 164 470 200
    $g2.Controls.Add((New-Label 'Map' 14 26))
    $u.MapBox = New-Object Windows.Forms.ComboBox; $u.MapBox.DropDownStyle = 'DropDownList'; $u.MapBox.Location = New-Object Drawing.Point(60, 22); $u.MapBox.Size = New-Object Drawing.Size(250, 24)
    foreach ($m in $script:BmMaps) { [void]$u.MapBox.Items.Add(('{0}   ({1})' -f $m.Name, $m.Code)) }
    $g2.Controls.Add((New-Label 'Mode' 14 60))
    $u.ModeBox = New-Object Windows.Forms.ComboBox; $u.ModeBox.DropDownStyle = 'DropDownList'; $u.ModeBox.Location = New-Object Drawing.Point(60, 56); $u.ModeBox.Size = New-Object Drawing.Size(250, 24)
    $u.PlayNow = New-Button 'Play now' 324 20 130
    $u.PlayNext = New-Button 'Play next' 324 54 130
    $u.Skip = New-Button 'Next map (skip)' 14 100 140
    $u.Restart = New-Button 'Restart round' 164 100 140
    $u.EndUS = New-Button 'End round: US win' 14 136 140
    $u.EndRU = New-Button 'End round: RU win' 164 136 140
    $u.ModeNote = New-Label '' 14 172; $u.ModeNote.ForeColor = $Grey
    $g2.Controls.AddRange(@($u.MapBox, $u.ModeBox, $u.PlayNow, $u.PlayNext, $u.Skip, $u.Restart, $u.EndUS, $u.EndRU, $u.ModeNote))

    $g3 = New-Group 'Rotation (the server''s map list)' 10 370 470 210; $u.RotGroup = $g3
    $u.Rot = New-Object Windows.Forms.ListBox; $u.Rot.Location = New-Object Drawing.Point(12, 22); $u.Rot.Size = New-Object Drawing.Size(320, 176)
    $u.RotAdd = New-Button 'Add chosen map' 342 20 116 28
    $u.RotDel = New-Button 'Remove' 342 50 116 28
    $u.RotUp = New-Button 'Move up' 342 80 116 28
    $u.RotDown = New-Button 'Move down' 342 110 116 28
    $u.RotGo = New-Button 'Play selected' 342 140 116 28
    $u.RotFill = New-Button 'Fill with...' 342 170 116 28   # (V14: every Conquest map, every Rush map, ...)
    $g3.Controls.AddRange(@($u.Rot, $u.RotAdd, $u.RotDel, $u.RotUp, $u.RotDown, $u.RotGo, $u.RotFill))

    $g4 = New-Group 'Bots and rules' 490 8 490 170
    $g4.Controls.Add((New-Label 'Bots' 14 28))
    $u.Bots = New-Object Windows.Forms.NumericUpDown; $u.Bots.Minimum = 0; $u.Bots.Maximum = 64; $u.Bots.Location = New-Object Drawing.Point(110, 24); $u.Bots.Width = 70
    $u.BotsApply = New-Button 'Set' 190 22 70 26
    $u.Settings = New-Button 'All bot settings...' 300 22 170 26   # (AltBotMod V11)
    $g4.Controls.Add((New-Label 'Difficulty' 14 62))
    $u.Diff = New-Object Windows.Forms.ComboBox; $u.Diff.DropDownStyle = 'DropDownList'; $u.Diff.Location = New-Object Drawing.Point(110, 58); $u.Diff.Width = 150
    foreach ($d in @('off (V8 combat)', 'easy', 'normal', 'hard', 'veteran', 'elite')) { [void]$u.Diff.Items.Add($d) }
    $u.DiffApply = New-Button 'Set' 270 56 70 26
    $g4.Controls.Add((New-Label 'Tickets %' 14 96))
    $u.Tickets = New-Object Windows.Forms.NumericUpDown; $u.Tickets.Minimum = 1; $u.Tickets.Maximum = 2000; $u.Tickets.Increment = 25; $u.Tickets.Value = 100; $u.Tickets.Location = New-Object Drawing.Point(110, 92); $u.Tickets.Width = 70
    $u.TicketsApply = New-Button 'Set' 190 90 70 26
    $g4.Controls.Add((New-Label '(vars.gameModeCounter - from the next round)' 268 96))
    $g4.Controls.Add((New-Label 'Say' 14 132))
    $u.SayBox = New-Object Windows.Forms.TextBox; $u.SayBox.Location = New-Object Drawing.Point(110, 128); $u.SayBox.Width = 280
    $u.SayGo = New-Button 'Send' 398 126 70 26
    $g4.Controls.AddRange(@($u.Bots, $u.BotsApply, $u.Settings, $u.Diff, $u.DiffApply, $u.Tickets, $u.TicketsApply, $u.SayBox, $u.SayGo))

    $g5 = New-Group 'Players' 490 184 490 396
    $u.Players = New-Object Windows.Forms.ListView; $u.Players.View = 'Details'; $u.Players.FullRowSelect = $true; $u.Players.GridLines = $true
    $u.Players.Location = New-Object Drawing.Point(10, 22); $u.Players.Size = New-Object Drawing.Size(468, 330)
    foreach ($c in @(@('Name', 170), @('Team', 50), @('Squad', 50), @('K', 40), @('D', 40), @('Score', 60), @('Ping', 45))) { [void]$u.Players.Columns.Add($c[0], $c[1]) }
    $u.Kick = New-Button 'Kick selected' 10 358 120 28
    $u.PlayerNote = New-Label '' 140 364; $u.PlayerNote.ForeColor = $Grey
    $g5.Controls.AddRange(@($u.Players, $u.Kick, $u.PlayerNote))

    $page.Controls.AddRange(@($g1, $g2, $g3, $g4, $g5))
    $u.RotEntries = @(); $u.LastMap = ''; $u.Online = $false
    $Ui[$inst.Number] = $u
}

# ---- the settings window: every AltBotMod setting for one server (its enhanced_<port>.ini) or for all of them
# (enhanced.ini). Values are checked before anything is written. ----
function Show-BmSettingsDialog($inst) {
    $title = $(if ($inst) { "AltBotMod settings - $($inst.Label) (port $($inst.GamePort))" } else { 'AltBotMod settings - all servers' })
    $f = New-Object Windows.Forms.Form
    $f.Text = $title; $f.Size = New-Object Drawing.Size(1000, 700); $f.MinimumSize = New-Object Drawing.Size(800, 500)
    $f.StartPosition = 'CenterParent'; $f.Font = $Font
    $note = New-Object Windows.Forms.Label; $note.AutoSize = $false; $note.Location = New-Object Drawing.Point(10, 8); $note.Size = New-Object Drawing.Size(965, 36); $note.Anchor = 'Top,Left,Right'
    $note.Text = $(if ($inst) { "Changes go into BotMod\enhanced_$($inst.GamePort).ini and apply to this server only. 'From' shows where a value comes from: this server, all servers (enhanced.ini) or the built-in default." } else { 'Changes go into BotMod\enhanced.ini and apply to every server (a server''s own settings still win for that server).' })
    $grid = New-Object Windows.Forms.DataGridView
    $grid.Location = New-Object Drawing.Point(10, 48); $grid.Size = New-Object Drawing.Size(965, 540); $grid.Anchor = 'Top,Bottom,Left,Right'
    $grid.AllowUserToAddRows = $false; $grid.AllowUserToDeleteRows = $false; $grid.RowHeadersVisible = $false
    $grid.SelectionMode = 'FullRowSelect'; $grid.EditMode = 'EditOnEnter'; $grid.AutoSizeRowsMode = 'None'
    foreach ($c in @(@('Group', 80), @('Setting', 200), @('Value', 110), @('From', 90), @('Range', 90), @('What it does', 380))) {
        $col = New-Object Windows.Forms.DataGridViewTextBoxColumn; $col.HeaderText = $c[0]; $col.Width = $c[1]; $col.SortMode = 'NotSortable'
        if ($c[0] -ne 'Value') { $col.ReadOnly = $true }
        [void]$grid.Columns.Add($col)
    }
    $grid.Columns[5].AutoSizeMode = 'Fill'
    $rows = Get-BmEffectiveSettings $dir $inst
    foreach ($r in $rows) {
        $s = $r.Setting
        $range = switch ($s.Kind) { 'bool' { 'on / off' } 'level' { 'off .. elite' } 'aggr' { '0 - 2' } default { "$($s.Min) - $($s.Max)" } }
        $shown = $r.Value
        if ($s.Kind -eq 'bool') { $shown = $(if ($r.Value -eq '1') { 'on' } else { 'off' }) }
        $n = $grid.Rows.Add(@($s.Group, $s.Label, $shown, $r.Source, $range, $s.Help))
        $row = $grid.Rows[$n]; $row.Tag = [pscustomobject]@{ Setting = $s; Original = $shown; Source = $r.Source }
        if ($s.Kind -eq 'bool' -or $s.Kind -eq 'level' -or $s.Kind -eq 'aggr') {
            $cell = New-Object Windows.Forms.DataGridViewComboBoxCell
            $items = switch ($s.Kind) { 'bool' { @('on', 'off') } 'level' { $script:BmLevels } default { @('0', '1', '2') } }
            foreach ($it in $items) { [void]$cell.Items.Add($it) }
            $cell.Value = $shown
            $row.Cells[2] = $cell
        }
        if ($r.Source -eq 'this server') { $row.Cells[3].Style.ForeColor = $Green }
    }
    $grid.Add_CellValueChanged({ param($sender, $e)
        if ($e.RowIndex -lt 0 -or $e.ColumnIndex -ne 2) { return }
        $row = $sender.Rows[$e.RowIndex]; $info = $row.Tag
        $ok = $null -ne (Test-BmSettingValue $info.Setting ([string]$row.Cells[2].Value))
        $row.Cells[2].Style.BackColor = $(if (-not $ok) { $BadCell } elseif ([string]$row.Cells[2].Value -ne $info.Original) { $ChangedCell } else { $Field })
    })
    $grid.Add_DataError({ param($sender, $e) $e.ThrowException = $false })   # (a value not in a drop-down list: no error box)
    $grid.Add_CurrentCellDirtyStateChanged({ param($sender, $e) if ($sender.IsCurrentCellDirty) { [void]$sender.CommitEdit('CurrentCellChange') } })
    $status = New-Object Windows.Forms.Label; $status.AutoSize = $false; $status.Location = New-Object Drawing.Point(10, 596); $status.Size = New-Object Drawing.Size(965, 20); $status.Anchor = 'Bottom,Left,Right'
    $bSave = New-Object Windows.Forms.Button; $bSave.Text = 'Save (applies at restart)'; $bSave.Location = New-Object Drawing.Point(10, 622); $bSave.Size = New-Object Drawing.Size(190, 30); $bSave.Anchor = 'Bottom,Left'
    $bApply = New-Object Windows.Forms.Button; $bApply.Text = 'Save and apply now'; $bApply.Location = New-Object Drawing.Point(210, 622); $bApply.Size = New-Object Drawing.Size(170, 30); $bApply.Anchor = 'Bottom,Left'
    $bReset = New-Object Windows.Forms.Button; $bReset.Text = $(if ($inst) { 'Selected: use the all-servers value' } else { 'Selected: back to the default' }); $bReset.Location = New-Object Drawing.Point(390, 622); $bReset.Size = New-Object Drawing.Size(250, 30); $bReset.Anchor = 'Bottom,Left'
    $bClose = New-Object Windows.Forms.Button; $bClose.Text = 'Close'; $bClose.Location = New-Object Drawing.Point(875, 622); $bClose.Size = New-Object Drawing.Size(100, 30); $bClose.Anchor = 'Bottom,Right'
    $f.Controls.AddRange(@($note, $grid, $status, $bSave, $bApply, $bReset, $bClose))
    $ctx = [pscustomobject]@{ Inst = $inst; Grid = $grid; Status = $status; Form = $f }
    foreach ($b in @($bSave, $bApply, $bReset, $bClose)) { $b.Tag = $ctx }
    $save = {
        param($c, [bool]$apply)
        [void]$c.Grid.EndEdit()
        $values = @{}; $bad = @()
        foreach ($row in $c.Grid.Rows) {
            $info = $row.Tag; $text = [string]$row.Cells[2].Value
            if ($text -eq $info.Original) { continue }
            $v = Test-BmSettingValue $info.Setting $text
            if ($null -eq $v) { $bad += $info.Setting.Label; continue }
            $values[$info.Setting.Key] = $v
        }
        if ($bad.Count) { $c.Status.ForeColor = $Red; $c.Status.Text = 'Not saved - check: ' + ($bad -join ', '); return }
        if (-not $values.Count) { $c.Status.ForeColor = $Fg; $c.Status.Text = 'Nothing changed.'; return }
        try { Set-BmSettings $dir $c.Inst $values } catch { $c.Status.ForeColor = $Red; $c.Status.Text = 'Could not write the settings: ' + $_.Exception.Message; return }
        $targets = $(if ($c.Inst) { @($c.Inst) } else { @($Instances) })
        if ($apply) { foreach ($t in $targets) { try { Send-BmSettingsReload $dir $t } catch { } } }
        foreach ($row in $c.Grid.Rows) {
            $info = $row.Tag
            if ($values.ContainsKey($info.Setting.Key)) {
                $info.Original = [string]$row.Cells[2].Value; $row.Cells[2].Style.BackColor = $Field
                $row.Cells[3].Value = $(if ($c.Inst) { 'this server' } else { 'all servers' }); if ($c.Inst) { $row.Cells[3].Style.ForeColor = $Green }
            }
        }
        $c.Status.ForeColor = $Green
        $c.Status.Text = "Saved $($values.Count) setting(s)" + $(if ($apply) { ' - the server(s) read them again now (some show at the next spawn or round).' } else { ' - they apply when the server(s) restart.' })
        if ($c.Inst) { Say-Status "$($c.Inst.Label): $($values.Count) bot setting(s) saved" } else { Say-Status "$($values.Count) bot setting(s) saved for all servers" }
    }
    $bSave.Add_Click({ & $save $this.Tag $false })   # ($save: this function's, live while the window is open)
    $bApply.Add_Click({ & $save $this.Tag $true })
    $bReset.Add_Click({
        $c = $this.Tag; $keys = @()
        foreach ($row in $c.Grid.SelectedRows) { $keys += $row.Tag.Setting.Key }
        if (-not $keys.Count) { $c.Status.Text = 'Select one or more rows first.'; return }
        try { Set-BmSettings $dir $c.Inst @{} $keys } catch { $c.Status.ForeColor = $Red; $c.Status.Text = $_.Exception.Message; return }
        $fresh = @{}; foreach ($r in (Get-BmEffectiveSettings $dir $c.Inst)) { $fresh[$r.Setting.Key] = $r }
        foreach ($row in $c.Grid.SelectedRows) {
            $r = $fresh[$row.Tag.Setting.Key]; $shown = $r.Value
            if ($r.Setting.Kind -eq 'bool') { $shown = $(if ($r.Value -eq '1') { 'on' } else { 'off' }) }
            $row.Tag.Original = $shown; $row.Cells[2].Value = $shown; $row.Cells[3].Value = $r.Source; $row.Cells[3].Style.ForeColor = $Fg
            $row.Cells[2].Style.BackColor = $Field
        }
        $c.Status.ForeColor = $Fg; $c.Status.Text = "$($keys.Count) setting(s) reset - they apply when the server(s) restart (or 'Save and apply now')."
    })
    $bClose.Add_Click({ $this.Tag.Form.Close() })
    Set-BmTheme $f; [void]$f.ShowDialog($form)
}

# ---- behaviour ----
function Selected-Map($u) {
    if ($u.MapBox.SelectedIndex -lt 0) { return $null }
    return $script:BmMaps[$u.MapBox.SelectedIndex].Code
}
function Selected-Mode($u) {
    if ($u.ModeBox.SelectedIndex -lt 0 -or -not $u.ModeCodes) { return $null }
    return $u.ModeCodes[$u.ModeBox.SelectedIndex]
}
function Fill-Modes($u, [string]$prefer) {
    $code = Selected-Map $u
    $u.ModeBox.Items.Clear(); $u.ModeCodes = @()
    if (-not $code) { return }
    $u.ModeCodes = Get-BmMapModes $code
    foreach ($m in $u.ModeCodes) { [void]$u.ModeBox.Items.Add((Get-BmModeName $m)) }
    $i = [Array]::IndexOf([string[]]$u.ModeCodes, $prefer)
    $u.ModeBox.SelectedIndex = $(if ($i -ge 0) { $i } else { 0 })
}
function Refresh-Rotation($u) {
    $list = Get-BmMapList (Get-Conn $u.Inst)
    $idx = Get-BmMapIndices (Get-Conn $u.Inst)
    $sel = $u.Rot.SelectedIndex
    $u.Rot.BeginUpdate(); $u.Rot.Items.Clear()
    foreach ($e in $list) {
        $mark = ''
        if ($idx -and $e.Index -eq $idx.Current) { $mark = '>  ' } elseif ($idx -and $e.Index -eq $idx.Next) { $mark = 'next  ' }
        [void]$u.Rot.Items.Add(('{0}{1}' -f $mark, (Map-Display $e.Map $e.Mode)))
    }
    $u.Rot.EndUpdate()
    if ($sel -ge 0 -and $sel -lt $u.Rot.Items.Count) { $u.Rot.SelectedIndex = $sel }
    $u.RotEntries = $list
}
function Refresh-Players($u) {
    $players = Get-BmPlayers (Get-Conn $u.Inst)
    $u.Players.BeginUpdate(); $u.Players.Items.Clear()
    $bots = 0
    foreach ($p in $players) {
        $name = [string]$p.name
        if ($name -like '`[BOT`]*') { $bots++ }
        $it = New-Object Windows.Forms.ListViewItem($name)
        foreach ($f in @('teamId', 'squadId', 'kills', 'deaths', 'score', 'ping')) {
            $v = ''; if ($p.PSObject.Properties[$f]) { $v = [string]$p.$f }
            if ($f -eq 'teamId') { $v = $(if ($v -eq '1') { 'US' } elseif ($v -eq '2') { 'RU' } else { $v }) }
            [void]$it.SubItems.Add($v)
        }
        [void]$u.Players.Items.Add($it)
    }
    $u.Players.EndUpdate()
    $u.PlayerNote.Text = "{0} players, {1} of them bots" -f $players.Count, $bots
}
function Refresh-Instance($u, [bool]$full) {
    $inst = $u.Inst
    $c = Get-Conn $inst
    $info = if ($c) { Get-BmServerInfo $c } else { $null }
    if (-not $info -and $c) { Drop-Conn $inst; $c = Get-Conn $inst; if ($c) { $info = Get-BmServerInfo $c } }
    $row = $lvAll.Items[$inst.Number - 1]
    if (-not $info) {
        $running = $null -ne (Get-BmServerProcess $inst)
        $u.Online = $false
        $u.Status.Text = $(if ($running) { 'Starting / not answering RCON yet' } else { 'Stopped' }); $u.Status.ForeColor = $(if ($running) { $Grey } else { $Red })
        $u.Map.Text = '-'; $u.Info.Text = "RCON $($inst.RconHost):$($inst.RconPort)"; $u.Score.Text = ''
        $row.SubItems[2].Text = $u.Status.Text; foreach ($k in 3..7) { $row.SubItems[$k].Text = '' }
        Refresh-RotationFile $u   # V14: the map list file - editable while the server is off
    } else {
        $u.Online = $true
        $u.Status.Text = 'Online - ' + $info.Name; $u.Status.ForeColor = $Green
        $u.Map.Text = Map-Display $info.Map $info.Mode
        $rt = [TimeSpan]::FromSeconds([Math]::Max(0, $info.RoundTime))
        $u.Info.Text = "Players {0}/{1}  -  round {2} of {3}  -  round time {4:mm\:ss}" -f $info.Players, $info.MaxPlayers, $info.Round, $info.Rounds, $rt
        $sc = ''
        if ($info.Scores.Count -ge 2) { $sc = 'US {0:0} - RU {1:0}' -f $info.Scores[0], $info.Scores[1] }
        $u.Score.Text = $(if ($sc) { "Tickets (game counter): $sc" } else { '' })
        $row.SubItems[2].Text = 'Online'; $row.SubItems[3].Text = (Get-BmMapName $info.Map); $row.SubItems[4].Text = (Get-BmModeName $info.Mode)
        $row.SubItems[5].Text = "$($info.Players)/$($info.MaxPlayers)"; $row.SubItems[6].Text = $sc; $row.SubItems[7].Text = ('{0:mm\:ss}' -f $rt)
        if ($full -or $u.LastMap -ne ($info.Map + $info.Mode)) {
            if ($u.MapBox.SelectedIndex -lt 0) {
                for ($k = 0; $k -lt $script:BmMaps.Count; $k++) { if ($script:BmMaps[$k].Code -ieq $info.Map) { $u.MapBox.SelectedIndex = $k } }
                Fill-Modes $u $info.Mode
            }
            Refresh-Rotation $u
            $gm = Get-BmVar (Get-Conn $inst) 'gameModeCounter'; $n = 0
            if ($gm -and [int]::TryParse($gm, [ref]$n) -and $n -ge 1 -and $n -le 2000) { $u.Tickets.Value = $n }
            $u.LastMap = $info.Map + $info.Mode
        }
        Refresh-Players $u
    }
    $bc = Get-BmBotCount $dir $inst
    if ($null -ne $bc) { $row.SubItems[8].Text = [string]$bc; if ($full) { $u.Bots.Value = [Math]::Max(0, [Math]::Min(64, $bc)) } }
    if ($full) {
        $d = Get-BmInstanceSetting $dir $inst 'Difficulty'
        $lv = 5; if ($d) { $names = @('off', 'easy', 'normal', 'hard', 'veteran', 'elite'); $j = [Array]::IndexOf($names, $d.ToLower()); if ($j -ge 0) { $lv = $j } elseif ($d -match '^[0-5]$') { $lv = [int]$d } }
        $u.Diff.SelectedIndex = $lv
    }
    $u.Start.Enabled = -not $u.Online; $u.Stop.Enabled = $true
    foreach ($b in @($u.PlayNow, $u.PlayNext, $u.Skip, $u.Restart, $u.EndUS, $u.EndRU, $u.RotGo, $u.TicketsApply, $u.SayGo, $u.Kick)) { $b.Enabled = $u.Online }
    $running = $u.Online -or $null -ne (Get-BmServerProcess $u.Inst)
    foreach ($b in @($u.RotAdd, $u.RotDel, $u.RotUp, $u.RotDown, $u.RotFill)) { $b.Enabled = $u.Online -or -not $running }   # (starting: neither the file nor RCON)
    $u.RotGroup.Text = $(if ($u.Online) { 'Rotation (the server''s map list)' } elseif ($running) { 'Rotation (the server is starting)' } else { 'Rotation - MapList.txt (server off: used at its next start)' })
}
function Move-Rotation($x, [int]$step) {
    $i = $x.Rot.SelectedIndex; $j = $i + $step
    if ($i -lt 0 -or $j -lt 0 -or $j -ge $x.RotEntries.Count) { return }
    $e = New-Object System.Collections.ArrayList
    foreach ($r in $x.RotEntries) { [void]$e.Add($r) }
    $tmp = $e[$i]; $e[$i] = $e[$j]; $e[$j] = $tmp
    Set-RotationEntries $x $e 'rotation reordered'
    if ($j -lt $x.Rot.Items.Count) { $x.Rot.SelectedIndex = $j }
}
# V14: online - the server's rotation over RCON (saved by the server); off - Instance<n>\Admin\MapList.txt.
function Set-RotationEntries($x, $entries, [string]$what) {
    if ($x.Online) { Run-Action $x $what { Set-BmRotation (Get-Conn $x.Inst) $entries } }
    else { Run-Action $x ($what + ' (MapList.txt - used at the next start)') { Write-BmMapListFile $x.Inst $entries; $null } }
}
function Refresh-RotationFile($u) {
    $list = Read-BmMapListFile $u.Inst
    $sel = $u.Rot.SelectedIndex
    $u.Rot.BeginUpdate(); $u.Rot.Items.Clear()
    foreach ($e in $list) { [void]$u.Rot.Items.Add((Map-Display $e.Map $e.Mode) + $(if ($e.Rounds -gt 1) { " x$($e.Rounds)" } else { '' })) }
    if (-not $list.Count) { [void]$u.Rot.Items.Add('(no MapList.txt yet - add maps, or Fill with...)') }
    $u.Rot.EndUpdate()
    if ($sel -ge 0 -and $sel -lt $list.Count) { $u.Rot.SelectedIndex = $sel }
    $u.RotEntries = $list
}
function Run-Action($u, [string]$what, [scriptblock]$action) {
    try {
        $err = & $action
        if ($err) { Say-Status "$($u.Inst.Label): $what failed - $err" $true }
        else { Say-Status "$($u.Inst.Label): $what" }
    } catch { Say-Status "$($u.Inst.Label): $what failed - $($_.Exception.Message)" $true }
    Refresh-Instance $u $true
}

foreach ($inst in $Instances) {
    $u = $Ui[$inst.Number]
    $u.MapBox.Tag = $u; $u.PlayNow.Tag = $u; $u.PlayNext.Tag = $u; $u.Skip.Tag = $u; $u.Restart.Tag = $u; $u.EndUS.Tag = $u; $u.EndRU.Tag = $u
    $u.RotAdd.Tag = $u; $u.RotDel.Tag = $u; $u.RotUp.Tag = $u; $u.RotDown.Tag = $u; $u.RotGo.Tag = $u; $u.BotsApply.Tag = $u; $u.DiffApply.Tag = $u
    $u.Settings.Tag = $u; $u.Settings.Add_Click({ Show-BmSettingsDialog $this.Tag.Inst; Refresh-Instance $this.Tag $true })
    $u.TicketsApply.Tag = $u; $u.SayGo.Tag = $u; $u.Kick.Tag = $u; $u.Start.Tag = $u; $u.Stop.Tag = $u; $u.ModeBox.Tag = $u
    $u.MapBox.Add_SelectedIndexChanged({ $x = $this.Tag; $prev = Selected-Mode $x; Fill-Modes $x $prev })
    $u.ModeBox.Add_SelectedIndexChanged({
        $x = $this.Tag; $m = Selected-Mode $x
        $x.ModeNote.Text = $(if ($m -eq 'RushLarge0') { 'Rush bots attack / defend M-COMs; works best with 20+ bots.' }
            elseif ($m -eq 'SquadRush0') { 'Squad Rush: one M-COM a sector, 4 a side. Levels without recorded M-COMs: bots play deathmatch-style.' }
            elseif ($m -like 'TeamDeathMatch*' -or $m -eq 'SquadDeathMatch0') { 'Deathmatch: bots hunt on the TDM routes.' }
            elseif ($m -eq 'GunMaster0') { 'Gun Master: bots hunt on the TDM routes; the game hands out the weapons.' }
            elseif ($m -eq 'CaptureTheFlag0') { 'Capture the Flag (experimental): a third of the bots raid the enemy base and run home, the rest hunt.' }
            elseif ($m -eq 'AirSuperiority0') { 'Air Superiority (untested): bots fly the jets they spawn in.' }
            elseif ($m -like 'ConquestAssault*') { 'Conquest Assault: defenders start on the flags (no HQ).' } else { '' })
    })
    $u.PlayNow.Add_Click({ $x = $this.Tag; $m = Selected-Map $x; $mo = Selected-Mode $x
        if (-not $m -or -not $mo) { Say-Status 'Pick a map and a mode first' $true; return }
        Run-Action $x ("now playing " + (Map-Display $m $mo)) { Set-BmMap (Get-Conn $x.Inst) $m $mo -Now } })
    $u.PlayNext.Add_Click({ $x = $this.Tag; $m = Selected-Map $x; $mo = Selected-Mode $x
        if (-not $m -or -not $mo) { Say-Status 'Pick a map and a mode first' $true; return }
        Run-Action $x ("next map: " + (Map-Display $m $mo)) { Set-BmMap (Get-Conn $x.Inst) $m $mo } })
    $u.Skip.Add_Click({ $x = $this.Tag; Run-Action $x 'skipped to the next map' { Invoke-BmSimple (Get-Conn $x.Inst) @('mapList.runNextRound') } })
    $u.Restart.Add_Click({ $x = $this.Tag; Run-Action $x 'round restarted' { Invoke-BmSimple (Get-Conn $x.Inst) @('mapList.restartRound') } })
    $u.EndUS.Add_Click({ $x = $this.Tag; Run-Action $x 'round ended (US win)' { Invoke-BmSimple (Get-Conn $x.Inst) @('mapList.endRound', '1') } })
    $u.EndRU.Add_Click({ $x = $this.Tag; Run-Action $x 'round ended (RU win)' { Invoke-BmSimple (Get-Conn $x.Inst) @('mapList.endRound', '2') } })
    $u.RotAdd.Add_Click({ $x = $this.Tag; $m = Selected-Map $x; $mo = Selected-Mode $x
        if (-not $m -or -not $mo) { Say-Status 'Pick a map and a mode first' $true; return }
        if (-not $x.Online) { $e = @($x.RotEntries) + @([pscustomobject]@{ Map = $m; Mode = $mo; Rounds = 1 }); Set-RotationEntries $x $e ("added " + (Map-Display $m $mo)); return }
        Run-Action $x ("added " + (Map-Display $m $mo) + ' to the rotation') {
            $e = Invoke-BmSimple (Get-Conn $x.Inst) @('mapList.add', $m, $mo, '1'); if ($e) { return $e }
            Invoke-BmSimple (Get-Conn $x.Inst) @('mapList.save') } })
    $u.RotDel.Add_Click({ $x = $this.Tag; $i = $x.Rot.SelectedIndex
        if ($i -lt 0) { Say-Status 'Select a rotation entry first' $true; return }
        if ($x.RotEntries.Count -le 1) { Say-Status 'The rotation needs at least one map' $true; return }
        if (-not $x.Online) { $e = New-Object System.Collections.ArrayList; foreach ($r in $x.RotEntries) { [void]$e.Add($r) }; $e.RemoveAt($i); Set-RotationEntries $x $e 'removed from the rotation'; return }
        Run-Action $x 'removed from the rotation' {
            $e = Invoke-BmSimple (Get-Conn $x.Inst) @('mapList.remove', [string]$i); if ($e) { return $e }
            Invoke-BmSimple (Get-Conn $x.Inst) @('mapList.save') } })
    $u.RotFill.Tag = $u
    $u.RotFill.Add_Click({
        $x = $this.Tag; $menu = New-Object Windows.Forms.ContextMenuStrip
        foreach ($k in @(@('conquest', 'every Conquest map (Large; Assault on the assault maps)'), @('conquestsmall', 'every Conquest map (small)'), @('rush', 'every Rush map'),
                         @('tdm', 'every Team Deathmatch map'), @('sqdm', 'every Squad Deathmatch map'), @('domination', 'every close-quarters map (Conquest Domination)'))) {
            $mi = $menu.Items.Add($k[1]); $mi.Tag = @($x, $k[0], $k[1])
            $mi.Add_Click({ $t = $this.Tag; $list = Get-BmPresetRotation $t[1]
                $ok = [Windows.Forms.MessageBox]::Show("Replace $($t[0].Inst.Label)'s rotation with $($t[2]) ($($list.Count) maps)?", 'Fill the rotation', 'YesNo', 'Question')
                if ($ok -eq 'Yes') { Set-RotationEntries $t[0] $list "rotation: $($t[2])" } })
        }
        $menu.Show($this, (New-Object Drawing.Point(0, $this.Height)))
    })
    $u.RotUp.Add_Click({ Move-Rotation $this.Tag -1 })
    $u.RotDown.Add_Click({ Move-Rotation $this.Tag 1 })
    $u.RotGo.Add_Click({ $x = $this.Tag; $i = $x.Rot.SelectedIndex
        if ($i -lt 0) { Say-Status 'Select a rotation entry first' $true; return }
        Run-Action $x 'playing the selected rotation entry' {
            $e = Invoke-BmSimple (Get-Conn $x.Inst) @('mapList.setNextMapIndex', [string]$i); if ($e) { return $e }
            Invoke-BmSimple (Get-Conn $x.Inst) @('mapList.runNextRound') } })
    $u.BotsApply.Add_Click({ $x = $this.Tag; $n = [int]$x.Bots.Value
        Run-Action $x "bot count set to $n" { Set-BmBotCount $dir $x.Inst $n; $null } })
    $u.DiffApply.Add_Click({ $x = $this.Tag; $names = @('off', 'easy', 'normal', 'hard', 'veteran', 'elite'); $lv = $names[[Math]::Max(0, $x.Diff.SelectedIndex)]
        Run-Action $x "bot difficulty set to $lv (kept in BotMod\enhanced_$($x.Inst.GamePort).ini)" {
            Set-BmInstanceSetting $dir $x.Inst 'Difficulty' $lv; Send-BmBotCommand $dir $x.Inst "difficulty $lv"; $null } })
    $u.TicketsApply.Add_Click({ $x = $this.Tag; $n = [int]$x.Tickets.Value
        Run-Action $x "tickets set to $n% (from the next round)" { if (Set-BmVar (Get-Conn $x.Inst) 'gameModeCounter' ([string]$n)) { $null } else { 'refused' } } })
    $u.SayGo.Add_Click({ $x = $this.Tag; $t = $x.SayBox.Text.Trim(); if (-not $t) { return }
        Run-Action $x 'message sent' { Invoke-BmSimple (Get-Conn $x.Inst) @('admin.say', $t, 'all') }; $x.SayBox.Text = '' })
    $u.Kick.Add_Click({ $x = $this.Tag; if ($x.Players.SelectedItems.Count -lt 1) { Say-Status 'Select a player first' $true; return }
        $name = $x.Players.SelectedItems[0].Text
        Run-Action $x "kicked $name" { Invoke-BmSimple (Get-Conn $x.Inst) @('admin.kickPlayer', $name, 'Kicked by the server admin') } })
    $u.Start.Add_Click({ $x = $this.Tag; Start-BmServer $x.Inst; Say-Status "$($x.Inst.Label): starting ($([IO.Path]::GetFileName($x.Inst.Bat)))" })
    $u.Stop.Add_Click({ $x = $this.Tag
        $ok = [Windows.Forms.MessageBox]::Show("Stop $($x.Inst.Label) (port $($x.Inst.GamePort))? Everyone on it is disconnected.", 'AltBotMod Server Control', 'YesNo', 'Question')
        if ($ok -ne 'Yes') { return }
        Drop-Conn $x.Inst
        if (Stop-BmServer $x.Inst) { Say-Status "$($x.Inst.Label): stopped" } else { Say-Status "$($x.Inst.Label): no running server found on port $($x.Inst.GamePort)" $true }
        Refresh-Instance $x $true })
}
$bAllNext.Add_Click({ foreach ($inst in $Instances) { $x = $Ui[$inst.Number]; if ($x.Online) { Run-Action $x 'next map' { Invoke-BmSimple (Get-Conn $x.Inst) @('mapList.runNextRound') } } } })
$bAllRestart.Add_Click({ foreach ($inst in $Instances) { $x = $Ui[$inst.Number]; if ($x.Online) { Run-Action $x 'round restarted' { Invoke-BmSimple (Get-Conn $x.Inst) @('mapList.restartRound') } } } })
$bAllStart.Add_Click({ foreach ($inst in $Instances) { if (-not (Get-BmServerProcess $inst -Fresh)) { Start-BmServer $inst; Say-Status "$($inst.Label): starting" } } })
$bAllSettings.Add_Click({ Show-BmSettingsDialog $null })
$lvAll.Add_DoubleClick({ if ($lvAll.SelectedIndices.Count) { $tabs.SelectedTab = $Ui[$Instances[$lvAll.SelectedIndices[0]].Number].Page } })

# ---- BF-Portable: service status, one click, shortcut, client ----
function Refresh-Services {
    foreach ($it in $lvBp.Items) {
        $t = $it.Tag
        if ($t.PSObject.Properties['Proc']) { $s = Get-BpState $t $Root }
        else {
            $s = $(if ($Ui[$t.Number].Online) { 'running' } elseif (Get-BmServerProcess $t) { 'starting' } else { 'stopped' })
        }
        $it.SubItems[1].Text = $s
        $it.ForeColor = $(if ($s -eq 'running') { $Green } elseif ($s -eq 'starting') { $Grey } elseif ($s -eq 'missing') { $Red } else { $Fg })
    }
}
$script:Launch = $null
$launchTimer = New-Object Windows.Forms.Timer; $launchTimer.Interval = 500
$launchClock = [Diagnostics.Stopwatch]::StartNew()
function Start-OneClick {
    if ($script:Launch -and -not $script:Launch.Done) { Say-Status 'BF-Portable is already starting'; return }
    $servers = @(); foreach ($k in $bpServerChecks.Keys) { if ($bpServerChecks[$k].Checked) { $servers += [int]$k } }
    $script:Prefs.LaunchServers = $servers; $script:Prefs.StartDns = $bpDns.Checked; $script:Prefs.OpenBattlelog = $bpWeb.Checked; Save-Settings $null
    $cfg = Read-BpConfig $dir
    if ($cfg.AsAdmin -and -not (Test-BpElevated) -and -not $script:ElevationNote -and -not $Handoff) {   # V14: as PortableBF.exe - everything as administrator
        Say-Status 'Asking Windows for administrator rights - answer its prompt (if it does not show, look for a flashing shield in the taskbar)'
        [Windows.Forms.Application]::DoEvents()
        $bpStart.Enabled = $false
        if (Restart-Elevated $true) { Save-Settings $null; $form.Close(); return }
        $bpStart.Enabled = $true
        $script:ElevationNote = 'Administrator rights were not given - parts that need them ask one by one (Launcher settings).'
    }
    $script:Launch = New-BpLaunch $dir $Instances $servers $bpDns.Checked $cfg
    $tabs.SelectedTab = $tabBp; $bpStart.Enabled = $false
    Bp-Log ('One click: ' + $(if ($servers.Count) { "servers $($servers -join ', ')" } else { 'no game servers ticked' }) + $(if (Test-BpElevated) { ' (as administrator)' } else { ' (without administrator rights)' }))
    if ($script:ElevationNote) { Bp-Log $script:ElevationNote }
    $launchTimer.Start()
}
$launchTimer.Add_Tick({
    $l = $script:Launch
    if (-not $l) { $launchTimer.Stop(); return }
    $m = Step-BpLaunch $l $launchClock.Elapsed.TotalSeconds
    if ($m) { Bp-Log $m; Refresh-Services }
    if ($l.Done) {
        $launchTimer.Stop(); $bpStart.Enabled = $true
        Say-Status $(if ($l.Failed) { 'BF-Portable: started with problems - see the BF-Portable page' } else { 'BF-Portable is up' }) $l.Failed
        if ($bpWeb.Checked -and $l.Ready['apache']) { $tabs.SelectedTab = $tabWeb; Show-Battlelog $null }
        if (-not $l.Failed -and (Read-BpAccounts).AutoJoin) { Bp-Log 'Quick join: waiting for the server to be listed...'; $script:AutoJoinUntil = [DateTime]::Now.AddMinutes(3); $autoJoinTimer.Start() }   # V16.2
    }
})
$bpStart.Add_Click({ Start-OneClick })
$bpStop.Add_Click({
    $ok = [Windows.Forms.MessageBox]::Show('Stop all BF3 servers and the BF-Portable services (web server, database, emulator, redirector)?', 'Stop everything', 'YesNo', 'Question')
    if ($ok -ne 'Yes') { return }
    if ($script:Launch) { $script:Launch.Done = $true; $launchTimer.Stop(); $bpStart.Enabled = $true }
    foreach ($inst in $Instances) { Drop-Conn $inst }
    Stop-BpAll $dir $Instances
    Bp-Log 'Stopped the servers and services'; Refresh-Services
})
$bpOne.Add_Click({
    foreach ($it in $lvBp.SelectedItems) {
        $t = $it.Tag
        if ($t.PSObject.Properties['Proc']) { $e = Start-BpService $t $Root; Bp-Log $(if ($e) { "$($t.Label): $e" } else { "Starting $($t.Label)" }) }
        elseif (-not (Get-BmServerProcess $t -Fresh)) { Start-BmServer $t; Bp-Log "Starting $($t.Label)" }
    }
    Refresh-Services
})
$bpOneStop.Add_Click({
    foreach ($it in $lvBp.SelectedItems) {
        $t = $it.Tag
        if ($t.PSObject.Properties['Proc']) { Stop-BpService $t $Root; Bp-Log "Stopped $($t.Label)" }
        else { Drop-Conn $t; if (Stop-BmServer $t) { Bp-Log "Stopped $($t.Label)" } }
    }
    Refresh-Services
})
$bpShortcut.Add_Click({
    try {
        $sh = New-Object -ComObject WScript.Shell
        $lnk = $sh.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'BF-Portable (AltBotMod).lnk'))
        $lnk.TargetPath = Join-Path $PSScriptRoot 'Start-BF-Portable.bat'
        $lnk.WorkingDirectory = $PSScriptRoot
        $icon = Join-Path $Root 'PortableBF.exe'; if (Test-Path -LiteralPath $icon) { $lnk.IconLocation = "$icon,0" }
        $lnk.Description = 'Starts BF-Portable for BF3 (web server, database, emulator, servers) and opens Server Control'
        $lnk.Save()
        Bp-Log 'Desktop shortcut "BF-Portable (AltBotMod)" made: double-click it to start everything'
    } catch { Bp-Log "Could not make the shortcut: $($_.Exception.Message)" }
})
$bpClient.Add_Click({
    $exe = Get-BpClient $dir
    if (Test-Path -LiteralPath $exe) { Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe); Bp-Log 'Opened the BF Launcher' }
    else { Bp-Log "BF Launcher not found: $exe" }
})

# ---- AltBotMod V14: Launcher settings - every part of the one click (program, arguments, folder, port, wait, window,
# administrator, required, typed answers), the LAN DNS name / IP and the servers' spacing. Saved in Launcher.json. ----
function Show-BpLauncherDialog {
    $cfg = Read-BpConfig $dir
    $f = New-Object Windows.Forms.Form; $f.Text = 'Launcher settings - what the one click starts'; $f.Size = New-Object Drawing.Size(1180, 560); $f.StartPosition = 'CenterParent'; $f.Font = $Font
    $top = New-Object Windows.Forms.Panel; $top.Dock = 'Top'; $top.Height = 92
    $cAdmin = New-Object Windows.Forms.CheckBox; $cAdmin.Text = 'Run the one click as administrator (as PortableBF.exe does - Windows asks once)'; $cAdmin.AutoSize = $true; $cAdmin.Location = New-Object Drawing.Point(10, 8); $cAdmin.Checked = [bool]$cfg.AsAdmin
    $top.Controls.Add($cAdmin)
    $top.Controls.Add((New-Label 'LAN DNS domain name' 10 40)); $tDom = New-Object Windows.Forms.TextBox; $tDom.Location = New-Object Drawing.Point(150, 37); $tDom.Width = 200; $tDom.Text = [string]$cfg.DnsDomain
    $top.Controls.Add((New-Label 'LAN IP (this PC)' 370 40)); $tIp = New-Object Windows.Forms.TextBox; $tIp.Location = New-Object Drawing.Point(480, 37); $tIp.Width = 130; $tIp.Text = [string]$cfg.DnsIp
    $top.Controls.Add((New-Label 'Seconds between servers' 630 40)); $nGap = New-Object Windows.Forms.NumericUpDown; $nGap.Location = New-Object Drawing.Point(790, 37); $nGap.Width = 60; $nGap.Minimum = 0; $nGap.Maximum = 120; $nGap.Value = [int]$cfg.ServerGapSeconds
    $top.Controls.Add((New-Label 'Server windows' 870 40)); $cWin = New-Object Windows.Forms.ComboBox; $cWin.DropDownStyle = 'DropDownList'; $cWin.Location = New-Object Drawing.Point(970, 37); $cWin.Width = 110
    foreach ($w in @('Normal', 'Minimized', 'Hidden')) { [void]$cWin.Items.Add($w) }; $cWin.SelectedItem = $cfg.ServerWindow
    $top.Controls.AddRange(@($tDom, $tIp, $nGap, $cWin))
    $top.Controls.Add((New-Label 'Paths are relative to the BF-Portable folder. "Typed answers": lines typed into the program as it starts ({domain}, {ip}). "Required": the one click stops if it does not come up.' 10 68))
    $grid = New-Object Windows.Forms.DataGridView; $grid.Dock = 'Fill'; $grid.AllowUserToAddRows = $false; $grid.AllowUserToDeleteRows = $false; $grid.RowHeadersVisible = $false
    $grid.SelectionMode = 'FullRowSelect'; $grid.MultiSelect = $false
    function Col($type, $name, $head, $w) { $c = New-Object $type; $c.Name = $name; $c.HeaderText = $head; $c.Width = $w; [void]$grid.Columns.Add($c); return $c }
    [void](Col Windows.Forms.DataGridViewCheckBoxColumn 'On' 'Start' 42)
    $cPart = Col Windows.Forms.DataGridViewTextBoxColumn 'Part' 'Part' 190; $cPart.ReadOnly = $true
    [void](Col Windows.Forms.DataGridViewTextBoxColumn 'Exe' 'Program' 230)
    [void](Col Windows.Forms.DataGridViewTextBoxColumn 'Args' 'Arguments' 170)
    [void](Col Windows.Forms.DataGridViewTextBoxColumn 'WorkDir' 'Folder' 90)
    [void](Col Windows.Forms.DataGridViewTextBoxColumn 'Proc' 'Process name' 120)
    [void](Col Windows.Forms.DataGridViewTextBoxColumn 'Port' 'Wait for port' 70)
    [void](Col Windows.Forms.DataGridViewTextBoxColumn 'Wait' 'Wait (s)' 52)
    $cw = Col Windows.Forms.DataGridViewComboBoxColumn 'Window' 'Window' 82; foreach ($w in @('Normal', 'Minimized', 'Hidden')) { [void]$cw.Items.Add($w) }
    [void](Col Windows.Forms.DataGridViewCheckBoxColumn 'Admin' 'Admin' 46)
    [void](Col Windows.Forms.DataGridViewCheckBoxColumn 'Required' 'Required' 58)
    [void](Col Windows.Forms.DataGridViewTextBoxColumn 'Input' 'Typed answers' 120)
    $fill = {
        param($c)
        $grid.Rows.Clear()
        foreach ($pt in $c.Parts) {
            $on = $(if ($pt.Key -eq 'dns') { $bpDns.Checked } else { [bool]$pt.Enabled })
            $n = $grid.Rows.Add(@($on, $pt.Label, $pt.Exe, [string]$pt.Args, $pt.WorkDir, $pt.Proc, [string]$pt.Port, [string]$pt.Wait, $pt.Window, [bool]$pt.Admin, [bool]$pt.Required, (([string]$pt.Input) -replace "`r?`n", ' | ')))
            $grid.Rows[$n].Tag = $pt.Key
        }
    }
    & $fill $cfg
    $bar = New-Object Windows.Forms.Panel; $bar.Dock = 'Bottom'; $bar.Height = 44
    $bSave = New-Button 'Save' 10 8 100; $bDef = New-Button 'Defaults' 120 8 100; $bBrowse = New-Button 'Program for the selected part...' 230 8 220
    $bTest = New-Button 'Start the selected part now' 460 8 200; $bLog = New-Button 'Open the launcher log' 670 8 170; $bClose = New-Button 'Close' 1060 8 90
    $bar.Controls.AddRange(@($bSave, $bDef, $bBrowse, $bTest, $bLog, $bClose))
    $f.Controls.Add($grid); $f.Controls.Add($bar); $f.Controls.Add($top); $grid.BringToFront()
    $collect = {
        $c = Read-BpConfig $dir
        $c.AsAdmin = $cAdmin.Checked; $c.DnsDomain = $tDom.Text.Trim(); $c.DnsIp = $tIp.Text.Trim(); $c.ServerGapSeconds = [int]$nGap.Value; $c.ServerWindow = [string]$cWin.SelectedItem
        foreach ($r in $grid.Rows) {
            $pt = $c.Parts | Where-Object { $_.Key -eq $r.Tag } | Select-Object -First 1; if (-not $pt) { continue }
            $v = $r.Cells
            $pt.Enabled = [bool]$v['On'].Value; $pt.Exe = [string]$v['Exe'].Value; $pt.Args = [string]$v['Args'].Value; $pt.WorkDir = [string]$v['WorkDir'].Value
            $pt.Proc = ([string]$v['Proc'].Value) -replace '\.exe$', ''
            $port = 0; $pt.Port = $(if ([int]::TryParse([string]$v['Port'].Value, [ref]$port) -and $port -gt 0 -and $port -lt 65536) { $port } else { $null })
            $wt = 0; $pt.Wait = $(if ([int]::TryParse([string]$v['Wait'].Value, [ref]$wt) -and $wt -ge 1 -and $wt -le 600) { $wt } else { 30 })
            $pt.Window = [string]$v['Window'].Value; $pt.Admin = [bool]$v['Admin'].Value; $pt.Required = [bool]$v['Required'].Value
            $pt.Input = (([string]$v['Input'].Value) -split '\s*\|\s*' | Where-Object { $_ }) -join "`n"
        }
        return $c
    }
    $bSave.Add_Click({
        $c = & $collect
        try { Save-BpConfig $c } catch { [void][Windows.Forms.MessageBox]::Show("Could not save: $($_.Exception.Message)"); return }
        $dnsRow = $c.Parts | Where-Object { $_.Key -eq 'dns' }; if ($dnsRow) { $bpDns.Checked = [bool]$dnsRow.Enabled; $script:Prefs.StartDns = $bpDns.Checked; Save-Settings $null }
        $script:Services = @(Get-BpServices $dir $c); Fill-ServiceList; Refresh-Services
        $missing = @($script:Services | Where-Object { ($_.Enabled -or ($_.Key -eq 'dns' -and $bpDns.Checked)) -and -not (Test-Path -LiteralPath $_.Exe) } | ForEach-Object { $_.Label })
        Say-Status $(if ($missing.Count) { "Launcher settings saved - not found: $($missing -join ', ')" } else { 'Launcher settings saved' }) ($missing.Count -gt 0)
    })
    $bDef.Add_Click({ $d = Get-BpDefaultConfig $dir; $cAdmin.Checked = $d.AsAdmin; $nGap.Value = $d.ServerGapSeconds; $cWin.SelectedItem = $d.ServerWindow; if (-not $tIp.Text) { $tIp.Text = $d.DnsIp }; & $fill $d })
    $bBrowse.Add_Click({
        if (-not $grid.SelectedRows.Count) { return }
        $o = New-Object Windows.Forms.OpenFileDialog; $o.Filter = 'Programs (*.exe;*.bat;*.cmd)|*.exe;*.bat;*.cmd|All files (*.*)|*.*'; $o.InitialDirectory = $Root
        if ($o.ShowDialog() -ne 'OK') { return }
        $pth = $o.FileName; if ($pth.StartsWith($Root + '\', [StringComparison]::OrdinalIgnoreCase)) { $pth = $pth.Substring($Root.Length + 1) }
        $r = $grid.SelectedRows[0]; $r.Cells['Exe'].Value = $pth; $r.Cells['WorkDir'].Value = Split-Path -Parent $pth; $r.Cells['Proc'].Value = [IO.Path]::GetFileNameWithoutExtension($pth)
    })
    $bTest.Add_Click({
        if (-not $grid.SelectedRows.Count) { return }
        $c = & $collect; $svc = @(Get-BpServices $dir $c) | Where-Object { $_.Key -eq $grid.SelectedRows[0].Tag } | Select-Object -First 1
        if (-not $svc) { return }
        $e = Start-BpService $svc $Root
        Bp-Log ("Test start of $($svc.Label): " + $(if ($e) { $e } else { 'started (or already running) - see its status on the BF-Portable page' }))
        Say-Status "$($svc.Label): $(if ($e) { $e } else { 'started' })" ([bool]$e -and $e -notlike 'started*')
    })
    $bLog.Add_Click({ $lp = Join-Path $PSScriptRoot 'Launcher.log'; if (Test-Path -LiteralPath $lp) { Start-Process notepad.exe $lp } else { Say-Status 'No launcher log yet' } })
    $bClose.Add_Click({ $f.Close() })
    Set-BmTheme $f; [void]$f.ShowDialog($form)
}
$bpCfg.Add_Click({ Show-BpLauncherDialog })

# ---- AltBotMod V16.2: Network - one IP for the redirector, its config, the LAN DNS and the BF Launcher; the DNS answers
# saved once (the LAN DNS server then starts with the one click and is answered automatically) ----
function Show-BpNetworkDialog {
    $cfg = Read-BpConfig $dir
    $f = New-Object Windows.Forms.Form; $f.Text = 'Network - IP and LAN DNS'; $f.Size = New-Object Drawing.Size(760, 470); $f.StartPosition = 'CenterParent'; $f.Font = $Font; $f.FormBorderStyle = 'FixedDialog'; $f.MaximizeBox = $false
    $g1 = New-Group 'This PC''s LAN address' 12 10 720 230
    $g1.Controls.Add((New-Label 'IP address' 14 32)); $cIp = New-Object Windows.Forms.ComboBox; $cIp.Location = New-Object Drawing.Point(120, 28); $cIp.Width = 170
    foreach ($a in (Get-BpLocalIps)) { [void]$cIp.Items.Add($a) }; [void]$cIp.Items.Add('127.0.0.1')
    $targets = @(Get-BpIpTargets $dir $cfg)
    $cIp.Text = $(if ($cfg.DnsIp) { [string]$cfg.DnsIp } elseif ($cIp.Items.Count) { [string]$cIp.Items[0] } else { '' })
    $g1.Controls.Add($cIp); $g1.Controls.Add((New-Label '(the addresses of this PC are in the list; 127.0.0.1 = this PC only)' 300 32))
    $g1.Controls.Add((New-Label 'Write it to:' 14 66)); $checks = @{}; $y = 88
    foreach ($t in $targets) {
        $cb = New-Object Windows.Forms.CheckBox; $cb.AutoSize = $true; $cb.Location = New-Object Drawing.Point(20, $y)
        $cb.Text = $t.Label + '   now: ' + $(if (-not $t.Exists) { '(not found)' } elseif ($t.Current) { $t.Current } else { '-' })
        $cb.Enabled = $t.Exists; $cb.Checked = $t.Exists; $checks[$t.Key] = $cb; $g1.Controls.Add($cb); $y += 26
    }
    $bIp = New-Button 'Write the IP' 560 196 150 28; $g1.Controls.Add($bIp)
    $g2 = New-Group 'LAN DNS server (for other PCs on your network)' 12 250 720 120
    $g2.Controls.Add((New-Label 'Domain name it answers' 14 32)); $tDom = New-Object Windows.Forms.TextBox; $tDom.Location = New-Object Drawing.Point(170, 28); $tDom.Width = 220; $tDom.Text = [string]$cfg.DnsDomain; $g2.Controls.Add($tDom)
    $cDns = New-Object Windows.Forms.CheckBox; $cDns.AutoSize = $true; $cDns.Location = New-Object Drawing.Point(18, 64); $cDns.Text = 'Start it with the one click (its questions are answered automatically: the domain above and the IP)'; $cDns.Checked = $bpDns.Checked; $g2.Controls.Add($cDns)
    $bDnsNow = New-Button 'Save and start the DNS now' 500 24 210 30; $g2.Controls.Add($bDnsNow)
    $st = New-Label '' 14 384; $f.Controls.AddRange(@($g1, $g2, $st))
    $bClose = New-Button 'Save and close' 582 380 150 30; $bClose.Font = $Bold; $f.Controls.Add($bClose)
    $save = {
        $c = Read-BpConfig $dir; $c.DnsDomain = $tDom.Text.Trim(); if (Test-BpIp $cIp.Text.Trim()) { $c.DnsIp = $cIp.Text.Trim() }
        Save-BpConfig $c; $bpDns.Checked = $cDns.Checked; $script:Prefs.StartDns = $cDns.Checked; Save-Settings $null
        $script:Services = @(Get-BpServices $dir $c); return $c
    }
    $bIp.Add_Click({
        $ip = $cIp.Text.Trim()
        if (-not (Test-BpIp $ip)) { $st.ForeColor = $Red; $st.Text = "Not an IPv4 address: $ip"; return }
        $keys = @($checks.Keys | Where-Object { $checks[$_].Checked })
        try { $c = & $save; $done = @(Set-BpIp $dir $ip $keys $c) } catch { $st.ForeColor = $Red; $st.Text = $_.Exception.Message; return }
        $st.ForeColor = $Green; $st.Text = $(if ($done.Count) { "Written ($($done.Count)): restart the redirector / DNS / BF Launcher to use it. Backups: <file>.before-ip" } else { 'Nothing to change - everything already says ' + $ip })
        Bp-Log ("Network: IP $ip written to " + $(if ($done.Count) { $done -join '; ' } else { 'nothing (already set)' }))
    })
    $bDnsNow.Add_Click({
        if (-not $tDom.Text.Trim() -or -not (Test-BpIp $cIp.Text.Trim())) { $st.ForeColor = $Red; $st.Text = 'The DNS needs a domain name and an IP address.'; return }
        $c = & $save; $svc = $script:Services | Where-Object { $_.Key -eq 'dns' } | Select-Object -First 1
        if (-not $svc) { $st.ForeColor = $Red; $st.Text = 'No LAN DNS part in the launcher settings.'; return }
        $e = Start-BpService $svc $Root; $st.ForeColor = $(if ($e) { $Red } else { $Green }); $st.Text = $(if ($e) { "LAN DNS: $e" } else { "LAN DNS started: $($c.DnsDomain) -> $($c.DnsIp)" }); Bp-Log $st.Text; Refresh-Services
    })
    $bClose.Add_Click({ [void](& $save); $f.Close() })
    Set-BmTheme $f; [void]$f.ShowDialog($form)
}
$bpNet.Add_Click({ Show-BpNetworkDialog })

# ---- AltBotMod V16.2: Accounts and quick join - logs in to the BF-Portable backend as the BF Launcher does and starts
# BF3 straight into the chosen server; optionally right after the one click ----
$script:BpAccountsPath = Join-Path $PSScriptRoot 'Accounts.json'
function Get-BpJoinTarget($list, [string]$want) {
    $s = @($list | Where-Object { $want -and $_.gnam -eq $want } | Select-Object -First 1)
    if (-not $s.Count) { $s = @($list | Select-Object -First 1) }
    return $(if ($s.Count) { $s[0] } else { $null })
}
function Start-BpQuickJoin([string]$wantServer = '') {
    $acc = Read-BpAccounts; $mine = @($acc.Accounts | Where-Object { -not $_.Backend })   # (V21.1: this PC's backend only)
    $a = @($mine | Where-Object { $_.User -eq $acc.Default } | Select-Object -First 1)
    if (-not $a.Count) { $a = @($mine | Select-Object -First 1) }
    if (-not $a.Count) { Bp-Log 'Quick join: no saved account - add one under "Accounts and quick join"'; return $false }
    $c = Get-BpClientConfig $dir; $list = @(Get-BpServerList $c.BaseUrl $c.ApiKey)
    $srv = Get-BpJoinTarget $list $(if ($wantServer) { $wantServer } else { $acc.AutoJoinServer })
    if (-not $srv) { Bp-Log 'Quick join: no BF3 server is listed by the backend yet'; return $false }
    try { Bp-Log (Invoke-BpQuickJoin $dir $a[0] $srv); return $true } catch { Bp-Log ('Quick join: ' + $_.Exception.Message); return $false }
}
function Show-BpAccountsDialog {
    $acc = Read-BpAccounts; $cc = Get-BpClientConfig $dir
    $f = New-Object Windows.Forms.Form; $f.Text = 'Accounts and quick join'; $f.Size = New-Object Drawing.Size(760, 520); $f.StartPosition = 'CenterParent'; $f.Font = $Font; $f.FormBorderStyle = 'FixedDialog'; $f.MaximizeBox = $false
    $g1 = New-Group 'Accounts (BF-Portable backend - the BF Launcher''s logins)' 12 10 720 200
    $lb = New-Object Windows.Forms.ListBox; $lb.Location = New-Object Drawing.Point(14, 28); $lb.Size = New-Object Drawing.Size(250, 156); $g1.Controls.Add($lb)
    $locals = { @($acc.Accounts | Where-Object { -not $_.Backend }) }   # (V21.1: players on other PCs live in LAN servers)
    $fill = { $lb.Items.Clear(); foreach ($x in (& $locals)) { [void]$lb.Items.Add($(if ($x.User -eq $acc.Default) { $x.User + '   (default)' } else { $x.User })) } }
    & $fill
    $g1.Controls.Add((New-Label 'User name' 290 32)); $tU = New-Object Windows.Forms.TextBox; $tU.Location = New-Object Drawing.Point(380, 28); $tU.Width = 200; $g1.Controls.Add($tU)
    $g1.Controls.Add((New-Label 'Password' 290 66)); $tP = New-Object Windows.Forms.TextBox; $tP.Location = New-Object Drawing.Point(380, 62); $tP.Width = 200; $tP.UseSystemPasswordChar = $true; $g1.Controls.Add($tP)
    $bAdd = New-Button 'Save account' 380 98 120 28; $bTest = New-Button 'Test login' 506 98 100 28; $bDef = New-Button 'Make default' 380 132 120 28; $bDel = New-Button 'Remove' 506 132 100 28
    $g1.Controls.AddRange(@($bAdd, $bTest, $bDef, $bDel)); $g1.Controls.Add((New-Label 'Passwords are stored encrypted for your Windows user.' 290 170))
    $g2 = New-Group 'Join a server' 12 220 720 200
    $g2.Controls.Add((New-Label 'Server' 14 32)); $cS = New-Object Windows.Forms.ComboBox; $cS.DropDownStyle = 'DropDownList'; $cS.Location = New-Object Drawing.Point(80, 28); $cS.Width = 420; $g2.Controls.Add($cS)
    $bList = New-Button 'Refresh list' 510 26 100 28; $bGo = New-Button 'Join now' 616 26 92 28; $bGo.Font = $Bold; $g2.Controls.AddRange(@($bList, $bGo))
    $cAuto = New-Object Windows.Forms.CheckBox; $cAuto.AutoSize = $true; $cAuto.Location = New-Object Drawing.Point(18, 70); $cAuto.Text = 'After the one click: log in with the default account and join this server (when it shows up)'; $cAuto.Checked = $acc.AutoJoin; $g2.Controls.Add($cAuto)
    $g2.Controls.Add((New-Label ("Backend: " + $(if ($cc.BaseUrl) { $cc.BaseUrl } else { '(not set)' }) + "    BF3 folder: " + $(if ($cc.Bf3Path) { $cc.Bf3Path } else { '(not set)' })) 18 104))
    $g2.Controls.Add((New-Label '(from the BF Launcher: Client-Files\BF-Launcher\Settings.ini and Path.txt)' 18 126))
    $st = New-Label '' 14 432; $f.Controls.AddRange(@($g1, $g2, $st))
    $servers = @()
    $loadList = {
        $script:joinList = @(Get-BpServerList $cc.BaseUrl $cc.ApiKey); $cS.Items.Clear()
        foreach ($x in $script:joinList) { [void]$cS.Items.Add(('{0}   [{1}]   {2}' -f $x.gnam, $x.PLAYERS, (Get-BmMapName ([string]$x.level)))) }
        $i = [array]::IndexOf(@($script:joinList | ForEach-Object { $_.gnam }), $acc.AutoJoinServer); if ($i -lt 0 -and $cS.Items.Count) { $i = 0 }; if ($i -ge 0) { $cS.SelectedIndex = $i }
        $st.ForeColor = $(if ($cS.Items.Count) { $Green } else { $Red }); $st.Text = $(if ($cS.Items.Count) { "$($cS.Items.Count) BF3 server(s) online" } else { 'No BF3 server listed by the backend (is the web server / database up, are the servers running?)' })
    }
    $save = { if ($cS.SelectedIndex -ge 0) { $acc.AutoJoinServer = [string]$script:joinList[$cS.SelectedIndex].gnam }; $acc.AutoJoin = $cAuto.Checked; Save-BpAccounts $acc }
    $lb.Add_SelectedIndexChanged({ if ($lb.SelectedIndex -ge 0) { $tU.Text = (& $locals)[$lb.SelectedIndex].User; $tP.Text = '' } })
    $bAdd.Add_Click({
        $u = $tU.Text.Trim(); if (-not $u -or -not $tP.Text) { $st.ForeColor = $Red; $st.Text = 'User name and password, please.'; return }
        $acc.Accounts = @($acc.Accounts | Where-Object { $_.User -ne $u -or $_.Backend }) + @([pscustomobject]@{ User = $u; Secret = (Protect-BpSecret $tP.Text); Backend = '' })
        if (-not $acc.Default) { $acc.Default = $u }; Save-BpAccounts $acc; & $fill; $tP.Text = ''; $st.ForeColor = $Green; $st.Text = "Saved $u"
    })
    $bTest.Add_Click({
        $u = $tU.Text.Trim(); $pw = $tP.Text; if (-not $pw) { $x = @((& $locals) | Where-Object { $_.User -eq $u }); if ($x.Count) { $pw = Unprotect-BpSecret $x[0].Secret } }
        $r = Invoke-BpLogin $cc.BaseUrl $u $pw; $st.ForeColor = $(if ($r.Ok) { $Green } else { $Red }); $st.Text = $(if ($r.Ok) { "Login OK for $u (user id $($r.UserId))" } else { "Login failed: $($r.Message)" })
    })
    $bDef.Add_Click({ if ($lb.SelectedIndex -ge 0) { $acc.Default = (& $locals)[$lb.SelectedIndex].User; Save-BpAccounts $acc; & $fill } })
    $bDel.Add_Click({ if ($lb.SelectedIndex -ge 0) { $u = (& $locals)[$lb.SelectedIndex].User; $acc.Accounts = @($acc.Accounts | Where-Object { $_.User -ne $u -or $_.Backend }); if ($acc.Default -eq $u) { $acc.Default = '' }; Save-BpAccounts $acc; & $fill } })
    $bList.Add_Click({ & $loadList })
    $bGo.Add_Click({
        if ($cS.SelectedIndex -lt 0) { $st.ForeColor = $Red; $st.Text = 'Pick a server (Refresh list).'; return }
        & $save
        $a = @((& $locals) | Where-Object { $_.User -eq $acc.Default }); if ($lb.SelectedIndex -ge 0) { $a = @((& $locals)[$lb.SelectedIndex]) }
        if (-not $a.Count) { $st.ForeColor = $Red; $st.Text = 'Save an account first.'; return }
        try { $m = Invoke-BpQuickJoin $dir $a[0] $script:joinList[$cS.SelectedIndex]; $st.ForeColor = $Green; $st.Text = $m; Bp-Log $m } catch { $st.ForeColor = $Red; $st.Text = $_.Exception.Message }
    })
    $f.Add_FormClosing({ & $save })
    $f.Add_Shown({ & $loadList })
    Set-BmTheme $f; [void]$f.ShowDialog($form)
}
$bpAcc.Add_Click({ Show-BpAccountsDialog })
$bpJoin.Add_Click({ [void](Start-BpQuickJoin) })
# ---- AltBotMod V21.1: LAN servers - find BF3 servers on other PCs running BF-Portable, register a player there, join ----
function Show-BpLanDialog {
    $f = New-Object Windows.Forms.Form; $f.Text = 'LAN servers'; $f.Size = New-Object Drawing.Size(860, 600); $f.StartPosition = 'CenterParent'; $f.Font = $Font; $f.FormBorderStyle = 'FixedDialog'; $f.MaximizeBox = $false
    $state = [pscustomobject]@{ Backends = @(); Servers = @(); Acc = (Read-BpAccounts) }
    $g1 = New-Group 'Find servers' 12 8 820 100
    $bScan = New-Button 'Search my network' 14 24 170 30; $bScan.Font = $Bold
    $g1.Controls.Add((New-Label 'or a PC by name / IP (e.g. 192.168.1.42, or 192.168.1.42:8080)' 196 30))
    $tHost = New-Object Windows.Forms.TextBox; $tHost.Location = New-Object Drawing.Point(196, 58); $tHost.Width = 250
    $bHost = New-Button 'Look there' 452 54 110 28; $bRefresh = New-Button 'Refresh list' 568 54 110 28
    $g1.Controls.AddRange(@($bScan, $tHost, $bHost, $bRefresh))
    $lv = New-Object Windows.Forms.ListView; $lv.View = 'Details'; $lv.FullRowSelect = $true; $lv.GridLines = $true; $lv.HideSelection = $false; $lv.MultiSelect = $false
    $lv.Location = New-Object Drawing.Point(12, 116); $lv.Size = New-Object Drawing.Size(820, 220)
    foreach ($c in @(@('PC', 170), @('Server', 250), @('Map', 190), @('Players', 70), @('Game id', 110))) { [void]$lv.Columns.Add($c[0], $c[1]) }
    $g2 = New-Group 'Your player on the chosen PC' 12 344 820 170
    $g2.Controls.Add((New-Label 'Saved account' 14 30)); $cAcc = New-Object Windows.Forms.ComboBox; $cAcc.DropDownStyle = 'DropDownList'; $cAcc.Location = New-Object Drawing.Point(130, 26); $cAcc.Width = 260; $g2.Controls.Add($cAcc)
    $bJoin = New-Button 'Join' 400 22 110 32; $bJoin.Font = $Bold; $g2.Controls.Add($bJoin)
    $g2.Controls.Add((New-Label 'No player on that PC yet? Make one there (and save it here):' 14 72))
    $g2.Controls.Add((New-Label 'Player name' 14 102)); $tU = New-Object Windows.Forms.TextBox; $tU.Location = New-Object Drawing.Point(130, 98); $tU.Width = 180; $g2.Controls.Add($tU)
    $g2.Controls.Add((New-Label 'Password' 324 102)); $tP = New-Object Windows.Forms.TextBox; $tP.Location = New-Object Drawing.Point(400, 98); $tP.Width = 160; $tP.UseSystemPasswordChar = $true; $g2.Controls.Add($tP)
    $bReg = New-Button 'Register' 570 94 110 30; $bSave = New-Button 'Save existing' 686 94 120 30; $g2.Controls.AddRange(@($bReg, $bSave))
    $g2.Controls.Add((New-Label 'Letters, digits and spaces. The password is stored encrypted for your Windows user. Each PC has its own players.' 14 136))
    $st = New-Label '' 14 524; $st.AutoSize = $false; $st.Size = New-Object Drawing.Size(820, 40)
    $f.Controls.AddRange(@($g1, $lv, $g2, $st))
    $say = { param([string]$t, [bool]$bad) $st.ForeColor = $(if ($bad) { $Red } else { $Green }); $st.Text = $t; [Windows.Forms.Application]::DoEvents() }
    $selected = { if ($lv.SelectedItems.Count) { return $lv.SelectedItems[0].Tag }; return $null }
    $fillAcc = {
        $cAcc.Items.Clear(); $s = & $selected; if (-not $s) { return }
        $list = @(Get-BpAccountsFor $state.Acc $s.Backend)
        foreach ($a in $list) { [void]$cAcc.Items.Add($a.User) }
        if ($cAcc.Items.Count) { $i = [array]::IndexOf(@($list | ForEach-Object { $_.User }), $state.Acc.Default); $cAcc.SelectedIndex = [Math]::Max(0, $i) }
        $g2.Text = "Your player on $($s.Backend.Name)"
    }
    $fill = {
        $lv.BeginUpdate(); $lv.Items.Clear()
        foreach ($x in $state.Servers) {
            $it = New-Object Windows.Forms.ListViewItem($(if ($x.Backend.Local) { 'This PC' } else { "$($x.Backend.Name) ($($x.Backend.Host))" }))
            [void]$it.SubItems.Add([string]$x.gnam); [void]$it.SubItems.Add((Map-Display ([string]$x.level) ([string]$x.levellocation)))
            [void]$it.SubItems.Add([string]$x.PLAYERS); [void]$it.SubItems.Add([string]$x.gid); $it.Tag = $x; [void]$lv.Items.Add($it)
        }
        $lv.EndUpdate(); if ($lv.Items.Count) { $lv.Items[0].Selected = $true }
        & $fillAcc
        $pcs = @($state.Backends | Where-Object { -not $_.Local }).Count
        & $say ("{0} BF3 server(s) on {1} PC(s) ({2} other PC(s) with BF-Portable found)" -f $lv.Items.Count, @($state.Servers | ForEach-Object { $_.Backend.BaseUrl } | Select-Object -Unique).Count, $pcs) (-not $lv.Items.Count)
    }
    $search = { param([string[]]$extra, [bool]$scan)
        & $say $(if ($scan) { 'Searching the network for BF-Portable backends (a few seconds)...' } else { "Looking at $($extra -join ', ')..." }) $false
        $f.Cursor = 'WaitCursor'
        try {
            $found = @(Find-BpLanBackends $dir $extra -NoScan:(-not $scan))
            $keep = @($state.Backends | Where-Object { $b = $_; -not @($found | Where-Object { $_.BaseUrl -eq $b.BaseUrl }).Count })
            $state.Backends = @($found) + @($keep)
            $state.Servers = @(Get-BpLanServers $state.Backends)
        } catch { & $say $_.Exception.Message $true } finally { $f.Cursor = 'Default' }
        & $fill
        if ($extra.Count -and -not @($state.Backends | Where-Object { $_.Host -eq ($extra[0] -split ':')[0] }).Count) { & $say "$($extra[0]): no BF-Portable backend answered (is its web server running, port 80 open in its firewall?)" $true }
    }
    $lv.Add_SelectedIndexChanged({ & $fillAcc })
    $bScan.Add_Click({ & $search @() $true })
    $bHost.Add_Click({ if ($tHost.Text.Trim()) { & $search @($tHost.Text.Trim()) $false } })
    $bRefresh.Add_Click({ $state.Servers = @(Get-BpLanServers $state.Backends); & $fill })
    $bReg.Add_Click({
        $s = & $selected; if (-not $s) { & $say 'Pick a server first (its PC gets the player).' $true; return }
        $u = $tU.Text.Trim(); $r = Invoke-BpRegister $s.Backend.BaseUrl $s.Backend.ApiKey $u $tP.Text
        if (-not $r.Ok) { & $say "Register on $($s.Backend.Name): $($r.Message)" $true; return }
        $l = Invoke-BpLogin $s.Backend.BaseUrl $u $tP.Text
        $state.Acc.Accounts = @($state.Acc.Accounts | Where-Object { -not ($_.User -eq $u -and $_.Backend -eq $(if ($s.Backend.Local) { '' } else { $s.Backend.BaseUrl })) }) + @([pscustomobject]@{ User = $u; Secret = (Protect-BpSecret $tP.Text); Backend = $(if ($s.Backend.Local) { '' } else { $s.Backend.BaseUrl }) })
        Save-BpAccounts $state.Acc; $tP.Text = ''; & $fillAcc; $cAcc.SelectedItem = $u
        & $say $(if ($l.Ok) { "Player $u made on $($s.Backend.Name) and saved - ready to join." } else { "Player $u made, but the login test failed: $($l.Message)" }) (-not $l.Ok)
        Bp-Log "LAN: player $u registered on $($s.Backend.BaseUrl)"
    })
    $bSave.Add_Click({
        $s = & $selected; if (-not $s) { & $say 'Pick a server first.' $true; return }
        $u = $tU.Text.Trim(); $l = Invoke-BpLogin $s.Backend.BaseUrl $u $tP.Text
        if (-not $l.Ok) { & $say "Login on $($s.Backend.Name) failed: $($l.Message)" $true; return }
        $b = $(if ($s.Backend.Local) { '' } else { $s.Backend.BaseUrl })
        $state.Acc.Accounts = @($state.Acc.Accounts | Where-Object { -not ($_.User -eq $u -and $_.Backend -eq $b) }) + @([pscustomobject]@{ User = $u; Secret = (Protect-BpSecret $tP.Text); Backend = $b })
        Save-BpAccounts $state.Acc; $tP.Text = ''; & $fillAcc; $cAcc.SelectedItem = $u; & $say "Saved $u for $($s.Backend.Name)." $false
    })
    $bJoin.Add_Click({
        $s = & $selected; if (-not $s) { & $say 'Pick a server.' $true; return }
        $a = @(Get-BpAccountsFor $state.Acc $s.Backend | Where-Object { $_.User -eq [string]$cAcc.SelectedItem })
        if (-not $a.Count) { & $say "No player of yours on $($s.Backend.Name) yet - register one below (or save an existing one)." $true; $tU.Focus(); return }
        try { $m = Invoke-BpLanJoin $dir $a[0] $s; & $say $m $false; Bp-Log $m } catch { & $say $_.Exception.Message $true }
    })
    $f.Add_Shown({ & $search @() $false })   # (this PC's servers at once; the network search on the button)
    Set-BmTheme $f; [void]$f.ShowDialog($form)
}
$bpLan.Add_Click({ Show-BpLanDialog })
# After the one click: wait (up to 3 min) for the chosen server to be listed, then join it.
$script:AutoJoinUntil = $null
$autoJoinTimer = New-Object Windows.Forms.Timer; $autoJoinTimer.Interval = 5000
$autoJoinTimer.Add_Tick({
    if (-not $script:AutoJoinUntil -or [DateTime]::Now -gt $script:AutoJoinUntil) { $autoJoinTimer.Stop(); if ($script:AutoJoinUntil) { Bp-Log 'Quick join: the server did not show up within 3 minutes' }; $script:AutoJoinUntil = $null; return }
    $acc = Read-BpAccounts; $c = Get-BpClientConfig $dir; $list = @(Get-BpServerList $c.BaseUrl $c.ApiKey)
    if (-not @($list | Where-Object { -not $acc.AutoJoinServer -or $_.gnam -eq $acc.AutoJoinServer }).Count) { return }
    $autoJoinTimer.Stop(); $script:AutoJoinUntil = $null; [void](Start-BpQuickJoin $acc.AutoJoinServer)
})

# ---- a server's own game rules: name, players, normal / hardcore, ... (Startup.txt + its .bat) ----
function Show-BmServerSetupDialog($u) {
    $inst = $u.Inst
    $f = New-Object Windows.Forms.Form
    $f.Text = "Server setup - $($inst.Label) (port $($inst.GamePort))"; $f.Size = New-Object Drawing.Size(900, 700); $f.MinimumSize = New-Object Drawing.Size(760, 500)
    $f.StartPosition = 'CenterParent'; $f.Font = $Font
    $note = New-Object Windows.Forms.Label; $note.AutoSize = $false; $note.Location = New-Object Drawing.Point(10, 8); $note.Size = New-Object Drawing.Size(865, 36); $note.Anchor = 'Top,Left,Right'
    $note.Text = "Saved into $(Get-BmStartupPath $inst) (the server reads it when it starts) and its $([IO.Path]::GetFileName($inst.Bat)). The admin password and anything else in those files is kept."
    $grid = New-Object Windows.Forms.DataGridView
    $grid.Location = New-Object Drawing.Point(10, 48); $grid.Size = New-Object Drawing.Size(865, 520); $grid.Anchor = 'Top,Bottom,Left,Right'
    $grid.AllowUserToAddRows = $false; $grid.AllowUserToDeleteRows = $false; $grid.RowHeadersVisible = $false; $grid.SelectionMode = 'FullRowSelect'; $grid.EditMode = 'EditOnEnter'
    foreach ($c in @(@('Setting', 220), @('Value', 230), @('Notes', 380))) {
        $col = New-Object Windows.Forms.DataGridViewTextBoxColumn; $col.HeaderText = $c[0]; $col.Width = $c[1]; $col.SortMode = 'NotSortable'; if ($c[0] -ne 'Value') { $col.ReadOnly = $true }
        [void]$grid.Columns.Add($col)
    }
    $grid.Columns[2].AutoSizeMode = 'Fill'
    foreach ($r in (Get-BmServerSetup $inst)) {
        $v = $r.Var; $shown = $(if ($null -eq $r.Value) { '' } else { $r.Value })
        if ($v.Key -eq 'maxPlayers' -and -not $shown) { $shown = [string](Get-BmBatValue $inst.Bat 'PLAYERS') }
        if ($v.Key -eq 'serverName' -and -not $shown) { $shown = [string](Get-BmBatValue $inst.Bat 'SERVERNAME') }
        $n = $grid.Rows.Add(@($v.Label, $shown, $v.Help)); $row = $grid.Rows[$n]
        $row.Tag = [pscustomobject]@{ Var = $v; Original = $shown }
        if ($v.Kind -eq 'bool' -or $v.Kind -eq 'choice') {
            $cell = New-Object Windows.Forms.DataGridViewComboBoxCell
            $items = $(if ($v.Kind -eq 'bool') { @('true', 'false') } else { $script:BmUnlockModes })
            foreach ($it in $items) { [void]$cell.Items.Add($it) }
            if (-not $cell.Items.Contains($shown)) { [void]$cell.Items.Add($shown) }   # (an empty / unusual value stays selectable)
            $cell.Value = $shown; $row.Cells[1] = $cell
        }
    }
    $grid.Add_DataError({ param($sender, $e) $e.ThrowException = $false })
    $grid.Add_CurrentCellDirtyStateChanged({ param($sender, $e) if ($sender.IsCurrentCellDirty) { [void]$sender.CommitEdit('CurrentCellChange') } })
    $grid.Add_CellValueChanged({ param($sender, $e)
        if ($e.RowIndex -lt 0 -or $e.ColumnIndex -ne 1) { return }
        $row = $sender.Rows[$e.RowIndex]; $t = [string]$row.Cells[1].Value
        $ok = $t -eq $row.Tag.Original -or $null -ne (Test-BmVarValue $row.Tag.Var $t)
        $row.Cells[1].Style.BackColor = $(if (-not $ok) { $BadCell } elseif ($t -ne $row.Tag.Original) { $ChangedCell } else { $Field })
    })
    $status = New-Object Windows.Forms.Label; $status.AutoSize = $false; $status.Location = New-Object Drawing.Point(10, 576); $status.Size = New-Object Drawing.Size(865, 36); $status.Anchor = 'Bottom,Left,Right'
    $bNormal = New-Object Windows.Forms.Button; $bNormal.Text = 'Normal preset'; $bNormal.Location = New-Object Drawing.Point(10, 618); $bNormal.Size = New-Object Drawing.Size(120, 30); $bNormal.Anchor = 'Bottom,Left'
    $bHard = New-Object Windows.Forms.Button; $bHard.Text = 'Hardcore preset'; $bHard.Location = New-Object Drawing.Point(136, 618); $bHard.Size = New-Object Drawing.Size(120, 30); $bHard.Anchor = 'Bottom,Left'
    $bSave = New-Object Windows.Forms.Button; $bSave.Text = 'Save (next start)'; $bSave.Location = New-Object Drawing.Point(290, 618); $bSave.Size = New-Object Drawing.Size(150, 30); $bSave.Anchor = 'Bottom,Left'
    $bApply = New-Object Windows.Forms.Button; $bApply.Text = 'Save and apply now'; $bApply.Location = New-Object Drawing.Point(446, 618); $bApply.Size = New-Object Drawing.Size(160, 30); $bApply.Anchor = 'Bottom,Left'
    $bClose = New-Object Windows.Forms.Button; $bClose.Text = 'Close'; $bClose.Location = New-Object Drawing.Point(775, 618); $bClose.Size = New-Object Drawing.Size(100, 30); $bClose.Anchor = 'Bottom,Right'
    $f.Controls.AddRange(@($note, $grid, $status, $bNormal, $bHard, $bSave, $bApply, $bClose))
    $ctx = [pscustomobject]@{ U = $u; Grid = $grid; Status = $status; Form = $f }
    foreach ($b in @($bNormal, $bHard, $bSave, $bApply, $bClose)) { $b.Tag = $ctx }
    $preset = {
        param($c, [bool]$hard)
        foreach ($row in $c.Grid.Rows) { $v = $row.Tag.Var; $want = $(if ($hard) { $v.Hardcore } else { $v.Normal }); if ($want) { $row.Cells[1].Value = $want } }
        $c.Status.ForeColor = $Fg; $c.Status.Text = $(if ($hard) { 'Hardcore values filled in (yellow) - save to keep them.' } else { 'Normal values filled in (yellow) - save to keep them.' })
    }
    $save = {
        param($c, [bool]$apply)
        [void]$c.Grid.EndEdit()
        $values = @{}; $bad = @()
        foreach ($row in $c.Grid.Rows) {
            $t = [string]$row.Cells[1].Value; if ($t -eq $row.Tag.Original) { continue }
            $v = Test-BmVarValue $row.Tag.Var $t
            if ($null -eq $v) { $bad += $row.Tag.Var.Label } else { $values[$row.Tag.Var.Key] = $v }
        }
        if ($bad.Count) { $c.Status.ForeColor = $Red; $c.Status.Text = 'Not saved - check: ' + ($bad -join ', '); return }
        if (-not $values.Count) { $c.Status.ForeColor = $Fg; $c.Status.Text = 'Nothing changed.'; return }
        try { Set-BmServerSetup $c.U.Inst $values } catch { $c.Status.ForeColor = $Red; $c.Status.Text = 'Could not write: ' + $_.Exception.Message; return }
        foreach ($row in $c.Grid.Rows) { if ($values.ContainsKey($row.Tag.Var.Key)) { $row.Tag.Original = [string]$row.Cells[1].Value; $row.Cells[1].Style.BackColor = $Field } }
        $msg = "Saved $($values.Count) setting(s) - the server uses them from its next start."
        if ($apply) {
            $conn = Get-Conn $c.U.Inst
            if (-not $conn) { $msg += ' It is not running, so nothing was sent now.' }
            else {
                $refused = @(Send-BmServerSetup $conn $values)
                $msg = "Saved $($values.Count) setting(s) and sent them to the running server (most take effect from the next round)."
                if ($refused.Count) { $msg += ' Refused while running (they apply at the next start): ' + ($refused -join '; ') }
            }
        }
        $c.Status.ForeColor = $Green; $c.Status.Text = $msg
        Say-Status "$($c.U.Inst.Label): server setup saved"
    }
    $bNormal.Add_Click({ & $preset $this.Tag $false }); $bHard.Add_Click({ & $preset $this.Tag $true })
    $bSave.Add_Click({ & $save $this.Tag $false }); $bApply.Add_Click({ & $save $this.Tag $true })
    $bClose.Add_Click({ $this.Tag.Form.Close() })
    Set-BmTheme $f; [void]$f.ShowDialog($form)
}
foreach ($inst in $Instances) { $u = $Ui[$inst.Number]; $u.Setup.Tag = $u; $u.Setup.Add_Click({ Show-BmServerSetupDialog $this.Tag; Refresh-Instance $this.Tag $true }) }

# ---- Live map page (AltBotMod V13): positions, goals, kills, stuck spots, heatmaps ----
. (Join-Path $PSScriptRoot 'LiveMap-Page.ps1')

# ---- Learning page (AltBotMod V21): what the bots learned on each map; delete, export, import ----
. (Join-Path $PSScriptRoot 'Learning-Page.ps1')

# ---- refresh: the open server every 3 s, all of them every 9 s ----
$tick = 0
$timer = New-Object Windows.Forms.Timer
$timer.Interval = 3000
$timer.Add_Tick({
    $script:tick++
    $selTab = $tabs.SelectedTab; $selNum = $selTab.Tag
    if ($selTab -eq $tabWeb) { return }   # (Battlelog open: no polling - keeps the page smooth)
    if ($selTab -eq $tabLm) { return }    # (Live map open: it reads its own files every second, no RCON needed)
    if ($selTab -eq $tabLearn) { return } # (Learning open: files only, read when shown / refreshed)
    if ($selTab -eq $tabBp) { Refresh-Services; return }
    foreach ($inst in $Instances) {
        if ($selNum -eq $inst.Number -or $script:tick % 3 -eq 0 -or $selTab -eq $tabAll) { Refresh-Instance $Ui[$inst.Number] $false }
    }
})
$tabs.Add_SelectedIndexChanged({
    $t = $tabs.SelectedTab
    if ($t.Tag -is [int]) { Refresh-Instance $Ui[$t.Tag] $true }
    elseif ($t -eq $tabBp) { Refresh-Services }
    elseif ($t -eq $tabWeb -and -not $script:Web) { Show-Battlelog $null }
    if ($t -eq $tabLm) { if ($lmServer.SelectedIndex -lt 0) { $lmServer.SelectedIndex = 0 } else { Lm-Tick }; $lmTimer.Start() } else { $lmTimer.Stop() }
})
$form.Add_Shown({
    $script:StartupDone = $true
    $form.Activate()
    Refresh-Services
    foreach ($inst in $Instances) { Refresh-Instance $Ui[$inst.Number] $true }
    $timer.Start()
    if ($Launch) { Start-OneClick }
})
$form.Add_FormClosed({ $timer.Stop(); $launchTimer.Stop(); $autoJoinTimer.Stop(); $lmTimer.Stop(); Save-Settings $null; foreach ($inst in $Instances) { Drop-Conn $inst } })
Set-BmTheme $form
[void]$form.ShowDialog()
