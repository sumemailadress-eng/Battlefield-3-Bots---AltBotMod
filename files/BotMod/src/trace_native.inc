// Caspian V2: Fun-Bots routes for infantry (see trace_nav.h).
// Files: <server>\BotMod_traces\<Level>_<Mode>.map, copied from Fun-Bots'
// mapfiles folder. When the exact game mode has no file, another file of the
// same level is used: the recorded paths are geometry, flags are matched by
// position, so any mode's paths are valid walking routes on that level.
#define TRACE_FLAG_RADIUS     22.0f   // waypoints this close to a flag count as "at the flag"
#define TRACE_TARGET_NEAR     45.0f   // a route target this close to a flag uses that flag's routes
#define TRACE_MIN_TRIP        8.0f    // shorter trips: a checked straight walk, else the local navigation grid (V2: 60; V22: 15; V23: 8 - Fun-Bots routes at all times)
#define TRACE_JOIN_H          45.0f   // the bot walks at most this far to join a route (AltBotMod V6: 30 -> 45)
#define TRACE_JOIN_V           6.0f    // (V6: 4 -> 6 - Kharg's hillsides)
#define TRACE_GRID_JOIN_H     90.0f   // V6: further than a straight walk - the grid walks it to the route
#define TRACE_SPACING          4.0f
#define TRACE_FLOOR_RISE       2.5f    // AltBotMod V18.2: a target this much higher / lower is on another floor ...
#define TRACE_MIN_TRIP_FLOOR   8.0f    // ... and a recorded route is taken from this far
static TraceNet g_trace;
static void route_actions_level(void); static void route_actions_reset(void); static void route_actions_stats(void);   // AltBotMod V15
static int route_board_spot(const float *vp);
static unsigned g_raBoardRoutes; static unsigned char g_raCrouched[MAX_BOTS];
static TraceNet g_traceLand, g_traceWater;   // AltBotMod V6: Fun-Bots vehicle paths (land vehicles / boats)
static unsigned g_traceTried, g_traceNoField, g_traceNoJoin, g_traceGridJoins, g_traceVehRoutes;
static void trace_points_reset(void);
static char g_traceKey[64];
static unsigned g_traceRoutes, g_traceFallbacks;
typedef struct TraceUse { unsigned at; int flag; float from[3]; } TraceUse;
static TraceUse g_traceUse[MAX_BOTS];
static unsigned char g_traceRepeat[MAX_BOTS];   // AltBotMod V22: routes from this spot to this goal that got the bot nowhere
static const char *const g_traceModes[] = {
    "ConquestLarge0", "ConquestSmall0", "ConquestAssaultLarge0", "ConquestAssaultSmall0", "ConquestAssaultSmall1",
    "RushLarge0", "SquadRush0", "TeamDeathMatch0", "SquadDeathMatch0", "GunMaster0", "Domination0", "TankSuperiority0",
    "Scavenger0", "CaptureTheFlag0", "AirSuperiority0", NULL   // AltBotMod V18.2
};
#define TRACE_MIN_USABLE 50   // AltBotMod V18.2: fewer infantry waypoints = a placeholder (Fun-Bots' Air Superiority files: one point)
static unsigned g_traceCompanionNodes;
// key: the file to load; want: the level_mode being played - its companion (BotMod_traces\companions\<want>.map, the
// level's other recordings within this mode's area) is attached when it was built for this file
static int trace_try_file(const char *key, const char *want)
{
    char dir[MAX_PATH], path[MAX_PATH + 96], comp[MAX_PATH + 128];
    GetModuleFileNameA(NULL, dir, sizeof dir);
    *strrchr(dir, '\\') = 0;
    snprintf(path, sizeof path, "%s\\BotMod_traces\\%s.map", dir, key);
    snprintf(comp, sizeof comp, "%s\\BotMod_traces\\companions\\%s.map", dir, want);
    // AltBotMod V21: plus the routes human players showed on this map (BotMod_learned)
    size_t lf = 0, ll = 0, lw = 0;
    const char *learnFoot = bm_learn_routes ? bm_learn_routes(want, 0, &lf) : NULL;
    if (!tn_load_layers(&g_trace, path, comp, learnFoot, lf, 0)) return 0;
    if (g_trace.primaryNodes < TRACE_MIN_USABLE) {
        bot_log("trace: %s has only %d waypoint(s) - a placeholder, not routes; skipped", path, g_trace.primaryNodes);
        tn_free(&g_trace); return 0;
    }
    g_traceCompanionNodes = (unsigned)g_trace.companionNodes;
    bot_log("trace: loaded Fun-Bots routes %s: %d waypoints, %d paths, %d links (+%d joins), %d vehicle paths kept apart",
            path, g_trace.count, g_trace.pathCount, g_trace.linkCount, g_trace.proximityLinks, g_trace.vehiclePaths);
    if (g_trace.companionNodes)
        bot_log("trace: + %d waypoint(s) from the level's other Fun-Bots recordings within this mode's area (%s)", g_trace.companionNodes, comp);
    if (g_trace.learnedNodes) bot_log("trace: + %d waypoint(s) from routes human players showed (learned)", g_trace.learnedNodes);
    tn_free(&g_traceLand); tn_free(&g_traceWater);
    const char *learnLand = bm_learn_routes ? bm_learn_routes(want, 1, &ll) : NULL, *learnWater = bm_learn_routes ? bm_learn_routes(want, 2, &lw) : NULL;
    if (tn_load_layers(&g_traceLand, path, NULL, learnLand, ll, TN_KIND_LAND))
        bot_log("trace: %d land-vehicle route(s) (%d waypoints, %d learned from players) for drivers", g_traceLand.pathCount, g_traceLand.count, g_traceLand.learnedNodes);
    if (tn_load_layers(&g_traceWater, path, NULL, learnWater, lw, TN_KIND_WATER))
        bot_log("trace: %d boat route(s) (%d waypoints, %d learned from players) for boats", g_traceWater.pathCount, g_traceWater.count, g_traceWater.learnedNodes);
    // AltBotMod V21: learned killing grounds - recorded routes through them cost up to 2.5x (bots take another way when
    // there is one; nothing is closed)
    if (bm_learn_danger) {
        int costly = 0;
        for (int a = 0; a < g_trace.count; a++)
            for (int e = g_trace.edgeStart[a]; e < g_trace.edgeStart[a + 1]; e++) {
                const float *p = g_trace.nodes[a].p, *q = g_trace.nodes[g_trace.edgeTo[e]].p;
                float d = bm_learn_danger((p[0] + q[0]) * 0.5f, (p[2] + q[2]) * 0.5f);
                if (d > 0.05f) { g_trace.edgeCost[e] *= 1.0f + 1.5f * d; costly++; }
            }
        if (costly) bot_log("trace: %d route link(s) through learned killing grounds cost more", costly / 2);
    }
    route_actions_level();   // AltBotMod V15
    return 1;
}
// Called when a level becomes ready (after the nav grid is chosen).
static void trace_level_load(void)
{
    char key[64]; snprintf(key, sizeof key, g_modeName[0] ? "%s_%s" : "%s", g_level, g_modeName);
    if (!strcmp(key, g_traceKey)) return;
    tn_free(&g_trace); tn_free(&g_traceLand); tn_free(&g_traceWater); memset(g_traceUse, 0, sizeof g_traceUse); trace_points_reset();
    route_actions_reset();   // AltBotMod V15
    snprintf(g_traceKey, sizeof g_traceKey, "%s", key);
    if (!g_enhanced.funBotsRoutes) { bot_log("trace: Fun-Bots routes disabled (FunBotsRoutes=0)"); return; }
    if (trace_try_file(key, key)) return;
    // AltBotMod V15: the nearest kind of play first - Gun Master / Squad Deathmatch borrow the TDM paths, Squad Rush the
    // Rush ones, the flag modes the Conquest ones - then any other file of the level.
    static const char *const teamPlay[] = {"TeamDeathMatch0", "SquadDeathMatch0", "GunMaster0", "Domination0", "Scavenger0", "CaptureTheFlag0", NULL};
    static const char *const rushPlay[] = {"SquadRush0", "RushLarge0", NULL};
    const char *const *first = strstr(g_modeName, "Rush") ? rushPlay :
        strstr(g_modeName, "DeathMatch") || strstr(g_modeName, "GunMaster") || strstr(g_modeName, "Scavenger") || strstr(g_modeName, "CaptureTheFlag") ? teamPlay : NULL;
    for (int pass = first ? 0 : 1; pass < 2; pass++)
        for (int k = 0; (pass ? g_traceModes : first)[k]; k++) {
            char other[64]; snprintf(other, sizeof other, "%s_%s", g_level, (pass ? g_traceModes : first)[k]);
            if (strcmp(other, key) && trace_try_file(other, key)) { bot_log("trace: no file for %s; using %s (same level)", key, other); return; }
        }
    bot_log("trace: no Fun-Bots routes for %s (BotMod_traces\\%s.map) - navigation grid only", g_level, key);
}

