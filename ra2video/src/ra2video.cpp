#include <windows.h>
#include <commctrl.h>
#include <stdio.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

static const char *kSections[] = {"ddraw", "game", "gamemd", "ra2", "ra2md"};

struct RenderMode
{
    const char *res;
    const char *label;
};

static const RenderMode kRender[] = {
    {"640x480", "640x480    4:3"},
    {"800x600", "800x600    4:3"},
    {"1024x768", "1024x768    4:3"},
    {"1280x720", "1280x720    16:9"},
    {"1280x960", "1280x960    4:3"},
    {"1366x768", "1366x768    16:9 approx"},
    {"1600x900", "1600x900    16:9"},
    {"1708x960", "1708x960    16:9 approx"},
    {"1920x1080", "1920x1080    16:9"},
    {"2560x1440", "2560x1440    16:9"},
};

#define SIZE_ID_DIVIDER (-2)
#define SIZE_ID_MAXFIT (-1)
#define SIZE_ID_MAXFIT_NB (-3)
#define SIZE_ID_DESKTOP 0

#define MAX_SIZES 64
static int g_sizeW[MAX_SIZES];
static int g_sizeH[MAX_SIZES];
static int g_sizeCount;

static char g_ini[MAX_PATH];
static char g_resIni[MAX_PATH];
static char g_dir[MAX_PATH];
static const char *kGameInis[] = {"RA2MD.ini", "RA2.ini", "RA2MO.ini"};
static HWND g_render, g_output, g_status;
static HWND g_pass[3];

#define MAX_SHADERS 512
static char g_shaders[MAX_SHADERS][MAX_PATH];
static int g_shaderCount;

static void ScanShaders(const char *root, const char *rel)
{
    char pattern[MAX_PATH];
    _snprintf(pattern, sizeof(pattern), "%s\\%s*", root, rel);

    WIN32_FIND_DATAA fd;
    HANDLE find = FindFirstFileA(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (fd.cFileName[0] == '.')
            continue;

        char child[MAX_PATH];
        _snprintf(child, sizeof(child), "%s%s", rel, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            char sub[MAX_PATH];
            _snprintf(sub, sizeof(sub), "%s\\", child);
            ScanShaders(root, sub);
            continue;
        }

        const char *dot = strrchr(fd.cFileName, '.');
        if (!dot || _stricmp(dot, ".glsl") != 0 || g_shaderCount >= MAX_SHADERS)
            continue;

        strncpy(g_shaders[g_shaderCount], child, MAX_PATH - 1);
        g_shaders[g_shaderCount][MAX_PATH - 1] = '\0';
        g_shaderCount++;
    } while (FindNextFileA(find, &fd));

    FindClose(find);
}
static int g_dpi = 96;
static char g_customRes[32];
static HFONT g_font;

static int S(int v)
{
    return MulDiv(v, g_dpi, 96);
}

static void Log(const char *fmt, ...)
{
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, sizeof(path));
    char *slash = strrchr(path, '\\');
    if (!slash)
        return;
    strcpy(slash + 1, "ra2video.log");

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

static void EnableDpiAwareness()
{
    HMODULE u32 = GetModuleHandleA("user32.dll");
    if (!u32)
        return;

    typedef BOOL(WINAPI * SetCtx_t)(HANDLE);
    SetCtx_t setCtx = (SetCtx_t)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
    if (setCtx && (setCtx((HANDLE)-4) || setCtx((HANDLE)-3)))
        return;

    typedef BOOL(WINAPI * Legacy_t)(void);
    Legacy_t legacy = (Legacy_t)GetProcAddress(u32, "SetProcessDPIAware");
    if (legacy)
        legacy();
}

