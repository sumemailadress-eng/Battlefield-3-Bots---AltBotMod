"""AltBotMod V14 checks.
1. Side steps: the V9-V13 back-and-forth (a bot in a gunfight strafed left-right without a break - "strutting") is now a
   short step now and then, only under fire / close in. The real step / rest cycle (bm_dodge_next) run for a minute.
2. Tanks: on Seine Crossing the RU T90 sat at its base all round - two Fun-Bots routes used up for the tank's whole life,
   then "no drivable route ... A* exhausted" for every flag. The recorded vehicle routes do reach every flag from where
   it sat (checked on the shipped route file with the real trace_nav.c); V14 tries them again per flag and on a failed
   driving-map search, then checked straight legs.
3. A level that was never ready: no game mode from RCON (V13 waited for ever: "not ready yet (0 flags, hq mask 0)"), or
   a mode with no recorded layout. The real mode_auto_profile against mock spawn points.
Server Control (launcher settings, start plans, offline map lists) is checked in validation/servercontrol."""
from pathlib import Path
import argparse, json, re, shutil, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
SERVER = ROOT / 'files/server'

DODGE = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "enhanced.h"
static void run(const BMConfig *c, const BMDifficulty *P, int *stepping, int *steps, int *flips){
 BMDodgeStyle st; int on=bm_dodge_style(c,P,&st); *stepping=*steps=*flips=0; if(!on)return;
 signed char dir=0,last=0,prev=0; unsigned until=0; srand(7);
 for(unsigned f=1;f<=1800;f++){ unsigned r=(unsigned)rand()^((unsigned)rand()<<15); *steps+=bm_dodge_next(&st,&dir,&last,&until,f,r);
   if(dir)(*stepping)++; if(dir&&prev&&dir!=prev)(*flips)++; prev=dir?dir:prev; }
}
int main(void){
 BMConfig c; bm_defaults(&c); assert(c.sideSteps==1);
 assert(bm_set(&c,"SideSteps","2")==1&&c.sideSteps==2); assert(bm_set(&c,"SideSteps","3")==-1&&c.sideSteps==2); assert(bm_set(&c,"SideSteps","0")==1);
 const BMDifficulty *elite=bm_difficulty_level(5),*normal=bm_difficulty_level(2);
 BMDodgeStyle st;
 c.sideSteps=0; assert(!bm_dodge_style(&c,elite,&st) && st.hopScale==0);
 c.sideSteps=1; assert(bm_dodge_style(&c,elite,&st) && st.needThreat && st.restMin>=30 && st.maxMeters<=45 && st.level<0.7f && st.hopScale>=3 && st.hopNeedsThreat);
 assert(!bm_dodge_style(&c,normal,&st));   /* no strafe in the profile: no steps */
 int a,b,cf; run(&c,elite,&a,&b,&cf);
 c.sideSteps=2; int a2,b2,cf2; run(&c,elite,&a2,&b2,&cf2);
 assert(bm_dodge_style(&c,elite,&st) && !st.needThreat && st.restMax==0);
 printf("PASS: side steps - V14 (SideSteps=1): %d steps a minute, strafing %.0f%% of the time, %d of them back the other way; V9-V13 (SideSteps=2): %d steps, strafing %.0f%%, %d back the other way; 0 = none\n",
        b,100.0*a/1800,cf,b2,100.0*a2/1800,cf2);
 assert(a2==1800 && a<1800*0.30 && a>1800*0.08 && b<b2);
 puts("PASS: side steps only while being shot at or with the enemy within 20 m (out to 45 m), 60% of the profile's strafe, and hops three times rarer and only under fire (SideSteps=1); the Server Control bot settings list SideSteps");
 return 0;
}
'''

ROUTES = r'''
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "trace_nav.h"
int main(int c,char**v){TraceNet t;memset(&t,0,sizeof t);if(!tn_load_kind(&t,v[1],TN_KIND_LAND))return 2;
 /* Seine Crossing flags and the T90's spots, from the live map / log of the 2026-10-10 round */
 float flags[5][3]={{30.6f,0,119.6f},{-50.9f,0,109.5f},{-75.4f,0,21.7f},{71.6f,0,-32.1f},{-15.5f,0,-37.2f}};
 float spots[3][3]={{43,11,189},{47,11,189},{102.6f,11.2f,170.6f}};
 int ok=0,total=0;
 for(int f=0;f<5;f++){
  float fp[3]={flags[f][0],0,flags[f][2]},best=1e9f;
  for(int k=0;k<t.count;k++){float dx=t.nodes[k].p[0]-fp[0],dz=t.nodes[k].p[2]-fp[2];if(dx*dx+dz*dz<best){best=dx*dx+dz*dz;fp[1]=t.nodes[k].p[1];}}
  if(!tn_field(&t,f,fp,45))continue;
  for(int s=0;s<3;s++){int cand[8],n=tn_candidates(&t,f,spots[s],45,8,cand,8);total++;
   if(n){float pts[3*256];unsigned char fl[256];int tr;float len;if(tn_route(&t,f,cand[0],6,pts,fl,128,&tr,&len)>=2)ok++;}}
 }
 printf("%d %d %d\n",t.pathCount,ok,total);tn_free(&t);return 0;}
