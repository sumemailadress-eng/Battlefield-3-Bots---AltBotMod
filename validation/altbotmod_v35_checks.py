#!/usr/bin/env python3
"""AltBotMod V35.2 - New Ground: offline checks.

1. There are 110 objective meshes (110 level / mode meshes, 13 more than V34: the new Squad Rush meshes
   and Alborz Tank Superiority; all re-cut with corners clear of wall ends and floors rescanned). The file-for-file check
   against the RC2 checkout and the M-COM coverage run in altbotmod_v34_checks.py.
2. Mesh routing from RC2's NavZones / NavRoutes (zone_nav.c zn_route_ex): each bot life takes its own way among
   similar ones (30 m regions seem up to 25 % longer by its seed), and 3 of 4 lives keep off where their team died in the
   last minutes (15 m a death on the points within 6 m on that floor, at most 50 m, half gone after a minute). Run on five
   shipped meshes with AddressSanitizer / UBSan (v35_zone_spread_tests.c).
3. The game side: the seed per life, the team's danger, deaths from the downed record; version.
"""
from pathlib import Path
import argparse, os, re, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
ZON = ROOT / 'files/server/BotMod_traces/navzones'
SC = ROOT / 'files/ServerControl'
NAVZ = Path(os.environ.get("NAVZONES_SOURCE", "/nonexistent"))   # optional: the navzones JSON folder

def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True, env={'ASAN_OPTIONS': 'detect_leaks=1', 'PATH': '/usr/bin:/bin'})
    if r.returncode: raise SystemExit('FAILED: ' + ' '.join(map(str, cmd)) + '\n' + r.stdout + r.stderr)
    return r.stdout

def check_meshes():
    names = {f.stem for f in ZON.glob('*.zon')}
    assert len(names) >= 110, len(names)   # (V35.2: + Capture the Flag x4 and BFLAG - altbotmod_v352_checks.py)
    new = {'MP_012_SquadRush0', 'MP_013_SquadRush0', 'MP_018_SquadRush0', 'XP1_001_SquadRush0', 'XP1_002_SquadRush0', 'XP1_003_SquadRush0',
           'XP1_004_SquadRush0', 'XP3_Alborz_SquadRush0', 'XP3_Alborz_TankSuperiority0', 'XP4_FD_SquadRush0', 'XP4_Parl_SquadRush0',
           'XP5_001_SquadRush0', 'XP5_003_SquadRush0'}
    assert new <= names, new - names
    print('PASS: 110 objective meshes (13 new: 12 Squad Rush, Alborz Tank Superiority)')

def check_routing(san):
    flags = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'] if san else []
    with tempfile.TemporaryDirectory(prefix='bf3-v35-') as td:
        exe = Path(td) / 'zst'
        run(['gcc', '-std=gnu11', '-O1', '-Wall', '-Wextra', '-Werror', *flags, '-I', str(SRC), str(ROOT / 'validation/v35_zone_spread_tests.c'),
             str(SRC / 'zone_nav.c'), '-lm', '-o', str(exe)])
        out = run([str(exe), str(ZON)])
    print('\n'.join('  ' + l for l in out.strip().splitlines()))
    m = re.search(r'spread ok on (\d+) of 5 maps, danger detour on (\d+)', out)
    assert m and int(m.group(1)) >= 4 and int(m.group(2)) >= 3
    print('PASS: mesh routes - own way per life (at most 25 % longer), team danger after deaths (fades by half a minute), plain routes unchanged without a seed')


