param([string]$ServerDir,[ValidateRange(1024,65535)][int]$Port=8787,[switch]$LocalOnly,[switch]$Browser)
$a=@('-NoProfile','-ExecutionPolicy','Bypass','-STA','-File',('"'+(Join-Path $PSScriptRoot 'AltBotMod-Command.ps1')+'"'),'-Port',[string]$Port)
if ($ServerDir) { if ($ServerDir -match '"') { throw 'Invalid folder' }; $a+=@('-ServerDir',('"'+$ServerDir+'"')) }
if ($LocalOnly) { $a+='-LocalOnly' }
if (-not $Browser) { $a+='-Desktop' }
try { Start-Process powershell.exe -Verb RunAs -ArgumentList $a -WorkingDirectory $PSScriptRoot -WindowStyle Hidden } catch { Write-Host $_.Exception.Message; exit 1 }
