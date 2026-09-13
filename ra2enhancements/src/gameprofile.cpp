#include <stdlib.h>
#include <string.h>
#include "gameprofile.h"

static const BYTE kDetectYR[6] = {0x83, 0xC4, 0x48, 0xC2, 0x0C, 0x00};
static const BYTE kDetectRA2[7] = {0x5B, 0x83, 0xC4, 0x48, 0xC2, 0x0C, 0x00};

// Mental Omega 3.3.6 ships the stock gamemd.exe but reads RA2MO.ini, always via Syringe
static const GameProfile kProfiles[] = {
    {"gamemd", "RA2MD.ini", "RA2MO.ini", 0x007B9528, kDetectYR, sizeof(kDetectYR)},
    {"game", "RA2.ini", "RA2.ini", 0x00772893, kDetectRA2, sizeof(kDetectRA2)},
};

// Syringe.exe 0.7.3.0 also images at 0x00400000 and LoadLibrary/FreeLibrary's our DLLs to resolve .inj exports
bool HostIsGame()
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, sizeof(path));
    if (!n || n >= sizeof(path))
        return false;
    const char *exe = strrchr(path, '\\');
    exe = exe ? exe + 1 : path;
    return _stricmp(exe, "gamemd.exe") == 0 || _stricmp(exe, "game.exe") == 0;
}

const GameProfile *DetectGame()
{
    static bool s_done;
    static const GameProfile *s_game;
    if (s_done)
        return s_game;
    s_done = true;
    if (!HostIsGame() || GetModuleHandleA(NULL) != (HMODULE)0x00400000)
        return NULL;
    for (int i = 0; i < _countof(kProfiles); ++i)
    {
        const GameProfile &p = kProfiles[i];
        if (IsBadReadPtr((const void *)p.earlyOut, p.detectLen))
            continue;
        if (memcmp((const void *)p.earlyOut, p.detectSig, p.detectLen) == 0)
            return s_game = &p;
    }
    return NULL;
}

const char *ProfileIni(const GameProfile *g)
{
    return IsDebuggerPresent() ? g->syringeIni : g->ini;
}
