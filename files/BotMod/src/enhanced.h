// Portable policy layer for the experimental native BotMod enhancement.
// Vehicle classes and primary-shell constants were cross-checked against
// the supplied FunBots 3.0.0 VehicleData.lua. This is not a VU API port.
#pragma once
#ifndef BOT_ROSTER   /* AltBotMod V28: core.c maps it to bot_roster(); a test harness reads the name */
#define BOT_ROSTER(pl) ((const char *)(pl) + OFF_PLAYER_ONLINE_NAME)
#endif
#include <stddef.h>
#include "air.h"

#define BM_ENHANCED_VERSION "AltBotMod V34 - Squad Up"
enum BMVehicleKind { BM_UNKNOWN, BM_LIGHT, BM_BOAT, BM_TANK, BM_IFV, BM_AIR, BM_AA, BM_AMTRAC };
typedef struct BMVehicleProfile {
    const char *label;
    enum BMVehicleKind kind;
    float muzzleSpeed, gravity, halfWidth, frontLead;
} BMVehicleProfile;
typedef struct BMConfig {
    int driving, light, boats, armored, driverWeapons, smoothSteering, ballisticAim;
    int returnToBase, groundDriversPerTeam, passengerTakeover;
    float returnMeters;
    float minTrip, findRadius, squadRadius, stopShort, humanRadius;
    float lightMaxSpeed, boatMaxSpeed, tankMaxSpeed, ifvMaxSpeed;
    float stuckSeconds, bailHealth, steerKp, steerKd;
    float infantryRange, reconRange, reactionSeconds, infantryAimMargin;
    float armorRange, armorInfantryRange;
    int weaponVariety, weaponAttachments;
    int gadgetVariety, c4Attacks, lockOnLaunchers;   // AltBotMod V16
    float botTicketCost;   // AltBotMod V16: tickets a bot respawn costs (Conquest / Rush attackers)
    int vehicleCountermeasures;   // AltBotMod V16: smoke / flares when hit or locked
    int weaponHandling, armorTactics, armorEscort, traffic, diagnostics, driveStages;
    int aggression;      // Caspian V2: 0 = Test15 combat, 1 = aggressive (default), 2 = brutal
    int funBotsRoutes;   // Caspian V2: follow Fun-Bots recorded routes (BotMod_traces)
    float vehicleDamageTaken, vehicleDamageDealt;   // AltBotMod V16.2: damage to vehicles x / damage from anyone in a vehicle x (1 = game damage)
    float botDamage;     // Caspian V5: damage multiplier for bullets/shells fired by bots (1 = game damage)
    float armorRepairHealth, armorTacticMeters, trafficSpacing;
    BMAirConfig air;
    int difficulty;        // AltBotMod V9: 0 = V8 combat (Aggression), 1-5 = easy, normal, hard, veteran, elite
    int randomDifficulty;  // AltBotMod V9: each bot plays one level either side of Difficulty
    float sniperRange;     // AltBotMod V10: bolt-action rifles see, engage and fight soldiers this far out
    int sniperOverwatch;   // AltBotMod V10: snipers climb to high ground over the objective
    int sniperPairs;       // AltBotMod V11: two snipers may share a perch (a pair)
    float supplyMeters;    // AltBotMod V11: a medic / support answers a bot's call this far away (a player's: 3x)
    int classSupport;      // AltBotMod V11: medics and support drop medkits / ammo boxes for themselves and those near
    int botNames;                 // AltBotMod V28: gamertags for roster bots (BotMod_names.txt or made up)
    int lockAssist;               // AltBotMod V29: put a bot's lock in place after the weapon's lock time on target
    float lockOnSeconds;          // AltBotMod V30: the time on target before a bot's lock is put in place (s; the weapon's own lock time if shorter)
    float doorGunRange;           // AltBotMod V30: helicopter door guns / bench seats reach (m)
    int funBotsMesh;              // AltBotMod V34: walk Fun-Bots' objective mesh (BotMod_traces\navzones) near objectives
    float vehicleWalkMeters;      // AltBotMod V34: after the round's opening, infantry walk at most this far to a vehicle
    int liveMap;                  // AltBotMod V13: write BotMod\LiveMap (positions, kills, stuck spots) for Server Control
    int learning, learnFromPlayers;   // AltBotMod V21: learn from stuck spots / losses / deaths (BotMod_learned); record players' routes
    float liveMapSeconds;         // AltBotMod V13: how often the live picture is replaced
    float liveMapTrailSeconds;    // AltBotMod V13: how often each soldier's position goes into the events (traffic heat)
    int liveMapKeepMB;            // AltBotMod V13: events file size before it is rolled over to events_<port>.old.txt
    int routeActions;             // AltBotMod V15: Fun-Bots recorded actions - boarding spots, exit points, waits, crouch sections
    int reconBeacons;             // AltBotMod V15: recon bots set radio beacons at the recorded beacon spots
    int sideSteps;                // AltBotMod V14: 0 none, 1 short steps under fire / close in (default), 2 constant (V9-V13)
} BMConfig;
// AltBotMod V9: combat profiles per difficulty level, taken from the BotMod public preview build our dev made
// (2026-10-10; its BotModCore.dll carries this table - the values are read from it, the code that uses them is ours).
typedef struct BMDifficulty {
    float reaction; int burst, pause; float aimTol, aimMargin, aimLag, turnGain;
    float fightRange, engageRange, detectRange, fov, cooldown, breather, coverBelow, advanceStop, coverHold;
    float sprint, fightSprint, strafe, jumpRun, jumpFight, farCrouch, farCrouchChance; int settle;
    float ads, guard, guardSpread, aimWorsening, skillSpread; int fireModel; float rangeFalloff;
} BMDifficulty;
int bm_ranges_v25(BMConfig *c);   // AltBotMod V25: ranges at the V9-V24 defaults -> the V25 ones (1 when any moved)
const BMDifficulty *bm_difficulty_level(int level);         // 1..5 (clamped)
const char *bm_difficulty_name(int level);
int bm_difficulty_parse(const char *text);                  // easy..elite / 1-5 / off|0 -> 0..5, -1 invalid
int bm_bot_difficulty(const BMConfig *c, const char *botName);  // the level this bot plays (0 = off)
// Weapon burst / pause frames scaled by a profile (normal = 12 / 10 = the weapon's own rhythm).
int bm_profile_burst(const BMDifficulty *d, int weaponBurst, float distance);
int bm_profile_pause(const BMDifficulty *d, int weaponPause);
// Caspian V2: pick a combat value by the Aggression setting.
static inline float bm_aggr(const BMConfig *c, float normal, float aggressive, float brutal)
{ return !c ? normal : c->aggression >= 2 ? brutal : c->aggression == 1 ? aggressive : normal; }
// Frame counts (bursts longer, pauses shorter) scaled the same way; at least 1 frame.
static inline unsigned bm_aggr_frames(const BMConfig *c, int frames, float aggressive, float brutal)
{ float n = frames * bm_aggr(c, 1, aggressive, brutal) + 0.5f; return n < 1 ? 1u : (unsigned)n; }
// AltBotMod V13: how the live map hears about stuck bots - the watchdogs call bm_report_stuck; core.c's live map sets
// the hook (NULL elsewhere, e.g. in the offline tests, where reports go nowhere).
typedef void (*BMStuckHook)(int bot, const float *pos, const char *kind);
extern BMStuckHook bm_stuck_hook;
static inline void bm_report_stuck(int bot, const float *pos, const char *kind) { if (bm_stuck_hook) bm_stuck_hook(bot, pos, kind); }
// AltBotMod V21: learning (learn_native.inc sets these; NULL in the offline tests and with Learning=0). Events: where
// bots get stuck, jump, die, lose aircraft. Questions: is this spot known to be passable (1, 2 = with a jump), how many
// metres more should an aircraft keep here. Routes: the learned routes of a kind as Fun-Bots lines for the trace loader.
enum { BM_LEARN_STUCK_FOOT = 1, BM_LEARN_STUCK_GROUND, BM_LEARN_STUCK_BOAT, BM_LEARN_AIR_LOSS, BM_LEARN_DEATH, BM_LEARN_JUMP,
       BM_LEARN_PAD_LANDED, BM_LEARN_PAD_BLOCKED };   // (pads: a transport landed at / found blocked a helicopter landing spot)
