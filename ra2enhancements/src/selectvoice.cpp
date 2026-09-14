#include "patch.h"
#include "features.h"

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

static bool SiteLooksRight(const SelectSite &s)
{
    return BytesAt(s.callAddr, s.callSig, sizeof(s.callSig)) && BytesAt(s.doneAddr, s.doneSig, s.doneLen);
}

static void PatchSite(const SelectSite &s)
{
    BYTE *selectStub = StubCursor();
    EmitByte(0x8B); EmitByte(s.objReg);
    EmitCallAbs(SelectThenMute);
    EmitJmp(s.resumeAddr);

    BYTE *doneStub = StubCursor();
    EmitByte(0x60);
    EmitCallAbs(UnmuteC);
    EmitByte(0x61);
    Emit((const void *)s.doneAddr, s.doneLen);
    EmitJmp(s.doneAddr + s.doneLen);

    WriteJmp(s.callAddr, selectStub, 10);
    WriteJmp(s.doneAddr, doneStub, s.doneLen);
}

void InstallSelectVoice()
{
    const FixProfile *f = ForThisGame(kFixes);
    if (!f || !SiteLooksRight(f->typeSelect) || !SiteLooksRight(f->combatantSelect) || !ReserveStubs())
        return;
    g_allowVoice = (BYTE *)f->allowVoice;
    g_selectSlot = f->selectSlot;
    PatchSite(f->typeSelect);
    PatchSite(f->combatantSelect);
}
