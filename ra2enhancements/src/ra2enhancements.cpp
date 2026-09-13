#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "gameprofile.h"
#include "escort.h"

struct SelectSite
{
    DWORD callAddr;
    BYTE callSig[10];
    BYTE objReg;
    DWORD resumeAddr;
    DWORD doneAddr;
    BYTE doneLen;
    BYTE doneSig[7];
};

struct FixProfile
{
    const char *name;
    DWORD allowVoice;
    DWORD selectSlot;
    SelectSite typeSelect;
    SelectSite combatantSelect;
};

enum { REG_ESI = 0xCE, REG_EDI = 0xCF };

static const FixProfile kFixes[] = {
    {"gamemd", 0x00822CF2, 0x14C,
     {0x00732B3D, {0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x90, 0x4C, 0x01, 0x00, 0x00}, REG_ESI, 0x00732B47,
      0x00732B4C, 5, {0xA1, 0xC8, 0xEC, 0xA8, 0x00}},
     {0x007324A0, {0x8B, 0x17, 0x8B, 0xCF, 0xFF, 0x92, 0x4C, 0x01, 0x00, 0x00}, REG_EDI, 0x007324AA,
      0x007324AF, 5, {0xA1, 0xC8, 0xEC, 0xA8, 0x00}}},
    {"game", 0x007D88D6, 0x140,
     {0x006F75AF, {0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x90, 0x40, 0x01, 0x00, 0x00}, REG_ESI, 0x006F75B9,
      0x006F75C7, 7, {0x83, 0xCA, 0xFF, 0x8D, 0x4C, 0x24, 0x1C}},
     {0x006F6F1F, {0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x90, 0x40, 0x01, 0x00, 0x00}, REG_ESI, 0x006F6F29,
      0x006F6F2E, 5, {0xA1, 0x70, 0x0C, 0xA4, 0x00}}},
};

static BYTE *g_allowVoice;
static DWORD g_selectSlot;

static void __fastcall SelectThenMute(void *obj)
{
    typedef bool (__thiscall *SelectFn)(void *);
    (*(SelectFn **)obj)[g_selectSlot / 4](obj);
    *g_allowVoice = 0;
}

static void __fastcall UnmuteC()
{
    *g_allowVoice = 1;
}

static void UseProfile(const FixProfile &f)
{
    g_allowVoice = (BYTE *)f.allowVoice;
    g_selectSlot = f.selectSlot;
}

static const FixProfile *SyringeProfile()
{
    static bool s_done;
    static const FixProfile *s_profile;
    if (s_done)
        return s_profile;
    s_done = true;
    if (!HostIsGame())
        return NULL;
    for (int i = 0; i < _countof(kFixes); ++i)
        if (strcmp(kFixes[i].name, "gamemd") == 0)
            return s_profile = &kFixes[i];
    return NULL;
}

EXPORT_FUNC(TypeSelect_SelectOne)
{
    const FixProfile *f = SyringeProfile();
    if (!f)
        return 0;
    UseProfile(*f);
    SelectThenMute((void *)R->_ESI);
    return f->typeSelect.resumeAddr;
}

EXPORT_FUNC(TypeSelect_LoopDone)
{
    const FixProfile *f = SyringeProfile();
    if (f)
    {
        UseProfile(*f);
        UnmuteC();
    }
    return 0;
}

EXPORT_FUNC(CombatantSelect_SelectOne)
{
    const FixProfile *f = SyringeProfile();
    if (!f)
        return 0;
    UseProfile(*f);
    SelectThenMute((void *)R->_EDI);
    return f->combatantSelect.resumeAddr;
}

EXPORT_FUNC(CombatantSelect_LoopDone)
{
    const FixProfile *f = SyringeProfile();
    if (f)
    {
        UseProfile(*f);
        UnmuteC();
    }
    return 0;
}

enum { EV_PRODUCE = 0x0E, EV_ABANDON = 0x10, EV_ABANDON_ALL = 0x2E };
enum { OUTLIST_MAX = 0x80, OUTLIST_MASK = 0x7F, EVENT_SIZE = 111, SHIFT_BATCH = 5 };

typedef void *(__fastcall *FillEventFn)(void *self, void *edx, int player, int kind, int absType,
                                        int idx, int isNaval);
typedef DWORD(WINAPI *TimeGetTimeFn)(void);

struct QueueProfile
{
    const char *name;
    DWORD fillEvent;
    DWORD produceSites[2];
    DWORD abandonSites[2];
    DWORD *outCount;
    DWORD *outIndex;
    BYTE *outRing;
    DWORD *outStamps;
    TimeGetTimeFn *timeGetTime;
};

static const QueueProfile kQueues[] = {
    {"gamemd", 0x004C6970,
     {0x006AB511, 0x006AB76E},
     {0x006AB1FF, 0x006AB231},
     (DWORD *)0x00A802C8, (DWORD *)0x00A802D0, (BYTE *)0x00A802D4, (DWORD *)0x00A83A54,
     (TimeGetTimeFn *)0x007E1530},
    {"game", 0x004B6280,
     {0x0068119E, 0x006813EF},
     {0x00680B42, 0x00680EBE},
     (DWORD *)0x00A32338, (DWORD *)0x00A32340, (BYTE *)0x00A32344, (DWORD *)0x00A35AC4,
     (TimeGetTimeFn *)0x0079A524},
};

static const QueueProfile *g_queue;

static void PushOutList(const void *event)
{
    if (*g_queue->outCount >= OUTLIST_MAX)
        return;
    DWORD i = *g_queue->outIndex;
    memcpy(g_queue->outRing + i * EVENT_SIZE, event, EVENT_SIZE);
    g_queue->outStamps[i] = (*g_queue->timeGetTime)();
    *g_queue->outIndex = (i + 1) & OUTLIST_MASK;
    ++*g_queue->outCount;
}

static void *__fastcall FillEventHook(void *self, void *edx, int player, int kind, int absType,
                                      int idx, int isNaval)
{
    FillEventFn orig = (FillEventFn)g_queue->fillEvent;
    if (!(GetKeyState(VK_SHIFT) & 0x8000))
        return orig(self, 0, player, kind, absType, idx, isNaval);

    if (kind == EV_ABANDON_ALL)
        kind = EV_ABANDON;
    for (int n = 1; n < SHIFT_BATCH; ++n)
        PushOutList(orig(self, 0, player, kind, absType, idx, isNaval));
    return orig(self, 0, player, kind, absType, idx, isNaval);
}

static bool CallGoesTo(DWORD at, DWORD target)
{
    const BYTE *p = (const BYTE *)at;
    if (IsBadReadPtr(p, 5) || p[0] != 0xE8)
        return false;
    return (DWORD)(at + 5 + *(const int *)(p + 1)) == target;
}

