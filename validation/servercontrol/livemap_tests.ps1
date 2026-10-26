# Tests LiveMap-Core.ps1 (Server Control's Live map data side) against the files the real livemap_native.inc wrote in
# validation/altbotmod_v13_checks.py (livemap_fixture), plus made-up edge cases. The Live map page itself (WinForms /
# GDI+ drawing) is parse-checked only - there is no Windows desktop in the test environment.
param([string]$Dir)
$ErrorActionPreference = 'Stop'
. (Join-Path $Dir 'ServerControl-Core.ps1')
. (Join-Path $Dir 'LiveMap-Core.ps1')
$fail = 0
function Assert($ok, [string]$what) { if (-not $ok) { Write-Host "FAIL: $what"; $script:fail++ } }
$fixture = Join-Path $PSScriptRoot 'livemap_fixture'

foreach ($f in @('LiveMap-Core.ps1', 'LiveMap-Page.ps1', 'AltBotMod-ServerControl.ps1')) {
    $p = Join-Path $Dir $f; $t = $null; $e = $null
    [void][Management.Automation.Language.Parser]::ParseFile($p, [ref]$t, [ref]$e)
    Assert ($e.Count -eq 0) "$f parses ($($e | Out-String))"
    Assert (-not ([IO.File]::ReadAllBytes($p) | Where-Object { $_ -gt 127 })) "$f is plain ASCII"
}
$gui = [IO.File]::ReadAllText((Join-Path $Dir 'AltBotMod-ServerControl.ps1'))
Assert ($gui -match "\. \(Join-Path \`$PSScriptRoot 'LiveMap-Core.ps1'\)" -and $gui -match "\. \(Join-Path \`$PSScriptRoot 'LiveMap-Page.ps1'\)") 'the window loads the Live map core and page'
Assert ($gui.IndexOf("'LiveMap-Core.ps1'") -lt $gui.IndexOf('Set-StrictMode -Off')) 'the core is loaded before the window switches strict mode off'
Write-Host 'PASS: Live map page and core parse, are plain ASCII, and are loaded by the Server Control window'

# ---- the live picture the core wrote ----
$live = Read-LmLive (Join-Path $fixture 'live_25300.txt')
Assert ($live -and -not $live.Off -and $live.Format -eq 1 -and $live.Version -like 'AltBotMod V[0-9]*') 'live picture header'
Assert ($live.Level -eq 'MP_007' -and $live.Mode -eq 'ConquestLarge0' -and $live.BotCount -eq 3 -and $live.Round -gt 1700000000 -and $live.Kills -eq 3 -and $live.Stucks -eq 3) "time line ($($live.Level) $($live.Mode) $($live.BotCount) $($live.Kills) $($live.Stucks))"
Assert ($live.Picture -and $live.Picture.File -eq 'map_MP_007.bmp' -and $live.Picture.MinX -eq -400 -and $live.Picture.MaxZ -eq 700 -and $live.Picture.W -eq 500) 'map picture line'
Assert ($live.Flags.Count -eq 3 -and $live.Flags[1].Name -eq 'B' -and $live.Flags[1].X -eq 300 -and $live.Flags[1].Owner -eq 2) 'flags'
Assert ($live.Players.Count -eq 4) "four players ($($live.Players.Count))"
$a = $live.Players | Where-Object { $_.Name -eq 'AltBot01Assault' }
Assert ($a.Bot -and $a.Team -eq 1 -and $a.Alive -and $a.X -eq 100 -and $a.Z -eq 150 -and $a.Doing -eq 'attack A' -and $a.HasGoal -and $a.GoalZ -eq 200 -and $a.Class -eq 'Assault') 'bot on foot: position, job, goal, class'
Assert ($a.Route.Count -eq 4 -and $a.Route[0][0] -eq 100 -and $a.Route[0][1] -eq 162 -and $a.Route[3][1] -eq 198) "its route ($($a.Route.Count) points)"
$t = $live.Players | Where-Object { $_.Name -eq 'AltBot02Engineer' }
Assert ($t.Vehicle -eq 'M1A2_Abrams' -and $t.Doing -eq 'driving' -and $t.Route.Count -eq 20 -and $t.Class -eq 'Engineer') 'driver: vehicle, route'
$r = $live.Players | Where-Object { $_.Name -eq 'TestPlayer' }
Assert (-not $r.Bot -and $r.Doing -eq 'player' -and -not $r.HasGoal -and $r.Route.Count -eq 0 -and $r.Vehicle -eq '') 'a person'
Write-Host 'PASS: live picture - header, map / mode / round, map picture bounds, flags with owners, players (bot or person, team, place, vehicle, what each bot is doing, its goal and route, class from the bot name)'

