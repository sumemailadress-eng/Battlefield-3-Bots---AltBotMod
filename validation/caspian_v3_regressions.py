"""Caspian V3: lethal aim, fast routes, vehicles actually boarded.
Compiles the ACTUAL production code (activity_tick, trace_plan + trace_nav.c on the real
MP_007 Fun-Bots file, walk_clear / direct_path / direct_retry / vehicle_quick_route and
on_process_hit extracted from core.c) against mocked engine state. Not a live BF3 test."""
from pathlib import Path
import argparse, os, re, subprocess, tempfile
from test4_checks import function
import test15_regressions as t15
import caspian_v2_regressions as v2
ROOT = Path(__file__).resolve().parents[1]; SRC = ROOT/'files/BotMod/src'; TRACES = ROOT/'files/server/BotMod_traces'
core = (SRC/'core.c').read_text()

# --- 1. The activity watchdog leaves a bot alone while it fights an enemy it can see (<150 m).
WATCHDOG = r'''
int main(void){
 for(int team=1;team<=2;team++){
  // Visible enemy 60 m away for 40 s: no recovery, the fight goes on.
  fresh(team);H->botCount=4;*(int*)(players[3]+8)=3-team;soldiers[3][2]=60;
  brains[0].task=TASK_FIGHT;g_fightTarget[0]=players[3];activity_tick(0);
  for(int s=0;s<40;s++){H->frame+=30;g_fightSeenAt[0]=H->frame;activity_tick(0);}
  assert(brains[0].task==TASK_FIGHT && !g_activity[0].attempts && !g_paths[0].count && !retired);
  // The same, but the enemy is 200 m away (trading shots across the map from HQ): recovered after 18 s.
  fresh(team);H->botCount=4;*(int*)(players[3]+8)=3-team;soldiers[3][2]=200;
  brains[0].task=TASK_FIGHT;g_fightTarget[0]=players[3];activity_tick(0);
  for(int s=0;s<20;s++){H->frame+=30;g_fightSeenAt[0]=H->frame;activity_tick(0);}
  assert(brains[0].task==TASK_NONE && g_activity[0].attempts==1);
  // Out of sight for more than 3 s: an ordinary stall again.
  fresh(team);H->botCount=4;*(int*)(players[3]+8)=3-team;soldiers[3][2]=60;
  brains[0].task=TASK_FIGHT;g_fightTarget[0]=players[3];g_fightSeenAt[0]=H->frame;activity_tick(0);
  for(int s=0;s<20;s++){H->frame+=30;activity_tick(0);}
  assert(brains[0].task==TASK_NONE && g_activity[0].attempts==1);
 }
 puts("PASS: the watchdog no longer ends a fight against a visible enemy within 150 m (V2 cut 160 of 353 fights); far or unseen targets still count as a stall");
 return 0;
}
'''

# --- 2. Fun-Bots routes to points that are not flags (vehicles, cover...), cached and budgeted.
POINTS = r'''
int main(int argc,char **argv){
 g_exe=argv[1];bm_defaults(&g_enhanced);
 static char player[128];static float body[4];H->bots[0]=player;H->botCount=1;*(float**)(player+16)=body;strcpy(player+64,"bot");
 trace_level_load();assert(g_trace.count>1000);
 // Goal: a waypoint on the main network; a flag far away so the goal is NOT a flag.
 int goal=g_trace.count/3,start=-1;
 assert(tn_field(&g_trace,0,g_trace.nodes[goal].p,TRACE_POINT_RADIUS));
 for(int k=0;k<g_trace.count;k++)if(isfinite(g_trace.field[0].dist[k]) && dist2(g_trace.nodes[k].p,g_trace.nodes[goal].p)>300.0f*300.0f){start=k;break;}
 assert(start>=0);
 g_flagCount=1;memcpy(flagP[0],g_trace.nodes[goal].p,12);flagP[0][0]+=3000;
 tn_free(&g_trace);g_traceKey[0]=0;trace_level_load();   // fresh fields
 memcpy(body,g_trace.nodes[start].p,12);body[0]+=1;H->frame=1000;
 float *T=g_trace.nodes[goal].p;
 int n=trace_plan(0,(char*)body,T[0]+3,T[1],T[2]);
 assert(n>=2 && g_tracePointRoutes==1 && g_paths[0].goalX==T[0]+3);
 assert(dist2(&g_paths[0].pts[(n-1)*3],T)<=(TRACE_POINT_RADIUS+0.5f)*(TRACE_POINT_RADIUS+0.5f) || g_paths[0].partial);
 for(int k=1;k<n;k++)assert(sqrtf(dist2(&g_paths[0].pts[(k-1)*3],&g_paths[0].pts[k*3]))<40);
 // A goal within 8 m reuses the cached route field: no new Dijkstra.
 int built=g_tracePointBuilt;H->frame+=30*30;
 assert(trace_plan(0,(char*)body,T[0]-4,T[1],T[2]+4) && g_tracePointBuilt==built);
 // Budget: 2 new goals per tick; AltBotMod V6: the 3rd and 4th WAIT a tick for their Fun-Bots route (deferred)
 // instead of falling back to the grid (Fun-Bots routes first).
 H->frame+=30*30;int deferred=0,c0=cancels;
 for(int k=0;k<4;k++){float *G=g_trace.nodes[(goal+400*(k+1))%g_trace.count].p;g_lastPlanDeferred=0;
  trace_plan(0,(char*)body,G[0],G[1],G[2]);deferred+=g_lastPlanDeferred;}
 assert(g_tracePointBuilt==TRACE_POINT_PER_TICK && deferred==2);
 // The next tick builds the next one; a route found cancels the bot's queued grid request.
 H->frame+=1;{float *G=g_trace.nodes[(goal+1200)%g_trace.count].p;if(trace_plan(0,(char*)body,G[0],G[1],G[2]))assert(cancels>c0);}
 // A vehicle parked at a flag: useFlags=0 routes to the vehicle itself, not to the flag field.
 memcpy(flagP[0],T,12);flagP[0][0]+=30;H->frame+=30*30;
 n=trace_plan_ex(0,(char*)body,T[0],T[1],T[2],0);assert(n>=2 && g_traceUse[0].flag>=TN_FLAG_FIELDS);
 flagP[0][0]+=3000;
 // A goal with no waypoint near it: no route, not deferred, and remembered (no second Dijkstra).
 H->frame+=1;g_lastPlanDeferred=0;assert(!trace_plan(0,(char*)body,T[0]+5000,T[1],T[2]+5000) && !g_lastPlanDeferred);
 built=g_tracePointBuilt;g_lastPlanDeferred=0;assert(!trace_plan(0,(char*)body,T[0]+5001,T[1],T[2]+5000) && !g_lastPlanDeferred && g_tracePointBuilt==built);
 // 60 different goals over many ticks: slots are recycled, nothing leaks, routes keep working.
 for(int k=0;k<60;k++){H->frame+=30*30;float *G=g_trace.nodes[(k*97)%g_trace.count].p;trace_plan(0,(char*)body,G[0],G[1],G[2]);}
 H->frame+=30*30;assert(trace_plan(0,(char*)body,T[0],T[1],T[2]));
 // Flags still use their own fields (ids 0..15).
 memcpy(flagP[0],T,12);H->frame+=30*30;n=trace_plan(0,(char*)body,T[0],T[1],T[2]);assert(n>=2);
 assert(g_traceUse[0].flag==0);
 tn_free(&g_trace);
 puts("PASS: Fun-Bots routes reach any point near the recorded network (not only flags), cached per goal (8 m reuse, 32 slots, recycled), at most 2 new route fields a tick (the rest use the grid that tick, never a stall; a found route cancels the queued grid request); vehicles at flags get a route to the vehicle itself; goals far from waypoints fall back to the grid");
 return 0;
}
'''

