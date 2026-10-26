// Native C translation/adaptation of the supplied FunBots 3.0.0 flight logic.
// See AIRCRAFT-PORT.md for provenance, changed safeguards and runtime limits.
#pragma once

enum BMAirKind { BM_AIR_NONE, BM_AIR_JET, BM_AIR_ATTACK, BM_AIR_SCOUT, BM_AIR_TRANSPORT };
enum BMAirPhase { BM_FLIGHT_TAKEOFF, BM_FLIGHT_CRUISE, BM_FLIGHT_ATTACK, BM_FLIGHT_RECOVER, BM_FLIGHT_APPROACH,
                  BM_FLIGHT_EXTEND };   // AltBotMod V20: a jet flies away after a pass, then comes round again
// AltBotMod V20: what an aircraft is doing in a fight (logged when it changes).
enum BMAirTactic { BM_TACTIC_NONE, BM_TACTIC_PURSUE, BM_TACTIC_GUNS, BM_TACTIC_BREAK, BM_TACTIC_EXTEND,
                   BM_TACTIC_SETUP, BM_TACTIC_DIVE, BM_TACTIC_PULLOUT, BM_TACTIC_EVADE, BM_TACTIC_STRAFE,
                   BM_TACTIC_RETURN,   // AltBotMod V29: turns back into the combat area
                   BM_TACTIC_STALL,    // AltBotMod V34: nose down to get its speed back
                   BM_TACTIC_COUNT };
#define BM_AIR_TRAFFIC 16
typedef struct BMAirProfile {
    const char *name;
    enum BMAirKind kind;
    float speed, gravity, muzzle[3];
    int firstPassenger; // zero-based seat; preceding seats have vehicle guns
} BMAirProfile;
typedef struct BMAirConfig {
    int jets, helicopters, weapons, countermeasures, transportDrops, pilotsPerTeam, debug;
    int landings, deploymentMenu;
    float findRadius, jetSpeed, jetHeight, jetCeiling, chopperHeight;
    float jetOrbit, chopperOrbit, waitSeconds, attackSeconds;
    float yawSign, pitchSign, rollSign;
    float jetAreaMargin;   // AltBotMod V29: the jets' area - flags, bases and airfields plus this much (m); 0 = no area
} BMAirConfig;
typedef struct BMAirPid { float integral, last; } BMAirPid;
typedef struct BMAirController {
    enum BMAirKind kind;
    enum BMAirPhase phase;
    float timer, attackTime, shotTime;
    int lastTargetAir;
    BMAirPid yaw, throttle, tilt, roll;
    // AltBotMod V20 combat state (jets: dogfights and strike runs; helicopters: missiles)
    BMAirPid aimPitch, aimYaw;
    float extendTime, diveTime, lockTime, secondaryWait, burstTime, ip[3];
    int ipValid, ipStage, pullup, passes, tactic, secondaryThisPass;
    int returning;   // AltBotMod V29: turning back into the combat area (until well inside)
    int stall;       // AltBotMod V34: too slow to fly - nose down, wings level, full power until it flies again
} BMAirController;
typedef struct BMAirMotion {
    float anchor[3], velocity[3], elapsed;
    int initialized, ready;
} BMAirMotion;
typedef struct BMAirObservation {
    float position[3], right[3], up[3], forward[3], velocity[3];
    float goal[3], target[3], targetVelocity[3];
    float groundY, aheadGroundY, obstacleDistance, orbitDirection;
    int velocityValid, groundValid, aheadGroundValid;
    int targetValid, targetAir, targetVisible;
    int pickup; // low-speed transport landing approach uses reduced terrain clearance
    int landing; // validated transport pad; descent clearance and braking differ from cruise
    // AltBotMod V20: other aircraft nearby (both teams, the target included): position xyz, velocity xyz.
    int trafficCount;
    float traffic[BM_AIR_TRAFFIC][6];
    float extraClearance;   // AltBotMod V21: learned - aircraft were lost near the ground around here (metres more)
    // AltBotMod V29: the area a jet stays in (x / z box: the flags, both bases and the airfields with a margin) - a jet
    // that leaves it, or would within 6 s, turns back to its middle (BF3 kills a pilot who stays outside the combat area)
    int areaValid; float areaMin[2], areaMax[2];
} BMAirObservation;
typedef struct BMAirOutput {
    float throttle, brake, yaw, pitch, roll;
    float yawError, pitchError, aim[3];
    int fire, attacking, recovering;
    int secondary;   // AltBotMod V20: fire the secondary weapon once (rocket pods / missiles)
    int armSecondary;   // AltBotMod V22: hold the missile selected (no gun fire) while its seeker locks on
    int tactic;      // enum BMAirTactic
} BMAirOutput;

void bm_air_defaults(BMAirConfig *config);
const BMAirProfile *bm_air_profile(const char *engineName);
int bm_air_allowed(const BMAirConfig *config, const BMAirProfile *profile);
int bm_air_transform_valid(const float *matrix);
void bm_air_start(BMAirController *state, enum BMAirKind kind, int alreadyFlying);
float bm_air_pid(BMAirPid *state, float error, float kp, float ki, float kd, float dt);
void bm_air_motion(BMAirMotion *state, const float position[3], float dt);
int bm_air_intercept(const float origin[3], const float target[3], const float velocity[3],
                     float speed, float gravity, float aim[3], float *flightTime);
int bm_air_deviation(const BMAirObservation *obs, const float target[3], float *yaw, float *pitch);
// Returns zero and neutral output for invalid observations. All levels bounded.
const char *bm_air_tactic_name(int tactic);
int bm_air_step(BMAirController *state, const BMAirProfile *profile, const BMAirConfig *config,
                const BMAirObservation *obs, float dt, BMAirOutput *output);
