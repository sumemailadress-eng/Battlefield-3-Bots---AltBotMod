"""Actual non-Conquest spawning and Rush sector observation with native APIs mocked."""
from pathlib import Path
import subprocess,tempfile,argparse,os,re
from test4_checks import function
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src';core=(SRC/'core.c').read_text();native=(SRC/'modes_native.inc').read_text();state=(SRC/'mode_state.h').read_text()
PRE=r'''
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "modes.h"
#include "rush_policy.h"
#define MAX_BOTS 8
#define OFF_PLAYER_SOLDIER 16
#define OFF_PLAYER_BLUEPRINT 24
#define OFF_PLAYER_ONLINE_NAME 64
#define OFF_SOLDIER_POSITION 0
#define TASK_NONE 0
typedef void(*SetPtr_t)(void*,void*);
typedef void*(*CseSpawn_t)(void*,void*,unsigned,unsigned,unsigned);
static struct {unsigned frame;int botCount;char*bots[MAX_BOTS];struct{void*entity;unsigned a2,a3,a4;}teamSpawn[5];void*teamBlueprint[3];}host,*H=&host;
static char players[MAX_BOTS][128] __attribute__((aligned(8))),human[128] __attribute__((aligned(8)));
static float bodies[MAX_BOTS][4],humanBody[4];
static struct {unsigned vt;int team;float pos[3];int refused,accept,hasAt;float at[3];}spawns[256];
static int g_modeSpawnsLoose;static unsigned char g_modeSpawnNo[256];   /* AltBotMod V15 */
static struct {void *entity,*soldier;int slot,team,stage;unsigned a2,a3,a4,at;} g_rushBlind[MAX_BOTS];static short g_modeBotSlot[MAX_BOTS];static unsigned char g_modeEarlyDeaths[256][2];   /* AltBotMod V16 */
static char *g_attacker[MAX_BOTS];static unsigned g_attackedAt[MAX_BOTS];static int crewRequests;
static BMRushState g_rushBefore;static int g_rushBeforeStage;static unsigned g_rushBadDeaths;static unsigned char g_mcomDestroyedBy[17];static float g_modeSpawnLand[256][3];static unsigned char g_modeSpawnLandKnown[256];static void rush_watch_tick(void){}   /* AltBotMod V16.1 */
static char *g_modeSpawns[256];static int g_modeSpawnCount;
static struct {void *entity;unsigned a2,a3,a4;int stage;}g_modeTeamSpawn[5];
static struct {void *entity;char*player;unsigned a2,a3,a4,at;}g_modePending[64];
static unsigned char g_modeSpawnOk[256];static unsigned g_modeFailedSpawn[256],g_lastSpawnedAt[MAX_BOTS],g_spawnJoinWait[MAX_BOTS];
static char*g_lastSpawnedBot[MAX_BOTS];
static struct {void *vehicle;unsigned until,queuedAt;}g_fleet[MAX_BOTS];
static void*g_fleetBase[3];static float g_hqPos[3][3];
static struct {int enabled,targetFlag,task,enroute,waiting;}brains[MAX_BOTS];
static struct {int mode;}navs[MAX_BOTS];
static struct {int phase,manual,flag,n,next,partial,stageCandidate,stageActive;void*veh;float goal[3];}g_drive[MAX_BOTS];
static struct {int phase,flag,patrolChosen;unsigned patrolAt;}g_air[MAX_BOTS];
static typeof(brains[0])*bot_brain(int i){return &brains[i];}
static typeof(navs[0])*bot_nav(int i){return &navs[i];}
static enum BMMode g_mode=BM_MODE_RUSH;static int g_rushStage=1;static unsigned g_rushAdvanceAt;static BMRushState g_rush;
static int squads[MAX_BOTS],joins[MAX_BOTS],spawnRequests,kitRequests,fleetRequests;
static int mode_is_cq(void){return g_mode==BM_MODE_CQ;}static int mode_is_squad_rush(void){return 0;}
static int mode_teams(void){return bm_mode_teams(g_mode);}
static int player_team(char*p){return *(int*)(p+8);}
static int bot_index(char*p){for(int i=0;i<H->botCount;i++)if(H->bots[i]==p)return i;return -1;}
static int is_bot(char*p){return bot_index(p)>=0;}
static int player_exists(char*p){return p==human||is_bot(p);}
static int player_squad(char*p){return squads[bot_index(p)];}
static int squad_join(char*p,int team){assert(team==player_team(p));joins[bot_index(p)]++;return 1;}
static int chat_rcon(const char*f,...){(void)f;return 0;}
static int squad_request_recent(char*p){(void)p;return 1;}
static void bot_log(const char*f,...){(void)f;}
static char *player_vehicle(char*p){(void)p;return NULL;}
static int IsBadReadPtr(const void*p,size_t n){(void)n;return !p;}
static int cse_valid(void*e){return e && ((typeof(spawns[0])*)e)->vt==0x025bbbc0;}
static int spawn_team(void*e){return ((typeof(spawns[0])*)e)->team;}
static float *spawn_data_pos(void*e){return e?((typeof(spawns[0])*)e)->pos:NULL;}
static float dist2(const float*a,const float*b){float x=a[0]-b[0],z=a[2]-b[2];return x*x+z*z;}
static int near_origin(const float*p){return fabsf(p[0])<12&&fabsf(p[2])<12;}
static int g_flagCount;static float *flag_pos(int f){(void)f;return NULL;}static int flag_owner(int f){(void)f;return 0;}
static const float *vehicle_pos(void*v,int*t){(void)v;(void)t;return NULL;}
static void nav_cancel_path(int i){(void)i;}
static void set_blueprint(void*p,void*b){memcpy((char*)p+24,&b,sizeof b);}
#define ADDR_PLAYER_SET_BLUEPRINT set_blueprint
static void apply_kit(char*p){(void)p;kitRequests++;}
static int deployment_spawn(char*p){(void)p;return 0;}
static void fleet_spawn(char*p){(void)p;fleetRequests++;}
static int vspawn_prepare(char*b,int t,const char*n){(void)b;(void)t;(void)n;return 0;}
static int crew_spawn(char*b,int t,const char*n){(void)b;(void)t;(void)n;crewRequests++;return 0;}
static void*spawn(void*e,void*p,unsigned a,unsigned b,unsigned c){(void)a;(void)b;(void)c;
 typeof(spawns[0])*sp=e;int i=bot_index(p);assert(i>=0 && (spawn_team(e)==player_team(p) || (!spawn_team(e) && g_modeSpawnsLoose)));spawnRequests++;
 if(sp->refused || (sp->accept && sp->accept!=player_team(p)))return NULL;   /* V15: the engine refuses a team-less entity of the other side */
 memcpy(bodies[i],sp->hasAt?sp->at:spawn_data_pos(e),12);*(float**)((char*)p+16)=bodies[i];return bodies[i];
}
#define ADDR_CSE_SPAWN spawn
'''
PROFILE=re.search(r'typedef struct BMModeProfile.*?} BMModeProfile;',state,re.S)[0];BOT=re.search(r'static struct \{int target.*?g_modeBot\[MAX_BOTS\];',state)[0]
body=PRE+PROFILE+'\n#include "mode_profiles.inc"\nstatic const BMModeProfile*g_modeProfile;\n'+BOT+'\n'+function(core,'spawn_wait_for_squad')+'\n'+'\n'.join(function(native,n)for n in ['mode_profile','mode_inside','mode_base_present','objective_count','objective_pos','objective_wanted','mode_vehicle_target','rush_sync_stage','mode_observe_spawn','mode_rush_blind_tick','mode_note_death','mode_observation_tick','mode_spawn'])+r'''
static void reset(const char*level,const char*mode){
 memset(&host,0,sizeof host);memset(players,0,sizeof players);memset(spawns,0,sizeof spawns);memset(g_modeTeamSpawn,0,sizeof g_modeTeamSpawn);memset(g_modePending,0,sizeof g_modePending);memset(g_modeFailedSpawn,0,sizeof g_modeFailedSpawn);memset(g_spawnJoinWait,0,sizeof g_spawnJoinWait);memset(squads,0,sizeof squads);memset(joins,0,sizeof joins);memset(g_lastSpawnedAt,0,sizeof g_lastSpawnedAt);
 g_modeProfile=mode_profile(level,mode);assert(g_modeProfile);g_mode=bm_mode(mode);g_rushStage=1;bm_rush_reset(&g_rush,g_modeProfile->stages,2,100);
 H->frame=100;H->botCount=mode_teams();spawnRequests=kitRequests=fleetRequests=0;g_modeSpawnCount=0;
 for(int t=1;t<=mode_teams();t++){H->bots[t-1]=players[t-1];*(int*)(players[t-1]+8)=t;brains[t-1].enabled=1;H->teamBlueprint[(t-1)%2+1]=human;
  spawns[g_modeSpawnCount].vt=0x025bbbc0;spawns[g_modeSpawnCount].team=t;
  memcpy(spawns[g_modeSpawnCount].pos,g_mode==BM_MODE_RUSH?g_modeProfile->bases[1][t]:g_modeProfile->patrol[t],12);g_modeSpawns[g_modeSpawnCount]=(char*)&spawns[g_modeSpawnCount];g_modeSpawnCount++;
 }
}
int main(void){
 int maps=0;
 for(unsigned k=0;k<sizeof g_modeProfiles/sizeof g_modeProfiles[0];k++)if(!strcmp(g_modeProfiles[k].mode,"RushLarge0")){
  reset(g_modeProfiles[k].level,"RushLarge0");maps++;
  for(int t=1;t<=2;t++){assert(!mode_spawn(players[t-1]));}assert(spawnRequests==0); // rejected asynchronous squad requests
  H->frame+=151;for(int t=1;t<=2;t++){assert(mode_spawn(players[t-1]));assert(g_lastSpawnedAt[t-1]==251 && !squads[t-1]);assert(dist2(bodies[t-1],g_modeProfile->bases[1][t])<0.01f);}
  assert(fleetRequests==2&&kitRequests==2);
 }
 assert(maps==25);puts("PASS: actual mode_spawn naturally deploys BOTH human-free teams on all 25 Rush profiles after the bounded failed-squad deadline; team, base, loadout and vehicle requisition preserved");
 reset("XP3_Alborz","RushLarge0");squads[0]=1;spawns[0].refused=1;
 spawns[2]=spawns[0];spawns[2].refused=0;spawns[2].pos[0]+=1;g_modeSpawns[2]=(char*)&spawns[2];g_modeSpawnCount=3;
 assert(!mode_spawn(players[0])&&g_modeFailedSpawn[0]==400&&!g_lastSpawnedAt[0]);assert(mode_spawn(players[0])&&g_lastSpawnedAt[0]==100);
 *(void**)(players[0]+16)=NULL;spawns[2].refused=1;assert(!mode_spawn(players[0]));int previous=spawnRequests;assert(!mode_spawn(players[0])&&spawnRequests==previous);H->frame=401;spawns[0].refused=0;assert(mode_spawn(players[0]));
 puts("PASS: native refused spawns rotate to another validated current-team child, cool down ten seconds, and never mark a failed deployment successful");
 reset("MP_017","SquadDeathMatch0");for(int t=1;t<=4;t++)assert(!mode_spawn(players[t-1]));H->frame+=151;for(int t=1;t<=4;t++)assert(mode_spawn(players[t-1])&&player_team(players[t-1])==t);
 puts("PASS: four-team SQDM shares the bounded squad wait and alternates the two observed soldier blueprints without changing native team assignment");
 reset("XP3_Alborz","RushLarge0");assert(g_modeProfile->stages>1);squads[0]=1;
 const float*next=g_modeProfile->bases[2][1];assert(dist2(next,g_modeProfile->bases[1][1])>10000);
 memcpy(spawns[2].pos,next,12);spawns[2].vt=0x025bbbc0;spawns[2].team=1;g_modeSpawns[2]=(char*)&spawns[2];g_modeSpawnCount=3;
 spawns[0].refused=1;assert(!mode_spawn(players[0]));assert(!mode_spawn(players[0]));assert(!*(void**)(players[0]+16)); // won't probe the locked later base
 memcpy(bodies[0],next,12);*(float**)(players[0]+16)=bodies[0];mode_observe_spawn(&spawns[2],players[0],1,1,1);H->frame+=10;mode_observation_tick();assert(g_rushStage==1);
 memset(human,0,sizeof human);*(int*)(human+8)=1;*(float**)(human+16)=humanBody;memcpy(humanBody,next,12);mode_observe_spawn(&spawns[2],human,123,456,789);H->frame+=10;mode_observation_tick();assert(g_rushStage==2&&g_rush.stage==2&&g_modeTeamSpawn[1].entity==&spawns[2]&&g_modeTeamSpawn[1].a2==123 && !g_modeTeamSpawn[2].entity);
 *(void**)(players[0]+16)=NULL;assert(mode_spawn(players[0])&&dist2(bodies[0],next)<0.01f);
 puts("PASS: bots cannot probe or advance future Rush sectors; a successful human menu deployment confirms the next sector and reuses its exact native spawn arguments; no forced body, suicide or timer progression");
 /* AltBotMod V15: Tehran Highway TDM on 2026-10-11 - two team-less spawn entities, neither in the play area by its recorded position */
 reset("MP_003","TeamDeathMatch0");squads[0]=squads[1]=1;g_modeSpawnsLoose=1;g_modeSpawnCount=2;memset(spawns,0,sizeof spawns);memset(g_modeSpawnOk,0,sizeof g_modeSpawnOk);memset(g_modeSpawnNo,0,sizeof g_modeSpawnNo);memset(g_modeFailedSpawn,0,sizeof g_modeFailedSpawn);
 for(int k=0;k<2;k++){spawns[k].vt=0x025bbbc0;spawns[k].pos[0]=5000+k;spawns[k].pos[2]=5000;spawns[k].accept=2-k;spawns[k].hasAt=1;memcpy(spawns[k].at,g_modeProfile->patrol[k],12);g_modeSpawns[k]=(char*)&spawns[k];}
 assert(!mode_spawn(players[0])&&(g_modeSpawnNo[0]&1)&&!(g_modeSpawnNo[0]&2));          /* team 1 tried RU's: refused for team 1 only */
 assert(mode_spawn(players[0])&&(g_modeSpawnOk[1]&1));                                    /* the other one works and is remembered */
 assert(!mode_spawn(players[1]));H->frame+=91;assert(mode_spawn(players[1])&&(g_modeSpawnOk[0]&2)); /* team 2 skips team 1's, waits out the 3 s rest on its own */
 *(void**)(players[0]+16)=NULL;int before=spawnRequests;assert(mode_spawn(players[0])&&spawnRequests==before+1&&dist2(bodies[0],g_modeProfile->patrol[1])<0.01f); /* straight to its own */
 g_modeSpawnNo[0]|=1;g_modeSpawnNo[1]|=1;*(void**)(players[0]+16)=NULL;assert(!mode_spawn(players[0])&&!(g_modeSpawnNo[0]&1)&&!(g_modeSpawnNo[1]&1)); /* all refused once: tried again */
 puts("PASS: V15 loose spawning - with no spawn entity in the play area (Tehran Highway TDM, V14: never started) bots try the level's team-less ones, each side keeps the one the engine accepts, a refusal rests 3 s for that side only");
 /* Rush with team-less entities (Grand Bazaar on 2026-10-11): a blind spawn must land at the bot's own sector base */
 reset("XP3_Alborz","RushLarge0");squads[0]=1;g_modeSpawnsLoose=1;g_modeSpawnCount=2;memset(spawns,0,sizeof spawns);H->frame=150;memset(g_modeSpawnOk,0,sizeof g_modeSpawnOk);memset(g_modeSpawnNo,0,sizeof g_modeSpawnNo);memset(g_modeFailedSpawn,0,sizeof g_modeFailedSpawn);memset(g_modeTeamSpawn,0,sizeof g_modeTeamSpawn);
 const float *b1=g_modeProfile->bases[1][1],*b2=g_modeProfile->bases[2][1];
 for(int k=0;k<2;k++){spawns[k].vt=0x025bbbc0;spawns[k].pos[0]=2+k;spawns[k].accept=1;spawns[k].hasAt=1;memcpy(spawns[k].at,k?b2:b1,12);g_modeSpawns[k]=(char*)&spawns[k];}
 /* V16: judged half a second later - right after the spawn call the soldier still stands at (0, 0, 0) */
 float zero[3]={0,0,0};memcpy(spawns[1].at,zero,12);
 assert(mode_spawn(players[0])&&g_rushBlind[0].at&&g_modeFailedSpawn[1]==0);
 H->frame+=15;mode_rush_blind_tick();assert(g_rushBlind[0].at&&g_modeFailedSpawn[1]==0);   /* not placed yet: looked at again, nothing judged */
 memcpy(bodies[0],b2,12);H->frame+=15;mode_rush_blind_tick();
 assert(!g_rushBlind[0].at&&g_modeFailedSpawn[1]==180+120*30&&!(g_modeSpawnOk[1]&1)&&!g_modeTeamSpawn[1].entity); /* wrong sector: rests 2 min */
 *(void**)(players[0]+16)=NULL;assert(mode_spawn(players[0])&&dist2(bodies[0],b1)<0.01f&&!g_modeTeamSpawn[1].entity);
 H->frame+=15;mode_rush_blind_tick();assert(g_modeTeamSpawn[1].entity==&spawns[0]&&g_modeTeamSpawn[1].stage==1);
 *(void**)(players[0]+16)=NULL;assert(mode_spawn(players[0])&&dist2(bodies[0],b1)<0.01f);
 puts("PASS: V15/V16 Rush without team spawn entities - a blind spawn is judged once the soldier is placed (V15 judged it at (0, 0, 0)); one away from the bot's sector base rests that entity for the team 2 min; one at the base is learned for the sector");
 /* V16: deaths with no shooter right after spawning (MP_Subway Rush on V15: defenders put in sector 3 died out of bounds in a loop) */
 g_attacker[0]=human;g_attackedAt[0]=H->frame;mode_note_death(players[0]);assert(!g_modeEarlyDeaths[0][0]);   /* shot: an ordinary death */
 *(void**)(players[0]+16)=NULL;assert(mode_spawn(players[0]));g_attacker[0]=NULL;H->frame+=200;mode_note_death(players[0]);assert(g_modeEarlyDeaths[0][0]==1&&g_modeTeamSpawn[1].entity==&spawns[0]);
 *(void**)(players[0]+16)=NULL;assert(mode_spawn(players[0]));H->frame+=26*30;mode_note_death(players[0]);assert(g_modeEarlyDeaths[0][0]==1);   /* lived 26 s: not counted */
 *(void**)(players[0]+16)=NULL;assert(mode_spawn(players[0]));H->frame+=60;mode_note_death(players[0]);
 assert(g_modeFailedSpawn[0]==H->frame+180*30&&!g_modeTeamSpawn[1].entity&&!(g_modeSpawnOk[0]&1));
 puts("PASS: V16 out-of-bounds spawns - two deaths with no shooter within 25 s of spawning at one entity rest it for that team 3 min (a shot death or a longer life does not count)");
 return 0;
}
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--sanitize',action='store_true');args=ap.parse_args()
 with tempfile.TemporaryDirectory(prefix='bf3-mode-spawn-')as tmp:
  f=Path(tmp)/'checks.c';f.write_text(body);exe=f.with_suffix('');cmd=['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-I',str(SRC),str(f),str(SRC/'modes.c'),'-lm','-o',str(exe)]
  if args.sanitize:cmd[1:1]=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']
  subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
if __name__=='__main__':main()
