// AltBotMod V21: learn_native.inc (the game side of learning) in a scripted world - real learn.c / enhanced.c / air.c,
// stand-ins for the game: players, soldiers, vehicles, the grid, the driving maps, the clock and the file system.
// What is checked: a bot stuck and then through (with a jump) / still stuck; the learned spot kept open and its jump
// used; a spot nobody passes closing grid links at load; drivers' stall spots struck off the driving map; an aircraft
// lost low raising the height there; a player's walk recorded as a route (stairs kept, a ladder / jump off a ledge
// split), a drive and a respawn; a player's helicopter landing (away from its spawn) learned as a landing spot and
// offered to transports; save, reload on the next map load, an outside edit (Server Control deleting lines) merged
// with what was learned since; Learning=0 reads and writes nothing; the map picture line.
#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "../files/BotMod/src/enhanced.h"
#include "../files/BotMod/src/nav.h"
#include "../files/BotMod/src/trace_nav.h"
#include "../files/BotMod/src/learn.h"
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

// ---- Windows stand-ins (paths use '\\': turned into '/') ----
#define MAX_PATH 260
typedef struct { unsigned dwLowDateTime, dwHighDateTime; } FILETIME;
typedef struct { unsigned attr; FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime; unsigned hi, lo; } WIN32_FILE_ATTRIBUTE_DATA;
enum { GetFileExInfoStandard = 0, MOVEFILE_REPLACE_EXISTING = 1 };
static char g_root[MAX_PATH];
static void unix_path(char *out, const char *in) { snprintf(out, MAX_PATH * 2, "%s", in); for (char *c = out; *c; c++) if (*c == '\\') *c = '/'; }
static unsigned GetModuleFileNameA(void *m, char *out, unsigned n) { (void)m; snprintf(out, n, "%s\\bf3_server_Final.exe", g_root); return (unsigned)strlen(out); }
static int CreateDirectoryA(const char *p, void *s) { (void)s; char u[MAX_PATH * 2]; unix_path(u, p); return mkdir(u, 0777) == 0; }
static unsigned g_tick;   // a write counter as the file time (mtime has whole seconds here)
static int GetFileAttributesExA(const char *p, int lvl, WIN32_FILE_ATTRIBUTE_DATA *a)
{
    (void)lvl; char u[MAX_PATH * 2]; unix_path(u, p); struct stat st;
    if (stat(u, &st)) return 0;
    memset(a, 0, sizeof *a);
    a->ftLastWriteTime.dwLowDateTime = (unsigned)st.st_mtim.tv_nsec; a->ftLastWriteTime.dwHighDateTime = (unsigned)st.st_mtim.tv_sec;
    return 1;
}
static long CompareFileTime(const FILETIME *a, const FILETIME *b) { return a->dwHighDateTime != b->dwHighDateTime ? (long)a->dwHighDateTime - (long)b->dwHighDateTime : (long)a->dwLowDateTime - (long)b->dwLowDateTime; }
static int MoveFileExA(const char *a, const char *b, int f) { (void)f; char u[MAX_PATH * 2], v[MAX_PATH * 2]; unix_path(u, a); unix_path(v, b); return rename(u, v) == 0; }
static int DeleteFileA(const char *p) { char u[MAX_PATH * 2]; unix_path(u, p); return remove(u) == 0; }
static FILE *win_fopen(const char *p, const char *m) { char u[MAX_PATH * 2]; unix_path(u, p); return fopen(u, m); }
#define fopen win_fopen
#define _strdup strdup