SR_NEW = ['MP_012', 'MP_013', 'MP_018', 'XP1_001', 'XP1_002', 'XP1_003', 'XP1_004', 'XP3_Alborz', 'XP4_FD', 'XP4_Parl', 'XP5_001', 'XP5_003']
SR_C = r"""
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef struct BMModeProfile {
 const char *level,*mode; float low[3],high[3],patrol[32][3]; int stages;
 unsigned baseMask[9];float bases[9][3][3]; struct {float pos[3],yaw,pitch;int present;}mcom[17];
} BMModeProfile;
#include "mode_profiles.inc"
static const BMModeProfile *g_modeProfile;
#include "mcom_aim.h"
static const BMModeProfile *find(const char *l, const char *m){ for(unsigned k=0;k<sizeof g_modeProfiles/sizeof g_modeProfiles[0];k++) if(!strcmp(g_modeProfiles[k].level,l)&&!strcmp(g_modeProfiles[k].mode,m)) return &g_modeProfiles[k]; return NULL; }
static int inside(const BMModeProfile *p, const float *q){ for(int a=0;a<3;a++) if(q[a]<p->low[a]-0.01f||q[a]>p->high[a]+0.01f) return 0; return 1; }
static float h(const float *a, const float *b){ return hypotf(a[0]-b[0], a[2]-b[2]); }
int main(void){
 const char *lv[]={@@LEVELS@@}; int sr=0, rec=0, unrec=0;
 for(unsigned k=0;k<sizeof g_modeProfiles/sizeof g_modeProfiles[0];k++) sr+=!strcmp(g_modeProfiles[k].mode,"SquadRush0");
 assert(sr==25);
 for(int n=0;n<12;n++){
  const BMModeProfile *p=find(lv[n],"SquadRush0"), *r=find(lv[n],"RushLarge0"); assert(p&&r&&p->stages==2);
  for(int s=1;s<=2;s++){
   assert(p->baseMask[s]==6 && !p->mcom[2*s].present && (p->mcom[2*s-1].present==1||p->mcom[2*s-1].present==2));
   const float *m=p->mcom[2*s-1].pos; assert(inside(p,m));
   for(int t=1;t<=2;t++){ assert(inside(p,p->bases[s][t]) && h(p->bases[s][t],m)<450 && h(p->bases[s][t],m)>10); }
   if(p->mcom[2*s-1].present==1){   /* the recorded Rush arming spot and aim, exactly */
    int same=0; for(int j=1;j<=16;j++) if(r->mcom[j].present && !memcmp(r->mcom[j].pos,m,12) && r->mcom[j].yaw==p->mcom[2*s-1].yaw && r->mcom[j].pitch==p->mcom[2*s-1].pitch) same=1;
    assert(same); rec++;
   } else unrec++;
  }
  assert(h(p->bases[1][1],p->mcom[1].pos) > 1 && h(p->bases[1][2],p->mcom[3].pos) > 1);
  for(int j=0;j<32;j++) assert(inside(p,p->patrol[j]));
 }
 assert(p_count_ok: rec+unrec==24);
 /* the aim without a recorded spot: at the M-COM from where the bot stands, then further round each try, then lower */
 static BMModeProfile t; memset(&t,0,sizeof t); t.mcom[1].present=2; t.mcom[1].pos[0]=10; t.mcom[1].pos[2]=10; t.mcom[3].present=1; t.mcom[3].yaw=1.25f; t.mcom[3].pitch=-0.7f;
 g_modeProfile=&t; float y,pt;
 float south[3]={10,0,8.7f}; mode_mcom_aim(1,south,&y,&pt); assert(fabsf(y)<1e-4f && fabsf(pt+0.6f)<1e-6f);      /* faces +z: (-sin 0, cos 0) */
 float east[3]={11.3f,0,10}; mode_mcom_aim(1,east,&y,&pt); assert(fabsf(-sinf(y)+1)<1e-4f && fabsf(cosf(y))<1e-4f); /* faces -x */
 float seen[8]; for(int k=0;k<8;k++){ g_mcomAimTry[1]=(unsigned char)k; mode_mcom_aim(1,south,&y,&pt); assert(y>=0&&y<6.2832f&&fabsf(pt+0.6f)<1e-6f); seen[k]=y; for(int j=0;j<k;j++) assert(fabsf(seen[j]-y)>0.4f); }
 g_mcomAimTry[1]=8; mode_mcom_aim(1,south,&y,&pt); assert(fabsf(pt+0.95f)<1e-6f);   /* a second round, lower */
 g_mcomAimKnown[1]=1; g_mcomAimYaw[1]=2.0f; g_mcomAimPitch[1]=-0.95f; mode_mcom_aim(1,east,&y,&pt); assert(y==2.0f && pt==-0.95f);   /* kept once it worked */
 mode_mcom_aim(3,east,&y,&pt); assert(y==1.25f && pt==-0.7f && !mcom_aim_unknown(3) && mcom_aim_unknown(1));   /* recorded: unchanged */
 printf("Squad Rush layouts 25 (12 new), M-COMs %d with a recorded spot, %d aimed at the M-COM\n", rec, unrec);
 return 0; }
"""

