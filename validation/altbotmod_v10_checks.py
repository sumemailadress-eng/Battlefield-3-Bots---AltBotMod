"""AltBotMod V10 checks: sniper reach and aim (SniperEngageMeters, bolt-rifle range, chest past 150 m, fire window), and
snipers on high ground (sniper_native.inc: perch survey from the navigation grid, playing-area limit, line of sight,
choice, the walk up, holding, glassing, relocating, three perches a life, unreachable perches, one sniper per perch,
the overwatch share of lives). The real code from enhanced.c, tactics_policy.inc, combat_profile_native.inc and
sniper_native.inc runs against a mocked world (a flag on flat ground, a 15 m plateau 200 m off it, a higher hill outside
the playing area, a wall that blocks one side). These checks do not run BF3; whether the perches suit real maps is
only shown by a live round."""
from pathlib import Path
import argparse, re, subprocess, sys, tempfile
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test4_checks import _function as function

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'

CONFIG = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "enhanced.h"
int main(void)
{
    BMConfig c; bm_defaults(&c);
    assert(c.sniperRange == 550 && c.sniperOverwatch == 1);
    assert(bm_set(&c, "SniperEngageMeters", "700") == 1 && c.sniperRange == 700);
    assert(bm_set(&c, "SniperEngageMeters", "2000") == -1 && c.sniperRange == 700 && bm_set(&c, "SniperEngageMeters", "100") == -1);
    assert(bm_set(&c, "SniperOverwatch", "0") == 1 && !c.sniperOverwatch && bm_set(&c, "SniperOverwatch", "2") == -1);
    bm_defaults(&c); c.aggression = 2;
    const BMWeaponProfile *bolt = bm_weapon_profile("Weapons/SV98/U_SV98", 1, 0), *dmr = bm_weapon_profile("Weapons/SKS/U_SKS", 1, 0);
    assert(bolt->family == BM_W_BOLT && dmr->family == BM_W_DMR);
    assert(fabsf(bm_weapon_range(&c, bolt) - 550) < 0.01f);              // V9: min(500, 320 x 1.15) = 368 m
    c.sniperRange = 900; assert(fabsf(bm_weapon_range(&c, bolt) - 900) < 0.01f);
    c.sniperRange = 200; assert(fabsf(bm_weapon_range(&c, bolt) - 450) < 0.01f);   // never below ReconEngageMeters (V25: 450 m)
    bm_defaults(&c); c.aggression = 2;
    assert(fabsf(bm_weapon_range(&c, dmr) - 500) < 0.01f);       // marksman rifles: ReconEngageMeters x 1.15 (V25: 450 m), capped at the rifle's 500 m
    puts("PASS: SniperEngageMeters (default 550, 150-900) and SniperOverwatch read from enhanced.ini; bolt rifles reach 550 m (V9 368 m), never less than ReconEngageMeters; marksman rifles unchanged");
    return 0;
}
'''

PRE = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "enhanced.h"
#include "nav.h"
#define MAX_BOTS 4
#define OFF_PLAYER_SOLDIER 16
#define OFF_PLAYER_ONLINE_NAME 64
#define OFF_SOLDIER_POSITION 0
#define RAY_DONT_CHECK_CHARACTER 0x20
#define TASK_NONE 0
#define TASK_FIGHT 8
#define BM_MODE_CQ 1
typedef struct { int task, crouched, enroute, waiting; } BotBrain;
typedef struct { int mode; float targetYaw, x, z; } BotNav;
static struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; float botMove[MAX_BOTS][3]; } host, *H = &host;
static BMConfig g_enhanced;
static char players[MAX_BOTS][128] __attribute__((aligned(8)));
static float bodies[MAX_BOTS][4];
static BotBrain brains[MAX_BOTS]; static BotNav navs[MAX_BOTS];
static unsigned g_rayFlags; static int g_lastPlanDeferred;
static unsigned char g_fightSlot[MAX_BOTS];
static NavGrid grid, *gridPtr = &grid;
static int g_flagCount = 2; static float flags[2][3] = {{0, 0, 0}, {0, 0, -450}};
static const void *g_modeProfile; static int g_cq = 1;
static int crouches, stands, plans, planFails, deferPlans, rays, boltRifle[MAX_BOTS] = {1, 1, 0, 1};
static NavGrid **nav_slot(void) { return &gridPtr; }
static float *flag_pos(int f) { return flags[f]; }
static int mode_is_cq(void) { return g_cq; }
static int mode_inside(const float *p, float m) { (void)p; (void)m; return 1; }
static BotBrain *bot_brain(int i) { return &brains[i]; }
static BotNav *bot_nav(int i) { return &navs[i]; }
static void bot_crouch(int i) { brains[i].crouched = 1; crouches++; }
static void bot_stand(int i) { (void)i; stands++; }
static void bot_log(const char *f, ...) { (void)f; }
static char *player_vehicle(char *p) { (void)p; return NULL; }
static float dist2(const float *a, const float *b) { float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return x * x + y * y + z * z; }
static float hdist(const float *a, const float *b) { float dx = a[0] - b[0], dz = a[2] - b[2]; return sqrtf(dx * dx + dz * dz); }
static float yaw_towards(float x, float z) { return atan2f(x, z); }
static int is_recon(int i) { return strstr(players[i] + 64, "Recon") != NULL; }
static const BMWeaponProfile *infantry_weapon(int i) { return bm_weapon_profile(boltRifle[i] ? "Weapons/M40A5/U_M40A5" : "Weapons/SKS/U_SKS", 1, 0); }
static int plan_budget_left(void) { return 1; }
static int plan_path(int i, char *s, float x, float y, float z) {
    (void)i; (void)s; (void)x; (void)y; (void)z;
    g_lastPlanDeferred = 0; plans++;
    if (deferPlans) { deferPlans--; g_lastPlanDeferred = 1; return 0; }
    if (planFails) { planFails--; return 0; }
    return 4;
}
// The world: flat ground at y = 0, flags at z = 0 and z = -450 (the playing area: x -120..120, z -570..120); a 15 m
// plateau (|x| < 90, -340 < z < -170); a 4.5 m rise (|x| < 60, -150 < z < -110) behind a 6 m wall at z = -100..-90 that
// hides the flag from it; a 40 m hill at x > 250, outside the playing area; water at x < -300.
static float ground(float x, float z) {
    if (fabsf(x) < 90 && z < -170 && z > -340) return 15;
    if (x > 250) return 40;
    if (fabsf(x) < 60 && z > -150 && z < -110) return 4.5f;
    return 0;
}
int nav_floors_at(const NavGrid *g, float x, float y, float z, float *h, int max) { (void)g; (void)y; if (max < 1) return 0; h[0] = ground(x, z); return 1; }
int nav_snap(const NavGrid *g, float x, float y, float z, float *out) { (void)g; out[0] = x; out[1] = ground(x, z); out[2] = z; (void)y; return 1; }
int nav_is_water(const NavGrid *g, float x, float y, float z) { (void)g; (void)y; (void)z; return x < -300; }
static int ray_cast(const float *a, const float *b, float *hit, float *n) {
    (void)hit; (void)n; rays++;
    // the wall at z = -100..-90, 6 m high
    if ((a[2] < -100 && b[2] > -90) || (b[2] < -100 && a[2] > -90)) {
        float t = (-95 - a[2]) / (b[2] - a[2]), y = a[1] + (b[1] - a[1]) * t;
        if (y < 6) return 1;
    }
    return 0;
}
static int g_perchGadgets;static void place_class_gadget(int i, char *soldier) { (void)i; (void)soldier; g_perchGadgets++; }   /* AltBotMod V16: snipers place their equipment at the perch */
'''

