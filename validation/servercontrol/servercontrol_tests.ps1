# Tests the real ServerControl-Core.ps1 against mock BF3 RCON servers (mock_rcon.py) and a fixture server folder.
# The WinForms window itself is only parse-checked here (no Windows desktop in the test environment).
param([string]$Core, [string]$Gui, [string]$Python = 'python3')
$ErrorActionPreference = 'Stop'
. $Core
. (Join-Path (Split-Path -Parent $Core) 'BFPortable-Core.ps1')
$fail = 0
function Assert($ok, [string]$what) { if (-not $ok) { Write-Host "FAIL: $what"; $script:fail++ } }

# the GUI script parses
$t = $null; $e = $null
[void][Management.Automation.Language.Parser]::ParseFile($Gui, [ref]$t, [ref]$e)
Assert ($e.Count -eq 0) "Server Control window script parses ($($e | Out-String))"
foreach ($f in @($Core, $Gui, (Join-Path (Split-Path -Parent $Core) 'BFPortable-Core.ps1'), (Join-Path (Split-Path -Parent $Core) 'ServerControl-Theme.ps1'))) { $bytes = [IO.File]::ReadAllBytes($f); Assert (-not ($bytes | Where-Object { $_ -gt 127 })) "$f is plain ASCII (Windows PowerShell 5.1 reads it without a BOM)" }

# map / mode table: the 215 map-mode combinations AltBotMod supports (V15: + Squad Rush 25, Gun Master 8, Scavenger 4, Tank / Air Superiority 4 + 4, CTF 4)
$total = 0; foreach ($m in $script:BmMaps) { $total += (Get-BmMapModes $m.Code).Count }
Assert ($script:BmMaps.Count -eq 29 -and $total -eq 215) "29 maps / 215 map-mode combinations (got $($script:BmMaps.Count) / $total)"
Assert ((Get-BmMapModes 'XP2_Office') -join ',' -eq 'Domination0,TeamDeathMatchC0,SquadDeathMatch0,GunMaster0') 'close-quarters map modes'
Assert ((Get-BmMapModes 'XP4_FD') -contains 'Scavenger0' -and (Get-BmMapModes 'XP3_Shield') -contains 'TankSuperiority0' -and (Get-BmMapModes 'XP5_003') -contains 'CaptureTheFlag0' -and (Get-BmModeName 'SquadRush0') -eq 'Squad Rush') 'V15: the other modes on their levels'
Assert ((Get-BmMapModes 'XP1_001') -contains 'ConquestAssaultSmall1' -and -not ((Get-BmMapModes 'XP1_001') -contains 'ConquestLarge0')) 'Karkand: Conquest Assault modes only'
Assert ((Get-BmMapModes 'XP5_002') -contains 'TeamDeathMatch0' -and (Get-BmMapModes 'XP5_002') -contains 'RushLarge0') 'Nebandan has TDM and Rush'

# fixture server folder with four instances
$root = Join-Path ([IO.Path]::GetTempPath()) ("sc-test-" + [guid]::NewGuid().ToString('N'))
$srv = Join-Path $root 'Battlefield_3_Server'; $bm = Join-Path $root 'BotMod'
New-Item -ItemType Directory -Force -Path $srv, $bm | Out-Null
[IO.File]::WriteAllBytes((Join-Path $srv 'bf3_server_Final.exe'), [byte[]](1, 2, 3))
$ports = @(47990, 47991, 47992, 47993)
for ($k = 1; $k -le 4; $k++) {
    $name = $(if ($k -eq 1) { '_StartServer.bat' } else { "_StartServer$k.bat" })
    $lines = @("set INSTANCEDIRECTORY=%~dp0Instance$(if ($k -gt 1) { $k })", 'set SERVERNAME=Battlefield 3 Server', "set GAMEPORT=$(25299 + $k)",
        "set REMOTEADMINPORT=127.0.0.1:$($ports[$k - 1])", 'set ADMINPASSWORD=TestPass1', 'set PLAYERS=64', 'start bf3_server_Final.exe ...', 'exit')
    [IO.File]::WriteAllLines((Join-Path $srv $name), [string[]]$lines)
}
Assert ((Resolve-BmServerDir $root) -eq (Resolve-Path $srv).Path) 'BF-Portable folder resolves to Battlefield_3_Server'
$inst = Get-BmInstances $srv
Assert ($inst.Count -eq 4) "four instances found ($($inst.Count))"
Assert ($inst[0].GamePort -eq 25300 -and $inst[0].RconPort -eq 47990 -and $inst[3].GamePort -eq 25303 -and $inst[1].Password -eq 'TestPass1') 'ports and password read from the .bat files'
Assert ($inst[1].InstanceDir -like '*Battlefield_3_Server*Instance2') "instance folder expanded ($($inst[1].InstanceDir))"

