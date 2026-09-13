@echo off
setlocal
set "VC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
set "ROOT=%~dp0"
set "GAME=%~dp0..\..\.."

tasklist /fi "imagename eq game.exe" 2>nul | find /i "game.exe" >nul && (
    echo ERROR: game.exe is running, close it before building. & exit /b 1
)
tasklist /fi "imagename eq gamemd.exe" 2>nul | find /i "gamemd.exe" >nul && (
    echo ERROR: gamemd.exe is running, close it before building. & exit /b 1
)

call "%VC%" x86 >nul || exit /b 1
if not exist "%ROOT%build" mkdir "%ROOT%build"
cd /d "%ROOT%build"

cl /nologo /O2 /MT /LD /I "%ROOT%..\shared" /Fe:RA2Enhancements.dll "%ROOT%src\*.cpp" "%ROOT%..\shared\patchguard.cpp" /link /DLL user32.lib || exit /b 1

if not exist "%ROOT%_dist" mkdir "%ROOT%_dist"
set "COPYFAIL="
copy /y "RA2Enhancements.dll" "%GAME%\RA2Enhancements.dll" >nul || (echo ERROR: copy RA2Enhancements.dll to game folder failed. & set "COPYFAIL=1")
copy /y "RA2Enhancements.dll" "%ROOT%_dist\RA2Enhancements.dll" >nul || (echo ERROR: copy RA2Enhancements.dll to _dist failed. & set "COPYFAIL=1")
copy /y "%ROOT%src\RA2Enhancements.dll.inj" "%GAME%\RA2Enhancements.dll.inj" >nul || (echo ERROR: copy RA2Enhancements.dll.inj failed. & set "COPYFAIL=1")
copy /y "%ROOT%src\RA2Enhancements.dll.inj" "%ROOT%_dist\RA2Enhancements.dll.inj" >nul || (echo ERROR: copy RA2Enhancements.dll.inj failed. & set "COPYFAIL=1")
copy /y "%ROOT%INSTALL.txt" "%ROOT%_dist\README.txt" >nul
if defined COPYFAIL exit /b 1

echo.
echo RA2Enhancements.dll -^> engine fixes, game speed keys + .inj for Syringe
