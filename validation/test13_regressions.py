"""Reproduce Test12 control failures against its actual functions, then check fixes.
Engine seats, collision and terrain are mocked. No live BF3 validation is implied.
"""
from pathlib import Path
import argparse, os, subprocess, tempfile
from test4_checks import function
import driver_departure_checks as driver
import armor_traffic_checks as traffic
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
OLD = ROOT / 'rollback/Test12/src'

DRIVER_MAIN = driver.MAIN[:driver.MAIN.index('int main(void)')] + r'''
int main(void){
 for(int team=1;team<=2;team++){
  reset(team);DriveState*d=&g_drive[0];d->phase=2;d->boardAt=1;H->frame=1000;
  routeDeferred=1;H->botMove[0][0]=1;H->botMove[0][2]=0.7f;
  drive_tick(0);
  if(BASELINE){assert(H->botMove[0][0]==1 && H->botMove[0][2]==0.7f);continue;}
  assert(!H->botMove[0][0] && !H->botMove[0][2] && d->phase==2 && controlled[0] && !exits);
  routeDeferred=0;drive_tick(0);assert(d->phase==3);
  H->frame++;drive_tick(0);assert(H->botMove[0][0]>0.9f);
  // A reverse already in progress must execute before queued route work.
  reset(team);d=&g_drive[0];d->phase=2;d->boardAt=1;H->frame=1000;
  d->reverseUntil=H->frame+30;d->awayOn=1;d->awayHeading=1;routeDeferred=1;
  drive_tick(0);assert(d->phase==2 && H->botMove[0][0]<0 && H->botMove[0][2]>0 && corridorCalls);
  corridorClear=0;H->frame++;drive_tick(0);
  assert(!H->botMove[0][0] && !d->reverseUntil && controlled[0] && !exits);
  corridorClear=1;routeDeferred=0;H->frame++;drive_tick(0);assert(d->phase==3);
  // A waypoint directly behind (AltBotMod V25): it turns round on the spot - full lock, a little throttle, the route kept -
  // instead of V6-V24's reverse that dropped and re-planned the same route over and over.
  reset(team);d=&g_drive[0];d->phase=3;d->boardAt=1;d->n=1;
  d->pts[0]=12;d->pts[1]=77;d->pts[2]=-10;
  H->frame=1000;d->at=d->checkAt=H->frame;drive_tick(0);
  if(BASELINE)assert(H->botMove[0][0]<0 && d->reverseUntil>H->frame && controlled[0]);
  else assert(H->botMove[0][0]>0 && H->botMove[0][0]<=0.5f && (H->botMove[0][2]==1.0f || H->botMove[0][2]==-1.0f) && d->phase==3 && controlled[0]);
 }
 puts(BASELINE?"REPRODUCED: Test12 retains stale forward throttle/yaw while a vehicle route is deferred on both teams":
   "PASS: full native driver cancels stale inputs, resumes completed paths, executes checked reverse before queued planning and rejects blocked rear corridors on both teams");
 return 0;
}
'''

TRAFFIC_MAIN = traffic.MAIN[:traffic.MAIN.index('int main(void)')] + r'''
int main(void){
 for(int team=1;team<=2;team++){
  reset(team);float point[3]={0,77,10};narrowRoad=1;
  g_drive[0].trafficLeaseBackoff=H->frame+150;
  int stopped=traffic_lease(0,xf+12,point);
  assert(stopped==BASELINE);
  if(BASELINE)continue;
  // Bypassing virtual leases does not bypass a physically occupied lane.
  *(int*)(players[3]+8)=team;peerXf[14]=5;
  assert(traffic_speed(0,xf+12,xf,point,16)==0 && g_drive[0].trafficWait);
 }
 puts(BASELINE?"REPRODUCED: Test12 recovery traffic bypass creates an unconditional stop with no vehicle blocking it":
   "PASS: recovery bypass releases virtual road waits but still yields to real vehicles on both teams");
 return 0;
}
'''

