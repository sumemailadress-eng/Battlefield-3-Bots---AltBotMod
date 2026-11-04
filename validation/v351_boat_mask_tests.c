// AltBotMod V35.1: the boats' cached clear mask with its two keep disks is exactly V34's per-plan copy + strip, on a
// synthetic grid (any size, keep points on and off the grid, at the edges); the disks close again.
#include "nav.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
    int checks = 0;
    for (int run = 0; run < 3; run++) {
        int w = 120 + run * 70, h = 90 + run * 40; float cell = run == 1 ? 2.0f : 1.0f;
        NavGrid *g = nav_create("V351", -50.0f * run, 30.0f, -50.0f * run + w * cell, 30.0f + h * cell, cell); assert(g); w = g->w; h = g->h;
        size_t n = (size_t)g->w * g->h * NAV_LAYERS;
        unsigned char *mask = malloc(n), *old = malloc(n), *cache = malloc(n), *fresh = malloc(n);
        srand(11 + run);
        for (size_t k = 0; k < n; k++) { int r = rand() % 10; mask[k] = r < 4 ? 0 : r < 7 ? 1 : r < 9 ? 4 : 2; }
        memcpy(cache, mask, n); nav_mask_strip(g, cache, 4, NULL, 0, 0); memcpy(fresh, cache, n);
        for (int it = 0; it < 60; it++) {
            float span = w * cell, depth = h * cell;
            float keep[4] = {g->x0 - 20 + (rand() % 1000) / 1000.0f * (span + 40), g->z0 - 20 + (rand() % 1000) / 1000.0f * (depth + 40),
                             g->x0 + (rand() % 1000) / 1000.0f * span, g->z0 + (rand() % 1000) / 1000.0f * depth};
            float r = it % 3 == 0 ? 40.0f : it % 3 == 1 ? 7.5f : 0.6f;
            memcpy(old, mask, n); nav_mask_strip(g, old, 4, keep, 2, r);
            for (int k = 0; k < 2; k++) nav_mask_patch_disk(g, cache, mask, 0, keep[k * 2], keep[k * 2 + 1], r);
            assert(!memcmp(old, cache, n)); checks++;
            for (int k = 0; k < 2; k++) nav_mask_patch_disk(g, cache, mask, 4, keep[k * 2], keep[k * 2 + 1], r);
            assert(!memcmp(fresh, cache, n)); checks++;
        }
        free(mask); free(old); free(cache); free(fresh); nav_free(g);
    }
    (void)argc; (void)argv;
    printf("boat clear mask: %d comparisons identical to V34's copy + strip\n", checks);
    return 0;
}
