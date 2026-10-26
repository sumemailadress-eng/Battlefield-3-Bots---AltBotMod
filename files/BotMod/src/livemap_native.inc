// AltBotMod V13: live map - what is happening on the server, for Server Control's Live map page.
// Included in core.c after the vehicle, brain, director and sniper helpers. Off with LiveMap=0 ([CombatAI]); it then
// does nothing at all except drain the kill queue. Files (BotMod\LiveMap): see livemap.h.
//
// Cost when on: a position copy for each player every 3rd frame, one small text file (about 8 KB with 64 players)
// replaced every LiveMapSeconds, one line per kill / stuck spot, and every LiveMapTrailSeconds a short line per living
// soldier (about 2 MB an hour with 64 bots at 5 s). The map picture is drawn once per map, a strip of rows per frame.
#include "livemap.h"
#ifndef LM_SEP
#define LM_SEP "\\"
#endif
#ifndef LM_PLAYER_LIST
// Every player on the server (bots and people): the player manager's list.
static unsigned lm_players(void ***arr)
{
    char *pm = player_manager();
    if (!pm) { *arr = NULL; return 0; }
    *arr = *(void ***)(pm + 0xac);
    return *arr ? *(unsigned *)(pm + 8) : 0;
}
#endif

typedef struct LMSeen { char name[40]; float pos[3]; int team, bot; unsigned at; } LMSeen;
#define LM_SEEN 128
static LMSeen g_lmSeen[LM_SEEN];
static LMEvents g_lmEvents;
static int g_lmOn;                     // last state seen (to write a "stopped" picture once when switched off)
static long g_lmRound;                 // round id: wall clock time the round was first seen ready
static LONG g_lmEpoch = -1;
static char g_lmLevel[64];
static unsigned g_lmSnapAt, g_lmTrailAt, g_lmSeenAt;
static struct { void *who; LMProgress p; } g_lmProg[MAX_BOTS];
static unsigned g_lmKills, g_lmStucks;  // this run (status / tests)
// map picture: drawn from the nav grid a strip of rows per frame, written once per level and grid
static struct { unsigned serial; char level[64]; int phase, w, h; float bounds[4]; LMRender r; } g_lmPic;

static void lm_path(char *out, int n, const char *file)
{
    char dir[MAX_PATH + 40];
    botmod_file(dir, sizeof dir, "LiveMap");
    CreateDirectoryA(dir, NULL);
    snprintf(out, n, "%s" LM_SEP "%s", dir, file);
}
static LMSeen *lm_seen(const char *name, int create)
{
    LMSeen *old = NULL;
    for (int k = 0; k < LM_SEEN; k++) {
        if (g_lmSeen[k].name[0] && !strcmp(g_lmSeen[k].name, name)) return &g_lmSeen[k];
        if (!old || !g_lmSeen[k].name[0] || (old->name[0] && g_lmSeen[k].at < old->at)) old = &g_lmSeen[k];
    }
    if (!create || !old) return NULL;
    memset(old, 0, sizeof *old);
    snprintf(old->name, sizeof old->name, "%s", name);
    return old;
}
static float *lm_player_pos(char *pl)
{
    char *veh = player_vehicle(pl);
    float *p = veh && vehicle_ok(veh) ? vehicle_pos(veh, NULL) : NULL;
    if (!p) p = human_pos(pl);
    return p && isfinite(p[0]) && isfinite(p[2]) && (p[0] != 0 || p[2] != 0) ? p : NULL;
}
static int lm_alive(char *pl)
{
    char *s = *(char **)(pl + OFF_PLAYER_SOLDIER);
    return s && *(float *)(s + OFF_SOLDIER_HEALTH) > 0;
}
static void lm_line(const char *fmt, ...)
{
    if (!g_lmEvents.file) {
        char path[MAX_PATH + 80], name[48];
        snprintf(name, sizeof name, "events_%d.txt", H->gamePort);
        lm_path(path, sizeof path, name);
        long cap = (long)(g_enhanced.liveMapKeepMB > 0 ? g_enhanced.liveMapKeepMB : 64) * 1024L * 1024L;
        if (!lm_events_open(&g_lmEvents, path, cap)) return;
    }
    char line[512];
    va_list a; va_start(a, fmt); vsnprintf(line, sizeof line, fmt, a); va_end(a);
    lm_events_line(&g_lmEvents, line);
}

