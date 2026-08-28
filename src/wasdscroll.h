#pragma once
#include <windows.h>

namespace WasdScroll
{
    struct Binding { int from, to; };

    static const Binding kBindings[] = {
        {'W', VK_UP},
        {'A', VK_LEFT},
        {'S', VK_DOWN},
        {'D', VK_RIGHT},
    };

    static bool g_held[4];
    static double g_freq;
    static LONGLONG g_last;

    inline void Send(int vk, bool down)
    {
        keybd_event((BYTE)vk, (BYTE)MapVirtualKeyA(vk, MAPVK_VK_TO_VSC),
                    (down ? 0 : KEYEVENTF_KEYUP) | KEYEVENTF_EXTENDEDKEY, 0);
    }

    inline void ReleaseAll()
    {
        for (int i = 0; i < 4; ++i)
        {
            if (g_held[i])
            {
                g_held[i] = false;
                Send(kBindings[i].to, false);
            }
        }
    }

    inline void Tick()
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (g_freq == 0.0)
        {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            g_freq = (double)f.QuadPart;
            g_last = now.QuadPart;
            return;
        }
        if ((now.QuadPart - g_last) / g_freq < 0.002)
            return;
        g_last = now.QuadPart;

        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        if (pid != GetCurrentProcessId())
        {
            ReleaseAll();
            return;
        }

        if (GetAsyncKeyState(VK_CONTROL) & 0x8000 ||
            GetAsyncKeyState(VK_MENU) & 0x8000)
        {
            ReleaseAll();
            return;
        }

        for (int i = 0; i < 4; ++i)
        {
            bool down = (GetAsyncKeyState(kBindings[i].from) & 0x8000) != 0;
            if (down != g_held[i])
            {
                g_held[i] = down;
                Send(kBindings[i].to, down);
            }
        }
    }
}
