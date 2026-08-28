#pragma once
#include <windows.h>
#include <stdio.h>

#pragma comment(linker, "/export:DirectSoundCreate=_DsFn0,@1")
#pragma comment(linker, "/export:DirectSoundEnumerateA=_DsFn1,@2")
#pragma comment(linker, "/export:DirectSoundEnumerateW=_DsFn2,@3")
#pragma comment(linker, "/export:DllCanUnloadNow=_DsFn3,PRIVATE")
#pragma comment(linker, "/export:DllGetClassObject=_DsFn4,PRIVATE")
#pragma comment(linker, "/export:DirectSoundCaptureCreate=_DsFn5,@6")
#pragma comment(linker, "/export:DirectSoundCaptureEnumerateA=_DsFn6,@7")
#pragma comment(linker, "/export:DirectSoundCaptureEnumerateW=_DsFn7,@8")
#pragma comment(linker, "/export:GetDeviceID=_DsFn8,@9")
#pragma comment(linker, "/export:DirectSoundFullDuplexCreate=_DsFn9,@10")
#pragma comment(linker, "/export:DirectSoundCreate8=_DsFn10,@11")
#pragma comment(linker, "/export:DirectSoundCaptureCreate8=_DsFn11,@12")

namespace DSoundForward
{
    static const char *kNames[12] = {
        "DirectSoundCreate", "DirectSoundEnumerateA", "DirectSoundEnumerateW",
        "DllCanUnloadNow", "DllGetClassObject", "DirectSoundCaptureCreate",
        "DirectSoundCaptureEnumerateA", "DirectSoundCaptureEnumerateW", "GetDeviceID",
        "DirectSoundFullDuplexCreate", "DirectSoundCreate8", "DirectSoundCaptureCreate8"};
}

static FARPROC g_dsFn[12];

static void DSoundLoadReal()
{
    char path[MAX_PATH];
    UINT n = GetSystemDirectoryA(path, MAX_PATH);
    if (!n || n > MAX_PATH - 16)
        return;

    _snprintf(path + n, MAX_PATH - n, "\\dsound.dll");

    HMODULE real = LoadLibraryA(path);
    if (!real)
        return;

    for (int i = 0; i < 12; ++i)
        g_dsFn[i] = GetProcAddress(real, DSoundForward::kNames[i]);
}

#define DSOUND_STUB(slot)                            \
    extern "C" __declspec(naked) void DsFn##slot()   \
    {                                                \
        __asm { jmp dword ptr [g_dsFn + slot * 4] }  \
    }

DSOUND_STUB(0)
DSOUND_STUB(1)
DSOUND_STUB(2)
DSOUND_STUB(3)
DSOUND_STUB(4)
DSOUND_STUB(5)
DSOUND_STUB(6)
DSOUND_STUB(7)
DSOUND_STUB(8)
DSOUND_STUB(9)
DSOUND_STUB(10)
DSOUND_STUB(11)
