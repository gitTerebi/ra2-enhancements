#pragma once
#include <windows.h>
#include <stdlib.h>
#include "patchguard.h"

namespace LayerFilter
{
    static const DWORD kFrame = 0x00A8ED84;
    static const DWORD kItems = 0x008A0394;
    static const DWORD kCount = 0x008A03A0;
    static const DWORD kTerrainCall = 0x006D44D3;
    static const DWORD kTerrainPass = 0x006D3AC0;
    static const DWORD kBuildingCall = 0x006D44F7;
    static const DWORD kBuildingPass = 0x006D3870;

    typedef int (__thiscall *WhatAmIFn)(void *);
    typedef void (__thiscall *PassFn)(void *, int, int, int);

    struct Filtered
    {
        int type;
        void **objects;
        int *indexes;
        int count;
        int capacity;
        DWORD frame;
        void **items;
        int itemCount;
        bool valid;
    };

    static Filtered g_terrain = {0x24};
    static Filtered g_buildings = {6};
    static bool g_check;
    static long g_passes, g_rebuilt, g_mismatched, g_changed;

    inline int WhatAmI(void *o)
    {
        return ((WhatAmIFn)(*(void ***)o)[0x2C / 4])(o);
    }

    static bool Current(const Filtered &f, void **items, int n)
    {
        if (!f.valid || f.frame != *(DWORD *)kFrame || f.items != items || f.itemCount != n)
            return false;
        for (int i = 0; i < f.count; ++i)
            if (items[f.indexes[i]] != f.objects[i])
                return false;
        return true;
    }

    static bool Rebuild(Filtered &f, void **items, int n)
    {
        f.valid = false;
        if (f.capacity < n)
        {
            void **objects = (void **)realloc(f.objects, n * sizeof(void *));
            if (objects)
                f.objects = objects;
            int *indexes = (int *)realloc(f.indexes, n * sizeof(int));
            if (indexes)
                f.indexes = indexes;
            if (!objects || !indexes)
                return false;
            f.capacity = n;
        }
        f.count = 0;
        for (int i = 0; i < n; ++i)
            if (WhatAmI(items[i]) == f.type)
            {
                f.objects[f.count] = items[i];
                f.indexes[f.count++] = i;
            }
        f.frame = *(DWORD *)kFrame;
        f.items = items;
        f.itemCount = n;
        f.valid = true;
        ++g_rebuilt;
        return true;
    }

    static bool MatchesFresh(const Filtered &f, void **items, int n)
    {
        int k = 0;
        for (int i = 0; i < n; ++i)
            if (WhatAmI(items[i]) == f.type && (k >= f.count || f.objects[k++] != items[i]))
                return false;
        return k == f.count;
    }

    static void Run(Filtered &f, DWORD pass, void *tactical, int a, int b, int c)
    {
        void **items = *(void ***)kItems;
        int n = *(int *)kCount;
        if (++g_passes % 2000 == 0)
            SpeedLog("  tree and building filter: %ld passes, %ld rebuilt, %ld mismatched, %ld changed during draw%s",
                     g_passes, g_rebuilt, g_mismatched, g_changed, g_check ? ", checking only" : "");
        bool ready = Current(f, items, n) || Rebuild(f, items, n);
        if (!ready || g_check)
        {
            if (ready && !MatchesFresh(f, items, n))
                ++g_mismatched;
            ((PassFn)pass)(tactical, a, b, c);
            return;
        }
        *(void ***)kItems = f.objects;
        *(int *)kCount = f.count;
        ((PassFn)pass)(tactical, a, b, c);
        if (*(void ***)kItems != f.objects || *(int *)kCount != f.count)
            ++g_changed;
        *(void ***)kItems = items;
        *(int *)kCount = n;
    }

    static void __fastcall TerrainPass(void *tactical, void *, int a, int b, int c)
    {
        Run(g_terrain, kTerrainPass, tactical, a, b, c);
    }

    static void __fastcall BuildingPass(void *tactical, void *, int a, int b, int c)
    {
        Run(g_buildings, kBuildingPass, tactical, a, b, c);
    }

    inline void Install()
    {
        if (!CallGoesTo(kTerrainCall, kTerrainPass) || !CallGoesTo(kBuildingCall, kBuildingPass))
            return;
        g_check = DdrawIniFlag("layerfiltercheck");
        WriteCall(kTerrainCall, TerrainPass);
        WriteCall(kBuildingCall, BuildingPass);
    }
}
