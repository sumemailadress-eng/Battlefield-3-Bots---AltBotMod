#!/usr/bin/env python3
"""AltBotMod V29 - Full Throttle: offline checks.

1. v29_drive_sim.c on the longest Fun-Bots land road of eight maps, a tank and a jeep: V29 driving (bm_drive_throttle - full
   throttle unless a real bend, the end, hard steering or a vehicle in the lane says otherwise) against the V28 way (the
   speed target raised 8 m/s per second after a dip, partial throttle on the way up); no stop-go, full throttle most of
   the time; a vehicle stopped on the road is stopped for, then passed with room to spare.
2. The pure parts: bm_drive_throttle, bm_lane_limit, bm_lane_relation (ahead / beside / behind / on the route round a bend).
3. The lock put in place (core.c lock_want_ent / lock_fake, from on_lock_update): after the weapon's own lock time on the
   same live vehicle, a target holder for it is made (the game's allocator, the vehicle-target vtable, a weak reference),
   stored at controller +0x54 (the old one deleted) and SetLocked(true) called; not before the lock time, not for a dead
   vehicle, not when the aim has lapsed, not with LockOnAssist=0; a new target replaces the holder.
4. v29_jet_area_sim.c: the real bm_air_step on the air_combat_sim model with the V29 area - jets flying out turn back
   (from the middle they never leave; from the edge they are back within seconds), strike runs and dogfights stay inside.
5. Source checks: no drive_wait_turn; nothing static counts on a recorded road; the lane scan; drivers' on-foot presses
   dropped; the director keeps the throttle through a re-plan; drive statistics; door guns' tolerance and longer nudge;
   AA missiles; locks from launchers, gunners and aircraft; LockOnAssist and JetAreaMarginMeters in the UI; the version.
"""
from pathlib import Path
import argparse, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
VAL = ROOT / 'validation'
TRACES = ROOT / 'files/server/BotMod_traces'
MAPS = ['XP3_Alborz_ConquestLarge0', 'XP3_Desert_ConquestLarge0', 'MP_012_ConquestLarge0', 'XP1_002_ConquestLarge0',
        'XP3_Shield_ConquestLarge0', 'XP3_Valley_ConquestLarge0', 'MP_007_ConquestLarge0', 'MP_017_ConquestLarge0']