enum { BM_ASK_FOOT_KEEP_OPEN = 1, BM_ASK_AIR_CLEARANCE_JET, BM_ASK_AIR_CLEARANCE_HELI };
typedef void (*BMLearnHook)(int bot, int event, const float *pos, const float *dir);
typedef int (*BMLearnAsk)(int bot, int question, const float *pos, const float *dir);
typedef const char *(*BMLearnRoutes)(const char *key, int kind, size_t *length);
typedef float (*BMLearnDanger)(float x, float z);
typedef int (*BMLearnPad)(const float *center, float radius, int nth, float *out);
extern BMLearnHook bm_learn_hook; extern BMLearnAsk bm_learn_ask; extern BMLearnRoutes bm_learn_routes; extern BMLearnDanger bm_learn_danger;
extern BMLearnPad bm_learn_pad;
// the nth learned helicopter landing spot within radius of center (closest first); 0 = none
static inline int bm_learn_pad_near(const float *center, float radius, int nth, float *out) { return bm_learn_pad ? bm_learn_pad(center, radius, nth, out) : 0; }
static inline void bm_learn(int bot, int event, const float *pos, const float *dir) { if (bm_learn_hook) bm_learn_hook(bot, event, pos, dir); }
static inline int bm_learn_query(int bot, int question, const float *pos, const float *dir) { return bm_learn_ask ? bm_learn_ask(bot, question, pos, dir) : 0; }
// AltBotMod V14: how a bot side-steps in a gunfight. A step lasts stepMin..stepMax frames at `level` strafe, then it
// stands still restMin..restMax frames (0: steps follow each other, the V9-V13 back-and-forth). needThreat: only while
// being shot at or with the enemy within closeMeters. Hops while fighting / running come every hopScale x the profile.
typedef struct BMDodgeStyle { float level, maxMeters, closeMeters, hopScale; int stepMin, stepMax, restMin, restMax, needThreat, hopNeedsThreat; } BMDodgeStyle;
int bm_dodge_style(const BMConfig *c, const BMDifficulty *profile, BMDodgeStyle *out);   // 0: no side steps at all
// One frame of the step / rest cycle: *dir is the step under way (+1 left, -1 right, 0 standing), *last the last
// step's side, *until when the step or rest ends. r: a random number. Returns 1 when a new step starts.
int bm_dodge_next(const BMDodgeStyle *st, signed char *dir, signed char *last, unsigned *until, unsigned now, unsigned r);
typedef struct BMSteering { float previousError, filteredDerivative; int ready; } BMSteering;

