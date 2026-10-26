#!/usr/bin/env python3
"""AltBotMod V15 - Route Actions: offline checks.

1. The real Fun-Bots route files (all of BotMod_traces): every recorded action parsed (boarding spots, exit points
   with onlyPassengers, beacon spots, M-COM spots), wait lengths decoded, path labels (team / vehicle kind) read.
2. Flag names: Seine Crossing's labelled paths matched to the flag positions the live map recorded (A-E), and routes
   to a flag preferring its own labelled paths (real file + a synthetic pair of paths).
3. The native logic (route_actions_native.inc, compiled with stubs): waits held and released, crouch sections,
   exit points (passengers only / everyone / armour keeps its crew / too far from the objective), the fleet's
   boarding-spot preference, beacon spot choice and the beacon run.
4. The new route files: Rush, Squad Rush, Domination, Squad DM, Gun Master ... loaded for their modes.
"""
from pathlib import Path
import argparse, re, subprocess, sys, tempfile
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test4_checks import function  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
TRACES = ROOT / 'files/server/BotMod_traces'

PARSE = r'''
#include "trace_nav.h"
#include <assert.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int near3(const float *p, float x, float y, float z) { return fabsf(p[0]-x) < 0.01f && fabsf(p[1]-y) < 0.01f && fabsf(p[2]-z) < 0.01f; }
static char dirpath[600];
static int load(TraceNet *t, const char *f) { char p[800]; snprintf(p, sizeof p, "%s/%s", dirpath, f); return tn_load(t, p); }
int main(int argc, char **argv)
{
    snprintf(dirpath, sizeof dirpath, "%s", argv[1]);
    DIR *d = opendir(dirpath); struct dirent *e; int files = 0, failed = 0; long tot[5] = {0}, riders = 0, waits = 0, crouch = 0, waitNodes = 0;
    while ((e = readdir(d))) {
        if (!strstr(e->d_name, ".map")) continue;
        TraceNet t; files++;
        if (!load(&t, e->d_name)) { failed++; printf("cannot load %s\n", e->d_name); continue; }
        for (int k = 0; k < t.actionCount; k++) { tot[t.actions[k].type]++; riders += t.actions[k].onlyPassengers; }
        for (int k = 0; k < t.count; k++) { if (TN_WAIT_SECONDS(t.nodes[k].flags)) { waits += TN_WAIT_SECONDS(t.nodes[k].flags); waitNodes++; } if (t.nodes[k].flags & TN_FLAG_CROUCH) crouch++; }
        tn_free(&t);
    }
    closedir(d);
    assert(!failed);
    // all 181 files: 1,112 "vehicle" / 367 "exit" (72 onlyPassengers:true) / 197 "beacon" / 559 "mcom" actions (grep of the files)
    assert(tot[TN_ACT_VEHICLE] == 1112 && tot[TN_ACT_EXIT] == 367 && riders == 72 && tot[TN_ACT_BEACON] == 197 && tot[TN_ACT_MCOM] == 559);
    TraceNet t;
    // MP_001: path 19 "vehicle tank1 us", its point 4 the boarding spot (yaw 2.7127, pitch -0.6036)
    assert(load(&t, "MP_001_ConquestLarge0.map"));
    int a = tn_action_near(&t, TN_ACT_VEHICLE, (float[]){81.8f, 73.8f, 142.4f}, 2, 2);
    assert(a >= 0 && t.actions[a].path == 19 && t.actions[a].point == 4 && t.actions[a].team == 1 && t.actions[a].vehKind == TN_VEH_TANK &&
           fabsf(t.actions[a].yaw - 2.7127f) < 0.001f && fabsf(t.actions[a].pitch + 0.6036f) < 0.001f && near3(t.actions[a].p, 81.823891f, 73.844879f, 142.433578f));
    a = tn_action_near(&t, TN_ACT_VEHICLE, (float[]){-162.1f, 74.0f, -154.4f}, 2, 2);
    assert(a >= 0 && t.actions[a].team == 2 && t.actions[a].vehKind == TN_VEH_TANK);
    assert(tn_action_near(&t, TN_ACT_EXIT, (float[]){0, 0, 0}, 2000, 2000) < 0);   // (this file has none)
    tn_free(&t);
    // MP_017: an exit for passengers only, one for everyone, a beacon spot (time 5)
    assert(load(&t, "MP_017_ConquestLarge0.map"));
    a = tn_action_near(&t, TN_ACT_EXIT, (float[]){-361.9f, 70.4f, 250.3f}, 1, 1); assert(a >= 0 && t.actions[a].onlyPassengers == 1);
    a = tn_action_near(&t, TN_ACT_EXIT, (float[]){-280.6f, 67.0f, 248.5f}, 1, 1); assert(a >= 0 && t.actions[a].onlyPassengers == 0);
    a = tn_action_near(&t, TN_ACT_BEACON, (float[]){-240.3f, 70.5f, 154.7f}, 1, 1); assert(a >= 0 && fabsf(t.actions[a].time - 5) < 0.01f && fabsf(t.actions[a].yaw - 5.508f) < 0.001f);
    int air = 0; for (int k = 0; k < t.actionCount; k++) if (t.actions[k].type == TN_ACT_VEHICLE && t.actions[k].vehKind == TN_VEH_AIR) air++;
    assert(air >= 2);   // "vehicle spawn us viper" / "... ru havoc"
    tn_free(&t);
    // a recorded wait (input 512 = SpeedMode 0, OptValue 2) is 2 s and every route keeps it
    const char *txt = "pathIndex;pointIndex;transX;transY;transZ;inputVar;data\n"
        "1;1;0;0;0;3;\n1;2;1;0;0;3;\n1;3;2;0;0;512;\n1;4;3;0;0;3;\n1;5;4;0;0;3;\n1;6;30;0;0;3;\n1;7;31;0;0;2;\n1;8;32;0;0;2;\n1;9;33;0;0;3;\n";
    assert(tn_load_text(&t, txt, strlen(txt)) && TN_WAIT_SECONDS(t.nodes[2].flags) == 2 && (t.nodes[2].flags & TN_FLAG_WAIT) && (t.nodes[6].flags & TN_FLAG_CROUCH));
    float pts[64 * 3]; unsigned char fl[64]; int tr; float len;
    assert(tn_field(&t, 0, (float[]){4, 0, 0}, 0.5f));
    int n = tn_route(&t, 0, 0, 20.0f, pts, fl, 64, &tr, &len), kept = 0;
    for (int k = 0; k < n; k++) if (TN_WAIT_SECONDS(fl[k]) == 2 && pts[k * 3] == 2) kept = 1;
    assert(kept);
    tn_free(&t);
    printf("PASS: route files - %d files, every recorded action read: %ld boarding spots, %ld exit points (%ld passengers only), %ld beacon spots, %ld M-COM spots; "
           "%ld recorded waits (%ld s in all), %ld crouch waypoints; boarding spots carry their path's team and vehicle kind (\"vehicle tank1 us\"), facing and pitch; waits survive route simplification\n",
           files, tot[TN_ACT_VEHICLE], tot[TN_ACT_EXIT], riders, tot[TN_ACT_BEACON], tot[TN_ACT_MCOM], waitNodes, waits, crouch);
    return 0;
}
'''

