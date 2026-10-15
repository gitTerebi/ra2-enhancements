#include <windows.h>
#include <stdio.h>
#include "gameprofile.h"
#include "smoothscroll.h"
#include "rightscroll.h"
#include "profiler.h"

static const GameProfile *g_game;
static bool g_scrollHooked;
static DWORD g_scrollResume;
static bool g_rendering;
static FILE *g_camTrace;
static int g_camTraceLines;

static void OpenCamTrace()
{
    char path[MAX_PATH];
    if (!DdrawIniFlag("frametrace") || !BesideExe("camtrace.csv", path, sizeof(path)))
        return;
    g_camTrace = fopen(path, "w");
    if (!g_camTrace)
        return;
    setvbuf(g_camTrace, NULL, _IOFBF, 1 << 20);
    fprintf(g_camTrace, "source,start,end,x,y,moved,divisor,target\n");
}

static void CamTrace(int source, LONGLONG start, bool moved)
{
    if (!g_camTrace)
        return;
    BYTE *tac = *(BYTE **)g_game->tacticalInstance;
    if (!tac)
        return;
    LARGE_INTEGER end;
    QueryPerformanceCounter(&end);
    fprintf(g_camTrace, "%d,%lld,%lld,%d,%d,%d,%d,%lld\n", source, start, end.QuadPart,
            *(int *)(tac + 0xD64), *(int *)(tac + 0xD68), moved ? 1 : 0,
            PresentClock::g_divisor, PresentClock::g_renderedTarget);
    if (++g_camTraceLines % 1000 == 0)
        fflush(g_camTrace);
}

// Called on the game thread by RA2Enhancements while its simulation timer waits.
// Do not dispatch Map input here: that can consume commands or open modal dialogs.
extern "C" __declspec(dllexport) void __cdecl RenderScrollFrame()
{
    if (!g_scrollHooked || g_rendering)
        return;
    g_rendering = true;
    LONGLONG start = PresentClock::Now();
    SmoothScroll::Tick(g_game->tacticalInstance, g_game->setTacticalPos);
    bool rightMoved = RightScroll::Tick(g_game->mapInstance, g_game->tacticalInstance);
    bool moved = SmoothScroll::g_moved || rightMoved;
    if (moved)
    {
        void *tactical = *(void **)g_game->tacticalInstance;
        void *map = (void *)g_game->mapInstance;
        typedef void (__thiscall *Method)(void *);
        // Original idle-render sequence at RA2 540A61 / YR 55E261.
        (*(Method **)tactical)[0x5C / 4](tactical);
        // GScreenClass::DrawOnTop renders layers and presents the complete scene.
        (*(Method **)map)[0x3C / 4](map);
        PresentClock::RecordCost(PresentClock::Now() - start, start);
    }
    CamTrace(rightMoved ? 2 : 0, start, moved);
    g_rendering = false;
}

static bool ScrollInputActive()
{
    if (!*(void **)g_game->tacticalInstance)
        return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId())
        return false;
    if (RightScroll::g_profile && (GetAsyncKeyState(VK_RBUTTON) & 0x8000) &&
        *(BYTE *)(g_game->mapInstance + RightScroll::g_profile->captureOffset) == 1)
        return true;
    return ((GetAsyncKeyState('W') | GetAsyncKeyState('A') | GetAsyncKeyState('S') |
             GetAsyncKeyState('D')) & 0x8000) != 0;
}

extern "C" __declspec(dllexport) LONGLONG __cdecl ScrollRenderDue(LONGLONG now)
{
    if (!g_scrollHooked || !ScrollInputActive())
        return MAXLONGLONG;
    return PresentClock::RenderDue(now);
}

extern "C" __declspec(dllexport) LONGLONG __cdecl ScrollRenderCost()
{
    return PresentClock::g_renderBudget;
}

extern "C" __declspec(noinline) void __cdecl ScrollTickC(void *map)
{
    if (map == (void *)g_game->mapInstance)
    {
        DrawProfiler::StartOnThisThread(&g_rendering);
        LONGLONG start = PresentClock::Now();
        SmoothScroll::Tick(g_game->tacticalInstance, g_game->setTacticalPos);
        CamTrace(1, start, SmoothScroll::g_moved);
    }
}

static __declspec(naked) void ScrollStub()
{
    __asm {
        pushad
        pushfd
        // The tick uses floating point. Preserve the caller's x87/SSE state too.
        mov ebp, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        push ecx
        call ScrollTickC
        add esp, 4
        fxrstor [esp]
        mov esp, ebp
        popfd
        popad
        // Replay the five displaced bytes of GetInputAndUpdate.
        sub esp, 8
        push ebp
        push esi
        jmp dword ptr [g_scrollResume]
    }
}

static void InstallScrollHook()
{
    static const BYTE signature[] = {0x83, 0xEC, 0x08, 0x55, 0x56, 0x57, 0x8B, 0xF9};
    if (!g_game || !BytesAt(g_game->scrollHook, signature, sizeof(signature)) ||
        !SmoothScroll::Available(g_game->setTacticalPos))
        return;
    g_scrollResume = g_game->scrollHook + 5;
    WriteJmp(g_game->scrollHook, ScrollStub);
}

static void InstallRightScroll()
{
    RightScroll::Install(strcmp(g_game->name, "gamemd") == 0);
}

static void InstallAll()
{
    static bool s_done;
    if (s_done || !HostIsGame())
        return;
    s_done = true;
    g_game = DetectGame();
    if (!InstallPatch("WASD scroll", InstallScrollHook))
        return;
    g_scrollHooked = true;
    OpenCamTrace();
    ScrollSpeed::g_rate = (const int *)(strcmp(g_game->name, "gamemd") == 0 ? 0xA8EB70 : 0xA40B24);
    if (!InstallPatch("right-drag scroll", InstallRightScroll))
        RightScroll::g_profile = NULL;
    SpeedLog("  WASD scroll ready at %X, right-drag scroll %s", g_game->scrollHook,
             RightScroll::g_profile ? "ready" : "off");
}

EXPORT_FUNC(WasdScroll_Install)
{
    InstallAll();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(mod);
    return TRUE;
}
