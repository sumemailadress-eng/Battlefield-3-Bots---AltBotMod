@echo off
:: AltBotMod uninstall - puts the original XInput DLL back; settings and navigation are kept.
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0files\AltBotMod-Setup.ps1" -Mode uninstall %*
set "RESULT=%ERRORLEVEL%"
echo.
pause
exit /b %RESULT%
