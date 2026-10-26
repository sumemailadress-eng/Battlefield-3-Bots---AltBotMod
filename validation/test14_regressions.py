"""Actual Test14 director, pilot dispatch and driver integration, with mocked
engine entities, seats, collision and terrain. These tests do not run BF3."""
from pathlib import Path
import argparse, os, re, subprocess, tempfile
from test4_checks import function
import driver_departure_checks as driver
import ground_job_checks as jobs
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src';OLD=ROOT/'rollback/Test13/src'

AIR=r'''
int main(void){
 NavGrid ready={300,300,2,10,-1500,-1500};
 reset("AH1Z",2);g_roundEpoch++;bm_defaults(&g_enhanced);g_enhanced.air.weapons=1;
 g_enhanced.returnToBase=g_enhanced.armored=1;H->botCount=32;vehicleCount=24;grid=&ready;Vehicle pool[24];
 for(int team=1;team<=2;team++){
  g_hqCpKnown[team]=1;g_hqCpPos[team][0]=team==1?-1500:1500;
  for(int k=0;k<16;k++){int i=(team-1)*16+k;H->bots[i]=players[i];H->botInput[i]=inputs[i];
   *(int*)(players[i]+8)=team;*(float**)(players[i]+16)=soldiers[i];soldiers[i][0]=g_hqCpPos[team][0];soldiers[i][3]=100;
   brains[i].enabled=1;brains[i].targetFlag=-1;}
  for(int k=0;k<12;k++){int i=(team-1)*12+k;pool[i]=vehicle;pool[i].team=team;
   strcpy(pool[i].name,k==0?"F16":k<7?"AH1Z":"M1Abrams");pool[i].seats=k==0?1:2;
   pool[i].xf[12]=g_hqCpPos[team][0]+k*100;pool[i].xf[3]=pool[i].xf[7]=pool[i].xf[11]=NAN;
   for(int seat=0;seat<5;seat++)pool[i].entries[seat]=pool[i].seatsMemory[seat]+4;
   vehicles[i]=(char*)&pool[i];}
 }
 H->frame=100000;fleet_air_dispatch();
 for(int team=1;team<=2;team++){
  int air,jets,ground,total;fleet_counts(team,&air,&jets,&ground,&total);
  assert(air==6&&jets==1&&!ground&&total==16);
 }
 // The actual pending reservation -> fleet tick -> native seat -> flight input chain.
 for(int i=0;i<32;i++)if(g_fleet[i].vehicle){
  Vehicle *v=(Vehicle*)g_fleet[i].vehicle;assert(v->team==player_team(players[i]));
  memcpy(soldiers[i],v->xf+12,12);fleet_tick(i);assert(g_air[i].phase==AIR_WALK&&g_air[i].pilot);
 }
 for(int i=0;i<32;i++)if(!g_air[i].phase)brains[i].task=99;
 for(int i=0;i<32;i++)if(g_air[i].phase){air_tick(i);assert(g_air[i].phase==AIR_WAIT&&controlled[i]==g_air[i].veh);}
 H->frame+=(unsigned)(g_enhanced.air.waitSeconds*30)+1;
 for(int i=0;i<32;i++)if(g_air[i].phase){air_tick(i);assert(g_air[i].phase==AIR_FLY&&g_air[i].output.throttle>0);
  air_apply(i,inputs[i]);assert(((float*)(inputs[i]+0x10))[EIA_THROTTLE]>0);}
 fleet_air_dispatch();
 for(int team=1;team<=2;team++){int air,jets,ground,total;fleet_counts(team,&air,&jets,&ground,&total);assert(air==6&&jets==1);}
 puts("PASS: actual pre-ground dispatcher assigns both teams one jet and five choppers, boards native seats and emits takeoff inputs; no seventh pilot; NaN SIMD padding does not reject usable aircraft");
 float valid[16]={0};valid[0]=valid[5]=valid[10]=1;valid[3]=valid[7]=valid[11]=valid[15]=NAN;
 assert(bm_air_transform_valid(valid));valid[5]=NAN;assert(!bm_air_transform_valid(valid));
 // An empty map aircraft claimed by a distant walking bot goes to its nearby teammate.
 reset("AH1Z",2);g_roundEpoch++;g_enhanced.returnToBase=1;H->frame=1000;
 soldiers[0][0]=-500;g_air[0].phase=AIR_WALK;g_air[0].pilot=1;g_air[0].veh=(char*)&vehicle;
 g_air[0].profile=bm_air_profile("AH1Z");g_air[0].at=100;g_drive[0].phase=9;
 H->frame=1019;fleet_release_distant_claim(1);
 assert(!g_air[0].phase&&!g_drive[0].phase&&g_fleet[1].vehicle==(char*)&vehicle&&g_fleet[1].role==FLEET_HELI);
 fleet_tick(1);air_tick(1);assert(controlled[1]==(char*)&vehicle&&g_air[1].pilot);
 puts("PASS: actual far pilot claim transfers to a nearby teammate and boards an abandoned helicopter");return 0;
}
'''
DRIVER=driver.MAIN[:driver.MAIN.index('int main(void)')]+r'''
int main(void){
 for(int team=1;team<=2;team++){
  reset(team);DriveState*d=&g_drive[0];d->phase=2;d->boardAt=1;d->flag=0;H->frame=1000;
  d->progressAt=H->frame-6*30;d->goal[1]=77;routeDeferred=1;
  drive_tick(0);assert(d->phase==3&&d->n==1&&d->partial&&d->pts[2]==12&&!exits);
  H->frame++;drive_tick(0);assert(H->botMove[0][0]>0.9f&&controlled[0]&&!exits);
  reset(team);d=&g_drive[0];d->phase=2;d->boardAt=1;d->flag=0;H->frame=1000;
  d->progressAt=H->frame-6*30;routeDeferred=1;corridorClear=0;
  drive_tick(0);assert(d->phase==2&&!H->botMove[0][0]&&controlled[0]&&!exits);
  corridorClear=1;H->frame+=3*30;drive_tick(0);assert(d->phase==3&&d->partial);
  // Captures/director changes keep the driver and route to the new objective.
  dutyFlag=2;d->phase=4;H->frame++;drive_tick(0);
  assert(d->flag==2&&d->phase==2&&controlled[0]&&!exits);dutyFlag=-1;
 }
 puts("PASS: complete native driver for both teams leaves clear deployment despite queued A*, rejects blocked corridors, then resumes objectives and retargets captured flags without ejecting crew");return 0;
}
'''
JOB=r'''
int main(void){
 H->botCount=2;H->bots[0]=players[0];H->bots[1]=players[1];controlled[0]=controlled[1]=(char*)&vehicleHealth;
 DriveState*d=&g_drive[0];d->phase=4;d->veh=controlled[0];d->flag=0;d->jobAt=1;
 float pos[3]={0};g_flagCount=3;owners[0]=2;owners[1]=owners[2]=1;
 H->frame=60*30;drive_hold_objective(0,d,pos);
 if(BASELINE){assert(d->phase==4);H->frame+=60*30;drive_hold_objective(0,d,pos);assert(d->phase==4);
  puts("REPRODUCED: Test13 last-enemy-flag support job stops permanently because there is no different hostile flag");return 0;}
 assert(d->phase==2&&d->flag==0&&dist2(d->goal,pos)>100&&controlled[0]&&controlled[1]&&!stops&&!exits);
 // While all flags are friendly, local routing cannot undo the director's patrol trip.
 owners[0]=1;preferred=0;d->phase=4;d->jobAt=H->frame;drive_hold_objective(0,d,pos);assert(d->phase==4&&d->flag==0);
 H->frame+=8*30;drive_hold_objective(0,d,pos);assert(d->phase==2&&d->flag==0);
 preferred=1;flags[1][0]=500;d->phase=4;d->jobAt=H->frame;
 drive_hold_objective(0,d,pos);assert(d->phase==2&&d->flag==1&&d->goal[0]==500&&!stops&&!exits);
 puts("PASS: actual last-enemy-flag job repositions after eight seconds; a committed patrol does not bounce between competing allocators; captured flag advances when the director changes duty");return 0;
}
'''
DIRECTOR=r'''
int main(void){
 reset("M1Abrams",3);g_roundEpoch++;H->botCount=32;g_flagCount=2;H->frame=10000;
 flagOwners[0]=flagOwners[1]=0;flags[0][0]=-600;flags[0][2]=0;flags[1][0]=600;flags[1][2]=0;
 for(int i=0;i<32;i++){H->bots[i]=players[i];*(int*)(players[i]+8)=i<16?1:2;
  *(float**)(players[i]+16)=soldiers[i];soldiers[i][3]=100;brains[i].enabled=1;brains[i].targetFlag=-1;}
 director_tick();
 for(int i=0;i<32;i++){int role,f=director_target(i,&role);assert(f>=0&&role==BM_OBJECTIVE_ATTACK);
  brains[i].targetFlag=f;g_flagRoute[i].pending=1;}
 int cancels=routeCancels;H->frame+=30;director_tick();assert(routeCancels==cancels);
 // A completed side fight returns to the committed attack; close/path threats stay.
 int role,f=director_target(0,&role);brains[0].task=TASK_FIGHT;brains[0].prevTask=TASK_NONE;
 brains[0].taskUntil=H->frame+300;g_fightTarget[0]=players[16];soldiers[16][0]=0;soldiers[16][2]=300;
 H->frame+=30;director_tick();assert(brains[0].task==TASK_NONE&&director_target(0,&role)==f&&brains[0].targetFlag==-1);
 brains[0].task=TASK_FIGHT;brains[0].taskUntil=H->frame+300;soldiers[16][0]=flags[f][0]*.5f;soldiers[16][2]=0;
 H->frame+=30;director_tick();assert(brains[0].task==TASK_FIGHT);
 brains[0].task=TASK_FIGHT;soldiers[16][0]=0;soldiers[16][2]=20;H->frame+=30;director_tick();assert(brains[0].task==TASK_FIGHT);
 // Explicit human commands and dead soldiers receive no automatic objective.
 brains[1].commander=human;soldiers[2][3]=0;H->frame+=30;director_tick();
 assert(director_target(1,&role)==-1&&director_target(2,&role)==-1);
 puts("PASS: actual native director preserves pending routes, resumes objectives after distant side engagements, retains immediate/path threats, excludes human commands and dead soldiers");return 0;
}
'''

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--sanitize',action='store_true');a=ap.parse_args()
 base=(ROOT/'validation/air_native_tests.c').read_text().split('int main(void)')[0].replace('#include "../files/BotMod/src/','#include "').replace('#define MAX_BOTS 8','#define MAX_BOTS 32').replace('vehicles[8]','vehicles[32]')
 air=base+'\n#include "activity_state.h"\n#include "air_dispatch.inc"\n'+AIR
 pre=re.sub(r'^static int drive_(?:director_tick|clear_departure)\([^\n]*\n','',driver.PRE,flags=re.M)
 pre=pre.replace('static int tactic_segment(int i,const float*a,const float*b){(void)i;(void)a;(void)b;return 1;}', 'static int corridorClear=1;static int tactic_segment(int i,const float*a,const float*b){(void)i;(void)a;(void)b;return corridorClear;}')
 pre=pre.replace('typedef struct {int enabled,task,targetFlag;}BotBrain;', 'typedef struct {int enabled,task,targetFlag,waiting,enroute;}BotBrain;')
 pre+='\n#include "director_policy.h"\nstatic int dutyFlag=-1;static int director_target(int i,int*role){(void)i;*role=BM_OBJECTIVE_PATROL;return dutyFlag;}\nstatic int tactic_probe_budget(void){return 1;}\n'
 driving=pre+(SRC/'drive_orders.inc').read_text()+(SRC/'drive_staging.inc').read_text()+function((SRC/'core.c').read_text(),'drive_tick')+DRIVER
 jobs_pre=re.sub(r'^static int director_target\([^\n]*','static int preferred=-1;static int director_target(int i,int*role){(void)i;*role=3;return preferred;}',jobs.PRE,flags=re.M)
 native=re.sub(r'^static int director_target\([^\n]*','static int director_target(int i,int*role);',base,flags=re.M).replace('int enabled,task,prevTask,targetFlag,', 'unsigned taskUntil;int enabled,task,prevTask,targetFlag,')
 native+='\n#include "director_policy.h"\nstatic int g_overflowFlag[MAX_BOTS];\nstatic float*human_pos(char*p){return (float*)*(char**)(p+OFF_PLAYER_SOLDIER);}\n#include "director_native.inc"\n'+DIRECTOR
 bodies={'air-dispatch':air,'driver-clear-road':driving,'native-director':native,
         'test13-last-flag':jobs_pre+(OLD/'ground_jobs.inc').read_text()+JOB,
         'test14-last-flag':jobs_pre+(SRC/'ground_jobs.inc').read_text()+JOB}
 with tempfile.TemporaryDirectory(prefix='bf3-test14-')as tmp:
  for name,body in bodies.items():
   p=Path(tmp)/(name+'.c');p.write_text(body);exe=p.with_suffix('')
   cmd=['gcc','-std=c11','-Wall','-Wextra','-Wno-unused-function','-Wno-unused-variable','-DBASELINE='+str(int(name=='test13-last-flag')),'-I',str(SRC),str(p),*[str(SRC/n)for n in ['enhanced.c','air.c','modes.c']],'-lm','-o',str(exe)]
   if a.sanitize:cmd[1:1]=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']
   subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
if __name__=='__main__':main()
