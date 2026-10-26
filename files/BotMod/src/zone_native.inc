// AltBotMod V34: Fun-Bots' objective walking mesh in the game (zone_nav.h). Files: <server>\BotMod_traces\navzones\
// <Level>_<Mode>.zon (made from Fun-Bots 3.6.0-dev navzones/*.json by the package). Near a capture point, an M-COM or a
// base a bot walks the mesh - points every ~5 m that know the level's walls, floors, stairs and doorways, with the
// corners to walk round between them, recorded jumps and crouch spots - instead of the grid / recorded paths, as Fun-Bots
// dev bots do. Fewer bots stuck at the flag, M-COMs inside buildings reached by the way the mesh knows.
#define ZONE_NEAR_EXTRA    15.0f   // a target this far beyond a zone's radius still counts as "in the zone"
#define ZONE_BOT_EXTRA     50.0f   // the bot this far beyond the radius: close enough to be on the zone's mesh
#define ZONE_JOIN_H         9.0f   // the bot walks at most this far straight to the mesh
#define ZONE_GOAL_H        10.0f
#define ZONE_STUCK_PENALTY 50.0f   // (Fun-Bots: BLOCKED_PENALTY 50, closed after 3)
static ZoneNet g_zone;
static char g_zoneKey[64];
static unsigned g_zoneRoutes, g_zoneNoWay, g_zoneStuck, g_zoneClosed, g_zoneLogged;
static unsigned g_zonePathAt[MAX_BOTS];   // the bot's route came from the mesh (frame), 0: not (declared in core.c)
static int zone_try(const char *key)
{
    char dir[MAX_PATH], path[MAX_PATH + 96];
    GetModuleFileNameA(NULL, dir, sizeof dir);
    *strrchr(dir, '\\') = 0;
    snprintf(path, sizeof path, "%s\\BotMod_traces\\navzones\\%s.zon", dir, key);
    double t = perf_now();
    if (!zn_load(&g_zone, path)) return 0;
    int mcom = 0, cap = 0, base = 0;
    for (int k = 0; k < g_zone.nz; k++) { if (g_zone.zones[k].kind == ZN_KIND_MCOM) mcom++; else if (g_zone.zones[k].kind == ZN_KIND_CAPTURE) cap++; else base++; }
    bot_log("zone: Fun-Bots objective mesh %s: %d points, %d connections, %d zones (%d capture points, %d M-COMs, %d bases) in %.0f ms",
            path, g_zone.np, g_zone.ne, g_zone.nz, cap, mcom, base, perf_now() - t);
    return 1;
}
static void zone_level_load(void)
{
    char key[64]; snprintf(key, sizeof key, g_modeName[0] ? "%s_%s" : "%s", g_level, g_modeName);
    if (!strcmp(key, g_zoneKey)) return;
    zn_free(&g_zone); memset(g_zonePathAt, 0, sizeof g_zonePathAt);
    snprintf(g_zoneKey, sizeof g_zoneKey, "%s", key);
    if (!g_enhanced.funBotsMesh) { bot_log("zone: Fun-Bots objective mesh off (FunBotsMesh=0)"); return; }
    if (zone_try(key)) return;
    // the same kind of play on the level: Squad Rush the Rush mesh, the smaller Conquests the large one (the mesh is the
    // level's ground; its zones are matched to the objectives by position)
    static const char *const alt[] = {"RushLarge0", "SquadRush0", "ConquestLarge0", "ConquestSmall0", "ConquestAssaultLarge0", "ConquestAssaultSmall0", NULL};
    int rush = strstr(g_modeName, "Rush") != NULL, cq = strstr(g_modeName, "Conquest") != NULL;
    if (rush || cq)
        for (int k = 0; alt[k]; k++) {
            if ((rush && !strstr(alt[k], "Rush")) || (cq && !strstr(alt[k], "Conquest"))) continue;
            char other[64]; snprintf(other, sizeof other, "%s_%s", g_level, alt[k]);
            if (strcmp(other, key) && zone_try(other)) { bot_log("zone: no mesh for %s; using %s (same level)", key, other); return; }
        }
    bot_log("zone: no Fun-Bots objective mesh for %s - recorded routes and the grid as before", key);
}
// A route over the mesh when the target lies in an objective's zone and the bot is near it. Returns the route's point
// count (0: not here - the caller plans as before).
static int zone_plan(int i, char *soldier, float tx, float ty, float tz)
{
    if (!g_zone.np || !g_enhanced.funBotsMesh || isnan(ty)) return 0;
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION), to[3] = {tx, ty, tz};
    int zi = zn_zone_near(&g_zone, to, 0, 400.0f);
    if (zi < 0) return 0;
    const ZnZone *zn = &g_zone.zones[zi];
    float rz = zn->radius > 5 ? zn->radius : 20.0f;
    if (dist2(to, zn->c) > (rz + ZONE_NEAR_EXTRA) * (rz + ZONE_NEAR_EXTRA) || dist2(pos, zn->c) > (rz + ZONE_BOT_EXTRA) * (rz + ZONE_BOT_EXTRA)) return 0;
    if (!plan_budget_left()) { g_lastPlanDeferred = 1; return 0; }
    int a = zn_closest(&g_zone, pos, ZONE_JOIN_H, 2.5f), b = zn_closest(&g_zone, to, ZONE_GOAL_H, 3.5f);
    if (a < 0 || b < 0 || g_zone.pts[a].part != g_zone.pts[b].part) return 0;
    int w = walk_clear(pos, g_zone.pts[a].p, ZONE_JOIN_H + 1.0f);   // onto the mesh: a clear straight walk
    if (w < 0) { g_lastPlanDeferred = 1; return 0; }
    if (!w && dist2(pos, g_zone.pts[a].p) > 2.0f * 2.0f) return 0;
    g_plansThisFrame++;
    float pts[(PATH_MAX_POINTS - 2) * 3]; unsigned char fl[PATH_MAX_POINTS - 2]; int partial = 0; float length = 0;
    double t0 = perf_now();
    int n = zn_route(&g_zone, a, b, pts, fl, PATH_MAX_POINTS - 2, &partial, &length);
    perf_add(7, perf_now() - t0);   // (PF_PATH)
    if (!n) { g_zoneNoWay++; return 0; }
    BotPath *p = &g_paths[i];
    nav_cancel_path(i);
    memset(p, 0, sizeof *p);
    int c = 0;
    if (dist2(pos, g_zone.pts[a].p) > 1.0f) { memcpy(&p->pts[0], g_zone.pts[a].p, 12); p->link[0] = -1; c = 1; }
    for (int k = 0; k < n && c < PATH_MAX_POINTS; k++, c++) {
        memcpy(&p->pts[c * 3], &pts[k * 3], 12); p->link[c] = -1;
        p->wpFlags[c] = (unsigned char)((fl[k] & ZN_ROUTE_JUMP ? TN_FLAG_JUMP : 0) | (fl[k] & ZN_ROUTE_CROUCH ? TN_FLAG_CROUCH : 0));
    }
    // the exact target last (the M-COM's arming spot, a guard post) when it is a short, level step from the mesh
    if (!partial && c < PATH_MAX_POINTS && dist2(&p->pts[(c - 1) * 3], to) > 0.5f * 0.5f && dist2(&p->pts[(c - 1) * 3], to) < 6.0f * 6.0f &&
        fabsf(p->pts[(c - 1) * 3 + 1] - ty) < 1.5f) { memcpy(&p->pts[c * 3], to, 12); p->link[c] = -1; c++; }
    p->count = c; p->partial = partial; p->next = 0;
    p->goalX = tx; p->goalY = ty; p->goalZ = tz;
    p->lastX = pos[0]; p->lastZ = pos[2]; p->lastProgressFrame = H->frame;
    g_zonePathAt[i] = H->frame ? H->frame : 1;
    g_zoneRoutes++; g_pathPlans++;
    if (g_zoneLogged < 20 || g_zoneRoutes % 100 == 0) {
        g_zoneLogged++;
        bot_log("zone: %s walks the objective mesh of %s to (%.0f, %.0f) (%d points, %.0f m%s; %u mesh routes so far)", H->bots[i] + OFF_PLAYER_ONLINE_NAME,
                zn->name, tx, tz, c, length, partial ? ", continues" : "", g_zoneRoutes);
    }
    return c;
}
// A bot stuck on a mesh route: that connection costs more for everyone; after 3 times it is closed for the level.
static void zone_stuck(int i, const float *pos)
{
    if (!g_zone.np || !g_zonePathAt[i] || H->frame - g_zonePathAt[i] > 90 * 30) return;
    int e = zn_penalize(&g_zone, pos, ZONE_STUCK_PENALTY);
    if (e < 0) return;
    g_zoneStuck++;
    if (g_zone.edges[e].removed) {
        g_zoneClosed++;
        bot_log("zone: the mesh connection at (%.1f, %.1f) closed for this level (3 bots stuck on it; %u closed)", pos[0], pos[2], g_zoneClosed);
    }
}
// A guard post round an M-COM from its zone's points (the mesh knows which spots near it are walkable and how far round
// the walls they are: Fun-Bots trims an M-COM zone to points whose way to it is short). 0: no zone / no point.
static int zone_guard_point(const float *mcom, unsigned seed, float *out)
{
    int zi = g_zone.np && g_enhanced.funBotsMesh ? zn_zone_near(&g_zone, mcom, ZN_KIND_MCOM, 15.0f) : -1;
    int pt = zi >= 0 ? zn_zone_point(&g_zone, zi, seed) : -1;
    if (pt < 0) return 0;
    memcpy(out, g_zone.pts[pt].p, 12);
    return 1;
}
static void zone_stats(void)
{
    if (g_zone.np) bot_log("zone: %u mesh routes, %u without a way on the mesh, %u stuck reports, %u connections closed (%s)", g_zoneRoutes, g_zoneNoWay, g_zoneStuck, g_zoneClosed, g_zone.name);
}
