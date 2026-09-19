#pragma once
#include "patchguard.h"

typedef void *(__thiscall *GetFramePixels_t)(void *shape, int frame);

struct ConvertClass
{
    void *vtbl;
    int BytesPerPixel;
    void *Blitters[50];
    void *RLEBlitters[39];
    DWORD ShadeCount;
    void *BufferA;
    void *Midpoint;
    void *BufferB;
    DWORD CurrentZRemap;
    DWORD HalfColorMask;
    DWORD QuarterColorMask;
};

struct GameProfile
{
    const char *name;
    DWORD hook;
    const BYTE *signature;
    DWORD earlyOut;
    const BYTE *detectSig;
    int detectLen;
    ConvertClass **mousePal;
    GetFramePixels_t getPixels;
    bool thisInEbx;
    DWORD tacticalInstance;
    DWORD setTacticalPos;
    DWORD scrollHook;
    DWORD mapInstance;
    DWORD showEnd;
    DWORD hideEnd;
    DWORD captureHide;
    DWORD showCursorImport;
};

static const BYTE kSigYR[5] = {0x8B, 0x48, 0x04, 0x85, 0xC9};
static const BYTE kSigRA2[5] = {0x8B, 0x4B, 0x04, 0x85, 0xC9};
static const BYTE kDetectYR[6] = {0x83, 0xC4, 0x48, 0xC2, 0x0C, 0x00};
static const BYTE kDetectRA2[7] = {0x5B, 0x83, 0xC4, 0x48, 0xC2, 0x0C, 0x00};

static const GameProfile kProfiles[] = {
    {"gamemd", 0x007B93F9, kSigYR, 0x007B9528, kDetectYR, sizeof(kDetectYR),
     (ConvertClass **)0x0087F6C8, (GetFramePixels_t)0x0069E740, false,
     0x00887324, 0x006D6000, 0x004F4320, 0x0087F7E8,
     0x007B98EC, 0x007B9A23, 0x007B9C0B, 0x007E13DC},
    {"game", 0x00772746, kSigRA2, 0x00772893, kDetectRA2, sizeof(kDetectRA2),
     (ConvertClass **)0x008323C8, NULL, true,
     0x0083996C, 0x006A6640, 0x004E09D0, 0x008324E0,
     0x00772C5C, 0x00772D93, 0x00772F7B, 0x0079A3CC},
};

inline const GameProfile *DetectGame()
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
