// Tactical destinations use the existing asynchronous driving planner. No
// obstacle is struck off the map because another vehicle temporarily occupies it.
static unsigned g_tacticProbeFrame;static int g_tacticProbes;
static int tactic_probe_budget(void)
{
    if(g_tacticProbeFrame!=H->frame){g_tacticProbeFrame=H->frame;g_tacticProbes=0;}
    if(g_tacticProbes>=2)return 0;
    g_tacticProbes++;return 1;
}
static int tactic_ray(int i,const float *from,const float *to)
{
    char *v=g_drive[i].veh;void *own=vehicle_ok(v)?*(void **)(v+0x30):NULL;
    float a[3],hit[3],dir[3],len=0;memcpy(a,from,12);
    for(int k=0;k<3;k++){dir[k]=to[k]-from[k];len+=dir[k]*dir[k];}
    len=sqrtf(len);if(len<0.1f)return 1;
    for(int k=0;k<3;k++)dir[k]/=len;
    for(int n=0;n<5;n++){
        if(!ray_cast(a,to,hit,NULL))return 1;
        void *body=NULL;memcpy(&body,g_lastHitRest,sizeof body);
        if(!own || body!=own || n==4)return 0;
        float left=0;for(int k=0;k<3;k++)left+=(to[k]-hit[k])*dir[k];
        if(left<=0.15f)return 1;
        for(int k=0;k<3;k++)a[k]=hit[k]+dir[k]*0.15f;
    }return 0;
}
static int tactic_segment(int i,const float *a,const float *b)
{
    const BMVehicleProfile *p=bm_vehicle_profile(vehicle_name(g_drive[i].veh));
    float dx=b[0]-a[0],dz=b[2]-a[2],len=hypotf(dx,dz);if(len<0.5f)return 1;
    float rx=dz/len,rz=-dx/len;unsigned saved=g_rayFlags;int clear=1;
    g_rayFlags=RAY_DONT_CHECK_CHARACTER|RAY_CHECK_DETAIL_MESH;
    for(int side=-1;side<=1 && clear;side++)for(int h=0;h<2 && clear;h++){
        float off=side*(p->halfWidth+0.15f),up=h?1.8f:0.7f;
        float start[3]={a[0]+rx*off,a[1]+up,a[2]+rz*off};
        float end[3]={b[0]+rx*off,b[1]+up,b[2]+rz*off};
        clear=tactic_ray(i,start,end);
    }
    g_rayFlags=saved;return clear;
}
static float armor_stop_short(int i)
{
    return g_armor[i].job!=ARMOR_NORMAL && g_armor[i].vehicle==g_drive[i].veh?6:fminf(g_enhanced.stopShort,18.0f);
}
static void armor_resume(int i,const char *why)
{
    DriveState *d=&g_drive[i];ArmorNativeState *a=&g_armor[i];
    memcpy(d->goal,a->original,12);a->job=ARMOR_NORMAL;a->nextAt=H->frame+25*30;
    d->phase=2;d->why="armour job ends";d->n=d->next=d->partial=0;d->at=d->checkAt=H->frame;
    d->jobAt=0;d->boxedSince=0;d->reverseUntil=d->backOutUntil=0;
    nav_cancel_path(i+MAX_BOTS);memset(H->botMove[i],0,sizeof H->botMove[i]);
    diagnostic_note(i,why,d->veh);
}
static int armor_route_failed(int i)
{
    if(g_armor[i].job==ARMOR_NORMAL)return 0;
    armor_resume(i,"tactical route unavailable; retains crew and original objective");return 1;
}
static int armor_arrive(int i,const float *vp)
{
    ArmorNativeState *a=&g_armor[i];DriveState *d=&g_drive[i];
    if(a->vehicle!=d->veh || a->job==ARMOR_NORMAL)return 0;
    if(dist2(vp,a->destination)>12*12){armor_resume(i,"tactical route ended short; resumes objective");return 1;}
    d->phase=4;d->jobAt=H->frame;d->checkAt=H->frame;memcpy(d->lastPos,vp,12);
    if(a->job==ARMOR_RETREAT)a->job=ARMOR_REPAIR;
    diagnostic_note(i,a->job==ARMOR_REPAIR?"repair position reached; engineer requested":"armored firing position reached",d->veh);
    return 1;
}
static char *armor_enemy(int i,float **position)
{
    char *v=g_gunTargetVeh[i],*pl=H->bots[i];
    if(vehicle_ok(v) && vehicle_occupied(v) && !bm_air_profile(vehicle_name(v))){
        char *crew=vehicle_occupant(v);if(crew && player_team(crew)!=player_team(pl)){*position=vehicle_pos(v,NULL);return v;}
    }
    *position=NULL;return NULL;
}
static int armor_engineer(int i,const float *vp)
{
    int team=player_team(H->bots[i]),best=-1,slot=-1;float nearest=80*80;
    for(int j=0;j<H->botCount;j++){
        char *p=H->bots[j],*s=*(char **)(p+OFF_PLAYER_SOLDIER);BotBrain *b=bot_brain(j);
        if(j==i || !s || player_team(p)!=team || player_vehicle(p) || !b->enabled)continue;
        if(b->task==TASK_REPAIR && b->serviceTarget==g_drive[i].veh)return 1;
        if(b->task!=TASK_NONE || g_drive[j].phase)continue;
        int tool=bot_gadget_slot(j,"Repairtool","RepairTool");if(tool<0)continue;
        float distance=dist2((float *)(s+OFF_SOLDIER_POSITION),vp);
        if(distance<nearest){nearest=distance;best=j;slot=tool;}
    }
    if(best<0)return 0;
    start_repair(best,g_drive[i].veh,slot);return 1;
}
static void armor_escort(int i,const float *vp,const float *enemy)
{
    ArmorNativeState *a=&g_armor[i];DriveState *d=&g_drive[i];
    if(!g_enhanced.armorEscort || !enemy || H->frame<a->escortAt || d->speed>2)return;
    a->escortAt=H->frame+20*30;
    int team=player_team(H->bots[i]),assigned=0;
    for(int j=0;j<H->botCount && assigned<2;j++){
        char *pl=H->bots[j],*s=*(char **)(pl+OFF_PLAYER_SOLDIER);BotBrain *b=bot_brain(j);
        if(j==i || !s || player_team(pl)!=team || !b->enabled || b->task!=TASK_NONE ||
           player_vehicle(pl) || g_drive[j].phase || g_fleet[j].vehicle)continue;
        float *pos=(float *)(s+OFF_SOLDIER_POSITION);if(dist2(vp,pos)>30*30)continue;
        float dx=enemy[0]-vp[0],dz=enemy[2]-vp[2],len=hypotf(dx,dz);if(len<1)break;
        float side=assigned?-1:1;
        float point[3]={vp[0]+dz/len*side*9-dx/len*6,vp[1],vp[2]-dx/len*side*9-dz/len*6},snap[3];
        if(!nav_snap(*nav_slot(),point[0],point[1],point[2],snap) || fabsf(snap[1]-vp[1])>3)continue;
        assign_task(j,TASK_DEFEND,H->bots[i]);b->taskUntil=g_defendUntil[j]=H->frame+12*30;   // V15: also after a fight
        memcpy(b->postPos,snap,12);b->postFacing=yaw_towards(enemy[0]-snap[0],enemy[2]-snap[2]);b->stepAt=0;
        g_fleet[j].vehicle=NULL;assigned++;
        diagnostic_note(j,"temporary infantry screen for nearby armor",d->veh);
    }
}
static int armor_tick(int i,const float *vp)
{
    DriveState *d=&g_drive[i];ArmorNativeState *a=&g_armor[i];
    if(d->manual || !bm_armored(bm_vehicle_profile(vehicle_name(d->veh))) || d->phase<2 || d->phase==5 || d->phase==6 || d->phase==7)return 0;
    if(a->vehicle!=d->veh){memset(a,0,sizeof *a);a->vehicle=d->veh;a->nextAt=H->frame+(i%8)*3;}
    if(!g_enhanced.armorTactics){if(a->job!=ARMOR_NORMAL){armor_resume(i,"armor tactics disabled; resumes objective");return 1;}return 0;}
    VehTrack *track=vehicle_sample(d->veh);
    float fraction=track && track->maxSeen>0?*(float *)(d->veh+OFF_VEHICLE_HEALTH)/track->maxSeen:1;
    if(!isfinite(fraction) || fraction<g_enhanced.bailHealth)return 0; // Existing bail logic handles wrecks.
    float *enemy=NULL;char *target=armor_enemy(i,&enemy);
    if(a->job!=ARMOR_NORMAL){
        if(H->frame-a->startedAt>45*30){armor_resume(i,"tactical job timed out; resumes objective");return 1;}
        if(d->phase==4){
            if(a->job==ARMOR_REPAIR){
                if(H->frame>=a->repairAt){a->engineer=armor_engineer(i,vp);a->repairAt=H->frame+3*30;}
                if(fraction>=0.65f || H->frame-d->jobAt>(a->engineer?25:8)*30 ||
                   (enemy && dist2(vp,enemy)<50*50 && fraction>g_enhanced.armorRepairHealth)){
                    armor_resume(i,"repair pause complete; retains crew and rejoins objectives");return 1;
                }
            }else if(!target || target!=a->target || H->frame-d->jobAt>10*30){
                armor_resume(i,"firing position complete; resumes objective");return 1;
            }
            H->botMove[i][0]=d->speed>0.5f?-0.6f:0;H->botMove[i][1]=H->botMove[i][2]=0;
            d->checkAt=H->frame;memcpy(d->lastPos,vp,12);d->boxedSince=0;d->at++;
            armor_escort(i,vp,enemy);return 1;
        }
        return 0;
    }
    if(d->phase!=3 && d->phase!=4)return 0;
    armor_escort(i,vp,enemy);
    if(H->frame<a->nextAt || !plan_budget_left() || !tactic_probe_budget())return 0;
    a->nextAt=H->frame+10*30;
    int retreat=fraction<g_enhanced.armorRepairHealth;
    if(!retreat && (!enemy || dist2(vp,d->goal)>90*90 || dist2(vp,enemy)<45*45 || dist2(vp,enemy)>180*180))return 0;
    NavGrid *grid=*nav_slot();const unsigned char *mask=drive_mask();if(!grid || !mask)return 0;
    float dx=enemy?enemy[0]-vp[0]:d->goal[0]-vp[0],dz=enemy?enemy[2]-vp[2]:d->goal[2]-vp[2];
    float len=hypotf(dx,dz);if(len<1)return 0;dx/=len;dz/=len;
    float chosen[3],best=INFINITY;int coverChosen=0;
    // Four bounded candidates; each eventual route gets the ordinary queue.
    for(int n=0;n<4;n++){
        float side=(n&1)?-1:1,distance=g_enhanced.armorTacticMeters*(n<2?0.6f:1);
        float wanted[3]={vp[0]+(retreat?-dx:dz*side)*distance,vp[1],vp[2]+(retreat?-dz:-dx*side)*distance},point[3];
        if(retreat && n>=2){wanted[0]+=dz*side*distance*0.5f;wanted[2]-=dx*side*distance*0.5f;}
        if(!nav_drive_near(grid,mask,wanted[0],wanted[1],wanted[2],12,point) ||
           fabsf(point[1]-vp[1])>12 || dist2(point,vp)<15*15)continue;
        int cover=0;
        if(enemy){
            float muzzle[3]={point[0],point[1]+2.2f,point[2]},body[3]={point[0],point[1]+0.8f,point[2]};
            float threat[3]={enemy[0],enemy[1]+1.5f,enemy[2]};
            int seen=gun_can_see(muzzle,threat,1);cover=solid_between(threat,body,NULL);
            if(!retreat && !seen)continue;
        }
        float score=sqrtf(dist2(point,vp))-(cover?100:0);
        if(retreat && enemy)score-=sqrtf(dist2(point,enemy))*0.4f;
        if(score<best){best=score;memcpy(chosen,point,12);coverChosen=cover;}
    }
    if(!isfinite(best))return 0;
    memcpy(a->original,d->goal,12);memcpy(a->destination,chosen,12);a->target=target;
    a->job=retreat?ARMOR_RETREAT:coverChosen?ARMOR_COVER:ARMOR_FLANK;a->startedAt=H->frame;a->engineer=0;
    memcpy(d->goal,chosen,12);d->phase=2;d->why="armour job position";d->n=d->next=d->partial=0;d->at=H->frame;
    d->reverseUntil=d->backOutUntil=0;d->boxedSince=0;nav_cancel_path(i+MAX_BOTS);
    memset(H->botMove[i],0,sizeof H->botMove[i]);
    diagnostic_note(i,retreat?"armor retreats for repair":coverChosen?"armor seeks hull cover with turret line of sight":"armor changes flank while retaining crew",d->veh);
    return 1;
}

