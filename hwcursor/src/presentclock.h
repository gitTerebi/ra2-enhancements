#pragma once
#include <windows.h>
#include <dwmapi.h>

namespace PresentClock
{
    static const double kUploadPhaseAfterVBlank = 0.30;

    static LONGLONG g_freq;
    static LONGLONG g_period;
    static LONGLONG g_anchor;
    static LONGLONG g_refreshedAt;
    static LONGLONG g_renderedTarget;
    static HRESULT(WINAPI *g_getTiming)(HWND, DWM_TIMING_INFO *);

    inline LONGLONG Now()
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return now.QuadPart;
    }

    inline void Refresh(LONGLONG now)
    {
        if (!g_freq)
        {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            g_freq = f.QuadPart;
            g_period = g_freq / 240;
            g_anchor = now;
            HMODULE dwm = LoadLibraryA("dwmapi.dll");
            if (dwm)
                g_getTiming = (HRESULT(WINAPI *)(HWND, DWM_TIMING_INFO *))GetProcAddress(dwm, "DwmGetCompositionTimingInfo");
        }
        if (g_refreshedAt && now - g_refreshedAt < g_freq / 4)
            return;
        g_refreshedAt = now;
        if (!g_getTiming)
            return;
        DWM_TIMING_INFO info = {0};
        info.cbSize = sizeof(info);
        if (FAILED(g_getTiming(NULL, &info)) || !info.qpcRefreshPeriod || !info.qpcVBlank)
            return;
        g_period = (LONGLONG)info.qpcRefreshPeriod;
        g_anchor = (LONGLONG)info.qpcVBlank + (LONGLONG)(g_period * kUploadPhaseAfterVBlank);
    }

    inline LONGLONG Target(LONGLONG now)
    {
        Refresh(now);
        LONGLONG offset = (now - g_anchor) % g_period;
        if (offset < 0)
            offset += g_period;
        return now - offset + g_period;
    }

    inline LONGLONG RenderDue(LONGLONG now)
    {
        LONGLONG target = Target(now);
        return g_renderedTarget < target ? now : target;
    }
}
