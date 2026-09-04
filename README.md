# RA2Enhancements

Engine fixes and a game speed control for **Command & Conquer: Red Alert 2**, **Yuri's
Revenge** and **Mental Omega**. One small DLL, no launcher, no patched game exe.

- **Game speed keys.** `[` slows the game down a step, `]` speeds it up. Seven steps run
  from 1.00x (stock) to 2.00x. The window title shows the current step. The campaign and
  skirmish both run at the game's own speed cap; this raises it by alternating fast and
  slow frames inside the engine's pacer.
- **Speed is remembered.** The DLL writes the chosen step to the game's own ini under
  `[RA2Enhancements] GameSpeedStep`, and reads it back on the next launch. Each game keeps
  its own value: `game.exe` uses `RA2.ini`, `gamemd.exe` uses `RA2MD.ini`, and Mental Omega
  uses `RA2MO.ini`.
- **One voice on select-all.** Select-all-of-type (T, whole map) and select-all-combatants
  (Q) call `Select()` on every matching unit without clearing the engine's `AllowVoice`
  flag, so every unit shouts its select line at once and the mix clips. Team select and
  band-box already clear the flag after the first unit; this does the same for those two
  commands.
- **Shift-queue five.** Shift-click a sidebar cameo to queue 5 of that unit, shift
  right-click to remove 5.

Pairs with [ra2-hwcursor-wasd](https://github.com/gitTerebi/ra2-hwcursor-wasd), which adds
a hardware mouse cursor and WASD map scrolling, and ships the `dsound.dll` loader this DLL
uses on vanilla.

32-bit, Windows only, matching the game.

## Install

Copy files into your game folder (the one with `gamemd.exe` / `game.exe` in it).

### Vanilla Red Alert 2 / Yuri's Revenge

| Copy | From |
| --- | --- |
| `RA2Enhancements.dll` | here |
| `dsound.dll` | [ra2-hwcursor-wasd](https://github.com/gitTerebi/ra2-hwcursor-wasd) |

The game statically imports DirectSound, so Windows loads `dsound.dll` from the game folder
before the first instruction runs. That proxy forwards all 12 exports to the real
`C:\Windows\SysWOW64\dsound.dll`, so **audio is untouched**, then loads this DLL beside it.
This DLL has no loader of its own on vanilla.

### Mental Omega (and anything else on Syringe)

Copy `RA2Enhancements.dll` and `RA2Enhancements.dll.inj`. Syringe scans `*.dll` in the game
folder and picks up the matching `.inj` on the next launch. `syringe.log` names it when it
loads. You do not need `dsound.dll` here.

The `.inj` carries `gamemd.exe` addresses only. On `game.exe` the inline patch covers the
same ground.

### Uninstall

Delete `RA2Enhancements.dll`. Nothing else depends on it.

## Speed steps

| Step | Speed | Frames |
| --- | --- | --- |
| 0 | 1.00x (stock) | 0 fast, 1 slow |
| 1 | 1.14x | 1 fast, 3 slow |
| 2 | 1.20x | 1 fast, 2 slow |
| 3 | 1.33x | 1 fast, 1 slow |
| 4 | 1.50x (default) | 2 fast, 1 slow |
| 5 | 1.60x | 3 fast, 1 slow |
| 6 | 2.00x (max) | 1 fast, 0 slow |

You can set `GameSpeedStep` by hand in the ini. The DLL falls back to 4 for an
out-of-range value.

**Multiplayer:** the speed keys change how fast the local client paces frames. They do not
touch game logic, units, or the network protocol, but the RA2 netcode does not expect a
client running ahead of its peers. Use the speed keys in single player.

## Compatibility

Verified against Steam Yuri's Revenge 1.001 (`gamemd.exe`) and Red Alert 2 (`game.exe`),
and against Mental Omega via Syringe 0.7.3.0.

The DLL checks the exe it lands in and refuses to patch anything it does not recognise, so
a mismatched build no-ops rather than crashing. It byte-matches every patch site before
writing, and aborts the whole speed patch if any site reads wrong.

## Build

Needs the VS 2022 x86 toolset.

```
build.cmd
```

The script outputs `RA2Enhancements.dll` into `_dist\` and copies it into the game folder
above the repo.

## Source layout

| File | What |
| --- | --- |
| `src/gameprofile.h` | game detection, the per-exe address table, and which ini each game reads |
| `src/ra2enhancements.cpp` | the DLL: select sites, speed pacer hook, sidebar queue hook, Syringe exports |
| `src/RA2Enhancements.dll.inj` | Syringe hook list, `gamemd.exe` only |

The DLL patches the game itself when the `dsound.dll` proxy loads it, and stands down under
Syringe (`IsDebuggerPresent`) so the `.inj` hooks are the only ones applied for the select
fix. One binary serves both load paths.

The speed patch runs on both paths, because Syringe does not touch those sites.
`IsDebuggerPresent` also tells Mental Omega from stock Yuri's Revenge: both run the same
`gamemd.exe`, but MO always launches through Syringe and reads `RA2MO.ini`.

The hardcoded addresses live in tables at the top of each section, one row per exe. The DLL
confirms the exe by matching bytes at an address nothing else patches, never at a hook site,
because under Syringe the bytes at a hook site are already Syringe's.

Credit: the speed hook sites come from Phobos, `src/Misc/Hooks.Gamespeed.cpp`.

## Licence

MIT, see `LICENSE`.

Not affiliated with or endorsed by EA. Red Alert 2 and Yuri's Revenge are EA's trademarks.
This ships no game content, so you need your own copy of the game.
