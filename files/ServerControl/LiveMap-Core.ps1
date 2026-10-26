# AltBotMod Server Control - Live map core (no UI): reads what the AltBotMod V13 core writes to BotMod\LiveMap
#   live_<port>.txt     where everyone is right now (replaced every LiveMapSeconds)
#   events_<port>.txt   round starts, kills, stuck spots, movement samples (appended; rolls over to .old.txt)
#   map_<level>.bmp     a top-down picture of the map drawn from the bots' navigation grid
# and turns it into what the Live map page draws: soldiers, goals and routes, a kill feed, a scoreboard, stuck spots
# and heatmaps. Works in Windows PowerShell 5.1 and PowerShell 7. Dot-source after ServerControl-Core.ps1.

Set-StrictMode -Version 2

$script:LmCell = 10.0          # heatmap cell (metres)
$script:LmKeepKills = 20000    # kills / stuck spots kept in memory (newest)
$script:LmFirstRead = 8MB      # a long history file: only its last 8 MB are read when the page opens

function Get-LmDir([string]$serverDir) { return Join-Path (Get-BmBotModDir $serverDir) 'LiveMap' }
function Get-LmLivePath([string]$serverDir, $instance) { return Join-Path (Get-LmDir $serverDir) ("live_{0}.txt" -f $instance.GamePort) }
function Get-LmEventsPath([string]$serverDir, $instance) { return Join-Path (Get-LmDir $serverDir) ("events_{0}.txt" -f $instance.GamePort) }
function Get-LmPicturePath([string]$serverDir, [string]$file) { if (-not $file -or $file -match '[\\/:]') { return $null }; return Join-Path (Get-LmDir $serverDir) $file }

