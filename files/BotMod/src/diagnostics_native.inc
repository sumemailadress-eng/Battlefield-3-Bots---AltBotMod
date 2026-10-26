static void diagnostic_note(int i,const char *reason,char *vehicle)
{
    if(i<0 || i>=H->botCount || !reason)return;
    VehicleDiagnostic *d=&g_vehicleDiag[i];
    if(!strcmp(reason,d->reason) && d->trackedVehicle==vehicle)return;
    d->trackedVehicle=vehicle;
    snprintf(d->reason,sizeof d->reason,"%s",reason);
    snprintf(d->vehicle,sizeof d->vehicle,"%s",vehicle_ok(vehicle)?vehicle_name(vehicle):"none");
    d->at=H->frame;d->changes++;
    if(strstr(reason,"unavailable") || strstr(reason,"failed") || strstr(reason,"timed out") || strstr(reason,"stalled"))d->failures++;
    if(g_enhanced.diagnostics && (!d->loggedAt || H->frame-d->loggedAt>=30)){
        d->loggedAt=H->frame?H->frame:1;
        bot_log("vehicle-decision: bot=%d team=%d %s | %s",i,player_team(H->bots[i]),reason,d->vehicle);
    }
}
static void diagnostic_message(const char *name)
{
    if(!g_enhanced.diagnostics || !name)return;
    // Count type names only. Unverified message payloads never become lock or
    // target observations, so an arbitrary 'Lock' notification cannot fire.
    if(strstr(name,"Lock"))g_lockMessageCount++;
    if(strstr(name,"Missile"))g_missileMessageCount++;
}
// Two bounded status lines every thirty seconds expose live rejection reasons.
// No full report or per-bot dump runs automatically.
static void director_summary_tick(void)
{
    static unsigned at;static LONG epoch;
    if(epoch!=g_roundEpoch){at=0;epoch=g_roundEpoch;}
    if(!g_enhanced.diagnostics || !mode_is_cq() || !level_ready() ||
       (at && H->frame-at<30*30))return;
    at=H->frame?H->frame:1;
    char **list;int count=vehicle_list(&list);
    for(int team=1;team<=2;team++){
        int alive=0,attack=0,guard=0,patrol=0,air,jets,ground,total,available[3]={0},rejected[6]={0};
        fleet_counts(team,&air,&jets,&ground,&total);
        for(int i=0;i<H->botCount;i++)if(player_team(H->bots[i])==team){
            char *body=*(char **)(H->bots[i]+OFF_PLAYER_SOLDIER);
            alive+=body && *(float *)(body+OFF_SOLDIER_HEALTH)>0;
            int role;director_target(i,&role);attack+=role==BM_OBJECTIVE_ATTACK;guard+=role==BM_OBJECTIVE_GUARD;patrol+=role==BM_OBJECTIVE_PATROL;
        }
        for(int k=0;k<count && k<256;k++)if(vehicle_ok(list[k])){
            const BMAirProfile *profile=bm_air_profile(vehicle_name(list[k]));const char *why=fleet_reason(list[k],team);
            if(!strcmp(why,"available") && !fleet_claimed(list[k],-1))available[profile?(profile->kind==BM_AIR_JET?1:2):0]++;
            else if(profile){int reason=!strcmp(why,"reserved-for-human")?0:!strcmp(why,"enemy-deployment") || !strcmp(why,"other-team")?1:
                !strcmp(why,"aircraft-transform-unavailable")?2:!strcmp(why,"class-disabled")?3:!strcmp(why,"driver-seat-taken")?4:5;rejected[reason]++;}
        }
        bot_log("director: team=%d alive=%d/%d attack=%d guard=%d patrol=%d ground-duties=%d pilots=%d jets=%d free-ground/jet/heli=%d/%d/%d air-excluded-human/enemy/transform/disabled/occupied/other=%d/%d/%d/%d/%d/%d",
            team,alive,total,attack,guard,patrol,ground,air,jets,available[0],available[1],available[2],rejected[0],rejected[1],rejected[2],rejected[3],rejected[4],rejected[5]);
    }
    {   // Caspian V3: every aircraft by name, once a minute, with why each team can or cannot take it
        // (only one team flew helicopters in V2 and the summary above did not say which aircraft was refused).
        static unsigned listedAt;
        if(!listedAt || H->frame-listedAt>=60*30){
            listedAt=H->frame?H->frame:1;
            char line[900];int n=snprintf(line,sizeof line,"air: aircraft on the map:");
            for(int k=0;k<count && k<256 && n<(int)sizeof line-120;k++)if(vehicle_ok(list[k]) && bm_air_profile(vehicle_name(list[k]))){
                float *pos=vehicle_pos(list[k],NULL);char *crew=vehicle_occupied(list[k])?vehicle_occupant(list[k]):NULL;
                n+=snprintf(line+n,sizeof line-n," %s at (%.0f, %.0f) US:%s RU:%s%s%s;",vehicle_name(list[k]),pos?pos[0]:0,pos?pos[2]:0,
                    fleet_reason(list[k],1),fleet_reason(list[k],2),fleet_claimed(list[k],-1)?" claimed":"",crew&&player_exists(crew)?(is_bot(crew)?" bot aboard":" human aboard"):"");
            }
            bot_log("%s",line);
            const char *names[32];int counts[32],kinds=0;
            for(int k=0;k<count && k<256;k++)if(vehicle_ok(list[k])){
                const char *nm=vehicle_name(list[k]);int f=-1;
                for(int m=0;m<kinds;m++)if(!strcmp(names[m],nm)){f=m;break;}
                if(f<0 && kinds<32){names[kinds]=nm;counts[kinds]=0;f=kinds++;}
                if(f>=0)counts[f]++;
            }
            n=snprintf(line,sizeof line,"vehicles: %d on the map:",count);
            for(int m=0;m<kinds && n<(int)sizeof line-48;m++)n+=snprintf(line+n,sizeof line-n," %s x%d%s",names[m],counts[m],bm_air_profile(names[m])?" (aircraft)":"");
            bot_log("%s",line);
        }
    }
}
static void enhanced_diagnostics(void)
{
    char path[MAX_PATH+64],name[64];snprintf(name,sizeof name,"diagnostics_%d.txt",H->gamePort);
    botmod_file(path,sizeof path,name);FILE *out=fopen(path,"w");
    bot_log("diagnostics: %s; report=%s%s",BM_ENHANCED_VERSION,path,out?"":" (could not write)");
    if(out)fprintf(out,"%s\nmap=%s mode=%s frame=%u round=%ld cache=%ld navigation_pending=%d\n",
        BM_ENHANCED_VERSION,g_level,g_modeName,H->frame,g_roundEpoch,g_compEpoch,nav_pending_paths());
    NavGrid *grid=*nav_slot();
    if(out && grid)fprintf(out,"navigation=%s phase=%d dimensions=%dx%d cell=%.2f bounds_x=%.1f..%.1f bounds_z=%.1f..%.1f stages=%d\n",
        grid->level,grid->phase,grid->w,grid->h,grid->cell,grid->x0,grid->x0+grid->w*grid->cell,grid->z0,grid->z0+grid->h*grid->cell,g_enhanced.driveStages);
    if(out && g_navExpansion)fprintf(out,"background_navigation=%s phase=%d cursor=%d active_grid_retained=1\n",g_navExpansion->level,g_navExpansion->phase,g_navExpansion->cursor);
    const char *limits="Missile selection/lock and incoming-missile entity bindings: UNBOUND. Primary cannon/rocket control and damage-triggered countermeasures retained. Deployment-menu recipes require a human example in the current round. Rush arm/disarm/destroy event payloads: UNBOUND. Human deployment observations or admin Rush synchronization advance route sectors; no simulated objective completion.";
    bot_log("diagnostics: %s",limits);
    if(out)fprintf(out,"%s\nlock_type_notifications=%u missile_type_notifications=%u (counts only; payloads unbound)\n",limits,g_lockMessageCount,g_missileMessageCount);
    if(out && g_mode==BM_MODE_RUSH){
        fprintf(out,"rush_stage=%d stages=%d native_state_bound=0 confirmed_events=%u revision=%u\n",g_rushStage,g_rush.stages,g_rush.confirmedEvents,g_rush.revision);
        for(int k=1;k<=g_rush.stages*2;k++)fprintf(out,"rush_objective=%d status=%d active=%d operator_present=%d\n",k,g_rush.objective[k].status,bm_rush_active(&g_rush,k),g_rush.objective[k].operatorPlayer!=NULL);
    }
    for(int team=1;team<=2;team++){
        int alive=0;for(int i=0;i<H->botCount;i++)if(player_team(H->bots[i])==team && *(void **)(H->bots[i]+OFF_PLAYER_SOLDIER))alive++;
        int air,jets,ground,total,available=0,queued=0;fleet_counts(team,&air,&jets,&ground,&total);
        bot_log("diagnostics: team=%d alive=%d MpSoldier=%p HQ-options=%d selected-option=%d",team,alive,H->teamBlueprint[team],g_hqOptionCount[team],g_hqOptionCursor[team]);
        if(out)fprintf(out,"spawn_team=%d alive=%d blueprint_known=%d HQ_options=%d selected_option=%d\n",team,alive,H->teamBlueprint[team]!=NULL,g_hqOptionCount[team],g_hqOptionCursor[team]);
        char **list;int count=vehicle_list(&list);
        for(int k=0;k<count;k++)if(!strcmp(fleet_reason(list[k],team),"available") && !fleet_claimed(list[k],-1))available++;
        for(int i=0;i<H->botCount;i++)if(player_team(H->bots[i])==team && g_fleet[i].vehicle && !g_drive[i].phase && !g_air[i].phase)queued++;
        bot_log("diagnostics: team=%d bots=%d ground=%d air=%d jets=%d unclaimed-usable=%d approach-queue=%d HQ-valid=%d HQ-spawn-team=%d",team,total,ground,air,jets,available,queued,cse_valid(g_fleetBase[team]),spawn_team(g_fleetBase[team]));
        if(out)fprintf(out,"team=%d bots=%d ground=%d air=%d jets=%d available=%d queued=%d HQ_valid=%d HQ_spawn_team=%d HQ=(%.1f %.1f %.1f)\n",team,total,ground,air,jets,available,queued,cse_valid(g_fleetBase[team]),spawn_team(g_fleetBase[team]),g_fleetBasePos[team][0],g_fleetBasePos[team][1],g_fleetBasePos[team][2]);
    }
    for(int i=0;i<H->botCount;i++){
        char *pl=H->bots[i],*s=*(char **)(pl+OFF_PLAYER_SOLDIER);BotBrain *b=bot_brain(i);VehicleDiagnostic *d=&g_vehicleDiag[i];
        const float *p=s?(float *)(s+OFF_SOLDIER_SPAWN_POSITION):NULL;
        unsigned queued=g_fleet[i].queuedAt?H->frame-g_fleet[i].queuedAt:0;
        int dutyRole,dutyFlag=director_target(i,&dutyRole);
        if(out)fprintf(out,"director_flag=%d director_role=%d assigned_seconds=%.1f arrived_seconds=%.1f\n",dutyFlag,dutyRole,(H->frame-g_director.unit[i].assignedAt)/30.0f,g_director.unit[i].arrivedAt?(H->frame-g_director.unit[i].arrivedAt)/30.0f:0);
        bot_log("diagnostics: bot=%d team=%d alive=%d task=%d flag=%d nav=%d ground=%d air=%d armor-job=%d queued=%.1fs decision=%s",i,player_team(pl),s!=NULL,b->task,b->targetFlag,bot_nav(i)->mode,g_drive[i].phase,g_air[i].phase,g_armor[i].job,queued/30.0f,d->reason);
        if(out)fprintf(out,"bot=%d team=%d alive=%d task=%d flag=%d nav=%d ground=%d air=%d armor=%d queue_seconds=%.1f pos=(%.1f %.1f %.1f) weapon=%s decisions=%u failures=%u last=%s vehicle=%s\n",i,player_team(pl),s!=NULL,b->task,b->targetFlag,bot_nav(i)->mode,g_drive[i].phase,g_air[i].phase,g_armor[i].job,queued/30.0f,p?p[0]:0,p?p[1]:0,p?p[2]:0,infantry_weapon(i)->label,d->changes,d->failures,d->reason,d->vehicle);
        if(out)fprintf(out,"  motion=(%.2f %.2f %.2f) squad=%d squad_wait_seconds=%.1f flank_active=%d flank_pending=%d pickup_seconds=%.1f\n",H->botMove[i][0],H->botMove[i][1],H->botMove[i][2],player_squad(pl),g_spawnJoinWait[i]?(H->frame-g_spawnJoinWait[i])/30.0f:0,g_combatMove[i].until>H->frame,g_combatMove[i].pending,g_drive[i].pickupUntil>H->frame?(g_drive[i].pickupUntil-H->frame)/30.0f:0);
        if(out)fprintf(out,"  covered=%d road_stage=%d candidate=%d path_points=%d next=%d partial=%d landing=%d traffic_wait=%d\n",
            p && nav_covers(grid,p[0],p[2],0),g_drive[i].stageActive,g_drive[i].stageCandidate,g_drive[i].n,g_drive[i].next,g_drive[i].partial,g_air[i].landing.phase,g_drive[i].trafficWait);
        ActivityState *activity=&g_activity[i];
        if(out)fprintf(out,"  activity_still_seconds=%.1f travel_stall_seconds=%.1f absent_seconds=%.1f escape_seconds=%.1f recovery_attempts=%d\n",
            activity->progressAt?(H->frame-activity->progressAt)/30.0f:0,
            activity->travelAt?(H->frame-activity->travelAt)/30.0f:0,
            activity->absentAt?(H->frame-activity->absentAt)/30.0f:0,
            activity->escapeUntil>H->frame?(activity->escapeUntil-H->frame)/30.0f:0,activity->attempts);
    }
    for(int k=0;k<32;k++)if(g_deployChoice[k].target){
        int valid=deployment_function(g_deployChoice[k].target,g_deployChoice[k].vt)==g_deployChoice[k].function;
        if(out)fprintf(out,"deployment_recipe=%d team=%d aircraft=%s menu_only=%d valid=%d failures=%d\n",k,g_deployChoice[k].team,g_deployChoice[k].profile->name,g_deployChoice[k].virtualSpawn,valid,g_deployChoice[k].failures);
    }
    char **vehicles;int count=vehicle_list(&vehicles);
    for(int k=0;k<count;k++)if(vehicle_ok(vehicles[k])){
        char *v=vehicles[k];float *p=vehicle_pos(v,NULL);
        if(out)fprintf(out,"vehicle=%s pos=(%.1f %.1f %.1f) free=%d US=%s RU=%s claimed=%d\n",vehicle_name(v),p?p[0]:0,p?p[1]:0,p?p[2]:0,free_seat_count(v),fleet_reason(v,1),fleet_reason(v,2),fleet_claimed(v,-1));
    }
    if(out){fclose(out);bot_log("diagnostics: saved %s",path);}
}
