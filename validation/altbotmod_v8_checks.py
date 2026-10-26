"""AltBotMod V8 checks. The real step_walk / walk_clear / direct_path (infantry before a map's grid exists),
drive_straight_route (vehicles before the driving map exists), drive_coast (rolling between route legs), crouch_gap
(crouch under a head-height block) and the armour target order, extracted from core.c / enhanced.c and run against a
small mocked world (flat ground, a wall, a beam, deep and shallow water). Engine physics is mocked: these checks do
not run BF3."""
from pathlib import Path
import argparse, re, subprocess, tempfile, sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test4_checks import _function as function

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'


def define(core, name):
    m = re.search(r'^#define ' + name + r'\b[^\n]*', core, re.M)
    assert m, name
    return re.sub(r'//.*', '', m[0])


def typedef(core, name):
    m = re.search(r'typedef struct ' + name + r' \{[\s\S]*?\} ' + name + ';', core)
    assert m, name
    return m[0]


PRE = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "enhanced.h"
#define MAX_BOTS 64
#define NAV_HIT_WATER 1
#define OFF_SOLDIER_POSITION 0x290
#define OFF_SOLDIER_YAW 0x218
#define RAY_DONT_CHECK_CHARACTER 0x20
#define RAY_CHECK_DETAIL_MESH 0x01
#define PATH_MAX_POINTS 128
#define DRIVE_MAX_PTS 128
#define MAX_FLAGS 16
#define OFF_PLAYER_ONLINE_NAME 0x40
static struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; float botMove[MAX_BOTS][3]; } host, *H = &host;
static unsigned g_rayFlags;
static float dist2(const float *a, const float *b) { float dx = a[0]-b[0], dy = a[1]-b[1], dz = a[2]-b[2]; return dx*dx+dy*dy+dz*dz; }
static void bot_log(const char *f, ...) { (void)f; }
static void nav_cancel_path(int i) { (void)i; }
static BMSteering g_enhancedSteer[MAX_BOTS];
// ---- the mocked world: ground at y=0; a wall; a beam at head height; deep water; a shallow ford ----
typedef struct Box { float x0, x1, z0, z1, y0, y1; } Box;
static Box walls[8]; static int nWalls;
static Box waters[4]; static float waterDepth[4]; static int nWaters;   // y0..y1 unused: surface at 0, bed at -depth
static int lastWater, rays;
static int in_box(const Box *b, float x, float y, float z) { return x >= b->x0 && x <= b->x1 && z >= b->z0 && z <= b->z1 && y >= b->y0 && y <= b->y1; }
static int ray_cast(const float *a, const float *b, float *hit, float *n)
{
    (void)n; rays++; lastWater = 0;
    if (fabsf(a[0]-b[0]) < 1e-4f && fabsf(a[2]-b[2]) < 1e-4f) {   // vertical
        float x = a[0], z = a[2], top = a[1], bottom = b[1];
        for (int w = 0; w < nWaters; w++) if (in_box(&waters[w], x, 0, z) && top > -waterDepth[w]) {
            if (top >= 0 && bottom <= 0) { hit[0] = x; hit[1] = 0; hit[2] = z; lastWater = 1; return 1; }   // the surface
            if (top < 0 && bottom <= -waterDepth[w]) { hit[0] = x; hit[1] = -waterDepth[w]; hit[2] = z; return 1; }   // the bed
            return 0;
        }
        if (top >= 0 && bottom <= 0) { hit[0] = x; hit[1] = 0; hit[2] = z; return 1; }
        return 0;
    }
    float d[3] = {b[0]-a[0], b[1]-a[1], b[2]-a[2]}, len = sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    for (float t = 0; t <= len; t += 0.05f) {
        float p[3] = {a[0]+d[0]*t/len, a[1]+d[1]*t/len, a[2]+d[2]*t/len};
        for (int w = 0; w < nWalls; w++) if (in_box(&walls[w], p[0], p[1], p[2])) { memcpy(hit, p, 12); return 1; }
    }
    return 0;
}
static int nav_hit_class(void) { return lastWater ? NAV_HIT_WATER : 0; }
typedef int (*NavRayFn)(const float *, const float *, float *, float *);
static int nav_segment_clear(NavRayFn ray, const float *from, const float *to)
{
    float h[3], a[3] = {from[0], from[1]+0.5f, from[2]}, b[3] = {to[0], to[1]+0.5f, to[2]};
    if (ray(a, b, h, NULL)) return 0;
    a[1] = from[1]+1.2f; b[1] = to[1]+1.2f;
    return !ray(a, b, h, NULL);
}
'''

MAIN = r'''
static char soldier[0x400] __attribute__((aligned(8)));
static float *pos(void) { return (float *)(soldier + OFF_SOLDIER_POSITION); }
static void reset_world(void) { nWalls = nWaters = 0; memset(g_paths, 0, sizeof g_paths); memset(g_directUse, 0, sizeof g_directUse);
    memset(g_stepCandidate, 0, sizeof g_stepCandidate); H->frame += 100; }
