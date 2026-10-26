#ifndef GUN_IS_AIRCRAFT
#define GUN_IS_AIRCRAFT(v) (bm_air_profile(vehicle_name(v)) != NULL)   // AltBotMod V23 (core.c: the gunship too)
#endif
#ifndef GUN_SEEN_RECENTLY
#define GUN_SEEN_RECENTLY(i,who) (-1)   // AltBotMod V31.1 (core.c: the gunner's cached line of sight to its target, -1 = trace it)
#endif
#ifndef GUN_ARC_SKIPPED
#define GUN_ARC_SKIPPED(i,p) 0   // AltBotMod V30 (core.c: a target the seat's gun could not turn to)
#endif
// Shared native gunner selection. Main guns favor nearby ground threats over
// distant aircraft; scores include distance and retain a modest aim hysteresis.
// AltBotMod V31.1: every candidate is scored first (no rays), then only the best few are traced for a line of sight, best
// first - the first one in sight is the pick (the same pick as before, when it is among the best GUN_PICK_RAYS). V5-V31
// traced every candidate that beat the best so far: with 58 bots and 600 m door guns, hundreds of rays a tick.
#define GUN_PICK_RAYS 6
typedef struct { float score, d; char *v, *t, *ts; } GunCand;
static void gunner_pick_target(int i,const float *gp)
{
    char *pl=H->bots[i],*own=g_gunVeh[i],*bestPl=NULL,*bestVeh=NULL;
    int team=player_team(pl);const BMVehicleProfile *profile=bm_vehicle_profile(vehicle_name(own));
    static const BMVehicleProfile aaEmplacement={"AA emplacement",BM_AA,900,0,1.7f,4};
    if(stationary_weapon(own)==2)profile=&aaEmplacement;   // Caspian V5: C-RAM / Pantsir prefer aircraft
    float bestScore=INFINITY,bestDistance=0,bestHeight=0,currentScore=INFINITY;
    GunCand c[96]; int nc=0;
    char **vehs; int n=vehicle_list(&vehs);
    for (int k=0;k<n && nc<96;k++) {
        char *v=vehs[k]; int vt=0;
        if (v==own || !vehicle_ok(v) || !vehicle_occupied(v) || GUN_ARC_SKIPPED(i,v)) continue;
        float *tp=vehicle_pos(v,&vt);
        char *crew=vehicle_occupant(v);
        if (crew) vt=player_team(crew);
        if (!tp || !vt || vt==team) continue;
        float d=sqrtf(dist2(tp,gp)+(tp[1]-gp[1])*(tp[1]-gp[1]));
        if (d>=GUN_VEHICLE_RANGE) continue;
        float score=bm_vehicle_target_score(profile,GUN_IS_AIRCRAFT(v) ? BM_TARGET_AIR : BM_TARGET_GROUND,d);   // (V23: the gunship is an aircraft)
        if(crew && crew==g_attacker[i] && H->frame-g_attackedAt[i]<5*30)score=fminf(score-250,d*0.2f-100);   // V16: the vehicle that just hit us
        if (!isfinite(score)) continue;   // (not this gun's job - V17: an aircraft far off for a tank)
        c[nc].score=score; c[nc].d=d; c[nc].v=v; c[nc].t=NULL; c[nc].ts=NULL; nc++;
    }
    char *pm=player_manager(); void **arr=NULL; unsigned count=0;
    if (pm) { memcpy(&arr,pm+0xac,sizeof arr); memcpy(&count,pm+8,sizeof count); }
    for (unsigned k=0;arr && k<count && k<64 && nc<96;k++) {
        char *t=arr[k],*ts;
        if (!t || player_team(t)==team || player_vehicle(t) || !(ts=*(char **)(t+OFF_PLAYER_SOLDIER)) ||
            *(float *)(ts+OFF_SOLDIER_HEALTH)<=0 || GUN_ARC_SKIPPED(i,t)) continue;
        float *tp=(float *)(ts+OFF_SOLDIER_SPAWN_POSITION);
        float d=sqrtf(dist2(tp,gp)+(tp[1]-gp[1])*(tp[1]-gp[1]));
        if (d>=GUN_INFANTRY_RANGE) continue;
        float score=bm_vehicle_target_score(profile,player_is_engineer(t) ? BM_TARGET_ENGINEER : BM_TARGET_INFANTRY,d);
        // AltBotMod V16: a soldier right at the vehicle (C4, a knife, a mine) beats anything far off; and whoever just shot
        // this bot comes before the routine order (tanks "ignored" their attackers, 2026-10-13).
        if(d<25)score=fminf(score,40+d);
        if(t==g_attacker[i] && H->frame-g_attackedAt[i]<5*30)score=fminf(score-250,d*0.2f-100);   // (beats the aim hysteresis too)
        if (!isfinite(score)) continue;
        c[nc].score=score; c[nc].d=d; c[nc].v=NULL; c[nc].t=t; c[nc].ts=ts; nc++;
    }
    for (int a=1;a<nc;a++) { GunCand x=c[a]; int b=a; while (b>0 && c[b-1].score>x.score) { c[b]=c[b-1]; b--; } c[b]=x; }   // best first
    for (int k=0;k<nc;k++) {   // the current target: its score, if it is in sight (core.c: the sight checked in the last 6 frames)
        if (!((c[k].v && c[k].v==g_gunTargetVeh[i]) || (!c[k].v && !g_gunTargetVeh[i] && c[k].t==g_gunTarget[i]))) continue;
        int vis=GUN_SEEN_RECENTLY(i,c[k].v ? c[k].v : c[k].t);
        if (vis<0) {
            if (c[k].v) { float *tp=vehicle_pos(c[k].v,NULL); float aim[3]={tp ? tp[0] : 0,tp ? tp[1]+1 : 0,tp ? tp[2] : 0}; vis=tp && gun_can_see(gp,aim,1); }
            else vis=gun_clear_height(gp,(float *)(c[k].ts+OFF_SOLDIER_SPAWN_POSITION))>=0;
        }
        if (vis) currentScore=c[k].score;
        break;
    }
    for (int k=0,rays=0;k<nc && rays<GUN_PICK_RAYS;k++) {
        rays++;
        if (c[k].v) {
            float *tp=vehicle_pos(c[k].v,NULL); if (!tp) continue;
            float aim[3]={tp[0],tp[1]+1,tp[2]};
            if (!gun_can_see(gp,aim,1)) continue;
            bestScore=c[k].score; bestDistance=c[k].d; bestVeh=c[k].v; bestPl=vehicle_occupant(c[k].v); break;
        }
        float h=gun_clear_height(gp,(float *)(c[k].ts+OFF_SOLDIER_SPAWN_POSITION)); if (h<0) continue;
        bestScore=c[k].score; bestDistance=c[k].d; bestVeh=NULL; bestPl=c[k].t; bestHeight=h; break;
    }
    if (isfinite(currentScore) && currentScore<=bestScore+50 && gunner_target_ok(i,gp)) return;
    if (bestVeh==g_gunTargetVeh[i] && bestPl==g_gunTarget[i]) return;
    if (bestVeh || bestPl) {
        bot_log("gunner: %s engages %s%s at %.0f m (threat score %.0f)",pl+OFF_PLAYER_ONLINE_NAME,
            bestPl ? bestPl+OFF_PLAYER_ONLINE_NAME : "a vehicle",bestVeh ? " (vehicle)" : "",bestDistance,bestScore);
        if (bestPl) bot_spot(pl,bestPl);
    }
    g_gunAimH[i]=bestHeight; g_gunBurstAt[i]=H->frame; g_gunLostAt[i]=0;
    g_gunTarget[i]=bestPl; g_gunTargetVeh[i]=bestVeh;
}
