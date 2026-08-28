@echo off
setlocal
set "VC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
set "ROOT=%~dp0"
set "GAME=%~dp0..\.."

call "%VC%" x86 >nul || exit /b 1
if not exist "%ROOT%build" mkdir "%ROOT%build"
cd /d "%ROOT%build"

cl /nologo /O2 /MT /LD /Fe:HWCursor.dll "%ROOT%src\hwcursor.cpp" /link /DLL user32.lib gdi32.lib || exit /b 1
cl /nologo /O2 /MT /LD /Fe:WasdScroll.dll "%ROOT%src\wasd.cpp" /link /DLL user32.lib || exit /b 1
cl /nologo /O2 /MT /LD /Fe:dsound.dll "%ROOT%src\dsoundproxy.cpp" /link /DLL || exit /b 1
cl /nologo /O2 /MT /Fe:RA2Video.exe "%ROOT%src\videocfg.cpp" /link user32.lib gdi32.lib comctl32.lib /SUBSYSTEM:WINDOWS || exit /b 1

if not exist "%ROOT%_dist" mkdir "%ROOT%_dist"
for %%F in (HWCursor.dll WasdScroll.dll dsound.dll RA2Video.exe) do (
    copy /y "%%F" "%GAME%\%%F" >nul
    copy /y "%%F" "%ROOT%_dist\%%F" >nul
)
for %%F in (HWCursor.dll.inj WasdScroll.dll.inj) do (
    copy /y "%ROOT%src\%%F" "%GAME%\%%F" >nul
    copy /y "%ROOT%src\%%F" "%ROOT%_dist\%%F" >nul
)
copy /y "%ROOT%INSTALL.txt" "%ROOT%_dist\README.txt" >nul

echo.
echo dsound.dll    -^> proxy only, loads whichever feature DLLs are present (vanilla)
echo HWCursor.dll  -^> hardware cursor      + .inj for Syringe
echo WasdScroll.dll-^> WASD map scrolling   + .inj for Syringe, optional
