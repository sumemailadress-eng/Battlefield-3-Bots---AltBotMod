"""AltBotMod V9 checks: the difficulty profiles taken from the dev's BotMod build (table, parsing, per-bot levels,
burst / pause scaling), the gunfight helpers (fire window, far crouch, side-step room, sidearm swap, grenades at
infantry), the ticket charge (a bot's first deploy of the round is free, respawns cost one), helicopter air-to-air
stand-off, and the Team Deathmatch route files. Real code from enhanced.c, air.c, trace_nav.c, core.c and
combat_profile_native.inc, engine mocked. These checks do not run BF3."""
from pathlib import Path
import argparse, re, struct, subprocess, sys, tempfile
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test4_checks import _function as function

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
TRACES = ROOT / 'files/server/BotMod_traces'
DEV_DLL = Path('/mnt/user-data/uploads/BF3-BotMod-Public-2026-10-10/ServerOverlay/BotModCore.dll')

CONFIG = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "enhanced.h"
int main(int argc, char **argv)
{
    BMConfig c; bm_defaults(&c);
    assert(c.difficulty == 5 && !c.randomDifficulty);   // elite unless enhanced.ini says otherwise
    assert(bm_difficulty_parse("elite") == 5 && bm_difficulty_parse("Veteran") == 4 && bm_difficulty_parse(" hard ") == 3);
    assert(bm_difficulty_parse("normal") == 2 && bm_difficulty_parse("easy") == 1 && bm_difficulty_parse("off") == 0);
    assert(bm_difficulty_parse("3") == 3 && bm_difficulty_parse("0") == 0 && bm_difficulty_parse("6") == -1 && bm_difficulty_parse("brutal") == -1);
    assert(bm_set(&c, "Difficulty", "veteran") == 1 && c.difficulty == 4);
    assert(bm_set(&c, "Difficulty", "nope") == -1 && c.difficulty == 4);   // invalid input changes nothing
    assert(bm_set(&c, "RandomDifficulty", "1") == 1 && c.randomDifficulty == 1 && bm_set(&c, "RandomDifficulty", "2") == -1);
    puts("PASS: Difficulty (off / easy..elite / 0-5) and RandomDifficulty read from enhanced.ini, elite by default, bad values ignored");
    // the dev build's table, field by field, when its DLL is here (argv[1] = 5 x 31 values it holds)
    const BMDifficulty *e = bm_difficulty_level(5), *n = bm_difficulty_level(2);
    assert(e->reaction == 0.05f && e->burst == 26 && e->pause == 2 && e->aimMargin == 0.45f && e->engageRange == 300 && e->detectRange == 350);   /* V26: elite engages from 300 m (V25: 240, V22: 130) */
    assert(e->fov == 170 && e->advanceStop == 8 && e->coverBelow == 10 && e->strafe == 1.0f && e->jumpFight == 1.8f && e->farCrouch == 22 && e->ads == 18);
    assert(n->burst == 12 && n->pause == 10 && bm_difficulty_level(9) == e && bm_difficulty_level(-3) == bm_difficulty_level(1));
    if (argc > 1) {
        FILE *f = fopen(argv[1], "r"); assert(f);
        for (int lv = 1; lv <= 5; lv++) {
            const BMDifficulty *d = bm_difficulty_level(lv);
            const float mine[31] = {d->reaction, (float)d->burst, (float)d->pause, d->aimTol, d->aimMargin, d->aimLag, d->turnGain, d->fightRange,
                d->engageRange, d->detectRange, d->fov, d->cooldown, d->breather, d->coverBelow, d->advanceStop, d->coverHold, d->sprint,
                d->fightSprint, d->strafe, d->jumpRun, d->jumpFight, d->farCrouch, d->farCrouchChance, (float)d->settle, d->ads, d->guard,
                d->guardSpread, d->aimWorsening, d->skillSpread, (float)d->fireModel, d->rangeFalloff};
            for (int k = 0; k < 31; k++) { float v; assert(fscanf(f, "%f", &v) == 1); assert(lv >= 3 || fabsf(v - mine[k]) < 0.01f); }   /* (V22: elite, V25: veteran, V26: hard too - deliberately sharper than the dev's) */
        }
        fclose(f);
        puts("PASS: easy and normal (2 x 31 profile values) equal the table in the dev's BotModCore.dll (read from its .rdata here); hard, veteran and elite are AltBotMod's sharper ones (V22-V26)");
    }
    // per-bot levels: one either side, never outside 1..5, the same for the same bot
    c.difficulty = 5; c.randomDifficulty = 1; int seen[6] = {0};
    char name[32];
    for (int k = 0; k < 200; k++) { snprintf(name, sizeof name, "[BOT] Medic US %d", k); int l = bm_bot_difficulty(&c, name);
        assert(l >= 4 && l <= 5 && l == bm_bot_difficulty(&c, name)); seen[l]++; }
    assert(seen[4] > 20 && seen[5] > 20);
    c.difficulty = 3; for (int k = 0; k < 200; k++) { snprintf(name, sizeof name, "b%d", k); int l = bm_bot_difficulty(&c, name); assert(l >= 2 && l <= 4); seen[l]++; }
    assert(seen[2] && seen[3] && seen[4]);
    c.difficulty = 0; assert(bm_bot_difficulty(&c, "x") == 0);
    c.difficulty = 4; c.randomDifficulty = 0; assert(bm_bot_difficulty(&c, "x") == 4);
    puts("PASS: RandomDifficulty gives each bot its own level one either side (stable per bot, clamped to easy..elite); off = V8 combat");
    // bursts and pauses: normal plays the weapon's own rhythm; elite holds the trigger ~1.7x longer and pauses a third
    assert(bm_profile_burst(n, 9, 10) == 9 && bm_profile_pause(n, 10) == 10);
    assert(bm_profile_burst(e, 9, 10) == 20 && bm_profile_burst(e, 9, 30) == 9 && bm_profile_burst(e, 9, 80) == 5);   // V23: close x2, at range at most x1.25 (recoil)
    assert(bm_profile_pause(e, 10) == 2 && bm_profile_pause(e, 1) == 1 && bm_profile_burst(e, 0, 5) >= 1);
    assert(bm_profile_burst(bm_difficulty_level(1), 9, 10) == 6 && bm_profile_pause(bm_difficulty_level(1), 10) == 16);
    puts("PASS: weapon bursts / pauses scale with the level (elite x1.67 / x0.3, easy x0.67 / x1.6), far shots stay short taps");
    return 0;
}
'''

AIR = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "enhanced.h"
#define DT (1.0f / 30)
static void face(BMAirObservation *o, float yaw)
{
    o->right[0] = cosf(yaw); o->right[1] = 0; o->right[2] = -sinf(yaw);
    o->up[0] = 0; o->up[1] = 1; o->up[2] = 0;
    o->forward[0] = sinf(yaw); o->forward[1] = 0; o->forward[2] = cosf(yaw);
}
static BMAirObservation duel(float range, float closing)
{
    BMAirObservation o; memset(&o, 0, sizeof o);
    o.groundValid = o.velocityValid = 1; o.groundY = 0; o.position[1] = 120; o.goal[1] = 110; o.goal[2] = 900;
    face(&o, 0); o.velocity[2] = closing;
    o.targetValid = o.targetVisible = o.targetAir = 1; o.target[1] = 121; o.target[2] = range;
    return o;
}
int main(void)
{
    BMConfig cfg; bm_defaults(&cfg); cfg.air.weapons = 1;
    const BMAirProfile *heli = bm_air_profile("AH1Z"); BMAirController s; BMAirOutput out;
    assert(heli && heli->kind != BM_AIR_JET);
    // 400 m off, nose on: a gun duel - it attacks and fires
    BMAirObservation o = duel(400, 0); bm_air_start(&s, heli->kind, 1);
    int fired = 0, attacking = 0;
    for (int k = 0; k < 30; k++) { assert(bm_air_step(&s, heli, &cfg.air, &o, DT, &out)); fired |= out.fire; attacking |= out.attacking; }
    assert(attacking && fired);
    // 750 m: still in the duel window (V8: only out to 700 m)
    o = duel(750, 0); bm_air_start(&s, heli->kind, 1); attacking = 0;
    for (int k = 0; k < 5; k++) { assert(bm_air_step(&s, heli, &cfg.air, &o, DT, &out)); attacking |= out.attacking; }
    assert(attacking);
    puts("PASS: a helicopter engages an enemy helicopter from 150-800 m and fires (V8 flew in to under 700 m)");
    // 75 m: too close (V24: under 80 m, V9-V23: 150 m) - breaks off towards its stand-off ring: the goal is away from the target, no firing
    o = duel(75, 0); bm_air_start(&s, heli->kind, 1);
    for (int k = 0; k < 5; k++) assert(bm_air_step(&s, heli, &cfg.air, &o, DT, &out));
    assert(!out.attacking && !out.fire && fabsf(out.yawError) > 1.2f);
    // 130 m and closing at 20 m/s (V24: under 150 m closing at 15+ m/s; V9-V23: 250 m at 8): break off as well; 220 m holding station: fight on
    o = duel(130, 20); bm_air_start(&s, heli->kind, 1);
    for (int k = 0; k < 5; k++) assert(bm_air_step(&s, heli, &cfg.air, &o, DT, &out));
    assert(!out.attacking && !out.fire);
    o = duel(220, 0); bm_air_start(&s, heli->kind, 1); attacking = 0;
    for (int k = 0; k < 10; k++) { assert(bm_air_step(&s, heli, &cfg.air, &o, DT, &out)); attacking |= out.attacking; }
    assert(attacking);
    // 50 m: collision course - climbs out (recovery), never fires
    o = duel(50, 5); bm_air_start(&s, heli->kind, 1);
    assert(bm_air_step(&s, heli, &cfg.air, &o, DT, &out) && out.recovering && !out.fire && out.throttle > 0.9f);
    puts("PASS: inside 150 m (or 250 m and closing fast) it breaks off to a 350 m stand-off ring, inside 70 m it climbs out - no ramming");
    // a ground target is unchanged (no stand-off ring for tanks / infantry under the helicopter)
    o = duel(300, 0); o.targetAir = 0; o.target[1] = 0; bm_air_start(&s, heli->kind, 1); attacking = 0;
    for (int k = 0; k < 20; k++) { assert(bm_air_step(&s, heli, &cfg.air, &o, DT, &out)); attacking |= out.attacking; }
    assert(attacking);
    puts("PASS: ground attack runs unchanged");
    return 0;
}
'''

