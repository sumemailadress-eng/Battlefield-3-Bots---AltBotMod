// Independent of traffic/crew/routing timers: none of those can refresh a
// motion watchdog without ACTUAL movement. Recover a stalled automatic driver.
static int drive_progress_tick(int i,DriveState *d,const float *position)
{
    if(d->manual || (d->phase!=2 && d->phase!=3 && d->phase!=8 && d->phase!=10))return 0;
    if(!d->progressAt || dist2(position,d->progressPosition)>3*3){
        memcpy(d->progressPosition,position,12);d->progressAt=H->frame?H->frame:1;return 0;
    }
    if(H->frame-d->progressAt<15*30)return 0;
    if(!tactic_probe_budget())return 0;
    bm_report_stuck(i,position,"vehicle stalled");   // AltBotMod V13: the live map
    int boat=vehicle_is_boat(d->veh);
    {float way[2]={d->goal[0]-position[0],d->goal[2]-position[2]};bm_learn(i,boat?BM_LEARN_STUCK_BOAT:BM_LEARN_STUCK_GROUND,position,way);}   // AltBotMod V21
    NavGrid *grid=*nav_slot();const unsigned char *mask=boat?boat_mask():drive_mask();
    if(!grid || !mask){
        float exit[3];
        if(!boat && drive_live_exit(i,d,position,exit)){
            nav_cancel_path(i+MAX_BOTS);memcpy(d->pts,exit,12);d->n=1;d->next=0;d->partial=1;d->phase=3;
            d->at=d->checkAt=H->frame;d->progressAt=H->frame;memcpy(d->lastPos,position,12);
            diagnostic_note(i,"missing driving cache: takes a live checked hull-width exit",d->veh);return 1;
        }
        return 0;
    }
    nav_cancel_path(i+MAX_BOTS);
    nav_set_path_owner(i+MAX_BOTS);nav_set_path_mask(mask);
    nav_set_vehicle_width(bm_vehicle_profile(vehicle_name(d->veh))->halfWidth+0.15f);
    nav_set_vehicle_avoid(NULL,0);
    float heading=atan2f(d->goal[0]-position[0],d->goal[2]-position[2]),point[3];
    static const float angles[]={0,0.7f,-0.7f,1.4f,-1.4f,2.4f,-2.4f,3.14159265f};
    int found=0;
    for(int k=0;k<8 && !found;k++){
        float x=position[0]+sinf(heading+angles[k])*10,z=position[2]+cosf(heading+angles[k])*10;
        if(!nav_drive_near(grid,mask,x,position[1],z,2,point) || fabsf(point[1]-position[1])>3 || dist2(position,point)<4*4 ||
           (boat && !nav_is_water(grid,point[0],point[1],point[2])))continue;
        found=nav_direct_route(grid,drive_nav_ray,position,point);
    }
    nav_set_path_mask(NULL);nav_set_vehicle_width(1.5f);nav_set_path_owner(-1);
    if(!found && !boat)found=drive_live_exit(i,d,position,point);
    d->progressAt=H->frame;memcpy(d->progressPosition,position,12);
    d->pickupUntil=0;d->pickupAskAt=H->frame;d->trafficSince=0;d->trafficWait=0;
    d->trafficLeaseBackoff=H->frame+5*30;d->routeRetryAt=0;d->routeFailures=0;
    d->reverseUntil=d->backOutUntil=0;d->reroutes=d->stuckCount=d->nudges=0;
    d->stageActive=d->stageCandidate=0;
    if(found){
        memcpy(d->pts,point,12);d->n=1;d->next=0;d->partial=1;d->phase=3;
        d->at=d->checkAt=H->frame;memcpy(d->lastPos,position,12);
        diagnostic_note(i,"15 s without motion: drives a checked clear exit then resumes enemy objective",d->veh);
    }else{
        d->phase=2;d->why="15 s still - no clear exit";d->at=H->frame;memset(d->failedFlagUntil,0,sizeof d->failedFlagUntil);
        diagnostic_note(i,"15 s without motion: blocked exits; requests another checked route",d->veh);
    }
    return found;
}

