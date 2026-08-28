#include <windows.h>
#include "gameprofile.h"
#include "wasdscroll.h"
#include "smoothscroll.h"

static const GameProfile *g_game;
static bool g_scrollHooked;
static DWORD g_scrollFlag;
static DWORD g_scrollResume;

static const GameProfile *Game()
{
    if (!g_game)
        g_game = DetectGame();
    return g_game;
}

extern "C" __declspec(noinline) void __cdecl ScrollTickC()
{
    const GameProfile *g = Game();
    if (g)
        SmoothScroll::Tick(g->tacticalInstance, g->setTacticalPos);
}

static __declspec(naked) void ScrollStub()
{
    __asm {
        pushad
        pushfd
        call ScrollTickC
        popfd
        popad
        mov eax, g_scrollFlag
        mov al, byte ptr [eax]
        push g_scrollResume
        ret
    }
}

static void InstallScrollHook()
{
    const GameProfile *g = Game();
    if (!g || !g->scrollHook)
        return;
    if (*(const BYTE *)g->scrollHook != 0xA0)
        return;
    if (!SmoothScroll::Available(g->setTacticalPos))
        return;

    g_scrollFlag = *(DWORD *)(g->scrollHook + 1);
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
            InstallScrollHook();
            if (!g_scrollHooked)
                WasdScroll::Tick();
        }
        Sleep(2);
    }
}

EXPORT_FUNC(Tactical_WasdTick)
{
    if (!g_scrollHooked)
        InstallScrollHook();
    if (!g_scrollHooked)
        WasdScroll::Tick();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(mod);
        if (!HostIsGame())
            return TRUE;
        CloseHandle(CreateThread(NULL, 0, TickThread, NULL, 0, NULL));
    }
    return TRUE;
}