TRACE = r'''
#include <assert.h>
#include <stdio.h>
#include "trace_nav.h"
int main(int argc, char **argv)
{
    int maps = 0, points = 0;
    for (int a = 1; a < argc; a++) {
        TraceNet t; assert(tn_load(&t, argv[a]));
        assert(t.count > 200 && t.pathCount >= 3 && t.edgeCount >= t.count);
        maps++; points += t.count; tn_free(&t);
    }
    assert(maps == 29);
    printf("PASS: 29 Team Deathmatch route files (the dev build's set) load as Fun-Bots routes - %d waypoints\n", points);
    return 0;
}
'''

PROFILE_PRE = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "enhanced.h"
#define MAX_BOTS 4
#define OFF_PLAYER_SOLDIER 16
#define OFF_PLAYER_ONLINE_NAME 64
#define OFF_SOLDIER_POSITION 0
#define OFF_SOLDIER_SPAWN_POSITION 0
#define OFF_SOLDIER_YAW 16
#define OFF_SOLDIER_PITCH 20
#define OFF_SOLDIER_POSE 24
#define OFF_SOLDIER_HEALTH 28
#define RAY_DONT_CHECK_CHARACTER 0x20
#define RAY_CHECK_DETAIL_MESH 1
#define NAV_HIT_WATER 1
#define EIA_JUMP 12
#define EIA_FIRE 7
#define SLOT_ACTION(x) (100 + (x))
#define TASK_NONE 0
#define TASK_FIGHT 1
#define TASK_ROCKET 2
#define GRENADE_SPEED 17.0f
#define FIGHT_MAX_FRAMES (20 * 30)
typedef struct { int task, step, crouched; unsigned stepAt, taskUntil; } BotBrain;
typedef struct { int mode; float x, z, targetYaw; } BotNav;
typedef struct { float x, z; unsigned frame; } Progress;
typedef struct { int count, next; short link[8]; } BotPath;
static struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; } host, *H = &host;
static BMConfig g_enhanced;
static char players[MAX_BOTS][128] __attribute__((aligned(8)));
static float bodies[MAX_BOTS][8];
static BotBrain brains[MAX_BOTS]; static BotNav navs[MAX_BOTS]; static Progress g_progress[MAX_BOTS]; static BotPath g_paths[MAX_BOTS];
static unsigned g_rayFlags, g_fenceUntil[MAX_BOTS], g_vaultUntil[MAX_BOTS], g_crouchWalkUntil[MAX_BOTS], g_sprintUntil[MAX_BOTS], g_fightSeenAt[MAX_BOTS];
static unsigned char g_stairOn[MAX_BOTS], g_gliding[MAX_BOTS], g_fightAdvance[MAX_BOTS], g_fightChase[MAX_BOTS], g_fightSlot[MAX_BOTS];
static char *g_fightTarget[MAX_BOTS]; static float g_fightLast[MAX_BOTS][3], g_aimPitch[MAX_BOTS]; static int g_fightBursts[MAX_BOTS], g_grenadesThisLife[MAX_BOTS];
static int crouches, stands, presses[256], loaded = 30, spare = 90, pistol = 12, grenadeSlot = 5, grenadeAmmo = 2, wallLeft, wallRight, water;
static const BMWeaponProfile *infantry_weapon(int i) { (void)i; return bm_weapon_profile(NULL, 0, 0); }
static const BMDifficulty *bot_profile(int i) { int l = bm_bot_difficulty(&g_enhanced, players[i] + OFF_PLAYER_ONLINE_NAME); return l ? bm_difficulty_level(l) : NULL; }
static BotBrain *bot_brain(int i) { return &brains[i]; }
static BotNav *bot_nav(int i) { return &navs[i]; }
static void bot_crouch(int i) { (void)i; crouches++; }
static void bot_stand(int i) { (void)i; stands++; }
static void press_button(int i, int a, int f) { (void)i; (void)f; if (a >= 0 && a < 256) presses[a]++; }
static void bot_log(const char *f, ...) { (void)f; }
static int player_exists(char *p) { return p >= players[0] && p <= players[MAX_BOTS - 1]; }
static char *player_vehicle(char *p) { (void)p; return NULL; }
static float dist2(const float *a, const float *b) { float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return x * x + y * y + z * z; }
static float yaw_towards(float x, float z) { return atan2f(x, z); }
static int soldier_ammo2(char *s, int slot, int *l, int *sp) { (void)s; if (slot == 0) { *l = loaded; *sp = spare; } else { *l = pistol; *sp = 0; } return 1; }
static int bot_gadget_slot(int i, const char *a, const char *b) { (void)i; (void)a; (void)b; return grenadeSlot; }
static int bot_ammo(int i, int slot) { (void)i; (void)slot; return grenadeAmmo; }
static int nav_hit_class(void) { return water; }
// the world: flat ground at y=0, walls 1 m to the left (+strafe) / right
static int ray_cast(const float *a, const float *b, float *hit, float *n) {
    (void)n;
    if (fabsf(a[0] - b[0]) < 1e-3f && fabsf(a[2] - b[2]) < 1e-3f) { if (a[1] >= 0 && b[1] <= 0) { hit[0] = a[0]; hit[1] = 0; hit[2] = a[2]; return 1; } return 0; }
    float side = b[0] - a[0];   // facing +z: left is -x
    if ((side < -0.5f && wallLeft) || (side > 0.5f && wallRight)) { memcpy(hit, b, 12); return 1; }
    return 0;
}
static float bm_infantry_tolerance_v8(int recon, float d) { return bm_infantry_tolerance(&g_enhanced, recon, d); }
'''

PROFILE_MAIN = r'''
int main(void)
{
    bm_defaults(&g_enhanced); g_enhanced.weaponHandling = 1; H->botCount = 2; H->frame = 1000;
    for (int k = 0; k < 2; k++) { H->bots[k] = players[k]; *(float **)(players[k] + OFF_PLAYER_SOLDIER) = bodies[k]; }
    strcpy(players[0] + 64, "[BOT] Assault US"); strcpy(players[1] + 64, "[BOT] Recon RU");
    char *me = (char *)bodies[0];
    // fire window: elite opens up with 0.5 m of the aim point (V8 0.2 m), capped by the profile's angle close in
    float t50 = fight_tolerance(0, 0, 50), t3 = fight_tolerance(0, 0, 3);
    g_enhanced.difficulty = 0; float v8 = fight_tolerance(0, 0, 50); g_enhanced.difficulty = 5;
    assert(t50 > v8 * 2.0f && t3 <= 0.065f + 1e-6f && fight_tolerance(0, 1, 50) == bm_weapon_tolerance(&g_enhanced, infantry_weapon(0), 50));
    puts("PASS: elite fire window 0.5 m (2.5x V8's), at most 0.065 rad close in; snipers keep their precise window");
    // far fight crouch: beyond 22 m, standing, not closing in, 60 % of draws; never when advancing
    int got = 0;
    for (int k = 0; k < 400; k++) { crouches = 0; g_fightAdvance[0] = 0; brains[0].crouched = 0; fight_posture_start(0, me, 60); got += crouches; if (crouches) fight_posture_end(0); }
    assert(got > 180 && got < 300);
    crouches = 0; for (int k = 0; k < 50; k++) fight_posture_start(0, me, 10); assert(!crouches);
    g_fightAdvance[0] = 1; for (int k = 0; k < 50; k++) fight_posture_start(0, me, 60); assert(!crouches);
    g_fightAdvance[0] = 0; stands = 0; srand(7); while (!crouches) fight_posture_start(0, me, 60); fight_posture_end(0); assert(stands == 1);
    puts("PASS: an elite bot crouches for a far fight (>22 m) about 60% of the time, never when closing in, and stands when it ends");
    // side-step room: walls / water on a side are seen, open ground is free
    float pos[3] = {0, 0, 0};
    assert(dodge_room(pos, 0, 1) && dodge_room(pos, 0, -1));
    wallLeft = 1; assert(!dodge_room(pos, 0, 1) && dodge_room(pos, 0, -1)); wallLeft = 0;
    water = 1; assert(!dodge_room(pos, 0, 1)); water = 0;
    puts("PASS: side steps check the side for walls, drops and water first");
    // sidearm: empty magazine, enemy at 10 m, rounds in the pistol -> pistol (not a reload); at 40 m or with ammo: no
    BotBrain *b = &brains[0]; b->task = TASK_FIGHT; b->step = 3; g_fightSlot[0] = 0;
    loaded = 0; assert(fight_sidearm_swap(0, b, me, 10, 1) && g_fightSlot[0] == 1 && presses[SLOT_ACTION(1)] == 1);
    g_fightSlot[0] = 0; assert(!fight_sidearm_swap(0, b, me, 40, 1)); loaded = 30; assert(!fight_sidearm_swap(0, b, me, 10, 1));
    loaded = 0; pistol = 0; assert(!fight_sidearm_swap(0, b, me, 10, 1)); pistol = 12; loaded = 30;
    g_enhanced.difficulty = 0; loaded = 0; assert(!fight_sidearm_swap(0, b, me, 10, 1)); loaded = 30; g_enhanced.difficulty = 5;
    puts("PASS: close in with an empty magazine the bot swaps to its pistol instead of reloading (not at range, not with ammo left, not with Difficulty=off)");
    // grenade at a target that ducked behind cover 18 m away: out, aimed on the arc, thrown, rifle back
    char *ts = (char *)bodies[1]; g_fightTarget[0] = players[1];
    bodies[0][0] = bodies[0][1] = bodies[0][2] = 0; g_fightLast[0][0] = 0; g_fightLast[0][1] = 0; g_fightLast[0][2] = 18;
    g_fightSeenAt[0] = H->frame - 60; b->taskUntil = H->frame + FIGHT_MAX_FRAMES; g_grenadesThisLife[0] = 0;
    assert(fight_grenade_tick(0, b, me, ts, 0, 18) && g_nadeStep[0] == 1 && presses[SLOT_ACTION(5)] == 1);
    int thrown = 0;
    for (int k = 0; k < 120 && g_nadeStep[0]; k++) {
        H->frame++;
        *(float *)(me + OFF_SOLDIER_YAW) = navs[0].targetYaw; *(float *)(me + OFF_SOLDIER_PITCH) = g_aimPitch[0];   // (the look follows)
        int r = fight_grenade_tick(0, b, me, ts, 0, 18);
        if (presses[EIA_FIRE] && !thrown) { thrown = 1; assert(g_aimPitch[0] > 0.1f && g_grenadesThisLife[0] == 1); }
        if (!g_nadeStep[0]) assert(!r);
    }
    assert(thrown && !g_nadeStep[0] && presses[SLOT_ACTION(0)] >= 1);
    // not again for 15 s; not at 40 m; not for a normal-level bot
    H->frame += 30; g_fightSeenAt[0] = H->frame - 60; assert(!fight_grenade_tick(0, b, me, ts, 0, 18));
    H->frame += 16 * 30; g_fightSeenAt[0] = H->frame - 60; g_fightLast[0][2] = 40; assert(!fight_grenade_tick(0, b, me, ts, 0, 40));
    g_fightLast[0][2] = 18; g_enhanced.difficulty = 2; assert(!fight_grenade_tick(0, b, me, ts, 0, 18)); g_enhanced.difficulty = 5;
    // a standoff: visible, 4+ bursts, 6 s in, target not moving for 3 s
    g_grenadesThisLife[0] = 0; g_nadeCooldown[0] = 0; g_fightBursts[0] = 5; bodies[1][2] = 20; b->taskUntil = H->frame + FIGHT_MAX_FRAMES - 7 * 30;
    assert(!fight_grenade_tick(0, b, me, ts, 1, 20));
    H->frame += 4 * 30; b->taskUntil -= 4 * 30; assert(fight_grenade_tick(0, b, me, ts, 1, 20));
    puts("PASS: hard+ bots throw a grenade at one that ducked behind cover (8-28 m) or sits still in a standoff - two a life, one per 15 s");
    return 0;
}
'''

TICKETS_PRE = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define BM_MODE_RUSH 3
#define OFF_PLAYER_SOLDIER 16
#define OFF_TC_TICKETS 0xa0
#define OFF_TC_INITIAL 0xa8
#define TICKET_BLEED_PER_MIN 15.0f
#define GAME_BLEED_GRACE_FRAMES (20 * 30)
static struct { unsigned frame; } host, *H = &host;
static int g_mode, g_ticketsOn = 1, g_roundEnded, g_flagCount;
static struct { float botTicketCost; } g_enhanced = { 1.0f };   /* AltBotMod V16: BotTicketCost */
static char g_modeName[64]; static int bm_mode(const char *m) { (void)m; return g_mode; }   /* AltBotMod V15: tickets follow the server's mode */
static unsigned g_roundEndedAt, g_gameTicketsAt[3], g_ticketsFoundAt, g_zeroAt;
static float g_ticketsSet[3];
static char counters[3][0x100] __attribute__((aligned(8)));
static char *g_ticketCounter[3] = {NULL, counters[1], counters[2]};
static char *g_lastSoldierOf[64], *g_lastPlayerAt[64], *g_hadSoldier[64];
static unsigned g_freeDeploys, g_chargedDeploys;
static char players[4][64] __attribute__((aligned(8))); static char soldiers[8][8];
static void *arr[4]; static char pm[0x100] __attribute__((aligned(8)));
static char *player_manager(void) { return pm; }
static int mode_is_cq(void) { return 1; }
static void rush_tickets_tick(void) {}
static void chat_rcon(const char *f, ...) { (void)f; }
static void chat_say(const char *f, ...) { (void)f; }
static void bot_log(const char *f, ...) { (void)f; }
static int is_bot(char *p) { return p != players[3]; }
static int player_team(char *p) { return p == players[0] || p == players[3] ? 1 : 2; }
static int flag_owner(int f) { (void)f; return 0; }
static int IsBadReadPtr(const void *p, int n) { (void)p; (void)n; return 0; }
static float *tickets_of(int team) { return (float *)(g_ticketCounter[team] + OFF_TC_TICKETS); }
static void tickets_take(int team, float n) { float *t = tickets_of(team); *t = *t - n > 0 ? *t - n : 0; g_ticketsSet[team] = *t; }
'''
TICKETS_MAIN = r'''
static void setp(int k, char *s) { *(char **)(players[k] + OFF_PLAYER_SOLDIER) = s; }
int main(void)
{
    for (int t = 1; t <= 2; t++) { *tickets_of(t) = 200; g_ticketsSet[t] = 200; *(int *)(g_ticketCounter[t] + OFF_TC_INITIAL) = 200; }
    for (int k = 0; k < 4; k++) arr[k] = players[k];
    void **a = (void **)arr; memcpy(pm + 0xac, &a, sizeof a); unsigned count = 4; memcpy(pm + 8, &count, sizeof count);
    H->frame = 1; tickets_tick();                 // bots seen, not deployed yet
    setp(0, soldiers[0]); setp(1, soldiers[1]); setp(2, soldiers[2]); setp(3, soldiers[3]);
    H->frame = 2; tickets_tick();                 // the first deploy of the round
    assert(*tickets_of(1) == 200 && *tickets_of(2) == 200 && g_freeDeploys == 3 && !g_chargedDeploys);
    setp(0, NULL); setp(1, NULL); H->frame = 3; tickets_tick();   // two bots die
    setp(0, soldiers[4]); setp(1, soldiers[5]); H->frame = 4; tickets_tick();   // and respawn
    assert(*tickets_of(1) == 199 && *tickets_of(2) == 199 && g_chargedDeploys == 2);
    setp(3, NULL); H->frame = 5; tickets_tick(); setp(3, soldiers[6]); H->frame = 6; tickets_tick();   // the human: the game's own business
    assert(*tickets_of(1) == 199);
    g_enhanced.botTicketCost = 0.5f;   /* AltBotMod V16 default: a bot respawn costs half a ticket */
    setp(0, NULL); H->frame = 7; tickets_tick(); setp(0, soldiers[7]); H->frame = 8; tickets_tick();
    assert(*tickets_of(1) == 198.5f && *tickets_of(2) == 199);
    g_enhanced.botTicketCost = 1.0f;
    puts("PASS: tickets - a bot's first deploy of the round is free (V8 charged it: ~30 per team at every round start), each respawn after a death costs one, a human's deploy is left to the game; V16 BotTicketCost 0.5 charges half a ticket");
    return 0;
}
'''


