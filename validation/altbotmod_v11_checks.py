"""AltBotMod V11 checks: sniper pairs on one perch, class jobs (medics / support dropping medkits and ammo boxes for
themselves and teammates near them, a bot that called for supplies waiting for its supplier, who answers a call, a
driver holding still for an engineer, the watchdog leaving class jobs be, wider revives), and that every setting the
Server Control settings window offers matches the core (key, kind, range, default). Real code from core.c,
sniper_native.inc, enhanced.c, air.c and ServerControl-Core.ps1, engine mocked. These checks do not
run BF3."""
from pathlib import Path
import argparse, re, subprocess, sys, tempfile
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test4_checks import _function as function
import altbotmod_v10_checks as v10

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
SC = ROOT / 'files/ServerControl/ServerControl-Core.ps1'

PAIRS = r'''
static void life(int i, const char *name, float x, float y, float z)
{
    static float spare[64][4]; static int next;
    float *s = spare[next++ & 63]; s[0] = x; s[1] = y; s[2] = z;
    *(float **)(players[i] + OFF_PLAYER_SOLDIER) = s; strcpy(players[i] + 64, name);
    brains[i] = (BotBrain){0}; navs[i] = (BotNav){0};
}
static float *pos(int i) { return *(float **)(players[i] + OFF_PLAYER_SOLDIER); }
static void start(int i, const char *name) {
    life(i, name, 10, 0, -400); g_snipe[i].soldier = NULL;
    sniper_overwatch_tick(i, &brains[i], (char *)pos(i), flags[0], 0);
    g_snipe[i].state = SNIPE_UNDECIDED; g_snipe[i].retryAt = 0;   // (an overwatch life)
    for (int k = 0; k < 12 && g_snipe[i].state == SNIPE_UNDECIDED; k++) { H->frame++; sniper_overwatch_tick(i, &brains[i], (char *)pos(i), flags[0], 0); }
}
int main(void)
{
    bm_defaults(&g_enhanced); g_enhanced.aggression = 2; grid.phase = 2; grid.serial = 7;
    H->botCount = 4; H->frame = 1000;
    for (int k = 0; k < 4; k++) H->bots[k] = players[k];
    assert(g_enhanced.sniperPairs == 1);
    start(0, "[BOT] Recon US 1");
    assert(g_snipe[0].state == SNIPE_GOING);
    PerchSet *p = &g_perch[g_snipe[0].set];
    for (int k = 0; k < p->n; k++) if (k != g_snipe[0].slot) p->badUntil[k] = H->frame + 99999;   // only the first one's perch is left
    start(1, "[BOT] Recon US 2");
    assert(g_snipe[1].state == SNIPE_GOING && g_snipe[1].slot == g_snipe[0].slot);
    float d = sqrtf(dist2(g_snipe[1].spot, g_snipe[0].spot));
    assert(d >= 3.0f && d <= 7.5f && fabsf(g_snipe[1].spot[1] - g_snipe[0].spot[1]) <= 1.0f);
    start(3, "[BOT] Recon US 4");
    assert(g_snipe[3].state == SNIPE_OFF);   // never three on one perch
    // a free perch is still preferred over pairing up
    for (int k = 0; k < p->n; k++) p->badUntil[k] = 0;
    start(3, "[BOT] Recon US 4");
    assert(g_snipe[3].state == SNIPE_GOING && dist2(g_snipe[3].spot, p->spot[g_snipe[0].slot]) >= 25 * 25);
    // SniperPairs=0: a taken perch is not shared
    g_enhanced.sniperPairs = 0;
    for (int k = 0; k < p->n; k++) if (k != g_snipe[0].slot) p->badUntil[k] = H->frame + 99999;
    g_snipe[1].state = SNIPE_OFF; g_snipe[3].state = SNIPE_OFF;
    start(1, "[BOT] Recon US 2");
    assert(g_snipe[1].state == SNIPE_OFF);
    printf("PASS: sniper pairs - a second sniper may share a perch, %.1f m beside the first at the same height with the flag in view; never three; a free perch is still preferred; SniperPairs=0 keeps one per perch\n", d);
    return 0;
}
'''

