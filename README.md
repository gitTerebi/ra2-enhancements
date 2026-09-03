# RA2Video

Video settings tool for Red Alert 2 / Yuri's Revenge running under cnc-ddraw.

Writes the render resolution to the game INIs (`RA2MD.ini`, `RA2.ini`, `RA2MO.ini`)
and the window size, position and shader chain to `ddraw.ini`.

## Build

    build.cmd

Needs Visual Studio 2022 Community with the x86 toolset. The script refuses to
build while RA2Video.exe is running, and copies the result to `_dist` and to the
game folder two levels up.

## Settings it writes

| Target | Keys |
| --- | --- |
| game INIs | `[Video] ScreenWidth`, `ScreenHeight` |
| ddraw.ini | `inject_resolution`, `width`, `height`, `windowed`, `border`, `fullscreen`, `posX`, `posY`, `center_window`, `shader` |

`ra2video_mode` in `ddraw.ini` remembers the chosen output entry.

## Output modes

- **Windowed maximised** keeps the title bar and fills the work area.
- **Borderless maximised** drops the frame and keeps the taskbar visible.
- **Borderless fullscreen** covers the desktop including the taskbar.
- Fixed sizes are offered when they fit the work area, marked when they match the
  game aspect ratio.

Window sizing accounts for per-monitor DPI and for the invisible DWM drop-shadow
border, measured from a hidden `WS_OVERLAPPEDWINDOW` probe so the inset matches
the resizable game window rather than this tool's own dialog.
