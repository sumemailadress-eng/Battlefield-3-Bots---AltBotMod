"""Caspian V2: Fun-Bots routes, zombie removal, aggression, flag retaking and
abandoned vehicles. Compiles the ACTUAL production sources (trace_nav.c,
trace_native.inc, removal_tick, director_policy.h, abandoned_dispatch.inc,
enhanced.c/tactics_policy.inc) against mocked engine state, and loads the real
Fun-Bots map files shipped in files/server/BotMod_traces. Not a live BF3 test."""
from pathlib import Path
import argparse, os, re, subprocess, tempfile
from test4_checks import function
import test4_checks as t4
import test15_regressions as t15
import combat_mobility_checks as cm
ROOT = Path(__file__).resolve().parents[1]; SRC = ROOT/'files/BotMod/src'; TRACES = ROOT/'files/server/BotMod_traces'

GRAPH = r'''
#include "trace_nav.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static float hd(const float*a,const float*b){float x=a[0]-b[0],z=a[2]-b[2];return sqrtf(x*x+z*z);}
int main(int argc,char**argv){
 int maps=0;
 for(int a=1;a<argc;a++){
  TraceNet t;assert(tn_load(&t,argv[a]));maps++;
  assert(t.count>1000 && t.edgeCount>t.count && t.pathCount>10);
  // One big walkable network (>=95% of waypoints connected).
  int *comp=malloc(t.count*sizeof*comp),*st=malloc(t.count*sizeof*st),big=0,bigId=0,nc=0;
  for(int k=0;k<t.count;k++)comp[k]=-1;
  for(int k=0;k<t.count;k++)if(comp[k]<0){int sp=0,sz=0;st[sp++]=k;comp[k]=nc;while(sp){int n=st[--sp];sz++;
   for(int e=t.edgeStart[n];e<t.edgeStart[n+1];e++){int m=t.edgeTo[e];assert(m>=0&&m<t.count);if(comp[m]<0){comp[m]=nc;st[sp++]=m;}}}
   if(sz>big){big=sz;bigId=nc;}nc++;}
  // Most maps are one network; a few (e.g. MP_011/XP4_FD Conquest Small) have separate recorded areas,
  // where the bot falls back to the grid between them.
  assert(big*100>=t.count*40);
  if(strstr(t.name,"MP_007_ConquestLarge0")||strstr(t.name,"MP_012_ConquestLarge0")||strstr(t.name,"XP1_002_ConquestLarge0")||strstr(t.name,"MP_Subway"))assert(big*100>=t.count*95);
  // Fields toward 8 goals spread over the main network: routes are continuous,
  // made of recorded waypoints, end at the goal, and never grow longer than the graph.
  for(int g=0;g<8;g++){
   int goal=-1;for(int k=(t.count/9)*(g+1);k<t.count;k++)if(comp[k]==bigId){goal=k;break;}
   assert(goal>=0);int id=g%TN_MAX_FIELDS;assert(tn_field(&t,id,t.nodes[goal].p,20));
   int checked=0;
   for(int s=0;s<t.count && checked<40;s+=t.count/40+1){
    if(comp[s]!=bigId)continue;checked++;
    assert(isfinite(t.field[id].dist[s]));
    float pts[128*3];unsigned char fl[128];int tr;float len;
    int n=tn_route(&t,id,s,4.0f,pts,fl,128,&tr,&len);assert(n>=1 && n<=128);
    for(int k=1;k<n;k++)assert(hd(&pts[(k-1)*3],&pts[k*3])<40.0f);
    if(!tr)assert(hd(&pts[(n-1)*3],t.nodes[goal].p)<=20.5f);
    // every point is a recorded waypoint
    for(int k=0;k<n;k+=7){int found=0;for(int m=0;m<t.count&&!found;m++)found=t.nodes[m].p[0]==pts[k*3]&&t.nodes[m].p[2]==pts[k*3+2];assert(found);}
   }
   int c[6];int nc2=tn_candidates(&t,id,t.nodes[goal].p,30,4,c,6);assert(nc2>=1);
  }
  printf("  %s: %d waypoints, %d paths, %d vehicle paths excluded, %d%% in one network\n",t.name,t.count,t.pathCount,t.vehiclePaths,big*100/t.count);
  free(comp);free(st);tn_free(&t);
 }
 // Malformed input is rejected without crashing.
 TraceNet bad;const char *junk="pathIndex;pointIndex\nxx;yy;zz\n1;2;3\n";assert(!tn_load_text(&bad,junk,strlen(junk)));
 const char *tiny="h\n1;1;0;0;0;3;\n1;2;3;0;0;3;{\"Links\":[[1,1]]}\n2;1;0;0;3;3;{\"Vehicles\":[\"land\"]}\n2;2;0;0;6;3;\n";
 assert(tn_load_text(&bad,tiny,strlen(tiny)) && bad.count==2 && bad.vehiclePaths==1);tn_free(&bad);
 printf("PASS: all %d Fun-Bots Conquest route files load (Caspian Border, Firestorm, Kharg, Metro >=95%% one network); per-flag route fields give continuous recorded-waypoint routes that end at the flag; vehicle-only paths excluded; malformed files rejected\n",maps);
 return 0;
}
'''

