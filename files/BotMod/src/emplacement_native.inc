#ifndef GUN_IS_AIRCRAFT
#define GUN_IS_AIRCRAFT(v) (bm_air_profile(vehicle_name(v)) != NULL)   // AltBotMod V23 (core.c: the gunship too)
#endif
// Caspian V5: bots man stationary weapons (TOW / Kornet launchers, C-RAM / Pantsir AA) the game places at flags
// and bases. An idle or guarding infantry bot within 12 m of a free emplacement takes it when there is something for
// it to shoot: an enemy aircraft in the air for AA, an enemy ground vehicle within 450 m for a launcher. The gunner
// code (seat 0 of an emplacement now counts as a gun seat) aims and fires; the bot gets out after 40 s without a
// target, or when a human gives it an order. At most 3 emplacements per team are manned at once.
#define EMPLACEMENT_REACH   12.0f
#define EMPLACEMENT_IDLE    (15 * 30)   // AltBotMod V22: 15 s without a shot (V5-V21.1: 40 s with a target merely picked)
#define EMPLACEMENTS_PER_TEAM 2
static unsigned g_empIdleSince[MAX_BOTS], g_empCooldown[MAX_BOTS];
static unsigned g_empManned, g_empLeft;
static int emplacement_threat(int kind, int team, const float *at)
{
    char **list; int n = vehicle_list(&list);
    for (int k = 0; k < n && k < 256; k++) {
        char *v = list[k];
        if (!vehicle_ok(v) || !vehicle_occupied(v) || stationary_weapon(v)) continue;
        char *crew = vehicle_occupant(v);
        if (!crew || !player_exists(crew) || player_team(crew) == team) continue;
        float *p = vehicle_pos(v, NULL); if (!p) continue;
        int air = GUN_IS_AIRCRAFT(v);   // (V23: the gunship too)
        if (kind == 2 && air && dist2(p, at) < 850.0f * 850.0f && !vehicle_on_ground(p)) return 1;   // (not one parked at its base; V22: in the gun's reach, not 1500 m)
        if (kind == 1 && !air && dist2(p, at) < 400.0f * 400.0f) return 1;
    }
    return 0;
}
static void emplacement_tick(int i)
{
    char *pl = H->bots[i], *s = *(char **)(pl + OFF_PLAYER_SOLDIER), *v = player_vehicle(pl);
    BotBrain *b = bot_brain(i);
    if (v) {
        if (!stationary_weapon(v)) return;
        // A target keeps it there - but not an aircraft sitting on the ground (a parked jet at its base would
        // hold the gunner forever without one shot that counts).
        char *tv = g_gunTargetVeh[i]; float *tvp = tv && vehicle_ok(tv) ? vehicle_pos(tv, NULL) : NULL;
        int parked = tvp && bm_air_profile(vehicle_name(tv)) && vehicle_on_ground(tvp);
        // AltBotMod V22: only shooting keeps it there (V5-V21.1: any target picked did - a C-RAM gunner watched a jet 1.4 km
        // away all round; 611 mannings on 2026-10-17)
        (void)parked;
        if ((g_botFiredAt[i] && H->frame - g_botFiredAt[i] < 3) || !g_empIdleSince[i]) g_empIdleSince[i] = H->frame ? H->frame : 1;
        // AltBotMod V16: an enemy soldier on foot closing in (a stationary gun cannot turn onto him - C-RAM / Pantsir
        // gunners were knifed in their seats, 2026-10-13): out, and fight him with the rifle.
        if ((H->frame + i) % 10 == 0) {
            char *pm = player_manager(); void **arr = pm ? *(void ***)(pm + 0xac) : NULL; unsigned cnt = pm ? *(unsigned *)(pm + 8) : 0;
            float *vp = vehicle_pos(v, NULL); char *close = NULL; float best = 18.0f * 18.0f;
            for (unsigned k = 0; vp && arr && k < cnt && k < 64; k++) {
                char *t = arr[k], *ts = t ? *(char **)(t + OFF_PLAYER_SOLDIER) : NULL;
                if (!ts || player_vehicle(t) || player_team(t) == player_team(pl) || player_team(t) < 1 || *(float *)(ts + OFF_SOLDIER_HEALTH) <= 0) continue;
                float *tp = (float *)(ts + OFF_SOLDIER_POSITION), d = dist2(tp, vp);
                if (d < best && fabsf(tp[1] - vp[1]) < 6) { best = d; close = t; }
            }
            if (close && g_gunTarget[i] != close) {
                bot_exit_vehicle(i, 0);
                g_empCooldown[i] = H->frame + 20 * 30; g_empIdleSince[i] = 0; g_empLeft++;
                bot_log("emplacement: %s leaves the %s - %s is %.0f m away", pl + OFF_PLAYER_ONLINE_NAME, vehicle_name(v), close + OFF_PLAYER_ONLINE_NAME, sqrtf(best));
                start_fight(i, close, 0, "enemy at the stationary weapon");
                return;
            }
        }
        if (H->frame - g_empIdleSince[i] > EMPLACEMENT_IDLE || b->commander || !b->enabled) {
            bot_exit_vehicle(i, 0);
            g_empCooldown[i] = H->frame + 90 * 30; g_empIdleSince[i] = 0; g_empLeft++;
            bot_log("emplacement: %s leaves the %s (%s)", pl + OFF_PLAYER_ONLINE_NAME, vehicle_name(v), b->commander ? "orders" : "no shot in 15 s");
            choose_local_objective(i, "left the stationary weapon");
        }
        return;
    }
    if ((H->frame + i) % 30 || !s || *(float *)(s + OFF_SOLDIER_HEALTH) <= 0 || !b->enabled || b->commander || b->prevCommander ||
        (b->task != TASK_NONE && b->task != TASK_DEFEND) || g_drive[i].phase || g_air[i].phase || g_fleet[i].vehicle || g_crewVehicle[i] ||
        g_gliding[i] || H->frame < g_empCooldown[i] || !mode_uses_vehicles() || !level_ready() || !g_vehInput || g_roundCleanup) return;
    int team = player_team(pl);
    if (team < 1 || team > 2) return;
    int manned = 0;
    for (int j = 0; j < H->botCount; j++) if (player_team(H->bots[j]) == team && stationary_weapon(player_vehicle(H->bots[j]))) manned++;
    if (manned >= EMPLACEMENTS_PER_TEAM) return;
    float *me = (float *)(s + OFF_SOLDIER_POSITION);
    const float *enemyHq = g_hqCpKnown[3 - team] ? g_hqCpPos[3 - team] : g_hqPos[3 - team];
    char **list; int n = vehicle_list(&list);
    for (int k = 0; k < n && k < 256; k++) {
        char *e = list[k]; int kind = stationary_weapon(e);
        if (!kind || !vehicle_ok(e) || vehicle_occupied(e) || !driver_seat_free(e)) continue;
        float *p = vehicle_pos(e, NULL);
        if (!p || dist2(p, me) > EMPLACEMENT_REACH * EMPLACEMENT_REACH || fabsf(p[1] - me[1]) > 4) continue;
        if (!near_origin(enemyHq) && dist2(p, enemyHq) < 150.0f * 150.0f) continue;   // not at the enemy's base
        if (human_near(p, g_enhanced.humanRadius) || !emplacement_threat(kind, team, p)) continue;
        if (b->task != TASK_NONE) end_task(i, b, "mans a stationary weapon");
        nav_cancel_path(i); bot_nav(i)->mode = 0; memset(H->botMove[i], 0, sizeof H->botMove[i]);
        ((EnterVehicle_t)ADDR_PLAYER_ENTER_VEHICLE)(pl, e, 0);
        if (player_vehicle(pl) != e) { g_empCooldown[i] = H->frame + 30 * 30; return; }
        g_empIdleSince[i] = H->frame ? H->frame : 1; g_empManned++;
        bot_log("emplacement: %s mans the %s at (%.0f, %.0f) (%s; %u manned so far)", pl + OFF_PLAYER_ONLINE_NAME, vehicle_name(e), p[0], p[2],
                kind == 2 ? "enemy aircraft up" : "enemy armour near", g_empManned);
        return;
    }
}
