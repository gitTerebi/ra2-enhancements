#include "patch.h"
#include "features.h"

typedef bool (__stdcall *LoadWrapFn)(const char *file);
typedef bool (__stdcall *SaveWrapFn)(const char *file, const wchar_t *description);

struct SaveProfile
{
    const char *name;
    DWORD confirmJump;
    DWORD savedBox;
    DWORD loadWrap;
    DWORD saveWrap;
    DWORD mouseInstance;
    DWORD session;
    DWORD sessionPauseCall;
    DWORD sessionPause;
    DWORD sessionResume;
    DWORD sessionRunningOffset;
    DWORD menuCall;
    DWORD menu;
    DWORD swapMiss;
    DWORD saveGameCall;
    DWORD saveGame;
    DWORD particles;
    DWORD particleSystems;
    DWORD particleSystemOffset;
    DWORD systemParticleList;
    DWORD deleteQueue;
    DWORD menuKeyCall;
    DWORD keyboardGet;
    DWORD loadMenuCall;
    DWORD loadMenu;
    DWORD mainMenuCall;
    DWORD mainMenu;
    DWORD inputBlocked;
    DWORD savePattern;
};

static const SaveProfile kSaves[] = {
    {"gamemd", 0x00559353, 0x0055943A, 0x00559D60, 0x00559E40, 0x00887640, 0x00A8B238, 0x00559DBC,
     0x0069BB40, 0x0069BAB0, 0x30D8, 0x0048CE9C, 0x0048C8B0, 0x006CF3CF, 0x00559E85, 0x0067CEF0,
     0x00A83DC8, 0x00A80208, 0x124, 0xBC, 0x00B0F698, 0x00531E05, 0x0054F050, 0x0052E0E1, 0x005587F0, 0x0052DC9A,
     0x00531CC0, 0x00A8ED9C, 0x00559C3C},
    {"game", 0x0053BCE7, 0x0053BDCE, 0x0053C680, 0x0053C760, 0x00839C68, 0x00A3D298, 0x0053C6DC,
     0x00672E70, 0x00672DE0, 0x3080, 0x00481DCC, 0x00481850, 0x0069FCAF, 0x0053C7A5, 0x006554A0,
     0x00A35E28, 0x00A32280, 0x114, 0xA8, 0x00AC0CD8, 0x0051706A, 0x00532320, 0x00513D68, 0x0053B190, 0x005138E9,
     0x00516F00, 0x00A40D44, 0x0053C5D1},
};

static const BYTE kSwapMissSig[22] = {0xC7, 0x44, 0x24, 0x10, 0x00, 0x00, 0x00, 0x00, 0xB8, 0x01, 0x00,
                                      0x00, 0x00, 0x99, 0xF7, 0x7C, 0x24, 0x10, 0x89, 0x44, 0x24, 0x10};
static DWORD g_clearedPointers;
static BYTE *g_swapMissStub;

struct GameVector
{
    void *vtable;
    void **items;
    int capacity;
    bool isInitialized;
    bool isAllocated;
    int count;
};

struct SwapPair
{
    DWORD oldPtr;
    DWORD newPtr;
};

static void __cdecl ClearSwapMiss(DWORD field, const BYTE *swizzard)
{
    if (++g_clearedPointers > 20)
        return;
    const SwapPair *pairs = *(const SwapPair **)(swizzard + 0x20);
    int count = *(const int *)(swizzard + 0x2C);
    DWORD owner = 0;
    for (int i = 0; i < count; ++i)
        if (pairs[i].newPtr <= field && pairs[i].newPtr > owner)
            owner = pairs[i].newPtr;
    DWORD vtable = owner && !IsBadReadPtr((void *)owner, 4) ? *(DWORD *)owner : 0;
    SpeedLog("  quick load: cleared field +%X of object vtable %08X", field - owner, vtable);
}

static const BYTE kConfirmJumpSig[4] = {0x74, 0x6A, 0x6A, 0x00};
static const BYTE kSavedBoxSig[7] = {0x8B, 0x16, 0x8B, 0xCE, 0xFF, 0x52, 0x20};
static const BYTE kLoadWrapSig[5] = {0x68, 0x1C, 0x04, 0x00, 0x00};
static const BYTE kSaveWrapSig[5] = {0x68, 0x53, 0x04, 0x00, 0x00};
static const char *const kQuickSaveFiles[2] = {"QUICK.SAV", "QUICK2.SAV"};
static const wchar_t *const kQuickSaveNames[2] = {L"Quick Save", L"Quick Save 2"};

