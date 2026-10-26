#ifndef FLEET_ROUND_RUSH
#define FLEET_ROUND_RUSH 0   // AltBotMod V23 (core.c: the first 2 minutes of a round)
#endif
#ifndef FLEET_BOAT_NOT_NEEDED
#define FLEET_BOAT_NOT_NEEDED(v,from,f) 0
#endif
#ifndef ROUTE_BOARD_BONUS
#define ROUTE_BOARD_BONUS(v,team) 0.0f
#endif
// Requisition available vehicles using the beta's existing spawn/path/seat APIs.
// No teleporting living bots, forced deaths, vehicle creation, or new addresses.
static void fleet_inventory_tick(void)
{
    static unsigned signature,checkedAt;static LONG scanPass;
    if(!mode_uses_vehicles() || !level_ready() || H->frame-checkedAt<15)return;
    checkedAt=H->frame;unsigned value=0;
    for(int f=0;f<objective_count();f++)value=value*31u+(unsigned)(mode_is_cq()?flag_owner(f):(int)g_rush.objective[(g_rushStage-1)*2+1+f].status)+1u;
    if(value!=signature){signature=value;InterlockedExchange(&g_compRefresh,1);}
    LONG current=InterlockedCompareExchange(&g_scanPasses,0,0);
    if(current!=scanPass){scanPass=current;for(int i=0;i<H->botCount;i++)
        if(!g_drive[i].phase && !g_air[i].phase && bot_brain(i)->task==TASK_NONE)g_fleet[i].retryAt=0;}
}
static unsigned fleet_walk_frames(float distance)
{
    float seconds=fmaxf(35,fminf(600,distance/3+30));
    return (unsigned)(seconds*30);
}
static int fleet_route_blocked(char *vehicle,int team,const float *origin)
{
    for(int k=0;k<64;k++)if(g_fleetRouteFailure[k].vehicle==vehicle &&
        g_fleetRouteFailure[k].team==team && H->frame<g_fleetRouteFailure[k].until &&
        fabsf(origin[1]-g_fleetRouteFailure[k].origin[1])<6 && dist2(origin,g_fleetRouteFailure[k].origin)<60*60)return 1;
    return 0;
}
static void fleet_route_failed(char *vehicle,int team,const float *origin,unsigned seconds)
{
    int slot=-1;unsigned oldest=~0u;
    for(int k=0;k<64;k++){
        if(g_fleetRouteFailure[k].vehicle==vehicle && g_fleetRouteFailure[k].team==team &&
           dist2(origin,g_fleetRouteFailure[k].origin)<60*60){slot=k;break;}
        if(g_fleetRouteFailure[k].until<oldest){oldest=g_fleetRouteFailure[k].until;slot=k;}
    }
    if(slot<0)return;
    g_fleetRouteFailure[slot].vehicle=vehicle;g_fleetRouteFailure[slot].team=team;
    memcpy(g_fleetRouteFailure[slot].origin,origin,12);g_fleetRouteFailure[slot].until=H->frame+seconds*30;
}
static int fleet_claimed(char *v,int except)
{
    for (int j=0;j<H->botCount;j++) if (j!=except &&
        ((g_drive[j].phase && g_drive[j].veh==v) ||
         (g_fleet[j].vehicle==v && H->frame<g_fleet[j].until))) return 1;
    return 0;
}
static void fleet_counts(int team,int *air,int *jets,int *ground,int *total)
{
    *air=*jets=*ground=*total=0;
    for (int j=0;j<H->botCount;j++) if (player_team(H->bots[j])==team) {
        (*total)++;
        if (g_air[j].phase && g_air[j].pilot) {
            (*air)++; if (g_air[j].profile && g_air[j].profile->kind==BM_AIR_JET) (*jets)++;
        } else if (g_drive[j].phase && g_drive[j].phase!=5 && g_drive[j].phase!=7 && g_drive[j].phase!=9) {
            int counted=0;
            for (int k=0;k<j;k++) if (player_team(H->bots[k])==team && g_drive[k].phase &&
                g_drive[k].phase!=5 && g_drive[k].phase!=7 && g_drive[k].phase!=9 && g_drive[k].veh==g_drive[j].veh) counted=1;
            if (!counted) (*ground)++;
        }
        else if(g_deployActive[j].at && g_deployActive[j].epoch==(unsigned)g_roundEpoch){(*air)++;int k=g_deployActive[j].choice;if(k>=0&&k<32&&g_deployChoice[k].profile&&g_deployChoice[k].profile->kind==BM_AIR_JET)(*jets)++;}
        else if (g_fleet[j].vehicle && H->frame<g_fleet[j].until) {
            if (g_fleet[j].role==FLEET_GROUND) (*ground)++;
            else { (*air)++; if (g_fleet[j].role==FLEET_JET) (*jets)++; }
        }
    }
}
static int fleet_capacity(int total)
{
    // Driver and six-pilot class limits apply independently on both teams.
    // Do not reserve an artificial infantry quota while usable vehicles wait.
    return total>0?total:1;
}
static void fleet_class_counts(int team,int counts[BM_DISPATCH_COUNT])
{
    memset(counts,0,BM_DISPATCH_COUNT*sizeof *counts);
    for(int j=0;j<H->botCount;j++)if(player_team(H->bots[j])==team){
        const BMAirProfile *a=g_air[j].phase&&g_air[j].pilot?g_air[j].profile:NULL;
        if(a)counts[a->kind==BM_AIR_JET?BM_DISPATCH_JET:BM_DISPATCH_HELI]++;
        else if(g_drive[j].phase&&g_drive[j].phase!=5&&g_drive[j].phase!=7&&g_drive[j].phase!=9){counts[bm_armored(bm_vehicle_profile(vehicle_name(g_drive[j].veh)))?BM_DISPATCH_ARMOR:BM_DISPATCH_LIGHT]++;}
        else if(g_fleet[j].vehicle&&H->frame<g_fleet[j].until){char *v=g_fleet[j].vehicle;const BMAirProfile *p=bm_air_profile(vehicle_name(v));int role=p?(p->kind==BM_AIR_JET?BM_DISPATCH_JET:BM_DISPATCH_HELI):(bm_armored(bm_vehicle_profile(vehicle_name(v)))?BM_DISPATCH_ARMOR:BM_DISPATCH_LIGHT);counts[role]++;}
    }
}
static int fleet_has_jet(int team)
{
    char **list;int n=vehicle_list(&list);
    for(int k=0;k<n;k++){
        const BMAirProfile *p=bm_air_profile(vehicle_name(list[k]));
        if(p && p->kind==BM_AIR_JET && !strcmp(fleet_reason(list[k],team),"available"))return 1;
    }
    return 0;
}