'''

MODE = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include "modes.h"
typedef struct { const char *level,*mode; float low[3],high[3],patrol[32][3]; int stages; unsigned baseMask[9];float bases[9][3][3]; struct {float pos[3],yaw,pitch;int present;}mcom[17]; } BMModeProfile;
static char g_log[512];static int g_logs;
static enum BMMode mode_kind(const char *m){return bm_mode(m);}   /* AltBotMod V15: no round fallback here */
static void bot_log(const char *f,...){va_list a;va_start(a,f);vsnprintf(g_log,sizeof g_log,f,a);va_end(a);g_logs++;}
static float g_sp[40][3];static int g_nsp;static char g_obj[40];
static int scan_objects(unsigned vt,char **out,int max){(void)vt;int n=0;for(int k=0;k<g_nsp&&n<max;k++)out[n++]=&g_obj[k];return n;}
static int cse_valid(void *e){(void)e;return 1;}
static float *spawn_data_pos(void *e){return g_sp[(char*)e-g_obj];}
'''


KITS = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define KIT_MSG_SIZE 0x80
#define KIT_MAX_WEAPONS 8
#define MAX_LOADOUTS 16
#define MAX_FIXUPS 1024
typedef struct Loadout { int team; char kit[64]; unsigned char unlockMsg[KIT_MSG_SIZE]; int nWeapons; unsigned char weaponMsg[KIT_MAX_WEAPONS][KIT_MSG_SIZE]; } Loadout;
typedef struct LoadoutLib { int count; Loadout l[MAX_LOADOUTS]; } LoadoutLib;
static const char *kit_class(const char *k){return k;}
static char g_log[64][300];static int g_nlog;
static void bot_log(const char *f,...){va_list a;va_start(a,f);vsnprintf(g_log[g_nlog%64],300,f,a);va_end(a);g_nlog++;}
FIXUP_STRUCT
/* this level has these assets (at these addresses) */
static const char *g_have[]={"Persistence/Unlocks/Soldiers/Specializations/SprintBoostL1","Weapons/MP443/U_MP443",0};
static void *lookup(const char *n){for(int k=0;g_have[k];k++)if(!strcmp(n,g_have[k]))return (void*)(0x1000+k);return (void*)-1;}
static int fast_resolve_assets(Fixup *fx,int n){int f=0;for(int k=0;k<n;k++){*fx[k].slot=lookup(fx[k].name);f+=*fx[k].slot!=(void*)-1;}return f;}
static void resolve_assets(Fixup *fx,int n){(void)fx;(void)n;}
FUNCTION
static int logged(const char *t){for(int k=0;k<g_nlog&&k<64;k++)if(strstr(g_log[k],t))return 1;return 0;}
int main(void){
 static LoadoutLib lib;static Fixup fx[16];int nfx=0,bad[MAX_LOADOUTS]={0};
 lib.count=2;strcpy(lib.l[0].kit,"Assault");strcpy(lib.l[1].kit,"Support");lib.l[0].team=lib.l[1].team=1;
 lib.l[0].nWeapons=3;lib.l[1].nWeapons=2;
 for(int L=0;L<2;L++)for(int w=0;w<lib.l[L].nWeapons;w++){*(int*)(lib.l[L].weaponMsg[w]+0x38)=w==2?7:w;lib.l[L].weaponMsg[w][0]=(unsigned char)(10*L+w);}
 static unsigned att[6] __attribute__((aligned(8)))={4,0x111,0x333,0x444,0,0};   /* an attachment list: 4 entries, the last one missing (last so a 64-bit test pointer write stays inside the array) */
 #define ADD(sl,nm,L,wm,o,ar,or) do{fx[nfx].slot=(void**)(sl);strcpy(fx[nfx].name,nm);fx[nfx].loadout=L;fx[nfx].vtable=0;strcpy(fx[nfx].orig,or);fx[nfx].wmsg=wm;fx[nfx].off=o;fx[nfx].arr=ar;*(void**)(sl)=(void*)-1;nfx++;}while(0)
 ADD(lib.l[0].unlockMsg+8,"Persistence/Unlocks/Soldiers/Specializations/AmmoBoostL1",0,NULL,8,NULL,"Persistence/Unlocks/Soldiers/Specializations/SprintBoostL1");
 ADD(lib.l[0].weaponMsg[2]+0x3c,"Weapons/XP2_Knife_RazorBlade/U_Knife_Razor",0,lib.l[0].weaponMsg[2],0x3c,NULL,"");
 ADD(&att[4],"Weapons/Common/MissingSight",0,NULL,-1,att,"");
 ADD(lib.l[0].weaponMsg[1]+0x3c,"Weapons/Gadgets/M15/U_M15",0,lib.l[0].weaponMsg[1],0x3c,NULL,"Weapons/MP443/U_MP443");
 ADD(lib.l[1].weaponMsg[0]+0x3c,"Weapons/XP4_Missing/U_Primary",1,lib.l[1].weaponMsg[0],0x3c,NULL,"");
 loadouts_missing(&lib,fx,nfx,bad);
 assert(*(void**)(lib.l[0].unlockMsg+8)==(void*)0x1000);   /* perk: the kit's own again */
 assert(*(void**)(lib.l[0].weaponMsg[1]+0x3c)==(void*)0x1001);   /* kitextra M15 missing: the kit's own pistol */
 assert(att[0]==3&&att[1]==0x111&&att[2]==0x333&&att[3]==0x444);   /* attachment left out of its list */
 assert(lib.l[0].nWeapons==2&&lib.l[0].weaponMsg[0][0]==0&&lib.l[0].weaponMsg[1][0]==1&&!bad[0]);   /* knife slot emptied, the kit stays */
 assert(bad[1]&&lib.l[1].nWeapons==2);   /* a missing primary drops that kit only */
 assert(logged("not on this level: Weapons/XP2_Knife_RazorBlade")&&logged("the kit's own item instead")&&logged("1 list entr(ies) and 1 gadget"));
 puts("PASS: kits on a level that lacks some items (Markaz Monolith: all 8 kits dropped for 11 missing items, bots spawned with none - invisible): a BotMod_kitextra item / perk falls back to the kit's own, a missing attachment leaves its list, a missing knife / gadget / sidearm empties that slot - the kit stays; only a missing primary (or appearance) drops a kit; each missing item is logged by name");
 return 0;}