static bool WriteCall(DWORD at, const void *fn)
{
    BYTE patch[5];
    patch[0] = 0xE8;
    *(DWORD *)(patch + 1) = (DWORD)fn - (at + 5);

    DWORD old = 0;
    if (!VirtualProtect((void *)at, 5, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy((void *)at, patch, 5);
    VirtualProtect((void *)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)at, 5);
    return true;
}

static void InstallQueueBatch()
{
    const GameProfile *g = DetectGame();
    if (!g)
        return;
    const QueueProfile *q = NULL;
    for (int i = 0; i < _countof(kQueues); ++i)
        if (strcmp(kQueues[i].name, g->name) == 0)
            q = &kQueues[i];
    if (!q)
        return;

    for (int i = 0; i < 2; ++i)
        if (!CallGoesTo(q->produceSites[i], q->fillEvent) ||
            !CallGoesTo(q->abandonSites[i], q->fillEvent))
            return;

    g_queue = q;
    for (int i = 0; i < 2; ++i)
    {
        WriteCall(q->produceSites[i], FillEventHook);
        WriteCall(q->abandonSites[i], FillEventHook);
    }
}

enum { ACT_NONE = 0, ACT_MOVE = 1, ACT_NOMOVE = 2, ACT_ATTACK = 5, ACT_HEALER_GUARD = 0x3B };
enum { ABSFLAG_FOOT = 0x04 };
enum { MISSION_AREA_GUARD = 11 };
enum { RTTI_UNIT = 1, RTTI_INFANTRY = 0x0F };
enum { VT_WHATAMI = 0x2C, VT_GETCOORDS = 0x48 };

typedef int (__thiscall *CombatDamageFn)(void *techno, int weaponIndex);
typedef bool (__thiscall *AssignMissionFn)(void *foot, int mission, void *target, void *dest,
                                           int unused);
typedef int (__thiscall *WhatAmIFn)(void *obj);
typedef int *(__thiscall *GetCoordsFn)(void *obj, int *out);

struct HealerProfile
{
    const char *name;
    DWORD clickSite;
    DWORD clickSlot;
    DWORD cellSite;
    DWORD cellSlot;
    DWORD combatDamage;
    DWORD assignMissionSlot;
    DWORD deactivatedOffset;
    void ***selectedItems;
    int *selectedCount;
};

static const HealerProfile kHealers[] = {
    {"gamemd", 0x004AE844, 0x144, 0x004AE927, 0x140, 0x006F3970, 0x378, 0x298,
     (void ***)0x00A8ECBC, (int *)0x00A8ECC8},
    {"game", 0x0049E964, 0x138, 0x0049EA47, 0x134, 0x006C16F0, 0x320, 0, (void ***)0x00A40C64,
     (int *)0x00A40C70},
};

static const BYTE kCombatDamageSig[16] = {0x53, 0x8B, 0x5C, 0x24, 0x08, 0x56, 0x57, 0x33,
                                          0xFF, 0x83, 0xFB, 0xFF, 0x8B, 0xF1, 0x0F, 0x85};

static const HealerProfile *g_healer;
static DWORD g_healerSlot;
static DWORD g_healerResume;
static DWORD g_healerCellSlot;
static DWORD g_healerCellResume;

static void SpeedLog(const char *fmt, ...);

static bool IsGroundFoot(void *obj)
{
    if (!obj || !(*((const BYTE *)obj + 0x14) & ABSFLAG_FOOT))
        return false;
    int rtti = (*(WhatAmIFn **)obj)[VT_WHATAMI / 4](obj);
    return rtti == RTTI_UNIT || rtti == RTTI_INFANTRY;
}

static bool IsHealer(void *obj)
{
    return IsGroundFoot(obj) && ((CombatDamageFn)g_healer->combatDamage)(obj, -1) < 0;
}

static void **g_escortItems;
static int g_escortSelected = -1;
static int g_escortHealerCount;
static void **g_escortHealers;
static void **g_escortTargets;

static void RebuildEscorts(void **items, int count, const int *targetPos)
{
    free(g_escortHealers);
    free(g_escortTargets);
    g_escortHealers = g_escortTargets = NULL;
    g_escortHealerCount = 0;
    g_escortItems = items;
    g_escortSelected = count;

    size_t n = count > 0 ? count : 1;
    void **healers = (void **)malloc(sizeof(void *) * n);
    void **armed = (void **)malloc(sizeof(void *) * n);
    void **targets = (void **)malloc(sizeof(void *) * n);
    int *healerPos = (int *)malloc(sizeof(int) * 3 * n);
    int *armedPos = (int *)malloc(sizeof(int) * 3 * n);
    int *assigned = (int *)malloc(sizeof(int) * n);
    int healerCount = 0, armedCount = 0;
    if (healers && armed && targets && healerPos && armedPos && assigned)
    {
        for (int i = 0; i < count; ++i)
        {
            void *obj = items[i];
            if (!IsGroundFoot(obj))
                continue;
            int damage = ((CombatDamageFn)g_healer->combatDamage)(obj, -1);
            if (damage < 0)
            {
                (*(GetCoordsFn **)obj)[VT_GETCOORDS / 4](obj, healerPos + healerCount * 3);
                healers[healerCount++] = obj;
            }
            else if (damage > 0)
            {
                (*(GetCoordsFn **)obj)[VT_GETCOORDS / 4](obj, armedPos + armedCount * 3);
                armed[armedCount++] = obj;
            }
        }
        if (AssignEscorts(healerPos, healerCount, armedPos, armedCount, targetPos, assigned))
        {
            for (int h = 0; h < healerCount; ++h)
                targets[h] = assigned[h] >= 0 ? armed[assigned[h]] : NULL;
            g_escortHealers = healers;
            g_escortTargets = targets;
            g_escortHealerCount = healerCount;
            healers = targets = NULL;
        }
    }
    free(healers);
    free(armed);
    free(targets);
    free(healerPos);
    free(armedPos);
    free(assigned);
}

static void *EscortFor(void *healer)
{
    for (int h = 0; h < g_escortHealerCount; ++h)
        if (g_escortHealers[h] == healer)
            return g_escortTargets[h];
    return NULL;
}

static void ForgetEscorts()
{
    g_escortSelected = -1;
}

static void ForgetEscortsIfFirst(void *obj)
{
    if (*g_healer->selectedCount > 0 && (*g_healer->selectedItems)[0] == obj)
        g_escortSelected = -1;
}

static void Escort(void *obj, const int *targetPos)
{
    void **items = *g_healer->selectedItems;
    int count = *g_healer->selectedCount;
    if (items != g_escortItems || count != g_escortSelected)
        RebuildEscorts(items, count, targetPos);

    DWORD off = g_healer->deactivatedOffset;
    if (off && *((const BYTE *)obj + off))
        return;
    void *escort = EscortFor(obj);
    AssignMissionFn assign = (*(AssignMissionFn **)obj)[g_healer->assignMissionSlot / 4];
    assign(obj, MISSION_AREA_GUARD, escort ? escort : obj, NULL, 0);
}

static bool __cdecl HealerObjectClick(void *obj, int action, void *target, int groupAction)
{
    ForgetEscortsIfFirst(obj);
    if (!target || !IsHealer(obj))
        return false;
    if (groupAction != ACT_ATTACK && action != ACT_NONE && action != ACT_MOVE &&
        action != ACT_NOMOVE && action != ACT_HEALER_GUARD)
        return false;
    int targetPos[3];
    (*(GetCoordsFn **)target)[VT_GETCOORDS / 4](target, targetPos);
    Escort(obj, targetPos);
    return true;
}

static bool __cdecl HealerCellClick(void *obj, const short *cell, int groupAction)
{
    ForgetEscortsIfFirst(obj);
    if (groupAction != ACT_ATTACK || !cell || !IsHealer(obj))
        return false;
    int targetPos[3] = {cell[0] * 256 + 128, cell[1] * 256 + 128, 0};
    Escort(obj, targetPos);
    return true;
}

static __declspec(naked) void HealerClickedAction()
{
    __asm {
        push ecx
        push edx
        mov eax, [esp + 0x44]
        push eax
        mov eax, [esp + 0x14]
        push eax
        mov eax, [esp + 0x14]
        push eax
        push ecx
        call HealerObjectClick
        add esp, 0x10
        pop edx
        pop ecx
        test al, al
        jnz escorted
        mov eax, g_healerSlot
        jmp dword ptr [edx + eax]
    escorted:
        mov eax, 1
        ret 0x0C
    }
}

static __declspec(naked) void HealerCellClickedAction()
{
    __asm {
        push ecx
        mov eax, [esp + 0x44]
        push eax
        mov eax, [esp + 0x10]
        push eax
        push ecx
        call HealerCellClick
        add esp, 0x0C
        pop ecx
        test al, al
        jnz escorted
        mov eax, g_healerCellSlot
        jmp dword ptr [ebp + eax]
    escorted:
        mov eax, 1
        ret 0x10
    }
}

static __declspec(naked) void HealerClickedActionSyringe()
{
    __asm {
        call HealerClickedAction
        jmp dword ptr [g_healerResume]
    }
}

static __declspec(naked) void HealerCellClickedActionSyringe()
{
    __asm {
        call HealerCellClickedAction
        jmp dword ptr [g_healerCellResume]
    }
}

static const HealerProfile *FindHealerProfile()
{
    const GameProfile *g = DetectGame();
    if (!g)
        return NULL;
    for (int i = 0; i < _countof(kHealers); ++i)
        if (strcmp(kHealers[i].name, g->name) == 0)
            return &kHealers[i];
    return NULL;
}

static bool UseHealerProfile(const HealerProfile *h)
{
    if (!h || IsBadReadPtr((const void *)h->combatDamage, sizeof(kCombatDamageSig)) ||
        memcmp((const void *)h->combatDamage, kCombatDamageSig, sizeof(kCombatDamageSig)) != 0)
        return false;
    g_healer = h;
    g_healerSlot = h->clickSlot;
    g_healerResume = h->clickSite + 6;
    g_healerCellSlot = h->cellSlot;
    g_healerCellResume = h->cellSite + 6;
    return true;
}

EXPORT_FUNC(HealerEscort_ClickedAction)
{
    const HealerProfile *h = FindHealerProfile();
    if (!h || h->clickSite != R->origin || !UseHealerProfile(h))
        return 0;
    return (DWORD)HealerClickedActionSyringe;
}

EXPORT_FUNC(HealerEscort_CellClickedAction)
{
    const HealerProfile *h = FindHealerProfile();
    if (!h || h->cellSite != R->origin || !UseHealerProfile(h))
        return 0;
    return (DWORD)HealerCellClickedActionSyringe;
}

static bool CallSiteLooksRight(DWORD site, BYTE modrm, DWORD slot)
{
    BYTE want[6] = {0xFF, modrm};
    *(DWORD *)(want + 2) = slot;
    return !IsBadReadPtr((const void *)site, 6) && memcmp((const void *)site, want, 6) == 0;
}

static void WriteCallSite(DWORD site, const void *stub)
{
    BYTE patch[6];
    patch[0] = 0xE8;
    *(DWORD *)(patch + 1) = (DWORD)stub - (site + 5);
    patch[5] = 0x90;

    DWORD old = 0;
    if (!VirtualProtect((void *)site, 6, PAGE_EXECUTE_READWRITE, &old))
        return;
    memcpy((void *)site, patch, 6);
    VirtualProtect((void *)site, 6, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)site, 6);
}

