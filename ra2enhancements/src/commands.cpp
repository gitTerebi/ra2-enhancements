#include "patch.h"
#include "features.h"

enum { CMD_SPEED_DOWN, CMD_SPEED_UP, CMD_QUICK_SAVE, CMD_QUICK_LOAD };

static const wchar_t *g_commandCategory = L"Interface";
static bool g_commandsReady;

class ModCommand
{
public:
    ModCommand(int action, const char *name, const wchar_t *uiName, const wchar_t *description)
        : m_action(action), m_name(name), m_uiName(uiName), m_description(description) {}
    virtual ~ModCommand() {}
    virtual const char *GetName() const { return m_name; }
    virtual const wchar_t *GetUIName() const { return m_uiName; }
    virtual const wchar_t *GetUICategory() const { return g_commandCategory; }
    virtual const wchar_t *GetUIDescription() const { return m_description; }
    virtual bool PreventCombinationOverride(int) const { return false; }
    virtual bool ExtraTriggerCondition(int key) const { return !(key & WWKEY_RELEASE); }
    virtual bool CheckLoop(int) const { return false; }
    virtual void Execute(int) const
    {
        switch (m_action)
        {
        case CMD_SPEED_DOWN:
        case CMD_SPEED_UP:
            if (SinglePlayerMode())
                StepSpeed(m_action == CMD_SPEED_UP ? 1 : -1);
            break;
        case CMD_QUICK_SAVE:
            QueueQuickSave();
            break;
        case CMD_QUICK_LOAD:
            QueueQuickLoad();
            break;
        }
    }

private:
    int m_action;
    const char *m_name;
    const wchar_t *m_uiName;
    const wchar_t *m_description;
};

struct CommandSpec
{
    int action;
    const char *name;
    const wchar_t *uiName;
    const wchar_t *description;
    int defaultKey;
};

static const CommandSpec kCommandSpecs[] = {
    {CMD_QUICK_SAVE, "QuickSave", L"Quick Save", L"Save over the older of the two quick saves.", VK_F5},
    {CMD_QUICK_LOAD, "QuickLoad", L"Quick Load", L"Load the newer of the two quick saves.", VK_F9},
    {CMD_SPEED_DOWN, "GameSpeedDown", L"Game Speed Down", L"Slow the game down one step.", VK_OEM_4},
    {CMD_SPEED_UP, "GameSpeedUp", L"Game Speed Up", L"Speed the game up one step.", VK_OEM_6},
};

static ModCommand *g_commands[_countof(kCommandSpecs)];

struct CommandVector
{
    void **vtable;
    void **items;
    int capacity;
    bool isReference;
    bool isAllocated;
    int count;
    int capacityIncrement;
};

typedef bool (__thiscall *SetCapacityFn)(CommandVector *self, int capacity, void **existing);
typedef const char *(__thiscall *CommandNameFn)(void *cmd);
typedef const wchar_t *(__thiscall *CommandCategoryFn)(void *cmd);

struct CommandProfile
{
    const char *name;
    DWORD keyboardLoad;
    CommandVector *commands;
    const char *keyboardIni;
};

static const CommandProfile kCommands[] = {
    {"gamemd", 0x00533D20, (CommandVector *)0x0087F658, "KeyboardMD.ini"},
    {"game", 0x00519240, (CommandVector *)0x00832358, "Keyboard.ini"},
};

static const CommandProfile *g_commandProfile;

bool CommandsReady()
{
    return g_commandsReady;
}

const char *KeyboardIni()
{
    return g_commandProfile ? g_commandProfile->keyboardIni : NULL;
}

static bool HasCommand(const CommandVector *v, const void *cmd)
{
    for (int i = 0; cmd && i < v->count; ++i)
        if (v->items[i] == cmd)
            return true;
    return false;
}

