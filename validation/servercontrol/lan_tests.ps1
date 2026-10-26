# AltBotMod V21.1: LAN servers (BFPortable-Core.ps1) against two fake BF-Portable backends (mock_backend.py): this PC's
# own (from the BF Launcher settings) and another PC's (typed in as host:port). Finding them, their servers, registering
# a player on the other PC, the accounts per backend, and joining (BF3 start replaced by a recorder). No window opened.
param([Parameter(Mandatory)][string]$Dir, [Parameter(Mandatory)][string]$Python, [Parameter(Mandatory)][string]$Mock)
$ErrorActionPreference = 'Stop'
. (Join-Path $Dir 'BFPortable-Core.ps1')
$script:n = 0; $script:fail = 0
function Assert($c, [string]$what) { $script:n++; if (-not $c) { $script:fail++; Write-Host "FAIL: $what" } }
function Free-Port { $l = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0); $l.Start(); $p = $l.LocalEndpoint.Port; $l.Stop(); return $p }
$sep = [IO.Path]::DirectorySeparatorChar
$root = Join-Path ([IO.Path]::GetTempPath()) ('bm-sc-lan-' + [Guid]::NewGuid().ToString('N'))
$srv = Join-Path $root 'Battlefield_3_Server'; $bl = Join-Path $root ('Client-Files' + $sep + 'BF-Launcher'); $bf3 = Join-Path $root 'BF3'
New-Item -ItemType Directory -Path $srv, $bl, $bf3 -Force | Out-Null
$pA = Free-Port; $pB = Free-Port; $procs = @()
try {
    foreach ($x in @(@($pA, 'Home'), @($pB, 'Dellg2'))) {
        $procs += Start-Process -FilePath $Python -ArgumentList @($Mock, $x[0], $x[1]) -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $root "mock-$($x[0]).log")
    }
    foreach ($p in @($pA, $pB)) {
        $ok = $false; for ($i = 0; $i -lt 50 -and -not $ok; $i++) { $ok = [bool](Test-BpOpenPorts @('127.0.0.1') $p 200); if (-not $ok) { Start-Sleep -Milliseconds 100 } }
        Assert $ok "fake backend on $p up"
    }
    [IO.File]::WriteAllText((Join-Path $bl 'Settings.ini'), "[Settings]`r`nBaseURL=http://127.0.0.1:$pA/api/`r`nAPIKey=key-Home`r`n")
    [IO.File]::WriteAllText((Join-Path $bl 'Path.txt'), $bf3 + "`r`n")
    # 1. network helpers
    $h = @(Get-BpLanHosts @('192.168.1.20', '127.0.0.1', '169.254.3.3'))
    Assert ($h.Count -eq 253 -and $h -notcontains '192.168.1.20' -and $h -contains '192.168.1.1' -and $h -contains '192.168.1.254') 'hosts to try: the /24 without this PC, no loopback or link-local'
    $open = @(Test-BpOpenPorts @('127.0.0.1') $pB 600)
    Assert ($open.Count -eq 1 -and $open[0] -eq '127.0.0.1') 'open port found'
    $closed = Free-Port
    Assert (@(Test-BpOpenPorts @('127.0.0.1') $closed 300).Count -eq 0) 'closed port: nothing'
    Assert ((Get-BpBackendKey "http://127.0.0.1:$pB/api/") -eq 'key-Dellg2') 'backend key'
    Assert ($null -eq (Get-BpBackendKey "http://127.0.0.1:$closed/api/" 2)) 'no backend there: no key'
    # 2. finding backends: this PC first, the typed-in one, a dead one skipped, this PC not twice
    $b = @(Find-BpLanBackends $srv @("127.0.0.1:$pB", "127.0.0.1:$closed", "127.0.0.1:$pA", ' ') -NoScan)
    Assert ($b.Count -eq 2) "two backends (found $($b.Count))"
    Assert ($b[0].Local -and $b[0].Name -eq 'This PC' -and $b[0].ApiKey -eq 'key-Home') 'this PC first, its key from the launcher settings'
    Assert (-not $b[1].Local -and $b[1].BaseUrl -eq "http://127.0.0.1:$pB/api/" -and $b[1].ApiKey -eq 'key-Dellg2') 'the other PC with its own key'
    Assert (@(Find-BpLanBackends '' @() -NoScan).Count -eq 0) 'no settings, nothing typed: none'
    # 3. servers behind each backend
    $s = @(Get-BpLanServers $b)
    Assert ($s.Count -eq 2 -and $s[0].gnam -eq 'Home Conquest' -and $s[1].gnam -eq 'Dellg2 Conquest') 'one server per backend'
    Assert ($s[1].Backend.BaseUrl -eq $b[1].BaseUrl -and $s[1].level -eq 'MP_001') 'each server knows its backend'
    # 4. registering on the other PC
    Assert (-not (Invoke-BpRegister $b[1].BaseUrl $b[1].ApiKey 'Rob_X' 'pw').Ok) 'name with _ refused before asking'
    Assert (-not (Invoke-BpRegister $b[1].BaseUrl $b[1].ApiKey 'Player12' '').Ok) 'empty password refused'
    $r = Invoke-BpRegister $b[1].BaseUrl $b[1].ApiKey 'Rob Two' 'pw2'
    Assert ($r.Ok) "new player registered ($($r.Message))"
    $r = Invoke-BpRegister $b[1].BaseUrl $b[1].ApiKey 'Player1' 'x'
    Assert (-not $r.Ok -and $r.Message -match 'already exists') "taken name: the backend's message ($($r.Message))"
    Assert (-not (Invoke-BpRegister $b[1].BaseUrl 'wrong' 'Rob Three' 'pw').Ok) 'wrong key refused'
    $l = Invoke-BpLogin $b[1].BaseUrl 'Rob Two' 'pw2'
    Assert ($l.Ok -and $l.AuthCode -eq 'auth-Dellg2-Rob Two') 'the new player logs in there'
    Assert (-not (Invoke-BpLogin $b[0].BaseUrl 'Rob Two' 'pw2').Ok) '... and not on this PC'
    # 5. accounts per backend (saved and read back)
    $accPath = Join-Path $root 'Accounts.json'
    $acc = [pscustomobject]@{ Accounts = @(
            [pscustomobject]@{ User = 'Player1'; Secret = (Protect-BpSecret 'pw1'); Backend = '' },
            [pscustomobject]@{ User = 'Rob Two'; Secret = (Protect-BpSecret 'pw2'); Backend = $b[1].BaseUrl }); Default = 'Player1'; AutoJoin = $false; AutoJoinServer = '' }
    Save-BpAccounts $acc $accPath; $acc = Read-BpAccounts $accPath
    Assert ($acc.Accounts.Count -eq 2 -and $acc.Accounts[1].Backend -eq $b[1].BaseUrl -and $acc.Accounts[0].Backend -eq '') 'backend saved with each account'
    $mine = @(Get-BpAccountsFor $acc $b[0]); $theirs = @(Get-BpAccountsFor $acc $b[1])
    Assert ($mine.Count -eq 1 -and $mine[0].User -eq 'Player1' -and $theirs.Count -eq 1 -and $theirs[0].User -eq 'Rob Two') 'each PC shows only its own players'
    # 6. joining: logs in on the server's own backend, BF3 gets that PC as Blaze address and the game id
    $script:started = @()
    function Start-BpBf3Join([string]$bf3Path, [string]$url) { $script:started += , @($bf3Path, $url); return $null }
    $line = Invoke-BpLanJoin $srv $theirs[0] $s[1]
    $args1 = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($script:started[0][1].Substring(9)))
    Assert ($script:started.Count -eq 1 -and $script:started[0][0] -eq $bf3 -and $script:started[0][1].StartsWith('bf3Lan://')) 'BF3 started from the launcher''s folder'
    Assert ($args1 -match '-blazeIp 127\.0\.0\.1 ' -and $args1 -match 'AuthCode "auth-Dellg2-Rob Two"' -and $args1 -match ('gameid=\\"' + $s[1].gid + '\\"')) 'join: that backend''s login, Blaze address, game id'
    Assert ($line -match 'Rob Two joins Dellg2 Conquest') "log line ($line)"
    $line = Invoke-BpLanJoin $srv $mine[0] $s[0]
    $args2 = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($script:started[1][1].Substring(9)))
    Assert ($args2 -match 'auth-Home-Player1' -and $line -match 'Player1 joins Home Conquest') 'join on this PC'
    $bad = [pscustomobject]@{ User = 'Rob Two'; Secret = (Protect-BpSecret 'nope'); Backend = $b[1].BaseUrl }
    $err = ''; try { Invoke-BpLanJoin $srv $bad $s[1] | Out-Null } catch { $err = $_.Exception.Message }
    Assert ($err -match 'login as Rob Two on .* failed' -and $script:started.Count -eq 2) 'wrong password: no BF3 start, a clear reason'
    # a server from a list made before V21.1 (no backend): the quick join path
    $old = [pscustomobject]@{ gid = '5'; gnam = 'Old list' }
    Assert ((Invoke-BpLanJoin $srv $mine[0] $old) -match 'joins Old list' -and $script:started.Count -eq 3) 'server without backend: this PC''s quick join'
    # 7. the window code is there and parses
    $errs = $null; [void][Management.Automation.Language.Parser]::ParseFile((Join-Path $Dir 'AltBotMod-ServerControl.ps1'), [ref]$null, [ref]$errs)
    Assert (-not $errs.Count) 'Server Control parses'
    $sc = Get-Content -Raw -LiteralPath (Join-Path $Dir 'AltBotMod-ServerControl.ps1')
    Assert ($sc -match 'function Show-BpLanDialog' -and $sc -match "'LAN servers\.\.\.'" -and $sc -match 'Show-BpLanDialog \}') 'LAN servers button and window'
    Assert ($sc -match 'Invoke-BpRegister' -and $sc -match 'Invoke-BpLanJoin' -and $sc -match 'Find-BpLanBackends') 'the window registers, finds and joins'
} finally {
    foreach ($p in $procs) { try { $p.Kill() } catch { } }
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
if ($script:fail) { Write-Host "$($script:fail) of $($script:n) check(s) failed"; exit 1 }
Write-Host "PASS: $($script:n) LAN server checks (hosts to try, open ports, backend keys, this PC + another PC, servers per backend, register, accounts per backend, join with that backend's login, wrong password, old list)"
