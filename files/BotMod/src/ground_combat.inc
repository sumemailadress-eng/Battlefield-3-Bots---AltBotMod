// A confirmed visible ground target is a firing position, not a point to
// drive into. Aircraft do not stop a tank's objective journey.
static int drive_combat_hold(int i,DriveState *d,const float *vp)
{
    // AltBotMod V23: off. Armour slowed to 4 m/s for 2 s, drove 6 s, slowed again, for every enemy vehicle in sight - the
    // "slowdown, go" convoys. Gunners lead from a moving hull since V22 (their own speed is in the aim), so they fire on the move.
    if(1){(void)i;(void)d;(void)vp;return 0;}
    if(g_armor[i].job!=ARMOR_NORMAL || d->manual || !g_enhanced.driverWeapons || !bm_armored(bm_vehicle_profile(vehicle_name(d->veh))) ||
       g_gunVeh[i]!=d->veh || !g_gunWeapon[i] || g_gunLostAt[i] ||
       H->frame<d->reverseUntil || H->frame<d->backOutUntil)return 0;
    char *target=g_gunTargetVeh[i];float *tp=NULL;
    if(target){
        if(!vehicle_ok(target) || !vehicle_occupied(target) || bm_air_profile(vehicle_name(target)))return 0;
        char *crew=vehicle_occupant(target);if(!crew || player_team(crew)==player_team(H->bots[i]))return 0;
        tp=vehicle_pos(target,NULL);
    }   // AltBotMod V8: soldiers are shot on the move - stopping for each one was the tanks' "drive, stop, drive, stop"
    if(!tp)return 0;
    float distance=sqrtf(dist2(vp,tp));
    if(distance<=(target?35.0f:25.0f) || distance>=bm_gun_range(&g_enhanced,bm_vehicle_profile(vehicle_name(d->veh)),!target)){
        d->combatHoldAt=0;return 0;
    }
    if(H->frame<d->combatAdvanceUntil)return 0;
    if(!d->combatHoldAt)d->combatHoldAt=H->frame?H->frame:1;
    if(H->frame-d->combatHoldAt>=2*30){   // (V8: slows for 2 s - see drive_tick - then 6 s at full pace)
        d->combatHoldAt=0;d->combatAdvanceUntil=H->frame+6*30;return 0;
    }
    return 1;
}
