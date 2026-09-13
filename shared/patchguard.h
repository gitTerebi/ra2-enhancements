#pragma once
#include <windows.h>

void SpeedLog(const char *fmt, ...);
bool BesideExe(const char *name, char *out, size_t outSize);

bool WriteBytes(DWORD at, const BYTE *bytes, size_t n);
bool WriteCall(DWORD at, const void *target, size_t len = 5);
bool WriteJmp(DWORD at, const void *target, size_t len = 5);
bool InstallPatch(const char *name, void (*install)());
