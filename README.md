# RA2 Enhancements

Small, independent add-ons for **Command & Conquer: Red Alert 2**, **Yuri's Revenge** and
**Mental Omega**. No launcher, no patched game exe, no game content. Pick the files you
want and copy them into your game folder.

32-bit, Windows only, matching the game.

Grab the zips from [Releases](../../releases).

| Folder | Ships | What it does |
| --- | --- | --- |
| [`hwcursor`](hwcursor) | `dsound.dll`, `HWCursor.dll`, `WasdScroll.dll` | A real Windows mouse cursor instead of RA2's blitted sprite, and optional WASD map scrolling. `dsound.dll` is the loader the other DLLs use on vanilla. |
| [`ra2enhancements`](ra2enhancements) | `RA2Enhancements.dll` | Game speed keys on `[` and `]`, one voice instead of every unit on select-all, and shift-click a sidebar cameo to queue 5. |
| [`ra2video`](ra2video) | `RA2Video.exe` | A settings tool that writes the render resolution to the game INIs and the window size, position and shader chain to `ddraw.ini`. |

Each folder has its own `README.md` with the full feature list, install table and source
layout. Read that one before you install.

## How the pieces fit

On vanilla Red Alert 2 and Yuri's Revenge there is no mod loader, so `dsound.dll` from
`hwcursor` acts as one. The game statically imports DirectSound, so Windows loads our
`dsound.dll` from the game folder before the first instruction runs. It forwards all 12
exports to the real `C:\Windows\SysWOW64\dsound.dll`, which leaves **audio untouched**,
then loads whichever of `HWCursor.dll`, `WasdScroll.dll` and `RA2Enhancements.dll` it
finds beside it. That is why `hwcursor` is worth copying even if you only want the speed
keys.

On Mental Omega, and anything else running Syringe, you do not need `dsound.dll`. Syringe
scans `*.dll` in the game folder and picks up the matching `.inj` file, so copy the DLL
and its `.inj` together. Each `.inj` holds one hook early in WinMain, and each DLL installs
its patches from that hook, after Syringe has written every other mod's hooks. One binary
serves both load paths, and deleting any DLL leaves the others working. Syringe ignores an
`.inj` whose DLL is gone.

`RA2Video.exe` is a standalone tool. It edits ini files and never loads into the game, so
it does not care which of the above you use. It does assume you render through
[cnc-ddraw](https://github.com/gitTerebi/cnc-ddraw-ra2).

## Also worth having

[cnc-ddraw-ra2](https://github.com/gitTerebi/cnc-ddraw-ra2) is a renderer fork whose
`fixchilds=5` keeps the RA2 menus clickable while the game is upscaled. It lives in its
own repo because it tracks FunkyFr3sh's upstream.

## Build

Each folder builds on its own with its own `build.cmd`, and needs the VS 2022 x86 toolset.
`build.cmd` at the root runs all three in turn. Every script copies its output into `_dist`
and into the game folder three levels above the repo, and refuses to build while the game
is running.

## Compatibility

Verified against Steam Yuri's Revenge 1.001 (`gamemd.exe`) and Red Alert 2 (`game.exe`),
against Mental Omega via Syringe 0.7.3.0, and against C&C Reloaded 2.7.0 (Ares, Phobos and
SyringeEx).

Every DLL checks the exe it lands in and refuses to patch anything it does not recognise,
so a mismatched build no-ops rather than crashing. All three DLLs write game code through
`shared/patchguard.cpp`. Before every write, it compares the bytes in memory with the exe on
disk. If another mod already changed a site, that feature stays off, `campaignspeed.log`
names the site, and every other feature still installs.

Phobos hooks the frame pacer and the stop order code. The speed keys then hook the next
instruction after Phobos's pacer hook, and attack move leaves the stop order to Phobos,
which clears attack move there itself.

**Multiplayer:** the cursor, the scrolling and the select-all voice fix do not touch game
logic, units, or the network protocol. The speed keys do not either, but they change how
fast the local client paces frames, and the RA2 netcode does not expect a client running
ahead of its peers. Use the speed keys in single player.

## Licence

MIT, see `LICENSE`.

Not affiliated with or endorsed by EA. Red Alert 2 and Yuri's Revenge are EA's trademarks.
This ships no game content, so you need your own copy of the game.
