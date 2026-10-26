# AltBotMod Server Control - the Live map page: what is happening on a server right now. Where every soldier is and
# where each bot is heading (its goal and route), who killed whom (lines on the map, a kill feed, a scoreboard),
# where bots get stuck, and heatmaps (kills, deaths, stuck spots, traffic) for this round or every round on the map.
# Dot-sourced by AltBotMod-ServerControl.ps1 after its tabs exist; the data side is LiveMap-Core.ps1.

# ---- the drawing surface: a double-buffered panel that takes the mouse wheel (C#, compiled once by Windows) ----
$script:LmCanvasType = $null
try {
    if (-not ('AltBotModLiveMapCanvas' -as [type])) {
        Add-Type -ReferencedAssemblies System.Windows.Forms, System.Drawing -TypeDefinition @'
using System;
using System.Windows.Forms;
public class AltBotModLiveMapCanvas : Panel {
    public AltBotModLiveMapCanvas() {
        SetStyle(ControlStyles.Selectable | ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
                 ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        TabStop = true;
    }
    protected override void OnMouseEnter(EventArgs e) { if (Form.ActiveForm == FindForm()) Focus(); base.OnMouseEnter(e); }
    protected override void OnMouseDown(MouseEventArgs e) { Focus(); base.OnMouseDown(e); }
}
'@
    }
    $script:LmCanvasType = 'AltBotModLiveMapCanvas'
} catch { $script:LmCanvasType = $null }

$TeamColor = @{ 0 = [Drawing.Color]::FromArgb(200, 200, 200); 1 = [Drawing.Color]::FromArgb(70, 150, 255); 2 = [Drawing.Color]::FromArgb(255, 120, 50) }
$TeamName = @{ 0 = '-'; 1 = 'US'; 2 = 'RU' }
function Lm-TeamColor([int]$t) { if ($TeamColor.ContainsKey($t)) { return $TeamColor[$t] }; return $TeamColor[0] }
function Lm-TeamName([int]$t) { if ($TeamName.ContainsKey($t)) { return $TeamName[$t] }; return [string]$t }

$tabLm = New-Object Windows.Forms.TabPage; $tabLm.Text = 'Live map'
$tabs.TabPages.Add($tabLm)

# top bar: which server, the switch, what to show
$lmBar = New-Object Windows.Forms.Panel; $lmBar.Dock = 'Top'; $lmBar.Height = 66
$lmServer = New-Object Windows.Forms.ComboBox; $lmServer.DropDownStyle = 'DropDownList'; $lmServer.Location = New-Object Drawing.Point(6, 6); $lmServer.Width = 150
foreach ($inst in $Instances) { [void]$lmServer.Items.Add("$($inst.Label)  ($($inst.GamePort))") }
$lmOn = New-Object Windows.Forms.CheckBox; $lmOn.Text = 'Live map on'; $lmOn.AutoSize = $true; $lmOn.Location = New-Object Drawing.Point(166, 8); $lmOn.Font = $Bold
$lmShow = @{}; $x0 = 290
foreach ($k in @(@('Soldiers', $true), @('Names', $false), @('Goals', $true), @('Routes', $false), @('Kills', $true), @('Stuck', $true))) {
    $cb = New-Object Windows.Forms.CheckBox; $cb.Text = $(switch ($k[0]) { 'Kills' { 'Kills (last minute)' } 'Stuck' { 'Stuck spots' } 'Routes' { 'All routes' } default { $k[0] } })
    $cb.AutoSize = $true; $cb.Location = New-Object Drawing.Point($x0, 8); $cb.Checked = $k[1]; $lmShow[$k[0]] = $cb; $lmBar.Controls.Add($cb)
    $x0 += $(switch ($k[0]) { 'Kills' { 130 } 'Stuck' { 100 } 'Routes' { 90 } default { 78 } })
}
$lmBar.Controls.Add((New-Label 'Heatmap' 6 40))
$lmHeat = New-Object Windows.Forms.ComboBox; $lmHeat.DropDownStyle = 'DropDownList'; $lmHeat.Location = New-Object Drawing.Point(64, 36); $lmHeat.Width = 190
foreach ($h in @('None', 'Kills (where killers stood)', 'Deaths (where soldiers fell)', 'Stuck (where bots got stuck)', 'Traffic (where soldiers go)')) { [void]$lmHeat.Items.Add($h) }
$lmHeat.SelectedIndex = 0
$lmScope = New-Object Windows.Forms.ComboBox; $lmScope.DropDownStyle = 'DropDownList'; $lmScope.Location = New-Object Drawing.Point(262, 36); $lmScope.Width = 170
[void]$lmScope.Items.Add('This round'); [void]$lmScope.Items.Add('Every round on this map'); $lmScope.SelectedIndex = 0
$lmFlipZ = New-Object Windows.Forms.CheckBox; $lmFlipZ.Text = 'Flip N/S'; $lmFlipZ.AutoSize = $true; $lmFlipZ.Location = New-Object Drawing.Point(444, 38)
$lmFlipX = New-Object Windows.Forms.CheckBox; $lmFlipX.Text = 'Flip E/W'; $lmFlipX.AutoSize = $true; $lmFlipX.Location = New-Object Drawing.Point(522, 38)
$lmZoomIn = New-Button '+' 604 34 30 26; $lmZoomOut = New-Button '-' 636 34 30 26; $lmFit = New-Button 'Fit' 668 34 50 26
$lmSave = New-Button 'Save picture...' 722 34 110 26; $lmOpen = New-Button 'Open folder' 836 34 90 26
$lmBar.Controls.AddRange(@($lmServer, $lmOn, $lmHeat, $lmScope, $lmFlipZ, $lmFlipX, $lmZoomIn, $lmZoomOut, $lmFit, $lmSave, $lmOpen))

# right: kill feed, scoreboard, stuck spots, the selected soldier
$lmSide = New-Object Windows.Forms.TabControl; $lmSide.Dock = 'Right'; $lmSide.Width = 360
function Lm-List([string]$title, $cols) {
    $p = New-Object Windows.Forms.TabPage; $p.Text = $title; [void]$lmSide.TabPages.Add($p)
    $lv = New-Object Windows.Forms.ListView; $lv.View = 'Details'; $lv.FullRowSelect = $true; $lv.GridLines = $true; $lv.Dock = 'Fill'; $lv.HideSelection = $false
    foreach ($c in $cols) { [void]$lv.Columns.Add($c[0], $c[1]) }
    $p.Controls.Add($lv); return $lv
}
$lmFeedList = Lm-List 'Kill feed' @(@('Time', 58), @('Killer', 92), @('Weapon', 70), @('Victim', 92), @('m', 34))
$lmBoard = Lm-List 'Scoreboard' @(@('Player', 104), @('Team', 38), @('K', 30), @('D', 30), @('HS', 30), @('Weapon', 70), @('Far m', 40))
$lmStuckList = Lm-List 'Stuck spots' @(@('#', 26), @('Where (x, z)', 92), @('Times', 44), @('Bots', 36), @('How', 90), @('Last', 60))
$lmSelPage = New-Object Windows.Forms.TabPage; $lmSelPage.Text = 'Selected'; [void]$lmSide.TabPages.Add($lmSelPage)
$lmSelText = New-Object Windows.Forms.TextBox; $lmSelText.Multiline = $true; $lmSelText.ReadOnly = $true; $lmSelText.Dock = 'Fill'; $lmSelText.ScrollBars = 'Vertical'
$lmSelText.Text = 'Click a soldier on the map.'
$lmSelPage.Controls.Add($lmSelText)

# the map itself
if ($script:LmCanvasType) { $lmCanvas = New-Object $script:LmCanvasType }
else {
    $lmCanvas = New-Object Windows.Forms.Panel
    try { $lmCanvas.GetType().GetProperty('DoubleBuffered', [Reflection.BindingFlags]'NonPublic,Instance').SetValue($lmCanvas, $true, $null) } catch { }
}
$lmCanvas.Dock = 'Fill'; $lmCanvas.BackColor = [Drawing.Color]::FromArgb(22, 24, 28)
$lmInfo = New-Object Windows.Forms.Label; $lmInfo.Dock = 'Bottom'; $lmInfo.Height = 20; $lmInfo.ForeColor = $Grey
$lmInfo.Text = 'Mouse wheel: zoom   Drag: move   Click a soldier: details   Double-click a list row: go there'
$tabLm.Controls.Add($lmCanvas); $tabLm.Controls.Add($lmInfo); $tabLm.Controls.Add($lmSide); $tabLm.Controls.Add($lmBar)
$lmCanvas.BringToFront()

# ---- state ----
$script:Lm = @{ Inst = $null; Live = $null; LiveStamp = [DateTime]::MinValue; Feeds = @{}; View = (New-LmView); Image = $null; ImageFile = ''
    HeatBmp = $null; HeatGrid = $null; HeatKey = ''; HeatAt = 0; Spots = @(); Selected = ''; Drag = $null; Moved = $false; ListsAt = 0; ListsVer = -1; Level = ''; Syncing = $false }
$lmTimer = New-Object Windows.Forms.Timer; $lmTimer.Interval = 1000

function Lm-Feed { $n = $script:Lm.Inst.Number; if (-not $script:Lm.Feeds.ContainsKey($n)) { $script:Lm.Feeds[$n] = New-LmFeed }; return $script:Lm.Feeds[$n] }
function Lm-Round { if ($lmScope.SelectedIndex -eq 0 -and $script:Lm.Live -and $script:Lm.Live.Round) { return [long]$script:Lm.Live.Round }; return [long]0 }
function Lm-Level { if ($script:Lm.Live -and $script:Lm.Live.Level) { return $script:Lm.Live.Level }; return $script:Lm.Level }
function Lm-Select-Server([int]$index) {
    if ($index -lt 0 -or $index -ge $Instances.Count) { return }
    $script:Lm.Inst = $Instances[$index]; $script:Lm.Live = $null; $script:Lm.LiveStamp = [DateTime]::MinValue
    if ($script:Lm.Image) { $script:Lm.Image.Dispose() }; $script:Lm.Image = $null; $script:Lm.ImageFile = ''
    $script:Lm.HeatKey = ''; $script:Lm.View.Fitted = ''; $script:Lm.Selected = ''; $script:Lm.ListsAt = 0
    $script:Lm.Syncing = $true; try { $lmOn.Checked = Get-LmEnabled $dir $script:Lm.Inst } catch { $lmOn.Checked = $true }; $script:Lm.Syncing = $false
    Lm-Tick
}
function Lm-Load-Image([string]$file) {
    $path = Get-LmPicturePath $dir $file
    if (-not $path -or -not (Test-Path -LiteralPath $path)) { return }
    try {
        $bytes = [IO.File]::ReadAllBytes($path)   # (a copy in memory: the file stays free for the server)
        $ms = New-Object IO.MemoryStream(, $bytes); $tmp = [Drawing.Image]::FromStream($ms)
        try { $img = New-Object Drawing.Bitmap($tmp) } finally { $tmp.Dispose(); $ms.Dispose() }
        if ($script:Lm.Image) { $script:Lm.Image.Dispose() }
        $script:Lm.Image = $img; $script:Lm.ImageFile = $file
    } catch { }
}
# The heat layer as a small picture (one pixel per 10 m cell), stretched smoothly over the map when drawn.
function Lm-Build-Heat {
    $layer = @('', 'kills', 'deaths', 'stuck', 'traffic')[$lmHeat.SelectedIndex]
    if ($script:Lm.HeatBmp) { $script:Lm.HeatBmp.Dispose(); $script:Lm.HeatBmp = $null }; $script:Lm.HeatGrid = $null
    if (-not $layer -or -not $script:Lm.Inst) { return }
    $cells = Get-LmHeat (Lm-Feed) (Lm-Level) $layer (Lm-Round)
    $g = Get-LmHeatPixels $cells
    if (-not $g) { return }
    $bmp = New-Object Drawing.Bitmap($g.W, $g.H, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $argb = $g.Argb
    $data = $bmp.LockBits((New-Object Drawing.Rectangle(0, 0, $g.W, $g.H)), [Drawing.Imaging.ImageLockMode]::WriteOnly, $bmp.PixelFormat)
    try { [Runtime.InteropServices.Marshal]::Copy($argb, 0, $data.Scan0, $argb.Length) } finally { $bmp.UnlockBits($data) }
    $script:Lm.HeatBmp = $bmp; $script:Lm.HeatGrid = $g
}
function Lm-Fit {
    $v = $script:Lm.View; $v.W = [Math]::Max(100, $lmCanvas.ClientSize.Width); $v.H = [Math]::Max(100, $lmCanvas.ClientSize.Height)
    $b = Get-LmBounds $script:Lm.Live; Set-LmViewFit $v $b[0] $b[1] $b[2] $b[3]; $v.Fitted = Lm-Level; $lmCanvas.Invalidate()
}
function Lm-Goto([double]$x, [double]$z) {
    $v = $script:Lm.View; $v.CX = $x; $v.CZ = $z; if ($v.Scale -lt 1.5) { $v.Scale = 1.5 }; $lmCanvas.Invalidate()
}
function Lm-Ago([long]$t) { if (-not $t) { return '' }; return ([DateTimeOffset]::FromUnixTimeSeconds($t).LocalDateTime.ToString('HH:mm:ss')) }

function Lm-Refresh-Lists {
    $feed = Lm-Feed; $lvl = Lm-Level; $round = Lm-Round
    $kills = Select-LmEvents $feed.Kills $lvl $round
    $lmFeedList.BeginUpdate(); $lmFeedList.Items.Clear()
    for ($i = $kills.Count - 1; $i -ge 0 -and $i -ge $kills.Count - 300; $i--) {
        $k = $kills[$i]
        $it = New-Object Windows.Forms.ListViewItem((Lm-Ago $k.Time))
        [void]$it.SubItems.Add($(if ($k.Suicide) { '(self / world)' } else { $k.Killer })); [void]$it.SubItems.Add($k.Weapon + $(if ($k.Headshot) { ' HS' } else { '' }))
        [void]$it.SubItems.Add($k.Victim); [void]$it.SubItems.Add([string][int]$k.Distance)
        $it.ForeColor = $(if ($k.TeamKill) { $Red } else { Lm-TeamColor $k.KillerTeam }); $it.Tag = @($k.VX, $k.VZ)
        [void]$lmFeedList.Items.Add($it)
    }
    $lmFeedList.EndUpdate()
    $lmBoard.BeginUpdate(); $lmBoard.Items.Clear()
    foreach ($r in (Get-LmScoreboard $feed $lvl $round)) {
        $it = New-Object Windows.Forms.ListViewItem($r.Name + $(if ($r.Bot) { '' } else { ' *' }))
        foreach ($v in @((Lm-TeamName $r.Team), $r.Kills, $r.Deaths, $r.Headshots, $r.Weapon, [int]$r.Longest)) { [void]$it.SubItems.Add([string]$v) }
        $it.ForeColor = Lm-TeamColor $r.Team; $it.Tag = $r.Name
        [void]$lmBoard.Items.Add($it)
    }
    $lmBoard.EndUpdate()
    $script:Lm.Spots = @(Get-LmStuckSpots $feed $lvl $round)
    $lmStuckList.BeginUpdate(); $lmStuckList.Items.Clear(); $n = 0
    foreach ($s in $script:Lm.Spots) {
        $n++; if ($n -gt 200) { break }
        $it = New-Object Windows.Forms.ListViewItem([string]$n)
        foreach ($v in @(("{0:0}, {1:0}" -f $s.X, $s.Z), $s.Count, $s.Bots, $s.Kinds, (Lm-Ago $s.Last))) { [void]$it.SubItems.Add([string]$v) }
        $it.Tag = @($s.X, $s.Z); [void]$lmStuckList.Items.Add($it)
    }
    $lmStuckList.EndUpdate()
    $script:Lm.ListsAt = [Environment]::TickCount
}
function Lm-Show-Selected {
    $name = $script:Lm.Selected; $live = $script:Lm.Live
    if (-not $name -or -not $live) { return }
    $p = $live.Players | Where-Object { $_.Name -eq $name } | Select-Object -First 1
    if (-not $p) { $lmSelText.Text = "$name has left."; return }
    $feed = Lm-Feed; $lvl = Lm-Level; $round = [long]$live.Round
    $mine = @(Select-LmEvents $feed.Kills $lvl $round | Where-Object { $_.Killer -eq $name -or $_.Victim -eq $name })
    $k = @($mine | Where-Object { $_.Killer -eq $name -and -not $_.Suicide -and -not $_.TeamKill }).Count; $d = @($mine | Where-Object { $_.Victim -eq $name }).Count
    $lines = @("$($p.Name)", "$(if ($p.Bot) { 'Bot' } else { 'Player' })$(if ($p.Class) { ', ' + $p.Class }), team $(Lm-TeamName $p.Team)",
        "$(if ($p.Alive) { "Alive, health $([int]$p.Health)" } else { 'Dead' })", "Doing: $($p.Doing)")
    if ($p.Vehicle) { $lines += "In: $($p.Vehicle)" }
    $lines += ("At: {0:0}, {1:0}  (height {2:0} m)" -f $p.X, $p.Z, $p.Y)
    if ($p.HasGoal) { $lines += ("Heading for: {0:0}, {1:0}  ({2:0} m away)" -f $p.GoalX, $p.GoalZ, [Math]::Sqrt(($p.GoalX - $p.X) * ($p.GoalX - $p.X) + ($p.GoalZ - $p.Z) * ($p.GoalZ - $p.Z))) }
    if ($p.Route.Count) { $lines += "Route: $($p.Route.Count) points left (drawn yellow)" }
    $lines += ''; $lines += "This round: $k kill(s), $d death(s)"
    foreach ($e in ($mine | Select-Object -Last 12)) {
        $lines += $(if ($e.Victim -eq $name -and $e.Suicide) { "  $(Lm-Ago $e.Time) died ($($e.Weapon))" } elseif ($e.Victim -eq $name) { "  $(Lm-Ago $e.Time) killed by $($e.Killer) ($($e.Weapon), $([int]$e.Distance) m)" } else { "  $(Lm-Ago $e.Time) killed $($e.Victim) ($($e.Weapon), $([int]$e.Distance) m$(if ($e.Headshot) { ', head shot' }))" })
    }
    $stuck = @(Select-LmEvents $feed.Stucks $lvl $round | Where-Object { $_.Name -eq $name })
    if ($stuck.Count) { $lines += ''; $lines += "Stuck $($stuck.Count) time(s) this round, last at $("{0:0}, {1:0}" -f $stuck[-1].X, $stuck[-1].Z) ($($stuck[-1].Kind))" }
    $lmSelText.Text = $lines -join "`r`n"
}

# ---- every second while the page is open ----
function Lm-Tick {
    $inst = $script:Lm.Inst; if (-not $inst) { return }
    $livePath = Get-LmLivePath $dir $inst
    $stamp = [DateTime]::MinValue; try { if (Test-Path -LiteralPath $livePath) { $stamp = [IO.File]::GetLastWriteTimeUtc($livePath) } } catch { }
    if ($stamp -ne $script:Lm.LiveStamp) {
        $l = Read-LmLive $livePath
        if ($l) { $script:Lm.Live = $l; $script:Lm.LiveStamp = $stamp; if ($l.Level) { $script:Lm.Level = $l.Level } }
    }
    $live = $script:Lm.Live
    if ($live -and $live.Picture -and $live.Picture.File -ne $script:Lm.ImageFile) { Lm-Load-Image $live.Picture.File }
    if ($live -and -not $live.Picture -and $script:Lm.Image) { $script:Lm.Image.Dispose(); $script:Lm.Image = $null; $script:Lm.ImageFile = '' }
    $feed = Lm-Feed
    [void](Update-LmFeed $feed (Get-LmEventsPath $dir $inst))
    if ($script:Lm.View.Fitted -ne (Lm-Level) -and $live -and -not $live.Off) { Lm-Fit }
    $now = [Environment]::TickCount
    $heatKey = "$($lmHeat.SelectedIndex)|$(Lm-Level)|$(Lm-Round)|$($inst.Number)"
    if ($heatKey -ne $script:Lm.HeatKey -or ($feed.Changed -and $now - $script:Lm.HeatAt -gt 4000)) { Lm-Build-Heat; $script:Lm.HeatKey = $heatKey; $script:Lm.HeatAt = $now }
    # lists: when kills / stuck reports / rounds came in (at most every 3 s), and every 15 s for the "ago" times
    if (-not $script:Lm.ListsAt -or ($feed.EventsVersion -ne $script:Lm.ListsVer -and $now - $script:Lm.ListsAt -gt 3000) -or $now - $script:Lm.ListsAt -gt 15000) {
        $script:Lm.ListsVer = $feed.EventsVersion; Lm-Refresh-Lists
    }
    if ($script:Lm.Selected) { Lm-Show-Selected }
    $lmCanvas.Invalidate()
}

# ---- drawing ----
$lmFontSmall = New-Object Drawing.Font('Segoe UI', 7.5)
$lmFontFlag = New-Object Drawing.Font('Segoe UI Semibold', 9)
$lmFontBig = New-Object Drawing.Font('Segoe UI Semibold', 11)
function Lm-Pt([double]$x, [double]$z) { $s = ConvertTo-LmScreen $script:Lm.View $x $z; return (New-Object Drawing.PointF([float]$s[0], [float]$s[1])) }
function Lm-Draw-Image($g, $img, [double]$minX, [double]$minZ, [double]$maxX, [double]$maxZ) {
    # three corners (north-west, north-east, south-west): flipping the view mirrors the picture by itself
    $pts = [Drawing.PointF[]]@((Lm-Pt $minX $maxZ), (Lm-Pt $maxX $maxZ), (Lm-Pt $minX $minZ))
    $g.DrawImage($img, $pts)
}
$lmCanvas.Add_Paint({
    param($sender, $e)
    $g = $e.Graphics; $v = $script:Lm.View
    $v.W = [Math]::Max(100, $lmCanvas.ClientSize.Width); $v.H = [Math]::Max(100, $lmCanvas.ClientSize.Height)
    $v.FlipX = $lmFlipX.Checked; $v.FlipZ = $lmFlipZ.Checked
    $live = $script:Lm.Live
    $white = [Drawing.Brushes]::White
    if (-not $live -or $live.Off) {
        $msg = $(if (-not $script:Lm.Inst) { 'Pick a server.' }
            elseif ($live -and $live.Off) { "The live map is off on $($script:Lm.Inst.Label). Tick 'Live map on' above (it takes effect at once)." }
            else { "No live map from $($script:Lm.Inst.Label) yet.`r`nIt appears when the server runs AltBotMod V13 (or later) with the live map on, a few seconds into a round.`r`nFiles: $(Get-LmDir $dir)" })
        $g.DrawString($msg, $lmFontBig, $white, 20, 20); return
    }
    $g.SmoothingMode = 'AntiAlias'
    if ($script:Lm.Image -and $live.Picture) {
        $g.InterpolationMode = $(if ($script:Lm.Drag) { 'NearestNeighbor' } else { 'Bilinear' })   # (quick while dragging)
        Lm-Draw-Image $g $script:Lm.Image $live.Picture.MinX $live.Picture.MinZ $live.Picture.MaxX $live.Picture.MaxZ
    } else {   # no picture yet (the bots' map is still being surveyed): a 100 m grid
        $pen = New-Object Drawing.Pen([Drawing.Color]::FromArgb(45, 50, 58)); $b = Get-LmBounds $live
        for ($x = [Math]::Floor($b[0] / 100) * 100; $x -le $b[2]; $x += 100) { $g.DrawLine($pen, (Lm-Pt $x $b[1]), (Lm-Pt $x $b[3])) }
        for ($z = [Math]::Floor($b[1] / 100) * 100; $z -le $b[3]; $z += 100) { $g.DrawLine($pen, (Lm-Pt $b[0] $z), (Lm-Pt $b[2] $z)) }
        $pen.Dispose()
    }
    if ($script:Lm.HeatBmp -and $script:Lm.HeatGrid) {
        $hg = $script:Lm.HeatGrid; $g.InterpolationMode = 'HighQualityBilinear'; $g.PixelOffsetMode = 'Half'
        Lm-Draw-Image $g $script:Lm.HeatBmp $hg.MinX $hg.MinZ ($hg.MinX + $hg.W * $hg.Cell) ($hg.MinZ + $hg.H * $hg.Cell)
        $g.PixelOffsetMode = 'Default'
    }
    # stuck spots: magenta rings, bigger for spots hit more often
    if ($lmShow['Stuck'].Checked) {
        $pen = New-Object Drawing.Pen([Drawing.Color]::FromArgb(230, 255, 60, 220), 2); $n = 0
        foreach ($s in $script:Lm.Spots) {
            $n++; if ($n -gt 60) { break }
            $p = Lm-Pt $s.X $s.Z; $r = [float](5 + [Math]::Min(12, 2 * [Math]::Sqrt($s.Count)))
            $g.DrawEllipse($pen, $p.X - $r, $p.Y - $r, 2 * $r, 2 * $r)
            if ($s.Count -gt 1) { $g.DrawString([string]$s.Count, $lmFontSmall, $white, $p.X + $r, $p.Y - $r) }
        }
        $pen.Dispose()
    }
    # flags
    foreach ($f in $live.Flags) {
        $p = Lm-Pt $f.X $f.Z; $c = Lm-TeamColor $f.Owner
        $br = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(200, $c)); $g.FillEllipse($br, $p.X - 12, $p.Y - 12, 24, 24); $br.Dispose()
        $g.DrawEllipse([Drawing.Pens]::White, $p.X - 12, $p.Y - 12, 24, 24)
        $sz = $g.MeasureString($f.Name, $lmFontFlag); $g.DrawString($f.Name, $lmFontFlag, $white, $p.X - $sz.Width / 2, $p.Y - $sz.Height / 2)
    }
    # kills in the last minute: killer -> victim, fading
    if ($lmShow['Kills'].Checked) {
        $now = [long]$live.Time; $lvl = $live.Level
        $feed = Lm-Feed
        for ($i = $feed.Kills.Count - 1; $i -ge 0; $i--) {
            $k = $feed.Kills[$i]
            if ($k.Time -lt $now - 60) { break }
            if ($k.Level -ne $lvl) { continue }
            $a = [int](70 + 185 * [Math]::Max(0.0, 1 - ($now - $k.Time) / 60.0)); $c = $(if ($k.TeamKill) { [Drawing.Color]::Red } else { Lm-TeamColor $k.KillerTeam })
            $pen = New-Object Drawing.Pen([Drawing.Color]::FromArgb($a, $c), 1.5)
            $pv = Lm-Pt $k.VX $k.VZ
            if (-not $k.Suicide) { $pk = Lm-Pt $k.KX $k.KZ; $g.DrawLine($pen, $pk, $pv) }
            $g.DrawLine($pen, $pv.X - 4, $pv.Y - 4, $pv.X + 4, $pv.Y + 4); $g.DrawLine($pen, $pv.X - 4, $pv.Y + 4, $pv.X + 4, $pv.Y - 4)
            $pen.Dispose()
        }
    }
    # soldiers, their goals and routes
    if ($lmShow['Soldiers'].Checked) {
        $sel = $script:Lm.Selected
        foreach ($p in $live.Players) {
            if (-not $p.Alive -or ($p.X -eq 0 -and $p.Z -eq 0)) { continue }
            $c = Lm-TeamColor $p.Team; $pt = Lm-Pt $p.X $p.Z
            $isSel = $p.Name -eq $sel
            if ($p.Route.Count -and ($isSel -or $lmShow['Routes'].Checked)) {
                $pen = New-Object Drawing.Pen($(if ($isSel) { [Drawing.Color]::FromArgb(230, 255, 230, 60) } else { [Drawing.Color]::FromArgb(90, $c) }), $(if ($isSel) { 2 } else { 1 }))
                $last = $pt; foreach ($q in $p.Route) { $np = Lm-Pt $q[0] $q[1]; $g.DrawLine($pen, $last, $np); $last = $np }
                $pen.Dispose()
            }
            if ($p.HasGoal -and ($lmShow['Goals'].Checked -or $isSel)) {
                $pen = New-Object Drawing.Pen([Drawing.Color]::FromArgb($(if ($isSel) { 220 } else { 90 }), $c), 1); $pen.DashStyle = 'Dash'
                $g.DrawLine($pen, $pt, (Lm-Pt $p.GoalX $p.GoalZ)); $pen.Dispose()
            }
            $br = New-Object Drawing.SolidBrush($c); $r = $(if ($p.Bot) { 4 } else { 5 })
            if ($p.Vehicle) { $g.FillRectangle($br, $pt.X - 5, $pt.Y - 5, 10, 10); $g.DrawRectangle([Drawing.Pens]::Black, $pt.X - 5, $pt.Y - 5, 10, 10) }
            else { $g.FillEllipse($br, $pt.X - $r, $pt.Y - $r, 2 * $r, 2 * $r) }
            $br.Dispose()
            if (-not $p.Bot) { $g.DrawEllipse([Drawing.Pens]::White, $pt.X - 7, $pt.Y - 7, 14, 14) }
            if ($isSel) { $pen = New-Object Drawing.Pen([Drawing.Color]::Yellow, 2); $g.DrawEllipse($pen, $pt.X - 10, $pt.Y - 10, 20, 20); $pen.Dispose() }
            if ($lmShow['Names'].Checked -or $isSel -or -not $p.Bot) { $g.DrawString($p.Name, $lmFontSmall, $white, $pt.X + 7, $pt.Y - 6) }
        }
    }
    # what is on screen
    $alive = @($live.Players | Where-Object { $_.Alive }); $bots = @($alive | Where-Object { $_.Bot }).Count
    $age = (Get-LmUnixNow) - $live.Time
    $head = "{0} - {1}   {2} bots and {3} players alive   kills {4}, stuck reports {5} (this server run)" -f (Get-BmMapName $live.Level), (Get-BmModeName $live.Mode), $bots, ($alive.Count - $bots), $live.Kills, $live.Stucks
    $sz = $g.MeasureString($head, $lmFontFlag)
    $bg = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(150, 0, 0, 0)); $g.FillRectangle($bg, 4, 4, $sz.Width + 8, $sz.Height + 4); $bg.Dispose()
    $g.DrawString($head, $lmFontFlag, $white, 8, 6)
    if ($age -gt 15) { $g.DrawString("Last update $age s ago - is the server still running?", $lmFontFlag, [Drawing.Brushes]::Orange, 8, 28) }
    elseif (-not $live.Picture) { $g.DrawString('Map picture: once the bots'' map of this level is ready (100 m grid meanwhile)', $lmFontSmall, [Drawing.Brushes]::Silver, 8, 28) }
    # scale bar
    $m = 100; while ($m * $v.Scale -lt 60) { $m *= 2 }; while ($m * $v.Scale -gt 200 -and $m -gt 10) { $m /= 2 }
    $len = [float]($m * $v.Scale); $y = [float]($v.H - 14)
    $x1 = [float]10; $x2 = [float](10 + $len)
    $g.DrawLine([Drawing.Pens]::White, $x1, $y, $x2, $y); $g.DrawLine([Drawing.Pens]::White, $x1, [float]($y - 4), $x1, [float]($y + 1)); $g.DrawLine([Drawing.Pens]::White, $x2, [float]($y - 4), $x2, [float]($y + 1))
    $g.DrawString("$m m", $lmFontSmall, $white, [float](14 + $len), [float]($y - 8))
    $nUp = $(if ($v.FlipZ) { 'S' } else { 'N' }); $g.DrawString("$nUp up", $lmFontSmall, $white, $v.W - 40, 8)
})

