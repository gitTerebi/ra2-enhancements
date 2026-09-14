#pragma once
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "gameprofile.h"
#include "patchguard.h"

bool ReserveStubs();
BYTE *StubCursor();
void Emit(const void *bytes, size_t n);
void EmitByte(BYTE b);
void EmitDword(DWORD d);
void EmitJmp(DWORD target);
void EmitCallAbs(const void *fn);

template <class T, size_t N>
const T *ForThisGame(const T (&rows)[N])
{
    const GameProfile *g = DetectGame();
    for (size_t i = 0; g && i < N; ++i)
        if (strcmp(rows[i].name, g->name) == 0)
            return &rows[i];
    return NULL;
}