static bool AddCommand(CommandVector *v, void *cmd)
{
    if (v->count >= v->capacity)
    {
        if ((!v->isAllocated && v->capacity) || v->capacityIncrement <= 0)
            return false;
        SetCapacityFn grow = (SetCapacityFn)v->vtable[2];
        if (!grow(v, v->capacity + v->capacityIncrement, NULL))
            return false;
    }
    v->items[v->count++] = cmd;
    return true;
}

static void SeedDefaultKeys()
{
    char ini[MAX_PATH];
    if (!BesideExe(g_commandProfile->keyboardIni, ini, sizeof(ini)) ||
        GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES)
        return;

    char section[8192];
    DWORD len = GetPrivateProfileSectionA("Hotkey", section, sizeof(section), ini);
    if (len == 0 || len >= sizeof(section) - 2)
        return;

    for (int c = 0; c < _countof(kCommandSpecs); ++c)
    {
        const CommandSpec &spec = kCommandSpecs[c];
        const char *name = spec.name;
        if (GetPrivateProfileIntA("Hotkey", name, -1, ini) != -1)
            continue;
        for (const char *entry = section; *entry; entry += strlen(entry) + 1)
        {
            const char *eq = strchr(entry, '=');
            if (!eq || atoi(eq + 1) != spec.defaultKey)
                continue;
            char other[128];
            size_t n = eq - entry;
            if (n >= sizeof(other))
                continue;
            memcpy(other, entry, n);
            other[n] = 0;
            WritePrivateProfileStringA("Hotkey", other, "0", ini);
            SpeedLog("  unbound %s from key %d for %s", other, spec.defaultKey, name);
        }
        char value[16];
        wsprintfA(value, "%d", spec.defaultKey);
        WritePrivateProfileStringA("Hotkey", name, value, ini);
    }
}

static void __cdecl RegisterCommands()
{
    CommandVector *v = g_commandProfile->commands;
    for (int i = 0; i < v->count; ++i)
    {
        void *cmd = v->items[i];
        if (cmd && strcmp((*(CommandNameFn **)cmd)[1](cmd), "Options") == 0)
            g_commandCategory = (*(CommandCategoryFn **)cmd)[3](cmd);
    }
    int ready = 0;
    for (int c = 0; c < _countof(kCommandSpecs); ++c)
    {
        if (HasCommand(v, g_commands[c]))
        {
            ++ready;
            continue;
        }
        const CommandSpec &spec = kCommandSpecs[c];
        ModCommand *cmd = new ModCommand(spec.action, spec.name, spec.uiName, spec.description);
        if (AddCommand(v, cmd))
        {
            g_commands[c] = cmd;
            ++ready;
        }
        else
        {
            delete cmd;
        }
    }
    if (!g_commandsReady)
        SeedDefaultKeys();
    g_commandsReady = ready == _countof(kCommandSpecs);
    SpeedLog("  commands registered %d of %d", ready, _countof(kCommandSpecs));
}

void InstallCommands()
{
    const GameProfile *g = DetectGame();
    if (!g)
        return;
    const CommandProfile *p = NULL;
    for (int i = 0; i < _countof(kCommands); ++i)
        if (strcmp(kCommands[i].name, g->name) == 0)
            p = &kCommands[i];
    if (!p)
        return;

    BYTE want[6] = {0x81, 0xEC};
    *(DWORD *)(want + 2) = strcmp(p->name, "gamemd") == 0 ? 0xCC : 0xBC;
    if (!BytesAt(p->keyboardLoad, want, sizeof(want)))
    {
        SpeedLog("  commands: keyboard load %X unexpected, skipped", p->keyboardLoad);
        return;
    }
    if (!ReserveStubs())
        return;
    g_commandProfile = p;

    BYTE *stub = StubCursor();
    EmitByte(0x60);
    EmitByte(0x9C);
    EmitCallAbs(RegisterCommands);
    EmitByte(0x9D);
    EmitByte(0x61);
    Emit(want, sizeof(want));
    EmitJmp(p->keyboardLoad + sizeof(want));
    if (WriteJmp(p->keyboardLoad, stub, sizeof(want)))
        SpeedLog("  commands hook ready at %X", p->keyboardLoad);
}
