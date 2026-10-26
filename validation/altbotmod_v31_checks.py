#!/usr/bin/env python3
"""AltBotMod V31 - Solid Ground: offline checks. V31 is V29 plus a short list of V30 features; everything that touched the
vehicles' input or the bots' unlock list in V30 / V30.1 is gone (V30 crashed the server; V30.1 too).

1. Back to V29: the vehicle input feed is V29's (one direct call per bot frame through the vehicle's vtable) with only the
   read-only driver trace added; no vtable hooks on the vehicle; the loadout loader is V29's line for line (no vehicle
   unlocks appended); aircraft secondaries as in V29; the lock controller matched as in V29.
2. The driver trace (vring_record / vring_dump).
3. Lock-ons: LockOnSeconds (0.5 s) - a weapon with a 2 s lock time locks after 0.5 s on target, not before.
4. Swimming and getting off a cut-off spot (sims), nav_water_along on the real Wake Island grid.
5. The exit guard: no game exit for a bot its vehicle no longer seats, nor twice in a frame.
6. Rush route coverage; door guns (arc, DoorGunMeters); jets' area; settings; the version.
"""
from pathlib import Path
import argparse, re, subprocess, tempfile, sys
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
VAL = ROOT / 'validation'
SERVER = ROOT / 'files/server'
V29SRC = VAL / 'fixtures/v29_src'   # (AltBotMod V29's sources, as packaged)
sys.path.insert(0, str(VAL))

def run(cmd, cwd=None):
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd, env={'ASAN_OPTIONS': 'detect_leaks=0', 'PATH': '/usr/bin:/bin'})
    if r.returncode:
        raise SystemExit('FAILED: ' + ' '.join(map(str, cmd)) + '\n' + r.stdout + r.stderr)
    return r.stdout

