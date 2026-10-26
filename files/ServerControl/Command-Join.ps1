# Joining in the alternate UI, using Server Control's login and BFUlauncher protocol.
function Get-CommandJoinConfig([string]$serverDir) {
    $c=Get-BpClientConfig $serverDir
    if (-not $c.BaseUrl -or $c.BaseUrl -notmatch '^https?://') { throw 'Set the backend BaseURL in the BF Launcher settings first.' }
    if (-not $c.Bf3Path -or -not (Test-Path -LiteralPath (Join-Path $c.Bf3Path 'BFUlauncher.exe') -PathType Leaf)) { throw 'Set the BF3 game folder in BF Launcher first. Its Path.txt must point to a folder containing BFUlauncher.exe.' }
    return $c
}
function Test-CommandLocalAccount($a,[string]$baseUrl) { return (-not $a.Backend -or $a.Backend.TrimEnd('/') -ieq $baseUrl.TrimEnd('/')) }
function Get-CommandJoinPort($server) {
    foreach ($name in @('gprt','gameport','game_port','gport','port')) {
        $p=$server.PSObject.Properties[$name]; $n=0
        if ($p -and [int]::TryParse([string]$p.Value,[ref]$n) -and $n -gt 0 -and $n -lt 65536) { return $n }
    }
    return $null
}
function Get-CommandJoinServers($cfg) {
    $raw=@(Get-BpServerList $cfg.BaseUrl $cfg.ApiKey)
    return @(foreach ($s in $raw) { if ([string]$s.gid -match '^[0-9]+$') { $s } })
}
function Get-CommandJoinOptions([string]$serverDir,$instance,[string]$runtimeName) {
    $cfg=Get-CommandJoinConfig $serverDir
    $servers=@(Get-CommandJoinServers $cfg)
    $accountData=Read-BpAccounts
    $accounts=@($accountData.Accounts | Where-Object { Test-CommandLocalAccount $_ $cfg.BaseUrl } | ForEach-Object { [string]$_.User } | Select-Object -Unique)
    $names=@($runtimeName,$instance.ServerName) | Where-Object { $_ }
    $matches=@($servers | Where-Object { $p=Get-CommandJoinPort $_; ($p -eq $instance.GamePort) -or (-not $p -and $names -icontains $_.gnam) })
    $recommended=''; if ($matches.Count -eq 1) { $recommended=[string]$matches[0].gid }
    $default=''; if ($accounts -contains $accountData.Default) { $default=[string]$accountData.Default } elseif ($accounts.Count -eq 1) { $default=$accounts[0] }
    return @{ Servers=@($servers | ForEach-Object { @{ Gid=[string]$_.gid; Name=[string]$_.gnam; Port=(Get-CommandJoinPort $_) } }); Accounts=$accounts; DefaultUser=$default; RecommendedGid=$recommended; Instance=$instance.Label; TargetName=$(if ($runtimeName) { $runtimeName } else { $instance.ServerName }); GamePort=$instance.GamePort }
}
function Invoke-CommandJoin([string]$serverDir,$instance,$body) {
    $cfg=Get-CommandJoinConfig $serverDir
    $targets=@(Get-CommandJoinServers $cfg | Where-Object { [string]$_.gid -ceq [string]$body.gid })
    if ($targets.Count -ne 1) { throw 'That backend server is no longer listed. Refresh the join dialog and choose it again.' }
    $target=$targets[0]; $port=Get-CommandJoinPort $target
    if ($port -and $port -ne $instance.GamePort) { throw 'The chosen backend server uses another game port. Select the matching instance or refresh the list.' }
    $user=([string]$body.user).Trim(); if (-not $user -or $user.Length -gt 128 -or $user -match '[\x00-\x1f]') { throw 'Enter your BF-Portable player name.' }
    $password=[string]$body.password; $accounts=Read-BpAccounts
    if ([bool]$body.saved) {
        $saved=@($accounts.Accounts | Where-Object { $_.User -ceq $user -and (Test-CommandLocalAccount $_ $cfg.BaseUrl) })
        if (-not $saved.Count) { throw 'This saved player is no longer available. Enter the login again.' }
        $password=Unprotect-BpSecret $saved[0].Secret
        if (-not $password) { throw 'This Windows user cannot decrypt that saved login. Choose Enter login and type the password.' }
    }
    if (-not $password) { throw 'Enter your BF-Portable password.' }
    if ($password.Length -gt 4096) { throw 'Password is too long.' }
    $login=Invoke-BpLogin $cfg.BaseUrl $user $password
    if (-not $login.Ok) { throw ('Login failed: '+[string]$login.Message) }
    if (-not $login.AuthCode -or -not $login.UserId) { throw 'The backend login response is missing its game authentication fields.' }
    $ip=Resolve-BpBlazeIp $cfg.BaseUrl; if (-not $ip) { throw 'Cannot resolve the configured BF-Portable backend address.' }
    if ($body.remember -and -not $body.saved) {
        $secret=Protect-BpSecret $password
        if (-not $secret.StartsWith('dpapi:')) { throw 'Windows could not encrypt the saved login. Uncheck Remember login and try again.' }
        $accounts.Accounts=@($accounts.Accounts | Where-Object { $_.User -cne $user -or -not (Test-CommandLocalAccount $_ $cfg.BaseUrl) })+@([pscustomobject]@{ User=$user; Secret=$secret; Backend='' })
        if (-not $accounts.Default) { $accounts.Default=$user }
        if ($script:BpAccountsPath -and (Test-Path -LiteralPath $script:BpAccountsPath)) { $bak=$script:BpAccountsPath+'.before-command'; if (-not (Test-Path -LiteralPath $bak)) { Copy-Item -LiteralPath $script:BpAccountsPath -Destination $bak } }
        Save-BpAccounts $accounts
    }
    $url=New-BpBf3JoinUrl $ip ([string]$login.AuthCode) ([string]$login.UserId) ([string]$target.gid)
    [void](Start-BpBf3Join $cfg.Bf3Path $url)
    $password=$null
    return @{ Message=("BF3 is starting as {0} and joining {1}." -f $user,$target.gnam) }
}
