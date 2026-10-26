"""Caspian Test16: recovery orders versus the objective director, and the
vehicle-queue timeout. Compiles the ACTUAL production functions (director_tick,
activity_replan, local-order selection, fleet_tick) against the shared mocked
engine fixture. Each check is built twice: once against the Test15 sources
(BASELINE=1) to reproduce the bug, once against the current sources.
These are not live BF3 tests."""
from pathlib import Path
import argparse, os, re, subprocess, tempfile
from test4_checks import function
import test15_regressions as t15
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT/'files/BotMod/src'; OLD = ROOT/'rollback/Test15/src'

MAIN = r'''
static int pick(int i,int *role,int *defend)
{
#if BASELINE
    // Test15 brain: the director first, local_objective() never consulted.
    int f=director_target(i,role);*defend=*role==BM_OBJECTIVE_GUARD;
    if(f<0)f=squad_objective(player_team(H->bots[i]),1);
    return f;
#else
    return brain_pick_objective(i,player_team(H->bots[i]),1,role,defend);
#endif
}
static void setup(int team)
{
    reset("M1Abrams",3);g_roundEpoch++;H->botCount=8;g_flagCount=3;H->frame=10000;
    memset(g_activity,0,sizeof g_activity);memset(g_noRouteSince,0,sizeof g_noRouteSince);
    float side=team==1?-1.f:1.f;
    // flag 0: enemy, close to this team's base; flag 1: enemy, far; flag 2: owned, mid-map.
    flagOwners[0]=flagOwners[1]=3-team;flagOwners[2]=team;
    flags[0][0]=side*300;flags[0][2]=0;flags[1][0]=-side*500;flags[1][2]=0;flags[2][0]=0;flags[2][2]=400;
    for(int i=0;i<8;i++){
        int t=i<4?team:3-team;float s=t==team?side:-side;
        H->bots[i]=players[i];H->botInput[i]=inputs[i];*(int*)(players[i]+8)=t;
        *(float**)(players[i]+OFF_PLAYER_SOLDIER)=soldiers[i];
        soldiers[i][0]=s*600+(float)i;soldiers[i][1]=0;soldiers[i][2]=0;soldiers[i][3]=100;
        strcpy(players[i]+OFF_PLAYER_ONLINE_NAME,"test bot");
        brains[i].enabled=1;brains[i].targetFlag=-1;brains[i].avoidFlag=-1;
    }
}
int main(void)
{
    for(int team=1;team<=2;team++){
        int role,defend,f;
        // 1. Director gives bot 0 an attack, it stalls, the watchdog recovers it.
        setup(team);director_tick();
        int stuck=director_target(0,&role);assert(stuck>=0 && role==BM_OBJECTIVE_ATTACK);
        brains[0].targetFlag=stuck;
#if !BASELINE
        // AltBotMod V6: held up far from the flag (at its HQ) - the flag is NOT struck off
        activity_replan(0);assert(!brains[0].avoidUntil);
        director_tick();brains[0].targetFlag=stuck;
        // ...stuck near it: avoided, as before
        soldiers[0][0]=flags[stuck][0]+60;soldiers[0][2]=flags[stuck][2];
#endif
        activity_replan(0);
        int order=g_localFlag[0]-1;
#if BASELINE
        H->frame+=30;director_tick();f=pick(0,&role,&defend);
        assert(f==stuck);   // Test15: the recovery is overridden; back to the same flag
        continue;
#else
        assert(order>=0 && order!=stuck);
        assert(brains[0].avoidFlag==stuck && brains[0].avoidUntil>H->frame);
        brains[0].targetFlag=order;   // the brain starts routing to the order
        for(int k=0;k<30;k++){
            H->frame+=30;director_tick();
            assert(g_localFlag[0]==order+1 && brains[0].targetFlag==order);
            f=pick(0,&role,&defend);assert(f==order && role==BM_OBJECTIVE_NONE);
        }
        // 2. Expiry: the director takes over again, not to the flag still avoided.
        H->frame=g_localUntil[0]+1;director_tick();f=pick(0,&role,&defend);
        assert(!g_localFlag[0] && f>=0 && f!=stuck);
        // 3. Capturing the ordered flag ends the order early.
        setup(team);director_tick();stuck=director_target(0,&role);brains[0].targetFlag=stuck;
        soldiers[0][0]=flags[stuck][0]+60;soldiers[0][2]=flags[stuck][2];   // (V6: stuck near it)
        activity_replan(0);order=g_localFlag[0]-1;assert(order>=0 && g_localOrder[0].attack);
        flagOwners[order]=team;H->frame+=30;f=pick(0,&role,&defend);assert(!g_localFlag[0] && f!=order);
        // 4. A new life (respawn) ends it.
        setup(team);director_tick();brains[0].targetFlag=director_target(0,&role);activity_replan(0);
        assert(g_localFlag[0]);g_lastSpawnedAt[0]=H->frame+1;H->frame+=2;pick(0,&role,&defend);assert(!g_localFlag[0]);
        // 5. A team change ends it.
        setup(team);director_tick();brains[0].targetFlag=director_target(0,&role);activity_replan(0);
        assert(g_localFlag[0]);*(int*)(players[0]+8)=3-team;H->frame+=30;assert(!local_order_active(0));
        pick(0,&role,&defend);assert(!g_localFlag[0]);
        // 6. Ordinary local orders (out of a vehicle, failed vehicle route, GoGoGo) are obeyed too.
        setup(team);director_tick();choose_local_objective(1,"vehicle route unavailable; advances on foot");
        order=g_localFlag[1]-1;assert(order>=0);brains[1].targetFlag=order;
        for(int k=0;k<10;k++){H->frame+=30;director_tick();assert(g_localFlag[1]==order+1 && brains[1].targetFlag==order);
            f=pick(1,&role,&defend);assert(f==order);}
        // 7. Only flag left is the stuck one: still gets a (last-resort) order instead of nothing.
        setup(team);g_flagCount=1;flagOwners[0]=3-team;director_tick();
        stuck=director_target(0,&role);assert(stuck==0);brains[0].targetFlag=0;activity_replan(0);
        assert(g_localFlag[0]==1 && brains[0].avoidFlag==-1);
#endif
    }
#if BASELINE
    puts("REPRODUCED: Test15 director overrides recovery orders and sends the bot back to the flag it was stuck on (both teams)");
#else
    puts("PASS: recovery orders win over the director on BOTH teams, avoid the stalled flag, and end on expiry, capture, respawn or team change; the director then resumes without the stalled flag");
#endif
    return 0;
}
'''

