#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <string.h>

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

    static const int kCostSamples = 32;
    static const int kMaxDivisor = 8;
    static LONGLONG g_costs[kCostSamples];
    static int g_costCount;
    static int g_costNext;
    static int g_divisor = 1;
    static LONGLONG g_fasterSince;

    inline void RecordCost(LONGLONG cost, LONGLONG now)
    {
        g_costs[g_costNext] = cost;
        g_costNext = (g_costNext + 1) % kCostSamples;
        if (g_costCount < kCostSamples)
            ++g_costCount;
        if (g_costCount < 8 || !g_period)
            return;

        LONGLONG sorted[kCostSamples];
        memcpy(sorted, g_costs, sizeof(LONGLONG) * g_costCount);
        for (int i = 1; i < g_costCount; ++i)
            for (int j = i; j > 0 && sorted[j - 1] > sorted[j]; --j)
            {
                LONGLONG t = sorted[j];
                sorted[j] = sorted[j - 1];
                sorted[j - 1] = t;
            }
        LONGLONG slow = sorted[g_costCount * 9 / 10] + g_freq / 2000;
        int wanted = (int)((slow + g_period - 1) / g_period);
        if (wanted < 1)
            wanted = 1;
        if (wanted > kMaxDivisor)
            wanted = kMaxDivisor;

        if (wanted >= g_divisor)
        {
            g_divisor = wanted;
            g_fasterSince = 0;
        }
        else if (!g_fasterSince)
            g_fasterSince = now;
        else if (now - g_fasterSince > g_freq)
        {
            g_divisor = wanted;
            g_fasterSince = 0;
        }
    }

    inline LONGLONG CameraTarget(LONGLONG now)
    {
        if (now >= g_renderedTarget)
            g_renderedTarget = Target(now) + (g_divisor - 1) * g_period;
        return g_renderedTarget;
    }

    inline LONGLONG RenderDue(LONGLONG now)
    {
        return now >= g_renderedTarget ? now : g_renderedTarget;
    }
}
