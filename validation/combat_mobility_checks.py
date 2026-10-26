"""Actual combat/policy/weapon and own-hull ray functions, with engine mocks."""
from pathlib import Path
import argparse,re,subprocess,tempfile
from test4_checks import function
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src';core=(SRC/'core.c').read_text()
POLICY=r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "enhanced.h"
int main(void){
 BMConfig c;bm_defaults(&c);assert(c.infantryRange==260 && c.reconRange==450);   /* V25: 260 / 450 m (180 / 320) */
 assert(c.lightMaxSpeed==34 && c.tankMaxSpeed==16 && c.ifvMaxSpeed==20);
 assert(bm_gun_range(&c,bm_vehicle_profile("T90"),0)==800);   /* V25: 800 m (650) */
 assert(bm_gun_range(&c,bm_vehicle_profile("LAV25"),1)==400);   /* V25: 400 m (300) */
 assert(!bm_hold_vehicle("QuadBike",1) && !bm_hold_vehicle("KLR650",1));
 assert(bm_hold_vehicle("M1Abrams",0) && bm_hold_vehicle("HMMWV",1) && !bm_hold_vehicle("HMMWV",0));
 assert(bm_set(&c,"InfantryEngageMeters","nan")==-1 && c.infantryRange==260);
 assert(bm_set(&c,"ReconEngageMeters","501")==-1 && bm_set(&c,"WeaponAttachments","2")==-1);
 unsigned checks=0;
 for(float speed=180;speed<=400;speed+=20)for(float dist=50;dist<=900;dist+=25)for(float height=-80;height<=80;height+=20){
  float flight,lift;int ok=bm_ground_ballistics(speed,9.81f,dist,height,&flight,&lift);
  if(ok){float tan=(height+lift)/dist,angle=atanf(tan),x=speed*cosf(angle)*flight,y=speed*sinf(angle)*flight-4.905f*flight*flight;
   assert(isfinite(lift) && fabsf(x-dist)<0.025f && fabsf(y-height)<0.025f);checks++;
  }
 }
 float t,h;assert(!bm_ground_ballistics(20,9.81f,650,0,&t,&h));
 assert(bm_ground_ballistics(200,9.81f,650,0,&t,&h) && t>3.2f);
 assert(bm_infantry_burst(0,0,160)==6 && bm_infantry_burst(0,0,25)==18);
 assert(bm_infantry_pause(1,250)==14 && bm_infantry_tolerance(&c,0,150)<0.01f);
 assert(bm_drive_horizon(34,0)>bm_drive_horizon(12,0));
 assert(bm_drive_obstacle_speed(5,34,0)==0 && bm_drive_obstacle_speed(40,20,0)>10);
 printf("PASS: %u exact ballistic impact checks, longer ranges, burst/reaction configuration, speed/braking and useful objective vehicles\n",checks);return 0;
}
'''
FIGHT=r'''
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "enhanced.h"
#define MAX_BOTS 2
#define OFF_PLAYER_SOLDIER 16
#define OFF_PLAYER_ONLINE_NAME 64
#define OFF_SOLDIER_HEALTH 12
#define OFF_SOLDIER_POSITION 0
#define OFF_SOLDIER_SPAWN_POSITION 0
#define OFF_SOLDIER_YAW 16
#define OFF_SOLDIER_PITCH 20
#define OFF_SOLDIER_POSE 24
#define TASK_NONE 0
#define TASK_FIGHT 1
#define TASK_DEFEND 2
#define TASK_FOLLOW 3
#define NEVER 0xffffffffu
static unsigned defend_until(int i){(void)i;return NEVER;}   /* AltBotMod V15: a defend job's own deadline (altbotmod_v15_checks) */
#define FOLLOW_FRAMES 900
#define ENGAGE_COOLDOWN_FRAMES 60
#define FIGHT_CHASE_FRAMES 90
#define FIGHT_LOST_FRAMES 90
#define FIGHT_ADVANCE_STOP 55
#define FIGHT_AIM_HEIGHT 0.55f
#define FIGHT_RANGE (g_enhanced.infantryRange*1.15f)
#define RECON_SPOTTED_RANGE (g_enhanced.reconRange)
#define BULLET_SPEED 600
#define SLOT_ACTION(x) (x)
typedef struct {int task,prevTask,step,waiting;unsigned taskUntil,stepAt,replanAt;char *commander,*prevCommander;}BotBrain;
typedef struct {int mode;float x,z,targetYaw;}BotNav;
static struct {unsigned frame;char*bots[2];}host,*H=&host;
static char players[2][128] __attribute__((aligned(8)));static float bodies[2][8];
static BMConfig g_enhanced;static BotNav navs[2];static int blocked,ends;
static char *g_fightTarget[2],*g_cooldownWho[2],*g_testVictim;
static unsigned g_fightMoveUntil[2],g_engageCooldown[2],g_fightSeenAt[2],g_hitAt[2];
static unsigned char g_fightAdvance[2],g_fightChase[2],g_fightSlot[2],g_fightRetreat[2],g_fineAim[2];
static int g_fightBursts[2];static float g_fightLast[2][3],g_fightSpot[2][3],g_aimPitch[2];
static float g_aimBias[3]={0.45f,0.45f,0.45f},g_aimValue[2];static unsigned g_aimValueAt[2];static unsigned char g_aimPose[2];
static float g_aimFixH[2],g_aimFixHAvg=0.4f,g_aimFixV[2],g_aimClimb[2],g_aimFixS[2],g_aimLanding[2],g_aimFixVAvg=0.03f,g_aimClimbAvg=0.001f,g_aimFixSAvg;static unsigned g_burstStart[2];static unsigned char g_aimFixSet[2];static float g_pitchHist[2][16];static unsigned g_pitchHistAt[2][16];
static const BMWeaponProfile *g_primaryDoctrine[2];
static int player_team(char*p){return *(int*)(p+8);}static int player_exists(char*p){return p==players[1];}
static char *player_vehicle(char*p){(void)p;return NULL;}static BotNav *bot_nav(int i){return &navs[i];}
static int solid_between(const float*a,const float*b,float*c){(void)a;(void)b;(void)c;return blocked;}
static float dist2(const float*a,const float*b){float x=a[0]-b[0],z=a[2]-b[2];return x*x+z*z;}
static void lead_point(int i,char*t,const float*p,float d,float*out){(void)i;(void)t;(void)d;memcpy(out,p,12);}
static float yaw_towards(float x,float z){return atan2f(x,z);}
static void go_towards(int i,char*s,float x,float y,float z){(void)i;(void)s;(void)x;(void)y;(void)z;}
static int primary_empty(char*s){(void)s;return 0;}static void press_button(int i,int a,int f){(void)i;(void)a;(void)f;}
static int soldier_ammo2(char*s,int slot,int*l,int*p){(void)s;(void)slot;*l=30;*p=90;return 1;}
static int go_to_cover(int i,char*s,char*t,const float*p){(void)i;(void)s;(void)t;(void)p;return 0;}
static void bot_log(const char*f,...){(void)f;}static const char*kTaskNames[]={"none","fight","defend","follow"};
static void end_task(int i,BotBrain*b,const char*w){(void)i;(void)w;b->task=0;ends++;}
// This fixture isolates weapon timing; complete relocation sequences are in
// combat_flow_checks.py (the actual native movement code).
static struct{unsigned until;}g_combatMove[2];
static void combat_motion_cancel(int i){g_combatMove[i].until=0;}
static int combat_motion_tick(int i,BotBrain*b,char*s,const float*p,float d){(void)i;(void)b;(void)s;(void)p;(void)d;return 0;}
static const BMWeaponProfile *infantry_weapon(int i);static float fight_range(int i,char *t);
// AltBotMod V9 profile hooks: this fixture keeps V8 combat (Difficulty=0); the profile path is in altbotmod_v9_checks.py
static const BMDifficulty *bot_profile(int i){(void)i;return NULL;}
static float fight_tolerance(int i,int recon,float d){(void)i;return g_enhanced.weaponHandling?bm_weapon_tolerance(&g_enhanced,infantry_weapon(i),d):bm_infantry_tolerance(&g_enhanced,recon,d);}
static void fight_posture_start(int i,char*s,float d){(void)i;(void)s;(void)d;}static void fight_posture_end(int i){(void)i;}
static int fight_sidearm_swap(int i,BotBrain*b,char*s,float d,int v){(void)i;(void)b;(void)s;(void)d;(void)v;return 0;}
static int fight_grenade_tick(int i,BotBrain*b,char*s,char*t,int v,float d){(void)i;(void)b;(void)s;(void)t;(void)v;(void)d;return 0;}
static int __attribute__((unused)) sniper_holding(int i){(void)i;return 0;}   /* AltBotMod V10 (sniper overwatch: altbotmod_v10_checks) */
static float hdist(const float*a,const float*b){float dx=a[0]-b[0],dz=a[2]-b[2];return sqrtf(dx*dx+dz*dz);}

