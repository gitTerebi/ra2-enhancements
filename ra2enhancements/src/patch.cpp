#include "patch.h"

static BYTE *g_stubs;
static BYTE *g_stubEnd;

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
