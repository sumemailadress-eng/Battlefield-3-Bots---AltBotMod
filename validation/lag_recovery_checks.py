"""Regression checks of actual objective, request, path follower and recovery code."""
from pathlib import Path
import argparse,re,subprocess,tempfile
from test4_checks import function,ROUND

ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src';core=(SRC/'core.c').read_text()
OBJECTIVES=r'''
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "modes.h"
#define MAX_BOTS 8
typedef struct {int avoidFlag,task,targetFlag;unsigned avoidUntil;}BotBrain;
static BotBrain brains[8];static BotBrain *bot_brain(int i){return &brains[i];}
static int owners[3]={1,2,2},g_flagCount=3,pressure[3],g_overflowFlag[8];
static float flags[3][3]={{0},{100,0,0},{105,0,0}};
static int flag_owner(int f){return owners[f];}static float *flag_pos(int f){return flags[f];}
static float dist2(const float*a,const float*b){float x=a[0]-b[0],z=a[2]-b[2];return x*x+z*z;}
static int flag_pressure(int t,int f,int except){assert(t==1 && except==0);return pressure[f];}
static int g_lastPlanDeferred,planResult,wantDeferred=1,dynamicHeight,budget=1;
static float seen[3];
static int plan_path(int i,char*s,float x,float y,float z){(void)i;(void)s;seen[0]=x;seen[1]=y;seen[2]=z;int n=dynamicHeight?(y==10?2:0):planResult;g_lastPlanDeferred=!n && wantDeferred;return n;}
static struct NavGrid {int phase;}grid={2},*gridPtr=&grid;
static struct NavGrid **nav_slot(void){return &gridPtr;}
static int nav_floors_at(struct NavGrid*g,float x,float y,float z,float*out,int max){(void)g;(void)x;(void)y;(void)z;assert(max>=2);out[0]=0;out[1]=10;return 2;}
static int flag_sector_spot(int i,char*s,int f,int d,float*out,float y){(void)i;(void)s;(void)f;(void)d;(void)out;(void)y;return 0;}
static int plan_budget_left(void){return budget;}
static float g_sectorYaw[8];static unsigned char g_hasSector[8];
#define FLAG_RETRY_OFFSETS 4
#define FLAG_SPREAD 10.0f
#define PATROL_RADIUS 15.0f
#define OFF_PLAYER_ONLINE_NAME 0
#define OFF_SOLDIER_POSITION 0
#define OFF_SOLDIER_HEALTH 12
static char *player_manager(void){return 0;}   /* AltBotMod V6: TDM hunt scan (no players in this harness) */
static struct {unsigned frame;int botCount;char*bots[8];float botMove[8][3];}host,*H=&host;
static struct BotNav {int mode;float x,z;}navs[8];
static struct BotNav *bot_nav(int i){return &navs[i];}static void rush_mark(int k,int st,const char*w){(void)k;(void)st;(void)w;}
static void bot_log(const char*fmt,...){(void)fmt;}
static char *player_vehicle(char*p){(void)p;return NULL;}
static struct {float patrol[32][3];struct {int present;float pos[3];}mcom[17];}profile,*g_modeProfile=&profile;
#include "rush_policy.h"
static BMRushState g_rush;
#define OFF_PLAYER_SOLDIER 16
static unsigned g_hitAt[MAX_BOTS],g_modeGoalProbeFrame;static int g_modeGoalProbeNext;
static struct {int phase;}g_drive[MAX_BOTS],g_air[MAX_BOTS];
static struct {void *vehicle;unsigned until;}g_fleet[MAX_BOTS];
static int player_team(char *p){(void)p;return 1;}
static void nav_cancel_path(int i){(void)i;}
static int ground_below(float x,float z,float a,float b,float*out){(void)x;(void)z;(void)a;(void)b;*out=0;return 1;}
static int nav_snap(struct NavGrid*g,float x,float y,float z,float*out){(void)g;out[0]=x;out[1]=y;out[2]=z;return 1;}
static int nav_is_water(struct NavGrid*g,float x,float y,float z){(void)g;(void)x;(void)y;(void)z;return 0;}
static struct {int target,heldInteract,goalReady,candidate,routePending,goalRole,snipe;unsigned plannedAt,interactAt,retryAt,revision,guardUntil,operatorAt,closerUntil,approachAt;float goal[3];char *soldier;} g_modeBot[MAX_BOTS];static int g_crateEvents;static int indoor_spot(int i,const float*a,float r,float*o){(void)i;(void)a;(void)r;(void)o;return 0;}
static enum BMMode g_mode=BM_MODE_TDM;static int g_rushStage=1;static int mode_ctf_goal(int i,const float*me,int team){(void)i;(void)me;(void)team;return 0;}   /* AltBotMod V15 */
'''+re.search(r'typedef struct FlagRouteRequest.*?static FlagRouteRequest g_flagRoute\[MAX_BOTS\];',core,re.S)[0]+'\n'+function(core,'overflow_objective')+'\n'+function(core,'plan_flag_path')+'\n'+function(core,'plan_objective_route')+'\n'+function(core,'go_towards')+'\n'+function((SRC/'modes_native.inc').read_text(),'rush_target')+'\n'+function((SRC/'modes_native.inc').read_text(),'mode_goal_clear')+'\n'+function((SRC/'modes_native.inc').read_text(),'mode_brain_tick')+r'''
int main(void){
 float pos[3]={0};assert(overflow_objective(0,1,pos)==1);
 pressure[1]=20;for(int i=0;i<200;i++)assert(overflow_objective(0,1,pos)==1); // no self-pressure flip every tick
 owners[1]=1;assert(overflow_objective(0,1,pos)==2); // capture advances the assignment
 brains[0].avoidUntil=100;brains[0].avoidFlag=2;owners[0]=2;assert(overflow_objective(0,1,pos)==0);
 char body[4],replacement[4];
 assert(!plan_flag_path(0,body,1,20,0,30) && g_flagRoute[0].pending);
 assert(!plan_flag_path(0,body,1,25,0,34) && seen[0]==20 && seen[2]==30); // random sector offsets cannot restart a queued search
 assert(!plan_flag_path(0,body,2,90,0,80) && seen[0]==90); // a real objective change replaces the request
 assert(!plan_flag_path(0,replacement,2,95,0,85) && seen[0]==95); // new soldier has its own request
 planResult=2;assert(plan_flag_path(0,replacement,2,98,0,87)==2 && !g_flagRoute[0].pending);
 H->bots[0]="test bot";memset(g_flagRoute,0,sizeof g_flagRoute);planResult=0;
 assert(!plan_objective_route(0,body,1,0) && g_lastPlanDeferred && g_flagRoute[0].floor==0 && g_flagRoute[0].candidate==1);
 float firstX=seen[0];assert(!plan_objective_route(0,body,1,0) && seen[0]==firstX && g_flagRoute[0].candidate==1);
 wantDeferred=0;dynamicHeight=1;assert(plan_objective_route(0,body,1,0)==2 && seen[1]==10 && !g_flagRoute[0].routeActive);
 dynamicHeight=0;assert(!plan_objective_route(0,body,1,0) && !g_lastPlanDeferred && !g_flagRoute[0].routeActive); // both floors really exhausted
 float feet[3]={0};wantDeferred=1;brains[0].task=1;H->botMove[0][0]=1;
 go_towards(0,(char*)feet,20,0,30);assert(navs[0].mode==0 && !H->botMove[0][0]);
 planResult=2;go_towards(0,(char*)feet,23,0,33);assert(navs[0].mode==3 && seen[0]==20); // moving human does not restart the pending task route
 H->frame=100;planResult=0;mode_brain_tick(0,(char*)feet);assert(g_modeBot[0].plannedAt==103 && navs[0].mode==3);
 H->frame=103;planResult=2;mode_brain_tick(0,(char*)feet);assert(g_modeBot[0].plannedAt==253 && navs[0].mode==3); // TDM polls unfinished work promptly
 puts("PASS: 200 unchanged objective decisions; capture/avoid changes; queued flag/body identity; alternate floors advance after real failures");return 0;
}
'''
FOLLOW=r'''
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#define MAX_BOTS 4
#define PATH_MAX_POINTS 128
#define OFF_SOLDIER_POSITION 0
#define OFF_SOLDIER_YAW 12
#define OFF_PLAYER_ONLINE_NAME 0
#define WAYPOINT_REACHED 1.5f
#define WAYPOINT_EXACT 0.5f
typedef struct {int mode;float x,z,targetYaw;}BotNav;
static struct {unsigned frame;int traceInput;char *bots[MAX_BOTS];float botMove[MAX_BOTS][3];}host,*H=&host;
'''+re.search(r'typedef struct BotPath.*?static BotPath g_paths\[MAX_BOTS\];',core,re.S)[0]+r'''
static unsigned g_wpNearAt[4];static int g_wpNearIdx[4],g_lastPlanDeferred,budget=1,result,arrivals;
static int plan_budget_left(void){return budget;}
static int ray_cast(const float*a,const float*b,float*p,float*n){(void)a;(void)b;(void)p;(void)n;return 0;}
static int nav_segment_clear(int(*r)(const float*,const float*,float*,float*),const float*a,const float*b){(void)r;(void)a;(void)b;return 1;}
static void bot_log(const char*fmt,...){if(strstr(fmt,"arrived"))arrivals++;}
static int plan_path(int i,char*s,float x,float y,float z){(void)s;(void)x;(void)y;(void)z;g_lastPlanDeferred=!result;if(result){g_paths[i].count=2;g_paths[i].next=0;g_paths[i].partial=0;g_paths[i].pts[0]=30;g_paths[i].pts[3]=100;}return result;}
static void trace_jump_tick(int i,char *s){(void)i;(void)s;}static void route_waypoint_tick(int i,char *s,BotPath *p){(void)i;(void)s;(void)p;}   /* AltBotMod V15 */
'''+function(core,'follow_path')+r'''
int main(void){
 float body[4]={20,0,0,0};BotNav nav={.mode=3};H->frame=100;H->bots[0]="bot";
 BotPath*p=&g_paths[0];p->count=p->next=2;p->partial=1;p->goalX=100;for(int i=0;i<128;i++)p->link[i]=-1;
 budget=0;follow_path(0,(char*)body,&nav);assert(nav.mode==3 && !arrivals && p->partial);
 budget=1;result=0;follow_path(0,(char*)body,&nav);assert(nav.mode==3 && !arrivals && p->partial);
 result=2;follow_path(0,(char*)body,&nav);assert(nav.mode==3 && nav.x==30 && !arrivals && !p->partial);
 body[0]=100;p->next=p->count;follow_path(0,(char*)body,&nav);assert(nav.mode==1 && arrivals==1);
 puts("PASS: actual path follower waits for continuation and only reports final arrival");return 0;
}
'''
prefix=ROUND[:ROUND.index('int main(void)')]
prefix=re.sub(r'static void queue_bot_removal\(int i\)\{[^\n]+\}\n','',prefix)
prefix=prefix.replace((SRC/'round_native.inc').read_text(),'')
RECOVERY=prefix+r'''
#include <math.h>
#define OFF_SOLDIER_POSITION 0
#define OFF_SOLDIER_SPAWN_POSITION 0x4b0
static int stuckReports;static void bm_report_stuck(int b,const float *p,const char *k){(void)b;(void)p;(void)k;stuckReports++;}   // AltBotMod V13 live map
typedef struct {int enabled,task,waiting;}BotBrain;
static BotBrain brains[8];static BotBrain *bot_brain(int i){return &brains[i];}
static struct {int value;}g_drive[8],g_fleet[8],g_paths[8];
static struct {int mode;}navs[8];
static void *bot_nav(int i); // replaced with a typed accessor below
'''
RECOVERY=RECOVERY.replace('static void *bot_nav(int i); // replaced with a typed accessor below','static typeof(navs[0]) *bot_nav(int i){return &navs[i];}')+r'''
static void set_brain(int i,int on){brains[i].enabled=on;}
static int airStops;static void air_stop(int i,const char*why,int exitVehicle){(void)i;(void)why;assert(!exitVehicle);airStops++;}
static char *player_vehicle(char*p){(void)p;return NULL;}
static struct {int phase;}grid={2},*gridPtr=&grid;
static typeof(gridPtr) *nav_slot(void){return &gridPtr;}
static unsigned g_local_botmod_tick_lastMove[8];static float g_local_botmod_tick_lastPos[8][3];
'''+re.search(r'^static int removal_pending[^\n]+',core,re.M)[0]+'\n'+function(core,'queue_bot_removal')+'\n'+function(core,'retire_stuck_bot')+'\nstatic void watchdog(void){\n'+r'''
}
int main(void){
 char player[128]={0};float soldier[4]={0};H->frame=6000;H->botCount=1;H->bots[0]=player;H->botInput[0]=malloc(4);
 strcpy(player+64,"test bot");*(void**)(player+16)=soldier;brains[0].enabled=1;g_local_botmod_tick_lastMove[0]=4000;
 assert(!g_removeCount && !kills); // retirement only occurs when explicitly requested
 H->botMove[0][0]=1;H->frame=7500;retire_stuck_bot(0,"checked recovery exhausted");assert(g_removeCount==1 && g_silentRemove[0] && !brains[0].enabled && !kills && kicks==1 && neutral==60);
 assert(!H->botMove[0][0] && airStops==1);retire_stuck_bot(0,"again");assert(kicks==1 && neutral==60);
 removal_tick();assert(!kills && H->botCount==1); // wait for the kick's actual native unspawn
 *(void**)(player+16)=NULL;removal_tick();assert(!H->botCount && deleted==1 && !kills);
 puts("PASS: actual retirement helper neutralizes, kicks and deletes a fresh identity without suicide");return 0;
}
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--sanitize',action='store_true');a=ap.parse_args()
 with tempfile.TemporaryDirectory(prefix='bf3-lag-recovery-')as temp:
  temp=Path(temp)
  for name,code in [('objectives',OBJECTIVES),('continuation',FOLLOW),('recovery',RECOVERY)]:
   p=temp/(name+'.c');p.write_text(code);exe=temp/name
   cmd=['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Wno-unused-function','-Wno-unused-variable','-I',str(SRC),str(p),str(SRC/'modes.c'),'-lm','-o',str(exe)]
   if a.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
   subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