// AltBotMod V17: a driver that has not got 35 m away from where it stalled in 35 s is stuck, however much it backs and
// fills on the spot (Alborz 2026-10-14: a Growler nosed into the same parked vehicles at the US base 132 times in 202 s;
// every back-off moved it a few metres, which reset the motion watches, and every re-plan took the same recorded road).
// Not while it is at its objective, fighting from the hull, picking up or waiting for an engineer. Escalates:
//   1. backs straight out, strikes a 7 m patch ahead off the driving map and takes another recorded road;
//   2. another objective, on the drivable grid;
//   3. gives the vehicle up (no bot takes it for 2 min) and goes on foot.
static int drive_goal_watch(int i,DriveState *d,const float *vp)
{
    if(d->manual || vehicle_is_boat(d->veh) || (d->phase!=2 && d->phase!=3) || !vp){d->watchAt=0;return 0;}
    int busy=dist2(vp,d->goal)<40*40 || d->combatHoldAt || (d->repairHoldAt && H->frame-d->repairHoldAt<20*30) || H->frame<d->pickupUntil;
    if(!d->watchAt || busy || dist2(vp,d->watchAnchor)>35*35){
        if(!busy && d->watchAt && dist2(vp,d->watchAnchor)>35*35)d->watchLevel=0;   // real progress
        memcpy(d->watchAnchor,vp,12);d->watchAt=H->frame?H->frame:1;return 0;
    }
    if(H->frame-d->watchAt<35*30)return 0;
    d->watchAt=H->frame?H->frame:1;d->watchLevel++;
    {float way[2]={d->goal[0]-vp[0],d->goal[2]-vp[2]};bm_learn(i,BM_LEARN_STUCK_GROUND,vp,way);}   // AltBotMod V21: learning
    char *pl=H->bots[i];
    const float *ahead=d->n>0 && d->next<d->n?&d->pts[d->next*3]:d->goal;
    float ax=ahead[0]-vp[0],az=ahead[2]-vp[2],al=sqrtf(ax*ax+az*az)+1e-3f;
    if(d->watchLevel==1){
        unsigned char *sm=g_driveMask;int k=0;
        if(sm)k=drive_block_for_job(i,sm,vp[0]+ax/al*7,vp[2]+az/al*7,7.0f,vp[1]);
        d->traceTries=d->traceTries<2?2:d->traceTries+1;d->traceContinue=0;d->traceAlt++;
        d->reroutes=d->stuckCount=d->nudges=0;d->reverseUntil=0;d->backOutUntil=H->frame+75;
        d->phase=2;d->why="35 s without 35 m (level 1)";d->n=d->next=d->partial=0;nav_cancel_path(i+MAX_BOTS);
        bot_log("drive: %s has not got 35 m on in 35 s at (%.0f, %.0f) - backs straight out, strikes the way ahead off (%d node(s)), takes another road",
                pl+OFF_PLAYER_ONLINE_NAME,vp[0],vp[2],k);
        return 0;
    }
    if(d->watchLevel==2){
        d->traceTries+=2;d->traceAlt++;d->backOutUntil=H->frame+75;   // (V25: another recorded road - V17-V24 set 99: no Fun-Bots road for the rest of the trip)
        bot_log("drive: %s is still stuck at (%.0f, %.0f) - another objective, on the driving map",pl+OFF_PLAYER_ONLINE_NAME,vp[0],vp[2]);
        if(drive_recover_job(i,d,vp))return 1;
        d->phase=2;d->why="35 s without 35 m (level 2)";return 0;
    }
    char *v=d->veh;
    bot_log("drive: %s gives the %s up at (%.0f, %.0f) after 105 s without getting anywhere - on foot",pl+OFF_PLAYER_ONLINE_NAME,vehicle_name(v),vp[0],vp[2]);
    vehicle_mark_bad(v,120);d->watchAt=0;d->watchLevel=0;
    drive_stop(i,"the vehicle was stuck - goes on foot",45);
    return 1;
}
