"""Actual both-team fleet dispatch and natural vehicle spawn selection."""
from pathlib import Path
import argparse,os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src'
base=(ROOT/'validation/air_native_tests.c').read_text().split('int main(void)')[0].replace('#include "../files/BotMod/src/','#include "')
base=base.replace('static int player_squad(char *p) { (void)p; return 1; }','static int fixtureSquad=1;static int player_squad(char *p) { (void)p; return fixtureSquad; }').replace('static int squad_objective(int t,int s) { (void)t; (void)s; return 0; }','static int squad_objective(int t,int s) { (void)t; return s>0?0:-1; }')
MAIN=r'''
static NavGrid ready={300,300,2,10,-1500,-1500};
static void prepare(int team){
 reset("M1Abrams",3);fixtureSquad=0;grid=&ready;g_enhanced.returnToBase=g_enhanced.armored=1;
 for(int i=0;i<4;i++){*(int*)(players[i]+8)=team;brains[i].targetFlag=-1;}
 flagOwners[0]=3-team;flags[0][0]=0;flags[0][2]=500;vehicle.team=team;
}
int main(void){
 for(int team=1;team<=2;team++){
  prepare(team);fleet_tick(0);assert(g_drive[0].phase==1&&g_drive[0].flag==0&&g_drive[0].goal[2]==500);
  prepare(team);fixtureSquad=1;g_flagCount=2;flagOwners[0]=team;flagOwners[1]=3-team;
  flags[1][0]=0;flags[1][2]=700;fleet_tick(0);assert(g_drive[0].flag==1&&g_drive[0].goal[2]==700);
  // Natural death leads to a spawn near a farther captured-flag tank.
  prepare(team);H->teamSpawn[team].entity=team==1?human:players[3];
  H->teamSpawn[team].a2=123;H->teamSpawn[team].a3=456;H->teamSpawn[team].a4=789;
  *(void**)(players[0]+OFF_PLAYER_SOLDIER)=NULL;g_flagCount=2;flagOwners[1]=team;
  flags[1][0]=0;flags[1][2]=2800;g_flagSpawn[1][team]=players[2];vehicle.xf[14]=2805;
  assert(fleet_spawn(players[0])==players[2]&&g_fleet[0].vehicle==(char*)&vehicle);
  assert(g_fleet[0].spawnArgs[0]==0xffffffff&&g_fleet[0].spawnArgs[1]==1&&!g_fleet[0].spawnArgs[2]);
  // When that flag is contested, the original same-team deployment is used.
  memset(&g_fleet[0],0,sizeof g_fleet[0]);controls[1]=3-team;
  assert(fleet_spawn(players[0])==H->teamSpawn[team].entity&&g_fleet[0].spawnArgs[0]==123);
  // Assault defenders with no HQ can use a validated owned flag.
  prepare(team);*(void**)(players[0]+16)=NULL;g_flagCount=2;flagOwners[1]=team;
  flags[1][2]=5000;g_flagSpawn[1][team]=players[2];vehicle.xf[14]=5005;
  assert(fleet_spawn(players[0])==players[2]);
  prepare(team);*(void**)(players[0]+16)=NULL;g_flagCount=2;flagOwners[1]=3-team;
  flags[1][2]=5000;g_flagSpawn[1][team]=players[2];vehicle.xf[14]=5005;
  assert(!fleet_spawn(players[0]));
  prepare(team);*(void**)(players[0]+16)=NULL;g_flagCount=2;flagOwners[1]=team;
  flags[1][2]=5000;g_flagSpawn[1][team]=team==1?players[3]:human;vehicle.xf[14]=5005;
  assert(!fleet_spawn(players[0]));
 }
 puts("PASS: actual unsquadded bots on BOTH teams get a valid vehicle objective; natural deaths choose a validated owned spawn near a distant tank, with contested/enemy-team spawn rejection and no-HQ defender support");
 bm_defaults(&g_enhanced);assert(g_enhanced.groundDriversPerTeam==16&&g_enhanced.air.pilotsPerTeam==6&&g_enhanced.returnMeters==3000);
 for(int total=1;total<=64;total++){H->frame=100000;g_fleetPriorityUntil=100;assert(fleet_capacity(total)==total);}
 assert(bm_set(&g_enhanced,"GroundDriversPerTeam","32")&&g_enhanced.groundDriversPerTeam==32);
 puts("PASS: default 16 ground drivers and six pilots per team; optional 32-driver setting accepted; every bot remains eligible for fleet duty after the opening minute");return 0;
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');a=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='bf3-fleet-spawn-')as tmp:
  f=Path(tmp)/'checks.c';f.write_text(base+MAIN);exe=f.with_suffix('');cmd=['gcc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-I',str(SRC),str(f),*[str(SRC/n)for n in ['enhanced.c','air.c','modes.c']],'-lm','-o',str(exe)]
  if a.sanitize:cmd[1:1]=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']
  subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
if __name__=='__main__':main()
