#include <windows.h>
#include "gameprofile.h"

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

enum { ACT_NONE = 0, ACT_MOVE = 1, ACT_NOMOVE = 2, ACT_HEALER_GUARD = 0x3B };
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
    DWORD combatDamage;
    DWORD assignMissionSlot;
    DWORD deactivatedOffset;
    void ***selectedItems;
    int *selectedCount;
};

static const HealerProfile kHealers[] = {
    {"gamemd", 0x004AE844, 0x144, 0x006F3970, 0x378, 0x298, (void ***)0x00A8ECBC,
     (int *)0x00A8ECC8},
    {"game", 0x0049E964, 0x138, 0x006C16F0, 0x320, 0, (void ***)0x00A40C64, (int *)0x00A40C70},
};

static const BYTE kCombatDamageSig[16] = {0x53, 0x8B, 0x5C, 0x24, 0x08, 0x56, 0x57, 0x33,
                                          0xFF, 0x83, 0xFB, 0xFF, 0x8B, 0xF1, 0x0F, 0x85};

static const HealerProfile *g_healer;
static DWORD g_healerSlot;
static DWORD g_healerResume;

static void SpeedLog(const char *fmt, ...);

static bool IsGroundFoot(void *obj)
{
    if (!obj || !(*((const BYTE *)obj + 0x14) & ABSFLAG_FOOT))
        return false;
    int rtti = (*(WhatAmIFn **)obj)[VT_WHATAMI / 4](obj);
    return rtti == RTTI_UNIT || rtti == RTTI_INFANTRY;
}

static void *NearestArmedSelected(void *healer)
{
    int here[3], there[3];
    (*(GetCoordsFn **)healer)[VT_GETCOORDS / 4](healer, here);

    void *best = NULL;
    double bestDist = 0;
    void **items = *g_healer->selectedItems;
    int count = *g_healer->selectedCount;
    for (int i = 0; i < count; ++i)
    {
        void *other = items[i];
        if (other == healer || !IsGroundFoot(other) ||
            ((CombatDamageFn)g_healer->combatDamage)(other, -1) <= 0)
            continue;
        (*(GetCoordsFn **)other)[VT_GETCOORDS / 4](other, there);
        double dx = there[0] - here[0], dy = there[1] - here[1];
        double dist = dx * dx + dy * dy;
        if (!best || dist < bestDist)
        {
            best = other;
            bestDist = dist;
        }
    }
    return best;
}

static bool __cdecl HealerEscorts(void *obj, int action)
{
    if (action != ACT_NONE && action != ACT_MOVE && action != ACT_NOMOVE &&
        action != ACT_HEALER_GUARD)
        return false;
    if (!IsGroundFoot(obj) || ((CombatDamageFn)g_healer->combatDamage)(obj, -1) >= 0)
        return false;
    DWORD off = g_healer->deactivatedOffset;
    if (off && *((const BYTE *)obj + off))
        return true;
    void *escort = NearestArmedSelected(obj);
    AssignMissionFn assign = (*(AssignMissionFn **)obj)[g_healer->assignMissionSlot / 4];
    assign(obj, MISSION_AREA_GUARD, escort ? escort : obj, NULL, 0);
    return true;
}

static __declspec(naked) void HealerClickedAction()
{
    __asm {
        push ecx
        push edx
        mov eax, [esp + 0x0C]
        push eax
        push ecx
        call HealerEscorts
        add esp, 8
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

static __declspec(naked) void HealerClickedActionSyringe()
{
    __asm {
        call HealerClickedAction
        jmp dword ptr [g_healerResume]
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
    return true;
}

EXPORT_FUNC(HealerEscort_ClickedAction)
{
    const HealerProfile *h = FindHealerProfile();
    if (!h || h->clickSite != R->origin || !UseHealerProfile(h))
        return 0;
    return (DWORD)HealerClickedActionSyringe;
}

static void InstallHealerEscort()
{
    const HealerProfile *h = FindHealerProfile();
    if (!h)
        return;
    BYTE want[6] = {0xFF, 0x92};
    *(DWORD *)(want + 2) = h->clickSlot;
    if (IsBadReadPtr((const void *)h->clickSite, 6) ||
        memcmp((const void *)h->clickSite, want, sizeof(want)) != 0 || !UseHealerProfile(h))
    {
        SpeedLog("  healer escort: site %X unexpected, skipped", h->clickSite);
        return;
    }

    BYTE patch[6];
    patch[0] = 0xE8;
    *(DWORD *)(patch + 1) = (DWORD)HealerClickedAction - (h->clickSite + 5);
    patch[5] = 0x90;

    DWORD old = 0;
    if (!VirtualProtect((void *)h->clickSite, 6, PAGE_EXECUTE_READWRITE, &old))
        return;
    memcpy((void *)h->clickSite, patch, 6);
    VirtualProtect((void *)h->clickSite, 6, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)h->clickSite, 6);
    SpeedLog("  healer escort ready at %X", h->clickSite);
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

static void PollSpeedKeys()
{
    const int keys[2] = {VK_OEM_4, VK_OEM_6};
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    bool focused = pid == GetCurrentProcessId();
    for (int i = 0; i < 2; ++i)
    {
        bool down = focused && (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
        if (down && !g_prevDown[i])
        {
            int next = g_step + (i ? 1 : -1);
            if (next >= 0 && next < _countof(kSteps))
            {
                g_step = next;
                SaveStep();
                SpeedLog("  step -> %s (%d fps)", kSteps[g_step].label, kSteps[g_step].fps);
            }
            ShowStep();
        }
        g_prevDown[i] = down;
    }
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
            g_renderScrollFrame();
            QueryPerformanceCounter(&now);
            stalled = now.QuadPart - before < instant && g_scrollRenderDue(now.QuadPart) <= now.QuadPart;
            continue;
        }
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

    if (g_pacedStep != g_step || g_nextFrame == 0 ||
        now.QuadPart - g_nextFrame > period || g_nextFrame - now.QuadPart > period * 4)
    {
        g_pacedStep = g_step;
        g_nextFrame = now.QuadPart + period;
        return;
    }

    ScrollDrawsUntil(g_nextFrame, now, NULL);
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
    PollSpeedKeys();

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
    }
    return TRUE;
}