// Caspian V3: routes to any point near the recorded network, not only to flags: a vehicle, a cover or firing
// spot, a squad mate. Each needs one Dijkstra over the network (~1 ms on 7,000 waypoints), cached per point
// (32 slots, least recently used replaced, a goal within 8 m reuses a slot). At most 2 new ones per tick.
#define TRACE_POINT_RADIUS    15.0f
#define TRACE_VEHICLE_RADIUS  40.0f   // V4: a jet parked 34 m from the nearest recorded waypoint had no route
#define TRACE_POINT_REUSE      8.0f
#define TRACE_POINT_PER_TICK   2
static float g_tracePointGoal[TN_MAX_FIELDS - TN_FLAG_FIELDS][3];
static unsigned g_tracePointUsed[TN_MAX_FIELDS - TN_FLAG_FIELDS];
static signed char g_tracePointState[TN_MAX_FIELDS - TN_FLAG_FIELDS];   // 0 empty, 1 ready, -1 no waypoint near it
static float g_tracePointRadius[TN_MAX_FIELDS - TN_FLAG_FIELDS];
static unsigned g_tracePointFrame; static int g_tracePointBuilt;
static unsigned g_tracePointRoutes;
static void trace_points_reset(void)
{
    memset(g_tracePointGoal, 0, sizeof g_tracePointGoal); memset(g_tracePointUsed, 0, sizeof g_tracePointUsed);
    memset(g_tracePointState, 0, sizeof g_tracePointState); g_tracePointBuilt = 0;
}
// Field id for a route to `goal`, -1 = none (no waypoint near it), -2 = not this tick (budget).
static int trace_point_field(const float *goal, float radius)
{
    int slots = TN_MAX_FIELDS - TN_FLAG_FIELDS, lru = 0;
    for (int k = 0; k < slots; k++) {
        if (g_tracePointState[k] && g_tracePointRadius[k] == radius && fabsf(g_tracePointGoal[k][1] - goal[1]) < 4 &&
            (g_tracePointGoal[k][0] - goal[0]) * (g_tracePointGoal[k][0] - goal[0]) +
            (g_tracePointGoal[k][2] - goal[2]) * (g_tracePointGoal[k][2] - goal[2]) < TRACE_POINT_REUSE * TRACE_POINT_REUSE) {
            g_tracePointUsed[k] = H->frame ? H->frame : 1;
            return g_tracePointState[k] > 0 ? TN_FLAG_FIELDS + k : -1;
        }
        if (g_tracePointUsed[k] < g_tracePointUsed[lru]) lru = k;
    }
    if (g_tracePointFrame != H->frame) { g_tracePointFrame = H->frame; g_tracePointBuilt = 0; }
    if (g_tracePointBuilt >= TRACE_POINT_PER_TICK) return -2;
    g_tracePointBuilt++;
    memcpy(g_tracePointGoal[lru], goal, 12); g_tracePointUsed[lru] = H->frame ? H->frame : 1;
    g_tracePointRadius[lru] = radius;
    g_tracePointState[lru] = tn_field(&g_trace, TN_FLAG_FIELDS + lru, goal, radius) ? 1 : -1;
    return g_tracePointState[lru] > 0 ? TN_FLAG_FIELDS + lru : -1;
}