static void MakeFont(HWND h)
{
    HMODULE u32 = GetModuleHandleA("user32.dll");
    typedef UINT(WINAPI * GetDpi_t)(HWND);
    GetDpi_t getDpi = u32 ? (GetDpi_t)GetProcAddress(u32, "GetDpiForWindow") : NULL;
    if (getDpi)
    {
        UINT d = getDpi(h);
        if (d)
            g_dpi = (int)d;
    }
    else
    {
        HDC dc = GetDC(NULL);
        g_dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(NULL, dc);
    }

    NONCLIENTMETRICSA ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    ncm.lfMessageFont.lfHeight = -MulDiv(10, g_dpi, 96);
    g_font = CreateFontIndirectA(&ncm.lfMessageFont);
}

static void IniPaths()
{
    GetModuleFileNameA(NULL, g_ini, sizeof(g_ini));
    char *slash = strrchr(g_ini, '\\');
    if (slash)
        *slash = 0;
    strcpy(g_dir, g_ini);
    _snprintf(g_resIni, sizeof(g_resIni), "%s\\Resources\\cnc-ddraw.ini", g_ini);
    strcat(g_ini, "\\ddraw.ini");
}

static void WriteBoth(const char *key, const char *value)
{
    BOOL haveRes = GetFileAttributesA(g_resIni) != INVALID_FILE_ATTRIBUTES;

    for (int i = 0; i < _countof(kSections); ++i)
    {
        const char *v = (i == 0) ? value : NULL;

        WritePrivateProfileStringA(kSections[i], key, v, g_ini);
        if (haveRes)
            WritePrivateProfileStringA(kSections[i], key, v, g_resIni);
    }
}

static int Gcd(int a, int b)
{
    while (b)
    {
        int t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static int WorkAreaDpi(void)
{
    POINT pt = {0, 0};
    RECT work = {};
    if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0))
    {
        pt.x = work.left + (work.right - work.left) / 2;
        pt.y = work.top + (work.bottom - work.top) / 2;
    }

    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);

    HMODULE shcore = LoadLibraryA("shcore.dll");
    typedef HRESULT(WINAPI * ForMon_t)(HMONITOR, int, UINT *, UINT *);
    ForMon_t forMon = shcore ? (ForMon_t)GetProcAddress(shcore, "GetDpiForMonitor") : NULL;

    int dpi = 0;
    if (forMon)
    {
        UINT dx = 0;
        UINT dy = 0;
        if (SUCCEEDED(forMon(mon, 0, &dx, &dy)) && dx)
            dpi = (int)dx;
    }

    if (shcore)
        FreeLibrary(shcore);

    return dpi ? dpi : g_dpi;
}

#define GAME_WINDOW_STYLE 0x96CF0000

