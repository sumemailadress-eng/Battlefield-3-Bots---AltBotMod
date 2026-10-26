#pragma once
#include <string.h>

// AltBotMod V16.1: how long a bot holds the interact key at an M-COM to arm / defuse it
#define RUSH_HOLD_FRAMES (8 * 30)
// V16.2: within this of the recorded spot a bot starts / keeps arming or defusing (Fun-Bots: 4 m from the MCOM)
#define RUSH_ARM_REACH 2.6f
#define RUSH_ARM_KEEP 3.2f

// The engine owns M-COM completion and sector activation. Unknown means that
// no confirmed state has arrived, not that an interaction succeeded.
enum BMRushStatus { BM_RUSH_UNKNOWN, BM_RUSH_IDLE, BM_RUSH_ARMED, BM_RUSH_DESTROYED };
typedef struct BMRushObjective {
    enum BMRushStatus status;
    unsigned changedAt, leaseUntil;
    void *operatorPlayer;
} BMRushObjective;
typedef struct BMRushState {
    int stage, stages, width;
    unsigned revision, changedAt, confirmedEvents;
    BMRushObjective objective[17];
} BMRushState;

static inline void bm_rush_reset(BMRushState *r,int stages,int width,unsigned now)
{
    memset(r,0,sizeof *r);
    r->stage=1;r->stages=stages>0 && stages<=8?stages:1;
    r->width=width==1?1:2;r->changedAt=now;r->revision=1;
}
static inline int bm_rush_active(const BMRushState *r,int objective)
{
    return objective>(r->stage-1)*r->width && objective<=r->stage*r->width &&
        objective<=16 && r->objective[objective].status!=BM_RUSH_DESTROYED;
}
static inline int bm_rush_can_interact(const BMRushState *r,int objective,int team)
{
    if(!bm_rush_active(r,objective))return 0;
    enum BMRushStatus status=r->objective[objective].status;
    // In unknown state the native interaction itself decides whether a team
    // can arm/defuse. Never mark the station armed based on held input.
    // AltBotMod V16.1: defenders only go to a station known (or seen) to be armed. V9-V16 also sent them to an
    // unknown one - they reached the panel first, held the single operator lease and stood holding the key on an
    // unarmed crate, so the attackers never got to arm it ("bots don't arm / disarm in Rush", 2026-10-13).
    return team==1 ? status==BM_RUSH_UNKNOWN || status==BM_RUSH_IDLE :
        team==2 && status==BM_RUSH_ARMED;
}
static inline int bm_rush_event(BMRushState *r,int objective,enum BMRushStatus status,unsigned now)
{
    if(!bm_rush_active(r,objective) || status<BM_RUSH_IDLE || status>BM_RUSH_DESTROYED)return 0;
    BMRushObjective *o=&r->objective[objective];
    if(o->status==status)return 0;
    o->status=status;o->changedAt=now;o->leaseUntil=0;o->operatorPlayer=NULL;
    r->changedAt=now;r->revision++;r->confirmedEvents++;
    return 1;
}
static inline int bm_rush_sector_complete(const BMRushState *r)
{
    for(int k=(r->stage-1)*r->width+1;k<=r->stage*r->width;k++)
        if(k>16 || r->objective[k].status!=BM_RUSH_DESTROYED)return 0;
    return 1;
}
static inline int bm_rush_set_stage(BMRushState *r,int stage,unsigned now)
{
    if(stage<1 || stage>r->stages || stage<r->stage)return 0;
    if(stage==r->stage)return 1;
    // A confirmed new deployment stage also confirms that earlier objectives
    // are no longer actionable; it doesn't alter native scores or tickets.
    for(int k=1;k<=(stage-1)*r->width;k++){
        r->objective[k].status=BM_RUSH_DESTROYED;
        r->objective[k].operatorPlayer=NULL;r->objective[k].leaseUntil=0;
    }
    r->stage=stage;r->changedAt=now;r->revision++;
    return 1;
}
static inline int bm_rush_acquire(BMRushState *r,int objective,int team,void *player,unsigned now,unsigned duration)
{
    if(!player || !bm_rush_can_interact(r,objective,team))return 0;
    BMRushObjective *o=&r->objective[objective];
    if(o->operatorPlayer && o->operatorPlayer!=player && now<o->leaseUntil)return 0;
    o->operatorPlayer=player;o->leaseUntil=now+duration;
    return 1;
}
static inline void bm_rush_release(BMRushState *r,void *player)
{
    for(int k=1;k<=16;k++)if(r->objective[k].operatorPlayer==player){
        r->objective[k].operatorPlayer=NULL;r->objective[k].leaseUntil=0;
    }
}
