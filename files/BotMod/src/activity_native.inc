// Actual displacement owns these deadlines. An AI job restarting does not.
// One geometric candidate per frame, no synchronous full-map search here.
static void activity_end_task(int i)
{
    BotBrain *b=bot_brain(i);
    if(b->task==TASK_FIGHT)end_fight(i,b,"stationary fight; moves to a checked flank/objective");
    else if(b->task==TASK_REVIVE)end_revive(i,b,"stationary revive; releases unreachable casualty");
    else if(b->task==TASK_REPAIR)end_repair(i,b,"stationary service; releases unreachable vehicle");
    else if(b->task==TASK_COVER)end_cover(i,b,"cover stop ended; resumes moving");
    else if(b->task==TASK_ROCKET)end_rocket(i,b,"stationary rocket job; resumes objectives");
    else if(b->task!=TASK_NONE)end_task(i,b,"stationary service; resumes objectives");
    b->prevTask=TASK_NONE;b->prevCommander=NULL;
}
static void activity_replan(int i)
{
    nav_cancel_path(i);memset(&g_flagRoute[i],0,sizeof g_flagRoute[i]);
    memset(&g_vehicleApproach[i],0,sizeof g_vehicleApproach[i]);
    g_paths[i].count=g_paths[i].next=g_paths[i].partial=0;
    combat_motion_cancel(i);
    BotBrain *b=bot_brain(i);
    // Caspian Test16: remember the objective it stalled on (an active order's
    // flag, the director's assignment or the brain's target) and avoid it.
    int role,stuck=g_localFlag[i]-1;
    if(stuck<0)stuck=director_target(i,&role);
    if(stuck<0)stuck=b->targetFlag;
    if(stuck>=objective_count())stuck=-1;
    // AltBotMod V6: only a flag it got stuck NEAR is avoided. A bot held up at its HQ (stairs, a building) used to
    // strike off the flag it was heading for - on Kharg 299 such recoveries in 4 minutes left neutral flags unattacked.
    {   char *s=*(char **)(H->bots[i]+OFF_PLAYER_SOLDIER);
        if(stuck>=0 && s && objective_pos(stuck) && dist2((float *)(s+OFF_SOLDIER_POSITION),objective_pos(stuck))>150*150)stuck=-1; }
    b->targetFlag=-1;b->waiting=b->enroute=0;
    b->retryAt=b->replanAt=b->stepAt=0;b->avoidUntil=0;b->avoidFlag=-1;
    b->crouched=0;bot_stand(i);bot_nav(i)->mode=0;
    g_noRouteSince[i]=0;g_directorAt=0;
    choose_local_objective_except(i,"activity recovery: continues an objective or patrol",stuck);
}
static int activity_step(int i,char *soldier)
{
    ActivityState *w=&g_activity[i];
    if(g_activityProbeFrame==H->frame)return 0;
    g_activityProbeFrame=H->frame;
    const float *p=(float *)(soldier+OFF_SOLDIER_POSITION);
    int role,f=director_target(i,&role);
    if(f<0 || f>=objective_count())f=bot_brain(i)->targetFlag;
    const float *target=f>=0 && f<objective_count()?objective_pos(f):NULL;
    float heading=target?atan2f(target[0]-p[0],target[2]-p[2]):i*0.785398f;
    static const float offsets[]={0,0.7f,-0.7f,1.4f,-1.4f,2.4f,-2.4f,3.14159265f};
    int candidate=w->candidate++%8;
    float angle=heading+offsets[candidate],point[3]={p[0]+sinf(angle)*8,p[1],p[2]+cosf(angle)*8};
    NavGrid *grid=*nav_slot();
    if(grid && grid->phase==2){
        float snapped[3];
        if(!nav_snap(grid,point[0],p[1],point[2],snapped) || fabsf(snapped[1]-p[1])>2.5f ||
           dist2(p,snapped)<3*3 || nav_is_water(grid,snapped[0],snapped[1],snapped[2]))return 0;
        memcpy(point,snapped,12);
    }
    unsigned saved=g_rayFlags;g_rayFlags=RAY_DONT_CHECK_CHARACTER|RAY_CHECK_DETAIL_MESH;
    nav_set_path_mask(NULL);nav_set_path_owner(i);
    int clear=nav_segment_clear(ray_cast,p,point);
    // Live floor samples also cover unfinished/missing grid data. Never step
    // blindly over a drop, into water, or onto a different bridge level.
    float previous=p[1],hit[3];
    for(int k=1;k<=4 && clear;k++){
        float t=k/4.0f,from[3]={p[0]+(point[0]-p[0])*t,previous+2, p[2]+(point[2]-p[2])*t};
        float to[3]={from[0],previous-2.5f,from[2]};
        if(!ray_cast(from,to,hit,NULL) || nav_hit_class()==NAV_HIT_WATER || fabsf(hit[1]-previous)>1.5f)clear=0;
        else previous=hit[1];
    }
    nav_set_path_owner(-1);g_rayFlags=saved;
    if(!clear)return 0;
    point[1]=previous;
    nav_cancel_path(i);memset(&g_flagRoute[i],0,sizeof g_flagRoute[i]);
    BotPath *path=&g_paths[i];memset(path,0,sizeof *path);
    memcpy(path->pts,point,12);path->link[0]=-1;path->count=1;
    path->goalX=point[0];path->goalY=point[1];path->goalZ=point[2];
    memcpy(w->escape,point,12);w->escapeUntil=H->frame+8*30;
    bot_nav(i)->mode=3;bot_brain(i)->waiting=0;bot_brain(i)->enroute=0;bot_stand(i);
    diagnostic_note(i,"activity recovery takes a live checked exit/flank",NULL);
    return 1;
}
static int activity_grid_tick(int i,char *soldier)
{
    ActivityState *w=&g_activity[i];
    if(w->escapeUntil && H->frame<w->escapeUntil &&
       dist2((float *)(soldier+OFF_SOLDIER_POSITION),w->escape)>1.5f*1.5f){bot_nav(i)->mode=3;return 1;}
    w->escapeUntil=0;return 0;
}
static void activity_release_vehicle(int i,char *vehicle,const float *position)
{
    // Scoped to this team and location; another approach remains possible.
    fleet_route_failed(vehicle,player_team(H->bots[i]),position,90);
    if(g_air[i].phase)air_stop(i,"flight/boarding made no progress; returns to objectives",1);
    else if(g_drive[i].phase)drive_stop(i,"vehicle made no progress after recovery; advances on foot",45);
    else if(player_vehicle(H->bots[i]))bot_exit_vehicle(i,1);
    nav_cancel_path(i+MAX_BOTS);memset(&g_fleet[i],0,sizeof g_fleet[i]);
    g_fleet[i].failedVehicle=vehicle;g_fleet[i].failedUntil=H->frame+90*30;g_fleet[i].retryAt=H->frame+45*30;
    g_driveCooldown[i]=g_airCooldown[i]=H->frame+45*30;
    activity_replan(i);
    // AltBotMod V8: one release per stall, not one a second while the crew is still climbing out (V7 looped 668 times)
    g_activity[i].progressAt=g_activity[i].travelAt=H->frame?H->frame:1;
}
#define ACTIVITY_REPLAN_FRAMES (30*30)
static int activity_circling(int i,ActivityState *w,BotBrain *b,const float *p)
{
    if(w->trailN<12)w->trailN++;
    memmove(w->trail[1],w->trail[0],sizeof w->trail-sizeof w->trail[0]);
    w->trail[0][0]=p[0];w->trail[0][1]=p[2];
    if(w->trailN<12 || bot_nav(i)->mode!=3 || b->task==TASK_FIGHT || sniper_holding(i) || class_job_working(i,p) ||
       (w->circleAt && H->frame-w->circleAt<15*30))return 0;
    if(b->waiting && b->targetFlag>=0 && b->targetFlag<objective_count() && dist2(p,objective_pos(b->targetFlag))<25*25)return 0;
    float cx=0,cz=0,walked=0,reach=0;
    for(int k=0;k<12;k++){cx+=w->trail[k][0];cz+=w->trail[k][1];}
    cx/=12;cz/=12;
    for(int k=0;k<12;k++){
        float dx=w->trail[k][0]-cx,dz=w->trail[k][1]-cz;reach=fmaxf(reach,sqrtf(dx*dx+dz*dz));
        if(k)walked+=sqrtf((w->trail[k][0]-w->trail[k-1][0])*(w->trail[k][0]-w->trail[k-1][0])+(w->trail[k][1]-w->trail[k-1][1])*(w->trail[k][1]-w->trail[k-1][1]));
    }
    if(walked<20 || reach>5)return 0;
    // a crowd (a stairwell, a doorway full of teammates) is traffic, not a bad link: no blocking there - a link blocked in
    // a stairwell cut the only way down and bots were left in the room (Office 14:38)
    int crowd=0;
    for(int j=0;j<H->botCount;j++){char *os=j!=i?*(char **)(H->bots[j]+OFF_PLAYER_SOLDIER):NULL;if(os && dist2((float *)(os+OFF_SOLDIER_POSITION),p)<4*4)crowd++;}
    w->circleAt=H->frame?H->frame:1;w->trailN=0;
    if(crowd>=2){w->progressAt=H->frame;nav_cancel_path(i);bot_nav(i)->mode=0;return 1;}   // (re-plans; the queue moves on)
    w->circles++;
    int blocked=*nav_slot()?nav_block_towards(*nav_slot(),p[0],p[1],p[2],p[0]+(p[0]-cx)*2,p[2]+(p[2]-cz)*2):0;   // (the way it keeps taking)
    bot_log("nav: %s runs in circles at (%.1f, %.1f) - %.0f m walked within %.1f m in 12 s; blocks %d link(s) and takes a new way%s",
            H->bots[i]+OFF_PLAYER_ONLINE_NAME,cx,cz,walked,reach,blocked,w->circles>1?" (and a new objective)":"");
    nav_cancel_path(i);bot_nav(i)->mode=0;
    if(w->circles>1){activity_end_task(i);activity_replan(i);}
    w->progressAt=H->frame;
    return 1;
}
static void activity_tick(int i)
{
    if((H->frame+i)%30)return;
    ActivityState *w=&g_activity[i];BotBrain *b=bot_brain(i);char *pl=H->bots[i];
    if(!level_ready() || g_roundCleanup || !H->stepBots || !b->enabled || g_brainHeld[i] || g_silentRemove[i] ||
       (b->commander && !is_bot(b->commander)) || (b->prevCommander && !is_bot(b->prevCommander)) || g_drive[i].manual){memset(w,0,sizeof *w);return;}   // V15: only a player's order exempts (a tank's screen did too)
    char *s=*(char **)(pl+OFF_PLAYER_SOLDIER);
    if(!s || *(float *)(s+OFF_SOLDIER_HEALTH)<=0){
        if(!w->absentAt)w->absentAt=H->frame?H->frame:1;
        int team=player_team(pl);
        // Do not churn identities while the level's spawn assets are loading.
        if(team>=1 && team<=2 && H->teamBlueprint[team] && H->frame-w->absentAt>=90*30 &&
           (!g_activityRetireAt || H->frame-g_activityRetireAt>=3*30)){
            g_activityRetireAt=H->frame;retire_stuck_bot(i,"spawn produced no living soldier for 90 s; rebuilds player identity");
        }
        return;
    }
    char *v=player_vehicle(pl);const float *p=v?vehicle_pos(v,NULL):(float *)(s+OFF_SOLDIER_POSITION);
    if(!p || !isfinite(p[0]) || !isfinite(p[1]) || !isfinite(p[2]))return;
    if(g_gliding[i]){memset(w,0,sizeof *w);return;}
    if(w->soldier!=s || w->vehicle!=v || !w->progressAt){
        memset(w,0,sizeof *w);w->soldier=s;w->vehicle=v;
        memcpy(w->position,p,12);memcpy(w->travelAnchor,p,12);w->progressAt=w->travelAt=H->frame?H->frame:1;return;
    }
    w->absentAt=0;
    if(dist2(w->position,p)>2*2 || fabsf(w->position[1]-p[1])>3){
        memcpy(w->position,p,12);w->progressAt=H->frame;w->attempts=0;w->candidate=0;
    }
    if(dist2(w->travelAnchor,p)>12*12){memcpy(w->travelAnchor,p,12);w->travelAt=H->frame;}
    unsigned still=H->frame-w->progressAt,travel=H->frame-w->travelAt;
    if(v && stationary_weapon(v)){w->progressAt=w->travelAt=H->frame;return;}   // Caspian V5: emplacement_tick decides
    if(v){
        char **seats=NULL;int n=vehicle_seats(v,&seats);char *driver=n?air_seat_player(seats[0]):NULL;
        if(driver && !is_bot(driver)){w->progressAt=w->travelAt=H->frame;return;}
        if(g_air[i].pilot && g_air[i].phase==AIR_FLY){
            if(still>=15*30 && (!w->actionAt || H->frame-w->actionAt>=10*30)){
                w->actionAt=H->frame;g_air[i].pickupPlayer=NULL;g_air[i].pickup=0;
                g_air[i].pickupRetryAt=H->frame+30*30;
                bm_air_start(&g_air[i].control,g_air[i].profile->kind,1);
                g_air[i].control.phase=BM_FLIGHT_RECOVER;
                g_air[i].control.timer=6; // a real recovery window, not a zero-duration phase
                diagnostic_note(i,"stationary aircraft resets flight controller and resumes patrol",v);
            }
            if(still>=45*30 || travel>=90*30)activity_release_vehicle(i,v,p);
            return;
        }
        if(still>=45*30 || travel>=90*30){activity_release_vehicle(i,v,p);return;}
        // Ground, boat and orphaned cargo are covered regardless of route,
        // repair, boarding or traffic timers. Drivers get the 15 s live probe.
        return;
    }
    // AltBotMod V18: running in circles - 20 m or more walked in the last 12 s without ever getting more than 5 m from
    // the middle of it (a waypoint it keeps overshooting, a stair it slides off, two routes that undo each other). The
    // stuck watches above never see it (it is moving). Not while fighting, holding a perch, at work or capturing.
    if(activity_circling(i,w,b,p))return;
    // Caspian V3: a bot shooting at an enemy it can see is working, not stuck. In V2, 160 of 353 fights were
    // ended here ("stationary fight") just as the bot was firing, and the bot was sent off to another flag.
    // (Only up to 150 m, so a bot cannot spend the round at its HQ trading shots across the map.)
    if(b->task==TASK_FIGHT && g_fightTarget[i] && player_exists(g_fightTarget[i]) && g_fightSeenAt[i] && H->frame-g_fightSeenAt[i]<3*30){
        char *enemy=*(char **)(g_fightTarget[i]+OFF_PLAYER_SOLDIER);
        float reach=sniper_rifle(i)?sniper_reach(i):150;   // V10: a sniper's fight reaches further
        if(enemy && dist2((float *)(enemy+OFF_SOLDIER_SPAWN_POSITION),p)<reach*reach){w->progressAt=H->frame;return;}
    }
    // AltBotMod V10: a sniper on its perch is meant to stay put (its hold ends by itself after 75-120 s)
    if(sniper_holding(i) && (b->task==TASK_NONE || b->task==TASK_FIGHT)){w->progressAt=H->frame;return;}
    if(class_job_working(i,p)){w->progressAt=H->frame;return;}   // AltBotMod V11: a revive / repair / drop / rocket aim / supply wait
    if(still<18*30 || (w->actionAt && H->frame-w->actionAt<3*30))return;
    // A real capture in its radius is useful work. Guard/patrol stops already
    // have a short director deadline; an invalid waiting flag has no exemption.
    int f=b->targetFlag;
    if(b->waiting && f>=0 && f<objective_count() && objective_wanted(player_team(pl),f) &&
       dist2(p,objective_pos(f))<25*25 && fabsf(p[1]-objective_pos(f)[1])<8){w->progressAt=H->frame;return;}
    if(w->escapeUntil>H->frame)return;
    // Caspian V5: a fight that stopped a bot on its way to a vehicle is not a stuck walk. End the fight and let it
    // carry on to the vehicle (V4: fights cut most walks to abandoned vehicles, and the watchdog then dropped them).
    if(b->task==TASK_FIGHT && (g_drive[i].phase==1 || g_drive[i].phase==5 || (g_air[i].phase==AIR_WALK)) && !w->attempts){
        activity_end_task(i);w->progressAt=H->frame;w->attempts=1;w->actionAt=H->frame;
        if(g_drive[i].phase==1)g_drive[i].walkUntil+=20*30;
        bot_nav(i)->mode=3;
        diagnostic_note(i,"fight on the way to a vehicle ended; the walk to it continues",g_drive[i].veh?g_drive[i].veh:g_air[i].veh);
        return;
    }
    w->actionAt=H->frame;w->attempts++;
    // Caspian Test16: re-plan the objective once per stall, then at most every
    // 30 s. Re-planning every 3 s (Test15) cancelled the bot's route each time,
    // so its own path/stairs/vault recovery never had time to work and the
    // new objective flipped between flags. The 3 s local exit probe remains.
    if(w->attempts==1 || !w->replanAt || H->frame-w->replanAt>=ACTIVITY_REPLAN_FRAMES){
        w->replanAt=H->frame?H->frame:1;
        activity_end_task(i);activity_replan(i);
    }
    g_fleet[i].vehicle=NULL;g_fleet[i].until=g_fleet[i].queuedAt=0;
    g_fleet[i].retryAt=H->frame+15*30;
    if(g_air[i].phase)air_stop(i,"boarding walk stalled; releases aircraft to nearby teammate",0);
    else if(g_drive[i].phase)drive_stop(i,"boarding walk stalled; resumes foot objective",20);
    activity_step(i,s);
    diagnostic_note(i,"stationary infantry recovers instead of restarting the same wait",NULL);
    if(still>=90*30 && (!g_activityRetireAt || H->frame-g_activityRetireAt>=3*30)){
        g_activityRetireAt=H->frame;retire_stuck_bot(i,"no physical progress after checked escapes; fresh player redeploys");
    }
}
