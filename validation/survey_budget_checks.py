"""Exercise actual paving/tree survey functions against bounded fake ray times."""
from pathlib import Path
import argparse,subprocess,tempfile,os
from test4_checks import function
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src';core=(SRC/'core.c').read_text()
PRE=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "nav.h"
#define MAX_PATH 256
#define RAY_DONT_CHECK_CHARACTER 0x20
#define RAY_CHECK_DETAIL_MESH 0x40
static NavGrid grid,*gp=&grid;
static unsigned g_driveMaskFor,g_treeFor,g_rayFlags;
static unsigned char*g_driveMask,*g_treeDist;
static int g_paveCursor,g_paveCount,g_treeCursor,g_treeCount,g_treeFinishPass,g_treeFinishNode,g_treeNearCount;
static int rays,trees,applied;
static double clockMs,rayMs;
static double perf_now(void){return clockMs;}
static NavGrid**nav_slot(void){return &gp;}
static void*GetProcessHeap(void){return NULL;}
static void*HeapAlloc(void*h,int f,size_t n){(void)h;(void)f;return malloc(n);}
static void HeapFree(void*h,int f,void*p){(void)h;(void)f;free(p);}
static void GetModuleFileNameA(void*m,char*s,int n){(void)m;snprintf(s,n,"X:\\dummy\\server.exe");}
static void pave_cache_path(char*s,int n,const char*l){(void)l;snprintf(s,n,"/dev/null");}
static void bot_log(const char*s,...){(void)s;}
void nav_node_pos(const NavGrid*g,int node,float*out){(void)g;out[0]=node/NAV_LAYERS;out[1]=out[2]=0;}
int nav_step_node(const NavGrid*g,int node,int dir){(void)g;(void)dir;return node>0?node-1:-1;}
void nav_set_tree_dist(const NavGrid*g,const unsigned char*d){(void)g;(void)d;applied++;}
static void tree_apply_drive(void){applied++;}
static float dist2(const float*a,const float*b){float x=a[0]-b[0],z=a[2]-b[2];return x*x+z*z;}
static unsigned hit_material(void){return 0x3f00;}
static int ray_cast(const float*a,const float*b,float*h,float*n){
 rays++;clockMs+=rayMs;
 if(!trees && a[1]>b[1]){if(h){h[0]=a[0];h[1]=h[2]=0;}return 1;}
 if(trees && (g_rayFlags&RAY_CHECK_DETAIL_MESH)){
  if(h){h[0]=a[0]+.5f;h[1]=a[1];h[2]=a[2];}if(n){n[0]=1;n[1]=n[2]=0;}return 1;
 }
 return 0;
}
static FILE*ignore_file(const char*p,const char*m){(void)p;(void)m;return NULL;}
#define fopen ignore_file
'''
BODY=PRE+function(core,'drive_pave_tick')+function(core,'tree_tick')+r'''
int main(void){
 int n=120000;grid.phase=2;grid.serial=1;grid.w=200;grid.h=100;grid.cell=2;strcpy(grid.level,"SURVEY");
 grid.flags=calloc(n,1);grid.edges=calloc(n,1);g_driveMask=malloc(n);memset(grid.flags,NAV_WALKABLE,n);memset(grid.edges,255,n);memset(g_driveMask,1,n);
 g_driveMaskFor=1;rayMs=.1;drive_pave_tick();assert(rays<=8 && g_paveCursor<n && clockMs<1.6 && g_paveCount>0);
 rays=0;clockMs=0;rayMs=0;drive_pave_tick();assert(rays==64);
 // The cheap mask exists before paving completes; survey work cannot freeze all drivers.
 assert(g_driveMask[1000]==1 && g_paveCursor<n);
 rays=0;clockMs=0;trees=1;rayMs=.1;tree_tick();assert(rays<=32 && g_treeCursor<n && g_treeCursor>0);
 rays=0;clockMs=0;rayMs=0;tree_tick();assert(rays<=16*32 && g_treeCursor<n);
 // Final propagation of a large survey resumes over multiple calls. It never
 // runs both full-map sweeps and the node count in the same server tick.
 g_treeCursor=n;g_treeCount=1;g_treeFinishPass=g_treeFinishNode=g_treeNearCount=0;
 memset(g_treeDist,255,n);g_treeDist[0]=5;int calls=0,previous;
 do{previous=g_treeFinishNode;tree_tick();calls++;
  assert(g_treeCursor<0 || g_treeFinishNode-previous<=4096 || g_treeFinishPass>0);
  assert(calls<200);
 }while(g_treeCursor>=0);
 assert(calls>50 && g_treeFinishPass==2 && applied==3);
 free(grid.flags);free(grid.edges);free(g_driveMask);free(g_treeDist);
 puts("PASS: actual paving survey respects a 64-ray/time cap, tree geometry respects a 16-node/time cap, and large-map final propagation/count resumes across server ticks while a usable driving mask remains published");return 0;
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');a=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='bf3-survey-')as d:
  f=Path(d)/'checks.c';f.write_text(BODY);exe=f.with_suffix('');cmd=['gcc','-std=c11','-Wall','-Wextra','-Werror','-I',str(SRC),str(f),'-lm','-o',str(exe)]
  if a.sanitize:cmd[1:1]=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']
  subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
if __name__=='__main__':main()
