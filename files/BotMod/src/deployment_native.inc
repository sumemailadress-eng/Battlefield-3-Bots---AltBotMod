// Replay only a request observed at the beta's existing spawn call hook, after
// confirming its human pilot result. Unknown classes are never enumerated or
// probed. The exact target, vtable, callback, argument, team and round are held.
static unsigned deployment_function(void *target,unsigned vt)
{
    if(!target || IsBadReadPtr(target,4) || *(unsigned *)target!=vt ||
       vt<0x00400000 || vt>=0x02800000 || IsBadReadPtr((void *)(uintptr_t)(vt+0x6c),4))return 0;
    unsigned f=*(unsigned *)(uintptr_t)(vt+0x6c);
    return f>=0x00400000 && f<0x02800000?f:0;
}
static void deployment_request(void *target,unsigned function,char *bot,unsigned arg)
{
    ((CseSpawn_t)(uintptr_t)function)(target,bot,0xffffffff,arg,0);
}
static char deployment_squad_request(char *bot,char *mate)
{
    typedef char (__attribute__((thiscall)) *SpawnOnSquad_t)(void *,void *);
    return ((SpawnOnSquad_t)0x01185b50)(bot+*(int *)0x0285bad8,mate);
}
static void deployment_observe(void *target,char *player,unsigned arg)
{
    if(!g_enhanced.air.deploymentMenu || !level_ready() || !target || !player ||
       is_bot(player) || IsBadReadPtr(target,4))return;
    unsigned vt=*(unsigned *)target,function=deployment_function(target,vt);
    int team=player_team(player);
    if(!function || team<1 || team>2)return;
    for(int k=0;k<32;k++)if(!g_deployPending[k].at || H->frame-g_deployPending[k].at>90){
        g_deployPending[k].target=target;g_deployPending[k].player=player;
        g_deployPending[k].beforeSoldier=*(char **)(player+OFF_PLAYER_SOLDIER);
        g_deployPending[k].beforeVehicle=player_vehicle(player);
        g_deployPending[k].vt=vt;g_deployPending[k].function=function;
        g_deployPending[k].arg=arg;g_deployPending[k].team=team;
        g_deployPending[k].at=H->frame?H->frame:1;g_deployPending[k].epoch=g_roundEpoch;
        break;
    }
}
static void deployment_tick(void)
{
    if(!level_ready())return;
    for(int k=0;k<32;k++)if(g_deployPending[k].at){
        if(g_deployPending[k].epoch!=(unsigned)g_roundEpoch || H->frame-g_deployPending[k].at>90){g_deployPending[k].at=0;continue;}
        if(H->frame-g_deployPending[k].at<10)continue;
        char *pl=g_deployPending[k].player;
        if(!player_exists(pl) || player_team(pl)!=g_deployPending[k].team){g_deployPending[k].at=0;continue;}
        char *s=*(char **)(pl+OFF_PLAYER_SOLDIER),*v=player_vehicle(pl);
        if(!s || !vehicle_ok(v))continue;
        const BMAirProfile *p=bm_air_profile(vehicle_name(v));
        int virtualSpawn=v!=g_deployPending[k].target;
        if(!p || (virtualSpawn && (s==g_deployPending[k].beforeSoldier ||
            v==g_deployPending[k].beforeVehicle || vehicle_seat_of(v,pl)!=0))){g_deployPending[k].at=0;continue;}
        if(deployment_function(g_deployPending[k].target,g_deployPending[k].vt)!=g_deployPending[k].function){g_deployPending[k].at=0;continue;}
        for(int n=0;n<32;n++)if(!g_deployChoice[n].target ||
             (g_deployChoice[n].target==g_deployPending[k].target && g_deployChoice[n].arg==g_deployPending[k].arg && g_deployChoice[n].team==g_deployPending[k].team)){
            g_deployChoice[n].target=g_deployPending[k].target;g_deployChoice[n].vehicle=v;
            g_deployChoice[n].vt=g_deployPending[k].vt;g_deployChoice[n].function=g_deployPending[k].function;
            g_deployChoice[n].arg=g_deployPending[k].arg;g_deployChoice[n].team=g_deployPending[k].team;
            g_deployChoice[n].profile=p;g_deployChoice[n].epoch=g_roundEpoch;g_deployChoice[n].virtualSpawn=virtualSpawn;
            g_deployChoice[n].failures=0;
            bot_log("deployment: learned team %d %s %s request for this round",g_deployPending[k].team,p->name,virtualSpawn?"menu-only":"live-vehicle");break;
        }
        g_deployPending[k].at=0;
    }
    for(int i=0;i<H->botCount;i++)if(g_deployActive[i].at){
        int k=g_deployActive[i].choice;char *pl=H->bots[i];
        if(k<0 || k>=32 || g_deployActive[i].epoch!=(unsigned)g_roundEpoch ||
           player_team(pl)!=g_deployActive[i].team){memset(&g_deployActive[i],0,sizeof g_deployActive[i]);continue;}
        char *s=*(char **)(pl+OFF_PLAYER_SOLDIER),*v=player_vehicle(pl);
        const BMAirProfile *p=vehicle_ok(v)?bm_air_profile(vehicle_name(v)):NULL;
        if(s && p && p==g_deployChoice[k].profile && vehicle_seat_of(v,pl)==0){
            g_lastSpawnedBot[i]=pl;g_lastSpawnedAt[i]=H->frame;
            memset(&g_deployActive[i],0,sizeof g_deployActive[i]);
            if(air_claim(i,v,1,bot_brain(i)->targetFlag)){
                g_deployChoice[k].vehicle=v;g_deployChoice[k].failures=0;
                diagnostic_note(i,"deployment-menu pilot confirmed",v);
            }else diagnostic_note(i,"deployment pilot controller unavailable",v);
        }else if(s || H->frame-g_deployActive[i].at>60){
            g_deployChoice[k].failures++;
            g_deployChoice[k].retryAt=H->frame+(g_deployChoice[k].failures>=3?180:30)*30;
            diagnostic_note(i,s?"deployment recipe returned ordinary body; retained":"deployment recipe unavailable; ordinary spawn next",v);
            if(s){g_lastSpawnedBot[i]=pl;g_lastSpawnedAt[i]=H->frame;}
            memset(&g_deployActive[i],0,sizeof g_deployActive[i]);
        }
    }
}
static int deployment_spawn(char *bot)
{
    int i=bot_index(bot),team=player_team(bot);
    if(i<0 || team<1 || team>2 || !level_ready() || !g_enhanced.driving || g_driveOff ||
       !g_vehInput || !bot_brain(i)->enabled)return 0;
    if(g_deployActive[i].at)return 1; // Await callback completion; never create a second body.
    if(*(char **)(bot+OFF_PLAYER_SOLDIER))return 0;
    if(g_enhanced.air.deploymentMenu)for(int k=0;k<32;k++){
        const BMAirProfile *p=g_deployChoice[k].profile;void *target=g_deployChoice[k].target;
        if(!target || !p || g_deployChoice[k].epoch!=(unsigned)g_roundEpoch ||
           g_deployChoice[k].team!=team || H->frame<g_deployChoice[k].retryAt ||
           !bm_air_allowed(&g_enhanced.air,p) ||
           deployment_function(target,g_deployChoice[k].vt)!=g_deployChoice[k].function)continue;
        int virtualSpawn=g_deployChoice[k].virtualSpawn,transport=p->kind==BM_AIR_TRANSPORT;
        char *v=virtualSpawn?NULL:g_deployChoice[k].vehicle;
        if(!virtualSpawn && (!vehicle_ok(v) || !bm_air_transform_valid(vehicle_xf(v)) ||
           bm_air_profile(vehicle_name(v))!=p ||
           (transport?free_seat_count(v)<=1:!driver_seat_free(v)) ||
           (!transport && human_near(vehicle_xf(v)+12,g_enhanced.humanRadius))))continue;
        if(!virtualSpawn && vehicle_occupied(v)){
            char *crew=vehicle_occupant(v);
            if(!crew || !player_exists(crew) || player_team(crew)!=team)continue;
        }
        if((virtualSpawn || !transport) && !fleet_air_slot(team,p))continue;
        if(!*(void **)(bot+OFF_PLAYER_BLUEPRINT)){
            if(!H->teamBlueprint[team])continue;
            ((SetPtr_t)ADDR_PLAYER_SET_BLUEPRINT)(bot,H->teamBlueprint[team]);
        }
        // Keep one request per recipe in flight, and count the pending pilot.
        g_deployChoice[k].retryAt=H->frame+15*30;apply_kit(bot);
        g_deployActive[i].at=H->frame?H->frame:1;g_deployActive[i].epoch=g_roundEpoch;
        g_deployActive[i].choice=k;g_deployActive[i].team=team;
        deployment_request(target,g_deployChoice[k].function,bot,g_deployChoice[k].arg);
        if(!virtualSpawn && *(char **)(bot+OFF_PLAYER_SOLDIER) && player_vehicle(bot)==v){
            g_lastSpawnedBot[i]=bot;g_lastSpawnedAt[i]=H->frame;memset(&g_deployActive[i],0,sizeof g_deployActive[i]);
            if(vehicle_seat_of(v,bot)==0)air_claim(i,v,1,bot_brain(i)->targetFlag);else air_ride(i,v);
        }
        diagnostic_note(i,virtualSpawn?"deployment-menu request pending":"live aircraft deployment requested",v);
        return 1;
    }
    // Flying transport deployment uses BF3's already verified squad spawn.
    int squad=player_squad(bot);if(squad<=0)return 0;
    char *pm=player_manager();void **arr=pm?*(void ***)(pm+0xac):NULL;unsigned n=pm?*(unsigned *)(pm+8):0;
    for(unsigned k=0;k<n&&k<64;k++){char *mate=arr[k];if(!mate || mate==bot || player_team(mate)!=team || player_squad(mate)!=squad)continue;
        char *v=player_vehicle(mate),*ms=*(char **)(mate+OFF_PLAYER_SOLDIER);
        if(!ms || !vehicle_ok(v) || free_seat_count(v)<=1 || squadmate_spawned_on_recently(mate))continue;
        const BMAirProfile *p=bm_air_profile(vehicle_name(v));
        if(!p || p->kind!=BM_AIR_TRANSPORT || !bm_air_allowed(&g_enhanced.air,p))continue;
        apply_kit(bot);char ok=deployment_squad_request(bot,mate);
        if(ok&&*(char **)(bot+OFF_PLAYER_SOLDIER)){squadmate_note_spawn(mate);g_lastSpawnedBot[i]=bot;g_lastSpawnedAt[i]=H->frame;if(player_vehicle(bot))air_ride(i,player_vehicle(bot));return 1;}
    }return 0;
}
