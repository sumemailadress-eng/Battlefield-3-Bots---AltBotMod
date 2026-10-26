# AltBotMod Server Control - BF-Portable side: the services BF-Portable runs for BF3 (redirector, web server, database,
# BF3 Blaze emulator, the four game servers), a one-click start / stop in the right order, the Battlelog site address,
# and each server's own game rules (Instance<n>\Admin\Startup.txt and its _StartServer*.bat).
# Windows PowerShell 5.1, plain ASCII. Dot-sourced by AltBotMod-ServerControl.ps1 (after ServerControl-Core.ps1).

# ---- the BF-Portable folder and its parts (AltBotMod V14: each one configurable - Launcher.json next to this script) ----
function Get-BpRoot([string]$serverDir) { return Split-Path -Parent $serverDir }
function Read-BpKeyValue([string]$path, [string]$key) {
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    foreach ($l in [IO.File]::ReadAllLines($path)) { if ($l -match ('^\s*' + [regex]::Escape($key) + '\s*=\s*(.+?)\s*$')) { return $Matches[1] } }
    return $null
}
function New-BpPart($key, $label, $exe, $arguments, $workDir, $proc, $port, $wait, $window, $admin, $required, $enabled, $inputLines = '') {
    return [pscustomobject]@{ Key = $key; Label = $label; Enabled = [bool]$enabled; Exe = $exe; Args = $arguments; WorkDir = $workDir; Proc = $proc
        Port = $port; Wait = [int]$wait; Window = $window; Admin = [bool]$admin; Required = [bool]$required; Input = $inputLines }
}
# What BF-Portable itself starts, in start order. Paths are relative to the BF-Portable folder. PortableBF.exe runs
# as administrator, so everything it starts does too; Server Control asks for administrator rights the same way
# (AsAdmin) - the per-part Admin box only matters when it runs without them.
function Get-BpDefaultConfig([string]$serverDir) {
    $root = Get-BpRoot $serverDir
    $blazePort = 42128; $p = Read-BpKeyValue (Join-Path $root 'BF3-EMU\conf\conf.txt') 'BF3_ServerPort'; $n = 0
    if ($p -and [int]::TryParse($p, [ref]$n) -and $n -gt 0) { $blazePort = $n }
    $ip = ''; $ipFile = Join-Path $root 'Redirector\ServerIP.txt'
    if (Test-Path -LiteralPath $ipFile) { $ip = ([IO.File]::ReadAllText($ipFile)).Trim() }
    return [pscustomobject]@{
        Version = 1; AsAdmin = $true; ServerGapSeconds = 4; ServerWindow = 'Minimized'; DnsDomain = ''; DnsIp = $ip
        Parts = @(
            (New-BpPart 'dns' 'LAN DNS server' 'DNS\DnsServer.exe' '' 'DNS' 'DnsServer' $null 3 'Minimized' $true $false $false "{domain}`n{ip}")
            (New-BpPart 'redirector' 'Redirector' 'Redirector\gosredirector.ea.com.exe' '/console' 'Redirector' 'gosredirector.ea.com' $null 4 'Minimized' $true $false $true)
            (New-BpPart 'xampp' 'XAMPP control panel (starts the database and web server itself)' 'xampp\xampp-control.exe' '' 'xampp' 'xampp-control' $null 3 'Minimized' $true $false $false)
            (New-BpPart 'mysql' 'Database (MySQL)' 'xampp\mysql\bin\mysqld.exe' '--defaults-file=mysql\bin\my.ini --standalone' 'xampp' 'mysqld' 3306 40 'Hidden' $false $true $true)
            (New-BpPart 'apache' 'Web server (Apache, Battlelog)' 'xampp\apache\bin\httpd.exe' '' 'xampp' 'httpd' 80 30 'Hidden' $false $false $true)
            (New-BpPart 'blaze' 'BF3 Blaze emulator' 'BF3-EMU\BlazeServer3.exe' '' 'BF3-EMU' 'BlazeServer3' $blazePort 30 'Minimized' $false $true $true)
        )
    }
}
$script:BpConfigPath = $null   # set by the window (BotMod\ServerControl\Launcher.json)
# The saved settings over the defaults: parts matched by Key, unknown keys ignored, missing values from the defaults.
function Read-BpConfig([string]$serverDir, [string]$path = $script:BpConfigPath) {
    $cfg = Get-BpDefaultConfig $serverDir
    if (-not $path -or -not (Test-Path -LiteralPath $path)) { return $cfg }
    try { $saved = [IO.File]::ReadAllText($path) | ConvertFrom-Json } catch { return $cfg }
    foreach ($k in @('AsAdmin', 'ServerGapSeconds', 'ServerWindow', 'DnsDomain', 'DnsIp')) {
        if ($saved.PSObject.Properties[$k] -and $null -ne $saved.$k) { $cfg.$k = $saved.$k }
    }
    $cfg.AsAdmin = [bool]$cfg.AsAdmin; $g = 0; if (-not [int]::TryParse([string]$cfg.ServerGapSeconds, [ref]$g) -or $g -lt 0 -or $g -gt 120) { $g = 4 }; $cfg.ServerGapSeconds = $g
    if (@('Normal', 'Minimized', 'Hidden') -notcontains $cfg.ServerWindow) { $cfg.ServerWindow = 'Minimized' }
    if ($saved.PSObject.Properties['Parts']) {
        foreach ($sp in @($saved.Parts)) {
            $d = $cfg.Parts | Where-Object { $_.Key -eq $sp.Key } | Select-Object -First 1
            if (-not $d) { continue }
            foreach ($f in @('Enabled', 'Exe', 'Args', 'WorkDir', 'Proc', 'Port', 'Wait', 'Window', 'Admin', 'Required', 'Input')) {
                if ($sp.PSObject.Properties[$f]) { $d.$f = $sp.$f }
            }
            $d.Enabled = [bool]$d.Enabled; $d.Admin = [bool]$d.Admin; $d.Required = [bool]$d.Required
            if ($null -eq $d.Args) { $d.Args = '' }; if ($null -eq $d.Input) { $d.Input = '' }
            $w = 0; if (-not [int]::TryParse([string]$d.Wait, [ref]$w) -or $w -lt 1 -or $w -gt 600) { $w = 30 }; $d.Wait = $w
            $pt = 0; if ($null -eq $d.Port -or [string]$d.Port -eq '' -or -not [int]::TryParse([string]$d.Port, [ref]$pt) -or $pt -lt 1 -or $pt -gt 65535) { $d.Port = $null } else { $d.Port = $pt }
            if (@('Normal', 'Minimized', 'Hidden') -notcontains $d.Window) { $d.Window = 'Minimized' }
        }
    }
    return $cfg
}
function Save-BpConfig($cfg, [string]$path = $script:BpConfigPath) {
    if (-not $path) { return }
    $o = [ordered]@{ Version = 1; AsAdmin = [bool]$cfg.AsAdmin; ServerGapSeconds = [int]$cfg.ServerGapSeconds; ServerWindow = $cfg.ServerWindow; DnsDomain = [string]$cfg.DnsDomain; DnsIp = [string]$cfg.DnsIp
        Parts = @($cfg.Parts | ForEach-Object { [ordered]@{ Key = $_.Key; Enabled = [bool]$_.Enabled; Exe = $_.Exe; Args = [string]$_.Args; WorkDir = $_.WorkDir; Proc = $_.Proc; Port = $_.Port; Wait = [int]$_.Wait; Window = $_.Window; Admin = [bool]$_.Admin; Required = [bool]$_.Required; Input = [string]$_.Input } }) }
    [IO.File]::WriteAllText($path, ($o | ConvertTo-Json -Depth 5))
}
function Resolve-BpPath([string]$root, [string]$p) { if (-not $p) { return $root }; if ([IO.Path]::IsPathRooted($p)) { return $p }; return Join-Path $root $p }
# The parts with full paths, in start order (every part, Enabled or not: the status list shows them all).
function Get-BpServices([string]$serverDir, $cfg = $null) {
    if (-not $cfg) { $cfg = Read-BpConfig $serverDir }
    $root = Get-BpRoot $serverDir
    return @($cfg.Parts | ForEach-Object {
        $inp = ([string]$_.Input).Replace('{domain}', [string]$cfg.DnsDomain).Replace('{ip}', [string]$cfg.DnsIp)
        [pscustomobject]@{ Key = $_.Key; Label = $_.Label; Enabled = $_.Enabled; Exe = (Resolve-BpPath $root $_.Exe); Args = [string]$_.Args; WorkDir = (Resolve-BpPath $root $_.WorkDir)
            Proc = $_.Proc; Port = $_.Port; Wait = $_.Wait; Window = $_.Window; Admin = $_.Admin; Required = $_.Required; Input = $inp; Optional = -not $_.Enabled }
    })
}
function Get-BpClient([string]$serverDir) { return Join-Path (Get-BpRoot $serverDir) 'Client-Files\BF-Launcher\BF Launcher.exe' }
function Get-BpBattlelogUrl([string]$saved) { if ($saved -and $saved -match '^https?://') { return $saved }; return 'http://localhost/' }
function Test-BpElevated { try { return ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) } catch { return $false } }

