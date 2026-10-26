"""Reproduce old HQ rejects, then test real recovery/control code with mocks.
No assertion here represents live BF3 physics, replication or latency testing.
"""
from pathlib import Path
import argparse,subprocess,tempfile,os
from test4_checks import function
import hq_navigation_checks as hq
import driver_departure_checks as driver
from nav_queue_checks import WINDOWS,HARNESS
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src'
# This baseline fragment is kept in the release, not read from a developer's
# working checkout, so the old-bug reproduction remains portable.
old=(ROOT/'validation/test11-hq-discovery.fragment').read_text()
new=function((SRC/'core.c').read_text(),'find_hq_spawns').replace('+0x14','+0x18')
HQ_PRE=hq.HQ[:hq.HQ.index('int main(void)')]
OLD_HQ=HQ_PRE.replace(new,old)+r'''
int main(void){float lo[3]={1e9f,0,1e9f},hi[3]={-1e9f,0,-1e9f};
 init();*(void**)(spawn[2]+32)=(void*)101;*(int*)(data[2]+0x6C)=1;*(int*)(data[5]+0x6C)=1;
 find_hq_spawns(lo,hi);assert(g_hqCpKnown[2] && !H->teamSpawn[2].entity);
 init();*(int*)(data[2]+0x6C)=0;*(int*)(data[5]+0x6C)=1;
 find_hq_spawns(lo,hi);assert(g_hqCpKnown[2] && !H->teamSpawn[2].entity);
 puts("REPRODUCED: Test11 discovers RU HQ but rejects its generic/neutral children, leaving scoreboard players without soldiers until a human supplies a spawn");return 0;}
'''
# Full actual 15s watchdog, sharing exactly the driver's real state layout.
pre=driver.PRE.replace('static int drive_progress_tick(int i,DriveState*d,const float*p){(void)i;(void)d;(void)p;return 0;}','').replace('static int drive_goal_watch(int i,DriveState*d,const float*p){(void)i;(void)d;(void)p;return 0;}   /* AltBotMod V17 */','')
RECOVERY=pre+r'''
static int directCalls,allowExit;
static int tactic_probe_budget(void){return 1;}
static int nav_is_water(const NavGrid*g,float x,float y,float z){(void)g;(void)x;(void)y;(void)z;return 0;}
static void nav_set_path_owner(int i){(void)i;}
static int nav_direct_route(const NavGrid*g,int(*r)(const float*,const float*,float*,float*),const float*a,const float*b){
 (void)g;(void)r;(void)a;directCalls++;return allowExit && b[0]>3; // straight ahead is blocked; side exit clear
}
'''+(SRC/'ground_progress.inc').read_text()+driver.MAIN[:driver.MAIN.index('int main(void)')]+r'''
int main(void){
 for(int team=1;team<=2;team++){
  reset(team);DriveState*d=&g_drive[0];d->phase=10;d->goal[0]=0;d->goal[2]=100;
  float p[3]={0,77,0};allowExit=1;directCalls=0;
  assert(!drive_progress_tick(0,d,p));H->frame+=449;assert(!drive_progress_tick(0,d,p)&&!directCalls);
  H->frame++;assert(drive_progress_tick(0,d,p) && d->phase==3 && d->n==1 && d->partial);
  assert(directCalls>=2 && d->pts[0]>3 && d->goal[2]==100 && controlled[0] && !exits);
  assert(d->trafficLeaseBackoff>H->frame && !d->pickupUntil);
  H->frame+=450;allowExit=0;assert(!drive_progress_tick(0,d,p));assert(d->phase==2 && !exits && controlled[0]);
  // A genuinely moving vehicle never receives a recovery command.
  H->frame+=450;p[0]+=4;assert(!drive_progress_tick(0,d,p));assert(d->progressAt==H->frame);
 }
 puts("PASS: BOTH teams recover after exactly 15s of actual immobility; blocked forward exit selects a checked side exit, retains crew and enemy objective; fully obstructed exits request routing instead of blind throttle");return 0;}
'''
# Physics block absent from the coarse nav grid. A returned prefix must never
# cross it. After moving to that prefix, the learned edge selects another route.
NAV=HARNESS[:HARNESS.index('int main(void)')].replace('wall_x+0.5f','wall_x+0.25f')+r'''
int main(void){
 float s[3]={10.5f,0,40.5f},t[3]={70.5f,0,40.5f},out[128*3];
 NavGrid*g=flat(90,90,-1,0);unsigned char*mask=calloc((size_t)g->w*g->h*L,1);
 for(int u=0;u<g->w*g->h;u++)mask[u*L]=1;
 nav_set_path_mask(mask);nav_set_vehicle_width(1.9f);wall_x=40;wall_gap=25;
 int completed=0,legs=0;
 while(!completed && legs++<20){
  assert(!poll(64,g,s,t,out,128));int n=0;
  for(int k=0;k<20000 && !n;k++){nav_process_paths(.25);n=poll(64,g,s,t,out,128);if(!n && !nav_last_deferred())break;}
  assert(n>0);float a[3];memcpy(a,s,12);
  for(int k=0;k<n;k++){assert(nav_segment_clear(test_ray,a,out+k*3));memcpy(a,out+k*3,12);}
  fprintf(stderr,"leg=%d n=%d partial=%d from=(%.1f,%.1f) to=(%.1f,%.1f) %s\n",legs,n,nav_last_partial(),s[0],s[2],a[0],a[2],nav_last_error());
  completed=!nav_last_partial();assert(hypotf(a[0]-s[0],a[2]-s[2])>4);memcpy(s,a,12);
 }
 assert(completed && legs>1 && hypotf(s[0]-t[0],s[2]-t[2])<.1);
 printf("PASS: a coarse-grid route with a real physics obstruction advances along its clear prefix, then replans around the wall to the enemy goal in %d checked legs\n",legs);
 nav_set_path_mask(NULL);free(mask);nav_free(g);return 0;}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');a=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='bf3-test12-') as d:
  tmp=Path(d);(tmp/'windows.h').write_text(WINDOWS)
  for name,body in [('old-hq',OLD_HQ),('driver-recovery',RECOVERY),('nav-obstacle',NAV)]:
   f=tmp/(name+'.c');f.write_text(body);exe=f.with_suffix('')
   cmd=['gcc','-std=c11','-Wall','-Wextra','-Wno-unused-function','-Wno-unused-variable','-I',str(tmp),'-I',str(SRC),str(f)]
   if name=='driver-recovery':cmd += [str(SRC/'enhanced.c'),str(SRC/'air.c')]
   cmd+=['-lm','-o',str(exe)]
   if a.sanitize:cmd[1:1]=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']
   subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
if __name__=='__main__':main()
