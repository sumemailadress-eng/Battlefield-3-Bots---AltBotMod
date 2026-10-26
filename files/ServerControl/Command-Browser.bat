@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0AltBotMod-Command.ps1" %*
if errorlevel 1 pause