def dev_table(path):
    d = path.read_bytes()
    # .rdata at file offset 0x62c00 = VA 0x67fa5000; the table at VA 0x67faea04, 5 records of 0x80 bytes
    off = 0x62c00 + (0x67faea04 - 0x67fa5000)
    rows = []
    for lv in range(5):
        vals = []
        for k in range(31):
            o = off + lv * 0x80 + 4 * k
            if k in (1, 2, 23, 29): vals.append(float(struct.unpack_from('<i', d, o)[0]))
            else: vals.append(struct.unpack_from('<f', d, o)[0])
        rows.append(vals)
    return rows


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    san = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g'] if a.sanitize else ['-O1']
    env = {'ASAN_OPTIONS': 'detect_leaks=0'}
    core = (SRC / 'core.c').read_text(encoding='utf-8-sig')
    inc = (SRC / 'combat_profile_native.inc').read_text(encoding='utf-8-sig')
    with tempfile.TemporaryDirectory(prefix='bf3-altbotmod-v9-') as tmp:
        tmp = Path(tmp)
        def run(name, body, extra, args):
            c = tmp / (name + '.c'); c.write_text(body); exe = tmp / name
            subprocess.run(['gcc', *san, '-std=gnu11', '-Wall', '-Wextra', '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-unused-variable',
                            '-I', str(SRC), str(c), *[str(SRC / x) for x in extra], '-lm', '-o', str(exe)], check=True)
            subprocess.run([str(exe), *args], check=True, env=env)
        args = []
        if DEV_DLL.exists():
            t = tmp / 'devtable.txt'; t.write_text('\n'.join(' '.join('%.6f' % v for v in r) for r in dev_table(DEV_DLL)))
            args = [str(t)]
        run('config', CONFIG, ['enhanced.c', 'air.c'], args)
        run('air', AIR, ['enhanced.c', 'air.c'], [])
        tdm = sorted(str(p) for p in TRACES.glob('*_TeamDeathMatch0.map') if not p.name.startswith(('COOP_', 'SP_')))   # (V18.2: + the co-op / campaign levels' files, not multiplayer maps)
        run('trace', TRACE, ['trace_nav.c'], tdm)
        pieces = [PROFILE_PRE]
        for name in ['grenade_pitch']:
            pieces.append(function(core, name))
        state = inc[:inc.index('// The fire window:')]
        state = state.replace('static int grenade_pitch(float dh, float dy, float *pitch);', '')
        pieces.append(state)
        for name in ['fight_tolerance', 'fight_posture_start', 'fight_posture_end', 'dodge_room', 'fight_sidearm_swap', 'fight_grenade_tick']:
            pieces.append(function(inc, name))
        pieces.append(PROFILE_MAIN)
        run('profile', '\n'.join(pieces), ['enhanced.c', 'air.c'], [])
        run('tickets', TICKETS_PRE + function(core, 'tickets_tick') + TICKETS_MAIN, [], [])


if __name__ == '__main__':
    main()
