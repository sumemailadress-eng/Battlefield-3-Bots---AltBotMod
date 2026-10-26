// AltBotMod V31.1: the gunner's target pick traces at most GUN_PICK_RAYS candidates, best first, and picks the same target as
// tracing every candidate whenever that target is among the best GUN_PICK_RAYS.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../files/BotMod/src/enhanced.h"
#define MAX_BOTS 1
#define OFF_PLAYER_SOLDIER 16
#define OFF_PLAYER_ONLINE_NAME 32
#define OFF_SOLDIER_SPAWN_POSITION 0
#define OFF_SOLDIER_HEALTH 12
#define GUN_VEHICLE_RANGE 650
#define GUN_INFANTRY_RANGE 600
#define NP 48
typedef struct { char bytes[128] __attribute__((aligned(8))); int team,engineer,seat,vis; void *ride; float soldier[4]; } Player;
typedef struct { char name[32]; float health,position[3]; int team; char *occupant; } Vehicle;
typedef struct { unsigned frame; char *bots[1]; } Host;
static Host host,*H=&host;
static Player own,pl[NP];
static Vehicle tank;
static char *g_gunVeh[1],*g_gunTarget[1],*g_gunTargetVeh[1],*vehicles[1];
static unsigned g_gunBurstAt[1],g_gunLostAt[1]; static float g_gunAimH[1];
static void *players[NP+1]; static char manager[256] __attribute__((aligned(8)));
static int rays;
static char *g_attacker[MAX_BOTS];static unsigned g_attackedAt[MAX_BOTS];
static void bot_log(const char *format,...) { (void)format; }
static int player_team(char *p) { return ((Player*)p)->team; }
static int player_is_engineer(char *p) { return ((Player*)p)->engineer; }
static char *player_vehicle(char *p) { return ((Player*)p)->ride; }
static char *player_manager(void) { return manager; }
static int vehicle_list(char ***out) { *out=vehicles; return 1; }
static int vehicle_ok(char *v) { return v && ((Vehicle*)v)->health>0; }
static const char *vehicle_name(char *v) { return v ? ((Vehicle*)v)->name : ""; }
static int vehicle_occupied(char *v) { return ((Vehicle*)v)->occupant!=NULL; }
static char *vehicle_occupant(char *v) { return ((Vehicle*)v)->occupant; }
static float *vehicle_pos(char *v,int *team) { if(team) *team=((Vehicle*)v)->team; return ((Vehicle*)v)->position; }
static float dist2(const float *a,const float *b) { float x=a[0]-b[0],z=a[2]-b[2]; return x*x+z*z; }
static int gun_can_see(const float *a,const float *b,int v) { (void)a; (void)b; (void)v; rays++; return 0; }
static float gun_clear_height(const float *a,const float *b) { (void)a; rays++; Player *p=(Player*)((char*)b-offsetof(Player,soldier)); return p->vis ? 1.1f : -1; }
static int gunner_target_ok(int i,const float *p) { (void)i; (void)p; return 1; }
static void bot_spot(char *a,char *b) { (void)a; (void)b; }
static int stationary_weapon(char *v){(void)v;return 0;}
#include "../files/BotMod/src/gunner_targets.inc"
int main(void)
{
    srand(7);
    strcpy(tank.name,"M1Abrams"); tank.health=1000; tank.team=1; tank.occupant=(char*)&own;
    own.team=1; own.ride=&tank; H->bots[0]=(char*)&own; H->frame=100; g_gunVeh[0]=(char*)&tank; vehicles[0]=(char*)&tank;
    players[0]=&own;
    for(int k=0;k<NP;k++){ Player *p=&pl[k]; memset(p,0,sizeof *p); p->team=2; p->soldier[2]=20+rand()%570; p->soldier[3]=100; char *s=(char*)p->soldier; memcpy(p->bytes+OFF_PLAYER_SOLDIER,&s,sizeof s); players[k+1]=p; }
    void **array=players; unsigned count=NP+1; memcpy(manager+0xac,&array,sizeof array); memcpy(manager+8,&count,sizeof count);
    float origin[3]={0}; int agree=0, cases=0, maxRays=0;
    for(int round=0;round<400;round++){
        for(int k=0;k<NP;k++){ pl[k].vis=rand()%3==0; pl[k].soldier[2]=20+rand()%570; pl[k].engineer=rand()%5==0; }
        /* brute force: the best-scoring visible soldier */
        const BMVehicleProfile *prof=bm_vehicle_profile("M1Abrams"); float best=INFINITY; Player *want=NULL; int rank=0;
        float sc[NP]; for(int k=0;k<NP;k++){ float d=pl[k].soldier[2]; float s=bm_vehicle_target_score(prof,pl[k].engineer?BM_TARGET_ENGINEER:BM_TARGET_INFANTRY,d); if(d<25)s=fminf(s,40+d); sc[k]=s; if(pl[k].vis && s<best){best=s;want=&pl[k];} }
        if(want){ for(int k=0;k<NP;k++) if(sc[k]<best) rank++; }
        g_gunTarget[0]=g_gunTargetVeh[0]=NULL; rays=0; H->frame+=20;
        gunner_pick_target(0,origin);
        if(rays>maxRays)maxRays=rays;
        assert(rays<=GUN_PICK_RAYS);
        if(want && rank<GUN_PICK_RAYS){ cases++; agree+=g_gunTarget[0]==(char*)want; }
        if(!want) assert(!g_gunTarget[0]);
    }
    assert(cases>100 && agree==cases);
    printf("PASS: gunner pick - %d soldiers, 400 random rounds: at most %d rays a pick (V5-V31: up to %d), the same target as tracing all of them in every one of the %d rounds where it ranks in the best %d\n", NP, maxRays, NP, cases, GUN_PICK_RAYS);
    return 0;
}