'''+re.search(r'^static int is_recon[^\n]+',core,re.M)[0]+'\n'+function(core,'infantry_weapon')+'\n'+function(core,'infantry_trigger_ready')+'\n'+function(core,'fight_range')+'\n#undef FIGHT_ADVANCE_STOP\n#define FIGHT_ADVANCE_STOP 55\n'+'\n'+function(core,'end_fight')+'\n'+function(core,'fight_tick')+r'''
int main(void){
 bm_defaults(&g_enhanced);g_enhanced.aggression=0;   /* Test15 timing; Aggression is covered by test16_regressions */
 for(int k=0;k<2;k++){H->bots[k]=players[k];*(int*)(players[k]+8)=k+1;*(float**)(players[k]+16)=bodies[k];bodies[k][3]=100;}
 strcpy(players[0]+64,"Medic");g_fightAdvance[0]=1;H->frame=1000;g_fightTarget[0]=players[1];bodies[1][2]=160;
 BotBrain b={.task=TASK_FIGHT,.taskUntil=10000};assert(fight_tick(0,&b,(char*)bodies[0]) && b.step==1 && b.stepAt==1006);
 assert(navs[0].mode==4);bodies[0][5]=g_aimPitch[0];H->frame=1005;fight_tick(0,&b,(char*)bodies[0]);assert(b.step==1);
 H->frame=1006;fight_tick(0,&b,(char*)bodies[0]);assert(b.step==2 && b.stepAt==1010 && g_fineAim[0]);
 assert(infantry_trigger_ready(0,(char*)bodies[0]));bodies[0][5]+=0.2f;assert(!infantry_trigger_ready(0,(char*)bodies[0]));bodies[0][5]=g_aimPitch[0];
 blocked=1;H->frame++;fight_tick(0,&b,(char*)bodies[0]);assert(b.step!=2);
 blocked=0;*(int*)(players[1]+8)=1;fight_tick(0,&b,(char*)bodies[0]);assert(!g_fightTarget[0] && !g_fineAim[0] && ends==1);
 *(int*)(players[1]+8)=2;strcpy(players[0]+64,"Recon");bodies[1][2]=300;g_fightTarget[0]=players[1];b=(BotBrain){.task=TASK_FIGHT,.taskUntil=10000};
 assert(fight_tick(0,&b,(char*)bodies[0]) && b.step==1 && fight_range(0,players[1])>300);
 puts("PASS: actual 0.2-second reaction, 160m rifle burst, 300m recon range, occlusion releases fire, team change ends fight");return 0;
}
'''
HULL=r'''
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#define MAX_BOTS 2
static struct{int botCount;}host={2},*H=&host;
static struct{char*veh;}g_drive[2];static char vehicle[80] __attribute__((aligned(8)));
static char g_lastHitRest[32];static int owner=2,mode,calls;
static void *self=(void*)0x1010,*wall=(void*)0x2020;
static int nav_active_path_owner(void){return owner;}static int vehicle_ok(char*v){return v==vehicle;}
static int IsBadReadPtr(void*p,int n){(void)p;(void)n;return 0;}
static int vehicle_crushable_hit(void){return 0;}
static int ray_cast(const float*a,const float*b,float*p,float*n){
 (void)b;memset(n,0,12);calls++;
 if(mode==3){memcpy(g_lastHitRest,&self,sizeof self);memcpy(p,a,12);return 1;}
 if(a[0]<2){memcpy(g_lastHitRest,&self,sizeof self);p[0]=2;p[1]=p[2]=0;return 1;}
 if(mode==1 || mode==2){memcpy(g_lastHitRest,&wall,sizeof wall);p[0]=8;p[1]=p[2]=0;return 1;}return 0;
}
'''+function(core,'drive_nav_ray')+r'''
int main(void){
 g_drive[0].veh=vehicle;memcpy(vehicle+0x30,&self,sizeof self);float a[3]={0},b[3]={10,0,0},hit[3];
 mode=0;assert(!drive_nav_ray(a,b,hit,NULL) && calls==2);
 calls=0;mode=1;assert(drive_nav_ray(a,b,hit,NULL) && calls==2 && hit[0]==8);
 owner=-1;calls=0;assert(drive_nav_ray(a,b,hit,NULL) && calls==1);
 owner=2;mode=3;calls=0;assert(drive_nav_ray(a,b,hit,NULL) && calls==5);
 puts("PASS: native own-hull retrace clears only own hull; wall behind remains blocked; no owner and repeated self-hit fail closed");return 0;
}
'''
WEAPONS=r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "enhanced.h"
#define MAX_BOTS 8
#define OFF_PLAYER_ONLINE_NAME 64
#define ADDR_WEAPON_UNLOCK_VTABLE 0x02504b34u
#define BM_PRIMARY_MESSAGE_OFFSET 60
#define BM_ATTACHMENT_MESSAGE_OFFSET 72
typedef struct{void**slot;char name[160];int loadout;unsigned vtable;}Fixup;
typedef struct{unsigned char weaponMsg[1][128];int nWeapons;}Loadout;
typedef struct{int count;Loadout l[1];}LoadoutLib;
typedef struct{unsigned vt;char name[160];}Asset;
static Asset assets[84];static int assetN,missingWeapon,missingPart;
static struct{unsigned pad,count;void*items[4];}stockArray;
static LoadoutLib lib;static BMConfig g_enhanced;
static struct{int botCount;char*bots[8];}host,*H=&host;
static char players[8][128] __attribute__((aligned(8)));
static LoadoutLib *loadout_lib(void){return &lib;}static unsigned fb_count(void**a){unsigned n;memcpy(&n,(char*)a-4,4);return n;}
static const char *asset_name(void*a){return ((Asset*)a)->name;}
static int IsBadReadPtr(void*a,int n){(void)n;return !a || a==(void*)-1;}
static int bot_index(char*p){for(int i=0;i<H->botCount;i++)if(H->bots[i]==p)return i;return -1;}
static int player_team(char*p){return *(int*)(p+8);}
static unsigned asset_cache_vt(const char*n){(void)n;return 0;}
static void bot_log(const char*f,...){(void)f;}static void asset_cache_save(Fixup*f,int n){(void)f;(void)n;}
static int fast_resolve_assets(Fixup*f,int n){
 for(int k=0;k<n;k++)if(!(missingWeapon && k%5==0) && !(missingPart && k%5==2)){
  Asset*a=&assets[assetN++];a->vt=f[k].vtable;snprintf(a->name,sizeof a->name,"%s",f[k].name);*f[k].slot=a;
 }return n;
}
#include "weapon_variety.inc"
static void setup(void){
 assetN=4;memset(assets,0,sizeof assets);memset(g_weaponVariantLife,0,sizeof g_weaponVariantLife);lib.count=1;lib.l[0].nWeapons=1;
 const char*names[]={"Weapons/Common/NoOptics","Weapons/Common/NoPrimaryAccessory","Weapons/Common/NoSecondaryAccessory","Weapons/Common/NoCamo"};
 stockArray.count=4;for(int p=0;p<4;p++){assets[p].vt=0x02100000+p;strcpy(assets[p].name,names[p]);stockArray.items[p]=&assets[p];}
 void**a=stockArray.items;memcpy(lib.l[0].weaponMsg[0]+64,&a,sizeof a);
 bm_defaults(&g_enhanced);H->botCount=8;for(int i=0;i<8;i++){H->bots[i]=players[i];*(int*)(players[i]+8)=i<4?1:2;strcpy(players[i]+64,"Medic");}
}
static void release(void){for(int k=0;k<BM_WEAPON_VARIANTS;k++)if(g_weaponVariants[k].array)free((char*)g_weaponVariants[k].array-4);}
int main(void){
 setup();assert(!g_primaryDoctrine[0]);weapon_variety_load();for(int k=0;k<16;k++)assert(g_weaponVariants[k].ready && g_weaponVariants[k].attached);
 for(int team=0;team<2;team++)for(int k=0;k<4;k++)assert(weapon_variant_pick(players[team*4+k],"Assault")==k);
 assert(weapon_variant_pick(players[0],"Assault")==1);
 unsigned char msg[128],old[128];memset(msg,0x5a,128);memcpy(old,msg,128);weapon_variant_message(msg,1);
 void*gun;void**atts;memcpy(&gun,msg+60,sizeof gun);memcpy(&atts,msg+72,sizeof atts);assert(gun==g_weaponVariants[1].weapon && atts==g_weaponVariants[1].array && fb_count(atts)==4);
 for(int k=0;k<128;k++)if(!(k>=60 && k<68) && !(k>=72 && k<80))assert(msg[k]==old[k]);
 g_enhanced.weaponAttachments=0;weapon_variant_message(msg,1);memcpy(&atts,msg+72,sizeof atts);void*first;memcpy(&first,atts,sizeof first);assert(first==g_stockAttachments[0]);
 release();setup();missingPart=1;weapon_variety_load();assert(g_weaponVariants[0].ready && !g_weaponVariants[0].attached);release();
 setup();missingPart=0;missingWeapon=1;weapon_variety_load();assert(weapon_variant_pick(players[0],"Assault")==-1);release();
 setup();g_enhanced.weaponVariety=0;weapon_variety_load();assert(weapon_variant_pick(players[0],"Assault")==-1);
 puts("PASS: actual 16 weapon/attachment variants, class/team distribution, respawn rotation, gadget/message preservation, partial asset and disabled-option fallbacks");return 0;
}
'''
DETECT=r'''
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "enhanced.h"
#define OFF_PLAYER_SOLDIER 16
#define OFF_PLAYER_VEHICLE 24
#define OFF_PLAYER_ONLINE_NAME 64
#define OFF_SOLDIER_POSITION 0
#define OFF_SOLDIER_SPAWN_POSITION 0
#define OFF_SOLDIER_HEALTH 12
#define OFF_SOLDIER_YAW 16
#define DETECT_RANGE_INFANTRY (g_enhanced.infantryRange*1.1f)
#define RECON_SPOTTED_RANGE (g_enhanced.reconRange)
#define ENGAGE_RANGE (g_enhanced.infantryRange)
#define DETECT_RANGE_VEHICLE 250
#define DETECT_ALL_AROUND 25
#define DETECT_COS_HALF_FOV .5f
#define RAY_DONT_CHECK_CHARACTER 1
#define SPOT_COOLDOWN_FRAMES 90
static struct{unsigned frame;char*bots[1];}host,*H=&host;
static char players[64][128] __attribute__((aligned(8))),managerBytes[260] __attribute__((aligned(8)));
static float bodies[64][8];static void*rows[64];static BMConfig g_enhanced;
static struct{int enabled;}brain={1};static unsigned g_rayFlags,g_spottedAt[64];static char*g_spottedWho[64],*selected;
static float selectedDistance;static int rays,blockedBelow;
static char*player_manager(void){return managerBytes+4;}static int player_team(char*p){return *(int*)(p+8);}
static char*player_vehicle(char*p){(void)p;return NULL;}static __typeof__(brain)*bot_brain(int i){(void)i;return &brain;}
static int is_recon(int i){(void)i;return 0;}static int spotted_cached(char*p){(void)p;return 1;}
static int sniper_rifle(int i){(void)i;return 0;}static float sniper_reach(int i){(void)i;return 400;}   /* AltBotMod V10 */
static int bot_spot(char*p,char*t){(void)p;(void)t;return 1;}static void bot_log(const char*f,...){(void)f;}
static int ray_cast(const float*a,const float*b,float*hit,float*n){(void)a;(void)n;rays++;memcpy(hit,b,12);return b[2]<blockedBelow;}
static void engage_seen(int i,char*p,char*v,float d){(void)i;(void)v;selected=p;selectedDistance=d;}
'''+function(core,'detect_tick')+r'''
int main(void){
 bm_defaults(&g_enhanced);H->frame=100;H->bots[0]=players[0];
 for(int k=0;k<64;k++){rows[k]=players[k];*(int*)(players[k]+8)=k?2:1;*(float**)(players[k]+16)=bodies[k];bodies[k][2]=k*2;bodies[k][3]=100;}
 void**array=rows;memcpy(player_manager()+0xac,&array,sizeof array);unsigned n=64;memcpy(player_manager()+8,&n,4);
 blockedBelow=1000;detect_tick(0);assert(rays==8 && !selected);
 rays=0;blockedBelow=8;detect_tick(0);assert(rays==4 && selected==players[4] && selectedDistance==8);
 rays=0;blockedBelow=0;*(int*)(players[1]+8)=1;detect_tick(0);assert(rays==1 && selected==players[2]);
 puts("PASS: actual full 64-player range/FOV shortlist bounds LOS at eight rays, nearest visible enemy and friendly exclusion");return 0;
}
'''

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--sanitize',action='store_true');a=ap.parse_args()
 with tempfile.TemporaryDirectory(prefix='bf3-test8-combat-') as tmp:
  tmp=Path(tmp)
  for name,body in [('policy',POLICY),('fight',FIGHT),('detect',DETECT),('hull',HULL),('weapons',WEAPONS)]:
   p=tmp/(name+'.c');p.write_text(body);exe=p.with_suffix('')
   flags=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'] if a.sanitize else ['-O2']
   subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror',*flags,'-I',str(SRC),str(p),str(SRC/'enhanced.c'),str(SRC/'air.c'),'-lm','-o',str(exe)],check=True)
   subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
