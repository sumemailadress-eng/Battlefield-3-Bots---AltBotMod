"""Exercise the actual native navigation implementation with a deterministic clock.
No BF3 physics or damage replication is simulated.
"""
from pathlib import Path
import argparse, subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'files/BotMod/src'
WINDOWS=r'''
#pragma once
#include <stdlib.h>
#include <stdint.h>
typedef struct {long long QuadPart;} LARGE_INTEGER;
#define HEAP_ZERO_MEMORY 8
static long long test_ticks,clock_step=10,ray_cost;
static void *GetProcessHeap(void){return (void*)1;}
static void *HeapAlloc(void *h,unsigned f,size_t n){(void)h;return f&8?calloc(1,n):malloc(n);}
static int HeapFree(void *h,unsigned f,void *p){(void)h;(void)f;free(p);return 1;}
static unsigned GetTickCount(void){return (unsigned)(test_ticks/1000);}
static int QueryPerformanceFrequency(LARGE_INTEGER *p){p->QuadPart=1000000;return 1;}
static int QueryPerformanceCounter(LARGE_INTEGER *p){test_ticks+=clock_step;p->QuadPart=test_ticks;return 1;}
'''
HARNESS=r'''
#include <assert.h>
#include <stdio.h>
#include "nav.c"
static int wall_x,wall_gap,ray_calls;
static int test_ray(const float *a,const float *b,float *p,float *n){
 (void)p;(void)n;test_ticks+=ray_cost;ray_calls++;
 if(!wall_x)return 0;
 float wall=wall_x+0.5f;
 if((a[0]<wall && b[0]>wall)||(b[0]<wall && a[0]>wall)){
  float t=(wall-a[0])/(b[0]-a[0]),z=a[2]+t*(b[2]-a[2]);return z>=wall_gap;
 }return 0;
}
static NavGrid *flat(int w,int h,int barrier,int gap){
 NavGrid *g=nav_create("QUEUE_TEST",0,0,w,h,1);g->phase=2;
 for(int z=0;z<h;z++)for(int x=0;x<w;x++){
  if(x==barrier && z>=gap)continue;
  int u=(z*w+x)*L;g->flags[u]=NAV_WALKABLE;g->height[u]=0;
  for(int d=0;d<8;d++){
   int xx=x+kDir[d][0],zz=z+kDir[d][1];
   if(xx>=0 && zz>=0 && xx<w && zz<h && !(xx==barrier && zz>=gap))g->edges[u]|=1u<<d;
  }
 }return g;
}
static int poll(int owner,NavGrid *g,const float *s,const float *t,float *out,int max){
 nav_set_path_owner(owner);int n=nav_find_path(g,test_ray,s[0],s[1],s[2],t[0],t[1],t[2],out,max);nav_set_path_owner(-1);return n;
}
static void connected(NavGrid *g,const float *s,const float *out,int n){
 float from[3];memcpy(from,s,12);
 for(int k=0;k<n;k++){
  const float *to=out+k*3;int a=nearest_node(g,from[0],from[1],from[2]),b=nearest_node(g,to[0],to[1],to[2]);
  assert(a>=0 && b>=0);assert(straight_walkable(g,a,b));memcpy(from,to,12);
 }
}
int main(void){
 float out[256*3],s[3]={10.5f,0,145.5f},t[3]={150.5f,0,145.5f};
 wall_x=80;wall_gap=4;NavGrid *g=flat(180,180,wall_x,wall_gap);
 assert(!poll(0,g,s,t,out,128)&&nav_last_deferred());assert(g_requests[0].state==1);
 nav_process_paths(0.25);assert(g_requests[0].state==2);int before=g_requests[0].closed;assert(before>0 && before<1000);
 assert(!poll(0,g,s,t,out,128)&&nav_last_deferred());assert(g_requests[0].closed==before); // polling never restarts A*
 int n=0,ticks=0;long long largest=0;
 while(!n && ticks++<10000){
  long long a=test_ticks;nav_process_paths(0.25);if(test_ticks-a>largest)largest=test_ticks-a;
  n=poll(0,g,s,t,out,128);if(!n)assert(nav_last_deferred());
 }
 assert(n && ticks>2 && !nav_last_partial());assert(fabsf(out[(n-1)*3]-t[0])<0.01f && fabsf(out[(n-1)*3+2]-t[2])<0.01f);
 connected(g,s,out,n);assert(largest<1000);
 printf("PASS: detour route finishes across %d bounded slices; no search restart or guessed route; largest clock slice %.3f ms\n",ticks,largest/1000.0);
 // Shortcut physics checks can be slow. Their candidate index must resume too.
 nav_cancel_paths();ray_cost=100;ray_calls=0;
 assert(!poll(0,g,s,t,out,128));ticks=0;n=0;
 while(!n && ticks++<10000){nav_process_paths(0.25);n=poll(0,g,s,t,out,128);}
 assert(n && ray_calls>10 && !nav_last_partial());connected(g,s,out,n);ray_cost=0;
 puts("PASS: expensive rejected shortcut checks resume instead of repeating the first candidate");
 // All 60 bot request owners eventually receive their own result.
 nav_cancel_paths();wall_x=0;nav_free(g);g=flat(100,100,-1,0);
 float sources[60][3],targets[60][3];int done[60]={0},remaining=60;
 for(int i=0;i<60;i++){sources[i][0]=5.5f+i%8;sources[i][1]=0;sources[i][2]=5.5f+i%7;targets[i][0]=70.5f+i%8;targets[i][1]=0;targets[i][2]=70.5f+i%7;assert(!poll(i,g,sources[i],targets[i],out,128));}
 for(ticks=0;ticks<10000 && remaining;ticks++){
  nav_process_paths(0.5);
  for(int i=0;i<60;i++)if(!done[i]){n=poll(i,g,sources[i],targets[i],out,128);if(n){assert(!nav_last_partial());connected(g,sources[i],out,n);done[i]=1;remaining--;}}
 }
 assert(!remaining);puts("PASS: 60 owners share one workspace and all receive the correct completed route");
 // A truly disconnected destination stays pending until the search ends.
 nav_free(g);g=flat(90,90,45,0);s[0]=10.5f;s[2]=40.5f;t[0]=80.5f;t[2]=40.5f;assert(!poll(0,g,s,t,out,128));
 int ended=0;for(ticks=0;ticks<10000 && !ended;ticks++){nav_process_paths(0.25);n=poll(0,g,s,t,out,128);assert(!n);ended=!nav_last_deferred();}
 assert(ended && strstr(nav_last_error(),"exhausted"));puts("PASS: unfinished work differs from a completed unreachable search");
 // A short output buffer truncates a known route, with an explicit continuation.
 nav_free(g);g=flat(300,10,-1,0);s[0]=5.5f;s[2]=5.5f;t[0]=290.5f;t[2]=5.5f;assert(!poll(0,g,s,t,out,2));
 for(ticks=0,n=0;ticks<10000 && !n;ticks++){nav_process_paths(0.25);n=poll(0,g,s,t,out,2);}
 assert(n==2 && nav_last_partial() && out[(n-1)*3]<t[0]);connected(g,s,out,n);
 puts("PASS: waypoint capacity is marked as continuation rather than destination arrival");
 // A request for another vehicle/goal cancels the old owner's work.
 assert(!poll(0,g,s,t,out,128));float changed[3]={30.5f,0,5.5f};assert(!poll(0,g,s,changed,out,128));assert(g_requests[0].target[0]==changed[0]);
 unsigned serial=g->serial;nav_free(g);g=flat(30,30,-1,0);assert(g->serial!=serial && g_activeRequest<0);
 for(int i=0;i<NAV_PATH_OWNERS;i++)assert(!g_requests[i].state);

 puts("PASS: replacement goals and grid destruction clear pending owners and prevent stale pointers");nav_free(g);
 // One deployment is outside the driving mask; its rival starts on it.
 g=flat(80,24,-1,0);g->cell=2;unsigned char *mask=calloc((size_t)g->w*g->h*L,1);
 for(int z=0;z<g->h;z++)for(int x=12;x<g->w;x++)mask[(z*g->w+x)*L]=1;
 nav_set_path_mask(mask);nav_set_vehicle_width(1.9f);s[0]=2.5f;s[1]=0;s[2]=24.5f;t[0]=100.5f;t[1]=0;t[2]=24.5f;
 for(int team=0;team<2;team++){
  nav_cancel_paths();s[0]=team?140.5f:2.5f;assert(!poll(team+64,g,s,t,out,128));
  for(ticks=0,n=0;ticks<2000 && !n;ticks++){nav_process_paths(.25);n=poll(team+64,g,s,t,out,128);if(!n && !nav_last_deferred())break;}
  assert(n && !nav_last_partial());if(fabsf(out[(n-1)*3]-t[0])>=.01f)fprintf(stderr,"team%d n%d end%f goal%f grid %d/%d cell%f\n",team,n,out[(n-1)*3],t[0],g->w,g->h,g->cell);assert(fabsf(out[(n-1)*3]-t[0])<.01f);
  if(!team){assert(out[0]>=24 && out[0]<=26);const unsigned char *saved=g_pathMask;g_pathMask=NULL;
   assert(straight_walkable(g,nearest_node(g,s[0],0,s[2]),nearest_node(g,out[0],0,out[2])));g_pathMask=saved;}
 }
 // Parking connector validation must not ignore a wall between pad and road.
 nav_cancel_paths();wall_x=10;wall_gap=0;s[0]=2.5f;assert(!poll(64,g,s,t,out,128));
 int finished=0;for(ticks=0;ticks<2000 && !finished;ticks++){nav_process_paths(.25);n=poll(64,g,s,t,out,128);assert(!n);finished=!nav_last_deferred();}
 assert(finished);wall_x=0;puts("PASS: both deployment routes; 22m checked parking connector; blocked connector returns no guessed route");
 // A destination on a bridge cannot snap to the road underneath it.
 int u=(12*g->w+50)*L;g->height[u+1]=12;g->flags[u+1]=NAV_WALKABLE;mask[u+1]=1;
 float floor[3];assert(nav_drive_near(g,mask,100,12,24,120,floor) && floor[1]==12);
 assert(nav_drive_near(g,mask,100,0,24,120,floor) && floor[1]==0);
 assert(!nav_drive_near(g,mask,100,NAN,24,120,floor));
 puts("PASS: height-aware vehicle endpoint selects correct bridge/road layer and rejects invalid height");
 nav_set_path_mask(NULL);nav_set_vehicle_width(1.5f);nav_cancel_paths();free(mask);nav_free(g);return 0;
}
'''
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--sanitize',action='store_true');a=ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='bf3-nav-queue-') as temp:
        temp=Path(temp);(temp/'windows.h').write_text(WINDOWS);(temp/'check.c').write_text(HARNESS)
        cmd=['gcc','-std=c11','-O0','-g','-Wall','-Wextra','-Werror','-I',str(temp),'-I',str(SRC),str(temp/'check.c'),'-lm','-o',str(temp/'check')]
        if a.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        subprocess.run(cmd,check=True);subprocess.run([str(temp/'check')],check=True)
if __name__=='__main__':main()
