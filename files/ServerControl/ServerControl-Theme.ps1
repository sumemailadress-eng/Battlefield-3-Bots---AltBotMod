# AltBotMod Server Control - V16.2 look: Battlefield 3 colours (dark gunmetal, BF3 orange, a cold blue), flat controls.
# Windows PowerShell 5.1, plain ASCII. Dot-sourced by AltBotMod-ServerControl.ps1. Set-BmTheme colours a window and
# everything in it; call it again after adding controls (it is cheap and idempotent).
$Bg = [Drawing.Color]::FromArgb(18, 20, 23)          # window
$Panel = [Drawing.Color]::FromArgb(28, 31, 36)       # pages, groups, bars
$Field = [Drawing.Color]::FromArgb(14, 16, 19)       # text boxes, lists, grids
$Line = [Drawing.Color]::FromArgb(58, 63, 71)        # borders, grid lines
$Fg = [Drawing.Color]::FromArgb(226, 228, 231)       # text
$Dim = [Drawing.Color]::FromArgb(140, 146, 154)      # secondary text
$Accent = [Drawing.Color]::FromArgb(242, 122, 26)    # BF3 orange
$AccentDark = [Drawing.Color]::FromArgb(160, 72, 10)
$Cold = [Drawing.Color]::FromArgb(64, 170, 230)      # BF3 HUD blue
$Btn = [Drawing.Color]::FromArgb(40, 44, 51)
$BtnHover = [Drawing.Color]::FromArgb(56, 61, 70)
$BadCell = [Drawing.Color]::FromArgb(92, 32, 28)     # a value that does not check out
$ChangedCell = [Drawing.Color]::FromArgb(86, 66, 20) # an edited, unsaved value
$Green = [Drawing.Color]::FromArgb(92, 200, 110); $Red = [Drawing.Color]::FromArgb(235, 85, 70); $Grey = $Dim
$Font = New-Object Drawing.Font('Segoe UI', 9)
$Bold = New-Object Drawing.Font('Segoe UI Semibold', 10)
$Big = New-Object Drawing.Font('Segoe UI Semibold', 13)
$Title = New-Object Drawing.Font('Segoe UI Black', 15)

