#!/usr/bin/env python3
"""AltBotMod V16 - Full Arsenal: offline checks.

Native code compiled with stubs (the real functions, extracted from core.c / the .inc files):
1. Passengers (passenger_native.inc): a nearly wrecked vehicle empties (parachute from aircraft), the gunner seat stays,
   cargo of a human driver dismounts at an objective still to take / after a long halt, a bot driver on a job keeps
   its cargo, a human pilot's cargo jumps over an objective. Tank smoke when hit, once per 25 s.
2. C4 (c4_tick): run up, two charges 28 frames apart, back off 15 m, detonate (ADS), end.
3. Lock-on launchers (lockon_tick): the launcher comes out, ADS held, fires only after 2.2 s on target; a hidden
   aircraft ends the attempt; out of range ends it.
4. flag_pos / flag_owner: any index is safe (V15 Rush crash on server 3: an M-COM index into the empty flag table).
5. boat_trip_needed: a boat only when the objective is on another landmass.
Source checks: the spawn team field, Rush crew spawns, out-of-bounds spawn rest, nav grid borrowing, the version.
"""
from pathlib import Path
import argparse, re, subprocess, sys, tempfile
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test4_checks import function  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'

PASSENGER = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "enhanced.h"
#include "air.h"
#define MAX_BOTS 8
#define OFF_PLAYER_ONLINE_NAME 0
#define OFF_VEHICLE_HEALTH 64
#define EIA_AIR_COUNTERMEASURE 10
enum { TASK_NONE, TASK_FOLLOW, TASK_DEFEND };
static struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; } host, *H = &host;
static BMConfig g_enhanced;
typedef struct VehTrack { char *veh; float pos[3]; unsigned at; float speed; float maxSeen; } VehTrack;
typedef struct { char name[32]; char pad[32]; float health; float pos[3]; char *seat[6]; VehTrack track; } Veh;
static Veh jeep, heli, tank, boat;
static char players[MAX_BOTS][64], human[64];
static int teams[MAX_BOTS], exits[MAX_BOTS], chutes[MAX_BOTS], objectivesChosen, presses, pressAction;
static unsigned g_driving[MAX_BOTS];
static struct { int phase; } g_drive[MAX_BOTS], g_air[MAX_BOTS];
static struct { unsigned retryAt; } g_fleet[MAX_BOTS];
static struct { int task; } brains[MAX_BOTS];
static float flags[2][3]; static int wanted[2];
static void bot_log(const char *f, ...) { (void)f; }
static const char *vehicle_name(char *v) { return ((Veh *)v)->name; }
static int bot_index(char *p) { for (int i = 0; i < H->botCount; i++) if (H->bots[i] == p) return i; return -1; }
static int is_bot(char *p) { return bot_index(p) >= 0; }
static int player_team(char *p) { int i = bot_index(p); return i >= 0 ? teams[i] : 1; }
static int vehicle_seat_of(char *v, char *p) { for (int k = 0; k < 6; k++) if (((Veh *)v)->seat[k] == p) return k; return -1; }
static char *vehicle_occupant(char *v) { return ((Veh *)v)->seat[0]; }
static char *player_vehicle(char *p) { Veh *all[] = {&jeep, &heli, &tank, &boat}; for (int k = 0; k < 4; k++) if (vehicle_seat_of((char *)all[k], p) >= 0) return (char *)all[k]; return NULL; }
static void bot_exit_vehicle(int i, int parachute) { char *v = player_vehicle(H->bots[i]); if (v) ((Veh *)v)->seat[vehicle_seat_of(v, H->bots[i])] = NULL; exits[i]++; chutes[i] += parachute; }
static void gunner_reset(int i) { (void)i; }
static void choose_local_objective(int i, const char *why) { (void)i; (void)why; objectivesChosen++; }
static int stationary_weapon(char *v) { (void)v; return 0; }
static VehTrack *vehicle_sample(char *v) { return &((Veh *)v)->track; }
static float *vehicle_pos(char *v, int *t) { (void)t; return ((Veh *)v)->pos; }
static int objective_count(void) { return 2; }
static int objective_wanted(int team, int f) { (void)team; return wanted[f]; }
static const float *objective_pos(int f) { return flags[f]; }
static int vehicle_is_boat(char *v) { return v == (char *)&boat; }
static typeof(brains[0]) *bot_brain(int i) { return &brains[i]; }
static void schedule_press(int i, int a, unsigned at) { (void)i; (void)at; presses++; pressAction = a; }
#include "passenger_native.inc"
static void seat(Veh *v, int k, char *p) { v->seat[k] = p; }
int main(void)
{
    bm_defaults(&g_enhanced);
    H->botCount = 4; for (int i = 0; i < 4; i++) { H->bots[i] = players[i]; teams[i] = 1; }
    strcpy(jeep.name, "Humvee"); strcpy(heli.name, "UH-1Y"); strcpy(tank.name, "M1Abrams"); strcpy(boat.name, "RHIB");
    Veh *all[] = {&jeep, &heli, &tank, &boat};
    for (int k = 0; k < 4; k++) { all[k]->health = 1000; all[k]->track.maxSeen = 1000; }
    flags[0][0] = 0; flags[1][0] = 800; wanted[0] = 1; wanted[1] = 1;
    /* a human drives the jeep; bot 1 is the gunner, bot 2 cargo */
    seat(&jeep, 0, human); seat(&jeep, 1, players[1]); seat(&jeep, 2, players[2]);
    jeep.pos[0] = 300; jeep.track.speed = 12; H->frame = 1000;
    assert(!passenger_dropout_tick(2, (char *)&jeep) && !passenger_dropout_tick(1, (char *)&jeep));     /* moving: ride */
    jeep.track.speed = 0; jeep.pos[0] = 20;                                                              /* stops at flag 0 */
    assert(!passenger_dropout_tick(2, (char *)&jeep)); H->frame += 3 * 30; assert(!passenger_dropout_tick(2, (char *)&jeep));
    H->frame += 2 * 30; assert(passenger_dropout_tick(2, (char *)&jeep) && exits[2] == 1 && !chutes[2] && g_fleet[2].retryAt == H->frame + 20 * 30);
    assert(!passenger_dropout_tick(1, (char *)&jeep));                                                   /* the gunner stays */
    /* halted away from any objective: cargo out after 25 s, not before */
    seat(&jeep, 3, players[3]); jeep.pos[0] = 400; passenger_dropout_tick(3, (char *)&jeep);
    H->frame += 24 * 30; assert(!passenger_dropout_tick(3, (char *)&jeep)); H->frame += 2 * 30; assert(passenger_dropout_tick(3, (char *)&jeep) && exits[3] == 1);
    /* a captured flag (not wanted) is no reason to get out at once */
    wanted[0] = 0; seat(&jeep, 3, players[3]); jeep.pos[0] = 10; passenger_dropout_tick(3, (char *)&jeep); H->frame += 5 * 30; assert(!passenger_dropout_tick(3, (char *)&jeep)); wanted[0] = 1;
    /* a bot driver on a job delivers its cargo itself */
    seat(&jeep, 0, players[0]); g_drive[0].phase = 3; H->frame += 60 * 30; assert(!passenger_dropout_tick(3, (char *)&jeep));
    /* nearly wrecked: everyone out, the gunner too */
    jeep.health = 120; assert(passenger_dropout_tick(1, (char *)&jeep) && exits[1] == 1); jeep.health = 1000;
    /* a human flies the transport over flag 1: cargo jumps with the chute, the door gunners (seats 1, 2) stay */
    memset(exits, 0, sizeof exits); seat(&heli, 0, human); seat(&heli, 2, players[1]); seat(&heli, 3, players[2]);
    heli.pos[0] = 760; heli.pos[1] = 60; heli.track.speed = 20;
    assert(!passenger_dropout_tick(1, (char *)&heli) && passenger_dropout_tick(2, (char *)&heli) && chutes[2] == 1);
    heli.pos[0] = 500; seat(&heli, 3, players[2]); assert(!passenger_dropout_tick(2, (char *)&heli));   /* not over it yet */
    /* boats wait longer before cargo gives up */
    seat(&boat, 0, human); seat(&boat, 2, players[3]); boat.pos[0] = 400; boat.track.speed = 0; exits[3] = 0;
    passenger_dropout_tick(3, (char *)&boat); H->frame += 30 * 30; assert(!passenger_dropout_tick(3, (char *)&boat));
    H->frame += 11 * 30; assert(passenger_dropout_tick(3, (char *)&boat) && exits[3] == 1);
    puts("PASS: passengers - cargo of a human driver dismounts 4 s after a stop at an objective still to take, after 25 s halted elsewhere (40 s in a boat), "
         "never while moving or from the gunner seat; a bot driver on a job keeps its cargo; a nearly wrecked vehicle empties; a human pilot's cargo jumps with the chute over an objective (door gunners stay)");
    /* smoke: a bot tank driver hit for 8 %+ pops smoke, once per 25 s; a scratch does not; jeeps have none */
    memset(jeep.seat, 0, sizeof jeep.seat); seat(&tank, 0, players[0]); H->frame = 20000; vehicle_countermeasure_tick(0);
    tank.health = 960; H->frame += 10; vehicle_countermeasure_tick(0); assert(!presses);
    tank.health = 700; H->frame += 10; vehicle_countermeasure_tick(0); assert(presses == 1 && pressAction == EIA_AIR_COUNTERMEASURE);
    tank.health = 500; H->frame += 10; vehicle_countermeasure_tick(0); assert(presses == 1);
    H->frame += 25 * 30; tank.health = 300; vehicle_countermeasure_tick(0); assert(presses == 2);
    g_enhanced.vehicleCountermeasures = 0; tank.health = 100; H->frame += 30 * 30; vehicle_countermeasure_tick(0); assert(presses == 2);
    puts("PASS: vehicle countermeasures - a bot tank / IFV driver pops smoke when a hit takes 8 % or more, once per 25 s; VehicleCountermeasures=0 turns it off");
    return 0;
}
'''

C4 = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define MAX_BOTS 4
#define OFF_PLAYER_ONLINE_NAME 0
#define OFF_SOLDIER_POSITION 0
#define EIA_FIRE 7
#define EIA_ZOOM 11
#define SLOT_ACTION(s) (16 + (s))
typedef struct { int step, supplySlot; unsigned stepAt; } BotBrain;
typedef struct VehTrack { float speed; } VehTrack;
static struct { unsigned frame; char *bots[MAX_BOTS]; float botMove[MAX_BOTS][4]; } host, *H = &host;
static struct { int mode; float x, z, targetYaw; } navs[MAX_BOTS];
static VehTrack track; static char name[8] = "bot";
static unsigned g_sprintUntil[MAX_BOTS]; static float g_aimPitch[MAX_BOTS], g_c4Away[MAX_BOTS][3];
static int g_rocketShots[MAX_BOTS], g_rocketsThisLife[MAX_BOTS], g_lastPlanDeferred, ended, plans, presses[64], lastPress;
static float dist2(const float *a, const float *b) { float x = a[0] - b[0], z = a[2] - b[2]; return x * x + z * z; }
static VehTrack *vehicle_sample(char *v) { (void)v; return &track; }
static int plan_budget_left(void) { return 1; }
static int plan_path(int i, char *s, float x, float y, float z) { (void)i; (void)s; (void)x; (void)y; (void)z; plans++; return 1; }
static typeof(navs[0]) *bot_nav(int i) { return &navs[i]; }
static void bot_stand(int i) { (void)i; }
static void press_button(int i, int a, int f) { (void)i; (void)f; presses[a]++; lastPress = a; }
static float yaw_towards(float dx, float dz) { return atan2f(dx, dz); }
static void bot_log(const char *f, ...) { (void)f; }
static const char *vehicle_name(char *v) { (void)v; return "T90"; }
static void end_rocket(int i, BotBrain *b, const char *why) { (void)i; (void)why; b->step = 0; ended++; }
'''

