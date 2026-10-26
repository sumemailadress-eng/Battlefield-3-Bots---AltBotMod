// AltBotMod V24: driving a real Fun-Bots land route (the longest "land" path of a route file) with a simple car - turn
// radius 6 m, 5 m/s^2 up, 8 m/s^2 down, speed limit 34 m/s (a jeep) - steered the V6-V23 way (reach every waypoint;
// within 1.75 m at any 25-deg "corner") and the V24 way (pure pursuit: bm_route_progress / bm_route_aim). The speed target
// is the driver's own: the bend speeds ahead (bm_bend_speed) and its heading-error caps. Measured: average speed, how
// often the speed drops by 3 m/s or more ("slow down and go"), and how far the car strays from the recorded road.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
        if (path != curPath) {
            if (curLand && curN > npts) { memcpy(pts, cur, sizeof(float) * 3 * curN); npts = curN; }
            curPath = path; curN = 0; curLand = 0;
        }
        if (strstr(line, "\"land\"")) curLand = 1;
        if (curN < MAXP) { cur[curN * 3] = x; cur[curN * 3 + 1] = y; cur[curN * 3 + 2] = z; curN++; }
    }
    if (curLand && curN > npts) { memcpy(pts, cur, sizeof(float) * 3 * curN); npts = curN; }
    fclose(f);
    return npts;
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
typedef struct { float avg, worstOff; int drops, finished, pointless; } Result;
/* a real bend within `range` m from route point k on: the road turning more than 41 deg over +-25 m (radius < 35 m) */
static int real_bend_ahead(int k, float range)
{
    float along = 0;
    for (int j = k; j < npts - 1 && along < range; j++) {
        int kb = j, kf = j;
        while (kb > 0 && d2(&pts[kb * 3], &pts[j * 3]) < 625) kb--;
        while (kf < npts - 1 && d2(&pts[kf * 3], &pts[j * 3]) < 625) kf++;
        if (kb < j && kf > j) {
            float a1 = atan2f(pts[j * 3] - pts[kb * 3], pts[j * 3 + 2] - pts[kb * 3 + 2]), a2 = atan2f(pts[kf * 3] - pts[j * 3], pts[kf * 3 + 2] - pts[j * 3 + 2]);
            float t = a2 - a1; while (t > 3.14159265f) t -= 6.2831853f; while (t < -3.14159265f) t += 6.2831853f;
            if (fabsf(t) > 0.71f) return 1;
        }
        along += sqrtf(d2(&pts[(j + 1) * 3], &pts[j * 3]));
    }
    return 0;
}
static Result drive(int v24)
{
    float pos[3] = {pts[0], pts[1], pts[2]}, heading = atan2f(pts[3] - pts[0], pts[5] - pts[2]), speed = 0, dt = 1.0f / 30;
    int next = 1; float peak = 0, dist = 0, worst = 0; int drops = 0, pointless = 0, inDrop = 0, t; float trough = 0;
    for (t = 0; t < 30 * 600 && next < npts; t++) {
        float fwd[3] = {sinf(heading), 0, cosf(heading)}, aim[3];
        if (v24) {
            next = bm_route_progress(pts, npts, next, pos, fwd, 6.0f);
            if (next >= npts) break;
            bm_route_aim(pts, npts, next, pos, speed, aim);
        } else {   // V6-V23
            while (next < npts) {
                float *w = &pts[next * 3], dx = w[0] - pos[0], dz = w[2] - pos[2], dd = dx * dx + dz * dz, wr = 6.0f;
                int behind = dd < 225 && dx * fwd[0] + dz * fwd[2] < 0;
                if (next + 1 < npts) {
                    float *a = &pts[(next + 1) * 3], ax = a[0] - w[0], az = a[2] - w[2], sc = hypotf(dx, dz) * hypotf(ax, az);
                    if (sc > 0.01f && (dx * ax + dz * az) / sc < 0.9f) { wr = 1.75f; behind = dd < 144 && dx * ax + dz * az < 0; }
                }
                if (dd < wr * wr || behind) next++; else break;
            }
            if (next >= npts) break;
            memcpy(aim, &pts[next * 3], 12);
        }
        float err = wrapf(atan2f(aim[0] - pos[0], aim[2] - pos[2]) - heading), ae = fabsf(err);
        // the driver's speed target (drive_tick): limit, heading-error caps, bends ahead
        float vwant = 34;
        int why = 0, whyK = -1; float whyAt = 0, whyVt = 0;
        if (!v24) {   /* V6-V24 heading caps */
            if (ae > 0.55f) { vwant = fminf(vwant, 12.0f); why = 1; }
            if (ae > 1.05f) { vwant = fminf(vwant, 3.5f); why = 1; }
        } else {      /* V25: only hard steering */
            if (ae > 1.05f) { vwant = fminf(vwant, 9.0f); why = 1; }
            if (ae > 1.6f) { vwant = fminf(vwant, 4.0f); why = 1; }
        }
        float along = sqrtf(d2(&pts[next * 3], pos));
        for (int k = next; k < npts && along < 200; k++) {
            float *w = &pts[k * 3], vt;
            if (k == npts - 1) vt = 4;
            else {
                int kb = k, kf = k + 1;
                float arm = v24 ? 625 : 100;   /* V24: +-25 m; V22-V23: +-10 m */
                while (kb > (v24 ? 0 : next) && d2(&pts[kb * 3], w) < arm) kb--;
                while (kf < npts - 1 && d2(&pts[kf * 3], w) < arm) kf++;
                vt = v24 ? bm_route_bend_limit(kb < k ? &pts[kb * 3] : NULL, w, &pts[kf * 3], 0, 0) : bm_bend_speed(kb < k ? &pts[kb * 3] : pos, w, &pts[kf * 3], 0, 0);
            }
            float lead = fmaxf(0, along - 6), v = sqrtf(vt * vt + 2 * 4 * lead);
            if (v < vwant) { vwant = v; why = k == npts - 1 ? 3 : 2; whyAt = along; whyVt = vt; whyK = k; }
            if (k + 1 < npts) along += sqrtf(d2(&pts[(k + 1) * 3], w));
        }
        // the car: turn rate limited by a 6 m radius (and a quick steering response), speed toward the target
        float maxRate = fmaxf(0.6f, speed / 6.0f), rate = fmaxf(-maxRate, fminf(maxRate, err * 4.0f));
        heading = wrapf(heading + rate * dt);
        speed += vwant > speed ? fminf(5 * dt, vwant - speed) : -fminf(8 * dt, speed - vwant);
        pos[0] += sinf(heading) * speed * dt; pos[2] += cosf(heading) * speed * dt; dist += speed * dt;
        /* one slow-down = one "slow down and go": speed falls 3+ m/s below its peak, then picks up 3+ m/s again */
        if (inDrop) { if (speed < trough) trough = speed; if (speed > trough + 3.0f) { inDrop = 0; peak = speed; } }
        else if (speed > peak) peak = speed;
        if (!inDrop && peak - speed >= 3.0f) { drops++; inDrop = 1; trough = speed; if (why == 1 || (why == 2 && !real_bend_ahead(whyK > 3 ? whyK - 3 : 0, 8))) { pointless++; if (getenv("WHY")) fprintf(stderr, "v%d why %d err %.2f speed %.1f bend at %.0f m vt %.1f\n", v24, why, ae, speed, whyAt, whyVt); } }
        if (t % 15 == 0) { float o = off_route(pos); if (o > worst) worst = o; }
    }
    Result r = {dist / (t * dt), worst, drops, next >= npts, pointless};
    return r;
}
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *file = argc > 1 ? argv[1] : "XP3_Alborz_ConquestLarge0.map";
    assert(load_longest_land(file) > 50);
    float len = 0; for (int k = 1; k < npts; k++) len += sqrtf(d2(&pts[k * 3], &pts[(k - 1) * 3]));
    Result old = drive(0), now = drive(1);
    printf("route: %d points, %.0f m (the longest land path of %s)\n", npts, len, strrchr(file, '/') ? strrchr(file, '/') + 1 : file);
    printf("V6-V23: avg %.1f m/s, %d slow-down-and-go of 3+ m/s, %d of them pointless (no real bend / the end to slow for); strays up to %.1f m\n", old.avg, old.drops, old.pointless, old.worstOff);
    printf("V24:    avg %.1f m/s, %d slow-down-and-go of 3+ m/s, %d of them pointless (no real bend / the end to slow for); strays up to %.1f m\n", now.avg, now.drops, now.pointless, now.worstOff);
    assert(now.finished && now.worstOff < 6.0f && now.avg > old.avg);
    assert(now.pointless * 5 <= old.pointless + 2);   /* the pointless ones (the stop-go) go */
    printf("PASS: V24 driving a real Fun-Bots road (%s, %.0f m): pointless slow-downs (no real bend or the end to slow for) %d -> %d, average %.1f -> %.1f m/s, never more than %.1f m off the road\n",
           strrchr(file, '/') ? strrchr(file, '/') + 1 : file, len, old.pointless, now.pointless, old.avg, now.avg, now.worstOff);
    return 0;
}
