"""AltBotMod V6 checks: Fun-Bots vehicle route graphs (land / boat paths), the director's neutral-flag preference and
armour target priority. Compiles the real trace_nav.c, director_policy.h and enhanced.c (with ASan/UBSan on --sanitize)."""
from pathlib import Path
import argparse, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
TRACES = ROOT / 'files/server/BotMod_traces'

C = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "trace_nav.h"
#include "director_policy.h"
#include "enhanced.h"
static float nearest(const TraceNet *t, float x, float z, float *y)
{
    float best = 1e30f;
    for (int k = 0; k < t->count; k++) { float dx = t->nodes[k].p[0] - x, dz = t->nodes[k].p[2] - z, d = dx * dx + dz * dz;
        if (d < best) { best = d; *y = t->nodes[k].p[1]; } }
    return sqrtf(best);
}
int main(int argc, char **argv)
{
    char kharg[512], canals[512];
    snprintf(kharg, sizeof kharg, "%s/MP_018_ConquestLarge0.map", argv[1]);
    snprintf(canals, sizeof canals, "%s/MP_017_ConquestLarge0.map", argv[1]);
    // Infantry graph unchanged by the vehicle loader; vehicle paths kept apart from it.
    TraceNet inf, land, water;
    assert(tn_load(&inf, kharg) && inf.count == 4355 && inf.vehiclePaths == 16);
    assert(tn_load_kind(&land, kharg, TN_KIND_LAND) && land.count > 2000 && land.pathCount == 16);
    assert(!tn_load_kind(&water, kharg, TN_KIND_WATER));   // Kharg: no recorded boat lanes
    // Kharg flags (log): 3 at (-276, 360), 2 at (-379, 182), 4 at (-490, 165), 1 at (-534, -90), 0 at (-255, -1).
    // RU base vehicles near (25, 520), US base vehicles near (-440, -470): every flag a road route from a base.
    const float fl[5][2] = {{-255, -1}, {-534, -90}, {-379, 182}, {-276, 360}, {-490, 165}};
    const float base[2][2] = {{-440, -470}, {25, 520}};
    int routes = 0;
    for (int f = 0; f < 5; f++) {
        float y = 0; nearest(&land, fl[f][0], fl[f][1], &y);
        float g[3] = {fl[f][0], y, fl[f][1]};
        if (!tn_field(&land, f, g, 45.0f)) continue;
        for (int b = 0; b < 2; b++) {
            float by = 0; if (nearest(&land, base[b][0], base[b][1], &by) > 45) continue;
            float s[3] = {base[b][0], by, base[b][1]}; int cand[4];
            int n = tn_candidates(&land, f, s, 45.0f, 8.0f, cand, 4);
            if (!n) continue;
            float pts[256 * 3]; unsigned char fl8[256]; int tr = 0; float len = 0;
            int m = tn_route(&land, f, cand[0], 6.0f, pts, fl8, 256, &tr, &len);
            if (m < 2) continue;
            float ex = pts[(m - 1) * 3] - g[0], ez = pts[(m - 1) * 3 + 2] - g[2];
            assert(sqrtf(ex * ex + ez * ez) < 46.0f);
            for (int k = 1; k < m; k++) { float dx = pts[k * 3] - pts[k * 3 - 3], dz = pts[k * 3 + 2] - pts[k * 3 - 1]; assert(sqrtf(dx * dx + dz * dz) < 30); }
            routes++;
        }
    }
    assert(routes >= 5);
    // Noshahr: boat lanes from the US carrier (-200, 570) to the shore.
    tn_free(&water);
    assert(tn_load_kind(&water, canals, TN_KIND_WATER) && water.pathCount == 3 && water.count > 150);
    {
        float y = 0; nearest(&water, -281, 249, &y); float g[3] = {-281, y, 249};
        assert(tn_field(&water, 0, g, 150.0f));
        float cy = 0; assert(nearest(&water, -200, 570, &cy) < 60);
        float s[3] = {-200, cy, 570}; int cand[4];
        assert(tn_candidates(&water, 0, s, 60.0f, 12.0f, cand, 4) > 0);
    }
    tn_free(&inf); tn_free(&land); tn_free(&water);
    puts("PASS: Fun-Bots vehicle paths load as their own graphs (Kharg: 16 land routes linking both bases to the flags; Noshahr: 3 boat lanes from the carrier); the infantry graph is unchanged");

    // Director: of two equally distant hostile flags, the neutral one is attacked first.
    {
        static BMDirectorState st; BMDirectorFlag flags[3]; BMDirectorUnit units[2];
        memset(&st, 0, sizeof st); memset(flags, 0, sizeof flags); memset(units, 0, sizeof units);
        for (int i = 0; i < 2; i++) st.unit[i].flag = -1;
        flags[0].owner = 2; flags[0].position[0] = 300;      // enemy
        flags[1].owner = 0; flags[1].position[0] = -300;     // neutral, same distance
        flags[2].owner = 1; flags[2].position[2] = 900;      // ours, far
        units[0].active = 1; units[0].team = 1; units[0].mobile = 1;
        bm_director_update(&st, flags, 3, units, 1, 30);
        assert(st.unit[0].flag == 1 && st.unit[0].role == BM_OBJECTIVE_ATTACK);
        flags[1].owner = 2;   // both enemy: the nearer-by-tiebreak (first) one, no preference
        memset(&st, 0, sizeof st); st.unit[0].flag = -1;
        bm_director_update(&st, flags, 3, units, 1, 30);
        assert(st.unit[0].flag == 0 || st.unit[0].flag == 1);
    }
    puts("PASS: the director sends a free bot to a neutral flag before an equally distant enemy flag (neutral flags were left untaken)");

    // Armour: an enemy vehicle at 200-400 m outranks a soldier at 90 m; air within 500 m still before far infantry.
    assert(bm_target_score(1, BM_TARGET_GROUND, 200) < bm_target_score(1, BM_TARGET_INFANTRY, 90));
    assert(bm_target_score(1, BM_TARGET_GROUND, 390) < bm_target_score(1, BM_TARGET_INFANTRY, 99));
    assert(bm_target_score(1, BM_TARGET_GROUND, 100) < bm_target_score(1, BM_TARGET_INFANTRY, 80));   // (a close engineer - an RPG - still comes first)
    assert(bm_target_score(1, BM_TARGET_AIR, 400) > bm_target_score(1, BM_TARGET_INFANTRY, 350));   // V17: a far aircraft is the AA launchers' job
    // AltBotMod V7: a helicopter comes after enemy armour and nearby infantry (tanks kept stopping the push for choppers)
    assert(bm_target_score(1, BM_TARGET_AIR, 300) > bm_target_score(1, BM_TARGET_GROUND, 300));
    assert(bm_target_score(1, BM_TARGET_AIR, 300) > bm_target_score(1, BM_TARGET_INFANTRY, 90));
    assert(bm_target_score(1, BM_TARGET_AIR, 300) > bm_target_score(1, BM_TARGET_INFANTRY, 250));   // V17
    // AltBotMod V8: soldiers within ~150 m before a helicopter 300 m off; a helicopter right overhead still first
    assert(bm_target_score(1, BM_TARGET_INFANTRY, 150) < bm_target_score(1, BM_TARGET_AIR, 300));
    assert(bm_target_score(1, BM_TARGET_AIR, 100) < bm_target_score(1, BM_TARGET_INFANTRY, 150));
    puts("PASS: a bot tank engages an enemy tank / vehicle out to 400 m before nearby infantry (V5 kept shooting soldiers while a player's tank drove up); V7: helicopters after armour and close infantry");
    return 0;
}
'''

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='bf3-altbotmod-v6-') as tmp:
        c = Path(tmp) / 'v6.c'; c.write_text(C); exe = Path(tmp) / 'v6'
        san = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g'] if a.sanitize else ['-O1']
        subprocess.run(['gcc', *san, '-std=gnu11', '-Wall', '-Wextra', '-Wno-unused-function', '-I', str(SRC), str(c),
                        str(SRC / 'trace_nav.c'), str(SRC / 'enhanced.c'), str(SRC / 'air.c'), '-lm', '-o', str(exe)], check=True)
        subprocess.run([str(exe), str(TRACES)], check=True, env={'ASAN_OPTIONS': 'detect_leaks=0'})

if __name__ == '__main__':
    main()
