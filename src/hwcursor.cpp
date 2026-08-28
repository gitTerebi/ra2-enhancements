#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "gameprofile.h"

static const GameProfile *g_game;
static HWND g_wnd;
static HCURSOR g_fallback;
static const unsigned short *g_lut;
static bool g_is565;

struct CacheEntry
{
    void *shape;
    int frame, w, h, hx, hy;
    HCURSOR cursor;
};

static CacheEntry g_cache[256];
static int g_cacheCount;

static BOOL CALLBACK PickWindow(HWND hwnd, LPARAM)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || GetWindow(hwnd, GW_OWNER) || !IsWindowVisible(hwnd))
        return TRUE;
    g_wnd = hwnd;
    return FALSE;
}

static const BYTE *FramePixels(void *shape, int frame)
{
    if (g_game->getPixels)
        return (const BYTE *)g_game->getPixels(shape, frame);

    const BYTE *shp = (const BYTE *)shape;
    if (frame < 0 || frame >= *(const unsigned short *)(shp + 6))
        return NULL;
    const BYTE *header = shp + frame * 24 + 8;
    DWORD offset = *(const DWORD *)(header + 0x14);
    return offset ? shp + offset : NULL;
}

static bool ResolvePalette()
{
    ConvertClass *cc = *g_game->mousePal;
    if (!cc || IsBadReadPtr(cc, sizeof(ConvertClass)))
        return false;
    if (cc->BytesPerPixel != 2)
        return false;
    if (!cc->Midpoint || IsBadReadPtr(cc->Midpoint, 512))
        return false;
    g_lut = (const unsigned short *)cc->Midpoint;
    g_is565 = (cc->HalfColorMask == 0x7BEF);
    return true;
}

