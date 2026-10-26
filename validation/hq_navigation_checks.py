"""Native HQ discovery and background navigation integration, using engine mocks.
32-bit pointer fields are widened/repositioned in the host-only CSE fixture.
"""
from pathlib import Path
import argparse,re,subprocess,tempfile
from test4_checks import function
from nav_queue_checks import WINDOWS
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src';core=(SRC/'core.c').read_text()
HQ=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#define MAX_FLAGS 8
#define OFF_CP_OWNER_OBJECT 32
#define OFF_CP_DATA 24
#define OFF_CP_POSITION 64
#define OFF_CP_OWNER_TEAM 80
#define OFF_CPDATA_TAKEABLE 16
#define OFF_CSEDATA_TEAM 0x6C   /* AltBotMod V16: CharacterSpawnReferenceObjectData.Team */
#define ADDR_CAPTURE_POINT_VTABLE 999
static struct{unsigned frame;struct{void *entity;unsigned a2,a3,a4;}teamSpawn[3];}host,*H=&host;
static char cp[4][128] __attribute__((aligned(8))),cpData[4][128] __attribute__((aligned(8)));
static char spawn[10][128] __attribute__((aligned(8))),data[10][128] __attribute__((aligned(8)));
static int ncp,nsp,g_hqCandCount;static unsigned char g_hqCandTried[3][16];
static float g_hqPos[3][3],g_hqCpPos[3][3];static int g_hqCpKnown[3];static char g_modeName[64];static void *g_fleetBase[3];
static int IsBadReadPtr(void*p,int n){(void)n;return !p;}static void bot_log(const char*f,...){(void)f;}
static float dist2(const float*a,const float*b){float x=a[0]-b[0],z=a[2]-b[2];return x*x+z*z;}
static int scan_objects(unsigned vt,char **out,int cap){int n=vt==999?ncp:nsp;for(int k=0;k<n&&k<cap;k++)out[k]=vt==999?cp[k]:spawn[k];return n<cap?n:cap;}
'''+function(core,'spawn_team').replace('+ 0x14','+ 0x18')+'\n'+function(core,'cse_valid').replace('+ 0x14','+ 0x18')+'\n'+(SRC/'hq_spawns.inc').read_text()+'\n'+function(core,'find_hq_spawns').replace('+0x14','+0x18')+r'''
static void init(void){
 memset(&host,0,sizeof host);memset(cp,0,sizeof cp);memset(cpData,0,sizeof cpData);memset(spawn,0,sizeof spawn);memset(data,0,sizeof data);
 ncp=3;nsp=6;H->frame=100;strcpy(g_modeName,"ConquestLarge0");
 for(int k=0;k<ncp;k++){*(char**)(cp[k]+24)=cpData[k];*(void**)(cp[k]+32)=(void*)(size_t)(100+k);*(int*)(cp[k]+80)=k+1;((float*)(cp[k]+64))[0]=k==0?100:k==1?-100:1;}
 cpData[2][16]=1; // a capturable point never qualifies as deployment
 for(int k=0;k<nsp;k++){*(unsigned*)spawn[k]=0x025bbbc0;*(char**)(spawn[k]+24)=data[k];*(unsigned*)data[k]=0x2501000;}
 *(int*)(data[0]+0x6C)=2;*(void**)(spawn[0]+32)=(void*)100;((float*)(data[0]+64))[0]=100; // opposite child first
 *(int*)(data[1]+0x6C)=1;*(void**)(spawn[1]+32)=(void*)100;((float*)(data[1]+64))[0]=100;
 *(int*)(data[2]+0x6C)=2;*(void**)(spawn[2]+32)=(void*)777;((float*)(data[2]+64))[0]=-100; // explicit RU, separate owner beside HQ
 *(int*)(data[3]+0x6C)=0;*(void**)(spawn[3]+32)=(void*)100;((float*)(data[3]+64))[0]=101; // linked neutral alternate
 *(int*)(data[4]+0x6C)=2;*(void**)(spawn[4]+32)=(void*)102;((float*)(data[4]+64))[0]=-90; // capturable spawn, reject
 *(int*)(data[5]+0x6C)=0;*(void**)(spawn[5]+32)=(void*)888;((float*)(data[5]+64))[0]=-90; // untyped untied, reject
}
int main(void){
 init();float lo[3]={1e9f,0,1e9f},hi[3]={-1e9f,0,-1e9f};assert(find_hq_spawns(lo,hi)==3);
 assert(H->teamSpawn[1].entity==spawn[1] && H->teamSpawn[2].entity==spawn[2]);
 assert(hq_is_option(1,spawn[1]) && hq_is_option(2,spawn[2]) && !hq_is_option(1,spawn[2]));
 assert(g_hqOptionCount[1]==3 && g_hqOptionCount[2]==2 && g_hqCpKnown[1] && g_hqCpKnown[2]);
 puts("PASS: native discovery chooses both team HQs without humans, rejects enemy/shared-owner children and captured/untyped points, accepts explicit-team child beside its known HQ");
 g_fleetBase[1]=spawn[1];hq_spawn_refused(1,spawn[1]);assert(H->teamSpawn[1].entity==spawn[3] && !g_fleetBase[1]);
 puts("PASS: refused HQ selects another validated native child for the NEXT attempt; no probe soldier or forced death");
 // A unique RU non-capturable parent remains authoritative when its child's
 // byte is generic US (the exact filter regression affecting both-team HQs).
 init();*(void**)(spawn[2]+32)=(void*)101;*(int*)(data[2]+0x6C)=1;*(int*)(data[5]+0x6C)=1;
 find_hq_spawns(lo,hi);assert(H->teamSpawn[2].entity==spawn[2] && hq_select_option(2));
 // Shared parents DO require team/position discrimination.
 init();*(void**)(cp[1]+32)=(void*)100;
 find_hq_spawns(lo,hi);assert(H->teamSpawn[1].entity==spawn[1] && H->teamSpawn[2].entity==spawn[0]);
 puts("PASS: unique native HQ owner overrides a generic child team byte for RU; shared-owner children retain explicit team discrimination");
 // Regression: both teams have HQ anchors; RU's actual CSE is neutral and
 // unlinked. Test11 rejected it, so scoreboard bots had no soldiers until a
 // human switched sides. No human spawn or pre-populated teamSpawn here.
 init();*(int*)(data[2]+0x6C)=0;*(int*)(data[5]+0x6C)=1;
 find_hq_spawns(lo,hi);assert(g_hqCpKnown[2] && H->teamSpawn[2].entity==spawn[2]);
 puts("PASS: neutral unlinked CQ HQ child near its own native anchor enables RU without any human, including when RU owns zero flags");
 // An explicit RU child tied to a different non-capturable owner is valid;
 // tying it to a capturable flag still rejects it.
 init();*(void**)(spawn[2]+32)=(void*)100;*(int*)(data[5]+0x6C)=1;
 find_hq_spawns(lo,hi);assert(H->teamSpawn[2].entity==spawn[2]);
 init();*(void**)(spawn[2]+32)=(void*)102;*(int*)(data[5]+0x6C)=1;
 find_hq_spawns(lo,hi);assert(!H->teamSpawn[2].entity);
 init();*(int*)(data[2]+0x6C)=0;*(int*)(data[5]+0x6C)=1;((float*)(data[2]+64))[0]=100;
 find_hq_spawns(lo,hi);assert(!H->teamSpawn[2].entity);
 puts("PASS: captured-flag and enemy-side neutral children remain excluded; failure retains HQ anchor without sacrificial probe spawns");
 /* AltBotMod V22: Office Domination (2026-10-17) - RU's HQ anchor had no deployment beside it and RU never spawned. Outside
    Conquest the team with an HQ but no matched deployment tries the level's team-less / own entities, the other team's last. */
 init();strcpy(g_modeName,"Domination0");*(int*)(data[2]+0x6C)=0;*(int*)(data[5]+0x6C)=1;((float*)(data[2]+64))[0]=100;
 find_hq_spawns(lo,hi);assert(H->teamSpawn[2].entity && H->teamSpawn[2].entity!=H->teamSpawn[1].entity && spawn_team(H->teamSpawn[2].entity)!=1);
 puts("PASS: V22 - outside Conquest a team with an HQ but no matched deployment gets the level's team-less entities to try (Conquest keeps flags only)");return 0;
}
'''
# Run real nav.c plus real navigation_extend/nav_tick; callbacks model flat
# terrain. The already playable grid must survive every expansion build slice.
NAV=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "nav.c"
#define MAX_PATH 256
#define NAV_RAYS_PER_FRAME 32
#define NAV_MS_PER_FRAME .1
static struct{unsigned frame;}host,*H=&host;
static NavGrid *active,*g_navExpansion;static int building,links;
static char g_level[64]="MP_017";static const char *savedPath;
static NavGrid **nav_slot(void){return &active;}static int *nav_building(void){return &building;}
static int mode_is_cq(void){return 1;}static int __attribute__((unused)) mode_uses_vehicles(void){return mode_is_cq();}static void bot_log(const char*f,...){(void)f;}static void chat_say(const char*f,...){(void)f;}
static void nav_stairfix_tick(void){}static int nav_hit_class(void){return 0;}static void ladders_link(void){links++;}
static void nav_file(char*out,int cap,const char*level){(void)level;snprintf(out,cap,"%s",savedPath);}
static int ray_cast(const float*a,const float*b,float*h,float*n){
 if(a[1]>=0 && b[1]<=0 && a[1]>b[1]){if(h){h[0]=a[0];h[1]=0;h[2]=a[2];}if(n){n[0]=n[2]=0;n[1]=1;}return 1;}return 0;
}
static int ray_cast_no_terrain(const float*a,const float*b,float*h,float*n){(void)a;(void)b;(void)h;(void)n;return 0;}
'''+function(core,'navigation_extend')+'\n'+function(core,'nav_tick')+r'''
int main(int argc,char**argv){
 assert(argc==3);ray_cost=0;savedPath=argv[1];active=nav_create("MOCK",0,0,20,20,2);assert(active);
 while(active->phase!=2)nav_build_step(active,ray_cast,1000,1);
 NavGrid *old=active;float low[3]={-20,0,-20},high[3]={40,0,40};
 active=navigation_extend(active,low,high);assert(active==old && active->phase==2 && g_navExpansion && !building);
 float out[32*3];int ticks=0;
 while(g_navExpansion && ticks++<20000){
  assert(active==old && active->phase==2);
  nav_set_path_owner(-1);assert(nav_find_path(active,ray_cast,3,0,3,15,0,15,out,32)>0);
  H->frame++;nav_tick();
 }
 assert(!g_navExpansion && active!=old && active->phase==2 && links==1 && ticks>1);
 assert(nav_covers(active,-15,-15,0));nav_free(active);active=NULL;
 puts("PASS: actual navigation_extend -> nav_tick keeps existing routes usable through every incremental survey tick and swaps only a completed grid");
 // Actual shipped Noshahr cache and shipped HQ coordinates, without pretending
 // that the flat terrain used above is Noshahr's physics.
 NavGrid *canals=nav_load(argv[2]);assert(canals && !strcmp(canals->level,"MP_017"));
 float hqs[2][3]={{-229.6f,85.5f,557.5f},{-808.9f,73.6f,-68.1f}},node[3];
 for(int t=0;t<2;t++){
  assert(nav_covers(canals,hqs[t][0],hqs[t][2],0));int reachable=nav_reach(canals,hqs[t][0],hqs[t][1],hqs[t][2],node);
  assert(reachable>20 && fabsf(node[1]-hqs[t][1])<5);printf("PASS: shipped Noshahr team %d HQ is covered; %d reachable nav nodes at its layer (cache inspection, not a live map test)\n",t+1,reachable);
 }
 nav_free(canals);return 0;
}
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--sanitize',action='store_true');a=ap.parse_args()
 with tempfile.TemporaryDirectory(prefix='bf3-hq-navigation-') as tmp:
  tmp=Path(tmp);(tmp/'windows.h').write_text(WINDOWS)
  for name,body in [('hq',HQ),('nav',NAV)]:
   f=tmp/('check_'+name+'.c');f.write_text(body);exe=f.with_suffix('')
   cmd=['gcc','-std=c11','-Wall','-Wextra','-Werror','-g','-I',str(tmp),'-I',str(SRC),str(f),'-lm','-o',str(exe)]
   if a.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-sanitize-recover=all']
   subprocess.run(cmd,check=True);subprocess.run([str(exe),str(tmp/'expanded.bin'),str(ROOT/'files/server/BotMod_nav_MP_017.bin')],check=True)
if __name__=='__main__':main()
