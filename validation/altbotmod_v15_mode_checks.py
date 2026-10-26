#!/usr/bin/env python3
"""AltBotMod V15 - every game mode the server runs.

1. Mode table: every mode each level's server data carries (its sublevels: squad_rush, gunmaster, scavenger,
   tanksuperiority, air_superiority, ctf ...) is recognised (V9-V14: Squad Rush, Gun Master, Scavenger, Tank / Air
   Superiority and Capture the Flag were "unknown" - the bots waited "not ready yet" all round) and sorted as Fun-Bots'
   SetGameMode sorts it; !map offers each mode exactly on the levels that have it.
2. Squad Rush layouts: 13 profiles regenerated from the route files (one M-COM a sector, bases for both teams), and
   the generator reproduces the 25 shipped Rush profiles' sectors, bases, M-COMs and patrol points.
3. The native logic compiled with stubs: Squad Rush has one objective a sector and bots never aim at the empty second
   slot; Gun Master uses the TDM play area; a round fallen back to team play stays so until the next round; the
   Capture the Flag raid (to the enemy base, sweep, home, sweep).
4. The core wiring: discovery follows the mode as played, the fallbacks (no recorded M-COMs / Capture the Flag or Air
   Superiority with no capture points), Squad Rush squads (one squad, 4 a side), mode-aware route-file fallback; every server layout has a
   route file to walk on.
"""
from pathlib import Path
import argparse, re, subprocess, sys, tempfile

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
TRACES = ROOT / 'files/server/BotMod_traces'
sys.path.insert(0, str(ROOT / 'validation')); sys.path.insert(0, str(ROOT / 'research'))
from test4_checks import function  # noqa: E402
import generate_profiles  # noqa: E402

# The modes in each level's server data (Data/Linux/Levels/<L>/<L>.toc sublevels, read for V14's kit fix).
# Conquest / Rush / TDM / SQDM are on every level as before; these are the extra ones.
EXTRA = {
    'SquadRush0': [m for m in ['MP_001', 'MP_003', 'MP_007', 'MP_011', 'MP_012', 'MP_013', 'MP_017', 'MP_018', 'MP_Subway',
                               'XP1_001', 'XP1_002', 'XP1_003', 'XP1_004', 'XP3_Alborz', 'XP3_Desert', 'XP3_Shield', 'XP3_Valley',
                               'XP4_FD', 'XP4_Parl', 'XP4_Quake', 'XP4_Rubble', 'XP5_001', 'XP5_002', 'XP5_003', 'XP5_004']],
    'GunMaster0': ['XP2_Factory', 'XP2_Office', 'XP2_Palace', 'XP2_Skybar', 'XP4_FD', 'XP4_Parl', 'XP4_Quake', 'XP4_Rubble'],
    'Scavenger0': ['XP4_FD', 'XP4_Parl', 'XP4_Quake', 'XP4_Rubble'],
    'TankSuperiority0': ['XP3_Alborz', 'XP3_Desert', 'XP3_Shield', 'XP3_Valley'],
    'AirSuperiority0': ['XP5_001', 'XP5_002', 'XP5_003', 'XP5_004'],
    'CaptureTheFlag0': ['XP5_001', 'XP5_002', 'XP5_003', 'XP5_004'],
}
LEVELS = ['MP_001', 'MP_003', 'MP_007', 'MP_011', 'MP_012', 'MP_013', 'MP_017', 'MP_018', 'MP_Subway', 'XP1_001', 'XP1_002',
          'XP1_003', 'XP1_004', 'XP2_Palace', 'XP2_Office', 'XP2_Factory', 'XP2_Skybar', 'XP3_Alborz', 'XP3_Shield',
          'XP3_Desert', 'XP3_Valley', 'XP4_Parl', 'XP4_Quake', 'XP4_FD', 'XP4_Rubble', 'XP5_001', 'XP5_002', 'XP5_003', 'XP5_004']

