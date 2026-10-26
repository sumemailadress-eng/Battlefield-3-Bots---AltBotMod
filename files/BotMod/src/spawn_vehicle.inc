#ifndef FLEET_BOAT_NOT_NEEDED
#define FLEET_BOAT_NOT_NEEDED(v,from,f) 0
#endif
// AltBotMod V6: spawning straight into a vehicle, the way a player picks a jet, a helicopter, a tank or a boat on the
// spawn screen. A dead bot about to respawn takes the driver / pilot seat of a parked friendly vehicle at its side's
// base: it spawns as usual and crew_board_tick puts it in the seat with EnterVehicle (which works at any distance -
// the V5 crew seats proved it live). air_consider / fleet_takeover then adopt the seated bot as pilot / driver.
// Kharg Island, V5: every jet and helicopter was "claimed" by a bot that then could not walk to it ("cannot route
// to Su35 1294 m away"), so no aircraft ever flew; boats and tanks at the bases were left standing too.
#define VSPAWN_GROUND_BASE   300.0f   // ground vehicles / boats: only those this close to the team's HQ (base vehicles)
#define VSPAWN_WALKER_NEAR   120.0f   // a bot already this close to it, on its way, keeps it
static unsigned g_vspawnCount, g_vspawnFails;
static int emplacement_threat(int kind, int team, const float *at);   // emplacement_native.inc
static int vspawn_rank(char *v)
{
    const BMAirProfile *a = bm_air_profile(vehicle_name(v));
    if (a) return a->kind == BM_AIR_JET ? 60 : a->kind == BM_AIR_ATTACK ? 55 : a->kind == BM_AIR_SCOUT ? 45 : 40;
    const BMVehicleProfile *p = bm_vehicle_profile(vehicle_name(v));
    if (p->kind == BM_TANK) return 50;
    if (p->kind == BM_IFV || p->kind == BM_AA || p->kind == BM_AMTRAC) return 42;
    if (p->kind == BM_BOAT) return 35;
    return 30;
}
static char *vspawn_pick(char *bot, int team)
{
    if (!mode_uses_vehicles() || !level_ready() || !g_enhanced.driving || g_driveOff || !g_vehInput || g_roundCleanup) return NULL;
    if (team < 1 || team > 2) return NULL;
    const float *own = g_hqCpKnown[team] ? g_hqCpPos[team] : g_hqPos[team];
    const float *other = g_hqCpKnown[3 - team] ? g_hqCpPos[3 - team] : g_hqPos[3 - team];
    int air, jets, ground, total; fleet_counts(team, &air, &jets, &ground, &total);
    char **list; int n = vehicle_list(&list);
    char *best = NULL; int bestRank = 0;
    for (int k = 0; k < n && k < 256; k++) {
        char *v = list[k];
        int emp = stationary_weapon(v);
        if (!vehicle_ok(v) || (!emp && strcmp(fleet_reason(v, team), "available"))) continue;   // (an emplacement has no driving class)
        int vt = 0; float *p = vehicle_pos(v, &vt);
        if (!p) continue;
        if (emp) {
            if (human_near(p, g_enhanced.humanRadius) || vehicle_bad(v)) continue;   // AltBotMod V7: a free AA (Pantsir / C-RAM) or TOW / Kornet emplacement on our side with something to
                     // shoot - an enemy aircraft in the air / an enemy vehicle within 450 m - is taken at the spawn screen too
            if (!driver_seat_free(v) || (vt && vt != team)) continue;
            if (!near_origin(own) && !near_origin(other) && dist2(p, own) > dist2(p, other)) continue;
            int manned = 0;
            for (int j = 0; j < H->botCount; j++) if (player_team(H->bots[j]) == team && stationary_weapon(player_vehicle(H->bots[j]))) manned++;
            int taken = 0;
            for (int j = 0; j < H->botCount; j++) if (H->bots[j] != bot && g_crewVehicle[j] == v && H->frame - g_crewAt[j] < 10 * 30) taken = 1;
            if (taken || manned >= 3 || !emplacement_threat(emp, team, p)) continue;
            int rank = emp == 2 ? 58 : 44;
            if (rank > bestRank) { bestRank = rank; best = v; }
            continue;
        }
        const BMAirProfile *a = bm_air_profile(vehicle_name(v));
        if (a) { if (!fleet_air_slot(team, a)) continue; }
        else if (ground >= g_enhanced.groundDriversPerTeam || !vehicle_is_light(v)) continue;
        // its side: the vehicle's team when it has one, else nearer our HQ than theirs
        if (vt && vt != team) continue;
        if (!vt && !near_origin(own) && !near_origin(other) && dist2(p, own) > dist2(p, other)) continue;
        if (!a && (near_origin(own) || dist2(p, own) > VSPAWN_GROUND_BASE * VSPAWN_GROUND_BASE)) continue;   // a base vehicle
        if (!a && !near_origin(own)) {   // AltBotMod V16: a boat only when the next objective is across water
            int nf = -1; float nd = 1e30f;
            for (int f = 0; f < objective_count(); f++) if (objective_wanted(team, f) && objective_pos(f)) {
                float d = dist2(own, objective_pos(f)); if (d < nd) { nd = d; nf = f; }
            }
            if (nf >= 0 && FLEET_BOAT_NOT_NEEDED(v, own, nf)) continue;
        }
        VehTrack *t = vehicle_sample(v);
        if (t && t->speed > 1.5f) continue;   // parked, not rolling
        int keep = 0;   // a teammate (or a dead bot) already on its way and close
        for (int j = 0; j < H->botCount && !keep; j++) {
            if (H->bots[j] == bot) continue;
            if (g_crewVehicle[j] == v && H->frame - g_crewAt[j] < 10 * 30) keep = 1;
            int walking = (g_fleet[j].vehicle == v && H->frame < g_fleet[j].until) || (g_drive[j].veh == v && g_drive[j].phase) ||
                          (g_air[j].veh == v && g_air[j].phase);
            char *s = *(char **)(H->bots[j] + OFF_PLAYER_SOLDIER);
            if (walking && s && dist2((float *)(s + OFF_SOLDIER_POSITION), p) < VSPAWN_WALKER_NEAR * VSPAWN_WALKER_NEAR) keep = 1;
        }
        if (keep) continue;
        int rank = vspawn_rank(v);
        if (a && a->kind == BM_AIR_JET && jets >= 2) rank -= 30;   // two jets a side first, then the rest
        if (rank > bestRank) { bestRank = rank; best = v; }
    }
    return best;
}
// The seat taken: the bots that were walking to this vehicle let it go.
static void vspawn_release_walkers(int i, char *v)
{
    for (int j = 0; j < H->botCount; j++) {
        if (j == i) continue;
        if (g_fleet[j].vehicle == v) { memset(&g_fleet[j], 0, sizeof g_fleet[j]); g_fleet[j].retryAt = H->frame + 5 * 30; }
        if (g_drive[j].veh == v && (g_drive[j].phase == 1 || g_drive[j].phase == 5)) drive_stop(j, "the vehicle was taken at the spawn screen", 5);
        if (g_air[j].veh == v && g_air[j].phase == AIR_WALK) air_stop(j, "the aircraft was taken at the spawn screen", 0);
    }
}
static int vspawn_board(int i, char *v)
{
    char *pl = H->bots[i];
    if (!vehicle_ok(v) || !driver_seat_free(v) || player_vehicle(pl) || g_air[i].phase) { g_vspawnFails++; return 0; }
    ((EnterVehicle_t)ADDR_PLAYER_ENTER_VEHICLE)(pl, v, 0);
    if (player_vehicle(pl) != v || vehicle_seat_of(v, pl) != 0) {
        g_vspawnFails++;
        bot_log("vehicle-spawn: %s could not take the %s (%u failed)", pl + OFF_PLAYER_ONLINE_NAME, vehicle_name(v), g_vspawnFails);
        return 0;
    }
    g_vspawnCount++;
    vspawn_release_walkers(i, v);
    memset(&g_fleet[i], 0, sizeof g_fleet[i]);
    nav_cancel_path(i); bot_nav(i)->mode = 0; memset(H->botMove[i], 0, sizeof H->botMove[i]);
    end_task(i, bot_brain(i), "spawned into a vehicle");
    g_airCooldown[i] = 0; g_driveCooldown[i] = 0;
    bot_log("vehicle-spawn: %s spawns into the %s (%u so far)", pl + OFF_PLAYER_ONLINE_NAME, vehicle_name(v), g_vspawnCount);
    // air_consider (aircraft) / fleet_takeover (ground, boats) adopt the seated bot this tick
    if (bm_air_profile(vehicle_name(v))) air_consider(i); else fleet_takeover(i);
    return 1;
}
