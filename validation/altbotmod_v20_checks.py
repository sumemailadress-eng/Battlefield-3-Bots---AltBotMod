#!/usr/bin/env python3
"""AltBotMod V20 - Air War: offline checks.

1. air_combat_sim.c: the real bm_air_step flown in closed loop on a rigid-body kinematic model - 300 jet-vs-jet
   dogfights from random starts (a third dead head-on), strike runs on static and moving ground targets, friendly
   head-on traffic, gunships on ground targets, jets on helicopters, and five model variants (slow/quick controls,
   gravity on the flight path). Gates: no ram (< 25 m), no ground crash, gun time for both sides, repeated strike
   dives with rocket salvos, gunships firing. The same model with the V19 controller rams in 294 of 300 dogfights.
2. Source checks: the per-frame traffic table feeds jets, the target's velocity comes from it, attacks hold their
   target, the secondary weapon sequence (select 2, fire, select 1), the combat log lines, the version.
This is a model, not Frostbite flight physics: it validates the tactics, not exact live behaviour.
"""
from pathlib import Path
import argparse, subprocess, sys, tempfile
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
VAL = ROOT / 'validation'

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    flags = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'] if a.sanitize else ['-O1']
    with tempfile.TemporaryDirectory(prefix='bf3-v20-') as tmp:
        exe = str(Path(tmp) / 'sim')
        subprocess.run(['gcc', '-std=c11', '-O1', *flags, '-Wall', '-Wno-misleading-indentation', '-I', str(SRC), str(VAL / 'air_combat_sim.c'),
                        *[str(SRC / f) for f in ('enhanced.c', 'air.c', 'modes.c')], '-lm', '-o', exe], check=True)
        out = subprocess.run([exe], check=True, stdout=subprocess.PIPE, text=True).stdout
        print(out, end='')
        assert 'PASS: V20 air combat simulation' in out
    print('PASS: V20 closed-loop air combat: no rams, no crashes, dogfight gun time, strike dives, gunship fire, 5 model variants')

    air = (SRC / 'air.c').read_text(); nat = (SRC / 'air_native.inc').read_text(); hdr = (SRC / 'air.h').read_text()
    assert 'BM_FLIGHT_EXTEND' in hdr and 'trafficCount' in hdr and 'int secondary;' in hdr
    for name in ('jet_evasion', 'break_point', 'jet_steer', 'jet_combat', 'jet_pull', 'air_secondary', 'pid_time'):
        assert f'static ' in air and f' {name}(' in air, name
    assert 'jet && !hazard && (!aboveCeiling || areaCall)' in air          # emergencies and the ceiling stay with the flight law (V29: but for a jet leaving its area)
    assert 'range<closing*2.0f' in air and 'JET_BREAK_RANGE' in air   # never ram: break inside 2 s to impact
    assert 'secondaryThisPass' in air                          # one rocket salvo per strike dive
    assert 'attackCap=jet ? c->attackSeconds : c->attackSeconds*3' in air   # helicopters stay on target
    assert 'air_traffic_update(); if(a->profile->kind==BM_AIR_JET) air_traffic_fill(a->veh,&o);' in nat
    assert 'air_traffic_velocity(a->targetVeh)' in nat
    assert 'int holding=(a->control.phase==BM_FLIGHT_ATTACK || setup)' in nat and 'a->control.ipValid=0;' in nat and 'o->velocity[1]>2 && s->phase!=BM_FLIGHT_ATTACK' in air
    assert 'int want=(o->armSecondary || as->secondaryAt) ? 2 : 1;' in nat and 'fire=H->frame>=start && H->frame<start+12;' in nat   # V22: missile held selected while it locks
    assert 'fires its secondary weapon' in nat and 'air_combat_log(i);' in nat and 'dogfight gun runs' in nat
    assert '400*acosf' in nat                                  # Fun-Bots angle penalty on targets behind
    assert '#define BM_ENHANCED_VERSION "AltBotMod V34 - Squad Up"' in (SRC / 'enhanced.h').read_text()
    print('PASS: V20 sources: traffic table, target hold, angle penalty, secondary weapon sequence, combat logs, version')

if __name__ == '__main__':
    main()
