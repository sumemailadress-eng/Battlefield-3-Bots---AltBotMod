// Retire player identities between rounds. Never force startup deaths to learn spawns.
static void round_roster_tick(void)
{
    static int initialized;static long oldLoads,oldRounds;static void *oldObject;
    static char oldMap[64],oldMode[64];long loads,rounds;chat_level_events(&loads,&rounds);
    const char *map=current_level();char reportedMap[64],mode[64];chat_server_state(reportedMap,sizeof reportedMap,mode,sizeof mode);
    char *ctx=*(char **)ADDR_SERVER_GAME_CONTEXT;void *object=ctx?*(void **)(ctx+0x10):NULL;
    if(map&&(!strcmp(reportedMap,map))&&bm_mode(mode)!=BM_MODE_UNKNOWN){snprintf(g_modeName,sizeof g_modeName,"%s",mode);g_mode=mode_kind(mode);}   // V15: mode_kind keeps a fallback for the round
    int changed=initialized&&(loads!=oldLoads||rounds!=oldRounds||(oldObject&&object!=oldObject)||(map&&strcmp(oldMap,map))||strcmp(oldMode,g_modeName));
    if(g_roundForceReset){changed=1;g_roundForceReset=0;}
    oldLoads=loads;oldRounds=rounds;oldObject=object;if(map)snprintf(oldMap,sizeof oldMap,"%s",map);snprintf(oldMode,sizeof oldMode,"%s",g_modeName);initialized=1;
    if(changed){InterlockedIncrement(&g_roundEpoch);g_roundCleanup=1;g_levelReady=0;g_readdCount=0;g_level[0]=0;g_levelCheckAt=0;
        memset(H->teamBlueprint,0,sizeof H->teamBlueprint);memset(H->teamSpawn,0,sizeof H->teamSpawn);enhanced_level_reset();tickets_reset();
        for(int i=0;i<H->botCount;i++){if(H->botInput[i])for(int a=0;a<60;a++)((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(H->botInput[i],a,0);
            memset(H->botMove[i],0,sizeof H->botMove[i]);chat_rcon("admin.kickPlayer\t%s\tBotMod new round",H->bots[i]+OFF_PLAYER_ONLINE_NAME);queue_bot_removal(i);}
        bot_log("round: epoch %ld, clearing %d old bots before recreating current-mode roster",g_roundEpoch,H->botCount);
    }
    char name[40],weapon[64];while(chat_poll_death(name,sizeof name,weapon,sizeof weapon)){
        if(_stricmp(weapon,"Suicide"))continue;
        int i=bot_index(find_player(name));if(i<0)continue;
        if(roster_ordinal(BOT_ROSTER(H->bots[i]))>=0||H->frame-g_createdAt[i]<60*30||H->frame-g_lastSpawnedAt[i]<30*30){g_silentRemove[i]=1;chat_rcon("admin.kickPlayer\t%s\tBotMod invalid startup soldier",name);queue_bot_removal(i);bot_log("round: retires %s after suicide; creates a fresh player instead of reusing it",name);}
    }
}
