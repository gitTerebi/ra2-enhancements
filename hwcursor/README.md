# RA2 HWCursor + WASD

Hardware mouse cursor and WASD map scrolling for **Command & Conquer: Red Alert 2** and
**Yuri's Revenge**, including **Mental Omega**. Two small DLLs, no launcher, no patched
game exe.

- **Hardware cursor** — RA2 blits its cursor into the frame like any other sprite, so
  the pointer only moves when the game draws a frame. This replaces it with a real
  Windows cursor, which moves at your mouse's poll rate no matter what the game is
  doing. Colours and hotspots come from the game's own cursor art, so it still looks
  like RA2.
- **WASD scrolling** — smooth, sub-pixel-accumulated map scrolling on W/A/S/D, on top of
  the arrow keys and edge scroll you already have. Ctrl and Alt are ignored so hotkeys
  still work. **Fully optional: it is a separate DLL, so leave it out and you do not
  get it.**
Pairs with [`ra2enhancements`](../ra2enhancements) in this repo, engine fixes and game
speed keys in a DLL this `dsound.dll` loads the same way.

Pairs with [cnc-ddraw-ra2](https://github.com/gitTerebi/cnc-ddraw-ra2), a renderer fork
whose `fixchilds=5` keeps the RA2 menus clickable while the game is upscaled.
[`ra2video`](../ra2video) in this repo is the `ddraw.ini` resolution picker for it.

32-bit, Windows only, matching the game.

## Install

Grab the zip from [Releases](../../releases) and copy files into your game folder (the
one with `gamemd.exe` / `game.exe` in it).

### Any install, vanilla or Syringe

`dsound.dll` is the loader and does nothing else, so it is always needed. Add the
feature DLLs you want next to it.

| Want | Copy |
| --- | --- |
| Cursor | `dsound.dll` + `HWCursor.dll` |
| WASD | `dsound.dll` + `WasdScroll.dll` |
| Both | `dsound.dll` + `HWCursor.dll` + `WasdScroll.dll` |

`dsound.dll` also loads `RA2Enhancements.dll` from [`ra2enhancements`](../ra2enhancements)
if you drop that in beside it.

The game statically imports DirectSound, so Windows loads our `dsound.dll` from the exe's
folder whatever the working directory is. It forwards all 12 exports to the real
`C:\Windows\SysWOW64\dsound.dll`, so **audio is untouched**, then loads whichever of
`HWCursor.dll` and `WasdScroll.dll` it finds beside it. The same files work on Mental
Omega and other Syringe mods.

### Uninstall

Delete the file for the feature you do not want. The DLLs are independent, so removing
one leaves the other working.

## Compatibility

Verified against Steam Yuri's Revenge 1.001 (`gamemd.exe`) and Red Alert 2
(`game.exe`), and against Mental Omega via Syringe 0.7.3.0.

The DLLs check the exe they land in and refuse to patch anything they do not recognise,
so a mismatched build no-ops rather than crashing. If you use Steam's `gamemd.exe` with
Mental Omega, note MO needs its patched Syringe; that is an MO issue, not this one.

**Multiplayer:** this changes how the cursor is drawn and moves the camera locally. It
does not touch game logic, units, or the network protocol.

## Build

WASD camera movement now runs at entry to `GScreenClass::GetInputAndUpdate`,
before its input/update work, using elapsed time and fractional-pixel accumulation.
The hook validates the function signature and preserves CPU and floating-point
state. If validation fails, WASD scrolling is off.
With `RA2Enhancements.dll` installed, the simulation limiter also calls
`RenderScrollFrame` during its wait while WASD or a right-button drag moves the map.
It aims for one camera update per monitor refresh, timed from the DWM vblank clock.
If DWM timing is unavailable, it falls back to 240 updates per second.
DWM reports only the primary display, so on another monitor the camera uses that
monitor's refresh rate without vblank alignment. Each update
uses the game's idle-render sequence, without dispatching gameplay input or advancing
the simulation. When a redraw takes longer than one refresh, the camera updates on
every second refresh, then every third, and so on up to every eighth. Right-button scrolling reuses the native drag threshold, capture,
cursor and scroll settings. WASD and continuous right-drag scrolling share the
in-game Scroll Rate slider: slowest to fastest is 600, 900, 1200, 1500, 1800,
2100 and 2400 pixels/second. The middle position is 1500 pixels/second, with
diagonal movement normalized to the same speed. Changes apply immediately.
Pointer-relative drag modes use the same curve for sensitivity while retaining
distance-based movement.
Arrow-key and edge scrolling retain their existing behavior.
Build and startup checks passed; terrain/unit alignment and sustained redraw rate
still need an in-game check. See [address evidence](../RA2-MAP-ADDRESS.md).

Needs the VS 2022 x86 toolset.

```
build.cmd
```

Outputs `HWCursor.dll`, `WasdScroll.dll` and `dsound.dll` into `_dist\` and copies them
into the game folder three levels above this folder.

## Source layout

| File | What |
| --- | --- |
| `src/gameprofile.h` | game detection and the per-exe address tables both feature DLLs need |
| `src/hwcursor.cpp` | `HWCursor.dll` — SHP frame to `HCURSOR`, palette conversion, cache |
| `src/wasd.cpp` | `WasdScroll.dll`: inline hook on the scroll tick and the camera redraw exports |
| `src/dsoundproxy.cpp` | `dsound.dll` — forwarding and sidecar loading, nothing else |
| `src/dsoundforward.h` | the 12 DirectSound export thunks |

`dsound.dll` redirects the exe's `GetCurrentThreadId` import when it loads. The exe's first
call to it, at the top of WinMain, comes after Syringe has written every mod's hooks and
before the game loads its hotkeys. That call puts the import back, loads the feature DLLs
and calls each one's `_Install` export. `DllMain` installs nothing. Every write goes through
`InstallPatch` and `WriteJmp` or `WriteCall` in `../shared/patchguard.cpp`, which leaves a
feature off if another mod already changed its code.

The hardcoded addresses live in one table in `src/gameprofile.h`, one row per exe. Each
DLL confirms the exe by matching bytes at an address nothing else patches, never at a
hook site.

## Licence

MIT, see [`LICENSE`](../LICENSE) at the repo root.

Not affiliated with or endorsed by EA. Red Alert 2 and Yuri's Revenge are EA's
trademarks. This ships no game content — you need your own copy of the game.
