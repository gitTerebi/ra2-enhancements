#include <stdlib.h>
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
    DWORD lastUse;
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
    if (!cc || IsBadReadPtr(cc, sizeof(ConvertClass)) || cc->BytesPerPixel != 2 || !cc->Midpoint ||
        IsBadReadPtr(cc->Midpoint, 512))
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
    ii.xHotspot = hx;
    ii.yHotspot = hy;
    ii.hbmMask = mask;
    ii.hbmColor = colour;
    HCURSOR cur = CreateIconIndirect(&ii);

    DeleteObject(colour);
    DeleteObject(mask);
    return cur;
}

static HCURSOR g_applied;
static DWORD g_useClock;

static HCURSOR CursorFor(void *shape, int frame, int w, int h, int hx, int hy)
{
    for (int i = 0; i < g_cacheCount; ++i)
    {
        CacheEntry &e = g_cache[i];
        if (e.shape == shape && e.frame == frame && e.w == w && e.h == h && e.hx == hx && e.hy == hy)
        {
            e.lastUse = ++g_useClock;
            return e.cursor;
        }
    }

    const BYTE *px = FramePixels(shape, frame);
    if (!px)
        return NULL;

    HCURSOR cur = BuildCursor(px, w, h, hx, hy);
    if (!cur)
        return NULL;

    int slot = g_cacheCount;
    if (g_cacheCount == _countof(g_cache))
    {
        slot = -1;
        for (int i = 0; i < g_cacheCount; ++i)
            if (g_cache[i].cursor != g_applied && (slot < 0 || g_cache[i].lastUse < g_cache[slot].lastUse))
                slot = i;
        DestroyCursor(g_cache[slot].cursor);
    }
    else
        ++g_cacheCount;
    CacheEntry &e = g_cache[slot];
    e.lastUse = ++g_useClock;
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
    if (g_wnd)
    {
        if ((HCURSOR)GetClassLongPtrA(g_wnd, GCLP_HCURSOR) != want)
        {
            SetClassLongPtrA(g_wnd, GCLP_HCURSOR, (LONG_PTR)want);
            SetCursor(want);
        }
        g_applied = want;
    }
    InterlockedExchange(&g_cursorBusy, 0);
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

typedef int(WINAPI *ShowCursorProc)(BOOL);
static ShowCursorProc *g_gameShowCursor;

// CaptureMouse switches from the OS cursor to the software cursor. With the
// software sprite replaced, apply the logical state instead of hiding the OS
// cursor unconditionally. Use the game's import so cnc-ddraw tracks the count.
static void __cdecl OnCaptureMouse(BYTE *self)
{
    const BOOL visible = *(const int *)(self + 0x0C) >= 0;
    if (visible)
    {
        HCURSOR cursor = g_applied;
        if (!cursor)
            cursor = LoadCursorA(NULL, IDC_ARROW);
        SetCursor(cursor);
        while ((*g_gameShowCursor)(TRUE) < 0) {}
    }
    else
    {
        while ((*g_gameShowCursor)(FALSE) >= 0) {}
    }
}

static __declspec(naked) void CaptureMouseStub()
{
    __asm {
        pushfd
        pushad
        push esi
        call OnCaptureMouse
        add esp, 4
        popad
        popfd
        ret
    }
}

static void InstallCursorHook()
{
    if (!g_game || !BytesAt(g_game->hook, g_game->signature, 5))
        return;

    const bool yr = strcmp(g_game->name, "gamemd") == 0;
    const DWORD showEnd = yr ? 0x007B98EC : 0x00772C5C;
    const DWORD hideEnd = yr ? 0x007B9A23 : 0x00772D93;
    const DWORD captureHide = yr ? 0x007B9C0B : 0x00772F7B;
    const DWORD showImport = yr ? 0x007E13DC : 0x0079A3CC;
    const BYTE skipOSVisibility[] = {0xEB, 0x16};
    BYTE captureCall[] = {0x6A, 0x00, 0xFF, 0x15, 0, 0, 0, 0};
    memcpy(captureCall + 4, &showImport, sizeof(showImport));
    if (!BytesAt(showEnd, skipOSVisibility, sizeof(skipOSVisibility)) ||
        !BytesAt(hideEnd, skipOSVisibility, sizeof(skipOSVisibility)) ||
        !BytesAt(captureHide, captureCall, sizeof(captureCall)))
    {
        SpeedLog("  hardware cursor: visibility sites differ; no hooks installed");
        return;
    }

    g_gameShowCursor = (ShowCursorProc *)showImport;
    g_earlyOut = g_game->earlyOut;
    WriteJmp(g_game->hook, g_game->thisInEbx ? (void *)StubEbx : (void *)StubEax);

    // Keep the software bookkeeping and image update, then fall through to the
    // game's existing OS ShowCursor loops. These paths run only at logical
    // visibility transitions (refcount -1 -> 0 and 0 -> -1).
    const BYTE fallThrough[] = {0x90, 0x90};
    WriteBytes(showEnd, fallThrough, sizeof(fallThrough));
    WriteBytes(hideEnd, fallThrough, sizeof(fallThrough));
    WriteCall(captureHide, (void *)CaptureMouseStub, sizeof(captureCall));
}

static void InstallAll()
{
    static bool s_done;
    if (s_done || !HostIsGame())
        return;
    s_done = true;
    EnableDpiAwareness();
    g_game = DetectGame();
    if (InstallPatch("hardware cursor", InstallCursorHook))
        SpeedLog("  hardware cursor ready at %X", g_game->hook);
}

EXPORT_FUNC(HWCursor_Install)
{
    InstallAll();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(mod);
        if (!IsDebuggerPresent())
            InstallAll();
    }
    return TRUE;
}
