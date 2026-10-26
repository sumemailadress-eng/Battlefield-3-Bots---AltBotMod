@echo off
setlocal
:: Installed shortcut opens the active file, never the cached defaults.
if exist "%~dp0installer\AltBotMod-Setup.ps1" (
    if not exist "%~dp0enhanced.ini" (
        echo Active settings are missing. Run Install-AltBotMod.bat first.
        pause
        exit /b 1
    )
    start "" notepad.exe "%~dp0enhanced.ini"
    exit /b 0
)
:: In the extracted package, let the installer locate the active installation.
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0..\AltBotMod-Setup.ps1" -Mode settings %*
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" pause
exit /b %RESULT%
