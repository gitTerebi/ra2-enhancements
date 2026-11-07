#pragma once
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "patchguard.h"

namespace FrameReuse
{
    static const DWORD kFrame = 0x00A8ED84;
    static const DWORD kTempSurface = 0x00887314;
    static const DWORD kTactical = 0x00887324;
    static const DWORD kViewBounds = 0x00886FA0;
    static const DWORD kObjectsCall = 0x006D465F;
    static const DWORD kDrawObjects = 0x006D8DB0;
    static const DWORD kStageEndCall = 0x006D492B;
    static const DWORD kOreGlitter = 0x006D7840;
    static const DWORD kFootDraw = 0x006D9070;
    static const int kMarginX = 0x168;
    static const int kMarginY = 0xB4;

    typedef BYTE *(__thiscall *LockFn)(void *, int, int);
    typedef void (__thiscall *UnlockFn)(void *);
    typedef int (__thiscall *IntFn)(void *);

    static bool g_scrollFrame;
    static bool g_scrollInput;
    static bool g_installed;
    static BYTE g_cull;
    static bool g_reuse;
    static bool g_check;
    static int g_dx, g_dy;
    static int g_stripX0, g_stripX1, g_stripY0, g_stripY1;

    static WORD *g_prev;
    static int g_prevCapacity;
    static bool g_prevValid;
    static int g_prevSurfaceW, g_prevSurfaceH;
    static DWORD g_prevFrame;
    static int g_prevView[4];
    static int g_prevX, g_prevY;
    static long g_checked, g_differed, g_scrollFrames;
    static long g_declined[6];
    static bool g_loggedGeometry;

    inline bool Fresh()
    {
        return !g_prevValid || g_prevFrame != *(DWORD *)kFrame;
    }

    inline BYTE *Tactical()
    {
        return *(BYTE **)kTactical;
    }

    static bool __cdecl KeepFoot(int x, int y)
    {
        return (x >= g_stripX0 - kMarginX && x <= g_stripX1 + kMarginX) ||
               (y >= g_stripY0 - kMarginY && y <= g_stripY1 + kMarginY);
    }

