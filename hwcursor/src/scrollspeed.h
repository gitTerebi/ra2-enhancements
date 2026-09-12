#pragma once

namespace ScrollSpeed
{
    // Native menu stores 6 - slider position: 0 fastest, 6 slowest.
    static const int kDefaultRate = 3;
    static const double kMediumSpeed = 1900.0;
    static const int *g_rate;

    inline int Rate()
    {
        int rate = g_rate ? *g_rate : kDefaultRate;
        return rate < 0 ? 0 : rate > 6 ? 6 : rate;
    }

    inline double PixelsPerSecond()
    {
        static const double speeds[] = {2400, 2233, 2067, kMediumSpeed, 1733, 1567, 1400};
        return speeds[Rate()];
    }
}
