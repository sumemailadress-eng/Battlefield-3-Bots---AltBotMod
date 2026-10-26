# AltBotMod Server Control - the Learning page (AltBotMod V21): what the bots learned on each map, on a list and on the
# map, and deleting what you do not want them to keep. The data side is Learning-Core.ps1.
# Dot-sourced by AltBotMod-ServerControl.ps1 after the Live map page (it borrows its canvas type and map orientation).

$tabLearn = New-Object Windows.Forms.TabPage; $tabLearn.Text = 'Learning'
$tabs.TabPages.Add($tabLearn)
$script:Ln = [pscustomobject]@{ Files = @(); File = $null; Image = $null; ImageFile = ''; View = (New-LmView); Fitted = ''; Rows = @(); Drag = $null; Moved = $false; Filling = $false }

# top: which map, what to show, the folder
$lnBar = New-Object Windows.Forms.Panel; $lnBar.Dock = 'Top'; $lnBar.Height = 70
$lnBar.Controls.Add((New-Label 'Map' 6 10))
$lnMap = New-Object Windows.Forms.ComboBox; $lnMap.DropDownStyle = 'DropDownList'; $lnMap.Location = New-Object Drawing.Point(44, 6); $lnMap.Width = 380
$lnShow = @{}; $x0 = 6
foreach ($k in @(@('Spot', 'Stuck spots'), @('Air', 'Aircraft'), @('Heat', 'Killing grounds'), @('Route', 'Routes from players'), @('Pad', 'Landing spots'))) {
    $cb = New-Object Windows.Forms.CheckBox; $cb.Text = $k[1]; $cb.AutoSize = $true; $cb.Checked = $true; $cb.Location = New-Object Drawing.Point($x0, 40)
    $lnShow[$k[0]] = $cb; $lnBar.Controls.Add($cb); $x0 += $(switch ($k[0]) { 'Spot' { 100 } 'Air' { 80 } 'Heat' { 120 } 'Route' { 150 } default { 110 } })
}
$lnRefresh = New-Button 'Refresh' 436 4 80 28; $lnOpen = New-Button 'Open folder' 522 4 100 28
$lnExport = New-Button 'Export...' 628 4 90 28; $lnImport = New-Button 'Import...' 724 4 90 28
$lnFit = New-Button 'Fit' 820 4 50 28
$lnBar.Controls.AddRange(@($lnMap, $lnRefresh, $lnOpen, $lnExport, $lnImport, $lnFit))

# left: the entries (tick to delete), the buttons
$lnLeft = New-Object Windows.Forms.Panel; $lnLeft.Dock = 'Left'; $lnLeft.Width = 520
$lnList = New-Object Windows.Forms.ListView; $lnList.View = 'Details'; $lnList.CheckBoxes = $true; $lnList.FullRowSelect = $true; $lnList.GridLines = $true; $lnList.HideSelection = $false
$lnList.Dock = 'Fill'
foreach ($c in @(@('What', 92), @('Kind', 50), @('Where (x, z)', 92), @('Bots now', 92), @('Count', 52), @('Last', 104))) { [void]$lnList.Columns.Add($c[0], $c[1]) }
$lnInfo = New-Object Windows.Forms.TextBox; $lnInfo.Multiline = $true; $lnInfo.ReadOnly = $true; $lnInfo.Dock = 'Bottom'; $lnInfo.Height = 64; $lnInfo.ScrollBars = 'Vertical'
$lnInfo.Text = 'Pick an entry to see what it makes the bots do. Tick entries and delete them to make the bots forget.'
$lnButtons = New-Object Windows.Forms.Panel; $lnButtons.Dock = 'Bottom'; $lnButtons.Height = 74
$lnDel = New-Button 'Delete ticked' 4 4 120 30; $lnDel.Font = $Bold
$lnAll = New-Button 'Tick all shown' 128 4 110 30; $lnNone = New-Button 'Untick all' 242 4 90 30
$lnDelMap = New-Button 'Forget this map...' 336 4 140 30
$lnDelHeat = New-Button 'Forget killing grounds' 4 38 160 30
$lnGlass = New-Button 'Forget glass and panels...' 168 38 190 30
$lnStatus = New-Label '' 364 44
$lnButtons.Controls.AddRange(@($lnDel, $lnAll, $lnNone, $lnDelMap, $lnDelHeat, $lnGlass, $lnStatus))
$lnLeft.Controls.AddRange(@($lnList, $lnInfo, $lnButtons))

