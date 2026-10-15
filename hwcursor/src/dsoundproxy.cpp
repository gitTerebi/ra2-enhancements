#include <intrin.h>
#include "patchguard.h"
#include "dsoundforward.h"

static const char *kSidecars[] = {"HWCursor.dll", "WasdScroll.dll", "RA2Enhancements.dll"};

typedef DWORD(WINAPI *ThreadIdProc)();
typedef DWORD(__cdecl *InstallProc)(REGISTERS *);

static ThreadIdProc *g_threadIdSlot;
static ThreadIdProc g_realThreadId;
static LONG g_fired;

static void LoadSidecars()
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, sizeof(path));
    if (!n || n >= sizeof(path))
        return;
    char *slash = strrchr(path, '\\');
    if (!slash)
        return;
    size_t room = sizeof(path) - (slash + 1 - path);

    for (int i = 0; i < _countof(kSidecars); ++i)
    {
        if (strlen(kSidecars[i]) >= room)
            continue;
        strcpy(slash + 1, kSidecars[i]);
        HMODULE mod = GetModuleHandleA(path);
        if (!mod)
            mod = LoadLibraryA(path);
        if (!mod)
            continue;

        char name[64];
        strcpy(name, kSidecars[i]);
        strcpy(strrchr(name, '.'), "_Install");
        InstallProc install = (InstallProc)GetProcAddress(mod, name);
        REGISTERS regs = {};
        if (install)
            install(&regs);
    }
}

static bool InExe(const void *at)
{
    BYTE *exe = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
    return at >= exe && at < exe + nt->OptionalHeader.SizeOfImage;
}

static void SetSlot(ThreadIdProc fn)
{
    DWORD old;
    if (VirtualProtect(g_threadIdSlot, sizeof(*g_threadIdSlot), PAGE_READWRITE, &old))
    {
        *g_threadIdSlot = fn;
        VirtualProtect(g_threadIdSlot, sizeof(*g_threadIdSlot), old, &old);
    }
}

static DWORD WINAPI FirstThreadIdCall()
{
    if (InExe(_ReturnAddress()) && !InterlockedExchange(&g_fired, 1))
    {
        SetSlot(g_realThreadId);
        LoadSidecars();
    }
    return g_realThreadId();
}

static void HookFirstThreadIdCall()
{
    BYTE *exe = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress)
        return;

    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(exe + dir.VirtualAddress); d->Name; ++d)
    {
        if (_stricmp((char *)(exe + d->Name), "kernel32.dll") != 0 || !d->OriginalFirstThunk)
            continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(exe + d->OriginalFirstThunk);
        ThreadIdProc *slots = (ThreadIdProc *)(exe + d->FirstThunk);
        for (int i = 0; names[i].u1.AddressOfData; ++i)
        {
            if (IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal))
                continue;
            IMAGE_IMPORT_BY_NAME *imp = (IMAGE_IMPORT_BY_NAME *)(exe + names[i].u1.AddressOfData);
            if (strcmp((char *)imp->Name, "GetCurrentThreadId") != 0)
                continue;
            g_threadIdSlot = &slots[i];
            g_realThreadId = slots[i];
            SetSlot(FirstThreadIdCall);
            return;
        }
    }
}

BOOL WINAPI DllMain(HINSTANCE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(mod);
        DSoundLoadReal();
        if (HostIsGame())
            HookFirstThreadIdCall();
    }
    return TRUE;
}
