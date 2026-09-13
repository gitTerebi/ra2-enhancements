#pragma once
#include <stdlib.h>

struct EscortRank
{
    double side;
    int index;
};

static int __cdecl CompareEscortRank(const void *a, const void *b)
{
    double d = ((const EscortRank *)a)->side - ((const EscortRank *)b)->side;
    return d < 0 ? -1 : d > 0 ? 1 : 0;
}

static void RankBySide(const int *pos, int count, double dx, double dy, EscortRank *out)
{
    for (int i = 0; i < count; ++i)
    {
        out[i].side = pos[i * 3 + 1] * dx - pos[i * 3] * dy;
        out[i].index = i;
    }
    qsort(out, count, sizeof(EscortRank), CompareEscortRank);
}

static bool AssignEscorts(const int *healerPos, int healerCount, const int *armedPos,
                          int armedCount, const int *targetPos, int *assigned)
{
    for (int h = 0; h < healerCount; ++h)
        assigned[h] = -1;
    if (healerCount == 0 || armedCount == 0)
        return true;

    double cx = 0, cy = 0;
    for (int i = 0; i < healerCount; ++i)
    {
        cx += healerPos[i * 3];
        cy += healerPos[i * 3 + 1];
    }
    for (int i = 0; i < armedCount; ++i)
    {
        cx += armedPos[i * 3];
        cy += armedPos[i * 3 + 1];
    }
    cx /= healerCount + armedCount;
    cy /= healerCount + armedCount;
    double dx = targetPos[0] - cx, dy = targetPos[1] - cy;
    if (dx == 0 && dy == 0)
        dx = 1;

    EscortRank *healers = (EscortRank *)malloc(sizeof(EscortRank) * healerCount);
    EscortRank *armed = (EscortRank *)malloc(sizeof(EscortRank) * armedCount);
    if (!healers || !armed)
    {
        free(healers);
        free(armed);
        return false;
    }
    RankBySide(healerPos, healerCount, dx, dy, healers);
    RankBySide(armedPos, armedCount, dx, dy, armed);

    for (int r = 0; r < healerCount; ++r)
    {
        int a = (int)(((2LL * r + 1) * armedCount) / (2LL * healerCount));
        assigned[healers[r].index] = armed[a].index;
    }

    free(healers);
    free(armed);
    return true;
}