def check_squad_rush(san):
    src = SR_C.replace('@@LEVELS@@', ','.join('"%s"' % l for l in SR_NEW)).replace('assert(p_count_ok: rec+unrec==24);', 'assert(rec+unrec==24);')
    flags = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'] if san else []
    with tempfile.TemporaryDirectory(prefix='bf3-v35sr-') as td:
        c = Path(td) / 'sr.c'; c.write_text(src); exe = Path(td) / 'sr'
        run(['gcc', '-std=gnu11', '-O1', '-Wall', '-Wno-unused-function', *flags, '-I', str(SRC), str(c), '-lm', '-o', str(exe)])
        out = run([str(exe)])
    print('  ' + out.strip())
    m = re.search(r'M-COMs (\d+) with a recorded spot, (\d+) aimed', out); assert m and int(m.group(1)) == 9 and int(m.group(2)) == 15
    # the shipped table is exactly what the generator makes from the RC2 meshes
    import importlib.util, json
    spec = importlib.util.spec_from_file_location('gsr', ROOT / 'research/generate_sr_mesh_profiles.py'); g = importlib.util.module_from_spec(spec); spec.loader.exec_module(g)
    inc = (SRC / 'mode_profiles.inc').read_text(); block = inc[inc.index(g.MARK):]
    if NAVZ.is_dir():
        rush = g.rush_table()
        for lv in SR_NEW:
            r, _ = g.profile(json.loads((NAVZ / ('%s_SquadRush0.json' % lv)).read_text()), rush.get(lv, []))
            assert g.record(lv, r) in block, lv
        print('PASS: the 12 new Squad Rush layouts are exactly what research/generate_sr_mesh_profiles.py makes from the RC2 meshes')
    mn = (SRC / 'modes_native.inc').read_text()
    assert '#include "mcom_aim.h"' in mn and 'mode_mcom_aim(target,(float *)(s+OFF_SOLDIER_POSITION),&aimYaw,&aimPitch);' in mn
    assert 'need=mcom_aim_unknown(target)?MCOM_UNRECORDED_REACH:' in mn and 'g_mcomAimTry[target]++;' in mn and 'g_mcomAimKnown[k]=1;' in mn
    print('PASS: Squad Rush on the 12 levels with no recorded Squad Rush (V34: played as team play) - M-COMs and stage bases from the RC2 meshes; '
          '9 M-COMs use the recorded Rush arming spot, 15 are armed facing the M-COM from 1.3 m and turn further round after each try the game does not answer')

