"""Actual Test15 activity, departure and aircraft dispatch code with mocked
native entities/physics. These checks cannot prove live BF3 map/flight behavior.
"""
from pathlib import Path
import argparse,os,re,subprocess,tempfile
import driver_departure_checks as driver
from test4_checks import function
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src'

base=(ROOT/'validation/air_native_tests.c').read_text().split('int main(void)')[0].replace('#include "../files/BotMod/src/','#include "')
base=base.replace('typedef struct { int phase,flag,manual; char *veh; unsigned at,walkUntil; float start[3],goal[3]; } DriveState;', '#define MAX_FLAGS 16\n#define DRIVE_MAX_PTS 128\n'+driver.drive_type)
base=base.replace('BotTeamSpawn teamSpawn[3];','BotTeamSpawn teamSpawn[3];int stepBots;void *teamBlueprint[3];')
base=base.replace('int enabled,task,prevTask,targetFlag,','unsigned stepAt;int crouched;int enabled,task,prevTask,targetFlag,')
base=base.replace('static struct{int count,next;}g_paths[MAX_BOTS];','#define PATH_MAX_POINTS 128\n'+re.search(r'typedef struct BotPath \{[^\n]+',(SRC/'core.c').read_text())[0]+'\nstatic BotPath g_paths[MAX_BOTS];')
base=base.replace('memset(&g_drive[i],0,sizeof g_drive[i]);}', 'bot_exit_vehicle(i,0);memset(&g_drive[i],0,sizeof g_drive[i]);}')
base=base.replace('static void drive_stop(int i,','static void bot_exit_vehicle(int i,int parachute);\nstatic void drive_stop(int i,')
base=base.replace('static int flagOwners[6]', 'static int flagOwners[6]')
ACTIVITY=r'''
#define TASK_FOLLOW 1
#define TASK_DEFEND 2
#define TASK_SUPPLY 3
#define TASK_REPAIR 4
#define TASK_REVIVE 5
#define TASK_COVER 6
#define TASK_ROCKET 7
#define NAV_HIT_WATER 1
static int g_roundCleanup;
static unsigned char g_brainHeld[MAX_BOTS],g_silentRemove[MAX_BOTS];
static unsigned g_noRouteSince[MAX_BOTS],g_directorAt;
static int retired,blockedGeometry;
static void combat_motion_cancel(int i){(void)i;}
static void bot_stand(int i){(void)i;}
static void end_revive(int i,BotBrain*b,const char*w){end_task(i,b,w);}
static void end_cover(int i,BotBrain*b,const char*w){end_task(i,b,w);}
static void end_repair(int i,BotBrain*b,const char*w){end_task(i,b,w);}
static void end_rocket(int i,BotBrain*b,const char*w){end_task(i,b,w);}
static int is_bot(char*p){return bot_index(p)>=0;}
static int nav_hit_class(void){return padWater?NAV_HIT_WATER:0;}
static void nav_set_path_mask(const unsigned char*m){(void)m;}
static void nav_set_path_owner(int i){(void)i;}
static int nav_segment_clear(int(*ray)(const float*,const float*,float*,float*),const float*a,const float*b){(void)ray;(void)a;(void)b;return !blockedGeometry;}
static void retire_stuck_bot(int i,const char*w){(void)w;retired++;g_silentRemove[i]=1;}
static int stationary_weapon(char*v){(void)v;return 0;}
static int sniperHolding,sniperRifle;   /* AltBotMod V10 sniper overwatch (sniper_native.inc) */
static int sniper_holding(int i){(void)i;return sniperHolding;}
static int sniper_rifle(int i){(void)i;return sniperRifle;}
static float sniper_reach(int i){(void)i;return 550;}
static int class_job_working(int i,const float*p){(void)i;(void)p;return 0;}   /* AltBotMod V11 */
int nav_block_towards(NavGrid*g,float x,float y,float z,float tx,float tz){(void)g;(void)x;(void)y;(void)z;(void)tx;(void)tz;return 0;}   /* AltBotMod V18 */
#include "activity_state.h"
#include "activity_native.inc"
static void fresh(int team){
 reset("M1Abrams",3);H->stepBots=1;H->frame=990;g_enhanced.returnToBase=1;
 memset(g_activity,0,sizeof g_activity);memset(g_silentRemove,0,sizeof g_silentRemove);memset(g_brainHeld,0,sizeof g_brainHeld);
 g_activityRetireAt=g_activityProbeFrame=retired=blockedGeometry=g_roundCleanup=0;
 for(int i=0;i<H->botCount;i++){*(int*)(players[i]+8)=team;soldiers[i][3]=100;brains[i].targetFlag=-1;}
 flagOwners[0]=3-team;flags[0][0]=0;flags[0][1]=0;flags[0][2]=500;
}
int main(void){
 for(int team=1;team<=2;team++){
  fresh(team);activity_tick(0);H->frame+=18*30;activity_tick(0);
  assert(g_activity[0].attempts==1 && g_paths[0].count==1 && navs[0].mode==3);
  assert(g_activity[0].escapeUntil>H->frame && !retired && !H->botMove[0][0]);
  assert(activity_grid_tick(0,(char*)soldiers[0]));
  // All flags owned, no objective target: still obtains a checked patrol exit.
  fresh(team);flagOwners[0]=team;activity_tick(0);H->frame+=18*30;activity_tick(0);
  assert(g_paths[0].count==1 && g_activity[0].escapeUntil && !retired);
  // Changing jobs or retry timers never counts as actual movement.
  fresh(team);brains[0].task=TASK_FIGHT;activity_tick(0);
  H->frame+=18*30;brains[0].replanAt=H->frame+99999;activity_tick(0);
  assert(brains[0].task==TASK_NONE && g_paths[0].count==1 && g_activity[0].progressAt==990);
  // Ready-grid and no-grid geometry must both reject obstacles/water.
  fresh(team);blockedGeometry=1;activity_tick(0);H->frame+=18*30;activity_tick(0);assert(!g_paths[0].count);
  fresh(team);padWater=1;activity_tick(0);H->frame+=18*30;activity_tick(0);assert(!g_paths[0].count);
  // Human orders, real captures, pause, airborne parachutes, round cleanup preserved.
  fresh(team);brains[0].commander=human;activity_tick(0);H->frame+=3000;activity_tick(0);assert(!g_activity[0].progressAt&&!retired);
  fresh(team);brains[0].waiting=1;brains[0].targetFlag=0;flags[0][2]=10;activity_tick(0);H->frame+=18*30;activity_tick(0);assert(!g_paths[0].count&&!retired);
  fresh(team);brains[0].enabled=0;activity_tick(0);assert(!g_activity[0].progressAt);
  fresh(team);g_roundCleanup=1;activity_tick(0);assert(!g_activity[0].progressAt);
  fresh(team);g_gliding[0]=1;activity_tick(0);assert(!g_activity[0].progressAt);
  // Invalid "waiting" at deployment is no exemption.
  fresh(team);brains[0].waiting=1;brains[0].targetFlag=0;activity_tick(0);H->frame+=18*30;activity_tick(0);assert(g_paths[0].count==1);
  // Ground/boat/passenger route/traffic waits end despite continuous retries.
  for(int seat=0;seat<=2;seat++){
   fresh(team);strcpy(vehicle.name,seat==2?"RHIB":"M1Abrams");enter_vehicle(players[0],&vehicle,seat);
   g_drive[0].phase=seat?7:10;g_drive[0].veh=(char*)&vehicle;activity_tick(0);
   H->frame+=44*30;g_drive[0].at=H->frame;activity_tick(0);assert(controlled[0]);
   H->frame+=30;activity_tick(0);assert(!controlled[0] && !g_drive[0].phase && g_fleet[0].retryAt>H->frame);
   assert(fleet_route_blocked((char*)&vehicle,team,vehicle.xf+12));
  }
  // Oscillating in a small area is not a successful objective journey.
  fresh(team);enter_vehicle(players[0],&vehicle,0);g_drive[0].phase=3;g_drive[0].veh=(char*)&vehicle;activity_tick(0);
  for(int k=1;k<=90;k++){H->frame+=30;vehicle.xf[12]=k%2?4:0;activity_tick(0);}
  assert(!controlled[0]);
  // A moving crew and human-driven vehicle remain seated.
  fresh(team);enter_vehicle(players[0],&vehicle,0);g_drive[0].phase=3;g_drive[0].veh=(char*)&vehicle;activity_tick(0);
  for(int k=1;k<=120;k++){H->frame+=30;vehicle.xf[12]+=3;activity_tick(0);}assert(controlled[0]);
  fresh(team);enter_vehicle(players[0],&vehicle,1);memcpy(vehicle.entries[0]+0x4c,&(char*){human},sizeof(char*));
  g_drive[0].phase=7;g_drive[0].veh=(char*)&vehicle;activity_tick(0);H->frame+=3000;activity_tick(0);assert(controlled[0]);
  // Stationary flight resets controls at 15 s, then releases failed job at 45 s.
  fresh(team);strcpy(vehicle.name,"AH1Z");enter_vehicle(players[0],&vehicle,0);
  g_air[0].phase=AIR_FLY;g_air[0].pilot=1;g_air[0].veh=(char*)&vehicle;g_air[0].profile=bm_air_profile("AH1Z");
  g_drive[0].phase=9;activity_tick(0);H->frame+=15*30;activity_tick(0);
  assert(g_air[0].control.phase==BM_FLIGHT_RECOVER && g_air[0].control.timer==6 && controlled[0]);H->frame+=30*30;activity_tick(0);
  assert(!controlled[0]&&!g_air[0].phase);
  // No forced spawn deaths. Last resort retires a trapped identity after 90 s.
  fresh(team);blockedGeometry=1;activity_tick(0);H->frame+=90*30;activity_tick(0);assert(retired==1);
  fresh(team);*(void**)(players[0]+OFF_PLAYER_SOLDIER)=NULL;H->teamBlueprint[team]=(void*)1;activity_tick(0);
  H->frame+=89*30;activity_tick(0);assert(!retired);H->frame+=30;activity_tick(0);assert(retired==1);
 }
 puts("PASS: actual activity adapter recovers zero-input infantry, stale fights/services, invalid flag waits and all-owned patrols on BOTH teams; respects captures, human orders, pauses, falls and round teardown");
 puts("PASS: ground/boat/cargo waits have a 45 s terminal deadline independent of routing timers; small-area oscillation has a 90 s travel deadline; moving/human-driven crews remain seated");
 puts("PASS: stationary flight resets after 15 s, failed flight falls back after 45 s, and absent/trapped player identities retire after 90 s without forced suicide");
 return 0;
}
'''

