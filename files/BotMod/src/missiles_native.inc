// AltBotMod V23: guided missiles in flight (ServerMissileEntity, vtable 0x025cf640 - found from the exe's own type info:
// "ServerMissileEntity" -> its static type data -> "mov eax, typeinfo; ret" in slot 0 of the vtable, the same pattern
// that gives ServerVehicleEntity 0x025bf8e8). A worker thread finds them twice a second; the game thread reads where each
// one is and where it points, every tick. Uses:
//  1. a bot's own launch is confirmed (a new missile beside the launcher) - next to the magazine check;
//  2. a bot whose vehicle or aircraft a missile is heading for pops flares / smoke before it hits (Fun-Bots
//     CheckForFlareOrSmoke: within 350 m, its nose pointing at the vehicle); V16-V22 only did that after the hit.
// The missile's transform (right / up / forward / position, 16 floats like a vehicle's) is found the first time missiles
// fly: the offset whose basis is orthonormal and whose position moves like a missile between two reads.
#define ADDR_MISSILE_VTABLE 0x025cf640
#define MISSILE_MAX         32
#define MISSILE_WARN_RANGE  350.0f
#define MISSILE_WARN_ANGLE  0.45f      // rad between the missile's nose and the vehicle
static char *g_missileFound[2][MISSILE_MAX];
static volatile LONG g_missileFoundN[2], g_missileFoundCur, g_missileXfOff = -1, g_missileScans, g_missileCalibLogged;
static volatile LONG g_missileStop, g_missileNeeded;   // (needed: set on the game thread - a bot in a vehicle or at a launcher)
static HANDLE g_missileThread;
typedef struct MissileTrack { char *m; unsigned first, last; float firstPos[3], pos[3], fwd[3]; } MissileTrack;
static MissileTrack g_missiles[MISSILE_MAX];
static int g_missileCount;
static unsigned g_missileSeenTotal, g_missileWarnings;
static unsigned g_missileWarnAt[MAX_BOTS];

