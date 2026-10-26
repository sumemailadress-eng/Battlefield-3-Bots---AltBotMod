@echo off
setlocal
set "setup=%~dp0installer\AltBotMod-Setup.ps1"
if not exist "%setup%" set "setup=%~dp0..\AltBotMod-Setup.ps1"
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%setup%" -Mode fleet %*
pause
