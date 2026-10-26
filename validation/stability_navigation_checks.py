"""Actual nav implementation: uploaded-log coverage regression, extension,
boundary corruption and allocation failures. Terrain in the small route
scenario is mocked; no BF3 terrain or crash dump is simulated.
"""
from pathlib import Path
import argparse, subprocess, tempfile
from nav_queue_checks import WINDOWS
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'files/BotMod/src'
WINDOWS=WINDOWS.replace('static void *HeapAlloc', 'static int fail_after=-1,live_allocations,allocation_calls;\nstatic void *HeapAlloc')
WINDOWS=WINDOWS.replace('(void)h;return f&8?calloc(1,n):malloc(n);', '(void)h;allocation_calls++;if(fail_after==0)return NULL;if(fail_after>0)fail_after--;void *p=f&8?calloc(1,n):malloc(n);if(p)live_allocations++;return p;')
WINDOWS=WINDOWS.replace('(void)h;(void)f;free(p);return 1;', '(void)h;(void)f;if(p)live_allocations--;free(p);return 1;')
HARNESS=r'''
#include <assert.h>
#include <stdio.h>
#include <float.h>
#include "nav.c"
static int old_ground_rays;
static int flat_ray(const float *from,const float *to,float *hit,float *normal){
 if(from[1]>to[1] && from[1]>=0 && to[1]<=0){
  if(from[1]>1000 && from[0]>=.5f && from[0]<=29.5f && from[2]>=.5f && from[2]<=29.5f)old_ground_rays++;
  if(hit){hit[0]=from[0];hit[1]=0;hit[2]=from[2];}
  if(normal){normal[0]=normal[2]=0;normal[1]=1;}return 1;
 }return 0;
}
static NavGrid *flat_grid(int w,int h){
 NavGrid *g=nav_create("STABILITY",0,0,w,h,1);assert(g);g->phase=2;
 for(int z=0;z<h;z++)for(int x=0;x<w;x++){
  int node=(z*w+x)*L;g->flags[node]=NAV_WALKABLE;
  for(int d=0;d<8;d++){int xx=x+kDir[d][0],zz=z+kDir[d][1];if(xx>=0&&zz>=0&&xx<w&&zz<h)g->edges[node]|=1u<<d;}
 }return g;
}
static int route(int owner,NavGrid *g,float x,float z,float *out){
 nav_set_path_owner(owner);int n=nav_find_path(g,flat_ray,x,0,z,15.5f,0,15.5f,out,128);nav_set_path_owner(-1);return n;
}
int main(int argc,char **argv){
 assert(argc==3);float out[128*3];ray_cost=0;nav_set_path_mask(NULL);
 // This is the actual packaged Desert grid used in the submitted Test8 log.
 NavGrid *g=nav_load(argv[1]);assert(g && g->w==624 && g->h==612 && g->cell==2);
 assert(!nav_covers(g,271,776,0) && !nav_covers(g,271,1423,0));
 NavGrid *large=nav_expand_bounds(g,-600,-750,800,1500);assert(large);
 assert(nav_covers(large,271,776,0) && nav_covers(large,271,1423,0));
 int left=(int)lroundf((g->x0-large->x0)/g->cell),top=(int)lroundf((g->z0-large->z0)/g->cell);
 for(int z=0;z<g->h;z++){
  size_t a=(size_t)z*g->w*L,b=((size_t)(z+top)*large->w+left)*L,n=(size_t)g->w*L;
  assert(!memcmp(g->height+a,large->height+b,n*sizeof(float)));
  assert(!memcmp(g->flags+a,large->flags+b,n) && !memcmp(g->edges+a,large->edges+b,n) && !memcmp(g->fences+a,large->fences+b,n));
 }
 int tank=nav_cell_index(large,271,776);assert(tank>=0 && !large->flags[tank*L] && large->phase==0);
 nav_free(g);nav_free(large);
 puts("PASS: real Desert cache excludes logged tank/deployment area; extension covers both and preserves all surveyed bytes without inventing terrain");
 // Small physics-survey mock: old cells are preserved, new cells are probed,
 // and their seam forms routes from deployments on both sides.
 g=flat_grid(30,30);g->fences[(15*30+15)*L]=2;
 NavGrid *expanded=nav_expand_bounds(g,-20,-20,60,60);assert(expanded);
 nav_free(g);old_ground_rays=0;int slices=0;
 while(expanded->phase!=2 && slices++<10000)nav_build_step(expanded,flat_ray,256,.5);
 assert(expanded->phase==2 && !old_ground_rays && slices>1);
 assert(expanded->fences[((15+21)*expanded->w+15+21)*L]==2);
 for(int team=0;team<2;team++){
  float x=team?55.5f:-15.5f,z=team?55.5f:-15.5f;int n=route(team,expanded,x,z,out),ticks=0;
  while(!n && ticks++<1000){nav_process_paths(.5);n=route(team,expanded,x,z,out);}
  assert(n && !nav_last_partial() && fabsf(out[(n-1)*3]-15.5f)<.01f);
 }
 nav_free(expanded);puts("PASS: incremental extension skips old ground and joins both deployment routes through real surveyed seam links");
 // Corrupted boundary bits cannot wrap into another row or index past arrays.
 g=flat_grid(6,6);g->edges[0]=g->fences[0]=255;
 assert(nav_step_node(g,0,4)==-1 && nav_step_node(g,0,6)==-1 && nav_step_node(g,-1,0)==-1);
 assert(nav_cell_index(g,NAN,0)==-1 && nav_cell_index(g,FLT_MAX,0)==-1);
 assert(nav_cell_index(NULL,0,0)==-1);
 nav_set_path_owner(-1);assert(!nav_find_path(g,flat_ray,NAN,0,1,2,0,2,out,128));
 assert(!nav_find_path(g,flat_ray,1,INFINITY,1,2,0,2,out,128));
 assert(nav_find_path_sync(g,flat_ray,.5f,0,.5f,5.5f,0,5.5f,out,128)>0);
 nav_free(g);puts("PASS: malformed boundary links, nonfinite positions and null grids fail safely; valid routes remain usable");
 char path[1024];snprintf(path,sizeof path,"%s/cache.bin",argv[2]);
 NavFileHeader header={0};header.magic=NAV_FILE_MAGIC;header.layers=L;header.cell=2;header.w=4000;header.h=1000;
 FILE *file=fopen(path,"wb");assert(file);fwrite(&header,sizeof header,1,file);fclose(file);
 int allocated=allocation_calls;assert(!nav_load(path) && allocation_calls==allocated);
 header.w=0x7fffffff;header.h=0x7fffffff;
 file=fopen(path,"wb");fwrite(&header,sizeof header,1,file);fclose(file);assert(!nav_load(path));
 g=flat_grid(8,8);g->height[0]=NAN;assert(nav_save(g,path));nav_free(g);assert(!nav_load(path));
 assert(!nav_create("bad",0,0,INFINITY,2,1) && !nav_create("bad",0,0,2,2,0));
 puts("PASS: truncated/overflow/nonfinite caches rejected before use; truncated large cache rejected before allocating its claimed payload");
 for(int fail=0;fail<5;fail++){
  int live=live_allocations;fail_after=fail;assert(!nav_create("OOM",0,0,8,8,1));fail_after=-1;assert(live_allocations==live);
 }
 g=flat_grid(12,12);
 for(int fail=0;fail<4;fail++){
  fail_after=fail;nav_set_path_owner(0);assert(!nav_find_path(g,flat_ray,.5f,0,.5f,10.5f,0,10.5f,out,128));nav_set_path_owner(-1);
  nav_process_paths(1);int n;
  nav_set_path_owner(0);n=nav_find_path(g,flat_ray,.5f,0,.5f,10.5f,0,10.5f,out,128);nav_set_path_owner(-1);
  assert(!n && strstr(nav_last_error(),"allocation failed") && !g_searchCost && !g_searchParent && !g_searchStamp && !g_searchHeap);
  fail_after=-1;
 }
 for(int fail=0;fail<4;fail++){
  fail_after=fail;assert(!nav_find_path_sync(g,flat_ray,.5f,0,.5f,10.5f,0,10.5f,out,128));
  assert(strstr(nav_last_error(),"allocation failed"));fail_after=-1;
 }
 nav_free(g);assert(!live_allocations);
 puts("PASS: every grid/queued/synchronous workspace allocation failure releases partial allocations and reports failure; tracked heap returns to zero");
 return 0;
}
'''
def main():
    p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');a=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='bf3-stability-nav-') as tmp:
        tmp=Path(tmp);(tmp/'windows.h').write_text(WINDOWS);(tmp/'check.c').write_text(HARNESS)
        cmd=['gcc','-std=c11','-Wall','-Wextra','-Werror','-g','-I',str(tmp),'-I',str(SRC),str(tmp/'check.c'),'-lm','-o',str(tmp/'check')]
        if a.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-sanitize-recover=all']
        subprocess.run(cmd,check=True)
        subprocess.run([str(tmp/'check'),str(ROOT/'files/server/BotMod_nav_XP3_Desert.bin'),str(tmp)],check=True)
if __name__=='__main__':main()
