#include <stdio.h>
#include "patch.h"
#include "features.h"

typedef DWORD(WINAPI *TimeGetTimeFn)(void);

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

void StepSpeed(int dir)
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

bool SinglePlayerMode()
{
    return g_gameMode && (*g_gameMode == GAMEMODE_CAMPAIGN || *g_gameMode == GAMEMODE_SKIRMISH);
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
    if (!CommandsReady())
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
    if (!ReserveStubs())
        return;

    g_delaySlot = (DWORD *)s->delaySlot;
    g_gameMode = (const DWORD *)s->gameMode;
    g_speed = s;

    BYTE *stub = StubCursor();
    EmitByte(0x60);
    EmitByte(0x9C);
    EmitCallAbs(AlternateDelayPreserveFP);
    EmitByte(0x9D);
    EmitByte(0x61);
    Emit(p, 6);
    EmitJmp(s->pacer + 6);

    if (WriteJmp(s->pacer, stub, 6))
        SpeedLog("  speed keys ready, start=%s, [ slower, ] faster, hook at %X",
                 kSteps[g_step].label, s->pacer);
}

void InstallCampaignSpeed()
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
