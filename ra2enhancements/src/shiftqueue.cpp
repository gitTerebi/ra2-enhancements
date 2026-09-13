#include "patch.h"
#include "features.h"

enum { EV_PRODUCE = 0x0E, EV_ABANDON = 0x10, EV_ABANDON_ALL = 0x2E };
enum { OUTLIST_MAX = 0x80, OUTLIST_MASK = 0x7F, EVENT_SIZE = 111, SHIFT_BATCH = 5 };

typedef void *(__fastcall *FillEventFn)(void *self, void *edx, int player, int kind, int absType,
                                        int idx, int isNaval);
typedef DWORD(WINAPI *TimeGetTimeFn)(void);

struct QueueProfile
{
    const char *name;
    DWORD fillEvent;
    DWORD produceSites[2];
    DWORD abandonSites[2];
    DWORD *outCount;
    DWORD *outIndex;
    BYTE *outRing;
    DWORD *outStamps;
    TimeGetTimeFn *timeGetTime;
};

static const QueueProfile kQueues[] = {
    {"gamemd", 0x004C6970,
     {0x006AB511, 0x006AB76E},
     {0x006AB1FF, 0x006AB231},
     (DWORD *)0x00A802C8, (DWORD *)0x00A802D0, (BYTE *)0x00A802D4, (DWORD *)0x00A83A54,
     (TimeGetTimeFn *)0x007E1530},
    {"game", 0x004B6280,
     {0x0068119E, 0x006813EF},
     {0x00680B42, 0x00680EBE},
     (DWORD *)0x00A32338, (DWORD *)0x00A32340, (BYTE *)0x00A32344, (DWORD *)0x00A35AC4,
     (TimeGetTimeFn *)0x0079A524},
};

static const QueueProfile *g_queue;

static void PushOutList(const void *event)
{
    if (*g_queue->outCount >= OUTLIST_MAX)
        return;
    DWORD i = *g_queue->outIndex;
    memcpy(g_queue->outRing + i * EVENT_SIZE, event, EVENT_SIZE);
    g_queue->outStamps[i] = (*g_queue->timeGetTime)();
    *g_queue->outIndex = (i + 1) & OUTLIST_MASK;
    ++*g_queue->outCount;
}

static void *__fastcall FillEventHook(void *self, void *edx, int player, int kind, int absType,
                                      int idx, int isNaval)
{
    FillEventFn orig = (FillEventFn)g_queue->fillEvent;
    if (!(GetKeyState(VK_SHIFT) & 0x8000))
        return orig(self, 0, player, kind, absType, idx, isNaval);

    if (kind == EV_ABANDON_ALL)
        kind = EV_ABANDON;
    for (int n = 1; n < SHIFT_BATCH; ++n)
        PushOutList(orig(self, 0, player, kind, absType, idx, isNaval));
    return orig(self, 0, player, kind, absType, idx, isNaval);
}

void InstallQueueBatch()
{
    const GameProfile *g = DetectGame();
    if (!g)
        return;
    const QueueProfile *q = NULL;
    for (int i = 0; i < _countof(kQueues); ++i)
        if (strcmp(kQueues[i].name, g->name) == 0)
            q = &kQueues[i];
    if (!q)
        return;

    for (int i = 0; i < 2; ++i)
        if (!CallGoesTo(q->produceSites[i], q->fillEvent) ||
            !CallGoesTo(q->abandonSites[i], q->fillEvent))
            return;

    g_queue = q;
    for (int i = 0; i < 2; ++i)
    {
        WriteCall(q->produceSites[i], FillEventHook);
        WriteCall(q->abandonSites[i], FillEventHook);
    }
}