FLIGHT = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "enhanced.h"
int main(void){
 BMConfig c;bm_defaults(&c);BMAirController s;BMAirOutput out;
 BMAirObservation o={0};o.position[1]=o.goal[1]=350;o.goal[0]=1000;
 o.right[0]=o.up[1]=o.forward[2]=1;o.velocity[2]=87;o.velocityValid=1;
 const BMAirProfile*p=bm_air_profile("F16");bm_air_start(&s,p->kind,1);
 assert(bm_air_step(&s,p,&c.air,&o,1.0f/30,&out));
 assert(BASELINE?out.yaw<0:out.yaw>0);
 // At constant world altitude, a bank toward the target still needs elevator.
 float bank=-0.6f;o.right[0]=cosf(bank);o.right[1]=sinf(bank);
 o.up[0]=-sinf(bank);o.up[1]=cosf(bank);
 assert(bm_air_step(&s,p,&c.air,&o,1.0f/30,&out));
 assert(BASELINE?fabsf(out.pitch)<0.001f:out.pitch<0);
 puts(BASELINE?"REPRODUCED: Test12 jet rudder opposes the supplied FunBots sign and provides no turn elevator for a level bank":
   "PASS: jet guidance follows FunBots rudder direction and converts finite world guidance into banked body-axis elevator");
 return 0;
}
'''

def main():
    parser = argparse.ArgumentParser();parser.add_argument('--sanitize', action='store_true');args = parser.parse_args()
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    with tempfile.TemporaryDirectory(prefix='bf3-test13-') as tmp:
        tmp = Path(tmp)
        for baseline in [1, 0]:
            source = OLD if baseline else SRC
            pre = driver.PRE.replace('static int tactic_segment(int i,const float*a,const float*b){(void)i;(void)a;(void)b;return 1;}',
                'static int corridorClear=1,corridorCalls;\nstatic int tactic_segment(int i,const float*a,const float*b){(void)i;(void)a;(void)b;corridorCalls++;return corridorClear;}')
            if not baseline:
                pre = pre.replace('static int drive_reverse_tick(int i,DriveState*d,const float*p){(void)i;(void)d;(void)p;return 0;}', '').replace('static int drive_unstick_tick(int i,DriveState*d,const float*p){(void)i;(void)d;(void)p;return 0;}', '')
                pre += (SRC/'drive_maneuvers.inc').read_text()
            driver_body = pre + (SRC/'drive_staging.inc').read_text() + function((source/'core.c').read_text(encoding='utf-8-sig'), 'drive_tick') + DRIVER_MAIN
            bodies = {'driver': driver_body, 'traffic': traffic.pre + traffic.extra + (source/'ground_tactics_native.inc').read_text() + TRAFFIC_MAIN, 'flight': FLIGHT}
            for name, body in bodies.items():
                file = tmp/f'{name}-{baseline}.c';file.write_text(body);exe = file.with_suffix('')
                command = ['gcc', '-std=c11', '-Wall', '-Wextra', '-Wno-unused-function', '-Wno-unused-variable', f'-DBASELINE={baseline}', '-I', str(SRC), str(file), str(SRC/'enhanced.c'), str(source/'air.c'), '-lm', '-o', str(exe)]
                if args.sanitize:command[1:1]=['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
                subprocess.run(command, check=True);subprocess.run([str(exe)], check=True, env=env)
    # The actual idle-replan expression must be compatible with both cadence halves.
    old = (OLD/'core.c').read_text(encoding='utf-8-sig');new = (SRC/'core.c').read_text()
    assert '(H->frame % 30 == 0 && !b->enroute && !b->waiting)' in old
    assert '((H->frame+(unsigned)i) % 30 == 0 && !b->enroute && !b->waiting)' in new
    for i in range(64):
        ticks = [f for f in range(1,61) if (f+i)%2==0]
        assert sum((f+i)%30==0 for f in ticks)==2
        if i%2:assert not any(f%30==0 for f in ticks)
    print('PASS: idle replans reach every bot slot under the actual alternating 15-Hz brain schedule')

if __name__=='__main__':main()