AIR=r'''
#define ORDER_NEGATIVE 1
#define ORDER_ROGERTHAT 2
#define ORDER_GETIN 3
static unsigned g_askedAt[MAX_BOTS];
static int vehicle_is_boat(char*v){return bm_vehicle_profile(vehicle_name(v))->kind==BM_BOAT;}
static void bot_say(int i,int o){(void)i;(void)o;}
static void bot_say_later(int i,int o,unsigned d){(void)i;(void)o;(void)d;}
'''+function((SRC/'core.c').read_text(),'drive_pickup')+r'''
#include "activity_state.h"
#include "air_dispatch.inc"
int main(void){
 for(int team=1;team<=2;team++){
  reset("M1Abrams",3);bm_defaults(&g_enhanced);g_enhanced.returnToBase=g_enhanced.armored=1;
  g_roundEpoch++;H->frame=3000;H->botCount=4;vehicleCount=3;
  Vehicle pool[3];for(int k=0;k<3;k++){pool[k]=vehicle;pool[k].team=team;strcpy(pool[k].name,k==0?"F16":k==1?"AH1Z":"M1Abrams");
   pool[k].xf[12]=k*30;pool[k].xf[14]=0;for(int q=0;q<5;q++)pool[k].entries[q]=pool[k].seatsMemory[q]+4;vehicles[k]=(char*)&pool[k];}
  for(int i=0;i<4;i++){*(int*)(players[i]+8)=team;soldiers[i][3]=100;
   g_drive[i].phase=i%2?5:1;g_drive[i].veh=vehicles[2];
   g_fleet[i].vehicle=vehicles[2];g_fleet[i].role=FLEET_GROUND;g_fleet[i].until=H->frame+900;
  }
  // Every bot has a ground walking job: new aircraft must still get pilots.
  fleet_air_dispatch();int air,jets,ground,total;fleet_counts(team,&air,&jets,&ground,&total);
  assert(air==2&&jets==1);
  // An ordinary ground ride offer must not consume either reserved pilot.
  DriveState *d=&g_drive[3];d->phase=3;d->veh=vehicles[2];memcpy(d->goal,flags[0],12);
  int claimsBefore=claims;drive_pickup(3,d,soldiers[3]);assert(claims==claimsBefore);
  for(int i=0;i<4;i++)if(g_fleet[i].role!=FLEET_GROUND){
    assert(!g_drive[i].phase);Vehicle*v=(Vehicle*)g_fleet[i].vehicle;memcpy(soldiers[i],v->xf+12,12);
    fleet_tick(i);air_tick(i);assert(controlled[i]==(char*)v && g_air[i].pilot);
  }
  H->frame+=9*30;for(int i=0;i<4;i++)if(g_air[i].phase){air_tick(i);air_apply(i,inputs[i]);assert(g_air[i].phase==AIR_FLY&&((float*)(inputs[i]+0x10))[EIA_THROTTLE]>0);}
 }
 puts("PASS: BOTH teams transfer ground walking assignments to a late available jet and attack helicopter, board native seats and emit flight throttle even when every bot already has a ground claim");return 0;
}
'''