LOCK = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define MAX_BOTS 4
#define OFF_PLAYER_ONLINE_NAME 0
#define OFF_SOLDIER_YAW 0
#define OFF_SOLDIER_PITCH 4
#define EIA_FIRE 7
#define EIA_ZOOM 11
#define SLOT_ACTION(s) (16 + (s))
#define ROCKET_MAX_SHOTS 3
#define ROCKET_RELOAD_FRAMES 90
enum { LAUNCHER_NONE, LAUNCHER_AT, LAUNCHER_JAVELIN, LAUNCHER_AA };
#define LOCK_FRAMES (int)(2.2f * 30)
typedef struct { int step, supplySlot; unsigned stepAt; } BotBrain;
static struct { unsigned frame; char *bots[MAX_BOTS]; } host, *H = &host;
static struct { int mode; float x, z, targetYaw; } navs[MAX_BOTS];
static unsigned g_lockSince[MAX_BOTS]; static float g_aimPitch[MAX_BOTS];
static unsigned g_lockCheckAt[MAX_BOTS], g_lockFirstAt[MAX_BOTS], g_lockLaunches, g_lockNoLaunch; static int g_lockAmmo[MAX_BOTS];   /* V22 */
static unsigned char g_lockStrat[MAX_BOTS], g_lockTry[MAX_BOTS]; static unsigned g_lockWayWins[3], g_lockWayTries[3];   /* V23 */
static const char *const kLockWayNames[3] = {"no sight, fire taps", "sight, fire taps", "sight, fire held"};
/* V25: the game's own lock (core.c on_lock_update) - here a fake controller state */
static float g_lockDevSum, g_lockDevMax; static unsigned g_lockDevN;   /* V26 */
static unsigned char g_lockAttempt[MAX_BOTS], g_lockForced[MAX_BOTS]; static float g_lockBest[MAX_BOTS]; static unsigned g_lockTargetFrames[MAX_BOTS], g_lockSeenFrames[MAX_BOTS], g_lockNativeSince[MAX_BOTS], g_lockAttemptAt[MAX_BOTS], g_lockForcedShots;   /* V28 */
static int g_lockHooked, fakeLock, wants; static char *g_lockCtrl[MAX_BOTS]; static unsigned g_lockHookCalls, g_lockMatches, g_lockActivations, g_lockNativeLocks;
static void lock_want(int i, const float *a, const float *b, float r) { (void)i; (void)a; (void)b; (void)r; wants++; }
static void lock_want_ent(int i, const float *a, const float *b, float r, char *t) { (void)t; lock_want(i, a, b, r); }   /* V29 */
static int lock_state(int i, float *p) { (void)i; if (p) *p = fakeLock == 1 ? 0.5f : 1.0f; return fakeLock; }
static int vehicle_is_gunship(char *v) { (void)v; return 0; }
static int missileSeen; static int missile_launched_near(const float *at, unsigned since) { (void)at; (void)since; return missileSeen; }
static int ammoLoaded = 1, ammoSpare = 2;
static int g_rocketShots[MAX_BOTS], g_rocketsThisLife[MAX_BOTS], g_lastPlanDeferred, ended, presses[64], blocked, onGround;
static char reason[64];
static float dist2(const float *a, const float *b) { float x = a[0] - b[0], z = a[2] - b[2]; return x * x + z * z; }
static int plan_budget_left(void) { return 1; }
static int plan_path(int i, char *s, float x, float y, float z) { (void)i; (void)s; (void)x; (void)y; (void)z; return 1; }
static typeof(navs[0]) *bot_nav(int i) { return &navs[i]; }
static void bot_stand(int i) { (void)i; }
static void press_button(int i, int a, int f) { (void)i; (void)f; presses[a]++; }
static float yaw_towards(float dx, float dz) { return atan2f(dx, dz); }
static void bot_log(const char *f, ...) { (void)f; }
static const char *vehicle_name(char *v) { (void)v; return "Mi28"; }
static void end_rocket(int i, BotBrain *b, const char *why) { (void)i; b->step = 0; ended++; snprintf(reason, sizeof reason, "%s", why); }
static int vehicle_on_ground(const float *p) { (void)p; return onGround; }
static int ray_blocked_char(const float *a, const float *b, float *hit) { (void)a; hit[0] = b[0] + 50; hit[1] = b[1]; hit[2] = b[2]; return blocked; }
static int bot_ammo(int i, int s) { (void)i; (void)s; return 2; }
static int soldier_ammo2(char *so, int slot, int *l, int *sp) { (void)so; (void)slot; *l = ammoLoaded; *sp = ammoSpare; return 1; }
'''


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    print(r.stdout, end='')
    if r.returncode:
        print(r.stderr); raise SystemExit('FAIL: %s exited %d' % (Path(cmd[0]).name, r.returncode))
    return r.stdout


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    san = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g'] if a.sanitize else ['-O2']
    core = (SRC / 'core.c').read_text()
    cc = ['gcc', '-std=gnu11', '-Wall', '-Wno-unused-function', '-Wno-unused-variable', '-I', str(SRC)] + san
    with tempfile.TemporaryDirectory(prefix='bf3-altbotmod-v16-') as tmp:
        tmp = Path(tmp)
        def build(name, body, extra=()):
            c = tmp / (name + '.c'); c.write_text(body)
            subprocess.run(cc + [str(c), *[str(SRC / x) for x in extra], '-lm', '-o', str(tmp / name)], check=True)
            return run([str(tmp / name)])
        build('passenger', PASSENGER, ['enhanced.c', 'air.c', 'modes.c'])
        c4 = C4 + function(core, 'c4_tick') + r'''
