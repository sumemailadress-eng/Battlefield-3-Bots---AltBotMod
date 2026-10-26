#!/usr/bin/env python3
"""AltBotMod V34 - Squad Up: offline checks.

1. Fun-Bots' objective walking mesh (zone_nav.c, Fun-Bots 3.6.0-dev navzones): the shipped .zon files are exactly what the
   converter makes from Fun-Bots' JSON; zone_nav.c loads all 97, routes inside every zone (ASan/UBSan), keeps one-way
   jumps one way, closes a connection after 3 stuck reports; every Rush M-COM of every Rush map lies in a mesh M-COM zone
   and 217 of 218 are reachable over the mesh from most of their zone.
2. M-COMs behind a window: the operator knifes the pane between it and its M-COM (walks up to it first when it is further
   in and the bot gets no closer), shoots it when the knife did nothing; a wall is never taken for a pane - the real
   panel_dir / mcom_pane_tick / breach_tick against a small world. "Unbreakable" is learned only after a second failure.
3. Standing still / circling (V33 Caspian Border log): mid-round walks to vehicles capped (VehicleWalkMeters 250, 40 m with
   the objective close), a supply run that gave up is not sent to the same caller again for 45 s, a combat relocation
   that waited for the planner is checked again from where the bot is.
4. Air: a stalled jet recovers (nose down, wings level, full power) - the real bm_air_step; flares / smoke on a lock on the
   vehicle (the game's locking controllers); jets keep their heat seekers for aircraft; an enemy attack helicopter is a
   jet's job; the jet area is wider (900 m margin, 1400 m minimum).
5. Squad orders (!attack / !defend / !orders, the commo rose's Attack), bot leaders' announcements; the settings in
   Server Control; version.
"""
from pathlib import Path
import argparse, subprocess, tempfile, re, os, filecmp, sys
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
SC = ROOT / 'files/ServerControl'
ZON = ROOT / 'files/server/BotMod_traces/navzones'
VAL = ROOT / 'validation'
FUNBOTS_NAVZONES = Path('/home/claude/joe91/fun-bots/navzones')

def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, env={'ASAN_OPTIONS': 'detect_leaks=1', 'PATH': '/usr/bin:/bin'}, **kw)
    if r.returncode: raise SystemExit('FAILED: ' + ' '.join(map(str, cmd)) + '\n' + r.stdout + r.stderr)
    return r.stdout

def san(flag): return ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'] if flag else []

def check_mesh(sanitize):
    files = sorted(ZON.glob('*.zon'))
    assert len(files) == 97, len(files)
    if FUNBOTS_NAVZONES.is_dir():   # (the Fun-Bots checkout is on the build machine only)
        with tempfile.TemporaryDirectory() as td:
            run([sys.executable, str(VAL / 'tools/navzones_convert.py'), str(FUNBOTS_NAVZONES), td])
            same = [f.name for f in files if filecmp.cmp(f, Path(td) / f.name, shallow=False)]
            assert len(same) == len(files), set(f.name for f in files) - set(same)
        print('PASS: the 97 shipped mesh files are exactly what the converter makes from Fun-Bots 3.6.0-dev navzones')
    with tempfile.TemporaryDirectory() as td:
        exe = Path(td) / 'zt'
        run(['gcc', '-std=gnu11', '-O1', '-Wall', '-Wextra', '-Werror', *san(sanitize), '-I', str(SRC), str(VAL / 'v34_zone_tests.c'), str(SRC / 'zone_nav.c'), '-lm', '-o', str(exe)])
        print(run([str(exe), str(ZON)]).strip().replace('\n', ' | '))
        # every Rush M-COM in AltBotMod's mode profiles against the meshes
        (Path(td) / 'mc.c').write_text(MCOMS)
        run(['gcc', '-std=gnu11', '-O1', '-w', '-I', str(SRC), str(Path(td) / 'mc.c'), '-o', str(Path(td) / 'mc')])
        (Path(td) / 'mcoms.txt').write_text(run([str(Path(td) / 'mc')]))
        (Path(td) / 'reach.c').write_text(REACH)
        run(['gcc', '-std=gnu11', '-O1', '-Wall', *san(sanitize), '-I', str(SRC), str(Path(td) / 'reach.c'), str(SRC / 'zone_nav.c'), '-lm', '-o', str(Path(td) / 'reach')])
        out = run([str(Path(td) / 'reach'), str(Path(td) / 'mcoms.txt'), str(ZON)])
        m = re.search(r'M-COMs (\d+), in a mesh M-COM zone (\d+), with a mesh point within 10 m (\d+), reachable from most of their zone (\d+)', out)
        assert m and int(m.group(1)) == 218 and int(m.group(2)) == 218 and int(m.group(3)) == 218 and int(m.group(4)) >= 217, out
        print('PASS: ' + m.group(0) + ' (all Rush and Squad Rush maps)')
    core = (SRC / 'core.c').read_text(); zn = (SRC / 'zone_native.inc').read_text()
    assert 'int zr = zone_plan(i, soldier, tx, ty, tz);' in core and core.index('int zr = zone_plan(') < core.index('int traced = trace_plan(i, soldier, tx, ty, tz);')
    assert 'zone_stuck(i, pos);' in core and 'zone_level_load();   // AltBotMod V34' in core and 'zone_guard_point(goal,' in (SRC / 'modes_native.inc').read_text()
    assert 'BotMod_traces\\\\navzones\\\\%s.zon' in zn and 'if (w < 0) { g_lastPlanDeferred = 1; return 0; }' in zn
    print('PASS: near an objective the bots plan over the mesh first (then Fun-Bots routes, then the grid); stuck reports cost the mesh connection; Rush guard posts from the M-COM zone')

