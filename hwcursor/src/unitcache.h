#pragma once
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "patchguard.h"

namespace UnitCache
{
    static const DWORD kFrame = 0x00A8ED84;
    static const DWORD kStaging = 0x00B1D13C;
    static const DWORD kDirty = 0x00B1CFC0;
    static const DWORD kPartsStart = 0x0073B66C;
    static const DWORD kPartBlit = 0x00707736;
    static const DWORD kPartBlitTarget = 0x00437A10;
    static const DWORD kCopyCall = 0x0073C1A5;

    struct Entry
    {
        void *unit;
        DWORD id;
        DWORD frame;
        DWORD generation;
        int rect[4];
        BYTE *pixels;
        int capacity;
    };

    static const int kSlots = 4096;
    static Entry g_entries[kSlots];
    static DWORD g_generation = 1;
    static DWORD g_lastFrame;
    static void *g_unit;
    static Entry *g_hit;
    static BYTE g_skip;
    static bool g_check;
    static long g_reused, g_mismatched;

    inline BYTE *StagingPixels()
    {
        BYTE *surface = *(BYTE **)kStaging;
        return surface ? *(BYTE **)(surface + 0x14) : NULL;
    }

    inline bool RectFits(const int *r)
    {
        return r[0] >= 0 && r[1] >= 0 && r[2] > 0 && r[3] > 0 && r[0] + r[2] <= 256 && r[1] + r[3] <= 256;
    }

    inline Entry &Slot(void *unit)
    {
        DWORD h = (DWORD)unit;
        h ^= h >> 13;
        h *= 0x9E3779B1;
        return g_entries[(h >> 20) & (kSlots - 1)];
    }

    static void __cdecl OnPartsStart(BYTE *unit)
    {
        DWORD frame = *(DWORD *)kFrame;
        if (frame < g_lastFrame)
            ++g_generation;
        g_lastFrame = frame;
        g_unit = unit;
        Entry &e = Slot(unit);
        g_hit = e.unit == unit && e.id == *(DWORD *)(unit + 4) && e.frame == frame &&
                e.generation == g_generation ? &e : NULL;
        g_skip = g_hit && !g_check;
    }

    static void __cdecl OnCopy(BYTE *unit)
    {
        Entry *hit = g_hit;
        bool skipped = g_skip != 0;
        g_hit = NULL;
        g_skip = 0;
        if (unit != g_unit)
            return;
        g_unit = NULL;
        BYTE *px = StagingPixels();
        int *dirty = (int *)kDirty;
        if (!px)
            return;
        if (hit)
        {
            const int *r = hit->rect;
            if (skipped)
            {
                for (int y = 0; y < r[3]; ++y)
                    memcpy(px + (r[1] + y) * 256 + r[0], hit->pixels + y * r[2], r[2]);
                memcpy(dirty, r, sizeof(hit->rect));
                ++g_reused;
                return;
            }
            bool same = memcmp(dirty, r, sizeof(hit->rect)) == 0;
            for (int y = 0; same && y < r[3]; ++y)
                same = memcmp(px + (r[1] + y) * 256 + r[0], hit->pixels + y * r[2], r[2]) == 0;
            ++g_reused;
            if (!same)
                ++g_mismatched;
            if (g_reused % 5000 == 0)
                SpeedLog("  unit image reuse check: %ld reused, %ld differed", g_reused, g_mismatched);
            return;
        }
        if (!RectFits(dirty))
            return;
        Entry &e = Slot(unit);
        int need = dirty[2] * dirty[3];
        if (e.capacity < need)
        {
            BYTE *grown = (BYTE *)realloc(e.pixels, need);
            if (!grown)
            {
                e.unit = NULL;
                return;
            }
            e.pixels = grown;
            e.capacity = need;
        }
        for (int y = 0; y < dirty[3]; ++y)
            memcpy(e.pixels + y * dirty[2], px + (dirty[1] + y) * 256 + dirty[0], dirty[2]);
        memcpy(e.rect, dirty, sizeof(e.rect));
        e.unit = unit;
        e.id = *(DWORD *)(unit + 4);
        e.frame = *(DWORD *)kFrame;
        e.generation = g_generation;
    }

    static __declspec(naked) void PartsStartStub()
    {
        __asm {
            pushad
            pushfd
            push ebp
            call OnPartsStart
            add esp, 4
            popfd
            popad
            mov ecx, dword ptr [ebp + 0x6C4]
            push 0x0073B672
            ret
        }
    }

    static __declspec(naked) void PartBlitStub()
    {
        __asm {
            cmp byte ptr [g_skip], 0
            je original
            cmp ecx, dword ptr ds:[0x00B1D13C]
            jne original
            ret 0x34
        original:
            push 0x00437A10
            ret
        }
    }

    static __declspec(naked) void CopyStub()
    {
        __asm {
            pushad
            pushfd
            push ebp
            call OnCopy
            add esp, 4
            popfd
            popad
            jmp dword ptr [eax + 0x55C]
        }
    }

    inline void Install()
    {
        static const BYTE partsStart[6] = {0x8B, 0x8D, 0xC4, 0x06, 0x00, 0x00};
        static const BYTE copyCall[6] = {0xFF, 0x90, 0x5C, 0x05, 0x00, 0x00};
        if (!BytesAt(kPartsStart, partsStart, sizeof(partsStart)) ||
            !BytesAt(kCopyCall, copyCall, sizeof(copyCall)) ||
            !CallGoesTo(kPartBlit, kPartBlitTarget))
            return;
        g_check = DdrawIniFlag("unitcachecheck");
        WriteJmp(kPartsStart, PartsStartStub, sizeof(partsStart));
        WriteCall(kCopyCall, CopyStub, sizeof(copyCall));
        WriteCall(kPartBlit, PartBlitStub);
    }
}
