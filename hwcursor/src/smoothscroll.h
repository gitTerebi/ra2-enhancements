#pragma once
#include <windows.h>
#include <stdlib.h>
#include "scrollspeed.h"
#include "presentclock.h"

namespace SmoothScroll
{
    typedef void(__fastcall *SetTacticalPos_t)(void *self, void *unused, int *pos);

    static const BYTE kSetPosSig[5] = {0x83, 0xEC, 0x08, 0x56, 0x57};
    static const int kPosX = 0xD64;
    static const int kPosY = 0xD68;

    static double g_freq;
    static LONGLONG g_last;
    static double g_ax, g_ay;
    static double g_dt;
    static bool g_moved;

    inline bool Available(DWORD setPos)
    {
        static DWORD s_checked;
        static bool s_ok;
        if (s_checked != setPos)
        {
            s_checked = setPos;
            s_ok = setPos && GetModuleHandleA(NULL) == (HMODULE)0x00400000 &&
                   !IsBadReadPtr((void *)setPos, sizeof(kSetPosSig)) &&
                   memcmp((const void *)setPos, kSetPosSig, sizeof(kSetPosSig)) == 0;
        }
        return s_ok;
    }

    inline bool Tick(DWORD instance, DWORD setPos)
    {
        g_moved = false;
        if (!Available(setPos))
            return false;

        void *tac = *(void **)instance;
        if (!tac)
            return false;

        LONGLONG target = PresentClock::CameraTarget(PresentClock::Now());
        if (g_freq == 0.0)
        {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            g_freq = (double)f.QuadPart;
            g_last = target;
            return true;
        }
        double dt = (target - g_last) / g_freq;
        g_last = target;
        if (dt > 0.05)
            dt = 0.05;

        // Raw dt, matching Vinifera scrollext_hooks.cpp:223 and OpenTS scroll.cpp:546
        g_dt = dt;

        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);

        int dx = 0, dy = 0;
        if (pid == GetCurrentProcessId() &&
            !(GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
            !(GetAsyncKeyState(VK_MENU) & 0x8000))
        {
            if (GetAsyncKeyState('A') & 0x8000) --dx;
            if (GetAsyncKeyState('D') & 0x8000) ++dx;
            if (GetAsyncKeyState('W') & 0x8000) --dy;
            if (GetAsyncKeyState('S') & 0x8000) ++dy;
        }

        if (!dx && !dy)
        {
            g_ax = 0.0;
            g_ay = 0.0;
            return true;
        }

        double step = ScrollSpeed::PixelsPerSecond() * g_dt;
        if (dx && dy)
            step *= 0.70710678;

        g_ax += dx * step;
        g_ay += dy * step;

        int mx = (int)g_ax;
        int my = (int)g_ay;
        if (!mx && !my)
            return true;

        g_ax -= mx;
        g_ay -= my;

        int pos[2];
        pos[0] = *(int *)((BYTE *)tac + kPosX) + mx;
        pos[1] = *(int *)((BYTE *)tac + kPosY) + my;
        ((SetTacticalPos_t)setPos)(tac, NULL, pos);
        g_moved = true;
        return true;
    }
}