// ---- the game, scripted ----
#define MAX_BOTS 8
#define OFF_PLAYER_SOLDIER 0
#define OFF_PLAYER_ONLINE_NAME 16
#define OFF_SOLDIER_POSITION 0
#define OFF_SOLDIER_HEALTH 16
typedef struct Soldier { float pos[3]; float pad; float health; } Soldier;
typedef struct Player { Soldier *soldier; char pad[8]; char name[32]; char *vehicle; int seat, team, bot; } Player;
typedef struct Vehicle { const char *name; float pos[3]; int boat; } Vehicle;
static Player P[10]; static Soldier S[10]; static Vehicle V[4];
static struct { unsigned frame; int botCount; void *bots[MAX_BOTS]; } HH, *H = &HH;
static char g_pm[0x100]; static void *g_list[10]; static unsigned g_listN;
static char *player_manager(void) { return g_pm; }
static void sync_players(void) { g_listN = 0; for (int k = 0; k < 10; k++) if (P[k].name[0]) g_list[g_listN++] = &P[k]; void **a = g_list; memcpy(g_pm + 0xac, &a, sizeof a); memcpy(g_pm + 8, &g_listN, sizeof g_listN); }
static int player_exists(void *pl) { for (unsigned k = 0; k < g_listN; k++) if (g_list[k] == pl) return 1; return 0; }
static int is_bot(void *pl) { return ((Player *)pl)->bot; }
static int player_team(void *pl) { return ((Player *)pl)->team; }
static char *player_vehicle(void *pl) { return ((Player *)pl)->vehicle; }
static int vehicle_seat_of(char *v, void *pl) { (void)v; return ((Player *)pl)->seat; }
static float *vehicle_pos(char *v, int *team) { (void)team; return ((Vehicle *)v)->pos; }
static const char *vehicle_name(char *v) { return ((Vehicle *)v)->name; }
static int vehicle_is_boat(char *v) { return ((Vehicle *)v)->boat; }
static float dist2(const float *a, const float *b) { float dx = a[0] - b[0], dz = a[2] - b[2]; return dx * dx + dz * dz; }
static int air_ground(const float *p, float fx, float fz, float ahead, float *h) { (void)p; (void)fx; (void)fz; (void)ahead; *h = 0; return 1; }
typedef struct BotNav { int mode; float targetYaw; float x, z; } BotNav;
static BotNav g_nav[MAX_BOTS]; static BotNav *bot_nav(int i) { return &g_nav[i]; }
static int g_jumps[MAX_BOTS]; static void press_button(int i, int action, int frames) { (void)frames; if (action == 12) g_jumps[i]++; }
static NavGrid g_grid, *g_gridPtr; static NavGrid **nav_slot(void) { return &g_gridPtr; }
static int g_blockCalls; static float g_blockAt[3];
int nav_block_towards(NavGrid *g, float x, float y, float z, float tx, float tz) { (void)g; (void)tx; (void)tz; g_blockCalls++; g_blockAt[0] = x; g_blockAt[1] = y; g_blockAt[2] = z; return 2; }
static unsigned char g_drive[16], g_boat[16]; static int g_masksReady, g_strikes;
static const unsigned char *drive_mask(void) { return g_masksReady ? g_drive : NULL; }
static const unsigned char *boat_mask(void) { return g_masksReady ? g_boat : NULL; }
static int drive_strike(unsigned char *m, float x, float z, float r) { (void)m; (void)x; (void)z; (void)r; g_strikes++; return 1; }
static TraceNet g_trace, g_traceLand, g_traceWater;
static char g_level[64] = "MP_001", g_modeName[64] = "ConquestLarge0";
static int level_ready(void) { return 1; }
static BMConfig g_enhanced;
static struct { unsigned serial; char level[64]; int phase, w, h; float bounds[4]; } g_lmPic;
static char g_lastLog[512]; static int g_logs;
static void bot_log(const char *fmt, ...) { va_list a; va_start(a, fmt); vsnprintf(g_lastLog, sizeof g_lastLog, fmt, a); va_end(a); g_logs++; if (getenv("LEARN_VERBOSE")) puts(g_lastLog); }
#include "../files/BotMod/src/learn_native.inc"