static void InstallHealerEscort()
{
    const HealerProfile *h = FindHealerProfile();
    if (!h)
        return;
    if (!CallSiteLooksRight(h->clickSite, 0x92, h->clickSlot) ||
        !CallSiteLooksRight(h->cellSite, 0x95, h->cellSlot) || !UseHealerProfile(h))
    {
        SpeedLog("  healer escort: sites %X/%X unexpected, skipped", h->clickSite, h->cellSite);
        return;
    }
    WriteCallSite(h->clickSite, HealerClickedAction);
    WriteCallSite(h->cellSite, HealerCellClickedAction);
    SpeedLog("  healer escort ready at %X and %X", h->clickSite, h->cellSite);
}

typedef bool (__stdcall *LoadWrapFn)(const char *file);
typedef bool (__stdcall *SaveWrapFn)(const char *file, const wchar_t *description);

static bool BesideExe(const char *name, char *out, size_t outSize);

struct SaveProfile
{
    const char *name;
    DWORD confirmJump;
    DWORD savedBox;
    DWORD loadWrap;
    DWORD saveWrap;
    DWORD mouseInstance;
    DWORD session;
    DWORD sessionPauseCall;
    DWORD sessionPause;
    DWORD sessionResume;
    DWORD sessionRunningOffset;
    DWORD menuCall;
    DWORD menu;
    DWORD swapMiss;
    DWORD saveGameCall;
    DWORD saveGame;
    DWORD particles;
    DWORD particleSystems;
    DWORD particleSystemOffset;
    DWORD systemParticleList;
    DWORD deleteQueue;
    DWORD menuKeyCall;
    DWORD keyboardGet;
    DWORD loadMenuCall;
    DWORD loadMenu;
    DWORD mainMenuCall;
    DWORD mainMenu;
    DWORD inputBlocked;
};

static const SaveProfile kSaves[] = {
    {"gamemd", 0x00559353, 0x0055943A, 0x00559D60, 0x00559E40, 0x00887640, 0x00A8B238, 0x00559DBC,
     0x0069BB40, 0x0069BAB0, 0x30D8, 0x0048CE9C, 0x0048C8B0, 0x006CF3CF, 0x00559E85, 0x0067CEF0,
     0x00A83DC8, 0x00A80208, 0x124, 0xBC, 0x00B0F698, 0x00531E05, 0x0054F050, 0x0052E0E1, 0x005587F0, 0x0052DC9A,
     0x00531CC0, 0x00A8ED9C},
    {"game", 0x0053BCE7, 0x0053BDCE, 0x0053C680, 0x0053C760, 0x00839C68, 0x00A3D298, 0x0053C6DC,
     0x00672E70, 0x00672DE0, 0x3080, 0x00481DCC, 0x00481850, 0x0069FCAF, 0x0053C7A5, 0x006554A0,
     0x00A35E28, 0x00A32280, 0x114, 0xA8, 0x00AC0CD8, 0x0051706A, 0x00532320, 0x00513D68, 0x0053B190, 0x005138E9,
     0x00516F00, 0x00A40D44},
};

static const BYTE kSwapMissSig[22] = {0xC7, 0x44, 0x24, 0x10, 0x00, 0x00, 0x00, 0x00, 0xB8, 0x01, 0x00,
                                      0x00, 0x00, 0x99, 0xF7, 0x7C, 0x24, 0x10, 0x89, 0x44, 0x24, 0x10};
static DWORD g_clearedPointers;
static BYTE *g_swapMissStub;

struct GameVector
{
    void *vtable;
    void **items;
    int capacity;
    bool isInitialized;
    bool isAllocated;
    int count;
};

struct SwapPair
{
    DWORD oldPtr;
    DWORD newPtr;
};

static void __cdecl ClearSwapMiss(DWORD field, const BYTE *swizzard)
{
    if (++g_clearedPointers > 20)
        return;
    const SwapPair *pairs = *(const SwapPair **)(swizzard + 0x20);
    int count = *(const int *)(swizzard + 0x2C);
    DWORD owner = 0;
    for (int i = 0; i < count; ++i)
        if (pairs[i].newPtr <= field && pairs[i].newPtr > owner)
            owner = pairs[i].newPtr;
    DWORD vtable = owner && !IsBadReadPtr((void *)owner, 4) ? *(DWORD *)owner : 0;
    SpeedLog("  quick load: cleared field +%X of object vtable %08X", field - owner, vtable);
}

static const BYTE kConfirmJumpSig[4] = {0x74, 0x6A, 0x6A, 0x00};
static const BYTE kSavedBoxSig[7] = {0x8B, 0x16, 0x8B, 0xCE, 0xFF, 0x52, 0x20};
static const BYTE kLoadWrapSig[5] = {0x68, 0x1C, 0x04, 0x00, 0x00};
static const BYTE kSaveWrapSig[5] = {0x68, 0x53, 0x04, 0x00, 0x00};
static const char *const kQuickSaveFiles[2] = {"QUICK.SAV", "QUICK2.SAV"};
static const wchar_t *const kQuickSaveNames[2] = {L"Quick Save", L"Quick Save 2"};

static const SaveProfile *g_save;
static const char *g_quickLoadIni;

enum { WWKEY_RELEASE = 0x800 };

static bool BytesAt(DWORD at, const BYTE *sig, size_t n)
{
    return !IsBadReadPtr((const void *)at, n) && memcmp((const void *)at, sig, n) == 0;
}

