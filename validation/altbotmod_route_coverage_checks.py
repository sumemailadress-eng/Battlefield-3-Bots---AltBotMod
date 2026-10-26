#!/usr/bin/env python3
"""AltBotMod V18.2 - Fun-Bots routes on every map and mode.

Every map / game mode the server can run (the Server Control table: 29 maps, 215 combinations) is loaded through the
ACTUAL production trace_level_load / trace_try_file / tn_load_with_companion with the shipped files:
 - the file it picks is the mode's own Fun-Bots file, or the nearest same-level one (Squad Rush -> Rush, Gun Master /
   Scavenger / CTF / TDM Close Quarters -> TDM ...), never a placeholder (Fun-Bots' one-point Air Superiority files);
 - the mode's companion (the level's other recordings within the mode's area, tools/build_route_companions.py) is
   attached, and only to the file it was built for;
 - the network is walkable: the main connected network holds most of the waypoints, the companion waypoints join it,
   and routes are found across it (Dijkstra field from spread goals).
Writes research/route-coverage.txt (one line per map and mode). Not a live BF3 test.
"""
from pathlib import Path
import argparse, os, re, subprocess, sys, tempfile, importlib.util
sys.path.insert(0, str(Path(__file__).resolve().parent))
import caspian_v2_regressions as v2
ROOT = Path(__file__).resolve().parents[1]; SRC = ROOT / 'files/BotMod/src'; TRACES = ROOT / 'files/server/BotMod_traces'
spec = importlib.util.spec_from_file_location('brc', ROOT / 'tools/build_route_companions.py'); brc = importlib.util.module_from_spec(spec); spec.loader.exec_module(brc)

