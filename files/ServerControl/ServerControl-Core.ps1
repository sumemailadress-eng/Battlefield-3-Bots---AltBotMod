# AltBotMod Server Control - core (no UI): finds the BF3 server, reads the _StartServer*.bat instances, talks Frostbite
# RCON (the same protocol Procon uses) and writes AltBotMod's per-instance command / setting files.
# Works in Windows PowerShell 5.1 and PowerShell 7. Dot-source it: . "$PSScriptRoot\ServerControl-Core.ps1"

Set-StrictMode -Version 2

# ---- Maps and the game modes each one has (the official BF3 table AltBotMod uses; bots play every one listed) ----
$script:BmMaps = @(
    @{Code='MP_001';      Name='Grand Bazaar';        Cq=0; Assault=0}, @{Code='MP_003';      Name='Teheran Highway';     Cq=0; Assault=0},
    @{Code='MP_007';      Name='Caspian Border';      Cq=0; Assault=0}, @{Code='MP_011';      Name='Seine Crossing';      Cq=0; Assault=0},
    @{Code='MP_012';      Name='Operation Firestorm'; Cq=0; Assault=0}, @{Code='MP_013';      Name='Damavand Peak';       Cq=0; Assault=0},
    @{Code='MP_017';      Name='Noshahr Canals';      Cq=0; Assault=0}, @{Code='MP_018';      Name='Kharg Island';        Cq=0; Assault=0},
    @{Code='MP_Subway';   Name='Operation Metro';     Cq=0; Assault=0}, @{Code='XP1_001';     Name='Strike at Karkand';   Cq=0; Assault=1},
    @{Code='XP1_002';     Name='Gulf of Oman';        Cq=0; Assault=0}, @{Code='XP1_003';     Name='Sharqi Peninsula';    Cq=0; Assault=1},
    @{Code='XP1_004';     Name='Wake Island';         Cq=0; Assault=1}, @{Code='XP2_Palace';  Name='Donya Fortress';      Cq=1; Assault=0},
    @{Code='XP2_Office';  Name='Operation 925';       Cq=1; Assault=0}, @{Code='XP2_Factory'; Name='Scrapmetal';          Cq=1; Assault=0},
    @{Code='XP2_Skybar';  Name='Ziba Tower';          Cq=1; Assault=0}, @{Code='XP3_Alborz';  Name='Alborz Mountains';    Cq=0; Assault=0},
    @{Code='XP3_Shield';  Name='Armored Shield';      Cq=0; Assault=0}, @{Code='XP3_Desert';  Name='Bandar Desert';       Cq=0; Assault=0},
    @{Code='XP3_Valley';  Name='Death Valley';        Cq=0; Assault=0}, @{Code='XP4_Parl';    Name='Azadi Palace';        Cq=0; Assault=0},
    @{Code='XP4_Quake';   Name='Epicenter';           Cq=0; Assault=0}, @{Code='XP4_FD';      Name='Markaz Monolith';     Cq=0; Assault=0},
    @{Code='XP4_Rubble';  Name='Talah Market';        Cq=0; Assault=1}, @{Code='XP5_001';     Name='Operation Riverside'; Cq=0; Assault=0},
    @{Code='XP5_002';     Name='Nebandan Flats';      Cq=0; Assault=0}, @{Code='XP5_003';     Name='Kiasar Railroad';     Cq=0; Assault=0},
    @{Code='XP5_004';     Name='Sabalan Pipeline';    Cq=0; Assault=0}
)
$script:BmModeNames = [ordered]@{
    'ConquestLarge0'        = 'Conquest Large';
    'ConquestSmall0'        = 'Conquest';
    'ConquestAssaultLarge0' = 'Conquest Assault Large';
    'ConquestAssaultSmall0' = 'Conquest Assault';
    'ConquestAssaultSmall1' = 'Conquest Assault #2';
    'RushLarge0'            = 'Rush';
    'TeamDeathMatch0'       = 'Team Deathmatch';
    'TeamDeathMatchC0'      = 'TDM Close Quarters';
    'SquadDeathMatch0'      = 'Squad Deathmatch';
    'Domination0'           = 'Conquest Domination';
    'SquadRush0'            = 'Squad Rush';
    'GunMaster0'            = 'Gun Master';
    'Scavenger0'            = 'Scavenger';
    'TankSuperiority0'      = 'Tank Superiority';
    'AirSuperiority0'       = 'Air Superiority';
    'CaptureTheFlag0'       = 'Capture the Flag';
}
function Get-BmMap([string]$code) {
    foreach ($m in $script:BmMaps) { if ($m.Code -ieq $code) { return $m } }
    return $null
}
function Get-BmMapName([string]$code) { $m = Get-BmMap $code; if ($m) { return $m.Name }; return $code }
function Get-BmModeName([string]$mode) { if ($script:BmModeNames.Contains($mode)) { return $script:BmModeNames[$mode] }; return $mode }
# The modes a map has, in menu order (same rules as AltBotMod's bm_map_mode in modes.c).
function Get-BmMapModes([string]$code) {
    $m = Get-BmMap $code
    if (-not $m) { return @() }
    $out = New-Object System.Collections.ArrayList
    if ($m.Cq) { [void]$out.Add('Domination0') }
    elseif ($m.Assault) {
        [void]$out.Add('ConquestAssaultLarge0'); [void]$out.Add('ConquestAssaultSmall0')
        if ($m.Code -like 'XP1_*') { [void]$out.Add('ConquestAssaultSmall1') }
    } else {
        [void]$out.Add('ConquestLarge0'); [void]$out.Add('ConquestSmall0')
        if ($m.Code -eq 'XP1_002') { [void]$out.Add('ConquestAssaultSmall0') }
    }
    if (-not $m.Cq) { [void]$out.Add('RushLarge0'); [void]$out.Add('TeamDeathMatch0') }
    [void]$out.Add('TeamDeathMatchC0'); [void]$out.Add('SquadDeathMatch0')
    # AltBotMod V15: the other modes, on the levels whose server data has them
    if (-not $m.Cq) { [void]$out.Add('SquadRush0') }
    if ($m.Cq -or $m.Code -like 'XP4_*') { [void]$out.Add('GunMaster0') }
    if ($m.Code -like 'XP4_*') { [void]$out.Add('Scavenger0') }
    if ($m.Code -like 'XP3_*') { [void]$out.Add('TankSuperiority0') }
    if ($m.Code -like 'XP5_*') { [void]$out.Add('AirSuperiority0'); [void]$out.Add('CaptureTheFlag0') }
    return ,$out.ToArray()
}