void bm_defaults(BMConfig *config);
// 1 accepted, 0 unknown key, -1 invalid value. Invalid input does not change config.
int bm_set(BMConfig *config, const char *key, const char *value);
const BMVehicleProfile *bm_vehicle_profile(const char *engineName);
int bm_vehicle_allowed(const BMConfig *config, const BMVehicleProfile *profile);
int bm_armored(const BMVehicleProfile *profile);
int bm_hold_vehicle(const char *engineName, int passengers);
float bm_speed_limit(const BMConfig *config, const BMVehicleProfile *profile);
float bm_steer(BMSteering *state, float error, float dt, const BMConfig *config);
int bm_stuck_due(unsigned now, unsigned checkAt, int reversing, int backingOut, float seconds);
void bm_ballistics(float speed, float gravity, float distance, float *flightTime, float *rise);
// Used to compact per-bot arrays when the native roster deletes a bot.
void bm_forget_slot(void *array, size_t itemSize, int index, int count);
enum BMTargetKind { BM_TARGET_INFANTRY, BM_TARGET_ENGINEER, BM_TARGET_GROUND, BM_TARGET_AIR };
float bm_target_score(int armored, enum BMTargetKind kind, float distance);
float bm_vehicle_target_score(const BMVehicleProfile *profile,enum BMTargetKind kind,float distance);
float bm_gun_range(const BMConfig *config, const BMVehicleProfile *profile, int infantry);
int bm_ground_ballistics(float speed, float gravity, float distance, float height, float *flight, float *lift);
int bm_infantry_burst(int recon, int sidearm, float distance);
int bm_infantry_pause(int recon, float distance);
float bm_infantry_tolerance(const BMConfig *config, int recon, float distance);
float bm_drive_horizon(float speed, int boat);
float bm_drive_obstacle_speed(float distance, float speed, int boat);
// AltBotMod V29: Fun-Bots driving. The throttle for a speed target: 1.0 below it (0.6 for a wheeled vehicle steering
// hard), 0.3 at it, a brake over it (negative), a stop for 0. And the speed a vehicle in the lane allows (along / lat:
// where it is from this one, half: both half-widths and the margin, speed: its own speed along this one's heading).
float bm_drive_throttle(float vwant, float speed, int armored, float headingError);
float bm_lane_limit(float along, float otherSpeedAlong, float frontLead);
// 1 when a vehicle at `op` is in the lane of one at `vp` heading `fwd` (x, z) along the route pts[next..n): within `half`
// of its line - straight ahead up to 15 m, then along the route itself up to `range` m. *along / *lat: how far ahead along
// the way, and how far right of it (negative: left).
int bm_lane_relation(const float *pts, int n, int next, const float *vp, const float fwd[2], const float *op, float half, float range,
                     float *along, float *lat);
// AltBotMod V22: the speed (m/s) for the bend at w, measured from a point ~10 m before it to one ~10 m after (v = sqrt(grip x radius))
float bm_bend_speed(const float before[3], const float w[3], const float after[3], int boat, int armored);
// AltBotMod V24: route following. Progress: the nearest route point within the next 30 m (never backwards), then on past
// one within `reach` or already behind the vehicle (forward = its heading, xyz; NULL: none). Aim: the point to steer at -
// 8 m on when slow, up to 25 m at speed, short of a bend of 35 deg+ (pure pursuit).
float bm_route_bend_limit(const float before[3], const float w[3], const float after[3], int boat, int armored);   // V24: 99 unless radius < 35 m
int bm_route_progress(const float *pts, int n, int next, const float pos[3], const float *forward, float reach);
void bm_route_aim(const float *pts, int n, int next, const float pos[3], float speed, float out[3]);
#include "tactics.h"
