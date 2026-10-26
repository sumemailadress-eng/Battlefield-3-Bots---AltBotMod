"""Actual queued A*: searches past the reported 60,001-node failure, still
shares bounded time slices, and cannot postpone timeout by frequent polling.
"""
from pathlib import Path
import argparse,subprocess,tempfile
from nav_queue_checks import WINDOWS
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src'
HARNESS=r'''
#include <assert.h>
#include <stdio.h>
#include "nav.c"
static int ray(const float*a,const float*b,float*h,float*n){(void)a;(void)b;(void)h;(void)n;test_ticks+=ray_cost;return 0;}
static NavGrid *split_grid(void){
 NavGrid*g=nav_create("LARGE_SEARCH",0,0,410,400,1);assert(g);g->phase=2;
 for(int z=0;z<g->h;z++)for(int x=0;x<g->w;x++){
  if(x==400)continue;
  int node=(z*g->w+x)*L;g->flags[node]=NAV_WALKABLE;
  for(int d=0;d<8;d++){int xx=x+kDir[d][0],zz=z+kDir[d][1];if(xx>=0 && zz>=0 && xx<g->w && zz<g->h && xx!=400)g->edges[node]|=1u<<d;}
 }return g;
}
static int poll(NavGrid*g,float*out){nav_set_path_owner(0);int n=nav_find_path(g,ray,10.5f,0,200.5f,405.5f,0,200.5f,out,128);nav_set_path_owner(-1);return n;}
int main(void){
 NavGrid*g=split_grid();size_t size=(size_t)g->w*g->h*L;unsigned char*mask=calloc(size,1);assert(mask);
 for(size_t i=0;i<size;i++)if(g->flags[i]&NAV_WALKABLE)mask[i]=1;
 nav_set_path_mask(mask);float out[128*3];assert(!poll(g,out));int ticks=0;long long largest=0;
 while(g_requests[0].state!=3 && ticks++<10000){
  long long start=test_ticks;nav_process_paths(.25);if(test_ticks-start>largest)largest=test_ticks-start;
  if(g_requests[0].state!=3){assert(!poll(g,out) && nav_last_deferred());}
 }
 assert(g_requests[0].state==3 && g_requests[0].closed==4096 && g_requests[0].partial);
 assert(largest<1000);assert(poll(g,out)>0 && !nav_last_deferred() && nav_last_partial());
 assert(out[0]>10.5f && out[0]<400);
 printf("PASS: disconnected large vehicle goal yields a real grid/collision-checked continuation after 4096 nodes (%d bounded slices, largest %.3f ms); no 160k-node queue monopoly\n",ticks,largest/1000.0);
 assert(!poll(g,out));nav_process_paths(.25);assert(g_requests[0].state==2);
 double started=g_requests[0].startedAt;for(int i=0;i<100;i++)assert(!poll(g,out));assert(g_requests[0].startedAt==started);
 test_ticks+=8100000;
 // Poll refreshes request ownership but not the search's start clock.
 assert(!poll(g,out));for(int i=0;i<10 && g_requests[0].state!=3;i++){nav_process_paths(.25);if(g_requests[0].state!=3)poll(g,out);}
 assert(g_requests[0].state==3 && strstr(g_requests[0].error,"time limit"));
 puts("PASS: continuous polling never resets the eight-second active-search timeout; stalled large searches terminate without blocking a whole tick");
 nav_set_path_mask(NULL);nav_free(g);free(mask);return 0;
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');a=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='bf3-large-search-') as tmp:
  tmp=Path(tmp);(tmp/'windows.h').write_text(WINDOWS);(tmp/'check.c').write_text(HARNESS)
  opts=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'] if a.sanitize else ['-O2']
  subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror',*opts,'-I',str(tmp),'-I',str(SRC),str(tmp/'check.c'),'-lm','-o',str(tmp/'check')],check=True)
  subprocess.run([str(tmp/'check')],check=True)
if __name__=='__main__':main()
