@echo off
:: AltBotMod install - finds your BF3 server folder (or asks where it is). The work is done by files\AltBotMod-Setup.ps1
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0files\AltBotMod-Setup.ps1" -Mode install -ObjectivePreset %*
set "RESULT=%ERRORLEVEL%"
echo.
pause
exit /b %RESULT%
