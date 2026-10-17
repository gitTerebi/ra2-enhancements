#include <intrin.h>
#include "patchguard.h"
#include "dsoundforward.h"

static const char *kSidecars[] = {"HWCursor.dll", "WasdScroll.dll", "RA2Enhancements.dll"};

typedef DWORD(WINAPI *ThreadIdProc)();
typedef HMODULE(WINAPI *LoadLibraryProc)(LPCSTR);
typedef DWORD(__cdecl *InstallProc)(REGISTERS *);

static ThreadIdProc *g_threadIdSlot;
static ThreadIdProc g_realThreadId;
static LONG g_fired;
static LoadLibraryProc *g_loadLibrarySlot;
static LoadLibraryProc g_realLoadLibrary;
static void *g_heldBase;

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

static void SetSlot(void *slot, void *fn)
{
    DWORD old;
    if (VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old))
    {
        *(void **)slot = fn;
        VirtualProtect(slot, sizeof(void *), old, &old);
    }
}

static void ReleaseHeldBase()
{
    void *held = InterlockedExchangePointer(&g_heldBase, NULL);
    if (held)
        VirtualFree(held, 0, MEM_RELEASE);
    if (g_loadLibrarySlot)
    {
        SetSlot(g_loadLibrarySlot, g_realLoadLibrary);
        g_loadLibrarySlot = NULL;
    }
}

static HMODULE WINAPI LoadLibraryHook(LPCSTR name)
{
    const char *file = name ? strrchr(name, '\\') : NULL;
    file = file ? file + 1 : name;
    if (file && _stricmp(file, "cncnet5.dll") == 0)
        ReleaseHeldBase();
    return g_realLoadLibrary(name);
}

static DWORD WINAPI FirstThreadIdCall()
{
    if (InExe(_ReturnAddress()) && !InterlockedExchange(&g_fired, 1))
    {
        SetSlot(g_threadIdSlot, g_realThreadId);
        ReleaseHeldBase();
        LoadSidecars();
    }
    return g_realThreadId();
}

static void **FindKernel32Slot(const char *fn)
{
    BYTE *exe = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress)
        return NULL;

    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(exe + dir.VirtualAddress); d->Name; ++d)
    {
        if (_stricmp((char *)(exe + d->Name), "kernel32.dll") != 0 || !d->OriginalFirstThunk)
            continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(exe + d->OriginalFirstThunk);
        void **slots = (void **)(exe + d->FirstThunk);
        for (int i = 0; names[i].u1.AddressOfData; ++i)
        {
            if (IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal))
                continue;
            IMAGE_IMPORT_BY_NAME *imp = (IMAGE_IMPORT_BY_NAME *)(exe + names[i].u1.AddressOfData);
            if (strcmp((char *)imp->Name, fn) == 0)
                return &slots[i];
        }
    }
    return NULL;
}

static void HookFirstThreadIdCall()
{
    void **slot = FindKernel32Slot("GetCurrentThreadId");
    if (!slot)
        return;
    g_threadIdSlot = (ThreadIdProc *)slot;
    g_realThreadId = *g_threadIdSlot;
    SetSlot(slot, FirstThreadIdCall);
}

static void HoldUntilCncnet5Loads()
{
    void **slot = FindKernel32Slot("LoadLibraryA");
    if (!slot)
    {
        ReleaseHeldBase();
        return;
    }
    g_realLoadLibrary = *(LoadLibraryProc *)slot;
    g_loadLibrarySlot = (LoadLibraryProc *)slot;
    SetSlot(slot, LoadLibraryHook);
}

static void *ReserveDllBase(const char *dll)
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, sizeof(path));
    char *slash = n && n < sizeof(path) ? strrchr(path, '\\') : NULL;
    if (!slash || strlen(dll) >= sizeof(path) - (slash + 1 - path))
        return NULL;
    strcpy(slash + 1, dll);

    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return NULL;
    BYTE head[1024];
    DWORD got = 0;
    BOOL read = ReadFile(f, head, sizeof(head), &got, NULL);
    CloseHandle(f);
    if (!read || got < sizeof(IMAGE_DOS_HEADER) || ((IMAGE_DOS_HEADER *)head)->e_magic != IMAGE_DOS_SIGNATURE)
        return NULL;
    LONG at = ((IMAGE_DOS_HEADER *)head)->e_lfanew;
    if (at < 0 || at + sizeof(IMAGE_NT_HEADERS32) > got)
        return NULL;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(head + at);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return NULL;
    return VirtualAlloc((void *)(ULONG_PTR)nt->OptionalHeader.ImageBase, nt->OptionalHeader.SizeOfImage, MEM_RESERVE,
                        PAGE_NOACCESS);
}

BOOL WINAPI DllMain(HINSTANCE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(mod);
        bool game = HostIsGame();
        g_heldBase = game ? ReserveDllBase("cncnet5.dll") : NULL;
        DSoundLoadReal();
        if (game)
        {
            HookFirstThreadIdCall();
            if (g_heldBase)
                HoldUntilCncnet5Loads();
        }
    }
    return TRUE;
}