MCOMS = r'''
#include <stdio.h>
#include <string.h>
typedef struct BMModeProfile {
 const char *level,*mode; float low[3],high[3],patrol[32][3]; int stages;
 unsigned baseMask[9];float bases[9][3][3]; struct {float pos[3],yaw,pitch;int present;}mcom[17];
} BMModeProfile;
#include "mode_profiles.inc"
int main(void){ for(unsigned k=0;k<sizeof g_modeProfiles/sizeof g_modeProfiles[0];k++){const BMModeProfile*p=&g_modeProfiles[k]; if(!strstr(p->mode,"Rush"))continue;
  for(int m=1;m<=16;m++) if(p->mcom[m].present) printf("%s %s %d %.2f %.2f %.2f\n",p->level,p->mode,m,p->mcom[m].pos[0],p->mcom[m].pos[1],p->mcom[m].pos[2]);} return 0;}
'''
REACH = r'''
#include "zone_nav.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char**argv){ (void)argc; FILE*f=fopen(argv[1],"r"); char lv[64],md[64]; int m; float p[3]; int total=0,inzone=0,goal=0,reach=0; char cur[300]="";
 ZoneNet z; memset(&z,0,sizeof z); static float out[256*3];
 while(fscanf(f,"%63s %63s %d %f %f %f",lv,md,&m,&p[0],&p[1],&p[2])==6){ char key[300]; snprintf(key,sizeof key,"%s/%s_%s.zon",argv[2],lv,md);
  if(strcmp(key,cur)){ zn_free(&z); cur[0]=0; if(!zn_load(&z,key))continue; strcpy(cur,key);} total++;
  int zi=zn_zone_near(&z,p,ZN_KIND_MCOM,15); if(zi>=0)inzone++;
  int g=zn_closest(&z,p,10,3.5f); if(g<0)continue; goal++;
  int ok=0,tried=0;
  for(int k=0;zi>=0&&k<z.zones[zi].ninside;k++){int a=z.inside[z.zones[zi].inside0+k]; if(a==g)continue; tried++; int part; float len; if(zn_route(&z,a,g,out,NULL,256,&part,&len)>0)ok++;}
  if(tried && ok*2>=tried) reach++;
 } zn_free(&z); fclose(f);
 printf("M-COMs %d, in a mesh M-COM zone %d, with a mesh point within 10 m %d, reachable from most of their zone %d\n",total,inzone,goal,reach); return 0;}
'''

