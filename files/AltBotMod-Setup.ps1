# AltBotMod V6 installer (based on the BF3 BotMod / Enhanced Test 15 installer).
# Works on any PC: finds the BF3 server folder or asks for it (ALP / bf4play / BF-Portable layouts).
# Settings live in a sibling BotMod folder; DLLs, navigation data and Fun-Bots routes live beside the server EXE.
param(
    [ValidateSet('install', 'uninstall', 'settings', 'fleet')] [string]$Mode = 'install',
    [string]$ServerDir = '',
    [switch]$OpenSettings,
    [switch]$FleetPreset,
    [switch]$ObjectivePreset
)
$ErrorActionPreference = 'Stop'
$Pkg = $PSScriptRoot
$PkgRoot = Split-Path $Pkg -Parent
$ExeName = 'bf3_server_Final.exe'
$ExeSha256 = 'F39F7623063CFDE9C449A0BDC9A57448D2D137D6DEF00B17EA04800EDA955470'
$StockXinputSha256 = '8E3534E38725C51EA9F08340C580374F0EBA8CFD4E6A83CF5BB741F518CD02ED'
$RequiredPackageFiles = @(
    'server\xinput9_1_0_stock.dll', 'server\xinput9_1_0.dll', 'server\BotModCore.dll',
    'BotMod\enhanced.ini', 'BotMod\Open-Settings.bat', 'BotMod\install.bat',
    'BotMod\uninstall.bat', 'BotMod\Enable-Full-Fleet.bat',
    'BotMod\Collect-Diagnostics.bat', 'BotMod\Collect-Diagnostics.ps1',
    'server\BotMod_traces\MP_017_ConquestLarge0.map'
)
$Product = 'AltBotMod V34 - Squad Up'
$KeepIfPresent = @('BotMod_loadouts.txt', 'BotMod_kitextra.txt', 'BotMod_hq_*.txt',
    'BotMod_nav_*.bin', 'BotMod_pave_*.bin', 'BotMod_trees_*.bin', 'admins.txt',
    'botcount.txt', 'botcount_*.txt', 'kitskip.txt', 'enhanced*.ini')
$script:BackupDir = $null
$script:BackedUp = 0

function Say([string]$m, [string]$c = 'Gray') { Write-Host $m -ForegroundColor $c }
function Ask([string]$q) { (Read-Host "$q [Y/N]") -match '^\s*y' }
function Fail([string]$m) { throw $m }
function Ensure-Directory([string]$d) {
    if (-not (Test-Path -LiteralPath $d -PathType Container)) {
        New-Item -ItemType Directory -Path $d -Force | Out-Null
    }
}
function Test-ServerDir([string]$d) {
    return $d -and (Test-Path -LiteralPath (Join-Path $d $ExeName) -PathType Leaf)
}