static int missile_read(const char *m, int off, float x[16])
{
    SIZE_T got = 0;
    return m && off >= 0 && ReadProcessMemory(GetCurrentProcess(), m + off, x, 64, &got) && got == 64;
}
// a rigid transform: three unit rows at right angles, a position on the map
static int missile_xf_plausible(const float *x)
{
    for (int r = 0; r < 4; r++) for (int k = 0; k < 3; k++) if (!isfinite(x[r * 4 + k])) return 0;
    for (int r = 0; r < 3; r++) { float l = x[r * 4] * x[r * 4] + x[r * 4 + 1] * x[r * 4 + 1] + x[r * 4 + 2] * x[r * 4 + 2]; if (fabsf(l - 1.0f) > 0.03f) return 0; }
    for (int a = 0; a < 3; a++) for (int b = a + 1; b < 3; b++) if (fabsf(x[a * 4] * x[b * 4] + x[a * 4 + 1] * x[b * 4 + 1] + x[a * 4 + 2] * x[b * 4 + 2]) > 0.03f) return 0;
    const float *p = x + 12;
    return fabsf(p[0]) < 8000 && fabsf(p[2]) < 8000 && p[1] > -500 && p[1] < 3000 && (fabsf(p[0]) > 1 || fabsf(p[2]) > 1);
}
// the offset of the missile's transform: plausible at two reads 150 ms apart, the position moved 2-120 m (a missile in
// flight), and the forward row points (roughly) along that move
static int missile_calibrate(char **list, int n)
{
    static unsigned short votes[0x400 / 4];
    for (int k = 0; k < n && k < 8; k++) {
        float a[0x400 / 4][16]; unsigned char ok[0x400 / 4] = {0};
        for (int off = 0x10; off + 64 <= 0x400; off += 4) { int s = off / 4; ok[s] = missile_read(list[k], off, a[s]) && missile_xf_plausible(a[s]); }
        Sleep(150);
        for (int off = 0x10; off + 64 <= 0x400; off += 4) {
            int s = off / 4; float b[16];
            if (!ok[s] || !missile_read(list[k], off, b) || !missile_xf_plausible(b)) continue;
            float d[3] = {b[12] - a[s][12], b[13] - a[s][13], b[14] - a[s][14]}, l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (l < 2.0f || l > 120.0f) continue;
            if ((d[0] * b[8] + d[1] * b[9] + d[2] * b[10]) / l < 0.5f) continue;   // flies nose first
            if (++votes[s] >= 2) { memset(votes, 0, sizeof votes); return off; }
        }
    }
    return -1;
}
static DWORD WINAPI missile_worker(LPVOID unused)
{
    (void)unused;
    static char *found[MISSILE_MAX];
    while (!g_missileStop) {
        if (!level_ready() || g_roundCleanup || !H->botCount || !g_missileNeeded) { for (int t = 0; t < 5 && !g_missileStop; t++) Sleep(100); continue; }   // (no scans nobody needs)
        int n = scan_objects(ADDR_MISSILE_VTABLE, found, MISSILE_MAX);
        LONG next = !g_missileFoundCur;
        memcpy(g_missileFound[next], found, sizeof found);
        InterlockedExchange(&g_missileFoundN[next], n);
        InterlockedExchange(&g_missileFoundCur, next);
        InterlockedIncrement(&g_missileScans);
        if (n && g_missileXfOff < 0) { int off = missile_calibrate(found, n); if (off >= 0) InterlockedExchange(&g_missileXfOff, off); }
        for (int t = 0; t < 4 && !g_missileStop; t++) Sleep(100);   // ~2 scans a second (a scan takes ~0.1 s)
    }
    return 0;
}
static void missile_start(void) { g_missileStop = 0; g_missileThread = CreateThread(NULL, 0, missile_worker, NULL, 0, NULL); }
static void missile_stop(void)
{
    g_missileStop = 1;
    if (g_missileThread) { if (WaitForSingleObject(g_missileThread, 3000) == WAIT_TIMEOUT) bot_log("WARNING: missile worker still running at unload"); CloseHandle(g_missileThread); g_missileThread = NULL; }
}
// A missile that appeared beside `at` (within 30 m + how far one flies in the time since) at or after `since`.
static int missile_launched_near(const float *at, unsigned since)
{
    for (int k = 0; k < g_missileCount; k++) {
        MissileTrack *t = &g_missiles[k];
        if (t->first + 5 < since || t->first > since + 4 * 30) continue;
        float dt = t->first > since ? (t->first - since) / 30.0f : 0.0f;
        if (dist2(t->firstPos, at) + (t->firstPos[1] - at[1]) * (t->firstPos[1] - at[1]) < (30 + 300 * dt) * (30 + 300 * dt)) return 1;
    }
    return 0;
}
// the bot that answers a missile for a vehicle: its driver / pilot when a bot; for the gunship any bot aboard
static void missile_warn(char *veh, const float *vp, float d)
{
    char **seats; int n = vehicle_seats(veh, &seats);
    for (int s = 0; s < n; s++) {
        char *pl = *(char **)(seats[s] + 0x4c); int i = pl ? bot_index(pl) : -1;
        if (i < 0 || i >= MAX_BOTS) { if (s == 0 && !vehicle_is_gunship(veh)) return; continue; }   // a human drives: theirs to answer
        if (g_missileWarnAt[i] && H->frame - g_missileWarnAt[i] < 6 * 30) return;
        g_missileWarnAt[i] = H->frame ? H->frame : 1; g_missileWarnings++;
        int aircraft = bm_vehicle_profile(vehicle_name(veh))->kind == BM_AIR;
        if (g_air[i].phase && g_air[i].veh == veh) { g_air[i].counterAt = H->frame; g_air[i].counterUntil = H->frame + 3; }
        else schedule_press(i, EIA_AIR_COUNTERMEASURE, H->frame + 1);
        bot_log("missile: %s's %s - a missile %.0f m away is coming at it: %s (%u warnings)", pl + OFF_PLAYER_ONLINE_NAME, vehicle_name(veh), d,
                aircraft ? "flares" : "smoke", g_missileWarnings);
        (void)vp;
        return;
    }
}
static void missile_tick(void)
{
    if (!level_ready() || g_roundCleanup) { g_missileCount = 0; InterlockedExchange(&g_missileNeeded, 0); return; }
    if (H->frame % 15 == 0) {   // (twice a second: does any bot need the missile scan - in a vehicle / aircraft, or at a lock-on launcher)
        LONG need = 0;
        for (int i = 0; i < H->botCount && !need; i++) if (player_vehicle(H->bots[i]) || g_lockFirstAt[i]) need = 1;
        InterlockedExchange(&g_missileNeeded, need);
    }
    if (g_missileXfOff >= 0 && !g_missileCalibLogged) { g_missileCalibLogged = 1; bot_log("missile: missiles in flight are tracked (transform at +0x%lx)", (unsigned long)g_missileXfOff); }
    LONG cur = g_missileFoundCur; int n = g_missileFoundN[cur]; char **list = g_missileFound[cur];
    int off = g_missileXfOff;
    MissileTrack keep[MISSILE_MAX]; int kept = 0;
    for (int k = 0; k < n && k < MISSILE_MAX; k++) {
        char *m = list[k]; float x[16];
        if (IsBadReadPtr(m, 4) || *(unsigned *)m != ADDR_MISSILE_VTABLE || off < 0 || !missile_read(m, off, x) || !missile_xf_plausible(x)) continue;
        MissileTrack t = {0}; int known = 0;
        for (int j = 0; j < g_missileCount; j++) if (g_missiles[j].m == m) { t = g_missiles[j]; known = 1; break; }
        if (!known) { t.m = m; t.first = H->frame ? H->frame : 1; memcpy(t.firstPos, x + 12, 12); g_missileSeenTotal++; }
        t.last = H->frame; memcpy(t.pos, x + 12, 12); memcpy(t.fwd, x + 8, 12);
        keep[kept++] = t;
    }
    memcpy(g_missiles, keep, sizeof keep[0] * (size_t)kept); g_missileCount = kept;
    if (!g_enhanced.vehicleCountermeasures || !kept || (H->frame % 3)) return;
    char **vehs; int nv = vehicle_list(&vehs);
    for (int v = 0; v < nv && v < 256; v++) {
        char *veh = vehs[v];
        if (!vehicle_ok(veh) || !vehicle_occupied(veh) || stationary_weapon(veh)) continue;
        float *vp = vehicle_pos(veh, NULL); if (!vp) continue;
        for (int k = 0; k < kept; k++) {
            MissileTrack *t = &g_missiles[k];
            if (dist2(t->firstPos, vp) + (t->firstPos[1] - vp[1]) * (t->firstPos[1] - vp[1]) < 25.0f * 25.0f) continue;   // its own launch
            float r[3] = {vp[0] - t->pos[0], vp[1] - t->pos[1], vp[2] - t->pos[2]}, d = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
            if (d > MISSILE_WARN_RANGE || d < 3.0f) continue;
            float c = (r[0] * t->fwd[0] + r[1] * t->fwd[1] + r[2] * t->fwd[2]) / d;
            if (c < cosf(MISSILE_WARN_ANGLE)) continue;
            missile_warn(veh, vp, d);
            break;
        }
    }
}