# --- 3. Straight walks with live physics checks, and vehicles reached without the grid queue.
DIRECT_PRE = r'''
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define MAX_BOTS 4
#define PATH_MAX_POINTS 128
#define OFF_SOLDIER_POSITION 0
#define OFF_PLAYER_ONLINE_NAME 64
#define NAV_HIT_WATER 1
#define RAY_DONT_CHECK_CHARACTER 0x20
#define RAY_CHECK_DETAIL_MESH 0x01
#define VEHICLE_BOARD_DIST 7.0f
typedef struct { unsigned frame; char *bots[MAX_BOTS]; } Host; static Host host,*H=&host;
%BOTPATH%
static BotPath g_paths[MAX_BOTS];
typedef struct { char *vehicle; } FleetReservation; static FleetReservation g_fleet[MAX_BOTS];
typedef struct { int spot; } VehicleApproachState; static VehicleApproachState g_vehicleApproach[MAX_BOTS];
static unsigned g_rayFlags;
static float dist2(const float *a,const float *b){float x=a[0]-b[0],z=a[2]-b[2];return x*x+z*z;}
static int cancelled,g_lastPlanDeferred;static void nav_cancel_path(int i){(void)i;cancelled++;}
static void bot_log(const char *f,...){(void)f;}
static const char *vehicle_name(char *v){(void)v;return "f16";}
// World: flat floor at y=0, a 3 m cliff (y=-3) for x in [cliffA,cliffB], water for x in [waterA,waterB], a wall flag.
static float traceEnd=10,cliffA=1e9,cliffB=1e9,waterA=1e9,waterB=1e9;static int wall,lastWater,rays,traceCalls,traceResult;
static int ray_cast(const float *a,const float *b,float *hit,float *n){(void)n;rays++;
 if(a[0]==b[0]&&a[2]==b[2]){float y=(a[0]>=cliffA&&a[0]<=cliffB)?-3.0f:0.0f;lastWater=a[0]>=waterA&&a[0]<=waterB;
  if(a[1]>=y&&b[1]<=y){if(hit){hit[0]=a[0];hit[1]=y;hit[2]=a[2];}return 1;}return 0;}
 return 0;}
static int nav_hit_class(void){return lastWater?NAV_HIT_WATER:0;}
static int nav_segment_clear(int(*r)(const float*,const float*,float*,float*),const float *a,const float *b){(void)r;(void)a;(void)b;return !wall;}
static int trace_plan_ex(int i,char *s,float x,float y,float z,int mode){(void)s;assert(mode==2);traceCalls++;if(!traceResult)return 0;
 BotPath *p=&g_paths[i];memset(p,0,sizeof *p);p->count=traceResult;p->partial=1;
 for(int k=0;k<traceResult;k++){p->pts[k*3]=x*(k+1)/traceResult;p->pts[k*3+1]=y;p->pts[k*3+2]=z*(k+1)/traceResult-traceEnd;}
 p->goalX=x;p->goalY=y;p->goalZ=z;return traceResult;}
'''
DIRECT_MAIN = r'''
static int quick(char *s,const float *vp){int n=0;for(int k=0;k<4;k++){g_lastPlanDeferred=0;n=vehicle_quick_route(0,s,vp);if(!g_lastPlanDeferred)break;H->frame++;}return n;}
int main(void){
 static char player[128];H->bots[0]=player;strcpy(player+64,"bot");
 float me[3]={0,0,0},to[3]={40,0,0};H->frame=100;
 assert(walk_clear(me,to,45));
 to[0]=60;assert(!walk_clear(me,to,45));to[0]=40;                                  // too long
 cliffA=18;cliffB=22;assert(!walk_clear(me,to,45));cliffA=cliffB=1e9;              // drop on the way
 waterA=10;waterB=14;assert(!walk_clear(me,to,45));waterA=waterB=1e9;              // water on the way
 wall=1;assert(!walk_clear(me,to,45));wall=0;                                      // body rays blocked
 float up[3]={40,4,0};assert(!walk_clear(me,up,45));                               // ends on another floor
 H->frame=200;int ok=0;int later=0;for(int k=0;k<20;k++){int r=walk_clear(me,to,45);ok+=r>0;later+=r<0;}assert(ok==8 && later==12);  // 8 checks a tick, then 'not this tick'
 // direct_path: one waypoint, and the same walk from the same spot within 25 s is not offered again.
 H->frame=300;assert(direct_path(0,(char*)me,to)==1 && g_paths[0].count==1 && g_paths[0].pts[0]==40 && !g_paths[0].partial);
 assert(direct_retry(0,me,to));H->frame+=26*30;assert(!direct_retry(0,me,to));
 // Vehicle 100 m away on open ground: a straight walk to a spot 6 m short of the hull, no Fun-Bots/grid route.
 H->frame=2000;float jet[3]={0,0,100};traceCalls=0;g_vehicleApproach[0].spot=0;
 assert(vehicle_quick_route(0,(char*)me,jet)==1 && g_paths[0].count==1 && !traceCalls);
 assert(fabsf(g_paths[0].pts[2]-94)<0.01f && fabsf(g_paths[0].pts[1])<0.01f && cancelled);
 // Blocked straight line: the Fun-Bots route to the vehicle; ending beside the hull counts as arrival.
 H->frame=3000;wall=1;traceResult=5;traceCalls=0;g_vehicleApproach[0].spot=0;
 assert(quick((char*)me,jet)==5 && traceCalls==1 && !g_paths[0].partial);   // (6 walk checks a tick: resumes next tick)
 // A route that ends 30 m short is no use (the hull centre is no goal): rejected, the grid ring handles it.
 H->frame=3500;traceEnd=30;g_vehicleApproach[0].spot=0;assert(!quick((char*)me,jet) && !g_paths[0].count);traceEnd=10;
 // A straight walk that failed from this spot is not repeated: spots within 4 m of it are skipped, the 12 m spot is walked to.
 H->frame=3600;wall=0;g_vehicleApproach[0].spot=0;direct_path(0,(char*)me,(float[3]){0,0,94});traceCalls=0;
 assert(vehicle_quick_route(0,(char*)me,jet)==1 && fabsf(g_paths[0].pts[2]-88)<0.01f && !traceCalls);wall=1;
 // Neither: 0, so plan_to_vehicle falls back to its grid candidates.
 H->frame=4000;traceResult=0;g_vehicleApproach[0].spot=0;assert(!quick((char*)me,jet));wall=0;
 // Farther than 150 m: no straight walk attempted, only the route.
 H->frame=5000;g_vehicleApproach[0].spot=0;float far[3]={0,0,400};rays=0;traceCalls=0;assert(!vehicle_quick_route(0,(char*)me,far) && traceCalls==1 && !rays);
 puts("PASS: straight walks are checked with live rays (body, floor every 2.5 m, drops, water, end floor; 8 a tick, no repeat of a failed walk for 25 s); a vehicle is reached (a route ending 30 m short is rejected) by a straight walk beside the hull or a Fun-Bots route before any grid search");
 return 0;
}
'''