# How to start one part (no side effects - the tests check it). V13 passed an empty argument list to Start-Process,
# which refuses it: the DNS server, web server and Blaze emulator never started.
#  - typed answers (the DNS server asks for the domain name and the IP): its input is kept open and written to it;
#    that needs this window to have administrator rights when the part does - otherwise its own window opens and
#    you type them there (the launcher log says what to type);
#  - administrator rights: asked for (Windows shows a prompt) when this window has none and the part needs them.
function New-BpStartPlan($svc, [bool]$elevated) {
    $lines = @(); if ($svc.Input) { $lines = @(([string]$svc.Input) -split "`r?`n" | Where-Object { $_ -ne '' }) }
    $needAdmin = [bool]$svc.Admin -and -not $elevated
    $plan = [pscustomobject]@{ File = $svc.Exe; Arguments = [string]$svc.Args; WorkDir = $svc.WorkDir; Window = $svc.Window; Verb = ''; Shell = $true; Input = @(); Note = '' }
    if ($lines.Count) {
        if ($needAdmin) { $plan.Verb = 'runas'; $plan.Window = 'Normal'; $plan.Note = "type in its window: $($lines -join ', then ')" }
        else { $plan.Shell = $false; $plan.Input = $lines }
    } elseif ($needAdmin) { $plan.Verb = 'runas' }
    return $plan
}
$script:BpStarted = @{}   # Key -> the process this window started (to see it exit, to type "exit" to the DNS server)

# Hooks (the tests replace them): is a TCP port answering on this PC, which processes run, start one.
$script:BpPortOpen = {
    param([int]$port)
    $c = New-Object Net.Sockets.TcpClient
    try { $t = $c.ConnectAsync('127.0.0.1', $port); return ($t.Wait(150) -and $c.Connected) } catch { return $false } finally { $c.Close() }
}
$script:BpProcesses = {
    param([string]$name, [string]$root)
    return @(Get-Process -Name $name -ErrorAction SilentlyContinue | Where-Object { -not $_.Path -or $_.Path -like ($root + '*') })
}
$script:BpStartProcess = {
    param($svc)
    $plan = New-BpStartPlan $svc (Test-BpElevated)
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = $plan.File; $psi.Arguments = $plan.Arguments; $psi.WorkingDirectory = $plan.WorkDir
    $psi.UseShellExecute = $plan.Shell
    if ($plan.Shell) { $psi.WindowStyle = [Diagnostics.ProcessWindowStyle]$plan.Window; if ($plan.Verb) { $psi.Verb = $plan.Verb } }
    else { $psi.RedirectStandardInput = $true; $psi.CreateNoWindow = ($plan.Window -eq 'Hidden') }
    $p = [Diagnostics.Process]::Start($psi)
    if ($p -and $plan.Input.Count) { foreach ($l in $plan.Input) { $p.StandardInput.WriteLine($l) }; $p.StandardInput.Flush() }   # (kept open: it waits for "exit")
    if ($p) { $script:BpStarted[$svc.Key] = $p }
    return $plan.Note
}
function Get-BpState($svc, [string]$root) {
    if (-not (Test-Path -LiteralPath $svc.Exe)) { return 'missing' }
    $running = @(& $script:BpProcesses $svc.Proc $root).Count -gt 0
    if (-not $running) { return 'stopped' }
    if ($null -eq $svc.Port) { return 'running' }
    if (& $script:BpPortOpen $svc.Port) { return 'running' }
    return 'starting'
}
# Why a part this window started is gone: its exit code (or '' when unknown).
function Get-BpExitNote([string]$key) {
    $p = $script:BpStarted[$key]
    if (-not $p) { return '' }
    try { if ($p.HasExited) { return " (it closed with exit code $($p.ExitCode))" } } catch { }
    return ''
}
function Start-BpService($svc, [string]$root) {
    $s = Get-BpState $svc $root
    if ($s -eq 'missing') { return "not found: $($svc.Exe)" }
    if ($s -ne 'stopped') { return $null }
    try { $n = & $script:BpStartProcess $svc } catch { return "could not start - $($_.Exception.Message)" }
    if ($n) { return "started - $n" }
    return $null
}
function Stop-BpService($svc, [string]$root) {
    $p = $script:BpStarted[$svc.Key]
    if ($p -and $svc.Input) { try { if (-not $p.HasExited) { $p.StandardInput.WriteLine('exit'); $p.StandardInput.Flush(); [void]$p.WaitForExit(3000) } } catch { } }   # (the DNS server takes its hosts entry back out)
    foreach ($q in @(& $script:BpProcesses $svc.Proc $root)) { try { Stop-Process -Id $q.Id -Force -ErrorAction Stop } catch { } }
    $script:BpStarted.Remove($svc.Key)
}

