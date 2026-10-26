#!/usr/bin/env python3
"""AltBotMod V23 - Air and Armour: offline checks.

1. missile_tests.c: missiles in flight - the transform found from a missile moving between two reads, a bot's own launch
   recognised, flares / smoke for the bot whose vehicle or aircraft a missile is heading for (not for a human's, not for
   its own launch, not for one pointing elsewhere).
2. The gunner's line of sight passes its own hull (gun_can_see with a ray that meets the own vehicle's body first): a man
   standing in front of a tank is in sight; a wall still hides him.
3. Armour's targets: a soldier within 60 m before an enemy vehicle further off (bm_target_score).
4. gunship_native.inc: a bot that has just spawned takes a free gun seat of its team's AC-130 (seat 1 and up), a seat stays
   free for a human on the team, the other team's gunship and an old spawn are left alone, a refused entry rests a minute.
5. Source checks: lock-on launchers' three ways (harness: altbotmod_v16_checks.py), aircraft targets include the
   gunship, jets prefer the ground and the gunship, the gunship's guns reach 1.6 km and its gunners stay, the drive sensors
   look down the route, no armour combat hold, quicker speed recovery, Fun-Bots roads joined from 90 m, routes from 8 m,
   the round-start vehicle rush, the missile worker started and stopped, short bursts at range, the version.
"""
from pathlib import Path
import argparse, subprocess, tempfile
from test4_checks import function
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
VAL = ROOT / 'validation'

