// Portable decisions; native observations must be validated by the adapter.
#pragma once
enum BMWeaponFamily { BM_W_RIFLE, BM_W_BURST, BM_W_CARBINE, BM_W_LMG,
                      BM_W_DMR, BM_W_BOLT, BM_W_PDW, BM_W_SHOTGUN, BM_W_PISTOL };
typedef struct BMWeaponProfile {
    const char *label;
    enum BMWeaponFamily family;
    int burst, pause;
    float maxRange, rangeFactor, aimFactor, adsMeters;
} BMWeaponProfile;
const BMWeaponProfile *bm_weapon_profile(const char *asset, int recon, int sidearm);
float bm_weapon_range(const BMConfig *config, const BMWeaponProfile *weapon);
int bm_weapon_burst(const BMWeaponProfile *weapon, float distance);
int bm_weapon_pause(const BMWeaponProfile *weapon, float distance);
float bm_weapon_tolerance(const BMConfig *config, const BMWeaponProfile *weapon, float distance);
int bm_weapon_stable(float yawError, float pitchError, float tolerance);

typedef struct BMTrafficContact {
    float ahead, lateral, height, alignment, otherSpeed, ownSpeed, halfWidth, otherWidth;
    int ownId, otherId, human, crossing;
} BMTrafficContact;
typedef struct BMTrafficDecision { float limit; int yield, following; } BMTrafficDecision;
BMTrafficDecision bm_traffic_contact(const BMTrafficContact *contact, float spacing, float limit);

enum BMLandingPhase { BM_LAND_NONE, BM_LAND_APPROACH, BM_LAND_DESCEND,
                      BM_LAND_UNLOAD, BM_LAND_ASCEND };
typedef struct BMLandingState { enum BMLandingPhase phase; float elapsed, settled; } BMLandingState;
typedef struct BMLandingObservation {
    float distance, clearance, horizontalSpeed, verticalSpeed;
    int padValid, threatened, cargo, pickup;
} BMLandingObservation;
typedef struct BMLandingDecision { float height; int unload, board, abort, finished; } BMLandingDecision;
BMLandingDecision bm_transport_landing(BMLandingState *state, const BMLandingObservation *obs, float dt);

// FunBots-inspired missile timing. The native lock/weapon-slot observation bridge
// is deliberately unbound until this executable's data layout is verified.
typedef struct BMMissileState { float lockSeconds, cooldown, counterCooldown; void *target; } BMMissileState;
typedef struct BMMissileObservation {
    void *target;
    float distance, yawError, pitchError, incomingSeconds;
    int available, visible, targetAir, hostile, nativeLock, incomingConfirmed;
} BMMissileObservation;
typedef struct BMMissileDecision { int selectSecondary, fire, countermeasure; } BMMissileDecision;
BMMissileDecision bm_air_missile(BMMissileState *state, const BMMissileObservation *obs, float dt);