static const SaveProfile *g_save;

static void InstallSaveTweaks()
{
    const GameProfile *g = DetectGame();
    if (!g)
        return;
    const SaveProfile *s = NULL;
    for (int i = 0; i < _countof(kSaves); ++i)
        if (strcmp(kSaves[i].name, g->name) == 0)
            s = &kSaves[i];
    if (!s)
        return;

    if (!BytesAt(s->confirmJump, kConfirmJumpSig, sizeof(kConfirmJumpSig)) ||
        !BytesAt(s->savedBox, kSavedBoxSig, sizeof(kSavedBoxSig)))
    {
        SpeedLog("  save boxes: sites %X/%X unexpected, skipped", s->confirmJump, s->savedBox);
    }
    else
    {
        static const BYTE skipConfirm = 0xEB;
        static const BYTE skipSaved[2] = {0xEB, 0x36};
        WriteBytes(s->confirmJump, &skipConfirm, 1);
        WriteBytes(s->savedBox, skipSaved, 2);
        SpeedLog("  save boxes removed at %X and %X", s->confirmJump, s->savedBox);
    }

    if (!BytesAt(s->loadWrap + 0x10, kLoadWrapSig, sizeof(kLoadWrapSig)) ||
        !BytesAt(s->saveWrap + 0x04, kSaveWrapSig, sizeof(kSaveWrapSig)))
    {
        SpeedLog("  quick save: wrappers %X/%X unexpected, skipped", s->loadWrap, s->saveWrap);
        return;
    }
    static const BYTE mouseLoadSig[2] = {0x8B, 0x0D};
    if (!BytesAt(s->loadWrap, mouseLoadSig, sizeof(mouseLoadSig)) ||
        !BytesAt(s->loadWrap + 2, (const BYTE *)&s->mouseInstance, sizeof(s->mouseInstance)))
        return;
    BYTE runningTest[10] = {0x8A, 0x81, 0, 0, 0, 0, 0x84, 0xC0, 0x0F, 0x85};
    memcpy(runningTest + 2, &s->sessionRunningOffset, 4);
    if (!CallGoesTo(s->sessionPauseCall, s->sessionPause) ||
        !BytesAt(s->sessionResume, runningTest, sizeof(runningTest)) || !CallGoesTo(s->menuCall, s->menu) ||
        !CallGoesTo(s->saveGameCall, s->saveGame) || !CallGoesTo(s->menuKeyCall, s->keyboardGet) ||
        !CallGoesTo(s->loadMenuCall, s->loadMenu) || !CallGoesTo(s->mainMenuCall, s->mainMenu))
    {
        SpeedLog("  quick save: engine call sites unexpected, skipped");
        return;
    }
    g_save = s;
}