# mock RCON servers (only the first three run: the fourth is "stopped")
$valid = @{}; foreach ($m in $script:BmMaps) { $valid[$m.Code] = (Get-BmMapModes $m.Code) }
$validFile = Join-Path $root 'valid.json'; ($valid | ConvertTo-Json -Depth 4) | Set-Content $validFile
$mock = Join-Path $PSScriptRoot 'mock_rcon.py'
$procs = @()
for ($k = 0; $k -lt 3; $k++) {
    $procs += Start-Process -FilePath $Python -ArgumentList @($mock, $ports[$k], 'TestPass1', (Join-Path $root "state$k.json"), $validFile) -PassThru -NoNewWindow
}
Start-Sleep -Milliseconds 900
function State($k) { return Get-Content -Raw (Join-Path $root "state$k.json") | ConvertFrom-Json }
try {
    $c = Connect-BmRcon $inst[0] 1500
    Assert ($null -ne $c) 'logs in over RCON'
    $bad = $inst[1].PSObject.Copy(); $bad.Password = 'wrong'
    Assert ($null -eq (Connect-BmRcon $bad 1500)) 'a wrong password is refused'
    Assert ($null -eq (Connect-BmRcon $inst[3] 500)) 'a stopped server gives no connection (no hang)'

    $info = Get-BmServerInfo $c
    Assert ($info.Map -eq 'XP5_001' -and $info.Mode -eq 'ConquestLarge0' -and $info.Players -eq 3 -and $info.MaxPlayers -eq 64) 'serverInfo: map, mode, players'
    Assert ($info.Scores.Count -eq 2 -and $info.Scores[0] -eq 187.5 -and $info.RoundTime -eq 321) "serverInfo: team scores and round time ($($info.Scores -join '/') $($info.RoundTime))"
    $list = Get-BmMapList $c
    Assert ($list.Count -eq 2 -and $list[1].Map -eq 'XP5_002') 'map list read'

    # play now: TDM on Ziba Tower is refused (close-quarters map: TDM CQ only); TDM CQ works
    $err = Set-BmMap $c 'XP2_Skybar' 'TeamDeathMatch0' -Now
    Assert ($err -like '*InvalidGameModeOnMap*') "an impossible map/mode is reported, not sent as a map change ($err)"
    $err = Set-BmMap $c 'XP2_Skybar' 'TeamDeathMatchC0' -Now
    $s = State 0
    Assert (-not $err -and $s.maps.Count -eq 3 -and $s.current -eq 2 -and $s.maps[2][0] -eq 'XP2_Skybar') "play now adds it, makes it next and starts it ($err)"
    # play next: an entry already in the rotation is reused, not added twice
    $err = Set-BmMap $c 'XP5_002' 'ConquestLarge0'
    $s = State 0
    Assert (-not $err -and $s.maps.Count -eq 3 -and $s.next -eq 1 -and $s.current -eq 2) 'play next reuses the rotation entry and leaves the current round running'
    # rotation reorder + save
    $list = Get-BmMapList $c
    $e = New-Object System.Collections.ArrayList; foreach ($r in $list) { [void]$e.Add($r) }
    $tmp = $e[0]; $e[0] = $e[2]; $e[2] = $tmp
    $err = Set-BmRotation $c $e
    $s = State 0
    Assert (-not $err -and $s.maps[0][0] -eq 'XP2_Skybar' -and $s.maps[2][0] -eq 'XP5_001' -and $s.saved.Count -eq 3) 'rotation reordered and saved to MapList.txt'
    Assert (-not (Invoke-BmSimple $c @('mapList.runNextRound')) -and -not (Invoke-BmSimple $c @('mapList.restartRound')) -and -not (Invoke-BmSimple $c @('mapList.endRound', '2'))) 'skip / restart / end round'
    # players, vars
    $pl = Get-BmPlayers $c
    Assert ($pl.Count -eq 3 -and $pl[0].name -eq 'Player1' -and $pl[2].teamId -eq '2' -and $pl[1].kills -eq '2') 'player list parsed (names, teams, kills)'
    Assert ((Get-BmVar $c 'gameModeCounter') -eq '100' -and (Set-BmVar $c 'gameModeCounter' '250') -and (Get-BmVar $c 'gameModeCounter') -eq '250') 'ticket % read and set'
    Assert (-not (Invoke-BmSimple $c @('admin.say', 'hello', 'all'))) 'chat message'
    # a second instance is independent
    $c2 = Connect-BmRcon $inst[1] 1500
    Assert ($null -ne $c2 -and -not (Set-BmMap $c2 'XP3_Desert' 'TeamDeathMatch0' -Now)) 'second server changed on its own port'
    Assert ((State 1).maps[(State 1).current][0] -eq 'XP3_Desert' -and (State 0).maps.Count -eq 3) 'the change reached only that server'
    # a dropped connection is detected
    Disconnect-BmRcon $c
    Assert ($null -eq (Invoke-BmRcon $c @('serverInfo'))) 'a closed connection returns nothing (the window then reconnects)'

    # AltBotMod files
    Set-BmBotCount $srv $inst[1] 24
    Assert (([IO.File]::ReadAllText((Join-Path $bm 'botcount_25301.txt'))).Trim() -eq '24' -and (Get-BmBotCount $srv $inst[1]) -eq 24) 'bot count kept in BotMod\botcount_<port>.txt'
    Assert ((Get-Content (Join-Path $srv 'BotMod_25301.cmd')) -contains 'botcount 24') 'bot count sent as a console command to that instance only'
    Assert (-not (Test-Path (Join-Path $srv 'BotMod_25300.cmd'))) 'other instances untouched'
    Set-BmInstanceSetting $srv $inst[2] 'Difficulty' 'veteran'
    Set-BmInstanceSetting $srv $inst[2] 'Difficulty' 'elite'
    $ini = Get-Content (Join-Path $bm 'enhanced_25302.ini')
    Assert ((@($ini | Where-Object { $_ -match '^Difficulty=' })).Count -eq 1 -and (Get-BmInstanceSetting $srv $inst[2] 'Difficulty') -eq 'elite') 'difficulty written once into enhanced_<port>.ini'
    # ---- AltBotMod V12: each server's own rules (Startup.txt + .bat) ----
    $sdir = Join-Path $srv 'Instance\Admin'; New-Item -ItemType Directory -Force -Path $sdir | Out-Null
    [IO.File]::WriteAllLines((Join-Path $sdir 'Startup.txt'), [string[]]@('vars.ranked true', 'admin.password "secret1"', 'vars.serverName "Battlefield 3 Server 1"',
        'vars.serverMessage "Welcome to "Battlefield 3 Server 1"', 'vars.friendlyFire false', 'vars.hud true', 'vars.maxPlayers 64', 'vars.unlockMode "stats"'))
    $rows = Get-BmServerSetup $inst[0]; $by = @{}; foreach ($r in $rows) { $by[$r.Var.Key] = $r.Value }
    Assert ($by['serverName'] -eq 'Battlefield 3 Server 1' -and $by['friendlyFire'] -eq 'false' -and $by['maxPlayers'] -eq '64' -and $by['unlockMode'] -eq 'stats') 'server rules read from Startup.txt'
    Assert ($by['serverMessage'] -eq 'Welcome to Battlefield 3 Server 1') "BF-Portable's broken quote in the welcome message is read cleanly ($($by['serverMessage']))"
    Assert ($null -eq $by['killCam']) 'a variable not in the file has no value (the server default)'
    $V = @{}; foreach ($x in $script:BmServerVars) { $V[$x.Key] = $x }
    Assert ((Test-BmVarValue $V['maxPlayers'] '70') -eq $null -and (Test-BmVarValue $V['maxPlayers'] '32') -eq $null -and (Test-BmVarValue $V['maxPlayers'] '64') -eq '64' -and (Test-BmVarValue $V['friendlyFire'] 'on') -eq 'true') 'values checked'
    Assert ((Test-BmVarValue $V['serverName'] 'My "best" server') -eq "My 'best' server" -and (Test-BmVarValue $V['serverName'] '') -eq $null -and (Test-BmVarValue $V['unlockMode'] 'ALL') -eq 'all') 'names: no quotes (they would break the file), not empty; unlock modes'
    Assert (@($script:BmServerVars | Where-Object { $_.Hardcore }).Count -ge 15 -and $V['soldierHealth'].Hardcore -eq '60' -and $V['hud'].Hardcore -eq 'false' -and $V['hud'].Normal -eq 'true') 'normal / hardcore presets'
    Set-BmServerSetup $inst[0] @{ serverName = 'Player1 Riverside 100%'; maxPlayers = '64'; friendlyFire = 'true'; killCam = 'false'; serverMessage = 'Hi all' }
    $st = [IO.File]::ReadAllLines((Join-Path $sdir 'Startup.txt'))
    Assert (($st -contains 'admin.password "secret1"') -and ($st -contains 'vars.ranked true') -and ($st -contains 'vars.friendlyFire true') -and ($st -contains 'vars.killCam false') -and ($st -contains 'vars.serverName "Player1 Riverside 100%"') -and ($st -contains 'vars.serverMessage "Hi all"') -and ($st -contains 'vars.maxPlayers 64')) "Startup.txt written in place, password and other lines kept ($($st -join ' | '))"
    $bt = [IO.File]::ReadAllLines($inst[0].Bat)
    Assert (($bt -contains 'set SERVERNAME=Player1 Riverside 100') -and ($bt -contains 'set PLAYERS=64') -and ($bt -contains 'set GAMEPORT=25300')) "the .bat follows: name (no % or quotes - cmd would eat them) and player limit ($($bt -join ' | '))"
    Assert ((Get-BmInstances $srv)[0].RconPort -eq 47990) 'the .bat still reads the same after the edit'
    $cS = Connect-BmRcon $inst[1] 1500
    $ref = @(Send-BmServerSetup $cS @{ friendlyFire = 'true'; hud = 'false'; unlockMode = 'all'; gameModeCounter = '200' })
    Assert ($ref.Count -eq 1 -and $ref[0] -like 'HUD*CommandIsReadOnly*' -and (State 1).vars.friendlyFire -eq 'true' -and (State 1).vars.gameModeCounter -eq '200' -and -not (State 1).vars.PSObject.Properties['unlockMode']) "apply now: live values sent, refusals reported, start-only ones not sent ($($ref -join '; '))"
    Disconnect-BmRcon $cS

    # ---- AltBotMod V12: BF-Portable services and the one-click start (processes / ports mocked) ----
    $bp = Split-Path -Parent $srv
    foreach ($d in @('Redirector', 'xampp\mysql\bin', 'xampp\apache\bin', 'BF3-EMU\conf', 'DNS')) { New-Item -ItemType Directory -Force -Path (Join-Path $bp $d) | Out-Null }
    foreach ($fx in @('Redirector\gosredirector.ea.com.exe', 'xampp\mysql\bin\mysqld.exe', 'xampp\apache\bin\httpd.exe', 'BF3-EMU\BlazeServer3.exe', 'DNS\DnsServer.exe', 'xampp\xampp-control.exe')) { [IO.File]::WriteAllBytes((Join-Path $bp $fx), [byte[]](1)) }
    [IO.File]::WriteAllText((Join-Path $bp 'Redirector\ServerIP.txt'), "192.168.1.100`r`n")
    [IO.File]::WriteAllLines((Join-Path $bp 'BF3-EMU\conf\conf.txt'), [string[]]@('LogLevel=1', 'BF3_ServerPort=42199'))
    $svcs = @(Get-BpServices $srv)
    Assert (($svcs | ForEach-Object { $_.Key }) -join ',' -eq 'dns,redirector,xampp,mysql,apache,blaze') 'parts in start order'
    Assert ($svcs[5].Port -eq 42199 -and $svcs[3].Port -eq 3306 -and $svcs[4].Port -eq 80 -and $svcs[1].Args -eq '/console' -and $svcs[3].WorkDir -like '*xampp') 'emulator port from its conf.txt; database / web ports; working folders'
    Assert (-not $svcs[2].Enabled -and $svcs[3].Enabled -and $svcs[5].Required -and $svcs[3].Required -and -not $svcs[4].Required -and $svcs[0].Admin -and $svcs[1].Admin -and $svcs[0].Input -eq "`n192.168.1.100") 'defaults: XAMPP panel off (database / web server direct), database and emulator required, DNS / redirector as administrator, the DNS answers (IP from ServerIP.txt)'
    $script:fakeRun = @{}; $script:fakePorts = @{}; $script:started = New-Object System.Collections.ArrayList; $script:srvStarted = New-Object System.Collections.ArrayList
    $script:BpProcesses = { param($name, $root) if ($script:fakeRun[$name]) { return @([pscustomobject]@{ Id = 1; Path = $root + '\x.exe' }) }; return @() }
    $script:BpPortOpen = { param($port) return [bool]$script:fakePorts[[int]$port] }
    $script:BpStartProcess = { param($svc) [void]$script:started.Add($svc.Key); $script:fakeRun[$svc.Proc] = $true }
    $script:BpServerProcess = { param($i) if ($script:srvStarted -contains $i.Number) { return 1 }; return $null }
    $script:BpStartServer = { param($i) [void]$script:srvStarted.Add($i.Number) }
    $script:fakeRun['httpd'] = $true; $script:fakePorts[80] = $true   # the web server is already running
    $l = New-BpLaunch $srv $inst @(1, 3) $false
    $t = 0.0; $guard = 0
    while (-not $l.Done -and $guard -lt 400) {
        $guard++; $t += 0.5
        if ($t -ge 3 -and $script:fakeRun['mysqld']) { $script:fakePorts[3306] = $true }     # the database answers after a while
        if ($t -ge 6 -and $script:fakeRun['BlazeServer3']) { $script:fakePorts[42199] = $true }
        [void](Step-BpLaunch $l $t)
    }
    Assert ($l.Done -and -not $l.Failed) "one click finished ($($l.Log -join ' / '))"
    Assert (($script:started -join ',') -eq 'redirector,mysql,blaze') "started what was not running, in order, skipped the running web server and the DNS ($($script:started -join ','))"
    Assert (($script:srvStarted -join ',') -eq '1,3') 'then only the ticked game servers'
    $iDb = [array]::IndexOf(@($l.Log), 'Database (MySQL) is up'); $iBl = [array]::IndexOf(@($l.Log), 'Starting BF3 Blaze emulator...')
    Assert ($iDb -ge 0 -and $iBl -gt $iDb) 'the emulator starts only once the database answers'
    # the database never comes up: stop before the emulator and the servers
    $script:fakeRun = @{}; $script:fakePorts = @{}; $script:started.Clear(); $script:srvStarted.Clear()
    $cfgD = Read-BpConfig $srv (Join-Path $root 'none.json'); $cfgD.DnsDomain = 'bf.lan'
    $l = New-BpLaunch $srv $inst @(1, 2, 3, 4) $true $cfgD; $t = 0.0
    while (-not $l.Done -and $t -lt 200) { $t += 0.5; [void](Step-BpLaunch $l $t) }
    Assert ($l.Failed -and ($script:started -join ',') -eq 'dns,redirector,mysql' -and $script:srvStarted.Count -eq 0) "a database that never answers stops the start before the emulator and servers ($($script:started -join ','))"
    Remove-Item -LiteralPath (Join-Path $bp 'BF3-EMU\BlazeServer3.exe')
    $script:fakeRun = @{ mysqld = $true; httpd = $true; 'gosredirector.ea.com' = $true }; $script:fakePorts = @{ 3306 = $true; 80 = $true }
    $l = New-BpLaunch $srv $inst @(1) $false; $t = 0.0
    while (-not $l.Done -and $t -lt 50) { $t += 0.5; [void](Step-BpLaunch $l $t) }
    Assert ($l.Failed -and ($l.Log -join ' ') -like '*BF3 Blaze emulator: not found*') 'a missing emulator is reported, not started blindly'
    # ---- AltBotMod V14: launcher settings (Launcher.json), how each part is started, map lists while a server is off ----
    $cfgPath = Join-Path $root 'Launcher.json'
    $c = Read-BpConfig $srv $cfgPath
    Assert ($c.AsAdmin -and $c.ServerGapSeconds -eq 4 -and $c.DnsIp -eq '192.168.1.100' -and -not (Test-Path -LiteralPath $cfgPath)) 'no Launcher.json: the defaults (as administrator, 4 s between servers, the IP from ServerIP.txt)'
    $absBlaze = Join-Path $root 'Other\BlazeServer3.exe'; ($c.Parts | Where-Object Key -eq 'blaze').Exe = $absBlaze; ($c.Parts | Where-Object Key -eq 'redirector').Args = '/console /x'
    ($c.Parts | Where-Object Key -eq 'apache').Enabled = $false; ($c.Parts | Where-Object Key -eq 'mysql').Port = $null; $c.DnsDomain = 'bf.lan'; $c.ServerGapSeconds = 9; $c.ServerWindow = 'Normal'
    Save-BpConfig $c $cfgPath
    $j = [IO.File]::ReadAllText($cfgPath) | ConvertFrom-Json; $j.Parts = @($j.Parts | Where-Object { $_.Key -ne 'xampp' }); $j | Add-Member -NotePropertyName Junk -NotePropertyValue 1
    $j.Parts[0] | Add-Member -NotePropertyName Wait -NotePropertyValue 'abc' -Force
    [IO.File]::WriteAllText($cfgPath, ($j | ConvertTo-Json -Depth 5))
    $c2 = Read-BpConfig $srv $cfgPath; $s2 = @(Get-BpServices $srv $c2)
    Assert ($c2.DnsDomain -eq 'bf.lan' -and $c2.ServerGapSeconds -eq 9 -and $c2.ServerWindow -eq 'Normal' -and ($s2 | Where-Object Key -eq 'blaze').Exe -eq $absBlaze -and ($s2 | Where-Object Key -eq 'redirector').Args -eq '/console /x' -and -not ($s2 | Where-Object Key -eq 'apache').Enabled -and $null -eq ($s2 | Where-Object Key -eq 'mysql').Port) 'saved settings come back (absolute paths kept, the rest under the BF-Portable folder)'
    Assert (($s2 | Where-Object Key -eq 'xampp') -and -not ($s2 | Where-Object Key -eq 'xampp').Enabled -and ($s2 | Where-Object Key -eq 'dns').Wait -eq 30 -and ($s2 | Where-Object Key -eq 'dns').Input -eq "bf.lan`n192.168.1.100") 'a part missing from the file keeps its default; a bad value falls back; {domain} / {ip} filled in'
    [IO.File]::WriteAllText($cfgPath, '{ not json'); Assert ((Read-BpConfig $srv $cfgPath).Parts.Count -eq 6) 'a broken Launcher.json: the defaults'
    # start plans (the real start: V13 handed Start-Process an empty argument list - DNS, web server and emulator never started)
    $dnsS = $s2 | Where-Object Key -eq 'dns'; $blS = $s2 | Where-Object Key -eq 'blaze'; $apS = $svcs | Where-Object Key -eq 'apache'
    $pl = New-BpStartPlan $apS $false; Assert ($pl.Arguments -eq '' -and $pl.Shell -and -not $pl.Verb -and $pl.Window -eq 'Hidden') 'no arguments: an empty argument string, not an empty list'
    $pl = New-BpStartPlan $dnsS $true; Assert (-not $pl.Shell -and ($pl.Input -join '|') -eq 'bf.lan|192.168.1.100' -and -not $pl.Verb) 'DNS as administrator: its questions answered through its input'
    $pl = New-BpStartPlan $dnsS $false; Assert ($pl.Verb -eq 'runas' -and $pl.Shell -and $pl.Window -eq 'Normal' -and $pl.Note -like '*bf.lan*192.168.1.100*') 'DNS without administrator rights: Windows asks, and its window opens with what to type'
    $pl = New-BpStartPlan ($s2 | Where-Object Key -eq 'redirector') $false; Assert ($pl.Verb -eq 'runas' -and $pl.Arguments -eq '/console /x') 'redirector: administrator when this window is not'
    $pl = New-BpStartPlan $blS $false; Assert (-not $pl.Verb) 'emulator: no administrator rights needed'
    # a missing optional part, a server that does not stay up, DNS without a domain, a start that throws, the server window
    $script:fakeRun = @{ mysqld = $true; httpd = $true; 'gosredirector.ea.com' = $true }; $script:fakePorts = @{ 3306 = $true; 80 = $true; 42199 = $true }; $script:started.Clear(); $script:srvStarted.Clear()
    [IO.File]::WriteAllBytes((Join-Path $bp 'BF3-EMU\BlazeServer3.exe'), [byte[]](1)); $script:fakeRun['BlazeServer3'] = $true
    $script:winSeen = $null; $script:BpStartServer = { param($i, $w) $script:winSeen = $w; if ($i.Number -ne 2) { [void]$script:srvStarted.Add($i.Number) } }
    $cfgN = Read-BpConfig $srv (Join-Path $root 'none.json'); $cfgN.ServerWindow = 'Normal'; $cfgN.ServerGapSeconds = 2
    $l = New-BpLaunch $srv $inst @(1, 2) $true $cfgN; $t = 0.0
    while (-not $l.Done -and $t -lt 100) { $t += 0.5; [void](Step-BpLaunch $l $t) }
    $lg = $l.Log -join ' / '
    Assert ($lg -like '*LAN DNS server: skipped - set the LAN domain name*' -and $lg -like '*Server 2 is not running*' -and $l.Failed -and ($script:srvStarted -join ',') -eq '1' -and $script:winSeen -eq 'Normal') "DNS without a domain is skipped with the reason; a server whose .bat closed is reported; the server window setting is passed ($lg)"
    $script:fakeRun = @{ httpd = $true }; $script:fakePorts = @{ 80 = $true }; $script:BpStartProcess = { param($svc) if ($svc.Key -eq 'redirector') { throw 'The requested operation requires elevation' }; $script:fakeRun[$svc.Proc] = $true; $script:fakePorts[3306] = $true; $script:fakePorts[42199] = $true }
    $l = New-BpLaunch $srv $inst @() $false $cfgN; $t = 0.0
    while (-not $l.Done -and $t -lt 100) { $t += 0.5; [void](Step-BpLaunch $l $t) }
    $lg = $l.Log -join ' / '
    Assert ($lg -like '*Redirector: could not start - The requested operation requires elevation*' -and $lg -like '*Database (MySQL) is up*' -and $lg -like '*BF3 Blaze emulator is up*') "a part that cannot start is reported and the rest carries on ($lg)"
    $script:BpStartProcess = { param($svc) throw 'boom' }; $script:fakeRun = @{}; $script:fakePorts = @{}
    $l = New-BpLaunch $srv $inst @(1) $false $cfgN; $t = 0.0
    while (-not $l.Done -and $t -lt 100) { $t += 0.5; [void](Step-BpLaunch $l $t) }
    Assert ($l.Failed -and ($l.Log -join ' ') -like '*Database (MySQL): could not start - boom*Stopped: BF3 cannot run without it.*' -and -not (($l.Log -join ' ') -like '*Starting Server 1*')) 'a required part that cannot start stops the one click'
    # map list file (the server off)
    $mi = $inst[2]; $mlp = Get-BmMapListPath $mi
    Assert ($mlp -like '*Instance3*Admin*MapList.txt') "map list file path ($mlp)"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $mlp) | Out-Null
    [IO.File]::WriteAllLines($mlp, [string[]]@('MP_001 ConquestLarge0 1', '', '# comment', 'XP1_001 ConquestAssaultLarge0 2', 'broken'))
    $ml = Read-BmMapListFile $mi
    Assert ($ml.Count -eq 2 -and $ml[1].Map -eq 'XP1_001' -and $ml[1].Rounds -eq 2 -and $ml[0].Mode -eq 'ConquestLarge0') 'map list file read (blank / comment / broken lines skipped)'
    $cq = Get-BmPresetRotation 'conquest'
    Write-BmMapListFile $mi $cq
    $back = Read-BmMapListFile $mi
    Assert ($back.Count -eq 25 -and $cq.Count -eq 25 -and ($back | Where-Object Map -eq 'XP1_004').Mode -eq 'ConquestAssaultLarge0' -and -not ($back | Where-Object Map -eq 'XP2_Office') -and (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $mlp) 'MapList.before-edit.txt'))) 'every Conquest map written (assault maps as Conquest Assault, close-quarters left out; the old file kept)'
    Assert ((Get-BmPresetRotation 'rush').Count -eq 25 -and (Get-BmPresetRotation 'domination').Count -eq 4 -and (Get-BmPresetRotation 'tdm').Count -eq 29 -and ((Get-BmPresetRotation 'tdm') | Where-Object Map -eq 'XP2_Office').Mode -eq 'TeamDeathMatchC0') 'presets: Rush, close quarters, TDM (close-quarters maps as TDM CQ)'
    $err = $null; try { Write-BmMapListFile $mi @() } catch { $err = $_.Exception.Message }; Assert ($err -like '*at least one*' -and (Read-BmMapListFile $mi).Count -eq 25) 'an empty map list is refused (the file is left as it was)'
    $script:BpStartProcess = { param($svc) [void]$script:started.Add($svc.Key); $script:fakeRun[$svc.Proc] = $true }
    Assert ((Get-BpBattlelogUrl '') -eq 'http://localhost/' -and (Get-BpBattlelogUrl 'http://192.168.1.100/') -eq 'http://192.168.1.100/' -and (Get-BpBattlelogUrl 'javascript:x') -eq 'http://localhost/') 'Battlelog address: localhost unless set'

    $threw = $false; try { Send-BmBotCommand $srv $inst[0] 'reload' } catch { $threw = $true }
    Assert $threw 'hot reload can never be sent from Server Control'

    # AltBotMod V11: the settings window's data - every setting from the all-servers file, a server's own file or the default
    [IO.File]::WriteAllLines((Join-Path $bm 'enhanced.ini'), [string[]]@('# tuning', '[CombatAI]', 'BotDamage=1.5   # stronger', 'Aggression=2', 'SniperEngageMeters=5000', '[Vehicles]', 'BotDamage=1.25'))
    $rows = Get-BmEffectiveSettings $srv $inst[0]; $by = @{}; foreach ($r in $rows) { $by[$r.Setting.Key] = $r }
    Assert ($rows.Count -eq $script:BmSettings.Count -and $rows.Count -ge 45) "every setting listed ($($rows.Count))"
    Assert ($by['BotDamage'].Value -eq '1.25' -and $by['BotDamage'].Source -eq 'all servers') 'the last line for a key wins, comments stripped (as in the core)'
    Assert ($by['SniperEngageMeters'].Value -eq '550' -and $by['SniperEngageMeters'].Source -eq 'default') 'an out-of-range value in the file is shown as the default the core falls back to'
    Assert ($by['Difficulty'].Value -eq 'elite' -and $by['Driving'].Value -eq '1' -and $by['ArmoredVehicles'].Value -eq '0') 'defaults'
    $rows2 = Get-BmEffectiveSettings $srv $inst[2]; $by2 = @{}; foreach ($r in $rows2) { $by2[$r.Setting.Key] = $r }
    Assert ($by2['Difficulty'].Value -eq 'elite' -and $by2['Difficulty'].Source -eq 'this server') "a server's own file wins for that server ($($by2['Difficulty'].Source))"
    $S = @{}; foreach ($x in $script:BmSettings) { $S[$x.Key] = $x }
    Assert ((Test-BmSettingValue $S['SniperEngageMeters'] '2000') -eq $null -and (Test-BmSettingValue $S['SniperEngageMeters'] ' 700 ') -eq '700') 'ranges checked'
    Assert ((Test-BmSettingValue $S['Driving'] 'on') -eq '1' -and (Test-BmSettingValue $S['Driving'] 'off') -eq '0' -and (Test-BmSettingValue $S['Driving'] 'maybe') -eq $null) 'on / off'
    Assert ((Test-BmSettingValue $S['Difficulty'] 'Veteran') -eq 'veteran' -and (Test-BmSettingValue $S['Difficulty'] '3') -eq 'hard' -and (Test-BmSettingValue $S['Difficulty'] 'brutal') -eq $null) 'levels'
    Assert ((Test-BmSettingValue $S['GroundDriversPerTeam'] '4.5') -eq $null -and (Test-BmSettingValue $S['GroundDriversPerTeam'] '8') -eq '8' -and (Test-BmSettingValue $S['BotDamage'] '1,5') -eq $null -and (Test-BmSettingValue $S['BotDamage'] '2.25') -eq '2.25') 'whole numbers and decimals (a comma is refused, not misread)'
    Set-BmSettings $srv $null @{ BotDamage = '2'; SupplyMeters = '80' }
    $g = [IO.File]::ReadAllLines((Join-Path $bm 'enhanced.ini'))
    Assert (($g -contains '# tuning') -and ($g -contains '[CombatAI]') -and ($g -contains 'Aggression=2') -and (@($g | Where-Object { $_ -match '^BotDamage=' }).Count -eq 1) -and ($g -contains 'BotDamage=2') -and ($g -contains 'SupplyMeters=80')) "all-servers file: a key replaced where it was (duplicates folded), new keys added, the rest kept ($($g -join ' | '))"
    Assert ((Read-BmIni (Join-Path $bm 'enhanced.ini'))['BotDamage'] -eq '2') 'the core reads the new value'
    Set-BmSettings $srv $null @{ SniperPairs = '0' }
    Assert (@([IO.File]::ReadAllLines((Join-Path $bm 'enhanced.ini')) | Where-Object { $_ -eq '# Set in Server Control:' }).Count -eq 1 -and (Read-BmIni (Join-Path $bm 'enhanced.ini'))['SniperPairs'] -eq '0') 'a second save adds its keys under the same note'
    Set-BmSettings $srv $inst[2] @{ SniperOverwatch = '0' } @('Difficulty')
    $o = Read-BmIni (Join-Path $bm 'enhanced_25302.ini')
    Assert ($o['SniperOverwatch'] -eq '0' -and -not $o.ContainsKey('Difficulty')) 'a server''s own setting set / taken back to the all-servers value'
    Send-BmSettingsReload $srv $inst[2]
    Assert ((Get-Content (Join-Path $srv 'BotMod_25302.cmd')) -contains 'ehreload') 'apply now = the settings re-read command (ehreload), never the core reload'
} finally {
    foreach ($p in $procs) { try { Stop-Process -Id $p.Id -Force } catch { } }
    Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}