VRING = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#define MAX_BOTS 4
#define OFF_VEHICLE_BODY 0x230
#define OFF_VEHBODY_POSITION 0x100
#define OFF_PLAYER_SOLDIER 0x10
#define OFF_SOLDIER_SPAWN_POSITION 0x4b0
#define OFF_PLAYER_ONLINE_NAME 0x40
typedef unsigned long SIZE_T;
static int IsBadReadPtr(const void *p, SIZE_T n) { (void)n; return p == NULL; }
static struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; } host, *H = &host;
static double clock_ms;
static double perf_now(void) { return clock_ms; }
static int lines; static char last[512], first[512];
static void bot_log(const char *f, ...) { va_list a; va_start(a, f); vsnprintf(last, sizeof last, f, a); va_end(a); if (lines == 1) strcpy(first, last); lines++; }
@@CODE@@
int main(void)
{
    static char veh[0x400], body[0x200], player[0x100], soldier[0x600], in[0x200];
    *(char **)(veh + OFF_VEHICLE_BODY) = body; *(char **)(player + OFF_PLAYER_SOLDIER) = soldier; strcpy(player + OFF_PLAYER_ONLINE_NAME, "Bot");
    H->bots[0] = player; H->botCount = 1;
    for (int f = 1; f <= 100; f++) {
        H->frame = f; clock_ms = f * 33.3;
        ((float *)(body + OFF_VEHBODY_POSITION))[0] = (float)f; ((float *)(in + 0x10))[0] = 1.0f; *(float *)(in + 0x1ec) = 1.0f / 30;
        vring_record(0, veh, in);
    }
    vring_dump(0);
    assert(lines == 1 + VRING && strstr(first, "vtrace: 56 ") && strstr(first, "body 56.00") && strstr(last, "vtrace: 100 ") && strstr(last, "in 1.00") && strstr(last, "dt 0.0333"));
    for (int k = 0; k < 10; k++) vring_dump(0);
    assert(lines == 6 * (1 + VRING));   /* six traces a level at most */
    puts("PASS: the driver trace - the last 45 frames, oldest first (body / seat / physics position, input levels, dt), six a level at most");
    return 0;
}
'''

SWIM = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define MAX_BOTS 4
#define NAV_HIT_WATER 3
#define EIA_JUMP 12
#define OFF_PLAYER_ONLINE_NAME 0
#define OFF_SOLDIER_POSITION 0
#define OFF_SOLDIER_YAW 16
static struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; float botMove[MAX_BOTS][3]; } host, *H = &host;
typedef struct { int phase; } NavGrid;
static NavGrid grid = {2}, *gridp = &grid;
static NavGrid **nav_slot(void) { return &gridp; }
typedef struct { int targetFlag; } BotBrain;
static BotBrain brains[MAX_BOTS];
static BotBrain *bot_brain(int i) { return &brains[i]; }
typedef struct { float targetYaw; } BotNav;
static BotNav navs[MAX_BOTS];
static BotNav *bot_nav(int i) { return &navs[i]; }
typedef struct { unsigned frame; float x, z; } Progress;
static Progress g_progress[MAX_BOTS];
static float flags[2][3] = {{0, 0, 300}, {400, 0, 0}};
static int g_flagCount = 2;
static float *flag_pos(int f) { return flags[f]; }
static int objective_count(void) { return 2; }
static const float *objective_pos(int f) { return flags[f]; }
static char *vehicles[MAX_BOTS];
static char *player_vehicle(char *pl) { (void)pl; return vehicles[0]; }
static float yaw_towards(float dx, float dz) { return atan2f(dx, dz); }
static int reroutes, jumps; static char lastWhy[64];
static void reroute_from_here(int i, char *s, const char *why) { (void)i; (void)s; reroutes++; snprintf(lastWhy, sizeof lastWhy, "%s", why); }
static void press_button(int i, int a, int f) { (void)i; (void)f; if (a == EIA_JUMP) jumps++; }
static void bot_log(const char *f, ...) { (void)f; }
/* the world: sea below y = 0 where z < 200 (and x > -50), a beach beyond; a rock at (0, 100) r 8; a deck at y 20 over x < -50 */
static float sx[6];   /* soldier: pos[3], pad, yaw */
static int g_lastHit;
static int in_sea(float x, float z) { return z < 200 && x > -50; }
static int ray_cast(const float *a, const float *b, float *hit, float *n)
{
    (void)n; g_lastHit = 0;
    if (!in_sea(a[0], a[2])) return 0;
    if (a[1] > 0 && b[1] <= 0) { hit[0] = a[0]; hit[1] = 0; hit[2] = a[2]; g_lastHit = NAV_HIT_WATER; return 1; }
    return 0;
}
static int nav_hit_class(void) { return g_lastHit; }
static float nav_water_along(const NavGrid *g, float x0, float z0, float x1, float z1, float range)
{
    (void)g; float dx = x1 - x0, dz = z1 - z0, l = sqrtf(dx * dx + dz * dz);
    for (float d = 2; d <= range && d <= l; d += 4) if (in_sea(x0 + dx / l * d, z0 + dz / l * d)) return d;
    return -1;
}
@@DECL@@
@@CODE@@
static float wrapneg(float y) { y = fmodf(y, 6.2831853f); if (y > 0) y -= 6.2831853f; return y; }
static int blocked(float x, float z) { return hypotf(x - 0, z - 100) < 8; }
/* one frame of the soldier: turn by the yaw input, move 2 m/s forward (swimming) or 4 m/s (on foot), feet at -1.5 in the sea */
static void step(void)
{
    float *p = sx, *yaw = sx + 4;
    *yaw = wrapneg(*yaw - H->botMove[0][2] * 0.1f);
    float v = H->botMove[0][0] * (in_sea(p[0], p[2]) ? 2.0f : 4.0f) / 30.0f;
    float nx = p[0] + sinf(*yaw) * v, nz = p[2] + cosf(*yaw) * v;
    if (!blocked(nx, nz)) { p[0] = nx; p[2] = nz; }
    if (p[0] < -50 && p[1] > 5) p[1] = 20;                     /* on the deck */
    else p[1] = in_sea(p[0], p[2]) ? -1.5f : 0.5f;            /* over the side: in the sea; ashore: on the beach */
    H->frame++;
}
int main(void)
{
    static char bot0[16] = "b0"; H->botCount = 1; H->bots[0] = bot0; H->frame = 1000;
    char *soldier = (char *)sx;
    /* 1. in the sea at (0, 20) heading away (south): swims north for flag 0 at (0, 300), out on the beach at z 200 */
    brains[0].targetFlag = 0; sx[0] = 0; sx[1] = -1.5f; sx[2] = 20; sx[4] = wrapneg(3.14159f);
    int frames = 0, started = 0;
    while (frames < 30 * 240 && !reroutes) { swim_steer_tick(0, soldier); started |= g_swimming[0]; step(); frames++; }
    assert(started && reroutes == 1 && !strcmp(lastWhy, "out of the water") && sx[2] >= 199 && fabsf(sx[0]) < 25);
    printf("PASS: a bot in the sea heading the wrong way turns and swims for its objective - ashore after %.0f s (180 m at 2 m/s), its route planned again there\n", frames / 30.0f);
    /* 2. a rock straight in the way at (0, 100): turns off, gets round it, still ashore */
    memset(g_swimming, 0, sizeof g_swimming); g_swimCheckAt[0] = 0; reroutes = 0;
    sx[0] = 0; sx[1] = -1.5f; sx[2] = 60; sx[4] = 0; frames = 0;
    while (frames < 30 * 300 && !reroutes) { swim_steer_tick(0, soldier); step(); frames++; }
    assert(reroutes == 1 && sx[2] >= 199);
    printf("PASS: a rock in the way - the swimmer turns off for 2 s and gets round it, ashore after %.0f s\n", frames / 30.0f);
    /* 3. on dry land: nothing is touched */
    memset(g_swimming, 0, sizeof g_swimming); g_swimCheckAt[0] = 0; reroutes = 0;
    sx[0] = 0; sx[1] = 0.5f; sx[2] = 250; H->botMove[0][0] = 0.25f; H->botMove[0][2] = 0.5f;
    for (int k = 0; k < 90; k++) { swim_steer_tick(0, soldier); H->frame++; }
    assert(!g_swimming[0] && H->botMove[0][0] == 0.25f && H->botMove[0][2] == 0.5f && !reroutes);
    puts("PASS: on dry land the swim code leaves the bot's own steering alone");
    /* 4. cut off on a deck at (-80, 20, 150), 20 m up: three failed routes in 20 s -> over the side towards flag 1, then swims */
    memset(g_swimming, 0, sizeof g_swimming); g_swimCheckAt[0] = 0; reroutes = 0;
    sx[0] = -80; sx[1] = 20; sx[2] = 150; sx[4] = wrapneg(-1.5708f); brains[0].targetFlag = 1;
    escape_consider(0, soldier, 1); escape_consider(0, soldier, 1); assert(!g_escapeUntil[0]);
    H->frame += 30; escape_consider(0, soldier, 1); assert(g_escapeUntil[0] > H->frame);
    frames = 0; int wet = 0;
    while (frames < 30 * 60 && !wet) { swim_steer_tick(0, soldier); wet = g_swimming[0]; step(); frames++; }
    assert(wet && !g_escapeUntil[0] && sx[0] > -50);
    printf("PASS: a bot cut off on a deck (no route to any objective 3 times in 20 s, water on the way) walks over the side - in the water after %.1f s, swimming from there\n", frames / 30.0f);
    /* no water on the way (flag 0 straight across the deck - here: a goal on land): no escape */
    memset(g_escapeUntil, 0, sizeof g_escapeUntil); memset(g_escapeFails, 0, sizeof g_escapeFails); memset(g_escapeFailAt, 0, sizeof g_escapeFailAt);
    flags[0][0] = -300; flags[0][2] = 150; sx[0] = -80; sx[1] = 20; sx[2] = 150; memset(g_swimming, 0, sizeof g_swimming);
    for (int k = 0; k < 3; k++) { escape_consider(0, soldier, 0); H->frame += 10; }
    assert(!g_escapeUntil[0]);
    puts("PASS: no escape when there is no water between the bot and the objective (the usual route recovery)");
    return 0;
}
'''

