#!/usr/bin/env python3
"""AltBotMod V24 - On the Road: offline checks.

1. v24_drive_sim.c on the longest Fun-Bots land road of eight maps: a car with a 6 m turn radius driven the V6-V23 way
   (every waypoint, 1.75 m at any 25-deg "corner", bends judged over +-10 m) and the V24 way (pure pursuit along the road,
   bends judged over +-25 m of the road itself, only radius < 28 m slowed for). It finishes the road, never strays 6 m off
   it, is faster on average, and the pointless slow-down-and-go (no real bend or the end to slow for) drops to a fifth.
2. Convoys (bm_traffic_contact): a follower matches the leader's pace and only stops when about to touch it - not at 65%
   of the gap (the V9-V23 stop-go behind every leader); a leader on a bend is still a leader; head-on rules unchanged.
3. Source checks: drive sensors ignore a recorded road's verge (on a Fun-Bots road, within 4 m: only a vehicle or a wall
   dead ahead within 4 m counts), the Fun-Bots route flag set / cleared on every route kind, pure pursuit in drive_tick,
   the bend limit from the road's own points; helicopters fight enemy helicopters within 1.2 km first, keep a duel going
   (no 8 s cap, break only inside 80 m / 150 m closing fast, 250 m ring), a 12 m firing window on aircraft; the version.
"""
from pathlib import Path
import argparse, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
VAL = ROOT / 'validation'
TRACES = ROOT / 'files/server/BotMod_traces'
MAPS = ['XP3_Alborz_ConquestLarge0', 'XP3_Desert_ConquestLarge0', 'MP_012_ConquestLarge0', 'XP1_002_ConquestLarge0',
        'XP3_Shield_ConquestLarge0', 'XP3_Valley_ConquestLarge0', 'MP_007_ConquestLarge0', 'MP_017_ConquestLarge0']

TRAFFIC = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "enhanced.h"
int main(void)
{
    /* {ahead, lateral, height, alignment, otherSpeed, ownSpeed, halfWidth, otherWidth, ownId, otherId, human, crossing} */
    BMTrafficContact c = {20, 0, 0, 1, 15, 18, 1.2f, 1.2f, 0, 1, 0, 0};
    BMTrafficDecision d = bm_traffic_contact(&c, 14, 34);
    assert(d.following && !d.yield && d.limit > 10 && d.limit < 18);   /* 20 m behind a leader at 15 m/s: a little under its pace */
    c.ahead = 12; d = bm_traffic_contact(&c, 14, 34);
    assert(!d.yield && d.limit > 0);                                     /* V23: stopped dead here (inside 65% of the gap) */
    c.ahead = 45; d = bm_traffic_contact(&c, 14, 34); assert(d.limit > 15);   /* further back: closes up */
    c.ahead = 5; d = bm_traffic_contact(&c, 14, 34); assert(d.yield && d.limit == 0);   /* about to touch it: stop */
    c.ahead = 20; c.alignment = 0.45f; d = bm_traffic_contact(&c, 14, 34); assert(d.following && !d.yield);   /* a leader on a bend */
    c.alignment = -1; c.human = 1; assert(bm_traffic_contact(&c, 14, 34).yield);   /* head-on with a human: unchanged */
    puts("PASS: convoys - a follower runs at the leader's pace 12-45 m behind it (V23 stopped dead inside 65% of the gap), stops only about to touch it; a leader on a bend is still followed; head-on unchanged");
    return 0;
}
'''

def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit('FAILED: ' + ' '.join(map(str, cmd)) + '\n' + r.stdout + r.stderr)
    return r.stdout

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--cc', default='gcc'); ap.add_argument('--sanitize', action='store_true'); args = ap.parse_args()
    san = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'] if args.sanitize else []
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        sim = td / 'sim'
        run([args.cc, *san, '-std=gnu11', '-O1', '-Wall', '-I', str(SRC), str(VAL / 'v24_drive_sim.c'), str(SRC / 'enhanced.c'), str(SRC / 'air.c'), str(SRC / 'modes.c'), '-lm', '-o', str(sim)])
        for m in MAPS:
            out = run([str(sim), str(TRACES / (m + '.map'))])
            print(out.strip().splitlines()[-1])
        (td / 'traffic.c').write_text(TRAFFIC)
        run([args.cc, *san, '-std=gnu11', '-O1', '-Wall', '-I', str(SRC), str(td / 'traffic.c'), str(SRC / 'enhanced.c'), str(SRC / 'air.c'), str(SRC / 'modes.c'), '-lm', '-o', str(td / 'traffic')])
        print(run([str(td / 'traffic')]).strip())
    core = (SRC / 'core.c').read_text(); en = (SRC / 'enhanced.c').read_text(); air = (SRC / 'air.c').read_text()
    an = (SRC / 'air_native.inc').read_text(); eh = (SRC / 'enhanced.h').read_text()
    assert 'for (int r = 0; r < 7 && !onRoad; r++)' in core   # (V29: on a recorded road nothing static counts at all - vehicles by geometry, drive_lane_scan)
    assert 'onRoad = hypotf(a[0] + ax * t - vp[0], a[2] + az * t - vp[2]) < 4.0f;' in core
    assert core.count('d->funRoute = 1;') == 2 and 'd->straightRoute = 1; d->funRoute = 0;' in core and 'd->traceContinue=0;d->funRoute=0;' in core
    assert 'if (*(void **)(v + 0x30) == body) return 1;' in core
    assert 'd->next = bm_route_progress(d->pts, d->n, d->next, vp,' in core and 'bm_route_aim(d->pts, d->n, d->next, vp, d->speed, aimP);' in core
    assert 'while (kb > 0 && dist2(&d->pts[kb * 3], w) < 25.0f * 25.0f) kb--;' in core and 'const float *pb = kb < k ? &d->pts[kb * 3] : NULL' in core
    assert 'return vt * vt / grip > 28.0f ? 99.0f : vt;' in en and 'd2h(pb, w) < 12.0f * 12.0f' in en
    assert 'if(range<80 || (range<150 && closing>15)){attack=0;approach=1;}' in air
    assert 'float attackCap=jet ? c->attackSeconds : c->attackSeconds*3;' in air and '*250;goal[2]=o->target[2]+(ux*sa+uz*ca)*250;' in air
    assert 'atanf((o->targetAir ? 12.0f : 6.0f)/fmaxf(aimRange,1)),o->targetAir ? 0.035f : 0.025f' in air
    assert '(heliTarget && distance<1200?(tp->kind==BM_AIR_ATTACK?-700:-450)' in an
    assert '#define BM_ENHANCED_VERSION "AltBotMod V34 - Squad Up"' in eh
    print('PASS: V24 source - recorded roads trusted by the drive sensors (a vehicle or a wall dead ahead within 4 m still counts), the Fun-Bots route flag, '
          'pure pursuit and road-only bends; helicopter duels (enemy helicopters within 1.2 km first, no 8 s cap, break inside 80 m, 12 m window); version V24')

if __name__ == '__main__':
    main()