LABELS = r'''
#include "trace_nav.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static float frac(TraceNet *t, int id, int start, unsigned bit)
{
    float pts[256 * 3]; unsigned char fl[256]; int tr; float len;
    // walk the next pointers directly (the simplified route drops points)
    int k = start, on = 0, all = 0;
    while (k >= 0 && all < 100000) { all++; if (t->nodes[k].obj & bit) on++; k = t->field[id].next[k]; }
    (void)pts; (void)fl; (void)tr; (void)len;
    return all ? (float)on / all : 0;
}
int main(int argc, char **argv)
{
    TraceNet t;
    assert(tn_load(&t, argv[1]));
    // Seine Crossing's flags where the live map put them (2026-10-11), in a scrambled order: C E A D B
    float flags[5][3] = {{-75.4f, 0, 21.7f}, {-15.5f, 0, -37.2f}, {30.6f, 0, 119.6f}, {71.6f, 0, -32.1f}, {-50.9f, 0, 109.5f}};
    for (int f = 0; f < 5; f++) { float best = 1e9f; for (int k = 0; k < t.count; k++) { float dx = t.nodes[k].p[0]-flags[f][0], dz = t.nodes[k].p[2]-flags[f][2]; if (dx*dx+dz*dz < best) { best = dx*dx+dz*dz; flags[f][1] = t.nodes[k].p[1]; } } }
    int letter[5];
    int m = tn_match_flags(&t, (const float (*)[3])flags, 5, letter);
    printf("seine: %d matched: %c %c %c %c %c\n", m, 'a'+letter[0], 'a'+letter[1], 'a'+letter[2], 'a'+letter[3], 'a'+letter[4]);
    // Fun-Bots lettered Seine's flags its own way (from the US base): its "a" is the game's D, "b" E, "c" C, "d" A, "e" B -
    // which is why the letters are matched by where the labelled paths run, not by name
    assert(m == 5 && letter[0] == 2 && letter[1] == 1 && letter[2] == 3 && letter[3] == 0 && letter[4] == 4);
    // routes to D (index 3) from 40 far-off waypoints: with the labels a route runs on D's own paths at least as much
    unsigned bit = t.flagPrefer[3];
    assert(bit == 1u << 0 && tn_field(&t, 3, flags[3], 22));
    float with = 0, without = 0; int starts[40], ns = 0;
    for (int k = 0; k < t.count && ns < 40; k += t.count / 40) if (isfinite(t.field[3].dist[k])) starts[ns++] = k;
    for (int s = 0; s < ns; s++) with += frac(&t, 3, starts[s], bit);
    t.flagPrefer[3] = 0; t.field[3].ready = 0; assert(tn_field(&t, 3, flags[3], 22));
    for (int s = 0; s < ns; s++) without += frac(&t, 3, starts[s], bit);
    printf("seine: share of a route to D on D's labelled paths %.2f with labels, %.2f without (%d routes)\n", with / ns, without / ns, ns);
    assert(with >= without);
    tn_free(&t);
    // synthetic: two ways from S (0,0) to flag F (100,0): the unlabelled one 100 m, the one labelled "a" 108 m
    const char *txt = "pathIndex;pointIndex;transX;transY;transZ;inputVar;data\n"
        "1;1;0;0;0;3;\n1;2;6;0;-3;3;\n1;3;12;0;-6;3;\n1;4;18;0;-9;3;\n1;5;24;0;-12;3;\n1;6;30;0;-15;3;\n1;7;37;0;-17;3;\n1;8;44;0;-18;3;\n1;9;52;0;-18;3;\n1;10;60;0;-17;3;\n1;11;67;0;-15;3;\n1;12;73;0;-12;3;\n1;13;79;0;-9;3;\n1;14;85;0;-6;3;\n1;15;92;0;-3;3;\n1;16;100;0;0;3;{\"Objectives\":[\"a\"]}\n"
        "2;1;0;0;0.5;3;\n2;2;8;0;0.5;3;\n2;3;16;0;0.5;3;\n2;4;24;0;0.5;3;\n2;5;32;0;0.5;3;\n2;6;40;0;0.5;3;\n2;7;48;0;0.5;3;\n2;8;56;0;0.5;3;\n2;9;64;0;0.5;3;\n2;10;72;0;0.5;3;\n2;11;80;0;0.5;3;\n2;12;88;0;0.5;3;\n2;13;94;0;0.5;3;\n2;14;100;0;0.5;3;\n"
        "3;1;-40;0;0;3;{\"Objectives\":[\"b\"]}\n3;2;-34;0;0;3;\n3;3;-28;0;0;3;\n3;4;-22;0;0;3;\n3;5;-16;0;0;3;\n3;6;-10;0;0;3;\n3;7;-5;0;0;3;\n3;8;0;0;0.2;3;\n";
    assert(tn_load_text(&t, txt, strlen(txt)) && t.labelledPaths == 2);
    float fl2[2][3] = {{-40, 0, 0}, {100, 0, 0}};
    int let[2];
    assert(tn_match_flags(&t, (const float (*)[3])fl2, 2, let) == 2 && let[0] == 1 && let[1] == 0);
    int start = -1; for (int k = 0; k < t.count; k++) if (t.nodes[k].path == 3 && t.nodes[k].point == 1) start = k;
    assert(tn_field(&t, 1, fl2[1], 3));
    float on = frac(&t, 1, start, 1u);
    t.flagPrefer[1] = 0; t.field[1].ready = 0; assert(tn_field(&t, 1, fl2[1], 3));
    float off = frac(&t, 1, start, 1u);
    assert(on > 0.5f && off == 0);
    tn_free(&t);
    puts("PASS: flag names - Seine Crossing's labelled paths matched to the flags the live map recorded (5 of 5, scrambled order; Fun-Bots' own letters differ from the game's, so they are matched by position); a route to a flag prefers its own labelled paths (a labelled way 8% longer is taken; without labels the shorter one)");
    return 0;
}
'''