PANE = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define MAX_BOTS 2
#define OFF_PLAYER_ONLINE_NAME 0
#define OFF_SOLDIER_POSITION 0
#define OFF_SOLDIER_YAW 16
#define EIA_FIRE 7
#define EIA_FIRE_KNIFE 8
#define SLOT_ACTION(n) (100 + (n))
static struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; float botMove[MAX_BOTS][3]; } host, *H = &host;
typedef struct { int mode; float targetYaw, x, z; } BotNav;
static BotNav navs[MAX_BOTS];
static BotNav *bot_nav(int i) { return &navs[i]; }
static unsigned g_fenceUntil[MAX_BOTS], g_rayFlags;
static float yaw_towards(float dx, float dz) { return atan2f(dx, dz); }
static float dist2(const float *a, const float *b) { float x = a[0] - b[0], z = a[2] - b[2]; return x * x + z * z; }
static char *player_vehicle(char *pl) { (void)pl; return NULL; }
static void bot_log(const char *f, ...) { (void)f; }
static void nav_cancel_path(int i) { (void)i; }
static unsigned g_unbreakMat[64]; static int g_unbreakN; static unsigned learnedBreaks;
static int material_listed(const unsigned *l, int n, unsigned m) { for (int k = 0; k < n; k++) if (l[k] == m) return 1; return 0; }
static void breakable_learn(unsigned m, int b) { (void)m; if (b) learnedBreaks++; }
static unsigned hit_material(void) { return 0x0900abcdu; }
/* the world: a floor at y = 0; a pane (2 cm) or a wall (60 cm) across x = 2 from y 0 to 2.5 */
static int pane = 1, wallThick = 0, knifeBreaks = 1, shotBreaks = 1, knifes, shots;
static float sx[8];   /* the soldier: position 0..2, yaw at 4 */
static int ray_cast(const float *a, const float *b, float *hit, float *n) {
    (void)n;
    if (a[1] > 0 && b[1] <= 0 && fabsf(a[0] - b[0]) < 1e-3f && fabsf(a[2] - b[2]) < 1e-3f) { if (hit) { hit[0] = a[0]; hit[1] = 0; hit[2] = a[2]; } return 1; }
    if (!pane) return 0;
    float x0 = 2.0f, x1 = wallThick ? 2.6f : 2.02f;
    for (int face = 0; face < 2; face++) {
        float x = (a[0] <= b[0]) ? (face ? x1 : x0) : (face ? x0 : x1);
        if ((a[0] - x) * (b[0] - x) > 0) continue;
        float t = (x - a[0]) / (b[0] - a[0]), y = a[1] + (b[1] - a[1]) * t;
        if (y < 0 || y > 2.5f) continue;
        if (hit) { hit[0] = x; hit[1] = y; hit[2] = a[2] + (b[2] - a[2]) * t; }
        return 1;
    }
    return 0;
}
static void schedule_press(int i, int a, unsigned at) {
    (void)i; (void)at;
    float facing = sinf(sx[4]);   /* +x is east: the pane lies east */
    if (a == EIA_FIRE_KNIFE) { knifes++; if (pane && knifeBreaks && facing > 0.95f && sx[0] > 1.0f) pane = 0; }
    if (a == EIA_FIRE) { shots++; if (pane && shotBreaks && facing > 0.9f) pane = 0; }
}
@@DECL@@
@@PANEL@@
@@BREACH@@
@@PANE@@
float wrapneg(float y) { y = fmodf(y, 6.2831853f); if (y > 0) y -= 6.2831853f; return y; }
static void reset(void) { memset(sx, 0, sizeof sx); sx[4] = wrapneg(0.0f); pane = 1; knifes = shots = 0; memset(g_breachUntil, 0, sizeof g_breachUntil);
    g_breachKnife[0] = 0; g_paneBestAt[0] = g_paneWalkUntil[0] = g_paneSpotAt[0] = 0; navs[0].mode = 3; H->botMove[0][2] = 0; }