# ---- mouse: wheel zooms at the cursor, drag moves, a click picks a soldier ----
$lmCanvas.Add_MouseWheel({ param($s, $e) Set-LmViewZoom $script:Lm.View $(if ($e.Delta -gt 0) { 1.25 } else { 0.8 }) $e.X $e.Y; $lmCanvas.Invalidate() })
$lmCanvas.Add_MouseDown({ param($s, $e) if ($e.Button -eq 'Left') { $script:Lm.Drag = @($e.X, $e.Y, $script:Lm.View.CX, $script:Lm.View.CZ); $script:Lm.Moved = $false } })
$lmCanvas.Add_MouseMove({
    param($s, $e)
    $d = $script:Lm.Drag; if (-not $d -or $e.Button -ne 'Left') { return }
    $dx = $e.X - $d[0]; $dy = $e.Y - $d[1]
    if ([Math]::Abs($dx) + [Math]::Abs($dy) -gt 3) { $script:Lm.Moved = $true }
    $v = $script:Lm.View; $fx = $(if ($v.FlipX) { -1 } else { 1 }); $fz = $(if ($v.FlipZ) { -1 } else { 1 })
    $v.CX = $d[2] - $dx / ($v.Scale * $fx); $v.CZ = $d[3] + $dy / ($v.Scale * $fz); $lmCanvas.Invalidate()
})
$lmCanvas.Add_MouseUp({
    param($s, $e)
    $wasDrag = $script:Lm.Moved; $script:Lm.Drag = $null; $script:Lm.Moved = $false
    if ($wasDrag -or $e.Button -ne 'Left' -or -not $script:Lm.Live) { return }
    $best = $null; $bestD = 12.0 * 12.0
    foreach ($p in $script:Lm.Live.Players) {
        if (-not $p.Alive) { continue }
        $q = ConvertTo-LmScreen $script:Lm.View $p.X $p.Z; $dd = ($q[0] - $e.X) * ($q[0] - $e.X) + ($q[1] - $e.Y) * ($q[1] - $e.Y)
        if ($dd -lt $bestD) { $bestD = $dd; $best = $p }
    }
    if ($best) { $script:Lm.Selected = $best.Name; $lmSide.SelectedTab = $lmSelPage; Lm-Show-Selected } else { $script:Lm.Selected = '' }
    $lmCanvas.Invalidate()
})
$lmCanvas.Add_Resize({ $lmCanvas.Invalidate() })

