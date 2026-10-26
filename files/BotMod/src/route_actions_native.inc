// AltBotMod V15 - Route Actions: what the Fun-Bots route files record besides the paths themselves.
//  - Boarding spots ("vehicle" actions, 926 in the shipped files): where to stand to get into a vehicle that spawns there.
//    A bot going for that vehicle takes the recorded path to the spot (the labelled "vehicle tank1 us" paths), and the
//    fleet prefers a vehicle standing on its own team's spot.
//  - Exit points ("exit" actions): where the people in a vehicle get out on the way to an objective. Fun-Bots: with
//    onlyPassengers the passengers get out and the driver drives on; without it everyone gets out. Here: an exit point
//    within 150 m of the driver's objective; tanks and IFVs keep their crew (only passengers get out), and a light
//    vehicle does what it does on arriving (cargo out; a gun truck keeps its gunner, a transport is left).
//  - Waits and crouch sections (SpeedMode 0 + OptValue seconds / SpeedMode 1-2): a bot holds at a recorded wait point
//    for that long (at most 8 s, never under fire, never on an errand) and moves crouched through recorded crouch sections.
//  - Beacon spots ("beacon" actions): a recon with a radio beacon sets one there, once a life (half of its lives), at the
//    spot nearest the objective its team is going for (30-250 m from it, not by an enemy flag).
//  - Flag names: the single-letter path labels are matched to this level's flags (tn_match_flags); a route to a flag
//    prefers the paths labelled with it.
#define RA_BOARD_SPOT_H       10.0f   // a recorded boarding spot this close to a vehicle belongs to it
#define RA_EXIT_REACH          8.0f   // the vehicle passes this close to a recorded exit point
#define RA_EXIT_GOAL         150.0f   // ... and the exit serves the objective (within this far of it)
#define RA_WAIT_MAX_SECONDS    8
#define RA_BEACON_CHANCE      50      // % of a beacon-carrying recon's lives spent setting one first
#define RA_BEACON_MAX_SPOTS  512
static unsigned g_raExitsAll, g_raExitsRiders, g_raHolds, g_raCrouchSections, g_raBeaconRuns, g_raBeaconsSet;
static int g_raFlagLetter[TN_FLAG_FIELDS];
static unsigned g_raHoldUntil[MAX_BOTS]; static int g_raHoldIdx[MAX_BOTS];   // (g_raCrouched, g_raBoardRoutes: trace_native.inc)

static int route_actions_on(void) { return g_enhanced.funBotsRoutes && g_enhanced.routeActions && g_trace.count; }

// After a route file is loaded: which flag each objective letter is, and what the file holds (logged once).
static void route_actions_level(void)
{
    int counts[5] = {0}, riders = 0;
    for (int k = 0; k < g_trace.actionCount; k++) { if (g_trace.actions[k].type < 5) counts[g_trace.actions[k].type]++; riders += g_trace.actions[k].onlyPassengers; }
    bot_log("trace: recorded actions - %d boarding spot(s), %d exit point(s) (%d passengers only), %d beacon spot(s), %d M-COM spot(s); %d labelled path(s)",
            counts[TN_ACT_VEHICLE], counts[TN_ACT_EXIT], riders, counts[TN_ACT_BEACON], counts[TN_ACT_MCOM], g_trace.labelledPaths);
    for (int f = 0; f < TN_FLAG_FIELDS; f++) g_raFlagLetter[f] = -1;
    if (!g_enhanced.routeActions || !g_flagCount || !g_trace.labelledPaths) return;
    float pos[TN_FLAG_FIELDS][3]; int n = g_flagCount < TN_FLAG_FIELDS ? g_flagCount : TN_FLAG_FIELDS;
    for (int f = 0; f < n; f++) memcpy(pos[f], flag_pos(f), 12);
    int matched = tn_match_flags(&g_trace, (const float (*)[3])pos, n, g_raFlagLetter);
    char list[128] = ""; int at = 0;
    for (int f = 0; f < n && at < (int)sizeof list - 8; f++)
        at += snprintf(list + at, sizeof list - at, "%s%d=%c", f ? " " : "", f, g_raFlagLetter[f] >= 0 ? 'a' + g_raFlagLetter[f] : '?');
    bot_log("trace: path labels matched to %d of %d flag(s) (%s) - routes to a flag prefer its own labelled paths", matched, n, list);
}

