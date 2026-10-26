@echo off
:: Builds the BotMod loader (proxy xinput9_1_0.dll) and the hot-reloadable core (BotModCore.dll), 32-bit, with MinGW at C:\mingw32
setlocal
set "PATH=C:\mingw32\bin;%PATH%"
cd /d "%~dp0"
if not exist out mkdir out
gcc -m32 -O2 -Wall -shared -static-libgcc -static -o out\xinput9_1_0.dll src\loader.c src\loader.def -Wl,--enable-stdcall-fixup -Wl,--kill-at
if errorlevel 1 exit /b 1
gcc -m32 -O2 -Wall -shared -static-libgcc -static -o out\BotModCore.dll src\core.c src\nav.c src\chat.c src\enhanced.c src\air.c src\modes.c src\trace_nav.c src\livemap.c src\learn.c src\zone_nav.c -lws2_32
if errorlevel 1 exit /b 1
echo Built out\xinput9_1_0.dll and out\BotModCore.dll
copy /y out\BotModCore.dll ..\server\BotModCore.dll >nul
copy /y out\xinput9_1_0.dll ..\server\xinput9_1_0.dll >nul
