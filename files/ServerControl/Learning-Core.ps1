# AltBotMod Server Control - learning, the data side (AltBotMod V21). What the bots learned is kept next to the BF3
# server in BotMod_learned\ (one <Level>_<Mode>.learn text file per map and mode, plus breakable.txt for glass and
# panels). The server reads a file when the map loads and saves it every minute; when the file changed on disk it
# merges: what was deleted here stays deleted, what bots learned meanwhile is kept. These functions read the files,
# say what each entry makes the bots do, delete entries, and export / import whole files (to carry learning to
# another install or PC - a new AltBotMod version keeps the folder by itself).
# Dot-sourced by AltBotMod-ServerControl.ps1 (and by the tests, without any window).

function Get-BmLearnDir([string]$serverDir) { return Join-Path $serverDir 'BotMod_learned' }
function ConvertTo-BmLearnNumber([string]$t) { $v = 0.0; if ([double]::TryParse($t, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$v)) { return $v }; return [double]::NaN }
function Format-BmLearnTime([long]$unix) {
    if ($unix -le 0) { return '' }
    try { return ([DateTimeOffset]::FromUnixTimeSeconds($unix)).LocalDateTime.ToString('yyyy-MM-dd HH:mm') } catch { return '' }
}

# One entry (line) of a learned file, with what it means. Types: Spot, Heat, Route, Pad, Map, Other.
function ConvertFrom-BmLearnLine([string]$line) {
    $w = $line -split "`t"
    $n = { param($s) ConvertTo-BmLearnNumber $s }
    switch ($w[0]) {
        'spot' {
            if ($w.Count -lt 3) { break }
            $p = @($w[2] -split ' '); $c = @(); if ($w.Count -ge 5) { $c = @($w[4] -split ' ') }
            $t = @(); if ($w.Count -ge 6) { $t = @($w[5] -split ' ') }
            $seen = $(if ($c.Count -ge 1) { [int](& $n $c[0]) } else { 1 }); $passed = $(if ($c.Count -ge 2) { [int](& $n $c[1]) } else { 0 })
            $failed = $(if ($c.Count -ge 3) { [int](& $n $c[2]) } else { 0 }); $jumps = $(if ($c.Count -ge 4) { [int](& $n $c[3]) } else { 0 })
            $r = [pscustomobject]@{ Type = 'Spot'; Kind = $w[1]; X = (& $n $p[0]); Y = (& $n $p[1]); Z = (& $n $p[2]); Seen = $seen; Passed = $passed; Failed = $failed; Jumps = $jumps
                First = $(if ($t.Count -ge 1) { [long](& $n $t[0]) } else { 0 }); Last = $(if ($t.Count -ge 2) { [long](& $n $t[1]) } else { 0 }); Line = $line; Points = $null; Effect = ''; Status = '' }
            Set-BmLearnEffect $r; return $r
        }
        'heat' {
            if ($w.Count -lt 3) { break }
            $c = @($w[1] -split ' '); $d = & $n $w[2]
            $r = [pscustomobject]@{ Type = 'Heat'; Kind = 'deaths'; X = (& $n $c[0]) * 10 + 5; Y = 0.0; Z = (& $n $c[1]) * 10 + 5; Seen = $d; Passed = 0; Failed = 0; Jumps = 0
                First = 0; Last = 0; Line = $line; Points = $null; Effect = ''; Status = '' }
            Set-BmLearnEffect $r; return $r
        }
        'route' {
            if ($w.Count -lt 4) { break }
            $u = @($w[2] -split ' '); $pts = New-Object Collections.Generic.List[object]
            foreach ($xyz in ($w[3] -split ' ')) { $q = $xyz -split ','; if ($q.Count -eq 3) { $pts.Add(@((& $n $q[0]), (& $n $q[1]), (& $n $q[2]))) } }
            if ($pts.Count -lt 2) { break }
            $len = 0.0; for ($k = 1; $k -lt $pts.Count; $k++) { $dx = $pts[$k][0] - $pts[$k - 1][0]; $dy = $pts[$k][1] - $pts[$k - 1][1]; $dz = $pts[$k][2] - $pts[$k - 1][2]; $len += [Math]::Sqrt($dx * $dx + $dy * $dy + $dz * $dz) }
            $r = [pscustomobject]@{ Type = 'Route'; Kind = $w[1]; X = $pts[0][0]; Y = $pts[0][1]; Z = $pts[0][2]; Seen = [int](& $n $u[0]); Passed = 0; Failed = 0; Jumps = 0
                First = $(if ($u.Count -ge 2) { [long](& $n $u[1]) } else { 0 }); Last = 0; Line = $line; Points = $pts.ToArray(); Length = $len; Effect = ''; Status = '' }
            Set-BmLearnEffect $r; return $r
        }
        'pad' {
            if ($w.Count -lt 3) { break }
            $p = @($w[2] -split ' '); $c = @(); if ($w.Count -ge 4) { $c = @($w[3] -split ' ') }; $t = @(); if ($w.Count -ge 5) { $t = @($w[4] -split ' ') }
            $r = [pscustomobject]@{ Type = 'Pad'; Kind = $w[1]; X = (& $n $p[0]); Y = (& $n $p[1]); Z = (& $n $p[2]); Seen = $(if ($c.Count) { [int](& $n $c[0]) } else { 1 }); Passed = 0
                Failed = $(if ($c.Count -ge 2) { [int](& $n $c[1]) } else { 0 }); Jumps = 0
                First = $(if ($t.Count) { [long](& $n $t[0]) } else { 0 }); Last = $(if ($t.Count -ge 2) { [long](& $n $t[1]) } else { 0 }); Line = $line; Points = $null; Effect = ''; Status = '' }
            Set-BmLearnEffect $r; return $r
        }
        'map' {
            if ($w.Count -lt 4) { break }
            $b = @($w[2] -split ' '); $s = @(); if ($w.Count -ge 4) { $s = @($w[3] -split ' ') }
            if ($b.Count -lt 4) { break }
            return [pscustomobject]@{ Type = 'Map'; File = $w[1]; MinX = (& $n $b[0]); MinZ = (& $n $b[1]); MaxX = (& $n $b[2]); MaxZ = (& $n $b[3]); Line = $line }
        }
    }
    return [pscustomobject]@{ Type = 'Other'; Kind = ''; Line = $line }
}
# What an entry makes the bots do - the same rules as the server (learn.c bl_spot_bad / passable / jump).
function Set-BmLearnEffect($r) {
    switch ($r.Type) {
        'Spot' {
            $foot = $r.Kind -eq 'foot'
            $bad = $r.Passed -eq 0 -and $r.Failed -ge $(if ($foot) { 3 } else { 2 })
            $passable = $r.Passed -ge 1 -and $r.Passed -ge $r.Failed
            $jump = $r.Passed -ge 1 -and $r.Jumps * 2 -ge $r.Passed
            if ($r.Kind -eq 'air') {
                $r.Status = 'more height'
                $r.Effect = "aircraft lost low here ({0}x): jets keep {1} m more height within 500 m, helicopters {2} m within 150 m" -f $r.Failed, [Math]::Min(90, 35 * [Math]::Max(1, $r.Failed)), [Math]::Min(45, 15 * [Math]::Max(1, $r.Failed))
            } elseif ($bad) {
                $r.Status = 'avoided'
                $r.Effect = $(if ($foot) { 'nobody ever got through: the way is closed on the grid, recorded routes split here' } else { 'drivers always stalled here: struck off the driving map' })
            } elseif ($passable -and $jump -and $foot) { $r.Status = 'jump'; $r.Effect = 'bots get through with a jump: they jump on the way in and the way stays open' }
            elseif ($passable) { $r.Status = 'kept open'; $r.Effect = $(if ($foot) { 'bots get through: never blocked again' } else { 'drivers get through here' }) }
            else { $r.Status = 'learning'; $r.Effect = 'stuck here {0}x, through {1}x, still stuck {2}x - not decided yet' -f $r.Seen, $r.Passed, $r.Failed }
        }
        'Heat' {
            $d = [Math]::Min(1.0, $r.Seen / 12.0)
            $r.Status = $(if ($d -ge 0.5) { 'killing ground' } else { 'deaths' })
            $r.Effect = "{0:0.#} death(s) here (fading 20% per map load): routes through it cost up to {1:0.##}x" -f $r.Seen, (1 + 1.5 * $d)
        }
        'Route' {
            $r.Status = 'from players'
            $r.Effect = "a {0:0} m {1} route a player took ({2}x): part of the bots' route network from the next map load" -f $r.Length, $(switch ($r.Kind) { 'foot' { 'walking' } 'boat' { 'boat' } default { 'driving' } }), $r.Seen
        }
        'Pad' {
            $usable = $r.Seen -ge 1 -and $r.Failed -le $r.Seen
            $r.Status = $(if ($usable) { 'landing spot' } else { 'blocked' })
            $r.Effect = $(if ($usable) { "helicopters landed here {0}x: transports try it first" -f $r.Seen } else { "found blocked {0}x, landed {1}x: not used" -f $r.Failed, $r.Seen })
        }
    }
}
# A learned file: header (key, format), the map picture line, entries. $null when it is not one.
function Read-BmLearnFile([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    try { $lines = [IO.File]::ReadAllLines($path) } catch { return $null }
    if (-not $lines.Count -or -not $lines[0].TrimStart([char]0xFEFF).StartsWith('AltBotMod learned')) { return $null }
    $h = $lines[0].TrimStart([char]0xFEFF) -split "`t"
    $key = $(if ($h.Count -ge 3) { $h[2] } else { [IO.Path]::GetFileNameWithoutExtension($path) })
    $f = [pscustomobject]@{ Path = $path; Key = $key; Level = $key; Mode = ''; Format = $(if ($h.Count -ge 2) { $h[1] } else { '1' }); Map = $null
        Entries = (New-Object Collections.Generic.List[object]); Spots = 0; Heat = 0; Routes = 0; Pads = 0; Written = (Get-Item -LiteralPath $path).LastWriteTime }
    if ($key -match '^(.+?)_((?:Conquest|Rush|Squad|TeamDeathMatch|GunMaster|Domination|TankSuperiority|Scavenger|CaptureTheFlag|AirSuperiority).*)$') { $f.Level = $Matches[1]; $f.Mode = $Matches[2] }
    for ($k = 1; $k -lt $lines.Count; $k++) {
        $l = $lines[$k]; if (-not $l -or $l.StartsWith('#')) { continue }
        $e = ConvertFrom-BmLearnLine $l
        switch ($e.Type) { 'Map' { $f.Map = $e } 'Spot' { $f.Entries.Add($e); $f.Spots++ } 'Heat' { $f.Entries.Add($e); $f.Heat++ } 'Route' { $f.Entries.Add($e); $f.Routes++ } 'Pad' { $f.Entries.Add($e); $f.Pads++ } }
    }
    return $f
}
function Get-BmLearnFiles([string]$serverDir) {
    $d = Get-BmLearnDir $serverDir
    if (-not (Test-Path -LiteralPath $d)) { return @() }
    return @(Get-ChildItem -LiteralPath $d -Filter '*.learn' -File | Sort-Object Name | ForEach-Object { Read-BmLearnFile $_.FullName } | Where-Object { $_ })
}
# The same entry in a file read again later: the server moves a spot a little towards where bots really get stuck and
# changes its counts, so entries match by type, kind and place (spots 2.5 m, pads 6 m, death cells exactly, routes by
# kind and both ends within 3 m), not by text.
function Test-BmLearnSame($a, $b) {
    if ($a.Type -ne $b.Type -or $a.Kind -ne $b.Kind) { return $false }
    $dx = $a.X - $b.X; $dz = $a.Z - $b.Z
    switch ($a.Type) {
        'Spot' { $r = $(if ($a.Kind -eq 'air') { 60 } elseif ($a.Kind -eq 'foot') { 2.5 } else { 8 }); return ($dx * $dx + $dz * $dz -le $r * $r) -and [Math]::Abs($a.Y - $b.Y) -le $(if ($a.Kind -eq 'air') { 80 } else { 4 }) }
        'Heat' { return $a.Line.Split("`t")[1] -eq $b.Line.Split("`t")[1] }
        'Pad' { return $dx * $dx + $dz * $dz -le 36 }
        'Route' {
            if ([Math]::Abs($a.Points.Count - $b.Points.Count) -gt 2 -or $dx * $dx + $dz * $dz -gt 9) { return $false }
            $ea = $a.Points[$a.Points.Count - 1]; $eb = $b.Points[$b.Points.Count - 1]
            return (($ea[0] - $eb[0]) * ($ea[0] - $eb[0]) + ($ea[2] - $eb[2]) * ($ea[2] - $eb[2])) -le 9
        }
    }
    return $a.Line -eq $b.Line
}
# Writes lines to a file the way the server does (a temporary file, then a swap): never half a file.
function Write-BmLearnLines([string]$path, $lines) {
    $tmp = $path + '.sc.tmp'
    [IO.File]::WriteAllText($tmp, (($lines -join "`r`n") + "`r`n"), (New-Object Text.UTF8Encoding($false)))
    if (Test-Path -LiteralPath $path) { [IO.File]::Replace($tmp, $path, [NullString]::Value) } else { [IO.File]::Move($tmp, $path) }
}
# Deletes entries from a learned file (read again first: the server may have saved meanwhile). Returns how many went.
function Remove-BmLearnEntries([string]$path, $entries) {
    $entries = @($entries); if (-not $entries.Count) { return 0 }
    for ($try = 0; $try -lt 3; $try++) {
        try {
            $lines = [IO.File]::ReadAllLines($path); $keep = New-Object Collections.Generic.List[string]; $gone = 0
            for ($k = 0; $k -lt $lines.Count; $k++) {
                $l = $lines[$k]
                if ($k -gt 0 -and $l -and -not $l.StartsWith('#')) {
                    $e = ConvertFrom-BmLearnLine $l
                    if ($e.Type -in @('Spot', 'Heat', 'Route', 'Pad') -and @($entries | Where-Object { Test-BmLearnSame $_ $e }).Count) { $gone++; continue }
                }
                $keep.Add($l)
            }
            Write-BmLearnLines $path $keep
            return $gone
        } catch { Start-Sleep -Milliseconds 300 }   # (the server was writing it this very moment)
    }
    throw "Could not change $path (in use?)"
}
function Clear-BmLearnFile([string]$path, [string[]]$types = @('Spot', 'Heat', 'Route', 'Pad')) {
    $f = Read-BmLearnFile $path; if (-not $f) { return 0 }
    return Remove-BmLearnEntries $path @($f.Entries | Where-Object { $types -contains $_.Type })
}
# Glass / panel materials (breakable.txt: "<material id> b" breaks, "n" does not).
function Get-BmBreakableInfo([string]$serverDir) {
    $p = Join-Path (Get-BmLearnDir $serverDir) 'breakable.txt'
    $o = [pscustomobject]@{ Path = $p; Breakable = 0; Unbreakable = 0 }
    if (Test-Path -LiteralPath $p) { foreach ($l in [IO.File]::ReadAllLines($p)) { if ($l -match '^[0-9a-fA-F]{8} b') { $o.Breakable++ } elseif ($l -match '^[0-9a-fA-F]{8} n') { $o.Unbreakable++ } } }
    return $o
}
# Export: the chosen learned files (all when none chosen) and breakable.txt into a zip.
function Export-BmLearning([string]$serverDir, [string]$zipPath, [string[]]$keys = @()) {
    $d = Get-BmLearnDir $serverDir
    $files = @(Get-ChildItem -LiteralPath $d -File | Where-Object { $_.Name -eq 'breakable.txt' -or ($_.Extension -eq '.learn' -and (-not $keys.Count -or $keys -contains $_.BaseName)) })
    if (-not $files.Count) { throw 'Nothing learned yet to export.' }
    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
    Compress-Archive -LiteralPath @($files.FullName) -DestinationPath $zipPath
    return $files.Count
}
# Import: a zip, a folder (an older install's BotMod_learned or server folder) or single .learn files. A map with
# nothing learned here takes the file as it is; otherwise the entries this install does not have yet are added.
# Returns @(files new, files merged, entries added).
function Import-BmLearning([string]$serverDir, [string]$source) {
    $d = Get-BmLearnDir $serverDir; if (-not (Test-Path -LiteralPath $d)) { [void](New-Item -ItemType Directory -Path $d) }
    $tmp = $null; $src = @()
    if ((Test-Path -LiteralPath $source -PathType Leaf) -and $source -match '\.zip$') {
        $tmp = Join-Path ([IO.Path]::GetTempPath()) ('bm-learn-' + [Guid]::NewGuid().ToString('N'))
        Expand-Archive -LiteralPath $source -DestinationPath $tmp; $src = @(Get-ChildItem -LiteralPath $tmp -Recurse -File)
    } elseif (Test-Path -LiteralPath $source -PathType Container) {
        $sub = Join-Path $source 'BotMod_learned'; if (Test-Path -LiteralPath $sub) { $source = $sub }
        $src = @(Get-ChildItem -LiteralPath $source -File)
        $old = Join-Path (Split-Path -Parent $source) 'BotMod_breakable.txt'; if (Test-Path -LiteralPath $old) { $src += Get-Item -LiteralPath $old }
    } else { $src = @(Get-Item -LiteralPath $source) }
    $new = 0; $merged = 0; $added = 0
    try {
        foreach ($s in $src) {
            if ($s.Name -eq 'breakable.txt' -or $s.Name -eq 'BotMod_breakable.txt') {
                $dst = Join-Path $d 'breakable.txt'; $have = @(); if (Test-Path -LiteralPath $dst) { $have = @([IO.File]::ReadAllLines($dst)) }
                $more = @([IO.File]::ReadAllLines($s.FullName) | Where-Object { $_ -match '^[0-9a-fA-F]{8} [bn]' -and $have -notcontains $_ })
                if ($more.Count) { Write-BmLearnLines $dst @($have + $more); $added += $more.Count }
                continue
            }
            if ($s.Extension -ne '.learn') { continue }
            $in = Read-BmLearnFile $s.FullName; if (-not $in) { continue }
            $dst = Join-Path $d $s.Name
            if (-not (Test-Path -LiteralPath $dst)) { Copy-Item -LiteralPath $s.FullName -Destination $dst; $new++; $added += $in.Entries.Count; continue }
            $here = Read-BmLearnFile $dst; if (-not $here) { continue }
            $lines = New-Object Collections.Generic.List[string]; foreach ($l in [IO.File]::ReadAllLines($dst)) { $lines.Add($l) }
            $n = 0
            foreach ($e in $in.Entries) { if (-not @($here.Entries | Where-Object { Test-BmLearnSame $_ $e }).Count) { $lines.Add($e.Line); $n++ } }
            if (-not $here.Map -and $in.Map) { $lines.Insert(1, $in.Map.Line) }
            if ($n) { Write-BmLearnLines $dst $lines; $merged++; $added += $n }
        }
    } finally { if ($tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue } }
    return @($new, $merged, $added)
}