static int fleet_air_slot(int team,const BMAirProfile *p)
{
    if (!p || team<1 || team>2) return 0;
    int air,jets,ground,total; fleet_counts(team,&air,&jets,&ground,&total);
    // The aircraft dispatcher may transfer a ground walking duty, so existing
    // ground claims cannot consume the independent pilot cap. Ordinary fleet
    // selection still checks total capacity before calling this helper.
    if (air>=g_enhanced.air.pilotsPerTeam || air>=6) return 0;
    // Reserve one slot for a usable friendly jet. Helicopters cannot consume
    // every pilot slot simply because their pads are closer to infantry.
    if (p->kind!=BM_AIR_JET && g_enhanced.air.jets && !jets && fleet_has_jet(team) &&
        air>=g_enhanced.air.pilotsPerTeam-1) return 0;
    return 1;
}
static int fleet_pilot_allowed(int i)
{
    int team=player_team(H->bots[i]),pilots=0,limit=g_enhanced.air.pilotsPerTeam;
    if(limit>6)limit=6;
    for(int j=0;j<H->botCount;j++)if(j!=i && player_team(H->bots[j])==team){
        if(g_air[j].phase && g_air[j].pilot)pilots++;
        else if(g_deployActive[j].at && g_deployActive[j].epoch==(unsigned)g_roundEpoch)pilots++;
        else if(g_fleet[j].vehicle && H->frame<g_fleet[j].until && g_fleet[j].role!=FLEET_GROUND)pilots++;
    }
    return team>=1 && team<=2 && pilots<limit;
}
static const char *fleet_reason(char *v,int team)
{
    if (!g_enhanced.driving || g_driveOff || !g_vehInput) return "driving/input-disabled";
    if (!vehicle_ok(v)) return "destroyed/stale";
    const BMAirProfile *a=bm_air_profile(vehicle_name(v));
    const BMVehicleProfile *p=bm_vehicle_profile(vehicle_name(v));
    if (a ? !bm_air_allowed(&g_enhanced.air,a) : !bm_vehicle_allowed(&g_enhanced,p)) return "class-disabled";
    int vt=0; float *pos=vehicle_pos(v,&vt);
    if (!pos || !isfinite(pos[0]) || !isfinite(pos[1]) || !isfinite(pos[2])) return "position-unavailable";
    int occupied=vehicle_occupied(v);
    if(occupied){char *crew=vehicle_occupant(v);if(crew && player_exists(crew))vt=player_team(crew);}
    if (occupied && vt && vt!=team) return "other-team";
    const float *own=g_hqCpKnown[team] ? g_hqCpPos[team] : g_hqPos[team];
    const float *other=g_hqCpKnown[3-team] ? g_hqCpPos[3-team] : g_hqPos[3-team];
    // Last-owner team and the map midpoint do not make an abandoned vehicle
    // unusable. Protect the opposing deployment pad, not its whole map half.
    if (!occupied && (g_hqCpKnown[team] || (g_mode==BM_MODE_RUSH?mode_base_present(g_rushStage,team):!near_origin(own))) &&
        (g_hqCpKnown[3-team] || (g_mode==BM_MODE_RUSH?mode_base_present(g_rushStage,3-team):!near_origin(other))) &&
        dist2(pos,other)<140*140 && dist2(pos,own)>dist2(pos,other)) return "enemy-deployment";
    if (!driver_seat_free(v)) return "driver-seat-taken";
    if (human_near(pos,g_enhanced.humanRadius)) return "reserved-for-human";
    if (vehicle_bad(v)) return "route/vehicle-cooldown";
    if (a && !bm_air_transform_valid(vehicle_xf(v))) return "aircraft-transform-unavailable";
    return "available";
}
static void fleet_remember_bases(void)
{
    // Keep the original HQ when human spawn learning later moves infantry HQ.
    for (int t=1;t<=2;t++) if (!cse_valid(g_fleetBase[t]) && cse_valid(H->teamSpawn[t].entity) &&
        (spawn_team(H->teamSpawn[t].entity)==0 || spawn_team(H->teamSpawn[t].entity)==t || hq_is_option(t,H->teamSpawn[t].entity))) {
        g_fleetBase[t]=H->teamSpawn[t].entity;
        float *position=spawn_data_pos(H->teamSpawn[t].entity);
        memcpy(g_fleetBasePos[t],position && !near_origin(position)?position:g_hqPos[t],12);
        g_fleetBaseArgs[t][0]=H->teamSpawn[t].a2; g_fleetBaseArgs[t][1]=H->teamSpawn[t].a3; g_fleetBaseArgs[t][2]=H->teamSpawn[t].a4;
    }
}
static int fleet_enabled(int i)
{
    return mode_uses_vehicles() && g_enhanced.returnToBase && g_enhanced.driving && !g_driveOff && g_vehInput &&
        level_ready() && bot_brain(i)->enabled && bot_brain(i)->task==TASK_NONE &&
        !player_vehicle(H->bots[i]) && !g_drive[i].phase &&
        H->frame>=g_fleet[i].retryAt;
}
static int fleet_reserve(int i,const float *origin)
{
    int team=player_team(H->bots[i]); if (team<1 || team>2) return 0;
    if(g_mode==BM_MODE_RUSH && mode_vehicle_target(i,origin)<0)return 0;
    FleetReservation *r=&g_fleet[i];
    if (r->vehicle && H->frame<r->until && !strcmp(fleet_reason(r->vehicle,team),"available") &&
        !fleet_claimed(r->vehicle,i)) return 1;
    r->vehicle=NULL; r->role=0; r->until=0;r->queuedAt=0;
    // Bound unstarted walking searches per team. Other infantry can leave
    // deployment while these two requests receive the shared route budget.
    int queued=0;for(int j=0;j<H->botCount;j++)if(j!=i && player_team(H->bots[j])==team &&
        g_fleet[j].vehicle && H->frame<g_fleet[j].until && !g_drive[j].phase && !g_air[j].phase)queued++;
    // AltBotMod V23: a round's start is a rush to every vehicle - in its first 2 minutes any number walk to one; after that
    // 8 at a time (V9-V22: 2 at a time unless one stood within 25 m - most of a base's vehicles waited)
    int rush=FLEET_ROUND_RUSH;
    if(!rush && queued>=8){
        char **available;int count=vehicle_list(&available),close=0;
        for(int k=0;k<count && !close;k++)if(!strcmp(fleet_reason(available[k],team),"available") &&
            !fleet_claimed(available[k],i) && dist2(origin,vehicle_pos(available[k],NULL))<25*25)close=1;
        if(!close)return 0;
    }
    int air,jets,ground,total; fleet_counts(team,&air,&jets,&ground,&total);
    if (air+ground>=fleet_capacity(total)) return 0;
    int classes[BM_DISPATCH_COUNT];fleet_class_counts(team,classes);
    char **list,*best=NULL; int role=0,n=vehicle_list(&list); float bestScore=1e30f;
    for (int k=0;k<n;k++) {
        char *v=list[k];
        if(r->failedVehicle==v && H->frame<r->failedUntil)continue;
        if(fleet_route_blocked(v,team,origin))continue;
        if (strcmp(fleet_reason(v,team),"available") || fleet_claimed(v,i)) continue;
        float *pos=vehicle_pos(v,NULL),distance=sqrtf(dist2(origin,pos));
        if (distance>g_enhanced.returnMeters || (queued>=2 && distance>=25)) continue;
        // AltBotMod V34: after the round's opening, a bot not just out of the spawn walks at most VehicleWalkMeters (250 m)
        // to a vehicle, and one with its objective close by (120 m) only to a vehicle beside it (40 m). V33 Caspian Border:
        // 242 objectives dropped in 17 min for 300-700 m walks back to base, the vehicle mostly gone or destroyed on arrival
        // (84 taken at the spawn screen, 92 destroyed) - the bots turned round again and again.
        if (!rush && *(char **)(H->bots[i]+OFF_PLAYER_SOLDIER) && H->frame-g_lastSpawnedAt[i]>20*30) {   // (a bot choosing where to spawn is not walking)
            if (distance>g_enhanced.vehicleWalkMeters) continue;
            int tf=bot_brain(i)->targetFlag;
            if (distance>40 && tf>=0 && tf<objective_count() && objective_pos(tf) && dist2(origin,objective_pos(tf))<120*120) continue;
        }
        const BMAirProfile *a=bm_air_profile(vehicle_name(v));
        const BMVehicleProfile *p=bm_vehicle_profile(vehicle_name(v));
        int kind;
        int dispatch;
        if (a) {
            if (H->frame<g_airCooldown[i] || !fleet_air_slot(team,a)) continue;
            kind=a->kind==BM_AIR_JET ? FLEET_JET : FLEET_HELI;
            dispatch=kind==FLEET_JET?BM_DISPATCH_JET:BM_DISPATCH_HELI;
        } else {
            if (H->frame<g_driveCooldown[i] || ground>=g_enhanced.groundDriversPerTeam) continue;
            int f=fleet_driver_flag(i,v);
            if (f<0 || f>=objective_count()) f=bot_brain(i)->targetFlag;
            // Don't send infantry on a long detour for a jeep for a short trip.
            if (!bm_armored(p) && distance>g_enhanced.findRadius && (f<0 || f>=objective_count() || dist2(origin,objective_pos(f))<g_enhanced.minTrip*g_enhanced.minTrip)) continue;
            if(f>=0 && f<objective_count() && FLEET_BOAT_NOT_NEEDED(v,origin,f))continue;   // AltBotMod V16: a boat only across water
            kind=FLEET_GROUND;dispatch=bm_armored(p)?BM_DISPATCH_ARMOR:BM_DISPATCH_LIGHT;
        }
        float score=bm_dispatch_score(dispatch,classes,distance);
        score-=ROUTE_BOARD_BONUS(v,team);   // AltBotMod V15: standing on a boarding spot recorded for this team (Fun-Bots "vehicle tank1 us")
        // Use an unclaimed combat helicopter before another scout/transport.
        if(a && a->kind==BM_AIR_ATTACK)score-=450;
        // Immediate boarding takes priority over a distant reservation.
        if(distance<=25)score-=3000;
        if (score<bestScore) { best=v; bestScore=score; role=kind; }
    }
    if (!best) { diagnostic_note(i,"no available vehicle or fleet capacity",NULL); return 0; }
    r->vehicle=best; r->role=role; r->until=H->frame+30*30;r->queuedAt=H->frame?H->frame:1;
    diagnostic_note(i,"vehicle reserved; path to boarding point",best);
    return 1;
}
static void *fleet_spawn(char *bot)
{
    int i=bot_index(bot),team=player_team(bot);
    if (i<0 || team<1 || team>2) return NULL;
    if(!*(char **)(bot+OFF_PLAYER_SOLDIER)) {
        // A new life at deployment can reach a vehicle that the previous life
        // could not. Old walking / destroyed-vehicle cooldowns must not force
        // this spawn back onto a front-line flag.
        g_fleet[i].retryAt=0;g_fleet[i].failedVehicle=NULL;g_fleet[i].failedUntil=0;g_driveCooldown[i]=0;g_airCooldown[i]=0;
    }
    if (!fleet_enabled(i)) return NULL;
    fleet_remember_bases();
    void *base=g_fleetBase[team];
    int have=cse_valid(base) && fleet_reserve(i,g_fleetBasePos[team]);
    // Assault defenders and far captured-flag vehicles may have no usable
    // original HQ. Consider only currently owned, uncontested native spawns.
    if(!have && mode_is_cq())for(int f=0;f<g_flagCount && !have;f++){
        int control=*(int *)(g_flags[f]+OFF_CP_CONTROL_TEAM);
        void *e=g_flagSpawn[f][team];
        if(flag_owner(f)!=team || (control && control!=team) || !cse_valid(e) ||
           (spawn_team(e)!=0 && spawn_team(e)!=team))continue;
        have=fleet_reserve(i,flag_pos(f));
    }
    if(!have)return NULL;
    FleetReservation *r=&g_fleet[i];
    memcpy(r->spawnArgs,g_fleetBaseArgs[team],sizeof r->spawnArgs);
    float *vehicle=vehicle_pos(r->vehicle,NULL);
    float distance=cse_valid(base)?dist2(g_fleetBasePos[team],vehicle):INFINITY;
    if(mode_is_cq())for(int f=0;f<g_flagCount;f++){
        int control=*(int *)(g_flags[f]+OFF_CP_CONTROL_TEAM);void *e=g_flagSpawn[f][team];
        if(flag_owner(f)!=team || (control && control!=team) || !cse_valid(e) ||
           (spawn_team(e)!=0 && spawn_team(e)!=team))continue;
        const float *position=spawn_data_pos(e);if(!position)position=flag_pos(f);
        float value=dist2(position,vehicle);
        if(value<distance){base=e;distance=value;r->spawnArgs[0]=0xffffffff;r->spawnArgs[1]=1;r->spawnArgs[2]=0;}
    }
    if(!cse_valid(base)){r->vehicle=NULL;r->until=r->queuedAt=0;return NULL;}
    bot_log("fleet: %s naturally respawns at a validated team deployment for %s (%.0f m to board)",bot+OFF_PLAYER_ONLINE_NAME,vehicle_name(r->vehicle),sqrtf(distance));
    return base;
}
static void fleet_nearby_opportunity(int i)
{
    BotBrain *b=bot_brain(i);char *pl=H->bots[i],*s=*(char **)(pl+OFF_PLAYER_SOLDIER);
    // A distant firefight must not permanently exclude an unused tank beside
    // the bot. Keep human orders and immediate self-defence in control.
    if((H->frame+i)%30 || !mode_uses_vehicles() || !level_ready() || !g_enhanced.returnToBase ||
       !b->enabled || b->task!=TASK_FIGHT || b->prevTask!=TASK_NONE || !s ||
       player_vehicle(pl) || g_drive[i].phase || g_air[i].phase || H->frame<g_fleet[i].retryAt ||
       (g_hitAt[i] && H->frame-g_hitAt[i]<3*30))return;
    char *target=g_fightTarget[i],*enemy=target && player_exists(target)?*(char **)(target+OFF_PLAYER_SOLDIER):NULL;
    float *me=(float *)(s+OFF_SOLDIER_POSITION);
    float enemyDistance=enemy?sqrtf(dist2(me,(float *)(enemy+OFF_SOLDIER_SPAWN_POSITION))):INFINITY;
    if(enemyDistance<45)return;
    char **vehicles,*best=NULL;int n=vehicle_list(&vehicles),team=player_team(pl);
    if(team<1 || team>2)return;
    int air,jets,ground,total;fleet_counts(team,&air,&jets,&ground,&total);
    if(air+ground>=fleet_capacity(total))return;
    int queued=0;for(int j=0;j<H->botCount;j++)if(j!=i && player_team(H->bots[j])==team &&
        g_fleet[j].vehicle && H->frame<g_fleet[j].until && !g_drive[j].phase && !g_air[j].phase)queued++;
    
    float nearest=40*40;int role=FLEET_GROUND;
    for(int k=0;k<n;k++){
        char *v=vehicles[k];if(strcmp(fleet_reason(v,team),"available") || fleet_claimed(v,i) ||
            fleet_route_blocked(v,team,me) ||
            (g_fleet[i].failedVehicle==v && H->frame<g_fleet[i].failedUntil))continue;
        const BMAirProfile *a=bm_air_profile(vehicle_name(v));
        if(a){if(!fleet_air_slot(team,a) || H->frame<g_airCooldown[i])continue;}
        else if(ground>=g_enhanced.groundDriversPerTeam || H->frame<g_driveCooldown[i])continue;
        if(!a && !bm_armored(bm_vehicle_profile(vehicle_name(v))) && enemyDistance<90)continue;
        float d=dist2(me,vehicle_pos(v,NULL));if(d<nearest){nearest=d;best=v;role=a?(a->kind==BM_AIR_JET?FLEET_JET:FLEET_HELI):FLEET_GROUND;}
    }
    if(!best)return;
    end_fight(i,b,"nearby unoccupied vehicle offers the next advance");
    FleetReservation *r=&g_fleet[i];r->vehicle=best;r->role=role;
    r->until=H->frame+30*30;r->queuedAt=H->frame?H->frame:1;
}
static void fleet_release_distant_claim(int i)
{
    if((H->frame+i)%30 || !fleet_enabled(i) || g_fleet[i].vehicle || g_gliding[i])return;
    char *s=*(char **)(H->bots[i]+OFF_PLAYER_SOLDIER);if(!s)return;
    float *me=(float *)(s+OFF_SOLDIER_POSITION);int team=player_team(H->bots[i]);
    if(team<1 || team>2)return;
    for(int j=0;j<H->botCount;j++){
        char *other=*(char **)(H->bots[j]+OFF_PLAYER_SOLDIER),*vehicle=NULL;unsigned since=0;int role=0;
        if(j==i || !other || player_team(H->bots[j])!=team || player_vehicle(H->bots[j]))continue;
        if(g_air[j].phase==AIR_WALK && g_air[j].pilot){vehicle=g_air[j].veh;since=g_air[j].at;role=g_air[j].profile->kind==BM_AIR_JET?FLEET_JET:FLEET_HELI;}
        else if(g_drive[j].phase==1 && !g_drive[j].manual){vehicle=g_drive[j].veh;since=g_drive[j].at;role=FLEET_GROUND;}
        else if(!g_drive[j].phase && !g_air[j].phase && g_fleet[j].vehicle){vehicle=g_fleet[j].vehicle;since=g_fleet[j].queuedAt;role=g_fleet[j].role;}
        if(!vehicle || !since || H->frame-since<5*30 || strcmp(fleet_reason(vehicle,team),"available") || vehicle_occupied(vehicle))continue;
        float *pos=vehicle_pos(vehicle,NULL);
        if(!pos || dist2(me,pos)>25*25 || dist2((float *)(other+OFF_SOLDIER_POSITION),pos)<90*90 ||
           H->frame<(role==FLEET_GROUND?g_driveCooldown[i]:g_airCooldown[i]) || fleet_route_blocked(vehicle,team,me))continue;
        if(g_air[j].phase)air_stop(j,"nearby teammate takes distant pilot claim",0);
        else if(g_drive[j].phase)drive_stop(j,"nearby teammate takes distant vehicle claim",15);
        nav_cancel_path(j);nav_cancel_path(j+MAX_BOTS);memset(&g_fleet[j],0,sizeof g_fleet[j]);
        choose_local_objective(j,"vehicle claim handed to nearby teammate");
        FleetReservation *r=&g_fleet[i];r->vehicle=vehicle;r->role=role;
        r->until=H->frame+30*30;r->queuedAt=H->frame?H->frame:1;
        return;
    }
}
// A bot stands still while its vehicle request is queued; bound that wait.
#define FLEET_QUEUE_TIMEOUT_FRAMES (20*30)
static void fleet_tick(int i)
{
    fleet_nearby_opportunity(i);
    fleet_release_distant_claim(i);
    FleetReservation *pending=&g_fleet[i];
    if(pending->vehicle && pending->queuedAt && !g_drive[i].phase && !g_air[i].phase &&
       H->frame-pending->queuedAt>FLEET_QUEUE_TIMEOUT_FRAMES){
        // Caspian Test16: the route search never finished (planner budget, or
        // the nav grid still building at round start). That is not evidence
        // the vehicle is unreachable, so it is NOT blocked for the whole team
        // at this location; only this bot skips it briefly. Real route
        // failures below still use fleet_route_failed().
        char *failed=pending->vehicle;nav_cancel_path(i);
        memset(&g_vehicleApproach[i],0,sizeof g_vehicleApproach[i]);
        pending->vehicle=NULL;pending->role=0;pending->until=pending->queuedAt=0;
        pending->failedVehicle=failed;pending->failedUntil=H->frame+25*30;pending->retryAt=H->frame+8*30;
        choose_local_objective(i,"vehicle approach queue timed out; advance on foot");
        diagnostic_note(i,"vehicle approach timed out; advancing on foot",failed);
        return;
    }
    if(g_fleet[i].vehicle && (!bot_brain(i)->enabled || bot_brain(i)->task!=TASK_NONE || player_vehicle(H->bots[i]))){
        // Combat/human orders supersede a walking reservation immediately.
        // Other bots must not wait ninety seconds for an abandoned claim.
        g_fleet[i].vehicle=NULL;g_fleet[i].role=0;g_fleet[i].until=0;
    }
    if (!fleet_enabled(i)) return;
    char *pl=H->bots[i],*s=*(char **)(pl+OFF_PLAYER_SOLDIER);
    NavGrid *grid=*nav_slot();
    if(!s || *(float *)(s+OFF_SOLDIER_HEALTH)<=0 || g_gliding[i] || H->frame-g_lastSpawnedAt[i]<6)return;
    if(!grid || grid->phase!=2){
        float *boardingPosition=g_fleet[i].vehicle && vehicle_ok(g_fleet[i].vehicle)?vehicle_pos(g_fleet[i].vehicle,NULL):NULL;
        if(!boardingPosition || dist2((float *)(s+OFF_SOLDIER_POSITION),boardingPosition)>25*25)return;
    }
    if(!g_fleetPriorityUntil)g_fleetPriorityUntil=H->frame+60*30;
    int opening=H->frame<g_fleetPriorityUntil || H->frame-g_lastSpawnedAt[i]<15*30;
    if(!g_fleet[i].vehicle && (H->frame+i)%15)return;
    (void)opening;
    const float *me=(float *)(s+OFF_SOLDIER_POSITION);
    if (!fleet_reserve(i,me)) return;
    FleetReservation *r=&g_fleet[i]; char *v=r->vehicle;
    float *pos=vehicle_pos(v,NULL); float distance=sqrtf(dist2(me,pos));
    if(distance>25 && !vehicle_board_close(s,pos) && !plan_budget_left())return;
    int flag=fleet_driver_flag(i,v);
    if (flag<0 || flag>=objective_count()) flag=bot_brain(i)->targetFlag;
    if (flag<0 || flag>=objective_count() || !objective_pos(flag) || (!mode_is_cq() && !objective_wanted(player_team(pl),flag))){r->vehicle=NULL;r->until=r->queuedAt=0;return;}
    int success=0;
    if (r->role!=FLEET_GROUND) success=air_claim(i,v,1,flag);
    else if (plan_to_vehicle(i,s,pos)) {
        end_task(i,bot_brain(i),"returns for available vehicle");
        DriveState *d=&g_drive[i]; memset(d,0,sizeof *d);
        d->phase=1; d->veh=v; d->at=H->frame; d->flag=flag;
        d->walkUntil=H->frame+fleet_walk_frames(distance);
        memcpy(d->start,pos,12); memcpy(d->goal,objective_pos(flag),12);
        memset(&g_flagRoute[i],0,sizeof g_flagRoute[i]);
        bot_brain(i)->targetFlag=flag;bot_brain(i)->enroute=1;
        bot_nav(i)->mode=vehicle_board_close(s,pos)?0:3;
        success=1;
    }
    if(success)r->queuedAt=0;
    if (success) bot_log("fleet: %s %s for %s (%.0f m; walking timeout %.0f s)",pl+OFF_PLAYER_ONLINE_NAME,
        distance>g_enhanced.findRadius ? "returns to base" : "takes a nearby vehicle",vehicle_name(v),distance,fleet_walk_frames(distance)/30.0f);
    else if(vehicle_plan_deferred(i)) {
        // Keep the selected vehicle and continue with the next approach point.
        return;
    } else {
        bot_log("fleet: %s cannot route to %s %.0f m away; tries another vehicle after cooldown",pl+OFF_PLAYER_ONLINE_NAME,vehicle_name(v),distance);
        // Walking failure belongs to this bot's location. Other bots at
        // deployment may be able to reach the same perfectly usable vehicle.
        r->failedVehicle=v;r->failedUntil=H->frame+30*30;r->retryAt=H->frame+10*30;
        fleet_route_failed(v,player_team(pl),me,90);
        choose_local_objective(i,"vehicle route unavailable; advances on foot");
        diagnostic_note(i,"vehicle route failed; nearby teammates try other vehicles or infantry objectives",v);
    }
    r->vehicle=NULL; r->role=0; r->until=0;r->queuedAt=0;
}
static void fleet_status(void)
{
    bot_log("fleet: return-to-base=%d passenger-takeover=%d range=%.0f m ground-dispatch-limit/team=%d; human seats preserved",g_enhanced.returnToBase,g_enhanced.passengerTakeover,g_enhanced.returnMeters,g_enhanced.groundDriversPerTeam);
    bot_log("fleet: opening priority remaining=%.0f s; vehicle cache epoch=%ld current round=%ld",H->frame<g_fleetPriorityUntil?(g_fleetPriorityUntil-H->frame)/30.0f:0,g_compEpoch,g_roundEpoch);
    for (int t=1;t<=2;t++) {
        int air,jets,ground,total; fleet_counts(t,&air,&jets,&ground,&total);
        bot_log("fleet: team=%d bots=%d pilots/queued=%d jets/queued=%d ground/queued=%d original-HQ=%p",t,total,air,jets,ground,g_fleetBase[t]);
    }
    for (int i=0;i<H->botCount;i++) if (g_fleet[i].vehicle)
        bot_log("fleet: bot=%d pending=%s expires-in=%.0f s",i,vehicle_name(g_fleet[i].vehicle),(int)(g_fleet[i].until-H->frame)/30.0f);
}
// Progress watchdog for uncommanded bots. It never kills or teleports a live
// soldier, and leaves combat, explicit human orders and active captures alone.
static void deployment_idle_tick(int i)
{
    if((H->frame+i)%30 || !level_ready() || !mode_uses_vehicles())return;
    char *pl=H->bots[i],*s=*(char **)(pl+OFF_PLAYER_SOLDIER);
    BotBrain *b=bot_brain(i);DeploymentIdleState *watch=&g_deploymentIdle[i];
    if(!s || !b->enabled || b->task!=TASK_NONE || player_vehicle(pl) || g_gliding[i] ||
       !*nav_slot() || (*nav_slot())->phase!=2){memset(watch,0,sizeof *watch);return;}
    const float *p=(float *)(s+OFF_SOLDIER_POSITION);
    if(!isfinite(p[0]) || !isfinite(p[1]) || !isfinite(p[2]))return;
    if(watch->soldier!=s || !watch->progressAt || dist2(watch->position,p)>2*2){
        watch->soldier=s;watch->progressAt=H->frame?H->frame:1;memcpy(watch->position,p,12);return;
    }
    if(H->frame-watch->progressAt<18*30)return;
    if(g_flagRoute[i].pending && H->frame-watch->progressAt<45*30)return;
    if(g_air[i].phase==AIR_WALK || g_drive[i].phase==1 || g_drive[i].phase==5){
        char *v=g_air[i].phase?g_air[i].veh:g_drive[i].veh;
        if(g_air[i].phase)air_stop(i,"boarding walk stalled; advances on foot",0);
        else drive_stop(i,"boarding walk stalled; advances on foot",20);
        g_fleet[i].failedVehicle=v;g_fleet[i].failedUntil=H->frame+45*30;g_fleet[i].retryAt=H->frame+20*30;
        fleet_route_failed(v,player_team(pl),p,45);
        diagnostic_note(i,"boarding made no progress; released assignment",v);
    }else if(g_drive[i].phase || g_air[i].phase || g_fleet[i].vehicle)return;
    int team=player_team(pl),best=-1;float score=INFINITY;
    for(int f=0;f<objective_count();f++)if(objective_wanted(team,f)){
        if(b->waiting && b->targetFlag==f && dist2(p,objective_pos(f))<30*30){watch->progressAt=H->frame;return;}
        float value=sqrtf(dist2(p,objective_pos(f)))+flag_pressure(team,f,i)*150.0f;
        if(f==b->targetFlag)value+=300;
        if(value<score){score=value;best=f;}
    }
    if(best>=0){
        nav_cancel_path(i);memset(&g_flagRoute[i],0,sizeof g_flagRoute[i]);
        g_paths[i].count=g_paths[i].next=0;
        int stuckFlag=b->targetFlag;
        g_localFlag[i]=best+1;g_localUntil[i]=H->frame+60*30;local_order_note(i,best);
        mode_reassign_idle(i,best);
        b->targetFlag=best;b->enroute=b->waiting=0;b->retryAt=b->replanAt=0;
        // Caspian Test16: keep the flag it stalled on avoided so the director
        // does not send it straight back there once the order ends.
        if(mode_is_cq() && stuckFlag>=0 && stuckFlag!=best){b->avoidFlag=stuckFlag;b->avoidUntil=H->frame+60*30;}
        else {b->avoidFlag=-1;b->avoidUntil=0;}
        bot_nav(i)->mode=0;
        diagnostic_note(i,"idle infantry replans toward a less crowded enemy flag",NULL);
    }
    watch->progressAt=H->frame;
}
static int fleet_driver_flag(int i,char *vehicle)
{
    int role,team=player_team(H->bots[i]),flag=director_target(i,&role);
    if(flag<0)flag=squad_objective(team,player_squad(H->bots[i]));
    if(flag<0 || flag>=objective_count())flag=bot_brain(i)->targetFlag;
    if(flag>=0 && flag<objective_count() && (objective_wanted(team,flag) ||
        (mode_is_cq() && squad_defending(team,player_squad(H->bots[i])))))return flag;
    flag=-1;
    const float *position=vehicle_pos(vehicle,NULL);float best=INFINITY;
    if(!position)return -1;
    for(int pass=0;pass<2 && flag<0;pass++)for(int f=0;f<objective_count();f++){
        if((!mode_is_cq() || !pass) && !objective_wanted(team,f))continue;
        float score=sqrtf(dist2(position,objective_pos(f)))+flag_pressure(team,f,i)*150;
        if(score<best){best=score;flag=f;}
    }
    return flag;
}
static void fleet_takeover(int i)
{
    char *pl=H->bots[i],*v=player_vehicle(pl);
    if(v && !g_drive[i].phase && !g_air[i].phase && vehicle_ok(v) && vehicle_seat_of(v,pl)==0 &&
        g_enhanced.driving && !g_driveOff && g_vehInput && mode_uses_vehicles() && level_ready() && bot_brain(i)->enabled &&
        !bm_air_profile(vehicle_name(v)) && bm_vehicle_allowed(&g_enhanced,bm_vehicle_profile(vehicle_name(v)))){
        // A bot already seated as driver (squad spawn/manual entry/reload)
        // needs a ground job before the idle infantry brain makes it get out.
        int flag=fleet_driver_flag(i,v);
        // AltBotMod V8: not the vehicle this bot has just given up on (stuck / no route): V7 re-adopted it the next
        // tick - its driver was still getting out - and called "Get in!" again; on XP5_001 a stuck LAV-25's squad
        // boarded and bailed once a second for minutes ("bots forget what they are doing").
        int released=H->frame<g_driveCooldown[i] || vehicle_bad(v) || (g_fleet[i].failedVehicle==v && H->frame<g_fleet[i].failedUntil);
        if(released){if((H->frame+i)%30==0)bot_exit_vehicle(i,0);}
        else if((flag>=0 && flag<objective_count()) || g_mode==BM_MODE_RUSH){
            end_task(i,bot_brain(i),"adopts occupied ground driver seat");gunner_reset(i);
            g_fleet[i].vehicle=NULL;g_fleet[i].role=0;g_fleet[i].until=0;
            memset(&g_flagRoute[i],0,sizeof g_flagRoute[i]);
            DriveState *d=&g_drive[i];memset(d,0,sizeof *d);d->phase=2;d->veh=v;d->flag=flag;d->at=H->frame;
            memcpy(d->goal,flag>=0?objective_pos(flag):vehicle_pos(v,NULL),12);bot_brain(i)->targetFlag=flag;bot_brain(i)->enroute=1;
            g_driving[i]=1;bot_nav(i)->mode=0;
            bot_log("fleet: %s retains the existing driver seat of %s",pl+OFF_PLAYER_ONLINE_NAME,vehicle_name(v));
        }
    }
    if (!g_enhanced.passengerTakeover || !g_enhanced.driving || g_driveOff || !g_vehInput ||
        !mode_uses_vehicles() || !level_ready() || !bot_brain(i)->enabled || !vehicle_ok(v) || vehicle_seat_of(v,pl)<=0 ||
        !driver_seat_free(v)) { g_takeoverSince[i]=0; return; }
    const BMAirProfile *a=bm_air_profile(vehicle_name(v));
    if (a ? !bm_air_allowed(&g_enhanced.air,a) || !bm_air_transform_valid(vehicle_xf(v)) || !fleet_pilot_allowed(i) :
        !bm_vehicle_allowed(&g_enhanced,bm_vehicle_profile(vehicle_name(v)))) return;
    if (!g_takeoverSince[i]) { g_takeoverSince[i]=H->frame ? H->frame : 1; return; }
    if ((int)(H->frame-g_takeoverSince[i])<6) return; // allow a human's seat switch to complete
    int oldSeat=vehicle_seat_of(v,pl),flag=fleet_driver_flag(i,v);
    ((EnterVehicle_t)ADDR_PLAYER_ENTER_VEHICLE)(pl,v,0);
    if (!a && player_vehicle(pl)==v && vehicle_seat_of(v,pl)!=0) {
        // Some builds refuse changing seats through EnterVehicle while seated.
        // Leave/re-enter in this one game-thread update and restore the old
        // seat if the driver entry is refused; never remove another occupant.
        bot_exit_vehicle(i,0);
        if (driver_seat_free(v)) ((EnterVehicle_t)ADDR_PLAYER_ENTER_VEHICLE)(pl,v,0);
        if (player_vehicle(pl)!=v) ((EnterVehicle_t)ADDR_PLAYER_ENTER_VEHICLE)(pl,v,oldSeat);
    }
    if (player_vehicle(pl)!=v || vehicle_seat_of(v,pl)!=0) {
        g_takeoverSince[i]=H->frame+24;
        bot_log("fleet: %s's driver-seat transfer was refused; retained seat=%d",pl+OFF_PLAYER_ONLINE_NAME,vehicle_seat_of(v,pl));
        return;
    }
    if ((flag<0 || flag>=objective_count() || !objective_pos(flag)) && g_mode!=BM_MODE_RUSH)return;
    end_task(i,bot_brain(i),"takes over empty driver seat"); gunner_reset(i);
    g_fleet[i].vehicle=NULL;g_fleet[i].role=0;g_fleet[i].until=0;
    memset(&g_drive[i],0,sizeof g_drive[i]); memset(&g_air[i],0,sizeof g_air[i]);
    if (a) {
        if (air_claim(i,v,1,flag)) g_air[i].takeover=1;
    } else {
        DriveState *d=&g_drive[i]; d->phase=2; d->veh=v; d->flag=flag; d->at=H->frame;
        memcpy(d->goal,flag>=0&&flag<objective_count()?objective_pos(flag):vehicle_pos(v,NULL),12);
        g_driving[i]=1; bot_nav(i)->mode=0;
    }
    g_takeoverSince[i]=0;
    bot_log("fleet: %s takes over the empty pilot/driver seat of %s",pl+OFF_PLAYER_ONLINE_NAME,vehicle_name(v));
}