TABLE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "modes.h"
int main(int argc,char **argv){
 (void)argc;
 /* Fun-Bots SetGameMode: IsRush (RushLarge0, SquadRush0), IsTdm/IsGm (team play), IsSdm, IsConquest (+Domination, Scavenger) */
 assert(bm_mode("SquadRush0")==BM_MODE_RUSH && bm_mode_squad_rush("SquadRush0") && !bm_mode_squad_rush("RushLarge0"));
 assert(bm_mode("GunMaster0")==BM_MODE_TDM && bm_mode_gun_master("GunMaster0") && !bm_mode_gun_master("TeamDeathMatch0"));
 const char *cq[]={"Scavenger0","TankSuperiority0","AirSuperiority0","CaptureTheFlag0","BFLAG","BFLAG0","Domination0","ConquestLarge0"};
 for(unsigned k=0;k<sizeof cq/sizeof cq[0];k++)assert(bm_mode(cq[k])==BM_MODE_CQ);
 assert(bm_mode("SquadDeathMatch0")==BM_MODE_SQDM && bm_mode("TeamDeathMatchC0")==BM_MODE_TDM && bm_mode("Nothing0")==BM_MODE_UNKNOWN);
 /* argv[1]: lines "mode level level ..." - the levels whose server data has that mode */
 FILE *f=fopen(argv[1],"r");assert(f);char line[2048];int checked=0;
 while(fgets(line,sizeof line,f)){
  char *mode=strtok(line," \n"),*lv;char have[32][16];int n=0;
  while((lv=strtok(NULL," \n")))snprintf(have[n++],16,"%s",lv);
  for(int m=0;m<29;m++){int on=0;for(int k=0;k<n;k++)on|=!strcmp(have[k],bm_maps[m].code);
   const char *got=bm_map_mode(&bm_maps[m],mode);
   if(on!=(got!=NULL))printf("MISMATCH %s %s on=%d got=%s\n",mode,bm_maps[m].code,on,got?got:"-");
   assert(on==(got!=NULL));if(got)assert(!strcmp(got,mode)&&bm_mode(got)!=BM_MODE_UNKNOWN);checked++;}
 }
 const char *alias[][3]={{"metro","sqrush","SquadRush0"},{"ziba","gm","GunMaster0"},{"azadi","scav","Scavenger0"},{"bandar","tank","TankSuperiority0"},{"kiasar","air","AirSuperiority0"},{"riverside","ctf","CaptureTheFlag0"}};
 for(int k=0;k<6;k++){const BMMap *m=bm_map(alias[k][0]);assert(m&&!strcmp(bm_map_mode(m,alias[k][1]),alias[k][2]));}
 assert(!bm_map_mode(bm_map("ziba"),"sqrush")&&!bm_map_mode(bm_map("metro"),"gm")&&!bm_map_mode(bm_map("caspian"),"ctf"));
 printf("PASS: mode table - %d level/mode pairs match the server's level data: Squad Rush on 25 levels, Gun Master on 8, Scavenger 4, Tank Superiority 4, Air Superiority 4, Capture the Flag 4; all are recognised (V9-V14: unknown) and sorted as Fun-Bots sorts them\n",checked);
 return 0;
}
'''

NATIVE_PRE = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "modes.h"
#include "rush_policy.h"
#define MAX_BOTS 8
#define OFF_PLAYER_SOLDIER 16
#define OFF_PLAYER_ONLINE_NAME 64
#define _stricmp strcasecmp
static int squad_order_rush(int i){(void)i;return 0;}   /* AltBotMod V34 */
typedef long LONG;
static struct {unsigned frame;int botCount;char *bots[MAX_BOTS];} host,*H=&host;
static char players[MAX_BOTS][128] __attribute__((aligned(8)));
static float bodies[MAX_BOTS][4];
static enum BMMode g_mode;static char g_modeName[64];static LONG g_roundEpoch=1;
static int g_flagCount;static int g_rushStage=1;static BMRushState g_rush;
static int mode_is_cq(void){return g_mode==BM_MODE_CQ;}
static float *flag_pos(int f){(void)f;return NULL;}static int flag_owner(int f){(void)f;return 0;}
static int player_team(char *p){return *(int *)(p+8);}
static float dist2(const float *a,const float *b){float x=a[0]-b[0],z=a[2]-b[2];return x*x+z*z;}
static int near_origin(const float *p){return fabsf(p[0])<12&&fabsf(p[2])<12;}
static int logs;static void bot_log(const char *f,...){(void)f;logs++;}
static int grid=1;static int *nav_slot(void){return &grid;}
static int nav_snap(int g,float x,float y,float z,float *out){(void)g;out[0]=x;out[1]=y;out[2]=z;return 1;}
static int cancels;static void nav_cancel_path(int i){(void)i;cancels++;}
'''