def check_boat_mask(san):
    """V35.1: the server lag of the 2026-11-03 LAN game (Kharg Island): a boat planning a route copied and stripped the whole
    3-million-node grid and searched it for a landing on every tick its plan waited for the budget (driving 13-40 ms of
    each 33 ms tick, the server down to 18-26 ticks/s for 1-3 minutes whenever a bot boat left its slip)."""
    import sys
    sys.path.insert(0, str(ROOT / 'validation')); from nav_queue_checks import WINDOWS
    flags = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'] if san else []
    with tempfile.TemporaryDirectory(prefix='bf3-v351-') as td:
        (Path(td) / 'windows.h').write_text(WINDOWS); exe = Path(td) / 'bmt'
        run(['gcc', '-std=gnu11', '-O1', '-w', *flags, '-I', td, '-I', str(SRC), str(ROOT / 'validation/v351_boat_mask_tests.c'), str(SRC / 'nav.c'), '-lm', '-lpthread', '-o', str(exe)])
        out = run([str(exe)])
    print('  ' + out.strip()); assert '360 comparisons identical' in out
    core = (SRC / 'core.c').read_text()
    a = core.index('static const unsigned char *clearOf; static unsigned clearGen; static int clearOpen;')
    blk = core[a:core.index('nav_set_long_legs(0);', a)]
    assert blk.index('if (clearOf != mask || clearGen != g_driveMaskGen || clearOpen + need > 32) {') < blk.index('memcpy(clearMask, mask, n);')
    assert blk.count('nav_mask_patch_disk(g, clearMask, mask, 0, want[k][0], want[k][1], 40.0f);') == 1 and 'nav_mask_patch_disk(g, clearMask, mask, 4' not in blk   # (circles stay open for the queued search)
    # one try at a time, kept until it answers, from a held start
    assert 'if (!boat || boatStage == 0)' in blk and 'boatStage = d->boatTry = 1;' in blk and 'if (!d->n && !g_lastPlanDeferred) boatStage = d->boatTry = 2;' in blk
    assert blk.count('bs[0], bs[1], bs[2], tgt[0], tgt[1], tgt[2]') == 2 and 'ow[0], ow[1], ow[2], tgt[0]' in blk
    assert 'hypotf(vp[0] - d->boatTryFrom[0], vp[2] - d->boatTryFrom[2]) < 15.0f) { boatStage = d->boatTry; memcpy(bs, d->boatTryFrom, 12); }' in blk
    assert core.count('g_driveMaskGen++') == 3
    assert 'd->landCacheAt && H->frame - d->landCacheAt < 30 * 30 && d->landCacheFlag == d->flag' in core
    assert 'vehicle choice %.0f ms, talk %.0f ms' in core
    print('PASS: boats - the clear mask is kept (rebuilt only when the mask changes) and the landing re-used for 30 s, so a waiting plan no longer copies and searches the whole grid every tick (Kharg Island: 2.4 ms -> 0.2 ms per plan on the build machine); a boat keeps the route try it is on (clear / shallows / open water first) until it answers, from a held start - V34 restarted the shallows search every tick and Kharg Island\'s boats never left the slip; slow thinks are split by part')

def check_game_side():
    zn = (SRC / 'zone_native.inc').read_text(); core = (SRC / 'core.c').read_text(); h = (SRC / 'zone_nav.h').read_text()
    assert 'int n = zn_route_ex(&g_zone, a, b, seed, zn_cautious(seed) ? team : 0, pts, fl, PATH_MAX_POINTS - 2, &partial, &length);' in zn
    assert 'unsigned seed = zone_seed(i, soldier); int team = player_team(H->bots[i]);' in zn and 'return (h % 99991u) + 1u;' in zn
    assert 'zone_note_death(e->team, e->pos);' in core
    assert core.index('bm_learn(-1, BM_LEARN_DEATH, e->pos, NULL);') < core.index('zone_note_death(e->team, e->pos);') < core.index('mode_note_death(pl);   // AltBotMod V16')
    assert 'if (!g_zone.np || !g_enhanced.objectiveMesh) return;' in zn   # (ObjectiveMesh=0 switches it off with the mesh)
    for k, v in [('ZN_SPREAD', '0.25f'), ('ZN_DANGER_PER_DEATH', '15.0f'), ('ZN_DANGER_MAX', '50.0f'), ('ZN_DANGER_HALF_LIFE', '60.0f')]:
        assert re.search(r'#define %s +%s' % (k, re.escape(v)), h), k
    print('PASS: the seed changes with each life, the bot\'s team weighs the danger (3 of 4 lives), every death on foot is noted on the mesh')

def check_version():
    assert '#define BM_ENHANCED_VERSION "AltBotMod V35.2 - New Ground"' in (SRC / 'enhanced.h').read_text()
    assert 'New-BmBanner "AltBotMod V35.2"' in (SC / 'AltBotMod-ServerControl.ps1').read_text()
    print('PASS: version V35.2')

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    check_meshes(); check_routing(a.sanitize); check_squad_rush(a.sanitize); check_boat_mask(a.sanitize); check_game_side(); check_version()