PURE = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "enhanced.h"
int main(void)
{
    /* throttle: full below the target, 0.6 for a wheeled vehicle on full lock, hold at it, off a little over, brake well over, stop */
    assert(bm_drive_throttle(16, 0, 1, 0) == 1.0f && bm_drive_throttle(16, 10, 1, 1.4f) == 1.0f && bm_drive_throttle(30, 5, 0, 1.4f) == 0.6f);
    assert(bm_drive_throttle(16, 15.5f, 1, 0) == 0.3f && bm_drive_throttle(16, 18, 1, 0) == 0.0f && bm_drive_throttle(10, 20, 1, 0) < -0.5f);
    assert(bm_drive_throttle(0, 3, 1, 0) < 0 && bm_drive_throttle(0, 0, 1, 0) == 0.0f);
    /* never a partial throttle on the way back up to speed (V28: 0.35 + 0.12 x the error) */
    for (float v = 0; v < 14.9f; v += 0.5f) assert(bm_drive_throttle(16, v, 1, 0.2f) == 1.0f);
    puts("PASS: bm_drive_throttle - full throttle all the way up to 1 m/s under the target (0.6 for a wheeled vehicle on full lock), 0.3 at it, off the throttle up to 3 m/s over, a brake beyond, a stop for 0");
    /* lane limit: a stopped vehicle - stop 6 m short; a moving one - its speed at the gap; head on and close - stop */
    assert(bm_lane_limit(30, 0, 3.5f) > 10 && bm_lane_limit(10.0f, 0, 3.5f) == 0.0f && bm_lane_limit(9.0f, 0, 3.5f) == 0.0f && bm_lane_limit(12.0f, 0, 3.5f) < 4.5f);
    assert(fabsf(bm_lane_limit(10.0f, 8, 3.5f) - 7.0f) < 0.01f && bm_lane_limit(25, 8, 3.5f) > 8.5f);
    assert(bm_lane_limit(20, -10, 3.5f) == 0.0f && bm_lane_limit(40, -10, 3.5f) > 10);
    puts("PASS: bm_lane_limit - closes on a vehicle at 3.5 m/s^2 at most, stops 6 m short of a stopped one, follows a moving one at its speed, stops for one head on within 15 m");
    /* lane relation: straight road along +z, the car at the origin heading +z */
    float road[3 * 4] = {0, 0, 10, 0, 0, 30, 0, 0, 60, 0, 0, 100}, vp[3] = {0, 0, 0}, fwd[2] = {0, 1}, along, lat;
    float ahead[3] = {0.5f, 0, 12}, beside[3] = {6, 0, 12}, behind[3] = {0, 0, -8}, far[3] = {-1, 0, 50}, off[3] = {9, 0, 50};
    assert(bm_lane_relation(road, 4, 0, vp, fwd, ahead, 3.9f, 50, &along, &lat) && fabsf(along - 12) < 0.01f && lat > 0);
    assert(!bm_lane_relation(road, 4, 0, vp, fwd, beside, 3.9f, 50, &along, &lat));
    assert(!bm_lane_relation(road, 4, 0, vp, fwd, behind, 3.9f, 50, &along, &lat));
    assert(bm_lane_relation(road, 4, 0, vp, fwd, far, 3.9f, 60, &along, &lat) && fabsf(along - 50) < 0.5f && lat < 0);
    assert(!bm_lane_relation(road, 4, 0, vp, fwd, off, 3.9f, 60, &along, &lat));
    assert(!bm_lane_relation(road, 4, 0, vp, fwd, far, 3.9f, 40, &along, &lat));   /* beyond the look-ahead */
    /* a bend: the road turns right after 30 m; a vehicle on it round the bend is in the lane, one straight ahead off the road is not */
    float bend[3 * 4] = {0, 0, 10, 0, 0, 30, 20, 0, 45, 45, 0, 50}, round[3] = {20, 0, 45}, straight[3] = {0, 0, 45};
    assert(bm_lane_relation(bend, 4, 0, vp, fwd, round, 3.9f, 60, &along, &lat) && along > 40 && along < 60);
    assert(!bm_lane_relation(bend, 4, 0, vp, fwd, straight, 3.9f, 60, &along, &lat));
    puts("PASS: bm_lane_relation - in the lane straight ahead (15 m) and on the route itself beyond (round a bend too); not beside the road, behind, off the route or beyond the look-ahead");
    return 0;
}
'''

LOCK = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#define MAX_BOTS 4
#define __cdecl
typedef unsigned long SIZE_T;
static int IsBadReadPtr(const void *p, SIZE_T n) { (void)n; return p == NULL; }
static struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; } host, *H = &host;
#define OFF_PLAYER_ONLINE_NAME 0
static void bot_log(const char *f, ...) { (void)f; }
static const char *vehicle_name(char *v) { (void)v; return "Ka-60"; }
static struct { int lockAssist; float lockOnSeconds; } g_enhanced = {1, -1.0f};
static unsigned t_allocs, t_deletes, t_weak, t_locked;
static void *t_alloc(void) { t_allocs++; return calloc(1, 16); }
static void t_weakref(void **out, void *ent) { void **tok = malloc(sizeof(void *)); *tok = (char *)ent + 4; *out = tok; t_weak++; }
static void t_delete(void *h) { t_deletes++; (void)h; }
static void t_set_locked(char *c) { *(int *)(c + 0xc) = 2; *(float *)(c + 8) = 1.0f; *(unsigned char *)(c + 0x50) = 1; t_locked++; }
__asm__(".text\n");
@@CODE@@
static unsigned char ctrl[0x80] __attribute__((aligned(16)));
static float *xf; static unsigned stack[4];
static char veh[0x200], veh2[0x200];
static void weapon_at(float x, float y, float z, float fx, float fy, float fz)
{
    memset(xf, 0, 64); xf[8] = fx; xf[9] = fy; xf[10] = fz; xf[12] = x; xf[13] = y; xf[14] = z; xf[15] = 1;
}
static void tick(float *eye, float *target, char *tv)
{
    float l = sqrtf((target[0] - eye[0]) * (target[0] - eye[0]) + (target[1] - eye[1]) * (target[1] - eye[1]) + (target[2] - eye[2]) * (target[2] - eye[2]));
    lock_want_ent(1, eye, target, 4.0f, tv);
    weapon_at(eye[0] + 0.3f, eye[1] - 0.1f, eye[2] + 0.2f, (target[0] - eye[0]) / l, (target[1] - eye[1]) / l, (target[2] - eye[2]) / l);
    stack[2] = (unsigned)(size_t)xf; on_lock_update((char *)ctrl, stack);
}
int main(void)
{
    H->botCount = 2; H->frame = 100; static char name0[16] = "a", name1[16] = "b"; H->bots[0] = name0; H->bots[1] = name1;
    xf = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0); assert(xf != MAP_FAILED);
    g_lockXf = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    float *data = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    data[3] = 1.0f;   /* LockTime (+0xc) = 1 s */
    memset(ctrl, 0, sizeof ctrl); *(unsigned *)ctrl = ADDR_LOCK_CTRL_VTABLE; *(unsigned *)(ctrl + 4) = (unsigned)(size_t)data; ctrl[0x11] = 1;
    *(unsigned *)veh = 0x025bf8e8u; *(float *)(veh + 0x128) = 500.0f; *(unsigned *)veh2 = 0x025bf8e8u; *(float *)(veh2 + 0x128) = 800.0f;
    float eye[3] = {0, 1.6f, 0}, target[3] = {0, 40, 300};
    /* on target, before the lock time: the controller is activated, nothing put in place */
    for (int k = 0; k < 29; k++) { tick(eye, target, veh); H->frame++; }
    assert(ctrl[0x10] == 1 && t_allocs == 0 && *(void **)(ctrl + 0x54) == NULL && lock_state(1, NULL) == 1);
    /* the lock time on the same vehicle: a holder for it, SetLocked */
    tick(eye, target, veh); H->frame++; tick(eye, target, veh);
    unsigned firstLock = H->frame;
    assert(t_allocs == 1 && t_weak == 1 && t_locked == 1 && lock_holder_vehicle(*(void **)(ctrl + 0x54)) == veh && *(int *)(ctrl + 0xc) == 2);
    assert(lock_state(1, NULL) == 2);
    puts("PASS: the lock put in place - after the weapon's lock time (1 s) on the same live vehicle: a target holder for it at controller +0x54 and SetLocked; nothing before");
    /* locked already: nothing more while it holds */
    for (int k = 0; k < 10; k++) { H->frame++; tick(eye, target, veh); }
    assert(t_allocs == 1 && t_locked == 1);
    /* the game resets it (after the shot): the next missile waits its own lock time, then the same holder is kept, locked again */
    *(int *)(ctrl + 0xc) = 1; *(float *)(ctrl + 8) = 0;
    unsigned at = H->frame;
    while (t_locked == 1 && H->frame - at < 60) { H->frame++; tick(eye, target, veh); }
    assert(t_locked == 2 && H->frame - firstLock >= 29 && t_allocs == 1);   /* (its own lock time, counted from the last lock - the shot) */
    /* another vehicle: a new holder replaces the old one (deleted) after the lock time on it */
    *(int *)(ctrl + 0xc) = 1; *(float *)(ctrl + 8) = 0; at = H->frame;
    while (t_allocs == 1 && H->frame - at < 60) { H->frame++; tick(eye, target, veh2); }
    assert(t_allocs == 2 && t_deletes == 1 && lock_holder_vehicle(*(void **)(ctrl + 0x54)) == veh2 && H->frame - at >= 29);
    puts("PASS: a lock is put in place once per shot (the next one waits its own lock time); another vehicle - a new holder replaces the old one, deleted as the game deletes it");
    /* a dead vehicle, a lapsed aim, LockOnAssist=0: nothing */
    *(int *)(ctrl + 0xc) = 1; *(float *)(ctrl + 8) = 0; *(float *)(veh + 0x128) = 0; unsigned a0 = t_allocs, l0 = t_locked;
    for (int k = 0; k < 60; k++) { H->frame++; tick(eye, target, veh); }
    assert(t_allocs == a0 && t_locked == l0);
    *(float *)(veh2 + 0x128) = 800.0f; lock_want_ent(1, eye, target, 4.0f, veh2); H->frame += 40;   /* (the aim left it 40 frames ago) */
    { float l = sqrtf(40 * 40 + 300 * 300); weapon_at(0.3f, 1.5f, 0.2f, 0, (40 - 1.6f) / l, 300 / l); }
    stack[2] = (unsigned)(size_t)xf; on_lock_update((char *)ctrl, stack); assert(t_locked == l0);
    g_enhanced.lockAssist = 0;
    for (int k = 0; k < 60; k++) { H->frame++; tick(eye, target, veh2); }
    assert(t_locked == l0);
    puts("PASS: no lock put in place on a dead vehicle, when the aim has lapsed (want older than 3 frames), or with LockOnAssist=0");
    return 0;
}
'''

