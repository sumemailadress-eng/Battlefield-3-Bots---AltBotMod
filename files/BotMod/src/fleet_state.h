// Native vehicle reservations. Every pointer is revalidated before use.
#pragma once
enum { FLEET_JET=1, FLEET_HELI, FLEET_GROUND };
typedef struct FleetReservation { char *vehicle,*failedVehicle; int role; unsigned until,retryAt,failedUntil,queuedAt,spawnArgs[3]; } FleetReservation;
static FleetReservation g_fleet[MAX_BOTS];
static void *g_fleetBase[3];
static float g_fleetBasePos[3][3];
static unsigned g_fleetBaseArgs[3][3];
static unsigned g_takeoverSince[MAX_BOTS];
static unsigned g_fleetPriorityUntil;
// Nearby teammates share failed walks; other teams and distant bots may try.
typedef struct FleetRouteFailure {char *vehicle;int team;float origin[3];unsigned until;}FleetRouteFailure;
static FleetRouteFailure g_fleetRouteFailure[64];
typedef struct DeploymentIdleState {char *soldier;float position[3];unsigned progressAt;}DeploymentIdleState;
static DeploymentIdleState g_deploymentIdle[MAX_BOTS];
static void deployment_idle_tick(int i);
typedef struct VehicleApproachState {
    char *soldier;float target[3];unsigned candidate,lastAt;int deferred,quick,spot;
} VehicleApproachState;
static VehicleApproachState g_vehicleApproach[MAX_BOTS];
static int vehicle_plan_deferred(int i);
static int vehicle_board_close(char *soldier,const float *position);
static int vehicle_finish_approach(int i,char *soldier,const float *position,float reach);
static int fleet_claimed(char *vehicle, int except);
static int fleet_air_slot(int team, const BMAirProfile *profile);
static int fleet_pilot_allowed(int i);
static int fleet_driver_flag(int i,char *vehicle);
static void *fleet_spawn(char *bot);
static void fleet_tick(int i);
static void fleet_remember_bases(void);
static void fleet_status(void);
static const char *fleet_reason(char *vehicle, int team);
static unsigned fleet_walk_frames(float distance);
static void fleet_takeover(int i);