def native_source():
    native = (SRC / 'modes_native.inc').read_text(); state = (SRC / 'mode_state.h').read_text()
    profile = re.search(r'typedef struct BMModeProfile.*?} BMModeProfile;', state, re.S).group(0)
    bot = re.search(r'static struct \{int target.*?g_modeBot\[MAX_BOTS\];', state).group(0)
    base = re.search(r'static float g_modeTeamBase\[3\]\[3\];', state).group(0)
    kind = re.search(r'static LONG g_modeFallbackEpoch=-1;.*?static int mode_is_ctf\(void\)\{[^\n]*\}', state, re.S).group(0)
    ctf = re.search(r'static const struct \{const char \*level;float us\[3\],ru\[3\];\} g_ctfBases\[\]=\{.*?\n\};', native, re.S).group(0)
    fns = ctf + '\nstatic char g_level[64];\n' + '\n'.join(function(native, n) for n in ['mode_profile', 'objective_count', 'objective_pos', 'objective_wanted', 'rush_target', 'mode_reassign_idle', 'ctf_base', 'mode_ctf_goal'])
    return NATIVE_PRE + profile + '\n#include "mode_profiles.inc"\nstatic const BMModeProfile *g_modeProfile;\n' + bot + '\n' + base + '\n' + kind + '\n' + fns + r'''
static void bot(int i,int team,float x,float z){H->bots[i]=players[i];*(int *)(players[i]+8)=team;bodies[i][0]=x;bodies[i][2]=z;*(float **)(players[i]+16)=bodies[i];if(H->botCount<=i)H->botCount=i+1;}
int main(void){
 /* Squad Rush: one M-COM a sector */
 g_mode=BM_MODE_RUSH;snprintf(g_modeName,sizeof g_modeName,"SquadRush0");
 g_modeProfile=mode_profile("MP_011","SquadRush0");assert(g_modeProfile&&g_modeProfile->stages==2);
 bm_rush_reset(&g_rush,g_modeProfile->stages,2,0);
 assert(mode_is_squad_rush()&&objective_count()==1);
 const float *o=objective_pos(0);assert(o&&o==g_modeProfile->mcom[1].pos&&!objective_pos(1));
 assert(objective_wanted(1,0)&&!objective_wanted(1,1));
 bot(0,1,o[0]+30,o[2]);bot(1,2,o[0]-30,o[2]);
 assert(rush_target(0,bodies[0])==1&&rush_target(1,bodies[1])==1);
 g_modeBot[0].target=0;mode_reassign_idle(0,1);assert(g_modeBot[0].target==0);   /* never the empty second slot */
 mode_reassign_idle(0,0);assert(g_modeBot[0].target==1);
 assert(bm_rush_set_stage(&g_rush,2,10));g_rushStage=2;
 assert(objective_pos(0)==g_modeProfile->mcom[3].pos&&rush_target(0,bodies[0])==3&&objective_count()==1);
 /* every Squad Rush profile: both sectors' M-COMs at slots 1 and 3, both teams' bases, slots 2 and 4 empty */
 int sr=0;for(unsigned k=0;k<sizeof g_modeProfiles/sizeof g_modeProfiles[0];k++){const BMModeProfile *p=&g_modeProfiles[k];if(!bm_mode_squad_rush(p->mode))continue;sr++;
  assert(p->stages>=1&&p->stages<=4);for(int s=1;s<=p->stages;s++){assert(p->mcom[(s-1)*2+1].present&&!p->mcom[s*2].present&&p->baseMask[s]==6);
   for(int t=1;t<=2;t++)assert(!near_origin(p->bases[s][t])||p->bases[s][t][1]!=0);
   assert(p->mcom[(s-1)*2+1].pos[0]>=p->low[0]-1&&p->mcom[(s-1)*2+1].pos[0]<=p->high[0]+1);}}
 assert(sr==13);
 /* plain Rush unchanged: two a sector */
 snprintf(g_modeName,sizeof g_modeName,"RushLarge0");g_modeProfile=mode_profile("MP_011","RushLarge0");g_rushStage=1;bm_rush_reset(&g_rush,g_modeProfile->stages,2,0);
 assert(!mode_is_squad_rush()&&objective_count()==2&&objective_pos(1)==g_modeProfile->mcom[2].pos);
 /* Gun Master plays on the TDM play area; Squad Rush with no recorded M-COMs has no profile (falls back to team play) */
 {const char *gm[]={"XP2_Factory","XP2_Office","XP2_Palace","XP2_Skybar","XP4_FD","XP4_Parl","XP4_Quake","XP4_Rubble"};
  for(int k=0;k<8;k++)assert(mode_profile(gm[k],"GunMaster0")&&mode_profile(gm[k],"GunMaster0")==mode_profile(gm[k],"TeamDeathMatch0"));}
 assert(!mode_profile("MP_012","SquadRush0")&&mode_profile("MP_Subway","SquadRush0"));
 /* the fallback lasts the round */
 assert(mode_kind("CaptureTheFlag0")==BM_MODE_CQ);g_modeFallbackEpoch=g_roundEpoch;assert(mode_kind("CaptureTheFlag0")==BM_MODE_TDM&&mode_kind("SquadRush0")==BM_MODE_TDM);
 g_roundEpoch++;assert(mode_kind("CaptureTheFlag0")==BM_MODE_CQ&&mode_kind("SquadRush0")==BM_MODE_RUSH&&mode_kind("")==BM_MODE_UNKNOWN);
 /* Capture the Flag (V16 roles by i%3): 0 raids - the enemy stand, a 12 s sweep, home, a sweep, out again; 1 guards its own stand; 2 hunts */
 g_mode=BM_MODE_TDM;snprintf(g_modeName,sizeof g_modeName,"CaptureTheFlag0");assert(mode_is_ctf());
 float us[3]={-400,60,100},ru[3]={300,60,-200};memcpy(g_modeTeamBase[1],us,12);memcpy(g_modeTeamBase[2],ru,12);
 memset(g_modeBot,0,sizeof g_modeBot);H->botCount=0;bot(0,1,us[0],us[2]);bot(1,1,us[0],us[2]);bot(2,1,us[0],us[2]);bot(3,2,ru[0],ru[2]);
 bodies[0][1]=bodies[1][1]=bodies[3][1]=60;
 H->frame=100;assert(mode_ctf_goal(0,bodies[0],1)&&dist2(g_modeBot[0].goal,ru)<7*7&&g_modeBot[0].goalReady);   /* raider: onto the stand itself */
 assert(mode_ctf_goal(1,bodies[1],1)&&dist2(g_modeBot[1].goal,us)<26*26&&dist2(g_modeBot[1].goal,us)>9*9&&g_modeBot[1].guardUntil==100+12*30);   /* guard: round its own stand */
 assert(!mode_ctf_goal(2,bodies[2],1));   /* hunter */
 assert(mode_ctf_goal(3,bodies[3],2)&&dist2(g_modeBot[3].goal,us)<20*20);
 memcpy(bodies[0],ru,12);H->frame=200;assert(mode_ctf_goal(0,bodies[0],1)&&g_modeBot[0].raidAt==200&&dist2(g_modeBot[0].goal,ru)<20*20);  /* sweeping */
 H->frame=200+12*30+1;assert(mode_ctf_goal(0,bodies[0],1)&&g_modeBot[0].raid==2&&dist2(g_modeBot[0].goal,us)<20*20);   /* home */
 memcpy(bodies[0],us,12);H->frame=1000;mode_ctf_goal(0,bodies[0],1);H->frame=1000+12*30+1;assert(mode_ctf_goal(0,bodies[0],1)&&g_modeBot[0].raid==1&&dist2(g_modeBot[0].goal,ru)<20*20);
 snprintf(g_modeName,sizeof g_modeName,"TeamDeathMatch0");assert(!mode_ctf_goal(0,bodies[0],1));
 snprintf(g_modeName,sizeof g_modeName,"CaptureTheFlag0");memset(g_modeTeamBase,0,sizeof g_modeTeamBase);assert(!mode_ctf_goal(0,bodies[0],1));   /* no team spawns known: hunt */
 /* V16: the four CTF maps' flag stands come from the level data (ctf_us_base / ctf_ru_base), whatever the spawns say */
 snprintf(g_level,sizeof g_level,"XP5_001");assert(ctf_base(1)&&fabsf(ctf_base(1)[0]-361.69f)<0.1f&&fabsf(ctf_base(2)[2]-51.97f)<0.1f);
 snprintf(g_level,sizeof g_level,"xp5_004");assert(ctf_base(2)&&fabsf(ctf_base(2)[2]+710.15f)<0.1f);
 snprintf(g_level,sizeof g_level,"MP_001");assert(!ctf_base(1));
 puts("PASS: native - Squad Rush has one objective a sector (slots 1 and 3; bots never aim at the empty slot), Rush keeps two; Gun Master uses the TDM play area; "
      "a fallback to team play lasts the round; Capture the Flag (V16): a third raid the enemy stand (sweep 12 s, home, sweep, again), a third guard their own stand, a third hunt; flag stands of all four CTF maps from the level data");
 return 0;
}
'''


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    print(r.stdout, end='')
    if r.returncode:
        print(r.stderr); raise SystemExit('FAIL: %s exited %d' % (Path(cmd[0]).name, r.returncode))
    return r.stdout


