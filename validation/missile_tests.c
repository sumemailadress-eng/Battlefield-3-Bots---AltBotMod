// AltBotMod V23: missiles_native.inc against fake missiles in memory - the transform offset is found from two reads of a
// missile in flight (decoys around it: a still matrix, a non-orthonormal one), a bot's own launch is recognised, and the
// bot whose vehicle / aircraft a missile is heading for pops flares / smoke (not for its own launch, not for one going
// elsewhere, not twice in 6 s; a human's vehicle is left to the human).
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#define MAX_BOTS 8
typedef void *HANDLE; typedef unsigned long DWORD; typedef long LONG; typedef void *LPVOID; typedef size_t SIZE_T;
#define WINAPI
static HANDLE GetCurrentProcess(void) { return 0; }
static int ReadProcessMemory(HANDLE h, const void *a, void *b, SIZE_T n, SIZE_T *got) { (void)h; memcpy(b, a, n); *got = n; return 1; }
static int IsBadReadPtr(const void *p, SIZE_T n) { (void)n; return !p; }
static LONG InterlockedExchange(volatile LONG *p, LONG v) { LONG o = *p; *p = v; return o; }
static LONG InterlockedIncrement(volatile LONG *p) { return ++*p; }
static HANDLE CreateThread(void *a, int b, DWORD (*f)(LPVOID), void *c, int d, void *e) { (void)a; (void)b; (void)f; (void)c; (void)d; (void)e; return 0; }
static DWORD WaitForSingleObject(HANDLE h, DWORD t) { (void)h; (void)t; return 0; }
static int CloseHandle(HANDLE h) { (void)h; return 1; }
#define WAIT_TIMEOUT 258
static float *moving; static void Sleep(DWORD ms) { (void)ms; if (moving) moving[14] += 20.0f; }   /* the missile flies 20 m on during the 150 ms */
#define OFF_PLAYER_ONLINE_NAME 0
#define OFF_VEHICLE_HEALTH 4
#define EIA_AIR_COUNTERMEASURE 10
#include "enhanced.h"
static struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; } host, *H = &host;
static BMConfig g_enhanced; static int g_roundCleanup, levelReady = 1;
static int level_ready(void) { return levelReady; }
static float dist2(const float *a, const float *b) { float x = a[0] - b[0], z = a[2] - b[2]; return x * x + z * z; }
static int scan_objects(unsigned vt, char **out, int max) { (void)vt; (void)out; (void)max; return 0; }
static char logline[512]; static int logs;
static void bot_log(const char *f, ...) { va_list a; va_start(a, f); vsnprintf(logline, sizeof logline, f, a); va_end(a); logs++; }
typedef struct { int phase; char *veh; unsigned counterAt, counterUntil; } AirLite;
static AirLite g_air[MAX_BOTS];
static int pressed[MAX_BOTS]; static void schedule_press(int i, int a, unsigned at) { (void)at; if (a == EIA_AIR_COUNTERMEASURE) pressed[i]++; }
/* vehicles: a 0x60-byte block, seats are separate blocks with the occupant at +0x4c */
typedef struct { char body[0x60]; float pos[3]; const char *name; char *seat[3]; int nseat; } FakeVeh;
static FakeVeh vehs[3]; static char *vehList[3]; static char seatMem[9][0x60]; static char players[MAX_BOTS + 1][16];
static int vehicle_list(char ***out) { *out = vehList; return 3; }
static int vehicle_ok(char *v) { return v != NULL; }
static int vehicle_occupied(char *v) { FakeVeh *f = (FakeVeh *)v; for (int s = 0; s < f->nseat; s++) if (*(char **)(f->seat[s] + 0x4c)) return 1; return 0; }
static int stationary_weapon(char *v) { (void)v; return 0; }
static float *vehicle_pos(char *v, int *team) { if (team) *team = 0; return ((FakeVeh *)v)->pos; }
static int vehicle_seats(char *v, char ***out) { *out = ((FakeVeh *)v)->seat; return ((FakeVeh *)v)->nseat; }
static const char *vehicle_name(char *v) { return ((FakeVeh *)v)->name; }
static int vehicle_is_gunship(char *v) { return !strcmp(((FakeVeh *)v)->name, "GUNSHIP"); }
static unsigned g_lockFirstAt[MAX_BOTS]; static char *player_vehicle(char *p) { (void)p; return 0; }
static int bot_index(void *pl) { for (int i = 0; i < MAX_BOTS; i++) if (pl == players[i]) return i; return -1; }
#include "missiles_native.inc"

