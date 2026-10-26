"""Simultaneous team aircraft dispatch through the actual native adapter."""
from pathlib import Path
import argparse,os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src'
base=(ROOT/'validation/air_native_tests.c').read_text().split('int main(void)')[0].replace('#include "../files/BotMod/src/','#include "').replace('#define MAX_BOTS 8','#define MAX_BOTS 32').replace('vehicles[8]','vehicles[32]')
MAIN=r'''
int main(void){
 reset("AH1Z",2);bm_defaults(&g_enhanced);g_enhanced.air.helicopters=g_enhanced.air.jets=g_enhanced.armored=1;
 g_enhanced.returnToBase=1;H->botCount=32;vehicleCount=24;Vehicle pool[24];
 for(int team=1;team<=2;team++){
  g_hqCpKnown[team]=1;g_hqCpPos[team][0]=team==1?-1000:1000;
  for(int k=0;k<16;k++){int i=(team-1)*16+k;H->bots[i]=players[i];H->botInput[i]=inputs[i];
   *(int*)(players[i]+8)=team;*(float**)(players[i]+16)=soldiers[i];soldiers[i][0]=g_hqCpPos[team][0];
   brains[i].enabled=1;brains[i].targetFlag=-1;}
  for(int k=0;k<12;k++){int i=(team-1)*12+k;pool[i]=vehicle;pool[i].team=team;
   strcpy(pool[i].name,k?"AH1Z":"F16");pool[i].seats=k?2:1;pool[i].xf[12]=g_hqCpPos[team][0]+k*4;
   for(int seat=0;seat<5;seat++)pool[i].entries[seat]=pool[i].seatsMemory[seat]+4;
   vehicles[i]=(char*)&pool[i];}
 }
 H->frame=100000;g_fleetPriorityUntil=1;
 for(int k=0;k<6;k++)for(int team=1;team<=2;team++){
  int i=(team-1)*16+k;assert(fleet_reserve(i,soldiers[i]));
  assert(((Vehicle*)g_fleet[i].vehicle)->team==team);
  assert(g_fleet[i].role==(k?FLEET_HELI:FLEET_JET));
  assert(air_claim(i,g_fleet[i].vehicle,1,0));
 }
 for(int team=1;team<=2;team++){
  int air,jets,ground,total;fleet_counts(team,&air,&jets,&ground,&total);
  assert(air==6&&jets==1&&ground==0&&total==16);
  int i=(team-1)*16+6;assert(!fleet_reserve(i,soldiers[i])&&!fleet_pilot_allowed(i));
  g_enhanced.air.pilotsPerTeam=99;assert(!air_claim(i,vehicles[(team-1)*12+6],1,0));g_enhanced.air.pilotsPerTeam=6;
 }
 puts("PASS: BOTH teams simultaneously reserve one jet and five helicopters at their own deployment, after opening priority expires; twelve pilots coexist, each team refuses a seventh even with malformed settings");
 reset("M1Abrams",3);bm_defaults(&g_enhanced);g_enhanced.armored=1;H->botCount=32;
 vehicleCount=32;Vehicle groundPool[32];H->frame=100000;g_fleetPriorityUntil=1;
 g_flagCount=2;flagOwners[0]=1;flagOwners[1]=2;flags[0][0]=-500;flags[1][0]=500;
 for(int team=1;team<=2;team++){
  g_hqCpKnown[team]=1;g_hqCpPos[team][0]=team==1?-1000:1000;
  for(int k=0;k<16;k++){int i=(team-1)*16+k;H->bots[i]=players[i];H->botInput[i]=inputs[i];
   *(int*)(players[i]+8)=team;*(float**)(players[i]+16)=soldiers[i];soldiers[i][0]=g_hqCpPos[team][0];
   brains[i].enabled=1;brains[i].targetFlag=-1;groundPool[i]=vehicle;groundPool[i].team=team;
   groundPool[i].xf[12]=g_hqCpPos[team][0]+k*4;
   for(int seat=0;seat<5;seat++)groundPool[i].entries[seat]=groundPool[i].seatsMemory[seat]+4;
   vehicles[i]=(char*)&groundPool[i];}
 }
 for(int k=0;k<16;k++)for(int team=1;team<=2;team++){
  int i=(team-1)*16+k;assert(fleet_reserve(i,soldiers[i]));
  assert(((Vehicle*)g_fleet[i].vehicle)->team==team);
  g_drive[i].phase=1;g_drive[i].veh=g_fleet[i].vehicle;g_fleet[i].vehicle=NULL;
 }
 for(int team=1;team<=2;team++){int air,jets,ground,total;fleet_counts(team,&air,&jets,&ground,&total);assert(ground==16&&total==16&&!air);}
 puts("PASS: BOTH teams simultaneously dispatch all sixteen available tanks, with independent driver limits and no later-round infantry quota blocking a vehicle");return 0;
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');a=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='bf3-team-air-')as tmp:
  f=Path(tmp)/'checks.c';f.write_text(base+MAIN);exe=f.with_suffix('');cmd=['gcc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-I',str(SRC),str(f),*[str(SRC/n)for n in ['enhanced.c','air.c','modes.c']],'-lm','-o',str(exe)]
  if a.sanitize:cmd[1:1]=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']
  subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
if __name__=='__main__':main()
