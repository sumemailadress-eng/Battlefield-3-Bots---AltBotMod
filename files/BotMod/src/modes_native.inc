// Native spawn/input/navigation adapter. FunBots data supplies routing hints.
// Objective state is confirmed separately; input duration never fabricates it.
static const BMModeProfile *mode_profile(const char *level,const char *mode)
{
    if(!strcmp(mode,"TeamDeathMatchC0"))mode="TeamDeathMatch0";
    for(unsigned k=0;k<sizeof g_modeProfiles/sizeof g_modeProfiles[0];k++)
        if(!strcmp(level,g_modeProfiles[k].level)&&!strcmp(mode,g_modeProfiles[k].mode))return &g_modeProfiles[k];
    // AltBotMod V15: Gun Master is played on the TDM play area (Fun-Bots recorded no Gun Master layout for the Close
    // Quarters and Aftermath levels, the only ones the server runs it on).
    if(bm_mode_gun_master(mode))return mode_profile(level,"TeamDeathMatch0");
    return NULL;
}
static void mode_reset(void)
{
    g_modeProfile=NULL;g_modeSpawnCount=0;g_rushStage=1;g_rushAdvanceAt=0;g_rushBeforeStage=0;g_rushBadDeaths=0;memset(g_mcomDestroyedBy,0,sizeof g_mcomDestroyedBy);
    bm_rush_reset(&g_rush,1,2,H->frame);
    memset(g_modeSpawns,0,sizeof g_modeSpawns);memset(g_modeTeamSpawn,0,sizeof g_modeTeamSpawn);
    memset(g_modeFailedSpawn,0,sizeof g_modeFailedSpawn);memset(g_modeSpawnOk,0,sizeof g_modeSpawnOk);memset(g_modeSpawnNo,0,sizeof g_modeSpawnNo);memset(g_rushBlind,0,sizeof g_rushBlind);
    memset(g_modeBotSlot,0,sizeof g_modeBotSlot);memset(g_modeEarlyDeaths,0,sizeof g_modeEarlyDeaths);
    memset(g_modeSpawnLandKnown,0,sizeof g_modeSpawnLandKnown);memset(g_mcomArmAt,0,sizeof g_mcomArmAt);memset(g_mcomNearSince,0,sizeof g_mcomNearSince);memset(g_rushMsgCount,0,sizeof g_rushMsgCount);
    memset(g_deployPending,0,sizeof g_deployPending);memset(g_deployChoice,0,sizeof g_deployChoice);
    memset(g_deployActive,0,sizeof g_deployActive);
    memset(g_modeBot,0,sizeof g_modeBot);memset(g_modePending,0,sizeof g_modePending);
    g_modeGoalProbeFrame=~0u;g_modeGoalProbeNext=0;
}
static int mode_inside(const float *p,float margin)
{
    if(!g_modeProfile||!p)return 0;
    for(int k=0;k<3;k++)if(!isfinite(p[k])||p[k]<g_modeProfile->low[k]-margin||p[k]>g_modeProfile->high[k]+margin)return 0;
    return 1;
}
static int mode_base_present(int stage,int team)
{
    return g_modeProfile && stage>=1 && stage<=g_modeProfile->stages && stage<=8 && team>=1 && team<=2 &&
        (g_modeProfile->baseMask[stage]&(1u<<team))!=0;
}
// AltBotMod V14: a mode with no recorded layout for this map (a Team / Squad Deathmatch the profiles do not list) used
// to leave the level "not ready yet (0 flags, hq mask 0)" for good. Now the layout is made from the spawn points the
// level has: the play area round them, and the spawn points as patrol spots. Rush still needs a recorded layout
// (the M-COM positions).
static BMModeProfile g_modeAutoProfile;
static char g_modeAutoLevel[64],g_modeAutoMode[64];
static const BMModeProfile *mode_auto_profile(const char *level,const char *mode)
{
    static char logged[160];char key[160];snprintf(key,sizeof key,"%s %s",level,mode);   // (each outcome said once per level and mode)
    enum BMMode m=mode_kind(mode);   // V15: a round fallen back to team play counts as TDM
    if(m!=BM_MODE_TDM && m!=BM_MODE_SQDM){
        if(strcmp(logged,key)){snprintf(logged,sizeof logged,"%s",key);
            bot_log("mode: no recorded layout for %s %s - %s",level,mode[0]?mode:"(no mode)",
                    !mode[0]?"the server has not said which game mode it runs (is RCON reachable? see the chat: lines)":
                    m==BM_MODE_RUSH?"Rush needs the recorded M-COM positions; bots cannot play it here":"this mode is not supported");}
        return NULL;
    }
    char *spawns[256];int n=scan_objects(0x025bbbc0,spawns,256),used=0;
    float pts[32][3],lo[3]={1e9f,1e9f,1e9f},hi[3]={-1e9f,-1e9f,-1e9f};
    for(int k=0;k<n;k++){
        float *q=cse_valid(spawns[k])?spawn_data_pos(spawns[k]):NULL;
        if(!q || !isfinite(q[0]) || !isfinite(q[1]) || !isfinite(q[2]) || (fabsf(q[0])<12 && fabsf(q[2])<12))continue;
        if(used<32)memcpy(pts[used],q,12);
        used++;
        for(int a=0;a<3;a++){if(q[a]<lo[a])lo[a]=q[a];if(q[a]>hi[a])hi[a]=q[a];}
    }
    if(!used){
        snprintf(key,sizeof key,"none %s %s",level,mode);
        if(strcmp(logged,key)){snprintf(logged,sizeof logged,"%s",key);bot_log("mode: no recorded layout for %s %s and no spawn points found yet - waits",level,mode);}
        return NULL;
    }
    memset(&g_modeAutoProfile,0,sizeof g_modeAutoProfile);
    snprintf(g_modeAutoLevel,sizeof g_modeAutoLevel,"%s",level);snprintf(g_modeAutoMode,sizeof g_modeAutoMode,"%s",mode);
    g_modeAutoProfile.level=g_modeAutoLevel;g_modeAutoProfile.mode=g_modeAutoMode;
    for(int a=0;a<3;a++){g_modeAutoProfile.low[a]=lo[a]-(a==1?40:60);g_modeAutoProfile.high[a]=hi[a]+(a==1?60:60);}
    int kept=used<32?used:32;
    for(int k=0;k<32;k++)memcpy(g_modeAutoProfile.patrol[k],pts[k%kept],12);
    snprintf(key,sizeof key,"auto %s %s",level,mode);
    if(strcmp(logged,key)){snprintf(logged,sizeof logged,"%s",key);
        bot_log("mode: no recorded layout for %s %s - uses the %d spawn point(s) the level has (play area %.0f..%.0f / %.0f..%.0f)",
                level,mode,used,g_modeAutoProfile.low[0],g_modeAutoProfile.high[0],g_modeAutoProfile.low[2],g_modeAutoProfile.high[2]);}
    return &g_modeAutoProfile;
}
// AltBotMod V15/V16: each spawn entity once per level and mode (the first 16): its team (data+0x6C), enabled flag
// (data+0x60) and recorded position - shows the live picture next to what the level data says.
static void mode_spawn_dump(const char *level,const char *mode,int k,char *e)
{
    static char done[160];static int skip;char key[160];snprintf(key,sizeof key,"%s %s",level,mode);
    if(!k){skip=!strcmp(done,key);snprintf(done,sizeof done,"%s",key);}
    if(skip)return;
    unsigned char *d=cse_valid(e)?*(unsigned char **)(e+0x14):NULL;
    if(!d){bot_log("spawn-dump: %s #%d %p (no readable data)",key,k,(void *)e);return;}
    bot_log("spawn-dump: %s #%d %p team %d enabled %d at (%.1f %.1f %.1f)",key,k,(void *)e,spawn_team(e),d[0x60],
            ((float *)d)[16],((float *)d)[17],((float *)d)[18]);
}
static int mode_discover(const char *level,const char *mode,float *low,float *high)
{
    g_modeProfile=mode_profile(level,mode);
    // AltBotMod V15: a round fallen back to team play walks the level's recorded play area of the nearest mode - Rush's
    // for a Squad Rush, Conquest's for Capture the Flag / Air Superiority - before guessing one from spawn points
    // (whose recorded positions are often local to their prefab, so a box round them can be anywhere).
    if(!g_modeProfile && mode_kind(mode)!=bm_mode(mode)){
        const char *kin[]={bm_mode(mode)==BM_MODE_RUSH?"RushLarge0":"ConquestLarge0","ConquestSmall0","ConquestAssaultLarge0","TeamDeathMatch0"};
        for(int k=0;k<4 && !g_modeProfile;k++)if((g_modeProfile=mode_profile(level,kin[k])))bot_log("mode: %s %s plays on the recorded %s play area",level,mode,kin[k]);
    }
    if(!g_modeProfile)g_modeProfile=mode_auto_profile(level,mode);
    if(!g_modeProfile)return 0;
    memcpy(low,g_modeProfile->low,12);memcpy(high,g_modeProfile->high,12);
    g_modeSpawnCount=scan_objects(0x025bbbc0,g_modeSpawns,256);
    int valid=0,teams=bm_mode_teams(mode_kind(mode)),count[5]={0,0,0,0,0},none=0,anywhere=0;
    float sum[3][3];memset(sum,0,sizeof sum);memset(g_modeTeamBase,0,sizeof g_modeTeamBase);g_modeSpawnsLoose=0;
    for(int k=0;k<g_modeSpawnCount;k++){
        int t=spawn_team(g_modeSpawns[k]);float *p=cse_valid(g_modeSpawns[k])?spawn_data_pos(g_modeSpawns[k]):NULL;
        if(p && (t==0 || (t>=1 && t<=teams)))anywhere++;
        if(k<16)mode_spawn_dump(level,mode,k,g_modeSpawns[k]);   // V15: for the team offset (data+8 is not reliably the team)
        if(!p || !mode_inside(p,120))continue;
        if(t>=1&&t<=teams){valid|=1<<(t-1);count[t]++;if(t<=2)for(int a=0;a<3;a++)sum[t][a]+=p[a];}
        else if(!t)none++;
    }
    // AltBotMod V9: Team Deathmatch spawn points carry no team (V8 waited for team 1 / 2 ones and TDM never started:
    // "not ready yet (0 flags, hq mask 0)"). Team-less points inside the play area serve every team; the engine refuses
    // a soldier where it will not have one, and that point is then left alone for that team (mode_spawn).
    if(mode_kind(mode)!=BM_MODE_RUSH && none && valid!=(1<<teams)-1)valid=(1<<teams)-1;
    // AltBotMod V15: on 2026-10-11 V14 never started TDM on Tehran Highway (its 2 spawn entities - one a team - lie
    // outside the recorded play area by their recorded position) nor Rush on Grand Bazaar (9 of 11 without a team):
    // "not ready yet" for 47 minutes. Fun-Bots spawns a bot on a spawn entity of its team without asking where it is,
    // and the engine refuses a wrong one. So when the play area holds none for a team, the bots try the level's
    // spawn entities (team or no team) and keep the ones that work (mode_spawn: refusals rest, Rush checks the base).
    if(valid!=(1<<teams)-1 && anywhere){g_modeSpawnsLoose=1;valid=(1<<teams)-1;}
    for(int t=1;t<=2;t++)if(count[t])for(int a=0;a<3;a++)g_modeTeamBase[t][a]=sum[t][a]/count[t];   // V15: Capture the Flag raids
    if(bm_mode(mode)==BM_MODE_CQ && g_hqCpKnown[1] && g_hqCpKnown[2]){memcpy(g_modeTeamBase[1],g_hqCpPos[1],12);memcpy(g_modeTeamBase[2],g_hqCpPos[2],12);}   // V15: a flag mode fallen back: its HQs (found this level by the Conquest discovery that ran first)
    bot_log("tdm: scan found %d spawn entities on %s %s: team1 %d, team2 %d, no team %d -> teams usable %x%s",
            g_modeSpawnCount,level,mode,count[1],count[2],none,valid,g_modeSpawnsLoose?" (V15: none in the play area for a team - bots try all of them, the engine picks)":"");
    return valid;
}
static int mode_has_spawns(void)
{
    for(int t=1;t<=mode_teams();t++)if(cse_valid(g_modeTeamSpawn[t].entity))return 1;
    if(g_modeSpawnsLoose)for(int k=0;k<g_modeSpawnCount;k++){int t=spawn_team(g_modeSpawns[k]);if((t==0||(t>=1&&t<=mode_teams())) && cse_valid(g_modeSpawns[k]))return 1;}
    for(int k=0;k<g_modeSpawnCount;k++){
        int t=spawn_team(g_modeSpawns[k]);
        if(((t>=1&&t<=mode_teams()) || (!t && g_mode!=BM_MODE_RUSH)) && cse_valid(g_modeSpawns[k]) && mode_inside(spawn_data_pos(g_modeSpawns[k]),120))return 1;
    }
    return 0;
}
static int objective_count(void)
{
    // V15: Squad Rush has one M-COM per sector (kept at the first slot of each sector's pair).
    return mode_is_cq()?g_flagCount:g_mode==BM_MODE_RUSH && g_modeProfile?(mode_is_squad_rush()?1:2):0;
}
static const float *objective_pos(int f)
{
    if(mode_is_cq())return flag_pos(f);
    int k=(g_rushStage-1)*2+1+f;
    return g_modeProfile && f>=0 && f<2 && k<=16 && g_modeProfile->mcom[k].present?g_modeProfile->mcom[k].pos:NULL;
}
static int objective_wanted(int team,int f)
{
    if(f<0 || f>=objective_count())return 0;
    if(mode_is_cq())return flag_owner(f)!=team;
    int k=(g_rushStage-1)*2+1+f;
    return objective_pos(f)!=NULL && bm_rush_active(&g_rush,k);
}
static int mode_vehicle_target(int i,const float *position)
{
    if(mode_is_cq())return -1;
    int team=player_team(H->bots[i]),best=-1;float score=INFINITY;
    for(int f=0;f<objective_count();f++)if(objective_wanted(team,f)){
        int cover=0;for(int j=0;j<H->botCount;j++)if(j!=i && player_team(H->bots[j])==team &&
            (g_drive[j].phase || g_air[j].phase) && bot_brain(j)->targetFlag==f)cover++;
        int k=(g_rushStage-1)*2+1+f;
        float value=sqrtf(dist2(position,objective_pos(f)))+cover*300;
        if(team==2 && g_rush.objective[k].status==BM_RUSH_ARMED)value-=1000;
        if(value<score){score=value;best=f;}
    }
    return best;
}
static void rush_sync_stage(int stage,const char *reason)
{
    BMRushState before=g_rush;int beforeStage=g_rushStage;
    if(g_mode!=BM_MODE_RUSH || !g_modeProfile || !bm_rush_set_stage(&g_rush,stage,H->frame))return;
    if(stage==g_rushStage)return;
    g_rushBefore=before;g_rushBeforeStage=beforeStage;g_rushBadDeaths=0;   // V17: kept, in case the move was wrong
    g_rushStage=stage;g_rushAdvanceAt=H->frame?H->frame:1;
    memset(g_modeBot,0,sizeof g_modeBot);memset(g_modeFailedSpawn,0,sizeof g_modeFailedSpawn);
    memset(g_modeSpawnOk,0,sizeof g_modeSpawnOk);memset(g_modeSpawnNo,0,sizeof g_modeSpawnNo);memset(g_rushBlind,0,sizeof g_rushBlind);   // V16.1: the deployment moved
    memset(g_modeTeamSpawn,0,sizeof g_modeTeamSpawn);
    memset(g_fleetBase,0,sizeof g_fleetBase);memset(H->teamSpawn,0,sizeof H->teamSpawn);
    for(int t=1;t<=2;t++)memcpy(g_hqPos[t],g_modeProfile->bases[stage][t],12);
    for(int i=0;i<H->botCount;i++)if(bot_brain(i)->task==TASK_NONE){
        bot_brain(i)->targetFlag=-1;bot_brain(i)->enroute=bot_brain(i)->waiting=0;
        nav_cancel_path(i);nav_cancel_path(i+MAX_BOTS);bot_nav(i)->mode=0;
        if(g_drive[i].phase && g_drive[i].phase!=1 && !g_drive[i].manual){
            const float *vp=vehicle_pos(g_drive[i].veh,NULL);int f=vp?mode_vehicle_target(i,vp):-1;
            if(f>=0){g_drive[i].flag=f;memcpy(g_drive[i].goal,objective_pos(f),12);g_drive[i].phase=2;
                g_drive[i].n=g_drive[i].next=g_drive[i].partial=0;g_drive[i].stageCandidate=0;g_drive[i].stageActive=0;
                bot_brain(i)->targetFlag=f;}
        }
        if(g_air[i].phase){g_air[i].flag=-1;g_air[i].patrolChosen=0;g_air[i].patrolAt=0;}
    }
    bot_log("rush: routing sector %d/%d (%s); native objectives/tickets remain engine-owned",stage,g_modeProfile->stages,reason);
}
static void mode_observe_spawn(void *entity,char *player,unsigned a2,unsigned a3,unsigned a4)
{
    if(mode_is_cq()||!player||IsBadReadPtr(entity,4)||*(unsigned *)entity!=0x025bbbc0)return;
    for(int k=0;k<64;k++)if(!g_modePending[k].at){
        g_modePending[k].entity=entity;g_modePending[k].player=player;
        g_modePending[k].a2=a2;g_modePending[k].a3=a3;g_modePending[k].a4=a4;
        g_modePending[k].at=H->frame?H->frame:1;break;
    }
}
static void mode_rush_blind_tick(void)
{
    for(int i=0;i<MAX_BOTS;i++)if(g_rushBlind[i].at && H->frame-g_rushBlind[i].at>=15){
        int t=g_rushBlind[i].team,slot=g_rushBlind[i].slot;void *ent=g_rushBlind[i].entity;
        char *pl=i<H->botCount?H->bots[i]:NULL,*s=pl?*(char **)(pl+OFF_PLAYER_SOLDIER):NULL;
        int stage=g_rushBlind[i].stage;g_rushBlind[i].at=0;
        if(g_mode!=BM_MODE_RUSH || !g_modeProfile || !s || s!=g_rushBlind[i].soldier || stage!=g_rushStage || slot<0 || slot>=g_modeSpawnCount ||
           g_modeSpawns[slot]!=ent || !mode_base_present(stage,t))continue;
        const float *base=g_modeProfile->bases[stage][t],*body=(const float *)(s+OFF_SOLDIER_POSITION);
        if(IsBadReadPtr(body,12) || (fabsf(body[0])<1 && fabsf(body[2])<1)){g_rushBlind[i].at=H->frame?H->frame:1;g_rushBlind[i].soldier=s;g_rushBlind[i].stage=stage;
            g_rushBlind[i].team=t;g_rushBlind[i].slot=slot;g_rushBlind[i].entity=ent;continue;}   // not placed yet: look again
        memcpy(g_modeSpawnLand[slot],body,12);g_modeSpawnLandKnown[slot]=1;   // V16.1: this entity's deployment zone
        if(dist2(body,base)>200*200){
            g_modeFailedSpawn[slot]=H->frame+120*30;g_modeSpawnOk[slot]&=(unsigned char)~(1<<(t-1));
            bot_log("rush: spawn entity %p put a team %d bot %.0f m from its sector %d base - not used for this team for 2 min",ent,t,sqrtf(dist2(body,base)),stage);
        }else{
            g_modeTeamSpawn[t].entity=ent;g_modeTeamSpawn[t].a2=g_rushBlind[i].a2;g_modeTeamSpawn[t].a3=g_rushBlind[i].a3;g_modeTeamSpawn[t].a4=g_rushBlind[i].a4;g_modeTeamSpawn[t].stage=stage;
            bot_log("rush: spawn entity %p serves team %d in sector %d (learned by a bot spawn, %.0f m from the base)",ent,t,stage,sqrtf(dist2(body,base)));
        }
    }
}
// ---- AltBotMod V16.1: M-COM state from what can be seen. The game's own arm / defuse / destroy events are not bound,
// so V9-V16 never left "unknown": defenders camped every panel, the sector never advanced and the deployment never
// moved up. Now: a full arm / defuse hold (a bot's or a player's at the panel), the game's objective messages (logged
// every time to confirm what each means) and the human menu deployment move the state on. ----
static void rush_mark(int k,enum BMRushStatus st,const char *why)
{
    if(g_mode!=BM_MODE_RUSH || !g_modeProfile || k<1 || k>16 || !bm_rush_event(&g_rush,k,st,H->frame))return;
    if(st==BM_RUSH_DESTROYED)g_mcomDestroyedBy[k]=strstr(why,"the game reported it destroyed")?1:2;
    if(st==BM_RUSH_ARMED)g_mcomArmAt[k]=H->frame?H->frame:1;
    bot_log("rush: M-COM %d %s (%s)",k,st==BM_RUSH_ARMED?"armed":st==BM_RUSH_IDLE?"disarmed":"destroyed",why);
    if(st!=BM_RUSH_DESTROYED)return;
    // Squad Rush has one station a sector: its empty slot counts as done
    for(int j=(g_rushStage-1)*2+1;j<=g_rushStage*2 && j<=16;j++)if(!g_modeProfile->mcom[j].present)bm_rush_event(&g_rush,j,BM_RUSH_DESTROYED,H->frame);
    if(bm_rush_sector_complete(&g_rush)){
        if(g_rushStage<g_modeProfile->stages)rush_sync_stage(g_rushStage+1,"the sector's M-COMs are destroyed - the deployment moves up");
        else bot_log("rush: the last sector's M-COMs are destroyed");
    }
}
// The active station a report most likely belongs to: the armed one (longest armed first), else the one with an
// attacker nearest to it (within 40 m).
static int rush_report_target(int wantArmed)
{
    int best=0;unsigned oldest=0;float nearD2=40*40;
    for(int k=(g_rushStage-1)*2+1;k<=g_rushStage*2 && k<=16;k++){
        if(!g_modeProfile->mcom[k].present || !bm_rush_active(&g_rush,k))continue;
        if(wantArmed && g_rush.objective[k].status==BM_RUSH_ARMED && (!best || g_mcomArmAt[k]<oldest)){best=k;oldest=g_mcomArmAt[k];}
    }
    if(best)return best;
    char *pm=player_manager();if(!pm)return 0;
    void **arr=*(void ***)(pm+0xac);unsigned count=*(unsigned *)(pm+8);
    for(unsigned n=0;arr && n<count && n<128;n++){
        char *pl=arr[n],*so;if(!pl || player_team(pl)!=1 || !(so=*(char **)(pl+OFF_PLAYER_SOLDIER)))continue;
        for(int k=(g_rushStage-1)*2+1;k<=g_rushStage*2 && k<=16;k++){
            if(!g_modeProfile->mcom[k].present || !bm_rush_active(&g_rush,k))continue;
            float d=dist2((float *)(so+OFF_SOLDIER_POSITION),g_modeProfile->mcom[k].pos);
            if(d<nearD2){nearD2=d;best=k;}
        }
    }
    return best;
}
static void rush_watch_tick(void)
{
    if(g_mode!=BM_MODE_RUSH || !g_modeProfile || H->frame%15)return;
    // a player holding the key at a panel for 5 s: armed (attacker) / defused (defender)
    char *pm=player_manager();
    void **arr=pm?*(void ***)(pm+0xac):NULL;unsigned count=pm?*(unsigned *)(pm+8):0;
    for(int k=(g_rushStage-1)*2+1;k<=g_rushStage*2 && k<=16;k++){
        if(!g_modeProfile->mcom[k].present || !bm_rush_active(&g_rush,k))continue;
        const float *m=g_modeProfile->mcom[k].pos;int close[3]={0,0,0};
        for(unsigned n=0;arr && n<count && n<128;n++){
            char *pl=arr[n],*so;int t;if(!pl || is_bot(pl) || (t=player_team(pl))<1 || t>2 || !(so=*(char **)(pl+OFF_PLAYER_SOLDIER)))continue;
            float *p=(float *)(so+OFF_SOLDIER_POSITION);
            if(dist2(p,m)<2.5f*2.5f && fabsf(p[1]-m[1])<2.5f)close[t]=1;
        }
        for(int t=1;t<=2;t++){
            if(!close[t]){g_mcomNearSince[k][t]=0;continue;}
            if(!g_mcomNearSince[k][t]){g_mcomNearSince[k][t]=H->frame?H->frame:1;continue;}
            if(H->frame-g_mcomNearSince[k][t]<5*30 || g_crateEvents)continue;   // (V16.2.2: the game's own events say it when hooked)
            if(t==1 && g_rush.objective[k].status!=BM_RUSH_ARMED)rush_mark(k,BM_RUSH_ARMED,"a player stood at its panel 5 s");
            if(t==2 && g_rush.objective[k].status==BM_RUSH_ARMED)rush_mark(k,BM_RUSH_IDLE,"a player stood at its panel 5 s");
            g_mcomNearSince[k][t]=0;
        }
        // armed and nothing reported for 75 s: it was defused or the arm never took - attackers try again
        if(g_rush.objective[k].status==BM_RUSH_ARMED && H->frame-g_mcomArmAt[k]>60*30)rush_mark(k,BM_RUSH_IDLE,"no destruction reported 60 s after arming");
    }
}
// AltBotMod V16.2.2: the game's own M-COM events (StatEvent_CrateArmed / CrateDisarmed / CrateDestroyed, sent by
// ServerStatEventTriggerEntity::sendEvent with the player who did it). kind 0 armed, 1 disarmed, 2 destroyed. The station
// is the active one nearest that player (15 m); a destruction goes to the armed station first.
static void rush_crate_event(int kind,char *pl)
{
    if(g_mode!=BM_MODE_RUSH || !g_modeProfile || kind<0 || kind>2)return;
    int k=kind==2?rush_report_target(1):0;
    if(k && g_rush.objective[k].status!=BM_RUSH_ARMED)k=0;
    char *so=pl && player_exists(pl)?*(char **)(pl+OFF_PLAYER_SOLDIER):NULL;
    if(!k && so){
        float best=15*15,*p=(float *)(so+OFF_SOLDIER_POSITION);
        for(int j=(g_rushStage-1)*2+1;j<=g_rushStage*2 && j<=16;j++)if(g_modeProfile->mcom[j].present && bm_rush_active(&g_rush,j)){
            float d=dist2(p,g_modeProfile->mcom[j].pos);if(d<best){best=d;k=j;}
        }
    }
    if(!k)k=rush_report_target(kind!=0);
    bot_log("rush: the game says M-COM %s by %s -> station %d",kind==0?"armed":kind==1?"disarmed":"destroyed",
        pl && player_exists(pl)?pl+OFF_PLAYER_ONLINE_NAME:"?",k);
    if(k)rush_mark(k,kind==0?BM_RUSH_ARMED:kind==1?BM_RUSH_IDLE:BM_RUSH_DESTROYED,
        kind==0?"the game reported it armed":kind==1?"the game reported it disarmed":"the game reported it destroyed");
}
// The game's objective messages (type names only - their payload is not bound). ServerMission.ObjectiveCompleted came
// 30 s after a player armed an M-COM on 2026-10-13 (MP_Subway): taken as "a station destroyed". ServerMetrics.
// ObjectiveSuccess: taken as "armed" when an attacker is at a panel.
static void rush_objective_message(const char *name)
{
    if(g_mode!=BM_MODE_RUSH || !g_modeProfile || !name)return;
    int kind=!strcmp(name,"ServerMission.ObjectiveCompleted")?0:!strcmp(name,"ServerMetrics.ObjectiveSuccess")?1:
        strstr(name,"Objective")?2:3;
    if(kind==3)return;
    static unsigned lastAt[3];
    if(lastAt[kind] && H->frame-lastAt[kind]<2*30)return;   // (one report can come as several messages)
    lastAt[kind]=H->frame?H->frame:1;g_rushMsgCount[kind]++;
    // V16.2, from the two live rounds: ServerMetrics.ObjectiveSuccess comes ~26 s after an arm (MP_011 09:17:10 armed ->
    // 09:17:35) = an M-COM destroyed; ServerMission.ObjectiveCompleted came with the sector's second one (Metro) = the
    // sector done. (V16.1 read them the other way round and marked the second station armed - attackers stopped there.)
    // V16.2.3: with the game's own crate events hooked these messages change nothing - ObjectiveSuccess comes with the same
    // destruction the crate event already reported, and V16.2.2 then destroyed the OTHER station too: the bots moved to
    // the next sector while B still stood, spawned at its bases outside the play area and died (11:30 MP_011, 11:37 XP1_002).
    if(g_crateEvents){bot_log("rush: game message %s (#%u this round) - the crate events are used instead",name,g_rushMsgCount[kind]);return;}
    int k=kind==1?rush_report_target(1):0;
    bot_log("rush: game message %s (#%u this round) in sector %d -> %s %d",name,g_rushMsgCount[kind],g_rushStage,
        kind==1?"destroyed M-COM":kind==0?"sector complete":"no change for",kind==0?g_rushStage:k);
    if(kind==1 && k)rush_mark(k,BM_RUSH_DESTROYED,"the game reported an objective success");
    if(kind==0){
        int left=0;for(int j=(g_rushStage-1)*2+1;j<=g_rushStage*2 && j<=16;j++)if(g_modeProfile->mcom[j].present && bm_rush_active(&g_rush,j))left=j;
        if(left)rush_mark(left,BM_RUSH_DESTROYED,"the game reported the sector completed");
        else if(g_rushStage<g_modeProfile->stages)rush_sync_stage(g_rushStage+1,"the game reported the sector completed");
    }
}
static int rush_operator_holding(int i)
{
    if(g_mode!=BM_MODE_RUSH || !g_modeProfile || i<0 || i>=H->botCount || !g_modeBot[i].interactAt)return 0;
    int k=g_modeBot[i].target;
    return k>=1 && k<=16 && g_rush.objective[k].operatorPlayer==H->bots[i];
}
// The bot arming / defusing a station keeps at it: no new fight with an enemy more than 6 m away while it is within
// 20 m of the panel (a player does the same - the arm is worth more than the kill).
static int rush_operator_focus(int i,char *enemy)
{
    if(g_mode!=BM_MODE_RUSH || !g_modeProfile || i<0 || i>=H->botCount)return 0;
    int k=g_modeBot[i].target;char *so=*(char **)(H->bots[i]+OFF_PLAYER_SOLDIER);
    if(k<1 || k>16 || !so || g_rush.objective[k].operatorPlayer!=H->bots[i] || H->frame>=g_rush.objective[k].leaseUntil)return 0;
    float *me=(float *)(so+OFF_SOLDIER_POSITION);
    if(dist2(me,g_modeProfile->mcom[k].pos)>20*20)return 0;
    if(g_modeBot[i].interactAt)return 1;   // V16.2: arming / defusing - nothing stops it but death
    char *es=enemy?*(char **)(enemy+OFF_PLAYER_SOLDIER):NULL;
    if(es && dist2(me,(float *)(es+OFF_SOLDIER_POSITION))<4*4)return 0;
    return 1;
}
// AltBotMod V16: a bot that dies with no shooter within 25 s of a mode spawn was put outside the play area (a later
// Rush sector's spawn, a disabled TDM point) and the out-of-bounds timer killed it. Twice from the same spawn entity and
// that entity rests for this team for 3 minutes. (V15, MP_Subway Rush: the defenders spawned in sector 3 and died in a
// loop every few seconds - "the bots weren't moving".)
static void mode_note_death(char *pl)
{
    // AltBotMod V22: Rush only. In TDM / Squad DM / Gun Master the game picks the spawn point inside a team-less entity, and
    // a death with no shooter (a fall, a grenade the game names no thrower for) says nothing about it - V16-V21.1 rested one
    // of Epicenter's two entities for 3 minutes and team 1 could not spawn at all (2026-10-17 21:24, 41 bots retired).
    if(g_mode!=BM_MODE_RUSH || !g_modeProfile || !pl)return;
    int i=bot_index(pl),t=player_team(pl);
    if(i<0 || i>=MAX_BOTS || t<1 || t>2)return;
    int slot=g_modeBotSlot[i]-1;g_modeBotSlot[i]=0;
    if(slot<0 || slot>=g_modeSpawnCount || g_lastSpawnedBot[i]!=pl || H->frame-g_lastSpawnedAt[i]>25*30)return;
    if(g_attacker[i] && H->frame-g_attackedAt[i]<5*30)return;   // shot: an ordinary death
    // AltBotMod V17: right after a sector change, bots of both teams dying outside the play area means the game is still in
    // the old sector (V16.2.2 counted one destruction twice: all bots then spawned ahead and died, defenders worst). Six such
    // deaths within 90 s of the change: back to the sector before, as it was.
    if(g_mode==BM_MODE_RUSH && g_rushBeforeStage && g_rushAdvanceAt && H->frame-g_rushAdvanceAt<90*30 && ++g_rushBadDeaths>=6){
        bot_log("rush: bots keep dying outside the play area since the move to sector %d - the game is still in sector %d: back",g_rushStage,g_rushBeforeStage);
        int stage=g_rushBeforeStage;g_rush=g_rushBefore;g_rush.revision+=2;g_rushBeforeStage=0;g_rushBadDeaths=0;
        for(int k=(stage-1)*2+1;k<=stage*2 && k<=16;k++)if(g_rush.objective[k].status==BM_RUSH_DESTROYED && g_mcomDestroyedBy[k]!=1){g_rush.objective[k].status=BM_RUSH_IDLE;g_mcomDestroyedBy[k]=0;}
        g_rushStage=stage;g_rushAdvanceAt=0;
        memset(g_modeBot,0,sizeof g_modeBot);memset(g_modeFailedSpawn,0,sizeof g_modeFailedSpawn);
        memset(g_modeSpawnOk,0,sizeof g_modeSpawnOk);memset(g_modeSpawnNo,0,sizeof g_modeSpawnNo);memset(g_rushBlind,0,sizeof g_rushBlind);
        memset(g_modeTeamSpawn,0,sizeof g_modeTeamSpawn);memset(H->teamSpawn,0,sizeof H->teamSpawn);
        for(int t2=1;t2<=2;t2++)memcpy(g_hqPos[t2],g_modeProfile->bases[stage][t2],12);
        return;
    }
    if(++g_modeEarlyDeaths[slot][t-1]<2)return;
    g_modeEarlyDeaths[slot][t-1]=0;
    g_modeFailedSpawn[slot]=H->frame+180*30;g_modeSpawnOk[slot]&=(unsigned char)~(1<<(t-1));
    if(g_modeTeamSpawn[t].entity==g_modeSpawns[slot])memset(&g_modeTeamSpawn[t],0,sizeof g_modeTeamSpawn[t]);
    bot_log("spawn: team %d bots die with no shooter soon after spawning at entity %p (outside the play area) - not used for 3 min",t,g_modeSpawns[slot]);
}
static void mode_observation_tick(void)
{
    if(mode_is_cq()||!g_modeProfile)return;
    mode_rush_blind_tick();   // V16
    rush_watch_tick();   // V16.1
    if(g_mode==BM_MODE_RUSH && g_rush.stages!=g_modeProfile->stages){
        bm_rush_reset(&g_rush,g_modeProfile->stages,2,H->frame);
        for(int t=1;t<=2;t++)memcpy(g_hqPos[t],g_modeProfile->bases[1][t],12);
    }
    for(int k=0;k<64;k++)if(g_modePending[k].at&&H->frame-g_modePending[k].at>=10){
        char *pl=g_modePending[k].player;g_modePending[k].at=0;if(!player_exists(pl))continue;int t=player_team(pl);
        char *s=*(char **)(pl+OFF_PLAYER_SOLDIER);if(t<1||t>mode_teams()||!s||player_vehicle(pl))continue;
        float *p=(float *)(s+OFF_SOLDIER_POSITION);if(!mode_inside(p,120)||!cse_valid(g_modePending[k].entity))continue;
        // Only a successful HUMAN deployment selected through the game's
        // normal menu can confirm a later sector. Never probe future spawns
        // with live bots or infer sector completion from elapsed time.
        if(g_mode==BM_MODE_RUSH && !is_bot(pl)){
            int stage=g_rushStage;float best=35*35;
            for(int n=g_rushStage+1;n<=g_modeProfile->stages&&n<9;n++){
                const float *base=g_modeProfile->bases[n][t];
                float d=dist2(p,base);
                if(mode_base_present(n,t) && d<best && dist2(p,g_modeProfile->bases[g_rushStage][t])>100*100){best=d;stage=n;}
            }
            if(stage>g_rushStage)rush_sync_stage(stage,"confirmed human menu deployment");
        }
        if(g_mode==BM_MODE_RUSH && dist2(p,g_modeProfile->bases[g_rushStage][t])>120*120)continue;
        g_modeTeamSpawn[t].entity=g_modePending[k].entity;
        g_modeTeamSpawn[t].a2=g_modePending[k].a2;g_modeTeamSpawn[t].a3=g_modePending[k].a3;
        g_modeTeamSpawn[t].a4=g_modePending[k].a4;g_modeTeamSpawn[t].stage=g_rushStage;
        if(g_mode==BM_MODE_RUSH){H->teamSpawn[t].entity=g_modePending[k].entity;
            H->teamSpawn[t].a2=g_modePending[k].a2;H->teamSpawn[t].a3=g_modePending[k].a3;H->teamSpawn[t].a4=g_modePending[k].a4;}
    }
    if(g_mode==BM_MODE_RUSH)for(int k=1;k<=16;k++){
        void *pl=g_rush.objective[k].operatorPlayer;
        if(pl && (!player_exists(pl) || !*(void **)((char *)pl+OFF_PLAYER_SOLDIER) ||
            H->frame>=g_rush.objective[k].leaseUntil)){
            g_rush.objective[k].operatorPlayer=NULL;g_rush.objective[k].leaseUntil=0;
        }
    }
}
static int mode_spawn(char *bot)
{
    int t=player_team(bot),i=bot_index(bot);if(t<1||t>mode_teams()||i<0||!g_modeProfile)return 0;
    // Squad assignment is asynchronous for all modes, including four-team DM.
    // The same bounded wait that fixes Conquest applies here as well.
    if(spawn_wait_for_squad(bot,t))return 0;
    void *bp=H->teamBlueprint[(t-1)%2+1];
    if(!*(void **)(bot+OFF_PLAYER_BLUEPRINT)){if(!bp)return 0;((SetPtr_t)ADDR_PLAYER_SET_BLUEPRINT)(bot,bp);}
    void *ent=NULL;unsigned a2=0xffffffff,a3=1,a4=0;int slot=-1,loose=0;
    if(g_modeTeamSpawn[t].entity&&cse_valid(g_modeTeamSpawn[t].entity)&&
       (g_mode!=BM_MODE_RUSH||g_modeTeamSpawn[t].stage==g_rushStage)){
        ent=g_modeTeamSpawn[t].entity;a2=g_modeTeamSpawn[t].a2;a3=g_modeTeamSpawn[t].a3;a4=g_modeTeamSpawn[t].a4;
        for(int k=0;k<g_modeSpawnCount;k++)if(g_modeSpawns[k]==ent)slot=k;
        if(slot>=0 && H->frame<g_modeFailedSpawn[slot])ent=NULL;
    }
    if(!ent){
        float best=1e30f;const float *base=t<=2?g_modeProfile->bases[g_rushStage][t]:NULL;
        for(int k=0;k<g_modeSpawnCount;k++){
            char *e=g_modeSpawns[k];int st=cse_valid(e)?spawn_team(e):-1;
            if(st<0 || H->frame<g_modeFailedSpawn[k])continue;
            if(st!=t && !(st==0 && g_mode!=BM_MODE_RUSH))continue;   // V9: team-less TDM points serve any team
            float *p=spawn_data_pos(e);if(!mode_inside(p,120))continue;
            if(g_mode==BM_MODE_RUSH && (!base||!mode_base_present(g_rushStage,t)||dist2(p,base)>120*120))continue;
            float score=g_mode==BM_MODE_RUSH?dist2(p,base):(float)((k+MAX_BOTS-i+H->frame/150)%256);
            if(!st){   // proven for this team first; never one the other side has been using
                if(g_modeSpawnOk[k]&(1<<(t-1)))score-=1000;
                else if(g_modeSpawnOk[k])score+=2000;
            }
            if(score<best){best=score;ent=e;slot=k;}
        }
        // AltBotMod V15: no spawn entity in the play area for this team - try the level's others, as Fun-Bots does
        // (one that worked for this team first, then in turn; a refused one rests, see below).
        // V16.1, Rush: the entity whose deployment zone (where it put a bot before, else its own position) is nearest
        // this sector's base for the team - the deployment moves up with the objectives. (V15/V16 tried them in turn.)
        if(!ent && g_modeSpawnsLoose && g_mode==BM_MODE_RUSH && t<=2 && mode_base_present(g_rushStage,t)){
            const float *rb=g_modeProfile->bases[g_rushStage][t];float bestD=400*400;
            for(int n=0;n<g_modeSpawnCount;n++){
                char *e=g_modeSpawns[n];if(!cse_valid(e) || H->frame<g_modeFailedSpawn[n] || (g_modeSpawnNo[n]&(1<<(t-1))))continue;
                if(g_modeSpawnOk[n] && !(g_modeSpawnOk[n]&(1<<(t-1))))continue;   // the other side's
                const float *p=g_modeSpawnLandKnown[n]?g_modeSpawnLand[n]:spawn_data_pos(e);if(!p)continue;
                float d=dist2(p,rb);if(d<bestD){bestD=d;ent=e;slot=n;loose=1;}
            }
        }
        for(int k=0;!ent && g_modeSpawnsLoose && k<g_modeSpawnCount;k++){
            int n=(k+i+H->frame/150)%g_modeSpawnCount;char *e=g_modeSpawns[n];int st=cse_valid(e)?spawn_team(e):-1;
            if((st!=t && st!=0) || H->frame<g_modeFailedSpawn[n] || (g_modeSpawnNo[n]&(1<<(t-1))) ||
               (!(g_modeSpawnOk[n]&(1<<(t-1))) && g_modeSpawnOk[n]))continue;   // (refused for this team / another team's)
            if(!(g_modeSpawnOk[n]&(1<<(t-1))))for(int m=0;m<g_modeSpawnCount;m++)if(m!=n && (g_modeSpawnOk[m]&(1<<(t-1))) &&
                H->frame>=g_modeFailedSpawn[m] && cse_valid(g_modeSpawns[m]) && (spawn_team(g_modeSpawns[m])==t||!spawn_team(g_modeSpawns[m]))){n=m;e=g_modeSpawns[m];break;}
            ent=e;slot=n;loose=1;
        }
        if(!ent && g_modeSpawnsLoose){   // every one refused this team once: the level may have been settling - start over
            int refused=0;for(int k=0;k<g_modeSpawnCount;k++)if(g_modeSpawnNo[k]&(1<<(t-1))){refused=1;g_modeSpawnNo[k]&=(unsigned char)~(1<<(t-1));}
            if(refused)bot_log("spawn: every spawn entity refused team %d once - trying them again",t);
        }
        a2=0xffffffff;a3=1;a4=0;
    }
    if(!ent)return 0;
    if(g_mode==BM_MODE_RUSH){H->teamSpawn[t].entity=ent;H->teamSpawn[t].a2=a2;H->teamSpawn[t].a3=a3;H->teamSpawn[t].a4=a4;
        memcpy(g_hqPos[t],g_modeProfile->bases[g_rushStage][t],12);
        if(deployment_spawn(bot))return 1;
        if(!vspawn_prepare(bot,t,bot+OFF_PLAYER_ONLINE_NAME)){   // AltBotMod V6: spawn-screen vehicle pick first
            fleet_spawn(bot); // Reserve existing nearby vehicles for a natural base respawn.
            if(!g_fleet[i].vehicle)crew_spawn(bot,t,bot+OFF_PLAYER_ONLINE_NAME);   // AltBotMod V16: or a free seat in a teammate's vehicle
        }
    }
    apply_kit(bot);void *s=((CseSpawn_t)ADDR_CSE_SPAWN)(ent,bot,a2,a3,a4);
    if(!s)s=*(void **)(bot+OFF_PLAYER_SOLDIER);
    if(s && loose && g_mode==BM_MODE_RUSH && slot>=0 && i>=0 && i<MAX_BOTS){
        // V15/V16: a Rush spawn tried blind must put the bot at its own base of this sector. V16: checked half a second
        // later (mode_rush_blind_tick) - right after the spawn call the new soldier still stands at (0, 0, 0), so V15
        // judged every one "206 m from the base" (server 3, 2026-10-11).
        g_rushBlind[i].entity=ent;g_rushBlind[i].slot=slot;g_rushBlind[i].team=t;g_rushBlind[i].stage=g_rushStage;
        g_rushBlind[i].a2=a2;g_rushBlind[i].a3=a3;g_rushBlind[i].a4=a4;g_rushBlind[i].soldier=s;g_rushBlind[i].at=H->frame?H->frame:1;
    }
    if(s && slot>=0 && t>=1 && t<=8)g_modeSpawnOk[slot]|=(unsigned char)(1<<(t-1));
    if(s && i>=0 && i<MAX_BOTS)g_modeBotSlot[i]=(short)(slot+1);
    if(s){g_lastSpawnedBot[i]=bot;g_lastSpawnedAt[i]=H->frame;
        bm_rush_release(&g_rush,bot);memset(&g_modeBot[i],0,sizeof g_modeBot[i]);return 1;}
    if(slot>=0 && loose){g_modeSpawnNo[slot]|=(unsigned char)(1<<(t-1));g_modeFailedSpawn[slot]=H->frame+3*30;}   // V15: refused for this team - the other team may still use it
    else if(slot>=0)g_modeFailedSpawn[slot]=H->frame+(g_mode!=BM_MODE_RUSH?3:spawn_team(ent)?10:60)*30;   // (V22: team play - 3 s; Rush: a refused team-less point a minute)
    g_fleet[i].vehicle=NULL;g_fleet[i].until=0;g_fleet[i].queuedAt=0;
    return 0;
}
static int rush_target(int i,const float *me)
{
    int team=player_team(H->bots[i]),best=0;float bestScore=INFINITY;
    {int ro=squad_order_rush(i);if(ro)return ro;}   // AltBotMod V34: the squad was told which M-COM
    for(int k=(g_rushStage-1)*2+1;k<=g_rushStage*2;k++){
        if(k>16 || !g_modeProfile->mcom[k].present || !bm_rush_active(&g_rush,k))continue;
        int assigned=0;
        for(int j=0;j<H->botCount;j++)if(j!=i && player_team(H->bots[j])==team &&
            *(void **)(H->bots[j]+OFF_PLAYER_SOLDIER) && g_modeBot[j].target==k)assigned++;
        float score=sqrtf(dist2(me,g_modeProfile->mcom[k].pos))+assigned*80;
        if(team==2 && g_rush.objective[k].status==BM_RUSH_ARMED)score-=1000;
        // V16.2.3: an armed station is held - the first five attackers within reach stay and guard it until it blows
        // (V16.2 sent them all to the other one and the defenders walked up and disarmed it)
        if(team==1 && g_rush.objective[k].status==BM_RUSH_ARMED)score+=assigned<5?(dist2(me,g_modeProfile->mcom[k].pos)<120*120?-300:0):40;
        if(score<bestScore){bestScore=score;best=k;}
    }
    return best;
}
static int mode_goal_clear(int i,const float *goal)
{
    for(int j=0;j<H->botCount;j++)if(j!=i && player_team(H->bots[j])==player_team(H->bots[i])){
        char *s=*(char **)(H->bots[j]+OFF_PLAYER_SOLDIER);
        if(s && dist2((float *)(s+OFF_SOLDIER_POSITION),goal)<3*3)return 0;
        if(g_modeBot[j].goalReady && dist2(g_modeBot[j].goal,goal)<3*3)return 0;
    }
    return 1;
}
// AltBotMod V18: a spot inside a building near a point (roof overhead within 14 m, on the grid, not taken) - guards
// and patrols use one now and then, so interiors are searched and held, not only streets and squares.
static int indoor_spot(int i,const float *around,float radius,float *out)
{
    NavGrid *g=*nav_slot();if(!g || g->phase!=2)return 0;
    unsigned saved=g_rayFlags;int found=0;
    for(int k=0;k<4 && !found;k++){
        float ang=i*2.399963f+k*1.7f+(H->frame/30)*0.61f,r=radius*(0.35f+0.65f*(float)((i*7+k*13+H->frame/30)%10)/10.0f),pt[3];
        if(!nav_snap(g,around[0]+sinf(ang)*r,around[1]+(k&1?4.0f:0.0f),around[2]+cosf(ang)*r,pt))continue;
        if(fabsf(pt[1]-around[1])>8 || nav_is_water(g,pt[0],pt[1],pt[2]) || !mode_goal_clear(i,pt))continue;
        float a[3]={pt[0],pt[1]+1.8f,pt[2]},up[3]={pt[0],pt[1]+14,pt[2]};
        g_rayFlags=RAY_DONT_CHECK_CHARACTER;
        if(!ray_cast(a,up,NULL,NULL))continue;   // open sky: not inside
        int walls=0;
        for(int w=0;w<4;w++){float e[3]={pt[0]+sinf(w*1.5708f)*10,pt[1]+1.2f,pt[2]+cosf(w*1.5708f)*10},c[3]={pt[0],pt[1]+1.2f,pt[2]};if(ray_cast(c,e,NULL,NULL))walls++;}
        if(walls<2)continue;   // a carport / an arch: roofed but open
        memcpy(out,pt,12);found=1;
    }
    g_rayFlags=saved;
    return found;
}
// AltBotMod V16: each side's flag stand on the four End Game maps, read from the level data (levels/xp5_00x/ctf:
// the xp5/gameplay/ctf_us_base / ctf_ru_base prefabs). V15 guessed the bases from spawn points.
static const struct {const char *level;float us[3],ru[3];} g_ctfBases[]={
    {"XP5_001",{361.69f,107.18f,-6.82f},{-45.86f,108.26f,51.97f}},
    {"XP5_002",{-2025.38f,59.31f,-230.62f},{-1885.49f,50.19f,180.20f}},
    {"XP5_003",{448.58f,155.00f,-828.12f},{-212.66f,155.00f,-875.88f}},
    {"XP5_004",{-903.58f,151.42f,-1093.60f},{-853.18f,148.25f,-710.15f}},
};
static const float *ctf_base(int team)
{
    for(unsigned k=0;k<sizeof g_ctfBases/sizeof g_ctfBases[0];k++)if(!_stricmp(g_level,g_ctfBases[k].level))return team==1?g_ctfBases[k].us:g_ctfBases[k].ru;
    return team>=1 && team<=2 && !near_origin(g_modeTeamBase[team])?g_modeTeamBase[team]:NULL;
}
// AltBotMod V15/V16, Capture the Flag: a third of the bots raid - run onto the enemy flag stand (BF3 picks the flag up on
// contact), sweep it for 12 s, run home onto their own stand (a carried flag scores there), sweep, go again. V16: a third
// guard their own flag stand (the raiders coming for it), the rest hunt as in TDM. Returns 1 when it set the goal.
static int mode_ctf_goal(int i,const float *me,int team)
{
    if(!mode_is_ctf() || team<1 || team>2 || i%3==2)return 0;
    const float *own=ctf_base(team),*enemy=ctf_base(3-team);
    if(!own || !enemy)return 0;
    int guard=i%3==1,home=!guard && g_modeBot[i].raid==2;const float *base=guard||home?own:enemy;
    if(!guard && dist2(me,base)<25*25 && fabsf(me[1]-base[1])<10){
        if(!g_modeBot[i].raidAt)g_modeBot[i].raidAt=H->frame?H->frame:1;
        else if(H->frame-g_modeBot[i].raidAt>12*30){
            g_modeBot[i].raid=home?1:2;g_modeBot[i].raidAt=0;base=home?enemy:own;
            bot_log("ctf: %s %s",H->bots[i]+OFF_PLAYER_ONLINE_NAME,home?"raids the enemy flag again":"heads home from the enemy flag");
        }
    }
    int c=g_modeBot[i].candidate++;float spot[3],r=guard?10+(c%4)*5:(c%4)*2.0f;   // raiders onto the stand itself, guards round it
    if(!*nav_slot() || !nav_snap(*nav_slot(),base[0]+sinf(i*2.399963f+c)*r,base[1],base[2]+cosf(i*2.399963f+c)*r,spot) ||
       fabsf(spot[1]-base[1])>=8)return 0;
    memcpy(g_modeBot[i].goal,spot,12);g_modeBot[i].goalReady=1;
    g_modeBot[i].routePending=0;g_modeBot[i].plannedAt=0;nav_cancel_path(i);
    g_modeBot[i].guardUntil=H->frame+(guard?12:5+i%3)*30;
    return 1;
}
static void mode_brain_tick(int i,char *soldier)
{
    if(!g_modeProfile||player_vehicle(H->bots[i]) || g_drive[i].phase || g_air[i].phase)return;
    if(g_fleet[i].vehicle && H->frame<g_fleet[i].until)return;
    float *me=(float *)(soldier+OFF_SOLDIER_POSITION);int team=player_team(H->bots[i]);
    if(g_modeBot[i].soldier!=soldier){bm_rush_release(&g_rush,H->bots[i]);memset(&g_modeBot[i],0,sizeof g_modeBot[i]);g_modeBot[i].soldier=soldier;}
    int target=g_modeBot[i].target;const float *goal=NULL;int operator=0;
    if(g_mode==BM_MODE_RUSH){
        // V16.2.3: a bot holding the key at a station keeps it through other stations' news (a revision bump re-picked every
        // bot's station and broke its hold)
        if(target && g_modeBot[i].interactAt && bm_rush_active(&g_rush,target) && bm_rush_can_interact(&g_rush,target,team))g_modeBot[i].revision=g_rush.revision;
        if(!target || !bm_rush_active(&g_rush,target) || g_modeBot[i].revision!=g_rush.revision){
            bm_rush_release(&g_rush,H->bots[i]);target=rush_target(i,me);
            g_modeBot[i].target=target;g_modeBot[i].goalReady=0;g_modeBot[i].routePending=0;
            g_modeBot[i].interactAt=0;g_modeBot[i].candidate=0;g_modeBot[i].plannedAt=0;
            g_modeBot[i].revision=g_rush.revision;nav_cancel_path(i);
        }
        if(!target){bot_nav(i)->mode=0;memset(H->botMove[i],0,sizeof H->botMove[i]);return;}
        // V16.2.3: a bot next to a station it can arm / defuse (10 m) does it, whatever station it was sent to - unless a
        // teammate is already at that panel
        if(!g_modeBot[i].interactAt){
            for(int j=(g_rushStage-1)*2+1;j<=g_rushStage*2 && j<=16;j++){
                if(j==target || !g_modeProfile->mcom[j].present || !bm_rush_active(&g_rush,j) || !bm_rush_can_interact(&g_rush,j,team))continue;
                const float *jp=g_modeProfile->mcom[j].pos;float dj=dist2(me,jp);
                if(dj>10*10 || fabsf(me[1]-jp[1])>3 || dj>=dist2(me,g_modeProfile->mcom[target].pos))continue;
                char *holder=g_rush.objective[j].operatorPlayer,*hs=holder && holder!=H->bots[i] && player_exists(holder)?*(char **)(holder+OFF_PLAYER_SOLDIER):NULL;
                if(hs && dist2((float *)(hs+OFF_SOLDIER_POSITION),jp)<RUSH_ARM_REACH*RUSH_ARM_REACH)continue;
                bot_log("rush: %s is next to M-COM %d - goes to %s it",H->bots[i]+OFF_PLAYER_ONLINE_NAME,j,team==1?"arm":"defuse");
                bm_rush_release(&g_rush,H->bots[i]);target=j;g_modeBot[i].target=j;g_modeBot[i].goalReady=0;g_modeBot[i].routePending=0;
                g_modeBot[i].candidate=0;g_modeBot[i].plannedAt=0;g_modeBot[i].approachAt=0;g_modeBot[i].retryAt=0;nav_cancel_path(i);
                break;
            }
        }
        goal=g_modeProfile->mcom[target].pos;
        // V16.2: a bot standing at the panel takes the job from one that is not there (dead, fighting, stuck far off) -
        // V16.1 bots walked over the spot to their guard posts while the lease holder was elsewhere.
        if(dist2(me,goal)<RUSH_ARM_REACH*RUSH_ARM_REACH && fabsf(me[1]-goal[1])<2.5f && bm_rush_can_interact(&g_rush,target,team)){
            char *holder=g_rush.objective[target].operatorPlayer,*hs=holder && player_exists(holder)?*(char **)(holder+OFF_PLAYER_SOLDIER):NULL;
            if(holder && holder!=H->bots[i] && (!hs || dist2((float *)(hs+OFF_SOLDIER_POSITION),goal)>RUSH_ARM_REACH*RUSH_ARM_REACH)){
                g_rush.objective[target].operatorPlayer=NULL;g_rush.objective[target].leaseUntil=0;
            }
            g_modeBot[i].retryAt=0;
        }
        if(dist2(me,goal)<60*60 && bm_rush_can_interact(&g_rush,target,team) && H->frame>=g_modeBot[i].retryAt)
            operator=bm_rush_acquire(&g_rush,target,team,H->bots[i],H->frame,9*30);
        if(operator){
            if(!g_modeBot[i].operatorAt)g_modeBot[i].operatorAt=H->frame?H->frame:1;
            if(H->frame-g_modeBot[i].operatorAt>=45*30 && !g_modeBot[i].interactAt){
                bm_rush_release(&g_rush,H->bots[i]);operator=0;g_modeBot[i].retryAt=H->frame+10*30;
            }
        }else g_modeBot[i].operatorAt=0;
        // AltBotMod V10: snipers (not the one arming / defusing it) overwatch the MCOM from high ground
        if(!operator){
            if(sniper_overwatch_tick(i,bot_brain(i),soldier,goal,1000+target)){g_modeBot[i].snipe=1;return;}
            if(g_modeBot[i].snipe){   // back from overwatch: its own goal / route again
                g_modeBot[i].snipe=0;g_modeBot[i].goalReady=0;g_modeBot[i].routePending=0;g_modeBot[i].plannedAt=0;nav_cancel_path(i);bot_nav(i)->mode=0;
            }
        }
        if(g_modeBot[i].goalRole!=operator){
            g_modeBot[i].goalRole=operator;g_modeBot[i].goalReady=0;g_modeBot[i].routePending=0;
            g_modeBot[i].plannedAt=0;nav_cancel_path(i);bot_nav(i)->mode=0;
        }
        // V16.2: the engine arms / defuses from a few metres (Fun-Bots: 4 m from the MCOM); the bot starts within 2.6 m of
        // the recorded spot and keeps at it within 3.2 m (a push or a crouch does not end it)
        // V16.2.3: the game arms from the recorded spot with the recorded aim (as Fun-Bots, which puts the bot on it): the bot
        // walks onto it (0.6 m; 0.4 m after a hold that did nothing) and only holds from further (up to 2.6 m) when it
        // could not get closer in 4 s. (V16.2.2 started at 2.6 m - the walk there stops 2 m short - and mostly did nothing.)
        // AltBotMod V34: a window between the operator and its M-COM - knifed (then shot) open
        if(operator && !g_modeBot[i].interactAt && mcom_pane_tick(i,soldier,goal))return;
        float d2=dist2(me,goal),need=H->frame<g_modeBot[i].closerUntil?0.4f:0.6f;int level=fabsf(me[1]-goal[1])<2.5f;
        if(!g_modeBot[i].interactAt){
            if(operator && level && d2<RUSH_ARM_REACH*RUSH_ARM_REACH){if(!g_modeBot[i].approachAt)g_modeBot[i].approachAt=H->frame?H->frame:1;}
            else g_modeBot[i].approachAt=0;
        }
        int ready=g_modeBot[i].interactAt?d2<RUSH_ARM_KEEP*RUSH_ARM_KEEP:
            d2<need*need || (g_modeBot[i].approachAt && H->frame-g_modeBot[i].approachAt>=4*30 && d2<RUSH_ARM_REACH*RUSH_ARM_REACH);
        if(operator && ready && level){
            if(!g_modeBot[i].interactAt){
                g_modeBot[i].interactAt=H->frame?H->frame:1;bot_crouch(i);
                bot_log("rush: %s %s M-COM %d (%.1f m from the spot)",H->bots[i]+OFF_PLAYER_ONLINE_NAME,team==1?"arms":"defuses",target,sqrtf(dist2(me,goal)));
            }
            bot_nav(i)->mode=0;memset(H->botMove[i],0,sizeof H->botMove[i]);
            if(H->frame-g_modeBot[i].interactAt>=RUSH_HOLD_FRAMES && g_crateEvents){
                // V16.2.2: the game reports a real arm / defuse (about 3 s into the hold); 8 s without one means the
                // panel was not in reach from here - walk up to within 1.2 m of the spot (for 20 s) and hold again.
                bot_log("rush: %s held the key 8 s at M-COM %d with no %s - moves onto the spot and tries again",H->bots[i]+OFF_PLAYER_ONLINE_NAME,target,team==1?"arm":"defuse");
                g_modeBot[i].interactAt=0;g_modeBot[i].closerUntil=H->frame+20*30;g_modeBot[i].approachAt=H->frame?H->frame:1;bot_stand(i);
            }else if(H->frame-g_modeBot[i].interactAt>=RUSH_HOLD_FRAMES){
                // V16.1: a full hold arms (attacker) / defuses (defender) it as far as the bots know - the game reports
                // nothing readable yet. Defenders then come to defuse; an arm that did not take is retried (rush_watch_tick).
                rush_mark(target,team==1?BM_RUSH_ARMED:BM_RUSH_IDLE,team==1?"a bot held the arm key at it":"a bot held the defuse key at it");
                g_modeBot[i].interactAt=0;g_modeBot[i].retryAt=H->frame+10*30;bot_stand(i);
                g_modeBot[i].goalReady=0;g_modeBot[i].routePending=0;g_modeBot[i].plannedAt=0;
                bm_rush_release(&g_rush,H->bots[i]);
            }
            return;
        }
        g_modeBot[i].interactAt=0;
        // One operator approaches the panel. Other bots hold distinct checked
        // positions around it rather than all stacking on the interaction point.
        if(operator){
            if(!g_modeBot[i].goalReady || dist2(g_modeBot[i].goal,goal)>1){
                memcpy(g_modeBot[i].goal,goal,12);g_modeBot[i].goalReady=1;g_modeBot[i].routePending=0;
                g_modeBot[i].plannedAt=0;nav_cancel_path(i);
            }
            // V16.1: the last 5 m straight onto the recorded spot - the crate itself stopped routed bots 2-4 m short
            if(dist2(me,goal)<5*5 && fabsf(me[1]-goal[1])<2.5f){
                // (V16.2.3: walks at a point 2.5 m past the spot - a straight walk stops 2 m short of its point)
                float wx=goal[0]-me[0],wz=goal[2]-me[2],wd=sqrtf(wx*wx+wz*wz)+1e-3f;
                nav_cancel_path(i);bot_nav(i)->mode=2;bot_nav(i)->x=goal[0]+wx/wd*2.5f;bot_nav(i)->z=goal[2]+wz/wd*2.5f;
                bot_brain(i)->targetFlag=(target-1)%2;return;
            }
        }else if(!g_modeBot[i].goalReady || (H->frame>=g_modeBot[i].guardUntil && bot_nav(i)->mode!=3)){
            if(g_modeGoalProbeFrame==H->frame)return;
            if(g_modeGoalProbeFrame!=H->frame-1)g_modeGoalProbeNext=0;
            if(i<g_modeGoalProbeNext)return;
            g_modeGoalProbeFrame=H->frame;g_modeGoalProbeNext=i+1;
            if(g_modeGoalProbeNext>=H->botCount)g_modeGoalProbeNext=0;
            {   // V18: every third guard post inside a building near the station
                float in[3];
                if((i+g_modeBot[i].candidate)%3==0 && indoor_spot(i,goal,team==2?18.0f:24.0f,in)){
                    g_modeBot[i].candidate++;memcpy(g_modeBot[i].goal,in,12);g_modeBot[i].goalReady=1;
                    g_modeBot[i].routePending=0;g_modeBot[i].plannedAt=0;nav_cancel_path(i);
                    g_modeBot[i].guardUntil=H->frame+(12+i%7)*30;goal=g_modeBot[i].goal;bot_brain(i)->targetFlag=(target-1)%2;goto planned;
                }
            }
            float angle=(i*2.399963f)+(g_modeBot[i].candidate++%8)*0.785398f;
            int held=team==1 && g_rush.objective[target].status==BM_RUSH_ARMED;   // V16.2.3: guarding an armed station - close, all round it
            float radius=held?4+(i%4)*2.0f:team==2?8+(i%4)*3:12+(i%4)*4;
            {   // AltBotMod V34: two posts in three from the M-COM's zone of the Fun-Bots objective mesh
                float zp[3];
                if((i+g_modeBot[i].candidate)%3!=0 && zone_guard_point(goal,(unsigned)(i*7919u+g_modeBot[i].candidate*104729u),zp) &&
                   dist2(zp,goal)<(radius+8)*(radius+8) && dist2(zp,goal)>2.0f*2.0f && fabsf(zp[1]-goal[1])<=4 && mode_goal_clear(i,zp)){
                    memcpy(g_modeBot[i].goal,zp,12);g_modeBot[i].goalReady=1;
                    g_modeBot[i].routePending=0;g_modeBot[i].plannedAt=0;nav_cancel_path(i);
                    g_modeBot[i].guardUntil=H->frame+(10+i%7)*30;goal=g_modeBot[i].goal;bot_brain(i)->targetFlag=(target-1)%2;goto planned;
                }
            }
            float candidate[3]={goal[0]+sinf(angle)*radius,goal[1],goal[2]+cosf(angle)*radius},floor;
            if(!ground_below(candidate[0],candidate[2],goal[1]+4,goal[1]-4,&floor))return;
            candidate[1]=floor;
            if(!nav_snap(*nav_slot(),candidate[0],floor,candidate[2],candidate) ||
               nav_is_water(*nav_slot(),candidate[0],candidate[1],candidate[2]) ||
               fabsf(candidate[1]-goal[1])>4 || !mode_goal_clear(i,candidate))return;
            memcpy(g_modeBot[i].goal,candidate,12);g_modeBot[i].goalReady=1;
            g_modeBot[i].routePending=0;g_modeBot[i].plannedAt=0;nav_cancel_path(i);
            g_modeBot[i].guardUntil=H->frame+(10+i%7)*30;
        }
        goal=g_modeBot[i].goal;
        bot_brain(i)->targetFlag=(target-1)%2;
    }else{
        if(!g_modeBot[i].goalReady || (H->frame>=g_modeBot[i].guardUntil && bot_nav(i)->mode!=3)){
            // AltBotMod V6, Team / Squad Deathmatch: hunt. Three times in four a bot heads for the nearest living enemy
            // (a spot a few metres beside it, so a group does not queue up in one line); otherwise a recorded patrol
            // point, so the map is still swept. (V5: patrol points only - bots wandered and rarely met.)
            if(mode_ctf_goal(i,me,team)){goal=g_modeBot[i].goal;goto planned;}   // AltBotMod V15
            char *pm=player_manager();void **arr=pm?*(void ***)(pm+0xac):NULL;unsigned count=pm?*(unsigned *)(pm+8):0;
            float *hunt=NULL,best=400.0f*400.0f;
            for(unsigned k=0;arr && k<count && k<64;k++){
                char *pl=arr[k],*es=pl?*(char **)(pl+OFF_PLAYER_SOLDIER):NULL;
                if(!es || player_team(pl)==team || player_team(pl)<1 || *(float *)(es+OFF_SOLDIER_HEALTH)<=0)continue;
                float *ep=(float *)(es+OFF_SOLDIER_POSITION),d=dist2(ep,me);
                if(d<best){best=d;hunt=ep;}
            }
            int c=g_modeBot[i].candidate++;
            float spot[3];
            if(hunt && (i+c)%4!=0 && *nav_slot() && nav_snap(*nav_slot(),hunt[0]+sinf(i*2.399963f+c)*(5+i%4*3),hunt[1],hunt[2]+cosf(i*2.399963f+c)*(5+i%4*3),spot) &&
               fabsf(spot[1]-hunt[1])<4){
                memcpy(g_modeBot[i].goal,spot,12);g_modeBot[i].goalReady=1;
                g_modeBot[i].routePending=0;g_modeBot[i].plannedAt=0;nav_cancel_path(i);
                g_modeBot[i].guardUntil=H->frame+(6+i%4)*30;
                goal=g_modeBot[i].goal;
                goto planned;
            }
            int n=(i*7+c*11)%32;
            memcpy(g_modeBot[i].goal,g_modeProfile->patrol[n],12);g_modeBot[i].goalReady=1;
            {   // V18: every other patrol stop searches a building near the patrol point instead
                float in[3];
                if((i+c)%2==0 && indoor_spot(i,g_modeProfile->patrol[n],45.0f,in))memcpy(g_modeBot[i].goal,in,12);
            }
            g_modeBot[i].routePending=0;g_modeBot[i].plannedAt=0;nav_cancel_path(i);
            g_modeBot[i].guardUntil=H->frame+(8+i%5)*30;
        }
        goal=g_modeBot[i].goal;
    }
planned:
    if(H->frame>=g_modeBot[i].plannedAt && plan_budget_left()){
        if(plan_path(i,soldier,goal[0],goal[1],goal[2])){
            bot_nav(i)->mode=3;g_modeBot[i].routePending=0;
            g_modeBot[i].plannedAt=H->frame+5*30;
        }else if(g_lastPlanDeferred){
            // Preserve the exact queued destination. A previous live route
            // remains usable while a replacement is pending.
            g_modeBot[i].routePending=1;g_modeBot[i].plannedAt=H->frame+3;
        }else{
            g_modeBot[i].goalReady=0;g_modeBot[i].routePending=0;g_modeBot[i].plannedAt=H->frame+3*30;
            if(operator)bm_rush_release(&g_rush,H->bots[i]);
        }
    }
}
static void mode_reassign_idle(int i,int objective)
{
    if(g_mode!=BM_MODE_RUSH || objective<0 || objective>=objective_count())return;
    bm_rush_release(&g_rush,H->bots[i]);
    g_modeBot[i].target=(g_rushStage-1)*2+1+objective;
    g_modeBot[i].goalReady=g_modeBot[i].routePending=g_modeBot[i].goalRole=0;
    g_modeBot[i].interactAt=g_modeBot[i].operatorAt=g_modeBot[i].plannedAt=0;
    g_modeBot[i].revision=g_rush.revision;g_modeBot[i].candidate++;
}
static void mode_apply(int i,void *input)
{
    int target=g_modeBot[i].target,team=player_team(H->bots[i]);
    char *s=*(char **)(H->bots[i]+OFF_PLAYER_SOLDIER);
    int hold=g_mode==BM_MODE_RUSH && g_modeBot[i].interactAt && g_modeProfile && target>0 && target<=16 &&
        bm_rush_can_interact(&g_rush,target,team) && g_rush.objective[target].operatorPlayer==H->bots[i] &&
        H->frame<g_rush.objective[target].leaseUntil && H->frame-g_modeBot[i].interactAt<RUSH_HOLD_FRAMES &&
        bot_brain(i)->enabled && s && *(float *)(s+OFF_SOLDIER_HEALTH)>0 && !player_vehicle(H->bots[i]) &&
        dist2((float *)(s+OFF_SOLDIER_POSITION),g_modeProfile->mcom[target].pos)<RUSH_ARM_KEEP*RUSH_ARM_KEEP &&
        fabsf(((float *)(s+OFF_SOLDIER_POSITION))[1]-g_modeProfile->mcom[target].pos[1])<2.5f;   // (V16.1/V16.2: shots, fights, other jobs no longer stop it)
    if(hold||g_modeBot[i].heldInteract)((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(input,EIA_INTERACT,hold?1:0);
    g_modeBot[i].heldInteract=hold;
    // V16.2.1: a full hold is finished by mode_brain_tick (it marks the station armed / defused). mode_apply runs every
    // frame and the brain every few, so V16.2 always got here first at 8.0 s, logged a stop and threw the hold away.
    if(!hold && g_modeBot[i].interactAt && H->frame-g_modeBot[i].interactAt>=RUSH_HOLD_FRAMES && H->frame-g_modeBot[i].interactAt<RUSH_HOLD_FRAMES+3*30 &&
       g_rush.objective[target>0 && target<=16?target:0].operatorPlayer==H->bots[i] && s && *(float *)(s+OFF_SOLDIER_HEALTH)>0)return;
    if(!hold && g_modeBot[i].interactAt){
        bot_log("rush: %s stops at M-COM %d after %.1f s",H->bots[i]+OFF_PLAYER_ONLINE_NAME,target,(H->frame-g_modeBot[i].interactAt)/30.0f);
        g_modeBot[i].interactAt=0;bm_rush_release(&g_rush,H->bots[i]);bot_stand(i);
    }
    if(hold){
        ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(input,EIA_THROTTLE,0);
        ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(input,EIA_STRAFE,0);
        ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(input,EIA_YAW,0);
        ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(input,EIA_PITCH,0);
        ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(input,EIA_FIRE,0);((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(input,EIA_ZOOM,0);   // (V16.2.3: firing breaks an arm)
        *(int *)((char *)input+OFF_INPUT_AIM_VALID)=1;
        *(float *)((char *)input+OFF_INPUT_AIM_YAW)=g_modeProfile->mcom[target].yaw;
        *(float *)((char *)input+OFF_INPUT_AIM_PITCH)=g_modeProfile->mcom[target].pitch;
    }
}
static int rush_command(const char *who,const char *arg)
{
    if(g_mode!=BM_MODE_RUSH || !g_modeProfile){chat_say("[AltBotMod] Rush is not active");return 1;}
    char action[32]="";int value=0;sscanf(arg,"%31s %d",action,&value);
    if(!action[0] || !_stricmp(action,"status")){
        chat_say("[AltBotMod] Rush sector %d/%d. Native objective event binding: pending verification.",g_rushStage,g_modeProfile->stages);
        for(int k=(g_rushStage-1)*2+1;k<=g_rushStage*2;k++)if(k<=16 && g_modeProfile->mcom[k].present)
            chat_say("[AltBotMod] M-COM %d: %s",k,(const char *[]){"unknown","idle","armed","destroyed"}[g_rush.objective[k].status]);
        return 1;
    }
    if(!chat_is_admin(who)){chat_say("[AltBotMod] only admins can synchronize Rush observations");return 1;}
    if(!_stricmp(action,"stage")){
        if(value<g_rushStage || value>g_modeProfile->stages){chat_say("[AltBotMod] invalid Rush sector");return 1;}
        rush_sync_stage(value,"admin confirmed active sector");return 1;
    }
    enum BMRushStatus status=!_stricmp(action,"armed")?BM_RUSH_ARMED:!_stricmp(action,"disarmed")?BM_RUSH_IDLE:
        !_stricmp(action,"destroyed")?BM_RUSH_DESTROYED:BM_RUSH_UNKNOWN;
    if(status==BM_RUSH_UNKNOWN || !bm_rush_event(&g_rush,value,status,H->frame)){
        chat_say("[AltBotMod] !rush status | stage <n> | armed/disarmed/destroyed <global M-COM number>");return 1;
    }
    // Destruction can end the pair before the next combat zone is active.
    // Wait for actual new deployment confirmation instead of inventing a delay.
    if(bm_rush_sector_complete(&g_rush))chat_say("[AltBotMod] sector objectives complete; waiting for next active deployment sector");
    return 1;
}
static void rush_trace_message(const char *name,const void *message)
{
    (void)message;
    // Until native scoring payloads have been verified, collect TYPE NAMES
    // only. Similar names or a numeric 86 in an arbitrary payload are not
    // enough evidence to mark an M-COM armed. No guessed offsets or calls.
    static const char *seen[32];static LONG epoch=-1;static int count;
    if(g_mode!=BM_MODE_RUSH || !name)return;
    if(epoch!=g_roundEpoch){epoch=g_roundEpoch;count=0;}
    if(!strstr(name,"Scor")&&!strstr(name,"StatEvent")&&!strstr(name,"Crate")&&!strstr(name,"Objective")&&!strstr(name,"LifeCounter"))return;
    rush_objective_message(name);   // V16.1
    for(int k=0;k<count;k++)if(!strcmp(seen[k],name))return;
    if(count<32){seen[count++]=name;bot_log("rush-native-type: %s (payload unbound; no objective transition inferred)",name);}
}
#include "deployment_native.inc"
