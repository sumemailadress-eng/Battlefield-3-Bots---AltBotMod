# AltBotMod Command: the web / phone launcher, installed with AltBotMod V33 beside Server Control (which it reuses).
[CmdletBinding()]
param([string]$ServerDir, [ValidateRange(1024,65535)][int]$Port = 8787, [switch]$LocalOnly, [switch]$NoBrowser, [switch]$Desktop)
$ErrorActionPreference = 'Stop'
$base = $PSScriptRoot
$script:CommandVersion='V33 / Command 1.5'
trap {
    $why=$_.Exception.Message
    try { [IO.File]::AppendAllText((Join-Path $PSScriptRoot 'Command-error.log'),(Get-Date -Format s)+'  '+$why+"`r`n") } catch { }
    if ($Desktop) { try { Add-Type -AssemblyName System.Windows.Forms; [void][Windows.Forms.MessageBox]::Show($why,'AltBotMod Command - could not start','OK','Error') } catch { } }
    else { Write-Host $why -ForegroundColor Red }
    exit 1
} 
. (Join-Path $base 'Command-Window.ps1')
. (Join-Path $base 'ServerControl-Core.ps1')
. (Join-Path $base 'BFPortable-Core.ps1')
. (Join-Path $base 'LiveMap-Core.ps1')
. (Join-Path $base 'Learning-Core.ps1')
. (Join-Path $base 'Command-Learning.ps1')
. (Join-Path $base 'Command-Join.ps1')
Set-StrictMode -Off
$script:BpConfigPath = Join-Path $base 'Launcher.json'
$script:BpAccountsPath = Join-Path $base 'Accounts.json'
$script:Prefs = @{ ServerDir=''; LaunchServers=@(); StartDns=$false; BattlelogUrl='' }
$prefPath = Join-Path $base 'ServerControl.json'
if (Test-Path -LiteralPath $prefPath) { try { $p = [IO.File]::ReadAllText($prefPath) | ConvertFrom-Json; foreach ($v in $p.PSObject.Properties) { $script:Prefs[$v.Name]=$v.Value } } catch { } }
$dir = Resolve-BmServerDir $ServerDir
if (-not $dir) { $dir = Resolve-BmServerDir $script:Prefs.ServerDir }
if (-not $dir -and (Test-Path -LiteralPath (Join-Path $base 'Command-last-folder.txt'))) { $dir = Resolve-BmServerDir ([IO.File]::ReadAllText((Join-Path $base 'Command-last-folder.txt')).Trim()) }
if (-not $dir) { $dir = Find-BmServerDir $base }
if (-not $dir) {
    Add-Type -AssemblyName System.Windows.Forms
    $dlg = New-Object Windows.Forms.FolderBrowserDialog
    $dlg.Description = 'Select BF-Portable or the folder containing bf3_server_Final.exe'
    if ($dlg.ShowDialog() -eq 'OK') { $dir = Resolve-BmServerDir $dlg.SelectedPath }
}
if (-not $dir) { throw 'No BF3 server folder selected. Run again with -ServerDir or install beside your existing ServerControl files.' }
$Instances = Get-BmInstances $dir
if (-not $Instances.Count) { throw "No _StartServer*.bat with GAMEPORT and REMOTEADMINPORT in $dir" }
# A separate preference file avoids rewriting the original launcher's preferences.
[IO.File]::WriteAllText((Join-Path $base 'Command-last-folder.txt'), $dir)
$script:Cfg = Read-BpConfig $dir
$script:LaunchPlan = $null
$script:StatusCache = $null; $script:StatusAt = [datetime]::MinValue
$script:Feeds = @{}; $script:Audit = New-Object Collections.Generic.List[string]
$ips = @(Get-BpLocalIps | Where-Object { $_ -notlike '127.*' -and $_ -notlike '169.254.*' } | Select-Object -Unique)
$rng = [Security.Cryptography.RandomNumberGenerator]::Create(); $secret = New-Object byte[] 32; $rng.GetBytes($secret); $rng.Dispose()
$token = ([BitConverter]::ToString($secret)).Replace('-','').ToLowerInvariant()
$hosts = @("localhost:$Port", "127.0.0.1:$Port") + @($ips | ForEach-Object { "$_`:$Port" })
$urls = @($ips | ForEach-Object { "http://$_`:$Port/#key=$token" })
$localUrl = "http://127.0.0.1:$Port/#key=$token"
# Reopening the desktop launcher reuses this user's running local host/session.
$sessionPath=$null
if ($env:LOCALAPPDATA) {
    $sessionDir=Join-Path $env:LOCALAPPDATA 'AltBotMod\Command'
    [void](New-Item -ItemType Directory -Force -Path $sessionDir)
    $sessionPath=Join-Path $sessionDir ("session-$Port.json")
    if (Test-Path -LiteralPath $sessionPath) {
        $running=$null; $prior=$null
        try {
            $prior=[IO.File]::ReadAllText($sessionPath) | ConvertFrom-Json
            if ($prior.Key -match '^[0-9a-f]{64}$' -and $prior.Url -eq ("http://127.0.0.1:$Port/#key="+$prior.Key)) {
                $running=Invoke-RestMethod -Uri "http://127.0.0.1:$Port/api/meta" -Headers @{ Authorization=('Bearer '+$prior.Key) } -TimeoutSec 5
            }
        } catch { }
        if ($running) {
            if ($running.Version -ne $script:CommandVersion) { throw 'An older Command host is still running. Exit its tray icon or close its launcher console, then open the updated launcher. BF3 servers can keep running.' }
            if ($running.ServerDir -ne $dir -or [bool]$running.LocalOnly -ne [bool]$LocalOnly) { throw "Another Command host is running on port $Port with a different folder or LAN mode. Exit it using its tray icon or console, or select another -Port." }
            if (-not $NoBrowser) { if ($Desktop) { Open-CommandWindow $prior.Url } else { Start-Process $prior.Url } }
            exit 0
        }
    }
}
function Note([string]$text) { $script:Audit.Add((Get-Date -Format 'HH:mm:ss') + '  ' + $text); if ($script:Audit.Count -gt 150) { $script:Audit.RemoveAt(0) } }
function Inst($id) { $i = $Instances | Where-Object { $_.Number -eq [int]$id } | Select-Object -First 1; if (-not $i) { throw 'Unknown server instance' }; return $i }
function Conn($i) { return Connect-BmRcon $i 350 }
function Need-Conn($i) { $c = Conn $i; if (-not $c) { throw 'Server RCON is unavailable. Start the server and check its admin password/port.' }; return $c }
function Check-Result($e) { if ($e) { throw [string]$e } }
function Hash-Paths($paths) {
    $s = ''; foreach ($p in $paths) { $s += [string]$p + "`n"; if (Test-Path -LiteralPath $p) { $s += [Convert]::ToBase64String([IO.File]::ReadAllBytes($p)) }; $s += "`n" }
    $sha=[Security.Cryptography.SHA256]::Create(); try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($s)))).Replace('-','') } finally { $sha.Dispose() }
}
function Guard($version, $paths) { if (-not $version -or $version -ne (Hash-Paths $paths)) { throw 'CONFLICT: This file changed on another device or in the original UI. Refresh before saving.' } }
function Backup($paths) { foreach ($p in $paths) { if (Test-Path -LiteralPath $p) { $b=$p+'.before-command'; if (-not (Test-Path -LiteralPath $b)) { Copy-Item -LiteralPath $p -Destination $b } } } }
function Bot-Paths($i) { return @((Get-BmSettingsPath $dir $null), (Get-BmSettingsPath $dir $i)) | Select-Object -Unique }
function Setup-Paths($i) { return @((Get-BmStartupPath $i), $i.Bat) }
function Get-Status {
    if ($script:StatusCache -and ((Get-Date)-$script:StatusAt).TotalSeconds -lt 3) { return $script:StatusCache }
    $rows = @(foreach ($i in $Instances) {
        $c=Conn $i; $info=$null
        try { if ($c) { $info=Get-BmServerInfo $c } } finally { Disconnect-BmRcon $c }
        $running = $null -ne (Get-BmServerProcess $i)
        [pscustomobject]@{ Number=$i.Number; Label=$i.Label; Port=$i.GamePort; Name=$i.ServerName; Running=$running; Online=($null -ne $info); Info=$info; Bots=(Get-BmBotCount $dir $i) }
    })
    $services = @(foreach ($s in (Get-BpServices $dir $script:Cfg)) { @{ Key=$s.Key; Label=$s.Label; State=(Get-BpState $s (Get-BpRoot $dir)); Enabled=$s.Enabled; Port=$s.Port } })
    $plan=$null; if ($script:LaunchPlan) { $plan=@{ Done=$script:LaunchPlan.Done; Failed=$script:LaunchPlan.Failed; Log=@($script:LaunchPlan.Log) } }
    $script:StatusCache=@{ Servers=$rows; Services=$services; Launch=$plan; Log=@($script:Audit); Time=(Get-Date).ToString('o') }
    $script:StatusAt=Get-Date; return $script:StatusCache
}
function Valid-Map($map,$mode) { if (-not (Get-BmMap $map) -or (Get-BmMapModes $map) -notcontains $mode) { throw 'Invalid map / mode combination' } }
function Get-Learn($key) { if ($key -notmatch '^[A-Za-z0-9_]+$') { throw 'Invalid learned-map key' }; $p=Join-Path (Get-BmLearnDir $dir) ($key+'.learn'); $f=Read-CommandLearnFile $p; if (-not $f) { throw 'No learned data for this map' }; return $f }
function Api([string]$path, $b, $req, $query) {
    switch ($path) {
        '/api/meta' {
            return @{ Maps=@($script:BmMaps | ForEach-Object { @{ Code=$_.Code; Name=$_.Name; Modes=(Get-BmMapModes $_.Code) } }); Modes=$script:BmModeNames; Levels=$script:BmLevels; Settings=$script:BmSettings; Vars=$script:BmServerVars; UnlockModes=$script:BmUnlockModes; Local=$req.Local; LocalOnly=[bool]$LocalOnly; Urls=$urls; Ips=$ips; Elevated=(Test-BpElevated); ServerDir=$dir; LaunchServers=@($script:Prefs.LaunchServers); StartDns=[bool]$script:Prefs.StartDns; Version=$script:CommandVersion }
        }
        '/api/status' { return Get-Status }
        '/api/settings' {
            $i=$null; if ($query['id'] -ne 'all') { $i=Inst $query['id'] }
            return @{ Rows=@(Get-BmEffectiveSettings $dir $i); Revision=(Hash-Paths (Bot-Paths $i)) }
        }
        '/api/settings/save' {
            $i=$null; if ($b.id -ne 'all') { $i=Inst $b.id }
            $paths=Bot-Paths $i; Guard $b.revision $paths; $values=@{}; $remove=@()
            foreach ($p in $b.values.PSObject.Properties) { $s=$script:BmSettings | Where-Object { $_.Key -eq $p.Name } | Select-Object -First 1; if (-not $s) { throw 'Unknown bot setting' }; $v=Test-BmSettingValue $s ([string]$p.Value); if ($null -eq $v) { throw "Invalid value for $($s.Label)" }; $values[$s.Key]=$v }
            foreach ($k in @($b.remove)) { if ($k -notin $script:BmSettings.Key -or -not $i) { throw 'Invalid inheritance request' }; $remove += [string]$k }
            Backup $paths; Set-BmSettings $dir $i $values $remove
            if ($b.apply) { if ($i) { Send-BmSettingsReload $dir $i } else { foreach ($x in $Instances) { Send-BmSettingsReload $dir $x } } }
            return @{ Message=$(if ($b.apply) {'Saved; settings reload queued. Some values take effect at the next spawn or round.'} else {'Saved for the next server start.'}) }
        }
        '/api/instance' {
            $i=Inst $query['id']; $c=Conn $i
            try { $players=@(); $rotation=Read-BmMapListFile $i; $indices=$null; if ($c) { $players=Get-BmPlayers $c; $rotation=Get-BmMapList $c; $indices=Get-BmMapIndices $c }
                return @{ Players=$players; Rotation=$rotation; Indices=$indices; Revision=(Hash-Paths @((Get-BmMapListPath $i))); Setup=@(Get-BmServerSetup $i); SetupRevision=(Hash-Paths (Setup-Paths $i)) }
            } finally { Disconnect-BmRcon $c }
        }
        '/api/setup/save' {
            $i=Inst $b.id; $paths=Setup-Paths $i; Guard $b.revision $paths; $values=@{}
            foreach ($p in $b.values.PSObject.Properties) { $v=$script:BmServerVars | Where-Object { $_.Key -eq $p.Name } | Select-Object -First 1; if (-not $v) { throw 'Unknown rule' }; $t=Test-BmVarValue $v ([string]$p.Value); if ($null -eq $t) { throw "Invalid $($v.Label)" }; $values[$v.Key]=$t }
            Backup $paths; Set-BmServerSetup $i $values; $refused=@()
            if ($b.apply) { $c=Need-Conn $i; try { $refused=@(Send-BmServerSetup $c $values) } finally { Disconnect-BmRcon $c } }
            return @{ Message='Rules saved. Live rules sent if requested; others apply at the next start or round.'; Refused=$refused }
        }
        '/api/action' {
            $i=Inst $b.id; $a=[string]$b.action
            if ($a -eq 'start') { if (-not (Get-BmServerProcess $i -Fresh)) { Start-BmServer $i $script:Cfg.ServerWindow }; return @{ Message='Server start requested.' } }
            if ($a -eq 'stop') { [void](Stop-BmServer $i); return @{ Message='Server stopped.' } }
            if ($a -eq 'bots') { $n=0; if (-not [int]::TryParse([string]$b.count,[ref]$n) -or $n -lt 0 -or $n -gt 64) { throw 'Bot count must be 0 to 64' }; Set-BmBotCount $dir $i $n; return @{ Message='Bot count saved and queued.' } }
            $c=Need-Conn $i
            try {
                switch ($a) {
                    'next' { Check-Result (Invoke-BmSimple $c @('mapList.runNextRound')) }
                    'restart' { Check-Result (Invoke-BmSimple $c @('mapList.restartRound')) }
                    'uswin' { Check-Result (Invoke-BmSimple $c @('mapList.endRound','1')) }
                    'ruwin' { Check-Result (Invoke-BmSimple $c @('mapList.endRound','2')) }
                    'map' { Valid-Map $b.map $b.mode; Check-Result (Set-BmMap $c $b.map $b.mode -Now:([bool]$b.now)) }
                    'say' { if (-not $b.text -or $b.text.Length -gt 255 -or $b.text -match '[\x00-\x1f]') { throw 'Use a message of 1 to 255 characters' }; Check-Result (Invoke-BmSimple $c @('admin.say',[string]$b.text,'all')) }
                    'kick' { $p=@(Get-BmPlayers $c | Where-Object { $_.name -ceq $b.name }); if (-not $p.Count) { throw 'Player has left' }; Check-Result (Invoke-BmSimple $c @('admin.kickPlayer',[string]$b.name,'Kicked by the server admin')) }
                    default { throw 'Unknown server action' }
                }
            } finally { Disconnect-BmRcon $c }
            return @{ Message='Command accepted by the server.' }
        }
        '/api/rotation/save' {
            $i=Inst $b.id; $path=Get-BmMapListPath $i; Guard $b.revision @($path)
            $entries=@(foreach ($e in @($b.entries)) { Valid-Map $e.Map $e.Mode; if ([int]$e.Rounds -lt 1 -or [int]$e.Rounds -gt 100) { throw 'Rounds must be 1 to 100' }; [pscustomobject]@{ Map=[string]$e.Map; Mode=[string]$e.Mode; Rounds=[int]$e.Rounds } })
            if (-not $entries.Count) { throw 'Add at least one map' }; Backup @($path)
            # Apply through RCON first: a refused live update must not be reported as successful.
            if ($b.apply) { $c=Need-Conn $i; try { Check-Result (Set-BmRotation $c $entries) } finally { Disconnect-BmRcon $c } }
            Write-BmMapListFile $i $entries; return @{ Message='Rotation saved.' }
        }
        '/api/preset' { if ($query['kind'] -notin @('conquest','conquestsmall','rush','tdm','sqdm','domination')) { throw 'Unknown preset' }; return @{ Entries=(Get-BmPresetRotation $query['kind']) } }
        '/api/launch' {
            if ($script:LaunchPlan -and -not $script:LaunchPlan.Done) { throw 'A launch is already in progress' }
            $ids=@(); foreach ($id in @($b.servers)) { [void](Inst $id); $ids += [int]$id }; if (-not $ids.Count) { throw 'Choose a server to launch' }
            $script:LaunchPlan=New-BpLaunch $dir $Instances $ids ([bool]$b.dns) $script:Cfg
            return @{ Message='Launch sequence started. Progress is shown on Overview.' }
        }
        '/api/launch/cancel' { $script:LaunchPlan=$null; return @{ Message='Launch cancelled. Already started components keep running.' } }
        '/api/services' {
            $s=Get-BpServices $dir $script:Cfg | Where-Object { $_.Key -eq $b.key } | Select-Object -First 1; if (-not $s) { throw 'Unknown service' }
            if ($b.action -eq 'start') { Check-Result (Start-BpService $s (Get-BpRoot $dir)) } elseif ($b.action -eq 'stop') { Stop-BpService $s (Get-BpRoot $dir) } else { throw 'Unknown service action' }
            return @{ Message='Service command sent.' }
        }
        '/api/launcher/config' { if (-not $req.Local) { throw 'LOCAL: Edit launcher executables and IP targets on the host PC.' }; return @{ Config=$script:Cfg; Targets=@(Get-BpIpTargets $dir $script:Cfg); Revision=(Hash-Paths @($script:BpConfigPath)) } }
        '/api/launcher/config/save' {
            if (-not $req.Local) { throw 'LOCAL: Use the host PC for launcher executable settings.' }; Guard $b.revision @($script:BpConfigPath)
            Backup @($script:BpConfigPath); $temp=Join-Path $base 'Command-config.tmp.json'
            try { [IO.File]::WriteAllText($temp,($b.config | ConvertTo-Json -Depth 12)); $cfg=Read-BpConfig $dir $temp; Save-BpConfig $cfg; $script:Cfg=$cfg } finally { Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue }
            return @{ Message='Launcher settings saved.' }
        }
        '/api/ip/save' { if (-not $req.Local) { throw 'LOCAL: Use the host PC for backend IP changes.' }; if (@($b.keys | Where-Object { $_ -notin @('serverip','redirector','dns','client') }).Count) { throw 'Unknown IP target' }; return @{ Message=((Set-BpIp $dir $b.ip @($b.keys) $script:Cfg) -join '; ') } }
        '/api/desktop' {
            if (-not $req.Local) { throw 'LOCAL: Open the original launcher on the host PC.' }
            Start-Process powershell.exe -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-STA','-File',('"'+(Join-Path $base 'AltBotMod-ServerControl.ps1')+'"'),'-ServerDir',('"'+$dir+'"'))
            return @{ Message='Original desktop launcher opened on this PC.' }
        }
        '/api/join/options' {
            if (-not $req.Local) { throw 'LOCAL: Launch and join BF3 from Command on the game PC.' }
            $i=Inst $query['id']; $name=$i.ServerName; $c=Conn $i
            try { if ($c) { $info=Get-BmServerInfo $c; if ($info) { $name=$info.Name } } } finally { Disconnect-BmRcon $c }
            return Get-CommandJoinOptions $dir $i $name
        }
        '/api/join/launch' {
            if (-not $req.Local) { throw 'LOCAL: Launch and join BF3 from Command on the game PC.' }
            $i=Inst $b.id
            return Invoke-CommandJoin $dir $i $b
        }
        '/api/client' { if (-not $req.Local) { throw 'LOCAL: Start the game client from the host PC.' }; Start-Process -FilePath (Get-BpClient $dir); return @{ Message='BF Launcher started on this PC.' } }
        '/api/live' {
            $i=Inst $query['id']; if (-not $script:Feeds.ContainsKey($i.Number)) { $script:Feeds[$i.Number]=New-LmFeed }; $feed=$script:Feeds[$i.Number]
            Update-LmFeed $feed (Get-LmEventsPath $dir $i) | Out-Null; $live=Read-LmLive (Get-LmLivePath $dir $i)
            $level=$live.Level; $round=[long]$query['round']; $layer=[string]$query['layer']; if ($layer -notin @('kills','deaths','stuck','traffic')) { $layer='traffic' }
            $heat=@(); if ($level) { $h=Get-LmHeat $feed $level $layer $round; $heat=@(foreach ($e in $h.GetEnumerator()) { $xz=ConvertFrom-LmCellKey $e.Key; ,@($xz[0],$xz[1],$e.Value) }) }
            return @{ Live=$live; Now=(Get-LmUnixNow); Heat=$heat; Kills=@(Select-LmEvents $feed.Kills $level $round | Select-Object -Last 80); Stucks=@(Get-LmStuckSpots $feed $level $round | Select-Object -First 80); Scores=@(Get-LmScoreboard $feed $level $round); Rounds=@(Get-LmRounds $feed $level); Enabled=(Get-LmEnabled $dir $i) }
        }
        '/api/live/toggle' { $i=Inst $b.id; Set-LmEnabled $dir $i ([bool]$b.enabled); return @{ Message='Live map recording setting saved.' } }
        '/api/learning' { return Get-CommandLearning $dir }
        '/api/learning/file' { $f=Get-Learn $query['key']; $offset=0; [void][int]::TryParse([string]$query['offset'],[ref]$offset); $page=Get-CommandLearnPage $f $offset; $page.Revision=Hash-Paths @($f.Path); return $page }
        '/api/learning/delete' {
            $f=Get-Learn $b.key; Guard $b.revision @($f.Path); Backup @($f.Path)
            $entries=@($f.Entries | Where-Object { $b.lines -contains $_.Line }); $n=Remove-CommandLearnLines $f.Path @($entries | ForEach-Object { $_.Line })
            return @{ Message="$n learned entries removed." }
        }
        '/api/learning/export' {
            $tmp=Join-Path ([IO.Path]::GetTempPath()) ('command-'+[guid]::NewGuid().ToString('N')+'.zip')
            try { [void](Export-BmLearning $dir $tmp); return @{ Name='AltBotMod-Learning.zip'; Data=[Convert]::ToBase64String([IO.File]::ReadAllBytes($tmp)) } } finally { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue }
        }
        '/api/learning/import' {
            $raw=[Convert]::FromBase64String([string]$b.data); if ($raw.Length -gt 16MB) { throw 'Learning ZIP must be under 16 MB' }
            $tmp=Join-Path ([IO.Path]::GetTempPath()) ('command-'+[guid]::NewGuid().ToString('N')+'.zip')
            try {
                [IO.File]::WriteAllBytes($tmp,$raw); Add-Type -AssemblyName System.IO.Compression.FileSystem
                $z=[IO.Compression.ZipFile]::OpenRead($tmp); try { $total=0L; foreach ($e in $z.Entries) { $total += $e.Length; if ($e.FullName -match '(^[/\\]|(^|[/\\])\.\.([/\\]|$)|:)' -or $e.Length -gt 16MB -or $total -gt 64MB) { throw 'Invalid or oversized learning archive' } } } finally { $z.Dispose() }
                $r=Import-BmLearning $dir $tmp; return @{ Message="Imported: $($r[0]) new maps, $($r[1]) merged maps, $($r[2]) added entries." }
            } finally { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue }
        }
        default { throw 'Unknown endpoint' }
    }
}
Add-Type -Path (Join-Path $base 'Command-Http.cs')
$http=New-Object CommandHttp($Port,(-not $LocalOnly))
if ($sessionPath) { [IO.File]::WriteAllText($sessionPath,(@{ Url=$localUrl; Key=$token; ServerDir=$dir } | ConvertTo-Json -Compress)) }
Write-Host ''
Write-Host 'ALTBOTMOD / COMMAND' -ForegroundColor Cyan
Write-Host "Host: http://127.0.0.1:$Port/"
Write-Host 'The browser opens with a private pairing key. Use Connect phone to show its QR code.'
if ($LocalOnly) { Write-Host 'Local only: phone access is disabled.' } else { Write-Host "LAN addresses: $($ips -join ', ') (port $Port). Allow Windows Firewall on Private networks if prompted." }
Write-Host 'Keep this window open. Ctrl+C stops the web launcher; game servers keep running.'
$script:CommandQuit=$false; $tray=$null
if ($Desktop) { $tray=New-CommandTray $localUrl }
if (-not $NoBrowser) { if ($Desktop) { Open-CommandWindow $localUrl } else { Start-Process $localUrl } }
Note 'Command launcher ready.'
try {
    while (-not $script:CommandQuit) {
        if ($Desktop) { [Windows.Forms.Application]::DoEvents() }
        if ($script:LaunchPlan -and -not $script:LaunchPlan.Done) { $m=Step-BpLaunch $script:LaunchPlan ([Environment]::TickCount/1000.0); if ($m) { Note $m; $script:StatusAt=[datetime]::MinValue } }
        $req=$http.Next(); if (-not $req) { Start-Sleep -Milliseconds 60; continue }
        try {
            if ($hosts -notcontains $req.Host) { $req.Reply(403,'text/plain',[Text.Encoding]::UTF8.GetBytes('Invalid Host')); continue }
            if ($req.Origin -and $req.Origin -cne ('http://'+$req.Host)) { $req.Reply(403,'text/plain',[Text.Encoding]::UTF8.GetBytes('Invalid Origin')); continue }
            $uri=New-Object Uri('http://'+$req.Host+$req.Target); $path=$uri.AbsolutePath
            if ($path.StartsWith('/api/')) {
                if ($req.Authorization -cne ('Bearer '+$token)) { $req.Reply(401,'application/json',[Text.Encoding]::UTF8.GetBytes('{"Error":"Pair this browser using the host link or QR code."}')); continue }
                $post=@('/api/settings/save','/api/setup/save','/api/action','/api/rotation/save','/api/launch','/api/launch/cancel','/api/services','/api/launcher/config/save','/api/ip/save','/api/desktop','/api/client','/api/live/toggle','/api/learning/delete','/api/learning/export','/api/learning/import','/api/join/launch') -contains $path
                if (($post -and ($req.Method -ne 'POST' -or $req.ContentType -notlike 'application/json*')) -or (-not $post -and $req.Method -ne 'GET')) { throw 'Invalid method or content type' }
                $b=$null; if ($post) { $b=[Text.Encoding]::UTF8.GetString($req.Body) | ConvertFrom-Json }
                $query=@{}; foreach ($part in $uri.Query.TrimStart('?').Split('&')) { $kv=$part.Split('=',2); if ($kv.Length -eq 2) { $query[[Uri]::UnescapeDataString($kv[0])]=[Uri]::UnescapeDataString($kv[1]) } }
                if ($path -eq '/api/live/picture') {
                    $i=Inst $query['id']; $live=Read-LmLive (Get-LmLivePath $dir $i)
                    $picture=$null; if ($live.Picture) { $picture=Get-LmPicturePath $dir $live.Picture.File }
                    if (-not $picture -or -not (Test-Path -LiteralPath $picture)) { $req.Reply(404,'text/plain',[Text.Encoding]::UTF8.GetBytes('No terrain picture')); continue }
                    Add-Type -AssemblyName System.Drawing
                    $bitmap=[Drawing.Image]::FromFile($picture); $stream=New-Object IO.MemoryStream
                    try { $bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png); $req.Reply(200,'image/png',$stream.ToArray()) } finally { $bitmap.Dispose(); $stream.Dispose() }
                    continue
                }
                $data=Api $path $b $req $query
                if ($post) { $script:StatusAt=[datetime]::MinValue; Note ([string]$data.Message) }
                $json=ConvertTo-Json -InputObject $data -Depth 28 -Compress
                $req.Reply(200,'application/json; charset=utf-8',[Text.Encoding]::UTF8.GetBytes($json))
            } else {
                $file=@{ '/'='index.html'; '/index.html'='index.html'; '/command.css'='command.css'; '/command.js'='command.js'; '/qr.js'='qr.js'; '/command-art.png'='command-art.png'; '/favicon.svg'='favicon.svg' }[$path]
                if (-not $file -and $path -match '^/art/(arctic|urban|air|signal|frontier)\.png$') { $file='art/'+$Matches[1]+'.png' }
                if (-not $file -and $path -match '^/maps/([A-Za-z0-9_]+)\.jpg$' -and (Get-BmMap $Matches[1])) { $file='maps/'+$Matches[1]+'.jpg' }
                if ($path -eq '/command.ico') { $file='command.ico' }
                if (-not $file -or $req.Method -ne 'GET') { $req.Reply(404,'text/plain',[Text.Encoding]::UTF8.GetBytes('Not found')); continue }
                $type=@{ '.html'='text/html; charset=utf-8'; '.css'='text/css; charset=utf-8'; '.js'='application/javascript; charset=utf-8'; '.png'='image/png'; '.svg'='image/svg+xml'; '.jpg'='image/jpeg'; '.ico'='image/x-icon' }[[IO.Path]::GetExtension($file)]
                $req.Reply(200,$type,[IO.File]::ReadAllBytes((Join-Path (Join-Path $base 'Command-Web') $file)))
            }
        } catch {
            $msg=$_.Exception.Message; $code=400; if ($msg.StartsWith('CONFLICT:')) { $code=409 }; if ($msg.StartsWith('LOCAL:')) { $code=403 }
            Note ('Failed: '+$msg); $j=@{ Error=$msg } | ConvertTo-Json -Compress; $req.Reply($code,'application/json; charset=utf-8',[Text.Encoding]::UTF8.GetBytes($j))
        }
    }
} finally { $http.Dispose(); if ($sessionPath -and (Test-Path -LiteralPath $sessionPath)) { try { $saved=[IO.File]::ReadAllText($sessionPath) | ConvertFrom-Json; if ($saved.Key -eq $token) { Remove-Item -LiteralPath $sessionPath -Force } } catch { } }; if ($tray) { $tray.Visible=$false; $tray.ContextMenuStrip.Dispose(); $tray.Dispose() } }
