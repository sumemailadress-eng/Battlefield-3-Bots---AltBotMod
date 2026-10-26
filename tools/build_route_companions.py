#!/usr/bin/env python3
"""AltBotMod V18.2: route companions - every level/mode gets the infantry paths of the level's other Fun-Bots files too.

For each map and game mode the server can run (the Server Control table), the runtime loads <Level>_<Mode>.map (or the
nearest same-level file, as before) and then companions/<Level>_<Mode>.map when present. This tool writes those companion files:
the infantry paths of every OTHER file of the same level, kept only where they lie within reach of the primary file's own
waypoints (CLIP_H horizontally / CLIP_V vertically - the area that mode is played in), with the paths renumbered after the
primary's, labels / actions / vehicle paths left out (they belong to the other mode). Only new ground is added: stretches already
within COVER_H of a network waypoint are left out, and each added piece is linked at both ends to the nearest network
waypoint (JOIN_H) so it joins the routes bots already walk.
Usage: build_route_companions.py <BotMod_traces dir> [--report report.json]
"""
import os, re, sys, json, math, collections

CLIP_H, CLIP_V = 20.0, 8.0     # a companion waypoint must be this close to one of the primary's own waypoints
COVER_H, COVER_V = 3.0, 1.5     # a waypoint this close to one already in the network is ground already covered
JOIN_H, JOIN_V = 3.0, 1.5       # a new piece's ends are linked to the nearest network waypoint this close (none: piece dropped)
MIN_PRIMARY = 50               # fewer infantry waypoints than this: not a usable file (Fun-Bots' dummy Air Superiority files)
MIN_PIECE = 3                  # shorter runs of kept points are dropped
MODE_ORDER = ["ConquestLarge0", "ConquestSmall0", "ConquestAssaultLarge0", "ConquestAssaultSmall0", "ConquestAssaultSmall1",
              "RushLarge0", "SquadRush0", "TeamDeathMatch0", "SquadDeathMatch0", "GunMaster0", "Domination0", "TankSuperiority0",
              "Scavenger0", "CaptureTheFlag0", "AirSuperiority0"]
TEAM_PLAY = ["TeamDeathMatch0", "SquadDeathMatch0", "GunMaster0", "Domination0", "Scavenger0", "CaptureTheFlag0"]   # = trace_level_load
RUSH_PLAY = ["SquadRush0", "RushLarge0"]
MAPS = [('MP_001',0,0),('MP_003',0,0),('MP_007',0,0),('MP_011',0,0),('MP_012',0,0),('MP_013',0,0),('MP_017',0,0),('MP_018',0,0),
        ('MP_Subway',0,0),('XP1_001',0,1),('XP1_002',0,0),('XP1_003',0,1),('XP1_004',0,1),('XP2_Palace',1,0),('XP2_Office',1,0),
        ('XP2_Factory',1,0),('XP2_Skybar',1,0),('XP3_Alborz',0,0),('XP3_Shield',0,0),('XP3_Desert',0,0),('XP3_Valley',0,0),
        ('XP4_Parl',0,0),('XP4_Quake',0,0),('XP4_FD',0,0),('XP4_Rubble',0,1),('XP5_001',0,0),('XP5_002',0,0),('XP5_003',0,0),('XP5_004',0,0)]

def map_modes(code, cq, assault):   # = Get-BmMapModes (ServerControl-Core.ps1) / bm_map_mode (modes.c)
    out = []
    if cq: out.append('Domination0')
    elif assault:
        out += ['ConquestAssaultLarge0', 'ConquestAssaultSmall0']
        if code.startswith('XP1_'): out.append('ConquestAssaultSmall1')
    else:
        out += ['ConquestLarge0', 'ConquestSmall0']
        if code == 'XP1_002': out.append('ConquestAssaultSmall0')
    if not cq: out += ['RushLarge0', 'TeamDeathMatch0']
    out += ['TeamDeathMatchC0', 'SquadDeathMatch0']
    if not cq: out.append('SquadRush0')
    if cq or code.startswith('XP4_'): out.append('GunMaster0')
    if code.startswith('XP4_'): out.append('Scavenger0')
    if code.startswith('XP3_'): out.append('TankSuperiority0')
    if code.startswith('XP5_'): out += ['AirSuperiority0', 'CaptureTheFlag0']
    return out