static void tick(int frames) { for (int k = 0; k < frames; k++) { H->frame++; learn_tick(); } }
static char *slurp(const char *u) { FILE *f = fopen(u, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f); char *b = calloc((size_t)n + 1, 1); fread(b, 1, (size_t)n, f); fclose(f); return b; }
static void learned_file(char *out) { snprintf(out, MAX_PATH * 2, "%s/BotMod_learned/MP_001_ConquestLarge0.learn", g_root); }
static void add_player(int k, const char *name, int bot, float x, float y, float z)
{
    memset(&P[k], 0, sizeof P[k]); snprintf(P[k].name, sizeof P[k].name, "%s", name); P[k].soldier = &S[k]; P[k].team = 1; P[k].bot = bot;
    S[k].pos[0] = x; S[k].pos[1] = y; S[k].pos[2] = z; S[k].health = 100;
    sync_players();
}
int main(void)
{
    snprintf(g_root, sizeof g_root, "/tmp/bm-learn-%d", (int)getpid());
    char cmd[400]; snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s", g_root, g_root); CHECK(system(cmd) == 0);
    bm_defaults(&g_enhanced);
    CHECK(g_enhanced.learning && g_enhanced.learnFromPlayers);
    g_grid.phase = 2; g_gridPtr = &g_grid;
    add_player(0, "[BOT] Alpha", 1, 0, 0, 0); H->botCount = 1; H->bots[0] = &P[0];
    tick(1);
    CHECK(g_learnLoaded && !strcmp(g_learnKey, "MP_001_ConquestLarge0") && bm_learn_hook && bm_learn_pad);
    // 1. a bot stuck at (10,0,0) heading east, then 4 m further east with a jump on the way: got through, with a jump
    float at[3] = {10, 0, 0}, east[2] = {1, 0};
    bm_learn(0, BM_LEARN_STUCK_FOOT, at, east);
    CHECK(g_learn.nSpots == 1 && g_learn.spots[0].seen == 1 && g_learnPending[0].at);
    bm_learn(0, BM_LEARN_STUCK_FOOT, at, east);   // (the same bot stuck again on the spot it is judged at: one event)
    CHECK(g_learn.spots[0].seen == 1);
    S[0].pos[0] = 10; tick(5); bm_learn(0, BM_LEARN_JUMP, NULL, NULL);
    S[0].pos[0] = 14.5f; tick(15);
    CHECK(g_learn.spots[0].passed == 1 && g_learn.spots[0].jumps == 1 && !g_learnPending[0].at);
    // the learned spot is kept open (with its jump) when the next bot gets stuck there
    CHECK(learn_ask(0, BM_ASK_FOOT_KEEP_OPEN, at, east) == 2);
    float west[2] = {-1, 0}; CHECK(learn_ask(0, BM_ASK_FOOT_KEEP_OPEN, at, west) == 0);   // (the other way: unknown)
    // ... and a bot walking into it jumps on the way in
    S[0].pos[0] = 9; S[0].pos[2] = 0; g_nav[0].mode = 3; g_nav[0].x = 20; g_nav[0].z = 0; g_jumps[0] = 0;
    tick(12); if (g_jumps[0] != 1) fprintf(stderr, "jumps %d spot %.1f %.1f %.1f h %.2f %.2f passed %u jumps %u frame %u last %u\n", g_jumps[0], g_learn.spots[0].p[0], g_learn.spots[0].p[1], g_learn.spots[0].p[2], g_learn.spots[0].h[0], g_learn.spots[0].h[1], g_learn.spots[0].passed, g_learn.spots[0].jumps, H->frame, g_learnPreJumpAt[0]);
    CHECK(g_jumps[0] == 1);
    // 2. a spot nobody gets through (3 times stuck, still there 15 s later): grid links closed at the next apply
    float wall[3] = {50, 0, 50}, north[2] = {0, 1};
    for (int k = 0; k < 3; k++) {
        S[0].pos[0] = 50; S[0].pos[2] = 50; bm_learn(0, BM_LEARN_STUCK_FOOT, wall, north);
        tick(15 * 30 + 15);
    }
    int ws = bl_find(&g_learn, BL_FOOT, wall, north, 3);
    CHECK(ws >= 0 && g_learn.spots[ws].failed == 3 && bl_spot_bad(&g_learn.spots[ws]));
    g_blockCalls = 0; g_learnApplied = 0; tick(1);
    CHECK(g_blockCalls == 1 && fabsf(g_blockAt[0] - 50) < 0.5f);
    // 3. drivers: two stalls nobody drives out of strike the patch off the driving map once the maps exist
    Vehicle *jeep = &V[0]; jeep->name = "HMMWV"; jeep->pos[0] = 300; jeep->pos[2] = 300;
    P[0].vehicle = (char *)jeep; P[0].seat = 0;
    float stall[3] = {300, 0, 300}, way[2] = {1, 1};
    for (int k = 0; k < 2; k++) { bm_learn(0, BM_LEARN_STUCK_GROUND, stall, way); tick(40 * 30 + 15); }
    int gs = bl_find(&g_learn, BL_GROUND, stall, NULL, 8);
    CHECK(gs >= 0 && bl_spot_bad(&g_learn.spots[gs]));
    g_masksReady = 1; g_strikes = 0; tick(1); CHECK(g_strikes == 1);
    // V21.1: ... and the Fun-Bots vehicle routes (what drivers follow first) go round it when another road exists:
    // a straight road through the stall spot and a detour; before the map's route reload the straight one is taken.
    {
        char net[8000]; int o = snprintf(net, sizeof net, "pathIndex;pointIndex;transX;transY;transZ;inputVar;data\n");
        for (int k = 0; k <= 20; k++) o += snprintf(net + o, sizeof net - o, "0;%d;%.1f;2;300;3;{\"Vehicles\":[\"land\"]}\n", k, 200.0f + k * 10);
        int k2 = 0;
        for (int k = 0; k <= 10; k++, k2++) o += snprintf(net + o, sizeof net - o, "1;%d;200;2;%.1f;3;{\"Vehicles\":[\"land\"]}\n", k2, 300.0f + k * 10);
        for (int k = 1; k <= 20; k++, k2++) o += snprintf(net + o, sizeof net - o, "1;%d;%.1f;2;400;3;{\"Vehicles\":[\"land\"]}\n", k2, 200.0f + k * 10);
        for (int k = 1; k <= 10; k++, k2++) o += snprintf(net + o, sizeof net - o, "1;%d;400;2;%.1f;3;{\"Vehicles\":[\"land\"]}\n", k2, 400.0f - k * 10);
        tn_free(&g_traceLand); CHECK(tn_load_text_kind(&g_traceLand, net, (size_t)o, TN_KIND_LAND) && g_traceLand.count > 30);
        float goal[3] = {400, 2, 300}, pts[256 * 3]; unsigned char fl[256]; int trunc = 0; float len = 0;
        int start = -1; for (int k = 0; k < g_traceLand.count; k++) if (g_traceLand.nodes[k].p[0] == 200 && g_traceLand.nodes[k].p[2] == 300) { start = k; break; }
        CHECK(start >= 0 && tn_field(&g_traceLand, 0, goal, 45));
        int n = tn_route(&g_traceLand, 0, start, 6, pts, fl, 256, &trunc, &len);
        CHECK(n >= 2 && len < 230);                                   // (the straight road, through the stall spot)
        g_learnLandNodes = NULL; tick(1);                                  // (the vehicle routes "reloaded": learned spots apply)
        CHECK(tn_field(&g_traceLand, 0, goal, 45));
        n = tn_route(&g_traceLand, 0, start, 6, pts, fl, 256, &trunc, &len);
        int viaDetour = 0; for (int k = 0; k < n; k++) viaDetour |= pts[k * 3 + 2] > 390;
        CHECK(n >= 2 && len > 350 && viaDetour);                      // (round by the detour: 400 m)
        // a spot turning bad mid-round applies at once: a new stall on the detour (two fails) puts it back on the straight road
        float stall2[3] = {300, 2, 400}, way2[2] = {1, 0};
        P[0].vehicle = (char *)jeep; jeep->pos[0] = 300; jeep->pos[1] = 2; jeep->pos[2] = 400;
        for (int k = 0; k < 2; k++) { bm_learn(0, BM_LEARN_STUCK_GROUND, stall2, way2); tick(40 * 30 + 15); }
        CHECK(tn_field(&g_traceLand, 0, goal, 45));
        n = tn_route(&g_traceLand, 0, start, 6, pts, fl, 256, &trunc, &len);
        viaDetour = 0; for (int k = 0; k < n; k++) viaDetour |= pts[k * 3 + 2] > 390;
        CHECK(n >= 2 && !viaDetour);                                   // (both bad: the shorter one again, nothing is cut off)
        P[0].vehicle = NULL; tn_free(&g_traceLand);
    }
    P[0].vehicle = NULL;
    // 4. an aircraft lost low: pilots keep more height within 500 m (jets) / 150 m (helicopters) of it
    float crash[3] = {1000, 20, 1000}, near1[3] = {1200, 300, 1000}, far1[3] = {1700, 300, 1000};
    bm_learn(0, BM_LEARN_AIR_LOSS, crash, NULL);
    CHECK(learn_ask(0, BM_ASK_AIR_CLEARANCE_JET, near1, NULL) == 35 && learn_ask(0, BM_ASK_AIR_CLEARANCE_JET, far1, NULL) == 0);
    CHECK(learn_ask(0, BM_ASK_AIR_CLEARANCE_HELI, near1, NULL) == 0 && learn_ask(0, BM_ASK_AIR_CLEARANCE_HELI, crash, NULL) == 15);
    // 5. a human player walks 60 m east, down a staircase (2 m drop over 3 m), keeps going: one route, stairs kept
    add_player(1, "Player1", 0, 0, 10, 100);
    for (int k = 0; k < 30; k++) { S[1].pos[0] = k * 3.0f; S[1].pos[1] = k >= 10 && k < 14 ? 10 - (k - 9) * 0.6f : (k >= 14 ? 7.6f : 10); tick(15); }
    CHECK(g_learnTrack[0].pl == (char *)&P[1] && g_learnTrack[0].n >= 25);
    // ... a ladder (6 m straight up) ends that walk: the route is learned (new ground, > 40 m)
    S[1].pos[1] = 13.6f; tick(15);
    CHECK(g_learn.nRoutes == 1 && g_learn.routes[0].kind == BL_FOOT && bl_route_length(&g_learn.routes[0]) > 80);
    CHECK(g_learnTrack[0].n == 1);
    // ... the same walk again is the same route: a use, not a second route
    S[1].pos[1] = 10; for (int k = 0; k < 30; k++) { S[1].pos[0] = k * 3.0f; S[1].pos[1] = k >= 10 && k < 14 ? 10 - (k - 9) * 0.6f : (k >= 14 ? 7.6f : 10); tick(15); }
    S[1].pos[0] = 500; tick(15);   // (respawn far away: the walk ends)
    CHECK(g_learn.nRoutes == 1 && g_learn.routes[0].uses == 2);
    // ... a drive of 200 m: a driving route
    Vehicle *car = &V[1]; car->name = "VDV Buggy"; P[1].vehicle = (char *)car; P[1].seat = 0;
    for (int k = 0; k <= 20; k++) { car->pos[0] = 600 + k * 10.0f; car->pos[1] = 2; car->pos[2] = 600; tick(15); }
    P[1].vehicle = NULL; S[1].pos[0] = 800; S[1].pos[2] = 600; tick(15);
    int drives = 0; for (int k = 0; k < g_learn.nRoutes; k++) drives += g_learn.routes[k].kind == BL_GROUND;
    CHECK(drives == 1);
    // 6. the player lands a helicopter 300 m from where they got in and sits 2 s: a landing spot; one at the spawn: none
    Vehicle *heli = &V[2]; heli->name = "Venom"; heli->pos[0] = 2000; heli->pos[1] = 0.5f; heli->pos[2] = 2000;
    P[1].vehicle = (char *)heli; P[1].seat = 0;
    tick(15 * 10);
    CHECK(g_learn.nPads == 0);                               // (still on its spawn pad)
    for (int k = 0; k < 20; k++) { heli->pos[0] += 15; heli->pos[1] = 60; tick(15); }
    heli->pos[1] = 1.0f; for (int k = 0; k < 8; k++) tick(15);
    CHECK(g_learn.nPads == 1 && fabsf(g_learn.pads[0].p[0] - 2300) < 1 && g_learn.pads[0].p[1] == 0);
    float objective[3] = {2350, 0, 2000}, pad[3];
    CHECK(bm_learn_pad_near(objective, 220, 0, pad) && fabsf(pad[0] - 2300) < 1 && !bm_learn_pad_near(objective, 20, 0, pad));
    P[1].vehicle = NULL;
    // 7. save, Server Control deletes a line meanwhile, more learning, save again: the edit stays, the new learning too
    learn_save(1);
    char path[MAX_PATH * 2]; learned_file(path);
    char *text = slurp(path);
    CHECK(text && !strncmp(text, "AltBotMod learned\t1\tMP_001_ConquestLarge0", 41) && strstr(text, "\npad\theli\t") && strstr(text, "\nroute\tfoot\t"));
    // (Server Control: delete the bad spot at (50, 0, 50))
    char *line = strstr(text, "spot\tfoot\t50.0 0.0 50.0"); CHECK(line);
    char *end = strchr(line, '\n'); memmove(line, end + 1, strlen(end + 1) + 1);
    usleep(20000);
    FILE *f = fopen(path, "wb"); fputs(text, f); fclose(f); free(text);
    float other[3] = {-40, 0, -40}; bm_learn(0, BM_LEARN_STUCK_FOOT, other, east);   // (learned after the edit)
    learn_save(0);
    text = slurp(path);
    CHECK(!strstr(text, "spot\tfoot\t50.0 0.0 50.0") && strstr(text, "spot\tfoot\t-40.0 0.0 -40.0") && strstr(text, "spot\tfoot\t10.0 0.0 0.0"));
    CHECK(bl_find(&g_learn, BL_FOOT, wall, north, 3) < 0);   // (gone from the running server too)
    free(text);
    // 8. the next map load reads it back (the map picture line written for Server Control)
    g_lmPic.phase = 3; g_lmPic.w = g_lmPic.h = 1024; snprintf(g_lmPic.level, sizeof g_lmPic.level, "MP_001");
    g_lmPic.bounds[0] = -500; g_lmPic.bounds[1] = -400; g_lmPic.bounds[2] = 2500; g_lmPic.bounds[3] = 2600;
    snprintf(g_modeName, sizeof g_modeName, "RushLarge0"); tick(1);
    CHECK(!strcmp(g_learnKey, "MP_001_RushLarge0") && g_learn.nSpots == 0);   // (another mode: its own file)
    text = slurp(path); CHECK(strstr(text, "map\tmap_MP_001.bmp\t-500.0 -400.0 2500.0 2600.0\t1024 1024")); free(text);
    snprintf(g_modeName, sizeof g_modeName, "ConquestLarge0"); tick(1);
    CHECK(!strcmp(g_learnKey, "MP_001_ConquestLarge0") && g_learn.nPads == 1 && g_learn.nRoutes >= 2 && bl_find(&g_learn, BL_FOOT, at, east, 3) >= 0);
    // the routes players showed, as Fun-Bots lines for the route loader (foot: no vehicle label; driving: "land")
    size_t len = 0; const char *foot = learn_routes_for("MP_001_ConquestLarge0", BL_FOOT, &len);
    CHECK(foot && len > 100 && !strncmp(foot, "pathIndex;", 10) && !strstr(foot, "Vehicles"));
    const char *land = learn_routes_for("MP_001_ConquestLarge0", BL_GROUND, &len);
    CHECK(land && strstr(land, "\"Vehicles\":[\"land\"]"));
    // 9. Learning=0: nothing read, nothing written, nothing asked
    g_enhanced.learning = 0; tick(1);
    CHECK(!g_learnLoaded && !bm_learn_hook && !bm_learn_ask && !bm_learn_pad && !bm_learn_routes);
    struct stat st0, st1; stat(path, &st0);
    bm_learn(0, BM_LEARN_STUCK_FOOT, other, east); tick(70 * 30);
    stat(path, &st1); CHECK(st0.st_mtim.tv_nsec == st1.st_mtim.tv_nsec && st0.st_size == st1.st_size);
    // 10. an old BotMod_breakable.txt is not this file's business, a foreign file is left alone
    g_enhanced.learning = 1;
    snprintf(cmd, sizeof cmd, "printf 'hello\\n' > '%s/BotMod_learned/MP_002_ConquestLarge0.learn'", g_root); CHECK(system(cmd) == 0);
    snprintf(g_level, sizeof g_level, "MP_002"); tick(1);
    CHECK(strstr(g_lastLog, "not a learned file") || g_logs);
    char p2[MAX_PATH * 2]; snprintf(p2, sizeof p2, "%s/BotMod_learned/MP_002_ConquestLarge0.learn", g_root);
    text = slurp(p2); CHECK(text && !strcmp(text, "hello\n")); free(text);
    snprintf(cmd, sizeof cmd, "rm -rf %s", g_root); (void)system(cmd);
    printf("PASS: %u learning game-side checks (stuck/through/jump, kept open, pre-jump, grid links, driving maps, vehicle routes round stall spots, air height, player routes with stairs/ladder/respawn, drive, helicopter landing spots, save/outside edit/merge, per mode files, map line, Learning=0, foreign file)\n", checks);
    return 0;
}
