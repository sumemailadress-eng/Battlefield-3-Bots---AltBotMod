@echo off
:: AltBotMod Command: lets phones / tablets on your home network reach Command (adds one Private-network firewall rule).
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Command-Firewall.ps1" %*
if errorlevel 1 pause
