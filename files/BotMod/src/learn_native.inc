// AltBotMod V21 - learning, the game side (the store is learn.c). Included in core.c after the live map.
//
// Folder: <server>\BotMod_learned\ - one <Level>_<Mode>.learn per map and mode, plus breakable.txt (glass / panel
// materials). The installer never touches the folder, so what was learned carries over to the next version; Server
// Control's Learning tab lists, deletes, exports and imports it.
//
// What is learned, and what it changes:
//   on foot      - stuck spots: does a bot get through afterwards (with a jump?) or stay stuck. Spots bots get through
//                  are never blocked again (V18's stairwell trouble), the ones they got through with a jump get a jump
//                  on the way in, the ones nobody ever gets through (3 times) are taken out of the grid at load.
//                  Deaths (10 m cells): the recorded routes through a killing ground cost more.
//   ground/boat  - where drivers stall: 2 stalls nobody drove out of strike that patch off the driving map at load.
//   air          - where aircraft were lost close to the ground: pilots keep more height around there.
//                  Helicopter landing spots: where players set a helicopter down (away from its spawn) and where
//                  transports landed; transports try those first (a spot found blocked more often than used is dropped).
//   from players - the routes human players walk, drive and sail (not fly) are recorded; a long enough one over new
//                  ground joins the Fun-Bots route network at the next map load (BotMod_learned, route lines).
// Settings: Learning=0 switches all of it off (nothing read, nothing written); LearnFromPlayers=0 only the recording.
static BLStore g_learn;
static char g_learnKey[96];
static int g_learnLoaded, g_learnApplied, g_learnMasksApplied;
static unsigned g_learnSaveAt, g_learnBlockAt, g_learnJudgeAt;
static FILETIME g_learnStamp;   // the file's write time when we last read / wrote it
static unsigned g_learnJumpAt[MAX_BOTS], g_learnPreJumpAt[MAX_BOTS];
static unsigned g_learnStats[6];   // stuck notes, passes, fails, pre-jumps, routes learned, kept open
typedef struct LearnPending { int spot, kind; float p[3], h[2]; unsigned at; } LearnPending;
static LearnPending g_learnPending[MAX_BOTS];
static char *g_learnRoutes[BL_KINDS]; static size_t g_learnRoutesLen[BL_KINDS];
static const void *g_learnLandNodes, *g_learnWaterNodes;   // (V21.1: the vehicle route nets the learned stall spots were applied to)

