#include "patch.h"
#include "features.h"

enum { MISSION_ATTACK = 1, MISSION_GUARD = 5, MISSION_ATTACK_MOVE = 0x1D };
enum { THREAT_AREA = 2 };
enum { ABS_FLAGS = 0x14, ABSFLAG_TECHNO = 0x01, ABSFLAG_FOOT = 0x04 };
enum { VT_GETCOORDS = 0x48 };
enum { SCAN_CALL_LEN = 6, IDLE_CHECK_LEN = 15, STOP_LEN = 6, ARM_FLAG_AT = 0x54 };

typedef bool (__thiscall *CanAttackMoveFn)(void *techno);
typedef bool (__thiscall *InRangeFn)(void *techno, void *target);
typedef bool (__thiscall *ScanFn)(void *techno, void *coords, int method);
typedef int (__thiscall *MissionFn)(void *techno);
typedef void (__thiscall *ClearMegaFn)(void *foot);
typedef bool (__thiscall *HouseCheckFn)(void *house);
typedef int (__thiscall *GuardRangeFn)(void *techno, int mode);
typedef int *(__thiscall *GetCoordsFn)(void *obj, int *out);
typedef void (__cdecl *ArmFn)();

struct AttackMoveProfile
{
    const char *name;
    DWORD armFunc;
    BYTE *armFlag;
    DWORD canAttackMoveSlot;
    DWORD leashSites[2];
    DWORD leashSlot;
    DWORD scanSites[2];
    DWORD scanSlot;
    DWORD idleCheck;
    DWORD missionSlot;
    DWORD idleResume;
    DWORD idleClear;
    DWORD lineSite;
    DWORD lineFunc;
    DWORD stopSite;
    DWORD clearMegaSlot;
    DWORD megaMission;
    DWORD owner;
    DWORD controlledByHuman;
    DWORD guardRange;
    void ***selectedItems;
    int *selectedCount;
};

static const AttackMoveProfile kAttackMove[] = {
    {"gamemd", 0x00731AF0, (BYTE *)0x00B0FE58, 0x4C0, {0x004DF3C5, 0x004DF458}, 0x3B4,
     {0x004DF402, 0x004DF491}, 0x39C, 0x004DF4EA, 0x184, 0x004DF4CF, 0x004DF4F9, 0x004DC323,
     0x007049C0, 0x004C75DA, 0x4A8, 0x5C4, 0x21C, 0x0050B730, 0x00707E60,
     (void ***)0x00A8ECBC, (int *)0x00A8ECC8},
    {"game", 0x006F6570, (BYTE *)0x00AC1330, 0x44C, {0x004CDC85, 0x004CDD18}, 0x35C,
     {0x004CDCC2, 0x004CDD51}, 0x344, 0x004CDDAA, 0x16C, 0x004CDD8F, 0x004CDDB9, 0x004CAB73,
     0x006D0510, 0x004B6E6A, 0x434, 0x4B4, 0x1B4, 0x004F52F0, 0x006D31E0,
     (void ***)0x00A40C64, (int *)0x00A40C70},
};

static const BYTE kStopBytes[STOP_LEN] = {0x8B, 0x16, 0x6A, 0x03, 0x8B, 0xCE};

static const AttackMoveProfile *g_am;
static DWORD g_lineFunc;
static DWORD g_idleResume;
static DWORD g_idleClear;
static DWORD g_stopReturn;

template <class Fn>
static Fn Slot(void *obj, DWORD slot)
{
    return (*(Fn **)obj)[slot / sizeof(void *)];
}

static bool IsAttackMoving(const void *foot)
{
    return *(const int *)((const BYTE *)foot + g_am->megaMission) == MISSION_ATTACK_MOVE;
}

static bool PlayerOwned(void *techno)
{
    void *owner = *(void **)((BYTE *)techno + g_am->owner);
    return owner && ((HouseCheckFn)g_am->controlledByHuman)(owner);
}

static bool OwnSkirmishUnit(void *foot)
{
    return SinglePlayerMode() && PlayerOwned(foot);
}

void ArmAttackMove()
{
    if (!g_am)
        return;
    void **items = *g_am->selectedItems;
    for (int i = 0; i < *g_am->selectedCount; ++i)
    {
        void *obj = items[i];
        if (obj && (*((const BYTE *)obj + ABS_FLAGS) & ABSFLAG_TECHNO) &&
            Slot<CanAttackMoveFn>(obj, g_am->canAttackMoveSlot)(obj))
        {
            *g_am->armFlag = 1;
            return;
        }
    }
    ((ArmFn)g_am->armFunc)();
}

static bool __cdecl KeepTarget(void *foot, void *target)
{
    if (Slot<InRangeFn>(foot, g_am->leashSlot)(foot, target))
        return true;
    if (!target || !OwnSkirmishUnit(foot))
        return false;
    int me[3], it[3];
    Slot<GetCoordsFn>(foot, VT_GETCOORDS)(foot, me);
    Slot<GetCoordsFn>(target, VT_GETCOORDS)(target, it);
    double dx = me[0] - it[0], dy = me[1] - it[1];
    double reach = ((GuardRangeFn)g_am->guardRange)(foot, 1);
    return dx * dx + dy * dy <= reach * reach;
}

