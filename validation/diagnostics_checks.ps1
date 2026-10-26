# Actual read-only collector with temporary files and a mocked process query.
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent;$script:checks=0
function Assert([bool]$ok,[string]$why){if(-not $ok){throw "FAIL: $why"};$script:checks++}
$file=Join-Path $root 'files/BotMod/Collect-Diagnostics.ps1'
$tokens=$null;$errors=$null;$null=[Management.Automation.Language.Parser]::ParseFile($file,[ref]$tokens,[ref]$errors)
Assert ($errors.Count -eq 0) 'Production collector parses'
$source=[IO.File]::ReadAllText($file);$at=$source.LastIndexOf('try {Start-DiagnosticCollection}')
$tmp=Join-Path ([IO.Path]::GetTempPath()) ('bf3-diagnostic-tests-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $tmp
$functions=Join-Path $tmp 'functions.ps1';[IO.File]::WriteAllText($functions,$source.Substring(0,$at));. $functions
try {
 $srv=Join-Path $tmp 'BF-Portable/Battlefield_3_Server';$bot=Join-Path $tmp 'BF-Portable/BotMod';$logs=Join-Path $srv 'Instances/one/Logs';$custom=Join-Path $tmp 'Live Instance/Logs'
 $null=New-Item -ItemType Directory -Path $srv,$bot,$logs,$custom -Force
 [IO.File]::WriteAllText((Join-Path $srv 'bf3_server_Final.exe'),'fixture');[IO.File]::WriteAllText((Join-Path $srv 'BotModCore.dll'),'fixture DLL')
 $active=Join-Path $bot 'enhanced.ini';[IO.File]::WriteAllText($active,"GroundDriversPerTeam=8`n# custom")
 $original=(Get-FileHash -LiteralPath $active).Hash
 $live=Join-Path $bot 'BotMod_25200.log';[IO.File]::WriteAllText($live,'locked live log')
 $lock=[IO.File]::Open($live,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::ReadWrite)
 [IO.File]::WriteAllText((Join-Path $bot 'diagnostics_25200.txt'),'bot decisions')
 [IO.File]::WriteAllText((Join-Path $bot 'admins.txt'),'do not collect');[IO.File]::WriteAllText((Join-Path $bot 'Blaze.log'),'do not collect credentials')
 $old=Join-Path $logs 'old.dmp';$new=Join-Path $custom 'new.dmp'
 [IO.File]::WriteAllText($old,'old fixture');[IO.File]::WriteAllText($new,'latest fixture');(Get-Item $old).LastWriteTimeUtc=[DateTime]::UtcNow.AddDays(-1)
 $script:fixtureProcess=[pscustomobject]@{ExecutablePath=(Join-Path $srv 'bf3_server_Final.exe');CommandLine=('game.exe -password secret -serverInstancePath "'+(Split-Path $custom -Parent)+'" -token private')}
 function Get-CimInstance {param($ClassName,$Filter,$ErrorAction);return $script:fixtureProcess}
 Assert ((Get-DiagnosticInstancePath $script:fixtureProcess.CommandLine) -eq (Split-Path $custom -Parent)) 'Quoted instance path parsed without credentials'
 Assert ((Get-DiagnosticInstancePath '-serverInstancePath D:\instance -x') -eq 'D:\instance') 'Unquoted path parsed'
 Assert (-not (Get-DiagnosticInstancePath '-password secret')) 'No unrelated commandline parsing'
 Assert ((Resolve-DiagnosticServer $bot) -eq $srv) 'Sibling BotMod resolves actual server'
 # Header layout matches native packed NAV5 (60 bytes). No grid is copied.
 $nav=Join-Path $srv 'BotMod_nav_fixture.bin';$stream=[IO.File]::OpenWrite($nav);$writer=New-Object IO.BinaryWriter($stream)
 $writer.Write([uint32]0x3541564e);$writer.Write((New-Object byte[] 32));$writer.Write([single]-10);$writer.Write([single]-30);$writer.Write([single]2);$writer.Write([int]10);$writer.Write([int]20);$writer.Write([int]6);$writer.Dispose()
 Assert ((Read-NavHeader (Get-Item $nav)).Contains('bounds_z=-30..10')) 'Navigation coverage header reported'
 Assert ((Read-NavHeader (Get-Item (Join-Path $root 'files/server/BotMod_nav_XP3_Desert.bin'))).Contains('dimensions=624x612')) 'Real packaged NAV5 header layout reported accurately'
 try {$zip=Invoke-DiagnosticCollection $srv (Join-Path $tmp 'output')}finally{$lock.Dispose()}
 Assert (Test-Path -LiteralPath $zip) 'Archive created'
 Add-Type -AssemblyName System.IO.Compression.FileSystem
 $archive=[IO.Compression.ZipFile]::OpenRead($zip)
 try {
  $names=@($archive.Entries | ForEach-Object {$_.FullName})
  Assert (@($names | Where-Object {$_ -like 'crash/*'}).Count -eq 1) 'Only latest native dump collected'
  Assert ($names -contains 'crash/new.dmp') 'Live instance dump selected'
  Assert (@($names | Where-Object {$_ -like '*BotMod_25200.log'}).Count -eq 1) 'Locked live log collected'
  Assert (@($names | Where-Object {$_ -like '*enhanced.ini'}).Count -eq 1) 'Active settings collected'
  Assert (@($names | Where-Object {$_ -like '*diagnostics_25200.txt'}).Count -eq 1) 'Bot decision snapshot collected'
  Assert (-not @($names | Where-Object {$_ -match 'admins|Blaze|\.bin$'}).Count) 'Unrelated files and nav payload excluded'
  $reader=New-Object IO.StreamReader($archive.GetEntry('REPORT.txt').Open());try{$report=$reader.ReadToEnd()}finally{$reader.Dispose()}
  Assert ($report.Contains('bounds_z=-30..10') -and $report.Contains('SHA256')) 'Coverage and binary hashes included'
  Assert (-not $report.Contains('secret') -and -not $report.Contains('private')) 'Process credentials absent'
 }finally{$archive.Dispose()}
 Assert ((Get-FileHash -LiteralPath $active).Hash -eq $original) 'Custom settings untouched'
 Write-Host "PASS: $($script:checks) actual diagnostic collector checks, including locked log, latest dump, nav header, bounded directory search and unchanged settings"
}finally{Remove-Item -LiteralPath $tmp -Recurse -Force}
