#pragma once
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "gameprofile.h"
#include "patchguard.h"

bool BytesAt(DWORD at, const BYTE *sig, size_t n);
bool CallGoesTo(DWORD at, DWORD target);

bool ReserveStubs();
BYTE *StubCursor();
void Emit(const void *bytes, size_t n);
void EmitByte(BYTE b);
void EmitDword(DWORD d);
void EmitJmp(DWORD target);
void EmitCallAbs(const void *fn);