static bool __cdecl Scan(void *foot, void *coords, int method)
{
    ScanFn scan = Slot<ScanFn>(foot, g_am->scanSlot);
    if (scan(foot, coords, method))
        return true;
    return OwnSkirmishUnit(foot) && scan(foot, coords, THREAT_AREA);
}

static bool __cdecl ResumeAfterIdle(void *foot)
{
    int mission = Slot<MissionFn>(foot, g_am->missionSlot)(foot);
    return mission == MISSION_GUARD || (mission == MISSION_ATTACK && OwnSkirmishUnit(foot));
}

static void __cdecl TintLine(const void *foot, BYTE *color)
{
    if (IsAttackMoving(foot))
    {
        color[0] = 0xFF;
        color[1] = 0xFF;
        color[2] = 0x00;
    }
}

static void __cdecl StopAttackMove(void *techno)
{
    if ((*((const BYTE *)techno + ABS_FLAGS) & ABSFLAG_FOOT) && IsAttackMoving(techno) &&
        SinglePlayerMode())
        Slot<ClearMegaFn>(techno, g_am->clearMegaSlot)(techno);
}

static __declspec(naked) void KeepTargetCall()
{
    __asm {
        push dword ptr [esp + 4]
        push ecx
        call KeepTarget
        add esp, 8
        ret 4
    }
}

static __declspec(naked) void ScanCall()
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push ecx
        call Scan
        add esp, 0x0C
        ret 8
    }
}

static __declspec(naked) void IdleCheck()
{
    __asm {
        push esi
        call ResumeAfterIdle
        add esp, 4
        test al, al
        jz clear
        jmp dword ptr [g_idleResume]
    clear:
        jmp dword ptr [g_idleClear]
    }
}

static __declspec(naked) void LineCall()
{
    __asm {
        push ecx
        push edx
        lea eax, [esp + 0x24]
        push eax
        push esi
        call TintLine
        add esp, 8
        pop edx
        pop ecx
        jmp dword ptr [g_lineFunc]
    }
}

static __declspec(naked) void StopCheck()
{
    __asm {
        push eax
        push ecx
        push edx
        push esi
        call StopAttackMove
        add esp, 4
        pop edx
        pop ecx
        pop eax
        mov edx, [esi]
        push 3
        mov ecx, esi
        jmp dword ptr [g_stopReturn]
    }
}

static bool SlotCallAt(DWORD site, DWORD slot)
{
    BYTE want[SCAN_CALL_LEN] = {0xFF, 0x90};
    *(DWORD *)(want + 2) = slot;
    return BytesAt(site, want, sizeof(want));
}

static bool IdleCheckAt(const AttackMoveProfile *p)
{
    BYTE want[IDLE_CHECK_LEN] = {0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x90, 0, 0, 0, 0,
                                 0x83, 0xF8, MISSION_GUARD, 0x74, 0};
    *(DWORD *)(want + 6) = p->missionSlot;
    want[14] = (BYTE)(p->idleResume - (p->idleCheck + IDLE_CHECK_LEN));
    return BytesAt(p->idleCheck, want, sizeof(want));
}

static bool ArmAt(const AttackMoveProfile *p)
{
    BYTE entry[6] = {0x51, 0xA1};
    *(DWORD *)(entry + 2) = (DWORD)p->selectedCount;
    BYTE set[7] = {0xC6, 0x05, 0, 0, 0, 0, 1};
    *(DWORD *)(set + 2) = (DWORD)p->armFlag;
    return BytesAt(p->armFunc, entry, sizeof(entry)) &&
           BytesAt(p->armFunc + ARM_FLAG_AT, set, sizeof(set));
}

void InstallAttackMove()
{
    const AttackMoveProfile *p = ForThisGame(kAttackMove);
    if (!p)
        return;
    if (!ArmAt(p) || !SlotCallAt(p->leashSites[0], p->leashSlot) ||
        !SlotCallAt(p->leashSites[1], p->leashSlot) || !SlotCallAt(p->scanSites[0], p->scanSlot) ||
        !SlotCallAt(p->scanSites[1], p->scanSlot) || !IdleCheckAt(p) ||
        !CallGoesTo(p->lineSite, p->lineFunc) || !BytesAt(p->stopSite, kStopBytes, STOP_LEN))
    {
        SpeedLog("  attack move: sites %X/%X/%X unexpected, skipped", p->armFunc, p->idleCheck,
                 p->lineSite);
        return;
    }
    g_am = p;
    g_lineFunc = p->lineFunc;
    g_idleResume = p->idleResume;
    g_idleClear = p->idleClear;
    g_stopReturn = p->stopSite + STOP_LEN;
    for (int i = 0; i < 2; ++i)
    {
        WriteCall(p->leashSites[i], KeepTargetCall, SCAN_CALL_LEN);
        WriteCall(p->scanSites[i], ScanCall, SCAN_CALL_LEN);
    }
    WriteJmp(p->idleCheck, IdleCheck, IDLE_CHECK_LEN);
    WriteCall(p->lineSite, LineCall);
    WriteJmp(p->stopSite, StopCheck, STOP_LEN);
    SpeedLog("  attack move ready, arm %X, update %X, idle %X, line %X, stop %X", p->armFunc,
             p->scanSites[0], p->idleCheck, p->lineSite, p->stopSite);
}
