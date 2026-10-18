#include <intrin.h>
#include "patch.h"
#include "features.h"

enum { UNIT_VTABLE = 0x007F5C70, SLOT_SET_DEST = 0x480, SLOT_TYPE = 0x84, MAX_LINES = 400 };
enum { OFF_MISSION = 0xAC, OFF_OWNER = 0x21C, OFF_TARGET = 0x2B4, OFF_ARCHIVE = 0x218 };
enum { TYPE_PASSIVE = 0xD99, TYPE_SPAWNS = 0xD58, TYPE_ID = 0x24 };

typedef void (__thiscall *SetDestFn)(BYTE *techno, void *dest, bool rule);
typedef const BYTE *(__thiscall *GetTypeFn)(const BYTE *techno);
typedef bool (__thiscall *HouseCheckFn)(void *house);

static SetDestFn g_origSetDest;
static int g_lines;

static void __fastcall LoggedSetDestination(BYTE *techno, void *, void *dest, bool rule)
{
    DWORD caller = (DWORD)_ReturnAddress();
    if (g_lines < MAX_LINES)
    {
        const BYTE *type = (*(GetTypeFn *const *)techno)[SLOT_TYPE / sizeof(void *)](techno);
        void *owner = *(void **)(techno + OFF_OWNER);
        if (type && !type[TYPE_PASSIVE] && *(void *const *)(type + TYPE_SPAWNS) && owner &&
            ((HouseCheckFn)0x0050B730)(owner))
        {
            ++g_lines;
            SpeedLog("  rgdiag %s %p dest %p rule %d caller %X mission %d target %p archive %p",
                     (const char *)(type + TYPE_ID), techno, dest, rule, caller,
                     *(int *)(techno + OFF_MISSION), *(void **)(techno + OFF_TARGET),
                     *(void **)(techno + OFF_ARCHIVE));
        }
    }
    g_origSetDest(techno, dest, rule);
}

void InstallRangedGuardDiag()
{
    const GameProfile *g = DetectGame();
    if (!g || strcmp(g->name, "gamemd") != 0)
        return;
    void **slot = (void **)(UNIT_VTABLE + SLOT_SET_DEST);
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old))
        return;
    g_origSetDest = (SetDestFn)*slot;
    *slot = (void *)LoggedSetDestination;
    VirtualProtect(slot, sizeof(void *), old, &old);
    SpeedLog("  rgdiag on, UnitClass SetDestination was %p", (void *)g_origSetDest);
}