// What a bot is doing, in a few words, and where it is heading (0 = no goal).
static void lm_doing(int i, char *out, int n, float *goal)
{
    char *pl = H->bots[i];
    goal[0] = goal[1] = 0;
    if (!lm_alive(pl)) { snprintf(out, n, "dead"); return; }
    AirNativeState *a = &g_air[i];
    if (a->phase) {
        snprintf(out, n, "%s", a->phase == AIR_FLY ? "flying" : a->phase == AIR_RIDE ? "air passenger" : a->phase == AIR_WALK ? "to aircraft" : "waiting at aircraft");
        if (a->phase == AIR_FLY && (a->goal[0] || a->goal[2])) { goal[0] = a->goal[0]; goal[1] = a->goal[2]; }
        return;
    }
    DriveState *d = &g_drive[i];
    if (d->phase) {
        static const char *drive[] = {"", "to vehicle", "planning drive", "driving", "driving", "squad boarding", "waiting for squad", "riding"};
        snprintf(out, n, "%s", d->phase >= 1 && d->phase <= 7 ? drive[d->phase] : "driving");
        if (d->goal[0] || d->goal[2]) { goal[0] = d->goal[0]; goal[1] = d->goal[2]; }
        return;
    }
    if (player_vehicle(pl)) { snprintf(out, n, "gunner/passenger"); return; }
    BotBrain *b = bot_brain(i);
    BotNav *nav = bot_nav(i);
    if (nav->mode == 3 && (g_paths[i].goalX || g_paths[i].goalZ)) { goal[0] = g_paths[i].goalX; goal[1] = g_paths[i].goalZ; }
    else if (nav->mode && (nav->x || nav->z)) { goal[0] = nav->x; goal[1] = nav->z; }
    if (sniper_holding(i)) { snprintf(out, n, "sniper perch"); return; }
    if (b->task > TASK_NONE && b->task <= TASK_FIGHT) { snprintf(out, n, "%s", kTaskNames[b->task]); return; }
    int role, f = director_target(i, &role);
    if (f >= 0) snprintf(out, n, "%s %c", role == BM_OBJECTIVE_ATTACK ? "attack" : role == BM_OBJECTIVE_GUARD ? "guard" : "patrol", 'A' + f);
    else snprintf(out, n, "objectives");
}
// The rest of the bot's route (on foot or driving), every few points, as "x,z x,z ..."; empty when none.
static void lm_route(int i, char *out, int n)
{
    int k = 0; out[0] = 0;
    const float *pts = NULL; int from = 0, count = 0;
    if (g_drive[i].phase == 3) { pts = g_drive[i].pts; from = g_drive[i].next; count = g_drive[i].n; if (count > DRIVE_MAX_PTS) return; }
    else if (!g_drive[i].phase && !g_air[i].phase && bot_nav(i)->mode == 3) { pts = g_paths[i].pts; from = g_paths[i].next; count = g_paths[i].count; if (count > PATH_MAX_POINTS) return; }
    if (!pts || from < 0) return;
    int step = count - from > 24 ? (count - from) / 24 + 1 : 1;
    for (int p = from; p < count && k < n - 24; p += step) k += snprintf(out + k, n - k, "%s%.0f,%.0f", k ? " " : "", pts[p * 3], pts[p * 3 + 2]);
    if (count > from && (count - 1 - from) % step && k < n - 24) k += snprintf(out + k, n - k, " %.0f,%.0f", pts[(count - 1) * 3], pts[(count - 1) * 3 + 2]);
}

