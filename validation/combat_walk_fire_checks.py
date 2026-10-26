"""Actual checked-path combat control with native offsets mocked for host width."""
from pathlib import Path
import argparse, tempfile, subprocess, os
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src'
body=r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define OFF_PLAYER_SOLDIER 16
#define OFF_SOLDIER_POSITION 0
#define OFF_SOLDIER_SPAWN_POSITION 0
#define OFF_SOLDIER_YAW 24
#define TASK_FIGHT 2
static struct {unsigned frame;char*bots[1];float botMove[1][3];} host,*H=&host;
static struct {unsigned until;} g_combatMove[1];
static struct {int task;} brain;
static struct {int mode;float targetYaw;} navigation;
typedef struct {int next,count;short link[2];float pts[6];} BotPath;
static BotPath g_paths[1];
static unsigned g_fenceUntil[1],g_vaultUntil[1];static int g_stairOn[1];
static char*g_fightTarget[1];
static _Alignas(16) char players[2][64],soldiers[2][64];
static void *bot_brain(int i){(void)i;return &brain;}
static void *bot_nav(int i){(void)i;return &navigation;}
#define bot_brain(i) (&brain)
#define bot_nav(i) (&navigation)
static int player_exists(char*p){return p==players[1];}
static int player_team(char*p){return *(int*)(p+8);}
static float yaw_towards(float x,float z){return atan2f(x,z);}
static float wrap_err(float a,float b){return remainderf(a-b,6.2831853f);}
'''+(SRC/'combat_walk_fire.inc').read_text()+r'''
static void ready(void){
 memset(&host,0,sizeof host);memset(g_paths,0,sizeof g_paths);memset(players,0,sizeof players);memset(soldiers,0,sizeof soldiers);
 H->frame=100;H->bots[0]=players[0];g_fightTarget[0]=players[1];
 *(char**)(players[0]+16)=soldiers[0];*(char**)(players[1]+16)=soldiers[1];
 *(int*)(players[0]+8)=1;*(int*)(players[1]+8)=2;
 g_combatMove[0].until=200;brain.task=TASK_FIGHT;navigation.mode=3;
 g_paths[0].count=1;g_paths[0].link[0]=-1;g_paths[0].pts[0]=10;
 ((float*)soldiers[1])[2]=100;g_vaultUntil[0]=g_fenceUntil[0]=0;g_stairOn[0]=0;
}
static void held(void){H->botMove[0][0]=.2f;H->botMove[0][1]=.3f;H->botMove[0][2]=.4f;combat_walk_fire(0);assert(H->botMove[0][0]==.2f && H->botMove[0][1]==.3f && H->botMove[0][2]==.4f);}
int main(void){
 ready();combat_walk_fire(0);assert(fabsf(H->botMove[0][0])<.01f && H->botMove[0][1]<-.74f && navigation.targetYaw==0);
 *(float*)(soldiers[0]+24)=1.5707963f;combat_walk_fire(0);assert(H->botMove[0][0]>.74f && H->botMove[0][2]>0);
 ready();g_vaultUntil[0]=101;held();ready();g_fenceUntil[0]=101;held();
 ready();g_stairOn[0]=1;held();ready();g_paths[0].link[0]=0;held();
 ready();*(int*)(players[1]+8)=1;held();ready();g_paths[0].count=0;held();
 puts("PASS: actual flank control follows a checked waypoint while facing the enemy; vaulting, fence cutting, stairs, ladder links and changed target teams retain control");return 0;
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');a=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='bf3-combat-control-') as tmp:
  f=Path(tmp)/'checks.c';f.write_text(body);exe=f.with_suffix('')
  cmd=['gcc','-std=c11','-Wall','-Wextra','-Wno-unused-function','-I',str(SRC),str(f),'-lm','-o',str(exe)]
  if a.sanitize:cmd[1:1]=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']
  subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
if __name__=='__main__':main()