# ---- Finding the server and its instances ----
function Test-BmServerDir([string]$dir) {
    return $dir -and (Test-Path -LiteralPath (Join-Path $dir 'bf3_server_Final.exe') -PathType Leaf)
}
# A folder holding bf3_server_Final.exe, or one above it (BF-Portable). $null if neither.
function Resolve-BmServerDir([string]$dir) {
    if (-not $dir) { return $null }
    if (Test-BmServerDir $dir) { return (Resolve-Path -LiteralPath $dir).Path }
    foreach ($sub in @('Battlefield_3_Server', 'Battlefield 3 Server', 'server\Battlefield 3 Server')) {
        $p = Join-Path $dir $sub
        if (Test-BmServerDir $p) { return (Resolve-Path -LiteralPath $p).Path }
    }
    return $null
}
function Find-BmServerDir([string]$scriptDir) {
    $candidates = New-Object System.Collections.ArrayList
    $d = $scriptDir
    for ($k = 0; $k -lt 4 -and $d; $k++) { [void]$candidates.Add($d); $d = Split-Path -Parent $d }
    try {
        foreach ($drive in [IO.DriveInfo]::GetDrives()) {
            if ($drive.DriveType -ne 'Fixed' -or -not $drive.IsReady) { continue }
            [void]$candidates.Add((Join-Path $drive.RootDirectory.FullName 'BF-Portable'))
            [void]$candidates.Add((Join-Path $drive.RootDirectory.FullName 'ALPLauncher'))
        }
    } catch { }
    foreach ($c in $candidates) { $r = Resolve-BmServerDir $c; if ($r) { return $r } }
    return $null
}
# One entry per _StartServer*.bat: what it starts and how to reach it.
function Get-BmInstances([string]$serverDir) {
    $list = New-Object System.Collections.ArrayList
    $bats = @(Get-ChildItem -LiteralPath $serverDir -Filter '_StartServer*.bat' -File -ErrorAction SilentlyContinue | Sort-Object Name)
    $n = 0
    foreach ($bat in $bats) {
        $vars = @{}
        foreach ($line in [IO.File]::ReadAllLines($bat.FullName)) {
            if ($line -match '^\s*set\s+"?([A-Za-z_]+)=(.*?)"?\s*$') { $vars[$Matches[1].ToUpper()] = $Matches[2] }
        }
        if (-not $vars.ContainsKey('GAMEPORT') -or -not $vars.ContainsKey('REMOTEADMINPORT')) { continue }
        $n++
        $rcon = $vars['REMOTEADMINPORT']; $rhost = '127.0.0.1'; $rport = 0
        if ($rcon -match '^(.*):(\d+)$') { if ($Matches[1]) { $rhost = $Matches[1] }; $rport = [int]$Matches[2] } else { $rport = [int]$rcon }
        if ($rhost -eq '0.0.0.0') { $rhost = '127.0.0.1' }
        $inst = $null
        if ($vars.ContainsKey('INSTANCEDIRECTORY')) { $inst = $vars['INSTANCEDIRECTORY'] -replace '%~dp0', ($serverDir.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar) }
        [void]$list.Add([pscustomobject]@{
            Number = $n; Label = "Server $n"; Bat = $bat.FullName; GamePort = [int]$vars['GAMEPORT']
            RconHost = $rhost; RconPort = $rport; Password = $vars['ADMINPASSWORD']; InstanceDir = $inst
            ServerName = $vars['SERVERNAME']
        })
    }
    return ,$list.ToArray()
}
function Get-BmBotModDir([string]$serverDir) { return Join-Path (Split-Path -Parent $serverDir) 'BotMod' }