static DWORD ColorFor(BYTE idx)
{
    unsigned short v = g_lut[idx];
    DWORD r, g, b;
    if (g_is565)
    {
        r = ((v >> 11) & 31) * 255 / 31;
        g = ((v >> 5) & 63) * 255 / 63;
        b = (v & 31) * 255 / 31;
    }
    else
    {
        r = ((v >> 10) & 31) * 255 / 31;
        g = ((v >> 5) & 31) * 255 / 31;
        b = (v & 31) * 255 / 31;
    }
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static HCURSOR BuildCursor(const BYTE *px, int w, int h, int hx, int hy)
{
    if (!ResolvePalette())
        return NULL;

    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = w;
    bi.bV5Height = -h;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    void *bits = NULL;
    HDC dc = GetDC(NULL);
    HBITMAP colour = CreateDIBSection(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    if (!colour)
        return NULL;

    DWORD *out = (DWORD *)bits;
    for (int i = 0; i < w * h; ++i)
    {
        BYTE idx = px[i];
        out[i] = idx ? ColorFor(idx) : 0;
    }


    const int maskStride = ((w + 15) / 16) * 2;
    BYTE *maskBits = (BYTE *)calloc(maskStride * h, 1);
    HBITMAP mask = CreateBitmap(w, h, 1, 1, maskBits);
    free(maskBits);

    ICONINFO ii = {};
    ii.fIcon = FALSE;
    ii.xHotspot = hx;
    ii.yHotspot = hy;
    ii.hbmMask = mask;
    ii.hbmColor = colour;
    HCURSOR cur = CreateIconIndirect(&ii);

    DeleteObject(colour);
    DeleteObject(mask);
    return cur;
}

static HCURSOR CursorFor(void *shape, int frame, int w, int h, int hx, int hy)
{
    for (int i = 0; i < g_cacheCount; ++i)
    {
        CacheEntry &e = g_cache[i];
        if (e.shape == shape && e.frame == frame && e.w == w && e.h == h && e.hx == hx && e.hy == hy)
            return e.cursor;
    }

    const BYTE *px = FramePixels(shape, frame);
    if (!px)
        return NULL;

    HCURSOR cur = BuildCursor(px, w, h, hx, hy);
    if (!cur)
        return NULL;

    if (g_cacheCount == _countof(g_cache))
    {
        DestroyCursor(g_cache[0].cursor);
        MoveMemory(g_cache, g_cache + 1, sizeof(g_cache) - sizeof(g_cache[0]));
        --g_cacheCount;
    }
    CacheEntry &e = g_cache[g_cacheCount++];
    e.shape = shape;
    e.frame = frame;
    e.w = w;
    e.h = h;
    e.hx = hx;
    e.hy = hy;
    e.cursor = cur;
    return cur;
}

static LONG g_cursorBusy;

extern "C" __declspec(noinline) void __cdecl OnDrawMouse(BYTE *self)
{
    if (InterlockedExchange(&g_cursorBusy, 1))
        return;

    void *shape = *(void **)(self + 0x04);
    int frame = *(int *)(self + 0x08);
    int w = *(int *)(self + 0x50);
    int h = *(int *)(self + 0x54);
    int hx = *(int *)(self + 0x1C) - *(int *)(self + 0x48);
    int hy = *(int *)(self + 0x20) - *(int *)(self + 0x4C);

    HCURSOR want = NULL;
    if (shape && w > 0 && h > 0 && w <= 256 && h <= 256)
    {
        if (hx < 0) hx = 0; else if (hx > w - 1) hx = w - 1;
        if (hy < 0) hy = 0; else if (hy > h - 1) hy = h - 1;
        want = CursorFor(shape, frame, w, h, hx, hy);
    }
    if (!want)
    {
        if (!g_fallback)
            g_fallback = LoadCursorA(NULL, IDC_ARROW);
        want = g_fallback;
    }

    if (!g_wnd || !IsWindow(g_wnd))
    {
        g_wnd = NULL;
        EnumWindows(PickWindow, 0);
    }
    if (!g_wnd)
    {
        InterlockedExchange(&g_cursorBusy, 0);
        return;
    }

    if ((HCURSOR)GetClassLongPtrA(g_wnd, GCLP_HCURSOR) != want)
    {
        SetClassLongPtrA(g_wnd, GCLP_HCURSOR, (LONG_PTR)want);
        SetCursor(want);
    }
    while (ShowCursor(TRUE) < 0)
        ;
    InterlockedExchange(&g_cursorBusy, 0);
}

EXPORT_FUNC(WWMouse_SkipDraw)
{
    if (!g_game)
        g_game = DetectGame();
    if (!g_game)
        return 0;
    OnDrawMouse((BYTE *)(g_game->thisInEbx ? R->_EBX : R->_EAX));
    return g_game->earlyOut;
}

extern "C" __declspec(noinline) void __cdecl RefreshCursor()
{
    if (!g_game)
        g_game = DetectGame();
    if (!g_game || !g_game->mouseInstance)
        return;
    BYTE *self = *g_game->mouseInstance;
    if (!self || IsBadReadPtr(self, 0x60))
        return;
    OnDrawMouse(self);
}

EXPORT_FUNC(Tactical_CursorTick)
{
    RefreshCursor();
    return 0;
}

EXPORT_FUNC(WWMouse_SkipHideOnCreate)
{
    return 0x007B88C0;
}

EXPORT_FUNC(WWMouse_SkipHideOnCapture)
{
    return 0x007B9C13;
}

static void EnableDpiAwareness()
{
    char buf[8] = {};
    if (!GetEnvironmentVariableA("HWCURSOR_DPI", buf, sizeof(buf)) || buf[0] != '1')
        return;

    HMODULE u32 = GetModuleHandleA("user32.dll");
    if (!u32)
        return;

    typedef BOOL(WINAPI * SetCtx_t)(HANDLE);
    SetCtx_t setCtx = (SetCtx_t)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
    if (setCtx && (setCtx((HANDLE)-4) || setCtx((HANDLE)-3) || setCtx((HANDLE)-2)))
        return;

    typedef BOOL(WINAPI * Legacy_t)(void);
    Legacy_t legacy = (Legacy_t)GetProcAddress(u32, "SetProcessDPIAware");
    if (legacy)
        legacy();
}

#ifdef HWCURSOR_SELFPATCH

#ifdef HWCURSOR_BINKPROXY
#include "binkforward.h"
#endif

#ifdef HWCURSOR_DSOUNDPROXY
#include "dsoundforward.h"
#endif

static bool g_selfPatched;
static DWORD g_earlyOut;

static __declspec(naked) void StubEax()
{
    __asm {
        pushad
        pushfd
        push eax
        call OnDrawMouse
        add esp, 4
        popfd
        popad
        push g_earlyOut
        ret
    }
}

static __declspec(naked) void StubEbx()
{
    __asm {
        pushad
        pushfd
        push ebx
        call OnDrawMouse
        add esp, 4
        popfd
        popad
        push g_earlyOut
        ret
    }
}

static bool InstallInlineHook()
{
    g_game = DetectGame();
    if (!g_game)
        return false;
    if (memcmp((const void *)g_game->hook, g_game->signature, 5) != 0)
        return false;
    g_earlyOut = g_game->earlyOut;

    void *stub = g_game->thisInEbx ? (void *)StubEbx : (void *)StubEax;
    BYTE patch[5];
    patch[0] = 0xE9;
    *(DWORD *)(patch + 1) = (DWORD)stub - (g_game->hook + 5);

    DWORD old = 0;
    if (!VirtualProtect((void *)g_game->hook, 5, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy((void *)g_game->hook, patch, 5);
    VirtualProtect((void *)g_game->hook, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)g_game->hook, 5);
    return true;
}

// off the loader lock: WasdScroll.dll is an optional drop-in, absent means no WASD
static void LoadWasdSidecar()
{
    if (!g_selfPatched || GetModuleHandleA("WasdScroll.dll"))
        return;

    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, sizeof(path));
    if (!n || n >= sizeof(path))
        return;
    char *slash = strrchr(path, '\\');
    if (!slash || (slash - path) + 16 >= sizeof(path))
        return;
    strcpy(slash + 1, "WasdScroll.dll");
    LoadLibraryA(path);
}

#endif

static DWORD WINAPI TickThread(LPVOID)
{
#ifdef HWCURSOR_SELFPATCH
    LoadWasdSidecar();
#endif
    for (;;)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        if (pid == GetCurrentProcessId())
            RefreshCursor();
        Sleep(8);
    }
}

BOOL WINAPI DllMain(HINSTANCE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(mod);
#ifdef HWCURSOR_DSOUNDPROXY
        DSoundLoadReal();
#endif
        if (!HostIsGame())
            return TRUE;
        EnableDpiAwareness();
#ifdef HWCURSOR_SELFPATCH
        g_game = DetectGame();
        if (!IsDebuggerPresent())
            g_selfPatched = InstallInlineHook();
#endif
        CloseHandle(CreateThread(NULL, 0, TickThread, NULL, 0, NULL));
    }
    return TRUE;
}
