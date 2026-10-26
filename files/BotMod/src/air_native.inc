#include "lock_warn.h"   // AltBotMod V34
#ifndef AIR_LOCK_WANT   // AltBotMod V25: the game's lock (core.c on_lock_update); a harness without it never locks
#define AIR_LOCK_WANT(i, from, to, r) ((void)(i), (void)(from), (void)(r))
#define AIR_LOCK_STATE(i) ((void)(i), 0)
#define AIR_LOCK_COUNT() 0u
#endif
#ifndef AIR_LOCK_WANT_ENT
#define AIR_LOCK_WANT_ENT(i, from, to, r, t) ((void)(i), (void)(from), (void)(r), (void)(t))
#endif
#ifndef AIR_IS_GUNSHIP
#define AIR_IS_GUNSHIP(v) 0   // AltBotMod V23 (core.c: vehicle_is_gunship)
#endif
// Engine adapter, included after the native vehicle helpers in core.c.
// All engine reads/calls reuse the beta's addresses; no new pointer offsets.
enum { AIR_WALK=1, AIR_WAIT, AIR_FLY, AIR_RIDE };
// VU's published EntryInputActionEnum agrees with every action used by the
// beta; FireCountermeasure=10. Actual availability/unlocks need a live test.
#define EIA_AIR_COUNTERMEASURE 10
typedef struct AirNativeState {
    int phase, pilot, flag, dropping, takeover, patrolChosen;
    char *veh, *targetVeh, *targetPl;
    const BMAirProfile *profile;
    unsigned at, walkUntil, scanAt, probeAt, invalidAt, dropAt, patrolAt;
    unsigned counterAt, counterUntil;
    char *pickupPlayer;unsigned pickupAt,pickupRetryAt;int pickup;
    char *pickupAvoid;unsigned pickupAvoidUntil,pickupProgressAt;
    float pickupProgressDistance;
    char *pickupCandidate;int pickupScan,pickupCursor;
    float pickupBestDistance,pickupHeight,pickupProbePosition[3];unsigned pickupProbeAt;
    int transportPatrol;unsigned transportPatrolAt;
    float lastHealth;
    float home[3], goal[3], groundY, aheadY, obstacle;
    int groundValid, aheadValid;
    BMAirController control;
    BMAirMotion motion, targetMotion;
    BMAirOutput output;
    BMLandingState landing;
    float landingPad[3];
    unsigned landingRetryAt,landingThreatAt;
    int landingCandidate,landingPadValid,landingThreat,landingLearnedTry;   // (V21: learned landing spots tried)
    unsigned secondaryAt,tacticLogAt;int lastTactic;   // AltBotMod V20
    unsigned slotAt,slotPressUntil;int slot;          // AltBotMod V22: the weapon selected (1 guns, 2 missiles / rockets)
    float lastPos[3],lastClear;unsigned lastFlyAt;      // AltBotMod V21: where it last flew (learning what went wrong)
} AirNativeState;
static AirNativeState g_air[MAX_BOTS];
#ifndef EIA_SELECT_WEAPON1
#define EIA_SELECT_WEAPON1 16
#endif
#ifndef EIA_SELECT_WEAPON2
#define EIA_SELECT_WEAPON2 17   // EIASelectWeapon2 (VU EntryInputActionEnum; Fun-Bots selects vehicle weapon slot 2 with it)
#endif
// AltBotMod V20: every occupied aircraft on the map with a velocity estimate, refreshed once a frame. Jets see
// friends and foes in it (collision evasion) and the dogfight reads its target's real velocity from it.
typedef struct AirTraffic { char *veh; BMAirMotion motion; float pos[3]; } AirTraffic;
static AirTraffic g_airTraffic[32];
static int g_airTrafficCount;
static unsigned g_airTrafficFrame;
static unsigned g_airStats[4];   // dogfight gun passes, strike dives, secondary weapon shots, collision breaks
static void air_traffic_update(void)
{
    if(g_airTrafficFrame==H->frame && g_airTrafficCount>=0 && g_airTrafficFrame) return;
    g_airTrafficFrame=H->frame;
    char *pm=player_manager();void **players=NULL;unsigned count=0;
    if(pm){memcpy(&players,pm+0xac,sizeof players);memcpy(&count,pm+8,sizeof count);}
    AirTraffic next[32];int n=0;
    for(unsigned k=0;players && k<count && k<64 && n<32;k++){
        char *pl=players[k];if(!pl)continue;
        char *v=player_vehicle(pl);if(!v || !vehicle_ok(v))continue;
        int dup=0;for(int j=0;j<n;j++)if(next[j].veh==v)dup=1;if(dup)continue;
        if(!bm_air_profile(vehicle_name(v)) && bm_vehicle_profile(vehicle_name(v))->kind!=BM_AIR)continue;
        float *p=vehicle_pos(v,NULL);if(!p || !isfinite(p[0]) || !isfinite(p[1]) || !isfinite(p[2]))continue;
        AirTraffic *e=&next[n++];memset(e,0,sizeof *e);e->veh=v;
        for(int j=0;j<g_airTrafficCount;j++)if(g_airTraffic[j].veh==v){e->motion=g_airTraffic[j].motion;break;}
        bm_air_motion(&e->motion,p,BOT_TICK_DT);memcpy(e->pos,p,12);
    }
    memcpy(g_airTraffic,next,sizeof(AirTraffic)*n);g_airTrafficCount=n;
}
static const float *air_traffic_velocity(char *veh)
{
    for(int j=0;j<g_airTrafficCount;j++)if(g_airTraffic[j].veh==veh && g_airTraffic[j].motion.ready)return g_airTraffic[j].motion.velocity;
    return NULL;
}
static void air_traffic_fill(char *own,BMAirObservation *o)
{
    o->trafficCount=0;
    for(int j=0;j<g_airTrafficCount && o->trafficCount<BM_AIR_TRAFFIC;j++){
        AirTraffic *e=&g_airTraffic[j];if(e->veh==own || !e->motion.ready)continue;
        float d[3]={e->pos[0]-o->position[0],e->pos[1]-o->position[1],e->pos[2]-o->position[2]};
        if(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]>1500.0f*1500.0f)continue;
        memcpy(o->traffic[o->trafficCount],e->pos,12);memcpy(o->traffic[o->trafficCount]+3,e->motion.velocity,12);o->trafficCount++;
    }
}
static unsigned g_airCooldown[MAX_BOTS];
static unsigned g_airLandingProbeFrame;
static unsigned g_airPickupProbeFrame;
static char *air_seat_player(char *entry)
{
    // Byte copy also permits exercising the 32-bit offset in a 64-bit harness.
    char *player=NULL;
    if (entry) memcpy(&player,entry+0x4c,sizeof player);
    return player;
}
static int air_pilot(int i) { return g_air[i].pilot && g_air[i].phase>=AIR_WAIT; }
static void air_manual_guard(char *veh)
{
    bot_log("air: %s needs its flight controller; enable Jets/Helicopters in enhanced.ini and use ehreload (ground drive commands refused)",vehicle_name(veh));
}
static void air_neutral(int i)
{
    void *in=H->botInput[i]; if (!in) return;
    static const int actions[]={EIA_THROTTLE,EIA_BRAKE,EIA_STRAFE,EIA_YAW,EIA_PITCH,EIA_SEAT_YAW,EIA_FIRE,EIA_ZOOM,EIA_AIR_COUNTERMEASURE};
    for (unsigned k=0;k<sizeof actions/sizeof actions[0];k++)
        ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in,actions[k],0);
}
static void air_stop(int i,const char *why,int exitVehicle)
{
    AirNativeState *a=&g_air[i];
    if (!a->phase) return;
    char *veh=a->veh; int pilot=a->pilot;
    if(pilot && vehicle_ok(veh) &&
       (strstr(why,"runway stays blocked") || strstr(why,"takeoff failed") || strstr(why,"invalid physics") ||
        strstr(why,"invalid flight") || strstr(why,"no progress")))vehicle_mark_bad(veh,45);
    bot_log("air: %s %s",(char *)H->bots[i]+OFF_PLAYER_ONLINE_NAME,why);
    // AltBotMod V21: an aircraft lost (or abandoned, badly damaged) while flying under 35 m: learned - more height there
    if(pilot && a->lastFlyAt && H->frame-a->lastFlyAt<30 && a->lastClear<35 && (strstr(why,"lost pilot") || strstr(why,"badly damaged")))
        bm_learn(i,BM_LEARN_AIR_LOSS,a->lastPos,NULL);
    air_neutral(i);
    if (exitVehicle && player_vehicle(H->bots[i])==veh) bot_exit_vehicle(i,1);
    memset(a,0,sizeof *a); memset(&g_drive[i],0,sizeof g_drive[i]);
    g_driving[i]=0; gunner_reset(i);
    g_airCooldown[i]=H->frame+30*30;
    H->botMove[i][0]=H->botMove[i][1]=H->botMove[i][2]=0;
    bot_nav(i)->mode=1;
    bot_brain(i)->replanAt=0;
    if (level_ready()) choose_local_objective(i,"aircraft duty ended");
    // Only our bot crew is released; humans choose whether to stay aboard.
    if (pilot) for (int j=0;j<H->botCount;j++)
        if (j!=i && g_air[j].phase && g_air[j].veh==veh) air_stop(j,"pilot left - parachuting",1);
}
static int air_ground(const float *position,float forwardX,float forwardZ,float ahead,float *height)
{
    float from[3]={position[0]+forwardX*ahead,position[1]+(ahead>0 ? 100 : -3),position[2]+forwardZ*ahead};
    float to[3]={from[0],position[1]-900,from[2]}, hit[3];
    unsigned saved=g_rayFlags; g_rayFlags=RAY_DONT_CHECK_CHARACTER;
    int found=ray_cast(from,to,hit,NULL); g_rayFlags=saved;
    if (!found || !isfinite(hit[1]) || fabsf(hit[1])>100000) return 0;
    *height=hit[1]; return 1;
}
static void air_probe(AirNativeState *a,const float *xf)
{
    if (H->frame-a->probeAt<6 && a->probeAt) return;
    a->probeAt=H->frame;
    float len=hypotf(xf[8],xf[10]), fx=len>0.05f ? xf[8]/len : 0, fz=len>0.05f ? xf[10]/len : 1;
    float speed=hypotf(a->motion.velocity[0],a->motion.velocity[2]);
    float ahead=fmaxf(a->profile->kind==BM_AIR_JET ? 120 : 35,speed*2.5f);
    a->groundValid=air_ground(xf+12,fx,fz,0,&a->groundY);
    // Close to the spawn pad the below-hull ray starts under the terrain.
    if (!a->groundValid && dist2(xf+12,a->home)<25*25 && fabsf(xf[13]-a->home[1])<20) {
        a->groundValid=1; a->groundY=a->home[1]-2;
    }
    a->aheadValid=air_ground(xf+12,fx,fz,ahead,&a->aheadY);
    // Begin beyond the nose/rotor. Characters are ignored; terrain is included.
    float from[3]={xf[12]+fx*15,xf[13]+2,xf[14]+fz*15};
    float to[3]={xf[12]+fx*ahead,xf[13]+xf[9]*ahead+2,xf[14]+fz*ahead}, hit[3];
    unsigned saved=g_rayFlags; g_rayFlags=RAY_DONT_CHECK_CHARACTER|RAY_CHECK_DETAIL_MESH;
    int blocked=ray_cast(from,to,hit,NULL); g_rayFlags=saved;
    a->obstacle=blocked ? sqrtf(dist2(hit,xf+12)+(hit[1]-xf[13])*(hit[1]-xf[13])) : 0;
}
static void air_goal(int i,const float *pos)
{
    AirNativeState *a=&g_air[i]; int team=player_team(H->bots[i]);
    int f=mode_is_cq()?squad_objective(team,player_squad(H->bots[i])):mode_vehicle_target(i,pos);
    int dutyRole,duty=mode_is_cq()?director_target(i,&dutyRole):-1;
    if(duty>=0 && duty<objective_count())f=duty;
    else {
    if (f<0 || f>=objective_count()) f=a->flag;
    if (f<0 || f>=objective_count() || !objective_wanted(team,f)) {
        float best=1e30f; f=-1;
        for (int k=0;k<objective_count();k++) {
            if(!mode_is_cq() && !objective_wanted(team,k))continue;
            float d=dist2(pos,objective_pos(k));
            if (!objective_wanted(team,k)) d+=1000000;
            if (d<best) { best=d; f=k; }
        }
    }
    if(a->profile->kind!=BM_AIR_TRANSPORT && objective_count()){
        int previous=a->flag;
        float visit=a->profile->kind==BM_AIR_JET?700.0f:220.0f;
        int keep=0;
        if(a->patrolChosen && previous>=0 && previous<objective_count() && objective_wanted(team,previous)){
            if(dist2(pos,objective_pos(previous))>visit*visit){a->patrolAt=0;keep=1;}
            else {if(!a->patrolAt)a->patrolAt=H->frame?H->frame:1;keep=H->frame-a->patrolAt<25*30;}
        }
        if(keep)f=previous;
        else {
            float score=INFINITY;int selected=-1,hostile=0;
            for(int k=0;k<objective_count();k++)if(objective_wanted(team,k))hostile++;
            for(int k=0;k<objective_count();k++){
                if(!mode_is_cq() && !objective_wanted(team,k))continue;
                if(hostile && !objective_wanted(team,k))continue;
                if(a->patrolChosen && k==previous && (hostile?hostile:objective_count())>1)continue;
                int cover=0;for(int j=0;j<H->botCount;j++)if(j!=i && g_air[j].pilot && g_air[j].patrolChosen && player_team(H->bots[j])==team && g_air[j].flag==k)cover++;
                float value=sqrtf(dist2(pos,objective_pos(k)))+cover*4000.0f;
                if(value<score){score=value;selected=k;}
            }
            f=selected;a->patrolAt=0;a->patrolChosen=1;
        }
    }
    }
    a->flag=f;
    const float *center=f>=0 ? objective_pos(f) : a->home;
    int jet=a->profile->kind==BM_AIR_JET;
    memcpy(a->goal,center,12);
    a->goal[1]+=jet ? g_enhanced.air.jetHeight : g_enhanced.air.chopperHeight;
    float radius=jet ? g_enhanced.air.jetOrbit : g_enhanced.air.chopperOrbit;
    if (a->profile->kind!=BM_AIR_TRANSPORT || !g_enhanced.air.transportDrops || a->dropping) {
        float angle=atan2f(pos[0]-center[0],pos[2]-center[2]);
        angle+=(team==1 ? 1 : -1)*(jet ? 0.9f : 0.7f);
        a->goal[0]+=sinf(angle)*radius; a->goal[2]+=cosf(angle)*radius;
    }
    // The grid is a conservative patrol envelope, NOT the server's air bounds.
    NavGrid *grid=*nav_slot();
    if (grid && grid->w*grid->cell>200 && grid->h*grid->cell>200) {
        float inset=jet ? 60 : 20;
        a->goal[0]=fmaxf(grid->x0+inset,fminf(grid->x0+(grid->w-1)*grid->cell-inset,a->goal[0]));
        a->goal[2]=fmaxf(grid->z0+inset,fminf(grid->z0+(grid->h-1)*grid->cell-inset,a->goal[2]));
    }
}
static int air_enemy(char *veh,int team,float **position,int *isAir)
{
    if (!vehicle_ok(veh) || !vehicle_occupied(veh)) return 0;
    int vt=0;float *p=vehicle_pos(veh,&vt);
    // FunBots AirTargets keys targets by their pilot's current team. A
    // capturable jeep's old owner field is not authoritative for its crew.
    char *crew=vehicle_occupant(veh);
    if(crew && player_exists(crew))vt=player_team(crew);
    if (!p || vt<1 || vt>2 || vt==team || !isfinite(p[0]) || !isfinite(p[1]) || !isfinite(p[2])) return 0;
    *position=p;*isAir=bm_air_profile(vehicle_name(veh))!=NULL || bm_vehicle_profile(vehicle_name(veh))->kind==BM_AIR;
    return 1;
}
static float *air_infantry(char *pl,int team)
{
    if(!pl || !player_exists(pl) || player_team(pl)<1 || player_team(pl)>2 || player_team(pl)==team || player_vehicle(pl))return NULL;
    char *s=*(char **)(pl+OFF_PLAYER_SOLDIER);
    if(!s || *(float *)(s+OFF_SOLDIER_HEALTH)<=0)return NULL;
    float *p=(float *)(s+OFF_SOLDIER_SPAWN_POSITION);
    return isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]) ? p : NULL;
}
static void air_target(int i,BMAirObservation *o)
{
    AirNativeState *a=&g_air[i];int team=player_team(H->bots[i]);o->targetValid=0;
    if (!g_enhanced.air.weapons || a->profile->speed<=0 || a->control.phase==BM_FLIGHT_TAKEOFF ||
        a->control.phase==BM_FLIGHT_RECOVER) { a->targetVeh=a->targetPl=NULL;memset(&a->targetMotion,0,sizeof a->targetMotion);return; }
    // Empty scans also wait half a second; six pilots must not each rescan
    // every vehicle and player on every tick when no enemy is nearby.
    // V20: an attack in progress keeps its target while it lasts (Fun-Bots: a new target just as the jet lined up
    // spoiled the attack); a fresh choice after the pass.
    // V20.1: a strike run being set up keeps its ground target too (live: it switched targets mid-setup and flew to
    // the old target's initial point, so it never rolled in).
    int setup=a->profile->kind==BM_AIR_JET && a->control.phase==BM_FLIGHT_APPROACH &&
        ((a->targetPl && air_infantry(a->targetPl,team)) || (a->targetVeh && !bm_air_profile(vehicle_name(a->targetVeh)) &&
         bm_vehicle_profile(vehicle_name(a->targetVeh))->kind!=BM_AIR));
    int holding=(a->control.phase==BM_FLIGHT_ATTACK || setup) && (a->targetVeh || a->targetPl) && a->scanAt &&
        H->frame-a->scanAt<(setup?40:12)*30;
    if (!holding && (!a->scanAt || H->frame-a->scanAt>=15)) {
        a->scanAt=H->frame?H->frame:1;
        char **vehs,*best=NULL,*bestPl=NULL;int n=vehicle_list(&vehs);
        float bestScore=INFINITY;
        for (int k=0;k<n;k++) {
            char *v=vehs[k];float *p;int air;
            if (v==a->veh || !air_enemy(v,team,&p,&air)) continue;
            float distance=sqrtf(dist2(o->position,p)+(p[1]-o->position[1])*(p[1]-o->position[1]));
            if (distance>(a->profile->kind==BM_AIR_JET?3500:2500)) continue;
            // AltBotMod V6: helicopters fight enemy helicopters within 600 m (gunships first); jets stay a jet's job
            const BMAirProfile *tp=air?bm_air_profile(vehicle_name(v)):NULL;
            int heliTarget=tp && tp->kind!=BM_AIR_JET;
            // AltBotMod V23: a jet fights people. V20-V22 put every enemy aircraft within 3.5 km 1200 points ahead of anything on
            // the ground, so jets chased each other all round ("jets still don't do squat"). Now an enemy aircraft is the job
            // only when it is close (900 m - a dogfight on, or one coming at us) or it is the AC-130; otherwise armour and
            // groups of soldiers come first.
            int gunshipTarget=AIR_IS_GUNSHIP(v);
            float jetScore=gunshipTarget?-900:air?(distance<900?-700:500):-250;
            // AltBotMod V34: an enemy attack helicopter within 2 km is a jet's first job after the gunship (it is what kills the
            // jet's own team on the ground; missiles lock on it)
            if(air && !gunshipTarget && tp && tp->kind==BM_AIR_ATTACK && distance<2000)jetScore=-650;
            // AltBotMod V24: an enemy helicopter within 1.2 km is a helicopter's first job (V6-V23: only inside 600 m, and +400
            // beyond - soldiers on the ground nearly always won: "choppers almost never attack each other")
            float score=distance+(a->profile->kind==BM_AIR_JET?jetScore:
                (heliTarget && distance<1200?(tp->kind==BM_AIR_ATTACK?-700:-450):air?(gunshipTarget?-150:400):0));
            // V20 (Fun-Bots AirTargets): 400 per radian off the nose - a target ahead beats one behind
            if(distance>1)score+=400*acosf(fmaxf(-1,fminf(1,(o->forward[0]*(p[0]-o->position[0])+o->forward[1]*(p[1]-o->position[1])+o->forward[2]*(p[2]-o->position[2]))/distance)));
            if(v==a->targetVeh)score-=150;
            for(int j=0;j<H->botCount;j++)if(j!=i && g_air[j].pilot && player_team(H->bots[j])==team && g_air[j].targetVeh==v)score+=350;
            if(score<bestScore){bestScore=score;best=v;bestPl=NULL;}
        }
        char *pm=player_manager();void **players=NULL;unsigned count=0;
        if(pm){memcpy(&players,pm+0xac,sizeof players);memcpy(&count,pm+8,sizeof count);}
        for(unsigned k=0;players && k<count && k<64;k++){
            char *pl=players[k];float *p=air_infantry(pl,team);if(!p)continue;
            float distance=sqrtf(dist2(o->position,p)+(p[1]-o->position[1])*(p[1]-o->position[1]));
            if(distance>(a->profile->kind==BM_AIR_JET?2000:1500))continue;   // V20: 2000 / 1500 m (1600 / 1200)
            // Caspian V5 (was 2000/500): aircraft hit infantry too. V20: jets +300 (700), gunships +0 (150), scouts +50.
            float score=distance+(a->profile->kind==BM_AIR_JET?-150:a->profile->kind==BM_AIR_ATTACK?0:50);   // (V23: jets -150, was +300)
            // AltBotMod V16: a group of soldiers is a better strafing run than a lone one (each other enemy within 15 m: -120; V23: -160)
            for(unsigned m=0;m<count && m<64;m++)if(m!=k){float *q=air_infantry(players[m],team);if(q && dist2(q,p)<15*15)score-=160;}
            if(pl==a->targetPl)score-=150;
            if(score<bestScore){bestScore=score;best=NULL;bestPl=pl;}
        }
        if(best!=a->targetVeh || bestPl!=a->targetPl){
            a->targetVeh=best;a->targetPl=bestPl;memset(&a->targetMotion,0,sizeof a->targetMotion);
            a->control.ipValid=0;   // V20.1: a new target needs a new initial point
            char *spot=best?vehicle_occupant(best):bestPl;if(spot)bot_spot(H->bots[i],spot);
        }
    }
    float *p=NULL;int air=0;
    if(a->targetVeh){if(!air_enemy(a->targetVeh,team,&p,&air))a->targetVeh=NULL;}
    else if(a->targetPl){p=air_infantry(a->targetPl,team);if(!p)a->targetPl=NULL;}
    if(!p){memset(&a->targetMotion,0,sizeof a->targetMotion);return;}
    bm_air_motion(&a->targetMotion,p,BOT_TICK_DT);
    memcpy(o->target,p,12);o->target[1]+=a->targetVeh?1.0f:1.1f;
    memcpy(o->targetVelocity,a->targetMotion.velocity,12);o->targetValid=1;o->targetAir=air;
    const float *tv=a->targetVeh?air_traffic_velocity(a->targetVeh):NULL;   // V20: the shared per-frame estimate
    if(tv)memcpy(o->targetVelocity,tv,12);
    float muzzle[3];for(int k=0;k<3;k++)muzzle[k]=o->position[k]+o->right[k]*a->profile->muzzle[0]+o->up[k]*a->profile->muzzle[1]+o->forward[k]*a->profile->muzzle[2];
    o->targetVisible=gun_can_see(muzzle,o->target,a->targetVeh!=NULL);
}