CLASS_PRE = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "enhanced.h"
#define MAX_BOTS 6
#define OFF_PLAYER_SOLDIER 16
#define OFF_PLAYER_ONLINE_NAME 64
#define OFF_SOLDIER_POSITION 0
#define OFF_SOLDIER_SPAWN_POSITION 0
#define OFF_SOLDIER_HEALTH 28
#define TASK_NONE 0
#define TASK_FOLLOW 1
#define TASK_DEFEND 2
#define TASK_SUPPLY 3
typedef struct { int task, enabled; char *serviceTarget; } BotBrain;
typedef struct { int mode; float targetYaw, x, z; } BotNav;
static struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; float botMove[MAX_BOTS][3]; } host, *H = &host;
static BMConfig g_enhanced;
static char players[8][128] __attribute__((aligned(8)));
static float bodies[8][8];
static BotBrain brains[MAX_BOTS]; static BotNav navs[MAX_BOTS];
static unsigned g_hitAt[MAX_BOTS]; static int g_spareMax[MAX_BOTS];
static int teams[8] = {1, 1, 1, 1, 2, 1, 1, 1}, spare[8], drops[16], dropSlot, wall, inVehicle[8], medkits = 1;
static void *arr[8]; static char pm[0x100] __attribute__((aligned(8)));
static char *player_manager(void) { return pm; }
static int player_team(char *p) { return teams[(p - players[0]) / 128]; }
static char *player_vehicle(char *p) { return inVehicle[(p - players[0]) / 128] ? (char *)1 : NULL; }
static int bot_index(void *p) { for (int i = 0; i < H->botCount; i++) if (H->bots[i] == p) return i; return -1; }
static BotBrain *bot_brain(int i) { return &brains[i]; }
static BotNav *bot_nav(int i) { return &navs[i]; }
static int bot_errand_free(int i) { return brains[i].enabled && !inVehicle[i] && (brains[i].task == TASK_NONE || brains[i].task == TASK_DEFEND || brains[i].task == TASK_FOLLOW); }
static float dist2(const float *a, const float *b) { float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return x * x + y * y + z * z; }
static float yaw_towards(float x, float z) { return atan2f(x, z); }
static int solid_between(const float *a, const float *b, float *h) { (void)h; return wall && ((a[0] < 3) != (b[0] < 3)); }   // a wall at x = 3
static void place_gadget_now(int i, int slot) { drops[i]++; dropSlot = slot; }
static void bot_log(const char *f, ...) { (void)f; }
enum { JOB_MEDKIT = 6, JOB_AMMO = 7 }; static unsigned g_jobStats[8];   /* (the "classes:" counters) */
// classes by name: Medic carries a medkit in slot 4, Support an ammo box in slot 5
static int bot_gadget_slot(int i, const char *a, const char *b) {
    (void)b; const char *n = players[i] + 64;
    if (!strcmp(a, "Medkit") && strstr(n, "Medic")) return 4;
    if (!strcmp(a, "Ammobag") && strstr(n, "Support")) return 5;
    return -1;
}
static int bot_ammo(int i, int slot) { (void)i; return slot == 4 ? medkits : 1; }
static int soldier_ammo2(char *s, int slot, int *l, int *sp) { (void)slot; int k = (int)(((float *)s - bodies[0]) / 8); *l = 30; *sp = spare[k]; return 1; }
'''
CLASS_MAIN = r'''
static void reset(void) {
    memset(drops, 0, sizeof drops); memset(g_classDropAt, 0, sizeof g_classDropAt); memset(g_hitAt, 0, sizeof g_hitAt);
    memset(brains, 0, sizeof brains); memset(navs, 0, sizeof navs); wall = 0; memset(inVehicle, 0, sizeof inVehicle); medkits = 1;
    g_enhanced.classSupport = 1; H->frame = 100000;
    for (int k = 0; k < 8; k++) {
        bodies[k][0] = k * 2.0f; bodies[k][1] = 0; bodies[k][2] = 0; bodies[k][3] = 0; bodies[k][7] = 100; spare[k] = 120;
        *(float **)(players[k] + OFF_PLAYER_SOLDIER) = bodies[k];
    }
    for (int k = 0; k < MAX_BOTS; k++) { brains[k].enabled = 1; g_spareMax[k] = 120; }
}
static float *hp(int k) { return &bodies[k][7]; }
int main(void)
{
    bm_defaults(&g_enhanced);
    assert(g_enhanced.classSupport == 1 && g_enhanced.supplyMeters == 60 && g_enhanced.sniperPairs == 1);
    assert(bm_set(&g_enhanced, "SupplyMeters", "300") == -1 && bm_set(&g_enhanced, "SupplyMeters", "100") == 1 && g_enhanced.supplyMeters == 100);
    assert(bm_set(&g_enhanced, "ClassSupport", "0") == 1 && !g_enhanced.classSupport && bm_set(&g_enhanced, "SniperPairs", "0") == 1 && !g_enhanced.sniperPairs);
    bm_defaults(&g_enhanced);
    H->botCount = 5;
    for (int k = 0; k < 5; k++) H->bots[k] = players[k];
    strcpy(players[0] + 64, "[BOT] Medic US"); strcpy(players[1] + 64, "[BOT] Support US"); strcpy(players[2] + 64, "[BOT] Engineer US");
    strcpy(players[3] + 64, "[BOT] Recon US"); strcpy(players[4] + 64, "[BOT] Assault RU"); strcpy(players[5] + 64, "Player1");
    for (int k = 0; k < 8; k++) arr[k] = players[k];
    void **a = arr; memcpy(pm + 0xac, &a, sizeof a); unsigned count = 6; memcpy(pm + 8, &count, sizeof count);
    // (bodies: k at x = 2k - all within 10 m of the medic at x = 0 except 5 at x = 10)
    reset(); *hp(2) = 50;
    class_support_tick(0, (char *)bodies[0]); assert(drops[0] == 1 && dropSlot == 4);
    H->frame += 10 * 30; class_support_tick(0, (char *)bodies[0]); assert(drops[0] == 1);   // one per 20 s
    H->frame += 11 * 30; *hp(2) = 100; class_support_tick(0, (char *)bodies[0]); assert(drops[0] == 1);   // nobody hurt
    reset(); *hp(4) = 30; class_support_tick(0, (char *)bodies[0]); assert(!drops[0]);   // an enemy
    reset(); bodies[2][0] = 15; *hp(2) = 30; class_support_tick(0, (char *)bodies[0]); assert(!drops[0]);   // too far
    reset(); wall = 1; bodies[2][0] = 4; *hp(2) = 30; class_support_tick(0, (char *)bodies[0]); assert(!drops[0]);   // behind a wall
    reset(); *hp(0) = 60; class_support_tick(0, (char *)bodies[0]); assert(drops[0] == 1);   // the medic itself
    reset(); *hp(2) = 30; g_hitAt[0] = H->frame - 30; class_support_tick(0, (char *)bodies[0]); assert(!drops[0]);   // under fire
    reset(); *hp(2) = 30; brains[0].task = 8; class_support_tick(0, (char *)bodies[0]); assert(!drops[0]);   // fighting
    reset(); *hp(2) = 30; medkits = 0; class_support_tick(0, (char *)bodies[0]); assert(!drops[0]);   // none left
    reset(); *hp(2) = 30; g_enhanced.classSupport = 0; class_support_tick(0, (char *)bodies[0]); assert(!drops[0]);
    reset(); *hp(2) = 30; class_support_tick(2, (char *)bodies[2]); assert(!drops[2]);   // an engineer has no medkit
    puts("PASS: a medic drops a medkit for itself or a hurt teammate within 10 m in sight - one per 20 s, not for enemies, not behind walls, not under fire or in a fight, not with none left, not with ClassSupport=0");
    reset(); spare[3] = 30; class_support_tick(1, (char *)bodies[1]); assert(drops[1] == 1 && dropSlot == 5);   // a bot: under a third of its load
    reset(); spare[3] = 60; class_support_tick(1, (char *)bodies[1]); assert(!drops[1]);
    reset(); bodies[5][0] = 6; spare[5] = 0; class_support_tick(1, (char *)bodies[1]); assert(drops[1] == 1);   // a player out of magazines
    reset(); spare[1] = 10; class_support_tick(1, (char *)bodies[1]); assert(drops[1] == 1);   // itself
    puts("PASS: support drops an ammo box for itself or a teammate within 10 m low on magazines (a bot under a third of its load, a player with none spare)");
    assert(own_supply_slot(0, 1) == 4 && own_supply_slot(0, 0) < 0 && own_supply_slot(1, 0) == 5 && own_supply_slot(2, 1) < 0);
    // a bot that called waits for its supplier, facing it; done, gone or 15 s: it carries on
    reset(); supply_wait_start(players[2], players[0]); brains[0].task = TASK_SUPPLY; brains[0].serviceTarget = players[2];
    navs[2].mode = 3; H->botMove[2][0] = 1;
    assert(supply_wait_tick(2, (char *)bodies[2]) && navs[2].mode == 1 && !H->botMove[2][0]);
    brains[0].task = TASK_NONE; assert(!supply_wait_tick(2, (char *)bodies[2]) && !g_supplyWaitFor[2]);
    reset(); supply_wait_start(players[2], players[0]); brains[0].task = TASK_SUPPLY; brains[0].serviceTarget = players[2];
    H->frame += 16 * 30; assert(!supply_wait_tick(2, (char *)bodies[2]));
    reset(); supply_wait_start(players[2], players[0]); brains[0].task = TASK_SUPPLY; brains[0].serviceTarget = players[2];
    g_hitAt[2] = H->frame; assert(!supply_wait_tick(2, (char *)bodies[2]));   // shot at: not standing about
    reset(); supply_wait_start(players[5], players[0]); assert(!g_supplyWaitFor[5 < MAX_BOTS ? 5 : 0]);   // a player is never held
    puts("PASS: a bot that called for a medkit / ammo stops and faces its supplier until it is served, the supplier gives up, 15 s pass or it is shot at");
    return 0;
}
'''

REPAIR = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define MAX_BOTS 4
#define TASK_REPAIR 4
#define OFF_VEHICLE_HEALTH 0
typedef struct { int task; char *serviceTarget; } BotBrain;
typedef struct { float maxSeen, speed; } VehTrack;
typedef struct { int phase, manual; char *veh; unsigned repairHoldAt; } DriveState;
static struct { unsigned frame; int botCount; } host, *H = &host;
static BotBrain brains[MAX_BOTS]; static float vehHealth = 400; static VehTrack track = {1000, 0}; static float far = 10; static int notes;
static BotBrain *bot_brain(int i) { return &brains[i]; }
static VehTrack *vehicle_sample(char *v) { (void)v; return &track; }
static int vehicle_is_boat(char *v) { (void)v; return 0; }
static float bot_distance_to(int i, const float *p) { (void)i; (void)p; return far; }
static void diagnostic_note(int i, const char *w, char *v) { (void)i; (void)w; (void)v; notes++; }
'''
REPAIR_MAIN = r'''
int main(void)
{
    char *veh = (char *)&vehHealth; DriveState d = {3, 0, veh, 0}; float vp[3] = {0, 0, 0};
    H->botCount = 3; H->frame = 1000;
    assert(!drive_repair_hold(0, &d, vp));                                   // nobody coming
    brains[2].task = TASK_REPAIR; brains[2].serviceTarget = veh;
    assert(drive_repair_hold(0, &d, vp) && notes == 1);                     // an engineer on its way: hold
    H->frame += 20 * 30; assert(drive_repair_hold(0, &d, vp));
    H->frame += 6 * 30; assert(!drive_repair_hold(0, &d, vp));             // 25 s at most
    d.repairHoldAt = 0; far = 60; assert(!drive_repair_hold(0, &d, vp));   // too far off to wait for
    far = 10; vehHealth = 990; assert(!drive_repair_hold(0, &d, vp));      // hardly damaged
    vehHealth = 400; d.manual = 1; assert(!drive_repair_hold(0, &d, vp));
    puts("PASS: a damaged bot-driven vehicle holds still for up to 25 s for a friendly engineer coming within 40 m to repair it (V10: 9 of 17 repairs ended 'vehicle drove off')");
    return 0;
}
'''