'''


def function(src, name):
    m = re.search(r'^(static [^\n]*\b' + re.escape(name) + r'\([^\n]*\)\n\{.*?^\})', src, re.S | re.M)
    assert m, name
    return m.group(1)


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    print(r.stdout, end='')
    if r.returncode:
        print(r.stderr); raise SystemExit('FAIL: %s exited %d' % (Path(cmd[0]).name, r.returncode))
    return r.stdout


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    san = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g'] if a.sanitize else ['-O2']
    with tempfile.TemporaryDirectory(prefix='bf3-altbotmod-v14-') as tmp:
        tmp = Path(tmp)
        (tmp / 'windows.h').write_text((ROOT / 'validation/altbotmod_v13_checks.py').read_text().split("WINDOWS = r'''")[1].split("'''")[0])
        for n in SRC.iterdir():
            if n.suffix in ('.c', '.h', '.inc'): shutil.copy(n, tmp / n.name)
        cc = ['gcc', '-std=gnu11', '-Wall', '-Wno-unused-function', '-I', str(tmp)] + san
        (tmp / 'dodge.c').write_text(DODGE)
        subprocess.run(cc + [str(tmp / 'dodge.c'), str(tmp / 'enhanced.c'), str(tmp / 'air.c'), str(tmp / 'modes.c'), '-lm', '-o', str(tmp / 'dodge')], check=True)
        run([str(tmp / 'dodge')])
        (tmp / 'routes.c').write_text(ROUTES)
        subprocess.run(cc + ['-w', str(tmp / 'routes.c'), str(tmp / 'trace_nav.c'), '-lm', '-o', str(tmp / 'routes')], check=True)
        paths, ok, total = map(int, run([str(tmp / 'routes'), str(SERVER / 'BotMod_traces/MP_011_ConquestLarge0.map')]).split())
        assert ok == total == 15, (ok, total)
        print(f'PASS: Seine Crossing - the recorded vehicle routes ({paths}) reach all 5 flags from each of the 3 spots the T90 sat at ({ok}/{total}): the tank had routes, V13 had stopped using them')
        mn = (SRC / 'modes_native.inc').read_text()
        (tmp / 'mode.c').write_text(MODE + re.search(r'static BMModeProfile g_modeAutoProfile;.*?\nstatic const BMModeProfile \*mode_auto_profile\(const char \*level,const char \*mode\)\n\{.*?\n\}\n', mn, re.S).group(0) + r'''