# ---- Frostbite RCON ----
# Packet: uint32 sequence, uint32 total size, uint32 word count, then per word: uint32 length, bytes, NUL.
function New-BmRconPacket([uint32]$seq, [string[]]$words) {
    $ms = New-Object IO.MemoryStream
    $bw = New-Object IO.BinaryWriter($ms)
    $enc = [Text.Encoding]::UTF8
    $size = 12
    foreach ($w in $words) { $size += 5 + $enc.GetByteCount($w) }
    $bw.Write([uint32]$seq); $bw.Write([uint32]$size); $bw.Write([uint32]$words.Count)
    foreach ($w in $words) { $b = $enc.GetBytes($w); $bw.Write([uint32]$b.Length); $bw.Write($b); $bw.Write([byte]0) }
    $bw.Flush()
    return ,$ms.ToArray()
}
function Read-BmExact($stream, [int]$count) {
    $buf = New-Object byte[] $count
    $got = 0
    while ($got -lt $count) {
        $r = $stream.Read($buf, $got, $count - $got)
        if ($r -le 0) { throw 'RCON connection closed' }
        $got += $r
    }
    return ,$buf
}
function Read-BmRconPacket($stream) {
    $hdr = Read-BmExact $stream 12
    $seq = [BitConverter]::ToUInt32($hdr, 0); $size = [BitConverter]::ToUInt32($hdr, 4); $count = [BitConverter]::ToUInt32($hdr, 8)
    if ($size -lt 12 -or $size -gt 16777216) { throw "RCON packet size $size" }
    $body = Read-BmExact $stream ([int]$size - 12)
    $words = New-Object System.Collections.ArrayList
    $o = 0
    for ($k = 0; $k -lt $count -and $o + 4 -le $body.Length; $k++) {
        $len = [BitConverter]::ToUInt32($body, $o)
        if ($o + 4 + $len -gt $body.Length) { break }
        [void]$words.Add([Text.Encoding]::UTF8.GetString($body, $o + 4, [int]$len))
        $o += 5 + [int]$len
    }
    return [pscustomobject]@{ Sequence = $seq; Words = [string[]]$words.ToArray() }
}
# A logged-in connection, or $null (server not running / wrong password). $Error detail in .LastError.
function Connect-BmRcon($instance, [int]$timeoutMs = 800) {
    $client = New-Object Net.Sockets.TcpClient
    try {
        $task = $client.ConnectAsync($instance.RconHost, $instance.RconPort)
        if (-not $task.Wait($timeoutMs) -or -not $client.Connected) { $client.Close(); return $null }
        $client.ReceiveTimeout = 3000; $client.SendTimeout = 3000; $client.NoDelay = $true
        $conn = [pscustomobject]@{ Client = $client; Stream = $client.GetStream(); Seq = [uint32]0; Instance = $instance }
        $r = Invoke-BmRcon $conn @('login.plainText', [string]$instance.Password)
        if (-not $r -or $r[0] -ne 'OK') { $client.Close(); return $null }
        return $conn
    } catch { try { $client.Close() } catch { }; return $null }
}
function Disconnect-BmRcon($conn) { if ($conn) { try { $conn.Client.Close() } catch { } } }
# Sends one request and returns the response words (first word OK or an error), $null if the connection broke.
function Invoke-BmRcon($conn, [string[]]$words) {
    if (-not $conn) { return $null }
    try {
        $conn.Seq = [uint32](($conn.Seq + 1) -band 0x3fffffff)
        $packet = New-BmRconPacket $conn.Seq $words
        $conn.Stream.Write($packet, 0, $packet.Length)
        for ($k = 0; $k -lt 64; $k++) {   # (server events, if any were enabled, are skipped)
            $p = Read-BmRconPacket $conn.Stream
            if (($p.Sequence -band 0x3fffffff) -eq $conn.Seq -and ($p.Sequence -band 0x40000000)) { return ,$p.Words }   # (comma: a one-word answer stays an array)
        }
        return $null
    } catch { return $null }
}

# ---- Server state ----
function Get-BmServerInfo($conn) {
    $w = Invoke-BmRcon $conn @('serverInfo')
    if (-not $w -or $w[0] -ne 'OK' -or $w.Count -lt 6) { return $null }
    $info = [ordered]@{ Name = $w[1]; Players = [int]$w[2]; MaxPlayers = [int]$w[3]; Mode = $w[4]; Map = $w[5]
        Round = 0; Rounds = 0; Scores = @(); TargetScore = 0; RoundTime = 0 }
    try {
        $info.Round = [int]$w[6] + 1; $info.Rounds = [int]$w[7]
        $teams = [int]$w[8]; $scores = New-Object System.Collections.ArrayList
        for ($t = 0; $t -lt $teams; $t++) { [void]$scores.Add([double]::Parse($w[9 + $t], [Globalization.CultureInfo]::InvariantCulture)) }
        $info.Scores = $scores.ToArray(); $info.TargetScore = [int]$w[9 + $teams]
        # ... onlineState ranked punkbuster hasPassword serverUpTime roundTime
        $rt = 9 + $teams + 6
        if ($w.Count -gt $rt) { $info.RoundTime = [int]$w[$rt] }
    } catch { }
    return [pscustomobject]$info
}
function Get-BmMapList($conn) {
    $w = Invoke-BmRcon $conn @('mapList.list')
    if (-not $w -or $w[0] -ne 'OK' -or $w.Count -lt 3) { return @() }
    $count = [int]$w[1]; $per = [int]$w[2]; $list = New-Object System.Collections.ArrayList
    for ($k = 0; $k -lt $count; $k++) {
        $b = 3 + $k * $per
        if ($b + 1 -ge $w.Count) { break }
        $rounds = 1; if ($per -ge 3 -and $b + 2 -lt $w.Count) { $rounds = [int]$w[$b + 2] }
        [void]$list.Add([pscustomobject]@{ Index = $k; Map = $w[$b]; Mode = $w[$b + 1]; Rounds = $rounds })
    }
    return ,$list.ToArray()
}
function Get-BmMapIndices($conn) {
    $w = Invoke-BmRcon $conn @('mapList.getMapIndices')
    if (-not $w -or $w[0] -ne 'OK' -or $w.Count -lt 3) { return $null }
    return [pscustomobject]@{ Current = [int]$w[1]; Next = [int]$w[2] }
}
function Get-BmPlayers($conn) {
    $w = Invoke-BmRcon $conn @('admin.listPlayers', 'all')
    if (-not $w -or $w[0] -ne 'OK') { $w = Invoke-BmRcon $conn @('listPlayers', 'all') }
    if (-not $w -or $w[0] -ne 'OK' -or $w.Count -lt 3) { return @() }
    $nf = [int]$w[1]; $fields = $w[2..(1 + $nf)]; $np = [int]$w[2 + $nf]; $list = New-Object System.Collections.ArrayList
    for ($p = 0; $p -lt $np; $p++) {
        $row = [ordered]@{}
        for ($f = 0; $f -lt $nf; $f++) { $i = 3 + $nf + $p * $nf + $f; if ($i -lt $w.Count) { $row[$fields[$f]] = $w[$i] } }
        [void]$list.Add([pscustomobject]$row)
    }
    return ,$list.ToArray()
}
function Get-BmVar($conn, [string]$name) {
    $w = Invoke-BmRcon $conn @("vars.$name")
    if ($w -and $w[0] -eq 'OK' -and $w.Count -ge 2) { return $w[1] }
    return $null
}
function Set-BmVar($conn, [string]$name, [string]$value) { $w = Invoke-BmRcon $conn @("vars.$name", $value); return ($w -and $w[0] -eq 'OK') }

