"""Compile portable policies and exercise actual extracted native driving code.

Run with Python 3 and a host C compiler (gcc/cc). This does not run BF3.
Native engine pointers, replication, physical vehicle clearance, and M-COM
interaction still need testing with the exact Windows server in game.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "files/BotMod/src"


def extract_function(source, signature):
    start = source.index(signature)
    masked = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"', lambda m: " " * len(m[0]), source)
    opening = masked.index("{", start)
    depth = 1
    for end in range(opening + 1, len(masked)):
        depth += (masked[end] == "{") - (masked[end] == "}")
        if depth == 0:
            return source[start:end + 1]
    raise ValueError("Unbalanced function")


def main():
    cc = shutil.which("gcc") or shutil.which("cc")
    if not cc:
        raise SystemExit("Install a host C compiler to run these offline checks.")
    original = (ROOT / "validation/reference_driving.c.txt").read_text(encoding="utf-8-sig")
    patched = (SOURCE / "core.c").read_text(encoding="utf-8-sig")
    signature = "static void drive_tick(int i)"
    tail_marker = "    if (H->frame >= d->reverseUntil) d->awayOn = 0;"

    def tail(text):
        fn = extract_function(text, signature)
        return fn[fn.index(tail_marker):fn.rindex("}")]

    typedef_start = patched.index("typedef struct DriveState {")
    typedef_end = patched.index("} DriveState;", typedef_start) + len("} DriveState;")
    drive_type = patched[typedef_start:typedef_end]
    stop_signature = "static void drive_stop(int i, const char *why, int cooldownSec)"
    old_stop = extract_function(original, stop_signature).replace("drive_stop(", "legacy_stop(", 1)
    new_stop = extract_function(patched, stop_signature).replace("drive_stop(", "patched_stop(", 1)
    harness = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "enhanced.h"
#define MAX_BOTS 4
#define MAX_FLAGS 16
#define DRIVE_MAX_PTS 128
#define OFF_PLAYER_ONLINE_NAME 0
#define ORDER_GETOUT 16
''' + drive_type + r'''
typedef struct { unsigned frame; char *bots[MAX_BOTS]; float botMove[MAX_BOTS][3]; } Host;
typedef struct { int mode; } BotNav;
typedef struct { int replanAt; } BotBrain;
typedef struct { int unused; } NavGrid;
static Host host, *H = &host;
static DriveState g_drive[MAX_BOTS];
static unsigned g_driveCooldown[MAX_BOTS], g_vehChanceUntil[MAX_BOTS];
static unsigned char g_driving[MAX_BOTS], *g_boatMask, *g_driveMask;
static BMSteering g_enhancedSteer[MAX_BOTS];
static BMConfig g_enhanced;
static BotBrain brains[MAX_BOTS]; static BotNav navs[MAX_BOTS];
static int inside, stops;
static float dist2(const float *a,const float *b) { float x=a[0]-b[0],z=a[2]-b[2]; return x*x+z*z; }
static void bot_log(const char *fmt, ...) { (void)fmt; }
static char *player_vehicle(char *p) { (void)p; return inside ? (char*)1 : NULL; }
static void bot_say(int i,int type) { (void)i; (void)type; }
static void order_reaction(char *p,int type,void *unused) { (void)p; (void)type; (void)unused; }
static void bot_exit_vehicle(int i,int chute) { (void)i; (void)chute; inside=0; }
static BotNav *bot_nav(int i) { return &navs[i]; }
static BotBrain *bot_brain(int i) { return &brains[i]; }
static void choose_local_objective(int i,const char *why) { (void)i; (void)why; }
static struct{int job;}g_armor[MAX_BOTS];
static void diagnostic_note(int i,const char *why,char *v){(void)i;(void)why;(void)v;}
static int traffic_controls(int i,const float *vp,const float *xf,float *throttle,float *steer){(void)i;(void)vp;(void)xf;(void)throttle;(void)steer;return 0;}
static int vehicle_is_boat(char *p) { (void)p; return 0; }
static void vehicle_mark_bad(char *v,int seconds) { (void)v; (void)seconds; }
static int drive_strike(unsigned char *mask,float x,float z,float radius) { (void)mask; (void)x; (void)z; (void)radius; return 1; }
static void drive_stop(int i,const char *why,int cooldownSec) { (void)why; (void)cooldownSec; stops++; g_drive[i].phase=0; }
static int drive_recover_job(int i,DriveState*d,const float*p){(void)i;(void)d;(void)p;return 0;}
static int drive_block_for_job(int i,unsigned char*m,float x,float z,float r,float y){(void)i;(void)y;return drive_strike(m,x,z,r);}
static void drive_arrive_objective(int i,DriveState *d,const float *vp) { (void)i; (void)vp; d->phase=4; }
''' + old_stop + "\n" + new_stop + r'''
static void reset_test(void) {
    memset(&host,0,sizeof host); memset(g_drive,0,sizeof g_drive); memset(g_driveCooldown,0,sizeof g_driveCooldown);
    host.frame=90; host.bots[0]="[BOT] test"; g_drive[0].phase=3; g_drive[0].goal[0]=1000;
    bm_defaults(&g_enhanced); stops=0; inside=0;
}
static void original_step(void) {
    int i=0; DriveState *d=&g_drive[i]; float point[3]={0}, transform[16]={0};
    const float *vp=point,*xf=transform; float throttle=1,steer=0,vwant=10;
    char *pl=H->bots[i];
''' + tail(original) + r'''
}
static void patched_step(void) {
    int i=0; DriveState *d=&g_drive[i]; float point[3]={0}, transform[16]={0};
    const float *vp=point,*xf=transform; float throttle=1,steer=0,vwant=10;
    char *pl=H->bots[i];
''' + tail(patched) + r'''
}
int main(void) {
    reset_test(); original_step(); assert(g_drive[0].reverseUntil==0 && g_drive[0].stuckCount==0);
    reset_test(); patched_step(); assert(g_drive[0].reverseUntil>H->frame && g_drive[0].stuckCount==1);
    reset_test(); g_drive[0].reverseUntil=H->frame+15; patched_step(); assert(g_drive[0].stuckCount==0);
    reset_test(); g_drive[0].backOutUntil=H->frame+15; patched_step(); assert(g_drive[0].stuckCount==0);
    reset_test(); g_drive[0].checkAt=H->frame; patched_step(); assert(g_drive[0].stuckCount==0);
    reset_test(); inside=1; legacy_stop(0,"test",20); assert(g_driveCooldown[0]==0);
    reset_test(); inside=1; patched_stop(0,"test",20); assert(g_driveCooldown[0]==H->frame+600);
    assert(g_drive[0].phase==0 && !inside);
    puts("PASS: original forward-stuck and cooldown bugs reproduced; actual patched native blocks recover correctly");
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="botmod-checks-") as temp:
        temp = Path(temp)
        executable = temp / "policy-tests"
        subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", str(ROOT / "validation/policy_tests.c"), str(SOURCE / "enhanced.c"), str(SOURCE / "air.c"), str(SOURCE / "modes.c"), "-lm", "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        for name in ("air_tests", "air_native_tests", "flight_regression_tests", "gunner_target_tests", "tactics_policy_tests"):
            executable = temp / name
            subprocess.run([cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "validation" / (name + ".c")),
                            str(SOURCE / "enhanced.c"), str(SOURCE / "air.c"), str(SOURCE / "modes.c"), "-lm", "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
        (temp / "native-regressions.c").write_text(harness)
        executable = temp / "native-regressions"
        subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-I", str(SOURCE), str(temp / "native-regressions.c"), str(SOURCE / "enhanced.c"), str(SOURCE / "air.c"), str(SOURCE / "modes.c"), "-lm", "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