MAIN = r'''
/* mainShare: waypoints in networks of >= 100 waypoints (a Rush stage, a Conquest map) - the rest are stray islands bots
   cannot route through; compJoined: companion waypoints in a network that holds >= 50 of the primary's own waypoints */
static int component_stats(const TraceNet *t, double *mainShare, double *compJoined)
{
    int *comp = malloc(t->count * sizeof *comp), *st = malloc(t->count * sizeof *st), nc = 0;
    int *size = calloc(t->count + 1, sizeof *size), *prim = calloc(t->count + 1, sizeof *prim);
    for (int k = 0; k < t->count; k++) comp[k] = -1;
    for (int k = 0; k < t->count; k++) if (comp[k] < 0) {
        int sp = 0; st[sp++] = k; comp[k] = nc;
        while (sp) { int n = st[--sp]; size[nc]++;
            for (int e = t->edgeStart[n]; e < t->edgeStart[n + 1]; e++) { int m = t->edgeTo[e]; if (comp[m] < 0) { comp[m] = nc; st[sp++] = m; } } }
        nc++;
    }
    /* primary waypoints come first (companion paths are numbered after the primary's) */
    int primaryMaxPath = -1; for (int k = 0; k < t->primaryNodes && k < t->count; k++) if (t->nodes[k].path > primaryMaxPath) primaryMaxPath = t->nodes[k].path;
    for (int k = 0; k < t->count; k++) if (t->nodes[k].path <= primaryMaxPath) prim[comp[k]]++;
    int inBig = 0, joined = 0, cn = 0;
    for (int k = 0; k < t->count; k++) {
        if (size[comp[k]] >= 100) inBig++;
        if (t->nodes[k].path > primaryMaxPath) { cn++; if (prim[comp[k]] >= 50) joined++; }
    }
    *mainShare = (double)inBig / t->count; *compJoined = cn ? (double)joined / cn : 1.0;
    /* routes across each network: up to 8 goals spread over the waypoints, each reaching >= 95% of its own network */
    int goals = 0, ok = 1;
    for (int g = 0; g < 8; g++) {
        int k = (int)((long long)t->count * (2 * g + 1) / 16); while (k < t->count && size[comp[k]] < 100) k++; if (k >= t->count) continue;
        if (!tn_field((TraceNet *)t, g, t->nodes[k].p, 5.0f)) { ok = 0; continue; }
        int reach = 0, own = 0, c = comp[k];
        for (int m = 0; m < t->count; m++) if (comp[m] == c) { own++; if (isfinite(t->field[g].dist[m])) reach++; }
        if (reach < own * 0.95) ok = 0;
        goals++;
    }
    free(comp); free(st); free(size); free(prim);
    return ok && goals >= 3;
}
int main(int argc, char **argv)
{
    g_exe = argv[1]; bm_defaults(&g_enhanced);
    FILE *in = fopen(argv[2], "r"), *rep = fopen(argv[3], "w"); assert(in && rep);
    char lvl[64], mode[64], want[96]; int wantComp, combos = 0, own = 0, borrowed = 0, withComp = 0; long addedTotal = 0;
    double worstMain = 1, worstJoin = 1; char worstMainKey[128] = "", worstJoinKey[128] = "";
    while (fscanf(in, "%63s %63s %95s %d", lvl, mode, want, &wantComp) == 4) {
        strcpy(g_level, lvl); strcpy(g_modeName, mode); g_traceKey[0] = 0;
        trace_level_load();
        char key[160]; snprintf(key, sizeof key, "%s_%s", lvl, mode);
        if (!g_trace.count || strcmp(g_trace.name, want)) { fprintf(stderr, "%s: loaded '%s', expected %s\n", key, g_trace.count ? g_trace.name : "(nothing)", want); return 1; }
        assert(g_trace.primaryNodes >= TRACE_MIN_USABLE);
        if (wantComp != (g_trace.companionNodes > 0)) { fprintf(stderr, "%s: companion %d waypoints, expected %s\n", key, g_trace.companionNodes, wantComp ? "some" : "none"); return 1; }
        double share, joined;
        if (!component_stats(&g_trace, &share, &joined)) { fprintf(stderr, "%s: routes across the network fail\n", key); return 1; }
        if (share < worstMain) { worstMain = share; strcpy(worstMainKey, key); }
        if (joined < worstJoin) { worstJoin = joined; strcpy(worstJoinKey, key); }
        int isOwn = !strncmp(want, key, strlen(key)) && want[strlen(key)] == '.';
        combos++; own += isOwn; borrowed += !isOwn; withComp += g_trace.companionNodes > 0; addedTotal += g_trace.companionNodes;
        fprintf(rep, "%-30s %-38s %s %6d own + %6d companion waypoints, %5.1f%% in networks of 100+, %5.1f%% of companion joined to the mode's own\n",
                key, g_trace.name, isOwn ? "own     " : "borrowed", g_trace.primaryNodes, g_trace.companionNodes, share * 100, joined * 100);
        if (share < 0.90 || joined < 0.93) { fprintf(stderr, "%s: %.1f%% in one network, %.1f%% companion joined\n", key, share * 100, joined * 100); return 1; }
    }
    /* a placeholder (one point) is skipped and the level's Conquest routes are used */
    assert(combos == 215);
    printf("PASS: all %d map/mode combinations load Fun-Bots routes through the real trace_level_load: %d with their own file, %d borrowing the nearest same-level one; "
           "%d with a companion (+%ld waypoints of the level's other recordings within the mode's area); worst share in networks of 100+ waypoints %.0f%% (%s), worst companion join %.0f%% (%s); routes reach >=95%% of each network\n",
           combos, own, borrowed, withComp, addedTotal, worstMain * 100, worstMainKey, worstJoin * 100, worstJoinKey);
    return 0;
}
'''