static void learn_dir(char *out, int n)
{
    char dir[MAX_PATH]; GetModuleFileNameA(NULL, dir, sizeof dir); *strrchr(dir, '\\') = 0;
    snprintf(out, n, "%s\\BotMod_learned", dir);
    CreateDirectoryA(out, NULL);   // (already there: fails harmlessly)
}
static void learn_path(char *out, int n, const char *key)
{
    char dir[MAX_PATH + 32]; learn_dir(dir, sizeof dir);
    char safe[96]; int j = 0;
    for (const char *k = key; *k && j < (int)sizeof safe - 1; k++) safe[j++] = (isalnum((unsigned char)*k) || *k == '_' || *k == '-') ? *k : '_';
    safe[j] = 0;
    snprintf(out, n, "%s\\%s.learn", dir, safe);
}
static char *learn_read(const char *path, size_t *len, FILETIME *stamp)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (stamp) memset(stamp, 0, sizeof *stamp);
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &a)) return NULL;
    if (stamp) *stamp = a.ftLastWriteTime;
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    if (n <= 0 || n > 32L * 1024 * 1024) { fclose(f); return NULL; }
    char *b = malloc((size_t)n + 1); if (!b) { fclose(f); return NULL; }
    *len = fread(b, 1, (size_t)n, f); b[*len] = 0; fclose(f);
    return b;
}
static void learn_routes_free(void) { for (int k = 0; k < BL_KINDS; k++) { free(g_learnRoutes[k]); g_learnRoutes[k] = NULL; g_learnRoutesLen[k] = 0; } }
static void learn_summary(const char *what)
{
    int bad = 0, open = 0, jump = 0, kinds[BL_KINDS] = {0}, routes[BL_KINDS] = {0};
    for (int k = 0; k < g_learn.nSpots; k++) { const BLSpot *s = &g_learn.spots[k]; kinds[s->kind]++; bad += bl_spot_bad(s); open += bl_spot_passable(s); jump += bl_spot_jump(s); }
    for (int k = 0; k < g_learn.nRoutes; k++) routes[g_learn.routes[k].kind]++;
    bot_log("learn: %s %s - stuck spots %d on foot / %d ground / %d boat / %d air (%d to avoid, %d known passable, %d with a jump), %d death cell(s), routes from players %d on foot / %d driving / %d by boat, %d helicopter landing spot(s)",
            what, g_learnKey, kinds[BL_FOOT], kinds[BL_GROUND], kinds[BL_BOAT], kinds[BL_AIR], bad, open, jump, g_learn.nHeat, routes[BL_FOOT], routes[BL_GROUND], routes[BL_BOAT], g_learn.nPads);
}
static void learn_save(int force)
{
    if (!g_learnLoaded) return;
    // the live map picture of this level and its bounds, so Server Control can draw what was learned on the map (a new
    // or changed picture is a reason to save)
    if (g_lmPic.phase == 3 && g_lmPic.w > 0 && !strcmp(g_lmPic.level, g_level)) {
        char line[200]; snprintf(line, sizeof line, "map\tmap_%s.bmp\t%.1f %.1f %.1f %.1f\t%d %d", g_lmPic.level, g_lmPic.bounds[0], g_lmPic.bounds[1], g_lmPic.bounds[2], g_lmPic.bounds[3], g_lmPic.w, g_lmPic.h);
        int have = 0; for (int k = 0; k < g_learn.nExtra; k++) have |= !strcmp(g_learn.extra[k], line);
        if (!have) {
            int w = 0; for (int k = 0; k < g_learn.nExtra; k++) { if (!strncmp(g_learn.extra[k], "map\t", 4)) free(g_learn.extra[k]); else g_learn.extra[w++] = g_learn.extra[k]; }
            g_learn.nExtra = w;
            if (g_learn.nExtra < BL_MAX_EXTRA && (g_learn.extra[g_learn.nExtra] = _strdup(line))) { g_learn.nExtra++; g_learn.dirty = 1; }
        }
    }
    if (!g_learn.dirty && !force) return;
    char path[MAX_PATH + 128], tmp[MAX_PATH + 140]; learn_path(path, sizeof path, g_learnKey);
    // Changed on disk since we read / wrote it (Server Control deleted entries, another server on this map saved):
    // read it again and keep only what this session added on top.
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &a) && CompareFileTime(&a.ftLastWriteTime, &g_learnStamp)) {
        size_t n = 0; char *text = learn_read(path, &n, NULL);
        if (text) { bl_reload_keep_fresh(&g_learn, text, n); free(text); bot_log("learn: %s changed outside the server - merged before saving", path); }
    }
    size_t cap = bl_text_size(&g_learn); char *buf = malloc(cap);
    if (!buf) return;
    size_t n = bl_write(&g_learn, buf, cap);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    int ok = f && fwrite(buf, 1, n, f) == n; if (f) fclose(f);
    free(buf);
    if (ok && MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
        if (GetFileAttributesExA(path, GetFileExInfoStandard, &a)) g_learnStamp = a.ftLastWriteTime;
        bl_mark_saved(&g_learn);
    } else { DeleteFileA(tmp); bot_log("learn: could not write %s", path); }
}
static void learn_unload(void)
{
    if (g_learnLoaded) learn_save(0);
    bl_free(&g_learn); g_learnLoaded = g_learnApplied = g_learnMasksApplied = 0; g_learnKey[0] = 0;
    g_learnLandNodes = g_learnWaterNodes = NULL;
    memset(g_learnPending, 0, sizeof g_learnPending); learn_routes_free();
}
static void learn_load(const char *key)
{
    if (g_learnLoaded && !strcmp(key, g_learnKey)) return;
    learn_unload();
    bl_init(&g_learn, key); snprintf(g_learnKey, sizeof g_learnKey, "%s", key);
    char path[MAX_PATH + 128]; learn_path(path, sizeof path, key);
    size_t n = 0; char *text = learn_read(path, &n, &g_learnStamp);
    g_learnLoaded = 1;
    if (text) {
        if (bl_load_text(&g_learn, text, n) < 0) bot_log("learn: %s is not a learned file - left alone, starts fresh", path);
        free(text);
        bl_fade(&g_learn);   // (deaths age a little with every map load)
        learn_summary("loaded");
    } else bot_log("learn: nothing learned yet for %s - %s is written as bots learn", key, path);
    for (int k = BL_FOOT; k <= BL_BOAT; k++) {   // the routes players showed, for the route loader
        size_t len = bl_routes_map_text(&g_learn, k, NULL, 0);
        if (len < 80) continue;
        g_learnRoutes[k] = malloc(len + 1);
        if (g_learnRoutes[k]) g_learnRoutesLen[k] = bl_routes_map_text(&g_learn, k, g_learnRoutes[k], len + 1);
    }
}
static void learn_key(char *key, int n) { snprintf(key, n, g_modeName[0] ? "%s_%s" : "%s", g_level, g_modeName); }

