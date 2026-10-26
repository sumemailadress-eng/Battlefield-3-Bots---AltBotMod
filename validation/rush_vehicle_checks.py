"""Actual fleet/air adapters in Rush, with mocked native seats/ownership and terrain."""
from pathlib import Path
import argparse,os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src'
base=(ROOT/'validation/air_native_tests.c').read_text().split('int main(void)')[0].replace('#include "../files/BotMod/src/', '#include "')
MAIN=r'''
static NavGrid ready={300,300,2,10,-1500,-1500};
static void prepare(const char*name,int seats,int team){
 reset(name,seats);g_mode=BM_MODE_RUSH;g_flagCount=2;grid=&ready;g_enhanced.returnToBase=g_enhanced.armored=1;
 vehicle.team=team;for(int i=0;i<4;i++)*(int*)(players[i]+8)=team;
 flags[0][0]=0;flags[0][2]=500;flags[1][0]=100;flags[1][2]=700;g_compEpoch=g_roundEpoch;
}
int main(void){
 const char*names[]={"M1Abrams","Growler","F16","AH1Z","Ka-60"};int seats[]={3,3,1,2,5};
 for(int team=1;team<=2;team++)for(int kind=0;kind<5;kind++){
  prepare(names[kind],seats[kind],team);fleet_tick(0);
  if(kind<2)assert(g_drive[0].phase==1&&g_drive[0].goal[2]==500);else{assert(g_air[0].phase==AIR_WALK&&g_air[0].pilot);air_tick(0);assert(player_vehicle(players[0])==(char*)&vehicle && vehicle_seat_of((char*)&vehicle,players[0])==0);air_goal(0,vehicle.xf+12);assert(g_air[0].flag==0);}
 }
 puts("PASS: actual Rush fleet dispatch claims tanks, jeeps, jets, attack and transport helicopters for BOTH teams using the shared objective provider");
 for(int team=1;team<=2;team++){
  prepare("M1Abrams",3,team);assert(enter_vehicle(players[1],&vehicle,1));fleet_takeover(1);H->frame+=6;fleet_takeover(1);assert(playerSeats[1]==0&&g_drive[1].phase==2&&!exits);
  prepare("AH1Z",2,team);assert(enter_vehicle(players[1],&vehicle,1));fleet_takeover(1);H->frame+=6;fleet_takeover(1);assert(playerSeats[1]==0&&g_air[1].pilot&&!exits);
  prepare("M1Abrams",3,team);bm_rush_event(&g_rush,1,BM_RUSH_DESTROYED,H->frame);bm_rush_event(&g_rush,2,BM_RUSH_DESTROYED,H->frame);assert(!fleet_reserve(0,soldiers[0]));
  assert(enter_vehicle(players[0],&vehicle,0));fleet_takeover(0);assert(g_drive[0].phase==2 && g_drive[0].flag==-1 && controlled[0] && !exits);
  prepare("AH1Z",2,team);bm_rush_event(&g_rush,1,BM_RUSH_DESTROYED,H->frame);bm_rush_event(&g_rush,2,BM_RUSH_DESTROYED,H->frame);
  assert(enter_vehicle(players[1],&vehicle,1));fleet_takeover(1);H->frame+=6;fleet_takeover(1);assert(g_air[1].pilot&&playerSeats[1]==0);air_goal(1,vehicle.xf+12);assert(g_air[1].flag==-1&&isfinite(g_air[1].goal[0]));
 }
 puts("PASS: Rush passenger takeover preserves ground/air controls on BOTH teams; a completed objective pair stops new walking claims while retaining occupied ground seats and valid aircraft fallback goals");return 0;
}
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--sanitize',action='store_true');args=ap.parse_args()
 with tempfile.TemporaryDirectory(prefix='bf3-rush-vehicle-')as tmp:
  f=Path(tmp)/'checks.c';f.write_text(base+MAIN);exe=f.with_suffix('');cmd=['gcc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-I',str(SRC),str(f),*[str(SRC/n)for n in ['enhanced.c','air.c','modes.c']],'-lm','-o',str(exe)]
  if args.sanitize:cmd[1:1]=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']
  subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
if __name__=='__main__':main()
