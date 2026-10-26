// Complete an escape before a queued route can take control. Every reverse
// step checks the rear corridor; retained crew never receive blind throttle.
static int drive_reverse_tick(int i,DriveState *d,const float *vp)
{
    if((d->phase!=2 && d->phase!=3 && d->phase!=10) || vehicle_is_boat(d->veh) || (H->frame>=d->reverseUntil && H->frame>=d->backOutUntil))return 0;
    float *xf=vehicle_xf(d->veh);if(!xf)return 0;
    float length=hypotf(xf[8],xf[10]);if(length<0.1f)return 0;
    float back[3]={vp[0]-xf[8]/length*2.5f,vp[1],vp[2]-xf[10]/length*2.5f};
    if(!tactic_segment(i,vp,back)){
        d->reverseUntil=d->backOutUntil=0;d->awayOn=0;
        // AltBotMod V26: on a route it keeps the route - no re-plan (V6-V25 re-planned the same route, which turned the
        // same way and backed into the same wall: V25 live, a tank on Firestorm took the same route 20 times in 2 s).
        // It turns forwards on full lock instead (drive_tick) and the stuck checks take it from there.
        if(d->phase==3){d->rearBlockedUntil=H->frame+60;diagnostic_note(i,"rear blocked; keeps its route and turns forwards",d->veh);return 0;}
        d->phase=2;d->why="rear blocked while reversing";nav_cancel_path(i+MAX_BOTS);
        memset(H->botMove[i],0,sizeof H->botMove[i]);
        diagnostic_note(i,"rear blocked; cancels reverse and replans with crew seated",d->veh);
        return 1;
    }
    float heading=atan2f(xf[8],xf[10]);
    float want=d->awayOn?d->awayHeading:
        (d->next>=0 && d->next<d->n?atan2f(d->pts[d->next*3]-vp[0],d->pts[d->next*3+2]-vp[2]):heading);
    float error=remainderf(want-heading,6.2831853f);
    H->botMove[i][0]=-0.55f;H->botMove[i][1]=0;
    H->botMove[i][2]=H->frame<d->backOutUntil?0:fmaxf(-1,fminf(1,error*1.5f));
    d->checkAt=H->frame;memcpy(d->lastPos,vp,12);
    return 1;
}

// Caspian V4: a driver that means to move (drive_tick saw a target speed or an obstacle ahead) but has not moved
// 2.5 m in 6 s does what a person does: reverse on full lock for 1.6 s, then forward on the opposite lock for 1.6 s,
// alternating sides each try, with no sensor veto. In V3 a Growler at the US base sat at 0 m/s for 45 s: the obstacle
// limiter held it, "rear blocked" cancelled every reverse and re-planned the same route, until the watchdog made the
// bot get out. After 4 tries without getting 6 m away, the spot ahead is struck off the drivable map and re-planned.
#define UNSTICK_STILL_FRAMES (6 * 30)
#define UNSTICK_LEG_FRAMES   48
static unsigned g_unsticks;
static int drive_unstick_tick(int i,DriveState *d,const float *vp)
{
    if(d->manual || vehicle_is_boat(d->veh) || (d->phase!=2 && d->phase!=3 && d->phase!=10)){d->unstickAt=d->unstickUntil=0;d->unstickCount=0;return 0;}
    if(H->frame<d->unstickUntil){
        float side=(d->unstickCount&1)?1.0f:-1.0f;
        int back=d->unstickStage==0;
        H->botMove[i][0]=back?-0.85f:0.75f;H->botMove[i][1]=0;H->botMove[i][2]=back?side:-side;
        if(back && H->frame+1>=d->unstickUntil){d->unstickStage=1;d->unstickUntil=H->frame+UNSTICK_LEG_FRAMES;}
        d->checkAt=H->frame;memcpy(d->lastPos,vp,12);d->boxedSince=0;
        d->unstickAt=H->frame;memcpy(d->unstickPos,vp,12);
        return 1;
    }
    if(d->trafficWait || d->trafficFollowing || H->frame<d->pickupUntil || (d->combatHoldAt && H->frame-d->combatHoldAt<4*30) ||
       !d->wantMoveAt || H->frame-d->wantMoveAt>150 ||   // (the older reverse moves skip the steering code for up to 2.5 s)
       !d->unstickAt || dist2(vp,d->unstickPos)>2.5f*2.5f){
        d->unstickAt=H->frame?H->frame:1;memcpy(d->unstickPos,vp,12);
        if(d->unstickCount && dist2(vp,d->unstickFrom)>6.0f*6.0f)d->unstickCount=0;   // got away: start over next time
        return 0;
    }
    if(H->frame-d->unstickAt<UNSTICK_STILL_FRAMES)return 0;
    if(!d->unstickCount)memcpy(d->unstickFrom,vp,12);
    if(++d->unstickCount>4){
        unsigned char *mask=g_driveMask;float *xf=vehicle_xf(d->veh);
        if(mask && xf){
            float ax=vp[0]+xf[8]*6.0f,az=vp[2]+xf[10]*6.0f;
            int k=drive_block_for_job(i,mask,ax,az,4.0f,vp[1]);
            bot_log("drive: %s still stuck at (%.0f, %.0f) after 4 unstick tries - (%.0f, %.0f) struck off (%d node(s)), new route",
                    H->bots[i]+OFF_PLAYER_ONLINE_NAME,vp[0],vp[2],ax,az,k);
        }
        d->unstickCount=0;d->phase=2;d->why="4 unstick tries";d->n=d->next=0;nav_cancel_path(i+MAX_BOTS);
        d->unstickAt=H->frame;memcpy(d->unstickPos,vp,12);
        return 0;
    }
    d->unstickStage=0;d->unstickUntil=H->frame+UNSTICK_LEG_FRAMES;d->reverseUntil=d->backOutUntil=0;d->awayOn=0;
    if(g_unsticks++%5==0 || d->unstickCount==1)
        bot_log("drive: %s has not moved for 6 s at (%.0f, %.0f) - reverses on full lock, then pulls away the other way (try %d)",
                H->bots[i]+OFF_PLAYER_ONLINE_NAME,vp[0],vp[2],d->unstickCount);
    diagnostic_note(i,"stalled 6 s: scripted reverse-and-turn unstick",d->veh);
    return drive_unstick_tick(i,d,vp);
}
