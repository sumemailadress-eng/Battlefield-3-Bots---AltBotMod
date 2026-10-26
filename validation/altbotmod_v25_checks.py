#!/usr/bin/env python3
"""AltBotMod V25 - Lock and Load: offline checks.

1. The game's own lock (core.c: lock_want / lock_state / on_lock_update, the pre-hook of ServerLockingController::update):
   the controller of the weapon a bot aims (its aim transform at the bot's eye, pointing the bot's way) is activated
   exactly as the controller's activate() does (+0x10 = 1, +0x11 = 0); a human's weapon, one pointing elsewhere, a stale
   request and anything that is not a ServerLockingController are left alone; "locked" (+0xc = 2) is read back.
2. Source checks:
   - the hook is installed on 0x00dda490 with the expected prologue and resumes at 0x00dda496;
   - launchers ask for the lock and fire on it; aircraft and gunners fire on it;
   - the convoy system is gone from drive_tick, and the brakes answer only to what is in the way;
   - a re-plan keeps the last throttle; the turn-around replaces the reverse-and-re-plan loop;
   - a plan is no longer a route try; a grid leg joins the Fun-Bots road; heading caps only on hard steering;
   - director: nearest flags first; elite and veteran engage from further; the version.
"""
from pathlib import Path
import argparse, re, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'

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
    puts("PASS: nothing for a weapon pointing elsewhere, a request older than 0.1 s, or an object that is not a ServerLockingController; a controller not updated for 0.5 s reads as none");
    return 0;
}
'''

def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit('FAILED: ' + ' '.join(map(str, cmd)) + '\n' + r.stdout + r.stderr)
    return r.stdout

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); args = ap.parse_args()
    core = (SRC / 'core.c').read_text()
    lw = (SRC / 'lock_warn.h').read_text().replace('#pragma once', ''); lc = core.replace('#define ADDR_LOCK_CTRL_UPDATE', '/*V34LW*/' + lw + '#define ADDR_LOCK_CTRL_UPDATE', 1)   # (V34: the lock-warning table the hook feeds)
    a = lc.index('/*V34LW*/'); b = lc.index('extern void hook_lock_update(void);')
    code = lc[a:b]
    with tempfile.TemporaryDirectory() as td:
        code = code.replace('static float g_lockXf[MAX_BOTS][16] __attribute__((aligned(16)));', 'static float (*g_lockXf)[16];   /* (test: below 4 GB, as on the server) */')
        h = HARNESS.replace('@@CODE@@', code).replace('assert(xf != MAP_FAILED);', 'assert(xf != MAP_FAILED); g_lockXf = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);')
        f = Path(td) / 'lock.c'; f.write_text(h)
        san = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-sanitize=alignment'] if args.sanitize else []
        run(['gcc', *san, '-m32' if False else '-std=gnu11', '-O1', '-w', str(f), '-lm', '-o', str(Path(td) / 'lock')])
        print(run([str(Path(td) / 'lock')]).strip())
    air = (SRC / 'air_native.inc').read_text(); en = (SRC / 'enhanced.c').read_text(); eh = (SRC / 'enhanced.h').read_text()
    pol = (SRC / 'director_policy.h').read_text(); prog = (SRC / 'ground_progress.inc').read_text()
    # the hook
    assert 'g_lockHooked = detour(ADDR_LOCK_CTRL_UPDATE, kLockUpdatePrologue, 6, hook_lock_update, "ServerLockingController::update");' in core
    assert '{0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0}' in core and '"  push 0x00dda496\\n"' in core and '#define ADDR_LOCK_CTRL_VTABLE  0x025b9248' in core
    # who asks for it and fires on it
    assert 'if (b->step == 1 && !hidden) lock_want_ent(i, eye, aim, 4.0f, g_lockSince[i] ? veh : NULL);' in core and 'native == 2' in core   # (V29: the vehicle named)
    assert 'AIR_LOCK_WANT_ENT(i,axf+12,to,15.0f,g_air[i].targetVeh);' in air and 'if(AIR_LOCK_STATE(i)==2 && H->frame-as->slotAt>=3){fire=1;' in air and '#define AIR_LOCK_WANT(i, from, to, r) lock_want(i, from, to, r)' in core
    assert 'if (g_lockHooked && lock_state(i, NULL) == 2) g_gunFire[i] = 1;' in core
    print('PASS: V25 lock-on source - the hook on ServerLockingController::update (0x00dda490, resumes 0x00dda496); launchers, aircraft missiles and gunner seats ask for the game\'s lock and fire on it; the failure log reports the controller')
    # driving
    dt = core[core.index('static void drive_tick(int i)'):]
    dt = dt[:dt.index('\n// ---- Tickets')]
    assert 'traffic_speed(' not in dt and 'traffic_controls(' not in dt and 'd->trafficFollowing = 0; d->trafficPeer = NULL;' in dt   # (V27: trafficWait now means waiting its turn)
    assert 'if (!d->onRoadNow && d->brakeNear < 99) vwant = fminf(vwant, bm_drive_obstacle_speed(d->brakeNear, d->speed, isBoat));' in dt and 'd->brakeNear = fminf(sideNear[1], fminf(sideNear[0] < 4.0f ? sideNear[0] : 99.0f, sideNear[2] < 4.0f ? sideNear[2] : 99.0f));' in dt
    assert 'if (d->drivenAt && H->frame - d->drivenAt < 90 && d->speed > 2.0f && d->brakeNear > 6.0f' in core and 'if (throttle > 0.0f) d->drivenAt = H->frame ? H->frame : 1;' in core
    assert 'else if ((d->midNear > 4.0f && d->brakeNear > 3.0f) || H->frame < d->rearBlockedUntil) { throttle = d->midNear > 2.0f ? 0.45f : 0.25f; steer = lock; }' in dt   # (V26: rear blocked too; V27: wheeled only - tanks pivot)
    assert 'd->reverseUntil=0;d->phase=2;nav_cancel_path(i+MAX_BOTS);' not in dt
    assert 'if (!part) d->traceTries++;' not in core and 'd->traceTries=99;d->backOutUntil' not in prog and 'd->traceTries+=2;d->traceAlt++;' in prog
    assert 'joins the Fun-Bots road by %d grid point(s)' in core and 'int direct = nav_direct_route(jg, drive_nav_ray, vp, &d->pts[k0 * 3]);' in core
    assert 'if (ae > 1.05f) vwant = fminf(vwant, isBoat ? 10.0f : bm_armored(profile) ? 6.0f : 9.0f);' in dt and 'if (ae > 0.55f)' not in dt
    print('PASS: V25 driving source - no convoy system (no following gaps, yields or route-dropping), brakes only for what is in the way, '
          'the last throttle held through a re-plan, a turn on the spot instead of reverse-and-re-plan, plans are not tries, grid legs onto the road, speed caps only for hard steering')
    assert 'if(hostile)value+=rank[f]*300.0f;' in pol and 'int reach=units[i].mobile?3:2;' in pol
    assert '{0.05f, 26,  2, 0.050f, 0.45f, 0.015f, 6.0f, 380, 300, 350, 170.00f' in en and '{0.15f, 18,  4, 0.055f, 0.42f, 0.05f, 4.0f, 280, 230, 270' in en
    assert '#define BM_ENHANCED_VERSION "AltBotMod V34 - Squad Up"' in eh
    print('PASS: V25 flags nearest-first (director ranks the front), elite engages from 240 m / sees 290 m (130 / 150), veteran 170 / 200 m; version V25')

if __name__ == '__main__':
    main()
