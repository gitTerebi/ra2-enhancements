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
    if (!HostIsGame())
        return NULL;
    for (int i = 0; i < _countof(kFixes); ++i)
        if (strcmp(kFixes[i].name, "gamemd") == 0)
            return &kFixes[i];
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

struct SpeedProfile
{
    const char *name;
    DWORD gameOptions;
    DWORD sites[2];
    DWORD delaySite;
    DWORD pacer;
    DWORD delaySlot;
};

// Phobos src/Misc/Hooks.Gamespeed.cpp:11 hooks 0x69BAE7 and 0x55E160; 0x55D78D is delaySite.
static const SpeedProfile kSpeeds[] = {
    {"gamemd", 0x00A8EB60, {0x0055D774, 0x0069BAE7}, 0x0055D78C, 0x0055E160, 0x00887350},
    {"game", 0x00A40B18, {0x0053FFE4, 0x00672E17}, 0x0053FFFC, 0x00540960, 0x00839998},
};

enum { CAMPAIGN_SPEED = 1 };
enum { ALT_FAST = 1, ALT_SLOW = 2 };

struct SpeedStep
{
    int fastFrames;
    int slowFrames;
    const char *label;
};

static const SpeedStep kSteps[] = {
    {0, 1, "1.00x (stock)"},
    {1, 3, "1.14x"},
    {1, 2, "1.20x"},
    {1, 1, "1.33x"},
    {2, 1, "1.50x"},
    {3, 1, "1.60x"},
    {1, 0, "2.00x (max)"},
};

enum { STEP_DEFAULT = 4 };

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

    int done = 0;
    for (int i = 0; i < 3; ++i)
    {
        DWORD at = (i < 2) ? s->sites[i] + 6 : s->delaySite + 1;
        DWORD old = 0;
        if (!VirtualProtect((void *)at, 4, PAGE_EXECUTE_READWRITE, &old))
        {
            SpeedLog("  write %X VirtualProtect failed err=%d", at, GetLastError());
            continue;
        }
        *(DWORD *)at = ALT_SLOW;
        VirtualProtect((void *)at, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void *)at, 4);
        if (*(DWORD *)at == ALT_SLOW)
            ++done;
        SpeedLog("  write %X now reads %d", at, *(DWORD *)at);
    }
    SpeedLog("  patched %d of 3 writes", done);
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
static DWORD g_altCounter;
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
    for (int i = 0; i < 2; ++i)
    {
        bool down = (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
        if (down && !g_prevDown[i])
        {
            int next = g_step + (i ? 1 : -1);
            if (next >= 0 && next < _countof(kSteps))
            {
                g_step = next;
                g_altCounter = 0;
                SaveStep();
                SpeedLog("  step -> %s (fast=%d slow=%d)", kSteps[g_step].label,
                         kSteps[g_step].fastFrames, kSteps[g_step].slowFrames);
            }
            ShowStep();
        }
        g_prevDown[i] = down;
    }
}

static void __cdecl AlternateDelay()
{
    PollSpeedKeys();

    const SpeedStep &s = kSteps[g_step];
    int period = s.fastFrames + s.slowFrames;
    if (period <= 0)
        return;
    *g_delaySlot = (int)(g_altCounter++ % period) < s.fastFrames ? ALT_FAST : ALT_SLOW;
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

    BYTE *stub = g_stubEnd;
    EmitByte(0x60);
    EmitByte(0x9C);
    EmitCallAbs(AlternateDelay);
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
        if (IsDebuggerPresent())
            return TRUE;
        InstallInline();
        InstallQueueBatch();
    }
    return TRUE;
}
