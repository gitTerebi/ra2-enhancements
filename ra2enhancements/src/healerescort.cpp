#include "patch.h"
#include "features.h"
#include "escort.h"

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
static DWORD g_healerCellSlot;

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

void ForgetEscorts()
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
    g_healerCellSlot = h->cellSlot;
    return true;
}

static bool CallSiteLooksRight(DWORD site, BYTE modrm, DWORD slot)
{
    BYTE want[6] = {0xFF, modrm};
    *(DWORD *)(want + 2) = slot;
    return BytesAt(site, want, sizeof(want));
}

void InstallHealerEscort()
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
    WriteCall(h->clickSite, HealerClickedAction, 6);
    WriteCall(h->cellSite, HealerCellClickedAction, 6);
    SpeedLog("  healer escort ready at %X and %X", h->clickSite, h->cellSite);
}