PLAN = r'''
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "enhanced.h"
#include "trace_nav.h"
#define MAX_BOTS 4
#define MAX_PATH 260
#define PATH_MAX_POINTS 128
#define OFF_PLAYER_SOLDIER 16
#define OFF_PLAYER_ONLINE_NAME 64
#define OFF_SOLDIER_POSITION 0
typedef struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; int traceInput; } Host;
static Host host,*H=&host;
static BMConfig g_enhanced;
''' + '%BOTPATH%' + r'''
static BotPath g_paths[MAX_BOTS];
typedef struct { int mode; } BotNav; static BotNav navs[MAX_BOTS]; static BotNav *bot_nav(int i){return &navs[i];}
static int g_flagCount; static float flagP[16][3]; static float *flag_pos(int f){return flagP[f];}
static char g_level[32]="MP_007", g_modeName[32]="TankSuperiority0";   /* (V15: Rush has its own file now - a mode with none) */
static float dist2(const float *a,const float *b){float x=a[0]-b[0],z=a[2]-b[2];return x*x+z*z;}
static int blocked,presses,rays,g_lastPlanDeferred;
static int ray_cast(const float *a,const float *b,float *h,float *n){(void)a;(void)b;(void)h;(void)n;return 0;}
static int nav_segment_clear(int(*r)(const float*,const float*,float*,float*),const float *a,const float *b){(void)r;(void)a;(void)b;rays++;return !blocked;}
static void press_button(int i,int a,int f){(void)i;(void)a;(void)f;presses++;}
static int cancels;static void nav_cancel_path(int i){(void)i;cancels++;}
static void bot_log(const char *f,...){(void)f;}
static const char *g_exe;
static unsigned GetModuleFileNameA(void *m,char *out,unsigned n){(void)m;snprintf(out,n,"%s",g_exe);return 1;}
/* AltBotMod V6: the grid walk onto a Fun-Bots route - no grid in this harness */
typedef struct { int phase; } NavGrid; static NavGrid *g_noGrid; static NavGrid **nav_slot(void){return &g_noGrid;}
static int budgeted_nav_path(int o,const NavGrid *g,int(*r)(const float*,const float*,float*,float*),float a,float b,float c,float d,float e,float f,float *out,int m){(void)o;(void)g;(void)r;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)out;(void)m;return 0;}
static int nav_last_partial(void){return 0;} static int nav_path_link(int k){(void)k;return -1;}
/* AltBotMod V15 route actions (altbotmod_v15_checks) */
static void bot_stand(int i){(void)i;}static void route_actions_level(void){}static void route_actions_reset(void){}static void route_actions_stats(void){}
static int route_board_spot(const float *v){(void)v;return -1;}static void route_wait_reset(int i){(void)i;}
static int plan_budget_left(void){return 1;}   /* AltBotMod V18.1: the grid join tries 3 route waypoints within the plan budget */
#include "trace_native.inc"
int main(int argc,char **argv){
 g_exe=argv[1];bm_defaults(&g_enhanced);
 static char player[128];static float body[4];H->bots[0]=player;H->botCount=1;*(float**)(player+16)=body;strcpy(player+64,"bot");
 // No file for this exact mode: another mode of the same level is used.
 trace_level_load();assert(g_trace.count>1000 && strstr(g_trace.name,"MP_007_ConquestLarge0"));
 // A flag on the main network and a start >500 m away that can reach it.
 int far=g_trace.count/2,start=-1;
 assert(tn_field(&g_trace,0,g_trace.nodes[far].p,TRACE_FLAG_RADIUS));
 for(int k=0;k<g_trace.count;k++)if(isfinite(g_trace.field[0].dist[k]) && dist2(g_trace.nodes[k].p,g_trace.nodes[far].p)>500.0f*500.0f){start=k;break;}
 assert(start>=0);
 g_flagCount=2;memcpy(flagP[0],g_trace.nodes[far].p,12);memcpy(flagP[1],g_trace.nodes[start].p,12);flagP[1][0]+=1000;
 memcpy(body,g_trace.nodes[start].p,12);body[0]+=2;
 H->frame=1000;
 int n=trace_plan(0,(char*)body,flagP[0][0]+5,flagP[0][1],flagP[0][2]);
 assert(n>=2 && g_paths[0].count==n && g_paths[0].partial && g_paths[0].next==0 && g_paths[0].goalX==flagP[0][0]+5);
 for(int k=0;k<n;k++)assert(g_paths[0].link[k]==-1);
 // The route heads for the flag (its last point is much closer than the start).
 assert(dist2(&g_paths[0].pts[(n-1)*3],flagP[0])<dist2(body,flagP[0])*0.5f);
 // Same spot, same flag, soon after: AltBotMod V22 - Fun-Bots routes first: the route is taken again (from another way
 // onto the network) twice, then the grid gets one turn (no loop on a bad join).
 assert(trace_plan(0,(char*)body,flagP[0][0],flagP[0][1],flagP[0][2])>=2 && g_traceFallbacks==0);
 assert(trace_plan(0,(char*)body,flagP[0][0],flagP[0][1],flagP[0][2])>=2 && g_traceFallbacks==0);
 assert(!trace_plan(0,(char*)body,flagP[0][0],flagP[0][1],flagP[0][2]) && g_traceFallbacks==1);
 // Short trips, targets away from flags, blocked joins and the switch off: grid.
 float near[3];memcpy(near,body,12);near[0]+=12;assert(!trace_plan(0,(char*)body,near[0],near[1],near[2]));   /* (V22: routes from 15 m) */
 assert(!trace_plan(0,(char*)body,flagP[0][0]+300,flagP[0][1],flagP[0][2]+300));
 // With every join ray blocked, only a waypoint within 1.5 m (no ray needed) may be used; at most 5 rays are cast (AltBotMod V6: 3 -> 5).
 H->frame+=30*30;blocked=1;rays=0;n=trace_plan(0,(char*)body,flagP[0][0],flagP[0][1],flagP[0][2]);
 assert(rays<=5 && (!n || dist2(g_paths[0].pts,body)<=1.5f*1.5f));
 body[0]+=40;body[2]+=40;H->frame+=30*30;rays=0;n=trace_plan(0,(char*)body,flagP[0][0],flagP[0][1],flagP[0][2]);
 assert(rays<=5 && (!n || dist2(g_paths[0].pts,body)<=1.5f*1.5f));body[0]-=40;body[2]-=40;blocked=0;
 g_enhanced.funBotsRoutes=0;assert(!trace_plan(0,(char*)body,flagP[0][0],flagP[0][1],flagP[0][2]));g_enhanced.funBotsRoutes=1;
 // Recorded jumps press jump only near a higher jump point.
 navs[0].mode=3;g_paths[0].next=0;g_paths[0].count=2;g_paths[0].wpFlags[0]=TN_FLAG_JUMP;
 memcpy(g_paths[0].pts,body,12);g_paths[0].pts[1]+=1;g_paths[0].pts[0]+=1;H->frame=1000-0;presses=0;
 for(int k=0;k<10;k++){H->frame++;trace_jump_tick(0,(char*)body);}assert(presses==1);
 g_paths[0].pts[1]=body[1];presses=0;for(int k=0;k<10;k++){H->frame++;trace_jump_tick(0,(char*)body);}assert(!presses);
 // A level with no file: grid only, no crash.
 strcpy(g_level,"MP_999");trace_level_load();assert(!g_trace.count && !trace_plan(0,(char*)body,flagP[0][0],flagP[0][1],flagP[0][2]));
 puts("PASS: actual trace_plan gives a hand-walked Fun-Bots route to a far flag (any mode of the level), falls back to the grid for short trips, blocked joins, repeats and FunBotsRoutes=0; recorded jumps press jump");
 return 0;
}
'''

