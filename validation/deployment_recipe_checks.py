"""Actual observation/replay adapter using the native air/fleet engine fixture.
The known call wrapper is mocked; a low image-address vtable exercises the
real callback validation. No BF3 menu spawner is discovered by this test.
"""
from pathlib import Path
import argparse,re,subprocess,tempfile
from test4_checks import function
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src'
base=(ROOT/'validation/air_native_tests.c').read_text().split('int main(void)')[0]
base=base.replace('#include "../files/BotMod/src/', '#include "')
base=base.replace('#include <assert.h>', '#define _GNU_SOURCE\n#include <assert.h>\n#include <stdint.h>\n#include <sys/mman.h>')
base=base.replace('float botMove[MAX_BOTS][3]; BotTeamSpawn', 'float botMove[MAX_BOTS][3]; void *teamBlueprint[3]; BotTeamSpawn')
state=(SRC/'mode_state.h').read_text();state=state[state.index('static struct {void *target;'):state.index('static void deployment_observe')]
base=base.replace('static struct{unsigned at,epoch;int choice,team;}g_deployActive[MAX_BOTS];\nstatic struct{const BMAirProfile *profile;}g_deployChoice[32];',state+'\nstatic char *g_lastSpawnedBot[MAX_BOTS];')
base=base.replace('static int player_exists(char *p){return bot_index(p)>=0;}', 'static int player_exists(char *p){return p==human || bot_index(p)>=0;}\nstatic int is_bot(char *p){return bot_index(p)>=0;}')
base=base.replace('static char *player_vehicle(char *p) { int i=bot_index(p); return i<0 ? NULL : controlled[i]; }', 'static char *humanVehicle;static int humanSeat;\nstatic char *player_vehicle(char *p) { int i=bot_index(p); return p==human?humanVehicle:i<0?NULL:controlled[i]; }')
base=base.replace('return player_vehicle(p)==v ? playerSeats[bot_index(p)] : -1;', 'return player_vehicle(p)==v ? (p==human?humanSeat:playerSeats[bot_index(p)]) : -1;')
extra=r'''
#define OFF_PLAYER_BLUEPRINT 24
typedef void (*SetPtr_t)(void*,void*);
static void set_blueprint(void *bot,void *value){memcpy((char*)bot+OFF_PLAYER_BLUEPRINT,&value,sizeof value);}
#define ADDR_PLAYER_SET_BLUEPRINT set_blueprint
static int IsBadReadPtr(const void*p,size_t n){(void)n;return p==NULL;}
static int kits,requests,squadRequests,notes,spawnMode;static void *requestedTarget;static unsigned requestedFunction,requestedArg;
static void apply_kit(char *p){(void)p;kits++;}
static int squadmate_spawned_on_recently(char*p){(void)p;return 0;}
static void squadmate_note_spawn(char*p){(void)p;notes++;}
static void deployment_request(void*target,unsigned fn,char*bot,unsigned arg){
 requests++;requestedTarget=target;requestedFunction=fn;requestedArg=arg;
 int i=bot_index(bot);if(spawnMode){*(float**)(bot+OFF_PLAYER_SOLDIER)=soldiers[i];if(spawnMode==2)enter_vehicle(bot,&vehicle,0);}
}
static char deployment_squad_request(char*bot,char*mate){(void)mate;squadRequests++;int i=bot_index(bot);*(float**)(bot+OFF_PLAYER_SOLDIER)=soldiers[i];return enter_vehicle(bot,&vehicle,3);}
'''
deploy=(SRC/'deployment_native.inc').read_text()
for name in ['deployment_request','deployment_squad_request']:deploy=deploy.replace(function(deploy,name),'')
MAIN=r'''
static unsigned target[2];
static void prepare(const char*name,int seats){
 reset(name,seats);memset(g_deployPending,0,sizeof g_deployPending);memset(g_deployChoice,0,sizeof g_deployChoice);
 memset(human,0,sizeof human);*(int*)(human+8)=1;humanVehicle=NULL;humanSeat=0;requests=squadRequests=notes=kits=spawnMode=0;
 H->teamBlueprint[1]=H->teamBlueprint[2]=players[3];g_enhanced.air.pilotsPerTeam=6;g_enhanced.returnToBase=1;
 target[0]=0x00500000;target[1]=0;
}
static void learn(int team){
 *(int*)(human+8)=team;deployment_observe(target,human,0x1234);
 assert(g_deployPending[0].at && !g_deployChoice[0].target);
 *(float**)(human+OFF_PLAYER_SOLDIER)=soldiers[3];humanVehicle=(char*)&vehicle;humanSeat=0;H->frame+=10;deployment_tick();
 assert(g_deployChoice[0].target==target && g_deployChoice[0].virtualSpawn && g_deployChoice[0].team==team && g_deployChoice[0].arg==0x1234);
}
int main(void){
 void *image=mmap((void*)0x00500000,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);assert(image==(void*)0x00500000);
 *(unsigned*)((char*)image+0x6c)=0x00510000;
 for(int team=1;team<=2;team++){
  prepare("F16",1);learn(team);*(int*)(players[0]+8)=team;*(void**)(players[0]+16)=NULL;
  assert(deployment_spawn(players[0]) && requests==1 && requestedTarget==target && requestedFunction==0x00510000 && requestedArg==0x1234);
  assert(deployment_spawn(players[0]) && requests==1); // no duplicate soldier request
  *(float**)(players[0]+16)=soldiers[0];assert(enter_vehicle(players[0],&vehicle,0));H->frame++;deployment_tick();
  assert(!g_deployActive[0].at && g_air[0].pilot && g_air[0].phase && g_lastSpawnedBot[0]==players[0] && !exits);
 }
 puts("PASS: human-confirmed deployment recipe on BOTH teams replays the exact observed native request, awaits completion and confirms its pilot without duplicate bodies or suicides");
 prepare("F16",1);deployment_observe(target,players[0],1);assert(!g_deployPending[0].at);
 deployment_observe(target,human,1);H->frame+=10;*(float**)(human+16)=soldiers[0];humanVehicle=(char*)&vehicle;humanSeat=1;deployment_tick();assert(!g_deployChoice[0].target);
 prepare("F16",1);learn(1);*(void**)(players[0]+16)=NULL;*(int*)(players[0]+8)=2;assert(!deployment_spawn(players[0]));
 *(int*)(players[0]+8)=1;g_roundEpoch++;assert(!deployment_spawn(players[0]));g_roundEpoch--;
 target[0]=0x00500100;assert(!deployment_spawn(players[0]));target[0]=0x00500000;
 g_enhanced.air.pilotsPerTeam=0;assert(!deployment_spawn(players[0]));
 puts("PASS: bot observations, cargo-seat results, wrong-team, expired-round, changed-vtable and exhausted-pilot recipes are refused");
 prepare("F16",1);learn(1);*(void**)(players[0]+16)=NULL;
 for(int failure=1;failure<=3;failure++){
  H->frame=g_deployChoice[0].retryAt+1;assert(deployment_spawn(players[0]));H->frame+=61;deployment_tick();
  assert(!g_deployActive[0].at && !*(void**)(players[0]+16) && g_deployChoice[0].failures==failure && !exits);
 }
 assert(g_deployChoice[0].retryAt-H->frame==180*30);
 H->frame=g_deployChoice[0].retryAt+1;spawnMode=1;assert(deployment_spawn(players[0]));H->frame++;deployment_tick();
 assert(*(void**)(players[0]+16) && !g_air[0].phase && !g_deployActive[0].at && !exits);
 puts("PASS: unavailable menu requests back off after three failures; an unexpected ordinary body is retained, never killed or replaced");
 prepare("Ka-60",5);assert(enter_vehicle(players[1],&vehicle,0));vehicle.xf[13]=100;
 *(void**)(players[0]+16)=NULL;managerEnabled=1;playerRows[0]=players[1];void **rows=playerRows;unsigned count=1;memcpy(manager+0xac,&rows,sizeof rows);memcpy(manager+8,&count,4);
 assert(deployment_spawn(players[0]) && squadRequests==1 && notes==1 && playerSeats[0]==3 && g_air[0].phase==AIR_RIDE && !g_air[0].pilot);
 assert(controlled[1] && free_seat_count((char*)&vehicle)>=1 && !exits);
 puts("PASS: actual flying friendly transport squad-spawn path adopts cargo seat and retains pilot plus human capacity");
 munmap(image,4096);return 0;
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');a=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='bf3-deployment-recipes-') as tmp:
  f=Path(tmp)/'checks.c';f.write_text(base+extra+deploy+MAIN);exe=f.with_suffix('')
  opts=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'] if a.sanitize else ['-O2']
  subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable',*opts,'-I',str(SRC),str(f),str(SRC/'enhanced.c'),str(SRC/'air.c'),str(SRC/'modes.c'),'-lm','-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