static bool WriteBytes(DWORD at, const BYTE *bytes, size_t n)
{
    DWORD old = 0;
    if (!VirtualProtect((void *)at, n, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy((void *)at, bytes, n);
    VirtualProtect((void *)at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)at, n);
    return true;
}

static void InstallSaveTweaks()
{
    const GameProfile *g = DetectGame();
    if (!g)
        return;
    const SaveProfile *s = NULL;
    for (int i = 0; i < _countof(kSaves); ++i)
        if (strcmp(kSaves[i].name, g->name) == 0)
            s = &kSaves[i];
    if (!s)
        return;

    if (!BytesAt(s->confirmJump, kConfirmJumpSig, sizeof(kConfirmJumpSig)) ||
        !BytesAt(s->savedBox, kSavedBoxSig, sizeof(kSavedBoxSig)))
    {
        SpeedLog("  save boxes: sites %X/%X unexpected, skipped", s->confirmJump, s->savedBox);
    }
    else
    {
        static const BYTE skipConfirm = 0xEB;
        static const BYTE skipSaved[2] = {0xEB, 0x36};
        WriteBytes(s->confirmJump, &skipConfirm, 1);
        WriteBytes(s->savedBox, skipSaved, 2);
        SpeedLog("  save boxes removed at %X and %X", s->confirmJump, s->savedBox);
    }

    if (!BytesAt(s->loadWrap + 0x10, kLoadWrapSig, sizeof(kLoadWrapSig)) ||
        !BytesAt(s->saveWrap + 0x04, kSaveWrapSig, sizeof(kSaveWrapSig)))
    {
        SpeedLog("  quick save: wrappers %X/%X unexpected, skipped", s->loadWrap, s->saveWrap);
        return;
    }
    // The load wrapper starts by reading this WWMouseClass instance.
    static const BYTE mouseLoadSig[2] = {0x8B, 0x0D};
    if (!BytesAt(s->loadWrap, mouseLoadSig, sizeof(mouseLoadSig)) ||
        !BytesAt(s->loadWrap + 2, (const BYTE *)&s->mouseInstance, sizeof(s->mouseInstance)))
        return;
    BYTE runningTest[10] = {0x8A, 0x81, 0, 0, 0, 0, 0x84, 0xC0, 0x0F, 0x85};
    memcpy(runningTest + 2, &s->sessionRunningOffset, 4);
    if (!CallGoesTo(s->sessionPauseCall, s->sessionPause) ||
        !BytesAt(s->sessionResume, runningTest, sizeof(runningTest)) || !CallGoesTo(s->menuCall, s->menu) ||
        !CallGoesTo(s->saveGameCall, s->saveGame) || !CallGoesTo(s->menuKeyCall, s->keyboardGet) ||
        !CallGoesTo(s->loadMenuCall, s->loadMenu) || !CallGoesTo(s->mainMenuCall, s->mainMenu))
    {
        SpeedLog("  quick save: engine call sites unexpected, skipped");
        return;
    }
    g_save = s;
}

static int __cdecl ComparePointers(const void *a, const void *b)
{
    DWORD x = *(const DWORD *)a, y = *(const DWORD *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static void __cdecl ClearOrphanParticles()
{
    const GameVector *particles = (const GameVector *)g_save->particles;
    const GameVector *systems = (const GameVector *)g_save->particleSystems;
    const GameVector *queue = (const GameVector *)g_save->deleteQueue;

    int owned = queue->count;
    for (int j = 0; j < systems->count; ++j)
        owned += ((const GameVector *)((BYTE *)systems->items[j] + g_save->systemParticleList))->count;
    DWORD *known = (DWORD *)malloc((owned + 1) * sizeof(DWORD));
    if (!known)
        return;
    int n = 0;
    for (int j = 0; j < queue->count; ++j)
        known[n++] = (DWORD)queue->items[j];
    for (int j = 0; j < systems->count; ++j)
    {
        const GameVector *list = (const GameVector *)((BYTE *)systems->items[j] + g_save->systemParticleList);
        for (int k = 0; k < list->count; ++k)
            known[n++] = (DWORD)list->items[k];
    }
    qsort(known, n, sizeof(DWORD), ComparePointers);

    typedef void *(__thiscall *DeletingDtorFn)(void *self, int flags);
    int deleted = 0, cleared = 0;
    for (int i = particles->count - 1; i >= 0; --i)
    {
        void *particle = particles->items[i];
        if (!bsearch(&particle, known, n, sizeof(DWORD), ComparePointers))
        {
            (*(DeletingDtorFn **)particle)[0x20 / 4](particle, 1);
            ++deleted;
            continue;
        }
        void **system = (void **)((BYTE *)particle + g_save->particleSystemOffset);
        if (!*system)
            continue;
        bool alive = false;
        for (int j = 0; j < systems->count && !alive; ++j)
            alive = systems->items[j] == *system;
        if (!alive)
        {
            *system = NULL;
            ++cleared;
        }
    }
    free(known);
    if (deleted || cleared)
        SpeedLog("  save: deleted %d leaked particles, cleared %d pointers to deleted particle systems", deleted,
                 cleared);
}

static int NewestQuickSave()
{
    FILETIME written[2];
    bool found[2];
    for (int i = 0; i < 2; ++i)
    {
        char path[MAX_PATH];
        WIN32_FILE_ATTRIBUTE_DATA data;
        found[i] = BesideExe(kQuickSaveFiles[i], path, sizeof(path)) &&
                   GetFileAttributesExA(path, GetFileExInfoStandard, &data);
        if (found[i])
            written[i] = data.ftLastWriteTime;
    }
    if (found[0] && found[1])
        return CompareFileTime(&written[1], &written[0]) > 0 ? 1 : 0;
    return found[0] ? 0 : found[1] ? 1 : -1;
}

static bool g_quickSavePending;
static bool g_quickLoadPending;

static bool LoadQuickSave(int slot)
{
    BYTE clearMiss[sizeof(kSwapMissSig)] = {0xE9};
    *(DWORD *)(clearMiss + 1) = (DWORD)g_swapMissStub - (g_save->swapMiss + 5);
    memset(clearMiss + 5, 0x90, sizeof(clearMiss) - 5);
    g_clearedPointers = 0;
    bool tolerant = g_swapMissStub && BytesAt(g_save->swapMiss, kSwapMissSig, sizeof(kSwapMissSig)) &&
                    WriteBytes(g_save->swapMiss, clearMiss, sizeof(clearMiss));
    bool ok = ((LoadWrapFn)g_save->loadWrap)(kQuickSaveFiles[slot]);
    if (tolerant)
        WriteBytes(g_save->swapMiss, kSwapMissSig, sizeof(kSwapMissSig));
    ForgetEscorts();
    if (g_clearedPointers)
        SpeedLog("  quick load: cleared %d pointers to objects missing from the save", g_clearedPointers);
    SpeedLog("  quick load %s %s", kQuickSaveFiles[slot], ok ? "done" : "failed");
    return ok;
}

static void RunPendingSaves(bool allowed)
{
    bool save = g_quickSavePending, load = g_quickLoadPending;
    g_quickSavePending = g_quickLoadPending = false;
    if (!g_save || !allowed)
        return;
    if (save)
    {
        int slot = NewestQuickSave() == 0 ? 1 : 0;
        bool ok = ((SaveWrapFn)g_save->saveWrap)(kQuickSaveFiles[slot], kQuickSaveNames[slot]);
        SpeedLog("  quick save %s %s", kQuickSaveFiles[slot], ok ? "written" : "failed");
    }
    if (!load)
        return;
    int slot = NewestQuickSave();
    if (slot < 0)
    {
        SpeedLog("  quick load: no quick save yet");
        return;
    }
    typedef void (__thiscall *SessionResumeFn)(void *session);
    LoadQuickSave(slot);
    ((SessionResumeFn)g_save->sessionResume)((void *)g_save->session);
}

static bool g_menuQuickLoad;

static bool __cdecl MenuKeyPressed(int key)
{
    if (!g_save || (key & WWKEY_RELEASE))
        return false;
    int bound = VK_F9;
    char ini[MAX_PATH];
    if (g_quickLoadIni && BesideExe(g_quickLoadIni, ini, sizeof(ini)))
        bound = GetPrivateProfileIntA("Hotkey", "QuickLoad", VK_F9, ini);
    if (bound <= 0 || (key & 0x7FF) != (bound & 0x7FF))
        return false;
    if (NewestQuickSave() < 0)
    {
        SpeedLog("  menu quick load: no quick save yet");
        return false;
    }
    g_menuQuickLoad = true;
    return true;
}

static bool __cdecl MenuQuickLoad()
{
    g_menuQuickLoad = false;
    int slot = NewestQuickSave();
    if (slot < 0)
        return false;
    typedef void (__thiscall *MouseCountFn)(void *mouse);
    void *mouse = *(void **)g_save->mouseInstance;
    if (mouse)
        (*(MouseCountFn **)mouse)[0x0C / 4](mouse);
    bool ok = LoadQuickSave(slot);
    mouse = *(void **)g_save->mouseInstance;
    if (!ok && mouse)
        (*(MouseCountFn **)mouse)[0x10 / 4](mouse);
    return ok;
}

struct SpeedProfile
{
    const char *name;
    DWORD gameOptions;
    DWORD sites[2];
    DWORD delaySite;
    DWORD pacer;
    DWORD delaySlot;
    DWORD gameMode;
    DWORD netStart;
    DWORD netDelay;
    TimeGetTimeFn *timeGetTime;
    DWORD netPoll;
};

// Phobos src/Misc/Hooks.Gamespeed.cpp:11 hooks 0x69BAE7 and 0x55E160; 0x55D78D is delaySite.
// gameMode is read by the pacer itself at 55E197 / 540997; YRpp SessionClass.h:54.
static const SpeedProfile kSpeeds[] = {
    {"gamemd", 0x00A8EB60, {0x0055D774, 0x0069BAE7}, 0x0055D78C, 0x0055E160, 0x00887350, 0x00A8B238,
     0x00887328, 0x00887330, (TimeGetTimeFn *)0x007E1530, 0x0048D080},
    {"game", 0x00A40B18, {0x0053FFE4, 0x00672E17}, 0x0053FFFC, 0x00540960, 0x00839998, 0x00A3D298,
     0x00839970, 0x00839978, (TimeGetTimeFn *)0x0079A524, 0x00481FB0},
};

enum { GAMEMODE_CAMPAIGN = 0, GAMEMODE_SKIRMISH = 5 };

struct SpeedStep
{
    int fps;
    const char *label;
};

// RA2 paces frames in whole 60Hz ticks: OpenTS mainloop.cpp:295 and mainloop.cpp:610
static const SpeedStep kSteps[] = {
    {30, "1.00x  30fps"},
    {35, "1.17x  35fps"},
    {40, "1.33x  40fps"},
    {45, "1.50x  45fps"},
    {48, "1.60x  48fps"},
    {50, "1.67x  50fps"},
    {60, "2.00x  60fps"},
};

enum { STEP_DEFAULT = 3 };

static const char kIniSection[] = "RA2Enhancements";
static const char kIniKey[] = "GameSpeedStep";
static char g_iniPath[MAX_PATH];
static int g_step = STEP_DEFAULT;

static bool BesideExe(const char *name, char *out, size_t outSize)
{
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)outSize);
    if (!n || n >= outSize)
        return false;
    char *slash = strrchr(out, '\\');
    if (!slash || strlen(name) >= outSize - (slash + 1 - out))
        return false;
    strcpy(slash + 1, name);
    return true;
}

static void SpeedLog(const char *fmt, ...)
{
    char path[MAX_PATH];
    if (!BesideExe("campaignspeed.log", path, sizeof(path)))
        return;

    char line[512];
    va_list ap;
    va_start(ap, fmt);
    int len = wvsprintfA(line, fmt, ap);
    va_end(ap);

    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD wrote = 0;
    WriteFile(h, line, len, &wrote, NULL);
    WriteFile(h, "\r\n", 2, &wrote, NULL);
    CloseHandle(h);
}

static void LoadStep(const GameProfile *g)
{
    if (!BesideExe(ProfileIni(g), g_iniPath, sizeof(g_iniPath)))
    {
        g_iniPath[0] = 0;
        return;
    }
    int v = (int)GetPrivateProfileIntA(kIniSection, kIniKey, STEP_DEFAULT, g_iniPath);
    if (v < 0 || v >= _countof(kSteps))
        v = STEP_DEFAULT;
    g_step = v;
    SpeedLog("  ini=%s step=%d (%s)", g_iniPath, g_step, kSteps[g_step].label);
}

static void SaveStep()
{
    if (!g_iniPath[0])
        return;
    char v[16];
    wsprintfA(v, "%d", g_step);
    if (!WritePrivateProfileStringA(kIniSection, kIniKey, v, g_iniPath))
        SpeedLog("  ini write failed err=%d", GetLastError());
}

static void InstallAlternatingSpeed(const SpeedProfile *s);

static void InstallCampaignSpeed()
{
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    SpeedLog("attach exe=%s base=%X dbg=%d", exe, (DWORD)GetModuleHandleA(NULL),
             IsDebuggerPresent());

    const GameProfile *g = DetectGame();
    if (!g)
    {
        SpeedLog("  DetectGame failed");
        return;
    }
    LoadStep(g);

    const SpeedProfile *s = NULL;
    for (int i = 0; i < _countof(kSpeeds); ++i)
        if (strcmp(kSpeeds[i].name, g->name) == 0)
            s = &kSpeeds[i];
    if (!s)
    {
        SpeedLog("  no speed profile for %s", g->name);
        return;
    }
    SpeedLog("  profile=%s gameOptions=%X", s->name, s->gameOptions);

    BYTE want[10];
    want[0] = 0xC7;
    want[1] = 0x05;
    *(DWORD *)(want + 2) = s->gameOptions;
    *(DWORD *)(want + 6) = 2;

    for (int i = 0; i < 2; ++i)
    {
        if (IsBadReadPtr((const void *)s->sites[i], sizeof(want)))
        {
            SpeedLog("  site %X unreadable", s->sites[i]);
            return;
        }
        const BYTE *p = (const BYTE *)s->sites[i];
        SpeedLog("  site %X has %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 s->sites[i], p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9]);
        if (memcmp(p, want, sizeof(want)) != 0)
        {
            SpeedLog("  site %X signature mismatch, aborting", s->sites[i]);
            return;
        }
    }

    const BYTE *dp = (const BYTE *)s->delaySite;
    if (IsBadReadPtr(dp, 5) || dp[0] != 0xB9 || *(const DWORD *)(dp + 1) != 2)
    {
        SpeedLog("  delay site %X unexpected, aborting", s->delaySite);
        return;
    }

    InstallAlternatingSpeed(s);
}

static BYTE *g_stubs;
static BYTE *g_stubEnd;

static BYTE *Emit(const void *bytes, size_t n)
{
    memcpy(g_stubEnd, bytes, n);
    g_stubEnd += n;
    return g_stubEnd;
}

static void EmitByte(BYTE b) { Emit(&b, 1); }
static void EmitDword(DWORD d) { Emit(&d, 4); }

static void EmitJmp(DWORD target)
{
    EmitByte(0xE9);
    EmitDword(target - ((DWORD)g_stubEnd + 4));
}

static void EmitCallAbs(const void *fn)
{
    EmitByte(0xE8);
    EmitDword((DWORD)fn - ((DWORD)g_stubEnd + 4));
}

static bool WriteJmp(DWORD at, BYTE stolen, const void *stub)
{
    BYTE patch[16];
    patch[0] = 0xE9;
    *(DWORD *)(patch + 1) = (DWORD)stub - (at + 5);
    for (int i = 5; i < stolen; ++i)
        patch[i] = 0x90;

    DWORD old = 0;
    if (!VirtualProtect((void *)at, stolen, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy((void *)at, patch, stolen);
    VirtualProtect((void *)at, stolen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)at, stolen);
    return true;
}

static DWORD *g_delaySlot;
static const DWORD *g_gameMode;
static bool g_prevDown[2];
static HWND g_wnd;
static char g_baseTitle[160];

static BOOL CALLBACK FindGameWindow(HWND h, LPARAM lp)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || GetWindow(h, GW_OWNER))
        return TRUE;
    *(HWND *)lp = h;
    return FALSE;
}

static void ShowStep()
{
    if (!g_wnd || !IsWindow(g_wnd))
    {
        g_wnd = NULL;
        EnumWindows(FindGameWindow, (LPARAM)&g_wnd);
        if (!g_wnd)
            return;
        if (!g_baseTitle[0])
            GetWindowTextA(g_wnd, g_baseTitle, sizeof(g_baseTitle) - 40);
    }
    char title[224];
    wsprintfA(title, "%s   [ speed %s ]", g_baseTitle, kSteps[g_step].label);
    SetWindowTextA(g_wnd, title);
}

static bool GameFocused()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

static void StepSpeed(int dir)
{
    int next = g_step + dir;
    if (next >= 0 && next < _countof(kSteps))
    {
        g_step = next;
        SaveStep();
        SpeedLog("  step -> %s (%d fps)", kSteps[g_step].label, kSteps[g_step].fps);
    }
    ShowStep();
}

static void PollSpeedKeys(bool focused)
{
    const int keys[2] = {VK_OEM_4, VK_OEM_6};
    for (int i = 0; i < 2; ++i)
    {
        bool down = focused && (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
        if (down && !g_prevDown[i])
            StepSpeed(i ? 1 : -1);
        g_prevDown[i] = down;
    }
}

enum { CMD_SPEED_DOWN, CMD_SPEED_UP, CMD_QUICK_SAVE, CMD_QUICK_LOAD };

static const wchar_t *g_commandCategory = L"Interface";
static bool g_commandsReady;

static bool PacedMode()
{
    return g_gameMode && (*g_gameMode == GAMEMODE_CAMPAIGN || *g_gameMode == GAMEMODE_SKIRMISH);
}

class ModCommand
{
public:
    ModCommand(int action, const char *name, const wchar_t *uiName, const wchar_t *description)
        : m_action(action), m_name(name), m_uiName(uiName), m_description(description) {}
    virtual ~ModCommand() {}
    virtual const char *GetName() const { return m_name; }
    virtual const wchar_t *GetUIName() const { return m_uiName; }
    virtual const wchar_t *GetUICategory() const { return g_commandCategory; }
    virtual const wchar_t *GetUIDescription() const { return m_description; }
    virtual bool PreventCombinationOverride(int) const { return false; }
    virtual bool ExtraTriggerCondition(int key) const { return !(key & WWKEY_RELEASE); }
    virtual bool CheckLoop(int) const { return false; }
    virtual void Execute(int) const
    {
        switch (m_action)
        {
        case CMD_SPEED_DOWN:
        case CMD_SPEED_UP:
            if (PacedMode())
                StepSpeed(m_action == CMD_SPEED_UP ? 1 : -1);
            break;
        case CMD_QUICK_SAVE:
            g_quickSavePending = true;
            break;
        case CMD_QUICK_LOAD:
            g_quickLoadPending = true;
            break;
        }
    }

private:
    int m_action;
    const char *m_name;
    const wchar_t *m_uiName;
    const wchar_t *m_description;
};

struct CommandSpec
{
    int action;
    const char *name;
    const wchar_t *uiName;
    const wchar_t *description;
    int defaultKey;
};

static const CommandSpec kCommandSpecs[] = {
    {CMD_QUICK_SAVE, "QuickSave", L"Quick Save", L"Save over the older of the two quick saves.", VK_F5},
    {CMD_QUICK_LOAD, "QuickLoad", L"Quick Load", L"Load the newer of the two quick saves.", VK_F9},
    {CMD_SPEED_DOWN, "GameSpeedDown", L"Game Speed Down", L"Slow the game down one step.", VK_OEM_4},
    {CMD_SPEED_UP, "GameSpeedUp", L"Game Speed Up", L"Speed the game up one step.", VK_OEM_6},
};

static ModCommand *g_commands[_countof(kCommandSpecs)];

struct CommandVector
{
    void **vtable;
    void **items;
    int capacity;
    bool isReference;
    bool isAllocated;
    int count;
    int capacityIncrement;
};

typedef bool (__thiscall *SetCapacityFn)(CommandVector *self, int capacity, void **existing);
typedef const char *(__thiscall *CommandNameFn)(void *cmd);
typedef const wchar_t *(__thiscall *CommandCategoryFn)(void *cmd);

struct CommandProfile
{
    const char *name;
    DWORD keyboardLoad;
    CommandVector *commands;
    const char *keyboardIni;
};

static const CommandProfile kCommands[] = {
    {"gamemd", 0x00533D20, (CommandVector *)0x0087F658, "KeyboardMD.ini"},
    {"game", 0x00519240, (CommandVector *)0x00832358, "Keyboard.ini"},
};

static const CommandProfile *g_commandProfile;

static bool HasCommand(const CommandVector *v, const void *cmd)
{
    for (int i = 0; cmd && i < v->count; ++i)
        if (v->items[i] == cmd)
            return true;
    return false;
}

static bool AddCommand(CommandVector *v, void *cmd)
{
    if (v->count >= v->capacity)
    {
        if ((!v->isAllocated && v->capacity) || v->capacityIncrement <= 0)
            return false;
        SetCapacityFn grow = (SetCapacityFn)v->vtable[2];
        if (!grow(v, v->capacity + v->capacityIncrement, NULL))
            return false;
    }
    v->items[v->count++] = cmd;
    return true;
}

static void SeedDefaultKeys()
{
    char ini[MAX_PATH];
    if (!BesideExe(g_commandProfile->keyboardIni, ini, sizeof(ini)) ||
        GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES)
        return;

    char section[8192];
    DWORD len = GetPrivateProfileSectionA("Hotkey", section, sizeof(section), ini);
    if (len == 0 || len >= sizeof(section) - 2)
        return;

    for (int c = 0; c < _countof(kCommandSpecs); ++c)
    {
        const CommandSpec &spec = kCommandSpecs[c];
        const char *name = spec.name;
        if (GetPrivateProfileIntA("Hotkey", name, -1, ini) != -1)
            continue;
        for (const char *entry = section; *entry; entry += strlen(entry) + 1)
        {
            const char *eq = strchr(entry, '=');
            if (!eq || atoi(eq + 1) != spec.defaultKey)
                continue;
            char other[128];
            size_t n = eq - entry;
            if (n >= sizeof(other))
                continue;
            memcpy(other, entry, n);
            other[n] = 0;
            WritePrivateProfileStringA("Hotkey", other, "0", ini);
            SpeedLog("  unbound %s from key %d for %s", other, spec.defaultKey, name);
        }
        char value[16];
        wsprintfA(value, "%d", spec.defaultKey);
        WritePrivateProfileStringA("Hotkey", name, value, ini);
    }
}

static void __cdecl RegisterCommands()
{
    CommandVector *v = g_commandProfile->commands;
    for (int i = 0; i < v->count; ++i)
    {
        void *cmd = v->items[i];
        if (cmd && strcmp((*(CommandNameFn **)cmd)[1](cmd), "Options") == 0)
            g_commandCategory = (*(CommandCategoryFn **)cmd)[3](cmd);
    }
    int ready = 0;
    for (int c = 0; c < _countof(kCommandSpecs); ++c)
    {
        if (HasCommand(v, g_commands[c]))
        {
            ++ready;
            continue;
        }
        const CommandSpec &spec = kCommandSpecs[c];
        ModCommand *cmd = new ModCommand(spec.action, spec.name, spec.uiName, spec.description);
        if (AddCommand(v, cmd))
        {
            g_commands[c] = cmd;
            ++ready;
        }
        else
        {
            delete cmd;
        }
    }
    if (!g_commandsReady)
        SeedDefaultKeys();
    g_commandsReady = ready == _countof(kCommandSpecs);
    SpeedLog("  commands registered %d of %d", ready, _countof(kCommandSpecs));
}

static void __cdecl RunPendingSavesAfterFrame()
{
    RunPendingSaves(g_gameMode && *g_gameMode == GAMEMODE_CAMPAIGN);
}

static bool RedirectCall(DWORD site, const BYTE *stub)
{
    BYTE call[5] = {0xE8};
    *(DWORD *)(call + 1) = (DWORD)stub - (site + 5);
    return WriteBytes(site, call, sizeof(call));
}

static void InstallSaveHook()
{
    if (!g_save)
        return;
    if (!g_stubs)
    {
        g_stubs = (BYTE *)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!g_stubs)
        {
            g_save = NULL;
            return;
        }
        g_stubEnd = g_stubs;
    }

    BYTE *stub = g_stubEnd;
    EmitByte(0x60);
    EmitByte(0x9C);
    EmitCallAbs(RunPendingSavesAfterFrame);
    EmitByte(0x9D);
    EmitByte(0x61);
    EmitJmp(g_save->menu);

    g_swapMissStub = g_stubEnd;
    EmitByte(0x60);
    EmitByte(0x9C);
    EmitByte(0x56);
    EmitByte(0xFF); EmitByte(0x77); EmitByte(0x04);
    EmitCallAbs(ClearSwapMiss);
    EmitByte(0x83); EmitByte(0xC4); EmitByte(0x08);
    EmitByte(0x9D);
    EmitByte(0x61);
    EmitByte(0x8B); EmitByte(0x57); EmitByte(0x04);
    EmitByte(0xC7); EmitByte(0x02); EmitDword(0);
    EmitByte(0x83); EmitByte(0xC7); EmitByte(0x08);
    EmitByte(0x4D);
    EmitJmp(g_save->swapMiss + sizeof(kSwapMissSig));

    BYTE *saveStub = g_stubEnd;
    EmitByte(0x60);
    EmitByte(0x9C);
    EmitCallAbs(ClearOrphanParticles);
    EmitByte(0x9D);
    EmitByte(0x61);
    EmitJmp(g_save->saveGame);

    BYTE *menuKeyStub = g_stubEnd;
    EmitCallAbs((const void *)g_save->keyboardGet);
    EmitByte(0x50);
    EmitByte(0x52);
    EmitByte(0x51);
    EmitByte(0x50);
    EmitCallAbs(MenuKeyPressed);
    EmitByte(0x83); EmitByte(0xC4); EmitByte(0x04);
    EmitByte(0x59);
    EmitByte(0x5A);
    EmitByte(0x84); EmitByte(0xC0);
    EmitByte(0x58);
    EmitByte(0x74); EmitByte(0x08);
    EmitByte(0xC7); EmitByte(0x44); EmitByte(0x24); EmitByte(0x18); EmitDword(9);
    EmitByte(0xC3);

    BYTE *loadMenuStub = g_stubEnd;
    EmitByte(0x80); EmitByte(0x3D); EmitDword((DWORD)&g_menuQuickLoad); EmitByte(0x00);
    EmitByte(0x75); EmitByte(0x05);
    EmitJmp(g_save->loadMenu);
    EmitByte(0x51);
    EmitByte(0x52);
    EmitCallAbs(MenuQuickLoad);
    EmitByte(0x5A);
    EmitByte(0x59);
    EmitByte(0xC3);

    if (!RedirectCall(g_save->menuCall, stub))
    {
        g_save = NULL;
        return;
    }
    RedirectCall(g_save->saveGameCall, saveStub);
    RedirectCall(g_save->loadMenuCall, loadMenuStub);
    RedirectCall(g_save->menuKeyCall, menuKeyStub);

    BYTE *mainMenuStub = g_stubEnd;
    EmitByte(0xC6); EmitByte(0x05); EmitDword(g_save->inputBlocked); EmitByte(0x00);
    EmitJmp(g_save->mainMenu);
    RedirectCall(g_save->mainMenuCall, mainMenuStub);
    SpeedLog("  quick save ready at %X, F5 and F9 use %s and %s, F9 also works on the main menu",
             g_save->menuCall, kQuickSaveFiles[0], kQuickSaveFiles[1]);
}

static void InstallCommands()
{
    const GameProfile *g = DetectGame();
    if (!g)
        return;
    const CommandProfile *p = NULL;
    for (int i = 0; i < _countof(kCommands); ++i)
        if (strcmp(kCommands[i].name, g->name) == 0)
            p = &kCommands[i];
    if (!p)
        return;

    BYTE want[6] = {0x81, 0xEC};
    *(DWORD *)(want + 2) = strcmp(p->name, "gamemd") == 0 ? 0xCC : 0xBC;
    if (!BytesAt(p->keyboardLoad, want, sizeof(want)))
    {
        SpeedLog("  commands: keyboard load %X unexpected, skipped", p->keyboardLoad);
        return;
    }
    if (!g_stubs)
    {
        g_stubs = (BYTE *)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!g_stubs)
            return;
        g_stubEnd = g_stubs;
    }
    g_commandProfile = p;
    g_quickLoadIni = p->keyboardIni;

    BYTE *stub = g_stubEnd;
    EmitByte(0x60);
    EmitByte(0x9C);
    EmitCallAbs(RegisterCommands);
    EmitByte(0x9D);
    EmitByte(0x61);
    Emit(want, sizeof(want));
    EmitJmp(p->keyboardLoad + sizeof(want));
    if (WriteJmp(p->keyboardLoad, sizeof(want), stub))
        SpeedLog("  commands hook ready at %X", p->keyboardLoad);
}

