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

// Syringe.exe 0.7.3.0 also images at 0x00400000
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

void SpeedLog(const char *fmt, ...);
bool BesideExe(const char *name, char *out, size_t outSize);
bool DdrawIniFlag(const char *key);

bool BytesAt(DWORD at, const BYTE *sig, size_t n);
bool OtherModJumpAt(DWORD at, const BYTE *sig, size_t n);
bool CallGoesTo(DWORD at, DWORD target);

bool WriteBytes(DWORD at, const BYTE *bytes, size_t n);
bool WriteCall(DWORD at, const void *target, size_t len = 5);
bool WriteJmp(DWORD at, const void *target, size_t len = 5);
bool InstallPatch(const char *name, void (*install)());
