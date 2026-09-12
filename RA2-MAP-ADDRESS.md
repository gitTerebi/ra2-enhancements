# RA2 Map singleton: verified from the local executable

Examined 2026-09-12 using pefile and Capstone, without modifying the game.
game.exe SHA-256: `73288c03b58d370be268ca6d156b4e33bfdb2066dc980359467d8852ff3b00df`
Image base: `0x00400000`. Addresses below are virtual addresses.

| Item | RA2 game.exe | YR gamemd.exe comparison |
| --- | --- | --- |
| Map / Mouse / GScreen singleton object | `0x008324E0` | `0x0087F7E8` |
| Mouse primary vtable | `0x0079A954` | `0x007E1964` |
| GetInputAndUpdate, vtable byte offset +0x24 | `0x004E09D0` | `0x004F4320` |
| Slot storage address | `0x0079A978` | `0x007E1988` |

The singleton address is the object itself, not a pointer variable. Its first
DWORD is the vtable pointer. Do not dereference the singleton address to obtain
the `this` pointer.

Evidence:

* Startup at `0x0040D060` loads `ECX = 0x008324E0` and calls the Mouse constructor
  at `0x0059F020`. At `0x0059F02A`, that constructor writes `0x0079A954` to `[ESI]`,
  where ESI is the incoming ECX.
* The vtable's +0x24 entry contains `0x004E09D0`.
* At `0x0054010C`, the game loads `ECX = 0x008324E0`, then directly calls
  `0x004E09D0` at `0x00540111`, after pushing three output addresses.
* Additional direct calls are at `0x00540A53`, `0x00624AB7`, and `0x00625D27`,
  each preceded by the same singleton loaded into ECX.
* The target reads mouse coordinates into two output pointers, reads a key into
  the third, invokes the object's +0x28 Update method at `0x004E0A89`, and returns
  with `ret 0x0C` at `0x004E0A92`. This matches YRpp's GetInputAndUpdate signature.
* The known YR singleton in local YRpp/GScreenClass.h has a matching vtable and
  instruction sequence in gamemd.exe; its corresponding first direct caller
  is `0x0055D8AB`. The remaining YR direct callers are `0x0055E253`,
  `0x006485B7`, and `0x00649827`, matching RA2 one for one. The YR constructor
  writes the vtable pointer at `0x0040D1B2`, the RA2 one at `0x0040D082`.

Hook implication: a sweep that patches only vtable +0x24 misses these direct
calls. Instrument the function entry or validated direct call sites to measure
the actual input/update cadence. Function entry bytes are
`83 EC 08 55 56 57 8B F9`; preserve whole instructions and the thiscall ABI.

This establishes the address statically. No runtime call count or scrolling
behavior was measured in this investigation; the game was closed. The address
alone does not establish that moving the camera here fixes terrain caching.

## Drawing during the simulation wait

The original idle loop at RA2 `0x00540A61` / YR `0x0055E261` loads the Tactical
singleton, invokes its primary vtable +0x5C with no arguments, then invokes
GScreen DrawOnTop (RA2 `0x004E0B30`, YR `0x004F4480`). YRpp/GScreenClass.h identifies
DrawOnTop as primary vtable +0x3C. Its RA2 body draws the tactical layers, calls
Map.Draw (+0x40), restores surface state and calls +0x44 at the end.

RenderScrollFrame reuses this sequence after moving the camera, on the same game
thread from the custom pacer's wait. It does not repeat GetInputAndUpdate or the
key-dispatch routine, which could consume commands or enter nested dialogs.
The camera deadline is separate from the simulation deadline. Draw overruns skip
missed camera deadlines rather than issuing catch-up draws. A slow draw can still
delay simulation: both run on the same thread.

## Shared menu speed and right-drag movement

The RA2 menu at `0x0054236F` stores `6 - slider position` in `0x00A40B24`;
menu initialization at `0x00542C25` reverses the mapping. The corresponding YR
ScrollRate field is `0x00A8EB70`. The shared curve reads that live field.

RA2's native right-drag handler is `0x0066A7A0`, called at `0x0066A332`.
Its movement calls at `0x0066AB56` and `0x0066AB7A` target `0x00499B60`.
The YR equivalents are `0x00693440`, `0x00692FD2`, `0x006937F6`,
`0x0069381A`, and `0x004A9840` respectively. Only these three validated call
sites are replaced; native capture, thresholds and release remain intact.

RA2 Scroll_Map delegates to `0x006A8B70`. Its writes at `0x006A8C5A` and
`0x006A8C62` update pending camera position +0xD74/+0xD78, not the current
position +0xD64/+0xD68. The extra-frame trigger therefore checks pending position
changes before invoking the native render update.