FLEET = r'''
int main(void)
{
    NavGrid ready={300,300,2,10,-1500,-1500};
    for(int team=1;team<=2;team++){
        reset("M1Abrams",3);g_enhanced.returnToBase=g_enhanced.armored=1;grid=&ready;
        for(int i=0;i<4;i++){*(int*)(players[i]+8)=team;}vehicle.team=team;
        assert(fleet_reserve(0,soldiers[0]));
        // The route search for bot 0 never completes (planner busy / grid building).
        g_fleet[0].queuedAt=H->frame-31*30;g_fleet[0].until=H->frame+300;fleet_tick(0);
#if BASELINE
        assert(!g_fleet[0].vehicle && fleet_route_blocked((char*)&vehicle,team,soldiers[1]));
#else
        assert(!g_fleet[0].vehicle && g_fleet[0].retryAt>H->frame && g_fleet[0].failedVehicle==(char*)&vehicle);
        // Teammates at the same base are not locked out of the vehicle.
        assert(!fleet_route_blocked((char*)&vehicle,team,soldiers[1]) && fleet_reserve(1,soldiers[1]));
        // Bounded at 20 s rather than 30 s; still waiting at 19 s.
        reset("M1Abrams",3);g_enhanced.returnToBase=g_enhanced.armored=1;grid=&ready;
        for(int i=0;i<4;i++){*(int*)(players[i]+8)=team;}vehicle.team=team;
        vehicle.xf[12]=100;assert(fleet_reserve(0,soldiers[0]));budgetAvailable=0;
        g_fleet[0].queuedAt=H->frame-19*30;fleet_tick(0);assert(g_fleet[0].vehicle);
        g_fleet[0].queuedAt=H->frame-21*30;fleet_tick(0);assert(!g_fleet[0].vehicle);
#endif
    }
#if BASELINE
    puts("REPRODUCED: Test15 queue timeout (never more than a busy planner) blocks the vehicle for every teammate at the base");
#else
    puts("PASS: a vehicle-queue timeout (now 20 s) releases the waiting bot without blocking the vehicle for teammates at the same base (both teams)");
#endif
    return 0;
}
'''

CADENCE = r'''
int main(void){
 for(int team=1;team<=2;team++){
  fresh(team);replans=0;blockedGeometry=1;activity_tick(0);H->frame+=18*30;activity_tick(0);
  int first=replans;assert(first==1);
  for(int k=0;k<9;k++){H->frame+=3*30;activity_tick(0);}   // 27 s more, still stuck, no exit found
#if BASELINE
  assert(replans==first+9);   // Test15: a new objective (and a cancelled route) every 3 s
#else
  assert(replans==first);     // local exit probes continue, the route is left alone
  H->frame+=3*30;activity_tick(0);assert(replans==first+1);   // 30 s later: one more re-plan
#endif
 }
#if BASELINE
 puts("REPRODUCED: Test15 activity watchdog re-plans a stuck bot's objective every 3 s, cancelling its route each time (both teams)");
#else
 puts("PASS: activity watchdog re-plans a stuck bot's objective once, then every 30 s; 3 s local exit probes continue (both teams)");
#endif
 return 0;
}
'''

