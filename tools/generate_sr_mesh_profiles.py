"""AltBotMod V35: Squad Rush layouts for the 12 levels the Fun-Bots route files never recorded one for, from the objective
meshes of Fun-Bots V3.6.0-RC2 (navzones/<Level>_SquadRush0.json).

    mcom     the mesh's "mcom 1" / "mcom 2" zones (stage 1 / 2; checked on the 13 recorded levels: every one lines up
             1.2-2.0 m from the recorded arming spot). Where a Rush M-COM of the same level was recorded within 2.5 m,
             its arming spot and aim are taken (present 1). Otherwise the zone centre (the M-COM) with present 2: the
             aim is unknown, the bot looks at the M-COM from where it stands and turns a little further on each try
             that does not arm (modes_native.inc mode_mcom_aim).
    bases    per stage from the mesh's spawn zones, along the axis M-COM 1 -> M-COM 2: stage 1 attackers the nearest
             US spawn zone behind M-COM 1, stage 2 attackers the nearest one before M-COM 2; defenders the nearest RU
             spawn zone behind each M-COM. (On the 13 recorded levels 49 of 52 such guesses are within 120 m of the
             recorded base - the radius the spawn search uses; a wrong one is caught by the spawn safety nets.)
    low/high the mesh's points and the bases, 60 m round them, 15 m down, 60 m up.
    patrol   32 mesh points evenly over the M-COM zones.

Usage: python3 research/generate_sr_mesh_profiles.py <fun-bots navzones folder> [--write]
"""
import json, math, re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
INC = ROOT / 'files/BotMod/src/mode_profiles.inc'
LEVELS = ['MP_012', 'MP_013', 'MP_018', 'XP1_001', 'XP1_002', 'XP1_003', 'XP1_004', 'XP3_Alborz', 'XP4_FD', 'XP4_Parl', 'XP5_001', 'XP5_003']
MARK = '// AltBotMod V35: Squad Rush from the Fun-Bots V3.6.0-RC2 objective meshes (research/generate_sr_mesh_profiles.py)'

def h(a, b): return math.hypot(a[0] - b[0], a[2] - b[2])

DUMP = r"""
#include <stdio.h>
#include <string.h>
typedef struct BMModeProfile {
 const char *level,*mode; float low[3],high[3],patrol[32][3]; int stages;
 unsigned baseMask[9];float bases[9][3][3]; struct {float pos[3],yaw,pitch;int present;}mcom[17];
} BMModeProfile;
#include "mode_profiles.inc"
int main(void){ for(unsigned k=0;k<sizeof g_modeProfiles/sizeof g_modeProfiles[0];k++){const BMModeProfile*p=&g_modeProfiles[k];
  if(strcmp(p->mode,"RushLarge0"))continue;
  for(int m=1;m<=16;m++) if(p->mcom[m].present==1) printf("%s %.6f %.6f %.6f %.6f %.6f\n",p->level,p->mcom[m].pos[0],p->mcom[m].pos[1],p->mcom[m].pos[2],p->mcom[m].yaw,p->mcom[m].pitch);} return 0;}
"""
def rush_table():
    """The recorded Rush M-COMs per level: {level: [(pos, yaw, pitch)]} (compiled from mode_profiles.inc itself)."""
    import subprocess, tempfile
    with tempfile.TemporaryDirectory() as td:
        c = Path(td) / 'd.c'; c.write_text(DUMP); exe = Path(td) / 'd'
        subprocess.run(['gcc', '-w', '-I', str(INC.parent), str(c), '-o', str(exe)], check=True)
        out = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout
    t = {}
    for l in out.splitlines():
        x = l.split(); t.setdefault(x[0], []).append((tuple(map(float, x[1:4])), float(x[4]), float(x[5])))
    return t

def bases_for(m, us, ru):
    ax = (m[1][0] - m[0][0], m[1][2] - m[0][2]); L = math.hypot(*ax) or 1.0; ax = (ax[0] / L, ax[1] / L)
    pr = lambda p: (p[0] - m[0][0]) * ax[0] + (p[2] - m[0][2]) * ax[1]
    b = {}
    c = [u for u in us if pr(u) < -20 and h(u, m[0]) < 400]
    b[(1, 1)] = min(c, key=lambda u: h(u, m[0])) if c else min(us, key=pr)
    c = [u for u in us if -60 < pr(u) < pr(m[1]) - 15 and u is not b[(1, 1)]]
    b[(2, 1)] = min(c, key=lambda u: h(u, m[1])) if c else b[(1, 1)]
    for s in (1, 2):
        c = [r for r in ru if pr(r) > pr(m[s - 1]) + 15]
        b[(s, 2)] = min(c, key=lambda r: h(r, m[s - 1])) if c else max(ru, key=pr)
    return b

