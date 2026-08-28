@echo off
setlocal
set "VC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
set "ROOT=%~dp0"
set "GAME=%~dp0..\.."

call "%VC%" x86 >nul || exit /b 1
if not exist "%ROOT%build" mkdir "%ROOT%build"
if not exist "%ROOT%bin" mkdir "%ROOT%bin"
cd /d "%ROOT%build"

cl /nologo /O2 /MT /LD /Fe:HWCursor.dll "%ROOT%src\hwcursor.cpp" /link /DLL user32.lib gdi32.lib || exit /b 1
cl /nologo /O2 /MT /LD /Fe:WasdScroll.dll "%ROOT%src\wasd.cpp" /link /DLL user32.lib || exit /b 1
cl /nologo /O2 /MT /Fe:RA2Video.exe "%ROOT%src\videocfg.cpp" /link user32.lib gdi32.lib comctl32.lib /SUBSYSTEM:WINDOWS || exit /b 1
cl /nologo /O2 /MT /LD /DHWCURSOR_SELFPATCH /DHWCURSOR_DSOUNDPROXY /Fe:dsound.dll "%ROOT%src\hwcursor.cpp" /link /DLL user32.lib gdi32.lib || exit /b 1

copy /y dsound.dll "%GAME%\dsound.dll" >nul
echo Auto-inject   -^> dsound proxy installed

copy /y HWCursor.dll "%GAME%\HWCursor.dll" >nul
copy /y "%ROOT%src\HWCursor.dll.inj" "%GAME%\HWCursor.dll.inj" >nul
copy /y WasdScroll.dll "%GAME%\WasdScroll.dll" >nul
copy /y "%ROOT%src\WasdScroll.dll.inj" "%GAME%\WasdScroll.dll.inj" >nul
copy /y dsound.dll "%ROOT%bin\dsound.dll" >nul
copy /y RA2Video.exe "%GAME%\RA2Video.exe" >nul

if not exist "%ROOT%_dist" mkdir "%ROOT%_dist"
copy /y dsound.dll "%ROOT%_dist\dsound.dll" >nul
copy /y HWCursor.dll "%ROOT%_dist\HWCursor.dll" >nul
copy /y "%ROOT%src\HWCursor.dll.inj" "%ROOT%_dist\HWCursor.dll.inj" >nul
copy /y WasdScroll.dll "%ROOT%_dist\WasdScroll.dll" >nul
copy /y "%ROOT%src\WasdScroll.dll.inj" "%ROOT%_dist\WasdScroll.dll.inj" >nul
copy /y RA2Video.exe "%ROOT%_dist\RA2Video.exe" >nul

echo.
echo Cursor        -^> dsound.dll (vanilla) / HWCursor.dll + .inj (Mental Omega)
echo WASD scroll   -^> WasdScroll.dll + .inj, optional, delete to disable
