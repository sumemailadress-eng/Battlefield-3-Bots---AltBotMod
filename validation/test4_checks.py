"""Compile actual Test 4 tactical and round functions with mocked BF3 engine calls.
No client replication, native M-COM state, or BF3 flight physics is simulated.
"""
from pathlib import Path
import re,subprocess,tempfile,shutil,argparse
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src'
# Caspian V5: drive_tick calls the boat helpers (slip exit, landing). Harnesses that drive land vehicles get the V4
# behaviour (no landing check, the old end-of-route stop); caspian_v3_regressions.py tests the real helpers.
BOAT_STUBS=('static float boat_exit_heading(int i,DriveState*d,const float*vp,float h){(void)i;(void)d;(void)vp;return h;}\n'
 'static int boat_landed(int i,DriveState*d,const float*vp){(void)i;(void)d;(void)vp;return 0;}\n'
 'static int boat_ashore(int i,DriveState*d,const float*vp,const char*w){(void)w;vehicle_mark_bad(d->veh,180);if(armor_arrive(i,vp))return 1;drive_stop(i,"arrived - gets out",20);return 1;}\n')
def function(s,name):
 if name=='drive_tick' and 'boat_landed(' in s:return BOAT_STUBS+_function(s,name)
 return _function(s,name)
def _function(s,name):
 m=re.search(r'^static [^\n;]+\b'+name+r'\([^\n]*\)\n\{',s,re.M);assert m,name
 masked=re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"',lambda m:' '*len(m[0]),s)
 a=masked.index('{',m.start());d=1
 for b in range(a+1,len(s)):
  d+=(masked[b]=='{')-(masked[b]=='}')
  if d==0:return s[m.start():b+1]
 raise ValueError(name)
