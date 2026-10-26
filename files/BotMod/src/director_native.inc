// A single objective owner for automatic CQ duties. Vehicles and infantry use
// the same team assignment snapshot; human Q-wheel orders remain explicit.
static BMDirectorState g_director;
static char *g_directorIdentity[MAX_BOTS];
static unsigned g_directorAt,g_directorSig;
static LONG g_directorEpoch;
static int director_target(int i,int *role)
{
    *role=BM_OBJECTIVE_NONE;
    if(!mode_is_cq() || i<0 || i>=H->botCount || g_directorEpoch!=g_roundEpoch)return -1;
    BMDirectorAssignment *a=&g_director.unit[i];
    if(a->flag<0 || a->flag>=g_flagCount)return -1;
    *role=a->role;return a->flag;
}
static void director_tick(void)
{
    if(!mode_is_cq() || !level_ready() || !g_flagCount)return;
    unsigned sig=(unsigned)H->botCount;
    for(int f=0;f<g_flagCount;f++)sig=sig*31u+(unsigned)flag_owner(f)+1u;
    // Caspian Test16: refresh at once when a bot's local/recovery order starts or ends.
    for(int i=0;i<H->botCount;i++)sig=sig*3u+(unsigned)local_order_active(i);
    if(g_directorEpoch!=g_roundEpoch){memset(&g_director,0,sizeof g_director);memset(g_directorIdentity,0,sizeof g_directorIdentity);g_directorAt=0;g_directorEpoch=g_roundEpoch;}
    if(g_directorAt && H->frame-g_directorAt<30 && sig==g_directorSig)return;
    g_directorAt=H->frame?H->frame:1;g_directorSig=sig;
    BMDirectorFlag flags[BM_DIRECTOR_FLAGS]={0};BMDirectorUnit units[MAX_BOTS]={0};
    // Caspian V2: remember which team lost each flag, and when.
    static int prevOwner[BM_DIRECTOR_FLAGS];static unsigned lostAt[BM_DIRECTOR_FLAGS][3];static LONG lossEpoch;
    if(lossEpoch!=g_roundEpoch){memset(prevOwner,0,sizeof prevOwner);memset(lostAt,0,sizeof lostAt);lossEpoch=g_roundEpoch;}
    for(int f=0;f<g_flagCount && f<BM_DIRECTOR_FLAGS;f++){
        int owner=flag_owner(f);
        if(prevOwner[f]>=1 && prevOwner[f]<=2 && owner!=prevOwner[f])lostAt[f][prevOwner[f]]=H->frame?H->frame:1;
        prevOwner[f]=owner;
        flags[f].owner=owner;memcpy(flags[f].position,flag_pos(f),12);
        for(int t=1;t<=2;t++)flags[f].recentLoss[t]=lostAt[f][t] && H->frame-lostAt[f][t]<120*30;
    }
    char *pm=player_manager();void **players=pm?*(void ***)(pm+0xac):NULL;unsigned count=pm?*(unsigned *)(pm+8):0;
    for(unsigned k=0;players && k<count && k<64;k++){
        char *pl=players[k],*s=pl?*(char **)(pl+OFF_PLAYER_SOLDIER):NULL;
        int team=pl?player_team(pl):0;
        if(!s || team<1 || team>2 || *(float *)(s+OFF_SOLDIER_HEALTH)<=0)continue;
        float *pos=(float *)(s+OFF_SOLDIER_SPAWN_POSITION);
        for(int f=0;f<g_flagCount;f++)if(dist2(pos,flag_pos(f))<50*50 && fabsf(pos[1]-flag_pos(f)[1])<15)flags[f].enemy[3-team]++;
    }
    for(int i=0;i<H->botCount;i++){
        char *pl=H->bots[i],*s=*(char **)(pl+OFF_PLAYER_SOLDIER);BotBrain *b=bot_brain(i);
        if(g_directorIdentity[i]!=pl){memset(&g_director.unit[i],0,sizeof g_director.unit[i]);g_director.unit[i].flag=-1;g_directorIdentity[i]=pl;}
        units[i].active=s && b->enabled && *(float *)(s+OFF_SOLDIER_HEALTH)>0;units[i].team=player_team(pl);
        // An active local/recovery order is obeyed by the brain; the director
        // must not overwrite it or keep a stale attack committed for this bot.
        units[i].commanded=b->commander!=NULL || b->prevCommander!=NULL || g_drive[i].manual || local_order_active(i);
        units[i].mobile=g_air[i].phase && g_air[i].pilot && g_air[i].profile?
            (g_air[i].profile->kind==BM_AIR_JET?3:2):(g_drive[i].phase || player_vehicle(pl)!=NULL);
        units[i].avoid=b->avoidUntil>H->frame?b->avoidFlag:-1;
        if(s){float *pos=player_vehicle(pl)?human_pos(pl):(float *)(s+OFF_SOLDIER_POSITION);if(pos)memcpy(units[i].position,pos,12);}
    }
    g_director.guardDivisor=g_enhanced.aggression>=2?8:g_enhanced.aggression==1?5:3;   // Caspian V2: fewer idle guards, more attackers
    bm_director_update(&g_director,flags,g_flagCount,units,H->botCount,H->frame);
    for(int i=0;i<H->botCount;i++){
        BotBrain *b=bot_brain(i);int role,f=director_target(i,&role);
        if(f<0 || units[i].commanded || g_drive[i].phase || g_air[i].phase || g_fleet[i].vehicle)continue;
        if(role==BM_OBJECTIVE_ATTACK && b->task==TASK_FIGHT && b->prevTask==TASK_NONE &&
           b->taskUntil>H->frame && b->taskUntil-H->frame<=12*30){
            char *enemy=g_fightTarget[i],*body=enemy && player_exists(enemy)?*(char **)(enemy+OFF_PLAYER_SOLDIER):NULL;
            const float *target=body?(float *)(body+OFF_SOLDIER_SPAWN_POSITION):NULL;
            if(target && dist2(units[i].position,target)>80*80 &&
               !bm_director_threat_in_path(units[i].position,flag_pos(f),target))
                end_fight(i,b,"distant side target; resumes objective advance");
        }
        if(b->task!=TASK_NONE)continue;
        if(b->targetFlag!=f){
            nav_cancel_path(i);memset(&g_flagRoute[i],0,sizeof g_flagRoute[i]);
            g_localFlag[i]=g_overflowFlag[i]=0;b->targetFlag=-1;b->enroute=b->waiting=0;
            b->retryAt=0;bot_nav(i)->mode=0;
            diagnostic_note(i,role==BM_OBJECTIVE_GUARD?"director: small flag guard group":role==BM_OBJECTIVE_PATROL?"director: mobile patrol to another flag":"director: advance on enemy/contested flag",NULL);
        }
    }
}