NAVWATER = r'''
#include <assert.h>
#include <stdio.h>
#include "nav.h"
int main(int argc, char **argv)
{
    NavGrid *g = nav_load(argv[1]); assert(g);
    /* Wake Island (XP1_004): the US carrier deck where V29 bots stood ("A* exhausted after 3695 nodes") and the flags */
    float carrier[2] = {-735, -573};
    float flags[5][2] = {{301, -188}, {8, -205}, {340, 234}, {149, 324}, {-38, 189}};
    for (int f = 0; f < 5; f++) {
        float w = nav_water_along(g, carrier[0], carrier[1], flags[f][0], flags[f][1], 300);
        assert(w >= 0 && w < 300);
    }
    /* no line, or a line off the grid: no water */
    assert(nav_water_along(g, flags[4][0], flags[4][1], flags[4][0], flags[4][1], 300) < 0);
    assert(nav_water_along(g, 90000, 90000, 91000, 91000, 300) < 0);
    puts("PASS: nav_water_along on the real Wake Island grid - water within 300 m between the US carrier and every flag; none for no line or off the grid");
    return 0;
}
'''

def function(src, name):
    i = src.index(name)
    j = src.index('\n}\n', i)
    return src[i:j + 3]

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--cc', default='gcc'); ap.add_argument('--sanitize', action='store_true'); args = ap.parse_args()
    san = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-sanitize=alignment'] if args.sanitize else []
    core = (SRC / 'core.c').read_text()
    an = (SRC / 'air_native.inc').read_text(); air = (SRC / 'air.c').read_text()
    en = (SRC / 'enhanced.c').read_text(); eh = (SRC / 'enhanced.h').read_text()
    gt = (SRC / 'gunner_targets.inc').read_text(); nav = (SRC / 'nav.c').read_text()
    sc = (ROOT / 'files/ServerControl/ServerControl-Core.ps1').read_text()
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        # 1. back to V29
        v29core = (V29SRC / 'core.c').read_text()
        assert 'hook_vehicle_entry' not in core and 'patch_vslot(ADDR_VEHICLE_VTABLE + 0x144' not in core and 'patch_vslot(ADDR_VEHICLE_VTABLE + 0x148' not in core
        site = core[core.index('        if (g_vehInput) {   // in a seat: nobody feeds'):]; site = site[:site.index('        if (g_vaimOn[i] && !air_pilot(i)) {')]
        old = v29core[v29core.index('        if (g_vehInput) {   // in a seat: nobody feeds'):]; old = old[:old.index('        if (g_vaimOn[i] && !air_pilot(i)) {')]
        assert site.replace('                if (seat == 0 && g_driving[i]) vring_record(i, veh, in);   // AltBotMod V31: the driver trace (read only)\n', '') == old
        def block(src, a, b):
            x = src.index(a); return src[x:src.index(b, x)]
        assert block(core, "        } else if (L && msg && line[0] == 'A') {", "    if (L) lib->count++;") == block(v29core, "        } else if (L && msg && line[0] == 'A') {", "    if (L) lib->count++;")
        assert 'Persistence/Unlocks/Vehicles/' not in core and 'vehicle_unlock_extras' not in core and 'AIR_KIT_A2G' not in core
        an34 = (SRC / 'air_native.inc').read_text().replace('#include "lock_warn.h"   // AltBotMod V34\n', '')   # (V34 additions taken out again)
        x = an34.index('    // AltBotMod V34: a lock on this aircraft'); an34 = an34[:x] + an34[an34.index('    if (g_enhanced.air.countermeasures && a->lastHealth>0', x):]
        x = an34.index('            // AltBotMod V34: an enemy attack helicopter within 2 km'); an34 = an34[:x] + an34[an34.index('            // AltBotMod V24: an enemy helicopter within 1.2 km', x):]
        assert an34.replace('m,1400.0f);   // (V34: 1400 m - V30 1000 m, V29 800 m)', 'm,800.0f);') == (V29SRC / 'air_native.inc').read_text()
        assert 'g_lockGeoAt' not in core and 'if (fwd[0] * g_lockWantDir[i][0] + fwd[1] * g_lockWantDir[i][1] + fwd[2] * g_lockWantDir[i][2] < 0.85f) continue;' in core
        print('PASS: back to V29 - the vehicle input feed is V29\'s (plus the read-only driver trace), no hooks on the vehicle, the kit loader V29\'s line for line (no vehicle unlocks), aircraft weapons and the lock matching as in V29')
        a = core.index('#define VRING 45'); b = core.index('#define EIA_SEAT_YAW 6')
        (td / 'vring.c').write_text(VRING.replace('@@CODE@@', core[a:b]))
        run([args.cc, *san, '-std=gnu11', '-O1', '-Wall', '-Wno-unused-variable', '-Wno-unused-function', str(td / 'vring.c'), '-o', str(td / 'vring')])
        print(run([str(td / 'vring')]).strip())
        assert 'vring_dump(i);   // AltBotMod V30.1' in core and 'g_vringDumps = 0;' in core
        # 2. lock-ons
        import altbotmod_v29_checks as v29
        lock = v29.LOCK
        lw = (SRC / 'lock_warn.h').read_text().replace('#pragma once', ''); lc = core.replace('#define ADDR_LOCK_CTRL_UPDATE', '/*V34LW*/' + lw + '#define ADDR_LOCK_CTRL_UPDATE', 1)   # (V34: the lock-warning table the hook feeds)
        a = lc.index('/*V34LW*/'); b = lc.index('extern void hook_lock_update(void);')
        code = lc[a:b].replace('static float g_lockXf[MAX_BOTS][16] __attribute__((aligned(16)));', 'static float (*g_lockXf)[16];')
        for old, new in [('((GameAlloc_t)ADDR_GAME_ALLOC)((void *)ADDR_GAME_HEAP, 8, 0x10, 0)', 't_alloc()'),
                         ('((WeakRefInit_t)ADDR_WEAKREF_INIT)((void **)w + 1, t)', 't_weakref((void **)w + 1, t)'),
                         ('((HolderDelete_t)(*(void ***)cur)[0])(cur, 1)', 't_delete(cur)'),
                         ('((LockSetLocked_t)ADDR_LOCK_SET_LOCKED)(ctrl, 1)', 't_set_locked(ctrl)')]:
            assert old in code, old
            code = code.replace(old, new)
        head = lock[:lock.index('int main(void)')].replace('g_enhanced = {1, -1.0f};', 'g_enhanced = {1, 0.5f};')
        main_c = r'''
int main(void)
{
    H->botCount = 2; H->frame = 100; static char name0[16] = "a", name1[16] = "b"; H->bots[0] = name0; H->bots[1] = name1;
    xf = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0); assert(xf != MAP_FAILED);
    g_lockXf = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    float *data = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    data[3] = 2.0f;   /* the weapon's LockTime: 2 s */
    memset(ctrl, 0, sizeof ctrl); *(unsigned *)ctrl = ADDR_LOCK_CTRL_VTABLE; *(unsigned *)(ctrl + 4) = (unsigned)(size_t)data; ctrl[0x11] = 1;
    *(unsigned *)veh = 0x025bf8e8u; *(float *)(veh + 0x128) = 500.0f;
    float eye[3] = {0, 1.6f, 0}, target[3] = {0, 40, 300};
    for (int k = 0; k < 14; k++) { tick(eye, target, veh); H->frame++; }
    assert(t_locked == 0);
    tick(eye, target, veh); H->frame++; tick(eye, target, veh);
    assert(t_locked == 1 && lock_holder_vehicle(*(void **)(ctrl + 0x54)) == veh);
    puts("PASS: LockOnSeconds 0.5 - a weapon with a 2 s lock time locks after 0.5 s on target, not before");
    return 0;
}
'''
        (td / 'lock.c').write_text(head.replace('@@CODE@@', code) + main_c)
        run([args.cc, *san, '-std=gnu11', '-O1', '-w', str(td / 'lock.c'), '-lm', '-o', str(td / 'lock')])
        print(run([str(td / 'lock')]).strip())
        # 3. swimming
        decl = re.search(r'static unsigned char g_swimming\[MAX_BOTS\].*?\nstatic void escape_consider\(int i, char \*soldier, int f\);\n', core, re.S).group(0)
        a = core.index('static int soldier_in_water(const float *p)'); b = core.index('static void bot_motion_tick(int i,void *in)')
        (td / 'swim.c').write_text(SWIM.replace('@@DECL@@', decl).replace('@@CODE@@', core[a:b]))
        run([args.cc, *san, '-std=gnu11', '-O1', '-Wall', '-Wno-unused-function', '-Wno-unused-variable', str(td / 'swim.c'), '-lm', '-o', str(td / 'swim')])
        print(run([str(td / 'swim')]).strip())
        (td / 'navwater.c').write_text(NAVWATER)
        import altbotmod_v13_checks as v13
        (td / 'windows.h').write_text(v13.WINDOWS)   # (the same Win32 shim the V13 checks build nav.c with)
        run([args.cc, *san, '-std=gnu11', '-O1', '-Wall', '-Wno-unused-function', '-Wno-unused-variable', '-I', str(td), '-I', str(SRC), str(td / 'navwater.c'), str(SRC / 'nav.c'), '-lm', '-o', str(td / 'navwater')])
        print(run([str(td / 'navwater'), str(SERVER / 'BotMod_nav_XP1_004.bin')]).strip())
    assert 'swim_steer_tick(i, *(char **)(H->bots[i] + OFF_PLAYER_SOLDIER));   // AltBotMod V30' in core
    assert 'escape_consider(i, soldier, f);   // AltBotMod V30' in core and 'if (b->enroute) { g_noRouteSince[i] = 0; g_escapeFails[i] = 0; }' in core
    assert 'FORGET(g_swimming);' in core and 'FORGET(g_escapeUntil);' in core
    # 7. V31.1 - server load: the gunner's pick traces at most 6 candidates (sim against tracing them all); the line of sight
    #    to the current target is cached for 6 frames; scans every 0.4-0.5 s; off-road bumper rays at 6 Hz under 12 m/s;
    #    route planning 6 ms a tick at most when busy; the perf line splits the input time
    with tempfile.TemporaryDirectory() as td2:
        exe = Path(td2) / 'gp'
        run([args.cc, *san, '-std=gnu11', '-O1', '-Wall', '-Wno-unused-function', '-I', str(SRC), str(VAL / 'v31_gunner_pick.c'), str(SRC / 'enhanced.c'), str(SRC / 'air.c'), '-lm', '-o', str(exe)])
        print(run([str(exe)]).strip())
    assert 'if (who == g_gunSeenFor[i] && g_gunSeenAt[i] && H->frame - g_gunSeenAt[i] < 6) seen = g_gunSeenOk[i];' in core
    assert 'g_gunScanAt[i] = H->frame + 12 + (unsigned)(i % 4); gunner_pick_target(i, gp);' in core
    assert "H->frame-d->sensorAt>=(d->speed>12.0f?3u:5u)" in core and '#define PLAN_MS_BUSY     6.0' in core
    assert 'input split avg/max ms: driving %.2f/%.1f aircraft %.2f/%.1f gunners %.2f/%.1f' in core
    print('PASS: V31.1 server load - gunner sight cached 6 frames, target scans every 0.4-0.5 s, bumper rays at 6 Hz under 12 m/s, route planning 6 ms a busy tick; the perf line splits driving / aircraft / gunners')
    # 4b. Rush: every Rush / Squad Rush route file carries routes to every M-COM and its "interact" point
    rush = sorted((SERVER / 'BotMod_traces').glob('*Rush*.map'))
    assert len(rush) >= 40, len(rush)
    total = 0
    for f in rush:
        tags, inter = {}, set()
        for line in f.read_text(errors='ignore').splitlines():
            p = line.split(';', 6)
            if len(p) < 7 or not p[0].isdigit(): continue
            m = re.search(r'"Objectives":\[([^\]]*)\]', p[6])
            if not m: continue
            for o in re.findall(r'"([^"]*)"', m.group(1)):
                mm = re.match(r'mcom (\d+)( interact)?$', o.lower())
                if mm:
                    k = int(mm.group(1))
                    if mm.group(2): inter.add(k)
                    else: tags.setdefault(k, set()).add(p[0])
        n = max(set(tags) | inter)
        assert n >= 2 and n % 2 == 0, (f.name, n)
        assert all(len(tags.get(k, ())) >= 2 for k in range(1, n + 1)), (f.name, {k: len(tags.get(k, ())) for k in range(1, n + 1)})
        assert all(k in inter for k in range(1, n + 1)), (f.name, sorted(inter))
        total += n
    print('PASS: Rush routes - %d Rush / Squad Rush route files, %d M-COMs: every one has 2+ Fun-Bots routes to it and its arm / disarm point' % (len(rush), total))

    # 5. source
    eg = core[core.index('static void bot_exit_vehicle(int i, int parachute)\n{'):]; eg = eg[:eg.index('\n}\n')]
    assert "if ((g_exitAt[i] && g_exitAt[i] == H->frame) || (nSeats > 0 && vehicle_seat_of(veh, H->bots[i]) < 0)) {" in eg and 'g_exitAt[i] = H->frame ? H->frame : 1;' in eg
    assert eg.index('g_exitAt[i] = H->frame ? H->frame : 1;') < eg.index('((ExitVehicle_t)ADDR_PLAYER_EXIT_VEHICLE)(H->bots[i], 0, 1);')
    print('PASS: the exit guard - the game\'s exit only for a bot its vehicle still seats (when the seats can be read), once a frame')
    assert 'if (g_gunStuck[i] >= 3 && g_gunTurned[i]) {   // V30: at the end of its arc - another target, same gun' in core
    assert '#define GUN_ARC_SKIPPED(i,p) ((p)==g_gunArcSkip[i] && H->frame<g_gunArcSkipUntil[i])' in core and gt.count('GUN_ARC_SKIPPED(i,') == 3
    assert 'if (bm_air_profile(vehicle_name(veh))) return g_enhanced.doorGunRange;' in core and 'if(distance>gun_range_for(g_gunVeh[i],!isVeh))return 0;' in core
    assert 'float lead=3.0f, px=' in air and 'c->jetAreaMargin=900;' in air and 'm,1400.0f);' in an   # (V34: 900 / 1400 - V31: 600 / 1000)
    assert 'if (g_enhanced.lockOnSeconds >= 0.0f && lt > g_enhanced.lockOnSeconds) lt = g_enhanced.lockOnSeconds;' in core
    for key, line in (('LockOnSeconds', 'FLOAT_KEY("LockOnSeconds", lockOnSeconds, 0, 6)'), ('DoorGunMeters', 'FLOAT_KEY("DoorGunMeters", doorGunRange, 150, 1500)')):
        assert line in en and "New-BmSetting '%s'" % key in sc, key
    assert 'c->lockOnSeconds = 0.5f;' in en and 'c->doorGunRange = 600.0f;' in en
    assert '#define BM_ENHANCED_VERSION "AltBotMod V34 - Squad Up"' in eh
    print('PASS: V31 source - door guns keep their gun at the end of its arc; DoorGunMeters; jets turn back 3 s before the edge of a wider area; LockOnSeconds and DoorGunMeters in Server Control; version V31')

if __name__ == '__main__':
    main()