int main(void)
{
    BotBrain b = {0}; b.supplySlot = 5; float me[3] = {0, 0, 0}, vp[3] = {20, 0, 0}; char *soldier = (char *)me; H->bots[0] = name;
    H->frame = 100; assert(c4_tick(0, &b, soldier, "T90", vp) && b.step == 0 && plans == 1 && navs[0].mode == 3);   /* runs up */
    me[0] = 17; H->frame += 5; assert(c4_tick(0, &b, soldier, "T90", vp) && b.step == 1 && lastPress == SLOT_ACTION(5));   /* at 3 m: C4 out */
    assert(c4_tick(0, &b, soldier, "T90", vp) && !presses[EIA_FIRE]);   /* (a second to take it out) */
    H->frame += 30; assert(c4_tick(0, &b, soldier, "T90", vp) && b.step == 2 && presses[EIA_FIRE] == 1);
    H->frame += 27; c4_tick(0, &b, soldier, "T90", vp); assert(presses[EIA_FIRE] == 1);
    H->frame += 1; assert(c4_tick(0, &b, soldier, "T90", vp) && b.step == 3 && presses[EIA_FIRE] == 2 && g_rocketShots[0] == 2);
    assert(navs[0].mode == 2 && g_c4Away[0][0] < 3 && fabsf(g_c4Away[0][0] - 2) < 0.01f);   /* backs off 15 m, away from the tank */
    H->frame += 3 * 30; assert(c4_tick(0, &b, soldier, "T90", vp) && b.step == 3 && !presses[EIA_ZOOM]);   /* still 3 m away: keep going */
    me[0] = 2; assert(c4_tick(0, &b, soldier, "T90", vp) && b.step == 4 && presses[EIA_ZOOM] == 1);         /* 18 m: set off */
    H->frame += 20; assert(!c4_tick(0, &b, soldier, "T90", vp) && ended == 1);
    b.step = 0; me[0] = 0; track.speed = 8; assert(!c4_tick(0, &b, soldier, "T90", vp) && ended == 2);   /* a fast vehicle: no */
    puts("PASS: C4 - an engineer / support runs up to a slow enemy vehicle, takes the C4 out, throws two charges 28 frames apart, backs off 15 m on the far side and sets them off (ADS / detonator); not at a vehicle driving off");
    return 0;
}
'''
        build('c4', c4)
        lock = LOCK + function(core, 'lock_pick_way') + function(core, 'lockon_tick') + r'''
