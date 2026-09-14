#pragma once
#include "patchguard.h"

struct GameProfile
{
    const char *name;
    const char *ini;
    const char *syringeIni;
    DWORD earlyOut;
    const BYTE *detectSig;
    int detectLen;
};

const GameProfile *DetectGame();
const char *ProfileIni(const GameProfile *g);
