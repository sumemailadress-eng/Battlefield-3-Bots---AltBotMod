#!/usr/bin/env python3
"""AltBotMod V21 - Learning: offline checks.

1. learn_tests.c: the learning store (learn.c) - the file format both ways, merging stuck events, the judgement rules
   (avoided / kept open / jump), death heat and its fading, routes from players (same route = a use, bounded), an
   outside edit (Server Control deleting lines) merged with what the server learned since, forward compatibility
   (unknown lines kept), helicopter landing spots, routes split where bots never get through, the map picture line.
2. learn_native_tests.c: the game side (learn_native.inc) in a scripted world - a bot stuck then through with a jump,
   the spot kept open and its jump used, links closed for a spot nobody passes, drivers' stall spots off the driving
   map, more height where an aircraft was lost low, a player's walk recorded (stairs kept, a ladder splits it), the
   same walk again = a use, a drive, a player's helicopter landing as a landing spot for transports, save / outside edit
   / merge, one file per map and mode, the map line, Learning=0 reads and writes nothing, a foreign file left alone.
3. Source checks: every hook is wired (foot stuck, jumps, deaths, ground / boat stalls, aircraft losses, landing
   spots, the route loader, killing-ground costs), build.bat links learn.c, the version.
(Server Control's Learning page: validation/servercontrol/learning_tests.ps1, run by run_release_checks with pwsh.)
"""
from pathlib import Path
import argparse, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
VAL = ROOT / 'validation'

def build_run(tmp, name, sources, flags, std='c11'):
    exe = str(Path(tmp) / name)
    subprocess.run(['gcc', f'-std={std}', *flags, '-Wall', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-format-truncation',
                    '-Wno-unused-result', '-I', str(SRC), *[str(s) for s in sources], '-lm', '-o', exe], check=True)
    out = subprocess.run([exe], check=True, stdout=subprocess.PIPE, text=True).stdout
    print(out, end='')
    return out

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    flags = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'] if a.sanitize else ['-O1']
    with tempfile.TemporaryDirectory(prefix='bf3-v21-') as tmp:
        out = build_run(tmp, 'store', [VAL / 'learn_tests.c', SRC / 'learn.c'], flags)
        assert 'PASS:' in out and 'landing spots' in out
        out = build_run(tmp, 'native', [VAL / 'learn_native_tests.c', SRC / 'learn.c', SRC / 'enhanced.c', SRC / 'air.c', SRC / 'modes.c', SRC / 'trace_nav.c'], flags, 'gnu11')
        assert 'PASS:' in out and 'helicopter landing spots' in out
    core = (SRC / 'core.c').read_text(); nat = (SRC / 'learn_native.inc').read_text(); air = (SRC / 'air_native.inc').read_text()
    land = (SRC / 'transport_landings_native.inc').read_text(); trace = (SRC / 'trace_native.inc').read_text(); gp = (SRC / 'ground_progress.inc').read_text()
    assert '#include "learn_native.inc"' in core and 'learn_tick();' in core
    assert 'bm_learn(i, BM_LEARN_STUCK_FOOT, pos, heading)' in core and 'BM_ASK_FOOT_KEEP_OPEN' in core and 'bm_learn(i, BM_LEARN_JUMP' in core
    assert 'bm_learn(-1, BM_LEARN_DEATH' in core
    assert gp.count('bm_learn(') >= 2 and 'BM_LEARN_STUCK_BOAT' in gp
    assert 'BM_LEARN_AIR_LOSS' in air and 'extraClearance' in air
    assert 'bm_learn_pad_near(center' in land and 'BM_LEARN_PAD_LANDED' in land and 'BM_LEARN_PAD_BLOCKED' in land
    assert 'bm_learn_routes(want, 0' in trace and 'bm_learn_danger' in trace and 'tn_load_layers' in trace
    assert 'learn_route_costs();' in nat and 'tn_penalize_near' in nat
    assert 'learn_heli_tick' in nat and 'steep' in nat and 'map\\tmap_%s.bmp' in nat
    assert 'src\\learn.c' in (ROOT / 'files/BotMod/build.bat').read_text()
    assert '#define BM_ENHANCED_VERSION "AltBotMod V34 - Squad Up"' in (SRC / 'enhanced.h').read_text()
    ec = (SRC / 'enhanced.c').read_text(); assert 'BOOL_KEY("Learning", learning)' in ec and 'BOOL_KEY("LearnFromPlayers", learnFromPlayers)' in ec
    print('PASS: V21 sources: every learning hook wired (foot, jumps, deaths, ground/boat, aircraft, landing spots, routes, killing grounds), build.bat, settings, version')

if __name__ == '__main__':
    main()