# ---- Map changes ----
# Index of map+mode in the rotation (adds it at the end when it is not there). -1 and the error word on failure.
function Get-BmRotationIndex($conn, [string]$map, [string]$mode, [ref]$why) {
    $list = Get-BmMapList $conn
    foreach ($e in $list) { if ($e.Map -ieq $map -and $e.Mode -ieq $mode) { return $e.Index } }
    $w = Invoke-BmRcon $conn @('mapList.add', $map, $mode, '1')
    if (-not $w -or $w[0] -ne 'OK') { if ($why) { $why.Value = $(if ($w) { $w[0] } else { 'no answer' }) }; return -1 }
    return $list.Count
}
# 'next' = play it after this round; 'now' = also end this round and load it.
function Set-BmMap($conn, [string]$map, [string]$mode, [switch]$Now) {
    $why = ''
    $idx = Get-BmRotationIndex $conn $map $mode ([ref]$why)
    if ($idx -lt 0) { return "The server refused $map / $mode ($why)" }
    $w = Invoke-BmRcon $conn @('mapList.setNextMapIndex', [string]$idx)
    if (-not $w -or $w[0] -ne 'OK') { return "Could not make it the next map ($(if ($w) { $w[0] } else { 'no answer' }))" }
    if ($Now) {
        $w = Invoke-BmRcon $conn @('mapList.runNextRound')
        if (-not $w -or $w[0] -ne 'OK') { return "Could not start it ($(if ($w) { $w[0] } else { 'no answer' }))" }
    }
    return $null
}
function Invoke-BmSimple($conn, [string[]]$words) {
    $w = Invoke-BmRcon $conn $words
    if (-not $w) { return 'no answer from the server' }
    if ($w[0] -ne 'OK') { return $w[0] }
    return $null
}
# Rewrites the whole rotation (used by the up / down / remove buttons) and saves it to the instance's MapList.txt.
function Set-BmRotation($conn, $entries) {
    $e = Invoke-BmSimple $conn @('mapList.clear'); if ($e) { return $e }
    foreach ($x in $entries) {
        $e = Invoke-BmSimple $conn @('mapList.add', $x.Map, $x.Mode, [string]([Math]::Max(1, [int]$x.Rounds))); if ($e) { return "$($x.Map) $($x.Mode): $e" }
    }
    return Invoke-BmSimple $conn @('mapList.save')
}

# ---- AltBotMod per-instance files (BotMod folder next to Battlefield_3_Server) ----
# A console command for AltBotMod on that instance (the loader runs BotMod_<port>.cmd on its next tick, then deletes it).
# 'reload' is never sent from here: hot-reloading the core mid-round has crashed this server before.
function Send-BmBotCommand([string]$serverDir, $instance, [string]$command) {
    if ($command -match '^\s*(reload|unload)\b') { throw 'Hot reload is not allowed from Server Control - restart the server instead.' }
    $path = Join-Path $serverDir ("BotMod_{0}.cmd" -f $instance.GamePort)
    [IO.File]::AppendAllText($path, $command + "`r`n")
}
function Get-BmBotCount([string]$serverDir, $instance) {
    $bm = Get-BmBotModDir $serverDir
    foreach ($f in @(("botcount_{0}.txt" -f $instance.GamePort), 'botcount.txt')) {
        $p = Join-Path $bm $f
        if (Test-Path -LiteralPath $p) { $t = ([IO.File]::ReadAllText($p)).Trim(); $n = 0; if ([int]::TryParse($t, [ref]$n)) { return $n } }
    }
    return $null
}
function Set-BmBotCount([string]$serverDir, $instance, [int]$count) {
    $count = [Math]::Max(0, [Math]::Min(64, $count))
    $bm = Get-BmBotModDir $serverDir
    if (Test-Path -LiteralPath $bm) { [IO.File]::WriteAllText((Join-Path $bm ("botcount_{0}.txt" -f $instance.GamePort)), "$count`r`n") }
    Send-BmBotCommand $serverDir $instance "botcount $count"
}
# key=value in BotMod\enhanced_<port>.ini (that instance's overrides of enhanced.ini; created when missing).
function Get-BmInstanceSetting([string]$serverDir, $instance, [string]$key) {
    $bm = Get-BmBotModDir $serverDir
    foreach ($f in @(("enhanced_{0}.ini" -f $instance.GamePort), 'enhanced.ini')) {
        $p = Join-Path $bm $f
        if (-not (Test-Path -LiteralPath $p)) { continue }
        foreach ($line in [IO.File]::ReadAllLines($p)) { if ($line -match ('^\s*' + [regex]::Escape($key) + '\s*=\s*(.*?)\s*$')) { return $Matches[1] } }
    }
    return $null
}
function Set-BmInstanceSetting([string]$serverDir, $instance, [string]$key, [string]$value) {
    $bm = Get-BmBotModDir $serverDir
    $p = Join-Path $bm ("enhanced_{0}.ini" -f $instance.GamePort)
    $lines = New-Object System.Collections.ArrayList
    if (Test-Path -LiteralPath $p) { foreach ($l in [IO.File]::ReadAllLines($p)) { [void]$lines.Add($l) } }
    else { [void]$lines.Add("# AltBotMod settings for the server on port $($instance.GamePort) only (they override enhanced.ini). Written by Server Control.") }
    $done = $false
    for ($k = 0; $k -lt $lines.Count; $k++) {
        if ($lines[$k] -match ('^\s*' + [regex]::Escape($key) + '\s*=')) { $lines[$k] = "$key=$value"; $done = $true }
    }
    if (-not $done) { [void]$lines.Add("$key=$value") }
    [IO.File]::WriteAllLines($p, [string[]]$lines.ToArray())
}