static LONGLONG g_qpcFreq;
static LONGLONG g_nextFrame;
static HANDLE g_paceTimer;
static int g_pacedStep = -1;
typedef void (__cdecl *RenderScrollFrameFn)();
typedef LONGLONG (__cdecl *ScrollRenderDueFn)(LONGLONG now);
typedef LONGLONG (__cdecl *ScrollRenderCostFn)();
static RenderScrollFrameFn g_renderScrollFrame;
static ScrollRenderDueFn g_scrollRenderDue;
static ScrollRenderCostFn g_scrollRenderCost;
static FILE *g_paceTrace;
static int g_paceTraceLines;
static int g_scrollDraws, g_scrollSkips;

static void OpenPaceTrace()
{
    static bool s_checked;
    if (s_checked)
        return;
    s_checked = true;
    char ini[MAX_PATH], path[MAX_PATH], value[16];
    if (!BesideExe("ddraw.ini", ini, sizeof(ini)) || !BesideExe("pacetrace.csv", path, sizeof(path)))
        return;
    GetPrivateProfileStringA("ddraw", "frametrace", "", value, sizeof(value), ini);
    if (lstrcmpiA(value, "true") != 0 && lstrcmpA(value, "1") != 0)
        return;
    g_paceTrace = fopen(path, "w");
    if (!g_paceTrace)
        return;
    setvbuf(g_paceTrace, NULL, _IOFBF, 1 << 16);
    fprintf(g_paceTrace, "entry,deadline,period,reset,draws,skips,cost,exit\n");
}

