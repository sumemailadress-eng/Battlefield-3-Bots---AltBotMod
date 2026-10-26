"""Actual bounded armor and traffic adapter with mocked engine observations.
Repair tools, health and hostile crew are observed through primitive fixtures;
no BF3 collision, repair rate or road physics is represented.
"""
from pathlib import Path
import argparse,re,subprocess,tempfile
from driver_departure_checks import PRE
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src'
pre=PRE.replace('static void nav_cancel_path(int i){(void)i;}','')
for name in ['armor_tick','armor_arrive','armor_route_failed','tactic_segment']:
 pre=re.sub(r'^static [^\n]*\b'+name+r'\([^\n]*\n','',pre,flags=re.M)
armor=(SRC/'tactics_state.h').read_text().split('typedef struct VehicleDiagnostic')[0].replace('#pragma once','')
pre=pre.replace('static struct{int job;}g_armor[MAX_BOTS];\n#define ARMOR_NORMAL 0',armor)
pre=pre.replace('typedef struct {int enabled,task,targetFlag;}BotBrain;', 'typedef struct {int enabled,task,targetFlag,stepAt;unsigned taskUntil;char *serviceTarget;float postPos[3],postFacing;}BotBrain;')
pre=pre.replace('typedef struct {float maxSeen;}VehTrack;', 'typedef struct {float maxSeen,speed;}VehTrack;')
pre=pre.replace('static float soldiers[MAX_BOTS][3],vehicleHealth,xf[16],lastTarget[3];','static float soldiers[MAX_BOTS][3],xf[16],lastTarget[3],peerXf[16];\nstatic union {float health;char bytes[64];}storage;\n#define vehicleHealth storage.health\nstatic char peer[64];static const char *vehicleName="M1Abrams";')
pre=pre.replace('return v==(char*)&vehicleHealth && vehicleHealth>0;', 'return v && ((v==(char*)&vehicleHealth && vehicleHealth>0) || v==peer);')
pre=pre.replace('return "Growler";', 'return vehicleName;')
pre=pre.replace('(void)v;(void)team;return xf+12;', '(void)team;return v==peer?peerXf+12:xf+12;')
pre=pre.replace('(void)v;return xf;', 'return v==peer?peerXf:xf;')
pre=pre.replace('static VehTrack track={1000};','static VehTrack track={1000,0};')
for name in ['traffic_speed','traffic_controls']:
 pre=re.sub(r'^static [^\n]*\b'+name+r'\([^\n]*\n','',pre,flags=re.M)
