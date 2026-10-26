// Exercise the ACTUAL native adapter with mocked engine calls and entities.
// This checks lifecycle/control routing, not BF3 flight physics or replication.
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../files/BotMod/src/enhanced.h"

#include "../files/BotMod/src/modes.h"
#define MAX_BOTS 8
#define MAX_PRESSES 8
#define OFF_PLAYER_SOLDIER 16
#define OFF_PLAYER_ONLINE_NAME 64
#define OFF_SOLDIER_POSITION 0
#define OFF_SOLDIER_SPAWN_POSITION 0
#define OFF_SOLDIER_HEALTH 12
#define OFF_VEHICLE_HEALTH 32
#define OFF_CP_CONTROL_TEAM 0
#define OFF_INPUT_AIM_VALID 0x1dc
#define OFF_INPUT_AIM_YAW 0x1f4
#define OFF_INPUT_AIM_PITCH 0x1f8
#define BOT_TICK_DT (1.0f/30)
#define VEHICLE_BOARD_DIST 7.0f
#define EIA_THROTTLE 0
#define EIA_STRAFE 1
#define EIA_BRAKE 2
#define EIA_YAW 4
#define EIA_PITCH 5
#define EIA_SEAT_YAW 6
#define EIA_FIRE 7
#define EIA_ZOOM 11
#define TASK_NONE 0
#define TASK_FIGHT 8
typedef long LONG;
static LONG g_compRefresh,g_scanPasses;
static LONG InterlockedExchange(LONG *p,LONG v){LONG old=*p;*p=v;return old;}
static LONG InterlockedCompareExchange(LONG *p,LONG v,LONG expected){LONG old=*p;if(old==expected)*p=v;return old;}
#define RAY_DONT_CHECK_CHARACTER 0x20
#define RAY_CHECK_DETAIL_MESH 0x40
typedef void (*SetLevel_t)(void*,int,float);
typedef char (*EnterVehicle_t)(void*,void*,int);
typedef struct { void *entity; unsigned a2,a3,a4; } BotTeamSpawn;
typedef struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; void *botInput[MAX_BOTS]; float botMove[MAX_BOTS][3]; BotTeamSpawn teamSpawn[3]; } Host;
typedef struct { int phase,flag,manual; char *veh; unsigned at,walkUntil; float start[3],goal[3]; } DriveState;
typedef struct { int enabled,task,prevTask,targetFlag,replanAt,enroute,waiting,retryAt,avoidFlag,avoidUntil; char *commander,*prevCommander; } BotBrain;
typedef struct { int mode; } BotNav;
typedef struct { int w,h,phase; float cell,x0,z0; } NavGrid;
typedef struct { float maxSeen; } VehTrack;
typedef struct { int action,frames; } BotPress;
typedef struct { int action; unsigned at; } ScheduledPress;
typedef struct {
    char name[32]; float health; float xf[16]; int team,seats;
    char seatsMemory[5][88] __attribute__((aligned(8)));
    char *entries[5];
} Vehicle;
static Host host,*H=&host;
static DriveState g_drive[MAX_BOTS]; static unsigned char g_driving[MAX_BOTS];
static BMConfig g_enhanced; static int g_driveOff,g_vehInput=1,g_flagCount=1;
static BotBrain brains[MAX_BOTS]; static BotNav navs[MAX_BOTS];
static BotPress g_press[MAX_BOTS][MAX_PRESSES];
static ScheduledPress g_sched[MAX_BOTS][8];
static unsigned g_hitAt[MAX_BOTS];static char *g_fightTarget[MAX_BOTS];static unsigned g_fightSeenAt[MAX_BOTS] __attribute__((unused));
static unsigned g_lastSpawnedAt[MAX_BOTS],g_botFiredAt[MAX_BOTS],g_rayFlags;
static float g_hqPos[3][3],g_hqCpPos[3][3],flags[6][3]={{0,0,500}};
static int g_hqCpKnown[3]; static unsigned g_driveCooldown[MAX_BOTS]; static unsigned char g_gliding[MAX_BOTS];
static char players[MAX_BOTS][128] __attribute__((aligned(8)));
static float soldiers[MAX_BOTS][4]; static char inputs[MAX_BOTS][1024] __attribute__((aligned(8)));
static char *controlled[MAX_BOTS]; static int playerSeats[MAX_BOTS];
static Vehicle vehicle,extraVehicle,thirdVehicle,fourthVehicle; static char *vehicles[8]; static char human[128];
static int targetVisible=1;
static struct {int pending;} g_flagRoute[MAX_BOTS];
static struct{int count,next;}g_paths[MAX_BOTS];
static int g_localFlag[MAX_BOTS];static unsigned g_localUntil[MAX_BOTS];
static struct{unsigned at,epoch;int choice,team;}g_deployActive[MAX_BOTS];
static struct{const BMAirProfile *profile;}g_deployChoice[32];
static int padWater,padObstructed,diagnosticChanges,routeCancels;static void *invalidSpawn;
static char manager[192];static void *playerRows[MAX_BOTS];static int managerEnabled;
static char *player_manager(void){return managerEnabled?manager:NULL;}
static int humanNearby,allowEnter=1,allowSwitch=1,allowPlan=1,engineReady=1,exits,claims,vehicleCount=1;
static int planDeferred,budgetAvailable=1;
static long g_compEpoch,g_roundEpoch;
static char *badVehicle; static unsigned badUntil;
static NavGrid *grid; static VehTrack health={1000};
static int bot_index(char *p) { for(int i=0;i<H->botCount;i++) if(H->bots[i]==p) return i; return -1; }
#include "../files/BotMod/src/rush_policy.h"
static enum BMMode g_mode=BM_MODE_CQ;static int g_rushStage=1;static BMRushState g_rush;
static int mode_is_cq(void){return g_mode==BM_MODE_CQ;}
static int mode_uses_vehicles(void){return mode_is_cq() || g_mode==BM_MODE_RUSH;}
static int player_exists(char *p){return bot_index(p)>=0;}
static void bot_log(const char *format,...) { (void)format; }
static void set_level(void *input,int action,float value) { ((float*)((char*)input+0x10))[action]=value; }
static char enter_vehicle(void *player,void *v,int seat)
{
    int i=bot_index(player); if(!allowEnter || i<0) return 0;
    Vehicle *veh=v; if(seat<0 || seat>=veh->seats || *(char**)(veh->entries[seat]+0x4c)) return 0;
    if(controlled[i] && !allowSwitch) return 0;
    if(controlled[i]) { Vehicle *old=(Vehicle*)controlled[i]; *(char**)(old->entries[playerSeats[i]]+0x4c)=NULL; }
    controlled[i]=v; playerSeats[i]=seat; *(char**)(veh->entries[seat]+0x4c)=player; return 1;
}
#define ADDR_ENTRYINPUT_SET_LEVEL set_level
#define ADDR_PLAYER_ENTER_VEHICLE enter_vehicle
static char *player_vehicle(char *p) { int i=bot_index(p); return i<0 ? NULL : controlled[i]; }
static int vehicle_seat_of(char *v,char *p) { return player_vehicle(p)==v ? playerSeats[bot_index(p)] : -1; }
static int vehicle_seats(char *v,char ***out) { Vehicle *a=(Vehicle*)v; if(!a) return 0; *out=a->entries; return a->seats; }
static int free_seat_count(char *v) { char **e=NULL; int n=vehicle_seats(v,&e),free=0; for(int k=0;k<n;k++) free+=!*(char**)(e[k]+0x4c); return free; }
static int free_seat(char *v) { char **e=NULL; int n=vehicle_seats(v,&e); for(int k=1;k<n;k++) if(!*(char**)(e[k]+0x4c)) return k; return n && !*(char**)(e[0]+0x4c) ? 0 : -1; }
static int driver_seat_free(char *v) { char **e=NULL; return vehicle_seats(v,&e) && !*(char**)(e[0]+0x4c); }
static int vehicle_ok(char *v) { for(int k=0;k<vehicleCount;k++) if(vehicles[k]==v) return ((Vehicle*)v)->health>0; return 0; }
static const char *vehicle_name(char *v) { return v ? ((Vehicle*)v)->name : "?"; }
static float *vehicle_xf(char *v) { return v ? ((Vehicle*)v)->xf : NULL; }
static float *vehicle_pos(char *v,int *team) { if(team) *team=((Vehicle*)v)->team; return ((Vehicle*)v)->xf+12; }
static int vehicle_list(char ***out) { *out=vehicles; return vehicleCount; }
static int vehicle_occupied(char *v) { return free_seat_count(v)<((Vehicle*)v)->seats; }
// Emulate the beta's 32-bit entry offset in a 64-bit fixture using bytes,
// as air_seat_player does. Do not cast a byte arena into a host pointer slot.
static char *vehicle_occupant(char *v) { char **e=NULL;int n=vehicle_seats(v,&e);for(int k=0;k<n;k++){char *pl=NULL;memcpy(&pl,e[k]+0x4c,sizeof pl);if(pl)return pl;}return NULL; }

