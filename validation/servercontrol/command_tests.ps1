# AltBotMod V33: tests AltBotMod Command (the web launcher) for real - the host script is started against a fixture
# server folder and a mock BF3 RCON server (mock_rcon.py), then driven over HTTP like the browser / phone UI does.
param([string]$Dir, [string]$Python = 'python3', [string]$Pwsh = 'pwsh')
$ErrorActionPreference = 'Stop'
$fail = 0
function Assert($ok, [string]$what) { if (-not $ok) { Write-Host "FAIL: $what"; $script:fail++ } else { Write-Host "ok: $what" } }

# every script parses and is plain ASCII (Windows PowerShell 5.1 reads files without a BOM as ANSI)
foreach ($f in @(Get-ChildItem -LiteralPath $Dir -File | Where-Object { $_.Name -like 'Command*' -or $_.Name -eq 'AltBotMod-Command.ps1' })) {
    if ($f.Extension -eq '.ps1') { $t = $null; $e = $null; [void][Management.Automation.Language.Parser]::ParseFile($f.FullName, [ref]$t, [ref]$e); Assert ($e.Count -eq 0) "$($f.Name) parses" }
    if ($f.Extension -in @('.ps1', '.bat', '.cs')) { Assert (-not ([IO.File]::ReadAllBytes($f.FullName) | Where-Object { $_ -gt 127 })) "$($f.Name) is plain ASCII" }
}
$web = Join-Path $Dir 'Command-Web'
foreach ($a in @('index.html', 'command.js', 'command.css', 'qr.js', 'favicon.svg', 'command.ico', 'command-art.png', 'art/arctic.png', 'art/urban.png', 'art/air.png', 'art/signal.png', 'art/frontier.png')) {
    $p = Join-Path $web $a; Assert ((Test-Path -LiteralPath $p -PathType Leaf) -and (Get-Item -LiteralPath $p).Length -gt 100) "web file $a present"
}
$host1 = [IO.File]::ReadAllText((Join-Path $Dir 'AltBotMod-Command.ps1')); $js = [IO.File]::ReadAllText((Join-Path $web 'command.js'))
$ver = [regex]::Match($host1, "CommandVersion='([^']+)'").Groups[1].Value
Assert ($ver -eq 'V33 / Command 1.5' -and $js.Contains("state.meta.Version!=='$ver'")) "host and page agree on the version ($ver)"
Assert ([IO.File]::ReadAllText((Join-Path $web 'index.html')).Contains('ALTBOTMOD V33')) 'page shows V33'
foreach ($bat in @('Command-Desktop.bat', 'Command-Browser.bat', 'Command.bat', 'Command-Admin.bat', 'Command-Allow-Phone.bat')) {
    $b = [IO.File]::ReadAllText((Join-Path $Dir $bat)); $m = [regex]::Matches($b, '%~dp0([A-Za-z0-9_.-]+)')
    Assert ($m.Count -gt 0 -and -not @($m | Where-Object { -not (Test-Path -LiteralPath (Join-Path $Dir $_.Groups[1].Value)) }).Count) "$bat points at files that exist"
}
# the core functions the host calls all exist in the shipped Server Control scripts
$defs = @{}; foreach ($f in Get-ChildItem -LiteralPath $Dir -Filter '*.ps1') { foreach ($m in [regex]::Matches([IO.File]::ReadAllText($f.FullName), '(?m)^\s*function\s+([A-Za-z0-9-]+)')) { $defs[$m.Groups[1].Value] = 1 } }
$used = @(foreach ($f in Get-ChildItem -LiteralPath $Dir -Filter '*.ps1' | Where-Object { $_.Name -like 'Command*' -or $_.Name -eq 'AltBotMod-Command.ps1' }) { foreach ($m in [regex]::Matches([IO.File]::ReadAllText($f.FullName), '\b[A-Z][a-z]+-(Bm|Bp|Lm|Command)[A-Za-z]*\b')) { $m.Value } }) | Sort-Object -Unique
$missing = @($used | Where-Object { -not $defs.ContainsKey($_) -and $_ -ne 'Get-Command' })
Assert (-not $missing.Count) "every Bm/Bp/Lm/Command function the host calls is defined (missing: $($missing -join ', '))"

