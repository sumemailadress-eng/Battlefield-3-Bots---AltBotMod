// Exercise the actual native target selector with mocked soldiers/vehicles.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../files/BotMod/src/enhanced.h"
#define MAX_BOTS 1
#define OFF_PLAYER_SOLDIER 16
#define OFF_PLAYER_ONLINE_NAME 32
#define OFF_SOLDIER_SPAWN_POSITION 0
#define OFF_SOLDIER_HEALTH 12
#define GUN_VEHICLE_RANGE 650
#define GUN_INFANTRY_RANGE 300
typedef struct { char bytes[128] __attribute__((aligned(8))); int team,engineer,seat; void *ride; float soldier[4]; } Player;
typedef struct { char name[32]; float health,position[3]; int team; char *occupant; } Vehicle;
typedef struct { unsigned frame; char *bots[1]; } Host;
static Host host,*H=&host;
static Player own,infantry,engineer,heliPilot;
static Vehicle tank,helicopter,enemyTank;
static char *g_gunVeh[1],*g_gunTarget[1],*g_gunTargetVeh[1],*vehicles[3];
static unsigned g_gunBurstAt[1],g_gunLostAt[1]; static float g_gunAimH[1];
static void *players[4]; static char manager[256] __attribute__((aligned(8)));
static int visible=1;
static void bot_log(const char *format,...) { (void)format; }
static int player_team(char *p) { return ((Player*)p)->team; }
static int player_is_engineer(char *p) { return ((Player*)p)->engineer; }
static char *player_vehicle(char *p) { return ((Player*)p)->ride; }
static char *player_manager(void) { return manager; }
static int vehicle_list(char ***out) { *out=vehicles; return 3; }
static int vehicle_ok(char *v) { return v && ((Vehicle*)v)->health>0; }
static const char *vehicle_name(char *v) { return v ? ((Vehicle*)v)->name : ""; }
static int vehicle_occupied(char *v) { return ((Vehicle*)v)->occupant!=NULL; }
static char *vehicle_occupant(char *v) { return ((Vehicle*)v)->occupant; }
static float *vehicle_pos(char *v,int *team) { if(team) *team=((Vehicle*)v)->team; return ((Vehicle*)v)->position; }
static int vehicle_seat_of(char *v,char *p) { return player_vehicle(p)==v ? ((Player*)p)->seat : -1; }
static float dist2(const float *a,const float *b) { float x=a[0]-b[0],z=a[2]-b[2]; return x*x+z*z; }
static int gun_can_see(const float *a,const float *b,int v) { (void)a; (void)b; (void)v; return visible; }
static float gun_clear_height(const float *a,const float *b) { (void)a; (void)b; return visible ? 1.1f : -1; }
static int gunner_target_ok(int i,const float *p) { (void)i; (void)p; return visible; }
static void bot_spot(char *a,char *b) { (void)a; (void)b; }
typedef struct {int manual;char*veh;unsigned reverseUntil,backOutUntil,combatHoldAt,combatAdvanceUntil;}DriveState;
static BMConfig g_enhanced;static void*g_gunWeapon[1];
static int __attribute__((unused)) player_exists(char*p){return p==(char*)&infantry || p==(char*)&engineer;}
enum {ARMOR_NORMAL};static struct {int job;}g_armor[MAX_BOTS];
static int stationary_weapon(char *v){(void)v;return 0;}
static char *g_attacker[MAX_BOTS];static unsigned g_attackedAt[MAX_BOTS];   /* AltBotMod V16: who last hit each bot */
#include "../files/BotMod/src/gunner_targets.inc"
#include "../files/BotMod/src/ground_combat.inc"
static void setup_player(Player *p,int team,float distance)
{
    memset(p,0,sizeof *p); p->team=team; p->soldier[2]=distance; p->soldier[3]=100;
    char *s=(char*)p->soldier; memcpy(p->bytes+OFF_PLAYER_SOLDIER,&s,sizeof s);
    strcpy(p->bytes+OFF_PLAYER_ONLINE_NAME,"fixture");
}
int main(void)
{
    g_armor[0].job=ARMOR_NORMAL;
    setup_player(&own,1,0); setup_player(&infantry,2,35); setup_player(&engineer,2,500); setup_player(&heliPilot,2,650);
    strcpy(tank.name,"M1Abrams"); tank.health=1000; tank.team=1; tank.occupant=(char*)&own;
    strcpy(helicopter.name,"Mi28"); helicopter.health=1000; helicopter.team=2; helicopter.position[2]=600; helicopter.occupant=(char*)&heliPilot;
    strcpy(enemyTank.name,"T90"); enemyTank.health=0; enemyTank.team=2; enemyTank.position[2]=80; enemyTank.occupant=(char*)&heliPilot;
    own.ride=&tank; own.seat=0; heliPilot.ride=&helicopter;
    H->bots[0]=(char*)&own; H->frame=100;
    vehicles[0]=(char*)&tank; vehicles[1]=(char*)&helicopter; vehicles[2]=(char*)&enemyTank;
    players[0]=&own; players[1]=&infantry; players[2]=&engineer; players[3]=&heliPilot;
    void **array=players; unsigned count=4; memcpy(manager+0xac,&array,sizeof array); memcpy(manager+8,&count,sizeof count);
    g_gunVeh[0]=(char*)&tank; g_gunTarget[0]=(char*)&heliPilot; g_gunTargetVeh[0]=(char*)&helicopter;
    float origin[3]={0}; gunner_pick_target(0,origin);
    assert(g_gunTarget[0]==(char*)&infantry && !g_gunTargetVeh[0] && g_gunAimH[0]>0);
    unsigned burst=g_gunBurstAt[0]; H->frame++; gunner_pick_target(0,origin); assert(g_gunBurstAt[0]==burst);
    enemyTank.health=1000; H->frame++; gunner_pick_target(0,origin); assert(g_gunTargetVeh[0]==(char*)&enemyTank);
    engineer.engineer=1; engineer.soldier[2]=40; H->frame++; gunner_pick_target(0,origin);
    assert(g_gunTarget[0]==(char*)&engineer && !g_gunTargetVeh[0]);
    enemyTank.health=0; engineer.soldier[3]=0; infantry.soldier[2]=8;
    assert(vehicle_seat_of((char*)&tank,(char*)&own)==0);
    H->frame++; gunner_pick_target(0,origin); assert(g_gunTarget[0]==(char*)&infantry && !g_gunTargetVeh[0]); // cannon's existing <15 m fire guard remains in gunner_tick
    own.seat=1; H->frame++; gunner_pick_target(0,origin);
    assert(g_gunTarget[0]==(char*)&infantry && !g_gunTargetVeh[0]); // crew gun can deal with the close infantry
    visible=0; H->frame++; gunner_pick_target(0,origin); assert(!g_gunTarget[0] && !g_gunTargetVeh[0]);
    visible=1; infantry.team=1; H->frame++; gunner_pick_target(0,origin); assert(!g_gunTargetVeh[0] && !g_gunTarget[0]);   /* V17: a helicopter 600 m off is the AA launchers' job */
    helicopter.position[2]=250; H->frame++; gunner_pick_target(0,origin); assert(g_gunTargetVeh[0]==(char*)&helicopter);   /* 250 m with nothing else: last resort */
    helicopter.position[2]=600;
    puts("PASS: actual tank selector close-infantry over distant helicopter/near armor/engineer/aim hysteresis/visibility/friendly/point-blank checks");
    /* AltBotMod V16: whoever just shot the bot comes first (tanks "ignored" their attackers); a soldier at the hull beats a far one */
    infantry.team=2; infantry.soldier[2]=200; engineer.soldier[3]=100; engineer.engineer=1; engineer.soldier[2]=60; enemyTank.health=0; own.seat=0;
    g_gunTarget[0]=NULL; g_gunTargetVeh[0]=NULL; H->frame+=10; gunner_pick_target(0,origin); assert(g_gunTarget[0]==(char*)&engineer);
    g_attacker[0]=(char*)&infantry; g_attackedAt[0]=H->frame; H->frame++; gunner_pick_target(0,origin); assert(g_gunTarget[0]==(char*)&infantry);
    H->frame+=6*30; gunner_pick_target(0,origin); assert(g_gunTarget[0]==(char*)&engineer);   /* 5 s later: the normal order again */
    g_attacker[0]=NULL; infantry.soldier[2]=12; H->frame++; gunner_pick_target(0,origin); assert(g_gunTarget[0]==(char*)&infantry);   /* at the hull */
    puts("PASS: V16 - the soldier who just hit the tank is engaged first for 5 s; a soldier within 25 m of the hull beats a far engineer");
    bm_defaults(&g_enhanced);g_enhanced.driverWeapons=1;g_gunWeapon[0]=&tank;
    DriveState d={.veh=(char*)&tank};enemyTank.health=1000;enemyTank.position[2]=450;
    g_gunTargetVeh[0]=(char*)&enemyTank;assert(!drive_combat_hold(0,&d,origin));   /* AltBotMod V23: no slow-down for a target - fire on the move */
    g_gunTargetVeh[0]=(char*)&helicopter;assert(!drive_combat_hold(0,&d,origin));
    // AltBotMod V8: soldiers are shot on the move - no hold for infantry at all
    g_gunTargetVeh[0]=NULL;g_gunTarget[0]=(char*)&infantry;infantry.team=2;infantry.soldier[2]=140;assert(!drive_combat_hold(0,&d,origin));
    g_gunTargetVeh[0]=(char*)&enemyTank;assert(!drive_combat_hold(0,&d,origin));
    g_gunLostAt[0]=H->frame;assert(!drive_combat_hold(0,&d,origin));g_gunLostAt[0]=0;
    d.manual=1;assert(!drive_combat_hold(0,&d,origin));d.manual=0;
    enemyTank.position[2]=10;assert(!drive_combat_hold(0,&d,origin));
    enemyTank.position[2]=450;d.combatHoldAt=0;assert(!drive_combat_hold(0,&d,origin));
    for(int k=0;k<10;k++){H->frame+=30;assert(!drive_combat_hold(0,&d,origin));}
    puts("PASS: V23 - armour never slows for a target any more (V8-V22: 2 s slow, 6 s full pace - the convoys' slowdown-go); it fires on the move");
    puts("PASS: actual armor stand-off against visible ground targets, aircraft/occlusion/manual/team and point-blank exclusions");
    return 0;
}
