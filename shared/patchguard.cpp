#include <string.h>
#include "patchguard.h"

bool BesideExe(const char *name, char *out, size_t outSize)
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

void SpeedLog(const char *fmt, ...)
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

static bool Poke(DWORD at, const BYTE *bytes, size_t n)
{
    DWORD old = 0;
    if (!VirtualProtect((void *)at, n, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy((void *)at, bytes, n);
    VirtualProtect((void *)at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)at, n);
    return true;
}

static const BYTE *OriginalBytes(DWORD at, size_t n)
{
    static bool s_mapped;
    static const BYTE *s_view;
    static DWORD s_size;
    if (!s_mapped)
    {
        s_mapped = true;
        char path[MAX_PATH];
        DWORD len = GetModuleFileNameA(NULL, path, sizeof(path));
        HANDLE file = len && len < sizeof(path) ? CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                                                              OPEN_EXISTING, 0, NULL)
                                                : INVALID_HANDLE_VALUE;
        if (file != INVALID_HANDLE_VALUE)
        {
            s_size = GetFileSize(file, NULL);
            HANDLE map = CreateFileMappingA(file, NULL, PAGE_READONLY, 0, 0, NULL);
            if (map)
            {
                s_view = (const BYTE *)MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
                CloseHandle(map);
            }
            CloseHandle(file);
        }
        if (!s_view)
            SpeedLog("  patch guard: cannot read the game exe, no patches applied");
    }
    if (!s_view || s_size < sizeof(IMAGE_DOS_HEADER))
        return NULL;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)s_view;
    if ((DWORD)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS32) > s_size)
        return NULL;
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(s_view + dos->e_lfanew);
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    if ((const BYTE *)(sec + nt->FileHeader.NumberOfSections) > s_view + s_size)
        return NULL;
    DWORD rva = at - nt->OptionalHeader.ImageBase;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        if (rva < sec[i].VirtualAddress || rva + n > sec[i].VirtualAddress + sec[i].SizeOfRawData)
            continue;
        DWORD off = sec[i].PointerToRawData + (rva - sec[i].VirtualAddress);
        return off + n <= s_size ? s_view + off : NULL;
    }
    return NULL;
}

static bool SiteIsOurs(DWORD at, const BYTE *bytes, size_t n)
{
    const BYTE *orig = OriginalBytes(at, n);
    if (!orig || IsBadReadPtr((const void *)at, n))
        return false;
    return memcmp((const void *)at, orig, n) == 0 || memcmp(bytes, orig, n) == 0;
}

struct PendingWrite
{
    DWORD at;
    size_t n;
    BYTE bytes[32];
};

static PendingWrite g_pending[16];
static int g_pendingCount;
static bool g_collecting;
static bool g_collectFailed;

bool WriteBytes(DWORD at, const BYTE *bytes, size_t n)
{
    if (!SiteIsOurs(at, bytes, n))
    {
        SpeedLog("  site %X was already changed by another mod, left alone", at);
        g_collectFailed = true;
        return false;
    }
    if (!g_collecting)
        return Poke(at, bytes, n);
    if (g_pendingCount >= _countof(g_pending) || n > sizeof(g_pending[0].bytes))
    {
        SpeedLog("  site %X: too many writes in one patch", at);
        g_collectFailed = true;
        return false;
    }
    PendingWrite &w = g_pending[g_pendingCount++];
    w.at = at;
    w.n = n;
    memcpy(w.bytes, bytes, n);
    return true;
}

static bool WriteBranch(BYTE opcode, DWORD at, const void *target, size_t len)
{
    BYTE patch[32];
    if (len < 5 || len > sizeof(patch))
        return false;
    patch[0] = opcode;
    *(DWORD *)(patch + 1) = (DWORD)target - (at + 5);
    memset(patch + 5, 0x90, len - 5);
    return WriteBytes(at, patch, len);
}

bool WriteCall(DWORD at, const void *target, size_t len)
{
    return WriteBranch(0xE8, at, target, len);
}

bool WriteJmp(DWORD at, const void *target, size_t len)
{
    return WriteBranch(0xE9, at, target, len);
}

bool InstallPatch(const char *name, void (*install)())
{
    g_collecting = true;
    g_collectFailed = false;
    g_pendingCount = 0;
    install();
    g_collecting = false;
    if (g_collectFailed)
    {
        SpeedLog("  %s off, because another mod changed its code first", name);
        return false;
    }
    for (int i = 0; i < g_pendingCount; ++i)
        Poke(g_pending[i].at, g_pending[i].bytes, g_pending[i].n);
    return g_pendingCount > 0;
}