// ---- events (enhanced.h's bm_learn) ----
static void learn_pending_start(int i, int spot, int kind, const float *p, const float *h)
{
    if (i < 0 || i >= MAX_BOTS || spot < 0) return;
    LearnPending *q = &g_learnPending[i];
    q->spot = spot; q->kind = kind; memcpy(q->p, p, 12); q->h[0] = h ? h[0] : 0; q->h[1] = h ? h[1] : 0; q->at = H->frame ? H->frame : 1;
}
static void learn_event(int i, int ev, const float *pos, const float *dir)
{
    if (!g_learnLoaded || !g_enhanced.learning) return;
    long now = (long)time(NULL);
    if (ev == BM_LEARN_JUMP) { if (i >= 0 && i < MAX_BOTS) g_learnJumpAt[i] = H->frame ? H->frame : 1; return; }
    if (!pos) return;
    if (ev == BM_LEARN_DEATH) { bl_note_death(&g_learn, pos); return; }
    if (ev == BM_LEARN_PAD_LANDED || ev == BM_LEARN_PAD_BLOCKED) {
        int fresh = g_learn.nPads, k = bl_note_pad(&g_learn, pos, ev == BM_LEARN_PAD_LANDED, now);
        if (k >= 0 && k >= fresh) bot_log("learn: a new helicopter landing spot at (%.0f, %.0f, %.0f)", pos[0], pos[1], pos[2]);
        return;
    }
    int kind = ev == BM_LEARN_STUCK_FOOT ? BL_FOOT : ev == BM_LEARN_STUCK_GROUND ? BL_GROUND : ev == BM_LEARN_STUCK_BOAT ? BL_BOAT :
               ev == BM_LEARN_AIR_LOSS ? BL_AIR : -1;
    if (kind < 0) return;
    float h[2] = {dir ? dir[0] : 0, dir ? dir[1] : 0}, l = sqrtf(h[0] * h[0] + h[1] * h[1]);
    if (l > 0.01f) { h[0] /= l; h[1] /= l; } else h[0] = h[1] = 0;
    // (the same bot stuck again at the spot it is already being judged at: one event)
    if (i >= 0 && i < MAX_BOTS && g_learnPending[i].at && g_learnPending[i].kind == kind && dist2(g_learnPending[i].p, pos) < (kind == BL_FOOT ? 2.5f * 2.5f : 8 * 8)) return;
    int spot = bl_note_stuck(&g_learn, kind, pos, h, now);
    g_learnStats[0]++;
    if (kind == BL_AIR) { bl_note_result(&g_learn, spot, 0, 0, now); bot_log("learn: an aircraft was lost close to the ground at (%.0f, %.0f, %.0f) - pilots keep more height there", pos[0], pos[1], pos[2]); return; }
    learn_pending_start(i, spot, kind, pos, h);
}
// ---- questions (enhanced.h's bm_learn_query) ----
static int learn_ask(int i, int q, const float *pos, const float *dir)
{
    (void)i;
    if (!g_learnLoaded || !g_enhanced.learning || !pos) return 0;
    if (q == BM_ASK_FOOT_KEEP_OPEN) {
        int k = bl_find(&g_learn, BL_FOOT, pos, dir, 3.0f);
        if (k < 0 || !bl_spot_passable(&g_learn.spots[k])) return 0;
        g_learnStats[5]++;
        return bl_spot_jump(&g_learn.spots[k]) ? 2 : 1;
    }
    if (q == BM_ASK_AIR_CLEARANCE_JET || q == BM_ASK_AIR_CLEARANCE_HELI) {
        int jet = q == BM_ASK_AIR_CLEARANCE_JET; float r = jet ? 500.0f : 150.0f, extra = 0;
        for (int k = 0; k < g_learn.nSpots; k++) {
            const BLSpot *s = &g_learn.spots[k];
            if (s->kind != BL_AIR || !s->failed) continue;
            float dx = s->p[0] - pos[0], dz = s->p[2] - pos[2];
            if (dx * dx + dz * dz > r * r) continue;
            extra = fmaxf(extra, fminf(jet ? 90.0f : 45.0f, (jet ? 35.0f : 15.0f) * s->failed));
        }
        return (int)extra;
    }
    return 0;
}
// ---- the routes players showed, for the trace loader (enhanced.h's bm_learn_routes) ----
static const char *learn_routes_for(const char *key, int kind, size_t *len)
{
    if (!g_enhanced.learning || kind < BL_FOOT || kind > BL_BOAT) return NULL;
    learn_load(key);
    *len = g_learnRoutesLen[kind];
    return g_learnRoutes[kind];
}
static float learn_danger_at(float x, float z) { return g_learnLoaded && g_enhanced.learning ? bl_danger(&g_learn, x, z) : 0; }
static int learn_pad_at(const float *center, float radius, int nth, float *out)
{ return g_learnLoaded && g_enhanced.learning && center ? bl_pad_near(&g_learn, center, radius, nth, out) : 0; }