# ---- AltBotMod settings (enhanced.ini for all servers, enhanced_<port>.ini for one) ----
# Every setting the window offers: key, group, kind (bool / int / float / level / aggr), range, the core's own default
# (used when neither file sets it), a label and a line of help. A test checks the keys, ranges and defaults against
# the core's source (enhanced.c / air.c).
function New-BmSetting($key, $group, $kind, $min, $max, $default, $label, $help) {
    return [pscustomobject]@{ Key = $key; Group = $group; Kind = $kind; Min = $min; Max = $max; Default = [string]$default; Label = $label; Help = $help }
}
$script:BmLevels = @('off', 'easy', 'normal', 'hard', 'veteran', 'elite')
$script:BmSettings = @(
    (New-BmSetting 'Difficulty' 'Combat' 'level' 0 5 'elite' 'Gunfight level' 'off = V8 combat; easy .. elite (reaction, bursts, ranges, side steps, grenades)'),
    (New-BmSetting 'RandomDifficulty' 'Combat' 'bool' 0 1 0 'Mixed levels' 'each bot plays one level either side of the gunfight level'),
    (New-BmSetting 'Aggression' 'Combat' 'aggr' 0 2 1 'Aggression' '0 calm, 1 aggressive, 2 brutal (longer chases, wider vision, fewer idle guards)'),
    (New-BmSetting 'BotDamage' 'Combat' 'float' 0.25 4 1 'Bot bullet damage x' 'multiplier for damage done by bots'' bullets'),
    (New-BmSetting 'InfantryEngageMeters' 'Combat' 'float' 40 300 260 'Rifle range (m)' 'how far riflemen engage soldiers'),
    (New-BmSetting 'InfantryReactionSeconds' 'Combat' 'float' 0.1 2 0.2 'Reaction (s)' 'time from seeing an enemy to the first shot (the gunfight level adds its own)'),
    (New-BmSetting 'InfantryAimMarginMeters' 'Combat' 'float' 0.08 0.5 0.2 'Fire window (m)' 'how close to the aim point before firing (Difficulty off only)'),
    (New-BmSetting 'ReconEngageMeters' 'Combat' 'float' 80 500 450 'Marksman range (m)' 'recon with marksman rifles'),
    (New-BmSetting 'SniperEngageMeters' 'Combat' 'float' 150 900 550 'Sniper range (m)' 'bolt-action rifles spot and fight this far out'),
    (New-BmSetting 'SniperOverwatch' 'Combat' 'bool' 0 1 1 'Snipers take high ground' 'most sniper lives climb to a perch over the objective'),
    (New-BmSetting 'SniperPairs' 'Combat' 'bool' 0 1 1 'Sniper pairs' 'two snipers may share a perch'),
    (New-BmSetting 'SideSteps' 'Combat' 'int' 0 2 1 'Side steps in a fight' '0 none; 1 a short step now and then under fire or close in (V14); 2 constant left-right (V9-V13)'),
    (New-BmSetting 'WeaponVariety' 'Combat' 'bool' 0 1 1 'Weapon variety' 'bots carry different primaries per life'),
    (New-BmSetting 'WeaponAttachments' 'Combat' 'bool' 0 1 1 'Weapon attachments' 'scopes / grips on those weapons'),
    (New-BmSetting 'WeaponHandling' 'Combat' 'bool' 0 1 1 'Per-weapon handling' 'bursts, ranges and aim per weapon type'),
    (New-BmSetting 'ClassSupport' 'Classes' 'bool' 0 1 1 'Medics / support help unasked' 'drop medkits and ammo boxes for themselves and teammates within 10 m'),
    (New-BmSetting 'SupplyMeters' 'Classes' 'float' 20 250 60 'Supply call reach (m)' 'how far a medic / support comes when a bot calls (a player''s call: 3x)'),
    (New-BmSetting 'FunBotsRoutes' 'Movement' 'bool' 0 1 1 'Fun-Bots routes' 'follow the recorded routes in BotMod_traces'),
    (New-BmSetting 'FunBotsMesh' 'Movement' 'bool' 0 1 1 'Fun-Bots objective mesh' 'near flags, M-COMs and bases walk the Fun-Bots objective mesh (BotMod_traces\navzones)'),
    (New-BmSetting 'RouteActions' 'Movement' 'bool' 0 1 1 'Fun-Bots route actions' 'boarding spots, exit points, waits and crouch sections recorded in the routes (V15)'),
    (New-BmSetting 'ReconBeacons' 'Classes' 'bool' 0 1 1 'Recon radio beacons' 'recons set a radio beacon at a recorded beacon spot near the objective (V15)'),
    (New-BmSetting 'GadgetVariety' 'Classes' 'bool' 0 1 1 'Gadget variety' 'engineers carry SMAW / RPG, Javelin or Stinger / Igla; support claymore or C4; recon T-UGS or SOFLAM, per life (V16)'),
    (New-BmSetting 'C4Attacks' 'Classes' 'bool' 0 1 1 'C4 on vehicles' 'a soldier with C4 runs up to a slow enemy vehicle, plants two charges, backs off and sets them off (V16)'),
    (New-BmSetting 'LockOnLaunchers' 'Classes' 'bool' 0 1 1 'Lock-on launchers' 'Javelin at ground vehicles, Stinger / Igla at aircraft: ADS, 2.2 s lock, fire (V16)'),
    (New-BmSetting 'BotTicketCost' 'Combat' 'float' 0 2 0.5 'Tickets per bot respawn' 'tickets a bot respawn costs its team (V16: half a ticket - bots die more often than people)'),
    (New-BmSetting 'Driving' 'Vehicles' 'bool' 0 1 1 'Bots drive' 'bots use ground vehicles at all'),
    (New-BmSetting 'LightVehicles' 'Vehicles' 'bool' 0 1 1 'Cars and bikes' 'jeeps, quads, dirt bikes, trucks'),
    (New-BmSetting 'ArmoredVehicles' 'Vehicles' 'bool' 0 1 0 'Tanks and IFVs' 'armour drives and fights'),
    (New-BmSetting 'DriverWeapons' 'Vehicles' 'bool' 0 1 0 'Driver weapons' 'tank / IFV drivers fire their main gun (needs Tanks and IFVs)'),
    (New-BmSetting 'Boats' 'Vehicles' 'bool' 0 1 1 'Boats' 'RHIBs and attack boats'),
    (New-BmSetting 'VehicleCountermeasures' 'Vehicles' 'bool' 0 1 1 'Smoke when hit' 'bot tank / IFV drivers pop smoke when a hit lands, once per 25 s (V16)'),
    (New-BmSetting 'VehicleDamageTaken' 'Vehicles' 'float' 0.25 4 1 'Damage to vehicles x' 'multiplier for all damage vehicles take, from anyone (V16.2; switches itself off if the game''s damage record does not check out)'),
    (New-BmSetting 'VehicleDamageDealt' 'Vehicles' 'float' 0.25 4 1 'Damage from vehicles x' 'multiplier for damage done by anyone in a vehicle, to soldiers and vehicles (V16.2)'),
    (New-BmSetting 'GroundDriversPerTeam' 'Vehicles' 'int' 1 32 16 'Drivers per team' 'most ground vehicles a team''s bots drive at once'),
    (New-BmSetting 'MinimumTripMeters' 'Vehicles' 'float' 25 2000 250 'Shortest trip by vehicle (m)' 'closer objectives are walked'),
    (New-BmSetting 'FindVehicleMeters' 'Vehicles' 'float' 5 250 80 'Vehicle search (m)' 'how far a bot walks to a free vehicle'),
    (New-BmSetting 'SquadVehicleMeters' 'Vehicles' 'float' 5 250 90 'Squad ride search (m)' 'how far squadmates come to ride along'),
    (New-BmSetting 'DismountMeters' 'Vehicles' 'float' 20 150 55 'Stop short of a flag (m)' 'cars stop this far from the flag; the rest on foot'),
    (New-BmSetting 'ReturnToBase' 'Vehicles' 'bool' 0 1 1 'Return for vehicles' 'bots go back to base for vehicles that respawned'),
    (New-BmSetting 'PassengerTakeover' 'Vehicles' 'bool' 0 1 1 'Passengers take the wheel' 'a passenger drives when the driver gets out'),
    (New-BmSetting 'LightSpeedMps' 'Vehicles' 'float' 3 45 34 'Car speed (m/s)' 'top speed for light vehicles'),
    (New-BmSetting 'TankSpeedMps' 'Vehicles' 'float' 2 20 16 'Tank speed (m/s)' 'top speed for tanks'),
    (New-BmSetting 'IfvSpeedMps' 'Vehicles' 'float' 2 25 20 'IFV speed (m/s)' 'top speed for IFVs'),
    (New-BmSetting 'BoatSpeedMps' 'Vehicles' 'float' 3 35 22 'Boat speed (m/s)' 'top speed for boats'),
    (New-BmSetting 'TrafficManagement' 'Vehicles' 'bool' 0 1 1 'Traffic rules' 'convoys, give way, one at a time on narrow bridges'),
    (New-BmSetting 'ConvoySpacingMeters' 'Vehicles' 'float' 8 35 14 'Convoy gap (m)' 'distance kept behind the vehicle in front'),
    (New-BmSetting 'ArmorTactics' 'Vehicles' 'bool' 0 1 1 'Armour tactics' 'hull cover, flanking, pulling back for repairs'),
    (New-BmSetting 'ArmorInfantrySupport' 'Vehicles' 'bool' 0 1 1 'Infantry screen armour' 'nearby infantry guard a tank in a fight'),
    (New-BmSetting 'ArmorEngageMeters' 'Vehicles' 'float' 150 1000 800 'Tank gun range (m)' 'against vehicles'),
    (New-BmSetting 'ArmorInfantryMeters' 'Vehicles' 'float' 80 450 400 'Tank range vs soldiers (m)' 'against infantry'),
    (New-BmSetting 'DriveInStages' 'Vehicles' 'bool' 0 1 1 'Drive in stages' 'checked short legs out of the base on big maps'),
    (New-BmSetting 'VehicleReturnMeters' 'Vehicles' 'float' 100 6000 3000 'Return-to-base distance (m)' 'how far a vehicle may be taken from its spawn before it heads back'),
    (New-BmSetting 'VehicleWalkMeters' 'Vehicles' 'float' 25 6000 250 'Walk to a vehicle (m)' 'after the round''s opening, how far a bot on foot walks to a free vehicle (one near its objective: 40 m)'),
    (New-BmSetting 'SmoothSteering' 'Vehicles' 'bool' 0 1 0 'Smooth steering' 'PD steering (SteerKp / SteerKd) instead of the direct steering'),
    (New-BmSetting 'BallisticAim' 'Vehicles' 'bool' 0 1 1 'Ballistic aim' 'tank and IFV guns aim for the shell''s drop over distance'),
    (New-BmSetting 'ReserveHumanMeters' 'Vehicles' 'float' 0 100 5 'Leave vehicles to you within (m)' 'a bot does not take a vehicle a human is this close to'),
    (New-BmSetting 'StuckSeconds' 'Vehicles' 'float' 1 15 3 'Stuck after (s)' 'how long a driver may not move before it backs off'),
    (New-BmSetting 'BailHealthFraction' 'Vehicles' 'float' 0.05 0.5 0.1 'Bail out below (health)' 'a bot leaves a vehicle with less than this share of its health'),
    (New-BmSetting 'SteerKp' 'Vehicles' 'float' 0.2 4 1.5 'Steering gain' 'how hard drivers steer for a heading error'),
    (New-BmSetting 'SteerKd' 'Vehicles' 'float' 0 0.5 0.08 'Steering damping' 'smooth steering only'),
    (New-BmSetting 'VehicleDiagnostics' 'Vehicles' 'bool' 0 1 1 'Vehicle diagnostics' 'vehicle-decision lines in the log'),
    (New-BmSetting 'ArmorRepairHealthFraction' 'Vehicles' 'float' 0.15 0.65 0.35 'Armour repairs below (health)' 'a tank / IFV pulls back for repairs below this share of its health'),
    (New-BmSetting 'ArmorTacticMeters' 'Vehicles' 'float' 25 120 70 'Armour tactics within (m)' 'how far a tank goes for cover, flanks and repair spots'),
    (New-BmSetting 'Jets' 'Aircraft' 'bool' 0 1 1 'Jets' 'bots fly jets'),
    (New-BmSetting 'Helicopters' 'Aircraft' 'bool' 0 1 1 'Helicopters' 'bots fly helicopters'),
    (New-BmSetting 'AircraftWeapons' 'Aircraft' 'bool' 0 1 0 'Aircraft weapons' 'pilots fire their guns / rockets'),
    (New-BmSetting 'AircraftCountermeasures' 'Aircraft' 'bool' 0 1 0 'Flares' 'pilots pop flares when hit'),
    (New-BmSetting 'TransportDrops' 'Aircraft' 'bool' 0 1 0 'Transport drops' 'transport helicopters drop bots at flags'),
    (New-BmSetting 'TransportLandings' 'Aircraft' 'bool' 0 1 1 'Transport landings' 'they land on flat clear pads to unload'),
    (New-BmSetting 'AirPilotsPerTeam' 'Aircraft' 'int' 1 6 6 'Pilots per team' 'most aircraft a team''s bots fly at once'),
    (New-BmSetting 'AircraftFindMeters' 'Aircraft' 'float' 10 250 120 'Aircraft search (m)' 'how far a bot walks to a free aircraft'),
    (New-BmSetting 'JetHeightMeters' 'Aircraft' 'float' 150 600 350 'Jet height (m)' 'cruise height'),
    (New-BmSetting 'ChopperHeightMeters' 'Aircraft' 'float' 40 250 110 'Helicopter height (m)' 'cruise height'),
    (New-BmSetting 'LearnAircraftDeployment' 'Aircraft' 'bool' 0 1 1 'Learn aircraft deployment' 'remember which spawn-screen choices give aircraft'),
    (New-BmSetting 'AircraftDebug' 'Aircraft' 'bool' 0 1 0 'Aircraft debug log' 'flight control lines in the log'),
    (New-BmSetting 'JetCruiseMps' 'Aircraft' 'float' 60 130 86.9444 'Jet cruise speed (m/s)' 'jet speed when not attacking'),
    (New-BmSetting 'JetCeilingMeters' 'Aircraft' 'float' 200 1200 600 'Jet ceiling (m)' 'highest a jet climbs above the map'),
    (New-BmSetting 'JetOrbitMeters' 'Aircraft' 'float' 150 600 300 'Jet orbit (m)' 'radius jets circle an objective'),
    (New-BmSetting 'JetAreaMarginMeters' 'Aircraft' 'float' 0 3000 900 'Jet area margin (m)' 'jets turn back when they leave the box round the flags, bases and airfields by more than this (0 = off)'),
    (New-BmSetting 'ChopperOrbitMeters' 'Aircraft' 'float' 30 150 70 'Helicopter orbit (m)' 'radius helicopters circle an objective'),
    (New-BmSetting 'AircraftWaitSeconds' 'Aircraft' 'float' 0.5 25 8 'Aircraft wait (s)' 'how long aircraft hold over an objective before moving on'),
    (New-BmSetting 'AircraftAttackSeconds' 'Aircraft' 'float' 2 15 8 'Attack run (s)' 'longest a jet stays on one attack before breaking off'),
    (New-BmSetting 'AirYawSign' 'Aircraft' 'int' -1 1 1 'Yaw input sign' '1 or -1: flips the bots'' yaw input (only if aircraft turn the wrong way)'),
    (New-BmSetting 'AirPitchSign' 'Aircraft' 'int' -1 1 1 'Pitch input sign' '1 or -1: flips the bots'' pitch input'),
    (New-BmSetting 'AirRollSign' 'Aircraft' 'int' -1 1 1 'Roll input sign' '1 or -1: flips the bots'' roll input'),
    (New-BmSetting 'BotNames' 'Classes' 'bool' 0 1 1 'Gamertags for bots' 'bots join under gamer tags (from BotMod\BotMod_names.txt, else made up) instead of [BOT] Support RU; takes effect for bots created after the change'),
    (New-BmSetting 'LockOnAssist' 'Combat' 'bool' 0 1 1 'Lock-on assist' 'bots'' lock-on weapons (Stinger, Igla, Javelin, heat seekers, AA missiles) lock after the weapon''s lock time on target - the game''s own lock never locks for a bot'),
    (New-BmSetting 'LockOnSeconds' 'Combat' 'float' 0 6 0.5 'Lock-on time (s)' 'how long a bot holds a lock-on weapon on its target before the lock is in place (the weapon''s own lock time if that is shorter)'),
    (New-BmSetting 'DoorGunMeters' 'Combat' 'float' 150 1500 600 'Door gun range (m)' 'how far helicopter door gunners and Little Bird bench passengers shoot'),
    (New-BmSetting 'Learning' 'Learning' 'bool' 0 1 1 'Bots learn' 'remember stuck spots, aircraft losses, landing spots and killing grounds per map (BotMod_learned; Learning page)'),
    (New-BmSetting 'LearnFromPlayers' 'Learning' 'bool' 0 1 1 'Learn from players' 'record the routes you walk, drive and sail and where you land helicopters, for the bots'),
    (New-BmSetting 'LiveMap' 'Live map' 'bool' 0 1 1 'Live map' 'record positions, kills and stuck spots for the Live map page (BotMod\LiveMap)'),
    (New-BmSetting 'LiveMapSeconds' 'Live map' 'float' 0.5 10 1 'Live picture every (s)' 'how often the server writes where everyone is'),
    (New-BmSetting 'LiveMapTrailSeconds' 'Live map' 'float' 1 60 5 'Traffic sample every (s)' 'how often each soldier''s position goes into the traffic heatmap'),
    (New-BmSetting 'LiveMapKeepMB' 'Live map' 'int' 5 1000 64 'History file size (MB)' 'events file size before it rolls over to events_<port>.old.txt')
)
function Get-BmSettingsPath([string]$serverDir, $instance) {
    $bm = Get-BmBotModDir $serverDir
    if ($instance) { return Join-Path $bm ("enhanced_{0}.ini" -f $instance.GamePort) }
    return Join-Path $bm 'enhanced.ini'
}
# key -> value of one ini file (the last line for a key wins, like the core; # and ; start comments)
function Read-BmIni([string]$path) {
    $h = @{}
    if (-not (Test-Path -LiteralPath $path)) { return $h }
    foreach ($line in [IO.File]::ReadAllLines($path)) {
        $t = $line; $c = $t.IndexOfAny([char[]]'#;'); if ($c -ge 0) { $t = $t.Substring(0, $c) }
        $t = $t.Trim()
        if (-not $t -or $t.StartsWith('[')) { continue }
        $e = $t.IndexOf('='); if ($e -lt 1) { continue }
        $h[$t.Substring(0, $e).Trim()] = $t.Substring($e + 1).Trim()
    }
    return $h
}
# A typed-in value checked against the setting: the text to write, or $null when it is not valid.
function Test-BmSettingValue($setting, [string]$text) {
    $t = ([string]$text).Trim()
    switch ($setting.Kind) {
        'bool' { if ($t -match '^(1|on|yes|true)$') { return '1' }; if ($t -match '^(0|off|no|false)$') { return '0' }; return $null }
        'level' {
            $i = [Array]::IndexOf($script:BmLevels, $t.ToLower()); if ($i -ge 0) { return $script:BmLevels[$i] }
            if ($t -match '^[0-5]$') { return $script:BmLevels[[int]$t] }; return $null
        }
        default {
            $v = 0.0
            if (-not [double]::TryParse($t, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$v)) { return $null }
            if ($v -lt $setting.Min -or $v -gt $setting.Max) { return $null }
            if ($setting.Kind -ne 'float') { if ($v -ne [Math]::Floor($v)) { return $null }; return ([int]$v).ToString() }
            return $v.ToString([Globalization.CultureInfo]::InvariantCulture)
        }
    }
}
# What a server runs with: its own file, else the all-servers file, else the core's default. One row per setting.
function Get-BmEffectiveSettings([string]$serverDir, $instance) {
    $global = Read-BmIni (Get-BmSettingsPath $serverDir $null)
    $own = if ($instance) { Read-BmIni (Get-BmSettingsPath $serverDir $instance) } else { @{} }
    $rows = @()
    foreach ($s in $script:BmSettings) {
        $v = $s.Default; $src = 'default'
        if ($global.ContainsKey($s.Key) -and $null -ne (Test-BmSettingValue $s $global[$s.Key])) { $v = Test-BmSettingValue $s $global[$s.Key]; $src = 'all servers' }
        if ($own.ContainsKey($s.Key) -and $null -ne (Test-BmSettingValue $s $own[$s.Key])) { $v = Test-BmSettingValue $s $own[$s.Key]; $src = 'this server' }
        $rows += [pscustomobject]@{ Setting = $s; Value = $v; Source = $src }
    }
    return $rows
}
# Writes key=value pairs into enhanced.ini ($instance $null) or enhanced_<port>.ini, replacing a key's line where it
# is (comments and everything else kept), adding new keys at the end. $remove: keys taken out (back to the
# all-servers value). Values must already be checked (Test-BmSettingValue).
function Set-BmSettings([string]$serverDir, $instance, [hashtable]$values, [string[]]$remove = @()) {
    $p = Get-BmSettingsPath $serverDir $instance
    $lines = New-Object System.Collections.ArrayList
    if (Test-Path -LiteralPath $p) { foreach ($l in [IO.File]::ReadAllLines($p)) { [void]$lines.Add($l) } }
    elseif ($instance) { [void]$lines.Add("# AltBotMod settings for the server on port $($instance.GamePort) only (they override enhanced.ini). Written by Server Control.") }
    $left = @{}; foreach ($k in $values.Keys) { $left[$k] = $values[$k] }
    for ($n = $lines.Count - 1; $n -ge 0; $n--) {
        if ($lines[$n] -notmatch '^\s*([A-Za-z0-9_]+)\s*=') { continue }
        $k = $Matches[1]
        if ($remove -contains $k) { $lines.RemoveAt($n); continue }
        foreach ($key in @($values.Keys)) {
            if ($key -ieq $k) {
                if ($left.ContainsKey($key)) { $lines[$n] = "$key=$($values[$key])"; $left.Remove($key) }   # (the last line for a key is the one the core uses)
                else { $lines.RemoveAt($n) }                                                                 # (older duplicates)
                break
            }
        }
    }
    if ($left.Count) {
        if (-not $instance -and -not ($lines -contains '# Set in Server Control:')) { [void]$lines.Add('# Set in Server Control:') }
        foreach ($s in $script:BmSettings) { if ($left.ContainsKey($s.Key)) { [void]$lines.Add("$($s.Key)=$($left[$s.Key])") } }
    }
    [IO.File]::WriteAllLines($p, [string[]]$lines.ToArray())
}
# Asks the running core to read its settings again (the 'ehreload' console command: settings only - never the hot
# reload of the core itself). Some settings show at the next spawn / round.
function Send-BmSettingsReload([string]$serverDir, $instance) { Send-BmBotCommand $serverDir $instance 'ehreload' }