static BotBrain *bot_brain(int i) { return &brains[i]; }
static BotNav *bot_nav(int i) { return &navs[i]; }
static int player_team(char *p) { return p ? *(int *)(p+8) : 0; }
static int player_squad(char *p) { (void)p; return 1; }
static int squad_objective(int t,int s) { (void)t; (void)s; return 0; }
static int squad_defending(int t,int s){(void)t;(void)s;return 0;}
static int director_target(int i,int *role){(void)i;*role=0;return -1;}
static int flagOwners[6]={2,2,2,2,2,2};
static int flag_owner(int f) { return flagOwners[f]; }
static int controls[6];static char *g_flags[6];static void *g_flagSpawn[6][3];
static float *flag_pos(int f) { return flags[f]; }
static int objective_count(void){return g_flagCount;}
static const float *objective_pos(int f){return flag_pos(f);}
static int objective_wanted(int team,int f){return mode_is_cq()?flag_owner(f)!=team:bm_rush_active(&g_rush,(g_rushStage-1)*2+1+f);}
static int mode_vehicle_target(int i,const float *p){(void)i;(void)p;for(int f=0;f<objective_count();f++)if(objective_wanted(1,f))return f;return -1;}
static int mode_base_present(int stage,int team){(void)stage;(void)team;return 1;}
static void mode_reassign_idle(int i,int f){(void)i;(void)f;}
static NavGrid **nav_slot(void) { return &grid; }
static int level_ready(void) { return engineReady; }
static float dist2(const float *a,const float *b) { float x=a[0]-b[0],z=a[2]-b[2]; return x*x+z*z; }
static int plan_to_vehicle(int i,char *s,const float *p) { (void)i; (void)s; (void)p; claims++; return allowPlan; }
static int plan_budget_left(void) { return budgetAvailable; }
static int vehicle_plan_deferred(int i) { (void)i;return planDeferred; }
static int vehicle_board_close(char *s,const float *p) { return dist2((float*)s,p)<=VEHICLE_BOARD_DIST*VEHICLE_BOARD_DIST && fabsf(((float*)s)[1]-p[1])<=8; }
static int vehicle_finish_approach(int i,char *s,const float *p,float reach){(void)i;return dist2((float*)s,p)<=reach*reach && fabsf(((float*)s)[1]-p[1])<=8;}
static int vehicle_bad(char *v) { return badVehicle==v && H->frame<badUntil; }
static void vehicle_mark_bad(char*v,int sec){badVehicle=v;badUntil=H->frame+sec*30;}
static int cse_valid(void *v) { return v!=NULL && v!=invalidSpawn; }
static int spawn_team(void *v){return v==human?1:v==players[3]?2:0;}
static void *validatedHq[3];
static int hq_is_option(int team,void*e){return team>=1 && team<=2 && validatedHq[team]==e;}
static float *spawn_data_pos(void *v){(void)v;return NULL;}
static void nav_cancel_path(int owner){(void)owner;routeCancels++;}
static int nav_is_water(NavGrid *g,float x,float y,float z){(void)g;(void)x;(void)y;(void)z;return padWater;}
static int nav_snap(NavGrid *g,float x,float y,float z,float *out){(void)g;out[0]=x;out[1]=y;out[2]=z;return 1;}
static int tactic_ray(int i,const float *from,const float *to){(void)i;(void)from;(void)to;return !padObstructed;}
static void diagnostic_note(int i,const char *reason,char *v){(void)i;(void)reason;(void)v;diagnosticChanges++;}
static int flag_pressure(int team,int flag,int except){int n=0;for(int i=0;i<H->botCount;i++)if(i!=except && player_team(H->bots[i])==team && brains[i].targetFlag==flag)n++;return n;}
static void drive_stop(int i,const char *why,int seconds){(void)why;(void)seconds;memset(&g_drive[i],0,sizeof g_drive[i]);}
static int near_origin(const float *p) { return fabsf(p[0])<12 && fabsf(p[2])<12; }
static int human_near(const float *p,float radius) { (void)p; (void)radius; return humanNearby; }
static void end_task(int i,BotBrain *b,const char *why) { (void)why; b->targetFlag=-1; b->task=0; navs[i].mode=0; }
static void end_fight(int i,BotBrain*b,const char*w){end_task(i,b,w);g_fightTarget[i]=NULL;}
static void choose_local_objective(int i,const char *why) { (void)why; brains[i].targetFlag=0; }
static int lastLocalExclude=-2;
static inline void choose_local_objective_except(int i,const char *why,int exclude) { lastLocalExclude=exclude; choose_local_objective(i,why); }
static void gunner_reset(int i) { (void)i; }
static void bot_exit_vehicle(int i,int parachute) { (void)parachute; if(controlled[i]) { Vehicle *v=(Vehicle*)controlled[i]; *(char**)(v->entries[playerSeats[i]]+0x4c)=NULL; controlled[i]=NULL; exits++; } }
static void press_button(int i,int action,int frames) { g_press[i][0].action=action; g_press[i][0].frames=frames; }
static VehTrack *vehicle_sample(char *v) { (void)v; return &health; }
static int gun_can_see(const float *a,const float *b,int veh) { (void)a; (void)b; (void)veh; return targetVisible; }
static void bot_spot(char *a,char *b) { (void)a; (void)b; }
static int ray_cast(const float *from,const float *to,float *hit,float *normal)
{
    (void)normal; if(from[1]<0 || to[1]>0) return 0;
    hit[0]=from[0]; hit[1]=0; hit[2]=from[2]; return 1;
}
#include "../files/BotMod/src/local_order.h"
#include "../files/BotMod/src/fleet_state.h"
#include "../files/BotMod/src/air_native.inc"
#include "../files/BotMod/src/fleet_native.inc"
static void reset(const char *name,int seats)
{
    for(int f=0;f<6;f++){g_flags[f]=(char*)&controls[f];}
    memset(controls,0,sizeof controls);memset(g_flagSpawn,0,sizeof g_flagSpawn);g_mode=BM_MODE_CQ;g_rushStage=1;bm_rush_reset(&g_rush,4,2,0);managerEnabled=0;memset(g_flagRoute,0,sizeof g_flagRoute);memset(&host,0,sizeof host); memset(&vehicle,0,sizeof vehicle); memset(g_air,0,sizeof g_air);
    memset(g_drive,0,sizeof g_drive); memset(g_driving,0,sizeof g_driving); memset(controlled,0,sizeof controlled);
    memset(g_airCooldown,0,sizeof g_airCooldown); memset(g_sched,0,sizeof g_sched); memset(g_press,0,sizeof g_press);
    memset(inputs,0,sizeof inputs); memset(players,0,sizeof players); memset(brains,0,sizeof brains);
    memset(g_fleet,0,sizeof g_fleet); memset(g_fleetBase,0,sizeof g_fleetBase); memset(g_fleetBasePos,0,sizeof g_fleetBasePos);
    memset(g_takeoverSince,0,sizeof g_takeoverSince); memset(g_driveCooldown,0,sizeof g_driveCooldown); memset(g_gliding,0,sizeof g_gliding);
    memset(g_lastSpawnedAt,0,sizeof g_lastSpawnedAt);targetVisible=1;
    memset(g_hitAt,0,sizeof g_hitAt);memset(g_fightTarget,0,sizeof g_fightTarget);
    memset(g_vehicleApproach,0,sizeof g_vehicleApproach);
    memset(g_deploymentIdle,0,sizeof g_deploymentIdle);memset(g_fleetRouteFailure,0,sizeof g_fleetRouteFailure);
    memset(g_deployActive,0,sizeof g_deployActive);memset(g_deployChoice,0,sizeof g_deployChoice);
    memset(g_paths,0,sizeof g_paths);memset(g_localFlag,0,sizeof g_localFlag);memset(g_localUntil,0,sizeof g_localUntil);memset(g_localOrder,0,sizeof g_localOrder);
    padWater=padObstructed=diagnosticChanges=routeCancels=0;invalidSpawn=NULL;memset(validatedHq,0,sizeof validatedHq);g_airLandingProbeFrame=g_airPickupProbeFrame=0;
    g_fleetPriorityUntil=0;
    memset(g_hqPos,0,sizeof g_hqPos); memset(g_hqCpKnown,0,sizeof g_hqCpKnown);
    g_flagCount=1;badVehicle=NULL; badUntil=0; allowSwitch=allowPlan=budgetAvailable=1; planDeferred=0; vehicleCount=1;g_flagCount=1;
    host.frame=990; host.botCount=4; humanNearby=0; allowEnter=engineReady=1; exits=claims=0;
    g_driveOff=0; g_vehInput=1; grid=NULL; bm_defaults(&g_enhanced); g_enhanced.returnToBase=0; g_enhanced.vehicleWalkMeters=6000;   /* (V34: these fleet scenarios predate the mid-round walk cap - altbotmod_v34_checks tests it) */
    // Keep the original narrow-cap fixtures explicit; Test 7 installs 8 ground / 6 air.
    g_enhanced.groundDriversPerTeam=2;g_enhanced.air.pilotsPerTeam=2;
    g_enhanced.air.jets=g_enhanced.air.helicopters=1;
    for(int i=0;i<4;i++) {
        H->bots[i]=players[i]; H->botInput[i]=inputs[i];
        *(int*)(players[i]+8)=1;
        *(float**)(players[i]+OFF_PLAYER_SOLDIER)=soldiers[i]; memset(soldiers[i],0,12);soldiers[i][3]=100;
        strcpy(players[i]+OFF_PLAYER_ONLINE_NAME,"test bot"); brains[i].enabled=1;
    }
    strcpy(vehicle.name,name); vehicle.seats=seats; vehicle.health=1000; vehicle.team=1;
    vehicle.xf[0]=vehicle.xf[5]=vehicle.xf[10]=vehicle.xf[15]=1; vehicle.xf[13]=2; vehicle.xf[14]=5;
    for(int k=0;k<5;k++) vehicle.entries[k]=vehicle.seatsMemory[k]+4;
    vehicles[0]=(char*)&vehicle;
}
int main(void)
{
    NavGrid captureReady={300,300,2,10,-1500,-1500};
    // Idle infantry must leave deployment for either team without another
    // body, a suicide, or interference with combat/human orders/capture duty.
    for(int team=1;team<=2;team++){
        reset("M1Abrams",3);grid=&captureReady;g_flagCount=2;
        for(int i=0;i<4;i++)*(int*)(players[i]+8)=team;
        flagOwners[0]=flagOwners[1]=3-team;flags[0][2]=500;flags[1][2]=700;
        brains[0].targetFlag=-1;deployment_idle_tick(0);H->frame+=540;deployment_idle_tick(0);
        assert(g_localFlag[0]>0 && !brains[0].waiting && !brains[0].enroute && routeCancels==1 && !exits);
        int chosen=g_localFlag[0];brains[0].task=1;H->frame+=540;deployment_idle_tick(0);
        assert(g_localFlag[0]==chosen && routeCancels==1);
        brains[0].task=0;brains[0].targetFlag=0;brains[0].waiting=1;soldiers[0][2]=500;
        deployment_idle_tick(0);H->frame+=540;deployment_idle_tick(0);assert(routeCancels==1 && brains[0].waiting);
    }
    puts("PASS: BOTH teams recover idle deployment infantry; combat, human orders and active captures are retained without suicides");
    for(int team=1;team<=2;team++){
        reset("M1Abrams",3);g_enhanced.returnToBase=g_enhanced.armored=1;grid=&captureReady;
        for(int i=0;i<4;i++)*(int*)(players[i]+8)=team;
        *(int*)(players[3]+8)=3-team;soldiers[3][2]=180;soldiers[3][3]=100;
        vehicle.team=3-team;brains[0].task=TASK_FIGHT;g_fightTarget[0]=players[3];
        fleet_tick(0);assert(g_drive[0].phase==1 && g_drive[0].veh==(char*)&vehicle && brains[0].task==TASK_NONE);
        reset("M1Abrams",3);g_enhanced.returnToBase=g_enhanced.armored=1;grid=&captureReady;
        brains[0].task=TASK_FIGHT;brains[0].prevTask=1;fleet_tick(0);assert(!g_drive[0].phase);
        brains[0].prevTask=0;g_hitAt[0]=H->frame-1;fleet_tick(0);assert(!g_drive[0].phase);
        g_hitAt[0]=0;g_fightTarget[0]=players[3];soldiers[3][2]=20;fleet_tick(0);assert(!g_drive[0].phase);
    }
    puts("PASS: BOTH teams leave distant firefights for a nearby abandoned tank; human orders, recent hits and close enemies retain priority");
    reset("M1Abrams",3);g_enhanced.returnToBase=g_enhanced.armored=1;grid=&captureReady;
    g_drive[1].phase=1;g_drive[1].veh=(char*)&vehicle;g_drive[1].at=H->frame-200;soldiers[1][2]=-180;
    fleet_tick(0);assert(!g_drive[1].phase && g_drive[0].phase==1 && g_drive[0].veh==(char*)&vehicle && !exits);
    reset("M1Abrams",3);g_enhanced.returnToBase=g_enhanced.armored=1;grid=&captureReady;
    g_drive[1].phase=1;g_drive[1].veh=(char*)&vehicle;g_drive[1].at=H->frame-200;soldiers[1][2]=-180;*(int*)(players[1]+8)=2;
    fleet_tick(0);assert(g_drive[1].phase==1 && !g_drive[0].phase);
    puts("PASS: nearby idle infantry can take an unused ground vehicle from a distant teammate's walking claim; no seated crew or opposing-team claim is displaced");
    // A failed distant vehicle walk is shared only by teammates nearby.
    reset("M1Abrams",3);g_enhanced.returnToBase=g_enhanced.armored=1;grid=&captureReady;
    fleet_route_failed((char*)&vehicle,1,soldiers[0],90);
    assert(!fleet_reserve(1,soldiers[1]));*(int*)(players[1]+8)=2;
    assert(fleet_reserve(1,soldiers[1]));memset(&g_fleet[1],0,sizeof g_fleet[1]);
    soldiers[2][0]=100;assert(fleet_reserve(2,soldiers[2]));memset(&g_fleet[2],0,sizeof g_fleet[2]);
    H->frame+=90*30;assert(fleet_reserve(0,soldiers[0]));
    g_fleet[0].queuedAt=H->frame-901;g_fleet[0].until=H->frame+900;fleet_tick(0);
    assert(!g_fleet[0].vehicle && g_fleet[0].retryAt>H->frame && routeCancels && !exits);
    puts("PASS: shared local vehicle-route failures preserve distant/opposing-team use; thirty-second pending timeout releases infantry");
    reset("M1Abrams",3);g_enhanced.returnToBase=g_enhanced.armored=1;g_enhanced.groundDriversPerTeam=8;H->botCount=8;
    Vehicle queuePool[6];for(int k=0;k<6;k++){
        queuePool[k]=vehicle;queuePool[k].xf[12]=k*30;
        for(int seat=0;seat<5;seat++)queuePool[k].entries[seat]=queuePool[k].seatsMemory[seat]+4;
        vehicles[k]=(char*)&queuePool[k];
    }vehicleCount=6;
    for(int i=0;i<8;i++){H->bots[i]=players[i];H->botInput[i]=inputs[i];*(int*)(players[i]+8)=i%2+1;*(float**)(players[i]+16)=soldiers[i];brains[i].enabled=1;memset(soldiers[i],0,sizeof soldiers[i]);}
    assert(fleet_reserve(0,soldiers[0]) && fleet_reserve(2,soldiers[2]) && !fleet_reserve(4,soldiers[4]));
    assert(fleet_reserve(1,soldiers[1]) && fleet_reserve(3,soldiers[3]) && !fleet_reserve(5,soldiers[5]));
    soldiers[4][0]=120;soldiers[5][0]=150;
    assert(fleet_reserve(4,soldiers[4]) && fleet_reserve(5,soldiers[5]));
    memset(&g_fleet[4],0,sizeof g_fleet[4]);memset(&g_fleet[5],0,sizeof g_fleet[5]);
    g_drive[0].phase=1;g_drive[0].veh=g_fleet[0].vehicle;g_fleet[0].vehicle=NULL;
    assert(fleet_reserve(4,soldiers[4]));
    puts("PASS: BOTH teams can claim immediately nearby vehicles while two distant walks are queued; moving drivers release a queue slot");
    // Menu pending pilots also consume the hard six-pilot allowance.
    reset("F16",1);g_enhanced.air.pilotsPerTeam=1;
    g_deployActive[1].at=H->frame;g_deployActive[1].epoch=g_roundEpoch;g_deployActive[1].choice=0;
    g_deployChoice[0].profile=bm_air_profile("F16");assert(!fleet_pilot_allowed(0));
    g_deployActive[1].epoch=g_roundEpoch+1;assert(fleet_pilot_allowed(0));
    // Reject an enemy HQ child and refresh a base whose old entity vanished.
    reset("M1Abrams",3);H->teamSpawn[1].entity=players[3];fleet_remember_bases();assert(!g_fleetBase[1]);
    H->teamSpawn[1].entity=human;H->teamSpawn[2].entity=players[3];fleet_remember_bases();assert(g_fleetBase[1]==human && g_fleetBase[2]==players[3]);
    invalidSpawn=human;H->teamSpawn[1].entity=players[2];fleet_remember_bases();assert(g_fleetBase[1]==players[2] && g_fleetBase[2]==players[3]);
    puts("PASS: pending pilots counted; enemy HQ spawn refused; stale original base refreshed without swapping teams");
    reset("M1Abrams",3);H->teamSpawn[2].entity=human;validatedHq[2]=human;
    fleet_remember_bases();assert(g_fleetBase[2]==human);
    puts("PASS: fleet base selection accepts a discovery-validated RU HQ despite a generic child team field");
    // Landed transport delivery uses ordinary exit for bot cargo. Its pilot,
    // gunner and human cargo remain aboard, then the pilot takes off again.
    reset("Ka-60",5);g_enhanced.air.transportDrops=1;grid=&captureReady;flags[0][0]=0;flags[0][2]=5;flagOwners[0]=1;
    assert(enter_vehicle(players[0],&vehicle,0) && air_claim(0,(char*)&vehicle,1,0));
    assert(enter_vehicle(players[1],&vehicle,1) && enter_vehicle(players[2],&vehicle,3));
    *(char**)(vehicle.entries[4]+0x4c)=human;g_air[0].phase=AIR_FLY;g_air[0].control.phase=BM_FLIGHT_CRUISE;
    for(int tick=0;tick<130;tick++){H->frame++;air_landing_tick(0,vehicle.xf+12);}
    assert(!controlled[2] && controlled[0] && controlled[1] && *(char**)(vehicle.entries[4]+0x4c)==human && exits==1);
    assert(g_air[0].landing.phase==BM_LAND_ASCEND);vehicle.xf[13]=40;H->frame++;
    assert(!air_landing_tick(0,vehicle.xf+12) && g_air[0].landing.phase==BM_LAND_NONE && g_air[0].dropping);
    float pad[3]={0,0,5},outPad[3];padWater=1;assert(!air_landing_pad(0,pad,outPad));padWater=0;padObstructed=1;assert(!air_landing_pad(0,pad,outPad));
    puts("PASS: actual transport lands/unloads bot cargo/takes off; human and gunner seats preserved; water/obstruction reject landing");
    flags[0][0]=0;flags[0][2]=500;flagOwners[0]=2;
    reset("M1Abrams",3);g_enhanced.armored=g_enhanced.returnToBase=1;
    fleet_inventory_tick();assert(g_compRefresh==1);g_compRefresh=0;
    H->frame+=15;fleet_inventory_tick();assert(g_compRefresh==0);
    flagOwners[0]=1;H->frame+=15;fleet_inventory_tick();assert(g_compRefresh==1);
    g_fleet[0].retryAt=H->frame+300;g_scanPasses++;H->frame+=15;fleet_inventory_tick();assert(!g_fleet[0].retryAt);
    // A flag vehicle appears in the next published cache, for either team.
    for(int team=1;team<=2;team++){
        reset("M1Abrams",3);vehicleCount=0;g_enhanced.armored=g_enhanced.returnToBase=1;grid=&captureReady;
        for(int i=0;i<H->botCount;i++)*(int*)(players[i]+8)=team;
        fleet_tick(0);assert(!g_drive[0].phase);
        vehicle.team=team;vehicle.xf[12]=soldiers[0][0]=250;vehicle.xf[14]=soldiers[0][2]=250;
        flags[0][0]=flags[0][2]=250;flagOwners[0]=team;vehicleCount=1;
        H->frame+=15;g_scanPasses++;fleet_inventory_tick();fleet_tick(0);
        assert(g_drive[0].phase==1 && g_drive[0].veh==(char*)&vehicle);
    }
    flags[0][0]=0;flags[0][2]=500;flagOwners[0]=2;
    puts("PASS: captured-flag owner refresh, new-cache retry, and newly appeared vehicle dispatch on both teams");
    reset("F16",1); g_enhanced.air.jets=0; air_consider(0); assert(!g_air[0].phase && !claims);
    g_enhanced.air.jets=1; humanNearby=1; air_consider(0); assert(!g_air[0].phase);
    humanNearby=0; air_consider(0); assert(g_air[0].phase==AIR_WALK && g_drive[0].phase==9);
    allowEnter=0; air_tick(0); assert(!g_air[0].phase && !g_drive[0].phase && !controlled[0]);
    reset("F16",1); air_consider(0); air_tick(0);
    assert(g_air[0].phase==AIR_WAIT && g_driving[0] && controlled[0] && !g_air[1].phase);
    H->frame+=240; air_tick(0); assert(g_air[0].phase==AIR_FLY && g_air[0].control.phase==BM_FLIGHT_TAKEOFF);
    assert(g_air[0].output.throttle==1 && g_air[0].output.pitch==0 && g_air[0].output.brake==0);
    set_level(inputs[0],EIA_FIRE,1); set_level(inputs[0],EIA_PITCH,1); set_level(inputs[0],EIA_SEAT_YAW,1);
    air_apply(0,inputs[0]); float *levels=(float*)(inputs[0]+0x10);
    assert(levels[EIA_FIRE]==0 && levels[EIA_PITCH]==0 && levels[EIA_SEAT_YAW]==0 && levels[EIA_THROTTLE]==1);
    H->frame+=751; air_tick(0); assert(!g_air[0].phase && !g_drive[0].phase && !controlled[0]); // failed takeoff
    reset("F16",1); air_consider(0); air_tick(0); humanNearby=1; air_tick(0);
    assert(exits==1 && driver_seat_free((char*)&vehicle));
    reset("Ka-60",5); air_consider(0); air_tick(0);
    for(int i=1;i<4;i++) { assert(g_air[i].phase==AIR_WALK); air_tick(i); assert(g_air[i].phase==AIR_RIDE); }
    assert(free_seat_count((char*)&vehicle)==1); // fifth seat reserved for humans
    *(char**)(vehicle.entries[4]+0x4c)=human;
    g_enhanced.air.helicopters=0; air_tick(0);
    assert(exits==4 && *(char**)(vehicle.entries[4]+0x4c)==human);
    for(int i=0;i<4;i++) assert(!g_air[i].phase && !g_drive[i].phase && !g_driving[i]);
    reset("AH1Z",2); air_consider(0); air_tick(0); air_tick(1); assert(free_seat_count((char*)&vehicle)==0);
    g_air[0].output.fire=1; vehicle.xf[10]=NAN; air_tick(0); assert(g_air[0].output.fire==0);
    H->frame+=16; air_tick(0); assert(exits==2 && !g_air[0].phase && !g_air[1].phase);
    reset("Ka-60",5); air_consider(0); air_tick(0); for(int i=1;i<4;i++) air_tick(i);
    g_enhanced.air.transportDrops=1; H->frame+=240; air_tick(0);
    vehicle.xf[13]=110; vehicle.xf[14]=500; g_air[0].control.phase=BM_FLIGHT_CRUISE;
    memset(&g_air[0].motion,0,sizeof g_air[0].motion); H->frame+=30; air_tick(0);
    assert(!controlled[3] && controlled[1] && controlled[2]); // only passenger seat drops; gunners retained
    reset("AH1Z",2); air_consider(0); air_tick(0); air_tick(1);
    H->frame+=240; air_tick(0); g_enhanced.air.countermeasures=1; vehicle.health=900; air_tick(0);
    assert(g_air[0].counterUntil==H->frame+3); air_apply(0,inputs[0]); assert(((float*)(inputs[0]+0x10))[10]==1);
    H->frame+=4; air_tick(0); air_apply(0,inputs[0]); assert(((float*)(inputs[0]+0x10))[10]==0);
    vehicle.health=800; air_tick(0); assert(g_air[0].counterUntil<H->frame); // cooldown respected
    vehicle.health=50; air_tick(0); assert(exits==2 && driver_seat_free((char*)&vehicle));
    reset("F16",1); enter_vehicle(players[0],&vehicle,0); air_consider(0);
    assert(g_air[0].phase==AIR_WAIT && g_driving[0]); // parked hot-reload adoption
    reset("F16",1); vehicle.xf[13]=350; enter_vehicle(players[0],&vehicle,0); air_consider(0);
    assert(g_air[0].phase==AIR_FLY && g_air[0].control.phase==BM_FLIGHT_CRUISE);
    reset("Ka-60",5); air_consider(0); air_tick(0); memset(&g_air[1],0,sizeof g_air[1]);
    enter_vehicle(players[1],&vehicle,1); assert(air_ride(1,(char*)&vehicle));
    assert(g_air[1].phase==AIR_RIDE && g_drive[1].phase==9);
    // Simulate roster compaction: controller and reservation must move together.
    g_air[2].flag=123; bm_forget_slot(g_air,sizeof g_air[0],1,4); assert(g_air[1].flag==123 && !g_air[3].phase);
    air_status(); air_manual_guard((char*)&vehicle);
    // A natural respawn reserves a vehicle at the ORIGINAL deployment, even
    // after the infantry HQ has been learned nearer the front.
    reset("M1Abrams",3); g_enhanced.air.jets=g_enhanced.air.helicopters=0; g_enhanced.armored=1; g_enhanced.returnToBase=1;
    H->teamSpawn[1].entity=human; H->teamSpawn[1].a2=0xffffffff; H->teamSpawn[1].a3=1;
    g_hqPos[1][0]=g_hqPos[1][2]=-500; fleet_remember_bases();
    H->teamSpawn[1].entity=players[3]; g_hqPos[1][0]=800;
    *(char**)(players[0]+OFF_PLAYER_SOLDIER)=NULL;
    g_fleet[0].retryAt=H->frame+600;g_driveCooldown[0]=H->frame+600;g_airCooldown[0]=H->frame+900;
    assert(fleet_spawn(players[0])==human && g_fleet[0].vehicle==(char*)&vehicle && g_fleetBaseArgs[1][0]==0xffffffff);
    assert(!g_fleet[0].retryAt && !g_driveCooldown[0] && !g_airCooldown[0]);
    assert(fleet_claimed((char*)&vehicle,1) && !fleet_claimed((char*)&vehicle,0));
    *(char**)(players[0]+OFF_PLAYER_SOLDIER)=(char*)soldiers[0];
    NavGrid ready={300,300,2,10,-1500,-1500}; grid=&ready; soldiers[0][2]=800;
    fleet_tick(0); assert(g_drive[0].phase==1 && g_drive[0].veh==(char*)&vehicle && g_drive[0].walkUntil>H->frame+35*30);
    assert(!g_fleet[0].vehicle && navs[0].mode==3);
    reset("M1Abrams",3); g_enhanced.air.jets=g_enhanced.air.helicopters=0; g_enhanced.armored=1; g_enhanced.returnToBase=1;
    H->teamSpawn[1].entity=human; grid=&ready; vehicle.health=0;
    assert(!fleet_spawn(players[0])); // destroyed chassis is never requisitioned
    extraVehicle=vehicle; extraVehicle.health=1000; vehicles[0]=(char*)&extraVehicle;
    for(int k=0;k<5;k++) extraVehicle.entries[k]=extraVehicle.seatsMemory[k]+4;
    assert(fleet_spawn(players[0])==human && g_fleet[0].vehicle==(char*)&extraVehicle);
    humanNearby=1; assert(!strcmp(fleet_reason((char*)&extraVehicle,1),"reserved-for-human"));
    // One helicopter cannot reserve the final slot while a usable jet waits.
    reset("AH1Z",2); extraVehicle=vehicle; strcpy(extraVehicle.name,"F16"); extraVehicle.seats=1;
    for(int k=0;k<5;k++) extraVehicle.entries[k]=extraVehicle.seatsMemory[k]+4;
    vehicles[1]=(char*)&extraVehicle; vehicleCount=2; extraVehicle.xf[14]=600;
    g_air[1].phase=AIR_FLY; g_air[1].pilot=1; g_air[1].profile=bm_air_profile("AH1Z");
    assert(!fleet_air_slot(1,bm_air_profile("AH1Z")) && fleet_air_slot(1,bm_air_profile("F16")));
    // With four bots and an existing aircraft, give the remaining base
    // assignment to enabled armor rather than filling both slots with jets.
    reset("M1Abrams",3); g_enhanced.armored=1; g_enhanced.returnToBase=1; extraVehicle=vehicle; strcpy(extraVehicle.name,"F16");
    for(int k=0;k<5;k++) extraVehicle.entries[k]=extraVehicle.seatsMemory[k]+4;
    vehicles[1]=(char*)&extraVehicle; vehicleCount=2;
    g_air[1].phase=AIR_FLY; g_air[1].pilot=1; g_air[1].profile=bm_air_profile("F16");
    assert(fleet_reserve(0,soldiers[0]) && g_fleet[0].role==FLEET_GROUND);
    // Human departure: a passenger takes the pilot seat even while that human
    // is nearby; other passengers are kept. Failed airborne transfer stays seated.
    reset("Ka-60",5); assert(enter_vehicle(players[1],&vehicle,1)); humanNearby=1;
    *(char**)(vehicle.entries[4]+0x4c)=human;
    fleet_takeover(1); H->frame+=6; fleet_takeover(1);
    assert(playerSeats[1]==0 && g_air[1].pilot && g_air[1].takeover && g_driving[1]);
    air_tick(1); assert(controlled[1] && *(char**)(vehicle.entries[4]+0x4c)==human);
    reset("Ka-60",5); assert(enter_vehicle(players[1],&vehicle,1)); allowSwitch=0;
    fleet_takeover(1); H->frame+=6; fleet_takeover(1);
    assert(playerSeats[1]==1 && controlled[1] && !exits && !g_air[1].pilot);
    reset("M1Abrams",3); g_enhanced.armored=1; g_enhanced.returnToBase=1; assert(enter_vehicle(players[1],&vehicle,1)); allowSwitch=0;
    fleet_takeover(1); H->frame+=6; fleet_takeover(1);
    assert(controlled[1] && playerSeats[1]==0 && g_drive[1].phase==2 && g_driving[1]);
    // A human in the pilot seat is never displaced.
    reset("Ka-60",5); assert(enter_vehicle(players[1],&vehicle,1)); *(char**)(vehicle.entries[0]+0x4c)=human;
    fleet_takeover(1); H->frame+=60; fleet_takeover(1); assert(playerSeats[1]==1 && !g_air[1].pilot);
    // No path: release the reservation and cool down instead of crowding a pad.
    reset("M1Abrams",3); g_enhanced.armored=1; g_enhanced.returnToBase=1; grid=&ready; allowPlan=0;
    vehicle.xf[14]=50;soldiers[0][2]=800;g_lastSpawnedAt[0]=H->frame-10;   // (V34: just spawned - a 750 m walk only then)
    fleet_tick(0); assert(!g_fleet[0].vehicle && !g_drive[0].phase && !vehicle_bad((char*)&vehicle) && g_fleet[0].failedVehicle==(char*)&vehicle);
    assert(fleet_reserve(1,soldiers[1])); // a route failure elsewhere never globally bans the vehicle
    memset(&g_fleet[1],0,sizeof g_fleet[1]);H->teamSpawn[1].entity=human;*(char**)(players[0]+OFF_PLAYER_SOLDIER)=NULL;
    assert(fleet_spawn(players[0])==human && !g_fleet[0].failedVehicle && !g_fleet[0].failedUntil);
    g_fleet[2].vehicle=(char*)&vehicle; g_fleet[2].role=FLEET_GROUND;
    bm_forget_slot(g_fleet,sizeof g_fleet[0],1,4); assert(g_fleet[1].vehicle==(char*)&vehicle && !g_fleet[3].vehicle);
    fleet_status();
    // A budget pause retains the reservation and does not blacklist the tank.
    reset("M1Abrams",3);g_enhanced.armored=g_enhanced.returnToBase=1;grid=&ready;
    vehicle.xf[14]=50;allowPlan=0;planDeferred=1;
    fleet_tick(0);assert(g_fleet[0].vehicle==(char*)&vehicle&&!badVehicle&&!g_fleet[0].retryAt);
    H->frame++;allowPlan=1;planDeferred=0;fleet_tick(0);
    assert(g_drive[0].phase==1&&!g_fleet[0].vehicle&&!badVehicle);
    // A nearby aircraft can be assigned after other planners spend the budget.
    reset("AH1Z",2);g_enhanced.returnToBase=1;grid=&ready;budgetAvailable=0;
    fleet_tick(0);assert(g_air[0].phase==AIR_WALK);air_tick(0);assert(controlled[0]==(char*)&vehicle);
    // Eight bots and four classes: proximity cannot give all slots to helicopters.
    reset("M1Abrams",3);g_enhanced.returnToBase=g_enhanced.armored=1;host.botCount=8;grid=&ready;
    for(int i=4;i<8;i++){H->bots[i]=players[i];H->botInput[i]=inputs[i];*(int*)(players[i]+8)=1;*(float**)(players[i]+OFF_PLAYER_SOLDIER)=soldiers[i];brains[i].enabled=1;}
    extraVehicle=thirdVehicle=fourthVehicle=vehicle;
    strcpy(extraVehicle.name,"F16");strcpy(thirdVehicle.name,"Growler");strcpy(fourthVehicle.name,"AH1Z");
    Vehicle *fleet[4]={&vehicle,&extraVehicle,&thirdVehicle,&fourthVehicle};vehicleCount=4;
    for(int k=0;k<4;k++){vehicles[k]=(char*)fleet[k];fleet[k]->xf[14]=1000-k*250;for(int seat=0;seat<5;seat++)fleet[k]->entries[seat]=fleet[k]->seatsMemory[seat]+4;}
    for(int i=0;i<4;i++){assert(fleet_reserve(i,soldiers[i]));assert(g_fleet[i].vehicle==vehicles[i]);fleet_tick(i);assert(g_drive[i].phase || g_air[i].phase);}
    assert(!fleet_reserve(4,soldiers[4])); // half the team stays on foot
    // Opening priority can use all four classes even with four bots per team.
    memset(g_fleet,0,sizeof g_fleet);memset(g_drive,0,sizeof g_drive);memset(g_air,0,sizeof g_air);host.botCount=4;g_fleetPriorityUntil=H->frame+60*30;
    for(int i=0;i<4;i++){assert(fleet_reserve(i,soldiers[i]));assert(g_fleet[i].vehicle==vehicles[i]);fleet_tick(i);assert(g_drive[i].phase || g_air[i].phase);}
    // Both teams dispatch a tank and a chopper from their own deployment.
    reset("M1Abrams",3);g_enhanced.armored=g_enhanced.returnToBase=1;g_enhanced.air.jets=0;
    host.botCount=8;grid=&ready;extraVehicle=thirdVehicle=fourthVehicle=vehicle;
    strcpy(thirdVehicle.name,"AH1Z");strcpy(fourthVehicle.name,"AH1Z");
    vehicle.team=thirdVehicle.team=1;extraVehicle.team=fourthVehicle.team=2;
    Vehicle *both[4]={&vehicle,&extraVehicle,&thirdVehicle,&fourthVehicle};vehicleCount=4;
    for(int k=0;k<4;k++){vehicles[k]=(char*)both[k];both[k]->xf[14]=100+k*20;for(int seat=0;seat<5;seat++)both[k]->entries[seat]=both[k]->seatsMemory[seat]+4;}
    for(int i=0;i<8;i++){H->bots[i]=players[i];H->botInput[i]=inputs[i];*(int*)(players[i]+8)=i%2+1;*(float**)(players[i]+OFF_PLAYER_SOLDIER)=soldiers[i];brains[i].enabled=1;g_lastSpawnedAt[i]=H->frame-6;}
    for(int i=0;i<8;i++){H->frame=990+(15-i)%15;fleet_tick(i);}
    assert(g_drive[0].veh==(char*)&vehicle&&g_drive[1].veh==(char*)&extraVehicle);
    assert(g_air[2].veh==(char*)&thirdVehicle&&g_air[3].veh==(char*)&fourthVehicle);
    assert(g_drive[0].phase==1&&g_drive[1].phase==1&&g_air[2].pilot&&g_air[3].pilot);
    // Aircraft acquisition survives temporary occlusion, while fire visibility remains false.
    reset("F16",1);g_enhanced.air.weapons=1;g_air[0].profile=bm_air_profile("F16");g_air[0].control.phase=BM_FLIGHT_CRUISE;g_air[0].veh=(char*)&vehicle;
    extraVehicle=vehicle;strcpy(extraVehicle.name,"AH1Z");extraVehicle.team=2;extraVehicle.xf[14]=1500;extraVehicle.xf[13]=250;
    for(int k=0;k<5;k++)extraVehicle.entries[k]=extraVehicle.seatsMemory[k]+4;
    *(char**)(extraVehicle.entries[0]+0x4c)=human;vehicles[1]=(char*)&extraVehicle;vehicleCount=2;
    BMAirObservation observation={0};observation.position[1]=350;observation.right[0]=observation.up[1]=observation.forward[2]=1;targetVisible=0;
    air_target(0,&observation);assert(g_air[0].targetVeh==(char*)&extraVehicle&&observation.targetValid&&!observation.targetVisible);targetVisible=1;
    // A pickup passenger boards the cargo cabin rather than permanently occupying a gunner seat.
    reset("Ka-60",5);g_enhanced.air.transportDrops=1;g_enhanced.air.landings=0;enter_vehicle(players[0],&vehicle,0);air_claim(0,(char*)&vehicle,1,0);
    g_air[0].phase=AIR_FLY;g_air[0].control.phase=BM_FLIGHT_CRUISE;g_air[0].pickup=1;g_air[0].pickupPlayer=players[1];g_air[0].pickupAt=H->frame;
    vehicle.xf[13]=5;brains[2].task=brains[3].task=1;
    air_transport_job(0,vehicle.xf+12);assert(g_air[1].phase==AIR_WALK&&g_air[1].pickup);
    air_tick(1);assert(playerSeats[1]>=g_air[0].profile->firstPassenger&&g_air[1].phase==AIR_RIDE&&free_seat_count((char*)&vehicle)>=1);
    assert(air_cargo(&g_air[0])==1);
    reset("AH1Z",2);H->botCount=8;g_enhanced.returnToBase=1;g_enhanced.groundDriversPerTeam=8;g_enhanced.air.pilotsPerTeam=6;g_fleetPriorityUntil=H->frame+1800;
    Vehicle pool[8];vehicleCount=8;
    for(int i=0;i<8;i++){
        H->bots[i]=players[i];H->botInput[i]=inputs[i];*(int*)(players[i]+8)=1;*(float**)(players[i]+OFF_PLAYER_SOLDIER)=soldiers[i];memset(soldiers[i],0,12);soldiers[i][3]=100;brains[i].enabled=1;
        pool[i]=vehicle;strcpy(pool[i].name,i?"AH1Z":"F16");pool[i].seats=i?2:1;pool[i].xf[12]=10+i*5;
        for(int k=0;k<5;k++)pool[i].entries[k]=pool[i].seatsMemory[k]+4;
        vehicles[i]=(char*)&pool[i];
    }
    for(int i=0;i<6;i++){assert(fleet_reserve(i,soldiers[i]));assert(g_fleet[i].role==(i?FLEET_HELI:FLEET_JET));assert(air_claim(i,g_fleet[i].vehicle,1,0));}
    assert(!fleet_reserve(6,soldiers[6]));
    memset(g_fleet,0,sizeof g_fleet);memset(g_air,0,sizeof g_air);memset(g_drive,0,sizeof g_drive);for(int i=0;i<8;i++){*(int*)(players[i]+8)=2;pool[i].team=2;}
    for(int i=0;i<6;i++){assert(fleet_reserve(i,soldiers[i]));assert(g_fleet[i].role==(i?FLEET_HELI:FLEET_JET));assert(air_claim(i,g_fleet[i].vehicle,1,0));}
    // Six is a hard per-team admission cap, including reservations, takeover
    // and even a malformed in-memory configuration above the parser maximum.
    assert(!fleet_pilot_allowed(6));g_enhanced.air.pilotsPerTeam=8;
    assert(!air_claim(6,(char*)&pool[6],1,0));
    assert(enter_vehicle(players[6],&pool[6],1));fleet_takeover(6);H->frame+=30;fleet_takeover(6);
    assert(playerSeats[6]==1 && !g_air[6].pilot && controlled[6]==(char*)&pool[6]);
    *(int*)(players[6]+8)=1;pool[6].team=1;assert(fleet_pilot_allowed(6));
    assert(air_claim(6,(char*)&pool[6],1,0)); // other team's six reservations do not consume this team
    // Mid-map abandoned vehicles do not inherit an unoccupied last owner's
    // team restriction. The actual opposing deployment still stays protected.
    reset("M1Abrams",3);g_enhanced.armored=1;vehicle.team=2;
    g_hqPos[1][0]=g_hqPos[1][2]=-1000;g_hqPos[2][0]=g_hqPos[2][2]=1000;
    vehicle.xf[12]=vehicle.xf[14]=300;assert(!strcmp(fleet_reason((char*)&vehicle,1),"available"));
    assert(!strcmp(fleet_reason((char*)&vehicle,2),"available"));
    vehicle.xf[12]=vehicle.xf[14]=1000;assert(!strcmp(fleet_reason((char*)&vehicle,1),"enemy-deployment"));
    assert(!strcmp(fleet_reason((char*)&vehicle,2),"available"));
    // An armed chopper has a real infantry fallback when no enemy drives a
    // vehicle. Invalid/dead/friendly targets clear immediately between scans.
    reset("AH1Z",2);g_enhanced.air.weapons=1;
    managerEnabled=1;unsigned count=4;void **rows=playerRows;memcpy(manager+8,&count,sizeof count);memcpy(manager+0xac,&rows,sizeof rows);
    for(int k=0;k<4;k++){playerRows[k]=players[k];soldiers[k][3]=100;}
    *(int*)(players[1]+8)=2;soldiers[1][2]=500;
    g_air[0].profile=bm_air_profile("AH1Z");g_air[0].veh=(char*)&vehicle;g_air[0].control.phase=BM_FLIGHT_CRUISE;
    BMAirObservation infantry={0};infantry.position[1]=100;infantry.right[0]=infantry.up[1]=infantry.forward[2]=1;
    air_target(0,&infantry);assert(infantry.targetValid && g_air[0].targetPl==players[1] && !g_air[0].targetVeh && !infantry.targetAir);
    unsigned scanned=g_air[0].scanAt;H->frame++;air_target(0,&infantry);assert(g_air[0].scanAt==scanned && infantry.targetValid);
    *(int*)(players[1]+8)=1;H->frame++;air_target(0,&infantry);assert(!infantry.targetValid && !g_air[0].targetPl && g_air[0].scanAt==scanned);
    H->frame+=15;*(int*)(players[1]+8)=2;soldiers[1][3]=0;air_target(0,&infantry);assert(!infantry.targetValid);
    // Crew team wins over stale vehicle ownership for aircraft pursuit.
    reset("F16",1);g_enhanced.air.weapons=1;
    extraVehicle=vehicle;strcpy(extraVehicle.name,"AH1Z");extraVehicle.seats=2;extraVehicle.xf[14]=1000;
    for(int k=0;k<5;k++)extraVehicle.entries[k]=extraVehicle.seatsMemory[k]+4;
    vehicles[1]=(char*)&extraVehicle;vehicleCount=2;*(int*)(players[1]+8)=2;assert(enter_vehicle(players[1],&extraVehicle,0));
    g_air[0].profile=bm_air_profile("F16");g_air[0].veh=(char*)&vehicle;g_air[0].control.phase=BM_FLIGHT_CRUISE;
    targetVisible=0;BMAirObservation pursuit={0};pursuit.position[1]=350;pursuit.right[0]=pursuit.up[1]=pursuit.forward[2]=1;
    air_target(0,&pursuit);assert(pursuit.targetValid && pursuit.targetAir && !pursuit.targetVisible && g_air[0].targetVeh==(char*)&extraVehicle);
    *(int*)(players[1]+8)=1;air_target(0,&pursuit);assert(!pursuit.targetValid && !g_air[0].targetVeh);
    // Helicopters lift vertically beside parked vehicles; only a jet waits
    // for the runway ahead. Both cases exercise the actual AIR_WAIT branch.
    reset("AH1Z",2);air_consider(0);air_tick(0);extraVehicle=vehicle;strcpy(extraVehicle.name,"M1Abrams");
    for(int k=0;k<5;k++)extraVehicle.entries[k]=extraVehicle.seatsMemory[k]+4;
    extraVehicle.xf[14]=20;vehicles[1]=(char*)&extraVehicle;vehicleCount=2;H->frame+=240;
    air_tick(0);assert(g_air[0].phase==AIR_FLY && controlled[0] && !exits);
    reset("F16",1);air_consider(0);air_tick(0);extraVehicle=vehicle;strcpy(extraVehicle.name,"M1Abrams");
    for(int k=0;k<5;k++)extraVehicle.entries[k]=extraVehicle.seatsMemory[k]+4;
    extraVehicle.xf[14]=20;vehicles[1]=(char*)&extraVehicle;vehicleCount=2;H->frame+=240;
    air_tick(0);assert(g_air[0].phase==AIR_WAIT && controlled[0] && !exits);
    // Armed patrols spread across the map, dwell after arrival, then visit a
    // different objective. A lengthy inbound journey does not trigger a turn-back.
    reset("AH1Z",2);g_flagCount=3;
    for(int f=0;f<3;f++){flags[f][0]=f*1000;flags[f][1]=flags[f][2]=0;flagOwners[f]=2;}
    for(int i=0;i<3;i++){g_air[i].profile=bm_air_profile("AH1Z");g_air[i].phase=AIR_FLY;g_air[i].pilot=1;}
    float patrol[3]={0,100,0};for(int i=0;i<3;i++)air_goal(i,patrol);
    assert(g_air[0].flag!=g_air[1].flag && g_air[0].flag!=g_air[2].flag && g_air[1].flag!=g_air[2].flag);
    int remote=g_air[2].flag;H->frame+=120*30;air_goal(2,patrol);assert(g_air[2].flag==remote && !g_air[2].patrolAt);
    float arrived[3]={flags[remote][0],100,0};air_goal(2,arrived);unsigned arrival=g_air[2].patrolAt;assert(arrival==H->frame);
    H->frame+=24*30;air_goal(2,arrived);assert(g_air[2].flag==remote && g_air[2].patrolAt==arrival);
    H->frame+=31;air_goal(2,arrived);assert(g_air[2].flag!=remote);
    // A commander/combat task releases an old pending walking claim so a
    // different bot can use the abandoned vehicle immediately.
    reset("M1Abrams",3);g_enhanced.armored=g_enhanced.returnToBase=1;
    assert(fleet_reserve(0,soldiers[0]));brains[0].task=1;fleet_tick(0);
    assert(!g_fleet[0].vehicle && !g_fleet[0].role && fleet_reserve(1,soldiers[1]));
    // Already-seated ground drivers are adopted before idle infantry logic.
    reset("M1Abrams",3);g_enhanced.armored=1;assert(enter_vehicle(players[0],&vehicle,0));fleet_takeover(0);
    assert(g_drive[0].phase==2 && g_drive[0].veh==(char*)&vehicle && g_driving[0] && !exits);
    puts("PASS: actual vertical helicopter takeoff/jet runway guard, armed patrol coverage/dwell and occupied ground-driver adoption");
    puts("PASS: Test7 abandoned map vehicles, six-pilot claim/takeover cap, independent team quotas, crew-team targeting, infantry fallback and bounded empty scans");
    puts("PASS: actual native aircraft/fleet HQ-respawn/replacement reservation/return routing/jet capacity/passenger takeover/crew protection/compaction; six pilots reserve one jet and five helicopters on BOTH teams");
    return 0;
}