# ---- one click: everything BF3 needs, in order, without freezing the window. New-BpLaunch makes the plan;
# Step-BpLaunch is called by a timer (every half second) and does one thing at a time. ----
function New-BpLaunch([string]$serverDir, $instances, [int[]]$servers, [bool]$dns = $false, $cfg = $null) {
    if (-not $cfg) { $cfg = Read-BpConfig $serverDir }
    $steps = New-Object System.Collections.ArrayList
    foreach ($svc in (Get-BpServices $serverDir $cfg)) {
        $on = $svc.Enabled -or ($svc.Key -eq 'dns' -and $dns)
        if ($svc.Key -eq 'dns') { $on = $dns }   # (the DNS box on the BF-Portable page decides)
        if (-not $on) { continue }
        [void]$steps.Add([pscustomobject]@{ Kind = 'service'; Svc = $svc; Inst = $null })
    }
    foreach ($inst in $instances) { if ($servers -contains $inst.Number) { [void]$steps.Add([pscustomobject]@{ Kind = 'server'; Svc = $null; Inst = $inst }) } }
    return [pscustomobject]@{ Root = (Get-BpRoot $serverDir); ServerDir = $serverDir; Steps = $steps; Index = 0; Phase = 'start'; Since = 0.0; Done = $false; Failed = $false
        Log = (New-Object System.Collections.ArrayList); Ready = @{}; Gap = [double]$cfg.ServerGapSeconds; ServerWindow = $cfg.ServerWindow; Cfg = $cfg }
}
# $now: seconds (any clock). Returns the line to show, or $null when nothing new happened.
function Step-BpLaunch($l, [double]$now) {
    if ($l.Done) { return $null }
    if ($l.Index -ge $l.Steps.Count) { $l.Done = $true; $m = $(if ($l.Failed) { 'Done, with problems (see above).' } else { 'BF-Portable is up.' }); [void]$l.Log.Add($m); return $m }
    $st = $l.Steps[$l.Index]
    if ($st.Kind -eq 'server') {
        if ($l.Phase -eq 'start') {
            $running = $null -ne (& $script:BpServerProcess $st.Inst)
            if (-not $running) {
                try { & $script:BpStartServer $st.Inst $l.ServerWindow; $m = "Starting $($st.Inst.Label) (port $($st.Inst.GamePort))" }
                catch { $m = "$($st.Inst.Label): could not start - $($_.Exception.Message)"; $l.Failed = $true }
            }
            else { $m = "$($st.Inst.Label) is already running" }
            $l.Phase = 'gap'; $l.Since = $now; [void]$l.Log.Add($m); return $m
        }
        if ($l.Phase -eq 'gap') {
            if ($now - $l.Since -lt [Math]::Max(1.5, $l.Gap)) { return $null }   # (servers load the same files: a few seconds apart)
            # V14: is it there? (a .bat that failed closes at once)
            $l.Index++; $l.Phase = 'start'
            if ($null -eq (& $script:BpServerProcess $st.Inst)) { $m = "$($st.Inst.Label) is not running - its window may show why ($($st.Inst.Bat))"; [void]$l.Log.Add($m); $l.Failed = $true; return $m }
            return $null
        }
    }
    $svc = $st.Svc
    if ($l.Phase -eq 'start') {
        if ($svc.Key -eq 'dns' -and $svc.Input -and (-not $l.Cfg.DnsDomain -or -not $l.Cfg.DnsIp)) {   # it would sit waiting for its answers
            $l.Index++; $m = 'LAN DNS server: skipped - set the LAN domain name and IP in Launcher settings first'; [void]$l.Log.Add($m); return $m
        }
        $s = Get-BpState $svc $l.Root
        if ($s -eq 'missing') {
            $m = "$($svc.Label): not found ($($svc.Exe)) - set its program in Launcher settings"; [void]$l.Log.Add($m); $l.Failed = $true
            if ($svc.Required) { $l.Done = $true; $m2 = 'Stopped: BF3 cannot run without it.'; [void]$l.Log.Add($m2); return $m2 }
            $l.Index++; return $m
        }
        if ($s -eq 'running') { $l.Ready[$svc.Key] = $true; $l.Index++; $m = "$($svc.Label) is already running"; [void]$l.Log.Add($m); return $m }
        $note = $null
        if ($s -eq 'stopped') {
            try { $note = & $script:BpStartProcess $svc }
            catch {
                $m = "$($svc.Label): could not start - $($_.Exception.Message)"; [void]$l.Log.Add($m); $l.Failed = $true
                if ($svc.Required) { $l.Done = $true; [void]$l.Log.Add('Stopped: BF3 cannot run without it.') } else { $l.Index++ }
                return $m
            }
        }
        $l.Phase = 'wait'; $l.Since = $now; $m = "Starting $($svc.Label)..." + $(if ($note) { " ($note)" } else { '' }); [void]$l.Log.Add($m); return $m
    }
    $s = Get-BpState $svc $l.Root
    if ($s -eq 'running' -and ($null -ne $svc.Port -or $now - $l.Since -ge [Math]::Min(2, $svc.Wait))) {
        $l.Ready[$svc.Key] = $true; $l.Index++; $l.Phase = 'start'; $m = "$($svc.Label) is up"; [void]$l.Log.Add($m); return $m
    }
    if ($now - $l.Since -ge $svc.Wait) {
        $m = "$($svc.Label) did not come up in $($svc.Wait) s"
        if ($s -eq 'stopped') { $m += (Get-BpExitNote $svc.Key); $m += ' - it closed: another copy may hold its port, or it needs administrator rights (Launcher settings)' }
        elseif ($s -eq 'starting') { $m += " - it runs, but port $($svc.Port) does not answer yet" }
        [void]$l.Log.Add($m); $l.Failed = $true
        if ($svc.Required) { $l.Done = $true; [void]$l.Log.Add('Stopped: BF3 cannot run without it.') }
        else { $l.Index++; $l.Phase = 'start' }
        return $m
    }
    return $null
}
$script:BpServerProcess = { param($inst) Get-BmServerProcess $inst -Fresh }
$script:BpStartServer = { param($inst, $window) Start-BmServer $inst $window }
# Stops the game servers first, then the services in reverse order.
function Stop-BpAll([string]$serverDir, $instances) {
    foreach ($inst in $instances) { try { [void](Stop-BmServer $inst) } catch { } }
    $svcs = @(Get-BpServices $serverDir); [array]::Reverse($svcs)
    foreach ($svc in $svcs) { Stop-BpService $svc (Get-BpRoot $serverDir) }
}
# The launcher log (Launcher.log next to this script): what the one click did, kept for later (and for diagnostics).
function Write-BpLog([string]$line) {
    if (-not $script:BpConfigPath) { return }
    try {
        $p = Join-Path (Split-Path -Parent $script:BpConfigPath) 'Launcher.log'
        if ((Test-Path -LiteralPath $p) -and (Get-Item -LiteralPath $p).Length -gt 1MB) { Move-Item -LiteralPath $p -Destination ($p + '.old') -Force }
        [IO.File]::AppendAllText($p, (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + '  ' + $line + "`r`n")
    } catch { }
}

# ---- each server's game rules: Instance<n>\Admin\Startup.txt (vars.* lines the server runs at start) and the
# name / player count in its _StartServer*.bat. admin.password and anything else in the files is kept as it is. ----
function New-BmVar($key, $kind, $min, $max, $normal, $hardcore, $label, $help, [bool]$live = $true) {
    return [pscustomobject]@{ Key = $key; Kind = $kind; Min = $min; Max = $max; Normal = [string]$normal; Hardcore = [string]$hardcore; Label = $label; Help = $help; Live = $live }
}
# Normal / Hardcore are BF-Portable's own presets ($null: the preset leaves it alone).
$script:BmServerVars = @(
    (New-BmVar 'serverName' 'text' 1 63 $null $null 'Server name' 'shown in the server browser and Battlelog'),
    (New-BmVar 'serverDescription' 'text' 0 255 $null $null 'Description' 'server browser details'),
    (New-BmVar 'serverMessage' 'text' 0 255 $null $null 'Welcome message' 'shown to players in game'),
    (New-BmVar 'maxPlayers' 'int' 64 64 '64' '64' 'Max players' 'must stay 64 on BF-Portable: its Blaze emulator fails on any other limit and the server drops every player ~2 min after the start (V16.2.2). Use the bot count for fewer players'),
    (New-BmVar 'gameModeCounter' 'int' 1 2000 100 100 'Tickets %' 'round length (100 = normal)'),
    (New-BmVar 'friendlyFire' 'bool' 0 1 'false' 'true' 'Friendly fire' ''),
    (New-BmVar 'regenerateHealth' 'bool' 0 1 'true' 'false' 'Health regenerates' ''),
    (New-BmVar 'soldierHealth' 'int' 1 300 100 60 'Soldier health %' ''),
    (New-BmVar 'bulletDamage' 'int' 1 300 100 100 'Bullet damage %' '(bot bullets: also BotDamage in the bot settings)'),
    (New-BmVar 'hud' 'bool' 0 1 'true' 'false' 'HUD' ''),
    (New-BmVar 'crossHair' 'bool' 0 1 'true' 'false' 'Crosshair' ''),
    (New-BmVar 'miniMap' 'bool' 0 1 'true' 'true' 'Minimap' ''),
    (New-BmVar 'miniMapSpotting' 'bool' 0 1 'true' 'true' 'Minimap spotting' ''),
    (New-BmVar '3dSpotting' 'bool' 0 1 'true' 'false' '3D spotting' ''),
    (New-BmVar 'nameTag' 'bool' 0 1 'true' 'false' 'Name tags' ''),
    (New-BmVar 'killCam' 'bool' 0 1 'true' 'false' 'Kill cam' ''),
    (New-BmVar '3pCam' 'bool' 0 1 'true' 'false' '3rd-person vehicle cam' ''),
    (New-BmVar 'onlySquadLeaderSpawn' 'bool' 0 1 'false' 'true' 'Spawn on squad leader only' ''),
    (New-BmVar 'playerManDownTime' 'int' 1 1000 100 100 'Man-down time %' 'how long a downed soldier can be revived'),
    (New-BmVar 'vehicleSpawnAllowed' 'bool' 0 1 'true' 'true' 'Vehicles' ''),
    (New-BmVar 'vehicleSpawnDelay' 'int' 1 1000 100 100 'Vehicle respawn time %' ''),
    (New-BmVar 'autoBalance' 'bool' 0 1 $null $null 'Auto-balance teams' ''),
    (New-BmVar 'roundStartPlayerCount' 'int' 0 64 $null $null 'Players to start a round' ''),
    (New-BmVar 'roundRestartPlayerCount' 'int' -1 64 $null $null 'Players to restart a round' '-1 = never'),
    (New-BmVar 'roundWarmupTimeout' 'int' 0 3600 $null $null 'Warm-up time (s)' ''),
    (New-BmVar 'roundLockdownCountdown' 'int' 0 300 $null $null 'Lock-down countdown (s)' ''),
    (New-BmVar 'idleTimeout' 'int' 0 86400 $null $null 'Idle kick (s)' '0 = never'),
    (New-BmVar 'teamKillCountForKick' 'int' 0 100 $null $null 'Team kills before a kick' '0 = never'),
    (New-BmVar 'unlockMode' 'choice' 0 0 $null $null 'Unlocks' 'none / all / common / stats' $false),
    (New-BmVar 'premiumStatus' 'bool' 0 1 $null $null 'Premium for everyone' '' $false)
)
$script:BmUnlockModes = @('none', 'all', 'common', 'stats')
function Get-BmStartupPath($instance) {
    $d = $instance.InstanceDir
    if (-not $d) { $d = Join-Path (Split-Path -Parent $instance.Bat) $(if ($instance.Number -eq 1) { 'Instance' } else { "Instance$($instance.Number)" }) }
    return Join-Path $d 'Admin\Startup.txt'
}
# The value text of a vars.* line ("quoted" or bare), $null when the line is not that variable.
function Get-BmStartupValue([string]$line, [string]$key) {
    if ($line -notmatch ('^\s*vars\.' + [regex]::Escape($key) + '\s+(.*)$')) { return $null }
    $v = $Matches[1].Trim()
    if ($v.StartsWith('"')) { $v = $v.Substring(1); $e = $v.LastIndexOf('"'); if ($e -ge 0) { $v = $v.Substring(0, $e) }; $v = $v.Replace('"', '') }
    return $v
}
function Test-BmVarValue($var, [string]$text) {
    $t = ([string]$text).Trim()
    switch ($var.Kind) {
        'bool' { if ($t -match '^(true|on|1|yes)$') { return 'true' }; if ($t -match '^(false|off|0|no)$') { return 'false' }; return $null }
        'text' { $t = $t.Replace('"', "'"); if ($t.Length -lt $var.Min -or $t.Length -gt $var.Max) { return $null }; if ($t -match '[\x00-\x1f]') { return $null }; return $t }
        'choice' { if ($script:BmUnlockModes -contains $t.ToLower()) { return $t.ToLower() }; return $null }
        default { $n = 0; if (-not [int]::TryParse($t, [ref]$n) -or $n -lt $var.Min -or $n -gt $var.Max) { return $null }; return [string]$n }
    }
}
function Format-BmStartupLine($var, [string]$value) { if ($var.Kind -eq 'text' -or $var.Kind -eq 'choice') { return "vars.$($var.Key) `"$value`"" }; return "vars.$($var.Key) $value" }
# ---- AltBotMod V14: the map list file (Instance<n>\Admin\MapList.txt) - what the server loads at start, editable
# while it is off. Lines are "MAP MODE ROUNDS" (BF-Portable writes that format; the server's mapList.save too). ----
function Get-BmMapListPath($instance) { return Join-Path (Split-Path -Parent (Get-BmStartupPath $instance)) 'MapList.txt' }
function Read-BmMapListFile($instance) {
    $p = Get-BmMapListPath $instance; $list = New-Object System.Collections.ArrayList
    if (-not (Test-Path -LiteralPath $p)) { return ,$list.ToArray() }
    foreach ($l in [IO.File]::ReadAllLines($p)) {
        $w = @($l.Trim() -split '\s+' | Where-Object { $_ })
        if ($w.Count -lt 2 -or $w[0].StartsWith('#') -or $w[0].StartsWith('//')) { continue }
        $r = 1; if ($w.Count -ge 3) { [void][int]::TryParse($w[2], [ref]$r) }; if ($r -lt 1) { $r = 1 }
        [void]$list.Add([pscustomobject]@{ Index = $list.Count; Map = $w[0]; Mode = $w[1]; Rounds = $r })
    }
    return ,$list.ToArray()
}
# Writes the list (a copy of the old file is kept once as MapList.before-edit.txt). Throws on an empty list.
function Write-BmMapListFile($instance, $entries) {
    $e = @($entries); if (-not $e.Count) { throw 'the map list needs at least one map' }
    $p = Get-BmMapListPath $instance; $d = Split-Path -Parent $p
    if (-not (Test-Path -LiteralPath $d)) { [void](New-Item -ItemType Directory -Force -Path $d) }
    $bak = Join-Path $d 'MapList.before-edit.txt'
    if ((Test-Path -LiteralPath $p) -and -not (Test-Path -LiteralPath $bak)) { Copy-Item -LiteralPath $p -Destination $bak }
    [IO.File]::WriteAllLines($p, [string[]]@($e | ForEach-Object { '{0} {1} {2}' -f $_.Map, $_.Mode, [Math]::Max(1, [int]$_.Rounds) }))
}
# Every map in one mode family: 'conquest' (Conquest Large; Conquest Assault Large on the assault maps; close-quarters
# maps left out), 'conquestsmall', 'rush', 'tdm', 'sqdm', 'domination' (close-quarters maps).
function Get-BmPresetRotation([string]$kind) {
    $out = New-Object System.Collections.ArrayList
    foreach ($m in $script:BmMaps) {
        $modes = Get-BmMapModes $m.Code; $mode = $null
        switch ($kind) {
            'conquest' { if (-not $m.Cq) { $mode = $(if ($m.Assault) { 'ConquestAssaultLarge0' } else { 'ConquestLarge0' }) } }
            'conquestsmall' { if (-not $m.Cq) { $mode = $(if ($m.Assault) { 'ConquestAssaultSmall0' } else { 'ConquestSmall0' }) } }
            'rush' { if ($modes -contains 'RushLarge0') { $mode = 'RushLarge0' } }
            'tdm' { $mode = $(if ($modes -contains 'TeamDeathMatch0') { 'TeamDeathMatch0' } else { 'TeamDeathMatchC0' }) }
            'sqdm' { $mode = 'SquadDeathMatch0' }
            'domination' { if ($m.Cq) { $mode = 'Domination0' } }
        }
        if ($mode) { [void]$out.Add([pscustomobject]@{ Index = $out.Count; Map = $m.Code; Mode = $mode; Rounds = 1 }) }
    }
    return ,$out.ToArray()
}
# Current rules of a server: one row per variable (value from Startup.txt, the player limit from the .bat).
function Get-BmServerSetup($instance) {
    $lines = @(); $p = Get-BmStartupPath $instance
    if (Test-Path -LiteralPath $p) { $lines = [IO.File]::ReadAllLines($p) }
    $rows = @()
    foreach ($v in $script:BmServerVars) {
        $val = $null
        foreach ($l in $lines) { $x = Get-BmStartupValue $l $v.Key; if ($null -ne $x) { $val = $x } }
        if ($null -ne $val) { $ok = Test-BmVarValue $v $val; if ($null -ne $ok) { $val = $ok } }
        $rows += [pscustomobject]@{ Var = $v; Value = $val }
    }
    return $rows
}
function Get-BmBatValue([string]$bat, [string]$name) { return Read-BpKeyValue $bat ('set ' + $name) }
# Writes checked values (key -> text from Test-BmVarValue) into Startup.txt - a variable's line replaced where it is,
# new ones added at the end - and the server name / player limit into the .bat.
function Set-BmServerSetup($instance, [hashtable]$values) {
    $p = Get-BmStartupPath $instance
    $lines = New-Object System.Collections.ArrayList
    if (Test-Path -LiteralPath $p) { foreach ($l in [IO.File]::ReadAllLines($p)) { [void]$lines.Add($l) } }
    else { [void](New-Item -ItemType Directory -Force -Path (Split-Path -Parent $p)) }
    $byKey = @{}; foreach ($v in $script:BmServerVars) { $byKey[$v.Key] = $v }
    foreach ($k in @($values.Keys)) {
        $var = $byKey[$k]; if (-not $var) { throw "unknown server setting $k" }
        $line = Format-BmStartupLine $var $values[$k]; $done = $false
        for ($n = $lines.Count - 1; $n -ge 0; $n--) {
            if ($null -ne (Get-BmStartupValue $lines[$n] $k)) { if (-not $done) { $lines[$n] = $line; $done = $true } else { $lines.RemoveAt($n) } }
        }
        if (-not $done) { [void]$lines.Add($line) }
    }
    [IO.File]::WriteAllLines($p, [string[]]$lines.ToArray())
    if ($values.ContainsKey('serverName') -or $values.ContainsKey('maxPlayers')) {
        $b = [IO.File]::ReadAllLines($instance.Bat)
        for ($n = 0; $n -lt $b.Count; $n++) {
            if ($values.ContainsKey('serverName') -and $b[$n] -match '^\s*set\s+SERVERNAME=') { $b[$n] = 'set SERVERNAME=' + ($values['serverName'] -replace '[%&|<>^"]', '') }
            if ($values.ContainsKey('maxPlayers') -and $b[$n] -match '^\s*set\s+PLAYERS=') { $b[$n] = 'set PLAYERS=' + $values['maxPlayers'] }
        }
        [IO.File]::WriteAllLines($instance.Bat, [string[]]$b)
    }
}
# Sends the live-changeable values to the running server. Returns the ones it refused (name: reason).
function Send-BmServerSetup($conn, [hashtable]$values) {
    $refused = @()
    foreach ($v in $script:BmServerVars) {
        if (-not $values.ContainsKey($v.Key) -or -not $v.Live) { continue }
        $w = Invoke-BmRcon $conn @("vars.$($v.Key)", [string]$values[$v.Key])
        if (-not $w) { $refused += "$($v.Label): no answer"; continue }
        if ($w[0] -ne 'OK') { $refused += "$($v.Label): $($w[0])" }
    }
    return $refused
}

# ---- Battlelog inside the window: the embedded browser uses Internet Explorer's engine, which shows pages as IE7
# unless the program is listed for IE11 mode (per user, no admin rights needed). ----
function Set-BpBrowserMode([string]$exeName) {
    $k = 'HKCU:\Software\Microsoft\Internet Explorer\Main\FeatureControl\FEATURE_BROWSER_EMULATION'
    try {
        if (-not (Test-Path $k)) { [void](New-Item -Path $k -Force) }
        $cur = (Get-ItemProperty -Path $k -Name $exeName -ErrorAction SilentlyContinue).$exeName
        if ($cur -ne 11001) { [void](New-ItemProperty -Path $k -Name $exeName -Value 11001 -PropertyType DWord -Force) }
        return $true
    } catch { return $false }
}
function Find-BpEdge {
    foreach ($p in @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe", "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe")) {
        if ($p -and (Test-Path -LiteralPath $p)) { return $p }
    }
    return $null
}

# ---- AltBotMod V16.2: network - one IP for everything that needs it (IP configurator) ----
# The LAN address of this PC lives in four places: the redirector's ServerIP.txt and its config.ini (ip_host), the LAN
# DNS answers (Launcher.json DnsIp) and the BF Launcher's backend address (Settings.ini BaseURL). Each one is shown with
# what it says now; only the ticked ones are written, each file backed up once (<file>.before-ip).
function Get-BpLocalIps {
    $ips = @()
    try {
        foreach ($n in [Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces()) {
            if ($n.OperationalStatus -ne 'Up' -or $n.NetworkInterfaceType -eq 'Loopback') { continue }
            foreach ($u in $n.GetIPProperties().UnicastAddresses) { if ($u.Address.AddressFamily -eq 'InterNetwork') { $ips += $u.Address.ToString() } }
        }
    } catch { }
    return @($ips | Select-Object -Unique)
}
function Test-BpIp([string]$ip) { $a = $null; return ($ip -match '^\d{1,3}(\.\d{1,3}){3}$') -and [Net.IPAddress]::TryParse($ip, [ref]$a) }
function Get-BpIniValue([string]$path, [string]$key) {
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    foreach ($l in [IO.File]::ReadAllLines($path)) { if ($l -match ('^\s*' + [regex]::Escape($key) + '\s*=\s*(.*?)\s*$')) { return $Matches[1] } }
    return $null
}
function Set-BpIniValue([string]$path, [string]$key, [string]$value) {
    $lines = [IO.File]::ReadAllLines($path); $done = $false
    for ($k = 0; $k -lt $lines.Count; $k++) {
        if ($lines[$k] -match ('^(\s*' + [regex]::Escape($key) + '\s*=\s*).*$')) { $lines[$k] = $Matches[1] + $value; $done = $true; break }
    }
    if (-not $done) { return $false }
    [IO.File]::WriteAllText($path, (($lines -join "`r`n") + "`r`n")); return $true
}
function Get-BpUrlHost([string]$url) { if ($url -match '^\s*https?://([^/:]+)') { return $Matches[1] }; return $null }
function Get-BpIpTargets([string]$serverDir, $cfg = $null) {
    $root = Get-BpRoot $serverDir; if (-not $cfg) { $cfg = Read-BpConfig $serverDir }
    $sip = Join-Path $root 'Redirector\ServerIP.txt'; $ini = Join-Path $root 'Redirector\config.ini'; $cl = Join-Path $root 'Client-Files\BF-Launcher\Settings.ini'
    return @(
        [pscustomobject]@{ Key = 'serverip'; Label = 'Redirector server IP (Redirector\ServerIP.txt)'; Path = $sip; Exists = (Test-Path -LiteralPath $sip); Current = $(if (Test-Path -LiteralPath $sip) { ([IO.File]::ReadAllText($sip)).Trim() } else { '' }) }
        [pscustomobject]@{ Key = 'redirector'; Label = 'Redirector target (Redirector\config.ini ip_host)'; Path = $ini; Exists = ($null -ne (Get-BpIniValue $ini 'ip_host')); Current = [string](Get-BpIniValue $ini 'ip_host') }
        [pscustomobject]@{ Key = 'dns'; Label = 'LAN DNS answer (Launcher settings)'; Path = $script:BpConfigPath; Exists = $true; Current = [string]$cfg.DnsIp }
        [pscustomobject]@{ Key = 'client'; Label = 'BF Launcher backend (Client-Files\BF-Launcher\Settings.ini BaseURL)'; Path = $cl; Exists = ($null -ne (Get-BpIniValue $cl 'BaseURL')); Current = [string](Get-BpUrlHost (Get-BpIniValue $cl 'BaseURL')) }
    )
}
function Backup-BpOnce([string]$path) { $b = $path + '.before-ip'; if ((Test-Path -LiteralPath $path) -and -not (Test-Path -LiteralPath $b)) { Copy-Item -LiteralPath $path -Destination $b } }
# Writes $ip to the targets whose Key is in $keys; returns what it changed (labels). Throws on a bad address.
function Set-BpIp([string]$serverDir, [string]$ip, [string[]]$keys, $cfg = $null) {
    if (-not (Test-BpIp $ip)) { throw "not an IPv4 address: $ip" }
    if (-not $cfg) { $cfg = Read-BpConfig $serverDir }
    $done = @()
    foreach ($t in (Get-BpIpTargets $serverDir $cfg)) {
        if ($keys -notcontains $t.Key -or -not $t.Exists -or $t.Current -eq $ip) { continue }
        switch ($t.Key) {
            'serverip' { Backup-BpOnce $t.Path; [IO.File]::WriteAllText($t.Path, $ip); $done += $t.Label }
            'redirector' { Backup-BpOnce $t.Path; if (Set-BpIniValue $t.Path 'ip_host' $ip) { $done += $t.Label } }
            'dns' { $cfg.DnsIp = $ip; Save-BpConfig $cfg; $done += $t.Label }
            'client' {
                $u = Get-BpIniValue $t.Path 'BaseURL'
                if ($u -match '^(\s*https?://)([^/:]+)(.*)$') { Backup-BpOnce $t.Path; if (Set-BpIniValue $t.Path 'BaseURL' ($Matches[1] + $ip + $Matches[3])) { $done += $t.Label } }
            }
        }
    }
    return $done
}

# ---- AltBotMod V16.2: accounts - log in to the BF-Portable backend and join a server, as the BF Launcher does ----
# The BF Launcher (Client-Files\BF-Launcher): POST <BaseURL>login_api.php (username, password) -> AuthCode, user_id;
# GET <BaseURL>get_servers_bf3.php?key=<APIKey> -> gid, gnam, ...; then BFUlauncher.exe in the BF3 folder (Path.txt)
# with "bf3Lan://" + Base64(UTF-8 of the game's command line below) - the exact string of its BF3 page (bf3.JoinServer).
function Get-BpClientConfig([string]$serverDir) {
    $d = Join-Path (Get-BpRoot $serverDir) 'Client-Files\BF-Launcher'
    $ini = Join-Path $d 'Settings.ini'; $pt = Join-Path $d 'Path.txt'
    $bf3 = ''; if (Test-Path -LiteralPath $pt) { $bf3 = @([IO.File]::ReadAllLines($pt) | Where-Object { $_.Trim() } | Select-Object -First 1) -join '' }
    return [pscustomobject]@{ Dir = $d; BaseUrl = [string](Get-BpIniValue $ini 'BaseURL'); ApiKey = [string](Get-BpIniValue $ini 'APIKey'); Bf3Path = $bf3.Trim() }
}
function New-BpBf3JoinArgs([string]$ip, [string]$authCode, [string]$userId, [string]$gameId) {
    $fmt = '-blazeIp {0} -Core.IgnoreAlways true -Core.EnableJuice false -DisplayAsserts false -super layout.toc -dataPlatform Win32 -webMode MP -Origin_NoAppFocus -AuthCode "{1}" -requestState State_ClaimReservation -requestStateParams "<data logintoken=\"{2}\" levelmode=\"mp\" personaref=\"{3}\" role=\"soldier\" gameid=\"{4}\" putinsquad=\"true\"></data>"|{5}|'
    return [string]::Format($fmt, $ip, $authCode, $authCode, $userId, $gameId, $ip)
}
function New-BpBf3JoinUrl([string]$ip, [string]$authCode, [string]$userId, [string]$gameId) {
    return 'bf3Lan://' + [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes((New-BpBf3JoinArgs $ip $authCode $userId $gameId)))
}
function Resolve-BpBlazeIp([string]$baseUrl) {
    $h = Get-BpUrlHost $baseUrl; if (-not $h) { return $null }
    if (Test-BpIp $h) { return $h }
    try { return ([Net.Dns]::GetHostAddresses($h) | Where-Object { $_.AddressFamily -eq 'InterNetwork' } | Select-Object -First 1).ToString() } catch { return $null }
}
function Join-BpUrl([string]$base, [string]$page) { return $base.TrimEnd('/') + '/' + $page }
function Invoke-BpLogin([string]$baseUrl, [string]$user, [string]$password) {
    try { $r = Invoke-RestMethod -Method Post -Uri (Join-BpUrl $baseUrl 'login_api.php') -Body @{ username = $user; password = $password } -TimeoutSec 8 }
    catch { return [pscustomobject]@{ Ok = $false; Message = 'backend not reachable: ' + $_.Exception.Message } }
    if ($r.status -eq 'success' -and $r.AuthCode) { return [pscustomobject]@{ Ok = $true; AuthCode = [string]$r.AuthCode; UserId = [string]$r.user_id; Message = 'logged in' } }
    return [pscustomobject]@{ Ok = $false; Message = $(if ($r.message) { [string]$r.message } else { 'login refused' }) }
}
function Get-BpServerList([string]$baseUrl, [string]$apiKey) {
    try { $r = Invoke-RestMethod -Uri ((Join-BpUrl $baseUrl 'get_servers_bf3.php') + '?key=' + [Uri]::EscapeDataString($apiKey)) -Headers @{ 'X-API-Key' = $apiKey } -TimeoutSec 8 }
    catch { return @() }
    return @($r | Where-Object { $_ -and $_.gid })
}
function Start-BpBf3Join([string]$bf3Path, [string]$url) {
    $exe = Join-Path $bf3Path 'BFUlauncher.exe'
    if (-not (Test-Path -LiteralPath $exe)) { throw "BFUlauncher.exe not found in $bf3Path (BF Launcher: Path.txt)" }
    $psi = New-Object Diagnostics.ProcessStartInfo; $psi.FileName = $exe; $psi.Arguments = $url; $psi.WorkingDirectory = $bf3Path; $psi.UseShellExecute = $true
    return [Diagnostics.Process]::Start($psi)
}
# Saved accounts (BotMod\ServerControl\Accounts.json): passwords encrypted for this Windows user (DPAPI).
$script:BpAccountsPath = $null
function Protect-BpSecret([string]$s) {
    if (-not $s) { return '' }
    try { Add-Type -AssemblyName System.Security -ErrorAction Stop; return 'dpapi:' + [Convert]::ToBase64String([Security.Cryptography.ProtectedData]::Protect([Text.Encoding]::UTF8.GetBytes($s), $null, 'CurrentUser')) }
    catch { return 'b64:' + [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($s)) }   # (no DPAPI: not Windows - tests only)
}
function Unprotect-BpSecret([string]$s) {
    if (-not $s) { return '' }
    try {
        if ($s.StartsWith('dpapi:')) { Add-Type -AssemblyName System.Security; return [Text.Encoding]::UTF8.GetString([Security.Cryptography.ProtectedData]::Unprotect([Convert]::FromBase64String($s.Substring(6)), $null, 'CurrentUser')) }
        if ($s.StartsWith('b64:')) { return [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($s.Substring(4))) }
    } catch { }
    return ''
}
function Read-BpAccounts([string]$path = $script:BpAccountsPath) {
    $o = [pscustomobject]@{ Accounts = @(); Default = ''; AutoJoin = $false; AutoJoinServer = '' }
    if (-not $path -or -not (Test-Path -LiteralPath $path)) { return $o }
    try { $j = [IO.File]::ReadAllText($path) | ConvertFrom-Json } catch { return $o }
    $o.Accounts = @($j.Accounts | Where-Object { $_.User } | ForEach-Object { [pscustomobject]@{ User = [string]$_.User; Secret = [string]$_.Secret; Backend = [string]$_.Backend } })
    $o.Default = [string]$j.Default; $o.AutoJoin = [bool]$j.AutoJoin; $o.AutoJoinServer = [string]$j.AutoJoinServer
    return $o
}
function Save-BpAccounts($o, [string]$path = $script:BpAccountsPath) {
    if (-not $path) { return }
    $j = [ordered]@{ Accounts = @($o.Accounts | ForEach-Object { [ordered]@{ User = $_.User; Secret = $_.Secret; Backend = $(if ($_.PSObject.Properties['Backend']) { [string]$_.Backend } else { '' }) } }); Default = $o.Default; AutoJoin = [bool]$o.AutoJoin; AutoJoinServer = $o.AutoJoinServer }
    [IO.File]::WriteAllText($path, ($j | ConvertTo-Json -Depth 4))
}
# Logs in with a saved account and starts BF3 into the server: returns a line for the log (or throws).
function Invoke-BpQuickJoin([string]$serverDir, $account, $server) {
    $c = Get-BpClientConfig $serverDir
    if (-not $c.BaseUrl) { throw 'the BF Launcher has no BaseURL (Client-Files\BF-Launcher\Settings.ini)' }
    if (-not $c.Bf3Path -or -not (Test-Path -LiteralPath $c.Bf3Path)) { throw "the BF3 folder in the BF Launcher's Path.txt is not set or missing ($($c.Bf3Path))" }
    $ip = Resolve-BpBlazeIp $c.BaseUrl; if (-not $ip) { throw "cannot resolve $($c.BaseUrl)" }
    $login = Invoke-BpLogin $c.BaseUrl $account.User (Unprotect-BpSecret $account.Secret)
    if (-not $login.Ok) { throw "login as $($account.User) failed: $($login.Message)" }
    [void](Start-BpBf3Join $c.Bf3Path (New-BpBf3JoinUrl $ip $login.AuthCode $login.UserId ([string]$server.gid)))
    return "BF3 starting: $($account.User) joins $($server.gnam) (game $($server.gid), backend $ip)"
}

# ---- AltBotMod V21.1: LAN servers - other PCs running BF-Portable on this network ----
# Every BF-Portable backend answers http://<pc>/api/generate_key.php with a key for the asking PC, lists its running BF3
# servers (get_servers_bf3.php) and registers players (register_api.php). Joining a server on another PC = logging in
# to THAT PC's backend (an account there) and starting BF3 with -blazeIp <that PC>. Accounts remember their backend
# (Backend = its base URL; empty = this PC's own backend from the BF Launcher settings).
function Get-BpLanHosts([string[]]$localIps) {
    $own = @($localIps); $hosts = New-Object Collections.Generic.List[string]
    foreach ($ip in $own) {
        if ($ip -notmatch '^(\d+)\.(\d+)\.(\d+)\.(\d+)$' -or $ip -like '127.*' -or $ip -like '169.254.*') { continue }
        $net = "$($Matches[1]).$($Matches[2]).$($Matches[3])."
        for ($k = 1; $k -le 254; $k++) { $h = $net + $k; if ($own -notcontains $h -and -not $hosts.Contains($h)) { $hosts.Add($h) } }
    }
    return $hosts.ToArray()
}
# The hosts (of $hosts) that accept a TCP connection on $port within $timeoutMs - all tried at once.
function Test-BpOpenPorts([string[]]$hosts, [int]$port = 80, [int]$timeoutMs = 600) {
    $tries = @(); foreach ($h in $hosts) { $c = New-Object Net.Sockets.TcpClient; try { $tries += , @($h, $c, $c.ConnectAsync($h, $port)) } catch { $c.Dispose() } }
    $until = [DateTime]::UtcNow.AddMilliseconds($timeoutMs)
    while ([DateTime]::UtcNow -lt $until -and @($tries | Where-Object { -not $_[2].IsCompleted }).Count) { Start-Sleep -Milliseconds 40 }
    $open = @(); foreach ($t in $tries) { if ($t[2].Status -eq 'RanToCompletion' -and $t[1].Connected) { $open += $t[0] }; try { $t[1].Dispose() } catch { } }
    return $open
}
# This PC's key on a backend (generate_key.php hands out one key per asking PC). $null: not a BF-Portable backend.
function Get-BpBackendKey([string]$baseUrl, [int]$timeoutSec = 4) {
    try { $r = Invoke-RestMethod -Uri (Join-BpUrl $baseUrl 'generate_key.php') -TimeoutSec $timeoutSec } catch { return $null }
    if ($r -and $r.success -and $r.api_key) { return [string]$r.api_key }
    return $null
}
# The backends to show: this PC's own (BF Launcher settings) first, then every PC found on the network (and the extra
# hosts typed in, "pc" or "pc:port").
function Find-BpLanBackends([string]$serverDir, [string[]]$extraHosts = @(), [switch]$NoScan, [int]$port = 80) {
    $list = New-Object Collections.Generic.List[object]; $seen = @{}
    $c = $null; if ($serverDir) { try { $c = Get-BpClientConfig $serverDir } catch { } }
    if ($c -and $c.BaseUrl) {
        $lh = Get-BpUrlHost $c.BaseUrl; $lp = $(if ($c.BaseUrl -match '^\s*https?://[^/:]+:(\d+)') { [int]$Matches[1] } else { 80 })
        $list.Add([pscustomobject]@{ Name = 'This PC'; Host = $lh; BaseUrl = $c.BaseUrl; ApiKey = $c.ApiKey; Local = $true }); $seen[$lh] = $true; $seen["$lh`:$lp"] = $true
    }
    $local = @(Get-BpLocalIps); foreach ($ip in $local) { $seen[$ip] = $true }; $seen['localhost'] = $true; $seen['127.0.0.1'] = $true
    $targets = New-Object Collections.Generic.List[object]
    foreach ($x in $extraHosts) { $x = $x.Trim(); if (-not $x) { continue }; $hp = $x -split ':'; $targets.Add(@($hp[0], $(if ($hp.Count -gt 1) { [int]$hp[1] } else { $port }), $true)) }
    if (-not $NoScan) { foreach ($h in (Test-BpOpenPorts (Get-BpLanHosts $local) $port)) { $targets.Add(@($h, $port, $false)) } }
    foreach ($t in $targets) {
        $h = $t[0]; if ($seen.ContainsKey("$h`:$($t[1])") -or ($seen.ContainsKey($h) -and -not $t[2])) { continue }
        $seen["$h`:$($t[1])"] = $true
        $base = $(if ($t[1] -eq 80) { "http://$h/api/" } else { "http://$h`:$($t[1])/api/" })
        $key = Get-BpBackendKey $base; if (-not $key) { continue }
        $name = $h; try { $name = ([Net.Dns]::GetHostEntry($h)).HostName } catch { }
        $list.Add([pscustomobject]@{ Name = $name; Host = $h; BaseUrl = $base; ApiKey = $key; Local = $false })
    }
    return $list.ToArray()
}
# The BF3 servers running behind each backend (each with .Backend).
function Get-BpLanServers($backends) {
    $all = @()
    foreach ($b in @($backends)) { foreach ($s in @(Get-BpServerList $b.BaseUrl $b.ApiKey)) { $s | Add-Member -NotePropertyName Backend -NotePropertyValue $b -Force; $all += $s } }
    return $all
}
# A new player on a backend (letters, digits and spaces). Returns Ok / Message.
function Invoke-BpRegister([string]$baseUrl, [string]$apiKey, [string]$user, [string]$password) {
    if ($user -notmatch '^[a-zA-Z0-9 ]+$') { return [pscustomobject]@{ Ok = $false; Message = 'a player name has letters, digits and spaces only' } }
    if (-not $password) { return [pscustomobject]@{ Ok = $false; Message = 'a password, please' } }
    try {
        $r = Invoke-RestMethod -Method Post -Uri ((Join-BpUrl $baseUrl 'register_api.php') + '?key=' + [Uri]::EscapeDataString($apiKey)) -Headers @{ 'X-API-Key' = $apiKey } -Body @{ username = $user; password = $password } -TimeoutSec 8
    } catch {
        $m = $_.Exception.Message; try { $b = $_.ErrorDetails.Message | ConvertFrom-Json; if ($b.message) { $m = [string]$b.message } } catch { }
        return [pscustomobject]@{ Ok = $false; Message = $m }
    }
    if ($r.status -eq 'success') { return [pscustomobject]@{ Ok = $true; Message = 'registered' } }
    return [pscustomobject]@{ Ok = $false; Message = $(if ($r.message) { [string]$r.message } else { 'registration refused' }) }
}
# The saved accounts for a backend: its own (Backend = its URL), and for this PC's backend the ones without one.
function Get-BpAccountsFor($acc, $backend) {
    return @($acc.Accounts | Where-Object { $_.Backend -eq $backend.BaseUrl -or ($backend.Local -and -not $_.Backend) })
}
# Logs in to the server's own backend and starts BF3 into it. Returns a line for the log (or throws).
function Invoke-BpLanJoin([string]$serverDir, $account, $server) {
    $b = $server.Backend; if (-not $b) { return Invoke-BpQuickJoin $serverDir $account $server }
    $c = Get-BpClientConfig $serverDir
    if (-not $c.Bf3Path -or -not (Test-Path -LiteralPath $c.Bf3Path)) { throw "the BF3 folder in the BF Launcher's Path.txt is not set or missing ($($c.Bf3Path))" }
    $ip = Resolve-BpBlazeIp $b.BaseUrl; if (-not $ip) { throw "cannot resolve $($b.BaseUrl)" }
    $login = Invoke-BpLogin $b.BaseUrl $account.User (Unprotect-BpSecret $account.Secret)
    if (-not $login.Ok) { throw "login as $($account.User) on $($b.Name) failed: $($login.Message)" }
    [void](Start-BpBf3Join $c.Bf3Path (New-BpBf3JoinUrl $ip $login.AuthCode $login.UserId ([string]$server.gid)))
    return "BF3 starting: $($account.User) joins $($server.gnam) on $($b.Name) (game $($server.gid), backend $ip)"
}