PLACEHOLDER = r'''
int main(int argc, char **argv)
{
    g_exe = argv[1]; bm_defaults(&g_enhanced);
    /* Fun-Bots' Air Superiority file is a one-point placeholder: skipped, the Conquest routes are used */
    strcpy(g_level, "XP5_001"); strcpy(g_modeName, "AirSuperiority0"); g_traceKey[0] = 0; trace_level_load();
    assert(g_trace.count > 1000 && strstr(g_trace.name, "XP5_001_ConquestLarge0") && g_trace.primaryNodes >= TRACE_MIN_USABLE);
    /* a companion built for another file (here: the primary changed size) is not attached */
    strcpy(g_level, "XP2_Office"); strcpy(g_modeName, "TeamDeathMatchC0"); g_traceKey[0] = 0; trace_level_load();
    assert(strstr(g_trace.name, "XP2_Office_TeamDeathMatch0") && g_trace.companionNodes == 0);
    /* Squad Rush with no file of its own borrows Rush; Scavenger / CTF with none borrow the team-play routes first */
    strcpy(g_level, "MP_012"); strcpy(g_modeName, "SquadRush0"); g_traceKey[0] = 0; trace_level_load(); assert(strstr(g_trace.name, "MP_012_RushLarge0"));
    strcpy(g_level, "XP5_001"); strcpy(g_modeName, "CaptureTheFlag0"); g_traceKey[0] = 0; trace_level_load(); assert(strstr(g_trace.name, "XP5_001_TeamDeathMatch0"));
    /* a trip to another floor takes the recorded route from 8 m; on one floor still from 40 m */
    static char player[128]; static float body[4]; H->bots[0] = player; H->botCount = 1; *(float **)(player + 16) = body; strcpy(player + 64, "bot");
    strcpy(g_level, "XP2_Office"); strcpy(g_modeName, "Domination0"); g_traceKey[0] = 0; trace_level_load(); assert(g_trace.count > 1000);
    int a = -1, b = -1;
    for (int k = 0; k < g_trace.count && a < 0; k++) for (int m = 0; m < g_trace.count; m += 7) {
        float dx = g_trace.nodes[k].p[0] - g_trace.nodes[m].p[0], dz = g_trace.nodes[k].p[2] - g_trace.nodes[m].p[2], dy = g_trace.nodes[m].p[1] - g_trace.nodes[k].p[1];
        if (dy > 3.5f && dy < 6.0f && dx * dx + dz * dz > 9 * 9 && dx * dx + dz * dz < 14.5f * 14.5f) { a = k; b = m; break; }   /* (V22: one-floor trips get routes from 15 m) */
    }
    assert(a >= 0);
    memcpy(body, g_trace.nodes[a].p, 12); H->frame = 5000;
    const float *t = g_trace.nodes[b].p;
    int n = trace_plan(0, (char *)body, t[0], t[1], t[2]);
    int floorTried = g_traceTried;
    float flat[3] = {t[0], body[1], t[2]};
    H->frame += 30 * 30; trace_plan(0, (char *)body, flat[0], flat[1], flat[2]);
    assert(floorTried == 1 && g_traceTried == 2);   /* V23: Fun-Bots routes at all times - the one-floor trip of the same length is tried too (V18.2-V22: grid) */
    printf("PASS: a one-point placeholder file is skipped for the level's Conquest routes; a companion built for another file is not attached; "
           "Squad Rush borrows Rush, CTF / Scavenger the team-play routes; a %s route to another floor 9-14 m away is tried, and (V23) the same trip on one floor too\n",
           n ? "recorded" : "(no join here)");
    return 0;
}
'''