def profile_checks():
    inc = (SRC / 'mode_profiles.inc').read_text()
    # the generator reproduces the shipped Rush records (sectors, bases, M-COMs, patrol points)
    recs = {(m.group(1), m.group(2)): m.group(0) for m in re.finditer(r'^\{"([^"]+)","([^"]+)",[^\n]*?\}\}\}(?=,?$)', inc, re.M)}
    same = 0
    for (lv, mode), text in recs.items():
        if mode != 'RushLarge0': continue
        r = generate_profiles.profile(TRACES / f'{lv}_{mode}.map', mode)
        gen = generate_profiles.record(lv, mode, r)
        cut = lambda t: t[t.index('{', t.index('}}') + 2):]   # everything after low/high (12 levels' bounds moved a few metres in the later route files)
        a, b = cut(text), cut(gen)
        nums = lambda t: [float(x.rstrip('fu')) for x in re.findall(r'-?\d+\.\d+f|\b\d+u?\b', t)]
        assert all(abs(x - y) < 2e-3 for x, y in zip(nums(a), nums(b))) and len(nums(a)) == len(nums(b)), lv
        same += 1
    assert same == 25
    sr = [k for k in recs if k[1] == 'SquadRush0']
    for lv, _ in sr:
        assert recs[(lv, 'SquadRush0')] == generate_profiles.record(lv, 'SquadRush0', generate_profiles.profile(TRACES / f'{lv}_SquadRush0.map', 'SquadRush0'))
    missing = sorted(set(EXTRA['SquadRush0']) - {lv for lv, _ in sr})
    print(f'PASS: profiles - the rebuilt generator reproduces all 25 shipped Rush sectors/bases/M-COMs/patrol points; {len(sr)} Squad Rush profiles (one M-COM a sector) match it; '
          f'Squad Rush without recorded M-COMs ({len(missing)} levels: {" ".join(missing)}) is played as team play')
    return missing


