// Firing cycle families follow FunBots WeaponClass.lua. Range/ADS disciplines
// are conservative BotMod policies, not claimed EBX statistics. Bullet speed
// remains the beta's 600 m/s fallback until native projectile data is bound.
static const BMWeaponProfile bm_weaponFamilies[] = {
    {"rifle", BM_W_RIFLE, 15, 9, 300, 1.0f, 1.0f, 18},
    {"burst rifle", BM_W_BURST, 3, 3, 300, 1.0f, 0.9f, 18},
    {"carbine", BM_W_CARBINE, 12, 7, 240, 0.85f, 1.0f, 15},
    {"machine gun", BM_W_LMG, 45, 9, 300, 1.1f, 1.0f, 16},
    {"semiautomatic rifle", BM_W_DMR, 1, 10, 500, 1.0f, 0.65f, 8},
    {"bolt rifle", BM_W_BOLT, 1, 30, 900, 1.0f, 0.6f, 6},   // AltBotMod V10: reach set by SniperEngageMeters (V9: 500 m cap)
    {"PDW", BM_W_PDW, 9, 6, 140, 0.65f, 1.0f, 12},
    {"shotgun", BM_W_SHOTGUN, 1, 12, 55, 0.35f, 1.3f, 10},
    {"pistol", BM_W_PISTOL, 1, 6, 80, 0.45f, 1.1f, 12}
};
const BMWeaponProfile *bm_weapon_profile(const char *asset,int recon,int sidearm)
{
    if(sidearm)return &bm_weaponFamilies[BM_W_PISTOL];
    char name[192];size_t n=0;
    if(asset)for(size_t k=0;asset[k] && n+1<sizeof name;k++)
        if(isalnum((unsigned char)asset[k]))name[n++]=(char)toupper((unsigned char)asset[k]);
    name[n]=0;
    static const char *const groups[][12]={
        {"M16A4","M16BURST","AN94","KH2002",NULL},
        {"M4A1","AKS74","G36C","SCARH","SG553","A91","ACWR","QBZ95","MTAR","G53",NULL},
        {"M249","M240","PECHENEG","RPK","M60","M27IAR","L86","LSAT","TYPE88","MG36","QBB95",NULL},
        {"MK11","SVD","M39","QBU88","SKS","M417",NULL},
        {"SV98","M40A5","M98B","JNG90","L96",NULL},
        {"UMP45","MP7","P90","PP2000","PP19","ASVAL","PDWR","MP5","MPX",NULL},
        {"870","SAIGA","USAS","SPAS","DAO12","M1014","MK3A1",NULL}
    };
    static const enum BMWeaponFamily families[]={BM_W_BURST,BM_W_CARBINE,BM_W_LMG,BM_W_DMR,BM_W_BOLT,BM_W_PDW,BM_W_SHOTGUN};
    for(size_t k=0;k<sizeof groups/sizeof groups[0];k++)for(int j=0;groups[k][j];j++)
        if(strstr(name,groups[k][j]))return &bm_weaponFamilies[families[k]];
    return &bm_weaponFamilies[recon?BM_W_DMR:BM_W_RIFLE];
}
float bm_weapon_range(const BMConfig *c,const BMWeaponProfile *w)
{
    if(!w)w=&bm_weaponFamilies[BM_W_RIFLE];
    int sniper=w->family==BM_W_DMR || w->family==BM_W_BOLT;
    if(w->family==BM_W_BOLT)return fminf(w->maxRange,fmaxf(c->sniperRange,c->reconRange));   // AltBotMod V10
    float k=bm_aggr(c,1,1.1f,1.15f);   // Caspian V3 (V2 was 1.2/1.35): engage a little further, not at hopeless range
    return fminf(w->maxRange,(sniper?c->reconRange:c->infantryRange)*w->rangeFactor*k);
}
int bm_weapon_burst(const BMWeaponProfile *w,float d)
{
    if(!w)w=&bm_weaponFamilies[BM_W_RIFLE];
    if(w->family==BM_W_LMG)return d>100?12:d>50?24:w->burst;
    if(w->family==BM_W_RIFLE || w->family==BM_W_CARBINE)return d>120?4:d>60?7:w->burst;
    if(w->family==BM_W_PDW)return d>65?5:w->burst;
    return w->burst;
}
int bm_weapon_pause(const BMWeaponProfile *w,float d)
{
    if(!w)w=&bm_weaponFamilies[BM_W_RIFLE];
    return w->pause+(d>120 && w->burst>3?3:0);
}
float bm_weapon_tolerance(const BMConfig *c,const BMWeaponProfile *w,float d)
{
    if(!w)w=&bm_weaponFamilies[BM_W_RIFLE];
    // Caspian V3: the V2 aggression scaling (x1.8) widened the window to ~0.9 m at 50 m: shots went past the body.
    return clampf(c->infantryAimMargin*w->aimFactor/fmaxf(1,d),0.0002f,0.05f);
}
int bm_weapon_stable(float yaw,float pitch,float tolerance)
{
    return isfinite(yaw) && isfinite(pitch) && isfinite(tolerance) && tolerance>0 &&
           fabsf(yaw)<=tolerance*2.5f && fabsf(pitch)<=tolerance*2.5f;
}
BMTrafficDecision bm_traffic_contact(const BMTrafficContact *c,float spacing,float limit)
{
    BMTrafficDecision out={limit,0,0};
    if(!c || !isfinite(c->ahead) || !isfinite(c->lateral) || !isfinite(c->height) ||
       !isfinite(c->alignment) || !isfinite(c->ownSpeed) || !isfinite(c->otherSpeed) ||
       !isfinite(c->halfWidth) || !isfinite(c->otherWidth) || !isfinite(spacing) || !isfinite(limit)){
        out.limit=0;out.yield=1;return out;
    }
    if(fabsf(c->height)>4 || c->ahead < -2 || c->ahead>100)return out;
    float width=c->halfWidth+c->otherWidth+1.0f;
    if(fabsf(c->lateral)>width+1)return out;
    float gap=fmaxf(spacing,width*2)+fmaxf(0,c->ownSpeed)*0.5f;
    // AltBotMod V24: a convoy follows at the leader's pace - closer than the gap it runs a little slower, further back a
    // little faster - and only stops when about to touch it. V9-V23 stopped dead whenever it got inside 65% of the gap
    // (13-18 m at speed), so every follower on a road went stop, go, stop, go behind the one in front. A leader on a bend
    // (up to 70 deg off) is still a leader, not a crossing.
    if(c->alignment>0.35f){
        out.following=1;
        out.limit=fminf(limit,fmaxf(0,c->otherSpeed+(c->ahead-gap)*0.5f));
        if(c->ahead<width*1.5f+2.0f){out.yield=1;out.limit=0;}
    }else if(c->alignment < -0.5f || c->crossing){
        if(c->ahead>fmaxf(25,c->ownSpeed*2+c->otherSpeed))return out;
        out.limit=fminf(limit,5);
        if(c->human || c->ownId>c->otherId){out.yield=1;out.limit=0;}
        else if(c->ahead<width*2){out.yield=1;out.limit=0;}
    }
    return out;
}
BMLandingDecision bm_transport_landing(BMLandingState *s,const BMLandingObservation *o,float dt)
{
    BMLandingDecision out={30,0,0,0,0};
    if(!s || !o || !isfinite(dt) || dt<=0 || dt>0.25f || !isfinite(o->distance) ||
       !isfinite(o->clearance) || !isfinite(o->horizontalSpeed) || !isfinite(o->verticalSpeed)){
        out.abort=1;return out;
    }
    s->elapsed+=dt;
    if(s->phase==BM_LAND_NONE){s->phase=BM_LAND_APPROACH;s->elapsed=s->settled=0;}
    if(s->phase!=BM_LAND_ASCEND && (!o->padValid || o->threatened || s->elapsed>50 ||
        (s->phase==BM_LAND_DESCEND && (o->distance>12 || o->horizontalSpeed>6)))){
        s->phase=BM_LAND_ASCEND;s->elapsed=s->settled=0;out.abort=1;
    }
    if(s->phase==BM_LAND_APPROACH){
        out.height=fmaxf(12,fminf(70,o->distance*0.3f));
        if(o->distance<6 && o->horizontalSpeed<2.5f){s->phase=BM_LAND_DESCEND;s->elapsed=s->settled=0;}
    }
    if(s->phase==BM_LAND_DESCEND){
        out.height=1.8f;
        if(o->clearance<2.8f && o->clearance>=0 && o->horizontalSpeed<1.2f && fabsf(o->verticalSpeed)<1){
            s->settled+=dt;
            if(s->settled>=1){s->phase=BM_LAND_UNLOAD;s->elapsed=0;}
        }else s->settled=0;
    }
    if(s->phase==BM_LAND_UNLOAD){
        out.height=1.8f;
        // Keep the landing stable until each cargo bot can leave on foot.
        if(o->distance>8 || o->clearance>3.5f || o->horizontalSpeed>2 || fabsf(o->verticalSpeed)>1.5f){
            s->phase=BM_LAND_DESCEND;s->settled=s->elapsed=0;return out;
        }
        out.board=o->pickup;out.unload=!o->pickup && o->cargo;
        if((!o->pickup && !o->cargo && s->elapsed>2) || (o->pickup && o->cargo && s->elapsed>3) || s->elapsed>12){
            s->phase=BM_LAND_ASCEND;s->elapsed=0;out.board=out.unload=0;
        }
    }
    if(s->phase==BM_LAND_ASCEND){out.height=45;if(o->clearance>30){out.finished=1;s->phase=BM_LAND_NONE;s->elapsed=s->settled=0;}}
    return out;
}
BMMissileDecision bm_air_missile(BMMissileState *s,const BMMissileObservation *o,float dt)
{
    BMMissileDecision out={0,0,0};
    if(!s || !o || !isfinite(dt) || dt<=0 || dt>0.25f)return out;
    s->cooldown=fmaxf(0,s->cooldown-dt);s->counterCooldown=fmaxf(0,s->counterCooldown-dt);
    if(o->incomingConfirmed && isfinite(o->incomingSeconds) && o->incomingSeconds>0 &&
       o->incomingSeconds<2 && s->counterCooldown<=0){out.countermeasure=1;s->counterCooldown=25;}
    if(s->target!=o->target){s->target=o->target;s->lockSeconds=0;}
    if(!o->available || !o->target || !o->targetAir || !o->hostile || !o->visible ||
       !isfinite(o->distance) || !isfinite(o->yawError) || !isfinite(o->pitchError) ||
       o->distance<70 || o->distance>1200 || fabsf(o->yawError)>0.12f || fabsf(o->pitchError)>0.12f){
        s->lockSeconds=0;return out;
    }
    out.selectSecondary=1;s->lockSeconds+=dt;
    // Aim alignment never substitutes for a confirmed native missile lock.
    if(o->nativeLock && s->lockSeconds>=1 && s->cooldown<=0){out.fire=1;s->cooldown=4;s->lockSeconds=0;}
    return out;
}