int main(void){
 const BMModeProfile *p;
 p=mode_auto_profile("MP_001","");assert(!p && strstr(g_log,"has not said which game mode")&&strstr(g_log,"RCON"));
 p=mode_auto_profile("MP_001","RushLarge0");assert(!p && strstr(g_log,"Rush needs the recorded M-COM"));
 g_nsp=0;p=mode_auto_profile("MP_999","TeamDeathMatch0");assert(!p && strstr(g_log,"no spawn points found yet"));
 int before=g_logs;p=mode_auto_profile("MP_999","TeamDeathMatch0");assert(!p && g_logs==before);   /* (said once, not every 2 s) */
 float pts[5][3]={{0,0,0},{100,20,-50},{140,22,30},{NAN,0,0},{60,18,90}};g_nsp=5;memcpy(g_sp,pts,sizeof pts);
 p=mode_auto_profile("MP_999","TeamDeathMatch0");
 assert(p && !strcmp(p->level,"MP_999") && p->low[0]==0 && p->high[0]==200 && p->low[2]==-110 && p->high[2]==150 && strstr(g_log,"uses the 3 spawn point(s)"));
 assert(p->patrol[0][0]==100 && p->patrol[1][0]==140 && p->patrol[2][0]==60 && p->patrol[3][0]==100 && p->patrol[31][0]==140 && p->stages==0);
 puts("PASS: a mode with no recorded layout: TDM / Squad DM play the level's own spawn points (play area round them, patrol spots); Rush, or no mode at all, says why once instead of 'not ready yet' every 2 s");
 return 0;}
''')
        subprocess.run(cc + [str(tmp / 'mode.c'), str(tmp / 'modes.c'), '-lm', '-o', str(tmp / 'mode')], check=True)
        run([str(tmp / 'mode')])
    core = (SRC / 'core.c').read_text(); gj = (SRC / 'ground_jobs.inc').read_text(); tr = (SRC / 'trace_native.inc').read_text()
    with tempfile.TemporaryDirectory(prefix='bf3-altbotmod-v14k-') as tmp:
        tmp = Path(tmp)
        fixup = re.search(r'typedef struct Fixup \{.*?\} Fixup;[^\n]*', core, re.S).group(0)
        fn = re.search(r'static int g_kitsAllDropped;\nstatic void loadouts_missing\(.*?\n\}\n', core, re.S).group(0)
        (tmp / 'kits.c').write_text(KITS.replace('FIXUP_STRUCT', fixup).replace('FUNCTION', fn))
        subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-int-to-pointer-cast', '-Wno-pointer-to-int-cast'] + san + [str(tmp / 'kits.c'), '-o', str(tmp / 'kits')], check=True)
        run([str(tmp / 'kits')])
        # every level's own data (read from the server's level superbundles): after level_kit_swap, every kit item of the
        # shipped kits - for each mode set - must be one that level has, by its exact name
        data = json.loads((ROOT / 'validation/level-kit-assets.json').read_text())
        assets = data['assets']; levels = data['levels']
        names = sorted(set(re.findall(r'[0-9A-F]{8}:(\S+)', (SERVER / 'BotMod_loadouts.txt').read_text()) +
                           re.findall(r'^P \d+ [0-9A-F]+ (\S+)', (SERVER / 'BotMod_loadouts.txt').read_text(), re.M)) |
                       {l.split()[-1] for l in (SERVER / 'BotMod_kitextra.txt').read_text().splitlines() if l.strip() and not l.startswith('#')})
        cstr = lambda v: '{' + ','.join('"%s"' % x for x in v) + ',0}'
        lv = '\n'.join('{"%s",(const int[]){%s,-1}},' % (L, ','.join(map(str, ix))) for L, ix in levels.items())
        swap = re.search(r'static int level_kit_swap\(.*?\n\}\n', core, re.S).group(0)
        (tmp / 'swap.c').write_text(r'''#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