pre=re.sub(r'^static int __attribute__\(\(unused\)\) drive_live_exit\([^\n]*\n','',driver.PRE,flags=re.M)
LIVE=pre+r'''
#define NAV_HIT_WATER 1
static int liveWall,waterFloor;
static int nav_hit_class(void){return waterFloor;}
static void nav_set_path_owner(int i){(void)i;}
static int nav_segment_clear(int(*ray)(const float*,const float*,float*,float*),const float*a,const float*b){(void)ray;(void)a;(void)b;return !liveWall;}
'''+(SRC/'ground_live_exit.inc').read_text()+driver.MAIN[:driver.MAIN.index('int main(void)')]+r'''
int main(void){
 for(int team=1;team<=2;team++){
  reset(team);float p[3]={0,77,0},point[3];DriveState*d=&g_drive[0];maskMissing=1;
  H->frame++;assert(drive_live_exit(0,d,p,point));assert(dist2(p,point)>9*9);
  assert(!drive_live_exit(0,d,p,point)); // One live geometry candidate/frame.
  H->frame++;liveWall=1;assert(!drive_live_exit(0,d,p,point));liveWall=0;
  H->frame++;waterFloor=1;assert(!drive_live_exit(0,d,p,point));waterFloor=0;
  H->frame++;terrainMissing=1;assert(!drive_live_exit(0,d,p,point));terrainMissing=0;
 }
 puts("PASS: actual live hull-width escape works without a driving cache on BOTH teams, rejects walls, water and absent terrain, and limits surveying to one candidate per frame");return 0;
}
'''

