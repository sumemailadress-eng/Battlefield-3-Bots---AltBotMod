#!/usr/bin/env python3
"""AltBotMod V22 - Sharp Shooters: offline checks.

1. v22_native_tests.c: the bend speed from the real curve (a hand-recorded straight is driven flat out, a corner slow) and
   aircraft heat seekers held selected through the lock.
2. Source checks for what the live logs of 2026-10-17 showed:
   - TDM respawns (Epicenter): no 3-minute spawn rests outside Rush, a refused team-play spawn rests 3 s;
   - Domination (Office): a team with an HQ but no deployment beside it tries the level's team-less entities;
   - Fun-Bots routes first: trips from 15 m (V21.1: 40), a stalled route is taken again from another waypoint twice
     before the grid gets a go;
   - lock-on launchers count a launch only when the magazine drops, and keep at it;
   - vehicle gunners: Fun-Bots' weapon slot per seat and target, lead relative to their own platform, target turn rate,
     steadier fire; helicopters fire within ~6 m, pilots lead relative to their own speed;
   - stationary weapons: manned for a target in reach, left after 15 s without a shot;
   - boats take recorded river lanes; elite sharper; the version.
(Lock-on harness: altbotmod_v16_checks.py; the route repeat: caspian_v2_regressions.py; Domination HQ: hq_navigation_checks.py.)
"""
from pathlib import Path
import argparse, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
VAL = ROOT / 'validation'

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    flags = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'] if a.sanitize else ['-O1']
    with tempfile.TemporaryDirectory(prefix='bf3-v22-') as tmp:
        exe = str(Path(tmp) / 'v22')
        subprocess.run(['gcc', '-std=gnu11', *flags, '-Wall', '-Wno-unused-function', '-I', str(SRC), str(VAL / 'v22_native_tests.c'),
                        str(SRC / 'enhanced.c'), str(SRC / 'modes.c'), '-lm', '-o', exe], check=True)
        out = subprocess.run([exe], check=True, stdout=subprocess.PIPE, text=True).stdout
        print(out, end=''); assert out.count('PASS:') == 2
    core = (SRC / 'core.c').read_text(); modes = (SRC / 'modes_native.inc').read_text(); trace = (SRC / 'trace_native.inc').read_text()
    air = (SRC / 'air.c').read_text(); airn = (SRC / 'air_native.inc').read_text(); emp = (SRC / 'emplacement_native.inc').read_text()
    en = (SRC / 'enhanced.c').read_text(); eh = (SRC / 'enhanced.h').read_text()
    # respawns
    assert 'if(g_mode!=BM_MODE_RUSH || !g_modeProfile || !pl)return;' in modes
    assert '(g_mode!=BM_MODE_RUSH?3:spawn_team(ent)?10:60)*30' in modes
    assert 'strstr(g_modeName,"Conquest"))continue;' in core and 'tries the level\'s %d team-less / own entities' in core
    print('PASS: V22 respawns - the no-shooter death rest is Rush only (Epicenter TDM: two entities, one rested 3 min, team 1 never spawned); '
          'a refused team-play spawn rests 3 s; outside Conquest a team with an HQ but no matched deployment tries the team-less entities (Office Domination)')
    # routes first
    assert '#define TRACE_MIN_TRIP        8.0f' in trace and 'if (g_traceRepeat[i] >= 2) { g_traceFallbacks++;' in trace and 'FORGET(g_traceRepeat)' in core
    assert 'static int boat_lane_to(int f, const float *from)' in trace and '!boat_lane_to(f,from)' in core
    print('PASS: V22 Fun-Bots routes first - from 15 m, a stalled route is taken again from the next way onto the network twice before the grid; boats use recorded river lanes to far flags')
    # weapons
    assert 'int byAmmo = loaded + spare < g_lockAmmo[i]' in core and 'no %s left the tube in 7 s' in core and 'FORGET(g_lockCheckAt)' in core   # (V23: three ways)
    assert 'int armSecondary;' in (SRC / 'air.h').read_text() and 'out->armSecondary=s->secondaryWait<=0 && s->lockTime>0;' in air
    assert 'int want=(o->armSecondary || as->secondaryAt) ? 2 : 1;' in airn
    assert 'relativeVelocity[k]=o->targetVelocity[k]-' in air and 'clamp(atanf((o->targetAir ? 12.0f : 6.0f)/fmaxf(aimRange,1)),o->targetAir ? 0.035f : 0.025f,0.12f)' in air
    assert 'rel[k] = g_gunAirMotion[i].velocity[k] - own[k];' in core and 'aim[0] = lp[0] - own[0] * t;' in core and 'g_gunAccel[i][k]' in core
    assert 'driverWeapon && vprofile->kind == BM_TANK && !isVeh && td < 150.0f) slot = 2;' in core and 'airframe->kind == BM_AIR_ATTACK && seat == 1 && isVeh && !targetAir' in core
    assert '#define GUN_BURST_ON           54' in core and '#define GUN_BURST_OFF          6' in core and 'FORGET(g_gunOwnMotion)' in core
    print('PASS: V22 weapons - lock-on launches confirmed by the magazine; aircraft missiles selected through the lock; gunners lead relative to their own '
          'platform and by the target\'s turn, use the coax / guided missile as Fun-Bots does, fire 1.8 s bursts; helicopters fire within ~6 m')
    # stationaries, difficulty, version
    assert '#define EMPLACEMENT_IDLE    (15 * 30)' in emp and 'dist2(p, at) < 850.0f * 850.0f' in emp and 'H->frame - g_botFiredAt[i] < 3' in emp
    assert '{0.05f, 26,  2, 0.050f, 0.45f, 0.015f, 6.0f, 380, 300, 350' in en
    assert '#define BM_ENHANCED_VERSION "AltBotMod V34 - Squad Up"' in eh and 'vt = bm_route_bend_limit(pb, w, pf, isBoat, bm_armored(profile));' in core
    print('PASS: V22 stationary weapons manned for a target in reach and left after 15 s without a shot; elite sharper; bends by radius; version V22')

if __name__ == '__main__':
    main()
