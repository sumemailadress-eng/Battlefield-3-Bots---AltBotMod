#!/usr/bin/env python3
"""AltBotMod V32 - Open Doors: offline checks.

1. Shooting a way open (core.c breach_start / breach_tick): a bot stuck at a shut door on its route (blocked at 0.5, 1.2 and
   1.8 m within 2.5 m, the route's next point beyond) holds still and fires across the door and low on both sides of it
   (Epicenter's red gas tanks), then looks again; not for a hillside or an open way; twice per spot every 30 s at most.
2. Infantry onto the Fun-Bots routes: a grid join that runs out of the tick's planning budget waits for the next tick
   instead of counting as "no way onto a route" and walking the grid; a failed join is not retried from the same spot
   for 10 s.
3. Squad spawns (as Fun-Bots 3.6.0-dev7): not on a mate hit in the last 5 s or with a living enemy within 30 m.
4. Fun-Bots dev branch (3.6.0-dev7, checked 2026-10-23): its route files no longer carry objectives (bots walk a nav mesh
   at the objectives), so they are not taken over; ours keep every objective tag (the Rush check in altbotmod_v31_checks).
5. Version.
"""
from pathlib import Path
import argparse, subprocess, tempfile, re
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
TRACES = ROOT / 'files/server/BotMod_traces'

def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True, env={'ASAN_OPTIONS': 'detect_leaks=0', 'PATH': '/usr/bin:/bin'})
    if r.returncode: raise SystemExit('FAILED: ' + ' '.join(map(str, cmd)) + '\n' + r.stdout + r.stderr)
    return r.stdout