def combos():
    files = {}
    for fn in os.listdir(TRACES):
        m = re.match(r'^(.+?)_((?:Conquest|Rush|Squad|Team|Gun|Domination|Tank|Scavenger|Capture|Air)\w*?\d)\.map$', fn)
        if m:
            paths, veh = brc.parse(TRACES / fn); files.setdefault(m.group(1), {})[m.group(2)] = {'fn': fn, 'inf': brc.infantry_count(paths, veh)}
    out = []
    for code, cq, assault in brc.MAPS:
        for mode in brc.map_modes(code, cq, assault):
            prim = brc.primary_for(files, code, mode)
            assert prim, f'{code}_{mode}: no Fun-Bots routes at all'
            out.append((code, mode, files[code][prim]['fn'], int((TRACES / 'companions' / f'{code}_{mode}.map').exists())))
    return out

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    core = (SRC / 'core.c').read_text()
    pre = v2.PLAN.split('int main(int argc,char **argv){')[0].replace('%BOTPATH%', re.search(r'typedef struct BotPath \{[^\n]+', core)[0])
    pre = pre.replace('static char g_level[32]="MP_007", g_modeName[32]="TankSuperiority0";', 'static char g_level[32], g_modeName[32];')
    assert 'static char g_level[32], g_modeName[32];' in pre
    rows = combos()
    with tempfile.TemporaryDirectory(prefix='bf3-route-coverage-') as tmp:
        tmp = Path(tmp)
        def server(name, tweak=None):
            srv = tmp / name; srv.mkdir()
            for m in TRACES.glob('*.map'): os.symlink(m, str(srv) + '\\BotMod_traces\\' + m.name)
            for m in (TRACES / 'companions').glob('*.map'): os.symlink(m, str(srv) + '\\BotMod_traces\\companions\\' + m.name)
            if tweak: tweak(srv)
            return str(srv) + '\\bf3.exe'
        def placeholder(srv):
            (tmp / 'air.map').write_text('pathIndex;pointIndex;transX;transY;transZ;inputVar;data\n0;0;0.000000;0.000000;0.000000;0;{"Authors":["Joe"]}\n1;1;0.000000;0.000000;0.000000;0;\n')
            os.symlink(tmp / 'air.map', str(srv) + '\\BotMod_traces\\XP5_001_AirSuperiority0.map')
            # the Office TDM file "changed" (one byte more): its companions no longer match it
            office = TRACES / 'XP2_Office_TeamDeathMatch0.map'; t = tmp / 'office.map'; t.write_bytes(office.read_bytes() + b'\n')
            os.remove(str(srv) + '\\BotMod_traces\\' + office.name); os.symlink(t, str(srv) + '\\BotMod_traces\\' + office.name)
        lst = tmp / 'combos.txt'; lst.write_text(''.join(f'{c} {m} {f} {w}\n' for c, m, f, w in rows))
        report = ROOT / 'research/route-coverage.txt'
        for name, body, args in [('coverage', pre + MAIN, [server('srv'), str(lst), str(report)]), ('placeholder', pre + PLACEHOLDER, [server('srv2', placeholder)])]:
            p = tmp / (name + '.c'); p.write_text(body); out = p.with_suffix('')
            cmd = ['gcc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-parameter',
                   '-I', str(SRC), str(p), str(SRC / 'trace_nav.c'), str(SRC / 'enhanced.c'), str(SRC / 'air.c'), '-lm', '-o', str(out)]
            if a.sanitize and name == 'placeholder': cmd[1:1] = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
            subprocess.run(cmd, check=True); subprocess.run([str(out), *args], check=True, env=env)
    # the generator and the runtime agree on the fallback order
    tn = (SRC / 'trace_native.inc').read_text()
    assert '"TeamDeathMatch0", "SquadDeathMatch0", "GunMaster0", "Domination0", "Scavenger0", "CaptureTheFlag0", NULL' in tn
    assert brc.TEAM_PLAY == ["TeamDeathMatch0", "SquadDeathMatch0", "GunMaster0", "Domination0", "Scavenger0", "CaptureTheFlag0"]
    order = re.search(r'g_traceModes\[\] = \{(.*?)NULL', tn, re.S).group(1)
    assert re.findall(r'"(\w+)"', order) == brc.MODE_ORDER, 'trace_native.inc g_traceModes and build_route_companions.MODE_ORDER differ'
    ps = (ROOT / 'files/ServerControl/ServerControl-Core.ps1').read_text()
    assert "if ($m.Code -like 'XP5_*') { [void]$out.Add('AirSuperiority0'); [void]$out.Add('CaptureTheFlag0') }" in ps   # (the table the tool mirrors)
    print('PASS: route companions - the generator mirrors the runtime fallback order and the Server Control map/mode table')

if __name__ == '__main__': main()
