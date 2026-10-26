// AltBotMod V29: Fun-Bots driving, simulated. A tank (+3 m/s^2 falling to 0 at 17.5 m/s, brakes 8 m/s^2, turns 0.7 rad/s,
// pivots) and a jeep (+4.5 m/s^2 to 30 m/s, turns on a 7 m radius) on the longest Fun-Bots land road of a map, driven as
// drive_tick does in V29: pure pursuit (bm_route_progress / bm_route_aim), the heading caps, the road's real bends
// (bm_route_bend_limit), bm_drive_throttle on the speed measured every 1/3 s - and, for comparison, the V28 way (the speed
// target raised 8 m/s per second after a dip, throttle 0.35 + 0.12 x the error).
// Then the same road with a vehicle stopped on it 40% of the way along: bm_lane_limit stops the driver behind it, the
// pass (after 2.5 s, aiming 1 m clear of its side) takes it round, and it drives on.
// Asserts (V29): it finishes; full throttle most of the time; braking only for real bends and the end; no "stop-go" (a
// fall of 3 m/s and back up within 4 s with no bend there); past the stopped vehicle without touching it.
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
typedef struct { int armored; float amax, vtop, brake, turnRate, turnRadius, vmax, halfWidth, frontLead; const char *name; } Car;
static const Car kTank = {1, 3.0f, 17.5f, 8.0f, 0.7f, 0.0f, 16.0f, 1.9f, 3.5f, "tank"};
static const Car kJeep = {0, 4.5f, 30.0f, 9.0f, 1.2f, 7.0f, 34.0f, 1.2f, 2.5f, "jeep"};
typedef struct { int finished; float avg, fullShare, minGap; int brakeStarts, stopGo, passes, frames; } Run;
// the bend limit at waypoint k (as drive_tick: +-25 m of the road itself)
static float bend_at(int k, const Car *c)
{
    float *w = &pts[k * 3];
    if (k == npts - 1) return 4.0f;
    int kb = k, kf = k + 1;
    while (kb > 0 && d2(&pts[kb * 3], w) < 625) kb--;
    while (kf < npts - 1 && d2(&pts[kf * 3], w) < 625) kf++;
    return bm_route_bend_limit(kb < k ? &pts[kb * 3] : NULL, w, &pts[kf * 3], 0, c->armored);
}
static Run drive(const Car *c, int v28, const float *blocker)
{
    Run r; memset(&r, 0, sizeof r); r.minGap = 1e9f;
    float pos[3] = {pts[0], pts[1], pts[2]}, heading = atan2f(pts[3] - pts[0], pts[5] - pts[2]), speed = 0, meas = 0, dt = 1.0f / 30, dist = 0;
    float spPos[3]; memcpy(spPos, pos, 12); int spAt = 0;
    float vwantS = 0; int holdUntil = 0, laneSince = -1, passUntil = -1, passSide = 1; float passLat = 0, lastThrottle = 0;
    int next = 1, t, pivot = 0;
    float hist[30 * 5]; int histN = 0; float why[30 * 5];   // speed and the bend limit there, the last 5 s
    int lastDipCheck = 0;
    for (t = 0; t < 30 * 1500 && next < npts; t++) {
        float fwd[3] = {sinf(heading), 0, cosf(heading)}, aim[3];
        next = bm_route_progress(pts, npts, next, pos, fwd, 6.0f); if (next >= npts) break;
        bm_route_aim(pts, npts, next, pos, meas, aim);
        if (passUntil > t) { aim[0] += cosf(heading) * passSide * passLat; aim[2] -= sinf(heading) * passSide * passLat; }
        float err = wrapf(atan2f(aim[0] - pos[0], aim[2] - pos[2]) - heading), ae = fabsf(err), vwant = c->vmax;
        if (ae > 1.05f) vwant = fminf(vwant, c->armored ? 6.0f : 9.0f);
        if (ae > 1.6f) vwant = fminf(vwant, 4.0f);
        float along = sqrtf(d2(&pts[next * 3], pos)), bendHere = vwant;   // (the heading caps are a reason too)
        for (int k = next; k < npts && along < 200; k++) {
            float vt = bend_at(k, c);
            if (k == next && vt < bendHere) bendHere = vt;
            float v = sqrtf(vt * vt + 2 * 4 * fmaxf(0, along - 6)); if (v < vwant) vwant = v;
            if (k + 1 < npts) along += sqrtf(d2(&pts[(k + 1) * 3], &pts[k * 3]));
        }
        // the vehicle stopped in the lane (V29 only: drive_lane_scan - bm_lane_relation / bm_lane_limit, the pass)
        if (blocker) {
            float ex = blocker[0] - pos[0], ez = blocker[2] - pos[2], gap = sqrtf(ex * ex + ez * ez);
            if (gap < r.minGap) r.minGap = gap;
            if (passUntil > t && ex * fwd[0] + ez * fwd[2] < -2.0f) passUntil = -1;
            float half = c->halfWidth + 1.9f + 0.8f, al = 0, lat = 0, f2[2] = {fwd[0], fwd[2]};
            if (bm_lane_relation(pts, npts, next, pos, f2, blocker, half, 25.0f + 2.5f * meas, &al, &lat)) {
                float lim = bm_lane_limit(al, 0.0f, c->frontLead);
                if (passUntil > t) lim = (fabsf(lat) < half && al < c->frontLead + 8.0f) ? 2.5f : fmaxf(lim, 6.0f);
                if (laneSince < 0) laneSince = t;
                if (t - laneSince > 45 && passUntil <= t) { passSide = lat > 0.5f ? -1 : 1; passLat = fmaxf(1.5f, half + 1.0f - fabsf(lat)); passUntil = t + 10 * 30; r.passes++; }
                if (lim < vwant) vwant = lim;
            } else laneSince = -1;
        }
        // the speed as drive_tick measures it: over 1/3 s
        if (t - spAt >= 10) { meas = sqrtf(d2(pos, spPos)) / ((t - spAt) / 30.0f); memcpy(spPos, pos, 12); spAt = t; }
        float throttle;
        if (v28) {   // V28: the target smoothed, partial throttle on the way back up
            if (vwant <= vwantS) { if (vwant < vwantS - 2.0f) holdUntil = t + 12; vwantS = vwant; }
            else if (t >= holdUntil) vwantS = fminf(vwant, vwantS + 8.0f * dt);
            float verr = vwantS - meas;
            if (verr > 1.0f) throttle = fminf(ae < 0.6f ? 1.0f : 0.6f, 0.35f + 0.12f * verr);
            else if (verr > -2.5f) throttle = fmaxf(0.0f, 0.25f + 0.1f * verr);
            else throttle = fmaxf(-0.7f, 0.12f * (verr + 1.5f));
            if (vwantS <= 0) throttle = meas > 0.5f ? -0.4f : 0.0f;
        } else throttle = bm_drive_throttle(vwant, meas, c->armored, ae);
        // tanks pivot (drive_tick V27)
        if (c->armored) {
            if (pivot && (ae < 0.35f || speed > 4)) pivot = 0;
            if ((ae > 1.2f || pivot) && meas < 3) { pivot = 1; throttle = 0; }
        }
        if (throttle < 0 && lastThrottle >= 0 && speed > 2) r.brakeStarts++;
        lastThrottle = throttle;
        if (throttle >= 0.95f) r.fullShare += 1;
        // the vehicle
        float rate = c->armored ? fmaxf(-c->turnRate, fminf(c->turnRate, err * 2.0f))
                                : fmaxf(-c->turnRate, fminf(c->turnRate, err * 2.0f)) * fminf(1.0f, speed / (c->turnRadius * c->turnRate));
        heading = wrapf(heading + rate * dt);
        float acc = throttle > 0 ? throttle * c->amax * fmaxf(0.0f, 1.0f - speed / c->vtop) - 0.3f : throttle < 0 ? throttle * c->brake : -0.8f;
        speed = fmaxf(0.0f, speed + acc * dt);
        pos[0] += sinf(heading) * speed * dt; pos[2] += cosf(heading) * speed * dt; dist += speed * dt;
        // stop-go: down 3 m/s and back up 3 m/s within 4 s, where the road has no bend under 1.5x the speed
        if (histN < 150) { hist[histN] = speed; why[histN] = bendHere; histN++; }
        else { memmove(hist, hist + 1, sizeof(float) * 149); memmove(why, why + 1, sizeof(float) * 149); hist[149] = speed; why[149] = bendHere; }
        if (histN == 150 && t - lastDipCheck > 120 && t > 15 * 30) {
            float hi0 = 0, lo = 1e9f, hi1 = 0; int lok = -1, bend = 0;
            for (int k = 0; k < 120; k++) { if (hist[k] > hi0 && lok < 0) hi0 = hist[k]; if (hist[k] < lo) { lo = hist[k]; lok = k; } }
            for (int k = lok; k < 150; k++) if (hist[k] > hi1) hi1 = hist[k];
            for (int k = 0; k < 150; k++) if (why[k] < 1.5f * hi0) bend = 1;
            for (int k = 0; k < lok; k++) if (hist[k] > hi0) hi0 = hist[k];
            if (hi0 - lo > 3.0f && hi1 - lo > 3.0f && !bend && !blocker) { r.stopGo++; lastDipCheck = t; }
        }
    }
    r.finished = next >= npts; r.frames = t; r.avg = dist / (t * dt); r.fullShare = 100.0f * r.fullShare / (t ? t : 1);
    return r;
}
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    assert(argc > 1 && load_longest_land(argv[1]) > 50);
    float len = 0; for (int k = 1; k < npts; k++) len += sqrtf(d2(&pts[k * 3], &pts[(k - 1) * 3]));
    const char *map = strrchr(argv[1], '/') ? strrchr(argv[1], '/') + 1 : argv[1];
    const Car *cars[2] = {&kTank, &kJeep};
    for (int ci = 0; ci < 2; ci++) {
        const Car *c = cars[ci];
        Run old = drive(c, 1, NULL), now = drive(c, 0, NULL);
        // a vehicle stopped on the road 40% of the way along
        float acc = 0, block[3]; int kb = 1;
        for (; kb < npts && acc < 0.4f * len; kb++) acc += sqrtf(d2(&pts[kb * 3], &pts[(kb - 1) * 3]));
        memcpy(block, &pts[kb * 3], 12);
        Run lane = drive(c, 0, block);
        printf("%s on %s (%.0f m): V29 average %.1f m/s (V28 way %.1f), full throttle %.0f%% (V28 way %.0f%%), %d brake application(s) (V28 way %d), stop-go %d (V28 way %d); "
               "stopped vehicle on the road: passed %d time(s), %.1f m apart at the closest, finished %d\n",
               c->name, map, len, now.avg, old.avg, now.fullShare, old.fullShare, now.brakeStarts, old.brakeStarts, now.stopGo, old.stopGo, lane.passes, lane.minGap, lane.finished);
        assert(now.finished && lane.finished);
        assert(now.stopGo == 0);
        assert(now.avg >= old.avg - 0.3f);
        assert(now.fullShare > 45.0f);
        assert(now.brakeStarts <= (int)(len / 60.0f) + 4);   // (real bends only)
        assert(lane.passes >= 1 && lane.passes <= 3 && lane.minGap > 2.2f);
    }
    return 0;
}
