// Transport delivery uses surveyed dry pads and existing native seat calls.
// Only bot cargo leaves; human passengers, pilots and gunners stay aboard.
static int air_bot_cargo(AirNativeState *a)
{
    char **entries=NULL;int count=vehicle_seats(a->veh,&entries),cargo=0;
    for(int k=a->profile->firstPassenger;k<count;k++)if(bot_index(air_seat_player(entries[k]))>=0)cargo++;
    return cargo;
}
static int air_landing_threat(int i,const float *pad)
{
    int team=player_team(H->bots[i]);char **vehicles;int count=vehicle_list(&vehicles);
    for(int k=0;k<count;k++){
        float *position=NULL;int air=0;
        if(air_enemy(vehicles[k],team,&position,&air) && fabsf(position[1]-pad[1])<80 && dist2(position,pad)<150*150)return 1;
    }
    char *manager=player_manager();void **players=NULL;unsigned n=0;
    if(manager){memcpy(&players,manager+0xac,sizeof players);memcpy(&n,manager+8,sizeof n);}
    for(unsigned k=0;players && k<n && k<64;k++){
        float *position=air_infantry(players[k],team);
        if(position && fabsf(position[1]-pad[1])<15 && dist2(position,pad)<100*100)return 1;
    }
    return 0;
}
static int air_landing_pad(int i,const float *wanted,float *out)
{
    if(!wanted || !isfinite(wanted[0]) || !isfinite(wanted[1]) || !isfinite(wanted[2]))return 0;
    float height;
    if(!air_pickup_ground(wanted,&height))return 0;
    NavGrid *grid=*nav_slot();
    if(!grid || grid->phase!=2 || nav_is_water(grid,wanted[0],height,wanted[2]))return 0;
    float snap[3];
    if(!nav_snap(grid,wanted[0],height,wanted[2],snap) || fabsf(snap[1]-height)>1 || dist2(snap,wanted)>4*4)return 0;
    // A rotor needs more than a walkable cell. Check a 16 m square and its
    // climb column with detail meshes, ignoring this aircraft's own body.
    unsigned saved=g_rayFlags;g_rayFlags=RAY_DONT_CHECK_CHARACTER|RAY_CHECK_DETAIL_MESH;
    int valid=1;
    for(int k=0;k<4 && valid;k++){
        float angle=k*1.5707963268f,next=(k+1)*1.5707963268f;
        for(int h=0;h<2 && valid;h++){
            float from[3]={wanted[0]+sinf(angle)*8,height+(h?4:1),wanted[2]+cosf(angle)*8};
            float to[3]={wanted[0]+sinf(next)*8,from[1],wanted[2]+cosf(next)*8};
            valid=tactic_ray(i,from,to);
        }
    }
    float from[3]={wanted[0],height+1,wanted[2]},to[3]={wanted[0],height+45,wanted[2]};
    if(valid)valid=tactic_ray(i,from,to);
    g_rayFlags=saved;
    if(!valid || air_landing_threat(i,wanted))return 0;
    out[0]=wanted[0];out[1]=height;out[2]=wanted[2];return 1;
}
static void air_landing_unload(int i)
{
    AirNativeState *a=&g_air[i];
    if(a->dropAt && H->frame-a->dropAt<15)return;
    char **entries=NULL;int count=vehicle_seats(a->veh,&entries);
    for(int k=a->profile->firstPassenger;k<count;k++){
        int j=bot_index(air_seat_player(entries[k]));
        if(j<0 || j==i)continue;
        if(!a->dropAt || H->frame-a->dropAt>30*30)bm_learn(i,BM_LEARN_PAD_LANDED,a->landingPad,NULL);   // AltBotMod V21: a good spot
        if(g_air[j].phase)air_stop(j,"transport landed; leaves cargo seat on foot",0);
        bot_exit_vehicle(j,0);end_task(j,bot_brain(j),"transport delivery");
        choose_local_objective(j,"transport delivered at a clear landing pad");
        diagnostic_note(j,"transport landed; cargo disembarked on foot",a->veh);
        a->dropAt=H->frame;return;
    }
}
static int air_landing_tick(int i,const float *position)
{
    AirNativeState *a=&g_air[i];
    if(a->profile->kind!=BM_AIR_TRANSPORT || !g_enhanced.air.transportDrops)return 0;
    if(!g_enhanced.air.landings){memset(&a->landing,0,sizeof a->landing);return 0;}
    if(a->control.phase==BM_FLIGHT_TAKEOFF)return 0;
    if(a->landing.phase==BM_LAND_NONE){
        if(a->control.phase!=BM_FLIGHT_CRUISE || H->frame<a->landingRetryAt ||
           (!a->pickup && (!air_bot_cargo(a) || a->dropping)))return 0;
        const float *center=NULL;
        if(a->pickup && a->pickupPlayer && player_exists(a->pickupPlayer)){
            char *soldier=*(char **)(a->pickupPlayer+OFF_PLAYER_SOLDIER);
            if(soldier)center=(float *)(soldier+OFF_SOLDIER_POSITION);
        }else if(a->flag>=0 && a->flag<objective_count())center=objective_pos(a->flag);
        if(!center || dist2(position,center)>300*300)return 0;
        if(g_airLandingProbeFrame==H->frame)return 0; // one pad candidate for all pilots per frame
        g_airLandingProbeFrame=H->frame;
        float wanted[3];memcpy(wanted,center,12);
        // AltBotMod V21: first the landing spots learned on this map (where players and transports set down before),
        // the closest to the objective first; then the surveyed ring around it
        int learnedPad=a->landingLearnedTry<3 && bm_learn_pad_near(center,a->pickup?40.0f:220.0f,a->landingLearnedTry,wanted);
        if(!learnedPad && a->landingCandidate){
            float angle=(a->landingCandidate-1)*2.0943951024f;
            wanted[0]+=sinf(angle)*18;wanted[2]+=cosf(angle)*18;
        }
        if(!air_landing_pad(i,wanted,a->landingPad)){
            if(learnedPad){a->landingLearnedTry++;a->landingRetryAt=H->frame+3;if(!air_landing_threat(i,wanted))bm_learn(i,BM_LEARN_PAD_BLOCKED,wanted,NULL);return 0;}   // (enemies there: not the spot's fault)
            if(++a->landingCandidate>=4){a->landingCandidate=0;a->landingLearnedTry=0;a->landingRetryAt=H->frame+30*30;}
            else a->landingRetryAt=H->frame+3;
            return 0; // stock hover / parachute delivery remains the fallback
        }
        if(learnedPad)diagnostic_note(i,"transport heads for a learned landing spot",a->veh);
        a->landingCandidate=0;a->landingLearnedTry=0;a->landingPadValid=1;a->landingThreatAt=0;
        a->landing.phase=BM_LAND_APPROACH;a->landing.elapsed=a->landing.settled=0;
        diagnostic_note(i,a->pickup?"transport approaches a clear pickup pad":"transport approaches a clear delivery pad",a->veh);
    }
    if(!a->landingThreatAt || H->frame-a->landingThreatAt>=15){
        a->landingThreatAt=H->frame?H->frame:1;
        a->landingThreat=air_landing_threat(i,a->landingPad);
    }
    BMLandingObservation observation={0};
    observation.distance=sqrtf(dist2(position,a->landingPad));
    observation.clearance=position[1]-a->landingPad[1];
    observation.horizontalSpeed=hypotf(a->motion.velocity[0],a->motion.velocity[2]);
    observation.verticalSpeed=a->motion.velocity[1];
    observation.padValid=a->landingPadValid;
    observation.threatened=a->landingThreat || a->control.phase==BM_FLIGHT_RECOVER;
    observation.cargo=a->pickup?air_cargo(a):air_bot_cargo(a);observation.pickup=a->pickup;
    enum BMLandingPhase before=a->landing.phase;
    BMLandingDecision decision=bm_transport_landing(&a->landing,&observation,BOT_TICK_DT);
    memcpy(a->goal,a->landingPad,12);a->goal[1]+=decision.height;
    if(decision.abort){a->landingRetryAt=H->frame+30*30;diagnostic_note(i,"transport landing aborted; climbs clear",a->veh);}
    if(decision.unload)air_landing_unload(i);
    if(decision.board)air_invite(i);
    if(before!=a->landing.phase && a->landing.phase==BM_LAND_ASCEND)
        diagnostic_note(i,"transport lifts off; pilot and gunners retained",a->veh);
    if(decision.finished){
        a->landingRetryAt=H->frame+15*30;a->landingPadValid=0;
        if(!a->pickup && !air_bot_cargo(a))a->dropping=1;
        a->pickup=0;a->pickupPlayer=NULL;
        diagnostic_note(i,"transport climb complete; resumes transport duty",a->veh);
        return 0;
    }
    return 1;
}
