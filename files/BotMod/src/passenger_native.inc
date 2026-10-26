#include "lock_warn.h"   // AltBotMod V34
// AltBotMod V16: passengers. Bots spawn into a teammate's free seat (crew_spawn, now jeeps / boats / transports too) and
// get out again when riding no longer helps: a burning vehicle, a stop at an objective the team still has to take, or a
// long halt. The gunner seat (and a transport helicopter's door guns) stays manned - only cargo seats dismount.
static unsigned g_rideStopSince[MAX_BOTS];
static int passenger_keeps_seat(char *veh,int seat)
{
    const BMAirProfile *a=bm_air_profile(vehicle_name(veh));
    if(seat==1)return 1;                                         // the main gunner
    if(a && a->kind==BM_AIR_TRANSPORT && seat==2)return 1;       // the second door gun
    return 0;
}
static void passenger_out(int i,char *veh,int parachute,const char *why)
{
    bot_log("passenger: %s leaves %s's seat %d (%s)",H->bots[i]+OFF_PLAYER_ONLINE_NAME,vehicle_name(veh),vehicle_seat_of(veh,H->bots[i]),why);
    bot_exit_vehicle(i,parachute);
    g_driving[i]=0;g_rideStopSince[i]=0;gunner_reset(i);
    g_fleet[i].retryAt=H->frame+20*30;   // no straight re-boarding
    if(bot_brain(i)->task==TASK_NONE || bot_brain(i)->task==TASK_FOLLOW)choose_local_objective(i,why);
}
// Returns 1 when the bot got out.
static int passenger_dropout_tick(int i,char *veh)
{
    char *pl=H->bots[i];
    int seat=vehicle_seat_of(veh,pl);
    if(seat<=0 || stationary_weapon(veh))return 0;
    const BMAirProfile *a=bm_air_profile(vehicle_name(veh));
    VehTrack *t=vehicle_sample(veh);
    // 1. the vehicle is nearly wrecked: everyone out (a parachute from an aircraft)
    float hp=*(float *)(veh+OFF_VEHICLE_HEALTH);
    if(t && t->maxSeen>0 && hp>0 && hp<(g_enhanced.bailHealth+0.08f)*t->maxSeen){passenger_out(i,veh,a!=NULL,"the vehicle is nearly wrecked");return 1;}
    if(passenger_keeps_seat(veh,seat))return 0;
    // a bot driver on a job delivers its cargo itself (drive_arrive_objective / air_drop)
    char *driver=vehicle_occupant(veh);int dj=driver && is_bot(driver)?bot_index(driver):-1;
    if(dj>=0 && (g_drive[dj].phase || g_air[dj].phase)){g_rideStopSince[i]=0;return 0;}
    if(!driver){g_rideStopSince[i]=0;return 0;}   // (stranded_in_vehicle_tick: nobody drives it)
    int team=player_team(pl);float *vp=vehicle_pos(veh,NULL);
    if(!vp || team<1 || team>2)return 0;
    float speed=t?t->speed:0,nearWanted=1e30f;
    for(int f=0;f<objective_count();f++)if(objective_wanted(team,f) && objective_pos(f)){
        const float *o=objective_pos(f);float dx=o[0]-vp[0],dz=o[2]-vp[2],d=dx*dx+dz*dz;
        if(d<nearWanted)nearWanted=d;
    }
    // 2. an aircraft (a human pilot) over an objective we still have to take: jump with the chute
    if(a){
        if(nearWanted<70*70 && speed<35){passenger_out(i,veh,1,"over an objective - jumps");return 1;}
        return 0;
    }
    // 3. a ground vehicle / boat halted: at an objective at once (4 s), elsewhere after 25 s
    if(speed>2.0f){g_rideStopSince[i]=0;return 0;}
    if(!g_rideStopSince[i]){g_rideStopSince[i]=H->frame?H->frame:1;return 0;}
    unsigned halted=H->frame-g_rideStopSince[i];
    if(nearWanted<45*45 && halted>4*30){passenger_out(i,veh,0,"the vehicle stopped at an objective - captures on foot");return 1;}
    if(halted>(vehicle_is_boat(veh)?40:25)*30){passenger_out(i,veh,0,"the vehicle has not moved for a while");return 1;}
    return 0;
}
// AltBotMod V16: tanks / IFVs pop smoke (the vehicle countermeasure) when hit - once per 25 s - as players do against
// rockets and guided missiles. (Aircraft flares: air_native.inc.)
static float g_cmHealth[MAX_BOTS];static unsigned g_cmAt[MAX_BOTS],g_cmUsed;
static void vehicle_countermeasure_tick(int i)
{
    if(!g_enhanced.vehicleCountermeasures || (H->frame+i)%10)return;
    char *veh=player_vehicle(H->bots[i]);
    if(!veh || vehicle_seat_of(veh,H->bots[i])!=0 || stationary_weapon(veh) || bm_air_profile(vehicle_name(veh)) ||
       !bm_armored(bm_vehicle_profile(vehicle_name(veh)))){g_cmHealth[i]=0;return;}
    float hp=*(float *)(veh+OFF_VEHICLE_HEALTH),was=g_cmHealth[i];g_cmHealth[i]=hp;
    VehTrack *t=vehicle_sample(veh);
    // AltBotMod V34: a Javelin / missile lock on it (on_lock_update): smoke before the missile, as players do
    if(incoming_lock(veh)>=0.9f && (!g_cmAt[i] || H->frame-g_cmAt[i]>=25*30)){
        g_cmAt[i]=H->frame?H->frame:1;g_cmUsed++;
        schedule_press(i,EIA_AIR_COUNTERMEASURE,H->frame+1);
        bot_log("vehicle: %s's %s is locked on - smoke",H->bots[i]+OFF_PLAYER_ONLINE_NAME,vehicle_name(veh));
        return;
    }
    if(was<=0 || !t || t->maxSeen<=0 || was-hp<0.08f*t->maxSeen)return;   // a real hit, not a scratch
    if(g_cmAt[i] && H->frame-g_cmAt[i]<25*30)return;
    g_cmAt[i]=H->frame?H->frame:1;g_cmUsed++;
    schedule_press(i,EIA_AIR_COUNTERMEASURE,H->frame+1);
    bot_log("vehicle: %s's %s is hit (%.0f%% left) - smoke",H->bots[i]+OFF_PLAYER_ONLINE_NAME,vehicle_name(veh),100*hp/t->maxSeen);
}