// player.onKill from RCON (chat_poll_kill): one "kill" line with both positions.
static void livemap_kill(const char *killer, const char *victim, const char *weapon, int headshot)
{
    char kn[40], vn[40], wn[64];
    lm_clean(kn, sizeof kn, killer); lm_clean(vn, sizeof vn, victim); lm_clean(wn, sizeof wn, weapon);
    char *kp = killer && killer[0] ? find_player(killer) : NULL, *vp = victim && victim[0] ? find_player(victim) : NULL;
    LMSeen *ks = killer && killer[0] ? lm_seen(killer, 0) : NULL, *vs = victim && victim[0] ? lm_seen(victim, 0) : NULL;
    float kpos[3] = {0}, vpos[3] = {0}; int haveK = 0, haveV = 0;
    float *p = kp ? lm_player_pos(kp) : NULL;
    if (p && lm_alive(kp)) { memcpy(kpos, p, 12); haveK = 1; } else if (ks) { memcpy(kpos, ks->pos, 12); haveK = 1; }
    if (vs) { memcpy(vpos, vs->pos, 12); haveV = 1; } else if (vp && (p = lm_player_pos(vp))) { memcpy(vpos, p, 12); haveV = 1; }
    if (!haveV) return;   // (nowhere to put it: a player never seen alive)
    if (!haveK || !strcmp(kn, vn) || !strcmp(kn, "-")) memcpy(kpos, vpos, 12);
    int kt = kp ? player_team(kp) : ks ? ks->team : 0, vt = vp ? player_team(vp) : vs ? vs->team : 0;
    int kb = kp ? is_bot(kp) : ks ? ks->bot : 0, vb = vp ? is_bot(vp) : vs ? vs->bot : 0;
    float dist = sqrtf((kpos[0] - vpos[0]) * (kpos[0] - vpos[0]) + (kpos[1] - vpos[1]) * (kpos[1] - vpos[1]) + (kpos[2] - vpos[2]) * (kpos[2] - vpos[2]));
    lm_line("kill\t%ld\t%ld\t%s\t%s\t%d\t%d\t%.1f\t%.1f\t%s\t%d\t%d\t%.1f\t%.1f\t%s\t%d\t%.0f",
            (long)time(NULL), g_lmRound, g_lmLevel, kn, kb, kt, kpos[0], kpos[2], vn, vb, vt, vpos[0], vpos[2], wn, headshot, dist);
    g_lmKills++;
}
// A bot got stuck (the mod's own watchdogs, and the live map's "no progress" check). kind: a few words.
static void livemap_stuck(int i, const float *pos, const char *kind)
{
    if (!g_enhanced.liveMap || !g_lmLevel[0] || i < 0 || i >= H->botCount || !pos) return;
    static unsigned lastAt[MAX_BOTS]; static float lastPos[MAX_BOTS][2];
    // (the same bot at the same spot within 10 s is one event)
    if (lastAt[i] && H->frame - lastAt[i] < 10 * 30 && fabsf(lastPos[i][0] - pos[0]) < 5 && fabsf(lastPos[i][1] - pos[2]) < 5) return;
    lastAt[i] = H->frame ? H->frame : 1; lastPos[i][0] = pos[0]; lastPos[i][1] = pos[2];
    char name[40]; lm_clean(name, sizeof name, (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME);
    lm_line("stuck\t%ld\t%ld\t%s\t%s\t%d\t%.1f\t%.1f\t%s", (long)time(NULL), g_lmRound, g_lmLevel, name, player_team(H->bots[i]), pos[0], pos[2], kind);
    g_lmStucks++;
}

static void lm_snapshot(void)
{
    char path[MAX_PATH + 80], tmp[MAX_PATH + 90], name[48];
    snprintf(name, sizeof name, "live_%d.txt", H->gamePort);
    lm_path(path, sizeof path, name);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return;
    char mode[64]; lm_clean(mode, sizeof mode, g_modeName);
    fprintf(f, "AltBotMod live map\t%d\t%s\r\n", LM_FORMAT, BM_ENHANCED_VERSION);
    fprintf(f, "time\t%ld\t%ld\t%s\t%s\t%d\t%u\t%u\t%u\r\n", (long)time(NULL), g_lmRound, g_lmLevel, mode, H->botCount, H->frame, g_lmKills, g_lmStucks);
    if (g_lmPic.phase == 3 && g_lmPic.w && !strcmp(g_lmPic.level, g_lmLevel))
        fprintf(f, "picture\tmap_%s.bmp\t%.1f\t%.1f\t%.1f\t%.1f\t%d\t%d\r\n", g_lmPic.level, g_lmPic.bounds[0], g_lmPic.bounds[1], g_lmPic.bounds[2], g_lmPic.bounds[3], g_lmPic.w, g_lmPic.h);
    for (int k = 0; k < g_flagCount; k++) { float *fp = flag_pos(k); fprintf(f, "flag\t%c\t%.1f\t%.1f\t%d\r\n", 'A' + k, fp[0], fp[2], flag_owner(k)); }
    void **arr; unsigned count = lm_players(&arr);
    for (unsigned k = 0; arr && k < count && k < 128; k++) {
        char *pl = arr[k];
        if (!pl) continue;
        char pn[40], doing[40] = "", veh[48] = "-", route[600];
        lm_clean(pn, sizeof pn, pl + OFF_PLAYER_ONLINE_NAME);
        int bi = bot_index(pl), alive = lm_alive(pl), team = player_team(pl);
        float *p = lm_player_pos(pl), goal[2] = {0, 0};
        LMSeen *s = lm_seen(pl + OFF_PLAYER_ONLINE_NAME, 0);
        float pos[3] = {0, 0, 0};
        if (p && alive) memcpy(pos, p, 12); else if (s) memcpy(pos, s->pos, 12);
        char *vehicle = player_vehicle(pl);
        if (vehicle && vehicle_ok(vehicle)) lm_clean(veh, sizeof veh, vehicle_name(vehicle));
        char *so = *(char **)(pl + OFF_PLAYER_SOLDIER);
        float health = so ? *(float *)(so + OFF_SOLDIER_HEALTH) : 0, yaw = so ? *(float *)(so + OFF_SOLDIER_YAW) * 57.29578f : 0;
        route[0] = 0;
        if (bi >= 0) { lm_doing(bi, doing, sizeof doing, goal); if (alive) lm_route(bi, route, sizeof route); }
        else snprintf(doing, sizeof doing, "%s", alive ? "player" : "dead");
        fprintf(f, "p\t%s\t%d\t%d\t%d\t%.1f\t%.1f\t%.1f\t%.0f\t%s\t%s\t%.1f\t%.1f\t%.0f\t%s\r\n", pn, bi >= 0, team, alive, pos[0], pos[1], pos[2], yaw,
                veh, doing, goal[0], goal[1], health, route[0] ? route : "-");
    }
    fprintf(f, "end\r\n");
    int ok = fclose(f) == 0;
    if (ok) MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING);   // (Server Control reads a whole picture, never half of one)
}
// The map picture, drawn 16 rows a frame (under 2 ms) so no frame stalls on it: a 1024-pixel picture takes 2 s.
static void lm_picture_tick(void)
{
    NavGrid *g = *nav_slot();
    if (!g || g->phase != 2 || !g_lmLevel[0]) return;
    if (g_lmPic.serial != g->serial || strcmp(g_lmPic.level, g_lmLevel)) {   // a new map / grid: start again
        lm_render_free(&g_lmPic.r); memset(&g_lmPic, 0, sizeof g_lmPic);
        g_lmPic.serial = g->serial; snprintf(g_lmPic.level, sizeof g_lmPic.level, "%s", g_lmLevel);
        if (!lm_render_begin(&g_lmPic.r, g, 1024)) { g_lmPic.phase = 3; bot_log("livemap: no map picture for %s (grid %dx%d)", g_lmLevel, g->w, g->h); return; }
        g_lmPic.phase = 1;
        return;
    }
    if (g_lmPic.phase != 1) return;
    if (g_lmPic.r.g != g) { lm_render_free(&g_lmPic.r); g_lmPic.serial = 0; return; }   // (grid replaced meanwhile)
    if (!lm_render_rows(&g_lmPic.r, 16)) return;
    char name[96], path[MAX_PATH + 120];
    snprintf(name, sizeof name, "map_%s.bmp", g_lmLevel);
    lm_path(path, sizeof path, name);
    int ok = lm_write_bmp(path, g_lmPic.r.rgb, g_lmPic.r.w, g_lmPic.r.h);
    g_lmPic.w = ok ? g_lmPic.r.w : 0; g_lmPic.h = g_lmPic.r.h; memcpy(g_lmPic.bounds, g_lmPic.r.bounds, sizeof g_lmPic.bounds);
    lm_render_free(&g_lmPic.r); g_lmPic.phase = 3;
    bot_log("livemap: map picture %s %dx%d %s", name, g_lmPic.r.w, g_lmPic.r.h, ok ? "written" : "could not be written");
}

