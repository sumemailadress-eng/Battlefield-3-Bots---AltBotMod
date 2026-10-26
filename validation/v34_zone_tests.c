// AltBotMod V34: zone_nav.c against every converted Fun-Bots mesh (BotMod_traces/navzones/*.zon).
#include "zone_nav.h"
#include <assert.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static float d3(const float *a, const float *b) { float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return sqrtf(x * x + y * y + z * z); }
static int main2(char **argv);
int main(int argc, char **argv) { (void)argc; return main2(argv); }
static int main2(char **argv)
{
    const char *dir = argv[1];
    DIR *dd = opendir(dir); assert(dd);
    struct dirent *de; int files = 0, routes = 0, fails = 0, mcomZones = 0, capZones = 0; double loadMax = 0, routeMax = 0;
    float out[256 * 3]; unsigned char fl[256];
    srand(7);
    while ((de = readdir(dd))) {
        if (!strstr(de->d_name, ".zon")) continue;
        char path[1024]; snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        ZoneNet z; double t0 = now_ms();
        if (!zn_load(&z, path)) { fprintf(stderr, "LOAD FAILED %s\n", path); return 1; }
        double lt = now_ms() - t0; if (lt > loadMax) loadMax = lt;
        files++;
        assert(z.np > 0 && z.ne > 0 && z.nz > 0 && z.parts >= 1);
        for (int k = 0; k < z.nz; k++) {
            if (z.zones[k].kind == ZN_KIND_MCOM) mcomZones++;
            if (z.zones[k].kind == ZN_KIND_CAPTURE) capZones++;
            assert(z.zones[k].ninside >= 0 && z.zones[k].inside0 + z.zones[k].ninside <= z.ninside);
        }
        // routes: from a point of each zone to another point of the same zone (walking inside an objective) and to the zone's
        // point closest to its centre (an M-COM), plus random pairs in the same part
        for (int k = 0; k < z.nz; k++) {
            for (int r = 0; r < 6; r++) {
                int a = zn_zone_point(&z, k, (unsigned)rand()), b = r == 0 ? zn_closest(&z, z.zones[k].c, 30, 6) : zn_zone_point(&z, k, (unsigned)rand());
                if (a < 0 || b < 0) continue;
                int partial = 0; float len = 0; double t1 = now_ms();
                int n = zn_route(&z, a, b, out, fl, 256, &partial, &len);
                double rt = now_ms() - t1; if (rt > routeMax) routeMax = rt;
                if (z.pts[a].part != z.pts[b].part) { assert(n == 0); continue; }
                routes++;
                if (!n) { fails++; continue; }
                // every step is a real step (no jump across the map), the route ends on b unless cut short
                float prev[3]; memcpy(prev, z.pts[a].p, 12);
                for (int q = 0; q < n; q++) { assert(isfinite(out[q * 3]) && isfinite(out[q * 3 + 1]) && isfinite(out[q * 3 + 2])); assert(d3(prev, &out[q * 3]) < 60.0f); memcpy(prev, &out[q * 3], 12); }
                if (!partial) assert(d3(&out[(n - 1) * 3], z.pts[b].p) < 0.01f);
                assert(len >= d3(z.pts[a].p, z.pts[b].p) * 0.999f - 0.5f);
            }
        }
        // a stuck connection: penalised, closed after 3 times, routes avoid it or report no way
        int e0 = z.ne / 2; const float *pa = z.pts[z.edges[e0].a].p, *pb = z.pts[z.edges[e0].b].p;
        float mid[3] = {(pa[0] + pb[0]) * 0.5f, (pa[1] + pb[1]) * 0.5f, (pa[2] + pb[2]) * 0.5f};
        int hit = zn_penalize(&z, mid, 50); assert(hit >= 0);
        zn_penalize(&z, mid, 50); zn_penalize(&z, mid, 50);
        assert(z.edges[hit].removed && z.edges[hit].penalty >= 150);
        zn_free(&z);
    }
    closedir(dd);
    // garbage never loads, and a file cut short never loads
    ZoneNet bad;
    assert(!zn_load_text(&bad, "AltBotMod navzones 1 X\nP 2\n0 0 0 1\n"));
    assert(!zn_load_text(&bad, "AltBotMod navzones 2 X\nP 1\n0 0 0 1\nE 0\nZ 0\n"));
    assert(!zn_load_text(&bad, "hello"));
    assert(zn_load_text(&bad, "AltBotMod navzones 1 X\nP 3\n0 0 0 1\n5 0 0 1\n10 0 0 4\nE 3\n0 1 5 0 0 0\n1 2 5 1 1 1 7 0 0.5\n0 9 5 0 0 0\nZ 1\n2 20 10 0 0 2 mcom_1\n2 1\n"));
    assert(bad.np == 3 && bad.ne == 2 && bad.nz == 1 && bad.zones[0].ninside == 2 && !strcmp(bad.zones[0].name, "mcom 1"));
    { int partial; float len; int n = zn_route(&bad, 0, 2, out, fl, 16, &partial, &len);
      assert(n == 3 && fl[1] == ZN_ROUTE_JUMP && fl[2] == ZN_ROUTE_CROUCH && fabsf(out[2 * 3] - 10) < 1e-4f && !partial);
      assert(zn_route(&bad, 2, 0, out, fl, 16, &partial, &len) == 0);   // one way: not back up the jump
      n = zn_route(&bad, 0, 2, out, fl, 2, &partial, &len); assert(n == 2 && partial); }
    zn_free(&bad);
    printf("zone mesh: %d files, %d M-COM zones, %d capture zones, %d routes (%d without a way), load max %.0f ms, route max %.2f ms\n",
           files, mcomZones, capZones, routes, fails, loadMax, routeMax);
    assert(files >= 90 && mcomZones > 150 && capZones > 200 && fails * 20 < routes);
    puts("zone mesh ok");
    return 0;
}