static void PaceTrace(LONGLONG entry, LONGLONG deadline, LONGLONG period, bool reset, LONGLONG exit)
{
    if (!g_paceTrace)
        return;
    fprintf(g_paceTrace, "%lld,%lld,%lld,%d,%d,%d,%lld,%lld\n", entry, deadline, period, reset ? 1 : 0,
            g_scrollDraws, g_scrollSkips, g_scrollRenderCost ? g_scrollRenderCost() : 0, exit);
    if (++g_paceTraceLines % 200 == 0)
        fflush(g_paceTrace);
}

static void FindScrollRenderer()
{
    static bool s_searched;
    if (s_searched)
        return;
    HMODULE scroll = GetModuleHandleA("WasdScroll.dll");
    if (!scroll)
        return;
    s_searched = true;
    g_renderScrollFrame = (RenderScrollFrameFn)GetProcAddress(scroll, "RenderScrollFrame");
    g_scrollRenderDue = (ScrollRenderDueFn)GetProcAddress(scroll, "ScrollRenderDue");
    g_scrollRenderCost = (ScrollRenderCostFn)GetProcAddress(scroll, "ScrollRenderCost");
}

static void WaitUntil(LONGLONG wake, LARGE_INTEGER &now)
{
    LONGLONG remain = wake - now.QuadPart;
    LONGLONG spin = g_qpcFreq / 4000;
    if (g_paceTimer && remain > spin)
    {
        LARGE_INTEGER due;
        due.QuadPart = -((remain - spin) * 10000000 / g_qpcFreq);
        if (SetWaitableTimer(g_paceTimer, &due, 0, NULL, NULL, FALSE))
            WaitForSingleObject(g_paceTimer, INFINITE);
    }
    while (QueryPerformanceCounter(&now), now.QuadPart < wake)
        YieldProcessor();
}

