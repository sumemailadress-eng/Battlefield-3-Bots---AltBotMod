"""Exercise actual Test8 driver job functions and native arrival/timeout blocks.
BF3 seats, ownership, physics and logging are mocked; this does not run a server.
"""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile
from test4_checks import function

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'files/BotMod/src'
core=(SRC/'core.c').read_text()
drive_type=re.search(r'typedef struct DriveState \{[\s\S]*?\} DriveState;',core)[0]
drive=function(core,'drive_tick')

def block(needle):
    start=drive.index(needle)
    masked=re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"',lambda m:' '*len(m[0]),drive)
    at=masked.index('{',start)
    depth=1
    for end in range(at+1,len(masked)):
        depth+=(masked[end]=='{')-(masked[end]=='}')
        if not depth:return drive[start:end+1]
    raise ValueError(needle)

PRE=r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "enhanced.h"
#define MAX_BOTS 8
#define MAX_FLAGS 16
#define DRIVE_MAX_PTS 128
#define DRIVE_STOP_SHORT 18.0f
#define OFF_PLAYER_ONLINE_NAME 0
#define OFF_VEHICLE_HEALTH 0
#define TASK_NONE 0
''' + drive_type + r'''
typedef struct {int targetFlag,enroute,waiting,task,avoidFlag;unsigned avoidUntil;}BotBrain;
typedef struct {int mode;}BotNav;
typedef struct {float maxSeen;}VehTrack;
static struct {unsigned frame;int botCount;char *bots[MAX_BOTS];float botMove[MAX_BOTS][3];}host,*H=&host;
static DriveState g_drive[MAX_BOTS];static unsigned char g_driving[MAX_BOTS];
static BotBrain brains[MAX_BOTS];static BotNav navs[MAX_BOTS];
static float flags[MAX_FLAGS][3],centres[33][3];static int owners[MAX_FLAGS],g_flagCount=3;
static int g_squadTarget[3][33];static unsigned char g_squadDefend[3][33];
static int team=1;static int stops,exits,boat,bad,cancels;static float vehicleHealth=1000;
static char players[MAX_BOTS][32];static char *controlled[MAX_BOTS];static int seats[MAX_BOTS];
static BMConfig g_enhanced={.bailHealth=0.15f};static VehTrack track={1000};
typedef struct {int phase;}NavGrid;static NavGrid grid={2},*gridPtr=&grid;
static NavGrid **nav_slot(void){return &gridPtr;}
static unsigned char driveMask[16];static const unsigned char *drive_mask(void){return driveMask;}
static int nav_drive_near(const NavGrid*g,const unsigned char*m,float x,float y,float z,float r,float*out){(void)g;(void)m;(void)r;out[0]=x;out[1]=y;out[2]=z;return 1;}
static BotBrain *bot_brain(int i){return &brains[i];}
static BotNav *bot_nav(int i){return &navs[i];}
static int player_team(char *pl){(void)pl;return team;}
static int flag_owner(int f){return owners[f];}
static float *flag_pos(int f){return flags[f];}
static int mode_is_cq(void){return 1;}
static int director_target(int i,int *role){(void)i;*role=0;return -1;}
static int objective_count(void){return g_flagCount;}
static const float *objective_pos(int f){return flag_pos(f);}
static int objective_wanted(int t,int f){return flag_owner(f)!=t;}
static float dist2(const float *a,const float *b){float x=a[0]-b[0],z=a[2]-b[2];return x*x+z*z;}
static int flag_pressure(int team,int f,int except){(void)team;(void)f;(void)except;return 0;}
static void squad_centre(int team,int squad,float *out){memcpy(out,centres[squad],12);if(team==2)out[0]=2500;}
static char *player_vehicle(char *pl){for(int i=0;i<H->botCount;i++)if(H->bots[i]==pl)return controlled[i];return NULL;}
static int vehicle_seat_of(char *v,char *pl){(void)v;for(int i=0;i<H->botCount;i++)if(H->bots[i]==pl)return seats[i];return -1;}
static int vehicle_is_boat(char *v){(void)v;return boat;}
static const char *vehicleName="M1Abrams";static const char *vehicle_name(char *v){(void)v;return vehicleName;}
static int vehicle_seats(char*v,char***entries){(void)v;*entries=NULL;return 4;}
static int free_seat_count(char*v){int free=4;for(int j=0;j<H->botCount;j++)free-=controlled[j]==v;return free;}
static VehTrack *vehicle_sample(char *v){(void)v;return &track;}
static void bot_exit_vehicle(int i,int chute){(void)chute;controlled[i]=NULL;exits++;}
static void gunner_reset(int i){(void)i;}
static void choose_local_objective(int i,const char *why){(void)i;(void)why;}
static int drive_strike(unsigned char*m,float x,float z,float r){(void)m;(void)x;(void)z;(void)r;return 1;}
static void diagnostic_note(int i,const char*w,char*v){(void)i;(void)w;(void)v;}
static void nav_cancel_path(int owner){(void)owner;cancels++;}
static void drive_coast(int i,DriveState*d){(void)d;memset(H->botMove[i],0,sizeof H->botMove[i]);}
static void vehicle_mark_bad(char *v,int seconds){(void)v;(void)seconds;bad++;}
static void drive_stop(int i,const char *why,int seconds){(void)why;(void)seconds;g_drive[i].phase=0;controlled[i]=NULL;stops++;}
static void bot_log(const char *fmt,...){(void)fmt;}
static float armor_stop_short(int i){(void)i;return DRIVE_STOP_SHORT;}
static int armor_arrive(int i,const float *position){(void)i;(void)position;return 0;}
'''
MAIN=r'''
int main(void){
 H->frame=100;H->botCount=4;for(int i=0;i<4;i++){H->bots[i]=players[i];controlled[i]=(char*)&vehicleHealth;seats[i]=i;brains[i].targetFlag=-1;}
 for(int f=0;f<3;f++){flags[f][0]=f*500;owners[f]=2;}
 brains[3].task=1; // a human-directed follower is retained as cargo
 DriveState *d=&g_drive[0];d->veh=(char*)&vehicleHealth;d->flag=0;d->phase=3;d->n=d->next=5;
 g_driving[0]=g_driving[1]=g_driving[2]=1;g_drive[1].veh=g_drive[2].veh=d->veh;g_drive[1].phase=g_drive[2].phase=7;
 float pos[3]={0};
 assert(drive_block_for_job(0,NULL,5,6,3,0)==1&&d->avoidRadius==2.25f&&d->avoidUntil==H->frame+900);
 assert(drive_recover_job(0,d,pos)&&controlled[0]&&!exits&&!stops);
 d->phase=3;d->flag=0;d->n=d->next=5;d->routeFailures=0;
 // The actual no-route state keeps both teams seated and retries without Get Out.
 for(team=1;team<=2;team++){
  d->phase=2;H->botMove[0][0]=1;drive_wait_route(0,d,"fixture blocked parking");
  assert(d->phase==10 && controlled[0] && controlled[1] && !stops && !exits && !H->botMove[0][0]);
  actual_route_wait(0);assert(d->phase==10);H->frame=d->routeRetryAt;
  actual_route_wait(0);assert(d->phase==2 && controlled[0] && controlled[1] && !stops && !exits);
 }
 team=1;d->phase=3;d->n=d->next=5;actual_arrival(0,pos);
 assert(d->phase==4 && controlled[0] && controlled[1] && !controlled[2] && controlled[3]);
 assert(exits==1 && !stops && !bad && !g_drive[2].phase && !g_driving[2]);
 drive_hold_objective(0,d,pos);assert(d->phase==4 && !stops);
 // Capturing the flag sends the same driver/vehicle toward another flag.
 owners[0]=1;g_drive[3].phase=3;g_drive[3].flag=1;
 drive_hold_objective(0,d,pos);assert(d->phase==2 && d->flag==2 && controlled[0] && controlled[1]);
 assert(brains[0].targetFlag==2 && !stops && exits==1);
 // A blocked destination causes a different job, not a global vehicle ban.
 assert(drive_other_objective(0,d,pos,1));assert(d->flag==1 && brains[0].avoidFlag==2 && !bad && !stops);
 // A long journey is replanned in the vehicle rather than dismounting at 180s.
 H->frame=181*30;d->at=1;d->phase=3;actual_timeout(0);
 assert(d->phase==2 && d->at==H->frame && controlled[0] && !stops);
 // A truncated route is continued; it is never treated as final arrival.
 d->phase=3;d->partial=1;d->next=d->n=128;d->goal[0]=1000;
 actual_continuation(0,pos);assert(d->phase==2 && !stops && controlled[0]);
 // All objectives owned: patrol to a different owned flag after eight seconds. Near-wrecked armor may bail.
 for(int f=0;f<3;f++){owners[f]=1;}d->phase=4;d->jobAt=H->frame-1000;d->flag=0;
 drive_hold_objective(0,d,pos);assert(d->phase==2 && d->flag!=0 && !stops && controlled[0]);
 owners[d->flag]=2;d->phase=4;d->jobAt=H->frame;drive_hold_objective(0,d,pos);assert(d->phase==4);
 vehicleHealth=5;drive_hold_objective(0,d,pos);assert(stops==1 && !controlled[0]);
 // Small unarmed transports dismount; jeep crew and armor may support.
 const char *transport[]={"QuadBike","KLR650","Dirtbike"};
 for(int k=0;k<3;k++){vehicleName=transport[k];d->phase=3;controlled[0]=d->veh;int old=stops;drive_arrive_objective(0,d,pos);assert(stops==old+1 && !controlled[0]);}
 vehicleName="HMMWV";controlled[0]=d->veh;controlled[1]=d->veh;d->phase=3;
 drive_arrive_objective(0,d,pos);assert(d->phase==4 && controlled[0] && controlled[1]);
 for(int j=1;j<H->botCount;j++){controlled[j]=NULL;}d->phase=3;int old=stops;
 drive_arrive_objective(0,d,pos);assert(stops==old+1 && !controlled[0]);
 vehicleName="M1Abrams";
 puts("PASS: motorcycles/ATVs and solo jeep dismount at objective; crewed jeep and armor retain support duty");
 // Boats retain the original beach dismount behavior.
 vehicleHealth=1000;stops=bad=0;boat=1;d->phase=3;d->next=d->n=2;d->partial=0;
 actual_arrival(0,pos);assert(stops==1 && bad==1);
 // Three squads must cover both ends of a six-flag mountain map. Their
 // uncaptured destinations survive refresh, then advance when captured.
 g_flagCount=6;unsigned mask=(1u<<1)|(1u<<2)|(1u<<3);
 for(int f=0;f<6;f++){flags[f][0]=f*500;owners[f]=0;}
 memset(g_squadTarget,0,sizeof g_squadTarget);allocate_squads(1,mask);
 int count[6]={0},saved[3];for(int q=1;q<=3;q++){saved[q-1]=g_squadTarget[1][q];count[saved[q-1]-1]++;}
 assert(count[0]==1 && count[5]==1);for(int f=0;f<6;f++)assert(count[f]<=1);
 for(int n=0;n<20;n++){allocate_squads(1,mask);for(int q=1;q<=3;q++)assert(g_squadTarget[1][q]==saved[q-1]);}
 owners[0]=1;allocate_squads(1,mask);for(int q=1;q<=3;q++)assert(g_squadTarget[1][q]!=1);
 owners[0]=0;allocate_squads(2,mask);memset(count,0,sizeof count);for(int q=1;q<=3;q++)count[g_squadTarget[2][q]-1]++;
 assert(count[0]==1 && count[5]==1);
 puts("PASS: actual native driver arrival retains driver/gunner/human-directed cargo; capture retarget, blocked-goal retry, long-trip continuation, health and boat exceptions");
 puts("PASS: actual squad allocator covers both ends with three squads/six flags for BOTH teams; stable journeys and captured-flag advance");return 0;
}
'''

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--sanitize',action='store_true');args=parser.parse_args()
    arrival=block('if (d->next >= d->n ||')
    continuation=block('if(d->next>=d->n && d->partial')
    timeout=block('if (H->frame - d->at > 180 * 30)')
    body=PRE+'\n'+(SRC/'ground_jobs.inc').read_text()+'\n'+function(core,'allocate_squads')
    body+='\nstatic int boat_ashore(int i,DriveState*d,const float*vp,const char*w){(void)w;vehicle_mark_bad(d->veh,180);if(armor_arrive(i,vp))return 1;drive_stop(i,"arrived - gets out",20);return 1;}\n'
    body+='\nstatic void actual_arrival(int i,const float*vp){DriveState*d=&g_drive[i];'+arrival+'}\n'
    body+='\nstatic void actual_continuation(int i,const float*vp){DriveState*d=&g_drive[i];'+continuation+'}\n'
    body+='\nstatic void actual_route_wait(int i){DriveState*d=&g_drive[i];'+block('if(d->phase==10)')+'}\n'
    body+='\nstatic void actual_timeout(int i){DriveState*d=&g_drive[i];'+timeout+'}\n'+MAIN
    with tempfile.TemporaryDirectory(prefix='bf3-ground-jobs-') as tmp:
        path=Path(tmp)/'checks.c';path.write_text(body);exe=path.with_suffix('')
        options=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'] if args.sanitize else ['-O2']
        subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror',*options,'-I',str(SRC),str(path),str(SRC/'enhanced.c'),str(SRC/'air.c'),'-lm','-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)

if __name__=='__main__':main()
