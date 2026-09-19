#include "patch.h"
#include "features.h"

enum { MISSION_AREA_GUARD = 11 };
enum { THREAT_AREA = 2, THREAT_RANGE_AND_AREA = 3 };
enum { PASSIVE_CHECK_LEN = 10, SCAN_DONE_LEN = 6 };

typedef bool (__thiscall *HouseCheckFn)(void *house);
typedef const BYTE *(__thiscall *GetTypeFn)(const BYTE *techno);

struct RangedGuardProfile
{
    const char *name;
    DWORD passiveCheck;
    DWORD passiveRefuse;
    DWORD scanPush;
    DWORD scanDone;
    DWORD threatRange;
    DWORD controlledByHuman;
    BYTE humanSig[13];
    DWORD typePassive;
    DWORD typeSpawns;
    DWORD mission;
    DWORD owner;
    DWORD target;
    DWORD getTypeSlot;
};

static const RangedGuardProfile kRangedGuard[] = {
    {"gamemd", 0x007091FC, 0x0070927D, 0x004D6EFB, 0x004D6F0C, 0x00707E60, 0x0050B730,
     {0xA1, 0x38, 0xB2, 0xA8, 0x00, 0x85, 0xC0, 0x8A, 0x81, 0xEC, 0x01, 0x00, 0x00},
     0xD99, 0xD58, 0xAC, 0x21C, 0x2B4, 0x84},
    {"game", 0x006D40F9, 0x006D4175, 0x004C5CD3, 0x004C5CE4, 0x006D31E0, 0x004F52F0,
     {0xA1, 0x98, 0xD2, 0xA3, 0x00, 0x85, 0xC0, 0x8A, 0x81, 0x34, 0x01, 0x00, 0x00},
     0xB49, 0xB08, 0x98, 0x1B4, 0x228, 0x80},
};

static const BYTE kScanPush[5] = {0x6A, THREAT_AREA, 0x8B, 0x11, 0x50};
static const BYTE kThreatRangeEntry[7] = {0x53, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x10};

static const RangedGuardProfile *g_rangedGuard;
static DWORD g_passiveAccept;
static DWORD g_passiveRefuse;
static DWORD g_scanPushReturn;
static DWORD g_scanDoneReturn;
static DWORD g_threatRangeReturn;
static DWORD g_targetOffset;
static const BYTE *g_rangeScan;

static bool IsPlayerAreaGuardSpawner(const BYTE *type, const BYTE *techno)
{
    const RangedGuardProfile *p = g_rangedGuard;
    if (!type || !techno || type[p->typePassive] ||
        !*(void *const *)(type + p->typeSpawns) ||
        *(const int *)(techno + p->mission) != MISSION_AREA_GUARD || !SinglePlayerMode())
        return false;
    void *owner = *(void *const *)(techno + p->owner);
    return owner && ((HouseCheckFn)p->controlledByHuman)(owner);
}

static bool __cdecl MayAcquire(const BYTE *type, const BYTE *techno)
{
    return type &&
           (type[g_rangedGuard->typePassive] || IsPlayerAreaGuardSpawner(type, techno));
}

static int __cdecl ScanMethod(const BYTE *techno)
{
    const RangedGuardProfile *p = g_rangedGuard;
    const BYTE *type = (*(GetTypeFn *const *)techno)[p->getTypeSlot / sizeof(void *)](techno);
    g_rangeScan = IsPlayerAreaGuardSpawner(type, techno) ? techno : NULL;
    return g_rangeScan ? THREAT_RANGE_AND_AREA : THREAT_AREA;
}

static __declspec(naked) void PassiveAcquireCheck()
{
    __asm {
        push ecx
        push edx
        push esi
        push eax
        call MayAcquire
        add esp, 8
        pop edx
        pop ecx
        test al, al
        jz refused
        jmp dword ptr [g_passiveAccept]
    refused:
        jmp dword ptr [g_passiveRefuse]
    }
}

static __declspec(naked) void AreaScanMethod()
{
    __asm {
        push eax
        push ecx
        push esi
        call ScanMethod
        add esp, 4
        mov edx, eax
        pop ecx
        pop eax
        push edx
        mov edx, [ecx]
        push eax
        jmp dword ptr [g_scanPushReturn]
    }
}

static __declspec(naked) void EndRangeScan()
{
    __asm {
        mov dword ptr [g_rangeScan], 0
        mov edx, g_targetOffset
        cmp [esi + edx], ebx
        jmp dword ptr [g_scanDoneReturn]
    }
}

static __declspec(naked) void WeaponThreatRange()
{
    __asm {
        cmp ecx, g_rangeScan
        jne stock
        cmp dword ptr [esp + 4], 0
        jne stock
        xor eax, eax
        ret 4
    stock:
        push ebx
        push esi
        push edi
        mov edi, [esp + 0x10]
        jmp dword ptr [g_threatRangeReturn]
    }
}

static bool PassiveCheckLooksRight(const RangedGuardProfile *p)
{
    BYTE want[PASSIVE_CHECK_LEN] = {0x8A, 0x88, 0, 0, 0, 0, 0x84, 0xC9, 0x74};
    *(DWORD *)(want + 2) = p->typePassive;
    want[9] = (BYTE)(p->passiveRefuse - (p->passiveCheck + PASSIVE_CHECK_LEN));
    return BytesAt(p->passiveCheck, want, sizeof(want));
}

static bool ScanDoneLooksRight(const RangedGuardProfile *p)
{
    BYTE want[SCAN_DONE_LEN] = {0x39, 0x9E};
    *(DWORD *)(want + 2) = p->target;
    return BytesAt(p->scanDone, want, sizeof(want));
}

void InstallRangedGuard()
{
    const RangedGuardProfile *p = ForThisGame(kRangedGuard);
    if (!p)
        return;
    if (!PassiveCheckLooksRight(p) || !ScanDoneLooksRight(p) ||
        !BytesAt(p->scanPush, kScanPush, sizeof(kScanPush)) ||
        !BytesAt(p->threatRange, kThreatRangeEntry, sizeof(kThreatRangeEntry)) ||
        !BytesAt(p->controlledByHuman, p->humanSig, sizeof(p->humanSig)))
    {
        SpeedLog("  ranged guard: sites %X/%X unexpected, skipped", p->passiveCheck, p->scanPush);
        return;
    }
    g_rangedGuard = p;
    g_passiveAccept = p->passiveCheck + PASSIVE_CHECK_LEN;
    g_passiveRefuse = p->passiveRefuse;
    g_scanPushReturn = p->scanPush + sizeof(kScanPush);
    g_scanDoneReturn = p->scanDone + SCAN_DONE_LEN;
    g_threatRangeReturn = p->threatRange + sizeof(kThreatRangeEntry);
    g_targetOffset = p->target;
    WriteJmp(p->passiveCheck, PassiveAcquireCheck, PASSIVE_CHECK_LEN);
    WriteJmp(p->scanPush, AreaScanMethod, sizeof(kScanPush));
    WriteJmp(p->scanDone, EndRangeScan, SCAN_DONE_LEN);
    WriteJmp(p->threatRange, WeaponThreatRange, sizeof(kThreatRangeEntry));
    SpeedLog("  ranged guard ready at %X, %X, %X and %X", p->passiveCheck, p->scanPush,
             p->scanDone, p->threatRange);
}
