"""Actual vault/knife sequence regressions; engine materials and buttons are mocked."""
from pathlib import Path
import subprocess,tempfile,os,argparse,re
from test4_checks import function
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src';core=(SRC/'core.c').read_text()
body=r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_BOTS 4
#define OFF_PLAYER_ONLINE_NAME 0
#define OFF_SOLDIER_POSITION 0
#define EIA_JUMP 12
#define EIA_FIRE_KNIFE 7
#define VAULT_HOLD_FRAMES 30
#define SLOT_ACTION(x) (20+(x))
static struct {unsigned frame;char*bots[MAX_BOTS];}host,*H=&host;
static unsigned g_local_stuck_jump_lastAt[MAX_BOTS],g_vaultUntil[MAX_BOTS];
static float g_vaultFrom[MAX_BOTS][3],g_fenceStrafe[MAX_BOTS],g_fencePos[MAX_BOTS][3];
static unsigned g_fenceUntil[MAX_BOTS],g_fenceAt[MAX_BOTS];static unsigned char g_fenceTries[MAX_BOTS];
static int jumpAction,jumpFrames,schedules;static struct {int action;unsigned at;}presses[8];
static void bot_log(const char*f,...){(void)f;}
static void press_button(int i,int action,int frames){(void)i;jumpAction=action;jumpFrames=frames;}
static void schedule_press(int i,int action,unsigned at){(void)i;presses[schedules].action=action;presses[schedules++].at=at;}
'''+re.search(r'typedef struct FenceKind.*?};',core,re.S)[0]+r'''
static int kind=0;static const FenceKind*fence_ahead(int i,char*s){(void)i;(void)s;return kind<0?NULL:&kFences[kind];}
static int try_break_panel(int i,char*s){(void)i;(void)s;return 0;}   /* AltBotMod V18 */
static int breach_start(int i,char*s){(void)i;(void)s;return 0;}   /* AltBotMod V32 */
'''+function(core,'stuck_jump')+'\n'+function(core,'try_cut_fence')+r'''
int main(void){
 float body[3]={5,7,9};H->bots[0]="bot";H->frame=100;stuck_jump(0,(char*)body,"fixture barrier");
 assert(jumpAction==12&&jumpFrames==4&&g_vaultUntil[0]==130&&!memcmp(g_vaultFrom[0],body,12));
 H->frame=120;stuck_jump(0,(char*)body,"same barrier");assert(g_vaultUntil[0]==130);
 H->frame=140;stuck_jump(0,(char*)body,"next barrier");assert(g_vaultUntil[0]==170);
 H->frame=200;assert(try_cut_fence(0,(char*)body)==1&&schedules==5);
 assert(presses[0].action==SLOT_ACTION(7)&&presses[0].at==201);
 assert(presses[1].action==EIA_FIRE_KNIFE&&presses[1].at==225&&presses[2].at==245&&presses[3].at==265);
 assert(presses[4].action==SLOT_ACTION(0)&&presses[4].at==290&&g_fenceUntil[0]==295);
 schedules=0;H->frame=300;assert(try_cut_fence(0,(char*)body)==1&&g_fenceTries[0]==2);
 assert(try_cut_fence(0,(char*)body)==0);kind=1;body[0]+=10;H->frame=400;schedules=0;assert(try_cut_fence(0,(char*)body)==2&&!schedules);
 kind=-1;body[0]+=10;assert(try_cut_fence(0,(char*)body)==0);
 puts("PASS: actual vault jump/forward-hold timing and retry cooldown retained; knifeable fence selects knife, slashes three times and restores rifle; second try bounded and sheet metal requests detour");return 0;
}
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--sanitize',action='store_true');args=ap.parse_args()
 with tempfile.TemporaryDirectory(prefix='bf3-obstacles-')as tmp:
  f=Path(tmp)/'checks.c';f.write_text(body);exe=f.with_suffix('');cmd=['gcc','-std=c11','-Wall','-Wextra','-Werror',str(f),'-lm','-o',str(exe)]
  if args.sanitize:cmd[1:1]=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']
  subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
if __name__=='__main__':main()