# The tab strip draws its own tabs: dark, the open one with an orange bar.
$script:BmTabDraw = {
    param($s, $e)
    $g = $e.Graphics; $r = $s.GetTabRect($e.Index); $sel = $s.SelectedIndex -eq $e.Index
    $bg = New-Object Drawing.SolidBrush($(if ($sel) { $Panel } else { $Bg })); $g.FillRectangle($bg, $r); $bg.Dispose()
    if ($sel) { $a = New-Object Drawing.SolidBrush($Accent); $g.FillRectangle($a, $r.X, $r.Bottom - 3, $r.Width, 3); $a.Dispose() }
    $sf = New-Object Drawing.StringFormat; $sf.Alignment = 'Center'; $sf.LineAlignment = 'Center'
    $tb = New-Object Drawing.SolidBrush($(if ($sel) { $Fg } else { $Dim }))
    $g.DrawString($s.TabPages[$e.Index].Text.ToUpper(), $s.Font, $tb, [Drawing.RectangleF]$r, $sf); $tb.Dispose(); $sf.Dispose()
}
$script:BmHeaderDraw = {
    param($s, $e)
    $bg = New-Object Drawing.SolidBrush($Panel); $e.Graphics.FillRectangle($bg, $e.Bounds); $bg.Dispose()
    $p = New-Object Drawing.Pen($Line); $e.Graphics.DrawLine($p, $e.Bounds.Right - 1, $e.Bounds.Top + 3, $e.Bounds.Right - 1, $e.Bounds.Bottom - 4); $e.Graphics.DrawLine($p, $e.Bounds.Left, $e.Bounds.Bottom - 1, $e.Bounds.Right, $e.Bounds.Bottom - 1); $p.Dispose()
    $tb = New-Object Drawing.SolidBrush($Accent); $r = New-Object Drawing.RectangleF(($e.Bounds.X + 6), $e.Bounds.Y, ($e.Bounds.Width - 8), $e.Bounds.Height)
    $sf = New-Object Drawing.StringFormat; $sf.LineAlignment = 'Center'; $sf.Trimming = 'EllipsisCharacter'
    $e.Graphics.DrawString($e.Header.Text.ToUpper(), $Font, $tb, $r, $sf); $tb.Dispose(); $sf.Dispose()
}
function Set-BmTheme($c) {
    if (-not $c) { return }
    $t = $c.GetType().Name
    switch ($t) {
        'Form' { $c.BackColor = $Bg; $c.ForeColor = $Fg }
        'TabControl' {
            if ($c.DrawMode -ne 'OwnerDrawFixed') { $c.DrawMode = 'OwnerDrawFixed'; $c.SizeMode = 'Fixed'; $c.ItemSize = New-Object Drawing.Size(132, 30); $c.Add_DrawItem($script:BmTabDraw) }
        }
        'TabPage' { $c.BackColor = $Panel; $c.ForeColor = $Fg; $c.UseVisualStyleBackColor = $false }
        'Panel' { if ($c.Tag -ne 'keep') { $c.BackColor = $Panel; $c.ForeColor = $Fg } }
        'GroupBox' { $c.ForeColor = $Accent; $c.BackColor = $Panel }
        'Button' {
            $c.FlatStyle = 'Flat'; $c.UseVisualStyleBackColor = $false; $c.Cursor = 'Hand'
            $primary = $c.Font -and $c.Font.Bold
            $c.BackColor = $(if ($primary) { $Accent } else { $Btn }); $c.ForeColor = $(if ($primary) { [Drawing.Color]::Black } else { $Fg })
            $c.FlatAppearance.BorderColor = $(if ($primary) { $Accent } else { $Line }); $c.FlatAppearance.BorderSize = 1
            $c.FlatAppearance.MouseOverBackColor = $(if ($primary) { [Drawing.Color]::FromArgb(255, 150, 60) } else { $BtnHover })
            $c.FlatAppearance.MouseDownBackColor = $(if ($primary) { $AccentDark } else { $Line })
        }
        'TextBox' { $c.BackColor = $Field; $c.ForeColor = $Fg; $c.BorderStyle = 'FixedSingle' }
        'ComboBox' { $c.BackColor = $Field; $c.ForeColor = $Fg; $c.FlatStyle = 'Flat' }
        'NumericUpDown' { $c.BackColor = $Field; $c.ForeColor = $Fg; $c.BorderStyle = 'FixedSingle' }
        'ListBox' { $c.BackColor = $Field; $c.ForeColor = $Fg; $c.BorderStyle = 'FixedSingle' }
        'CheckedListBox' { $c.BackColor = $Field; $c.ForeColor = $Fg; $c.BorderStyle = 'FixedSingle' }
        'CheckBox' { $c.ForeColor = $Fg; $c.FlatStyle = 'Flat'; $c.FlatAppearance.BorderColor = $Accent; $c.FlatAppearance.CheckedBackColor = $AccentDark }
        'RadioButton' { $c.ForeColor = $Fg }
        'Label' { if ($c.ForeColor -eq [Drawing.SystemColors]::ControlText -or $c.ForeColor.ToArgb() -eq [Drawing.Color]::Black.ToArgb()) { $c.ForeColor = $Fg } }
        'ListView' {
            $c.BackColor = $Field; $c.ForeColor = $Fg; $c.BorderStyle = 'FixedSingle'
            if (-not $c.OwnerDraw -and $c.View -eq 'Details') {
                $c.OwnerDraw = $true; $c.Add_DrawColumnHeader($script:BmHeaderDraw)
                $c.Add_DrawItem({ param($s, $e) $e.DrawDefault = $true }); $c.Add_DrawSubItem({ param($s, $e) $e.DrawDefault = $true })
            }
            foreach ($it in $c.Items) { if ($it.ForeColor.ToArgb() -eq [Drawing.Color]::Black.ToArgb() -or $it.ForeColor -eq [Drawing.SystemColors]::WindowText) { $it.ForeColor = $Fg } }
        }
        'DataGridView' {
            $c.BackgroundColor = $Field; $c.GridColor = $Line; $c.BorderStyle = 'None'; $c.EnableHeadersVisualStyles = $false
            $c.DefaultCellStyle.BackColor = $Field; $c.DefaultCellStyle.ForeColor = $Fg; $c.DefaultCellStyle.SelectionBackColor = $AccentDark; $c.DefaultCellStyle.SelectionForeColor = [Drawing.Color]::White
            $c.ColumnHeadersDefaultCellStyle.BackColor = $Panel; $c.ColumnHeadersDefaultCellStyle.ForeColor = $Accent; $c.ColumnHeadersBorderStyle = 'Single'
            $c.RowHeadersDefaultCellStyle.BackColor = $Panel; $c.RowHeadersDefaultCellStyle.ForeColor = $Fg
            $c.AlternatingRowsDefaultCellStyle.BackColor = [Drawing.Color]::FromArgb(20, 23, 27)
        }
        'StatusStrip' { $c.BackColor = $Panel; $c.ForeColor = $Fg; $c.SizingGrip = $false; foreach ($i in $c.Items) { $i.ForeColor = $Fg } }
        'MenuStrip' { $c.BackColor = $Panel; $c.ForeColor = $Fg }
    }
    foreach ($child in $c.Controls) { Set-BmTheme $child }
}
# The banner at the top of the main window.
function New-BmBanner([string]$subtitle) {
    $p = New-Object Windows.Forms.Panel; $p.Dock = 'Top'; $p.Height = 52; $p.Tag = 'keep'; $p.BackColor = $Bg
    $p.Add_Paint({
        param($s, $e)
        $g = $e.Graphics; $g.SmoothingMode = 'AntiAlias'; $g.TextRenderingHint = 'ClearTypeGridFit'
        $a = New-Object Drawing.SolidBrush($Accent); $g.FillRectangle($a, 0, ($s.Height - 3), $s.Width, 3)
        $g.FillPolygon($a, [Drawing.Point[]]@((New-Object Drawing.Point(14, 12)), (New-Object Drawing.Point(26, 12)), (New-Object Drawing.Point(20, 38)), (New-Object Drawing.Point(8, 38))))
        $w = New-Object Drawing.SolidBrush($Fg); $g.DrawString('BATTLEFIELD 3', $Title, $w, 34, 6); $w.Dispose()
        $d = New-Object Drawing.SolidBrush($Accent); $g.DrawString('ALTBOTMOD  SERVER CONTROL', $Bold, $d, 37, 30); $d.Dispose(); $a.Dispose()
        $sub = New-Object Drawing.SolidBrush($Dim); $sf = New-Object Drawing.StringFormat; $sf.Alignment = 'Far'
        $g.DrawString([string]$script:BmBannerText, $Font, $sub, (New-Object Drawing.RectangleF(0, 18, ($s.Width - 14), 20)), $sf); $sub.Dispose(); $sf.Dispose()
    })
    $script:BmBannerText = $subtitle
    return $p
}
