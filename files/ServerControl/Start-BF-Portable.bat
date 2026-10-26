@echo off
:: AltBotMod: one click - starts BF-Portable for BF3 (redirector, database, web server, BF3 Blaze emulator, the ticked
:: servers) and opens Server Control with Battlelog. Choose which servers on Server Control's BF-Portable page.
start "" powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -WindowStyle Hidden -File "%~dp0AltBotMod-ServerControl.ps1" -Launch