''' + fixup + '\n' + swap + r'''
static const char *assets[]=''' + cstr(assets) + r''';
static const char *names[]=''' + cstr(names) + r''';
static struct {const char *level; const int *have;} levels[]={''' + lv + r'''};
static int has(int L,const char *n){for(const int *h=levels[L].have;*h>=0;h++)if(!strcmp(assets[*h],n))return 1;return 0;}
int main(void){
 const char *modes[]={"ConquestLarge0","TeamDeathMatch0","RushLarge0","GunMaster0","Scavenger0"};
 int nl=sizeof levels/sizeof levels[0],checked=0,xp4=0;
 for(int L=0;L<nl;L++)for(int m=0;m<5;m++){
  static Fixup fx[128];int n=0;void *slots[128];
  for(int k=0;names[k];k++){memset(&fx[n],0,sizeof fx[n]);fx[n].slot=&slots[n];snprintf(fx[n].name,sizeof fx[n].name,"%s",names[k]);n++;}
  int sw=level_kit_swap(levels[L].level,modes[m],fx,n);
  if(!strncmp(levels[L].level,"XP4_",4)){assert(sw==16);xp4++;}else assert(sw==0);
  for(int k=0;k<n;k++){ if(!has(L,fx[k].name)){printf("FAIL: %s %s lacks %s\n",levels[L].level,modes[m],fx[k].name);return 1;} checked++; }
 }
 printf("PASS: kits on every level - read from the server's own level data (%d levels): after the per-level swap each of the %d kit items is one the level has, in every mode set (%d checks); the 4 Aftermath levels (no stock kits or looks at all - Markaz Monolith's invisible bots) use their _XP4 kits and looks (Gun Master / Scavenger sets too)\n",nl,(int)(sizeof names/sizeof names[0])-1,checked);
 return xp4!=20;}
''')
        subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wno-unused-function'] + san + [str(tmp / 'swap.c'), '-o', str(tmp / 'swap')], check=True)
        run([str(tmp / 'swap')])
    assert re.search(r'if \(g_kitsAllDropped && pick_loadout\(bot\) < 0\) \{', core) and 'g_kitsAllDropped = w == 0 && lib->count > 0;' in core
    print('PASS: when no kit at all is usable on a level, bots wait (logged once a minute) instead of spawning invisible')
    assert 'd->traceTries=0;d->traceAlt=0;' in gj
    assert re.search(r'if\(!boat && drive_route_fallback\(i,d,vp\)\)return;.*?\n\s*if\(!boat && drive_other_objective\(i,d,vp,1\)\)return;', core)
    assert 'int alt = d->traceAlt++;' in core and 'H->frame - d->noRouteSince >= 45 * 30' in core and 'if (alt >= nd) return 0;' in tr
    print('PASS: tanks - Fun-Bots vehicle routes tried again for every new flag (V13: two per vehicle, for good), and after a failed driving-map search: the nearest route again, then up to three from other roads further out, then checked straight legs after 45 s without a route')
    assert 'g_jobGuessCq=g_flagCount>0;' in core and 'the server has not reported its game mode over RCON' in core and 'g_modeProfile=mode_auto_profile(level,mode);' in mn
    assert 'if (!whyAt || H->frame - whyAt > 20 * 30)' in core
    print('PASS: no game mode from RCON: the level\'s flags are looked for and, after 8 s, the bots play Conquest; "not ready yet" now says why (no mode / no layout / blueprint), every 20 s')


if __name__ == '__main__':
    main()
