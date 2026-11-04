// AltBotMod V35.1: the boats' clear mask, V34 (copy + whole-grid strip per plan) against V35.1 (cached strip + two keep
// disks), on a real level grid: the same mask for every plan, and the time each takes.
// usage: v351_boat_mask_bench <BotMod_nav_<level>.bin>
#include "nav.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int main(int argc, char **argv)
{
    assert(argc > 1);
    NavGrid *g = nav_load(argv[1]); assert(g);
    int count = 0; unsigned char *mask = nav_boat_mask(g, &count); assert(mask && count > 0);
    size_t n = (size_t)g->w * g->h * NAV_LAYERS;
    int water[4096], nw = 0;
    for (size_t k = 0; k < n && nw < 4096; k += 97) if (mask[k]) water[nw++] = (int)k;
    assert(nw > 100);
    unsigned char *oldM = malloc(n), *cache = malloc(n);
    memcpy(cache, mask, n); nav_mask_strip(g, cache, 4, NULL, 0, 0);
    int shallows = 0; for (size_t k = 0; k < n; k++) shallows += mask[k] == 4;
    double tOld = 0, tNew = 0; srand(7);
    for (int it = 0; it < 40; it++) {
        float a[3], b[3];
        nav_node_pos(g, water[rand() % nw], a); nav_node_pos(g, water[rand() % nw], b);
        float keep[4] = {a[0], a[2], b[0], b[2]};
        double t0 = now_ms();
        memcpy(oldM, mask, n); nav_mask_strip(g, oldM, 4, keep, 2, 40.0f);
        double t1 = now_ms();
        for (int k = 0; k < 2; k++) nav_mask_patch_disk(g, cache, mask, 0, keep[k * 2], keep[k * 2 + 1], 40.0f);
        double t2 = now_ms();
        assert(!memcmp(oldM, cache, n));   // the same mask the planner sees
        double t3 = now_ms();
        for (int k = 0; k < 2; k++) nav_mask_patch_disk(g, cache, mask, 4, keep[k * 2], keep[k * 2 + 1], 40.0f);
        double t4 = now_ms();
        tOld += t1 - t0; tNew += (t2 - t1) + (t4 - t3);
    }
    unsigned char *fresh = malloc(n); memcpy(fresh, mask, n); nav_mask_strip(g, fresh, 4, NULL, 0, 0);
    assert(!memcmp(fresh, cache, n));   // closed again: the cache is back to the plain clear mask
    int *labels = nav_land_labels(g); assert(labels);
    double tl = now_ms(); int found = 0; float w[3];
    for (int it = 0; it < 20; it++) { float a[3], b[3]; nav_node_pos(g, water[(it * 37) % nw], a); nav_node_pos(g, water[(it * 91 + 5) % nw], b);
        int lab = nav_label_at(g, labels, b[0], b[1], b[2]); if (lab) found += nav_landing_on(g, mask, labels, lab, a[0], a[2], b[0], b[2], 0.6f, 60.0f, w); }
    double landMs = (now_ms() - tl) / 20;
    printf("landing search (whole grid) %.1f ms each (%d found)\n", landMs, found);
    printf("grid %dx%d (%zu nodes, %d boat nodes, %d shallows): clear mask per plan %.1f ms (V34) -> %.3f ms (V35.1), identical masks in 40 plans\n",
           g->w, g->h, n, count, shallows, tOld / 40, tNew / 40);
    return 0;
}
