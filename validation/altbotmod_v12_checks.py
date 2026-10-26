"""AltBotMod V12 checks: the route planner's soldier search (A* weight 1.2 with roads, was 0.9) on shipped navigation
grids - fewer nodes expanded for the same routes - and that route time is reported once in the perf line. Real nav.c /
nav_async.inc against the packaged BotMod_nav_* and BotMod_pave_* files (no BF3 physics: rays always clear).
Server Control's BF-Portable side is checked by validation/servercontrol/servercontrol_tests.ps1."""
from pathlib import Path
import argparse, re, shutil, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
SERVER = ROOT / 'files/server'
WINDOWS = r'''
#pragma once
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
typedef struct {long long QuadPart;} LARGE_INTEGER;
#define HEAP_ZERO_MEMORY 8
static void *GetProcessHeap(void){return (void*)1;}
static void *HeapAlloc(void *h,unsigned f,size_t n){(void)h;return f&8?calloc(1,n):malloc(n);}
static int HeapFree(void *h,unsigned f,void *p){(void)h;(void)f;free(p);return 1;}
static long long now_us(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000000LL+t.tv_nsec/1000;}
static unsigned GetTickCount(void){return (unsigned)(now_us()/1000);}
static int QueryPerformanceFrequency(LARGE_INTEGER *p){p->QuadPart=1000000;return 1;}
static int QueryPerformanceCounter(LARGE_INTEGER *p){p->QuadPart=now_us();return 1;}
'''
BENCH = r'''
#include <stdio.h>
#include <string.h>
#include "nav.c"
static int clear_ray(const float *a,const float *b,float *p,float *n){(void)a;(void)b;(void)p;(void)n;return 0;}
int main(int argc,char**argv){
 NavGrid *g=nav_load(argv[1]); if(!g)return 2;
 FILE *f=fopen(argv[2],"rb"); int hdr[3]; if(!f||fread(hdr,4,3,f)!=3)return 3;
 size_t n=(size_t)hdr[0]*hdr[1]*hdr[2]; unsigned char *mask=malloc(n); if(fread(mask,1,n,f)!=n)return 4; fclose(f);
 if(hdr[0]!=g->w||hdr[1]!=g->h)return 5;
 nav_set_road_mask(g,mask);
 srand(11); int nodes=g->w*g->h*L, done=0; float out[3*128];
 for(int t=0;t<20000 && done<25;t++){
  int a=rand()%nodes,b=rand()%nodes;
  if(!(g->flags[a]&NAV_WALKABLE)||!g->edges[a]||!(g->flags[b]&NAV_WALKABLE)||!g->edges[b]||((g->flags[a]|g->flags[b])&NAV_WATER))continue;
  float pa[3],pb[3];nav_node_pos(g,a,pa);nav_node_pos(g,b,pb);
  float d=hypotf(pa[0]-pb[0],pa[2]-pb[2]); if(d<200||d>700)continue;
  nav_set_path_owner(0); g_benchClosed=0; int got=0;
  for(int polls=0;polls<5000;polls++){ got=nav_find_path(g,clear_ray,pa[0],pa[1],pa[2],pb[0],pb[1],pb[2],out,128); if(!nav_last_deferred())break; nav_process_paths(1000);}
  float len=0,px=pa[0],pz=pa[2]; for(int k=0;k<got;k++){len+=hypotf(out[k*3]-px,out[k*3+2]-pz);px=out[k*3];pz=out[k*3+2];}
  printf("%d %ld %.1f\n",got>0,g_benchClosed,got?len:0.0f); done++;
 }
 return 0;
}
'''


def run_variant(tmp, weight, nav, pave):
    d = tmp / ('w' + weight.replace('.', '_'))
    d.mkdir(exist_ok=True)
    (d / 'windows.h').write_text(WINDOWS)
    for n in ['nav.c', 'nav.h']:
        shutil.copy(SRC / n, d / n)
    inc = (SRC / 'nav_async.inc').read_text()
    m = re.search(r'float hw=q->mask\?1\.5f:roads\?([\d.]+)f:1\.5f;', inc)
    assert m, 'soldier search weight line'
    inc = inc.replace(m.group(0), 'float hw=q->mask?1.5f:roads?%sf:1.5f;' % weight)
    inc = inc.replace('int searchLimit=q->mask?250000:120000;', 'int searchLimit=q->mask?250000:120000; g_benchClosed++;')
    (d / 'nav_async.inc').write_text('static long g_benchClosed;\n' + inc)
    (d / 'bench.c').write_text(BENCH)
    subprocess.run(['gcc', '-O2', '-w', '-I', str(d), str(d / 'bench.c'), '-lm', '-o', str(d / 'bench')], check=True)
    out = subprocess.run([str(d / 'bench'), str(nav), str(pave)], check=True, capture_output=True, text=True).stdout.split('\n')
    return [tuple(float(x) for x in l.split()) for l in out if l.strip()], m.group(1)


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); ap.parse_args()
    core = (SRC / 'core.c').read_text()
    assert 'perf_add(7,perf_now()-a);\n      tMark=perf_now(); }' in core, 'route time not counted twice'
    print('PASS: perf line - route planning time is reported once, under paths (V11 also added it to input+steer+gunner)')
    with tempfile.TemporaryDirectory(prefix='bf3-altbotmod-v12-') as tmp:
        tmp = Path(tmp)
        totals = [0, 0]
        for level in ['MP_007', 'XP3_Desert', 'MP_011']:
            nav, pave = SERVER / f'BotMod_nav_{level}.bin', SERVER / f'BotMod_pave_{level}.bin'
            old, _ = run_variant(tmp, '0.9', nav, pave)
            new, shipped = run_variant(tmp, '1.2', nav, pave)
            assert shipped == '1.2', shipped
            assert len(old) == len(new) >= 10, (len(old), len(new))
            assert all(o[0] == nw[0] for o, nw in zip(old, new)), 'a route found before is still found'
            co, cn = sum(o[1] for o in old), sum(n[1] for n in new)
            lo = sum(o[2] for o in old if o[0]); ln = sum(n[2] for n in new if n[0])
            assert cn <= co * 1.02, (level, co, cn)   # never more work on any map
            totals[0] += co; totals[1] += cn
            assert ln <= lo * 1.08, (level, lo, ln)
            print('PASS: %s - %d soldier routes: %d -> %d nodes expanded (%.0f%% fewer), total length %.0f -> %.0f m (%+.1f%%)'
                  % (level, len(new), co, cn, 100 * (1 - cn / co), lo, ln, 100 * (ln / lo - 1)))
        assert totals[1] < totals[0] * 0.7, totals
        print('PASS: all three maps together: %.0f%% fewer nodes expanded per soldier route' % (100 * (1 - totals[1] / totals[0])))


if __name__ == '__main__':
    main()