def run(cmd, cwd=None):
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
    if r.returncode:
        raise SystemExit('FAILED: ' + ' '.join(map(str, cmd)) + '\n' + r.stdout + r.stderr)
    return r.stdout

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--cc', default='gcc'); ap.add_argument('--sanitize', action='store_true'); args = ap.parse_args()
    san = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'] if args.sanitize else []
    lib = [str(SRC / 'enhanced.c'), str(SRC / 'air.c'), str(SRC / 'modes.c')]
    core = (SRC / 'core.c').read_text()
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        # 1. the drive simulation
        sim = td / 'sim'
        run([args.cc, *san, '-std=gnu11', '-O1', '-Wall', '-I', str(SRC), str(VAL / 'v29_drive_sim.c'), *lib, '-lm', '-o', str(sim)])
        for m in MAPS:
            for line in run([str(sim), str(TRACES / (m + '.map'))]).strip().splitlines():
                print(line)
        print('PASS: V29 driving on eight maps (tank and jeep) - no stop-go, full throttle most of the time, braking for real bends only, a stopped vehicle passed with room to spare')
        # 2. the pure parts
        (td / 'pure.c').write_text(PURE)
        run([args.cc, *san, '-std=gnu11', '-O1', '-Wall', '-I', str(SRC), str(td / 'pure.c'), *lib, '-lm', '-o', str(td / 'pure')])
        print(run([str(td / 'pure')]).strip())
        # 3. the lock put in place
        lw = (SRC / 'lock_warn.h').read_text().replace('#pragma once', ''); lc = core.replace('#define ADDR_LOCK_CTRL_UPDATE', '/*V34LW*/' + lw + '#define ADDR_LOCK_CTRL_UPDATE', 1)   # (V34: the lock-warning table the hook feeds)
        a = lc.index('/*V34LW*/'); b = lc.index('extern void hook_lock_update(void);')
        code = lc[a:b]
        code = code.replace('static float g_lockXf[MAX_BOTS][16] __attribute__((aligned(16)));', 'static float (*g_lockXf)[16];   /* (test: below 4 GB, as on the server) */')
        for old, new in [('((GameAlloc_t)ADDR_GAME_ALLOC)((void *)ADDR_GAME_HEAP, 8, 0x10, 0)', 't_alloc()'),
                         ('((WeakRefInit_t)ADDR_WEAKREF_INIT)((void **)w + 1, t)', 't_weakref((void **)w + 1, t)'),
                         ('((HolderDelete_t)(*(void ***)cur)[0])(cur, 1)', 't_delete(cur)'),
                         ('((LockSetLocked_t)ADDR_LOCK_SET_LOCKED)(ctrl, 1)', 't_set_locked(ctrl)')]:
            assert old in code, old
            code = code.replace(old, new)
        (td / 'lock.c').write_text(LOCK.replace('@@CODE@@', code))
        lsan = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-sanitize=alignment'] if args.sanitize else []
        run([args.cc, *lsan, '-std=gnu11', '-O1', '-w', str(td / 'lock.c'), '-lm', '-o', str(td / 'lock')])
        out = subprocess.run([str(td / 'lock')], capture_output=True, text=True, env={'ASAN_OPTIONS': 'detect_leaks=0'})
        if out.returncode: raise SystemExit('FAILED: lock harness\n' + out.stdout + out.stderr)
        print(out.stdout.strip())
        # 4. jets and their area
        jet = td / 'jet'
        run([args.cc, *san, '-std=c11', '-O1', '-Wall', '-Wno-misleading-indentation', '-I', str(SRC), str(VAL / 'v29_jet_area_sim.c'), *lib, '-lm', '-o', str(jet)])
        print(run([str(jet)]).strip())
        print('PASS: jets keep to their area (flags, bases, airfields + 350 m) - flying out they turn back; strike runs and dogfights stay inside, still diving, gunning and firing missiles')
    # 5. source
    en = (SRC / 'enhanced.c').read_text(); eh = (SRC / 'enhanced.h').read_text(); air = (SRC / 'air.c').read_text(); an = (SRC / 'air_native.inc').read_text()
    orders = (SRC / 'drive_orders.inc').read_text()
    sc = (ROOT / 'files/ServerControl/ServerControl-Core.ps1').read_text()
    dt = core[core.index('static void drive_tick(int i)'):]
    dt = dt[:dt.index('\n// ---- Tickets')]
    assert 'drive_wait_turn(' not in core and 'static int drive_wait_turn' not in core
    assert 'for (int r = 0; r < 7 && !onRoad; r++)' in dt and 'if (drive_hit_vehicle(body, d->veh)) continue;' in dt
    assert 'if (!isBoat) drive_lane_scan(i, d, vp, xf, profile);' in dt and 'd->laneLimit < vwant) vwant = d->laneLimit;' in dt
    assert 'if (!d->onRoadNow && d->brakeNear < 99) vwant = fminf(vwant, bm_drive_obstacle_speed(d->brakeNear, d->speed, isBoat));' in dt
    assert 'throttle = bm_drive_throttle(vwant, d->speed, bm_armored(profile), ae);' in dt and 'vwantHoldUntil' not in dt
    assert 'if (!bm_lane_relation(d->pts, d->n, d->next, vp, fwd2, op, half, range, &along, &lat)) continue;' in core
    assert 'aimP[0] += xf[10] / hl0 * d->passSide * d->passLat; aimP[2] -= xf[8] / hl0 * d->passSide * d->passLat;' in dt
    assert 'with the throttle down (%.0f -> %.0f m/s)' in dt and 'full throttle %.0f%%, braking %.0f%%' in dt
    assert 'p->action == EIA_JUMP || p->action == EIA_CHANGE_POSE || p->action == EIA_PRONE || p->action == g_crouchAction' in core
    assert "!(g_driving[i] && player_vehicle(H->bots[i]))) { *(unsigned *)((char *)in + OFF_INPUT_FLAGS_A) |= SPRINT_BIT_A;" in core
    assert 'memset(H->botMove[i],0,sizeof H->botMove[i]);' not in orders.split('static int drive_clear_departure')[0]
    assert 'H->botMove[i][0] = d->lastThrottle; d->checkAt = H->frame; return;' in core and 'd->lastThrottle = throttle;' in dt
    print('PASS: V29 driving source - no waiting turns; nothing static counts on a recorded road (vehicles by geometry: the lane scan along the route); '
          'the throttle from bm_drive_throttle with no smoothing; passing aims beside the stopped vehicle; drivers never jump, crouch, go prone or sprint; '
          'the director and re-plans keep the last throttle; 20 s statistics and the speed drops with the throttle down are logged')
    assert 'if (airframe && seat > 0 && !guided) tolerance = fmaxf(tolerance, fmaxf(0.03f, atanf((isVeh ? 5.0f : 3.0f) / fmaxf(1, dh))));' in core
    assert 'if (vehicle_is_aircraft(veh)) { g_seatYaw[i] = (g_gunTries[i] & 1) ? 1.0f : -1.0f; g_gunStepAt[i] = H->frame + 12; }' in core
    assert 'else if (driverWeapon && vprofile->kind == BM_AA && targetAir && g_gunNoMissiles[i] != veh) {' in core
    print('PASS: V29 gunners - a helicopter door gun fires within 3 m of a man / 5 m of a vehicle (1.7 deg at least), its weapon found with a full 0.4 s nudge; an AA vehicle tries its missiles 4 s in 12 at aircraft')
    assert 'lock_want_ent(i, eye, aim, 4.0f, g_lockSince[i] ? veh : NULL);' in core
    assert "lock_want_ent(i, gp, aim, 10.0f, isVeh && fabsf(err) < 0.06f && fabsf(perr) < 0.06f ? g_gunTargetVeh[i] : NULL);" in core
    assert 'AIR_LOCK_WANT_ENT(i,axf+12,to,15.0f,g_air[i].targetVeh);' in an and '#define AIR_LOCK_WANT_ENT(i, from, to, r, t) lock_want_ent(i, from, to, r, t)' in core
    assert '        lock_fake(i, ctrl);   // AltBotMod V29' in core and 'FORGET(g_lockWantEnt);FORGET(g_lockWantEntSince);FORGET(g_lockWantEntAt);FORGET(g_lockFakeAt);FORGET(g_gunNoMissiles);' in core
    assert '#define ADDR_GAME_ALLOC         0x00430700' in core and '#define ADDR_LOCK_TARGET_VT     0x025ba2a4' in core and '#define ADDR_WEAKREF_INIT       0x00d7a030' in core and '#define ADDR_LOCK_SET_LOCKED    0x00dd27a0' in core
    assert 'BOOL_KEY("LockOnAssist", lockAssist)' in en and 'c->lockAssist = 1;' in en and "New-BmSetting 'LockOnAssist'" in sc
    print('PASS: V29 lock-ons - launchers, gunners (AA missiles, a gunship gunner\'s guided missile) and aircraft (heat seekers) name the vehicle they are on; the lock is put in place (LockOnAssist, on by default, in Server Control)')
    assert 'if(o->areaValid && jet_area_tick(s,o,goal,top,floorY,dt,out)) return;' in air and '#define JET_IP_DISTANCE    1700.0f' in air and 'if(o->areaValid){   // (V29: inside the combat area, 400 m from its edge)' in air and '#define JET_EXTEND_MAX     5.0f' in air
    assert 'air_area(i,&o);' in an and 'FLOAT_KEY("JetAreaMarginMeters", air.jetAreaMargin, 0, 3000)' in en and 'c->jetAreaMargin=900;' in air   # (V34: 900 m; V30 600 m)
    assert "New-BmSetting 'JetAreaMarginMeters'" in sc
    assert '#define BM_ENHANCED_VERSION "AltBotMod V34 - Squad Up"' in eh
    print('PASS: V29 jets - the area (flags, bases, airfields + JetAreaMarginMeters, in Server Control), strike runs set up inside it, extends of 5 s at most (7); version V29')

if __name__ == '__main__':
    main()
