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

$setupPath = Join-Path $PackageDir 'files/BotMod-Setup.ps1'
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
    $existingSettings = "# User settings`nArmoredVehicles=1`nDriverWeapons=1`nJets=1`nHelicopters=1`nAircraftWeapons=1`nAirPilotsPerTeam=2`n"
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

    Invoke-BotModSetup
    Assert ((Hash (Join-Path $srv 'BotModCore.dll')) -eq $coreHash) 'Test 13 core installed'
    Assert ((Hash (Join-Path $srv 'xinput9_1_0.dll')) -eq $loaderHash) 'Loader installed'
    Assert ((Hash (Join-Path $srv 'xinput9_1_0_orig.dll')) -eq $stockHash) 'Original XInput retained'
    Assert ([IO.File]::ReadAllText($active).StartsWith($existingSettings)) 'Existing enabled settings preserved'
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
    Assert ((Hash (Join-Path $cache 'BotMod-Setup.ps1')) -eq (Hash $setupPath)) 'Cached installer matches corrected source'
    Assert ([IO.File]::ReadAllText((Join-Path $bot 'INSTALLATION.txt')).Contains('Enhanced Test 15')) 'Installed note identifies Test 15'
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
    foreach ($setting in @('LightSpeedMps=34','BoatSpeedMps=22','IfvSpeedMps=20','ArmorRepairHealthFraction=0.35','ArmorTacticMeters=70','ConvoySpacingMeters=14','InfantryEngageMeters=180','ReconEngageMeters=320','ArmorEngageMeters=650','WeaponVariety=1','WeaponAttachments=1')) {
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
    Assert ([IO.File]::ReadAllText((Join-Path $PackageDir 'Install-BotMod.bat')).Contains('-ObjectivePreset')) 'Main installer invokes objective preset'

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
    Write-Host "PASS: $($script:Checks) installer integration checks. Real file operations; fixture EXE hash and Windows UI/process calls mocked."
} finally {
    if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}