def parse(path):
    """-> {path: [(point, x, y, z, input, data)]}, set of vehicle paths"""
    paths = collections.OrderedDict(); veh = set()
    with open(path, encoding='utf-8', errors='replace') as f:
        next(f, None)
        for line in f:
            p = line.rstrip('\r\n').split(';', 6)
            if len(p) < 6: continue
            try: pi, pt = int(p[0]), int(p[1]); x, y, z = float(p[2]), float(p[3]), float(p[4]); inp = int(p[5])
            except ValueError: continue
            if not all(map(math.isfinite, (x, y, z))): continue
            data = p[6] if len(p) > 6 else ''
            if pi <= 0: continue
            if '"Vehicles"' in data: veh.add(pi)
            paths.setdefault(pi, []).append((pt, x, y, z, inp, data))
    for v in paths.values(): v.sort()
    return paths, veh

class Grid:
    def __init__(self, cell): self.cell = cell; self.c = collections.defaultdict(list)
    def add(self, x, y, z, ref=None): self.c[(int(math.floor(x / self.cell)), int(math.floor(z / self.cell)))].append((x, y, z, ref))
    def nearest(self, x, y, z, h, v):
        cx, cz = int(math.floor(x / self.cell)), int(math.floor(z / self.cell)); r = int(math.ceil(h / self.cell)); best = None
        for dx in range(-r, r + 1):
            for dz in range(-r, r + 1):
                for (a, b, c, ref) in self.c.get((cx + dx, cz + dz), ()):
                    d = (a - x) ** 2 + (c - z) ** 2
                    if abs(b - y) <= v and d <= h * h and (best is None or d < best[0]): best = (d, ref)
        return best
    def near(self, x, y, z, h, v): return self.nearest(x, y, z, h, v) is not None

def infantry_count(paths, veh): return sum(len(v) for k, v in paths.items() if k not in veh)

def primary_for(files, level, mode):
    """the file the runtime loads for level_mode (trace_level_load): exact, else the nearest kind of play, else any"""
    def ok(m): return m in files.get(level, {}) and files[level][m]['inf'] >= MIN_PRIMARY
    if ok(mode): return mode
    first = RUSH_PLAY if 'Rush' in mode else TEAM_PLAY if ('DeathMatch' in mode or mode in ('GunMaster0', 'Scavenger0', 'CaptureTheFlag0')) else None
    for lst in ([first] if first else []) + [MODE_ORDER]:
        for m in lst:
            if m != mode and ok(m): return m
    return None

LINK_RE = re.compile(r'"Links":\[(\[[^\]]*\](?:,\[[^\]]*\])*)\]')

