#!/usr/bin/env python3
"""AltBotMod V18 - Indoors: offline checks (the real functions, extracted, with a scripted world).

1. Glass / thin panels (core.c): a bot stuck against a 2 cm pane with floor and room behind it knifes it; a pane the knife
   breaks is learned as breakable (BotMod_breakable.txt); one the knife does not break is shot; one nothing breaks is
   learned as unbreakable and never tried again; a 1 m wall is never taken for a panel.
2. Making way (core.c): a bot walking into a teammate bot asks it to side-step away from its line; a human teammate in the
   way makes the walker step round; an enemy or someone behind is ignored; the step aside overrides the steering for 1.3 s.
3. Running in circles (activity_native.inc): 20+ m walked within 5 m in 12 s is caught (links blocked, new way; the second
   time a new objective too); a straight walk or a fight is not.
Source checks: indoor guard / patrol spots, rooftop perches, the stairs hop, the version.
"""
from pathlib import Path
import argparse, re, subprocess, sys, tempfile
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test4_checks import function  # noqa: E402
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'

def oneliner(src, name):
    m = re.search(r'^static int ' + name + r'\(.*$', src, re.M); assert m, name; return m.group(0)

GLASS = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define MAX_BOTS 4
#define MAX_PATH 260
#define OFF_SOLDIER_POSITION 0
#define OFF_SOLDIER_YAW 12
#define OFF_PLAYER_ONLINE_NAME 0
#define EIA_FIRE 7
#define EIA_FIRE_KNIFE 7
#define SLOT_ACTION(s) (100 + (s))
static struct { unsigned frame; char *bots[MAX_BOTS]; } host, *H = &host;
static unsigned g_rayFlags, g_fenceUntil[MAX_BOTS];
static int knifed, shots, logs;
static float pane = 1.0f, thick = 0.02f; static int broken, knifeBreaks, shotBreaks; static unsigned mat = 0x0300beefu;
static void bot_log(const char *f, ...) { (void)f; logs++; }
static unsigned GetModuleFileNameA(void *m, char *out, unsigned n) { (void)m; snprintf(out, n, "C:\\srv\\bf3.exe"); return 1; }
/* AltBotMod V21: the breakable file moved into BotMod_learned (an old BotMod_breakable.txt is moved over) */
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
static int CreateDirectoryA(const char *p, void *s) { (void)p; (void)s; return 1; }
static unsigned GetFileAttributesA(const char *p) { FILE *f = fopen(p, "rb"); if (f) { fclose(f); return 0x20; } return INVALID_FILE_ATTRIBUTES; }
static int MoveFileA(const char *a, const char *b) { return rename(a, b) == 0; }
static int inHand = 0;   /* EIA_FIRE and EIA_FIRE_KNIFE are the same action (7): what is in hand fires */
static void schedule_press(int i, int action, unsigned at) { (void)i; (void)at; if (action >= 100) inHand = action - 100; else if (action == EIA_FIRE) { if (inHand == 7) knifed++; else shots++; } }
static unsigned hit_material(void) { return mat; }
/* world: floor y=0; a pane / wall across z in [pane, pane+thick] for y 0..2.5 (unless broken) */
static int inside(float x, float y, float z) { (void)x; return y <= 0.0f || (!broken && y < 2.5f && z >= pane && z <= pane + thick); }
static int ray_cast(const float *a, const float *b, float *hit, void *n)
{
    (void)n;
    for (int k = 1; k <= 2000; k++) {
        float t = k / 2000.0f, p[3] = {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
        if (inside(p[0], p[1], p[2])) { if (hit) memcpy(hit, p, 12); return 1; }
    }
    return 0;
}
''' + '@@FNS@@' + r'''
int main(void)
{
    static char name[16] = "bot"; H->bots[0] = name; H->frame = 30;
    float soldier[4] = {0, 0, 0, 0};   /* at z=0 facing +z (yaw 0) */
    /* 1: a pane the knife breaks */
    knifeBreaks = 1; assert(try_break_panel(0, (char *)soldier) && g_panelStage[0] == 1 && knifed == 2);
    broken = 1; H->frame += 70; panel_check_tick(0); assert(!g_panelStage[0] && material_breakable(mat));
    /* 2: a learned breakable is loaded again from the file */
    g_breakN = 0; g_breakLoaded = 0; breakable_load(); assert(material_breakable(mat));
    /* 3: a pane only shots break */
    broken = 0; mat = 0x0300cafe; knifed = shots = 0; assert(try_break_panel(0, (char *)soldier));
    H->frame += 70; panel_check_tick(0); assert(g_panelStage[0] == 2 && shots == 3);
    broken = 1; H->frame += 30; panel_check_tick(0); assert(!g_panelStage[0] && material_breakable(mat));
    /* 4: bulletproof: learned unbreakable after a second failed try (AltBotMod V34: V18-V33 after one), never tried again */
    broken = 0; mat = 0x01001234; assert(try_break_panel(0, (char *)soldier));
    H->frame += 70; panel_check_tick(0); H->frame += 30; panel_check_tick(0);
    assert(!g_panelStage[0] && !material_breakable(mat) && try_break_panel(0, (char *)soldier));   /* one failure: tried again */
    H->frame += 70; panel_check_tick(0); H->frame += 30; panel_check_tick(0);
    assert(!g_panelStage[0] && !material_breakable(mat) && !try_break_panel(0, (char *)soldier));
    /* 5: a wall (1 m thick) is not a panel */
    mat = 0x00005555; thick = 1.0f; assert(!try_break_panel(0, (char *)soldier));
    puts("PASS: V18 glass - a bot stuck against a thin pane (floor and room behind it) knifes it, shoots it if the knife did nothing; "
         "what breaks is learned as breakable (BotMod_breakable.txt, read back at load), what nothing breaks is learned as unbreakable and "
         "left alone; a 1 m wall is never taken for a pane");
    return 0;
}
'''

WAY = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define MAX_BOTS 4
#define OFF_SOLDIER_POSITION 0
#define OFF_SOLDIER_YAW 12
#define OFF_SOLDIER_HEALTH 16
#define OFF_PLAYER_SOLDIER 40
#define OFF_PLAYER_ONLINE_NAME 0
static struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; float botMove[MAX_BOTS][3]; } host, *H = &host;
static char players[4][64] __attribute__((aligned(8))); static float bodies[4][8]; static int teams[4], botIdx[4];
static char pm[256] __attribute__((aligned(8))); static void *arr[4];
static char *player_manager(void) { return pm; }
static int player_team(char *p) { for (int k = 0; k < 4; k++) if (p == players[k]) return teams[k]; return 0; }
static char *player_vehicle(char *p) { (void)p; return NULL; }
static int bot_index(void *p) { for (int k = 0; k < 4; k++) if (p == players[k]) return botIdx[k]; return -1; }
static int is_bot(void *p) { return bot_index(p) >= 0; }
static void bot_log(const char *f, ...) { (void)f; }
static unsigned g_rayFlags; static int wallLeft, wallRight;
static int ray_cast(const float *a, const float *b, float *h, float *n) { (void)h; (void)n; float side = b[0] - a[0]; return side > 0.5f ? wallRight : side < -0.5f ? wallLeft : 0; }
''' + '@@FNS@@' + r'''
static void place(int k, float x, float z) { bodies[k][0] = x; bodies[k][2] = z; bodies[k][4] = 100; *(float **)(players[k] + 40) = bodies[k]; }
int main(void)
{
    for (int k = 0; k < 4; k++) { arr[k] = players[k]; H->bots[k] = players[k]; botIdx[k] = k; teams[k] = 1; }
    void **a = arr; unsigned n = 4; memcpy(pm + 0xac, &a, sizeof a); memcpy(pm + 8, &n, sizeof n);
    H->botCount = 4; H->frame = 100; teams[3] = 2; botIdx[2] = -1;   /* 0: walker, 1: teammate bot, 2: human teammate, 3: enemy */
    place(0, 0, 0); place(1, 0.3f, 1.0f); place(2, 50, 50); place(3, 60, 60);
    make_way_ask(0, (char *)bodies[0]); assert(H->frame < g_wayUntil[1] && g_wayStrafe[1] != 0);   /* an idle teammate bot 1 m ahead: steps aside */
    H->botMove[1][0] = 1; make_way_tick(1, (char *)bodies[1]); assert(H->botMove[1][0] == 0 && H->botMove[1][1] == g_wayStrafe[1]);
    /* both sides walled (a stairwell): it is left alone */
    memset(g_wayUntil, 0, sizeof g_wayUntil); H->botMove[1][0] = 0; wallLeft = wallRight = 1; make_way_ask(0, (char *)bodies[0]); assert(!g_wayUntil[1]); wallLeft = wallRight = 0;
    /* walking the same way: the one behind queues, the one in front keeps walking */
    H->botMove[1][0] = 1; bodies[1][3] = 0; make_way_ask(0, (char *)bodies[0]); assert(!g_wayUntil[1] && H->frame < g_wayUntil[0] && g_wayThrottle[0] == 0 && g_wayStrafe[0] == 0);
    /* head-on: both keep right and keep walking */
    memset(g_wayUntil, 0, sizeof g_wayUntil); bodies[1][3] = 3.14159f; make_way_ask(0, (char *)bodies[0]);
    assert(H->frame < g_wayUntil[0] && H->frame < g_wayUntil[1] && g_wayStrafe[0] == g_wayStrafe[1] && g_wayThrottle[1] > 0);
    /* on a flight of stairs: never stopped */
    memset(g_wayUntil, 0, sizeof g_wayUntil); H->botMove[1][0] = 0; g_stairOn[1] = 1; make_way_ask(0, (char *)bodies[0]); assert(!g_wayUntil[1]); g_stairOn[1] = 0;
    H->botMove[1][0] = 0;
    memset(g_wayUntil, 0, sizeof g_wayUntil); place(1, 0, -1.0f); make_way_ask(0, (char *)bodies[0]); assert(!g_wayUntil[1]);   /* behind: no */
    place(1, 40, 40); place(3, 0, 1.0f); make_way_ask(0, (char *)bodies[0]); assert(!g_wayUntil[3]);   /* an enemy: no */
    place(3, 60, 60); place(2, 0, 1.0f); make_way_ask(0, (char *)bodies[0]); assert(H->frame < g_wayUntil[0]);   /* a human: the walker goes round */
    /* stuck walking: asks by itself every half second */
    memset(g_wayUntil, 0, sizeof g_wayUntil); place(2, 50, 50); place(1, 0, 1.2f); g_wayAskedAt[0] = 0;
    for (int f = 0; f < 40; f++) { H->frame++; H->botMove[0][0] = 1; make_way_tick(0, (char *)bodies[0]); }
    assert(H->frame < g_wayUntil[1]);
    puts("PASS: V18 making way - a bot that means to walk but stands still: an idle teammate bot in front side-steps to a clear side "
         "(walled both sides: left alone); one walking the same way is queued behind, not stopped; head-on both keep right and walk on; "
         "never a bot on a flight of stairs; a human in the way: the walker goes round; enemies and anyone behind ignored");
    return 0;
}
'''

CIRCLE = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define MAX_BOTS 4
#define OFF_PLAYER_ONLINE_NAME 0
enum { TASK_NONE, TASK_FIGHT };
typedef struct { int task, waiting, targetFlag; } BotBrain;
typedef struct ActivityState { float trail[12][2]; int trailN; unsigned circleAt; int circles; unsigned progressAt; } ActivityState;
typedef struct { int mode; float targetYaw, x, z; } BotNav;
#define OFF_PLAYER_SOLDIER 8
#define OFF_SOLDIER_POSITION 0
static struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; } host, *H = &host;
static BotNav navs[MAX_BOTS]; static int blocks, replans, holding, working; static void *grid = &host;
static BotNav *bot_nav(int i) { return &navs[i]; }
static void **nav_slot(void) { return &grid; }
static int nav_block_towards(void *g, float x, float y, float z, float tx, float tz) { (void)g; (void)x; (void)y; (void)z; (void)tx; (void)tz; blocks++; return 2; }
static void nav_cancel_path(int i) { (void)i; }
static int sniper_holding(int i) { (void)i; return holding; }
static int class_job_working(int i, const float *p) { (void)i; (void)p; return working; }
static int objective_count(void) { return 0; }
static const float *objective_pos(int f) { (void)f; return NULL; }
static float dist2(const float *a, const float *b) { float x = a[0] - b[0], z = a[2] - b[2]; return x * x + z * z; }
static void activity_end_task(int i) { (void)i; }
static void activity_replan(int i) { (void)i; replans++; }
static void bot_log(const char *f, ...) { (void)f; }
''' + '@@FN@@' + r'''
int main(void)
{
    static char pl[3][16] __attribute__((aligned(8))); static float body[3][3]; for (int k = 0; k < 3; k++) { H->bots[k] = pl[k]; *(float **)(pl[k] + 8) = body[k]; }
    H->botCount = 1; H->frame = 30; navs[0].mode = 3;
    ActivityState w; memset(&w, 0, sizeof w); BotBrain b = {TASK_NONE, 0, -1};
    int caught = 0;
    for (int s = 0; s < 14 && !caught; s++) { float p[3] = {3 * sinf(s * 1.3f), 0, 3 * cosf(s * 1.3f)}; H->frame += 30; caught = activity_circling(0, &w, &b, p); }
    assert(caught && blocks == 1 && !replans && navs[0].mode == 0);
    navs[0].mode = 3; H->frame += 16 * 30; caught = 0;
    for (int s = 0; s < 14 && !caught; s++) { float p[3] = {3 * sinf(s * 1.3f), 0, 3 * cosf(s * 1.3f)}; H->frame += 30; caught = activity_circling(0, &w, &b, p); }
    assert(caught && replans == 1);   /* the second time: a new objective too */
    memset(&w, 0, sizeof w); navs[0].mode = 3; H->frame += 16 * 30;
    for (int s = 0; s < 30; s++) { float p[3] = {0, 0, s * 2.5f}; H->frame += 30; assert(!activity_circling(0, &w, &b, p)); }   /* straight: fine */
    memset(&w, 0, sizeof w); b.task = TASK_FIGHT;
    for (int s = 0; s < 30; s++) { float p[3] = {3 * sinf(s * 1.3f), 0, 3 * cosf(s * 1.3f)}; H->frame += 30; assert(!activity_circling(0, &w, &b, p)); }   /* dodging in a fight: fine */
    /* in a crowd (two teammates within 4 m - a stairwell queue): re-plans, but blocks nothing and keeps its objective */
    memset(&w, 0, sizeof w); b.task = TASK_NONE; navs[0].mode = 3; H->frame += 16 * 30; H->botCount = 3; blocks = 0; replans = 0;
    body[1][0] = 1; body[2][2] = 1; caught = 0;
    for (int s = 0; s < 14 && !caught; s++) { float p[3] = {3 * sinf(s * 1.3f), 0, 3 * cosf(s * 1.3f)}; H->frame += 30; caught = activity_circling(0, &w, &b, p); }
    assert(caught && !blocks && !replans && w.circles == 0);
    puts("PASS: V18 running in circles - 20 m walked within 5 m in 12 s is caught: the links it keeps taking are blocked and it takes a "
         "new way (the second time a new objective as well); a straight walk and a bot dodging in a fight are left alone; in a crowd (a stairwell queue) it only re-plans - no links blocked, no new objective");
    return 0;
}
'''

DIRECT = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define MAX_BOTS 4
#define PATH_MAX_POINTS 64
#define OFF_SOLDIER_POSITION 0
@@TYPES@@
static BotNav navs[MAX_BOTS]; static BotNav *bot_nav(int i) { return &navs[i]; }
static BotPath g_paths[MAX_BOTS];
static float g_directTgt[MAX_BOTS][2];
static unsigned char g_directKind[MAX_BOTS];
/* world: floor y=0, an upper floor at y=4; a wall across z in [4, 4.2] for x < 1 (the stairwell wall), floor to ceiling */
static int inside(float x, float y, float z) { return y <= 0.0f || (x < 1.0f && z >= 4.0f && z <= 4.2f); }
static int ray_cast(const float *a, const float *b, float *hit, void *n)
{
    (void)n; (void)hit;
    for (int k = 1; k <= 2000; k++) {
        float t = k / 2000.0f, q[3] = {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
        if (inside(q[0], q[1], q[2])) return 1;
    }
    return 0;
}
@@FN@@
static void path(float gx, float gy, float gz, int n, const float *pts) { BotPath *p = &g_paths[0]; memset(p, 0, sizeof *p); p->count = n; p->next = 0; memcpy(p->pts, pts, (size_t)n * 12); p->goalX = gx; p->goalY = gy; p->goalZ = gz; }
int main(void)
{
    float soldier[3] = {0, 0, 0};
    navs[0].mode = 3; navs[0].x = 2; navs[0].z = 0;
    /* the goal is upstairs behind the stairwell wall: V18 walked at it (into the wall); now the furthest route point in sight */
    float r1[] = {2, 0, 0,  2, 0, 3,  0, 0, 5,  0, 4, 6};
    path(0, 4, 6, 4, r1); direct_target_pick(0, (char *)soldier);
    assert(g_directKind[0] == 1 && g_directTgt[0][0] == 2 && g_directTgt[0][1] == 3);
    /* the next route point is the top of the flight (a floor up): that one - the stairs - not anything beyond */
    float r2[] = {2, 3, 0,  2, 3, 3};
    path(9, 4, 9, 2, r2); direct_target_pick(0, (char *)soldier);
    assert(g_directKind[0] == 1 && g_directTgt[0][0] == 2 && g_directTgt[0][1] == 0);
    /* the destination in plain sight on this floor: straight at it, as before (a player can lead a bot out) */
    float r3[] = {2, 0, 0,  2, 0, 3};
    path(0, 0, 3, 2, r3); direct_target_pick(0, (char *)soldier);
    assert(g_directKind[0] == 0 && g_directTgt[0][0] == 0 && g_directTgt[0][1] == 3);
    /* a plain goto (no route): its point */
    navs[0].mode = 2; navs[0].x = 7; navs[0].z = 8; direct_target_pick(0, (char *)soldier);
    assert(g_directKind[0] == 0 && g_directTgt[0][0] == 7 && g_directTgt[0][1] == 8);
    puts("PASS: V18.1 the 'not moving' straight walk heads for the furthest route point in sight on the bot's own floor (or the next "
         "route point - the stairs), not the destination upstairs behind a wall (V18 stared at the stairwell wall); a destination in "
         "plain sight on the same floor is still walked at");
    return 0;
}
'''

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    cc = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-unused-variable', '-I', str(SRC)]
    if a.sanitize: cc += ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
    core = (SRC / 'core.c').read_text(); act = (SRC / 'activity_native.inc').read_text()
    glass_decl = re.search(r'#define MAX_BREAKABLE 64\n.*?static float g_panelHit\[MAX_BOTS\]\[3\], g_panelFrom\[MAX_BOTS\]\[3\];', core, re.S).group(0)
    glass = glass_decl + '\n' + '\n'.join([function(core, 'breakable_path'), function(core, 'breakable_load'), oneliner(core, 'material_listed'),
        oneliner(core, 'material_breakable'), function(core, 'breakable_learn'), function(core, 'panel_dir'), function(core, 'panel_ahead'), function(core, 'try_break_panel'),
        function(core, 'panel_check_tick')])
    way_decl = re.search(r'static unsigned g_wayUntil\[MAX_BOTS\].*?static float g_wayStrafe\[MAX_BOTS\], g_wayThrottle\[MAX_BOTS\], g_wayLast\[MAX_BOTS\]\[3\];', core, re.S).group(0)
    way = way_decl + '\nstatic unsigned char g_stairOn[MAX_BOTS];\n' + function(core, 'side_clear') + '\n' + function(core, 'make_way_ask') + '\n' + function(core, 'make_way_tick')
    with tempfile.TemporaryDirectory(prefix='bf3-altbotmod-v18-') as t:
        tmp = Path(t)
        def build(name, body):
            c = tmp / (name + '.c'); c.write_text(body)
            subprocess.run(cc + [str(c), '-lm', '-o', str(tmp / name)], check=True)
            r = subprocess.run([str(tmp / name)], stdout=subprocess.PIPE, text=True, cwd=tmp); print(r.stdout, end=''); assert r.returncode == 0, name
        build('glass', GLASS.replace('@@FNS@@', glass))
        build('way', WAY.replace('@@FNS@@', way))
        build('circle', CIRCLE.replace('@@FN@@', function(act, 'activity_circling')))
        types = re.search(r'^typedef struct BotNav \{.*?\} BotNav;', core, re.M).group(0) + '\n' + re.search(r'^typedef struct BotPath \{.*?\} BotPath;', core, re.M).group(0)
        build('direct', DIRECT.replace('@@TYPES@@', types).replace('@@FN@@', function(core, 'direct_target_pick')))
    mn = (SRC / 'modes_native.inc').read_text(); sn = (SRC / 'sniper_native.inc').read_text()
    assert 'static int indoor_spot(int i,const float *around,float radius,float *out)' in mn and mn.count('indoor_spot(i,') >= 2
    assert 'int roof = elev >= 6.0f' in sn and '{45, 70, 90, 140, 190, 250, 320, 400}' in sn
    assert 'stairs: %s is held up on the flight at (%.1f, %.1f) - hops' in core and 'if (!fk) { int r = try_break_panel(i, soldier); return r ? r : breach_start(i, soldier); }' in core
    assert 'if (g_breakN && material_breakable(m)) return NAV_HIT_FENCE;' in core and 'make_way_tick(i, *(char **)' in core and 'panel_check_tick(i);' in core
    assert 'if(activity_circling(i,w,b,p))return;' in act
    assert '#define BM_ENHANCED_VERSION "AltBotMod V34 - Squad Up"' in (SRC / 'enhanced.h').read_text()
    # V18.1: a landing ends the flight at once (no walk into the landing wall); stuck at stairs never blocks the stairwell's links
    assert 'if (g_stairLevel[i] && ray_cast(eye, ahead, NULL, NULL)) {' in core and 'reroute_from_here(i, soldier, "at a landing");' in core
    assert '!crowded && !stairs && !known ? nav_block_towards(' in core   # (V21: a spot learned passable is never blocked either)
    assert '!ladderNext && !stairs &&' in core
    assert 'float gx = g_directTgt[i][0], gz = g_directTgt[i][1];' in core and 'direct_target_pick(i, soldier);' in core
    print('PASS: V18 wiring - every third Rush guard post and every other TDM patrol stop inside a building (roof overhead, walls round); '
          'sniper perches from 45 m out, a rooftop scores over a window; a bot held up on a flight hops and asks the way clear before '
          'giving up the flight; learned glass counts as passable for the grid; V18.1: a stairs landing hands back to the route at '
          'once and a bot stuck at stairs never blocks the stairwell; version')

if __name__ == '__main__':
    main()
