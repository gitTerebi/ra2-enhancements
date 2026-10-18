#include "patch.h"
#include "features.h"

enum { MISSION_AREA_GUARD = 11 };
enum { THREAT_AREA = 2, THREAT_RANGE_AND_AREA = 3 };
enum { PASSIVE_CHECK_LEN = 10, SCAN_DONE_LEN = 6 };

typedef bool (__thiscall *HouseCheckFn)(void *house);
typedef const BYTE *(__thiscall *GetTypeFn)(const BYTE *techno);
typedef bool (__thiscall *InRangeFn)(BYTE *techno, void *target);
typedef void (__thiscall *SetTargetFn)(BYTE *techno, void *target);
typedef void (__thiscall *ApproachFn)(BYTE *techno);
typedef void (__thiscall *ApproachFlagFn)(BYTE *techno, bool flag);

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
    DWORD approach;
    DWORD approachSlot;
    bool approachTakesFlag;
    DWORD noTarget;
    DWORD inRangeSlot;
    DWORD setTargetSlot;
};

static const RangedGuardProfile kRangedGuard[] = {
    {"gamemd", 0x007091FC, 0x0070927D, 0x004D6EFB, 0x004D6F0C, 0x00707E60, 0x0050B730,
     {0xA1, 0x38, 0xB2, 0xA8, 0x00, 0x85, 0xC0, 0x8A, 0x81, 0xEC, 0x01, 0x00, 0x00},
     0xD99, 0xD58, 0xAC, 0x21C, 0x2B4, 0x84, 0x004D6F2D, 0x53C, true, 0x004D6F21, 0x3B4, 0x3C8},
    {"game", 0x006D40F9, 0x006D4175, 0x004C5CD3, 0x004C5CE4, 0x006D31E0, 0x004F52F0,
     {0xA1, 0x98, 0xD2, 0xA3, 0x00, 0x85, 0xC0, 0x8A, 0x81, 0x34, 0x01, 0x00, 0x00},
     0xB49, 0xB08, 0x98, 0x1B4, 0x228, 0x80, 0x004C5D05, 0x4C0, false, 0x004C5CF9, 0x35C, 0x370},
};

static const BYTE kScanPush[5] = {0x6A, THREAT_AREA, 0x8B, 0x11, 0x50};
static const BYTE kThreatRangeEntry[7] = {0x53, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x10};
static const BYTE kNoTargetEntry[6] = {0x8B, 0x16, 0x8B, 0xCE, 0xFF, 0x92};

static const RangedGuardProfile *g_rangedGuard;
static DWORD g_passiveAccept;
static DWORD g_passiveRefuse;
static DWORD g_scanPushReturn;
static DWORD g_scanDoneReturn;
static DWORD g_threatRangeReturn;
static DWORD g_targetOffset;
static DWORD g_approachReturn;
static DWORD g_noTargetPath;
static const BYTE *g_rangeScan;

template <class Fn>
static Fn Slot(const BYTE *obj, DWORD slot)
{
    return (*(Fn *const *)obj)[slot / sizeof(void *)];
}

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
    const BYTE *type = Slot<GetTypeFn>(techno, g_rangedGuard->getTypeSlot)(techno);
    g_rangeScan = IsPlayerAreaGuardSpawner(type, techno) ? techno : NULL;
    return g_rangeScan ? THREAT_RANGE_AND_AREA : THREAT_AREA;
}

static bool __cdecl HoldPositionOrApproach(BYTE *techno)
{
    const RangedGuardProfile *p = g_rangedGuard;
    const BYTE *type = Slot<GetTypeFn>(techno, p->getTypeSlot)(techno);
    void *target = *(void **)(techno + p->target);
    if (IsPlayerAreaGuardSpawner(type, techno) &&
        !Slot<InRangeFn>(techno, p->inRangeSlot)(techno, target))
    {
        Slot<SetTargetFn>(techno, p->setTargetSlot)(techno, NULL);
        return false;
    }
    if (p->approachTakesFlag)
        Slot<ApproachFlagFn>(techno, p->approachSlot)(techno, false);
    else
        Slot<ApproachFn>(techno, p->approachSlot)(techno);
    return true;
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

static __declspec(naked) void AreaGuardApproach()
{
    __asm {
        push esi
        call HoldPositionOrApproach
        add esp, 4
        test al, al
        jz dropped
        jmp dword ptr [g_approachReturn]
    dropped:
        jmp dword ptr [g_noTargetPath]
    }
}

static size_t ApproachBytes(const RangedGuardProfile *p, BYTE *out)
{
    size_t n = 0;
    out[n++] = 0x8B;
    out[n++] = 0x06;
    if (p->approachTakesFlag)
        out[n++] = 0x53;
    out[n++] = 0x8B;
    out[n++] = 0xCE;
    out[n++] = 0xFF;
    out[n++] = 0x90;
    *(DWORD *)(out + n) = p->approachSlot;
    return n + sizeof(DWORD);
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
    BYTE approach[11];
    size_t approachLen = ApproachBytes(p, approach);
    if (!PassiveCheckLooksRight(p) || !ScanDoneLooksRight(p) ||
        !BytesAt(p->approach, approach, approachLen) ||
        !BytesAt(p->noTarget, kNoTargetEntry, sizeof(kNoTargetEntry)) ||
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
    g_approachReturn = p->approach + approachLen;
    g_noTargetPath = p->noTarget;
    WriteJmp(p->passiveCheck, PassiveAcquireCheck, PASSIVE_CHECK_LEN);
    WriteJmp(p->approach, AreaGuardApproach, approachLen);
    WriteJmp(p->scanPush, AreaScanMethod, sizeof(kScanPush));
    WriteJmp(p->scanDone, EndRangeScan, SCAN_DONE_LEN);
    WriteJmp(p->threatRange, WeaponThreatRange, sizeof(kThreatRangeEntry));
    SpeedLog("  ranged guard ready at %X, %X, %X, %X and %X", p->passiveCheck, p->scanPush,
             p->scanDone, p->threatRange, p->approach);
}
