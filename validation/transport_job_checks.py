"""Actual transport jobs with mocked passengers, terrain and aircraft pose."""
from pathlib import Path
import argparse,os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src'
base=(ROOT/'validation/air_native_tests.c').read_text().split('int main(void)')[0].replace('#include "../files/BotMod/src/','#include "')
MAIN=r'''
static NavGrid ready={300,300,2,10,-1500,-1500};
static void prepare(int team){
 reset("Ka-60",5);grid=&ready;g_enhanced.air.transportDrops=1;g_flagCount=3;
 flags[0][0]=0;flags[0][2]=500;flags[1][0]=600;flags[1][2]=500;flags[2][0]=-600;flags[2][2]=500;
 for(int i=0;i<4;i++)*(int*)(players[i]+8)=team;
 assert(air_claim(0,(char*)&vehicle,1,0));assert(enter_vehicle(players[0],&vehicle,0));
 g_air[0].phase=AIR_FLY;bm_air_start(&g_air[0].control,BM_AIR_TRANSPORT,1);
 g_air[0].control.phase=BM_FLIGHT_CRUISE;vehicle.xf[13]=100;
}
static void seek(AirNativeState*a){
 for(int k=0;k<H->botCount+3&&!a->pickupPlayer;k++){air_transport_job(0,vehicle.xf+12);H->frame++;}
}
int main(void){
 for(int team=1;team<=2;team++){
  prepare(team);for(int i=1;i<4;i++)brains[i].task=TASK_FIGHT;
  AirNativeState*a=&g_air[0];air_transport_job(0,vehicle.xf+12);
  assert(!a->pickupPlayer&&dist2(a->goal,vehicle.xf+12)>100*100);
  int flag=a->transportPatrol;H->frame+=601;air_transport_job(0,vehicle.xf+12);
  assert(a->transportPatrol!=flag&&dist2(a->goal,vehicle.xf+12)>100*100);
  prepare(team);a=&g_air[0];soldiers[1][0]=200;soldiers[1][1]=0;soldiers[1][2]=0;
  brains[2].task=brains[3].task=TASK_FIGHT;seek(a);
  assert(a->pickupPlayer==players[1]&&a->pickup);
  H->frame+=601;air_transport_job(0,vehicle.xf+12);
  assert(!a->pickupPlayer&&a->pickupAvoid==players[1]&&a->pickupAvoidUntil>H->frame);
  assert(controlled[0]&&!exits&&dist2(a->goal,vehicle.xf+12)>100*100);
  brains[2].task=TASK_NONE;soldiers[2][0]=-300;soldiers[2][1]=0;
  H->frame=a->pickupRetryAt;seek(a);
  assert(a->pickupPlayer==players[2]&&a->pickup);
  assert(enter_vehicle(players[2],&vehicle,3));air_transport_job(0,vehicle.xf+12);
  assert(!a->pickup&&!a->pickupPlayer&&!a->dropping&&controlled[0]&&!exits);
 }
 puts("PASS: empty transports on BOTH teams fly moving patrol goals instead of hovering at home; stalled pickups are released after 20s, another eligible passenger can be selected, and loaded transports retain delivery duty");return 0;
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');a=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='bf3-transport-jobs-')as tmp:
  f=Path(tmp)/'checks.c';f.write_text(base+MAIN);exe=f.with_suffix('');cmd=['gcc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-I',str(SRC),str(f),*[str(SRC/n)for n in ['enhanced.c','air.c','modes.c']],'-lm','-o',str(exe)]
  if a.sanitize:cmd[1:1]=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']
  subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
if __name__=='__main__':main()
