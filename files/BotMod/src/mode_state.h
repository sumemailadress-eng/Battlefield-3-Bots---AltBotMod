#pragma once
static enum BMMode g_mode=BM_MODE_UNKNOWN;
static char g_modeName[64]="";
static int mode_teams(void){return bm_mode_teams(g_mode);}
static int mode_is_cq(void){return g_mode==BM_MODE_CQ;}
typedef struct BMModeProfile {
 const char *level,*mode; float low[3],high[3],patrol[32][3]; int stages;
 unsigned baseMask[9];float bases[9][3][3]; struct {float pos[3],yaw,pitch;int present;}mcom[17];
} BMModeProfile;
#include "mode_profiles.inc"
static const BMModeProfile *g_modeProfile;
static char *g_modeSpawns[256];static int g_modeSpawnCount;
static struct {void *entity;unsigned a2,a3,a4;int stage;} g_modeTeamSpawn[5];
#include "rush_policy.h"
static BMRushState g_rush;
static struct {int target,heldInteract,goalReady,candidate,routePending,goalRole,snipe,raid;unsigned plannedAt,interactAt,retryAt,revision,guardUntil,operatorAt,raidAt,closerUntil,approachAt;float goal[3];char *soldier;} g_modeBot[MAX_BOTS];
static float g_modeTeamBase[3][3];
static int g_modeSpawnsLoose;   // AltBotMod V15: no spawn entity in the play area for a team - bots try the level's others (mode_spawn)   // AltBotMod V15: centre of each team's own spawn points (Capture the Flag raids)
static unsigned g_modeFailedSpawn[256];
static unsigned char g_modeSpawnOk[256];
static struct {void *entity,*soldier;int slot,team,stage;unsigned a2,a3,a4,at;} g_rushBlind[MAX_BOTS];   // AltBotMod V16: blind Rush spawns checked 0.5 s later
static float g_modeSpawnLand[256][3];static unsigned char g_modeSpawnLandKnown[256];   // AltBotMod V16.1: where a spawn entity put a bot (its deployment zone)
static unsigned g_mcomArmAt[17],g_mcomNearSince[17][3],g_rushMsgCount[4];   // AltBotMod V16.1: M-COM watch
static int g_crateEvents;   // AltBotMod V16.2.2: the game's own crate armed / disarmed / destroyed events are hooked (ServerStatEventTriggerEntity::sendEvent)
static short g_modeBotSlot[MAX_BOTS];   // AltBotMod V16: spawn entity slot + 1 of each bot's last mode spawn
static unsigned char g_modeEarlyDeaths[256][2];   // AltBotMod V16: deaths with no shooter soon after spawning there, per team
static unsigned char g_modeSpawnNo[256];  // AltBotMod V15: bit t-1 = the engine refused team t here (loose spawning)   // AltBotMod V9: bit t-1 = team t has spawned here (team-less TDM spawns)
static unsigned g_modeGoalProbeFrame;
static int g_modeGoalProbeNext;
static struct {void *entity;char *player;unsigned a2,a3,a4,at;} g_modePending[64];
static int g_rushStage=1;
static unsigned g_rushAdvanceAt;
static unsigned char g_mcomDestroyedBy[17];   // V17: 1 the game's crate event, 2 a message / guess
static BMRushState g_rushBefore; static int g_rushBeforeStage; static unsigned g_rushBadDeaths;   // AltBotMod V17: the state before the last sector change (undone if it was wrong)
static volatile LONG g_roundCleanup,g_roundEpoch,g_roundForceReset,g_compEpoch=-1;
// AltBotMod V15: the server's mode, as the bots play it this round. A Squad Rush with no recorded M-COMs for the level, or
// a flag mode (Capture the Flag, Air Superiority ...) whose level shows no capture points, falls back to team play - the
// TDM hunt - for the rest of the round, instead of the bots waiting "not ready yet" all round.
static LONG g_modeFallbackEpoch=-1;
static enum BMMode mode_kind(const char *mode)
{
    enum BMMode m=bm_mode(mode);
    return m!=BM_MODE_UNKNOWN && g_modeFallbackEpoch==g_roundEpoch?BM_MODE_TDM:m;
}
static int mode_is_squad_rush(void){return g_mode==BM_MODE_RUSH && bm_mode_squad_rush(g_modeName);}
static int mode_is_ctf(void){return g_mode==BM_MODE_TDM && !_stricmp(g_modeName,"CaptureTheFlag0");}
static unsigned g_createdAt[MAX_BOTS];
static unsigned char g_silentRemove[MAX_BOTS];
static const BMModeProfile *mode_profile(const char *level,const char *mode);
static int mode_discover(const char *level,const char *mode,float *low,float *high);
static int mode_has_spawns(void);
static void mode_reset(void);
static int mode_spawn(char *bot);
static void mode_brain_tick(int i,char *soldier);
static void mode_apply(int i,void *input);
static void mode_observe_spawn(void *entity,char *player,unsigned a2,unsigned a3,unsigned a4);
static void mode_observation_tick(void);
static int deployment_spawn(char *bot);

static int level_ready(void);
static int mode_uses_vehicles(void){return mode_is_cq() || g_mode==BM_MODE_RUSH;}
static int objective_count(void);
static const float *objective_pos(int f);
static int objective_wanted(int team,int f);
static int mode_base_present(int stage,int team);
static int mode_vehicle_target(int i,const float *position);
static void mode_reassign_idle(int i,int objective);
static int rush_command(const char *who,const char *arg);
static void rush_trace_message(const char *name,const void *message);


static struct {void *target;char *player,*beforeSoldier,*beforeVehicle;unsigned vt,function,arg,at,epoch;int team;}g_deployPending[32];
static struct {void *target;char *vehicle;unsigned vt,function,arg,retryAt,epoch;int team,virtualSpawn,failures;const BMAirProfile *profile;}g_deployChoice[32];
static struct {unsigned at,epoch;int choice,team;}g_deployActive[MAX_BOTS];
static void deployment_observe(void *target,char *player,unsigned arg);
static void deployment_tick(void);
