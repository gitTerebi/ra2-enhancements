#include <windows.h>
#include "gameprofile.h"

struct SelectSite
{
    DWORD callAddr;
    BYTE callSig[10];
    BYTE objReg;
    DWORD resumeAddr;
    DWORD doneAddr;
    BYTE doneLen;
    BYTE doneSig[7];
};

struct FixProfile
{
    const char *name;
    DWORD allowVoice;
    DWORD selectSlot;
    SelectSite typeSelect;
    SelectSite combatantSelect;
};

enum { REG_ESI = 0xCE, REG_EDI = 0xCF };

static const FixProfile kFixes[] = {
    {"gamemd", 0x00822CF2, 0x14C,
     {0x00732B3D, {0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x90, 0x4C, 0x01, 0x00, 0x00}, REG_ESI, 0x00732B47,
      0x00732B4C, 5, {0xA1, 0xC8, 0xEC, 0xA8, 0x00}},
     {0x007324A0, {0x8B, 0x17, 0x8B, 0xCF, 0xFF, 0x92, 0x4C, 0x01, 0x00, 0x00}, REG_EDI, 0x007324AA,
      0x007324AF, 5, {0xA1, 0xC8, 0xEC, 0xA8, 0x00}}},
    {"game", 0x007D88D6, 0x140,
     {0x006F75AF, {0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x90, 0x40, 0x01, 0x00, 0x00}, REG_ESI, 0x006F75B9,
      0x006F75C7, 7, {0x83, 0xCA, 0xFF, 0x8D, 0x4C, 0x24, 0x1C}},
     {0x006F6F1F, {0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x90, 0x40, 0x01, 0x00, 0x00}, REG_ESI, 0x006F6F29,
      0x006F6F2E, 5, {0xA1, 0x70, 0x0C, 0xA4, 0x00}}},
};

static BYTE *g_allowVoice;
static DWORD g_selectSlot;

static void __fastcall SelectThenMute(void *obj)
{
    typedef bool (__thiscall *SelectFn)(void *);
    (*(SelectFn **)obj)[g_selectSlot / 4](obj);
    *g_allowVoice = 0;
}

static void __fastcall UnmuteC()
{
    *g_allowVoice = 1;
}

static void UseProfile(const FixProfile &f)
{
    g_allowVoice = (BYTE *)f.allowVoice;
    g_selectSlot = f.selectSlot;
}

static const FixProfile *SyringeProfile()
{
    if (!HostIsGame())
        return NULL;
    for (int i = 0; i < _countof(kFixes); ++i)
        if (strcmp(kFixes[i].name, "gamemd") == 0)
            return &kFixes[i];
    return NULL;
}

EXPORT_FUNC(TypeSelect_SelectOne)
{
    const FixProfile *f = SyringeProfile();
    if (!f)
        return 0;
    UseProfile(*f);
    SelectThenMute((void *)R->_ESI);
    return f->typeSelect.resumeAddr;
}

EXPORT_FUNC(TypeSelect_LoopDone)
{
    const FixProfile *f = SyringeProfile();
    if (f)
    {
        UseProfile(*f);
        UnmuteC();
    }
    return 0;
}

EXPORT_FUNC(CombatantSelect_SelectOne)
{
    const FixProfile *f = SyringeProfile();
    if (!f)
        return 0;
    UseProfile(*f);
    SelectThenMute((void *)R->_EDI);
    return f->combatantSelect.resumeAddr;
}

EXPORT_FUNC(CombatantSelect_LoopDone)
{
    const FixProfile *f = SyringeProfile();
    if (f)
    {
        UseProfile(*f);
        UnmuteC();
    }
    return 0;
}

static BYTE *g_stubs;
static BYTE *g_stubEnd;

static BYTE *Emit(const void *bytes, size_t n)
{
    memcpy(g_stubEnd, bytes, n);
    g_stubEnd += n;
    return g_stubEnd;
}

static void EmitByte(BYTE b) { Emit(&b, 1); }
static void EmitDword(DWORD d) { Emit(&d, 4); }

static void EmitJmp(DWORD target)
{
    EmitByte(0xE9);
    EmitDword(target - ((DWORD)g_stubEnd + 4));
}

static void EmitCallAbs(const void *fn)
{
    EmitByte(0xE8);
    EmitDword((DWORD)fn - ((DWORD)g_stubEnd + 4));
}

static bool WriteJmp(DWORD at, BYTE stolen, const void *stub)
{
    BYTE patch[16];
    patch[0] = 0xE9;
    *(DWORD *)(patch + 1) = (DWORD)stub - (at + 5);
    for (int i = 5; i < stolen; ++i)
        patch[i] = 0x90;

    DWORD old = 0;
    if (!VirtualProtect((void *)at, stolen, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy((void *)at, patch, stolen);
    VirtualProtect((void *)at, stolen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)at, stolen);
    return true;
}

static bool SiteLooksRight(const SelectSite &s)
{
    return memcmp((const void *)s.callAddr, s.callSig, sizeof(s.callSig)) == 0 &&
           memcmp((const void *)s.doneAddr, s.doneSig, s.doneLen) == 0;
}

static void PatchSite(const SelectSite &s)
{
    BYTE *selectStub = g_stubEnd;
    EmitByte(0x8B); EmitByte(s.objReg);
    EmitCallAbs(SelectThenMute);
    EmitJmp(s.resumeAddr);

    BYTE *doneStub = g_stubEnd;
    EmitByte(0x60);
    EmitCallAbs(UnmuteC);
    EmitByte(0x61);
    Emit((const void *)s.doneAddr, s.doneLen);
    EmitJmp(s.doneAddr + s.doneLen);

    WriteJmp(s.callAddr, 10, selectStub);
    WriteJmp(s.doneAddr, s.doneLen, doneStub);
}

static void InstallInline()
{
    const GameProfile *g = DetectGame();
    if (!g)
        return;
    const FixProfile *f = NULL;
    for (int i = 0; i < _countof(kFixes); ++i)
        if (strcmp(kFixes[i].name, g->name) == 0)
            f = &kFixes[i];
    if (!f)
        return;
    if (!SiteLooksRight(f->typeSelect) || !SiteLooksRight(f->combatantSelect))
        return;

    g_stubs = (BYTE *)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_stubs)
        return;
    g_stubEnd = g_stubs;
    UseProfile(*f);
    PatchSite(f->typeSelect);
    PatchSite(f->combatantSelect);
}

BOOL WINAPI DllMain(HINSTANCE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(mod);
        if (!HostIsGame() || IsDebuggerPresent())
            return TRUE;
        InstallInline();
    }
    return TRUE;
}