BREACH = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define MAX_BOTS 2
#define OFF_PLAYER_ONLINE_NAME 0
#define OFF_SOLDIER_POSITION 0
#define OFF_SOLDIER_YAW 16
#define EIA_FIRE 7
static struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; float botMove[MAX_BOTS][3]; } host, *H = &host;
typedef struct { int mode; float targetYaw, x, z; } BotNav;
static BotNav navs[MAX_BOTS];
static BotNav *bot_nav(int i) { return &navs[i]; }
static unsigned g_fenceUntil[MAX_BOTS], g_rayFlags;
static float yaw_towards(float dx, float dz) { return atan2f(dx, dz); }
static float dist2(const float *a, const float *b) { float x = a[0] - b[0], z = a[2] - b[2]; return x * x + z * z; }
static char *player_vehicle(char *pl) { (void)pl; return NULL; }
static void bot_log(const char *f, ...) { (void)f; }
#define EIA_FIRE_KNIFE 8
#define SLOT_ACTION(n) (100 + (n))
static void breakable_learn(unsigned m, int b) { (void)m; (void)b; }   /* AltBotMod V34 */
static int fires, doorOpen, wallKind;   /* wallKind 1: a door (all heights), 2: a hillside (only low) */
static float tankZ = -1.2f;           /* the gas tank beside the door (the wall plane x = 2), low */
static float sx[6];
float wrapneg(float y);
static void schedule_press(int i, int a, unsigned at) {
    (void)i; (void)at;
    if (a != EIA_FIRE) return;
    fires++;
    /* the shot goes where the bot looks: its line at the wall plane z = 2 */
    float y = sx[4], pitch = sx[5], dz = cosf(y), dx = sinf(y);
    if (dx <= 0.1f) return;
    float t = 2.0f / dx, hz = dz * t, hy = 1.6f + tanf(pitch) * t;
    if (fabsf(hz - tankZ) < 0.5f && hy < 1.1f) doorOpen = 1;   /* the tank blows the door */
}
static int ray_cast(const float *a, const float *b, float *hit, float *n) {
    (void)n;
    if (!wallKind || doorOpen) return 0;
    if ((a[0] - 2.0f) * (b[0] - 2.0f) > 0) return 0;
    if (wallKind == 2 && a[1] > 0.8f) return 0;
    if (hit) { hit[0] = 2.0f; hit[1] = a[1]; hit[2] = a[2]; }
    return 1;
}
@@DECL@@
@@CODE@@
float wrapneg(float y) { y = fmodf(y, 6.2831853f); if (y > 0) y -= 6.2831853f; return y; }
int main(void)
{
    static char bot0[16] = "b0"; H->bots[0] = bot0; H->botCount = 1; H->frame = 1000;
    char *soldier = (char *)sx;
    navs[0].mode = 3; navs[0].x = 10; navs[0].z = 0; sx[4] = wrapneg(1.5708f);   /* the door to the east, the bot facing it */
    /* an open way and a hillside: nothing */
    wallKind = 0; assert(!breach_start(0, soldier));
    wallKind = 2; assert(!breach_start(0, soldier));
    /* a shut door with a gas tank beside it */
    wallKind = 1; doorOpen = 0; fires = 0;
    assert(breach_start(0, soldier) && g_fenceUntil[0] == H->frame + 105);
    for (int f = 0; f < 110; f++) {
        breach_tick(0, soldier);
        sx[4] = wrapneg(sx[4] - H->botMove[0][2] * 0.12f);   /* turn by the yaw input */
        sx[5] = g_breachPitch[0];                            /* the pitch control follows it */
        H->frame++;
    }
    if (!(fires >= 9 && doorOpen && g_breachOpened == 1 && !g_breachUntil[0])) fprintf(stderr, "DBG fires=%d open=%d opened=%u until=%u yaw=%f\n", fires, doorOpen, g_breachOpened, g_breachUntil[0], sx[4]);
    assert(fires >= 9 && doorOpen && g_breachOpened == 1 && !g_breachUntil[0]);
    printf("PASS: a shut door with a gas tank beside it - the bot holds, sweeps its fire across the door and low on both sides (%d shots), the tank blows and the way is open\n", fires);
    /* a door that does not open: two tries at the spot, then not again for 30 s */
    doorOpen = 0; tankZ = 50; g_breachAt[0] = 0;
    assert(breach_start(0, soldier)); for (int f = 0; f < 110; f++) { breach_tick(0, soldier); H->frame++; }
    assert(breach_start(0, soldier)); for (int f = 0; f < 110; f++) { breach_tick(0, soldier); H->frame++; }
    assert(!breach_start(0, soldier));
    H->frame += 31 * 30; assert(breach_start(0, soldier));
    puts("PASS: a door that does not give - two tries at the spot, then the usual recovery; again after 30 s");
    return 0;
}
'''

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--cc', default='gcc'); ap.add_argument('--sanitize', action='store_true'); args = ap.parse_args()
    san = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'] if args.sanitize else []
    core = (SRC / 'core.c').read_text(); tr = (SRC / 'trace_native.inc').read_text(); eh = (SRC / 'enhanced.h').read_text()
    decl = re.search(r'static unsigned g_breachUntil\[MAX_BOTS\].*?\n', core).group(0)
    a = core.index("// AltBotMod V32: a closed door the bot's route goes through"); b = core.index('// 1: knifing a fence; 2: a fence no knife gets through')
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        (td / 'breach.c').write_text(BREACH.replace('@@DECL@@', decl).replace('@@CODE@@', core[a:b]))
        run([args.cc, *san, '-std=gnu11', '-O1', '-Wall', '-Wno-unused-function', '-Wno-unused-variable', str(td / 'breach.c'), '-lm', '-o', str(td / 'breach')])
        print(run([str(td / 'breach')]).strip())
    assert 'if (!fk) { int r = try_break_panel(i, soldier); return r ? r : breach_start(i, soldier); }' in core
    assert 'g_breachUntil[i] > H->frame ? g_breachPitch[i] :' in core and 'breach_tick(i, *(char **)(H->bots[i] + OFF_PLAYER_SOLDIER));' in core
    # 2. joins
    assert 'if (!plan_budget_left()) { g_lastPlanDeferred = 1; g_traceJoinDeferred++; return 0; }' in tr
    assert 'if (g_joinFailAt[i] && H->frame - g_joinFailAt[i] < 10 * 30 && g_joinFailId[i] == id && dist2(g_joinFailPos[i], pos) < 6.0f * 6.0f)' in tr
    assert 'g_joinFailAt[i] = H->frame ? H->frame : 1; g_joinFailId[i] = id;' in tr
    print('PASS: infantry onto the Fun-Bots routes - a join out of planning budget waits for the next tick; a failed join is not retried from the same spot for 10 s')
    # 3. squad spawns
    sp = core[core.index('static int squadmate_spawnable(char *mate, char *ms)\n{'):]; sp = sp[:sp.index('\n}\n')]
    assert 'g_attackedAt[mj] && H->frame - g_attackedAt[mj] < 5 * 30) return 0;' in sp and 'dist2((float *)(ts + OFF_SOLDIER_POSITION), p) < 30.0f * 30.0f) return 0;' in sp
    print('PASS: squad spawns - not on a mate hit in the last 5 s or with a living enemy within 30 m')
    # 4. our route files keep their objective tags (Fun-Bots dev dropped them)
    rush = [f for f in TRACES.glob('*Rush*.map')]
    assert rush and all('"Objectives"' in f.read_text(errors='ignore') for f in rush)
    print('PASS: our %d Rush route files keep their objective tags (Fun-Bots 3.6.0-dev7 route files carry none - not taken over)' % len(rush))
    assert '#define BM_ENHANCED_VERSION "AltBotMod V34 - Squad Up"' in eh
    print('PASS: version (current)')

if __name__ == '__main__':
    main()
