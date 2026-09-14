#include <stdlib.h>
#include "gameprofile.h"

static const BYTE kDetectYR[6] = {0x83, 0xC4, 0x48, 0xC2, 0x0C, 0x00};
static const BYTE kDetectRA2[7] = {0x5B, 0x83, 0xC4, 0x48, 0xC2, 0x0C, 0x00};

// Mental Omega 3.3.6 ships the stock gamemd.exe but reads RA2MO.ini, always via Syringe
static const GameProfile kProfiles[] = {
    {"gamemd", "RA2MD.ini", "RA2MO.ini", 0x007B9528, kDetectYR, sizeof(kDetectYR)},
    {"game", "RA2.ini", "RA2.ini", 0x00772893, kDetectRA2, sizeof(kDetectRA2)},
};

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
        if (BytesAt(kProfiles[i].earlyOut, kProfiles[i].detectSig, kProfiles[i].detectLen))
            return s_game = &kProfiles[i];
    return NULL;
}

const char *ProfileIni(const GameProfile *g)
{
    return IsDebuggerPresent() ? g->syringeIni : g->ini;
}