static void set_xf(float *x, const float pos[3], const float fwd[3])
{
    /* forward f, up ~y, right = up x f */
    float l = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]), f[3] = {fwd[0] / l, fwd[1] / l, fwd[2] / l}, u0[3] = {0, 1, 0};
    if (fabsf(f[1]) > 0.9f) { u0[1] = 0; u0[2] = 1; }
    float r[3] = {u0[1] * f[2] - u0[2] * f[1], u0[2] * f[0] - u0[0] * f[2], u0[0] * f[1] - u0[1] * f[0]}, rl = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (int k = 0; k < 3; k++) r[k] /= rl;
    float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
    memset(x, 0, 64); memcpy(x, r, 12); memcpy(x + 4, u, 12); memcpy(x + 8, f, 12); memcpy(x + 12, pos, 12); x[15] = 1;
}
int main(void)
{
    g_enhanced.vehicleCountermeasures = 1; H->botCount = 3;
    for (int i = 0; i < MAX_BOTS; i++) H->bots[i] = players[i];
    /* a missile object: vtable, a still decoy matrix at +0x40 (looks like a transform), junk at +0x90, the real one at +0x130 */
    static char m1[0x400], m2[0x400];
    *(unsigned *)m1 = ADDR_MISSILE_VTABLE; *(unsigned *)m2 = ADDR_MISSILE_VTABLE;
    float decoyPos[3] = {100, 50, 100}, fwdN[3] = {0, 0, 1}, p1[3] = {5, 20, 0};
    set_xf((float *)(m1 + 0x40), decoyPos, fwdN); for (int k = 0; k < 16; k++) ((float *)(m1 + 0x90))[k] = 0.5f;
    set_xf((float *)(m1 + 0x130), p1, fwdN); memcpy(m2, m1, sizeof m1);
    char *list[2] = {m1, m2};
    moving = (float *)(m1 + 0x130);
    assert(missile_calibrate(list, 1) == -1);   /* one vote is not enough... */
    moving = (float *)(m1 + 0x130); assert(missile_calibrate(list, 1) == 0x130);   /* ...a second read of a missile in flight confirms it */
    moving = NULL; g_missileXfOff = 0x130;
    puts("PASS: missile transform found - the offset whose basis is orthonormal and whose position moves nose-first between two reads (a still matrix and junk beside it are not taken)");
    /* the scene: bot 0 drives a tank at (0,0,300); bot 1 flies a jet at (400,200,0); a human drives a jeep at (-300,0,0); bots 2 rides the gunship */
    const char *names[3] = {"T90", "Su35", "Growler"};
    for (int v = 0; v < 3; v++) { vehs[v].name = names[v]; vehs[v].nseat = 1; vehs[v].seat[0] = seatMem[v]; vehList[v] = (char *)&vehs[v]; }
    vehs[0].pos[2] = 300; vehs[1].pos[0] = 400; vehs[1].pos[1] = 200; vehs[2].pos[0] = -300;
    { char *o0 = players[0], *o1 = players[1], *o2 = players[MAX_BOTS]; memcpy(seatMem[0] + 0x4c, &o0, sizeof o0); memcpy(seatMem[1] + 0x4c, &o1, sizeof o1); memcpy(seatMem[2] + 0x4c, &o2, sizeof o2); }
    g_air[1].phase = 1; g_air[1].veh = (char *)&vehs[1];
    /* a missile launched at (0,1,100), heading for the tank 200 m off */
    float mp[3] = {0, 1, 100}, toTank[3] = {0, -0.005f, 1};
    set_xf((float *)(m1 + 0x130), mp, toTank);
    g_missileFound[0][0] = m1; g_missileFoundN[0] = 1; g_missileFoundCur = 0;
    H->frame = 300; missile_tick();
    assert(g_missileCount == 1 && pressed[0] == 1 && strstr(logline, "smoke") && strstr(logline, "T90"));
    assert(missile_launched_near(mp, 295) && !missile_launched_near(vehs[1].pos, 295));   /* a launch beside (0,1,100), not beside the jet */
    H->frame = 303; missile_tick(); assert(pressed[0] == 1);   /* not again within 6 s */
    puts("PASS: a missile heading for a bot's tank 200 m off: the driver pops smoke at once (once in 6 s); the launch is recognised beside its launcher");
    /* the same missile turned towards the jet, 300 m off: flares for the pilot */
    float mj[3] = {150, 80, 0}, toJet[3] = {400 - 150, 200 - 80, 0}; set_xf((float *)(m1 + 0x130), mj, toJet);   /* 277 m off */
    H->frame = 306; missile_tick(); assert(g_air[1].counterUntil == 309 && strstr(logline, "flares"));
    /* towards the human's jeep: left to the human; one pointing away from everything: nothing */
    float toJeep[3] = {-300, -1, -100}; set_xf((float *)(m1 + 0x130), mp, toJeep);
    int before = logs; H->frame = 600; missile_tick(); assert(logs == before && pressed[0] == 1);
    float up[3] = {0, 1, 0.01f}; set_xf((float *)(m1 + 0x130), mp, up); H->frame = 900; missile_tick(); assert(logs == before);
    /* the tank's own launch (first seen beside it) does not make it pop smoke */
    g_missileCount = 0; float atTank[3] = {0, 2, 290}, away[3] = {0, 0, -1}; set_xf((float *)(m1 + 0x130), atTank, away);
    H->frame = 1200; missile_tick(); float later[3] = {0, 2, 280}; set_xf((float *)(m1 + 0x130), later, toTank); H->frame = 1203; missile_tick(); assert(pressed[0] == 1);
    puts("PASS: flares for a bot pilot a missile is heading for; nothing for a human's vehicle, a missile pointing elsewhere, or the vehicle's own launch");
    return 0;
}