# A file the server may be writing or replacing right now: read it shared, $null when it is not there (or busy).
function Read-LmText([string]$path) {
    if (-not $path -or -not (Test-Path -LiteralPath $path)) { return $null }
    try {
        $fs = New-Object IO.FileStream($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
        try { $sr = New-Object IO.StreamReader($fs, [Text.Encoding]::UTF8); return $sr.ReadToEnd() } finally { $fs.Dispose() }
    } catch { return $null }
}
function ConvertTo-LmNumber([string]$t) { $v = 0.0; if ([double]::TryParse($t, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$v)) { return $v }; return 0.0 }
function Get-LmClass([string]$name) {
    foreach ($c in @('Assault', 'Engineer', 'Support', 'Recon')) { if ($name -match $c) { return $c } }
    return ''
}
function Get-LmWeapon([string]$w) {
    if (-not $w -or $w -eq '-') { return '?' }
    $t = ($w -split '/')[-1]; if ($t -match '^U_(.+)$') { $t = $Matches[1] }
    switch ($t) { 'SoldierCollision' { return 'run over' } 'RoadKill' { return 'road kill' } 'DamageArea' { return 'out of bounds' } 'Death' { return 'fell / died' } }
    return $t
}

# ---- the live picture ----
# Returns $null when there is no complete picture yet; .Off when the server has the live map switched off.
function Read-LmLive([string]$path) {
    $text = Read-LmText $path
    if (-not $text) { return $null }
    $lines = $text -split "`r?`n"
    if ($lines.Count -lt 2 -or -not $lines[0].StartsWith("AltBotMod live map`t")) { return $null }
    if (-not ($lines -contains 'end')) { return $null }   # (half a file: try again next time)
    $head = $lines[0] -split "`t"
    $r = [pscustomobject]@{ Format = [int](ConvertTo-LmNumber $head[1]); Version = $(if ($head.Count -gt 2) { $head[2] } else { '' }); Off = $false
        Time = [long]0; Round = [long]0; Level = ''; Mode = ''; BotCount = 0; Frame = [long]0; Kills = 0; Stucks = 0
        Picture = $null; Flags = @(); Players = @() }
    $flags = New-Object System.Collections.ArrayList; $players = New-Object System.Collections.ArrayList
    foreach ($line in $lines) {
        $w = $line -split "`t"
        switch ($w[0]) {
            'off' { $r.Off = $true }
            'time' { if ($w.Count -ge 9) { $r.Time = [long](ConvertTo-LmNumber $w[1]); $r.Round = [long](ConvertTo-LmNumber $w[2]); $r.Level = $w[3]; $r.Mode = $w[4]
                    $r.BotCount = [int](ConvertTo-LmNumber $w[5]); $r.Frame = [long](ConvertTo-LmNumber $w[6]); $r.Kills = [int](ConvertTo-LmNumber $w[7]); $r.Stucks = [int](ConvertTo-LmNumber $w[8]) } }
            'picture' { if ($w.Count -ge 8) { $r.Picture = [pscustomobject]@{ File = $w[1]; MinX = (ConvertTo-LmNumber $w[2]); MinZ = (ConvertTo-LmNumber $w[3])
                        MaxX = (ConvertTo-LmNumber $w[4]); MaxZ = (ConvertTo-LmNumber $w[5]); W = [int](ConvertTo-LmNumber $w[6]); H = [int](ConvertTo-LmNumber $w[7]) } } }
            'flag' { if ($w.Count -ge 5) { [void]$flags.Add([pscustomobject]@{ Name = $w[1]; X = (ConvertTo-LmNumber $w[2]); Z = (ConvertTo-LmNumber $w[3]); Owner = [int](ConvertTo-LmNumber $w[4]) }) } }
            'p' {
                if ($w.Count -lt 15) { continue }
                $route = @()
                if ($w[14] -and $w[14] -ne '-') { $route = @(foreach ($pt in ($w[14] -split ' ')) { $xz = $pt -split ','; if ($xz.Count -eq 2) { ,@((ConvertTo-LmNumber $xz[0]), (ConvertTo-LmNumber $xz[1])) } }) }
                $gx = ConvertTo-LmNumber $w[11]; $gz = ConvertTo-LmNumber $w[12]
                [void]$players.Add([pscustomobject]@{ Name = $w[1]; Bot = ($w[2] -eq '1'); Team = [int](ConvertTo-LmNumber $w[3]); Alive = ($w[4] -eq '1')
                    X = (ConvertTo-LmNumber $w[5]); Y = (ConvertTo-LmNumber $w[6]); Z = (ConvertTo-LmNumber $w[7]); Yaw = (ConvertTo-LmNumber $w[8])
                    Vehicle = $(if ($w[9] -eq '-') { '' } else { $w[9] }); Doing = $w[10]; GoalX = $gx; GoalZ = $gz; HasGoal = ($gx -ne 0 -or $gz -ne 0)
                    Health = (ConvertTo-LmNumber $w[13]); Route = $route; Class = (Get-LmClass $w[1]) })
            }
        }
    }
    $r.Flags = $flags.ToArray(); $r.Players = $players.ToArray()
    return $r
}

# The compiled history reader (C# 5, so Windows PowerShell 5.1's own compiler takes it). Same lines, same results as
# Add-LmEvent / Update-LmFeed below.
$script:LmFeedSource = @'
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
public class AltBotModLmKill {
    public long Time, Round; public string Level, Killer, Victim, Weapon; public bool KillerBot, VictimBot, Headshot, Suicide, TeamKill;
    public int KillerTeam, VictimTeam; public double KX, KZ, VX, VZ, Distance;
}
public class AltBotModLmStuck { public long Time, Round; public string Level, Name, Kind; public int Team; public double X, Z; }
public class AltBotModLmRound { public long Time, Round; public string Level, Mode; }
public class AltBotModLmHeatGrid { public int W, H, Top; public double MinX, MinZ, Cell; public int[] Argb; }
public class AltBotModLmFeed {
    public string Path; public long Offset; public string Rest = ""; public bool SkipPartial, Skipped, Changed; public long Lines; public int EventsVersion;
    public long FirstRead = 8L << 20; public int Keep = 20000; public double Cell = 10;
    public List<AltBotModLmKill> Kills = new List<AltBotModLmKill>();
    public List<AltBotModLmStuck> Stucks = new List<AltBotModLmStuck>();
    public List<AltBotModLmRound> Rounds = new List<AltBotModLmRound>();
    public Dictionary<string, Dictionary<long, int>[]> HeatBy = new Dictionary<string, Dictionary<long, int>[]>();
    static readonly string[] Layers = { "kills", "deaths", "stuck", "traffic" };
    public void Reset() {
        Offset = 0; Rest = ""; SkipPartial = Skipped = Changed = false; Lines = 0; EventsVersion = 0;
        Kills.Clear(); Stucks.Clear(); Rounds.Clear(); HeatBy.Clear();
    }
    public long Key(double x, double z) {
        long cx = (long)Math.Floor(x / Cell) + 32768, cz = (long)Math.Floor(z / Cell) + 32768;
        return cx * 65536 + cz;
    }
    static double Num(string s) { double v; return double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out v) ? v : 0; }
    public static string Weapon(string w) {
        if (string.IsNullOrEmpty(w) || w == "-") return "?";
        int slash = w.LastIndexOf('/'); string t = slash >= 0 ? w.Substring(slash + 1) : w;
        if (t.StartsWith("U_") && t.Length > 2) t = t.Substring(2);
        switch (t) { case "SoldierCollision": return "run over"; case "RoadKill": return "road kill"; case "DamageArea": return "out of bounds"; case "Death": return "fell / died"; }
        return t;
    }
    void AddHeat(string level, long round, int layer, double x, double z) {
        string k = level + "|" + round.ToString(CultureInfo.InvariantCulture);
        Dictionary<long, int>[] set;
        if (!HeatBy.TryGetValue(k, out set)) {
            set = new Dictionary<long, int>[4]; for (int i = 0; i < 4; i++) set[i] = new Dictionary<long, int>();
            HeatBy[k] = set;
        }
        long c = Key(x, z); int n; set[layer].TryGetValue(c, out n); set[layer][c] = n + 1;
    }
    public void AddLine(string line) {
        string[] w = line.TrimEnd('\r').Split('\t');
        switch (w[0]) {
            case "round":
                if (w.Length >= 5) { Rounds.Add(new AltBotModLmRound { Time = (long)Num(w[1]), Round = (long)Num(w[2]), Level = w[3], Mode = w[4] }); EventsVersion++; }
                break;
            case "kill": {
                if (w.Length < 17) return;
                AltBotModLmKill k = new AltBotModLmKill {
                    Time = (long)Num(w[1]), Round = (long)Num(w[2]), Level = w[3], Killer = w[4], KillerBot = w[5] == "1", KillerTeam = (int)Num(w[6]),
                    KX = Num(w[7]), KZ = Num(w[8]), Victim = w[9], VictimBot = w[10] == "1", VictimTeam = (int)Num(w[11]), VX = Num(w[12]), VZ = Num(w[13]),
                    Weapon = Weapon(w[14]), Headshot = w[15] == "1", Distance = Num(w[16]) };
                k.Suicide = k.Killer == "-" || k.Killer == k.Victim;
                k.TeamKill = !k.Suicide && k.KillerTeam != 0 && k.KillerTeam == k.VictimTeam;
                Kills.Add(k); if (Kills.Count > Keep) Kills.RemoveRange(0, Kills.Count - Keep);
                if (!k.Suicide) AddHeat(k.Level, k.Round, 0, k.KX, k.KZ);
                AddHeat(k.Level, k.Round, 1, k.VX, k.VZ);
                EventsVersion++;
                break;
            }
            case "stuck": {
                if (w.Length < 9) return;
                AltBotModLmStuck s = new AltBotModLmStuck { Time = (long)Num(w[1]), Round = (long)Num(w[2]), Level = w[3], Name = w[4], Team = (int)Num(w[5]), X = Num(w[6]), Z = Num(w[7]), Kind = w[8] };
                Stucks.Add(s); if (Stucks.Count > Keep) Stucks.RemoveRange(0, Stucks.Count - Keep);
                AddHeat(s.Level, s.Round, 2, s.X, s.Z);
                EventsVersion++;
                break;
            }
            case "move":
                if (w.Length >= 8) AddHeat(w[3], (long)Num(w[2]), 3, Num(w[6]), Num(w[7]));
                break;
        }
    }
    // Reads what was added since last time (at most maxBytes). A file shorter than what was read has rolled over:
    // reading starts again at its top (what was in it is already counted). Returns the number of new lines.
    public int Update(string path, long maxBytes) {
        Changed = false;
        if (Path != path) { Reset(); Path = path; }
        if (string.IsNullOrEmpty(path) || !File.Exists(path)) return 0;
        string text;
        try {
            using (FileStream fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete)) {
                long len = fs.Length;
                if (len < Offset) { Offset = 0; Rest = ""; }
                if (Offset == 0 && Lines == 0 && len > FirstRead) { Offset = len - FirstRead; SkipPartial = true; Skipped = true; }
                if (len <= Offset) return 0;
                int n = (int)Math.Min(len - Offset, maxBytes);
                byte[] buf = new byte[n];
                fs.Seek(Offset, SeekOrigin.Begin);
                int got = 0; while (got < n) { int r = fs.Read(buf, got, n - got); if (r <= 0) break; got += r; }
                Offset += got;
                text = Rest + Encoding.UTF8.GetString(buf, 0, got);
            }
        } catch (IOException) { return 0; } catch (UnauthorizedAccessException) { return 0; }
        string[] parts = text.Split('\n');
        Rest = parts[parts.Length - 1];
        int count = 0;
        for (int i = 0; i < parts.Length - 1; i++) {
            if (SkipPartial) { SkipPartial = false; continue; }
            if (parts[i].Length > 0) { AddLine(parts[i]); count++; }
        }
        Lines += count;
        if (count > 0) Changed = true;
        return count;
    }
    static int R(double v) { return (int)Math.Round(v); }
    // Heat colour for t in 0..1 as a, r, g, b (the same ramp as Get-LmHeatColor).
    public static int[] HeatColor(double t) {
        if (t <= 0) return new int[] { 0, 0, 0, 0 };
        if (t > 1) t = 1;
        double[][] stops = { new double[] { 0.0, 40, 90, 255 }, new double[] { 0.35, 40, 220, 120 }, new double[] { 0.65, 255, 230, 40 }, new double[] { 1.0, 255, 40, 30 } };
        for (int i = 1; i < stops.Length; i++) {
            if (t <= stops[i][0]) {
                double[] a = stops[i - 1], b = stops[i]; double u = (t - a[0]) / (b[0] - a[0]);
                return new int[] { R(70 + 150 * t), R(a[1] + (b[1] - a[1]) * u), R(a[2] + (b[2] - a[2]) * u), R(a[3] + (b[3] - a[3]) * u) };   // (rounded like PowerShell's [int])
            }
        }
        return new int[] { 220, 255, 40, 30 };
    }
    // Heat cells as ARGB pixels, one per cell, row 0 = north; null when there is nothing (or too big).
    public static AltBotModLmHeatGrid HeatGrid(Dictionary<long, int> cells, double cell) {
        if (cells == null || cells.Count == 0) return null;
        long minX = long.MaxValue, minZ = long.MaxValue, maxX = long.MinValue, maxZ = long.MinValue; int top = 1;
        foreach (KeyValuePair<long, int> e in cells) {
            long cx = e.Key / 65536, cz = e.Key - cx * 65536;
            if (cx < minX) minX = cx; if (cx > maxX) maxX = cx; if (cz < minZ) minZ = cz; if (cz > maxZ) maxZ = cz;
            if (e.Value > top) top = e.Value;
        }
        long w = maxX - minX + 1, h = maxZ - minZ + 1;
        if (w * h > 4000000) return null;
        AltBotModLmHeatGrid g = new AltBotModLmHeatGrid { W = (int)w, H = (int)h, Top = top, Cell = cell, MinX = (minX - 32768) * cell, MinZ = (minZ - 32768) * cell, Argb = new int[w * h] };
        foreach (KeyValuePair<long, int> e in cells) {
            long cx = e.Key / 65536, cz = e.Key - cx * 65536;
            int[] c = HeatColor(Math.Sqrt((double)e.Value / top));
            g.Argb[(h - 1 - (cz - minZ)) * w + (cx - minX)] = (c[0] << 24) | (c[1] << 16) | (c[2] << 8) | c[3];
        }
        return g;
    }
    // One layer of heat for a map: one round, or every round on it (round 0).
    public Dictionary<long, int> Heat(string level, string layer, long round) {
        int li = Array.IndexOf(Layers, layer);
        Dictionary<long, int> sum = new Dictionary<long, int>();
        if (li < 0) return sum;
        foreach (KeyValuePair<string, Dictionary<long, int>[]> e in HeatBy) {
            int bar = e.Key.LastIndexOf('|');
            if (e.Key.Substring(0, bar) != level) continue;
            if (round != 0 && e.Key.Substring(bar + 1) != round.ToString(CultureInfo.InvariantCulture)) continue;
            foreach (KeyValuePair<long, int> c in e.Value[li]) { int n; sum.TryGetValue(c.Key, out n); sum[c.Key] = n + c.Value; }
        }
        return sum;
    }
}
'@