static int __cdecl ComparePointers(const void *a, const void *b)
{
    DWORD x = *(const DWORD *)a, y = *(const DWORD *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static void __cdecl ClearOrphanParticles()
{
    const GameVector *particles = (const GameVector *)g_save->particles;
    const GameVector *systems = (const GameVector *)g_save->particleSystems;
    const GameVector *queue = (const GameVector *)g_save->deleteQueue;

    int owned = queue->count;
    for (int j = 0; j < systems->count; ++j)
        owned += ((const GameVector *)((BYTE *)systems->items[j] + g_save->systemParticleList))->count;
    DWORD *known = (DWORD *)malloc((owned + 1) * sizeof(DWORD));
    if (!known)
        return;
    int n = 0;
    for (int j = 0; j < queue->count; ++j)
        known[n++] = (DWORD)queue->items[j];
    for (int j = 0; j < systems->count; ++j)
    {
        const GameVector *list = (const GameVector *)((BYTE *)systems->items[j] + g_save->systemParticleList);
        for (int k = 0; k < list->count; ++k)
            known[n++] = (DWORD)list->items[k];
    }
    qsort(known, n, sizeof(DWORD), ComparePointers);

    typedef void *(__thiscall *DeletingDtorFn)(void *self, int flags);
    int deleted = 0, cleared = 0;
    for (int i = particles->count - 1; i >= 0; --i)
    {
        void *particle = particles->items[i];
        if (!bsearch(&particle, known, n, sizeof(DWORD), ComparePointers))
        {
            (*(DeletingDtorFn **)particle)[0x20 / 4](particle, 1);
            ++deleted;
            continue;
        }
        void **system = (void **)((BYTE *)particle + g_save->particleSystemOffset);
        if (!*system)
            continue;
        bool alive = false;
        for (int j = 0; j < systems->count && !alive; ++j)
            alive = systems->items[j] == *system;
        if (!alive)
        {
            *system = NULL;
            ++cleared;
        }
    }
    free(known);
    if (deleted || cleared)
        SpeedLog("  save: deleted %d leaked particles, cleared %d pointers to deleted particle systems", deleted,
                 cleared);
}

static bool QuickSavePath(int slot, char *out, size_t outSize)
{
    const char *dir = "";
    size_t dirLen = 0;
    const BYTE *push = (const BYTE *)g_save->savePattern;
    if (!IsBadReadPtr(push, 5) && push[0] == 0x68)
    {
        const char *pattern = *(const char **)(push + 1);
        const char *star = IsBadStringPtrA(pattern, MAX_PATH) ? NULL : strchr(pattern, '*');
        if (star)
        {
            dir = pattern;
            dirLen = star - pattern;
        }
    }
    char name[MAX_PATH];
    if (dirLen + strlen(kQuickSaveFiles[slot]) >= sizeof(name))
        return false;
    memcpy(name, dir, dirLen);
    strcpy(name + dirLen, kQuickSaveFiles[slot]);
    return BesideExe(name, out, outSize);
}

static int NewestQuickSave()
{
    FILETIME written[2];
    bool found[2];
    for (int i = 0; i < 2; ++i)
    {
        char path[MAX_PATH];
        WIN32_FILE_ATTRIBUTE_DATA data;
        found[i] = QuickSavePath(i, path, sizeof(path)) &&
                   GetFileAttributesExA(path, GetFileExInfoStandard, &data);
        if (found[i])
            written[i] = data.ftLastWriteTime;
    }
    if (found[0] && found[1])
        return CompareFileTime(&written[1], &written[0]) > 0 ? 1 : 0;
    return found[0] ? 0 : found[1] ? 1 : -1;
}

static bool g_quickSavePending;
static bool g_quickLoadPending;

void QueueQuickSave()
{
    g_quickSavePending = true;
}

void QueueQuickLoad()
{
    g_quickLoadPending = true;
}

static bool LoadQuickSave(int slot)
{
    g_clearedPointers = 0;
    bool tolerant = g_swapMissStub && BytesAt(g_save->swapMiss, kSwapMissSig, sizeof(kSwapMissSig)) &&
                    WriteJmp(g_save->swapMiss, g_swapMissStub, sizeof(kSwapMissSig));
    bool ok = ((LoadWrapFn)g_save->loadWrap)(kQuickSaveFiles[slot]);
    if (tolerant)
        WriteBytes(g_save->swapMiss, kSwapMissSig, sizeof(kSwapMissSig));
    ForgetEscorts();
    if (g_clearedPointers)
        SpeedLog("  quick load: cleared %d pointers to objects missing from the save", g_clearedPointers);
    SpeedLog("  quick load %s %s", kQuickSaveFiles[slot], ok ? "done" : "failed");
    return ok;
}

static void RunPendingSaves(bool allowed)
{
    bool save = g_quickSavePending, load = g_quickLoadPending;
    g_quickSavePending = g_quickLoadPending = false;
    if (!g_save || !allowed)
        return;
    if (save)
    {
        int slot = NewestQuickSave() == 0 ? 1 : 0;
        bool ok = ((SaveWrapFn)g_save->saveWrap)(kQuickSaveFiles[slot], kQuickSaveNames[slot]);
        SpeedLog("  quick save %s %s", kQuickSaveFiles[slot], ok ? "written" : "failed");
    }
    if (!load)
        return;
    int slot = NewestQuickSave();
    if (slot < 0)
    {
        SpeedLog("  quick load: no quick save yet");
        return;
    }
    typedef void (__thiscall *SessionResumeFn)(void *session);
    LoadQuickSave(slot);
    ((SessionResumeFn)g_save->sessionResume)((void *)g_save->session);
}

static void __cdecl RunPendingSavesAfterFrame()
{
    RunPendingSaves(SinglePlayerMode());
}

static bool g_menuQuickLoad;

static bool __cdecl MenuKeyPressed(int key)
{
    if (!g_save || (key & WWKEY_RELEASE))
        return false;
    int bound = VK_F9;
    char ini[MAX_PATH];
    if (KeyboardIni() && BesideExe(KeyboardIni(), ini, sizeof(ini)))
        bound = GetPrivateProfileIntA("Hotkey", "QuickLoad", VK_F9, ini);
    if (bound <= 0 || (key & 0x7FF) != (bound & 0x7FF))
        return false;
    if (NewestQuickSave() < 0)
    {
        SpeedLog("  menu quick load: no quick save yet");
        return false;
    }
    g_menuQuickLoad = true;
    return true;
}

static bool __cdecl MenuQuickLoad()
{
    g_menuQuickLoad = false;
    int slot = NewestQuickSave();
    if (slot < 0)
        return false;
    typedef void (__thiscall *MouseCountFn)(void *mouse);
    void *mouse = *(void **)g_save->mouseInstance;
    if (mouse)
        (*(MouseCountFn **)mouse)[0x0C / 4](mouse);
    bool ok = LoadQuickSave(slot);
    mouse = *(void **)g_save->mouseInstance;
    if (!ok && mouse)
        (*(MouseCountFn **)mouse)[0x10 / 4](mouse);
    return ok;
}

static void InstallSaveHook()
{
    if (!g_save)
        return;
    if (!ReserveStubs())
    {
        g_save = NULL;
        return;
    }

    BYTE *stub = StubCursor();
    EmitByte(0x60);
    EmitByte(0x9C);
    EmitCallAbs(RunPendingSavesAfterFrame);
    EmitByte(0x9D);
    EmitByte(0x61);
    EmitJmp(g_save->menu);

    g_swapMissStub = StubCursor();
    EmitByte(0x60);
    EmitByte(0x9C);
    EmitByte(0x56);
    EmitByte(0xFF); EmitByte(0x77); EmitByte(0x04);
    EmitCallAbs(ClearSwapMiss);
    EmitByte(0x83); EmitByte(0xC4); EmitByte(0x08);
    EmitByte(0x9D);
    EmitByte(0x61);
    EmitByte(0x8B); EmitByte(0x57); EmitByte(0x04);
    EmitByte(0xC7); EmitByte(0x02); EmitDword(0);
    EmitByte(0x83); EmitByte(0xC7); EmitByte(0x08);
    EmitByte(0x4D);
    EmitJmp(g_save->swapMiss + sizeof(kSwapMissSig));

    BYTE *saveStub = StubCursor();
    EmitByte(0x60);
    EmitByte(0x9C);
    EmitCallAbs(ClearOrphanParticles);
    EmitByte(0x9D);
    EmitByte(0x61);
    EmitJmp(g_save->saveGame);

    BYTE *menuKeyStub = StubCursor();
    EmitCallAbs((const void *)g_save->keyboardGet);
    EmitByte(0x50);
    EmitByte(0x52);
    EmitByte(0x51);
    EmitByte(0x50);
    EmitCallAbs(MenuKeyPressed);
    EmitByte(0x83); EmitByte(0xC4); EmitByte(0x04);
    EmitByte(0x59);
    EmitByte(0x5A);
    EmitByte(0x84); EmitByte(0xC0);
    EmitByte(0x58);
    EmitByte(0x74); EmitByte(0x08);
    EmitByte(0xC7); EmitByte(0x44); EmitByte(0x24); EmitByte(0x18); EmitDword(9);
    EmitByte(0xC3);

    BYTE *loadMenuStub = StubCursor();
    EmitByte(0x80); EmitByte(0x3D); EmitDword((DWORD)&g_menuQuickLoad); EmitByte(0x00);
    EmitByte(0x75); EmitByte(0x05);
    EmitJmp(g_save->loadMenu);
    EmitByte(0x51);
    EmitByte(0x52);
    EmitCallAbs(MenuQuickLoad);
    EmitByte(0x5A);
    EmitByte(0x59);
    EmitByte(0xC3);

    BYTE *mainMenuStub = StubCursor();
    EmitByte(0xC6); EmitByte(0x05); EmitDword(g_save->inputBlocked); EmitByte(0x00);
    EmitJmp(g_save->mainMenu);

    if (!WriteCall(g_save->menuCall, stub))
    {
        g_save = NULL;
        return;
    }
    WriteCall(g_save->saveGameCall, saveStub);
    WriteCall(g_save->loadMenuCall, loadMenuStub);
    WriteCall(g_save->menuKeyCall, menuKeyStub);
    WriteCall(g_save->mainMenuCall, mainMenuStub);
    SpeedLog("  quick save ready at %X, F5 and F9 use %s and %s, F9 also works on the main menu",
             g_save->menuCall, kQuickSaveFiles[0], kQuickSaveFiles[1]);
}

void InstallQuickSave()
{
    InstallSaveTweaks();
    InstallSaveHook();
}
