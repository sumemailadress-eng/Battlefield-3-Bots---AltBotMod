"""Full fight_tick -> real combat_motion loop, with native terrain/path/pose mocked.
Checks separation, persistent queued goals, bad terrain, close threats and team changes.
"""
from pathlib import Path
import re,tempfile,subprocess,argparse
from test4_checks import function
from combat_mobility_checks import FIGHT
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src';core=(SRC/'core.c').read_text()
prefix=FIGHT[:FIGHT.index('// This fixture isolates weapon timing;')]
prefix=prefix.replace('#define MAX_BOTS 2','#define MAX_BOTS 16')
prefix=re.sub(r'((?:\bg_\w+|\bbots|\bplayers|\bbodies|\bnavs))\[2\]',r'\1[16]',prefix)
prefix=prefix.replace('int task,prevTask,step,waiting;','int task,prevTask,step,waiting,crouched;')
prefix=prefix.replace('unsigned frame;char*bots[16];','unsigned frame;int botCount;char*bots[16];')
prefix=prefix.replace('return p==players[1];','return p>=players[0] && p<=players[15];')
state=re.search(r'typedef struct CombatMoveState \{[\s\S]*?\} CombatMoveState;',core)[0]
body=prefix+'\n'+state+r'''
static CombatMoveState g_combatMove[MAX_BOTS];
static BotBrain brains[MAX_BOTS];static unsigned beginAt[MAX_BOTS];static int moves[MAX_BOTS],queuedPolls[MAX_BOTS];
static float queuedGoal[MAX_BOTS][3],goals[MAX_BOTS][3];static int requests,budget,terrainBad,g_lastPlanDeferred,cancels;
typedef struct{int phase;}NavGrid;static NavGrid grid={2},*gridPtr=&grid;
static NavGrid **nav_slot(void){return &gridPtr;}
static int nav_is_water(NavGrid*g,float x,float y,float z){(void)g;(void)x;(void)y;(void)z;return terrainBad==2;}
static int nav_snap(NavGrid*g,float x,float y,float z,float*out){(void)g;out[0]=x;out[1]=y;out[2]=z;return !terrainBad;}
static int plan_budget_left(void){return budget>0;}
static void nav_cancel_path(int i){queuedPolls[i]=0;cancels++;}
static int plan_path(int i,char*s,float x,float y,float z){
 (void)s;assert(budget>0);budget--;g_lastPlanDeferred=0;requests++;
 if(!queuedPolls[i]){queuedGoal[i][0]=x;queuedGoal[i][1]=y;queuedGoal[i][2]=z;}
 else assert(queuedGoal[i][0]==x && queuedGoal[i][1]==y && queuedGoal[i][2]==z); // goal stable until result
 if(++queuedPolls[i]<3){g_lastPlanDeferred=1;return 0;}
 queuedPolls[i]=0;memcpy(goals[i],queuedGoal[i],12);moves[i]++;beginAt[i]=H->frame;return 2;
}
static void bot_stand(int i){(void)i;}
static const BMWeaponProfile *infantry_weapon(int i);
static const BMDifficulty *bot_profile(int i){(void)i;return NULL;}   // (V8 combat here; V9 profile: altbotmod_v9_checks)
static float fight_tolerance(int i,int recon,float d){(void)i;return g_enhanced.weaponHandling?bm_weapon_tolerance(&g_enhanced,infantry_weapon(i),d):bm_infantry_tolerance(&g_enhanced,recon,d);}
static void fight_posture_start(int i,char*s,float d){(void)i;(void)s;(void)d;}static void fight_posture_end(int i){(void)i;}
static int fight_sidearm_swap(int i,BotBrain*b,char*s,float d,int v){(void)i;(void)b;(void)s;(void)d;(void)v;return 0;}
static int fight_grenade_tick(int i,BotBrain*b,char*s,char*t,int v,float d){(void)i;(void)b;(void)s;(void)t;(void)v;(void)d;return 0;}
static int __attribute__((unused)) sniper_holding(int i){(void)i;return 0;}   /* AltBotMod V10 (sniper overwatch: altbotmod_v10_checks) */
static float hdist(const float*a,const float*b){float dx=a[0]-b[0],dz=a[2]-b[2];return sqrtf(dx*dx+dz*dz);}
'''+(SRC/'combat_motion.inc').read_text()+'\n'+re.search(r'^static int is_recon[^\n]+',core,re.M)[0]+'\n'+'\n'.join(function(core,n) for n in ['infantry_weapon','fight_range','end_fight','fight_tick'])+r'''
static void init(unsigned frame){
 memset(&host,0,sizeof host);H->frame=frame;H->botCount=16;bm_defaults(&g_enhanced);
 memset(g_combatMove,0,sizeof g_combatMove);memset(queuedPolls,0,sizeof queuedPolls);memset(moves,0,sizeof moves);memset(navs,0,sizeof navs);
 memset(g_fightMoveUntil,0,sizeof g_fightMoveUntil);memset(g_fightRetreat,0,sizeof g_fightRetreat);memset(g_fightAdvance,0,sizeof g_fightAdvance);
 blocked=terrainBad=requests=ends=cancels=0;
 for(int i=0;i<16;i++){
  H->bots[i]=players[i];*(int*)(players[i]+8)=i<8?1:2;*(float**)(players[i]+16)=bodies[i];
  memset(bodies[i],0,sizeof bodies[i]);bodies[i][0]=i<8?-60:60;bodies[i][2]=(i%8)*.4f;bodies[i][3]=100;
  strcpy(players[i]+64,"Medic");g_fightTarget[i]=players[i<8?8:0];g_hitAt[i]=0;
  brains[i]=(BotBrain){.task=TASK_FIGHT,.taskUntil=frame+10000,.step=1};
 }
}
int main(void){
 init(1000);int started[16]={0},left=0,right=0;
 for(unsigned frame=1000;frame<1600;frame++){
  H->frame=frame;budget=3;
  for(int i=0;i<16;i++){
   fight_tick(i,&brains[i],(char*)bodies[i]);
   if(moves[i] && !started[i]){started[i]=1;assert(navs[i].mode==3);assert(fabsf(goals[i][2]-bodies[i][2])>4);if(goals[i][2]<bodies[i][2])left++;else right++;}
   if(g_combatMove[i].until && H->frame-beginAt[i]>15){memcpy(bodies[i],goals[i],12);navs[i].mode=1;}
  }
 }
 int us=0,ru=0;for(int i=0;i<16;i++)if(started[i]){if(i<8)us++;else ru++;}
 assert(us>=6 && ru>=6 && left && right);
 printf("PASS: actual fight/movement sequence separates crowded stationary infantry on BOTH teams (US %d/8, RU %d/8); staggered checked left/right routes survive asynchronous polls\n",us,ru);
 init(3000);terrainBad=1;
 for(H->frame=3000;H->frame<3400;H->frame++){budget=3;for(int i=0;i<16;i++)fight_tick(i,&brains[i],(char*)bodies[i]);}
 assert(!requests);for(int i=0;i<16;i++)assert(!moves[i] && navs[i].mode!=3 && navs[i].mode!=2);
 terrainBad=2;for(H->frame=3400;H->frame<3600;H->frame++){budget=3;for(int i=0;i<16;i++)fight_tick(i,&brains[i],(char*)bodies[i]);}
 assert(!requests);puts("PASS: absent ground and water never produce unchecked flanks or guessed movement");
 init(4000);bodies[8][0]=-55;g_combatMove[0].soldier=(char*)bodies[0];g_combatMove[0].until=4200;g_combatMove[0].goal[2]=20;navs[0].mode=3;
 budget=3;fight_tick(0,&brains[0],(char*)bodies[0]);assert(!g_combatMove[0].until && navs[0].mode==1);
 g_combatMove[0].pending=1;*(int*)(players[8]+8)=1;fight_tick(0,&brains[0],(char*)bodies[0]);
 assert(!g_combatMove[0].pending && !g_fightTarget[0] && cancels);
 puts("PASS: immediate close threats interrupt relocation; target team changes cancel queued movement and fighting");return 0;
}
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--sanitize',action='store_true');a=ap.parse_args()
 with tempfile.TemporaryDirectory(prefix='bf3-combat-flow-') as temp:
  f=Path(temp)/'check.c';f.write_text(body);exe=f.with_suffix('')
  cmd=['gcc','-std=c11','-Wall','-Wextra','-Werror','-g','-I',str(SRC),str(f),str(SRC/'enhanced.c'),str(SRC/'air.c'),'-lm','-o',str(exe)]
  if a.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-sanitize-recover=all']
  subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