int main(void)
{
    BotBrain b = {0}; b.supplySlot = 2; float look[2] = {0, 0}; char *soldier = (char *)look; H->bots[0] = (char *)"bot";
    float eye[3] = {0, 1.6f, 0}, vp[3] = {0, 60, 300};
    H->frame = 100; assert(lockon_tick(0, &b, soldier, "Mi28", vp, eye, LAUNCHER_AA) && b.step == 1 && presses[SLOT_ACTION(2)] == 1);
    look[0] = navs[0].targetYaw > 0 ? navs[0].targetYaw - 6.2831853f : navs[0].targetYaw; look[1] = g_aimPitch[0];
    /* V23 way 0 (nothing launched yet, least tried): Fun-Bots - no sight, fire tapped from 0.2 s on target */
    H->frame += 40; lockon_tick(0, &b, soldier, "Mi28", vp, eye, LAUNCHER_AA); assert(g_lockStrat[0] == 0 && !presses[EIA_ZOOM] && g_lockSince[0] && !presses[EIA_FIRE]);
    for (int k = 0; k < 6; k++) { H->frame++; lockon_tick(0, &b, soldier, "Mi28", vp, eye, LAUNCHER_AA); }
    assert(presses[EIA_FIRE] >= 1 && g_lockFirstAt[0] && g_rocketShots[0] == 0 && !presses[EIA_ZOOM]);
    int taps = presses[EIA_FIRE]; for (int k = 0; k < 28; k++) { H->frame++; lockon_tick(0, &b, soldier, "Mi28", vp, eye, LAUNCHER_AA); }
    assert(presses[EIA_FIRE] > taps && b.step == 1);   /* tapping on, nothing launched yet */
    ammoLoaded = 0; H->frame++; lockon_tick(0, &b, soldier, "Mi28", vp, eye, LAUNCHER_AA);   /* the magazine drops: away */
    assert(b.step == 2 && g_rocketShots[0] == 1 && g_lockLaunches == 1 && g_lockWayWins[0] == 1 && g_lockWayTries[0] == 1);
    assert(lock_pick_way() == 0);   /* the way that launched is used from then on */
    /* a launch seen as a missile beside the bot, the magazine unchanged */
    ammoLoaded = 1; b.step = 0; H->frame += 200; lockon_tick(0, &b, soldier, "Mi28", vp, eye, LAUNCHER_AA);
    H->frame += 41; lockon_tick(0, &b, soldier, "Mi28", vp, eye, LAUNCHER_AA);
    for (int k = 0; k < 7; k++) { H->frame++; lockon_tick(0, &b, soldier, "Mi28", vp, eye, LAUNCHER_AA); }
    missileSeen = 1; H->frame++; lockon_tick(0, &b, soldier, "Mi28", vp, eye, LAUNCHER_AA); missileSeen = 0;
    assert(b.step == 2 && g_lockLaunches == 2 && g_lockWayWins[0] == 2);
    /* no launch at all in 7 s: it gives up and counts the try against that way */
    memset(g_lockWayWins, 0, sizeof g_lockWayWins); memset(g_lockWayTries, 0, sizeof g_lockWayTries); g_lockWayTries[0] = 1;   /* way 1 next */
    b.step = 0; H->frame += 200; lockon_tick(0, &b, soldier, "Mi28", vp, eye, LAUNCHER_AA);
    for (int k = 0; k < 12 * 30 && ended == 0; k++) { H->frame++; lockon_tick(0, &b, soldier, "Mi28", vp, eye, LAUNCHER_AA); }
    assert(ended == 1 && !strcmp(reason, "no lock") && g_lockWayTries[1] == 1 && presses[EIA_ZOOM] > 0); ended = 0;   /* way 1: the sight held */
    assert(lock_pick_way() == 2);   /* the next untried way */
    /* off target: the lock starts again */
    b.step = 1; b.stepAt = 0; look[1] += 0.2f; H->frame++; lockon_tick(0, &b, soldier, "Mi28", vp, eye, LAUNCHER_AA); assert(!g_lockSince[0]);
    blocked = 1; H->frame++; assert(!lockon_tick(0, &b, soldier, "Mi28", vp, eye, LAUNCHER_AA) && ended == 1);   /* behind cover with no lock: give up */
    blocked = 0; b.step = 0; vp[2] = 900; assert(!lockon_tick(0, &b, soldier, "Mi28", vp, eye, LAUNCHER_AA) && ended == 2);   /* out of range */
    vp[2] = 300; onGround = 1; assert(!lockon_tick(0, &b, soldier, "Mi28", vp, eye, LAUNCHER_AA) && ended == 3); onGround = 0;
    vp[1] = 0; vp[2] = 420; b.step = 0; assert(lockon_tick(0, &b, soldier, "T90", vp, eye, LAUNCHER_JAVELIN) && b.step == 0);   /* Javelin near its range: closes in first */
    /* V25: with the game's lock - the bot asks for it while on target; no fire while the game is still locking, fire at once
       when it reports "locked", without the sight; the launch is counted as before */
    vp[1] = 60; vp[2] = 300; g_lockHooked = 1; ended = 0; ammoLoaded = 1; b.step = 0; H->frame += 300;
    lockon_tick(0, &b, soldier, "Mi28", vp, eye, LAUNCHER_AA); H->frame += 41;
    look[0] = navs[0].targetYaw > 0 ? navs[0].targetYaw - 6.2831853f : navs[0].targetYaw; look[1] = g_aimPitch[0];
    fakeLock = 1; int before = presses[EIA_FIRE], zoomBefore = presses[EIA_ZOOM], w0 = wants;
    for (int k = 0; k < 90; k++) { H->frame++; lockon_tick(0, &b, soldier, "Mi28", vp, eye, LAUNCHER_AA); }
    assert(wants > w0 && presses[EIA_FIRE] == before && ended == 0);   /* locking: asked for, not fired, not given up */
    fakeLock = 2; H->frame++; lockon_tick(0, &b, soldier, "Mi28", vp, eye, LAUNCHER_AA);
    assert(presses[EIA_FIRE] > before && g_lockNativeLocks == 1 && presses[EIA_ZOOM] == zoomBefore);
    ammoLoaded = 0; H->frame++; lockon_tick(0, &b, soldier, "Mi28", vp, eye, LAUNCHER_AA); assert(b.step == 2);
    g_lockHooked = 0; fakeLock = 0;
    puts("PASS: lock-on launchers - Stinger / Igla / Javelin come out, the bot holds ADS on the target and fires only after a 2.2 s lock (it restarts when the aim drifts); "
         "a hidden aircraft, a landed one or one past 700 m ends it; a Javelin closes in before locking near its 450 m range; "
         "V23: three ways of firing (Fun-Bots' no-sight taps first), a launch counted by the magazine or a missile beside the bot, the way that launches kept, 7 s and no launch = no lock; "
         "V25: with the game's lock, asked for on target, no fire while locking, fired the moment the game reports the lock");
    return 0;
}
'''
        build('lock', lock)
        flags = r'''