static int air_claim(int i,char *veh,int pilot,int flag)
{
    char *s=*(char **)((char *)H->bots[i]+OFF_PLAYER_SOLDIER);
    const BMAirProfile *p=bm_air_profile(vehicle_name(veh));
    float *xf=vehicle_xf(veh);
    if (!s || !p || !bm_air_transform_valid(xf) || (pilot && !fleet_pilot_allowed(i))) return 0;
    if (!player_vehicle(H->bots[i]) && !plan_to_vehicle(i,s,xf+12)) return 0;
    end_task(i,bot_brain(i),"assigned aircraft");
    memset(&g_flagRoute[i],0,sizeof g_flagRoute[i]);
    g_fleet[i].vehicle=NULL;g_fleet[i].role=0;g_fleet[i].until=0;
    memset(g_sched[i],0,sizeof g_sched[i]);
    for (int k=0;k<MAX_PRESSES;k++) if (g_press[i][k].frames>0 && H->botInput[i])
        ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(H->botInput[i],g_press[i][k].action,0);
    memset(g_press[i],0,sizeof g_press[i]);
    AirNativeState *a=&g_air[i]; memset(a,0,sizeof *a);
    a->phase=AIR_WALK; a->pilot=pilot; a->veh=veh; a->profile=p; a->flag=flag; a->at=H->frame;
    a->walkUntil=H->frame+fleet_walk_frames(sqrtf(dist2(xf+12,(float *)(s+OFF_SOLDIER_POSITION))));
    memcpy(a->home,xf+12,12);
    memset(&g_drive[i],0,sizeof g_drive[i]);
    g_drive[i].phase=9; g_drive[i].veh=veh; g_drive[i].flag=flag;
    bot_nav(i)->mode=vehicle_board_close(s,xf+12)?0:3;
    if (player_vehicle(H->bots[i])==veh) {
        air_probe(a,xf);
        int flying=a->groundValid && xf[13]-a->groundY>30;
        a->phase=pilot ? (flying ? AIR_FLY : AIR_WAIT) : AIR_RIDE; bot_nav(i)->mode=0;
        bm_air_start(&a->control,p->kind,flying); g_driving[i]=(unsigned char)pilot;
        if (pilot) press_button(i,16,2);
    }
    return 1;
}
static int air_ride(int i,char *veh)
{
    if (vehicle_seat_of(veh,H->bots[i])<=0) return 0;
    for (int j=0;j<H->botCount;j++)
        if (g_air[j].phase && g_air[j].pilot && g_air[j].veh==veh)
            return air_claim(i,veh,0,g_air[j].flag);
    const BMAirProfile *p=bm_air_profile(vehicle_name(veh));
    if(p && p->kind==BM_AIR_TRANSPORT && bm_air_allowed(&g_enhanced.air,p))return air_claim(i,veh,0,bot_brain(i)->targetFlag);
    return 0;
}
static void air_invite(int pilot)
{
    AirNativeState *a=&g_air[pilot]; char *pl=H->bots[pilot], **entries=NULL;
    int seats=vehicle_seats(a->veh,&entries), invited=0;
    int available=free_seat_count(a->veh)-(seats>2 ? 1 : 0);
    if(a->pickup){int cargo=0;for(int k=a->profile->firstPassenger;k<seats;k++)if(!air_seat_player(entries[k]))cargo++;if(available>cargo)available=cargo;} // reserve one human seat
    for (int j=0;j<H->botCount && invited<available;j++) {
        char *other=H->bots[j], *s=*(char **)(other+OFF_PLAYER_SOLDIER);
        BotBrain *b=bot_brain(j);
        if (j==pilot || !s || !b->enabled || b->task!=TASK_NONE || g_drive[j].phase || g_air[j].phase ||
            (g_fleet[j].vehicle && H->frame<g_fleet[j].until) || player_vehicle(other) ||
            player_team(other)!=player_team(pl) || (!a->pickup && player_squad(other)!=player_squad(pl)) ||
            H->frame<g_airCooldown[j] || dist2((float *)(s+OFF_SOLDIER_POSITION),a->pickup ? vehicle_xf(a->veh)+12 : a->home)>90*90) continue;
        if (air_claim(j,a->veh,0,a->flag)) {g_air[j].pickup=a->pickup;invited++;}
    }
}
static void air_consider(int i)
{
    char *pl=H->bots[i], *s=*(char **)(pl+OFF_PLAYER_SOLDIER);
    BotBrain *b=bot_brain(i);
    if (g_driveOff || !g_enhanced.driving || !g_vehInput || !level_ready() || !s || !b->enabled ||
        (!g_enhanced.air.jets && !g_enhanced.air.helicopters) || g_air[i].phase || H->frame<g_airCooldown[i]) return;
    char *current=player_vehicle(pl);
    if (current) {
        const BMAirProfile *p=bm_air_profile(vehicle_name(current));
        if (bm_air_allowed(&g_enhanced.air,p) && vehicle_seat_of(current,pl)==0 && !g_drive[i].phase)
            air_claim(i,current,1,b->targetFlag); // hot-reload / manual entry adoption
        return;
    }
    if (g_enhanced.returnToBase || !mode_is_cq()) return;
    if (g_drive[i].phase || b->task!=TASK_NONE || (H->frame+i)%30 || H->frame-g_lastSpawnedAt[i]<3*30) return;
    int team=player_team(pl);
    char **vehs, *best=NULL; int n=vehicle_list(&vehs);
    float bestD=g_enhanced.air.findRadius*g_enhanced.air.findRadius;
    for (int k=0;k<n;k++) {
        char *v=vehs[k]; const BMAirProfile *p=bm_air_profile(vehicle_name(v));
        if (!vehicle_ok(v) || !bm_air_allowed(&g_enhanced.air,p) || !driver_seat_free(v) || vehicle_bad(v) || !fleet_air_slot(team,p)) continue;
        float *xf=vehicle_xf(v); int vt=0; float *vp=vehicle_pos(v,&vt);
        if (!vp || !bm_air_transform_valid(xf) || (vt && vt!=team) || human_near(vp,g_enhanced.humanRadius)) continue;
        if (!vt && g_hqPos[team][0] && g_hqPos[3-team][0] && dist2(vp,g_hqPos[team])>dist2(vp,g_hqPos[3-team])) continue;
        if (fabsf(vp[1]-((float *)(s+OFF_SOLDIER_POSITION))[1])>12) continue;
        int claimed=fleet_claimed(v,i);
        float distance=dist2(vp,(float *)(s+OFF_SOLDIER_POSITION));
        if (claimed || distance>=bestD) continue;
        best=v; bestD=distance;
    }
    if (best && air_claim(i,best,1,b->targetFlag))
        bot_log("air: %s walks to %s %.0f m away",pl+OFF_PLAYER_ONLINE_NAME,vehicle_name(best),sqrtf(bestD));
}
static void air_drop(int i,const float *position)
{
    AirNativeState *a=&g_air[i];
    if (!g_enhanced.air.transportDrops || a->profile->kind!=BM_AIR_TRANSPORT || a->flag<0 || a->dropping || a->landing.phase!=BM_LAND_NONE ||
        a->control.phase!=BM_FLIGHT_CRUISE || H->frame-a->dropAt<15) return;
    const float *fp=objective_pos(a->flag);
    float height=position[1]-fp[1];
    if (dist2(position,fp)>35*35 || height<40 || height>200 ||
        hypotf(a->motion.velocity[0],a->motion.velocity[2])>12) return;
    char **entries=NULL; int n=vehicle_seats(a->veh,&entries), remaining=0;
    for (int k=a->profile->firstPassenger;k<n;k++) {
        char *occupant=air_seat_player(entries[k]); int j=occupant ? bot_index(occupant) : -1;
        if (j<0) continue;
        remaining++;
        if (H->frame-a->dropAt>=15) {
            if (g_air[j].phase) air_stop(j,"drops over its objective",1);
            else { bot_exit_vehicle(j,1); end_task(j,bot_brain(j),"air drop"); choose_local_objective(j,"air drop"); }
            a->dropAt=H->frame;
        }
    }
    if (!remaining) a->dropping=1; // return to patrol; gunners remain aboard
}
static int air_cargo(AirNativeState *a)
{
    char **entries=NULL;int n=vehicle_seats(a->veh,&entries),cargo=0;
    for(int k=a->profile->firstPassenger;k<n;k++)if(air_seat_player(entries[k]))cargo++;
    return cargo;
}
static int air_pickup_ground(const float *p,float *height)
{
    float above[3]={p[0],p[1]+8,p[2]};
    if(!air_ground(above,0,0,0,height))return 0;
    for(int k=0;k<4;k++){float q[3]={p[0]+(k&1?6:-6),p[1]+8,p[2]+(k&2?6:-6)},h;
        if(!air_ground(q,0,0,0,&h)||fabsf(h-*height)>1.5f)return 0;}
    return fabsf(p[1]-*height)<2;
}
static void air_transport_patrol(int i,const float *position)
{
    AirNativeState *a=&g_air[i];int team=player_team(H->bots[i]);
    int n=objective_count(),old=a->transportPatrol-1;
    if(!a->transportPatrolAt || H->frame-a->transportPatrolAt>=20*30 || old<0 || old>=n){
        int best=-1;float score=INFINITY;
        for(int f=0;f<n;f++){
            if(!mode_is_cq() && !objective_wanted(team,f))continue;
            if(f==old && n>1)continue;
            int cover=0;for(int j=0;j<H->botCount;j++)if(j!=i && g_air[j].pilot &&
                player_team(H->bots[j])==team && g_air[j].transportPatrol==f+1)cover++;
            float value=sqrtf(dist2(position,objective_pos(f)))+cover*3000.0f;
            if(value<score){score=value;best=f;}
        }
        a->transportPatrol=best+1;a->transportPatrolAt=H->frame?H->frame:1;old=best;
    }
    const float *center=old>=0 && old<n?objective_pos(old):a->home;
    float angle=atan2f(position[0]-center[0],position[2]-center[2])+(team==1?0.9f:-0.9f);
    float radius=fmaxf(120,g_enhanced.air.chopperOrbit);
    a->goal[0]=center[0]+sinf(angle)*radius;
    a->goal[1]=center[1]+g_enhanced.air.chopperHeight;
    a->goal[2]=center[2]+cosf(angle)*radius;
    NavGrid *grid=*nav_slot();
    if(grid && grid->w*grid->cell>200 && grid->h*grid->cell>200){
        a->goal[0]=fmaxf(grid->x0+20,fminf(grid->x0+(grid->w-1)*grid->cell-20,a->goal[0]));
        a->goal[2]=fmaxf(grid->z0+20,fminf(grid->z0+(grid->h-1)*grid->cell-20,a->goal[2]));
    }
}
static void air_transport_job(int i,const float *position)
{
    AirNativeState *a=&g_air[i];if(a->profile->kind!=BM_AIR_TRANSPORT||!g_enhanced.air.transportDrops||a->control.phase==BM_FLIGHT_TAKEOFF||a->control.phase==BM_FLIGHT_RECOVER)return;
    if(a->landing.phase!=BM_LAND_NONE)return;
    if(air_cargo(a)){a->pickup=0;a->pickupPlayer=NULL;a->dropping=0;return;}
    int team=player_team(H->bots[i]),valid=0;float h=0;
    if(a->pickupPlayer&&player_exists(a->pickupPlayer)){char *s=*(char **)(a->pickupPlayer+OFF_PLAYER_SOLDIER);valid=s&&player_team(a->pickupPlayer)==team&&!player_vehicle(a->pickupPlayer)&&H->frame-a->pickupAt<60*30;
        if(valid){
            float distance=sqrtf(dist2(position,(float *)(s+OFF_SOLDIER_POSITION)));
            if(!a->pickupProgressAt || distance<a->pickupProgressDistance-10){a->pickupProgressAt=H->frame?H->frame:1;a->pickupProgressDistance=distance;}
            else if(H->frame-a->pickupProgressAt>=20*30){
                a->pickupAvoid=a->pickupPlayer;a->pickupAvoidUntil=H->frame+60*30;valid=0;
                diagnostic_note(i,"transport pickup stalled; selects another job",a->veh);
            }
        }
        if(valid){
            float *p=(float *)(s+OFF_SOLDIER_POSITION);h=a->pickupHeight;
            if(!a->pickupProbeAt || H->frame-a->pickupProbeAt>=30 || dist2(p,a->pickupProbePosition)>6*6){
                if(g_airPickupProbeFrame==H->frame)return;
                g_airPickupProbeFrame=H->frame;a->pickupProbeAt=H->frame?H->frame:1;
                valid=air_pickup_ground(p,&h);a->pickupHeight=h;memcpy(a->pickupProbePosition,p,12);
            }
        }}
    if(!valid){a->pickupPlayer=NULL;a->pickup=0;
        if(!a->pickupScan && H->frame>=a->pickupRetryAt){a->pickupScan=1;a->pickupCursor=0;a->pickupCandidate=NULL;a->pickupBestDistance=1200*1200;}
        while(a->pickupScan && a->pickupCursor<H->botCount){
            int j=a->pickupCursor;char *pl=H->bots[j],*s=*(char **)(pl+OFF_PLAYER_SOLDIER);
            if(j==i||!s||player_team(pl)!=team||player_vehicle(pl)||g_drive[j].phase||bot_brain(j)->task!=TASK_NONE||!bot_brain(j)->enabled||(pl==a->pickupAvoid&&H->frame<a->pickupAvoidUntil)){a->pickupCursor++;continue;}
            int assigned=0;for(int k=0;k<H->botCount;k++)if(g_air[k].pickupPlayer==pl)assigned=1;
            float *p=(float *)(s+OFF_SOLDIER_POSITION),d=dist2(p,position);
            if(assigned || (a->flag>=0&&dist2(p,objective_pos(a->flag))<100*100) || d>=a->pickupBestDistance){a->pickupCursor++;continue;}
            if(g_airPickupProbeFrame==H->frame)break;
            g_airPickupProbeFrame=H->frame;a->pickupCursor++;
            float ground;if(air_pickup_ground(p,&ground)){a->pickupBestDistance=d;a->pickupCandidate=pl;a->pickupHeight=ground;}
            break; // one surveyed candidate shared by all transports per frame
        }
        if(a->pickupScan && a->pickupCursor>=H->botCount){
            a->pickupScan=0;a->pickupRetryAt=H->frame+15*30;
            char *candidate=a->pickupCandidate,*s=candidate && player_exists(candidate)?*(char **)(candidate+OFF_PLAYER_SOLDIER):NULL;
            int assigned=0;for(int k=0;k<H->botCount;k++)if(g_air[k].pickupPlayer==candidate && candidate)assigned=1;
            if(s && !assigned && player_team(candidate)==team && !player_vehicle(candidate)){
                a->pickupPlayer=candidate;a->pickup=1;a->pickupAt=H->frame;
                a->pickupProgressAt=H->frame?H->frame:1;a->pickupProgressDistance=sqrtf(dist2(position,(float *)(s+OFF_SOLDIER_POSITION)));
                a->pickupProbeAt=H->frame?H->frame:1;memcpy(a->pickupProbePosition,s+OFF_SOLDIER_POSITION,12);h=a->pickupHeight;
            }
        }
    }
    if(a->pickupPlayer){char *s=*(char **)(a->pickupPlayer+OFF_PLAYER_SOLDIER);if(!s)return;
        float *p=(float *)(s+OFF_SOLDIER_POSITION);memcpy(a->goal,p,12);float d=sqrtf(dist2(p,position)),speed=hypotf(a->motion.velocity[0],a->motion.velocity[2]);
        a->goal[1]=h+(d<25&&speed<8?5:fmaxf(30,g_enhanced.air.chopperHeight*fminf(1,d/200)));
        if(!g_enhanced.air.landings && d<30&&position[1]-h<9&&speed<3)air_invite(i);
    }else air_transport_patrol(i,position);
}
#include "transport_landings_native.inc"
// AltBotMod V20: what the pilot does in a fight, for the log (a change of tactic; the routine ones at most every
// 3 s), and the start of a secondary weapon shot.
static const char *air_target_name(AirNativeState *a,char *buffer,unsigned size)
{
    if(a->targetVeh){char *crew=vehicle_occupant(a->targetVeh);
        snprintf(buffer,size,"%s (%s)",vehicle_name(a->targetVeh),crew && player_exists(crew)?crew+OFF_PLAYER_ONLINE_NAME:"?");}
    else if(a->targetPl && player_exists(a->targetPl))snprintf(buffer,size,"%s",a->targetPl+OFF_PLAYER_ONLINE_NAME);
    else snprintf(buffer,size,"its target");
    return buffer;
}
static void air_combat_log(int i)
{
    AirNativeState *a=&g_air[i];char *pl=H->bots[i],name[96];
    if(a->output.secondary && !a->secondaryAt){
        a->secondaryAt=H->frame?H->frame:1;g_airStats[2]++;
        bot_log("air: %s fires its secondary weapon (%s) at %s",pl+OFF_PLAYER_ONLINE_NAME,
            a->targetVeh && bm_air_profile(vehicle_name(a->targetVeh))?"missile":"rockets/missile",air_target_name(a,name,sizeof name));
    }
    int t=a->output.tactic;if(t==a->lastTactic)return;
    int previous=a->lastTactic;a->lastTactic=t;
    if(t==BM_TACTIC_GUNS && previous!=BM_TACTIC_PURSUE && previous!=BM_TACTIC_NONE)return;   // re-acquired mid-fight
    if(t==BM_TACTIC_GUNS)g_airStats[0]++;
    if(t==BM_TACTIC_DIVE)g_airStats[1]++;
    if(t==BM_TACTIC_EVADE || t==BM_TACTIC_BREAK)g_airStats[3]++;
    int always=t==BM_TACTIC_DIVE || t==BM_TACTIC_BREAK || t==BM_TACTIC_EVADE;
    if(t==BM_TACTIC_NONE || t==BM_TACTIC_EXTEND || (!always && a->tacticLogAt && H->frame-a->tacticLogAt<3*30))return;
    a->tacticLogAt=H->frame?H->frame:1;
    int withTarget=t==BM_TACTIC_PURSUE || t==BM_TACTIC_GUNS || t==BM_TACTIC_SETUP || t==BM_TACTIC_DIVE || t==BM_TACTIC_STRAFE || t==BM_TACTIC_BREAK;
    bot_log("air: %s (%s) %s%s%s",pl+OFF_PLAYER_ONLINE_NAME,a->profile->name,bm_air_tactic_name(t),withTarget?" ":"",
        withTarget?air_target_name(a,name,sizeof name):"");
}
// AltBotMod V29: the box a jet stays in - every flag, both bases and the airfields (the jets' own boarding spots), plus
// JetAreaMarginMeters (350 m), at least 1.6 km across. BotMod does not read the server's combat area; this box is meant to
// lie inside it (the flags, the bases and the airfields are inside it, and BF3's area for jets reaches beyond them).
// Not checked against the real area on every map: a jet that still goes out shows in the log as "turns back" lines.
static void air_area(int i,BMAirObservation *o)
{
    AirNativeState *a=&g_air[i];
    o->areaValid=0;
    if(a->profile->kind!=BM_AIR_JET || !(g_enhanced.air.jetAreaMargin>0))return;
    float lo[2]={1e30f,1e30f},hi[2]={-1e30f,-1e30f};int n=0;
    #define AIR_AREA_ADD(p) do{const float *q_=(p);if(q_ && isfinite(q_[0]) && isfinite(q_[2]) && (q_[0]!=0 || q_[2]!=0)){ \
        lo[0]=fminf(lo[0],q_[0]);hi[0]=fmaxf(hi[0],q_[0]);lo[1]=fminf(lo[1],q_[2]);hi[1]=fmaxf(hi[1],q_[2]);n++;}}while(0)
    for(int f=0;f<objective_count();f++)AIR_AREA_ADD(objective_pos(f));
    AIR_AREA_ADD(g_hqPos[1]);AIR_AREA_ADD(g_hqPos[2]);
    for(int j=0;j<H->botCount && j<MAX_BOTS;j++)if(g_air[j].phase && g_air[j].profile && g_air[j].profile->kind==BM_AIR_JET)AIR_AREA_ADD(g_air[j].home);
    #undef AIR_AREA_ADD
    if(n<2)return;
    float m=g_enhanced.air.jetAreaMargin;
    for(int k=0;k<2;k++){
        float c=0.5f*(lo[k]+hi[k]),half=fmaxf(0.5f*(hi[k]-lo[k])+m,1400.0f);   // (V34: 1400 m - V30 1000 m, V29 800 m)
        o->areaMin[k]=c-half;o->areaMax[k]=c+half;
    }
    o->areaValid=1;
}
static void air_tick(int i)
{
    AirNativeState *a=&g_air[i]; if (!a->phase) return;
    char *pl=H->bots[i], *s=*(char **)(pl+OFF_PLAYER_SOLDIER);
    if (!level_ready()) { memset(&a->output,0,sizeof a->output); return; }
    if (!s || !bot_brain(i)->enabled || !vehicle_ok(a->veh)) { air_stop(i,"lost pilot/aircraft",1); return; }
    if (g_driveOff || !g_enhanced.driving || !g_vehInput || !bm_air_allowed(&g_enhanced.air,a->profile)) {
        air_stop(i,"aircraft policy disabled - exits",1); return;
    }
    float *xf=vehicle_xf(a->veh);
    if (!bm_air_transform_valid(xf)) {
        memset(&a->output,0,sizeof a->output);
        if (!a->invalidAt) a->invalidAt=H->frame;
        if (H->frame-a->invalidAt>=15) air_stop(i,"invalid physics transform - exits",1);
        return;
    }
    a->invalidAt=0;
    char *current=player_vehicle(pl);
    if (a->phase==AIR_WALK) {
        if (H->frame>=a->walkUntil || (current && current!=a->veh)) { air_stop(i,"boarding cancelled",0); return; }
        if (!current) {
            if (a->pilot && !driver_seat_free(a->veh)) { air_stop(i,"pilot seat taken",0); return; }
            if (human_near(xf+12,g_enhanced.humanRadius) && a->pilot) { air_stop(i,"reserves aircraft for nearby player",0); return; }
            if (!vehicle_finish_approach(i,s,xf+12,VEHICLE_BOARD_DIST)) return;
            int seat=a->pilot ? 0 : free_seat(a->veh);
            if(!a->pilot && a->pickup){char **cargo=NULL;int count=vehicle_seats(a->veh,&cargo);seat=-1;for(int k=a->profile->firstPassenger;k<count;k++)if(!air_seat_player(cargo[k])){seat=k;break;}}
            char **entries=NULL; int seats=vehicle_seats(a->veh,&entries);
            if (seat<0 || (!a->pilot && (seat==0 || (seats>2 && free_seat_count(a->veh)<=1)))) { air_stop(i,"no crew seat available",0); return; }
            ((EnterVehicle_t)ADDR_PLAYER_ENTER_VEHICLE)(pl,a->veh,seat);
            current=player_vehicle(pl); if (current!=a->veh) { air_stop(i,"native boarding rejected",0); return; }
        }
        int seat=vehicle_seat_of(a->veh,pl);
        if ((a->pilot && seat!=0) || (!a->pilot && seat<=0)) { air_stop(i,"wrong aircraft seat",1); return; }
        bot_nav(i)->mode=0; H->botMove[i][0]=H->botMove[i][1]=H->botMove[i][2]=0;
        a->phase=a->pilot ? AIR_WAIT : AIR_RIDE; a->at=H->frame;
        if (a->pilot) { g_driving[i]=1; press_button(i,16,2); air_invite(i); }
        bot_log("air: %s boards %s seat %d",pl+OFF_PLAYER_ONLINE_NAME,a->profile->name,seat);
    }
    if (current!=a->veh) { air_stop(i,"left aircraft",0); return; }
    H->botMove[i][0]=H->botMove[i][1]=H->botMove[i][2]=0;
    if (!a->pilot) {
        char **entries=NULL; int n=vehicle_seats(a->veh,&entries);
        char *driver=n ? air_seat_player(entries[0]) : NULL;
        if (!driver && !g_enhanced.passengerTakeover) { air_stop(i,"unpiloted aircraft - exits",1); return; }
        return;
    }
    if (vehicle_seat_of(a->veh,pl)!=0) { air_stop(i,"pilot seat changed",0); return; }
    bm_air_motion(&a->motion,xf+12,BOT_TICK_DT); air_probe(a,xf);
    float speed=sqrtf(a->motion.velocity[0]*a->motion.velocity[0]+a->motion.velocity[1]*a->motion.velocity[1]+a->motion.velocity[2]*a->motion.velocity[2]);
    if (a->phase==AIR_WAIT) {
        if (!a->takeover && human_near(xf+12,g_enhanced.humanRadius)) { air_stop(i,"hands aircraft to nearby player",1); return; }
        if (H->frame-a->at<(unsigned)(g_enhanced.air.waitSeconds*30)) return;
        // FunBots runway guard is for jets. A helicopter can lift vertically
        // beside a parked vehicle; a runway queue must not cancel its takeoff.
        char **vehicles; int n=vehicle_list(&vehicles), blocked=0;
        for (int k=0;a->profile->kind==BM_AIR_JET && k<n;k++) {
            if (vehicles[k]==a->veh || !vehicle_ok(vehicles[k])) continue;
            float *p=vehicle_pos(vehicles[k],NULL); if (!p || fabsf(p[1]-xf[13])>10) continue;
            float dx=p[0]-xf[12], dz=p[2]-xf[14], along=dx*xf[8]+dz*xf[10], side=dx*xf[0]+dz*xf[2];
            if (along>0 && along<30 && fabsf(side)<10) blocked=1;
        }
        if (blocked) { if (H->frame-a->at>25*30) air_stop(i,"runway stays blocked",1); return; }
        bm_air_start(&a->control,a->profile->kind,a->groundValid && xf[13]-a->groundY>30);
        a->phase=AIR_FLY; a->at=H->frame;
        bot_log("air: %s starts %s flight",pl+OFF_PLAYER_ONLINE_NAME,a->profile->name);
    }
    VehTrack *health=vehicle_sample(a->veh);
    float currentHealth=*(float *)(a->veh+OFF_VEHICLE_HEALTH);
    // AltBotMod V34: a lock on this aircraft (anyone's missile seeker, from the game's own locking controller - on_lock_update)
    // pops flares before the missile comes, once it is 75% on (once every 8 s); the hit below stays as the last resort
    if (g_enhanced.air.countermeasures && incoming_lock(a->veh)>=0.75f && (!a->counterAt || H->frame-a->counterAt>=8*30)) {
        a->counterAt=H->frame; a->counterUntil=H->frame+3; g_lockWarnFlares++;
        if (g_lockWarnFlares<=10 || g_lockWarnFlares%25==0)
            bot_log("air: %s is being locked on - flares (%u so far)",pl+OFF_PLAYER_ONLINE_NAME,g_lockWarnFlares);
    }
    if (g_enhanced.air.countermeasures && a->lastHealth>0 && health && health->maxSeen>0 &&
        a->lastHealth-currentHealth>=health->maxSeen*0.05f &&
        (!a->counterAt || H->frame-a->counterAt>=25*30)) {
        a->counterAt=H->frame; a->counterUntil=H->frame+3;
        bot_log("air: %s requests countermeasure after aircraft damage (native lock-on event not bound)",pl+OFF_PLAYER_ONLINE_NAME);
    }
    a->lastHealth=currentHealth;
    if (health && health->maxSeen>0 && *(float *)(a->veh+OFF_VEHICLE_HEALTH)<health->maxSeen*g_enhanced.bailHealth) {
        air_stop(i,"aircraft badly damaged - parachutes",1); return;
    }
    if (a->control.phase==BM_FLIGHT_TAKEOFF && H->frame-a->at>25*30 &&
        (speed<10 || (a->groundValid && xf[13]-a->groundY<8))) {
        air_stop(i,"takeoff failed - releases aircraft",1); return;
    }
    air_goal(i,xf+12);
    air_transport_job(i,xf+12);
    int landing=air_landing_tick(i,xf+12);
    BMAirObservation o; memset(&o,0,sizeof o);
    memcpy(o.position,xf+12,12); memcpy(o.right,xf,12); memcpy(o.up,xf+4,12); memcpy(o.forward,xf+8,12);
    memcpy(o.velocity,a->motion.velocity,12); memcpy(o.goal,a->goal,12); o.velocityValid=a->motion.ready;
    o.groundValid=a->groundValid; o.groundY=a->groundY; o.aheadGroundValid=a->aheadValid; o.aheadGroundY=a->aheadY;
    o.obstacleDistance=a->obstacle; o.orbitDirection=player_team(pl)==1 ? 1 : -1;
    if(a->phase==AIR_FLY && a->control.phase!=BM_FLIGHT_TAKEOFF){memcpy(a->lastPos,xf+12,12);a->lastClear=a->groundValid?xf[13]-a->groundY:1000;a->lastFlyAt=H->frame?H->frame:1;}
    o.extraClearance=(float)bm_learn_query(i,a->profile->kind==BM_AIR_JET?BM_ASK_AIR_CLEARANCE_JET:BM_ASK_AIR_CLEARANCE_HELI,xf+12,NULL);   // AltBotMod V21
    air_traffic_update(); if(a->profile->kind==BM_AIR_JET) air_traffic_fill(a->veh,&o);
    air_target(i,&o);
    // V20: gunships hold 60 m over a ground target (80)
    if(o.targetValid && a->profile->kind!=BM_AIR_TRANSPORT && a->control.phase!=BM_FLIGHT_TAKEOFF && a->control.phase!=BM_FLIGHT_RECOVER){memcpy(o.goal,o.target,12);if(!o.targetAir)o.goal[1]+=(a->profile->kind==BM_AIR_JET?g_enhanced.air.jetHeight:60);}
    o.pickup=a->pickup;
    o.landing=landing;
    air_area(i,&o);   // AltBotMod V29: the area a jet stays in
    if (!bm_air_step(&a->control,a->profile,&g_enhanced.air,&o,BOT_TICK_DT,&a->output)) {
        air_stop(i,"invalid flight observation - exits",1); return;
    }
    air_drop(i,xf+12);
    air_combat_log(i);
    if (g_enhanced.air.debug && (H->frame+i)%30==0)
        bot_log("air: bot=%d %s phase=%d tactic=%s pos=(%.1f,%.1f,%.1f) speed=%.1f vy=%.1f ground=%d/%.1f target=%p controls T/B/Y/P/R=%.2f/%.2f/%.2f/%.2f/%.2f fire=%d",
            i,a->profile->name,a->control.phase,bm_air_tactic_name(a->output.tactic),xf[12],xf[13],xf[14],speed,o.velocity[1],a->groundValid,a->groundY,
            a->targetVeh,a->output.throttle,a->output.brake,a->output.yaw,a->output.pitch,a->output.roll,a->output.fire);
}
static void air_apply(int i,void *in)
{
    if (!air_pilot(i)) return;
    BMAirOutput *o=&g_air[i].output;
    ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in,EIA_THROTTLE,o->throttle);
    ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in,EIA_BRAKE,o->brake);
    ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in,EIA_STRAFE,0);
    ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in,EIA_YAW,o->yaw);
    ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in,EIA_PITCH,o->pitch);
    ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in,EIA_SEAT_YAW,o->roll); // same native action 6: EIARoll
    int fire=o->fire;
    // V22 secondary weapon: weapon 2 is selected while the seeker locks (armSecondary) and for a salvo / launch, weapon 1
    // otherwise - each change pressed once (0.1 s). Fire on weapon 2 only for the launch itself, 0.4 s; no gun fire then.
    // (V20: select 2, fire, back to 1 within 0.6 s - a heat seeker never had the time to lock.)
    AirNativeState *as=&g_air[i];
    if(!as->slot)as->slot=1;
    int want=(o->armSecondary || as->secondaryAt) ? 2 : 1;
    if(want!=as->slot){as->slot=want;as->slotAt=H->frame?H->frame:1;as->slotPressUntil=H->frame+3;}
    ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in,EIA_SELECT_WEAPON2,as->slot==2 && H->frame<as->slotPressUntil ? 1 : 0);
    ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in,EIA_SELECT_WEAPON1,as->slot==1 && H->frame<as->slotPressUntil ? 1 : 0);
    if(as->slot==2){
        fire=0;
        // AltBotMod V25: the game's own lock for the aircraft's missiles (on_lock_update): wanted while the missile is
        // selected; a lock the game reports is fired at once.
        {
            float *axf=vehicle_xf(g_air[i].veh);
            // (V29: on its target - the lock is put in place after the missile's lock time)
            if(axf){float to[3]={axf[12]+axf[8]*100,axf[13]+axf[9]*100,axf[14]+axf[10]*100};AIR_LOCK_WANT_ENT(i,axf+12,to,15.0f,g_air[i].targetVeh);(void)to;}
            if(AIR_LOCK_STATE(i)==2 && H->frame-as->slotAt>=3){fire=1;if(!as->secondaryAt){as->secondaryAt=H->frame?H->frame:1;
                bot_log("air: %s - the game locks its missile on (%u native locks so far) - fires",H->bots[i]+OFF_PLAYER_ONLINE_NAME,(unsigned)AIR_LOCK_COUNT());}}
        }
        if(as->secondaryAt){
            unsigned start=as->secondaryAt>as->slotAt+3 ? as->secondaryAt : as->slotAt+3;
            fire=H->frame>=start && H->frame<start+12;
            if(H->frame>=start+15 || H->frame-as->secondaryAt>90)as->secondaryAt=0;
        }
    } else if(H->frame-as->slotAt<3) fire=0;   // (switching back)
    ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in,EIA_FIRE,fire ? 1 : 0);
    ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in,EIA_ZOOM,0);
    ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in,EIA_AIR_COUNTERMEASURE,
        g_enhanced.air.countermeasures && g_air[i].counterUntil && (int)(g_air[i].counterUntil-H->frame)>0 ? 1 : 0);
    if (fire) g_botFiredAt[i]=H->frame ? H->frame : 1;
    // Authoritative aim belongs to the aircraft pose. Infantry aim must not
    // rewrite this later, nor can turret code overwrite flight pitch/roll.
    float *xf=vehicle_xf(g_air[i].veh);
    if (bm_air_transform_valid(xf)) {
        *(int *)((char *)in+OFF_INPUT_AIM_VALID)=1;
        *(float *)((char *)in+OFF_INPUT_AIM_YAW)=-atan2f(xf[8],xf[10]);
        *(float *)((char *)in+OFF_INPUT_AIM_PITCH)=asinf(fmaxf(-1,fminf(1,xf[9])));
    }
}
static void air_status(void)
{
    bot_log("air: jets=%d helicopters=%d weapons=%d countermeasures=%d transport-drops=%d pilot-limit/team=%d; experimental, no live validation",
        g_enhanced.air.jets,g_enhanced.air.helicopters,g_enhanced.air.weapons,g_enhanced.air.countermeasures,g_enhanced.air.transportDrops,g_enhanced.air.pilotsPerTeam);
    bot_log("air: combat so far - %u dogfight gun runs, %u strike dives, %u secondary weapon shots, %u collision breaks",
        g_airStats[0],g_airStats[1],g_airStats[2],g_airStats[3]);
    for (int i=0;i<H->botCount;i++) if (g_air[i].phase)
        bot_log("air: bot=%d %s role=%s state=%d flight=%d aircraft=%s target=%p speed=%.1f",
            i,(char *)H->bots[i]+OFF_PLAYER_ONLINE_NAME,g_air[i].pilot ? "pilot" : "crew",g_air[i].phase,g_air[i].control.phase,
            g_air[i].profile->name,g_air[i].targetVeh,hypotf(g_air[i].motion.velocity[0],g_air[i].motion.velocity[2]));
}