def wiring_checks():
    core = (SRC / 'core.c').read_text(); state = (SRC / 'mode_state.h').read_text(); tn = (SRC / 'trace_native.inc').read_text()
    rn = (SRC / 'round_native.inc').read_text(); mn = (SRC / 'modes_native.inc').read_text()
    assert 'enum BMMode kind=g_jobKind;' in core and core.count('bm_mode(g_jobMode)==') == 1 and 'bm_mode(g_jobMode)==BM_MODE_RUSH)){tickets_reset();tickets_find();}' in core and 'g_jobKind=g_mode;' in core
    assert 'g_jobEpoch!=g_roundEpoch || g_jobKind!=g_mode' in core
    assert 'if (g_mode == BM_MODE_RUSH && !mode_profile(g_level, g_modeName)) mode_fallback(' in core
    assert 'else if (H->frame - noFlagsSince > (unsigned)noFlagsWait * 30) { mode_fallback("the level has no capture points"); return; }' in core
    assert 'int noFlagsWait = !_stricmp(g_modeName, "CaptureTheFlag0") || !_strnicmp(g_modeName, "BFLAG", 5) ? 45 : !_stricmp(g_modeName, "AirSuperiority0") ? 90 : 0;' in core
    assert 'g_mode=mode_kind(mode);' in rn and 'm!=BM_MODE_UNKNOWN && g_modeFallbackEpoch==g_roundEpoch?BM_MODE_TDM:m' in state
    assert '(g_mode==BM_MODE_SQDM || mode_is_squad_rush()) && per>4 ? 4 : per' in core and core.count('g_mode==BM_MODE_SQDM||mode_is_squad_rush()') >= 3
    assert 'enum BMMode m=mode_kind(mode);' in mn and 'bm_mode_teams(mode_kind(mode))' in mn and 'if(mode_ctf_goal(i,me,team)){goal=g_modeBot[i].goal;goto planned;}' in mn
    assert 'strstr(g_modeName, "Rush") ? rushPlay' in tn
    assert 'if (bm_mode(g_modeName) != BM_MODE_RUSH || !g_ticketCounter[1] || g_roundEnded) return;' in core and 'if (bm_mode(g_modeName) == BM_MODE_RUSH) { rush_tickets_tick(); return; }' in core
    # every layout the server runs walks on a route file: its own, else the nearest kind of play on the level
    team = ['TeamDeathMatch0', 'SquadDeathMatch0', 'GunMaster0', 'Domination0']; rush = ['SquadRush0', 'RushLarge0']
    order = re.search(r'g_traceModes\[\] = \{(.*?)NULL', tn, re.S).group(1); order = re.findall(r'"([^"]+)"', order)
    have = {p.name[:-4] for p in TRACES.glob('*.map')}
    picks = {}
    for mode, levels in EXTRA.items():
        for lv in levels:
            first = rush if 'Rush' in mode else team if ('DeathMatch' in mode or 'GunMaster' in mode) else []
            key = f'{lv}_{mode}'
            pick = key if key in have else next((f'{lv}_{m}' for m in first + order if f'{lv}_{m}' in have), None)
            assert pick, key
            picks[key] = pick
    own = sum(1 for k, v in picks.items() if k == v)
    assert picks['XP2_Office_GunMaster0'] == 'XP2_Office_TeamDeathMatch0' and picks['MP_012_SquadRush0'] == 'MP_012_RushLarge0'
    assert picks['XP5_001_CaptureTheFlag0'] == 'XP5_001_ConquestLarge0'
    print(f'PASS: wiring - discovery follows the mode as played; a Rush without recorded M-COMs and Capture the Flag (45 s) / Air Superiority (90 s) with no capture points fall back to team play for the round; '
          f'a Rush fallen back still pays attacker tickets (the round still ends); Squad Rush is one squad of 4 a side; all {len(picks)} extra-mode layouts have a route file ({own} their own, the rest the nearest kind of play on the level, e.g. Gun Master on Operation 925 -> its TDM file)')


