"""Reproduce Test4 boarding starvation and exercise the actual fixed planner.
The engine pathfinder is mocked; no BF3 server or vehicle physics is simulated.
"""
from pathlib import Path
import argparse, re, subprocess, tempfile
from test4_checks import function

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'files/BotMod/src'
core=(SRC/'core.c').read_text()
state=re.search(r'typedef struct VehicleApproachState \{[\s\S]*?static VehicleApproachState g_vehicleApproach\[MAX_BOTS\];',(SRC/'fleet_state.h').read_text())[0]
PRE=r'''
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#define MAX_BOTS 4
#define OFF_SOLDIER_POSITION 0
#define OFF_PLAYER_ONLINE_NAME 0
#define VEHICLE_BOARD_DIST 7.0f
static struct {unsigned frame;char *bots[MAX_BOTS];float botMove[MAX_BOTS][3];}host,*H=&host;
typedef struct {int count,next,partial;short link[128];float pts[384],goalX,goalY,goalZ;}BotPath;static BotPath g_paths[MAX_BOTS];
typedef struct Nav {int mode;float x,z;}BotNav;static BotNav navs[MAX_BOTS];
static struct Nav *bot_nav(int i){return &navs[i];}
static float dist2(const float*a,const float*b){float x=a[0]-b[0],z=a[2]-b[2];return x*x+z*z;}
static int budget=3,calls,totalCalls,allowSuccess=1,g_lastPlanDeferred;
static float goal[3]={100,0,100};
static int plan_budget_left(void){return calls<budget;}
static void nav_cancel_path(int i){(void)i;}
static int ray_cast(const float*a,const float*b,float*h,float*n){(void)a;(void)b;(void)h;(void)n;return 0;}
static int shortClear;
static int nav_segment_clear(int(*r)(const float*,const float*,float*,float*),const float*a,const float*b){(void)r;(void)a;(void)b;return shortClear;}
static int plan_path(int i,char*s,float x,float y,float z){
 (void)i;(void)s;(void)y;assert(plan_budget_left());calls++;totalCalls++;
 return allowSuccess && hypotf(x-goal[0],z-goal[2])>5.8f &&
        fabsf(x-goal[0])<0.25f && z-goal[2]>5.8f ? 7:0;
}
static void frame(void){H->frame++;calls=0;}
static int vehicle_quick_route(int i,char*s,const float*vp){(void)i;(void)s;(void)vp;return 0;}
'''
OLD_MAIN=r'''
int main(void){float soldier[3]={0};
 for(int n=0;n<10;n++){frame();assert(!plan_to_vehicle(0,(char*)soldier,goal));assert(calls==3);}
 assert(totalCalls==30);
 puts("REPRODUCED: Test4 repeats the same blocked hull points; wider approaches are never reached");return 0;
}
'''
BOARD_PRE=r'''
typedef struct {int phase;unsigned walkUntil,at;char*veh;float start[3];}DriveState;
static DriveState drives[MAX_BOTS];static int g_driving[MAX_BOTS],boat,seatFree=1,stops,enters;
static char *inside;
static int vehicle_is_boat(char*v){(void)v;return boat;}
static int driver_seat_free(char*v){(void)v;return seatFree;}
static void drive_stop(int i,const char*why,int delay){(void)why;(void)delay;drives[i].phase=0;stops++;}
static char *player_vehicle(char*pl){(void)pl;return inside;}
static int enter_vehicle(void*pl,void*v,int seat){(void)pl;assert(!seat);inside=v;enters++;return 1;}
typedef int(*EnterVehicle_t)(void*,void*,int);
#define ADDR_PLAYER_ENTER_VEHICLE enter_vehicle
static void bot_log(const char*fmt,...){(void)fmt;}
static const char *vehicle_name(char*v){(void)v;return "tank";}
'''
NEW_MAIN=r'''
int main(void){float soldier[3]={0},replacement[3]={0};
 frame();assert(!plan_to_vehicle(0,(char*)soldier,goal));
 assert(vehicle_plan_deferred(0)&&g_vehicleApproach[0].candidate==3&&calls==3);
 frame();assert(plan_to_vehicle(0,(char*)soldier,goal)==7);
 assert(!vehicle_plan_deferred(0)&&totalCalls==4);
 // No budget available before the first point: no attempted path or blacklist.
 budget=0;frame();assert(!plan_to_vehicle(1,(char*)soldier,goal));
 assert(vehicle_plan_deferred(1)&&!g_vehicleApproach[1].candidate&&!calls);
 // A changed soldier restarts the search rather than inheriting an old body.
 budget=3;frame();assert(!plan_to_vehicle(1,(char*)soldier,goal));
 frame();assert(!plan_to_vehicle(1,(char*)replacement,goal));
 assert(g_vehicleApproach[1].candidate==3);
 // A moving target restarts approach selection.
 goal[0]+=20;frame();assert(!plan_to_vehicle(1,(char*)replacement,goal));
 assert(g_vehicleApproach[1].candidate==3);
 // A real failure is declared only after all 13 candidates have been tested.
 allowSuccess=0;memset(g_vehicleApproach,0,sizeof g_vehicleApproach);totalCalls=0;
 for(int n=0;n<5;n++){frame();assert(!plan_to_vehicle(0,(char*)soldier,goal));
   assert(vehicle_plan_deferred(0)==(n<4));}
 assert(totalCalls==13&&!g_vehicleApproach[0].soldier);
 // Already beside the vehicle: no route required, even with zero budget.
 soldier[0]=goal[0];soldier[2]=goal[2]+5;budget=0;frame();
 g_paths[0].count=8;g_paths[0].next=3;navs[0].mode=3;H->botMove[0][0]=1;
 assert(plan_to_vehicle(0,(char*)soldier,goal)==1&&!calls&&!vehicle_plan_deferred(0));
 assert(!g_paths[0].count&&!g_paths[0].next&&!navs[0].mode&&!H->botMove[0][0]);
 // A clear short walk bypasses an exhausted queue and clears stale ladder links.
 soldier[0]=goal[0]-20;soldier[2]=goal[2];shortClear=1;g_paths[0].link[0]=12;frame();
 assert(plan_to_vehicle(0,(char*)soldier,goal)==1&&!calls&&!vehicle_plan_deferred(0));
 assert(g_paths[0].count==1&&g_paths[0].link[0]==-1&&g_paths[0].pts[0]==goal[0]-6);
 shortClear=0;frame();assert(!plan_to_vehicle(0,(char*)soldier,goal)&&vehicle_plan_deferred(0));
 // Different storey and invalid data do not bypass the pathfinder.
 soldier[1]=20;frame();assert(!plan_to_vehicle(0,(char*)soldier,goal)&&vehicle_plan_deferred(0));
 goal[0]=NAN;frame();assert(!plan_to_vehicle(0,(char*)soldier,goal)&&!vehicle_plan_deferred(0));
 // The ACTUAL ground-driver branch must agree with the planner's boarding range.
 goal[0]=100;goal[1]=0;goal[2]=100;soldier[0]=100;soldier[1]=0;soldier[2]=105;
 H->bots[0]=(char*)1;drives[0].phase=1;drives[0].veh=(char*)2;
 drives[0].walkUntil=H->frame+100;memcpy(drives[0].start,goal,12);
 driver_board(0,(char*)soldier);assert(inside==(char*)2&&drives[0].phase==2&&g_driving[0]&&!stops&&enters==1);
 // A snapped seventeen-metre outer route endpoint gets a short final approach, not a stall.
 inside=NULL;drives[0].phase=1;g_paths[0].next=g_paths[0].count;soldier[2]=117;navs[0].mode=0;
 driver_board(0,(char*)soldier);assert(navs[0].mode==2&&navs[0].x==100&&navs[0].z==100&&!inside);
 soldier[2]=106;driver_board(0,(char*)soldier);assert(inside==(char*)2&&enters==2&&!stops);
 // A soldier on another storey does not enter through the proximity shortcut.
 inside=NULL;drives[0].phase=1;soldier[1]=20;driver_board(0,(char*)soldier);assert(!inside&&enters==2);
 puts("PASS: actual resumable boarding planner, bounded queries, hull perimeter, native-close boarding, lifetime reset and genuine failure");return 0;
}
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--sanitize',action='store_true');args=ap.parse_args()
 old=(ROOT/'validation/test4-vehicle-planner.fragment').read_text()
 drive=function(core,'drive_tick');start=drive.index('    if (d->phase == 1) {');a=drive.index('{',start);depth=1
 for b in range(a+1,len(drive)):
  depth+=(drive[b]=='{')-(drive[b]=='}')
  if not depth:break
 boarding=BOARD_PRE+'\nstatic void driver_board(int i,char*s){DriveState*d=&drives[i];char*pl=H->bots[i];const float*vp=goal;\n'+drive[start:b+1]+'\n}\n'
 new=state+'\nstatic int vehicle_board_close(char*,const float*);\n'+function(core,'plan_to_vehicle')+'\n'+function(core,'vehicle_board_close')+'\n'+function(core,'vehicle_plan_deferred')+'\n'+function(core,'vehicle_finish_approach')+'\n'+boarding
 with tempfile.TemporaryDirectory(prefix='bf3-vehicle-approach-') as tmp:
  for name,body in [('test4-reproduction',old+OLD_MAIN),('test5-fix',new+NEW_MAIN)]:
   p=Path(tmp)/(name+'.c');p.write_text(PRE+body);exe=p.with_suffix('')
   opts=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-g']if args.sanitize else ['-O2']
   subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Wno-unused-function','-Wno-unused-variable',*opts,str(p),'-lm','-o',str(exe)],check=True)
   subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
