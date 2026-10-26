@echo off
:: AltBotMod Server Control - one window for all your BF3 server instances (map / mode, next map, rotation, bots).
start "" powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -WindowStyle Hidden -File "%~dp0AltBotMod-ServerControl.ps1" %*
