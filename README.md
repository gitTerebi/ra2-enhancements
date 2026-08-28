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
- **RA2Video.exe** — a small renderer/resolution picker for `ddraw.ini`, for use with
  [cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw).

32-bit, Windows only, matching the game.

## Install

Grab the zip from [Releases](../../releases) and copy files into your game folder (the
one with `gamemd.exe` / `game.exe` in it).

### Vanilla Red Alert 2 / Yuri's Revenge

| Want | Copy |
| --- | --- |
| Cursor | `dsound.dll` |
| Cursor + WASD | `dsound.dll` and `WasdScroll.dll` |

The game statically imports DirectSound, so Windows loads our `dsound.dll` from the game
folder before the first instruction runs. It forwards all 12 exports to the real
`C:\Windows\SysWOW64\dsound.dll`, so **audio is untouched**, and it loads
`WasdScroll.dll` from the same folder if you put it there.

`WasdScroll.dll` has no loader of its own on vanilla, so it needs `dsound.dll` present.

### Mental Omega (and anything else on Syringe)

| Want | Copy |
| --- | --- |
| Cursor | `HWCursor.dll` + `HWCursor.dll.inj` |
| WASD | `WasdScroll.dll` + `WasdScroll.dll.inj` |

Either pair works on its own. Syringe scans `*.dll` in the game folder and picks up the
matching `.inj` on the next launch. `syringe.log` will name both if they loaded.

The `dsound.dll` proxy is harmless alongside Syringe but is not needed — it detects
Syringe and stands down.

### Uninstall

Delete the files. Deleting `WasdScroll.dll` leaves the cursor working.

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

Needs the VS 2022 x86 toolset.

```
build.cmd
```

Outputs `HWCursor.dll`, `WasdScroll.dll`, `dsound.dll` and `RA2Video.exe` into `_dist\`
and copies them into the game folder above the repo.

## Source layout

| File | What |
| --- | --- |
| `src/gameprofile.h` | game detection and the per-exe address tables both DLLs need |
| `src/hwcursor.cpp` | the cursor: SHP frame to `HCURSOR`, palette conversion, cache |
| `src/wasd.cpp` | WASD scroll: inline hook on the scroll tick, plus a key-synth fallback |
| `src/dsoundforward.h` | the 12-export DirectSound proxy for the vanilla load path |
| `src/videocfg.cpp` | `RA2Video.exe` |

The hardcoded addresses live in one table in `src/gameprofile.h`, one row per exe. Each
DLL confirms the exe by matching bytes at an address nothing else patches, never at a
hook site, because under Syringe the bytes at a hook site are already Syringe's.

## Licence

MIT, see `LICENSE`.

Not affiliated with or endorsed by EA. Red Alert 2 and Yuri's Revenge are EA's
trademarks. This ships no game content — you need your own copy of the game.