// AltBotMod V21.1: drivers follow the Fun-Bots vehicle routes first (the driving map is only the fallback), so a stall
// spot nobody drives out of also makes the route links through it 60x as costly: drivers take another road when the
// network has one. Applied whenever the vehicle routes are (re)loaded and at once when a spot turns bad mid-round.
static int learn_route_avoid(const BLSpot *sp)
{
    TraceNet *t = sp->kind == BL_BOAT ? &g_traceWater : sp->kind == BL_GROUND ? &g_traceLand : NULL;
    return t ? tn_penalize_near(t, sp->p, 10.0f, 60.0f) : 0;
}
static void learn_route_costs(void)
{
    if (g_traceLand.nodes == g_learnLandNodes && g_traceWater.nodes == g_learnWaterNodes) return;
    int links = 0, spots = 0;
    for (int k = 0; k < g_learn.nSpots; k++) {
        const BLSpot *sp = &g_learn.spots[k];
        if ((sp->kind != BL_GROUND && sp->kind != BL_BOAT) || !bl_spot_bad(sp)) continue;
        if ((sp->kind == BL_GROUND && g_traceLand.nodes == g_learnLandNodes) || (sp->kind == BL_BOAT && g_traceWater.nodes == g_learnWaterNodes)) continue;
        int n = learn_route_avoid(sp); links += n; spots += n > 0;
    }
    g_learnLandNodes = g_traceLand.nodes; g_learnWaterNodes = g_traceWater.nodes;
    if (spots) bot_log("learn: %d stall spot(s) drivers never got out of: %d vehicle route link(s) avoided", spots, links);
}
// ---- per tick ----
// Did the bot get past the spot it was stuck at? Through: 3 m (vehicles 12 m) past it the way it was going (or 6 / 20 m
// away when the way is unknown) within 15 s (vehicles 40 s). Still there after that: still stuck. Gone elsewhere
// (re-planned, died): no verdict.
static void learn_judge(void)
{
    long now = (long)time(NULL);
    for (int i = 0; i < H->botCount && i < MAX_BOTS; i++) {
        LearnPending *q = &g_learnPending[i];
        if (!q->at) continue;
        char *pl = H->bots[i], *s = pl ? *(char **)(pl + OFF_PLAYER_SOLDIER) : NULL;
        int vehicle = q->kind != BL_FOOT;
        float *p = NULL;
        if (s && vehicle) { char *v = player_vehicle(pl); p = v ? vehicle_pos(v, NULL) : NULL; }
        else if (s && !player_vehicle(pl)) p = (float *)(s + OFF_SOLDIER_POSITION);
        unsigned limit = (vehicle ? 40 : 15) * 30;
        if (!p) { if (H->frame - q->at > limit) q->at = 0; continue; }
        float dx = p[0] - q->p[0], dz = p[2] - q->p[2], along = q->h[0] || q->h[1] ? dx * q->h[0] + dz * q->h[1] : sqrtf(dx * dx + dz * dz);
        float pass = vehicle ? (q->h[0] || q->h[1] ? 12.0f : 20.0f) : (q->h[0] || q->h[1] ? 3.0f : 6.0f);
        if (along >= pass && fabsf(p[1] - q->p[1]) < (vehicle ? 8.0f : 3.0f)) {
            int jumped = !vehicle && g_learnJumpAt[i] && g_learnJumpAt[i] >= q->at;
            bl_note_result(&g_learn, q->spot, 1, jumped, now); g_learnStats[1]++; q->at = 0; continue;
        }
        if (H->frame - q->at < limit) continue;
        if (dx * dx + dz * dz < (vehicle ? 12.0f * 12.0f : 4.0f * 4.0f)) {
            int was = bl_spot_bad(&g_learn.spots[q->spot]);
            bl_note_result(&g_learn, q->spot, 0, 0, now); g_learnStats[2]++;
            const BLSpot *sp = &g_learn.spots[q->spot];
            if (vehicle && !was && bl_spot_bad(sp)) {   // (just turned bad: the routes avoid it from now on)
                int n = learn_route_avoid(sp);
                bot_log("learn: drivers never get out at (%.0f, %.0f) - %d vehicle route link(s) avoided from now on", sp->p[0], sp->p[2], n);
            }
        }
        q->at = 0;
    }
}
// A bot walking into a spot where bots got through with a jump jumps on the way in.
static void learn_prejump(void)
{
    for (int i = 0; i < H->botCount && i < MAX_BOTS; i++) {
        if ((H->frame + i) % 6) continue;
        char *pl = H->bots[i], *s = pl ? *(char **)(pl + OFF_PLAYER_SOLDIER) : NULL;
        if (!s || player_vehicle(pl) || (g_learnPreJumpAt[i] && H->frame - g_learnPreJumpAt[i] < 45)) continue;
        BotNav *nav = bot_nav(i);
        if (nav->mode != 3 && nav->mode != 1) continue;
        float *pos = (float *)(s + OFF_SOLDIER_POSITION), hx = nav->x - pos[0], hz = nav->z - pos[2], hl = sqrtf(hx * hx + hz * hz);
        if (hl < 0.5f) continue;
        hx /= hl; hz /= hl;
        for (int k = 0; k < g_learn.nSpots; k++) {
            const BLSpot *sp = &g_learn.spots[k];
            if (sp->kind != BL_FOOT || !bl_spot_jump(sp)) continue;
            float dx = sp->p[0] - pos[0], dz = sp->p[2] - pos[2], ahead = dx * hx + dz * hz, side = fabsf(dx * hz - dz * hx);
            if (ahead < 0.4f || ahead > 1.8f || side > 1.0f || fabsf(sp->p[1] - pos[1]) > 1.5f) continue;
            if ((sp->h[0] || sp->h[1]) && sp->h[0] * hx + sp->h[1] * hz < 0.5f) continue;
            press_button(i, 12, 4); g_learnPreJumpAt[i] = H->frame; g_learnStats[3]++;
            break;
        }
    }
}
// What the grid and the driving maps take from it: spots nobody gets through, once the map's data is ready (blocks on
// the grid run out after 3 minutes - renewed every 150 s, so deleting a spot in Server Control frees it within that).
static void learn_apply(void)
{
    NavGrid *g = *nav_slot();
    if (g && g->phase == 2 && (!g_learnApplied || H->frame - g_learnBlockAt >= 150 * 30)) {
        int links = 0, spots = 0;
        for (int k = 0; k < g_learn.nSpots; k++) {
            const BLSpot *sp = &g_learn.spots[k];
            if (sp->kind != BL_FOOT || !bl_spot_bad(sp) || (!sp->h[0] && !sp->h[1])) continue;
            int n = nav_block_towards(g, sp->p[0], sp->p[1], sp->p[2], sp->p[0] + sp->h[0] * 3, sp->p[2] + sp->h[1] * 3);
            links += n; spots += n > 0;
        }
        if (!g_learnApplied && spots) bot_log("learn: %d spot(s) on foot nobody ever got through: %d grid link(s) closed", spots, links);
        g_learnApplied = 1; g_learnBlockAt = H->frame;
    }
    if (!g_learnMasksApplied && drive_mask()) {
        int strikes = 0;
        for (int k = 0; k < g_learn.nSpots; k++) {
            const BLSpot *sp = &g_learn.spots[k];
            if ((sp->kind != BL_GROUND && sp->kind != BL_BOAT) || !bl_spot_bad(sp)) continue;
            const unsigned char *m = sp->kind == BL_BOAT ? boat_mask() : drive_mask();
            if (m && drive_strike((unsigned char *)m, sp->p[0], sp->p[2], 5.0f)) strikes++;
        }
        if (strikes) bot_log("learn: %d spot(s) drivers always stalled at taken off the driving maps", strikes);
        g_learnMasksApplied = 1;
    }
}