# ---- Processes ----
# One process query serves every instance for 5 s (the window refreshes stopped servers often; a WMI query each time
# made it stutter). -Fresh for Start / Stop.
$script:BmProcCache = @(); $script:BmProcAt = 0
function Get-BmServerProcess($instance, [switch]$Fresh) {
    $now = [Environment]::TickCount
    if ($Fresh -or $script:BmProcAt -eq 0 -or [Math]::Abs($now - $script:BmProcAt) -gt 5000) {
        try { $script:BmProcCache = @(Get-CimInstance Win32_Process -Filter "Name='bf3_server_Final.exe'" -ErrorAction Stop) } catch { $script:BmProcCache = @() }
        $script:BmProcAt = $now
    }
    foreach ($p in $script:BmProcCache) { if ($p.CommandLine -match ('-GamePort\s+' + $instance.GamePort + '\b')) { return $p } }
    return $null
}
function Start-BmServer($instance, [string]$window = 'Minimized') {
    if (@('Normal', 'Minimized', 'Hidden') -notcontains $window) { $window = 'Minimized' }
    Start-Process -FilePath $env:ComSpec -ArgumentList ('/c "' + $instance.Bat + '"') -WorkingDirectory (Split-Path -Parent $instance.Bat) -WindowStyle $window
}
function Stop-BmServer($instance) {
    $p = Get-BmServerProcess $instance -Fresh
    if (-not $p) { return $false }
    Stop-Process -Id $p.ProcessId -Force
    return $true
}
