#include "patchguard.h"
#include "dsoundforward.h"

static const char *kSidecars[] = {"HWCursor.dll", "WasdScroll.dll", "RA2Enhancements.dll"};

static DWORD WINAPI LoadThread(LPVOID)
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, sizeof(path));
    if (!n || n >= sizeof(path))
        return 0;
    char *slash = strrchr(path, '\\');
    if (!slash)
        return 0;
    size_t room = sizeof(path) - (slash + 1 - path);

    for (int i = 0; i < _countof(kSidecars); ++i)
    {
        if (strlen(kSidecars[i]) >= room || GetModuleHandleA(kSidecars[i]))
            continue;
        strcpy(slash + 1, kSidecars[i]);
        LoadLibraryA(path);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(mod);
        DSoundLoadReal();
        if (!HostIsGame() || IsDebuggerPresent())
            return TRUE;
        CloseHandle(CreateThread(NULL, 0, LoadThread, NULL, 0, NULL));
    }
    return TRUE;
}
