# Runs the actual installer functions against temporary files, not a live server.
# Only Windows process/UI calls and the fixture server EXE hash are mocked.
param([string]$PackageDir = (Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference = 'Stop'
$script:Checks = 0
$script:FakeRunning = $false
$script:OpenedSettings = $null
$script:FixtureExe = $null
$script:FixtureExeHash = $null

function Assert([bool]$ok, [string]$message) {
    if (-not $ok) { throw "FAIL: $message" }
    $script:Checks++
}
function Hash([string]$path) {
    return (Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
}
function Tree-Hashes([string]$root) {
    $items = @{}
    foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File) {
        $items[$file.FullName.Substring($root.Length)] = Hash $file.FullName
    }
    return ($items.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join "`n"
}
function Expect-Rejection([string]$message) {
    $before = Tree-Hashes $script:FixtureRoot
    $reason = $null
    try { Invoke-BotModSetup } catch { $reason = $_.Exception.Message }
    Assert ($null -ne $reason -and $reason.Contains($message)) "Expected rejection: $message; received: $reason"
    Assert ((Tree-Hashes $script:FixtureRoot) -ceq $before) 'Rejected install/uninstall leaves fixture files untouched'
}

$setupPath = Join-Path $PackageDir 'files/AltBotMod-Setup.ps1'
$tokens = $null; $errors = $null
$null = [Management.Automation.Language.Parser]::ParseFile($setupPath, [ref]$tokens, [ref]$errors)
Assert ($errors.Count -eq 0) 'Production installer parses without errors'
$source = [IO.File]::ReadAllText($setupPath)
$mainAt = $source.LastIndexOf('try { Invoke-BotModSetup }')
Assert ($mainAt -gt 0) 'Production installer entry point located'
# Load unchanged production declarations/functions; tests catch failures instead of exit 1.
$functionsFile = Join-Path ([IO.Path]::GetTempPath()) ('BotMod-functions-' + [guid]::NewGuid().ToString('N') + '.ps1')
[IO.File]::WriteAllText($functionsFile, $source.Substring(0, $mainAt))
try { . $functionsFile } finally { Remove-Item -LiteralPath $functionsFile -Force }

function Get-FileHash {
    [CmdletBinding()] param([string]$LiteralPath, [string]$Algorithm = 'SHA256')
    if ($LiteralPath -eq $script:FixtureExe) {
        return [pscustomobject]@{ Hash = $script:FixtureExeHash }
    }
    return Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath $LiteralPath -Algorithm $Algorithm
}
function Get-Process {
    [CmdletBinding()] param([string]$Name)
    if ($script:FakeRunning) { return [pscustomobject]@{ Path = $script:FixtureExe } }
}
function Read-Host { param([string]$Prompt) return 'n' }
function Unblock-File { [CmdletBinding()] param([string]$LiteralPath) }
function Start-Process {
    [CmdletBinding()] param([string]$FilePath, [string]$ArgumentList)
    Assert ($FilePath -eq 'notepad.exe') 'Settings mode only opens Notepad'
    $script:OpenedSettings = $ArgumentList.Trim('"')
}

$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('BotMod Installer Test ' + [guid]::NewGuid().ToString('N'))
try {
    $downloads = Join-Path $testRoot 'Downloads/Extracted Package'
    $script:FixtureRoot = Join-Path $testRoot 'BF-Portable'
    $srv = Join-Path $script:FixtureRoot 'Battlefield_3_Server'
    $bot = Join-Path $script:FixtureRoot 'BotMod'
    New-Item -ItemType Directory -Path $downloads, $srv, $bot -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $PackageDir 'files') -Destination $downloads -Recurse
    $script:Pkg = Join-Path $downloads 'files'
    $script:PkgRoot = $downloads
    $script:ServerDir = $srv
    $script:Mode = 'install'
    $script:FixtureExe = Join-Path $srv $ExeName
    $script:FixtureExeHash = $ExeSha256
    [IO.File]::WriteAllText($script:FixtureExe, 'Fixture only: not a game executable')
    $stockSource = Join-Path $script:Pkg 'server/xinput9_1_0_stock.dll'
    Copy-Item -LiteralPath $stockSource -Destination (Join-Path $srv 'xinput9_1_0.dll')
    $stockHash = Hash $stockSource
    $coreHash = Hash (Join-Path $script:Pkg 'server/BotModCore.dll')
    $loaderHash = Hash (Join-Path $script:Pkg 'server/xinput9_1_0.dll')
    [IO.File]::WriteAllText((Join-Path $srv 'BotModCore.dll'), 'Previous core fixture')
    $active = Join-Path $bot 'enhanced.ini'
    $existingSettings = "# User settings`nArmoredVehicles=1`nDriverWeapons=1`nJets=1`nHelicopters=1`nAircraftWeapons=1`nAirPilotsPerTeam=2`nJetAreaMarginMeters=600`n"
    [IO.File]::WriteAllText($active, $existingSettings)
    [IO.File]::WriteAllText((Join-Path $bot 'admins.txt'), 'TestPlayer')
    [IO.File]::WriteAllText((Join-Path $bot 'botcount.txt'), '32')
    [IO.File]::WriteAllText((Join-Path $srv 'BotMod_loadouts.txt'), 'User loadouts')
    $nav = Join-Path $srv 'BotMod_nav_MP_001.bin'
    [IO.File]::WriteAllText($nav, 'User learned navigation')
    $navHash = Hash $nav

    foreach ($path in @($srv, $script:FixtureRoot, $script:FixtureExe, $bot)) {
        Assert ((Resolve-ServerDir $path) -eq $srv) "Accept server, parent, EXE and BotMod paths: $path"
    }
    # Reproduce the reported failure, plus every other declared payload omission.
    foreach ($relative in $RequiredPackageFiles) {
        $file = Join-Path $script:Pkg $relative
        $bytes = [IO.File]::ReadAllBytes($file)
        Remove-Item -LiteralPath $file
        try { Expect-Rejection 'Package file missing:' }
        finally { [IO.File]::WriteAllBytes($file, $bytes) }
    }
    $script:FixtureExeHash = '0' * 64
    Expect-Rejection 'different BF3 server build'
    $script:FixtureExeHash = $ExeSha256
    $bytes = [IO.File]::ReadAllBytes($stockSource)
    [IO.File]::WriteAllText($stockSource, 'Damaged packaged stock XInput')
    try { Expect-Rejection 'stock XInput DLL failed verification' }
    finally { [IO.File]::WriteAllBytes($stockSource, $bytes) }
    $script:FakeRunning = $true
    Expect-Rejection 'server is still running'
    $script:FakeRunning = $false

    # AltBotMod V21: learning from an earlier version - a learned map file and V18-V20's BotMod_breakable.txt
    $learnDir = Join-Path $srv 'BotMod_learned'; New-Item -ItemType Directory -Path $learnDir -Force | Out-Null
    $learnFile = Join-Path $learnDir 'MP_001_ConquestLarge0.learn'
    [IO.File]::WriteAllText($learnFile, "AltBotMod learned`t1`tMP_001_ConquestLarge0`r`nspot`tfoot`t10.0 1.0 20.0`t1.00 0.00`t3 0 3 0`t1760000000 1760000100`r`n")
    $learnHash = Hash $learnFile
    [IO.File]::WriteAllText((Join-Path $srv 'BotMod_breakable.txt'), "0a0b0c0d b`r`n")
    # AltBotMod V34: a BF-Portable server set to 32 players (BF-Portable's Blaze drops everyone on any limit but 64)
    [IO.File]::WriteAllBytes((Join-Path (Split-Path -Parent $srv) 'PortableBF.exe'), [byte[]](1, 2))
    $st3 = Join-Path $srv 'Instance3/Admin/Startup.txt'; $st1 = Join-Path $srv 'Instance/Admin/Startup.txt'
    [void](New-Item -ItemType Directory -Force -Path (Split-Path -Parent $st3), (Split-Path -Parent $st1))
    [IO.File]::WriteAllText($st3, "admin.password fixture-value`r`nvars.serverName `"Test`"`r`nvars.maxPlayers 32`r`nvars.gameModeCounter 100`r`n")
    [IO.File]::WriteAllText($st1, "vars.maxPlayers 64`r`nvars.friendlyFire false`r`n"); $st1Hash = Hash $st1
    $bat3 = Join-Path $srv '_StartServer3.bat'; [IO.File]::WriteAllText($bat3, "set GAMEPORT=25302`r`nset PLAYERS=32`r`nstart bf3_server_Final.exe -maxPlayers %PLAYERS%`r`n")

    Invoke-BotModSetup
    Assert ((Hash (Join-Path $srv 'BotModCore.dll')) -eq $coreHash) 'Test 13 core installed'
    Assert ([IO.File]::ReadAllText($st3) -ceq "admin.password fixture-value`r`nvars.serverName `"Test`"`r`nvars.maxPlayers 64`r`nvars.gameModeCounter 100`r`n") 'AltBotMod V34: a BF-Portable server at 32 players is put back to 64, nothing else in Startup.txt changes'
    Assert ((Hash $st1) -eq $st1Hash) 'AltBotMod V34: a server already at 64 players is left untouched'
    Assert ([IO.File]::ReadAllText($bat3) -ceq "set GAMEPORT=25302`r`nset PLAYERS=64`r`nstart bf3_server_Final.exe -maxPlayers %PLAYERS%`r`n") 'AltBotMod V34: its _StartServer3.bat PLAYERS=32 is put back to 64'
    Assert ((Hash (Join-Path $srv 'xinput9_1_0.dll')) -eq $loaderHash) 'Loader installed'
    Assert ((Hash (Join-Path $srv 'xinput9_1_0_orig.dll')) -eq $stockHash) 'Original XInput retained'
    Assert ([IO.File]::ReadAllText($active).StartsWith($existingSettings.Replace('JetAreaMarginMeters=600', 'JetAreaMarginMeters=900'))) 'Existing enabled settings preserved (V34: the old jet-area default moved)'
    Assert ([IO.File]::ReadAllText($active).Contains('ReturnToBase=1')) 'Missing deployment return setting added'
    Assert ([IO.File]::ReadAllText($active).Contains('PassengerTakeover=1')) 'Missing takeover setting added'
    Assert ([IO.File]::ReadAllText((Join-Path $bot 'admins.txt')) -eq 'TestPlayer') 'Admins preserved'
    Assert ([IO.File]::ReadAllText((Join-Path $bot 'botcount.txt')) -eq '32') 'Bot count preserved'
    Assert ([IO.File]::ReadAllText((Join-Path $srv 'BotMod_loadouts.txt')) -eq 'User loadouts') 'Loadouts preserved'
    Assert ((Hash $nav) -eq $navHash) 'Learned navigation preserved'
    Assert ((Hash (Join-Path $bot 'Open-Settings.bat')) -eq (Hash (Join-Path $script:Pkg 'BotMod/Open-Settings.bat'))) 'Settings shortcut installed'
    $cache = Join-Path $bot 'installer'
    foreach ($relative in $RequiredPackageFiles) {
        Assert (Test-Path -LiteralPath (Join-Path $cache $relative) -PathType Leaf) "Cached installer has $relative"
    }
    Assert ((Hash (Join-Path $cache 'AltBotMod-Setup.ps1')) -eq (Hash $setupPath)) 'Cached installer matches corrected source'
    $traces = @(Get-ChildItem -LiteralPath (Join-Path $srv 'BotMod_traces') -Filter '*.map' -File)
    Assert ($traces.Count -eq @(Get-ChildItem -LiteralPath (Join-Path $script:Pkg 'server/BotMod_traces') -Filter '*.map' -File).Count) 'AltBotMod: every Fun-Bots route file is installed'
    $meshes = @(Get-ChildItem -LiteralPath (Join-Path $srv 'BotMod_traces/navzones') -Filter '*.zon' -File -ErrorAction SilentlyContinue)
    $meshSrc = @(Get-ChildItem -LiteralPath (Join-Path $script:Pkg 'server/BotMod_traces/navzones') -Filter '*.zon' -File)
    Assert ($meshSrc.Count -ge 90 -and $meshes.Count -eq $meshSrc.Count -and (Hash (Join-Path $srv 'BotMod_traces/navzones/MP_001_RushLarge0.zon')) -eq (Hash (Join-Path $script:Pkg 'server/BotMod_traces/navzones/MP_001_RushLarge0.zon'))) "AltBotMod V34: every Fun-Bots objective mesh is installed ($($meshes.Count))"
    $activeText = [IO.File]::ReadAllText($active)
    Assert ($activeText -match '(?m)^JetAreaMarginMeters=900\s*$' -and $activeText -notmatch '(?m)^JetAreaMarginMeters=600') 'AltBotMod V34: JetAreaMarginMeters at the old default 600 moved to 900'
    Assert ($activeText -match '(?m)^FunBotsMesh=1\s*$' -and $activeText -match '(?m)^VehicleWalkMeters=250\s*$') 'AltBotMod V34: FunBotsMesh / VehicleWalkMeters added to the active settings'
    Assert ([IO.File]::ReadAllText($active).Contains('BotDamage=')) 'AltBotMod: BotDamage setting present after install'
    Assert ([IO.File]::ReadAllText((Join-Path $bot 'INSTALLATION.txt')).Contains('AltBotMod V34')) 'Installed note identifies AltBotMod V34'
    Assert ((Hash $learnFile) -eq $learnHash) 'AltBotMod V21: what the bots learned carries over unchanged'
    Assert ((Test-Path -LiteralPath (Join-Path $learnDir 'breakable.txt')) -and -not (Test-Path -LiteralPath (Join-Path $srv 'BotMod_breakable.txt'))) 'AltBotMod V21: the old BotMod_breakable.txt moved into BotMod_learned'
    Assert (@(Get-ChildItem -LiteralPath $script:FixtureRoot -Recurse -File -Filter 'MP_001_ConquestLarge0.learn' | Where-Object { $_.FullName -match 'BotMod_backup_' }).Count -ge 1) 'AltBotMod V21: a copy of the learning goes into the backup'
    Assert ([IO.File]::ReadAllText($active).Contains('Learning=1') -and [IO.File]::ReadAllText($active).Contains('LearnFromPlayers=1')) 'AltBotMod V21: the learning settings added to the active settings (on)'
    Assert ((Hash (Join-Path $bot 'ServerControl/Learning-Core.ps1')) -eq (Hash (Join-Path $script:Pkg 'ServerControl/Learning-Core.ps1')) -and (Hash (Join-Path $bot 'ServerControl/Learning-Page.ps1')) -eq (Hash (Join-Path $script:Pkg 'ServerControl/Learning-Page.ps1'))) 'AltBotMod V21: the Learning page and its core installed with Server Control'
    Assert ([IO.File]::ReadAllText((Join-Path $bot 'INSTALLATION.txt')).Contains('BotMod_learned')) 'AltBotMod V21: the installed note says where the learning is'
    Assert ([IO.File]::ReadAllText($active).Contains('SideSteps=1')) 'AltBotMod V14: the side-step setting added to the active settings'
    Assert ([IO.File]::ReadAllText($active).Contains('RouteActions=1') -and [IO.File]::ReadAllText($active).Contains('ReconBeacons=1')) 'AltBotMod V15: the route-action and beacon settings added to the active settings'
    Assert ([IO.File]::ReadAllText($active).Contains('GadgetVariety=1') -and [IO.File]::ReadAllText($active).Contains('C4Attacks=1') -and [IO.File]::ReadAllText($active).Contains('LockOnLaunchers=1') -and [IO.File]::ReadAllText($active).Contains('BotTicketCost=0.5') -and [IO.File]::ReadAllText($active).Contains('VehicleCountermeasures=1') -and [IO.File]::ReadAllText($active).Contains('VehicleDamageTaken=1') -and [IO.File]::ReadAllText($active).Contains('VehicleDamageDealt=1')) 'AltBotMod V16.2: the gadget, C4, lock-on, ticket, smoke and vehicle damage settings added to the active settings'
    Assert (@(Get-ChildItem -LiteralPath (Join-Path $srv 'BotMod_traces') -Filter '*.map').Count -ge 176 -and (Test-Path -LiteralPath (Join-Path $srv 'BotMod_traces/MP_007_RushLarge0.map'))) 'AltBotMod V15: every recorded route file installed (Rush, Squad Rush, Squad DM, Domination, Gun Master ...)'
    Assert ((Hash (Join-Path $bot 'ServerControl/BFPortable-Core.ps1')) -eq (Hash (Join-Path $script:Pkg 'ServerControl/BFPortable-Core.ps1')) -and ([IO.File]::ReadAllText((Join-Path $bot 'ServerControl/BFPortable-Core.ps1'))).Contains('function Read-BpConfig')) 'AltBotMod V14: launcher settings installed with Server Control'
    Assert ([IO.File]::ReadAllText((Join-Path $bot 'Collect-Diagnostics.ps1')).Contains("Launcher\.(log|json)")) 'AltBotMod V14: diagnostics also collect the launcher log and settings'
    $lm = [IO.File]::ReadAllText($active)
    Assert ($lm.Contains('LiveMap=1') -and $lm.Contains('LiveMapSeconds=') -and $lm.Contains('LiveMapTrailSeconds=') -and $lm.Contains('LiveMapKeepMB=')) 'AltBotMod V13: the four live map settings added to the active settings (on)'
    Assert ((Hash (Join-Path $bot 'ServerControl/LiveMap-Core.ps1')) -eq (Hash (Join-Path $script:Pkg 'ServerControl/LiveMap-Core.ps1')) -and (Hash (Join-Path $bot 'ServerControl/LiveMap-Page.ps1')) -eq (Hash (Join-Path $script:Pkg 'ServerControl/LiveMap-Page.ps1'))) 'AltBotMod V13: the Live map page and its core installed with Server Control'
    Assert ([IO.File]::ReadAllText((Join-Path $bot 'Collect-Diagnostics.ps1')).Contains("(Join-Path `$bot 'LiveMap')")) 'AltBotMod V13: diagnostics also collect the live map files'
    Assert (Test-Path -LiteralPath (Join-Path $bot 'ServerControl/Server-Control.bat') -PathType Leaf) 'AltBotMod V9: Server Control installed in BotMod\ServerControl'
    Assert (Test-Path -LiteralPath (Join-Path $bot 'ServerControl/ServerControl-Core.ps1') -PathType Leaf) 'AltBotMod V9: Server Control core installed'
    Assert ((Hash (Join-Path $bot 'ServerControl/ServerControl-Theme.ps1')) -eq (Hash (Join-Path $script:Pkg 'ServerControl/ServerControl-Theme.ps1'))) 'AltBotMod V16.2: the BF3 look (ServerControl-Theme.ps1) installed with Server Control'
    Assert ([IO.File]::ReadAllText($active).Contains('Difficulty=')) 'AltBotMod V9: Difficulty setting added to the active settings'
    Assert (@(Get-ChildItem -LiteralPath (Join-Path $srv 'BotMod_traces') -Filter '*_TeamDeathMatch0.map' -File).Count -ge 29) 'AltBotMod V9: every TDM route file installed (29 maps, V18.2: + the co-op / campaign levels)'
    $compPkg = @(Get-ChildItem -LiteralPath (Join-Path $script:Pkg 'server/BotMod_traces/companions') -Filter '*.map' -File)
    $compSrv = @(Get-ChildItem -LiteralPath (Join-Path $srv 'BotMod_traces/companions') -Filter '*.map' -File -ErrorAction SilentlyContinue)
    Assert ($compPkg.Count -ge 200 -and $compSrv.Count -eq $compPkg.Count -and (Hash (Join-Path $srv 'BotMod_traces/companions/XP2_Office_TeamDeathMatchC0.map')) -eq (Hash (Join-Path $script:Pkg 'server/BotMod_traces/companions/XP2_Office_TeamDeathMatchC0.map'))) 'AltBotMod V19: every route companion installed (BotMod_traces\companions, one per map and mode)'
    Assert ([IO.File]::ReadAllText($active).Contains('SniperEngageMeters=') -and [IO.File]::ReadAllText($active).Contains('SniperOverwatch=')) 'AltBotMod V10: sniper settings added to the active settings'
    Assert ([IO.File]::ReadAllText($active).Contains('ClassSupport=') -and [IO.File]::ReadAllText($active).Contains('SupplyMeters=') -and [IO.File]::ReadAllText($active).Contains('SniperPairs=')) 'AltBotMod V11: class and sniper-pair settings added to the active settings'
    Assert ((Test-Path -LiteralPath (Join-Path $bot 'ServerControl/Start-BF-Portable.bat') -PathType Leaf) -and (Hash (Join-Path $bot 'ServerControl/BFPortable-Core.ps1')) -eq (Hash (Join-Path $script:Pkg 'ServerControl/BFPortable-Core.ps1'))) 'AltBotMod V12: one-click start and the BF-Portable core installed with Server Control'
    Assert ((Hash (Join-Path $bot 'ServerControl/ServerControl-Core.ps1')) -eq (Hash (Join-Path $script:Pkg 'ServerControl/ServerControl-Core.ps1'))) 'AltBotMod V10: Server Control core updated'
    $a = [IO.File]::ReadAllText($active)
    Assert ($a.Contains('SniperPairs=') -and $a.Contains('SupplyMeters=') -and $a.Contains('ClassSupport=')) 'AltBotMod V11: pair / supply / class settings added to the active settings'
    Assert ((Hash (Join-Path $bot 'ServerControl/AltBotMod-ServerControl.ps1')) -eq (Hash (Join-Path $script:Pkg 'ServerControl/AltBotMod-ServerControl.ps1'))) 'AltBotMod V11: Server Control window (settings dialog) updated'
    # AltBotMod V34: AltBotMod Command (web launcher) installed beside Server Control, web folders included, with its shortcuts
    $cmdSrc = Join-Path $script:Pkg 'ServerControl'
    $cmdFiles = @(Get-ChildItem -LiteralPath $cmdSrc -Recurse -File | Where-Object { $_.Name -like 'Command*' -or $_.Name -eq 'AltBotMod-Command.ps1' -or $_.FullName -match '[\\/]Command-Web[\\/]' })
    Assert ($cmdFiles.Count -ge 50) "AltBotMod V34: the package carries AltBotMod Command ($($cmdFiles.Count) files)"
    $cmdBad = @($cmdFiles | Where-Object { $rel = $_.FullName.Substring($cmdSrc.Length).TrimStart([char[]]'\/'); (Hash (Join-Path (Join-Path $bot 'ServerControl') $rel)) -ne (Hash $_.FullName) })
    Assert (-not $cmdBad.Count) "AltBotMod V34: every Command file installed unchanged, art and maps included (wrong: $(@($cmdBad | ForEach-Object Name) -join ', '))"
    Assert ([IO.File]::ReadAllText((Join-Path $bot 'ServerControl/Command-last-folder.txt')) -eq $srv) 'AltBotMod V34: Command opens on the server folder the installer used'
    $setupText = [IO.File]::ReadAllText($setupPath)
    Assert ($setupText.Contains("'AltBotMod Command.lnk'") -and $setupText.Contains("'Command-Desktop.bat'") -and $setupText.Contains("GetFolderPath('Programs')") -and $setupText.Contains('Install-Command (Join-Path $botModDir') -and $setupText.Contains('Remove-CommandShortcuts')) 'AltBotMod V34: desktop / Start menu shortcuts made on install, removed on uninstall'
    Assert (@(Get-ChildItem -LiteralPath $script:FixtureRoot -Directory -Filter 'BotMod_backup_*').Count -gt 0) 'Previous core and settings backed up'

    # Reproduce V3 disabled features and verify Test 4 preset plus first-backup preservation.
    $presetOriginal="# custom tuning`r`nArmoredVehicles=0`r`nJets=0`r`nHelicopters=0`r`nTankSpeedMps=7`r`nGroundDriversPerTeam=3`r`nAirPilotsPerTeam=4`r`n"
    [IO.File]::WriteAllText($active,$presetOriginal)
    $portActive=Join-Path $bot 'enhanced_25200.ini'
    $portOriginal="Driving=0`r`nArmoredVehicles=0`r`nHelicopters=0`r`nReserveHumanMeters=15`r`nTankSpeedMps=9`r`n"
    [IO.File]::WriteAllText($portActive,$portOriginal)
    $customPort=Join-Path $bot 'enhanced_25201.ini'
    [IO.File]::WriteAllText($customPort,"Driving=0`r`nReserveHumanMeters=23`r`nGroundDriversPerTeam=5`r`nAirPilotsPerTeam=8`r`nTankSpeedMps=20`r`nInfantryEngageMeters=210`r`n")
    $script:FleetPreset=$false
    Invoke-BotModSetup
    Assert ([IO.File]::ReadAllText($active).StartsWith($presetOriginal)) 'Ordinary install preserves disabled features, custom speed and class limits'
    Assert ([IO.File]::ReadAllText($portActive) -ceq $portOriginal) 'Ordinary install preserves port override byte-for-byte'
    [IO.File]::WriteAllText($active,$presetOriginal)
    $script:FleetPreset=$true
    Invoke-BotModSetup
    $preset=[IO.File]::ReadAllText($active)
    foreach ($key in @('Driving','ArmoredVehicles','DriverWeapons','Jets','Helicopters','AircraftWeapons','TransportDrops','ReturnToBase','PassengerTakeover','DriveInStages','TransportLandings','LearnAircraftDeployment','WeaponHandling','ArmorTactics','ArmorInfantrySupport','TrafficManagement','VehicleDiagnostics')) {
        Assert ($preset -match "(?m)^$key=1\r?$") "Preset enables $key"
    }
    Assert ($preset.Contains('TankSpeedMps=16')) 'Requested mobility preset raises legacy tank speed'
    Assert ($preset.Contains('VehicleReturnMeters=3000')) 'Vehicle deployment selection range raised to 3000m'
    Assert ($preset.Contains('GroundDriversPerTeam=16')) 'Preset raises legacy ground driver count to sixteen'
    Assert ($preset.Contains('AirPilotsPerTeam=6')) 'Preset raises legacy pilot count to six'
    $portSettings=[IO.File]::ReadAllText($portActive)
    foreach ($key in @('Driving','ArmoredVehicles','Helicopters','Jets','ReturnToBase')) {
        Assert ($portSettings -match "(?m)^$key=1\r?$") "Port override enables $key"
    }
    Assert ($portSettings.Contains('ReserveHumanMeters=5')) 'Old default reservation radius reduced at spawn'
    Assert ($portSettings.Contains('TankSpeedMps=16')) 'Port-specific legacy speed upgraded'
    Assert ([IO.File]::ReadAllText((Join-Path $script:BackupDir 'BotMod/enhanced_25200.ini')) -ceq $portOriginal) 'Port override backed up separately'
    Assert ([IO.File]::ReadAllText($customPort).Contains('ReserveHumanMeters=23')) 'Custom human reservation radius preserved'
    Assert ([IO.File]::ReadAllText($customPort).Contains('VehicleReturnMeters=3000')) 'Per-port vehicle range raised'
    Assert ([IO.File]::ReadAllText($customPort).Contains('GroundDriversPerTeam=16')) 'Per-port driver limit raised to sixteen'
    Assert ([IO.File]::ReadAllText($customPort).Contains('AirPilotsPerTeam=6')) 'Per-port eight-pilot override capped at six'
    Assert ([IO.File]::ReadAllText((Join-Path $script:BackupDir 'BotMod/enhanced.ini')) -ceq $presetOriginal) 'First backup preserves original before missing-option migration and preset'
    Assert ([IO.File]::ReadAllText($customPort).Contains('TankSpeedMps=20')) 'Higher custom speed preserved'
    Assert ([IO.File]::ReadAllText($customPort).Contains('InfantryEngageMeters=210')) 'Custom combat range preserved'
    foreach ($setting in @('LightSpeedMps=34','BoatSpeedMps=22','IfvSpeedMps=20','ArmorRepairHealthFraction=0.35','ArmorTacticMeters=70','ConvoySpacingMeters=14','InfantryEngageMeters=260','ReconEngageMeters=450','ArmorEngageMeters=800','WeaponVariety=1','WeaponAttachments=1')) {
        Assert ($preset.Contains($setting)) "Mobility/combat defaults installed: $setting"
    }
    $presetHash=Hash $active
    Set-FleetPreset $active
    Assert ((Hash $active) -eq $presetHash) 'Fleet migration is idempotent'
    $script:FleetPreset=$false

    # New installation preset enables duties while preserving custom control/combat values.
    $objectiveDir=Join-Path $testRoot 'ObjectivePresetTest'
    New-Item -ItemType Directory -Path $objectiveDir | Out-Null
    $objectiveFile=Join-Path $objectiveDir 'enhanced.ini'
    $objectivePort=Join-Path $objectiveDir 'enhanced_25200.ini'
    $objectiveOriginal="# custom`r`nJets=0`r`nHelicopters=0`r`nAirPilotsPerTeam=2`r`nGroundDriversPerTeam=3`r`nSteerKp=1.23`r`nSteerKd=0.07`r`nTankSpeedMps=9`r`nInfantryEngageMeters=213`r`n"
    [IO.File]::WriteAllText($objectiveFile,$objectiveOriginal)
    [IO.File]::WriteAllText($objectivePort,"Driving=0`r`nHelicopters=0`r`nAirPilotsPerTeam=20`r`nTankSpeedMps=8`r`nVehicleReturnMeters=1600`r`n")
    Set-AllObjectivePresets $objectiveFile
    $objective=[IO.File]::ReadAllText($objectiveFile)
    foreach ($key in @('Driving','Jets','Helicopters','AircraftWeapons','ReturnToBase','PassengerTakeover')) {
        Assert ($objective -match "(?m)^$key=1\r?$") "Objective preset enables $key"
    }
    foreach ($entry in @('AirPilotsPerTeam=6','GroundDriversPerTeam=16','SteerKp=1.23','SteerKd=0.07','TankSpeedMps=9','InfantryEngageMeters=213')) {
        Assert ($objective.Contains($entry)) "Objective preset preserves or sets $entry"
    }
    $port=[IO.File]::ReadAllText($objectivePort)
    Assert ($port.Contains('Helicopters=1') -and $port.Contains('AirPilotsPerTeam=6') -and $port.Contains('TankSpeedMps=8') -and $port.Contains('VehicleReturnMeters=3000')) 'Objective preset migrates port override while preserving speed'
    $objectiveHash=Hash $objectiveFile;Set-AllObjectivePresets $objectiveFile
    Assert ((Hash $objectiveFile) -eq $objectiveHash) 'Objective preset is idempotent'
    Assert ([IO.File]::ReadAllText((Join-Path $PackageDir 'Install-AltBotMod.bat')).Contains('-ObjectivePreset')) 'Main installer invokes objective preset'

    # Installed shortcuts must remain functional after the extracted ZIP is removed.
    Remove-Item -LiteralPath $downloads -Recurse -Force
    $script:Pkg = $cache
    $script:PkgRoot = $bot
    $script:BackupDir = $null; $script:BackedUp = 0
    $settingsHash = Hash $active
    [IO.File]::WriteAllText((Join-Path $srv 'BotModCore.dll'), 'Old cached-upgrade fixture')
    Invoke-BotModSetup
    Assert ((Hash (Join-Path $srv 'BotModCore.dll')) -eq $coreHash) 'Cached installer can reinstall without original ZIP'
    Assert ((Hash $active) -eq $settingsHash) 'Repeated install preserves settings without duplicate options'

    $script:Mode = 'settings'
    Invoke-BotModSetup
    Assert ($script:OpenedSettings -eq $active) 'Settings mode selects active sibling config, not cached defaults'
    $script:Mode = 'uninstall'
    $script:FakeRunning = $true
    Expect-Rejection 'server is still running'
    $script:FakeRunning = $false
    $script:BackupDir = $null; $script:BackedUp = 0
    [IO.File]::WriteAllText((Join-Path $srv 'BotModCore.loaded123.dll'), 'Loaded core fixture')
    Invoke-BotModSetup
    Assert ((Hash (Join-Path $srv 'xinput9_1_0.dll')) -eq $stockHash) 'Uninstall restores original XInput'
    Assert (@(Get-ChildItem -LiteralPath $srv -File -Filter 'BotModCore*.dll').Count -eq 0) 'Uninstall removes core copies'
    Assert ((Hash $active) -eq $settingsHash) 'Uninstall preserves active settings'
    Assert ((Hash $nav) -eq $navHash) 'Uninstall preserves learned navigation'
    Assert ((Hash $learnFile) -eq $learnHash -and (Test-Path -LiteralPath (Join-Path $learnDir 'breakable.txt'))) 'AltBotMod V21: uninstall keeps what the bots learned'
    Write-Host "PASS: $($script:Checks) installer integration checks. Real file operations; fixture EXE hash and Windows UI/process calls mocked."
} finally {
    if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}