// Short leases protect narrow waypoint corridors. They are pointer keyed so
// roster compaction cannot transfer another driver's right of way.
static struct {char *vehicle;float position[3];unsigned until,createdAt,enteredAt;}g_roadLease[16];
static char *g_leaseHolder;   // the vehicle that holds the stretch when traffic_lease returns 1
static unsigned g_narrowWaits,g_narrowFollows,g_narrowBackOffs;
// Is the road at `at` (heading dx, dz) too narrow for two vehicles side by side? (drive mask either side)
static int traffic_narrow(int i,const float *at,float dx,float dz,float len)
{
    NavGrid *grid=*nav_slot();const unsigned char *mask=drive_mask();if(!grid || !mask || len<0.1f)return 0;
    // AltBotMod V10: room for a second vehicle beside this one (V9 asked 1.5 m more: ordinary two-lane streets counted
    // as "narrow" and oncoming cars queued for each other on them)
    float width=bm_vehicle_profile(vehicle_name(g_drive[i].veh))->halfWidth*2+0.75f,p[3];
    int room=0;for(int side=-1;side<=1;side+=2){
        float x=at[0]+dz/len*width*side,z=at[2]-dx/len*width*side;
        if(nav_drive_near(grid,mask,x,at[1],z,2,p) && hypotf(p[0]-x,p[2]-z)<1.5f)room++;
    }
    return room<2;
}
static int traffic_lease(int i,const float *vp,const float *waypoint)
{
    g_leaseHolder=NULL;
    DriveState *d=&g_drive[i];
    if(H->frame<d->trafficLeaseBackoff)return 0;
    float dx=waypoint[0]-vp[0],dz=waypoint[2]-vp[2],len=hypotf(dx,dz);if(len<1 || len>40)return 0;
    if(!traffic_narrow(i,waypoint,dx,dz,len))return 0;
    float ux=dx/len,uz=dz/len,*mx=vehicle_xf(d->veh);
    int freeSlot=-1,mine=-1;
    for(int k=0;k<16;k++){
        if(g_roadLease[k].until<=H->frame || !vehicle_ok(g_roadLease[k].vehicle)){if(freeSlot<0)freeSlot=k;continue;}
        if(fabsf(g_roadLease[k].position[1]-waypoint[1])>4)continue;
        if(g_roadLease[k].vehicle==d->veh){
            if(dist2(g_roadLease[k].position,waypoint)<=18*18 && mine<0)mine=k;
            continue;
        }
        // V10: the whole stretch ahead (70 m along the way, 7 m either side), not just 18 m round the next waypoint - a
        // vehicle already coming the other way along a bridge holds the far end too (V9: both drove on and met halfway)
        float rx=g_roadLease[k].position[0]-vp[0],rz=g_roadLease[k].position[2]-vp[2];
        float along=rx*ux+rz*uz,side=fabsf(rx*uz-rz*ux);
        int atNext=dist2(g_roadLease[k].position,waypoint)<=18*18;
        if(!atNext && !(along>0 && along<70 && side<7))continue;
        char *hv=g_roadLease[k].vehicle;VehTrack *ht=vehicle_sample(hv);float *hx=vehicle_xf(hv);
        float same=hx && mx?hx[8]*mx[8]+hx[10]*mx[10]:0;
        // V9: a vehicle going the same way through it is simply followed (convoy) - V10 carries on looking past it
        if(ht && ht->speed>=1.5f && same>0){g_narrowFollows++;continue;}
        // both already in the stretch, nose to nose: the one that drove in first has the right of way; the other waits
        // (and backs out a little, once, when they are close)
        if(d->narrowSince && H->frame-d->narrowSeen<3*30 && g_roadLease[k].enteredAt &&
           (d->narrowSince<g_roadLease[k].enteredAt || (d->narrowSince==g_roadLease[k].enteredAt && d->veh<hv)))continue;
        g_leaseHolder=hv;g_narrowWaits++;
        return 1;
    }
    if(!d->narrowSince || H->frame-d->narrowSeen>3*30)d->narrowSince=H->frame?H->frame:1;
    d->narrowSeen=H->frame;
    if(mine>=0){
        // AltBotMod V8 note: an owner 8 s in the same narrow stretch gives the lease up (it is stuck or holding there)
        // and gets no new one for 3 s - it is never told to wait for itself (V7 returned 1 here).
        if(H->frame-g_roadLease[mine].createdAt>8*30){g_roadLease[mine].until=0;d->trafficLeaseBackoff=H->frame+3*30;return 0;}
        g_roadLease[mine].until=H->frame+4*30;g_roadLease[mine].enteredAt=d->narrowSince;return 0;
    }
    if(freeSlot>=0){g_roadLease[freeSlot].vehicle=d->veh;memcpy(g_roadLease[freeSlot].position,waypoint,12);g_roadLease[freeSlot].until=H->frame+4*30;
        g_roadLease[freeSlot].createdAt=H->frame;g_roadLease[freeSlot].enteredAt=d->narrowSince;}
    return 0;
}
static float traffic_speed(int i,const float *vp,const float *xf,const float *waypoint,float limit)
{
    DriveState *d=&g_drive[i];if(!g_enhanced.traffic || vehicle_is_boat(d->veh))return limit;
    if(d->trafficAt && H->frame-d->trafficAt<3)return fminf(limit,d->trafficLimit);
    d->trafficAt=H->frame?H->frame:1;d->trafficLimit=limit;d->trafficWait=d->trafficFollowing=0;d->trafficPeer=NULL;
    char **list;int count=vehicle_list(&list),team=player_team(H->bots[i]);
    float headingLen=hypotf(xf[8],xf[10]);if(headingLen<0.1f)return 0;
    float fx=xf[8]/headingLen,fz=xf[10]/headingLen;
    for(int k=0;k<count && k<128;k++){
        char *other=list[k];if(other==d->veh || !vehicle_ok(other) || !vehicle_occupied(other) || bm_air_profile(vehicle_name(other)))continue;
        char *crew=vehicle_occupant(other);if(!crew || player_team(crew)!=team)continue;
        float *p=vehicle_pos(other,NULL),*ox=vehicle_xf(other);if(!p || !ox || dist2(p,vp)>100*100)continue;
        float oxlen=hypotf(ox[8],ox[10]);if(oxlen<0.1f)continue;
        float dx=p[0]-vp[0],dz=p[2]-vp[2];VehTrack *track=vehicle_sample(other);
        BMTrafficContact contact={dx*fx+dz*fz,dx*fz-dz*fx,p[1]-vp[1],(fx*ox[8]+fz*ox[10])/oxlen,
            track?track->speed:0,d->speed,bm_vehicle_profile(vehicle_name(d->veh))->halfWidth,
            bm_vehicle_profile(vehicle_name(other))->halfWidth,i,bot_index(crew),!is_bot(crew),0};
        contact.crossing=fabsf(contact.alignment)<0.5f;
        BMTrafficDecision result=bm_traffic_contact(&contact,g_enhanced.trafficSpacing,d->trafficLimit);
        if(result.limit<d->trafficLimit || result.yield){d->trafficLimit=result.limit;d->trafficWait=result.yield;d->trafficFollowing=result.following;d->trafficPeer=other;}
    }
    // AltBotMod V10: nose to nose with a friendly vehicle in a narrow stretch (both stopped, each waiting for the other -
    // V9 ended that only by both driving round after 6 s): the one that drove in first has the right of way. The other
    // reverses out of the stretch (up to 6 s, straight back, rear checked) and then pulls aside; the first follows slowly.
    if(d->trafficWait && d->trafficPeer && !d->trafficFollowing && d->narrowSince && H->frame-d->narrowSeen<3*30){
        float *hx=vehicle_xf(d->trafficPeer);int j=-1;
        for(int k=0;k<H->botCount;k++)if(k!=i && g_drive[k].veh==d->trafficPeer && g_drive[k].phase>=2 && g_drive[k].phase<=4){j=k;break;}
        float hl=hx?hypotf(hx[8],hx[10]):0;
        if(j>=0 && hl>0.1f && (hx[8]*fx+hx[10]*fz)/hl<-0.25f){
            DriveState *o=&g_drive[j];
            int oIn=o->narrowSince && H->frame-o->narrowSeen<3*30;
            int first=!oIn || d->narrowSince<o->narrowSince || (d->narrowSince==o->narrowSince && i<j);
            if(first){
                if(H->frame<o->trafficBackUntil || (o->trafficWait && o->trafficPeer==d->veh)){
                    float *op=vehicle_pos(d->trafficPeer,NULL);
                    float w=bm_vehicle_profile(vehicle_name(d->veh))->halfWidth+bm_vehicle_profile(vehicle_name(d->trafficPeer))->halfWidth+1;
                    if(op && hypotf(op[0]-vp[0],op[2]-vp[2])>w*2.0f){d->trafficWait=0;d->trafficLimit=fminf(limit,4.0f);d->trafficPeer=NULL;}
                }
            }else if(!d->trafficBacked){
                d->trafficBacked=1;
                float behind[3]={vp[0]-fx*7,vp[1],vp[2]-fz*7};
                if(tactic_probe_budget() && tactic_segment(i,vp,behind)){d->trafficBackUntil=H->frame+6*30;g_narrowBackOffs++;}
            }
        }
    }
    // AltBotMod V9: narrow stretches - a bridge, an alley, a gate - are one vehicle at a time. A friendly vehicle in
    // the narrow stretch ahead (within 40 m, its road lease) holds this one at the entrance instead of piling in.
    float leasePoint[3];int narrow=0;
    if(!d->trafficWait && waypoint && traffic_lease(i,vp,waypoint)){
        // V10: the holder is recorded as the peer, so a parked holder learns it is in the way (vehicle_queue_behind)
        d->trafficWait=1;d->trafficFollowing=1;d->trafficLimit=0;d->trafficPeer=g_leaseHolder;narrow=1;memcpy(leasePoint,waypoint,12);
    }
    if(d->trafficWait){
        if(!d->trafficSince)d->trafficSince=H->frame;
        // V9: 6 s behind a stopped / blocking vehicle (V7/V8: 10 s, then the very same road again): route round it -
        // the blocked stretch is avoided for 40 s, no Fun-Bots road for this trip - or, with no other way, another flag.
        // V10: one coming through a narrow stretch towards it is waited for (15 s - it is about to come out; V9 gave up
        // after 6 s and drove round, a long detour for a bridge that cleared a moment later); a stopped one: 6 s.
        VehTrack *pt=d->trafficPeer?vehicle_sample(d->trafficPeer):NULL;
        int stuckAhead=!pt || pt->speed<1.0f;
        if(H->frame-d->trafficSince>(stuckAhead?6:narrow?15:12)*30){
            float *bp=d->trafficPeer?vehicle_pos(d->trafficPeer,NULL):NULL;
            if(bp || narrow){memcpy(d->avoidPosition,bp?bp:leasePoint,12);d->avoidRadius=10.0f;d->avoidUntil=H->frame+25*30;}
            d->traceTries=99;
            nav_cancel_path(i+MAX_BOTS);d->phase=2;d->n=d->next=0;d->trafficSince=0;
            diagnostic_note(i,narrow?"waited 6 s at a narrow stretch another vehicle holds; routes round it":
                              "blocked 6 s behind a stopped vehicle; routes round it (another road or flag)",d->veh);
        }
    }else{d->trafficSince=0;d->trafficBacked=0;}
    return fminf(limit,d->trafficLimit);
}
static int traffic_controls(int i,const float *vp,const float *xf,float *throttle,float *steer)
{
    DriveState *d=&g_drive[i];if(!g_enhanced.traffic || !d->trafficWait)return 0;
    *throttle=d->speed>0.5f?-0.6f:0;*steer=0;
    d->checkAt=H->frame;memcpy(d->lastPos,vp,12);d->boxedSince=0;d->at++;
    // V10: nose to nose in a narrow stretch and the other has the right of way: two seconds straight back, clear of its way out
    if(H->frame<d->trafficBackUntil){
        float hl=hypotf(xf[8],xf[10]);
        if((H->frame-d->trafficSince)%15==0 && hl>0.1f && !traffic_narrow(i,vp,xf[8]/hl,xf[10]/hl,1))d->trafficBackUntil=0;   // out: pull aside
        else{*throttle=-0.6f;diagnostic_note(i,"backs out of a narrow stretch for an oncoming friendly vehicle",d->veh);return 1;}
    }
    if(!d->trafficFollowing && d->trafficPeer && d->speed<2 && H->frame-d->trafficSince>30 && tactic_probe_budget()){
        float len=hypotf(xf[8],xf[10]);if(len<0.1f)return 1;
        float pull[3]={vp[0]-xf[8]/len*4+xf[10]/len*5,vp[1],vp[2]-xf[10]/len*4-xf[8]/len*5},snap[3];
        NavGrid *grid=*nav_slot();const unsigned char *mask=drive_mask();
        if(grid && mask && nav_drive_near(grid,mask,pull[0],pull[1],pull[2],2,snap) &&
           fabsf(snap[1]-vp[1])<2 && tactic_segment(i,vp,snap)){
            // Pull back into verified free road on the right; brake/reverse
            // steering is opposite to forward steering in the native input.
            *throttle=-0.35f;*steer=0.5f;
        }
    }
    diagnostic_note(i,"yields to traffic; crew retained",d->veh);return 1;
}