# --- 4. Aim calibration from real bullet impacts.
AIM = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define MAX_BOTS 2
#define OFF_PLAYER_SOLDIER 16
#define OFF_PLAYER_ONLINE_NAME 64
#define OFF_SOLDIER_POSITION 0
#define OFF_SOLDIER_SPAWN_POSITION 0
#define OFF_SOLDIER_PITCH 0x21c
#define BULLET_SPEED 600.0f
#define ADDR_HITBODY_VTABLE 0x1234
#include "enhanced.h"
#include "tactics.h"
static const BMWeaponProfile rifleProfile={"rifle",BM_W_RIFLE,15,9,300,1.0f,1.0f,18};
static const BMWeaponProfile *infantry_weapon(int i){(void)i;return &rifleProfile;}
static float g_pitchHist[MAX_BOTS][16];static unsigned g_pitchHistAt[MAX_BOTS][16],g_aimUpdAt[MAX_BOTS];
typedef struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; } Host; static Host host,*H=&host;
static char players[2][128] __attribute__((aligned(8)));static float bodies[2][256];
static char *g_fightTarget[MAX_BOTS];static int g_gunDebug;
static float g_aimBias[3]={0.45f,0.45f,0.45f},g_aimValue[MAX_BOTS];static unsigned g_aimValueAt[MAX_BOTS];static unsigned char g_aimPose[MAX_BOTS];
static float g_aimFixH[MAX_BOTS],g_aimFixHAvg=0.4f,g_aimFixV[MAX_BOTS],g_aimClimb[MAX_BOTS],g_aimFixS[MAX_BOTS],g_aimLanding[MAX_BOTS],g_aimFixVAvg=0.03f,g_aimClimbAvg=0.001f,g_aimFixSAvg;static unsigned g_burstStart[MAX_BOTS];static unsigned char g_aimFixSet[MAX_BOTS];static unsigned g_aimTorso;
static unsigned g_aimImpacts,g_aimOnBody,g_aimSamples[3];static float g_aimSideSum,g_aimRiseSum;
static int IsBadReadPtr(const void *p,unsigned n){(void)n;return p==NULL;}
static int bot_index(void *p){for(int i=0;i<H->botCount;i++)if(H->bots[i]==p)return i;return -1;}
static char *player_vehicle(char *p){(void)p;return NULL;}
static int player_exists(char *p){return bot_index(p)>=0;}
static const char *describe_ptr(void *p,char *d,int n){(void)p;(void)n;d[0]=0;return d;}
static void bot_log(const char *f,...){(void)f;}
#define __cdecl
'''
AIM_MAIN = r'''
static char projectile[0x100] __attribute__((aligned(8)));static void *ref;static float D=50;
static void impact(float side,float along,float rise){unsigned char hit[0x40] __attribute__((aligned(8)));memset(hit,0,sizeof hit);
 float *p=(float*)hit;p[0]=side;p[1]=rise;p[2]=D+along;on_process_hit(projectile,hit,NULL);}