# ---- controls ----
$lmServer.Add_SelectedIndexChanged({ Lm-Select-Server $lmServer.SelectedIndex })
$lmOn.Add_CheckedChanged({
    if ($script:Lm.Syncing -or -not $script:Lm.Inst) { return }
    try {
        Set-LmEnabled $dir $script:Lm.Inst $lmOn.Checked
        Say-Status "$($script:Lm.Inst.Label): live map $(if ($lmOn.Checked) { 'on' } else { 'off' }) (LiveMap=$(if ($lmOn.Checked) { 1 } else { 0 }) in enhanced_$($script:Lm.Inst.GamePort).ini; the server applies it at once)"
    } catch { Say-Status "Live map switch: $($_.Exception.Message)" $true }
})
foreach ($cb in $lmShow.Values) { $cb.Add_CheckedChanged({ $lmCanvas.Invalidate() }) }
$lmFlipX.Add_CheckedChanged({ $lmCanvas.Invalidate() }); $lmFlipZ.Add_CheckedChanged({ $lmCanvas.Invalidate() })
$lmHeat.Add_SelectedIndexChanged({ $script:Lm.HeatKey = ''; Lm-Tick })
$lmScope.Add_SelectedIndexChanged({ $script:Lm.HeatKey = ''; $script:Lm.ListsAt = 0; Lm-Refresh-Lists; Lm-Tick })
$lmFit.Add_Click({ Lm-Fit })
$lmZoomIn.Add_Click({ Set-LmViewZoom $script:Lm.View 1.4 ($script:Lm.View.W / 2) ($script:Lm.View.H / 2); $lmCanvas.Invalidate() })
$lmZoomOut.Add_Click({ Set-LmViewZoom $script:Lm.View (1 / 1.4) ($script:Lm.View.W / 2) ($script:Lm.View.H / 2); $lmCanvas.Invalidate() })
$lmOpen.Add_Click({ $d = Get-LmDir $dir; if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Force -Path $d | Out-Null }; Start-Process explorer.exe $d })
$lmSave.Add_Click({
    $dlg = New-Object Windows.Forms.SaveFileDialog; $dlg.Filter = 'PNG picture (*.png)|*.png'
    $dlg.FileName = "LiveMap_$($script:Lm.Inst.GamePort)_$(Lm-Level)_$(Get-Date -Format 'yyyy-MM-dd_HHmm').png"
    if ($dlg.ShowDialog() -ne 'OK') { return }
    $bmp = New-Object Drawing.Bitmap($lmCanvas.ClientSize.Width, $lmCanvas.ClientSize.Height)
    try { $lmCanvas.DrawToBitmap($bmp, (New-Object Drawing.Rectangle(0, 0, $bmp.Width, $bmp.Height))); $bmp.Save($dlg.FileName, [Drawing.Imaging.ImageFormat]::Png); Say-Status "Saved $($dlg.FileName)" }
    catch { Say-Status "Could not save: $($_.Exception.Message)" $true } finally { $bmp.Dispose() }
})
foreach ($lv in @($lmFeedList, $lmStuckList)) {
    $lv.Add_DoubleClick({ param($s, $e) if ($s.SelectedItems.Count) { $t = $s.SelectedItems[0].Tag; Lm-Goto $t[0] $t[1] } })
}
$lmBoard.Add_DoubleClick({
    if (-not $lmBoard.SelectedItems.Count -or -not $script:Lm.Live) { return }
    $name = $lmBoard.SelectedItems[0].Tag; $p = $script:Lm.Live.Players | Where-Object { $_.Name -eq $name -and $_.Alive } | Select-Object -First 1
    $script:Lm.Selected = $name; if ($p) { Lm-Goto $p.X $p.Z }; $lmSide.SelectedTab = $lmSelPage; Lm-Show-Selected
})
$lmTimer.Add_Tick({ Lm-Tick })