$tmp = Join-Path ([IO.Path]::GetTempPath()) ('lm-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$half = Join-Path $tmp 'half.txt'; $text = [IO.File]::ReadAllText((Join-Path $fixture 'live_25300.txt'))
[IO.File]::WriteAllText($half, $text.Substring(0, $text.IndexOf('p' + "`t" + 'AltBot03')))
Assert ($null -eq (Read-LmLive $half)) 'a half-written picture is not used'
[IO.File]::WriteAllText($half, "AltBotMod live map`t1`tAltBotMod V13 - Live Map`r`noff`r`nend`r`n")
$o = Read-LmLive $half; Assert ($o -and $o.Off -and $o.Players.Count -eq 0) 'switched off'
[IO.File]::WriteAllText($half, "something else`r`nend`r`n"); Assert ($null -eq (Read-LmLive $half)) 'not a live map file'
Assert ($null -eq (Read-LmLive (Join-Path $tmp 'none.txt'))) 'no file yet'
[IO.File]::WriteAllText($half, "AltBotMod live map`t1`tX`r`np`tshort`r`nflag`tA`r`nend`r`n"); $o = Read-LmLive $half
Assert ($o -and $o.Players.Count -eq 0 -and $o.Flags.Count -eq 0) 'short / damaged lines are skipped'
# a file held open by the server while it is read
$fs = New-Object IO.FileStream((Join-Path $tmp 'held.txt'), 'Create', 'ReadWrite', 'ReadWrite')
$w = New-Object IO.StreamWriter($fs); $w.Write($text); $w.Flush()
$o = Read-LmLive (Join-Path $tmp 'held.txt'); $w.Dispose()
Assert ($o -and $o.Players.Count -eq 4) 'read while the server holds the file open'
Write-Host 'PASS: live picture edge cases - half written (no end line), switched off, not ours, missing, damaged lines, file held open by the server'

Assert $script:LmFast 'the compiled history reader is available (Add-Type)'
foreach ($slow in @($false, $true)) {
$kind = $(if ($slow) { 'PowerShell' } else { 'compiled' })
# ---- the event history ----
$feed = New-LmFeed -Slow:$slow
$n = Update-LmFeed $feed (Join-Path $fixture 'events_25300.txt')
Assert ($n -gt 30 -and $feed.Changed) "events read ($n lines)"
Assert ($feed.Kills.Count -eq 3 -and $feed.Stucks.Count -eq 3 -and $feed.Rounds.Count -eq 2) "kills / stuck / rounds ($($feed.Kills.Count) / $($feed.Stucks.Count) / $($feed.Rounds.Count))"
$k = $feed.Kills[0]
Assert ($k.Killer -eq 'TestPlayer' -and -not $k.KillerBot -and $k.Victim -eq 'AltBot03Recon' -and $k.VictimBot -and $k.Weapon -eq 'M16A4' -and $k.Headshot -and $k.Distance -eq 350 -and $k.VX -eq -150 -and -not $k.Suicide -and -not $k.TeamKill) 'a kill: who, whom, weapon (path cut), head shot, distance, where'
Assert ($feed.Kills[2].Suicide -and $feed.Kills[2].Weapon -eq 'out of bounds') 'the world''s kill: a suicide, readable weapon'
Assert ((Update-LmFeed $feed (Join-Path $fixture 'events_25300.txt')) -eq 0 -and -not $feed.Changed) 'nothing new: nothing read'
$round1 = $feed.Rounds[0].Round
$kills = Get-LmHeat $feed 'MP_007' 'kills' 0; $deaths = Get-LmHeat $feed 'MP_007' 'deaths' 0
Assert ($kills.Count -eq 2 -and $deaths.Count -eq 3) "kill heat leaves out the world's kill ($($kills.Count) / $($deaths.Count) cells)"
Assert ((Get-LmHeat $feed 'MP_007' 'deaths' $round1).Count -eq 3 -and (Get-LmHeat $feed 'MP_011' 'deaths' 0).Count -eq 0 -and (Get-LmHeat $feed 'MP_007' 'kills' 12345).Count -eq 0) 'heat per round / per map'
$traffic = Get-LmHeat $feed 'MP_007' 'traffic' 0
$sum = 0; foreach ($v in $traffic.Values) { $sum += $v }
Assert ($traffic.Count -eq 4 -and $sum -ge 28) "traffic heat: 4 spots, every move line counted ($sum)"
$key = Get-LmCellKey 100 150; $c = ConvertFrom-LmCellKey $key
Assert ($c[0] -eq 100 -and $c[1] -eq 150 -and (Get-LmCellKey -0.5 -0.5) -ne (Get-LmCellKey 0.5 0.5) -and (ConvertFrom-LmCellKey (Get-LmCellKey -0.5 -0.5))[0] -eq -10) 'heat cells: 10 m, negative coordinates round down'
$board = Get-LmScoreboard $feed 'MP_007' 0
$rb = $board | Where-Object { $_.Name -eq 'TestPlayer' }; $en = $board | Where-Object { $_.Name -eq 'AltBot02Engineer' }; $as = $board | Where-Object { $_.Name -eq 'AltBot01Assault' }
Assert ($rb.Kills -eq 1 -and $rb.Deaths -eq 1 -and $rb.Headshots -eq 1 -and $rb.Weapon -eq 'M16A4' -and $rb.Longest -eq 350 -and -not $rb.Bot -and $rb.Team -eq 1) 'scoreboard: the person'
Assert ($en.Kills -eq 1 -and $en.Deaths -eq 0 -and $as.Kills -eq 0 -and $as.Deaths -eq 1 -and $board.Count -eq 4 -and $board[0].Kills -eq 1) 'scoreboard: bots, the world counts as a death only, most kills first'
$spots = Get-LmStuckSpots $feed 'MP_007' 0
Assert ($spots.Count -eq 2 -and $spots[0].Count -eq 2 -and $spots[0].X -eq 500 -and $spots[0].Kinds -eq 'no progress, vehicle' -and $spots[0].Bots -eq 1 -and $spots[1].Count -eq 1) 'stuck spots: grouped within 15 m, most often first, how and how many bots'
Write-Host "PASS: ($kind reader) history - kills (who, whom, weapon, head shot, distance, places; suicides and team kills told apart), stuck reports, rounds; heat per layer (kills where the killer stood, deaths, stuck, traffic) per round or per map in 10 m cells; scoreboard; stuck spots grouped"

# reading in small pieces gives the same result (lines cut between reads are put back together)
$copy = Join-Path $tmp 'events_25300.txt'; Copy-Item (Join-Path $fixture 'events_25300.txt') $copy
$f2 = New-LmFeed -Slow:$slow; $reads = 0; while ((Update-LmFeed $f2 $copy 37) -ge 0 -and $f2.Offset -lt (Get-Item $copy).Length) { $reads++; if ($reads -gt 100000) { break } }
Assert ($f2.Kills.Count -eq 3 -and $f2.Stucks.Count -eq 3 -and $f2.Lines -eq $feed.Lines -and $reads -gt 50) "37 bytes at a time: same lines ($($f2.Lines) vs $($feed.Lines), $reads reads)"
# appended lines are picked up, a half-written last line waits for its end
$rid = $feed.Rounds[1].Round
[IO.File]::AppendAllText($copy, "kill`t1791700000`t$rid`tMP_007`tAltBot01Assault`t1`t1`t10.0`t20.0`tAltBot02Engineer`t1`t2`t30.0`t40.0`tM16A4`t0`t28`r`nstuck`t1791700001`t$rid`tMP_007`tAltBot01Ass")
Assert ((Update-LmFeed $f2 $copy) -eq 1 -and $f2.Kills.Count -eq 4 -and $f2.Stucks.Count -eq 3) 'appended kill read; the half line waits'
[IO.File]::AppendAllText($copy, "ault`t1`t12.0`t22.0`ton foot`r`n")
Assert ((Update-LmFeed $f2 $copy) -eq 1 -and $f2.Stucks.Count -eq 4 -and $f2.Stucks[3].Name -eq 'AltBot01Assault' -and $f2.Stucks[3].Kind -eq 'on foot') 'the rest of the line completes it'
# the server rolled the file over (events_<port>.old.txt): reading starts again at the top, nothing counted twice
[IO.File]::WriteAllText($copy, "round`t1791700100`t1791700100`tMP_011`tConquestLarge0`r`n")
Assert ((Update-LmFeed $f2 $copy) -eq 1 -and $f2.Rounds.Count -eq 3 -and $f2.Kills.Count -eq 4) 'roll-over: new file read from the start, history kept'
# a long history: only its end is read when the page opens, starting at a whole line
$big = Join-Path $tmp 'big.txt'; $sb = New-Object Text.StringBuilder
for ($i = 0; $i -lt 3000; $i++) { [void]$sb.Append("move`t1791700000`t5`tMP_001`t1`t1`t$i`t0`t0`r`n") }
[IO.File]::WriteAllText($big, $sb.ToString())
$old = $script:LmFirstRead; $script:LmFirstRead = 4000
$f3 = New-LmFeed -Slow:$slow; while ((Update-LmFeed $f3 $big 1000) -gt 0) { }
$script:LmFirstRead = $old
$cells = Get-LmHeat $f3 'MP_001' 'traffic' 0; $total = 0; foreach ($v in $cells.Values) { $total += $v }
Assert ($f3.Skipped -and $total -gt 50 -and $total -lt 4000 / 30 + 2 -and $f3.Lines -eq $total) "long history: only the last part read, whole lines only ($total lines)"
Write-Host "PASS: ($kind reader) reading the history as it grows - pieces of any size, appended lines, a half-written line completed later, the server rolling the file over, a long file read from its last 8 MB only"
}
# the two readers agree on a bigger history, and the compiled one is quick
$bigEv = Join-Path $tmp 'agree.txt'; $sb = New-Object Text.StringBuilder; $rnd = New-Object Random 7
for ($i = 0; $i -lt 4000; $i++) {
    $x = $rnd.Next(-900, 900) + 0.5; $z = $rnd.Next(-900, 900) - 0.5
    switch ($i % 8) {
        0 { [void]$sb.Append("kill`t$(1791700000 + $i)`t$(1 + $i % 3)`tMP_00$($i % 2)`tK$($i % 13)`t1`t$(1 + $i % 2)`t$x`t$z`tV$($i % 11)`t1`t$(1 + ($i / 8) % 2)`t$z`t$x`tWeapons/A/AK74M`t$(if ($i % 3 -eq 0) { 1 } else { 0 })`t$($i % 400)`r`n") }
        1 { [void]$sb.Append("stuck`t$(1791700000 + $i)`t$(1 + $i % 3)`tMP_00$($i % 2)`tB$($i % 7)`t2`t$x`t$z`ton foot`r`n") }
        2 { [void]$sb.Append("round`t$(1791700000 + $i)`t$(1 + $i % 3)`tMP_00$($i % 2)`tConquestLarge0`r`n") }
        default { [void]$sb.Append("move`t$(1791700000 + $i)`t$(1 + $i % 3)`tMP_00$($i % 2)`t1`t1`t$x`t$z`t0`r`n") }
    }
}
[IO.File]::WriteAllText($bigEv, $sb.ToString())
$fa = New-LmFeed; $fb = New-LmFeed -Slow
$sw = [Diagnostics.Stopwatch]::StartNew(); [void](Update-LmFeed $fa $bigEv 64MB); $fastMs = $sw.ElapsedMilliseconds
$sw.Restart(); [void](Update-LmFeed $fb $bigEv 64MB); $slowMs = $sw.ElapsedMilliseconds
$same = $fa.Kills.Count -eq $fb.Kills.Count -and $fa.Stucks.Count -eq $fb.Stucks.Count -and $fa.Rounds.Count -eq $fb.Rounds.Count -and $fa.Lines -eq $fb.Lines -and $fa.EventsVersion -eq $fb.EventsVersion
foreach ($lv in @('MP_000', 'MP_001')) { foreach ($layer in @('kills', 'deaths', 'stuck', 'traffic')) { foreach ($rd in @(0, 1, 2)) {
    $ha = Get-LmHeat $fa $lv $layer $rd; $hb = Get-LmHeat $fb $lv $layer $rd
    if ($ha.Count -ne $hb.Count) { $same = $false }
    foreach ($e in $hb.GetEnumerator()) { $n = 0; if (-not $ha.TryGetValue($e.Key, [ref]$n) -or $n -ne $e.Value) { $same = $false } }
} } }
for ($i = 0; $i -lt $fa.Kills.Count; $i += 37) { foreach ($prop in @('Killer', 'Victim', 'Weapon', 'Headshot', 'Suicide', 'TeamKill', 'KX', 'VZ', 'Distance', 'Round')) { if ($fa.Kills[$i].$prop -ne $fb.Kills[$i].$prop) { $same = $false } } }
Assert $same 'compiled and PowerShell readers give the same kills, stuck reports, rounds and heat'
$traffic2 = Get-LmHeat $fa 'MP_001' 'traffic' 0
$pa = Get-LmHeatPixels $traffic2; $script:LmFast = $false; $pb = Get-LmHeatPixels $traffic2; $script:LmFast = $true
$px = $pa.W -eq $pb.W -and $pa.H -eq $pb.H -and $pa.MinX -eq $pb.MinX -and $pa.MinZ -eq $pb.MinZ -and $pa.Top -eq $pb.Top
for ($i = 0; $i -lt $pa.Argb.Length; $i++) { if ($pa.Argb[$i] -ne $pb.Argb[$i]) { $px = $false; break } }
Assert $px 'compiled and PowerShell heat pixels are identical'
Assert ($fastMs * 10 -lt $slowMs) "compiled reader is at least 10x quicker ($fastMs ms vs $slowMs ms for 4000 lines)"
Write-Host "PASS: the compiled and PowerShell readers agree line for line (kills, stuck, rounds, heat per map / layer / round, heat pixels); compiled $fastMs ms vs PowerShell $slowMs ms for 4000 lines"

# ---- drawing helpers ----
$v = New-LmView; $v.W = 800; $v.H = 600
Set-LmViewFit $v -400 -300 600 700
Assert ([Math]::Abs($v.Scale - 0.582) -lt 0.001 -and $v.CX -eq 100 -and $v.CZ -eq 200) "fit: whole map in view (scale $($v.Scale))"
$s = ConvertTo-LmScreen $v 100 700; Assert ([Math]::Abs($s[0] - 400) -lt 0.01 -and $s[1] -lt 300) 'north is up'
$s = ConvertTo-LmScreen $v 600 200; Assert ($s[0] -gt 400) 'east is right'
foreach ($flip in @(@($false, $false), @($true, $false), @($false, $true), @($true, $true))) {
    $v.FlipX = $flip[0]; $v.FlipZ = $flip[1]
    $s = ConvertTo-LmScreen $v 123.5 -45.25; $w2 = ConvertFrom-LmScreen $v $s[0] $s[1]
    Assert ([Math]::Abs($w2[0] - 123.5) -lt 0.001 -and [Math]::Abs($w2[1] + 45.25) -lt 0.001) "screen <-> world round trip (flip $flip)"
}
$v.FlipZ = $true; $s = ConvertTo-LmScreen $v 100 700; Assert ($s[1] -gt 300) 'flipped: north is down'
$v.FlipX = $false; $v.FlipZ = $false
$under = ConvertFrom-LmScreen $v 650 120; Set-LmViewZoom $v 1.25 650 120; $after = ConvertFrom-LmScreen $v 650 120
Assert ([Math]::Abs($under[0] - $after[0]) -lt 0.001 -and [Math]::Abs($under[1] - $after[1]) -lt 0.001 -and [Math]::Abs($v.Scale - 0.582 * 1.25) -lt 0.002) 'zoom keeps the point under the mouse'
for ($i = 0; $i -lt 60; $i++) { Set-LmViewZoom $v 2 0 0 }; Assert ($v.Scale -eq 40) 'zoom limit'
$b = Get-LmBounds $live; Assert ($b[0] -eq -400 -and $b[3] -eq 700) 'bounds: the map picture'
$noPic = Read-LmLive (Join-Path $fixture 'live_25300.txt'); $noPic.Picture = $null; $b = Get-LmBounds $noPic
Assert ($b[0] -eq (-200 - 80) -and $b[1] -eq (-50 - 80) -and $b[2] -eq (500 + 80) -and $b[3] -eq (600 + 80)) "bounds without a picture: flags and soldiers + 80 m ($($b -join ', '))"
Assert ((Get-LmBounds $null)[2] -eq 500) 'bounds with nothing at all'
$prevA = -1; $ok = $true; foreach ($x in 1..20) { $col = Get-LmHeatColor ($x / 20); if ($col[0] -lt $prevA -or $col[0] -gt 255) { $ok = $false }; $prevA = $col[0] }
Assert ($ok -and (Get-LmHeatColor 0)[0] -eq 0 -and (Get-LmHeatColor 1)[1] -eq 255 -and (Get-LmHeatColor 5)[1] -eq 255) 'heat colours: clear at 0, more opaque as it gets hotter, red at the top'
$hg = Get-LmHeatGrid $traffic
Assert ($hg -and $hg.Top -ge 7 -and $hg.T.Length -eq $hg.W * $hg.H -and ($hg.T | Measure-Object -Maximum).Maximum -eq 1 -and $hg.MinX -eq -150 -and $hg.MinZ -eq 150) "heat grid ($($hg.W) x $($hg.H) cells from $($hg.MinX), $($hg.MinZ))"
Assert ($null -eq (Get-LmHeatGrid (New-Object 'System.Collections.Generic.Dictionary[long,int]'))) 'no heat: no grid'
Assert ((Get-LmWeapon 'Weapons/XP1_L85A2/L85A2') -eq 'L85A2' -and (Get-LmWeapon 'U_M9') -eq 'M9' -and (Get-LmWeapon 'SoldierCollision') -eq 'run over' -and (Get-LmWeapon '') -eq '?') 'weapon names'
Assert ((Get-LmClass 'AltBot17Support') -eq 'Support' -and (Get-LmClass 'TestPlayer') -eq '') 'class from the bot name'
Write-Host 'PASS: drawing helpers - fit, north up / east right, flips, screen <-> world, zoom at the mouse with limits, bounds with / without a map picture, heat colours and grid, weapon and class names'

# ---- the switch (per server, through its own settings file, applied at once) ----
$root = Join-Path $tmp 'bf'; $srv = Join-Path $root 'Battlefield_3_Server'; New-Item -ItemType Directory -Force -Path $srv, (Join-Path $root 'BotMod') | Out-Null
$inst = [pscustomobject]@{ GamePort = 25301; Label = 'Server 2' }
Assert (Get-LmEnabled $srv $inst) 'on by default'
Set-LmEnabled $srv $inst $false
$ini = [IO.File]::ReadAllText((Join-Path $root 'BotMod\enhanced_25301.ini'.Replace('\', [IO.Path]::DirectorySeparatorChar)))
$cmd = [IO.File]::ReadAllText((Join-Path $srv 'BotMod_25301.cmd'))
Assert ($ini -match '(?m)^LiveMap=0\r?$' -and $cmd -match 'ehreload' -and -not (Get-LmEnabled $srv $inst)) 'off: LiveMap=0 in enhanced_<port>.ini and the server told to re-read its settings'
Set-BmSettings $srv $null @{ LiveMap = '0' }
Set-LmEnabled $srv $inst $true
Assert ((Get-LmEnabled $srv $inst) -and ([IO.File]::ReadAllText((Join-Path $srv 'BotMod_25301.cmd')) -split 'ehreload').Count -eq 3) 'on again for this server even with the all-servers file off'
Assert ((Get-LmLivePath $srv $inst) -like '*BotMod*LiveMap*live_25301.txt' -and (Get-LmPicturePath $srv 'map_MP_007.bmp') -like '*LiveMap*map_MP_007.bmp' -and $null -eq (Get-LmPicturePath $srv '..\x.bmp')) 'file places (a picture name with a path is refused)'
$set = $script:BmSettings | Where-Object { $_.Group -eq 'Live map' }
Assert ($set.Count -eq 4 -and ($set | Where-Object { $_.Key -eq 'LiveMap' }).Default -eq '1') 'the four Live map settings are in the bot settings window'
Write-Host 'PASS: Live map switch - per server (LiveMap in enhanced_<port>.ini, overriding the all-servers file), applied at once through the settings-only reload; file places; the settings window lists the four Live map settings'

Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
if ($fail) { Write-Host "$fail check(s) failed"; exit 1 }
Write-Host 'PASS: Server Control Live map core'
