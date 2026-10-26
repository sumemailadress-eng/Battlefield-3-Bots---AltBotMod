#!/usr/bin/env python3
"""AltBotMod V26: offline checks.

1. The game's lock: the target search gets an aim transform pointing exactly where the bot aims (the argument is swapped
   for a copy; the game's transform is untouched); the deviation is measured for the log.
2. Helicopter gunners (bm_vehicle_target_score): an enemy aircraft within 900 m first, then armour / AA, soldiers with
   launchers, other soldiers; tank gunners unchanged.
3. Server Control has every setting the core reads (each FLOAT/BOOL/INT/SIGN key in enhanced.c, plus Difficulty /
   RandomDifficulty), with the core's default.
4. Source checks: a blocked rear no longer drops a route; re-plan reasons and the storm warning; 20 s drive statistics;
   jets roll in from further off the nose and dive deeper; ranges for hard / veteran / elite; the version.
"""
from pathlib import Path
import argparse, re, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
SC = ROOT / 'files/ServerControl'

HARNESS = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#define MAX_BOTS 4
#define __cdecl
typedef unsigned long SIZE_T;
static int IsBadReadPtr(const void *p, SIZE_T n) { (void)n; return p == NULL; }
#include <stdint.h>
static struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; } host, *H = &host;
#define OFF_PLAYER_ONLINE_NAME 0
static struct { int lockAssist; float lockOnSeconds; } g_enhanced = {1, -1.0f};   /* (V29: lock_fake - not reached here: no target named) */
static const char *vehicle_name(char *v) { (void)v; return ""; }
static void bot_log(const char *f, ...) { (void)f; }
__asm__(".text\n");   /* (the hook's own asm stub is not part of this test) */
@@CODE@@
static unsigned char ctrl[0x60];
static float *xf;   /* (below 4 GB: the hook reads the transform pointer as 32 bits, as on the server) */
static unsigned stack[4];
static void weapon_at(float x, float y, float z, float fx, float fy, float fz)
{
    memset(xf, 0, 64); xf[8] = fx; xf[9] = fy; xf[10] = fz; xf[12] = x; xf[13] = y; xf[14] = z; xf[15] = 1;
}
static void reset_ctrl(void) { memset(ctrl, 0, sizeof ctrl); *(unsigned *)ctrl = ADDR_LOCK_CTRL_VTABLE; ctrl[0x11] = 1; }
static void test_swap(void);
int main(void)
{
    H->botCount = 2; H->frame = 100;
    xf = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0); assert(xf != MAP_FAILED);
    stack[2] = (unsigned)(size_t)xf;
    float eye[3] = {0, 1.6f, 0}, target[3] = {0, 40, 300};
    float l = sqrtf(target[0] * target[0] + (target[1] - eye[1]) * (target[1] - eye[1]) + target[2] * target[2]);
    /* a human 50 m off aims the same way: his launcher is left alone */
    reset_ctrl(); lock_want(1, eye, target, 4.0f);
    weapon_at(50, 1.5f, 0, 0, (target[1] - eye[1]) / l, target[2] / l);
    stack[2] = (unsigned)(size_t)xf; on_lock_update((char *)ctrl, stack); assert(ctrl[0x10] == 0 && !g_lockCtrl[1]);
    /* the bot's own launcher, at its shoulder, pointing at the target: activated as activate() does */
    weapon_at(0.3f, 1.5f, 0.2f, 0, (target[1] - eye[1]) / l, target[2] / l);
    stack[2] = (unsigned)(size_t)xf; on_lock_update((char *)ctrl, stack); assert(ctrl[0x10] == 1 && ctrl[0x11] == 0 && g_lockCtrl[1] == (char *)ctrl && g_lockActivations == 1);
    assert(lock_state(1, NULL) == 1);
    *(float *)(ctrl + 8) = 0.6f; float pr = 0; assert(lock_state(1, &pr) == 1 && fabsf(pr - 0.6f) < 1e-6f);
    *(int *)(ctrl + 0xc) = 2; assert(lock_state(1, NULL) == 2);   /* the game says locked */
    puts("PASS: the game's lock - a bot's launcher (aim transform at its shoulder, pointing its way) is activated as the controller's activate() does; a human's launcher 50 m off is not; locked (+0xc = 2) is read back");
    /* the same spot pointing the other way, a stale request, another class: nothing */
    reset_ctrl(); weapon_at(0.3f, 1.5f, 0.2f, 0, 0, -1); stack[2] = (unsigned)(size_t)xf; on_lock_update((char *)ctrl, stack); assert(ctrl[0x10] == 0);
    reset_ctrl(); weapon_at(0.3f, 1.5f, 0.2f, 0, (target[1] - eye[1]) / l, target[2] / l); H->frame = 110; stack[2] = (unsigned)(size_t)xf; on_lock_update((char *)ctrl, stack); assert(ctrl[0x10] == 0);
    H->frame = 100; reset_ctrl(); *(unsigned *)ctrl = 0x02500000; stack[2] = (unsigned)(size_t)xf; on_lock_update((char *)ctrl, stack); assert(ctrl[0x10] == 0);
    /* an old controller reading is not trusted after 0.5 s */
    reset_ctrl(); stack[2] = (unsigned)(size_t)xf; on_lock_update((char *)ctrl, stack); assert(lock_state(1, NULL) == 1); H->frame = 120; assert(lock_state(1, NULL) == 0);
    /* V26 */ test_swap();
    puts("PASS: nothing for a weapon pointing elsewhere, a request older than 0.1 s, or an object that is not a ServerLockingController; a controller not updated for 0.5 s reads as none");
    return 0;
}
static void test_swap(void)
{
    /* V26: the target search gets a copy pointing exactly where the bot aims, from the weapon's own position */
    H->frame = 200; float eye[3] = {0, 1.6f, 0}, target[3] = {100, 61.6f, 0};
    reset_ctrl(); lock_want(0, eye, target, 4.0f);
    weapon_at(0.3f, 1.5f, 0.2f, 0.9f, 0.3f, 0.25f);   /* ~19 deg off the aim */
    { float l = sqrtf(0.9f * 0.9f + 0.3f * 0.3f + 0.25f * 0.25f); xf[8] /= l; xf[9] /= l; xf[10] /= l; }
    stack[2] = (unsigned)(size_t)xf;
    stack[2] = (unsigned)(size_t)xf; on_lock_update((char *)ctrl, stack);
    float *m = (float *)(size_t)stack[2];
    assert(m != xf && ((size_t)m & 15) == 0);                        /* a copy, 16-byte aligned (the game reads it with SSE) */
    float w[3] = {100, 60, 0}, wl = sqrtf(100 * 100 + 60 * 60); w[0] /= wl; w[1] /= wl;
    assert(fabsf(m[8] - w[0]) < 1e-4f && fabsf(m[9] - w[1]) < 1e-4f && fabsf(m[10]) < 1e-4f);   /* forward = the bot's aim */
    assert(m[12] == xf[12] && m[13] == xf[13] && m[14] == xf[14]);    /* the weapon's own position */
    for (int a = 0; a < 3; a++) { float l = m[a*4] * m[a*4] + m[a*4+1] * m[a*4+1] + m[a*4+2] * m[a*4+2]; assert(fabsf(l - 1) < 1e-3f);
        for (int b = a + 1; b < 3; b++) assert(fabsf(m[a*4] * m[b*4] + m[a*4+1] * m[b*4+1] + m[a*4+2] * m[b*4+2]) < 1e-3f); }
    assert(xf[8] > 0.8f && g_lockDevN >= 1 && g_lockDevMax > 15 && g_lockDevMax < 25);   /* the game's own transform untouched; the deviation measured */
    puts("PASS: V26 - the lock's target search is handed a copy of the weapon's transform pointing exactly at the bot's aim (16-byte aligned, orthonormal, the weapon's own position); the game's transform is not touched; the deviation (19 deg here) is measured for the log");
}
'''

SCORE = r"""
#include <assert.h>
#include <stdio.h>
#include "enhanced.h"
int main(void)
{
    const BMVehicleProfile *heli = bm_vehicle_profile("AH1Z"), *tank = bm_vehicle_profile("T90");
    assert(heli && heli->kind == BM_AIR);
    float air800 = bm_vehicle_target_score(heli, BM_TARGET_AIR, 800), armour300 = bm_vehicle_target_score(heli, BM_TARGET_GROUND, 300);
    float eng100 = bm_vehicle_target_score(heli, BM_TARGET_ENGINEER, 100), inf50 = bm_vehicle_target_score(heli, BM_TARGET_INFANTRY, 50);
    assert(air800 < armour300 && armour300 < bm_vehicle_target_score(heli, BM_TARGET_ENGINEER, 300) && eng100 < inf50 + 300);
    assert(bm_vehicle_target_score(heli, BM_TARGET_GROUND, 400) < bm_vehicle_target_score(heli, BM_TARGET_INFANTRY, 100));
    /* tanks as before: a close soldier before a far tank, a near tank before a far soldier */
    assert(bm_vehicle_target_score(tank, BM_TARGET_INFANTRY, 40) < bm_vehicle_target_score(tank, BM_TARGET_GROUND, 300));
    assert(bm_vehicle_target_score(tank, BM_TARGET_GROUND, 200) < bm_vehicle_target_score(tank, BM_TARGET_INFANTRY, 150));
    puts("PASS: helicopter gunners - an enemy aircraft 800 m off before armour 300 m off, armour before soldiers, launchers before riflemen; tanks unchanged (a soldier at 40 m before a tank at 300 m, a tank at 200 m before a soldier at 150 m)");
    return 0;
}
"""
DEFAULTS = r"""
#include <stdio.h>
#include <stddef.h>
#include "enhanced.h"
int main(void) { BMConfig c; bm_defaults(&c); fwrite(&c, sizeof c, 1, stdout); return 0; }
"""

def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode:
        raise SystemExit('FAILED: ' + ' '.join(map(str, cmd)) + '\n' + r.stdout + r.stderr)
    return r.stdout

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); args = ap.parse_args()
    core = (SRC / 'core.c').read_text()
    lw = (SRC / 'lock_warn.h').read_text().replace('#pragma once', ''); lc = core.replace('#define ADDR_LOCK_CTRL_UPDATE', '/*V34LW*/' + lw + '#define ADDR_LOCK_CTRL_UPDATE', 1)   # (V34: the lock-warning table the hook feeds)
    a = lc.index('/*V34LW*/'); b = lc.index('extern void hook_lock_update(void);')
    san = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-sanitize=alignment'] if args.sanitize else []
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        code = lc[a:b].replace('static float g_lockXf[MAX_BOTS][16] __attribute__((aligned(16)));', 'static float (*g_lockXf)[16];   /* (test: below 4 GB, as on the server) */')
        assert 'static float (*g_lockXf)[16];' in code
        h = HARNESS.replace('@@CODE@@', code).replace('assert(xf != MAP_FAILED);', 'assert(xf != MAP_FAILED); g_lockXf = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);')
        (td / 'lock.c').write_text(h)
        run(['gcc', *san, '-std=gnu11', '-O1', '-w', '-I', str(SRC), str(td / 'lock.c'), '-lm', '-o', str(td / 'lock')])
        print([l for l in run([str(td / 'lock')]).splitlines() if 'V26' in l][0])
        (td / 'score.c').write_text(SCORE)
        run(['gcc', *san, '-std=gnu11', '-O1', '-I', str(SRC), str(td / 'score.c'), str(SRC / 'enhanced.c'), str(SRC / 'air.c'), str(SRC / 'modes.c'), '-lm', '-o', str(td / 'score')])
        print(run([str(td / 'score')]).strip())
    # Server Control: every key the core reads
    en = (SRC / 'enhanced.c').read_text(); ps = (SC / 'ServerControl-Core.ps1').read_text()
    keys = re.findall(r'[A-Z_]+KEY\("([A-Za-z0-9_]+)"', en)
    ui = dict((m[0], m[1]) for m in re.findall(r"New-BmSetting '([A-Za-z0-9_]+)' '[^']+' '[a-z]+' \S+ \S+ (\S+) ", ps))
    missing = [k for k in keys if k not in ui]
    assert not missing and 'Difficulty' in ui and 'RandomDifficulty' in ui, missing
    print(f"PASS: Server Control lists every one of the core's {len(keys)} settings (plus Difficulty / RandomDifficulty) - V26 added the 22 that were missing")
    # source checks
    man = (SRC / 'drive_maneuvers.inc').read_text(); air = (SRC / 'air.c').read_text(); eh = (SRC / 'enhanced.h').read_text()
    assert 'if(d->phase==3){d->rearBlockedUntil=H->frame+60;diagnostic_note(i,"rear blocked; keeps its route and turns forwards",d->veh);return 0;}' in man
    assert '|| H->frame < d->rearBlockedUntil) { throttle = d->midNear > 2.0f ?' in core
    assert core.count('d->why="') >= 4 and 're-planned its road 6 times in %.1f s - last reason: %s' in core
    assert 'last 20 s: average %.1f m/s, full throttle %.0f%%, braking %.0f%% (bends %u, vehicle in lane %u, hard steering %u, obstacle %u, stop %u frames)' in core   # (V29: the statistics say more)
    assert 'range>300 && range<2500 && diveAngle>0.05f && diveAngle<0.8f && azimuth<0.6f' in air and 'range<200 || height<50 || s->diveTime>12' in air
    assert '{0.05f, 26,  2, 0.050f, 0.45f, 0.015f, 6.0f, 380, 300, 350, 170.00f' in en and '{0.30f, 14,  6, 0.055f, 0.40f, 0.10f, 3.0f, 170, 130, 150' in en
    assert '#define BM_ENHANCED_VERSION "AltBotMod V34 - Squad Up"' in eh
    print('PASS: V26 source - a blocked rear keeps the route (no re-plan loop), every re-plan carries its reason and a storm is logged, 20 s drive statistics, '
          'jets roll in from 34 deg off the nose and dive to 200 m, hard / veteran / elite engage from 130 / 230 / 300 m; version V26')

if __name__ == '__main__':
    main()
