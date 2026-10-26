"""Real drive_tick exhaustion/corner branches. Engine physics is mocked."""
from pathlib import Path
import argparse, os, subprocess, tempfile
from driver_departure_checks import PRE, MAIN, core, SRC
from test4_checks import function

pre=PRE
for name in ('drive_wait_route','drive_recover_job','drive_block_for_job'):
    pre='\n'.join(line for line in pre.splitlines() if not line.startswith('static '+('void' if name=='drive_wait_route' else 'int')+' '+name+'('))
jobs=(SRC/'ground_jobs.inc').read_text()
actual='\n'.join(function(jobs,n) for n in ('drive_wait_route','drive_block_for_job','drive_recover_job'))
reset=MAIN[:MAIN.index('int main(void)')]
main=r'''
static void road(int team){
 reset(team);DriveState*d=&g_drive[0];d->phase=3;d->boardAt=1;d->n=3;d->next=1;
 d->pts[1*3+1]=77;d->pts[1*3+2]=10;
 d->pts[2*3]=10;d->pts[2*3+1]=77;d->pts[2*3+2]=10;
 d->at=H->frame;d->checkAt=H->frame;
}
int main(void){
 for(int team=1;team<=2;team++){
  road(team);xf[14]=3.0f;drive_tick(0);assert(g_drive[0].next==1&&controlled[0]&&!exits);   /* (V24: a waypoint is passed within 6 m - 7 m short of it still heads for it) */
  xf[14]=9;H->frame++;drive_tick(0);assert(g_drive[0].next==2);
  // All three old auto-dismount branches now retain the healthy driver.
  road(team);H->frame=1000;g_drive[0].checkAt=1;g_drive[0].stuckCount=4;
  drive_tick(0);assert(g_drive[0].phase==10&&controlled[0]&&!stops&&!exits&&!H->botMove[0][0]);
  H->frame=g_drive[0].routeRetryAt;drive_tick(0);assert(g_drive[0].phase==3&&controlled[0]&&!exits);
  road(team);H->frame=1000;g_drive[0].checkAt=1;g_drive[0].stuckCount=1;g_drive[0].reroutes=2;
  drive_tick(0);assert(g_drive[0].phase==10&&controlled[0]&&!stops&&!exits);
  road(team);DriveState*d=&g_drive[0];H->frame=1000;d->checkAt=H->frame;
  d->sensorAt=H->frame;d->frontNear=d->midNear=1;d->sideL=1;d->sideR=5;
  d->boxedSince=H->frame-16;d->nudges=1;d->nudgePos[1]=77;d->reroutes=2;
  drive_tick(0);assert(d->phase==10&&controlled[0]&&!stops&&!exits);
  unsigned char original[16];memcpy(original,mask,16);
  assert(drive_block_for_job(0,mask,0,4,5,77)==1);
  assert(!memcmp(original,mask,16)&&d->avoidRadius==2.25f&&d->avoidUntil==H->frame+900);
  assert(!g_drive[1].avoidUntil&&!g_drive[1].avoidRadius);
 }
 puts("PASS: both teams retain healthy ground drivers through all three exhausted stuck/re-route branches, retry checked routing, and keep obstruction learning local to the affected driver");
 puts("PASS: actual drive_tick heads for a right-angle waypoint 7 m off and passes it within 6 m (V24 pure pursuit; V6-V23 chased it to 1.75 m)");return 0;
}
'''
def main_run():
    p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');a=p.parse_args()
    body=pre+'\n'+actual+'\n'+(SRC/'drive_staging.inc').read_text()+'\n'+function(core,'drive_tick')+'\n'+reset+main
    with tempfile.TemporaryDirectory(prefix='bf3-ground-recovery-') as tmp:
        f=Path(tmp)/'checks.c';f.write_text(body);exe=f.with_suffix('')
        cmd=['gcc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-but-set-variable','-Wno-unused-function','-I',str(SRC),str(f),str(SRC/'enhanced.c'),str(SRC/'air.c'),'-lm','-o',str(exe)]
        if a.sanitize:cmd[1:1]=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']
        subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
if __name__=='__main__':main_run()
