// Aircraft dispatch is a team duty, processed before ground requisitions.
// Ground walks and the rotating bot order cannot consume every available bot
// before pilots are chosen. Counts include pending/walking/deployment claims.
static void fleet_air_dispatch(void)
{
    static unsigned checkedAt;static LONG epoch;
    if(epoch!=g_roundEpoch){checkedAt=0;epoch=g_roundEpoch;}
    if(!mode_uses_vehicles() || !level_ready() || !g_enhanced.driving || !g_enhanced.returnToBase || g_driveOff || !g_vehInput ||
       (!g_enhanced.air.jets && !g_enhanced.air.helicopters) || (checkedAt && H->frame-checkedAt<15))return;
    checkedAt=H->frame?H->frame:1;
    char **vehicles;int nv=vehicle_list(&vehicles);if(!nv)return;
    for(int team=1;team<=2;team++)for(int duty=0;duty<6;duty++){
        int air,jets,ground,total;fleet_counts(team,&air,&jets,&ground,&total);
        if(air>=6 || air>=g_enhanced.air.pilotsPerTeam)break;
        char *chosen=NULL;int pilot=-1,role=0;float best=INFINITY;
        for(int k=0;k<nv && k<256;k++){
            char *v=vehicles[k];const BMAirProfile *profile=bm_air_profile(vehicle_name(v));
            if(!profile || !fleet_air_slot(team,profile) || strcmp(fleet_reason(v,team),"available") || fleet_claimed(v,-1))continue;
            float *position=vehicle_pos(v,NULL);if(!position)continue;
            for(int i=0;i<H->botCount;i++){
                char *pl=H->bots[i],*s=*(char **)(pl+OFF_PLAYER_SOLDIER);BotBrain *b=bot_brain(i);
                int groundWalk=(g_drive[i].phase==1 || g_drive[i].phase==5) && !g_drive[i].manual;
                int groundQueue=g_fleet[i].vehicle && g_fleet[i].role==FLEET_GROUND && !g_drive[i].phase;
                if(!s || !b->enabled || *(float *)(s+OFF_SOLDIER_HEALTH)<=0 || player_team(pl)!=team || player_vehicle(pl) || g_air[i].phase ||
                   (g_drive[i].phase && !groundWalk) || (g_fleet[i].vehicle && !groundQueue && !groundWalk) ||
                   g_gliding[i] || b->commander || b->prevCommander ||
                   (b->task!=TASK_NONE && (b->task!=TASK_FIGHT || b->prevTask!=TASK_NONE)) ||
                   H->frame<g_airCooldown[i] || (g_hitAt[i] && H->frame-g_hitAt[i]<3*30))continue;
                float *origin=(float *)(s+OFF_SOLDIER_POSITION);
                if(b->task==TASK_FIGHT){char *enemy=g_fightTarget[i];char *body=enemy && player_exists(enemy)?*(char **)(enemy+OFF_PLAYER_SOLDIER):NULL;
                    if(body && dist2(origin,(float *)(body+OFF_SOLDIER_SPAWN_POSITION))<60*60)continue;}
                float distance=sqrtf(dist2(origin,position));
                if(distance>(g_enhanced.returnToBase?g_enhanced.returnMeters:g_enhanced.air.findRadius) ||
                   fleet_route_blocked(v,team,origin) ||
                   (g_fleet[i].failedVehicle==v && H->frame<g_fleet[i].failedUntil))continue;
                float score=distance+fabsf(origin[1]-position[1])*2+
                    (groundWalk || groundQueue?100:0)+(profile->kind==BM_AIR_JET?(jets?1200:-1000):profile->kind==BM_AIR_ATTACK?-450:0);
                if(score<best){best=score;chosen=v;pilot=i;role=profile->kind==BM_AIR_JET?FLEET_JET:FLEET_HELI;}
            }
        }
        if(pilot<0)break;
        BotBrain *b=bot_brain(pilot);if(b->task==TASK_FIGHT)end_fight(pilot,b,"assigned available aircraft for team objectives");
        if(g_drive[pilot].phase)drive_stop(pilot,"walk transfers to available aircraft duty",10);
        nav_cancel_path(pilot);memset(&g_vehicleApproach[pilot],0,sizeof g_vehicleApproach[pilot]);
        FleetReservation *r=&g_fleet[pilot];r->vehicle=chosen;r->role=role;
        r->until=H->frame+30*30;r->queuedAt=H->frame?H->frame:1;
        g_activity[pilot].progressAt=g_activity[pilot].travelAt=H->frame?H->frame:1;g_activity[pilot].attempts=0;   // Caspian V3 (see abandoned_dispatch.inc)
        diagnostic_note(pilot,"team assigns nearest eligible pilot before ground fleet",chosen);
    }
}