static void fly(int frames, const float *mcom) {   /* the bot turns by its yaw input, walks at 4 m/s towards a nav mode 2 point */
    for (int f = 0; f < frames; f++) {
        if (!g_breachUntil[0] || H->frame >= g_breachUntil[0]) mcom_pane_tick(0, (char *)sx, mcom);
        breach_tick(0, (char *)sx);
        sx[4] = wrapneg(sx[4] - H->botMove[0][2] * 0.12f);
        if (navs[0].mode == 2 && !(g_breachUntil[0] > H->frame)) { float dx = navs[0].x - sx[0], dz = navs[0].z - sx[2], d = sqrtf(dx * dx + dz * dz); if (d > 0.15f) { sx[0] += dx / d * 0.13f; sx[2] += dz / d * 0.13f; } }
        H->frame++;
    }
}
int main(void)
{
    static char bot0[16] = "b0"; H->bots[0] = bot0; H->botCount = 1; H->frame = 1000;
    float mcom[3] = {6, 0, 0.5f};
    /* 1: the pane 2 m in, the bot (facing north) stuck at 0 m: after 3 s it walks up, faces it, knifes it - open */
    reset(); fly(400, mcom);
    assert(!pane && knifes >= 1 && g_panesBroken == 1 && learnedBreaks == 1 && sx[0] > 0.8f && !g_breachUntil[0]);
    printf("PASS: an M-COM behind a window - the operator walks up to the pane, turns to it, knifes it (%d slash(es)), the way is open\n", knifes);
    /* 2: a pane the knife does not break - then it shoots it */
    reset(); knifeBreaks = 0; sx[0] = 1.1f; fly(300, mcom);
    assert(!pane && knifes >= 1 && shots >= 1);
    printf("PASS: a pane the knife does not break - the bot shoots it (%d shot(s))\n", shots);
    /* 3: a wall (60 cm): never taken for a pane */
    reset(); knifeBreaks = shotBreaks = 1; wallThick = 1; sx[0] = 1.1f; fly(300, mcom);
    assert(pane && !knifes && !shots && !g_breachUntil[0]);
    /* 4: nothing in the way, or the M-COM 20 m off: nothing */
    reset(); wallThick = 0; pane = 0; fly(100, mcom); assert(!knifes && !shots);
    reset(); { float far[3] = {25, 0, 0}; fly(200, far); } assert(!knifes && !shots);
    puts("PASS: a wall is never taken for a pane; a clear way, or an M-COM 20 m off, leaves the bot to its route");
    return 0;
}
'''

def check_pane(sanitize):
    core = (SRC / 'core.c').read_text()
    decl = re.search(r'static unsigned g_breachUntil\[MAX_BOTS\].*?\n', core).group(0)
    p0 = core.index('// Something thin within `reach` along (fx, fz)'); p1 = core.index('// Something thin right in front at chest height (the way the soldier faces')
    b0 = core.index("// AltBotMod V32: a closed door the bot's route goes through"); b1 = core.index('// 1: knifing a fence; 2: a fence no knife gets through')
    q0 = core.index('// AltBotMod V34: an M-COM behind a window.'); q1 = core.index('// For the nav builder: what the last ray hit')
    src = PANE.replace('@@DECL@@', decl).replace('@@PANEL@@', core[p0:p1]).replace('@@BREACH@@', core[b0:b1]).replace('@@PANE@@', core[q0:q1])
    with tempfile.TemporaryDirectory() as td:
        (Path(td) / 'pane.c').write_text(src)
        run(['gcc', *san(sanitize), '-std=gnu11', '-O1', '-Wall', '-Wno-unused-function', '-Wno-unused-variable', str(Path(td) / 'pane.c'), '-lm', '-o', str(Path(td) / 'pane')])
        print(run([str(Path(td) / 'pane')]).strip())
    mn = (SRC / 'modes_native.inc').read_text()
    assert 'if(operator && !g_modeBot[i].interactAt && mcom_pane_tick(i,soldier,goal))return;' in mn
    bl = core[core.index('static void breakable_learn(unsigned m, int breaks)'):]; bl = bl[:bl.index('\n}\n')]
    assert 'if (++fails[k] < 2) return;' in bl and 'if (material_breakable(m)) return;' in bl
    print('PASS: a pane is learned "unbreakable" only after a second failed try, never one known to break (V18-V33: after one)')

def check_still():
    fl = (SRC / 'fleet_native.inc').read_text(); core = (SRC / 'core.c').read_text(); cm = (SRC / 'combat_motion.inc').read_text(); en = (SRC / 'enhanced.c').read_text()
    assert "if (distance>g_enhanced.vehicleWalkMeters) continue;" in fl and "dist2(origin,objective_pos(tf))<120*120) continue;" in fl
    assert "if (!rush && *(char **)(H->bots[i]+OFF_PLAYER_SOLDIER) && H->frame-g_lastSpawnedAt[i]>20*30) {" in fl
    assert 'c->funBotsMesh = 1; c->vehicleWalkMeters = 250.0f;' in en and 'FLOAT_KEY("VehicleWalkMeters", vehicleWalkMeters, 25, 6000)' in en
    assert 'if (g_supplyFailTo[i] == sender && H->frame - g_supplyFailAt[i] < 45 * 30 && i != ti) continue;' in core
    assert cm.index('if(dist2(me,m->goal)<4*4 || dist2(enemy,m->goal)<12*12)goto failed;') < cm.index('if(plan_path(i,soldier,m->goal[0],m->goal[1],m->goal[2])){')
    print('PASS: fewer bots turning round - mid-round vehicle walks at most 250 m (40 m with the objective within 120 m; spawning and the opening rush unchanged), no repeat supply run to a caller it could not reach for 45 s, relocations re-checked')

STALL = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "air.h"
int main(void)
{
    BMAirConfig c; bm_air_defaults(&c);
    assert(c.jetAreaMargin == 900);
    const BMAirProfile *p = bm_air_profile("F18");
    assert(p && p->kind == BM_AIR_JET);
    BMAirController s; bm_air_start(&s, BM_AIR_JET, 1); s.phase = BM_FLIGHT_CRUISE;
    BMAirObservation o; memset(&o, 0, sizeof o);
    /* nose 40 deg up, 12 m/s, 900 m above the ground (V33 live) */
    float pitch = 0.7f;
    float fw[3] = {0, sinf(pitch), cosf(pitch)}, up[3] = {0, cosf(pitch), -sinf(pitch)}, rt[3] = {1, 0, 0};
    memcpy(o.forward, fw, 12); memcpy(o.up, up, 12); memcpy(o.right, rt, 12);
    o.position[1] = 1050; o.velocity[2] = 10; o.velocity[1] = 6; o.velocityValid = 1;
    o.groundValid = 1; o.groundY = 150; o.goal[0] = 0; o.goal[1] = 600; o.goal[2] = 2000;
    BMAirOutput out; assert(bm_air_step(&s, p, &c, &o, 1 / 30.0f, &out));
    assert(s.stall && out.tactic == BM_TACTIC_STALL && out.throttle == 1 && !out.fire);
    float push = out.pitch * c.pitchSign;
    assert(push > 0.5f);   /* nose down hard */
    printf("PASS: a stalled jet (12 m/s, nose 40 deg up, 900 m up) - nose down (pitch %.2f), wings level, full power, no guns: \"%s\"\n", push, bm_air_tactic_name(out.tactic));
    /* fast again: flies on */
    o.velocity[2] = 95; o.velocity[1] = -10; memcpy(o.forward, (float[3]){0, -0.1f, 0.995f}, 12); memcpy(o.up, (float[3]){0, 0.995f, 0.1f}, 12);
    assert(bm_air_step(&s, p, &c, &o, 1 / 30.0f, &out) && !s.stall && out.tactic != BM_TACTIC_STALL);
    /* low and slow: left to the flight law's ground recovery, never a stall dive into the ground */
    bm_air_start(&s, BM_AIR_JET, 1); s.phase = BM_FLIGHT_CRUISE; o.position[1] = 200; o.velocity[2] = 20; o.velocity[1] = 0;
    assert(bm_air_step(&s, p, &c, &o, 1 / 30.0f, &out) && !s.stall);
    puts("PASS: once fast again (95 m/s) the jet flies on; low (50 m up) it is never pushed into a dive");
    return 0;
}
'''