static void livemap_tick(void)
{
    char k[40], v[40], w[64]; int hs;
    if (!g_enhanced.liveMap) {
        while (chat_poll_kill(k, sizeof k, v, sizeof v, w, sizeof w, &hs)) { }   // (nothing stale when switched back on)
        if (g_lmOn) {   // just switched off: say so in the picture, close the events file
            g_lmOn = 0;
            char path[MAX_PATH + 80], name[48];
            snprintf(name, sizeof name, "live_%d.txt", H->gamePort);
            lm_path(path, sizeof path, name);
            FILE *f = fopen(path, "wb");
            if (f) { fprintf(f, "AltBotMod live map\t%d\t%s\r\noff\r\nend\r\n", LM_FORMAT, BM_ENHANCED_VERSION); fclose(f); }
            lm_events_close(&g_lmEvents);
            bot_log("livemap: off (LiveMap=0)");
        }
        return;
    }
    bm_stuck_hook = livemap_stuck;   // (the watchdogs report through enhanced.h's bm_report_stuck)
    if (!g_lmOn) { g_lmOn = 1; g_lmSnapAt = g_lmTrailAt = 0; bot_log("livemap: on - BotMod\\LiveMap, picture every %.1f s, trail every %.0f s", g_enhanced.liveMapSeconds, g_enhanced.liveMapTrailSeconds); }
    // a new round or map: a round line (the round id is the time it started)
    if (level_ready() && g_level[0] && (g_lmEpoch != g_roundEpoch || strcmp(g_lmLevel, g_level))) {
        g_lmEpoch = g_roundEpoch; g_lmRound = (long)time(NULL);
        lm_clean(g_lmLevel, sizeof g_lmLevel, g_level);
        char mode[64]; lm_clean(mode, sizeof mode, g_modeName);
        lm_line("round\t%ld\t%ld\t%s\t%s", g_lmRound, g_lmRound, g_lmLevel, mode);
        memset(g_lmProg, 0, sizeof g_lmProg);
    }
    // between levels: nothing is read from soldiers (they are being torn down); kills meanwhile are dropped
    if (!level_ready() || !g_lmLevel[0]) { while (chat_poll_kill(k, sizeof k, v, sizeof v, w, sizeof w, &hs)) { } return; }
    // last known place of every soldier (a victim's soldier is gone by the time RCON reports the kill)
    if (H->frame - g_lmSeenAt >= 3) {
        g_lmSeenAt = H->frame;
        void **arr; unsigned count = lm_players(&arr);
        for (unsigned n = 0; arr && n < count && n < 128; n++) {
            char *pl = arr[n]; float *p;
            if (!pl || !lm_alive(pl) || !(p = lm_player_pos(pl))) continue;
            LMSeen *s = lm_seen(pl + OFF_PLAYER_ONLINE_NAME, 1);
            if (!s) continue;
            memcpy(s->pos, p, 12); s->team = player_team(pl); s->bot = is_bot(pl); s->at = H->frame ? H->frame : 1;
        }
    }
    for (int n = 0; n < 16 && chat_poll_kill(k, sizeof k, v, sizeof v, w, sizeof w, &hs); n++) livemap_kill(k, v, w, hs);
    // "no progress": a bot with a far goal that has not moved 4 m in 30 s (the watchdogs catch most; this catches the rest)
    for (int i = 0; i < H->botCount; i++) {
        if (g_lmProg[i].who != H->bots[i]) { memset(&g_lmProg[i], 0, sizeof g_lmProg[i]); g_lmProg[i].who = H->bots[i]; }
        if ((H->frame + i) % 15) continue;
        char *pl = H->bots[i]; float *p = lm_alive(pl) ? lm_player_pos(pl) : NULL, goal[2] = {0, 0}; char doing[40];
        float goalDist = 0;
        if (p) {
            lm_doing(i, doing, sizeof doing, goal);
            int holding = !strcmp(doing, "sniper perch") || !strcmp(doing, "defend") || !strcmp(doing, "revive") || !strcmp(doing, "repair") ||
                          !strcmp(doing, "supply") || !strcmp(doing, "fight") || !strcmp(doing, "waiting for squad") || !strcmp(doing, "gunner/passenger") ||
                          !strcmp(doing, "air passenger") || !strcmp(doing, "riding") || !strncmp(doing, "guard", 5);
            if (!holding && (goal[0] || goal[1])) goalDist = sqrtf((goal[0] - p[0]) * (goal[0] - p[0]) + (goal[1] - p[2]) * (goal[1] - p[2]));
        }
        if (lm_progress(&g_lmProg[i].p, p, goalDist, H->frame, 30 * 30)) livemap_stuck(i, p, "no progress");
    }
    unsigned snapFrames = (unsigned)(g_enhanced.liveMapSeconds * 30.0f + 0.5f), trailFrames = (unsigned)(g_enhanced.liveMapTrailSeconds * 30.0f + 0.5f);
    if (!g_lmTrailAt || H->frame - g_lmTrailAt >= trailFrames) {
        g_lmTrailAt = H->frame ? H->frame : 1;
        long now = (long)time(NULL);
        void **arr; unsigned count = lm_players(&arr);
        for (unsigned n = 0; arr && n < count && n < 128; n++) {
            char *pl = arr[n]; float *p;
            if (!pl || !lm_alive(pl) || !(p = lm_player_pos(pl))) continue;
            lm_line("move\t%ld\t%ld\t%s\t%d\t%d\t%.0f\t%.0f\t%d", now, g_lmRound, g_lmLevel, player_team(pl), is_bot(pl), p[0], p[2], player_vehicle(pl) != NULL);
        }
    }
    if (!g_lmSnapAt || H->frame - g_lmSnapAt >= snapFrames) {
        g_lmSnapAt = H->frame ? H->frame : 1;
        lm_snapshot();
        lm_events_flush(&g_lmEvents);
    }
    lm_picture_tick();
}