// Fill g_paths[i] with a Fun-Bots route towards (tx,ty,tz). Returns the number of points, 0 = use the grid.
static int trace_plan_ex(int i, char *soldier, float tx, float ty, float tz, int mode);
static unsigned g_traceJoinDeferred, g_traceJoinMemo, g_joinFailAt[MAX_BOTS]; static int g_joinFailId[MAX_BOTS]; static float g_joinFailPos[MAX_BOTS][3];   // AltBotMod V32
static int trace_plan(int i, char *soldier, float tx, float ty, float tz) { return trace_plan_ex(i, soldier, tx, ty, tz, 1); }
// mode 1: flags use their own routes. TRACE_VEHICLE: a route to this very point even when it is near a flag
// (vehicles parked at flags), ending up to 40 m from it.
#define TRACE_VEHICLE 2
static int trace_plan_ex(int i, char *soldier, float tx, float ty, float tz, int mode)
{
    if (i < 0 || i >= MAX_BOTS || !g_trace.count || !g_enhanced.funBotsRoutes || !isfinite(tx) || !isfinite(ty) || !isfinite(tz)) return 0;
    float target[3] = {tx, ty, tz}, *pos = (float *)(soldier + OFF_SOLDIER_POSITION);
    // AltBotMod V15: a vehicle standing on its recorded boarding spot - the route ends exactly there (the recorded path in)
    int board = mode == TRACE_VEHICLE ? route_board_spot(target) : -1;
    if (board >= 0) { memcpy(target, g_trace.actions[board].p, 12); tx = target[0]; ty = target[1]; tz = target[2]; }
    // AltBotMod V18.2: a trip to another floor takes the recorded route from 8 m: the recording knows the stairs, the grid
    // inside a building often does not (Office: bots on the grid faced the stairwell wall)
    float tripMin = ty != 0.0f && fabsf(ty - pos[1]) >= TRACE_FLOOR_RISE ? TRACE_MIN_TRIP_FLOOR : TRACE_MIN_TRIP;   // (0: height not given)
    if (dist2(pos, target) < tripMin * tripMin) return 0;
    g_traceTried++;
    int flag = -1; float best = TRACE_TARGET_NEAR * TRACE_TARGET_NEAR;
    for (int f = 0; mode == 1 && f < g_flagCount && f < TN_FLAG_FIELDS; f++) { float d = dist2(flag_pos(f), target); if (d < best) { best = d; flag = f; } }
    int id = flag;
    if (flag >= 0 && !tn_field(&g_trace, flag, flag_pos(flag), TRACE_FLAG_RADIUS)) id = -1;
    if (id < 0) {   // not a flag (or the flag has no waypoints): a route to this very point
        id = trace_point_field(target, board >= 0 ? 4.0f : mode == TRACE_VEHICLE ? TRACE_VEHICLE_RADIUS : TRACE_POINT_RADIUS);
        if (id == -2) { g_lastPlanDeferred = 1; return 0; }   // V6: no new route field this tick - wait for one, not the grid
        if (id < 0) { g_traceNoField++; return 0; }   // no waypoint near it: the grid
    }
    // A route from (almost) the same spot to the same goal that did not get
    // the bot anywhere: let the grid try this time.
    // AltBotMod V22: Fun-Bots routes over the grid - the route is tried again from the next waypoint along (another way onto
    // the network), twice, before the grid gets one go (V21.1 handed the bot to the grid at once: Office 638 times in 3 min,
    // and the grid walked them into the walls and past the stairs).
    TraceUse *u = &g_traceUse[i];
    int skip = 0;
    if (u->at && u->flag == id && H->frame - u->at < 25 * 30 && dist2(u->from, pos) < 8 * 8) {
        if (g_traceRepeat[i] >= 2) { g_traceFallbacks++; u->at = 0; g_traceRepeat[i] = 0; return 0; }
        skip = ++g_traceRepeat[i];
    } else g_traceRepeat[i] = 0;
    int cand[8], n = tn_candidates(&g_trace, id, pos, TRACE_JOIN_H, TRACE_JOIN_V, cand, 8);
    if (skip && n > 1) { int k = skip % n; int t0 = cand[0]; cand[0] = cand[k]; cand[k] = t0; }
    for (int c = 0, rays = 0; c < n && rays < 5; c++) {
        const float *node = g_trace.nodes[cand[c]].p;
        if (dist2(pos, node) > 1.5f * 1.5f) {
            rays++;
            float feet[3] = {pos[0], pos[1], pos[2]}, to[3] = {node[0], node[1], node[2]};
            if (!nav_segment_clear(ray_cast, feet, to)) continue;
        }
        BotPath *p = &g_paths[i];
        int truncated = 0; float length = 0;
        int count = tn_route(&g_trace, id, cand[c], TRACE_SPACING, p->pts, p->wpFlags, PATH_MAX_POINTS, &truncated, &length);
        if (count < 2) continue;
        const float *end = &p->pts[(count - 1) * 3];
        p->count = count;
        p->partial = truncated || dist2(end, target) > 4.0f * 4.0f;   // the follower re-plans the last leg on arrival
        for (int k = 0; k < count; k++) p->link[k] = -1;
        p->climbAt = 0; p->climbPhase = 0; p->next = 0;
        p->goalX = tx; p->goalY = ty; p->goalZ = tz;
        p->lastX = pos[0]; p->lastZ = pos[2]; p->lastProgressFrame = H->frame;
        u->at = H->frame ? H->frame : 1; u->flag = id; memcpy(u->from, pos, 12);
        nav_cancel_path(i);   // Caspian V3: drop a grid request queued earlier for this bot
        route_wait_reset(i); if (g_raCrouched[i]) { g_raCrouched[i] = 0; bot_stand(i); }   // AltBotMod V15: a new route
        if (board >= 0 && g_raBoardRoutes++ % 10 == 0)
            bot_log("trace: %s takes the recorded boarding path to a vehicle (%d points, %.0f m; %u such routes)", H->bots[i] + OFF_PLAYER_ONLINE_NAME, count, length, g_raBoardRoutes);
        g_traceRoutes++; if (id >= TN_FLAG_FIELDS) g_tracePointRoutes++;
        if (H->traceInput || g_traceRoutes % 25 == 1) {
            if (id < TN_FLAG_FIELDS)
                bot_log("trace: %s takes a Fun-Bots route to flag %d (%d points, %.0f m%s)", H->bots[i] + OFF_PLAYER_ONLINE_NAME, flag, count, length, truncated ? ", continues" : "");
            else
                bot_log("trace: %s takes a Fun-Bots route to (%.0f, %.0f) (%d points, %.0f m%s; %u routes, %u to points other than flags)",
                        H->bots[i] + OFF_PLAYER_ONLINE_NAME, tx, tz, count, length, truncated ? ", continues" : "", g_traceRoutes, g_tracePointRoutes);
        }
        return count;
    }
    // AltBotMod V6: Fun-Bots routes first - when no waypoint is a clear straight walk away (inside an HQ building, round a
    // hill), the navigation grid walks the bot to the best waypoint within 90 m and the recorded route takes it from there.
    // (V5 on Kharg: one Fun-Bots route in four minutes; bots stuck at both HQs re-planned all round.)
    // AltBotMod V32: the grid found no way onto the network from (about) here in the last 10 s: not again yet
    if (g_joinFailAt[i] && H->frame - g_joinFailAt[i] < 10 * 30 && g_joinFailId[i] == id && dist2(g_joinFailPos[i], pos) < 6.0f * 6.0f) { g_traceNoJoin++; g_traceJoinMemo++; return 0; }
    {
        NavGrid *g = *nav_slot();
        // AltBotMod V18: the three best waypoints, not only the nearest - from inside an Office room the nearest one was
        // often behind a wall the grid has no way through (236 "no way onto a route" a minute; the grid then walked them)
        int gc[3], gn = g && g->phase == 2 ? tn_candidates(&g_trace, id, pos, TRACE_GRID_JOIN_H, 10.0f, gc, 3) : 0;
        for (int gk = 0; gk < gn; gk++) {
            gc[0] = gc[gk];
            BotPath *p = &g_paths[i];
            float tmp[PATH_MAX_POINTS * 3]; unsigned char tf[PATH_MAX_POINTS];
            int truncated = 0; float length = 0;
            int tcount = tn_route(&g_trace, id, gc[0], TRACE_SPACING, tmp, tf, PATH_MAX_POINTS / 2, &truncated, &length);
            if (tcount >= 2) {
                const float *node = g_trace.nodes[gc[0]].p;
                int lead = budgeted_nav_path(i, g, ray_cast, pos[0], pos[1], pos[2], node[0], node[1], node[2], p->pts, PATH_MAX_POINTS - tcount);
                if (!lead && g_lastPlanDeferred) return 0;
                if (lead && !nav_last_partial()) {
                    for (int k = 0; k < lead; k++) { p->link[k] = (short)nav_path_link(k); p->wpFlags[k] = 0; }
                    memcpy(p->pts + lead * 3, tmp, (size_t)tcount * 12);
                    for (int k = 0; k < tcount; k++) { p->link[lead + k] = -1; p->wpFlags[lead + k] = tf[k]; }
                    int count = lead + tcount;
                    const float *end = &p->pts[(count - 1) * 3];
                    p->count = count; p->partial = truncated || dist2(end, target) > 4.0f * 4.0f;
                    p->climbAt = 0; p->climbPhase = 0; p->next = 0;
                    p->goalX = tx; p->goalY = ty; p->goalZ = tz;
                    p->lastX = pos[0]; p->lastZ = pos[2]; p->lastProgressFrame = H->frame;
                    u->at = H->frame ? H->frame : 1; u->flag = id; memcpy(u->from, pos, 12);
                    route_wait_reset(i); if (g_raCrouched[i]) { g_raCrouched[i] = 0; bot_stand(i); }   // AltBotMod V15
                    g_traceRoutes++; g_traceGridJoins++;
                    if (H->traceInput || g_traceGridJoins % 20 == 1)
                        bot_log("trace: %s walks %d grid point(s) to a Fun-Bots route, then follows it (%d points, %.0f m; %u such joins)",
                                H->bots[i] + OFF_PLAYER_ONLINE_NAME, lead, tcount, length, g_traceGridJoins);
                    return count;
                }
            }
            if (!plan_budget_left()) { g_lastPlanDeferred = 1; g_traceJoinDeferred++; return 0; }   // (V32: wait for the next tick's budget - V6-V31 counted it as "no way onto a route" and walked the grid)
        }
    }
    g_traceNoJoin++;
    g_joinFailAt[i] = H->frame ? H->frame : 1; g_joinFailId[i] = id; memcpy(g_joinFailPos[i], pos, 12);   // (V32)
    return 0;
}
// AltBotMod V22: a recorded boat lane serves flag f from `from` - its field reaches the flag (within 150 m of it), a lane
// waypoint is within 120 m of `from`, and the flag is at least 300 m away.
static int boat_lane_to(int f, const float *from)
{
    if (!g_traceWater.count || !g_enhanced.funBotsRoutes || f < 0 || f >= TN_FLAG_FIELDS || f >= objective_count() || !from) return 0;
    const float *fp = objective_pos(f);
    if (!fp || dist2(fp, from) < 300.0f * 300.0f || !tn_field(&g_traceWater, f, fp, 150.0f)) return 0;
    int cand[1];
    return tn_candidates(&g_traceWater, f, from, 120.0f, 40.0f, cand, 1) > 0;
}
// AltBotMod V6: a driver / boat follows a Fun-Bots VEHICLE route to its flag when one starts within 45 m (land) or
// 60 m (boats) of it. Returns the number of points written into pts (0 = use the drivable grid). Land routes end
// within 45 m of the flag; boat routes end on the water nearest it (up to 150 m: the beach run is on foot).
// alt 0: the route from the nearest recorded waypoint (within 45 m). AltBotMod V14: alt 1-3 (the driving map found no
// way) - the route from another road's waypoint (at least 15 m from the ones before), searched further out each time
// (70 / 95 / 120 m), and a longer detour is accepted.
static int trace_vehicle_route(int boat, int flag, const float *flagPos, const float *vp, float *pts, int maxPts, int *partial, float *lengthOut, int alt)
{
    TraceNet *t = boat ? &g_traceWater : &g_traceLand;
    if (!t->count || !g_enhanced.funBotsRoutes || flag < 0 || flag >= TN_FLAG_FIELDS || !flagPos) return 0;
    if (!tn_field(t, flag, flagPos, boat ? 150.0f : 45.0f)) return 0;
    float reach = (boat ? 100.0f : 90.0f) + alt * 30.0f;   // AltBotMod V23: Fun-Bots roads first - joined from 90 m (V6-V22: 45 m, else the driving grid)
    int cand[8], n = tn_candidates(t, flag, vp, reach, boat ? 12.0f : 8.0f + alt * 2.0f, cand, 8);
    if (!n) return 0;
    int distinct[8], nd = 0;   // start points at least 15 m apart (neighbouring waypoints of one road give the same route)
    for (int k = 0; k < n; k++) {
        int close = 0;
        for (int j = 0; j < nd && !close; j++) { const float *a = t->nodes[cand[k]].p, *b = t->nodes[distinct[j]].p; close = (a[0] - b[0]) * (a[0] - b[0]) + (a[2] - b[2]) * (a[2] - b[2]) < 15.0f * 15.0f; }
        if (!close) distinct[nd++] = cand[k];
    }
    if (alt >= nd) return 0;   // (no further road to start from)
    static unsigned char flags[256];
    int truncated = 0; float length = 0;
    int count = tn_route(t, flag, distinct[alt], 6.0f, pts, flags, maxPts > 256 ? 256 : maxPts, &truncated, &length);
    if (count < 2) return 0;
    float straight = sqrtf(dist2(vp, flagPos));
    if (length > straight * (alt ? 3.5f : 2.5f) + (alt ? 350.0f : 200.0f)) return 0;   // a detour round the whole map: the grid knows better
    if (partial) *partial = truncated;
    if (lengthOut) *lengthOut = length;
    g_traceVehRoutes++;
    return count;
}
static int trace_loaded(void) { return g_trace.count > 0 && g_enhanced.funBotsRoutes; }
// Once a minute: how infantry routes were found (to see from the log whether the Fun-Bots routes carry the bots).
static void trace_stats_tick(void)
{
    static unsigned at;
    if (!g_trace.count || H->frame - at < 60 * 30) return;
    at = H->frame;
    bot_log("trace: Fun-Bots routes %u (%u via a grid walk to the route), vehicle routes %u; tried %u, no waypoint near the goal %u, no way onto a route %u (%u of them not retried within 10 s), fallbacks %u; %u join(s) waited for the planning budget",
            g_traceRoutes, g_traceGridJoins, g_traceVehRoutes, g_traceTried, g_traceNoField, g_traceNoJoin, g_traceJoinMemo, g_traceFallbacks, g_traceJoinDeferred);
    route_actions_stats();   // AltBotMod V15
    zone_stats();   // AltBotMod V34
}
// Recorded jumps: press jump when the next point was recorded as a jump and is higher than the feet.
static void trace_jump_tick(int i, char *soldier)
{
    BotPath *p = &g_paths[i];
    if (bot_nav(i)->mode != 3 || p->next >= p->count || !(p->wpFlags[p->next] & TN_FLAG_JUMP)) return;
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION), *w = &p->pts[p->next * 3];
    float dx = w[0] - pos[0], dz = w[2] - pos[2];
    if (dx * dx + dz * dz < 3.0f * 3.0f && w[1] - pos[1] > 0.3f && (H->frame + i) % 10 == 0) press_button(i, 12, 4);
}