ZOMBIE = r'''
int main(void){
 memcpy(context+0x10,&object,sizeof object);H->frame=100;H->botCount=1;g_roundCleanup=0;
 static char pl[128];snprintf(pl+64,40,"stuck bot");H->bots[0]=pl;H->botInput[0]=malloc(4);*(void**)(pl+16)=(void*)1;
 g_silentRemove[0]=1;queue_bot_removal(0);
 for(int k=0;k<5*30-1;k++){H->frame++;removal_tick();}assert(!kills && H->botCount==1);   // kick first
 for(int k=0;k<30;k++){H->frame++;removal_tick();}assert(kills>=1 && H->botCount==1);     // then the live soldier is killed
 *(void**)(pl+16)=NULL;removal_tick();assert(!H->botCount && deleted==1);                 // and the identity replaced
 // Round cleanup stays kick-only.
 kills=0;static char p2[128];snprintf(p2+64,40,"round bot");H->bots[0]=p2;H->botCount=1;H->botInput[0]=malloc(4);*(void**)(p2+16)=(void*)1;
 g_roundCleanup=1;queue_bot_removal(0);for(int k=0;k<600;k++){H->frame++;removal_tick();}assert(!kills);
 puts("PASS: a retired bot whose kick does not unspawn it is killed after 5 s and replaced (Test15 left it as a brain-less zombie); round cleanup never kills");
 return 0;
}
'''