def build(tdir):
    os.makedirs(os.path.join(tdir, 'companions'), exist_ok=True)
    files = collections.defaultdict(dict)
    for fn in sorted(os.listdir(tdir)):
        m = re.match(r'^(.+?)_((?:Conquest|Rush|Squad|Team|Gun|Domination|Tank|Scavenger|Capture|Air)\w*?\d)\.map$', fn)
        if not m: continue
        paths, veh = parse(os.path.join(tdir, fn))
        files[m.group(1)][m.group(2)] = {'fn': fn, 'paths': paths, 'veh': veh, 'inf': infantry_count(paths, veh)}
    report = {}
    for code, cq, assault in MAPS:
        for mode in map_modes(code, cq, assault):
            key = f'{code}_{mode}'
            prim = primary_for(files, code, mode)
            out = os.path.join(tdir, 'companions', key + '.map')
            if not prim:
                report[key] = {'primary': None}
                if os.path.exists(out): os.remove(out)
                continue
            P = files[code][prim]
            clip = Grid(CLIP_H); have = Grid(3.0)
            for k, pts in P['paths'].items():
                if k in P['veh']: continue
                for (pt, x, y, z, _, _) in pts: clip.add(x, y, z); have.add(x, y, z, (k, pt))
            nxt = max(P['paths']) + 1 if P['paths'] else 1
            rows = []; added = collections.Counter(); pieces = 0; dropped_dup = 0
            for om in MODE_ORDER:
                if om == prim or om not in files[code]: continue
                O = files[code][om]
                if O['inf'] < MIN_PRIMARY: continue
                for k, pts in O['paths'].items():
                    if k in O['veh']: continue
                    # new ground: inside the mode's area and not already covered; a piece is a run of such points plus the
                    # covered point at each end (linked to the network there), so it joins what bots already walk
                    new = [clip.near(x, y, z, CLIP_H, CLIP_V) and not have.near(x, y, z, COVER_H, COVER_V) for (_, x, y, z, _, _) in pts]
                    if not any(new): dropped_dup += 1; continue
                    k0 = 0
                    while k0 < len(pts):
                        if not new[k0]: k0 += 1; continue
                        k1 = k0
                        while k1 + 1 < len(pts) and new[k1 + 1]: k1 += 1
                        a, b = k0, k1
                        if a > 0 and clip.near(*pts[a - 1][1:4], CLIP_H, CLIP_V): a -= 1
                        if b + 1 < len(pts) and clip.near(*pts[b + 1][1:4], CLIP_H, CLIP_V): b += 1
                        run = pts[a:b + 1]
                        length = sum(math.dist(run[q][1:4], run[q + 1][1:4]) for q in range(len(run) - 1))
                        ends = {}
                        if len(run) >= MIN_PIECE and length >= 4.0:
                            for q in (0, len(run) - 1):
                                hit = have.nearest(*run[q][1:4], JOIN_H, JOIN_V)
                                if hit: ends[q] = hit[1]
                        if ends:   # (a piece joined to nothing would be an island bots cannot route into)
                            newp = nxt; nxt += 1; pieces += 1
                            for q, (pt, x, y, z, inp, data) in enumerate(run):
                                d = '{"LinkMode":0,"Links":[[%d,%d]]}' % ends[q] if q in ends else ''
                                rows.append(f'{newp};{pt};{x:.3f};{y:.3f};{z:.3f};{inp};{d}')
                            for (pt, x, y, z, inp, data) in run: have.add(x, y, z, (newp, pt))
                            added[om] += len(run)
                        k0 = k1 + 1
            if rows:
                with open(out, 'w', encoding='utf-8', newline='\n') as f:
                    # the runtime only attaches it to the file it was built for (name and size in this header line)
                    f.write('pathIndex;pointIndex;transX;transY;transZ;inputVar;data;{"Primary":"%s","PrimaryBytes":%d,"AltBotMod":"infantry paths of %s within %.0f m of the primary waypoints"}\n'
                            % (P['fn'], os.path.getsize(os.path.join(tdir, P['fn'])), ' '.join(sorted(added)), CLIP_H))
                    f.write('\n'.join(rows) + '\n')
            elif os.path.exists(out): os.remove(out)
            report[key] = {'primary': P['fn'], 'exact': prim == mode, 'primaryWaypoints': P['inf'], 'added': sum(added.values()),
                           'from': dict(added), 'pieces': pieces, 'pathsAlreadyCovered': dropped_dup}
    return report

if __name__ == '__main__':
    rep = build(sys.argv[1])
    if '--report' in sys.argv: json.dump(rep, open(sys.argv[sys.argv.index('--report') + 1], 'w'), indent=1)
    none = [k for k, v in rep.items() if not v['primary']]
    print(f'{len(rep)} map/mode combinations; {sum(1 for v in rep.values() if v["primary"] and v["exact"])} with their own file, '
          f'{sum(1 for v in rep.values() if v["primary"] and not v["exact"])} borrowing a same-level file, {len(none)} without any: {none}')
    print(f'companions: {sum(1 for v in rep.values() if v.get("added"))} files, {sum(v.get("added", 0) for v in rep.values())} waypoints added')