# ---- the event history (read a bit more each time) ----
# Two readers with the same result: a compiled one (C#, Add-Type - about 100 times quicker: an hour of history is
# 50 000 lines) and this PowerShell one, used only if Windows cannot compile the other.
$script:LmFast = $false
try {
    if (-not ('AltBotModLmFeed' -as [type])) { Add-Type -TypeDefinition $script:LmFeedSource }
    $script:LmFast = $true
} catch { $script:LmFast = $false }

function New-LmFeed([switch]$Slow) {
    if ($script:LmFast -and -not $Slow) { $f = New-Object AltBotModLmFeed; $f.FirstRead = $script:LmFirstRead; $f.Keep = $script:LmKeepKills; $f.Cell = $script:LmCell; return $f }
    return [pscustomobject]@{ Path = $null; Offset = [long]0; Rest = ''; SkipPartial = $false; Skipped = $false; Lines = [long]0; Changed = $false; EventsVersion = 0
        Kills = (New-Object System.Collections.Generic.List[object]); Stucks = (New-Object System.Collections.Generic.List[object])
        Rounds = (New-Object System.Collections.Generic.List[object]); Heat = @{} }
}
function Get-LmCellKey([double]$x, [double]$z) {
    $cx = [long][Math]::Floor($x / $script:LmCell) + 32768; $cz = [long][Math]::Floor($z / $script:LmCell) + 32768
    return $cx * 65536 + $cz
}
# key -> the cell's south-west corner (x, z) in metres
function ConvertFrom-LmCellKey([long]$key) {
    $cx = [Math]::Floor($key / 65536); $cz = $key - $cx * 65536
    return @((($cx - 32768) * $script:LmCell), (($cz - 32768) * $script:LmCell))
}
function Add-LmHeat($feed, [string]$level, [long]$round, [string]$layer, [double]$x, [double]$z) {
    $k = "$level|$round"
    if (-not $feed.Heat.ContainsKey($k)) {
        $feed.Heat[$k] = @{ Level = $level; Round = $round; kills = (New-Object 'System.Collections.Generic.Dictionary[long,int]'); deaths = (New-Object 'System.Collections.Generic.Dictionary[long,int]')
            stuck = (New-Object 'System.Collections.Generic.Dictionary[long,int]'); traffic = (New-Object 'System.Collections.Generic.Dictionary[long,int]') }
    }
    $d = $feed.Heat[$k][$layer]; $c = Get-LmCellKey $x $z
    $n = 0; if ($d.TryGetValue($c, [ref]$n)) { $d[$c] = $n + 1 } else { $d[$c] = 1 }
}
function Add-LmEvent($feed, [string]$line) {
    $w = $line.TrimEnd("`r") -split "`t"
    switch ($w[0]) {
        'round' { if ($w.Count -ge 5) { $feed.EventsVersion++; $feed.Rounds.Add([pscustomobject]@{ Time = [long](ConvertTo-LmNumber $w[1]); Round = [long](ConvertTo-LmNumber $w[2]); Level = $w[3]; Mode = $w[4] }) } }
        'kill' {
            if ($w.Count -lt 17) { return }
            $k = [pscustomobject]@{ Time = [long](ConvertTo-LmNumber $w[1]); Round = [long](ConvertTo-LmNumber $w[2]); Level = $w[3]
                Killer = $w[4]; KillerBot = ($w[5] -eq '1'); KillerTeam = [int](ConvertTo-LmNumber $w[6]); KX = (ConvertTo-LmNumber $w[7]); KZ = (ConvertTo-LmNumber $w[8])
                Victim = $w[9]; VictimBot = ($w[10] -eq '1'); VictimTeam = [int](ConvertTo-LmNumber $w[11]); VX = (ConvertTo-LmNumber $w[12]); VZ = (ConvertTo-LmNumber $w[13])
                Weapon = (Get-LmWeapon $w[14]); Headshot = ($w[15] -eq '1'); Distance = (ConvertTo-LmNumber $w[16]) }
            $k | Add-Member -NotePropertyName Suicide -NotePropertyValue ($k.Killer -eq '-' -or $k.Killer -eq $k.Victim)
            $k | Add-Member -NotePropertyName TeamKill -NotePropertyValue (-not $k.Suicide -and $k.KillerTeam -ne 0 -and $k.KillerTeam -eq $k.VictimTeam)
            $feed.Kills.Add($k); $feed.EventsVersion++
            if ($feed.Kills.Count -gt $script:LmKeepKills) { $feed.Kills.RemoveRange(0, $feed.Kills.Count - $script:LmKeepKills) }
            if (-not $k.Suicide) { Add-LmHeat $feed $k.Level $k.Round 'kills' $k.KX $k.KZ }
            Add-LmHeat $feed $k.Level $k.Round 'deaths' $k.VX $k.VZ
        }
        'stuck' {
            if ($w.Count -lt 9) { return }
            $s = [pscustomobject]@{ Time = [long](ConvertTo-LmNumber $w[1]); Round = [long](ConvertTo-LmNumber $w[2]); Level = $w[3]; Name = $w[4]
                Team = [int](ConvertTo-LmNumber $w[5]); X = (ConvertTo-LmNumber $w[6]); Z = (ConvertTo-LmNumber $w[7]); Kind = $w[8] }
            $feed.Stucks.Add($s); $feed.EventsVersion++
            if ($feed.Stucks.Count -gt $script:LmKeepKills) { $feed.Stucks.RemoveRange(0, $feed.Stucks.Count - $script:LmKeepKills) }
            Add-LmHeat $feed $s.Level $s.Round 'stuck' $s.X $s.Z
        }
        'move' { if ($w.Count -ge 8) { Add-LmHeat $feed $w[3] ([long](ConvertTo-LmNumber $w[2])) 'traffic' (ConvertTo-LmNumber $w[6]) (ConvertTo-LmNumber $w[7]) } }
    }
}
# Reads what was added to the events file since last time (at most $maxBytes per call, so the window stays smooth).
# A file shorter than what was read has rolled over: reading starts again at its beginning (nothing is lost - what
# was in it is already counted). Returns the number of new lines.
function Update-LmFeed($feed, [string]$path, [long]$maxBytes = 4MB) {
    if ($feed.GetType().Name -eq 'AltBotModLmFeed') { return $feed.Update($path, $maxBytes) }
    $feed.Changed = $false
    if ($feed.Path -ne $path) { $fresh = New-LmFeed -Slow; foreach ($p in $fresh.PSObject.Properties) { $feed.($p.Name) = $p.Value }; $feed.Path = $path }
    if (-not $path -or -not (Test-Path -LiteralPath $path)) { return 0 }
    $count = 0
    try {
        $fs = New-Object IO.FileStream($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
        try {
            $len = $fs.Length
            if ($len -lt $feed.Offset) { $feed.Offset = 0; $feed.Rest = '' }
            if ($feed.Offset -eq 0 -and $feed.Lines -eq 0 -and $len -gt $script:LmFirstRead) { $feed.Offset = $len - $script:LmFirstRead; $feed.SkipPartial = $true; $feed.Skipped = $true }
            if ($len -le $feed.Offset) { return 0 }
            $n = [long][Math]::Min($len - $feed.Offset, $maxBytes)
            $buf = New-Object byte[] $n
            [void]$fs.Seek($feed.Offset, [IO.SeekOrigin]::Begin)
            $got = 0; while ($got -lt $n) { $r = $fs.Read($buf, $got, $n - $got); if ($r -le 0) { break }; $got += $r }
            $feed.Offset += $got
            $text = $feed.Rest + [Text.Encoding]::UTF8.GetString($buf, 0, $got)
        } finally { $fs.Dispose() }
    } catch { return 0 }
    $parts = $text -split "`n"
    $feed.Rest = $parts[-1]
    for ($i = 0; $i -lt $parts.Count - 1; $i++) {
        if ($feed.SkipPartial) { $feed.SkipPartial = $false; continue }   # (started mid-file: the first line is cut)
        if ($parts[$i].Length) { Add-LmEvent $feed $parts[$i]; $count++ }
    }
    $feed.Lines += $count
    if ($count) { $feed.Changed = $true }
    return $count
}

# Heat cells of one layer (kills = where killers stood, deaths, stuck, traffic) for a map: one round, or every
# round on it ($round 0). Returns a Dictionary[long,int] (cell key -> count).
function Get-LmHeat($feed, [string]$level, [string]$layer, [long]$round = 0) {
    if ($feed.GetType().Name -eq 'AltBotModLmFeed') { return ,$feed.Heat($level, $layer, $round) }
    if ($round) { $k = "$level|$round"; if ($feed.Heat.ContainsKey($k)) { return ,$feed.Heat[$k][$layer] }; return ,(New-Object 'System.Collections.Generic.Dictionary[long,int]') }
    $sum = New-Object 'System.Collections.Generic.Dictionary[long,int]'
    foreach ($h in $feed.Heat.Values) {
        if ($h.Level -ne $level) { continue }
        foreach ($e in $h[$layer].GetEnumerator()) { $n = 0; if ($sum.TryGetValue($e.Key, [ref]$n)) { $sum[$e.Key] = $n + $e.Value } else { $sum[$e.Key] = $e.Value } }
    }
    return ,$sum
}
function Select-LmEvents($list, [string]$level, [long]$round = 0) {
    return @(foreach ($e in $list) { if ($e.Level -eq $level -and (-not $round -or $e.Round -eq $round)) { $e } })
}
# Who killed whom: kills, deaths, head shots, team kills and the favourite weapon per player.
function Get-LmScoreboard($feed, [string]$level, [long]$round = 0) {
    $by = @{}
    function Row($name, $team, $bot) { if (-not $by.ContainsKey($name)) { $by[$name] = [pscustomobject]@{ Name = $name; Team = $team; Bot = $bot; Kills = 0; Deaths = 0; Headshots = 0; TeamKills = 0; Weapons = @{}; Weapon = ''; Longest = 0.0 } }; return $by[$name] }
    foreach ($k in (Select-LmEvents $feed.Kills $level $round)) {
        $v = Row $k.Victim $k.VictimTeam $k.VictimBot; $v.Deaths++
        if ($k.Suicide) { continue }
        $r = Row $k.Killer $k.KillerTeam $k.KillerBot
        if ($k.TeamKill) { $r.TeamKills++; continue }
        $r.Kills++; if ($k.Headshot) { $r.Headshots++ }
        if ($k.Distance -gt $r.Longest) { $r.Longest = $k.Distance }
        if ($r.Weapons.ContainsKey($k.Weapon)) { $r.Weapons[$k.Weapon]++ } else { $r.Weapons[$k.Weapon] = 1 }
    }
    foreach ($r in $by.Values) { if ($r.Weapons.Count) { $r.Weapon = ($r.Weapons.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 1).Key } }
    return @($by.Values | Sort-Object @{ Expression = 'Kills'; Descending = $true }, @{ Expression = 'Deaths'; Descending = $false }, Name)
}
# Stuck reports grouped into spots ($radius metres): where bots get stuck again and again comes first.
function Get-LmStuckSpots($feed, [string]$level, [long]$round = 0, [double]$radius = 15) {
    $spots = New-Object System.Collections.ArrayList
    foreach ($s in (Select-LmEvents $feed.Stucks $level $round)) {
        $hit = $null
        foreach ($p in $spots) { $dx = $p.X - $s.X; $dz = $p.Z - $s.Z; if ($dx * $dx + $dz * $dz -le $radius * $radius) { $hit = $p; break } }
        if (-not $hit) { $hit = [pscustomobject]@{ X = $s.X; Z = $s.Z; Count = 0; Kinds = @{}; Bots = @{}; Last = [long]0; SumX = 0.0; SumZ = 0.0 }; [void]$spots.Add($hit) }
        $hit.Count++; $hit.SumX += $s.X; $hit.SumZ += $s.Z; $hit.X = $hit.SumX / $hit.Count; $hit.Z = $hit.SumZ / $hit.Count
        $hit.Kinds[$s.Kind] = 1; $hit.Bots[$s.Name] = 1; if ($s.Time -gt $hit.Last) { $hit.Last = $s.Time }
    }
    return @($spots | Sort-Object @{ Expression = 'Count'; Descending = $true }, @{ Expression = 'Last'; Descending = $true } | ForEach-Object {
        [pscustomobject]@{ X = [Math]::Round($_.X, 1); Z = [Math]::Round($_.Z, 1); Count = $_.Count; Kinds = (@($_.Kinds.Keys | Sort-Object) -join ', '); Bots = $_.Bots.Count; Last = $_.Last } })
}
function Get-LmRounds($feed, [string]$level) { return @($feed.Rounds | Where-Object { $_.Level -eq $level } | Sort-Object Round -Descending) }

# ---- drawing helpers (world metres <-> window pixels) ----
# North (+z) is up and east (+x) right unless flipped; Scale = pixels per metre.
function New-LmView { return [pscustomobject]@{ CX = 0.0; CZ = 0.0; Scale = 0.5; FlipX = $false; FlipZ = $false; W = 800; H = 600; Fitted = '' } }
function Set-LmViewFit($view, [double]$minX, [double]$minZ, [double]$maxX, [double]$maxZ) {
    $sx = [Math]::Max(1.0, $maxX - $minX); $sz = [Math]::Max(1.0, $maxZ - $minZ)
    $view.CX = ($minX + $maxX) / 2; $view.CZ = ($minZ + $maxZ) / 2
    $view.Scale = [Math]::Min($view.W / $sx, $view.H / $sz) * 0.97
}
function ConvertTo-LmScreen($view, [double]$x, [double]$z) {
    $fx = $(if ($view.FlipX) { -1 } else { 1 }); $fz = $(if ($view.FlipZ) { -1 } else { 1 })
    return @(($view.W / 2 + ($x - $view.CX) * $view.Scale * $fx), ($view.H / 2 - ($z - $view.CZ) * $view.Scale * $fz))
}
function ConvertFrom-LmScreen($view, [double]$sx, [double]$sy) {
    $fx = $(if ($view.FlipX) { -1 } else { 1 }); $fz = $(if ($view.FlipZ) { -1 } else { 1 })
    return @(($view.CX + ($sx - $view.W / 2) / ($view.Scale * $fx)), ($view.CZ - ($sy - $view.H / 2) / ($view.Scale * $fz)))
}
# Zoom by $factor keeping the world point under (sx, sy) where it is.
function Set-LmViewZoom($view, [double]$factor, [double]$sx, [double]$sy) {
    $before = ConvertFrom-LmScreen $view $sx $sy
    $view.Scale = [Math]::Max(0.02, [Math]::Min(40.0, $view.Scale * $factor))
    $after = ConvertFrom-LmScreen $view $sx $sy
    $view.CX += $before[0] - $after[0]; $view.CZ += $before[1] - $after[1]
}
# Area to show: the map picture, else everything the server reported (flags, soldiers, goals), 80 m round it.
function Get-LmBounds($live) {
    if ($live -and $live.Picture) { return @($live.Picture.MinX, $live.Picture.MinZ, $live.Picture.MaxX, $live.Picture.MaxZ) }
    $xs = New-Object System.Collections.ArrayList; $zs = New-Object System.Collections.ArrayList
    if ($live) {
        foreach ($f in $live.Flags) { [void]$xs.Add($f.X); [void]$zs.Add($f.Z) }
        foreach ($p in $live.Players) { if ($p.X -ne 0 -or $p.Z -ne 0) { [void]$xs.Add($p.X); [void]$zs.Add($p.Z) } }
    }
    if (-not $xs.Count) { return @(-500.0, -500.0, 500.0, 500.0) }
    $m = $xs | Measure-Object -Minimum -Maximum; $n = $zs | Measure-Object -Minimum -Maximum
    return @(($m.Minimum - 80), ($n.Minimum - 80), ($m.Maximum + 80), ($n.Maximum + 80))
}
# Heat colour for t in 0..1: clear -> blue -> green -> yellow -> red, more opaque as it gets hotter. @(a, r, g, b)
function Get-LmHeatColor([double]$t) {
    if ($t -le 0) { return @(0, 0, 0, 0) }
    $t = [Math]::Min(1.0, $t)
    $stops = @(@(0.0, 40, 90, 255), @(0.35, 40, 220, 120), @(0.65, 255, 230, 40), @(1.0, 255, 40, 30))
    for ($i = 1; $i -lt $stops.Count; $i++) {
        if ($t -le $stops[$i][0]) {
            $a = $stops[$i - 1]; $b = $stops[$i]; $u = ($t - $a[0]) / ($b[0] - $a[0])
            return @([int](70 + 150 * $t), [int]($a[1] + ($b[1] - $a[1]) * $u), [int]($a[2] + ($b[2] - $a[2]) * $u), [int]($a[3] + ($b[3] - $a[3]) * $u))
        }
    }
    return @(220, 255, 40, 30)
}
# Heat as a grid for drawing: bounds of the cells and t (0..1, square-root scaled so a few hot spots do not hide the
# rest) per cell. Returns $null when there is nothing.
function Get-LmHeatGrid($cells) {
    if (-not $cells -or $cells.Count -eq 0) { return $null }
    $minX = [long]::MaxValue; $minZ = [long]::MaxValue; $maxX = [long]::MinValue; $maxZ = [long]::MinValue; $top = 1
    foreach ($e in $cells.GetEnumerator()) {
        $cx = [long][Math]::Floor($e.Key / 65536); $cz = $e.Key - $cx * 65536
        if ($cx -lt $minX) { $minX = $cx }; if ($cx -gt $maxX) { $maxX = $cx }; if ($cz -lt $minZ) { $minZ = $cz }; if ($cz -gt $maxZ) { $maxZ = $cz }
        if ($e.Value -gt $top) { $top = $e.Value }
    }
    $w = [int]($maxX - $minX + 1); $h = [int]($maxZ - $minZ + 1)
    if ($w * $h -gt 4000000) { return $null }
    $t = New-Object 'double[]' ($w * $h)
    foreach ($e in $cells.GetEnumerator()) {
        $cx = [long][Math]::Floor($e.Key / 65536); $cz = $e.Key - $cx * 65536
        $t[[int](($cz - $minZ) * $w + ($cx - $minX))] = [Math]::Sqrt($e.Value / $top)
    }
    return [pscustomobject]@{ W = $w; H = $h; MinX = (($minX - 32768) * $script:LmCell); MinZ = (($minZ - 32768) * $script:LmCell); Cell = $script:LmCell; T = $t; Top = $top }
}
# The heat as ARGB pixels for a bitmap (one per cell, row 0 = north) with its place: W, H, MinX, MinZ, Cell, Top, Argb.
function Get-LmHeatPixels($cells) {
    if ($script:LmFast) { return [AltBotModLmFeed]::HeatGrid($cells, $script:LmCell) }
    $g = Get-LmHeatGrid $cells
    if (-not $g) { return $null }
    $argb = New-Object 'int[]' ($g.W * $g.H)
    foreach ($e in $cells.GetEnumerator()) {
        $cx = [long][Math]::Floor($e.Key / 65536); $cz = $e.Key - $cx * 65536
        $col = [int]($cx - 32768 - $g.MinX / $g.Cell); $row = $g.H - 1 - [int]($cz - 32768 - $g.MinZ / $g.Cell)
        $c = Get-LmHeatColor ([Math]::Sqrt($e.Value / $g.Top))
        $argb[$row * $g.W + $col] = [int](($c[0] -shl 24) -bor ($c[1] -shl 16) -bor ($c[2] -shl 8) -bor $c[3])
    }
    return [pscustomobject]@{ W = $g.W; H = $g.H; MinX = $g.MinX; MinZ = $g.MinZ; Cell = $g.Cell; Top = $g.Top; Argb = $argb }
}
function Format-LmAgo([long]$then, [long]$now) {
    $s = [Math]::Max(0, $now - $then)
    if ($s -lt 60) { return "$s s ago" }; if ($s -lt 3600) { return "$([Math]::Floor($s / 60)) min ago" }; return "$([Math]::Floor($s / 3600)) h ago"
}
function Get-LmUnixNow { return [long]([DateTime]::UtcNow - (New-Object DateTime(1970, 1, 1, 0, 0, 0, [DateTimeKind]::Utc))).TotalSeconds }

# ---- the switch ----
# On / off for one server: LiveMap=1/0 in its enhanced_<port>.ini, then the running core re-reads its settings.
function Get-LmEnabled([string]$serverDir, $instance) {
    foreach ($r in (Get-BmEffectiveSettings $serverDir $instance)) { if ($r.Setting.Key -eq 'LiveMap') { return ($r.Value -eq '1') } }
    return $true
}
function Set-LmEnabled([string]$serverDir, $instance, [bool]$on) {
    Set-BmSettings $serverDir $instance @{ LiveMap = $(if ($on) { '1' } else { '0' }) }
    Send-BmSettingsReload $serverDir $instance
}
