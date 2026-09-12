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

### Vanilla Red Alert 2 / Yuri's Revenge

`dsound.dll` is the loader and does nothing else, so it is always needed. Add the
feature DLLs you want next to it.

| Want | Copy |
| --- | --- |
| Cursor | `dsound.dll` + `HWCursor.dll` |
| WASD | `dsound.dll` + `WasdScroll.dll` |
| Both | `dsound.dll` + `HWCursor.dll` + `WasdScroll.dll` |

`dsound.dll` also loads `RA2Enhancements.dll` from [`ra2enhancements`](../ra2enhancements)
if you drop that in beside it.

The game statically imports DirectSound, so Windows loads our `dsound.dll` from the game
folder before the first instruction runs. It forwards all 12 exports to the real
`C:\Windows\SysWOW64\dsound.dll`, so **audio is untouched**, then loads whichever of
`HWCursor.dll` and `WasdScroll.dll` it finds beside it. Neither has a loader of its own
on vanilla.

### Mental Omega (and anything else on Syringe)

| Want | Copy |
| --- | --- |
| Cursor | `HWCursor.dll` + `HWCursor.dll.inj` |
| WASD | `WasdScroll.dll` + `WasdScroll.dll.inj` |

Any pair works on its own. The `.inj` files carry `gamemd.exe` addresses only. Syringe scans `*.dll` in the game folder and picks up the
matching `.inj` on the next launch. `syringe.log` will name whichever loaded.

`dsound.dll` is harmless alongside Syringe but is not needed — it detects Syringe and
stands down, leaving the loading to it.

### Uninstall

Delete the file for the feature you do not want. The DLLs are independent, so removing
one leaves the other working.

## Compatibility

Verified against Steam Yuri's Revenge 1.001 (`gamemd.exe`) and Red Alert 2
(`game.exe`), and against Mental Omega via Syringe 0.7.3.0.

The DLLs check the exe they land in and refuse to patch anything they do not recognise,
so a mismatched build no-ops rather than crashing. If you use Steam's `gamemd.exe` with
Mental Omega, note MO needs its patched Syringe; that is an MO issue, not this one.

**Multiplayer:** this changes how the cursor is drawn and synthesises the same scroll
input the arrow keys produce. It does not touch game logic, units, or the network
protocol.

## Build

WASD camera movement now runs at entry to `GScreenClass::GetInputAndUpdate`,
before its input/update work, using elapsed time and fractional-pixel accumulation.
The hook validates the function signature and preserves CPU and floating-point
state. It is installed during DLL loading; if validation fails, a worker uses
the arrow-key fallback. The exported Syringe tick remains for loader compatibility.
With `RA2Enhancements.dll` installed, the simulation limiter also calls
`RenderScrollFrame` during its wait, targeting 240 camera updates per second while
WASD or a right-button drag moves the map. Each update uses the game's idle-render sequence, without
dispatching gameplay input or advancing the simulation. Actual redraw rate depends
on rendering cost. Right-button scrolling reuses the native drag threshold, capture,
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
| `src/wasd.cpp` | `WasdScroll.dll` — inline hook on the scroll tick, plus a key-synth fallback |
| `src/dsoundproxy.cpp` | `dsound.dll` — forwarding and sidecar loading, nothing else |
| `src/dsoundforward.h` | the 12 DirectSound export thunks |

The cursor DLL uses the `.inj` hooks under Syringe. The WASD DLL installs its
validated input/update hook during loading for either loader.

The hardcoded addresses live in one table in `src/gameprofile.h`, one row per exe. Each
DLL confirms the exe by matching bytes at an address nothing else patches, never at a
hook site, because under Syringe the bytes at a hook site are already Syringe's.

## Licence

MIT, see [`LICENSE`](../LICENSE) at the repo root.

Not affiliated with or endorsed by EA. Red Alert 2 and Yuri's Revenge are EA's
trademarks. This ships no game content — you need your own copy of the game.
