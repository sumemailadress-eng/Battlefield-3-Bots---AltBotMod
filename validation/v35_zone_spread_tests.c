// AltBotMod V35: the mesh routing (zone_nav.c zn_route_ex / zn_on_killed / zn_decay),
// run on the shipped meshes. usage: v35_zone_spread_tests <navzones folder>
#include "zone_nav.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXP 512
static float outA[MAXP * 3], outB[MAXP * 3];

static int same_route(const float *x, int nx, const float *y, int ny)
{
    if (nx != ny) return 0;
    for (int k = 0; k < nx * 3; k++) if (fabsf(x[k] - y[k]) > 1e-4f) return 0;
    return 1;
}
// a pair of points in one part of the mesh, far apart (from the first zone's first point)
static void far_pair(const ZoneNet *z, int *a, int *b)
{
    *a = z->nz ? z->inside[z->zones[0].inside0] : 0; *b = -1; float best = 0;
    for (int n = 0; n < z->np; n++) {
        if (z->pts[n].part != z->pts[*a].part) continue;
        float dx = z->pts[n].p[0] - z->pts[*a].p[0], dz = z->pts[n].p[2] - z->pts[*a].p[2], d = dx * dx + dz * dz;
        if (d > best && d < 250.0f * 250.0f) { best = d; *b = n; }
    }
}

int main(int argc, char **argv)
{
    assert(argc > 1); setvbuf(stdout, NULL, _IONBF, 0);
    const char *maps[] = {"MP_003_RushLarge0", "MP_Subway_RushLarge0", "XP2_Office_Domination0", "MP_012_SquadRush0", "XP3_Valley_ConquestSmall0"};
    int mapsDone = 0, spreadMaps = 0, dangerMaps = 0;
    for (unsigned m = 0; m < sizeof maps / sizeof maps[0]; m++) {
        char path[512]; snprintf(path, sizeof path, "%s/%s.zon", argv[1], maps[m]);
        ZoneNet z; memset(&z, 0, sizeof z);
        if (!zn_load(&z, path)) { printf("%s: not found\n", maps[m]); continue; }
        mapsDone++;
        int a, b; far_pair(&z, &a, &b); assert(b >= 0);
        int pa, pb; float la, lb;
        // 1. without a seed or a team: exactly the V34 route
        int na = zn_route(&z, a, b, outA, NULL, MAXP, &pa, &la);
        int nb = zn_route_ex(&z, a, b, 0, 0, outB, NULL, MAXP, &pb, &lb);
        assert(na > 0 && same_route(outA, na, outB, nb) && fabsf(la - lb) < 1e-3f);
        // 2. 200 lives: their own ways, never more than the spread (25 %) longer than the shortest, always arriving
        int distinct = 0; float worst = 0; static float seen[24][MAXP * 3]; static int seenN[24]; int nSeen = 0;
        for (unsigned seed = 1; seed <= 200; seed++) {
            int p; float l; int n = zn_route_ex(&z, a, b, seed * 7919u % 99991u + 1, 0, outB, NULL, MAXP, &p, &l);
            assert(n > 0);
            const float *end = &outB[(n - 1) * 3];
            if (!p) assert(fabsf(end[0] - z.pts[b].p[0]) < 1e-3f && fabsf(end[2] - z.pts[b].p[2]) < 1e-3f);
            if (l / la > worst) worst = l / la;
            int known = 0;
            for (int k = 0; k < nSeen && !known; k++) known = same_route(seen[k], seenN[k], outB, n);
            if (!known && nSeen < 24) { memcpy(seen[nSeen], outB, (size_t)n * 12); seenN[nSeen++] = n; distinct++; }
        }
        assert(worst <= 1.0f + ZN_SPREAD + 0.02f);
        if (distinct >= 3) spreadMaps++;
        // 3. danger: three deaths half way along the shortest route, for team 1
        const float *mid = &outA[(na / 2) * 3];
        float t = 100.0f; int marked = 0;
        for (int k = 0; k < 3; k++) marked += zn_on_killed(&z, 1, mid, t + k);
        assert(marked > 0);
        int nearest = zn_closest(&z, mid, 6.0f, 2.5f); assert(nearest >= 0);
        assert(zn_danger(&z, 1, nearest) > 40.0f && zn_danger(&z, 1, nearest) <= ZN_DANGER_MAX && zn_danger(&z, 2, nearest) == 0);
        int nd = zn_route_ex(&z, a, b, 0, 1, outB, NULL, MAXP, &pb, &lb);
        assert(nd > 0);
        float closest = 1e9f;   // how near the cautious team-1 way comes to the deaths
        for (int k = 0; k < nd; k++) { float dx = outB[k * 3] - mid[0], dz = outB[k * 3 + 2] - mid[2]; float d = sqrtf(dx * dx + dz * dz); if (d < closest) closest = d; }
        int detour = closest > 3.0f;
        if (detour) dangerMaps++;
        assert(lb <= la + 3 * ZN_DANGER_MAX + 1.0f);   // never a detour longer than the danger it avoids
        // team 2 is not affected
        int n2 = zn_route_ex(&z, a, b, 0, 2, outB, NULL, MAXP, &pb, &lb);
        assert(same_route(outA, na, outB, n2));
        // 4. it fades: half after a minute, gone after 6
        float before = zn_danger(&z, 1, nearest);
        zn_decay(&z, t + 60.0f);   // (the clock started at the first death)
        assert(fabsf(zn_danger(&z, 1, nearest) - before * 0.5f) < 0.5f);
        zn_decay(&z, t + 2 + 360.0f);
        assert(zn_danger(&z, 1, nearest) == 0 && z.dangerCount[1] == 0);
        int nc = zn_route_ex(&z, a, b, 0, 1, outB, NULL, MAXP, &pb, &lb);
        assert(same_route(outA, na, outB, nc));
        printf("%s: %d points, way %.0f m; %d different ways for 200 lives (longest %.0f%% of the shortest); after 3 deaths on it team 1 %s (closest %.1f m)\n",
               maps[m], z.np, la, distinct, worst * 100, detour ? "goes round" : "has no other way", closest);
        // 5. a broken position never marks anything
        float bad[3] = {NAN, 0, 0}; assert(zn_on_killed(&z, 1, bad, t) == 0 && zn_on_killed(&z, 3, mid, t) == 0);
        zn_free(&z);
    }
    // 6. cautious share: 3 of 4 lives
    int cautious = 0; for (unsigned s = 1; s <= 10000; s++) cautious += zn_cautious(s);
    assert(cautious > 7000 && cautious < 8000);
    printf("cautious lives %.1f%%\n", cautious / 100.0);
    assert(mapsDone == 5 && spreadMaps >= 4 && dangerMaps >= 3);
    printf("spread ok on %d of %d maps, danger detour on %d\n", spreadMaps, mapsDone, dangerMaps);
    return 0;
}
