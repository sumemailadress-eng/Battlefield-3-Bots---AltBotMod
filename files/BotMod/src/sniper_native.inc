// AltBotMod V10 - snipers on high ground. A recon with a bolt-action rifle (most of its lives) does not run onto the
// flag: it climbs to a perch - a roof, an upper floor, a hill - that looks down on the flag its team is going for, from
// 90-440 m out, and snipes from there (crouched, scanning the flag area). It changes perch after a while, when it was
// pushed off it (cover, a fight that moved it) or when the objective moves on, three perches a life at most, then plays
// the flag like everyone else. Perches are surveyed per objective from the navigation grid (highest floor at each sample
// point, clear line of sight to the flag and around it), a ring a tick, and cached for the map.
#define SNIPE_MIN_ELEVATION   4.0f      // m above the objective
#define SNIPE_TAKEN_RADIUS    25.0f     // one perch: at most two snipers, as a pair (V11)
#define SNIPE_CHANCE          70        // % of a bolt-rifle recon's lives spent on overwatch
#define PERCH_MAX             12
#define PERCH_CACHE           24
#define PERCH_RINGS           8
#define PERCH_PER_RING        16
typedef struct {
    int key; unsigned serial; float obj[3]; int cursor, n, done; unsigned usedAt;
    float spot[PERCH_MAX][3], score[PERCH_MAX], elev[PERCH_MAX]; unsigned char stand[PERCH_MAX]; unsigned badUntil[PERCH_MAX];
} PerchSet;
static PerchSet g_perch[PERCH_CACHE];
enum { SNIPE_UNDECIDED, SNIPE_OFF, SNIPE_GOING, SNIPE_HOLD };
typedef struct {
    char *soldier; unsigned char state, moves, stand, planned; int key, set, slot;
    float spot[3], look[3], last[3]; unsigned at, until, scanAt, retryAt, lives;
} SniperState;
static SniperState g_snipe[MAX_BOTS];
static unsigned g_perchSurveys, g_perchTaken, g_perchFailed, g_perchHolds;
static unsigned g_perchSurveyFrame;

static float sniper_range(void) { return g_enhanced.sniperRange > 0 ? g_enhanced.sniperRange : 550.0f; }
static int sniper_rifle(int i)
{
    const BMWeaponProfile *w = infantry_weapon(i);
    return is_recon(i) && g_fightSlot[i] == 0 && w && w->family == BM_W_BOLT;
}
// How far this bot sees, engages and keeps fighting a soldier: a bolt-rifle recon out to SniperEngageMeters.
static float sniper_reach(int i)
{
    float r = RECON_SPOTTED_RANGE;
    return sniper_rifle(i) ? fmaxf(r, sniper_range()) : r;
}
static int sniper_holding(int i)
{
    SniperState *s = &g_snipe[i];
    char *so = *(char **)((char *)H->bots[i] + OFF_PLAYER_SOLDIER);
    return so && s->soldier == so && s->state == SNIPE_HOLD && dist2((float *)(so + OFF_SOLDIER_POSITION), s->spot) < 6.0f * 6.0f;
}

