"""Exercise complete cmd_spawn and drive -> final movement inputs, with native APIs mocked.
These are sequence regressions, not live BF3/replication or terrain tests.
"""
from pathlib import Path
import argparse,subprocess,tempfile
from test4_checks import function
import driver_departure_checks as driver
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src';core=(SRC/'core.c').read_text()
SPAWN=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "modes.h"
#define MAX_BOTS 4
#define OFF_PLAYER_SOLDIER 16
#define OFF_PLAYER_BLUEPRINT 24
#define OFF_PLAYER_ONLINE_NAME 64
#define BOT_ROSTER(pl) ((const char *)(pl) + OFF_PLAYER_ONLINE_NAME)   /* V28 */
#define OFF_SOLDIER_POSITION 0
#define OFF_SOLDIER_SPAWN_POSITION 0
#define OFF_SOLDIER_HEALTH 12
#define OFF_CP_CONTROL_TEAM 0
#define MAX_HQ_PROBES 4
#define __attribute__(x)
typedef struct {void *entity;unsigned a2,a3,a4;} BotTeamSpawn;
typedef void(*SetPtr_t)(void*,void*);
typedef char(*AddToTeam_t)(void*,int,int,int);
typedef void*(*CseSpawn_t)(void*,void*,unsigned,unsigned,unsigned);
typedef struct{char *bot;int team,cand;unsigned at;}HqProbe;
static struct{unsigned frame;int botCount;char *bots[MAX_BOTS];BotTeamSpawn teamSpawn[3];void *teamBlueprint[3];}host,*H=&host;
static char players[2][128] __attribute__((aligned(8))),bodies[2][32] __attribute__((aligned(8))),hq[2][32];
static unsigned g_spawnJoinWait[MAX_BOTS],g_lastSpawnedAt[MAX_BOTS];
static struct {unsigned spawnArgs[3];}g_fleet[MAX_BOTS];
static char *g_lastSpawnedBot[MAX_BOTS];static float g_hqPos[3][3];
static unsigned blueprint=0x02500e38;static void *g_jobBlueprint=&blueprint;
static int g_jobEpoch=1,g_roundEpoch=1,g_levelReady=1,g_mode=1,g_flagCount=0,g_hqCandCount=0;
static void *g_hqCand[16],*g_flagSpawn[2][3];static char *g_flags[2];
static unsigned char g_hqCandTried[3][16];static HqProbe g_hqProbes[4];
static int squads[2],requests[2],spawned[2],rejectSpawn,humanCount,blueprintSets,vehicleReject,calls;static void *vehicleEntity;static unsigned expectedArgs[3];
static char *find_player(const char*n){for(int i=0;i<2;i++)if(!strcmp(n,players[i]+64))return players[i];return NULL;}
static int bot_index(char*p){for(int i=0;i<2;i++)if(p==players[i])return i;return -1;}
static int player_team(char*p){return *(int*)(p+8);}static int player_squad(char*p){return squads[bot_index(p)];}
static int squad_request_recent(char*p){(void)p;return 1;} // RCON retries never complete
static int squad_join(char*p,int t){(void)t;requests[bot_index(p)]++;return 1;}
static int chat_rcon(const char*f,...){(void)f;return 0;}
static char add_team(void*p,int t,int a,int b){(void)a;(void)b;*(int*)((char*)p+8)=t;return 1;}
#define ADDR_ADD_TO_TEAM add_team
static void set_blueprint(void*p,void*v){*(void**)((char*)p+24)=v;blueprintSets++;}
#define ADDR_PLAYER_SET_BLUEPRINT set_blueprint
static void *spawn(void*e,void*p,unsigned a,unsigned b,unsigned c){
 int i=bot_index(p);calls++;if(e==vehicleEntity && vehicleReject){assert(a==11&&b==22&&c==33);return NULL;}
 assert(i>=0 && e==hq[i]);if(vehicleEntity){assert(a==expectedArgs[0]&&b==expectedArgs[1]&&c==expectedArgs[2]);}if(rejectSpawn)return NULL;
 assert(*(void**)((char*)p+24)==&blueprint);*(void**)((char*)p+16)=bodies[i];*(float*)(bodies[i]+12)=100;spawned[i]++;return bodies[i];
}
#define ADDR_CSE_SPAWN spawn
static int mode_is_cq(void){return 1;}static int mode_is_squad_rush(void){return 0;}static void mode_spawn(char*p){(void)p;}static int deployment_spawn(char*p){(void)p;return 0;}
static int IsBadReadPtr(void*p,int n){(void)n;return !p;}static int cse_valid(void*p){return p==hq[0]||p==hq[1]||p==vehicleEntity;}
static int hq_select_option(int t){(void)t;return 0;}static void hq_spawn_refused(int t,void*e){(void)t;(void)e;}
static int crew_spawn(char*b,int t,const char*n){(void)b;(void)t;(void)n;return 0;}
static int vspawn_prepare(char*b,int t,const char*n){(void)b;(void)t;(void)n;return 0;}
static void *fleet_spawn(char*p){int i=bot_index(p);g_fleet[i].spawnArgs[0]=11;g_fleet[i].spawnArgs[1]=22;g_fleet[i].spawnArgs[2]=33;return vehicleEntity;}static int squad_objective(int t,int s){(void)t;(void)s;return -1;}
static int squad_defending(int t,int s){(void)t;(void)s;return 0;}static int flag_owner(int f){(void)f;return 0;}
static float *flag_pos(int f){(void)f;return g_hqPos[1];}static float dist2(const float*a,const float*b){float x=a[0]-b[0],z=a[2]-b[2];return x*x+z*z;}
static char *player_manager(void){return NULL;}static int is_bot(char*p){return bot_index(p)>=0;}
static char *player_vehicle(char*p){(void)p;return NULL;}static int free_seat_count(char*v){(void)v;return 0;}
static int squadmate_spawned_on_recently(char*p){(void)p;return 0;}static int squadmate_in_combat(char*p,char*s){(void)p;(void)s;return 0;}
static int squadmate_spawnable(char*p,char*s){(void)p;(void)s;return 0;}static void squadmate_note_spawn(char*p){(void)p;}
static void squadmate_ride(int i,char*v){(void)i;(void)v;}static int spawn_team(void*p){(void)p;return 0;}static int as_spawn(char*b,int t){(void)b;(void)t;return 0;}
static int g_kitsAllDropped;static int pick_loadout(char*p){(void)p;return 0;}static void apply_kit(char*p){(void)p;}static void bot_log(const char*f,...){(void)f;}
'''+function(core,'spawn_wait_for_squad')+'\n'+function(core,'cmd_spawn')+r'''
static void init(void){
 memset(&host,0,sizeof host);memset(players,0,sizeof players);memset(g_spawnJoinWait,0,sizeof g_spawnJoinWait);memset(squads,0,sizeof squads);memset(spawned,0,sizeof spawned);memset(g_lastSpawnedAt,0,sizeof g_lastSpawnedAt);
 H->botCount=2;H->frame=500;rejectSpawn=humanCount=vehicleReject=calls=0;vehicleEntity=NULL;
 for(int i=0;i<2;i++){H->bots[i]=players[i];*(int*)(players[i]+8)=i+1;strcpy(players[i]+64,i?"[BOT] Engineer RU":"[BOT] Medic US");H->teamSpawn[i+1].entity=hq[i];H->teamBlueprint[i+1]=NULL;}
}
int main(void){
 init();for(int i=0;i<2;i++)cmd_spawn(players[i]+64);assert(!spawned[0]&&!spawned[1]);
 H->frame+=151;for(int i=0;i<2;i++)cmd_spawn(players[i]+64);
 assert(spawned[0]==1 && spawned[1]==1 && !humanCount && !squads[0] && !squads[1]);
 assert(g_lastSpawnedAt[0]==651 && g_lastSpawnedAt[1]==651 && blueprintSets==2);
 puts("PASS: actual cmd_spawn deploys BOTH human-free teams with discovered MpSoldier after failed squad moves; repeated recent RCON requests cannot extend the deadline");
 init();squads[0]=squads[1]=1;rejectSpawn=1;cmd_spawn(players[0]+64);cmd_spawn(players[1]+64);
 assert(!g_lastSpawnedAt[0]&&!g_lastSpawnedAt[1]&&!spawned[0]&&!spawned[1]);
 puts("PASS: refused native deployments do not set successful-spawn timestamps");
 init();squads[0]=1;*(int*)(players[0]+8)=2;cmd_spawn(players[0]+64);assert(player_team(players[0])==1 && spawned[0]==1);
 puts("PASS: dead bot's named team is restored before native spawn; no living soldier is killed");
 for(int i=0;i<2;i++){
  init();squads[i]=1;static char flag[32];vehicleEntity=flag;vehicleReject=1;
  expectedArgs[0]=H->teamSpawn[i+1].a2=77;expectedArgs[1]=H->teamSpawn[i+1].a3=88;expectedArgs[2]=H->teamSpawn[i+1].a4=99;
  cmd_spawn(players[i]+64);assert(spawned[i]==1&&calls==2);
 }
 puts("PASS: a refused closer vehicle flag spawn falls back to the real same-team HQ with its exact arguments on BOTH teams");return 0;
}
'''
# Use the complete real driver and movement-to-native-input writer in sequence.
MOTION=driver.PRE+r'''
#define EIA_THROTTLE 0
#define EIA_BRAKE 2
#define EIA_STRAFE 1
#define EIA_YAW 4
static float input[8];static int infantrySteers,gunnerCalls;
typedef void(*SetLevel_t)(void*,int,float);
static void set_level(void*p,int a,float v){((float*)p)[a]=v;}
#define ADDR_ENTRYINPUT_SET_LEVEL set_level
static void fleet_takeover(int i){(void)i;}static void crew_board_tick(int i){(void)i;}static void gunship_tick(int i){(void)i;}static void emplacement_tick(int i){(void)i;}static void air_consider(int i){(void)i;}static void air_tick(int i){(void)i;}
static void steer_bot(int i,char*s){(void)s;infantrySteers++;if(navs[i].mode)memset(H->botMove[i],0,12);}
static void combat_walk_fire(int i){(void)i;}static void combat_dodge_tick(int i){(void)i;}
static void gunner_tick(int i){(void)i;gunnerCalls++;}static void fall_tick(int i){(void)i;}
'''+(SRC/'drive_staging.inc').read_text()+'\n'+function(core,'drive_tick')+'\n'+function(core,'bot_motion_tick')+'\n'+driver.MAIN[:driver.MAIN.index('int main(void)')]+r'''
int main(void){
 for(int team=1;team<=2;team++){
  reset(team);infantrySteers=gunnerCalls=0;memset(input,0,sizeof input);g_driving[0]=1;navs[0].mode=1;
  // The squad members never board. Hold a stale infantry navigation mode too.
  for(H->frame=100;H->frame<200;H->frame++)bot_motion_tick(0,input);
  assert(input[EIA_THROTTLE]>0 && input[EIA_BRAKE]==0 && !infantrySteers && gunnerCalls==100 && controlled[0]);
  g_drive[0].reverseUntil=H->frame+30;bot_motion_tick(0,input);assert(input[EIA_THROTTLE]==0 && input[EIA_BRAKE]>0);
  g_drive[0].reverseUntil=0;bot_motion_tick(0,input);assert(input[EIA_THROTTLE]>0 && input[EIA_BRAKE]==0);
 }
 puts("PASS: complete drive_tick -> bot_motion_tick emits positive native throttle on BOTH teams despite absent passengers and stale infantry navigation; reverse/brake releases on departure");return 0;
}
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--sanitize',action='store_true');args=ap.parse_args()
 with tempfile.TemporaryDirectory(prefix='bf3-spawn-driver-') as tmp:
  for name,body in [('spawn',SPAWN),('motion',MOTION)]:
   f=Path(tmp)/(name+'.c');f.write_text(body);exe=f.with_suffix('')
   cmd=['gcc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-but-set-variable','-g','-I',str(SRC),str(f),str(SRC/'enhanced.c'),str(SRC/'air.c'),str(SRC/'modes.c'),'-lm','-o',str(exe)]
   if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-sanitize-recover=all']
   subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