// ---- learning from players: the routes human players take ----
typedef struct LearnTrack { char *pl; int kind, n; float pts[BL_ROUTE_PTS][3]; unsigned lastAt, movedAt; } LearnTrack;
static LearnTrack g_learnTrack[8];
// Share of the points further than `far` from every waypoint of the net (new ground).
static float learn_new_ground(const TraceNet *t, const LearnTrack *tr, float reach)
{
    if (!t->count) return 1;
    int fresh = 0, tested = 0;
    for (int k = 0; k < tr->n; k += 2, tested++) {
        const float *p = tr->pts[k]; int close = 0;
        for (int j = 0; j < t->count && !close; j++) {
            const float *q = t->nodes[j].p; float dx = q[0] - p[0], dz = q[2] - p[2];
            close = dx * dx + dz * dz < reach * reach && fabsf(q[1] - p[1]) < 3.0f;
        }
        fresh += !close;
    }
    return tested ? (float)fresh / tested : 0;
}
static void learn_track_flush(LearnTrack *tr)
{
    if (tr->n >= 2 && tr->pl && player_exists(tr->pl)) {
        const TraceNet *net = tr->kind == BL_FOOT ? &g_trace : tr->kind == BL_BOAT ? &g_traceWater : &g_traceLand;
        float share = learn_new_ground(net, tr, tr->kind == BL_FOOT ? 5.0f : 12.0f);
        if (share >= 0.25f) {
            BLRoute probe; memset(&probe, 0, sizeof probe); probe.n = tr->n; memcpy(probe.pts, tr->pts, (size_t)tr->n * 12);
            float len = bl_route_length(&probe);
            if (bl_add_route(&g_learn, tr->kind, (const float (*)[3])tr->pts, tr->n, (long)time(NULL)) >= 0) {
                g_learnStats[4]++;
                bot_log("learn: %s showed a %.0f m %s route (%.0f%% new ground) - bots use it from the next map load", tr->pl + OFF_PLAYER_ONLINE_NAME, len,
                        tr->kind == BL_FOOT ? "walking" : tr->kind == BL_BOAT ? "boat" : "driving", share * 100);
            }
        }
    }
    tr->n = 0;
}
// Players' helicopter landings: the pilot seat of a helicopter held still (under 2 m/s) within 3 m of the ground for
// 2 s, 80 m or more from where the helicopter was when the player got in (not its spawn pad): a landing spot.
typedef struct LearnHeli { char *pl, *veh; float home[3], last[3]; unsigned stillAt, done; } LearnHeli;
static LearnHeli g_learnHeli[8];
static void learn_heli_tick(char *pl, char *v)
{
    LearnHeli *h = NULL;
    for (int k = 0; k < 8 && !h; k++) if (g_learnHeli[k].pl == pl) h = &g_learnHeli[k];
    const BMAirProfile *ap = v ? bm_air_profile(vehicle_name(v)) : NULL;
    if (!v || !ap || ap->kind == BM_AIR_JET || vehicle_seat_of(v, pl) != 0) { if (h) memset(h, 0, sizeof *h); return; }
    float *p = vehicle_pos(v, NULL); if (!p || !isfinite(p[0]) || !isfinite(p[1]) || !isfinite(p[2])) return;
    for (int k = 0; k < 8 && !h; k++) if (!g_learnHeli[k].pl) { h = &g_learnHeli[k]; memset(h, 0, sizeof *h); h->pl = pl; }
    if (!h) return;
    if (h->veh != v) { h->veh = v; memcpy(h->home, p, 12); memcpy(h->last, p, 12); h->stillAt = 0; h->done = 0; return; }
    float dx = p[0] - h->last[0], dy = p[1] - h->last[1], dz = p[2] - h->last[2], speed = sqrtf(dx * dx + dy * dy + dz * dz) * 2;   // (every 15 frames)
    memcpy(h->last, p, 12);
    float ground = 0; int low = air_ground(p, 0, 0, 0, &ground) && p[1] - ground < 3.0f;
    if (speed > 2.0f || !low) { h->stillAt = 0; if (p[1] - ground > 15) h->done = 0; return; }
    if (!h->stillAt) { h->stillAt = H->frame ? H->frame : 1; return; }
    if (h->done || H->frame - h->stillAt < 60 || dist2(p, h->home) < 80 * 80) return;
    float spot[3] = {p[0], ground, p[2]};
    h->done = 1;
    learn_event(-1, BM_LEARN_PAD_LANDED, spot, NULL);
}
static void learn_players_tick(void)
{
    if (!g_enhanced.learnFromPlayers || (H->frame % 15)) return;
    char *pm = player_manager(); void **arr = pm ? *(void ***)(pm + 0xac) : NULL; unsigned count = pm ? *(unsigned *)(pm + 8) : 0;
    for (int t = 0; t < 8; t++) if (g_learnTrack[t].pl && (!player_exists(g_learnTrack[t].pl) || is_bot(g_learnTrack[t].pl))) { learn_track_flush(&g_learnTrack[t]); g_learnTrack[t].pl = NULL; }
    for (int t = 0; t < 8; t++) if (g_learnHeli[t].pl && (!player_exists(g_learnHeli[t].pl) || is_bot(g_learnHeli[t].pl))) memset(&g_learnHeli[t], 0, sizeof g_learnHeli[t]);
    for (unsigned k = 0; arr && k < count && k < 64; k++) {
        char *pl = arr[k];
        if (!pl || is_bot(pl) || player_team(pl) < 1 || player_team(pl) > 2) continue;
        LearnTrack *tr = NULL;
        for (int t = 0; t < 8 && !tr; t++) if (g_learnTrack[t].pl == pl) tr = &g_learnTrack[t];
        for (int t = 0; t < 8 && !tr; t++) if (!g_learnTrack[t].pl) { tr = &g_learnTrack[t]; memset(tr, 0, sizeof *tr); tr->pl = pl; }
        if (!tr) continue;
        char *s = *(char **)(pl + OFF_PLAYER_SOLDIER), *v = player_vehicle(pl);
        learn_heli_tick(pl, s ? v : NULL);
        int kind = -1; float *p = NULL;
        if (s && !v && *(float *)(s + OFF_SOLDIER_HEALTH) > 0) { kind = BL_FOOT; p = (float *)(s + OFF_SOLDIER_POSITION); }
        else if (v && vehicle_seat_of(v, pl) == 0 && !bm_air_profile(vehicle_name(v)) && bm_vehicle_profile(vehicle_name(v))->kind != BM_AIR) {
            kind = vehicle_is_boat(v) ? BL_BOAT : BL_GROUND; p = vehicle_pos(v, NULL);
        }
        if (kind < 0 || !p || !isfinite(p[0]) || !isfinite(p[1]) || !isfinite(p[2])) { learn_track_flush(tr); continue; }
        if (tr->n && tr->kind != kind) learn_track_flush(tr);
        tr->kind = kind;
        float step = kind == BL_FOOT ? 3.0f : kind == BL_BOAT ? 12.0f : 10.0f;
        if (!tr->n) { memcpy(tr->pts[0], p, 12); tr->n = 1; tr->movedAt = H->frame; continue; }
        float *last = tr->pts[tr->n - 1], dx = p[0] - last[0], dz = p[2] - last[2], d = sqrtf(dx * dx + dz * dz);
        // a jump in position (respawn, teleport), 20 s without moving, or on foot a drop / climb steeper than any
        // stairs (jumping off a ledge, a ladder, vaulting a wall - nothing a bot can copy): the walk ends there.
        // Stairs (V21 fix of the first draft) rise about 0.7 m per metre and stay in one walk.
        float rise = p[1] - last[1], steep = fabsf(rise) > 1.6f && fabsf(rise) > d * 1.0f + 0.5f;
        if (d > (kind == BL_FOOT ? 25.0f : 80.0f) || (kind == BL_FOOT && steep) || H->frame - tr->movedAt > 20 * 30) {
            learn_track_flush(tr); memcpy(tr->pts[0], p, 12); tr->n = 1; tr->movedAt = H->frame; continue;
        }
        if (d < step) continue;
        tr->movedAt = H->frame;
        memcpy(tr->pts[tr->n++], p, 12);
        if (tr->n == BL_ROUTE_PTS) { float keep[3]; memcpy(keep, p, 12); learn_track_flush(tr); memcpy(tr->pts[0], keep, 12); tr->n = 1; }
    }
}

