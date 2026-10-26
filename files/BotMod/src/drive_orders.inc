static int drive_director_tick(int i,DriveState *d,const float *position)
{
    (void)position;
    if(!mode_is_cq() || d->manual || d->phase<2 || d->phase==5 || d->phase==6 || d->phase==7 || d->phase==9 || g_armor[i].job!=ARMOR_NORMAL)return 0;
    int role,f=director_target(i,&role);
    if(f<0 || f==d->flag)return 0;
    d->flag=f;memcpy(d->goal,objective_pos(f),12);d->phase=2;
    d->n=d->next=d->partial=0;d->stageActive=d->stageCandidate=0;d->jobAt=0;
    d->at=H->frame;nav_cancel_path(i+MAX_BOTS);   // (V29: the input is kept - drive_coast rolls on through the re-plan)
    bot_brain(i)->targetFlag=f;bot_brain(i)->waiting=0;bot_brain(i)->enroute=1;
    diagnostic_note(i,role==BM_OBJECTIVE_PATROL?"driver advances to patrol objective":"driver advances to enemy/contested objective",d->veh);
    return 1;
}
// A short, verified driving leg lets a queued driver leave open deployment.
// It is an ordinary partial path; walls, cliffs and occupied roads remain
// collision checked. No blind throttle is issued while a route is unavailable.
static int drive_clear_departure(int i,DriveState *d,const float *position)
{
    if(d->manual || vehicle_is_boat(d->veh) || !d->progressAt || H->frame-d->progressAt<6*30 ||
       (d->dutyMoveAt && H->frame-d->dutyMoveAt<3*30) || !tactic_probe_budget())return 0;
    d->dutyMoveAt=H->frame?H->frame:1;
    NavGrid *grid=*nav_slot();const unsigned char *mask=drive_mask();float *xf=vehicle_xf(d->veh);
    if(!xf)return 0;
    if(!grid || !mask){
        float point[3];
        if(!drive_live_exit(i,d,position,point))return 0;
        nav_cancel_path(i+MAX_BOTS);memcpy(d->pts,point,12);d->n=1;d->next=0;d->partial=1;d->phase=3;
        d->at=d->checkAt=H->frame;memcpy(d->lastPos,position,12);
        diagnostic_note(i,"driver leaves deployment on live checked terrain while cache is unavailable",d->veh);return 1;
    }
    float heading=atan2f(xf[8],xf[10]),want=atan2f(d->goal[0]-position[0],d->goal[2]-position[2]);
    float error=remainderf(want-heading,6.2831853f);
    if(fabsf(error)>1.0f)return 0;
    static const float offsets[]={0,0.45f,-0.45f};float point[3];
    for(int k=0;k<3;k++){
        float angle=want+offsets[k],x=position[0]+sinf(angle)*12,z=position[2]+cosf(angle)*12,floor;
        if(!ground_below(x,z,position[1]+8,position[1]-6,&floor) || fabsf(floor-position[1])>3 ||
           !nav_drive_near(grid,mask,x,floor,z,3,point) || dist2(position,point)<6*6 || !tactic_segment(i,position,point))continue;
        nav_cancel_path(i+MAX_BOTS);memcpy(d->pts,point,12);d->n=1;d->next=0;d->partial=1;d->phase=3;
        d->at=d->checkAt=H->frame;memcpy(d->lastPos,position,12);d->reverseUntil=d->backOutUntil=0;
        diagnostic_note(i,"queued driver takes clear checked departure leg toward objective",d->veh);return 1;
    }
    if(drive_live_exit(i,d,position,point)){
        nav_cancel_path(i+MAX_BOTS);memcpy(d->pts,point,12);d->n=1;d->next=0;d->partial=1;d->phase=3;
        d->at=d->checkAt=H->frame;memcpy(d->lastPos,position,12);
        diagnostic_note(i,"cached departure rejected; takes live checked hull-width route",d->veh);return 1;
    }
    return 0;
}
