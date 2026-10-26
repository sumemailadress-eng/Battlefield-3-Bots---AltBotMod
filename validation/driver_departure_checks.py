"""Actual complete drive_tick: bounded boarding, staged routing and throttle.
Engine terrain, seats, raycasts and route results are mocked; this is not BF3 physics.
"""
from pathlib import Path
import argparse,re,subprocess,tempfile
from test4_checks import function
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src'
core=(SRC/'core.c').read_text()
drive_type=re.search(r'typedef struct DriveState \{[\s\S]*?\} DriveState;',core)[0]
PRE=r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "enhanced.h"
#include "modes.h"
#define MAX_BOTS 4
#define MAX_FLAGS 16
#define DRIVE_MAX_PTS 128
#define OFF_PLAYER_SOLDIER 16
#define OFF_PLAYER_ONLINE_NAME 64
#define OFF_SOLDIER_POSITION 0
#define OFF_VEHICLE_HEALTH 0
#define TASK_NONE 0
#define ORDER_GETIN 1
#define VEHICLE_BOARD_DIST 7
#define SQUAD_VEH_RADIUS 90
#define DRIVE_WAYPOINT 6
#define BOT_TICK_DT (1.0f/30)
#define NAV_LAYERS 6
#define RAY_DONT_CHECK_CHARACTER 0x20
#define RAY_CHECK_DETAIL_MESH 0x40
#define ADDR_WATER_PHYSICS_VTABLE 999
'''+drive_type+r'''
typedef struct {int enabled,task,targetFlag;}BotBrain;
typedef struct {int mode;}BotNav;
typedef struct {float maxSeen;}VehTrack;
typedef struct {int w,h;float cell;}NavGrid;
typedef char (*EnterVehicle_t)(void*,void*,int);
static struct {unsigned frame;int botCount;char *bots[MAX_BOTS];float botMove[MAX_BOTS][3];}host,*H=&host;
static DriveState g_drive[MAX_BOTS];static BMSteering g_enhancedSteer[MAX_BOTS];
static struct{int phase;}g_air[MAX_BOTS] __attribute__((unused));
static struct{char *vehicle;unsigned until;}g_fleet[MAX_BOTS] __attribute__((unused));
static unsigned char g_driving[MAX_BOTS],mask[16],*g_driveMask=mask,*g_boatMask;
static unsigned g_driveCooldown[MAX_BOTS],g_rayFlags;static char g_lastHitRest[32];
static BMConfig g_enhanced;static BotBrain brains[MAX_BOTS];static BotNav navs[MAX_BOTS];
static struct{int job;}g_armor[MAX_BOTS];
#define ARMOR_NORMAL 0
static char players[MAX_BOTS][128] __attribute__((aligned(8)));
static float soldiers[MAX_BOTS][3],vehicleHealth,xf[16],lastTarget[3];
static char *controlled[MAX_BOTS];static int stops,exits,routeBlocked,routeDeferred,terrainMissing,maskMissing;
static int g_driveOff,g_lastPlanDeferred,*g_landLabels;
static enum BMMode g_mode=BM_MODE_CQ;
static const float *objective_pos(int f){(void)f;return lastTarget;}
static int mode_is_cq(void){return 1;}
static int objective_count(void){return 4;}
static int drive_progress_tick(int i,DriveState*d,const float*p){(void)i;(void)d;(void)p;return 0;}
static int drive_goal_watch(int i,DriveState*d,const float*p){(void)i;(void)d;(void)p;return 0;}   /* AltBotMod V17 */
__attribute__((unused)) static void make_way_tick(int i,char*s){(void)i;(void)s;}__attribute__((unused)) static void panel_check_tick(int i){(void)i;}   /* AltBotMod V18 */
__attribute__((unused)) static void swim_steer_tick(int i,char*s){(void)i;(void)s;}   /* AltBotMod V30 */
__attribute__((unused)) static void vring_dump(int i){(void)i;}   /* AltBotMod V30.1 */
__attribute__((unused)) static void breach_tick(int i,char*s){(void)i;(void)s;}   /* AltBotMod V32 */
static double g_subMs[4] __attribute__((unused)), g_subMax[4] __attribute__((unused));   /* AltBotMod V31.1 */
static int drive_director_tick(int i,DriveState*d,const float*p){(void)i;(void)d;(void)p;return 0;}
static int drive_clear_departure(int i,DriveState*d,const float*p){(void)i;(void)d;(void)p;return 0;}
static int __attribute__((unused)) drive_live_exit(int i,DriveState*d,const float*p,float*out){(void)i;(void)d;(void)p;(void)out;return 0;}
static int drive_reverse_tick(int i,DriveState*d,const float*p){(void)i;(void)d;(void)p;return 0;}
static int drive_unstick_tick(int i,DriveState*d,const float*p){(void)i;(void)d;(void)p;return 0;}
static int trace_vehicle_route(int b,int f,const float*fp,const float*vp,float*pts,int m,int*pa,float*l,int alt){(void)b;(void)f;(void)fp;(void)vp;(void)pts;(void)m;(void)pa;(void)l;(void)alt;return 0;}
static int drive_route_fallback(int i,DriveState*d,const float*vp){(void)i;(void)d;(void)vp;return 0;}   /* AltBotMod V14 (altbotmod_v14_checks) */
static int route_exit_tick(int i,DriveState*d,const float*vp){(void)i;(void)d;(void)vp;return 0;}   /* AltBotMod V15 (altbotmod_v15_checks) */
static unsigned g_defendUntil[MAX_BOTS] __attribute__((unused));   /* V15 */
static int tactic_segment(int i,const float*a,const float*b){(void)i;(void)a;(void)b;return 1;}
static int vehicle_crushable_hit(void){return 0;}
static int drive_hit_vehicle(const void*b,char*o){(void)b;(void)o;return 0;}   /* AltBotMod V24 */
static int drive_route_join(int i,DriveState*d,const float*vp,int*t){(void)i;(void)d;(void)vp;(void)t;return 0;}   /* AltBotMod V25 (altbotmod_v25_checks) */
static unsigned g_replanStorms;   /* AltBotMod V26 */
static unsigned g_lanePasses __attribute__((unused)), g_driveDrops;   /* AltBotMod V29 */
static void drive_lane_scan(int i,DriveState*d,const float*vp,const float*xf,const BMVehicleProfile*p){(void)i;(void)vp;(void)xf;(void)p;d->laneLimit=99.0f;d->laneBlocker=NULL;d->laneAt=H->frame?H->frame:1;}   /* AltBotMod V29 (altbotmod_v29_checks) */
static int drive_straight_route(int i,DriveState*d,const float*vp,int boat){(void)i;(void)d;(void)vp;(void)boat;return 0;}
static void drive_coast(int i,DriveState*d){if(H->frame<d->coastUntil && d->speed>3.0f && d->frontNear>15.0f && H->frame>=d->reverseUntil && H->frame>=d->backOutUntil){H->botMove[i][0]=d->speed>9.0f?0.0f:0.3f;H->botMove[i][1]=0;H->botMove[i][2]=0;d->checkAt=H->frame;return;}memset(H->botMove[i],0,sizeof H->botMove[i]);}
static int objective_wanted(int t,int f){(void)t;(void)f;return 1;}
static int mode_vehicle_target(int i,const float*p){(void)i;(void)p;return -1;}
static void nav_cancel_path(int i){(void)i;}
static NavGrid grid={1000,1000,2},*gridPtr=&grid;
static BotBrain *bot_brain(int i){return &brains[i];}static BotNav *bot_nav(int i){return &navs[i];}
static char *player_vehicle(char *p){for(int i=0;i<H->botCount;i++)if(p==players[i])return controlled[i];return NULL;}
static int player_team(char *p){return *(int*)(p+8);}static int player_squad(char *p){(void)p;return 1;}
static int level_ready(void){return 1;}static int vehicle_ok(char *v){return v==(char*)&vehicleHealth && vehicleHealth>0;}
static int vehicle_is_light(char *v){(void)v;return 1;}static int vehicle_is_boat(char *v){(void)v;return 0;}
static const char *vehicle_name(char *v){(void)v;return "Growler";}
static float *vehicle_pos(char *v,int *team){(void)v;(void)team;return xf+12;}
static float *vehicle_xf(char *v){(void)v;return xf;}
static int driver_seat_free(char *v){return controlled[0]!=v;}static int free_seat_count(char *v){(void)v;return 3;}
static int free_seat(char *v){(void)v;return 1;}
static char enter_vehicle(void *p,void*v,int seat){(void)seat;for(int i=0;i<H->botCount;i++)if(p==players[i])controlled[i]=v;return 1;}
#define ADDR_PLAYER_ENTER_VEHICLE enter_vehicle
static float dist2(const float*a,const float*b){float dx=a[0]-b[0],dz=a[2]-b[2];return dx*dx+dz*dz;}
static void drive_stop(int i,const char*why,int sec){(void)why;(void)sec;g_drive[i].phase=0;controlled[i]=NULL;stops++;exits++;}
static void choose_local_objective(int i,const char*w){(void)i;(void)w;}static void bot_log(const char*f,...){(void)f;}
static int vehicle_finish_approach(int i,char*s,const float*p,float r){(void)i;(void)s;(void)p;(void)r;return 1;}
static int plan_to_vehicle(int i,char*s,const float*p){(void)i;(void)s;(void)p;return 1;}
static int armor_tick(int i,const float*p){(void)i;(void)p;return 0;}
static int armor_arrive(int i,const float*p){(void)i;(void)p;return 0;}
static int armor_route_failed(int i){(void)i;return 0;}static float armor_stop_short(int i){(void)i;return 18;}
static void drive_arrive_objective(int i,DriveState*d,const float*p){(void)i;(void)p;d->phase=4;}
static void drive_hold_objective(int i,DriveState*d,const float*p){(void)i;(void)d;(void)p;}
static void drive_wait_route(int i,DriveState*d,const char*w){(void)i;(void)w;d->phase=10;d->routeRetryAt=H->frame+90;memset(H->botMove[i],0,12);}
static int drive_other_objective(int i,DriveState*d,const float*p,int failed){(void)i;(void)d;(void)p;(void)failed;return 0;}
static void bot_say(int i,int t){(void)i;(void)t;}static void order_reaction(char*p,int t,void*x){(void)p;(void)t;(void)x;}
static NavGrid **nav_slot(void){return &gridPtr;}static const unsigned char *drive_mask(void){return maskMissing?NULL:mask;}
static const unsigned char *boat_mask(void){return mask;}static int plan_budget_left(void){return 1;}
static int ground_below(float x,float z,float top,float bottom,float*out){(void)x;(void)z;(void)top;(void)bottom;if(terrainMissing)return 0;*out=77;return 1;}
static int nav_drive_near(const NavGrid*g,const unsigned char*m,float x,float y,float z,float r,float*out){(void)g;(void)m;(void)r;out[0]=x;out[1]=y;out[2]=z;return 1;}
static int nav_land_around(NavGrid*g,const unsigned char*m,float x,float z,float r,float*mx,float*mz,float*n){(void)g;(void)m;(void)x;(void)z;(void)r;(void)mx;(void)mz;(void)n;return 0;}
static void nav_set_path_mask(const unsigned char*m){(void)m;}static void nav_set_vehicle_width(float w){(void)w;}static void nav_set_vehicle_avoid(const float*p,float r){(void)p;(void)r;}static void nav_set_long_legs(int l){(void)l;}
static int nav_label_at(NavGrid*g,int*l,float x,float y,float z){(void)g;(void)l;(void)x;(void)y;(void)z;return 0;}
static int nav_landing_on(NavGrid*g,const unsigned char*m,int*l,int label,float x,float z,float tx,float tz,float a,float b,float*out){(void)g;(void)m;(void)l;(void)label;(void)x;(void)z;(void)tx;(void)tz;(void)a;(void)b;(void)out;return 0;}
static int nav_mask_nearest(NavGrid*g,const unsigned char*m,float x,float z,float r,float*out){(void)g;(void)m;(void)x;(void)z;(void)r;(void)out;return 0;}
static int nav_snap(NavGrid*g,float x,float y,float z,float*out){(void)g;out[0]=x;out[1]=y;out[2]=z;return 1;}
static int nav_open_water_near(NavGrid*g,const unsigned char*m,float x,float z,float r,float*out){(void)g;(void)m;(void)x;(void)z;(void)r;(void)out;return 0;}
static void nav_mask_strip(NavGrid*g,unsigned char*m,float r,float*k,int n,float d){(void)g;(void)m;(void)r;(void)k;(void)n;(void)d;}
static int ray_cast(const float*a,const float*b,float*h,float*n){(void)a;(void)b;(void)h;(void)n;return 0;}
static int drive_nav_ray(const float*a,const float*b,float*h,float*n){return ray_cast(a,b,h,n);}
static int budgeted_nav_path(int owner,NavGrid*g,int(*ray)(const float*,const float*,float*,float*),float x,float y,float z,float tx,float ty,float tz,float*out,int cap){
 (void)owner;(void)g;(void)ray;(void)cap;lastTarget[0]=tx;lastTarget[1]=ty;lastTarget[2]=tz;
 g_lastPlanDeferred=routeDeferred;if(routeDeferred||routeBlocked)return 0;
 out[0]=x;out[1]=y;out[2]=z;out[3]=tx;out[4]=ty;out[5]=tz;return 2;
}
static int nav_last_partial(void){return 0;}static const char *nav_last_error(void){return "mocked blocked road";}
static double perf_now(void){return 0;}static void *GetProcessHeap(void){return NULL;}
static void *HeapAlloc(void*h,unsigned f,size_t n){(void)h;(void)f;return malloc(n);}static int HeapFree(void*h,unsigned f,void*p){(void)h;(void)f;free(p);return 1;}
static VehTrack track={1000};static VehTrack *vehicle_sample(char*v){(void)v;return &track;}
static int IsBadReadPtr(void*p,int n){(void)p;(void)n;return 1;}static unsigned hit_material(void){return 0;}
static void vehicle_mark_bad(char*v,int sec){(void)v;(void)sec;}static int drive_strike(unsigned char*m,float x,float z,float r){(void)m;(void)x;(void)z;(void)r;return 0;}
static void diagnostic_note(int i,const char*w,char*v){(void)i;(void)w;(void)v;}
static float traffic_speed(int i,const float*p,const float*x,const float*w,float l){(void)i;(void)p;(void)x;(void)w;return l;}
static int traffic_controls(int i,const float*p,const float*x,float*t,float*s){(void)i;(void)p;(void)x;(void)t;(void)s;return 0;}
static int drive_combat_hold(int i,DriveState*d,const float*p){(void)i;(void)d;(void)p;return 0;}
static int drive_repair_hold(int i,DriveState*d,const float*p){(void)i;(void)d;(void)p;return 0;}   /* AltBotMod V11 (altbotmod_v11_checks) */
static int drive_pickup(int i,DriveState*d,const float*p){(void)i;(void)d;(void)p;return 0;}
static int drive_recover_job(int i,DriveState*d,const float*p){(void)i;(void)d;(void)p;return 0;}
static int drive_block_for_job(int i,unsigned char*m,float x,float z,float r,float y){(void)i;(void)y;return drive_strike(m,x,z,r);}
'''
MAIN=r'''
static void reset(int team){
 memset(&host,0,sizeof host);memset(g_drive,0,sizeof g_drive);memset(brains,0,sizeof brains);memset(controlled,0,sizeof controlled);memset(xf,0,sizeof xf);
 H->frame=100;H->botCount=3;bm_defaults(&g_enhanced);g_enhanced.armored=1;routeBlocked=routeDeferred=terrainMissing=maskMissing=stops=exits=0;
 vehicleHealth=1000;xf[0]=xf[5]=xf[10]=xf[15]=1;xf[13]=77;
 for(int i=0;i<3;i++){H->bots[i]=players[i];*(int*)(players[i]+8)=team;*(float**)(players[i]+16)=soldiers[i];memset(soldiers[i],0,12);brains[i].enabled=1;}
 controlled[0]=(char*)&vehicleHealth;DriveState*d=&g_drive[0];d->phase=6;d->at=H->frame;d->veh=controlled[0];d->goal[1]=400;d->goal[2]=1000;
}
int main(void){
 for(int team=1;team<=2;team++){
  reset(team);*(void**)(players[1]+16)=NULL;soldiers[2][2]=1000;drive_tick(0);
  assert(g_drive[0].phase==2 && g_drive[0].boardAt==100 && !exits);
  reset(team);drive_tick(0);assert(g_drive[0].phase==6 && g_drive[1].phase==5 && !H->botMove[0][0]);
  H->frame=189;drive_tick(0);assert(g_drive[0].phase==6);
  H->frame=190;drive_tick(0);assert(g_drive[0].phase==2 && g_drive[0].boardAt==100);
  H->frame++;drive_tick(0);assert(g_drive[0].phase==3 && g_drive[0].partial && g_drive[0].stageActive);
  assert(fabsf(lastTarget[2]-160)<.1f && lastTarget[1]==77 && g_drive[0].goal[1]==400 && g_drive[0].goal[2]==1000);
  H->frame++;drive_tick(0);assert(H->botMove[0][0]>0.9f && !stops && !exits && controlled[0]);
  xf[14]=160;H->frame++;drive_tick(0);assert(g_drive[0].phase==2 && !H->botMove[0][0] && controlled[0]);
  H->frame++;drive_tick(0);assert(g_drive[0].phase==3 && fabsf(lastTarget[2]-320)<.1f);
  H->frame++;drive_tick(0);assert(H->botMove[0][0]>0 && !exits);
 }
 puts("PASS: complete native drive_tick departs for BOTH teams after one three-second living-passenger window, emits throttle and continues checked nearby road legs at local mountain height");
 reset(1);g_drive[0].phase=2;g_drive[0].boardAt=1;H->frame=1000;routeBlocked=1;
 for(int k=0;k<8;k++){H->frame++;drive_tick(0);assert(!H->botMove[0][0] && controlled[0] && !stops && !exits);}
 assert(g_drive[0].phase==10);routeBlocked=0;H->frame=g_drive[0].routeRetryAt;drive_tick(0);drive_tick(0);assert(g_drive[0].phase==3 && controlled[0]);
 reset(1);g_drive[0].phase=2;g_drive[0].boardAt=1;H->frame=1000;routeDeferred=1;drive_tick(0);
 assert(g_drive[0].phase==2 && !g_drive[0].stageCandidate && !H->botMove[0][0] && !exits);
 routeDeferred=0;drive_tick(0);assert(g_drive[0].phase==3);
 reset(1);g_drive[0].phase=2;g_drive[0].boardAt=1;H->frame=1000;terrainMissing=1;
 drive_tick(0);drive_tick(0);assert(g_drive[0].phase==2 && !H->botMove[0][0]);drive_tick(0);assert(g_drive[0].phase==3 && !g_drive[0].stageActive);
 puts("PASS: blocked staged exits keep crew seated without blind throttle; deferred routes preserve candidates; unavailable local terrain falls back to ordinary routing");return 0;
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');a=p.parse_args()
 body=PRE+'\n'+(SRC/'drive_staging.inc').read_text()+'\n'+function(core,'drive_tick')+'\n'+MAIN
 with tempfile.TemporaryDirectory(prefix='bf3-driver-departure-') as tmp:
  f=Path(tmp)/'checks.c';f.write_text(body);exe=f.with_suffix('')
  opts=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'] if a.sanitize else ['-O2']
  subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-but-set-variable','-Wno-unused-function',*opts,'-I',str(SRC),str(f),str(SRC/'enhanced.c'),str(SRC/'air.c'),'-lm','-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
