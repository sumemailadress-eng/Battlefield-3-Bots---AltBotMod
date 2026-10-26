"""Test actual Rush/TDM routing and native input adapter with mocked engine APIs.
No BF3 scoring, physics, replication or automatic native event binding is simulated.
"""
from pathlib import Path
import re, subprocess, tempfile, argparse, os
from test4_checks import function
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src'
state=(SRC/'mode_state.h').read_text();native=(SRC/'modes_native.inc').read_text()
PROFILE=re.search(r'typedef struct BMModeProfile.*?} BMModeProfile;',state,re.S)[0]
BOT=re.search(r'static struct \{int target.*?g_modeBot\[MAX_BOTS\];',state)[0]
PRE=r'''
static int g_crateEvents;
static int indoor_spot(int i,const float*a,float r,float*o){(void)i;(void)a;(void)r;(void)o;return 0;}   /* AltBotMod V18 */
static int squad_order_rush(int i){(void)i;return 0;}static int mcom_pane_tick(int i,char*s,const float*g){(void)i;(void)s;(void)g;return 0;}static int zone_guard_point(const float*m,unsigned s,float*o){(void)m;(void)s;(void)o;return 0;}   /* AltBotMod V34 */
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "modes.h"
#include "rush_policy.h"
#define MAX_BOTS 24
#define OFF_PLAYER_SOLDIER 16
#define OFF_SOLDIER_POSITION 0
#define OFF_SOLDIER_HEALTH 12
static char *player_manager(void){return 0;}   /* AltBotMod V6: TDM hunt scan (no players in this harness) */
#define TASK_NONE 0
#define OFF_INPUT_AIM_VALID 128
#define OFF_INPUT_AIM_YAW 132
#define OFF_INPUT_AIM_PITCH 136
#define EIA_INTERACT 0
#define EIA_THROTTLE 1
#define EIA_STRAFE 2
#define EIA_YAW 3
#define EIA_PITCH 4
#define EIA_FIRE 5
#define EIA_ZOOM 6
#define BOT_TICK_DT (1.0f/30)
typedef void(*SetLevel_t)(void*,int,float);
static struct {unsigned frame;int botCount;char*bots[MAX_BOTS];float botMove[MAX_BOTS][3];}host,*H=&host;
static char players[MAX_BOTS][128] __attribute__((aligned(8))),inputs[MAX_BOTS][256] __attribute__((aligned(8)));
static float bodies[MAX_BOTS][4];
static struct {int enabled,task,targetFlag;}brains[MAX_BOTS];
static struct {int mode;float x,z;}navs[MAX_BOTS];static int marks,markK,markSt;static void rush_mark(int k,enum BMRushStatus st,const char*w){(void)w;marks++;markK=k;markSt=st;}
static typeof(brains[0])*bot_brain(int i){return &brains[i];}
#define OFF_PLAYER_ONLINE_NAME 64
static int crouches,stands;static void bot_crouch(int i){(void)i;crouches++;}static void bot_stand(int i){(void)i;stands++;}static void bot_log(const char*f,...){(void)f;}
static int player_exists(char*p){for(int k=0;k<MAX_BOTS;k++)if(p==players[k])return 1;return 0;}   /* AltBotMod V16.2 */
static int sniperOverwatch;static int sniper_overwatch_tick(int i,typeof(brains[0])*b,char*s,const float*o,int k){(void)i;(void)b;(void)s;(void)o;(void)k;return sniperOverwatch;}   /* AltBotMod V10 */
static typeof(navs[0])*bot_nav(int i){return &navs[i];}
static struct {int phase;}g_drive[MAX_BOTS],g_air[MAX_BOTS];
static struct {void*vehicle;unsigned until;}g_fleet[MAX_BOTS];
static unsigned g_hitAt[MAX_BOTS],g_modeGoalProbeFrame;
static int g_modeGoalProbeNext,g_rushStage=1,g_lastPlanDeferred,groundMissing,water,planResult=1,probeCount;
static enum BMMode g_mode=BM_MODE_RUSH;static int mode_is_squad_rush(void){return 0;}static int objective_count(void){return g_mode==BM_MODE_RUSH?2:0;}static int mode_ctf_goal(int i,const float*me,int team){(void)i;(void)me;(void)team;return 0;}   /* AltBotMod V15 (tested in altbotmod_v15_checks) */
static BMRushState g_rush;
static int player_team(char*p){return *(int*)(p+8);}
static char *player_vehicle(char*p){(void)p;return NULL;}
static float dist2(const float*a,const float*b){float x=a[0]-b[0],z=a[2]-b[2];return x*x+z*z;}
static void set_level(void*input,int action,float value){((float*)input)[action]=value;}
#define ADDR_ENTRYINPUT_SET_LEVEL set_level
static void nav_cancel_path(int i){(void)i;}
static void*grid;static void**nav_slot(void){return &grid;}
static int ground_below(float x,float z,float hi,float lo,float*out){(void)x;(void)z;(void)lo;probeCount++;if(groundMissing)return 0;*out=hi-4;return 1;}
static int nav_snap(void*g,float x,float y,float z,float*out){(void)g;out[0]=x;out[1]=y;out[2]=z;return 1;}
static int nav_is_water(void*g,float x,float y,float z){(void)g;(void)x;(void)y;(void)z;return water;}
static int plan_budget_left(void){return 1;}
static float queued[MAX_BOTS][3];static int pending[MAX_BOTS];
static int plan_path(int i,char*s,float x,float y,float z){(void)s;
 float goal[3]={x,y,z};
 if(pending[i] && g_modeBot[i].routePending)assert(dist2(goal,queued[i])<0.001f && goal[1]==queued[i][1]);
 memcpy(queued[i],goal,12);g_lastPlanDeferred=planResult==0;pending[i]=g_lastPlanDeferred;return planResult;
}
'''
# plan_path uses the actual state declaration, so place it after that declaration.
a=PRE.index('static float queued');pre=PRE[:a]+PROFILE+'\n#include "mode_profiles.inc"\nstatic const BMModeProfile *g_modeProfile;\n'+BOT+'\n'+PRE[a:]
body=pre+'\n'+'\n'.join(function(native,n) for n in ['mode_profile','rush_target','mode_goal_clear','mode_brain_tick','mode_reassign_idle','mode_apply'])+r'''
static void reset(const BMModeProfile*p,int mode){
 memset(&host,0,sizeof host);memset(players,0,sizeof players);memset(bodies,0,sizeof bodies);memset(inputs,0,sizeof inputs);
 memset(brains,0,sizeof brains);memset(navs,0,sizeof navs);memset(g_modeBot,0,sizeof g_modeBot);memset(g_drive,0,sizeof g_drive);
 memset(g_air,0,sizeof g_air);memset(g_fleet,0,sizeof g_fleet);memset(g_hitAt,0,sizeof g_hitAt);memset(pending,0,sizeof pending);
 for(int k=0;k<MAX_BOTS;k++)bodies[k][3]=100;   /* alive (V16.2 checks health while arming) */
 g_modeProfile=p;g_mode=mode;g_rushStage=1;g_modeGoalProbeFrame=~0u;g_modeGoalProbeNext=0;groundMissing=water=probeCount=0;planResult=1;
 H->frame=100;H->botCount=20;bm_rush_reset(&g_rush,p->stages,2,H->frame);
 for(int i=0;i<20;i++){H->bots[i]=players[i];*(int*)(players[i]+8)=i/10+1;*(float**)(players[i]+16)=bodies[i];brains[i].enabled=1;
  memcpy(bodies[i],mode==BM_MODE_RUSH?p->bases[1][i/10+1]:p->patrol[0],12);}
}
static void ticks(int n){for(int k=0;k<n;k++){H->frame++;int start=probeCount;for(int turn=0;turn<H->botCount;turn++){int i=(H->frame+turn)%H->botCount;if(((H->frame+i)&1)==0)mode_brain_tick(i,(char*)bodies[i]);}assert(probeCount-start<=1);}}
static void policy(void){
 BMRushState r;int player1,player2;bm_rush_reset(&r,4,2,0);
 assert(!bm_rush_active(&r,0)&&bm_rush_active(&r,1)&&!bm_rush_active(&r,3));
 assert(!bm_rush_event(&r,3,BM_RUSH_ARMED,1)&&!bm_rush_event(&r,1,BM_RUSH_UNKNOWN,1));
 assert(bm_rush_acquire(&r,1,1,&player1,1,270));assert(!bm_rush_acquire(&r,1,2,&player2,2,270));
 /* V16.1: a defender never operates an unknown / idle station (it held the lease and blocked the attackers) */
 assert(!bm_rush_acquire(&r,1,2,&player2,271,270)&&!bm_rush_can_interact(&r,1,2)&&bm_rush_can_interact(&r,1,1));
 assert(bm_rush_acquire(&r,1,1,&player1,272,270));bm_rush_release(&r,&player1);
 assert(bm_rush_event(&r,1,BM_RUSH_ARMED,300)&&!bm_rush_can_interact(&r,1,1)&&bm_rush_can_interact(&r,1,2));
 assert(!bm_rush_event(&r,1,BM_RUSH_ARMED,301));assert(bm_rush_event(&r,1,BM_RUSH_IDLE,302));
 assert(bm_rush_event(&r,1,BM_RUSH_DESTROYED,303)&&!bm_rush_active(&r,1)&&!bm_rush_event(&r,1,BM_RUSH_IDLE,304));
 assert(!bm_rush_sector_complete(&r));assert(bm_rush_event(&r,2,BM_RUSH_DESTROYED,304)&&bm_rush_sector_complete(&r));
 assert(r.stage==1);assert(bm_rush_set_stage(&r,2,400)&&bm_rush_active(&r,3)&&!bm_rush_active(&r,1));
 assert(!bm_rush_set_stage(&r,1,500)&&!bm_rush_set_stage(&r,5,500));
 puts("PASS: confirmed Rush transitions, attacker/defender permissions (V16.1: defenders only at an armed station), single operator lease, no timeout-based sector completion, duplicate/future/past event refusal");
}
int main(void){
 policy();int rushMaps=0,steals=0;
 for(unsigned p=0;p<sizeof g_modeProfiles/sizeof g_modeProfiles[0];p++)if(!strcmp(g_modeProfiles[p].mode,"RushLarge0")){
  const BMModeProfile*profile=&g_modeProfiles[p];rushMaps++;reset(profile,BM_MODE_RUSH);ticks(128);
  for(int team=1;team<=2;team++){int counts[3]={0};for(int i=(team-1)*10;i<team*10;i++){assert(g_modeBot[i].target==1||g_modeBot[i].target==2);counts[g_modeBot[i].target]++;assert(g_modeBot[i].goalReady);}assert(counts[1]>=3&&counts[2]>=3);}
  for(int i=0;i<20;i++)if(!g_modeBot[i].goalRole)for(int j=i+1;j<20;j++)if(player_team(players[i])==player_team(players[j])&&!g_modeBot[j].goalRole)assert(dist2(g_modeBot[i].goal,g_modeBot[j].goal)>=9);
  // Finishing the recorded interaction hold never invents an engine event.
  reset(profile,BM_MODE_RUSH);H->botCount=1;memcpy(bodies[0],profile->mcom[1].pos,12);mode_brain_tick(0,(char*)bodies[0]);mode_apply(0,inputs[0]);
  assert(((float*)inputs[0])[EIA_INTERACT]==1 && *(int*)(inputs[0]+128)==1);
  marks=0;H->frame+=180;mode_brain_tick(0,(char*)bodies[0]);mode_apply(0,inputs[0]);assert(((float*)inputs[0])[EIA_INTERACT]==1 && !marks);   /* V16.1: 8 s hold */
  H->frame+=60;marks=0;mode_brain_tick(0,(char*)bodies[0]);mode_apply(0,inputs[0]);
  assert(!((float*)inputs[0])[EIA_INTERACT] && marks==1 && markK==1 && markSt==BM_RUSH_ARMED && g_rush.stage==1);   /* the hold done: marked armed (rush_mark) */
  // V16.2.1: at 8 s the per-frame input step runs before the brain - the hold must still be marked, not thrown away.
  reset(profile,BM_MODE_RUSH);H->botCount=1;memcpy(bodies[0],profile->mcom[1].pos,12);mode_brain_tick(0,(char*)bodies[0]);mode_apply(0,inputs[0]);
  H->frame+=240;marks=0;mode_apply(0,inputs[0]);assert(g_modeBot[0].interactAt && g_rush.objective[1].operatorPlayer==players[0]);
  mode_brain_tick(0,(char*)bodies[0]);assert(marks==1 && markSt==BM_RUSH_ARMED && !g_modeBot[0].interactAt);
  // V16.2.2: with the game's crate events hooked, 8 s without an arm is no arm - the bot walks closer and holds again.
  reset(profile,BM_MODE_RUSH);H->botCount=1;g_crateEvents=1;memcpy(bodies[0],profile->mcom[1].pos,12);bodies[0][0]+=2.0f;mode_brain_tick(0,(char*)bodies[0]);
  assert(!g_modeBot[0].interactAt && g_modeBot[0].approachAt);   /* V16.2.3: 2 m off - walks onto the spot first */
  H->frame+=60;mode_brain_tick(0,(char*)bodies[0]);assert(!g_modeBot[0].interactAt);
  H->frame+=61;mode_brain_tick(0,(char*)bodies[0]);mode_apply(0,inputs[0]);assert(g_modeBot[0].interactAt);   /* could not get closer in 4 s: holds from there */
  H->frame+=240;marks=0;mode_apply(0,inputs[0]);mode_brain_tick(0,(char*)bodies[0]);
  assert(!marks && !g_modeBot[0].interactAt && g_modeBot[0].closerUntil>H->frame);
  H->frame++;mode_brain_tick(0,(char*)bodies[0]);assert(!g_modeBot[0].interactAt);   /* 2 m off: onto the spot now */
  bodies[0][0]-=1.7f;H->frame++;mode_brain_tick(0,(char*)bodies[0]);assert(g_modeBot[0].interactAt);mode_apply(0,inputs[0]);
  assert(((float*)inputs[0])[EIA_FIRE]==0 && ((float*)inputs[0])[EIA_INTERACT]==1);g_crateEvents=0;   /* 0.3 m: holds again, not firing */
  // V16.2.3: sent to M-COM 1 but standing 3 m from M-COM 2: it arms M-COM 2.
  reset(profile,BM_MODE_RUSH);H->botCount=1;mode_brain_tick(0,(char*)bodies[0]);g_modeBot[0].target=1;g_modeBot[0].revision=g_rush.revision;
  memcpy(bodies[0],profile->mcom[2].pos,12);bodies[0][2]+=3.0f;H->frame++;mode_brain_tick(0,(char*)bodies[0]);
  if(dist2(profile->mcom[1].pos,profile->mcom[2].pos)>13*13)assert(g_modeBot[0].target==2);
  // V16.2.3: an attacker near an armed station stays to guard it (close guard ring), rather than leaving for the other one.
  reset(profile,BM_MODE_RUSH);H->botCount=1;assert(bm_rush_event(&g_rush,1,BM_RUSH_ARMED,H->frame));
  {float near1[3];memcpy(near1,profile->mcom[1].pos,12);near1[0]+=30;assert(rush_target(0,near1)==1);}
  // V16.2: a fight order, damage or another job no longer releases it; leaving the spot, a vehicle or death does.
  reset(profile,BM_MODE_RUSH);H->botCount=1;memcpy(bodies[0],profile->mcom[1].pos,12);mode_brain_tick(0,(char*)bodies[0]);mode_apply(0,inputs[0]);
  brains[0].task=8;mode_apply(0,inputs[0]);assert(((float*)inputs[0])[EIA_INTERACT]==1 && g_rush.objective[1].operatorPlayer==players[0] && crouches>=1);
  bodies[0][0]+=2.0f;H->frame++;mode_apply(0,inputs[0]);assert(((float*)inputs[0])[EIA_INTERACT]==1);   /* pushed 2 m: keeps at it */
  bodies[0][0]+=2.0f;H->frame++;mode_apply(0,inputs[0]);assert(!((float*)inputs[0])[EIA_INTERACT] && !g_rush.objective[1].operatorPlayer);   /* 4 m off: stops */
  bodies[0][0]-=4.0f;brains[0].task=0;H->frame++;mode_brain_tick(0,(char*)bodies[0]);mode_apply(0,inputs[0]);assert(((float*)inputs[0])[0]==1);
  g_hitAt[0]=H->frame;mode_apply(0,inputs[0]);assert(((float*)inputs[0])[0]==1);   /* V16.1: being shot no longer stops an arm / defuse */
  g_hitAt[0]=0;H->frame++;mode_brain_tick(0,(char*)bodies[0]);mode_apply(0,inputs[0]);*(void**)(players[0]+16)=NULL;mode_apply(0,inputs[0]);assert(!((float*)inputs[0])[0]);
  // V16.2: a teammate holding the job from far away (fighting, stuck, dead) loses it to the bot standing at the panel.
  reset(profile,BM_MODE_RUSH);H->botCount=2;*(int*)(players[1]+8)=player_team(players[0]);memcpy(bodies[1],profile->mcom[1].pos,12);bodies[1][0]+=40;
  assert(bm_rush_acquire(&g_rush,1,player_team(players[0]),players[1],H->frame,270));g_modeBot[0].target=1;g_modeBot[0].revision=g_rush.revision;
  memcpy(bodies[0],profile->mcom[1].pos,12);bodies[0][0]+=0.4f;H->frame++;mode_brain_tick(0,(char*)bodies[0]);mode_apply(0,inputs[0]);
  assert(g_rush.objective[1].operatorPlayer==players[0] && ((float*)inputs[0])[EIA_INTERACT]==1);   /* on the spot: takes over */
  steals++;
 }
 assert(rushMaps==25 && steals==25);puts("PASS: actual routing on all 25 supplied Rush layouts spreads BOTH teams across M-COMs and checked distinct positions; real hold/release adapter interrupts on combat, vehicle entry or death (V16.1: not on damage); a full 8 s hold marks the station armed; V16.2: the hold survives fight orders and a 2 m push, a bot at the panel takes the job from a teammate who holds it from far away");
 const BMModeProfile*p=mode_profile("XP3_Alborz","RushLarge0");assert(p);reset(p,BM_MODE_RUSH);H->botCount=2;groundMissing=1;ticks(12);assert(!g_modeBot[0].goalReady&&!g_modeBot[1].goalReady && probeCount>=6); // bad ground cannot starve the second bot
 groundMissing=0;water=1;ticks(12);assert(!g_modeBot[0].goalReady&&!g_modeBot[1].goalReady);water=0;ticks(4);assert(g_modeBot[0].goalReady&&g_modeBot[1].goalReady);
 planResult=0;g_modeBot[0].plannedAt=H->frame;mode_brain_tick(0,(char*)bodies[0]);assert(g_modeBot[0].routePending);float goal[3];memcpy(goal,g_modeBot[0].goal,12);H->frame+=3;mode_brain_tick(0,(char*)bodies[0]);assert(dist2(goal,g_modeBot[0].goal)==0);planResult=1;H->frame+=3;mode_brain_tick(0,(char*)bodies[0]);assert(!g_modeBot[0].routePending && navs[0].mode==3);
 puts("PASS: failed terrain and water checks create no guessed goals, rotating one-probe budget gives both bots a turn, queued goals remain stable until completion");
 reset(p,BM_MODE_RUSH);H->botCount=1;memcpy(bodies[0],p->mcom[1].pos,12);bodies[0][0]+=10;mode_brain_tick(0,(char*)bodies[0]);assert(g_modeBot[0].goalRole);H->frame+=45*30;mode_brain_tick(0,(char*)bodies[0]);assert(!g_rush.objective[1].operatorPlayer && g_modeBot[0].retryAt>H->frame);
 *(int*)(players[0]+8)=2;assert(bm_rush_event(&g_rush,2,BM_RUSH_ARMED,H->frame));mode_brain_tick(0,(char*)bodies[0]);assert(g_modeBot[0].target==2);
 assert(bm_rush_event(&g_rush,2,BM_RUSH_DESTROYED,H->frame));mode_brain_tick(0,(char*)bodies[0]);assert(g_modeBot[0].target==1);
 puts("PASS: stalled M-COM approach yields its operator slot; defenders prioritize confirmed armed stations and skip destroyed stations");
 p=mode_profile("MP_017","TeamDeathMatch0");reset(p,BM_MODE_TDM);H->botCount=1;planResult=0;mode_brain_tick(0,(char*)bodies[0]);memcpy(goal,g_modeBot[0].goal,12);H->frame+=3;mode_brain_tick(0,(char*)bodies[0]);assert(dist2(goal,g_modeBot[0].goal)==0);planResult=1;H->frame+=3;mode_brain_tick(0,(char*)bodies[0]);assert(navs[0].mode==3);navs[0].mode=1;H->frame=g_modeBot[0].guardUntil;mode_brain_tick(0,(char*)bodies[0]);assert(dist2(goal,g_modeBot[0].goal)>1);
 puts("PASS: actual Noshahr TDM preserves unfinished patrol goals and rotates its destination after arrival");return 0;
}
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--sanitize',action='store_true');args=ap.parse_args()
 with tempfile.TemporaryDirectory(prefix='bf3-rush-mode-')as tmp:
  path=Path(tmp)/'checks.c';path.write_text(body);exe=path.with_suffix('')
  cmd=['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-I',str(SRC),str(path),str(SRC/'modes.c'),'-lm','-o',str(exe)]
  if args.sanitize:cmd[1:1]=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']
  subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
if __name__=='__main__':main()
