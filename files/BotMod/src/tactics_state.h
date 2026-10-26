#pragma once
enum BMArmorJob { ARMOR_NORMAL, ARMOR_FLANK, ARMOR_COVER, ARMOR_RETREAT, ARMOR_REPAIR };
typedef struct ArmorNativeState {
    char *vehicle, *target;
    enum BMArmorJob job;
    unsigned nextAt, startedAt, repairAt, escortAt;
    float original[3], destination[3];
    int retries, engineer;
} ArmorNativeState;
static ArmorNativeState g_armor[MAX_BOTS];
typedef struct VehicleDiagnostic {
    unsigned at, loggedAt, changes, failures;
    char reason[80], vehicle[96];
    char *trackedVehicle;
} VehicleDiagnostic;
static VehicleDiagnostic g_vehicleDiag[MAX_BOTS];
static unsigned g_lockMessageCount, g_missileMessageCount;
static void diagnostic_note(int i,const char *reason,char *vehicle);
static void diagnostic_message(const char *name);
static void enhanced_diagnostics(void);
static int armor_tick(int i,const float *position);
static int armor_arrive(int i,const float *position);
static int armor_route_failed(int i);
static float armor_stop_short(int i);
static int infantry_trigger_ready(int i,char *soldier);