static BOOL CALLBACK FindGameTitle(HWND hwnd, LPARAM lp)
{
    static const char *kTitles[] = {"Red Alert 2", "Yuri's Revenge", "OpenRA"};
    char title[160];
    int len = GetWindowTextA(hwnd, title, sizeof(title));
    for (int i = 0; i < _countof(kTitles); ++i)
    {
        size_t n = strlen(kTitles[i]);
        if ((size_t)len >= n && strncmp(title, kTitles[i], n) == 0 &&
            (title[n] == 0 || strncmp(title + n, "   [ speed ", 11) == 0))
        {
            *(HWND *)lp = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL ClientBorder(HWND wnd, RECT *out)
{
    RECT wr = {};
    RECT cr = {};
    POINT org = {0, 0};
    if (!GetWindowRect(wnd, &wr) || !GetClientRect(wnd, &cr) || !ClientToScreen(wnd, &org))
        return FALSE;

    out->left = org.x - wr.left;
    out->top = org.y - wr.top;
    out->right = (wr.right - wr.left) - (cr.right - cr.left) - out->left;
    out->bottom = (wr.bottom - wr.top) - (cr.bottom - cr.top) - out->top;
    return TRUE;
}

static BOOL BorderFromLiveGame(RECT *out)
{
    HWND game = NULL;
    EnumWindows(FindGameTitle, (LPARAM)&game);
    return game && ClientBorder(game, out) && out->top > 0;
}

static void MeasureGameFrame(RECT *border)
{
    static RECT cached = {-1, 0, 0, 0};

    if (cached.left < 0)
    {
        cached.left = cached.top = cached.right = cached.bottom = 0;

        RECT live = {};
        if (BorderFromLiveGame(&live))
        {
            cached = live;
            char buf[16];
            _snprintf(buf, sizeof(buf), "%d,%d,%d,%d", live.left, live.top, live.right,
                      live.bottom);
            WritePrivateProfileStringA("ddraw", "ra2video_border", buf, g_ini);
            Log("MeasureGameFrame from live game L%d T%d R%d B%d (cached to ini)", cached.left,
                cached.top, cached.right, cached.bottom);
            *border = cached;
            return;
        }

        char saved[32] = {};
        GetPrivateProfileStringA("ddraw", "ra2video_border", "", saved, sizeof(saved), g_ini);
        if (saved[0])
        {
            int l = 0, t = 0, r = 0, b = 0;
            if (sscanf(saved, "%d,%d,%d,%d", &l, &t, &r, &b) == 4 && t > 0)
            {
                cached.left = l;
                cached.top = t;
                cached.right = r;
                cached.bottom = b;
                Log("MeasureGameFrame from ini cache L%d T%d R%d B%d", l, t, r, b);
                *border = cached;
                return;
            }
        }

        DWORD style = (GAME_WINDOW_STYLE & ~WS_VISIBLE) & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        HWND probe = CreateWindowExA(0, "#32770", NULL, style, 0, 0, 400, 300, NULL, NULL,
                                     GetModuleHandleA(NULL), NULL);
        if (probe)
        {
            ClientBorder(probe, &cached);
            DestroyWindow(probe);
        }

        if (cached.left <= 0 && cached.top <= 0)
        {
            RECT f = {0, 0, 0, 0};
            HMODULE u32 = GetModuleHandleA("user32.dll");
            typedef BOOL(WINAPI * ForDpi_t)(LPRECT, DWORD, BOOL, DWORD, UINT);
            ForDpi_t forDpi = u32 ? (ForDpi_t)GetProcAddress(u32, "AdjustWindowRectExForDpi") : NULL;
            if (!forDpi || !forDpi(&f, GAME_WINDOW_STYLE, FALSE, 0, (UINT)WorkAreaDpi()))
                AdjustWindowRect(&f, GAME_WINDOW_STYLE, FALSE);
            cached.left = -f.left;
            cached.top = -f.top;
            cached.right = f.right;
            cached.bottom = f.bottom;
            Log("MeasureGameFrame probe FAILED, API fallback L%d T%d R%d B%d", cached.left,
                cached.top, cached.right, cached.bottom);
        }
        else
        {
            Log("MeasureGameFrame probe ok L%d T%d R%d B%d (frame %dx%d)", cached.left, cached.top,
                cached.right, cached.bottom, cached.left + cached.right, cached.top + cached.bottom);
        }
    }

    *border = cached;
}

static void AvailArea(int *outW, int *outH)
{
    RECT work = {};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    RECT b = {};
    MeasureGameFrame(&b);

    *outW = work.right - work.left - b.left - b.right;
    *outH = work.bottom - work.top - b.top - b.bottom;
    Log("AvailArea dpi=%d work=%d,%d %dx%d frame=%dx%d -> %dx%d", WorkAreaDpi(), work.left, work.top,
        work.right - work.left, work.bottom - work.top, b.left + b.right, b.top + b.bottom, *outW, *outH);
}

static void MaxFit(int rw, int rh, BOOL bordered, int *outW, int *outH)
{
    RECT work = {};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);

    int winW = work.right - work.left;
    int winH = work.bottom - work.top;

    if (rw <= 0 || rh <= 0)
    {
        rw = 16;
        rh = 9;
    }

    int a = Gcd(rw, rh);
    int unitW = rw / a;
    int unitH = rh / a;

    RECT bd = {};
    if (bordered)
        MeasureGameFrame(&bd);

    int clientH = winH - bd.top;
    int clientW = (int)((double)clientH * unitW / unitH + 0.5);

    if (clientW > winW - bd.left - bd.right)
    {
        clientW = winW - bd.left - bd.right;
        clientH = (int)((double)clientW * unitH / unitW + 0.5);
    }

    Log("MaxFit bordered=%d work=%dx%d border=L%d,T%d,R%d,B%d aspect=%d:%d client=%dx%d window=%dx%d",
        bordered, winW, winH, bd.left, bd.top, bd.right, bd.bottom, unitW, unitH, clientW, clientH,
        clientW + bd.left + bd.right, clientH + bd.top + bd.bottom);

    *outW = clientW;
    *outH = clientH;
}

static void BuildSizeList(int rw, int rh)
{
    int prevSel = (int)SendMessageA(g_output, CB_GETCURSEL, 0, 0);
    int prevW = 0;
    int prevH = 0;
    BOOL hadPrev = (prevSel >= 0 && prevSel < g_sizeCount);
    if (hadPrev)
    {
        prevW = g_sizeW[prevSel];
        prevH = g_sizeH[prevSel];
    }

    g_sizeCount = 0;
    SendMessageA(g_output, CB_RESETCONTENT, 0, 0);

    static const struct { int id; const char *label; } kModes[] = {
        {SIZE_ID_MAXFIT, "Windowed maximised    title bar, fills work area"},
        {SIZE_ID_MAXFIT_NB, "Borderless maximised    fills work area, keeps taskbar"},
        {SIZE_ID_DESKTOP, "Borderless fullscreen    fills desktop, covers taskbar"},
        {SIZE_ID_DIVIDER, "--------  windowed sizes (title bar)  --------"},
    };

    for (int i = 0; i < _countof(kModes); ++i)
    {
        g_sizeW[g_sizeCount] = kModes[i].id;
        g_sizeH[g_sizeCount] = kModes[i].id;
        g_sizeCount++;
        SendMessageA(g_output, CB_ADDSTRING, 0, (LPARAM)kModes[i].label);
    }

    static const struct { int w, h, aw, ah; } kStandard[] = {
        {640, 480, 4, 3},
        {800, 600, 4, 3},
        {1024, 768, 4, 3},
        {1280, 720, 16, 9},
        {1152, 864, 4, 3},
        {1280, 800, 16, 10},
        {1366, 768, 16, 9},
        {1280, 960, 4, 3},
        {1440, 900, 16, 10},
        {1600, 900, 16, 9},
        {1400, 1050, 4, 3},
        {1680, 1050, 16, 10},
        {1600, 1200, 4, 3},
        {1920, 1080, 16, 9},
        {1920, 1200, 16, 10},
        {1920, 1440, 4, 3},
        {2048, 1536, 4, 3},
        {2560, 1440, 16, 9},
        {2560, 1600, 16, 10},
        {3840, 2160, 16, 9},
    };

    int fitW = 0;
    int fitH = 0;
    AvailArea(&fitW, &fitH);

    int g = Gcd(rw, rh);
    int gw = rw / g;
    int gh = rh / g;

    for (int i = 0; i < _countof(kStandard) && g_sizeCount < MAX_SIZES; ++i)
    {
        if (kStandard[i].w > fitW || kStandard[i].h > fitH)
            continue;

        BOOL match = (kStandard[i].aw == gw && kStandard[i].ah == gh);

        char label[96];
        _snprintf(label, sizeof(label), "%dx%d    %d:%d%s",
                  kStandard[i].w, kStandard[i].h, kStandard[i].aw, kStandard[i].ah,
                  match ? "    matches game aspect" : "");

        g_sizeW[g_sizeCount] = kStandard[i].w;
        g_sizeH[g_sizeCount] = kStandard[i].h;
        g_sizeCount++;

        SendMessageA(g_output, CB_ADDSTRING, 0, (LPARAM)label);
    }

    int keep = 0;
    if (hadPrev)
    {
        for (int i = 0; i < g_sizeCount; ++i)
        {
            if (g_sizeW[i] == prevW && g_sizeH[i] == prevH)
            {
                keep = i;
                break;
            }
        }
    }

    SendMessageA(g_output, CB_SETCURSEL, keep, 0);
}

static void Apply()
{
    int r = (int)SendMessageA(g_render, CB_GETCURSEL, 0, 0);
    int o = (int)SendMessageA(g_output, CB_GETCURSEL, 0, 0);
    if (r < 0 || o < 0)
        return;

    const char *renderRes = (r < _countof(kRender)) ? kRender[r].res : g_customRes;
    WriteBoth("inject_resolution", renderRes);

    int rw = 0, rh = 0;
    sscanf(renderRes, "%dx%d", &rw, &rh);

    char written[256] = {};
    for (int i = 0; i < _countof(kGameInis); ++i)
    {
        char path[MAX_PATH];
        _snprintf(path, sizeof(path), "%s\\%s", g_dir, kGameInis[i]);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
            continue;

        char v[16];
        _snprintf(v, sizeof(v), "%d", rw);
        BOOL ok = WritePrivateProfileStringA("Video", "ScreenWidth", v, path);
        _snprintf(v, sizeof(v), "%d", rh);
        ok = WritePrivateProfileStringA("Video", "ScreenHeight", v, path) && ok;
        WritePrivateProfileStringA(NULL, NULL, NULL, path);

        if (ok)
        {
            if (written[0])
                strcat(written, ", ");
            strcat(written, kGameInis[i]);
        }
    }

    int w = 0;
    int h = 0;
    BOOL bordered = TRUE;
    const char *fullscreen = "false";

    int pick = o < g_sizeCount ? g_sizeW[o] : SIZE_ID_MAXFIT;
    BOOL pinTop = FALSE;

    if (pick == SIZE_ID_DESKTOP)
    {
        bordered = FALSE;
        fullscreen = "true";
    }
    else if (pick == SIZE_ID_MAXFIT || pick == SIZE_ID_DIVIDER)
    {
        MaxFit(rw, rh, TRUE, &w, &h);
        pinTop = TRUE;
    }
    else if (pick == SIZE_ID_MAXFIT_NB)
    {
        bordered = FALSE;
        MaxFit(rw, rh, FALSE, &w, &h);
        pinTop = TRUE;
    }
    else
    {
        w = g_sizeW[o];
        h = g_sizeH[o];
    }

    char buf[32];
    _snprintf(buf, sizeof(buf), "%d", w);
    WriteBoth("width", buf);
    _snprintf(buf, sizeof(buf), "%d", h);
    WriteBoth("height", buf);
    WriteBoth("windowed", "true");
    WriteBoth("border", bordered ? "true" : "false");
    WriteBoth("fullscreen", fullscreen);
    WriteBoth("savesettings", "0");

    if (w > 0 && h > 0)
    {
        RECT work = {};
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);

        RECT b = {};
        if (bordered)
            MeasureGameFrame(&b);

        int winX = work.left + ((work.right - work.left) - (w + b.left + b.right)) / 2;
        int winY = pinTop ? work.top
                          : work.top + ((work.bottom - work.top) - (h + b.top + b.bottom)) / 2;

        int x = winX + b.left;
        int y = winY + b.top;

        Log("Apply client=%dx%d bordered=%d border=L%d,T%d,R%d,B%d work=%d,%d %dx%d pos=%d,%d", w, h,
            bordered, b.left, b.top, b.right, b.bottom, work.left, work.top, work.right - work.left,
            work.bottom - work.top, x, y);

        WriteBoth("center_window", "0");
        _snprintf(buf, sizeof(buf), "%d", x);
        WriteBoth("posX", buf);
        _snprintf(buf, sizeof(buf), "%d", y);
        WriteBoth("posY", buf);
    }
    else
    {
        WriteBoth("center_window", "1");
        WriteBoth("posX", "-32000");
        WriteBoth("posY", "-32000");
    }

    _snprintf(buf, sizeof(buf), "%d", o);
    WritePrivateProfileStringA("ddraw", "ra2video_mode", buf, g_ini);
    if (GetFileAttributesA(g_resIni) != INVALID_FILE_ATTRIBUTES)
        WritePrivateProfileStringA("ddraw", "ra2video_mode", buf, g_resIni);

    WritePrivateProfileStringA(NULL, NULL, NULL, g_ini);
    WritePrivateProfileStringA(NULL, NULL, NULL, g_resIni);

    char chain[MAX_PATH * 3] = {};
    for (int i = 0; i < 3; ++i)
    {
        int sel = (int)SendMessageA(g_pass[i], CB_GETCURSEL, 0, 0);
        if (sel <= 0 || sel > g_shaderCount)
            continue;

        if (chain[0])
            strcat(chain, "|");
        strcat(chain, g_shaders[sel - 1]);
    }
    WriteBoth("shader", chain);

    char msg[400];
    _snprintf(msg, sizeof(msg), "%dx%d written to %s. Restart the game.",
              rw, rh, written[0] ? written : "NO game ini - none found");
    SetWindowTextA(g_status, msg);
}

static void LoadCurrent()
{
    char cur[64] = {};
    char path[MAX_PATH];
    _snprintf(path, sizeof(path), "%s\\RA2MD.ini", g_dir);
    int sw = GetPrivateProfileIntA("Video", "ScreenWidth", 0, path);
    int sh = GetPrivateProfileIntA("Video", "ScreenHeight", 0, path);
    if (sw > 0 && sh > 0)
        _snprintf(cur, sizeof(cur), "%dx%d", sw, sh);
    else
        GetPrivateProfileStringA("ddraw", "inject_resolution", "", cur, sizeof(cur), g_ini);

    int sel = -1;
    for (int i = 0; i < _countof(kRender); ++i)
        if (_stricmp(cur, kRender[i].res) == 0)
            sel = i;
    if (sel < 0 && cur[0])
    {
        char custom[80];
        _snprintf(custom, sizeof(custom), "%s    (current, not a preset)", cur);
        sel = (int)SendMessageA(g_render, CB_ADDSTRING, 0, (LPARAM)custom);
        strncpy(g_customRes, cur, sizeof(g_customRes) - 1);
    }
    if (sel < 0)
        sel = 7;
    SendMessageA(g_render, CB_SETCURSEL, sel, 0);

    int w = GetPrivateProfileIntA("ddraw", "width", 0, g_ini);
    int h = GetPrivateProfileIntA("ddraw", "height", 0, g_ini);

    BuildSizeList(sw > 0 ? sw : 4, sh > 0 ? sh : 3);

    int osel = GetPrivateProfileIntA("ddraw", "ra2video_mode", 0, g_ini);
    if (osel < 0 || osel >= g_sizeCount)
    {
        osel = 0;
        for (int i = 0; i < g_sizeCount; ++i)
        {
            if (g_sizeW[i] == w && g_sizeH[i] == h && w > 0)
            {
                osel = i;
                break;
            }
        }
    }
    SendMessageA(g_output, CB_SETCURSEL, osel, 0);

    char shaders[MAX_PATH * 3] = {};
    GetPrivateProfileStringA("ddraw", "shader", "", shaders, sizeof(shaders), g_ini);

    char *ctx = NULL;
    char *tok = strtok_s(shaders, "|", &ctx);
    for (int i = 0; i < 3; ++i)
    {
        int sel = 0;
        if (tok)
        {
            while (*tok == ' ')
                tok++;

            for (int k = 0; k < g_shaderCount; ++k)
            {
                if (_stricmp(tok, g_shaders[k]) == 0)
                {
                    sel = k + 1;
                    break;
                }
            }
            tok = strtok_s(NULL, "|", &ctx);
        }
        SendMessageA(g_pass[i], CB_SETCURSEL, sel, 0);
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        MakeFont(h);

        HWND l1 = CreateWindowA("STATIC", "Game resolution (what the engine draws)",
                                WS_CHILD | WS_VISIBLE, S(14), S(14), S(360), S(18), h, NULL, NULL, NULL);
        g_render = CreateWindowA("COMBOBOX", NULL,
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                 S(14), S(34), S(380), S(300), h, (HMENU)101, NULL, NULL);

        HWND l2 = CreateWindowA("STATIC", "Output (window mode and size)",
                                WS_CHILD | WS_VISIBLE, S(14), S(74), S(360), S(18), h, NULL, NULL, NULL);
        g_output = CreateWindowA("COMBOBOX", NULL,
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                 S(14), S(94), S(380), S(300), h, (HMENU)102, NULL, NULL);

        HWND l3 = CreateWindowA("STATIC", "Shader passes (run in order, top to bottom)",
                                WS_CHILD | WS_VISIBLE, S(14), S(134), S(380), S(18), h, NULL, NULL, NULL);

        for (int i = 0; i < 3; ++i)
        {
            g_pass[i] = CreateWindowA("COMBOBOX", NULL,
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                      S(14), S(154 + i * 30), S(380), S(400), h, (HMENU)(105 + i), NULL, NULL);
        }

        HWND apply = CreateWindowA("BUTTON", "Apply",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                   S(234), S(252), S(76), S(26), h, (HMENU)103, NULL, NULL);
        HWND close = CreateWindowA("BUTTON", "Close",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                   S(318), S(252), S(76), S(26), h, (HMENU)104, NULL, NULL);
        g_status = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE,
                                 S(14), S(288), S(380), S(34), h, NULL, NULL, NULL);

        HWND kids[] = {l1, g_render, l2, g_output, l3, g_pass[0], g_pass[1], g_pass[2],
                       apply, close, g_status};
        for (int i = 0; i < _countof(kids); ++i)
            SendMessageA(kids[i], WM_SETFONT, (WPARAM)g_font, TRUE);

        for (int i = 0; i < _countof(kRender); ++i)
            SendMessageA(g_render, CB_ADDSTRING, 0, (LPARAM)kRender[i].label);

        g_shaderCount = 0;
        ScanShaders(g_dir, "Shaders\\");

        for (int i = 0; i < 3; ++i)
        {
            SendMessageA(g_pass[i], CB_ADDSTRING, 0, (LPARAM) "(none)");
            for (int k = 0; k < g_shaderCount; ++k)
                SendMessageA(g_pass[i], CB_ADDSTRING, 0, (LPARAM)(g_shaders[k] + 8));
        }

        LoadCurrent();
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == 101 && HIWORD(wp) == CBN_SELCHANGE)
        {
            int sel = (int)SendMessageA(g_render, CB_GETCURSEL, 0, 0);
            const char *res = (sel >= 0 && sel < _countof(kRender)) ? kRender[sel].res : g_customRes;
            int aw = 0, ah = 0;
            sscanf(res, "%dx%d", &aw, &ah);
            if (aw > 0 && ah > 0)
                BuildSizeList(aw, ah);
        }
        else if (LOWORD(wp) == 103)
            Apply();
        else if (LOWORD(wp) == 104)
            DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show)
{
    EnableDpiAwareness();
    IniPaths();
    if (GetFileAttributesA(g_ini) == INVALID_FILE_ATTRIBUTES)
    {
        MessageBoxA(NULL, "ddraw.ini not found. Put this exe in the game folder.",
                    "RA2 Video", MB_ICONERROR);
        return 1;
    }

    InitCommonControls();

    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = "RA2VideoCfg";
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    HWND h = CreateWindowA("RA2VideoCfg", "RA2 / Yuri's Revenge video settings",
                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                           CW_USEDEFAULT, CW_USEDEFAULT, 424, 254, NULL, NULL, inst, NULL);

    RECT want = {0, 0, S(410), S(332)};
    AdjustWindowRect(&want, (DWORD)GetWindowLongA(h, GWL_STYLE), FALSE);
    int cw = want.right - want.left;
    int ch = want.bottom - want.top;
    RECT work = {};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    SetWindowPos(h, NULL,
                 work.left + ((work.right - work.left) - cw) / 2,
                 work.top + ((work.bottom - work.top) - ch) / 2,
                 cw, ch, SWP_NOZORDER);
    ShowWindow(h, show);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0)
    {
        if (!IsDialogMessageA(h, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    return 0;
}
