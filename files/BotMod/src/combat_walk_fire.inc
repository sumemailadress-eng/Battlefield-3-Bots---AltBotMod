// Translate the checked flank waypoint into forward/strafe inputs while the
// rifle faces the enemy. Vaulting, fence cutting and ladder links keep control.
static void combat_walk_fire(int i)
{
    if(!g_combatMove[i].until || bot_brain(i)->task!=TASK_FIGHT || bot_nav(i)->mode!=3 ||
       H->frame<g_fenceUntil[i] || H->frame<g_vaultUntil[i] || g_stairOn[i])return;
    BotPath *path=&g_paths[i];
    if(path->next<0 || path->next>=path->count || path->link[path->next]>=0)return;
    char *target=g_fightTarget[i],*pl=H->bots[i];
    if(!target || !player_exists(target))return;
    char *s=*(char **)(pl+OFF_PLAYER_SOLDIER),*enemy=*(char **)(target+OFF_PLAYER_SOLDIER);
    if(!s || !enemy || player_team(target)==player_team(pl))return;
    const float *me=(float *)(s+OFF_SOLDIER_POSITION),*tp=(float *)(enemy+OFF_SOLDIER_SPAWN_POSITION);
    float *point=&path->pts[path->next*3],yaw=*(float *)(s+OFF_SOLDIER_YAW);
    float moveError=remainderf(yaw_towards(point[0]-me[0],point[2]-me[2])-yaw,6.2831853f);
    float look=yaw_towards(tp[0]-me[0],tp[2]-me[2]);
    bot_nav(i)->targetYaw=look;
    H->botMove[i][0]=0.75f*cosf(moveError);
    H->botMove[i][1]=-0.75f*sinf(moveError); // positive strafe moves left
    H->botMove[i][2]=fmaxf(-1,fminf(1,-wrap_err(look,yaw)*6));
}
