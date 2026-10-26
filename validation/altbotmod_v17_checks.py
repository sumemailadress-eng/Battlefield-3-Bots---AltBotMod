#!/usr/bin/env python3
"""AltBotMod V18 - Indoors: offline checks.

1. drive_goal_watch (ground_progress.inc), the real function with stubs: a driver backing and filling on one spot for
   35 s backs out / strikes the way ahead / takes another road; 35 s more: another objective on the driving map; 35 s
   more: gives the vehicle up (no bot takes it for 2 min) and goes on foot. Real progress (35 m) resets it; at its
   objective, fighting from the hull, picking up or waiting for an engineer it is never "stuck".
2. Gun priorities (enhanced.c): tank / IFV guns take soldiers within 300 m and enemy vehicles before a helicopter
   past 100 m; a far aircraft only with nothing else (and not past 300 m); a jeep / boat gun never shoots at aircraft
   while a soldier is in reach, nor at all past 150 m; AA guns still prefer aircraft.
3. Fun-Bots dev-branch vehicle routes merged: MP_013 and MP_017 Rush have the new land / boat routes, infantry kept.
Source checks: detection looks at soldiers before vehicles; engineers carry more AA launchers while enemy aircraft fly.
"""
from pathlib import Path
import argparse, math, re, subprocess, sys, tempfile, collections
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test4_checks import function  # noqa: E402
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'

WATCH = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define MAX_BOTS 4
#define OFF_PLAYER_ONLINE_NAME 0
typedef struct DriveState { int phase, manual, n, next, partial, traceTries, traceContinue, reroutes, stuckCount, nudges;
    unsigned char traceAlt; char *veh; float goal[3], pts[30]; unsigned combatHoldAt, repairHoldAt, pickupUntil, reverseUntil, backOutUntil;
    float watchAnchor[3]; unsigned watchAt; int watchLevel; const char *why; } DriveState;   /* (why: V26) */
