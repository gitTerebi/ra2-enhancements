#include "patch.h"

static BYTE *g_stubs;
static BYTE *g_stubEnd;

bool BytesAt(DWORD at, const BYTE *sig, size_t n)
{
    return !IsBadReadPtr((const void *)at, n) && memcmp((const void *)at, sig, n) == 0;
}

bool CallGoesTo(DWORD at, DWORD target)
{
    const BYTE *p = (const BYTE *)at;
    if (IsBadReadPtr(p, 5) || p[0] != 0xE8)
        return false;
    return (DWORD)(at + 5 + *(const int *)(p + 1)) == target;
}

bool ReserveStubs()
{
    if (!g_stubs)
    {
        g_stubs = (BYTE *)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        g_stubEnd = g_stubs;
    }
    return g_stubs != NULL;
}

BYTE *StubCursor()
{
    return g_stubEnd;
}

void Emit(const void *bytes, size_t n)
{
    memcpy(g_stubEnd, bytes, n);
    g_stubEnd += n;
}

void EmitByte(BYTE b)
{
    Emit(&b, 1);
}

void EmitDword(DWORD d)
{
    Emit(&d, 4);
}

void EmitJmp(DWORD target)
{
    EmitByte(0xE9);
    EmitDword(target - ((DWORD)g_stubEnd + 4));
}

void EmitCallAbs(const void *fn)
{
    EmitByte(0xE8);
    EmitDword((DWORD)fn - ((DWORD)g_stubEnd + 4));
}