static int step(int i, const float *goal) { int r; int guard = 0; while ((r = step_walk(i, soldier, goal)) < 0 && guard++ < 40) H->frame++; return r; }
int main(void)
{
    static char player[256]; H->bots[0] = player; strcpy(player + OFF_PLAYER_ONLINE_NAME, "[BOT] Test"); H->botCount = 1; H->frame = 1000;
    // 1. Open ground, goal 200 m north: a 40 m checked leg straight at it, kept as a partial route to the real goal.
    reset_world(); memset(pos(), 0, 12);
    float goal[3] = {0, 0, 200};
    assert(step(0, goal) == 1);
    BotPath *p = &g_paths[0];
    assert(p->count == 1 && p->partial && fabsf(p->pts[2] - 40) < 0.01f && fabsf(p->pts[0]) < 0.01f);
    assert(p->goalZ == 200 && p->goalX == 0);
    puts("PASS: no grid yet - a bot walks a checked 40 m straight leg towards a 200 m objective and keeps the real goal (V7 stood at its HQ)");
    // 2. The goal itself within 40 m: the leg ends on it, not partial.
    reset_world(); float near[3] = {10, 0, 25};
    assert(step(0, near) == 1 && !g_paths[0].partial && fabsf(g_paths[0].pts[0] - 10) < 0.01f && fabsf(g_paths[0].pts[2] - 25) < 0.01f);
    puts("PASS: a goal inside one leg is walked to directly (not a partial route)");
    // 3. A wall across the direct line 10-14 m ahead, 6 m wide: a leg fanned out to one side clears it.
    reset_world(); walls[nWalls++] = (Box){-3, 3, 10, 14, -1, 5};
    assert(step(0, goal) == 1);
    float *e = g_paths[0].pts, a = atan2f(e[0], e[2]);
    assert(fabsf(a) > 0.3f && fabsf(a) < 1.2f && g_paths[0].partial);
    { float h[3], s0[3] = {0, 0.5f, 0}, s1[3] = {e[0], 0.5f, e[2]}; assert(!ray_cast(s0, s1, h, NULL)); }
    puts("PASS: a wall on the direct line - the leg fans out to a clear heading (checked with body rays) instead of walking into it");
    // 4. Deep water ahead on every heading within 63 deg: no leg (no walking into the sea), and the call says so.
    reset_world(); waters[nWaters] = (Box){-200, 200, 5, 400, -1, 1}; waterDepth[nWaters++] = 4;
    int r = step(0, goal);
    assert(r == 0 && g_paths[0].count == 0);
    puts("PASS: water across the way - no leg is planned into it (walk_clear rejects water samples)");
    // 5. Vehicles: open ground, flag 300 m away: straight legs every 20 m, 160 m at a time, a partial route.
    DriveState *d = &g_drive[0]; memset(d, 0, sizeof *d); reset_world();
    d->goal[0] = 0; d->goal[1] = 0; d->goal[2] = 300; d->flag = 2; float vp[3] = {0, 0, 0};
    assert(drive_straight_route(0, d, vp, 0) == 1 && d->phase == 3 && d->n == 8 && d->partial && d->straight == 1);
    for (int k = 0; k < d->n; k++) assert(fabsf(d->pts[k*3+2] - 20.0f*(k+1)) < 0.01f);
    puts("PASS: no driving map yet - the driver gets 20 m straight legs towards its flag, 160 m at a time (V7: \"driving map not ready\", crews parked at HQ)");
    // 6. Deep water 70 m ahead: the land vehicle's line stops before it; a 0.5 m ford is driven through.
    memset(d, 0, sizeof *d); d->goal[2] = 300; reset_world(); waters[nWaters] = (Box){-50, 50, 70, 120, -1, 1}; waterDepth[nWaters++] = 3;
    assert(drive_straight_route(0, d, vp, 0) == 1 && d->n == 3 && d->partial && d->pts[3*2+2] < 70);
    memset(d, 0, sizeof *d); d->goal[2] = 300; reset_world(); waters[nWaters] = (Box){-50, 50, 70, 120, -1, 1}; waterDepth[nWaters++] = 0.5f;
    assert(drive_straight_route(0, d, vp, 0) == 1 && d->n == 8);
    { int ford = 0; for (int k = 0; k < d->n; k++) if (fabsf(d->pts[k*3+1] + 0.5f) < 0.01f) ford++; assert(ford >= 2); }
    puts("PASS: the straight line stops short of deep water, and drives on through a 0.5 m ford (bed height used)");
    // 7. A boat: the line runs over water and ends on the shore - the landing.
    memset(d, 0, sizeof *d); d->goal[2] = 300; reset_world(); waters[nWaters] = (Box){-50, 50, -10, 95, -1, 1}; waterDepth[nWaters++] = 5;
    assert(drive_straight_route(0, d, vp, 1) == 1 && !d->partial && d->landSet && fabsf(d->pts[(d->n-1)*3+2] - 100) < 0.01f && d->n == 5);
    puts("PASS: a boat's straight line ends on the shore, its landing (everyone out there)");
    // 8. Rolling between legs: up to 1.5 s at a light throttle while the bumpers saw nothing near; otherwise stop.
    memset(d, 0, sizeof *d); d->speed = 8; d->frontNear = 40; d->coastUntil = H->frame + 45;
    drive_coast(0, d); assert(H->botMove[0][0] > 0.2f && H->botMove[0][2] == 0);
    d->frontNear = 10; drive_coast(0, d); assert(H->botMove[0][0] == 0);
    d->frontNear = 40; H->frame += 46; drive_coast(0, d); assert(H->botMove[0][0] == 0);
    puts("PASS: a vehicle rolls on between route legs (no dead stop at every partial-route end), but not with something within 15 m ahead");
    // 9. Crouch through: a beam from 1.4 m up, 1 m ahead - crouch; a knee-high crate - not (that is a vault).
    reset_world(); memset(pos(), 0, 12); *(float *)(soldier + OFF_SOLDIER_YAW) = 0;
    walls[nWalls++] = (Box){-2, 2, 0.6f, 1.0f, 1.4f, 2.4f};
    assert(crouch_gap(soldier));
    reset_world(); walls[nWalls++] = (Box){-2, 2, 0.6f, 1.0f, 0, 0.8f};
    assert(!crouch_gap(soldier));
    reset_world(); assert(!crouch_gap(soldier));   // nothing there at all: not a reason to crouch
    puts("PASS: blocked at head height with a clear crouch gap - the bot crouches through; a knee-high block or open ground does not trigger it");
    // 10. Armour: soldiers within ~150 m before a helicopter 300 m away; one right overhead still first.
    assert(bm_target_score(1, BM_TARGET_INFANTRY, 150) < bm_target_score(1, BM_TARGET_AIR, 300));
    assert(bm_target_score(1, BM_TARGET_INFANTRY, 250) < bm_target_score(1, BM_TARGET_AIR, 450));
    assert(bm_target_score(1, BM_TARGET_AIR, 100) < bm_target_score(1, BM_TARGET_INFANTRY, 150));
    assert(bm_target_score(1, BM_TARGET_GROUND, 300) < bm_target_score(1, BM_TARGET_INFANTRY, 100));
    puts("PASS: tanks keep shooting soldiers out to 300 m before far helicopters (V7 crews spent the round start turning after choppers); armour still first");
    return 0;
}
'''


NAVC = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "nav.c"
static void nav_free_mask(unsigned char *m) { HeapFree(GetProcessHeap(), 0, m); }
int main(void)
{
    NavGrid *g = nav_create("V8", 0, 0, 80, 80, 2);
    assert(g && g->w >= 40 && g->h >= 40);
    int n = g->w * g->h * L; (void)n;
    for (int c = 0; c < g->w * g->h; c++) { g->flags[c * L] = NAV_WALKABLE; g->height[c * L] = 0; g->edges[c * L] = 0xff; }
    // a fence line across row 20: the straight edge (direction 0) and the diagonals beside it (1, 7) are closed
    for (int x = 0; x < g->w; x++) { int node = (20 * g->w + x) * L; g->edges[node] = (unsigned char)(0xff & ~0x83u); }
    int count = 0, mid = (20 * g->w + 20) * L;
    unsigned char *m = nav_drive_mask(g, &count);
    assert(m && !m[mid]);   // no fence information: a wall for vehicles
    nav_free_mask(m);
    for (int x = 0; x < g->w; x++) g->fences[(20 * g->w + x) * L] = 0x01;   // knifeable fence in direction 0
    m = nav_drive_mask(g, &count);
    assert(m && m[mid]);
    puts("PASS: a knifeable fence across a road is drivable for vehicles (V7: the tank stopped at it); without a fence record the gap stays a wall");
    nav_free_mask(m);
    for (int x = 0; x < g->w; x++) g->fences[(20 * g->w + x) * L] = 0x02;   // a doorway marker only - not a fence
    m = nav_drive_mask(g, &count);
    assert(m && !m[mid]);
    puts("PASS: only a knifeable fence opens the way - a doorway / a real wall does not");
    nav_free_mask(m);
    // A* for a vehicle: a fence line down the whole map at x = 20 | 21 (direction 0 is +x, 4 is -x)
    for (int c = 0; c < g->w * g->h; c++) { g->edges[c * L] = 0xff; g->fences[c * L] = 0; }
    for (int z = 0; z < g->h; z++) {
        int a = (z * g->w + 20) * L, b = (z * g->w + 21) * L;
        g->edges[a] &= (unsigned char)~0x83u; g->edges[b] &= (unsigned char)~0x38u;   // straight + the two diagonals
    }
    g->phase = 2;
    float out[3 * 64];
    m = nav_drive_mask(g, &count);
    nav_set_path_owner(-1); nav_set_path_mask(m);
    int r = nav_find_path(g, NULL, 21, 0, 41, 61, 0, 41, out, 64);
    nav_set_path_mask(NULL); nav_free_mask(m);
    assert(r == 0);   // no fence record: a wall from edge to edge - no route
    for (int z = 0; z < g->h; z++) { g->fences[(z * g->w + 20) * L] = 0x01; g->fences[(z * g->w + 21) * L] = 0x10; }
    m = nav_drive_mask(g, &count);
    nav_set_path_mask(m);
    r = nav_find_path(g, NULL, 21, 0, 41, 61, 0, 41, out, 64);
    nav_set_path_mask(NULL);
    assert(r > 0 && fabsf(out[(r - 1) * 3] - 61) < 3);
    puts("PASS: a vehicle route crosses the fence line (the vehicle drives through it) instead of failing / going round");
    return 0;
}
"""

FUNCS = ['walk_clear', 'direct_retry', 'direct_path', 'step_walk', 'crouch_gap', 'drive_straight_route', 'drive_coast']


def build(core):
    parts = [PRE]
    for name in ['DIRECT_WALK_MAX', 'STEP_WALK_MAX', 'STRAIGHT_LEG', 'STRAIGHT_MAX', 'FORD_MAX_DEPTH']:
        parts.append(define(core, name))
    parts.append(typedef(core, 'BotPath'))
    parts.append('static BotPath g_paths[MAX_BOTS];')
    parts.append(typedef(core, 'DriveState'))
    parts.append('static DriveState g_drive[MAX_BOTS];')
    parts.append('typedef struct DirectUse { unsigned at; float from[3], to[3]; } DirectUse; static DirectUse g_directUse[MAX_BOTS];')
    parts.append('static unsigned g_walkCheckFrame, g_directWalks, g_stepWalks, g_straightRoutes; static int g_walkChecks;')
    parts.append('static unsigned char g_stepCandidate[MAX_BOTS];')
    for name in FUNCS:
        parts.append(function(core, name))
    parts.append(MAIN)
    return '\n'.join(parts)


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    core = (SRC / 'core.c').read_text(encoding='utf-8-sig')
    with tempfile.TemporaryDirectory(prefix='bf3-altbotmod-v8-') as tmp:
        c = Path(tmp) / 'v8.c'; c.write_text(build(core)); exe = Path(tmp) / 'v8'
        san = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g'] if a.sanitize else ['-O1']
        subprocess.run(['gcc', *san, '-std=gnu11', '-Wall', '-Wextra', '-Wno-unused-function', '-Wno-unused-parameter', '-I', str(SRC), str(c),
                        str(SRC / 'enhanced.c'), str(SRC / 'air.c'), '-lm', '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, env={'ASAN_OPTIONS': 'detect_leaks=0'})
        nc = Path(tmp) / 'nav8.c'; nc.write_text(NAVC); nexe = Path(tmp) / 'nav8'
        from nav_queue_checks import WINDOWS
        (Path(tmp) / 'windows.h').write_text(WINDOWS)
        subprocess.run(['gcc', *san, '-std=gnu11', '-Wall', '-Wno-unused-function', '-Wno-unused-variable', '-I', tmp, '-I', str(SRC),
                        str(nc), '-lm', '-o', str(nexe)], check=True)
        subprocess.run([str(nexe)], check=True, env={'ASAN_OPTIONS': 'detect_leaks=0'})


if __name__ == '__main__':
    main()
