@echo off
setlocal
set "ROOT=%~dp0"

for %%P in (hwcursor ra2enhancements ra2video) do (
    echo === %%P ===
    call "%ROOT%%%P\build.cmd" || exit /b 1
)

echo.
echo All three built. Each folder has its output in its own _dist, in the game folder and in G:\games\FM when it exists.