def fixture():
    base = (ROOT/'validation/air_native_tests.c').read_text().split('int main(void)')[0]
    base = base.replace('#include "../files/BotMod/src/', '#include "')
    base = base.replace('#define MAX_BOTS 8', '#define MAX_BOTS 32').replace('vehicles[8]', 'vehicles[32]')
    base = re.sub(r'^static int director_target\([^\n]*', 'static int director_target(int i,int*role);', base, flags=re.M)
    base = base.replace('int enabled,task,prevTask,targetFlag,', 'unsigned taskUntil,stepAt;int crouched;int enabled,task,prevTask,targetFlag,')
    base = base.replace('static struct{int count,next;}g_paths[MAX_BOTS];', 'static struct{int count,next,partial;}g_paths[MAX_BOTS];')
    return base

def body(source, kind):
    if kind == 'cadence':
        pre = t15.ACTIVITY.split('int main(void){')[0]
        if source == OLD:
            pre = pre.replace('#include "activity_state.h"', '#include "%s"' % (OLD/'activity_state.h')).replace('#include "activity_native.inc"', '#include "%s"' % (OLD/'activity_native.inc'))
        b = t15.base.replace('static void choose_local_objective(int i,const char *why) { (void)why; brains[i].targetFlag=0; }',
                             'static int replans;static void choose_local_objective(int i,const char *why) { (void)why; replans++; brains[i].targetFlag=0; }')
        assert 'replans++' in b
        return b + pre + CADENCE
    base = fixture()
    if kind == 'fleet':
        if source == OLD:   # Test15 fleet code, with the Test15 mocks
            base = base.replace('#include "fleet_native.inc"', '#include "%s"' % (OLD/'fleet_native.inc'))
        return base + '\n#include "director_policy.h"\nstatic int director_target(int i,int*role){(void)i;*role=0;return -1;}\n' + FLEET
    # Real local-order functions replace the fixture's mocks.
    base = re.sub(r'^static void choose_local_objective\(int i,const char \*why\) \{[^\n]*\n', 'static void choose_local_objective(int i,const char *why);\n', base, flags=re.M)
    base = re.sub(r'^static (?:inline )?void choose_local_objective_except\([^\n]*\n', 'static void choose_local_objective_except(int i,const char *why,int exclude);\n', base, flags=re.M)
    core = (source/'core.c').read_text(encoding='utf-8-sig')
    defines = '\n'.join(re.findall(r'^#define (?:LOCAL_OBJECTIVE_FRAMES|RECOVERY_\w+)[^\n]*', core, re.M))
    names = ['choose_local_objective'] if source == OLD else ['choose_local_objective_except', 'choose_local_objective', 'clear_local_objective', 'local_objective', 'brain_pick_objective']
    extra = '''
#include "director_policy.h"
static int g_overflowFlag[MAX_BOTS];static unsigned g_noRouteSince[MAX_BOTS];
static float*human_pos(char*p){return (float*)*(char**)(p+OFF_PLAYER_SOLDIER);}
static void combat_motion_cancel(int i){(void)i;}
static int squad_order_flag(int t,int q,int*d){(void)t;(void)q;(void)d;return -1;}   /* AltBotMod V34 */
static void bot_stand(int i){(void)i;}
typedef struct { unsigned progressAt; } ActivityStub; static ActivityStub g_activity[MAX_BOTS];
'''
    director = (source/'director_native.inc').read_text()
    replan = function((source/'activity_native.inc').read_text(), 'activity_replan')
    funcs = '\n'.join(function(core, n) for n in names)
    return base + extra + defines + '\n' + director + '\n' + funcs + '\n' + replan + '\n' + MAIN

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    with tempfile.TemporaryDirectory(prefix='bf3-test16-') as tmp:
        for kind in ['orders', 'fleet', 'cadence']:
            for baseline in [1, 0]:
                source = OLD if baseline else SRC
                p = Path(tmp)/f'{kind}-{baseline}.c'; p.write_text(body(source, kind)); exe = p.with_suffix('')
                cmd = ['gcc', '-std=c11', '-Wall', '-Wextra', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-parameter',
                       f'-DBASELINE={baseline}', '-I', str(SRC), str(p), *[str(SRC/n) for n in ['enhanced.c', 'air.c', 'modes.c']], '-lm', '-o', str(exe)]
                if a.sanitize: cmd[1:1] = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
                subprocess.run(cmd, check=True); subprocess.run([str(exe)], check=True, env=env)

if __name__ == '__main__': main()
