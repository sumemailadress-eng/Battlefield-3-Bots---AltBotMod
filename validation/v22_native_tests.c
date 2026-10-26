// AltBotMod V22 - Sharp Shooters: pure parts. 1. bend speed from the real curve (a recorded road's jitter is no bend);
// 2. an aircraft's heat seeker is held selected from the first moment on the nose and fired after the lock time.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "enhanced.h"
#include "air.c"
int main(void)
{
    /* 1. a straight road recorded by hand: a point every 3 m, +-0.6 m sideways jitter */
    float pts[60][3]; unsigned r = 12345;
    for (int k = 0; k < 60; k++) { r = r * 1103515245u + 12345u; pts[k][0] = ((int)(r >> 16) % 121 - 60) / 100.0f; pts[k][1] = 0; pts[k][2] = k * 3.0f; }
    float slowest = 1e9f, oldSlowest = 1e9f; int oldSlow = 0;
    for (int k = 4; k < 56; k++) {
        float v = bm_bend_speed(pts[k - 4], pts[k], pts[k + 4], 0, 0);   /* ~10 m either side, as the driver measures it */
        if (v < slowest) slowest = v;
        float a1 = atan2f(pts[k][0] - pts[k - 1][0], pts[k][2] - pts[k - 1][2]), a2 = atan2f(pts[k + 1][0] - pts[k][0], pts[k + 1][2] - pts[k][2]), t = fabsf(a2 - a1);
        float old = t > 1.05f ? 5.0f : t > 0.6f ? 8.5f : t > 0.3f ? 13.0f : 99.0f;   /* V8-V21.1: neighbouring segments */
        if (old < oldSlowest) oldSlowest = old;
        if (old < 99) oldSlow++;
    }
    assert(oldSlowest <= 13.0f && oldSlow > 10);   /* the old rule braked a dozen times on a straight */
    assert(slowest >= 25.0f);                       /* the new one: flat out (a jeep's limit is 34 m/s) */
    /* a bend of 30 m radius, sampled 10 m either side: sqrt(grip x 30) - a jeep ~14 m/s, a tank slower */
    float R = 30, th = 10 / R, b[3] = {R - R * cosf(-th), 0, R * sinf(-th)}, w[3] = {0, 0, 0}, f[3] = {R - R * cosf(th), 0, R * sinf(th)};
    float corner = bm_bend_speed(b, w, f, 0, 0), tank = bm_bend_speed(b, w, f, 0, 1);
    assert(fabsf(corner - sqrtf(6.5f * R)) < 1.0f && fabsf(tank - sqrtf(4.0f * R)) < 1.0f);
    float jb[3] = {0, 0, -10}, jf[3] = {10, 0, 0}; assert(bm_bend_speed(jb, w, jf, 0, 0) <= 5.0f);   /* a junction turn (90 degrees) */
    printf("PASS: V22 bend speed - a hand-recorded straight road (3 m points, +-0.6 m jitter) drives at %.0f m/s at its slowest (V21.1: braked to %.0f m/s %d times), a 30 m-radius bend %.1f m/s (tank %.1f), a junction turn 5\n",
           slowest, oldSlowest, oldSlow, corner, tank);
    /* 2. heat seeker lock */
    BMAirController s; memset(&s, 0, sizeof s); BMAirOutput o; float dt = 1.0f / 30; int armedFrom = -1, firedAt = -1;
    for (int k = 0; k < 120; k++) {
        memset(&o, 0, sizeof o);
        air_secondary(&s, 1, dt, &o);
        if (o.armSecondary && armedFrom < 0) armedFrom = k;
        if (o.secondary) { firedAt = k; break; }
    }
    assert(armedFrom == 0 && firedAt >= (int)(AIR_LOCK_TIME * 30) - 2 && firedAt <= (int)(AIR_LOCK_TIME * 30) + 2);
    memset(&o, 0, sizeof o); air_secondary(&s, 1, dt, &o); assert(!o.secondary && !o.armSecondary);   /* 10 s before the next */
    memset(&s, 0, sizeof s); for (int k = 0; k < 30; k++) { memset(&o, 0, sizeof o); air_secondary(&s, 1, dt, &o); }
    for (int k = 0; k < 20; k++) { memset(&o, 0, sizeof o); air_secondary(&s, 0, dt, &o); }
    assert(!o.armSecondary && !o.secondary);   /* off the nose: the lock fades and the guns come back */
    printf("PASS: V22 aircraft missiles - selected from the first frame on the nose, fired after %.1f s of lock (frame %d), guns back when the target leaves the nose\n", AIR_LOCK_TIME, firedAt);
    return 0;
}
