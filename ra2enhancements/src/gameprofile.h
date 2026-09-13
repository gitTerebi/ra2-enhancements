#pragma once
#include <windows.h>

struct REGISTERS
{
    DWORD origin;
    DWORD flags;
    DWORD _EDI, _ESI, _EBP, _ESP, _EBX, _EDX, _ECX, _EAX;
};

#define EXPORT_FUNC(name) extern "C" __declspec(dllexport) DWORD __cdecl name(REGISTERS *R)

struct GameProfile
{
    const char *name;
    const char *ini;
    const char *syringeIni;
    DWORD earlyOut;
    const BYTE *detectSig;
    int detectLen;
};

bool HostIsGame();
const GameProfile *DetectGame();
const char *ProfileIni(const GameProfile *g);