s=(SRC/'core.c').read_text()
TACTICS=r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#define MAX_FLAGS 16
#define OFF_PLAYER_TEAM 8
#define OFF_PLAYER_SOLDIER 16
#define OFF_SOLDIER_POSITION 0
#define OFF_SOLDIER_SPAWN_POSITION 0
#define OFF_SOLDIER_HEALTH 12
static struct {int botCount;char *bots[16];} host,*H=&host;
static unsigned char pm[256] __attribute__((aligned(8))),players[16][64] __attribute__((aligned(8))),soldiers[16][64] __attribute__((aligned(8)));
static void *arr[16];static int blocked;
static void *player_manager(void){return pm;}
static int player_team(char *p){return *(int *)(p+OFF_PLAYER_TEAM);}
static char *player_vehicle(char *p){return p==(char*)players[2] ? (char*)1 : NULL;}
static float dist2(const float *a,const float *b){float x=a[0]-b[0],z=a[2]-b[2];return x*x+z*z;}
static int solid_between(const float *a,const float *b,float *hit){(void)a;(void)hit;return b[0]==0?(blocked&1):(blocked&2);}
static int g_flagCount=4,owners[4];static float flags[4][3]={{0,0,0},{100,0,0},{200,0,0},{300,0,0}},centres[33][3];
static int g_squadTarget[3][33];static unsigned char g_squadDefend[3][33];
static void squad_centre(int team,int sq,float *out){(void)team;memcpy(out,centres[sq],12);}
static float *flag_pos(int f){return flags[f];}static int flag_owner(int f){return owners[f];}
'''+function(s,'revive_threat')+'\n'+function(s,'allocate_squads')+r'''
int main(void){
 H->botCount=4;void **array=arr;memcpy(pm+0xac,&array,sizeof array);*(unsigned*)(pm+8)=4;
 for(int i=0;i<4;i++){arr[i]=H->bots[i]=(char*)players[i];*(int*)(players[i]+8)=i?2:1;*(char**)(players[i]+16)=(char*)soldiers[i];*(float*)(soldiers[i]+12)=100;}
 *(float*)soldiers[0]=10;*(float*)soldiers[1]=50;*(float*)soldiers[2]=1000;*(float*)soldiers[3]=1000;
 float body[3]={0};blocked=0;assert(revive_threat(0,(char*)soldiers[0],body)==(char*)players[1]);
 blocked=1;assert(revive_threat(0,(char*)soldiers[0],body)==(char*)players[1]); // protected body, exposed medic
 blocked=2;assert(revive_threat(0,(char*)soldiers[0],body)==(char*)players[1]); // protected medic, exposed casualty
 blocked=3;assert(!revive_threat(0,(char*)soldiers[0],body));
 blocked=0;*(int*)(players[1]+8)=1;assert(!revive_threat(0,(char*)soldiers[0],body)); // friendly
 *(int*)(players[1]+8)=2;*(float*)(soldiers[1]+12)=0;assert(!revive_threat(0,(char*)soldiers[0],body));
 *(float*)(soldiers[2])=115;assert(revive_threat(0,(char*)soldiers[0],body)==(char*)players[2]); // vehicle threat at 115 m
 *(float*)(soldiers[2])=150;assert(!revive_threat(0,(char*)soldiers[0],body));
 for(int q=1;q<=8;q++)centres[q][0]=q*10;
 unsigned mask=0;for(int q=1;q<=8;q++)mask|=1u<<q;
 allocate_squads(1,mask);int n[4]={0};for(int q=1;q<=8;q++)n[g_squadTarget[1][q]-1]++;for(int f=0;f<4;f++)assert(n[f]==2);
 owners[0]=1;allocate_squads(1,mask);int guards=0;for(int q=1;q<=8;q++)guards+=g_squadDefend[1][q]!=0;assert(guards<=1);
 for(int f=0;f<4;f++)owners[f]=1;allocate_squads(1,mask);memset(n,0,sizeof n);for(int q=1;q<=8;q++)n[g_squadTarget[1][q]-1]++;for(int f=0;f<4;f++)assert(n[f]==2);
 puts("PASS: actual revive exposure/medic exposure/team/death/range and squad flag allocation regressions");return 0;
}
'''
ROUND=r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "modes.h"
#define MAX_BOTS 8
#define OFF_PLAYER_SOLDIER 16
#define OFF_PLAYER_INPUT 32
#define OFF_PLAYER_ONLINE_NAME 64
#define BOT_ROSTER(pl) ((const char *)(pl) + OFF_PLAYER_ONLINE_NAME)   /* V28 */
#define __attribute__(x)
#define __cdecl
#define TASK_NONE 0
static struct {int botCount;unsigned frame;char *bots[MAX_BOTS];void *botInput[MAX_BOTS];float botMove[MAX_BOTS][3];void *teamBlueprint[3];struct {void *entity;}teamSpawn[3];}host,*H=&host;
static int g_roundCleanup,g_roundEpoch,g_roundForceReset,g_levelReady,g_readdCount;static unsigned g_levelCheckAt;
static enum BMMode g_mode=BM_MODE_CQ;static char g_level[64]="MP_001",g_modeName[64]="ConquestLarge0";
static unsigned g_createdAt[MAX_BOTS],g_lastSpawnedAt[MAX_BOTS];static unsigned char g_silentRemove[MAX_BOTS];
static char *g_removeQueue[MAX_BOTS];static unsigned g_removeSince[MAX_BOTS];static int g_removeCount;
#define REMOVE_KICK_GRACE_FRAMES (5*30)
static long loads,rounds;static char reportedMap[64]="MP_001",reportedMode[64]="ConquestLarge0",map[64]="MP_001";
static char context[64] __attribute__((aligned(8)));static char *contextPtr=context;static void *object=(void*)1;
#define ADDR_SERVER_GAME_CONTEXT (&contextPtr)
static int kicks,kills,deleted,neutral;
static void chat_level_events(long *l,long *r){*l=loads;*r=rounds;}
static const char *current_level(void){return map;}
static void chat_server_state(char *m,int ml,char *s,int sl){snprintf(m,ml,"%s",reportedMap);snprintf(s,sl,"%s",reportedMode);}
static int suicidePending;
static int chat_poll_death(char *n,int nl,char *w,int wl){if(!suicidePending)return 0;suicidePending=0;snprintf(n,nl,"new bot");snprintf(w,wl,"Suicide");return 1;}
static int roster_ordinal(const char *n){(void)n;return -1;}
static void chat_rcon(const char *fmt,...){assert(strstr(fmt,"admin.kickPlayer"));kicks++;}
static void bot_log(const char *fmt,...){(void)fmt;}
static void tickets_reset(void){}static void enhanced_level_reset(void){}static int g_modeFallbackEpoch=-1;static enum BMMode mode_kind(const char *m){enum BMMode k=bm_mode(m);return k!=BM_MODE_UNKNOWN&&g_modeFallbackEpoch==g_roundEpoch?BM_MODE_TDM:k;}   /* AltBotMod V15, as mode_state.h */
static int InterlockedIncrement(int *p){return ++*p;}
static void set_level(void *in,int a,float v){(void)in;(void)a;assert(v==0);neutral++;}
typedef void (*SetLevel_t)(void*,int,float);
#define ADDR_ENTRYINPUT_SET_LEVEL set_level
#define _stricmp strcmp
static int bot_index(void *pl){for(int i=0;i<H->botCount;i++)if(H->bots[i]==pl)return i;return -1;}
static char *find_player(const char *n){for(int i=0;i<H->botCount;i++)if(!strcmp(n,H->bots[i]+64))return H->bots[i];return NULL;}
static void queue_bot_removal(int i){for(int k=0;k<g_removeCount;k++)if(g_removeQueue[k]==H->bots[i])return;g_removeSince[g_removeCount]=H->frame?H->frame:1;g_removeQueue[g_removeCount++]=H->bots[i];}
static void kill_bot(char *p){(void)p;kills++;}
static void *player_manager(void){return (void*)1;}
static void bot_forget(int i){memmove(&H->bots[i],&H->bots[i+1],(H->botCount-i-1)*sizeof H->bots[0]);memmove(&H->botInput[i],&H->botInput[i+1],(H->botCount-i-1)*sizeof H->botInput[0]);H->botCount--;}
static void delete_player(void *pm,void *pl){(void)pm;(void)pl;deleted++;}
typedef void (*PM_DeletePlayer_t)(void*,void*);
#define ADDR_PM_DELETE_PLAYER delete_player
#define _aligned_free free
'''+(SRC/'round_native.inc').read_text()+'\n'+function(s,'removal_tick')+r'''
int main(void){
 memcpy(context+0x10,&object,sizeof object);H->frame=100;H->botCount=3;
 char players[4][128]={0};for(int i=0;i<3;i++){snprintf(players[i]+64,40,"bot %d",i);H->bots[i]=players[i];H->botInput[i]=malloc(4);*(void**)(players[i]+16)=(void*)1;}
 round_roster_tick();assert(!kicks&&!g_roundCleanup);
 rounds++;round_roster_tick();assert(g_roundCleanup&&g_roundEpoch==1&&g_removeCount==3&&kicks==3&&neutral==180);
 H->frame=180;removal_tick();assert(!kills&&H->botCount==3&&kicks==6); // cleanup never suicide loops
 for(int i=0;i<3;i++)*(void**)(players[i]+16)=NULL;
 for(int i=0;i<3;i++)removal_tick();assert(deleted==3&&H->botCount==0&&g_removeCount==0);
 g_roundCleanup=0;loads++;round_roster_tick();assert(g_roundEpoch==2);
 strcpy(reportedMode,"TeamDeathMatch0");round_roster_tick();assert(g_mode==BM_MODE_TDM&&g_roundEpoch==3);strcpy(reportedMode,"CaptureTheFlag0");round_roster_tick();assert(g_mode==BM_MODE_CQ);g_modeFallbackEpoch=g_roundEpoch;round_roster_tick();assert(g_mode==BM_MODE_TDM);strcpy(reportedMode,"TeamDeathMatch0");round_roster_tick();round_roster_tick();assert(g_mode==BM_MODE_TDM&&g_modeFallbackEpoch!=g_roundEpoch);g_roundEpoch=3;
 g_roundForceReset=1;round_roster_tick();assert(!g_roundForceReset&&g_roundEpoch==4);
 H->botCount=1;H->bots[0]=players[3];strcpy(players[3]+64,"new bot");H->botInput[0]=malloc(4);*(void**)(players[3]+16)=(void*)1;g_roundCleanup=0;g_createdAt[0]=H->frame-5;suicidePending=1;round_roster_tick();assert(g_silentRemove[0]&&g_removeCount==1);removal_tick();assert(!kills);*(void**)(players[3]+16)=NULL;removal_tick();assert(!H->botCount&&deleted==4);
 puts("PASS: actual round retirement, neutral inputs, no suicide retries, fresh identity deletion, mode/event/manual reset");return 0;
}
'''
PORTABLE=r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "modes.h"
'''+re.search(r'typedef struct BMModeProfile \{[\s\S]*?\} BMModeProfile;', (SRC/'mode_state.h').read_text())[0]+'\n#include "mode_profiles.inc"\n'+r'''
int main(void){
 int rush=0,cq=0;for(int k=0;k<29;k++){const BMMap *m=&bm_maps[k];assert(bm_map(m->code)==m&&bm_map(m->alias)==m);assert(!strcmp(bm_map_mode(m,"sqdm"),"SquadDeathMatch0"));if(m->closeQuarters){cq++;assert(!bm_map_mode(m,"rush"));assert(!strcmp(bm_map_mode(m,"tdm"),"TeamDeathMatchC0"));}else{rush++;assert(!strcmp(bm_map_mode(m,"rush"),"RushLarge0"));assert(!strcmp(bm_map_mode(m,"tdm"),"TeamDeathMatch0"));}}
 assert(rush==25&&cq==4&&bm_mode_teams(BM_MODE_SQDM)==4);
 assert(!strcmp(bm_map_mode(bm_map("azadi"),"cq"),"ConquestLarge0"));assert(!strcmp(bm_map_mode(bm_map("talah"),"cq"),"ConquestAssaultLarge0"));
 assert(bm_roster_team("[BOT] Engineer SQ4",BM_MODE_SQDM,0)==4);assert(bm_roster_team("[BOT] Medic US",BM_MODE_CQ,0)==1);
 int h[5]={0},t[5];bm_roster_targets(4,4,1,32,h,t);for(int k=1;k<=4;k++)assert(t[k]==3);h[2]=2;bm_roster_targets(4,4,1,32,h,t);assert(t[2]==1&&t[1]==3);
 int counts[4]={0};for(int n=0;n<4;n++){float best=1e30f;int role=-1;for(int k=0;k<4;k++){float score=bm_dispatch_score(k,counts,k==0?1500:1);if(score<best){best=score;role=k;}}assert(role==n);counts[role]++;}
 assert(sizeof g_modeProfiles/sizeof g_modeProfiles[0]==152);int profiles[5]={0},squadRush=0;for(unsigned k=0;k<152;k++){const BMModeProfile *p=&g_modeProfiles[k];profiles[bm_mode(p->mode)]++;squadRush+=bm_mode_squad_rush(p->mode);for(int j=0;j<3;j++){assert(isfinite(p->low[j])&&isfinite(p->high[j])&&p->high[j]>=p->low[j]);}for(int j=0;j<32;j++)for(int a=0;a<3;a++)assert(isfinite(p->patrol[j][a]));}
 assert(profiles[BM_MODE_RUSH]==38&&squadRush==13&&profiles[BM_MODE_TDM]==29&&profiles[BM_MODE_SQDM]==29);
 puts("PASS: 29 maps, 152 layout profiles including 25 Rush/13 Squad Rush/29 TDM/29 SQDM, correct mode aliases, four-team human reserves, fair full-fleet dispatch");return 0;
}
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--sanitize',action='store_true');args=ap.parse_args();cc=shutil.which('gcc')or'cc'
 with tempfile.TemporaryDirectory(prefix='botmod-test4-') as temp:
  temp=Path(temp)
  for name,code in [('mode-profiles',PORTABLE),('tactics',TACTICS),('round',ROUND)]:
   f=temp/(name+'.c');f.write_text(code);exe=temp/name
   opts=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-sanitize-recover=all','-g']if args.sanitize else ['-O2']
   subprocess.run([cc,'-std=c11','-Wall','-Wextra','-Wno-misleading-indentation',*opts,'-I',str(SRC),str(f),str(SRC/'modes.c'),'-lm','-o',str(exe)],check=True)
   subprocess.run([str(exe)],check=True)
 # Code coverage audit: each indexed array must compact exactly once; pointer-keyed queues stay keyed by pointers.
 # AltBotMod V10: every include and header (V9's combat_profile_native.inc tables were outside the audit and uncompacted)
 expanded=s+'\n'+'\n'.join(p.read_text() for p in sorted(SRC.glob('*.inc'))+sorted(SRC.glob('*.h')))
 arrays=set(re.findall(r'\b(g_\w+)\[MAX_BOTS\]',expanded));pointer_keys={'g_removeQueue','g_removeSince','g_readdName','g_readdTeam','g_squadAsked','g_squadAskedAt'}
 shifting=re.findall(r'SHIFT\((g_\w+)\)',function(s,'bot_forget'));forgetting=re.findall(r'FORGET\((g_\w+)\)',function(s,'enhanced_forget'))
 assert not set(shifting)&set(forgetting), 'Double compaction'
 assert arrays-pointer_keys==set(shifting)|set(forgetting),(arrays-pointer_keys)-(set(shifting)|set(forgetting))
 assert len(forgetting)==len(set(forgetting))
 assert 'kill_bot' not in function(s,'hq_probe_one')
 assert 'if (0)' in function(s,'cmd_spawn')
 print('PASS: all',len(arrays-pointer_keys),'indexed bot arrays compact once; startup spawn probes disabled')
if __name__=='__main__':main()