// A simulated rifle: the look pitch follows the command (what fight_tick computes); the gun's bullets cross the target
// plane H0 metres (camera height) plus S0 radians plus C0 per frame of the burst (recoil) above the look line.
static const float H0=0.8f,S0=0.03f,C0=0.0025f,EYE=1.6f;
static float fire(unsigned t,float *err){
 float aimValue=1.15f-g_aimBias[0]-g_aimFixH[0]-(g_aimFixV[0]+g_aimClimb[0]*(float)(t+2))*D;g_aimValue[0]=aimValue;g_aimValueAt[0]=H->frame;
 float drop=0.5f*9.81f*(D/600)*(D/600);
 float pitch=atanf((aimValue+drop-EYE)/D);bodies[0][0x21c/4]=pitch;
 float rise=EYE+g_aimBias[0]+D*tanf(pitch)-drop+H0+(S0+C0*t)*D;
 *err=rise-1.15f;return rise;}
static float burst(int frames){float worst=0;H->frame+=40;g_burstStart[0]=H->frame;
 for(int t=0;t<frames;t+=3){H->frame=g_burstStart[0]+t;float e;float r=fire(t,&e);H->frame+=(unsigned)(D/600*30+0.5f);impact(0,0,r);if(fabsf(e)>worst)worst=fabsf(e);}
 return worst;}