pre=pre.replace('static int nav_drive_near(', 'static int narrowRoad;\nstatic int nav_drive_near(')
pre=pre.replace('(void)g;(void)m;(void)r;out[0]=x;', '(void)g;(void)m;if(narrowRoad && r==2)return 0;out[0]=x;')
extra=r'''
#define TASK_REPAIR 1
#define TASK_DEFEND 2
static char *g_gunTargetVeh[MAX_BOTS];static int repairs,assigned,engineerIndex=1,navCancels;
static int vehicle_occupied(char*v){return v==peer || controlled[0]==v;}
static char *vehicle_occupant(char*v){return v==peer?players[3]:players[0];}
static int vehicle_list(char***out){static char *list[2];list[0]=(char*)&vehicleHealth;list[1]=peer;*out=list;return 2;}
static int bot_index(char*p){for(int i=0;i<H->botCount;i++)if(p==players[i])return i;return -1;}
static int is_bot(char*p){return bot_index(p)>=0;}
static int bot_gadget_slot(int i,const char*a,const char*b){(void)a;(void)b;return i==engineerIndex?2:-1;}
static void start_repair(int i,char*v,int slot){assert(slot==2);brains[i].task=TASK_REPAIR;brains[i].serviceTarget=v;repairs++;}
static void assign_task(int i,int task,char*commander){(void)commander;brains[i].task=task;assigned++;}
static float yaw_towards(float x,float z){return atan2f(x,z);}
static void nav_cancel_path(int owner){(void)owner;navCancels++;}
static int gun_can_see(const float*a,const float*b,int v){(void)a;(void)b;(void)v;return 1;}
static int solid_between(const float*a,const float*b,float*h){(void)a;(void)b;(void)h;return 1;}
'''
MAIN=r'''
static void reset(int team){
 memset(&host,0,sizeof host);memset(g_drive,0,sizeof g_drive);memset(g_armor,0,sizeof g_armor);memset(brains,0,sizeof brains);memset(controlled,0,sizeof controlled);memset(xf,0,sizeof xf);memset(peerXf,0,sizeof peerXf);memset(g_gunTargetVeh,0,sizeof g_gunTargetVeh);memset(g_roadLease,0,sizeof g_roadLease);memset(g_fleet,0,sizeof g_fleet);
 H->frame=1000;H->botCount=4;bm_defaults(&g_enhanced);g_enhanced.armored=1;g_enhanced.armorEscort=0;
 vehicleHealth=1000;xf[0]=xf[5]=xf[10]=xf[15]=peerXf[0]=peerXf[5]=peerXf[10]=peerXf[15]=1;xf[13]=peerXf[13]=77;peerXf[14]=300;
 vehicleName="M1Abrams";repairs=assigned=navCancels=narrowRoad=0;engineerIndex=1;g_tacticProbeFrame=0;g_tacticProbes=0;
 for(int i=0;i<4;i++){H->bots[i]=players[i];*(int*)(players[i]+8)=team;*(float**)(players[i]+16)=soldiers[i];memset(soldiers[i],0,12);soldiers[i][1]=77;brains[i].enabled=1;}
 *(int*)(players[3]+8)=3-team;controlled[0]=(char*)&vehicleHealth;controlled[3]=peer;
 DriveState*d=&g_drive[0];d->phase=3;d->at=H->frame;d->veh=controlled[0];d->goal[1]=77;d->goal[2]=1000;g_gunTargetVeh[0]=peer;
}
int main(void){
 for(int team=1;team<=2;team++){
  reset(team);vehicleHealth=250;assert(armor_tick(0,xf+12));assert(g_armor[0].job==ARMOR_RETREAT && g_drive[0].phase==2 && controlled[0] && !exits);
  assert(g_armor[0].original[2]==1000 && g_drive[0].goal[2]<0 && navCancels==1);
  memcpy(xf+12,g_armor[0].destination,12);assert(armor_arrive(0,xf+12));assert(g_armor[0].job==ARMOR_REPAIR && g_drive[0].phase==4);
  H->frame++;assert(armor_tick(0,xf+12));assert(repairs==1 && brains[1].task==TASK_REPAIR && brains[1].serviceTarget==controlled[0]);
  vehicleHealth=700;H->frame++;assert(armor_tick(0,xf+12));assert(g_armor[0].job==ARMOR_NORMAL && g_drive[0].phase==2 && g_drive[0].goal[2]==1000 && controlled[0]);
  reset(team);assert(!armor_tick(0,xf+12)&&g_armor[0].job==ARMOR_NORMAL);g_drive[0].goal[2]=60;peerXf[14]=120;H->frame+=10*30;assert(armor_tick(0,xf+12));assert(g_armor[0].job==ARMOR_COVER && g_drive[0].phase==2);
  assert(armor_route_failed(0));assert(g_armor[0].job==ARMOR_NORMAL && g_drive[0].goal[2]==60 && controlled[0] && !exits);
  reset(team);vehicleHealth=250;assert(armor_tick(0,xf+12));memcpy(xf+12,g_armor[0].destination,12);armor_arrive(0,xf+12);engineerIndex=-1;
  H->frame+=9*30;assert(armor_tick(0,xf+12));assert(g_armor[0].job==ARMOR_NORMAL && g_drive[0].phase==2 && !exits);
 }
 puts("PASS: actual armor on BOTH teams seeks checked cover, retreats on observed damage, requests a real repair-tool engineer, resumes with crew after repair/no-engineer timeout and restores objective after route failure");
 reset(1);g_enhanced.armorEscort=1;brains[2].task=99;armor_escort(0,xf+12,peerXf+12);
 assert(assigned==1 && brains[1].task==TASK_DEFEND && brains[1].taskUntil==H->frame+12*30 && brains[2].task==99);
 reset(1);assert(tactic_probe_budget() && tactic_probe_budget() && !tactic_probe_budget());H->frame++;assert(tactic_probe_budget());
 puts("PASS: nearby infantry support uses brief actual defend tasks, respects existing orders, and tactical jobs share a two-probe-per-frame budget");
 reset(1);g_drive[1].veh=peer;narrowRoad=1;float point[3]={0,77,20};
 assert(!traffic_lease(0,xf+12,point));assert(traffic_lease(1,xf+12,point));
 for(int second=0;second<8;second++){H->frame+=30;assert(!traffic_lease(0,xf+12,point));}
 H->frame++;assert(!traffic_lease(0,xf+12,point));assert(g_drive[0].trafficLeaseBackoff>H->frame);   /* V9: the 8 s holder gives the lease up, never waits for itself */
 assert(!traffic_lease(1,xf+12,point));
 reset(1);*(int*)(players[3]+8)=1;peerXf[14]=5;
 assert(traffic_speed(0,xf+12,xf,point,16)==0 && g_drive[0].trafficWait && g_drive[0].trafficFollowing);
 H->frame+=10*30+1;assert(traffic_speed(0,xf+12,xf,point,16)==0 && g_drive[0].phase==2 && controlled[0] && !exits && navCancels);
 assert(g_drive[0].avoidUntil>H->frame && g_drive[0].avoidRadius==10.0f && g_drive[0].traceTries==99);   /* V9: routes round the blocker, not the same road again */
 float throttle=1,steer=1;assert(traffic_controls(0,xf+12,xf,&throttle,&steer) && !throttle && !steer);
 assert(g_drive[0].checkAt==H->frame && !g_drive[0].boxedSince);
 puts("PASS: narrow-road leases expire/release priority; actual traffic waits reset stuck clocks and replan after ten seconds while retaining seats");
 /* AltBotMod V10: a friendly vehicle already coming the other way along a narrow stretch holds it - 45 m ahead, not just
    within 18 m of this vehicle's next waypoint (V9 drove in and met it halfway) */
 reset(1);*(int*)(players[3]+8)=1;g_drive[3].veh=peer;g_drive[3].phase=3;narrowRoad=1;
 peerXf[8]=0;peerXf[10]=-1;peerXf[14]=60;float far[3]={0,77,45},mine[3]={0,77,20};
 assert(!traffic_lease(3,peerXf+12,far));H->frame+=5;               /* the peer drives in: its lease */
 assert(traffic_lease(0,xf+12,mine) && g_leaseHolder==peer);         /* this one waits at the entrance */
 narrowRoad=0;assert(!traffic_lease(0,xf+12,mine));narrowRoad=1;    /* a wide road: two can pass */
 peerXf[10]=1;track.speed=5;assert(!traffic_lease(0,xf+12,mine));   /* the same way, moving: a convoy, followed */
 track.speed=0;peerXf[10]=-1;
 for(int k=0;k<16;k++)if(g_roadLease[k].vehicle==controlled[0])g_roadLease[k].until=0;   /* (the convoy pass left it a lease of its own) */
 g_drive[0].narrowSince=0;g_drive[0].trafficAt=0;assert(traffic_speed(0,xf+12,xf,mine,16)==0 && g_drive[0].trafficWait && g_drive[0].trafficPeer==peer);   /* holder recorded */
 track.speed=3;   /* the holder keeps driving through (its lease renewed as it goes) */
 for(int sec=1;sec<=16;sec++){
  H->frame+=30;for(int k=0;k<16;k++)if(g_roadLease[k].vehicle==peer){g_roadLease[k].until=H->frame+4*30;g_roadLease[k].createdAt=H->frame;}
  g_drive[0].trafficAt=0;traffic_speed(0,xf+12,xf,mine,16);
  if(sec==8)assert(g_drive[0].phase==3);                           /* coming through: waited for, no detour after 6 s */
 }
 assert(g_drive[0].phase==2 && g_drive[0].avoidUntil>H->frame);     /* 16 s: it gives up and routes round */
 track.speed=0;
 puts("PASS: V10 narrow stretches - an oncoming vehicle anywhere along the stretch ahead holds the entrance, a wide road does not, a convoy going the same way is followed, the holder is recorded (parked-in-the-way check) and one coming through is waited for 15 s before a detour");
 /* nose to nose inside a narrow stretch: the one that drove in later backs out (straight back, until the road is wide
    again); the first creeps on once there is a gap */
 reset(1);*(int*)(players[3]+8)=1;g_drive[3].veh=peer;g_drive[3].phase=3;narrowRoad=1;
 peerXf[8]=0;peerXf[10]=-1;peerXf[14]=8;float w0[3]={0,77,10},w3[3]={0,77,0};
 g_drive[0].narrowSince=900;g_drive[0].narrowSeen=H->frame;g_drive[3].narrowSince=950;g_drive[3].narrowSeen=H->frame;
 traffic_speed(3,peerXf+12,peerXf,w3,16);
 assert(g_drive[3].trafficWait && g_drive[3].trafficBackUntil>H->frame && g_drive[3].trafficBacked);
 throttle=1;steer=1;g_drive[3].trafficSince=H->frame-1;assert(traffic_controls(3,peerXf+12,peerXf,&throttle,&steer) && throttle<0 && !steer);
 g_drive[0].trafficAt=0;traffic_speed(0,xf+12,xf,w0,16);
 assert(g_drive[0].trafficWait && !g_drive[0].trafficBackUntil);   /* 8 m apart: the first waits for the gap */
 peerXf[14]=12;g_drive[0].trafficAt=0;H->frame++;g_drive[0].narrowSeen=g_drive[3].narrowSeen=H->frame;
 assert(traffic_speed(0,xf+12,xf,w0,16)>0 && !g_drive[0].trafficWait);   /* gap: it creeps on */
 narrowRoad=0;g_drive[3].trafficSince=H->frame;throttle=1;traffic_controls(3,peerXf+12,peerXf,&throttle,&steer);
 assert(!g_drive[3].trafficBackUntil);                              /* out of the stretch: stops backing (pulls aside) */
 g_drive[3].trafficAt=0;H->frame+=3;narrowRoad=1;g_drive[3].narrowSeen=H->frame;peerXf[14]=8;traffic_speed(3,peerXf+12,peerXf,w3,16);
 assert(!g_drive[3].trafficBackUntil);                              /* backs out once per wait, not in a loop */
 puts("PASS: V10 nose to nose in a narrow stretch - the later one backs straight out (rear checked) until the road is wide, once; the first holds until there is a gap, then creeps through");
 return 0;
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');a=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='bf3-armor-traffic-') as tmp:
  f=Path(tmp)/'checks.c';f.write_text(pre+extra+(SRC/'ground_tactics_native.inc').read_text()+MAIN);exe=f.with_suffix('')
  opts=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'] if a.sanitize else ['-O2']
  subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable',*opts,'-I',str(SRC),str(f),str(SRC/'enhanced.c'),str(SRC/'air.c'),'-lm','-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