def check_air(sanitize):
    with tempfile.TemporaryDirectory() as td:
        (Path(td) / 'stall.c').write_text(STALL)
        run(['gcc', *san(sanitize), '-std=gnu11', '-O1', '-Wall', '-I', str(SRC), str(Path(td) / 'stall.c'), str(SRC / 'air.c'), '-lm', '-o', str(Path(td) / 'stall')])
        print(run([str(Path(td) / 'stall')]).strip())
    core = (SRC / 'core.c').read_text(); an = (SRC / 'air_native.inc').read_text(); air = (SRC / 'air.c').read_text(); pn = (SRC / 'passenger_native.inc').read_text(); lw = (SRC / 'lock_warn.h').read_text()
    hook = core[core.index('void __cdecl on_lock_update(char *ctrl, unsigned *stack)'):]; hook = hook[:hook.index('    float *m = (float *)stack[2];')]
    assert '*(unsigned *)holder == ADDR_LOCK_TARGET_VT' in hook and 'incoming_lock_note(v, st == 2 ? 1.0f : fminf(prog, 1.0f));' in hook
    assert 'incoming_lock(a->veh)>=0.75f && (!a->counterAt || H->frame-a->counterAt>=8*30)' in an and 'incoming_lock(veh)>=0.9f' in pn
    assert 'H->frame - g_inLock[k].at <= 15' in lw
    assert 'out->secondary=1; s->secondaryThisPass=1;' not in air and 'air_secondary(s,attacking && fabsf(devYaw)<0.3f' in air
    assert 'if(air && !gunshipTarget && tp && tp->kind==BM_AIR_ATTACK && distance<2000)jetScore=-650;' in an and 'm,1400.0f);' in an
    print('PASS: flares 75% into an enemy lock (8 s apart), smoke for armour at a full lock - read from the game\'s own locking controllers; jets keep heat seekers for aircraft and go for attack helicopters; wider jet area (900 m margin, 1400 m minimum)')