DYNAMIC=r'''
#include <assert.h>
#include <stdio.h>
#include "director_policy.h"
int main(void){
 for(int team=1;team<=2;team++)for(int nf=2;nf<=16;nf++){
  BMDirectorState state={0};BMDirectorFlag flags[16]={0};BMDirectorUnit units[64]={0};
  for(int f=0;f<nf;f++){flags[f].owner=3-team;flags[f].position[0]=f*1200+600;}
  for(int i=0;i<32;i++){units[i].active=1;units[i].team=team;units[i].avoid=-1;state.unit[i].flag=-1;}
  bm_director_update(&state,flags,nf,units,32,30);int old[32];for(int i=0;i<32;i++)old[i]=state.unit[i].flag;
  bm_director_update(&state,flags,nf,units,32,60);
  bm_director_update(&state,flags,nf,units,32,60+45*30);int changes=0;
  for(int i=0;i<32;i++){assert(state.unit[i].role==BM_OBJECTIVE_ATTACK&&state.unit[i].flag>=0);changes+=state.unit[i].flag!=old[i];}
  assert(changes>0);for(int f=0;f<nf;f++){int count=0;for(int i=0;i<32;i++)count+=state.unit[i].flag==f;assert(f<2?count>0:count==0);}   /* AltBotMod V25: the two nearest flags first, nobody sent beyond them */
  for(int f=0;f<nf;f++)flags[f].owner=team;
  bm_director_update(&state,flags,nf,units,32,1500);for(int i=0;i<32;i++)assert(state.unit[i].role==BM_OBJECTIVE_PATROL);
 }
 // One failed enemy route gets a temporary friendly patrol; after the route
 // cooldown expires the bot immediately resumes its enemy flag duty.
 BMDirectorState s={0};BMDirectorFlag f[2]={0};BMDirectorUnit u[1]={0};
 f[0].owner=2;f[1].owner=1;f[1].position[0]=1000;u[0].active=1;u[0].team=1;u[0].avoid=0;s.unit[0].flag=-1;
 bm_director_update(&s,f,2,u,1,30);assert(s.unit[0].flag==1&&s.unit[0].role==BM_OBJECTIVE_PATROL);
 u[0].avoid=-1;bm_director_update(&s,f,2,u,1,60);assert(s.unit[0].flag==0&&s.unit[0].role==BM_OBJECTIVE_ATTACK);
 puts("PASS: BOTH teams receive changing enemy duties after 45 s without objective progress, retain coverage across 2-16 flags, patrol when all owned, and return from temporary failed-route patrol to enemy attack");return 0;
}
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--sanitize',action='store_true');args=ap.parse_args()
 bodies={'activity':base+ACTIVITY,'late-aircraft':base+AIR,'live-departure':LIVE,'dynamic-director':DYNAMIC}
 with tempfile.TemporaryDirectory(prefix='bf3-test15-') as tmp:
  for name,body in bodies.items():
   p=Path(tmp)/(name+'.c');p.write_text(body);exe=p.with_suffix('')
   cmd=['gcc','-std=c11','-Wall','-Wextra','-Wno-unused-function','-Wno-unused-variable','-I',str(SRC),str(p),str(SRC/'enhanced.c'),str(SRC/'air.c'),str(SRC/'modes.c'),'-lm','-o',str(exe)]
   if args.sanitize:cmd[1:1]=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']
   subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
if __name__=='__main__':main()