def profile(d, rush):
    zs = d['zones']; pts = d['points']
    mz = sorted([z for z in zs if z['kind'] == 'mcom'], key=lambda z: int(z['name'].split()[1]))
    assert [int(z['name'].split()[1]) for z in mz] == [1, 2], [z['name'] for z in mz]
    m = [tuple(z['center']) for z in mz]
    us = [tuple(z['center']) for z in zs if z['name'].startswith('spawn us')]
    ru = [tuple(z['center']) for z in zs if z['name'].startswith('spawn ru')]
    assert us and ru
    b = bases_for(m, us, ru)
    mcom = [dict(pos=(0.0, 0.0, 0.0), yaw=0.0, pitch=0.0, present=0) for _ in range(17)]
    matched = 0
    for s in (1, 2):
        near = [r for r in rush if math.dist(r[0], m[s - 1]) < 2.5]
        if near:
            r = min(near, key=lambda r: math.dist(r[0], m[s - 1]))
            mcom[2 * s - 1] = dict(pos=r[0], yaw=r[1], pitch=r[2], present=1); matched += 1
        else:
            mcom[2 * s - 1] = dict(pos=m[s - 1], yaw=0.0, pitch=-0.6, present=2)
    inside = []
    for z in mz:
        inside += [pts[k] for k in (z.get('insidePoints') or [])] if isinstance(z.get('insidePoints'), list) else []
    if not inside:   # points within the M-COM zones' radius
        inside = [p for p in pts if any(h(p, z['center']) <= z['radius'] for z in mz)]
    patrol = [tuple(inside[round(i * (len(inside) - 1) / 31)][0:3]) for i in range(32)]
    allp = [p[0:3] for p in pts] + list(b.values())
    low = [min(p[0] for p in allp) - 60, min(p[1] for p in allp) - 15, min(p[2] for p in allp) - 60]
    high = [max(p[0] for p in allp) + 60, max(p[1] for p in allp) + 60, max(p[2] for p in allp) + 60]
    bases = [[(0.0, 0.0, 0.0)] * 3 for _ in range(9)]; mask = [0] * 9
    for (s, t), p in b.items(): bases[s][t] = p; mask[s] |= 1 << t
    return dict(low=low, high=high, patrol=patrol, stages=2, baseMask=mask, bases=bases, mcom=mcom), matched

def f(x): return '%.6ff' % x
def v(p): return '{' + ','.join(f(x) for x in p) + '}'
def record(level, r):
    return ('{"%s","SquadRush0",%s,%s,{%s},%d,{%s},{%s},{%s}}' % (level, v(r['low']), v(r['high']),
            ','.join(v(p) for p in r['patrol']), r['stages'], ','.join('%du' % m for m in r['baseMask']),
            ','.join('{' + ','.join(v(p) for p in s) + '}' for s in r['bases']),
            ','.join('{%s,%s,%s,%d}' % (v(m['pos']), f(m['yaw']), f(m['pitch']), m['present']) for m in r['mcom'])))

if __name__ == '__main__':
    src = Path(sys.argv[1]); write = '--write' in sys.argv
    text = INC.read_text()
    if MARK in text: text = text[:text.index(MARK)] + '};\n'   # (regenerate: the V35 block is the table's end)
    recs, report = [], []; rush = rush_table()
    for level in LEVELS:
        assert '{"%s","SquadRush0"' % level not in text, level
        r, matched = profile(json.loads((src / ('%s_SquadRush0.json' % level)).read_text()), rush.get(level, []))
        recs.append(record(level, r)); report.append('%s: %d of 2 M-COMs with a recorded arming spot' % (level, matched))
    print('\n'.join(report)); print('%d Squad Rush layouts, %d M-COMs with a recorded spot' % (len(recs), sum(int(x.split(': ')[1][0]) for x in report)))
    if write:
        assert text.rstrip().endswith('};')
        body = text.rstrip()[:-2].rstrip()
        if not body.endswith(','): body += ','
        INC.write_text(body + '\n' + MARK + '\n' + ',\n'.join(recs) + '\n};\n')
        print('written', INC)