static bool EnsureClock()
{
    if (g_qpcFreq)
        return true;
    LARGE_INTEGER f;
    if (!QueryPerformanceFrequency(&f))
        return false;
    g_qpcFreq = f.QuadPart;

    HANDLE(WINAPI * createTimerExW)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD) =
        (HANDLE(WINAPI *)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD))GetProcAddress(
            GetModuleHandleA("kernel32.dll"), "CreateWaitableTimerExW");
    if (createTimerExW)
        g_paceTimer = createTimerExW(NULL, NULL, 0x00000002 | 0x00000001, TIMER_ALL_ACCESS);
    if (!g_paceTimer)
        g_paceTimer = CreateWaitableTimerA(NULL, TRUE, NULL);
    return true;
}

typedef void (__cdecl *PollFn)();

static void ScrollDrawsUntil(LONGLONG deadline, LARGE_INTEGER &now, PollFn poll)
{
    FindScrollRenderer();
    const LONGLONG instant = g_qpcFreq / 10000;
    bool stalled = false;
    while (now.QuadPart < deadline)
    {
        if (poll)
        {
            poll();
            QueryPerformanceCounter(&now);
            if (now.QuadPart >= deadline)
                break;
        }
        LONGLONG due = g_renderScrollFrame && g_scrollRenderDue ? g_scrollRenderDue(now.QuadPart) : MAXLONGLONG;
        LONGLONG cost = g_scrollRenderCost ? g_scrollRenderCost() : 0;
        if (due <= now.QuadPart && !stalled && now.QuadPart < deadline - cost)
        {
            LONGLONG before = now.QuadPart;
            ++g_scrollDraws;
            g_renderScrollFrame();
            QueryPerformanceCounter(&now);
            stalled = now.QuadPart - before < instant && g_scrollRenderDue(now.QuadPart) <= now.QuadPart;
            continue;
        }
        if (due <= now.QuadPart && !stalled)
            ++g_scrollSkips;
        LONGLONG wake = due > now.QuadPart && due < deadline - cost ? due : deadline;
        if (poll && wake > now.QuadPart + g_qpcFreq / 500)
            wake = now.QuadPart + g_qpcFreq / 500;
        WaitUntil(wake, now);
    }
}

