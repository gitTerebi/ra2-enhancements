# RA2Enhancements

Engine fixes and a game speed control for **Command & Conquer: Red Alert 2**, **Yuri's
Revenge** and **Mental Omega**. One small DLL, no launcher, no patched game exe.

- **Game speed keys.** `[` slows the game down a step, `]` speeds it up. Seven steps run
  from 30fps (stock) to 60fps. The window title shows the current step. RA2 paces frames in
  whole 60Hz ticks, so its own speeds are only 60, 30, 20, 15, 12 and 10fps; this holds each
  frame to an exact target length instead, which is how it reaches rates in between.
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
- **Healers escort the group.** When a selection attacks, the stock game sends a Repair
  IFV straight at the target, because the vehicle order code treats "can't act on this" as a
  move. Now every healer in the selection (any vehicle or infantry whose weapon does negative
  damage) gets Area Guard anchored on an armed unit from the same selection, the same order
  as a Ctrl+Alt escort. Healers are spread evenly across the width of the army, so each
  armed unit gets its share and each healer stays on its own flank. This covers normal
  attacks, force attack on buildings and units, and force fire on empty ground.
- **Quick save and quick load.** In a campaign mission or a skirmish game, F5 saves over the
  older of `QUICK.SAV` and `QUICK2.SAV`, and F9 loads the newer one. If one quick save is ever
  damaged, the other still holds the save before it. Both keys call the same code as the Save
  and Load buttons, and they run at the same point in the frame, so the files also show up in
  the Load menu as "Quick Save" and "Quick Save 2". F9 also works on the main menu, where it
  loads the newer quick save straight away. The Save menu no longer asks before overwriting a
  save, and no longer shows a "Game saved" box afterwards. This works in Mental Omega too. Its
  CnCNet spawner moves every save into `Saved Games\`, so the quick saves sit there, and F9
  looks in whatever folder the Load menu lists.
- **Mental Omega mission start saves have names.** The game saves each campaign mission as it
  starts and titles the save with the mission name from `MISSIONMD.INI`. Mental Omega launches
  missions through its client, which leaves that name empty, so every mission start save was
  called `[]`. The DLL now takes the name from the client's `INI\BattleClient.ini`, for example
  `[Soviet 01: BLEED RED]`, and falls back to the map file name.
- **Old quick saves heal themselves.** An earlier build saved on F5 in the middle of a frame,
  after some particles had died but before the game deleted them. Those saves hold leaked
  particles, and once their particle system is gone a later save crashes on load. Before every
  save the DLL now deletes leaked particles and clears pointers to deleted particle systems, and
  a quick load clears any broken pointers it still finds in an older save.
- **Real hotkey commands.** Quick Save, Quick Load, Game Speed Down and Game Speed Up are
  added to the game's own command list, so they sit in the Keyboard options menu and in
  `[Hotkey]` in `Keyboard.ini` / `KeyboardMD.ini` like any stock command. The game delivers
  their key presses, so none are missed. On the first launch the DLL binds them to F5, F9,
  `[` and `]`, and unbinds whatever stock command held those keys (Taunt 1 and Taunt 5 by
  default). Mental Omega reads the same `KeyboardMD.ini` as Yuri's Revenge.

Pairs with [`hwcursor`](../hwcursor) in this repo, which adds a hardware mouse cursor and
WASD map scrolling, and ships the `dsound.dll` loader this DLL uses on vanilla.

32-bit, Windows only, matching the game.

## Install

Copy files into your game folder (the one with `gamemd.exe` / `game.exe` in it).

### Vanilla Red Alert 2 / Yuri's Revenge

| Copy | From |
| --- | --- |
| `RA2Enhancements.dll` | here |
| `dsound.dll` | [`hwcursor`](../hwcursor) |

The game statically imports DirectSound, so Windows loads `dsound.dll` from the game folder
before the first instruction runs. That proxy forwards all 12 exports to the real
`C:\Windows\SysWOW64\dsound.dll`, so **audio is untouched**, then loads this DLL beside it.
This DLL has no loader of its own on vanilla.

### Mental Omega (and anything else on Syringe)

Copy `RA2Enhancements.dll` and `RA2Enhancements.dll.inj`. Syringe scans `*.dll` in the game
folder and picks up the matching `.inj` on the next launch. `syringe.log` names it when it
loads. You do not need `dsound.dll` here.

The `.inj` holds one hook, early in `gamemd.exe`'s WinMain. Syringe calls it after every mod's
hooks are written, and the DLL installs all of its patches from there.

### Uninstall

Delete `RA2Enhancements.dll`. Nothing else depends on it.

## Speed steps

| Step | Speed | Frame rate | Frame length |
| --- | --- | --- | --- |
| 0 | 1.00x (stock) | 30fps | 33.33ms |
| 1 | 1.17x | 35fps | 28.57ms |
| 2 | 1.33x | 40fps | 25.00ms |
| 3 | 1.50x (default) | 45fps | 22.22ms |
| 4 | 1.60x | 48fps | 20.83ms |
| 5 | 1.67x | 50fps | 20.00ms |
| 6 | 2.00x (max) | 60fps | 16.67ms |

You can set `GameSpeedStep` by hand in the ini. The DLL falls back to 3 for an
out-of-range value.

Every step holds one frame length, so the motion stays even. An earlier version faked the
in-between rates by alternating 16.67ms and 33.33ms frames, which gave the right average
fps but juddered badly, because the apparent speed of anything moving swung by 2:1 from
frame to frame. Measured on `game.exe`, scroll speed varied 37.8% under the alternating
scheme and 3.2% once each frame was held to 22.22ms.

**Multiplayer:** the speed keys change how fast the local client paces frames. They do not
touch game logic, units, or the network protocol, but the RA2 netcode does not expect a
client running ahead of its peers. Use the speed keys in single player.

## Compatibility

Verified against Steam Yuri's Revenge 1.001 (`gamemd.exe`) and Red Alert 2 (`game.exe`),
and against Mental Omega via Syringe 0.7.3.0.

The DLL checks the exe it lands in and refuses to patch anything it does not recognise, so
a mismatched build no-ops rather than crashing. Before every write, it compares the bytes in
memory with the exe file on disk. If another mod already changed any site a feature needs,
that whole feature stays off and `campaignspeed.log` names the site. The other features
still install.

## Build

Needs the VS 2022 x86 toolset.

```
build.cmd
```

The script outputs `RA2Enhancements.dll` into `_dist\` and copies it into the game folder
three levels above this folder.

## Source layout

| File | What |
| --- | --- |
| `src/ra2enhancements.cpp` | the install list, run from `DllMain` or from the Syringe hook |
| `src/gameprofile.cpp` | game detection and which ini each game reads |
| `src/patch.cpp` | the stub page, and `ForThisGame` in `patch.h`, which picks a table's row for the running exe |
| `../shared/patchguard.cpp` | the log, byte checks, the write guard, and call and jump writes, shared with `hwcursor` |
| `src/gamespeed.cpp` | speed steps, the frame pacer hook, and the window title |
| `src/commands.cpp` | the hotkey commands in the Keyboard menu and their default keys |
| `src/quicksave.cpp` | F5 and F9, the main menu quick load, and the pre-save particle cleanup |
| `src/missionsavename.cpp` | the Mental Omega mission name in mission start save titles |
| `src/selectvoice.cpp` | one voice on select-all |
| `src/shiftqueue.cpp` | shift-click queues five |
| `src/healerescort.cpp` | healer escort hooks |
| `src/escort.h` | pairs healers with armed units across the width of the army |
| `src/RA2Enhancements.dll.inj` | the one Syringe hook that runs the install list |

Every feature installs the same way on both load paths. When the `dsound.dll` proxy loads
the DLL, `DllMain` runs the install list. Under Syringe (`IsDebuggerPresent`), `DllMain` runs
before Syringe writes any hooks, so it waits, and the `.inj` hook runs the list instead.

`InstallPatch` in `../shared/patchguard.cpp` wraps each feature. It collects the feature's writes, checks
each site against the exe on disk, and applies all of them or none. A new feature needs only
a line in the install list and its writes through `WriteCall`, `WriteJmp` or `WriteBytes`. It
never needs its own `.inj` entry.

`IsDebuggerPresent` also tells Mental Omega from stock Yuri's Revenge: both run the same
`gamemd.exe`, but MO always launches through Syringe and reads `RA2MO.ini`.

The hardcoded addresses live in tables at the top of each section, one row per exe. The DLL
confirms the exe by matching bytes at an address nothing else patches, never at a hook site.

Credit: the speed hook sites come from Phobos, `src/Misc/Hooks.Gamespeed.cpp`.

## Licence

MIT, see [`LICENSE`](../LICENSE) at the repo root.

Not affiliated with or endorsed by EA. Red Alert 2 and Yuri's Revenge are EA's trademarks.
This ships no game content, so you need your own copy of the game.
