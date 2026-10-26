// FunBots separates movement cycles from shooting. Here the native grid must
// validate each relocation: no VU teleport or direct, unchecked flank vectors.
static void combat_motion_cancel(int i)
{
    CombatMoveState *m=&g_combatMove[i];
    if(m->pending)nav_cancel_path(i);
    m->pending=0;m->until=0;m->candidate=0;
}
static int combat_motion_tick(int i,BotBrain *b,char *soldier,const float *enemy,float range)
{
    CombatMoveState *m=&g_combatMove[i];
    const float *me=(float *)(soldier+OFF_SOLDIER_POSITION);
    if(m->soldier!=soldier){memset(m,0,sizeof *m);m->soldier=soldier;memcpy(m->anchor,me,12);m->stillAt=H->frame?H->frame:1;}
    if(m->until){
        if(range<6 || H->frame>=m->until || dist2(me,m->goal)<1.5f*1.5f || bot_nav(i)->mode==1){
            m->until=0;m->nextTry=H->frame+(3+i%4)*30;m->stillAt=H->frame;
            memcpy(m->anchor,me,12);bot_nav(i)->mode=1;
        }else{return 1;}
    }
    if(dist2(me,m->anchor)>2*2){memcpy(m->anchor,me,12);m->stillAt=H->frame;}
    if(range<6 || H->frame<m->nextTry || g_fightRetreat[i] || g_fightMoveUntil[i] || sniper_holding(i))return 0;   // (V10: a sniper keeps its perch)
    NavGrid *grid=*nav_slot();if(!grid || grid->phase!=2 || nav_is_water(grid,me[0],me[1],me[2]))return 0;
    int crowd=0;
    if(!m->pending){
        if((H->frame+(unsigned)i)%15 || H->frame-m->stillAt<(unsigned)(2+i%3)*30)return 0;
        for(int j=0;j<H->botCount;j++){
            char *s=*(char **)(H->bots[j]+OFF_PLAYER_SOLDIER);
            if(j==i || !s || player_team(H->bots[j])!=player_team(H->bots[i]) || player_vehicle(H->bots[j]))continue;
            if(*(float *)(s+OFF_SOLDIER_HEALTH)>0 && dist2(me,(float *)(s+OFF_SOLDIER_POSITION))<10*10)crowd++;
        }
        if(crowd<3 && H->frame-m->stillAt<(unsigned)(3+i%3)*30)return 0;
        // Human follow/guard orders permit local separation, not a long detour.
        float radius=b->prevTask==TASK_FOLLOW || b->prevTask==TASK_DEFEND?6.0f:10.0f+(i%3)*3.0f;
        float dx=enemy[0]-me[0],dz=enemy[2]-me[2],len=sqrtf(dx*dx+dz*dz);
        if(len<1)return 0;
        float side=((i+m->candidate)&1)?1.0f:-1.0f;
        float ahead=0.25f+(m->candidate/2)*0.25f;
        float x=me[0]+(side*dz+ahead*dx)/len*radius;
        float z=me[2]+(-side*dx+ahead*dz)/len*radius;
        // At most one new geometric candidate per tick across all infantry.
        static unsigned checkedAt;
        if(checkedAt==H->frame || !plan_budget_left())return 0;
        checkedAt=H->frame;
        if(!nav_snap(grid,x,me[1],z,m->goal) || fabsf(m->goal[1]-me[1])>3 ||
           dist2(me,m->goal)<4*4 || dist2(enemy,m->goal)<12*12 ||
           nav_is_water(grid,m->goal[0],m->goal[1],m->goal[2]))goto failed;
        for(int j=0;j<H->botCount;j++)if(j!=i && player_team(H->bots[j])==player_team(H->bots[i])){
            if((g_combatMove[j].until || g_combatMove[j].pending) && dist2(m->goal,g_combatMove[j].goal)<4*4)goto failed;
            char *s=*(char **)(H->bots[j]+OFF_PLAYER_SOLDIER);
            if(s && !player_vehicle(H->bots[j]) && dist2(m->goal,(float *)(s+OFF_SOLDIER_POSITION))<3*3)goto failed;
        }
        m->pending=1;
    }
    if(!plan_budget_left())return 0;
    // AltBotMod V34: a relocation that waited for the planner is checked again from where the bot is now - V33 bots
    // "relocated" to a spot 2 m away (they had moved meanwhile), then stood: "not moving, walking straight at its destination"
    if(dist2(me,m->goal)<4*4 || dist2(enemy,m->goal)<12*12)goto failed;
    if(plan_path(i,soldier,m->goal[0],m->goal[1],m->goal[2])){
        m->pending=0;m->candidate=0;m->until=H->frame+6*30;
        bot_nav(i)->mode=3;bot_stand(i);b->crouched=0;
        bot_log("fight: %s relocates along a checked flank to (%.0f, %.0f)",H->bots[i]+OFF_PLAYER_ONLINE_NAME,m->goal[0],m->goal[2]);
        return 1;
    }
    if(g_lastPlanDeferred)return 0;
failed:
    m->pending=0;
    if(++m->candidate>=6){m->candidate=0;m->nextTry=H->frame+6*30;}
    else m->nextTry=H->frame+15;
    return 0;
}
