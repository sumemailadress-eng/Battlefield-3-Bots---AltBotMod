#!/usr/bin/env python3
"""AltBotMod V33 - Smooth Drive: offline checks.

1. Vehicle position (core.c vehicle_pos): the physics entity's transform (+0x30 -> +0x88 -> translation at +0x30) first.
   V31 driver traces showed the throttle held at 1.0 on every frame while the body position stood still and the seat
   position only synced every ~60 frames, so the drive code measured 0 m/s and then a jump (the "lets go of the gas"
   stop-go). The real vehicle_pos is compiled against fake memory: physics wins; body / seat stay the fallback for a
   missing, zero, non-finite or absurd transform; the drive speed from it (every 10 frames, as drive_tick) stays smooth.
2. AltBotMod Command (the web launcher) ships in ServerControl with its web files; the installer copies the web folders,
   checks the artwork, makes the desktop / Start menu shortcuts and removes them on uninstall (the running host is tested
   by servercontrol/command_tests.ps1, the installer by altbotmod_installer_tests.ps1).
3. Version.
"""
from pathlib import Path
import argparse, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
SC = ROOT / 'files/ServerControl'

def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True, env={'ASAN_OPTIONS': 'detect_leaks=0', 'PATH': '/usr/bin:/bin'})
    if r.returncode: raise SystemExit('FAILED: ' + ' '.join(map(str, cmd)) + '\n' + r.stdout + r.stderr)
    return r.stdout

HARNESS = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define OFF_VEHICLE_BODY           0x230
#define OFF_VEHBODY_POSITION       0x100
#define OFF_VEHICLE_TEAM           0xd8
static int IsBadReadPtr(const void *p, unsigned n) { (void)n; return p == NULL || (uintptr_t)p < 0x1000; }
static float *g_seat;
static float *vehicle_occupant_pos(char *veh) { (void)veh; return g_seat; }
/* pointers are stored in place (8 bytes here, 4 in the game: no field of the test overlaps either way) */
static char veh[0x400], phys[0x100], xfm[0x40], body[0x200];
static void put(char *at, void *p) { memcpy(at, &p, sizeof p); }
@@VEHICLE_POS@@
int main(void)
{
    float *bodyPos = (float *)(body + OFF_VEHBODY_POSITION), *t = (float *)(xfm + 0x30), seat[3];
    *(int *)(veh + OFF_VEHICLE_TEAM) = 2;
    put(veh + OFF_VEHICLE_BODY, body); put(veh + 0x30, phys); put(phys + 0x88, xfm);
    bodyPos[0] = 100; bodyPos[1] = 50; bodyPos[2] = 200;
    t[0] = 104; t[1] = 50; t[2] = 203;
    int team = 0; float *p = vehicle_pos(veh, &team);
    assert(team == 2 && p == t);                                   /* 1. the physics transform first */
    t[0] = 0; t[2] = 0; assert(vehicle_pos(veh, NULL) == bodyPos);  /* not placed yet: the body */
    t[0] = NAN; t[2] = 5; assert(vehicle_pos(veh, NULL) == bodyPos);/* garbage: the body */
    t[0] = 3e6f; assert(vehicle_pos(veh, NULL) == bodyPos);
    t[0] = 104; t[2] = 203; put(phys + 0x88, NULL); assert(vehicle_pos(veh, NULL) == bodyPos);   /* no transform */
    put(veh + 0x30, NULL); seat[0] = 140; seat[1] = 50; seat[2] = 200; g_seat = seat;
    assert(vehicle_pos(veh, NULL) == seat);                         /* the old rule: crew seat when the body is far off */
    put(veh + 0x30, phys); put(phys + 0x88, xfm);
    /* 2. a V31-style drive: the body never moves, the seat syncs every 60 frames, the physics moves 0.4 m a frame (12 m/s).
          drive_tick's speed (distance over 10 frames) must stay ~12 m/s every sample: no 0 m/s, no jump. */
    float sp[3] = {0}; unsigned spAt = 0; int samples = 0; float lo = 1e9f, hi = 0;
    for (unsigned f = 1; f <= 600; f++) {
        t[0] = 104 + 0.4f * f;
        if (f % 60 == 0) seat[0] = t[0];
        float *vp = vehicle_pos(veh, NULL);
        if (!spAt) { memcpy(sp, vp, 12); spAt = f; }
        else if (f - spAt >= 10) {
            float dx = vp[0] - sp[0], dy = vp[1] - sp[1], dz = vp[2] - sp[2], speed = sqrtf(dx * dx + dy * dy + dz * dz) / ((f - spAt) / 30.0f);
            if (speed < lo) lo = speed; if (speed > hi) hi = speed; samples++;
            memcpy(sp, vp, 12); spAt = f;
        }
    }
    printf("speed samples %d: %.2f .. %.2f m/s\n", samples, lo, hi);
    assert(samples > 50 && lo > 11.9f && hi < 12.1f);
    puts("vehicle_pos ok");
    return 0;
}
'''

def extract(core, name):
    a = core.index('static float *' + name + '(char *veh, int *team)\n{')
    b = core.index('\n}\n', a) + 3
    return core[a:b]

def check_vehicle_pos(san):
    core = (SRC / 'core.c').read_text()
    fn = extract(core, 'vehicle_pos')
    assert 'AltBotMod V33: the physics entity' in fn and fn.index('phys + 0x88') < fn.index('vehicle_occupant_pos(veh)')
    with tempfile.TemporaryDirectory(prefix='bf3-v33-') as tmp:
        c = Path(tmp) / 'vpos.c'; c.write_text(HARNESS.replace('@@VEHICLE_POS@@', fn))
        exe = Path(tmp) / 'vpos'
        flags = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all'] if san else []
        try:
            run(['gcc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wno-unused-function', *flags, str(c), '-lm', '-o', str(exe)])
        except SystemExit:
            if not san: raise
            run(['gcc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wno-unused-function', str(c), '-lm', '-o', str(exe)])
        out = run([str(exe)])
    print(out.strip())
    print('PASS: vehicles use the physics position (smooth speed, body / seat fallback)')

def check_command():
    for f in ['AltBotMod-Command.ps1', 'Command-Http.cs', 'Command-Desktop.bat', 'Command-Browser.bat', 'Command.bat', 'Command-Window.ps1',
              'Command-Join.ps1', 'Command-Learning.ps1', 'Command-Firewall.ps1', 'Command-Allow-Phone.bat', 'Command-Admin.bat', 'Command-Admin.ps1',
              'Command-NOTICES.txt', 'Command-README.txt', 'Command-Web/index.html', 'Command-Web/command.js', 'Command-Web/art/frontier.png', 'Command-Web/maps/XP5_004.jpg']:
        assert (SC / f).is_file(), f
    assert not (SC / 'Command-last-folder.txt').exists(), 'a user-specific folder file must not ship'
    host = (SC / 'AltBotMod-Command.ps1').read_text()
    assert "$script:CommandVersion='V33 / Command 1.5'" in host and "'V33 / Command 1.5'" in (SC / 'Command-Web/command.js').read_text()
    print('PASS: AltBotMod Command ships with Server Control')

def check_version():
    eh = (SRC / 'enhanced.h').read_text()
    assert '#define BM_ENHANCED_VERSION "AltBotMod V34 - Squad Up"' in eh
    assert 'New-BmBanner "AltBotMod V34"' in (SC / 'AltBotMod-ServerControl.ps1').read_text()
    print('PASS: version V34')

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    check_vehicle_pos(a.sanitize)
    check_command()
    check_version()