# fixture server folder: one running instance (mock RCON), one stopped
$root = Join-Path ([IO.Path]::GetTempPath()) ('cmd-test-' + [guid]::NewGuid().ToString('N'))
$srv = Join-Path $root 'Battlefield_3_Server'; $bm = Join-Path $root 'BotMod'; $sc = Join-Path $bm 'ServerControl'
New-Item -ItemType Directory -Force -Path $srv, $sc, (Join-Path $root 'appdata') | Out-Null
Copy-Item -Path (Join-Path $Dir '*') -Destination $sc -Recurse -Force
[IO.File]::WriteAllBytes((Join-Path $srv 'bf3_server_Final.exe'), [byte[]](1, 2, 3))
# Linux test host only: System.Drawing (the live-map terrain picture endpoint) cannot even be compiled here, so that one
# reference is neutralised in the test copy. Windows runs the shipped file unchanged.
if (-not $IsWindows) { $hp = Join-Path $sc 'AltBotMod-Command.ps1'; $txt = [IO.File]::ReadAllText($hp); Assert ($txt.Contains('[Drawing.Imaging.ImageFormat]::Png')) 'picture endpoint present'; [IO.File]::WriteAllText($hp, $txt.Replace('[Drawing.Imaging.ImageFormat]::Png', '$null')) }
$secret = 'T' + [guid]::NewGuid().ToString('N').Substring(0, 10)   # a throwaway RCON password: must never reach the API
$rcon = Get-Random -Minimum 40000 -Maximum 45000; $port = Get-Random -Minimum 45001 -Maximum 50000
foreach ($k in 1, 2) {
    $name = $(if ($k -eq 1) { '_StartServer.bat' } else { '_StartServer2.bat' })
    [IO.File]::WriteAllLines((Join-Path $srv $name), [string[]]@("set INSTANCEDIRECTORY=%~dp0Instance$(if ($k -gt 1) { $k })", 'set SERVERNAME=Battlefield 3 Server',
        "set GAMEPORT=$(25299 + $k)", "set REMOTEADMINPORT=127.0.0.1:$($rcon + $k - 1)", "set ADMINPASSWORD=$secret"))
}
$valid = @{}; . (Join-Path $Dir 'ServerControl-Core.ps1'); foreach ($m in $script:BmMaps) { $valid[$m.Code] = (Get-BmMapModes $m.Code) }
$validFile = Join-Path $root 'valid.json'; ($valid | ConvertTo-Json -Depth 4) | Set-Content $validFile
$mockProc = Start-Process -FilePath $Python -ArgumentList @((Join-Path $PSScriptRoot 'mock_rcon.py'), $rcon, $secret, (Join-Path $root 'state.json'), $validFile) -PassThru
$env:LOCALAPPDATA = Join-Path $root 'appdata'
$log = Join-Path $root 'host.log'
$hostProc = Start-Process -FilePath $Pwsh -ArgumentList @('-NoProfile', '-File', (Join-Path $sc 'AltBotMod-Command.ps1'), '-ServerDir', $root, '-Port', $port, '-LocalOnly', '-NoBrowser') -RedirectStandardOutput $log -RedirectStandardError ($log + '.err') -PassThru
try {
    $session = Join-Path $env:LOCALAPPDATA "AltBotMod/Command/session-$port.json"
    for ($w = 0; $w -lt 150 -and -not (Test-Path -LiteralPath $session); $w++) { Start-Sleep -Milliseconds 200 }
    Assert (Test-Path -LiteralPath $session) "host starts and writes its session ($(Get-Content -Raw -ErrorAction SilentlyContinue ($log + '.err')))"
    $key = ([IO.File]::ReadAllText($session) | ConvertFrom-Json).Key
    Assert ($key -match '^[0-9a-f]{64}$') 'a 256-bit pairing key'
    $base = "http://127.0.0.1:$port"; $auth = @{ Authorization = "Bearer $key" }
    function Call([string]$path, $headers = $auth, [string]$method = 'GET', $body = $null) {
        $p = @{ Uri = $base + $path; Method = $method; Headers = $headers; SkipHttpErrorCheck = $true; TimeoutSec = 30 }
        if ($null -ne $body) { $p.Body = ($body | ConvertTo-Json -Depth 8 -Compress); $p.ContentType = 'application/json' }
        return Invoke-WebRequest @p
    }
    $r = Call '/' @{}; Assert ($r.StatusCode -eq 200 -and $r.Content -match 'command\.js' -and $r.Headers['Content-Security-Policy']) 'page served with a CSP'
    foreach ($s in @('/command.js', '/command.css', '/qr.js', '/command-art.png', '/art/frontier.png', '/maps/MP_001.jpg', '/maps/XP5_004.jpg', '/favicon.svg', '/command.ico')) { Assert ((Call $s @{}).StatusCode -eq 200) "GET $s" }
    foreach ($s in @('/maps/NOPE.jpg', '/../ServerControl-Core.ps1', '/Command-Http.cs', '/art/other.png')) { Assert ((Call $s @{}).StatusCode -eq 404) "GET $s refused" }
    Assert ((Call '/api/meta' @{}).StatusCode -eq 401) 'API needs the pairing key'
    Assert ((Call '/api/meta' @{ Authorization = 'Bearer ' + ('0' * 64) }).StatusCode -eq 401) 'a wrong key is refused'
    Assert ((Call '/api/meta' @{ Authorization = "Bearer $key"; Host = "evil.example:$port" }).StatusCode -eq 403) 'an unknown Host is refused (DNS rebinding)'
    Assert ((Call '/api/action' @{ Authorization = "Bearer $key"; Origin = 'http://evil.example' } 'POST' @{ id = 1; action = 'next' }).StatusCode -eq 403) 'a foreign Origin is refused'
    $r = Call '/api/meta'; $meta = $r.Content | ConvertFrom-Json
    Assert ($r.StatusCode -eq 200 -and $meta.Version -eq 'V33 / Command 1.5' -and $meta.Maps.Count -eq 29 -and $meta.LocalOnly) "meta: version, 29 maps, local only ($($r.StatusCode))"
    Assert (@($meta.Settings | Where-Object { $_.Key -eq 'DoorGunMeters' -or $_.Key -eq 'LockOnSeconds' }).Count -eq 2) 'meta lists the V31+ bot settings'
    $all = $r.Content
    $st = Call '/api/status'; $status = $st.Content | ConvertFrom-Json; $all += $st.Content
    Assert ($st.StatusCode -eq 200 -and $status.Servers.Count -eq 2) "status: two instances ($($st.StatusCode))"
    Assert (@($status.Servers | Where-Object { $_.Online }).Count -eq 1 -and ($status.Servers | Where-Object { $_.Online }).Info.Map -eq 'XP5_001') 'status: the running instance answers over RCON'
    $i = Call '/api/instance?id=1'; $all += $i.Content; Assert ($i.StatusCode -eq 200 -and @(($i.Content | ConvertFrom-Json).Players).Count -ge 1) 'instance: players over RCON'
    $s = Call '/api/settings?id=1'; $all += $s.Content; $rows = ($s.Content | ConvertFrom-Json)
    Assert ($s.StatusCode -eq 200 -and $rows.Rows.Count -gt 50 -and $rows.Revision) "settings for server 1 ($($rows.Rows.Count) rows)"
    $save = Call '/api/settings/save' $auth 'POST' @{ id = 1; revision = $rows.Revision; values = @{ DoorGunMeters = '700' }; remove = @(); apply = $false }
    Assert ($save.StatusCode -eq 200) "settings save ($($save.Content))"
    $ini = Get-ChildItem -LiteralPath $bm -Filter 'enhanced*.ini' | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }
    Assert (($ini -join "`n") -match 'DoorGunMeters\s*=\s*700') 'the saved value is in the server''s settings file'
    $stale = Call '/api/settings/save' $auth 'POST' @{ id = 1; revision = $rows.Revision; values = @{ DoorGunMeters = '650' }; remove = @(); apply = $false }
    Assert ($stale.StatusCode -eq 409) 'a stale revision is refused (edited elsewhere)'
    Assert ((Call '/api/settings/save' $auth 'POST' @{ id = 1; revision = (Call '/api/settings?id=1').Content.Split('"Revision":"')[1].Split('"')[0]; values = @{ DoorGunMeters = '99999' }; remove = @(); apply = $false }).StatusCode -eq 400) 'an out-of-range value is refused'
    $b = Call '/api/action' $auth 'POST' @{ id = 1; action = 'bots'; count = 24 }; Assert ($b.StatusCode -eq 200) "bot count saved ($($b.Content))"
    $n = Call '/api/action' $auth 'POST' @{ id = 1; action = 'next' }; Assert ($n.StatusCode -eq 200) "next map over RCON ($($n.Content))"
    Assert ((Call '/api/action' $auth 'POST' @{ id = 2; action = 'next' }).StatusCode -eq 400) 'a stopped server reports RCON unavailable'
    Assert ((Call '/api/action' $auth 'POST' @{ id = 9; action = 'next' }).StatusCode -eq 400) 'an unknown instance is refused'
    Assert ((Call '/api/action' $auth 'GET').StatusCode -eq 400) 'actions need POST'
    $su = Call '/api/instance?id=1'; $all += $su.Content
    $lc = Call '/api/launcher/config'; $all += $lc.Content; Assert ($lc.StatusCode -eq 200) 'launcher config (local)'
    $lr = Call '/api/learning'; $all += $lr.Content; Assert ($lr.StatusCode -eq 200) 'learning page data'
    Assert (-not $all.Contains($secret)) 'no API answer contains the server admin password'
    Assert (-not (Get-Content -Raw $log).Contains($secret) -and -not (Get-Content -Raw $log).Contains($key)) 'the console shows neither the password nor the pairing key'
} finally {
    if ($hostProc -and -not $hostProc.HasExited) { Stop-Process -Id $hostProc.Id -Force }
    if ($mockProc -and -not $mockProc.HasExited) { Stop-Process -Id $mockProc.Id -Force }
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
if ($fail) { Write-Host "$fail FAILED"; exit 1 }
Write-Host 'PASS: AltBotMod Command'