// The recorded boarding spot of a vehicle standing at `vp` (-1 = none).
static int route_board_spot(const float *vp)
{
    if (!route_actions_on() || !vp) return -1;
    return tn_action_near(&g_trace, TN_ACT_VEHICLE, vp, RA_BOARD_SPOT_H, 4.0f);
}
// Fleet: a vehicle standing on a boarding spot recorded for this team (or both) is the one meant for it.
static float route_board_bonus(char *veh, int team)
{
    float *vp = vehicle_pos(veh, NULL);
    int a = route_board_spot(vp);
    if (a < 0) return 0;
    int t = g_trace.actions[a].team;
    return t == 0 || t == team ? 500.0f : 0.0f;
}

// ---- exit points ----
// 1: the drive changed (everyone out / holding at the objective) - the caller stops this tick.
static int route_exit_tick(int i, DriveState *d, const float *vp)
{
    if (!route_actions_on() || d->phase != 3 || d->manual || !d->veh || vehicle_is_boat(d->veh)) return 0;
    if ((H->frame + i) % 6) return 0;
    int a = tn_action_near(&g_trace, TN_ACT_EXIT, vp, RA_EXIT_REACH, 4.0f);
    if (a < 0 || d->routeExit == a + 1 || hdist(g_trace.actions[a].p, d->goal) > RA_EXIT_GOAL) return 0;
    d->routeExit = a + 1;
    const TNAction *x = &g_trace.actions[a];
    int armored = bm_armored(bm_vehicle_profile(vehicle_name(d->veh)));
    if (!x->onlyPassengers && !armored) {   // everyone: the light vehicle arrives here
        g_raExitsAll++;
        bot_log("drive: %s reaches a recorded exit point %.0f m from flag %d - everyone out", H->bots[i] + OFF_PLAYER_ONLINE_NAME, hdist(x->p, d->goal), d->flag);
        drive_arrive_objective(i, d, vp);
        return 1;
    }
    int out = 0;   // passengers only (and a tank's crew stays): ordinary bot cargo dismounts, gunners and humans stay
    for (int j = 0; j < H->botCount; j++) if (j != i && player_vehicle(H->bots[j]) == d->veh &&
        vehicle_seat_of(d->veh, H->bots[j]) > 1 && bot_brain(j)->task == TASK_NONE) {
        bot_exit_vehicle(j, 0); memset(&g_drive[j], 0, sizeof g_drive[j]); g_driving[j] = 0;
        gunner_reset(j); choose_local_objective(j, "recorded exit point - passengers out");
        out++;
    }
    if (out) {
        g_raExitsRiders++;
        bot_log("drive: %s drops %d passenger(s) at a recorded exit point %.0f m from flag %d and drives on%s", H->bots[i] + OFF_PLAYER_ONLINE_NAME, out,
                hdist(x->p, d->goal), d->flag, x->onlyPassengers ? "" : " (armour keeps its crew)");
    }
    return 0;
}

// ---- waits and crouch sections (infantry on a Fun-Bots route) ----
static void route_wait_reset(int i) { g_raHoldIdx[i] = -1; g_raHoldUntil[i] = 0; }
// From follow_path, with the next waypoint known.
static void route_waypoint_tick(int i, char *soldier, BotPath *p)
{
    if (!route_actions_on() || p->next >= p->count) {
        if (g_raCrouched[i]) { g_raCrouched[i] = 0; bot_stand(i); }
        return;
    }
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION), *w = &p->pts[p->next * 3];
    float dx = w[0] - pos[0], dz = w[2] - pos[2], d2 = dx * dx + dz * dz;
    unsigned wait = TN_WAIT_SECONDS(p->wpFlags[p->next]);
    if (wait && g_raHoldIdx[i] != p->next && d2 < 1.6f * 1.6f) {
        g_raHoldIdx[i] = p->next;
        BotBrain *b = bot_brain(i);
        if (b->task == TASK_NONE && !(g_hitAt[i] && H->frame - g_hitAt[i] < 5 * 30)) {
            if (wait > RA_WAIT_MAX_SECONDS) wait = RA_WAIT_MAX_SECONDS;
            g_raHoldUntil[i] = H->frame + wait * 30;
            if (g_raHolds++ % 25 == 0) bot_log("trace: %s holds %u s at a recorded wait point (%u holds)", H->bots[i] + OFF_PLAYER_ONLINE_NAME, wait, g_raHolds);
        }
    }
    // crouched from the first recorded crouch point until the route stands up again
    int want = (p->wpFlags[p->next] & TN_FLAG_CROUCH) && (p->next == 0 || (p->wpFlags[p->next - 1] & TN_FLAG_CROUCH) || d2 < 4.0f * 4.0f);
    if (want && !g_raCrouched[i]) { g_raCrouched[i] = 1; bot_crouch(i); g_raCrouchSections++; }
    else if (!want && g_raCrouched[i]) { g_raCrouched[i] = 0; bot_stand(i); }
}
// From steer_bot: holding at a wait point - stand still, keep the stuck checks quiet, look down the route.
static int route_hold_tick(int i, char *soldier, BotNav *nav)
{
    if (!g_raHoldUntil[i]) return 0;
    if (H->frame >= g_raHoldUntil[i] || nav->mode != 3 || bot_brain(i)->task != TASK_NONE || (g_hitAt[i] && H->frame - g_hitAt[i] < 30)) {
        g_raHoldUntil[i] = 0; return 0;
    }
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION);
    H->botMove[i][0] = H->botMove[i][1] = H->botMove[i][2] = 0;
    g_progress[i].frame = H->frame; g_progress[i].x = pos[0]; g_progress[i].z = pos[2]; g_progress[i].directTries = 0;
    g_paths[i].lastProgressFrame = H->frame;
    return 1;
}
static int route_crouched(int i) { return g_raCrouched[i]; }

