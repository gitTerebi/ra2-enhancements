#pragma once
#include <windows.h>
#include <stdlib.h>
#include <math.h>
#include "scrollspeed.h"
#include "presentclock.h"

namespace RightScroll
{
    struct Profile
    {
        DWORD update, caller, scrollMap, horizontal, vertical;
        DWORD mouse, viewOrigin, method, captureOffset;
    };
    static const Profile kRA2 = {
        0x66A7A0, 0x66A332, 0x499B60, 0x66AB56, 0x66AB7A,
        0x839C68, 0x839630, 0xA40B20, 0x4F2A
    };
    static const Profile kYR = {
        0x693440, 0x692FD2, 0x4A9840, 0x6937F6, 0x69381A,
        0x887640, 0x886FA0, 0xA8EB6C, 0x555A
    };
    static const double kGain = 2.00;
    static const Profile *g_profile;
    static LONGLONG g_last;
    static double g_frequency, g_scale;
    static double g_fraction[2];
    static double g_axis[2];

    typedef void (__thiscall *UpdateFn)(void *, int *);
    typedef bool (__thiscall *ScrollFn)(void *, int, int *, bool);

    static bool __fastcall Move(void *map, void *, int direction, int *distance, bool move)
    {
        // Method 0 is continuous scrolling. Methods 1/2 warp the pointer back
        // to its anchor and already consume a distance, not a velocity.
        const int axis = (direction == 2 || direction == 6) ? 0 : 1;
        const int sign = (direction == 0 || direction == 6) ? -1 : 1;
        double &fraction = g_fraction[axis];
        if (*(int *)g_profile->method == 0)
            fraction += sign * g_axis[axis] * ScrollSpeed::PixelsPerSecond() * g_scale * kGain;
        else
            // Distance-based drag modes retain pointer-relative movement, with
            // sensitivity driven by the same menu curve. Undo the native
            // 12/(rate+1) gain before applying the shared medium-relative gain.
            fraction += sign * *distance * (ScrollSpeed::Rate() + 1) / 12.0 *
                        ScrollSpeed::PixelsPerSecond() / ScrollSpeed::kMediumSpeed * kGain;
        int pixels = (int)fraction;
        fraction -= pixels;
        if (!pixels)
            return true;
        int amount = abs(pixels);
        int actualDirection = axis == 0 ? (pixels < 0 ? 6 : 2) : (pixels < 0 ? 0 : 4);
        return ((ScrollFn)g_profile->scrollMap)(map, actualDirection, &amount, move);
    }

    static void __fastcall Update(void *map, void *, int *point)
    {
        LONGLONG target = PresentClock::Target(PresentClock::Now());
        if (!*(BYTE *)((BYTE *)map + g_profile->captureOffset - 2))
        {
            g_last = 0;
            g_fraction[0] = g_fraction[1] = 0;
        }
        double dt = g_last ? (target - g_last) / g_frequency : 0.0;
        g_last = target;
        if (dt > 0.05) dt = 0.05;
        g_scale = dt;
        int *anchor = (int *)((BYTE *)map + g_profile->captureOffset - 10);
        double dx = (double)point[0] - anchor[0];
        double dy = (double)point[1] - anchor[1];
        double length = sqrt(dx * dx + dy * dy);
        g_axis[0] = length ? fabs(dx) / length : 0;
        g_axis[1] = length ? fabs(dy) / length : 0;
        ((UpdateFn)g_profile->update)(map, point);
    }

    inline bool Tick(DWORD mapAddress, DWORD tacticalInstance)
    {
        if (!g_profile)
            return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        if (pid != GetCurrentProcessId() ||
            !(GetAsyncKeyState(VK_RBUTTON) & 0x8000) ||
            (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ||
            *(BYTE *)(mapAddress + g_profile->captureOffset) != 1)
        {
            g_last = 0;
            g_fraction[0] = g_fraction[1] = 0;
            return false;
        }
        void *mouse = *(void **)g_profile->mouse;
        BYTE *tactical = *(BYTE **)tacticalInstance;
        if (!mouse || !tactical)
            return false;
        typedef int *(__thiscall *GetPointFn)(void *, int *);
        int point[2];
        int *current = (*(GetPointFn **)mouse)[0x34 / 4](mouse, point);
        point[0] = current[0] - *(int *)g_profile->viewOrigin;
        point[1] = current[1] - *(int *)(g_profile->viewOrigin + 4);
        // Native Scroll_Map writes the pending camera position (D74/D78).
        // Tactical's render update applies it to D64/D68 afterward.
        int x = *(int *)(tactical + 0xD74), y = *(int *)(tactical + 0xD78);
        Update((void *)mapAddress, NULL, point);
        return x != *(int *)(tactical + 0xD74) || y != *(int *)(tactical + 0xD78);
    }

    inline bool Install(bool yr)
    {
        const Profile &p = yr ? kYR : kRA2;
        const DWORD sites[] = {p.caller, p.horizontal, p.vertical};
        const DWORD originals[] = {p.update, p.scrollMap, p.scrollMap};
        const DWORD replacements[] = {(DWORD)Update, (DWORD)Move, (DWORD)Move};
        for (int i = 0; i < 3; ++i)
        {
            const BYTE *site = (const BYTE *)sites[i];
            if (IsBadReadPtr(site, 5) || site[0] != 0xE8 ||
                sites[i] + 5 + *(const DWORD *)(site + 1) != originals[i])
                return false;
        }
        DWORD old;
        SIZE_T size = p.vertical + 5 - p.caller;
        if (!VirtualProtect((void *)p.caller, size, PAGE_EXECUTE_READWRITE, &old))
            return false;
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        g_frequency = (double)frequency.QuadPart;
        g_profile = &p;
        for (int i = 0; i < 3; ++i)
            *(DWORD *)(sites[i] + 1) = replacements[i] - (sites[i] + 5);
        DWORD ignored;
        VirtualProtect((void *)p.caller, size, old, &ignored);
        FlushInstructionCache(GetCurrentProcess(), (void *)p.caller, size);
        return true;
    }
}