NATIVE = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "trace_nav.h"
#include "enhanced.h"
#define MAX_BOTS 4
#define OFF_SOLDIER_POSITION 0
#define OFF_PLAYER_ONLINE_NAME 16
enum { TASK_NONE, TASK_FOLLOW };
#define PATH_MAX_POINTS 32
typedef struct BotNav { int mode; float targetYaw; float x, z; } BotNav;
typedef struct BotPath { int count, next, partial; unsigned char wpFlags[PATH_MAX_POINTS]; float pts[PATH_MAX_POINTS * 3]; unsigned lastProgressFrame; } BotPath;
typedef struct { int task, enroute, waiting; } BotBrain;
typedef struct { unsigned frame; float x, z; int directTries; } Progress;
typedef struct DriveState { int phase, manual, flag, routeExit; char *veh; float goal[3]; } DriveState;
static struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; float botMove[MAX_BOTS][3]; } host, *H = &host;
static BMConfig g_enhanced; static TraceNet g_trace; static BotPath g_paths[MAX_BOTS]; static BotNav g_nav[MAX_BOTS]; static BotBrain g_brain[MAX_BOTS];
static Progress g_progress[MAX_BOTS]; static DriveState g_drive[MAX_BOTS]; static int g_driving[MAX_BOTS]; static unsigned g_hitAt[MAX_BOTS];
static unsigned g_raBoardRoutes; static unsigned char g_raCrouched[MAX_BOTS];
static int g_flagCount = 2, g_lastPlanDeferred, g_team[MAX_BOTS], g_seat[MAX_BOTS], g_owner[2], g_armored, g_boat, g_arrived, g_exits, g_crouches, g_stands, g_placed, g_beaconSlot = 5, g_beaconAmmo = 1, g_recon[MAX_BOTS];
static char *g_inVeh[MAX_BOTS];
static float g_flags[2][3] = {{0, 0, 0}, {400, 0, 0}}, g_vehPos[3];
static char g_log[64][300]; static int g_nlog;
static void bot_log(const char *f, ...) { va_list a; va_start(a, f); vsnprintf(g_log[g_nlog % 64], 300, f, a); va_end(a); g_nlog++; }
static int logged(const char *s) { for (int k = 0; k < 64 && k < g_nlog; k++) if (strstr(g_log[k], s)) return 1; return 0; }
static float *flag_pos(int f) { return g_flags[f]; }
static int flag_owner(int f) { return g_owner[f]; }
static BotNav *bot_nav(int i) { return &g_nav[i]; }
static BotBrain *bot_brain(int i) { return &g_brain[i]; }
static int player_team(char *pl) { for (int i = 0; i < MAX_BOTS; i++) if (H->bots[i] == pl) return g_team[i]; return 0; }
static char *player_vehicle(char *pl) { for (int i = 0; i < MAX_BOTS; i++) if (H->bots[i] == pl) return g_inVeh[i]; return NULL; }
static int vehicle_seat_of(char *v, char *pl) { (void)v; for (int i = 0; i < MAX_BOTS; i++) if (H->bots[i] == pl) return g_seat[i]; return -1; }
static float *vehicle_pos(char *v, int *t) { (void)v; (void)t; return g_vehPos; }
static int vehicle_is_boat(char *v) { (void)v; return g_boat; }
static const char *vehicle_name(char *v) { (void)v; return g_armored ? "T90" : "Humvee"; }   // (bm_vehicle_profile / bm_armored: enhanced.c)
static void drive_arrive_objective(int i, DriveState *d, const float *vp) { (void)i; (void)d; (void)vp; g_arrived++; }
static void bot_exit_vehicle(int j, int p) { (void)p; g_inVeh[j] = NULL; g_exits++; }
static void gunner_reset(int j) { (void)j; }
static void choose_local_objective(int j, const char *w) { (void)j; (void)w; }
static float hdist(const float *a, const float *b) { float dx = a[0] - b[0], dz = a[2] - b[2]; return sqrtf(dx * dx + dz * dz); }
static void bot_crouch(int i) { (void)i; g_crouches++; }
static void bot_stand(int i) { (void)i; g_stands++; }
static int is_recon(int i) { return g_recon[i]; }
static int bot_gadget_slot(int i, const char *a, const char *b) { (void)i; (void)a; (void)b; return g_beaconSlot; }
static int bot_ammo(int i, int s) { (void)i; (void)s; return g_beaconAmmo; }
static void place_gadget_now(int i, int s) { (void)i; (void)s; g_placed++; }
static int plan_budget_left(void) { return 1; }
static char soldiers[MAX_BOTS][64], players[MAX_BOTS][64];
static int plan_path(int i, char *s, float x, float y, float z) { (void)i; float *p = (float *)(s + OFF_SOLDIER_POSITION); p[0] = x; p[1] = y; p[2] = z; return 3; }   // (walks there at once)
#include "route_actions_native.inc"
int main(void)
{
    bm_defaults(&g_enhanced); H->botCount = MAX_BOTS;
    for (int i = 0; i < MAX_BOTS; i++) { H->bots[i] = players[i]; sprintf(players[i] + OFF_PLAYER_ONLINE_NAME, "[BOT] Recon US %d", i); g_team[i] = 1; }
    // a route file with a wait (3 s), a crouch section, an exit for everyone / for passengers only, beacon spots, a boarding spot
    const char *txt = "pathIndex;pointIndex;transX;transY;transZ;inputVar;data\n"
        "1;1;0;0;0;3;\n1;2;5;0;0;768;\n1;3;10;0;0;2;\n1;4;15;0;0;2;\n1;5;20;0;0;3;\n"
        "2;1;300;0;0;3;{\"Action\":{\"inputs\":[35],\"onlyPassengers\":false,\"time\":0.5,\"type\":\"exit\"}}\n"
        "3;1;340;0;0;3;{\"Action\":{\"inputs\":[35],\"onlyPassengers\":true,\"time\":0.5,\"type\":\"exit\"}}\n"
        "4;1;0;0;600;3;{\"Action\":{\"inputs\":[35],\"onlyPassengers\":false,\"time\":0.5,\"type\":\"exit\"}}\n"
        "5;1;250;0;20;65283;{\"Action\":{\"inputs\":{},\"pitch\":-0.9,\"time\":5,\"type\":\"beacon\",\"yaw\":4.1},\"Objectives\":[\"beacon\"]}\n"
        "6;1;380;0;0;65283;{\"Action\":{\"inputs\":{},\"pitch\":-0.9,\"time\":5,\"type\":\"beacon\",\"yaw\":1.0},\"Objectives\":[\"beacon\"]}\n"
        "7;1;120;0;0;65283;{\"Action\":{\"inputs\":{},\"pitch\":-0.9,\"time\":5,\"type\":\"beacon\",\"yaw\":2.0},\"Objectives\":[\"beacon\"]}\n"
        "8;1;50;0;50;3;{\"Objectives\":[\"vehicle tank1 ru\"]}\n8;2;52;0;52;3;{\"Action\":{\"inputs\":[35],\"pitch\":-0.6,\"time\":0.5,\"type\":\"vehicle\",\"yaw\":2.7}}\n";
    assert(tn_load_text(&g_trace, txt, strlen(txt)) && g_trace.actionCount == 7);
    // ---- waits and crouch sections ----
    BotPath *p = &g_paths[0]; char *s = soldiers[0]; float *pos = (float *)s;
    p->count = 5; for (int k = 0; k < 5; k++) { p->pts[k * 3] = g_trace.nodes[k].p[0]; p->wpFlags[k] = g_trace.nodes[k].flags; }
    route_wait_reset(0); H->frame = 100; g_nav[0].mode = 3;
    p->next = 1; pos[0] = 4.0f; route_waypoint_tick(0, s, p);
    assert(g_raHoldUntil[0] == 100 + 3 * 30 && route_hold_tick(0, s, &g_nav[0]) && H->botMove[0][0] == 0);
    H->frame = 150; assert(route_hold_tick(0, s, &g_nav[0]));
    H->frame = 191; assert(!route_hold_tick(0, s, &g_nav[0]));       // 3 s, then on
    route_waypoint_tick(0, s, p); assert(g_raHoldUntil[0] == 0);   // (the same wait point is not held twice)
    route_wait_reset(0); g_hitAt[0] = H->frame - 20; route_waypoint_tick(0, s, p); assert(g_raHoldUntil[0] == 0);   // under fire: no wait
    g_hitAt[0] = 0; route_wait_reset(0); g_brain[0].task = TASK_FOLLOW; route_waypoint_tick(0, s, p); assert(g_raHoldUntil[0] == 0); g_brain[0].task = TASK_NONE;   // on an errand: no wait
    p->next = 2; pos[0] = 7.0f; route_waypoint_tick(0, s, p); assert(g_crouches == 1 && route_crouched(0));    // into the crouch section
    p->next = 3; route_waypoint_tick(0, s, p); assert(g_crouches == 1);
    p->next = 4; pos[0] = 16; route_waypoint_tick(0, s, p); assert(g_stands == 1 && !route_crouched(0));      // out of it: stands
    g_enhanced.routeActions = 0; route_wait_reset(0); p->next = 1; pos[0] = 4.0f; route_waypoint_tick(0, s, p); assert(g_raHoldUntil[0] == 0); g_enhanced.routeActions = 1;
    // ---- exit points ----
    char veh[16]; DriveState *d = &g_drive[0];
    for (int j = 1; j < 4; j++) { g_inVeh[j] = veh; g_seat[j] = j; }   // seat 1 gunner, 2-3 passengers
    g_inVeh[0] = veh; g_seat[0] = 0;
    memset(d, 0, sizeof *d); d->phase = 3; d->veh = veh; d->flag = 1; memcpy(d->goal, g_flags[1], 12);
    float vp[3] = {301, 0, 2};
    H->frame = 600; g_armored = 0;
    assert(route_exit_tick(0, d, vp) == 1 && g_arrived == 1);                         // light vehicle, everyone: arrives here
    assert(route_exit_tick(0, d, vp) == 0 && g_arrived == 1);                         // (once)
    memset(d, 0, sizeof *d); d->phase = 3; d->veh = veh; d->flag = 1; memcpy(d->goal, g_flags[1], 12); g_armored = 1;
    assert(route_exit_tick(0, d, vp) == 0 && g_arrived == 1 && g_exits == 2 && g_inVeh[1] == veh && g_inVeh[0] == veh);   // armour: passengers out, gunner + driver stay
    for (int j = 2; j < 4; j++) g_inVeh[j] = veh;
    memset(d, 0, sizeof *d); d->phase = 3; d->veh = veh; d->flag = 1; memcpy(d->goal, g_flags[1], 12); g_armored = 0;
    float vp2[3] = {342, 0, 0};
    assert(route_exit_tick(0, d, vp2) == 0 && g_arrived == 1 && g_exits == 4 && logged("drops 2 passenger(s)"));   // passengers only: driver drives on
    memset(d, 0, sizeof *d); d->phase = 3; d->veh = veh; d->flag = 0; memcpy(d->goal, g_flags[0], 12);
    float vp3[3] = {0, 0, 601};
    assert(route_exit_tick(0, d, vp3) == 0 && g_arrived == 1);                        // 600 m from its objective: not this one
    g_boat = 1; memset(d, 0, sizeof *d); d->phase = 3; d->veh = veh; d->flag = 1; memcpy(d->goal, g_flags[1], 12);
    assert(route_exit_tick(0, d, vp) == 0 && g_arrived == 1); g_boat = 0;            // (boats land by their own rules)
    // ---- boarding spots / fleet ----
    float tank[3] = {53, 0, 53}; memcpy(g_vehPos, tank, 12);
    assert(route_board_spot(tank) >= 0 && route_board_bonus(veh, 2) == 500 && route_board_bonus(veh, 1) == 0);
    float far[3] = {80, 0, 80}; memcpy(g_vehPos, far, 12); assert(route_board_bonus(veh, 2) == 0);
    // ---- beacons ----
    float me[3] = {60, 0, 0};
    g_owner[0] = 1; g_owner[1] = 2;
    int k = route_beacon_pick(0, 1, me, g_flags[1]);
    assert(k >= 0 && g_trace.actions[k].p[0] == 250);   // 150 m short of the objective; 380 is beside the enemy flag (20 m), 120 is 280 m out
    g_recon[0] = 1; g_inVeh[0] = NULL; memset(&g_raBeacon[0], 0, sizeof g_raBeacon[0]);
    char *bs = soldiers[1]; memcpy(bs, me, 12);
    int any = 0;   // half of the lives: find one that goes
    for (int life = 0; life < 12 && !any; life++) {
        g_raBeacon[0].soldier = NULL; g_raBeacon[0].lives = (unsigned)life;
        if (route_beacon_tick(0, &g_brain[0], bs, g_flags[1])) any = 1;
    }
    assert(any && g_raBeacon[0].state == RB_GOING && logged("heads for a recorded beacon spot"));
    assert(g_nav[0].mode == 3 && g_raBeacon[0].planned);    // routed there (the stub walks it at once)
    assert(route_beacon_tick(0, &g_brain[0], bs, g_flags[1]) && g_raBeacon[0].state == RB_PLACING && g_placed == 1 && fabsf(g_nav[0].targetYaw - 4.1f) < 0.01f);
    H->frame += 81;
    assert(!route_beacon_tick(0, &g_brain[0], bs, g_flags[1]) && g_raBeacon[0].state == RB_DONE && g_raBeaconsSet == 1 && logged("sets a radio beacon"));
    assert(!route_beacon_tick(0, &g_brain[0], bs, g_flags[1]));   // once a life
    g_recon[1] = 1; g_inVeh[1] = NULL; memcpy(soldiers[2], me, 12);
    memset(&g_raBeacon[1], 0, sizeof g_raBeacon[1]); g_raBeacon[1].state = RB_UNDECIDED; g_raBeacon[1].soldier = soldiers[2]; g_raBeacon[1].spot = -1;
    route_beacon_tick(1, &g_brain[1], soldiers[2], g_flags[1]);
    assert(g_raBeacon[1].state == RB_DONE || (g_raBeacon[1].spot >= 0 && g_trace.actions[g_raBeacon[1].spot].p[0] != 250));   // the spot just used is not taken again for 5 min
    puts("PASS: route actions in the core - a recorded wait held for its seconds (not under fire, not on an errand, once per point), crouch sections walked crouched and stood up after; "
         "exit points: a light vehicle's everyone-out point = arriving there, passengers-only points drop the bot passengers and drive on, armour keeps its crew, not 150+ m from the objective, not boats; "
         "the fleet prefers a vehicle on its team's boarding spot; a recon picks the beacon spot nearest the objective (not by an enemy flag), walks there, faces the recorded way, places it, once a life");
    return 0;
}
'''


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    print(r.stdout, end='')
    if r.returncode:
        print(r.stderr); raise SystemExit('FAIL: %s exited %d' % (Path(cmd[0]).name, r.returncode))
    return r.stdout


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    san = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g'] if a.sanitize else ['-O2']
    with tempfile.TemporaryDirectory(prefix='bf3-altbotmod-v15-') as tmp:
        tmp = Path(tmp)
        cc = ['gcc', '-std=gnu11', '-Wall', '-Wno-unused-function', '-Wno-unused-variable', '-I', str(SRC)] + san
        (tmp / 'parse.c').write_text(PARSE)
        subprocess.run(cc + [str(tmp / 'parse.c'), str(SRC / 'trace_nav.c'), '-lm', '-o', str(tmp / 'parse')], check=True)
        run([str(tmp / 'parse'), str(TRACES)])
        (tmp / 'labels.c').write_text(LABELS)
        subprocess.run(cc + [str(tmp / 'labels.c'), str(SRC / 'trace_nav.c'), '-lm', '-o', str(tmp / 'labels')], check=True)
        run([str(tmp / 'labels'), str(TRACES / 'MP_011_ConquestLarge0.map')])
        (tmp / 'native.c').write_text(NATIVE)
        subprocess.run(cc + [str(tmp / 'native.c'), str(SRC / 'trace_nav.c'), str(SRC / 'enhanced.c'), str(SRC / 'air.c'), str(SRC / 'modes.c'), '-lm', '-o', str(tmp / 'native')], check=True)
        run([str(tmp / 'native')])
    # the new route files: every mode BF3 plays on these levels that Fun-Bots recorded
    names = {p.name for p in TRACES.glob('*.map')}
    modes = {}
    for n in names:
        m = re.match(r'(.+)_([A-Za-z]+\d)\.map$', n)
        if m: modes.setdefault(m.group(2), []).append(m.group(1))
    for want, count in (('RushLarge0', 25), ('SquadRush0', 15), ('SquadDeathMatch0', 29), ('Domination0', 4), ('GunMaster0', 12), ('TankSuperiority0', 3), ('Scavenger0', 4)):
        assert len(modes.get(want, [])) >= count, (want, len(modes.get(want, [])))
    assert len(names) >= 176 and not any(p.stat().st_size < 1000 for p in TRACES.glob('*.map'))
    tn = (SRC / 'trace_native.inc').read_text(); core = (SRC / 'core.c').read_text()
    assert '"RushLarge0", "SquadRush0", "TeamDeathMatch0", "SquadDeathMatch0", "GunMaster0", "Domination0", "TankSuperiority0"' in tn
    assert 'route_actions_level();' in tn and 'int board = mode == TRACE_VEHICLE ? route_board_spot(target) : -1;' in tn
    assert 'if (route_exit_tick(i, d, vp)) return;' in core and 'if (route_hold_tick(i, soldier, nav)) return;' in core and 'route_beacon_tick(i, b, soldier, flag_pos(of))' in core
    assert 'score-=ROUTE_BOARD_BONUS(v,team);' in (SRC / 'fleet_native.inc').read_text() and '#define ROUTE_BOARD_BONUS(v,team) route_board_bonus(v,team)' in core
    print(f'PASS: route files for every recorded mode - {len(names)} files: Rush {len(modes["RushLarge0"])}, Squad Rush {len(modes["SquadRush0"])}, Squad DM {len(modes["SquadDeathMatch0"])}, '
          f'Domination {len(modes["Domination0"])}, Gun Master {len(modes["GunMaster0"])}, Tank Superiority {len(modes["TankSuperiority0"])}, Scavenger {len(modes["Scavenger0"])} (empty Air Superiority files left out); '
          'the core loads the file of the mode being played, uses boarding spots for vehicle routes, exit points while driving, waits in the path follower, beacon runs before overwatch')
    defend_checks(core, (SRC / 'activity_native.inc').read_text(), (SRC / 'ground_tactics_native.inc').read_text())
    emplacement_checks(core)


def defend_checks(core, act, tactics):
    # the "bots stand in one place and forget what they were doing" fix (seen again on V14, 2026-10-11: [BOT] Medic US 4
    # held a post 186 times in 24 s, "back to defend" after each fight - the 12 s screen beside a tank, made permanent)
    assert core.count('b->taskUntil = b->task == TASK_DEFEND ? defend_until(i) : H->frame + FOLLOW_FRAMES;') == 6
    assert 'TASK_DEFEND ? NEVER' not in core and 'b->taskUntil = NEVER;' not in core
    assert 'b->taskUntil = g_defendUntil[i] = H->frame + BACKUP_DEFEND_FRAMES;' in core and '#define BACKUP_DEFEND_FRAMES (3 * 60 * 30)' in core
    assert 'b->taskUntil=g_defendUntil[j]=H->frame+12*30;' in tactics
    assert '(b->commander && !is_bot(b->commander)) || (b->prevCommander && !is_bot(b->prevCommander))' in act and 'b->commander || b->prevCommander ||' not in act
    fn = re.search(r'static unsigned defend_until\(int i\) \{[^\n]*\}', core).group(0)
    with tempfile.TemporaryDirectory(prefix='bf3-altbotmod-v15d-') as tmp:
        c = Path(tmp) / 'd.c'
        c.write_text('#include <assert.h>\n#include <stdio.h>\n#define MAX_BOTS 4\nstatic struct { unsigned frame; } host = {1000}, *H = &host;\nstatic unsigned g_defendUntil[MAX_BOTS];\n' + fn +
                     '\nint main(void){ g_defendUntil[1]=1360; assert(defend_until(1)==1360); assert(defend_until(0)==1000); assert(defend_until(-1)==1000); puts("ok"); return 0; }\n')
        subprocess.run(['gcc', '-std=gnu11', '-Wall', str(c), '-o', str(c.with_suffix(''))], check=True)
        assert run([str(c.with_suffix(''))]).strip() == 'ok'
    print('PASS: defend jobs end - a defend job keeps its own deadline when it resumes after a fight / cover / revive / repair / rocket / supply run (V9-V14: for ever - '
          'the 12 s screen beside a tank became a post held all round); a player\'s "need backup" holds 3 minutes; the activity watchdog only spares bots on a player\'s order (not a bot\'s)')



def emplacement_checks(core):
    # V5-V14: brain_tick's "riding with no one to follow: get out and play" (every 60 frames, so every bot with an even slot)
    # threw the gunner out of its TOW / Kornet / C-RAM / Pantsir; on 2026-10-11 2,354 of 2,380 mannings lasted under 3 s
    brain = function(core, 'brain_tick')
    guard = 'if (stationary_weapon(player_vehicle(pl)) || vehicle_is_gunship(player_vehicle(pl))) { g_local_brain_tick_wasRiding[i] = 1; return; }'   # (V23: and the gunship)
    assert brain.count(guard) == 1
    g = brain.index(guard)
    assert brain.index('stranded_in_vehicle_tick(i);') < g < brain.index('rocket_scan(i, soldier);') < brain.index('if (H->frame % 60 == 0) bot_exit_vehicle(i, 1);')
    emp = (SRC / 'emplacement_native.inc').read_text()
    assert 'H->frame - g_empIdleSince[i] > EMPLACEMENT_IDLE || b->commander || !b->enabled' in emp and '#define EMPLACEMENT_IDLE    (15 * 30)' in emp
    print('PASS: stationary weapons - the bot brain no longer throws a gunner out of its TOW / Kornet / C-RAM / Pantsir every 2 s (V5-V14: 2,354 of 2,380 '
          'mannings on 2026-10-11 lasted under 3 s); it returns before the on-foot scans, and emplacement_tick alone ends the job (40 s with nothing to shoot, or an order)')


if __name__ == '__main__':
    main()