# Accept the EXE, its folder, BF-Portable, or files\BotMod in an extracted ZIP.
# Walk only ancestors; never recursively scan a drive.
function Resolve-ServerDir([string]$d) {
    if (-not $d) { return $null }
    $d = $d.Trim().Trim('"')
    if (-not (Test-Path -LiteralPath $d)) { return $null }
    $item = Get-Item -LiteralPath $d
    if (-not $item.PSIsContainer) {
        if ($item.Name -ine $ExeName) { return $null }
        return $item.Directory.FullName
    }
    $p = $item.FullName
    for ($i = 0; $i -lt 6 -and $p; $i++) {
        if (Test-ServerDir $p) { return (Get-Item -LiteralPath $p).FullName }
        $sub = Join-Path $p 'Battlefield_3_Server'
        if (Test-ServerDir $sub) { return (Get-Item -LiteralPath $sub).FullName }
        $parent = Split-Path $p -Parent
        if ($parent -eq $p) { break }
        $p = $parent
    }
    return $null
}
function Find-ServerDir {
    $nearby = Resolve-ServerDir $PkgRoot
    if ($nearby) { return $nearby }
    $cands = New-Object System.Collections.Generic.List[string]
    $cands.Add('C:\ALPLauncher\server\Battlefield 3 Server')
    foreach ($drv in (Get-PSDrive -PSProvider FileSystem -ErrorAction SilentlyContinue)) {
        $cands.Add((Join-Path $drv.Root 'BF-Portable'))
        $cands.Add((Join-Path $drv.Root 'ALPLauncher\server\Battlefield 3 Server'))
    }
    foreach ($c in $cands) {
        $r = Resolve-ServerDir $c
        if ($r) { return $r }
    }
    return $null
}
function Pick-ServerDir {
    Say "Where are your BF3 server files? Select the folder containing $ExeName" Yellow
    Say "(usually ...\Battlefield_3_Server), or the folder above it (e.g. your BF-Portable folder)." Yellow
    try {
        Add-Type -AssemblyName System.Windows.Forms
        $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
        $dlg.Description = "AltBotMod: select your BF3 server folder (the one containing $ExeName)"
        $dlg.ShowNewFolderButton = $false
        try {
            if ($dlg.ShowDialog() -eq 'OK') { return (Resolve-ServerDir $dlg.SelectedPath) }
        } finally { $dlg.Dispose() }
    } catch { }
    for ($try = 0; $try -lt 3; $try++) {
        $typed = Read-Host 'Type / paste the server folder or EXE path (blank to cancel)'
        if (-not $typed) { return $null }
        $r = Resolve-ServerDir $typed
        if ($r) { return $r }
        Say "$ExeName is not in '$typed' or the folders above it. Try again." Yellow
    }
    return $null
}
function Get-ServerDir {
    if ($ServerDir) {
        $r = Resolve-ServerDir $ServerDir
        if (-not $r) { Fail "$ExeName was not found at '$ServerDir'. No files were replaced." }
        return $r
    }
    $r = Find-ServerDir
    if ($r) {
        Say "Found a BF3 server: $r" Cyan
        if (Ask 'Install AltBotMod into this server?') { return $r }
        Say 'OK - pick the right one.' Gray
    }
    $r = Pick-ServerDir
    if (-not $r) { Fail "No folder containing $ExeName was selected." }
    return $r
}
function Assert-ServerStopped([string]$srv) {
    $expected = Join-Path $srv $ExeName
    while ($true) {
        $running = @(Get-Process -Name 'bf3_server_Final' -ErrorAction SilentlyContinue | Where-Object {
            try {
                $path = $_.Path
                if (-not $path) { return $true }
                return [string]::Equals($path, $expected, [StringComparison]::OrdinalIgnoreCase)
            } catch { return $true }
        })
        if (-not $running.Count) { return }
        Say "The BF3 server is running ($($running.Count) instance(s)). Stop it from your launcher first." Yellow
        if (-not (Ask 'Retry now that it is stopped?')) {
            Fail 'Cancelled - the server is still running. No files were replaced.'
        }
    }
}
# Settings and backups are outside the server folder, so check both locations.
function Assert-Writable([string]$srv, [string]$root) {
    $blocked = $null
    foreach ($dir in @($srv, $root, (Join-Path $root 'BotMod'))) {
        if (-not (Test-Path -LiteralPath $dir -PathType Container)) { continue }
        $probe = Join-Path $dir ('.botmod_write_test_' + [guid]::NewGuid().ToString('N'))
        try { [IO.File]::WriteAllText($probe, 'x'); Remove-Item -LiteralPath $probe -Force }
        catch { $blocked = $dir; break }
    }
    if (-not $blocked) { return }
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
    if ($isAdmin) { Fail "Cannot write to $blocked." }
    Say 'This location needs administrator rights. Windows will request them.' Yellow
    $extra = if ($OpenSettings) { ' -OpenSettings' } else { '' }
    if ($FleetPreset) { $extra += ' -FleetPreset' }
    if ($ObjectivePreset) { $extra += ' -ObjectivePreset' }
    $args = '-NoProfile -STA -ExecutionPolicy Bypass -File "{0}" -Mode {1} -ServerDir "{2}"{3}' -f $PSCommandPath, $Mode, $srv, $extra
    try {
        $child = Start-Process powershell.exe -Verb RunAs -ArgumentList $args -Wait -PassThru
        exit $child.ExitCode
    } catch { Fail 'Administrator rights were refused.' }
}
function Is-BotModLoader([string]$path) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $false }
    return ([Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($path))).Contains('BotModCore.dll')
}
function Same-File([string]$a, [string]$b) {
    if (-not (Test-Path -LiteralPath $b -PathType Leaf)) { return $false }
    if ((Get-Item -LiteralPath $a).Length -ne (Get-Item -LiteralPath $b).Length) { return $false }
    return (Get-FileHash -LiteralPath $a -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $b -Algorithm SHA256).Hash
}
function Backup([string]$path, [string]$relName) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return }
    $dst = Join-Path $script:BackupDir $relName
    if (Test-Path -LiteralPath $dst) { return } # preserve the first backup before settings migration
    Ensure-Directory (Split-Path $dst -Parent)
    Copy-Item -LiteralPath $path -Destination $dst -Force
    $script:BackedUp++
}
function Is-KeepFile([string]$name) {
    foreach ($k in $KeepIfPresent) { if ($name -like $k) { return $true } }
    return $false
}
function Copy-Set([string]$srcDir, [string]$dstDir, [string]$backupSub, [string[]]$skip) {
    Ensure-Directory $dstDir
    $n = 0
    foreach ($f in Get-ChildItem -LiteralPath $srcDir -File) {
        if ($skip -contains $f.Name) { continue }
        $dst = Join-Path $dstDir $f.Name
        if (Test-Path -LiteralPath $dst) {
            if ((Is-KeepFile $f.Name) -or (Same-File $f.FullName $dst)) { continue }
            Backup $dst (Join-Path $backupSub $f.Name)
        }
        Copy-Item -LiteralPath $f.FullName -Destination $dst -Force
        Unblock-File -LiteralPath $dst -ErrorAction SilentlyContinue
        $n++
    }
    return $n
}
# AltBotMod V33: BF-Portable's Blaze emulator never answers a player-capacity change. A server started with
# vars.maxPlayers other than 64 retries it every ~90 s and, after a few minutes, loses its Blaze connection and kicks
# every player ("disconnected"). On BF-Portable (PortableBF.exe beside the server folder) Startup.txt is put back to 64;
# only that one value is changed (backed up first); nothing else in the file is read out or shown.
function Repair-MaxPlayers([string]$srvDir) {
    if (-not (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $srvDir) 'PortableBF.exe') -PathType Leaf)) { return 0 }
    $fixed = 0
    foreach ($inst in @(Get-ChildItem -LiteralPath $srvDir -Directory -Filter 'Instance*')) {
        $st = Join-Path $inst.FullName 'Admin\Startup.txt'
        if (-not (Test-Path -LiteralPath $st -PathType Leaf)) { continue }
        $text = [IO.File]::ReadAllText($st)
        $next = [regex]::Replace($text, '(?im)^(\s*vars\.maxPlayers\s+)(?!64\b)[0-9]+', '${1}64')
        if ($next -cne $text) {
            Backup $st (Join-Path (Join-Path 'Battlefield_3_Server' $inst.Name) 'Admin\Startup.txt')
            [IO.File]::WriteAllText($st, $next, (New-Object Text.UTF8Encoding($false)))
            Say "$($inst.Name): vars.maxPlayers set back to 64 (BF-Portable drops every player after a few minutes on any other limit)." Cyan
            $fixed++
        }
    }
    # ... and the PLAYERS value its _StartServer*.bat passes on the command line
    foreach ($bat in @(Get-ChildItem -LiteralPath $srvDir -File -Filter '_StartServer*.bat')) {
        $text = [IO.File]::ReadAllText($bat.FullName)
        $next = [regex]::Replace($text, '(?im)^(\s*set\s+PLAYERS=)(?!64\b)[0-9]+', '${1}64')
        if ($next -cne $text) {
            Backup $bat.FullName (Join-Path 'Battlefield_3_Server' $bat.Name)
            [IO.File]::WriteAllText($bat.FullName, $next, (New-Object Text.UTF8Encoding($false)))
            Say "$($bat.Name): PLAYERS set back to 64." Cyan
            $fixed++
        }
    }
    return $fixed
}
# AltBotMod V33: AltBotMod Command (the web / phone launcher) beside Server Control. Copy-Set copies one folder level,
# so the web files (Command-Web, its art and maps) are copied folder by folder; then the artwork is checked and the
# shortcuts are made: desktop 'AltBotMod Command' and Start menu > AltBotMod (Windows only; a shortcut is never fatal).
$CommandArt = @('command-art.png', 'art\arctic.png', 'art\urban.png', 'art\air.png', 'art\signal.png', 'art\frontier.png')
function Install-Command([string]$scDir, [string]$srvDir) {
    $webSrc = Join-Path (Join-Path $Pkg 'ServerControl') 'Command-Web'
    if (-not (Test-Path -LiteralPath $webSrc -PathType Container)) { return }
    $web = Join-Path $scDir 'Command-Web'
    $n = Copy-Set $webSrc $web 'BotMod\ServerControl\Command-Web' @()
    foreach ($sub in @('art', 'maps')) {
        $from = Join-Path $webSrc $sub
        if (Test-Path -LiteralPath $from -PathType Container) { $n += Copy-Set $from (Join-Path $web $sub) (Join-Path 'BotMod\ServerControl\Command-Web' $sub) @() }
    }
    foreach ($asset in $CommandArt) {
        $p = Join-Path $web $asset
        if (-not (Test-Path -LiteralPath $p -PathType Leaf) -or (Get-Item -LiteralPath $p).Length -lt 1000) { Fail "AltBotMod Command is incomplete: artwork missing at $p" }
    }
    foreach ($need in @('AltBotMod-Command.ps1', 'Command-Http.cs', 'Command-Desktop.bat', 'Command.bat', 'Command-Window.ps1', 'Command-Join.ps1', 'Command-Learning.ps1')) {
        if (-not (Test-Path -LiteralPath (Join-Path $scDir $need) -PathType Leaf)) { Fail "AltBotMod Command is incomplete: $need missing in $scDir" }
    }
    # Command finds the server folder from here first (the same folder this installer just used).
    [IO.File]::WriteAllText((Join-Path $scDir 'Command-last-folder.txt'), $srvDir)
    Say "AltBotMod Command: $(Join-Path $scDir 'Command-Desktop.bat') ($n web file(s) updated)" Cyan
    try {
        if ($env:OS -eq 'Windows_NT' -and -not $env:ALTBOTMOD_NO_SHORTCUT) {
            $sh = New-Object -ComObject WScript.Shell
            $menu = Join-Path ([Environment]::GetFolderPath('Programs')) 'AltBotMod'
            Ensure-Directory $menu
            foreach ($place in @([Environment]::GetFolderPath('Desktop'), $menu)) {
                $lnk = $sh.CreateShortcut((Join-Path $place 'AltBotMod Command.lnk'))
                $lnk.TargetPath = Join-Path $scDir 'Command-Desktop.bat'
                $lnk.WorkingDirectory = $scDir
                $lnk.WindowStyle = 7
                $lnk.Description = 'AltBotMod Command - servers, bots, rotation and BF-Portable from a desktop window or your phone'
                $ico = Join-Path $web 'command.ico'; if (Test-Path -LiteralPath $ico) { $lnk.IconLocation = "$ico,0" }
                $lnk.Save()
            }
            $lnk = $sh.CreateShortcut((Join-Path $menu 'AltBotMod Command (browser).lnk'))
            $lnk.TargetPath = Join-Path $scDir 'Command-Browser.bat'; $lnk.WorkingDirectory = $scDir
            $ico = Join-Path $web 'command.ico'; if (Test-Path -LiteralPath $ico) { $lnk.IconLocation = "$ico,0" }
            $lnk.Save()
            Say "Command: the desktop shortcut 'AltBotMod Command' (also in Start menu > AltBotMod)" Cyan
        }
    } catch { Say "Command shortcut not created ($($_.Exception.Message)). Run $(Join-Path $scDir 'Command-Desktop.bat') directly." Yellow }
}
function Remove-CommandShortcuts {
    if ($env:OS -ne 'Windows_NT') { return }
    try {
        $menu = Join-Path ([Environment]::GetFolderPath('Programs')) 'AltBotMod'
        foreach ($p in @((Join-Path ([Environment]::GetFolderPath('Desktop')) 'AltBotMod Command.lnk'), (Join-Path $menu 'AltBotMod Command.lnk'), (Join-Path $menu 'AltBotMod Command (browser).lnk'))) {
            if (Test-Path -LiteralPath $p -PathType Leaf) { Remove-Item -LiteralPath $p -Force }
        }
        if ((Test-Path -LiteralPath $menu) -and -not (Get-ChildItem -LiteralPath $menu -Force)) { Remove-Item -LiteralPath $menu -Force }
    } catch { }
}
# AltBotMod V31: settings still at an old default are moved to the new one (a value the user changed is kept).
function Update-OldDefaults([string]$active) {
    # key = @(new default, old default, older default ...)   (AltBotMod V34: JetAreaMarginMeters 350 / 600 -> 900)
    # Only a line that still holds an old default changes; the file's own line endings and everything else stay as they are.
    $old = @{ 'JetAreaMarginMeters' = @('900', '350', '600') }
    $text = [IO.File]::ReadAllText($active); $changed = 0
    foreach ($key in $old.Keys) {
        foreach ($was in ($old[$key] | Select-Object -Skip 1)) {
            $pattern = '(?m)^(' + [regex]::Escape($key) + '[ \t]*=[ \t]*)' + [regex]::Escape($was) + '([ \t]*)(?=\r?$)'
            $count = ([regex]::Matches($text, $pattern)).Count
            if ($count) { $text = [regex]::Replace($text, $pattern, '${1}' + $old[$key][0] + '${2}'); $changed += $count }
        }
    }
    if ($changed) {
        Backup $active 'BotMod\enhanced.ini'
        [IO.File]::WriteAllText($active, $text, (New-Object Text.UTF8Encoding($false)))
    }
    return $changed
}
function Add-MissingSettings([string]$defaults, [string]$active) {
    $seen = @{}
    foreach ($line in [IO.File]::ReadAllLines($active)) {
        if ($line -match '^\s*([A-Za-z][A-Za-z0-9_]*)\s*=') { $seen[$Matches[1]] = $true }
    }
    $missing = New-Object System.Collections.Generic.List[string]
    foreach ($line in [IO.File]::ReadAllLines($defaults)) {
        if ($line -match '^\s*([A-Za-z][A-Za-z0-9_]*)\s*=') {
            $key = $Matches[1]
            if (-not $seen.ContainsKey($key)) { $missing.Add($line); $seen[$key] = $true }
        }
    }
    if ($missing.Count) {
        Backup $active 'BotMod\enhanced.ini'
        $addition = "`r`n# Missing options added by the installer; existing values above are preserved.`r`n" +
            ($missing -join "`r`n") + "`r`n"
        [IO.File]::AppendAllText($active, $addition, (New-Object Text.UTF8Encoding($false)))
    }
    return $missing.Count
}
# Fleet and faster driving preset requested for Test 11. Other tuning is preserved.
function Set-FleetPreset([string]$active, [string]$backupRelative = 'BotMod\enhanced.ini') {
    if (-not (Test-Path -LiteralPath $active -PathType Leaf)) { Fail 'Active settings missing. Run the installer first.' }
    $features = @('Driving','LightVehicles','Boats','ArmoredVehicles','DriverWeapons','Jets','Helicopters','AircraftWeapons','TransportDrops','ReturnToBase','PassengerTakeover','DriveInStages','TransportLandings','LearnAircraftDeployment','WeaponHandling','ArmorTactics','ArmorInfantrySupport','TrafficManagement','VehicleDiagnostics')
    $speeds = @{LightSpeedMps=34; BoatSpeedMps=22; TankSpeedMps=16; IfvSpeedMps=20}
    $seen = @{}; $lines = New-Object System.Collections.Generic.List[string]
    foreach ($line in [IO.File]::ReadAllLines($active)) {
        if ($line -match '^\s*([A-Za-z][A-Za-z0-9_]*)\s*=\s*([^#;]*)') {
            $key=$Matches[1]; $value=$Matches[2].Trim(); $seen[$key]=$true
            if ($features -contains $key) { $line="$key=1" }
            elseif ($key -in @('GroundDriversPerTeam','AirPilotsPerTeam')) {
                $minimum=if($key -eq 'GroundDriversPerTeam'){16}else{6}
                $count=0; if (-not [int]::TryParse($value,[ref]$count) -or $count -lt $minimum -or ($key -eq 'AirPilotsPerTeam' -and $count -gt 6)) { $line="$key=$minimum" }
            }
            elseif ($key -eq 'VehicleReturnMeters') {
                $range=0.0
                if (-not [double]::TryParse($value,[Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,[ref]$range) -or $range -lt 3000) { $line='VehicleReturnMeters=3000' }
            }
            elseif ($key -eq 'ReserveHumanMeters' -and $value -eq '15') { $line='ReserveHumanMeters=5' }
            elseif ($speeds.ContainsKey($key)) {
                $speed=0.0
                if (-not [double]::TryParse($value,[Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,[ref]$speed) -or $speed -lt $speeds[$key]) { $line="$key=$($speeds[$key])" }
            }
        }
        $lines.Add($line)
    }
    foreach ($key in $features) { if (-not $seen.ContainsKey($key)) { $lines.Add("$key=1") } }
    foreach ($key in @('GroundDriversPerTeam','AirPilotsPerTeam')) { if (-not $seen.ContainsKey($key)) { $minimum=if($key -eq 'GroundDriversPerTeam'){16}else{6};$lines.Add("$key=$minimum") } }
    if (-not $seen.ContainsKey('VehicleReturnMeters')) { $lines.Add('VehicleReturnMeters=3000') }
    if (-not $seen.ContainsKey('ReserveHumanMeters')) { $lines.Add('ReserveHumanMeters=5') }
    foreach ($key in @('LightSpeedMps','BoatSpeedMps','TankSpeedMps','IfvSpeedMps')) { if (-not $seen.ContainsKey($key)) { $lines.Add("$key=$($speeds[$key])") } }
    $next=($lines -join "`r`n")+"`r`n"
    if ([IO.File]::ReadAllText($active) -cne $next) { Backup $active $backupRelative; [IO.File]::WriteAllText($active,$next,(New-Object Text.UTF8Encoding($false))) }
}
function Set-AllFleetPresets([string]$active) {
    Set-FleetPreset $active
    foreach ($override in Get-ChildItem -LiteralPath (Split-Path $active -Parent) -Filter 'enhanced_*.ini' -File) {
        if ($override.Name -match '^enhanced_[0-9]+\.ini$') {
            Set-FleetPreset $override.FullName (Join-Path 'BotMod' $override.Name)
            Say "Vehicle preset applied to port override: $($override.FullName)" Cyan
        }
    }
}
# Test15 installation enables the requested duties without changing physics/combat tuning.
function Set-ObjectivePreset([string]$active, [string]$backupRelative = 'BotMod\enhanced.ini') {
    $features=@('Driving','LightVehicles','Boats','ArmoredVehicles','DriverWeapons','Jets','Helicopters','AircraftWeapons','ReturnToBase','PassengerTakeover','DriveInStages','TransportDrops','TransportLandings','LearnAircraftDeployment','VehicleDiagnostics')
    $seen=@{}; $lines=New-Object System.Collections.Generic.List[string]
    foreach ($line in [IO.File]::ReadAllLines($active)) {
        if ($line -match '^\s*([A-Za-z][A-Za-z0-9_]*)\s*=\s*([^#;]*)') {
            $key=$Matches[1];$value=$Matches[2].Trim();$seen[$key]=$true
            if ($features -contains $key) { $line="$key=1" }
            elseif ($key -eq 'AirPilotsPerTeam') { $line='AirPilotsPerTeam=6' }
            elseif ($key -eq 'GroundDriversPerTeam') {
                $count=0;if (-not [int]::TryParse($value,[ref]$count) -or $count -lt 16) { $line='GroundDriversPerTeam=16' }
            }
            elseif ($key -eq 'VehicleReturnMeters') {
                $range=0.0
                if (-not [double]::TryParse($value,[Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,[ref]$range) -or $range -lt 3000) { $line='VehicleReturnMeters=3000' }
            }
            elseif ($key -eq 'ReserveHumanMeters' -and $value -eq '15') { $line='ReserveHumanMeters=5' }
        }
        $lines.Add($line)
    }
    foreach ($key in $features) { if (-not $seen.ContainsKey($key)) { $lines.Add("$key=1") } }
    foreach ($entry in @('AirPilotsPerTeam=6','GroundDriversPerTeam=16','VehicleReturnMeters=3000','ReserveHumanMeters=5')) {
        $key=$entry.Split('=')[0];if (-not $seen.ContainsKey($key)) { $lines.Add($entry) }
    }
    $next=($lines -join "`r`n")+"`r`n"
    if ([IO.File]::ReadAllText($active) -cne $next) { Backup $active $backupRelative;[IO.File]::WriteAllText($active,$next,(New-Object Text.UTF8Encoding($false))) }
}
function Set-AllObjectivePresets([string]$active) {
    Set-ObjectivePreset $active
    foreach ($override in Get-ChildItem -LiteralPath (Split-Path $active -Parent) -Filter 'enhanced_*.ini' -File) {
        if ($override.Name -match '^enhanced_[0-9]+\.ini$') { Set-ObjectivePreset $override.FullName (Join-Path 'BotMod' $override.Name) }
    }
}
# Keep a complete payload for installed shortcuts, independent of the ZIP location.
function Cache-Installer([string]$botModDir) {
    $cache = Join-Path $botModDir 'installer'
    if ([string]::Equals([IO.Path]::GetFullPath($Pkg), [IO.Path]::GetFullPath($cache), [StringComparison]::OrdinalIgnoreCase)) { return }
    Ensure-Directory $cache
    foreach ($name in @('AltBotMod-Setup.ps1', 'BotMod', 'server')) {
        $source = Join-Path $Pkg $name
        $files = if (Test-Path -LiteralPath $source -PathType Container) {
            Get-ChildItem -LiteralPath $source -Recurse -File
        } else { Get-Item -LiteralPath $source }
        foreach ($f in $files) {
            $rel = $f.FullName.Substring($Pkg.Length).TrimStart([char[]]'\/')
            $dst = Join-Path $cache $rel
            if (Same-File $f.FullName $dst) { continue }
            if (Test-Path -LiteralPath $dst) { Backup $dst (Join-Path 'BotMod\installer' $rel) }
            Ensure-Directory (Split-Path $dst -Parent)
            Copy-Item -LiteralPath $f.FullName -Destination $dst -Force
            Unblock-File -LiteralPath $dst -ErrorAction SilentlyContinue
        }
    }
}
function Open-ActiveSettings([string]$active) {
    if (-not (Test-Path -LiteralPath $active -PathType Leaf)) {
        Fail "Active settings missing: $active. Run the installer first."
    }
    try { Start-Process notepad.exe -ArgumentList ('"{0}"' -f $active) | Out-Null }
    catch { Say "Could not open Notepad. Open this file manually: $active" Yellow }
}
function Invoke-BotModSetup {
    Say ''
    Say "$Product ($Mode)" White
    $srv = Get-ServerDir
    $root = Split-Path $srv -Parent
    $botModDir = Join-Path $root 'BotMod'
    $active = Join-Path $botModDir 'enhanced.ini'
    Say "Server:          $srv" Cyan
    Say "ACTIVE settings: $active" Cyan
    if ($Mode -eq 'settings') { Open-ActiveSettings $active; return }
    if ($Mode -eq 'fleet') {
        $script:BackupDir=Join-Path $root ('BotMod_backup_'+[guid]::NewGuid().ToString('N'))
        Set-AllFleetPresets $active
        Say "Full fleet enabled in $active. Restart the server to apply it." Green
        Say 'enhanced_<port>.ini overrides can still disable individual classes.' Yellow
        return
    }
    $xin = Join-Path $srv 'xinput9_1_0.dll'
    $xorig = Join-Path $srv 'xinput9_1_0_orig.dll'
    $stock = Join-Path $Pkg 'server\xinput9_1_0_stock.dll'
    $loader = Join-Path $Pkg 'server\xinput9_1_0.dll'
    $core = Join-Path $Pkg 'server\BotModCore.dll'
    $defaults = Join-Path $Pkg 'BotMod\enhanced.ini'
    if ($Mode -eq 'install') {
        foreach ($relativePath in $RequiredPackageFiles) {
            $file = Join-Path $Pkg $relativePath
            if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
                Fail "Package file missing: $file. Extract the entire ZIP. No files were replaced."
            }
        }
        if ((Get-FileHash -LiteralPath (Join-Path $srv $ExeName) -Algorithm SHA256).Hash -ne $ExeSha256) {
            Fail 'This is a different BF3 server build. The exact supported executable is required. No files were replaced.'
        }
        if ((Get-FileHash -LiteralPath $stock -Algorithm SHA256).Hash -ne $StockXinputSha256) {
            Fail 'The packaged stock XInput DLL failed verification. Extract the ZIP again. No files were replaced.'
        }
    } elseif (-not (Test-Path -LiteralPath $xorig -PathType Leaf) -and (Is-BotModLoader $xin)) {
        if (-not (Test-Path -LiteralPath $stock -PathType Leaf) -or
            (Get-FileHash -LiteralPath $stock -Algorithm SHA256).Hash -ne $StockXinputSha256) {
            Fail 'The saved original XInput DLL and verified fallback are missing. No files were replaced.'
        }
    }
    Assert-ServerStopped $srv
    Assert-Writable $srv $root
    $script:BackupDir = Join-Path $root ('BotMod_backup_' + (Get-Date -Format 'yyyy-MM-dd_HHmmss_fff') + '_' + [guid]::NewGuid().ToString('N').Substring(0, 6))
    if ($Mode -eq 'uninstall') {
        if (Test-Path -LiteralPath $xorig -PathType Leaf) {
            if (Is-BotModLoader $xorig) { Fail 'The saved original XInput DLL is a BotMod loader. No files were replaced.' }
            Backup $xin 'Battlefield_3_Server\xinput9_1_0.dll'
            Copy-Item -LiteralPath $xorig -Destination $xin -Force
        } elseif (Is-BotModLoader $xin) {
            Backup $xin 'Battlefield_3_Server\xinput9_1_0.dll'
            Copy-Item -LiteralPath $stock -Destination $xin -Force
        }
        foreach ($f in Get-ChildItem -LiteralPath $srv -Filter 'BotModCore*.dll' -File) {
            Backup $f.FullName (Join-Path 'Battlefield_3_Server' $f.Name)
            Remove-Item -LiteralPath $f.FullName -Force
        }
        Remove-CommandShortcuts
        Say 'AltBotMod removed - the server runs stock again. Your settings, loadouts, navigation, routes and what the bots learned (BotMod_learned) were kept.' Green
        if ($script:BackedUp) { Say "Backup: $($script:BackupDir)" Cyan }
        return
    }
    if (Test-Path -LiteralPath $xin -PathType Leaf) {
        if (-not (Is-BotModLoader $xin)) {
            Backup $xin 'Battlefield_3_Server\xinput9_1_0.dll'
            Backup $xorig 'Battlefield_3_Server\xinput9_1_0_orig.dll'
            Copy-Item -LiteralPath $xin -Destination $xorig -Force
        } elseif (-not (Same-File $loader $xin)) { Backup $xin 'Battlefield_3_Server\xinput9_1_0.dll' }
    }
    if (-not (Test-Path -LiteralPath $xorig -PathType Leaf) -or (Is-BotModLoader $xorig)) {
        Backup $xorig 'Battlefield_3_Server\xinput9_1_0_orig.dll'
        Copy-Item -LiteralPath $stock -Destination $xorig -Force
    }
    if (-not (Same-File $loader $xin)) { Copy-Item -LiteralPath $loader -Destination $xin -Force }
    Unblock-File -LiteralPath $xin -ErrorAction SilentlyContinue
    foreach ($f in Get-ChildItem -LiteralPath $srv -Filter 'BotModCore.loaded*.dll' -File) {
        Remove-Item -LiteralPath $f.FullName -Force
    }
    # AltBotMod V21: what the bots learned (BotMod_learned) is never replaced or removed - it carries over to every new
    # version. A copy goes into this install's backup first; V18-V20's BotMod_breakable.txt moves into the folder.
    $learnDir = Join-Path $srv 'BotMod_learned'
    $learnOld = Join-Path $srv 'BotMod_breakable.txt'
    Ensure-Directory $learnDir
    if ((Test-Path -LiteralPath $learnOld -PathType Leaf) -and -not (Test-Path -LiteralPath (Join-Path $learnDir 'breakable.txt'))) {
        Backup $learnOld 'Battlefield_3_Server\BotMod_breakable.txt'
        Move-Item -LiteralPath $learnOld -Destination (Join-Path $learnDir 'breakable.txt')
    }
    $learned = @(Get-ChildItem -LiteralPath $learnDir -File -ErrorAction SilentlyContinue)
    if ($learned.Count) {
        $copy = Join-Path $script:BackupDir 'Battlefield_3_Server\BotMod_learned'; Ensure-Directory $copy
        foreach ($f in $learned) { Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $copy $f.Name) -Force }
        $script:BackedUp++
        Say ("Learning kept: {0} map file(s) in {1} carry over (a copy is in the backup)." -f @($learned | Where-Object { $_.Extension -eq '.learn' }).Count, $learnDir) Green
    } else { Say "Learning: bots start learning in $learnDir (Server Control's Learning page shows it)." Cyan }
    $n = Copy-Set (Join-Path $Pkg 'server') $srv 'Battlefield_3_Server' @('xinput9_1_0.dll', 'xinput9_1_0_stock.dll')
    Say "Core + navigation: $n file(s) updated." Green
    # Fun-Bots recorded routes (one .map per map/mode) - always the packaged copies.
    $traceSrc = Join-Path $Pkg 'server\BotMod_traces'
    $n = Copy-Set $traceSrc (Join-Path $srv 'BotMod_traces') 'Battlefield_3_Server\BotMod_traces' @()
    # AltBotMod V19: one companion per map and mode - the level's other recordings within that mode's area
    $compSrc = Join-Path $traceSrc 'companions'
    if (Test-Path -LiteralPath $compSrc) { $n += Copy-Set $compSrc (Join-Path $srv 'BotMod_traces\companions') 'Battlefield_3_Server\BotMod_traces\companions' @() }
    # AltBotMod V34: Fun-Bots' objective walking mesh (one file per level and mode)
    $meshSrc = Join-Path $traceSrc 'navzones'
    if (Test-Path -LiteralPath $meshSrc) { $n += Copy-Set $meshSrc (Join-Path $srv 'BotMod_traces\navzones') 'Battlefield_3_Server\BotMod_traces\navzones' @() }
    Say "Fun-Bots routes: $n file(s) updated." Green
    $n = Copy-Set (Join-Path $Pkg 'BotMod') $botModDir 'BotMod' @('build.bat')
    # AltBotMod V9: Server Control (map / mode / rotation / bots for every server instance) in BotMod\ServerControl
    $scSrc = Join-Path $Pkg 'ServerControl'
    if (Test-Path -LiteralPath $scSrc) {
        $n = Copy-Set $scSrc (Join-Path $botModDir 'ServerControl') 'BotMod\ServerControl' @()
        Say "Server Control: $(Join-Path $botModDir 'ServerControl\Server-Control.bat')" Cyan
        # AltBotMod V12: a desktop shortcut that starts BF-Portable for BF3 in one click (Windows only; never fatal)
        try {
            if ($env:OS -eq 'Windows_NT' -and -not $env:ALTBOTMOD_NO_SHORTCUT) {
                $sh = New-Object -ComObject WScript.Shell
                $lnk = $sh.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'BF-Portable (AltBotMod).lnk'))
                $lnk.TargetPath = Join-Path $botModDir 'ServerControl\Start-BF-Portable.bat'
                $lnk.WorkingDirectory = Join-Path $botModDir 'ServerControl'
                $pbf = Join-Path (Split-Path -Parent $srv) 'PortableBF.exe'; if (Test-Path -LiteralPath $pbf) { $lnk.IconLocation = "$pbf,0" }
                $lnk.Save()
                Say "One-click start: the desktop shortcut 'BF-Portable (AltBotMod)'" Cyan
            }
        } catch { }
        # AltBotMod V33: AltBotMod Command - the web launcher (desktop window, browser or phone on the home network)
        Install-Command (Join-Path $botModDir 'ServerControl') $srv
    }
    [void](Repair-MaxPlayers $srv)
    $added = Add-MissingSettings $defaults $active
    $moved = Update-OldDefaults $active
    if ($moved) { Say "$moved setting(s) moved from an old default to the new one (JetAreaMarginMeters 350 / 600 -> 900)." Cyan }
    if ($FleetPreset) { Set-AllFleetPresets $active }
    if ($ObjectivePreset) { Set-AllObjectivePresets $active }
    Cache-Installer $botModDir
    if (-not (Same-File $loader $xin) -or -not (Same-File $core (Join-Path $srv 'BotModCore.dll')) -or
        -not (Test-Path -LiteralPath $active -PathType Leaf)) {
        Fail "Installation verification failed. See the backup at $($script:BackupDir)."
    }
    $note = @($Product,
        "Server: $srv", "ACTIVE settings: $active", "Core: $(Join-Path $srv 'BotModCore.dll')",
        "Loader: $xin", "Settings shortcut: $(Join-Path $botModDir 'Open-Settings.bat')",
        'Custom steering, speed, combat tuning and learned navigation are preserved.',
        "What the bots learned: $(Join-Path $srv 'BotMod_learned') (kept by every new version; Server Control's Learning page).",
        "Objective duties migration: $ObjectivePreset (aircraft/driving on; six pilots, at least sixteen ground drivers per team).",
        'Edit ACTIVE settings, then restart the server.',
        'In game, type !botdiag in chat to see the loaded build (it should say AltBotMod V34).') -join "`r`n"
    [IO.File]::WriteAllText((Join-Path $botModDir 'INSTALLATION.txt'), $note + "`r`n")
    Say ''
    Say 'Installed and verified.' Green
    Say "ACTIVE settings: $active" Cyan
    Say "$added missing option(s) added; existing values preserved." Gray
    Say "Open settings: $(Join-Path $botModDir 'Open-Settings.bat')" Cyan
    Say "Add your player name to $(Join-Path $botModDir 'admins.txt') for admin chat commands." Gray
    if ($script:BackedUp) { Say "Backup: $($script:BackupDir)" Cyan }
    Say 'Start the BF3 server from your normal launcher. The BotMod log (BotMod_<port>.log next to the server exe)' Green
    Say 'should show "enhanced: AltBotMod V34 - Squad Up"; in game, !botdiag shows it too.' Green
    if ($ObjectivePreset) { Say 'Objective duties enabled: aircraft/driving on; six pilots per team. Custom steering, speed and combat tuning preserved.' Yellow }
    elseif ($FleetPreset) { Say 'Full fleet enabled.' Yellow }
    else { Say 'Existing feature settings preserved. Enable-Full-Fleet.bat applies the fleet preset.' Yellow }
    if ($OpenSettings) { Open-ActiveSettings $active }
}
try { Invoke-BotModSetup }
catch { Say ''; Say "ERROR: $($_.Exception.Message)" Red; exit 1 }
exit 0
