# Run once on the host PC if the phone cannot connect. This does not start the launcher.
param([ValidateRange(1024,65535)][int]$Port=8787,[switch]$Remove)
$ErrorActionPreference='Stop'
$name="AltBotMod Command LAN $Port"
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $a=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),'-Port',[string]$Port)
    if ($Remove) { $a+='-Remove' }
    Start-Process powershell.exe -Verb RunAs -ArgumentList $a -Wait
    exit
}
try {
    if ($Remove) { Get-NetFirewallRule -DisplayName $name -ErrorAction SilentlyContinue | Remove-NetFirewallRule; Write-Host "Removed: $name" }
    elseif (-not (Get-NetFirewallRule -DisplayName $name -ErrorAction SilentlyContinue)) {
        New-NetFirewallRule -DisplayName $name -Direction Inbound -Action Allow -Protocol TCP -LocalPort $Port -Profile Private -RemoteAddress LocalSubnet | Out-Null
        Write-Host "Added $name for Private networks on your local subnet."
    } else { Write-Host 'The Private LAN firewall rule already exists.' }
    Write-Host 'Your PC network must be marked Private. No public-network or router port forwarding is needed.'
} catch { Write-Host $_.Exception.Message -ForegroundColor Red }
Read-Host 'Press Enter to close'