// Inside the playing area? BF3 kills a soldier that stays outside the combat area, which BotMod cannot read: a perch must
// lie within 120 m of the box round the flags (Conquest), or inside the mode's surveyed bounds (Rush).
static int perch_inside(const float *pt)
{
    if (!mode_is_cq()) return g_modeProfile ? mode_inside(pt, 40) : 0;
    if (g_flagCount <= 0) return 0;
    float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
    for (int f = 0; f < g_flagCount; f++) {
        const float *fp = flag_pos(f);
        if (fp[0] < lo[0]) lo[0] = fp[0];
        if (fp[0] > hi[0]) hi[0] = fp[0];
        if (fp[2] < lo[1]) lo[1] = fp[2];
        if (fp[2] > hi[1]) hi[1] = fp[2];
    }
    return pt[0] > lo[0] - 120 && pt[0] < hi[0] + 120 && pt[2] > lo[1] - 120 && pt[2] < hi[1] + 120;
}
// One ring of samples a call (16 points: ~16 floor lookups and up to 48 rays). Returns 1 once the set is complete.
static int perch_survey_step(PerchSet *p)
{
    NavGrid *g = *nav_slot();
    if (!g || g->phase != 2) return 0;
    if (g_perchSurveyFrame == H->frame) return 0;   // one ring per tick for the whole server
    g_perchSurveyFrame = H->frame;
    static const float radii[PERCH_RINGS] = {45, 70, 90, 140, 190, 250, 320, 400};   // (V18: 45 / 70 m - rooftops round a town flag)
    int ring = p->cursor++;
    if (ring >= PERCH_RINGS) { p->done = 1; g_perchSurveys++; return 1; }
    float r = radii[ring];
    if (r > sniper_range() * 0.8f) return 0;
    unsigned saved = g_rayFlags;
    for (int a = 0; a < PERCH_PER_RING; a++) {
        float ang = a * (6.2831853f / PERCH_PER_RING) + ring * 0.37f;
        float x = p->obj[0] + sinf(ang) * r, z = p->obj[2] + cosf(ang) * r, hs[6], pt[3];
        int nf = nav_floors_at(g, x, p->obj[1] + 400.0f, z, hs, 6);
        if (nf <= 0) continue;
        float top = hs[0];
        for (int k = 1; k < nf; k++) if (hs[k] > top) top = hs[k];
        if (top - p->obj[1] < SNIPE_MIN_ELEVATION) continue;
        if (!nav_snap(g, x, top, z, pt) || fabsf(pt[1] - top) > 1.5f || nav_is_water(g, pt[0], pt[1], pt[2]) || !perch_inside(pt)) continue;
        float elev = pt[1] - p->obj[1];
        // line of sight crouched (1.05 m) - or standing - to the flag and 10 m either side of it
        float dx = p->obj[0] - pt[0], dz = p->obj[2] - pt[2], dl = sqrtf(dx * dx + dz * dz) + 1e-6f;
        float side[3][2] = {{0, 0}, {-dz / dl * 10, dx / dl * 10}, {dz / dl * 10, -dx / dl * 10}};
        int best = 0, stand = 0;
        for (int h = 0; h < 2 && best < 2; h++) {
            float eye[3] = {pt[0], pt[1] + (h ? 1.6f : 1.05f), pt[2]};
            int seen = 0;
            for (int k = 0; k < 3; k++) {
                float t[3] = {p->obj[0] + side[k][0], p->obj[1] + 1.2f, p->obj[2] + side[k][1]};
                g_rayFlags = RAY_DONT_CHECK_CHARACTER;
                if (!ray_cast(eye, t, NULL, NULL)) seen++;
                else if (k == 0) break;   // the flag itself must be in view
            }
            if (seen > best) { best = seen; stand = h; }
        }
        if (best < 2) continue;
        // AltBotMod V18: a rooftop (open sky above the spot) beats an upper-floor window at the same height - the whole
        // flag in view, and people look for snipers in windows
        float up[3] = {pt[0], pt[1] + 2.0f, pt[2]}, sky[3] = {pt[0], pt[1] + 40.0f, pt[2]};
        g_rayFlags = RAY_DONT_CHECK_CHARACTER;
        int roof = elev >= 6.0f && !ray_cast(up, sky, NULL, NULL);
        float score = elev + best * 8.0f - fabsf(r - (r < 100 ? 70.0f : 200.0f)) * 0.04f + (roof ? 12.0f : 0.0f);
        int at = p->n < PERCH_MAX ? p->n++ : PERCH_MAX - 1;
        if (at == PERCH_MAX - 1 && p->n == PERCH_MAX && score <= p->score[at]) continue;
        while (at > 0 && p->score[at - 1] < score) {
            memcpy(p->spot[at], p->spot[at - 1], 12); p->score[at] = p->score[at - 1]; p->elev[at] = p->elev[at - 1];
            p->stand[at] = p->stand[at - 1]; p->badUntil[at] = p->badUntil[at - 1]; at--;
        }
        memcpy(p->spot[at], pt, 12); p->score[at] = score; p->elev[at] = elev; p->stand[at] = (unsigned char)stand; p->badUntil[at] = 0;
    }
    g_rayFlags = saved;
    return 0;
}
// The perch set of an objective (key: flag index, 1000 + MCOM), started if new. NULL while it is still being surveyed.
static PerchSet *perch_set(int key, const float *obj, int *index)
{
    NavGrid *g = *nav_slot();
    if (!g || g->phase != 2) return NULL;
    int freeSlot = -1, oldest = 0;
    for (int k = 0; k < PERCH_CACHE; k++) {
        PerchSet *p = &g_perch[k];
        if (p->serial == g->serial && p->key == key && dist2(p->obj, obj) < 4.0f) {
            p->usedAt = H->frame;
            *index = k;
            if (!p->done) perch_survey_step(p);
            return p->done ? p : NULL;
        }
        if (p->serial != g->serial) { if (freeSlot < 0) freeSlot = k; }
        else if (p->usedAt < g_perch[oldest].usedAt) oldest = k;
    }
    int k = freeSlot >= 0 ? freeSlot : oldest;
    PerchSet *p = &g_perch[k];
    memset(p, 0, sizeof *p);
    p->key = key; p->serial = g->serial; memcpy(p->obj, obj, 12); p->usedAt = H->frame;
    *index = k;
    perch_survey_step(p);
    return NULL;
}
// Best perch for this bot: high, in view, not far round the map (closer to the bot is better), not one it left.
// AltBotMod V11: two snipers may share a perch as a pair (the second takes a spot 4-7 m beside the first, at the same
// height, also in view of the flag); a free perch is still preferred, and never three on one.
static int perch_pair_spot(PerchSet *p, int k, const float *mate, float *out)
{
    NavGrid *g = *nav_slot();
    if (!g) return 0;
    unsigned saved = g_rayFlags;
    int found = 0;
    for (int a = 0; a < 8 && !found; a++) {
        float ang = a * 0.785398f + 0.39f, r = a & 1 ? 6.5f : 4.5f, pt[3];
        if (!nav_snap(g, p->spot[k][0] + sinf(ang) * r, p->spot[k][1], p->spot[k][2] + cosf(ang) * r, pt)) continue;
        if (fabsf(pt[1] - p->spot[k][1]) > 1.0f || dist2(pt, mate) < 3.0f * 3.0f || nav_is_water(g, pt[0], pt[1], pt[2])) continue;
        float eye[3] = {pt[0], pt[1] + (p->stand[k] ? 1.6f : 1.05f), pt[2]}, t[3] = {p->obj[0], p->obj[1] + 1.2f, p->obj[2]};
        g_rayFlags = RAY_DONT_CHECK_CHARACTER;
        if (!ray_cast(eye, t, NULL, NULL)) { memcpy(out, pt, 12); found = 1; }
    }
    g_rayFlags = saved;
    return found;
}
static int perch_pick(int i, PerchSet *p, const float *me, const float *avoid, float *spot)
{
    int best = -1;
    float bestScore = -1e30f, bestSpot[3] = {0, 0, 0};
    for (int k = 0; k < p->n; k++) {
        if (p->badUntil[k] > H->frame) continue;
        if (avoid && dist2(p->spot[k], avoid) < 30.0f * 30.0f) continue;
        int taken = 0; const float *mate = NULL;
        for (int j = 0; j < H->botCount; j++)
            if (j != i && (g_snipe[j].state == SNIPE_GOING || g_snipe[j].state == SNIPE_HOLD) && g_snipe[j].soldier &&
                dist2(g_snipe[j].spot, p->spot[k]) < SNIPE_TAKEN_RADIUS * SNIPE_TAKEN_RADIUS) { taken++; mate = g_snipe[j].spot; }
        if (taken > 1 || (taken && !g_enhanced.sniperPairs)) continue;
        float score = p->score[k] - sqrtf(dist2(me, p->spot[k])) * 0.08f - (taken ? 6.0f : 0.0f);
        if (score <= bestScore) continue;
        float at[3];
        if (taken) { if (!perch_pair_spot(p, k, mate, at)) continue; }
        else memcpy(at, p->spot[k], 12);
        bestScore = score; best = k; memcpy(bestSpot, at, 12);
    }
    if (best >= 0) memcpy(spot, bestSpot, 12);
    return best;
}
static void sniper_release(int i, BotBrain *b, const char *why, unsigned retrySeconds)
{
    SniperState *s = &g_snipe[i];
    if (s->state == SNIPE_GOING || s->state == SNIPE_HOLD)
        bot_log("sniper: %s leaves overwatch (%s)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, why);
    s->state = SNIPE_OFF;
    s->retryAt = retrySeconds ? H->frame + retrySeconds * 30 : 0xffffffffu;
    if (b->crouched) { bot_stand(i); b->crouched = 0; }
    b->enroute = b->waiting = 0;
    bot_nav(i)->mode = 0;
}
static int sniper_choose(int i, BotBrain *b, char *soldier, const float *objective, int key, const float *avoid)
{
    SniperState *s = &g_snipe[i];
    int set;
    PerchSet *p = perch_set(key, objective, &set);
    if (!p) {
        NavGrid *g = *nav_slot();
        if (!g || g->phase != 2) { sniper_release(i, b, "navigation grid still being surveyed", 60); return 0; }   // (plays the flag meanwhile)
        bot_nav(i)->mode = 0; memset(H->botMove[i], 0, sizeof H->botMove[i]); return 1;   // perch survey running (a few ticks)
    }
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION), at[3];
    int k = perch_pick(i, p, me, avoid, at);
    if (k < 0) { sniper_release(i, b, p->n ? "every perch over this objective is taken" : "no high ground in view of this objective", 60); return 0; }
    s->state = SNIPE_GOING; s->key = key; s->set = set; s->slot = k; s->planned = 0;
    memcpy(s->spot, at, 12); memcpy(s->look, objective, 12); s->stand = p->stand[k];
    s->at = H->frame;
    s->until = H->frame + (unsigned)((sqrtf(dist2(me, s->spot)) / 3.0f + 40.0f) * 30);   // walking time + 40 s, then give it up
    if (b->crouched) { bot_stand(i); b->crouched = 0; }
    g_perchTaken++;
    bot_log("sniper: %s heads for high ground %.0f m above objective %d, %.0f m from it", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME,
            p->elev[k], key, hdist(s->spot, objective));
    return 1;
}
static void perch_mark_bad(int i)
{
    SniperState *s = &g_snipe[i];
    if (s->set >= 0 && s->set < PERCH_CACHE && s->slot >= 0 && s->slot < PERCH_MAX && g_perch[s->set].key == s->key)
        g_perch[s->set].badUntil[s->slot] = H->frame + 5 * 60 * 30;
    g_perchFailed++;
}
// The brain's turn for a bot with no task. 1: overwatch has the bot this tick (the flag loop waits).
static int sniper_overwatch_tick(int i, BotBrain *b, char *soldier, const float *objective, int key)
{
    SniperState *s = &g_snipe[i];
    const char *name = (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME;
    if (s->soldier != soldier) {   // a new life: overwatch or not
        unsigned lives = s->lives + 1;
        memset(s, 0, sizeof *s);
        s->soldier = soldier; s->lives = lives; s->set = s->slot = -1;
        s->state = SNIPE_OFF; s->retryAt = 0xffffffffu;
        if (g_enhanced.sniperOverwatch && sniper_rifle(i)) {
            unsigned h = 2166136261u;
            for (const char *c = name; *c; c++) h = (h ^ (unsigned char)*c) * 16777619u;
            h = (h ^ lives) * 16777619u;
            if ((h >> 8) % 100 < SNIPE_CHANCE) { s->state = SNIPE_UNDECIDED; s->retryAt = 0; }
        }
    }
    if (!objective || b->task != TASK_NONE || player_vehicle(H->bots[i])) return 0;
    if (s->state == SNIPE_OFF) {
        if (H->frame < s->retryAt || !sniper_rifle(i)) return 0;
        s->state = SNIPE_UNDECIDED;
    }
    if (!sniper_rifle(i)) { sniper_release(i, b, "no sniper rifle in hand", 0); return 0; }
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION);
    if (s->state == SNIPE_UNDECIDED) {
        if (hdist(me, objective) < 60.0f) { s->state = SNIPE_OFF; s->retryAt = H->frame + 30 * 30; return 0; }   // already there: play it
        return sniper_choose(i, b, soldier, objective, key, NULL);
    }
    b->enroute = b->waiting = 0;
    if (s->key != key) {   // the objective moved on: this perch if it sees the new one, else another
        float eye[3] = {s->spot[0], s->spot[1] + (s->stand ? 1.6f : 1.05f), s->spot[2]}, t[3] = {objective[0], objective[1] + 1.2f, objective[2]};
        unsigned saved = g_rayFlags; g_rayFlags = RAY_DONT_CHECK_CHARACTER;
        int sees = hdist(s->spot, objective) < sniper_range() * 0.85f && !ray_cast(eye, t, NULL, NULL);
        g_rayFlags = saved;
        if (sees) { s->key = key; memcpy(s->look, objective, 12); bot_log("sniper: %s covers objective %d from the same perch", name, key); }
        else {
            if (++s->moves >= 3) { sniper_release(i, b, "third perch done - plays the objective", 90); return 0; }
            float old[3]; memcpy(old, s->spot, 12);
            return sniper_choose(i, b, soldier, objective, key, old);
        }
    }
    if (s->state == SNIPE_GOING) {
        float dy = me[1] - s->spot[1];
        if (hdist(me, s->spot) < 2.5f && fabsf(dy) < 2.5f) {
            s->state = SNIPE_HOLD; s->until = H->frame + (unsigned)((75 + rand() % 46) * 30); s->scanAt = 0;
            memcpy(s->last, me, 12);
            bot_nav(i)->mode = 1;
            if (!s->stand && !b->crouched) { bot_crouch(i); b->crouched = 1; }
            g_perchHolds++;
            place_class_gadget(i, soldier);   // AltBotMod V16: its T-UGS / SOFLAM goes down at the perch (2026-10-13: snipers never placed theirs)
            bot_log("sniper: %s on its perch %.0f m above objective %d, %.0f m out", name, s->spot[1] - s->look[1], s->key, hdist(me, s->look));
            return 1;
        }
        if (H->frame > s->until) {
            perch_mark_bad(i);
            if (++s->moves >= 3) { sniper_release(i, b, "could not reach its perches", 90); return 0; }
            bot_log("sniper: %s could not get up to its perch in time - another one", name);
            float old[3]; memcpy(old, s->spot, 12);
            return sniper_choose(i, b, soldier, objective, key, old);
        }
        BotNav *nav = bot_nav(i);
        if (!s->planned || nav->mode != 3) {
            if (s->planned && nav->mode == 1) {   // the route ended short of it
                perch_mark_bad(i);
                if (++s->moves >= 3) { sniper_release(i, b, "its perches are out of reach", 90); return 0; }
                float old[3]; memcpy(old, s->spot, 12);
                return sniper_choose(i, b, soldier, objective, key, old);
            }
            if (!plan_budget_left()) { nav->mode = 0; memset(H->botMove[i], 0, sizeof H->botMove[i]); return 1; }
            int n = plan_path(i, soldier, s->spot[0], s->spot[1], s->spot[2]);
            if (!n && g_lastPlanDeferred) { nav->mode = 0; memset(H->botMove[i], 0, sizeof H->botMove[i]); return 1; }
            if (!n) {
                perch_mark_bad(i);
                if (++s->moves >= 3) { sniper_release(i, b, "no route up to its perches", 90); return 0; }
                float old[3]; memcpy(old, s->spot, 12);
                return sniper_choose(i, b, soldier, objective, key, old);
            }
            s->planned = 1; nav->mode = 3;
        }
        return 1;
    }
    // SNIPE_HOLD
    if (hdist(me, s->spot) > 6.0f || fabsf(me[1] - s->spot[1]) > 3.0f) {   // pushed off it (cover, a fight): another perch
        if (++s->moves >= 3) { sniper_release(i, b, "pushed off its third perch", 90); return 0; }
        bot_log("sniper: %s was moved off its perch - relocates", name);
        float old[3]; memcpy(old, s->spot, 12);
        return sniper_choose(i, b, soldier, objective, key, old);
    }
    if (H->frame > s->until) {   // been here long enough: a sniper relocates
        if (++s->moves >= 3) { sniper_release(i, b, "time to join the push", 90); return 0; }
        bot_log("sniper: %s relocates to another perch", name);
        float old[3]; memcpy(old, s->spot, 12);
        if (b->crouched) { bot_stand(i); b->crouched = 0; }
        return sniper_choose(i, b, soldier, objective, key, old);
    }
    BotNav *nav = bot_nav(i);
    nav->mode = 1;
    memset(H->botMove[i], 0, sizeof H->botMove[i]);
    if (H->frame >= s->scanAt) {   // glass the objective: a slow sweep across it, ~25 deg either side
        s->scanAt = H->frame + 60 + rand() % 60;
        float base = yaw_towards(s->look[0] - me[0], s->look[2] - me[2]);
        nav->targetYaw = base + ((rand() % 200) - 100) / 100.0f * 0.45f;
    }
    if (!s->stand && !b->crouched) { bot_crouch(i); b->crouched = 1; }
    return 1;
}
static void sniper_stats_tick(void)
{
    static unsigned at, last[4];
    if (H->frame - at < 60 * 30) return;
    at = H->frame;
    unsigned now[4] = {g_perchSurveys, g_perchTaken, g_perchHolds, g_perchFailed};
    if (memcmp(now, last, sizeof now)) {
        int going = 0, holding = 0;
        for (int j = 0; j < H->botCount; j++) { if (g_snipe[j].state == SNIPE_GOING) going++; else if (g_snipe[j].state == SNIPE_HOLD) holding++; }
        bot_log("sniper: last minute %u objective(s) surveyed for high ground, %u perch(es) chosen, %u reached, %u unreachable; now %d on the way, %d on overwatch",
                now[0] - last[0], now[1] - last[1], now[2] - last[2], now[3] - last[3], going, holding);
    }
    memcpy(last, now, sizeof now);
}