    static void __cdecl OnObjects()
    {
        g_reuse = false;
        g_cull = 0;
        BYTE *tac = Tactical();
        const int *view = (const int *)kViewBounds;
        BYTE *surface = *(BYTE **)kTempSurface;
        if (!g_scrollFrame || !tac || !surface)
            return;
        if (++g_scrollFrames % 1000 == 0)
            SpeedLog("  frame reuse: %ld scroll frames, %ld reused, %ld differed; declined: no previous %ld, "
                     "new frame %ld, surface size %ld, view %ld, mouse %ld, far %ld",
                     g_scrollFrames, g_checked, g_differed, g_declined[0], g_declined[1], g_declined[2],
                     g_declined[3], g_declined[4], g_declined[5]);
        int reason = !g_prevValid ? 0
                     : g_prevFrame != *(DWORD *)kFrame ? 1
                     : *(int *)(surface + 4) != g_prevSurfaceW || *(int *)(surface + 8) != g_prevSurfaceH ? 2
                     : memcmp(g_prevView, view, sizeof(g_prevView)) != 0 ? 3
                     : (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 4
                     : -1;
        int w = view[2], h = view[3];
        g_dx = *(int *)(tac + 0xB0) - g_prevX;
        g_dy = *(int *)(tac + 0xB4) - g_prevY;
        if (reason < 0 && (abs(g_dx) >= w / 2 || abs(g_dy) >= h / 2))
            reason = 5;
        if (reason >= 0)
        {
            ++g_declined[reason];
            return;
        }
        g_stripX0 = g_dx > 0 ? w - g_dx : 0;
        g_stripX1 = g_dx > 0 ? w : -g_dx;
        g_stripY0 = g_dy > 0 ? h - g_dy : 0;
        g_stripY1 = g_dy > 0 ? h : -g_dy;
        if (!g_dx)
            g_stripX0 = g_stripX1 = -100000;
        if (!g_dy)
            g_stripY0 = g_stripY1 = -100000;
        g_reuse = true;
        g_cull = !g_check;
    }

    static void __cdecl OnStageEnd()
    {
        bool reuse = g_reuse;
        g_reuse = false;
        g_cull = 0;
        BYTE *surface = *(BYTE **)kTempSurface;
        BYTE *tac = Tactical();
        const int *view = (const int *)kViewBounds;
        g_prevValid = false;
        if (!surface || !tac || !(g_scrollInput || g_scrollFrame))
            return;
        void **vt = *(void ***)surface;
        int x0 = view[0], y0 = view[1], w = view[2], h = view[3];
        if (!g_loggedGeometry)
        {
            g_loggedGeometry = true;
            SpeedLog("  frame reuse: view %d,%d %dx%d, surface %dx%d, %d bytes per pixel", x0, y0, w, h,
                     *(int *)(surface + 4), *(int *)(surface + 8), ((IntFn)vt[0x70 / 4])(surface));
        }
        if (((IntFn)vt[0x70 / 4])(surface) != 2 || x0 < 0 || y0 < 0 || w <= 0 || h <= 0 ||
            x0 + w > *(int *)(surface + 4) || y0 + h > *(int *)(surface + 8))
            return;
        if (g_prevCapacity < w * h)
        {
            WORD *grown = (WORD *)realloc(g_prev, w * h * sizeof(WORD));
            if (!grown)
                return;
            g_prev = grown;
            g_prevCapacity = w * h;
            reuse = false;
        }
        BYTE *bits = ((LockFn)vt[0x5C / 4])(surface, 0, 0);
        if (!bits)
            return;
        int pitch = ((IntFn)vt[0x74 / 4])(surface);
        int c0 = g_dx < 0 ? -g_dx : 0;
        int c1 = g_dx > 0 ? w - g_dx : w;
        if (reuse)
        {
            bool differs = false;
            for (int r = 0; r < h; ++r)
            {
                int src = r + g_dy;
                if (src < 0 || src >= h || c1 <= c0)
                    continue;
                WORD *dst = (WORD *)(bits + (y0 + r) * pitch) + x0 + c0;
                const WORD *from = g_prev + src * w + c0 + g_dx;
                size_t n = (c1 - c0) * sizeof(WORD);
                if (g_check)
                    differs |= memcmp(dst, from, n) != 0;
                else
                    memcpy(dst, from, n);
            }
            ++g_checked;
            if (differs)
                ++g_differed;
        }
        for (int r = 0; r < h; ++r)
            memcpy(g_prev + r * w, (WORD *)(bits + (y0 + r) * pitch) + x0, w * sizeof(WORD));
        ((UnlockFn)vt[0x60 / 4])(surface);
        g_prevValid = true;
        g_prevSurfaceW = *(int *)(surface + 4);
        g_prevSurfaceH = *(int *)(surface + 8);
        g_prevFrame = *(DWORD *)kFrame;
        memcpy(g_prevView, view, sizeof(g_prevView));
        g_prevX = *(int *)(tac + 0xB0);
        g_prevY = *(int *)(tac + 0xB4);
    }

    static __declspec(naked) void ObjectsStub()
    {
        __asm {
            pushad
            pushfd
            call OnObjects
            popfd
            popad
            push 0x006D8DB0
            ret
        }
    }

    static __declspec(naked) void StageEndStub()
    {
        __asm {
            pushad
            pushfd
            call OnStageEnd
            popfd
            popad
            push 0x006D7840
            ret
        }
    }

    static __declspec(naked) void FootStub()
    {
        __asm {
            cmp byte ptr [g_cull], 0
            je draw
            push eax
            push ecx
            push edx
            push dword ptr [esp + 0x0C + 0x1C]
            push dword ptr [esp + 0x10 + 0x18]
            call KeepFoot
            add esp, 8
            test al, al
            pop edx
            pop ecx
            pop eax
            jnz draw
            mov byte ptr [esi + 0x99], 1
            push 0x006D9408
            ret
        draw:
            mov edx, dword ptr [esi]
            lea eax, [esp + 0x18]
            push 0x006D9076
            ret
        }
    }

    inline void Install()
    {
        static const BYTE footDraw[6] = {0x8B, 0x16, 0x8D, 0x44, 0x24, 0x18};
        if (!CallGoesTo(kObjectsCall, kDrawObjects) || !CallGoesTo(kStageEndCall, kOreGlitter) ||
            !BytesAt(kFootDraw, footDraw, sizeof(footDraw)))
            return;
        g_check = DdrawIniFlag("framereusecheck");
        WriteCall(kObjectsCall, ObjectsStub);
        WriteCall(kStageEndCall, StageEndStub);
        WriteJmp(kFootDraw, FootStub, sizeof(footDraw));
    }
}