def launcher_checks(tmp, cc):
    """The launcher's map/mode menu (Get-BmMapModes) offers exactly what bm_map_mode accepts."""
    pwsh = next((p for p in ['/home/claude/pwsh/pwsh', 'pwsh'] if subprocess.run(['which', p], capture_output=True).returncode == 0 or Path(p).exists()), None)
    if not pwsh:
        print('SKIP: launcher menu (no PowerShell)'); return
    modes = ['ConquestLarge0', 'ConquestSmall0', 'ConquestAssaultLarge0', 'ConquestAssaultSmall0', 'ConquestAssaultSmall1', 'RushLarge0', 'TeamDeathMatch0',
             'TeamDeathMatchC0', 'SquadDeathMatch0', 'Domination0'] + list(EXTRA)
    c = tmp / 'menu.c'
    c.write_text('#include <stdio.h>\n#include "modes.h"\nint main(void){const char *m[]={' + ','.join('"%s"' % x for x in modes) + '};for(int k=0;k<29;k++){printf("%s",bm_maps[k].code);'
                 'for(unsigned j=0;j<sizeof m/sizeof m[0];j++)if(bm_map_mode(&bm_maps[k],m[j]))printf(" %s",m[j]);printf("\\n");}return 0;}\n')
    subprocess.run(cc + [str(c), str(SRC / 'modes.c'), '-lm', '-o', str(tmp / 'menu')], check=True)
    native = {l.split()[0]: set(l.split()[1:]) for l in run([str(tmp / 'menu')]).splitlines() if l.strip()}
    core = ROOT / 'files/ServerControl/ServerControl-Core.ps1'
    out = subprocess.run([pwsh, '-NoProfile', '-Command', f'. "{core}"; foreach ($m in $script:BmMaps) {{ Write-Output ($m.Code + " " + ((Get-BmMapModes $m.Code) -join " ")) }}'],
                         capture_output=True, text=True, check=True).stdout
    menu = {l.split()[0]: set(l.split()[1:]) for l in out.splitlines() if l.strip()}
    assert menu == native, {k: (menu.get(k), native.get(k)) for k in native if menu.get(k) != native.get(k)}
    print(f'PASS: launcher - the map/mode menu offers exactly the {sum(len(v) for v in menu.values())} map/mode pairs the bot core accepts, the new modes included')


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    san = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g'] if a.sanitize else ['-O2']
    with tempfile.TemporaryDirectory(prefix='bf3-altbotmod-v15m-') as tmp:
        tmp = Path(tmp)
        cc = ['gcc', '-std=gnu11', '-Wall', '-Wno-unused-function', '-Wno-unused-variable', '-I', str(SRC)] + san
        (tmp / 'modes.txt').write_text(''.join(f'{m} {" ".join(v)}\n' for m, v in EXTRA.items()))
        (tmp / 'table.c').write_text(TABLE)
        subprocess.run(cc + [str(tmp / 'table.c'), str(SRC / 'modes.c'), '-lm', '-o', str(tmp / 'table')], check=True)
        run([str(tmp / 'table'), str(tmp / 'modes.txt')])
        (tmp / 'native.c').write_text(native_source())
        subprocess.run(cc + [str(tmp / 'native.c'), str(SRC / 'modes.c'), '-lm', '-o', str(tmp / 'native')], check=True)
        run([str(tmp / 'native')])
        launcher_checks(tmp, cc)
    profile_checks()
    wiring_checks()


if __name__ == '__main__':
    main()