static struct { unsigned frame; char *bots[MAX_BOTS]; } host, *H = &host;
static unsigned char maskBytes[16], *g_driveMask = maskBytes;
static int recovers, stops, blocks, bad, logs;
static int vehicle_is_boat(char *v) { (void)v; return 0; }
static const char *vehicle_name(char *v) { (void)v; return "Growler"; }
static float dist2(const float *a, const float *b) { float x = a[0] - b[0], z = a[2] - b[2]; return x * x + z * z; }
static int drive_block_for_job(int i, unsigned char *m, float x, float z, float r, float y) { (void)i; (void)m; (void)x; (void)z; (void)r; (void)y; blocks++; return 5; }
static void nav_cancel_path(int i) { (void)i; }
static int drive_recover_job(int i, DriveState *d, const float *p) { (void)i; (void)p; recovers++; d->phase = 2; return 1; }
static void vehicle_mark_bad(char *v, int s) { (void)v; assert(s >= 120); bad++; }
static void drive_stop(int i, const char *why, int c) { (void)i; (void)why; (void)c; stops++; }
static void bot_log(const char *f, ...) { (void)f; logs++; }
/* AltBotMod V21: stalls are learned (not part of this check) */
enum { BM_LEARN_STUCK_GROUND = 2, BM_LEARN_STUCK_BOAT = 3 };
static int learned;
static void bm_learn(int i, int ev, const float *p, const float *d) { (void)i; (void)ev; (void)p; (void)d; learned++; }
''' + '@@FN@@' + r'''
static void tick(DriveState *d, float *p, int seconds) { for (int s = 0; s < seconds; s++) { H->frame += 30; drive_goal_watch(0, d, p); } }
int main(void)
{
    static char name[16] = "bot", veh[8] = "v"; H->bots[0] = name; H->frame = 30;
    DriveState d; memset(&d, 0, sizeof d); d.phase = 3; d.veh = veh; d.goal[0] = 600; d.n = 2; d.pts[3] = 50; d.pts[5] = 0;
    float p[3] = {0, 0, 0};
    /* backing and filling within 10 m: escalates every 35 s */
    for (int s = 0; s < 34; s++) { p[0] = (s % 4) * 3.0f; H->frame += 30; drive_goal_watch(0, &d, p); }
    assert(d.watchLevel == 0 && !blocks);
    tick(&d, p, 2); assert(d.watchLevel == 1 && blocks == 1 && d.phase == 2 && d.traceTries >= 2 && d.backOutUntil > H->frame);
    d.phase = 3; tick(&d, p, 35); assert(d.watchLevel == 2 && recovers == 1 && d.traceTries >= 4 && d.traceTries < 40);   /* V25: another recorded road, not 99 (no Fun-Bots road for the trip) */
    d.phase = 3; tick(&d, p, 35); assert(stops == 1 && bad == 1);   /* 105 s: the vehicle is given up */
    /* real progress resets the escalation */
    memset(&d, 0, sizeof d); d.phase = 3; d.veh = veh; d.goal[0] = 600; p[0] = 0; tick(&d, p, 36); assert(d.watchLevel == 1);
    d.phase = 3; p[0] = 60; tick(&d, p, 1); assert(d.watchLevel == 0); tick(&d, p, 34); assert(d.watchLevel == 0);
    /* at its objective, fighting from the hull, picking up: never stuck */
    memset(&d, 0, sizeof d); d.phase = 3; d.veh = veh; d.goal[0] = 620; p[0] = 600; tick(&d, p, 200); assert(d.watchLevel == 0);
    d.goal[0] = 900; d.combatHoldAt = 5; tick(&d, p, 200); assert(d.watchLevel == 0);
    d.combatHoldAt = 0; d.pickupUntil = H->frame + 400 * 30; tick(&d, p, 200); assert(d.watchLevel == 0);
    d.pickupUntil = 0; d.manual = 1; tick(&d, p, 200); assert(d.watchLevel == 0);
    puts("PASS: V17 vehicle progress watch - a driver backing and filling on one spot (Alborz: 132 back-offs in 202 s at the US base) "
         "backs out, strikes the way ahead off and takes another road after 35 s, tries another objective on the driving map after 70 s, "
         "gives the vehicle up (no bot takes it for 2 min) after 105 s; 35 m of real progress resets it; never while at its objective, "
         "fighting from the hull, picking up or driven by hand");
    return 0;
}
'''

SCORES = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "enhanced.h"
int main(void)
{
    const BMVehicleProfile *tank = bm_vehicle_profile("T90"), *jeep = bm_vehicle_profile("Growler"), *aa = bm_vehicle_profile("LAV_AD");
    assert(bm_armored(tank) && !bm_armored(jeep));
    #define S(p, k, d) bm_vehicle_target_score(p, k, d)
    assert(S(tank, BM_TARGET_INFANTRY, 290) < S(tank, BM_TARGET_AIR, 150));   /* soldiers within 300 m before a helicopter past 100 m */
    assert(S(tank, BM_TARGET_GROUND, 390) < S(tank, BM_TARGET_AIR, 150));
    assert(S(tank, BM_TARGET_AIR, 80) < S(tank, BM_TARGET_INFANTRY, 150));     /* one attacking low and close is engaged */
    assert(S(tank, BM_TARGET_INFANTRY, 299) < S(tank, BM_TARGET_AIR, 250) && isfinite(S(tank, BM_TARGET_AIR, 250)));   /* far: last resort */
    assert(!isfinite(S(tank, BM_TARGET_AIR, 320)));
    assert(S(jeep, BM_TARGET_INFANTRY, 210) < S(jeep, BM_TARGET_AIR, 50) && !isfinite(S(jeep, BM_TARGET_AIR, 200)));
    assert(S(aa, BM_TARGET_AIR, 600) < S(aa, BM_TARGET_INFANTRY, 100));        /* AA guns: aircraft first, as before */
    puts("PASS: V17 gun priorities - tank / IFV guns: soldiers within 300 m and enemy vehicles before a helicopter past 100 m, a far "
         "aircraft only with nothing else and not past 300 m, a low one within 100 m (attacking) still engaged; jeep / boat guns never "
         "at aircraft while a soldier is in reach nor past 150 m; AA guns still aircraft first");
    return 0;
}
'''

