// AltBotMod V27: a tracked tank (turns at most 0.7 rad/s at any speed, pivots on the spot, 16 m/s, +3 / -6 m/s^2) on the
// longest Fun-Bots land road of a map, driven as drive_tick does: pure pursuit (bm_route_progress / bm_route_aim), the
// heading caps (6 m/s from 60 deg off, 4 m/s from 92 deg), road-only bend limits, and the V27 pivot (no throttle, full
// lock, until within 20 deg). Asserts it finishes the road, keeps a decent average and pivots only for the turn round at the start.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "enhanced.h"
#define MAXP 4096
static float pts[MAXP * 3]; static int npts;
static int load_longest_land(const char *file)
{
    FILE *f = fopen(file, "r"); if (!f) return 0;
    static float cur[MAXP * 3]; int curN = 0, curPath = -1, curLand = 0; char line[4096];
    while (fgets(line, sizeof line, f)) {
        int path, idx; float x, y, z;
        if (sscanf(line, "%d;%d;%f;%f;%f", &path, &idx, &x, &y, &z) != 5) continue;
        if (path != curPath) { if (curLand && curN > npts) { memcpy(pts, cur, sizeof(float) * 3 * curN); npts = curN; } curPath = path; curN = 0; curLand = 0; }
        if (strstr(line, "\"land\"")) curLand = 1;
        if (curN < MAXP) { cur[curN * 3] = x; cur[curN * 3 + 1] = y; cur[curN * 3 + 2] = z; curN++; }
    }
    if (curLand && curN > npts) { memcpy(pts, cur, sizeof(float) * 3 * curN); npts = curN; }
    fclose(f); return npts;
}
static float wrapf(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static float d2(const float *a, const float *b) { float x = a[0] - b[0], z = a[2] - b[2]; return x * x + z * z; }
static float off_route(const float *p)
{
    float best = 1e9f;
    for (int k = 0; k + 1 < npts; k++) {
        const float *a = &pts[k * 3], *b = &pts[(k + 1) * 3];
        float ax = b[0] - a[0], az = b[2] - a[2], l = ax * ax + az * az, t = l > 0 ? ((p[0] - a[0]) * ax + (p[2] - a[2]) * az) / l : 0;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        float x = a[0] + ax * t - p[0], z = a[2] + az * t - p[2], d = sqrtf(x * x + z * z);
        if (d < best) best = d;
    }
    return best;
}
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    assert(argc > 1 && load_longest_land(argv[1]) > 50);
    float len = 0; for (int k = 1; k < npts; k++) len += sqrtf(d2(&pts[k * 3], &pts[(k - 1) * 3]));
    /* the tank starts facing the wrong way: the first thing it does is turn round */
    float pos[3] = {pts[0], pts[1], pts[2]}, heading = atan2f(pts[3] - pts[0], pts[5] - pts[2]) + 3.14159265f, speed = 0, dt = 1.0f / 30, dist = 0;
    int next = 1, pivot = 0, pivotFrames = 0, t; float worst = 0, offSum = 0; int offN = 0;
    for (t = 0; t < 30 * 1200 && next < npts; t++) {
        float fwd[3] = {sinf(heading), 0, cosf(heading)}, aim[3];
        next = bm_route_progress(pts, npts, next, pos, fwd, 6.0f); if (next >= npts) break;
        bm_route_aim(pts, npts, next, pos, speed, aim);
        float err = wrapf(atan2f(aim[0] - pos[0], aim[2] - pos[2]) - heading), ae = fabsf(err), vwant = 16;
        if (ae > 1.05f) vwant = 6;
        if (ae > 1.6f) vwant = 4;
        float along = sqrtf(d2(&pts[next * 3], pos));
        for (int k = next; k < npts && along < 200; k++) {
            float *w = &pts[k * 3], vt;
            if (k == npts - 1) vt = 4;
            else { int kb = k, kf = k + 1; while (kb > 0 && d2(&pts[kb * 3], w) < 625) kb--; while (kf < npts - 1 && d2(&pts[kf * 3], w) < 625) kf++;
                   vt = bm_route_bend_limit(kb < k ? &pts[kb * 3] : NULL, w, &pts[kf * 3], 0, 1); }
            float v = sqrtf(vt * vt + 2 * 4 * fmaxf(0, along - 6)); if (v < vwant) vwant = v;
            if (k + 1 < npts) along += sqrtf(d2(&pts[(k + 1) * 3], w));
        }
        if (pivot && (ae < 0.35f || speed > 4)) pivot = 0;
        if ((ae > 1.2f || pivot) && speed < 3) { pivot = 1; vwant = 0; pivotFrames++; }
        float rate = fmaxf(-0.7f, fminf(0.7f, err * 2.0f));
        heading = wrapf(heading + rate * dt);
        speed += vwant > speed ? fminf(3 * dt, vwant - speed) : -fminf(6 * dt, speed - vwant);
        pos[0] += sinf(heading) * speed * dt; pos[2] += cosf(heading) * speed * dt; dist += speed * dt;
        if (t % 15 == 0 && t > 10 * 30) { float o = off_route(pos); offSum += o; offN++; if (o > worst) worst = o; }
    }
    float avg = dist / (t * dt), piv = 100.0f * pivotFrames / t;
    printf("tank on %s (%.0f m): finished %d, average %.1f m/s, pivoting %.1f%% of the time (%.1f s), off the road %.1f m on average, %.1f m at most\n", strrchr(argv[1], '/') ? strrchr(argv[1], '/') + 1 : argv[1], len, next >= npts, avg, piv, pivotFrames / 30.0f, offN ? offSum / offN : 0, worst);
    assert(next >= npts && avg > 9.0f && pivotFrames < 8 * 30 && offSum / (offN ? offN : 1) < 1.0f);   /* (V28: on the road - under 1 m off it on average; the worst is a hairpin taken wide) */   /* (the one turn round at the start, then no more) */
    return 0;
}