# right: the map
if ($script:LmCanvasType) { $lnCanvas = New-Object $script:LmCanvasType } else {
    $lnCanvas = New-Object Windows.Forms.Panel
    try { $lnCanvas.GetType().GetProperty('DoubleBuffered', [Reflection.BindingFlags]'NonPublic,Instance').SetValue($lnCanvas, $true, $null) } catch { }
}
$lnCanvas.Dock = 'Fill'; $lnCanvas.BackColor = [Drawing.Color]::FromArgb(22, 24, 28)
$tabLearn.Controls.AddRange(@($lnCanvas, $lnLeft, $lnBar))

function Ln-Say([string]$t, [bool]$bad = $false) { $lnStatus.Text = $t; $lnStatus.ForeColor = $(if ($bad) { $Red } else { $Green }) }
function Ln-Kind($e) { if ($e.Type -eq 'Spot' -and $e.Kind -eq 'air') { return 'Air' }; return $e.Type }
function Ln-What($e) {
    switch ($e.Type) { 'Spot' { if ($e.Kind -eq 'air') { 'Aircraft lost' } else { 'Stuck spot' } } 'Heat' { 'Deaths' } 'Route' { 'Player route' } 'Pad' { 'Landing spot' } default { $e.Type } }
}
function Ln-Fill-Maps([string]$keep = '') {
    $script:Ln.Files = @(Get-BmLearnFiles $dir)
    $lnMap.BeginUpdate(); $lnMap.Items.Clear()
    foreach ($f in $script:Ln.Files) {
        $name = $(try { Map-Display $f.Level $f.Mode } catch { $f.Key })
        [void]$lnMap.Items.Add(("{0}   ({1} spots, {2} routes, {3} landing, {4} death cells)" -f $name, $f.Spots, $f.Routes, $f.Pads, $f.Heat))
    }
    $lnMap.EndUpdate()
    $g = Get-BmBreakableInfo $dir
    $lnGlass.Text = "Forget glass and panels ($($g.Breakable + $g.Unbreakable))..."
    if (-not $script:Ln.Files.Count) {
        $script:Ln.File = $null; $lnList.Items.Clear(); $lnCanvas.Invalidate()
        Ln-Say ("Nothing learned yet - " + (Get-BmLearnDir $dir) + " fills as bots play (Learning=1).") $false
        return
    }
    $i = 0; for ($k = 0; $k -lt $script:Ln.Files.Count; $k++) { if ($script:Ln.Files[$k].Key -eq $keep) { $i = $k } }
    $lnMap.SelectedIndex = $i
}
function Ln-Load-Image {
    $f = $script:Ln.File; $want = $(if ($f -and $f.Map) { $f.Map.File } else { '' })
    if ($want -eq $script:Ln.ImageFile) { return }
    if ($script:Ln.Image) { $script:Ln.Image.Dispose(); $script:Ln.Image = $null }
    $script:Ln.ImageFile = $want
    if (-not $want) { return }
    $path = Get-LmPicturePath $dir $want
    if (-not $path -or -not (Test-Path -LiteralPath $path)) { return }
    try {
        $bytes = [IO.File]::ReadAllBytes($path); $ms = New-Object IO.MemoryStream(, $bytes); $tmp = [Drawing.Image]::FromStream($ms)
        try { $script:Ln.Image = New-Object Drawing.Bitmap($tmp) } finally { $tmp.Dispose(); $ms.Dispose() }
    } catch { }
}
function Ln-Shown($e) { $k = Ln-Kind $e; return $lnShow.ContainsKey($k) -and $lnShow[$k].Checked }
function Ln-Fill-List {
    $f = $script:Ln.File
    $script:Ln.Filling = $true
    $lnList.BeginUpdate(); $lnList.Items.Clear(); $rows = New-Object Collections.Generic.List[object]
    if ($f) {
        $order = @{ 'Spot' = 0; 'Air' = 1; 'Pad' = 2; 'Route' = 3; 'Heat' = 4 }
        foreach ($e in @($f.Entries | Sort-Object @{ Expression = { $order[(Ln-Kind $_)] } }, @{ Expression = { $_.Seen }; Descending = $true })) {
            if (-not (Ln-Shown $e)) { continue }
            $it = New-Object Windows.Forms.ListViewItem((Ln-What $e))
            [void]$it.SubItems.Add($(if ($e.Type -eq 'Heat') { '' } else { $e.Kind }))
            [void]$it.SubItems.Add(("{0:0}, {1:0}" -f $e.X, $e.Z))
            [void]$it.SubItems.Add($e.Status)
            [void]$it.SubItems.Add($(switch ($e.Type) { 'Spot' { "{0}/{1}/{2}" -f $e.Seen, $e.Passed, $e.Failed } 'Heat' { "{0:0.#}" -f $e.Seen } 'Pad' { "{0}/{1}" -f $e.Seen, $e.Failed } default { "{0}x" -f $e.Seen } }))
            [void]$it.SubItems.Add((Format-BmLearnTime $(if ($e.Last) { $e.Last } else { $e.First })))
            $it.Tag = $e
            $it.ForeColor = $(switch ($e.Status) { 'avoided' { $Red } 'blocked' { $Red } 'killing ground' { $Red } 'kept open' { $Green } 'jump' { $Green } 'landing spot' { $Green } default { $lnList.ForeColor } })
            [void]$lnList.Items.Add($it); $rows.Add($e)
        }
    }
    $lnList.EndUpdate(); $script:Ln.Rows = $rows.ToArray(); $script:Ln.Filling = $false
    if ($f) { Ln-Say ("{0} entr(ies) shown - file saved by the server {1:yyyy-MM-dd HH:mm}" -f $lnList.Items.Count, $f.Written) }
    $lnCanvas.Invalidate()
}
function Ln-Select-Map {
    if ($lnMap.SelectedIndex -lt 0 -or $lnMap.SelectedIndex -ge $script:Ln.Files.Count) { return }
    $script:Ln.File = $script:Ln.Files[$lnMap.SelectedIndex]
    Ln-Load-Image; $script:Ln.Fitted = ''
    Ln-Fill-List
}
function Ln-Reload { $key = $(if ($script:Ln.File) { $script:Ln.File.Key } else { '' }); Ln-Fill-Maps $key }
function Ln-Bounds {
    $f = $script:Ln.File
    if ($f -and $f.Map -and $script:Ln.Image) { return @($f.Map.MinX, $f.Map.MinZ, $f.Map.MaxX, $f.Map.MaxZ) }
    $xs = @(); $zs = @()
    if ($f) { foreach ($e in $f.Entries) { $xs += $e.X; $zs += $e.Z; if ($e.Points) { foreach ($p in $e.Points) { $xs += $p[0]; $zs += $p[2] } } } }
    if (-not $xs.Count) { return @(-500.0, -500.0, 500.0, 500.0) }
    $m = $xs | Measure-Object -Minimum -Maximum; $n = $zs | Measure-Object -Minimum -Maximum
    return @(($m.Minimum - 80), ($n.Minimum - 80), ($m.Maximum + 80), ($n.Maximum + 80))
}