static void learn_tick(void)
{
    if (!g_enhanced.learning) {
        if (g_learnLoaded) { learn_unload(); bot_log("learn: off (Learning=0) - nothing learned is read or written"); }
        bm_learn_hook = NULL; bm_learn_ask = NULL; bm_learn_routes = NULL; bm_learn_danger = NULL; bm_learn_pad = NULL;
        return;
    }
    bm_learn_hook = learn_event; bm_learn_ask = learn_ask; bm_learn_routes = learn_routes_for; bm_learn_danger = learn_danger_at; bm_learn_pad = learn_pad_at;
    if (!level_ready() || !g_level[0]) return;
    char key[96]; learn_key(key, sizeof key);
    if (!g_learnLoaded || strcmp(key, g_learnKey)) { for (int t = 0; t < 8; t++) learn_track_flush(&g_learnTrack[t]); learn_load(key); }
    learn_apply();
    learn_route_costs();
    if (H->frame - g_learnJudgeAt >= 10) { g_learnJudgeAt = H->frame; learn_judge(); }
    learn_prejump();
    learn_players_tick();
    if (!g_learnSaveAt) g_learnSaveAt = H->frame;
    if (H->frame - g_learnSaveAt >= 60 * 30) {
        g_learnSaveAt = H->frame; learn_save(0);
        static unsigned reportAt;
        if (H->frame - reportAt >= 5 * 60 * 30) { reportAt = H->frame;
            bot_log("learn: this session - %u stuck note(s), %u got through, %u stayed stuck, %u learned jump(s) used, %u spot(s) kept open, %u route(s) from players",
                    g_learnStats[0], g_learnStats[1], g_learnStats[2], g_learnStats[3], g_learnStats[5], g_learnStats[4]); }
    }
}
