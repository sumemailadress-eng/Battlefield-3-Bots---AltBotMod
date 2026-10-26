// BF3 BotMod core - bot logic for bf3_server_Final.exe (Feb 2013 build, 32-bit). Hot-reloadable: loaded by the
// loader (xinput9_1_0.dll) which owns persistent state (BotModHost). Every hook installed here is undone in
// botmod_shutdown so the DLL can be unloaded and replaced while the server runs.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include "host.h"
#include "nav.h"
#include "chat.h"
#define BOT_ROSTER(pl) bot_roster(pl)   /* AltBotMod V28: a bot's roster identity (harnesses: its name) */
#include "enhanced.h"
#include "modes.h"
#include "director_policy.h"
#include "trace_nav.h"
#include "learn.h"
#include "zone_nav.h"   // AltBotMod V34: Fun-Bots objective mesh
static void trace_level_load(void);
static void zone_level_load(void); static int zone_plan(int i, char *soldier, float tx, float ty, float tz); static void zone_stuck(int i, const float *pos);
static int zone_guard_point(const float *mcom, unsigned seed, float *out); static void zone_stats(void); static unsigned g_zonePathAt[MAX_BOTS];   // AltBotMod V34 (zone_native.inc)
static char *g_supplyFailTo[MAX_BOTS]; static unsigned g_supplyFailAt[MAX_BOTS];   // AltBotMod V34: the caller a supply run gave up on
static int squad_order_flag(int team, int squad, int *defend); static int squad_order_rush(int i); static int squad_order_chat(const char *who, const char *cmd, const char *arg);
static void squad_order_radio(char *pl); static void squad_announce_tick(void);   // AltBotMod V34 (squad_orders.inc)

// ---- Engine addresses (bf3_server_Final.exe, image base 0x400000) ----
#define ADDR_SERVER_GAME_CONTEXT   0x02b3ee2c  // ServerGameContext* global; +0x30 = ServerPlayerManager*
#define ADDR_PM_CREATE_PLAYER      0x00e644b0  // ServerPlayerManager::createPlayer(u32 a1, int id, bool spectator)
#define ADDR_PM_SET_PLAYER_NAME    0x00e6ca00  // ServerPlayerManager::setPlayerName(ServerPlayer*, const char*) - also registers PlayerGhost
#define ADDR_PLAYER_SET_AI         0x00e14490  // ServerPlayer::setIsAIPlayer(bool) - clients hide AI players from the scoreboard
#define ADDR_ADD_TO_TEAM           0x0118ef80  // cdecl bool addToTeam(ServerPlayer*, int teamId, bool, bool) - as used by admin.movePlayer
#define ADDR_CTX_DISPATCH_MESSAGE  0x00549a20  // ServerGameContext::dispatchMessage(Message*) (thiscall on *ADDR_SERVER_GAME_CONTEXT)
#define ADDR_MSG_PLAYER_CREATE_CTOR 0x02307610 // ServerPlayerCreateMessage ctor; +0x28 = ServerPlayer*
#define ADDR_MSG_DTOR              0x005306a0  // Message dtor
#define ADDR_PLAYER_LEVEL_LOADED   0x00e19380  // ServerPlayer::onLevelLoaded() - sets +0x49c, sends ServerPlayer.LevelLoaded
#define ADDR_PLAYER_INIT_FOR_LEVEL 0x00e208e0  // ServerPlayer: runs per-player component init, sets +0x49d (PM does this for all players at level start)
#define ADDR_MSG_ENTERED_LEVEL_CTOR 0x023077f0 // ServerPlayerEnteredLevelMessage ctor; +0x28 = ServerPlayer*
#define ADDR_PLAYER_SET_BLUEPRINT  0x00e207f0  // ServerPlayer::setSelectedSoldierBlueprint(SoldierBlueprint*) (refcounted, stored at +0x480)
#define ADDR_ENTRYINPUT_CTOR       0x008c6440  // EntryInput::EntryInput() (thiscall), object ~0x2e0 bytes
#define ADDR_ENTRYINPUT_SET_LEVEL  0x008bef00  // EntryInput::setLevel(EntryInputAction, float); levels stored at +0x10 + action*4
#define ADDR_CSE_SPAWN             0x00ddbbd0  // ServerCharacterSpawnEntity::spawn(ServerPlayer*, a2, a3, a4), ret 0x10; human HQ deploy calls it as (player, -1, 1, 0)
#define ADDR_GHOST_ADD             0x00d820c0  // ServerGhostManager::addGhost(Ghost*, int)
#define ADDR_PLAYER_SELECT_UNLOCKS 0x00e331b0  // ServerPlayer::onSelectedUnlockAssets(NetworkPlayerSelectedUnlockAssetsMessage*) thiscall, ret 4
#define ADDR_PLAYER_SELECT_WEAPON  0x00e33040  // ServerPlayer::onSelectedWeapon(NetworkPlayerSelectedWeaponMessage*) thiscall, ret 4
#define ADDR_SOLDIER_VTABLE        0x025c3948  // ServerSoldierEntity vtable
#define ADDR_SOLDIER_PREPHYS       0x00e0a3d0  // ServerSoldierEntity::prePhysicsUpdate(frameInfo*), vtable slot 0x74
#define ADDR_SOLDIER_PREPHYS_ENTRY 0x00e0a7c0  // ServerSoldierEntity::prePhysicsUpdateEntry(entryIndex, EntryInput*), vtable slot 0x144

#define OFF_PLAYER_ONLINE_NAME     0x28        // inline char[] read by RCON listPlayers / scoreboard
#define OFF_PLAYER_IS_AI           0x318
#define OFF_PLAYER_TEAM            0x31c
#define OFF_PLAYER_INPUT           0x3c0       // EntryInput* read by the soldier every prePhysicsUpdate; NULL = no input applied
#define OFF_PLAYER_SOLDIER         0x3c4
#define OFF_SOLDIER_POSE       0x2d0     // int: 0 stand, 2 prone (+4: stance being changed to)
#define OFF_PLAYER_BLUEPRINT       0x480
#define OFF_PLAYER_ENTERED_LEVEL   0x49e
#define OFF_SOLDIER_POSITION       0x290       // Vec3 simulated position (stays 0,0,0 until the soldier is stepped)
#define OFF_SOLDIER_SPAWN_POSITION 0x4b0       // Vec3 transform (keeps tracking the seat while in a vehicle)
#define OFF_PLAYER_VEHICLE         0x3d8       // vehicle the player sits in (0 on foot)
#define OFF_PLAYER_SEAT            0x3dc       // seat index in that vehicle
#define ADDR_PLAYER_ENTER_VEHICLE  0x00e37950  // ServerPlayer::enterVehicle(vehicle, seatIndex), thiscall, ret 8
#define ADDR_PLAYER_EXIT_VEHICLE   0x00e365f0  // ServerPlayer::exitVehicle(0, 1) - what the ChangeVehicle (47) button calls
#define ADDR_PLAYER_ATTACH_SOLDIER 0x00e37180  // ServerPlayer::setSoldier(soldier) - re-attach control to the soldier
#define OFF_ENTITY_ENTRIES         0xfc        // entry components [begin, end); entry+0x4c = player in that seat
#define EIA_INTERACT               35
#define EIA_TOGGLE_PARACHUTE       36
#define ORDER_GETIN                15
#define ORDER_GETOUT               16
#define VEHICLE_BOARD_DIST         7.0f
#define GETIN_RADIUS               30.0f
#define OFF_SOLDIER_PITCH          0x21c       // look pitch (radians, negative = down)
#define PITCH_INPUT_SIGN           1.0f        // EIAPitch direction relative to OFF_SOLDIER_PITCH
static float g_pitchTest[MAX_BOTS];            // lookpitch test target per bot, used when g_pitchTestOn
static unsigned char g_pitchTestOn[MAX_BOTS];
static float g_aimPitch[MAX_BOTS];   // (rockets) wanted pitch while engaging
static unsigned char g_fineAim[MAX_BOTS];
#define OFF_SOLDIER_YAW            0x218       // heading (radians); positive EIAYaw input decreases it (~5 rad/s at 1.0)
// VeniceNetwork.Order (commo rose): order type at +0x48
#define OFF_ORDER_TYPE             0x48
#define ORDER_GOGOGO               8
#define ORDER_FOLLOWME             9
#define ORDER_NEEDBACKUP           14
#define ORDER_REQUESTAMMO          3
#define ORDER_REQUESTHEAL          1
#define ORDER_REQUESTRIDE          7
#define ORDER_REQUESTREPAIR        6
#define REPAIR_CALL_RADIUS         100.0f      // engineers answering "Need repair" (no line of sight needed)
#define REPAIR_CALL_VEHICLE_DIST   15.0f       // on foot: repair the closest friendly vehicle this near the caller
#define ORDER_NEGATIVE             10
#define ORDER_SORRY                11
#define ORDER_ROGERTHAT            13
#define ORDER_THANKYOU             12
#define FOLLOW_RADIUS              60.0f
#define FOLLOW_FRAMES              (5 * 60 * 30)
#define BACKUP_BOTS                2
#define STUCK_SECONDS              3          // trying to move but no 1 m of progress this long -> unstick
#define SAY_DELAY_FRAMES           (2 * 30)
#define SUPPLY_GIVEUP_FRAMES       (20 * 30)  // can't get within reach of the drop spot: Sorry, resume
#define DEFEND_RELEASE_RADIUS      50.0f      // GoGoGo from anyone this close to a post releases it
#define DEFEND_SPREAD              2.5f
#define NEVER                      0x7fffffffu   // bots reply ~2 s after the human's own voice line
#define ENTRYINPUT_SIZE            0x400
#define ADDR_ENTRYINPUT_SET_DT     0x008b9d30  // EntryInputState::setDeltaTime(float), stored at +0x1ec (0 = no movement)
#define BOT_TICK_DT                (1.0f / 30.0f)
#define OFF_INPUT_AIM_VALID        0x1dc       // 1 on human input when the aim below is set
#define OFF_INPUT_AIM_YAW          0x1f4       // authoritative aiming yaw (radians); the soldier faces this way
#define OFF_INPUT_AIM_PITCH        0x1f8       // authoritative aiming pitch (radians)
// EntryInputAction values from the engine's reflection table: Throttle 0, Strafe 1, Brake 2, Yaw 4, Pitch 5, Fire 7,
// CameraPitch 8, CameraYaw 9, Zoom 11, Jump 12, Reload 15, Sprint 32.
#define EIA_THROTTLE 0
#define EIA_STRAFE   1
#define EIA_BRAKE    2   // vehicles: brake, then reverse (S)
#define EIA_YAW      4
#define EIA_PITCH    5
#define EIA_FIRE     7
#define EIA_CHANGE_POSE 13
#define EIA_PRONE    14
#define EIA_ZOOM     11      // aim down the sights (held)
#define EIA_SELECT_WEAPON1 16      // SelectWeaponN selects loadout slot N (slot 1 = sidearm, 4 = gadget 1, 5 = gadget 2)
#define EIA_SWITCH_PRIMARY 25
// Loadout slot -> SelectWeapon action. Observed: SelectWeapon1 = primary (0), 2 = sidearm (1), 3 = slot 2 (engineer/recon
// gadget), 4 = slot 4 (medkit/ammo bag), 5 = slot 5 (defib/claymore/repair/beacon), 6 = grenade (6), 7 = knife (7).
static const int kSlotAction[8] = {16, 17, 18, 18, 19, 20, 21, 22};
#define SLOT_ACTION(s) ((s) >= 0 && (s) < 8 ? kSlotAction[s] : 16)
#define EIA_SELECT_WEAPON3 18

typedef void *(__attribute__((thiscall)) *PM_CreatePlayer_t)(void *pm, unsigned a1, int id, int spectator);
typedef void (__attribute__((thiscall)) *PM_SetPlayerName_t)(void *pm, void *player, const char *name);
typedef void (__attribute__((thiscall)) *Player_SetAI_t)(void *player, int isAI);
typedef void (__attribute__((thiscall)) *ThisVoid_t)(void *self);
typedef void (__attribute__((thiscall)) *DispatchMessage_t)(void *ctx, void *msg);
typedef void (__attribute__((thiscall)) *SetPtr_t)(void *self, void *p);
typedef void (__attribute__((thiscall)) *SetLevel_t)(void *input, int action, float level);
typedef void (__attribute__((thiscall)) *SetFloat_t)(void *self, float v);
typedef void *(__attribute__((thiscall)) *CseSpawn_t)(void *self, void *player, unsigned a2, unsigned a3, unsigned a4);
typedef void (__attribute__((thiscall)) *PlayerMsg_t)(void *player, void *msg);
typedef char (__cdecl *AddToTeam_t)(void *player, int teamId, int a, int b);

static BotModHost *H;
#include "lock_warn.h"   // AltBotMod V34: locks on vehicles (fed by on_lock_update)
static int g_driveOff;                  // console: drive off / on
static BMConfig g_enhanced;
static BMSteering g_enhancedSteer[MAX_BOTS];
#include "fleet_state.h"
#include "activity_state.h"
#include "mode_state.h"
#include "tactics_state.h"
static unsigned g_local_stuck_jump_lastAt[MAX_BOTS];
static float g_local_steer_bot_jx[MAX_BOTS], g_local_steer_bot_jz[MAX_BOTS];
static unsigned g_local_steer_bot_jat[MAX_BOTS];
static unsigned g_local_downed_tick_primaryAt[MAX_BOTS];
static unsigned g_local_brain_tick_nextTry[MAX_BOTS];
static unsigned char g_local_brain_tick_wasRiding[MAX_BOTS];
static unsigned g_local_cover_react_spottedAt[MAX_BOTS];
static unsigned g_local_repair_scan_whyAt[MAX_BOTS];
static unsigned g_local_repair_scan_losAt[MAX_BOTS];
static unsigned g_local_self_requests_ammoAt[MAX_BOTS], g_local_self_requests_healAt[MAX_BOTS];
static float g_local_rocket_tick_lastPos[MAX_BOTS][3];
static unsigned g_local_rocket_tick_lastAt[MAX_BOTS];
static unsigned char g_local_botmod_tick_fireHeld[MAX_BOTS];
static unsigned char g_local_botmod_tick_zoomHeld[MAX_BOTS];
static void enhanced_load_config(void);
static void enhanced_status(void);
static void enhanced_vehicles(void);
static void enhanced_forget(int i);
static void enhanced_level_reset(void);
static void air_tick(int i);
static int director_target(int i,int *role);
static void director_tick(void);
static void fleet_air_dispatch(void);
static void fleet_abandoned_dispatch(void);
static void air_consider(int i);
static void air_apply(int i, void *input);
static void air_status(void);
static int air_pilot(int i);
static int air_ride(int i, char *vehicle);
static void air_manual_guard(char *vehicle);
static float *vehicle_xf(char *veh);
static int vehicle_ok(char *veh);
static const char *vehicle_name(char *veh);
// Caspian V5: stationary weapons (TOW and Kornet launchers, C-RAM and Pantsir AA). 1 = anti-tank, 2 = anti-air.
static int stationary_weapon(char *veh)
{
    if (!veh) return 0;
    const char *raw = vehicle_name(veh); char n[64]; unsigned j = 0;
    for (unsigned k = 0; raw[k] && j + 1 < sizeof n; k++) if (isalnum((unsigned char)raw[k])) n[j++] = (char)toupper((unsigned char)raw[k]);
    n[j] = 0;
    if (strstr(n, "CRAM") || strstr(n, "PANTSIR") || strstr(n, "CENTURION") || strstr(n, "STINGER") || strstr(n, "IGLA")) return 2;
    if (!strcmp(n, "TOW") || strstr(n, "TOWLAUNCHER") || strstr(n, "KORNET") || strstr(n, "M220")) return 1;
    return 0;
}
// AltBotMod V23: the AC-130 (named "GUNSHIP" on the server): it flies itself; its seats are guns. An aircraft target for
// everyone (V5-V22 knew it only as "some vehicle": AA launchers never looked at it, gunners scored it a ground target).
static int vehicle_is_gunship(char *veh)
{
    if (!veh) return 0;
    const char *raw = vehicle_name(veh); char n[64]; unsigned j = 0;
    for (unsigned k = 0; raw && raw[k] && j + 1 < sizeof n; k++) if (isalnum((unsigned char)raw[k])) n[j++] = (char)toupper((unsigned char)raw[k]);
    n[j] = 0;
    return strstr(n, "GUNSHIP") || strstr(n, "AC130");
}
static int vehicle_is_aircraft(char *veh) { return veh && (bm_air_profile(vehicle_name(veh)) || vehicle_is_gunship(veh)); }
static int enhanced_driver_weapon(char *veh)
{
    const BMVehicleProfile *p=bm_vehicle_profile(vehicle_name(veh));
    return g_enhanced.armored && g_enhanced.driverWeapons && bm_armored(p) && p->kind!=BM_AMTRAC;
}
static const char *kOrderNames[] = {"None", "RequestHeal", "LetMeHeal", "RequestAmmo", "LetMeGiveAmmo", "LetMeRepair", "RequestRepair",
    "RequestRide", "GoGoGo", "FollowMe", "Negative", "Sorry", "ThankYou", "RogerThat", "NeedBackup", "GetIn", "GetOut", "Attack"};
#define bot_log H->log
// Set on shutdown: every memory scan (worker threads) stops at the next region, so a reload never waits on a
// long scan (the server's watchdog kills the process if the game loop stalls ~14 s).
static volatile LONG g_abortScans;
// AltBotMod V11: class jobs over the last minute (the "classes:" line): revive runs / back up, repairs started / done,
// supply runs / drops delivered, medkits / ammo boxes dropped unasked
enum { JOB_REVIVE_RUN, JOB_REVIVED, JOB_REPAIR, JOB_REPAIRED, JOB_SUPPLY_RUN, JOB_SUPPLY_DROP, JOB_MEDKIT, JOB_AMMO, JOB_COUNT };
static unsigned g_jobStats[JOB_COUNT];
static float *bot_aim(int i);
struct BotNav;
static void follow_path(int i, char *soldier, struct BotNav *nav);
static int route_hold_tick(int i, char *soldier, struct BotNav *nav);   // AltBotMod V15 (route_actions_native.inc)
static int *aim_mode(void);
#include <math.h>

// ---- Reversible patches ----
typedef struct Patch { unsigned char *addr; int len; unsigned char orig[16]; } Patch;
static Patch g_patches[24];
static int g_patchCount;

static int patch_bytes(void *addr, const void *bytes, int len)
{
    Patch *p = &g_patches[g_patchCount++];
    p->addr = addr;
    p->len = len;
    memcpy(p->orig, addr, len);
    DWORD old;
    VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &old);
    memcpy(addr, bytes, len);
    VirtualProtect(addr, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), addr, len);
    return 1;
}

static void unpatch_all(void)
{
    while (g_patchCount > 0) {
        Patch *p = &g_patches[--g_patchCount];
        DWORD old;
        VirtualProtect(p->addr, p->len, PAGE_EXECUTE_READWRITE, &old);
        memcpy(p->addr, p->orig, p->len);
        VirtualProtect(p->addr, p->len, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p->addr, p->len);
    }
}

static int detour(unsigned addr, const unsigned char *expect, int len, void *hook, const char *what)
{
    unsigned char *target = (unsigned char *)addr;
    if (memcmp(target, expect, len) != 0) { bot_log("ERROR: %s prologue mismatch - wrong server build?", what); return 0; }
    unsigned char jmp[16];
    jmp[0] = 0xE9;
    *(DWORD *)(jmp + 1) = (DWORD)((unsigned char *)hook - (target + 5));
    for (int i = 5; i < len; i++) jmp[i] = 0x90;
    patch_bytes(target, jmp, len);
    bot_log("hooked %s @ %p", what, target);
    return 1;
}

// Replaces a few instruction bytes after checking they are what this build should have.
static int patch_code(unsigned addr, const unsigned char *expect, const unsigned char *bytes, int len, const char *what)
{
    if (memcmp((void *)addr, expect, len) != 0) { bot_log("ERROR: %s bytes mismatch - wrong server build?", what); return 0; }
    patch_bytes((void *)addr, bytes, len);
    bot_log("patched %s @ %08x", what, addr);
    return 1;
}

static int patch_vslot(unsigned slotAddr, unsigned expect, void *hook, const char *what)
{
    if (*(unsigned *)slotAddr != expect) { bot_log("ERROR: vtable slot for %s mismatch (%08x)", what, *(unsigned *)slotAddr); return 0; }
    patch_bytes((void *)slotAddr, &hook, 4);
    bot_log("hooked %s (vtable)", what);
    return 1;
}

// ---- Players ----
static void *player_manager(void)
{
    char *ctx = *(char **)ADDR_SERVER_GAME_CONTEXT;
    return ctx ? *(void **)(ctx + 0x30) : NULL;
}

static int player_team(char *pl) { return *(int *)(pl + OFF_PLAYER_TEAM); }

static void *find_player(const char *name)
{
    char *pm = player_manager();
    if (!pm) return NULL;
    void **arr = *(void ***)(pm + 0xac);
    unsigned count = *(unsigned *)(pm + 8);
    for (unsigned i = 0; i < count; i++) {
        char *pl = arr[i];
        if (pl && !_stricmp(pl + OFF_PLAYER_ONLINE_NAME, name)) return pl;
    }
    return NULL;
}

static int bot_index(void *pl)
{
    for (int i = 0; i < H->botCount; i++) if (H->bots[i] == pl) return i;
    return -1;
}

static int is_bot(void *pl) { return bot_index(pl) >= 0; }

static int soldier_is_bot(void *soldier)
{
    for (int i = 0; i < H->botCount; i++)
        if (*(void **)((char *)H->bots[i] + OFF_PLAYER_SOLDIER) == soldier) return 1;
    return 0;
}

static void dispatch_player_message(unsigned ctor, void *player)
{
    void *ctx = *(void **)ADDR_SERVER_GAME_CONTEXT;
    unsigned char msg[0x80] __attribute__((aligned(16))) = {0};
    ((ThisVoid_t)ctor)(msg);
    *(void **)(msg + 0x28) = player;
    ((DispatchMessage_t)ADDR_CTX_DISPATCH_MESSAGE)(ctx, msg);
    ((ThisVoid_t)ADDR_MSG_DTOR)(msg);
}

// AltBotMod V28: gamertags (BotNames=1). A roster bot's identity - side, class, place in the roster - lives in its
// roster name ("[BOT] Support RU 2"); with gamertags the player is created under a made-up name and the roster name is
// kept here, keyed by the player, so everything that reads a bot's side or class from its name (bot_roster) still works.
// (A name can only be given at creation: renaming a player later re-registers its network ghost and trips an assert.)
#define ROSTER_MAX 128
static int roster_ordinal(const char *name); static int bot_index(void *pl); static void botmod_file(char *out, int n, const char *name);
static struct { void *pl; char roster[40]; } g_roster[ROSTER_MAX];
static const char *bot_roster(void *pl)
{
    if (!pl) return "";
    for (int k = 0; k < ROSTER_MAX; k++) if (g_roster[k].pl == pl) return g_roster[k].roster;
    return (const char *)pl + OFF_PLAYER_ONLINE_NAME;
}
static void *find_player(const char *name);
static void *find_roster(const char *roster)   // a roster slot's player, under whichever name it was created
{
    for (int k = 0; k < ROSTER_MAX; k++) if (g_roster[k].pl && !strcmp(g_roster[k].roster, roster) && bot_index(g_roster[k].pl) >= 0) return g_roster[k].pl;
    return find_player(roster);
}
static void roster_remember(void *pl, const char *roster)
{
    int slot = -1;
    for (int k = 0; k < ROSTER_MAX && slot < 0; k++) if (g_roster[k].pl == pl || !g_roster[k].pl || bot_index(g_roster[k].pl) < 0) slot = k;
    if (slot < 0) return;
    g_roster[slot].pl = pl; strncpy(g_roster[slot].roster, roster, sizeof g_roster[slot].roster - 1); g_roster[slot].roster[sizeof g_roster[slot].roster - 1] = 0;
}
// A gamertag for a new bot: from BotMod\BotMod_names.txt (one per line; # comments) when it is there, else made up from
// parts; never one a player on the server already has. Up to 15 characters, like a real one.
static void gamertag_make(char *cand, unsigned r)   // a made-up tag (16 bytes)
{
    static const char *a[] = {"Dark","Silent","Red","Ghost","Iron","Lone","Rapid","Shadow","Frost","Rogue","Toxic","Steel","Wild","Night","Crazy","Lucky","Grim","Sly","Mad","Big"};
    static const char *b[] = {"Wolf","Viper","Hawk","Sniper","Reaper","Fox","Tiger","Bear","Raven","Cobra","Falcon","Ninja","Spartan","Hunter","Bullet","Knight","Dragon","Shark","Rhino","Panda"};
    int style = r % 4;
    if (style == 0) snprintf(cand, 16, "%s%s%u", a[r / 4 % 20], b[r / 80 % 20], r / 1600 % 100);
    else if (style == 1) snprintf(cand, 16, "x%s%sx", a[r / 4 % 20], b[r / 80 % 20]);
    else if (style == 2) snprintf(cand, 16, "%s_%u", b[r / 4 % 20], 1980 + r / 80 % 30);
    else snprintf(cand, 16, "%s%s", a[r / 4 % 20], b[r / 80 % 20]);
}
static int gamertag_pick(char *out, int n)
{
    static char list[512][16]; static int count = -1, used;
    if (count < 0) {
        count = 0; char path[MAX_PATH + 40]; botmod_file(path, sizeof path, "BotMod_names.txt");
        FILE *f = fopen(path, "r");
        if (f) { char line[128];
            while (count < 512 && fgets(line, sizeof line, f)) {
                char *e = line + strcspn(line, "\r\n#"); *e = 0;
                while (e > line && e[-1] == ' ') *--e = 0;
                char *b = line; while (*b == ' ') b++;
                if (*b) { strncpy(list[count], b, 15); list[count][15] = 0; count++; }
            }
            fclose(f);
        }
        if (!f) {   // no list yet: write one (300 made-up tags) for the owner to edit, and use it
            FILE *w = fopen(path, "w");
            if (w) {
                fprintf(w, "# AltBotMod: gamertags the bots join under (BotNames=1). One per line, up to 15 characters; # starts a comment.\n"
                           "# Written by the mod because there was no list - edit it freely; delete it to have a new one made.\n");
                for (int k = 0; k < 300 && count < 512; k++) {
                    char c[16]; gamertag_make(c, (unsigned)k * 2654435761u + 12345u);
                    int dup = 0; for (int q = 0; q < count && !dup; q++) dup = !strcmp(list[q], c);
                    if (dup) continue;
                    strcpy(list[count++], c); fprintf(w, "%s\n", c);
                }
                fclose(w);
                bot_log("names: wrote %d made-up gamertags to BotMod_names.txt (edit it to choose your own)", count);
            }
        }
        bot_log("names: %d gamertag(s) from BotMod_names.txt%s", count, count ? "" : " - made up instead");
    }
    for (int tries = 0; tries < 200; tries++) {
        char cand[16];
        if (count > 0 && tries < 100) snprintf(cand, sizeof cand, "%s", list[((unsigned)rand() + (unsigned)(used++) * 7919u) % (unsigned)count]);
        else gamertag_make(cand, (unsigned)rand() * 2654435761u + (unsigned)tries * 40503u + H->frame);
        cand[15] = 0;
        if (find_player(cand)) continue;
        snprintf(out, n, "%s", cand); return 1;
    }
    return 0;
}
static void cmd_add_core(const char *name, int team, int isAI);
static void cmd_add(const char *name, int team, int isAI)
{
    char tag[16];
    int roster = name && !strncmp(name, "[BOT] ", 6) && roster_ordinal(name) >= 0;
    if (g_enhanced.botNames && roster && gamertag_pick(tag, sizeof tag)) {
        int before = H->botCount;
        cmd_add_core(tag, team, isAI);
        if (H->botCount > before) { roster_remember(H->bots[H->botCount - 1], name); bot_log("names: %s plays as %s", name, tag); }
        return;
    }
    cmd_add_core(name, team, isAI);
    if (roster && H->botCount > 0 && !strcmp(H->bots[H->botCount - 1] + OFF_PLAYER_ONLINE_NAME, name)) roster_remember(H->bots[H->botCount - 1], name);
}
static void cmd_add_core(const char *name, int team, int isAI)
{
    void *pm = player_manager();
    if (!pm) { bot_log("add: no ServerPlayerManager yet (level not loaded?)"); return; }
    if (H->botCount >= MAX_BOTS) { bot_log("add: bot limit %d reached", MAX_BOTS); return; }

    char autoName[32];
    if (!name || !*name) {
        snprintf(autoName, sizeof autoName, "BOT_%02d", H->botCount + 1);
        name = autoName;
    }
    char *player = ((PM_CreatePlayer_t)ADDR_PM_CREATE_PLAYER)(pm, 0, -1, 0);
    if (!player) { bot_log("add: createPlayer returned NULL (server full?)"); return; }
    ((PM_SetPlayerName_t)ADDR_PM_SET_PLAYER_NAME)(pm, player, name);
    strncpy(player + OFF_PLAYER_ONLINE_NAME, name, 31);
    player[OFF_PLAYER_ONLINE_NAME + 31] = 0;
    ((Player_SetAI_t)ADDR_PLAYER_SET_AI)(player, isAI);

    // A connected human's player points at the EntryInput its network input handler fills each tick.
    // Bots get their own; lives for the whole process (the engine never frees it).
    void *input = _aligned_malloc(ENTRYINPUT_SIZE, 16);
    if(!input){bot_log("add: EntryInput allocation failed; retiring incomplete player");chat_rcon("admin.kickPlayer\t%s\tBotMod input allocation failed",name);return;}
    memset(input, 0, ENTRYINPUT_SIZE);
    ((ThisVoid_t)ADDR_ENTRYINPUT_CTOR)(input);
    *(void **)(player + OFF_PLAYER_INPUT) = input;

    // Same notifications a human join sends so the scoreboard / replication systems pick the player up.
    dispatch_player_message(ADDR_MSG_PLAYER_CREATE_CTOR, player);
    ((ThisVoid_t)ADDR_PLAYER_LEVEL_LOADED)(player);
    ((ThisVoid_t)ADDR_PLAYER_INIT_FOR_LEVEL)(player);
    if (team <= 0 || team > mode_teams()) team = H->botCount % mode_teams() + 1;
    char ok = ((AddToTeam_t)ADDR_ADD_TO_TEAM)(player, team, 0, 1);
    if (H->teamBlueprint[(team-1)%2+1]) ((SetPtr_t)ADDR_PLAYER_SET_BLUEPRINT)(player,H->teamBlueprint[(team-1)%2+1]);
    dispatch_player_message(ADDR_MSG_ENTERED_LEVEL_CTOR, player);   // what the server does once a client has loaded the level
    player[OFF_PLAYER_ENTERED_LEVEL] = 1;
    player[0x4a0] = 0;

    int i = H->botCount++;
    H->bots[i] = player; g_createdAt[i]=H->frame;
    H->botInput[i] = input;
    H->botMove[i][0] = H->botMove[i][1] = H->botMove[i][2] = 0;
    bot_log("add: created '%s' player=%p team=%d ai=%d (%s)", name, player, team, isAI, ok ? "ok" : "addToTeam FAILED");
}

// ---- CharacterSpawnEntity::spawn observer: remember the spawn entity + args + blueprint each team's humans use ----
static const unsigned char kCseSpawnPrologue[6] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0}; // push ebp; mov ebp,esp; and esp,-16

// A human deploying from a non-flag spawn point (an HQ the capture points do not reveal, e.g. Wake's US island HQ):
// remembered here, judged a moment later from where the soldier appeared (hq_learn_tick).
typedef struct PendingHq { void *entity; unsigned a2, a3, a4; char *player; unsigned at; } PendingHq;
static PendingHq g_pendingHq;
// The team a spawn point belongs to (CharacterSpawnEntity data +0x08), 0 if not set / unreadable.
// AltBotMod V16: the spawn point's team is CharacterSpawnReferenceObjectData.Team, an int at data+0x6C - read from the
// game's own type layout in the level data (data+0x08..0x0B are IsEventConnectionTarget / IsPropertyConnectionTarget /
// IndexInBlueprint; V1-V15 read data+8 as the team, which is why Rush / TDM spawn points looked team-less).
#define OFF_CSEDATA_TEAM 0x6C
static int spawn_team(void *spawn)
{
    unsigned char *data = spawn && !IsBadReadPtr((char *)spawn + 0x14, 4) ? *(unsigned char **)((char *)spawn + 0x14) : NULL;
    int t = data && !IsBadReadPtr(data + OFF_CSEDATA_TEAM, 4) ? *(int *)(data + OFF_CSEDATA_TEAM) : 0;
    return t >= 1 && t <= 4 ? t : 0;
}
typedef struct HqProbe { char *bot; int team, cand; unsigned at; } HqProbe;
#define MAX_HQ_PROBES 4
static HqProbe g_hqProbes[MAX_HQ_PROBES];

void __cdecl on_cse_spawn(void *self, unsigned *stack)
{
    char *pl = (char *)stack[1];
    if (pl && !is_bot(pl)) {
        mode_observe_spawn(self,pl,stack[2],stack[3],stack[4]);
        g_pendingHq.entity = self; g_pendingHq.a2 = stack[2]; g_pendingHq.a3 = stack[3]; g_pendingHq.a4 = stack[4];
        g_pendingHq.player = pl; g_pendingHq.at = H->frame ? H->frame : 1;
        int team = player_team(pl);
        if ((team == 1 || team == 2) && !H->teamSpawn[team].entity) {   // only until the team's HQ is known: a human may
            H->teamSpawn[team].entity = self;                             // deploy on a flag, a squad mate, or the "wrong" HQ
            H->teamSpawn[team].a2 = stack[2];
            H->teamSpawn[team].a3 = stack[3];
            H->teamSpawn[team].a4 = stack[4];
        }
        if (team == 1 || team == 2) {
            if (*(void **)(pl + OFF_PLAYER_BLUEPRINT)) H->teamBlueprint[team] = *(void **)(pl + OFF_PLAYER_BLUEPRINT);
        }
    }
}

extern void hook_cse_spawn(void);
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _hook_cse_spawn\n"
    "_hook_cse_spawn:\n"
    "  pushad\n"
    "  pushfd\n"
    "  lea eax, [esp + 36]\n"
    "  push eax\n"
    "  push ecx\n"
    "  call _on_cse_spawn\n"
    "  add esp, 8\n"
    "  popfd\n"
    "  popad\n"
    "  push ebp\n"
    "  mov ebp, esp\n"
    "  and esp, 0xfffffff0\n"
    "  push 0x00ddbbd6\n"
    "  ret\n"
    ".att_syntax prefix\n");

// ---- ServerGhostManager::addGhost observer (diagnostics) ----
static const unsigned char kGhostAddPrologue[6] = {0x83, 0xEC, 0x10, 0x53, 0x55, 0x56}; // sub esp,16; push ebx; push ebp; push esi

static const char *ghost_type_name(void *ghost)
{
    // Ghost vtable slot 3 is "push <name>; call ...; add esp,4; ret" (e.g. "PlayerGhost").
    unsigned char *fn = (*(unsigned char ***)ghost)[3];
    return fn[0] == 0x68 ? *(const char **)(fn + 1) : "?";
}

void __cdecl on_ghost_add(unsigned *stack)
{
    if (!H->traceGhosts) return;
    void *ghost = (void *)stack[1];
    bot_log("  addGhost %s ghost=%p arg=%u ret=%08x", ghost ? ghost_type_name(ghost) : "(null)", ghost, stack[2], stack[0]);
}

extern void hook_ghost_add(void);
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _hook_ghost_add\n"
    "_hook_ghost_add:\n"
    "  pushad\n"
    "  pushfd\n"
    "  lea eax, [esp + 36]\n"
    "  push eax\n"
    "  call _on_ghost_add\n"
    "  add esp, 4\n"
    "  popfd\n"
    "  popad\n"
    "  sub esp, 0x10\n"
    "  push ebx\n"
    "  push ebp\n"
    "  push esi\n"
    "  push 0x00d820c6\n"
    "  ret\n"
    ".att_syntax prefix\n");

// ---- Kit mirroring: when a human's unlock/weapon selection arrives, apply the same message to the bots on their team ----
static const unsigned char kSelectUnlocksPrologue[6] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8}; // push ebp; mov ebp,esp; and esp,-8
static const unsigned char kSelectWeaponPrologue[5] = {0x83, 0xEC, 0x24, 0x53, 0x55};       // sub esp,0x24; push ebx; push ebp

// Captured kit per team, so bots can be kitted later without a human present. Lives in host->reserved (survives reloads).
// Message layout used by the handlers: +0x38 asset*, +0x3c / +0x40 fb arrays (element pointer, count at [-4]).
#define KIT_MSG_SIZE 0x80
#define KIT_MAX_WEAPONS 8
typedef struct KitStore {
    unsigned char unlockMsg[KIT_MSG_SIZE];
    int hasUnlocks;
    unsigned char weaponMsg[KIT_MAX_WEAPONS][KIT_MSG_SIZE];
    int nWeapons;
} KitStore;

static KitStore **kit_slots(void) { return (KitStore **)H->reserved; }   // [0] unused, [1] team 1, [2] team 2
static unsigned long long *kitted_mask(void) { return (unsigned long long *)(H->reserved + 2560); } // bit i = bot i has received a kit
static float *bot_aim(int i) { return (float *)(H->reserved + 1024) + i * 2; }  // yaw, pitch (radians) per bot (+1024..+1536)
static int *aim_mode(void) { return (int *)(H->reserved + 20); }               // 0 = leave aim fields alone, 1 = write them

static KitStore *team_kit(int team, int create)
{
    KitStore **slots = kit_slots();
    if (team != 1 && team != 2) return NULL;
    if (!slots[team] && create) slots[team] = calloc(1, sizeof(KitStore));
    return slots[team];
}

static void *copy_fb_array(void *elems)
{
    if (!elems || IsBadReadPtr((unsigned *)elems - 1, 4)) return NULL;
    unsigned n = ((unsigned *)elems)[-1];
    if (n > 256 || IsBadReadPtr(elems, n * sizeof(unsigned))) return NULL;   // not an fb array / garbage
    unsigned *copy = malloc((n + 1) * sizeof(unsigned));   // intentionally never freed: referenced by stored kits
    if (!copy) return NULL;
    copy[0] = n;
    memcpy(copy + 1, elems, n * sizeof(unsigned));
    return copy + 1;
}

// Name of a DataContainer asset (char* at +0x08), "?" if the pointer is not a readable asset.
static const char *asset_name(void *asset)
{
    if (!asset || (unsigned)asset < 0x10000 || IsBadReadPtr(asset, 12)) return "?";
    const char *n = *(const char **)((char *)asset + 8);
    return n && (unsigned)n >= 0x10000 && !IsBadStringPtrA(n, 128) ? n : "?";
}

// Unlock messages carry fb arrays at +0x3c and +0x40; weapon messages carry a plain asset pointer at +0x3c and an
// array at +0x40. Arrays are deep-copied (the message frees its own after dispatch).
static void capture_msg(unsigned char *dst, const unsigned char *msg, int weapon)
{
    memcpy(dst, msg, KIT_MSG_SIZE);
    if (!weapon) *(void **)(dst + 0x3c) = copy_fb_array(*(void **)(msg + 0x3c));
    *(void **)(dst + 0x40) = copy_fb_array(*(void **)(msg + 0x40));
}

static unsigned fb_count(void *arr)
{
    if (!arr || IsBadReadPtr((unsigned *)arr - 1, 4)) return 0;
    unsigned n = ((unsigned *)arr)[-1];
    return n > 256 ? 0 : n;
}

// ---- Loadout library: one captured loadout per team + kit (class), persisted to BotMod_loadouts.txt ----
// Unlock message: +0x38 kit asset (e.g. Gameplay/Kits/USSupport), +0x3c appearance array, +0x40 specialization array.
// Weapon message:  +0x38 slot index, +0x3c weapon unlock asset, +0x40 attachment array.
#define MAX_LOADOUTS 16
typedef struct Loadout {
    int team;
    char kit[64];
    unsigned char unlockMsg[KIT_MSG_SIZE];
    int nWeapons;
    unsigned char weaponMsg[KIT_MAX_WEAPONS][KIT_MSG_SIZE];
} Loadout;
typedef struct LoadoutLib { int count; Loadout l[MAX_LOADOUTS]; } LoadoutLib;
static LoadoutLib *loadout_lib(void)
{
    LoadoutLib **slot = (LoadoutLib **)(H->reserved + 44);
    if (!*slot) *slot = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(LoadoutLib));
    return *slot;
}
static int g_botLoadout[MAX_BOTS] = {-1};   // library index used by each bot (-1 = none yet)

static const char *kit_class(const char *kit)
{
    const char *k = strrchr(kit, '/');
    k = k ? k + 1 : kit;
    return (!strncmp(k, "US", 2) || !strncmp(k, "RU", 2)) ? k + 2 : k;   // USSupport -> Support
}

static void log_loadout(const Loadout *L, const char *from)
{
    const unsigned char *u = L->unlockMsg;
    void **app = *(void ***)(u + 0x3c), **spec = *(void ***)(u + 0x40);
    bot_log("loadout: team %d %s (%s)%s%s", L->team, kit_class(L->kit), L->kit, from ? " from " : "", from ? from : "");
    for (unsigned k = 0; k < fb_count(app); k++) bot_log("    appearance: %s", asset_name(app[k]));
    for (unsigned k = 0; k < fb_count(spec); k++) bot_log("    specialization: %s", asset_name(spec[k]));
    for (int w = 0; w < L->nWeapons; w++) {
        const unsigned char *m = L->weaponMsg[w];
        char att[512] = "";
        void **a = *(void ***)(m + 0x40);
        for (unsigned k = 0; k < fb_count(a) && strlen(att) < 400; k++) {
            const char *n = asset_name(a[k]), *s = strrchr(n, '/');
            strcat(att, k ? ", " : "");
            strcat(att, s ? s + 1 : n);
        }
        bot_log("    slot %d: %s  [%s]", *(int *)(m + 0x38), asset_name(*(void **)(m + 0x3c)), att);
    }
}

// Text format, one message per line:  U|W <hex 0x80 bytes> ; then pointer checks  P <off> <ptr> <name>  and arrays
// A <off> <n> <ptr>:<name> ...   On load each pointer is verified by its asset name (assets load at the same addresses).
static void save_loadouts(void)
{
    char dir[MAX_PATH], path[MAX_PATH + 32];
    GetModuleFileNameA(NULL, dir, sizeof dir);
    *strrchr(dir, '\\') = 0;
    snprintf(path, sizeof path, "%s\\BotMod_loadouts.txt", dir);
    FILE *f = fopen(path, "w");
    if (!f) { bot_log("loadouts: cannot write %s", path); return; }
    LoadoutLib *lib = loadout_lib();
    for (int i = 0; i < lib->count; i++) {
        Loadout *L = &lib->l[i];
        fprintf(f, "LOADOUT %d %s %d\n", L->team, L->kit, L->nWeapons);
        for (int m = -1; m < L->nWeapons; m++) {
            const unsigned char *msg = m < 0 ? L->unlockMsg : L->weaponMsg[m];
            fprintf(f, "%c ", m < 0 ? 'U' : 'W');
            for (int k = 0; k < KIT_MSG_SIZE; k++) fprintf(f, "%02x", msg[k]);
            fprintf(f, "\n");
            if (m < 0) fprintf(f, "P 56 %p %s\n", *(void **)(msg + 0x38), asset_name(*(void **)(msg + 0x38)));
            else fprintf(f, "P 60 %p %s\n", *(void **)(msg + 0x3c), asset_name(*(void **)(msg + 0x3c)));
            for (int off = (m < 0 ? 0x3c : 0x40); off <= 0x40; off += 4) {
                void **a = *(void ***)(msg + off);
                unsigned n = fb_count(a);
                fprintf(f, "A %d %u", off, n);
                for (unsigned k = 0; k < n; k++) fprintf(f, " %p:%s", a[k], asset_name(a[k]));
                fprintf(f, "\n");
            }
        }
    }
    fclose(f);
    bot_log("loadouts: saved %d to %s", lib->count, path);
}

// ---- Asset lookup by name (asset addresses change between server runs and levels) ----
// Pass 1 finds each name string in memory; pass 2 finds objects whose +0x08 points at one of those strings.
#define MAX_FIXUPS 1024
typedef struct Fixup { void **slot; char name[160]; int loadout; unsigned vtable;   // vtable 0 = any engine object
    char orig[160]; unsigned char *wmsg; int off; unsigned *arr; } Fixup;   // AltBotMod V14: what to fall back to when the level lacks it (see load_loadouts)

static void resolve_assets(Fixup *fx, int n)
{
    enum { CHUNK = 1 << 16 };
    static unsigned char buf[CHUNK + 256];
    // every copy of each name string (the asset points at one specific copy)
    #define MAX_COPIES 256
    static const char *strAddr[MAX_FIXUPS][MAX_COPIES];
    static int nCopies[MAX_FIXUPS];
    memset(nCopies, 0, sizeof nCopies);
    MEMORY_BASIC_INFORMATION mbi;
    for (int pass = 0; pass < 2; pass++) {
        for (unsigned char *a = (unsigned char *)0x10000; (unsigned)a < 0xffff0000u && !g_abortScans; a = (unsigned char *)mbi.BaseAddress + mbi.RegionSize) {
            if (!VirtualQuery(a, &mbi, sizeof mbi) || !mbi.RegionSize) break;
            DWORD prot = mbi.Protect & 0xff;
            if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOCACHE)) ||
                !(prot == PAGE_READWRITE || prot == PAGE_READONLY || prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_READ)) continue;
            for (SIZE_T off = 0; off < mbi.RegionSize && !g_abortScans; off += CHUNK) {
                SIZE_T want = mbi.RegionSize - off < CHUNK ? mbi.RegionSize - off : CHUNK, got = 0;
                char *base = (char *)mbi.BaseAddress + off;
                if (!ReadProcessMemory(GetCurrentProcess(), base, buf, want, &got) || !got) continue;
                if (pass == 0) {
                    for (int f = 0; f < n; f++) {
                        size_t len = strlen(fx[f].name) + 1;
                        for (unsigned char *p = buf; p + len <= buf + got && nCopies[f] < MAX_COPIES; p++) {
                            p = memchr(p, fx[f].name[0], buf + got - p);
                            if (!p || p + len > buf + got) break;
                            if (!memcmp(p, fx[f].name, len)) strAddr[f][nCopies[f]++] = base + (p - buf);
                        }
                    }
                } else {
                    for (SIZE_T k = 8; k + 4 <= got; k += 4) {
                        const char *v = *(const char **)(buf + k);
                        if (!v) continue;
                        for (int f = 0; f < n; f++) {
                            if (*fx[f].slot != (void *)-1) continue;
                            int hit = 0;
                            for (int cpy = 0; cpy < nCopies[f] && !hit; cpy++) hit = strAddr[f][cpy] == v;
                            if (!hit) continue;
                            // must be a real engine object: vtable (first field) inside the exe image (asset vtables sit ~0x025xxxxx)
                            unsigned vt = *(unsigned *)(buf + k - 8);
                            if (k < 8 || vt < 0x02028000 || vt >= 0x02b00000) continue;
                            if (fx[f].vtable && vt != fx[f].vtable) continue;
                            *fx[f].slot = base + k - 8;
                        }
                    }
                }
            }
        }
    }
}

// ---- Asset type cache: the addresses change every load, the types do not. BotMod_assetvt.txt keeps "vtable name"
// for every asset found once; later loads find them in one pass over memory looking only for those few types and
// checking the name at +0x08 (well under a second, instead of the ~80 s two-pass name search). ----
static void asset_cache_path(char *out, int n)
{
    char dir[MAX_PATH];
    GetModuleFileNameA(NULL, dir, sizeof dir);
    *strrchr(dir, '\\') = 0;
    snprintf(out, n, "%s\\BotMod_assetvt.txt", dir);
}
#define MAX_ASSET_CACHE 2048
static struct { unsigned vt; char name[160]; } g_assetCache[MAX_ASSET_CACHE];
static int g_assetCacheN;
static void asset_cache_load(void)
{
    char path[MAX_PATH + 32], line[256];
    asset_cache_path(path, sizeof path);
    g_assetCacheN = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (g_assetCacheN < MAX_ASSET_CACHE && fgets(line, sizeof line, f)) {
        unsigned vt; char name[160];
        if (sscanf(line, "%x %159s", &vt, name) == 2) { g_assetCache[g_assetCacheN].vt = vt; strcpy(g_assetCache[g_assetCacheN].name, name); g_assetCacheN++; }
    }
    fclose(f);
}
static unsigned asset_cache_vt(const char *name)
{
    for (int k = 0; k < g_assetCacheN; k++) if (!strcmp(g_assetCache[k].name, name)) return g_assetCache[k].vt;
    return 0;
}
static void asset_cache_save(Fixup *fx, int n)
{
    int added = 0;
    for (int f = 0; f < n; f++) {
        void *a = *fx[f].slot;
        if (a == (void *)-1 || !a || IsBadReadPtr(a, 4) || asset_cache_vt(fx[f].name) || g_assetCacheN >= MAX_ASSET_CACHE) continue;
        g_assetCache[g_assetCacheN].vt = *(unsigned *)a;
        strcpy(g_assetCache[g_assetCacheN].name, fx[f].name);
        g_assetCacheN++; added++;
    }
    if (!added) return;
    char path[MAX_PATH + 32];
    asset_cache_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int k = 0; k < g_assetCacheN; k++) fprintf(f, "%08x %s\n", g_assetCache[k].vt, g_assetCache[k].name);
    fclose(f);
}
// One pass: words equal to a cached type, whose +0x08 name matches a wanted asset of that type.
static int fast_resolve_assets(Fixup *fx, int n)
{
    unsigned vts[64];
    int nvt = 0, found = 0;
    unsigned fvt[MAX_FIXUPS];
    for (int f = 0; f < n; f++) {
        fvt[f] = fx[f].vtable ? fx[f].vtable : asset_cache_vt(fx[f].name);
        if (!fvt[f]) continue;
        int have = 0;
        for (int k = 0; k < nvt; k++) if (vts[k] == fvt[f]) have = 1;
        if (!have && nvt < 64) vts[nvt++] = fvt[f];
    }
    if (!nvt) return 0;
    static int dupes[MAX_FIXUPS];
    memset(dupes, 0, sizeof dupes);
    enum { CHUNK = 1 << 16 };
    static unsigned buf[CHUNK / 4];
    MEMORY_BASIC_INFORMATION mbi;
    for (unsigned char *a = (unsigned char *)0x10000; (unsigned)a < 0xffff0000u && !g_abortScans; a = (unsigned char *)mbi.BaseAddress + mbi.RegionSize) {
        if (!VirtualQuery(a, &mbi, sizeof mbi) || !mbi.RegionSize) break;
        DWORD prot = mbi.Protect & 0xff;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOCACHE)) ||
            !(prot == PAGE_READWRITE || prot == PAGE_READONLY || prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_READ)) continue;
        for (SIZE_T off = 0; off < mbi.RegionSize; off += CHUNK) {
            SIZE_T want = mbi.RegionSize - off < CHUNK ? mbi.RegionSize - off : CHUNK, got = 0;
            char *base = (char *)mbi.BaseAddress + off;
            if (base + want > (char *)buf && base < (char *)buf + sizeof buf) continue;
            if (!ReadProcessMemory(GetCurrentProcess(), base, buf, want, &got) || !got) continue;
            for (SIZE_T w = 0; w < got / 4; w++) {
                unsigned v = buf[w];
                if (v < 0x02028000u || v >= 0x02b00000u) continue;
                int k;
                for (k = 0; k < nvt && vts[k] != v; k++) ;
                if (k == nvt) continue;
                char *obj = base + w * 4;
                const char *nm = asset_name(obj);
                if (nm[0] == '?') continue;
                for (int f = 0; f < n; f++) {
                    if (fvt[f] != v || strcmp(nm, fx[f].name)) continue;
                    if (*fx[f].slot == (void *)-1) { *fx[f].slot = obj; found++; dupes[f] = 1; }
                    else if (*fx[f].slot != obj) {   // a second object by that name and type: log both (stale copy of another level?)
                        unsigned *o1 = (unsigned *)*fx[f].slot, *o2 = (unsigned *)obj;
                        // the live one is the one the level holds references to (+4 low bits: reference count - a freed
                        // copy left by the previous map shows 1); the stale one crashed the server when a bot used it
                        int keepNew = (o2[1] & 0xffffff) > (o1[1] & 0xffffff);
                        if (dupes[f]++ < 3)
                            bot_log("loadouts: %s twice: %p [refs %u] and %p [refs %u] - using %p", fx[f].name, o1, o1[1] & 0xffffff, o2,
                                    o2[1] & 0xffffff, keepNew ? o2 : o1);
                        if (keepNew) *fx[f].slot = obj;
                    }
                }
            }
        }
    }
    return found;
}

#define ADDR_WEAPON_UNLOCK_VTABLE  0x02504b34  // the weapon unlock assets a client selects (every kit item)
// AltBotMod V14: an item the level does not have. V13 dropped the whole kit for one missing item - on Markaz Monolith
// (the first Aftermath map played) all 8 kits went, and bots spawned with no kit at all: invisible. Now, item by item:
//  - a BotMod_kitextra.txt item or perk the level lacks: the kit's own item / perk again;
//  - an attachment / unlock-list entry: left out of its list;
//  - a weapon or gadget outside the primary slot: that slot left empty (the kit keeps the rest);
//  - the primary weapon, the appearance or anything else: only then is the kit dropped.
static int g_kitsAllDropped;
static void loadouts_missing(LoadoutLib *lib, Fixup *fx, int nfx, int *bad)
{
    static Fixup retry[MAX_FIXUPS]; int nr = 0, logged = 0;
    static unsigned char dropW[MAX_LOADOUTS][KIT_MAX_WEAPONS];
    memset(dropW, 0, sizeof dropW);
    for (int k = 0; k < nfx; k++) {
        if (*fx[k].slot != (void *)-1) continue;
        if (logged++ < 24) bot_log("loadouts: not on this level: %s (team %d %s)%s", fx[k].name, fx[k].loadout < lib->count ? lib->l[fx[k].loadout].team : 0,
                                   fx[k].loadout < lib->count ? kit_class(lib->l[fx[k].loadout].kit) : "?", fx[k].orig[0] ? " - the kit's own item instead" : "");
        if (fx[k].orig[0] && nr < MAX_FIXUPS) { retry[nr] = fx[k]; snprintf(retry[nr].name, sizeof retry[nr].name, "%s", fx[k].orig); retry[nr].orig[0] = 0; nr++; }
    }
    if (nr) { int fast = fast_resolve_assets(retry, nr); int rest = 0; for (int k = 0; k < nr; k++) if (*retry[k].slot == (void *)-1) rest++; if (rest) resolve_assets(retry, nr); (void)fast; }
    int skipped = 0, emptied = 0;
    for (int k = 0; k < nfx; k++) {
        if (*fx[k].slot != (void *)-1) continue;
        int L = fx[k].loadout; if (L < 0 || L >= lib->count) continue;
        if (fx[k].arr) { *fx[k].slot = NULL; skipped++; continue; }   // compacted below
        if (fx[k].wmsg && fx[k].off == 0x3c && *(int *)(fx[k].wmsg + 0x38) != 0) {
            for (int w = 0; w < lib->l[L].nWeapons; w++) if (lib->l[L].weaponMsg[w] == fx[k].wmsg) dropW[L][w] = 1;
            emptied++; continue;
        }
        bad[L] = 1;
    }
    // lists without the missing entries (count in [0], entries after it; the engine reads the count at [-1] of the list)
    for (int k = 0; k < nfx; k++) {
        unsigned *a = fx[k].arr;
        if (!a) continue;
        unsigned n = a[0], j = 0;
        int any = 0; for (unsigned e = 1; e <= n; e++) any |= a[e] == 0;
        if (!any) continue;
        for (unsigned e = 1; e <= n; e++) if (a[e]) a[++j] = a[e];
        a[0] = j;
    }
    for (int L = 0; L < lib->count; L++) {
        Loadout *X = &lib->l[L]; int w2 = 0;
        for (int w = 0; w < X->nWeapons; w++) { if (dropW[L][w]) continue; if (w2 != w) memcpy(X->weaponMsg[w2], X->weaponMsg[w], KIT_MSG_SIZE); w2++; }
        X->nWeapons = w2;
    }
    if (skipped || emptied) bot_log("loadouts: %d list entr(ies) and %d gadget / sidearm slot(s) left out where this level lacks them; the kits stay", skipped, emptied);
}
// AltBotMod V14: the Aftermath levels (XP4_FD Markaz Monolith, XP4_Parl Azadi Palace, XP4_Quake Epicenter, XP4_Rubble
// Talah Market) carry their own kits and soldier looks - Gameplay/Kits/USAssault_XP4, ..._Appearance01_XP4 - and none of the
// stock ones the captured kits name (read from the server's own level data: those 16 are the only kit items any of the 29
// levels lacks). A stock kit or look found there anyway is the previous level's freed copy. So on those levels each kit
// asks for the level's own kit and look instead (Gun Master and Scavenger have their own kit sets).
static int level_kit_swap(const char *level, const char *mode, Fixup *fx, int nfx)
{
    if (_strnicmp(level, "XP4_", 4)) return 0;
    const char *kitSuffix = mode && strstr(mode, "GunMaster") ? "_GM_XP4" : mode && strstr(mode, "Scavenger") ? "_XP4_SCV" : "_XP4";
    static const char *const teams[2] = {"US", "RU"};
    static const char *const kits[4] = {"Assault", "Engineer", "Support", "Recon"}, *const looks[4] = {"Assault", "Engi", "Support", "Recon"};
    int swapped = 0;
    for (int k = 0; k < nfx; k++) {
        char *n = fx[k].name, want[160] = "";
        for (int t = 0; t < 2 && !want[0]; t++) for (int c = 0; c < 4 && !want[0]; c++) {
            char stock[96];
            snprintf(stock, sizeof stock, "Gameplay/Kits/%s%s", teams[t], kits[c]);
            if (!_stricmp(n, stock)) { snprintf(want, sizeof want, "%s%s", stock, kitSuffix); break; }
            // any look of that team and class (Appearance01, 02, ...) -> the level's Appearance01_XP4 (US ones sit under "Us")
            snprintf(stock, sizeof stock, "Persistence/Unlocks/Soldiers/Visual/MP/%s/MP_%s_%s_Appearance", teams[t], teams[t], looks[c]);
            size_t len = strlen(stock);
            if (!_strnicmp(n, stock, len) && !strstr(n + len, "_XP4") && !strstr(n + len, "_xp4"))
                snprintf(want, sizeof want, "Persistence/Unlocks/Soldiers/Visual/MP/%s/MP_%s_%s_Appearance01_XP4", t ? "RU" : "Us", teams[t], looks[c]);
        }
        if (!want[0]) continue;
        snprintf(fx[k].name, sizeof fx[k].name, "%s", want);
        fx[k].orig[0] = 0;   // no falling back to the stock one: on these levels only a stale copy of it could be found
        fx[k].vtable = 0;
        swapped++;
    }
    return swapped;
}
static char g_jobLevel[64], g_jobMode[64];   // (defined with the level discovery job below)
#include "weapon_variety.inc"
#include "gadget_variety.inc"   // AltBotMod V16
static void load_loadouts(void)
{
    char dir[MAX_PATH], path[MAX_PATH + 32], line[8192];
    GetModuleFileNameA(NULL, dir, sizeof dir);
    *strrchr(dir, '\\') = 0;
    snprintf(path, sizeof path, "%s\\BotMod_loadouts.txt", dir);
    g_kitsAllDropped = 0;   // AltBotMod V14: set again below once this level's kits are checked
    FILE *f = fopen(path, "r");
    if (!f) { bot_log("loadouts: no %s yet", path); return; }
    static Fixup fx[MAX_FIXUPS];
    int nfx = 0, bad[MAX_LOADOUTS] = {0};
    LoadoutLib *lib = loadout_lib();
    lib->count = 0;
    Loadout *L = NULL;
    unsigned char *msg = NULL;
    // records a pointer slot: kept if it still names the right asset, otherwise marked for lookup by name
    // vt: the type the slot must hold (0 = any). A weapon slot must be a weapon unlock: on MP_Subway the ammo box's name
    // also belongs to another object (vt 0x0287f330), and handing that to a bot crashed the server.
    #define FIXUP_T(slotp, nm, vt) do { void **s_ = (void **)(slotp); \
        if (!strcmp((nm), "?")) break; \
        /* never kept because the old address still names the asset: after a map change that is often the previous            level's freed copy (Bazaar -> Seine handed bots freed assets and corrupted memory) - every load looks all up */ \
        if (nfx < MAX_FIXUPS) { fx[nfx].slot = s_; strncpy(fx[nfx].name, (nm), sizeof fx[nfx].name - 1); fx[nfx].loadout = lib->count; fx[nfx].vtable = (vt); nfx++; *s_ = (void *)-1; } \
        else bad[lib->count] = 1; } while (0)
    #define FIXUP(slotp, nm) FIXUP_T(slotp, nm, 0)
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "LOADOUT ", 8)) {
            if (L) lib->count++;
            L = lib->count < MAX_LOADOUTS ? &lib->l[lib->count] : NULL;
            if (!L) break;
            memset(L, 0, sizeof *L);
            int nw = 0;
            sscanf(line + 8, "%d %63s %d", &L->team, L->kit, &nw);
            msg = NULL;
        } else if (L && (line[0] == 'U' || line[0] == 'W') && line[1] == ' ') {
            msg = line[0] == 'U' ? L->unlockMsg : (L->nWeapons < KIT_MAX_WEAPONS ? L->weaponMsg[L->nWeapons++] : NULL);
            if (!msg) continue;
            for (int k = 0; k < KIT_MSG_SIZE; k++) { unsigned v; sscanf(line + 2 + 2 * k, "%2x", &v); msg[k] = (unsigned char)v; }
        } else if (L && msg && line[0] == 'P') {
            int off; void *ptr; char name[256];
            if (sscanf(line + 2, "%d %p %255s", &off, &ptr, name) == 3) {
                *(void **)(msg + off) = ptr;
                int weaponAsset = msg != L->unlockMsg && off == 0x3c;
                int before = nfx;
                FIXUP_T(msg + off, name, weaponAsset ? ADDR_WEAPON_UNLOCK_VTABLE : 0);
                if (nfx > before) { fx[nfx - 1].orig[0] = 0; fx[nfx - 1].wmsg = msg != L->unlockMsg ? msg : NULL; fx[nfx - 1].off = off; fx[nfx - 1].arr = NULL; }
            }
        } else if (L && msg && line[0] == 'A') {
            int off; unsigned n; int pos = 0;
            if (sscanf(line + 2, "%d %u%n", &off, &n, &pos) < 2 || n > 256) { bad[lib->count] = 1; continue; }
            unsigned *arr = malloc((n + 1) * sizeof(unsigned));   // kept for the process lifetime
            arr[0] = n;
            char *p = line + 2 + pos;
            for (unsigned k = 0; k < n; k++) {
                void *ptr; char name[256]; int used = 0;
                if (sscanf(p, " %p:%255s%n", &ptr, name, &used) < 2) { bad[lib->count] = 1; break; }
                p += used;
                arr[k + 1] = (unsigned)ptr;
                int before = nfx;
                FIXUP(&arr[k + 1], name);
                if (nfx > before) { fx[nfx - 1].orig[0] = 0; fx[nfx - 1].wmsg = NULL; fx[nfx - 1].off = -1; fx[nfx - 1].arr = arr; }
            }
            *(void **)(msg + off) = (void *)(arr + 1);   // empty lists must still point at a count of 0 (engine reads [-1])
        }
    }
    if (L) lib->count++;
    fclose(f);
    {   // BotMod_kitextra.txt "<class> <slot> <asset>": that item in that slot for every kit of the class (the slot's own
        // item replaced, its attachments dropped) - e.g. "Engineer 1 Weapons/Gadgets/M15/U_M15": AT mines in the pistol slot
        char epath[MAX_PATH + 32];
        snprintf(epath, sizeof epath, "%s\\BotMod_kitextra.txt", dir);
        FILE *fe = fopen(epath, "r");
        char el[512];
        while (fe && fgets(el, sizeof el, fe)) {
            char cls[32], asset[256], kind[16];
            int slot;
            if (el[0] == '#') continue;
            if (sscanf(el, "%31s %15s %255s", cls, kind, asset) == 3 && !_stricmp(kind, "perk")) {   // the specialization
                int changed = 0;
                for (int q = 0; q < nfx; q++) {
                    int k = fx[q].loadout;
                    if (k < 0 || k >= lib->count || (strcmp(cls, "*") && _stricmp(kit_class(lib->l[k].kit), cls))) continue;
                    if (!strstr(fx[q].name, "Specialization")) continue;   // (NoSpecialization or another perk)
                    if (!fx[q].orig[0]) snprintf(fx[q].orig, sizeof fx[q].orig, "%s", fx[q].name);   // V14: the kit's own perk, if the level lacks this one
                    snprintf(fx[q].name, sizeof fx[q].name, "%s", asset);
                    fx[q].vtable = 0;
                    changed++;
                }
                bot_log("loadouts: %s kits get the perk %s (%d kit(s), BotMod_kitextra.txt)", cls, asset, changed);
                continue;
            }
            if (sscanf(el, "%31s %d %255s", cls, &slot, asset) != 3 || slot < 0 || slot > 9) continue;
            for (int k = 0; k < lib->count; k++) {
                Loadout *X = &lib->l[k];
                if (_stricmp(kit_class(X->kit), cls)) continue;
                unsigned char *m = NULL;
                for (int w = 0; w < X->nWeapons && !m; w++) if (*(int *)(X->weaponMsg[w] + 0x38) == slot) m = X->weaponMsg[w];
                if (!m && X->nWeapons > 0 && X->nWeapons < KIT_MAX_WEAPONS) {   // a slot the kit leaves empty: a copy of its first entry
                    m = X->weaponMsg[X->nWeapons++];
                    memcpy(m, X->weaponMsg[0], KIT_MSG_SIZE);
                    *(int *)(m + 0x38) = slot;
                }
                if (!m) continue;
                unsigned *none = malloc(2 * sizeof(unsigned));   // no attachments (kept for the process lifetime)
                none[0] = 0;
                *(void **)(m + 0x40) = (void *)(none + 1);
                char own[160] = "";
                for (int q = 0; q < nfx; q++)   // the slot's own item is no longer wanted there (no lookup for it)
                    if (fx[q].slot == (void **)(m + 0x3c)) { snprintf(own, sizeof own, "%s", fx[q].name); fx[q] = fx[--nfx]; q--; }
                int save = lib->count;   // (FIXUP_T files the fixup under lib->count)
                lib->count = k;
                int before = nfx;
                FIXUP_T(m + 0x3c, asset, ADDR_WEAPON_UNLOCK_VTABLE);
                if (nfx > before) { snprintf(fx[nfx - 1].orig, sizeof fx[nfx - 1].orig, "%s", own); fx[nfx - 1].wmsg = m; fx[nfx - 1].off = 0x3c; fx[nfx - 1].arr = NULL; }
                lib->count = save;
                bot_log("loadouts: team %d %s gets %s in slot %d (BotMod_kitextra.txt)", X->team, kit_class(X->kit), asset, slot);
            }
        }
        if (fe) fclose(fe);
    }
    #undef FIXUP
    #undef FIXUP_T
    {   int sw = level_kit_swap(g_jobLevel, g_jobMode, fx, nfx);
        if (sw) bot_log("loadouts: %s has its own kits and soldier looks - %d kit / look item(s) use the level's own (%s)", g_jobLevel, sw,
                        strstr(g_jobMode, "GunMaster") ? "Gun Master set" : strstr(g_jobMode, "Scavenger") ? "Scavenger set" : "_XP4 set");
    }
    if (nfx) {
        DWORD t0 = GetTickCount();
        asset_cache_load();
        int fast = fast_resolve_assets(fx, nfx);
        static Fixup rest[MAX_FIXUPS];   // what the cache did not cover: the slow name search
        int nrest = 0;
        for (int k = 0; k < nfx; k++) if (*fx[k].slot == (void *)-1) rest[nrest++] = fx[k];
        if (nrest) resolve_assets(rest, nrest);
        asset_cache_save(fx, nfx);
        bot_log("loadouts: %d asset(s) from the type cache, %d by the name search", fast, nrest);
        int unresolved = 0;
        for (int k = 0; k < nfx; k++) if (*fx[k].slot == (void *)-1) unresolved++;
        bot_log("loadouts: looked up %d moved asset(s) by name in %lu ms, %d not found", nfx, GetTickCount() - t0, unresolved);
        if (unresolved) loadouts_missing(lib, fx, nfx, bad);   // AltBotMod V14: fall back item by item, not kit by kit
    }
    // drop loadouts with unresolved assets (not loaded on this level)
    int w = 0;
    for (int i = 0; i < lib->count; i++) {
        if (bad[i]) { bot_log("loadouts: skipping team %d %s (assets not loaded on this level)", lib->l[i].team, kit_class(lib->l[i].kit)); continue; }
        if (w != i) lib->l[w] = lib->l[i];
        w++;
    }
    g_kitsAllDropped = w == 0 && lib->count > 0;   // AltBotMod V14: kits exist, this level has none of them usable
    lib->count = w;
    weapon_variety_load();
    gadget_variety_load();   // AltBotMod V16
    bot_log("loadouts: %d usable from %s", lib->count, path);
    for (int i = 0; i < lib->count; i++) bot_log("  #%d team %d %s: %d weapon slot(s)", i, lib->l[i].team, kit_class(lib->l[i].kit), lib->l[i].nWeapons);
}
// Per-human batch: a deploy sends the weapon selections, then the unlocks.
#define MAX_PENDING 16
typedef struct PendingKit { void *player; unsigned lastFrame; int n; unsigned char weapons[KIT_MAX_WEAPONS][KIT_MSG_SIZE]; } PendingKit;
static PendingKit g_pending[MAX_PENDING];

static PendingKit *pending_for(void *player)
{
    PendingKit *freeSlot = NULL;
    for (int i = 0; i < MAX_PENDING; i++) {
        if (g_pending[i].player == player) return &g_pending[i];
        if (!g_pending[i].player && !freeSlot) freeSlot = &g_pending[i];
    }
    if (!freeSlot) freeSlot = &g_pending[0];
    memset(freeSlot, 0, sizeof *freeSlot);
    freeSlot->player = player;
    return freeSlot;
}

// Items bots must not carry on a given map (a kit item that crashes the server there): BotMod\kitskip.txt, one rule
// per line "<map code or *> <part of the item's asset name>", e.g. "MP_Subway Claymore". Re-read on every level.
#define MAX_KITSKIP 32
static char g_kitSkipMap[MAX_KITSKIP][32], g_kitSkipItem[MAX_KITSKIP][64];
static int g_kitSkipCount;
static void botmod_file(char *out, int n, const char *name);
static void load_kitskip(void)
{
    char path[MAX_PATH + 64], line[160];
    botmod_file(path, sizeof path, "kitskip.txt");
    g_kitSkipCount = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f) && g_kitSkipCount < MAX_KITSKIP) {
        char m[32], it[64];
        if (line[0] == '#' || sscanf(line, "%31s %63s", m, it) != 2) continue;
        strcpy(g_kitSkipMap[g_kitSkipCount], m); strcpy(g_kitSkipItem[g_kitSkipCount], it); g_kitSkipCount++;
    }
    fclose(f);
}
static const char *current_level(void);
static int kit_item_skipped(const char *asset)
{
    const char *lvl = current_level();
    for (int k = 0; k < g_kitSkipCount; k++)
        if ((!strcmp(g_kitSkipMap[k], "*") || (lvl && !strcmp(g_kitSkipMap[k], lvl))) && strstr(asset, g_kitSkipItem[k])) return 1;
    return 0;
}

static int apply_loadout(char *bot, int idx)
{
    LoadoutLib *lib = loadout_lib();
    if (idx < 0 || idx >= lib->count) return 0;
    Loadout *L = &lib->l[idx];
    unsigned char msg[KIT_MSG_SIZE] __attribute__((aligned(16)));
    int variant = weapon_variant_pick(bot, kit_class(L->kit));
    if (variant >= 0 && kit_item_skipped(g_weaponDefinitions[variant].weapon)) variant = -1;
    int gbi = bot_index(bot);   // AltBotMod V16: this life's gadgets
    if (gbi >= 0) { memset(g_botGadget[gbi], 0, sizeof g_botGadget[gbi]); g_gadgetLife[gbi]++; }
    for (int w = 0; w < L->nWeapons; w++) {   // same order a client sends them: weapons, then unlocks
        const char *asset = asset_name(*(void **)(L->weaponMsg[w] + 0x3c));
        if (kit_item_skipped(asset)) {
            static unsigned logged;
            if (logged++ < 20) bot_log("kit: leaving %s out of %s's kit on this map (kitskip.txt)", asset, bot + OFF_PLAYER_ONLINE_NAME);
            continue;
        }
        memcpy(msg, L->weaponMsg[w], KIT_MSG_SIZE);
        if (*(int *)(msg + 0x38) == 0) weapon_variant_message(msg, variant);
        {   int gs = *(int *)(msg + 0x38);   // V16: a gadget slot takes this life's class option
            void *ga = gs >= 2 && gs <= 5 ? gadget_variant_pick(bot, kit_class(L->kit), gs, L->team) : NULL;
            if (ga) { memcpy(msg + 0x3c, &ga, sizeof ga); if (gbi >= 0) g_botGadget[gbi][gs] = ga; }
        }
        ((PlayerMsg_t)ADDR_PLAYER_SELECT_WEAPON)(bot, msg);
        if (*(int *)(msg + 0x38) == 0) {
            int bi=bot_index(bot);
            if(bi>=0)g_primaryDoctrine[bi]=bm_weapon_profile(asset_name(*(void **)(msg+0x3c)),
                !strcmp(kit_class(L->kit),"Recon"),0);
        }
    }
    memcpy(msg, L->unlockMsg, KIT_MSG_SIZE);
    ((PlayerMsg_t)ADDR_PLAYER_SELECT_UNLOCKS)(bot, msg);
    if (variant >= 0) bot_log("kit: %s primary %s (%s)", bot + OFF_PLAYER_ONLINE_NAME,
        g_weaponDefinitions[variant].label,
        g_enhanced.weaponAttachments && g_weaponVariants[variant].attached ? "attachments" : "stock accessories");
    return 1;
}

// Class a bot plays, from its name ("[BOT] Medic US" -> Assault); NULL = any.
static const char *bot_role_class(const char *name)
{
    if (strstr(name, "Medic") || strstr(name, "Assault")) return "Assault";
    if (strstr(name, "Engineer")) return "Engineer";
    if (strstr(name, "Support")) return "Support";
    if (strstr(name, "Recon")) return "Recon";
    return NULL;
}

// Loadout for the bot: its role's class for its team if captured, otherwise cycle through the team's loadouts (-1 = none).
static int pick_loadout(char *bot)
{
    LoadoutLib *lib = loadout_lib();
    int team = ((player_team(bot)-1)%2+1), nth = 0, candidates[MAX_LOADOUTS], nc = 0;
    const char *role = bot_role_class(BOT_ROSTER(bot));
    if (role)
        for (int i = 0; i < lib->count; i++)
            if (lib->l[i].team == team && lib->l[i].nWeapons && !strcmp(kit_class(lib->l[i].kit), role)) return i;
    for (int i = 0; i < H->botCount && H->bots[i] != bot; i++) if (player_team(H->bots[i]) == team) nth++;
    for (int i = 0; i < lib->count; i++) if (lib->l[i].team == team && lib->l[i].nWeapons) candidates[nc++] = i;
    return nc ? candidates[nth % nc] : -1;
}

static void apply_kit(char *bot)
{
    LoadoutLib *lib = loadout_lib();
    int team = player_team(bot), bi = bot_index(bot);
    int idx = pick_loadout(bot);
    if (idx < 0) { bot_log("kit: no loadout captured for team %d yet", team); return; }
    if (apply_loadout(bot, idx)) {
        if (bi >= 0) g_botLoadout[bi] = idx;
        *kitted_mask() |= 1ull << bi;
        bot_log("kit: %s gets team %d %s", bot + OFF_PLAYER_ONLINE_NAME, team, kit_class(lib->l[idx].kit));
    }
}

// Loadout capture is off unless BotMod_capture.on exists next to the server exe ('capture on|off' toggles it),
// so the library stays fixed no matter what humans deploy as.
static int capture_enabled(void)
{
    static unsigned checkedAt;
    static int enabled;
    if (!checkedAt || H->frame - checkedAt > 150) {
        char dir[MAX_PATH], path[MAX_PATH + 32];
        GetModuleFileNameA(NULL, dir, sizeof dir);
        *strrchr(dir, '\\') = 0;
        snprintf(path, sizeof path, "%s\\BotMod_capture.on", dir);
        enabled = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
        checkedAt = H->frame ? H->frame : 1;
    }
    return enabled;
}

static int player_exists(char *pl);
// Class each human last deployed as (sent with their unlock selection on every deploy).
static char *g_humanKitPlayer[64];
static char g_humanEngineer[64];
static int player_is_engineer(char *pl)
{
    if (is_bot(pl)) return strstr(BOT_ROSTER(pl), "Engineer") != NULL;
    for (int k = 0; k < 64; k++) if (g_humanKitPlayer[k] == pl) return g_humanEngineer[k];
    return 0;
}

void __cdecl on_select_unlocks(void *player, unsigned *stack)
{
    if (!is_bot(player)) {
        const char *k = asset_name(*(void **)((unsigned char *)stack[1] + 0x38));
        int slot = -1;
        for (int j = 0; j < 64 && slot < 0; j++) if (g_humanKitPlayer[j] == player) slot = j;
        for (int j = 0; j < 64 && slot < 0; j++) if (!g_humanKitPlayer[j] || !player_exists(g_humanKitPlayer[j])) slot = j;
        if (slot >= 0) { g_humanKitPlayer[slot] = player; g_humanEngineer[slot] = !strcmp(kit_class(k), "Engineer"); }
    }
    if (is_bot(player) || !capture_enabled()) return;
    unsigned char *msg = (unsigned char *)stack[1];
    int team = player_team(player);
    if (team != 1 && team != 2) return;
    PendingKit *pk = pending_for(player);
    const char *kit = asset_name(*(void **)(msg + 0x38));
    if (!strcmp(kit, "?")) return;
    LoadoutLib *lib = loadout_lib();
    int idx = -1;
    for (int i = 0; i < lib->count; i++) if (lib->l[i].team == team && !strcmp(lib->l[i].kit, kit)) idx = i;
    if (idx < 0) { if (lib->count >= MAX_LOADOUTS) return; idx = lib->count++; }
    Loadout *L = &lib->l[idx];
    int newWeapons = H->frame - pk->lastFrame <= 90 ? pk->n : 0;   // weapons sent just before these unlocks
    if (!newWeapons && L->nWeapons) {
        // a plain redeploy does not resend weapons: keep the ones we already have for this kit
        capture_msg(L->unlockMsg, msg, 0);
        bot_log("loadout: team %d %s unlocks refreshed from %s (weapons kept)", team, kit_class(kit), (char *)player + OFF_PLAYER_ONLINE_NAME);
        save_loadouts();
        return;
    }
    if (!newWeapons) { bot_log("loadout: %s sent no weapon list for %s - change loadout once to capture it", (char *)player + OFF_PLAYER_ONLINE_NAME, kit); }
    memset(L, 0, sizeof *L);
    L->team = team;
    strncpy(L->kit, kit, sizeof L->kit - 1);
    capture_msg(L->unlockMsg, msg, 0);
    L->nWeapons = newWeapons;
    memcpy(L->weaponMsg, pk->weapons, sizeof(L->weaponMsg[0]) * L->nWeapons);
    pk->n = 0;
    log_loadout(L, (char *)player + OFF_PLAYER_ONLINE_NAME);
    save_loadouts();
}

static int g_kitWatch;   // console: kitwatch 0/1 - log every weapon a human selects (diagnostics, nothing saved)
static void log_weapon_msg(const char *who, const unsigned char *msg)
{
    void *asset = *(void **)(msg + 0x3c);
    char hex[300];
    int n = 0;
    for (int o = 0; o < 0x60 && n < (int)sizeof hex - 10; o += 4) n += snprintf(hex + n, sizeof hex - n, "%08x ", *(unsigned *)(msg + o));
    bot_log("kitwatch: %s slot %d asset %p (vt %08x) %s | %s", who, *(int *)(msg + 0x38), asset,
            asset && !IsBadReadPtr(asset, 4) ? *(unsigned *)asset : 0, asset_name(asset), hex);
}
void __cdecl on_select_weapon(void *player, unsigned *stack)
{
    if (g_kitWatch && !is_bot(player)) log_weapon_msg((char *)player + OFF_PLAYER_ONLINE_NAME, (unsigned char *)stack[1]);
    if (is_bot(player) || !capture_enabled()) return;
    unsigned char *msg = (unsigned char *)stack[1];
    PendingKit *pk = pending_for(player);
    if (H->frame - pk->lastFrame > 60) pk->n = 0;   // >2 s gap: new batch
    pk->lastFrame = H->frame;
    if (pk->n < KIT_MAX_WEAPONS) capture_msg(pk->weapons[pk->n++], msg, 1);
}
extern void hook_select_unlocks(void);
extern void hook_select_weapon(void);
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _hook_select_unlocks\n"
    "_hook_select_unlocks:\n"
    "  pushad\n"
    "  pushfd\n"
    "  lea eax, [esp + 36]\n"
    "  push eax\n"
    "  push ecx\n"
    "  call _on_select_unlocks\n"
    "  add esp, 8\n"
    "  popfd\n"
    "  popad\n"
    "  push ebp\n"
    "  mov ebp, esp\n"
    "  and esp, 0xfffffff8\n"
    "  push 0x00e331b6\n"
    "  ret\n"
    ".globl _hook_select_weapon\n"
    "_hook_select_weapon:\n"
    "  pushad\n"
    "  pushfd\n"
    "  lea eax, [esp + 36]\n"
    "  push eax\n"
    "  push ecx\n"
    "  call _on_select_weapon\n"
    "  add esp, 8\n"
    "  popfd\n"
    "  popad\n"
    "  sub esp, 0x24\n"
    "  push ebx\n"
    "  push ebp\n"
    "  push 0x00e33045\n"
    "  ret\n"
    ".att_syntax prefix\n");

// ---- Soldier update diagnostics + experimental bot stepping ----
static unsigned g_applyCalls, g_prePhysCalls;

void __cdecl on_soldier_apply(void *soldier, unsigned *stack)
{
    if (!H->traceInput || !soldier_is_bot(soldier)) return;
    if (g_applyCalls++ % 30) return;
    float *lv = (float *)((char *)stack[2] + 0x10);
    float *pos = (float *)((char *)soldier + OFF_SOLDIER_POSITION);
    bot_log("  prePhysicsUpdateEntry soldier=%p entry=%u input=%p levels=[%.2f %.2f %.2f] pos=(%.1f,%.1f,%.1f) calls=%u",
            soldier, stack[1], (void *)stack[2], lv[0], lv[1], lv[2], pos[0], pos[1], pos[2], g_applyCalls);
}

void __cdecl on_soldier_prephys(void *soldier, unsigned *stack)
{
    if (!H->traceInput) return;
    if (g_prePhysCalls++ % 61) return;   // odd modulus so alternating soldiers all get sampled
    unsigned *arg = (unsigned *)stack[1];
    // pushad saved the caller's registers just below the return address: esi (the update-list owner) is at stack[-7].
    unsigned *owner = (unsigned *)stack[-7];
    unsigned listBegin = owner ? owner[0x54 / 4] : 0, listEnd = owner ? owner[0x58 / 4] : 0;
    bot_log("  prePhysicsUpdate soldier=%p bot=%d ret=%08x arg=%p [%08x %08x %08x %08x] owner=%p vt=%08x list=%08x..%08x (%u entries)",
            soldier, soldier_is_bot(soldier), stack[0], arg, arg ? arg[0] : 0, arg ? arg[1] : 0, arg ? arg[2] : 0, arg ? arg[3] : 0,
            owner, owner ? owner[0] : 0, listBegin, listEnd, (listEnd - listBegin) / 12);
}

extern void hook_soldier_apply(void);
extern void hook_soldier_prephys(void);
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _hook_soldier_apply\n"
    "_hook_soldier_apply:\n"
    "  pushad\n"
    "  pushfd\n"
    "  lea eax, [esp + 36]\n"
    "  push eax\n"
    "  push ecx\n"
    "  call _on_soldier_apply\n"
    "  add esp, 8\n"
    "  popfd\n"
    "  popad\n"
    "  push 0x00e0a7c0\n"
    "  ret\n"
    ".globl _hook_soldier_prephys\n"
    "_hook_soldier_prephys:\n"
    "  pushad\n"
    "  pushfd\n"
    "  lea eax, [esp + 36]\n"
    "  push eax\n"
    "  push ecx\n"
    "  call _on_soldier_prephys\n"
    "  add esp, 8\n"
    "  popfd\n"
    "  popad\n"
    "  push 0x00e0a3d0\n"
    "  ret\n"
    ".att_syntax prefix\n");

// ---- Bot soldier stepping ----
// In MP (SingleStepCharacter) a human soldier only moves when its connection single-steps it once per received move:
// soldier->singleStep(entry) applies player+0x3c0 input, advances the character physics by the input's deltaTime and
// finishes the move. Bots have no connection, so we single-step their soldiers ourselves once per server frame.
#define SOLDIER_VSLOT_SINGLE_STEP  0x140       // ServerSoldierEntity::singleStep(entryIndex) = 0x00e009c0
typedef void (__attribute__((thiscall)) *SingleStep_t)(void *soldier, int entry);

static void step_bot_soldiers(void)
{
    for (int i = 0; i < H->botCount; i++) {
        void *s = *(void **)((char *)H->bots[i] + OFF_PLAYER_SOLDIER);
        if (!s) continue;
        SingleStep_t fn = (SingleStep_t)(*(void ***)s)[SOLDIER_VSLOT_SINGLE_STEP / 4];
        fn(s, 0);
    }
}
// ---- Steering ----
// Per-bot navigation state in host->reserved (survives reloads): mode 0 = manual (move command), 1 = face heading,
// 2 = go to point (face it, walk, stop within GOTO_ARRIVE_DIST).
typedef struct BotNav { int mode; float targetYaw; float x, z; } BotNav;   // mode 3 = follow planned path
#define GOTO_ARRIVE_DIST 2.0f
static BotNav *bot_nav(int i) { return (BotNav *)(H->reserved + 1536) + i; }   // +1536..+2560

static float wrap_pi(float a)
{
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

// Heading that points the soldier along (dx, dz). Measured: yaw 0 walks +z, yaw -90deg walks -x => forward = (sin yaw, cos yaw).
static float yaw_towards(float dx, float dz) { return atan2f(dx, dz); }

// Stuck handling independent of re-planning: trying to move but no 1 m of progress in 3 s -> back off and sidestep for
// a second, block the grid links in that direction and re-plan.
typedef struct Progress { float x, z; unsigned frame; unsigned unstickUntil; float strafe; unsigned directUntil; int directTries; } Progress;
static Progress g_progress[MAX_BOTS];
static void on_bot_stuck(int i, char *soldier);
static void press_button(int i, int action, int frames);
#define EIA_JUMP 12
// Stuck against something knee/waist high (barriers, low walls, crates): jumping into it vaults over - as long as
// forward stays held until the vault is done (VAULT_HOLD_FRAMES), whatever the stuck handling wanted next.
#define VAULT_HOLD_FRAMES 30
static unsigned g_vaultUntil[MAX_BOTS];
static float g_vaultFrom[MAX_BOTS][3];
static void stuck_jump(int i, char *soldier, const char *why)
{

    if (H->frame - g_local_stuck_jump_lastAt[i] < 40) return;
    g_local_stuck_jump_lastAt[i] = H->frame;
    press_button(i, EIA_JUMP, 4);
    g_vaultUntil[i] = H->frame + VAULT_HOLD_FRAMES;
    if (soldier) memcpy(g_vaultFrom[i], soldier + OFF_SOLDIER_POSITION, 12);
    static unsigned logged;
    if (logged++ < 50) bot_log("path: %s jumps (%s)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, why);
    (void)soldier;
}
static void path_goal(int i, float *x, float *z);
static void direct_target_pick(int i, char *soldier);   // AltBotMod V18.1
static float g_directTgt[MAX_BOTS][2];
static unsigned char g_directKind[MAX_BOTS];
// AltBotMod V8: crouch through. A bot stopped by something at head height with a clear way through at crouch height (a
// pipe, a beam, a low opening, a vehicle's barrel) crouches and walks on for 2.5 s, then stands. Jumping, the old first
// answer to every block, cannot get under anything.
static void bot_crouch(int i);
static void bot_stand(int i);
static int g_crouchAction;   // (defined with the stance code below)
static unsigned g_crouchWalkUntil[MAX_BOTS];
static unsigned g_crouchWalks;

// ---- Fences: a bot stuck against a chain-link fence (recognised by the material its ray hits - the "sonar
// signature" catalogue below) knifes through it like a player would, then walks on. If it is still stuck at the
// same spot afterwards the usual escape runs: jump, walk straight, back off and re-route. ----
typedef struct FenceKind { unsigned material; const char *name; int knifeable; } FenceKind;
static const FenceKind kFences[] = {
    {0x03003a83, "chain-link fence", 1},    // MP_007: GroupPhysicsEntity part, ~2 cm, 4-6 m tall, two-sided; a knife cuts it
    {0x200014d5, "sheet-metal fence", 0},   // MP_007: GroupPhysicsEntity; knives do nothing - go round
};
#define EIA_FIRE_KNIFE 7
static unsigned g_fenceUntil[MAX_BOTS], g_fenceAt[MAX_BOTS];
static unsigned char g_fenceTries[MAX_BOTS];   // knife attempts at the current spot (2nd one sidesteps off a post first)
static float g_fenceStrafe[MAX_BOTS];
static float g_fencePos[MAX_BOTS][3];
static unsigned char g_lastHitRest[0x40];
static int ray_cast(const float *from3, const float *to3, float *hitPos, float *hitNormal);
static unsigned g_rayFlags;
void schedule_press(int i, int action, unsigned at);
static unsigned hit_material(void)   // MaterialContainerPair flags+index of the last ray hit (0 = unknown)
{
    char *mp = *(char **)(g_lastHitRest + 4);
    return mp && !IsBadReadPtr(mp, 12) ? *(unsigned *)(mp + 8) : 0;
}
// The catalogued fence right in front of the bot at chest height, or NULL
static const FenceKind *fence_ahead(int i, char *soldier)
{
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION), yaw = *(float *)(soldier + OFF_SOLDIER_YAW);
    float a[3] = {pos[0], pos[1] + 1.2f, pos[2]}, b[3] = {pos[0] + sinf(yaw) * 1.5f, pos[1] + 1.2f, pos[2] + cosf(yaw) * 1.5f}, h[3];
    unsigned saved = g_rayFlags;
    g_rayFlags = 0x20;   // RAY_DONT_CHECK_CHARACTER
    int r = ray_cast(a, b, h, NULL);
    g_rayFlags = saved;
    if (!r) return NULL;
    unsigned m = hit_material();
    for (unsigned k = 0; k < sizeof kFences / sizeof kFences[0]; k++) if (m == kFences[k].material) return &kFences[k];
    static unsigned logged;
    if ((m >> 24) == 0x03 && logged++ < 30)   // see-through + penetrable, but not catalogued yet
        bot_log("fence: %s is stuck at an uncatalogued see-through material %08x at (%.1f, %.1f, %.1f)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, m, h[0], h[1], h[2]);
    return NULL;
}
// ---- AltBotMod V18: glass and other thin panels (windows, glass walls and doors - XP2_Office is full of them). The
// grid and the recorded routes go through doorways and window gaps whose glass a person breaks in passing; a bot stood
// against the pane. A bot stuck against something thin (under 30 cm, floor and room behind it) knifes it, and shoots it
// if the knife did nothing; a material that breaks is learned (BotMod_breakable.txt, every level), one that does not is
// left alone. Learned materials count as passable for the grid's rays (NAV_HIT_FENCE) on the next build of a map.
#define MAX_BREAKABLE 64
static unsigned g_breakMat[MAX_BREAKABLE], g_unbreakMat[MAX_BREAKABLE]; static int g_breakN, g_unbreakN, g_breakLoaded;
static unsigned g_panelCheckAt[MAX_BOTS], g_panelMat[MAX_BOTS]; static unsigned char g_panelStage[MAX_BOTS];
// AltBotMod V32: shooting a way open (breach_start / breach_tick)
static unsigned g_breachUntil[MAX_BOTS], g_breachAt[MAX_BOTS], g_breaches, g_breachOpened; static unsigned char g_breachKnife[MAX_BOTS]; static unsigned g_breachMat[MAX_BOTS], g_panesTried, g_panesBroken; /* V34: knife first (a pane) */ static float g_breachYaw[MAX_BOTS], g_breachPitch[MAX_BOTS], g_breachPos[MAX_BOTS][3], g_breachFrom[MAX_BOTS][3], g_breachTo[MAX_BOTS][3]; static unsigned char g_breachTries[MAX_BOTS];
static float g_panelHit[MAX_BOTS][3], g_panelFrom[MAX_BOTS][3];
static void breakable_path(char *out, int n)
{
    char dir[MAX_PATH], old[MAX_PATH + 32]; GetModuleFileNameA(NULL, dir, sizeof dir); *strrchr(dir, '\\') = 0;
    // AltBotMod V21: with everything else learned, in BotMod_learned (V18-V20 kept it as BotMod_breakable.txt: moved over)
    snprintf(out, n, "%s\\BotMod_learned", dir); CreateDirectoryA(out, NULL);
    snprintf(out, n, "%s\\BotMod_learned\\breakable.txt", dir);
    snprintf(old, sizeof old, "%s\\BotMod_breakable.txt", dir);
    if (GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES && GetFileAttributesA(old) != INVALID_FILE_ATTRIBUTES) MoveFileA(old, out);
}
static void breakable_load(void)
{
    if (g_breakLoaded) return;
    g_breakLoaded = 1;
    char path[MAX_PATH + 32], line[64]; breakable_path(path, sizeof path);
    FILE *f = fopen(path, "r"); if (!f) return;
    while (fgets(line, sizeof line, f)) {
        unsigned m; char kind = 0;
        if (sscanf(line, "%x %c", &m, &kind) < 1 || !m) continue;
        if (kind == 'n') { if (g_unbreakN < MAX_BREAKABLE) g_unbreakMat[g_unbreakN++] = m; }
        else if (g_breakN < MAX_BREAKABLE) g_breakMat[g_breakN++] = m;
    }
    fclose(f);
    if (g_breakN || g_unbreakN) bot_log("glass: %d breakable and %d unbreakable panel material(s) known", g_breakN, g_unbreakN);
}
static int material_listed(const unsigned *list, int n, unsigned m) { for (int k = 0; k < n; k++) if (list[k] == m) return 1; return 0; }
static int material_breakable(unsigned m) { return m && material_listed(g_breakMat, g_breakN, m); }
static void breakable_learn(unsigned m, int breaks)
{
    if (!m) return;
    if (!breaks) {   // AltBotMod V34: a pane is "unbreakable" only after a second failed try (V18-V33: after one - a knife that
                     // missed, a bot that turned away: 64 unbreakable against 11 breakable, and every pane of that kind
                     // was never tried again - M-COMs behind a window stayed out of reach)
        static unsigned mats[64]; static unsigned char fails[64]; int k, freeK = -1;
        if (material_breakable(m)) return;
        for (k = 0; k < 64 && mats[k] != m; k++) if (!mats[k] && freeK < 0) freeK = k;
        if (k == 64) { if (freeK < 0) return; k = freeK; mats[k] = m; fails[k] = 0; }
        if (++fails[k] < 2) return;
    }
    unsigned *list = breaks ? g_breakMat : g_unbreakMat; int *n = breaks ? &g_breakN : &g_unbreakN;
    if (material_listed(list, *n, m) || *n >= MAX_BREAKABLE) return;
    list[(*n)++] = m;
    char path[MAX_PATH + 32]; breakable_path(path, sizeof path);
    FILE *f = fopen(path, "a"); if (f) { fprintf(f, "%08x %c\n", m, breaks ? 'b' : 'n'); fclose(f); }
}
// Something thin within `reach` along (fx, fz) at chest height (a pane, a panel), with floor and room behind it. Returns
// its material. anyMaterial: also a material once learned unbreakable (the pane in front of an M-COM is always tried).
static int panel_dir(char *soldier, float fx, float fz, float reach, int anyMaterial, float *hitOut, float *fromOut, unsigned *mat)
{
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION);
    float a[3] = {pos[0], pos[1] + 1.2f, pos[2]}, b[3] = {pos[0] + fx * reach, pos[1] + 1.2f, pos[2] + fz * reach}, h[3], h2[3], g[3];
    unsigned saved = g_rayFlags; g_rayFlags = 0x20;   // RAY_DONT_CHECK_CHARACTER
    int ok = ray_cast(a, b, h, NULL);
    unsigned m = ok ? hit_material() : 0;
    if (ok) {   // the back face within 30 cm (a wall is thicker), seen from 45 cm beyond the front face
        float back[3] = {h[0] + fx * 0.45f, h[1], h[2] + fz * 0.45f}, front[3] = {h[0] - fx * 0.05f, h[1], h[2] - fz * 0.05f};
        if (ray_cast(back, front, h2, NULL) && (h2[0] - h[0]) * fx + (h2[2] - h[2]) * fz > 0.30f) ok = 0;
        // room behind: clear for another metre at chest height, and floor under it near our level
        float beyond[3] = {back[0] + fx * 1.0f, back[1], back[2] + fz * 1.0f};
        if (ok && ray_cast(back, beyond, NULL, NULL)) ok = 0;
        float down[3] = {back[0], pos[1] - 1.5f, back[2]};
        if (ok && (!ray_cast(back, down, g, NULL) || fabsf(g[1] - pos[1]) > 1.2f)) ok = 0;
    }
    g_rayFlags = saved;
    if (!ok || (!anyMaterial && material_listed(g_unbreakMat, g_unbreakN, m))) return 0;
    memcpy(hitOut, h, 12); memcpy(fromOut, a, 12); *mat = m;
    return 1;
}
// Something thin right in front at chest height (the way the soldier faces, 1.4 m).
static int panel_ahead(char *soldier, float *hitOut, float *fromOut, unsigned *mat)
{
    float yaw = *(float *)(soldier + OFF_SOLDIER_YAW);
    return panel_dir(soldier, sinf(yaw), cosf(yaw), 1.4f, 0, hitOut, fromOut, mat);
}
// Knife it now; after the knife, shoot it; after that, learn what happened (panel_check_tick).
static int try_break_panel(int i, char *soldier)
{
    breakable_load();
    float h[3], a[3]; unsigned m;
    if (g_panelStage[i] || !panel_ahead(soldier, h, a, &m)) return 0;
    unsigned f = H->frame;
    schedule_press(i, SLOT_ACTION(7), f + 1);   // knife out
    schedule_press(i, EIA_FIRE_KNIFE, f + 22);
    schedule_press(i, EIA_FIRE_KNIFE, f + 42);
    schedule_press(i, SLOT_ACTION(0), f + 62);  // rifle back out
    g_fenceUntil[i] = f + 66;                   // (steer_bot holds still meanwhile)
    g_panelStage[i] = 1; g_panelCheckAt[i] = f + 68; g_panelMat[i] = m; memcpy(g_panelHit[i], h, 12); memcpy(g_panelFrom[i], a, 12);
    bot_log("glass: %s knifes a thin panel (material %08x%s) at (%.1f, %.1f, %.1f)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, m,
            material_breakable(m) ? ", known to break" : "", h[0], h[1], h[2]);
    return 1;
}
static void panel_check_tick(int i)
{
    if (!g_panelStage[i] || H->frame < g_panelCheckAt[i]) return;
    float to[3] = {g_panelHit[i][0] + (g_panelHit[i][0] - g_panelFrom[i][0]) * 0.1f, g_panelHit[i][1], g_panelHit[i][2] + (g_panelHit[i][2] - g_panelFrom[i][2]) * 0.1f};
    unsigned saved = g_rayFlags; g_rayFlags = 0x20;
    int still = ray_cast(g_panelFrom[i], to, NULL, NULL);
    g_rayFlags = saved;
    const char *name = (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME;
    if (!still) {
        bot_log("glass: %s broke it (%s) - material %08x %s", name, g_panelStage[i] == 1 ? "knife" : "shots", g_panelMat[i],
                material_breakable(g_panelMat[i]) ? "" : "learned as breakable");
        breakable_learn(g_panelMat[i], 1); g_panelStage[i] = 0; return;
    }
    if (g_panelStage[i] == 1) {   // the knife did nothing: three shots at it
        unsigned f = H->frame;
        schedule_press(i, EIA_FIRE, f + 2); schedule_press(i, EIA_FIRE, f + 8); schedule_press(i, EIA_FIRE, f + 14);
        g_fenceUntil[i] = f + 20; g_panelStage[i] = 2; g_panelCheckAt[i] = f + 24;
        return;
    }
    bot_log("glass: %s cannot break material %08x - goes round", name, g_panelMat[i]);
    if (!material_breakable(g_panelMat[i])) breakable_learn(g_panelMat[i], 0);
    g_panelStage[i] = 0;
}
static char *player_vehicle(char *pl);
static float dist2(const float *a, const float *b);
// AltBotMod V32: a closed door the bot's route goes through - something solid at chest height within 2.5 m, the route's
// next point beyond it. Doors in some levels open when something next to them blows up (Epicenter: red gas tanks beside
// the doors) or when they are shot (boards, thin doors): the bot sweeps its fire across the door and low on both sides of
// it for 3.5 s, then looks again. Twice per spot (4 m) every 30 s at most; the usual recovery goes on meanwhile.
static int breach_start(int i, char *soldier)
{
    BotNav *nav = bot_nav(i);
    if (nav->mode != 2 && nav->mode != 3) return 0;
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION);
    float dx = nav->x - pos[0], dz = nav->z - pos[2], dl = sqrtf(dx * dx + dz * dz);
    if (dl < 3.0f) return 0;
    dx /= dl; dz /= dl;
    float mx = pos[0] - g_breachPos[i][0], mz = pos[2] - g_breachPos[i][2];
    int here = g_breachAt[i] && H->frame - g_breachAt[i] < 30 * 30 && mx * mx + mz * mz < 4.0f * 4.0f;
    if (here && g_breachTries[i] >= 2) return 0;
    float a[3] = {pos[0], pos[1] + 1.2f, pos[2]}, b[3] = {pos[0] + dx * 2.5f, pos[1] + 1.2f, pos[2] + dz * 2.5f}, h[3];
    unsigned saved = g_rayFlags; g_rayFlags = 0x20;   // RAY_DONT_CHECK_CHARACTER
    int blocked = ray_cast(a, b, h, NULL);
    // a door, not a hillside: the obstacle is upright (also blocked at 0.5 m and at 1.8 m)
    float a2[3] = {a[0], pos[1] + 0.5f, a[2]}, b2[3] = {b[0], pos[1] + 0.5f, b[2]}, a3[3] = {a[0], pos[1] + 1.8f, a[2]}, b3[3] = {b[0], pos[1] + 1.8f, b[2]};
    if (blocked) blocked = ray_cast(a2, b2, NULL, NULL) && ray_cast(a3, b3, NULL, NULL);
    g_rayFlags = saved;
    if (!blocked) return 0;
    g_breachTries[i] = here ? g_breachTries[i] + 1 : 1;
    g_breachAt[i] = H->frame ? H->frame : 1; memcpy(g_breachPos[i], pos, 12); memcpy(g_breachFrom[i], a, 12); memcpy(g_breachTo[i], b, 12);
    g_breachYaw[i] = yaw_towards(dx, dz); g_breachPitch[i] = -0.2f;
    g_breachUntil[i] = H->frame + 105; g_fenceUntil[i] = H->frame + 105;   // (steer_bot holds still meanwhile)
    g_breaches++;
    if (g_breaches <= 20 || g_breaches % 25 == 0)
        bot_log("breach: %s's way is shut at (%.1f, %.1f, %.1f), %.1f m ahead - shoots across it and low on both sides (a door, a gas tank next to it; %u so far, %u opened)",
                (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, h[0], h[1], h[2], sqrtf(dist2(h, pos)), g_breaches, g_breachOpened);
    return 1;
}
// While breaching: turn to the next of six aim points (the door, then low left and right of it) and fire three shots at each.
static void breach_tick(int i, char *soldier)
{
    if (!g_breachUntil[i] || !soldier) return;
    if (g_breachKnife[i]) {   // AltBotMod V34: a pane between the bot and its M-COM - face it, knife it (3 slashes), look again
        if (H->frame >= g_breachUntil[i] || player_vehicle(H->bots[i])) {
            unsigned saved = g_rayFlags; g_rayFlags = 0x20;
            int still = ray_cast(g_breachFrom[i], g_breachTo[i], NULL, NULL);
            g_rayFlags = saved;
            g_breachKnife[i] = 0;
            if (!still) {
                g_panesBroken++; breakable_learn(g_breachMat[i], 1); g_breachUntil[i] = 0;
                bot_log("glass: %s knifed the pane on its way to the M-COM (%u of %u)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, g_panesBroken, g_panesTried);
                return;
            }
            if (player_vehicle(H->bots[i])) { g_breachUntil[i] = 0; return; }
            // the knife did nothing: shoot it (the breach sweep - the pane, then low on both sides)
            g_breachUntil[i] = H->frame + 105; g_fenceUntil[i] = H->frame + 105; g_breaches++;
            bot_log("glass: %s's knife did not break the pane - shoots it", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME);
            return;
        }
        unsigned age = 75 - (g_breachUntil[i] - H->frame);
        float want = g_breachYaw[i], yaw = *(float *)(soldier + OFF_SOLDIER_YAW), t = fmodf(want, 6.2831853f);
        if (t > 0) t -= 6.2831853f;
        float up = t - yaw, down = up > 0 ? yaw - t + 6.2831853f : yaw - t, err = (up > 0 && up < down) ? up : -down;
        g_breachPitch[i] = 0.0f;
        bot_nav(i)->targetYaw = want;
        H->botMove[i][0] = 0; H->botMove[i][1] = 0; H->botMove[i][2] = fmaxf(-1.0f, fminf(1.0f, -err * 4.0f));
        if (age == 12) schedule_press(i, SLOT_ACTION(7), H->frame + 1);   // knife out
        if ((age == 34 || age == 48 || age == 62) && fabsf(err) < 0.25f) schedule_press(i, EIA_FIRE_KNIFE, H->frame + 1);
        if (age == 72) schedule_press(i, SLOT_ACTION(0), H->frame + 1);   // rifle back out
        return;
    }
    if (H->frame >= g_breachUntil[i] || player_vehicle(H->bots[i])) {
        g_breachUntil[i] = 0;
        unsigned saved = g_rayFlags; g_rayFlags = 0x20;
        int still = ray_cast(g_breachFrom[i], g_breachTo[i], NULL, NULL);
        g_rayFlags = saved;
        if (!still) { g_breachOpened++; g_breachTries[i] = 0; bot_log("breach: %s shot its way open (%u of %u)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, g_breachOpened, g_breaches); }
        return;
    }
    static const float kYaw[6] = {0.0f, -0.45f, -0.25f, 0.25f, 0.45f, 0.0f}, kPitch[6] = {-0.15f, -0.38f, -0.32f, -0.32f, -0.38f, -0.45f};
    unsigned age = 105 - (g_breachUntil[i] - H->frame);
    int k = (int)(age / 17); if (k > 5) k = 5;
    float want = g_breachYaw[i] + kYaw[k];
    g_breachPitch[i] = kPitch[k];
    float yaw = *(float *)(soldier + OFF_SOLDIER_YAW), t = fmodf(want, 6.2831853f);
    if (t > 0) t -= 6.2831853f;
    float up = t - yaw, down = up > 0 ? yaw - t + 6.2831853f : yaw - t, err = (up > 0 && up < down) ? up : -down;
    bot_nav(i)->targetYaw = want;
    H->botMove[i][0] = 0; H->botMove[i][1] = 0; H->botMove[i][2] = fmaxf(-1.0f, fminf(1.0f, -err * 4.0f));
    unsigned phase = age % 17;
    if ((phase == 9 || phase == 12 || phase == 15) && fabsf(err) < 0.12f) schedule_press(i, EIA_FIRE, H->frame + 1);
}
// 1: knifing a fence; 2: a fence no knife gets through (go round now); 0: no catalogued fence / tried here already
static int try_cut_fence(int i, char *soldier)
{
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION);
    float dx = pos[0] - g_fencePos[i][0], dz = pos[2] - g_fencePos[i][2];
    int again = g_fenceAt[i] && H->frame - g_fenceAt[i] < 20 * 30 && dx * dx + dz * dz < 3.0f * 3.0f;
    if (again && g_fenceTries[i] >= 2) return 0;   // tried here twice already
    if (!again) g_fenceTries[i] = 0;
    const FenceKind *fk = fence_ahead(i, soldier);
    if (!fk) { int r = try_break_panel(i, soldier); return r ? r : breach_start(i, soldier); }   // AltBotMod V18: glass / thin panels; V32: a shut door
    if (!fk->knifeable) {
        bot_log("fence: %s is stuck at a %s at (%.1f, %.1f) - going round", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, fk->name, pos[0], pos[2]);
        g_fenceAt[i] = H->frame;
        memcpy(g_fencePos[i], pos, 12);
        return 2;
    }
    unsigned f = H->frame;
    g_fenceTries[i]++;
    g_fenceStrafe[i] = g_fenceTries[i] > 1 ? ((rand() & 1) ? 0.8f : -0.8f) : 0.0f;   // 2nd try: a step along the fence (off a post)
    if (g_fenceStrafe[i] != 0) f += 20;
    schedule_press(i, SLOT_ACTION(7), f + 1);   // knife out
    schedule_press(i, EIA_FIRE_KNIFE, f + 25);   // slash, slash, slash
    schedule_press(i, EIA_FIRE_KNIFE, f + 45);
    schedule_press(i, EIA_FIRE_KNIFE, f + 65);
    schedule_press(i, SLOT_ACTION(0), f + 90);   // rifle back out
    g_fenceUntil[i] = f + 95;
    if (g_fenceStrafe[i] != 0) bot_log("fence: %s steps along the fence and tries again", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME);
    g_fenceAt[i] = H->frame;
    memcpy(g_fencePos[i], pos, 12);
    bot_log("fence: %s knifes through the %s at (%.1f, %.1f)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, fk->name, pos[0], pos[2]);
    return 1;
}

// AltBotMod V34: an M-COM behind a window. The operator within 15 m of its M-COM (about the same floor) looks at chest
// height from itself towards the M-COM: a pane in the way within 1.6 m - it faces it and knifes it (then shoots it if the
// knife did nothing: breach_tick); further in (up to 12 m) and the bot is getting no closer - it walks up to the pane
// first. Any pane material: the one in front of an objective is always tried (four times per spot a minute at most).
static float g_paneBest[MAX_BOTS]; static unsigned g_paneBestAt[MAX_BOTS], g_paneWalkUntil[MAX_BOTS], g_paneSpotAt[MAX_BOTS]; static unsigned char g_paneSpotTries[MAX_BOTS]; static float g_paneSpot[MAX_BOTS][3];
static int mcom_pane_tick(int i, char *soldier, const float *mcom)
{
    if (g_breachUntil[i] > H->frame) return 1;
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION);
    float dx = mcom[0] - pos[0], dz = mcom[2] - pos[2], d = sqrtf(dx * dx + dz * dz);
    if (d > 15.0f || d < 1.0f || fabsf(mcom[1] - pos[1]) > 3.0f) { g_paneBestAt[i] = 0; g_paneWalkUntil[i] = 0; return 0; }
    if (g_paneWalkUntil[i] > H->frame) {   // walking up to the pane
        if (dist2(pos, g_paneSpot[i]) > 1.2f * 1.2f) { bot_nav(i)->mode = 2; bot_nav(i)->x = g_paneSpot[i][0]; bot_nav(i)->z = g_paneSpot[i][2]; return 1; }
        g_paneWalkUntil[i] = 0;
    }
    if (!g_paneBestAt[i] || d < g_paneBest[i] - 1.0f) { g_paneBest[i] = d; g_paneBestAt[i] = H->frame ? H->frame : 1; }
    if ((H->frame + (unsigned)i) % 10) return 0;
    float fx = dx / d, fz = dz / d, h[3], a[3]; unsigned m;
    float reach = fminf(d + 0.2f, 12.0f);
    if (!panel_dir(soldier, fx, fz, reach, 1, h, a, &m)) return 0;
    float dh = sqrtf((h[0] - pos[0]) * (h[0] - pos[0]) + (h[2] - pos[2]) * (h[2] - pos[2]));
    if (dh > 1.6f) {   // further in: only when the route is not getting it closer (3 s)
        if (H->frame - g_paneBestAt[i] < 3 * 30) return 0;
        g_paneSpot[i][0] = h[0] - fx * 0.9f; g_paneSpot[i][1] = pos[1]; g_paneSpot[i][2] = h[2] - fz * 0.9f;
        g_paneWalkUntil[i] = H->frame + 5 * 30; g_paneBestAt[i] = H->frame;
        nav_cancel_path(i); bot_nav(i)->mode = 2; bot_nav(i)->x = g_paneSpot[i][0]; bot_nav(i)->z = g_paneSpot[i][2];
        bot_log("glass: %s walks up to a pane %.1f m away between it and its M-COM", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, dh);
        return 1;
    }
    int here = g_paneSpotAt[i] && H->frame - g_paneSpotAt[i] < 60 * 30 && dist2(pos, g_paneSpot[i]) < 3.0f * 3.0f;
    if (here && g_paneSpotTries[i] >= 4) return 0;
    g_paneSpotTries[i] = here ? g_paneSpotTries[i] + 1 : 1; g_paneSpotAt[i] = H->frame ? H->frame : 1; memcpy(g_paneSpot[i], pos, 12);
    float beyond[3] = {h[0] + fx * 0.6f, h[1], h[2] + fz * 0.6f};
    memcpy(g_breachFrom[i], a, 12); memcpy(g_breachTo[i], beyond, 12); memcpy(g_breachPos[i], pos, 12);
    g_breachYaw[i] = yaw_towards(fx, fz); g_breachPitch[i] = 0; g_breachMat[i] = m; g_breachKnife[i] = 1;
    g_breachUntil[i] = H->frame + 75; g_fenceUntil[i] = H->frame + 75; g_breachAt[i] = H->frame ? H->frame : 1;
    nav_cancel_path(i); bot_nav(i)->mode = 0; memset(H->botMove[i], 0, sizeof H->botMove[i]);
    g_panesTried++;
    bot_log("glass: %s knifes the pane between it and its M-COM (material %08x, %.1f m; %u so far)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, m, dh, g_panesTried);
    return 1;
}

// For the nav builder: what the last ray hit (engine pointers straight from the hit record - no checks, this runs
// for millions of rays).
#define ADDR_WATER_PHYSICS_VTABLE 0x02486a40
static int nav_hit_class(void)
{
    char *body = *(char **)g_lastHitRest, *mp = *(char **)(g_lastHitRest + 4);
    if (body && *(unsigned *)body == ADDR_WATER_PHYSICS_VTABLE) return NAV_HIT_WATER;
    if (mp) {
        unsigned m = *(unsigned *)(mp + 8);
        for (unsigned k = 0; k < sizeof kFences / sizeof kFences[0]; k++) if (m == kFences[k].material && kFences[k].knifeable) return NAV_HIT_FENCE;
        if (g_breakN && material_breakable(m)) return NAV_HIT_FENCE;   // AltBotMod V18: learned glass / panels
    }
    return 0;
}

// AltBotMod V8: what the last ray hit is a fence a vehicle drives through (chain-link or sheet metal - both flatten
// under a jeep or a tank in BF3)
// AltBotMod V24: the physics body a ray met belongs to another vehicle (each vehicle's body pointer is at +0x30)
static int vehicle_list(char ***out);
static int drive_hit_vehicle(const void *body, char *own)
{
    if (!body) return 0;
    char **vehs; int n = vehicle_list(&vehs);
    for (int k = 0; k < n && k < 256; k++) {
        char *v = vehs[k];
        if (!v || v == own || IsBadReadPtr(v + 0x30, 4)) continue;
        if (*(void **)(v + 0x30) == body) return 1;
    }
    return 0;
}
static int vehicle_crushable_hit(void)
{
    char *mp = *(char **)(g_lastHitRest + 4);
    if (!mp || IsBadReadPtr(mp + 8, 4)) return 0;
    unsigned m = *(unsigned *)(mp + 8);
    for (unsigned k = 0; k < sizeof kFences / sizeof kFences[0]; k++) if (m == kFences[k].material) return 1;
    return 0;
}
static int crouch_gap(char *soldier)
{
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION), yaw = *(float *)(soldier + OFF_SOLDIER_YAW);
    float fx = sinf(yaw), fz = cosf(yaw), hit[3];
    static const float kClear[3] = {0.45f, 0.85f, 1.1f};
    unsigned saved = g_rayFlags; g_rayFlags = 0x20 | 0x01   /* RAY_DONT_CHECK_CHARACTER | RAY_CHECK_DETAIL_MESH */;
    int ok = 1;
    for (int k = 0; k < 3 && ok; k++) {
        float a[3] = {pos[0], pos[1] + kClear[k], pos[2]}, b[3] = {pos[0] + fx * 1.4f, pos[1] + kClear[k], pos[2] + fz * 1.4f};
        if (ray_cast(a, b, hit, NULL)) ok = 0;
    }
    if (ok) {   // ...and blocked higher up: the reason it is not moving
        float a[3] = {pos[0], pos[1] + 1.6f, pos[2]}, b[3] = {pos[0] + fx * 1.0f, pos[1] + 1.6f, pos[2] + fz * 1.0f};
        ok = ray_cast(a, b, hit, NULL);
    }
    g_rayFlags = saved;
    return ok;
}
static int climb_ladder(int i, char *soldier, BotNav *nav);
// ---- Stairs (and escalators / steep ramps), up or down - like ladders: at the foot or the top the bot stops, turns to
// face straight along the flight (its real direction, from the slope of the ground - not the grid), walks it without
// turning, and only goes back to normal navigation 2 m after the ground is level again. Bots used to take flights at an
// angle from a waypoint off to the side and walk into the rails. ----
#define STAIR_RISE        0.45f    // ground 1.5 m ahead this much higher / lower: stairs
#define STAIR_RELEASE     2.0f     // level ground walked after the flight before normal navigation resumes
#define STAIR_MAX_FRAMES  (12 * 30)
// ---- AltBotMod V18: making way. A bot that means to walk but stands still looks at the teammate in front of it:
//  - a teammate bot standing idle steps aside (1.3 s, to whichever side is clear - none clear: it is left alone);
//  - one walking the same way: the bot behind queues (waits half a second) instead of pushing;
//  - one coming the other way (a doorway, a stairwell): both keep to their right and pass;
//  - a human: the walker goes round.
// Never a bot on a flight of stairs or one already stepping aside. (V18 first cut made walking bots stop and strafe too:
// five bots in an Office stairwell kept asking each other out of the way and nobody went down - 2026-10-15 14:38.)
static unsigned g_wayUntil[MAX_BOTS], g_wayCheckAt[MAX_BOTS], g_wayAskedAt[MAX_BOTS], g_wayLogs;
static float g_wayStrafe[MAX_BOTS], g_wayThrottle[MAX_BOTS], g_wayLast[MAX_BOTS][3];
static char *player_vehicle(char *pl);
#define OFF_SOLDIER_HEALTH     0x128
static unsigned char g_stairOn[MAX_BOTS];
static int side_clear(const float *me, float fx, float fz, float side)
{
    float a[3] = {me[0], me[1] + 1.1f, me[2]}, b2[3] = {me[0] + fz * side * 1.3f, me[1] + 1.1f, me[2] - fx * side * 1.3f};
    unsigned saved = g_rayFlags; g_rayFlags = 0x20; int hit = ray_cast(a, b2, NULL, NULL); g_rayFlags = saved;
    return !hit;
}
static void make_way_ask(int i, char *soldier)
{
    if (!soldier || player_vehicle(H->bots[i])) return;
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION), yaw = *(float *)(soldier + OFF_SOLDIER_YAW);
    float fx = sinf(yaw), fz = cosf(yaw);
    char *pm = player_manager(); if (!pm) return;
    void **arr = *(void ***)(pm + 0xac); unsigned count = *(unsigned *)(pm + 8);
    int team = player_team(H->bots[i]);
    for (unsigned k = 0; arr && k < count && k < 64; k++) {
        char *t = arr[k], *ts;
        if (!t || t == H->bots[i] || player_team(t) != team || player_vehicle(t) || !(ts = *(char **)(t + OFF_PLAYER_SOLDIER)) || *(float *)(ts + OFF_SOLDIER_HEALTH) <= 0) continue;
        float *tp = (float *)(ts + OFF_SOLDIER_POSITION), dx = tp[0] - me[0], dz = tp[2] - me[2], d = sqrtf(dx * dx + dz * dz);
        if (d > 1.6f || d < 0.05f || fabsf(tp[1] - me[1]) > 1.6f || (dx * fx + dz * fz) / d < 0.55f) continue;
        float side = dx * fz - dz * fx;   // which side of the walker's line it stands on
        int j = is_bot(t) ? bot_index(t) : -1;
        const char *what;
        if (j < 0) { g_wayUntil[i] = H->frame + 25; g_wayStrafe[i] = side >= 0 ? -1.0f : 1.0f; g_wayThrottle[i] = 0.3f; what = "goes round"; }
        else if (H->frame < g_wayUntil[j] || g_stairOn[j]) return;
        else {
            float ty = *(float *)(ts + OFF_SOLDIER_YAW), facing = sinf(ty) * fx + cosf(ty) * fz;
            if (H->botMove[j][0] > 0.5f) {
                if (facing > 0.5f) { g_wayUntil[i] = H->frame + 15; g_wayStrafe[i] = 0; g_wayThrottle[i] = 0; what = "queues behind it"; }
                else if (facing < -0.5f) {   // head-on: both keep right
                    g_wayUntil[i] = g_wayUntil[j] = H->frame + 20; g_wayStrafe[i] = g_wayStrafe[j] = 1.0f; g_wayThrottle[i] = g_wayThrottle[j] = 0.5f;
                    what = "both keep right";
                } else return;   // crossing: it will be gone in a moment
            } else {
                float want = side >= 0 ? 1.0f : -1.0f; if (fabsf(side) < 0.15f) want = (j & 1) ? 1.0f : -1.0f;
                float *jp = tp, jf[2] = {sinf(ty), cosf(ty)};
                if (!side_clear(jp, jf[0], jf[1], want)) { want = -want; if (!side_clear(jp, jf[0], jf[1], want)) return; }
                g_wayUntil[j] = H->frame + 40; g_wayStrafe[j] = want; g_wayThrottle[j] = 0; what = "steps aside";
            }
        }
        if (g_wayLogs++ < 400) bot_log("way: %s is in %s's way at (%.1f, %.1f) - %s", t + OFF_PLAYER_ONLINE_NAME, (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME,
                                       tp[0], tp[2], what);
        g_wayAskedAt[i] = H->frame;
        return;
    }
}
static unsigned char g_waySkips[MAX_BOTS];
// A living soldier (either team) within 1.6 m in front of the bot.
static int soldier_ahead(int i, char *soldier)
{
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION), yaw = *(float *)(soldier + OFF_SOLDIER_YAW), fx = sinf(yaw), fz = cosf(yaw);
    char *pm = player_manager(); if (!pm) return 0;
    void **arr = *(void ***)(pm + 0xac); unsigned count = *(unsigned *)(pm + 8);
    for (unsigned k = 0; arr && k < count && k < 64; k++) {
        char *t = arr[k], *ts = t && t != H->bots[i] ? *(char **)(t + OFF_PLAYER_SOLDIER) : NULL;
        if (!ts || player_vehicle(t) || *(float *)(ts + OFF_SOLDIER_HEALTH) <= 0) continue;
        float *tp = (float *)(ts + OFF_SOLDIER_POSITION), dx = tp[0] - me[0], dz = tp[2] - me[2], d = sqrtf(dx * dx + dz * dz);
        if (d < 1.6f && d > 0.05f && fabsf(tp[1] - me[1]) < 1.6f && (dx * fx + dz * fz) / d > 0.4f) return 1;
    }
    return 0;
}
// Every half second: a bot that means to walk but hardly moved asks; and the step aside itself (after steering).
static void make_way_tick(int i, char *soldier)
{
    if (!soldier) return;
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION);
    if (H->frame >= g_wayCheckAt[i]) {
        g_wayCheckAt[i] = H->frame + 15;
        float dx = me[0] - g_wayLast[i][0], dz = me[2] - g_wayLast[i][2];
        if (H->botMove[i][0] > 0.5f && !g_stairOn[i] && dx * dx + dz * dz < 0.2f * 0.2f && H->frame - g_wayAskedAt[i] > 45) make_way_ask(i, soldier);
        memcpy(g_wayLast[i], me, 12);
    }
    if (H->frame < g_wayUntil[i]) { H->botMove[i][0] = fminf(H->botMove[i][0], g_wayThrottle[i]); H->botMove[i][1] = g_wayStrafe[i]; }
}
static unsigned char g_stairLevel[MAX_BOTS], g_stairJumped[MAX_BOTS];   // (g_stairOn: with the make-way code above)   // (V18: one hop over a lip / a body first)
static unsigned g_stairLandings;   // AltBotMod V18.1
static float g_stairAx[MAX_BOTS], g_stairAz[MAX_BOTS], g_stairLevelPos[MAX_BOTS][3], g_stairMovedPos[MAX_BOTS][3];
static unsigned g_stairAt[MAX_BOTS], g_stairCheckAt[MAX_BOTS], g_stairMovedAt[MAX_BOTS];
static float wrap_err(float targetYaw, float yaw);
static float dist2(const float *a, const float *b);
static void reroute_from_here(int i, char *soldier, const char *why);
static int ground_at(float x, float y, float z, float *h)
{
    float a[3] = {x, y + 1.5f, z}, b[3] = {x, y - 3.0f, z}, hit[3];
    if (!ray_cast(a, b, hit, NULL)) return 0;
    *h = hit[1];
    return 1;
}
static int stair_tick(int i, char *soldier, BotNav *nav)
{
    const char *name = (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME;
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION);
    if (nav->mode != 3) { g_stairOn[i] = 0; return 0; }
    if (!g_stairOn[i]) {
        if (H->frame < g_stairCheckAt[i]) return 0;
        g_stairCheckAt[i] = H->frame + 5;   // ~6 looks a second
        float dx = nav->x - pos[0], dz = nav->z - pos[2], d = sqrtf(dx * dx + dz * dz), h0, h1;
        if (d < 1.0f) return 0;
        dx /= d; dz /= d;
        if (!ground_at(pos[0], pos[1], pos[2], &h0) || !ground_at(pos[0] + dx * 1.5f, pos[1], pos[2] + dz * 1.5f, &h1)) return 0;
        float rise = h1 - h0;
        if (fabsf(rise) < STAIR_RISE) return 0;
        // the flight's own direction: the slope of the ground 2 m ahead (1 m either side, both axes)
        float cx = pos[0] + dx * 2.0f, cz = pos[2] + dz * 2.0f, xp, xm, zp, zm;
        float yRef = pos[1] + (rise > 0 ? 1.0f : -1.0f);
        if (!ground_at(cx + 1.0f, yRef, cz, &xp) || !ground_at(cx - 1.0f, yRef, cz, &xm) || !ground_at(cx, yRef, cz + 1.0f, &zp) ||
            !ground_at(cx, yRef, cz - 1.0f, &zm)) return 0;
        float gx = (xp - xm) * 0.5f, gz = (zp - zm) * 0.5f, gl = sqrtf(gx * gx + gz * gz);
        if (gl < 0.2f) return 0;   // not a slope there (a single step / a ledge)
        float ax = gx / gl, az = gz / gl;
        if (rise < 0) { ax = -ax; az = -az; }   // going down: face downhill
        if (ax * dx + az * dz < 0.3f) return 0;   // a flight we are only passing beside
        g_stairOn[i] = 1; g_stairLevel[i] = 0; g_stairJumped[i] = 0;
        g_stairAx[i] = ax; g_stairAz[i] = az;
        g_stairAt[i] = g_stairMovedAt[i] = H->frame;
        memcpy(g_stairMovedPos[i], pos, 12);
        bot_log("stairs: %s lines up with a flight going %s at (%.1f, %.1f), heading %.0f deg", name, rise > 0 ? "up" : "down", pos[0], pos[2],
                yaw_towards(ax, az) * 57.2958f);
    }
    // on the flight: face along it (stop to turn first), walk straight
    if (H->frame - g_stairAt[i] > STAIR_MAX_FRAMES) { g_stairOn[i] = 0; g_stairCheckAt[i] = H->frame + 5 * 30; bot_log("stairs: %s gives up on the flight (too long)", name); return 0; }
    float ax = g_stairAx[i], az = g_stairAz[i];
    float err = wrap_err(yaw_towards(ax, az), *(float *)(soldier + OFF_SOLDIER_YAW));
    nav->targetYaw = yaw_towards(ax, az);
    H->botMove[i][0] = fabsf(err) < 0.12f ? 1.0f : 0.0f;
    H->botMove[i][1] = 0;
    H->botMove[i][2] = fmaxf(-1.0f, fminf(1.0f, -err * 2.5f));
    follow_path(i, soldier, nav);   // (waypoints passed on the way still count)
    if (nav->mode != 3) { g_stairOn[i] = 0; return 0; }
    // level again under the bot (front and back of it the same height): STAIR_RELEASE m more, then normal navigation
    float hf, hb;
    if (ground_at(pos[0] + ax * 0.8f, pos[1], pos[2] + az * 0.8f, &hf) && ground_at(pos[0] - ax * 0.8f, pos[1], pos[2] - az * 0.8f, &hb) &&
        fabsf(hf - hb) < 0.25f) {
        if (!g_stairLevel[i]) { g_stairLevel[i] = 1; memcpy(g_stairLevelPos[i], pos, 12); }
        // AltBotMod V18.1: a landing (a switchback's turn, a wall 1.2 m ahead) - off the flight now and on along the
        // route; V18 walked the full STAIR_RELEASE into the landing wall, stared at it for 3 s and gave the stairs up
        float eye[3] = {pos[0], pos[1] + 1.0f, pos[2]}, ahead[3] = {pos[0] + ax * 1.2f, pos[1] + 1.0f, pos[2] + az * 1.2f};
        if (g_stairLevel[i] && ray_cast(eye, ahead, NULL, NULL)) {
            g_stairOn[i] = 0; g_stairCheckAt[i] = H->frame + 10;   // (the next flight up is looked for again at once)
            g_stairLandings++;
            if (g_stairLandings < 200) bot_log("stairs: %s reaches a landing at (%.1f, %.1f) - turns for the next flight", name, pos[0], pos[2]);
            reroute_from_here(i, soldier, "at a landing");
            return 0;
        }
        else if (dist2(pos, g_stairLevelPos[i]) >= STAIR_RELEASE * STAIR_RELEASE) {
            g_stairOn[i] = 0; g_stairCheckAt[i] = H->frame + 30;
            bot_log("stairs: %s is off the flight at (%.1f, %.1f)", name, pos[0], pos[2]);
            reroute_from_here(i, soldier, "off the stairs");
            return 0;
        }
    } else g_stairLevel[i] = 0;
    // not getting anywhere on it (blocked): hand back to the normal stuck handling for a while
    if (dist2(pos, g_stairMovedPos[i]) > 0.5f * 0.5f) { memcpy(g_stairMovedPos[i], pos, 12); g_stairMovedAt[i] = H->frame; }
    else if (H->frame - g_stairMovedAt[i] > 3 * 30 && fabsf(err) < 0.12f) {
        // AltBotMod V18: a step lip, a body or a teammate on the flight - one hop (and the teammate makes way) first
        if (!g_stairJumped[i]) {
            g_stairJumped[i] = 1; g_stairMovedAt[i] = H->frame; press_button(i, EIA_JUMP, 4); make_way_ask(i, soldier);
            bot_log("stairs: %s is held up on the flight at (%.1f, %.1f) - hops", name, pos[0], pos[2]);
            return 1;
        }
        g_stairOn[i] = 0; g_stairCheckAt[i] = H->frame + 6 * 30;
        bot_log("stairs: %s is blocked on the flight at (%.1f, %.1f) - normal navigation", name, pos[0], pos[2]);
        return 0;
    }
    g_progress[i].frame = H->frame; g_progress[i].x = pos[0]; g_progress[i].z = pos[2];   // (no stuck alarm while turning onto it)
    return 1;
}

static void steer_bot(int i, char *soldier)
{
    BotNav *nav = bot_nav(i);
    if (nav->mode == 0 || !soldier) return;
    if (H->frame < g_fenceUntil[i]) {   // knifing a fence
        float *fp = (float *)(soldier + OFF_SOLDIER_POSITION);
        H->botMove[i][0] = 0; H->botMove[i][2] = 0;
        H->botMove[i][1] = H->frame - g_fenceAt[i] < 20 ? g_fenceStrafe[i] : 0;   // the sidestep of a second try
        g_progress[i].frame = H->frame; g_progress[i].x = fp[0]; g_progress[i].z = fp[2]; g_progress[i].directTries = 0;
        return;
    }
    if (climb_ladder(i, soldier, nav)) return;
    if (route_hold_tick(i, soldier, nav)) return;   // AltBotMod V15: a recorded wait point
    if (g_crouchWalkUntil[i] && H->frame >= g_crouchWalkUntil[i]) { g_crouchWalkUntil[i] = 0; bot_stand(i); }   // through: up again
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION);
    float throttle = 0;
    Progress *pg = &g_progress[i];
    if (nav->mode == 4) {   // push: keep walking into the point (e.g. against a vehicle being repaired), no stuck logic
        nav->targetYaw = yaw_towards(nav->x - pos[0], nav->z - pos[2]);
        float y = *(float *)(soldier + OFF_SOLDIER_YAW), t = fmodf(nav->targetYaw, 6.2831853f);
        if (t > 0) t -= 6.2831853f;
        float up = t - y, down = up > 0 ? y - t + 6.2831853f : y - t, err = (up > 0 && up < down) ? up : -down;
        H->botMove[i][0] = fabsf(err) < 0.6f ? 1.0f : 0.0f;
        H->botMove[i][1] = 0;
        H->botMove[i][2] = fmaxf(-1.0f, fminf(1.0f, -err * 2.0f));
        pg->frame = H->frame;
        pg->x = pos[0]; pg->z = pos[2];
        return;
    }
    if (H->frame < g_vaultUntil[i]) {   // jumped: keep walking into it so the vault completes
        H->botMove[i][0] = 1;
        H->botMove[i][1] = 0;
        H->botMove[i][2] = 0;
        if (pg->unstickUntil > H->frame) pg->unstickUntil = g_vaultUntil[i] + 30;   // back off afterwards, if still needed
        return;
    }
    if (g_vaultUntil[i] && H->frame >= g_vaultUntil[i]) {   // vault over: if it got us across, no backing off
        float vx = pos[0] - g_vaultFrom[i][0], vz = pos[2] - g_vaultFrom[i][2];
        g_vaultUntil[i] = 0;
        if (vx * vx + vz * vz > 1.0f) {
            pg->unstickUntil = 0; pg->frame = H->frame; pg->x = pos[0]; pg->z = pos[2]; pg->directTries = 0;
            reroute_from_here(i, soldier, "after a vault");
        }
    }
    if (H->frame < pg->unstickUntil) {   // escape manoeuvre in progress
        H->botMove[i][0] = -1;
        H->botMove[i][1] = pg->strafe;
        H->botMove[i][2] = 0;
        return;
    }
    if (H->frame >= pg->directUntil && stair_tick(i, soldier, nav)) return;
    // Direct-walk fallback: face the final destination and walk straight at it, ignoring the grid.
    // Lets a human "pull" a bot out of a stuck spot (or into a building) by walking where it should go.
    if (H->frame < pg->directUntil) {
        {   // still not getting anywhere while walking straight: jump every 1.5 s


            if (H->frame - g_local_steer_bot_jat[i] >= 45) {
                float mx = pos[0] - g_local_steer_bot_jx[i], mz = pos[2] - g_local_steer_bot_jz[i];
                if (g_local_steer_bot_jat[i] && mx * mx + mz * mz < 0.3f * 0.3f) stuck_jump(i, soldier, "still blocked");
                g_local_steer_bot_jx[i] = pos[0]; g_local_steer_bot_jz[i] = pos[2]; g_local_steer_bot_jat[i] = H->frame;
            }
        }
        float gx = g_directTgt[i][0], gz = g_directTgt[i][1];   // AltBotMod V18.1: a route point in sight, not the far goal
        float dx = gx - pos[0], dz = gz - pos[2];
        if (dx * dx + dz * dz < 0.8f * 0.8f) { pg->directUntil = H->frame; pg->frame = H->frame; pg->x = pos[0]; pg->z = pos[2]; return; }   // there: back on the route
        nav->targetYaw = yaw_towards(dx, dz);
        float y = *(float *)(soldier + OFF_SOLDIER_YAW), t = fmodf(nav->targetYaw, 6.2831853f);
        if (t > 0) t -= 6.2831853f;
        float up = t - y, down = up > 0 ? y - t + 6.2831853f : y - t, err = (up > 0 && up < down) ? up : -down;
        H->botMove[i][0] = fabsf(err) < 0.8f ? 1.0f : 0.0f;
        H->botMove[i][1] = 0;
        H->botMove[i][2] = fmaxf(-1.0f, fminf(1.0f, -err * 2.0f));
        return;
    }
    if (nav->mode == 3) {
        follow_path(i, soldier, nav);
        if (nav->mode == 3) {
            nav->targetYaw = yaw_towards(nav->x - pos[0], nav->z - pos[2]);
        }
    } else if (nav->mode == 2) {
        float dx = nav->x - pos[0], dz = nav->z - pos[2];
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist < GOTO_ARRIVE_DIST) {
            nav->mode = 1;   // arrived: keep current heading, stop
            nav->targetYaw = *(float *)(soldier + OFF_SOLDIER_YAW);
            if (H->traceInput) bot_log("goto: %s arrived (%.1f, %.1f)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, pos[0], pos[2]);
        } else {
            nav->targetYaw = yaw_towards(dx, dz);
        }
    }
    // The engine keeps heading in (-2pi, 0]: positive EIAYaw lowers it and wraps below -2pi, negative raises it but
    // stops at 0. So put the target in the same range and only turn "up" when that does not need to cross 0.
    float yaw = *(float *)(soldier + OFF_SOLDIER_YAW);
    float t = fmodf(nav->targetYaw, 6.2831853f);
    if (t > 0) t -= 6.2831853f;
    float up = t - yaw;                                  // > 0: reachable by raising without crossing 0
    float down = up > 0 ? yaw - t + 6.2831853f : yaw - t;  // lowering always works (wraps)
    float err = (up > 0 && up < down) ? up : -down;      // signed: + raise, - lower
    float yawLevel = -err * (g_fineAim[i] && nav->mode == 1 ? 5.0f : 2.0f);
    if (yawLevel > 1) yawLevel = 1;
    if (yawLevel < -1) yawLevel = -1;
    if ((nav->mode == 2 || nav->mode == 3) && fabsf(err) < 0.6f) throttle = 1;   // only walk once roughly facing the target
    H->botMove[i][0] = throttle;
    H->botMove[i][1] = 0;
    H->botMove[i][2] = yawLevel;
    float mx = pos[0] - pg->x, mz = pos[2] - pg->z, moved2 = mx * mx + mz * mz;
    int fenceAct = 0;
    if ((throttle <= 0 && nav->mode != 2 && nav->mode != 3) || !pg->frame || moved2 > 1.0f) {   // (turning on the spot = no progress)
        if (moved2 > 1.0f) { pg->directTries = 0; g_waySkips[i] = 0; }
        pg->x = pos[0]; pg->z = pos[2]; pg->frame = H->frame;
    } else if (moved2 < 0.09f && H->frame - pg->frame > 30 && pg->directTries == 0 && (fenceAct = try_cut_fence(i, soldier))) {
        // a catalogued fence in the way: knifed (steer_bot holds still meanwhile), or one no knife gets through - go round
        if (fenceAct == 2) {
            pg->frame = H->frame;
            pg->unstickUntil = H->frame + 30;
            pg->strafe = (rand() & 1) ? 1.0f : -1.0f;
            on_bot_stuck(i, soldier);
        }
    } else if (moved2 < 0.09f && H->frame - pg->frame > 30 && pg->directTries == 0 && H->frame >= g_crouchWalkUntil[i] + 3 * 30 &&
               g_crouchAction >= 0 && crouch_gap(soldier)) {
        bot_crouch(i);
        g_crouchWalkUntil[i] = H->frame + 75;
        pg->frame = H->frame;
        g_crouchWalks++;
        static unsigned logged;
        if (logged++ < 40) bot_log("path: %s is blocked at head height - crouches through at (%.1f, %.1f)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, pos[0], pos[2]);
    } else if (moved2 < 0.09f && H->frame - pg->frame > 30 && pg->directTries == 0 && soldier_ahead(i, soldier) && g_waySkips[i] < 6) {
        // AltBotMod V18: a soldier in front (a teammate in a doorway / on a stairwell) - not the map: no straight walk into
        // the wall, no links blocked; it makes way (make_way_ask) and this bot waits a moment (at most 6 s, then as before)
        g_waySkips[i]++; pg->frame = H->frame; make_way_ask(i, soldier);
    } else if (moved2 < 0.09f && H->frame - pg->frame > 30 && pg->directTries == 0) {
        // not moving at all for 1 s: walk straight at the destination for 3 s
        g_waySkips[i] = 0;
        pg->directTries = 1;
        pg->directUntil = H->frame + 90;
        pg->frame = H->frame;
        direct_target_pick(i, soldier);
        bot_log("path: %s not moving, walking straight at %s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, g_directKind[i] ? "a route point in sight" : "its destination");
        stuck_jump(i, soldier, "blocked");
    } else if (H->frame - pg->frame > STUCK_SECONDS * 30) {
        // still no progress: back off, sidestep, block those links and re-plan
        pg->frame = H->frame;
        pg->directTries = 0;
        pg->unstickUntil = H->frame + 30;
        pg->strafe = (rand() & 1) ? 1.0f : -1.0f;
        stuck_jump(i, soldier, "last try before backing off");
        on_bot_stuck(i, soldier);
    }
    if (H->traceInput && H->frame % 15 == 0)
        bot_log("  steer %s mode=%d target=%.1f yaw=%.1f err=%.1f yawIn=%.2f thr=%.0f pos=(%.1f,%.1f)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME,
                nav->mode, nav->targetYaw * 57.2958f, *(float *)(soldier + OFF_SOLDIER_YAW) * 57.2958f, err * 57.2958f, yawLevel, throttle, pos[0], pos[2]);
}

// ---- Physics raycast ----
// bool GameWorld::rayCast(const char* name, int one, Vec4* from, Vec4* to, RayCastHit* hit, u32 flags, void* ignore), ret 0x1c
#define ADDR_GAME_WORLD            0x02b3ee30  // ServerGameWorld* global
#define ADDR_GAMEWORLD_RAYCAST     0x0085c130
// Flag bits measured on this build (they differ from later builds / Venice Unleashed): 0x08 skips terrain, 0x20 skips characters.
#define RAY_DONT_CHECK_TERRAIN     0x08
#define RAY_DONT_CHECK_CHARACTER   0x20
#define RAY_CHECK_DETAIL_MESH      0x01   // vegetation (palm trunks, trees) is detail mesh: invisible without this
#define RAY_FLAGS_NAV              RAY_DONT_CHECK_CHARACTER
typedef char (__attribute__((thiscall)) *RayCast_t)(void *world, const char *name, int one, const float *from, const float *to,
                                                    void *hit, unsigned flags, void *ignore);
typedef struct RayHit { float pos[4]; float normal[4]; unsigned char rest[0x80]; } RayHit;

// Returns 1 on hit; hitPos/hitNormal (may be NULL) receive xyz.
static unsigned g_rayFlags = RAY_FLAGS_NAV;
// The physics profiler keeps the raycast name pointer: it must outlive this DLL (a string literal here dangles after a
// hot reload and crashes the profiler). One heap copy per process, shared by every core generation (host->reserved+328).
static const char *ray_name(void)
{
    char **slot = (char **)(H->reserved + 328);
    if (!*slot) {
        *slot = HeapAlloc(GetProcessHeap(), 0, 8);
        if (!*slot) return "BotMod";
        memcpy(*slot, "BotMod", 7);
    }
    return *slot;
}

static int ray_cast(const float *from3, const float *to3, float *hitPos, float *hitNormal)
{
    void *world = *(void **)ADDR_GAME_WORLD;
    if (!world) return 0;
    float from[4] __attribute__((aligned(16))) = {from3[0], from3[1], from3[2], 0};
    float to[4] __attribute__((aligned(16))) = {to3[0], to3[1], to3[2], 0};
    RayHit hit __attribute__((aligned(16)));
    memset(&hit, 0, sizeof hit);
    *(int *)(hit.rest + 0x08) = -1;   // hit+0x28 / +0x2c initialised to -1 by the engine's own callers
    *(int *)(hit.rest + 0x0c) = -1;
    char r = ((RayCast_t)ADDR_GAMEWORLD_RAYCAST)(world, ray_name(), 1, from, to, &hit, g_rayFlags, NULL);
    if (r) {
        if (hitPos) memcpy(hitPos, hit.pos, 12);
        if (hitNormal) memcpy(hitNormal, hit.normal, 12);
        memcpy(g_lastHitRest, hit.rest, sizeof g_lastHitRest);
    }
    return r != 0;
}

static int ray_cast_no_terrain(const float *from3, const float *to3, float *hitPos, float *hitNormal)
{
    unsigned saved = g_rayFlags;
    g_rayFlags |= 0x08;   // skip terrain
    int r = ray_cast(from3, to3, hitPos, hitNormal);
    g_rayFlags = saved;
    return r;
}

// ---- Navigation grid + path following ----
#define NAV_RAYS_PER_FRAME   4000
#define NAV_MS_PER_FRAME     3.0
#define PATH_MAX_POINTS      128
#define WAYPOINT_REACHED     1.5f    // advance early only if the straight line from here to the next waypoint is clear
#define WAYPOINT_EXACT       0.5f
static NavGrid **nav_slot(void) { return (NavGrid **)(H->reserved + 24); }   // survives reloads
static int *nav_building(void) { return (int *)(H->reserved + 28); }

typedef struct BotPath { int count, next, partial; short link[PATH_MAX_POINTS]; unsigned char wpFlags[PATH_MAX_POINTS]; unsigned climbAt; unsigned char climbPhase; float pts[PATH_MAX_POINTS * 3]; float goalX, goalY, goalZ; float lastX, lastZ; unsigned lastProgressFrame; } BotPath;
static BotPath g_paths[MAX_BOTS];   // lost on reload (bots just stop following)
static void path_goal(int i, float *x, float *z) { *x = g_paths[i].goalX; *z = g_paths[i].goalZ; }
// AltBotMod V18.1: where the "not moving" straight walk heads. V18 and before always walked at the final destination;
// inside a building that is usually upstairs or behind a wall, so the bot stared at the wall for 3 s and then backed off
// (Office: 9500 straight walks in one evening). Now: the furthest of the next 8 route points in sight from chest height
// on about the bot's own floor (cutting the corner it was snagged on), else the next route point - the stairs, if the
// route goes up. The destination itself only when it is in plain sight on this floor (a player can still lead a bot out).
static void direct_target_pick(int i, char *soldier)
{
    BotNav *nav = bot_nav(i);
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION);
    float gx = nav->x, gz = nav->z;
    g_directKind[i] = 0;
    if (nav->mode == 3) {
        BotPath *p = &g_paths[i];
        float eye[3] = {pos[0], pos[1] + 1.2f, pos[2]}, goal[3] = {p->goalX, p->goalY + 1.2f, p->goalZ};
        if (fabsf(p->goalY - pos[1]) < 2.0f && !ray_cast(eye, goal, NULL, NULL)) { gx = p->goalX; gz = p->goalZ; }
        else if (p->next >= 0 && p->next < p->count) {
            gx = p->pts[p->next * 3]; gz = p->pts[p->next * 3 + 2]; g_directKind[i] = 1;
            for (int k = p->next + 1; k < p->count && k <= p->next + 8; k++) {
                const float *w = &p->pts[k * 3];
                float to[3] = {w[0], w[1] + 1.2f, w[2]};
                if (fabsf(w[1] - pos[1]) > 2.0f || ray_cast(eye, to, NULL, NULL)) break;
                gx = w[0]; gz = w[2];
            }
        }
    }
    g_directTgt[i][0] = gx; g_directTgt[i][1] = gz;
}
static int plan_path(int i, char *soldier, float tx, float ty, float tz);
// After a vault, a flight of stairs / an escalator or a ladder the bot is somewhere the old route did not expect (the
// vault landing past the waypoint, the top of an escalator beside the rails): plan again from where it really is to the
// same goal - the new first leg goes straight to the way on (the old one turned it into the nearest wall, or back over
// what it just vaulted).
static void reroute_from_here(int i, char *soldier, const char *why)
{
    BotNav *nav = bot_nav(i);
    BotPath *p = &g_paths[i];
    if (nav->mode != 3 || !soldier || (p->goalX == 0 && p->goalZ == 0)) return;
    float gx = p->goalX, gy = p->goalY, gz = p->goalZ;
    if (plan_path(i, soldier, gx, gy, gz)) {
        nav->mode = 3;
        bot_log("path: %s re-routes from where it is now (%s)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, why);
    }
}

static void nav_file(char *out, int n, const char *level)
{
    char dir[MAX_PATH];
    GetModuleFileNameA(NULL, dir, sizeof dir);
    *strrchr(dir, '\\') = 0;
    snprintf(out, n, "%s\\BotMod_nav_%s.bin", dir, level);
}

// Learned link removals (nav_repair_around) are written back into the map's nav file, so every server benefits.
static int g_navDirty;
static void nav_save_if_dirty(void)
{
    NavGrid *g = *nav_slot();
    if (!g_navDirty || !g || g->phase != 2) return;
    char path[MAX_PATH + 64];
    nav_file(path, sizeof path, g->level);
    bot_log("nav: saving learned repairs to %s (%s)", path, nav_save(g, path) ? "ok" : "FAILED");
    g_navDirty = 0;
}

static void ladders_link(void);
static int g_navDirty;
// After a grid is loaded: the strict staircase re-check, a few thousand rays per frame; saved with the map if it changed.
static void nav_stairfix_tick(void)
{
    static NavGrid *done;
    NavGrid *g = *nav_slot();
    if (!g || g->phase != 2 || g == done || *nav_building()) return;
    int removed = 0;
    if (nav_fix_stairs_step(g, ray_cast, 1500, &removed)) {
        done = g;
        int restored = nav_stairs_restored(), rails = nav_rails_removed(), added = nav_stairs_added();
        bot_log("nav: stair re-check on %s done - %d false staircase link(s) (wall faces) removed, %d drop(s) (escalator tops ...) restored, "
                "%d staircase(s) up added, %d rail / wall top(s) no longer walkable", g->level, removed, restored, added, rails);
        if (removed || restored || rails || added) g_navDirty = 1;
    }
}
static NavGrid *g_navExpansion;
static void nav_tick(void)
{
    nav_stairfix_tick();
    NavGrid *g = g_navExpansion ? g_navExpansion : *nav_slot();
    if (!g || (!g_navExpansion && !*nav_building())) return;
    int wasPhase = g->phase;
    nav_set_terrain_test(ray_cast_no_terrain);
    nav_set_hit_classifier(nav_hit_class);
    if (nav_build_step(g, ray_cast, NAV_RAYS_PER_FRAME, NAV_MS_PER_FRAME)) {
        if(g_navExpansion){
            NavGrid *old=*nav_slot();*nav_slot()=g;g_navExpansion=NULL;
            nav_free(old); // also cancels requests that referenced the old grid
            bot_log("navigation: expanded grid ready; switching active routes now");
        }
        *nav_building() = 0;
        int walk = 0, edges = 0, water = 0, fences = 0, doors = 0;
        for (int i = 0; i < g->w * g->h * NAV_LAYERS; i++) {
            walk += g->flags[i] & NAV_WALKABLE;
            water += (g->flags[i] & NAV_WATER) != 0;
            for (int d = 0; d < 8; d++) { edges += (g->edges[i] >> d) & 1; if (d & 1) doors += (g->fences[i] >> d) & 1; else fences += (g->fences[i] >> d) & 1; }
        }
        bot_log("navbuild: %d water node(s), %d knifeable fence crossing(s), %d doorway(s)", water, fences / 2, doors / 2);
        char path[MAX_PATH + 64];
        nav_file(path, sizeof path, g->level);
        bot_log("navbuild: done %s %dx%d cells, %d walkable, %d edges, %u rays in %.0f ms -> %s (%s)", g->level, g->w, g->h, walk,
                edges / 2, g->raysCast, g->buildMs, path, nav_save(g, path) ? "saved" : "SAVE FAILED");
        ladders_link();
    } else if (g->phase != wasPhase || H->frame % 300 == 0) {
        int n = g->w * g->h;
        bot_log("navbuild: phase %d %.0f%% (%u rays, %.0f ms)", g->phase, 100.0 * g->cursor / (g->phase ? n * 4.0 : n), g->raysCast, g->buildMs);
    }
}

// ty = height of the goal (picks the right storey / pier deck vs. the water or seabed under it)
static int plan_path_real(int i, char *soldier, float tx, float ty, float tz);
static double perf_now(void);
static void perf_add(int k, double ms);
static unsigned g_pathPlans;
// Route plans per tick for the optional planners (getting to a vehicle, cover / firing spots): each may try many
// targets, and on a slow CPU a burst of them stalled the server (MP_017: 4,100-18,600 plans per 30 s on the AM1).
// All route requests share this allowance. A single A* call also observes the remaining time.
#define PLAN_BUDGET_PER_TICK 3
static unsigned g_planFrame;
static int g_plansThisFrame;
static unsigned g_planBudgetHits;
static double g_planMsThisFrame;
static int g_lastPlanDeferred;
static unsigned g_routeJoins;   // AltBotMod V25: grid legs onto a Fun-Bots road
static unsigned g_replanStorms;   // AltBotMod V26
static int g_consolePlan;
#define PLAN_MS_PER_TICK 5.0   // route-planning time per tick before the rest waits (rubber banding: 400 ms thinks)
#define PLAN_MS_BUSY     6.0   // (V31.1: 6 ms - was 8) Caspian V3: with a queue of 5+ routes (V2 averaged 9 ms of the 33 ms tick in total)
static int plan_budget_left(void)
{
    if (g_planFrame != H->frame) { g_planFrame = H->frame; g_plansThisFrame = 0; g_planMsThisFrame = 0; }
    if (g_plansThisFrame < PLAN_BUDGET_PER_TICK) return 1;
    g_planBudgetHits++;
    return 0;
}
static int budgeted_nav_path(int owner,const NavGrid *g,NavRayFn ray,float sx,float sy,float sz,float tx,float ty,float tz,float *out,int maxPts)
{
    g_lastPlanDeferred = 0;
    if (!g_consolePlan && !plan_budget_left()) { g_lastPlanDeferred = 1; return 0; }
    if (g_planFrame != H->frame) { g_planFrame = H->frame; g_plansThisFrame = 0; g_planMsThisFrame = 0; }
    g_plansThisFrame++;
    nav_set_path_owner(g_consolePlan?-1:owner);
    double a = perf_now();
    int r = nav_find_path(g,ray,sx,sy,sz,tx,ty,tz,out,maxPts);
    g_lastPlanDeferred = !r && nav_last_deferred();
    nav_set_path_owner(-1);
    g_planMsThisFrame += perf_now() - a;
    perf_add(7, perf_now() - a);   // PF_PATH
    if(!g_lastPlanDeferred)g_pathPlans++;else g_planBudgetHits++;
    return r;
}
static int plan_path(int i,char *soldier,float tx,float ty,float tz)
{
    g_lastPlanDeferred=0;
    return plan_path_real(i,soldier,tx,ty,tz);
}
static int trace_plan(int i, char *soldier, float tx, float ty, float tz);
static int trace_plan_ex(int i, char *soldier, float tx, float ty, float tz, int mode);
static int trace_vehicle_route(int boat, int flag, const float *flagPos, const float *vp, float *pts, int maxPts, int *partial, float *lengthOut, int alt);
static void trace_stats_tick(void);
static int trace_loaded(void);
// Caspian V3: a straight walk checked with live physics, no grid needed (some aircraft pads and HQs are outside
// the grid). Body rays at knee and chest height (nav_segment_clear), then the floor every 2.5 m: no step up or
// down over 1.3 m between samples, no water, and it must end on the target's floor. At most 8 checks a tick (~25 rays each).
// Returns 1 clear, 0 not walkable, -1 no check left this tick.
static unsigned g_walkCheckFrame, g_directWalks;
static int g_walkChecks;
static int walk_clear(const float *from, const float *to, float maxLen)
{
    if (!from || !to || !isfinite(from[0]) || !isfinite(from[1]) || !isfinite(from[2]) ||
        !isfinite(to[0]) || !isfinite(to[1]) || !isfinite(to[2])) return 0;
    float dx = to[0] - from[0], dz = to[2] - from[2], len = sqrtf(dx * dx + dz * dz);
    if (len > maxLen || fabsf(to[1] - from[1]) > len * 0.6f + 1.5f) return 0;
    if (len < 0.5f) return 1;
    if (g_walkCheckFrame != H->frame) { g_walkCheckFrame = H->frame; g_walkChecks = 0; }
    if (++g_walkChecks > 8) return -1;   // not this tick
    unsigned saved = g_rayFlags; g_rayFlags = RAY_DONT_CHECK_CHARACTER | RAY_CHECK_DETAIL_MESH;
    int ok = nav_segment_clear(ray_cast, from, to);
    int steps = (int)ceilf(len / 2.5f); float prev = from[1], hit[3];
    for (int k = 1; k <= steps && ok; k++) {
        float t = (float)k / steps, a[3] = {from[0] + dx * t, prev + 2.0f, from[2] + dz * t}, b[3] = {a[0], prev - 2.5f, a[2]};
        if (!ray_cast(a, b, hit, NULL) || nav_hit_class() == NAV_HIT_WATER || fabsf(hit[1] - prev) > 1.3f) ok = 0;
        else prev = hit[1];
    }
    g_rayFlags = saved;
    return ok && fabsf(prev - to[1]) < 2.0f;
}
typedef struct DirectUse { unsigned at; float from[3], to[3]; } DirectUse;
static DirectUse g_directUse[MAX_BOTS];
// The same straight walk from (almost) the same spot within 25 s did not get the bot there: use the grid.
static int direct_retry(int i, const float *pos, const float *to)
{
    DirectUse *u = &g_directUse[i];
    return u->at && H->frame - u->at < 25 * 30 && dist2(u->from, pos) < 6 * 6 && dist2(u->to, to) < 4 * 4;
}
static int direct_path(int i, char *soldier, const float *to)
{
    BotPath *p = &g_paths[i];
    DirectUse *u = &g_directUse[i];
    nav_cancel_path(i);   // a grid request queued earlier for this bot is not needed any more
    u->at = H->frame ? H->frame : 1; memcpy(u->from, (float *)(soldier + OFF_SOLDIER_POSITION), 12); memcpy(u->to, to, 12);
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION);
    memset(p, 0, sizeof *p);
    memcpy(p->pts, to, 12); p->link[0] = -1; p->count = 1;
    p->goalX = to[0]; p->goalY = to[1]; p->goalZ = to[2];
    p->lastX = pos[0]; p->lastZ = pos[2]; p->lastProgressFrame = H->frame;
    g_directWalks++;
    return 1;
}
#define DIRECT_WALK_MAX 45.0f
// AltBotMod V8: before a map's navigation grid exists (a map played for the first time: the survey takes minutes)
// bots walked nowhere - V7 on XP5_002 left most of both teams standing at their HQs. Now they walk towards the goal in
// checked straight legs of up to 40 m (walk_clear: body rays and the floor every 2.5 m, no water, no drops), fanning
// out up to 63 deg either side of the direct line when it is blocked. The leg is a partial route: at its end the bot
// plans again (a Fun-Bots route first, if it is near one by then). 3 candidates per call, rotating.
#define STEP_WALK_MAX 40.0f
static unsigned char g_stepCandidate[MAX_BOTS];
static unsigned g_stepWalks, g_gridFallbackWalks;
static int step_walk(int i, char *soldier, const float *goal)
{
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION);
    float dx = goal[0] - pos[0], dz = goal[2] - pos[2], len = sqrtf(dx * dx + dz * dz);
    if (len < 2.0f || !isfinite(len)) return 0;
    static const float kOff[7] = {0, 0.35f, -0.35f, 0.7f, -0.7f, 1.1f, -1.1f};
    static const float kLen[3] = {STEP_WALK_MAX, 24.0f, 12.0f};
    float heading = atan2f(dx, dz);
    for (int tries = 0; tries < 3; tries++) {
        int c = g_stepCandidate[i] % 21;
        float l = fminf(kLen[c / 7], len), a = heading + kOff[c % 7];
        float pt[3] = {pos[0] + sinf(a) * l, pos[1], pos[2] + cosf(a) * l}, hit[3];
        if (l >= len - 0.5f && c % 7 == 0) pt[1] = goal[1];
        else {   // the floor there
            float top[3] = {pt[0], pos[1] + l * 0.6f + 2.0f, pt[2]}, bot[3] = {pt[0], pos[1] - l * 0.6f - 3.0f, pt[2]};
            unsigned saved = g_rayFlags; g_rayFlags = RAY_DONT_CHECK_CHARACTER;
            int got = ray_cast(top, bot, hit, NULL) && nav_hit_class() != NAV_HIT_WATER;
            g_rayFlags = saved;
            if (!got) { if (++g_stepCandidate[i] >= 21) { g_stepCandidate[i] = 0; return 0; } continue; }
            pt[1] = hit[1];
        }
        int r = walk_clear(pos, pt, STEP_WALK_MAX + 1.0f);
        if (r < 0) return -1;
        if (r == 0) { if (++g_stepCandidate[i] >= 21) { g_stepCandidate[i] = 0; return 0; } continue; }
        g_stepCandidate[i] = 0;
        direct_path(i, soldier, pt);
        BotPath *p = &g_paths[i];
        if (l < len - 0.5f) { p->partial = 1; p->goalX = goal[0]; p->goalY = goal[1]; p->goalZ = goal[2]; }
        g_stepWalks++;
        return 1;
    }
    return -1;   // more candidates next time
}
static int plan_path_real(int i, char *soldier, float tx, float ty, float tz)
{
    g_zonePathAt[i] = 0;
    {   // AltBotMod V34: at an objective, Fun-Bots' walking mesh of it first
        int zr = zone_plan(i, soldier, tx, ty, tz);
        if (zr > 0) return zr;
        if (g_lastPlanDeferred) return 0;
    }
    {   // Caspian V2: a hand-walked Fun-Bots route to a flag when the level has one (works before the grid is ready)
        // (V3: to any point near the recorded network)
        int traced = trace_plan(i, soldier, tx, ty, tz);
        if (traced) return traced;
        if (g_lastPlanDeferred) return 0;
    }
    {   // Caspian V3: a short move that is a clear straight walk needs no queued grid search. In V2 the one shared
        // A* finished ~1 route a second for 60 bots, and bots stood still waiting for theirs.
        float *pos = (float *)(soldier + OFF_SOLDIER_POSITION), to[3] = {tx, ty, tz};
        if (!isnan(ty) && dist2(pos, to) < DIRECT_WALK_MAX * DIRECT_WALK_MAX && !direct_retry(i, pos, to) && walk_clear(pos, to, DIRECT_WALK_MAX) > 0)
            return direct_path(i, soldier, to);
    }
    NavGrid *g = *nav_slot();
    if (!g || g->phase != 2) {
        static unsigned warnedAt;
        if (H->frame - warnedAt > 300) { warnedAt = H->frame; bot_log("path: navigation grid not ready yet (building / navload) - checked straight legs meanwhile"); }
        float to[3] = {tx, ty, tz};
        int s = isnan(ty) ? 0 : step_walk(i, soldier, to);
        if (s > 0) return s;
        if (s < 0) { g_lastPlanDeferred = 1; return 0; }   // no walk check left this tick: try again shortly
        return 0;
    }
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION);
    BotPath *p = &g_paths[i];
    int count = budgeted_nav_path(i,g, ray_cast, pos[0], pos[1], pos[2], tx, ty, tz, p->pts, PATH_MAX_POINTS);
    if (!count && g_lastPlanDeferred) return 0;
    if (!count && !isnan(ty)) {   // AltBotMod V9: no grid route - a checked straight leg towards it rather than standing still
        float to[3] = {tx, ty, tz};
        if (step_walk(i, soldier, to) > 0) { g_gridFallbackWalks++; return g_paths[i].count; }
    }
    p->count = count;
    p->partial = nav_last_partial();
    for (int k = 0; k < p->count; k++) p->link[k] = (short)nav_path_link(k);
    memset(p->wpFlags, 0, sizeof p->wpFlags);
    p->climbAt = 0;
    p->climbPhase = 0;
    p->next = 0;
    p->goalX = tx;
    p->goalY = ty;
    p->goalZ = tz;
    p->lastX = pos[0];
    p->lastZ = pos[2];
    p->lastProgressFrame = H->frame;
    return p->count;
}

// Mode 3: follow the planned waypoints (sets nav->x/z to the current waypoint, then behaves like goto).
static unsigned g_wpNearAt[MAX_BOTS];
static int g_wpNearIdx[MAX_BOTS];
static void trace_jump_tick(int i, char *soldier);
// AltBotMod V15 (route_actions_native.inc): recorded waits and crouch sections on Fun-Bots routes
static void route_waypoint_tick(int i, char *soldier, BotPath *p);
static int route_crouched(int i);
static void route_wait_reset(int i);
static void follow_path(int i, char *soldier, BotNav *nav)
{
    BotPath *p = &g_paths[i];
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION);
    if (p->next < p->count && p->link[p->next] < 0) {
        float dx = p->pts[p->next * 3] - pos[0], dz = p->pts[p->next * 3 + 2] - pos[2];
        if (dx * dx + dz * dz < 1.0f) {
            if (g_wpNearIdx[i] != p->next || !g_wpNearAt[i]) { g_wpNearIdx[i] = p->next; g_wpNearAt[i] = H->frame ? H->frame : 1; }
            else if (H->frame - g_wpNearAt[i] > 30 && p->next + 1 < p->count) {
                p->next++;
                g_wpNearAt[i] = 0;
                if (H->traceInput) bot_log("path: %s hovered at a waypoint - moving on", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME);
            }
        } else g_wpNearAt[i] = 0;
    }
    while (p->next < p->count) {
        if (p->link[p->next] >= 0) break;   // a ladder top: climb_ladder() takes it from here
        float dx = p->pts[p->next * 3] - pos[0], dz = p->pts[p->next * 3 + 2] - pos[2], d2 = dx * dx + dz * dz;
        if (d2 > WAYPOINT_REACHED * WAYPOINT_REACHED) break;
        if (p->next + 1 < p->count && p->link[p->next + 1] >= 0 && d2 > 0.4f * 0.4f) break;   // the ladder foot: exactly
        int last = p->next + 1 >= p->count;
        if (d2 > WAYPOINT_EXACT * WAYPOINT_EXACT && !last) {
            float feet[3] = {pos[0], pos[1], pos[2]};
            if (!nav_segment_clear(ray_cast, feet, &p->pts[(p->next + 1) * 3])) break;   // keep walking to the corner
        }
        p->next++;
    }
    if (p->next >= p->count) {
        if (p->partial) {
            // A planning slice or the waypoint buffer ended before the goal.
            // Continue from here; reaching this intermediate point is not arrival.
            if (!plan_budget_left()) { memset(H->botMove[i],0,sizeof H->botMove[i]); return; }
            float gx=p->goalX, gy=p->goalY, gz=p->goalZ;
            if (plan_path(i,soldier,gx,gy,gz)) {
                nav->x=p->pts[0]; nav->z=p->pts[2]; return;
            }
            if (g_lastPlanDeferred) { memset(H->botMove[i],0,sizeof H->botMove[i]); return; }
            p->partial=0;
        }
        route_waypoint_tick(i, soldier, p);   // (stands up after a recorded crouch section)
        nav->mode = 1;
        nav->targetYaw = *(float *)(soldier + OFF_SOLDIER_YAW);
        bot_log("path: %s arrived (%.1f, %.1f)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, pos[0], pos[2]);
        return;
    }
    nav->x = p->pts[p->next * 3];
    nav->z = p->pts[p->next * 3 + 2];
    route_waypoint_tick(i, soldier, p);   // AltBotMod V15
    trace_jump_tick(i, soldier);
}

// ---- Ladders (up only - nobody climbs down in BF3: bots walk off the edge / pull the chute) ----
// The engine does the climbing: a soldier on the ladder's centre line, facing it and walking forward goes up and steps
// off at the top. Path links (nav_add_link) end at the ladder top; the bot lines up 0.9 m out, faces the ladder, holds
// forward while strafing onto the centre line, and carries on once it is up.
#define LADDER_STAND_OUT       0.9f
#define LADDER_CLIMB_FRAMES    (12 * 30)
typedef struct LadderLink { int ladder; float n[3], groundY; } LadderLink;   // groundY: the floor the climb starts from   // n: horizontal, from the ladder out to the climbing side
static LadderLink g_ladderLink[256];
static float wrap_err(float targetYaw, float yaw)
{
    float t = fmodf(targetYaw, 6.2831853f);
    if (t > 0) t -= 6.2831853f;
    float up = t - yaw, down = up > 0 ? yaw - t + 6.2831853f : yaw - t;
    return (up > 0 && up < down) ? up : -down;
}
static const float *ladder_foot(int k);
static float ladder_top_y(int k);
// Returns 1 while it has the controls.
static int climb_ladder(int i, char *soldier, BotNav *nav)
{
    BotPath *p = &g_paths[i];
    if (nav->mode != 3 || p->next >= p->count || p->link[p->next] < 0) return 0;
    int id = p->link[p->next];
    LadderLink *ll = &g_ladderLink[id];
    const float *foot = ladder_foot(ll->ladder);
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION), yaw = *(float *)(soldier + OFF_SOLDIER_YAW);
    float topY = ladder_top_y(ll->ladder);
    if (pos[1] > topY - 0.6f) {   // up: walk on to the exit and beyond
        bot_log("ladder: %s is up (%.1f m climbed)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, pos[1] - foot[1]);
        p->link[p->next] = -1;
        p->climbAt = 0;
        p->climbPhase = 0;
        reroute_from_here(i, soldier, "up the ladder");
        return 0;
    }
    if (!p->climbAt) p->climbAt = H->frame;
    if (H->frame - p->climbAt > LADDER_CLIMB_FRAMES) {   // could not get on / up: forget this ladder for the round
        bot_log("ladder: %s could not climb the ladder at (%.1f, %.1f) - dropping it", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, foot[0], foot[2]);
        nav_disable_link(id);
        p->climbAt = 0;
        p->climbPhase = 0;
        if (!plan_path(i, soldier, p->goalX, p->goalY, p->goalZ)) nav->mode = 1;
        return 0;
    }
    float stand[3] = {foot[0] + ll->n[0] * LADDER_STAND_OUT, foot[1], foot[2] + ll->n[2] * LADDER_STAND_OUT};
    float dx = stand[0] - pos[0], dz = stand[2] - pos[2], d = sqrtf(dx * dx + dz * dz);
    float faceYaw = yaw_towards(-ll->n[0], -ll->n[2]);
    // sideways error from the centre line, along the bot's left when facing the ladder (+strafe moves left)
    float fx = -ll->n[0], fz = -ll->n[2], lx = -fz, lz = fx;
    float side = (pos[0] - foot[0]) * lx + (pos[2] - foot[2]) * lz;
    int onLadder = pos[1] > ll->groundY + 0.4f;
    Progress *pg = &g_progress[i];
    pg->frame = H->frame; pg->x = pos[0]; pg->z = pos[2]; pg->directTries = 0;   // no stuck handling meanwhile
    if (!onLadder && !p->climbPhase && d > 2.5f && p->next > 0) {   // not at the foot yet: walk the path to it first
        p->next--;
        p->climbAt = 0;
        return 0;
    }
    if (!onLadder && !p->climbPhase && d < 0.3f) p->climbPhase = 1;
    if (!onLadder && !p->climbPhase) {   // walking up to the stand point
        nav->targetYaw = yaw_towards(dx, dz);
        float err = wrap_err(nav->targetYaw, yaw);
        H->botMove[i][0] = fabsf(err) < 0.5f ? (d < 1.5f ? 0.4f : 1.0f) : 0.0f;
        H->botMove[i][1] = 0;
        H->botMove[i][2] = fmaxf(-1.0f, fminf(1.0f, -err * 2.0f));
        return 1;
    }
    nav->targetYaw = faceYaw;
    float err = wrap_err(faceYaw, yaw);
    H->botMove[i][2] = fmaxf(-1.0f, fminf(1.0f, -err * 2.0f));
    if (onLadder) { H->botMove[i][0] = 1.0f; H->botMove[i][1] = 0; return 1; }
    float strafe = fmaxf(-0.6f, fminf(0.6f, -side * 2.5f));
    H->botMove[i][1] = fabsf(side) > 0.08f ? strafe : 0.0f;
    H->botMove[i][0] = fabsf(err) < 0.25f && fabsf(side) < 0.25f ? 1.0f : 0.0f;
    return 1;
}

// ---- Conquest flags ----
#define ADDR_CAPTURE_POINT_VTABLE  0x02696660  // ServerCapturePointEntity
#define OFF_CP_CONTROL_TEAM        0xb0        // team currently inside / controlling the capture area (0 = none)
#define OFF_CP_OWNER_TEAM          0xb4        // owning team (0 = neutral)
#define OFF_CP_OWNER_OBJECT        0x10        // shared with the spawn entities of the same flag/HQ blueprint
#define OFF_CP_DATA                0x14        // CapturePointEntityData*
#define OFF_CPDATA_TAKEABLE        0xb8        // byte: 1 on conquest flags, 0 on HQ bases
#define OFF_CP_POSITION            0x120       // Vec3
#define MAX_FLAGS                  16
static char *g_flags[MAX_FLAGS];
static int g_flagCount;
static void *g_flagSpawn[MAX_FLAGS][3];   // each flag has one CharacterSpawnEntity per team (data +0x08 byte = team)
static float g_hqPos[3][3];

// Scans committed memory for objects with the given vtable at +0 (16-byte aligned). Regions are copied with
// ReadProcessMemory so guard pages / concurrently freed memory cannot fault the server.
// align: objects are 8-byte aligned except a few components (4); a finer step also finds more false matches, and
// the callers cap the result count, so use 4 only where needed.
static int scan_objects_aligned(unsigned vtable, char **out, int max, int align);
static int scan_objects(unsigned vtable, char **out, int max) { return scan_objects_aligned(vtable, out, max, 8); }
static int scan_objects_aligned(unsigned vtable, char **out, int max, int align)
{
    LONG scanEpoch=g_roundEpoch;
    enum { CHUNK = 1 << 16 };
    unsigned buf[CHUNK / 4];   // per call: scans also run on worker threads
    int count = 0;
    MEMORY_BASIC_INFORMATION mbi;
    for (unsigned char *a = (unsigned char *)0x10000; (unsigned)a < 0xffff0000u && count < max && !(g_abortScans || scanEpoch!=g_roundEpoch);
         a = (unsigned char *)mbi.BaseAddress + mbi.RegionSize) {
        if (!VirtualQuery(a, &mbi, sizeof mbi) || !mbi.RegionSize) break;
        DWORD prot = mbi.Protect & 0xff;
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || (mbi.Protect & (PAGE_GUARD | PAGE_NOCACHE)) ||
            (prot != PAGE_READWRITE && prot != PAGE_EXECUTE_READWRITE)) continue;
        for (SIZE_T off = 0; off < mbi.RegionSize && count < max; off += CHUNK) {
            SIZE_T n = mbi.RegionSize - off < CHUNK ? mbi.RegionSize - off : CHUNK, got = 0;
            char *base = (char *)mbi.BaseAddress + off;
            if (base + n > (char *)buf && base < (char *)buf + sizeof buf) continue;   // (our own read buffer: copies, not objects)
            if (!ReadProcessMemory(GetCurrentProcess(), base, buf, n, &got)) continue;
            for (SIZE_T k = 0; k + 4 <= got && count < max; k += (SIZE_T)align)
                if (buf[k / 4] == vtable) out[count++] = base + k;
        }
    }
    return count;
}

// One pass over memory for several vtables at once (each with its own alignment): the scan worker used to walk all of
// memory once per type.
typedef struct ScanType { unsigned vtable; int align; char **out; int max; int count; } ScanType;
static void scan_objects_multi(ScanType *t, int nt)
{
    LONG scanEpoch=g_roundEpoch;
    enum { CHUNK = 1 << 16 };
    static unsigned buf[CHUNK / 4];   // only the scan worker calls this
    for (int k = 0; k < nt; k++) t[k].count = 0;
    MEMORY_BASIC_INFORMATION mbi;
    for (unsigned char *a = (unsigned char *)0x10000; (unsigned)a < 0xffff0000u && !(g_abortScans || scanEpoch!=g_roundEpoch);
         a = (unsigned char *)mbi.BaseAddress + mbi.RegionSize) {
        if (!VirtualQuery(a, &mbi, sizeof mbi) || !mbi.RegionSize) break;
        DWORD prot = mbi.Protect & 0xff;
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || (mbi.Protect & (PAGE_GUARD | PAGE_NOCACHE)) ||
            (prot != PAGE_READWRITE && prot != PAGE_EXECUTE_READWRITE)) continue;
        for (SIZE_T off = 0; off < mbi.RegionSize; off += CHUNK) {
            SIZE_T n = mbi.RegionSize - off < CHUNK ? mbi.RegionSize - off : CHUNK, got = 0;
            char *base = (char *)mbi.BaseAddress + off;
            if (base + n > (char *)buf && base < (char *)buf + sizeof buf) continue;   // (our own read buffer)
            if (!ReadProcessMemory(GetCurrentProcess(), base, buf, n, &got)) continue;
            for (SIZE_T w = 0; w + 1 <= got / 4; w++) {
                unsigned v = buf[w];
                if (v < 0x02400000u || v > 0x02700000u) continue;   // every type we look for has its vtable in this range
                for (int k = 0; k < nt; k++)
                    if (v == t[k].vtable && t[k].count < t[k].max && ((w * 4) % t[k].align) == 0) t[k].out[t[k].count++] = base + w * 4;
            }
        }
    }
}

// Takeable flags = capture points whose data marks them takeable (HQ bases are not), ignoring stale objects at the origin.
static void find_flags(void)
{
    char *cps[MAX_FLAGS * 2];
    int ncp = scan_objects(ADDR_CAPTURE_POINT_VTABLE, cps, MAX_FLAGS * 2);
    g_flagCount = 0;
    for (int i = 0; i < ncp && g_flagCount < MAX_FLAGS; i++) {
        char *data = *(char **)(cps[i] + OFF_CP_DATA);
        float *pos = (float *)(cps[i] + OFF_CP_POSITION);
        if (!data || IsBadReadPtr(data + OFF_CPDATA_TAKEABLE, 1) || !data[OFF_CPDATA_TAKEABLE]) continue;
        if (pos[0] == 0 && pos[1] == 0 && pos[2] == 0) continue;
        g_flags[g_flagCount++] = cps[i];
    }
    char *spawns[128];
    int nsp = scan_objects(0x025bbbc0, spawns, 128), linked = 0;
    memset(g_flagSpawn, 0, sizeof g_flagSpawn);
    for (int f = 0; f < g_flagCount; f++)
        for (int s = 0; s < nsp; s++) {
            if (*(void **)(spawns[s] + OFF_CP_OWNER_OBJECT) != *(void **)(g_flags[f] + OFF_CP_OWNER_OBJECT)) continue;
            unsigned char *data = *(unsigned char **)(spawns[s] + 0x14);
            if (!data || IsBadReadPtr(data + 8, 1) || (data[8] != 1 && data[8] != 2)) continue;
            g_flagSpawn[f][data[8]] = spawns[s];
            linked++;
        }
    bot_log("flags: %d capture point(s), %d takeable flag(s), %d flag spawn(s)", ncp, g_flagCount, linked);
}
// AltBotMod V16: safe for any index. V15 crashed Rush (server 3, 2026-10-11 17:55: ACCESS_VIOLATION reading 0x120 in
// flag_pressure): fleet / ground-job code passes an objective index (an M-COM) where the Conquest flag table is empty.
static int flag_owner(int f) { return f >= 0 && f < g_flagCount && f < MAX_FLAGS && g_flags[f] ? *(int *)(g_flags[f] + OFF_CP_OWNER_TEAM) : 0; }
static float *flag_pos(int f)
{
    static float none[3];
    if (f >= 0 && f < g_flagCount && f < MAX_FLAGS && g_flags[f]) return (float *)(g_flags[f] + OFF_CP_POSITION);
    if (!mode_is_cq()) { const float *o = objective_pos(f); if (o) return (float *)o; }
    return none;
}

// ---- Bot brain: respawn at HQ, capture the nearest flag the team does not own, repeat ----
#define RESPAWN_DELAY_FRAMES  (5 * 30)
#define FLAG_SPREAD           4.0f      // bots aim for a random spot this close to the flag centre
enum { TASK_NONE, TASK_FOLLOW, TASK_DEFEND, TASK_SUPPLY, TASK_REPAIR, TASK_REVIVE, TASK_COVER, TASK_ROCKET, TASK_FIGHT };
static const char *kTaskNames[] = {"objectives", "follow", "defend", "supply", "repair", "revive", "cover", "rocket", "fight"};
static void bot_say_later(int i, int type, unsigned delayFrames);
// ---- Downed teammates: a soldier that disappears from its player is a body that can be revived for ~10 s ----
#define DOWNED_WINDOW_FRAMES   (10 * 30)
#define REVIVE_RANGE           45.0f     // ~10 s at a run (V11: 35 m)
#define REVIVE_REACH           1.3f      // stand this close to the body
#define REVIVE_PITCH           -1.2f     // look down at the body (~70 degrees)
#define BOT_REVIVE_WAIT_FRAMES (10 * 30) // a downed bot with a medic coming waits this long before redeploying
typedef struct Downed { char *player; float pos[3]; unsigned at; int team; int medic; char *body; } Downed;   // medic: bot index + 1; body: the dead soldier
static Downed g_downed[64];
static char *g_downLastSoldier[64];
static char *g_downLastPlayer[64];
static float g_downLastPos[64][3];
static int g_downLastInVehicle[64];
static char *g_downLastBody[64];

// Where a dead soldier's ragdoll lies: its physics keeps moving after death while the soldier transform freezes.
// [body+0x30]+0xc0 and [body+0x4d4]+0x90 both follow the ragdoll; the frozen transform is the last resort.
static int ragdoll_pos(char *body, float *out)
{
    float *tr = (float *)(body + OFF_SOLDIER_SPAWN_POSITION);
    static const int kRagdoll[2][2] = {{0x30, 0xc0}, {0x4d4, 0x90}};
    for (int k = 0; k < 2; k++) {
        char *q = *(char **)(body + kRagdoll[k][0]);
        if (!q || IsBadReadPtr(q + kRagdoll[k][1], 12)) continue;
        float *v = (float *)(q + kRagdoll[k][1]);
        float dx = v[0] - tr[0], dy = v[1] - tr[1], dz = v[2] - tr[2];
        if (dx * dx + dy * dy + dz * dz > 30.0f * 30.0f || v[0] != v[0]) continue;   // not a position near the body
        memcpy(out, v, 12);
        return 1;
    }
    memcpy(out, tr, 12);
    return 0;
}

// Follow each fresh body while its soldier object exists.
static void downed_follow_bodies(void)
{
    for (int d = 0; d < 64; d++) {
        Downed *e = &g_downed[d];
        if (!e->player || !e->body || H->frame - e->at > DOWNED_WINDOW_FRAMES) continue;
        if (IsBadReadPtr(e->body, 0x4e0) || *(unsigned *)e->body != ADDR_SOLDIER_VTABLE) { e->body = NULL; continue; }
        ragdoll_pos(e->body, e->pos);
    }
}

static unsigned g_standUpAt[MAX_BOTS];   // frame to press prone (toggle) once after a revive
static void mode_note_death(char *pl);
static char *g_lastSpawnedBot[MAX_BOTS]; static unsigned g_lastSpawnedAt[MAX_BOTS];   // our own (re)spawns, by player

// A soldier that came back for `pl` was a revive (not a redeploy): bots - we did not spawn it; humans - near the body.
static int came_back_by_revive(char *pl, char *s, const float *body)
{
    if (is_bot(pl)) {
        for (int k = 0; k < MAX_BOTS; k++) if (g_lastSpawnedBot[k] == pl && H->frame - g_lastSpawnedAt[k] < 60) return 0;
        return 1;
    }
    float *np = (float *)(s + OFF_SOLDIER_SPAWN_POSITION);
    float dx = np[0] - body[0], dz = np[2] - body[2];
    return dx * dx + dz * dz < 10.0f * 10.0f;
}
static void press_button(int i, int action, int frames);

static int fired_recently(char *pl);
static void downed_tick(void)
{

    for (int i = 0; i < H->botCount; i++) {
        if (g_standUpAt[i] && H->frame >= g_standUpAt[i]) {   // after a revive: primary out (the crawl check below stands it up if needed)
            g_standUpAt[i] = 0;
            g_local_downed_tick_primaryAt[i] = H->frame + 20;
        }
        if (g_local_downed_tick_primaryAt[i] && H->frame >= g_local_downed_tick_primaryAt[i]) { g_local_downed_tick_primaryAt[i] = 0; press_button(i, SLOT_ACTION(0), 3); }   // primary weapon out
    }
    if (H->frame % 3) return;
    char *pm = player_manager();
    if (!pm) return;
    void **arr = *(void ***)(pm + 0xac);
    unsigned count = *(unsigned *)(pm + 8);
    for (unsigned k = 0; k < count && k < 64; k++) {
        char *pl = arr[k];
        if (pl != g_downLastPlayer[k]) { g_downLastPlayer[k] = pl; g_downLastSoldier[k] = NULL; }
        if (!pl) continue;
        char *s = *(char **)(pl + OFF_PLAYER_SOLDIER);
        if (s) {
            if (!g_downLastSoldier[k])   // just came back: revived at the body (not a redeploy elsewhere)?
                for (int d = 0; d < 64; d++) {
                    Downed *e = &g_downed[d];
                    if (e->player != pl || H->frame - e->at > DOWNED_WINDOW_FRAMES + 60) continue;
                    int bi = bot_index(pl);
                    if (bi >= 0 && came_back_by_revive(pl, s, e->pos)) {
                        g_standUpAt[bi] = H->frame + 30;   // revived soldiers get up prone: stand the bot up
                        bot_say_later(bi, ORDER_THANKYOU, 45);   // thanks to whoever revived it
                        bot_log("revive: %s is back up - standing up", pl + OFF_PLAYER_ONLINE_NAME); g_jobStats[JOB_REVIVED]++;
                    }
                }
            memcpy(g_downLastPos[k], s + OFF_SOLDIER_SPAWN_POSITION, 12);
            g_downLastSoldier[k] = s;
            g_downLastInVehicle[k] = *(char **)(pl + OFF_PLAYER_VEHICLE) != NULL;
            // back on their feet: whatever downed record they had is over
            for (int d = 0; d < 64; d++) if (g_downed[d].player == pl) g_downed[d].player = NULL;
            continue;
        }
        if (!g_downLastSoldier[k]) continue;
        g_downLastBody[k] = g_downLastSoldier[k];
        g_downLastSoldier[k] = NULL;
        if (g_downLastInVehicle[k]) continue;   // killed inside a vehicle: nothing to revive
        for (int d = 0; d < 64; d++) {
            Downed *e = &g_downed[d];
            if (e->player && H->frame - e->at < DOWNED_WINDOW_FRAMES) continue;
            e->player = pl;
            memcpy(e->pos, g_downLastPos[k], 12);
            e->at = H->frame;
            e->team = player_team(pl);
            e->medic = 0;
            e->body = g_downLastBody[k];
            bot_log("revive: %s went down at (%.1f, %.1f)", pl + OFF_PLAYER_ONLINE_NAME, e->pos[0], e->pos[2]);
            bm_learn(-1, BM_LEARN_DEATH, e->pos, NULL);   // AltBotMod V21: where soldiers die
            mode_note_death(pl);   // AltBotMod V16
            break;
        }
    }
}

static Downed *downed_of(char *pl)
{
    for (int d = 0; d < 64; d++) if (g_downed[d].player == pl && H->frame - g_downed[d].at < DOWNED_WINDOW_FRAMES) return &g_downed[d];
    return NULL;
}
// AltBotMod V15: when a defend job ends. V9-V14 set "for ever" whenever a defend job resumed after a fight, cover, a
// revive, a repair, a rocket or a supply run - right for a player's "need backup" (until GoGoGo), wrong for the 12 s
// screen beside a tank: those bots then held that spot for the rest of the round ("stand in one place and forget what
// they were doing"; the activity watchdog let them be, as they had a commander). Kept outside BotBrain (its heap
// layout survives reloads).
static unsigned g_defendUntil[MAX_BOTS];
#define BACKUP_DEFEND_FRAMES (3 * 60 * 30)   // a player's "need backup": 3 minutes (V9-V14: until GoGoGo)
static unsigned defend_until(int i) { return i >= 0 && i < MAX_BOTS && g_defendUntil[i] ? g_defendUntil[i] : H->frame; }
typedef struct BotBrain {
    int enabled; int targetFlag; unsigned deadSince; int waiting; int enroute; int avoidFlag; unsigned avoidUntil;
    // Human-issued task (Q menu); overrides the flag loop until released with GoGoGo by the commander.
    int task;
    char *commander;         // human who issued the current task (follow / backup)
    char *serviceTarget;     // human being served (ammo / medkit)
    int supplySlot;          // loadout weapon slot of the gadget to drop
    float supplyPos[3];      // where the human stood when calling: the gadget goes there
    unsigned taskUntil;      // frame when the task expires
    unsigned replanAt;       // next frame to refresh the path to the moving human
    int step; unsigned stepAt;           // supply drop sequence
    int prevTask; char *prevCommander;   // what to resume after the supply run
    int sayNext; unsigned sayAt;         // queued radio line (e.g. Sorry after Negative)
    unsigned retryAt;                    // no reachable flag: wait before trying again
    float postPos[3];                    // defend: where to hold
    float postFacing;                    // defend: heading to cover (away from the called spot)
    int crouched;                        // defend: crouch requested
} BotBrain;
#define FLAG_RETRY_OFFSETS    4
#define FLAG_AVOID_FRAMES     (10 * 30)
// host->reserved layout: 0..11 kit slots, 16 kitted mask, 20 aim mode, 24 nav grid, 28 nav building, 32/36/40/48/52/56 brain array ptr (v1..v6), 44 loadout library,
// 64..319 aim, 512..1023 nav state. The brain array lives on the process heap so it survives reloads.
static BotBrain *bot_brain(int i)
{
    BotBrain **slot = (BotBrain **)(H->reserved + 56);   // v6 layout (older pointers at +32..+52 are abandoned)
    if (!*slot) *slot = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(BotBrain) * MAX_BOTS);
    return *slot + i;
}

static float dist2(const float *a,const float *b);
static int flag_pressure(int team,int f,int except)
{
    const float *at=mode_is_cq()?flag_pos(f):objective_pos(f);   // V16: an M-COM in Rush (V15 read a Conquest flag there and crashed)
    int count=0;for(int j=0;j<H->botCount;j++)if(j!=except&&player_team(H->bots[j])==team&&*(char **)(H->bots[j]+OFF_PLAYER_SOLDIER)){
        if(bot_brain(j)->targetFlag==f||(at&&dist2((float *)(*(char **)(H->bots[j]+OFF_PLAYER_SOLDIER)+OFF_SOLDIER_POSITION),at)<35*35))count++;}return count;
}
static int pick_flag(int team, const float *from, int avoid)
{
    int best = -1;
    float bestD = 1e30f;
    for (int f = 0; f < g_flagCount; f++) {
        if (flag_owner(f) == team || f == avoid) continue;
        float dx = flag_pos(f)[0] - from[0], dz = flag_pos(f)[2] - from[2], d = sqrtf(dx * dx + dz * dz)+flag_pressure(team,f,-1)*150.0f;
        if (d < bestD) { bestD = d; best = f; }
    }
    return best;
}

// ---- Squad objectives: every member of a squad goes for the same flag. Flags the team does not own are ranked by
// distance from the squad (its living members, else the team HQ): odd squads take the closest, even squads the
// second closest (or the only one left). Re-chosen when the flag falls to the team or every 30 s.
#define SQUAD_OBJECTIVE_REFRESH  (30 * 30)
static int g_squadTarget[3][33];
static unsigned g_squadTargetAt[3][33];
static int player_squad(char *pl);

// Position of a bot-led squad among its team's bot-led squads (1 = lowest squad id), 0 if the squad is not bot-led.
// Human squads are skipped, so bot squads alternate odd / even no matter which squad numbers players hold.
static int bot_squad_rank(int team, int squad)
{
    char *pm = player_manager();
    if (!pm) return 0;
    void **arr = *(void ***)(pm + 0xac);
    unsigned count = *(unsigned *)(pm + 8);
    unsigned botLed = 0;   // bit per squad id
    for (unsigned k = 0; k < count; k++) {
        char *pl = arr[k];
        if (!pl || player_team(pl) != team || !is_bot(pl) || !pl[0x788]) continue;   // 0x788: squad leader flag
        int s = player_squad(pl);
        if (s >= 1 && s <= 31) botLed |= 1u << s;
    }
    if (!(botLed & (1u << squad))) return 0;
    int rank = 0;
    for (int s = 1; s <= squad; s++) if (botLed & (1u << s)) rank++;
    return rank;
}

// ---- Squad allocation per team (re-run when flag ownership or the bot squads change, at least every 30 s) ----
// Attack: each flag the team does not own gets the 2 closest bot squads. Defend: the rest guard friendly flags, the
// one nearest the enemy HQ first. With no friendly flag left the rest attack too (odd / even over the two closest).
#define SQUAD_ALLOC_REFRESH   (30 * 30)
#define ATTACKERS_PER_FLAG    2
#define PATROL_RADIUS         15.0f
static unsigned char g_squadDefend[3][33];
static unsigned g_allocAt[3], g_allocSig[3];

static unsigned bot_squads_mask(int team)
{
    unsigned mask = 0;
    for (int s = 1; s <= 31; s++) if (bot_squad_rank(team, s)) mask |= 1u << s;
    return mask;
}

static void squad_centre(int team, int squad, float *ref)
{
    int n = 0;
    ref[0] = ref[1] = ref[2] = 0;
    for (int i = 0; i < H->botCount; i++) {
        char *pl = H->bots[i], *s = *(char **)(pl + OFF_PLAYER_SOLDIER);
        if (!s || player_team(pl) != team || player_squad(pl) != squad) continue;
        float *p = (float *)(s + OFF_SOLDIER_POSITION);
        ref[0] += p[0]; ref[2] += p[2]; n++;
    }
    if (n) { ref[0] /= n; ref[2] /= n; } else { ref[0] = g_hqPos[team][0]; ref[2] = g_hqPos[team][2]; }
}

static float dist2(const float *a, const float *b) { float dx = a[0] - b[0], dz = a[2] - b[2]; return dx * dx + dz * dz; }

static void allocate_squads(int team, unsigned mask)
{
    int sq[32],ns=0,target[32],defend[32]={0},counts[MAX_FLAGS]={0}; float cen[32][3];
    for(int q=1;q<=31;q++)if(mask&(1u<<q)){sq[ns]=q;squad_centre(team,q,cen[ns]);target[ns++]=-1;}
    int attacks=0;for(int f=0;f<g_flagCount;f++)if(flag_owner(f)!=team)attacks++;
    // Keep squads committed to uncaptured flags. Long mountain-map journeys
    // must survive the allocator's periodic refresh.
    if(attacks)for(int k=0;k<ns;k++){
        int f=g_squadTarget[team][sq[k]]-1;
        if(f>=0 && f<g_flagCount && flag_owner(f)!=team && !counts[f]){target[k]=f;counts[f]++;}
    }
    // Cover separated objectives before reinforcing any one flag. With fewer
    // squads than flags, farthest-first coverage includes the far end of the
    // map rather than always exhausting the roster on low flag indices.
    for(;;){
        int chosen=-1,closest=-1;float coverage=-1;
        for(int f=0;f<g_flagCount;f++)if(flag_owner(f)!=team && !counts[f]){
            int kbest=-1;float nearestD=INFINITY,spacing=INFINITY;int assigned=0;
            for(int k=0;k<ns;k++){
                if(target[k]<0){float d=dist2(cen[k],flag_pos(f));if(d<nearestD){nearestD=d;kbest=k;}}
                else {float d=dist2(flag_pos(target[k]),flag_pos(f));if(d<spacing)spacing=d;assigned++;}
            }
            if(kbest<0)continue;
            float score=assigned ? spacing : 1.0f/(1.0f+nearestD);
            if(chosen<0 || score>coverage){chosen=f;closest=kbest;coverage=score;}
        }
        if(chosen<0)break;
        target[closest]=chosen;counts[chosen]++;
    }
    int guards=attacks?ns/4:ns;
    for(int f=0;f<g_flagCount&&guards;f++)if(flag_owner(f)==team){int best=-1;for(int k=0;k<ns;k++)if(target[k]<0&&(best<0||dist2(cen[k],flag_pos(f))<dist2(cen[best],flag_pos(f))))best=k;if(best>=0){target[best]=f;defend[best]=1;counts[f]++;guards--;}}
    for(int k=0;k<ns;k++)if(target[k]<0){float best=INFINITY;for(int f=0;f<g_flagCount;f++){if(attacks&&flag_owner(f)==team)continue;float score=sqrtf(dist2(cen[k],flag_pos(f)))+counts[f]*2000.0f;if(score<best){best=score;target[k]=f;}}if(target[k]>=0){counts[target[k]]++;defend[k]=!attacks;}}
    for(int k=0;k<ns;k++){g_squadTarget[team][sq[k]]=target[k]+1;g_squadDefend[team][sq[k]]=(unsigned char)defend[k];}
}

static int squad_objective(int team, int squad)
{
    if (team < 1 || team > 2 || squad < 1 || squad > 31 || !g_flagCount) return -1;
    { int od = 0, of = squad_order_flag(team, squad, &od); if (of >= 0) return of; }   // AltBotMod V34: a player's squad order
    if (H->frame % 15 == 0 || !g_allocAt[team]) {   // cheap checks at most twice a second
        unsigned sig = bot_squads_mask(team) * 2654435761u;
        for (int f = 0; f < g_flagCount; f++) sig = sig * 31u + (unsigned)flag_owner(f);
        if (sig != g_allocSig[team] || H->frame - g_allocAt[team] > SQUAD_ALLOC_REFRESH || !g_allocAt[team]) {
            g_allocSig[team] = sig;
            g_allocAt[team] = H->frame ? H->frame : 1;
            allocate_squads(team, bot_squads_mask(team));
        }
    }
    if (!bot_squad_rank(team, squad)) return -1;
    int t = g_squadTarget[team][squad] - 1;
    return t >= 0 && t < g_flagCount ? t : -1;
}

static int squad_defending(int team, int squad)
{
    { int od = 0; if (squad_order_flag(team, squad, &od) >= 0) return od; }   // AltBotMod V34
    return team >= 1 && team <= 2 && squad >= 1 && squad <= 31 && g_squadDefend[team][squad];
}

// ---- Local objective: after getting out of a vehicle (or being released with GoGoGo) a bot takes the flag nearest
// to where it stands - attack if it is not ours, guard if it is - until that flag is taken or 90 s pass ----
#define LOCAL_OBJECTIVE_FRAMES (15 * 30)
// Caspian Test16: a recovery order (activity watchdog) lasts longer and keeps
// the flag it failed on excluded, so the director cannot pull the bot straight
// back to the same unreachable objective when the order ends.
#define RECOVERY_OBJECTIVE_FRAMES (40 * 30)
#define RECOVERY_AVOID_FRAMES     (60 * 30)
static int g_localFlag[MAX_BOTS];        // flag index + 1 (0 = none)
static unsigned g_localUntil[MAX_BOTS];
#include "local_order.h"

static unsigned g_noRouteSince[MAX_BOTS];   // first failed flag route of a run of failures (0 = routing fine)
// AltBotMod V30: swimming and getting off a cut-off spot (swim_steer_tick, escape_consider)
static unsigned char g_swimming[MAX_BOTS], g_swimSide[MAX_BOTS]; static unsigned g_swimCheckAt[MAX_BOTS], g_swimSince[MAX_BOTS], g_swimProgAt[MAX_BOTS], g_swimTurnUntil[MAX_BOTS];
static float g_swimProg[MAX_BOTS][2]; static unsigned g_swimStarts;
static unsigned char g_escapeFails[MAX_BOTS]; static unsigned g_escapeFailAt[MAX_BOTS], g_escapeUntil[MAX_BOTS], g_escapeJumpAt[MAX_BOTS], g_escapes; static float g_escapeGoal[MAX_BOTS][3], g_escapeProg[MAX_BOTS][2];
static void escape_consider(int i, char *soldier, int f);
static int g_overflowFlag[MAX_BOTS];        // spare guards keep an attack assignment until it is taken / unreachable
typedef struct FlagRouteRequest {
    char *soldier; int flag,pending,routeActive,routeFlag,floor,candidate,nfloors,sector;
    float goal[3],floors[3],yaw;
} FlagRouteRequest;
static FlagRouteRequest g_flagRoute[MAX_BOTS];
static int plan_flag_path(int i,char *soldier,int flag,float x,float y,float z)
{
    FlagRouteRequest *r=&g_flagRoute[i];
    if(!r->pending || r->soldier!=soldier || r->flag!=flag){r->soldier=soldier;r->flag=flag;r->goal[0]=x;r->goal[1]=y;r->goal[2]=z;}
    int n=plan_path(i,soldier,r->goal[0],r->goal[1],r->goal[2]);
    r->pending=!n && g_lastPlanDeferred;
    return n;
}
static int overflow_objective(int i,int team,const float *pos)
{
    BotBrain *b=bot_brain(i);int f=g_overflowFlag[i]-1;
    if(f>=0 && f<g_flagCount && flag_owner(f)!=team && !(b->avoidUntil && f==b->avoidFlag))return f;
    int best=-1;float score=1e30f;
    for(f=0;f<g_flagCount;f++) {
        if(flag_owner(f)==team || (b->avoidUntil && f==b->avoidFlag))continue;
        float d=sqrtf(dist2(pos,flag_pos(f)))+flag_pressure(team,f,i)*150.0f;
        if(d<score){score=d;best=f;}
    }
    g_overflowFlag[i]=best+1;return best;
}
static void kill_bot(char *player);
// exclude >= 0: a recovery order. That flag is skipped (unless it is the only
// one) and stays avoided for RECOVERY_AVOID_FRAMES, which the director honours.
static void choose_local_objective_except(int i, const char *why, int exclude)
{
    char *pl = H->bots[i], *s = *(char **)(pl + OFF_PLAYER_SOLDIER);
    if (!s || !g_flagCount) return;
    float *me = (float *)(s + OFF_SOLDIER_SPAWN_POSITION);   // transform: right even the frame we step out
    int best = -1, team = player_team(pl);
    if (exclude >= g_flagCount) exclude = -1;
    float bestD = 1e30f;
    for (int pass = 0; pass < 3 && best < 0; pass++)   // a flag not ours first; all ours: the nearest to guard; last resort: the excluded one
        for (int f = 0; f < g_flagCount; f++) {
            if (pass < 2 && f == exclude) continue;
            if (!pass && flag_owner(f) == team) continue;
            if (pass == 2 && f != exclude) continue;
            float d = sqrtf(dist2(flag_pos(f),me))+flag_pressure(team,f,i)*150.0f;
            if (d < bestD) { bestD = d; best = f; }
        }
    if (best < 0) return;
    g_localFlag[i] = best + 1;
    g_localUntil[i] = H->frame + (exclude >= 0 ? RECOVERY_OBJECTIVE_FRAMES : LOCAL_OBJECTIVE_FRAMES);
    local_order_note(i, best);
    BotBrain *b = bot_brain(i);
    b->targetFlag = -1;
    b->enroute = b->waiting = 0;
    if (exclude >= 0 && best != exclude) { b->avoidFlag = exclude; b->avoidUntil = H->frame + RECOVERY_AVOID_FRAMES; }
    else { b->avoidUntil = 0; b->avoidFlag = -1; }
    bot_nav(i)->mode = 0;
    H->botMove[i][0] = H->botMove[i][2] = 0;
    bot_log("brain: %s (%s) re-evaluates: nearest flag %d, %.0f m away - %s it%s", pl + OFF_PLAYER_ONLINE_NAME, why, best, sqrtf(dist2(flag_pos(best),me)),
            flag_owner(best) == player_team(pl) ? "guard" : "attack", exclude >= 0 && best != exclude ? " (avoids the flag it was stuck on)" : "");
}
static void choose_local_objective(int i, const char *why)
{
    choose_local_objective_except(i, why, -1);
}

static void clear_local_objective(int i)
{
    g_localFlag[i] = 0; g_localUntil[i] = 0;
    memset(&g_localOrder[i], 0, sizeof g_localOrder[i]);
}

// The bot's current local objective (-1 = none); *defend says guard or attack.
// It ends on expiry, on capture of an attacked flag, on a new life (respawn)
// or on a team change, and then the objective director takes over again.
static int local_objective(int i, int team, int *defend)
{
    int f = g_localFlag[i] - 1;
    if (f < 0 || f >= g_flagCount || H->frame > g_localUntil[i] ||
        g_localOrder[i].team != team ||
        g_lastSpawnedAt[i] > g_localOrder[i].setAt ||
        (g_localOrder[i].attack && flag_owner(f) == team)) { clear_local_objective(i); return -1; }
    *defend = flag_owner(f) == team;
    return f;
}

static void cmd_spawn(const char *name);
static void teams_fix_all(void);
static void bot_say(int i, int orderType);
static void bot_say_later(int i, int type, unsigned delayFrames);
#define POSE_PRESS_FRAMES 8   // prone needs the button held a few frames (2 frames is ignored)
// inwatch: a player's input levels (EntryInput +0x10, a float per action) and stance as they change - research
static char *g_inWatchPl;
static unsigned g_inWatchUntil;
static float g_inWatchPrev[80];
static unsigned g_inWatchRaw[256];
static int g_inWatchPose;
// Crouch on a held spot: EIA 33 toggles crouch for a bot (found by trying every action on a passive bot and reading its
// stance - 43, the action a player's toggle-crouch key shows, and 13 do nothing for a bot); jump (EIA 12) stands it up
// again, as for prone. Both only from the right stance (soldier +0x2d0: 0 stand, 1 crouch, 2 prone; +0x2d4 the stance
// it is changing to), so the toggle can never put a bot the wrong way round.
static int g_crouchAction = 33;
// Sprint is not an action level: a player's input carries it as two flag bits (+0x1e0 bit 0x00400000 and +0x1e4 bit
// 0x8, set together for exactly as long as the sprint key is held - seen with inwatch). Set for a bot while
// g_sprintUntil[i] is ahead.
#define OFF_INPUT_FLAGS_A   0x1e0
#define OFF_INPUT_FLAGS_B   0x1e4
#define SPRINT_BIT_A        0x00400000u
#define SPRINT_BIT_B        0x00000008u
static unsigned g_sprintUntil[MAX_BOTS];
static void bot_crouch(int i)
{
    char *s = *(char **)((char *)H->bots[i] + OFF_PLAYER_SOLDIER);
    if (!s || g_crouchAction < 0) return;
    if (*(int *)(s + OFF_SOLDIER_POSE) == 0 && *(int *)(s + OFF_SOLDIER_POSE + 4) == 0) press_button(i, g_crouchAction, POSE_PRESS_FRAMES);
}
static void bot_stand(int i)
{
    char *s = *(char **)((char *)H->bots[i] + OFF_PLAYER_SOLDIER);
    if (!s) return;
    int pose = *(int *)(s + OFF_SOLDIER_POSE), want = *(int *)(s + OFF_SOLDIER_POSE + 4);
    if (pose == 1 || pose == 2 || want == 1 || want == 2) press_button(i, 12, 4);   // jump: up
}
static int task_tick(int i, BotBrain *b, char *soldier);
static char *player_vehicle(char *pl);
static void bot_exit_vehicle(int i, int parachute);
// A bot in a vehicle that nothing drives for it: no drive of its own, no human aboard, no bot driving it - its drive
// state was lost (a core reload wipes it: Recon RU sat in a buggy nosed into a building, nobody at the controls). Out
// after 3 s; the brain carries on and may take a vehicle again properly.
static unsigned g_strandedSince[MAX_BOTS];
static void stranded_in_vehicle_tick(int i);
static void place_class_gadget(int i, char *soldier);
void schedule_press(int i, int action, unsigned at);
static int bot_gadget_slot(int i, const char *k1, const char *k2);
static int bot_ammo(int i, int slot);
static const BMDifficulty *bot_profile(int i);   // AltBotMod V9
static float engage_range(int i);
static void drive_masks_reset(void);
static int *wanted_bots(void);
static void save_bot_count(void);
static int g_rosterAuto;
static int player_squad(char *pl);
static int squad_request_recent(char *pl);
static int squad_objective(int team, int squad);
static void retire_stuck_bot(int i,const char *reason);
static void air_stop(int i,const char *why,int exitVehicle);
static int squad_defending(int team, int squad);
static void choose_local_objective(int i, const char *why);
static void repair_scan(int i, char *soldier);
static void revive_scan(int i, char *soldier);
static void cover_react(int i, char *soldier);
static void end_revive(int i,BotBrain *b,const char *why);
static void rocket_scan(int i, char *soldier);
static void c4_scan(int i, char *soldier);   // AltBotMod V16
static int rush_operator_focus(int i, char *enemy);   // AltBotMod V16.1 (modes_native.inc)
static int rush_operator_holding(int i);
static void end_task(int i, BotBrain *b, const char *why);
static char *g_fightTarget[MAX_BOTS];
static unsigned char g_c4Mode[MAX_BOTS];
static unsigned g_lockSince[MAX_BOTS];
static unsigned g_lockCheckAt[MAX_BOTS], g_lockFirstAt[MAX_BOTS], g_lockLaunches, g_lockNoLaunch; static int g_lockAmmo[MAX_BOTS];   // AltBotMod V22
// AltBotMod V23: the ways of firing a lock-on launcher, and how each did (see lockon_tick)
static unsigned char g_lockStrat[MAX_BOTS], g_lockTry[MAX_BOTS];
// AltBotMod V25: the game's own lock. A locking weapon (ServerLockingWeapon, +0x1f0) has a ServerLockingController (vtable
// 0x025b9248) that the server updates every tick (ServerLockingController::update, 0x00dda490): with +0x10 ("active") set it
// looks along the weapon's aim for a target in its reticle and, after the weapon's LockTime on it, sets +0xc = 2 (locked);
// a FireOnlyWhenLockedOn weapon (Stinger, Igla, Javelin, aircraft missiles) launches only then. "active" is set by the
// controller's activate() (vtable slot 1: +0x10 = 1, +0x11 = 0), which the game calls for a player from the client's
// zoom / lock input - a bot never sends it, so a bot's controller never became active and nothing ever locked (V22-V24
// live: 0 launches in 300+ tries). Now the update is hooked: when a bot wants a lock (its aim on a target) and the weapon
// being updated is the bot's (the weapon's aim transform at the bot's eye / vehicle, pointing the way the bot aims), the
// controller is activated exactly as activate() does, and the game does the rest - target search, lock time, launch.
#define ADDR_LOCK_CTRL_UPDATE  0x00dda490
#define ADDR_LOCK_CTRL_VTABLE  0x025b9248
static const unsigned char kLockUpdatePrologue[6] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};   // push ebp; mov ebp,esp; and esp,-16
static unsigned g_lockWantAt[MAX_BOTS]; static float g_lockWantPos[MAX_BOTS][3], g_lockWantDir[MAX_BOTS][3], g_lockWantReach[MAX_BOTS];
static char *g_lockCtrl[MAX_BOTS]; static unsigned g_lockCtrlAt[MAX_BOTS], g_lockTargetFrames[MAX_BOTS];
static volatile unsigned g_lockHookCalls, g_lockMatches, g_lockActivations, g_lockNativeLocks; static int g_lockHooked;
static float g_lockXf[MAX_BOTS][16] __attribute__((aligned(16)));   // AltBotMod V26: the aim transform handed to the target search
static float g_lockDevSum, g_lockDevMax; static unsigned g_lockDevN;
// AltBotMod V28: per lock-on attempt - the best lock progress the game reached, how long its controller was seen, whether
// the bot fired without the lock (the fallback) - reported however the attempt ends
static unsigned char g_lockAttempt[MAX_BOTS], g_lockForced[MAX_BOTS]; static float g_lockBest[MAX_BOTS]; static unsigned g_lockSeenFrames[MAX_BOTS], g_lockNativeSince[MAX_BOTS], g_lockAttemptAt[MAX_BOTS];
static unsigned g_lockAttempts, g_lockForcedShots;
static void lock_want(int i, const float *from, const float *to, float reach)
{
    float d[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]}, l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (l < 1.0f) return;
    for (int k = 0; k < 3; k++) { g_lockWantPos[i][k] = from[k]; g_lockWantDir[i][k] = d[k] / l; }
    g_lockWantReach[i] = reach; g_lockWantAt[i] = H->frame ? H->frame : 1;
}
// AltBotMod V29: the lock the bot asks for, on WHICH vehicle - held on it continuously since g_lockWantEntSince. The game's
// own target search never picked one for a bot (V28 live: 44 attempts, "a target picked 0.0 s" every time): once the aim
// has been on the target for the weapon's own lock time, the lock is put in place (lock_fake, from on_lock_update).
static char *g_lockWantEnt[MAX_BOTS]; static unsigned g_lockWantEntSince[MAX_BOTS], g_lockWantEntAt[MAX_BOTS], g_lockFakeAt[MAX_BOTS];
static unsigned g_lockFakes, g_lockFakeTargets, g_lockFakeRefused;
static int g_lockFakeOn = 1;   // (console: lockfake 0/1)
static void lock_want_ent(int i, const float *from, const float *to, float reach, char *target)
{
    lock_want(i, from, to, reach);
    if (!target) return;   // (a moment off target: the hold on it lapses only after 10 frames without one - lock_fake needs one within 3)
    if (target != g_lockWantEnt[i] || !g_lockWantEntAt[i] || H->frame - g_lockWantEntAt[i] > 10) g_lockWantEntSince[i] = H->frame ? H->frame : 1;
    g_lockWantEnt[i] = target; g_lockWantEntAt[i] = H->frame ? H->frame : 1;
}
// The game's own pieces, as its target search (0x00dda0a0) uses them when it finds a vehicle: a target holder (8 bytes from
// the game's allocator: the vehicle-target vtable and a weak reference to the vehicle), stored at controller +0x54 (the
// holder it replaces deleted as the game deletes it), then SetLocked(true) (0x00dd27a0: state 2, progress 1.0, the
// lock time) - the next updates keep it locked while the vehicle lives (progress > 0.999999 with a valid holder).
#define ADDR_GAME_HEAP          0x0287f330   // the allocator the target search allocates its holder with
#define ADDR_GAME_ALLOC         0x00430700   // thiscall (heap, size, align, flags) -> memory
#define ADDR_LOCK_TARGET_VT     0x025ba2a4   // the vehicle-target holder: +4 weak reference (token: [0] = vehicle + 4)
#define ADDR_WEAKREF_INIT       0x00d7a030   // cdecl (void **out, entity): a counted weak reference to the entity
#define ADDR_LOCK_SET_LOCKED    0x00dd27a0   // thiscall (controller, bool locked)
typedef void *(__attribute__((thiscall)) *GameAlloc_t)(void *heap, unsigned size, unsigned align, unsigned flags);
typedef void (__cdecl *WeakRefInit_t)(void **out, void *entity);
typedef void (__attribute__((thiscall)) *LockSetLocked_t)(void *ctrl, int locked);
typedef void (__attribute__((thiscall)) *HolderDelete_t)(void *holder, int flags);
static char *lock_holder_vehicle(void *holder)
{
    if (!holder || IsBadReadPtr(holder, 8)) return NULL;
    void **tok = ((void ***)holder)[1];
    if (!tok || IsBadReadPtr(tok, 4) || !*tok) return NULL;
    return (char *)*tok - 4;
}
static void lock_fake(int i, char *ctrl)
{
    char *t = g_lockWantEnt[i];
    if (!g_lockFakeOn || !g_enhanced.lockAssist || !t || !g_lockWantEntSince[i] || H->frame - g_lockWantEntAt[i] > 3) return;
    if (IsBadReadPtr(t, 0x128 + 4) || *(unsigned *)t != 0x025bf8e8u || !(*(float *)(t + 0x128) > 0)) { g_lockFakeRefused++; return; }   // (a live ServerVehicleEntity: vtable, health +0x128)
    if (IsBadReadPtr(ctrl, 0x58)) return;
    char *data = (char *)(uintptr_t)*(unsigned *)(ctrl + 4);   // (the controller's data: LockTime at +0xc)
    float lt = data && !IsBadReadPtr(data, 0x30) ? *(float *)(data + 0xc) : 0;   // the weapon's own lock time
    if (!isfinite(lt) || lt < 0.3f || lt > 6.0f) lt = 1.5f;
    // AltBotMod V30: the bots lock faster than the weapon's own lock time (LockOnSeconds, default 0.5 s)
    if (g_enhanced.lockOnSeconds >= 0.0f && lt > g_enhanced.lockOnSeconds) lt = g_enhanced.lockOnSeconds;
    unsigned held = H->frame - g_lockWantEntSince[i];
    if (held < (unsigned)(lt * 30.0f)) return;
    void *cur = *(void **)(ctrl + 0x54);
    if (lock_holder_vehicle(cur) != t) {
        void *w = ((GameAlloc_t)ADDR_GAME_ALLOC)((void *)ADDR_GAME_HEAP, 8, 0x10, 0);
        if (!w) { g_lockFakeRefused++; return; }
        ((unsigned *)w)[0] = ADDR_LOCK_TARGET_VT; ((unsigned *)w)[1] = 0;
        ((WeakRefInit_t)ADDR_WEAKREF_INIT)((void **)w + 1, t);
        if (lock_holder_vehicle(w) != t) { g_lockFakeRefused++; }   // (logged in the status; the game deletes it with the controller)
        if (cur && !IsBadReadPtr(cur, 4)) ((HolderDelete_t)(*(void ***)cur)[0])(cur, 1);
        *(void **)(ctrl + 0x54) = w;
        g_lockFakeTargets++;
    }
    if (*(int *)(ctrl + 0xc) != 2 || !(*(float *)(ctrl + 0x8) > 0.999999f)) {
        ((LockSetLocked_t)ADDR_LOCK_SET_LOCKED)(ctrl, 1);
        g_lockFakes++; g_lockFakeAt[i] = H->frame ? H->frame : 1;
        g_lockWantEntSince[i] = H->frame ? H->frame : 1;   // (the weapon fires on the lock in this very update - 0x01330c58; the next missile waits its own lock time)
        if (g_lockFakes <= 5 || g_lockFakes % 25 == 0)
            bot_log("lock: %s's lock on the %s is put in place after %.1f s on target (%u locks put in place so far)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME,
                    vehicle_name(t), held / 30.0f, g_lockFakes);
    }
}
// the bot's locking controller as the game last updated it: 0 none / inactive, 1 active (locking), 2 locked
static int lock_state(int i, float *progress)
{
    char *c = g_lockCtrl[i];
    if (!c || H->frame - g_lockCtrlAt[i] > 15 || IsBadReadPtr(c, 0x40) || *(unsigned *)c != ADDR_LOCK_CTRL_VTABLE) return 0;
    if (progress) *progress = *(float *)(c + 0x8);
    if (*(void **)(c + 0x54)) g_lockTargetFrames[i]++;   // (V28: the game picked a target to lock on)
    if (*(int *)(c + 0xc) == 2) return 2;
    return *(unsigned char *)(c + 0x10) ? 1 : 0;
}
// AltBotMod V34: locks ON vehicles - every locking controller the game updates (players' and bots' alike) names the vehicle
// its seeker is on (+0x54: the target holder; +0x8 progress, +0xc state 2 = locked). A bot flying or driving that vehicle
// pops flares / smoke before the missile comes (air_native.inc, passenger_native.inc). V16-V33: only after a hit.
void __cdecl on_lock_update(char *ctrl, unsigned *stack)
{
    g_lockHookCalls++;
    if (!ctrl || IsBadReadPtr(ctrl, 0x58) || *(unsigned *)ctrl != ADDR_LOCK_CTRL_VTABLE || !stack) return;
    {   // AltBotMod V34: whose vehicle is this seeker on?
        void *holder = *(void **)(ctrl + 0x54);
        if (holder && !IsBadReadPtr(holder, 8) && *(unsigned *)holder == ADDR_LOCK_TARGET_VT) {
            char *v = lock_holder_vehicle(holder);
            float prog = *(float *)(ctrl + 0x8); int st = *(int *)(ctrl + 0xc);
            if (v && !IsBadReadPtr(v, 4) && *(unsigned *)v == 0x025bf8e8u && isfinite(prog) && (st == 2 || prog > 0.4f))
                incoming_lock_note(v, st == 2 ? 1.0f : fminf(prog, 1.0f));
        }
    }
    float *m = (float *)stack[2];
    if (!m || IsBadReadPtr(m, 64)) return;
    const float *fwd = m + 8, *pos = m + 12;
    if (!isfinite(pos[0]) || !isfinite(fwd[0])) return;
    for (int i = 0; i < H->botCount && i < MAX_BOTS; i++) {
        unsigned at = g_lockWantAt[i];
        if (!at || H->frame - at > 3) continue;
        float dx = pos[0] - g_lockWantPos[i][0], dy = pos[1] - g_lockWantPos[i][1], dz = pos[2] - g_lockWantPos[i][2], r = g_lockWantReach[i];
        if (dx * dx + dy * dy + dz * dz > r * r) continue;
        if (fwd[0] * g_lockWantDir[i][0] + fwd[1] * g_lockWantDir[i][1] + fwd[2] * g_lockWantDir[i][2] < 0.85f) continue;
        g_lockMatches++;
        g_lockCtrl[i] = ctrl; g_lockCtrlAt[i] = H->frame ? H->frame : 1;
        if (!*(unsigned char *)(ctrl + 0x10)) { *(unsigned char *)(ctrl + 0x10) = 1; *(unsigned char *)(ctrl + 0x11) = 0; g_lockActivations++; }   // = activate()
        lock_fake(i, ctrl);   // AltBotMod V29
        // AltBotMod V26: the target search looks along this transform's forward row. V25 live: 22,397 matches and the lock
        // never moved (progress 0) - the weapon's own transform on the server does not follow a bot's aim closely enough
        // (a bot has no client camera). The search is handed a copy that points exactly where the bot aims, from the
        // weapon's own position (the argument is swapped; the game's transform is not touched).
        {
            const float *w = g_lockWantDir[i];
            float dev = acosf(fmaxf(-1.0f, fminf(1.0f, fwd[0] * w[0] + fwd[1] * w[1] + fwd[2] * w[2]))) * 57.29578f;
            g_lockDevSum += dev; g_lockDevN++; if (dev > g_lockDevMax) g_lockDevMax = dev;
            float *x = g_lockXf[i], up0[3] = {0, 1, 0};
            if (fabsf(w[1]) > 0.95f) { up0[1] = 0; up0[2] = 1; }
            float r[3] = {up0[1] * w[2] - up0[2] * w[1], up0[2] * w[0] - up0[0] * w[2], up0[0] * w[1] - up0[1] * w[0]};
            float rl = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
            if (rl > 1e-4f) {
                for (int k = 0; k < 3; k++) r[k] /= rl;
                float u[3] = {w[1] * r[2] - w[2] * r[1], w[2] * r[0] - w[0] * r[2], w[0] * r[1] - w[1] * r[0]};
                memcpy(x, m, 64);   /* (keeps the 4th column the game uses) */
                x[0] = r[0]; x[1] = r[1]; x[2] = r[2]; x[4] = u[0]; x[5] = u[1]; x[6] = u[2]; x[8] = w[0]; x[9] = w[1]; x[10] = w[2];
                stack[2] = (unsigned)(size_t)x;
            }
        }
        return;
    }
}
extern void hook_lock_update(void);
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _hook_lock_update\n"
    "_hook_lock_update:\n"
    "  pushad\n"
    "  pushfd\n"
    "  lea eax, [esp + 36]\n"
    "  push eax\n"
    "  push ecx\n"
    "  call _on_lock_update\n"
    "  add esp, 8\n"
    "  popfd\n"
    "  popad\n"
    "  push ebp\n"
    "  mov ebp, esp\n"
    "  and esp, 0xfffffff0\n"
    "  push 0x00dda496\n"
    "  ret\n"
    ".att_syntax prefix\n");
static unsigned g_lockWayWins[3], g_lockWayTries[3];
static const char *const kLockWayNames[3] = {"no sight, fire taps", "sight, fire taps", "sight, fire held"};
static int missile_launched_near(const float *at, unsigned since);   // missiles_native.inc
// The way to try: one that has launched before (the best rate), now and then another; while none has, each in turn.
static int lock_pick_way(void)
{
    int best = -1; float bestRate = 0;
    for (int w = 0; w < 3; w++) if (g_lockWayWins[w]) { float r = (float)g_lockWayWins[w] / (float)(g_lockWayTries[w] + 1); if (r > bestRate) { bestRate = r; best = w; } }
    unsigned total = g_lockWayTries[0] + g_lockWayTries[1] + g_lockWayTries[2];
    if (best >= 0 && total % 10 != 9) return best;
    int least = 0; for (int w = 1; w < 3; w++) if (g_lockWayTries[w] < g_lockWayTries[least]) least = w;
    return least;
}
static void vehicle_threat_scan(int i, char *soldier);
static void self_requests(int i, char *soldier);
static float g_lastHealth[MAX_BOTS];   // (defined with the cover code)
static int g_spareMax[MAX_BOTS], g_launcherMax[MAX_BOTS];   // this life's full reserve (primary) / full launcher load
static int soldier_ammo2(char *soldier, int slot, int *loaded, int *spare);
static int pickup_tick(int i, BotBrain *b, char *soldier);
static char *g_testVictim;
static unsigned char g_brainHeld[MAX_BOTS];
static unsigned g_vehChanceUntil[MAX_BOTS];   // just got out / just took a flag: may grab a ground vehicle nearby
static void start_fight(int i, char *target, int retreat, const char *why);
static int g_rocketsThisLife[MAX_BOTS];
// Enemy ground vehicles near a bot without a launcher: over VEH_SLOW_SPEED it takes cover; at or under it, within throwing
// range and with a grenade, it throws one first. Cover lasts until the vehicle is gone / empty / beyond VEH_THREAT_RANGE /
// out of sight for a while; then back to what it was doing.
#define VEH_THREAT_RANGE       40.0f
#define VEH_SLOW_SPEED         3.0f
#define GRENADE_MIN_RANGE      6.0f
#define GRENADE_MAX_RANGE      30.0f
#define GRENADE_SPEED          17.0f     // m/s off the hand (estimate - calibrate from "grenade:" log lines)
static char *g_threatVeh[MAX_BOTS];      // the vehicle a bot is in cover from (NULL: a soldier / none)
static unsigned char g_coverOpen[MAX_BOTS];   // the cover spot is open ground (no cover within reach): lie prone there
static unsigned char g_coverProne[MAX_BOTS];  // lying prone in cover (the stand-up jump waits until cover ends)
static unsigned g_threatVehHidden[MAX_BOTS], g_vehThreatCooldown[MAX_BOTS];
static unsigned char g_rocketGrenade[MAX_BOTS];   // TASK_ROCKET with a hand grenade instead of a launcher
static int g_grenadesThisLife[MAX_BOTS];
static const char *vehicle_threat_gone(int i, const float *me);
static int player_exists(char *pl);
static float dist2(const float *a, const float *b);
static void start_repair(int i, char *veh, int slot);
struct VehTrack;
static int vehicle_list(char ***out);
static int g_claymores[MAX_BOTS], g_tugs[MAX_BOTS], g_mines[MAX_BOTS];
static const unsigned char *drive_mask(void);
static float g_lastGadgetPos[MAX_BOTS][2];
static void press_button(int i, int action, int frames);
static int launcher_slot(int i);
static int bot_ammo(int i, int slot);
struct KitStore;

// ---- Flag sectors: the bots going to a flag split the ring around it evenly; each looks for a spot in its own
// sector, inside the capture area, with something low between it and the outside, and faces outward. ----
#define SECTOR_ATTACK_MIN      5.0f
#define SECTOR_ATTACK_MAX      9.0f
#define SECTOR_DEFEND_MAX      14.0f
static float g_sectorYaw[MAX_BOTS];
static unsigned char g_hasSector[MAX_BOTS];
static int solid_between(const float *from, const float *to, float *hitOut);
static int flag_sector_spot(int i, char *soldier, int f, int defend, float *out, float floorY)
{
    NavGrid *g = *nav_slot();
    if (!g || g->phase != 2) return 0;
    int team = player_team(H->bots[i]), n = 0, rank = 0;
    for (int j = 0; j < H->botCount; j++) {
        if (player_team(H->bots[j]) != team || !*(char **)(H->bots[j] + OFF_PLAYER_SOLDIER)) continue;
        if (j != i && bot_brain(j)->targetFlag != f) continue;
        n++;
        if (j < i) rank++;
    }
    if (n < 1) n = 1;
    const float *fp = flag_pos(f);
    float ang0 = f * 1.3f + 6.2831853f * rank / n;
    float rmax = defend ? SECTOR_DEFEND_MAX : SECTOR_ATTACK_MAX;
    float half = 3.14159265f / n;                    // this bot's slice of the ring
    if (half > 0.6f) half = 0.6f;
    float best[3], bestYaw = 0;
    int have = 0;
    for (int t = 0; t < 12; t++) {
        float ang = ang0 + (defend ? ((rand() % 200) - 100) / 100.0f * half : ((t % 3) - 1) * half * 0.6f);
        float r = SECTOR_ATTACK_MIN + (rmax - SECTOR_ATTACK_MIN) * ((t / 3) / 3.0f + (defend ? (rand() % 100) / 300.0f : 0));
        float dx = sinf(ang), dz = cosf(ang), pt[3];
        if (!nav_snap(g, fp[0] + dx * r, floorY, fp[2] + dz * r, pt) || fabsf(pt[1] - floorY) > 2.5f) continue;   // (on that floor)
        float threat[3] = {fp[0] + dx * (r + 35.0f), pt[1] + 1.6f, fp[2] + dz * (r + 35.0f)}, body[3] = {pt[0], pt[1] + 0.9f, pt[2]}, hit[3];
        int cover = solid_between(threat, body, hit) && dist2(hit, pt) < 2.5f * 2.5f;
        if (!have || cover) { memcpy(best, pt, 12); bestYaw = yaw_towards(dx, dz); have = 1; }
        if (cover) break;
    }
    if (!have) return 0;
    memcpy(out, best, 12);
    g_sectorYaw[i] = bestYaw;
    return 1;
}

unsigned char g_gliding[MAX_BOTS];   // in the air (free fall / parachute): fall_tick steers, the brain waits
static int plan_objective_route(int i,char *soldier,int flag,int defend)
{
    FlagRouteRequest *r=&g_flagRoute[i];g_lastPlanDeferred=0;
    if(!r->routeActive || r->routeFlag!=flag || r->soldier!=soldier || r->flag!=flag){
        memset(r,0,sizeof *r);r->routeActive=1;r->routeFlag=r->flag=flag;r->soldier=soldier;
        r->nfloors=nav_floors_at(*nav_slot(),flag_pos(flag)[0],flag_pos(flag)[1],flag_pos(flag)[2],r->floors,3);
        if(!r->nfloors){r->floors[0]=flag_pos(flag)[1];r->nfloors=1;}
    }
    while(r->floor<r->nfloors){
        int candidates=r->nfloors>1?2:FLAG_RETRY_OFFSETS+2;
        if(r->candidate>=candidates){r->floor++;r->candidate=0;r->pending=0;continue;}
        if(!plan_budget_left()){g_lastPlanDeferred=1;return 0;}
        if(!r->pending){
            float fy=r->floors[r->floor];
            r->sector=0;
            if(!r->candidate){
                if(!flag_sector_spot(i,soldier,flag,defend,r->goal,fy)){r->candidate++;continue;}
                r->sector=1;r->yaw=g_sectorYaw[i];
            }else{
                int attempt=r->candidate-1;
                float spread=defend?(attempt?6.0f:PATROL_RADIUS):attempt?8.0f:FLAG_SPREAD;
                r->goal[0]=flag_pos(flag)[0]+((rand()%200)-100)/100.0f*spread;
                r->goal[1]=fy;r->goal[2]=flag_pos(flag)[2]+((rand()%200)-100)/100.0f*spread;
            }
        }
        int n=plan_flag_path(i,soldier,flag,r->goal[0],r->goal[1],r->goal[2]);
        if(n){
            g_hasSector[i]=(unsigned char)r->sector;if(r->sector)g_sectorYaw[i]=r->yaw;
            if(r->floor)bot_log("brain: %s goes for flag %d on the floor at %.1f m",H->bots[i]+OFF_PLAYER_ONLINE_NAME,flag,r->floors[r->floor]);
            r->routeActive=0;return n;
        }
        if(g_lastPlanDeferred)return 0;
        r->candidate++;   // Only a completed failed search advances the alternate approach / floor.
    }
    r->routeActive=0;return 0;
}

// Caspian Test16: which objective the infantry brain follows. An active
// local/recovery order (out of a vehicle, failed vehicle route, GoGoGo,
// activity recovery) wins over the objective director while it lasts, as it
// did in Test13; then the director; then the squad's objective.
static int brain_pick_objective(int i,int team,int squad,int *role,int *defend)
{
    int ld=0,lf=local_objective(i,team,&ld);
    *role=BM_OBJECTIVE_NONE;*defend=0;
    if(lf>=0){*defend=ld;return lf;}
    { int od=0,of=squad_order_flag(team,squad,&od); if(of>=0){*defend=od;return of;} }   // AltBotMod V34: the squad's order beats the team's plan
    int f=director_target(i,role);
    if(f>=0){*defend=*role==BM_OBJECTIVE_GUARD;return f;}
    *role=BM_OBJECTIVE_NONE;
    f=squad_objective(team,squad);
    *defend=f>=0 && squad_defending(team,squad);
    return f;
}
// AltBotMod V10: snipers on high ground (sniper_native.inc) and the V9 gunfight state cleared at death
static int sniper_overwatch_tick(int i, BotBrain *b, char *soldier, const float *objective, int key);
static int route_beacon_tick(int i, BotBrain *b, char *soldier, const float *objective);   // AltBotMod V15
static int sniper_holding(int i);
static float sniper_reach(int i); static int is_recon(int i);
static int supply_wait_tick(int i, char *soldier); static int bot_errand_free(int i);   // AltBotMod V11
static void combat_profile_life_reset(int i);
static void brain_tick(int i)
{
    BotBrain *b = bot_brain(i);
    if (!b->enabled) {
        char *pl = H->bots[i], *name = pl + OFF_PLAYER_ONLINE_NAME;
        int want = bm_roster_team(BOT_ROSTER(pl),g_mode,0); (void)name;
        if (want && !*(char **)(pl + OFF_PLAYER_SOLDIER) && player_team(pl) != want && (H->frame + i) % 150 == 0) {
            char ok = ((AddToTeam_t)ADDR_ADD_TO_TEAM)(pl, want, 0, 1);
            bot_log("roster: moved passive '%s' back to team %d (%s)", name, want, ok ? "ok" : "FAILED");
        }
        return;
    }
    if (g_gliding[i] && *(char **)((char *)H->bots[i] + OFF_PLAYER_SOLDIER)) return;
    char *pl = H->bots[i];
    char *soldier = *(char **)(pl + OFF_PLAYER_SOLDIER);
    const char *name = pl + OFF_PLAYER_ONLINE_NAME;
    if (!soldier) {
        g_lastHealth[i] = 0;
        g_rocketsThisLife[i] = 0;
        g_grenadesThisLife[i] = 0;
        g_spareMax[i] = g_launcherMax[i] = 0;
        if (!b->deadSince) {
            nav_cancel_path(i);nav_cancel_path(i+MAX_BOTS);
            memset(&g_flagRoute[i],0,sizeof g_flagRoute[i]);g_overflowFlag[i]=0;g_noRouteSince[i]=0;
            b->deadSince = H->frame ? H->frame : 1;
            b->targetFlag = -1;
            b->enroute = b->waiting = 0;
            b->task = TASK_NONE;   // dying releases any human order: back to the flag loop after respawn
            b->crouched = 0;       // a respawned soldier stands
            b->commander = b->serviceTarget = b->prevCommander = NULL;
            b->prevTask = TASK_NONE;
            bot_nav(i)->mode = 0;
            H->botMove[i][0] = H->botMove[i][2] = 0;
            combat_profile_life_reset(i);   // V10: a grenade half thrown / far-fight crouch did not survive into the next life
        }
        else if (H->frame - b->deadSince > RESPAWN_DELAY_FRAMES &&
                 (!downed_of(pl) || !downed_of(pl)->medic || H->frame - b->deadSince > BOT_REVIVE_WAIT_FRAMES)) {
            b->deadSince = 0;
            g_claymores[i] = g_tugs[i] = g_mines[i] = 0;
            g_lastGadgetPos[i][0] = g_lastGadgetPos[i][1] = 1e9f;
            cmd_spawn(name);
        }
        return;
    }
    b->deadSince = 0;
    // AltBotMod V8: while a new map's grid is still being surveyed the brain runs as usual - objectives, fights,
    // vehicles - on Fun-Bots routes and checked straight legs (step_walk). V7 only ran the stuck watchdog here, and on a
    // first-time map most bots stood at their HQ for the whole survey.
    if (!*nav_slot()) {
        if(!activity_grid_tick(i,soldier))memset(H->botMove[i],0,sizeof H->botMove[i]);
        return;
    }
    if ((*nav_slot())->phase!=2) {
        static unsigned noted;
        if (!noted || H->frame - noted > 120 * 30) { noted = H->frame ? H->frame : 1; bot_log("brain: navigation grid still building - bots play on Fun-Bots routes and checked straight walks meanwhile (%u legs so far)", g_stepWalks); }
    }
    // Let a checked escape complete before objective planning replaces its
    // owner slot. The same route is also usable while the grid is surveying.
    if(activity_grid_tick(i,soldier))return;
    // Newly deployed bots give the asynchronous vehicle inventory a chance to
    // publish before running away from deployment. Never wait indefinitely.
    if(mode_uses_vehicles() && g_enhanced.returnToBase && g_enhanced.driving && !g_driveOff &&
       g_compEpoch!=g_roundEpoch && H->frame-g_lastSpawnedAt[i]<10*30 && b->task==TASK_NONE){
        bot_nav(i)->mode=0;memset(H->botMove[i],0,sizeof H->botMove[i]);return;
    }
    {   // Stance (soldier +0x2d0: 0 standing, 2 prone; +0x2d4 the stance it is changing to). Bots never go prone on
        // purpose - revives and some falls leave them there. Jump stands a soldier up (one-way, unlike the prone key,
        // which is a toggle and could put it back down). Crouch is held, not toggled: it ends by itself.

        int pose = *(int *)(soldier + OFF_SOLDIER_POSE), want = *(int *)(soldier + OFF_SOLDIER_POSE + 4);
        if (pose == 1 && want == 1 && (bot_nav(i)->mode == 2 || bot_nav(i)->mode == 3) && H->frame >= g_local_brain_tick_nextTry[i] && !g_crouchWalkUntil[i]) {   // crouched, setting off
            press_button(i, EIA_JUMP, 4);   // up first (space), then walk
            g_local_brain_tick_nextTry[i] = H->frame + 30;
            b->crouched = 0;
        }
        if (pose == 2 && want == 2 && H->frame >= g_local_brain_tick_nextTry[i] && !g_coverProne[i] && !b->crouched) {   // (not one lying on its spot)
            press_button(i, EIA_JUMP, 4);
            g_local_brain_tick_nextTry[i] = H->frame + 2 * 30;
            bot_log("brain: %s is prone - jumps to stand up", name);
        }
    }
    if (b->sayNext && H->frame >= b->sayAt) { bot_say(i, b->sayNext); b->sayNext = 0; }
    stranded_in_vehicle_tick(i);
    // AltBotMod V15: not out of a stationary weapon - emplacement_tick decides when its gunner leaves (40 s with nothing
    // to shoot, or an order). V5-V14 threw every bot with an even slot number out of its TOW / Kornet / C-RAM / Pantsir
    // within 2 s, and it manned it again a second later: on 2026-10-11 2,354 of 2,380 mannings lasted under 3 s.
    // (Before the scans below: a gunner seated there takes no fight / cover / revive / rocket jobs on foot; the gunner code aims.)
    if (stationary_weapon(player_vehicle(pl)) || vehicle_is_gunship(player_vehicle(pl))) { g_local_brain_tick_wasRiding[i] = 1; return; }   // (V23: and the gunship's guns)
    {   // anywhere worth running to, at a run: whatever the bot is walking to (a flag, a vehicle, a player it follows, a body
        // to revive, a supply run, a post) more than 20 m off, it sprints - not in a fight or lining up a rocket, not
        // crouched / prone, not in a vehicle. The last 20 m at a walk so it does not overrun.
        BotNav *sn = bot_nav(i);
        int pose = *(int *)(soldier + OFF_SOLDIER_POSE);
        if ((sn->mode == 2 || sn->mode == 3) && b->task != TASK_FIGHT && b->task != TASK_ROCKET && pose == 0 && !player_vehicle(H->bots[i])) {
            float gx = sn->x, gz = sn->z;
            if (sn->mode == 3) path_goal(i, &gx, &gz);
            float *me = (float *)(soldier + OFF_SOLDIER_POSITION), dx = gx - me[0], dz = gz - me[2];
            float run = bot_profile(i) ? fmaxf(bot_profile(i)->sprint, 8.0f) : 20.0f;   // V9: elite runs anything over 8 m
            if (dx * dx + dz * dz > run * run) g_sprintUntil[i] = H->frame + 4;
        }
    }
    if (rush_operator_holding(i)) {   // AltBotMod V16.2: arming / defusing - no other job, no fight, no cover, until done or dead
        if (b->task != TASK_NONE) end_task(i, b, "arming / defusing the M-COM");
        mode_brain_tick(i, soldier); return;
    }
    int mcomOperator = rush_operator_focus(i, NULL);   // AltBotMod V16.1: at the M-COM it is arming / defusing - nothing else
    if (mcomOperator && b->task == TASK_FIGHT && rush_operator_focus(i, g_fightTarget[i])) end_task(i, b, "back to the M-COM it is arming / defusing");
    if (!mcomOperator) cover_react(i, soldier);
    if (!mcomOperator && H->frame % 15 == (unsigned)(i % 15)) { rocket_scan(i, soldier); c4_scan(i, soldier); vehicle_threat_scan(i, soldier); repair_scan(i, soldier); revive_scan(i, soldier); }
    if (H->frame % 30 == (unsigned)(i % 30)) self_requests(i, soldier);
    {
        int l, s;
        if (soldier_ammo2(soldier, 0, &l, &s) && s > g_spareMax[i]) g_spareMax[i] = s;
        int ls = launcher_slot(i), la = ls >= 0 ? bot_ammo(i, ls) : -1;
        if (la > g_launcherMax[i]) g_launcherMax[i] = la;
    }
    if (pickup_tick(i, b, soldier)) return;
    if (b->task != TASK_NONE && task_tick(i, b, soldier)) return;
    if (!player_vehicle(pl) && !*(void **)(pl + 0x3e8)) {   // out of a vehicle but not controlling the soldier: re-attach
        ((SetPtr_t)ADDR_PLAYER_ATTACH_SOLDIER)(pl, soldier);
        bot_log("vehicle: re-attached %s to its soldier", name);
    }

    if (player_vehicle(pl)) {   // riding with no one to follow: get out and play
        g_local_brain_tick_wasRiding[i] = 1;
        if (H->frame % 60 == 0) bot_exit_vehicle(i, 1);
        return;
    }
    if (g_local_brain_tick_wasRiding[i]) {   // just got out: plan again from here (a follower keeps following - handled by its task)
        g_local_brain_tick_wasRiding[i] = 0;
        if (b->task == TASK_NONE) choose_local_objective(i, "got out of a vehicle");
    }
    if (b->task == TASK_NONE && supply_wait_tick(i, soldier)) return;   // V11: waiting for the medic / ammo it asked for
    if (!mode_is_cq()) { mode_brain_tick(i,soldier); return; }
    if(g_fleet[i].vehicle && H->frame<g_fleet[i].until && b->task==TASK_NONE) {
        // Keep the reserved vehicle request alive while its search is queued.
        // A flag request for this same bot would otherwise cancel it each tick.
        bot_nav(i)->mode=0;b->enroute=b->waiting=0;g_noRouteSince[i]=0;
        memset(H->botMove[i],0,sizeof H->botMove[i]);return;
    }
    if (!g_flagCount) return;
    int team=player_team(pl),role=BM_OBJECTIVE_NONE;
    int defend=0,sq=brain_pick_objective(i,team,player_squad(pl),&role,&defend);
    int cur = b->targetFlag;
    if (cur >= 0 && flag_owner(cur) == team && role!=BM_OBJECTIVE_PATROL && !(defend && cur == sq)) {
        bot_log("brain: %s's team took flag %d", name, cur);
        g_vehChanceUntil[i] = H->frame + 20 * 30;   // a vehicle for the next leg?
        cur = -1;
        b->waiting = 0;
    }
    if (b->avoidUntil && H->frame > b->avoidUntil) { b->avoidUntil = 0; b->avoidFlag = -1; }
    {   // AltBotMod V10: a sniper overwatches the flag its team goes for from high ground instead of running onto it
        int of = sq >= 0 ? sq : cur;
        if (of < 0 && is_recon(i)) of = pick_flag(team, (float *)(soldier + OFF_SOLDIER_POSITION), b->avoidUntil ? b->avoidFlag : -1);
        if (of >= 0 && route_beacon_tick(i, b, soldier, flag_pos(of))) return;   // AltBotMod V15: a recon sets a radio beacon first
        if (of >= 0 && sniper_overwatch_tick(i, b, soldier, flag_pos(of), of)) { b->targetFlag = of; return; }
    }
    if (H->frame < b->retryAt) return;
    if (sq >= 0 && b->avoidUntil && sq == b->avoidFlag) { sq = -1; defend = 0; }   // cannot reach the squad's flag right now: own choice
    if (sq >= 0 && cur >= 0 && cur != sq) {                        // squad objective changed: re-route
        bot_log("brain: %s follows its squad to %s flag %d", name, defend ? "defend" : "attack", sq);
        cur = -1;
        b->enroute = b->waiting = 0;
    }
    int patrol = (defend || role==BM_OBJECTIVE_PATROL) && cur == sq && b->waiting && H->frame >= b->stepAt;   // guarding: time to move to another spot
    if (cur < 0 || patrol || g_flagRoute[i].pending || ((H->frame+(unsigned)i) % 30 == 0 && !b->enroute && !b->waiting)) {
        float *pos = (float *)(soldier + OFF_SOLDIER_POSITION);
        int f = sq >= 0 ? sq : pick_flag(team, pos, b->avoidUntil ? b->avoidFlag : -1);
        if (f < 0) { b->targetFlag = -1; return; }   // team owns everything reachable: idle
        if (f != cur || patrol || (!b->enroute && !b->waiting)) {
            b->targetFlag = f;
            b->waiting = 0;
            b->enroute = 0;
            g_hasSector[i] = 0;
            // Keep the candidate and floor while queued; completed failures
            // advance the search instead of restarting the same sector forever.
            b->enroute=plan_objective_route(i,soldier,f,defend)>0;
            int deferred=!b->enroute && g_lastPlanDeferred;
            if (deferred && !b->enroute) { b->targetFlag = f; b->retryAt = H->frame + 3;bot_nav(i)->mode=0;memset(H->botMove[i],0,sizeof H->botMove[i]); return; }
            if (b->enroute) { g_noRouteSince[i] = 0; g_escapeFails[i] = 0; }
            else if (!g_noRouteSince[i]) g_noRouteSince[i] = H->frame ? H->frame : 1;
            else if (H->frame - g_noRouteSince[i] > 60 * 30 && H->frame-g_lastSpawnedAt[i]>60*30 && (*nav_slot())->phase==2) {   // sustained genuine failures, after startup grace
                g_noRouteSince[i] = 0;                            // way out of - e.g. dropped onto Seine's lower quay): redeploy
                bot_log("brain: %s is trapped at (%.0f, %.1f, %.0f) - no route for 60 s; replacing its player", name, pos[0], pos[1], pos[2]);
                retire_stuck_bot(i,"no reachable objective");
                return;
            }
            if (b->enroute) {
                if (b->crouched) { bot_stand(i); b->crouched = 0; }   // stand up to move
                bot_nav(i)->mode = 3;
                if (!patrol)
                    bot_log("brain: %s %s flag %d at (%.0f, %.0f), owner %d", name, defend ? "going to guard" : "heading for", f, flag_pos(f)[0],
                            flag_pos(f)[2], flag_owner(f));
            } else {
                bot_log("brain: %s has no route to flag %d, trying others for 10s (%s)", name, f, nav_last_error());
                escape_consider(i, soldier, f);   // AltBotMod V30: cut off (a carrier deck, a rock in the sea): over the side and swim
                b->targetFlag = -1;
                b->avoidFlag = f;
                b->avoidUntil = H->frame + FLAG_AVOID_FRAMES;
                b->retryAt = H->frame + 30;   // at most one attempt per second
            }
        }
    }
    // Caspian V2: aggressive bots sprint to the objective, slowing for the last stretch
    if (g_enhanced.aggression >= 1 && b->enroute && b->task == TASK_NONE && bot_nav(i)->mode == 3 && b->targetFlag >= 0 &&
        dist2((float *)(soldier + OFF_SOLDIER_POSITION), flag_pos(b->targetFlag)) > 35.0f * 35.0f) g_sprintUntil[i] = H->frame + 8;
    // path finished (follower switched to mode 1): hold position until the flag flips
    if (b->enroute && bot_nav(i)->mode == 1) {
        float *me = (float *)(soldier + OFF_SOLDIER_POSITION);
        float fx = flag_pos(b->targetFlag)[0] - me[0], fz = flag_pos(b->targetFlag)[2] - me[2];
        if (fx * fx + fz * fz > (PATROL_RADIUS + 5.0f) * (PATROL_RADIUS + 5.0f)) { b->enroute = 0; b->targetFlag = -1; return; }   // path ended short: re-plan
        b->enroute = 0;
        b->waiting = 1;
        if (g_hasSector[i]) { bot_nav(i)->mode = 1; bot_nav(i)->targetYaw = g_sectorYaw[i]; }   // watch our sector
        if (defend && b->targetFlag == sq) {
            if (!b->crouched) { bot_crouch(i); b->crouched = 1; }   // guard crouched (up again before the patrol moves on)
            {   const BMDifficulty *P = bot_profile(i);   // V9: elite watches 5-9 s, then moves on to another sector
                b->stepAt = H->frame + (P ? (unsigned)((P->guard + P->guardSpread * (rand() % 100) / 100.0f) * 30) : (unsigned)(8 + rand() % 5) * 30); }
        } else {
            if (!b->crouched) { bot_crouch(i); b->crouched = 1; }   // crouch on the flag
            bot_log("brain: %s holding flag %d (owner %d)", name, b->targetFlag, flag_owner(b->targetFlag));
        }
        if(role==BM_OBJECTIVE_PATROL)b->stepAt=H->frame+8*30;
        place_class_gadget(i, soldier);
    }
    if (b->waiting && b->targetFlag >= 0 && flag_owner(b->targetFlag) == team && role!=BM_OBJECTIVE_PATROL && !(defend && b->targetFlag == sq)) b->waiting = 0;
}
static float bot_distance_to(int i, const float *p)
{
    char *s = *(char **)((char *)H->bots[i] + OFF_PLAYER_SOLDIER);
    if (!s) return 1e30f;
    float *me = (float *)(s + OFF_SOLDIER_POSITION), dx = p[0] - me[0], dz = p[2] - me[2];
    return sqrtf(dx * dx + dz * dz);
}

// Loadout slot of the first weapon whose asset name contains any of the given keywords (-1 = not carried).
static int bot_gadget_slot(int i, const char *k1, const char *k2)
{
    LoadoutLib *lib = loadout_lib();
    int idx = g_botLoadout[i];
    if (idx < 0 || idx >= lib->count) return -1;
    Loadout *L = &lib->l[idx];
    for (int sl = 0; sl < 8; sl++) if (g_botGadget[i][sl]) {   // AltBotMod V16: this life's gadget in that slot
        const char *n = asset_name(g_botGadget[i][sl]);
        if (strstr(n, k1) || (k2 && strstr(n, k2))) return sl;
    }
    for (int w = 0; w < L->nWeapons; w++) {
        int sl = *(int *)(L->weaponMsg[w] + 0x38);
        if (sl >= 0 && sl < 8 && g_botGadget[i][sl]) continue;   // (replaced this life)
        const char *n = asset_name(*(void **)(L->weaponMsg[w] + 0x3c));
        if (strstr(n, k1) || (k2 && strstr(n, k2))) return *(int *)(L->weaponMsg[w] + 0x38);
    }
    return -1;
}

// ---- Vehicles ----
static char *player_vehicle(char *pl) { return *(char **)(pl + OFF_PLAYER_VEHICLE); }

// Where a human is: the soldier transform keeps following the seat while in a vehicle (the simulated position freezes).
static float *human_pos(char *pl)
{
    char *s = *(char **)(pl + OFF_PLAYER_SOLDIER);
    return s ? (float *)(s + OFF_SOLDIER_SPAWN_POSITION) : NULL;
}

// Light-vehicle driving state (the driving code is with the tickets, further down)
#define DRIVE_MAX_PTS          256   // AltBotMod V16: 128 (V5-V15) cut a recorded route at ~770 m - long maps drove in stop-go legs
typedef struct DriveState {
    int phase;                          // 0 none, 1 walking to the vehicle, 2 planning, 3 driving,
                                        // 5 squad boarding (walking up), 6 at the wheel waiting for the squad, 7 riding
    char *veh;
    unsigned at, checkAt, reverseUntil, walkUntil;
    int stuckCount, n, next, partial, flag, manual, routeFailures;   // manual: a console driveto (runs with the brain off)
    unsigned combatHoldAt,combatAdvanceUntil;
    unsigned dutyMoveAt;int supportStep;
    unsigned jobAt;                     // capture/support dwell; driver keeps a healthy land vehicle
    unsigned routeRetryAt, sensorAt,avoidUntil;
    unsigned failedFlagUntil[MAX_FLAGS], progressAt;float progressPosition[3];
    float avoidPosition[3],avoidRadius;
    int stageCandidate,stageActive;
    unsigned trafficAt,trafficSince,trafficLeaseBackoff;
    float trafficLimit;int trafficWait,trafficFollowing;char *trafficPeer;
    unsigned boardAt;                   // "Get in" called at this frame (waiting for riders)
    int reroutes;                       // stuck re-routes this drive (survives re-planning)
    unsigned backOutUntil, sensorLogAt;            // a boat leaving its slip: straight back first
    unsigned pickupUntil, pickupAskAt;
    unsigned boxedSince;                // nosed up against something since (quick back-off)
    float sideL, sideR, midNear;        // nearest thing the left / middle / right front sensors see (m)
    unsigned char awayOn;               // backing off: swing the nose to awayHeading (away from the blocked side)
    float awayHeading;
    int nudges;                         // quick back-offs at about the same spot (2nd: strike it off and re-route)
    float nudgePos[3];
    float frontNear;                    // nearest thing the front sensors see (m)  // stopped to pick up bots by the road (until), last look for some
    unsigned char backedOut;
    float goal[3], start[3], lastPos[3], reverseSteer, speed, spPos[3];
    unsigned spAt;
    unsigned wantMoveAt, unstickAt, unstickUntil; int unstickCount, unstickStage; float unstickPos[3], unstickFrom[3];   // Caspian V4
    float exitHeading, exitFrom[3], landTgt[3]; unsigned exitProbeAt, landLogAt, boatPlanSince; unsigned char exitSet, landSet;   // Caspian V5 boats
    float vwantS; unsigned vwantAt, vwantHoldUntil; int traceTries, traceContinue;   // AltBotMod V6: smoothed speed target; Fun-Bots vehicle routes tried
    int straight; unsigned coastUntil, cutAt;   // AltBotMod V8: straight-line legs driven (no map yet); rolling re-plan; corner-cut probe
    unsigned char straightRoute; unsigned straightLogAt;   // AltBotMod V9: on a straight no-map route now; last log
    unsigned char funRoute;   // AltBotMod V24: driving a recorded Fun-Bots road now (not the grid / straight legs)
    int joinN; unsigned drivenAt; float brakeNear, turnHeading;
    const char *why; unsigned planCount, planWindowAt, stormLogAt, rearBlockedUntil, planLogAt;
    unsigned statAt, statFrames, brakeFrames[4]; float statSpeed;   // AltBotMod V26: drive statistics (20 s)
    unsigned char pivoting; unsigned pivotAt, holdAt; int holdLogged;   // AltBotMod V27: a tank turning on the spot; waiting its turn to leave   // AltBotMod V26: why the last re-plan; re-plan storm watch   // AltBotMod V25: grid points joining the road at the front of pts; last driving tick; what the brakes answer to
    // AltBotMod V29: the vehicle in this one's lane (limit, which, since when), passing it (side, how far over, until);
    // drive statistics - full-throttle frames, braking by cause, speed lost with the throttle down (not braking), plans
    float laneLimit, passLat, statPrevSpeed, lastThrottle; char *laneBlocker, *passVeh; unsigned laneSince, passUntil, laneAt; signed char passSide;
    unsigned statFull, statBrakeBy[6], statDrops, statPlans, statOffPhase, dropLogAt, statDropFrame; unsigned char onRoadNow;
    unsigned repairHoldAt;   // AltBotMod V11: holding still for an engineer since
    unsigned narrowSince, narrowSeen, trafficBackUntil; unsigned char trafficBacked;   // AltBotMod V10: in a narrow stretch since; head-on back-off
    unsigned char traceAlt; unsigned noRouteSince, straightFallbackAt;   // AltBotMod V14: other Fun-Bots routes tried; no route since; last straight-leg fallback
    int routeExit;   // AltBotMod V15: the recorded exit point already used on this drive (action index + 1)
    float watchAnchor[3]; unsigned watchAt; int watchLevel;   // AltBotMod V17: goal-progress watch (35 m in 35 s, escalating)
    float pts[DRIVE_MAX_PTS * 3];
} DriveState;
static DriveState g_drive[MAX_BOTS];
static int route_exit_tick(int i, DriveState *d, const float *vp);   // AltBotMod V15 (route_actions_native.inc)
static float route_board_bonus(char *veh, int team);
static void tree_apply_drive(void);
// Spots struck off a drive mask where a vehicle got stuck come back after DRIVE_STRIKE_FRAMES (they used to last the
// whole round: later routes went wide round a "wall" that was only a stuck moment), restored from a copy of the mask
// taken before its first strike.
#define DRIVE_STRIKE_FRAMES (5 * 60 * 30)
#define MAX_DRIVE_STRIKES 64
static struct { unsigned char *mask; float x, z, r; unsigned until; } g_strikes[MAX_DRIVE_STRIKES];
static int g_strikeNext;
static unsigned char *g_strikeBase[2], *g_strikeBaseOf[2];
static void drive_strikes_reset(void)   // a mask rebuilt (new map): its strikes and copies are gone too
{
    memset(g_strikes, 0, sizeof g_strikes);
    for (int s = 0; s < 2; s++) { if (g_strikeBase[s]) HeapFree(GetProcessHeap(), 0, g_strikeBase[s]); g_strikeBase[s] = NULL; g_strikeBaseOf[s] = NULL; }
}
static int drive_strike(unsigned char *mask, float x, float z, float r)
{
    NavGrid *g = *nav_slot();
    if (!g || !mask) return 0;
    size_t n = (size_t)g->w * g->h * NAV_LAYERS;
    int slot = g_strikeBaseOf[0] == mask ? 0 : g_strikeBaseOf[1] == mask ? 1 : -1;
    if (slot < 0) {   // first strike on this mask: keep it as it is now
        slot = g_strikeBaseOf[0] && !g_strikeBaseOf[1] ? 1 : 0;
        if (g_strikeBase[slot]) HeapFree(GetProcessHeap(), 0, g_strikeBase[slot]);
        g_strikeBase[slot] = HeapAlloc(GetProcessHeap(), 0, n);
        g_strikeBaseOf[slot] = g_strikeBase[slot] ? mask : NULL;
        if (g_strikeBase[slot]) memcpy(g_strikeBase[slot], mask, n);
    }
    int k = g_strikeNext++ % MAX_DRIVE_STRIKES;
    g_strikes[k].mask = mask; g_strikes[k].x = x; g_strikes[k].z = z; g_strikes[k].r = r; g_strikes[k].until = H->frame + DRIVE_STRIKE_FRAMES;
    return nav_mask_block(g, mask, x, z, r);
}
static void drive_strikes_tick(void)
{
    NavGrid *g = *nav_slot();
    if (!g || H->frame % 30 != 11) return;
    for (int k = 0; k < MAX_DRIVE_STRIKES; k++) {
        if (!g_strikes[k].mask || H->frame < g_strikes[k].until) continue;
        int slot = g_strikeBaseOf[0] == g_strikes[k].mask ? 0 : g_strikeBaseOf[1] == g_strikes[k].mask ? 1 : -1;
        if (slot >= 0) {
            int n = nav_mask_restore(g, g_strikes[k].mask, g_strikeBase[slot], g_strikes[k].x, g_strikes[k].z, g_strikes[k].r);
            bot_log("drive: (%.0f, %.0f) is back on the drivable map (%d node(s), struck 5 minutes ago)", g_strikes[k].x, g_strikes[k].z, n);
        }
        g_strikes[k].mask = NULL;
    }
}
static int g_paveCursor = -1, g_paveCount;   // paving survey of the drive mask (-1 = done / none)
// Vehicle type name ("Growler", "VDVBuggy", "M1A2", "T90", "Ka-60"...): VehicleEntityData +0x1a0.
#define OFF_VEHDATA_NAME 0x1a0
static const char *vehicle_name(char *veh)
{
    char *data = veh && !IsBadReadPtr(veh + 0x14, 4) ? *(char **)(veh + 0x14) : NULL;
    const char *n = data && !IsBadReadPtr(data + OFF_VEHDATA_NAME, 4) ? *(const char **)(data + OFF_VEHDATA_NAME) : NULL;
    return n && (unsigned)n > 0x10000 && !IsBadStringPtrA(n, 64) ? n : "?";
}
// A vehicle's seats (entry components): their count, 0 when the list does not look like one - some vehicle-type
// objects keep something else there, and reading through it crashed the server.
static unsigned char g_driving[MAX_BOTS];   // a bot put in a driver's seat to drive (its inputs go to the vehicle)
static int vehicle_seats(char *veh, char ***out)
{
    if (!veh || IsBadReadPtr(veh + OFF_ENTITY_ENTRIES, 8)) return 0;
    char **b = *(char ***)(veh + OFF_ENTITY_ENTRIES), **e = *(char ***)(veh + OFF_ENTITY_ENTRIES + 4);
    int n = (int)(e - b);
    if (!b || n <= 0 || n > 16 || IsBadReadPtr(b, n * 4)) return 0;
    for (int k = 0; k < n; k++) if (!b[k] || IsBadReadPtr(b[k] + 0x4c, 4)) return 0;
    *out = b;
    return n;
}
// Free seat index (passenger seats first, driver last), -1 if full.
static int free_seat(char *veh)
{
    char **b;
    int n = vehicle_seats(veh, &b);
    if (!n) return -1;
    for (int k = 1; k < n; k++) if (!*(void **)(b[k] + 0x4c)) return k;
    return !*(void **)(b[0] + 0x4c) ? 0 : -1;
}

static int free_seat_count(char *veh)
{
    char **b;
    int n = vehicle_seats(veh, &b), free = 0;
    for (int k = 0; k < n; k++) if (!*(void **)(b[k] + 0x4c)) free++;
    return free;
}
// "Get in": a bot that has not boarded within this time, or finds the vehicle full, goes back to its own business
#define GETIN_BOARD_FRAMES     (12 * 30)
static unsigned g_boardUntil[MAX_BOTS];

typedef char (__attribute__((thiscall)) *EnterVehicle_t)(void *player, void *vehicle, int seat);
static int bot_enter_vehicle(int i, char *veh)
{
    int seat = free_seat(veh);
    if (seat < 0) { bot_log("vehicle: %s finds no free seat", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME); return 0; }
    bot_nav(i)->mode = 1;
    H->botMove[i][0] = H->botMove[i][1] = H->botMove[i][2] = 0;
    ((EnterVehicle_t)ADDR_PLAYER_ENTER_VEHICLE)(H->bots[i], veh, seat);
    bot_log("vehicle: %s enters seat %d (now in %p)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, seat, player_vehicle(H->bots[i]));
    return player_vehicle(H->bots[i]) != NULL;
}

static int vehicle_seats(char *veh, char ***entries);
static int passenger_dropout_tick(int i, char *veh);
static void stranded_in_vehicle_tick(int i)
{
    char *pl = H->bots[i], *veh = player_vehicle(pl);
    if (!veh || g_drive[i].phase || H->frame % 15) { if (!veh || g_drive[i].phase) g_strandedSince[i] = 0; return; }
    if (stationary_weapon(veh) || vehicle_is_gunship(veh)) { g_strandedSince[i] = 0; return; }   // Caspian V5: emplacement_tick decides when to leave (V23: the gunship flies itself - its gunners stay)
    if (passenger_dropout_tick(i, veh)) { g_strandedSince[i] = 0; return; }   // AltBotMod V16: cargo dismounts when riding no longer helps
    char **b;
    int n = vehicle_seats(veh, &b), cared = 0;
    for (int k = 0; k < n && !cared; k++) {
        char *occ = *(char **)(b[k] + 0x4c);
        if (!occ || occ == pl) continue;
        if (!is_bot(occ)) cared = 1;   // a human aboard: riding with them is the point
        else { int j = bot_index(occ); if (j >= 0 && g_drive[j].phase && g_drive[j].veh == veh) cared = 1; }   // a bot driving it
    }
    if (cared) { g_strandedSince[i] = 0; return; }
    if (!g_strandedSince[i]) { g_strandedSince[i] = H->frame ? H->frame : 1; return; }
    if (H->frame - g_strandedSince[i] < 3 * 30) return;
    g_strandedSince[i] = 0;
    g_driving[i] = 0;
    bot_log("vehicle: %s is in a %s nobody drives for it - gets out", pl + OFF_PLAYER_ONLINE_NAME, vehicle_name(veh));
    bot_exit_vehicle(i, 1);
}
static unsigned g_exitAt[MAX_BOTS], g_exitRefused;
static int vehicle_seat_of(char *veh, char *pl);
static int vehicle_seats(char *veh, char ***out);
static void bot_exit_vehicle(int i, int parachute)
{
    char *veh = player_vehicle(H->bots[i]);
    if (!veh) return;
    // AltBotMod V31: the game's exit runs only for a bot its vehicle really seats, once a frame. Its exit code reads the seat's
    // player and crashed on an empty seat (V30 live, twice: 0x00e3058d reading player +0x3c0 of NULL) when a helicopter's
    // crew bailed out together - a bot whose vehicle pointer still named the aircraft after it had left the seat.
    char **seats; int nSeats = vehicle_seats(veh, &seats);   // (seats it cannot read - 0: the exit goes ahead as before)
    if ((g_exitAt[i] && g_exitAt[i] == H->frame) || (nSeats > 0 && vehicle_seat_of(veh, H->bots[i]) < 0)) {
        if (g_exitRefused++ < 10) bot_log("vehicle: %s's exit skipped - %s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME,
                                          g_exitAt[i] == H->frame ? "already out this frame" : "no longer in a seat of its vehicle");
        return;
    }
    g_exitAt[i] = H->frame ? H->frame : 1;
    typedef void (__attribute__((thiscall)) *ExitVehicle_t)(void *player, int a, int b);
    ((ExitVehicle_t)ADDR_PLAYER_EXIT_VEHICLE)(H->bots[i], 0, 1);   // same call as the exit button
    // (a fall after the jump is handled by fall_tick: the chute opens once the bot is really falling)
    bot_log("vehicle: %s gets out%s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, parachute ? " (parachute ready)" : "");
}

// ---- Class gadgets placed on arrival (flag / defend post) ----
// Uses per life, reset on spawn: claymores (Support) up to 2, T-UGS (Recon) 1 (counters declared near the brain).

static void place_gadget_now(int i, int slot)
{
    schedule_press(i, SLOT_ACTION(slot), H->frame + 1);   // select the gadget
    schedule_press(i, EIA_FIRE, H->frame + 31);           // place it
    schedule_press(i, SLOT_ACTION(0), H->frame + 66);     // back to the primary
}

static void place_class_gadget(int i, char *soldier)
{
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION);
    float dx = me[0] - g_lastGadgetPos[i][0], dz = me[2] - g_lastGadgetPos[i][1];
    if (dx * dx + dz * dz < 15.0f * 15.0f) return;   // already placed something around here
    int slot;
    const char *what = NULL;
    if ((slot = bot_gadget_slot(i, "Claymore", NULL)) >= 0 && g_claymores[i] < 2) { g_claymores[i]++; what = "claymore"; }
    else if ((slot = bot_gadget_slot(i, "UGS", NULL)) >= 0 && g_tugs[i] < 1) { g_tugs[i]++; what = "T-UGS"; }
    else if ((slot = bot_gadget_slot(i, "SOFLAM", NULL)) >= 0 && g_tugs[i] < 1) { g_tugs[i]++; what = "SOFLAM"; }   // AltBotMod V16: it lases enemy vehicles in view for the team's Javelins
    else if ((slot = bot_gadget_slot(i, "M15", NULL)) >= 0 && g_mines[i] < 2) {   // AT mine: only where vehicles drive
        const unsigned char *dm = drive_mask();
        NavGrid *g = *nav_slot();
        float rp[3];
        if (dm && g && nav_mask_nearest(g, dm, me[0], me[2], 3.0f, rp)) { g_mines[i]++; what = "AT mine"; }
    }
    if (!what) return;
    g_lastGadgetPos[i][0] = me[0];
    g_lastGadgetPos[i][1] = me[2];
    place_gadget_now(i, slot);
    bot_log("brain: %s places a %s (slot %d) at (%.1f, %.1f)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, what, slot, me[0], me[2]);
}

static void end_task(int i, BotBrain *b, const char *why)
{
    if (!strcmp(why, "GoGoGo from commander") || !strcmp(why, "their player died")) bot_say_later(i, ORDER_SORRY, SAY_DELAY_FRAMES);
    bot_log("brain: %s ends %s task (%s)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, kTaskNames[b->task], why);
    b->task = TASK_NONE;
    b->commander = b->serviceTarget = NULL;
    b->targetFlag = -1;
    b->enroute = b->waiting = 0;
    bot_nav(i)->mode = 0;
    H->botMove[i][0] = H->botMove[i][2] = 0;
}

// Plans a path to (x, z); if the grid has no route (gaps near objectives), walks straight there instead.
static void go_towards(int i, char *soldier, float x, float y, float z)
{
    int scope=-(bot_brain(i)->task+2);FlagRouteRequest *r=&g_flagRoute[i];
    float dx=x-r->goal[0],dz=z-r->goal[2];
    if(r->pending && r->flag==scope && (dx*dx+dz*dz>400 || fabsf(y-r->goal[1])>3))r->pending=0;
    if (plan_flag_path(i, soldier,scope,x,y,z)) { bot_nav(i)->mode = 3; return; }
    if(g_lastPlanDeferred){bot_nav(i)->mode=0;memset(H->botMove[i],0,sizeof H->botMove[i]);return;}
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION);
    if (H->frame % 150 == 0)
        bot_log("  %s: no grid route from (%.1f, %.1f) to (%.1f, %.1f), walking straight", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME,
                me[0], me[2], x, z);
    bot_nav(i)->mode = 2;
    bot_nav(i)->x = x;
    bot_nav(i)->z = z;
}

// Walks towards the human, stopping `standoff` metres short. Returns the current distance (1e30 if they have no soldier).
static float approach(int i, BotBrain *b, char *soldier, char *human, float standoff)
{
    float *hp = human_pos(human);
    if (!hp) return 1e30f;
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION);
    float dx = hp[0] - me[0], dz = hp[2] - me[2], d = sqrtf(dx * dx + dz * dz);
    if (d <= standoff + 1.0f) {
        bot_nav(i)->mode = 1;
        bot_nav(i)->targetYaw = yaw_towards(dx, dz);   // stand and face them
    } else if (H->frame >= b->replanAt) {
        b->replanAt = H->frame + 60;   // check every 2 s; re-plan only if the human moved > 3 m or we are not moving
        float mx = hp[0] - b->postPos[0], mz = hp[2] - b->postPos[2];
        if (mx * mx + mz * mz > 9.0f || bot_nav(i)->mode == 1) {
            b->postPos[0] = hp[0]; b->postPos[2] = hp[2];   // (postPos doubles as "last planned target" while following)
            go_towards(i, soldier, hp[0] - dx / d * standoff, hp[1], hp[2] - dz / d * standoff);
        }
    }
    return d;
}

static int player_exists(char *pl)
{
    char *pm = player_manager();
    if (!pm || !pl) return 0;
    void **arr = *(void ***)(pm + 0xac);
    unsigned count = *(unsigned *)(pm + 8);
    for (unsigned k = 0; k < count; k++) if (arr[k] == pl) return 1;
    return 0;
}

// ---- Cover: a bot that is hit looks for a spot the shooter cannot see (blocked at chest and head height by
// something close to the spot), runs there, and holds until it has not been hit for a few seconds ----
#define OFF_SOLDIER_HEALTH     0x128
#define COVER_THREAT_RANGE     150.0f

#define COVER_HOLD_FRAMES      (6 * 30)
#define COVER_MAX_FRAMES       (25 * 30)
#define COVER_HUG_DIST         3.0f      // the blocker must be this close to the cover spot
static float g_lastHealth[MAX_BOTS];
static unsigned g_hitAt[MAX_BOTS];
static char *g_threat[MAX_BOTS];
// The player who last damaged each bot, straight from the server's damage handler (hook_soldier_on_damage).
static char *g_attacker[MAX_BOTS];
static unsigned g_attackedAt[MAX_BOTS];
static unsigned g_attackerNoted;
static int is_spotted(char *pl);
static int spotted_cached(char *pl);

static int ray_blocked_char(const float *a, const float *b, float *hit)   // world geometry only (soldiers do not block)
{
    unsigned saved = g_rayFlags;
    g_rayFlags = RAY_DONT_CHECK_CHARACTER;
    int r = ray_cast(a, b, hit, NULL);
    g_rayFlags = saved;
    return r;
}

// Blocked by something that also stops bullets: the ray from the shooter hits, and the ray back from the target hits
// the other side more than COVER_MIN_THICKNESS away (chain-link fences, sheet metal, glass, foliage are thinner).
#define COVER_MIN_THICKNESS    0.15f
static int solid_between(const float *from, const float *to, float *hitOut)
{
    float h1[3], h2[3];
    if (!ray_blocked_char(from, to, h1)) return 0;
    if (hitOut) memcpy(hitOut, h1, 12);
    if (!ray_blocked_char(to, from, h2)) return 0;   // one-sided surface
    float dx = h1[0] - h2[0], dy = h1[1] - h2[1], dz = h1[2] - h2[2];
    return dx * dx + dy * dy + dz * dz > COVER_MIN_THICKNESS * COVER_MIN_THICKNESS;
}

// Nearest enemy with a clear line to the bot: the presumed shooter.
static int fired_recently(char *pl);
static char *likely_shooter(int i, char *soldier, float *eyeOut)
{
    char *pl = H->bots[i], *pm = player_manager();
    if (!pm) return NULL;
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION), best = COVER_THREAT_RANGE;
    char *who = NULL;
    {   // the server told us who it was: no guessing, any range
        char *a = g_attacker[i], *as;
        if (a && H->frame - g_attackedAt[i] < 30 && player_exists(a) && player_team(a) != player_team(pl) && (as = *(char **)(a + OFF_PLAYER_SOLDIER))) {
            float *tp = (float *)(as + OFF_SOLDIER_SPAWN_POSITION);
            if (eyeOut) { eyeOut[0] = tp[0]; eyeOut[1] = tp[1] + 1.6f; eyeOut[2] = tp[2]; }
            return a;
        }
    }
    void **arr = *(void ***)(pm + 0xac);
    unsigned count = *(unsigned *)(pm + 8);
    // pass 0: enemies that fired in the last 1.5 s; pass 1: any enemy with line of sight
    for (int pass = 0; pass < 2 && !who; pass++)
    for (unsigned k = 0; k < count; k++) {
        char *t = arr[k], *ts;
        if (!t || player_team(t) == player_team(pl) || !(ts = *(char **)(t + OFF_PLAYER_SOLDIER))) continue;
        if (pass == 0 && !fired_recently(t)) continue;
        float *tp = (float *)(ts + OFF_SOLDIER_SPAWN_POSITION);
        float d = sqrtf(dist2(tp, me) + (tp[1] - me[1]) * (tp[1] - me[1]));
        if (d >= best) continue;
        float eye[3] = {tp[0], tp[1] + 1.6f, tp[2]}, chest[3] = {me[0], me[1] + 1.2f, me[2]}, hit[3];
        if (pass == 1 && solid_between(eye, chest, hit)) continue;   // someone who just fired needs no line-of-sight test
        best = d; who = t;
        if (eyeOut) memcpy(eyeOut, eye, 12);
    }
    return who;
}

// Best cover spot from a shooter's eye position: walkable, hidden at chest and head height behind something close
// to it, not towards the shooter, reachable. Returns 1 with the spot in out.
// ---- Taking cover - one routine for every threat (a soldier shooting, a vehicle): get COVER_MIN_DIST from the threat
// and behind something solid within COVER_SEARCH of the bot (never the threat's own hull); no cover that close: open
// ground at least COVER_MIN_DIST from it, lying prone. In cover the bot faces the threat and stays until it is dead /
// gone (vehicles: also beyond range or out of sight for 10 s), or - a soldier - until 6 s pass without a hit, then
// re-engages it if it is still there on foot; otherwise back to what it was doing. ----
#define COVER_MIN_DIST         25.0f     // from the threat
#define COVER_SEARCH           30.0f     // from the bot
static float g_coverThreatPos[3];        // set by take_cover for find_cover
static int g_coverThreatIsVeh, g_coverFoundOpen;
static float hdist(const float *a, const float *b) { float dx = a[0] - b[0], dz = a[2] - b[2]; return sqrtf(dx * dx + dz * dz); }
static int find_cover(int i, char *soldier, const float *eye, float *out)
{
    NavGrid *g = *nav_slot();
    if (!g || g->phase != 2) return 0;
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION);
    float myThreatD = sqrtf(dist2(me, eye));
    float cand[80][3], score[80], open[80][3], openScore[80];
    int n = 0, no = 0;
    static const float radii[7] = {0.0f, 5.0f, 10.0f, 15.0f, 20.0f, 25.0f, COVER_SEARCH};
    for (int r = 0; r < 7; r++)
        for (int a = 0; a < (r ? 12 : 1); a++) {
            float ang = a * 0.5235988f + r * 0.26f, pt[3];
            if (!r) memcpy(pt, me, 12);
            else if (!nav_snap(g, me[0] + sinf(ang) * radii[r], me[1], me[2] + cosf(ang) * radii[r], pt)) continue;
            if (fabsf(pt[1] - me[1]) > 3.0f) continue;
            if (hdist(pt, g_coverThreatPos) < COVER_MIN_DIST) continue;   // too close to the threat
            float threatD = sqrtf(dist2(pt, eye));
            if (r && threatD < myThreatD - 2.0f) continue;                 // do not run towards it
            float hit[3], chest[3] = {pt[0], pt[1] + 1.2f, pt[2]}, head[3] = {pt[0], pt[1] + 1.7f, pt[2]};
            int covered = solid_between(eye, chest, hit) && sqrtf(dist2(hit, pt)) <= COVER_HUG_DIST && solid_between(eye, head, NULL);
            if (covered && g_coverThreatIsVeh && hdist(hit, g_coverThreatPos) < 6.0f) covered = 0;   // (the vehicle's own hull)
            if (covered && n < 80) { memcpy(cand[n], pt, 12); score[n] = radii[r] + (myThreatD - threatD) * 0.5f; n++; }
            else if (!covered && no < 80) { memcpy(open[no], pt, 12); openScore[no] = radii[r]; no++; }   // nearest open ground
        }
    for (int pass = 0; pass < 2; pass++) {   // cover first; else open ground (lie prone)
        float (*cs)[3] = pass ? open : cand, *sc = pass ? openScore : score;
        int cn = pass ? no : n;
        for (int tries = 0; tries < 3 && cn > 0; tries++) {
            int best = 0;
            for (int k = 1; k < cn; k++) if (sc[k] < sc[best]) best = k;
            if (dist2(cs[best], me) < 1.5f * 1.5f) { memcpy(out, cs[best], 12); g_coverFoundOpen = pass; bot_nav(i)->mode = 1; return 1; }   // right here
            if (!plan_budget_left()) return 0;
            if (plan_path(i, soldier, cs[best][0], cs[best][1], cs[best][2])) { memcpy(out, cs[best], 12); g_coverFoundOpen = pass; bot_nav(i)->mode = 3; return 1; }
            cs[best][0] = cs[cn - 1][0]; cs[best][1] = cs[cn - 1][1]; cs[best][2] = cs[cn - 1][2]; sc[best] = sc[cn - 1];
            cn--;
        }
    }
    return 0;
}

// ---- Returning fire: on a hit the bot turns to the shooter and spots it. Vehicle shooter: engineers with rockets
// fire back (any health), everyone else takes cover. Infantry shooter: 65%+ health fight back; below that one burst,
// cover, then re-engage; hit twice in cover within 4 s -> leave cover and fight it out ----
// AltBotMod V9: the combat profile this bot plays (Difficulty in enhanced.ini; NULL = V8 combat by Aggression)
static const BMDifficulty *bot_profile(int i)
{
    if (i < 0 || i >= H->botCount || !H->bots[i]) return NULL;
    int level = bm_bot_difficulty(&g_enhanced, (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME);
    return level ? bm_difficulty_level(level) : NULL;
}
static void fight_posture_end(int i);
static void difficulty_status(void)
{
    if (!g_enhanced.difficulty) { bot_log("difficulty: off - V8 combat (Aggression %d)", g_enhanced.aggression); return; }
    const BMDifficulty *P = bm_difficulty_level(g_enhanced.difficulty);
    bot_log("difficulty: %s%s - react %.2f s, burst x%.2f pause x%.2f, fire window %.2f m / %.3f rad, ranges fight/engage/detect %.0f/%.0f/%.0f m, "
            "fov %.0f deg, close to %.0f m, cover below %.0f hp, side steps %.1f, hops %.1f/%.1f s, far crouch >%.0f m %.0f%%, ADS >%.0f m",
            bm_difficulty_name(g_enhanced.difficulty), g_enhanced.randomDifficulty ? " (each bot one level either side)" : "",
            P->reaction, P->burst / 12.0f, P->pause / 10.0f, P->aimMargin, P->aimTol, P->fightRange, P->engageRange, P->detectRange,
            P->fov, fmaxf(P->advanceStop, 6.0f), P->coverBelow, P->strafe, P->jumpFight, P->jumpRun, P->farCrouch, P->farCrouchChance * 100, P->ads);
}
#define FIGHT_HEALTH           65.0f
#define FIGHT_MAX_FRAMES       ((unsigned)(20 * 30 * bm_aggr(&g_enhanced,1,1.25f,1.5f)))   // V8: 30 s at Aggression 2
#define FIGHT_LOST_FRAMES      ((unsigned)(3 * 30 * bm_aggr(&g_enhanced,1,1.6f,2.0f)))   // Caspian V2: aggressive bots keep a target longer
#define FIGHT_BURST_FRAMES     12        // 0.4 s trigger held
#define FIGHT_PAUSE_FRAMES     10        // 0.35 s between bursts
#define FIGHT_AIM_TOLERANCE    0.05f     // rad (~3 deg): widest allowed error, close in
#define FIGHT_AIM_MARGIN       0.35f     // m: fire once the aim is this close to the aim point
#define RECON_AIM_MARGIN       0.12f     // m: recon (snipers) wait for a much steadier aim
#define RECON_SHOT_FRAMES      3         // single shot
#define RECON_PAUSE_FRAMES     20        // ~0.75 s a shot at most (3 + 20 frames): faster spoils the aim
#define FIGHT_RANGE            (g_enhanced.infantryRange * 1.15f * bm_aggr(&g_enhanced,1,1.1f,1.15f))

#define RECON_RANGE_FACTOR     1.5f      // recon see, engage and keep fighting infantry this much further out
static int is_recon(int i) { return strstr((char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, "Recon") != NULL; }
#define RECON_SPOTTED_RANGE    (g_enhanced.reconRange * bm_aggr(&g_enhanced,1,1.2f,1.35f))
// How far a bot keeps fighting a soldier: recon go further, much further for a spotted one.
static float fight_range(int i, char *t)
{
    const BMDifficulty *P = bot_profile(i);
    if (P && !is_recon(i)) {   // AltBotMod V9: the profile's fight range, the weapon's own reach when shorter
        const BMWeaponProfile *w = g_primaryDoctrine[i] ? g_primaryDoctrine[i] : bm_weapon_profile(NULL, 0, 0);
        (void)t;
        return g_enhanced.weaponHandling ? fminf(w->maxRange, P->fightRange * w->rangeFactor) : P->fightRange;
    }
    if(g_enhanced.weaponHandling){(void)t;return bm_weapon_range(&g_enhanced,
        g_primaryDoctrine[i]?g_primaryDoctrine[i]:bm_weapon_profile(NULL,is_recon(i),0))*1.10f;}
    if (!is_recon(i)) return FIGHT_RANGE;
    (void)t;
    return RECON_SPOTTED_RANGE * 1.10f;
}
#define FIGHT_AIM_HEIGHT       0.55f     // above the target's feet (0.9 and 1.2 still went high: the real camera sits above our 1.6 m eye)
#define FIGHT_CHASE_FRAMES     ((unsigned)(6 * 30 * bm_aggr(&g_enhanced,1,1.25f,1.5f)))   // V8: brutal bots chase 9 s
#define ENGAGE_COOLDOWN_V8     ((unsigned)(2 * 30 * bm_aggr(&g_enhanced,1,0.5f,0.25f)))
#define ENGAGE_COOLDOWN_FRAMES engage_cooldown_frames(i)
#define ENGAGE_BREATHER_FRAMES engage_breather_frames(i)
static unsigned engage_cooldown_frames(int i) { const BMDifficulty *P = bot_profile(i); return P ? (unsigned)(P->cooldown * 30 + 0.5f) : ENGAGE_COOLDOWN_V8; }
static unsigned engage_breather_frames(int i) { const BMDifficulty *P = bot_profile(i); return P ? (unsigned)(P->breather * 30 + 0.5f) : 8; }
static char *g_cooldownWho[MAX_BOTS];  // advancing bots look for a target that went out of sight this long
#define FIGHT_ADVANCE_STOP_V8  bm_aggr(&g_enhanced,55.0f,42.0f,32.0f)   // V8: brutal bots close to 32 m (V7: 55)
#define FIGHT_ADVANCE_STOP     (bot_profile(i) ? fmaxf(bot_profile(i)->advanceStop, 6.0f) : FIGHT_ADVANCE_STOP_V8)   // V9: elite 8 m
static unsigned char g_fightAdvance[MAX_BOTS], g_fightChase[MAX_BOTS], g_fightSlot[MAX_BOTS];
static unsigned g_fightMoveUntil[MAX_BOTS], g_engageCooldown[MAX_BOTS];
static float g_fightLast[MAX_BOTS][3], g_fightSpot[MAX_BOTS][3];
static int soldier_ammo2(char *soldier, int slot, int *loaded, int *spare);
static int primary_empty(char *soldier) { int l, s; return soldier_ammo2(soldier, 0, &l, &s) && l + s == 0; }
static char *g_fightTarget[MAX_BOTS];
static unsigned char g_fightRetreat[MAX_BOTS];
static unsigned g_fightSeenAt[MAX_BOTS];
// Caspian V3: self-calibrating aim. The look pitch is computed from an assumed eye height, but the real camera
// sits higher (Test15 found 0.9 m and 1.2 m aim points both went high and settled on 0.55 m). Every bot bullet
// that lands next to its fight target is measured (on_process_hit) and the running offset per shooter stance
// (0 stand, 1 crouch, 2 prone) corrects where the pitch points, so the bullets land on the chest.
static float g_aimBias[3] = {0.45f, 0.45f, 0.45f};   // m the bullets land above the point the pitch was computed for
static float g_aimValue[MAX_BOTS];                    // height above the target's feet the pitch was computed for
static unsigned g_aimValueAt[MAX_BOTS];
static unsigned char g_aimPose[MAX_BOTS];
static unsigned g_aimImpacts, g_aimOnBody, g_aimSamples[3];
static float g_aimSideSum, g_aimRiseSum;
// Caspian V4: closed-loop aim per bot. V3's gundebug log showed LMG bursts climbing from 2.2 m to 4.7 m above a
// target's feet at 43 m (recoil the look pitch never shows) and a steady 0.6 m to one side, so the pitch and yaw are
// corrected from the bot's own impacts: angles (radians) that grow while bullets land high / to one side.
// Vertical: the gun points S + C*t radians above the look pitch, t = frames since the burst began (recoil climb).
// Both are fitted per bot from its impacts (least mean squares) and the pitch is commanded that much lower.
static float g_aimFixV[MAX_BOTS], g_aimClimb[MAX_BOTS], g_aimFixS[MAX_BOTS], g_aimLanding[MAX_BOTS];
// V5: plus a height term (m): the camera's real height differs from the assumed eye. A pure angle learnt at 80 m
// put the bullets into the ground at 10 m ("they shoot the ground quite nicely").
static float g_aimFixH[MAX_BOTS], g_aimFixHAvg = 0.4f;
static unsigned g_burstStart[MAX_BOTS];
static float g_pitchPrevV[MAX_BOTS], g_pitchPrevL[MAX_BOTS], g_pitchRate; static unsigned g_pitchPrevAt[MAX_BOTS], g_pitchRateN;   // V5 pitch response
static float g_pitchHist[MAX_BOTS][16]; static unsigned g_pitchHistAt[MAX_BOTS][16], g_aimUpdAt[MAX_BOTS];   // look pitch by frame (impacts arrive later)
static unsigned char g_aimFixSet[MAX_BOTS];
static float g_aimFixVAvg = 0.02f, g_aimClimbAvg = 0.001f, g_aimFixSAvg;   // what a fresh bot starts from (learned across all bots)
static unsigned g_aimTorso;                          // impacts that would have hit the torso
static int g_fightBursts[MAX_BOTS];
static unsigned g_coverHitAt[MAX_BOTS], g_coverHits[MAX_BOTS];
typedef struct CombatMoveState {
    char *soldier;float anchor[3],goal[3];unsigned stillAt,nextTry,until;int pending,candidate;
} CombatMoveState;
static CombatMoveState g_combatMove[MAX_BOTS];
static void combat_motion_cancel(int i);

static int launcher_slot(int i);
static void start_rocket(int i, char *veh, int slot);
static int bot_spot(char *spotter, char *target);

// Leading moving targets: velocity per bot's current target, smoothed; aim where the target will be when the bullet
// arrives, plus a little for the bot's own turning lag.
#define BULLET_SPEED           600.0f
#define AIM_LAG_SECONDS        0.15f
static float g_leadPos[MAX_BOTS][3], g_leadVel[MAX_BOTS][3];
static void *g_leadWho[MAX_BOTS];
static unsigned g_leadAt[MAX_BOTS];
static void lead_point_speed(int i, void *who, const float *tp, float dist, float speed, float *out)
{
    unsigned now = H->frame;
    if (g_leadWho[i] != who || now - g_leadAt[i] > 15) {
        g_leadWho[i] = who;
        memcpy(g_leadPos[i], tp, 12);
        memset(g_leadVel[i], 0, sizeof g_leadVel[i]);
        g_leadAt[i] = now;
    } else if (now != g_leadAt[i]) {
        float dt = (now - g_leadAt[i]) / 30.0f;
        for (int k = 0; k < 3; k += 2) {   // horizontal only (jumps / terrain bumps would make it bob)
            float v = (tp[k] - g_leadPos[i][k]) / dt;
            if (fabsf(v) > 90.0f) v = 0;   // teleport / respawn (V16: 90 m/s - V5-V15 cut at 30 and never led a fast vehicle)
            g_leadVel[i][k] = 0.5f * g_leadVel[i][k] + 0.5f * v;   // V16: quicker to follow a target that turns (was 0.7 / 0.3)
        }
        memcpy(g_leadPos[i], tp, 12);
        g_leadAt[i] = now;
    }
    float flight, rise;
    bm_ballistics(speed, 0, dist, &flight, &rise);
    float t = flight + AIM_LAG_SECONDS;
    out[0] = tp[0] + g_leadVel[i][0] * t;
    out[1] = tp[1];
    out[2] = tp[2] + g_leadVel[i][2] * t;
}
static void lead_point(int i, void *who, const float *tp, float dist, float *out)
{
    lead_point_speed(i, who, tp, dist, BULLET_SPEED, out);
}

// A spot near the bot where its eyes see the target but something low in front hides its body from it.
static int find_firing_spot(int i, char *soldier, const float *teye, const float *taim, float *out)
{
    NavGrid *g = *nav_slot();
    if (!g || g->phase != 2) return 0;
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION);
    int plans = 0;
    static const float radii[3] = {3.0f, 6.0f, 10.0f};
    for (int r = 0; r < 3; r++)
        for (int a = 0; a < 12; a++) {
            float ang = a * 0.5235988f + r * 0.26f, pt[3], hit[3];
            if (!nav_snap(g, me[0] + sinf(ang) * radii[r], me[1], me[2] + cosf(ang) * radii[r], pt)) continue;
            if (fabsf(pt[1] - me[1]) > 3.0f) continue;
            float eye[3] = {pt[0], pt[1] + 1.6f, pt[2]}, body[3] = {pt[0], pt[1] + 0.9f, pt[2]};
            if (!solid_between(teye, body, hit) || dist2(hit, pt) > 2.5f * 2.5f) continue;   // no low cover in front
            if (solid_between(eye, taim, NULL)) continue;                                   // cannot see it from there
            if (!plan_budget_left()) return 0;
            if (!plan_path(i, soldier, pt[0], pt[1], pt[2])) { if (++plans >= 3) return 0; continue; }   // A* is costly: 3 tries
            memcpy(out, pt, 12);
            return 1;
        }
    return 0;
}

static void start_fight(int i, char *target, int retreat, const char *why)
{
    if (rush_operator_focus(i, target)) return;   // arming / defusing an M-COM: no fight with an enemy over 6 m away
    combat_motion_cancel(i);
    BotBrain *b = bot_brain(i);
    if (b->task == TASK_REPAIR) { b->serviceTarget = NULL; b->task = b->prevTask; }   // drop the repair
    if (b->task == TASK_NONE || b->task == TASK_DEFEND || b->task == TASK_FOLLOW) { b->prevTask = b->task; b->prevCommander = b->commander; }
    b->task = TASK_FIGHT;
    b->step = 0;
    b->stepAt = 0;
    b->waiting = 0;
    b->taskUntil = H->frame + FIGHT_MAX_FRAMES;
    g_fightTarget[i] = target;
    g_fightRetreat[i] = (unsigned char)retreat;
    g_fightSeenAt[i] = H->frame;
    g_fightBursts[i] = 0;
    g_fightAdvance[i] = g_fightChase[i] = 0;
    g_fightMoveUntil[i] = 0;
    bot_nav(i)->mode = 1;
    bot_log("fight: %s fights %s (%s)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, target + OFF_PLAYER_ONLINE_NAME, why);
}

// Take cover from `shooter` (its eye at `eye`) - or from the vehicle `veh` it is in. Returns 1 if the bot is on its way.
static float *vehicle_pos(char *veh, int *teamOut);
static const char *vehicle_name(char *veh);
static int take_cover(int i, char *soldier, char *shooter, char *veh, const float *eye, const char *why)
{
    BotBrain *b = bot_brain(i);
    float spot[3];
    if (veh) { float *vp = vehicle_pos(veh, NULL); if (!vp) return 0; memcpy(g_coverThreatPos, vp, 12); }
    else { g_coverThreatPos[0] = eye[0]; g_coverThreatPos[1] = eye[1] - 1.6f; g_coverThreatPos[2] = eye[2]; }
    g_coverThreatIsVeh = veh != NULL;
    if (!find_cover(i, soldier, eye, spot)) return 0;
    if (b->task == TASK_REPAIR) { press_button(i, SLOT_ACTION(0), 3); b->serviceTarget = NULL; b->task = b->prevTask; }
    if (b->task == TASK_NONE || b->task == TASK_DEFEND || b->task == TASK_FOLLOW) { b->prevTask = b->task; b->prevCommander = b->commander; }
    b->task = TASK_COVER;
    g_threat[i] = shooter;
    g_threatVeh[i] = veh;
    g_threatVehHidden[i] = 0;
    g_coverOpen[i] = (unsigned char)g_coverFoundOpen;
    g_coverProne[i] = 0;
    memcpy(b->supplyPos, spot, 12);
    b->step = 0;
    b->waiting = 0;
    b->enroute = 0;
    b->taskUntil = H->frame + COVER_MAX_FRAMES;
    g_coverHits[i] = 0;
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION);
    bot_log("cover: %s %s %.0f m away, %.0f m from %s%s%s%s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME,
            g_coverFoundOpen ? "gets clear (no cover near) - goes prone" : "takes cover", sqrtf(dist2(spot, me)), hdist(spot, g_coverThreatPos),
            veh ? "an enemy " : "", veh ? vehicle_name(veh) : shooter + OFF_PLAYER_ONLINE_NAME, why ? " - " : "", why ? why : "");
    return 1;
}
static int go_to_cover(int i, char *soldier, char *shooter, const float *eye) { return take_cover(i, soldier, shooter, NULL, eye, NULL); }

// ---- Taking cover after shooting back: when the exchange is going badly ----
#define COVER_BELOW_HEALTH     40.0f
static int g_fightHitsTaken[MAX_BOTS];        // hits taken since we last hurt our target
static float g_fightTgtHp[MAX_BOTS];
static void fight_note_target_health(int i)
{
    char *t = g_fightTarget[i], *ts = t && player_exists(t) ? *(char **)(t + OFF_PLAYER_SOLDIER) : NULL;
    g_fightTgtHp[i] = ts ? *(float *)(ts + OFF_SOLDIER_HEALTH) : 0;
}
static const char *cover_reason(int i, char *soldier, char *shooter)
{
    float h = *(float *)(soldier + OFF_SOLDIER_HEALTH);
    if (h < COVER_BELOW_HEALTH) return "badly hurt";
    {   // losing the exchange: hit again and again while the target is not getting hurt
        char *t = g_fightTarget[i], *ts = t && player_exists(t) ? *(char **)(t + OFF_PLAYER_SOLDIER) : NULL;
        float th = ts ? *(float *)(ts + OFF_SOLDIER_HEALTH) : 0;
        if (ts && th < g_fightTgtHp[i] - 0.5f) { g_fightTgtHp[i] = th; g_fightHitsTaken[i] = 0; }
        else if (g_fightHitsTaken[i] >= 3) return "losing the exchange";
    }
    {   // outnumbered: more than one enemy has been shooting from where it can see us
        char *pm = player_manager();
        void **arr = pm ? *(void ***)(pm + 0xac) : NULL;
        unsigned count = pm ? *(unsigned *)(pm + 8) : 0;
        float *me = (float *)(soldier + OFF_SOLDIER_POSITION), chest[3] = {me[0], me[1] + 1.2f, me[2]};
        int shooters = 0;
        for (unsigned k = 0; k < count; k++) {
            char *e = arr[k], *es;
            if (!e || player_team(e) == player_team(H->bots[i]) || !(es = *(char **)(e + OFF_PLAYER_SOLDIER)) || !fired_recently(e)) continue;
            float *ep = (float *)(es + OFF_SOLDIER_SPAWN_POSITION), eye[3] = {ep[0], ep[1] + 1.6f, ep[2]};
            if (dist2(ep, me) > 100.0f * 100.0f || solid_between(eye, chest, NULL)) continue;
            if (++shooters >= 2) return "outnumbered";
        }
    }
    {   // magazine empty while being shot
        int l, s;
        if (soldier_ammo2(soldier, 0, &l, &s) && l == 0 && s > 0) return "reloading under fire";
    }
    (void)shooter;
    return NULL;
}

static void cover_react(int i, char *soldier)
{
    BotBrain *b = bot_brain(i);
    char *pl = H->bots[i];
    float h = *(float *)(soldier + OFF_SOLDIER_HEALTH);
    int hit = g_lastHealth[i] > 0 && h < g_lastHealth[i] - 0.5f;
    g_lastHealth[i] = h;
    if (!hit || player_vehicle(pl)) return;
    g_hitAt[i] = H->frame;
    if (b->task == TASK_REVIVE) end_revive(i,b,"medic under fire");
    if (b->task == TASK_SUPPLY) return;
    float eye[3];
    char *shooter = likely_shooter(i, soldier, eye);
    if (!shooter) {
        // AltBotMod V9 (as in the dev build): hit by someone unseen - turn on the nearest enemy in reach and fight it
        char *closest = NULL; float best = engage_range(i) * engage_range(i), *me = (float *)(soldier + OFF_SOLDIER_POSITION);
        char *pm = player_manager();
        if (pm && bot_profile(i) && (b->task == TASK_NONE || b->task == TASK_DEFEND || b->task == TASK_FOLLOW)) {
            void **arr = NULL; unsigned count = 0;
            memcpy(&arr, pm + 0xac, sizeof arr); memcpy(&count, pm + 8, sizeof count);
            // V10: only one it can actually see (V9 also turned on one behind a wall - or after a fall - and stood
            // facing the wall until it "lost sight" of it); at most four sight rays
            int rays = 0;
            float eye[3] = {me[0], me[1] + 1.6f, me[2]};
            for (unsigned k = 0; arr && k < count && k < 64 && rays < 4; k++) {
                char *t = arr[k], *ts = t ? *(char **)(t + OFF_PLAYER_SOLDIER) : NULL;
                if (!ts || t == pl || player_team(t) == player_team(pl) || player_vehicle(t) || *(float *)(ts + OFF_SOLDIER_HEALTH) <= 0) continue;
                float *tp = (float *)(ts + OFF_SOLDIER_SPAWN_POSITION), d = dist2(me, tp);
                if (d >= best) continue;
                float chest[3] = {tp[0], tp[1] + 1.2f, tp[2]};
                rays++;
                if (solid_between(eye, chest, NULL)) continue;
                best = d; closest = t;
            }
        }
        if (closest) {
            float *tp = (float *)(*(char **)(closest + OFF_PLAYER_SOLDIER) + OFF_SOLDIER_SPAWN_POSITION);
            bot_nav(i)->targetYaw = yaw_towards(tp[0] - me[0], tp[2] - me[2]);
            start_fight(i, closest, 0, "hit by someone unseen - fights the nearest enemy");
            return;
        }
        bot_log("cover: %s hit (%.0f hp) but sees no shooter", pl + OFF_PLAYER_ONLINE_NAME, h); return;
    }
    // look that way and spot it
    {
        char *ss = *(char **)(shooter + OFF_PLAYER_SOLDIER);
        float *me = (float *)(soldier + OFF_SOLDIER_POSITION);
        if (ss) { float *tp = (float *)(ss + OFF_SOLDIER_SPAWN_POSITION); bot_nav(i)->targetYaw = yaw_towards(tp[0] - me[0], tp[2] - me[2]); }

        if (H->frame - g_local_cover_react_spottedAt[i] > 90) { g_local_cover_react_spottedAt[i] = H->frame; bot_spot(pl, shooter); }
    }
    char *sveh = player_vehicle(shooter);
    if (sveh) {   // shot by a vehicle
        int slot = launcher_slot(i);
        if (slot >= 0 && !(bot_ammo(i, slot) == 0 && g_rocketsThisLife[i] > 0)) {
            if (b->task != TASK_ROCKET) start_rocket(i, sveh, slot);
        } else if (b->task != TASK_COVER) go_to_cover(i, soldier, shooter, eye);
        return;
    }
    if (b->task == TASK_ROCKET) return;   // busy with a vehicle
    {
        char *ss = *(char **)(shooter + OFF_PLAYER_SOLDIER);
        float *me = (float *)(soldier + OFF_SOLDIER_POSITION);
        if (ss) {
            float *tp = (float *)(ss + OFF_SOLDIER_SPAWN_POSITION), dx = tp[0] - me[0], dz = tp[2] - me[2], d = sqrtf(dx * dx + dz * dz);
            if (d > fight_range(i, shooter)) {
                if (b->task != TASK_COVER && b->task != TASK_FIGHT && go_to_cover(i, soldier, shooter, eye))
                    bot_log("cover: %s hit by %s from %.0f m - out of reach, takes cover", pl + OFF_PLAYER_ONLINE_NAME, shooter + OFF_PLAYER_ONLINE_NAME, d);
                return;
            }
        }
    }
    if (b->task == TASK_COVER) {
        if (b->step == 0) return;   // still running for it
        if (H->frame - g_coverHitAt[i] > 4 * 30) g_coverHits[i] = 0;
        g_coverHitAt[i] = H->frame;
        if (++g_coverHits[i] >= 2) start_fight(i, shooter, 0, "cover is not working");
        return;
    }
    if (b->task != TASK_FIGHT) {   // always shoot back first
        {   const BMDifficulty *P = bot_profile(i);   // V9: badly hurt (below the profile's CoverBelowHealth): one burst, then cover
            start_fight(i, shooter, P && h < P->coverBelow, P && h < P->coverBelow ? "returning fire - badly hurt" : "returning fire"); }
        g_fightHitsTaken[i] = 1;
        fight_note_target_health(i);
        return;
    }
    if (g_fightTarget[i] != shooter) {   // already fighting: turn on whoever is shooting us now
        g_fightTarget[i] = shooter; g_fightSeenAt[i] = H->frame; g_fightHitsTaken[i] = 0;
        fight_note_target_health(i);
    }
    g_fightHitsTaken[i]++;
    const char *why = cover_reason(i, soldier, shooter);
    if (why) {
        float eye[3];
        char *ss = *(char **)(shooter + OFF_PLAYER_SOLDIER);
        if (ss) {
            float *sp = (float *)(ss + OFF_SOLDIER_SPAWN_POSITION);
            eye[0] = sp[0]; eye[1] = sp[1] + 1.6f; eye[2] = sp[2];
            if (go_to_cover(i, soldier, shooter, eye)) bot_log("cover: %s breaks for cover (%s)", pl + OFF_PLAYER_ONLINE_NAME, why);
        }
    }
}

static void end_cover(int i, BotBrain *b, const char *why)
{
    char *t = g_threat[i];
    if (g_threatVeh[i]) { g_threatVeh[i] = NULL; g_vehThreatCooldown[i] = H->frame + 5 * 30; }
    g_coverProne[i] = 0;   // (prone in the open: the stance check jumps it back up)
    bot_stand(i);          // (crouched behind cover)
    g_coverOpen[i] = 0;
    b->step = 0;
    b->waiting = 0;
    g_threat[i] = NULL;
    if (t && player_exists(t) && *(char **)(t + OFF_PLAYER_SOLDIER) && !player_vehicle(t) && player_team(t) != player_team(H->bots[i])) {
        char *s = *(char **)(H->bots[i] + OFF_PLAYER_SOLDIER);
        if (s) {   // quiet now: re-engage the one who drove us here (one burst and back to cover again if still hurt)
            float h = *(float *)(s + OFF_SOLDIER_HEALTH);
            (void)h;
            start_fight(i, t, 0, "re-engaging from cover");
            return;
        }
    }
    if (b->prevTask == TASK_DEFEND || (b->prevTask == TASK_FOLLOW && b->prevCommander)) {
        bot_log("cover: %s leaves cover (%s), back to %s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, why, kTaskNames[b->prevTask]);
        b->task = b->prevTask;
        b->commander = b->prevCommander;
        b->prevTask = TASK_NONE;
        b->prevCommander = NULL;
        b->taskUntil = b->task == TASK_DEFEND ? defend_until(i) : H->frame + FOLLOW_FRAMES;   // V15: the defend job's own deadline
        b->replanAt = 0;
        bot_nav(i)->mode = 0;
        return;
    }
    bot_log("cover: %s leaves cover (%s)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, why);
    end_task(i, b, why);
}

static void end_fight(int i, BotBrain *b, const char *why)
{
    combat_motion_cancel(i);
    fight_posture_end(i);   // AltBotMod V9: up from a far-fight crouch, side steps and grenade off
    g_fineAim[i] = 0;
    char *lastTarget = g_fightTarget[i];
    g_fightTarget[i] = NULL;
    g_fightMoveUntil[i] = 0;
    g_cooldownWho[i] = lastTarget;   // cool off on this one; others after a short breather
    g_engageCooldown[i] = H->frame + ENGAGE_COOLDOWN_FRAMES;
    b->step = 0;
    if (b->prevTask == TASK_DEFEND || (b->prevTask == TASK_FOLLOW && b->prevCommander)) {
        bot_log("fight: %s stops (%s), back to %s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, why, kTaskNames[b->prevTask]);
        b->task = b->prevTask;
        b->commander = b->prevCommander;
        b->prevTask = TASK_NONE;
        b->prevCommander = NULL;
        b->taskUntil = b->task == TASK_DEFEND ? defend_until(i) : H->frame + FOLLOW_FRAMES;   // V15: the defend job's own deadline
        b->replanAt = 0;
        bot_nav(i)->mode = 0;
        return;
    }
    bot_log("fight: %s stops (%s)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, why);
    end_task(i, b, why);
}

// step 0: rifle out; 1: aiming; 2: burst (trigger held by botmod_tick); 3: pause
static const BMWeaponProfile *infantry_weapon(int i)
{
    if(g_fightSlot[i]==1)return bm_weapon_profile(NULL,0,1);
    return g_primaryDoctrine[i]?g_primaryDoctrine[i]:bm_weapon_profile(NULL,is_recon(i),0);
}
#include "combat_profile_native.inc"
static int mode_inside(const float *p,float margin);
#include "sniper_native.inc"
static int infantry_trigger_ready(int i,char *soldier)
{
    if(!g_enhanced.weaponHandling)return 1;
    if(!soldier || !g_fightTarget[i] || !player_exists(g_fightTarget[i]))return 0;
    char *target=*(char **)(g_fightTarget[i]+OFF_PLAYER_SOLDIER);
    if(!target || player_team(g_fightTarget[i])==player_team(H->bots[i]))return 0;
    float health=*(float *)(target+OFF_SOLDIER_HEALTH);
    if(!isfinite(health) || health<=0)return 0;
    float distance=sqrtf(dist2((float *)(target+OFF_SOLDIER_SPAWN_POSITION),(float *)(soldier+OFF_SOLDIER_POSITION)));
    float tol=fight_tolerance(i,is_recon(i),distance);
    float yaw=remainderf(bot_nav(i)->targetYaw-*(float *)(soldier+OFF_SOLDIER_YAW),6.2831853f);
    // Release during actual recoil/aim drift; reacquire using the existing
    // closed-loop look inputs. No recoil memory or damage values are changed.
    return distance<=fmaxf(bm_weapon_range(&g_enhanced,infantry_weapon(i))*1.1f,fight_range(i,g_fightTarget[i])) &&
        bm_weapon_stable(yaw,g_aimPitch[i]-*(float *)(soldier+OFF_SOLDIER_PITCH),tol);
}
#include "combat_motion.inc"
static int fight_tick(int i, BotBrain *b, char *soldier)
{
    char *t = g_fightTarget[i];
    if (H->frame > b->taskUntil) { end_fight(i, b, "long enough"); return 0; }
    if (!t || !player_exists(t)) { end_fight(i, b, "target left"); return 0; }
    if (player_team(t) == player_team(H->bots[i])) { end_fight(i, b, "target changed team"); return 0; }
    char *ts = *(char **)(t + OFF_PLAYER_SOLDIER);
    if (!ts || *(float *)(ts + OFF_SOLDIER_HEALTH) <= 0) { end_fight(i, b, "target down"); return 0; }
    if (player_vehicle(t)) { end_fight(i, b, "target got in a vehicle"); return 0; }
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION), *tp = (float *)(ts + OFF_SOLDIER_SPAWN_POSITION);
    g_pitchHist[i][H->frame & 15] = *(float *)(soldier + OFF_SOLDIER_PITCH); g_pitchHistAt[i][H->frame & 15] = H->frame;
    // Caspian V3: stance-aware heights. Our eye: standing 1.6 m, crouched 1.05 m, prone 0.45 m. Where the
    // bullets should land on the target: its chest, then the upper chest, then the hips/legs, for its stance.
    int myPose = *(int *)(soldier + OFF_SOLDIER_POSE), tPose = *(int *)(ts + OFF_SOLDIER_POSE);
    if (myPose < 0 || myPose > 2) myPose = 0;
    if (tPose < 0 || tPose > 2) tPose = 0;
    static const float eyeH[3] = {1.6f, 1.05f, 0.45f};
    static const float land[3][3] = {{1.15f, 1.4f, 0.8f}, {0.8f, 1.0f, 0.55f}, {0.25f, 0.35f, 0.18f}};
    // V5: snipers and marksmen go for the head first (BF3 bolt rifles kill with one head shot), then the chest.
    static const float head[3][3] = {{1.62f, 1.15f, 1.4f}, {1.12f, 0.8f, 1.0f}, {0.33f, 0.25f, 0.35f}};
    const BMWeaponProfile *wp = infantry_weapon(i);
    int sniper = wp && (wp->family == BM_W_BOLT || wp->family == BM_W_DMR) && g_fightSlot[i] == 0;
    // V10: the head only within 150 m; further out the chest (a head at 400 m is a few pixels, a miss every time)
    if (sniper && hdist(me, tp) > 150.0f) sniper = 0;
    const float *pts = sniper ? head[0] : land[0];
    float eye[3] = {me[0], me[1] + eyeH[myPose], me[2]}, aim[3] = {tp[0], tp[1] + pts[tPose * 3], tp[2]}, hit[3];
    float dx = aim[0] - eye[0], dz = aim[2] - eye[2], dh = sqrtf(dx * dx + dz * dz);
    if (dh > fight_range(i, t)) { end_fight(i, b, "out of range"); return 0; }
    // The first landing point actually in sight. (V2 tested 0.55/0.9/1.25 m but pointed the pitch at them
    // directly, and with the camera above our eye the higher two sent the bullets over the head.)
    int visible = 0; float landing = pts[tPose * 3];
    for (int h = 0; h < 3 && !visible; h++) {
        aim[1] = tp[1] + pts[tPose * 3 + h];
        if (!solid_between(eye, aim, hit)) { visible = 1; landing = pts[tPose * 3 + h]; }
    }
    if (g_testVictim && t == g_testVictim) visible = 1;   // test: fire regardless
    if (!g_aimFixSet[i]) { g_aimFixSet[i] = 1; g_aimFixH[i] = g_aimFixHAvg; g_aimFixV[i] = g_aimFixVAvg; g_aimClimb[i] = g_aimClimbAvg; g_aimFixS[i] = g_aimFixSAvg; }
    unsigned firing = b->step == 2 && g_burstStart[i] ? H->frame - g_burstStart[i] + 2 : 0;   // +2: the pitch lags a little
    float aimValue = landing - g_aimBias[myPose] - g_aimFixH[i] - (g_aimFixV[i] + g_aimClimb[i] * (float)(firing > 90 ? 90 : firing)) * dh;
    aim[1] = tp[1] + aimValue;
    g_aimValue[i] = aimValue; g_aimValueAt[i] = H->frame; g_aimPose[i] = (unsigned char)myPose; g_aimLanding[i] = landing;
    if (visible) { g_fightSeenAt[i] = H->frame; memcpy(g_fightLast[i], tp, 12); g_fightChase[i] = 0; }
    else if (H->frame - g_fightSeenAt[i] > ((g_fightAdvance[i] || g_combatMove[i].until) ? FIGHT_CHASE_FRAMES : FIGHT_LOST_FRAMES)) { end_fight(i, b, "lost sight"); return 0; }
    {   // lead a moving target
        float lp[3];
        lead_point(i, t, tp, dh, lp);
        aim[0] = lp[0]; aim[2] = lp[2];
        float tf = dh / BULLET_SPEED;
        aim[1] += 0.5f * 9.81f * tf * tf;   // bullet drop (~0.85 m at 250 m)
        // Caspian V4: lateral correction - move the aim point against the side the bullets have been landing on
        // (side is measured along n = (-dz, dx) / dist in on_process_hit).
        float ux = tp[0] - me[0], uz = tp[2] - me[2], ul = sqrtf(ux * ux + uz * uz) + 1e-6f, shift = g_aimFixS[i] * ul;
        aim[0] -= shift * (-uz / ul); aim[2] -= shift * (ux / ul);
        dx = aim[0] - eye[0]; dz = aim[2] - eye[2]; dh = sqrtf(dx * dx + dz * dz);
    }
    BotNav *nav = bot_nav(i);
    if (g_fightMoveUntil[i]) {   // recon: into the firing position first
        if (sqrtf(dist2(me, g_fightSpot[i])) < 1.2f || nav->mode == 1 || H->frame > g_fightMoveUntil[i]) {
            g_fightMoveUntil[i] = 0;
            bot_log("fight: %s in its firing position", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME);
        } else return 1;
    }
    int relocating=combat_motion_tick(i,b,soldier,tp,dh);
    if(relocating)nav->targetYaw=yaw_towards(dx,dz);
    else if (visible || !g_fightAdvance[i]) {
        if (g_fightAdvance[i] && dh > FIGHT_ADVANCE_STOP) { nav->mode = 4; nav->x = tp[0]; nav->z = tp[2]; }   // close in, facing it
        else nav->mode = 1;
        nav->targetYaw = yaw_towards(dx, dz);
    } else if (!g_fightChase[i]) {   // advancing and it went out of sight: go to where it was last seen
        g_fightChase[i] = 1;
        go_towards(i, soldier, g_fightLast[i][0], g_fightLast[i][1], g_fightLast[i][2]);
        bot_log("fight: %s lost sight of %s, going after it", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, t + OFF_PLAYER_ONLINE_NAME);
    }
    g_aimPitch[i] = atan2f(aim[1] - eye[1], dh);
    if (b->step != 0 && fight_grenade_tick(i, b, soldier, ts, visible, dh)) return 1;   // AltBotMod V9
    if (b->step == 0) {
        g_fightSlot[i] = primary_empty(soldier) ? 1 : 0;   // sidearm when the primary is dry
        press_button(i, SLOT_ACTION(g_fightSlot[i]), 3); b->step = 1;
        {   const BMDifficulty *P = bot_profile(i);   // V9: the profile's reaction (+ aim settle)
            b->stepAt = H->frame + (P ? (unsigned)ceilf(P->reaction * 30) + (unsigned)P->settle
                                      : (unsigned)ceilf(g_enhanced.reactionSeconds * bm_aggr(&g_enhanced,1,0.6f,0.4f) * 30)); }
        fight_posture_start(i, soldier, dh);   // V9: crouch for a far fight
        return 1;
    }
    if (b->step != 2 && fight_sidearm_swap(i, b, soldier, dh, visible)) return 1;   // AltBotMod V9: pistol, not a reload, close in
    if (H->frame - g_hitAt[i] < 3 * 30 && !g_fightMoveUntil[i]) {
        int l, s;
        if (soldier_ammo2(soldier, 0, &l, &s) && l == 0 && s > 0) {
            float teye[3] = {tp[0], tp[1] + 1.6f, tp[2]};
            if (go_to_cover(i, soldier, t, teye)) { bot_log("cover: %s breaks for cover (reloading under fire)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME); return 1; }
        }
    }
    if (b->step == 2 && !visible) { b->step = 1; b->stepAt = H->frame; }
    if (H->frame < b->stepAt) return 1;
    if (b->step == 2) {   // burst done
        g_fightBursts[i]++;
        if (g_fightSlot[i] == 0 && primary_empty(soldier)) {
            g_fightSlot[i] = 1;
            press_button(i, SLOT_ACTION(1), 3);
            bot_log("fight: %s is out of primary ammo, switching to its sidearm", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME);
            b->step = 3; b->stepAt = H->frame + 20;
            return 1;
        }
        if (g_fightRetreat[i]) {   // hurt: that was the burst - now cover (fight on if there is none)
            float teye[3] = {tp[0], tp[1] + 1.6f, tp[2]};
            g_fightRetreat[i] = 0;
            b->task = b->prevTask == TASK_FIGHT ? TASK_NONE : b->task;
            if (go_to_cover(i, soldier, t, teye)) return 1;
            b->task = TASK_FIGHT;
        }
        b->step = 3;
        // Caspian V4: pauses long enough for the recoil to settle (V2/V3 x0.45 kept the muzzle high between bursts)
        {   const BMDifficulty *P = bot_profile(i);
            int wp = g_enhanced.weaponHandling ? bm_weapon_pause(infantry_weapon(i),dh) : bm_infantry_pause(is_recon(i), dh);
            b->stepAt = H->frame + (P ? (unsigned)bm_profile_pause(P, wp) : bm_aggr_frames(&g_enhanced, wp, 0.85f, 0.75f)); }
        return 1;
    }
    if (b->step == 3) b->step = 1;
    // aiming: burst once on target and visible
    float yaw = *(float *)(soldier + OFF_SOLDIER_YAW), ty = fmodf(bot_nav(i)->targetYaw, 6.2831853f);
    if (ty > 0) ty -= 6.2831853f;
    float yerr = fabsf(ty - yaw);
    if (yerr > 3.14159265f) yerr = 6.2831853f - yerr;
    float perr = fabsf(g_aimPitch[i] - *(float *)(soldier + OFF_SOLDIER_PITCH));
    if (g_testVictim && H->frame % 30 == 0)
        bot_log("fight: %s aim yaw err %.3f pitch err %.3f (want %.3f have %.3f) visible %d dist %.1f step %d", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME,
                yerr, perr, g_aimPitch[i], *(float *)(soldier + OFF_SOLDIER_PITCH), visible, dh, b->step);
    int recon = strstr(BOT_ROSTER(H->bots[i]), "Recon") != NULL;
    float tol = fight_tolerance(i, recon, dh);
    g_fineAim[i] = 1;
    if (visible && yerr < tol && perr < tol) {
        b->step = 2; g_burstStart[i] = H->frame ? H->frame : 1;
        // Caspian V4: short controlled bursts. V2/V3 doubled them (an LMG held the trigger 3 s and climbed 2.5 m
        // over the target's head at 43 m); now at most ~0.45 s at range, ~0.6 s mid range, ~0.8 s close in.
        unsigned burst = (unsigned)(g_enhanced.weaponHandling ? bm_weapon_burst(infantry_weapon(i),dh) : bm_infantry_burst(recon, g_fightSlot[i] == 1, dh));
        unsigned cap = dh > 40 ? 4 : dh > 20 ? 7 : 12;   // V5: taps of 1-2 rounds beyond 40 m (recoil climbs ~2.5 mrad a frame)
        const BMDifficulty *P = bot_profile(i);
        if (P && !recon) burst = (unsigned)bm_profile_burst(P, (int)burst, dh);   // V9: elite bursts x1.7 (still capped by range)
        else if (g_enhanced.aggression > 0 && burst > cap) burst = cap;
        b->stepAt = H->frame + (burst ? burst : 1);
    }
    return 1;
}

static int cover_tick(int i, BotBrain *b, char *soldier)
{
    if (H->frame > b->taskUntil) { end_cover(i, b, "took too long"); return 0; }
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION);
    float d = sqrtf(dist2(me, b->supplyPos));
    if (b->step == 0) {
        g_sprintUntil[i] = H->frame + 4;   // sprint (the input flag bits) while running for cover
        if (H->frame % 30 == 0)
            bot_log("cover: %s running, %.1f m to go, nav mode %d, move %.1f/%.1f", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, d,
                    bot_nav(i)->mode, H->botMove[i][0], H->botMove[i][2]);
        if (d < 1.2f || bot_nav(i)->mode == 1) {
            b->step = 1;
            if (g_coverOpen[i] && !g_coverProne[i]) { press_button(i, EIA_PRONE, 3); g_coverProne[i] = 1; }   // no cover here: get low
            else if (!g_coverOpen[i]) bot_crouch(i);                                                         // behind cover: crouch
            bot_log("cover: %s in %s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, g_coverOpen[i] ? "the open, prone" : "cover");
        }
        return 1;
    }
    // in cover: face the threat, stay until quiet
    char *t = g_threat[i];
    char *ts = t && player_exists(t) ? *(char **)(t + OFF_PLAYER_SOLDIER) : NULL;
    bot_nav(i)->mode = 1;
    if (ts) { float *tp = (float *)(ts + OFF_SOLDIER_SPAWN_POSITION); bot_nav(i)->targetYaw = yaw_towards(tp[0] - me[0], tp[2] - me[2]); }
    if (d > 3.0f && !g_coverProne[i]) { b->step = 0; go_towards(i, soldier, b->supplyPos[0], b->supplyPos[1], b->supplyPos[2]); }   // pushed out
    if (g_threatVeh[i]) {   // from a vehicle: until it is destroyed / empty / beyond range / out of sight for 10 s
        const char *gone = vehicle_threat_gone(i, me);
        if (gone) { end_cover(i, b, gone); return 0; }
        return 1;
    }
    if (!ts || *(float *)(ts + OFF_SOLDIER_HEALTH) <= 0) { end_cover(i, b, "threat down"); return 0; }
    if (H->frame - g_hitAt[i] > COVER_HOLD_FRAMES) { end_cover(i, b, "quiet"); return 0; }
    return 1;
}

// ---- Medics revive downed teammates (last 10 s, within ~10 s walk): defib out, look down, hold the trigger ----
static char *revive_threat(int i,char *soldier,const float *body)
{
    char *pm=player_manager();if(!pm)return NULL;void **arr;unsigned count;memcpy(&arr,pm+0xac,sizeof arr);memcpy(&count,pm+8,sizeof count);
    float *me=(float *)(soldier+OFF_SOLDIER_POSITION), casualty[3]={body[0],body[1]+0.5f,body[2]}, approach[3]={me[0],me[1]+1.1f,me[2]};
    int team=player_team(H->bots[i]);
    for(unsigned k=0;k<count&&k<64;k++){char *enemy=arr[k],*es;if(!enemy||player_team(enemy)<=0||player_team(enemy)==team||!(es=*(char **)(enemy+OFF_PLAYER_SOLDIER))||*(float *)(es+OFF_SOLDIER_HEALTH)<=0)continue;
        float *p=(float *)(es+OFF_SOLDIER_SPAWN_POSITION),range=player_vehicle(enemy)?120:45;   /* V11: soldiers 45 m (was 65: 5 revive runs from 110 downs) */ if(dist2(p,body)>range*range&&dist2(p,me)>range*range)continue;
        float eye[3]={p[0],p[1]+1.6f,p[2]};if(!solid_between(eye,casualty,NULL)||!solid_between(eye,approach,NULL))return enemy;
    }return NULL;
}
static void revive_scan(int i, char *soldier)
{
    BotBrain *b = bot_brain(i);
    char *pl = H->bots[i];
    if ((b->task != TASK_NONE && b->task != TASK_DEFEND && b->task != TASK_FOLLOW) || player_vehicle(pl)) return;
    if ((g_hitAt[i] && H->frame-g_hitAt[i]<3*30) || *(float *)(soldier+OFF_SOLDIER_HEALTH)<40) return;
    int slot = bot_gadget_slot(i, "Defib", NULL);
    if (slot < 0) return;
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION), bestD = REVIVE_RANGE;
    Downed *best = NULL;
    for (int d = 0; d < 64; d++) {
        Downed *e = &g_downed[d];
        if (!e->player || e->player == pl || e->team != player_team(pl) || e->medic || H->frame - e->at > DOWNED_WINDOW_FRAMES - 60) continue;
        float dx = e->pos[0] - me[0], dz = e->pos[2] - me[2], dist = sqrtf(dx * dx + dz * dz);
        if (dist >= bestD) continue;   // (V11: range first - the threat check casts up to two rays per enemy)
        if (revive_threat(i,soldier,e->pos)) continue;
        bestD = dist; best = e;
    }
    if (!best) return;
    best->medic = i + 1;
    b->prevTask = b->task;
    b->prevCommander = b->commander;
    b->task = TASK_REVIVE;
    b->serviceTarget = best->player;
    memcpy(b->supplyPos, best->pos, 12);
    b->supplySlot = slot;
    b->step = 0;
    b->stepAt = 0;
    b->replanAt = 0;
    b->waiting = 0;
    b->taskUntil = best->at + DOWNED_WINDOW_FRAMES + 30;
    g_jobStats[JOB_REVIVE_RUN]++;
    bot_log("brain: %s runs to revive %s (%.0f m, down %.1f s ago)", pl + OFF_PLAYER_ONLINE_NAME, best->player + OFF_PLAYER_ONLINE_NAME,
            bestD, (H->frame - best->at) / 30.0f);
}

static void end_revive(int i, BotBrain *b, const char *why)
{
    Downed *e = b->serviceTarget && player_exists(b->serviceTarget) ? downed_of(b->serviceTarget) : NULL;
    if (e && e->medic == i + 1) e->medic = 0;
    b->step = 0;
    b->waiting = 0;
    b->stepAt = 0;
    press_button(i, SLOT_ACTION(0), 3);   // back to the primary weapon
    b->serviceTarget = NULL;
    if (b->prevTask == TASK_DEFEND || (b->prevTask == TASK_FOLLOW && b->prevCommander)) {
        bot_log("brain: %s stops reviving (%s), back to %s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, why, kTaskNames[b->prevTask]);
        b->task = b->prevTask;
        b->commander = b->prevCommander;
        b->prevTask = TASK_NONE;
        b->prevCommander = NULL;
        b->taskUntil = b->task == TASK_DEFEND ? defend_until(i) : H->frame + FOLLOW_FRAMES;   // V15: the defend job's own deadline
        b->replanAt = 0;
        b->crouched = 0;
        bot_nav(i)->mode = 0;
        return;
    }
    end_task(i, b, why);
}

// step: 0 walking, 1 defib coming out, 2 trigger held (charging), 3 released (shock), back to 2 until the window ends
static int revive_tick(int i, BotBrain *b, char *soldier)
{
    char *target = b->serviceTarget;
    if (!target || !player_exists(target)) { b->serviceTarget = NULL; end_revive(i, b, "player left"); return 0; }
    char *ts = *(char **)(target + OFF_PLAYER_SOLDIER);
    if (ts) {   // back up: revived here, or redeployed somewhere else
        int revived = came_back_by_revive(target, ts, b->supplyPos);
        bot_log("brain: %s: %s %s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, target + OFF_PLAYER_ONLINE_NAME, revived ? "revived!" : "redeployed");
        end_revive(i, b, revived ? "revived" : "they redeployed");
        return 0;
    }
    Downed *dn = downed_of(target);
    if (H->frame > b->taskUntil || !dn) { end_revive(i, b, "too late"); return 0; }
    if ((H->frame+i)%15==0 && (revive_threat(i,soldier,dn->pos) || (g_hitAt[i] && H->frame-g_hitAt[i]<3*30))) { end_revive(i,b,"revive exposed to enemy fire"); return 0; }
    memcpy(b->supplyPos, dn->pos, 12);   // the ragdoll may still be sliding
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION);
    float dx = b->supplyPos[0] - me[0], dz = b->supplyPos[2] - me[2], d = sqrtf(dx * dx + dz * dz);
    b->waiting = d <= REVIVE_REACH + 0.7f;
    if (!b->waiting) {
        if (d > 4.0f) {
            if (H->frame >= b->replanAt || bot_nav(i)->mode == 1 || bot_nav(i)->mode == 4) {
                b->replanAt = H->frame + 60;
                go_towards(i, soldier, b->supplyPos[0], b->supplyPos[1], b->supplyPos[2]);
            }
        } else { bot_nav(i)->mode = 4; bot_nav(i)->x = b->supplyPos[0]; bot_nav(i)->z = b->supplyPos[2]; }   // last metres: straight in
        return 1;
    }
    bot_nav(i)->mode = 1;
    bot_nav(i)->targetYaw = yaw_towards(dx, dz);
    if (b->step == 0) {
        press_button(i, SLOT_ACTION(b->supplySlot), 3);   // defibrillator out
        b->step = 1;
        b->stepAt = H->frame + 35;
        bot_log("brain: %s at %s's body, defib out", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, target + OFF_PLAYER_ONLINE_NAME);
    } else if (H->frame >= b->stepAt) {
        if (b->step == 1 || b->step == 3) { b->step = 2; b->stepAt = H->frame + 60; }   // hold ~2 s (charge)
        else { b->step = 3; b->stepAt = H->frame + 20; }                                  // release: shock
    }
    return 1;
}

// ---- Engineers repair damaged, stationary friendly vehicles they can see ----
// Vehicles are found through the players sitting in them (player+0x3d8); position = an occupant's seat transform.
#define OFF_VEHICLE_HEALTH         0x128       // float, current health (tank 1000)
#define OFF_VEHDATA_MAX_HEALTH     0x11c       // float in the vehicle's data (vehicle+0x14)
#define REPAIR_SCAN_RANGE          25.0f       // every engineer this close (with line of sight) helps
#define REPAIR_STANDOFF            2.5f
#define REPAIR_MAX_FRAMES          (60 * 30)
#define REPAIR_CONTACT_DIST        6.0f        // from the vehicle centre: push in and hold the trigger
#define REPAIR_PITCH               -0.44f      // look ~25 degrees down at the hull
typedef struct VehTrack { char *veh; float pos[3]; unsigned at; float speed; float maxSeen; } VehTrack;
static VehTrack g_vehTrack[32];
static unsigned g_repairVt[MAX_BOTS];

static int vehicle_alive(char *veh, unsigned vt)
{
    return veh && !IsBadReadPtr(veh, OFF_VEHICLE_HEALTH + 4) && *(unsigned *)veh == vt;
}

// Vehicle body position (vehicle+0x230 -> body entity, translation at +0x100) and owning team (vehicle+0xd8:
// 1 US, 2 RU, 0 never used). NULL when the vehicle is not placed in the world.
#define OFF_VEHICLE_BODY           0x230
#define OFF_VEHBODY_POSITION       0x100
#define OFF_VEHICLE_TEAM           0xd8
// Seat position of someone inside the vehicle (follows the turret), NULL if it is empty.
static float *vehicle_occupant_pos(char *veh)
{
    char *pm = player_manager();
    if (!pm) return NULL;
    void **arr = *(void ***)(pm + 0xac);
    unsigned count = *(unsigned *)(pm + 8);
    for (unsigned k = 0; k < count; k++) {
        char *pl = arr[k], *s;
        if (pl && *(char **)(pl + OFF_PLAYER_VEHICLE) == veh && (s = *(char **)(pl + OFF_PLAYER_SOLDIER)))
            return (float *)(s + OFF_SOLDIER_SPAWN_POSITION);
    }
    return NULL;
}

// Body position when it is trustworthy (it does not swing with the turret); for some vehicles that field is
// elsewhere, so an occupied vehicle whose body point is > 10 m from its crew uses the crew's seat instead.
static float *vehicle_pos(char *veh, int *team)
{
    if (!veh || IsBadReadPtr(veh, OFF_VEHICLE_BODY + 4)) return NULL;
    if (team) *team = *(int *)(veh + OFF_VEHICLE_TEAM);
    // AltBotMod V33: the physics entity's transform first (+0x30 -> +0x88 -> translation at +0x30). V31 driver traces
    // showed that while a vehicle is driven its body position stays put and the seat position only syncs every ~60 frames,
    // so speed / steering / "stuck" checks saw stop-go (0 m/s, then a jump) although the throttle was held at 1.0.
    // The physics transform moves smoothly every frame. Body / seat stay as the fallback.
    {
        char *phys = !IsBadReadPtr(veh + 0x30, 4) ? *(char **)(veh + 0x30) : NULL;
        char *xf = phys && !IsBadReadPtr(phys + 0x88, 4) ? *(char **)(phys + 0x88) : NULL;
        if (xf && !IsBadReadPtr(xf + 0x30, 12)) {
            float *t = (float *)(xf + 0x30);
            if (isfinite(t[0]) && isfinite(t[1]) && isfinite(t[2]) && (t[0] != 0 || t[2] != 0) &&
                fabsf(t[0]) < 100000.0f && fabsf(t[1]) < 100000.0f && fabsf(t[2]) < 100000.0f) return t;
        }
    }
    float *seat = vehicle_occupant_pos(veh), *p = NULL;
    char *body = *(char **)(veh + OFF_VEHICLE_BODY);
    if (body && !IsBadReadPtr(body + OFF_VEHBODY_POSITION, 12)) {
        p = (float *)(body + OFF_VEHBODY_POSITION);
        if (p[0] == 0 && p[1] == 0 && p[2] == 0) p = NULL;
    }
    if (seat && (!p || (seat[0] - p[0]) * (seat[0] - p[0]) + (seat[2] - p[2]) * (seat[2] - p[2]) > 10.0f * 10.0f)) return seat;
    if (!p) {   // parked (nobody in it, body not simulated): the physics entity's transform (+0x30 -> +0x88 -> translation)
        char *phys = !IsBadReadPtr(veh + 0x30, 4) ? *(char **)(veh + 0x30) : NULL;
        char *xf = phys && !IsBadReadPtr(phys + 0x88, 4) ? *(char **)(phys + 0x88) : NULL;
        if (xf && !IsBadReadPtr(xf + 0x30, 12)) {
            float *t = (float *)(xf + 0x30);
            if (t[0] != 0 || t[2] != 0) p = t;
        }
    }
    return p;
}

static VehTrack *vehicle_track(char *veh)
{
    VehTrack *free = NULL;
    for (int k = 0; k < 32; k++) {
        if (g_vehTrack[k].veh == veh) return &g_vehTrack[k];
        if (!free && (!g_vehTrack[k].veh || H->frame - g_vehTrack[k].at > 300)) free = &g_vehTrack[k];
    }
    if (free) { memset(free, 0, sizeof *free); free->veh = veh; }
    return free;
}

// Samples speed (m/s, over >= 0.5 s) and max health. Returns the track or NULL.
static VehTrack *vehicle_sample(char *veh)
{
    VehTrack *t = vehicle_track(veh);
    float *p = vehicle_pos(veh, NULL);
    if (!t || !p) return t;
    float h = *(float *)(veh + OFF_VEHICLE_HEALTH);
    char *data = *(char **)(veh + 0x14);
    float mx = data && !IsBadReadPtr(data + OFF_VEHDATA_MAX_HEALTH, 4) ? *(float *)(data + OFF_VEHDATA_MAX_HEALTH) : 0;
    if (mx < 1.0f || mx > 100000.0f) mx = 0;
    if (mx > t->maxSeen) t->maxSeen = mx;
    if (h > t->maxSeen) t->maxSeen = h;
    if (!t->at) { memcpy(t->pos, p, 12); t->at = H->frame; t->speed = 0; }
    else if (H->frame - t->at >= 15) {
        float dx = p[0] - t->pos[0], dy = p[1] - t->pos[1], dz = p[2] - t->pos[2];
        t->speed = sqrtf(dx * dx + dy * dy + dz * dz) / ((H->frame - t->at) / 30.0f);
        memcpy(t->pos, p, 12);
        t->at = H->frame;
    }
    return t;
}

// Sends engineer i to repair veh with the repair tool in `slot`; its current order (defend / follow) resumes afterwards.
static void start_repair(int i, char *veh, int slot)
{
    g_jobStats[JOB_REPAIR]++;
    BotBrain *b = bot_brain(i);
    float *vp = vehicle_pos(veh, NULL);
    if (!vp) return;
    b->prevTask = b->task;            // resume this afterwards (like a supply run)
    b->prevCommander = b->commander;
    b->task = TASK_REPAIR;
    b->serviceTarget = veh;
    memcpy(b->supplyPos, vp, 12);
    b->supplySlot = slot;
    b->step = 0;
    b->stepAt = 0;
    b->replanAt = 0;
    b->taskUntil = H->frame + REPAIR_MAX_FRAMES;
    b->enroute = b->waiting = 0;
    g_repairVt[i] = *(unsigned *)veh;
    VehTrack *t = vehicle_track(veh);
    bot_log("brain: %s goes to repair a vehicle %.0f m away (health %.0f / %.0f)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME,
            bot_distance_to(i, vp), *(float *)(veh + OFF_VEHICLE_HEALTH), t ? t->maxSeen : 0.0f);
}

static void repair_scan(int i, char *soldier)
{
    BotBrain *b = bot_brain(i);
    char *pl = H->bots[i];
    // free engineers, and ones holding a post / following someone (they go back to it afterwards)
    if ((b->task != TASK_NONE && b->task != TASK_DEFEND && b->task != TASK_FOLLOW) || player_vehicle(pl)) return;
    int slot = bot_gadget_slot(i, "Repairtool", "RepairTool");
    if (slot < 0) return;
    char **vehs;
    int nveh = vehicle_list(&vehs);
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION), bestD = REPAIR_SCAN_RANGE;
    char *best = NULL;
    for (int k = 0; k < nveh; k++) {
        char *veh = vehs[k];
        int vteam = 0;
        if (IsBadReadPtr(veh, 4) || *(unsigned *)veh != 0x025bf8e8) continue;   // still a ServerVehicleEntity
        float *vp = vehicle_pos(veh, &vteam);
        if (!vp || vteam != player_team(pl)) continue;
        VehTrack *t = vehicle_sample(veh);
        if (!t || !t->maxSeen) continue;
        float h = *(float *)(veh + OFF_VEHICLE_HEALTH);
        if (h <= 0 || h >= 0.99f * t->maxSeen) continue;
        float dx = vp[0] - me[0], dy = vp[1] - me[1], dz = vp[2] - me[2], d = sqrtf(dx * dx + dy * dy + dz * dz);
        // damaged friendly vehicle: say why it is skipped (at most every 5 s per engineer)
        const char *why = t->speed > 0.5f ? "moving" : d >= REPAIR_SCAN_RANGE ? "too far" : NULL;

        if (why) {
            if (H->frame - g_local_repair_scan_whyAt[i] > 150 && d < 2 * REPAIR_SCAN_RANGE) {
                g_local_repair_scan_whyAt[i] = H->frame;
                bot_log("repair: %s skips vehicle %p (%.0f / %.0f, %.0f m, %.1f m/s): %s", pl + OFF_PLAYER_ONLINE_NAME, veh, h, t->maxSeen, d,
                        t->speed, why);
            }
            continue;
        }
        if (d >= bestD) continue;
        float eye[3] = {me[0], me[1] + 1.6f, me[2]}, aim[3] = {vp[0], vp[1] + 0.5f, vp[2]}, hit[3];
        unsigned saved = g_rayFlags;
        g_rayFlags = RAY_DONT_CHECK_CHARACTER;
        int blocked = ray_cast(eye, aim, hit, NULL);
        g_rayFlags = saved;
        if (blocked) {   // the ray may stop on the vehicle's own hull
            float hx = hit[0] - aim[0], hy = hit[1] - aim[1], hz = hit[2] - aim[2];
            if (hx * hx + hy * hy + hz * hz > 6.0f * 6.0f) {

                if (H->frame - g_local_repair_scan_losAt[i] > 150) {
                    g_local_repair_scan_losAt[i] = H->frame;
                    bot_log("repair: %s skips vehicle %p (%.0f m): no line of sight (ray stopped %.1f m from it)", pl + OFF_PLAYER_ONLINE_NAME,
                            veh, d, sqrtf(hx * hx + hy * hy + hz * hz));
                }
                continue;
            }
        }
        bestD = d;
        best = veh;
    }
    if (!best) return;
    start_repair(i, best, slot);
}

static void end_repair(int i, BotBrain *b, const char *why)
{
    b->step = 0;
    b->waiting = 0;
    b->stepAt = 0;
    press_button(i, SLOT_ACTION(0), 3);   // back to the primary weapon
    b->serviceTarget = NULL;
    if (b->prevTask == TASK_DEFEND || (b->prevTask == TASK_FOLLOW && b->prevCommander)) {
        bot_log("brain: %s stops repairing (%s), back to %s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, why, kTaskNames[b->prevTask]);
        b->task = b->prevTask;
        b->commander = b->prevCommander;
        b->prevTask = TASK_NONE;
        b->prevCommander = NULL;
        b->taskUntil = b->task == TASK_DEFEND ? defend_until(i) : H->frame + FOLLOW_FRAMES;   // V15: the defend job's own deadline
        b->replanAt = 0;
        b->crouched = 0;
        bot_nav(i)->mode = 0;
        return;
    }
    end_task(i, b, why);
}

// ---- Engineers fire rockets at enemy ground vehicles they can see (while they have rockets) ----
#define ROCKET_RANGE           200.0f   // AltBotMod V7: 150 -> 200 (7 of 19 V6 engagements ended 'out of range')
#define ROCKET_HUNT_RANGE      400.0f   // V7: an engineer goes after a spotted enemy vehicle this far away
#define ROCKET_SPEED           200.0f    // m/s, for leading moving targets
#define ROCKET_MAX_SHOTS       3
#define ROCKET_AIM_TOLERANCE   0.035f    // rad (~2 deg) yaw and pitch before firing
#define ROCKET_RELOAD_FRAMES   (4 * 30)
#define ROCKET_HULL_HEIGHT     0.7f      // aim this high above the ground under the vehicle (1.0 hit BMP turrets)
#define ROCKET_DROP_START      80.0f     // m: beyond this, aim a little higher for rocket drop
static float g_aimPitch[MAX_BOTS];
static int g_rocketShots[MAX_BOTS];
static unsigned g_rocketVt[MAX_BOTS];

static int vehicle_occupied(char *veh)
{
    char *pm = player_manager();
    if (!pm) return 0;
    void **arr = *(void ***)(pm + 0xac);
    unsigned count = *(unsigned *)(pm + 8);
    for (unsigned k = 0; k < count; k++) if (arr[k] && *(char **)((char *)arr[k] + OFF_PLAYER_VEHICLE) == veh) return 1;
    return 0;
}

// On the ground (a walkable grid node within 3 m under it): not a flying helicopter or jet.
static int vehicle_on_ground(const float *vp)
{
    NavGrid *g = *nav_slot();
    float gp[3];
    return !g || g->phase != 2 || nav_snap(g, vp[0], vp[1], vp[2], gp);
}

// AltBotMod V16: any launcher the bot carries - SMAW / RPG-7 (unguided), FGM-148 Javelin (lock-on, ground vehicles),
// FIM-92 Stinger / SA-18 Igla (lock-on, aircraft).
enum { LAUNCHER_NONE, LAUNCHER_AT, LAUNCHER_JAVELIN, LAUNCHER_AA };
static int launcher_slot(int i)
{
    int s = bot_gadget_slot(i, "SMAW", "RPG7");
    if (s < 0 && g_enhanced.lockOnLaunchers) s = bot_gadget_slot(i, "FGM148", NULL);
    if (s < 0 && g_enhanced.lockOnLaunchers) s = bot_gadget_slot(i, "FIM92", "IGLA");
    return s;
}
static int launcher_kind(int i)
{
    if (bot_gadget_slot(i, "SMAW", "RPG7") >= 0) return LAUNCHER_AT;
    if (!g_enhanced.lockOnLaunchers) return LAUNCHER_NONE;
    if (bot_gadget_slot(i, "FGM148", NULL) >= 0) return LAUNCHER_JAVELIN;
    if (bot_gadget_slot(i, "FIM92", "IGLA") >= 0) return LAUNCHER_AA;
    return LAUNCHER_NONE;
}
// (g_lockSince: declared with the brain prototypes) V16: lock-on launchers hold the sight on the target this long before firing
#define LOCK_FRAMES (int)(2.2f * 30)

static unsigned g_rocketCooldown[MAX_BOTS];
static int g_rocketsThisLife[MAX_BOTS];
// ---- Bots ask for help themselves: "Need ammo" when out of rockets, "Need medic" under half health ----
#define SELF_REQUEST_COOLDOWN  (30 * 30)
#define SELF_HEAL_BELOW        50.0f
static void order_reaction(char *sender, int type, char *target);
// ---- AltBotMod V11: class jobs without being asked. A medic drops a medkit when it or a teammate within 10 m is hurt
// (under 70 hp); a support soldier drops an ammo box when it or a teammate within 10 m is low on magazines. Bots that
// need it step onto the pack (pickup_tick). One drop per 20 s, not while fighting / under fire. ----
#define CLASS_DROP_RADIUS      10.0f
#define CLASS_DROP_FRAMES      (20 * 30)
#define CLASS_HEAL_BELOW       70.0f
static unsigned g_classDropAt[MAX_BOTS], g_classDrops[2];
static char *g_supplyWaitFor[MAX_BOTS];      // a bot that asked for a medkit / ammo waits for this supplier...
static unsigned g_supplyWaitUntil[MAX_BOTS]; // ...until then
static int low_on_ammo(char *soldier, int j)
{
    int l, s;
    if (!soldier_ammo2(soldier, 0, &l, &s)) return 0;
    if (j >= 0 && g_spareMax[j] > 0) return s * 3 < g_spareMax[j];
    return s == 0;   // (a player: its full load is unknown here)
}
static int own_supply_slot(int i, int heal)
{
    int s = heal ? bot_gadget_slot(i, "Medkit", "Medicbag") : bot_gadget_slot(i, "Ammobag", "AmmoBag");
    return s >= 0 && bot_ammo(i, s) != 0 ? s : -1;
}
static void class_support_tick(int i, char *soldier)
{
    if (!g_enhanced.classSupport || !bot_errand_free(i) || (g_hitAt[i] && H->frame - g_hitAt[i] < 3 * 30) || H->frame - g_classDropAt[i] < CLASS_DROP_FRAMES) return;
    for (int heal = 1; heal >= 0; heal--) {
        int slot = own_supply_slot(i, heal);
        if (slot < 0) continue;
        char *pm = player_manager();
        if (!pm) return;
        void **arr = NULL; unsigned count = 0;
        memcpy(&arr, pm + 0xac, sizeof arr); memcpy(&count, pm + 8, sizeof count);
        float *me = (float *)(soldier + OFF_SOLDIER_POSITION), eye[3] = {me[0], me[1] + 1.0f, me[2]};
        int team = player_team(H->bots[i]), needy = 0;
        float face[3] = {0, 0, 0};
        for (unsigned k = 0; arr && k < count && k < 64; k++) {
            char *t = arr[k], *ts = t ? *(char **)(t + OFF_PLAYER_SOLDIER) : NULL;
            if (!ts || player_team(t) != team || player_vehicle(t)) continue;
            float hp = *(float *)(ts + OFF_SOLDIER_HEALTH), *tp = (float *)(ts + OFF_SOLDIER_SPAWN_POSITION);
            if (hp <= 0 || dist2(me, tp) > CLASS_DROP_RADIUS * CLASS_DROP_RADIUS || fabsf(tp[1] - me[1]) > 2.5f) continue;
            int j = bot_index(t);
            if (heal ? hp >= CLASS_HEAL_BELOW : !low_on_ammo(ts, j)) continue;
            float chest[3] = {tp[0], tp[1] + 1.0f, tp[2]};
            if (t != H->bots[i] && solid_between(eye, chest, NULL)) continue;
            if (!needy++ || t != H->bots[i]) memcpy(face, tp, 12);
        }
        if (!needy) continue;
        g_classDropAt[i] = H->frame;
        g_classDrops[heal]++; g_jobStats[heal ? JOB_MEDKIT : JOB_AMMO]++;
        if (dist2(face, me) > 1.0f && bot_nav(i)->mode == 1) bot_nav(i)->targetYaw = yaw_towards(face[0] - me[0], face[2] - me[2]);
        place_gadget_now(i, slot);
        bot_log("%s: %s drops %s for %d teammate(s) within %.0f m", heal ? "medic" : "support", H->bots[i] + OFF_PLAYER_ONLINE_NAME,
                heal ? "a medkit" : "an ammo box", needy, CLASS_DROP_RADIUS);
        return;
    }
}
// A bot that called for a medkit / ammo stops for its supplier (V10: it ran on and the medic chased it until it gave up).
static void supply_wait_start(char *requester, char *supplier)
{
    int r = bot_index(requester);
    if (r < 0) return;
    g_supplyWaitFor[r] = supplier;
    g_supplyWaitUntil[r] = H->frame + 15 * 30;
}
static int supply_wait_tick(int i, char *soldier)
{
    char *sup = g_supplyWaitFor[i];
    if (!sup) return 0;
    int j = bot_index(sup);
    BotBrain *o = j >= 0 ? bot_brain(j) : NULL;
    char *ss = j >= 0 ? *(char **)(sup + OFF_PLAYER_SOLDIER) : NULL;
    if (!o || !ss || o->task != TASK_SUPPLY || o->serviceTarget != H->bots[i] || H->frame > g_supplyWaitUntil[i] ||
        (g_hitAt[i] && H->frame - g_hitAt[i] < 2 * 30) || bot_brain(i)->task != TASK_NONE || player_vehicle(H->bots[i])) {
        g_supplyWaitFor[i] = NULL; return 0;
    }
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION), *sp = (float *)(ss + OFF_SOLDIER_POSITION);
    BotNav *nav = bot_nav(i);
    nav->mode = 1; nav->targetYaw = yaw_towards(sp[0] - me[0], sp[2] - me[2]);
    memset(H->botMove[i], 0, sizeof H->botMove[i]);
    return 1;
}
static void self_requests(int i, char *soldier)
{

    char *pl = H->bots[i];
    if (player_vehicle(pl)) return;
    class_support_tick(i, soldier);   // V11: medics / support look after themselves and those round them
    int slot = launcher_slot(i), pl0, ps0;
    if (own_supply_slot(i, 0) < 0 && soldier_ammo2(soldier, 0, &pl0, &ps0) && ps0 == 0 && g_spareMax[i] > 0 && H->frame - g_local_self_requests_ammoAt[i] > SELF_REQUEST_COOLDOWN) {
        g_local_self_requests_ammoAt[i] = H->frame;
        bot_log("brain: %s has no spare magazines (%d in the gun) - Need ammo", pl + OFF_PLAYER_ONLINE_NAME, pl0);
        bot_say(i, ORDER_REQUESTAMMO);
        order_reaction(pl, ORDER_REQUESTAMMO, NULL);
    }
    if (slot >= 0 && g_rocketsThisLife[i] > 0 && bot_ammo(i, slot) == 0 && H->frame - g_local_self_requests_ammoAt[i] > SELF_REQUEST_COOLDOWN) {
        g_local_self_requests_ammoAt[i] = H->frame;
        bot_log("brain: %s is out of rockets - Need ammo", pl + OFF_PLAYER_ONLINE_NAME);
        bot_say(i, ORDER_REQUESTAMMO);
        order_reaction(pl, ORDER_REQUESTAMMO, NULL);   // other bots answer like they do for players
    }
    float h = *(float *)(soldier + OFF_SOLDIER_HEALTH);
    if (h > 0 && h < SELF_HEAL_BELOW && own_supply_slot(i, 1) < 0 && H->frame - g_local_self_requests_healAt[i] > SELF_REQUEST_COOLDOWN) {   // (a medic heals itself)
        g_local_self_requests_healAt[i] = H->frame;
        bot_log("brain: %s is hurt (%.0f hp) - Need medic", pl + OFF_PLAYER_ONLINE_NAME, h);
        bot_say(i, ORDER_REQUESTHEAL);
        order_reaction(pl, ORDER_REQUESTHEAL, NULL);
    }
}

static int is_entity_spotted(char *ent);
static char *vehicle_occupant(char *veh);
static void rocket_scan(int i, char *soldier)
{
    BotBrain *b = bot_brain(i);
    char *pl = H->bots[i];
    // AltBotMod V6: also from a fight - an engineer shooting its rifle at a tank (or at the soldiers round it) switches
    // to the launcher (V5: rocket_scan only ran for idle bots, so an engineer that saw a tank "stared" at it).
    if ((b->task != TASK_NONE && b->task != TASK_DEFEND && b->task != TASK_FOLLOW && b->task != TASK_FIGHT) || player_vehicle(pl)) return;
    if (H->frame < g_rocketCooldown[i]) return;
    int slot = launcher_slot(i), kind = launcher_kind(i);
    if (slot < 0 || (bot_ammo(i, slot) == 0 && g_rocketsThisLife[i] > 0)) return;   // 0 is only trusted once it has fired
    char **vehs;
    int nveh = vehicle_list(&vehs);
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION), bestD = ROCKET_HUNT_RANGE;
    if (kind == LAUNCHER_AA) {   // AltBotMod V16: a Stinger / Igla at an enemy aircraft in the air it can see
        char *air = NULL; float airD = 650.0f;
        // (V23: the gunship circles high - up to 850 m away)
        for (int k = 0; k < nveh; k++) {
            char *veh = vehs[k]; int vteam = 0;
            if (IsBadReadPtr(veh, 4) || *(unsigned *)veh != 0x025bf8e8 || !vehicle_is_aircraft(veh)) continue;   // (V23: the gunship too)
            float *vp = vehicle_pos(veh, &vteam);
            if (!vp || *(float *)(veh + OFF_VEHICLE_HEALTH) <= 0 || !vehicle_occupied(veh) || vehicle_on_ground(vp)) continue;
            char *crew = vehicle_occupant(veh); if (crew && player_exists(crew)) vteam = player_team(crew);
            if (!vteam || vteam == player_team(pl)) continue;
            float d = sqrtf(dist2(vp, me) + (vp[1] - me[1]) * (vp[1] - me[1]));
            float eye[3] = {me[0], me[1] + 1.6f, me[2]}, hit[3];
            if (d >= (vehicle_is_gunship(veh) ? 850.0f : 650.0f) || (air && d >= airD) || (ray_blocked_char(eye, vp, hit) && dist2(hit, vp) > 8.0f * 8.0f)) continue;
            airD = d; air = veh;
        }
        if (!air) return;
        if (b->task == TASK_FIGHT) end_fight(i, b, "an enemy aircraft in range - switches to the AA launcher");
        start_rocket(i, air, slot);
        bot_log("rocket: %s locks on to an enemy %s %.0f m away (AA launcher)", pl + OFF_PLAYER_ONLINE_NAME, vehicle_name(air), airD);
        return;
    }
    char *best = NULL;
    for (int k = 0; k < nveh; k++) {
        char *veh = vehs[k];
        int vteam = 0;
        if (IsBadReadPtr(veh, 4) || *(unsigned *)veh != 0x025bf8e8) continue;
        float *vp = vehicle_pos(veh, &vteam);
        if (!vp || *(float *)(veh + OFF_VEHICLE_HEALTH) <= 0 || !vehicle_occupied(veh)) continue;
        char *crew = vehicle_occupant(veh);   // V6: the crew's team decides (a vehicle's own team field can be 0 / stale)
        if (crew && player_exists(crew)) vteam = player_team(crew);
        if (!vteam || vteam == player_team(pl)) continue;
        float d = sqrtf(dist2(vp, me));
        if (d >= bestD || !vehicle_on_ground(vp)) continue;
        if (d > 60.0f && !is_entity_spotted(veh)) continue;   // far: identified (spotted) first; close: seeing it is enough
        float eye[3] = {me[0], me[1] + 1.6f, me[2]}, aim[3] = {vp[0], vp[1] + 1.0f, vp[2]}, hit[3];
        if (d > (kind == LAUNCHER_JAVELIN ? 400.0f : ROCKET_RANGE)) {   // AltBotMod V7: a spotted enemy vehicle further off is hunted - at most 2 engineers each
            if (b->task == TASK_FIGHT || d > ROCKET_HUNT_RANGE || !bm_armored(bm_vehicle_profile(vehicle_name(veh)))) continue;   // (armour: jeeps outrun a walk)
            int hunters = 0;
            for (int j = 0; j < H->botCount; j++) if (j != i && bot_brain(j)->task == TASK_ROCKET && bot_brain(j)->serviceTarget == veh) hunters++;
            if (hunters >= 2) continue;
        } else if (ray_blocked_char(eye, aim, hit) && dist2(hit, aim) > 4.0f * 4.0f) continue;   // something in the way (the hull itself is fine)
        bestD = d;
        best = veh;
    }
    if (!best) return;
    if (b->task == TASK_FIGHT) end_fight(i, b, "an enemy vehicle in launcher range - switches to the launcher");
    start_rocket(i, best, slot);
    if (bestD > ROCKET_RANGE) {   // V7: a hunt - time to get there
        b->taskUntil = H->frame + 75 * 30;
        bot_log("rocket: %s goes after an enemy %s %.0f m away", pl + OFF_PLAYER_ONLINE_NAME, vehicle_name(best), bestD);
    }
}

// Engage `veh` with the launcher in `slot` (current order resumes afterwards).
static void start_rocket(int i, char *veh, int slot)
{
    BotBrain *b = bot_brain(i);
    if (b->task == TASK_NONE || b->task == TASK_DEFEND || b->task == TASK_FOLLOW) { b->prevTask = b->task; b->prevCommander = b->commander; }
    b->task = TASK_ROCKET;
    b->serviceTarget = veh;
    b->supplySlot = slot;
    b->step = 0;
    b->stepAt = 0;
    b->taskUntil = H->frame + 30 * 30;
    g_rocketShots[i] = 0;
    g_rocketVt[i] = *(unsigned *)veh;
    g_rocketGrenade[i] = 0;
    bot_nav(i)->mode = 1;
    bot_log("rocket: %s engages an enemy vehicle (%d rocket(s))", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, bot_ammo(i, slot));
}

static unsigned g_rocketCooldown[MAX_BOTS];
#define ROCKET_COOLDOWN_FRAMES (6 * 30)
static void lock_attempt_report(int i, const char *why);
static void end_rocket(int i, BotBrain *b, const char *why)
{
    lock_attempt_report(i, why);   // AltBotMod V28: every lock-on attempt reports what the game's lock did
    g_rocketCooldown[i] = H->frame + ROCKET_COOLDOWN_FRAMES;
    g_c4Mode[i] = 0; g_lockSince[i] = 0;   // V16
    press_button(i, SLOT_ACTION(0), 3);   // rifle back out
    b->step = 0;
    b->serviceTarget = NULL;
    if (b->prevTask == TASK_DEFEND || (b->prevTask == TASK_FOLLOW && b->prevCommander)) {
        bot_log("rocket: %s stops (%s), back to %s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, why, kTaskNames[b->prevTask]);
        b->task = b->prevTask;
        b->commander = b->prevCommander;
        b->prevTask = TASK_NONE;
        b->prevCommander = NULL;
        b->taskUntil = b->task == TASK_DEFEND ? defend_until(i) : H->frame + FOLLOW_FRAMES;   // V15: the defend job's own deadline
        b->replanAt = 0;
        bot_nav(i)->mode = 0;
        return;
    }
    bot_log("rocket: %s stops (%s)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, why);
    end_task(i, b, why);
}

static int grenade_tick(int i, BotBrain *b, char *soldier, char *veh, float *vp);
// AltBotMod V16: C4. A support bot carrying C4 that sees an enemy ground vehicle stopped or crawling within 40 m runs up
// to it, throws two charges onto it, backs off ~15 m and sets them off (the detonator is the secondary-fire / zoom key).
static float g_c4Away[MAX_BOTS][3];
static int c4_tick(int i, BotBrain *b, char *soldier, char *veh, float *vp)
{
    const char *name = (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME;
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION), d = sqrtf(dist2(me, vp));
    VehTrack *t = vehicle_sample(veh);
    if (b->step == 0) {   // run up to it
        if (d > 60.0f || (t && t->speed > 6.0f)) { end_rocket(i, b, d > 60.0f ? "the vehicle drove off" : "the vehicle is moving fast"); return 0; }
        if (d > 4.0f) {
            static unsigned planAt[MAX_BOTS];
            if (!planAt[i] || H->frame - planAt[i] > 2 * 30) {
                if (!plan_budget_left()) return 1;
                planAt[i] = H->frame ? H->frame : 1;
                if (plan_path(i, soldier, vp[0], vp[1], vp[2])) { bot_nav(i)->mode = 3; bot_stand(i); g_sprintUntil[i] = H->frame + 30; }
                else if (!g_lastPlanDeferred) { bot_nav(i)->mode = 2; bot_nav(i)->x = vp[0]; bot_nav(i)->z = vp[2]; }
            }
            return 1;
        }
        bot_nav(i)->mode = 1; memset(H->botMove[i], 0, sizeof H->botMove[i]);
        press_button(i, SLOT_ACTION(b->supplySlot), 3);
        b->step = 1; b->stepAt = H->frame + 30; return 1;
    }
    if (H->frame < b->stepAt) return 1;
    if (b->step == 1 || b->step == 2) {   // throw a charge onto the hull (twice)
        float dx = vp[0] - me[0], dz = vp[2] - me[2];
        bot_nav(i)->mode = 1; bot_nav(i)->targetYaw = yaw_towards(dx, dz); g_aimPitch[i] = -0.35f;
        press_button(i, EIA_FIRE, 3);
        g_rocketShots[i]++; g_rocketsThisLife[i]++;
        bot_log("c4: %s puts C4 %d on the %s", name, b->step, vehicle_name(veh));
        if (b->step == 1) { b->step = 2; b->stepAt = H->frame + 28; return 1; }
        // back off: 15 m away from the vehicle, on the far side from it
        float l = sqrtf(dx * dx + dz * dz) + 1e-3f;
        g_c4Away[i][0] = me[0] - dx / l * 15.0f; g_c4Away[i][1] = me[1]; g_c4Away[i][2] = me[2] - dz / l * 15.0f;
        bot_nav(i)->mode = 2; bot_nav(i)->x = g_c4Away[i][0]; bot_nav(i)->z = g_c4Away[i][2]; g_sprintUntil[i] = H->frame + 60;
        b->step = 3; b->stepAt = H->frame + 3 * 30; return 1;
    }
    if (b->step == 3) {   // far enough (or 3 s on): set them off
        if (d < 12.0f && H->frame < b->stepAt + 2 * 30) { bot_nav(i)->mode = 2; return 1; }
        memset(H->botMove[i], 0, sizeof H->botMove[i]);
        press_button(i, EIA_ZOOM, 4);
        bot_log("c4: %s sets off its C4 (%.0f m from the %s)", name, d, vehicle_name(veh));
        b->step = 4; b->stepAt = H->frame + 20; return 1;
    }
    end_rocket(i, b, "C4 set off");
    return 0;
}
static void c4_scan(int i, char *soldier)
{
    BotBrain *b = bot_brain(i);
    char *pl = H->bots[i];
    if (!g_enhanced.c4Attacks || (b->task != TASK_NONE && b->task != TASK_DEFEND && b->task != TASK_FIGHT) || player_vehicle(pl) || H->frame < g_rocketCooldown[i]) return;
    int slot = bot_gadget_slot(i, "C4", NULL);
    if (slot < 0 || (bot_ammo(i, slot) == 0 && g_rocketsThisLife[i] > 0)) return;
    char **vehs; int n = vehicle_list(&vehs);
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION), bestD = 40.0f; char *best = NULL;
    for (int k = 0; k < n; k++) {
        char *veh = vehs[k]; int vteam = 0;
        if (IsBadReadPtr(veh, 4) || *(unsigned *)veh != 0x025bf8e8 || bm_air_profile(vehicle_name(veh))) continue;
        float *vp = vehicle_pos(veh, &vteam);
        if (!vp || *(float *)(veh + OFF_VEHICLE_HEALTH) <= 0 || !vehicle_occupied(veh) || !vehicle_on_ground(vp)) continue;
        char *crew = vehicle_occupant(veh); if (crew && player_exists(crew)) vteam = player_team(crew);
        if (!vteam || vteam == player_team(pl)) continue;
        VehTrack *t = vehicle_sample(veh);
        float d = sqrtf(dist2(vp, me));
        if (d >= bestD || (t && t->speed > 3.0f) || fabsf(vp[1] - me[1]) > 5.0f) continue;
        float eye[3] = {me[0], me[1] + 1.6f, me[2]}, aim[3] = {vp[0], vp[1] + 1.0f, vp[2]}, hit[3];
        if (ray_blocked_char(eye, aim, hit) && dist2(hit, aim) > 4.0f * 4.0f) continue;
        bestD = d; best = veh;
    }
    if (!best) return;
    if (b->task == TASK_FIGHT) end_fight(i, b, "an enemy vehicle close by - goes for it with C4");
    start_rocket(i, best, slot);
    g_c4Mode[i] = 1; b->taskUntil = H->frame + 25 * 30;
    bot_log("c4: %s goes for the %s %.0f m away with C4", pl + OFF_PLAYER_ONLINE_NAME, vehicle_name(best), bestD);
}
// AltBotMod V16: a lock-on launcher (Javelin at a ground vehicle, Stinger / Igla at an aircraft): launcher out, aim down
// the sight at the target and hold it there (the game locks after ~2 s in the sight), fire, reload, again. The missile
// guides itself, so no lead.
static void lock_attempt_report(int i, const char *why)
{
    if (!g_lockAttempt[i]) return;
    g_lockAttempt[i] = 0; g_lockAttempts++;
    bot_log("lock: %s's attempt ends (%s) after %.1f s - the game's lock: controller seen %.1f s, a target picked %.1f s, best progress %.0f%%%s; weapon pointed %.1f deg off on average (%u attempts, %u fired without a lock, %u game locks so far)",
            (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, why, (H->frame - g_lockAttemptAt[i]) / 30.0f, g_lockSeenFrames[i] / 30.0f, g_lockTargetFrames[i] / 30.0f, g_lockBest[i] * 100.0f,
            g_lockForced[i] ? ", fired anyway after 3 s" : "", g_lockDevN ? g_lockDevSum / g_lockDevN : 0.0f, g_lockAttempts, g_lockForcedShots, g_lockNativeLocks);
    if (g_lockFakeAt[i] && H->frame - g_lockFakeAt[i] < 60 * 30)
        bot_log("lock: %s's lock was put in place %.1f s before the attempt ended (%u locks put in place, %u holders made, %u refused so far)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME,
                (H->frame - g_lockFakeAt[i]) / 30.0f, g_lockFakes, g_lockFakeTargets, g_lockFakeRefused);
}
static int lockon_tick(int i, BotBrain *b, char *soldier, char *veh, float *vp, float *eye, int kind)
{
    if (!g_lockAttempt[i]) { g_lockAttempt[i] = 1; g_lockForced[i] = 0; g_lockTargetFrames[i] = 0; g_lockBest[i] = 0; g_lockSeenFrames[i] = 0; g_lockNativeSince[i] = 0; g_lockAttemptAt[i] = H->frame ? H->frame : 1; }
    float aim[3] = {vp[0], vp[1] + (kind == LAUNCHER_AA ? 0.0f : 1.0f), vp[2]};
    float dx = aim[0] - eye[0], dz = aim[2] - eye[2], dh = sqrtf(dx * dx + dz * dz), d = sqrtf(dh * dh + (aim[1] - eye[1]) * (aim[1] - eye[1]));
    float range = kind == LAUNCHER_AA ? (vehicle_is_gunship(veh) ? 900.0f : 700.0f) : 450.0f, hit[3];
    if (d > range) { end_rocket(i, b, "out of lock-on range"); return 0; }
    if (kind == LAUNCHER_AA && vehicle_on_ground(vp)) { end_rocket(i, b, "the aircraft landed"); return 0; }
    int hidden = ray_blocked_char(eye, aim, hit) && dist2(hit, aim) > 6.0f * 6.0f;
    if (kind == LAUNCHER_JAVELIN && b->step < 2 && (hidden || d > range * 0.9f)) {   // close in on it, as with a rocket
        static unsigned planAt[MAX_BOTS];
        if (!planAt[i] || H->frame - planAt[i] > 4 * 30) {
            if (!plan_budget_left()) return 1;
            planAt[i] = H->frame ? H->frame : 1;
            if (plan_path(i, soldier, vp[0], vp[1], vp[2])) { bot_nav(i)->mode = 3; bot_stand(i); }
            else if (!g_lastPlanDeferred) { end_rocket(i, b, "no way to the vehicle"); return 0; }
        }
        g_lockSince[i] = 0;
        return 1;
    }
    if (kind == LAUNCHER_AA && hidden) { if (!g_lockSince[i]) { end_rocket(i, b, "lost sight of the aircraft"); return 0; } g_lockSince[i] = 0; }
    bot_nav(i)->mode = 1;
    bot_nav(i)->targetYaw = yaw_towards(dx, dz);
    g_aimPitch[i] = atan2f(aim[1] - eye[1], dh) + (kind == LAUNCHER_JAVELIN ? 0.05f : 0.0f);   // (a Javelin climbs: aim a hair high)
    if (b->step == 0) { press_button(i, SLOT_ACTION(b->supplySlot), 3); b->step = 1; b->stepAt = H->frame + 40; g_lockSince[i] = 0; g_lockFirstAt[i] = 0; g_lockTry[i] = 0; return 1; }
    if (H->frame < b->stepAt) return 1;
    if (b->step == 2) {
        int left = bot_ammo(i, b->supplySlot);
        if (left == 0 || g_rocketShots[i] >= ROCKET_MAX_SHOTS) { end_rocket(i, b, left == 0 ? "out of missiles" : "fired enough"); return 0; }
        b->step = 1; g_lockSince[i] = 0;
    }
    // AltBotMod V23: HOW the game launches a lock-on missile for a bot is found out live. V22's log (2026-10-18): 80 tries
    // with the sight held (ADS) and fire pressed after 2.2 s on target - 0 launches. Fun-Bots fires them without the sight:
    // the aim held on the target and fire tapped (0.2 s) over and over. Three ways, each try one of them; a launch is
    // counted when the magazine drops or a new missile appears beside the bot, and the way that launches is used from then on.
    //   0: Fun-Bots - no sight, aim held, fire tapped from the moment the aim is on;
    //   1: the sight held, fire tapped after 2.2 s on target;
    //   2: the sight held, fire held down from 2.2 s on target.
    if (b->step == 1 && !g_lockFirstAt[i] && !g_lockTry[i]) g_lockStrat[i] = (unsigned char)lock_pick_way(), g_lockTry[i] = 1;
    // AltBotMod V25: the game's own lock (on_lock_update): the bot asks for it while its aim is on the target; once the
    // game has the bot's controller, it fires when the game says "locked", without the sight (as Fun-Bots).
    if (b->step == 1 && !hidden) lock_want_ent(i, eye, aim, 4.0f, g_lockSince[i] ? veh : NULL);   // (V29: on it - the lock is put in place after the weapon's lock time)
    float lockProgress = 0; int native = g_lockHooked ? lock_state(i, &lockProgress) : 0;
    if (native) { g_lockStrat[i] = 0; g_lockSeenFrames[i]++; if (lockProgress > g_lockBest[i] && lockProgress <= 1.5f) g_lockBest[i] = lockProgress; }
    // AltBotMod V28: the game has been "locking" 3 s with the aim on the target and no lock: the bot fires anyway (the
    // launcher may launch unguided or not at all - the attempt's report says which), instead of holding until it loses sight
    if (native == 1 && g_lockSince[i]) { if (!g_lockNativeSince[i]) g_lockNativeSince[i] = H->frame ? H->frame : 1; } else if (native != 1) g_lockNativeSince[i] = 0;
    int forced = native == 1 && g_lockNativeSince[i] && H->frame - g_lockNativeSince[i] > 3 * 30;
    if (forced && !g_lockForced[i]) { g_lockForced[i] = 1; g_lockForcedShots++; }
    int way = g_lockStrat[i];
    if (way) press_button(i, EIA_ZOOM, 4);
    float yaw = *(float *)(soldier + OFF_SOLDIER_YAW), ty = fmodf(bot_nav(i)->targetYaw, 6.2831853f);
    if (ty > 0) ty -= 6.2831853f;
    float yerr = fabsf(ty - yaw); if (yerr > 3.14159265f) yerr = 6.2831853f - yerr;
    float perr = fabsf(g_aimPitch[i] - *(float *)(soldier + OFF_SOLDIER_PITCH));
    float tol = kind == LAUNCHER_AA ? 0.07f : 0.05f;
    if (yerr < tol && perr < tol && !hidden) { if (!g_lockSince[i]) g_lockSince[i] = H->frame ? H->frame : 1; }
    else g_lockSince[i] = 0;
    const char *wname = kind == LAUNCHER_AA ? "Stinger / Igla" : "Javelin";
    if (g_lockFirstAt[i]) {   // pressing: did one leave?
        int loaded = 0, spare = 0; soldier_ammo2(soldier, b->supplySlot, &loaded, &spare);
        int byAmmo = loaded + spare < g_lockAmmo[i], byMissile = missile_launched_near(eye, g_lockFirstAt[i]);
        if (byAmmo || byMissile) {
            g_rocketShots[i]++; g_rocketsThisLife[i]++; g_lockLaunches++; g_lockWayWins[way]++; g_lockWayTries[way]++;
            bot_log("rocket: %s's %s is away at the %s %.0f m off - way %d (%s) after %.1f s (seen by %s; %u launches so far)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME,
                    wname, vehicle_name(veh), d, way, kLockWayNames[way], (H->frame - g_lockFirstAt[i]) / 30.0f,
                    byAmmo && byMissile ? "magazine and missile" : byAmmo ? "magazine" : "missile", g_lockLaunches);
            if (g_lockHooked) bot_log("lock: %s's launch was %s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, native == 2 ? "on the game's lock" : g_lockForced[i] ? "WITHOUT a lock (fired after 3 s locking)" : "without the game's controller seen");
            b->step = 2; b->stepAt = H->frame + ROCKET_RELOAD_FRAMES + 30; g_lockSince[i] = 0; g_lockFirstAt[i] = 0; g_lockTry[i] = 0;
            return 1;
        }
        if (H->frame - g_lockFirstAt[i] > 7 * 30) {
            g_lockNoLaunch++; g_lockWayTries[way]++;
            bot_log("rocket: %s - no %s left the tube in 7 s at the %s %.0f m away - way %d (%s); ways so far: %u/%u, %u/%u, %u/%u launched; "
                    "game lock: hook %s, controller %s (state %d, progress %.2f), %u updates / %u matched / %u activated; the weapon pointed %.1f deg off the aim on average (%.0f at most) - corrected",
                    (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, wname, vehicle_name(veh), d, way, kLockWayNames[way],
                    g_lockWayWins[0], g_lockWayTries[0], g_lockWayWins[1], g_lockWayTries[1], g_lockWayWins[2], g_lockWayTries[2],
                    g_lockHooked ? "on" : "OFF", g_lockCtrl[i] ? "seen" : "never seen", native, lockProgress, g_lockHookCalls, g_lockMatches, g_lockActivations,
                    g_lockDevN ? g_lockDevSum / g_lockDevN : 0.0f, g_lockDevMax);
            g_lockFirstAt[i] = 0; g_lockTry[i] = 0; end_rocket(i, b, "no lock");
            return 0;
        }
    }
    int start = way == 0 ? 6 : LOCK_FRAMES;
    if (native == 2 && !g_lockFirstAt[i]) {
        g_lockNativeLocks++;
        bot_log("rocket: %s - the game locks the %s on the %s %.0f m away (%u native locks so far) - fires", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME,
                wname, vehicle_name(veh), d, g_lockNativeLocks);
    }
    if (native == 1 && !forced) { if (g_lockFirstAt[i] && H->frame - g_lockFirstAt[i] > 3 * 30) g_lockFirstAt[i] = H->frame ? H->frame : 1; }   // (still locking: the 7 s count waits)
    else if (b->step == 1 && (native == 2 || forced || (!native && g_lockSince[i] && (int)(H->frame - g_lockSince[i]) >= start))) {
        if (!g_lockFirstAt[i]) {
            int loaded = 0, spare = 0; soldier_ammo2(soldier, b->supplySlot, &loaded, &spare);
            g_lockAmmo[i] = loaded + spare; g_lockFirstAt[i] = H->frame ? H->frame : 1;
        }
        unsigned e = H->frame - g_lockFirstAt[i];
        if (way == 2) press_button(i, EIA_FIRE, 3);            // held down
        else if (e % 14 < 6) press_button(i, EIA_FIRE, 2);     // 0.2 s taps, 0.27 s apart (Fun-Bots: fire 0.2 s, pause 0.2 s)
    }
    return 1;
}
// step 0: launcher out; 1: aiming (fire once on target); 2: reloading
static int rocket_tick(int i, BotBrain *b, char *soldier)
{
    char *veh = b->serviceTarget;
    if (H->frame > b->taskUntil) { end_rocket(i, b, "took too long"); return 0; }
    if (IsBadReadPtr(veh, OFF_VEHICLE_HEALTH + 4) || *(unsigned *)veh != g_rocketVt[i]) { end_rocket(i, b, "vehicle gone"); return 0; }
    int vteam = 0;
    float *vp = vehicle_pos(veh, &vteam);
    if (!vp || *(float *)(veh + OFF_VEHICLE_HEALTH) <= 0) { end_rocket(i, b, "vehicle destroyed"); return 0; }
    { char *crew = vehicle_occupant(veh); if (crew && player_exists(crew)) vteam = player_team(crew); }   // V7: the crew's team
    if (vteam == player_team(H->bots[i]) || !vehicle_occupied(veh)) { end_rocket(i, b, "no longer a threat"); return 0; }
    if (g_rocketGrenade[i]) return grenade_tick(i, b, soldier, veh, vp);
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION);
    float eye[3] = {me[0], me[1] + 1.6f, me[2]};
    if (g_c4Mode[i]) return c4_tick(i, b, soldier, veh, vp);   // AltBotMod V16
    int kind = launcher_kind(i);   // AltBotMod V16
    if (kind == LAUNCHER_AA || kind == LAUNCHER_JAVELIN) return lockon_tick(i, b, soldier, veh, vp, eye, kind);
    // lead the target by its speed over the rocket's flight time
    VehTrack *t = vehicle_sample(veh);
    // aim at the hull: 1 m above the ground under the vehicle (the crew seat / body point can sit high up in a turret)
    float aim[3] = {vp[0], vp[1] + ROCKET_HULL_HEIGHT, vp[2]};
    {
        NavGrid *g = *nav_slot();
        float gp[3];
        if (g && g->phase == 2 && nav_snap(g, vp[0], vp[1] - 1.5f, vp[2], gp) && gp[1] <= vp[1] + 0.5f) aim[1] = gp[1] + ROCKET_HULL_HEIGHT;
    }
    float d = sqrtf(dist2(aim, eye));
    if (d > ROCKET_HUNT_RANGE * 1.2f) { end_rocket(i, b, "out of range"); return 0; }
    {   // AltBotMod V7: too far, or not in sight yet - close in on it (a route re-planned every 4 s), then fire
        static unsigned planAt[MAX_BOTS];
        float hit2[3];
        int hidden = ray_blocked_char(eye, aim, hit2) && dist2(hit2, aim) > 4.0f * 4.0f;
        if (b->step < 2 && (d > ROCKET_RANGE * 0.9f || (hidden && d > 25.0f))) {
            if (!planAt[i] || H->frame - planAt[i] > 4 * 30) {
                if (!plan_budget_left()) return 1;
                planAt[i] = H->frame ? H->frame : 1;
                if (plan_path(i, soldier, vp[0], vp[1], vp[2])) { bot_nav(i)->mode = 3; bot_stand(i); }
                else if (!g_lastPlanDeferred) { end_rocket(i, b, "no way to the vehicle"); return 0; }
            }
            return 1;
        }
    }
    if (t && t->speed > 0.5f && t->at) {


        if (g_local_rocket_tick_lastAt[i] && H->frame > g_local_rocket_tick_lastAt[i]) {
            float dt = (H->frame - g_local_rocket_tick_lastAt[i]) / 30.0f, fly = d / ROCKET_SPEED;
            for (int a = 0; a < 3; a += 2) aim[a] += (vp[a] - g_local_rocket_tick_lastPos[i][a]) / dt * fly;
        }
        memcpy(g_local_rocket_tick_lastPos[i], vp, 12);
        g_local_rocket_tick_lastAt[i] = H->frame;
    }
    float dx = aim[0] - eye[0], dz = aim[2] - eye[2], dh = sqrtf(dx * dx + dz * dz);
    bot_nav(i)->mode = 1;
    bot_nav(i)->targetYaw = yaw_towards(dx, dz);
    g_aimPitch[i] = atan2f(aim[1] - eye[1], dh) + (dh > ROCKET_DROP_START ? (dh - ROCKET_DROP_START) * 0.0004f : 0.0f);   // lift only for long shots
    if (b->step == 0) {
        press_button(i, SLOT_ACTION(b->supplySlot), 3);
        b->step = 1;
        b->stepAt = H->frame + 36;   // launcher up
        return 1;
    }
    if (H->frame < b->stepAt) return 1;
    if (b->step == 2) {   // reloaded
        int left = bot_ammo(i, b->supplySlot);
        if (left == 0 || g_rocketShots[i] >= ROCKET_MAX_SHOTS) { end_rocket(i, b, left == 0 ? "out of rockets" : "fired enough"); return 0; }   // (it has fired: 0 is real)
        float hit[3];
        if (ray_blocked_char(eye, aim, hit) && dist2(hit, aim) > 4.0f * 4.0f) { end_rocket(i, b, "lost sight"); return 0; }
        b->step = 1;
    }
    // fire when yaw and pitch are on target
    float yaw = *(float *)(soldier + OFF_SOLDIER_YAW), ty = fmodf(bot_nav(i)->targetYaw, 6.2831853f);
    if (ty > 0) ty -= 6.2831853f;
    float yerr = fabsf(ty - yaw);
    if (yerr > 3.14159265f) yerr = 6.2831853f - yerr;
    float perr = fabsf(g_aimPitch[i] - *(float *)(soldier + OFF_SOLDIER_PITCH));
    if (yerr < ROCKET_AIM_TOLERANCE && perr < ROCKET_AIM_TOLERANCE) {
        press_button(i, EIA_FIRE, 3);
        g_rocketShots[i]++;
        g_rocketsThisLife[i]++;
        b->step = 2;
        b->stepAt = H->frame + ROCKET_RELOAD_FRAMES;
        bot_log("rocket: %s fires rocket %d at %.0f m", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, g_rocketShots[i], d);
    }
    return 1;
}

// ---- Hand grenades and cover against enemy ground vehicles ----
// Why the vehicle a bot is in cover from no longer counts (NULL: still a threat).
static const char *vehicle_threat_gone(int i, const float *me)
{
    char *v = g_threatVeh[i];
    float *vp = !IsBadReadPtr(v, OFF_VEHICLE_HEALTH + 4) && *(unsigned *)v == 0x025bf8e8 && *(float *)(v + OFF_VEHICLE_HEALTH) > 0 &&
                vehicle_occupied(v) ? vehicle_pos(v, NULL) : NULL;
    if (!vp) return "vehicle gone";
    if (dist2(vp, me) > VEH_THREAT_RANGE * VEH_THREAT_RANGE) return "vehicle moved off";
    float eye[3] = {me[0], me[1] + 1.6f, me[2]}, a[3] = {vp[0], vp[1] + 1.0f, vp[2]}, hit[3];
    if (ray_blocked_char(eye, a, hit) && dist2(hit, a) > 4.0f * 4.0f) {
        if (!g_threatVehHidden[i]) g_threatVehHidden[i] = H->frame ? H->frame : 1;
        else if (H->frame - g_threatVehHidden[i] > 10 * 30) return "vehicle out of sight";
    } else g_threatVehHidden[i] = 0;
    return NULL;
}
static char *vehicle_occupant(char *veh);
static int cover_from_vehicle(int i, char *soldier, char *veh, const char *why)
{
    char *occ = vehicle_occupant(veh);
    float *vp = vehicle_pos(veh, NULL);
    if (!occ || !vp) return 0;
    float eye[3] = {vp[0], vp[1] + 2.0f, vp[2]};   // (a gunner's eye)
    return take_cover(i, soldier, occ, veh, eye, why);
}
static void start_grenade(int i, char *veh, int slot)
{
    start_rocket(i, veh, slot);
    g_rocketGrenade[i] = 1;
    bot_brain(i)->taskUntil = H->frame + 8 * 30;
    bot_log("grenade: %s goes for a grenade at an enemy %s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, vehicle_name(veh));
}
// Low throwing arc to a point dh away horizontally and dy up: pitch (rad), 0 if out of reach.
static int grenade_pitch(float dh, float dy, float *pitch)
{
    const float v = GRENADE_SPEED, g = 9.81f;
    float disc = v * v * v * v - g * (g * dh * dh + 2.0f * dy * v * v);
    if (disc < 0 || dh < 0.5f) return 0;
    *pitch = atan2f(v * v - sqrtf(disc), g * dh);
    return 1;
}
// step 0: grenade out; 1: aim on the arc and throw; 2: rifle back, then cover
static int grenade_tick(int i, BotBrain *b, char *soldier, char *veh, float *vp)
{
    const char *name = (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME;
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION), eye[3] = {me[0], me[1] + 1.6f, me[2]};
    float aim[3] = {vp[0], vp[1] + 0.3f, vp[2]};
    {
        NavGrid *g = *nav_slot();
        float gp[3];
        if (g && g->phase == 2 && nav_snap(g, vp[0], vp[1] - 1.5f, vp[2], gp) && gp[1] <= vp[1] + 0.5f) aim[1] = gp[1] + 0.3f;
    }
    float dx = aim[0] - eye[0], dz = aim[2] - eye[2], dh = sqrtf(dx * dx + dz * dz), pitch = 0;
    VehTrack *t = vehicle_sample(veh);
    int fast = t && t->speed > VEH_SLOW_SPEED;
    if (b->step < 2 && (fast || dh < GRENADE_MIN_RANGE - 1.0f || !grenade_pitch(dh, aim[1] - eye[1], &pitch))) {   // no throw after all
        press_button(i, SLOT_ACTION(0), 3);
        const char *why = fast ? "it sped up" : dh < GRENADE_MIN_RANGE - 1.0f ? "too close" : "out of reach";
        bot_log("grenade: %s does not throw (%s)", name, why);
        if (!cover_from_vehicle(i, soldier, veh, why)) end_rocket(i, b, why);
        return 0;
    }
    bot_nav(i)->mode = 1;
    bot_nav(i)->targetYaw = yaw_towards(dx, dz);
    g_aimPitch[i] = pitch;
    if (b->step == 0) {
        press_button(i, SLOT_ACTION(b->supplySlot), 3);
        b->step = 1;
        b->stepAt = H->frame + 25;   // grenade in hand
        return 1;
    }
    if (H->frame < b->stepAt) return 1;
    if (b->step == 1) {
        float yaw = *(float *)(soldier + OFF_SOLDIER_YAW), ty = fmodf(bot_nav(i)->targetYaw, 6.2831853f);
        if (ty > 0) ty -= 6.2831853f;
        float yerr = fabsf(ty - yaw);
        if (yerr > 3.14159265f) yerr = 6.2831853f - yerr;
        float perr = fabsf(pitch - *(float *)(soldier + OFF_SOLDIER_PITCH));
        if (yerr < 0.06f && perr < 0.06f) {
            press_button(i, EIA_FIRE, 3);
            g_grenadesThisLife[i]++;
            b->step = 2;
            b->stepAt = H->frame + 35;   // throwing animation
            bot_log("grenade: %s throws at the %s %.0f m away (%.0f deg up, it moves %.1f m/s)", name, vehicle_name(veh), dh,
                    pitch * 57.2958f, t ? t->speed : 0.0f);
        }
        return 1;
    }
    press_button(i, SLOT_ACTION(0), 3);   // rifle back out, and into cover
    if (!cover_from_vehicle(i, soldier, veh, "grenade thrown")) end_rocket(i, b, "grenade thrown, no cover near");
    return 0;
}
// Every half second: an enemy ground vehicle close by (bots with a launcher leave it to rocket_scan).
static void vehicle_threat_scan(int i, char *soldier)
{
    BotBrain *b = bot_brain(i);
    char *pl = H->bots[i];
    if (player_vehicle(pl) || H->frame < g_vehThreatCooldown[i]) return;
    if (b->task != TASK_NONE && b->task != TASK_DEFEND && b->task != TASK_FOLLOW && b->task != TASK_FIGHT) return;
    int ls = launcher_slot(i);
    if (ls >= 0 && !(bot_ammo(i, ls) == 0 && g_rocketsThisLife[i] > 0)) return;
    char **vehs;
    int nveh = vehicle_list(&vehs);
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION), bestD = VEH_THREAT_RANGE;
    char *best = NULL;
    for (int k = 0; k < nveh; k++) {
        char *veh = vehs[k];
        int vteam = 0;
        if (IsBadReadPtr(veh, OFF_VEHICLE_HEALTH + 4) || *(unsigned *)veh != 0x025bf8e8) continue;
        float *vp = vehicle_pos(veh, &vteam);
        if (!vp || !vteam || vteam == player_team(pl) || *(float *)(veh + OFF_VEHICLE_HEALTH) <= 0 || !vehicle_occupied(veh)) continue;
        float d = sqrtf(dist2(vp, me));
        if (d >= bestD || !vehicle_on_ground(vp)) continue;
        float eye[3] = {me[0], me[1] + 1.6f, me[2]}, aim[3] = {vp[0], vp[1] + 1.0f, vp[2]}, hit[3];
        if (ray_blocked_char(eye, aim, hit) && dist2(hit, aim) > 4.0f * 4.0f) continue;   // cannot see it
        bestD = d;
        best = veh;
    }
    if (!best) return;
    VehTrack *t = vehicle_sample(best);
    float speed = t ? t->speed : 0.0f;
    int gs = bot_gadget_slot(i, "M67", NULL);
    int nade = gs >= 0 && !(bot_ammo(i, gs) == 0 && g_grenadesThisLife[i] > 0) && g_grenadesThisLife[i] < 2;
    if (speed <= VEH_SLOW_SPEED && nade && bestD >= GRENADE_MIN_RANGE && bestD <= GRENADE_MAX_RANGE) { start_grenade(i, best, gs); return; }
    const char *why = speed > VEH_SLOW_SPEED ? "moving fast" : !nade ? "no grenade" : "out of throwing range";
    if (!cover_from_vehicle(i, soldier, best, why)) g_vehThreatCooldown[i] = H->frame + 3 * 30;
}

// Returns 1 while a task controls the bot (the flag loop is skipped).
static int task_tick(int i, BotBrain *b, char *soldier)
{
    if (b->task == TASK_REVIVE) return revive_tick(i, b, soldier);
    if (b->task == TASK_COVER) return cover_tick(i, b, soldier);
    if (b->task == TASK_ROCKET) return rocket_tick(i, b, soldier);
    if (b->task == TASK_FIGHT) return fight_tick(i, b, soldier);
    if (b->task == TASK_REPAIR) {
        char *veh = b->serviceTarget;
        if (H->frame > b->taskUntil) { end_repair(i, b, "repair took too long"); return 0; }
        if (!vehicle_alive(veh, g_repairVt[i])) { end_repair(i, b, "vehicle gone"); return 0; }
        VehTrack *t = vehicle_sample(veh);
        float h = *(float *)(veh + OFF_VEHICLE_HEALTH), mx = t && t->maxSeen ? t->maxSeen : 1000.0f;
        if (h <= 0) { end_repair(i, b, "vehicle destroyed"); return 0; }
        if (h >= 0.99f * mx) { g_jobStats[JOB_REPAIRED]++; end_repair(i, b, "vehicle repaired"); return 0; }
        float *vp = vehicle_pos(veh, NULL);
        if (vp) {
            float mx2 = vp[0] - b->supplyPos[0], mz2 = vp[2] - b->supplyPos[2];
            if (mx2 * mx2 + mz2 * mz2 > 3.0f * 3.0f) { end_repair(i, b, "vehicle drove off"); return 0; }
        }
        float *me = (float *)(soldier + OFF_SOLDIER_POSITION);
        float *cp = vp ? vp : b->supplyPos;   // vehicle centre (body position)
        float dx = cp[0] - me[0], dz = cp[2] - me[2], d = sqrtf(dx * dx + dz * dz);
        b->waiting = d <= REPAIR_CONTACT_DIST;   // (waiting = in contact while repairing)
        if (!b->waiting) {
            if (d > REPAIR_CONTACT_DIST + 2.0f && (H->frame >= b->replanAt || bot_nav(i)->mode == 1 || bot_nav(i)->mode == 4)) {
                b->replanAt = H->frame + 90;
                go_towards(i, soldier, cp[0] - dx / d * REPAIR_STANDOFF, cp[1], cp[2] - dz / d * REPAIR_STANDOFF);
            } else if (d <= REPAIR_CONTACT_DIST + 2.0f) {
                bot_nav(i)->mode = 4; bot_nav(i)->x = cp[0]; bot_nav(i)->z = cp[2];
            }
            return 1;
        }
        // in contact: walk into the vehicle's centre so the tool stays on the hull
        bot_nav(i)->mode = 4;
        bot_nav(i)->x = cp[0];
        bot_nav(i)->z = cp[2];
        if (b->step == 0) {
            press_button(i, SLOT_ACTION(b->supplySlot), 3);   // repair tool out (once)
            b->step = 1;
            b->stepAt = H->frame + 45;   // let the tool come out before the trigger goes down
            bot_log("brain: %s repairing (health %.0f / %.0f)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, h, mx);
        } else if (b->step == 1 && H->frame >= b->stepAt) {
            b->step = 2;   // tool is out: botmod_tick holds the trigger every frame from now on
        }
        return 1;
    }
    if (H->frame > b->taskUntil) { end_task(i, b, "timed out"); return 0; }
    if (b->task == TASK_FOLLOW && (!b->commander || !*(char **)(b->commander + OFF_PLAYER_SOLDIER))) { end_task(i, b, "their player died"); return 0; }
    if (b->task == TASK_FOLLOW) {
        char *cveh = player_vehicle(b->commander), *myveh = player_vehicle(H->bots[i]);
        if (myveh) {                                   // riding: stay while the commander is in the same vehicle
            g_boardUntil[i] = 0;
            if (cveh != myveh) { bot_exit_vehicle(i, 1); }
            return 1;
        }
        if (g_boardUntil[i] && (!cveh || H->frame > g_boardUntil[i] || free_seat(cveh) < 0)) {   // "Get in" that did not work out
            const char *why = !cveh ? "driver got out" : H->frame > g_boardUntil[i] ? "could not board in time" : "vehicle is full";
            g_boardUntil[i] = 0;
            end_task(i, b, why);
            choose_local_objective(i, "missed the ride");
            return 0;
        }
        if (cveh) {                                    // commander is in a vehicle: board it when close, else walk to it
            float *hp = human_pos(b->commander), *me = (float *)(soldier + OFF_SOLDIER_POSITION);
            float dx = hp[0] - me[0], dz = hp[2] - me[2];
            if (dx * dx + dz * dz < VEHICLE_BOARD_DIST * VEHICLE_BOARD_DIST && free_seat(cveh) >= 0) { bot_enter_vehicle(i, cveh); return 1; }
        }
        approach(i, b, soldier, b->commander, 4.0f);
        return 1;
    }
    if (b->task == TASK_DEFEND) {
        float *me = (float *)(soldier + OFF_SOLDIER_POSITION);
        float dx = b->postPos[0] - me[0], dz = b->postPos[2] - me[2], d = sqrtf(dx * dx + dz * dz);
        // close enough: within 2.5 m, or within 4 m and not getting closer for 2 s (teammates / cover in the way)
        if (d < 4.0f && !b->crouched && H->frame - b->stepAt > 60 && b->stepAt) d = 0;
        if (d > 2.5f && d < 4.0f && !b->stepAt) b->stepAt = H->frame;
        if (d > 4.0f) b->stepAt = 0;
        if (d > 2.5f) {
            if (b->crouched) { bot_stand(i); b->crouched = 0; }   // stand to move
            if (H->frame >= b->replanAt || bot_nav(i)->mode == 1) {
                b->replanAt = H->frame + 90;
                go_towards(i, soldier, b->postPos[0], b->postPos[1], b->postPos[2]);
            }
        } else {
            bot_nav(i)->mode = 1;
            bot_nav(i)->targetYaw = b->postFacing;   // cover outward
            if (!b->crouched) {
                bot_crouch(i);
                b->crouched = 1;
                static unsigned postLogAt[MAX_BOTS];   // V15: once per 30 s (a crouch knocked off by a fight logged it every tick)
                if (!postLogAt[i] || H->frame - postLogAt[i] > 30 * 30) { postLogAt[i] = H->frame ? H->frame : 1;
                    bot_log("brain: %s holding post at (%.1f, %.1f)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, b->postPos[0], b->postPos[2]); }
                place_class_gadget(i, soldier);
            }
        }
        return 1;
    }    if (b->task == TASK_SUPPLY) {
        float *me = (float *)(soldier + OFF_SOLDIER_POSITION);
        if (b->step == 0 && b->serviceTarget && player_exists(b->serviceTarget)) {   // follow the caller, not the spot they called from
            float *hp = human_pos(b->serviceTarget);
            if (hp) {
                if (dist2(hp, b->supplyPos) > 2.0f * 2.0f) b->replanAt = 0;
                memcpy(b->supplyPos, hp, sizeof b->supplyPos);
            }
        }
        float dx = b->supplyPos[0] - me[0], dz = b->supplyPos[2] - me[2], d = sqrtf(dx * dx + dz * dz);
        // V11: for a bot it is dropped from 4.5 m in sight (it steps onto it); a player gets it at 3 m
        float dropAt = b->serviceTarget && is_bot(b->serviceTarget) ? 4.5f : 3.0f;
        if (b->step == 0 && dropAt > 3.0f && d <= dropAt) {
            float e1[3] = {me[0], me[1] + 1.0f, me[2]}, e2[3] = {b->supplyPos[0], b->supplyPos[1] + 1.0f, b->supplyPos[2]};
            if (solid_between(e1, e2, NULL)) dropAt = 3.0f;
        }
        if (b->step == 0) {
            if (d > dropAt) {
                int pathDone = bot_nav(i)->mode == 1 && b->replanAt;   // walked and still not there
                if (pathDone && d < 6.0f) goto drop;                    // as close as we can get (e.g. the caller blocks the spot)
                if (H->frame - b->stepAt > SUPPLY_GIVEUP_FRAMES) {       // stepAt = when the run started
                    bot_log("brain: %s cannot reach the spot (%.1f m away), giving up", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, d);
                    g_supplyFailTo[i] = b->serviceTarget; g_supplyFailAt[i] = H->frame ? H->frame : 1;   // AltBotMod V34
                    bot_say_later(i, ORDER_SORRY, 0);
                    b->task = b->prevTask;
                    b->commander = b->prevCommander;
                    b->serviceTarget = NULL;
                    b->taskUntil = H->frame + FOLLOW_FRAMES;
                    if (b->task == TASK_NONE) end_task(i, b, "supply spot unreachable");
                    return 0;
                }
                if (H->frame >= b->replanAt || bot_nav(i)->mode == 1) {
                    b->replanAt = H->frame + 90;
                    go_towards(i, soldier, b->supplyPos[0] - dx / d * 1.5f, b->supplyPos[1], b->supplyPos[2] - dz / d * 1.5f);
                }
                return 1;
            }
        drop:
            bot_nav(i)->mode = 1;
            bot_nav(i)->targetYaw = yaw_towards(dx, dz);   // face the spot, drop it in front of us
            b->step = 1;
            b->stepAt = H->frame + 15;
        }
        if (H->frame >= b->stepAt) {
            switch (b->step++) {
            case 1: press_button(i, SLOT_ACTION(b->supplySlot), 3);           b->stepAt = H->frame + 30; break;   // gadget
            case 2: press_button(i, EIA_FIRE, 6);                            b->stepAt = H->frame + 35; break;   // drop it
            case 3: press_button(i, SLOT_ACTION(0), 3);                     b->stepAt = H->frame + 10; break;   // primary
            default:
                g_jobStats[JOB_SUPPLY_DROP]++;
                bot_log("brain: %s dropped slot %d gadget for %s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, b->supplySlot,
                        b->serviceTarget ? b->serviceTarget + OFF_PLAYER_ONLINE_NAME : "?");
                b->task = b->prevTask;
                b->commander = b->prevCommander;
                b->serviceTarget = NULL;
                b->step = 0;
                b->taskUntil = b->task == TASK_DEFEND ? defend_until(i) : H->frame + FOLLOW_FRAMES;   // V15: the defend job's own deadline
                b->replanAt = 0;
                if (b->task == TASK_NONE) end_task(i, b, "supply delivered");
            }
        }
        return 1;
    }
    return 0;
}
static void assign_task(int i, int task, char *commander)
{
    BotBrain *b = bot_brain(i);
    if (b->crouched) { bot_stand(i); b->crouched = 0; }   // stand up for the new order
    b->task = task;
    b->commander = commander;
    b->taskUntil = H->frame + FOLLOW_FRAMES;
    b->replanAt = 0;
    b->enroute = b->waiting = 0;
    b->targetFlag = -1;
}

// AltBotMod V11: a bot on foot with no vehicle job (not driving, flying or walking to its vehicle): one that can be sent
// on an errand. V10 sent drivers and pilots too - a medic on a dirt bike was told to bring a medkit 390 m away.
static int bot_air_job(int i);
static int bot_on_foot(int i)
{
    char *pl = H->bots[i];
    return *(char **)(pl + OFF_PLAYER_SOLDIER) && !player_vehicle(pl) && !g_drive[i].phase && !bot_air_job(i) && !g_gliding[i];
}
// Free for an errand: no task, or only guarding / following (they go back to it afterwards).
static int bot_errand_free(int i)
{
    BotBrain *b = bot_brain(i);
    return b->enabled && bot_on_foot(i) && (b->task == TASK_NONE || b->task == TASK_DEFEND || b->task == TASK_FOLLOW);
}
// Closest living, brain-enabled bot of the team; skip_busy ignores bots already serving a human.
static int closest_bot(int team, const float *p, int skipBusy, float *dist)
{
    int best = -1;
    float bestD = 1e30f;
    for (int i = 0; i < H->botCount; i++) {
        BotBrain *b = bot_brain(i);
        if (!b->enabled || player_team(H->bots[i]) != team || !bot_on_foot(i)) continue;   // (V11: not drivers / pilots)
        if (skipBusy && b->task != TASK_NONE) continue;
        float d = bot_distance_to(i, p);
        if (d < bestD) { bestD = d; best = i; }
    }
    if (dist) *dist = bestD;
    return bestD < 1e29f ? best : -1;
}

// target: the bot a quick-Q request was aimed at (NULL for the command wheel) - it answers first.
static void order_reaction(char *sender, int type, char *target)
{
    if (type == 17) { squad_order_radio(sender); return; }   // AltBotMod V34: the commo rose's Attack - the squad's bots go
    int ti = target ? bot_index(target) : -1;
    float *sp = human_pos(sender);
    if (!sp) return;
    int team = player_team(sender);
    const char *who = sender + OFF_PLAYER_ONLINE_NAME;
    char *sveh = player_vehicle(sender);
    if (type == ORDER_GETIN && sveh) {
        int seats = free_seat_count(sveh);
        for (int n = 0; n < seats; n++) {   // only as many bots as there are free seats, nearest first
            int i = -1;
            float bestD = GETIN_RADIUS;
            for (int j = 0; j < H->botCount; j++) {
                BotBrain *o = bot_brain(j);
                if (!o->enabled || player_team(H->bots[j]) != team || player_vehicle(H->bots[j]) || !*(char **)(H->bots[j] + OFF_PLAYER_SOLDIER)) continue;
                if (o->task == TASK_FOLLOW && o->commander == sender) continue;   // already coming
                if (g_drive[j].phase) continue;                                    // off to drive its own
                float d = bot_distance_to(j, sp);
                if (d < bestD) { bestD = d; i = j; }
            }
            if (i < 0) break;
            assign_task(i, TASK_FOLLOW, sender);   // riding along = following the driver
            g_boardUntil[i] = H->frame + GETIN_BOARD_FRAMES;
            bot_say_later(i, ORDER_ROGERTHAT, SAY_DELAY_FRAMES);
            bot_log("brain: %s gets in %s's vehicle", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, who);
        }
        return;
    }
    if (type == ORDER_GETOUT && sveh) {
        for (int i = 0; i < H->botCount; i++) {
            if (player_vehicle(H->bots[i]) != sveh || H->bots[i] == sender) continue;   // (a bot driver gets out by itself)
            bot_exit_vehicle(i, 1);
            bot_say_later(i, ORDER_THANKYOU, SAY_DELAY_FRAMES);   // thanks for the ride
            end_task(i, bot_brain(i), "bailed out");
            choose_local_objective(i, "bailed out");   // nearest flag from where they jump out
        }
        return;
    }
    if (type == ORDER_REQUESTREPAIR) {
        // the caller's vehicle, or the nearest friendly vehicle next to them
        char *veh = sveh;
        if (!veh) {
            char **vehs;
            int n = vehicle_list(&vehs);
            float bestD = REPAIR_CALL_VEHICLE_DIST;
            for (int k = 0; k < n; k++) {
                int vt = 0;
                float *vp = !IsBadReadPtr(vehs[k], 4) && *(unsigned *)vehs[k] == 0x025bf8e8 ? vehicle_pos(vehs[k], &vt) : NULL;
                if (!vp || vt != team) continue;
                float dx = vp[0] - sp[0], dz = vp[2] - sp[2], d = sqrtf(dx * dx + dz * dz);
                if (d < bestD) { bestD = d; veh = vehs[k]; }
            }
        }
        VehTrack *t = veh ? vehicle_sample(veh) : NULL;
        float h = veh ? *(float *)(veh + OFF_VEHICLE_HEALTH) : 0, mx = t && t->maxSeen ? t->maxSeen : 1000.0f;
        int sent = 0, nearest = -1;
        float nearD = 1e30f;
        for (int i = 0; veh && h > 0 && h < 0.99f * mx && i < H->botCount; i++) {
            BotBrain *b = bot_brain(i);
            char *pl = H->bots[i];
            if (!b->enabled || player_team(pl) != team || player_vehicle(pl) || !*(char **)(pl + OFF_PLAYER_SOLDIER)) continue;
            float d = bot_distance_to(i, sp);
            if (d < nearD) { nearD = d; nearest = i; }
            if (d > REPAIR_CALL_RADIUS && i != ti) continue;   // the engineer the player looked at comes from anywhere
            if (b->task == TASK_REPAIR && b->serviceTarget == veh) { sent++; continue; }   // already on it
            if (b->task != TASK_NONE && b->task != TASK_DEFEND && b->task != TASK_FOLLOW) continue;
            int slot = bot_gadget_slot(i, "Repairtool", "RepairTool");
            if (slot < 0) continue;
            start_repair(i, veh, slot);
            bot_say_later(i, ORDER_ROGERTHAT, SAY_DELAY_FRAMES);
            sent++;
        }
        if (!sent) {
            if (nearest < 0)   // any bot nearby to answer
                for (int i = 0; i < H->botCount; i++) {
                    if (player_team(H->bots[i]) != team || !*(char **)(H->bots[i] + OFF_PLAYER_SOLDIER)) continue;
                    float d = bot_distance_to(i, sp);
                    if (d < nearD) { nearD = d; nearest = i; }
                }
            bot_log("brain: %s needs a repair but %s", who, !veh ? "there is no friendly vehicle next to them" :
                    h >= 0.99f * mx ? "the vehicle is not damaged" : "no engineer is free within range");
            if (nearest >= 0 && nearD < REPAIR_CALL_RADIUS) {
                bot_say_later(nearest, ORDER_NEGATIVE, SAY_DELAY_FRAMES);
                bot_say_later(nearest, ORDER_SORRY, SAY_DELAY_FRAMES + 3 * 30);
            }
        } else bot_log("brain: %d engineer(s) answer %s's repair call", sent, who);
        return;
    }
    if (type == ORDER_GOGOGO) {
        // releases only the bots this player commands (following / backing up)
        for (int i = 0; i < H->botCount; i++) {
            BotBrain *b = bot_brain(i);
            if (b->task == TASK_FOLLOW && b->commander == sender) { end_task(i, b, "GoGoGo from commander"); choose_local_objective(i, "GoGoGo"); }
            else if (b->task == TASK_DEFEND && bot_distance_to(i, sp) <= DEFEND_RELEASE_RADIUS) {
                if (b->crouched) bot_stand(i);
                b->crouched = 0;
                end_task(i, b, "GoGoGo from commander");   // same reply (Sorry) and back to objectives
                choose_local_objective(i, "GoGoGo");
            }
            if (b->task == TASK_SUPPLY && b->prevCommander == sender) { b->prevTask = TASK_NONE; b->prevCommander = NULL; bot_say_later(i, ORDER_SORRY, SAY_DELAY_FRAMES); }
        }
    } else if (type == ORDER_FOLLOWME) {
        for (int i = 0; i < H->botCount; i++) {
            BotBrain *b = bot_brain(i);
            if (!b->enabled || player_team(H->bots[i]) != team || bot_distance_to(i, sp) > FOLLOW_RADIUS || !bot_on_foot(i)) continue;   // (V11: crews keep driving)
            if (b->task == TASK_SUPPLY) { b->prevTask = TASK_FOLLOW; b->prevCommander = sender; bot_say_later(i, ORDER_ROGERTHAT, SAY_DELAY_FRAMES); continue; }
            assign_task(i, TASK_FOLLOW, sender);
            bot_say_later(i, ORDER_ROGERTHAT, SAY_DELAY_FRAMES);
            bot_log("brain: %s follows %s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, who);
        }
    } else if (type == ORDER_NEEDBACKUP) {
        for (int n = 0; n < BACKUP_BOTS; n++) {
            float d;
            int i = closest_bot(team, sp, 1, &d);
            if (i < 0) break;
            BotBrain *b = bot_brain(i);
            assign_task(i, TASK_DEFEND, sender);
            b->taskUntil = g_defendUntil[i] = H->frame + BACKUP_DEFEND_FRAMES;   // V15 (V9-V14: NEVER)
            b->stepAt = 0;
            // spread evenly around the spot among every bot already defending near it
            int k = 0;
            for (int j = 0; j < H->botCount; j++) {
                BotBrain *o = bot_brain(j);
                if (j == i || o->task != TASK_DEFEND) continue;
                float ox = o->postPos[0] - sp[0], oz = o->postPos[2] - sp[2];
                if (ox * ox + oz * oz < 8.0f * 8.0f) k++;
            }
            float ang = 6.2831853f * k / 4.0f + 0.4f;   // quarter turns: up to 4 bots cover all sides
            b->postPos[0] = sp[0] + sinf(ang) * DEFEND_SPREAD;
            b->postPos[1] = sp[1];
            b->postPos[2] = sp[2] + cosf(ang) * DEFEND_SPREAD;
            b->postFacing = ang;   // yaw_towards(sin, cos) == ang: face away from the spot
            b->crouched = 0;
            bot_say_later(i, ORDER_ROGERTHAT, SAY_DELAY_FRAMES);
            bot_log("brain: %s will defend %s's position (%.0f m away)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, who, d);
        }    } else if (type == ORDER_REQUESTAMMO || type == ORDER_REQUESTHEAL || type == ORDER_REQUESTRIDE) {
        int heal = type == ORDER_REQUESTHEAL, ride = type == ORDER_REQUESTRIDE, botCall = is_bot(sender);
        const char *what = heal ? "medkit" : ride ? "radio beacon" : "ammo";
        // closest bot that carries the gadget; if none does, the closest bot declines
        // AltBotMod V11: only a bot on foot that is free (V10 also pulled medics out of revives, fights and vehicles), and
        // within reach - SupplyMeters for a bot's call (V10: anyone on the map: 64 runs, 2 delivered), 3x that for a player's
        float reach = g_enhanced.supplyMeters * (botCall ? 1.0f : 3.0f);
        int best = -1, fallback = -1, slot = -1;
        float bestD = 1e30f, fbD = 1e30f;
        for (int i = 0; i < H->botCount; i++) {
            BotBrain *b = bot_brain(i);
            if (!b->enabled || player_team(H->bots[i]) != team || b->task == TASK_SUPPLY || H->bots[i] == sender || !bot_errand_free(i)) continue;
            float d = bot_distance_to(i, sp);
            if (d > reach && i != ti) continue;
            // AltBotMod V34: not the bot that just gave up on this caller (V33: the same medic sent 3 times in 2 min to a
            // spot it could not reach - walking back and forth)
            if (g_supplyFailTo[i] == sender && H->frame - g_supplyFailAt[i] < 45 * 30 && i != ti) continue;
            int s = heal ? bot_gadget_slot(i, "Medkit", "Medicbag") : ride ? bot_gadget_slot(i, "RadioBeacon", "Beacon")
                                                                  : bot_gadget_slot(i, "Ammobag", "AmmoBag");
            if (s >= 0 && bot_ammo(i, s) == 0) {   // carries it but has none left to drop
                bot_log("brain: %s is out of %s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, what);
                s = -1;
            }
            if (s >= 0 && i == ti) { bestD = -1; best = i; slot = s; }   // the bot the player looked at
            if (s >= 0 && d < bestD) { bestD = d; best = i; slot = s; }
            if (d < fbD) { fbD = d; fallback = i; }
        }
        if (best < 0) {
            if (fallback < 0 || botCall) return;   // (V11: bots asking each other do not fill the radio with "Negative")
            BotBrain *b = bot_brain(fallback);
            bot_log("brain: %s has no %s for %s", (char *)H->bots[fallback] + OFF_PLAYER_ONLINE_NAME, what, who);
            bot_say_later(fallback, ORDER_NEGATIVE, SAY_DELAY_FRAMES);
            bot_say_later(fallback, ORDER_SORRY, SAY_DELAY_FRAMES + 3 * 30);
            return;
        }
        BotBrain *b = bot_brain(best);
        if (b->crouched) bot_stand(best);   // stand up for the supply run
        b->crouched = 0;
        b->prevTask = b->task;
        b->prevCommander = b->commander;
        b->task = TASK_SUPPLY;
        b->serviceTarget = sender;
        b->supplySlot = slot;
        memcpy(b->supplyPos, sp, sizeof b->supplyPos);   // where they stood when they called
        b->step = 0;
        b->stepAt = H->frame;   // run start (give-up timer)
        b->taskUntil = H->frame + 90 * 30;
        b->replanAt = 0;
        b->enroute = b->waiting = 0;
        bot_say_later(best, ORDER_ROGERTHAT, SAY_DELAY_FRAMES);
        if (botCall) supply_wait_start(sender, H->bots[best]);   // V11: the bot that asked waits for it
        g_jobStats[JOB_SUPPLY_RUN]++;
        bot_log("brain: %s brings %s (slot %d) to %s at (%.1f, %.1f), %.0f m away", (char *)H->bots[best] + OFF_PLAYER_ONLINE_NAME, what, slot, who,
                sp[0], sp[2], bestD);
    }
}
// ---- Radio: a bot "says" an order by setting it on its own soldier's order component (replicated to clients) ----
#define ADDR_ORDER_LISTENER_VTABLE 0x02687c3c  // message-listener sub-object at component + 0x20
#define ADDR_ORDERCOMP_SET_ORDER   0x012c22f0  // OrderComponent::setOrder(ServerPlayer* from, int type), thiscall
typedef void (__attribute__((thiscall)) *SetOrder_t)(void *comp, void *fromPlayer, int type);
#define ADDR_ORDER_EVENT_CTOR      0x00ddb270  // (target player, sender player, event hash, 7 x 0), ret 0x28
#define ADDR_ENTITY_FIRE_EVENT     0x008427c0  // component->fireEvent(event*) on its owning entity
#define ADDR_ORDER_EVENT_DTOR      0x00d72be0
typedef void (__attribute__((thiscall)) *OrderEventCtor_t)(void *ev, void *target, void *sender, unsigned hash, int, int, int, int, int, int, int);
typedef void (__attribute__((thiscall)) *FireEvent_t)(void *comp, void *ev);
// Event hash per order type (jump table in the order handler at 0x12c3200).
static const unsigned kOrderEventHash[17] = {0, 0x0142d075, 0x2fe260b1, 0x013f0a5b, 0xf0042302, 0x75a6e7ac, 0x32fac868, 0x013912cf,
    0x5d391f0c, 0x7f84e01d, 0x44daab47, 0xfc7719a1, 0x8e0b907f, 0x87757e00, 0x3f13e920, 0xfb0c89f5, 0x5c9dbedc};
static char *g_orderComp[MAX_BOTS];
static char *g_orderCompSoldier[MAX_BOTS];

// A component (order / spotting, listener at +0x20) that still belongs to `ent`. Cached component pointers go stale
// when a soldier dies and a new one reuses its address: using a freed component crashed the server (0x8427a3).
static int comp_owned_by(char *comp, char *ent)
{
    if (!comp || !ent || IsBadReadPtr(comp, 0x34) || *(unsigned *)(comp + 0x20) != ADDR_ORDER_LISTENER_VTABLE) return 0;
    void **ref = *(void ***)(comp + 8);
    return ref && !IsBadReadPtr(ref, 4) && *ref == ent;
}

static char *find_entity_component(char *ent);
// The soldier's order component, from the scan worker's cache (a memory scan here cost ~80 ms on the game thread).
static char *find_order_component(char *soldier) { return find_entity_component(soldier); }

static void bot_say(int i, int orderType)
{
    char *pl = H->bots[i], *soldier = *(char **)(pl + OFF_PLAYER_SOLDIER);
    if (!soldier) return;
    if (g_orderCompSoldier[i] != soldier || !comp_owned_by(g_orderComp[i], soldier)) {
        g_orderComp[i] = find_order_component(soldier);
        g_orderCompSoldier[i] = soldier;
    }
    if (!g_orderComp[i]) { bot_log("say: no order component for %s", pl + OFF_PLAYER_ONLINE_NAME); return; }
    ((SetOrder_t)ADDR_ORDERCOMP_SET_ORDER)(g_orderComp[i], pl, orderType);
    // The voice line / animation comes from an entity event fired on the soldier (what the order handler does next).
    if (orderType >= 1 && orderType <= 16) {
        unsigned char ev[0x80] __attribute__((aligned(16))) = {0};
        ((OrderEventCtor_t)ADDR_ORDER_EVENT_CTOR)(ev, pl, pl, kOrderEventHash[orderType], 0, 0, 0, 0, 0, 0, 0);
        ((FireEvent_t)ADDR_ENTITY_FIRE_EVENT)(g_orderComp[i], ev);
        ((ThisVoid_t)ADDR_ORDER_EVENT_DTOR)(ev);
    }
    bot_log("say: %s -> %s", pl + OFF_PLAYER_ONLINE_NAME, orderType >= 0 && orderType < 18 ? kOrderNames[orderType] : "?");
}

// ---- Spotting: VeniceNetwork.Spotting (client, ~1/s + on Q) carries the spotter (+0x3c) and up to 8 targets
// (+0x40..+0x5c, weak refs); the component of every target soldier/vehicle handles it: mode 1 (+0x80) ->
// markSpotted(comp, spotterPlayer) which stores the spotter and starts the spot timer (comp+0x30).
#define ADDR_COMP_MARK_SPOTTED     0x012c32a0  // thiscall (component, ServerPlayer* spotter), ret 4
typedef void (__attribute__((thiscall)) *MarkSpotted_t)(void *comp, void *spotter);

// Component whose owner entity is `ent` (soldier or vehicle): same listener sub-object as the order component.
// A worker thread rescans them every 2 s into a double buffer, so the main thread only does a lookup.
#define MAX_COMPS 512
typedef struct CompCache {
    int count; char *comp[MAX_COMPS]; char *owner[MAX_COMPS];
    int wcount; char *wcomp[MAX_COMPS]; char *wsoldier[MAX_COMPS];   // soldier weapon components (ammo)
    int vcount; char *veh[256];                                          // every ServerVehicleEntity (occupied or not)
    int bcount; char *body[128]; char *bodySoldier[128];                // soldier hit bodies (vt 0x0247ac10, +0x30 soldier)
    int pcount; char *pack[64];                                         // deployed ammo boxes / medkits (ServerSupplySphereEntity)
    int gcount; char *gun[256];                                         // vehicle / turret weapons (ServerWeapon)
} CompCache;
#define ADDR_SUPPLY_SPHERE_VTABLE  0x02696b68  // ServerSupplySphereEntity (type info 0x02c23a08)
#define ADDR_VEHICLE_WEAPON_VTABLE 0x025baba0  // ServerWeapon on vehicles: world matrix +0x30 (forward row +0x50, position +0x60)
#define ADDR_HITBODY_VTABLE        0x0247ac10
#define ADDR_VEHICLE_VTABLE        0x025bf8e8  // ServerVehicleEntity (tanks, LAVs, jeeps, ...)
#define ADDR_WEAPONS_COMP_VTABLE   0x025c2f60  // soldier weapons component: +0x14 weapon per slot [8], +0x44 soldier
static double perf_now(void);
static volatile double g_scanMs, g_scanMaxMs;
static volatile LONG g_scanPasses;
static CompCache g_compCache[2];
static unsigned char g_vaimOn[MAX_BOTS];
static int g_gunDebug;
static int g_vehInput = 1;   // feed bots' input to their vehicle seat (console: vinput 0/1)
// AltBotMod V31: the last 1.5 s of every bot driver, frame by frame - written out with the first drops of a round
// ("lost N m/s in 1/3 s with the throttle down") so the log shows what the vehicle, its seat and the controls did
// around one: the body position the driving code reads, the driver's seat position, the physics transform, the
// throttle / brake / steering levels actually in the input, its delta time, and the wall clock of each server frame.
#define VRING 45
typedef struct { unsigned frame; double ms; float body[3], seat[3], phys[3], lv[5], dt; } VRingSample;
static VRingSample g_vring[MAX_BOTS][VRING];
static unsigned g_vringDumps;
static void vring_record(int i, char *veh, void *in)
{
    VRingSample *r = &g_vring[i][H->frame % VRING];
    memset(r, 0, sizeof *r);
    r->frame = H->frame; r->ms = perf_now();
    char *body = *(char **)(veh + OFF_VEHICLE_BODY);
    if (body && !IsBadReadPtr(body + OFF_VEHBODY_POSITION, 12)) memcpy(r->body, body + OFF_VEHBODY_POSITION, 12);
    char *s = *(char **)(H->bots[i] + OFF_PLAYER_SOLDIER);
    if (s && !IsBadReadPtr(s + OFF_SOLDIER_SPAWN_POSITION, 12)) memcpy(r->seat, s + OFF_SOLDIER_SPAWN_POSITION, 12);
    char *phys = !IsBadReadPtr(veh + 0x30, 4) ? *(char **)(veh + 0x30) : NULL;
    char *xf = phys && !IsBadReadPtr(phys + 0x88, 4) ? *(char **)(phys + 0x88) : NULL;
    if (xf && !IsBadReadPtr(xf + 0x30, 12)) memcpy(r->phys, xf + 0x30, 12);
    if (in && !IsBadReadPtr(in, 0x1f0)) {
        float *lv = (float *)((char *)in + 0x10);
        r->lv[0] = lv[0]; r->lv[1] = lv[1]; r->lv[2] = lv[2]; r->lv[3] = lv[3]; r->lv[4] = lv[4];
        r->dt = *(float *)((char *)in + 0x1ec);
    }
}
static void vring_dump(int i)
{
    if (g_vringDumps >= 6) return;
    g_vringDumps++;
    bot_log("vtrace: %s - the last %d frames (frame, ms since the first, body x/z, seat x/z, physics x/z, throttle/strafe/brake/handbrake/yaw levels, dt):",
            (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, VRING);
    double t0 = 0;
    for (int k = 1; k <= VRING; k++) {
        VRingSample *r = &g_vring[i][(H->frame + k) % VRING];
        if (!r->frame) continue;
        if (!t0) t0 = r->ms;
        bot_log("vtrace: %u %7.1f body %.2f %.2f seat %.2f %.2f phys %.2f %.2f in %.2f %.2f %.2f %.2f %.2f dt %.4f",
                r->frame, r->ms - t0, r->body[0], r->body[2], r->seat[0], r->seat[2], r->phys[0], r->phys[2],
                r->lv[0], r->lv[1], r->lv[2], r->lv[3], r->lv[4], r->dt);
    }
}
#define EIA_SEAT_YAW 6          // turret traverse in a vehicle seat (EIAYaw 4 does nothing there)
static float g_seatYaw[MAX_BOTS], g_seatPitch[MAX_BOTS];
static int vehicle_seat_of(char *veh, char *pl)
{
    char **b;
    int n = vehicle_seats(veh, &b);
    for (int k = 0; k < n; k++) if (*(char **)(b[k] + 0x4c) == pl) return k;
    return -1;
}
static float g_vaimYaw[MAX_BOTS], g_vaimPitch[MAX_BOTS];
static volatile const char *g_where;      // what the game-thread tick is doing right now (NULL = not in our tick)
static volatile int g_whereBot = -1;
static void stall_check(void)
{
    static unsigned lastFrame, sameFor;
    static int reported;
    const char *w = (const char *)g_where;
    if (!w || H->frame != lastFrame) { lastFrame = H->frame; sameFor = 0; reported = 0; return; }
    sameFor += 2;
    if (sameFor >= 4 && !reported) {
        reported = 1;
        int b = g_whereBot;
        bot_log("STALL: game thread stuck %u s in %s (bot %d %s)", sameFor, w, b,
                b >= 0 && b < H->botCount ? (char *)H->bots[b] + OFF_PLAYER_ONLINE_NAME : "");
    }
}
static volatile LONG g_compCur;
static volatile LONG g_compStop;
static volatile LONG g_compRefresh;
static HANDLE g_compThread;
static DWORD WINAPI comp_worker(LPVOID unused)
{
    (void)unused;
    while (!g_compStop) {
        if (!level_ready() || g_roundCleanup || H->botCount == 0) {   // no bots: no memory scans either
            for (int t = 0; t < 20 && !g_compStop; t++) Sleep(100);
            continue;
        }
        LONG epoch=g_roundEpoch;
        InterlockedExchange(&g_compRefresh,0);
        double scanStart = perf_now();
        static char *hListen[MAX_COMPS], *hWeap[MAX_COMPS], *hVeh[512], *hGun[256], *hPack[64], *hBody[MAX_COMPS];
        ScanType st[6] = {
            {ADDR_ORDER_LISTENER_VTABLE, 8, hListen, MAX_COMPS, 0}, {ADDR_WEAPONS_COMP_VTABLE, 4, hWeap, MAX_COMPS, 0},
            {ADDR_VEHICLE_VTABLE, 8, hVeh, 512, 0}, {ADDR_VEHICLE_WEAPON_VTABLE, 8, hGun, 256, 0},
            {ADDR_SUPPLY_SPHERE_VTABLE, 4, hPack, 64, 0}, {ADDR_HITBODY_VTABLE, 4, hBody, MAX_COMPS, 0}};
        scan_objects_multi(st, 6);
        if(epoch!=g_roundEpoch || g_roundCleanup || !level_ready())continue;
        CompCache *c = &g_compCache[!g_compCur];
        c->count = 0;
        for (int k = 0; k < st[0].count; k++) {
            char *comp = hListen[k] - 0x20, *owner = NULL;
            void ***ref = (void ***)(comp + 8);
            if (!IsBadReadPtr(ref, 4) && *ref && !IsBadReadPtr(*ref, 4)) owner = **ref;
            if (owner) { c->comp[c->count] = comp; c->owner[c->count] = owner; c->count++; }
        }
        c->wcount = 0;
        for (int k = 0; k < st[1].count; k++) {
            char *s = !IsBadReadPtr(hWeap[k] + 0x44, 4) ? *(char **)(hWeap[k] + 0x44) : NULL;
            if (s) { c->wcomp[c->wcount] = hWeap[k]; c->wsoldier[c->wcount] = s; c->wcount++; }
        }
        c->vcount = 0;
        for (int k = 0; k < st[2].count && c->vcount<256; k++) {
            char *vehicle=hVeh[k];if(!vehicle_ok(vehicle))continue;
            int duplicate=0;for(int n=0;n<c->vcount;n++)if(c->veh[n]==vehicle){duplicate=1;break;}
            if(!duplicate)c->veh[c->vcount++]=vehicle;
        }
        c->gcount = 0;
        for (int k = 0; k < st[3].count; k++) c->gun[c->gcount++] = hGun[k];
        c->pcount = 0;
        for (int k = 0; k < st[4].count; k++) c->pack[c->pcount++] = hPack[k];
        char **hits = hBody;
        int n = st[5].count;
        c->bcount = 0;
        for (int k = 0; k < n && c->bcount < 128; k++) {
            char *s = !IsBadReadPtr(hits[k] + 0x30, 4) ? *(char **)(hits[k] + 0x30) : NULL;
            if (s && !IsBadReadPtr(s, 4) && *(unsigned *)s == ADDR_SOLDIER_VTABLE) { c->body[c->bcount] = hits[k]; c->bodySoldier[c->bcount] = s; c->bcount++; }
        }
        if(epoch!=g_roundEpoch || g_roundCleanup || !level_ready())continue;
        InterlockedExchange(&g_compCur,!g_compCur);InterlockedExchange(&g_compEpoch,epoch);
        {
            double ms = perf_now() - scanStart;
            g_scanMs += ms; if (ms > g_scanMaxMs) g_scanMaxMs = ms;
            InterlockedIncrement(&g_scanPasses);
        }
        stall_check();
        for (int t = 0; t < 40 && !g_compStop; t++) {
            if(InterlockedCompareExchange(&g_compRefresh,0,0))break;
            Sleep(100);
        }
    }
    return 0;
}
static char *soldier_hit_body(char *soldier)
{
    CompCache *c = &g_compCache[g_compCur];
    for (int k = 0; k < c->bcount; k++)
        if (c->bodySoldier[k] == soldier && !IsBadReadPtr(c->body[k], 0x34) && *(unsigned *)c->body[k] == ADDR_HITBODY_VTABLE &&
            *(char **)(c->body[k] + 0x30) == soldier) return c->body[k];
    return NULL;
}

// ---- Supply packs (ammo boxes / medkits): a bot passing within 8 m of a friendly one it needs walks onto it and
// stands there a second. Kind: learned per pack data from packs our bots dropped (class tells) ----
#define PACK_AMMO              1
#define PACK_HEAL              2
#define PACK_DETOUR_DIST       8.0f
#define OFF_PACK_OWNER_REF     0x84        // weak ref -> owner player + 8
#define OFF_PACK_POSITION      0xb0
static char *g_packData[16];
static int g_packKind[16];
static char *pack_owner(char *pack)
{
    void **ref = *(void ***)(pack + OFF_PACK_OWNER_REF);
    if (!ref || IsBadReadPtr(ref, 4) || !*ref) return NULL;
    char *pl = (char *)*ref - 8;
    return player_exists(pl) ? pl : NULL;
}
static int pack_kind(char *pack, char *owner)
{
    char *data = *(char **)(pack + 0x14);
    for (int k = 0; k < 16; k++) if (g_packData[k] == data) return g_packKind[k];
    if (!owner || !is_bot(owner)) return 0;
    const char *n = owner + OFF_PLAYER_ONLINE_NAME;
    int kind = strstr(n, "Medic") ? PACK_HEAL : strstr(n, "Support") ? PACK_AMMO : 0;
    if (kind) {
        static int next;
        g_packData[next] = data; g_packKind[next] = kind; next = (next + 1) % 16;
        bot_log("packs: data %p is %s", data, kind == PACK_HEAL ? "a medkit" : "an ammo box");
    }
    return kind;
}
static int pack_valid(char *pack) { return pack && !IsBadReadPtr(pack, 0x100) && *(unsigned *)pack == ADDR_SUPPLY_SPHERE_VTABLE; }
static int bot_needs(int i, char *soldier, int kind)
{
    if (kind == PACK_HEAL) { float h = *(float *)(soldier + OFF_SOLDIER_HEALTH); return h > 0 && h < 99.5f; }
    int l, s;
    if (soldier_ammo2(soldier, 0, &l, &s) && s < g_spareMax[i]) return 1;
    int ls = launcher_slot(i);
    return ls >= 0 && g_launcherMax[i] > 0 && bot_ammo(i, ls) >= 0 && bot_ammo(i, ls) < g_launcherMax[i];
}
static char *g_pickPack[MAX_BOTS];
static unsigned g_pickUntil[MAX_BOTS], g_pickStandUntil[MAX_BOTS];
static char *g_pickDone[MAX_BOTS][4];
static unsigned g_pickDoneAt[MAX_BOTS][4];
static void end_pickup(int i, BotBrain *b, const char *why)
{
    for (int k = 0; k < 4; k++)
        if (!g_pickDone[i][k] || H->frame - g_pickDoneAt[i][k] > 20 * 30) { g_pickDone[i][k] = g_pickPack[i]; g_pickDoneAt[i][k] = H->frame; break; }
    bot_log("packs: %s leaves the pack (%s)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, why);
    g_pickPack[i] = NULL;
    g_pickStandUntil[i] = 0;
    bot_nav(i)->mode = 1;
    b->replanAt = 0;
    if (b->task == TASK_NONE) b->enroute = b->waiting = 0;   // re-plan the way to the objective from here
}
static int pickup_tick(int i, BotBrain *b, char *soldier)
{
    char *pl = H->bots[i];
    int free = (b->task == TASK_NONE || b->task == TASK_DEFEND || b->task == TASK_FOLLOW) && !player_vehicle(pl);
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION);
    if (g_pickPack[i]) {
        char *pack = g_pickPack[i];
        if (!free) { g_pickPack[i] = NULL; g_pickStandUntil[i] = 0; return 0; }   // something more important came up
        if (!pack_valid(pack)) { end_pickup(i, b, "pack gone"); return 0; }
        if (H->frame > g_pickUntil[i]) { end_pickup(i, b, "could not get onto it"); return 0; }
        float *pp = (float *)(pack + OFF_PACK_POSITION);
        BotNav *nav = bot_nav(i);
        if (g_pickStandUntil[i]) {
            nav->mode = 1;
            if (H->frame >= g_pickStandUntil[i]) { end_pickup(i, b, "stood on it"); return 0; }
            return 1;
        }
        if (sqrtf(dist2(me, pp)) < 0.7f) { g_pickStandUntil[i] = H->frame + 30; nav->mode = 1; return 1; }
        nav->mode = 4; nav->x = pp[0]; nav->z = pp[2];
        return 1;
    }
    if (!free || H->frame % 15 != (unsigned)(i % 15)) return 0;
    CompCache *cc = &g_compCache[g_compCur];
    char *best = NULL;
    float bestD = PACK_DETOUR_DIST;
    for (int k = 0; k < cc->pcount; k++) {
        char *pack = cc->pack[k];
        if (!pack_valid(pack)) continue;
        char *owner = pack_owner(pack);
        if (!owner || player_team(owner) != player_team(pl)) continue;
        float *pp = (float *)(pack + OFF_PACK_POSITION);
        float d = sqrtf(dist2(me, pp));
        if (d >= bestD || fabsf(pp[1] - me[1]) > 2.5f) continue;
        int kind = pack_kind(pack, owner), done = 0;
        if (!kind || !bot_needs(i, soldier, kind)) continue;
        for (int m = 0; m < 4; m++) if (g_pickDone[i][m] == pack && H->frame - g_pickDoneAt[i][m] < 20 * 30) done = 1;
        if (done) continue;
        float a[3] = {me[0], me[1] + 0.5f, me[2]}, z[3] = {pp[0], pp[1] + 0.5f, pp[2]};
        if (solid_between(a, z, NULL)) continue;
        bestD = d; best = pack;
    }
    if (!best) return 0;
    g_pickPack[i] = best;
    g_pickUntil[i] = H->frame + 6 * 30;
    g_pickStandUntil[i] = 0;
    bot_log("packs: %s steps onto %s's %s %.0f m away", pl + OFF_PLAYER_ONLINE_NAME, pack_owner(best) + OFF_PLAYER_ONLINE_NAME,
            pack_kind(best, NULL) == PACK_HEAL ? "medkit" : "ammo box", bestD);
    return 1;
}

static int vehicle_list(char ***out)
{
    CompCache *c = &g_compCache[g_compCur];
    *out = c->veh;
    return level_ready() && !g_roundCleanup && g_compEpoch==g_roundEpoch ? c->vcount : 0;
}

static int comp_owned_by(char *comp, char *ent);
static char *find_entity_component(char *ent)
{
    CompCache *c = &g_compCache[g_compCur];
    for (int k = 0; k < c->count; k++) if (c->owner[k] == ent && comp_owned_by(c->comp[k], ent)) return c->comp[k];
    return NULL;
}

// Rounds left for a weapon slot of a soldier: loaded (+0x2a4) + spare (+0x2a8). -1 = unknown (no data yet / odd layout).
// Launchers: 1 + spares; medkit / ammo box / radio beacon: 1 while one can be deployed.
#define OFF_WEAPON_AMMO_LOADED     0x2a4
#define OFF_WEAPON_AMMO_SPARE      0x2a8
// The weapon entity layout differs per weapon class (primaries keep their ammo 0x120 further in than pistols and
// gadgets), but the ammo pair always sits 0x110 after a block that reads {.., +0x14 = 24, +0x18 = 344}.
static int soldier_ammo2(char *soldier, int slot, int *loaded, int *spare)
{
    // the soldier's own weapon slots: [soldier+0x53c]+0x30 -> weapon entity per slot
    if (!soldier || slot < 0 || slot > 7 || IsBadReadPtr(soldier + 0x53c, 4)) return 0;
    char *q = *(char **)(soldier + 0x53c);
    if (!q || IsBadReadPtr(q + 0x30, 4)) return 0;
    char **slots = *(char ***)(q + 0x30);
    if (!slots || IsBadReadPtr(slots, 32)) return 0;
    char *w = slots[slot];
    if (!w || IsBadReadPtr(w, 0x800)) return 0;
    for (int o = 0x100; o + 0x118 <= 0x800; o += 4) {
        if (*(int *)(w + o + 0x14) != 24 || *(int *)(w + o + 0x18) != 344) continue;
        int l = *(int *)(w + o + 0x110), s = *(int *)(w + o + 0x114);
        if (l < 0 || l > 500 || s < 0 || s > 2000) return 0;
        *loaded = l; *spare = s;
        return 1;
    }
    return 0;
}
static int soldier_ammo(char *soldier, int slot)
{
    int l, s;
    return soldier_ammo2(soldier, slot, &l, &s) ? l + s : -1;
}
static int bot_ammo(int i, int slot) { return soldier_ammo(*(char **)(H->bots[i] + OFF_PLAYER_SOLDIER), slot); }

// Spotted right now (by anyone, humans included): the entity's spotting component runs a countdown at +0x30.
static int is_entity_spotted(char *ent)
{
    char *comp = ent ? find_entity_component(ent) : NULL;
    return comp && !IsBadReadPtr(comp + 0x30, 4) && *(float *)(comp + 0x30) > 0;
}
static int is_spotted(char *pl)
{
    char *ent = *(char **)(pl + OFF_PLAYER_VEHICLE);
    return is_entity_spotted(ent ? ent : *(char **)(pl + OFF_PLAYER_SOLDIER));
}
// is_spotted, re-read at most every 1/3 s per player (it walks the component cache)
static int spotted_cached(char *pl)
{
    static char *who[64];
    static unsigned at[64];
    static unsigned char val[64];
    int k, oldest = 0;
    for (k = 0; k < 64; k++) {
        if (who[k] == pl) break;
        if (at[k] < at[oldest]) oldest = k;
    }
    if (k == 64) { k = oldest; who[k] = pl; at[k] = 0; }
    if (!at[k] || H->frame - at[k] >= 10) { val[k] = (unsigned char)is_spotted(pl); at[k] = H->frame ? H->frame : 1; }
    return val[k];
}

// "Enemy over there!": when a bot spots a soldier, teammate bots close enough to hear it that are not busy turn and
// engage it from where they stand (no charging off); if they cannot see it they drop it after a few seconds.
#define ALERT_RADIUS           30.0f
#define ENGAGE_RANGE_V8        (g_enhanced.infantryRange * bm_aggr(&g_enhanced,1,1.1f,1.15f))   // Caspian V3: V2 x1.35 started long-range fights that rarely hit
// AltBotMod V9: by the bot's profile (elite: 110 m - fights where shots land, the dev build's choice; V8 started them at 207 m)
static float engage_range(int i) { const BMDifficulty *P = bot_profile(i); return P ? P->engageRange : ENGAGE_RANGE_V8; }
#define ENGAGE_RANGE           engage_range(i)
static void alert_nearby(char *spotter, char *target)
{
    char *ss = *(char **)(spotter + OFF_PLAYER_SOLDIER), *ts = *(char **)(target + OFF_PLAYER_SOLDIER);
    if (!ss || !ts) return;
    float *sp = (float *)(ss + OFF_SOLDIER_POSITION), *tp = (float *)(ts + OFF_SOLDIER_SPAWN_POSITION);
    int team = player_team(spotter), told = 0;
    for (int j = 0; j < H->botCount; j++) {
        char *pl = H->bots[j], *os = *(char **)(pl + OFF_PLAYER_SOLDIER);
        BotBrain *o = bot_brain(j);
        if (pl == spotter || !os || !o->enabled || player_team(pl) != team || player_vehicle(pl)) continue;
        if (o->task != TASK_NONE && o->task != TASK_DEFEND && o->task != TASK_FOLLOW) continue;   // fighting / supplying / ...
        float *op = (float *)(os + OFF_SOLDIER_POSITION);
        if (dist2(op, sp) > ALERT_RADIUS * ALERT_RADIUS) continue;
        float range = is_recon(j) ? RECON_SPOTTED_RANGE : engage_range(j);
        if (dist2(op, tp) > range * range) continue;
        if (H->frame < g_engageCooldown[j] && g_cooldownWho[j] == target) continue;
        {   // only a teammate that can see it from where it stands: the rest keep going (they used to all stop and face an
            // enemy only the spotter saw - a squad stood 30 m short of an enemy flag for minutes, fighting each new spawn)
            float eye[3] = {op[0], op[1] + 1.6f, op[2]}, chest[3] = {tp[0], tp[1] + 1.2f, tp[2]};
            if (solid_between(eye, chest, NULL)) continue;
        }
        start_fight(j, target, 0, "heard it called out");
        g_fightAdvance[j] = !is_recon(j) && o->prevTask==TASK_NONE;
        told++;
    }
    if (told) bot_log("alert: %s called out %s - %d teammate(s) turn to engage", spotter + OFF_PLAYER_ONLINE_NAME, target + OFF_PLAYER_ONLINE_NAME, told);
}

static int bot_spot(char *spotter, char *target)
{
    char *ent = *(char **)(target + OFF_PLAYER_VEHICLE);
    if (!ent) ent = *(char **)(target + OFF_PLAYER_SOLDIER);
    if (!ent) return 0;
    char *comp = find_entity_component(ent);
    if (!comp) { bot_log("spot: no component for %s's %s", target + OFF_PLAYER_ONLINE_NAME, ent == *(char **)(target + OFF_PLAYER_SOLDIER) ? "soldier" : "vehicle"); return 0; }
    ((MarkSpotted_t)ADDR_COMP_MARK_SPOTTED)(comp, spotter);
    bot_log("spot: %s spotted %s (%s, comp %p, timer %.1f)", spotter + OFF_PLAYER_ONLINE_NAME, target + OFF_PLAYER_ONLINE_NAME,
            *(char **)(target + OFF_PLAYER_VEHICLE) ? "vehicle" : "soldier", comp, *(float *)(comp + 0x30));
    if (is_bot(spotter) && !*(char **)(target + OFF_PLAYER_VEHICLE)) alert_nearby(spotter, target);
    return 1;
}

// ---- Detection: every 0.5 s each bot looks for enemies in a 120 deg cone with line of sight and spots them ----
#define DETECT_RANGE_V8        (g_enhanced.infantryRange * 1.10f * bm_aggr(&g_enhanced,1,1.2f,1.35f))
#define DETECT_RANGE_INFANTRY  (bot_profile(i) ? bot_profile(i)->detectRange : DETECT_RANGE_V8)
#define DETECT_RANGE_VEHICLE   300.0f
#define DETECT_COS_V8          bm_aggr(&g_enhanced,0.22f,0.0f,-0.2f)     // ~77 / 90 / 102 deg either side of the heading (Aggression 0/1/2)
#define DETECT_COS_HALF_FOV    (bot_profile(i) ? cosf(bot_profile(i)->fov * 0.01745329f) : DETECT_COS_V8)   // V9: elite 110 deg either side
#define DETECT_ALL_AROUND      bm_aggr(&g_enhanced,15.0f,28.0f,45.0f)   /* V8: 45 m heard at Aggression 2 */     // closer than this: noticed in any direction (heard)
#define SPOT_COOLDOWN_FRAMES   (8 * 30)
static unsigned g_spottedAt[64];          // by player slot: frame of the last bot spot
static char *g_spottedWho[64];

// ---- Engaging what a bot has seen and spotted: armour is left to engineers with rockets (rocket_scan), everyone
// else carries on. Infantry: medics, support and engineers advance on it while firing; recon fires from low cover.
// After a fight (target down / out of sight / enough) a bot does not start another one on its own for a while. ----
static int find_firing_spot(int i, char *soldier, const float *teye, const float *taim, float *out);
static void engage_seen(int i, char *t, char *veh, float d)
{
    BotBrain *b = bot_brain(i);
    char *pl = H->bots[i], *soldier = *(char **)(pl + OFF_PLAYER_SOLDIER);
    if (!soldier || veh || d > (is_recon(i) ? sniper_reach(i) : ENGAGE_RANGE)) return;   // vehicles: rocket_scan (engineers only)
    int free = b->task == TASK_NONE || b->task == TASK_DEFEND || b->task == TASK_FOLLOW;
    if (!free && !(b->task == TASK_SUPPLY && d < 20.0f)) return;   // a supply run gives way to an enemy this close
    if (!is_spotted(t)) return;
    if (H->frame < g_engageCooldown[i] && (t == g_cooldownWho[i] || H->frame + ENGAGE_COOLDOWN_FRAMES - ENGAGE_BREATHER_FRAMES < g_engageCooldown[i])) return;
    if (b->task == TASK_SUPPLY) {
        bot_log("brain: %s drops its supply run - enemy %s at %.0f m", pl + OFF_PLAYER_ONLINE_NAME, t + OFF_PLAYER_ONLINE_NAME, d);
        b->task = b->prevTask; b->commander = b->prevCommander; b->serviceTarget = NULL; b->step = 0;
    }
    int recon = strstr(BOT_ROSTER(pl), "Recon") != NULL;
    // AltBotMod V7: only a flag still being taken holds its bots in place to fight; once it is ours they fight on the
    // move towards the next one (V6: everyone at a captured flag kept "holding the flag" and the push stalled).
    int atFlag = b->targetFlag >= 0 && b->waiting && objective_wanted(player_team(pl), b->targetFlag) &&
                 dist2((float *)(soldier + OFF_SOLDIER_POSITION), flag_pos(b->targetFlag)) < 20.0f * 20.0f;
    int advance = !recon && !atFlag && b->task == TASK_NONE;   // guards, followers and flag holders fight from where they are
    char *ts = *(char **)(t + OFF_PLAYER_SOLDIER);
    float *tp = (float *)(ts + OFF_SOLDIER_SPAWN_POSITION);
    float teye[3] = {tp[0], tp[1] + 1.6f, tp[2]}, taim[3] = {tp[0], tp[1] + 1.0f, tp[2]}, spot[3];
    int perched = sniper_holding(i);   // V10: on its perch it shoots from there
    int moving = recon && !atFlag && !perched && find_firing_spot(i, soldier, teye, taim, spot);   // (plans the path)
    start_fight(i, t, 0, recon ? (perched ? "spotted - sniping from high ground" : moving ? "spotted - moving to low cover" : "spotted - firing from here") : advance ? "spotted - advancing" : "spotted");
    g_fightAdvance[i] = (unsigned char)advance;
    if (atFlag)   // the others holding this flag who can see it engage it too
        for (int j = 0; j < H->botCount; j++) {
            BotBrain *o = bot_brain(j);
            char *os = *(char **)(H->bots[j] + OFF_PLAYER_SOLDIER);
            if (j == i || !os || !o->enabled || o->task != TASK_NONE || o->targetFlag != b->targetFlag || !o->waiting) continue;
            if (player_team(H->bots[j]) != player_team(pl) || player_vehicle(H->bots[j])) continue;
            float *op = (float *)(os + OFF_SOLDIER_POSITION), oeye[3] = {op[0], op[1] + 1.6f, op[2]};
            if (dist2(op, tp) > engage_range(j) * engage_range(j) || solid_between(oeye, taim, NULL)) continue;
            start_fight(j, t, 0, "helping hold the flag");
        }
    if (moving) {
        memcpy(g_fightSpot[i], spot, 12);
        g_fightMoveUntil[i] = H->frame + 8 * 30;
        bot_nav(i)->mode = 3;
    }
}

static void detect_tick(int i)
{
    char *pl = H->bots[i], *s = *(char **)(pl + OFF_PLAYER_SOLDIER);
    if (!s || player_vehicle(pl) || !bot_brain(i)->enabled) return;   // (brain off by hand: driven from the console)
    char *pm = player_manager();
    if (!pm) return;
    float *me = (float *)(s + OFF_SOLDIER_POSITION), yaw = *(float *)(s + OFF_SOLDIER_YAW);
    float fx = sinf(yaw), fz = cosf(yaw);
    void **arr=NULL;unsigned count=0;
    memcpy(&arr,pm+0xac,sizeof arr);memcpy(&count,pm+8,sizeof count);
    char *best = NULL, *bestVeh = NULL;
    float bestD = 1e30f;
    unsigned bestK = 0;
    struct { char *player, *vehicle; float distance; unsigned slot; } candidate[8];
    int nCandidates = 0;
    for (unsigned k = 0; k < count && k < 64; k++) {
        char *t = arr[k];
        if (!t || t == pl || player_team(t) == player_team(pl)) continue;
        char *ts = *(char **)(t + OFF_PLAYER_SOLDIER), *veh = *(char **)(t + OFF_PLAYER_VEHICLE);
        if (!ts || *(float *)(ts + OFF_SOLDIER_HEALTH) <= 0) continue;
        float *tp = (float *)(ts + OFF_SOLDIER_SPAWN_POSITION);   // transform: follows the seat in vehicles
        float dx = tp[0] - me[0], dy = tp[1] - me[1], dz = tp[2] - me[2], d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d < 0.5f) continue;
        float facing = (dx * fx + dz * fz) / sqrtf(dx * dx + dz * dz + 1e-6f);
        int own = d <= (veh ? DETECT_RANGE_VEHICLE : is_recon(i) ? RECON_SPOTTED_RANGE : DETECT_RANGE_INFANTRY) &&
                  (d <= DETECT_ALL_AROUND || facing >= DETECT_COS_HALF_FOV);
        // V10: a sniper glassing through its scope (35 deg either side of where it looks) sees soldiers out to its reach
        if (!own && !veh && d <= sniper_reach(i) && facing >= 0.82f && sniper_rifle(i)) own = 1;
        // not in view: a soldier a teammate has spotted shows on the map / 3D marker - look for it there
        if (!own && (veh || d > (is_recon(i) ? sniper_reach(i) : ENGAGE_RANGE) || !spotted_cached(t))) continue;
        int at = nCandidates;
        if (at == 8) { if (d >= candidate[7].distance) continue; at = 7; }
        else nCandidates++;
        while (at > 0 && d < candidate[at-1].distance) { candidate[at] = candidate[at-1]; at--; }
        candidate[at].player = t; candidate[at].vehicle = veh;
        candidate[at].distance = d; candidate[at].slot = k;
    }
    // Range/FOV tests cover every player; bound expensive sight rays.
    // AltBotMod V17: soldiers on foot first, then vehicles - a helicopter overhead no longer hides the soldier behind it
    // (an aircraft is the AA launchers' job: engage_seen leaves vehicles to rocket_scan)
    for (int pass = 0; pass < 2 && !best; pass++)
    for (int k = 0; k < nCandidates; k++) {
        char *t = candidate[k].player, *veh = candidate[k].vehicle;
        if ((veh != NULL) != pass) continue;
        char *ts = *(char **)(t + OFF_PLAYER_SOLDIER);
        float *tp = (float *)(ts + OFF_SOLDIER_SPAWN_POSITION), d = candidate[k].distance;
        float eye[3] = {me[0], me[1] + 1.6f, me[2]}, aim[3] = {tp[0], tp[1] + (veh ? 1.5f : 1.2f), tp[2]}, hit[3];
        unsigned saved = g_rayFlags;
        g_rayFlags = RAY_DONT_CHECK_CHARACTER;   // soldiers never block the view; walls, terrain and vehicles do
        int blocked = ray_cast(eye, aim, hit, NULL);
        g_rayFlags = saved;
        if (blocked) {
            float hx = hit[0] - aim[0], hy = hit[1] - aim[1], hz = hit[2] - aim[2];
            if (!veh || hx * hx + hy * hy + hz * hz > 5.0f * 5.0f) continue;   // hit something else than the target vehicle
        }
        bestD = d; best = t; bestVeh = veh; bestK = candidate[k].slot;
        break;
    }
    if (!best) return;
    // identify first: spot it unless it was spotted by a bot just now
    if (!(g_spottedWho[bestK] == best && H->frame - g_spottedAt[bestK] < SPOT_COOLDOWN_FRAMES) && bot_spot(pl, best)) {
        g_spottedAt[bestK] = H->frame;
        g_spottedWho[bestK] = best;
        bot_log("detect: %s sees %s%s at %.0f m", pl + OFF_PLAYER_ONLINE_NAME, best + OFF_PLAYER_ONLINE_NAME, bestVeh ? " (vehicle)" : "", bestD);
    }
    engage_seen(i, best, bestVeh, bestD);
}

// Replies are delayed so they do not drown under the human's own voice line.
#define SAY_QUEUE 4
typedef struct SayItem { int type; unsigned at; } SayItem;
static SayItem g_sayQueue[MAX_BOTS][SAY_QUEUE];

static void bot_say_later(int i, int type, unsigned delayFrames)
{
    for (int k = 0; k < SAY_QUEUE; k++)
        if (!g_sayQueue[i][k].type) { g_sayQueue[i][k].type = type; g_sayQueue[i][k].at = H->frame + delayFrames; return; }
}

static void say_tick(int i)
{
    for (int k = 0; k < SAY_QUEUE; k++)
        if (g_sayQueue[i][k].type && H->frame >= g_sayQueue[i][k].at) { bot_say(i, g_sayQueue[i][k].type); g_sayQueue[i][k].type = 0; }
}

static void kill_bot(char *pl)
{
    void *s = *(void **)(pl + OFF_PLAYER_SOLDIER);
    if (!s || IsBadReadPtr(s,4) || *(unsigned *)s!=ADDR_SOLDIER_VTABLE) return;
    typedef void (__attribute__((thiscall)) *Kill_t)(void *soldier, int arg);
    ((Kill_t)(*(void ***)s)[0x170 / 4])(s, 0);   // same call as NetworkSuicideMessage
}

// ---- Button presses (digital actions: setLevel != 0 sets the button bit) ----
#define MAX_PRESSES 8
typedef struct BotPress { int action; int frames; } BotPress;
static BotPress g_press[MAX_BOTS][MAX_PRESSES];

static void press_button(int i, int action, int frames)
{
    if (action == 12) bm_learn(i, BM_LEARN_JUMP, NULL, NULL);   // AltBotMod V21: did a jump get it through?
    // already held: hold it longer (a fresh press every frame overlapped the old ones, and each expiring one let the
    // button go - a held sprint mostly read as off)
    for (int k = 0; k < MAX_PRESSES; k++)
        if (g_press[i][k].frames > 0 && g_press[i][k].action == action) { if (frames > g_press[i][k].frames) g_press[i][k].frames = frames; return; }
    for (int k = 0; k < MAX_PRESSES; k++)
        if (g_press[i][k].frames <= 0) { g_press[i][k].action = action; g_press[i][k].frames = frames; return; }
}

#define MAX_SCHEDULED 8
typedef struct ScheduledPress { int action; unsigned at; } ScheduledPress;
static ScheduledPress g_sched[MAX_BOTS][MAX_SCHEDULED];
void schedule_press(int i, int action, unsigned at)
{
    for (int k = 0; k < MAX_SCHEDULED; k++)
        if (!g_sched[i][k].at) { g_sched[i][k].action = action; g_sched[i][k].at = at; return; }
}

static char *player_vehicle(char *pl);
static unsigned g_driverPressesDropped;
static void apply_presses(int i, void *input)
{
    for (int k = 0; k < MAX_SCHEDULED; k++)
        if (g_sched[i][k].at && H->frame >= g_sched[i][k].at) { press_button(i, g_sched[i][k].action, 3); g_sched[i][k].at = 0; }
    // AltBotMod V29: at the controls of a vehicle, a soldier's jump / crouch / prone (stance, stairs, stuck and learned
    // "jump here" presses from the on-foot code) never reach the vehicle - in a vehicle seat they are the handbrake and the
    // like. Fun-Bots stands its drivers up for the same reason.
    int driving = g_driving[i] && player_vehicle((char *)H->bots[i]);   // (pilots too)
    for (int k = 0; k < MAX_PRESSES; k++) {
        BotPress *p = &g_press[i][k];
        if (driving && p->frames > 0 && (p->action == EIA_JUMP || p->action == EIA_CHANGE_POSE || p->action == EIA_PRONE || p->action == g_crouchAction)) {
            p->frames = 0; g_driverPressesDropped++; continue;
        }
        if (p->frames > 0) {
            ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(input, p->action, 1.0f);
            if (--p->frames == 0) ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(input, p->action, 0.0f);
        }
    }
}

// ---- Message trace: log every distinct message type dispatched through the MessageManager ----
#define ADDR_MSGMGR_DISPATCH       0x005495e0  // MessageManager::dispatch(Message*)
static const unsigned char kMsgDispatchPrologue[10] = {0x83, 0xEC, 0x18, 0x80, 0x3D, 0xA8, 0xA0, 0xAC, 0x02, 0x00}; // sub esp,0x18; cmp byte [0x2aca0a8],0
static volatile int g_traceMessages;
static const char *g_seenMsg[512];
static int g_seenMsgCount;

static const char *message_name(void *msg)
{
    // Copy the pointer chain before inspection; a malformed/tearing-down
    // message must not crash a round transition just to obtain diagnostics.
    static struct {LONG vtable;const char *volatile name;}cache[256];
    uintptr_t vt=0,fn=0;unsigned char code[6];SIZE_T got=0;int slot=-1;
    if(!msg || !ReadProcessMemory(GetCurrentProcess(),msg,&vt,sizeof vt,&got) || got!=sizeof vt ||
       vt<0x00400000 || vt>=0x02800000)return NULL;
    // Immutable per-vtable entries: no collision replacement and no expensive
    // function/string inspection on every high-volume network message.
    for(unsigned n=0;n<8;n++){
        int k=((vt>>4)+n)&255;
        LONG observed=InterlockedCompareExchange(&cache[k].vtable,(LONG)vt,0);
        if(observed==0 || observed==(LONG)vt){
            slot=k;if(cache[k].name)return cache[k].name;break;
        }
    }
    if(!ReadProcessMemory(GetCurrentProcess(),(void *)(vt+2*sizeof(uintptr_t)),&fn,sizeof fn,&got) ||
       got!=sizeof fn || fn<0x00400000 || fn>=0x02800000)return NULL;
    if(!ReadProcessMemory(GetCurrentProcess(),(void *)fn,code,sizeof code,&got) || got!=sizeof code || code[0]!=0xB8)return NULL;
    uintptr_t address=0;memcpy(&address,code+1,4);
    const char *name=(const char *)address;
    if(address<0x00400000 || address>=0x02800000 || IsBadStringPtrA(name,128))return NULL;
    if(slot>=0)cache[slot].name=name; // aligned atomic pointer store on this PE32 target
    return name;
}

#define ADDR_MSG_GET_CONNECTION    0x01a089e0  // NetworkMessage -> ServerConnection* (thiscall)
#define ADDR_CONNECTION_GET_PLAYER 0x00e3b0e0  // ServerConnection -> ServerPlayer* (thiscall)
typedef void *(__attribute__((thiscall)) *GetPtr_t)(void *self);
static volatile int g_traceOrders = 1;

static char *message_sender(void *msg)
{
    void *conn = ((GetPtr_t)ADDR_MSG_GET_CONNECTION)(msg);
    return conn ? ((GetPtr_t)ADDR_CONNECTION_GET_PLAYER)(conn) : NULL;
}

static void order_reaction(char *sender, int type, char *target);

static const char *describe_ptr(void *p, char *buf, int n);
static void on_order_message(unsigned char *msg)
{
    char *pl = message_sender(msg);
    int type = *(int *)(msg + OFF_ORDER_TYPE);
    bot_log("order from %s: %s (%d)", pl ? pl + OFF_PLAYER_ONLINE_NAME : "?", type >= 0 && type < 18 ? kOrderNames[type] : "?", type);
    if (g_traceMessages) {   // research: every field, with player / soldier / vehicle pointers named
        char line[600], d[64];
        int n = snprintf(line, sizeof line, "  order raw:");
        for (int off = 0x28; off < 0x60; off += 4) {
            unsigned v = *(unsigned *)(msg + off);
            const char *w = describe_ptr((void *)v, d, sizeof d);
            if (*w) n += snprintf(line + n, sizeof line - n, " +%02x=%s", off, w);
            else n += snprintf(line + n, sizeof line - n, " +%02x=%08x", off, v);
            if (n > (int)sizeof line - 40) break;
        }
        bot_log("%s", line);
    }
    // the same order is dispatched several times (one per listener): react once per sender+type within 15 frames
    static char *lastSender; static int lastType; static unsigned lastFrame;
    if (pl == lastSender && type == lastType && H->frame - lastFrame < 15) return;
    lastSender = pl; lastType = type; lastFrame = H->frame;
    // quick-Q requests (look at a teammate + Q) name that teammate: +0x44 is a weak ref to the player (+8)
    char *target = NULL;
    void **ref = *(void ***)(msg + 0x44);
    if (ref && !IsBadReadPtr(ref, 4) && *ref) target = (char *)*ref - 8;
    if (target && !is_bot(target)) target = NULL;
    if (target) bot_log("  (quick request to %s)", target + OFF_PLAYER_ONLINE_NAME);
    if (pl && !is_bot(pl)) order_reaction(pl, type, target);
}

// Names a pointer if it is a player, a player's soldier or a player's vehicle ("" otherwise).
static const char *describe_ptr(void *p, char *buf, int n)
{
    buf[0] = 0;
    char *pm = player_manager();
    if (!pm || !p) return buf;
    void **arr = *(void ***)(pm + 0xac);
    unsigned count = *(unsigned *)(pm + 8);
    for (unsigned i = 0; i < count; i++) {
        char *pl = arr[i];
        if (!pl) continue;
        const char *nm = pl + OFF_PLAYER_ONLINE_NAME;
        if (p == pl) snprintf(buf, n, "player %s", nm);
        else if (p == *(void **)(pl + OFF_PLAYER_SOLDIER)) snprintf(buf, n, "soldier of %s", nm);
        else if (p == *(void **)(pl + OFF_PLAYER_VEHICLE)) snprintf(buf, n, "vehicle of %s", nm);
        else continue;
        return buf;
    }
    return buf;
}

// Spotting: dump the message and name every pointer (directly or inside an fb array) that is a player/soldier/vehicle.
static void on_spotting_message(unsigned char *msg)
{
    char *pl = message_sender(msg);
    bot_log("spotting from %s:", pl ? pl + OFF_PLAYER_ONLINE_NAME : "?");
    char d[64];
    for (int off = 0; off < 0x80; off += 4) {
        unsigned v = *(unsigned *)(msg + off);
        const char *what = describe_ptr((void *)v, d, sizeof d);
        if (*what) { bot_log("  +%02x %08x = %s", off, v, what); continue; }
        if (v >= 0x10000 && !IsBadReadPtr((void *)(v - 4), 8)) {   // fb array: count at ptr-4
            unsigned cnt = *(unsigned *)(v - 4);
            if (cnt > 0 && cnt < 16 && !IsBadReadPtr((void *)v, cnt * 4))
                for (unsigned k = 0; k < cnt; k++) {
                    char d2[64];
                    const char *w2 = describe_ptr(((void **)v)[k], d2, sizeof d2);
                    if (*w2) bot_log("  +%02x[%u] %08x = %s", off, k, ((unsigned *)v)[k], w2);
                }
        }
    }
    char line[400];
    int n = 0;
    for (int off = 0; off < 0x80; off += 4) n += snprintf(line + n, sizeof line - n, "%s%08x", off % 32 ? " " : " | ", *(unsigned *)(msg + off));
    bot_log("  raw%s", line);
}

// Vehicle damage (research): dump the message, name player/vehicle pointers and print floats.

// Who fired recently: ServerSoldier.Firing carries the shooter's soldier (or player) somewhere in its fields.
#define MAX_SHOOTERS 64
static char *g_firedBy[MAX_SHOOTERS];
static unsigned g_firedAt[MAX_SHOOTERS];
static void on_firing_message(unsigned char *msg)
{
    char *pm = player_manager();
    if (!pm) return;
    void **arr = *(void ***)(pm + 0xac);
    unsigned count = *(unsigned *)(pm + 8);
    for (int off = 0x28; off < 0x60; off += 4) {
        void *v = *(void **)(msg + off);
        if (!v) continue;
        for (unsigned k = 0; k < count && k < MAX_SHOOTERS; k++) {
            char *pl = arr[k];
            if (!pl || (v != pl && v != *(void **)(pl + OFF_PLAYER_SOLDIER))) continue;
            g_firedBy[k] = pl;
            g_firedAt[k] = H->frame;
            return;
        }
    }
}
static int fired_recently(char *pl)
{
    for (int k = 0; k < MAX_SHOOTERS; k++) if (g_firedBy[k] == pl && H->frame - g_firedAt[k] < 45) return 1;
    return 0;
}
static unsigned g_botFiredAt[MAX_BOTS];   // a bot's trigger last held by BotMod (shots without a Firing message too)
static int fired_within(char *pl, unsigned frames)
{
    for (int k = 0; k < MAX_SHOOTERS; k++) if (g_firedBy[k] == pl && H->frame - g_firedAt[k] < frames) return 1;
    int i = bot_index(pl);
    return i >= 0 && g_botFiredAt[i] && H->frame - g_botFiredAt[i] < frames;
}

// Projectile impacts (research): the server sees every bullet hit, bots' included - dump the fields, naming pointers
// (one level deep too) to find shooter, victim and damage.
// Where in a bullet object is its shooter? Look for weak refs (cell -> player + 8) and for the soldier itself.


// ServerSoldierEntity::onDamage (soldier vtable +0x110, thiscall, 3 args, ret 0xc) - research: log real calls.
#define ADDR_SOLDIER_ON_DAMAGE     0x00e08000
typedef void (__attribute__((thiscall)) *OnDamage_t)(void *soldier, void *a1, void *a2, void *a3);
static void dump_named(const char *tag, unsigned char *p, int len)
{
    char d[64], line[900];
    int n = snprintf(line, sizeof line, "%s %p:", tag, p);
    if (!p || IsBadReadPtr(p, len)) { bot_log("%s (unreadable)", line); return; }
    for (int off = 0; off < len && n < (int)sizeof line - 60; off += 4) {
        unsigned v = *(unsigned *)(p + off);
        const char *what = describe_ptr((void *)v, d, sizeof d);
        float f = *(float *)(p + off);
        if (*what) n += snprintf(line + n, sizeof line - n, " +%02x=%s", off, what);
        else if (v && (f > 0.001f || f < -0.001f) && f > -1e6f && f < 1e6f) n += snprintf(line + n, sizeof line - n, " +%02x=%.2f", off, f);
        else n += snprintf(line + n, sizeof line - n, " +%02x=%08x", off, v);
    }
    bot_log("%s", line);
}
int __cdecl bot_damage_giver(void *giver);
static void note_attacker(void *soldier, void *giver)
{
    if (!giver || IsBadReadPtr(giver, 0x30)) return;
    for (int j = 0; j < H->botCount; j++) {
        char *bot = H->bots[j];
        if (*(void **)(bot + OFF_PLAYER_SOLDIER) != soldier) continue;
        typedef char *(__attribute__((thiscall)) *GetPlayer_t)(void *);
        char *pl = ((GetPlayer_t)0x00dd1de0)(giver);
        if (pl && pl != bot && player_exists(pl) && player_team(pl) != player_team(bot)) {
            g_attacker[j] = pl;
            g_attackedAt[j] = H->frame;
            g_attackerNoted++;
        }
        return;
    }
}
// Caspian V5: BotDamage= scales the damage of bot-fired hits. The damage value's place in the damage record (a1)
// is not documented, so it is found at run time: for bot hits that leave the victim alive, the float field whose
// value equals the health actually lost is counted; a field that matched 6 times and disagreed at most 1 in 4 times
// (head and limb multipliers) is the damage.
// Until then nothing is scaled. Search space: the first 0x60 bytes of a1.
static int g_dmgOffset = -1;
static unsigned char g_dmgMatch[0x60 / 4], g_dmgMiss[0x60 / 4];
static unsigned g_dmgScaled, g_dmgProbes;
// Once found, the field is checked: over 8 scaled hits the health lost must follow the scaled value, not the original
// (a record holding a raw copy beside the value the game uses would match just as well). If not, that field is
// struck off and the search goes on.
static unsigned char g_dmgBanned[0x60 / 4], g_dmgGood, g_dmgBad, g_dmgVerified;
// AltBotMod V16.2: the shooter's player, if the damage giver has one (the same call note_attacker uses)
static char *damage_giver_player(void *giver)
{
    if (!giver || IsBadReadPtr(giver, 0x30)) return NULL;
    typedef char *(__attribute__((thiscall)) *GetPlayer_t)(void *);
    char *pl = ((GetPlayer_t)0x00dd1de0)(giver);
    return pl && player_exists(pl) ? pl : NULL;
}
static float giver_vehicle_factor(void *giver)
{
    if (g_enhanced.vehicleDamageDealt == 1.0f) return 1.0f;
    char *pl = damage_giver_player(giver);
    return pl && player_vehicle(pl) ? g_enhanced.vehicleDamageDealt : 1.0f;
}
void __attribute__((thiscall)) hook_soldier_on_damage(void *soldier, void *a1, void *a2, void *a3)
{
    note_attacker(soldier, a2);
    // With client-side hit detection on, damage from a server-simulated bullet (a1+0x58 bit 0x10) is ignored - the
    // shooter's client is expected to report the hit. Bots have no client: their bullets count as reported.
    int fromBot = a1 && bot_damage_giver(a2);
    if (fromBot && !IsBadWritePtr((char *)a1 + 0x58, 1) && (*((unsigned char *)a1 + 0x58) & 0x10))
        *((unsigned char *)a1 + 0x58) &= ~0x10;
    float *health = soldier && !IsBadReadPtr((char *)soldier + OFF_SOLDIER_HEALTH, 4) ? (float *)((char *)soldier + OFF_SOLDIER_HEALTH) : NULL;
    float before = health ? *health : 0, fields[0x60 / 4];
    int probe = fromBot && health && g_dmgOffset < 0 && !IsBadReadPtr(a1, 0x60) && g_dmgProbes < 4000;
    if (probe) memcpy(fields, a1, sizeof fields);
    float scaled = 0, original = 0;
    // AltBotMod V16.2: x VehicleDamageDealt when the shooter sits in a vehicle (anyone's, bots' and players')
    float factor = (fromBot ? g_enhanced.botDamage : 1.0f) * giver_vehicle_factor(a2);
    if (a1 && g_dmgOffset >= 0 && factor != 1.0f && (fromBot || g_dmgVerified) && !IsBadWritePtr((char *)a1 + g_dmgOffset, 4)) {
        float *dmg = (float *)((char *)a1 + g_dmgOffset);
        if (isfinite(*dmg) && *dmg > 0 && *dmg < 5000) { original = *dmg; scaled = original * factor; *dmg = scaled; g_dmgScaled++; }
    }
    ((OnDamage_t)ADDR_SOLDIER_ON_DAMAGE)(soldier, a1, a2, a3);
    if (scaled > 0 && !IsBadWritePtr((char *)a1 + g_dmgOffset, 4)) *(float *)((char *)a1 + g_dmgOffset) = original;   // leave the record as it was
    if (scaled > 0 && !g_dmgVerified && health && fabsf(scaled - original) > 1.0f) {
        float after = *health, lost = before - after;
        if (after > 0.5f && lost > 0.5f && lost < before) {
            if (fabsf(lost - scaled) < fabsf(lost - original)) g_dmgGood++; else g_dmgBad++;
            if (g_dmgGood + g_dmgBad >= 8) {
                if (g_dmgGood > g_dmgBad) {
                    g_dmgVerified = 1;
                    bot_log("damage: +0x%02x confirmed - %u of %u bot hits took the scaled damage", g_dmgOffset, g_dmgGood, g_dmgGood + g_dmgBad);
                } else {
                    bot_log("damage: +0x%02x is not what the game uses (%u of %u hits took the unscaled damage) - searching on", g_dmgOffset, g_dmgBad, g_dmgGood + g_dmgBad);
                    g_dmgBanned[g_dmgOffset / 4] = 1; g_dmgOffset = -1;
                    memset(g_dmgMatch, 0, sizeof g_dmgMatch); memset(g_dmgMiss, 0, sizeof g_dmgMiss);
                    g_dmgProbes = 0;
                }
                g_dmgGood = g_dmgBad = 0;
            }
        }
    }
    if (probe) {
        float after = *health, lost = before - after;
        g_dmgProbes++;
        if (after > 0.5f && lost > 0.5f && lost < before) {
            for (int k = 0; k < 0x60 / 4; k++) {
                float f = fields[k];
                if (!isfinite(f) || f <= 0.3f || f > 400) continue;
                if (fabsf(f - lost) < 0.35f) { if (g_dmgMatch[k] < 255) g_dmgMatch[k]++; }
                else if (g_dmgMatch[k] && g_dmgMiss[k] < 255) g_dmgMiss[k]++;
            }
            for (int k = 0; k < 0x60 / 4; k++) if (!g_dmgBanned[k] && g_dmgMatch[k] >= 6 && g_dmgMiss[k] * 4 <= g_dmgMatch[k]) {   // (head shots lose more than the base damage)
                g_dmgOffset = k * 4;
                bot_log("damage: the damage value sits at +0x%02x of the damage record (%u probes); bot hits now do x%.2f damage",
                        g_dmgOffset, g_dmgProbes, g_enhanced.botDamage);
                break;
            }
        }
    }
}

// AltBotMod V16.2: ServerVehicleEntity::onDamage - the same virtual (vtable +0x110, thiscall, 3 args, ret 0xc, the same
// damage record) as the soldier's. VehicleDamageTaken scales every hit on a vehicle, VehicleDamageDealt the hits from
// someone in a vehicle. Only once the damage field is confirmed on soldiers (g_dmgVerified); the first 8 scaled hits
// are checked against the vehicle's health and the scaling switches itself off if the vehicle did not take it.
#define ADDR_VEHICLE_ON_DAMAGE 0x00df9020
static unsigned g_vehDmgScaled, g_vehDmgGood, g_vehDmgBad; static int g_vehDmgOff, g_vehDmgVerified;
void __attribute__((thiscall)) hook_vehicle_on_damage(void *veh, void *a1, void *a2, void *a3)
{
    float factor = g_enhanced.vehicleDamageTaken * giver_vehicle_factor(a2), original = 0, scaled = 0;
    float *health = veh && !IsBadReadPtr((char *)veh + OFF_VEHICLE_HEALTH, 4) ? (float *)((char *)veh + OFF_VEHICLE_HEALTH) : NULL;
    float before = health ? *health : 0;
    if (factor != 1.0f && !g_vehDmgOff && g_dmgVerified && g_dmgOffset >= 0 && a1 && !IsBadWritePtr((char *)a1 + g_dmgOffset, 4)) {
        float *dmg = (float *)((char *)a1 + g_dmgOffset);
        if (isfinite(*dmg) && *dmg > 0 && *dmg < 100000) { original = *dmg; scaled = original * factor; *dmg = scaled; g_vehDmgScaled++; }
    }
    ((OnDamage_t)ADDR_VEHICLE_ON_DAMAGE)(veh, a1, a2, a3);
    if (scaled > 0 && !IsBadWritePtr((char *)a1 + g_dmgOffset, 4)) *(float *)((char *)a1 + g_dmgOffset) = original;
    if (scaled > 0 && !g_vehDmgVerified && health && fabsf(scaled - original) > 1.0f) {
        float lost = before - *health;
        if (lost > 0.5f && *health > 0.5f) {
            if (fabsf(lost - scaled) < fabsf(lost - original)) g_vehDmgGood++; else g_vehDmgBad++;
            if (g_vehDmgGood + g_vehDmgBad >= 8) {
                if (g_vehDmgGood > g_vehDmgBad) { g_vehDmgVerified = 1; bot_log("damage: vehicles take the scaled damage (%u of 8 hits) - VehicleDamageTaken x%.2f, VehicleDamageDealt x%.2f", g_vehDmgGood, g_enhanced.vehicleDamageTaken, g_enhanced.vehicleDamageDealt); }
                else { g_vehDmgOff = 1; bot_log("damage: vehicles ignored the scaled damage (%u of 8 hits) - vehicle damage scaling off", g_vehDmgBad); }
            }
        }
    }
}

// Bullet hit handler (research): projectile::processHit(hit record 0x40, ...) at 0xe72040 - dump real hit records.
#define ADDR_PROJECTILE_PROCESS_HIT 0x00e72040
static const unsigned char kProcessHitPrologue[6] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};   // push ebp; mov ebp,esp; and esp,-16
static char *g_fightTarget[MAX_BOTS];
static char *soldier_hit_body(char *soldier);
static int solid_between(const float *from, const float *to, float *hitOut);



static int g_gunDebug;
static char *g_gunTarget[MAX_BOTS];
static int patch_jump(unsigned addr, const unsigned char *expect, int len, void *stub, const char *what);
// ---- Health component damage (0x1180190): a soldier whose state wants client-confirmed hits only accepts damage
// records of type 1 (a2+0x28); bot bullets arrive as type 0 and were dropped at 0x11801c5. Let a bot shooter's through. ----
unsigned g_hcBlocked, g_hcBotPass;
int __cdecl bot_damage_giver(void *giver)
{
    if (!giver || IsBadReadPtr(giver, 0x30)) return 0;
    typedef char *(__attribute__((thiscall)) *GetPlayer_t)(void *);
    char *pl = ((GetPlayer_t)0x00dd1de0)(giver);
    int r = pl && is_bot(pl);
    if (r) g_hcBotPass++; else g_hcBlocked++;
    return r;
}
extern void hc_stub(void);
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _hc_stub\n"
    "_hc_stub:\n"
    "  je 3f\n"                           /* type 1: accepted as before */
    "  pushad\n"
    "  push dword ptr [ebp + 0xc]\n"      /* damage giver */
    "  call _bot_damage_giver\n"
    "  add esp, 4\n"
    "  mov dword ptr [_g_gateResult], eax\n"
    "  popad\n"
    "  cmp dword ptr [_g_gateResult], 0\n"
    "  jne 3f\n"
    "  push 0x011803f8\n"                 /* rejected */
    "  ret\n"
    "3:\n"
    "  push 0x011801cb\n"
    "  ret\n"
    ".att_syntax prefix\n");

void __cdecl on_process_hit(char *projectile, unsigned char *hit, unsigned *args)
{
    (void)args;
    // Caspian V3: always on (it was console-only, "gundebug 1"). Where a bot's bullets land next to its fight
    // target feeds the aim calibration (g_aimBias) and the 30 s "aim:" summary.
    if (!projectile || !hit || IsBadReadPtr(projectile + 0x84, 4) || IsBadReadPtr(hit, 0x24)) return;
    void **ref = *(void ***)(projectile + 0x84);
    char *shooter = ref && !IsBadReadPtr(ref, 4) && *ref ? (char *)*ref - 8 : NULL;
    int i = shooter ? bot_index(shooter) : -1;
    if (i < 0 || player_vehicle(shooter) || !g_fightTarget[i] || !player_exists(g_fightTarget[i])) return;
    char *ts = *(char **)(g_fightTarget[i] + OFF_PLAYER_SOLDIER), *me = *(char **)(shooter + OFF_PLAYER_SOLDIER), *ent = *(char **)(hit + 0x20), d[64];
    if (!ts || !me) return;
    float *hp = (float *)hit, *tp = (float *)(ts + OFF_SOLDIER_SPAWN_POSITION), *mp = (float *)(me + OFF_SOLDIER_POSITION);
    if (!isfinite(hp[0]) || !isfinite(hp[1]) || !isfinite(hp[2])) return;
    float dx = tp[0] - mp[0], dz = tp[2] - mp[2], dist = sqrtf(dx * dx + dz * dz) + 1e-6f;
    float rx = hp[0] - tp[0], rz = hp[2] - tp[2], side = (-rx * dz + rz * dx) / dist, along = (rx * dx + rz * dz) / dist;
    int onTarget = ent && !IsBadReadPtr(ent, 0x34) && *(unsigned *)ent == ADDR_HITBODY_VTABLE && *(char **)(ent + 0x30) == ts;
    float rise = hp[1] - tp[1];
    // Project the impact back onto the target's plane along the shooter's line of fire: a bullet that went
    // over the head lands on the ground behind, one that went low hits the ground or cover in front. Counting
    // only impacts AT the target would see the low ones and miss the high ones, and walk the aim up.
    float pitch = *(float *)(me + OFF_SOLDIER_PITCH), reach = dist + along;
    {   // the look pitch when this bullet was fired (flight time ~ dist / 20 frames), not now
        unsigned fired = H->frame - (unsigned)(reach / BULLET_SPEED * 30.0f + 0.5f);
        if (g_pitchHistAt[i][fired & 15] == fired) pitch = g_pitchHist[i][fired & 15];
    }
    if (g_aimValueAt[i] && H->frame - g_aimValueAt[i] < 20 && isfinite(pitch) && fabsf(pitch) < 0.6f &&
        along > -0.8f * dist && along < 150.0f && reach > 2.0f) {
        float sideAt = side * dist / reach, riseAt = rise - along * tanf(pitch);
        riseAt += 0.5f * 9.81f * ((reach / BULLET_SPEED) * (reach / BULLET_SPEED) - (dist / BULLET_SPEED) * (dist / BULLET_SPEED));   // extra drop past the target
        // V4: a wide window, symmetric about the chest (V3's misses were up to 4.7 m high; low ones hit the ground short
        // of the target and project back), and sideways it widens with the correction already applied.
        if (fabsf(sideAt) < 2.0f + fabsf(g_aimFixS[i]) * dist && riseAt > -4.0f && riseAt < 5.0f) {
            // one update per bot per frame (a shotgun's pellets are one shot), none for shotguns at all
            int learn = g_aimUpdAt[i] != H->frame && infantry_weapon(i)->family != BM_W_SHOTGUN;
            g_aimUpdAt[i] = H->frame;
            int pose = g_aimPose[i] < 3 ? g_aimPose[i] : 0;
            // Caspian V4: where the gun really points, relative to the look pitch: the height the pitch alone would
            // give on the target plane (eye + camera rise + drop) against where the bullet crossed it.
            static const float eyeH[3] = {1.6f, 1.05f, 0.45f};
            float drop = 0.5f * 9.81f * (dist / BULLET_SPEED) * (dist / BULLET_SPEED);
            float byPitch = mp[1] + eyeH[pose] + g_aimBias[pose] + dist * tanf(pitch) - drop;
            float y = riseAt + tp[1] - byPitch;   // metres the bullet crossed the target plane above the look line
            float t = g_burstStart[i] ? (float)(H->frame - g_burstStart[i]) - dist / BULLET_SPEED * 30.0f : 0;
            if (t < 0) t = 0;
            if (t > 90) t = 90;
            // V5: offset = H (m) + (S + C*t) * distance; normalised LMS on the three terms
            float x1 = dist / 50.0f, x2 = t * dist / 50.0f, norm = 1.0f + x1 * x1 + x2 * x2;
            float err = y - (g_aimFixH[i] + (g_aimFixV[i] + g_aimClimb[i] * t) * dist);
            err = err < -3.0f ? -3.0f : err > 3.0f ? 3.0f : err;
            if (learn) {   // (review: 0.6 only for the first five impacts, then 0.25 so bullet spread does not jolt the fit)
                float mu = g_aimFixSet[i] && g_aimFixSet[i] < 6 ? 0.6f : 0.25f;
                if (g_aimFixSet[i] && g_aimFixSet[i] < 6) g_aimFixSet[i]++;
                g_aimFixH[i] += mu * err / norm;
                g_aimFixV[i] += mu * err * x1 / norm / 50.0f;
                g_aimClimb[i] += mu * err * x2 / norm / 50.0f;
            }
            if (g_aimFixH[i] < -1.0f) g_aimFixH[i] = -1.0f;
            if (g_aimFixH[i] > 1.5f) g_aimFixH[i] = 1.5f;
            if (g_aimClimb[i] < 0) g_aimClimb[i] = 0;
            if (g_aimClimb[i] > 0.004f) g_aimClimb[i] = 0.004f;
            // Sideways: each impact pulls the yaw correction 25% of the way to cancelling its error.
            float ev = riseAt - g_aimLanding[i], es = sideAt;
            es = es < -1.2f ? -1.2f : es > 1.2f ? 1.2f : es;
            if (learn) g_aimFixS[i] += 0.25f * es / dist;
            if (g_aimFixV[i] < -0.06f) g_aimFixV[i] = -0.06f;
            if (g_aimFixV[i] > 0.12f) g_aimFixV[i] = 0.12f;
            if (g_aimFixS[i] < -0.05f) g_aimFixS[i] = -0.05f;
            if (g_aimFixS[i] > 0.05f) g_aimFixS[i] = 0.05f;
            g_aimFixHAvg += 0.01f * (g_aimFixH[i] - g_aimFixHAvg);
            g_aimFixVAvg += 0.01f * (g_aimFixV[i] - g_aimFixVAvg); g_aimClimbAvg += 0.01f * (g_aimClimb[i] - g_aimClimbAvg);
            g_aimFixSAvg += 0.01f * (g_aimFixS[i] - g_aimFixSAvg);
            g_aimSamples[pose]++;
            g_aimTorso += fabsf(ev) < 0.4f && fabsf(es) < 0.3f;
            g_aimImpacts++; g_aimOnBody += onTarget != 0; g_aimSideSum += fabsf(sideAt); g_aimRiseSum += riseAt;
        }
    }
    if (fabsf(side) > 3.0f || fabsf(along) > 4.0f) return;
    if (!g_gunDebug) return;
    bot_log("aim: %s -> %s %.0f m: %.2f m above feet, %+.2f side, %+.1f along, hit %s", shooter + OFF_PLAYER_ONLINE_NAME,
            g_fightTarget[i] + OFF_PLAYER_ONLINE_NAME, dist, hp[1] - tp[1], side, along, onTarget ? "TARGET" : describe_ptr(ent, d, sizeof d));
}
extern void hook_process_hit(void);
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _hook_process_hit\n"
    "_hook_process_hit:\n"
    "  pushad\n"
    "  pushfd\n"
    "  lea eax, [esp + 36]\n"      /* eax -> [ret][hit][a2][a3][a4] */
    "  push eax\n"
    "  push dword ptr [eax + 4]\n" /* hit record */
    "  push ecx\n"                 /* projectile (this) */
    "  call _on_process_hit\n"
    "  add esp, 12\n"
    "  popfd\n"
    "  popad\n"
    "  push ebp\n"
    "  mov ebp, esp\n"
    "  and esp, 0xfffffff0\n"
    "  push 0x00e72046\n"
    "  ret\n"
    ".att_syntax prefix\n");

int g_gateResult;   // scratch for the health-component stub below

static int patch_jump(unsigned addr, const unsigned char *expect, int len, void *stub, const char *what)
{
    if (memcmp((void *)addr, expect, len) != 0) { bot_log("ERROR: %s bytes mismatch - wrong server build?", what); return 0; }
    unsigned char code[16];
    code[0] = 0xE9;
    *(DWORD *)(code + 1) = (DWORD)((unsigned char *)stub - (unsigned char *)(addr + 5));
    for (int k = 5; k < len; k++) code[k] = 0x90;
    patch_bytes((void *)addr, code, len);
    bot_log("patched %s @ %08x", what, addr);
    return 1;
}

// Soldier damage (research): dump the fields, naming players / soldiers / vehicles.

void __cdecl on_msg_dispatch(unsigned *stack)
{
    void *msg = (void *)stack[1];
    const char *name = msg ? message_name(msg) : NULL;
    if (!name) return;
    diagnostic_message(name);
    rush_trace_message(name,msg);
    if (g_traceOrders && !strcmp(name, "VeniceNetwork.Order")) on_order_message(msg);
    if (!strcmp(name, "VeniceNetwork.Spotting") && g_traceMessages) on_spotting_message(msg);
    if (!strcmp(name, "ServerSoldier.Firing")) on_firing_message(msg);
    if (!g_traceMessages) return;
    for (int i = 0; i < g_seenMsgCount; i++) if (g_seenMsg[i] == name) return;
    if (g_seenMsgCount < 512) g_seenMsg[g_seenMsgCount++] = name;
    bot_log("  msg: %s (vt %08x)", name, *(unsigned *)msg);
}

extern void hook_msg_dispatch(void);
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _hook_msg_dispatch\n"
    "_hook_msg_dispatch:\n"
    "  pushad\n"
    "  pushfd\n"
    "  lea eax, [esp + 36]\n"
    "  push eax\n"
    "  call _on_msg_dispatch\n"
    "  add esp, 4\n"
    "  popfd\n"
    "  popad\n"
    "  sub esp, 0x18\n"
    "  cmp byte ptr [0x02aca0a8], 0\n"
    "  push 0x005495ea\n"   /* push/ret leave the flags from cmp intact for the je that follows */
    "  ret\n"
    ".att_syntax prefix\n");

// ---- AltBotMod V16.2.2: the game's stat events (ServerStatEventTriggerEntity::sendEvent(player), thiscall, 1 arg). The
// RuntimeLog line "Stat event StatEvent_CrateArmed triggered for player ..." comes from here: [this+0x14] is the entity
// data, its +0x60 the event (an enum; 0x0233fdb0 turns it into its name). Rush uses the crate events as the real M-COM
// state - armed / disarmed / destroyed and by whom.
#define ADDR_STAT_EVENT_SEND       0x012bc7c0
#define ADDR_STAT_EVENT_NAME       0x0233fdb0
static const unsigned char kStatEventPrologue[6] = {0x81, 0xEC, 0x68, 0x01, 0x00, 0x00};   // sub esp,0x168
static void rush_crate_event(int kind, char *pl);
void __cdecl on_stat_event(char *self, char *arg)
{
    if (!self || IsBadReadPtr(self + 0x14, 4)) return;
    char *data = *(char **)(self + 0x14);
    if (!data || IsBadReadPtr(data + 0x60, 4)) return;
    int ev = *(int *)(data + 0x60);
    if (ev < 0 || ev > 4096) return;
    const char *name = ((const char *(__cdecl *)(int))ADDR_STAT_EVENT_NAME)(ev);
    if (!name || IsBadStringPtrA(name, 64) || strncmp(name, "StatEvent_Crate", 15)) return;
    int kind = !strcmp(name, "StatEvent_CrateArmed") ? 0 : !strcmp(name, "StatEvent_CrateDisarmed") ? 1 : !strcmp(name, "StatEvent_CrateDestroyed") ? 2 : -1;
    if (kind < 0) return;
    char *pl = NULL, *pm = player_manager();   // the player argument: matched against the real players (never trusted as is)
    if (pm && arg) {
        void **arr = *(void ***)(pm + 0xac); unsigned count = *(unsigned *)(pm + 8);
        for (unsigned k = 0; arr && k < count && k < 128; k++) if (arr[k] && (arr[k] == arg || (char *)arr[k] + 8 == arg)) { pl = arr[k]; break; }
    }
    rush_crate_event(kind, pl);
}
extern void hook_stat_event(void);
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _hook_stat_event\n"
    "_hook_stat_event:\n"
    "  pushad\n"
    "  pushfd\n"
    "  push dword ptr [esp + 40]\n"   /* the player argument (esp+4 at entry; +36 for pushad/pushfd) */
    "  push ecx\n"
    "  call _on_stat_event\n"
    "  add esp, 8\n"
    "  popfd\n"
    "  popad\n"
    "  sub esp, 0x168\n"
    "  push 0x012bc7c6\n"
    "  ret\n"
    ".att_syntax prefix\n");

// ---- Human spawn requests: the server calls spawn target vslot 0x6c (player, -1, arg, 0) on whatever the client picked
// (HQ / flag CharacterSpawnEntity, radio beacon, squad mate). Logged so new spawn target classes can be identified.
#define ADDR_SPAWN_REQ_CALL        0x0118fab3  // mov edx,[edx+0x6c]; push 0; push eax; push -1; push esi; call edx
static const unsigned char kSpawnReqCall[11] = {0x8B, 0x52, 0x6C, 0x6A, 0x00, 0x50, 0x6A, 0xFF, 0x56, 0xFF, 0xD2};
void __cdecl on_spawn_request(unsigned *target,char *player,unsigned arg)
{
    if(!target || IsBadReadPtr(target,4) || !player || is_bot(player))return;
    deployment_observe(target,player,arg);
    if(g_enhanced.air.debug){unsigned vt=target[0];bot_log("spawnreq: %s -> target %p vt %08x arg %08x",player+OFF_PLAYER_ONLINE_NAME,target,vt,arg);}
}
extern void hook_spawn_request(void);
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _hook_spawn_request\n"
    "_hook_spawn_request:\n"
    "  pushad\n"
    "  pushfd\n"
    "  push eax\n"
    "  push esi\n"
    "  push ecx\n"
    "  call _on_spawn_request\n"
    "  add esp, 12\n"
    "  popfd\n"
    "  popad\n"
    "  mov edx, dword ptr [edx + 0x6c]\n"
    "  push 0\n"
    "  push eax\n"
    "  push -1\n"
    "  push esi\n"
    "  call edx\n"
    "  push 0x0118fabe\n"
    "  ret\n"
    ".att_syntax prefix\n");

// Soldier constructor: logs the code addresses on the stack (a poor man's call stack) to map spawn paths
// (e.g. radio beacon spawns do not go through the spawn-request call above).
#define ADDR_SOLDIER_CTOR          0x00e104b0
static const unsigned char kSoldierCtorPrologue[6] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};   // push ebp; mov ebp,esp; and esp,-16
void __cdecl on_soldier_ctor(unsigned *stack)
{
    if (!g_traceMessages) return;
    char line[256];
    int n = snprintf(line, sizeof line, "soldier ctor: stack");
    for (int k = 0, found = 0; k < 0x600 / 4 && found < 14 && n < (int)sizeof line - 10; k++)
        if (stack[k] >= 0x00401000 && stack[k] < 0x02028000) { n += snprintf(line + n, sizeof line - n, " %08x", stack[k]); found++; }
    bot_log("%s", line);
}
extern void hook_soldier_ctor(void);
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _hook_soldier_ctor\n"
    "_hook_soldier_ctor:\n"
    "  pushad\n"
    "  pushfd\n"
    "  lea eax, [esp + 36]\n"
    "  push eax\n"
    "  call _on_soldier_ctor\n"
    "  add esp, 4\n"
    "  popfd\n"
    "  popad\n"
    "  push ebp\n"
    "  mov ebp, esp\n"
    "  and esp, 0xfffffff0\n"
    "  push 0x00e104b6\n"
    "  ret\n"
    ".att_syntax prefix\n");

// ---- Unlock ownership: bots have no persistence stats, so the engine refuses every weapon/gadget they select ----
#define ADDR_PLAYER_VTABLE         0x025c778c  // ServerPlayer
#define ADDR_PLAYER_HAS_UNLOCK     0x00e145a0  // ServerPlayer::hasUnlock(asset) - vtable slot 0x18, thiscall, ret 4
int __cdecl core_is_bot_player(void *player) { return is_bot(player); }

extern void hook_has_unlock(void);
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _hook_has_unlock\n"
    "_hook_has_unlock:\n"
    "  push ecx\n"
    "  push ecx\n"
    "  call _core_is_bot_player\n"
    "  add esp, 4\n"
    "  pop ecx\n"
    "  test eax, eax\n"
    "  jnz 1f\n"
    "  push 0x00e145a0\n"
    "  ret\n"
    "1:\n"
    "  mov eax, 1\n"
    "  ret 4\n"
    ".att_syntax prefix\n");

static int g_navDirty;   // learned link removals not yet written to the map's nav file
static int g_autoRepair;  // console: navautorepair 0/1 - steep-slope check where bots get stuck (experimental)
static void on_bot_stuck(int i, char *soldier)
{
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION);
    BotNav *nav = bot_nav(i);
    // (off by default: underground its ground samples hit handrails / low ceilings and cut real stairs - MP_Subway)
    int repaired = g_autoRepair && *nav_slot() && (*nav_slot())->phase == 2 ? nav_repair_around(*nav_slot(), ray_cast, pos[0], pos[1], pos[2], 4.0f, nav->x - pos[0], nav->z - pos[2]) : 0;
    if (repaired) { g_navDirty = 1; bot_log("nav: %d link(s) up a slope too steep to walk removed around (%.1f, %.1f) - saved with the map", repaired, pos[0], pos[2]); }
    // AltBotMod V18: held up by a soldier (a teammate in a stairwell / a doorway), not by the map: no links blocked. Blocking
    // them cut the stairs out of the grid for 3 minutes and a crowd of bots was left in a room on Office (14:38).
    int crowded = 0;
    {
        char *pm = player_manager(); void **arr = pm ? *(void ***)(pm + 0xac) : NULL; unsigned count = pm ? *(unsigned *)(pm + 8) : 0;
        for (unsigned k = 0; arr && k < count && k < 64 && !crowded; k++) {
            char *t = arr[k], *ts = t && t != H->bots[i] ? *(char **)(t + OFF_PLAYER_SOLDIER) : NULL;
            if (ts && *(float *)(ts + OFF_SOLDIER_HEALTH) > 0) { float *tp = (float *)(ts + OFF_SOLDIER_POSITION);
                if (dist2(tp, pos) < 1.8f * 1.8f && fabsf(tp[1] - pos[1]) < 2.0f) crowded = 1; }
        }
    }
    // AltBotMod V18.1: nor at a flight of stairs (on it in the last 15 s, or the next route point a floor up/down): a
    // stairwell is the only way between floors - blocked for 3 minutes, every bot in the building stayed on its floor
    int stairs = 0;
    if (H->frame - g_stairAt[i] < 15 * 30 && g_stairAt[i]) stairs = 1;
    else if (nav->mode == 3) { BotPath *sp = &g_paths[i]; if (sp->next >= 0 && sp->next < sp->count && fabsf(sp->pts[sp->next * 3 + 1] - pos[1]) > 0.8f) stairs = 1; }
    // AltBotMod V21: a spot bots are known to get through (learned) is never blocked; a known jump spot gets its jump
    float heading[2] = {nav->x - pos[0], nav->z - pos[2]};
    int known = !crowded && !stairs ? bm_learn_query(i, BM_ASK_FOOT_KEEP_OPEN, pos, heading) : 0;
    if (known == 2) press_button(i, 12, 4);
    int blocked = *nav_slot() && !crowded && !stairs && !known ? nav_block_towards(*nav_slot(), pos[0], pos[1], pos[2], nav->x, nav->z) : 0;
    if (!crowded && !stairs) bm_learn(i, BM_LEARN_STUCK_FOOT, pos, heading);   // AltBotMod V21: learning
    if (crowded) make_way_ask(i, soldier);
    bot_log("path: %s stuck at (%.1f, %.1f) heading (%.1f, %.1f): backing off, blocked %d link(s)%s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME,
            pos[0], pos[2], nav->x, nav->z, blocked, crowded ? " (a soldier in the way, not the map)" : stairs ? " (at stairs: kept open)" : known == 2 ? " (learned: jumps through here)" : known ? " (learned: bots get through here - kept open)" : "");
    bm_report_stuck(i, pos, "on foot");   // AltBotMod V13: the live map's stuck spots
    zone_stuck(i, pos);   // AltBotMod V34: a mesh connection bots get stuck on costs more
    BotBrain *b = bot_brain(i);
    b->replanAt = 0;                                   // tasks re-plan on the next frame
    if (nav->mode == 3) {
        BotPath *p = &g_paths[i];
        float wx=0,wz=0;
        if(p->next>=0 && p->next<p->count){wx=p->pts[p->next*3]-pos[0];wz=p->pts[p->next*3+2]-pos[2];}
        int ladderNext = p->next>=0 && p->next < p->count && (p->link[p->next] >= 0 || (p->next + 1 < p->count && p->link[p->next + 1] >= 0));
        if (p->next>=0 && p->next < p->count && !ladderNext && !stairs && wx * wx + wz * wz < 2.0f * 2.0f) { p->next++; return; }   // blocked right at a corner: count it as reached
        if (!plan_path(i, soldier, p->goalX, p->goalY, p->goalZ)) nav->mode = 1;                         // otherwise re-plan to the same goal
    }
}

// ---- Level changes ----
// Level name lives at [[ctx+0x10]+0x6c] ("Levels/MP_003/MP_003"). On a change everything tied to the old level is
// dropped, and once the new level's capture points exist, HQ spawns / nav grid are rediscovered automatically.
static char g_level[64];
static int g_levelReady;           // flags + HQ spawns found for g_level
static unsigned g_levelReadyAt;    // AltBotMod V23: the frame it became ready (the round-start vehicle rush)
static unsigned g_levelCheckAt;
static int g_announceLevel;           // a real level change happened (not just a reload): tell the players when ready

static const char *current_level(void)
{
    char *ctx = *(char **)ADDR_SERVER_GAME_CONTEXT;
    char *lvl = ctx ? *(char **)(ctx + 0x10) : NULL;
    const char *name = lvl && !IsBadReadPtr(lvl + 0x6c, 4) ? *(const char **)(lvl + 0x6c) : NULL;
    if (!name || IsBadStringPtrA(name, 64)) return NULL;
    const char *s = strrchr(name, '/');
    return s ? s + 1 : name;
}

// HQ spawn per team: HQ capture points (not takeable) carry the owning team and share their owner object (+0x10)
// with that team's HQ CharacterSpawnEntity.
// Spawn points tied to no capture point: some are HQs the capture points do not show (Wake's US island HQ), others
// belong to other game modes. Bots try each once (cmd_spawn) and keep the one that serves their team best.
#define MAX_HQ_CAND 16
static void *g_hqCand[MAX_HQ_CAND];
static int g_hqCandCount;
static unsigned char g_hqCandTried[3][MAX_HQ_CAND];
static float g_hqCpPos[3][3];
static int g_hqCpKnown[3];
#include "hq_spawns.inc"
static int find_hq_spawns(float *bmin, float *bmax)
{
    char *cps[MAX_FLAGS*2],*spawns[1024];
    int ncp=scan_objects(ADDR_CAPTURE_POINT_VTABLE,cps,MAX_FLAGS*2);
    int nsp=scan_objects(0x025bbbc0,spawns,1024),found=0;
    memset(g_hqOptionCount,0,sizeof g_hqOptionCount);
    memset(g_hqOptionCursor,0,sizeof g_hqOptionCursor);
    memset(g_hqFailedAt,0,sizeof g_hqFailedAt);
    memset(g_hqCandTried,0,sizeof g_hqCandTried);g_hqCandCount=0;
    g_hqCpKnown[1]=g_hqCpKnown[2]=0;
    float bestFront[3]={INFINITY,INFINITY,INFINITY};
    for(int c=0;c<ncp;c++){
        char *cp=cps[c],*data=*(char **)(cp+OFF_CP_DATA);
        float *pos=(float *)(cp+OFF_CP_POSITION);
        if(!isfinite(pos[0]) || !isfinite(pos[1]) || !isfinite(pos[2]) || (pos[0]==0 && pos[2]==0))continue;
        for(int a=0;a<3;a+=2){bmin[a]=fminf(bmin[a],pos[a]);bmax[a]=fmaxf(bmax[a],pos[a]);}
        if(!data || IsBadReadPtr(data+OFF_CPDATA_TAKEABLE,1) || data[OFF_CPDATA_TAKEABLE])continue;
        int team=*(int *)(cp+OFF_CP_OWNER_TEAM);
        if(team!=1 && team!=2)continue;
        float front=1e20f;
        for(int k=0;k<ncp;k++){
            char *d=*(char **)(cps[k]+OFF_CP_DATA);
            if(d && !IsBadReadPtr(d+OFF_CPDATA_TAKEABLE,1) && d[OFF_CPDATA_TAKEABLE])
                front=fminf(front,dist2(pos,(float *)(cps[k]+OFF_CP_POSITION)));
        }
        // Record the HQ even if its child CSE uses another owner. This anchor
        // is also needed for team-side vehicle filtering and grid coverage.
        if(!g_hqCpKnown[team] || front<bestFront[team]){
            memcpy(g_hqPos[team],pos,12);memcpy(g_hqCpPos[team],pos,12);
            g_hqCpKnown[team]=1;bestFront[team]=front;
        }
    }
    for(int c=0;c<ncp;c++){
        char *cp=cps[c],*data=*(char **)(cp+OFF_CP_DATA);
        float *pos=(float *)(cp+OFF_CP_POSITION);
        if(!data || IsBadReadPtr(data+OFF_CPDATA_TAKEABLE,1) || data[OFF_CPDATA_TAKEABLE])continue;
        int team=*(int *)(cp+OFF_CP_OWNER_TEAM);
        if(team<1 || team>2 || !g_hqCpKnown[team])continue;
        float front=bestFront[team];
        void *owner=*(void **)(cp+OFF_CP_OWNER_OBJECT);
        for(int k=0;k<nsp;k++){
            char *spawn=spawns[k];if(!cse_valid(spawn))continue;
            int st=spawn_team(spawn);if(st>2)continue;
            char *sd=*(char **)(spawn+0x14);float *sp=(float *)(sd+0x40);
            if(!isfinite(sp[0]) || !isfinite(sp[1]) || !isfinite(sp[2]))continue;
            void *so=*(void **)(spawn+OFF_CP_OWNER_OBJECT);
            int linked=owner && so==owner;
            if(linked){
                // A non-capturable CP is the authoritative HQ owner. The
                // child's data team byte can be a generic deployment value;
                // Test9/10 rejected every RU child on such maps. Only shared
                // owners need a matching child hint / unambiguous position.
                int shared=0;
                for(int j=0;j<ncp;j++)if(owner==*(void **)(cps[j]+OFF_CP_OWNER_OBJECT)){
                    char *cd=*(char **)(cps[j]+OFF_CP_DATA);
                    int ct=*(int *)(cps[j]+OFF_CP_OWNER_TEAM);
                    if(cd && !IsBadReadPtr(cd+OFF_CPDATA_TAKEABLE,1) && !cd[OFF_CPDATA_TAKEABLE] && ct==3-team)shared=1;
                }
                if(shared && st!=team){
                    if(st || !g_hqCpKnown[3-team] || (fabsf(sp[0])<1 && fabsf(sp[2])<1) ||
                       dist2(sp,pos)>=dist2(sp,g_hqCpPos[3-team]))continue;
                }
            }
            if(!linked){
                // CQ deployment children may be neutral and have a different
                // owner from the HQ CP. Native side/position validates these;
                // a captured-flag owner never qualifies as deployment.
                float ownDistance=dist2(sp,pos);
                if((st!=0 && st!=team) || ownDistance>(st==team?900*900:220*220) ||
                   fabsf(sp[1]-pos[1])>60 || (fabsf(sp[0])<1 && fabsf(sp[2])<1))continue;
                if(g_hqCpKnown[3-team] && ownDistance>=dist2(sp,g_hqCpPos[3-team]))continue;
                int captured=0;
                for(int j=0;j<ncp;j++)if(so && so==*(void **)(cps[j]+OFF_CP_OWNER_OBJECT)){
                    char *cd=*(char **)(cps[j]+OFF_CP_DATA);
                    if(cd && !IsBadReadPtr(cd+OFF_CPDATA_TAKEABLE,1) && cd[OFF_CPDATA_TAKEABLE])captured=1;
                }
                if(captured)continue;
            }
            hq_add_option(team,spawn,sqrtf(front)+sqrtf(dist2(sp,pos))+(linked?0:10000)+(st==team?0:st?2000:1000));
        }
    }
    // AltBotMod V22: a team with an HQ but no deployment matched to it (Office Domination: 5 CSEs, in-memory positions all
    // near (0, 20, 0) - the matching gave team 1 the team-less one and team 2 nothing; 1,201 "no HQ" waits, RU never played)
    // tries the level's team-less / own-team entities, the other team's choice last - the game picks the team's points.
    for(int team=1;team<=2;team++){
        if(!g_hqCpKnown[team] || g_hqOptionCount[team] || strstr(g_modeName,"Conquest"))continue;   // (Conquest: never a stand-in - flags only)
        int added=0;
        for(int k=0;k<nsp;k++){
            char *spawn=spawns[k];if(!cse_valid(spawn))continue;
            int st=spawn_team(spawn);if(st!=0 && st!=team)continue;
            hq_add_option(team,spawn,50000.0f+(hq_is_option(3-team,spawn)?5000.0f:0.0f)+(st==team?0.0f:100.0f)+k);added++;
        }
        if(added)bot_log("spawn: team %d has an HQ but no deployment beside it - tries the level's %d team-less / own entities",team,added);
    }
    for(int team=1;team<=2;team++){
        if(hq_select_option(team))found|=team;
        else if(cse_valid(H->teamSpawn[team].entity))found|=team;
        bot_log("spawn: team %d HQ anchor=%d native options=%d selected=%p (scanned %d CSEs)",team,g_hqCpKnown[team],g_hqOptionCount[team],H->teamSpawn[team].entity,nsp);
        for(int k=0;k<g_hqOptionCount[team];k++){
            char *e=g_hqOptions[team][k],*sd=*(char **)(e+0x14);float *p=(float *)(sd+0x40);
            bot_log("spawn: HQ option team=%d entity=%p native-team=%d at=(%.1f %.1f %.1f) score=%.0f",team,e,spawn_team(e),p[0],p[1],p[2],g_hqScores[team][k]);
        }
    }
    return found;
}

// Level discovery (loadout asset lookup, flags, HQ spawns, soldier blueprint) scans process memory and can take
// seconds - long enough for the server's stall watchdog to kill it during a level load. It runs on a worker thread;
// the main thread only starts it and applies the results. Bots are paused (level not ready) meanwhile.
static volatile LONG g_jobState;     // 0 idle, 1 running, 2 done
static int g_jobDoLoadouts;
static int g_needLoadouts;   // a level change: look the kit assets up once the new level has loaded (not while the old one
                             // is still in memory - its copies are about to be freed; a bot handed one crashed the server)
static char g_jobLevel[64],g_jobMode[64];
static LONG g_jobEpoch;
static int g_jobHq;
static volatile int g_jobGuessCq;   // AltBotMod V14: no game mode reported, but the level has takeable flags
static enum BMMode g_jobKind;        // AltBotMod V15: the mode as played (g_mode) when the job started - after a fallback it is not bm_mode(g_jobMode)
static void mode_fallback(const char *why)
{
    if (g_modeFallbackEpoch == g_roundEpoch) return;
    g_modeFallbackEpoch = g_roundEpoch; g_mode = BM_MODE_TDM; g_flagCount = 0; g_levelCheckAt = 0;
    bot_log("mode: %s on %s - %s; the bots play it as team play (the TDM hunt) this round", g_modeName, g_level, why);
    if (H->botCount) chat_say("[AltBotMod] %s: %s - bots play team deathmatch-style this round", g_modeName, why);
}
static float g_jobMin[3], g_jobMax[3];
static void *g_jobBlueprint;
static HANDLE g_jobThread;

// ---- Ladders: every ServerLadderComponent sits in the component list of the object it belongs to (a ladder model,
// a ServerStaticModelEntity). The object's world transform (+0x160: right, up, forward, position) and model bounds
// (+0x70 min, +0x80 max, local) give the ladder's foot and top. ----
#define ADDR_LADDER_COMPONENT_VTABLE   0x025c0228
#define ADDR_STATIC_MODEL_ENTITY_VTABLE 0x025b6d50
#define OFF_STATIC_MODEL_TRANSFORM     0x160
#define OFF_STATIC_MODEL_BOUNDS        0x70
#define MAX_LADDERS 256
typedef struct Ladder { float foot[3], top[3], fwd[3], height; } Ladder;
static Ladder g_ladders[MAX_LADDERS];
static int g_ladderCount;
static void find_ladders(void)
{
    char *comps[MAX_LADDERS];
    int n = scan_objects(ADDR_LADDER_COMPONENT_VTABLE, comps, MAX_LADDERS), kept = 0, odd = 0;
    for (int k = 0; k < n; k++) {
        char *list = *(char **)(comps[k] + 8), *ent;
        if (!list || IsBadReadPtr(list, 4) || !(ent = *(char **)list) || IsBadReadPtr(ent, 0x1a0)) { odd++; continue; }
        if (*(unsigned *)ent != ADDR_STATIC_MODEL_ENTITY_VTABLE) {
            if (odd++ < 4) bot_log("ladders: component %p belongs to an object of vtable %08x - skipped", comps[k], *(unsigned *)ent);
            continue;
        }
        float *m = (float *)(ent + OFF_STATIC_MODEL_TRANSFORM), *bb = (float *)(ent + OFF_STATIC_MODEL_BOUNDS);
        float h = bb[5] - bb[1], w = bb[4] - bb[0], d = bb[6] - bb[2];
        if (!(h > 1.0f && h < 80.0f) || w > 3.0f || d > 3.0f) {   // not a plain ladder model
            if (odd++ < 4) bot_log("ladders: object %p bounds %.1f x %.1f x %.1f - skipped", ent, w, h, d);
            continue;
        }
        Ladder *L = &g_ladders[kept++];
        for (int a = 0; a < 3; a++) {
            float cx = (bb[0] + bb[4]) * 0.5f, cz = (bb[2] + bb[6]) * 0.5f;
            L->foot[a] = m[12 + a] + m[a] * cx + m[4 + a] * bb[1] + m[8 + a] * cz;
            L->top[a] = m[12 + a] + m[a] * cx + m[4 + a] * bb[5] + m[8 + a] * cz;
            L->fwd[a] = m[8 + a];
        }
        L->height = h;
        float best = 1e30f;
        int bf = -1;
        for (int f = 0; f < g_flagCount; f++) { float dd = dist2(flag_pos(f), L->foot); if (dd < best) { best = dd; bf = f; } }
        bot_log("ladders: #%d foot (%.1f, %.1f, %.1f) %.1f m tall, facing (%.2f, %.2f); nearest flag %d at %.0f m", kept - 1,
                L->foot[0], L->foot[1], L->foot[2], h, L->fwd[0], L->fwd[2], bf, bf >= 0 ? sqrtf(best) : -1.0f);
        if (kept == MAX_LADDERS) break;
    }
    g_ladderCount = kept;
    bot_log("ladders: %d ladder(s) on this level (%d component(s) found)", kept, n);
}

static const float *ladder_foot(int k) { return g_ladders[k].foot; }
static float ladder_top_y(int k) { return g_ladders[k].top[1]; }

// Ground straight below (x, fromY, z) down to toY: 1 with its height.
static int ground_below(float x, float z, float fromY, float toY, float *y)
{
    float a[3] = {x, fromY, z}, b[3] = {x, toY, z}, h[3];
    unsigned saved = g_rayFlags;
    g_rayFlags = RAY_DONT_CHECK_CHARACTER;
    int r = ray_cast(a, b, h, NULL);
    g_rayFlags = saved;
    if (r) *y = h[1];
    return r;
}
// Links every ladder into the nav grid: foot on the side with ground at the ladder's base, exit on the far side at
// the top (where the soldier steps off).
static void ladders_link(void)
{
    NavGrid *g = *nav_slot();
    nav_clear_links();
    if (!g || g->phase != 2) return;
    int made = 0;
    for (int k = 0; k < g_ladderCount; k++) {
        Ladder *L = &g_ladders[k];
        float fl = sqrtf(L->fwd[0] * L->fwd[0] + L->fwd[2] * L->fwd[2]);
        if (fl < 0.5f) continue;
        float f[3] = {L->fwd[0] / fl, 0, L->fwd[2] / fl};
        int done = 0;
        for (int s = 1; s >= -1 && !done; s -= 2) {
            float n[3] = {f[0] * s, 0, f[2] * s}, gy, ty;
            float ax = L->foot[0] + n[0] * LADDER_STAND_OUT, az = L->foot[2] + n[2] * LADDER_STAND_OUT;
            float bx = L->foot[0] - n[0] * 1.6f, bz = L->foot[2] - n[2] * 1.6f;   // over the top, onto the roof / deck / quay
            if (!ground_below(bx, bz, L->top[1] + 1.5f, L->top[1] - 2.0f, &ty) || ty < L->top[1] - 1.5f) continue;
            float b[3] = {bx, ty, bz}, snapB[3];
            if (!nav_snap(g, b[0], b[1] + 0.3f, b[2], snapB) || fabsf(snapB[1] - ty) > 1.0f) continue;
            // the floor (or water) in front of it, wherever the ladder passes through it - as long as it is below the
            // top exit (ladders often reach down past a deck, or out of the water up to a barge)
            if (!ground_below(ax, az, L->top[1] + 0.3f, L->foot[1] - 1.0f, &gy) || gy > ty - 0.5f) continue;
            float a[3] = {ax, gy, az}, snapA[3];
            if (!nav_snap(g, a[0], a[1] + 0.3f, a[2], snapA) || fabsf(snapA[1] - gy) > 1.0f) continue;
            int id = nav_add_link(g, a, b, 6.0f + (ty - gy) * 3.0f);
            if (id < 0 || id >= 256) continue;
            g_ladderLink[id].ladder = k;
            g_ladderLink[id].groundY = gy;
            memcpy(g_ladderLink[id].n, n, 12);
            bot_log("ladders: #%d linked - foot (%.1f, %.1f, %.1f) -> top (%.1f, %.1f, %.1f)", k, a[0], a[1], a[2], b[0], b[1], b[2]);
            made++;
            done = 1;
        }
        if (!done) bot_log("ladders: #%d at (%.1f, %.1f) not linked (no walkable ground at its foot or top)", k, L->foot[0], L->foot[2]);
    }
    bot_log("ladders: %d of %d ladder(s) linked into the nav grid", made, g_ladderCount);
}

static void tickets_find(void);
static void tickets_reset(void);
// AltBotMod V16, Air Superiority: the level data (levels/xp5_00x/air_superiority) has no soldier spawn - each side's
// spawn points are 12 "dynamicspawn_xp_f18 / su35_spawninair_as" prefabs, a character spawn (Team 1 / 2) bound to a jet
// spawned in the air. Fun-Bots spawns AS bots on exactly those (a team spawn with a vehicle spawn); air_consider then
// flies the jet the bot finds itself in.
static char *g_asSpawns[64]; static int g_asSpawnCount; static unsigned g_asFailedAt[64], g_asNext[3];
static int as_find_spawns(void)
{
    char *all[256]; int n = scan_objects(0x025bbbc0, all, 256), teams = 0; g_asSpawnCount = 0;
    for (int k = 0; k < n && g_asSpawnCount < 64; k++) {
        int t = cse_valid(all[k]) ? spawn_team(all[k]) : 0;
        if (t == 1 || t == 2) { g_asSpawns[g_asSpawnCount++] = all[k]; teams |= t; }
    }
    memset(g_asFailedAt, 0, sizeof g_asFailedAt);
    bot_log("air superiority: %d jet spawn point(s) (teams %x)", g_asSpawnCount, teams);
    return teams;
}
static int as_spawn(char *bot, int team)
{
    if (_stricmp(g_modeName, "AirSuperiority0") || !g_asSpawnCount || team < 1 || team > 2) return 0;
    for (int tries = 0; tries < g_asSpawnCount; tries++) {
        int k = (int)(g_asNext[team]++ % (unsigned)g_asSpawnCount); char *e = g_asSpawns[k];
        if (!cse_valid(e) || spawn_team(e) != team || H->frame < g_asFailedAt[k]) continue;
        apply_kit(bot);
        void *s = ((CseSpawn_t)ADDR_CSE_SPAWN)(e, bot, 0xffffffff, 1, 0);
        if (!s) s = *(void **)(bot + OFF_PLAYER_SOLDIER);
        if (!s) { g_asFailedAt[k] = H->frame + 10 * 30; continue; }
        int i = bot_index(bot); if (i >= 0) { g_lastSpawnedBot[i] = bot; g_lastSpawnedAt[i] = H->frame; }
        bot_log("spawn: '%s' team=%d into an Air Superiority jet spawn %p%s", bot + OFF_PLAYER_ONLINE_NAME, team, (void *)e, player_vehicle(bot) ? " (seated)" : "");
        return 1;
    }
    return 0;
}
static DWORD WINAPI discovery_worker(LPVOID doLoadouts)
{
    LONG epoch=g_jobEpoch;
    if(epoch!=g_roundEpoch){InterlockedExchange(&g_jobState,2);return 0;}
    if (doLoadouts) load_loadouts();   // passed in: the main thread clears g_jobDoLoadouts as soon as the thread starts
    g_jobMin[0] = g_jobMin[2] = 1e9f; g_jobMax[0] = g_jobMax[2] = -1e9f;
    if(epoch!=g_roundEpoch){InterlockedExchange(&g_jobState,2);return 0;}
    enum BMMode kind=g_jobKind;   // V15 (was bm_mode(g_jobMode): a round fallen back to team play would rediscover flags)
    if(kind==BM_MODE_CQ){g_modeProfile=mode_profile(g_jobLevel,g_jobMode);g_jobHq = find_hq_spawns(g_jobMin,g_jobMax);find_flags();
        if(!_stricmp(g_jobMode,"AirSuperiority0"))g_jobHq|=as_find_spawns();}   // V16: Air Superiority has no HQ spawn - its spawn points are the jets
    else if(kind==BM_MODE_UNKNOWN){   // AltBotMod V14: RCON has not said the mode: are there flags (Conquest)?
        g_modeProfile=NULL;g_jobHq=find_hq_spawns(g_jobMin,g_jobMax);find_flags();g_jobGuessCq=g_flagCount>0;
    }
    else {g_flagCount=0;g_jobGuessCq=0;g_jobHq=mode_discover(g_jobLevel,g_jobMode,g_jobMin,g_jobMax);}
    if(epoch!=g_roundEpoch) {InterlockedExchange(&g_jobState,2);return 0;}
    find_ladders();
    g_jobBlueprint = NULL;
    char *objs[16];
    int nb = scan_objects(0x02500e38, objs, 16);   // SoldierBlueprint instances
    for (int k = 0; k < nb && !g_jobBlueprint; k++) if (!strcmp(asset_name(objs[k]), "Characters/Soldiers/MpSoldier")) g_jobBlueprint = objs[k];
    if(epoch==g_roundEpoch && (kind==BM_MODE_CQ || bm_mode(g_jobMode)==BM_MODE_RUSH)){tickets_reset();tickets_find();}   // V6: Rush too (V15: also a Rush fallen back to team play - its tickets still end the round)
    InterlockedExchange(&g_jobState, 2);
    return 0;
}

static void tickets_reset(void);
static void tickets_find(void);
static void hq_memory_apply(void);
static int near_origin(const float *p) { return fabsf(p[0]) < 12.0f && fabsf(p[2]) < 12.0f; }
static void hq_memory_save(int team, const float *pos, unsigned a2, unsigned a3, unsigned a4);
static float *spawn_data_pos(void *spawn);
static int cse_valid(void *e);
static unsigned g_swimSeconds, g_swimSecondsLevel;
static LONG g_navVehicleCoverageEpoch=-1;
static void navigation_bounds(float *low,float *high)
{
    low[0]=g_jobMin[0]-200;low[2]=g_jobMin[2]-200;
    high[0]=g_jobMax[0]+200;high[2]=g_jobMax[2]+200;
    if(g_modeProfile)for(int k=0;k<3;k+=2){
        low[k]=fminf(low[k],g_modeProfile->low[k]-60);
        high[k]=fmaxf(high[k],g_modeProfile->high[k]+60);
    }
    if(!mode_uses_vehicles())return;
    for(int team=1;team<=2;team++){
        float *p=cse_valid(H->teamSpawn[team].entity)?spawn_data_pos(H->teamSpawn[team].entity):NULL;
        if(p && isfinite(p[0]) && isfinite(p[2]) && !near_origin(p))
            for(int k=0;k<3;k+=2){low[k]=fminf(low[k],p[k]-100);high[k]=fmaxf(high[k],p[k]+100);}
    }
    char **vehicles;int count=vehicle_list(&vehicles);
    for(int k=0;k<count;k++)if(vehicle_ok(vehicles[k]) && !vehicle_occupied(vehicles[k])){
        float *p=vehicle_pos(vehicles[k],NULL);
        if(!p || !isfinite(p[0]) || !isfinite(p[2]) ||
           p[0]<g_jobMin[0]-3000 || p[0]>g_jobMax[0]+3000 || p[2]<g_jobMin[2]-3000 || p[2]>g_jobMax[2]+3000)continue;
        low[0]=fminf(low[0],p[0]-60);low[2]=fminf(low[2],p[2]-60);
        high[0]=fmaxf(high[0],p[0]+60);high[2]=fmaxf(high[2],p[2]+60);
    }
}
static NavGrid *navigation_extend(NavGrid *grid,const float *low,const float *high)
{
    if(!grid || grid->phase!=2 ||
       (nav_covers(grid,low[0],low[2],0) && nav_covers(grid,high[0],high[2],0)))return grid;
    if(g_navExpansion)return grid;
    NavGrid *expanded=nav_expand_bounds(grid,low[0],low[2],high[0],high[2]);
    if(!expanded){bot_log("navigation: deployment coverage extension failed (size/allocation limit); use ehdiag");return grid;}
    bot_log("navigation: %s excludes deployment/vehicles; extending %.0f..%.0f / %.0f..%.0f; surveyed cells preserved",g_level,low[0],high[0],low[2],high[2]);
    g_navExpansion=expanded;
    chat_say("[AltBotMod] mapping extra deployment routes; existing navigation remains active");
    return grid;
}
static void navigation_vehicle_coverage_tick(void)
{
    NavGrid *grid=*nav_slot();char **vehicles;
    if(!mode_uses_vehicles() || !level_ready() || !grid || grid->phase!=2 ||
       g_navVehicleCoverageEpoch==g_roundEpoch || !vehicle_list(&vehicles))return;
    // The initial level discovery often precedes the first published vehicle
    // scan. Check that first inventory too, before any boarding route is queued.
    float low[3]={0},high[3]={0};navigation_bounds(low,high);
    *nav_slot()=navigation_extend(grid,low,high);*nav_building()=(*nav_slot())->phase!=2;
    g_navVehicleCoverageEpoch=g_roundEpoch;
}
static void level_tick(void)
{
    if(g_roundCleanup)return;
    const char *lvl = current_level();   // name checked every frame so no bot spawns on the previous level's HQ
    if (!lvl) { g_levelReady = 0; return; }
    // a new level object with the same name = the same map loaded again (round restart / !map to the current map):
    // everything found on the old instance (spawn points, flags, assets) is gone just the same
    // Signals: the level object changed, or RCON reported a level load. After a round is over bots wait (no spawns on
    // the old instance) until the level has loaded again - or 60 s, whichever comes first.
    char *ctx = *(char **)ADDR_SERVER_GAME_CONTEXT;
    void *lvlObj = ctx ? *(void **)(ctx + 0x10) : NULL;
    static void *lastLvlObj;
    static long lastLoads = -1, lastRounds = -1;
    static unsigned roundOverAt, changedAt;
    long loads, rounds;
    chat_level_events(&loads, &rounds);
    if (lastLoads < 0) { lastLoads = loads; lastRounds = rounds; }
    if (rounds != lastRounds) {
        lastRounds = rounds;
        roundOverAt = H->frame ? H->frame : 1;
        g_levelReady = 0;
        bot_log("level: round over - bots wait for the next level");
    }
    int reloaded = !strcmp(lvl, g_level) && ((lastLvlObj && lvlObj != lastLvlObj) || loads != lastLoads ||
                                            (roundOverAt && H->frame - roundOverAt > 180 * 30));
    if (reloaded && changedAt && H->frame - changedAt < 45 * 30) reloaded = 0;   // (the change's own load event)
    if (loads != lastLoads || reloaded) roundOverAt = 0;
    lastLvlObj = lvlObj;
    lastLoads = loads;
    if (roundOverAt && !strcmp(lvl, g_level)) { g_levelReady = 0; return; }   // waiting for the reload
    if (strcmp(lvl, g_level) || reloaded) {
        bot_log("level: %s -> %s%s, resetting bots", g_level[0] ? g_level : "(none)", lvl, reloaded ? " (same map loaded again)" : "");
        if (g_level[0]) bot_log("swim: %u bot-second(s) in the water on %s", g_swimSecondsLevel, g_level);
        g_swimSecondsLevel = 0;
        g_vringDumps = 0;   // AltBotMod V30.1: the driver traces are written again on the new level
        changedAt = H->frame ? H->frame : 1;
        tickets_reset();
        enhanced_level_reset();
        strncpy(g_level, lvl, sizeof g_level - 1);
        g_levelReady = 0;
        g_flagCount = 0;
        memset(H->teamSpawn, 0, sizeof H->teamSpawn);
        for (int i = 0; i < H->botCount; i++) {
            BotBrain *b = bot_brain(i);
            int en = b->enabled;
            memset(b, 0, sizeof *b);
            b->enabled = en; b->targetFlag = -1; b->avoidFlag = -1;
            bot_nav(i)->mode = 0;
            H->botMove[i][0] = H->botMove[i][1] = H->botMove[i][2] = 0;
            g_orderCompSoldier[i] = NULL;
        }
        g_needLoadouts = 1;    // re-verify asset pointers against this level (in the worker, once it has loaded)
        load_kitskip();
        g_pendingHq.at = 0;    // a deploy seen before the reset belongs to the old instance
        memset(g_hqProbes, 0, sizeof g_hqProbes);
        g_announceLevel = 1;
        memset(g_squadTarget, 0, sizeof g_squadTarget);   // flag indices belong to the old level
        memset(g_squadDefend, 0, sizeof g_squadDefend);
        memset(g_allocAt, 0, sizeof g_allocAt);
        memset(g_allocSig, 0, sizeof g_allocSig);
    }
    if (g_levelReady) return;
    if (H->frame < g_levelCheckAt) return;
    g_levelCheckAt = H->frame + 30;
    if (g_jobState == 1) return;                     // discovery running in the background
    if (g_jobState == 0) {
        // AltBotMod V15: a Rush / Squad Rush with no recorded M-COMs for this level cannot be played for its objectives
        // (V9-V14: "Rush needs the recorded M-COM positions" and the bots waited all round) - team play instead.
        if (g_mode == BM_MODE_RUSH && !mode_profile(g_level, g_modeName)) mode_fallback("no recorded M-COM positions for this level");
        snprintf(g_jobLevel,sizeof g_jobLevel,"%s",g_level);
        snprintf(g_jobMode,sizeof g_jobMode,"%s",g_modeName);g_jobEpoch=g_roundEpoch;g_jobKind=g_mode;
        InterlockedExchange(&g_jobState, 1);
        if (g_jobThread) CloseHandle(g_jobThread);
        g_jobThread = CreateThread(NULL, 0, discovery_worker, (LPVOID)(INT_PTR)g_jobDoLoadouts, 0, NULL);
        g_jobDoLoadouts = 0;
        return;
    }
    // job done: apply if it is for this level and complete, otherwise try again shortly (level still loading)
    InterlockedExchange(&g_jobState, 0);
    if (strcmp(g_jobLevel,g_level) || strcmp(g_jobMode,g_modeName) || g_jobEpoch!=g_roundEpoch || g_jobKind!=g_mode) {g_flagCount=0;memset(H->teamSpawn,0,sizeof H->teamSpawn);g_needLoadouts=1;tickets_reset();return;}
    if (g_mode == BM_MODE_UNKNOWN) {   // AltBotMod V14: the server never said its mode (RCON) - V13 waited for ever
        static unsigned unknownSince; static LONG unknownEpoch = -1;
        if (unknownEpoch != g_roundEpoch) { unknownEpoch = g_roundEpoch; unknownSince = H->frame ? H->frame : 1; }
        if (g_jobGuessCq && H->frame - unknownSince > 8 * 30) {
            g_mode = BM_MODE_CQ; snprintf(g_modeName, sizeof g_modeName, "ConquestLarge0");
            bot_log("level: the server has not reported its game mode over RCON - %s has %d flags, so the bots play Conquest", g_level, g_flagCount);
            g_levelCheckAt = 0;
            return;
        }
    }
    if ((mode_is_cq() ? !g_flagCount : !mode_has_spawns()) || !g_jobHq || !g_jobBlueprint || *(unsigned *)g_jobBlueprint != 0x02500e38) {
        // AltBotMod V15: Capture the Flag has flags to carry, not to capture: when its level shows no capture point after
        // 45 s (Air Superiority: 90 s) it is played as team play instead of never starting. Not the Conquest family: their
        // levels always have flags, and while a level is still loading none may be found yet.
        static unsigned noFlagsSince; static LONG noFlagsEpoch = -1;
        int noFlagsWait = !_stricmp(g_modeName, "CaptureTheFlag0") || !_strnicmp(g_modeName, "BFLAG", 5) ? 45 : !_stricmp(g_modeName, "AirSuperiority0") ? 90 : 0;
        if (noFlagsWait && mode_is_cq() && !g_flagCount && g_jobBlueprint && *(unsigned *)g_jobBlueprint == 0x02500e38) {
            if (noFlagsEpoch != g_roundEpoch) { noFlagsEpoch = g_roundEpoch; noFlagsSince = H->frame ? H->frame : 1; }
            else if (H->frame - noFlagsSince > (unsigned)noFlagsWait * 30) { mode_fallback("the level has no capture points"); return; }
        }
        static unsigned whyAt;
        if (!whyAt || H->frame - whyAt > 20 * 30) {   // V14: why, every 20 s (V13 said only "not ready yet")
            whyAt = H->frame ? H->frame : 1;
            bot_log("level: %s not ready yet (%d flags, hq mask %d, blueprint %p) - mode '%s'%s%s%s", g_level, g_flagCount, g_jobHq, g_jobBlueprint,
                    g_modeName, g_mode == BM_MODE_UNKNOWN ? ": the server has not said its game mode (check the chat: lines - RCON port / password)" : "",
                    g_mode != BM_MODE_UNKNOWN && !mode_is_cq() && !g_modeProfile ? ": no layout for this mode here" : "",
                    g_jobBlueprint && *(unsigned *)g_jobBlueprint != 0x02500e38 ? ": the soldier blueprint is not loaded yet" : "");
        }
        g_levelCheckAt = H->frame + 60;
        return;
    }
    if (g_needLoadouts) {   // loaded: now the kit assets (one more discovery pass, with the lookup)
        g_needLoadouts = 0;
        g_jobDoLoadouts = 1;
        bot_log("level: %s has loaded - looking up the bot kits' assets", g_level);
        return;
    }
    static char retriedFor[64];
    if (!loadout_lib()->count && strcmp(retriedFor, g_level)) {   // library didn't load: rerun discovery once with loading on
        strncpy(retriedFor, g_level, sizeof retriedFor - 1);
        bot_log("level: loadout library empty, loading it");
        g_jobDoLoadouts = 1; g_jobState = 0;
        return;
    }
    H->teamBlueprint[1] = H->teamBlueprint[2] = g_jobBlueprint;
    g_levelReady = 1; g_levelReadyAt = H->frame ? H->frame : 1;
    for (int t = 1; t <= 2; t++)
        if (!(g_jobHq & t)) bot_log("level: team %d has %s; available owned flags remain usable", t,g_hqCpKnown[t]?"a native HQ but its spawn child was not resolved":"no native HQ anchor");
    if (H->botCount && g_announceLevel) chat_say("[AltBotMod] bots ready on %s (%d flags)", g_level, g_flagCount);
    g_announceLevel = 0;
    bot_log("level: %s ready - %d flags, HQ spawns US %p RU %p, blueprint %p", g_level, g_flagCount, H->teamSpawn[1].entity,
            H->teamSpawn[2].entity, g_jobBlueprint);
    /* Ticket counters were discovered off the game thread. */
    fleet_remember_bases();
    if(mode_is_cq())hq_memory_apply();
    if(mode_is_cq())teams_fix_all();
    NavGrid *g = *nav_slot();
    char navKey[32];snprintf(navKey,sizeof navKey,g_modeName[0]?"%s_%s":"%s",g_level,g_modeName);
    if (!g || strcmp(g->level,navKey)) {
        char path[MAX_PATH + 64];
        nav_file(path,sizeof path,navKey);
        nav_save_if_dirty();
        NavGrid *loaded = nav_load(path);
        if(!loaded && mode_is_cq() && bm_legacy_cache_allowed(g_level,g_modeName)){nav_file(path,sizeof path,g_level);loaded=nav_load(path);}
        // AltBotMod V16: a mode played here for the first time (Rush / TDM / Squad Rush on a Conquest map) borrows the
        // level's grid from another mode - the same streets and stairs - and only surveys what that grid lacks. V15 built
        // a fresh grid (2-5 min) and the bots walked straight lines meanwhile: "in Rush the bots weren't moving".
        if(!loaded){
            static const char *borrow[]={"ConquestLarge0","ConquestAssaultLarge0","ConquestLarge1","ConquestSmall0","ConquestAssaultSmall0",
                "RushLarge0","SquadRush0","TeamDeathMatch0","SquadDeathMatch0","TeamDeathMatchC0","Domination0","GunMaster0",""};
            char key2[64];
            for(int k=0;k<(int)(sizeof borrow/sizeof *borrow) && !loaded;k++){
                if(borrow[k][0])snprintf(key2,sizeof key2,"%s_%s",g_level,borrow[k]);else snprintf(key2,sizeof key2,"%s",g_level);
                if(!strcmp(key2,navKey))continue;
                nav_file(path,sizeof path,key2);loaded=nav_load(path);
                if(loaded)bot_log("level: no %s grid yet - borrows the %s grid of the same level (missing area surveyed in the background)",navKey,key2);
            }
        }
        float low[3]={0},high[3]={0};navigation_bounds(low,high);
        loaded=navigation_extend(loaded,low,high);
        if(loaded)snprintf(loaded->level,sizeof loaded->level,"%s",navKey);
        if(g_navExpansion)snprintf(g_navExpansion->level,sizeof g_navExpansion->level,"%s",navKey);
        drive_masks_reset();   // AltBotMod V9: the last map's driving / boat maps go with its grid
        nav_free(*nav_slot());
        if (loaded) { *nav_slot() = loaded; *nav_building() = loaded->phase!=2; bot_log("level: loaded nav grid %s%s", path,*nav_building()?"; surveying missing deployment area":""); if(!*nav_building())ladders_link(); }
        else {
            *nav_slot() = nav_create(navKey,low[0],low[2],high[0],high[2],2.0f);
            *nav_building() = *nav_slot()!=NULL;
            bot_log("level: no nav grid for %s, %s (%.0f..%.0f, %.0f..%.0f)",g_level,*nav_slot()?"building":"allocation/size failure",low[0],high[0],low[2],high[2]);
        }
    }
    trace_level_load();
    zone_level_load();   // AltBotMod V34
}
static int level_ready(void) { return g_levelReady; }

// ---- Commands ----
// ---- Squad spawn rules (stricter than the game's own): a squadmate can be spawned on unless it is in combat - an
// enemy soldier within 25 m, hit / shot at / fired in the last 15 s, or (a bot) fighting / in cover / firing rockets - and
// at most one bot every 15 s on the same squadmate (no hydra). In a vehicle only with a free seat (the bot rides). ----
#define SQUAD_SPAWN_ENEMY_CLEAR 25.0f
static char *vehicle_occupant(char *veh);
#define SQUAD_SPAWN_EACH_FRAMES (15 * 30)
static char *g_squadSpawnOn[16];
static unsigned g_squadSpawnOnAt[16];
static int squadmate_in_combat(char *m, char *ms)
{
    int mi = bot_index(m);
    if (mi >= 0) {
        BotBrain *mb = bot_brain(mi);
        if (mb->task == TASK_FIGHT || mb->task == TASK_COVER || mb->task == TASK_ROCKET) return 1;
        if (g_hitAt[mi] && H->frame - g_hitAt[mi] < 15 * 30) return 1;
        if (g_attackedAt[mi] && H->frame - g_attackedAt[mi] < 15 * 30) return 1;
    }
    if (fired_within(m, 15 * 30)) return 1;   // has been shooting
    char *pm = player_manager();
    void **arr = pm ? *(void ***)(pm + 0xac) : NULL;
    unsigned count = pm ? *(unsigned *)(pm + 8) : 0;
    float *mp = (float *)(ms + OFF_SOLDIER_SPAWN_POSITION);
    for (unsigned k = 0; k < count && k < 64; k++) {   // an enemy close by
        char *e = arr[k], *es;
        if (!e || player_team(e) == player_team(m) || !(es = *(char **)(e + OFF_PLAYER_SOLDIER)) || *(float *)(es + OFF_SOLDIER_HEALTH) <= 0) continue;
        if (dist2((float *)(es + OFF_SOLDIER_SPAWN_POSITION), mp) < SQUAD_SPAWN_ENEMY_CLEAR * SQUAD_SPAWN_ENEMY_CLEAR) return 1;
    }
    return 0;
}
static int squadmate_spawned_on_recently(char *m)
{
    for (int k = 0; k < 16; k++) if (g_squadSpawnOn[k] == m && H->frame - g_squadSpawnOnAt[k] < SQUAD_SPAWN_EACH_FRAMES) return 1;
    return 0;
}
static void squadmate_note_spawn(char *m)
{
    int slot = 0;
    for (int k = 0; k < 16; k++) if (H->frame - g_squadSpawnOnAt[k] >= SQUAD_SPAWN_EACH_FRAMES) { slot = k; break; }
    g_squadSpawnOn[slot] = m; g_squadSpawnOnAt[slot] = H->frame ? H->frame : 1;
}
// After a squad spawn into a vehicle: ride with a bot driver (as a passenger of its drive) or follow a human one.
static void squadmate_ride(int i, char *veh)
{
    if (bm_air_profile(vehicle_name(veh)) && air_ride(i, veh)) return;
    for (int j = 0; j < H->botCount; j++)
        if (g_drive[j].phase && g_drive[j].veh == veh && j != i) {
            DriveState *d = &g_drive[i];
            memset(d, 0, sizeof *d);
            d->phase = 7; d->veh = veh; d->at = H->frame;
            return;
        }
    char *b0 = vehicle_occupant(veh);
    if (b0 && !is_bot(b0)) assign_task(i, TASK_FOLLOW, b0);   // a player's vehicle: ride along with them
}

// A bot without a squad (round restarts empty them) is moved into one with its teammates - the bot squad with room
// first, else a new one - via RCON while it is dead. Returns 1 if a move was just requested (spawn a moment later).
static unsigned g_squadJoinAt[MAX_BOTS], g_spawnJoinWait[MAX_BOTS];
static int squad_join(char *bot, int team)
{
    int i = bot_index(bot);
    if (i < 0 || H->frame - g_squadJoinAt[i] < 5 * 30) return 0;   // asked recently: spawn as it is
    g_squadJoinAt[i] = H->frame ? H->frame : 1;
    int members[33] = {0}, bots[33] = {0};
    char *pm = player_manager();
    void **arr = pm ? *(void ***)(pm + 0xac) : NULL;
    unsigned count = pm ? *(unsigned *)(pm + 8) : 0;
    for (unsigned k = 0; k < count && k < 64; k++) {
        char *m = arr[k];
        int s = m ? player_squad(m) : 0;
        if (!m || player_team(m) != team || s < 1 || s > 32) continue;
        members[s]++;
        if (is_bot(m)) bots[s]++;
    }
    int pick = 0;
    for (int s = 1; s <= 32 && !pick; s++) if (bots[s] && members[s] < 4) pick = s;   // a bot squad with room
    for (int s = 1; s <= 32 && !pick; s++) if (!members[s]) pick = s;                  // else a new one
    if (!pick) return 0;
    chat_rcon("admin.movePlayer\t%s\t%d\t%d\tfalse", bot + OFF_PLAYER_ONLINE_NAME, team, pick);
    bot_log("squad: %s joins %s squad %d", bot + OFF_PLAYER_ONLINE_NAME, team == 1 ? "US" : "RU", pick);
    return 1;
}
// Squad membership is optional. RCON may refuse a move while the team has
// no human or the server is transitioning. Repeated requests cannot renew this
// deadline; the next spawn attempt deploys independently after three seconds.
static int spawn_wait_for_squad(char *bot, int team)
{
    int i=bot_index(bot);
    if(i<0)return 0;
    if(player_squad(bot)){g_spawnJoinWait[i]=0;return 0;}
    if(!g_spawnJoinWait[i]){
        g_spawnJoinWait[i]=H->frame?H->frame:1;
        if(g_mode==BM_MODE_SQDM||mode_is_squad_rush())chat_rcon("admin.movePlayer\t%s\t%d\t1\tfalse",bot+OFF_PLAYER_ONLINE_NAME,team);
        else squad_join(bot,team);
        return 1;
    }
    return H->frame-g_spawnJoinWait[i]<3*30;
}
// A squadmate a bot may spawn on: on solid ground - not swimming, not in mid-air (the game's squad spawn crashed on
// Seine, where bots end up in the river; players cannot spawn on a swimming mate either).
static int squadmate_spawnable(char *mate, char *ms)
{
    if (player_vehicle(mate)) return 1;   // a free seat (checked by the callers)
    NavGrid *g = *nav_slot();
    float *p = (float *)(ms + OFF_SOLDIER_POSITION), gp[3];
    if (!g || g->phase != 2) return 0;
    if (nav_is_water(g, p[0], p[1], p[2])) return 0;
    // AltBotMod V32 (as Fun-Bots 3.6.0-dev7): not on a mate in a fight - one hit in the last 5 s, or a living enemy soldier
    // within 30 m of it (Fun-Bots measured 20-30% of squad-rush spawns next to an enemy before, 2% after)
    {   int mj = bot_index(mate);
        if (mj >= 0 && g_attackedAt[mj] && H->frame - g_attackedAt[mj] < 5 * 30) return 0;
        char *pm = player_manager(); void **arr = NULL; unsigned count = 0;
        if (pm) { memcpy(&arr, pm + 0xac, sizeof arr); memcpy(&count, pm + 8, sizeof count); }
        int team = player_team(mate);
        for (unsigned k = 0; arr && k < count && k < 64; k++) {
            char *t = arr[k], *ts;
            if (!t || player_team(t) == team || !(ts = *(char **)(t + OFF_PLAYER_SOLDIER)) || *(float *)(ts + OFF_SOLDIER_HEALTH) <= 0) continue;
            if (dist2((float *)(ts + OFF_SOLDIER_POSITION), p) < 30.0f * 30.0f) return 0;
        }
    }
    return nav_snap(g, p[0], p[1], p[2], gp) && fabsf(gp[1] - p[1]) < 1.5f;
}
static int cse_valid(void *e)
{
    if (!e || IsBadReadPtr(e, 0x18) || *(unsigned *)e != 0x025bbbc0) return 0;
    unsigned *data = *(unsigned **)((char *)e + 0x14);
    return data && !IsBadReadPtr(data, 0x70) && data[0] >= 0x00400000u && data[0] < 0x03000000u;   // (V16: through the Team field)
}
// The side a bot belongs on by its name ("... US" team 1, "... RU" team 2) - whatever the game's round swap says.
static int named_team(char *pl)
{
    const char *n = BOT_ROSTER(pl);
    return strstr(n, " US") ? 1 : strstr(n, " RU") ? 2 : (player_team(pl) == 2 ? 2 : 1);
}
// Map loaded (teams swapped by the game): every dead bot back on its named side in one go, before anyone spawns -
// nobody dies for it (bots used to be corrected at spawn, moved back by the squad code, spawned on the wrong side
// and killed to be corrected again: 156 deaths on one 60-bot map change).
static void teams_fix_all(void)
{
    int moved = 0;
    for (int i = 0; i < H->botCount; i++) {
        char *pl = H->bots[i];
        int want = named_team(pl);
        if (*(void **)(pl + OFF_PLAYER_SOLDIER) || player_team(pl) == want) continue;
        if (((AddToTeam_t)ADDR_ADD_TO_TEAM)(pl, want, 0, 1)) moved++;
    }
    if (moved) bot_log("level: %d bot(s) moved back to their named team before spawning", moved);
}
// Caspian V4.1: dead bots crew helicopters and armour. The native squad spawn never put a bot into a vehicle seat
// (every "spawn on squadmate (in a vehicle)" in the logs failed), so the bot spawns as usual and, within 5 s of
// spawning, enters a free gunner/passenger seat of a teammate bot's attack helicopter (first), transport helicopter
// or tank/IFV - EnterVehicle takes a seat at any distance, which is what the game's own spawn-in-vehicle does.
static unsigned g_crewMoveAt[MAX_BOTS];
static char *g_crewVehicle[MAX_BOTS];
static unsigned g_crewAt[MAX_BOTS];
static unsigned g_crewSpawns, g_crewFails;
static unsigned char g_crewDriver[MAX_BOTS];   // AltBotMod V6: g_crewVehicle is a vehicle to spawn into as driver / pilot
static char *vspawn_pick(char *bot, int team);   // spawn_vehicle.inc
static int vspawn_board(int i, char *v);
static char *crew_pick(char *bot, int team)
{
    char *best = NULL; int bestRank = 0;
    for (int j = 0; j < H->botCount; j++) {
        char *m = H->bots[j];
        if (!m || m == bot || player_team(m) != team) continue;
        char *v = player_vehicle(m), *ms = *(char **)(m + OFF_PLAYER_SOLDIER);
        if (!v || !ms || !vehicle_ok(v) || vehicle_seat_of(v, m) != 0 || free_seat_count(v) <= 0) continue;
        const BMAirProfile *a = bm_air_profile(vehicle_name(v));
        // AltBotMod V16: jeeps, boats and other light transports take passengers too (V4-V15: gunner seats of armor and
        // helicopters only - a driven Humvee or RHIB never filled up at the spawn screen)
        int rank = a ? (a->kind == BM_AIR_ATTACK ? 4 : a->kind == BM_AIR_JET ? 0 : 3) : bm_armored(bm_vehicle_profile(vehicle_name(v))) ? 2 : 1;
        int taken = 0;   // another bot already on its way to this seat
        for (int k = 0; k < H->botCount; k++) if (g_crewVehicle[k] == v && H->bots[k] != bot && H->frame - g_crewAt[k] < 10 * 30) taken++;
        if (taken >= free_seat_count(v)) continue;
        if (rank > bestRank) { bestRank = rank; best = v; }
    }
    return best;
}
static int crew_spawn(char *bot, int team, const char *name)
{
    int i = bot_index(bot);
    if (i < 0 || !g_enhanced.driving || g_roundCleanup) return 0;
    if (g_crewMoveAt[i] && H->frame - g_crewMoveAt[i] < 15 * 30) return 0;   // the last try failed: not again at once
    if (g_crewDriver[i] && g_crewVehicle[i]) return 0;   // (vspawn_prepare chose a vehicle to spawn into)
    char *v = crew_pick(bot, team);
    if (!v) return 0;
    g_crewVehicle[i] = v; g_crewAt[i] = H->frame ? H->frame : 1;
    bot_log("crew: %s spawns to crew the %s (%d free seat(s))", name, vehicle_name(v), free_seat_count(v));
    return 0;   // the ordinary spawn goes ahead; crew_board_tick seats it
}
static void squadmate_ride(int i, char *veh);
static void crew_board_tick(int i)
{
    char *v = g_crewVehicle[i];
    if (!v) return;
    char *pl = H->bots[i], *s = *(char **)(pl + OFF_PLAYER_SOLDIER);
    if (H->frame - g_crewAt[i] > 6 * 30) { g_crewVehicle[i] = NULL; return; }
    if (!s || *(float *)(s + OFF_SOLDIER_HEALTH) <= 0) return;   // not spawned yet
    g_crewVehicle[i] = NULL;
    if (g_crewDriver[i]) {   // AltBotMod V6: the driver / pilot seat
        g_crewDriver[i] = 0;
        if (g_drive[i].phase || !vspawn_board(i, v)) g_crewMoveAt[i] = H->frame ? H->frame : 1;
        return;
    }
    if (player_vehicle(pl) || g_drive[i].phase || !vehicle_ok(v)) return;
    char *pilot = vehicle_occupant(v);
    int seat = free_seat(v);
    if (!pilot || !player_exists(pilot) || player_team(pilot) != player_team(pl) || seat <= 0) { g_crewFails++; return; }
    ((EnterVehicle_t)ADDR_PLAYER_ENTER_VEHICLE)(pl, v, seat);
    if (player_vehicle(pl) != v) {
        g_crewFails++; g_crewMoveAt[i] = H->frame ? H->frame : 1;
        bot_log("crew: %s could not take seat %d of %s's %s (%u failed)", pl + OFF_PLAYER_ONLINE_NAME, seat, pilot + OFF_PLAYER_ONLINE_NAME, vehicle_name(v), g_crewFails);
        return;
    }
    g_crewSpawns++;
    nav_cancel_path(i); bot_nav(i)->mode = 0; memset(H->botMove[i], 0, sizeof H->botMove[i]);
    bot_log("crew: %s takes seat %d of %s's %s (%u crew seats filled)", pl + OFF_PLAYER_ONLINE_NAME, seat, pilot + OFF_PLAYER_ONLINE_NAME, vehicle_name(v), g_crewSpawns);
    squadmate_ride(i, v);
}
// AltBotMod V6: a parked aircraft / base vehicle first - as a player picks one on the spawn screen. Returns 1 when
// one was chosen (the ordinary spawn then goes ahead and crew_board_tick seats the bot).
static int vspawn_prepare(char *bot, int team, const char *name)
{
    int i = bot_index(bot);
    if (i < 0) return 0;
    g_crewDriver[i] = 0;
    if (g_crewMoveAt[i] && H->frame - g_crewMoveAt[i] < 15 * 30) return 0;
    char *pv = vspawn_pick(bot, team);
    if (!pv) return 0;
    g_crewVehicle[i] = pv; g_crewAt[i] = H->frame ? H->frame : 1; g_crewDriver[i] = 1;
    bot_log("vehicle-spawn: %s picks the %s at the spawn screen", name, vehicle_name(pv));
    return 1;
}
static void cmd_spawn(const char *name)
{
    char *bot = find_player(name);
    if (!bot) { bot_log("spawn: no player named '%s'", name); return; }
    if (*(void **)(bot + OFF_PLAYER_SOLDIER)) { bot_log("spawn: '%s' is already alive", name); return; }
    if (!g_levelReady) { bot_log("spawn: '%s' waits - level not ready", name); return; }
    // The game swaps teams between rounds: keep "... US" bots on team 1 and "... RU" bots on team 2.
    {
        int want = bm_roster_team(BOT_ROSTER(bot),g_mode,0);
        if (want && player_team(bot) != want) {
            char ok = ((AddToTeam_t)ADDR_ADD_TO_TEAM)(bot, want, 0, 1);
            bot_log("spawn: moved '%s' back to team %d (%s)", name, want, ok ? "ok" : "FAILED");
        }
        if (want && player_team(bot) != want) return;   // still on the wrong side: wait (never spawn there to be killed later)
    }
    if (g_kitsAllDropped && pick_loadout(bot) < 0) {   // AltBotMod V14: a soldier with no kit is invisible to players - wait
        static unsigned saidAt;
        if (!saidAt || H->frame - saidAt > 60 * 30) { saidAt = H->frame ? H->frame : 1; bot_log("spawn: bots wait - no kit is usable on this level (see the loadouts: lines); a soldier without one is invisible"); }
        return;
    }
    if (!mode_is_cq()) { mode_spawn(bot); return; }
    if (deployment_spawn(bot)) return;
    int team = player_team(bot) == 2 ? 2 : 1;
    if (!*(void **)(bot + OFF_PLAYER_BLUEPRINT)) {
        if (!H->teamBlueprint[team]) {
            if(g_jobEpoch==g_roundEpoch && g_jobBlueprint && !IsBadReadPtr(g_jobBlueprint,4) &&
               *(unsigned *)g_jobBlueprint==0x02500e38)H->teamBlueprint[team]=g_jobBlueprint;
            else {bot_log("spawn: team %d waits for the current level's MpSoldier asset",team);return;}
        }
        ((SetPtr_t)ADDR_PLAYER_SET_BLUEPRINT)(bot, H->teamBlueprint[team]);
    }
    if (spawn_wait_for_squad(bot, team)) return;
    if (as_spawn(bot, team)) return;   // AltBotMod V16: Air Superiority
    // the team's HQ spawn point gone: the level is being torn down (next map / restart) - nobody spawns, and no kit is
    // handed out (its assets are being freed: that crashed the server during map changes)
    if (H->teamSpawn[team].entity && !cse_valid(H->teamSpawn[team].entity)) {
        H->teamSpawn[team].entity = NULL;
        g_levelReady = 0;
        bot_log("level: team %d's HQ spawn point is gone - the level is unloading, bots wait", team);
        return;
    }
    if(!H->teamSpawn[team].entity)hq_select_option(team);
    BotTeamSpawn *ts = &H->teamSpawn[team];
    BotTeamSpawn flagArgs = {0};
    int noHq = !ts->entity;   // Conquest Assault defenders: flags only
    if (noHq) { flagArgs.a2 = 0xffffffff; flagArgs.a3 = 1; flagArgs.a4 = 0; ts = &flagArgs; }
    // Front-line spawn: of HQ and the flags the team holds (not being taken by the enemy), the one closest to a flag
    // the team still has to capture - so bots on long maps / carrier starts get into the fight quickly.
    void *vehicleSpawn = vspawn_prepare(bot, team, name) ? NULL : fleet_spawn(bot);
    if (!vehicleSpawn && crew_spawn(bot, team, name)) return;   // Caspian V4: gunner seats first
    if (vehicleSpawn) {
        flagArgs.entity = vehicleSpawn;
        int fleetBot=bot_index(bot);
        flagArgs.a2 = g_fleet[fleetBot].spawnArgs[0]; flagArgs.a3 = g_fleet[fleetBot].spawnArgs[1]; flagArgs.a4 = g_fleet[fleetBot].spawnArgs[2];
        ts = &flagArgs;
    }
    void *ent = ts->entity;
    const char *where = vehicleSpawn ? "validated deployment (vehicle assignment)" : "HQ";
    int best = -1;
    float bestD = 1e30f;
    {   // a defending squad spawns at the flag it guards (if it is ours and not being taken)
        int sqf = squad_objective(team, player_squad(bot));
        if (!vehicleSpawn && sqf >= 0 && squad_defending(team, player_squad(bot)) && flag_owner(sqf) == team && g_flagSpawn[sqf][team]) {
            int ctl = *(int *)(g_flags[sqf] + OFF_CP_CONTROL_TEAM);
            if (!ctl || ctl == team) { best = sqf; bestD = -1; }
        }
    }
    int sqObj = vehicleSpawn ? -1 : squad_objective(team, player_squad(bot));   // attacking squad: get near its flag
    if (sqObj >= 0 && squad_defending(team, player_squad(bot))) sqObj = -1;
    for (int s = noHq ? 0 : -1; !vehicleSpawn && s < g_flagCount && bestD >= 0; s++) {
        const float *sp = s < 0 ? g_hqPos[team] : flag_pos(s);
        if (s >= 0) {
            int ctl = *(int *)(g_flags[s] + OFF_CP_CONTROL_TEAM);
            if (flag_owner(s) != team || !g_flagSpawn[s][team] || (ctl && ctl != team)) continue;
        }
        float d = 1e30f;
        if (sqObj >= 0) d = dist2(flag_pos(sqObj), sp);   // the squad's flag
        else
            for (int f = 0; f < g_flagCount; f++) {
                if (flag_owner(f) == team) continue;
                float dx = flag_pos(f)[0] - sp[0], dz = flag_pos(f)[2] - sp[2], dd = dx * dx + dz * dz;
                if (dd < d) d = dd;
            }
        if (s < 0 && d > 1e29f) break;   // team holds every flag: HQ
        if (d < bestD || (noHq && best < 0)) { bestD = d; best = s; }
    }
    if (!vehicleSpawn && sqObj < 0 && bestD >= 0 && player_squad(bot) > 0) {   // in a player's squad: on the human leader (alive, on foot, not shooting)
        char *pm = player_manager(), *lead = NULL;
        void **arr = pm ? *(void ***)(pm + 0xac) : NULL;
        unsigned count = pm ? *(unsigned *)(pm + 8) : 0;
        for (unsigned k = 0; k < count && k < 64 && !lead; k++) {
            char *m = arr[k], *ms;
            if (!m || is_bot(m) || player_team(m) != team || player_squad(m) != player_squad(bot) || !m[0x788]) continue;
            if (!(ms = *(char **)(m + OFF_PLAYER_SOLDIER)) || *(float *)(ms + OFF_SOLDIER_HEALTH) <= 0) continue;
            if (player_vehicle(m) && !free_seat_count(player_vehicle(m))) continue;
            if (squadmate_spawned_on_recently(m) || squadmate_in_combat(m, ms) || !squadmate_spawnable(m, ms)) continue;
            lead = m;
        }
        if (lead) {
            typedef char (__attribute__((thiscall)) *SpawnOnSquad_t)(void *extent, void *mate);
            apply_kit(bot);   // the bot's class loadout from the library (only right before it really spawns)
            {
                float *lp = (float *)(*(char **)(lead + OFF_PLAYER_SOLDIER) + OFF_SOLDIER_POSITION);
                bot_log("spawn: '%s' tries its squad leader %s at (%.0f, %.1f, %.0f)%s", name, lead + OFF_PLAYER_ONLINE_NAME, lp[0], lp[1], lp[2],
                        player_vehicle(lead) ? " (in a vehicle)" : "");
            }
            char ok = ((SpawnOnSquad_t)0x01185b50)(bot + *(int *)0x0285bad8, lead);
            char *s = *(char **)(bot + OFF_PLAYER_SOLDIER);
            if (ok && s) {
                squadmate_note_spawn(lead);
                bot_log("spawn: '%s' team=%d on its squad leader %s%s -> soldier=%p", name, team, lead + OFF_PLAYER_ONLINE_NAME,
                        player_vehicle(bot) ? " into a vehicle seat" : "", s);
                int k = bot_index(bot);
                if (k >= 0) { g_lastSpawnedBot[k] = bot; g_lastSpawnedAt[k] = H->frame; if (player_vehicle(bot)) squadmate_ride(k, player_vehicle(bot)); }
                return;
            }
        }
    }
    if (sqObj >= 0 && bestD > 0) {   // ...or on the squadmate nearest it, like players do (not one in a vehicle or a fight)
        char *pm = player_manager(), *mate = NULL;
        float mateD = bestD;
        int squad = player_squad(bot);
        void **arr = pm ? *(void ***)(pm + 0xac) : NULL;
        unsigned count = pm ? *(unsigned *)(pm + 8) : 0;
        for (unsigned k = 0; k < count && k < 64; k++) {
            char *m = arr[k], *ms;
            if (!m || m == bot || player_team(m) != team || player_squad(m) != squad) continue;
            if (!(ms = *(char **)(m + OFF_PLAYER_SOLDIER)) || *(float *)(ms + OFF_SOLDIER_HEALTH) <= 0) continue;
            if (player_vehicle(m) && !free_seat_count(player_vehicle(m))) continue;   // a full vehicle
            if (squadmate_spawned_on_recently(m) || squadmate_in_combat(m, ms) || !squadmate_spawnable(m, ms)) continue;
            float d = dist2((float *)(ms + OFF_SOLDIER_SPAWN_POSITION), flag_pos(sqObj));
            if (d < mateD) { mateD = d; mate = m; }
        }
        if (mate) {
            typedef char (__attribute__((thiscall)) *SpawnOnSquad_t)(void *extent, void *mate);
            apply_kit(bot);
            {
                float *mp = (float *)(*(char **)(mate + OFF_PLAYER_SOLDIER) + OFF_SOLDIER_POSITION);
                bot_log("spawn: '%s' tries squadmate %s at (%.0f, %.1f, %.0f)%s", name, mate + OFF_PLAYER_ONLINE_NAME, mp[0], mp[1], mp[2],
                        player_vehicle(mate) ? " (in a vehicle)" : "");
            }
            char ok = ((SpawnOnSquad_t)0x01185b50)(bot + *(int *)0x0285bad8, mate);   // VeniceServerPlayerExtent::spawnOnSquad
            char *s = *(char **)(bot + OFF_PLAYER_SOLDIER);
            if (ok && s) {
                squadmate_note_spawn(mate);
                bot_log("spawn: '%s' team=%d on squadmate %s (%.0f m from flag %d)%s -> soldier=%p", name, team, mate + OFF_PLAYER_ONLINE_NAME,
                        sqrtf(mateD), sqObj, player_vehicle(bot) ? " into a vehicle seat" : "", s);
                int k = bot_index(bot);
                if (k >= 0) { g_lastSpawnedBot[k] = bot; g_lastSpawnedAt[k] = H->frame; if (player_vehicle(bot)) squadmate_ride(k, player_vehicle(bot)); }
                return;
            }
            bot_log("spawn: '%s' could not spawn on %s - using a spawn point", name, mate + OFF_PLAYER_ONLINE_NAME);
        }
    }
    if (noHq && !vehicleSpawn && best < 0) { bot_log("spawn: '%s' waits - its team has no HQ and no flag to spawn on", name); return; }
    if (best >= 0) {
        static char flagName[16];
        snprintf(flagName, sizeof flagName, "flag %d", best);
        ent = g_flagSpawn[best][team];
        where = flagName;
    }
    int cand = -1;
    int slot = -1; /* no spawn probes: do not create sacrificial soldiers */

    if (0)   // spawning at HQ: is there a closer HQ to discover?
        for (int k = 0; k < g_hqCandCount; k++)
            if (!g_hqCandTried[team][k] && g_hqCand[k] != ts->entity && spawn_team(g_hqCand[k]) != 3 - team) { cand = k; break; }   // not the other team's
    if (cand >= 0) { g_hqCandTried[team][cand] = 1; ent = g_hqCand[cand]; where = "HQ candidate (probe)"; }
    if (!cse_valid(ent)) {   // a spawn point that is gone (or never was one): back to HQ, else wait
        bot_log("spawn: '%s' - spawn point %p (%s) is not valid", name, ent, where);
        if (cand >= 0) cand = -1;
        if (ent == ts->entity || noHq || !cse_valid(ts->entity)) { if (ent == ts->entity && !noHq) ts->entity = NULL; return; }
        ent = ts->entity; where = "HQ";
    }
    apply_kit(bot);
    void *r = ((CseSpawn_t)ADDR_CSE_SPAWN)(ent, bot, ts->a2, ts->a3, ts->a4);
    if(!r && !*(void **)(bot+OFF_PLAYER_SOLDIER) && vehicleSpawn && !noHq &&
       ent!=H->teamSpawn[team].entity && cse_valid(H->teamSpawn[team].entity)){
        // A closer captured-flag vehicle spawn can be refused by the engine.
        // Retain the same-team original HQ as the real fallback, not the
        // temporary flagArgs struct that selected this vehicle deployment.
        BotTeamSpawn *hq=&H->teamSpawn[team];
        ent=hq->entity;where="HQ (vehicle flag spawn refused)";
        r=((CseSpawn_t)ADDR_CSE_SPAWN)(ent,bot,hq->a2,hq->a3,hq->a4);
    }
    if (cand >= 0 && r) { HqProbe *q = &g_hqProbes[slot]; q->bot = bot; q->team = team; q->cand = cand; q->at = H->frame ? H->frame : 1; }
    if (cand >= 0 && !r) bot_log("spawn: HQ candidate %p refused team %d - dropped", ent, team);
    if (!r && ent != ts->entity && !noHq && cse_valid(ts->entity)) {   // flag spawn refused: fall back to HQ
        where = "HQ (flag spawn refused)";
        ent = ts->entity;
        r = ((CseSpawn_t)ADDR_CSE_SPAWN)(ent, bot, ts->a2, ts->a3, ts->a4);
    }
    bot_log("spawn: '%s' team=%d at %s via entity=%p -> soldier=%p", name, team, where, ent, r);
    if(!r && !*(void **)(bot+OFF_PLAYER_SOLDIER))hq_spawn_refused(team,ent);
    {
        int k = bot_index(bot);
        if (k >= 0 && *(void **)(bot+OFF_PLAYER_SOLDIER)) {
            g_lastSpawnedBot[k] = bot; g_lastSpawnedAt[k] = H->frame;
        }
    }
}

static void cmd_pos(const char *name)
{
    char *pl = find_player(name);
    char *soldier = pl ? *(char **)(pl + OFF_PLAYER_SOLDIER) : NULL;
    if (!soldier) { bot_log("pos: '%s' has no soldier", name); return; }
    float *v = (float *)(soldier + OFF_SOLDIER_POSITION);
    float *s = (float *)(soldier + OFF_SOLDIER_SPAWN_POSITION);
    bot_log("pos: %s soldier=%p sim=(%.2f, %.2f, %.2f) transform=(%.2f, %.2f, %.2f) yaw=%.1fdeg", name, soldier, v[0], v[1], v[2], s[0], s[1], s[2],
            *(float *)(soldier + OFF_SOLDIER_YAW) * 180.0f / 3.14159265f);
}

__declspec(dllexport) void botmod_command(char *line)
{
    bot_log("> %s", line);
    if (!strcmp(line, "ehstatus")) { enhanced_status(); return; }
    if (!strcmp(line, "ehdiag")) { enhanced_diagnostics(); return; }
    if (!strcmp(line, "ehvehicles")) { enhanced_vehicles(); return; }
    if (!strcmp(line, "ehfleet")) { fleet_status(); enhanced_vehicles(); return; }
    if (!strcmp(line, "ehreload")) { enhanced_load_config(); enhanced_status(); return; }
    int isAddH = !strncmp(line, "addh", 4) && (line[4] == 0 || line[4] == ' ');
    if (isAddH || (!strncmp(line, "add", 3) && (line[3] == 0 || line[3] == ' '))) {
        char first[64] = "";
        int team = 0;
        sscanf(line + (isAddH ? 4 : 3), "%63s %d", first, &team);
        int n = atoi(first);
        if (n > 0) { for (int i = 0; i < n; i++) cmd_add(NULL, team, !isAddH); }
        else cmd_add(first, team, !isAddH);
    } else if (!strncmp(line, "spawn ", 6)) {
        char name[64] = "";
        sscanf(line + 6, "%63s", name);
        cmd_spawn(name);
    } else if (!strncmp(line, "move ", 5)) {
        char name[64] = "";
        float t = 0, s = 0, y = 0;
        int idx = -1;
        if (sscanf(line + 5, "%d %f %f %f", &idx, &t, &s, &y) == 4 && idx >= 0 && idx < H->botCount) snprintf(name, sizeof name, "%s", (char *)H->bots[idx] + OFF_PLAYER_ONLINE_NAME);
        else sscanf(line + 5, "%63s %f %f %f", name, &t, &s, &y);   // move <bot# | name without spaces> t s y
        int i = bot_index(find_player(name));
        if (i < 0) { bot_log("move: '%s' is not a bot", name); return; }
        bot_nav(i)->mode = 0;
        H->botMove[i][0] = t; H->botMove[i][1] = s; H->botMove[i][2] = y;
        bot_log("move: %s throttle=%.2f strafe=%.2f yaw=%.2f", name, t, s, y);
    } else if (!strncmp(line, "face ", 5)) {
        char name[64] = "";
        float yawDeg = 0, pitchDeg = 0;
        {   // face <name with spaces> <yawDeg> <pitchDeg>
            const char *e = line + strlen(line);
            int words = 0;
            while (e > line + 5 && words < 2) { e--; if (*e == ' ' && e[1] != ' ') words++; }
            snprintf(name, sizeof name, "%.*s", (int)(e - (line + 5)), line + 5);
            sscanf(e, "%f %f", &yawDeg, &pitchDeg);
        }
        int i = bot_index(find_player(name));
        if (i < 0) { bot_log("face: '%s' is not a bot", name); return; }
        bot_nav(i)->mode = 1;
        bot_nav(i)->targetYaw = wrap_pi(yawDeg * 3.14159265f / 180.0f);
        bot_aim(i)[1] = pitchDeg * 3.14159265f / 180.0f;
        bot_log("face: %s yaw=%.1f pitch=%.1f deg", name, yawDeg, pitchDeg);
    } else if (!strncmp(line, "aimmode ", 8)) {
        *aim_mode() = atoi(line + 8);
        for (int i = 0; i < H->botCount; i++)   // clear what we wrote so the engine's own values apply
            if (!*aim_mode()) *(int *)((char *)H->botInput[i] + OFF_INPUT_AIM_VALID) = 0;
        bot_log("aimmode %d", *aim_mode());
    } else if (!strncmp(line, "aim ", 4)) {
        char name[64] = "";
        float yawDeg = 0, pitchDeg = 0;
        sscanf(line + 4, "%63s %f %f", name, &yawDeg, &pitchDeg);
        int i = bot_index(find_player(name));
        if (i < 0) { bot_log("aim: '%s' is not a bot", name); return; }
        bot_nav(i)->mode = 0;
        H->botMove[i][2] = 0;
        bot_aim(i)[0] = yawDeg * 3.14159265f / 180.0f;
        bot_aim(i)[1] = pitchDeg * 3.14159265f / 180.0f;
        bot_log("aim: %s authoritative yaw=%.1f pitch=%.1f deg", name, yawDeg, pitchDeg);
    } else if (!strncmp(line, "goto ", 5)) {
        char name[64] = "";
        float x = 0, z = 0;
        {   // goto <name with spaces> <x> <z>
            const char *e = line + strlen(line);
            int words = 0;
            while (e > line + 5 && words < 2) { e--; if (*e == ' ' && e[1] != ' ') words++; }
            snprintf(name, sizeof name, "%.*s", (int)(e - (line + 5)), line + 5);
            sscanf(e, "%f %f", &x, &z);
        }
        int i = bot_index(find_player(name));
        if (i < 0) { bot_log("goto: '%s' is not a bot", name); return; }
        bot_nav(i)->mode = 2;
        bot_nav(i)->x = x;
        bot_nav(i)->z = z;
        bot_log("goto: %s -> (%.1f, %.1f)", name, x, z);
    } else if (!strncmp(line, "navbuild ", 9)) {
        char level[32] = "";
        float x0, z0, x1, z1, cell = 2.0f;
        if (sscanf(line + 9, "%31s %f %f %f %f %f", level, &x0, &z0, &x1, &z1, &cell) < 5) { bot_log("usage: navbuild <level> <x0> <z0> <x1> <z1> [cell]"); return; }
        nav_free(*nav_slot());
        *nav_slot() = nav_create(level, x0, z0, x1, z1, cell);
        *nav_building() = 1;
        bot_log("navbuild: %s %dx%d cells of %.1fm", level, (*nav_slot())->w, (*nav_slot())->h, cell);
    } else if (!strncmp(line, "navload ", 8)) {
        char path[MAX_PATH + 64];
        nav_file(path, sizeof path, line + 8);
        NavGrid *g = nav_load(path);
        if (!g) { bot_log("navload: cannot load %s", path); return; }
        nav_free(*nav_slot());
        *nav_slot() = g;
        *nav_building() = 0;
        bot_log("navload: %s %dx%d cells", g->level, g->w, g->h);
    } else if (!strcmp(line, "navrebuild")) {   // throw away this level's grid (memory + file); it is rebuilt now
        NavGrid *g = *nav_slot();
        if (!g || g->phase != 2) { bot_log("navrebuild: no finished grid loaded"); return; }
        char path[MAX_PATH + 64];
        nav_file(path, sizeof path, g->level);
        DeleteFileA(path);
        float x0 = g->x0 - g->cell * 0.5f, z0 = g->z0 - g->cell * 0.5f, x1 = x0 + g->w * g->cell, z1 = z0 + g->h * g->cell, cell = g->cell;
        char lvl[32];
        snprintf(lvl, sizeof lvl, "%s", g->level);
        *nav_slot() = nav_create(lvl, x0, z0, x1, z1, cell);
        *nav_building() = 1;
        nav_clear_links();   // (ladders re-link when the build is done)
        nav_free(g);
        bot_log("navrebuild: rebuilding %s (%.0f..%.0f x %.0f..%.0f, %.1f m cells)", lvl, x0, x1, z0, z1, cell);
    } else if (!strcmp(line, "navstatus")) {
        NavGrid *g = *nav_slot();
        if (!g) bot_log("nav: none");
        else {
            int water = 0, fences = 0, doors = 0;
            if (g->phase == 2)
                for (int k = 0; k < g->w * g->h * NAV_LAYERS; k++) {
                    water += (g->flags[k] & NAV_WATER) != 0;
                    for (int d = 0; d < 8; d++) { if (d & 1) doors += (g->fences[k] >> d) & 1; else fences += (g->fences[k] >> d) & 1; }
                }
            bot_log("nav: %s %dx%d phase=%d building=%d rays=%u, %d water node(s), %d knifeable fence crossing(s), %d doorway(s)", g->level, g->w,
                    g->h, g->phase, *nav_building(), g->raysCast, water, fences / 2, doors / 2);
        }
    } else if (!strncmp(line, "path ", 5)) {
        char name[64] = "";
        float x = 0, z = 0, y = NAN;
        {   // path <name with spaces> <x> <z>   |   path <name with spaces> <x> <z> y<height>
            const char *e = line + strlen(line), *ys = strrchr(line, ' ');
            int words = 0, want = ys && ys[1] == 'y' ? 3 : 2;
            if (want == 3) y = (float)atof(ys + 2);
            while (e > line + 5 && words < want) { e--; if (*e == ' ' && e[1] != ' ') words++; }
            snprintf(name, sizeof name, "%.*s", (int)(e - (line + 5)), line + 5);
            sscanf(e, "%f %f", &x, &z);
        }
        int i = bot_index(find_player(name));
        char *soldier = i >= 0 ? *(char **)((char *)H->bots[i] + OFF_PLAYER_SOLDIER) : NULL;
        if (!soldier) { bot_log("path: '%s' is not a living bot", name); return; }
        g_consolePlan=1;
        int n = plan_path(i, soldier, x, isnan(y) ? ((float *)(soldier + OFF_SOLDIER_POSITION))[1] : y, z);
        g_consolePlan=0;
        if (!n) { bot_log("path: no route for %s to (%.1f, %.1f): %s", name, x, z, nav_last_error()); return; }
        bot_nav(i)->mode = 3;
        bot_log("path: %s -> (%.1f, %.1f) via %d waypoint(s)", name, x, z, n);
        for (int k = 0; k < n && k < 12; k++) bot_log("    wp%d (%.1f, %.1f, %.1f)", k, g_paths[i].pts[k * 3], g_paths[i].pts[k * 3 + 1], g_paths[i].pts[k * 3 + 2]);
    } else if (!strcmp(line, "flags") || !strcmp(line, "flags rescan")) {
        if (!g_flagCount || line[5]) find_flags();
        for (int f = 0; f < g_flagCount; f++) bot_log("  flag %d at (%.1f, %.1f, %.1f) owner %d, in zone %d", f, flag_pos(f)[0], flag_pos(f)[1], flag_pos(f)[2], flag_owner(f),
                                          *(int *)(g_flags[f] + OFF_CP_CONTROL_TEAM));
    } else if (!strncmp(line, "brain ", 6)) {
        char name[64] = "", onoff[8] = "on";
        {   // brain <name with spaces | all> [on|off]
            const char *e = strrchr(line + 6, ' ');
            if (e && (!strcmp(e + 1, "on") || !strcmp(e + 1, "off"))) { snprintf(name, sizeof name, "%.*s", (int)(e - (line + 6)), line + 6); snprintf(onoff, sizeof onoff, "%s", e + 1); }
            else snprintf(name, sizeof name, "%s", line + 6);
        }
        int on = strcmp(onoff, "off") != 0;
        if (!g_flagCount) find_flags();
        for (int i = 0; i < H->botCount; i++) {
            if (_stricmp(name, "all") && _stricmp(name, (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME)) continue;
            memset(bot_brain(i), 0, sizeof(BotBrain));
            bot_brain(i)->enabled = on;
            g_brainHeld[i] = (unsigned char)!on;
            bot_brain(i)->targetFlag = -1;
            bot_brain(i)->avoidFlag = -1;
            if (!on) { bot_nav(i)->mode = 0; H->botMove[i][0] = H->botMove[i][2] = 0; }
            bot_log("brain: %s %s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, on ? "on" : "off");
        }
    } else if (!strncmp(line, "sprintbits ", 11)) {   // sprintbits <bot#> <frames>: research - set the sprint flag bits that long
        int i = -1, frames = 90;
        sscanf(line + 11, "%d %d", &i, &frames);
        if (i < 0 || i >= H->botCount) { bot_log("usage: sprintbits <bot#> <frames>"); return; }
        g_sprintUntil[i] = H->frame + frames;
        bot_log("sprintbits: %s for %d frame(s)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, frames);
    } else if (!strncmp(line, "setpose ", 8)) {   // setpose <bot#> <0 stand|1 crouch|2 prone>: research - write the wanted stance
        int i = -1, pose = 0;
        sscanf(line + 8, "%d %d", &i, &pose);
        char *s = i >= 0 && i < H->botCount ? *(char **)((char *)H->bots[i] + OFF_PLAYER_SOLDIER) : NULL;
        if (!s || pose < 0 || pose > 2) { bot_log("usage: setpose <bot#> <0|1|2>"); return; }
        bot_log("setpose: %s stance %d (to %d) -> to %d", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, *(int *)(s + OFF_SOLDIER_POSE), *(int *)(s + OFF_SOLDIER_POSE + 4), pose);
        *(int *)(s + OFF_SOLDIER_POSE + 4) = pose;
    } else if (!strncmp(line, "pressi ", 7)) {
        int i = -1, action = -1, frames = 1;
        sscanf(line + 7, "%d %d %d", &i, &action, &frames);
        if (i < 0 || i >= H->botCount || action < 0) { bot_log("usage: pressi <bot#> <action> [frames]"); return; }
        press_button(i, action, frames);
        bot_log("press: %s action %d for %d frame(s)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, action, frames);
    } else if (!strncmp(line, "press ", 6)) {
        char name[64] = "";
        int action = -1, frames = 1;
        sscanf(line + 6, "%63s %d %d", name, &action, &frames);
        int i = bot_index(find_player(name));
        if (i < 0 || action < 0) { bot_log("usage: press <bot> <action> [frames]"); return; }
        press_button(i, action, frames);
        bot_log("press: %s action %d for %d frame(s)", name, action, frames);
    } else if (!strcmp(line, "loadouts")) {
        LoadoutLib *lib = loadout_lib();
        bot_log("loadouts: %d stored", lib->count);
        for (int i = 0; i < lib->count; i++) log_loadout(&lib->l[i], NULL);
    } else if (!strcmp(line, "capture on") || !strcmp(line, "capture off")) {
        char dir[MAX_PATH], path[MAX_PATH + 32];
        GetModuleFileNameA(NULL, dir, sizeof dir);
        *strrchr(dir, '\\') = 0;
        snprintf(path, sizeof path, "%s\\BotMod_capture.on", dir);
        if (line[9] == 'n') { FILE *f = fopen(path, "w"); if (f) fclose(f); } else DeleteFileA(path);
        bot_log("loadout capture %s (deploys %s change the bot library)", line[9] == 'n' ? "ON" : "OFF", line[9] == 'n' ? "WILL" : "will NOT");
    } else if (!strncmp(line, "place ", 6)) {   // place <bot name> <slot>: select that slot, fire, back to the rifle (gadget test)
        const char *e = strrchr(line + 6, ' ');
        char name[64] = "";
        if (e) snprintf(name, sizeof name, "%.*s", (int)(e - (line + 6)), line + 6);
        int i = bot_index(find_player(name)), slot = e ? atoi(e + 1) : -1;
        if (i < 0 || slot < 0 || slot > 9) { bot_log("usage: place <bot name> <slot>"); return; }
        place_gadget_now(i, slot);
        bot_log("place: %s uses slot %d", name, slot);
    } else if (!strcmp(line, "navunblock")) {   // every link blocked at stuck spots: open again now
        bot_log("navunblock: %d link(s) reopened", *nav_slot() ? nav_unblock_expired(*nav_slot(), 1) : 0);
    } else if (!strcmp(line, "loadouts load")) {   // reloads on the discovery worker (the asset search takes ~40 s: never on the game thread)
        g_jobDoLoadouts = 1;
        g_levelReady = 0;
        bot_log("loadouts: reloading in the background");
    } else if (!strcmp(line, "roster")) {
        // 4 bots per team named by role; creates whichever are missing.
        static const char *roles[4] = {"Medic", "Engineer", "Support", "Recon"};
        static const char *tag[3] = {"", "US", "RU"};
        char *pm = player_manager();
        for (int team = 1; team <= 2; team++) {
            for (int r = 0; r < 4; r++) {
                char name[32];
                snprintf(name, sizeof name, "[BOT] %s %s", roles[r], tag[team]);
                if (find_roster(name)) continue;
                // (renaming an existing bot re-registers its network ghost and trips an engine assert: only create)
                {
                    cmd_add(name, team, 0);
                    bot_brain(H->botCount - 1)->enabled = 1;
                    bot_brain(H->botCount - 1)->targetFlag = -1;
                    bot_brain(H->botCount - 1)->avoidFlag = -1;
                }
            }
        }
    } else if (!strncmp(line, "vehicles", 8)) {   // vehicles [player]: every vehicle - type, position, health, free seats
        char *ref = line[8] == ' ' ? find_player(line + 9) : NULL;
        float *rp = ref ? human_pos(ref) : NULL;
        char **vehs;
        int nv = vehicle_list(&vehs);
        for (int k = 0; k < nv; k++) {
            char *v = vehs[k];
            if (IsBadReadPtr(v, 0x200) || *(unsigned *)v != ADDR_VEHICLE_VTABLE) continue;   // (stale cache entries)
            int vt = 0;
            float *vp = vehicle_pos(v, &vt);
            bot_log("  veh #%d %p %s team %d at (%.0f, %.0f, %.0f) hp %.0f, %d free seat(s)%s", k, v, vehicle_name(v), vt,
                    vp ? vp[0] : 0, vp ? vp[1] : 0, vp ? vp[2] : 0, *(float *)(v + OFF_VEHICLE_HEALTH), free_seat_count(v),
                    rp && vp ? (dist2(rp, vp) < 60.0f * 60.0f ? "  <- near" : "") : "");
        }
        bot_log("%d vehicle(s)", nv);
    } else if (!strcmp(line, "drive off") || !strcmp(line, "drive on")) {
        g_driveOff = line[7] == 'f';
        bot_log("drive: bots %s light vehicles", g_driveOff ? "do not take" : "may take");
    } else if (!strncmp(line, "driveto ", 8)) {   // driveto <bot#> <x> <z>: a bot at the wheel drives there
        int i = -1; float x = 0, z = 0;
        if (sscanf(line + 8, "%d %f %f", &i, &x, &z) != 3 || i < 0 || i >= H->botCount || !player_vehicle(H->bots[i])) { bot_log("usage: driveto <bot# at a wheel> <x> <z>"); return; }
        if (bm_air_profile(vehicle_name(player_vehicle(H->bots[i])))) { air_manual_guard(player_vehicle(H->bots[i])); return; }
        DriveState *d = &g_drive[i];
        memset(d, 0, sizeof *d);
        d->veh = player_vehicle(H->bots[i]); d->phase = 2; d->at = H->frame; d->flag = -1; d->manual = 1;
        d->goal[0] = x; d->goal[2] = z; d->goal[1] = NAN;
        for (int f = 0; f < g_flagCount; f++)   // at a flag: its height
            if ((flag_pos(f)[0] - x) * (flag_pos(f)[0] - x) + (flag_pos(f)[2] - z) * (flag_pos(f)[2] - z) < 60.0f * 60.0f) d->goal[1] = flag_pos(f)[1];
        float *vp = vehicle_pos(d->veh, NULL);
        if (vp) memcpy(d->start, vp, 12);
        g_driving[i] = 1;
        bot_log("driveto: %s -> (%.0f, %.0f)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, x, z);
    } else if (!strncmp(line, "drive ", 6)) {   // drive <bot#> <veh#>: put the bot in the driver's seat (research)
        int i = -1, k = -1;
        char **vehs;
        int nv = vehicle_list(&vehs);
        if (sscanf(line + 6, "%d %d", &i, &k) != 2 || i < 0 || i >= H->botCount || k < 0 || k >= nv) { bot_log("usage: drive <bot#> <veh#>"); return; }
        if (bm_air_profile(vehicle_name(vehs[k]))) { air_manual_guard(vehs[k]); return; }
        char **b;
        if (IsBadReadPtr(vehs[k], 0x200) || *(unsigned *)vehs[k] != ADDR_VEHICLE_VTABLE || !vehicle_seats(vehs[k], &b) || *(void **)(b[0] + 0x4c)) {
            bot_log("drive: no free driver's seat");
            return;
        }
        bot_nav(i)->mode = 0;
        H->botMove[i][0] = H->botMove[i][1] = H->botMove[i][2] = 0;
        ((EnterVehicle_t)ADDR_PLAYER_ENTER_VEHICLE)(H->bots[i], vehs[k], 0);
        g_driving[i] = 1;
        bot_log("drive: %s -> driver of %p (%s)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, vehs[k], player_vehicle(H->bots[i]) ? "in" : "FAILED");
    } else if (!strncmp(line, "enter ", 6)) {
        char name[64] = "", human[64] = "";
        sscanf(line + 6, "%63s %63s", name, human);
        int i = bot_index(find_player(name));
        char *h = find_player(human);
        if (i < 0 || !h || !player_vehicle(h)) { bot_log("usage: enter <bot> <player-in-a-vehicle>"); return; }
        bot_enter_vehicle(i, player_vehicle(h));
    } else if (!strncmp(line, "exit ", 5)) {
        int i = bot_index(find_player(line + 5));
        if (i >= 0) bot_exit_vehicle(i, 1);
    } else if (!strncmp(line, "crouchaction ", 13)) {
        g_crouchAction = atoi(line + 13);
        bot_log("crouch action = %d", g_crouchAction);
    } else if (!strncmp(line, "say ", 4)) {
        char name[64] = "";
        int type = 0;
        sscanf(line + 4, "%63s %d", name, &type);
        int i = bot_index(find_player(name));
        if (i < 0) { bot_log("usage: say <bot> <orderType>"); return; }
        bot_say(i, type);
    } else if (!strncmp(line, "kill ", 5)) {
        char *pl = find_player(line + 5);
        if (!is_bot(pl)) bot_log("kill: '%s' is not a bot", line + 5); else { kill_bot(pl); bot_log("kill: %s", line + 5); }
    } else if (!strncmp(line, "rayflags ", 9)) {
        g_rayFlags = strtoul(line + 9, NULL, 0);
        bot_log("rayflags 0x%x", g_rayFlags);
    } else if (!strncmp(line, "navprobe ", 9)) {   // navprobe x0 z0 x1 z1 fromY: add ground missed under a bridge / roof
        float x0, z0, x1, z1, fy;
        NavGrid *g = *nav_slot();
        if (sscanf(line + 9, "%f %f %f %f %f", &x0, &z0, &x1, &z1, &fy) != 5 || !g || g->phase != 2) { bot_log("usage: navprobe x0 z0 x1 z1 fromY (nav grid ready)"); return; }
        int n = nav_reprobe_area(g, ray_cast, x0, z0, x1, z1, fy);
        bot_log("navprobe: %d ground node(s) added in (%.0f..%.0f, %.0f..%.0f) from y %.1f", n, x0, x1, z0, z1, fy);
        g_navDirty = 1;
        nav_save_if_dirty();
    } else if (!strncmp(line, "navautorepair ", 14)) {
        g_autoRepair = atoi(line + 14);
        bot_log("navautorepair %d", g_autoRepair);
    } else if (!strncmp(line, "botpath ", 8)) {   // botpath <bot#>: the route a bot is following (goal, next waypoint, all points)
        int i = atoi(line + 8);
        if (i < 0 || i >= H->botCount) { bot_log("usage: botpath <bot#>"); return; }
        BotPath *p = &g_paths[i];
        BotBrain *b = bot_brain(i);
        char out[1000];
        int k = snprintf(out, sizeof out, "botpath: %s nav mode %d task %s flag %d - goal (%.0f,%.1f,%.0f), next %d of %d:", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME,
                         bot_nav(i)->mode, kTaskNames[b->task], b->targetFlag, p->goalX, p->goalY, p->goalZ, p->next, p->count);
        for (int q = 0; q < p->count && k < (int)sizeof out - 40; q++)
            k += snprintf(out + k, sizeof out - k, "%s(%.0f,%.1f,%.0f)%s", q == p->next ? " >" : " ", p->pts[q * 3], p->pts[q * 3 + 1], p->pts[q * 3 + 2], p->link[q] >= 0 ? "L" : "");
        bot_log("%s", out);
    } else if (!strncmp(line, "edgetest ", 9)) {   // edgetest x y z d: why the link from there in direction d (0..7) is / is not there
        float x, y, z;
        int d = 0;
        NavGrid *g = *nav_slot();
        if (sscanf(line + 9, "%f %f %f %d", &x, &y, &z, &d) != 4 || d < 0 || d > 7 || !g || g->phase != 2) { bot_log("usage: edgetest x y z dir(0..7: +x,+x+z,+z,-x+z,-x,-x-z,-z,+x-z)"); return; }
        char out[700];
        nav_debug_edge(g, ray_cast, x, y, z, d, out, sizeof out);
        bot_log("edgetest: %s", out);
    } else if (!strncmp(line, "navroute ", 9)) {   // navroute x y z tx ty tz: the soldier route between two points (waypoints)
        float s[3], t[3], pts[128 * 3];
        NavGrid *g = *nav_slot();
        if (sscanf(line + 9, "%f %f %f %f %f %f", &s[0], &s[1], &s[2], &t[0], &t[1], &t[2]) != 6 || !g || g->phase != 2) { bot_log("usage: navroute x y z tx ty tz"); return; }
        int n = nav_find_path(g, ray_cast, s[0], s[1], s[2], t[0], t[1], t[2], pts, 128);
        if (!n) { bot_log("navroute: no route (%s)", nav_last_error()); return; }
        char out[900];
        int k = snprintf(out, sizeof out, "navroute: %d point(s):", n);
        for (int q = 0; q < n && k < (int)sizeof out - 40; q++) k += snprintf(out + k, sizeof out - k, " (%.0f,%.1f,%.0f)", pts[q * 3], pts[q * 3 + 1], pts[q * 3 + 2]);
        bot_log("%s", out);
    } else if (!strncmp(line, "navtest ", 8)) {   // navtest x y z: reachable nodes from there on the live grid
        float x, y, z, np[3];
        NavGrid *g = *nav_slot();
        if (sscanf(line + 8, "%f %f %f", &x, &y, &z) != 3 || !g || g->phase != 2) { bot_log("usage: navtest x y z (nav grid ready)"); return; }
        int n = nav_reach(g, x, y, z, np);
        bot_log("navtest: from (%.1f, %.1f, %.1f) node (%.1f, %.1f, %.1f) reaches %d nodes", x, y, z, np[0], np[1], np[2], n);
    } else if (!strncmp(line, "navrepair ", 10)) {   // navrepair <x> <z> [radius]: fine slope check of the links around a spot
        float x, z, r = 8.0f;
        NavGrid *g = *nav_slot();
        if (sscanf(line + 10, "%f %f %f", &x, &z, &r) < 2 || !g || g->phase != 2) { bot_log("usage: navrepair x z [radius] (nav grid ready)"); return; }
        float top[3] = {x, 400.0f, z}, bot[3] = {x, -100.0f, z}, gp[3];   // the real ground there (not the lowest nav layer)
        if (!ray_cast(top, bot, gp, NULL)) { bot_log("navrepair: no ground at (%.1f, %.1f)", x, z); return; }
        int n = nav_repair_around(g, ray_cast, x, gp[1], z, r, 0, 0);
        if (n) g_navDirty = 1;
        bot_log("navrepair: %d link(s) removed around (%.1f, %.1f), r %.0f m", n, x, z, r);
        nav_save_if_dirty();
    } else if (!strncmp(line, "knife ", 6)) {   // knife <bot name>: the fence-cutting sequence, whatever is in front
        int i = bot_index(find_player(line + 6));
        char *s = i >= 0 ? *(char **)((char *)H->bots[i] + OFF_PLAYER_SOLDIER) : NULL;
        if (!s) { bot_log("knife: '%s' is not a living bot", line + 6); return; }
        g_fenceAt[i] = 0;
        const FenceKind *fk = fence_ahead(i, s);
        bot_log("knife: ahead of %s: %s (material %08x)", line + 6, fk ? fk->name : "nothing catalogued", hit_material());
        unsigned f = H->frame;
        schedule_press(i, SLOT_ACTION(7), f + 1);
        schedule_press(i, EIA_FIRE_KNIFE, f + 25);
        schedule_press(i, EIA_FIRE_KNIFE, f + 45);
        schedule_press(i, EIA_FIRE_KNIFE, f + 65);
        schedule_press(i, SLOT_ACTION(0), f + 90);
    } else if (!strncmp(line, "ground ", 7)) {   // ground <player>: the surface material under them and 3 m around
        char *pl = find_player(line + 7);
        float *hp = pl ? human_pos(pl) : NULL;
        if (!hp) { bot_log("ground: '%s' not found / not spawned", line + 7); return; }
        static const float ring[9][2] = {{0, 0}, {3, 0}, {-3, 0}, {0, 3}, {0, -3}, {2.1f, 2.1f}, {-2.1f, 2.1f}, {2.1f, -2.1f}, {-2.1f, -2.1f}};
        char out[600];
        int n = snprintf(out, sizeof out, "ground under %s (%.0f, %.0f):", line + 7, hp[0], hp[2]);
        unsigned saved = g_rayFlags;
        g_rayFlags = RAY_DONT_CHECK_CHARACTER;
        for (int k = 0; k < 9; k++) {
            float a[3] = {hp[0] + ring[k][0], hp[1] + 3.0f, hp[2] + ring[k][1]}, b[3] = {a[0], hp[1] - 5.0f, a[2]}, h[3];
            if (!ray_cast(a, b, h, NULL)) { n += snprintf(out + n, sizeof out - n, " [none]"); continue; }
            char *body = *(char **)g_lastHitRest;
            unsigned bvt = body && !IsBadReadPtr(body, 4) ? *(unsigned *)body : 0;
            n += snprintf(out + n, sizeof out - n, " %s%08x", bvt == 0x02486a40 ? "water:" : "", hit_material());
        }
        g_rayFlags = saved;
        bot_log("%s", out);
    } else if (!strncmp(line, "squadspawn ", 11)) {   // squadspawn <bot#> <player>: spawn a dead bot on a squadmate (research)
        int i = -1, used = 0;
        if (sscanf(line + 11, "%d %n", &i, &used) < 1 || i < 0 || i >= H->botCount) { bot_log("usage: squadspawn <bot#> <squadmate>"); return; }
        char *bot = H->bots[i], *mate = find_player(line + 11 + used);
        if (!mate) { bot_log("squadspawn: no player '%s'", line + 11 + used); return; }
        if (*(void **)(bot + OFF_PLAYER_SOLDIER)) { bot_log("squadspawn: %s is alive - kill it first", bot + OFF_PLAYER_ONLINE_NAME); return; }
        int team = player_team(bot) == 2 ? 2 : 1;
        if (!*(void **)(bot + OFF_PLAYER_BLUEPRINT) && H->teamBlueprint[team]) ((SetPtr_t)ADDR_PLAYER_SET_BLUEPRINT)(bot, H->teamBlueprint[team]);
        apply_kit(bot);
        typedef char (__attribute__((thiscall)) *SpawnOnSquad_t)(void *extent, void *mate);
        int extentOff = *(int *)0x0285bad8;   // ServerPlayer -> its VeniceServerPlayerExtent
        char r = ((SpawnOnSquad_t)0x01185b50)(bot + extentOff, mate);
        char *s = *(char **)(bot + OFF_PLAYER_SOLDIER);
        float *mp = human_pos(mate);
        bot_log("squadspawn: %s on %s (squads %d / %d): %s, soldier %p at (%.0f, %.0f) - mate at (%.0f, %.0f)", bot + OFF_PLAYER_ONLINE_NAME,
                mate + OFF_PLAYER_ONLINE_NAME, player_squad(bot), player_squad(mate), r ? "ok" : "refused", s, s ? ((float *)(s + OFF_SOLDIER_SPAWN_POSITION))[0] : 0,
                s ? ((float *)(s + OFF_SOLDIER_SPAWN_POSITION))[2] : 0, mp ? mp[0] : 0, mp ? mp[2] : 0);
    } else if (!strcmp(line, "ladders")) {   // (re)scan this level's ladders, list and link them
        find_ladders();
        ladders_link();
    } else if (!strncmp(line, "heightmap ", 10)) {   // heightmap <x> <z> <half size m> <step m>: ground heights around a point
        float cx, cz, hs = 3, st = 0.5f, fromY = 400.0f;
        if (sscanf(line + 10, "%f %f %f %f %f", &cx, &cz, &hs, &st, &fromY) < 2) { bot_log("usage: heightmap x z [half] [step] [ray start y]"); return; }
        unsigned saved = g_rayFlags;
        g_rayFlags = RAY_DONT_CHECK_CHARACTER;
        bot_log("heightmap around (%.1f, %.1f), rows = z from high to low, columns = x from low to high, step %.2f", cx, cz, st);
        for (float z = cz + hs; z >= cz - hs - 0.001f; z -= st) {
            char row[1200];
            int n = snprintf(row, sizeof row, "z %7.2f:", z);
            for (float x = cx - hs; x <= cx + hs + 0.001f && n < (int)sizeof row - 8; x += st) {
                float a[3] = {x, fromY, z}, b[3] = {x, -100.0f, z}, p[3];
                if (ray_cast(a, b, p, NULL)) n += snprintf(row + n, sizeof row - n, " %6.2f", p[1]);
                else n += snprintf(row + n, sizeof row - n, "   ----");
            }
            bot_log("%s", row);
        }
        g_rayFlags = saved;
    } else if (!strncmp(line, "ray ", 4)) {
        float a[3], b[3], p[3] = {0}, n[3] = {0};
        char name[64];
        if (sscanf(line + 4, "%f %f %f %f %f %f", &a[0], &a[1], &a[2], &b[0], &b[1], &b[2]) == 6) {
            int h = ray_cast(a, b, p, n);
            bot_log("ray: hit=%d pos=(%.2f, %.2f, %.2f) normal=(%.2f, %.2f, %.2f) material %08x", h, p[0], p[1], p[2], n[0], n[1], n[2],
                    h ? hit_material() : 0);
            if (h) {
                char line[400];
                int k = snprintf(line, sizeof line, "ray: record");
                for (int o = 0; o < 0x40; o += 4) k += snprintf(line + k, sizeof line - k, " +%02x=%08x", 0x20 + o, *(unsigned *)(g_lastHitRest + o));
                bot_log("%s", line);
            }
        } else if (sscanf(line + 4, "%63s", name) == 1 && find_player(name) && *(char **)((char *)find_player(name) + OFF_PLAYER_SOLDIER)) {
            float *sp = (float *)(*(char **)((char *)find_player(name) + OFF_PLAYER_SOLDIER) + OFF_SOLDIER_POSITION);
            float top[3] = {sp[0], sp[1] + 50, sp[2]}, bot[3] = {sp[0], sp[1] - 50, sp[2]};
            int h = ray_cast(top, bot, p, n);
            bot_log("ray down at %s (%.1f, %.1f, %.1f): hit=%d pos=(%.2f, %.2f, %.2f) normal=(%.2f, %.2f, %.2f)", name, sp[0], sp[1], sp[2],
                    h, p[0], p[1], p[2], n[0], n[1], n[2]);
        }
    } else if (!strncmp(line, "difficulty", 10) && (!line[10] || line[10] == ' ')) {   // AltBotMod V9: difficulty [easy..elite|off]
        const char *arg = line[10] ? line + 11 : "";
        if (*arg) {
            int level = bm_difficulty_parse(arg);
            if (level < 0) { bot_log("difficulty: unknown level '%s' (off, easy, normal, hard, veteran, elite)", arg); return; }
            g_enhanced.difficulty = level;
            bot_log("difficulty: set to %s (until restart; Difficulty= in enhanced.ini keeps it)", bm_difficulty_name(level));
        }
        difficulty_status();
    } else if (!strncmp(line, "botcount ", 9)) {
        int n = atoi(line + 9);
        *wanted_bots() = n < 0 ? 0 : n > MAX_BOTS ? MAX_BOTS : n;
        g_rosterAuto = 1;
        save_bot_count();
        bot_log("roster: wanted bot count %d", *wanted_bots());
    } else if (!strncmp(line, "lookpitch ", 10)) {   // lookpitch <bot#> <radians | off>  (test the pitch control)
        int i = -1; char v[16] = "";
        sscanf(line + 10, "%d %15s", &i, v);
        if (i < 0 || i >= H->botCount) { bot_log("usage: lookpitch <bot#> <radians|off>"); return; }
        g_pitchTestOn[i] = strcmp(v, "off") != 0;
        g_pitchTest[i] = (float)atof(v);
        char *s = *(char **)((char *)H->bots[i] + OFF_PLAYER_SOLDIER);
        bot_log("lookpitch: bot %d target %s, now %.3f", i, v, s ? *(float *)(s + OFF_SOLDIER_PITCH) : 0.0f);
    } else if (!strncmp(line, "shoot ", 6)) {   // shoot <bot#> <target bot#>: test bot gunfire (logs the target's health)
        int i = -1, t = -1;
        sscanf(line + 6, "%d %d", &i, &t);
        if (i < 0 || t < 0 || i >= H->botCount || t >= H->botCount || !*(char **)(H->bots[i] + OFF_PLAYER_SOLDIER)) { bot_log("usage: shoot <bot#> <target bot#>"); return; }
        start_fight(i, H->bots[t], 0, "test");
        g_testVictim = H->bots[t];
    } else if (!strncmp(line, "cover ", 6)) {   // cover <bot#>: act as if just hit
        int i = atoi(line + 6);
        char *s = i >= 0 && i < H->botCount ? *(char **)((char *)H->bots[i] + OFF_PLAYER_SOLDIER) : NULL;
        if (!s) { bot_log("usage: cover <living bot#>"); return; }
        g_lastHealth[i] = *(float *)(s + OFF_SOLDIER_HEALTH) + 10.0f;
        cover_react(i, s);
    } else if (!strcmp(line, "ammo")) {   // rounds per weapon slot for every bot (loaded + spare)
        for (int i = 0; i < H->botCount; i++) {
            char row[160];
            int n = 0;
            for (int s = 0; s < 8; s++) {
                int a = bot_ammo(i, s);
                if (a >= 0) n += snprintf(row + n, sizeof row - n, " s%d=%d", s, a);
            }
            bot_log("ammo: %s%s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, n ? row : " (unknown yet)");
        }
    } else if (!strncmp(line, "inwatch ", 8)) {   // inwatch <player> [seconds]: log every input level that changes, and stance
        char name[64] = "";
        int secs = 20;
        const char *e = strrchr(line + 8, ' ');
        if (e && atoi(e + 1) > 0) { secs = atoi(e + 1); snprintf(name, sizeof name, "%.*s", (int)(e - (line + 8)), line + 8); }
        else snprintf(name, sizeof name, "%s", line + 8);
        g_inWatchPl = find_player(name);
        g_inWatchUntil = H->frame + secs * 30;
        memset(g_inWatchPrev, 0, sizeof g_inWatchPrev);
        { char *wp = g_inWatchPl, *win = wp ? *(char **)(wp + OFF_PLAYER_INPUT) : NULL; if (win && !IsBadReadPtr(win, 0x400)) memcpy(g_inWatchRaw, win, 0x400); }
        g_inWatchPose = -1;
        bot_log("inwatch: %s for %d s", g_inWatchPl ? name : "(no such player)", secs);
    } else if (!strncmp(line, "inaim ", 6)) {   // inaim <player>: research - a player's input aim fields (humans: their client's)
        char *pl = find_player(line + 6);
        if (!pl) { bot_log("inaim: no player '%s'", line + 6); return; }
        char *in = *(char **)(pl + OFF_PLAYER_INPUT), *veh = player_vehicle(pl), *s = *(char **)(pl + OFF_PLAYER_SOLDIER);
        if (!in) { bot_log("inaim: %s has no input", line + 6); return; }
        float *vp = veh ? vehicle_pos(veh, NULL) : NULL;
        bot_log("inaim: %s valid %d aim yaw %.3f pitch %.3f (+1f0 %.3f) | levels yaw %.2f pitch %.2f fire %.2f | veh %p at (%.1f %.1f %.1f) | soldier yaw %.3f pitch %.3f",
                line + 6, *(int *)(in + OFF_INPUT_AIM_VALID), *(float *)(in + OFF_INPUT_AIM_YAW), *(float *)(in + OFF_INPUT_AIM_PITCH),
                *(float *)(in + 0x1f0), *(float *)(in + 0x10 + 4 * 4), *(float *)(in + 0x10 + 5 * 4), *(float *)(in + 0x10 + 7 * 4),
                veh, vp ? vp[0] : 0, vp ? vp[1] : 0, vp ? vp[2] : 0, s ? *(float *)(s + OFF_SOLDIER_YAW) : 0, s ? *(float *)(s + OFF_SOLDIER_PITCH) : 0);
    } else if (!strncmp(line, "gundebug ", 9)) {
        g_gunDebug = atoi(line + 9);
    } else if (!strncmp(line, "seatlv ", 7)) {   // seatlv <bot#> <yaw level> <pitch level>
        int k = -1; float y = 0, pt = 0;
        if (sscanf(line + 7, "%d %f %f", &k, &y, &pt) == 3 && k >= 0 && k < H->botCount) { g_seatYaw[k] = y; g_seatPitch[k] = pt; bot_log("seatlv %d %.2f %.2f", k, y, pt); }
    } else if (!strncmp(line, "lockfake ", 9)) {   // AltBotMod V29: lockfake 0/1 - put locks in place for bots on target
        g_lockFakeOn = atoi(line + 9);
        bot_log("lockfake %d (%u locks put in place so far)", g_lockFakeOn, g_lockFakes);
    } else if (!strncmp(line, "vinput ", 7)) {
        g_vehInput = atoi(line + 7);
        bot_log("vinput %d", g_vehInput);
    } else if (!strncmp(line, "vaim ", 5)) {   // vaim <bot#> <yaw rad> <pitch rad> | vaim <bot#> off  (research: absolute aim in a seat)
        int k = -1; float y = 0, pt = 0; char off[8] = "";
        if (sscanf(line + 5, "%d %7s", &k, off) == 2 && !strcmp(off, "off") && k >= 0 && k < H->botCount) {
            g_vaimOn[k] = 0; *(int *)((char *)H->botInput[k] + OFF_INPUT_AIM_VALID) = 0; bot_log("vaim %d off", k); return;
        }
        if (sscanf(line + 5, "%d %f %f", &k, &y, &pt) != 3 || k < 0 || k >= H->botCount) { bot_log("usage: vaim <bot#> <yaw> <pitch> | off"); return; }
        g_vaimOn[k] = 1; g_vaimYaw[k] = y; g_vaimPitch[k] = pt;
        bot_log("vaim: %s aim yaw %.3f pitch %.3f", (char *)H->bots[k] + OFF_PLAYER_ONLINE_NAME, y, pt);
    } else if (!strncmp(line, "vfire ", 6)) {   // vfire <bot#> <frames>: hold fire
        int k = -1, f = 30;
        sscanf(line + 6, "%d %d", &k, &f);
        if (k >= 0 && k < H->botCount) { press_button(k, EIA_FIRE, f); bot_log("vfire: %s %d frames", (char *)H->bots[k] + OFF_PLAYER_ONLINE_NAME, f); }
    } else if (!strncmp(line, "kitwatch ", 9)) {
        g_kitWatch = atoi(line + 9);
        bot_log("kitwatch %d", g_kitWatch);
    } else if (!strcmp(line, "kitdump")) {   // what the bots are handed, per loadout and item
        LoadoutLib *lib = loadout_lib();
        for (int i = 0; i < lib->count; i++) {
            char who[96];
            for (int w = 0; w < lib->l[i].nWeapons; w++) {
                snprintf(who, sizeof who, "LIBRARY team %d %s", lib->l[i].team, kit_class(lib->l[i].kit));
                log_weapon_msg(who, lib->l[i].weaponMsg[w]);
            }
        }
    } else if (!strcmp(line, "kitskip")) {   // re-read BotMod\kitskip.txt
        load_kitskip();
        bot_log("kitskip: %d rule(s)", g_kitSkipCount);
        for (int k = 0; k < g_kitSkipCount; k++) bot_log("  %s %s", g_kitSkipMap[k], g_kitSkipItem[k]);
    } else if (!strncmp(line, "packs", 5)) {   // research: dump deployed supply packs
        CompCache *cc = &g_compCache[g_compCur];
        bot_log("packs: %d", cc->pcount);
        for (int k = 0; k < cc->pcount; k++) {
            char tag[32];
            snprintf(tag, sizeof tag, "  pack%d", k);
            dump_named(tag, (unsigned char *)cc->pack[k], 0x100);
            dump_named("   +100", (unsigned char *)cc->pack[k] + 0x100, 0x100);
            dump_named("   +200", (unsigned char *)cc->pack[k] + 0x200, 0x100);
        }
    } else if (!strncmp(line, "request ", 8)) {   // request <ammo|heal> <player>: act as if they asked on the radio
        const char *who = line + 8;
        int heal = !strncmp(who, "heal ", 5);
        if (heal || !strncmp(who, "ammo ", 5)) who += 5;
        char *pl = find_player(who);
        if (!pl) { bot_log("request: unknown player '%s'", who); return; }
        order_reaction(pl, heal ? ORDER_REQUESTHEAL : ORDER_REQUESTAMMO, NULL);
    } else if (!strncmp(line, "spot ", 5)) {   // spot <bot name> | <target player>   (the bot spots the target)
        char a[64] = "", b[64] = "";
        const char *bar = strchr(line, '|');
        if (!bar) { bot_log("usage: spot <spotter> | <target>"); return; }
        snprintf(a, sizeof a, "%.*s", (int)(bar - (line + 5)), line + 5);
        snprintf(b, sizeof b, "%s", bar + 1);
        for (char *s = a + strlen(a); s > a && s[-1] == ' '; ) *--s = 0;
        char *bs = b; while (*bs == ' ') bs++;
        char *sp = find_player(a), *tg = find_player(bs);
        if (!sp || !tg) { bot_log("spot: unknown player '%s' or '%s'", a, bs); return; }
        bot_spot(sp, tg);
    } else if (!strncmp(line, "tp ", 3)) {   // tp <bot name> <x> <y> <z>  (experiment: move a soldier by writing its position)
        char name[64] = "";
        float v[3] = {0};
        const char *e = line + strlen(line);
        int words = 0;
        while (e > line + 3 && words < 3) { e--; if (*e == ' ' && e[1] != ' ') words++; }
        snprintf(name, sizeof name, "%.*s", (int)(e - (line + 3)), line + 3);
        sscanf(e, "%f %f %f", &v[0], &v[1], &v[2]);
        char *pl = find_player(name);
        char *s = pl ? *(char **)(pl + OFF_PLAYER_SOLDIER) : NULL;
        if (!s) { bot_log("tp: '%s' has no soldier", name); return; }
        memcpy(s + OFF_SOLDIER_POSITION, v, 12);
        memcpy(s + OFF_SOLDIER_SPAWN_POSITION, v, 12);
        bot_log("tp: %s -> (%.1f, %.1f, %.1f)", name, v[0], v[1], v[2]);
    } else if (!strncmp(line, "pos ", 4)) {
        cmd_pos(line + 4);
    } else if (!strncmp(line, "kit ", 4)) {
        char *pl = find_player(line + 4);
        if (!is_bot(pl)) bot_log("kit: '%s' is not a bot", line + 4); else apply_kit(pl);
    } else if (!strncmp(line, "step ", 5)) {
        H->stepBots = atoi(line + 5);
        bot_log("step bots: %d", H->stepBots);
    } else if (!strncmp(line, "trace ", 6)) {
        int v = atoi(line + 6);
        H->traceGhosts = v & 1;
        H->traceInput = (v & 2) != 0;
        g_traceMessages = (v & 4) != 0;
        if (g_traceMessages) g_seenMsgCount = 0;
        bot_log("trace: ghosts=%d input=%d", H->traceGhosts, H->traceInput);
    } else if (!strcmp(line, "list")) {
        bot_log("%d bot(s); team spawns: 1=%p 2=%p; blueprints: 1=%p 2=%p", H->botCount,
                H->teamSpawn[1].entity, H->teamSpawn[2].entity, H->teamBlueprint[1], H->teamBlueprint[2]);
        for (int i = 0; i < H->botCount; i++) {
            char *pl = H->bots[i];
            BotBrain *bb = bot_brain(i);
            bot_log("  #%d %s player=%p team=%d soldier=%p brain=%d task=%s flag=%d enroute=%d waiting=%d squad=%d", i, pl + OFF_PLAYER_ONLINE_NAME, pl,
                    player_team(pl), *(void **)(pl + OFF_PLAYER_SOLDIER), bb->enabled, bb->task >= 0 && bb->task <= TASK_REPAIR ? kTaskNames[bb->task] : "?",
                    bb->targetFlag, bb->enroute, bb->waiting, player_squad(pl));
        }
    } else {
        bot_log("commands: add|addh [count|name] [team], spawn <name>, move <name> <throttle> <strafe> <yaw>, face <name> <yawDeg> [pitchDeg], goto <name> <x> <z>, path <name> <x> <z>, navbuild <level> <x0> <z0> <x1> <z1> [cell], navload <level>, navstatus, flags, brain <name|all> on|off, kill <name>, loadouts [load], say <name> <order>, pos <name>, kit <name>, "
                "step 0|1, trace <bits: 1=ghosts 2=input>, list, reload, unload");
    }
}

// Bots whose ServerPlayer was destroyed (level change) are dropped and re-added once the new level is ready.
static char g_readdName[MAX_BOTS][32];
static int g_readdTeam[MAX_BOTS], g_readdCount;

static char *g_squadAsked[MAX_BOTS];
static unsigned g_squadAskedAt[MAX_BOTS];

// Drops bot i from every per-bot table (later bots move down one index).
static void bot_forget(int i)
{
    nav_cancel_paths();   // request owners are bot indices; none may survive index compaction
    #define SHIFT(arr) memmove(&(arr)[i], &(arr)[i + 1], sizeof((arr)[0]) * (size_t)(H->botCount - 1 - i))
    if (i < 0 || i >= H->botCount) return;
    char *removed=H->bots[i];
    for(int j=0;j<H->botCount;j++){if(g_fightTarget[j]==removed)g_fightTarget[j]=NULL;if(g_threat[j]==removed)g_threat[j]=NULL;if(g_gunTarget[j]==removed)g_gunTarget[j]=NULL;if(g_leadWho[j]==removed)g_leadWho[j]=NULL;if(g_attacker[j]==removed)g_attacker[j]=NULL;}
    for(int k=0;k<MAX_BOTS;k++)if(g_squadAsked[k]==removed){g_squadAsked[k]=NULL;g_squadAskedAt[k]=0;}
    for(int k=0;k<64;k++)if(g_downed[k].player==removed)memset(&g_downed[k],0,sizeof g_downed[k]);
    enhanced_forget(i);
    if (i < H->botCount - 1) {
        SHIFT(H->bots); SHIFT(H->botInput); SHIFT(H->botMove);
        SHIFT(g_botLoadout); SHIFT(g_progress); SHIFT(g_paths); SHIFT(g_claymores); SHIFT(g_tugs); SHIFT(g_lastGadgetPos);
        SHIFT(g_orderComp); SHIFT(g_orderCompSoldier); SHIFT(g_sayQueue); SHIFT(g_press); SHIFT(g_sched); SHIFT(g_repairVt);
        SHIFT(g_localFlag); SHIFT(g_localUntil); SHIFT(g_localOrder);
        memmove(bot_brain(i), bot_brain(i + 1), sizeof(BotBrain) * (size_t)(H->botCount - 1 - i));
        memmove(bot_nav(i), bot_nav(i + 1), sizeof(BotNav) * (size_t)(H->botCount - 1 - i));
        memmove(bot_aim(i), bot_aim(i + 1), sizeof(float) * 2 * (size_t)(H->botCount - 1 - i));
    }
    unsigned long long m = *kitted_mask(), low = (1ull << i) - 1;
    *kitted_mask() = (m & low) | ((m >> 1) & ~low);
    int last = H->botCount - 1;
    H->bots[last] = NULL; H->botInput[last] = NULL;
    memset(bot_brain(last), 0, sizeof(BotBrain));
    memset(bot_nav(last), 0, sizeof(BotNav));
    memset(&g_paths[last], 0, sizeof g_paths[last]);
    memset(g_sayQueue[last], 0, sizeof g_sayQueue[last]);
    memset(g_press[last], 0, sizeof g_press[last]);
    memset(g_sched[last], 0, sizeof g_sched[last]);
    g_botLoadout[last] = -1; g_orderComp[last] = g_orderCompSoldier[last] = NULL; g_claymores[last] = g_tugs[last] = g_mines[last] = 0;
    memset(&g_progress[last],0,sizeof g_progress[last]);memset(g_lastGadgetPos[last],0,sizeof g_lastGadgetPos[last]);g_repairVt[last]=0;g_localFlag[last]=0;g_localUntil[last]=0;memset(&g_localOrder[last],0,sizeof g_localOrder[last]);
    H->botCount--;
    #undef SHIFT
}

static void validate_bots(void)
{
    for (int i = 0; i < H->botCount; i++) {
        char *pl = H->bots[i];
        int ok = pl && !IsBadReadPtr(pl, 0x400) && *(unsigned *)pl == ADDR_PLAYER_VTABLE && player_manager();
        if (ok) {
            char *pm = player_manager();
            void **arr = *(void ***)(pm + 0xac);
            unsigned count = *(unsigned *)(pm + 8), k;
            for (k = 0; k < count && arr[k] != pl; k++) {}
            ok = k < count;
        }
        if (ok) continue;
        bot_log("bots: stale player removed; a fresh roster player will replace it");
        if(H->botInput[i]) _aligned_free(H->botInput[i]);
        bot_forget(i); i--;
    }
}

static void readd_bots(void)
{ g_readdCount=0; }

// ---- Roster: the wanted bot count (!bots N), split over the teams and named by role ----
// "[BOT] Medic US", "[BOT] Engineer US", ... then "[BOT] Medic US 2" ... Bots above the count are benched (AI off,
// dead); bots are never removed from the server. host->reserved: 320 wanted count (0 = default 8), 324 paused.
#define DEFAULT_BOT_COUNT 8
static const char *kRoleNames[4] = {"Medic", "Engineer", "Support", "Recon"};
static int *wanted_bots(void) { return (int *)(H->reserved + 320); }
static int *bots_paused(void) { return (int *)(H->reserved + 324); }
static int g_rosterAuto;   // manage the roster (autoroster on, or a count was set)

static void botmod_file(char *out, int n, const char *file)   // ...\Battlefield 3 Server\BotMod\<file>
{
    char dir[MAX_PATH];
    GetModuleFileNameA(NULL, dir, sizeof dir);
    *strrchr(dir, '\\') = 0;
    char *up = strrchr(dir, '\\');
    if (up) *up = 0;
    snprintf(out, n, "%s\\BotMod\\%s", dir, file);
}

// BotMod\botcount_<game port>.txt, if there is one, is this server instance's own bot count (e.g. a second instance
// running vehicle-only modes with no bots); otherwise the shared BotMod\botcount.txt.
static void bot_count_file(char *path, int n)
{
    char own[32];
    snprintf(own, sizeof own, "botcount_%d.txt", H->gamePort);
    botmod_file(path, n, own);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) botmod_file(path, n, "botcount.txt");
}
static void save_bot_count(void)
{
    char path[MAX_PATH + 64];
    bot_count_file(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%d\n", *wanted_bots()); fclose(f); }
}

static void load_bot_count(void)
{
    if (*wanted_bots()) return;   // already set in this server run
    char path[MAX_PATH + 64];
    bot_count_file(path, sizeof path);
    FILE *f = fopen(path, "r");
    int n = DEFAULT_BOT_COUNT;
    if (f) { if (fscanf(f, "%d", &n) != 1) n = DEFAULT_BOT_COUNT; fclose(f); }
    *wanted_bots() = n < 0 ? 0 : n > MAX_BOTS ? MAX_BOTS : n;
}

static void roster_name(int team, int k, char *out, int n)
{
    char label[8];if(g_mode==BM_MODE_SQDM)snprintf(label,sizeof label,"SQ%d",team);else snprintf(label,sizeof label,"%s",team==1?"US":"RU");
    if(k<4)snprintf(out,n,"[BOT] %s %s",kRoleNames[k%4],label);else snprintf(out,n,"[BOT] %s %s %d",kRoleNames[k%4],label,k/4+1);
}

// Position of a bot in its team's roster (from its name), -1 if it is not a roster bot.
static int roster_ordinal(const char *name)
{
    for (int r = 0; r < 4; r++) {
        const char *p = strstr(name, kRoleNames[r]);
        if (!p) continue;
        const char *t = strstr(p, " US");
        if (!t) t = strstr(p, " RU");
        if (!t) t=strstr(p," SQ");
        if (!t) return -1;
        int num = atoi(t + (t[1]=='S'?4:3));
        return r + 4 * (num > 1 ? num - 1 : 0);
    }
    return -1;
}

static unsigned char g_brainHeld[MAX_BOTS];   // "brain <bot> off" by hand: the roster leaves it off
static void set_brain(int i, int on)
{
    BotBrain *b = bot_brain(i);
    memset(b, 0, sizeof *b);
    b->enabled = on;
    b->targetFlag = -1;
    b->avoidFlag = -1;
    if (!on) { bot_nav(i)->mode = 0; H->botMove[i][0] = H->botMove[i][2] = 0; }
}

// ---- Removing a bot from the server: kill its soldier, wait until it is gone, then the engine's deletePlayer ----
#define ADDR_PM_DELETE_PLAYER      0x00e67460  // ServerPlayerManager::deletePlayer(ServerPlayer*), thiscall, ret 4
typedef void (__attribute__((thiscall)) *PM_DeletePlayer_t)(void *pm, void *player);
static char *g_removeQueue[MAX_BOTS];
static unsigned g_removeSince[MAX_BOTS];   // Caspian V2: when each queued removal started (indexed like g_removeQueue)
static int g_removeCount;

static int removal_pending(char *pl) { for (int k = 0; k < g_removeCount; k++) if (g_removeQueue[k] == pl) return 1; return 0; }

static void queue_bot_removal(int i)
{
    if (removal_pending(H->bots[i]) || g_removeCount >= MAX_BOTS) return;
    set_brain(i, 0);
    g_removeSince[g_removeCount] = H->frame ? H->frame : 1;
    g_removeQueue[g_removeCount++] = H->bots[i];
    bot_log("roster: removing %s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME);
}

static void retire_stuck_bot(int i,const char *reason)
{
    if(i<0 || i>=H->botCount || removal_pending(H->bots[i]))return;
    char *pl=H->bots[i];g_silentRemove[i]=1;
    {char *so=*(char **)(pl+OFF_PLAYER_SOLDIER);if(so)bm_report_stuck(i,(float *)(so+OFF_SOLDIER_SPAWN_POSITION),"retired");}   // AltBotMod V13: the live map's stuck spots
    air_stop(i,reason,0);
    memset(&g_drive[i],0,sizeof g_drive[i]);memset(&g_fleet[i],0,sizeof g_fleet[i]);
    memset(&g_paths[i],0,sizeof g_paths[i]);bot_nav(i)->mode=0;
    memset(H->botMove[i],0,sizeof H->botMove[i]);
    if(H->botInput[i])for(int a=0;a<60;a++)((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(H->botInput[i],a,0);
    queue_bot_removal(i);
    chat_rcon("admin.kickPlayer\t%s\tBotMod stuck recovery",pl+OFF_PLAYER_ONLINE_NAME);
    bot_log("recovery: retires %s (%s); fresh player, no forced suicide",pl+OFF_PLAYER_ONLINE_NAME,reason);
}

#define REMOVE_KICK_GRACE_FRAMES (5*30)
static void removal_tick(void)
{
    for (int k = 0; k < g_removeCount; k++) {
        char *pl = g_removeQueue[k];
        int i = bot_index(pl);
        if (i >= 0 && *(void **)(pl + OFF_PLAYER_SOLDIER)) {   // wait for the unspawn
            // Caspian V2: an RCON kick does not unspawn a bot whose soldier is alive. Test15 waited for it
            // forever, leaving a brain-less "zombie" standing where it got stuck (17 of 21 retirements in one
            // live round). After 5 s the soldier is killed so the identity can be replaced.
            // Round cleanup stays kick-only (never a suicide loop between rounds).
            // (AltBotMod V6 tried killing living bots here after 10 s - after a hot reload 60 were killed at once and the
            // server hit a FatalAssert ~35 s later. Kick only, as before; restart the server rather than hot-reloading.)
            int kickOnly = g_roundCleanup || (g_silentRemove[i] && H->frame - g_removeSince[k] < REMOVE_KICK_GRACE_FRAMES);
            if (kickOnly) { if(H->frame%90==0) chat_rcon("admin.kickPlayer\t%s\tBotMod fresh roster",pl+OFF_PLAYER_ONLINE_NAME); }
            else if (H->frame % 15 == 0) kill_bot(pl);
            continue;
        }
        g_removeSince[k] = g_removeSince[g_removeCount - 1];
        g_removeQueue[k--] = g_removeQueue[--g_removeCount];
        if (i < 0) continue;
        char name[40];
        snprintf(name, sizeof name, "%s", pl + OFF_PLAYER_ONLINE_NAME);
        void *input = H->botInput[i];
        *(void **)(pl + OFF_PLAYER_INPUT) = NULL;   // ours, not the engine's
        bot_forget(i);
        ((PM_DeletePlayer_t)ADDR_PM_DELETE_PLAYER)(player_manager(), pl);
        if (input) _aligned_free(input);
        bot_log("roster: %s removed from the server", name);
        return;   // one per frame
    }
}

// Players per team the server allows (vars.maxPlayers / 2, from Instance\Admin\Startup.txt; 64 if not found).
#define HUMAN_SLOTS_PER_TEAM 2
// Players per team the server allows right now: its live vars.maxPlayers (RCON, refreshed every 30 s), else the
// -Network.MaxClientCount it was started with. Nothing about the server is assumed.
static int team_capacity(void)
{
    int maxPlayers = chat_max_players();
    static int fromCmdLine;
    if (!fromCmdLine) {
        const char *p = strstr(GetCommandLineA(), "-Network.MaxClientCount ");
        fromCmdLine = p ? atoi(p + 24) : -1;
    }
    // V16.2.1: never more than the server was started for either (the .bat PLAYERS= value)
    if (maxPlayers <= 0 || (fromCmdLine > 0 && fromCmdLine < maxPlayers)) maxPlayers = fromCmdLine;
    if (maxPlayers <= 0) return 0;          // unknown yet: add no bots until the server tells us
    if (maxPlayers > MAX_BOTS + 2 * HUMAN_SLOTS_PER_TEAM) maxPlayers = MAX_BOTS + 2 * HUMAN_SLOTS_PER_TEAM;
    int per=maxPlayers/mode_teams(); return (g_mode==BM_MODE_SQDM || mode_is_squad_rush()) && per>4 ? 4 : per;   // V15: Squad Rush is one squad a side
}

// Bots each team should have now: the wanted count split evenly, but always HUMAN_SLOTS_PER_TEAM free slots per team.
static void roster_targets(int *target, int *humans)
{
    memset(humans,0,5*sizeof *humans);char *pm=player_manager();
    if(pm){void **arr=*(void ***)(pm+0xac);unsigned n=*(unsigned *)(pm+8);for(unsigned k=0;k<n;k++){char *pl=arr[k];if(!pl||is_bot(pl))continue;int t=player_team(pl);if(t>=1&&t<=mode_teams())humans[t]++;}}
    bm_roster_targets(mode_teams(),team_capacity(),g_mode==BM_MODE_SQDM||mode_is_squad_rush()?1:2,*wanted_bots(),humans,target);
}

static void roster_tick(void)
{
    // AltBotMod V7: on a map with no navigation grid yet (first time played: the grid is surveyed with rays for a few
    // minutes) bots play at once when the level has Fun-Bots routes - they follow those; the grid joins in when ready.
    removal_tick(); if(!g_rosterAuto || g_roundCleanup || !level_ready() || !*nav_slot() || ((*nav_slot())->phase!=2 && !trace_loaded()) || H->frame%30!=7 || !team_capacity())return;
    int target[5]={0},humans[5]={0},have[5]={0},last[5]={-1,-1,-1,-1,-1};roster_targets(target,humans);
    for(int i=0;i<H->botCount;i++){int t=bm_roster_team(BOT_ROSTER(H->bots[i]),g_mode,player_team(H->bots[i]));if(t<1||t>mode_teams()||removal_pending(H->bots[i]))continue;have[t]++;last[t]=i;}
    int remove=0,add=0;for(int t=1;t<=mode_teams();t++){if(have[t]>target[t]&&(!remove||have[t]-target[t]>have[remove]-target[remove]))remove=t;if(have[t]<target[t]&&(!add||humans[t]+have[t]<humans[add]+have[add]))add=t;}
    if(remove){queue_bot_removal(last[remove]);return;}
    if(add&&H->botCount<MAX_BOTS){for(int k=0;k<MAX_BOTS;k++){char name[40];roster_name(add,k,name,sizeof name);if(find_roster(name))continue;cmd_add(name,add,0);int i=bot_index(find_roster(name));if(i>=0)set_brain(i,!*bots_paused());return;}}
    for(int i=0;i<H->botCount;i++){if(removal_pending(H->bots[i])||roster_ordinal(BOT_ROSTER(H->bots[i]))<0)continue;int on=!*bots_paused();if(!on&&bot_brain(i)->enabled)set_brain(i,0);else if(on&&!bot_brain(i)->enabled&&!g_brainHeld[i])set_brain(i,1);}
}

// ---- Squads: bots fill bot-led squads (4 players); a bot that finds none creates the next free squad and leads it ----
// Squad changes go through RCON admin.movePlayer (the engine's own squad logic), which only works while the bot is dead.
#define OFF_PLAYER_SQUAD           0x784       // squad id (0 = none)
#define OFF_PLAYER_SQUAD_LEADER    0x788       // byte
#define SQUAD_SIZE                 4
#define MAX_SQUADS                 32


static int player_squad(char *pl) { return *(int *)(pl + OFF_PLAYER_SQUAD); }

// Squad a bot may use to wait for its squad before spawning (gives the RCON move a moment).
static int squad_request_recent(char *pl)
{
    for (int k = 0; k < MAX_BOTS; k++) if (g_squadAsked[k] == pl && H->frame - g_squadAskedAt[k] < 3 * 30) return 1;
    return 0;
}

static void squad_tick(void)
{
    if(g_mode==BM_MODE_SQDM||mode_is_squad_rush()){for(int i=0;i<H->botCount;i++)if(!*(char **)(H->bots[i]+OFF_PLAYER_SOLDIER)&&player_squad(H->bots[i])!=1&&!squad_request_recent(H->bots[i]))chat_rcon("admin.movePlayer\t%s\t%d\t1\tfalse",H->bots[i]+OFF_PLAYER_ONLINE_NAME,player_team(H->bots[i]));return;}
    if (H->frame % 30 != 17) return;
    char *pm = player_manager();
    if (!pm) return;
    void **arr = *(void ***)(pm + 0xac);
    unsigned count = *(unsigned *)(pm + 8);
    int members[3][MAX_SQUADS + 1], botLed[3][MAX_SQUADS + 1], roles[3][MAX_SQUADS + 1];   // roles: bit per class present
    memset(members, 0, sizeof members);
    memset(botLed, 0, sizeof botLed);
    memset(roles, 0, sizeof roles);
    for (unsigned k = 0; k < count; k++) {
        char *pl = arr[k];
        if (!pl) continue;
        int t = player_team(pl), s = player_squad(pl);
        if ((t != 1 && t != 2) || s <= 0 || s > MAX_SQUADS) continue;
        members[t][s]++;
        if (pl[OFF_PLAYER_SQUAD_LEADER] && is_bot(pl)) botLed[t][s] = 1;
        int k = roster_ordinal(BOT_ROSTER(pl));
        if (k >= 0) roles[t][s] |= 1 << (k % 4);
    }
    {   // Caspian V5: a human's squad with room gets bots: every 10 s one dead bot of that team (from a bot squad of 2+)
        // is moved into it, so a player who makes or joins a squad has squadmates to spawn on and command.
        static unsigned movedAt;
        int humanSquad[3] = {0, 0, 0};
        static unsigned char hasHuman[3][MAX_SQUADS + 1];
        memset(hasHuman, 0, sizeof hasHuman);
        for (unsigned k = 0; k < count; k++) {
            char *pl = arr[k]; if (!pl || is_bot(pl)) continue;
            int t = player_team(pl), sq = player_squad(pl);
            if ((t == 1 || t == 2) && sq > 0 && sq <= MAX_SQUADS) hasHuman[t][sq] = 1;
        }
        // The emptiest human squad with room is filled first; bots are never taken out of a squad that has a human.
        for (int t = 1; t <= 2; t++)
            for (int sq = 1; sq <= MAX_SQUADS; sq++)
                if (hasHuman[t][sq] && members[t][sq] < SQUAD_SIZE && (!humanSquad[t] || members[t][sq] < members[t][humanSquad[t]])) humanSquad[t] = sq;
        if (!movedAt || H->frame - movedAt >= 10 * 30)
            for (int t = 1; t <= 2; t++) if (humanSquad[t]) {
                for (int i = 0; i < H->botCount; i++) {
                    char *pl = H->bots[i]; int sq = player_squad(pl);
                    if (player_team(pl) != t || *(void **)(pl + OFF_PLAYER_SOLDIER) || sq == humanSquad[t] || removal_pending(pl) ||
                        squad_request_recent(pl) || !bot_brain(i)->enabled ||
                        (sq > 0 && sq <= MAX_SQUADS && (members[t][sq] < 2 || hasHuman[t][sq]))) continue;
                    chat_rcon("squad.private\t%d\t%d\tfalse", t, humanSquad[t]);
                    chat_rcon("admin.movePlayer\t%s\t%d\t%d\tfalse", pl + OFF_PLAYER_ONLINE_NAME, t, humanSquad[t]);
                    for (int k = 0; k < MAX_BOTS; k++)
                        if (!g_squadAsked[k] || g_squadAsked[k] == pl || H->frame - g_squadAskedAt[k] > 10 * 30) { g_squadAsked[k] = pl; g_squadAskedAt[k] = H->frame; break; }
                    bot_log("squad: %s joins the player's squad %d (%s)", pl + OFF_PLAYER_ONLINE_NAME, humanSquad[t], t == 1 ? "US" : "RU");
                    movedAt = H->frame ? H->frame : 1;
                    break;
                }
            }
    }
    // Caspian V5: bot squads are OPEN (they were made private, so a player could never join one). Every 30 s each
    // bot-led squad is opened, which also undoes the privacy older builds set.
    if (H->frame % (30 * 30) == 17)
        for (int t = 1; t <= 2; t++)
            for (int s = 1; s <= MAX_SQUADS; s++)
                if (botLed[t][s]) chat_rcon("squad.private\t%d\t%d\tfalse", t, s);
    for (int i = 0; i < H->botCount; i++) {
        char *pl = H->bots[i];
        if (player_squad(pl) || *(void **)(pl + OFF_PLAYER_SOLDIER) || removal_pending(pl) || !bot_brain(i)->enabled) continue;
        if (squad_request_recent(pl)) continue;
        int t = named_team(pl), pick = 0, k = roster_ordinal(BOT_ROSTER(pl));   // (named side: the move takes the team too)
        int role = k >= 0 ? 1 << (k % 4) : 0;
        // one of each class per squad where possible: a bot squad missing this class, then any bot squad with room, then a new one
        for (int s = 1; s <= MAX_SQUADS && !pick; s++) if (botLed[t][s] && members[t][s] < SQUAD_SIZE && !(roles[t][s] & role)) pick = s;
        for (int s = 1; s <= MAX_SQUADS && !pick; s++) if (botLed[t][s] && members[t][s] < SQUAD_SIZE) pick = s;
        for (int s = 1; s <= MAX_SQUADS && !pick; s++) if (!members[t][s]) { pick = s; botLed[t][s] = 1; }
        if (!pick) continue;
        members[t][pick]++;
        roles[t][pick] |= role;
        // (a squad a human made private stays private only until this bot joins it)
        chat_rcon("squad.private\t%d\t%d\tfalse", t, pick);
        chat_rcon("admin.movePlayer\t%s\t%d\t%d\tfalse", pl + OFF_PLAYER_ONLINE_NAME, t, pick);
        for (int k = 0; k < MAX_BOTS; k++)
            if (!g_squadAsked[k] || g_squadAsked[k] == pl || H->frame - g_squadAskedAt[k] > 10 * 30) {
                g_squadAsked[k] = pl; g_squadAskedAt[k] = H->frame; break;
            }
        bot_log("squad: %s -> %s squad %d (%s)", pl + OFF_PLAYER_ONLINE_NAME, t == 1 ? "US" : "RU", pick,
                members[t][pick] == 1 ? "creates it" : "joins");
    }
}

// ---- In-game chat commands (!bots ...) - read through RCON by chat.c ----
// Changing commands need the player's name in BotMod\admins.txt (one per line, # comments); without that file everyone may.
static int chat_is_admin(const char *who)
{
    char dir[MAX_PATH], path[MAX_PATH + 32], line[128];
    GetModuleFileNameA(NULL, dir, sizeof dir);
    *strrchr(dir, '\\') = 0;                        // ...\Battlefield_3_Server
    char *up = strrchr(dir, '\\');
    if (up) *up = 0;                                // ...\Battlefield 3 Server
    snprintf(path, sizeof path, "%s\\BotMod\\admins.txt", dir);
    FILE *f = fopen(path, "r");
    if (!f) return 1;
    int ok = 0;
    while (!ok && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *s = line; while (*s == ' ') s++;
        for (char *e = s + strlen(s); e > s && e[-1] == ' '; ) *--e = 0;
        if (*s && *s != '#' && !_stricmp(s, who)) ok = 1;
    }
    fclose(f);
    return ok;
}

static void chat_status(void)
{
    int n[5]={0},alive[5]={0},on=0;for(int i=0;i<H->botCount;i++){int t=player_team(H->bots[i]);if(t<1||t>mode_teams())continue;n[t]++;if(*(void **)(H->bots[i]+OFF_PLAYER_SOLDIER))alive[t]++;if(bot_brain(i)->enabled)on++;}
    chat_say("[AltBotMod] %d active / %d wanted, %s, %s",on,*wanted_bots(),g_modeName,*bots_paused()?"paused":"running");
    for(int t=1;t<=mode_teams();t++)chat_say("[AltBotMod] team %d: %d bots (%d alive)",t,n[t],alive[t]);
}

// Friendly names for !map (vanilla + expansion maps of this build).
static void chat_command(const char *who, const char *text)
{
    bot_log("chat: %s: %s", who, text);
    char cmd[64] = "", arg[64] = "";
    sscanf(text+1,"%63s %63[^\r\n]",cmd,arg);
    if (!_stricmp(cmd, "rush")) {rush_command(who,arg);return;}
    if (!_stricmp(cmd, "botdiag")) {
        enhanced_status();enhanced_diagnostics();
        chat_say("[AltBotMod] %s | map %s, flags %d, ready %d | armour %s, driver weapons %s", BM_ENHANCED_VERSION,
                 g_level[0] ? g_level : "loading", g_flagCount, g_levelReady,
                 g_enhanced.armored ? "EXPERIMENTAL" : "off", g_enhanced.driverWeapons ? "EXPERIMENTAL" : "off");
        return;
    }
    if (!_stricmp(cmd, "botconfig") && !_stricmp(arg, "reload")) {
        if (!chat_is_admin(who)) { chat_say("[AltBotMod] %s: only admins can do that", who); return; }
        enhanced_load_config();
        chat_say("[AltBotMod] enhanced.ini reloaded; see server console for accepted/ignored settings");
        return;
    }
    if (squad_order_chat(who, cmd, arg)) return;   // AltBotMod V34: !attack / !defend / !orders
    if (!_stricmp(cmd, "help")) {
        chat_say("[AltBotMod] !bots = status | admins: !bots <0-%d> or !bots fill = bot count, !bots off / on = pause / resume, !map <name>, !pos", MAX_BOTS);
        chat_say("[AltBotMod] your squad's bots: !attack B, !defend A (or look at it: !attack), !orders, !orders off");
    } else if (!_stricmp(cmd, "bots") && !arg[0]) {
        chat_status();
    } else if (!_stricmp(cmd, "pos")) {   // admins: where am I (for nav work) - position, heading, how much of the nav it reaches
        if (!chat_is_admin(who)) { chat_say("[AltBotMod] %s: only admins can do that", who); return; }
        char *pl = find_player(who), *s = pl ? *(char **)(pl + OFF_PLAYER_SOLDIER) : NULL;
        if (!s) { chat_say("[AltBotMod] %s: not spawned", who); return; }
        float *p = (float *)(s + OFF_SOLDIER_SPAWN_POSITION), np[3] = {0, 0, 0};
        float yaw = -*(float *)(s + OFF_SOLDIER_YAW) * 57.2958f;
        while (yaw >= 360.0f) yaw -= 360.0f;
        while (yaw < 0) yaw += 360.0f;
        NavGrid *g = *nav_slot();
        int reach = g && g->phase == 2 ? nav_reach(g, p[0], p[1], p[2], np) : -1;
        chat_say("[AltBotMod] %s at (%.1f, %.1f, %.1f) facing %.0f - nav reaches %d cells", who, p[0], p[1], p[2], yaw, reach);
        bot_log("pos: %s at (%.2f, %.2f, %.2f) yaw %.0f - nav node (%.1f, %.1f, %.1f) reaches %d", who, p[0], p[1], p[2], yaw, np[0], np[1], np[2], reach);
    } else if (!_stricmp(cmd, "bots") && (!_stricmp(arg, "on") || !_stricmp(arg, "off"))) {
        if (!chat_is_admin(who)) { chat_say("[AltBotMod] %s: only admins can do that", who); return; }
        int on = !_stricmp(arg, "on");
        *bots_paused() = !on;
        g_rosterAuto = 1;
        chat_say(on ? "[AltBotMod] bots resumed by %s - respawning" : "[AltBotMod] bots paused by %s (!bots on to resume)", who);
    } else if (!_stricmp(cmd,"botrefresh")) {
        if(!chat_is_admin(who))return;
        g_roundForceReset=1;chat_say("[AltBotMod] replacing bot roster on the next tick");
    } else if (!_stricmp(cmd, "map")) {
        if (!arg[0]) {
            // BF3 chat lines are short: send the list in several lines (aliases of the same map are left out)
            char list[128];
            int n = snprintf(list, sizeof list, "[AltBotMod] maps:");
            for (int k = 0; k < 29; k++) {
                if (k && !strcmp(bm_maps[k].code, bm_maps[k-1].code)) continue;
                if (n + (int)strlen(bm_maps[k].alias) + 2 > 100) { chat_say("%s", list); n = snprintf(list, sizeof list, "[AltBotMod]"); }
                n += snprintf(list + n, sizeof list - n, " %s", bm_maps[k].alias);
            }
            chat_say("%s", list);
            chat_say("[AltBotMod] !map <name> <cq|cqsmall|cqas1|rush|sqrush|tdm|sqdm|gm|dom|scav|tank|air|ctf> (admin)");
            return;
        }
        if (!chat_is_admin(who)) { chat_say("[AltBotMod] %s: only admins can do that", who); return; }
        char alias[64],requested[64]="cq";sscanf(arg,"%63s %63s",alias,requested);
        const BMMap *map=bm_map(alias);const char *code=map?map->code:NULL;
        if(map&&!bm_map_mode(map,requested)){chat_say("[AltBotMod] map/mode combination unavailable");return;}
        /* validated stock/DLC map */   // a raw map code (installed packs only)
        if (!code) { chat_say("[AltBotMod] unknown map '%s' - type !map for the list", arg); return; }
        chat_map(code,who,requested);
    } else if (!_stricmp(cmd, "bots") && ((arg[0] >= '0' && arg[0] <= '9') || !_stricmp(arg, "fill"))) {
        if (!chat_is_admin(who)) { chat_say("[AltBotMod] %s: only admins can do that", who); return; }
        int n = !_stricmp(arg, "fill") ? MAX_BOTS : atoi(arg);
        if (n > MAX_BOTS) { chat_say("[AltBotMod] max %d bots - using %d", MAX_BOTS, MAX_BOTS); n = MAX_BOTS; }
        *wanted_bots() = n;
        g_rosterAuto = 1;
        save_bot_count();
        int target[5], humans[5];
        roster_targets(target, humans);
        chat_say("[AltBotMod] %s set the bot count to %d: US %d, RU %d bots (%d free slots per team kept for players)", who, n,
                 target[1], target[2], HUMAN_SLOTS_PER_TEAM);
    } else {
        chat_say("[AltBotMod] unknown command %s - try !help", text);
    }
}


// ---- Gunners: a bot in a vehicle seat other than the driver's aims that seat's weapon and shoots - enemy vehicles
// first, then spotted infantry. Seat input: traverse EIA 6 (a negative level turns towards larger atan2(x, z)),
// elevation EIA 5 (positive = up). The seat's weapon is found by nudging the traverse after boarding and seeing
// which nearby vehicle weapon turned; its facing is read back every frame for closed-loop aiming. ----
// AltBotMod V30: a helicopter's door guns and a Little Bird's bench reach DoorGunMeters (default 600 m) - V5-V29 gave them
// a jeep's 220 m at soldiers, which from 100-200 m up left almost nothing in range
static float gun_range_for(char *veh, int infantry)
{
    if (vehicle_is_gunship(veh)) return 1600.0f;   // (V23: the gunship's guns reach from its orbit)
    if (bm_air_profile(vehicle_name(veh))) return g_enhanced.doorGunRange;
    return bm_gun_range(&g_enhanced, bm_vehicle_profile(vehicle_name(veh)), infantry);
}
#define GUN_VEHICLE_RANGE      gun_range_for(g_gunVeh[i], 0)
#define GUN_INFANTRY_RANGE     gun_range_for(g_gunVeh[i], 1)
#define GUN_BURST_ON           54   // AltBotMod V22: 1.8 s bursts, 0.2 s breaks (V5-V21.1: 1.0 / 0.4 s) - as fast as the gun fires
#define GUN_BURST_OFF          6
static char *g_gunVeh[MAX_BOTS], *g_gunWeapon[MAX_BOTS], *g_gunTarget[MAX_BOTS], *g_gunTargetVeh[MAX_BOTS];
static unsigned char g_gunFire[MAX_BOTS], g_gunTries[MAX_BOTS], g_gunNudging[MAX_BOTS];
static char *g_gunCand[MAX_BOTS][16];
static char *g_gunBad[MAX_BOTS][4];
static float g_gunCandFwd[MAX_BOTS][16][3];
static unsigned char g_gunCandLocal[MAX_BOTS];
static BMAirMotion g_gunAirMotion[MAX_BOTS];
static void *g_gunAirWho[MAX_BOTS];
// AltBotMod V22: the gunner's own platform (its shots carry the vehicle's speed - InheritWeaponSpeedAmount), the target's
// acceleration (a turning aircraft), and the weapon slot in use (Fun-Bots' VehicleAttacking choice per seat and target)
static BMAirMotion g_gunOwnMotion[MAX_BOTS];
static float g_gunPrevVel[MAX_BOTS][3], g_gunAccel[MAX_BOTS][3];
static unsigned char g_gunSlot[MAX_BOTS], g_gunSlotWant[MAX_BOTS];
static unsigned g_gunSlotAt[MAX_BOTS], g_gunSlotCycle[MAX_BOTS], g_gunSlotSwitches;
static char *g_gunNoMissiles[MAX_BOTS];   // AltBotMod V29: the AA vehicle whose weapon 2 never showed a locking controller
static int g_gunCandN[MAX_BOTS], g_gunCalibBot = -1;
static unsigned g_gunStepAt[MAX_BOTS], g_gunScanAt[MAX_BOTS], g_gunBurstAt[MAX_BOTS];
static float g_gunLastYaw[MAX_BOTS];
static unsigned g_gunCheckAt[MAX_BOTS], g_gunStuck[MAX_BOTS];
static int g_gunDebug;   // console: gundebug 0/1

static int gun_ok(char *w) { return w && !IsBadReadPtr(w, 0x70) && *(unsigned *)w == ADDR_VEHICLE_WEAPON_VTABLE; }
static char *g_gunArcSkip[MAX_BOTS]; static unsigned g_gunArcSkipUntil[MAX_BOTS]; static unsigned char g_gunTurned[MAX_BOTS]; static unsigned g_gunArcLimits;
static void gunner_reset(int i)
{
    g_gunTurned[i] = 0; g_gunArcSkip[i] = NULL;   // AltBotMod V30
    memset(&g_gunAirMotion[i], 0, sizeof g_gunAirMotion[i]); g_gunAirWho[i] = NULL;
    memset(&g_gunOwnMotion[i], 0, sizeof g_gunOwnMotion[i]); memset(g_gunPrevVel[i], 0, 12); memset(g_gunAccel[i], 0, 12);
    g_gunSlot[i] = g_gunSlotWant[i] = 0; g_gunSlotAt[i] = g_gunSlotCycle[i] = 0;
    if (g_gunCalibBot == i) g_gunCalibBot = -1;
    g_gunVeh[i] = g_gunWeapon[i] = g_gunTarget[i] = g_gunTargetVeh[i] = NULL;
    g_gunFire[i] = g_gunTries[i] = g_gunNudging[i] = 0;
    g_seatYaw[i] = g_seatPitch[i] = 0;
    g_gunLastYaw[i] = 0; g_gunCheckAt[i] = g_gunStuck[i] = 0;
}
static char *vehicle_occupant(char *veh)
{
    char **b;
    int n = vehicle_seats(veh, &b);
    for (int k = 0; k < n; k++) if (*(char **)(b[k] + 0x4c)) return *(char **)(b[k] + 0x4c);
    return NULL;
}
// clear line from the gun (starting a few metres out, past our own hull) to the aim point
static void *g_gunSeeOwnBody;   // AltBotMod V23: the gunner's own vehicle - its hull is not "in the way"
static int gun_can_see(const float *gp, const float *aim, int isVehicle)
{
    float dx = aim[0] - gp[0], dy = aim[1] - gp[1], dz = aim[2] - gp[2], d = sqrtf(dx * dx + dy * dy + dz * dz);
    if (d < 4.0f) return 1;
    float ux = dx / d, uy = dy / d, uz = dz / d, from[3] = {gp[0] + ux * 3.5f, gp[1] + uy * 3.5f, gp[2] + uz * 3.5f}, hit[3];
    // AltBotMod V23: a ray that meets the gunner's own hull first (a soldier close in front of a tank, below the turret)
    // starts again just past it - V5-V22 counted the own front plate as cover, and tanks never shot a man standing there
    for (int tries = 0; tries < 4; tries++) {
        if (!ray_blocked_char(from, aim, hit)) return 1;
        if (!g_gunSeeOwnBody || *(void **)g_lastHitRest != g_gunSeeOwnBody) break;
        for (int k = 0; k < 3; k++) from[k] = hit[k] + (k == 0 ? ux : k == 1 ? uy : uz) * 0.4f;
        if ((aim[0] - from[0]) * ux + (aim[1] - from[1]) * uy + (aim[2] - from[2]) * uz <= 0.5f) return 1;
    }
    {
    float hx = hit[0] - aim[0], hy = hit[1] - aim[1], hz = hit[2] - aim[2];
    return isVehicle && hx * hx + hy * hy + hz * hz < 4.0f * 4.0f;   // hit the target's hull
    }
}
static unsigned char g_gunMoving[MAX_BOTS]; static float g_gunNudgeYaw[MAX_BOTS];   // AltBotMod V16: calibration on the move
static float hull_yaw(char *veh) { float *x = vehicle_xf(veh); return bm_air_transform_valid(x) ? atan2f(x[8], x[10]) : 0; }
static int gunner_find_weapon(int i, char *veh, const float *vp)
{
    // (V28: a seat with no weapon found is searched again every 20 s - V5-V27 gave up for good after 4 tries, and half the
    // tanks' roof guns and the helicopters' door guns were never manned: 182 "found no weapon" in three rounds)
    if (H->frame < g_gunStepAt[i]) return 0;
    if (g_gunTries[i] >= 4) g_gunTries[i] = 0;
    if (!g_gunNudging[i]) {
        if (g_gunCalibBot >= 0 && g_gunCalibBot != i) return 0;   // one nudge at a time, so the answer is unambiguous
        CompCache *cc = &g_compCache[g_compCur];
        int n = 0;
        float *hull = vehicle_xf(veh);
        g_gunCandLocal[i] = (unsigned char)bm_air_transform_valid(hull);
        for (int k = 0; k < cc->gcount && n < 16; k++) {
            char *w = cc->gun[k];
            if (!gun_ok(w)) continue;
            float *wp = (float *)(w + 0x60);
            float reach = vehicle_is_gunship(veh) ? 35.0f : g_gunMoving[i] ? 6.0f : 12.0f;   // (V23: the AC-130's guns sit far from its middle)
            if (dist2(wp, vp) > reach * reach || fabsf(wp[1] - vp[1]) > 8.0f) continue;
            int taken = 0;
            for (int j = 0; j < H->botCount; j++) if (j != i && g_gunWeapon[j] == w) taken = 1;
            for (int b = 0; b < 4; b++) if (g_gunBad[i][b] == w) taken = 1;
            if (taken) continue;
            g_gunCand[i][n] = w;
            if (g_gunCandLocal[i]) {
                float *f = (float *)(w + 0x50);
                for (int axis = 0; axis < 3; axis++)
                    g_gunCandFwd[i][n][axis] = f[0]*hull[axis*4]+f[1]*hull[axis*4+1]+f[2]*hull[axis*4+2];
            } else memcpy(g_gunCandFwd[i][n], w + 0x50, 12);
            n++;
        }
        g_gunCandN[i] = n;
        if (!n) { g_gunTries[i]++; g_gunStepAt[i] = H->frame + (g_gunTries[i] >= 4 ? 20 * 30 : 60); return 0; }
        g_gunCalibBot = i;
        g_gunNudging[i] = 1; g_gunNudgeYaw[i] = hull_yaw(veh);
        g_seatYaw[i] = (g_gunTries[i] & 1) ? 0.8f : -0.8f;
        g_gunStepAt[i] = H->frame + 6;
        if (vehicle_is_aircraft(veh)) { g_seatYaw[i] = (g_gunTries[i] & 1) ? 1.0f : -1.0f; g_gunStepAt[i] = H->frame + 12; }   // (V29: a door gun slews slower - a longer, full nudge)
        return 0;
    }
    g_seatYaw[i] = 0;
    g_gunNudging[i] = 0;
    g_gunCalibBot = -1;
    if (g_gunMoving[i]) {   // V16: the hull turned during the nudge - the answer is unreliable; nudge again shortly (not a try)
        float turned = fabsf(hull_yaw(veh) - g_gunNudgeYaw[i]); if (turned > 3.14159265f) turned = 6.2831853f - turned;
        if (turned > 0.02f) { g_gunStepAt[i] = H->frame + 10; return 0; }
    }
    char *best = NULL;
    float bestD = 0.002f;
    float *hull = g_gunCandLocal[i] ? vehicle_xf(veh) : NULL;
    if (g_gunCandLocal[i] && !bm_air_transform_valid(hull)) { g_gunTries[i]++; return 0; }
    for (int k = 0; k < g_gunCandN[i]; k++) {
        char *w = g_gunCand[i][k];
        if (!gun_ok(w)) continue;
        float *f = (float *)(w + 0x50), local[3];
        if (g_gunCandLocal[i]) {
            for (int axis = 0; axis < 3; axis++) local[axis] = f[0]*hull[axis*4]+f[1]*hull[axis*4+1]+f[2]*hull[axis*4+2];
            f = local;
        }
        float dx = f[0] - g_gunCandFwd[i][k][0], dz = f[2] - g_gunCandFwd[i][k][2], d = dx * dx + dz * dz;
        if (d > bestD) { bestD = d; best = w; }
    }
    g_gunStepAt[i] = H->frame + 15;
    if (!best) {
        if (++g_gunTries[i] >= 4) { g_gunStepAt[i] = H->frame + 20 * 30; bot_log("gunner: %s found no weapon on its seat - looks again in 20 s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME); }
        return 0;
    }
    g_gunWeapon[i] = best; g_gunTurned[i] = 0;
    bot_log("gunner: %s mans weapon %p (%d candidates)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, best, g_gunCandN[i]);
    return 1;
}
// Lowest of a few aim heights above a soldier's feet the gun has a clear line to (low walls, sandbags, breakables in
// front of the target), or -1.
static float g_gunAimH[MAX_BOTS];
static float gun_clear_height(const float *gp, const float *tp)
{
    static const float hs[3] = {1.1f, 1.4f, 0.8f};   // torso first (0.55 went between a standing soldier's legs)
    for (int k = 0; k < 3; k++) {
        float aim[3] = {tp[0], tp[1] + hs[k], tp[2]};
        if (gun_can_see(gp, aim, 0)) return hs[k];
    }
    return -1.0f;
}

static unsigned g_gunLostAt[MAX_BOTS];
static char *g_gunSeenFor[MAX_BOTS]; static unsigned g_gunSeenAt[MAX_BOTS]; static unsigned char g_gunSeenOk[MAX_BOTS];   // AltBotMod V31.1
static char *g_gunBad[MAX_BOTS][4];   // weapons that did not turn for this bot's seat
// current target still worth shooting: alive, and seen within the last 1.5 s
static int gunner_target_ok(int i, const float *gp)
{
    float *tp = NULL;
    int isVeh = 0;
    if (g_gunTargetVeh[i]) {
        char *tv = g_gunTargetVeh[i];
        if (!IsBadReadPtr(tv, 4) && *(unsigned *)tv == ADDR_VEHICLE_VTABLE && *(float *)(tv + OFF_VEHICLE_HEALTH) > 0 && vehicle_occupied(tv)) { tp = vehicle_pos(tv, NULL); isVeh = 1; }
    } else if (g_gunTarget[i] && player_exists(g_gunTarget[i]) && !player_vehicle(g_gunTarget[i])) {
        char *ts = *(char **)(g_gunTarget[i] + OFF_PLAYER_SOLDIER);
        if (ts && *(float *)(ts + OFF_SOLDIER_HEALTH) > 0) tp = (float *)(ts + OFF_SOLDIER_SPAWN_POSITION);
    }
    if (!tp) return 0;
    int team=player_team(H->bots[i]);
    if(isVeh){char *crew=vehicle_occupant(g_gunTargetVeh[i]);if(!crew || player_team(crew)==team)return 0;}
    else if(player_team(g_gunTarget[i])==team)return 0;
    float distance=sqrtf(dist2(tp,gp)+(tp[1]-gp[1])*(tp[1]-gp[1]));
    if(distance>gun_range_for(g_gunVeh[i],!isVeh))return 0;
    // AltBotMod V31.1: the line of sight to the current target is traced every 6th frame (staggered per bot), not every
    // frame - with 58 bots on Gulf of Oman the gunners' rays took up to 19 ms of a 33 ms server tick (V31 live: the server
    // fell to 23 ticks/s, players rubber-banded and lost hits)
    char *who = isVeh ? g_gunTargetVeh[i] : g_gunTarget[i];
    int seen;
    if (who == g_gunSeenFor[i] && g_gunSeenAt[i] && H->frame - g_gunSeenAt[i] < 6) seen = g_gunSeenOk[i];
    else {
        if (isVeh) { float aim[3] = {tp[0], tp[1] + 1.0f, tp[2]}; seen = gun_can_see(gp, aim, 1); }
        else { float h = gun_clear_height(gp, tp); seen = h > 0; if (seen) g_gunAimH[i] = h; }
        g_gunSeenFor[i] = who; g_gunSeenAt[i] = H->frame ? H->frame : 1; g_gunSeenOk[i] = (unsigned char)seen;
    }
    if (seen) { g_gunLostAt[i] = 0; return 1; }
    if (!g_gunLostAt[i]) g_gunLostAt[i] = H->frame;
    return H->frame - g_gunLostAt[i] < 45;
}
#define GUN_IS_AIRCRAFT(v) vehicle_is_aircraft(v)   // AltBotMod V23: the gunship is an aircraft target
// AltBotMod V30: a gun that has turned for this bot and then stops turning is at the end of its arc (a door gun, a bench
// seat, an emplacement): the target is out of its reach - another is picked for 4 s; the gun stays manned. V28-V29 took
// that for "not this seat's gun" and dropped it ("does not turn - looking for its real one"): door guns went quiet.
#define GUN_ARC_SKIPPED(i,p) ((p)==g_gunArcSkip[i] && H->frame<g_gunArcSkipUntil[i])
#define GUN_SEEN_RECENTLY(i,who) ((who)==g_gunSeenFor[i] && g_gunSeenAt[i] && H->frame-g_gunSeenAt[i]<6 ? (int)g_gunSeenOk[i] : -1)   // AltBotMod V31.1
#include "gunner_targets.inc"
static void gunner_tick_inner(int i);
static void gunner_tick(int i)
{
    char *v = player_vehicle(H->bots[i]);
    g_gunSeeOwnBody = v && !IsBadReadPtr(v + 0x30, 4) ? *(void **)(v + 0x30) : NULL;
    gunner_tick_inner(i);
    g_gunSeeOwnBody = NULL;
}
static void gunner_tick_inner(int i)
{
    char *pl = H->bots[i], *veh = player_vehicle(pl);
    int seat = veh && !IsBadReadPtr(veh, 4) && *(unsigned *)veh == ADDR_VEHICLE_VTABLE ? vehicle_seat_of(veh, pl) : -1;
    int driverWeapon = seat == 0 && (enhanced_driver_weapon(veh) || stationary_weapon(veh));   // V5: emplacements too
    const BMAirProfile *airframe = bm_air_profile(vehicle_name(veh));
    // (V28: every seat of an aircraft but the pilot's looks for a gun - a transport helicopter's door miniguns too; a seat
    // with none simply finds nothing)
    if (air_pilot(i) || (airframe && (!g_enhanced.air.weapons || seat <= 0 ||
        !bm_air_transform_valid(vehicle_xf(veh))))) {
        if (g_gunVeh[i]) gunner_reset(i);
        return;
    }
    if (seat < 0 || (seat == 0 && !driverWeapon) || !g_vehInput) { if (g_gunVeh[i]) gunner_reset(i); return; }
    if (veh != g_gunVeh[i]) { gunner_reset(i); g_gunVeh[i] = veh; g_gunStepAt[i] = H->frame + 30; memset(g_gunBad[i], 0, sizeof g_gunBad[i]); }
    float *vp = vehicle_pos(veh, NULL);
    if (!vp) return;
    char *w = g_gunWeapon[i];
    if (w && !gun_ok(w)) { g_gunWeapon[i] = w = NULL; g_gunTries[i] = 0; }
    // Weapon discovery compares local turret orientation to the hull. It
    // never takes ownership of the throttle or waits for other calibrations.
    // AltBotMod V16: the main gun is found while driving too. V6-V15 waited for a halt, and a tank that never stopped
    // never fired - "tanks ignore enemies" (2026-10-13). Moving, the nudge only counts when the hull held its heading
    // through it, and only guns on our own turret (within 6 m) are candidates.
    {VehTrack *track=driverWeapon && !w && g_drive[i].phase?vehicle_sample(veh):NULL;
     g_gunMoving[i]=(unsigned char)((track && track->speed>0.5f) || (driverWeapon && !w && g_drive[i].phase && g_drive[i].speed>0.5f));}
    if (!w) { g_gunFire[i] = 0; gunner_find_weapon(i, veh, vp); return; }
    float *gp = (float *)(w + 0x60), *f = (float *)(w + 0x50);
    {   // the weapon must follow our traverse input; if it does not, it is not this seat's - drop it and look again
        if (H->frame >= g_gunCheckAt[i]) {
            float y = atan2f(f[0], f[2]), moved = fabsf(y - g_gunLastYaw[i]);
            if (moved > 3.14159265f) moved = 6.2831853f - moved;
            if (fabsf(g_seatYaw[i]) >= 0.35f && g_gunCheckAt[i]) g_gunStuck[i] = moved < 0.01f ? g_gunStuck[i] + 1 : 0;
            else g_gunStuck[i] = 0;
            if (fabsf(g_seatYaw[i]) >= 0.35f && g_gunCheckAt[i] && moved >= 0.01f) g_gunTurned[i] = 1;   // (V30: it is this seat's gun)
            g_gunLastYaw[i] = y;
            g_gunCheckAt[i] = H->frame + 20;
            if (g_gunStuck[i] >= 3 && g_gunTurned[i]) {   // V30: at the end of its arc - another target, same gun
                g_gunStuck[i] = 0; g_gunArcLimits++;
                g_gunArcSkip[i] = g_gunTargetVeh[i] ? g_gunTargetVeh[i] : g_gunTarget[i]; g_gunArcSkipUntil[i] = H->frame + 4 * 30;
                if (g_gunArcLimits <= 5 || g_gunArcLimits % 50 == 0)
                    bot_log("gunner: %s's gun on the %s is at the end of its arc - another target for 4 s (%u so far)", pl + OFF_PLAYER_ONLINE_NAME, vehicle_name(veh), g_gunArcLimits);
                g_gunTarget[i] = g_gunTargetVeh[i] = NULL; g_gunFire[i] = 0; g_seatYaw[i] = g_seatPitch[i] = 0; g_gunScanAt[i] = H->frame;
                return;
            }
            if (g_gunStuck[i] >= 3) {
                g_gunStuck[i] = 0;
                for (int b = 0; b < 4; b++) if (!g_gunBad[i][b] || b == 3) { g_gunBad[i][b] = w; break; }
                bot_log("gunner: %s's weapon %p does not turn - looking for its real one", pl + OFF_PLAYER_ONLINE_NAME, w);
                g_gunWeapon[i] = NULL; g_gunTries[i] = 0; g_seatYaw[i] = g_seatPitch[i] = 0; g_gunFire[i] = 0;
                return;
            }
        }
    }
    if (H->frame >= g_gunScanAt[i]) { g_gunScanAt[i] = H->frame + 12 + (unsigned)(i % 4); gunner_pick_target(i, gp); }   // (V31.1: 0.4-0.5 s, staggered - V5-V31: every 1/3 s)
    if(!gunner_target_ok(i,gp)){g_gunTarget[i]=g_gunTargetVeh[i]=NULL;g_gunFire[i]=0;g_seatYaw[i]=g_seatPitch[i]=0;return;}
    float *tp = NULL;
    int isVeh = 0;
    if (g_gunTargetVeh[i]) {
        char *tv = g_gunTargetVeh[i];
        if (!IsBadReadPtr(tv, 4) && *(unsigned *)tv == ADDR_VEHICLE_VTABLE && *(float *)(tv + OFF_VEHICLE_HEALTH) > 0) { tp = vehicle_pos(tv, NULL); isVeh = 1; }
    } else if (g_gunTarget[i] && player_exists(g_gunTarget[i])) {
        char *ts = *(char **)(g_gunTarget[i] + OFF_PLAYER_SOLDIER);
        if (ts && *(float *)(ts + OFF_SOLDIER_HEALTH) > 0 && !player_vehicle(g_gunTarget[i])) tp = (float *)(ts + OFF_SOLDIER_SPAWN_POSITION);
    }
    if (!tp) { g_seatYaw[i] = g_seatPitch[i] = 0; g_gunFire[i] = 0; g_gunTarget[i] = g_gunTargetVeh[i] = NULL; return; }
    float aim[3] = {tp[0], tp[1] + (isVeh ? 1.0f : g_gunAimH[i] > 0 ? g_gunAimH[i] : FIGHT_AIM_HEIGHT), tp[2]};   // infantry: lowest clear point
    float sight[3];memcpy(sight,aim,12);
    int airLeadOk = 1;
    const BMVehicleProfile *vprofile = bm_vehicle_profile(vehicle_name(veh));
    // AltBotMod V22: the weapon for this seat and target, as Fun-Bots picks it (VehicleAttacking.lua): a tank's coax on
    // soldiers, an IFV's missile on armour (4 s in 12), an attack helicopter gunner's guided missile on vehicles.
    int slot = 1, guided = stationary_weapon(veh) == 1;
    {
        const BMVehicleProfile *tprof = isVeh ? bm_vehicle_profile(vehicle_name(g_gunTargetVeh[i])) : NULL;
        int armour = tprof && (tprof->kind == BM_TANK || tprof->kind == BM_IFV || tprof->kind == BM_AA || tprof->kind == BM_AMTRAC);
        int targetAir = isVeh && bm_air_profile(vehicle_name(g_gunTargetVeh[i])) != NULL;
        float td = sqrtf(dist2(tp, gp));
        if (driverWeapon && vprofile->kind == BM_TANK && !isVeh && td < 150.0f) slot = 2;
        else if (driverWeapon && vprofile->kind == BM_IFV && armour) {
            if (!g_gunSlotCycle[i] || H->frame - g_gunSlotCycle[i] > 12 * 30) g_gunSlotCycle[i] = H->frame ? H->frame : 1;
            if (H->frame - g_gunSlotCycle[i] < 4 * 30) { slot = 2; guided = 1; }
        } else if (airframe && airframe->kind == BM_AIR_ATTACK && seat == 1 && isVeh && !targetAir) { slot = 2; guided = 1; }
        // AltBotMod V29: an AA vehicle's missiles at aircraft - 4 s in 12 on weapon 2 (the lock is put in place, lock_fake);
        // a vehicle whose weapon 2 shows no locking controller in its first window is not tried again (no missiles fitted)
        else if (driverWeapon && vprofile->kind == BM_AA && targetAir && g_gunNoMissiles[i] != veh) {
            if (!g_gunSlotCycle[i] || H->frame - g_gunSlotCycle[i] > 12 * 30) g_gunSlotCycle[i] = H->frame ? H->frame : 1;
            if (H->frame - g_gunSlotCycle[i] < 4 * 30) { slot = 2; guided = 1; }
            if (H->frame - g_gunSlotCycle[i] >= 4 * 30 - 15 && H->frame - g_gunSlotCycle[i] < 4 * 30 && (!g_lockCtrl[i] || H->frame - g_lockCtrlAt[i] > 90)) {
                g_gunNoMissiles[i] = veh;
                bot_log("gunner: %s's %s shows no missile lock on weapon 2 - stays on the cannon", pl + OFF_PLAYER_ONLINE_NAME, vehicle_name(veh));
            }
        }
        if (!g_gunSlot[i]) g_gunSlot[i] = 1;
        if (slot != g_gunSlot[i] && (!g_gunSlotAt[i] || H->frame - g_gunSlotAt[i] > 30)) {
            press_button(i, slot == 2 ? EIA_SELECT_WEAPON1 + 1 : EIA_SELECT_WEAPON1, 3);
            g_gunSlot[i] = (unsigned char)slot; g_gunSlotAt[i] = H->frame ? H->frame : 1; g_gunSlotSwitches++;
            if (g_gunSlotSwitches % 20 == 1)
                bot_log("gunner: %s switches to weapon %d of the %s (%s; %u switches so far)", pl + OFF_PLAYER_ONLINE_NAME, slot, vehicle_name(veh),
                        slot == 1 ? "main weapon" : guided ? "guided missile at armour" : "machine gun at soldiers", g_gunSlotSwitches);
        }
        if (g_gunSlot[i] != slot) slot = g_gunSlot[i];   // (still switching)
        if (slot == 1 && stationary_weapon(veh) != 1) guided = 0;
    }
    {   // lead a moving target
        float lp[3], ddx = tp[0] - gp[0], ddz = tp[2] - gp[2];
        const BMVehicleProfile *profile = vprofile;
        float distance = sqrtf(ddx * ddx + ddz * ddz);
        int cannon=(driverWeapon && slot == 1) || (profile->kind==BM_AMTRAC && seat==1);
        float speed = airframe && seat > 0 ? (airframe->kind == BM_AIR_TRANSPORT ? 900.0f : 600.0f)
                    : cannon && g_enhanced.ballisticAim ? profile->muzzleSpeed : BULLET_SPEED;
        void *who = isVeh ? (void *)g_gunTargetVeh[i] : (void *)g_gunTarget[i];
        // AltBotMod V22: the shots leave with the platform's own speed - lead by the target's velocity relative to ours
        // (a helicopter gunner circling a target, a tank firing on the move), and, for aircraft, by how they turn.
        bm_air_motion(&g_gunOwnMotion[i], vp, BOT_TICK_DT);
        float own[3] = {0, 0, 0};
        if (g_gunOwnMotion[i].ready) memcpy(own, g_gunOwnMotion[i].velocity, 12);
        if (guided) {
            // Caspian V5 (review): a TOW / Kornet missile is wire-guided - it flies to wherever the sight is held,
            // so the sight stays on the target with no lead (leading a moving tank made the missile miss ahead).
            // V22: the same for an IFV's missile and a gunship gunner's TV / guided missile.
        } else if (airframe || profile->kind==BM_AA) {
            if (g_gunAirWho[i] != who) { memset(&g_gunAirMotion[i], 0, sizeof g_gunAirMotion[i]); memset(g_gunAccel[i], 0, 12); g_gunAirWho[i] = who; }
            bm_air_motion(&g_gunAirMotion[i], tp, BOT_TICK_DT);
            float rel[3], t = 0;
            if (g_gunAirMotion[i].ready) {
                for (int k = 0; k < 3; k++) {
                    float a = (g_gunAirMotion[i].velocity[k] - g_gunPrevVel[i][k]) / BOT_TICK_DT;
                    if (!isfinite(a) || fabsf(a) > 60.0f) a = 0;
                    g_gunAccel[i][k] = 0.9f * g_gunAccel[i][k] + 0.1f * a;
                }
                memcpy(g_gunPrevVel[i], g_gunAirMotion[i].velocity, 12);
            }
            for (int k = 0; k < 3; k++) rel[k] = g_gunAirMotion[i].velocity[k] - own[k];
            airLeadOk = bm_air_intercept(gp, aim, rel, speed, 0, lp, &t);
            if (airLeadOk) {
                memcpy(aim, lp, 12);
                if (isVeh) for (int k = 0; k < 3; k++) aim[k] += 0.5f * fmaxf(-25.0f, fminf(25.0f, g_gunAccel[i][k])) * t * t;
            }
        } else {
            lead_point_speed(i, who, tp, distance, speed, lp);
            float t = distance / fmaxf(1.0f, speed);
            aim[0] = lp[0] - own[0] * t; aim[2] = lp[2] - own[2] * t;
        }
        if (cannon && g_enhanced.ballisticAim && profile->gravity>0) {
            float flight, rise;
            airLeadOk=bm_ground_ballistics(speed,profile->gravity,distance,aim[1]-gp[1],&flight,&rise);
            if(airLeadOk)aim[1]+=rise;
        }
    }
    float dx = aim[0] - gp[0], dy = aim[1] - gp[1], dz = aim[2] - gp[2], dh = sqrtf(dx * dx + dz * dz);
    float err = atan2f(dx, dz) - atan2f(f[0], f[2]);
    while (err > 3.14159265f) err -= 6.2831853f;
    while (err < -3.14159265f) err += 6.2831853f;
    float perr = atan2f(dy, dh) - asinf(fmaxf(-1.0f, fminf(1.0f, f[1])));
    // traverse: negative level raises atan2 yaw; a small floor so the last degrees still move
    float ya = fabsf(err) < 0.0015f ? 0.0f : fmaxf(0.025f, fminf(1.0f, fabsf(err) * 5.0f));
    g_seatYaw[i] = err > 0 ? -ya : ya;
    float pa = fabsf(perr) < 0.0015f ? 0.0f : fmaxf(0.025f, fminf(1.0f, fabsf(perr) * 5.0f));
    g_seatPitch[i] = perr > 0 ? pa : -pa;
    float tolerance=fmaxf(0.0025f,fminf(0.045f,atanf((isVeh?2.0f:0.35f)/fmaxf(1,dh))));
    if (slot == 2 && !guided) tolerance = fmaxf(tolerance, atanf(0.6f / fmaxf(1, dh)));   // (a coax machine gun: the spread does the rest)
    // AltBotMod V29: a helicopter's door / passenger gun fires once it is within 3 m of the man (5 m of a vehicle), at least
    // 1.7 deg - a minigun's spread does the rest. V22-V28 asked a gun on a swaying, moving helicopter for 0.35 m at the
    // target (0.2 deg at 100 m): the door gunners aimed and never fired.
    if (airframe && seat > 0 && !guided) tolerance = fmaxf(tolerance, fmaxf(0.03f, atanf((isVeh ? 5.0f : 3.0f) / fmaxf(1, dh))));
    int onTarget = airLeadOk && fabsf(err)<tolerance && fabsf(perr)<tolerance && gun_can_see(gp,sight,isVeh);
    if (driverWeapon && slot == 1 && vprofile->kind == BM_TANK && dh < 6.0f) onTarget = 0;   // V23: only a tank's main gun right on top of it (V5-V22: no driver gun under 15 m - a man in front of a tank was never shot; the coax fires at any range)
    unsigned ph = (H->frame - g_gunBurstAt[i]) % (GUN_BURST_ON + GUN_BURST_OFF);
    g_gunFire[i] = (unsigned char)(onTarget && ph < GUN_BURST_ON);
    if (guided && onTarget) g_gunFire[i] = (unsigned char)(ph < 4);   // V22: a guided missile - one press a burst cycle, then the sight steers it
    // AltBotMod V25: a lock-on weapon in this seat (an AA vehicle's missiles, a gunship's): the game's own lock is asked for
    // while the sight is near the target (on_lock_update) and fired the moment the game reports it
    if (fabsf(err) < 0.12f && fabsf(perr) < 0.12f && gun_can_see(gp, sight, isVeh)) lock_want_ent(i, gp, aim, 10.0f, isVeh && fabsf(err) < 0.06f && fabsf(perr) < 0.06f ? g_gunTargetVeh[i] : NULL);   // (V29: the lock is put in place)
    if (g_lockHooked && lock_state(i, NULL) == 2) g_gunFire[i] = 1;
    if (g_gunDebug && H->frame % 30 == 0)
        bot_log("gunner: %s aim err yaw %.3f pitch %.3f (want %.3f have %.3f) dist %.0f fire %d levels %.2f %.2f", pl + OFF_PLAYER_ONLINE_NAME,
                err, perr, atan2f(dy, dh), asinf(fmaxf(-1.0f, fminf(1.0f, f[1]))), dh, g_gunFire[i], g_seatYaw[i], g_seatPitch[i]);
}

// ---- Performance counters: time spent per part of the tick, logged every 30 s (console: perf 0/1) ----
enum { PF_INPUT, PF_LEVEL, PF_ROSTER, PF_SQUAD, PF_DOWNED, PF_NAV, PF_BRAIN, PF_PATH, PF_DETECT, PF_STEP, PF_TOTAL, PF_COUNT };
static const char *kPerfNames[PF_COUNT] = {"input+steer+gunner", "level", "roster", "squads", "downed", "nav", "brain", "(of which paths)", "detect", "soldier step", "total"};
static double g_perfMs[PF_COUNT], g_perfMax[PF_COUNT];
static volatile double g_scanMs, g_scanMaxMs;
static volatile LONG g_scanPasses;
static int g_perfOn = 1;
static double perf_now(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER t;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)f.QuadPart;
}
static void perf_add(int k, double ms) { g_perfMs[k] += ms; if (ms > g_perfMax[k]) g_perfMax[k] = ms; }
static double g_subMs[4], g_subMax[4];   // AltBotMod V31.1: the input bucket split - driving, aircraft, gunners, the rest of a bot's motion
static void perf_report(void)
{
    static unsigned lastFrame;
    static double lastWall;
    double now = perf_now();
    if (!lastWall) { lastWall = now; lastFrame = H->frame; return; }
    if (now - lastWall < 30000.0) return;
    unsigned frames = H->frame - lastFrame;
    if (g_perfOn && frames) {
        char line[600];
        int n = snprintf(line, sizeof line, "perf: %u bots, %.1f ticks/s; per tick avg/max ms:", H->botCount, frames * 1000.0 / (now - lastWall));
        for (int k = 0; k < PF_COUNT; k++) n += snprintf(line + n, sizeof line - n, " %s %.2f/%.1f", kPerfNames[k], g_perfMs[k] / frames, g_perfMax[k]);
        n += snprintf(line + n, sizeof line - n, " | %u path plans (%u deferred, %d queued) | %u hits on bots traced to a shooter | %u bot-s swimming (%u this level)",
                      g_pathPlans, g_planBudgetHits, nav_pending_paths(), g_attackerNoted, g_swimSeconds, g_swimSecondsLevel);
        n += snprintf(line + n, sizeof line - n, " | scan worker: %ld passes, avg %.0f ms, max %.0f ms", (long)g_scanPasses,
                      g_scanPasses ? g_scanMs / g_scanPasses : 0.0, g_scanMaxMs);
        if (frames) n += snprintf(line + n, sizeof line - n, " | input split avg/max ms: driving %.2f/%.1f aircraft %.2f/%.1f gunners %.2f/%.1f",
                                  g_subMs[0] / frames, g_subMax[0], g_subMs[1] / frames, g_subMax[1], g_subMs[2] / frames, g_subMax[2]);
        bot_log("%s", line);
        if (g_aimImpacts)
            bot_log("aim: %u bot bullets on the line to their target, %.0f%% within the torso, crossing it avg %.2f m above its feet, %.2f m to the side; "
                    "average correction %.2f m + %.1f mrad down + %.2f mrad per frame of a burst, %.1f mrad sideways (%u samples so far); pitch input %.1f mrad/frame",
                    g_aimImpacts, 100.0f * g_aimTorso / g_aimImpacts, g_aimRiseSum / g_aimImpacts, g_aimSideSum / g_aimImpacts,
                    g_aimFixHAvg, g_aimFixVAvg * 1000, g_aimClimbAvg * 1000, g_aimFixSAvg * 1000, g_aimSamples[0] + g_aimSamples[1] + g_aimSamples[2], g_pitchRate * 1000);
        g_aimImpacts = g_aimOnBody = g_aimTorso = 0; g_aimSideSum = g_aimRiseSum = 0;
    }
    memset(g_perfMs, 0, sizeof g_perfMs); memset(g_perfMax, 0, sizeof g_perfMax); memset(g_subMs, 0, sizeof g_subMs); memset(g_subMax, 0, sizeof g_subMax);
    g_scanMs = 0; g_scanMaxMs = 0; g_scanPasses = 0; g_pathPlans = 0; g_planBudgetHits = 0; g_attackerNoted = 0; g_swimSeconds = 0;
    lastWall = now; lastFrame = H->frame;
}

// ---- Falling: a bot in free fall (bailed out of an aircraft, off a cliff, a tower or a ladder top) pulls its chute.
// Vertical speed from the position every frame; past FALL_CHUTE_SPEED it presses the chute key, and again every
// 0.5 s while still falling that fast - until the descent slows to chute speed, it lands, or it dies. ----
#define FALL_CHUTE_SPEED       20.0f    // m/s down: free fall reaches 40+, an open chute comes down at ~13 (jumps and ledges never get there)
static float g_fallLastY[MAX_BOTS], g_fallVy[MAX_BOTS];
static char *g_fallSoldier[MAX_BOTS];
static unsigned g_fallPressAt[MAX_BOTS];
static float g_fallPressVy[MAX_BOTS];
static int g_fallLandedFrames[MAX_BOTS];
static unsigned char g_chuteOpen[MAX_BOTS];
extern unsigned char g_gliding[MAX_BOTS];
static void fall_tick(int i)
{
    char *pl = H->bots[i], *s = *(char **)(pl + OFF_PLAYER_SOLDIER);
    if (!s || player_vehicle(pl)) { g_fallSoldier[i] = NULL; g_fallPressAt[i] = 0; g_gliding[i] = 0; g_chuteOpen[i] = 0; return; }
    float y = ((float *)(s + OFF_SOLDIER_POSITION))[1];
    if (g_fallSoldier[i] != s) { g_fallSoldier[i] = s; g_fallLastY[i] = y; g_fallVy[i] = 0; return; }
    float vy = (y - g_fallLastY[i]) / BOT_TICK_DT;
    g_fallLastY[i] = y;
    g_fallVy[i] = 0.6f * g_fallVy[i] + 0.4f * vy;   // smoothed (positions arrive in small steps)
    // the key toggles the chute: after a press, only press again if 1.5 s later the bot is not slowing down
    int since = (int)(H->frame - g_fallPressAt[i]);
    if (g_fallPressAt[i] && g_fallVy[i] > g_fallPressVy[i] + 8.0f) g_chuteOpen[i] = 1;   // slowed right down: it is open
    int retry = g_fallPressAt[i] && since >= 45 && g_fallVy[i] <= g_fallPressVy[i] + 1.0f;   // pressed, still not slowing
    int first = !g_fallPressAt[i];
    if (g_chuteOpen[i]) first = retry = 0;   // never toggle an open chute shut
    // in the air: steer towards the nearest flag we do not own, else the nearest of ours, else the map centre
    if (!g_gliding[i] && g_fallVy[i] < -FALL_CHUTE_SPEED) { g_gliding[i] = 1; g_fallLandedFrames[i] = 0; }
    if (g_gliding[i]) {
        g_fallLandedFrames[i] = g_fallVy[i] > -1.0f ? g_fallLandedFrames[i] + 1 : 0;
        if (g_fallLandedFrames[i] >= 15) {   // on the ground again
            g_gliding[i] = 0;
            g_fallPressAt[i] = 0; g_chuteOpen[i] = 0;
            bot_nav(i)->mode = 1;
            bot_log("fall: %s landed - picking an objective from here", pl + OFF_PLAYER_ONLINE_NAME);
            end_task(i, bot_brain(i), "landed");
            choose_local_objective(i, "landed");
        } else if (g_flagCount) {
            float *me = (float *)(s + OFF_SOLDIER_POSITION), best = 1e30f, tx = 0, tz = 0;
            int team = player_team(pl), found = 0;
            for (int pass = 0; pass < 2 && !found; pass++)
                for (int f = 0; f < g_flagCount; f++) {
                    if ((pass == 0) == (flag_owner(f) == team)) continue;   // pass 0: not ours, pass 1: ours
                    float d = dist2(flag_pos(f), me);
                    if (d < best) { best = d; tx = flag_pos(f)[0]; tz = flag_pos(f)[2]; found = 1; }
                }
            if (!found) {
                for (int f = 0; f < g_flagCount; f++) { tx += flag_pos(f)[0]; tz += flag_pos(f)[2]; }
                tx /= g_flagCount; tz /= g_flagCount;
            }
            bot_nav(i)->mode = 4;   // face it and push forward (glides the chute that way)
            bot_nav(i)->x = tx;
            bot_nav(i)->z = tz;
        }
    }
    if (g_fallVy[i] < -FALL_CHUTE_SPEED && (first || retry)) {
        g_fallPressAt[i] = H->frame;
        g_fallPressVy[i] = g_fallVy[i];
        press_button(i, EIA_TOGGLE_PARACHUTE, 3);
        static unsigned logged;
        if (logged++ < 40) bot_log("fall: %s falling at %.0f m/s - parachute", pl + OFF_PLAYER_ONLINE_NAME, -g_fallVy[i]);
    }
}

// Adopt a human's deploy point as the team's HQ when it is not a flag spawn and puts soldiers closer to the takeable
// flags than the HQ the bots use now.
static void kill_bot(char *pl);
// Can soldiers of `team` appearing at sp be an HQ of theirs? Not the origin (spawns of other modes put them there),
// nearer their own HQ than the enemy's and within 900 m of it, and not inside the fighting (150 m from a flag
// they do not hold). Returns NULL if fine, else why not.
static const char *hq_spot_problem(int team, const float *sp)
{
    if (fabsf(sp[0]) < 1.0f && fabsf(sp[2]) < 1.0f) return "at the map origin (not a real spawn here)";
    int other = 3 - team;
    if (g_hqCpKnown[team]) {
        float dOwn = sqrtf(dist2(sp, g_hqCpPos[team])), dEnemy = g_hqCpKnown[other] ? sqrtf(dist2(sp, g_hqCpPos[other])) : 1e30f;
        if (dEnemy < dOwn) return "on the enemy's side";
        if (dOwn > 900.0f) return "too far from the team's HQ";
    }
    for (int f = 0; f < g_flagCount; f++) if (flag_owner(f) != team && dist2(flag_pos(f), sp) < 150.0f * 150.0f) return "inside the fighting";
    return NULL;
}
static void hq_probe_one(HqProbe *q);
static void hq_probe_tick(void) { for (int k = 0; k < MAX_HQ_PROBES; k++) hq_probe_one(&g_hqProbes[k]); }
static void hq_probe_one(HqProbe *q)
{ (void)q; /* Disabled: spawn probing killed new soldiers and reused their player ghosts. */ }

// ---- HQ memory: an HQ learned on a map (from a player's deploy or a probe) is saved with its position
// (BotMod_hq_<level>.txt: "team x y z") and picked straight away at the start of every later round - the spawn point
// whose data transform (+0x10, position at +0x40) is there. Wake's US island HQ from the first second. ----
#define ADDR_CSE_VTABLE 0x025bbbc0
static void hq_memory_file(char *out, int n)
{
    char dir[MAX_PATH];
    GetModuleFileNameA(NULL, dir, sizeof dir);
    *strrchr(dir, '\\') = 0;
    snprintf(out, n, "%s\\BotMod_hq_%s.txt", dir, g_level);
}
static void hq_memory_save(int team, const float *pos, unsigned a2, unsigned a3, unsigned a4)
{
    if (near_origin(pos)) { bot_log("level: not remembering team %d's HQ at the world origin (%.0f, %.0f)", team, pos[0], pos[2]); return; }
    char path[MAX_PATH + 64], lines[3][96] = {"", "", ""};
    hq_memory_file(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (f) { char l[96]; while (fgets(l, sizeof l, f)) { int t = atoi(l); if (t == 1 || t == 2) snprintf(lines[t], sizeof lines[t], "%s", l); } fclose(f); }
    // with the deploy arguments that spawn point was used with (another spawn's arguments crash the game)
    snprintf(lines[team], sizeof lines[team], "%d %.1f %.1f %.1f %u %u %u\n", team, pos[0], pos[1], pos[2], a2, a3, a4);
    if ((f = fopen(path, "w"))) { fputs(lines[1], f); fputs(lines[2], f); fclose(f); }
    bot_log("level: remembered team %d's HQ on %s at (%.0f, %.0f) [args %08x %u %u]", team, g_level, pos[0], pos[2], a2, a3, a4);
}
static float *spawn_data_pos(void *spawn)   // CharacterSpawnEntity data transform position (NULL if unreadable)
{
    char *data = spawn && !IsBadReadPtr((char *)spawn + 0x14, 4) ? *(char **)((char *)spawn + 0x14) : NULL;
    return data && !IsBadReadPtr(data + 0x40, 12) ? (float *)(data + 0x40) : NULL;
}
static void hq_memory_apply(void)
{
    char path[MAX_PATH + 64], l[96];
    hq_memory_file(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return;
    float want[3][3];
    unsigned args[3][3];
    int have[3] = {0, 0, 0};
    while (fgets(l, sizeof l, f)) {   // "team x y z a2 a3 a4" - a line without the arguments is not used
        int t; float x, y, z; unsigned a2, a3, a4;
        if (sscanf(l, "%d %f %f %f %u %u %u", &t, &x, &y, &z, &a2, &a3, &a4) != 7 || (t != 1 && t != 2)) continue;
        { float o[3] = {x, y, z}; if (near_origin(o)) { bot_log("level: ignoring a remembered team %d HQ at the world origin", t); continue; } }
        if (a2 != 0xffffffff && a2 >= 0x10000) continue;   // a pointer: only valid in the round it was seen in
        want[t][0] = x; want[t][1] = y; want[t][2] = z; args[t][0] = a2; args[t][1] = a3; args[t][2] = a4; have[t] = 1;
    }
    fclose(f);
    char *sp[160];
    int n = scan_objects(ADDR_CSE_VTABLE, sp, 160);
    for (int t = 1; t <= 2; t++) {
        if (!have[t]) continue;
        for (int k = 0; k < n; k++) {
            float *p = spawn_data_pos(sp[k]);
            if (!p || dist2(p, want[t]) > 15.0f * 15.0f || spawn_team(sp[k]) == 3 - t) continue;   // (HQ spawns carry no 1/2 team)
            H->teamSpawn[t].entity = sp[k];
            H->teamSpawn[t].a2 = args[t][0]; H->teamSpawn[t].a3 = args[t][1]; H->teamSpawn[t].a4 = args[t][2];
            memcpy(g_hqPos[t], want[t], 12);
            memset(g_hqCandTried[t], 1, sizeof g_hqCandTried[t]);   // already the best one found: nothing left to probe
            bot_log("level: team %d spawns at its remembered HQ (%.0f, %.0f)", t, want[t][0], want[t][2]);
            break;
        }
    }
}

static void hq_learn_tick(void)
{
    if (!mode_is_cq()) return;
    PendingHq *q = &g_pendingHq;
    if (!q->at || H->frame - q->at < 30 || !level_ready()) return;
    char *pl = q->player;
    void *ent = q->entity;
    q->at = 0;
    if (!player_exists(pl) || !g_flagCount) return;
    int team = player_team(pl);
    char *s = *(char **)(pl + OFF_PLAYER_SOLDIER);
    if ((team != 1 && team != 2) || !s || ent == H->teamSpawn[team].entity) return;
    if (IsBadReadPtr(ent, 4) || *(unsigned *)ent != 0x025bbbc0) return;   // a CharacterSpawnEntity (not a beacon / squad mate)
    for (int f = 0; f < g_flagCount; f++) if (g_flagSpawn[f][1] == ent || g_flagSpawn[f][2] == ent) return;   // a flag spawn
    float *sp = (float *)(s + OFF_SOLDIER_POSITION), dNew = 1e30f, dOld = 1e30f;
    const char *why = hq_spot_problem(team, sp);
    if (why) return;
    for (int f = 0; f < g_flagCount; f++) {
        float a = dist2(flag_pos(f), sp), b = dist2(flag_pos(f), g_hqPos[team]);
        if (a < dNew) dNew = a;
        if (b < dOld) dOld = b;
    }
    bot_log("level: %s deployed from spawn %p at (%.0f, %.0f) - %.0f m from the nearest flag (bot HQ: %.0f m)", pl + OFF_PLAYER_ONLINE_NAME,
            ent, sp[0], sp[2], sqrtf(dNew), sqrtf(dOld));
    if (H->teamSpawn[team].entity && dNew >= dOld) return;
    H->teamSpawn[team].entity = ent;
    H->teamSpawn[team].a2 = q->a2; H->teamSpawn[team].a3 = q->a3; H->teamSpawn[team].a4 = q->a4;
    memcpy(g_hqPos[team], sp, sizeof g_hqPos[team]);
    bot_log("level: team %d bots now use that spawn as their HQ (closer to the fighting)", team);
    hq_memory_save(team, spawn_data_pos(ent) && !near_origin(spawn_data_pos(ent)) ? spawn_data_pos(ent) : sp, q->a2, q->a3, q->a4);
}

// ---- Close threat: an enemy soldier within CLOSE_THREAT_RANGE with a line to the bot is dealt with first, whatever
// the bot was doing (fighting someone further away, a supply run, a revive, cover, a rocket...). Checked 5x a second. ----
#define CLOSE_THREAT_RANGE     10.0f
static void close_threat_tick(int i)
{
    char *pl = H->bots[i], *s = *(char **)(pl + OFF_PLAYER_SOLDIER), *pm = player_manager();
    BotBrain *b = bot_brain(i);
    if (!s || !pm || !b->enabled || player_vehicle(pl) || g_fallVy[i] < -8.0f || *(float *)(s + OFF_SOLDIER_HEALTH) <= 0) return;
    float *me = (float *)(s + OFF_SOLDIER_POSITION), eye[3] = {me[0], me[1] + 1.6f, me[2]};
    void **arr = *(void ***)(pm + 0xac);
    unsigned count = *(unsigned *)(pm + 8);
    char *best = NULL;
    float bestD = CLOSE_THREAT_RANGE;
    for (unsigned k = 0; k < count && k < 64; k++) {
        char *t = arr[k], *ts;
        if (!t || t == pl || player_team(t) == player_team(pl) || player_vehicle(t) || !(ts = *(char **)(t + OFF_PLAYER_SOLDIER))) continue;
        if (*(float *)(ts + OFF_SOLDIER_HEALTH) <= 0) continue;
        float *tp = (float *)(ts + OFF_SOLDIER_SPAWN_POSITION);
        float dx = tp[0] - me[0], dy = tp[1] - me[1], dz = tp[2] - me[2], d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d >= bestD) continue;
        float chest[3] = {tp[0], tp[1] + 1.2f, tp[2]}, head[3] = {tp[0], tp[1] + 1.6f, tp[2]};
        if (solid_between(eye, chest, NULL) && solid_between(eye, head, NULL)) continue;   // behind a wall / floor
        bestD = d; best = t;
    }
    if (!best) return;
    if (b->task == TASK_FIGHT) {
        char *cur = g_fightTarget[i], *cs;
        if (cur == best) return;
        if (cur && player_exists(cur) && (cs = *(char **)(cur + OFF_PLAYER_SOLDIER)) && *(float *)(cs + OFF_SOLDIER_HEALTH) > 0 &&
            dist2((float *)(cs + OFF_SOLDIER_SPAWN_POSITION), me) < CLOSE_THREAT_RANGE * CLOSE_THREAT_RANGE) return;   // already on a close one
    }
    bot_log("close: %s turns on %s %.1f m away (was %s)", pl + OFF_PLAYER_ONLINE_NAME, best + OFF_PLAYER_ONLINE_NAME, bestD, kTaskNames[b->task]);
    if (b->task == TASK_SUPPLY || b->task == TASK_REVIVE || b->task == TASK_COVER || b->task == TASK_ROCKET) {
        b->task = b->prevTask; b->commander = b->prevCommander; b->serviceTarget = NULL;   // drop it, back to what came before
        b->prevTask = TASK_NONE; b->prevCommander = NULL;
    }
    start_fight(i, best, 0, "enemy close");
    g_fightAdvance[i] = 0;
    if (!spotted_cached(best)) bot_spot(pl, best);   // and call it out
}

// ---- Light vehicles: a bot with a long way to go takes a free jeep / buggy / quad of its own side (never armour or
// aircraft, never one a human is near) and drives it on the drivable part of the nav grid (nav_drive_mask: open,
// gently sloped, dry - corridors of 4 m or more), gets out ~30 m short of its flag and walks on. Throttle = EIA 0,
// steering = EIAYaw (positive turns towards a lower heading). ----
static int vehicle_is_boat(char *veh) { return bm_vehicle_profile(vehicle_name(veh))->kind == BM_BOAT; }
static unsigned char *g_boatMask;
static unsigned g_boatMaskFor;   // grid serial
static int *g_landLabels;   // landmass of every dry node (boat landings must be on the flag's)
static const unsigned char *boat_mask(void)
{
    NavGrid *g = *nav_slot();
    if (!g || g->phase != 2) return NULL;
    if (g_boatMaskFor != g->serial) {
        if (g_boatMask) HeapFree(GetProcessHeap(), 0, g_boatMask);
        drive_strikes_reset();
        int n = 0;
        g_boatMask = nav_boat_mask(g, &n);
        g_boatMaskFor = g->serial;
        if (g_landLabels) HeapFree(GetProcessHeap(), 0, g_landLabels);
        g_landLabels = nav_land_labels(g);
        bot_log("drive: %d boat-drivable water node(s) on %s", n, g->level);
    }
    return g_boatMask;
}
// AltBotMod V16 (after Brian's beta): a boat only when the trip needs one - the objective is on another landmass than
// the bot. Unknown (no grid / water table yet) counts as needed, as before.
static int boat_trip_needed(const float *from,const float *to)
{
    NavGrid *g=*nav_slot();
    if(!from || !to || !g || g->phase!=2 || !boat_mask() || g_boatMaskFor!=g->serial || !g_landLabels)return 1;
    int a=nav_label_at(g,g_landLabels,from[0],from[1],from[2]),b=nav_label_at(g,g_landLabels,to[0],to[1],to[2]);
    return !(a && a==b);
}
#define DRIVE_MIN_TRIP         (g_enhanced.minTrip)
#define DRIVE_FIND_RADIUS      (g_enhanced.findRadius)
#define DRIVE_STOP_SHORT       (fminf(g_enhanced.stopShort,18.0f))
#define DRIVE_WAYPOINT         6.0f
static unsigned g_driveCooldown[MAX_BOTS];
static unsigned char *g_driveMask;
static unsigned g_driveMaskFor;   // grid serial
// AltBotMod V9: at a level change. V8 kept the old map's masks while the new grid was surveyed, and a boat's strike
// (drive_strike: the new grid's size over the old mask) could write past the end of it.
static void drive_masks_reset(void)
{
    nav_set_road_mask(NULL, NULL);
    drive_strikes_reset();
    if (g_driveMask) HeapFree(GetProcessHeap(), 0, g_driveMask);
    if (g_boatMask) HeapFree(GetProcessHeap(), 0, g_boatMask);
    g_driveMask = g_boatMask = NULL;
    g_driveMaskFor = g_boatMaskFor = 0;
}
static int vehicle_is_light(char *veh)
{
    // Kept under its old internal name to minimize the diff. Selection now
    // uses explicit classes; aircraft never pass the ground filter.
    return bm_vehicle_allowed(&g_enhanced, bm_vehicle_profile(vehicle_name(veh)));
}
static float *vehicle_xf(char *veh)   // 4x4 transform (right, up, forward, position) of the vehicle's physics
{
    char *phys = !IsBadReadPtr(veh + 0x30, 4) ? *(char **)(veh + 0x30) : NULL;
    char *xf = phys && !IsBadReadPtr(phys + 0x88, 4) ? *(char **)(phys + 0x88) : NULL;
    return xf && !IsBadReadPtr(xf, 64) ? (float *)xf : NULL;
}
static int vehicle_ok(char *veh)
{
    return veh && !IsBadReadPtr(veh, 0x200) && *(unsigned *)veh == ADDR_VEHICLE_VTABLE && *(float *)(veh + OFF_VEHICLE_HEALTH) > 0;
}
// A route starting inside a vehicle must ignore that hull, then continue the
// remaining ray. Treating a self-hit as an entirely clear ray would hide walls.
static int drive_nav_ray(const float *from,const float *to,float *hit,float *normal)
{
    int owner=nav_active_path_owner(),i=owner-MAX_BOTS;
    char *veh=i>=0 && i<H->botCount ? g_drive[i].veh : NULL;
    void *own=veh && vehicle_ok(veh) && !IsBadReadPtr(veh+0x30,4) ? *(void **)(veh+0x30) : NULL;
    float a[3],point[3],nrm[3],length=0,direction[3];memcpy(a,from,12);
    for(int k=0;k<3;k++){direction[k]=to[k]-from[k];length+=direction[k]*direction[k];}
    length=sqrtf(length);if(length<0.01f)return 0;
    for(int k=0;k<3;k++)direction[k]/=length;
    for(int attempt=0;attempt<5;attempt++){
        if(!ray_cast(a,to,point,nrm))return 0;
        void *body=NULL;memcpy(&body,g_lastHitRest,sizeof body);
        if(vehicle_crushable_hit() && attempt<4){   // AltBotMod V8: a fence is driven through - look on past it
            float left=0;for(int k=0;k<3;k++)left+=(to[k]-point[k])*direction[k];
            if(left<=0.1f)return 0;
            for(int k=0;k<3;k++)a[k]=point[k]+direction[k]*0.1f;
            continue;
        }
        if(!own || body!=own || attempt==4){if(hit)memcpy(hit,point,12);if(normal)memcpy(normal,nrm,12);return 1;}
        float left=0;for(int k=0;k<3;k++)left+=(to[k]-point[k])*direction[k];
        if(left<=0.1f)return 0;
        for(int k=0;k<3;k++)a[k]=point[k]+direction[k]*0.1f;
    }
    return 1;
}
static int driver_seat_free(char *veh)
{
    char **b;
    return vehicle_seats(veh, &b) && !*(void **)(b[0] + 0x4c);
}
static int __attribute__((unused)) human_near(const float *p, float r)
{
    char *pm = player_manager();
    if (!pm) return 0;
    void **arr = *(void ***)(pm + 0xac);
    unsigned count = *(unsigned *)(pm + 8);
    for (unsigned k = 0; k < count && k < 64; k++) {
        char *pl = arr[k];
        float *hp;
        if (!pl || is_bot(pl) || !(hp = human_pos(pl))) continue;
        if (dist2(hp, p) < r * r) return 1;
    }
    return 0;
}
// The paved drive mask is the same every time a map loads: kept in BotMod_pave_<level>.bin (the paving survey casts up
// to 15000 rays a frame for seconds after every load - a stutter on a slow server).
static void pave_cache_path(char *out, int n, const char *level)
{
    char dir[MAX_PATH];
    GetModuleFileNameA(NULL, dir, sizeof dir);
    *strrchr(dir, '\\') = 0;
    snprintf(out, n, "%s\\BotMod_pave_%s.bin", dir, level);
}
static const unsigned char *drive_mask(void)
{
    NavGrid *g = *nav_slot();
    if (!g || g->phase != 2) return NULL;
    if (g_driveMaskFor != g->serial) {
        if (g_driveMask) HeapFree(GetProcessHeap(), 0, g_driveMask);
        drive_strikes_reset();
        int n = 0;
        double t = perf_now();
        nav_set_road_mask(NULL, NULL);
        g_driveMask = nav_drive_mask(g, &n);
        g_driveMaskFor = g->serial;
        nav_set_road_mask(g, g_driveMask);
        g_paveCursor = 0; g_paveCount = 0;
        bot_log("drive: %d drivable node(s) on %s (%.0f ms)", n, g->level, perf_now() - t);
        if (g_driveMask) {
            char path[MAX_PATH + 64];
            pave_cache_path(path, sizeof path, g->level);
            FILE *f = fopen(path, "rb");
            int hdr[3] = {0, 0, 0}, cells = g->w * g->h * NAV_LAYERS;
            if (f) {
                unsigned char *costs=HeapAlloc(GetProcessHeap(),0,(size_t)cells);
                if (costs && fread(hdr, 4, 3, f) == 3 && hdr[0] == g->w && hdr[1] == g->h && hdr[2] == NAV_LAYERS &&
                    fread(costs, 1, (size_t)cells, f) == (size_t)cells) {
                    // Import paving COSTS only. Old zeroed obstruction cells
                    // cannot overwrite the freshly derived driving geometry.
                    for(int k=0;k<cells;k++)if(g_driveMask[k] && costs[k]>=1 && costs[k]<=4)g_driveMask[k]=costs[k];
                    g_paveCursor = -1;
                    bot_log("drive: paving loaded from %s", path);
                    tree_apply_drive();
                } else { nav_set_road_mask(NULL,NULL);HeapFree(GetProcessHeap(),0,g_driveMask);g_driveMask = nav_drive_mask(g, &n); nav_set_road_mask(g, g_driveMask); }   // (a bad file: start clean)
                if(costs)HeapFree(GetProcessHeap(),0,costs);
                fclose(f);
            }
        }
    }
    return g_driveMask;
}
static char *g_vehBad[8];          // vehicles whose route failed recently: left alone for a while
static unsigned g_vehBadUntil[8];
static int vehicle_bad(char *v)
{
    for (int k = 0; k < 8; k++) if (g_vehBad[k] == v && H->frame < g_vehBadUntil[k]) return 1;
    return 0;
}
static void vehicle_mark_bad(char *v, int sec)
{
    int slot = 0;
    for (int k = 0; k < 8; k++) if (g_vehBad[k] == v || H->frame >= g_vehBadUntil[k]) { slot = k; break; }
    g_vehBad[slot] = v; g_vehBadUntil[slot] = H->frame + (unsigned)sec * 30;
}
static void drive_stop(int i, const char *why, int cooldownSec)
{
    DriveState *d = &g_drive[i];
    if (!d->phase) return;
    diagnostic_note(i,why,d->veh);memset(&g_armor[i],0,sizeof g_armor[i]);
    int wasIn = player_vehicle(H->bots[i]) != NULL;
    if (wasIn && d->phase != 7) {   // the driver: "Get out!" - riders bail first, then the driver
        bot_say(i, ORDER_GETOUT);
        order_reaction(H->bots[i], ORDER_GETOUT, NULL);
    }
    if (player_vehicle(H->bots[i])) bot_exit_vehicle(i, 0);
    g_driving[i] = 0;
    bot_log("drive: %s %s", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, why);
    d->phase = 0; d->veh = NULL;
    memset(&g_enhancedSteer[i], 0, sizeof g_enhancedSteer[i]);
    g_driveCooldown[i] = H->frame + (unsigned)cooldownSec * 30;
    H->botMove[i][0] = H->botMove[i][1] = H->botMove[i][2] = 0;
    bot_nav(i)->mode = 0;
    bot_brain(i)->replanAt = 0;
    if (wasIn) {
        choose_local_objective(i, "out of the vehicle");   // the flag nearest where it got out, then back to the squad
        g_vehChanceUntil[i] = H->frame + 20 * 30;           // a ground vehicle nearby for the next leg?
        // Preserve the failure/dismount cooldown: otherwise the bot can
        // immediately board the same vehicle and repeat a failed trip.
    }
}
// AltBotMod V8: shallow water is driven through (a ford, a flooded road, a beach). After the paving survey, water
// nodes next to the drivable map are probed: a ray from the surface straight down (on through the water body) finds the
// bottom; under 0.9 m deep the node joins the drive mask (cost class 3), and its water neighbours are probed in the next
// pass - fords grow across from both banks. <= 48 rays and 0.5 ms a tick.
#define FORD_MAX_DEPTH 0.9f
static int g_fordCursor = -1, g_fordPass, g_fordMarked, g_fordPassMarked;
static unsigned g_fordFor;
static void drive_ford_tick(void)
{
    NavGrid *g = *nav_slot();
    if (!g || !g_driveMask || g_driveMaskFor != g->serial || g_paveCursor >= 0) return;
    if (g_fordFor != g->serial) { g_fordFor = g->serial; g_fordCursor = 0; g_fordPass = 0; g_fordMarked = g_fordPassMarked = 0; }
    if (g_fordCursor < 0) return;
    int n = g->w * g->h * NAV_LAYERS, rays = 0;
    double began = perf_now();
    unsigned saved = g_rayFlags;
    g_rayFlags = RAY_DONT_CHECK_CHARACTER;
    for (; g_fordCursor < n && rays < 48; g_fordCursor++) {
        if ((g_fordCursor & 15) == 0 && perf_now() - began >= 0.5) break;
        int node = g_fordCursor;
        if (g_driveMask[node] || !(g->flags[node] & NAV_WATER)) continue;
        int bank = 0;
        for (int d = 0; d < 8 && !bank; d++) { int nb = nav_step_node(g, node, d); if (nb >= 0 && g_driveMask[nb] && g_driveMask[nb] != 5) bank = 1; }
        if (!bank) continue;
        float p[3], a[3], b[3], h[3];
        nav_node_pos(g, node, p);
        a[0] = b[0] = p[0]; a[2] = b[2] = p[2]; a[1] = p[1] + 3.0f; b[1] = p[1] - 4.0f;
        float bottom = -1e30f, surface = -1e30f;
        for (int k = 0; k < 3; k++) {   // down onto the water surface, then on through it to the bed
            rays++;
            if (!ray_cast(a, b, h, NULL)) break;
            if (nav_hit_class() == NAV_HIT_WATER) { if (surface < -1e29f) surface = h[1]; a[1] = h[1] - 0.05f; continue; }
            bottom = h[1]; break;
        }
        // (the water surface must be seen first: depth = surface - bed, whatever height the grid keeps for the node)
        if (surface > -1e29f && bottom > -1e29f && surface - bottom < FORD_MAX_DEPTH) { g_driveMask[node] = 3; g_fordMarked++; g_fordPassMarked++; }
    }
    g_rayFlags = saved;
    if (g_fordCursor >= n) {
        if (g_fordPassMarked && ++g_fordPass < 40) { g_fordCursor = 0; g_fordPassMarked = 0; return; }   // the next ring out
        if (g_fordMarked) bot_log("drive: shallow-water survey done - %d ford node(s) drivable (under %.1f m deep)", g_fordMarked, FORD_MAX_DEPTH);
        g_fordCursor = -1;
    }
}
// Paving survey (after the mask is built, <=64 rays / 0.75 ms a frame): a ray down at every drivable node reads the ground's
// material - paved surfaces (asphalt / concrete, material index 0x3fxx) become the cheapest class, smooth unpaved
// ground (terrain soils 0x40xx, dirt roads included) one class dearer than before.
static void drive_pave_tick(void)
{
    NavGrid *g = *nav_slot();
    if (!g || !g_driveMask || g_driveMaskFor != g->serial || g_paveCursor < 0) return;
    int n = g->w * g->h * NAV_LAYERS, end = g_paveCursor + 2048, rays=0;
    double began=perf_now();
    unsigned saved = g_rayFlags;
    g_rayFlags = RAY_DONT_CHECK_CHARACTER;
    for (; g_paveCursor < n && g_paveCursor < end && rays<64; g_paveCursor++) {
        if((g_paveCursor&7)==0 && perf_now()-began>=0.75)break;
        unsigned char *m = &g_driveMask[g_paveCursor];
        if (!*m) continue;
        float p[3], a[3], b[3], h[3];
        nav_node_pos(g, g_paveCursor, p);
        a[0] = b[0] = p[0]; a[2] = b[2] = p[2]; a[1] = p[1] + 1.0f; b[1] = p[1] - 1.0f;
        rays++;if (!ray_cast(a, b, h, NULL)) continue;
        unsigned mat = hit_material();
        if ((mat & 0xff00) == 0x3f00 && (mat >> 24) == 0) { *m = 1; g_paveCount++; }   // paved
        else if (*m == 1) *m = 2;                                                        // smooth, but not paved
    }
    g_rayFlags = saved;
    if (g_paveCursor >= n) {
        bot_log("drive: paving survey done - %d paved node(s)", g_paveCount);
        g_paveCursor = -1;
        char path[MAX_PATH + 64];
        pave_cache_path(path, sizeof path, g->level);
        FILE *f = fopen(path, "wb");
        if (f) { int hdr[3] = {g->w, g->h, NAV_LAYERS}; fwrite(hdr, 4, 3, f); fwrite(g_driveMask, 1, (size_t)n, f); fclose(f); }
        tree_apply_drive();
    }
}

// Tree survey (once per nav grid, <=16 surveyed nodes / 0.75 ms a frame): trees are detail mesh, which the nav build never saw. Rays
// out from every dry walkable node at 1.5 m, 2.5 m long in 8 directions, with detail mesh on; a hit the same ray
// without detail mesh does not have is vegetation. Per node the distance to the nearest trunk (decimetres): soldiers
// keep off nodes within 1.2 m (nav), vehicles get class 5 within 2.5 m - very dear, no straight short cut past it.
static unsigned char *g_treeDist;
static unsigned g_treeFor;   // grid serial
static int g_treeCursor = -1, g_treeCount;
static int g_treeFinishPass,g_treeFinishNode,g_treeNearCount;
static void tree_apply_drive(void)
{
    NavGrid *g = *nav_slot();
    if (!g || g_treeFor != g->serial || g_treeCursor >= 0 || !g_treeDist || !g_driveMask || g_driveMaskFor != g->serial || g_paveCursor >= 0) return;
    int n = g->w * g->h * NAV_LAYERS, k = 0;
    for (int i = 0; i < n; i++) if (g_driveMask[i] && g_treeDist[i] <= 25) { g_driveMask[i] = 5; k++; }
    bot_log("drive: %d drivable node(s) within 2.5 m of a tree - vehicles keep off them", k);
}
static void tree_tick(void)
{
    NavGrid *g = *nav_slot();
    if (!g || g->phase != 2) return;
    int n = g->w * g->h * NAV_LAYERS;
    if (g_treeFor != g->serial) {
        nav_set_tree_dist(NULL, NULL);
        if (g_treeDist) HeapFree(GetProcessHeap(), 0, g_treeDist);
        g_treeDist = HeapAlloc(GetProcessHeap(), 0, (size_t)n);
        g_treeFor = g->serial;
        if (!g_treeDist) { g_treeCursor = -1; return; }
        memset(g_treeDist, 255, (size_t)n);
        g_treeCursor = 0; g_treeCount = 0;
        g_treeFinishPass=g_treeFinishNode=g_treeNearCount=0;
        char path[MAX_PATH + 64], dir[MAX_PATH];   // surveyed before (same grid size): straight from the file
        GetModuleFileNameA(NULL, dir, sizeof dir);
        *strrchr(dir, '\\') = 0;
        snprintf(path, sizeof path, "%s\\BotMod_trees_%s.bin", dir, g->level);
        FILE *f = fopen(path, "rb");
        if (f) {
            int hdr[3] = {0, 0, 0};
            if (fread(hdr, 4, 3, f) == 3 && hdr[0] == g->w && hdr[1] == g->h && hdr[2] == NAV_LAYERS && fread(g_treeDist, 1, (size_t)n, f) == (size_t)n) {
                g_treeCursor = n;   // finished below
                bot_log("trees: loaded %s", path);
            } else memset(g_treeDist, 255, (size_t)n);
            fclose(f);
        }
    }
    if (g_treeCursor < 0) return;
    int end = g_treeCursor + 16, scanned = 0;
    double began=perf_now();
    unsigned saved = g_rayFlags;
    for (; g_treeCursor < n && g_treeCursor < end && scanned < 2048; g_treeCursor++, scanned++) {
        if(perf_now()-began>=0.75)break;
        int node = g_treeCursor;
        if (!(g->flags[node] & NAV_WALKABLE) || (g->flags[node] & NAV_WATER) || !g->edges[node]) { end++; continue; }   // (empty: free)
        float p[3], h[3], h2[3], nrm[3], best = 99.0f;
        nav_node_pos(g, node, p);
        for (int d = 0; d < 16; d++) {   // (16: a thin palm trunk slips between 8 rays)
            float a[3] = {p[0], p[1] + 1.5f, p[2]}, b[3] = {p[0] + sinf(d * 0.392699f) * 2.5f, p[1] + 1.5f, p[2] + cosf(d * 0.392699f) * 2.5f};
            g_rayFlags = RAY_DONT_CHECK_CHARACTER | RAY_CHECK_DETAIL_MESH;
            if (!ray_cast(a, b, h, nrm) || fabsf(nrm[1]) > 0.6f) continue;   // (a trunk is upright: its normal is level)
            float dh = sqrtf(dist2(h, a));
            if (dh >= best) continue;
            g_rayFlags = RAY_DONT_CHECK_CHARACTER;
            if (ray_cast(a, b, h2, NULL) && sqrtf(dist2(h2, a)) <= dh + 0.1f) continue;   // a wall: the nav knows it
            best = dh;
        }
        if (best < 2.6f) { g_treeDist[node] = (unsigned char)(best * 10.0f); g_treeCount++; }
    }
    g_rayFlags = saved;
    if (g_treeCursor >= n) {
        // Spread tree distances and count nearby nodes incrementally. The old
        // two whole-map propagation sweeps blocked a single server tick.
        double finishBegan=perf_now();int work=0;
        if(!g_treeCount && g_treeFinishPass<2){g_treeFinishPass=2;g_treeFinishNode=0;}
        while(g_treeFinishPass<2){
            for(;g_treeFinishNode<n && work<4096;g_treeFinishNode++,work++){
                if((work&63)==0 && perf_now()-finishBegan>=0.75)return;
                int node=g_treeFinishNode;
                if(!(g->flags[node]&NAV_WALKABLE))continue;
                for(int d=0;d<8;d++){
                    int nb=nav_step_node(g,node,d);
                    if(nb<0 || g_treeDist[nb]==255)continue;
                    int v=g_treeDist[nb]+(int)(((d&1)?1.414f:1.0f)*g->cell*10.0f);
                    if(v<=25 && v<g_treeDist[node])g_treeDist[node]=(unsigned char)v;
                }
            }
            if(g_treeFinishNode<n)return;
            g_treeFinishPass++;g_treeFinishNode=0;
        }
        for(;g_treeFinishNode<n && work<4096;g_treeFinishNode++,work++){
            if((work&63)==0 && perf_now()-finishBegan>=0.75)return;
            if(g_treeDist[g_treeFinishNode]<=12)g_treeNearCount++;
        }
        if(g_treeFinishNode<n)return;
        int nearCnt=g_treeNearCount;
        bot_log("trees: survey of %s done - %d node(s) within 2.5 m of a trunk, %d within 1.2 m (soldiers go round)", g->level, g_treeCount, nearCnt);
        if (g_treeCount || nearCnt) {   // (a loaded survey has g_treeCount 0 - only save a fresh one)
            char path[MAX_PATH + 64], dir[MAX_PATH];
            GetModuleFileNameA(NULL, dir, sizeof dir);
            *strrchr(dir, '\\') = 0;
            snprintf(path, sizeof path, "%s\\BotMod_trees_%s.bin", dir, g->level);
            FILE *f = g_treeCount ? fopen(path, "wb") : NULL;
            if (f) { int hdr[3] = {g->w, g->h, NAV_LAYERS}; fwrite(hdr, 4, 3, f); fwrite(g_treeDist, 1, (size_t)n, f); fclose(f); }
        }
        g_treeCursor = -1;
        nav_set_tree_dist(g, g_treeDist);
        tree_apply_drive();
    }
}

// Squads: every 2 s, bot squads with a far objective (farthest first) take a free light vehicle near them if at
// least half their living members (2 or more) are within SQUAD_VEH_RADIUS of it; those members board - first in
// drives - and the rest carry on on foot.
static int plan_to_vehicle(int i, char *soldier, const float *vp);
#define SQUAD_VEH_RADIUS (g_enhanced.squadRadius)
static void squad_vehicle_tick(void)
{
    if (g_enhanced.returnToBase || !mode_is_cq()) return;
    if (g_driveOff || !g_enhanced.driving || H->frame % 60 || !g_flagCount) return;
    for (int team = 1; team <= 2; team++) {
        unsigned mask = bot_squads_mask(team);
        int sq[32], ns = 0;
        float sqD[32];
        for (int s = 1; s <= 31; s++) {
            if (!(mask & (1u << s))) continue;
            int f = squad_objective(team, s);
            if (f < 0) continue;
            float cen[3];
            squad_centre(team, s, cen);
            float d = dist2(cen, flag_pos(f));
            if (d < DRIVE_MIN_TRIP * DRIVE_MIN_TRIP) continue;
            int k = ns++;   // farthest objective first
            while (k > 0 && sqD[k - 1] < d) { sq[k] = sq[k - 1]; sqD[k] = sqD[k - 1]; k--; }
            sq[k] = s; sqD[k] = d;
        }
        for (int q = 0; q < ns; q++) {
            int s = sq[q], f = squad_objective(team, s), mem[16], nm = 0, living = 0;
            for (int j = 0; j < H->botCount && nm < 16; j++) {
                char *pl = H->bots[j], *js = *(char **)(pl + OFF_PLAYER_SOLDIER);
                if (player_team(pl) != team || player_squad(pl) != s || !js) continue;
                living++;
                BotBrain *b = bot_brain(j);
                if (!b->enabled || g_drive[j].phase || player_vehicle(pl) || b->task != TASK_NONE || H->frame < g_driveCooldown[j]) continue;
                mem[nm++] = j;
            }
            if (nm < 1) continue;
            int sameLand = -1;   // already on the flag's island: no boats (worked out when a boat comes up)
            char **vehs, *best = NULL;
            int nv = vehicle_list(&vehs), bestNear = 0;
            for (int k = 0; k < nv; k++) {
                char *v = vehs[k];
                if (!vehicle_ok(v) || !vehicle_is_light(v) || !driver_seat_free(v) || vehicle_bad(v)) continue;
                if (vehicle_is_boat(v)) {
                    if (sameLand < 0) {   // the landmass table must be this grid's (boat_mask() rebuilds it for a new map:
                        NavGrid *g = *nav_slot();   // an old map's table read with this grid's size ran off its end)
                        float cen[3];
                        sameLand = 0;
                        squad_centre(team, s, cen);
                        if (g && g->phase == 2 && boat_mask() && g_boatMaskFor == g->serial && g_landLabels) {
                            int a = nav_label_at(g, g_landLabels, cen[0], cen[1], cen[2]);
                            int b = nav_label_at(g, g_landLabels, flag_pos(f)[0], flag_pos(f)[1], flag_pos(f)[2]);
                            sameLand = a && a == b;
                        }
                    }
                    if (sameLand) continue;
                }
                int vt = 0, other = 3 - team, nNear = 0, taken = 0;
                float *vp = vehicle_pos(v, &vt);
                if (!vp || (vt && vt != team) || human_near(vp, g_enhanced.humanRadius)) continue;
                if (g_hqPos[team][0] != 0 && g_hqPos[other][0] != 0 && dist2(vp, g_hqPos[team]) > dist2(vp, g_hqPos[other])) continue;
                for (int j = 0; j < H->botCount && !taken; j++) if (g_drive[j].phase && g_drive[j].veh == v) taken = 1;
                if (taken) continue;
                for (int m = 0; m < nm; m++) {
                    char *js = *(char **)(H->bots[mem[m]] + OFF_PLAYER_SOLDIER);
                    if (dist2((float *)(js + OFF_SOLDIER_POSITION), vp) < SQUAD_VEH_RADIUS * SQUAD_VEH_RADIUS) nNear++;
                }
                if (nNear > bestNear) { bestNear = nNear; best = v; }
            }
            if (!best || bestNear < (living >= 2 ? 2 : 1) || bestNear * 2 < living) continue;
            float *vp = vehicle_pos(best, NULL);
            int seats = free_seat_count(best), boarding = 0;
            for (int m = 0; m < nm && boarding < seats; m++) {
                int j = mem[m];
                char *js = *(char **)(H->bots[j] + OFF_PLAYER_SOLDIER);
                if (dist2((float *)(js + OFF_SOLDIER_POSITION), vp) >= SQUAD_VEH_RADIUS * SQUAD_VEH_RADIUS) continue;   // too far: left behind
                if (!plan_to_vehicle(j, js, vp)) continue;
                DriveState *d = &g_drive[j];
                memset(d, 0, sizeof *d);
                d->phase = 5; d->veh = best; d->at = H->frame; d->flag = f;
                memcpy(d->start, vp, 12);
                memcpy(d->goal, flag_pos(f), 12);
                bot_nav(j)->mode = 3;
                boarding++;
            }
            if (boarding) bot_log("squad: %s squad %d boards the %s for flag %d (%d of %d, %.0f m to go)", team == 1 ? "US" : "RU", s,
                                  vehicle_name(best), f, boarding, living, sqrtf(sqD[q]));
        }
    }
}

// Caspian V3: most vehicles are reached without the grid search. V2 logs: bots "cannot route" to a jet 45 m
// away or a helicopter 54 m away on the US side (the pads are at the edge of / outside the grid), and abandoned
// vehicles were lost while the bot waited in the one shared A* queue. First a checked straight walk to a spot
// beside the hull (6, 9 or 12 m out: wings and rotors), then a Fun-Bots route to the vehicle.
static unsigned g_quickVehicleWalks, g_quickVehicleTraces;
// Floor under a spot beside a vehicle (not water): 1 and p[1] set.
static int vehicle_side_floor(const float *vp, float *p)
{
    float from[3] = {p[0], vp[1] + 3, p[2]}, down[3] = {p[0], vp[1] - 5, p[2]}, hit[3];
    unsigned saved = g_rayFlags; g_rayFlags = RAY_DONT_CHECK_CHARACTER;
    int floor = ray_cast(from, down, hit, NULL) && nav_hit_class() != NAV_HIT_WATER;
    g_rayFlags = saved;
    if (floor) p[1] = hit[1];
    return floor;
}
static unsigned g_quickVehicleFails;
static int vehicle_quick_route(int i, char *soldier, const float *vp)
{
    const float *me = (float *)(soldier + OFF_SOLDIER_POSITION);
    float dx = vp[0] - me[0], dz = vp[2] - me[2], d = sqrtf(dx * dx + dz * dz);
    if (!isfinite(d) || d < 2) return 0;
    // Spots beside the hull: on our side 6, 9 and 12 m out, then round it 10 m out at +-45, +-90 and 135 degrees
    // (a wing or a rotor in line). At most 8 straight-walk checks: one tick's allowance.
    float spots[8][3]; int ns = 0;
    static const float out[3] = {6, 9, 12};
    for (int k = 0; k < 3; k++) if (d > out[k] + 1) { float f = (d - out[k]) / d; spots[ns][0] = me[0] + dx * f; spots[ns][1] = vp[1]; spots[ns][2] = me[2] + dz * f; ns++; }
    float base = atan2f(-dx, -dz);
    for (int k = 1; k <= 5; k++) { float a = base + (k + 1) / 2 * 0.785398f * ((k & 1) ? 1 : -1);
        spots[ns][0] = vp[0] + sinf(a) * 10; spots[ns][1] = vp[1]; spots[ns][2] = vp[2] + cosf(a) * 10; ns++; }
    int floors = 0, tried = 0;
    VehicleApproachState *as = &g_vehicleApproach[i];
    for (int k = as->spot; k < ns && d <= 150; k++) {
        float *p = spots[k];
        if (!vehicle_side_floor(vp, p)) continue;
        floors++;
        if (direct_retry(i, me, p)) continue;   // this walk already failed from here
        int clear = tried < 6 ? walk_clear(me, p, 150) : -1;   // (one check kept for the route's finishing walk)
        if (clear < 0) { as->spot = k; g_lastPlanDeferred = 1; return 0; }   // checks used up this tick: resume here next tick
        tried++;
        if (clear) {
            direct_path(i, soldier, p);
            if (g_quickVehicleWalks++ % 10 == 0)
                bot_log("fleet: %s walks straight to %s (%.0f m; %u straight walks, %u Fun-Bots routes to vehicles)",
                        H->bots[i] + OFF_PLAYER_ONLINE_NAME, vehicle_name(g_fleet[i].vehicle ? g_fleet[i].vehicle : NULL), d,
                        g_quickVehicleWalks, g_quickVehicleTraces);
            return 1;
        }
    }
    // A route to the vehicle itself, even when it stands at a flag (a route to the flag can end 45 m away). When the
    // nearest recorded waypoint is farther out, the route ends there and a checked straight walk finishes it.
    int n = trace_plan_ex(i, soldier, vp[0], vp[1], vp[2], 2);   // 2 = TRACE_VEHICLE
    if (n) {
        BotPath *p = &g_paths[i];
        float *end = &p->pts[(n - 1) * 3];
        if (dist2(end, vp) <= 20 * 20) { p->partial = 0; g_quickVehicleTraces++; return n; }
        if (n < PATH_MAX_POINTS && dist2(end, vp) <= 60 * 60) {
            float ex = vp[0] - end[0], ez = vp[2] - end[2], el = sqrtf(ex * ex + ez * ez), q[3];
            q[0] = vp[0] - ex / el * 8; q[1] = vp[1]; q[2] = vp[2] - ez / el * 8;
            int clear = vehicle_side_floor(vp, q) ? walk_clear(end, q, 60) : 0;
            if (clear < 0) { as->spot = ns; p->count = p->next = p->partial = 0; g_lastPlanDeferred = 1; return 0; }
            if (clear > 0) {
                memcpy(&p->pts[n * 3], q, 12); p->link[n] = -1; p->wpFlags[n] = 0; p->count = n + 1; p->partial = 0;
                g_quickVehicleTraces++;
                return n + 1;
            }
        }
        p->count = p->next = p->partial = 0;
    }
    if (g_quickVehicleFails++ % 10 == 0)
        bot_log("fleet: %s has no quick way to %s %.0f m away (%d/%d spots beside it have a floor, %d straight walks blocked, Fun-Bots route %s) - grid search",
                H->bots[i] + OFF_PLAYER_ONLINE_NAME, vehicle_name(g_fleet[i].vehicle ? g_fleet[i].vehicle : NULL), d, floors, ns, tried,
                n ? "ends too far from it" : "none");
    return 0;
}
// Walk up to a vehicle: its own spot, else the nearest reachable point a few metres from it (a docked boat lies in
// the water beside the pier / beach). Returns the path's point count (0 = no way there).
static int plan_to_vehicle(int i, char *soldier, const float *vp)
{
    VehicleApproachState *a=&g_vehicleApproach[i];a->deferred=0;
    if(!soldier || !vp || !isfinite(vp[0]) || !isfinite(vp[1]) || !isfinite(vp[2]))return 0;
    if(vehicle_board_close(soldier,vp)){
        nav_cancel_path(i);
        // Native EnterVehicle validates the actual entry. No A* route to the
        // hull's occupied centre is needed when already within boarding range.
        memset(a,0,sizeof *a);g_paths[i].count=g_paths[i].next=g_paths[i].partial=0;
        bot_nav(i)->mode=0;memset(H->botMove[i],0,sizeof H->botMove[i]);return 1;
    }
    const float *me=(float *)(soldier+OFF_SOLDIER_POSITION);
    float distance=sqrtf(dist2(me,vp));
    if(distance<=25 && distance>VEHICLE_BOARD_DIST && fabsf(me[1]-vp[1])<=8){
        float fraction=(distance-(VEHICLE_BOARD_DIST-1))/distance;
        float outside[3]={me[0]+(vp[0]-me[0])*fraction,me[1],me[2]+(vp[2]-me[2])*fraction};
        // Short clear boarding walks need no distant A* queue or finished grid.
        // End outside the hull; the ordinary native entry call validates boarding.
        if(nav_segment_clear(ray_cast,me,outside)){
            nav_cancel_path(i);memset(a,0,sizeof *a);BotPath *path=&g_paths[i];
            memset(path,0,sizeof *path);memcpy(path->pts,outside,12);path->link[0]=-1;
            path->count=1;path->goalX=outside[0];path->goalY=outside[1];path->goalZ=outside[2];
            return 1;
        }
    }
    if(a->soldier!=soldier || dist2(a->target,vp)>4 || fabsf(a->target[1]-vp[1])>2 || H->frame-a->lastAt>60*30){
        memset(a,0,sizeof *a);a->soldier=soldier;memcpy(a->target,vp,12);
    }
    a->lastAt=H->frame;
    if(!a->candidate && !a->quick){
        g_lastPlanDeferred=0;
        int n=vehicle_quick_route(i,soldier,vp);
        if(n){memset(a,0,sizeof *a);return n;}
        if(g_lastPlanDeferred){a->deferred=1;return 0;}   // Fun-Bots route budget for this tick: next tick
        a->quick=1;
    }
    while(a->candidate<13){
        if(!plan_budget_left()){a->deferred=1;return 0;}
        unsigned candidate=a->candidate;
        float x=a->target[0],y=a->target[1],z=a->target[2];
        if(candidate<12){
            // The hull center can be an isolated nav cell. Begin OUTSIDE it.
            unsigned ring=candidate/4,quadrant=candidate%4;
            float r=ring==0?6.0f:ring==1?3.0f:9.0f;
            float angle=quadrant*1.5707963f+r*0.26f;
            x+=sinf(angle)*r;y+=1;z+=cosf(angle)*r;
        }
        int n=plan_path(i,soldier,x,y,z);
        if(!n && g_lastPlanDeferred){a->deferred=1;return 0;}
        a->candidate++;
        if(n){memset(a,0,sizeof *a);return n;}
    }
    // Exhausting all candidates is a route failure. Exhausting this frame's
    // budget is only a deferral, and retains both progress and the reservation.
    memset(a,0,sizeof *a);return 0;
}

static int vehicle_board_close(char *soldier,const float *vp)
{
    if(!soldier || !vp)return 0;
    const float *p=(float *)(soldier+OFF_SOLDIER_POSITION);
    return dist2(p,vp)<=VEHICLE_BOARD_DIST*VEHICLE_BOARD_DIST && fabsf(p[1]-vp[1])<=8;
}
static int vehicle_plan_deferred(int i)
{ return g_vehicleApproach[i].deferred; }
static int vehicle_finish_approach(int i,char *soldier,const float *vp,float reach)
{
    if(!soldier || !vp)return 0;
    const float *me=(float *)(soldier+OFF_SOLDIER_POSITION);
    float distance=dist2(me,vp);
    if(distance<=reach*reach && fabsf(me[1]-vp[1])<=8)return 1;
    BotPath *p=&g_paths[i];
    if(p->count && p->next<p->count)return 0;
    if(fabsf(me[1]-vp[1])<=8 && distance<=24*24){
        // Snapping a hull-perimeter point and consuming its final waypoint can
        // leave a bot outside boarding range. Finish that short walk explicitly.
        BotNav *n=bot_nav(i);n->x=vp[0];n->z=vp[2];n->mode=2;
    }else if(plan_budget_left()){
        if(plan_to_vehicle(i,soldier,vp))bot_nav(i)->mode=vehicle_board_close(soldier,vp)?0:3;
        else if(vehicle_plan_deferred(i)){bot_nav(i)->mode=0;memset(H->botMove[i],0,sizeof H->botMove[i]);}
    }
    return 0;
}


// On the way: friendly bots on foot by the vehicle are called in ("Get in!"). One heading the same way answers
// "Roger" and walks over (squad boarding), one going elsewhere "Negative". The driver stops until they are in.
static unsigned g_askedAt[MAX_BOTS];
static int drive_pickup(int i, DriveState *d, const float *vp)
{
    char *pl = H->bots[i];
    int boarding = 0;
    for (int j = 0; j < H->botCount; j++) if (j != i && g_drive[j].phase == 5 && g_drive[j].veh == d->veh) boarding++;
    if (d->pickupUntil) {
        if (boarding && free_seat_count(d->veh)>0 && H->frame < d->pickupUntil) return 1;
        bot_log("drive: %s drives on (%s)", pl + OFF_PLAYER_ONLINE_NAME, boarding ? "waited long enough" : "everyone is in");
        d->pickupUntil = 0;
        return 0;
    }
    if (H->frame - d->pickupAskAt < 15*30) return 0;   // AltBotMod V16: every 15 s, boats too (V7-V15: 30 s, never boats)
    d->pickupAskAt = H->frame;
    int seats = free_seat_count(d->veh), team = player_team(pl);
    if (seats <= 0 || dist2(vp, d->goal) < 120.0f * 120.0f) return 0;   // full, or nearly there
    int yes = 0, asked = 0;
    for (int j = 0; j < H->botCount && yes < seats; j++) {
        char *jp = H->bots[j], *js = *(char **)(jp + OFF_PLAYER_SOLDIER);
        BotBrain *b = bot_brain(j);
        if (j == i || !js || player_team(jp) != team || player_vehicle(jp) || g_drive[j].phase ||
            (g_fleet[j].vehicle && H->frame<g_fleet[j].until) || !b->enabled || H->frame < g_driveCooldown[j]) continue;
        if (b->task != TASK_NONE || g_fightTarget[j] || H->frame - g_askedAt[j] < 40 * 30) continue;
        float *jpos = (float *)(js + OFF_SOLDIER_POSITION);
        if (dist2(jpos, vp) > 35.0f * 35.0f || fabsf(jpos[1] - vp[1]) > 6) continue;   // within earshot (V16: 35 m, was 25)
        g_askedAt[j] = H->frame;
        if (!asked++) bot_say(i, ORDER_GETIN);
        int tf = b->targetFlag;
        // worth it: the ride ends nearer its own flag than it is now, by a good margin
        int useful = tf >= 0 && tf < objective_count() && sqrtf(dist2(d->goal, objective_pos(tf))) < sqrtf(dist2(jpos, objective_pos(tf))) - 60.0f;
        if (!useful || !plan_to_vehicle(j, js, vp)) {
            bot_say_later(j, ORDER_NEGATIVE, 25 + 10 * asked);
            bot_log("drive: %s offers %s a ride - Negative (going elsewhere)", pl + OFF_PLAYER_ONLINE_NAME, jp + OFF_PLAYER_ONLINE_NAME);
            continue;
        }
        DriveState *dj = &g_drive[j];
        memset(dj, 0, sizeof *dj);
        dj->phase = 5; dj->veh = d->veh; dj->at = H->frame; dj->flag = tf;
        memcpy(dj->start, vp, 12);
        memcpy(dj->goal, objective_pos(tf), 12);
        bot_nav(j)->mode = 3;
        bot_say_later(j, ORDER_ROGERTHAT, 25 + 10 * asked);
        bot_log("drive: %s offers %s a ride - Roger, it walks over", pl + OFF_PLAYER_ONLINE_NAME, jp + OFF_PLAYER_ONLINE_NAME);
        yes++;
    }
    if (!yes) return 0;
    d->pickupUntil = H->frame + 7 * 30;   // (V16: 7 s - a bot 35 m away needs ~6 s to run over)
    bot_log("drive: %s stops to pick up %d bot(s)", pl + OFF_PLAYER_ONLINE_NAME, yes);
    return 1;
}

// Brain hook: a bot heading for a far flag looks for a vehicle to take.
static void drive_consider(int i)
{
    if (g_enhanced.returnToBase || !mode_is_cq()) return;
    BotBrain *b = bot_brain(i);
    char *pl = H->bots[i], *s = *(char **)(pl + OFF_PLAYER_SOLDIER);
    if (g_driveOff || !g_enhanced.driving || !s || g_drive[i].phase || player_vehicle(pl) || !b->enabled || b->task != TASK_NONE || !b->enroute || b->targetFlag < 0 || b->targetFlag >= g_flagCount) return;
    int chance = H->frame < g_vehChanceUntil[i];   // just got out of a vehicle / just took a flag: anyone may grab one nearby
    if (!chance && (player_squad(pl) > 0 || squad_objective(player_team(pl), player_squad(pl)) >= 0)) return;   // squads decide together
    if (!chance && H->frame - g_lastSpawnedAt[i] < 10 * 30) return;   // (a fresh spawn joins its squad half a second later)
    if (H->frame < g_driveCooldown[i] || (H->frame + i) % 30) return;
    float *me = (float *)(s + OFF_SOLDIER_POSITION), *fp = flag_pos(b->targetFlag);
    float minTrip = chance ? 200.0f : DRIVE_MIN_TRIP, findR = chance ? 60.0f : DRIVE_FIND_RADIUS;
    if (dist2(me, fp) < minTrip * minTrip) return;
    int team = player_team(pl), other = 3 - team;
    char **vehs, *best = NULL;
    int nv = vehicle_list(&vehs);
    float bestD = findR * findR;
    for (int k = 0; k < nv; k++) {
        char *v = vehs[k];
        if (!vehicle_ok(v) || !vehicle_is_light(v) || !driver_seat_free(v) || vehicle_bad(v)) continue;
        if (chance && vehicle_is_boat(v)) continue;   // (on the way: ground vehicles)
        int vt = 0;
        float *vp = vehicle_pos(v, &vt);
        if (!vp || (vt && vt != team) || human_near(vp, g_enhanced.humanRadius)) continue;
        float d2 = dist2(vp, me);
        if (d2 >= bestD) continue;
        if (!chance && g_hqPos[team][0] != 0 && g_hqPos[other][0] != 0 && dist2(vp, g_hqPos[team]) > dist2(vp, g_hqPos[other])) continue;   // their side
        int taken = 0;
        for (int j = 0; j < H->botCount && !taken; j++) if (j != i && g_drive[j].phase && g_drive[j].veh == v) taken = 1;
        if (taken) continue;
        bestD = d2; best = v;
    }
    if (!best) { if (!chance) g_driveCooldown[i] = H->frame + 10 * 30; return; }
    g_vehChanceUntil[i] = 0;
    float *vp = vehicle_pos(best, NULL);
    if (!plan_to_vehicle(i, s, vp)) { if(!vehicle_plan_deferred(i))g_driveCooldown[i] = H->frame + 20 * 30; return; }
    DriveState *d = &g_drive[i];
    memset(d, 0, sizeof *d);
    d->phase = 1; d->veh = best; d->at = H->frame; d->flag = b->targetFlag;
    d->walkUntil = H->frame + fleet_walk_frames(sqrtf(bestD));
    memcpy(d->start, vp, 12);
    memcpy(d->goal, fp, 12);
    bot_nav(i)->mode = 3;
    bot_log("drive: %s goes for the %s %.0f m away (flag %d is %.0f m off)", pl + OFF_PLAYER_ONLINE_NAME, vehicle_name(best), sqrtf(bestD),
            b->targetFlag, sqrtf(dist2(me, fp)));
}
#include "ground_tactics_native.inc"
#include "drive_staging.inc"
#include "ground_jobs.inc"
#include "ground_live_exit.inc"
#include "ground_progress.inc"
#include "drive_maneuvers.inc"
#include "drive_orders.inc"
#include "ground_combat.inc"
// AltBotMod V11: a damaged bot-driven vehicle holds still (up to 25 s) while a friendly engineer is on its way to repair
// it or repairing it (V10: 9 of 17 repairs ended "vehicle drove off" - the driver left as the engineer arrived).
static int drive_repair_hold(int i,DriveState *d,const float *vp)
{
    if(d->manual || d->phase!=3 || vehicle_is_boat(d->veh)){d->repairHoldAt=0;return 0;}
    VehTrack *t=vehicle_sample(d->veh);
    float h=*(float *)(d->veh+OFF_VEHICLE_HEALTH);
    int coming=0;
    if(t && t->maxSeen>0 && h>0 && h<0.95f*t->maxSeen)
        for(int j=0;j<H->botCount && !coming;j++){
            BotBrain *o=bot_brain(j);
            if(j!=i && o->task==TASK_REPAIR && o->serviceTarget==d->veh && bot_distance_to(j,vp)<40)coming=1;
        }
    if(!coming){d->repairHoldAt=0;return 0;}
    if(!d->repairHoldAt){d->repairHoldAt=H->frame?H->frame:1;diagnostic_note(i,"holds still for an engineer coming to repair it",d->veh);}
    return H->frame-d->repairHoldAt<25*30;
}

// Every frame for a bot with a drive in progress (before steer_bot).
// Caspian V5 boats. Leaving a slip, the old way steered away from the mean of the land within 30 m: inside the US
// carrier's well deck on Noshahr Canals the land is all round the boat, its mean sits on the boat, and the RHIB pushed
// at the deck walls for 20 s (then drove its route back into the bay). Now 24 rays round the hull find the way out -
// the longest clear run, best if it ends over open water - and the boat drives (or backs) along it.
static float boat_exit_heading(int i, DriveState *d, const float *vp, float heading)
{
    NavGrid *g = *nav_slot();
    const unsigned char *mask = boat_mask();
    void *ownBody = !IsBadReadPtr(d->veh + 0x30, 4) ? *(void **)(d->veh + 0x30) : NULL;
    unsigned saved = g_rayFlags;
    g_rayFlags = RAY_DONT_CHECK_CHARACTER | RAY_CHECK_DETAIL_MESH;
    float best = heading, bestScore = -1e30f, bestFree = 0;
    for (int k = 0; k < 24; k++) {
        float a = k * (6.2831853f / 24), fx = sinf(a), fz = cosf(a), freeD = 70.0f;
        for (int up = 0; up < 2; up++) {
            float from[3] = {vp[0] + fx * 1.0f, vp[1] + (up ? 2.2f : 1.2f), vp[2] + fz * 1.0f}, hit[3], nrm[3];
            for (int tries = 0; tries < 3; tries++) {
                float to[3] = {vp[0] + fx * 70.0f, from[1], vp[2] + fz * 70.0f};
                if (!ray_cast(from, to, hit, nrm)) break;
                char *body = *(char **)g_lastHitRest;
                unsigned bvt = body && !IsBadReadPtr(body, 4) ? *(unsigned *)body : 0;
                if (body == ownBody || bvt == ADDR_WATER_PHYSICS_VTABLE || hypotf(hit[0] - vp[0], hit[2] - vp[2]) < 2.5f) {   // its own hull / a wave: look on past it
                    from[0] = hit[0] + fx * 0.5f; from[2] = hit[2] + fz * 0.5f; continue;
                }
                float dd = hypotf(hit[0] - vp[0], hit[2] - vp[2]);
                if (dd < freeD) freeD = dd;
                break;
            }
        }
        float e = a - heading;
        while (e > 3.14159265f) e -= 6.2831853f;
        while (e < -3.14159265f) e += 6.2831853f;
        float score = freeD - fabsf(e) * 3.0f, ow[3];   // (a slight preference for the way it already points)
        if (g && mask && freeD > 30.0f && nav_open_water_near(g, mask, vp[0] + fx * 40.0f, vp[2] + fz * 40.0f, 8.0f, ow)) score += 25.0f;
        if (score > bestScore) { bestScore = score; best = a; bestFree = freeD; }
    }
    g_rayFlags = saved;
    float turn = best - d->exitHeading;
    while (turn > 3.14159265f) turn -= 6.2831853f;
    while (turn < -3.14159265f) turn += 6.2831853f;
    if (!d->exitSet || fabsf(turn) > 0.5f)
        bot_log("drive: %s's way out of the slip: heading %.0f deg, %.0f m clear", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME,
                best * 57.29578f, bestFree);
    return best;
}
static int boat_ashore(int i, DriveState *d, const float *vp, const char *why)
{
    vehicle_mark_bad(d->veh, 180);   // (landed: the rest is on foot / by land vehicle)
    if (armor_arrive(i, vp)) return 1;
    char msg[96];
    snprintf(msg, sizeof msg, "lands - everyone gets out (%s)", why);
    drive_stop(i, msg, 20);
    return 1;
}
// A boat at its landing, or run up against a shore about as near its flag as the landing: the trip is over. (The old
// code only stopped at the last waypoint of a complete route - a boat 25 m short with its bow on the beach replanned
// for a minute and nobody got out.)
static int boat_landed(int i, DriveState *d, const float *vp)
{
    if (!d->landSet || d->phase == 8) return 0;
    float toLand = hypotf(vp[0] - d->landTgt[0], vp[2] - d->landTgt[2]);
    if (toLand < 40.0f) return boat_ashore(i, d, vp, "at the landing");
    if (d->exitSet && hypotf(vp[0] - d->exitFrom[0], vp[2] - d->exitFrom[2]) < 80.0f) return 0;   // (still by its slip)
    NavGrid *g = *nav_slot();
    const unsigned char *mask = boat_mask();
    float mx, mz, nearest = 1e30f;
    if (!g || !mask || !nav_land_around(g, mask, vp[0], vp[2], 14.0f, &mx, &mz, &nearest) || nearest > 12.0f) return 0;
    float landToFlag = hypotf(d->landTgt[0] - d->goal[0], d->landTgt[2] - d->goal[2]), meToFlag = hypotf(vp[0] - d->goal[0], vp[2] - d->goal[2]);
    // (Review: the bow sensor - not any front ray, which also sees other boats and bridge pillars - must be on
    // something with the boat nearly stopped; while replanning (phase 2) only the landing's own surroundings count.)
    if (meToFlag < landToFlag + 90.0f && (toLand < 90.0f || (d->phase != 2 && d->midNear < 6.0f && d->speed < 3.0f)))
        return boat_ashore(i, d, vp, "ran up on the shore near the landing");
    if (d->phase == 2 && d->boatPlanSince && H->frame - d->boatPlanSince > 25 * 30 && meToFlag < landToFlag + 150.0f)
        return boat_ashore(i, d, vp, "no way on by water - the rest on foot");
    return 0;
}
// AltBotMod V8: no driving map yet (a new map: the grid survey takes minutes) and no Fun-Bots vehicle route from here:
// drive the straight line towards the objective in 20 m legs, up to 160 m at a time (a partial route - then it looks
// again, by when it may be on a recorded route). The bumper sensors, the swerve and the unstick handling deal with what
// is in the way. Every leg end is checked from above: a land vehicle's line ends before water deeper than a ford or a
// drop / climb steeper than ~40 deg; a boat's line ends on the shore (its landing - everyone out). V7 parked the crews
// at their HQ instead ("driving map not ready"), where they spent the survey shooting at helicopters.
#define STRAIGHT_LEG 20.0f
#define STRAIGHT_MAX 160.0f
static unsigned g_straightRoutes;
static int drive_straight_route(int i, DriveState *d, const float *vp, int boat)
{
    if (isnan(d->goal[0]) || isnan(d->goal[2])) return 0;
    float dx = d->goal[0] - vp[0], dz = d->goal[2] - vp[2], len = hypotf(dx, dz);
    if (!isfinite(len) || len < 10.0f) return 0;
    float ux = dx / len, uz = dz / len, run = fminf(len, STRAIGHT_MAX), prevY = vp[1];
    int n = 0, cut = 0;
    unsigned saved = g_rayFlags; g_rayFlags = RAY_DONT_CHECK_CHARACTER;
    for (float s = STRAIGHT_LEG; n < DRIVE_MAX_PTS; s += STRAIGHT_LEG) {
        if (s > run) s = run;
        float x = vp[0] + ux * s, z = vp[2] + uz * s, a[3] = {x, prevY + 30.0f, z}, b[3] = {x, prevY - 30.0f, z}, h[3];
        if (!ray_cast(a, b, h, NULL)) { cut = 1; break; }
        int water = nav_hit_class() == NAV_HIT_WATER;
        float y = h[1];
        if (water && !boat) {   // a ford? the bed under the surface
            float a2[3] = {x, h[1] - 0.05f, z};
            if (!ray_cast(a2, b, h, NULL) || y - h[1] >= FORD_MAX_DEPTH) { cut = 1; break; }
            y = h[1];
        }
        if (!boat && fabsf(y - prevY) > STRAIGHT_LEG * 0.8f) { cut = 1; break; }
        d->pts[n * 3] = x; d->pts[n * 3 + 1] = y; d->pts[n * 3 + 2] = z; n++; prevY = y;
        if (boat && !water) { cut = 2; break; }   // the shore: the landing
        if (s >= run) break;
    }
    g_rayFlags = saved;
    if (!n) return 0;
    d->n = n; d->partial = cut != 2 && (cut || run < len);
    if (cut == 1 && n < 2) return 0;   // water / a cliff right ahead: not this way
    if (boat) { memcpy(d->landTgt, &d->pts[(n - 1) * 3], 12); d->landSet = 1; }
    d->next = 0; d->phase = 3; d->at = d->checkAt = H->frame; d->stuckCount = 0; d->routeFailures = 0; d->boatPlanSince = 0;
    d->stageCandidate = 0; d->stageActive = 0; d->straight++; d->straightRoute = 1; d->funRoute = 0;
    memset(&g_enhancedSteer[i], 0, sizeof g_enhancedSteer[i]);
    memcpy(d->lastPos, vp, 12);
    g_straightRoutes++;
    if (d->straightLogAt && H->frame - d->straightLogAt < 15 * 30) return 1;   // V9: (V8 logged every replan - 515 lines on Nebandan)
    d->straightLogAt = H->frame ? H->frame : 1;
    bot_log("drive: %s has no driving map yet - drives straight at flag %d (%d legs, %.0f of %.0f m%s)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME,
            d->flag, n, sqrtf(dist2(vp, &d->pts[(n - 1) * 3])), len, cut == 2 ? ", to the shore" : cut ? ", stops short of water / a drop" : "");
    return 1;
}
// AltBotMod V14: the driving map has no way to the flag (Seine Crossing: the RU tank sat at its base all round, "no
// drivable route ... A* exhausted", 30+ times, after two Fun-Bots routes were used up for the whole life of the tank).
// First the Fun-Bots vehicle routes near the vehicle again (the nearest, then up to three starting from other roads,
// searched further out each time), then - after 45 s without a route -
// checked straight legs at the flag (at most every 30 s), so a tank always gets going and into the fight.
static int drive_route_fallback(int i, DriveState *d, const float *vp)
{
    if (d->manual || g_armor[i].job != ARMOR_NORMAL || d->flag < 0 || d->flag >= objective_count()) return 0;
    if (!d->noRouteSince) d->noRouteSince = H->frame ? H->frame : 1;
    const float *fp = objective_pos(d->flag);
    while (fp && d->traceAlt < 4) {
        int part = 0; float len = 0;
        int alt = d->traceAlt++;   // 0: the nearest route again (it may only have been blocked), then the others
        int tn = trace_vehicle_route(0, d->flag, fp, vp, d->pts, DRIVE_MAX_PTS, &part, &len, alt);
        if (!tn) continue;
        d->n = tn; d->partial = part; d->straightRoute = 0; d->funRoute = 1; d->joinN = 0;
        d->next = 0; d->phase = 3; d->at = d->checkAt = H->frame; d->stuckCount = 0; d->routeFailures = 0;
        d->stageCandidate = 0; d->stageActive = 0;
        memset(&g_enhancedSteer[i], 0, sizeof g_enhancedSteer[i]);
        memcpy(d->lastPos, vp, 12);
        bot_log("drive: %s takes a Fun-Bots vehicle route (try %d) towards flag %d (%d points, %.0f m) - the driving map has no way there",
                (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, alt, d->flag, tn, len);
        return 1;
    }
    if (H->frame - d->noRouteSince >= 45 * 30 && (!d->straightFallbackAt || H->frame - d->straightFallbackAt >= 30 * 30)) {
        d->straightFallbackAt = H->frame ? H->frame : 1;
        if (drive_straight_route(i, d, vp, 0)) {
            bot_log("drive: %s has had no route for %u s - drives checked straight legs at flag %d", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME,
                    (H->frame - d->noRouteSince) / 30, d->flag);
            return 1;
        }
    }
    return 0;
}

// AltBotMod V8: between two legs of a long trip the vehicle keeps rolling for up to 1.5 s - straight ahead, light
// throttle, only while the bumper sensors saw nothing within 15 m - instead of stopping dead while the next leg is planned.
// AltBotMod V25: onto the road by the driving grid. The recorded road can start up to 90 m off (V23); V23-V24 drove
// straight at it, through whatever stood between (V24 live: nosed up against walls and parked tanks on the way to the
// road). When the nearest point of the road is more than 15 m off and the straight way there is not clear, an A* path
// over the driving map - which has the learned stuck spots struck off - leads to it, and the road follows.
// Returns the grid points put in front of the road (0: none needed / none found), -1: the A* is still running.
static int drive_route_join(int i, DriveState *d, const float *vp, int *tnp)
{
    NavGrid *jg = *nav_slot(); const unsigned char *jm = drive_mask();
    int tn = *tnp, k0 = bm_route_progress(d->pts, tn, 0, vp, NULL, 6.0f);
    if (k0 >= tn) k0 = tn - 1;
    if (jg && jm && dist2(vp, &d->pts[k0 * 3]) > 15.0f * 15.0f) {
        static float join[64 * 3];
        nav_set_path_mask(jm); nav_set_vehicle_width(bm_vehicle_profile(vehicle_name(d->veh))->halfWidth + 0.15f);
        int direct = nav_direct_route(jg, drive_nav_ray, vp, &d->pts[k0 * 3]);
        int jn = direct ? 0 : budgeted_nav_path(i + MAX_BOTS, jg, drive_nav_ray, vp[0], vp[1], vp[2], d->pts[k0 * 3], d->pts[k0 * 3 + 1], d->pts[k0 * 3 + 2], join, 64);
        nav_set_path_mask(NULL); nav_set_vehicle_width(1.5f);
        if (!direct && !jn && g_lastPlanDeferred) return -1;   // (the A* runs over the next ticks)
        if (jn > 1) {
            int rest = tn - k0;
            if (jn - 1 + rest > DRIVE_MAX_PTS) rest = DRIVE_MAX_PTS - (jn - 1);
            memmove(d->pts + (jn - 1) * 3, d->pts + k0 * 3, sizeof(float) * 3 * rest);
            memcpy(d->pts, join, sizeof(float) * 3 * (jn - 1));   // (its last point is the road's own)
            *tnp = jn - 1 + rest; g_routeJoins++; return jn - 1;
        }
    }
    return 0;
}
// (AltBotMod V27's drive_wait_turn - one vehicle at a time - is gone in V29: it made whole convoys stop and wait for one
// another, ~600 times in 16 min live; drive_lane_scan follows and passes instead)
static void drive_coast(int i, DriveState *d)
{
    // AltBotMod V25: a re-plan while driving keeps the last command (throttle and steering) for up to 1.5 s with nothing in
    // the way. V6-V24 cut the throttle to 0 for every re-plan, and drivers re-planned often (V24 live: 939 route plans in
    // 16 min) - each one a lift off the throttle: stop, go, stop, go.
    // (V29: up to 3 s, was 1.5 s; and the last throttle is kept even if a director / job change cleared the input this tick)
    d->statOffPhase++;
    if (d->drivenAt && H->frame - d->drivenAt < 90 && d->speed > 2.0f && d->brakeNear > 6.0f && H->frame >= d->reverseUntil &&
        H->frame >= d->backOutUntil && d->lastThrottle > 0.0f) {
        H->botMove[i][0] = d->lastThrottle; d->checkAt = H->frame; return;
    }
    if (H->frame < d->coastUntil && d->speed > 3.0f && d->frontNear > 15.0f && H->frame >= d->reverseUntil && H->frame >= d->backOutUntil) {
        H->botMove[i][0] = d->speed > 9.0f ? 0.0f : 0.3f; H->botMove[i][1] = 0; H->botMove[i][2] = 0;
        d->checkAt = H->frame;
        return;
    }
    memset(H->botMove[i], 0, sizeof H->botMove[i]);
}
// AltBotMod V29: traffic by geometry, not by sensors. Every vehicle in this one's lane - straight ahead up to 15 m, then on
// its route up to 25 m + 2.5 s of its speed ahead, within both half-widths and 0.8 m of the line, at its height - limits its
// speed (bm_lane_limit): it closes on it at 3.5 m/s^2 at most and stops 6 m short of a stopped one. A vehicle beside the
// road, behind, or round a bend off the route limits nothing (V25-V28 braked for whatever the bumper rays met, and V27 made
// whole convoys wait for one another: ~600 waits in 16 min live). One that is slow (under 3 m/s) and has held this one up
// for 1.5 s is passed: the steering aims 1 m clear of its side - the side it stands less on (dead centre: the right) -
// creeping (2.5 m/s) while still pulling out beside it, then back onto the road past it.
static unsigned g_lanePasses, g_driveDrops;
static void drive_lane_scan(int i, DriveState *d, const float *vp, const float *xf, const BMVehicleProfile *profile)
{
    float hx = xf[8], hz = xf[10], hl = hypotf(hx, hz);
    d->laneLimit = 99.0f; d->laneAt = H->frame ? H->frame : 1;
    if (hl < 0.1f) { d->laneBlocker = NULL; return; }
    hx /= hl; hz /= hl;
    float fwd2[2] = {hx, hz}, range = 25.0f + 2.5f * d->speed, best = 99.0f, bestLat = 0, bestHalf = 0, bestOwnSpeed = 0;
    char *blocker = NULL, **vl; int nv = vehicle_list(&vl);
    for (int k = 0; k < nv && k < 256; k++) {
        char *o = vl[k];
        if (!o || o == d->veh || !vehicle_ok(o)) continue;
        float *op = vehicle_pos(o, NULL);
        if (!op || fabsf(op[1] - vp[1]) > 4.0f || dist2(op, vp) > (range + 10.0f) * (range + 10.0f)) continue;   // (an aircraft overhead, a bridge above)
        const BMVehicleProfile *op2 = bm_vehicle_profile(vehicle_name(o));
        float half = profile->halfWidth + (op2 && op2->halfWidth > 0 ? op2->halfWidth : 1.2f) + 0.8f, along = 0, lat = 0;
        if (d->passVeh == o) {   // passed it: back onto the road
            float ex = op[0] - vp[0], ez = op[2] - vp[2];
            if (ex * hx + ez * hz < -2.0f) { d->passUntil = 0; d->passVeh = NULL; }
        }
        if (!bm_lane_relation(d->pts, d->n, d->next, vp, fwd2, op, half, range, &along, &lat)) continue;
        float ospeed = 0;
        for (int j = 0; j < H->botCount && j < MAX_BOTS; j++) if (g_drive[j].veh == o && g_drive[j].phase >= 2 && j != i) { ospeed = g_drive[j].speed; break; }
        if (ospeed <= 0) { VehTrack *t = vehicle_sample(o); ospeed = t ? t->speed : 0; }
        float *ox = vehicle_xf(o), toward = 1.0f;
        if (ox) { float ol = hypotf(ox[8], ox[10]); if (ol > 0.1f) toward = (ox[8] * hx + ox[10] * hz) / ol; }
        float along2 = ospeed * toward;   // its speed along this one's way (negative: coming this way)
        float lim = bm_lane_limit(along, along2, profile->frontLead);
        if (d->passUntil > H->frame && d->passVeh == o) lim = (fabsf(lat) < half && along < profile->frontLead + 8.0f) ? fminf(fmaxf(lim, 2.5f), 2.5f) : fmaxf(lim, 6.0f);
        if (lim < best) { best = lim; blocker = o; bestLat = lat; bestHalf = half; bestOwnSpeed = along2; }
    }
    if (blocker != d->laneBlocker) { d->laneBlocker = blocker; d->laneSince = H->frame ? H->frame : 1; }
    if (!blocker) { d->laneSince = 0; if (d->passUntil && H->frame >= d->passUntil) { d->passUntil = 0; d->passVeh = NULL; } return; }
    d->laneLimit = best;
    // held up by a slow / stopped vehicle for 1.5 s: pass it
    if (bestOwnSpeed < 3.0f && d->laneSince && H->frame - d->laneSince > 45 && d->passUntil <= H->frame) {
        d->passSide = (signed char)(bestLat > 0.5f ? -1 : 1);   // it stands right of the line: pass on the left; else on the right
        d->passLat = fmaxf(1.5f, bestHalf + 1.0f - fabsf(bestLat));
        d->passUntil = H->frame + 10 * 30; d->passVeh = blocker;
        g_lanePasses++;
        if (g_lanePasses % 10 == 1)
            bot_log("drive: %s passes the %s holding up its lane, on the %s (%u passes so far)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME,
                    vehicle_name(blocker), d->passSide > 0 ? "right" : "left", g_lanePasses);
    }
}
static void drive_tick(int i)
{
    DriveState *d = &g_drive[i];
    char *pl = H->bots[i], *s = *(char **)(pl + OFF_PLAYER_SOLDIER);
    if (!d->phase) return;
    if (g_driveOff || !g_enhanced.driving) { drive_stop(i, "driving disabled", 30); return; }
    if (!level_ready()) { H->botMove[i][0] = H->botMove[i][1] = H->botMove[i][2] = 0; return; }
    if (!s || (!d->manual && !bot_brain(i)->enabled)) { drive_stop(i, "stops driving (dead / switched off)", 10); return; }
    if (!vehicle_ok(d->veh)) { drive_stop(i, "lost the vehicle (destroyed)", 20); return; }
    if (!d->manual && d->phase != 7 && !vehicle_is_light(d->veh)) { drive_stop(i, "vehicle class disabled", 30); return; }
    float *vp = vehicle_pos(d->veh, NULL);
    if (!vp) { drive_stop(i, "lost the vehicle", 20); return; }
    if (d->phase == 7) {   // riding: until the vehicle is left
        if (player_vehicle(pl) != d->veh) { d->phase = 0; d->veh = NULL; choose_local_objective(i, "got out of the squad's vehicle"); return; }
        H->botMove[i][0] = H->botMove[i][1] = H->botMove[i][2] = 0;
        return;
    }
    if (d->phase == 5) {   // squad boarding: walk up; first one in takes the wheel
        if (H->frame - d->at > 30 * 30) { drive_stop(i, "could not reach the squad's vehicle", 20); return; }
        if (!driver_seat_free(d->veh) && !free_seat_count(d->veh)) { drive_stop(i, "finds the vehicle full", 20); return; }
        float reach = vehicle_is_boat(d->veh) ? 9.0f : VEHICLE_BOARD_DIST;
        if (!vehicle_finish_approach(i,s,vp,reach)) return;
        int seat = driver_seat_free(d->veh) ? 0 : free_seat(d->veh);
        if (seat < 0) { drive_stop(i, "finds the vehicle full", 20); return; }
        ((EnterVehicle_t)ADDR_PLAYER_ENTER_VEHICLE)(pl, d->veh, seat);
        if (player_vehicle(pl) != d->veh) { drive_stop(i, "could not get in", 20); return; }
        bot_nav(i)->mode = 0;
        H->botMove[i][0] = H->botMove[i][1] = H->botMove[i][2] = 0;
        if (seat == 0) { g_driving[i] = 1; d->phase = 6; d->at = H->frame; bot_log("drive: %s takes the wheel of the %s", pl + OFF_PLAYER_ONLINE_NAME, vehicle_name(d->veh)); }
        else { d->phase = 7; bot_log("drive: %s climbs into seat %d of the %s", pl + OFF_PLAYER_ONLINE_NAME, seat, vehicle_name(d->veh)); }
        return;
    }
    if (d->phase == 6) {   // at the wheel: one three-second boarding window, then depart
        if (player_vehicle(pl) != d->veh) { drive_stop(i, "is out of the vehicle", 20); return; }
        H->botMove[i][0] = H->botMove[i][1] = H->botMove[i][2] = 0;
        // Invite nearby living squadmates. Dead or distant members cannot hold
        // the driver at deployment, and this window never restarts on replans.
        int waiting = 0, team = player_team(pl), squad = player_squad(pl), seats = free_seat_count(d->veh);
        for (int j = 0; j < H->botCount && seats > 0; j++) {
            char *jp = H->bots[j], *js = *(char **)(jp + OFF_PLAYER_SOLDIER);
            if (j == i || player_team(jp) != team || squad <= 0 || player_squad(jp) != squad || player_vehicle(jp) == d->veh) continue;
            if (!bot_brain(j)->enabled || H->frame < g_driveCooldown[j]) continue;
            if (!js) continue;
            if (g_drive[j].phase == 5 && g_drive[j].veh == d->veh) { waiting++; continue; }
            if (g_drive[j].phase || (g_fleet[j].vehicle && H->frame<g_fleet[j].until) ||
                player_vehicle(jp) || bot_brain(j)->task != TASK_NONE) continue;
            if (dist2((float *)(js + OFF_SOLDIER_POSITION), vp) > (SQUAD_VEH_RADIUS + 20.0f) * (SQUAD_VEH_RADIUS + 20.0f)) continue;
            if (!plan_to_vehicle(j, js, vp)) continue;
            DriveState *dj = &g_drive[j];   // a squadmate that spawned after the others boarded: over here
            memset(dj, 0, sizeof *dj);
            dj->phase = 5; dj->veh = d->veh; dj->at = H->frame; dj->flag = d->flag;
            memcpy(dj->start, vp, 12);
            memcpy(dj->goal, d->goal, 12);
            bot_nav(j)->mode = 3;
            waiting++;
            bot_log("squad: %s waits for squadmate %s to board", pl + OFF_PLAYER_ONLINE_NAME, jp + OFF_PLAYER_ONLINE_NAME);
        }
        if (waiting && seats > 0 && H->frame - d->at < 3 * 30) return;
        d->phase = 2; d->boardAt = d->at ? d->at : 1;
        return;
    }
    if (d->phase == 1) {   // walking up to it
        if (d->walkUntil ? H->frame >= d->walkUntil : H->frame - d->at > 35 * 30) { drive_stop(i, "gave up walking to the vehicle", 30); return; }
        if (!driver_seat_free(d->veh) || dist2(vp, d->start) > 10.0f * 10.0f) { drive_stop(i, "found the vehicle taken", 20); return; }
        float reach = vehicle_is_boat(d->veh) ? 9.0f : VEHICLE_BOARD_DIST;
        if (!vehicle_finish_approach(i,s,vp,reach)) return;
        ((EnterVehicle_t)ADDR_PLAYER_ENTER_VEHICLE)(pl, d->veh, 0);
        if (player_vehicle(pl) != d->veh) { drive_stop(i, "could not get in", 30); return; }
        g_driving[i] = 1;
        bot_nav(i)->mode = 0;
        d->phase = 2;
        bot_log("drive: %s is at the wheel of the %s", pl + OFF_PLAYER_ONLINE_NAME, vehicle_name(d->veh));
        return;
    }
    if (player_vehicle(pl) != d->veh) { drive_stop(i, "is out of the vehicle", 20); return; }
    if(g_mode==BM_MODE_RUSH && !d->manual && d->phase!=5 && d->phase!=7 && d->phase!=9){
        int f=d->flag;
        if(!objective_wanted(player_team(pl),f) || !objective_pos(f) || dist2(d->goal,objective_pos(f))>50*50){
            f=mode_vehicle_target(i,vp);
            if(f<0){memset(H->botMove[i],0,sizeof H->botMove[i]);return;}
            d->flag=f;memcpy(d->goal,objective_pos(f),12);d->phase=2;
            d->n=d->next=d->partial=0;d->stageActive=d->stageCandidate=0;
            nav_cancel_path(i+MAX_BOTS);bot_brain(i)->targetFlag=f;
        }
    }
    if(drive_director_tick(i,d,vp))return;
    if(drive_unstick_tick(i,d,vp))return;   // Caspian V4
    drive_progress_tick(i,d,vp);
    if(drive_goal_watch(i,d,vp))return;   // AltBotMod V17
    if(mode_is_cq() && !d->manual && d->phase>=2 && d->phase!=5 && d->phase!=7 && d->phase!=9 &&
       d->flag>=0 && d->flag<objective_count() && !objective_wanted(player_team(pl),d->flag)){
        int hostile=0;for(int f=0;f<objective_count();f++)hostile+=objective_wanted(player_team(pl),f);
        if(hostile && drive_other_objective(i,d,vp,0))return;
    }
    if(drive_reverse_tick(i,d,vp))return;
    if(armor_tick(i,vp))return;
    if(d->phase==10){
        memset(H->botMove[i],0,sizeof H->botMove[i]);
        VehTrack *track=vehicle_sample(d->veh);
        if(track && track->maxSeen>0 && *(float *)(d->veh+OFF_VEHICLE_HEALTH)<g_enhanced.bailHealth*track->maxSeen){drive_stop(i,"bails out - the vehicle is nearly wrecked",20);return;}
        if(H->frame<d->routeRetryAt)return;
        d->phase=2;d->routeFailures=0;d->at=H->frame;
    }
    if(d->phase==4){drive_hold_objective(i,d,vp);return;}
    if (d->phase == 2 && !d->boardAt) {   // "Get in!" - teammates nearby take the free seats; 3 s to climb in
        d->boardAt = H->frame ? H->frame : 1;
        bot_say(i, ORDER_GETIN);
        order_reaction(pl, ORDER_GETIN, NULL);
        H->botMove[i][0] = H->botMove[i][1] = H->botMove[i][2] = 0;
        return;
    }
    if (d->phase == 2 && H->frame - d->boardAt < 90) return;
    if (d->phase == 2 && vehicle_is_boat(d->veh) && !d->backedOut) { d->phase = 8; d->at = H->frame; d->backedOut = 1; }
    if (d->phase == 8) {   // a boat leaving its mooring: along the longest clear run out of the slip (boat_exit_heading),
        // until it is 8 m clear of land or well away from where it started; then the route
        NavGrid *g = *nav_slot();
        const unsigned char *mask = boat_mask();
        float *xf = vehicle_xf(d->veh), mx = 0, mz = 0, nearest = 1e30f;
        int nl = g && mask ? nav_land_around(g, mask, vp[0], vp[2], 30.0f, &mx, &mz, &nearest) : 0;
        if (!d->exitSet) { memcpy(d->exitFrom, vp, 12); }
        float moved = hypotf(vp[0] - d->exitFrom[0], vp[2] - d->exitFrom[2]);
        if (!xf || !nl || (nearest >= 8.0f && moved > 6.0f) || moved > 45.0f || H->frame - d->at > 25 * 30) {
            bot_log("drive: %s's boat is %s (%.0f m from land, %.0f m out) - plotting the route", pl + OFF_PLAYER_ONLINE_NAME,
                    !nl || nearest >= 8.0f || moved > 45.0f ? "clear of the slip" : "still close in after 25 s", nl ? nearest : 99.0f, moved);
            d->phase = 2;
            H->botMove[i][0] = H->botMove[i][1] = H->botMove[i][2] = 0;
            return;
        }
        float heading = atan2f(xf[8], xf[10]);
        if (!d->exitSet || H->frame - d->exitProbeAt >= 45) {   // (re-looked every 1.5 s as it swings round)
            d->exitHeading = boat_exit_heading(i, d, vp, heading);
            d->exitSet = 1;
            d->exitProbeAt = H->frame;
        }
        float err = d->exitHeading - heading;
        while (err > 3.14159265f) err -= 6.2831853f;
        while (err < -3.14159265f) err += 6.2831853f;
        float throttle, steer;
        if (fabsf(err) > 1.75f) {   // the way out is behind: back out along it (reversing steers the other way)
            float back = err > 0 ? err - 3.14159265f : err + 3.14159265f;   // the stern's error
            throttle = -0.8f;
            steer = fmaxf(-1.0f, fminf(1.0f, -back * 1.5f));
        } else {
            throttle = fabsf(err) > 0.5f ? 0.5f : 0.9f;
            steer = fmaxf(-1.0f, fminf(1.0f, -err * 1.5f));
        }
        H->botMove[i][0] = throttle; H->botMove[i][1] = 0; H->botMove[i][2] = steer;
        return;
    }
    if (d->phase == 2) {   // route on the drivable grid
        // A queued route does not own the previous driving command (V8: except a short roll-on, straight, nothing near ahead)
        drive_coast(i,d);
        if(drive_clear_departure(i,d,vp))return;
        int boat = vehicle_is_boat(d->veh);
        if (boat) {
            if (!d->boatPlanSince) d->boatPlanSince = H->frame ? H->frame : 1;
            if (boat_landed(i, d, vp)) return;
        }
        if(!plan_budget_left()){drive_coast(i,d);return;}
        // AltBotMod V6: a Fun-Bots vehicle route first (V7: also before the driving map exists - a new map) (recorded by people driving that road / boat lane), the
        // drivable grid when there is none near or it was tried twice this trip.
        // AltBotMod V16: recorded routes are the rule, the grid the exception - up to 6 tries a trip (V6-V15: 2), and a
        // recorded route that was cut short (it goes on beyond 256 points) is always continued on recorded roads.
        if (!d->manual && (d->traceContinue || d->traceTries < 40 || (d->traceTries < 80 && !(boat ? boat_mask() : drive_mask()))) && g_armor[i].job == ARMOR_NORMAL && d->flag >= 0 && d->flag < objective_count()) {
            const float *fp = objective_pos(d->flag);
            int part = 0; float len = 0;
            int alt = d->traceContinue ? 0 : d->traceTries >= 2 ? (d->traceTries - 1) % 4 : 0;   // (later tries start from other roads)
            int tn = fp ? trace_vehicle_route(boat, d->flag, fp, vp, d->pts, DRIVE_MAX_PTS, &part, &len, alt) : 0;
            if (!tn && alt) tn = trace_vehicle_route(boat, d->flag, fp, vp, d->pts, DRIVE_MAX_PTS, &part, &len, 0);
            d->traceContinue = 0;
            if (tn) {
                // (V25: a plan is not a try - V16-V24 counted every route taken, and with re-plans every few seconds a driver
                // used up its tries and dropped to the driving grid for the rest of the trip: "use routes for a bit, then
                // get back on". Tries are counted only where it got stuck on a road - drive_goal_watch / the stuck checks.)
                d->traceContinue = part;
                int joinN = 0;   // AltBotMod V25: onto the road by the driving grid when the straight way to it is blocked (drive_route_join)
                if (!boat) { int r = drive_route_join(i, d, vp, &tn); if (r < 0) { d->traceContinue = 0; return; } joinN = r; }   // (V25: deferred A* -> next tick)
                d->n = tn; d->partial = part; d->straightRoute = 0; d->funRoute = 1; d->joinN = joinN;
                if (joinN) bot_log("drive: %s joins the Fun-Bots road by %d grid point(s) (the straight way to it is blocked)", pl + OFF_PLAYER_ONLINE_NAME, joinN);
                if (boat) { memcpy(d->landTgt, &d->pts[(tn - 1) * 3], 12); d->landSet = 1; }
                d->next = 0; d->phase = 3; d->at = d->checkAt = H->frame; d->stuckCount = 0; d->routeFailures = 0; d->boatPlanSince = 0;
                d->stageCandidate = 0; d->stageActive = 0; d->statPlans++;
                memset(&g_enhancedSteer[i], 0, sizeof g_enhancedSteer[i]);
                memcpy(d->lastPos, vp, 12);
                // AltBotMod V26: a re-plan storm (6+ plans of a road in 10 s) is logged with the reason for the last re-plan;
                // the plan lines themselves are cut to one per 5 s per driver (V25 live: 2,286 of them in 26 min)
                if (!d->planWindowAt || H->frame - d->planWindowAt > 10 * 30) { d->planWindowAt = H->frame ? H->frame : 1; d->planCount = 0; }
                if (++d->planCount == 6 && (!d->stormLogAt || H->frame - d->stormLogAt > 30 * 30)) {
                    d->stormLogAt = H->frame ? H->frame : 1; g_replanStorms++;
                    bot_log("drive: WARNING %s re-planned its road 6 times in %.1f s - last reason: %s (%u storms so far)", pl + OFF_PLAYER_ONLINE_NAME,
                            (H->frame - d->planWindowAt) / 30.0f, d->why ? d->why : "(none recorded)", g_replanStorms);
                }
                if (d->planCount <= 1 || !d->planLogAt || H->frame - d->planLogAt > 5 * 30) {
                    d->planLogAt = H->frame ? H->frame : 1;
                    bot_log("drive: %s follows a Fun-Bots %s route towards flag %d (%d points, %.0f m)", pl + OFF_PLAYER_ONLINE_NAME,
                            boat ? "boat" : "vehicle", d->flag, tn, len);
                }
                d->why = NULL;
                return;
            }
        }
        NavGrid *g = *nav_slot();
        const unsigned char *mask = boat ? boat_mask() : drive_mask();
        if (!g || !mask) {
            if (!d->manual && g_armor[i].job == ARMOR_NORMAL && drive_straight_route(i, d, vp, boat)) return;   // AltBotMod V8
            if(!boat && !d->manual)drive_wait_route(i,d,"driving map not ready");else drive_stop(i,"has no drivable map",60);return;
        }
        static unsigned char *clearMask;   // boats: the mask without the shallows (except at the start and the landing)
        static size_t clearSize;
        nav_set_path_mask(mask);
        nav_set_vehicle_width(bm_vehicle_profile(vehicle_name(d->veh))->halfWidth+0.15f);
        // the drivable spot nearest the goal, at the goal's height (a town flag's own spot is often too cramped); a boat:
        // the water nearest it (the beach run - the last bit is swum / walked)
        float tgt[3] = {d->goal[0], d->goal[1], d->goal[2]}, gy = isnan(d->goal[1]) ? vp[1] : d->goal[1];
        int have = 0;
        if (boat) {   // the landing: the beach of the flag's own landmass that best trades boat time for the walk
            int goalLab = g_landLabels ? nav_label_at(g, g_landLabels, d->goal[0], isnan(d->goal[1]) ? vp[1] : d->goal[1], d->goal[2]) : 0;
            if (goalLab) have = nav_landing_on(g, mask, g_landLabels, goalLab, vp[0], vp[2], d->goal[0], d->goal[2], 0.6f, 60.0f, tgt);
            if (!have) have = nav_mask_nearest(g, mask, d->goal[0], d->goal[2], 300.0f, tgt);
        }
        if (boat && !have) { nav_set_path_mask(NULL); vehicle_mark_bad(d->veh, 120); drive_stop(i, "has no water near its flag", 60); return; }
        if (boat && (!d->landSet || hypotf(tgt[0] - d->landTgt[0], tgt[2] - d->landTgt[2]) > 15.0f || H->frame - d->landLogAt > 20 * 30)) {
            bot_log("drive: %s lands the boat at (%.0f, %.0f), %.0f m from its flag", pl + OFF_PLAYER_ONLINE_NAME, tgt[0], tgt[2],
                    sqrtf(dist2(tgt, d->goal)));   // (once per landing - a deferred plan comes back here every tick)
            d->landLogAt = H->frame;
        }
        if (boat) { memcpy(d->landTgt, tgt, 12); d->landSet = 1; }
        if (boat) {
            size_t n = (size_t)g->w * g->h * NAV_LAYERS;
            if (clearSize != n) { if (clearMask) HeapFree(GetProcessHeap(), 0, clearMask); clearMask = HeapAlloc(GetProcessHeap(), 0, n); clearSize = clearMask ? n : 0; }
            if (clearMask) {
                float keep[4] = {vp[0], vp[2], tgt[0], tgt[2]};
                memcpy(clearMask, mask, n);
                nav_mask_strip(g, clearMask, 4, keep, 2, 40.0f);
                nav_set_path_mask(clearMask);
            }
        }
        d->stageActive=0;
        if(!boat && !d->manual && g_armor[i].job==ARMOR_NORMAL){
            int stage=drive_stage_target(i,g,mask,vp,tgt);
            if(stage<0){nav_set_path_mask(NULL);nav_set_vehicle_width(1.5f);return;}
            if(stage>0){have=1;d->stageActive=1;}
        }
        if(!boat && !have)have=nav_drive_near(g,mask,d->goal[0],d->goal[1],d->goal[2],120.0f,tgt);
        for (float r = 0; r <= 40.0f && !have; r += 10.0f)
            for (int a = 0; a < (r > 0 ? 8 : 1) && !have; a++) {
                float x = d->goal[0] + sinf(a * 0.785398f) * r, z = d->goal[2] + cosf(a * 0.785398f) * r;
                if (nav_snap(g, x, gy, z, tgt) && fabsf(tgt[1] - gy) < 4.0f) have = 1;
            }
        nav_set_long_legs(boat);
        nav_set_vehicle_avoid(H->frame<d->avoidUntil?d->avoidPosition:NULL,d->avoidRadius);
        double planT0 = perf_now();
        d->n = have ? budgeted_nav_path(i+MAX_BOTS,g, boat ? ray_cast : drive_nav_ray, vp[0], vp[1], vp[2], tgt[0], tgt[1], tgt[2], d->pts, DRIVE_MAX_PTS) : 0;
        d->partial=have && (nav_last_partial() || d->stageActive);
        if (perf_now() - planT0 > 50.0) bot_log("perf: %s's vehicle route took %.0f ms (%d points)", pl + OFF_PLAYER_ONLINE_NAME, perf_now() - planT0, d->n);
        if (have && !d->n && !g_lastPlanDeferred && boat) {   // no way round with 6 m to spare: the shallows too
            if (H->frame - d->sensorLogAt > 15 * 30) { d->sensorLogAt = H->frame; bot_log("drive: %s finds no route 6 m clear of land - allows the shallows", pl + OFF_PLAYER_ONLINE_NAME); }
            nav_set_path_mask(mask);
            d->n = budgeted_nav_path(i+MAX_BOTS,g, ray_cast, vp[0], vp[1], vp[2], tgt[0], tgt[1], tgt[2], d->pts, DRIVE_MAX_PTS);
            d->partial=nav_last_partial();
        }
        float ow[3];
        if (have && !d->n && !g_lastPlanDeferred && boat && nav_open_water_near(g, mask, vp[0], vp[2], 80.0f, ow)) {   // docked in a pocket (pier posts):
            d->n = budgeted_nav_path(i+MAX_BOTS,g, ray_cast, ow[0], ow[1], ow[2], tgt[0], tgt[1], tgt[2], d->pts + 3, DRIVE_MAX_PTS - 1);   // out to open water first
            d->partial=nav_last_partial();
            if (d->n) { memcpy(d->pts, ow, 12); d->n++; bot_log("drive: %s backs out to open water at (%.0f, %.0f) first", pl + OFF_PLAYER_ONLINE_NAME, ow[0], ow[2]); }
        }
        nav_set_path_mask(NULL);
        nav_set_vehicle_width(1.5f);nav_set_vehicle_avoid(NULL,0);
        nav_set_long_legs(0);
        int keep = d->n;
        for (int k = 0; k < d->n && !boat; k++) if (dist2(&d->pts[k * 3], d->goal) < armor_stop_short(i) * armor_stop_short(i)) { keep = k + 1; break; }
        d->n = keep;
        if (!d->n) {
            if(have && g_lastPlanDeferred)return;
            if(d->stageActive){
                d->stageCandidate++;d->stageActive=0;
                diagnostic_note(i,"nearby driving leg unavailable; checks another road exit",d->veh);
                return;
            }
            if(armor_route_failed(i))return;
            bot_log("drive: no drivable route from (%.0f, %.0f) to (%.0f, %.0f): %s", vp[0], vp[2], d->goal[0], d->goal[2], nav_last_error());
            if(!boat && drive_route_fallback(i,d,vp))return;   // AltBotMod V14: another Fun-Bots route, then checked straight legs
            if(!boat && drive_other_objective(i,d,vp,1))return;
            if(!boat && !d->manual)drive_wait_route(i,d,"no reachable driving objective yet");
            else drive_stop(i,"found no reachable drivable objective",30);
            return;
        }
        d->next = 0; d->phase = 3; d->at = d->checkAt = H->frame; d->stuckCount = 0;d->routeFailures=0;d->boatPlanSince=0;
        d->stageCandidate=0;d->straightRoute=0;d->traceContinue=0;d->funRoute=0;d->statPlans++;
        memset(&g_enhancedSteer[i], 0, sizeof g_enhancedSteer[i]);
        memcpy(d->lastPos, vp, 12);
        d->noRouteSince = 0;
        bot_log("drive: %s drives %d waypoint(s) towards flag %d%s", pl + OFF_PLAYER_ONLINE_NAME, d->n, d->flag,d->stageActive?" via a checked nearby road leg":"");
        return;
    }
    // driving
    if (H->frame - d->at > 180 * 30) {
        // The motion/stuck detector handles actual immobility. A long trip on
        // a large map gets a fresh route while retaining the driver and crew.
        d->phase=2;d->why="trip over 180 s";d->at=H->frame;d->coastUntil=H->frame+45;drive_coast(i,d);return;
    }
    {
        VehTrack *t = vehicle_sample(d->veh);
        float hp = *(float *)(d->veh + OFF_VEHICLE_HEALTH), mx = t && t->maxSeen > 0 ? t->maxSeen : 0;
        if (mx > 0 && hp < g_enhanced.bailHealth * mx) { drive_stop(i, "bails out - the vehicle is nearly wrecked", 20); return; }
    }
    {
        // AltBotMod V24: progress along the route is the nearest route point within the next 30 m of it (never backwards),
        // and on past one the vehicle is at or has passed. V6-V23 made it reach every waypoint - within 1.75 m at any
        // "corner" of 25 deg or more, and on a hand-recorded Fun-Bots road nearly every point is one: the vehicle aimed at
        // a point a few metres off its nose, its heading error jumped and the speed cap for it with it - "slow down and go
        // every few metres". Steering goes for a point further on (pure pursuit, below).
        float *hx = vehicle_xf(d->veh);
        d->next = bm_route_progress(d->pts, d->n, d->next, vp, hx ? hx + 8 : NULL, vehicle_is_boat(d->veh) ? 12.0f : DRIVE_WAYPOINT);
    }
    if (vehicle_is_boat(d->veh) && boat_landed(i, d, vp)) return;   // Caspian V5: at the landing / on the beach - everyone out
    if (route_exit_tick(i, d, vp)) return;   // AltBotMod V15: a recorded exit point on the way to the objective
    if(d->next>=d->n && d->partial && (vehicle_is_boat(d->veh) || dist2(vp,d->goal)>=armor_stop_short(i)*armor_stop_short(i))) {
        // AltBotMod V8: rolls on while the next leg is planned (it used to stop dead at the end of every partial route)
        d->phase=2;d->why="end of a partial route";d->coastUntil=H->frame+120;drive_coast(i,d);return;   // V16: 4 s (V8: 1.5 s - long maps stopped between legs)
    }
    if (d->next >= d->n || (!vehicle_is_boat(d->veh) && dist2(vp, d->goal) < armor_stop_short(i) * armor_stop_short(i))) {
        if (vehicle_is_boat(d->veh)) { boat_ashore(i, d, vp, "end of its route"); return; }
        if(armor_arrive(i,vp))return;
        if(!vehicle_is_boat(d->veh) && !d->manual){drive_arrive_objective(i,d,vp);return;}
        drive_stop(i, "arrived - gets out", 20);
        return;
    }
    float *xf = vehicle_xf(d->veh);
    if (!xf) { drive_stop(i, "cannot read the vehicle's heading", 60); return; }
    float *wp = &d->pts[d->next * 3];
    // AltBotMod V24: pure pursuit - steer at the route 8 m ahead when slow, up to 25 m at speed, not at the next point. The
    // look-ahead stops short of a real bend (the route turning 35 deg+ from the way to the next point), so corners are taken
    // round, not cut through what stands inside them.
    float aimP[3]; bm_route_aim(d->pts, d->n, d->next, vp, d->speed, aimP);
    if (d->passUntil > H->frame && d->passVeh) {   // AltBotMod V29: passing a stopped vehicle - aim beside it (drive_lane_scan)
        float hl0 = hypotf(xf[8], xf[10]);
        if (hl0 > 0.1f) { aimP[0] += xf[10] / hl0 * d->passSide * d->passLat; aimP[2] -= xf[8] / hl0 * d->passSide * d->passLat; }
    } else if (d->passUntil <= H->frame) d->passVeh = NULL;
    float heading = atan2f(xf[8], xf[10]), want = atan2f(aimP[0] - vp[0], aimP[2] - vp[2]);
    float err = want - heading;
    while (err > 3.14159265f) err -= 6.2831853f;
    while (err < -3.14159265f) err += 6.2831853f;
    float steer = bm_steer(&g_enhancedSteer[i], err, BOT_TICK_DT, &g_enhanced);
    // speed (m/s) from the position every 1/3 s
    if (!d->spAt) { memcpy(d->spPos, vp, 12); d->spAt = H->frame; }
    else if (H->frame - d->spAt >= 10) {
        d->speed = sqrtf(dist2(vp, d->spPos)) / ((H->frame - d->spAt) / 30.0f);
        memcpy(d->spPos, vp, 12); d->spAt = H->frame;
    }
    // how fast to go: no limit on a straight - flat out - only slowing in time for the next bend (and the end of the
    // route): every waypoint ahead allows a speed for its turn, and the bot brakes so as to get there at that speed.
    int isBoat = vehicle_is_boat(d->veh);
    const BMVehicleProfile *profile = bm_vehicle_profile(vehicle_name(d->veh));
    float ae = fabsf(err), vwant = bm_speed_limit(&g_enhanced, profile);
    // AltBotMod V25: as Fun-Bots - full speed unless it has to steer hard (V6-V24 capped at 12 m/s from 31 deg off the aim,
    // which on a winding road with a slow-turning tank was most of the time)
    if (ae > 1.05f) vwant = fminf(vwant, isBoat ? 10.0f : bm_armored(profile) ? 6.0f : 9.0f);
    if (ae > 1.6f) vwant = fminf(vwant, isBoat ? 6.0f : 4.0f);
    float vAfterHeading = vwant;   // (V26: the drive statistics - what the speed target answered to)
    {
        const float decel = isBoat ? 3.0f : 4.0f;    // m/s per second it can shed (rough)
        float along = sqrtf(dist2(wp, vp));
        for (int k = d->next; k < d->n && along < 200.0f; k++) {
            float *w = &d->pts[k * 3], vt;
            if (k == d->n - 1) vt = d->partial ? 99.0f : isBoat ? 6.0f : 4.0f;   // the end: arrive slowly (V16: not the end of a leg - the next one follows)
            else {
                // AltBotMod V22: the bend is measured over ~10 m either side of the waypoint, and its speed comes from its
                // radius (v = sqrt(grip x radius)). V8-V21.1 compared each pair of neighbouring segments: a recorded Fun-Bots
                // road has a point every few metres with a hand's jitter, so nearly every point read as a 17-degree bend and
                // vehicles crawled at 13 m/s down straight roads ("convoys are a pain").
                int kb = k, kf = k + 1;
                while (kb > 0 && dist2(&d->pts[kb * 3], w) < 25.0f * 25.0f) kb--;   // (V24: +-25 m of the road itself - a road's bend, not a driver's swerve or where the car happens to be)
                while (kf < d->n - 1 && dist2(&d->pts[kf * 3], w) < 25.0f * 25.0f) kf++;
                const float *pb = kb < k ? &d->pts[kb * 3] : NULL, *pf = &d->pts[kf * 3];
                vt = bm_route_bend_limit(pb, w, pf, isBoat, bm_armored(profile));   // (V24: only real bends - Fun-Bots drives flat out)
            }
            float lead = fmaxf(0.0f, along - (isBoat ? 12.0f : 6.0f));   // (it turns a little before the waypoint)
            float v = sqrtf(vt * vt + 2.0f * decel * lead);
            if (v < vwant) vwant = v;
            if (k + 1 < d->n) along += sqrtf(dist2(&d->pts[(k + 1) * 3], w));
        }
    }
    if(!d->sensorAt || H->frame-d->sensorAt>=(d->speed>12.0f?3u:5u)){   // staggered 10 Hz bumper samples (V31.1: 6 Hz under 12 m/s - 14 rays a sample)
        d->sensorAt=H->frame?H->frame:1;
        float hx = xf[8], hz = xf[10], hl = sqrtf(hx * hx + hz * hz) + 1e-6f, hit[3];
        unsigned saved = g_rayFlags;
        // (trees too - V16: not for tanks / IFVs, which flatten bushes and saplings; a stop for every bush made them crawl)
        g_rayFlags = RAY_DONT_CHECK_CHARACTER | (bm_armored(profile) ? 0 : RAY_CHECK_DETAIL_MESH);
        float fx = hx / hl, fz = hz / hl, rx = fz, rz = -fx;   // forward, right (in the ground plane)
        // AltBotMod V23: the sensors look down the route, not straight off the bonnet. On every bend of a recorded road the
        // straight-ahead rays met the verge - walls, rocks, trees - and the driver braked for things it was never going to
        // drive into (V22 live: 1,299 "sees something ahead" at an average 3 m/s; "stop-go every 5-10 m").
        float lx = fx, lz = fz;
        {
            float want = fminf(40.0f, fmaxf(10.0f, bm_drive_horizon(d->speed, isBoat) * 0.7f));
            for (int k = d->next; k < d->n; k++) {
                float *q = &d->pts[k * 3], ex = q[0] - vp[0], ez = q[2] - vp[2], el = sqrtf(ex * ex + ez * ez);
                if (el < want && k + 1 < d->n) continue;
                if (el > 1.0f && (ex * fx + ez * fz) / el > 0.35f) { lx = ex / el; lz = ez / el; }   // (not behind it: then the bonnet)
                break;
            }
            rx = lz; rz = -lx;
        }
        // AltBotMod V24: on a recorded Fun-Bots road (within 4 m of it) the road is the proof that the way is clear - people
        // drove it. AltBotMod V29: on it NOTHING static counts any more - no wall dead ahead, no verge, no swerving - only
        // vehicles, and those by geometry (drive_lane_scan). V24-V28 still braked and swerved for whatever the middle rays met
        // within 4 m (live V28: 479 "sees something ahead" at 0 m/s). Off the road (the grid, straight legs, a join, more
        // than 4 m off the line) the sensors count as before.
        int onRoad = 0;
        if (d->funRoute && d->n >= 2 && d->next < d->n && d->next > d->joinN) {
            int k1 = d->next > 0 ? d->next : 1;
            const float *a = &d->pts[(k1 - 1) * 3], *b = &d->pts[k1 * 3];
            float ax = b[0] - a[0], az = b[2] - a[2], l = ax * ax + az * az, t = l > 0 ? ((vp[0] - a[0]) * ax + (vp[2] - a[2]) * az) / l : 0;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            onRoad = hypotf(a[0] + ax * t - vp[0], a[2] + az * t - vp[2]) < 4.0f;
        }
        d->onRoadNow = (unsigned char)onRoad;
        float sideNear[3] = {99, 99, 99};                       // nearest thing ahead on the left, in the middle, on the right
        static const float kOff[7] = {-1.2f, -0.8f, -0.4f, 0.0f, 0.4f, 0.8f, 1.2f};   // 7 sensors across the front (palm trunks, poles)
        void *ownBody = !IsBadReadPtr(d->veh + 0x30, 4) ? *(void **)(d->veh + 0x30) : NULL;
        void *lastBody = NULL;
        unsigned lastVt = 0, lastMat = 0;
        float lastNy = 0, nrm[3];
        for (int r = 0; r < 7 && !onRoad; r++)
            for (int k = 0; k < 2; k++) {
                float off = kOff[r] * (profile->halfWidth / 1.2f), up = isBoat ? (k ? 2.2f : 1.4f) : k ? 1.0f : 0.5f;
                int s = r < 3 ? 0 : r == 3 ? 1 : 2;
                float lead = profile->frontLead + (isBoat ? d->speed * 0.15f : 0);
                float a[3] = {vp[0] + fx * lead + rx * off, vp[1] + up, vp[2] + fz * lead + rz * off};
                float horizon=bm_drive_horizon(d->speed,isBoat);
                float b[3] = {a[0] + lx * horizon, a[1], a[2] + lz * horizon};
                if (ray_cast(a, b, hit, nrm)) {
                    char *body = *(char **)g_lastHitRest;
                    unsigned bvt = body && !IsBadReadPtr(body, 4) ? *(unsigned *)body : 0;
                    if (body == ownBody || bvt == ADDR_WATER_PHYSICS_VTABLE) continue;   // its own hull / the water (waves)
                    if (vehicle_crushable_hit()) continue;   // AltBotMod V8: a fence - drive through it
                    if (nrm[1] > 0.65f) continue;   // ground rising ahead (a slope, a ramp, a kerb top): drivable, not a wall
                    if (drive_hit_vehicle(body, d->veh)) continue;   // (V29: vehicles are drive_lane_scan's - by where they are, not what a ray met)
                    float dh = sqrtf(dist2(hit, a));
                    if (dh < sideNear[s]) { sideNear[s] = dh; lastBody = body; lastVt = bvt; lastMat = hit_material(); lastNy = nrm[1]; }
                }
            }
        g_rayFlags = saved;
        float nearest = fminf(sideNear[1], fminf(sideNear[0], sideNear[2]));
        d->frontNear = nearest;
        d->sideL = sideNear[0]; d->sideR = sideNear[2]; d->midNear = sideNear[1];
        // AltBotMod V25: the brakes answer to what is in the vehicle's way - the middle sensors, and the outer ones only when
        // the thing is within 4 m (a corner it would clip).
        d->brakeNear = fminf(sideNear[1], fminf(sideNear[0] < 4.0f ? sideNear[0] : 99.0f, sideNear[2] < 4.0f ? sideNear[2] : 99.0f));
        if (isBoat && nearest >= 5.0f) { nearest = 99; sideNear[0] = sideNear[1] = sideNear[2] = 99; d->brakeNear = 99; }   // boats: only what is right ahead
        if (nearest < (isBoat ? 5.0f : 6.0f) && H->frame - d->sensorLogAt > 60) {
            d->sensorLogAt = H->frame;
            bot_log("drive: %s's %s sees something ahead at (%.0f, %.0f): left %.1f mid %.1f right %.1f m (vt %08x mat %08x normal-y %.2f, %.0f m/s)",
                    pl + OFF_PLAYER_ONLINE_NAME, vehicle_name(d->veh), vp[0], vp[2], sideNear[0], sideNear[1], sideNear[2], lastVt, lastMat, lastNy, d->speed);
        }
        (void)lastBody;
        if (isBoat && !d->backedOut) {   // (departure, phase 8, has done this)
            d->backedOut = 1;
            if (nearest < 8.0f) {
                d->backOutUntil = H->frame + 90;
                bot_log("drive: %s backs the boat out of its mooring", pl + OFF_PLAYER_ONLINE_NAME);
            }
        }
        if (!isBoat) drive_lane_scan(i, d, vp, xf, profile);   // AltBotMod V29: the vehicles in its lane
        else { d->laneLimit = 99.0f; d->laneBlocker = NULL; }
    }
    // (off the road only - the sensors see nothing on it) a pole / corner off to one side: ease away from it; something dead
    // ahead: swerve to the clearer side; boxed in on all three: stop (the stuck check reverses)
    if (!d->onRoadNow) {
        if (d->midNear > 6.0f && (d->sideL < 6.0f) != (d->sideR < 6.0f)) {
            float away = d->sideR < 6.0f ? 0.6f : -0.6f;   // positive EIAYaw turns towards a lower heading (left of a +right vector)
            steer = fmaxf(-1.0f, fminf(1.0f, steer + away));
        }
        if (d->midNear < 8.0f) {
            if (d->sideL < 4.0f && d->sideR < 4.0f) vwant = 0.0f;
            else steer = d->sideL > d->sideR ? 0.85f : -0.85f;   // left clearer -> left (positive), right clearer -> right
        }
    }
    float vAfterBends = vwant;
    // AltBotMod V29: the vehicle in its lane (drive_lane_scan), then - off the road only - a static thing in the way
    if (!isBoat && d->laneAt && H->frame - d->laneAt < 15 && d->laneLimit < vwant) vwant = d->laneLimit;
    float vAfterLane = vwant;
    if (!d->onRoadNow && d->brakeNear < 99) vwant = fminf(vwant, bm_drive_obstacle_speed(d->brakeNear, d->speed, isBoat));
    // AltBotMod V29: Fun-Bots driving - the throttle is down (1.0) unless there is a reason not to: a real bend or the end
    // of the route ahead (vAfterBends), hard steering (vAfterHeading), a vehicle in the lane, or - off a recorded road - an
    // obstacle. No smoothing of the target and no partial throttle on the way back up: V6-V28 raised the speed target
    // 8 m/s per second after every dip and fed the throttle 0.35 + 0.12 x the error - after each little check the vehicle
    // crept back up to speed ("accelerates slowly, brakes, accelerates slowly" - V28 live: 4-5 m/s average on open roads).
    float throttle;
    int cause = 0;   // what the speed answers to now: 0 nothing, 1 a bend / the end, 2 a vehicle in the lane, 3 hard steering, 4 an obstacle (off-road), 5 stop
    if (vwant < vAfterLane - 0.5f) cause = 4;
    else if (vAfterLane < vAfterBends - 0.5f) cause = 2;
    else if (vAfterBends < vAfterHeading - 0.5f) cause = 1;
    else if (vAfterHeading < bm_speed_limit(&g_enhanced, profile) - 0.5f) cause = 3;
    throttle = bm_drive_throttle(vwant, d->speed, bm_armored(profile), ae);   // (enhanced.c: 1.0 below the target, 0.3 at it, a brake over it)
    if (vwant <= 0.0f) cause = 5;
    {   // AltBotMod V26/V29: every 20 s per driver - average speed, the share of time at full throttle, braking by cause, and
        // the speed lost while the throttle was down (not braking): that is what the road, a collision or something outside
        // the driving code took - logged with the details when it happens, so a live round says what it was.
        if (cause && throttle < 0.0f && d->speed > 2.0f) d->statBrakeBy[cause]++;
        if (throttle >= 0.95f) d->statFull++;
        if (d->spAt == H->frame) {   // (a fresh 1/3 s speed sample)
            if (d->statPrevSpeed - d->speed > 2.5f && d->lastThrottle >= 0.5f && H->frame - d->statDropFrame > 15) {
                d->statDrops++; g_driveDrops++; d->statDropFrame = H->frame;
                vring_dump(i);   // AltBotMod V30.1
                if (H->frame - d->dropLogAt > 10 * 30 || !d->dropLogAt) {
                    d->dropLogAt = H->frame ? H->frame : 1;
                    bot_log("drive: %s's %s lost %.1f m/s in 1/3 s with the throttle down (%.0f -> %.0f m/s) at (%.0f, %.0f) - %s, sensors %.1f/%.1f/%.1f m, lane %s %.1f m/s, steering %.2f (%u such drops so far)",
                            pl + OFF_PLAYER_ONLINE_NAME, vehicle_name(d->veh), d->statPrevSpeed - d->speed, d->statPrevSpeed, d->speed, vp[0], vp[2],
                            d->onRoadNow ? "on the road" : "off the road", d->sideL, d->midNear, d->sideR,
                            d->laneBlocker ? vehicle_name(d->laneBlocker) : "clear", d->laneLimit, steer, g_driveDrops);
                }
            }
            d->statPrevSpeed = d->speed;
        }
        d->statFrames++; d->statSpeed += d->speed;
        if (!d->statAt) d->statAt = H->frame ? H->frame : 1;
        if (H->frame - d->statAt >= 20 * 30 && d->statFrames > 0) {
            unsigned tot = d->statBrakeBy[1] + d->statBrakeBy[2] + d->statBrakeBy[3] + d->statBrakeBy[4] + d->statBrakeBy[5];
            bot_log("drive: %s's %s, last 20 s: average %.1f m/s, full throttle %.0f%%, braking %.0f%% (bends %u, vehicle in lane %u, hard steering %u, obstacle %u, stop %u frames), "
                    "%u speed drop(s) with the throttle down, %u re-plan(s), %.1f s re-planning%s",
                    pl + OFF_PLAYER_ONLINE_NAME, vehicle_name(d->veh), d->statSpeed / d->statFrames, 100.0f * d->statFull / d->statFrames, 100.0f * tot / d->statFrames,
                    d->statBrakeBy[1], d->statBrakeBy[2], d->statBrakeBy[3], d->statBrakeBy[4], d->statBrakeBy[5], d->statDrops, d->statPlans, d->statOffPhase / 30.0f,
                    d->funRoute ? " on a Fun-Bots road" : " on the driving grid");
            d->statAt = H->frame ? H->frame : 1; d->statFrames = 0; d->statSpeed = 0; d->statFull = 0; d->statDrops = 0; d->statPlans = 0; d->statOffPhase = 0;
            memset(d->statBrakeBy, 0, sizeof d->statBrakeBy);
        }
    }
    // AltBotMod V25: the convoy system is gone. AltBotMod V29: a vehicle stopped in the lane is waited behind (1.5 s, then
    // passed - drive_lane_scan); the stuck checks leave that wait be.
    (void)wp; d->trafficWait = d->laneBlocker && d->laneLimit < 0.5f && d->laneSince && H->frame - d->laneSince < 6 * 30 && vwant <= 0.5f;
    d->trafficFollowing = 0; d->trafficPeer = NULL;
    if(!d->trafficWait && (vwant>1.5f || d->frontNear<6.0f))d->wantMoveAt=H->frame?H->frame:1;   // Caspian V4: it means to move (drive_unstick_tick)
    if(d->trafficWait){d->boxedSince=0;d->reverseUntil=d->backOutUntil=0;d->checkAt=H->frame;memcpy(d->lastPos,vp,12);}
    // (dead ahead only: a building alongside the front corner while pulling away slowly is not "nosed into it")
    if (!d->trafficWait && H->frame >= d->reverseUntil && H->frame >= d->backOutUntil && d->speed < 1.0f && (vwant <= 0.0f || d->midNear < 2.0f)) {
        if (!d->boxedSince) d->boxedSince = H->frame;
        else if (H->frame - d->boxedSince >= 15) {   // nose against something: back off straight away (not after the 3 s check)
            d->boxedSince = 0;
            d->reverseUntil = H->frame + 60;
            d->checkAt = H->frame; memcpy(d->lastPos, vp, 12);
            // swing the nose ~60 deg away from the blocked side while reversing (pointing it back along the route just
            // lined up the same corner again: a buggy nosed into one building corner 11 times in a minute)
            int leftBlocked = d->sideL < d->sideR;
            d->awayOn = 1;
            d->awayHeading = heading + (leftBlocked ? 1.0f : -1.0f);   // (+ = towards the right side vector)
            if (d->nudges && dist2(vp, d->nudgePos) < 5.0f * 5.0f) d->nudges++;
            else { d->nudges = 1; memcpy(d->nudgePos, vp, 12); }
            bot_log("drive: %s is nosed up against something at (%.0f, %.0f) - backs off, nose to the %s (%d here)", pl + OFF_PLAYER_ONLINE_NAME,
                    vp[0], vp[2], leftBlocked ? "right" : "left", d->nudges);
            unsigned char *sm = vehicle_is_boat(d->veh) ? g_boatMask : g_driveMask;   /* (V9: freed at a level change - drive_masks_reset) */
            if (d->nudges >= 2 && sm) {   // again here: that front corner is no road - off the map, new route
                d->nudges = 0;
                if (++d->reroutes > 2) { if(drive_recover_job(i,d,vp))return;vehicle_mark_bad(d->veh,60);drive_stop(i,"manual/boat recovery exhausted",30);return; }
                float hx = xf[8], hz = xf[10], hl = sqrtf(hx * hx + hz * hz) + 1e-6f, fx = hx / hl, fz = hz / hl, rx = fz, rz = -fx;
                float side = leftBlocked ? -1.0f : 1.0f;
                float ax = vp[0] + fx * 4.0f + rx * side * 1.5f, az = vp[2] + fz * 4.0f + rz * side * 1.5f;
                int k = drive_block_for_job(i,sm,ax,az,3.0f,vp[1]);
                bot_log("drive: (%.0f, %.0f) struck off the drivable map (%d node(s)); %s re-routes round it", ax, az, k, pl + OFF_PLAYER_ONLINE_NAME);
                d->phase = 2;d->why="nosed up twice - spot struck off"; d->traceTries++;   // (V25: a try - stuck on this road)
            }
        }
    } else d->boxedSince = 0;
    // AltBotMod V25: the road is behind or beside it (a route that starts back the way it faces, a hairpin): it turns round
    // on the spot - full lock forwards while the way ahead is clear (tanks pivot), a three-point turn when it is not. V6-V24
    // reversed, and with the rear blocked dropped the route and re-planned - the same route, the same turn, 40 times in 2 s
    // (V24 live, a T90 on Firestorm) - and every re-plan counted as a try, so after a few the Fun-Bots road was given up.
    // AltBotMod V27: a tank pivots on its tracks - full lock, no throttle - until it faces the road (within 20 deg), then
    // drives; V25-V26 gave it throttle as well, so it drove in circles round an aim point inside its own turn ("tanks just
    // turning in place"). Wheeled vehicles keep the forward turn on full lock (they cannot pivot).
    int tracked = profile->kind == BM_TANK;
    if (d->pivoting && (ae < 0.35f || d->speed > 4.0f || H->frame - d->pivotAt > 6 * 30)) d->pivoting = 0;
    if(!isBoat && !d->straightRoute && (ae>1.2f || d->pivoting) && d->speed<3 && H->frame>=d->reverseUntil && H->frame>=d->backOutUntil){
        float lock = err > 0 ? -1.0f : 1.0f;   // (positive steer turns towards a lower heading - the sign bm_steer uses)
        if (fabsf(steer) > 0.05f) lock = steer > 0 ? 1.0f : -1.0f;
        if (tracked) { if (!d->pivoting) { d->pivoting = 1; d->pivotAt = H->frame ? H->frame : 1; } throttle = 0.0f; steer = lock; }
        else if ((d->midNear > 4.0f && d->brakeNear > 3.0f) || H->frame < d->rearBlockedUntil) { throttle = d->midNear > 2.0f ? 0.45f : 0.25f; steer = lock; }
        else { d->awayOn = 1; d->awayHeading = heading + fmaxf(-1.0f, fminf(1.0f, err)); d->reverseUntil = H->frame + 30; }
        // (the stuck checks wait while it is actually turning - its heading moving - not while it is held fast)
        float turned = heading - d->turnHeading;
        while (turned > 3.14159265f) turned -= 6.2831853f;
        while (turned < -3.14159265f) turned += 6.2831853f;
        if (fabsf(turned) > 0.005f) { d->checkAt = H->frame; memcpy(d->lastPos, vp, 12); }
    }
    d->turnHeading = heading;
    if (drive_pickup(i, d, vp)) {
        throttle = d->speed > 0.5f ? -0.6f : 0.0f;
        steer = 0;
        d->checkAt = H->frame; memcpy(d->lastPos, vp, 12);
        d->at++;
    } else if (drive_repair_hold(i,d,vp)) {   // AltBotMod V11: an engineer is coming to repair it - wait for it
        throttle = d->speed > 0.5f ? -0.6f : 0.0f;
        steer = 0;
        d->checkAt = H->frame; memcpy(d->lastPos, vp, 12);
        d->at++;
    } else if (drive_combat_hold(i,d,vp)) {   // AltBotMod V8: a steadier shot at an enemy vehicle - rolls on at 4 m/s, still steering
        if (d->speed > 4.5f) throttle = fminf(throttle, d->speed > 7.0f ? -0.35f : 0.0f);
        else throttle = fminf(throttle, 0.4f);
        d->checkAt = H->frame; memcpy(d->lastPos, vp, 12);
    }
    if (H->frame < d->backOutUntil) { throttle = -0.9f; steer = 0; d->checkAt = H->frame; memcpy(d->lastPos, vp, 12); }   // straight back
    else if (H->frame < d->reverseUntil) {   // backing off: steer so the nose swings round to the route (the road) ahead
        throttle = -0.8f;
        float e = err;
        if (d->awayOn) {   // (a quick back-off: away from what it hit instead)
            e = d->awayHeading - heading;
            while (e > 3.14159265f) e -= 6.2831853f;
            while (e < -3.14159265f) e += 6.2831853f;
        }
        steer = fmaxf(-1.0f, fminf(1.0f, e * 1.5f));   // (reversing turns the nose the other way round)
    }
    if (H->frame >= d->reverseUntil) d->awayOn = 0;
    if (bm_stuck_due(H->frame, d->checkAt, H->frame < d->reverseUntil, H->frame < d->backOutUntil, g_enhanced.stuckSeconds)) {
        float dx = vp[0] - d->lastPos[0], dz = vp[2] - d->lastPos[2];
        if (dx * dx + dz * dz < 1.5f * 1.5f) {
            if (dist2(vp, d->goal) < 18.0f * 18.0f && !d->manual && !vehicle_is_boat(d->veh)) { drive_arrive_objective(i,d,vp); return; }   // (the last bit on foot)
            if (++d->stuckCount > 4) { if(drive_recover_job(i,d,vp))return;drive_stop(i,"manual/boat recovery exhausted",30);return; }
            d->reverseUntil = H->frame + 75;
            d->reverseSteer = -steer;
            unsigned char *sm = vehicle_is_boat(d->veh) ? g_boatMask : g_driveMask;   /* (V9: freed at a level change - drive_masks_reset) */   // the map this vehicle drives on
            if (d->stuckCount == 2 && sm) {   // stuck again: the spot is no road - strike it off for everyone, re-route
                if (++d->reroutes > 2) { if(drive_recover_job(i,d,vp))return;vehicle_mark_bad(d->veh,60);drive_stop(i,"manual/boat recovery exhausted",30);return; }
                float ax = vp[0] + xf[8] * 6.0f, az = vp[2] + xf[10] * 6.0f;   // ahead of it (its own spot stays drivable)
                int k = drive_block_for_job(i,sm,ax,az,3.0f,vp[1]);
                if (k == 0) {   // already struck and still stuck there: what holds it is beside / under the front - wider
                    ax = vp[0] + xf[8] * 4.5f; az = vp[2] + xf[10] * 4.5f;
                    k = drive_block_for_job(i,sm,ax,az,5.0f,vp[1]);
                }
                d->reverseUntil = H->frame + (d->reroutes > 1 ? 150 : 105);   // a longer back-off before the new route
                bot_log("drive: (%.0f, %.0f) struck off the drivable map (%d node(s)); %s re-routes", ax, az, k, pl + OFF_PLAYER_ONLINE_NAME);
                d->phase = 2;d->why="stuck twice - spot struck off"; d->traceTries++;   // (V25: a try - stuck on this road)
            }
            bot_log("drive: %s is stuck at (%.0f, %.0f) - reversing (wanted %.1f m/s, throttle %.2f, steer %.2f)", pl + OFF_PLAYER_ONLINE_NAME, vp[0], vp[2],
                    vwant, throttle, steer);
            bm_report_stuck(i, vp, "vehicle");
        } else d->stuckCount = 0;
        memcpy(d->lastPos, vp, 12);
        d->checkAt = H->frame;
    }
    H->botMove[i][0] = throttle;
    H->botMove[i][1] = 0;
    H->botMove[i][2] = steer;
    d->lastThrottle = throttle;
    if (throttle > 0.0f) d->drivenAt = H->frame ? H->frame : 1;   // (V25: drive_coast holds this through a re-plan)
}

// ---- Tickets (ServerTicketCounterEntity, one per team; +0xa0 = tickets, float - what RCON serverInfo reports).
// The game runs the flag bleed itself only in some rounds (not after a rotation with bots only, seen overnight), and
// it never charges bot deploys (bots spawn through BotMod, not the deploy screen). So BotMod charges every bot deploy
// (a bot with a new soldier; revives keep the soldier and stay free), runs the Conquest bleed itself while the game's
// own is dormant (a team holding a majority of the flags bleeds the other 15 tickets/min per flag of difference,
// double when it holds none), and ends the round at 0 if the game has not done so 10 s later. ----
#define ADDR_TICKET_COUNTER_VTABLE 0x0268bc30
#define OFF_TC_DATA                0x14
#define OFF_TCDATA_TEAM            0x6c
#define OFF_TC_TICKETS             0xa0
#define OFF_TC_INITIAL             0xa8        // int: the round's starting ticket count
#define TICKET_BLEED_PER_MIN       15.0f
#define GAME_BLEED_GRACE_FRAMES    (20 * 30)   // the game moved the counter this recently: its bleed is running
static char *g_ticketCounter[3];
static float g_ticketsSet[3];                  // value after BotMod's last change: any other change is the game's
static unsigned g_gameTicketsAt[3];            // frame the game last changed a team's tickets
static int g_ticketsOn, g_roundEnded;
static unsigned g_zeroAt, g_ticketsLookAt, g_ticketsFoundAt, g_roundEndedAt;

static int g_ticketsTries;
static char *g_lastSoldierOf[64];
static char *g_lastPlayerAt[64];
static char *g_hadSoldier[64];   // AltBotMod V9: this player already deployed once this round (only respawns cost a ticket)
static unsigned g_freeDeploys, g_chargedDeploys;
static void tickets_reset(void)
{
    g_ticketCounter[1] = g_ticketCounter[2] = NULL;
    g_ticketsOn = 0; g_roundEnded = 0; g_zeroAt = 0; g_ticketsTries = 0; g_roundEndedAt = 0;

    g_gameTicketsAt[1] = g_gameTicketsAt[2] = 0;
    memset(g_lastSoldierOf, 0, sizeof g_lastSoldierOf);
    memset(g_lastPlayerAt, 0, sizeof g_lastPlayerAt);
    memset(g_hadSoldier, 0, sizeof g_hadSoldier);
    g_freeDeploys = g_chargedDeploys = 0;
}
static void tickets_find(void)   // (level ready: a quick scan for two objects)
{
    char *tc[16];
    int n = scan_objects(ADDR_TICKET_COUNTER_VTABLE, tc, 16);   // (stale copies from earlier rounds come first)
    for (int k = 0; k < n; k++) {
        if (IsBadReadPtr(tc[k], OFF_TC_TICKETS + 4)) continue;
        char *data = *(char **)(tc[k] + OFF_TC_DATA);
        int team = data && !IsBadReadPtr(data + OFF_TCDATA_TEAM, 4) && !IsBadReadPtr(data, 4) && *(unsigned *)data == 0x0250bf04 ?
                   *(int *)(data + OFF_TCDATA_TEAM) : 0;   // (data must really be a TicketCounterEntityData)
        if (team == 1 || team == 2) { g_ticketCounter[team] = tc[k]; g_ticketsSet[team] = *(float *)(tc[k] + OFF_TC_TICKETS); }
    }
    g_ticketsOn = g_ticketCounter[1] && g_ticketCounter[2];
    g_ticketsFoundAt = H->frame;
    bot_log("tickets: %s (US %.0f, RU %.0f)", g_ticketsOn ? "found - BotMod charges bot deploys (and bleeds while the game does not)" :
            "no ticket counters on this level", g_ticketsOn ? g_ticketsSet[1] : 0.0f, g_ticketsOn ? g_ticketsSet[2] : 0.0f);
}
static float *tickets_of(int team) { return (float *)(g_ticketCounter[team] + OFF_TC_TICKETS); }
static void tickets_take(int team, float n)
{
    if (!g_ticketsOn || g_roundEnded || (team != 1 && team != 2)) return;
    float *t = tickets_of(team);
    *t = *t - n > 0 ? *t - n : 0;
    g_ticketsSet[team] = *t;
}
static void rush_sync_stage(int stage,const char *reason);
// AltBotMod V6, Rush: the attackers (team 1) pay a ticket per bot deploy, as players do, and when their tickets jump UP
// the game has refilled them for the next sector - both M-COMs of this one are gone - so the bots move on to the next
// sector. (Before, only a human's spawn-menu deploy at the next base could advance the bots' sector.)
static void rush_tickets_tick(void)
{
    static float last; static LONG epoch; static unsigned seenAt;
    if (bm_mode(g_modeName) != BM_MODE_RUSH || !g_ticketCounter[1] || g_roundEnded) return;   // V15: the server's mode (a fallback to team play still pays)
    if (epoch != g_roundEpoch) { epoch = g_roundEpoch; last = *tickets_of(1); seenAt = H->frame; }
    float v = *tickets_of(1);
    if (v > last + 4.0f && H->frame - seenAt > 10 * 30) {
        bot_log("rush: attacker tickets refilled %.0f -> %.0f - sector %d taken, bots move on", last, v, g_rushStage);
        if (g_mode == BM_MODE_RUSH) chat_say("[AltBotMod] Sector %d taken - on to the next M-COMs", g_rushStage);
        rush_sync_stage(g_rushStage + 1, "attacker tickets refilled");
    }
    last = v; g_ticketsSet[1] = v;
    char *pm = player_manager();
    if (!pm) return;
    void **arr = *(void ***)(pm + 0xac);
    unsigned count = *(unsigned *)(pm + 8);
    for (unsigned k = 0; k < count && k < 64; k++) {
        char *pl = arr[k], *sd = pl ? *(char **)(pl + OFF_PLAYER_SOLDIER) : NULL;
        if (pl != g_lastPlayerAt[k]) { g_lastPlayerAt[k] = pl; g_lastSoldierOf[k] = sd; continue; }
        if (sd && sd != g_lastSoldierOf[k] && is_bot(pl) && player_team(pl) == 1) { tickets_take(1, g_enhanced.botTicketCost); last = *tickets_of(1); }   // V16: BotTicketCost (0.5: bots die easier than people)
        g_lastSoldierOf[k] = sd;
    }
}
static void tickets_tick(void)
{
    if (bm_mode(g_modeName) == BM_MODE_RUSH) { rush_tickets_tick(); return; }
    if(!mode_is_cq())return;
    if (g_roundEnded && g_roundEndedAt && H->frame - g_roundEndedAt > 15 * 30) {   // endRound is ignored on this server
        g_roundEndedAt = 0;                                                         // (pre-round disabled): next map
        bot_log("tickets: the round did not end - starting the next map");
        chat_rcon("mapList.runNextRound");
    }
    if (!g_ticketsOn || g_roundEnded) return;
    char *pm = player_manager();
    if (!pm) return;
    for (int team = 1; team <= 2; team++) {   // the game's own changes (its bleed, human deploys)
        float v = *tickets_of(team);
        // AltBotMod V28: only the game's BLEED (small steps) tells BotMod to leave the bleed to the game. V9-V27 took any
        // change of the game's - a person's death too (1 ticket) - so while you were on a team, losing players held back that
        // team's bleed for a while and the other side bled alone: "RU tickets go down faster than US".
        float delta = v - g_ticketsSet[team];
        if (fabsf(delta) > 0.001f) { if (fabsf(delta) < 0.95f) g_gameTicketsAt[team] = H->frame ? H->frame : 1; g_ticketsSet[team] = v; }
    }
    void **arr = *(void ***)(pm + 0xac);
    unsigned count = *(unsigned *)(pm + 8);
    for (unsigned k = 0; k < count && k < 64; k++) {   // bot deploys: a bot with a new soldier
        char *pl = arr[k], *s = pl ? *(char **)(pl + OFF_PLAYER_SOLDIER) : NULL;
        if (pl != g_lastPlayerAt[k]) { g_lastPlayerAt[k] = pl; g_lastSoldierOf[k] = s; if (s) g_hadSoldier[k] = pl; continue; }
        // AltBotMod V9: a respawn costs a ticket, as for a player; a bot's first deploy of the round does not. V8 charged
        // the first one too - about 30 tickets per team at every round start (200 -> 171 in the first minute on Riverside).
        if (s && s != g_lastSoldierOf[k] && is_bot(pl)) {
            if (g_hadSoldier[k] == pl) { tickets_take(player_team(pl), g_enhanced.botTicketCost); g_chargedDeploys++; }   // V16: BotTicketCost
            else g_freeDeploys++;
        }
        if (s) g_hadSoldier[k] = pl;
        g_lastSoldierOf[k] = s;
    }
    if (H->frame % 30) return;
    int held[3] = {0, 0, 0}, total = 0;   // bleed, once a second, while the game's is not running
    for (int f = 0; f < g_flagCount; f++) { int o = flag_owner(f); total++; if (o == 1 || o == 2) held[o]++; }
    for (int team = 1; team <= 2; team++) {
        int other = 3 - team;
        if (held[other] * 2 <= total || held[other] <= held[team]) continue;   // no majority against it
        if (g_gameTicketsAt[team] && H->frame - g_gameTicketsAt[team] < GAME_BLEED_GRACE_FRAMES) continue;   // the game bleeds it
        float perMin = TICKET_BLEED_PER_MIN * (float)(held[other] - held[team]) * (held[team] == 0 ? 2.0f : 1.0f);
        tickets_take(team, perMin / 60.0f);
    }
    {   // Caspian V5: the in-game ticket display only follows the game's own ticket changes, not the bot deaths BotMod
        // charges, so it can show tickets a team no longer has. Say the real counts in chat as the end nears, and log
        // the counter's fields once a minute (to find the replicated display value).
        static unsigned toldAt, dumpedAt;
        float us = *tickets_of(1), ru = *tickets_of(2), low = us < ru ? us : ru;
        unsigned every = low < 30 ? 15 * 30 : low < 150 ? 60 * 30 : 0;
        if (every && (!toldAt || H->frame - toldAt >= every)) {
            toldAt = H->frame ? H->frame : 1;
            chat_say("[AltBotMod] Tickets (bot deaths included): US %.0f, RU %.0f", us, ru);
        }
        if (!dumpedAt || H->frame - dumpedAt >= 60 * 30) {
            dumpedAt = H->frame ? H->frame : 1;
            bot_log("tickets: US %.1f RU %.1f - %u bot respawns charged, %u first deploys free so far", us, ru, g_chargedDeploys, g_freeDeploys);
            for (int t = 1; t <= 2; t++) if (!IsBadReadPtr(g_ticketCounter[t] + 0x80, 0x40)) {
                char line[400]; int n = snprintf(line, sizeof line, "tickets: team %d counter +80..+bf:", t);
                for (int o = 0x80; o < 0xc0 && n < (int)sizeof line - 16; o += 4) n += snprintf(line + n, sizeof line - n, " %.1f", *(float *)(g_ticketCounter[t] + o));
                bot_log("%s", line);
            }
        }
    }
    for (int team = 1; team <= 2; team++) {   // out of tickets: the game should end the round; if not, BotMod does
        int initial = *(int *)(g_ticketCounter[team] + OFF_TC_INITIAL);   // a team that has tickets at all (starting count)
        if (*tickets_of(team) > 0 || initial <= 0 || H->frame - g_ticketsFoundAt < 10 * 30) continue;
        if (!g_zeroAt) { g_zeroAt = H->frame ? H->frame : 1; bot_log("tickets: %s out of tickets", team == 1 ? "US" : "RU"); }
        else if (H->frame - g_zeroAt > 10 * 30) {
            int winner = 3 - team;
            g_roundEnded = 1;
            g_roundEndedAt = H->frame ? H->frame : 1;
            bot_log("tickets: the round did not end by itself - ending it (%s win)", winner == 1 ? "US" : "RU");
            chat_say("[AltBotMod] %s are out of tickets - %s win!", team == 1 ? "US" : "RU", winner == 1 ? "US" : "RU");
            chat_rcon("mapList.endRound\t%d", winner);
        }
        break;
    }
}

// ---- Swimming: bot-seconds spent in the water (nav water nodes), per level, on the perf line ----
static unsigned g_swimSeconds, g_swimSecondsLevel;
static void swim_tick(void)
{
    NavGrid *g = *nav_slot();
    if (H->frame % 30 || !g || g->phase != 2) return;
    for (int i = 0; i < H->botCount; i++) {
        char *pl = H->bots[i], *s = *(char **)(pl + OFF_PLAYER_SOLDIER);
        if (!s || player_vehicle(pl)) continue;
        float *p = (float *)(s + OFF_SOLDIER_POSITION);
        if (nav_is_water(g, p[0], p[1], p[2])) { g_swimSeconds++; g_swimSecondsLevel++; }
    }
}

static void inwatch_tick(void)
{
    if (!g_inWatchPl || H->frame > g_inWatchUntil || !player_exists(g_inWatchPl)) { g_inWatchPl = NULL; return; }
    char *in = *(char **)(g_inWatchPl + OFF_PLAYER_INPUT), *s = *(char **)(g_inWatchPl + OFF_PLAYER_SOLDIER);
    if (!in || IsBadReadPtr(in, 0x10 + 80 * 4)) return;
    char out[600];
    int k = 0;
    for (int a = 0; a < 80; a++) {
        float v = *(float *)(in + 0x10 + a * 4);
        if (fabsf(v - g_inWatchPrev[a]) > 0.01f && k < (int)sizeof out - 30) k += snprintf(out + k, sizeof out - k, " [%d] %.2f->%.2f", a, g_inWatchPrev[a], v);
        g_inWatchPrev[a] = v;
    }
    if (!IsBadReadPtr(in, 0x400))   // raw words outside the level floats (+0x10..+0x150): flags / edges
        for (int w = 0; w < 256; w++) {
            unsigned v = *(unsigned *)(in + w * 4);
            if ((w * 4 < 0x10 || w * 4 >= 0x150) && v != g_inWatchRaw[w] && k < (int)sizeof out - 30) k += snprintf(out + k, sizeof out - k, " +%03x:%08x->%08x", w * 4, g_inWatchRaw[w], v);
            g_inWatchRaw[w] = v;
        }
    int pose = s ? *(int *)(s + OFF_SOLDIER_POSE) : -1, want = s ? *(int *)(s + OFF_SOLDIER_POSE + 4) : -1;
    if (pose * 10 + want != g_inWatchPose && k < (int)sizeof out - 40) { k += snprintf(out + k, sizeof out - k, " | stance %d (to %d)", pose, want); g_inWatchPose = pose * 10 + want; }
    if (k) bot_log("inwatch:%s", out);
}

#define AIR_IS_GUNSHIP(v) vehicle_is_gunship(v)   // AltBotMod V23
#define AIR_LOCK_WANT(i, from, to, r) lock_want(i, from, to, r)   // AltBotMod V25 (harnesses: no lock)
#define AIR_LOCK_WANT_ENT(i, from, to, r, t) lock_want_ent(i, from, to, r, t)   // AltBotMod V29
#define AIR_LOCK_STATE(i) lock_state(i, NULL)
#define AIR_LOCK_COUNT() (++g_lockNativeLocks)
#include "air_native.inc"
static int bot_air_job(int i) { return g_air[i].phase != 0; }   // AltBotMod V11 (bot_on_foot)
// AltBotMod V11: class jobs that stand still by design, each bounded by its own timer: a medic at a body (10 s window),
// an engineer on the hull (repair limit), a supplier dropping its pack, an engineer lining up a rocket on a vehicle in
// range, a bot waiting for the medkit / ammo it asked for (15 s). The stuck watchdog leaves them be (V10 cut short 41
// rocket jobs and 5 supply runs in a 17-minute round).
static int class_job_working(int i, const float *p)
{
    BotBrain *b = bot_brain(i);
    if (b->task == TASK_REVIVE || (b->task == TASK_REPAIR && b->waiting) || (b->task == TASK_SUPPLY && b->step > 0)) return 1;
    if (b->task == TASK_NONE && g_supplyWaitFor[i] && H->frame < g_supplyWaitUntil[i]) return 1;
    if (b->task == TASK_ROCKET && b->serviceTarget && vehicle_alive(b->serviceTarget, g_rocketVt[i])) {
        float *vp = vehicle_pos(b->serviceTarget, NULL);
        if (vp && dist2(vp, p) < 200.0f * 200.0f) return 1;
    }
    return 0;
}
#define ROUTE_BOARD_BONUS(v,team) route_board_bonus(v,team)   // AltBotMod V15 (offline harnesses: 0)
static int boat_lane_to(int f, const float *from);   // AltBotMod V22 (trace_native.inc)
// AltBotMod V16: a boat only across water. V22: or when a Fun-Bots boat lane runs from near here to within 150 m of the flag and
// the flag is 300 m+ away (a river: Armored Shield had its RHIB and its two recorded lanes, and bots took the road)
#define FLEET_BOAT_NOT_NEEDED(v,from,f) (vehicle_is_boat(v) && !boat_trip_needed(from,objective_pos(f)) && !boat_lane_to(f,from))   // (offline harnesses: 0)
#define FLEET_ROUND_RUSH (g_levelReadyAt && H->frame-g_levelReadyAt<120*30)   // AltBotMod V23: the round-start rush to the vehicles
#include "fleet_native.inc"
#include "spawn_vehicle.inc"
#include "passenger_native.inc"
#include "air_dispatch.inc"
#include "abandoned_dispatch.inc"
#include "modes_native.inc"
#include "director_native.inc"
#include "trace_native.inc"
#include "route_actions_native.inc"   // AltBotMod V15
#include "emplacement_native.inc"
#include "activity_native.inc"
#include "missiles_native.inc"   // AltBotMod V23: missiles in flight - launches confirmed, flares / smoke before the hit
#include "gunship_native.inc"    // AltBotMod V23: the AC-130
#include "diagnostics_native.inc"
#include "livemap_native.inc"   // AltBotMod V13
#include "learn_native.inc"     // AltBotMod V21: learning (BotMod_learned)
#include "zone_native.inc"      // AltBotMod V34: Fun-Bots objective mesh
#include "squad_orders.inc"     // AltBotMod V34: squad orders

// ---- Enhanced native policy / diagnostics (all game actions stay on game thread) ----
static char *enhanced_trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) *--end = 0;
    return s;
}
static void enhanced_read_config(BMConfig *config, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[320]; int number = 0, accepted = 0;
    while (fgets(line, sizeof line, f)) {
        number++;
        if (!strchr(line, '\n') && !feof(f)) {
            int ch; while ((ch = fgetc(f)) != EOF && ch != '\n') {}
            bot_log("enhanced: ignored overlong line %d in %s", number, path); continue;
        }
        char *s = line;
        if (number == 1 && strlen(s) >= 3 && (unsigned char)s[0] == 0xef && (unsigned char)s[1] == 0xbb && (unsigned char)s[2] == 0xbf) s += 3;
        s[strcspn(s, "#;")] = 0;
        s = enhanced_trim(s);
        if (!*s || *s == '[') continue;
        char *equals = strchr(s, '=');
        if (!equals) { bot_log("enhanced: ignored line %d (expected key=value)", number); continue; }
        *equals++ = 0;
        int result = bm_set(config, enhanced_trim(s), enhanced_trim(equals));
        if (result == 1) accepted++;
        else bot_log("enhanced: ignored %s key/value '%s' on line %d in %s", result ? "invalid" : "unknown", s, number, path);
    }
    fclose(f);
    bot_log("enhanced: read %d setting(s) from %s", accepted, path);
}
static void enhanced_load_config(void)
{
    BMConfig next; bm_defaults(&next);
    char path[MAX_PATH + 80], name[64];
    botmod_file(path, sizeof path, "enhanced.ini"); enhanced_read_config(&next, path);
    snprintf(name, sizeof name, "enhanced_%d.ini", H->gamePort);
    botmod_file(path, sizeof path, name); enhanced_read_config(&next, path);
    // AltBotMod V25: settings still at the V9-V24 defaults move to the V25 ones (bots engage from further); a range set to
    // anything else is the owner's choice and stays
    if (bm_ranges_v25(&next)) bot_log("enhanced: engagement ranges at the old defaults moved to the V25 ones - infantry/recon/armor/armor-infantry %.0f/%.0f/%.0f/%.0f m",
        next.infantryRange, next.reconRange, next.armorRange, next.armorInfantryRange);
    if (next.driverWeapons && !next.armored) {
        bot_log("enhanced: DriverWeapons needs ArmoredVehicles=1; driver weapons disabled");
        next.driverWeapons = 0;
    }
    g_enhanced = next;
    memset(g_enhancedSteer, 0, sizeof g_enhancedSteer);
    if (next.armored || next.driverWeapons) bot_log("enhanced: experimental armoured vehicle behavior enabled; validate in game");
}
static void enhanced_status(void)
{
    bot_log("enhanced: %s; map=%s flags=%d ready=%d", BM_ENHANCED_VERSION, g_level[0] ? g_level : "(loading)", g_flagCount, g_levelReady);
    bot_log("enhanced: driving=%d light=%d boats=%d armored=%d driver-weapons=%d smoothing=%d ballistic=%d",
            g_enhanced.driving, g_enhanced.light, g_enhanced.boats, g_enhanced.armored, g_enhanced.driverWeapons,
            g_enhanced.smoothSteering, g_enhanced.ballisticAim);
    bot_log("enhanced: speed caps light/boat/tank/IFV %.1f/%.1f/%.1f/%.1f m/s; reserve humans %.0f m; stuck %.1f s",
            g_enhanced.lightMaxSpeed, g_enhanced.boatMaxSpeed, g_enhanced.tankMaxSpeed, g_enhanced.ifvMaxSpeed,
            g_enhanced.humanRadius, g_enhanced.stuckSeconds);
    int variants=0;for(int k=0;k<BM_WEAPON_VARIANTS;k++)variants+=g_weaponVariants[k].ready;
    bot_log("enhanced: infantry/recon/armor/armor-infantry ranges %.0f/%.0f/%.0f/%.0f m; reaction %.2f s; weapon-variety=%d attachments=%d available=%d",
            g_enhanced.infantryRange,g_enhanced.reconRange,g_enhanced.armorRange,g_enhanced.armorInfantryRange,
            g_enhanced.reactionSeconds,g_enhanced.weaponVariety,g_enhanced.weaponAttachments,variants);
    difficulty_status();
    air_status();
    fleet_status();
    bot_log("enhanced: staged-driving=%d transport-landings=%d deployment-learning=%d weapon-handling=%d armor-tactics=%d infantry-support=%d traffic=%d diagnostics=%d",
        g_enhanced.driveStages,g_enhanced.air.landings,g_enhanced.air.deploymentMenu,g_enhanced.weaponHandling,
        g_enhanced.armorTactics,g_enhanced.armorEscort,g_enhanced.traffic,g_enhanced.diagnostics);
    bot_log("enhanced: side-steps=%d (0 none, 1 short steps under fire, 2 constant) live-map=%d", g_enhanced.sideSteps, g_enhanced.liveMap);   // AltBotMod V14
    bot_log("enhanced: route-actions=%d recon-beacons=%d (Fun-Bots boarding spots, exit points, waits, crouch sections, beacon spots)", g_enhanced.routeActions, g_enhanced.reconBeacons);   // AltBotMod V15
    bot_log("enhanced: lock-on: the game's own lock for bots - ServerLockingController::update hook %s; locks put in place after the weapon's lock time on target: %s (%u so far)",
            g_lockHooked ? "on" : "NOT installed (lock-ons fall back to button presses)", g_lockFakeOn && g_enhanced.lockAssist ? "on" : "off", g_lockFakes);   // AltBotMod V25, V29
    bot_log("enhanced: mode=%s teams=%d; Rush attack/defend adapter enabled; native objective-state binding pending verification; replication/flight require in-game validation",g_modeName,mode_teams());
}
static void enhanced_vehicles(void)
{
    char **vehicles; int count = vehicle_list(&vehicles);
    bot_log("enhanced: %d cached vehicle object(s)", count);
    for (int k = 0; k < count; k++) {
        char *v = vehicles[k]; if (!vehicle_ok(v)) continue;
        const BMVehicleProfile *p = bm_vehicle_profile(vehicle_name(v));
        char **entries; int seats = vehicle_seats(v, &entries), team = 0;
        float *pos = vehicle_pos(v, &team);
        const BMAirProfile *airframe = bm_air_profile(vehicle_name(v));
        bot_log("enhanced: vehicle=%p name=%s class=%s enabled=%d team=%d seats=%d free=%d pos=(%.1f,%.1f,%.1f) US=%s RU=%s claimed=%d",
                v, vehicle_name(v), airframe ? airframe->name : p->label,
                airframe ? g_enhanced.driving && bm_air_allowed(&g_enhanced.air, airframe) : bm_vehicle_allowed(&g_enhanced, p), team, seats, free_seat_count(v),
                pos ? pos[0] : 0, pos ? pos[1] : 0, pos ? pos[2] : 0, fleet_reason(v,1), fleet_reason(v,2), fleet_claimed(v,-1));
    }
}
static void enhanced_forget(int i)
{
    bm_rush_release(&g_rush,H->bots[i]);
    // Roster removal compacts bot indexes. Vehicle tables in the beta were
    // not compacted, so a surviving bot could inherit another bot's drive.
#define FORGET(a) bm_forget_slot(a,sizeof (a)[0],i,H->botCount)
    FORGET(g_lmProg);   // AltBotMod V13: live map no-progress watch
    FORGET(g_lockWantAt);FORGET(g_lockWantPos);FORGET(g_lockWantDir);FORGET(g_lockWantReach);FORGET(g_lockCtrl);FORGET(g_lockCtrlAt);FORGET(g_lockXf);FORGET(g_lockAttempt);FORGET(g_lockTargetFrames);FORGET(g_lockForced);FORGET(g_lockBest);FORGET(g_lockSeenFrames);FORGET(g_lockNativeSince);FORGET(g_lockAttemptAt);   // AltBotMod V25-V26: the game's lock
    FORGET(g_lockWantEnt);FORGET(g_lockWantEntSince);FORGET(g_lockWantEntAt);FORGET(g_lockFakeAt);FORGET(g_gunNoMissiles);   // AltBotMod V29: the lock put in place
    FORGET(g_vring);FORGET(g_joinFailAt);FORGET(g_joinFailId);FORGET(g_joinFailPos);FORGET(g_breachUntil);FORGET(g_supplyFailTo);FORGET(g_zonePathAt);FORGET(g_supplyFailAt);FORGET(g_breachKnife);FORGET(g_breachMat);FORGET(g_paneBest);FORGET(g_paneBestAt);FORGET(g_paneWalkUntil);FORGET(g_paneSpotAt);FORGET(g_paneSpotTries);FORGET(g_paneSpot);FORGET(g_breachAt);FORGET(g_breachYaw);FORGET(g_breachPitch);FORGET(g_breachPos);FORGET(g_breachFrom);FORGET(g_breachTo);FORGET(g_breachTries);FORGET(g_gunSeenFor);FORGET(g_gunSeenAt);FORGET(g_gunSeenOk);FORGET(g_exitAt);FORGET(g_swimming);FORGET(g_swimSide);FORGET(g_swimCheckAt);FORGET(g_swimSince);FORGET(g_swimProgAt);FORGET(g_swimTurnUntil);FORGET(g_swimProg);FORGET(g_escapeFails);FORGET(g_escapeFailAt);FORGET(g_escapeUntil);FORGET(g_escapeJumpAt);FORGET(g_escapeGoal);FORGET(g_escapeProg);FORGET(g_gunArcSkip);FORGET(g_gunArcSkipUntil);FORGET(g_gunTurned);
    FORGET(g_learnPending);FORGET(g_learnJumpAt);FORGET(g_learnPreJumpAt);   // AltBotMod V21: learning (a stuck spot being judged, its jump)
    FORGET(g_raHoldIdx);FORGET(g_raHoldUntil);FORGET(g_raCrouched);FORGET(g_raBeacon);FORGET(g_defendUntil);   // AltBotMod V15
    FORGET(g_aimPitch);
    // AltBotMod V10: the V9 gunfight tables were not compacted (a bot could inherit another's grenade throw / side step)
    FORGET(g_fightCrouch);FORGET(g_dodgeDir);FORGET(g_dodgeFlipAt);FORGET(g_dodgeCheckAt);FORGET(g_dodgeBlocked);FORGET(g_dodgeLastDir);
    FORGET(g_jumpFightAt);FORGET(g_jumpRunAt);FORGET(g_nadeStep);FORGET(g_nadeAt);FORGET(g_nadeCooldown);FORGET(g_nadeAim);FORGET(g_nadeSlot);
    FORGET(g_snipe);
    FORGET(g_classDropAt);FORGET(g_supplyWaitFor);FORGET(g_supplyWaitUntil);   // V11
    FORGET(g_weaponVariantLife);
    FORGET(g_primaryDoctrine);
    FORGET(g_armor);
    FORGET(g_vehicleDiag);
    FORGET(g_deployActive);
    FORGET(g_deploymentIdle);
    FORGET(g_activity);
    FORGET(g_traceUse);FORGET(g_traceRepeat);FORGET(g_missileWarnAt);   // AltBotMod V22 / V23
    FORGET(g_aimValue);FORGET(g_aimValueAt);FORGET(g_aimPose);FORGET(g_directUse);
    FORGET(g_aimFixV);FORGET(g_aimFixH);FORGET(g_aimClimb);FORGET(g_burstStart);FORGET(g_pitchHist);FORGET(g_pitchHistAt);FORGET(g_pitchPrevV);FORGET(g_pitchPrevL);FORGET(g_pitchPrevAt);FORGET(g_aimUpdAt);FORGET(g_aimFixS);FORGET(g_aimLanding);FORGET(g_aimFixSet);FORGET(g_crewMoveAt);FORGET(g_crewVehicle);FORGET(g_crewDriver);FORGET(g_crewAt);FORGET(g_empIdleSince);FORGET(g_empCooldown);
    FORGET(g_air);
    FORGET(g_airCooldown);
    FORGET(g_askedAt);
    FORGET(g_attackedAt);
    FORGET(g_attacker);
    FORGET(g_boardUntil);
    FORGET(g_botFiredAt);
    FORGET(g_brainHeld);
    FORGET(g_chuteOpen);
    FORGET(g_cooldownWho);
    FORGET(g_coverHitAt);
    FORGET(g_coverHits);
    FORGET(g_coverOpen);
    FORGET(g_coverProne);
    FORGET(g_createdAt);
    FORGET(g_drive);
    FORGET(g_driveCooldown);
    FORGET(g_driving);
    FORGET(g_engageCooldown);
    FORGET(g_enhancedSteer);
    FORGET(g_fallLandedFrames);
    FORGET(g_fallLastY);
    FORGET(g_fallPressAt);
    FORGET(g_fallPressVy);
    FORGET(g_fallSoldier);
    FORGET(g_fallVy);
    FORGET(g_fenceAt);
    FORGET(g_fencePos);
    FORGET(g_fenceStrafe);
    FORGET(g_fenceTries);
    FORGET(g_fenceUntil);
    FORGET(g_panelCheckAt);FORGET(g_panelMat);FORGET(g_panelStage);FORGET(g_panelHit);FORGET(g_panelFrom);   // AltBotMod V18
    FORGET(g_fightAdvance);
    FORGET(g_fightBursts);
    FORGET(g_fightChase);
    FORGET(g_fightHitsTaken);
    FORGET(g_fightLast);
    FORGET(g_fightMoveUntil);
    FORGET(g_combatMove);
    FORGET(g_fightRetreat);
    FORGET(g_fightSeenAt);
    FORGET(g_fightSlot);
    FORGET(g_fightSpot);
    FORGET(g_fightTarget);
    FORGET(g_fightTgtHp);
    FORGET(g_fineAim);
    FORGET(g_fleet);
    FORGET(g_director.unit);FORGET(g_directorIdentity);g_directorAt=0;
    FORGET(g_gliding);
    FORGET(g_grenadesThisLife);
    FORGET(g_gunAimH);
    FORGET(g_gunAirMotion);FORGET(g_gunOwnMotion);FORGET(g_gunPrevVel);FORGET(g_gunAccel);FORGET(g_gunSlot);FORGET(g_gunSlotWant);FORGET(g_gunSlotAt);FORGET(g_gunSlotCycle);   // AltBotMod V22
    FORGET(g_gunAirWho);
    FORGET(g_gunBad);
    FORGET(g_gunBurstAt);
    FORGET(g_gunCand);
    FORGET(g_gunCandFwd);
    FORGET(g_gunCandLocal);
    FORGET(g_gunCandN);
    FORGET(g_gunCheckAt);
    FORGET(g_gunFire);
    FORGET(g_gunLastYaw);
    FORGET(g_gunLostAt);
    FORGET(g_gunNudging);
    FORGET(g_gunMoving);FORGET(g_gunNudgeYaw);FORGET(g_rushBlind);FORGET(g_lockSince);FORGET(g_lockCheckAt);FORGET(g_lockFirstAt);FORGET(g_lockAmmo);FORGET(g_lockStrat);FORGET(g_lockTry);FORGET(g_botGadget);FORGET(g_gadgetLife);FORGET(g_c4Mode);FORGET(g_c4Away);FORGET(g_modeBotSlot);FORGET(g_rideStopSince);FORGET(g_cmHealth);FORGET(g_cmAt);FORGET(g_zoneHoldFlag);FORGET(g_zoneHoldSince);   // AltBotMod V16
    FORGET(g_gunScanAt);
    FORGET(g_gunStepAt);
    FORGET(g_gunStuck);
    FORGET(g_gunTarget);
    FORGET(g_gunTargetVeh);
    FORGET(g_gunTries);
    FORGET(g_gunVeh);
    FORGET(g_gunWeapon);
    FORGET(g_hasSector);
    FORGET(g_hitAt);
    FORGET(g_lastHealth);
    FORGET(g_lastSpawnedAt);
    FORGET(g_lastSpawnedBot);
    FORGET(g_launcherMax);
    FORGET(g_leadAt);
    FORGET(g_leadPos);
    FORGET(g_leadVel);
    FORGET(g_leadWho);
    FORGET(g_local_botmod_tick_fireHeld);
    FORGET(g_local_botmod_tick_zoomHeld);
    FORGET(g_local_brain_tick_nextTry);
    FORGET(g_local_brain_tick_wasRiding);
    FORGET(g_local_cover_react_spottedAt);
    FORGET(g_local_downed_tick_primaryAt);
    FORGET(g_local_repair_scan_losAt);
    FORGET(g_local_repair_scan_whyAt);
    FORGET(g_local_rocket_tick_lastAt);
    FORGET(g_local_rocket_tick_lastPos);
    FORGET(g_local_self_requests_ammoAt);
    FORGET(g_local_self_requests_healAt);
    FORGET(g_local_steer_bot_jat);
    FORGET(g_local_steer_bot_jx);
    FORGET(g_local_steer_bot_jz);
    FORGET(g_local_stuck_jump_lastAt);
    FORGET(g_mines);
    FORGET(g_modeBot);
    FORGET(g_vehicleApproach);
    FORGET(g_noRouteSince);
    FORGET(g_overflowFlag);
    FORGET(g_flagRoute);
    FORGET(g_pickDone);
    FORGET(g_pickDoneAt);
    FORGET(g_pickPack);
    FORGET(g_pickStandUntil);
    FORGET(g_pickUntil);
    FORGET(g_pitchTest);
    FORGET(g_pitchTestOn);
    FORGET(g_rocketCooldown);
    FORGET(g_rocketGrenade);
    FORGET(g_rocketShots);
    FORGET(g_rocketVt);
    FORGET(g_rocketsThisLife);
    FORGET(g_seatPitch);
    FORGET(g_seatYaw);
    FORGET(g_sectorYaw);
    FORGET(g_silentRemove);
    FORGET(g_spareMax);
    FORGET(g_sprintUntil);
    FORGET(g_squadJoinAt);
    FORGET(g_spawnJoinWait);
    FORGET(g_stairAt);
    FORGET(g_stairAx);
    FORGET(g_stairAz);
    FORGET(g_stairCheckAt);
    FORGET(g_stairLevel);
    FORGET(g_stairLevelPos);
    FORGET(g_stairMovedAt);
    FORGET(g_stairMovedPos);
    FORGET(g_stairOn);
    FORGET(g_stairJumped);FORGET(g_wayUntil);FORGET(g_wayCheckAt);FORGET(g_wayAskedAt);FORGET(g_wayStrafe);FORGET(g_wayLast);FORGET(g_wayThrottle);FORGET(g_waySkips);FORGET(g_directTgt);FORGET(g_directKind);   // AltBotMod V18
    FORGET(g_standUpAt);
    FORGET(g_strandedSince);
    FORGET(g_takeoverSince);
    FORGET(g_threat);
    FORGET(g_threatVeh);
    FORGET(g_threatVehHidden);
    FORGET(g_vaimOn);
    FORGET(g_vaimPitch);
    FORGET(g_vaimYaw);
    FORGET(g_vaultFrom);
    FORGET(g_vaultUntil);
    FORGET(g_crouchWalkUntil);
    FORGET(g_stepCandidate);
    FORGET(g_vehChanceUntil);
    FORGET(g_vehThreatCooldown);
    FORGET(g_wpNearAt);
    FORGET(g_wpNearIdx);
#undef FORGET
    if (g_gunCalibBot == i) g_gunCalibBot = -1;
    else if (g_gunCalibBot > i) g_gunCalibBot--;
    for (int k = 0; k < 64; k++) {
        if (g_downed[k].medic == i + 1) g_downed[k].medic = 0;
        else if (g_downed[k].medic > i + 1) g_downed[k].medic--;
    }
}
static void enhanced_level_reset(void)
{
    nav_free(g_navExpansion);g_navExpansion=NULL;
    memset(g_spawnJoinWait,0,sizeof g_spawnJoinWait);memset(g_combatMove,0,sizeof g_combatMove);
    memset(g_hqOptionCount,0,sizeof g_hqOptionCount);
    memset(g_weaponVariantLife,0,sizeof g_weaponVariantLife);
    memset(g_primaryDoctrine,0,sizeof g_primaryDoctrine);
    memset(g_armor,0,sizeof g_armor);memset(g_vehicleDiag,0,sizeof g_vehicleDiag);
    g_lockMessageCount=g_missileMessageCount=0;
    memset(g_vehicleApproach,0,sizeof g_vehicleApproach);
    memset(&g_director,0,sizeof g_director);memset(g_directorIdentity,0,sizeof g_directorIdentity);
    g_directorAt=g_directorSig=0;g_directorEpoch=0;
    g_fleetPriorityUntil=0;memset(g_deploymentIdle,0,sizeof g_deploymentIdle);
    memset(g_activity,0,sizeof g_activity);g_activityProbeFrame=g_activityRetireAt=0;
    g_navVehicleCoverageEpoch=-1;
    memset(g_fleetRouteFailure,0,sizeof g_fleetRouteFailure);
    memset(g_roadLease,0,sizeof g_roadLease);g_tacticProbeFrame=0;g_tacticProbes=0;
    mode_reset();memset(g_hqPos,0,sizeof g_hqPos);memset(g_hqCpPos,0,sizeof g_hqCpPos);memset(g_hqCpKnown,0,sizeof g_hqCpKnown);
    // Clear weak pointers instead of calling into entities being torn down.
    memset(g_drive, 0, sizeof g_drive); memset(g_driveCooldown, 0, sizeof g_driveCooldown);
    memset(g_air, 0, sizeof g_air); memset(g_airCooldown, 0, sizeof g_airCooldown);
    g_airLandingProbeFrame=g_airPickupProbeFrame=0;
    memset(g_fleet, 0, sizeof g_fleet); memset(g_fleetBase, 0, sizeof g_fleetBase);
    memset(g_fleetBasePos, 0, sizeof g_fleetBasePos); memset(g_fleetBaseArgs, 0, sizeof g_fleetBaseArgs);
    memset(g_takeoverSince, 0, sizeof g_takeoverSince);
    memset(g_driving, 0, sizeof g_driving); memset(g_enhancedSteer, 0, sizeof g_enhancedSteer);
    memset(g_localFlag, 0, sizeof g_localFlag); memset(g_localUntil, 0, sizeof g_localUntil); memset(g_localOrder, 0, sizeof g_localOrder);
    memset(g_overflowFlag,0,sizeof g_overflowFlag);
    memset(g_flagRoute,0,sizeof g_flagRoute);nav_cancel_paths();
    memset(g_vehChanceUntil, 0, sizeof g_vehChanceUntil); memset(g_boardUntil, 0, sizeof g_boardUntil);
    memset(g_strandedSince, 0, sizeof g_strandedSince); memset(g_askedAt, 0, sizeof g_askedAt);
    memset(g_vehBad, 0, sizeof g_vehBad); memset(g_vehBadUntil, 0, sizeof g_vehBadUntil);
    memset(g_vaimOn, 0, sizeof g_vaimOn);
    g_gunCalibBot = -1;
    for (int i = 0; i < MAX_BOTS; i++) gunner_reset(i);
}

#include "round_native.inc"

#include "combat_walk_fire.inc"
// AltBotMod V30: a bot in the water swims - towards its objective (the flag it is after, else the nearest objective), the
// way a player heads for the shore. On foot the bot follows the navigation grid, which has no cells on open water: a bot
// that fell off a pier or out of a boat on Wake Island stood in the water and drowned or waited to be shot. In the water
// (a ray down to the soldier hits the water's surface 0.9 m+ above its feet, checked every 1/3 s) it swims straight at the
// goal, turns off 70 deg for 2 s when it stops getting anywhere (a rock, a hull), and plans its route again on land.
static int soldier_in_water(const float *p)
{
    // the water's surface 0.9 m or more above its feet: swimming depth (a ford or the surf on a beach is waded on the route)
    float a[3] = {p[0], p[1] + 3.0f, p[2]}, b[3] = {p[0], p[1] + 0.9f, p[2]}, hit[3];
    return ray_cast(a, b, hit, NULL) && nav_hit_class() == NAV_HIT_WATER;
}
static const float *swim_goal(int i, const float *p)
{
    BotBrain *b = bot_brain(i);
    if (b->targetFlag >= 0 && b->targetFlag < g_flagCount) { float *f = flag_pos(b->targetFlag); if (f && (f[0] != 0 || f[2] != 0)) return f; }
    const float *best = NULL; float bd = 1e30f;
    for (int f = 0; f < objective_count(); f++) {
        const float *o = objective_pos(f); if (!o) continue;
        float dx = o[0] - p[0], dz = o[2] - p[2], d = dx * dx + dz * dz;
        if (d < bd) { bd = d; best = o; }
    }
    return best;
}
// AltBotMod V30: a bot with no route to any objective three times in 20 s, with water between it and the objective within
// 300 m (Wake Island's carrier, a pier, a rock), walks straight at the objective - off the edge into the sea - and swims
// from there (swim_steer_tick). Up to 45 s; then the usual recovery. (Wake Island live: bots stood on the carrier deck,
// "no route to flag N (A* exhausted after 3695 nodes)" for minutes.)
static void escape_consider(int i, char *soldier, int f)
{
    if (!soldier || player_vehicle(H->bots[i]) || g_swimming[i] || H->frame < g_escapeUntil[i] || f < 0 || f >= g_flagCount) return;
    if (!g_escapeFailAt[i] || H->frame - g_escapeFailAt[i] > 20 * 30) { g_escapeFails[i] = 0; g_escapeFailAt[i] = H->frame ? H->frame : 1; }
    if (++g_escapeFails[i] < 3) return;
    g_escapeFails[i] = 0; g_escapeFailAt[i] = 0;
    NavGrid *g = *nav_slot();
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION), *goal = flag_pos(f);
    if (!g || g->phase != 2 || !goal) return;
    float w = nav_water_along(g, pos[0], pos[2], goal[0], goal[2], 300.0f);
    if (w < 0) return;
    memcpy(g_escapeGoal[i], goal, 12);
    g_escapeUntil[i] = H->frame + 45 * 30; g_escapeJumpAt[i] = H->frame + 45; g_escapeProg[i][0] = pos[0]; g_escapeProg[i][1] = pos[2]; g_escapes++;
    if (g_escapes <= 10 || g_escapes % 25 == 0)
        bot_log("swim: %s is cut off at (%.0f, %.1f, %.0f) - no route to any objective; water %.0f m away towards flag %d: goes over the side and swims (%u so far)",
                (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, pos[0], pos[1], pos[2], w, f, g_escapes);
}
static int escape_steer(int i, char *soldier)
{
    if (!g_escapeUntil[i] || H->frame >= g_escapeUntil[i] || g_swimming[i] || !soldier) { if (g_swimming[i]) g_escapeUntil[i] = 0; return 0; }
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION), yaw = *(float *)(soldier + OFF_SOLDIER_YAW);
    float want = yaw_towards(g_escapeGoal[i][0] - pos[0], g_escapeGoal[i][2] - pos[2]), t = fmodf(want, 6.2831853f);
    if (t > 0) t -= 6.2831853f;
    float up = t - yaw, down = up > 0 ? yaw - t + 6.2831853f : yaw - t, err = (up > 0 && up < down) ? up : -down;
    H->botMove[i][0] = fabsf(err) < 0.8f ? 1.0f : 0.0f; H->botMove[i][1] = 0; H->botMove[i][2] = fmaxf(-1.0f, fminf(1.0f, -err * 2.0f));
    if (H->frame >= g_escapeJumpAt[i]) {   // a rail or a lip in the way: jump it
        float mx = pos[0] - g_escapeProg[i][0], mz = pos[2] - g_escapeProg[i][1];
        if (mx * mx + mz * mz < 1.0f) press_button(i, EIA_JUMP, 4);
        g_escapeJumpAt[i] = H->frame + 45; g_escapeProg[i][0] = pos[0]; g_escapeProg[i][1] = pos[2];
    }
    g_progress[i].frame = H->frame; g_progress[i].x = pos[0]; g_progress[i].z = pos[2];
    return 1;
}
static void swim_steer_tick(int i, char *soldier)
{
    if (!soldier || player_vehicle(H->bots[i])) { g_swimming[i] = 0; return; }
    float *pos = (float *)(soldier + OFF_SOLDIER_POSITION);
    if (H->frame >= g_swimCheckAt[i]) {
        g_swimCheckAt[i] = H->frame + 10;
        int wet = soldier_in_water(pos);
        if (wet && !g_swimming[i]) {
            g_swimming[i] = 1; g_swimSince[i] = g_swimProgAt[i] = H->frame ? H->frame : 1; g_swimProg[i][0] = pos[0]; g_swimProg[i][1] = pos[2]; g_swimStarts++;
            if (g_swimStarts <= 10 || g_swimStarts % 25 == 0)
                bot_log("swim: %s is in the water at (%.0f, %.0f) - swims for its objective (%u so far)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, pos[0], pos[2], g_swimStarts);
        } else if (!wet && g_swimming[i]) {
            g_swimming[i] = 0;
            reroute_from_here(i, soldier, "out of the water");
        }
    }
    if (!g_swimming[i]) { escape_steer(i, soldier); return; }
    g_escapeUntil[i] = 0;
    const float *goal = swim_goal(i, pos);
    float yaw = *(float *)(soldier + OFF_SOLDIER_YAW), want;
    if (goal) want = yaw_towards(goal[0] - pos[0], goal[2] - pos[2]);
    else want = yaw;   // (nothing to head for: keep the heading)
    if (H->frame - g_swimProgAt[i] >= 90) {   // 3 s: less than 2 m covered - something is in the way: turn off for 2 s
        float mx = pos[0] - g_swimProg[i][0], mz = pos[2] - g_swimProg[i][1];
        if (mx * mx + mz * mz < 2.0f * 2.0f) { g_swimTurnUntil[i] = H->frame + 60; g_swimSide[i] ^= 1; }
        g_swimProgAt[i] = H->frame; g_swimProg[i][0] = pos[0]; g_swimProg[i][1] = pos[2];
    }
    if (H->frame < g_swimTurnUntil[i]) want += g_swimSide[i] ? 1.22f : -1.22f;
    bot_nav(i)->targetYaw = want;
    float t = fmodf(want, 6.2831853f);
    if (t > 0) t -= 6.2831853f;
    float up = t - yaw, down = up > 0 ? yaw - t + 6.2831853f : yaw - t, err = (up > 0 && up < down) ? up : -down;
    H->botMove[i][0] = fabsf(err) < 1.2f ? 1.0f : 0.3f;   // (swimming, turning on the spot is slow: keep moving)
    H->botMove[i][1] = 0;
    H->botMove[i][2] = fmaxf(-1.0f, fminf(1.0f, -err * 2.0f));
    g_progress[i].frame = H->frame; g_progress[i].x = pos[0]; g_progress[i].z = pos[2];   // (no on-foot stuck handling meanwhile)
}
static void bot_motion_tick(int i,void *in)
{
        crew_board_tick(i);   // Caspian V4.1
        gunship_tick(i);      // AltBotMod V23
        emplacement_tick(i);  // Caspian V5
        fleet_takeover(i);
        air_consider(i);
        { double t0 = perf_now(); int k = g_air[i].phase ? 1 : 0;   // (V31.1: timed for the perf line)
          if (g_air[i].phase) air_tick(i);
          else if (g_drive[i].phase) drive_tick(i);
          double dt = perf_now() - t0; g_subMs[k] += dt; if (dt > g_subMax[k]) g_subMax[k] = dt; }
        // Only infantry steering owns infantry controls. A stale on-foot
        // navigation mode must never zero a seated driver's throttle.
        if (!player_vehicle(H->bots[i])) {
            steer_bot(i, *(char **)(H->bots[i] + OFF_PLAYER_SOLDIER));
            combat_walk_fire(i);
            combat_dodge_tick(i);   // AltBotMod V9: side steps / hops
            make_way_tick(i, *(char **)(H->bots[i] + OFF_PLAYER_SOLDIER));   // AltBotMod V18
            panel_check_tick(i);   // AltBotMod V18: glass
            swim_steer_tick(i, *(char **)(H->bots[i] + OFF_PLAYER_SOLDIER));   // AltBotMod V30: in the water - swim for the objective
            breach_tick(i, *(char **)(H->bots[i] + OFF_PLAYER_SOLDIER));   // AltBotMod V32: shooting a way open
        }
        if (level_ready()) { double t0 = perf_now(); gunner_tick(i); double dt = perf_now() - t0; g_subMs[2] += dt; if (dt > g_subMax[2]) g_subMax[2] = dt; fall_tick(i); }
        if (player_vehicle(H->bots[i]) && g_driving[i]) {   // a driver: S is the brake / reverse (EIABrake), not a negative throttle
            float t = H->botMove[i][0];
            ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in, EIA_THROTTLE, t > 0 ? t : 0.0f);
            ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in, EIA_BRAKE, t < 0 ? -t : 0.0f);
        } else ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in, EIA_THROTTLE, H->botMove[i][0]);
        ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in, EIA_STRAFE, H->botMove[i][1]);
        ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in, EIA_YAW, H->botMove[i][2]);
}

__declspec(dllexport) void botmod_tick(void)
{
    inwatch_tick();
    {
        char who[64], text[256];
        for (int k = 0; k < 4 && chat_poll(who, sizeof who, text, sizeof text); k++) chat_command(who, text);
    }
    double tStart = perf_now(), tMark = tStart;
    perf_report();
    round_roster_tick();
    validate_bots();
    g_where="live map";g_whereBot=-1;
    livemap_tick();   // AltBotMod V13: BotMod\LiveMap for Server Control (LiveMap=0: only drains the kill queue)
    g_where="learning";learn_tick();   // AltBotMod V21
    if (g_roundCleanup) { removal_tick(); if(!H->botCount)g_roundCleanup=0; return; }
    level_tick();
    mode_observation_tick();
    deployment_tick();
    if(!level_ready()) return;
    // No bots wanted and none here (e.g. a vehicles-only instance with botcount_<port>.txt = 0): idle - chat commands
    // (above) and the roster (so "!bots 8" still works) only; no nav building, surveys, level discovery or scans.
    if (*wanted_bots() == 0 && H->botCount == 0) {
        static unsigned idleLogged;
        if (!idleLogged) { idleLogged = 1; bot_log("idle: no bots wanted on this server - BotMod stays out of the way (\"!bots <n>\" to start)"); }
        roster_tick();
        return;
    }
    // Rotate the first dispatcher so both teams share the bounded route budget.
    g_where="deployment navigation coverage";g_whereBot=-1;
    navigation_vehicle_coverage_tick();
    g_where="queued navigation paths";
    { double a=perf_now();nav_process_paths(nav_pending_paths()>4?PLAN_MS_BUSY:PLAN_MS_PER_TICK);perf_add(7,perf_now()-a);
      tMark=perf_now(); }   // AltBotMod V12: route time is reported once, under paths (it was also counted in input+steer+gunner)
    // Complete vehicle assignments before any bot executes flag movement.
    g_where="fleet inventory and assignments";
    fleet_inventory_tick();
    director_tick();
    fleet_air_dispatch();
    fleet_abandoned_dispatch();
    director_summary_tick();
    trace_stats_tick();
    combat_stats_tick();
    g_where="missiles";missile_tick();   // AltBotMod V23
    {   static unsigned at, last[JOB_COUNT];   // AltBotMod V11: what the classes did in the last minute
        if (H->frame - at >= 60 * 30) {
            at = H->frame;
            unsigned d[JOB_COUNT]; int any = 0;
            for (int k = 0; k < JOB_COUNT; k++) { d[k] = g_jobStats[k] - last[k]; last[k] = g_jobStats[k]; any |= d[k] != 0; }
            if (any) bot_log("classes: last minute %u revive run(s), %u revived; %u repair(s) started, %u finished; %u supply run(s), %u delivered; %u medkit(s) and %u ammo box(es) dropped unasked",
                             d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
        }
    }
    sniper_stats_tick();
    for (int turn = 0; turn < H->botCount; turn++) {
        int i=(H->frame+turn)%H->botCount;
        void *in = H->botInput[i];
        if (!in) continue;
        g_whereBot=i;g_where="vehicle driver and pilot input";
        bot_motion_tick(i,in);
        ((SetFloat_t)ADDR_ENTRYINPUT_SET_DT)(in, BOT_TICK_DT);
        vehicle_countermeasure_tick(i);   // AltBotMod V16
        apply_presses(i, in);
        if (H->frame < g_sprintUntil[i] && !route_crouched(i) && !(g_driving[i] && player_vehicle(H->bots[i]))) { *(unsigned *)((char *)in + OFF_INPUT_FLAGS_A) |= SPRINT_BIT_A; *(unsigned *)((char *)in + OFF_INPUT_FLAGS_B) |= SPRINT_BIT_B; }   // (V29: never at the wheel)
        else { *(unsigned *)((char *)in + OFF_INPUT_FLAGS_A) &= ~SPRINT_BIT_A; *(unsigned *)((char *)in + OFF_INPUT_FLAGS_B) &= ~SPRINT_BIT_B; }
        {   // repairing: trigger held down continuously while in contact with the vehicle, released the moment that ends

            BotBrain *rb = bot_brain(i);
            int hold = ((rb->task == TASK_REPAIR || rb->task == TASK_REVIVE) && rb->step == 2 && rb->waiting) || (rb->task == TASK_FIGHT && rb->step == 2 && infantry_trigger_ready(i,*(char **)(H->bots[i]+OFF_PLAYER_SOLDIER))) || g_gunFire[i];
            if (hold) { ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in, EIA_FIRE, 1.0f); g_botFiredAt[i] = H->frame ? H->frame : 1; }
            else if (g_local_botmod_tick_fireHeld[i]) ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in, EIA_FIRE, 0.0f);
            g_local_botmod_tick_fireHeld[i] = (unsigned char)hold;
            // aim down the sights (hold to zoom) while fighting on foot at more than ADS_MIN_RANGE: hip fire spreads badly
            // at range (a sniper's from the hip is a lottery); close in, the hip is quicker. From lining up (step 1)
            // through the bursts and pauses until the fight ends.
            #define ADS_MIN_RANGE 20.0f

            char *ms = *(char **)((char *)H->bots[i] + OFF_PLAYER_SOLDIER);
            int zoom = rb->task == TASK_FIGHT && rb->step >= 1 && !player_vehicle((char *)H->bots[i]) && ms && !g_nadeStep[i];
            if (zoom) {
                char *t = g_fightTarget[i], *ts = t && player_exists(t) ? *(char **)(t + OFF_PLAYER_SOLDIER) : NULL;
                float ads=g_enhanced.weaponHandling?infantry_weapon(i)->adsMeters:ADS_MIN_RANGE;
                if(bot_profile(i) && !is_recon(i))ads=fminf(ads,bot_profile(i)->ads);   // V9: elite aims down the sights from 18 m
                zoom = ts && dist2((float *)(ts + OFF_SOLDIER_SPAWN_POSITION), (float *)(ms + OFF_SOLDIER_POSITION)) > ads * ads;
            }
            if (zoom) ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in, EIA_ZOOM, 1.0f);
            else if (g_local_botmod_tick_zoomHeld[i]) ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in, EIA_ZOOM, 0.0f);
            g_local_botmod_tick_zoomHeld[i] = (unsigned char)zoom;
            // look down at the hull while repairing; level again otherwise
            char *s = *(char **)((char *)H->bots[i] + OFF_PLAYER_SOLDIER);
            float want = g_pitchTestOn[i] ? g_pitchTest[i] : g_breachUntil[i] > H->frame ? g_breachPitch[i] : (rb->task == TASK_REPAIR && rb->step >= 1) ? REPAIR_PITCH
                       : (rb->task == TASK_REVIVE && rb->step >= 1) ? REVIVE_PITCH
                       : (rb->task == TASK_ROCKET || rb->task == TASK_FIGHT) ? g_aimPitch[i] : 0.0f;
            if (player_vehicle((char *)H->bots[i]) && g_vehInput) {   // in a seat: turret turn / elevation levels
                ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in, EIA_SEAT_YAW, g_seatYaw[i]);
                ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in, EIA_PITCH, g_seatPitch[i]);
            } else if (s) {
                float cur = *(float *)(s + OFF_SOLDIER_PITCH), err = want - cur;
                float dead = rb->task == TASK_FIGHT ? 0.0001f : 0.02f;
                float gain = rb->task == TASK_FIGHT ? 9.0f : 3.0f;
                // Caspian V5: measure how far one frame of full pitch input turns the look (rad), then command exactly
                // the step still needed (with a little damping) instead of a fixed gain that lagged behind the
                // recoil compensation and left the shots strung out vertically (0.1-2.3 m at 50 m in V4).
                if (g_pitchPrevAt[i] && g_pitchPrevAt[i] + 1 == H->frame && fabsf(g_pitchPrevL[i]) > 0.15f) {
                    float r = (cur - g_pitchPrevV[i]) / (g_pitchPrevL[i] * PITCH_INPUT_SIGN);
                    if (isfinite(r) && r > 0.0005f && r < 0.5f &&   // (once settled, a frame 3x off the estimate - recoil kick, a zoom change - is ignored)
                        (g_pitchRateN < 30 || (r < g_pitchRate * 3.0f && r > g_pitchRate / 3.0f))) {
                        g_pitchRate = g_pitchRateN ? g_pitchRate + 0.03f * (r - g_pitchRate) : r;
                        if (g_pitchRateN < 1000) g_pitchRateN++;
                    }
                }
                float lvl = fabsf(err) < dead ? 0.0f :
                    rb->task == TASK_FIGHT && g_pitchRateN >= 30 ? fmaxf(-1.0f, fminf(1.0f, err * PITCH_INPUT_SIGN * 0.6f / g_pitchRate)) :
                    fmaxf(-1.0f, fminf(1.0f, err * PITCH_INPUT_SIGN * gain));
                ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in, EIA_PITCH, lvl);
                g_pitchPrevV[i] = cur; g_pitchPrevL[i] = lvl; g_pitchPrevAt[i] = H->frame;
            }
        }
        air_apply(i, in); // last control writer BEFORE native vehicle EntryUpdate
        if (g_vehInput) {   // in a seat: nobody feeds a bot's input to the vehicle (humans' arrives from their client) - do it
            char *pl = H->bots[i], *veh = player_vehicle(pl);
            int seat = veh ? vehicle_seat_of(veh, pl) : -1;
            if (!veh) g_driving[i] = 0;
            if ((seat > 0 || (seat == 0 && (g_driving[i] || stationary_weapon(veh)))) && !IsBadReadPtr(veh, 4) && *(unsigned *)veh == ADDR_VEHICLE_VTABLE) {
                typedef void (__attribute__((thiscall)) *EntryUpdate_t)(void *ent, unsigned entry, void *input);
                ((EntryUpdate_t)(*(void ***)veh)[0x144 / 4])(veh, (unsigned)seat, in);
                if (seat == 0 && g_driving[i]) vring_record(i, veh, in);   // AltBotMod V31: the driver trace (read only)
                static unsigned logged;
                if (logged++ < 5) bot_log("vinput: fed %s's input to seat %d of %p", pl + OFF_PLAYER_ONLINE_NAME, seat, veh);
            }
        }
        if (g_vaimOn[i] && !air_pilot(i)) {
            *(int *)((char *)in + OFF_INPUT_AIM_VALID) = 1;
            *(float *)((char *)in + OFF_INPUT_AIM_YAW) = g_vaimYaw[i];
            *(float *)((char *)in + OFF_INPUT_AIM_PITCH) = g_vaimPitch[i];
        }
        if (*aim_mode() && !air_pilot(i)) {
            *(int *)((char *)in + OFF_INPUT_AIM_VALID) = 1;
            *(float *)((char *)in + OFF_INPUT_AIM_YAW) = bot_aim(i)[0];
            *(float *)((char *)in + OFF_INPUT_AIM_PITCH) = bot_aim(i)[1];
        }
        mode_apply(i,in);
    }
    { double t = perf_now(); perf_add(PF_INPUT, t - tMark); tMark = t; }
    level_tick();
    { double t = perf_now(); perf_add(PF_LEVEL, t - tMark); tMark = t; }
    if (!level_ready()) { perf_add(PF_TOTAL, perf_now() - tStart); return; }   // level loading / changing: leave everything alone
    hq_learn_tick();
    if (g_levelReady && H->frame % 30 == 7 && *nav_slot() && (*nav_slot())->phase == 2) {
        int back = nav_unblock_expired(*nav_slot(), 0);
        if (back) bot_log("nav: %d link(s) blocked at stuck spots 3 minutes ago are open again", back);
    }
    if (g_levelReady) { tickets_tick(); swim_tick(); tree_tick(); if (!g_driveOff) { drive_mask(); drive_pave_tick(); drive_ford_tick(); drive_strikes_tick(); squad_vehicle_tick(); } }
    if (g_readdCount) readd_bots();
    // After a server start, bring up the 8-bot roster automatically (unless BotMod_autoroster.off exists).
    static int autoRosterDone;
    if (!autoRosterDone && H->botCount == 0) {
        autoRosterDone = 1;
        char dir[MAX_PATH], path[MAX_PATH + 32];
        GetModuleFileNameA(NULL, dir, sizeof dir);
        *strrchr(dir, '\\') = 0;
        snprintf(path, sizeof path, "%s\\BotMod_autoroster.off", dir);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
            char c1[] = "step 1";
            botmod_command(c1);
            g_rosterAuto = 1;
            bot_log("autoroster: %d bots wanted", *wanted_bots());
            chat_say("[AltBotMod] %d bots joining (US %d, RU %d) - type !help for bot commands", *wanted_bots(), (*wanted_bots() + 1) / 2,
                     *wanted_bots() / 2);
        }
    }
    // Actual-motion recovery now covers zero-input bots, tasks and vehicles below.
    if (g_testVictim && H->frame % 15 == 0) {
        char *s = player_exists(g_testVictim) ? *(char **)(g_testVictim + OFF_PLAYER_SOLDIER) : NULL;
        static unsigned until;
        if (!until) until = H->frame + 20 * 30;
        bot_log("test: %s health %s", g_testVictim + OFF_PLAYER_ONLINE_NAME, s ? "alive" : "dead");
        if (s) bot_log("test:   %.1f", *(float *)(s + 0x128));
        if (!s || H->frame > until) { g_testVictim = NULL; until = 0; }
    }
    g_where = "roster/squads/downed/nav";
    roster_tick();
    { double t = perf_now(); perf_add(PF_ROSTER, t - tMark); tMark = t; }
    squad_tick();
    squad_announce_tick();   // AltBotMod V34
    { double t = perf_now(); perf_add(PF_SQUAD, t - tMark); tMark = t; }
    downed_tick();
    downed_follow_bodies();
    { double t = perf_now(); perf_add(PF_DOWNED, t - tMark); tMark = t; }
    if (H->stepBots) step_bot_soldiers();   // (the engine's simulation of the bot soldiers: no client steps them)
    { double t = perf_now(); perf_add(PF_STEP, t - tMark); tMark = t; }
    nav_tick();
    { double t = perf_now(); perf_add(PF_NAV, t - tMark); tMark = t; }
    // New walking claims follow seated drivers, never ahead of them.
    for(int turn=0;turn<H->botCount;turn++)fleet_tick((H->frame+turn)%H->botCount);
    for(int turn=0;turn<H->botCount;turn++)deployment_idle_tick((H->frame+turn)%H->botCount);
    double brainMs = 0, detectMs = 0;
    for (int turn = 0; turn < H->botCount; turn++) {
        int i=(H->frame+turn)%H->botCount;
        g_whereBot = i;
        g_where = "brain_tick";
        double a = perf_now();
        if (((H->frame + i) & 1) == 0 && !g_drive[i].phase) {   // each bot thinks every other frame (15 Hz), alternating halves
            brain_tick(i);
            if (level_ready()) drive_consider(i);
            g_where = "say_tick";
            say_tick(i);
        }
        double b = perf_now();
        if (b - a > 20.0) {
            static unsigned slowLogged;
            if (slowLogged++ < 60) bot_log("perf: slow think %.0f ms - %s task %s enroute %d waiting %d nav %d", b - a, (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME,
                                           kTaskNames[bot_brain(i)->task], bot_brain(i)->enroute, bot_brain(i)->waiting, bot_nav(i)->mode);
        }
        g_where = "detect_tick";
        if (!g_air[i].phase) {
            if ((H->frame + i * 2) % 15 == 0) detect_tick(i);
            else if ((H->frame + i) % 6 == 0) close_threat_tick(i);
        }
        activity_tick(i);
        brainMs += b - a; detectMs += perf_now() - b;
    }
    perf_add(PF_BRAIN, brainMs); perf_add(PF_DETECT, detectMs);
    perf_add(PF_TOTAL, perf_now() - tStart);
    g_where = NULL;
    g_whereBot = -1;
}


// AltBotMod V16.2.1: exception report. A vectored handler logs the first few fatal-looking exceptions (access violation,
// illegal instruction, divide by zero, breakpoint / assert) with the module and offset, what our tick was doing (g_where)
// and the return addresses on the stack that point into the game or this DLL, so a server that dies leaves a trace in
// BotMod_<port>.log. It changes nothing: the exception carries on to the game's own handling. Exceptions inside the system
// DLLs (IsBadReadPtr's own probes) are ignored.
static PVOID g_crashVeh; static volatile LONG g_crashLogged;
static int crash_module(void *addr, char *out, int n, unsigned *off)
{
    HMODULE m = NULL; char path[MAX_PATH] = "";
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &m) || !m) {
        snprintf(out, n, "?"); *off = (unsigned)(uintptr_t)addr; return 0;
    }
    GetModuleFileNameA(m, path, sizeof path);
    const char *b = strrchr(path, '\\'); b = b ? b + 1 : path;
    snprintf(out, n, "%s", b); *off = (unsigned)((uintptr_t)addr - (uintptr_t)m);
    return 1;
}
static LONG CALLBACK crash_veh(PEXCEPTION_POINTERS ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_BREAKPOINT && code != EXCEPTION_PRIV_INSTRUCTION)   // (not a stack overflow: no stack left to log with)
        return EXCEPTION_CONTINUE_SEARCH;
    void *at = ep->ExceptionRecord->ExceptionAddress; char mod[64]; unsigned off;
    crash_module(at, mod, sizeof mod, &off);
    if (!_stricmp(mod, "kernel32.dll") || !_stricmp(mod, "KernelBase.dll") || !_stricmp(mod, "ntdll.dll")) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&g_crashLogged) > 6) return EXCEPTION_CONTINUE_SEARCH;
    const char *w = (const char *)g_where; int wb = g_whereBot;
    char trail[400] = ""; int tl = 0, found = 0;
    {
        unsigned *sp = (unsigned *)(uintptr_t)ep->ContextRecord->Esp;
        for (int k = 0; k < 2048 && found < 12; k++) {
            if (IsBadReadPtr(sp + k, 4)) break;
            unsigned v = sp[k]; char m2[64]; unsigned o2;
            if (v < 0x00401000 || v > 0x7fff0000) continue;
            if (!crash_module((void *)(uintptr_t)v, m2, sizeof m2, &o2)) continue;
            int ours = !_stricmp(m2, "BotModCore.dll") || !strncmp(m2, "BotModCore", 10);
            if (!ours && _stricmp(m2, "bf3_server_Final.exe")) continue;
            tl += snprintf(trail + tl, sizeof trail - tl, " %s%08x", ours ? "core+" : "", ours ? o2 : v);
            found++;
            if (tl > (int)sizeof trail - 24) break;
        }
    }
    bot_log("exception: %08lx at %s+%08x (%p)%s%p, thread %lu, tick: %s (bot %d %s); stack:%s",
            code, mod, off, at, code == EXCEPTION_ACCESS_VIOLATION ? (ep->ExceptionRecord->ExceptionInformation[0] ? ", writing " : ", reading ") : " ",
            code == EXCEPTION_ACCESS_VIOLATION ? (void *)ep->ExceptionRecord->ExceptionInformation[1] : NULL,
            GetCurrentThreadId(), w ? w : "(not in the bot tick)", wb,
            w && wb >= 0 && wb < H->botCount ? (char *)H->bots[wb] + OFF_PLAYER_ONLINE_NAME : "", tl ? trail : " (none found)");
    return EXCEPTION_CONTINUE_SEARCH;
}
__declspec(dllexport) int botmod_init(BotModHost *host)
{
    if (host->version != BOTMOD_HOST_VERSION) {
        host->log("core: host version %d != core version %d - restart the server", host->version, BOTMOD_HOST_VERSION);
        return 0;
    }
    H = host;
    enhanced_load_config();
    int ok = 1;
    ok &= detour(ADDR_CSE_SPAWN, kCseSpawnPrologue, 6, hook_cse_spawn, "CharacterSpawnEntity::spawn");
    ok &= detour(ADDR_GHOST_ADD, kGhostAddPrologue, 6, hook_ghost_add, "ServerGhostManager::addGhost");
    ok &= detour(ADDR_PLAYER_SELECT_UNLOCKS, kSelectUnlocksPrologue, 6, hook_select_unlocks, "ServerPlayer::onSelectedUnlockAssets");
    ok &= detour(ADDR_PLAYER_SELECT_WEAPON, kSelectWeaponPrologue, 5, hook_select_weapon, "ServerPlayer::onSelectedWeapon");
    ok &= detour(ADDR_MSGMGR_DISPATCH, kMsgDispatchPrologue, 10, hook_msg_dispatch, "MessageManager::dispatch");
    g_crateEvents = detour(ADDR_STAT_EVENT_SEND, kStatEventPrologue, 6, hook_stat_event, "ServerStatEventTriggerEntity::sendEvent");   // V16.2.2 (optional)
    ok &= detour(ADDR_SPAWN_REQ_CALL, kSpawnReqCall, 11, hook_spawn_request, "spawn request call");
    {   // Bots have no persistence stats, so the award system has no counters for them and warns on every lookup
        // ("trying to access AwardCounter for player N before added to the system"). The warning is only a log line
        // (both paths continue the same way): skip it in ServerAwardSystem::getAwardCounter (two copies).
        static const unsigned char je[2] = {0x74, 0x4F}, jmp[2] = {0xEB, 0x4F};
        ok &= patch_code(0x011ff662, je, jmp, 2, "AwardCounter warning (1)");
        ok &= patch_code(0x01201592, je, jmp, 2, "AwardCounter warning (2)");
    }
    ok &= detour(ADDR_SOLDIER_CTOR, kSoldierCtorPrologue, 6, hook_soldier_ctor, "ServerSoldierEntity ctor");
    ok &= patch_vslot(ADDR_PLAYER_VTABLE + 0x18, ADDR_PLAYER_HAS_UNLOCK, hook_has_unlock, "ServerPlayer::hasUnlock");
    ok &= patch_vslot(ADDR_SOLDIER_VTABLE + 0x74, ADDR_SOLDIER_PREPHYS, hook_soldier_prephys, "ServerSoldierEntity::prePhysicsUpdate");
    ok &= patch_vslot(ADDR_SOLDIER_VTABLE + 0x110, ADDR_SOLDIER_ON_DAMAGE, (void *)hook_soldier_on_damage, "ServerSoldierEntity::onDamage");
    patch_vslot(ADDR_VEHICLE_VTABLE + 0x110, ADDR_VEHICLE_ON_DAMAGE, (void *)hook_vehicle_on_damage, "ServerVehicleEntity::onDamage");   // AltBotMod V16.2 (optional)
    ok &= detour(ADDR_PROJECTILE_PROCESS_HIT, kProcessHitPrologue, 6, hook_process_hit, "projectile processHit");
    {
        static const unsigned char hc[6] = {0x0F, 0x85, 0x2D, 0x02, 0x00, 0x00};   // jne 0x11803f8
        ok &= patch_jump(0x011801c5, hc, 6, (void *)hc_stub, "bot damage acceptance (health component)");
    }
    ok &= patch_vslot(ADDR_SOLDIER_VTABLE + 0x144, ADDR_SOLDIER_PREPHYS_ENTRY, hook_soldier_apply, "ServerSoldierEntity::prePhysicsUpdateEntry");
    g_lockHooked = detour(ADDR_LOCK_CTRL_UPDATE, kLockUpdatePrologue, 6, hook_lock_update, "ServerLockingController::update");   // AltBotMod V25 (optional)
    if (!ok) { unpatch_all(); return 0; }
    // A grid carried over from the previous core may use another layout (NAV_LAYERS): reload it from disk.
    nav_free(*nav_slot()); *nav_slot() = NULL; *nav_building() = 0;
    g_jobDoLoadouts = !loadout_lib()->count;   // the discovery worker loads/verifies the library (no main-thread stall)
    { const char *l = current_level(); if (l) strncpy(g_level, l, sizeof g_level - 1); }
    for (int i = 0; i < MAX_BOTS; i++) g_botLoadout[i] = i < H->botCount ? pick_loadout(H->bots[i]) : -1;   // what they spawned with
    for (int i = 0; i < H->botCount; i++) {   // planned paths do not survive a reload: re-plan everything
        BotBrain *b = bot_brain(i);
        b->enroute = b->waiting = 0;
        b->targetFlag = -1;
        b->replanAt = 0;
        if (b->crouched) press_button(i, 14, POSE_PRESS_FRAMES);   // prone is off now: stand up any bot we put down
        b->crouched = 0;
        if (bot_nav(i)->mode == 3) bot_nav(i)->mode = 1;
    }
    if (!g_crashVeh) g_crashVeh = AddVectoredExceptionHandler(1, crash_veh);   // AltBotMod V16.2.1
    g_breakLoaded = 0; g_breakN = g_unbreakN = 0; breakable_load();   // AltBotMod V18
    g_compThread = CreateThread(NULL, 0, comp_worker, NULL, 0, NULL);
    missile_start();   // AltBotMod V23
    chat_start(H->log);
    load_bot_count();
    if (H->botCount) g_rosterAuto = 1;   // reload with bots already in: keep managing them
    bot_log("core: ready (%d bot(s) carried over, %d loadout(s))", H->botCount, loadout_lib()->count);
    enhanced_status();
    return 1;
}

__declspec(dllexport) void botmod_shutdown(void)
{
    nav_free(g_navExpansion);g_navExpansion=NULL;
    nav_save_if_dirty();   // keep what the bots learned about the map across reloads
    // stop the workers first (scans abort within one memory region), then wait briefly - never long enough to stall
    InterlockedExchange(&g_abortScans, 1);
    g_compStop = 1;
    if (g_jobThread) {
        if (WaitForSingleObject(g_jobThread, 3000) == WAIT_TIMEOUT) bot_log("WARNING: discovery worker still running at unload");
        CloseHandle(g_jobThread); g_jobThread = NULL;
    }
    chat_stop();
    missile_stop();   // AltBotMod V23
    if (g_compThread) {
        if (WaitForSingleObject(g_compThread, 3000) == WAIT_TIMEOUT) bot_log("WARNING: scan worker still running at unload");
        CloseHandle(g_compThread); g_compThread = NULL;
    }
    if (g_crashVeh) { RemoveVectoredExceptionHandler(g_crashVeh); g_crashVeh = NULL; }
    unpatch_all();
    bot_log("core: shut down, all hooks removed");
}