#include <assert.h>
#include <stdio.h>
#define MAX_FLAGS 16
#define OFF_CP_OWNER_TEAM 80
#define OFF_CP_POSITION 64
static int g_flagCount, cq; static char *g_flags[MAX_FLAGS]; static char cp[128] __attribute__((aligned(8)));
static float mcom[3] = {5, 6, 7};
static int mode_is_cq(void) { return cq; }
static const float *objective_pos(int f) { return f == 0 ? mcom : 0; }
''' + re.search(r'^static int flag_owner\(int f\) \{[^\n]*\}', core, re.M).group(0) + '\n' + function(core, 'flag_pos') + r'''
int main(void)
{
    g_flagCount = 0; cq = 0;   /* Rush: no Conquest flags; objective 0 is an M-COM */
    assert(flag_owner(0) == 0 && flag_owner(-1) == 0 && flag_owner(99) == 0);
    assert(flag_pos(0) == mcom && flag_pos(1)[0] == 0 && flag_pos(-3)[2] == 0 && flag_pos(999)[1] == 0);
    cq = 1; g_flagCount = 2; g_flags[0] = cp; *(int *)(cp + 80) = 2; ((float *)(cp + 64))[0] = 42;   /* flag 1 slot empty */
    assert(flag_owner(0) == 2 && flag_pos(0)[0] == 42 && flag_owner(1) == 0 && flag_pos(1)[0] == 0);
    puts("PASS: flag_pos / flag_owner are safe for any index - V15 crashed Rush on server 3 (reading 0x120: an M-COM index into the empty Conquest flag table); in Rush the M-COM position is returned");
    return 0;
}
'''
        build('flags', flags)
        boat = r'''
