// Healthy ground vehicles remain useful after one trip. Cargo can dismount
// near an objective while the driver and gunner retain the vehicle.
static void drive_wait_route(int i,DriveState *d,const char *why)
{
    // A routing/cache failure is not a reason to order a healthy crew out.
    d->phase=10;d->routeRetryAt=H->frame+10*30;d->routeFailures=0;
    nav_cancel_path(i+MAX_BOTS);bot_nav(i)->mode=0;
    memset(H->botMove[i],0,sizeof H->botMove[i]);
    bot_log("drive: %s retains %s and retries routing (%s)",H->bots[i]+OFF_PLAYER_ONLINE_NAME,vehicle_name(d->veh),why);
}
static int drive_other_objective(int i,DriveState *d,const float *vp,int failed)
{
    if(failed && d->flag>=0 && d->flag<MAX_FLAGS)d->failedFlagUntil[d->flag]=H->frame+20*30;
    if(failed && ++d->routeFailures>objective_count()+1)return 0;
    int team=player_team(H->bots[i]),best=-1;
    int hostile=0;for(int f=0;f<objective_count();f++)hostile+=objective_wanted(team,f);
    int patrol=mode_is_cq() && !hostile;
    int dutyRole,preferred=mode_is_cq() && !failed?director_target(i,&dutyRole):-1;
    // AltBotMod V7: a director assignment to a flag we already hold (stale for a tick after the capture) is no reason
    // to stay - the vehicle moves on to the next one.
    if(preferred>=0 && hostile && !objective_wanted(team,preferred))preferred=-1;
    // A second allocator must not undo the director's committed patrol trip.
    if(preferred==d->flag && preferred>=0)return 0;
    float score=INFINITY;
    for(int f=0;f<objective_count();f++){
        if(preferred>=0 && f!=preferred)continue;
        if(f==d->flag || (f<MAX_FLAGS && H->frame<d->failedFlagUntil[f]) || (!patrol && !objective_wanted(team,f)))continue;
        BotBrain *b=bot_brain(i);
        if(b->avoidUntil>H->frame && b->avoidFlag==f)continue;
        int vehicles=0;
        for(int j=0;j<H->botCount;j++)if(j!=i && player_team(H->bots[j])==team &&
            g_drive[j].phase && g_drive[j].phase!=5 && g_drive[j].phase!=7 &&
            g_drive[j].phase!=9 && g_drive[j].flag==f)vehicles++;
        float value=sqrtf(dist2(vp,objective_pos(f)))+vehicles*1500.0f+flag_pressure(team,f,i)*150.0f;
        if(value<score){score=value;best=f;}
    }
    if(best<0)return 0;
    if(failed){bot_brain(i)->avoidFlag=d->flag;bot_brain(i)->avoidUntil=H->frame+30*30;}
    d->flag=best;memcpy(d->goal,objective_pos(best),12);
    d->traceTries=0;d->traceAlt=0;   // AltBotMod V14: a new flag - its Fun-Bots vehicle routes are tried first again
    d->phase=2;d->why="next objective";d->at=H->frame;d->jobAt=0;d->n=d->next=d->partial=0;
    d->stageCandidate=d->stageActive=0;
    nav_cancel_path(i+MAX_BOTS);
    bot_brain(i)->targetFlag=best;bot_brain(i)->waiting=0;bot_brain(i)->enroute=1;
    memset(H->botMove[i],0,sizeof H->botMove[i]);
    bot_log("drive: %s keeps %s for the next objective, flag %d%s",H->bots[i]+OFF_PLAYER_ONLINE_NAME,
        vehicle_name(d->veh),best,failed?" (previous objective unreachable)":"");
    return 1;
}
static void drive_arrive_objective(int i,DriveState *d,const float *vp)
{
    char **entries=NULL;
    int seats=vehicle_seats(d->veh,&entries),passengers=seats-free_seat_count(d->veh)-1;
    if(!bm_hold_vehicle(vehicle_name(d->veh),passengers)){
        drive_stop(i,"transport delivered it - captures on foot",30);return;
    }
    d->phase=4;d->jobAt=H->frame?H->frame:1;d->routeFailures=0;
    bot_nav(i)->mode=0;memset(H->botMove[i],0,sizeof H->botMove[i]);
    bot_brain(i)->targetFlag=d->flag;bot_brain(i)->enroute=0;bot_brain(i)->waiting=1;
    // Keep the gunner and humans seated. Only ordinary bot cargo dismounts.
    for(int j=0;j<H->botCount;j++)if(j!=i && player_vehicle(H->bots[j])==d->veh &&
        vehicle_seat_of(d->veh,H->bots[j])>1 && bot_brain(j)->task==TASK_NONE){
        bot_exit_vehicle(j,0);memset(&g_drive[j],0,sizeof g_drive[j]);g_driving[j]=0;
        gunner_reset(j);choose_local_objective(j,"vehicle delivered it to an objective");
    }
    if(!bm_hold_vehicle(vehicle_name(d->veh),seats-free_seat_count(d->veh)-1)){
        drive_stop(i,"transport crew dismounted - captures on foot",30);return;
    }
    bot_log("drive: %s supports flag %d from %s (%.0f m); retains driver/gunner",H->bots[i]+OFF_PLAYER_ONLINE_NAME,
        d->flag,vehicle_name(d->veh),sqrtf(dist2(vp,d->goal)));
}
static int drive_support_reposition(int i,DriveState *d,const float *position)
{
    if(d->flag<0 || d->flag>=objective_count() || !objective_pos(d->flag))return 0;
    NavGrid *grid=*nav_slot();const unsigned char *mask=drive_mask();if(!grid || !mask)return 0;
    const float *center=objective_pos(d->flag);float point[3];
    for(int k=0;k<8;k++){
        float angle=(i%8)*0.785398f+(d->supportStep+k)*1.5707963f;
        // AltBotMod V16: a flag not yet ours is held from inside its capture zone (V9-V15: 28 / 48 m out - a tank
        // with no infantry beside it sat outside the zone and the flag never turned: "flags ignored" on big maps)
        int ours=mode_is_cq() && flag_owner(d->flag)==player_team(H->bots[i]);
        float radius=ours?(k<4?28.0f:48.0f):(k<4?8.0f:14.0f);
        if(!nav_drive_near(grid,mask,center[0]+sinf(angle)*radius,center[1],center[2]+cosf(angle)*radius,12,point) ||
           dist2(position,point)<10*10)continue;
        d->supportStep+=k+1;memcpy(d->goal,point,12);d->phase=2;d->why="support position";d->jobAt=0;
        d->n=d->next=d->partial=0;d->stageActive=d->stageCandidate=0;d->at=H->frame;
        nav_cancel_path(i+MAX_BOTS);bot_brain(i)->waiting=0;bot_brain(i)->enroute=1;
        diagnostic_note(i,"driver changes support/patrol position at remaining objective",d->veh);return 1;
    }
    return 0;
}
// AltBotMod V9: a friendly vehicle has been waiting behind this one for 4 s (traffic queue / narrow stretch)
static int vehicle_queue_behind(int i,char *veh)
{
    for(int j=0;j<H->botCount;j++)if(j!=i && g_drive[j].phase==3 && g_drive[j].trafficWait && g_drive[j].trafficPeer==veh &&
        g_drive[j].trafficSince && H->frame-g_drive[j].trafficSince>4*30)return 1;
    return 0;
}
static int g_zoneHoldFlag[MAX_BOTS];static unsigned g_zoneHoldSince[MAX_BOTS];   // AltBotMod V16: flag being worked from its zone (+1)
static void drive_hold_objective(int i,DriveState *d,const float *vp)
{
    memset(H->botMove[i],0,sizeof H->botMove[i]);
    VehTrack *t=vehicle_sample(d->veh);
    if(t && t->maxSeen>0 && *(float *)(d->veh+OFF_VEHICLE_HEALTH)<g_enhanced.bailHealth*t->maxSeen){
        drive_stop(i,"bails out - the vehicle is nearly wrecked",20);return;
    }
    // V9: holding in the way of a queue (a tank parked on the bridge): move on - another support spot or the next flag
    if(vehicle_queue_behind(i,d->veh)){
        diagnostic_note(i,"parked in the way of a friendly queue; moves on",d->veh);
        if(drive_support_reposition(i,d,vp) || drive_other_objective(i,d,vp,0))return;
    }
    if(d->flag<0 || d->flag>=objective_count()){
        if(!drive_other_objective(i,d,vp,0))drive_wait_route(i,d,"objective inventory updating");
        return;
    }
    // Captured flags release drivers immediately. If only one objective is
    // available, patrol/reposition there rather than waiting for a nonexistent
    // different flag. An enemy support stop is also bounded to eight seconds.
    if(!objective_wanted(player_team(H->bots[i]),d->flag)){
        if(drive_other_objective(i,d,vp,0))return;   // AltBotMod V7: taken - straight on to the next flag
        if(H->frame-d->jobAt<3*30)return;
    }else{
        if(H->frame-d->jobAt<8*30)return;
        // AltBotMod V16: an enemy / neutral flag is worked from inside its capture zone - moving between spots 8-14 m from
        // its centre - until it turns, for up to 75 s from the first arrival. V9-V15 left after 8 s, long before a capture
        // finishes, and drove to another flag: big-map flags half taken, then "ignored".
        if(g_zoneHoldFlag[i]!=d->flag+1){g_zoneHoldFlag[i]=d->flag+1;g_zoneHoldSince[i]=H->frame?H->frame:1;}
        if(mode_is_cq() && H->frame-g_zoneHoldSince[i]<75*30){
            if(drive_support_reposition(i,d,vp))return;
            if(H->frame-d->jobAt<20*30)return;   // no other spot inside the zone: stay put a while
        }
    }
    if(drive_support_reposition(i,d,vp))return;
    if(drive_other_objective(i,d,vp,0))return;
    drive_wait_route(i,d,"support exit blocked; retry a checked approach");
}

// Obstruction learning belongs to this driver, not every team's street mask.
static int drive_block_for_job(int i,unsigned char *mask,float x,float z,float radius,float y)
{
    DriveState *d=&g_drive[i];
    if(d->manual || vehicle_is_boat(d->veh))return drive_strike(mask,x,z,radius);
    d->avoidPosition[0]=x;d->avoidPosition[1]=y;d->avoidPosition[2]=z;
    d->avoidRadius=fminf(radius,2.25f);d->avoidUntil=H->frame+30*30;
    nav_cancel_path(i+MAX_BOTS);return 1;
}
static int drive_recover_job(int i,DriveState *d,const float *position)
{
    if(d->manual || vehicle_is_boat(d->veh))return 0;
    d->reroutes=d->stuckCount=d->nudges=0;d->stageActive=d->stageCandidate=0;
    d->reverseUntil=d->backOutUntil=0;d->awayOn=0;
    nav_cancel_path(i+MAX_BOTS);
    if(!drive_other_objective(i,d,position,1))drive_wait_route(i,d,"blocked street; retains crew and retries checked routes");
    diagnostic_note(i,"stuck ground vehicle retained; tries another objective/route without banning teammates",d->veh);
    return 1;
}