# ---- drawing ----
function Ln-Pt([double]$x, [double]$z) { $s = ConvertTo-LmScreen $script:Ln.View $x $z; return (New-Object Drawing.PointF([float]$s[0], [float]$s[1])) }
$lnPens = @{
    'foot' = (New-Object Drawing.Pen([Drawing.Color]::FromArgb(220, 90, 170, 255), 2)); 'ground' = (New-Object Drawing.Pen([Drawing.Color]::FromArgb(220, 255, 160, 60), 3))
    'boat' = (New-Object Drawing.Pen([Drawing.Color]::FromArgb(220, 60, 220, 220), 3)); 'sel' = (New-Object Drawing.Pen([Drawing.Color]::White, 2))
}
function Ln-Status-Color([string]$s) {
    switch ($s) { 'avoided' { [Drawing.Color]::FromArgb(235, 70, 60) } 'blocked' { [Drawing.Color]::FromArgb(235, 70, 60) } 'kept open' { [Drawing.Color]::FromArgb(80, 210, 90) }
        'jump' { [Drawing.Color]::FromArgb(240, 220, 60) } 'landing spot' { [Drawing.Color]::FromArgb(80, 210, 90) } 'more height' { [Drawing.Color]::FromArgb(200, 120, 255) }
        default { [Drawing.Color]::FromArgb(170, 170, 170) } }
}
$lnCanvas.Add_Paint({
    param($sender, $e)
    $g = $e.Graphics; $g.SmoothingMode = 'AntiAlias'; $v = $script:Ln.View
    $v.W = [Math]::Max(100, $lnCanvas.ClientSize.Width); $v.H = [Math]::Max(100, $lnCanvas.ClientSize.Height)
    $v.FlipX = $lmFlipX.Checked; $v.FlipZ = $lmFlipZ.Checked   # (the Live map's orientation)
    $f = $script:Ln.File
    $b = Ln-Bounds; $key = ($b -join ',') + "|$($v.W)x$($v.H)"
    if ($script:Ln.Fitted -ne $key) { Set-LmViewFit $v $b[0] $b[1] $b[2] $b[3]; $script:Ln.Fitted = $key }
    if (-not $f) { $g.DrawString('Nothing learned yet.', $lmFontBig, [Drawing.Brushes]::Gray, 20, 20); return }
    if ($script:Ln.Image -and $f.Map) { $pts = [Drawing.PointF[]]@((Ln-Pt $f.Map.MinX $f.Map.MaxZ), (Ln-Pt $f.Map.MaxX $f.Map.MaxZ), (Ln-Pt $f.Map.MinX $f.Map.MinZ)); $g.DrawImage($script:Ln.Image, $pts) }
    $sel = @{}; foreach ($it in $lnList.SelectedItems) { $sel[$it.Tag.Line] = $true }; foreach ($it in $lnList.CheckedItems) { $sel[$it.Tag.Line] = $true }
    foreach ($en in $script:Ln.Rows) {
        $c = Ln-Status-Color $en.Status; $hot = $sel.ContainsKey($en.Line)
        switch ($en.Type) {
            'Heat' {
                $a = [int][Math]::Min(200, 40 + 160 * [Math]::Min(1.0, $en.Seen / 12.0)); $br = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb($a, 230, 40, 30))
                $p1 = Ln-Pt ($en.X - 5) ($en.Z + 5); $p2 = Ln-Pt ($en.X + 5) ($en.Z - 5)
                $g.FillRectangle($br, [Math]::Min($p1.X, $p2.X), [Math]::Min($p1.Y, $p2.Y), [Math]::Max(2, [Math]::Abs($p2.X - $p1.X)), [Math]::Max(2, [Math]::Abs($p2.Y - $p1.Y))); $br.Dispose()
            }
            'Route' {
                $pen = $(if ($hot) { $lnPens['sel'] } elseif ($lnPens.ContainsKey($en.Kind)) { $lnPens[$en.Kind] } else { $lnPens['foot'] })
                $pts = [Drawing.PointF[]]@($en.Points | ForEach-Object { Ln-Pt $_[0] $_[2] }); if ($pts.Count -ge 2) { $g.DrawLines($pen, $pts) }
            }
            'Pad' {
                $p = Ln-Pt $en.X $en.Z; $br = New-Object Drawing.SolidBrush($c)
                $g.FillEllipse($br, $p.X - 8, $p.Y - 8, 16, 16); $g.DrawString('H', $lmFontFlag, [Drawing.Brushes]::Black, $p.X - 6, $p.Y - 8); $br.Dispose()
                if ($hot) { $g.DrawEllipse($lnPens['sel'], $p.X - 11, $p.Y - 11, 22, 22) }
            }
            'Spot' {
                $p = Ln-Pt $en.X $en.Z; $br = New-Object Drawing.SolidBrush($c)
                if ($en.Kind -eq 'air') {
                    $r = [float](($(if ($lnShow['Air'].Checked) { 150 } else { 0 })) * $v.Scale)
                    if ($r -gt 4) { $pe = New-Object Drawing.Pen([Drawing.Color]::FromArgb(120, $c), 1); $g.DrawEllipse($pe, $p.X - $r, $p.Y - $r, 2 * $r, 2 * $r); $pe.Dispose() }
                    $g.FillPolygon($br, [Drawing.PointF[]]@((New-Object Drawing.PointF($p.X, ($p.Y - 8))), (New-Object Drawing.PointF(($p.X - 7), ($p.Y + 6))), (New-Object Drawing.PointF(($p.X + 7), ($p.Y + 6)))))
                } elseif ($en.Kind -eq 'foot') { $g.FillEllipse($br, $p.X - 4, $p.Y - 4, 8, 8) }
                else { $g.FillRectangle($br, $p.X - 5, $p.Y - 5, 10, 10) }
                if ($hot) { $g.DrawEllipse($lnPens['sel'], $p.X - 9, $p.Y - 9, 18, 18) }
                $br.Dispose()
            }
        }
    }
    $g.DrawString('red: avoided / blocked   green: bots get through, landing spot   yellow: jump   purple: aircraft keep height   grey: still learning', $lmFontSmall, [Drawing.Brushes]::Silver, 8, ($v.H - 18))
})
$lnCanvas.Add_MouseWheel({ param($s, $e) Set-LmViewZoom $script:Ln.View $(if ($e.Delta -gt 0) { 1.25 } else { 0.8 }) $e.X $e.Y; $script:Ln.Fitted = 'zoomed:' + $script:Ln.Fitted; $lnCanvas.Invalidate() })
$lnCanvas.Add_MouseDown({ param($s, $e) if ($e.Button -eq 'Left') { $script:Ln.Drag = @($e.X, $e.Y, $script:Ln.View.CX, $script:Ln.View.CZ); $script:Ln.Moved = $false } })
$lnCanvas.Add_MouseMove({
    param($s, $e)
    $d = $script:Ln.Drag; if (-not $d) { return }
    if ([Math]::Abs($e.X - $d[0]) + [Math]::Abs($e.Y - $d[1]) -lt 4 -and -not $script:Ln.Moved) { return }
    $script:Ln.Moved = $true; $v = $script:Ln.View
    $fx = $(if ($v.FlipX) { -1 } else { 1 }); $fz = $(if ($v.FlipZ) { -1 } else { 1 })
    $v.CX = $d[2] - ($e.X - $d[0]) / ($v.Scale * $fx); $v.CZ = $d[3] + ($e.Y - $d[1]) / ($v.Scale * $fz)
    $script:Ln.Fitted = 'moved:' + $script:Ln.Fitted; $lnCanvas.Invalidate()
})
$lnCanvas.Add_MouseUp({
    param($s, $e)
    $wasDrag = $script:Ln.Moved; $script:Ln.Drag = $null; $script:Ln.Moved = $false
    if ($wasDrag -or $e.Button -ne 'Left') { return }
    # a click picks the nearest entry within 14 pixels (routes: their nearest point)
    $best = -1; $bd = 14 * 14
    for ($k = 0; $k -lt $script:Ln.Rows.Count; $k++) {
        $en = $script:Ln.Rows[$k]
        if ($en.Points) { $cands = $en.Points } else { $cands = @(, @($en.X, 0, $en.Z)) }
        foreach ($q in $cands) { $p = Ln-Pt $q[0] $q[2]; $dd = ($p.X - $e.X) * ($p.X - $e.X) + ($p.Y - $e.Y) * ($p.Y - $e.Y); if ($dd -lt $bd) { $bd = $dd; $best = $k } }
    }
    if ($best -ge 0) { $lnList.SelectedItems.Clear(); $lnList.Items[$best].Selected = $true; $lnList.EnsureVisible($best); $lnList.Focus() }
})
$lnList.Add_SelectedIndexChanged({
    if ($lnList.SelectedItems.Count) { $en = $lnList.SelectedItems[0].Tag; $lnInfo.Text = "{0} at ({1:0}, {2:0}, {3:0}): {4}" -f (Ln-What $en), $en.X, $en.Y, $en.Z, $en.Effect }
    $lnCanvas.Invalidate()
})
$lnList.Add_ItemChecked({ if (-not $script:Ln.Filling) { $lnCanvas.Invalidate() } })
$lnMap.Add_SelectedIndexChanged({ Ln-Select-Map })
foreach ($cb in $lnShow.Values) { $cb.Add_CheckedChanged({ Ln-Fill-List }) }
$lnRefresh.Add_Click({ Ln-Reload })
$lnFit.Add_Click({ $script:Ln.Fitted = ''; $lnCanvas.Invalidate() })
$lnOpen.Add_Click({ $d = Get-BmLearnDir $dir; if (-not (Test-Path -LiteralPath $d)) { [void](New-Item -ItemType Directory -Path $d) }; Start-Process explorer.exe $d })
$lnAll.Add_Click({ foreach ($it in $lnList.Items) { $it.Checked = $true } })
$lnNone.Add_Click({ foreach ($it in $lnList.Items) { $it.Checked = $false } })
$lnDel.Add_Click({
    $f = $script:Ln.File; $pick = @($lnList.CheckedItems | ForEach-Object { $_.Tag })
    if (-not $f -or -not $pick.Count) { Ln-Say 'Tick the entries to delete first.' $true; return }
    try { $n = Remove-BmLearnEntries $f.Path $pick; Ln-Say "$n entr(ies) deleted - a running server merges this at its next save (within a minute)." }
    catch { Ln-Say $_.Exception.Message $true }
    Ln-Reload
})
$lnDelMap.Add_Click({
    $f = $script:Ln.File; if (-not $f) { return }
    $ok = [Windows.Forms.MessageBox]::Show("Forget everything the bots learned on $($f.Key)?`r`n`r`nStuck spots, landing spots, killing grounds and the routes players showed are deleted (an export first keeps a copy).", 'Forget this map', 'YesNo', 'Warning')
    if ($ok -ne 'Yes') { return }
    try { $n = Clear-BmLearnFile $f.Path; Ln-Say "$n entr(ies) deleted for $($f.Key)." } catch { Ln-Say $_.Exception.Message $true }
    Ln-Reload
})
$lnDelHeat.Add_Click({
    $f = $script:Ln.File; if (-not $f) { return }
    try { $n = Clear-BmLearnFile $f.Path @('Heat'); Ln-Say "$n death cell(s) forgotten for $($f.Key)." } catch { Ln-Say $_.Exception.Message $true }
    Ln-Reload
})
$lnGlass.Add_Click({
    $g = Get-BmBreakableInfo $dir
    if (-not ($g.Breakable + $g.Unbreakable)) { Ln-Say 'Nothing learned about glass and panels yet.'; return }
    $ok = [Windows.Forms.MessageBox]::Show("Forget the $($g.Breakable) breakable and $($g.Unbreakable) unbreakable panel material(s) the bots learned (every map)?`r`n`r`nThey learn them again by knifing and shooting panels. The server reads this at its next start.", 'Forget glass and panels', 'YesNo', 'Question')
    if ($ok -ne 'Yes') { return }
    try { Remove-Item -LiteralPath $g.Path -Force; Ln-Say 'Glass and panel learning deleted (after the next server start).' } catch { Ln-Say $_.Exception.Message $true }
    Ln-Reload
})
$lnExport.Add_Click({
    $d = New-Object Windows.Forms.SaveFileDialog; $d.Filter = 'Zip (*.zip)|*.zip'; $d.FileName = 'AltBotMod-learning-' + (Get-Date -Format 'yyyy-MM-dd') + '.zip'
    if ($d.ShowDialog($form) -ne 'OK') { return }
    try { $n = Export-BmLearning $dir $d.FileName; Ln-Say "$n file(s) exported to $($d.FileName)" } catch { Ln-Say $_.Exception.Message $true }
})
$lnImport.Add_Click({
    $d = New-Object Windows.Forms.OpenFileDialog; $d.Filter = 'Learning (*.zip;*.learn)|*.zip;*.learn|All files|*.*'; $d.Multiselect = $true
    $d.Title = 'Import learning (a zip from Export, or .learn files; for a whole folder pick any file in it and choose Yes)'
    if ($d.ShowDialog($form) -ne 'OK') { return }
    $sources = @($d.FileNames)
    if ($sources.Count -eq 1 -and $sources[0] -notmatch '\.(zip|learn)$') {
        $sources = @(Split-Path -Parent $sources[0])
    } elseif ($sources.Count -eq 1 -and $sources[0] -match '\.learn$') {
        $ask = [Windows.Forms.MessageBox]::Show("Import the whole folder (every map learned there) instead of just this file?", 'Import', 'YesNo', 'Question')
        if ($ask -eq 'Yes') { $sources = @(Split-Path -Parent $sources[0]) }
    }
    $t = @(0, 0, 0)
    try { foreach ($s in $sources) { $r = Import-BmLearning $dir $s; for ($k = 0; $k -lt 3; $k++) { $t[$k] += $r[$k] } }; Ln-Say "Imported: $($t[0]) new map file(s), $($t[1]) merged, $($t[2]) entr(ies) added." }
    catch { Ln-Say $_.Exception.Message $true }
    Ln-Reload
})
$tabs.Add_SelectedIndexChanged({ if ($tabs.SelectedTab -eq $tabLearn) { Ln-Reload } })
