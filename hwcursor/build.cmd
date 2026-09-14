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

cl /nologo /O2 /MT /LD /I "%ROOT%..\shared" /Fe:HWCursor.dll "%ROOT%src\hwcursor.cpp" "%ROOT%..\shared\patchguard.cpp" /link /DLL user32.lib gdi32.lib || exit /b 1
cl /nologo /O2 /MT /LD /I "%ROOT%..\shared" /Fe:WasdScroll.dll "%ROOT%src\wasd.cpp" "%ROOT%..\shared\patchguard.cpp" /link /DLL user32.lib || exit /b 1
cl /nologo /O2 /MT /LD /I "%ROOT%..\shared" /Fe:dsound.dll "%ROOT%src\dsoundproxy.cpp" /link /DLL || exit /b 1

if not exist "%ROOT%_dist" mkdir "%ROOT%_dist"
set "COPYFAIL="
for %%F in (HWCursor.dll WasdScroll.dll dsound.dll) do (
    copy /y "%%F" "%GAME%\%%F" >nul || (echo ERROR: copy %%F to game folder failed. & set "COPYFAIL=1")
    copy /y "%%F" "%ROOT%_dist\%%F" >nul || (echo ERROR: copy %%F to _dist failed. & set "COPYFAIL=1")
)
for %%F in (HWCursor.dll.inj WasdScroll.dll.inj) do (
    copy /y "%ROOT%src\%%F" "%GAME%\%%F" >nul || (echo ERROR: copy %%F failed. & set "COPYFAIL=1")
    copy /y "%ROOT%src\%%F" "%ROOT%_dist\%%F" >nul || (echo ERROR: copy %%F failed. & set "COPYFAIL=1")
)
copy /y "%ROOT%INSTALL.txt" "%ROOT%_dist\README.txt" >nul
if defined COPYFAIL exit /b 1

echo.
echo dsound.dll    -^> proxy only, loads whichever feature DLLs are present (vanilla)
echo HWCursor.dll  -^> hardware cursor      + .inj for Syringe
echo WasdScroll.dll-^> WASD map scrolling   + .inj for Syringe, optional