#include <assert.h>
#include <stdio.h>
typedef struct { int phase; unsigned serial; } NavGrid;
static NavGrid grid = {2, 7}, *slot = &grid; static NavGrid **nav_slot(void) { return &slot; }
static unsigned g_boatMaskFor = 7; static int labels[1] = {0}, *g_landLabels = labels; static unsigned char mask[1];
static const unsigned char *boat_mask(void) { return mask; }
static int nav_label_at(const NavGrid *g, const int *l, float x, float y, float z) { (void)g; (void)l; (void)y; (void)z; return x < 0 ? 0 : x < 500 ? 1 : 2; }
''' + function(core, 'boat_trip_needed') + r'''
int main(void)
{
    float a[3] = {100, 0, 0}, b[3] = {300, 0, 0}, c[3] = {800, 0, 0}, sea[3] = {-50, 0, 0};
    assert(!boat_trip_needed(a, b) && boat_trip_needed(a, c) && boat_trip_needed(sea, b));
    g_landLabels = 0; assert(boat_trip_needed(a, b));   /* no landmass table yet: as before */
    puts("PASS: boats - only when the objective is on another landmass than the bot (Brian's beta rule); unknown counts as needed");
    return 0;
}
'''
        build('boat', boat)
        mn_src = (SRC / 'modes_native.inc').read_text(); st = (SRC / 'mode_state.h').read_text()
        prof = re.search(r'typedef struct BMModeProfile.*?} BMModeProfile;', st, re.S).group(0)
        botst = re.search(r'static struct \{int target.*?g_modeBot\[MAX_BOTS\];', st).group(0)
        rush = r"""
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "modes.h"
#include "rush_policy.h"
#define MAX_BOTS 4
#define OFF_PLAYER_SOLDIER 16
#define OFF_SOLDIER_POSITION 0
static struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; } host, *H = &host;
static enum BMMode g_mode = BM_MODE_RUSH; static int g_rushStage = 1; static BMRushState g_rush; static char logline[256];
static unsigned g_mcomArmAt[17], g_mcomNearSince[17][3], g_rushMsgCount[4];
static int g_crateEvents; static unsigned char g_mcomDestroyedBy[17];
""" + prof + "\nstatic BMModeProfile prof; static const BMModeProfile *g_modeProfile = &prof;\n" + botst + r"""
static char players[4][64] __attribute__((aligned(8))); static float bodies[4][4]; static int teams[4], bots[4], stageCalls;
static char pm[256] __attribute__((aligned(8))); static void *arr[4];
static char *player_manager(void) { return pm; }
static int player_exists(char *p) { for (int k = 0; k < 4; k++) if (p == players[k]) return 1; return 0; }
#define OFF_PLAYER_ONLINE_NAME 0
static int player_team(char *p) { for (int k = 0; k < 4; k++) if (p == players[k]) return teams[k]; return 0; }
static int is_bot(char *p) { for (int k = 0; k < 4; k++) if (p == players[k]) return bots[k]; return 0; }
static float dist2(const float *a, const float *b) { float x = a[0] - b[0], z = a[2] - b[2]; return x * x + z * z; }
static void bot_log(const char *f, ...) { snprintf(logline, sizeof logline, "%s", f); }
static void rush_sync_stage(int stage, const char *why) { (void)why; if (bm_rush_set_stage(&g_rush, stage, H->frame)) { g_rushStage = stage; stageCalls++; } }
""" + '\n'.join(function(mn_src, n) for n in ['rush_mark', 'rush_report_target', 'rush_watch_tick', 'rush_objective_message', 'rush_operator_focus', 'rush_crate_event']) + r"""
static void place(int k, float x, float y, float z) { bodies[k][0] = x; bodies[k][1] = y; bodies[k][2] = z; *(float **)(players[k] + 16) = bodies[k]; }
static void run(int seconds) { for (int f = 0; f < seconds * 30; f++) { H->frame++; rush_watch_tick(); } }
int main(void)
{
    prof.stages = 2; for (int k = 1; k <= 4; k++) { prof.mcom[k].present = 1; prof.mcom[k].pos[0] = k * 100.0f; prof.mcom[k].pos[1] = 10; }
    bm_rush_reset(&g_rush, 2, 2, 0); H->frame = 30;
    for (int k = 0; k < 4; k++) arr[k] = players[k]; void **a = arr; unsigned n = 4; memcpy(pm + 0xac, &a, sizeof a); memcpy(pm + 8, &n, sizeof n);
    teams[0] = 1; teams[1] = 2; teams[2] = 1; teams[3] = 2; bots[2] = bots[3] = 1;   /* 0: a human attacker, 1: a human defender, 2/3: bots */
    place(0, 0, 10, 500); place(1, 0, 10, 500); place(2, 0, 10, 500); place(3, 0, 10, 500);
    /* a defender never operates an unknown station; an attacker does */
    assert(!bm_rush_can_interact(&g_rush, 1, 2) && bm_rush_can_interact(&g_rush, 1, 1));
    /* the human attacker stands at M-COM 1's panel for 5 s: armed - now the defenders' job */
    place(0, 101, 10, 1); run(4); assert(g_rush.objective[1].status == BM_RUSH_UNKNOWN); run(2);
    assert(g_rush.objective[1].status == BM_RUSH_ARMED && bm_rush_can_interact(&g_rush, 1, 2) && !bm_rush_can_interact(&g_rush, 1, 1));
    /* the human defender defuses it */
    place(0, 0, 10, 500); place(1, 100, 10, 1); run(6); assert(g_rush.objective[1].status == BM_RUSH_IDLE && bm_rush_can_interact(&g_rush, 1, 1));
    place(1, 0, 10, 500);
    /* V16.2: armed again by a bot's hold, then ServerMetrics.ObjectiveSuccess (~26 s after an arm in the live rounds): that station destroyed */
    rush_mark(1, BM_RUSH_ARMED, "test"); H->frame += 100; rush_objective_message("ServerMetrics.ObjectiveSuccess");
    assert(g_rush.objective[1].status == BM_RUSH_DESTROYED && g_rushStage == 1 && g_rushMsgCount[1] == 1);
    H->frame += 10; rush_objective_message("ServerMetrics.ObjectiveSuccess"); assert(g_rushMsgCount[1] == 1);   /* one report = one station */
    /* ServerMission.ObjectiveCompleted: the sector is done - whatever is left of it falls, sector 2 (the deployment moves up) */
    H->frame += 90; rush_objective_message("ServerMission.ObjectiveCompleted");
    assert(g_rush.objective[2].status == BM_RUSH_DESTROYED && g_rushStage == 2 && stageCalls == 1 && bm_rush_active(&g_rush, 3));
    /* ObjectiveSuccess with no armed station: the one with an attacker nearest */
    place(2, 302, 10, 1); H->frame += 90; rush_objective_message("ServerMetrics.ObjectiveSuccess"); assert(g_rush.objective[3].status == BM_RUSH_DESTROYED);
    /* armed with nothing reported for 60 s: back to idle (the arm did not take, or it was defused unseen) */
    rush_mark(4, BM_RUSH_ARMED, "test"); place(2, 0, 10, 500); run(59); assert(g_rush.objective[4].status == BM_RUSH_ARMED); run(2); assert(g_rush.objective[4].status == BM_RUSH_IDLE);
    /* the operator keeps at its station: no fight with an enemy 10 m off, a fight with one 3 m off - none at all while it holds the key */
    H->botCount = 4; for (int k = 0; k < 4; k++) H->bots[k] = players[k];
    assert(bm_rush_acquire(&g_rush, 4, 1, players[2], H->frame, 270)); g_modeBot[2].target = 4; place(2, 405, 10, 3);
    place(3, 405, 10, 13); assert(rush_operator_focus(2, players[3]) && rush_operator_focus(2, NULL));
    place(3, 405, 10, 5); assert(!rush_operator_focus(2, players[3]));
    g_modeBot[2].interactAt = H->frame; assert(rush_operator_focus(2, players[3])); g_modeBot[2].interactAt = 0;   /* holding the key: nothing stops it */
    place(2, 440, 10, 3); assert(!rush_operator_focus(2, NULL));   /* 35 m from the panel: an ordinary soldier */
    /* V16.2.2: the game's own crate events - armed by the bot at the panel, disarmed by the defender, destroyed */
    g_crateEvents = 1; bm_rush_release(&g_rush, players[2]); rush_mark(4, BM_RUSH_IDLE, "test");
    place(2, 401, 10, 1); rush_crate_event(0, players[2]); assert(g_rush.objective[4].status == BM_RUSH_ARMED);
    place(3, 400, 10, 2); rush_crate_event(1, players[3]); assert(g_rush.objective[4].status == BM_RUSH_IDLE);
    rush_crate_event(0, players[2]); place(2, 0, 10, 500); rush_crate_event(2, players[2]); assert(g_rush.objective[4].status == BM_RUSH_DESTROYED);
    /* ... and the ObjectiveSuccess that follows the same destruction changes nothing (V16.2.2 destroyed the other station too) */
    { int before = g_rushStage; H->frame += 90; rush_objective_message("ServerMetrics.ObjectiveSuccess"); H->frame += 90; rush_objective_message("ServerMission.ObjectiveCompleted"); assert(g_rushStage == before); }
    g_crateEvents = 0;
    puts("PASS: V16.1/V16.2 Rush objectives - defenders only go to an armed M-COM (V9-V16 camped every unknown panel and blocked the attackers); a player at a panel 5 s arms / defuses it; "
         "a bot's full 8 s hold marks it; ServerMetrics.ObjectiveSuccess destroys the armed (else the attacker-nearest) station, ServerMission.ObjectiveCompleted ends the sector = the next sector and new deployments; "
         "an arm with no result in 60 s is retried; V16.2.2: the game's own crate armed / disarmed / destroyed events set the station; the bot going to the panel ignores enemies over 4 m away, the bot holding the key ignores everything");
    return 0;
}
"""
        build('rush', rush, ['modes.c'])
    # ---- source checks ----
    mn = (SRC / 'modes_native.inc').read_text(); en = (SRC / 'enhanced.h').read_text(); ec = (SRC / 'enhanced.c').read_text()
    assert '#define OFF_CSEDATA_TEAM 0x6C' in core and '*(int *)(data + OFF_CSEDATA_TEAM)' in core
    assert 'if(!g_fleet[i].vehicle)crew_spawn(bot,t,bot+OFF_PLAYER_ONLINE_NAME);' in mn
    assert 'mode_note_death(pl);   // AltBotMod V16' in core and 'g_modeFailedSpawn[slot]=H->frame+180*30;' in mn
    assert 'borrows the %s grid of the same level' in core and '"ConquestLarge0","ConquestAssaultLarge0"' in core
    assert 'if(!grid || grid->phase!=2 ||' in core   # extension for every mode (V15: vehicle modes only)
    assert ': bm_armored(bm_vehicle_profile(vehicle_name(v))) ? 2 : 1;' in core   # jeeps / boats take spawn passengers
    assert 'if (passenger_dropout_tick(i, veh))' in core and 'vehicle_countermeasure_tick(i);   // AltBotMod V16' in core
    assert '#define BM_ENHANCED_VERSION "AltBotMod V34 - Squad Up"' in en and 'PilotEvery' not in ec
    chat = (SRC / 'chat.c').read_text()
    assert 'const char *mp[1] = {"vars.maxPlayers"};' in chat and 'if (v > 0 && (m <= 0 || v < m)) m = v;' in chat   # V16.2.1: bots obey vars.maxPlayers
    assert '(fromCmdLine > 0 && fromCmdLine < maxPlayers)) maxPlayers = fromCmdLine;' in core
    assert 'AddVectoredExceptionHandler(1, crash_veh)' in core and 'RemoveVectoredExceptionHandler(g_crashVeh)' in core
    for key in ('VehicleDamageTaken', 'VehicleDamageDealt'):   # V16.2
        assert 'FLOAT_KEY("%s"' % key in ec and ', 0.25f, 4.0f)' in ec.split('FLOAT_KEY("%s"' % key)[1].split(')')[0] + ')', key
    assert 'c->vehicleDamageTaken = c->vehicleDamageDealt = 1.0f;' in ec
    assert 'patch_vslot(ADDR_VEHICLE_VTABLE + 0x110, ADDR_VEHICLE_ON_DAMAGE, (void *)hook_vehicle_on_damage, "ServerVehicleEntity::onDamage");' in core
    assert '#define ADDR_VEHICLE_ON_DAMAGE' in core and '0x00df9020' in core.split('#define ADDR_VEHICLE_ON_DAMAGE')[1].split('\n')[0]
    assert 'float factor = g_enhanced.vehicleDamageTaken * giver_vehicle_factor(a2)' in core.split('hook_vehicle_on_damage(void *veh')[1][:600]
    assert 'if (rush_operator_holding(i))' in core and 'RUSH_ARM_REACH' in mn and '#define RUSH_HOLD_FRAMES (8 * 30)' in (SRC / 'rush_policy.h').read_text()
    for key in ('GadgetVariety', 'C4Attacks', 'LockOnLaunchers', 'BotTicketCost', 'VehicleCountermeasures'):
        assert '"%s"' % key in ec, key
    assert 'place_class_gadget(i, soldier);' in (SRC / 'sniper_native.inc').read_text()
    assert 'enemy at the stationary weapon' in (SRC / 'emplacement_native.inc').read_text()
    assert '#define DRIVE_MAX_PTS          256' in core
    print('PASS: V16 wiring - spawn points read their real Team field (data+0x6C); Rush bots crew teammates\' vehicles; spawn entities that kill bots out of bounds rest; '
          'a new mode borrows the level\'s grid (any mode extends it); jeeps / boats take passengers; passenger and smoke ticks run; snipers place equipment at the perch; '
          'gunners leave a stationary weapon for an enemy beside it; 256-point vehicle routes; the V16 settings; V16.2 vehicle damage hook (vtable +0x110) and its two settings, the M-COM hold override')


if __name__ == '__main__':
    main()
