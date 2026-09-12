#include <windows.h>
#include <stdio.h>
#include "gameprofile.h"
#include "wasdscroll.h"
#include "smoothscroll.h"
#include "rightscroll.h"

static const GameProfile *g_game;
static bool g_scrollHooked;
static DWORD g_scrollResume;
static bool g_rendering;
static FILE *g_camTrace;
static int g_camTraceLines;

static void OpenCamTrace()
{
    char dir[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, dir, sizeof(dir));
    char *slash = n ? strrchr(dir, '\\') : NULL;
    if (!slash)
        return;
    slash[1] = 0;
    char ini[MAX_PATH], path[MAX_PATH];
    _snprintf(ini, sizeof(ini) - 1, "%sddraw.ini", dir);
    ini[sizeof(ini) - 1] = 0;
    char value[16];
    GetPrivateProfileStringA("ddraw", "frametrace", "", value, sizeof(value), ini);
    if (_stricmp(value, "true") != 0 && strcmp(value, "1") != 0)
        return;
    _snprintf(path, sizeof(path) - 1, "%scamtrace.csv", dir);
    path[sizeof(path) - 1] = 0;
    g_camTrace = fopen(path, "w");
    if (!g_camTrace)
        return;
    setvbuf(g_camTrace, NULL, _IOFBF, 1 << 20);
    fprintf(g_camTrace, "source,start,end,x,y,moved,divisor,target\n");
}

static void CamTrace(int source, LONGLONG start, bool moved)
{
    if (!g_camTrace || !g_game)
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

static LONGLONG QpcNow()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

// Called on the game thread by RA2Enhancements while its simulation timer waits.
// Do not dispatch Map input here: that can consume commands or open modal dialogs.
extern "C" __declspec(dllexport) void __cdecl RenderScrollFrame()
{
    if (!g_scrollHooked || g_rendering || !g_game)
        return;
    g_rendering = true;
    LONGLONG start = QpcNow();
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
        PresentClock::RecordCost(QpcNow() - start, start);
    }
    CamTrace(0, start, moved);
    g_rendering = false;
}

extern "C" __declspec(dllexport) LONGLONG __cdecl ScrollRenderDue(LONGLONG now)
{
    if (!g_scrollHooked || !g_game)
        return MAXLONGLONG;
    return PresentClock::RenderDue(now);
}

static const GameProfile *Game()
{
    if (!g_game)
        g_game = DetectGame();
    return g_game;
}

extern "C" __declspec(noinline) void __cdecl ScrollTickC(void *map)
{
    const GameProfile *g = Game();
    if (g && map == (void *)g->mapInstance)
    {
        LONGLONG start = QpcNow();
        SmoothScroll::Tick(g->tacticalInstance, g->setTacticalPos);
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
    const GameProfile *g = Game();
    if (!g || !g->scrollHook)
        return;
    static const BYTE signature[] = {0x83, 0xEC, 0x08, 0x55, 0x56, 0x57, 0x8B, 0xF9};
    if (IsBadReadPtr((const void *)g->scrollHook, sizeof(signature)) ||
        memcmp((const void *)g->scrollHook, signature, sizeof(signature)) != 0)
        return;
    if (!SmoothScroll::Available(g->setTacticalPos))
        return;

    g_scrollResume = g->scrollHook + 5;

    BYTE patch[5];
    patch[0] = 0xE9;
    *(DWORD *)(patch + 1) = (DWORD)ScrollStub - (g->scrollHook + 5);

    DWORD old = 0;
    if (!VirtualProtect((void *)g->scrollHook, 5, PAGE_EXECUTE_READWRITE, &old))
        return;
    memcpy((void *)g->scrollHook, patch, 5);
    VirtualProtect((void *)g->scrollHook, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)g->scrollHook, 5);
    g_scrollHooked = true;
}

static DWORD WINAPI TickThread(LPVOID)
{
    for (;;)
    {
        if (!g_scrollHooked)
        {
            WasdScroll::Tick();
        }
        Sleep(2);
    }
}

EXPORT_FUNC(Tactical_WasdTick)
{
    // The worker owns fallback key state; do not race it from a render hook.
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(mod);
        if (!HostIsGame())
            return TRUE;
        // Install while the loading thread is stopped, before gameplay starts.
        // Never rewrite executable instructions from the polling worker.
        InstallScrollHook();
        if (g_scrollHooked)
        {
            OpenCamTrace();
            ScrollSpeed::g_rate = (const int *)(strcmp(g_game->name, "gamemd") == 0 ?
                                               0xA8EB70 : 0xA40B24);
            RightScroll::Install(strcmp(g_game->name, "gamemd") == 0);
        }
        if (!g_scrollHooked)
            CloseHandle(CreateThread(NULL, 0, TickThread, NULL, 0, NULL));
    }
    return TRUE;
}
