// Only native CSEs linked to a team's non-capturable HQ, or explicit-team
// children immediately beside that HQ, qualify. No sacrificial spawn probes.
#define HQ_OPTIONS 8
static void *g_hqOptions[3][HQ_OPTIONS];
static float g_hqScores[3][HQ_OPTIONS];
static int g_hqOptionCount[3],g_hqOptionCursor[3];
static unsigned g_hqFailedAt[3];
static int cse_valid(void *e);

static void hq_add_option(int team,void *entity,float score)
{
    int n=g_hqOptionCount[team],at=0;
    for(int k=0;k<n;k++)if(g_hqOptions[team][k]==entity)return;
    while(at<n && g_hqScores[team][at]<=score)at++;
    if(at>=HQ_OPTIONS)return;
    if(n<HQ_OPTIONS)n++;
    for(int k=n-1;k>at;k--){g_hqOptions[team][k]=g_hqOptions[team][k-1];g_hqScores[team][k]=g_hqScores[team][k-1];}
    g_hqOptions[team][at]=entity;g_hqScores[team][at]=score;g_hqOptionCount[team]=n;
}
static int hq_is_option(int team,void *entity)
{
    if(team<1 || team>2 || !entity)return 0;
    for(int k=0;k<g_hqOptionCount[team];k++)if(g_hqOptions[team][k]==entity)return 1;
    return 0;
}
static int hq_select_option(int team)
{
    if(team<1 || team>2)return 0;
    int n=g_hqOptionCount[team];
    for(int step=0;step<n;step++){
        int k=(g_hqOptionCursor[team]+step)%n;void *e=g_hqOptions[team][k];
        if(!cse_valid(e))continue; // discovery already validated the native HQ owner
        H->teamSpawn[team].entity=e;H->teamSpawn[team].a2=0xffffffffu;
        H->teamSpawn[team].a3=1;H->teamSpawn[team].a4=0;
        g_hqOptionCursor[team]=k;
        return 1;
    }
    return 0;
}
static void hq_spawn_refused(int team,void *entity)
{
    if(team<1 || team>2 || H->teamSpawn[team].entity!=entity)return;
    if(g_hqFailedAt[team] && H->frame-g_hqFailedAt[team]<30)return;
    g_hqFailedAt[team]=H->frame?H->frame:1;
    int n=g_hqOptionCount[team];
    if(n<2)return;
    g_hqOptionCursor[team]=(g_hqOptionCursor[team]+1)%n;
    H->teamSpawn[team].entity=NULL;
    if(g_fleetBase[team]==entity)g_fleetBase[team]=NULL;
    if(hq_select_option(team))
        bot_log("spawn: team %d HQ refused deployment; next attempt uses native HQ option %d/%d",team,g_hqOptionCursor[team]+1,n);
}
