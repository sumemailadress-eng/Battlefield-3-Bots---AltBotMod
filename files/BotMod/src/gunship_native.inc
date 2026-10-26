// AltBotMod V23: the AC-130 gunship ("GUNSHIP"). It flies its own orbit; its seats are guns. As Fun-Bots does
// (BotSpawner "SpawnInGunship"), a bot that has just spawned on the team that owns it takes a free gun seat straight away
// - seats 1 and up; one stays free for a human on the team. The gunner code aims and fires from there (its guns reach
// 1.6 km, its own speed is in the lead), and nothing pulls a gunner out of it.
static unsigned g_gunshipFailUntil, g_gunshipBoarded, g_gunshipLogAt;
static int gunship_team(char *veh)
{
    int vt = 0; float *p = vehicle_pos(veh, &vt);
    char *crew = vehicle_occupant(veh);
    if (crew && player_exists(crew)) vt = player_team(crew);
    return p && vt >= 1 && vt <= 2 ? vt : 0;
}
static void gunship_tick(int i)
{
    if (!mode_uses_vehicles() || !level_ready() || g_roundCleanup || !g_vehInput || H->frame < g_gunshipFailUntil || (H->frame + i) % 15) return;
    char *pl = H->bots[i], *s = *(char **)(pl + OFF_PLAYER_SOLDIER);
    if (!s || *(float *)(s + OFF_SOLDIER_HEALTH) <= 0 || player_vehicle(pl) || g_drive[i].phase || g_air[i].phase || g_fleet[i].vehicle ||
        g_crewVehicle[i] || bot_brain(i)->commander || g_lastSpawnedBot[i] != pl || H->frame - g_lastSpawnedAt[i] > 3 * 30) return;
    int team = player_team(pl);
    char **vehs; int n = vehicle_list(&vehs);
    for (int k = 0; k < n && k < 256; k++) {
        char *v = vehs[k];
        if (!vehicle_is_gunship(v) || !vehicle_ok(v) || *(float *)(v + OFF_VEHICLE_HEALTH) <= 0 || gunship_team(v) != team) continue;
        char **seats; int ns = vehicle_seats(v, &seats), freeSeat = -1, freeCount = 0, humanAboard = 0;
        for (int q = 1; q < ns; q++) { char *o = *(char **)(seats[q] + 0x4c); if (!o) { freeCount++; if (freeSeat < 0) freeSeat = q; } else if (!is_bot(o)) humanAboard = 1; }
        int humanOnTeam = 0;
        char *pm = player_manager(); void **arr = pm ? *(void ***)(pm + 0xac) : NULL; unsigned cnt = pm ? *(unsigned *)(pm + 8) : 0;
        for (unsigned m = 0; arr && m < cnt && m < 64; m++) if (arr[m] && !is_bot(arr[m]) && player_team(arr[m]) == team) humanOnTeam = 1;
        if (freeSeat < 0 || (humanOnTeam && !humanAboard && freeCount < 2)) return;   // a seat stays free for you
        ((EnterVehicle_t)ADDR_PLAYER_ENTER_VEHICLE)(pl, v, freeSeat);
        if (player_vehicle(pl) != v) {
            g_gunshipFailUntil = H->frame + 60 * 30;
            bot_log("gunship: %s could not take seat %d of the %s - tried again in a minute", pl + OFF_PLAYER_ONLINE_NAME, freeSeat, vehicle_name(v));
            return;
        }
        g_gunshipBoarded++;
        nav_cancel_path(i); bot_nav(i)->mode = 0; memset(H->botMove[i], 0, sizeof H->botMove[i]);
        bot_log("gunship: %s takes gun seat %d of the team's %s (%u boarded so far)", pl + OFF_PLAYER_ONLINE_NAME, freeSeat, vehicle_name(v), g_gunshipBoarded);
        return;
    }
}