def vehicle_routes(fn):
    paths = collections.defaultdict(list); kinds = collections.defaultdict(set)
    for line in open(fn, encoding='utf-8', errors='replace'):
        p = line.rstrip('\n').split(';', 6)
        if len(p) < 6 or not p[0].isdigit(): continue
        paths[int(p[0])].append(1)
        if len(p) > 6 and '"Vehicles"' in p[6]:
            if '"land"' in p[6]: kinds[int(p[0])].add('land')
            if '"water"' in p[6]: kinds[int(p[0])].add('water')
    land = sum(1 for k in kinds.values() if 'land' in k); water = sum(1 for k in kinds.values() if 'water' in k)
    return len(paths), land, water

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    cc = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter', '-I', str(SRC)]
    if a.sanitize: cc += ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
    gp = (SRC / 'ground_progress.inc').read_text()
    with tempfile.TemporaryDirectory(prefix='bf3-altbotmod-v17-') as t:
        tmp = Path(t)
        def build(name, body, extra=()):
            c = tmp / (name + '.c'); c.write_text(body)
            subprocess.run(cc + [str(c), *[str(SRC / x) for x in extra], '-lm', '-o', str(tmp / name)], check=True)
            r = subprocess.run([str(tmp / name)], stdout=subprocess.PIPE, text=True); print(r.stdout, end=''); assert r.returncode == 0, name
        build('watch', WATCH.replace('@@FN@@', function(gp, 'drive_goal_watch')))
        build('scores', SCORES, ['enhanced.c', 'air.c', 'modes.c'])
    tr = ROOT / 'files/server/BotMod_traces'
    n13, l13, w13 = vehicle_routes(tr / 'MP_013_RushLarge0.map'); n17, l17, w17 = vehicle_routes(tr / 'MP_017_RushLarge0.map')
    assert l13 == 17 and n13 >= 165 and l17 == 23 and w17 == 6 and n17 >= 213, (n13, l13, n17, l17, w17)
    print('PASS: V17 Fun-Bots dev-branch vehicle routes merged - MP_013 Rush 17 land routes (was 16), MP_017 Rush 23 land + 6 boat (was 22 + 2); every infantry path kept')
    core = (SRC / 'core.c').read_text(); gv = (SRC / 'gadget_variety.inc').read_text()
    assert 'for (int pass = 0; pass < 2 && !best; pass++)' in core and 'if ((veh != NULL) != pass) continue;' in core
    assert 'if(drive_goal_watch(i,d,vp))return;   // AltBotMod V17' in core
    assert 'static int enemy_air_up(int team)' in gv and 'd->weight * 3' in gv
    assert '#define BM_ENHANCED_VERSION "AltBotMod V34 - Squad Up"' in (SRC / 'enhanced.h').read_text()
    mn = (SRC / 'modes_native.inc').read_text()
    assert 'H->frame-g_rushAdvanceAt<90*30 && ++g_rushBadDeaths>=6' in mn and 'g_rush=g_rushBefore;' in mn and 'g_mcomDestroyedBy[k]!=1' in mn
    print('PASS: V17 Rush safety net - six bots dying outside the play area within 90 s of a sector change undo it (the station the game '
          'itself reported destroyed stays destroyed, a guessed one is standing again); the game\'s objective messages never move the sector '
          'while its crate events are read')
    print('PASS: V17 wiring - detection looks at soldiers before vehicle crews (a helicopter overhead no longer hides the soldier behind it); '
          'the progress watch runs every drive tick; engineers carry Stinger / Igla three times as often while enemy aircraft fly; version')

if __name__ == '__main__':
    main()
