#pragma once
#include <windows.h>
#include <string.h>

struct REGISTERS
{
    DWORD origin;
    DWORD flags;
    DWORD _EDI, _ESI, _EBP, _ESP, _EBX, _EDX, _ECX, _EAX;
};

#define EXPORT_FUNC(name) extern "C" __declspec(dllexport) DWORD __cdecl name(REGISTERS *R)

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
    BYTE **mouseInstance;
    GetFramePixels_t getPixels;
    bool thisInEbx;
    DWORD tacticalInstance;
    DWORD setTacticalPos;
    DWORD scrollHook;
    DWORD mapInstance;
};

static const BYTE kSigYR[5] = {0x8B, 0x48, 0x04, 0x85, 0xC9};
static const BYTE kSigRA2[5] = {0x8B, 0x4B, 0x04, 0x85, 0xC9};
static const BYTE kDetectYR[6] = {0x83, 0xC4, 0x48, 0xC2, 0x0C, 0x00};
static const BYTE kDetectRA2[7] = {0x5B, 0x83, 0xC4, 0x48, 0xC2, 0x0C, 0x00};

static const GameProfile kProfiles[] = {
    {"gamemd", 0x007B93F9, kSigYR, 0x007B9528, kDetectYR, sizeof(kDetectYR),
     (ConvertClass **)0x0087F6C8, (BYTE **)0x00B78164, (GetFramePixels_t)0x0069E740, false,
     0x00887324, 0x006D6000, 0x004F4320, 0x0087F7E8},
    {"game", 0x00772746, kSigRA2, 0x00772893, kDetectRA2, sizeof(kDetectRA2),
     (ConvertClass **)0x008323C8, (BYTE **)0x00B2AF5C, NULL, true,
     0x0083996C, 0x006A6640, 0x004E09D0, 0x008324E0},
};

// Syringe.exe 0.7.3.0 also images at 0x00400000 and LoadLibrary/FreeLibrary's our DLLs to resolve .inj exports
inline bool HostIsGame()
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, sizeof(path));
    if (!n || n >= sizeof(path))
        return false;
    const char *exe = strrchr(path, '\\');
    exe = exe ? exe + 1 : path;
    return _stricmp(exe, "gamemd.exe") == 0 || _stricmp(exe, "game.exe") == 0;
}

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
    {
        const GameProfile &p = kProfiles[i];
        if (IsBadReadPtr((const void *)p.earlyOut, p.detectLen))
            continue;
        if (memcmp((const void *)p.earlyOut, p.detectSig, p.detectLen) == 0)
            return s_game = &p;
    }
    return NULL;
}