MAIN = r'''
static void life(int i, const char *name, float x, float y, float z)
{
    static float spare[64][4]; static int next;
    float *s = spare[next++ & 63]; s[0] = x; s[1] = y; s[2] = z;
    *(float **)(players[i] + OFF_PLAYER_SOLDIER) = s; strcpy(players[i] + 64, name);
    brains[i] = (BotBrain){0}; navs[i] = (BotNav){0};
}
static float *pos(int i) { return *(float **)(players[i] + OFF_PLAYER_SOLDIER); }
static int tick(int i, int key) { H->frame++; return sniper_overwatch_tick(i, &brains[i], (char *)pos(i), flags[key], key); }
static int survey(int i, int key) { int r = 0; for (int k = 0; k < 12 && g_snipe[i].state != SNIPE_GOING; k++) r = tick(i, key); return r; }
int main(void)
{
    bm_defaults(&g_enhanced); g_enhanced.aggression = 2; grid.phase = 2; grid.serial = 7;
    H->botCount = 4; H->frame = 1000;
    for (int k = 0; k < 4; k++) H->bots[k] = players[k];
    // reach: a bolt-rifle recon sees and fights out to SniperEngageMeters; a marksman-rifle recon keeps the recon range
    life(0, "[BOT] Recon RU 1", 0, 0, -400); life(2, "[BOT] Recon RU 3", 0, 0, -400);
    assert(fabsf(sniper_reach(0) - fmaxf(550, RECON_SPOTTED_RANGE)) < 0.01f && fabsf(sniper_reach(2) - RECON_SPOTTED_RANGE) < 0.01f && RECON_SPOTTED_RANGE < 700);   /* (V25: recon 450 m x 1.35 = 607 m, past 550) */
    puts("PASS: a bolt-rifle recon spots, engages and keeps fighting soldiers out to SniperEngageMeters or the recon range, whichever is further (V25: 607 m at Aggression 2); a marksman-rifle recon keeps the recon range");
    // which lives go on overwatch: ~70 % of a bolt-rifle recon's, none of a marksman's or an assault's
    int over = 0;
    for (int k = 0; k < 400; k++) { char n[32]; snprintf(n, sizeof n, "[BOT] Recon US %d", k % 9); life(1, n, 0, 0, -400);
        sniper_overwatch_tick(1, &brains[1], (char *)pos(1), flags[0], 0); over += g_snipe[1].state != SNIPE_OFF; g_snipe[1].state = SNIPE_OFF; }
    assert(over > 230 && over < 330);
    for (int k = 0; k < 50; k++) { life(2, "[BOT] Recon RU 3", 0, 0, -400); assert(!sniper_overwatch_tick(2, &brains[2], (char *)pos(2), flags[0], 0)); }
    for (int k = 0; k < 50; k++) { life(1, "[BOT] Assault US 2", 0, 0, -400); assert(!sniper_overwatch_tick(1, &brains[1], (char *)pos(1), flags[0], 0)); }
    g_enhanced.sniperOverwatch = 0;
    for (int k = 0; k < 50; k++) { char n[32]; snprintf(n, sizeof n, "[BOT] Recon US %d", k); life(1, n, 0, 0, -400); assert(!sniper_overwatch_tick(1, &brains[1], (char *)pos(1), flags[0], 0)); }
    g_enhanced.sniperOverwatch = 1;
    printf("PASS: %d of 400 bolt-rifle recon lives go on overwatch (~70%%); marksman-rifle recon, other classes and SniperOverwatch=0 never do\n", over);
    // the first sniper: the survey runs a ring a tick (the bot waits a few ticks), then it heads for the plateau
    const char *sniper = NULL; char n[32];
    for (int k = 0; k < 400 && !sniper; k++) { snprintf(n, sizeof n, "[BOT] Recon RU %d", k); life(0, n, 10, 0, -400); g_snipe[0].soldier = NULL;
        sniper_overwatch_tick(0, &brains[0], (char *)pos(0), flags[0], 0); if (g_snipe[0].state == SNIPE_UNDECIDED) sniper = n; }
    assert(sniper);
    int ticks = 0; rays = 0;
    while (g_snipe[0].state == SNIPE_UNDECIDED && ticks < 20) { assert(tick(0, 0) == 1 && navs[0].mode == 0); ticks++; }
    assert(g_snipe[0].state == SNIPE_GOING && ticks >= 6 && ticks <= 8);
    float *spot = g_snipe[0].spot;
    assert(spot[1] == 15 && fabsf(spot[0]) < 90 && spot[2] < -170 && spot[2] > -340);   // on the plateau
    PerchSet *p = &g_perch[g_snipe[0].set];
    for (int k = 0; k < p->n; k++) { assert(p->spot[k][0] < 250 && p->elev[k] >= 4); assert(!(p->spot[k][2] < -110 && p->spot[k][2] > -150)); }
    printf("PASS: perch survey: %d tick(s), %d rays; %d perches on the plateau 15 m up; the hill outside the playing area and the rise behind the wall are not used\n", ticks, rays, p->n);
    // the walk up: one route, then it walks; on arrival it holds crouched, glassing the flag
    assert(tick(0, 0) == 1 && navs[0].mode == 3 && plans >= 1);
    assert(tick(0, 0) == 1);
    memcpy(pos(0), spot, 12);
    assert(tick(0, 0) == 1 && g_snipe[0].state == SNIPE_HOLD && brains[0].crouched && navs[0].mode == 1 && sniper_holding(0));
    float face = yaw_towards(-spot[0], -spot[2]);
    for (int k = 0; k < 200; k++) { tick(0, 0); float e = fabsf(navs[0].targetYaw - face); assert(e < 0.46f && !H->botMove[0][0]); }
    puts("PASS: it plans one route up, walks, and on the perch crouches and glasses the flag (sweeps within 26 deg of it), standing still");
    // a second sniper takes another perch at least 25 m away
    g_enhanced.sniperPairs = 0;   // (V11 pairs: altbotmod_v11_checks)
    life(3, sniper, 0, 0, -400); g_snipe[3].soldier = NULL; g_snipe[3].lives = 0;
    sniper_overwatch_tick(3, &brains[3], (char *)pos(3), flags[0], 0);
    g_snipe[3].state = SNIPE_UNDECIDED; g_snipe[3].retryAt = 0;   // (an overwatch life)
    survey(3, 0);
    assert(g_snipe[3].state == SNIPE_GOING && dist2(g_snipe[3].spot, spot) >= 25 * 25);
    g_enhanced.sniperPairs = 1;
    puts("PASS: with SniperPairs=0 two snipers never share a perch (25 m apart)");
    // pushed off the perch (cover / a fight moved it 10 m): another perch, not the old one
    float old[3]; memcpy(old, spot, 12);
    pos(0)[0] += 10; tick(0, 0);
    assert(g_snipe[0].state == SNIPE_GOING && g_snipe[0].moves == 1 && dist2(g_snipe[0].spot, old) >= 30 * 30);
    // an unreachable perch (no route up): marked, another one
    planFails = 1; memcpy(old, g_snipe[0].spot, 12); tick(0, 0);
    assert(g_snipe[0].state == SNIPE_GOING && g_snipe[0].moves == 2 && dist2(g_snipe[0].spot, old) > 1);
    // a queued route is waited for (no perch thrown away)
    deferPlans = 2; memcpy(old, g_snipe[0].spot, 12); tick(0, 0); tick(0, 0);
    assert(dist2(g_snipe[0].spot, old) < 0.01f && g_snipe[0].moves == 2); tick(0, 0); assert(navs[0].mode == 3);
    // third perch done: back to the flag loop, standing
    memcpy(pos(0), g_snipe[0].spot, 12); tick(0, 0); assert(g_snipe[0].state == SNIPE_HOLD);
    H->frame += 121 * 30; stands = 0; assert(!tick(0, 0) && g_snipe[0].state == SNIPE_OFF && !brains[0].crouched && stands);
    assert(!sniper_holding(0));
    puts("PASS: pushed off its perch, a route that fails, or time up: another perch (not the last one); a queued route is waited for; after the third it plays the flag again");
    // the objective moves on: a perch that sees the new flag stays; the rest of the time it moves
    life(0, sniper, 0, 0, -400); g_snipe[0].soldier = NULL; g_snipe[0].lives = 0;
    sniper_overwatch_tick(0, &brains[0], (char *)pos(0), flags[0], 0);
    g_snipe[0].state = SNIPE_UNDECIDED; g_snipe[0].retryAt = 0;
    {
        survey(0, 0); tick(0, 0); memcpy(pos(0), g_snipe[0].spot, 12); tick(0, 0); assert(g_snipe[0].state == SNIPE_HOLD);
        memcpy(old, g_snipe[0].spot, 12);
        tick(0, 1);   // the next flag is in view of the plateau, 130-280 m off: it stays and watches that one
        assert(g_snipe[0].key == 1 && g_snipe[0].state == SNIPE_HOLD && dist2(g_snipe[0].spot, old) < 0.01f && g_snipe[0].look[2] == -450);
    }
    // a bot near the flag already plays it
    life(1, sniper, 0, 0, -30); g_snipe[1].soldier = NULL; g_snipe[1].lives = 0;
    sniper_overwatch_tick(1, &brains[1], (char *)pos(1), flags[0], 0);
    g_snipe[1].state = SNIPE_UNDECIDED; g_snipe[1].retryAt = 0;
    assert(!sniper_overwatch_tick(1, &brains[1], (char *)pos(1), flags[0], 0) && g_snipe[1].state == SNIPE_OFF);
    // a map whose grid is still being surveyed: no standing about waiting for a perch - it plays the flag, tries later
    grid.phase = 1; life(1, sniper, 0, 0, -400); g_snipe[1].soldier = NULL; sniper_overwatch_tick(1, &brains[1], (char *)pos(1), flags[0], 0);
    g_snipe[1].state = SNIPE_UNDECIDED; g_snipe[1].retryAt = 0;
    assert(!tick(1, 0) && g_snipe[1].state == SNIPE_OFF && g_snipe[1].retryAt > H->frame + 50 * 30);
    grid.phase = 2;
    // a new map: the old perches are not reused
    grid.serial = 8; int idx; assert(!perch_set(0, flags[0], &idx) && !g_perch[idx].done);
    puts("PASS: when the objective moves on, a perch that sees the next flag is kept; a bot already near its flag plays it; while the grid is still surveyed snipers play the flags (retry in 60 s); a new map's grid surveys afresh");
    return 0;
}
'''


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    san = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g'] if a.sanitize else ['-O1']
    env = {'ASAN_OPTIONS': 'detect_leaks=0'}
    core = (SRC / 'core.c').read_text(encoding='utf-8-sig')
    inc = (SRC / 'sniper_native.inc').read_text(encoding='utf-8-sig')
    prof = (SRC / 'combat_profile_native.inc').read_text(encoding='utf-8-sig')
    spotted = re.search(r'^#define RECON_SPOTTED_RANGE[^\n]+', core, re.M)[0]
    with tempfile.TemporaryDirectory(prefix='bf3-altbotmod-v10-') as tmp:
        tmp = Path(tmp)
        def run(name, body, extra):
            c = tmp / (name + '.c'); c.write_text(body); exe = tmp / name
            subprocess.run(['gcc', *san, '-std=gnu11', '-Wall', '-Wextra', '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-unused-variable',
                            '-I', str(SRC), str(c), *[str(SRC / x) for x in extra], '-lm', '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True, env=env)
        run('config', CONFIG, ['enhanced.c', 'air.c'])
        # the aim: chest past 150 m, a 0.2 m window there
        tol = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "enhanced.h"
static BMConfig g_enhanced;
static const BMWeaponProfile *infantry_weapon(int i) { (void)i; return bm_weapon_profile("Weapons/M98B/U_M98B", 1, 0); }
static const BMDifficulty *bot_profile(int i) { (void)i; return bm_difficulty_level(5); }
''' + function(prof, 'fight_tolerance') + r'''
int main(void)
{
    bm_defaults(&g_enhanced); g_enhanced.weaponHandling = 1;
    float near = fight_tolerance(0, 1, 100), far = fight_tolerance(0, 1, 400);
    assert(fabsf(near - bm_weapon_tolerance(&g_enhanced, infantry_weapon(0), 100)) < 1e-7f);   // precise close in
    assert(fabsf(far * 400 - 0.2f) < 1e-4f && bm_weapon_tolerance(&g_enhanced, infantry_weapon(0), 400) * 400 < 0.15f);
    puts("PASS: snipers keep their 0.12 m fire window within 150 m and take 0.2 m (the chest) beyond it - V9 waited for 0.12 m at 400 m");
    return 0;
}
'''
        run('tolerance', tol, ['enhanced.c', 'air.c'])
        fight = function(core, 'fight_tick')
        assert 'hdist(me, tp) > 150.0f) sniper = 0' in fight, 'snipers aim at the chest past 150 m'
        print('PASS: fight_tick aims a sniper at the head only within 150 m, the chest beyond')
        body = PRE + spotted + '\n' + inc + MAIN
        run('sniper', body, ['enhanced.c', 'air.c'])
        # wiring in the real core: perched snipers are not moved by the fight code, the watchdog leaves them alone
        mot = (SRC / 'combat_motion.inc').read_text(); act = (SRC / 'activity_native.inc').read_text()
        assert 'sniper_holding(i))return 0;' in mot and 'sniper_holding(i) && (b->task==TASK_NONE' in act
        assert 'sniper_holding(i))' in prof and 'perched = sniper_holding(i)' in core and 'sniper_overwatch_tick(i, b, soldier, flag_pos(of), of)' in core
        assert 'sniper_overwatch_tick(i,bot_brain(i),soldier,goal,1000+target)' in (SRC / 'modes_native.inc').read_text()
        assert 'FORGET(g_snipe)' in core and 'FORGET(g_nadeStep)' in core
        print('PASS: wired in - Conquest and Rush brains, no side steps / flanking moves / firing-spot walks on a perch, the stuck watchdog leaves a perched sniper be, per-bot tables compacted on roster removal')


if __name__ == '__main__':
    main()