SEE = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static unsigned char g_lastHitRest[0x40];
static void *ownBody, *wallBody; static float wallAt = -1;   /* wall: a plane z = wallAt */
/* the own hull: the box |x| < 2, 0 < y < 2.5, |z| < 3.5 around the origin (the turret at (0, 2.6, 0)) */
static int ray_blocked_char(const float *a, const float *b, float *hit)
{
    for (int k = 0; k <= 200; k++) {
        float t = k / 200.0f, p[3] = {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
        if (fabsf(p[0]) < 2 && p[1] > 0 && p[1] < 2.5f && fabsf(p[2]) < 3.5f) { memcpy(hit, p, 12); memcpy(g_lastHitRest, &ownBody, sizeof ownBody); return 1; }
        if (wallAt > 0 && p[2] >= wallAt) { memcpy(hit, p, 12); memcpy(g_lastHitRest, &wallBody, sizeof wallBody); return 1; }
    }
    return 0;
}
'''

GUNSHIP = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#define MAX_BOTS 8
#define OFF_PLAYER_ONLINE_NAME 0
#define OFF_PLAYER_SOLDIER 16
#define OFF_SOLDIER_HEALTH 0
#define OFF_VEHICLE_HEALTH 0
#define ADDR_PLAYER_ENTER_VEHICLE 1
static struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; float botMove[MAX_BOTS][3]; } host, *H = &host;
typedef struct { int phase; } Phase; static Phase g_drive[MAX_BOTS], g_air[MAX_BOTS];
typedef struct { void *vehicle; } Fleet; static Fleet g_fleet[MAX_BOTS];
typedef struct { int commander; } Brain; static Brain brains[MAX_BOTS]; static Brain *bot_brain(int i) { return &brains[i]; }
typedef struct { int mode; } Nav; static Nav navs[MAX_BOTS]; static Nav *bot_nav(int i) { return &navs[i]; }
static char *g_crewVehicle[MAX_BOTS], *g_lastSpawnedBot[MAX_BOTS]; static unsigned g_lastSpawnedAt[MAX_BOTS];
static int g_roundCleanup, g_vehInput = 1, refuse;
static int mode_uses_vehicles(void) { return 1; } static int level_ready(void) { return 1; }
static void nav_cancel_path(int i) { (void)i; }
static char logline[256]; static void bot_log(const char *f, ...) { (void)f; snprintf(logline, sizeof logline, "%s", f); }
/* players: name at 0, soldier pointer at 16, team at 40, bot flag at 44 */
static char players[4][64], soldiers[4][16], *roster[4]; static float health = 100;
static char *player_manager(void) { static char pm[0xc0]; char **arr = roster; unsigned n = 4; memcpy(pm + 0xac, &arr, sizeof arr); memcpy(pm + 8, &n, sizeof n); return pm; }
static int player_team(char *p) { return *(int *)(p + 40); }
static int is_bot(void *p) { return *(int *)((char *)p + 44); }
/* vehicles */
typedef struct { float health; char *seat[4]; int nseat, team; const char *name; char *occupied; } FakeVeh;
static FakeVeh ships[2]; static char *vlist[2]; static char seatMem[2][4][0x60];
static int vehicle_list(char ***out) { *out = vlist; return 2; }
static const char *vehicle_name(char *v) { return ((FakeVeh *)v)->name; }
static int vehicle_ok(char *v) { return v != NULL; }
static int vehicle_is_gunship(char *v) { return !strcmp(((FakeVeh *)v)->name, "GUNSHIP"); }
static float origin[3] = {0, 500, 0};
static float *vehicle_pos(char *v, int *team) { if (team) *team = ((FakeVeh *)v)->team; return origin; }
static char *vehicle_occupant(char *v) { FakeVeh *f = (FakeVeh *)v; for (int s = 0; s < f->nseat; s++) { char *o; memcpy(&o, f->seat[s] + 0x4c, sizeof o); if (o) return o; } return NULL; }
static int vehicle_seats(char *v, char ***out) { *out = ((FakeVeh *)v)->seat; return ((FakeVeh *)v)->nseat; }
static int player_exists(char *p) { return p != NULL; }
static char *g_inVehicle[4];
static char *player_vehicle(char *p) { for (int k = 0; k < 4; k++) if (p == players[k]) return g_inVehicle[k]; return NULL; }
typedef char (*EnterVehicle_t)(void *player, void *vehicle, int seat);
static char enter(void *pl, void *veh, int seat)
{
    if (refuse) return 0;
    FakeVeh *f = (FakeVeh *)veh; memcpy(f->seat[seat] + 0x4c, &pl, sizeof pl);
    for (int k = 0; k < 4; k++) if (pl == players[k]) g_inVehicle[k] = (char *)veh;
    return 1;
}
#undef ADDR_PLAYER_ENTER_VEHICLE
#define ADDR_PLAYER_ENTER_VEHICLE enter
'''


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    print(r.stdout, end='')
    if r.returncode: print(r.stderr); raise SystemExit('FAIL: %s exited %d' % (Path(cmd[0]).name, r.returncode))
    return r.stdout


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    san = ['-g', '-fsanitize=address,undefined', '-fno-sanitize=alignment', '-fno-sanitize-recover=all'] if a.sanitize else ['-O1']
    core = (SRC / 'core.c').read_text()
    cc = ['gcc', '-std=gnu11', '-Wall', '-Wno-unused-function', '-Wno-unused-variable', '-I', str(SRC)] + san
    with tempfile.TemporaryDirectory(prefix='bf3-v23-') as tmp:
        tmp = Path(tmp)
        def build(name, body, extra=()):
            c = tmp / (name + '.c'); c.write_text(body)
            subprocess.run(cc + [str(c), *[str(SRC / x) for x in extra], '-lm', '-o', str(tmp / name)], check=True)
            return run([str(tmp / name)])
        out = build('missiles', (VAL / 'missile_tests.c').read_text(), ['enhanced.c', 'air.c', 'modes.c'])
        assert out.count('PASS:') == 3
        see = SEE + core[core.index('static void *g_gunSeeOwnBody;'):core.index('static void *g_gunSeeOwnBody;') + 200].split('\n')[0] + '\n' + function(core, 'gun_can_see') + r'''
int main(void)
{
    static char hull, wall; ownBody = &hull; wallBody = &wall;
    float turret[3] = {0, 2.6f, 0}, man[3] = {0, 1.2f, 8};   /* a soldier 8 m in front, the line runs down through the front plate */
    g_gunSeeOwnBody = NULL; assert(!gun_can_see(turret, man, 0));      /* V5-V22: the own hull was "cover" */
    g_gunSeeOwnBody = &hull; assert(gun_can_see(turret, man, 0));      /* V23: past the own hull, he is in sight */
    wallAt = 6; assert(!gun_can_see(turret, man, 0)); wallAt = -1;     /* a wall between still hides him */
    float far[3] = {0, 1.2f, 60}; assert(gun_can_see(turret, far, 0));
    puts("PASS: V23 gunner sight - a soldier 8 m in front of a tank, below the turret, is in sight (the ray goes on past the own hull); a wall still hides him");
    return 0;
}
'''
        build('see', see)
        score = r'''
#include <assert.h>
#include <stdio.h>
#include "enhanced.h"
int main(void)
{
    float man50 = bm_target_score(1, BM_TARGET_INFANTRY, 50), man10 = bm_target_score(1, BM_TARGET_INFANTRY, 10);
    float tank200 = bm_target_score(1, BM_TARGET_GROUND, 200), tank80 = bm_target_score(1, BM_TARGET_GROUND, 80);
    assert(man50 < tank200 && man10 < tank200 && man10 < man50 && tank80 < bm_target_score(1, BM_TARGET_INFANTRY, 35));   /* a near enemy tank still first */
    assert(bm_target_score(1, BM_TARGET_ENGINEER, 60) < bm_target_score(1, BM_TARGET_INFANTRY, 60));
    assert(bm_target_score(1, BM_TARGET_INFANTRY, 90) > tank200);      /* beyond 60 m, enemy armour first as before */
    puts("PASS: V23 armour targets - a soldier within 60 m before an enemy vehicle 150 m+ off (a near enemy tank and a close engineer still first); beyond it armour first as before");
    return 0;
}
'''
        build('score', score, ['enhanced.c', 'air.c', 'modes.c'])
        gun = GUNSHIP + (SRC / 'gunship_native.inc').read_text() + r'''
static void setup(void)
{
    memset(&host, 0, sizeof host); memset(ships, 0, sizeof ships); memset(seatMem, 0, sizeof seatMem); memset(g_inVehicle, 0, sizeof g_inVehicle);
    memset(g_lastSpawnedBot, 0, sizeof g_lastSpawnedBot); g_gunshipFailUntil = 0; refuse = 0;
    for (int k = 0; k < 4; k++) {
        roster[k] = players[k]; char *s = soldiers[k]; memcpy(players[k] + 16, &s, sizeof s); memcpy(soldiers[k], &health, sizeof health);
        *(int *)(players[k] + 40) = k < 3 ? 1 : 2; *(int *)(players[k] + 44) = k < 2 || k == 3;   /* player 2: the human on team 1 */
    }
    H->botCount = 2; H->bots[0] = players[0]; H->bots[1] = players[1];
    for (int v = 0; v < 2; v++) { ships[v].name = "GUNSHIP"; ships[v].health = 1; ships[v].team = v + 1; ships[v].nseat = 4; for (int q = 0; q < 4; q++) ships[v].seat[q] = seatMem[v][q]; vlist[v] = (char *)&ships[v]; }
    H->frame = 1500;   /* (frame + bot) % 15 == 0 for bot 0 */
}
int main(void)
{
    setup(); g_lastSpawnedBot[0] = players[0]; g_lastSpawnedAt[0] = H->frame - 30;
    gunship_tick(0); assert(g_inVehicle[0] == (char *)&ships[0]);   /* team 1's gunship, seat 1 */
    { char *o; memcpy(&o, seatMem[0][1] + 0x4c, sizeof o); assert(o == players[0]); }
    /* the next bot: 2 free seats left, a human on the team - it may take one (one still stays free) */
    g_lastSpawnedBot[1] = players[1]; g_lastSpawnedAt[1] = H->frame; H->frame += 14; gunship_tick(1); assert(g_inVehicle[1] == (char *)&ships[0]);
    /* a third: only one free seat left and the human not aboard - it stays free for him */
    { char *b = players[3]; *(int *)(players[3] + 40) = 1; H->bots[2] = b; H->botCount = 3; g_lastSpawnedBot[2] = b; g_lastSpawnedAt[2] = H->frame;
      H->frame += 1; while ((H->frame + 2) % 15) H->frame++; gunship_tick(2); assert(!g_inVehicle[3]); }
    puts("PASS: V23 gunship - a bot just spawned takes a gun seat (1 and up) of its team's AC-130; with a human on the team the last free seat stays his");
    setup(); g_lastSpawnedBot[0] = players[0]; g_lastSpawnedAt[0] = H->frame - 4 * 30; gunship_tick(0); assert(!g_inVehicle[0]);   /* spawned 4 s ago: on its way already */
    setup(); *(int *)(players[0] + 40) = 2; g_lastSpawnedBot[0] = players[0]; g_lastSpawnedAt[0] = H->frame; ships[1].team = 1; ships[0].team = 1;
    gunship_tick(0); assert(!g_inVehicle[0]);   /* a team-2 bot and only team 1's gunships */
    setup(); refuse = 1; g_lastSpawnedBot[0] = players[0]; g_lastSpawnedAt[0] = H->frame; gunship_tick(0); assert(!g_inVehicle[0] && g_gunshipFailUntil == H->frame + 1800);
    puts("PASS: V23 gunship - not for a bot already on its way, not the other team's, a refused entry rests a minute");
    return 0;
}
'''
        build('gunship', gun)
    air = (SRC / 'air_native.inc').read_text(); emp = (SRC / 'emplacement_native.inc').read_text(); gt = (SRC / 'gunner_targets.inc').read_text()
    trace = (SRC / 'trace_native.inc').read_text(); fleet = (SRC / 'fleet_native.inc').read_text(); gc = (SRC / 'ground_combat.inc').read_text()
    en = (SRC / 'enhanced.c').read_text(); eh = (SRC / 'enhanced.h').read_text()
    # aircraft targets and jets
    assert '!vehicle_is_aircraft(veh)) continue;   // (V23: the gunship too)' in core and 'vehicle_is_gunship(veh) ? 900.0f : 700.0f' in core
    assert 'GUN_IS_AIRCRAFT(v) ? BM_TARGET_AIR : BM_TARGET_GROUND' in gt and 'int air = GUN_IS_AIRCRAFT(v);' in emp and '#define GUN_IS_AIRCRAFT(v) vehicle_is_aircraft(v)' in core
    assert 'int gunshipTarget=AIR_IS_GUNSHIP(v);' in air and '#define AIR_IS_GUNSHIP(v) vehicle_is_gunship(v)' in core and 'float jetScore=gunshipTarget?-900:air?(distance<900?-700:500):-250;' in air and "BM_AIR_JET?-150:" in air and 'score-=160;' in air
    # gunship
    assert 'if (vehicle_is_gunship(veh)) return 1600.0f;' in core and 'vehicle_is_gunship(veh) ? 35.0f' in core
    assert 'stationary_weapon(veh) || vehicle_is_gunship(veh)) { g_strandedSince[i] = 0; return; }' in core and 'vehicle_is_gunship(player_vehicle(pl))) { g_local_brain_tick_wasRiding[i] = 1; return; }' in core
    assert 'gunship_tick(i);      // AltBotMod V23' in core and '#include "gunship_native.inc"' in core
    print('PASS: V23 aircraft - AA launchers (to 850 m), gunners and stationary AA take the gunship as an aircraft; jets go for the ground and the gunship unless an enemy aircraft is within 900 m; '
          'the gunship\'s guns reach 1.6 km and its gunners stay aboard')
    # armour, launchers, missiles
    assert 'driverWeapon && slot == 1 && vprofile->kind == BM_TANK && dh < 6.0f' in core and 'g_gunSeeOwnBody = v && !IsBadReadPtr(v + 0x30, 4)' in core
    assert 'static int lock_pick_way(void)' in core and 'missile_launched_near(eye, g_lockFirstAt[i])' in core and 'FORGET(g_lockStrat)' in core
    assert 'missile_start();   // AltBotMod V23' in core and 'missile_stop();   // AltBotMod V23' in core and 'missile_tick();   // AltBotMod V23' in core
    print('PASS: V23 armour fires at a man in front (coax at any range, the main gun from 6 m, past its own hull); lock-on launchers learn the way that launches; missiles are tracked from load to unload')
    # driving, routes
    assert 'float lx = fx, lz = fz;' in core and 'float b[3] = {a[0] + lx * horizon, a[1], a[2] + lz * horizon};' in core
    assert 'if(1){(void)i;(void)d;(void)vp;return 0;}' in gc
    assert 'throttle = bm_drive_throttle(vwant, d->speed, bm_armored(profile), ae);' in core   # (V29: no smoothed target - V23's 8 m/s per second climb is gone)
    assert 'float reach = (boat ? 100.0f : 90.0f) + alt * 30.0f;' in trace and '#define TRACE_MIN_TRIP        8.0f' in trace
    assert 'int rush=FLEET_ROUND_RUSH;' in fleet and '#define FLEET_ROUND_RUSH (g_levelReadyAt && H->frame-g_levelReadyAt<120*30)' in core and 'g_levelReadyAt = H->frame ? H->frame : 1;' in core
    assert 'float kc = distance > 20 ? fminf(k, 1.25f) : k;' in en
    assert '#define BM_ENHANCED_VERSION "AltBotMod V34 - Squad Up"' in eh
    print('PASS: V23 driving - sensors look down the route, no armour slow-down for targets, speed back at 8 m/s per second, Fun-Bots roads joined from 90 m, routes on foot from 8 m, '
          'a round-start rush to every vehicle; short bursts at range; version V23')

if __name__ == '__main__':
    main()
