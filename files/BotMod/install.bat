@echo off
setlocal
:: Use the complete cached installer after installation, or the extracted one.
set "SETUP=%~dp0..\AltBotMod-Setup.ps1"
if exist "%~dp0installer\AltBotMod-Setup.ps1" set "SETUP=%~dp0installer\AltBotMod-Setup.ps1"
if not exist "%SETUP%" (
    echo Installer missing. Extract the AltBotMod ZIP and run Install-AltBotMod.bat.
    pause
    exit /b 1
)
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%SETUP%" -Mode install -ObjectivePreset %*
set "RESULT=%ERRORLEVEL%"
echo.
pause
exit /b %RESULT%
