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

typedef int(WINAPI *ShowCursorProc)(BOOL);
static PROC *g_showCursorSlot;
static ShowCursorProc g_nextShowCursor;

static int WINAPI IgnoreCursorHide(BOOL show)
{
    if (show)
        return g_nextShowCursor(TRUE);
    return -1;
}

static PROC *FindShowCursorImport()
{
    BYTE *exe = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva)
        return NULL;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(exe + rva); d->FirstThunk; ++d)
    {
        if (!d->OriginalFirstThunk || _stricmp((char *)exe + d->Name, "user32.dll") != 0)
            continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(exe + d->OriginalFirstThunk);
        IMAGE_THUNK_DATA *slots = (IMAGE_THUNK_DATA *)(exe + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots)
        {
            if (names->u1.Ordinal & IMAGE_ORDINAL_FLAG)
                continue;
            IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME *)(exe + names->u1.AddressOfData);
            if (strcmp((char *)ibn->Name, "ShowCursor") == 0)
                return (PROC *)&slots->u1.Function;
        }
    }
    return NULL;
}

static void KeepShowCursorPatched()
{
    if (!g_showCursorSlot && !(g_showCursorSlot = FindShowCursorImport()))
        return;
    PROC current = *g_showCursorSlot;
    if (current == (PROC)IgnoreCursorHide)
        return;
    DWORD old;
    if (!VirtualProtect(g_showCursorSlot, sizeof(PROC), PAGE_READWRITE, &old))
        return;
    g_nextShowCursor = (ShowCursorProc)current;
    *g_showCursorSlot = (PROC)IgnoreCursorHide;
    VirtualProtect(g_showCursorSlot, sizeof(PROC), old, &old);
    CURSORINFO info = {sizeof(info)};
    if (GetCursorInfo(&info) && !(info.flags & CURSOR_SHOWING))
        while (g_nextShowCursor(TRUE) < 0)
            ;
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
        KeepShowCursorPatched();
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

static void InstallCursorHook()
{
    if (!g_game || !BytesAt(g_game->hook, g_game->signature, 5))
        return;
    g_earlyOut = g_game->earlyOut;
    WriteJmp(g_game->hook, g_game->thisInEbx ? (void *)StubEbx : (void *)StubEax);
    if (strcmp(g_game->name, "gamemd") == 0)
    {
        WriteJmp(0x007B88B9, (void *)0x007B88C0, 7);
        WriteJmp(0x007B9C0B, (void *)0x007B9C13, 8);
    }
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
