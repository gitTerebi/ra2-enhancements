#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>

namespace DrawProfiler
{
    static bool g_enabled;
    static volatile LONG g_started;
    static HANDLE g_target;
    static DWORD g_stackBase;
    static const bool *g_inScroll;
    static FILE *g_out;
    static char g_dir[MAX_PATH];

    inline bool IniEnabled()
    {
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(NULL, path, sizeof(path));
        char *slash = n ? strrchr(path, '\\') : NULL;
        if (!slash)
            return false;
        slash[1] = 0;
        strcpy(g_dir, path);
        char ini[MAX_PATH];
        _snprintf(ini, sizeof(ini) - 1, "%sddraw.ini", g_dir);
        ini[sizeof(ini) - 1] = 0;
        char value[16];
        GetPrivateProfileStringA("ddraw", "drawprofile", "", value, sizeof(value), ini);
        return _stricmp(value, "true") == 0 || strcmp(value, "1") == 0;
    }

    inline void WriteModules()
    {
        char path[MAX_PATH];
        _snprintf(path, sizeof(path) - 1, "%sdrawprofile-modules.csv", g_dir);
        path[sizeof(path) - 1] = 0;
        FILE *f = fopen(path, "w");
        if (!f)
            return;
        fprintf(f, "base,size,name\n");
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
        if (snap != INVALID_HANDLE_VALUE)
        {
            MODULEENTRY32 me;
            me.dwSize = sizeof(me);
            for (BOOL ok = Module32First(snap, &me); ok; ok = Module32Next(snap, &me))
                fprintf(f, "%08X,%08X,%s\n", (DWORD)(DWORD_PTR)me.modBaseAddr, me.modBaseSize, me.szModule);
            CloseHandle(snap);
        }
        fclose(f);
    }

    inline bool FollowsCall(DWORD ret)
    {
        if (ret < 0x10000 || ret >= 0x7FFE0000)
            return false;
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((void *)(DWORD_PTR)(ret - 7), &mbi, sizeof(mbi)))
            return false;
        if (mbi.State != MEM_COMMIT || !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            return false;
        if ((DWORD)(DWORD_PTR)mbi.BaseAddress + mbi.RegionSize < ret)
            return false;
        const BYTE *p = (const BYTE *)(DWORD_PTR)ret;
        if (p[-5] == 0xE8)
            return true;
        for (int len = 2; len <= 7; ++len)
            if (p[-len] == 0xFF && ((p[-len + 1] >> 3) & 7) == 2)
                return true;
        return false;
    }

    static DWORD WINAPI SamplerThread(LPVOID)
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        HANDLE timer = CreateWaitableTimerExW(NULL, NULL, 0x00000002, TIMER_ALL_ACCESS);
        if (!timer)
            timer = CreateWaitableTimerW(NULL, FALSE, NULL);
        LARGE_INTEGER due;
        due.QuadPart = -10000;
        DWORD stack[1024];
        int samples = 0;
        WriteModules();
        for (;;)
        {
            SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE);
            WaitForSingleObject(timer, INFINITE);

            LARGE_INTEGER qpc;
            QueryPerformanceCounter(&qpc);
            if (SuspendThread(g_target) == (DWORD)-1)
                return 0;
            CONTEXT ctx;
            ctx.ContextFlags = CONTEXT_CONTROL;
            BOOL got = GetThreadContext(g_target, &ctx);
            int scroll = *g_inScroll ? 1 : 0;
            int words = 0;
            if (got && ctx.Esp < g_stackBase)
            {
                DWORD avail = (g_stackBase - ctx.Esp) / 4;
                words = avail < 1024 ? (int)avail : 1024;
                memcpy(stack, (void *)(DWORD_PTR)ctx.Esp, words * 4);
            }
            ResumeThread(g_target);
            if (!got)
                continue;

            fprintf(g_out, "%lld,%d,%08X,", qpc.QuadPart, scroll, ctx.Eip);
            int found = 0;
            for (int i = 0; i < words && found < 40; ++i)
                if (FollowsCall(stack[i]))
                {
                    fprintf(g_out, found ? ";%08X" : "%08X", stack[i]);
                    ++found;
                }
            fputc('\n', g_out);
            if (++samples % 5000 == 0)
            {
                fflush(g_out);
                WriteModules();
            }
        }
    }

    inline void StartOnThisThread(const bool *inScroll)
    {
        if (InterlockedCompareExchange(&g_started, 1, 0) != 0)
            return;
        if (!IniEnabled())
            return;
        NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
        g_stackBase = (DWORD)(DWORD_PTR)tib->StackBase;
        g_inScroll = inScroll;
        if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_target,
                             THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, 0))
            return;
        char path[MAX_PATH];
        _snprintf(path, sizeof(path) - 1, "%sdrawprofile.csv", g_dir);
        path[sizeof(path) - 1] = 0;
        g_out = fopen(path, "w");
        if (!g_out)
            return;
        setvbuf(g_out, NULL, _IOFBF, 1 << 20);
        fprintf(g_out, "qpc,scroll,eip,returns\n");
        g_enabled = true;
        CloseHandle(CreateThread(NULL, 0, SamplerThread, NULL, 0, NULL));
    }
}