def check_squads():
    so = (SRC / 'squad_orders.inc').read_text(); core = (SRC / 'core.c').read_text(); mn = (SRC / 'modes_native.inc').read_text()
    assert 'if (squad_order_chat(who, cmd, arg)) return;' in core and 'if (type == 17) { squad_order_radio(sender); return; }' in core
    assert "{ int od=0,of=squad_order_flag(team,squad,&od); if(of>=0){*defend=od;return of;} }" in core
    assert core.index('if(lf>=0){*defend=ld;return lf;}') < core.index('squad_order_flag(team,squad,&od); if(of>=0){*defend=od;return of;}') < core.index('int f=director_target(i,role);')
    assert '{int ro=squad_order_rush(i);if(ro)return ro;}' in mn and 'squad_announce_tick();   // AltBotMod V34' in core
    assert 'chat_rcon("admin.say\\t%.120s\\tsquad\\t%d\\t%d", msg, team, squad);' in so and '#define SQUAD_ORDER_FRAMES   (240 * 30)' in so
    print('PASS: squad orders - !attack B / !defend A / looking at it, the commo rose\'s Attack; the squad\'s bots take it ahead of the team plan for 4 min (Rush: M-COM A / B); bot squad leaders announce their objective in the squad chat')

def check_settings_version():
    sc = (SC / 'ServerControl-Core.ps1').read_text(); eh = (SRC / 'enhanced.h').read_text()
    assert "New-BmSetting 'FunBotsMesh' 'Movement' 'bool' 0 1 1" in sc and "New-BmSetting 'VehicleWalkMeters' 'Vehicles' 'float' 25 6000 250" in sc
    assert "'float' 0 3000 900 'Jet area margin (m)'" in sc
    assert '#define BM_ENHANCED_VERSION "AltBotMod V34 - Squad Up"' in eh
    assert 'New-BmBanner "AltBotMod V34"' in (SC / 'AltBotMod-ServerControl.ps1').read_text()
    print('PASS: FunBotsMesh / VehicleWalkMeters in Server Control; version V34')

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    check_mesh(a.sanitize); check_pane(a.sanitize); check_still(); check_air(a.sanitize); check_squads(); check_settings_version()