# V14 fix (2026-10-11): the one click asked for administrator rights from a hidden window before Server Control opened;
# the Windows prompt could end up as a flashing taskbar button and nothing happened (no Launcher.log at all).
$winPath = $Gui
$ast = [Management.Automation.Language.Parser]::ParseFile((Resolve-Path $winPath).Path, [ref]$null, [ref]$null)
$top = @($ast.EndBlock.Statements | ForEach-Object { $_.Extent.Text })
$shownAt = [array]::FindIndex([string[]]$top, [Predicate[string]]{ param($t) $t -like '$form.Add_Shown(*' })
$early = @($top[0..($shownAt - 1)] | Where-Object { $_ -match 'Restart-Elevated \$true' -and $_ -notmatch '^function ' })
Assert ($shownAt -gt 0 -and $early.Count -eq 0) 'no request for administrator rights before the window is shown'
Assert ((($top | Where-Object { $_ -like 'function Start-OneClick*' }) -match 'Restart-Elevated \$true') -and (($top | Where-Object { $_ -like '$form.Add_Shown(*' }) -match 'StartupDone = \$true')) 'the one click asks from the open window; startup errors are reported until it opens'
Assert (@(@($ast.EndBlock.Traps) | Where-Object { $_ }).Count -ge 1 -or ($ast.Extent.Text -match '(?m)^trap \{')) 'a startup error goes to Launcher.log and a message box'
$fn = $ast.FindAll({ param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Restart-Elevated' }, $true)[0].Extent.Text
$hd = Join-Path ([IO.Path]::GetTempPath()) ('bm-handoff-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $hd | Out-Null
try {
    $script:HandoffPath = Join-Path $hd 'Launcher.handoff'; $script:HandoffWait = 2; $dir = 'D:\BF-Portable\Battlefield_3_Server'
    $script:hlog = @(); function Write-BpLog([string]$l) { $script:hlog += $l }
    $script:childArgs = $null; $script:childMode = 'opens'
    function Start-Process { param($FilePath, $Verb, $ArgumentList, $WindowStyle)
        if ($script:childMode -eq 'refused') { throw 'The operation was canceled by the user.' }
        $script:childArgs = $ArgumentList
        if ($script:childMode -eq 'opens') { $t = $ArgumentList[[array]::IndexOf($ArgumentList, '-Handoff') + 1]; [IO.File]::WriteAllText($script:HandoffPath, $t) } }
    . ([scriptblock]::Create($fn))
    $r1 = Restart-Elevated $true
    Assert ($r1 -and $script:childArgs -contains '-Launch' -and $script:childArgs -contains '-Handoff') 'administrator window opened and said so: this one closes'
    $script:childMode = 'silent'; $script:hlog = @()
    $r2 = Restart-Elevated $true
    Assert (-not $r2 -and ($script:hlog -join ' ') -match 'did not open within 2 s - carrying on') "administrator window never opened: this one carries on and says so ($($script:hlog -join ' | '))"
    $script:childMode = 'refused'; $script:hlog = @()
    $r3 = Restart-Elevated $true
    Assert (-not $r3 -and ($script:hlog -join ' ') -match 'not given') 'administrator rights refused: logged, carries on'
    Remove-Item function:Start-Process
} finally { Remove-Item -Recurse -Force $hd -ErrorAction SilentlyContinue }
# ---- AltBotMod V16.2: network (IP configurator), accounts, quick join, theme ----
$t2 = $null; $e2 = $null; [void][Management.Automation.Language.Parser]::ParseFile((Join-Path (Split-Path -Parent $Core) 'ServerControl-Theme.ps1'), [ref]$t2, [ref]$e2)
Assert (-not $e2) 'the theme script parses'
$gt = [IO.File]::ReadAllText($Gui)
Assert ($gt -match "ServerControl-Theme\.ps1" -and $gt -match 'Set-BmTheme \$form' -and ([regex]::Matches($gt, 'Set-BmTheme \$f; \[void\]\$f\.ShowDialog\(\$form\)').Count -ge 5)) 'the window and every dialog take the BF3 look'
Assert ($gt -notmatch '\[Drawing\.Color\]::(Black|White|MistyRose|LightYellow)') 'no light-theme colours left in the window script'
$nr = Join-Path ([IO.Path]::GetTempPath()) ('bm-net-' + [guid]::NewGuid().ToString('N'))
try {
    $srvDir = Join-Path $nr 'Battlefield_3_Server'; New-Item -ItemType Directory -Force -Path $srvDir, (Join-Path $nr 'Redirector'), (Join-Path $nr 'Client-Files\BF-Launcher') | Out-Null
    [IO.File]::WriteAllText((Join-Path $nr 'Redirector\ServerIP.txt'), '192.168.1.100')
    [IO.File]::WriteAllText((Join-Path $nr 'Redirector\config.ini'), "[Settings]`r`ndns_host = localhost`r`nip_host = 127.0.0.1`r`n")
    [IO.File]::WriteAllText((Join-Path $nr 'Client-Files\BF-Launcher\Settings.ini'), "BaseURL=http://127.0.0.1/api/`r`nAPIKey=abc123`r`nBF3=get_servers_bf3.php`r`n")
    [IO.File]::WriteAllText((Join-Path $nr 'Client-Files\BF-Launcher\Path.txt'), "D:\SteamLibrary\steamapps\common\Battlefield 3`r`n`r`n")
    $script:BpConfigPath = Join-Path $nr 'Launcher.json'
    $tg = @(Get-BpIpTargets $srvDir)
    Assert ($tg.Count -eq 4 -and ($tg | Where-Object Key -eq 'serverip').Current -eq '192.168.1.100' -and ($tg | Where-Object Key -eq 'redirector').Current -eq '127.0.0.1' -and ($tg | Where-Object Key -eq 'client').Current -eq '127.0.0.1') 'IP configurator reads the redirector IP, its ip_host and the BF Launcher backend'
    $done = @(Set-BpIp $srvDir '10.0.0.5' @('serverip', 'redirector', 'dns'))
    Assert ($done.Count -eq 3 -and ([IO.File]::ReadAllText((Join-Path $nr 'Redirector\ServerIP.txt'))) -eq '10.0.0.5' -and (Get-BpIniValue (Join-Path $nr 'Redirector\config.ini') 'ip_host') -eq '10.0.0.5' -and (Get-BpIniValue (Join-Path $nr 'Redirector\config.ini') 'dns_host') -eq 'localhost' -and (Read-BpConfig $srvDir).DnsIp -eq '10.0.0.5') 'IP written where ticked, other lines kept'
    Assert ((Get-BpIniValue (Join-Path $nr 'Client-Files\BF-Launcher\Settings.ini') 'BaseURL') -eq 'http://127.0.0.1/api/' -and (Test-Path (Join-Path $nr 'Redirector\config.ini.before-ip'))) 'unticked files untouched; a backup of each changed file'
    [void](Set-BpIp $srvDir '10.0.0.5' @('client')); Assert ((Get-BpIniValue (Join-Path $nr 'Client-Files\BF-Launcher\Settings.ini') 'BaseURL') -eq 'http://10.0.0.5/api/') 'the BF Launcher backend host replaced, path kept'
    $threw = $false; try { [void](Set-BpIp $srvDir '10.0.0' @('serverip')) } catch { $threw = $true }; Assert $threw 'a bad address is refused'
    $cc = Get-BpClientConfig $srvDir
    Assert ($cc.ApiKey -eq 'abc123' -and $cc.Bf3Path -eq 'D:\SteamLibrary\steamapps\common\Battlefield 3') 'BF Launcher settings and BF3 folder read'
    $a = New-BpBf3JoinArgs '127.0.0.1' 'AUTH42' '7' '3'
    Assert ($a -eq '-blazeIp 127.0.0.1 -Core.IgnoreAlways true -Core.EnableJuice false -DisplayAsserts false -super layout.toc -dataPlatform Win32 -webMode MP -Origin_NoAppFocus -AuthCode "AUTH42" -requestState State_ClaimReservation -requestStateParams "<data logintoken=\"AUTH42\" levelmode=\"mp\" personaref=\"7\" role=\"soldier\" gameid=\"3\" putinsquad=\"true\"></data>"|127.0.0.1|') 'join command line is the BF Launcher''s own (bf3.JoinServer: AuthCode twice, user id, game id, blaze IP twice)'
    $u = New-BpBf3JoinUrl '127.0.0.1' 'AUTH42' '7' '3'
    Assert ($u.StartsWith('bf3Lan://') -and [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($u.Substring(9))) -eq $a) 'join URL = bf3Lan:// + Base64(UTF-8)'
    Assert ((Resolve-BpBlazeIp 'http://127.0.0.1/api/') -eq '127.0.0.1' -and (Get-BpUrlHost 'https://bf.example:8080/x') -eq 'bf.example') 'backend address resolved'
    $script:BpAccountsPath = Join-Path $nr 'Accounts.json'
    $acc = Read-BpAccounts; $acc.Accounts = @([pscustomobject]@{ User = 'Player1'; Secret = (Protect-BpSecret 'p@ss w0rd') }); $acc.Default = 'Player1'; $acc.AutoJoin = $true; $acc.AutoJoinServer = 'AltBotMod 1'; Save-BpAccounts $acc
    $raw = [IO.File]::ReadAllText($script:BpAccountsPath); $back = Read-BpAccounts
    Assert ($raw -notmatch 'p@ss' -and $back.Accounts.Count -eq 1 -and (Unprotect-BpSecret $back.Accounts[0].Secret) -eq 'p@ss w0rd' -and $back.AutoJoin -and $back.AutoJoinServer -eq 'AltBotMod 1' -and $back.Default -eq 'Player1') 'accounts saved without the plain password and read back'
    Assert (@(Get-BpServerList 'http://127.0.0.1:9/api/' 'k').Count -eq 0 -and -not (Invoke-BpLogin 'http://127.0.0.1:9/api/' 'u' 'p').Ok) 'a backend that is down gives no servers and a failed login, no exception'
} finally { Remove-Item -Recurse -Force $nr -ErrorAction SilentlyContinue }
Write-Host 'PASS: V16.2 Server Control - BF3 look on the window and every dialog; IP configurator (redirector ServerIP.txt / config.ini ip_host, LAN DNS answer, BF Launcher backend; ticked only, backups, bad address refused); quick join builds the BF Launcher''s own bf3Lan:// command line; accounts stored encrypted'
if ($fail) { Write-Host "$fail check(s) failed"; exit 1 }
Write-Host 'PASS: Server Control core - V14 one click asks for administrator rights from the open window (not a hidden one), checks the administrator window opened (else carries on), startup errors logged; launcher settings (Launcher.json kept / merged / broken, each part''s program, arguments, admin, typed answers, required), start plans (no empty argument list, DNS answers, administrator prompts), failures reported (missing part, closed server, part that cannot start), map lists edited while a server is off and filled with every Conquest / Rush / TDM / close-quarters map; V12 BF-Portable one-click start (order, skipping running services, waiting for the database / emulator ports, only ticked servers, stopping on a dead database, missing programs), per-server rules (Startup.txt + .bat, broken quotes, password kept, presets, checks, apply now with refusals), Battlelog address; V11 settings window data (every setting: file / server / default, checks, writing, apply now), 4 instances from the .bat files, RCON login, status, play now / next, rotation edit + save, skip / restart / end round, players, tickets, chat, per-instance bot count and difficulty, reload blocked; window script parses'