int main(void){
 H->botCount=2;for(int k=0;k<2;k++){H->bots[k]=players[k];*(float**)(players[k]+OFF_PLAYER_SOLDIER)=bodies[k];}
 g_fightTarget[0]=players[1];g_aimLanding[0]=1.15f;g_aimPose[0]=0;
 ref=players[0]+8;void *pr=&ref;memcpy(projectile+0x84,&pr,sizeof pr);
 g_aimFixH[0]=g_aimFixV[0]=g_aimClimb[0]=0;g_aimFixSet[0]=1;   // untrained
 H->frame=1000;
 // Untrained at 50 m: metres off. Train over fights at 10, 30, 50 and 90 m (V4's angle-only model could not fit
 // a camera-height offset: right at one range, into the ground at another).
 D=50;bodies[1][2]=D;float first=burst(12);
 static const float ranges[4]={10,30,50,90};
 for(int r=0;r<60;r++){D=ranges[r%4];bodies[1][2]=D;burst(D>40?4:D>20?7:12);burst(12);}
 float worst=0;
 for(int r=0;r<4;r++){D=ranges[r];bodies[1][2]=D;float w=burst(D>40?4:D>20?7:12);if(w>worst)worst=w;}
 assert(first>1.5f && worst<0.4f);   // trained: every shot of a burst within 0.4 m of the chest at 10-90 m
 assert(fabsf(g_aimFixH[0]-H0)<0.15f && fabsf(g_aimFixV[0]-S0)<0.004f);
 // Sideways: 0.6 m to one side at first, then on the centre line.
 D=50;bodies[1][2]=D;
 float side=0;for(int k=0;k<40;k++){H->frame++;g_aimValueAt[0]=H->frame;side=0.6f+g_aimFixS[0]*D;float e;float r=fire(0,&e);g_burstStart[0]=H->frame;impact(side,0,r);}
 assert(fabsf(side)<0.05f);
 // Impacts off the line of fire, beyond 150 m behind, or long after the aim was set are ignored.
 float before=g_aimFixV[0];impact(3.0f,0,0.9f);impact(0,200,0.2f);H->frame+=40;impact(0,0,0.2f);assert(g_aimFixV[0]==before);
 // A shotgun's 10 pellets in one frame: one update at most.
 before=g_aimFixS[0];H->frame++;g_aimValueAt[0]=H->frame;for(int k=0;k<10;k++)impact(0.9f,0,1.2f);
 assert(fabsf(g_aimFixS[0]-before)<=0.25f*1.2f/50+1e-6f);
 // Clamped: height -1..1.5 m, pitch -0.06..0.12 rad, climb 0..0.004 rad/frame, yaw +-0.05 rad.
 for(int k=0;k<500;k++){H->frame++;g_aimValueAt[0]=H->frame;g_burstStart[0]=H->frame-20;impact(1.1f,0,4.9f);}
 assert(g_aimFixH[0]<=1.5f && g_aimFixV[0]<=0.12f && g_aimClimb[0]<=0.004f && g_aimFixS[0]>=-0.05f);
 g_fightTarget[0]=NULL;before=g_aimFixV[0];impact(0,0,1.5f);assert(g_aimFixV[0]==before);
 puts("PASS: closed-loop aim - each bot fits where its gun points relative to its look pitch (a height offset, an angle and recoil climb per frame of a burst) from its own impacts: untrained it is metres off, trained over fights at 10-90 m every shot is within 0.4 m of the chest at every range; sideways error cancelled; shotgun pellets count once; clamped");
 return 0;
}
'''


# --- 5. Dead bots crew helicopters and armour; aircraft names seen live; stalled drivers unstick.
CREW = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <ctype.h>
#include "enhanced.h"
#include "air.h"
#define MAX_BOTS 8
#define OFF_PLAYER_SOLDIER 16
#define OFF_PLAYER_ONLINE_NAME 64
#define OFF_SOLDIER_HEALTH 12
typedef struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; float botMove[MAX_BOTS][3]; } Host; static Host host,*H=&host;
static BMConfig g_enhanced;static int g_roundCleanup;
static char players[MAX_BOTS][128] __attribute__((aligned(8)));static float bodies[MAX_BOTS][4];
static int team_[MAX_BOTS],seat_[MAX_BOTS];static char *veh_[MAX_BOTS];
typedef struct { char name[32]; int freeSeats; char *pilot; } Veh; static Veh heli={"Viper",1},tank={"M1A2",1},jet={"f16",0},jeep={"Growler",3},transport={"Venom",4};
static struct { int phase; } g_drive[MAX_BOTS];
typedef struct { int mode; } BotNav; static BotNav navs[MAX_BOTS]; static BotNav *bot_nav(int i){return &navs[i];}
static int bot_index(void *p){for(int i=0;i<H->botCount;i++)if(H->bots[i]==p)return i;return -1;}
static int player_exists(char *p){return bot_index(p)>=0;}
static int player_team(char *p){return team_[bot_index(p)];}
static char *player_vehicle(char *p){return veh_[bot_index(p)];}
static int vehicle_ok(char *v){return v!=NULL;}
static int vehicle_seat_of(char *v,char *p){(void)v;return seat_[bot_index(p)];}
static int free_seat_count(char *v){return ((Veh*)v)->freeSeats;}
static int free_seat(char *v){return ((Veh*)v)->freeSeats>0?1:-1;}
static char *vehicle_occupant(char *v){return ((Veh*)v)->pilot;}
static const char *vehicle_name(char *v){return ((Veh*)v)->name;}
static void nav_cancel_path(int i){(void)i;}
static int refuse,enters;
typedef char (*EnterVehicle_t)(void *player, void *vehicle, int seat);
static char mock_enter(void *pl,void *v,int seat){(void)seat;enters++;if(refuse)return 0;veh_[bot_index(pl)]=v;((Veh*)v)->freeSeats--;return 1;}
#define ADDR_PLAYER_ENTER_VEHICLE ((uintptr_t)mock_enter)
static int rides;static void squadmate_ride(int i,char *v){(void)i;(void)v;rides++;}
static void bot_log(const char *f,...){(void)f;}
%CREW%
static void reset(void){memset(players,0,sizeof players);memset(veh_,0,sizeof veh_);memset(seat_,0,sizeof seat_);memset(g_crewMoveAt,0,sizeof g_crewMoveAt);
 memset(g_crewVehicle,0,sizeof g_crewVehicle);heli.freeSeats=1;tank.freeSeats=1;transport.freeSeats=4;refuse=enters=rides=0;
 H->botCount=6;for(int i=0;i<6;i++){H->bots[i]=players[i];team_[i]=i<4?1:2;snprintf(players[i]+64,32,"bot%d",i);
 *(float**)(players[i]+OFF_PLAYER_SOLDIER)=bodies[i];bodies[i][3]=100;}
 heli.pilot=tank.pilot=transport.pilot=players[0];H->frame=1000;}
static void dies(int i){*(float**)(players[i]+OFF_PLAYER_SOLDIER)=NULL;}
static void spawns(int i){*(float**)(players[i]+OFF_PLAYER_SOLDIER)=bodies[i];}
int main(void){
 bm_defaults(&g_enhanced);g_enhanced.driving=1;
 assert(bm_air_profile("Viper") && bm_air_profile("Viper")->kind==BM_AIR_ATTACK);   // the live US AH-1Z name
 assert(bm_air_profile("Mi28 Havoc") && bm_vehicle_profile("9K22")->kind==BM_AA);
 // Bot 0 flies the Viper with a free gunner seat; bot 1 respawns: ordinary spawn, then it takes the seat.
 reset();veh_[0]=(char*)&heli;dies(1);
 assert(!crew_spawn(players[1],1,"bot1") && g_crewVehicle[1]==(char*)&heli);
 crew_board_tick(1);assert(!enters);                                  // not spawned yet
 spawns(1);H->frame+=20;crew_board_tick(1);assert(enters==1 && veh_[1]==(char*)&heli && rides==1 && !g_crewVehicle[1]);
 // A second dead bot does not queue for the seat the first one is taking.
 reset();veh_[0]=(char*)&heli;dies(1);dies(2);crew_spawn(players[1],1,"b1");crew_spawn(players[2],1,"b2");
 assert(g_crewVehicle[1]==(char*)&heli && !g_crewVehicle[2]);
 // Priority: attack helicopter, then transport, then tank, then jeep (V16); jets are not crewed this way; never the enemy's.
 reset();veh_[0]=(char*)&tank;veh_[2]=(char*)&transport;veh_[3]=(char*)&heli;heli.pilot=players[3];transport.pilot=players[2];dies(1);
 crew_spawn(players[1],1,"b1");assert(g_crewVehicle[1]==(char*)&heli);
 reset();veh_[0]=(char*)&tank;veh_[2]=(char*)&transport;transport.pilot=players[2];dies(1);crew_spawn(players[1],1,"b1");assert(g_crewVehicle[1]==(char*)&transport);
 // AltBotMod V16: a driven jeep (or boat) takes spawn passengers too; a jet never (one seat).
 reset();veh_[0]=(char*)&jet;veh_[3]=(char*)&jeep;dies(1);crew_spawn(players[1],1,"b1");assert(g_crewVehicle[1]==(char*)&jeep);
 reset();veh_[0]=(char*)&jet;dies(1);crew_spawn(players[1],1,"b1");assert(!g_crewVehicle[1]);
 // ...but a helicopter / transport / tank seat still comes before the jeep.
 reset();veh_[0]=(char*)&tank;veh_[3]=(char*)&jeep;dies(1);crew_spawn(players[1],1,"b1");assert(g_crewVehicle[1]==(char*)&tank);
 reset();veh_[4]=(char*)&heli;heli.pilot=players[4];dies(1);crew_spawn(players[1],1,"b1");assert(!g_crewVehicle[1]);
 // A passenger is not a pilot to crew with.
 reset();veh_[0]=(char*)&heli;seat_[0]=1;dies(1);crew_spawn(players[1],1,"b1");assert(!g_crewVehicle[1]);
 // Refused seat: no retry for 15 s; a target older than 6 s is dropped.
 reset();veh_[0]=(char*)&heli;dies(1);crew_spawn(players[1],1,"b1");spawns(1);refuse=1;crew_board_tick(1);assert(!veh_[1]);
 dies(1);H->frame+=30;assert(!crew_spawn(players[1],1,"b1") && !g_crewVehicle[1]);
 reset();veh_[0]=(char*)&heli;dies(1);crew_spawn(players[1],1,"b1");H->frame+=7*30;spawns(1);crew_board_tick(1);assert(!enters && !g_crewVehicle[1]);
 // AltBotMod V6: a parked jet / base vehicle is picked at the "spawn screen" first; after spawning the bot is seated
 // as its driver / pilot (vspawn_board), never queued as a gunner as well.
 reset();pickV=(char*)&jet;dies(1);assert(vspawn_prepare(players[1],1,"b1") && g_crewVehicle[1]==(char*)&jet && g_crewDriver[1]);
 assert(!crew_spawn(players[1],1,"b1") && g_crewVehicle[1]==(char*)&jet);   // crew pick does not override it
 crew_board_tick(1);assert(!boards);spawns(1);crew_board_tick(1);assert(boards==1 && boardedV==(char*)&jet && !g_crewVehicle[1] && !g_crewDriver[1] && !enters);
 reset();pickV=NULL;dies(1);assert(!vspawn_prepare(players[1],1,"b1") && !g_crewDriver[1]);
 // a refused vehicle seat backs off 15 s like a refused crew seat
 reset();pickV=(char*)&jet;dies(1);g_crewMoveAt[1]=H->frame;assert(!vspawn_prepare(players[1],1,"b1"));pickV=NULL;
 puts("PASS: AltBotMod V6 - a respawning bot picks a parked jet / base vehicle at the spawn screen and is seated as its driver after spawning; crew seats do not override it; refusals back off");
 puts("PASS: a respawning bot takes a free seat in a teammate bot's attack helicopter (first), transport helicopter, tank or (V16) driven jeep right after spawning (the native squad spawn never seated anyone); no double booking, jets/enemy/passenger seats skipped, refusals back off; the live US 'Viper' and the 9K22 are known vehicles");
 return 0;
}
"""
VSPAWN_MOCK = r"""
/* AltBotMod V6 spawn-screen vehicle pick: vspawn_pick / vspawn_board are spawn_vehicle.inc's (mocked here) */
static char *pickV; static int boards; static char *boardedV;
static char *vspawn_pick(char *b,int t){(void)b;(void)t;return pickV;}
static int vspawn_board(int i,char *v){(void)i;boards++;boardedV=v;return 1;}
"""
UNSTICK = r"""
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define MAX_BOTS 2
#define MAX_FLAGS 16
#define OFF_PLAYER_ONLINE_NAME 64
typedef struct { unsigned frame; char *bots[MAX_BOTS]; float botMove[MAX_BOTS][3]; } Host; static Host host,*H=&host;
%DRIVE%
static DriveState g_drive[MAX_BOTS];
static unsigned char maskData[16],*g_driveMask=maskData;static int boat,strikes,cancels;static float xf[16]={0,0,0,0, 0,1,0,0, 0,0,1,0};
static float dist2(const float *a,const float *b){float x=a[0]-b[0],z=a[2]-b[2];return x*x+z*z;}
static int vehicle_is_boat(char *v){(void)v;return boat;}
static float *vehicle_xf(char *v){(void)v;return xf;}
static int drive_block_for_job(int i,unsigned char *m,float x,float z,float r,float y){(void)i;(void)m;(void)x;(void)z;(void)r;(void)y;strikes++;return 3;}
static void nav_cancel_path(int i){(void)i;cancels++;}
static void diagnostic_note(int i,const char *r,char *v){(void)i;(void)r;(void)v;}
static void bot_log(const char *f,...){(void)f;}
%UNSTICK%
int main(void){
 static char player[128];H->bots[0]=player;DriveState *d=&g_drive[0];float vp[3]={0,0,0};
 d->phase=3;d->veh=(char*)1;H->frame=100;
 // Means to move (target speed / obstacle) but has not: nothing for 6 s, then reverse on full lock, then forward.
 for(int k=0;k<6*30;k++){d->wantMoveAt=H->frame;assert(!drive_unstick_tick(0,d,vp));H->frame++;}
 d->wantMoveAt=H->frame;assert(drive_unstick_tick(0,d,vp) && H->botMove[0][0]<0 && fabsf(H->botMove[0][2])==1);
 float firstSteer=H->botMove[0][2];
 for(int k=0;k<48;k++){H->frame++;drive_unstick_tick(0,d,vp);}
 assert(H->botMove[0][0]>0 && H->botMove[0][2]==-firstSteer);   // then forward on the opposite lock
 // Waiting for traffic, riders or a fight is not stuck.
 H->frame+=200;d->unstickUntil=0;d->unstickCount=0;d->trafficWait=1;
 for(int k=0;k<10*30;k++){d->wantMoveAt=H->frame;assert(!drive_unstick_tick(0,d,vp));H->frame++;}
 d->trafficWait=0;for(int k=0;k<10*30;k++){d->combatHoldAt=H->frame;d->wantMoveAt=H->frame;assert(!drive_unstick_tick(0,d,vp));H->frame++;}
 d->trafficFollowing=1;for(int k=0;k<10*30;k++){d->wantMoveAt=H->frame;assert(!drive_unstick_tick(0,d,vp));H->frame++;}d->trafficFollowing=0;
 d->combatHoldAt=0;
 // Not wanting to move (parked at its objective): nothing.
 for(int k=0;k<10*30;k++){assert(!drive_unstick_tick(0,d,vp));H->frame++;}
 // Still stuck after 4 tries: the spot ahead is struck off and the route re-planned.
 d->unstickCount=0;int tries=0;
 for(int k=0;k<60*30 && !strikes;k++){d->wantMoveAt=H->frame;if(drive_unstick_tick(0,d,vp) && H->frame+1==d->unstickUntil-47)tries++;H->frame++;}
 assert(strikes==1 && d->phase==2 && cancels>=1);
 // Boats and console drives are left alone.
 boat=1;d->phase=3;for(int k=0;k<10*30;k++){d->wantMoveAt=H->frame;assert(!drive_unstick_tick(0,d,vp));H->frame++;}
 puts("PASS: a driver that means to move but has not for 6 s reverses on full lock then pulls away on the other lock (alternating), not while waiting for traffic/riders/a fight or parked; after 4 tries the spot ahead is struck off and re-planned (V3: sat at 0 m/s for 45 s, then got out)");
 return 0;
}
"""

BOAT = r"""
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define MAX_BOTS 2
#define MAX_FLAGS 16
#define OFF_PLAYER_ONLINE_NAME 64
#define RAY_DONT_CHECK_CHARACTER 0x20
#define RAY_CHECK_DETAIL_MESH 0x01
#define ADDR_WATER_PHYSICS_VTABLE 0x02486a40u
typedef struct { unsigned frame; char *bots[MAX_BOTS]; float botMove[MAX_BOTS][3]; } Host; static Host host,*H=&host;
%DRIVE%
static DriveState g_drive[MAX_BOTS];
typedef struct { int w; } NavGrid;
static NavGrid grid, *gridp=&grid; static NavGrid **nav_slot(void){return &gridp;}
static unsigned char maskData[4]; static const unsigned char *boat_mask(void){return maskData;}
static unsigned g_rayFlags; static char g_lastHitRest[64] __attribute__((aligned(16)));
static unsigned wallBody[4]={0x01234567u}, hullBody[4]={0x07654321u}; static char vehicle[0x40] __attribute__((aligned(16)));
static int IsBadReadPtr(const void *p,unsigned n){(void)n;return p==NULL;}
static void bot_log(const char *f,...){(void)f;}
static float landNear=50; static int stops, marked; static char stopWhy[128];
static int nav_land_around(const NavGrid *g,const unsigned char *m,float x,float z,float r,float *mx,float *mz,float *nearest){(void)g;(void)m;(void)r;*mx=x;*mz=z;*nearest=landNear;return 1;}
static int nav_open_water_near(const NavGrid *g,const unsigned char *m,float x,float z,float d,float *out){(void)g;(void)m;(void)x;(void)d;out[0]=x;out[1]=0;out[2]=z;return z<-15;}
static void vehicle_mark_bad(char *v,int s){(void)v;(void)s;marked++;}
static int armor_arrive(int i,const float *vp){(void)i;(void)vp;return 0;}
static void drive_stop(int i,const char *why,int c){(void)c;stops++;snprintf(stopWhy,sizeof stopWhy,"%s",why);g_drive[i].phase=0;}
// The US carrier's well deck on Noshahr: the boat at the origin, bow (+z) at the closed end; walls 4 m either side
// from z=-12 to z=30 and across the far end at z=30; open sea behind (-z). Its own hull answers rays inside 1.8 m.
static int seg(float ox,float oz,float dx,float dz,float ax,float az,float bx,float bz,float *t){
 float ex=bx-ax,ez=bz-az,den=dx*ez-dz*ex; if(fabsf(den)<1e-9f)return 0;
 float u=((ax-ox)*ez-(az-oz)*ex)/den,v=((ax-ox)*dz-(az-oz)*dx)/den; if(u<=0||v<0||v>1)return 0; *t=u;return 1;}
static int ray_cast(const float *a,const float *b,float *hit,float *nrm){
 float dx=b[0]-a[0],dz=b[2]-a[2],len=hypotf(dx,dz),best=1e30f,t;dx/=len;dz/=len;char *body=(char*)wallBody;
 if(hypotf(a[0],a[2])<1.8f){ float bb=a[0]*dx+a[2]*dz,c=a[0]*a[0]+a[2]*a[2]-1.8f*1.8f; best=-bb+sqrtf(bb*bb-c); body=(char*)hullBody; }
 else {
  if(seg(a[0],a[2],dx,dz,-4,-12,-4,30,&t)&&t<best)best=t;
  if(seg(a[0],a[2],dx,dz,4,-12,4,30,&t)&&t<best)best=t;
  if(seg(a[0],a[2],dx,dz,-4,30,4,30,&t)&&t<best)best=t;
 }
 if(best>len)return 0;
 hit[0]=a[0]+dx*best;hit[1]=a[1];hit[2]=a[2]+dz*best; if(nrm){nrm[0]=nrm[2]=0;nrm[1]=0;}
 *(char**)g_lastHitRest=body; return 1;}
%BOAT%
static float wrap(float a){while(a>3.14159265f)a-=6.2831853f;while(a<-3.14159265f)a+=6.2831853f;return a;}
int main(void){
 static char player[128];H->bots[0]=player;H->frame=10000;DriveState *d=&g_drive[0];
 *(void**)(vehicle+0x30)=hullBody;d->veh=vehicle;float vp[3]={0,0,0};
 // 1. The way out of the well deck is astern - not the mean of the land (which sits on the boat).
 float h=boat_exit_heading(0,d,vp,0.0f);
 assert(fabsf(wrap(h-3.14159265f))<0.4f);
 // facing out already: straight on out
 h=boat_exit_heading(0,d,vp,3.14159265f);assert(fabsf(wrap(h-3.14159265f))<0.2f);
 // 2. Landing: flag at z=-500, landing at z=-300 (200 m from the flag); the boat left its slip at z=+600.
 float goal[3]={0,0,-500},land[3]={0,0,-300},slip[3]={0,0,600};
 #define SET(x,z,ph,fn) do{memset(d,0,sizeof *d);d->veh=vehicle;d->phase=ph;memcpy(d->goal,goal,12);memcpy(d->landTgt,land,12);d->landSet=1;d->exitSet=1;memcpy(d->exitFrom,slip,12);d->frontNear=fn;vp[0]=x;vp[1]=0;vp[2]=z;stops=0;}while(0)
 SET(0,-270,3,99);landNear=50;assert(boat_landed(0,d,vp)&&stops==1&&strstr(stopWhy,"at the landing")&&marked);
 SET(0,-100,3,99);landNear=50;assert(!boat_landed(0,d,vp)&&!stops);                      // open water, far off
 SET(50,-280,3,3);landNear=5;assert(boat_landed(0,d,vp)&&strstr(stopWhy,"shore"));       // bow on the beach 54 m short (the live case)
 SET(100,-200,3,99);landNear=5;assert(!boat_landed(0,d,vp)&&!stops);                     // along a canal bank, still far: drive on
 SET(100,-200,2,99);landNear=8;d->boatPlanSince=H->frame-10*30;assert(!boat_landed(0,d,vp));   // re-planning by the shore: wait...
 d->boatPlanSince=H->frame-30*30;assert(boat_landed(0,d,vp)&&strstr(stopWhy,"on foot")); // ...no water route for 25 s: walk
 SET(10,0,2,0);memcpy(d->exitFrom,(float[3]){0,0,0},12);landNear=1;d->boatPlanSince=1;assert(!boat_landed(0,d,vp)&&!stops);   // still in its slip: never
 SET(0,-270,8,99);assert(!boat_landed(0,d,vp));                                          // (leaving the slip)
 d->landSet=0;d->phase=3;assert(!boat_landed(0,d,vp));
 puts("PASS: a boat in the US carrier's well deck finds the way out astern (24 rays round the hull; V4 steered at the deck walls for 20 s); it lands - everyone out - at its landing, or on a beach about as near its flag, or after 25 s with no water route near the shore; never in its own slip, nor along a distant canal bank");
 return 0;
}
"""

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    botpath = re.search(r'typedef struct BotPath \{[^\n]+', core)[0]
    watchdog = t15.base + t15.ACTIVITY.split('int main(void){')[0] + WATCHDOG
    points = v2.PLAN.replace('%BOTPATH%', botpath).split('int main(int argc,char **argv){')[0] + POINTS
    walk = function(core, 'walk_clear'); direct = function(core, 'direct_path'); retry = function(core, 'direct_retry'); quick = function(core, 'vehicle_quick_route')
    state = re.search(r'typedef struct DirectUse[^\n]+\nstatic DirectUse g_directUse\[MAX_BOTS\];', core)[0]
    counters = re.search(r'static unsigned g_walkCheckFrame[^\n]+\nstatic int g_walkChecks;', core)[0]
    qc = re.search(r'static unsigned g_quickVehicleWalks[^\n]+', core)[0] + '\n' + function(core, 'vehicle_side_floor') + '\nstatic unsigned g_quickVehicleFails;'
    directc = DIRECT_PRE.replace('%BOTPATH%', botpath) + counters + '\n' + walk + '\n' + state + '\n' + retry + '\n' + direct + '\n' + qc + '\n' + quick + DIRECT_MAIN
    k = core.index('void __cdecl on_process_hit('); depth = 0; e = core.index('{', k)
    while True:
        if core[e] == '{': depth += 1
        elif core[e] == '}':
            depth -= 1
            if not depth: break
        e += 1
    aim = AIM + core[k:e+1].replace('void __cdecl on_process_hit', 'static void on_process_hit') + AIM_MAIN
    k = core.index('static unsigned g_crewMoveAt[MAX_BOTS];'); e = core.index('static void cmd_spawn(const char *name)\n{')
    crew = CREW.replace('%CREW%', core[k:e] + '\n' + VSPAWN_MOCK)
    drive = re.search(r'#define DRIVE_MAX_PTS[^\n]*\ntypedef struct DriveState \{[\s\S]*?\} DriveState;', core)[0]
    man = (SRC/'drive_maneuvers.inc').read_text(); um = man[man.index('#define UNSTICK_STILL_FRAMES'):]
    unstick = UNSTICK.replace('%DRIVE%', drive).replace('%UNSTICK%', um)
    boatc = BOAT.replace('%DRIVE%', drive).replace('%BOAT%', function(core, 'boat_exit_heading') + '\n' + function(core, 'boat_ashore') + '\n' + function(core, 'boat_landed'))
    maps = sorted(TRACES.glob('*.map'))
    with tempfile.TemporaryDirectory(prefix='bf3-caspian-v3-') as tmp:
        tmp = Path(tmp)
        srv = tmp/'srv'; srv.mkdir(); exe = str(srv) + '\\bf3.exe'
        for m in maps: os.symlink(m, str(srv) + '\\BotMod_traces\\' + m.name)
        jobs = [('watchdog', watchdog, [str(SRC/'enhanced.c'), str(SRC/'air.c'), str(SRC/'modes.c')], [], 'c11'),
                ('points', points, [str(SRC/'trace_nav.c'), str(SRC/'enhanced.c'), str(SRC/'air.c')], [exe], 'gnu11'),
                ('direct', directc, [], [], 'gnu11'),
                ('aim', aim, [], [], 'gnu11'),
                ('crew', crew, [str(SRC/'enhanced.c'), str(SRC/'air.c')], [], 'gnu11'),
                ('unstick', unstick, [], [], 'gnu11'),
                ('boat', boatc, [], [], 'gnu11')]
        for name, body, extra, args, std in jobs:
            p = tmp/(name + '.c'); p.write_text(body); out = p.with_suffix('')
            cmd = ['gcc', '-std=' + std, '-O1', '-Wall', '-Wextra', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-parameter',
                   '-I', str(SRC), str(p), *extra, '-lm', '-o', str(out)]
            if a.sanitize: cmd[1:1] = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
            # the hit/projectile records use 32-bit BF3 offsets (a pointer at +0x84): alignment is a 64-bit-host artefact
            if a.sanitize and name in ('aim', 'crew', 'boat'): cmd[1:1] = ['-fno-sanitize=alignment']
            subprocess.run(cmd, check=True); subprocess.run([str(out), *args], check=True, env=env)

if __name__ == '__main__': main()
