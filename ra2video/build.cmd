@echo off
setlocal
set "VC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
set "ROOT=%~dp0"
set "GAME=%~dp0..\.."

tasklist /fi "imagename eq RA2Video.exe" 2>nul | find /i "RA2Video.exe" >nul && (
    echo ERROR: RA2Video.exe is running, close it before building. & exit /b 1
)

call "%VC%" x86 >nul || exit /b 1
if not exist "%ROOT%build" mkdir "%ROOT%build"
cd /d "%ROOT%build"

cl /nologo /O2 /MT /Fe:RA2Video.exe "%ROOT%src\ra2video.cpp" ^
   /link user32.lib gdi32.lib comctl32.lib dwmapi.lib /SUBSYSTEM:WINDOWS || exit /b 1

if not exist "%ROOT%_dist" mkdir "%ROOT%_dist"
copy /y RA2Video.exe "%ROOT%_dist\RA2Video.exe" >nul || (echo ERROR: copy to _dist failed. & exit /b 1)
copy /y RA2Video.exe "%GAME%\RA2Video.exe" >nul || (echo ERROR: copy to game folder failed. & exit /b 1)

echo RA2Video.exe -^> _dist and game folder
