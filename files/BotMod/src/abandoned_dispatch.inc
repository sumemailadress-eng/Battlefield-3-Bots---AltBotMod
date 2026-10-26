// Caspian V2: abandoned ground vehicles are put back into the fight.
// A usable, unoccupied, unclaimed land vehicle that is away from its team's
// deployment (left behind by a bailed-out or dead crew) gets the nearest free
// infantry bot within ABANDONED_REACH metres, regardless of the minimum-trip
// rule that normally stops infantry detouring for a jeep. One assignment per
// team every 3 s; the existing fleet walk/board/drive code does the rest.
#define ABANDONED_REACH      250.0f
#define ABANDONED_AWAY_HQ    150.0f   // closer than this to a deployment it is a base vehicle (normal dispatch)
static void fleet_abandoned_dispatch(void)
{
    static unsigned checkedAt; static LONG epoch;
    if (epoch != g_roundEpoch) { checkedAt = 0; epoch = g_roundEpoch; }
    if (!mode_uses_vehicles() || !level_ready() || !g_enhanced.driving || !g_enhanced.returnToBase || g_driveOff || !g_vehInput ||
        (checkedAt && H->frame - checkedAt < 3 * 30)) return;
    checkedAt = H->frame ? H->frame : 1;
    char **vehicles; int nv = vehicle_list(&vehicles); if (!nv) return;
    for (int team = 1; team <= 2; team++) {
        int air, jets, ground, total; fleet_counts(team, &air, &jets, &ground, &total);
        if (ground >= g_enhanced.groundDriversPerTeam || air + ground >= fleet_capacity(total)) continue;
        const float *own = g_hqCpKnown[team] ? g_hqCpPos[team] : g_hqPos[team];
        char *chosen = NULL; int pilot = -1; float best = INFINITY;
        for (int k = 0; k < nv && k < 256; k++) {
            char *v = vehicles[k];
            if (bm_air_profile(vehicle_name(v)) || vehicle_occupied(v) || fleet_claimed(v, -1) ||
                strcmp(fleet_reason(v, team), "available")) continue;
            float *position = vehicle_pos(v, NULL); if (!position) continue;
            if (!near_origin(own) && dist2(position, own) < ABANDONED_AWAY_HQ * ABANDONED_AWAY_HQ) continue;
            for (int i = 0; i < H->botCount; i++) {
                char *pl = H->bots[i], *s = *(char **)(pl + OFF_PLAYER_SOLDIER); BotBrain *b = bot_brain(i);
                if (!s || !b->enabled || player_team(pl) != team || *(float *)(s + OFF_SOLDIER_HEALTH) <= 0 ||
                    player_vehicle(pl) || g_drive[i].phase || g_air[i].phase || g_fleet[i].vehicle || g_gliding[i] ||
                    b->commander || b->prevCommander || b->task != TASK_NONE ||
                    H->frame < g_driveCooldown[i] || H->frame < g_fleet[i].retryAt ||
                    (g_fleet[i].failedVehicle == v && H->frame < g_fleet[i].failedUntil)) continue;
                float *origin = (float *)(s + OFF_SOLDIER_POSITION);
                float distance = sqrtf(dist2(origin, position));
                if (distance > ABANDONED_REACH || fleet_route_blocked(v, team, origin)) continue;
                // Prefer armour, then the closest bot.
                float score = distance - (bm_armored(bm_vehicle_profile(vehicle_name(v))) ? 120 : 0);
                if (score < best) { best = score; chosen = v; pilot = i; }
            }
        }
        if (pilot < 0) continue;
        nav_cancel_path(pilot); memset(&g_vehicleApproach[pilot], 0, sizeof g_vehicleApproach[pilot]);
        FleetReservation *r = &g_fleet[pilot]; r->vehicle = chosen; r->role = FLEET_GROUND;
        r->until = H->frame + 30 * 30; r->queuedAt = H->frame ? H->frame : 1;
        // Caspian V3: the walk starts now. A bot that had been standing (guarding, waiting) was otherwise taken
        // for stuck by the activity watchdog seconds later, which cancelled the assignment (37 of ~100 in V2).
        g_activity[pilot].progressAt = g_activity[pilot].travelAt = H->frame ? H->frame : 1;
        g_activity[pilot].attempts = 0;
        bot_log("fleet: %s goes for an abandoned %s %.0f m away", H->bots[pilot] + OFF_PLAYER_ONLINE_NAME, vehicle_name(chosen),
                sqrtf(dist2((float *)(*(char **)(H->bots[pilot] + OFF_PLAYER_SOLDIER) + OFF_SOLDIER_POSITION), vehicle_pos(chosen, NULL))));
        diagnostic_note(pilot, "team sends nearest free infantry to an abandoned vehicle", chosen);
    }
}