DEFAULTS_C = r'''
#include <stdio.h>
#include "enhanced.h"
int main(void) { BMConfig c; bm_defaults(&c);
%s
return 0; }
'''


def settings_schema():
    text = SC.read_text()
    rows = re.findall(r"New-BmSetting '(\w+)' '(\w+)' '(\w+)' ([\d.]+) ([\d.]+) '?([\w.]+)'?", text)
    assert len(rows) >= 45, len(rows)
    return [(k, kind, float(lo), float(hi), dflt) for k, _g, kind, lo, hi, dflt in rows]


def core_keys():
    text = (SRC / 'enhanced.c').read_text()
    keys = {}
    for kind, name, field, lo, hi in re.findall(r'(BOOL|FLOAT|INT)_KEY\("(\w+)",\s*([\w.]+)(?:,\s*([-\d.f]+),\s*([-\d.f]+))?\)', text):
        keys[name] = (kind, field, float(lo.rstrip('f')) if lo else 0.0, float(hi.rstrip('f')) if hi else 1.0)
    assert 'equal_ci(key, "RandomDifficulty")' in text
    keys['RandomDifficulty'] = ('BOOL', 'randomDifficulty', 0.0, 1.0)   # (parsed by name in bm_set, 0 / 1)
    return keys


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    san = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g'] if a.sanitize else ['-O1']
    env = {'ASAN_OPTIONS': 'detect_leaks=0'}
    core = (SRC / 'core.c').read_text(encoding='utf-8-sig')
    inc = (SRC / 'sniper_native.inc').read_text(encoding='utf-8-sig')
    spotted = re.search(r'^#define RECON_SPOTTED_RANGE[^\n]+', core, re.M)[0]
    with tempfile.TemporaryDirectory(prefix='bf3-altbotmod-v11-') as tmp:
        tmp = Path(tmp)
        def run(name, body, extra, args=()):
            c = tmp / (name + '.c'); c.write_text(body); exe = tmp / name
            subprocess.run(['gcc', *san, '-std=gnu11', '-Wall', '-Wextra', '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-unused-variable',
                            '-I', str(SRC), str(c), *[str(SRC / x) for x in extra], '-lm', '-o', str(exe)], check=True)
            return subprocess.run([str(exe), *args], check=True, env=env, capture_output=True, text=True).stdout
        print(run('pairs', v10.PRE + spotted + '\n' + inc + PAIRS, ['enhanced.c', 'air.c']), end='')
        # class jobs: the real functions from core.c
        start = core.index('#define CLASS_DROP_RADIUS'); end = core.index('static void self_requests(int i, char *soldier)\n{', start)
        body = CLASS_PRE + core[start:end] + CLASS_MAIN
        print(run('classes', body, ['enhanced.c', 'air.c']), end='')
        print(run('repair', REPAIR + function(core, 'drive_repair_hold') + REPAIR_MAIN, []), end='')
        # wiring: who answers a call, the watchdog, revives
        orx = function(core, 'order_reaction')
        assert 'bot_errand_free(i)' in orx and 'd > reach && i != ti' in orx and 'if (fallback < 0 || botCall) return;' in orx and 'supply_wait_start(sender' in orx
        assert '!bot_on_foot(i)) continue;   // (V11: crews keep driving)' in orx and 'g_enhanced.supplyMeters * (botCall ? 1.0f : 3.0f)' in orx
        cb = function(core, 'closest_bot'); assert '!bot_on_foot(i)' in cb
        act = (SRC / 'activity_native.inc').read_text(); cj = function(core, 'class_job_working')
        assert 'if(class_job_working(i,p)){w->progressAt=H->frame;return;}' in act
        assert "b->task == TASK_REVIVE || (b->task == TASK_REPAIR && b->waiting) || (b->task == TASK_SUPPLY && b->step > 0)" in cj and 'TASK_ROCKET && b->serviceTarget && vehicle_alive' in cj
        assert 'REVIVE_RANGE           45.0f' in core and 'range=player_vehicle(enemy)?120:45;' in core
        assert 'else if (drive_repair_hold(i,d,vp))' in core and 'b->task == TASK_NONE && supply_wait_tick(i, soldier)' in core
        assert 'FORGET(g_classDropAt);FORGET(g_supplyWaitFor);FORGET(g_supplyWaitUntil);' in core
        print('PASS: wired in - a call is answered only by a free bot on foot within SupplyMeters (3x for a player; V10: anyone, from vehicles, revives and fights), bots asking each other get no "Negative" chatter, "Need backup" / "Follow me" leave crews in their vehicles, the watchdog leaves a revive / repair / drop / rocket aim / supply wait be, revives reach 45 m with threats checked within 45 m')
        # the settings window offers the core's own keys, kinds, ranges and defaults
        schema = settings_schema(); keys = core_keys()
        prints = []
        for k, kind, lo, hi, dflt in schema:
            if k == 'Difficulty':
                assert kind == 'level' and dflt == 'elite'; continue
            assert k in keys, k
            ck, field, clo, chi = keys[k]
            want = {'bool': 'BOOL', 'int': 'INT', 'aggr': 'INT', 'float': 'FLOAT'}[kind]
            assert ck == want, (k, ck, kind)
            assert abs(clo - lo) < 1e-6 and abs(chi - hi) < 1e-6, (k, clo, chi, lo, hi)
            prints.append('printf("%s=%%g\\n",(double)c.%s);' % (k, field))
        out = run('defaults', DEFAULTS_C % '\n'.join(prints), ['enhanced.c', 'air.c'])
        got = dict(l.split('=') for l in out.split())
        for k, kind, lo, hi, dflt in schema:
            if k == 'Difficulty': continue
            assert abs(float(got[k]) - float(dflt)) < 1e-4, (k, got[k], dflt)
        print('PASS: Server Control settings window - all %d settings use the core\'s keys, on/off / whole-number / decimal kinds, ranges and built-in defaults' % len(schema))


if __name__ == '__main__':
    main()
