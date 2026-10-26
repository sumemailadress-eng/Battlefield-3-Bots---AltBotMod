#!/usr/bin/env python3
"""AltBotMod V34: Fun-Bots' objective walking meshes (navzones/<Level>_<Mode>.json, Fun-Bots 3.6.0-dev) -> the plain
text files BotModCore.dll reads (BotMod_traces/navzones/<Level>_<Mode>.zon). Format: see files/BotMod/src/zone_nav.h.

usage: navzones_convert.py <fun-bots navzones folder> <output folder>
Only the infantry mesh is taken (the vehicle mesh is not used by AltBotMod). Every index is checked; a file that does not
check out is skipped with a message (never a half file)."""
import json, os, sys, math

KINDS = {'capturepoint': 1, 'mcom': 2, 'base': 3}

def fnum(v):
    v = float(v)
    if not math.isfinite(v): raise ValueError('not finite')
    return ('%.2f' % v).rstrip('0').rstrip('.') if '.' in ('%.2f' % v) else '%.2f' % v

def convert(src, dst):
    d = json.load(open(src))
    name = os.path.splitext(os.path.basename(src))[0]
    pts = d['points']; np = len(pts)
    if not np: raise ValueError('no points')
    out = ['AltBotMod navzones 1 %s' % name, 'P %d' % np]
    for p in pts:
        flags = int(p[5]) if len(p) > 5 else 0
        out.append('%s %s %s %d' % (fnum(p[0]), fnum(p[1]), fnum(p[2]), flags & 7))
    edges = []
    for e in d['edges']:
        a, b, length = int(e[0]), int(e[1]), float(e[2])
        if not (0 <= a < np and 0 <= b < np) or a == b: continue
        corners = e[3] if len(e) > 3 and e[3] else []
        jumps = e[5] if len(e) > 5 and e[5] else []
        oneway = int(e[6]) if len(e) > 6 and e[6] else 0
        if oneway not in (-1, 0, 1) or len(corners) > 32: continue
        mask = 0
        for j in jumps:
            j = int(j)
            if 0 <= j < min(32, len(corners)): mask |= 1 << j
        line = '%d %d %s %d %d %d' % (a, b, fnum(length), oneway, len(corners), mask)
        if corners: line += ' ' + ' '.join('%s %s %s' % (fnum(c[0]), fnum(c[1]), fnum(c[2])) for c in corners)
        edges.append(line)
    out.append('E %d' % len(edges)); out += edges
    zones = [z for z in d['zones'] if z.get('kind') in KINDS]
    out.append('Z %d' % len(zones))
    for z in zones:
        inside = [int(i) for i in z.get('inside', []) if 0 <= int(i) < np]
        c = z['center']
        zname = str(z['name']).replace(' ', '_')[:23] or 'zone'
        out.append('%d %s %s %s %s %d %s' % (KINDS[z['kind']], fnum(z.get('radius', 20)), fnum(c[0]), fnum(c[1]), fnum(c[2]), len(inside), zname))
        out.append(' '.join(str(i) for i in inside))
    tmp = dst + '.tmp'
    with open(tmp, 'w', newline='\n') as f: f.write('\n'.join(out) + '\n')
    os.replace(tmp, dst)
    return np, len(edges), len(zones)

def main():
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    ok = bad = 0
    for fn in sorted(os.listdir(src)):
        if not fn.endswith('.json'): continue
        try:
            n = convert(os.path.join(src, fn), os.path.join(dst, fn[:-5] + '.zon')); ok += 1
        except Exception as ex:
            print('skipped %s: %s' % (fn, ex)); bad += 1
    print('converted %d mesh file(s), skipped %d' % (ok, bad))
    return 0 if ok else 1

if __name__ == '__main__': sys.exit(main())
