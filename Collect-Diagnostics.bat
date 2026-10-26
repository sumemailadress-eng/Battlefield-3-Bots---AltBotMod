@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0files\BotMod\Collect-Diagnostics.ps1" %*
if errorlevel 1 echo Diagnostics collection failed.
pause