static void PaceFrame(int fps)
{
    if (fps <= 0 || !EnsureClock())
        return;

    const LONGLONG period = g_qpcFreq / fps;

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    OpenPaceTrace();
    LONGLONG entry = now.QuadPart;
    g_scrollDraws = g_scrollSkips = 0;

    if (g_pacedStep != g_step || g_nextFrame == 0 ||
        now.QuadPart - g_nextFrame > period || g_nextFrame - now.QuadPart > period * 4)
    {
        g_pacedStep = g_step;
        PaceTrace(entry, g_nextFrame, period, true, entry);
        g_nextFrame = now.QuadPart + period;
        return;
    }

    LONGLONG deadline = g_nextFrame;
    ScrollDrawsUntil(g_nextFrame, now, NULL);
    PaceTrace(entry, deadline, period, false, now.QuadPart);
    g_nextFrame += period;
}

static const SpeedProfile *g_speed;

static void NetworkScrollWait()
{
    DWORD start = *(DWORD *)g_speed->netStart;
    if (start == 0xFFFFFFFF || !EnsureClock())
        return;
    int remainMs = (int)*(DWORD *)g_speed->netDelay - (int)((*g_speed->timeGetTime)() - start) - 1;
    if (remainMs <= 0)
        return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    ScrollDrawsUntil(now.QuadPart + remainMs * g_qpcFreq / 1000, now, (PollFn)g_speed->netPoll);
}

static void __cdecl AlternateDelay()
{
    DWORD mode = *g_gameMode;
    bool paced = mode == GAMEMODE_CAMPAIGN || mode == GAMEMODE_SKIRMISH;
    static DWORD s_loggedMode = (DWORD)-1;
    if (mode != s_loggedMode)
    {
        s_loggedMode = mode;
        SpeedLog("  game mode %d, %s", mode, paced ? "speed keys and scroll draws" : "scroll draws only");
    }
    if (!paced)
    {
        g_nextFrame = 0;
        NetworkScrollWait();
        return;
    }
    if (!g_commandsReady)
        PollSpeedKeys(GameFocused());

    *g_delaySlot = 0;
    PaceFrame(kSteps[g_step].fps);
}

static __declspec(naked) void AlternateDelayPreserveFP()
{
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        call AlternateDelay
        fxrstor [esp]
        mov esp, ebp
        pop ebp
        ret
    }
}

static void InstallAlternatingSpeed(const SpeedProfile *s)
{
    const BYTE *p = (const BYTE *)s->pacer;
    BYTE want[6];
    want[0] = 0x8B;
    want[1] = 0x0D;
    *(DWORD *)(want + 2) = s->delaySlot - 8;

    if (IsBadReadPtr(p, 6) || memcmp(p, want, sizeof(want)) != 0)
    {
        SpeedLog("  pacer %X unexpected: %02X %02X %02X %02X %02X %02X", s->pacer, p[0], p[1],
                 p[2], p[3], p[4], p[5]);
        return;
    }

    if (!g_stubs)
    {
        g_stubs = (BYTE *)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!g_stubs)
            return;
        g_stubEnd = g_stubs;
    }

    g_delaySlot = (DWORD *)s->delaySlot;
    g_gameMode = (const DWORD *)s->gameMode;
    g_speed = s;

    BYTE *stub = g_stubEnd;
    EmitByte(0x60);
    EmitByte(0x9C);
    EmitCallAbs(AlternateDelayPreserveFP);
    EmitByte(0x9D);
    EmitByte(0x61);
    Emit(p, 6);
    EmitJmp(s->pacer + 6);

    if (WriteJmp(s->pacer, 6, stub))
        SpeedLog("  speed keys ready, start=%s, [ slower, ] faster, hook at %X",
                 kSteps[g_step].label, s->pacer);
}

static bool SiteLooksRight(const SelectSite &s)
{
    return memcmp((const void *)s.callAddr, s.callSig, sizeof(s.callSig)) == 0 &&
           memcmp((const void *)s.doneAddr, s.doneSig, s.doneLen) == 0;
}

static void PatchSite(const SelectSite &s)
{
    BYTE *selectStub = g_stubEnd;
    EmitByte(0x8B); EmitByte(s.objReg);
    EmitCallAbs(SelectThenMute);
    EmitJmp(s.resumeAddr);

    BYTE *doneStub = g_stubEnd;
    EmitByte(0x60);
    EmitCallAbs(UnmuteC);
    EmitByte(0x61);
    Emit((const void *)s.doneAddr, s.doneLen);
    EmitJmp(s.doneAddr + s.doneLen);

    WriteJmp(s.callAddr, 10, selectStub);
    WriteJmp(s.doneAddr, s.doneLen, doneStub);
}

static void InstallInline()
{
    const GameProfile *g = DetectGame();
    if (!g)
        return;
    const FixProfile *f = NULL;
    for (int i = 0; i < _countof(kFixes); ++i)
        if (strcmp(kFixes[i].name, g->name) == 0)
            f = &kFixes[i];
    if (!f)
        return;
    if (!SiteLooksRight(f->typeSelect) || !SiteLooksRight(f->combatantSelect))
        return;

    g_stubs = (BYTE *)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_stubs)
        return;
    g_stubEnd = g_stubs;
    UseProfile(*f);
    PatchSite(f->typeSelect);
    PatchSite(f->combatantSelect);
}

BOOL WINAPI DllMain(HINSTANCE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(mod);
        if (!HostIsGame())
            return TRUE;
        InstallCampaignSpeed();
        InstallQueueBatch();
        if (IsDebuggerPresent())
            return TRUE;
        InstallInline();
        InstallHealerEscort();
        InstallSaveTweaks();
        InstallSaveHook();
        InstallCommands();
    }
    return TRUE;
}