POLICY = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "enhanced.h"
#include "director_policy.h"
int main(void){
 BMConfig c;bm_defaults(&c);assert(c.aggression==1 && c.funBotsRoutes==1);
 assert(bm_set(&c,"Aggression","2")==1 && c.aggression==2 && bm_set(&c,"Aggression","3")==-1 && c.aggression==2);
 assert(bm_set(&c,"FunBotsRoutes","0")==1 && !c.funBotsRoutes);
 BMConfig legacy;bm_defaults(&legacy);legacy.aggression=0;BMConfig aggro;bm_defaults(&aggro);
 BMConfig brutal;bm_defaults(&brutal);brutal.aggression=2;
 const BMWeaponProfile *rifle=bm_weapon_profile("M16A4",0,0);
 for(float d=10;d<300;d+=20){
  // Caspian V3: aggression no longer widens the aim window (it made bots fire while off the body)
  assert(bm_weapon_tolerance(&aggro,rifle,d)==bm_weapon_tolerance(&legacy,rifle,d));
  assert(bm_weapon_tolerance(&brutal,rifle,d)==bm_weapon_tolerance(&legacy,rifle,d));
 }
 assert(bm_weapon_range(&aggro,rifle)>bm_weapon_range(&legacy,rifle) && bm_weapon_range(&brutal,rifle)<=rifle->maxRange);
 assert(bm_aggr_frames(&legacy,10,0.6f,0.45f)==10 && bm_aggr_frames(&aggro,10,0.6f,0.45f)==6 && bm_aggr_frames(&brutal,10,0.6f,0.45f)==5);
 assert(bm_aggr_frames(&aggro,9,1.5f,2.0f)==14 && bm_aggr_frames(&aggro,0,0.6f,0.45f)==1);
 // Director: two enemy flags at the same distance; the one this team just lost is retaken first.
 for(int team=1;team<=2;team++){
  BMDirectorState s;memset(&s,0,sizeof s);for(int i=0;i<BM_DIRECTOR_UNITS;i++)s.unit[i].flag=-1;BMDirectorFlag f[3]={{0}};BMDirectorUnit u[8]={{0}};
  // flag 0 is nearer; flag 1 (farther) was just lost.
  f[0].owner=3-team;f[0].position[0]=-300;f[1].owner=3-team;f[1].position[0]=450;f[2].owner=team;f[2].position[2]=900;
  f[1].recentLoss[team]=1;
  for(int i=0;i<8;i++){u[i].active=1;u[i].team=team;u[i].mobile=1;u[i].avoid=-1;}
  bm_director_update(&s,f,3,u,8,1000);
  int a=0,b=0;for(int i=0;i<8;i++){if(s.unit[i].flag==0)a++;if(s.unit[i].flag==1)b++;}
  assert(b>a);
  BMDirectorState s2;memset(&s2,0,sizeof s2);for(int i=0;i<BM_DIRECTOR_UNITS;i++)s2.unit[i].flag=-1;f[1].recentLoss[team]=0;f[0].recentLoss[team]=1;
  bm_director_update(&s2,f,3,u,8,1000);a=b=0;for(int i=0;i<8;i++){if(s2.unit[i].flag==0)a++;if(s2.unit[i].flag==1)b++;}
  assert(a>b);   // control: without a recent loss the nearer flag gets more
 }
 puts("PASS: Aggression 0/1/2 parses, keeps the aim window (V3), scales engage range (never past the weapon's maximum), bursts and pauses; the director sends more bots to a flag the team just lost, on both teams");
 return 0;
}
'''

ABANDONED = r'''
#include "activity_state.h"
#include "abandoned_dispatch.inc"
int main(void){
 for(int team=1;team<=2;team++){
  reset("M1Abrams",3);bm_defaults(&g_enhanced);g_enhanced.returnToBase=g_enhanced.armored=1;g_roundEpoch++;H->frame=3000;
  for(int i=0;i<4;i++){*(int*)(players[i]+8)=team;soldiers[i][0]=-50+i*10;soldiers[i][3]=100;brains[i].enabled=1;}
  vehicle.team=team;vehicle.xf[12]=0;vehicle.xf[14]=100;   // left in the field
  g_hqCpKnown[team]=1;g_hqCpPos[team][0]=-1200;g_hqCpKnown[3-team]=1;g_hqCpPos[3-team][0]=1200;
  brains[0].task=TASK_FIGHT;   // busy bots are not sent
  fleet_abandoned_dispatch();
  int sent=-1;for(int i=0;i<4;i++)if(g_fleet[i].vehicle==(char*)&vehicle){assert(sent<0);sent=i;}
  assert(sent>0 && g_fleet[sent].role==FLEET_GROUND && g_fleet[sent].queuedAt);
  // Nearest free bot: soldiers at x=-40,-30,-20 (bot 0 busy) -> bot 3.
  assert(sent==3);
  // One claim only, and not again within 3 s.
  H->frame+=30;fleet_abandoned_dispatch();int claims=0;for(int i=0;i<4;i++)claims+=g_fleet[i].vehicle==(char*)&vehicle;assert(claims==1);
  // A vehicle at its own deployment is the normal dispatcher's job; an occupied one is nobody's.
  reset("M1Abrams",3);bm_defaults(&g_enhanced);g_enhanced.returnToBase=g_enhanced.armored=1;g_roundEpoch++;H->frame=3000;
  for(int i=0;i<4;i++){*(int*)(players[i]+8)=team;soldiers[i][3]=100;brains[i].enabled=1;}
  vehicle.team=team;vehicle.xf[12]=-90;g_hqCpKnown[team]=1;g_hqCpPos[team][0]=-100;fleet_abandoned_dispatch();
  for(int i=0;i<4;i++)assert(!g_fleet[i].vehicle);
  g_hqCpPos[team][0]=-1200;memcpy(vehicle.entries[0]+0x4c,&(char*){human},sizeof(char*));H->frame+=200;fleet_abandoned_dispatch();
  for(int i=0;i<4;i++)assert(!g_fleet[i].vehicle);
 }
 puts("PASS: an abandoned ground vehicle in the field gets the nearest free infantry bot on BOTH teams; busy bots, base vehicles and occupied vehicles are left alone");
 return 0;
}
'''

LOWCOVER = r'''
int main(void){
 bm_defaults(&g_enhanced);
 for(int k=0;k<2;k++){H->bots[k]=players[k];*(int*)(players[k]+8)=k+1;*(float**)(players[k]+16)=bodies[k];bodies[k][3]=100;}
 strcpy(players[0]+64,"Medic");H->frame=1000;g_fightTarget[0]=players[1];bodies[1][2]=60;
 BotBrain b={.task=TASK_FIGHT,.taskUntil=10000};
 // Knees hidden behind low cover, chest in sight: the fight keeps the target and aims above the cover.
 lowCover=1;fight_tick(0,&b,(char*)bodies[0]);g_fightSeenAt[0]=0;
 H->frame=1100;assert(fight_tick(0,&b,(char*)bodies[0]) && g_fightSeenAt[0]==1100 && b.task==TASK_FIGHT);
 float pitchCover=g_aimPitch[0];lowCover=0;H->frame=1101;fight_tick(0,&b,(char*)bodies[0]);
 assert(pitchCover>g_aimPitch[0]);   // aimed at the upper chest while the chest was covered
 // Caspian V3: uncovered, the pitch points at the chest less the measured camera rise.
 assert(fabsf(g_aimValue[0]-(1.15f-g_aimBias[0]-g_aimFixH[0]-g_aimFixV[0]*60.0f))<0.05f);   // V4: less the bot's measured correction
 // Fully hidden: still lost after the (longer, aggressive) lost-sight time.
 lowCover=2;g_fightAdvance[0]=0;H->frame=2000;g_fightSeenAt[0]=2000;
 for(int k=0;k<4*30;k++){H->frame++;if(!fight_tick(0,&b,(char*)bodies[0]))break;}
 assert(b.task==TASK_FIGHT);   // 4 s > Test15's 3 s, still fighting
 for(int k=0;k<4*30;k++){H->frame++;if(!fight_tick(0,&b,(char*)bodies[0]))break;}
 assert(b.task!=TASK_FIGHT);
 puts("PASS: a target with its body up to the chest behind cover stays in sight and is aimed at above the cover (Test15 lost it and shot the cover); the open-ground aim point is the chest less the calibrated rise; a fully hidden one is dropped after 4.8 s when aggressive");
 return 0;
}
'''

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    maps = sorted(TRACES.glob('*.map'))
    assert maps, 'no Fun-Bots map files in files/server/BotMod_traces'
    core = (SRC/'core.c').read_text()
    botpath = re.search(r'typedef struct BotPath \{[^\n]+', core)[0]
    zombie = t4.ROUND.split('int main(void){')[0] + ZOMBIE
    abandoned = t15.base + ABANDONED
    fight = cm.FIGHT.split('int main(void){')[0]
    fight = fight.replace('static int solid_between(const float*a,const float*b,float*c){(void)a;(void)b;(void)c;return blocked;}',
        'static int lowCover;static int solid_between(const float*a,const float*b,float*c){(void)a;(void)c;return blocked || lowCover==2 || (lowCover==1 && b[1]<1.3f);}')
    assert 'lowCover' in fight
    # use the production lost-sight time (the Test8 harness pins Test15's 90 frames)
    lost = re.search(r'^#define FIGHT_LOST_FRAMES[^\n]+', core, re.M)[0]
    assert '#define FIGHT_LOST_FRAMES 90' in fight; fight = fight.replace('#define FIGHT_LOST_FRAMES 90', lost)
    lowcover = fight + LOWCOVER
    with tempfile.TemporaryDirectory(prefix='bf3-caspian-v2-') as tmp:
        tmp = Path(tmp)
        # trace_level_load builds "<exe dir>\BotMod_traces\<key>.map": give it a
        # file with exactly that (backslash) name on this POSIX test host.
        srv = tmp/'srv'; srv.mkdir()
        exe = str(srv) + '\\bf3.exe'
        for m in maps:
            os.symlink(m, str(srv) + '\\BotMod_traces\\' + m.name)
        jobs = [('graph', GRAPH, [str(SRC/'trace_nav.c')], [str(m) for m in maps if 'Conquest' in m.name]),   # (V9 TDM routes: altbotmod_v9_checks; V15 Rush / SQDM / Gun Master ... files: altbotmod_v15_checks)
                ('plan', PLAN.replace('%BOTPATH%', botpath), [str(SRC/'trace_nav.c'), str(SRC/'enhanced.c'), str(SRC/'air.c')], [exe]),
                ('zombie', zombie, [], []),
                ('policy', POLICY, [str(SRC/'enhanced.c'), str(SRC/'air.c')], []),
                ('abandoned', abandoned, [str(SRC/'enhanced.c'), str(SRC/'air.c'), str(SRC/'modes.c')], []),
                ('lowcover', lowcover, [str(SRC/'enhanced.c'), str(SRC/'air.c')], [])]
        for name, body, extra, args in jobs:
            p = tmp/(name + '.c'); p.write_text(body); out = p.with_suffix('')
            cmd = ['gcc', '-std=gnu11', '-O1', '-Wall', '-Wextra', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-parameter',
                   '-I', str(SRC), str(p), *extra, '-lm', '-o', str(out)]
            if name == 'zombie': cmd[1:1] = ['-Wno-misleading-indentation']
            if a.sanitize: cmd[1:1] = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
            subprocess.run(cmd, check=True); subprocess.run([str(out), *args], check=True, env=env)

if __name__ == '__main__': main()