// ---- beacon spots ----
enum { RB_OFF, RB_UNDECIDED, RB_GOING, RB_PLACING, RB_DONE };
typedef struct { char *soldier; unsigned char state, planned; int spot, slot; unsigned until, lives; } RouteBeacon;
static RouteBeacon g_raBeacon[MAX_BOTS];
static unsigned g_raBeaconSpotUntil[RA_BEACON_MAX_SPOTS];   // spot in use / set until (frame)
static int route_beacon_pick(int i, int team, const float *me, const float *objective)
{
    int best = -1; float bestScore = 1e30f;
    for (int k = 0; k < g_trace.actionCount && k < RA_BEACON_MAX_SPOTS; k++) {
        const TNAction *a = &g_trace.actions[k];
        if (a->type != TN_ACT_BEACON || H->frame < g_raBeaconSpotUntil[k]) continue;
        float toObj = hdist(a->p, objective), toMe = hdist(a->p, me);
        if (toObj < 30.0f || toObj > 250.0f || toMe > 250.0f) continue;
        int nearEnemy = 0;   // not beside an enemy-held flag (other than the one being attacked)
        for (int f = 0; f < g_flagCount && !nearEnemy; f++) {
            int o = flag_owner(f);
            if (o && o != team && hdist(flag_pos(f), objective) > 5.0f && hdist(a->p, flag_pos(f)) < 60.0f) nearEnemy = 1;
        }
        if (nearEnemy) continue;
        float score = toObj + toMe * 0.5f;
        if (score < bestScore) { bestScore = score; best = k; }
    }
    (void)i;
    return best;
}
static void route_beacon_off(int i, BotBrain *b, const char *why)
{
    RouteBeacon *r = &g_raBeacon[i];
    if (r->state == RB_GOING || r->state == RB_PLACING) bot_log("beacon: %s gives up its beacon run (%s)", H->bots[i] + OFF_PLAYER_ONLINE_NAME, why);
    if (r->spot >= 0 && r->spot < RA_BEACON_MAX_SPOTS && r->state == RB_GOING) g_raBeaconSpotUntil[r->spot] = H->frame + 3 * 60 * 30;   // (try others meanwhile)
    r->state = RB_OFF; b->enroute = b->waiting = 0; bot_nav(i)->mode = 0;
}
// The brain's turn for a bot with no task (before sniper overwatch). 1: the beacon run has the bot this tick.
static int route_beacon_tick(int i, BotBrain *b, char *soldier, const float *objective)
{
    RouteBeacon *r = &g_raBeacon[i];
    const char *name = (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME;
    if (r->soldier != soldier) {   // a new life
        unsigned lives = r->lives + 1;
        memset(r, 0, sizeof *r); r->soldier = soldier; r->lives = lives; r->spot = -1; r->state = RB_OFF;
        if (g_enhanced.reconBeacons && route_actions_on() && is_recon(i) && bot_gadget_slot(i, "RadioBeacon", "Beacon") >= 0) {
            unsigned h = 2166136261u;
            for (const char *c = name; *c; c++) h = (h ^ (unsigned char)*c) * 16777619u;
            h = (h ^ (lives * 7919u)) * 16777619u;
            if ((h >> 9) % 100 < RA_BEACON_CHANCE) r->state = RB_UNDECIDED;
        }
    }
    if (r->state == RB_OFF || r->state == RB_DONE || !objective || b->task != TASK_NONE || player_vehicle(H->bots[i])) return 0;
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION);
    int team = player_team(H->bots[i]);
    if (r->state == RB_UNDECIDED) {
        for (int j = 0; j < H->botCount; j++)   // one beacon run per team at a time
            if (j != i && player_team(H->bots[j]) == team && (g_raBeacon[j].state == RB_GOING || g_raBeacon[j].state == RB_PLACING)) return 0;
        int k = route_beacon_pick(i, team, me, objective);
        if (k < 0) { r->state = RB_DONE; return 0; }
        r->spot = k; r->state = RB_GOING; r->planned = 0;
        r->until = H->frame + (unsigned)((hdist(me, g_trace.actions[k].p) / 3.0f + 40.0f) * 30);
        g_raBeaconSpotUntil[k] = H->frame + 10 * 60 * 30;
        g_raBeaconRuns++;
        bot_log("beacon: %s heads for a recorded beacon spot %.0f m from the objective (%.0f m away)", name, hdist(g_trace.actions[k].p, objective), hdist(me, g_trace.actions[k].p));
    }
    if (r->spot < 0 || r->spot >= g_trace.actionCount) { r->state = RB_DONE; return 0; }
    const TNAction *a = &g_trace.actions[r->spot];
    BotNav *nav = bot_nav(i);
    b->enroute = b->waiting = 0;
    if (r->state == RB_GOING) {
        if (hdist(me, a->p) < 2.5f && fabsf(me[1] - a->p[1]) < 2.5f) {
            int slot = bot_gadget_slot(i, "RadioBeacon", "Beacon");
            if (slot < 0 || bot_ammo(i, slot) == 0) { route_beacon_off(i, b, "no beacon left"); r->state = RB_DONE; return 0; }
            nav->mode = 1; nav->targetYaw = a->yaw; memset(H->botMove[i], 0, sizeof H->botMove[i]);
            place_gadget_now(i, slot);
            r->state = RB_PLACING; r->until = H->frame + 80;
            return 1;
        }
        if (H->frame > r->until) { route_beacon_off(i, b, "could not reach the spot in time"); return 0; }
        if (!r->planned || nav->mode != 3) {
            if (r->planned && nav->mode == 1) { route_beacon_off(i, b, "its route ended short of the spot"); return 0; }
            if (!plan_budget_left()) { nav->mode = 0; memset(H->botMove[i], 0, sizeof H->botMove[i]); return 1; }
            int n = plan_path(i, soldier, a->p[0], a->p[1], a->p[2]);
            if (!n && g_lastPlanDeferred) { nav->mode = 0; memset(H->botMove[i], 0, sizeof H->botMove[i]); return 1; }
            if (!n) { route_beacon_off(i, b, "no route to the spot"); return 0; }
            r->planned = 1; nav->mode = 3;
        }
        return 1;
    }
    // RB_PLACING: hold while the beacon goes down
    nav->mode = 1; memset(H->botMove[i], 0, sizeof H->botMove[i]);
    if (H->frame < r->until) return 1;
    r->state = RB_DONE; g_raBeaconsSet++;
    if (r->spot < RA_BEACON_MAX_SPOTS) g_raBeaconSpotUntil[r->spot] = H->frame + 5 * 60 * 30;
    bot_log("beacon: %s sets a radio beacon at the recorded spot (%.0f, %.0f) - %u set this level", name, a->p[0], a->p[2], g_raBeaconsSet);
    return 0;
}

static void route_actions_stats(void)
{
    if (!g_trace.count) return;
    bot_log("trace: route actions - %u boarding-path routes, exit points %u (everyone out) + %u (passengers out), %u waits held, %u crouch sections, beacons %u set of %u runs",
            g_raBoardRoutes, g_raExitsAll, g_raExitsRiders, g_raHolds, g_raCrouchSections, g_raBeaconsSet, g_raBeaconRuns);
}
static void route_actions_reset(void)
{
    memset(g_raBeaconSpotUntil, 0, sizeof g_raBeaconSpotUntil);
    for (int i = 0; i < MAX_BOTS; i++) { route_wait_reset(i); g_raCrouched[i] = 0; g_raBeacon[i].state = RB_OFF; g_raBeacon[i].soldier = NULL; }
}
