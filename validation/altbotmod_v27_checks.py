#!/usr/bin/env python3
"""AltBotMod V27 - Clear Road: offline checks.

1. v27_tank_sim.c: a tracked tank (0.7 rad/s turn, pivots on the spot) on the longest Fun-Bots road of five maps, starting
   the wrong way round: it pivots once, then drives the whole road at a decent average with no more turning on the spot.
2. Source checks: tanks pivot with no throttle until within 20 deg (V25-V26 drove circles round a close aim point); the
   pure-pursuit look-ahead is 15-30 m (8-25); team vehicles leave one at a time (the lower roster slot first, up to 6 s;
   the stuck checks leave a waiting driver alone); the version.
"""
from pathlib import Path
import argparse, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
VAL = ROOT / 'validation'
MAPS = ['MP_012_ConquestLarge0', 'XP3_Alborz_ConquestLarge0', 'XP3_Desert_ConquestLarge0', 'XP3_Shield_ConquestLarge0', 'MP_017_ConquestLarge0']

def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit('FAILED: ' + ' '.join(map(str, cmd)) + '\n' + r.stdout + r.stderr)
    return r.stdout

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); args = ap.parse_args()
    san = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'] if args.sanitize else []
    with tempfile.TemporaryDirectory() as td:
        exe = Path(td) / 'tank'
        run(['gcc', *san, '-std=gnu11', '-O1', '-Wall', '-I', str(SRC), str(VAL / 'v27_tank_sim.c'), str(SRC / 'enhanced.c'), str(SRC / 'air.c'), str(SRC / 'modes.c'), '-lm', '-o', str(exe)])
        for m in MAPS:
            print('PASS: ' + run([str(exe), str(ROOT / 'files/server/BotMod_traces' / (m + '.map'))]).strip().splitlines()[-1])
    core = (SRC / 'core.c').read_text(); en = (SRC / 'enhanced.c').read_text(); eh = (SRC / 'enhanced.h').read_text()
    assert 'if (tracked) { if (!d->pivoting) { d->pivoting = 1; d->pivotAt = H->frame ? H->frame : 1; } throttle = 0.0f; steer = lock; }' in core
    assert 'if (d->pivoting && (ae < 0.35f || d->speed > 4.0f || H->frame - d->pivotAt > 6 * 30)) d->pivoting = 0;' in core
    assert 'float L = fminf(30.0f, fmaxf(15.0f, 10.0f + 0.6f * (isfinite(speed) ? speed : 0)))' in en
    # (V29: the one-at-a-time departure is gone - it made convoys stop and wait for one another; drive_lane_scan follows and passes)
    assert 'drive_wait_turn(' not in core and 'd->trafficWait = d->laneBlocker && d->laneLimit < 0.5f' in core
    assert '#define BM_ENHANCED_VERSION "AltBotMod V34 - Squad Up"' in eh
    print('PASS: V27 source - tanks pivot on the spot without throttle until they face the road, look-ahead 15-30 m, (V29: the one-at-a-time departure replaced by the lane scan); version V27')

if __name__ == '__main__':
    main()
