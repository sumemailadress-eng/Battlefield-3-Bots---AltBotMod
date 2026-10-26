// AltBotMod V9 - gunfight behaviour adapted from the dev's BotMod public preview (its difficulty profiles, side steps,
// jumps, far crouch, sidearm swaps and grenades at infantry). Their source is not public; the profile numbers come from
// their DLL's table (enhanced.c), the code here is ours, built on AltBotMod's own fight loop.
static int grenade_pitch(float dh, float dy, float *pitch);
static unsigned char g_fightCrouch[MAX_BOTS];        // crouched for a far fight (stands again when it ends / moves)
static signed char g_dodgeDir[MAX_BOTS];             // side step: +1 left, -1 right, 0 none
static unsigned g_dodgeFlipAt[MAX_BOTS], g_dodgeCheckAt[MAX_BOTS], g_jumpFightAt[MAX_BOTS], g_jumpRunAt[MAX_BOTS];
static unsigned char g_dodgeBlocked[MAX_BOTS];       // bit 0: left blocked, bit 1: right blocked (last check)
static signed char g_dodgeLastDir[MAX_BOTS];         // AltBotMod V14: the last step's side (the next one mostly goes back)
static unsigned g_dodgeSteps, g_fightJumps, g_runJumps, g_farCrouches, g_sidearmSwaps, g_infantryNades;
// infantry grenades: 0 none, 1 grenade out, 2 aiming/throwing, 3 rifle back
static unsigned char g_nadeStep[MAX_BOTS];
static unsigned g_nadeAt[MAX_BOTS], g_nadeCooldown[MAX_BOTS];
static float g_nadeAim[MAX_BOTS][3];
static int g_nadeSlot[MAX_BOTS];

// The fire window: an elite bot opens up with 0.5 m of the aim point at range (V8: 0.2 m - it held fire waiting for a
// perfect aim, the "staring" look), never wider than the profile's angle close in. Snipers keep the precise window.
static float fight_tolerance(int i, int recon, float d)
{
    const BMDifficulty *P = bot_profile(i);
    if (P && !recon) {
        const BMWeaponProfile *w = infantry_weapon(i);
        float f = w ? w->aimFactor : 1.0f;
        float t = P->aimMargin * f / fmaxf(1.0f, d);
        return t < 0.0002f ? 0.0002f : t > P->aimTol ? P->aimTol : t;
    }
    float t = g_enhanced.weaponHandling ? bm_weapon_tolerance(&g_enhanced, infantry_weapon(i), d) : bm_infantry_tolerance(&g_enhanced, recon, d);
    // AltBotMod V10: past 150 m a sniper aims at the chest (see fight_tick) and fires within 0.2 m of it (0.12 m before:
    // at 400 m it could wait seconds for an aim that steady)
    if (recon && d > 150.0f && t < 0.2f / d) t = 0.2f / d;
    return t;
}
// A new life: nothing of the last fight's throw / crouch / side step carries over.
static void combat_profile_life_reset(int i)
{
    g_fightCrouch[i] = 0; g_dodgeDir[i] = 0; g_nadeStep[i] = 0; g_dodgeBlocked[i] = 0; g_dodgeLastDir[i] = 0; g_dodgeFlipAt[i] = 0;
}

// Rifle up for a fight: far off (beyond the profile's FarCrouchMeters) and not closing in, crouch - steadier aim -
// with the profile's chance. Guards that are already down stay down.
static void fight_posture_start(int i, char *soldier, float dh)
{
    const BMDifficulty *P = bot_profile(i);
    BotBrain *b = bot_brain(i);
    g_fightCrouch[i] = 0;
    if (!P || P->farCrouch <= 0 || dh < P->farCrouch || g_fightAdvance[i] || b->crouched) return;
    if (*(int *)(soldier + OFF_SOLDIER_POSE) != 0) return;
    if ((rand() % 100) >= (int)(P->farCrouchChance * 100)) return;
    bot_crouch(i);
    g_fightCrouch[i] = 1;
    g_farCrouches++;
}
static void fight_posture_end(int i)
{
    if (g_fightCrouch[i] && !bot_brain(i)->crouched) bot_stand(i);
    g_fightCrouch[i] = 0;
    g_dodgeDir[i] = 0;
    if (g_nadeStep[i]) { press_button(i, SLOT_ACTION(0), 3); g_nadeStep[i] = 0; }
}

// Is there room to step 1.6 m to this side? Knee-high ray clear, floor there within 1 m of ours, no water.
static int dodge_room(const float *me, float yaw, int dir)
{
    float fx = sinf(yaw), fz = cosf(yaw), lx = -fz * dir, lz = fx * dir, hit[3];
    float a[3] = {me[0], me[1] + 0.6f, me[2]}, b[3] = {me[0] + lx * 1.6f, me[1] + 0.6f, me[2] + lz * 1.6f};
    unsigned saved = g_rayFlags; g_rayFlags = RAY_DONT_CHECK_CHARACTER | RAY_CHECK_DETAIL_MESH;
    int ok = !ray_cast(a, b, hit, NULL);
    if (ok) {
        float top[3] = {b[0], me[1] + 1.0f, b[2]}, bottom[3] = {b[0], me[1] - 1.2f, b[2]};
        ok = ray_cast(top, bottom, hit, NULL) && nav_hit_class() != NAV_HIT_WATER && fabsf(hit[1] - me[1]) < 1.0f;
    }
    g_rayFlags = saved;
    return ok;
}

// Every frame after steer_bot: side steps and jumps while fighting, jumps while running. Only adds strafe / a jump
// press to what navigation decided; vaults, fences, ladders and stairs keep full control.
static void combat_dodge_tick(int i)
{
    char *pl = H->bots[i], *s = *(char **)(pl + OFF_PLAYER_SOLDIER);
    const BMDifficulty *P = bot_profile(i);
    if (!P || !s || player_vehicle(pl) || H->frame < g_fenceUntil[i] || H->frame < g_vaultUntil[i] || g_stairOn[i] ||
        H->frame < g_crouchWalkUntil[i] || g_gliding[i] || sniper_holding(i)) { g_dodgeDir[i] = 0; return; }   // (V10: not on a perch)
    BotBrain *b = bot_brain(i);
    BotNav *nav = bot_nav(i);
    float *me = (float *)(s + OFF_SOLDIER_POSITION), yaw = *(float *)(s + OFF_SOLDIER_YAW);
    int pose = *(int *)(s + OFF_SOLDIER_POSE);
    if (b->task == TASK_FIGHT && g_fightTarget[i] && player_exists(g_fightTarget[i]) && !g_nadeStep[i]) {
        char *ts = *(char **)(g_fightTarget[i] + OFF_PLAYER_SOLDIER);
        if (!ts) return;
        float *tp = (float *)(ts + OFF_SOLDIER_SPAWN_POSITION), d = sqrtf(dist2(me, tp));
        int seen = g_fightSeenAt[i] && H->frame - g_fightSeenAt[i] < 15;
        // chasing one that ducked out of sight: run (an elite bot sprints beyond FightSprintMeters / 2)
        if (g_fightChase[i] && nav->mode == 3 && P->fightSprint > 0 && pose == 0 && d > P->fightSprint * 0.5f) g_sprintUntil[i] = H->frame + 4;
        // side steps while it trades shots (stood still or closing in). AltBotMod V14: short steps with pauses, only
        // under fire or close in (SideSteps=1); SideSteps=2 is the V9-V13 constant back-and-forth, 0 none.
        BMDodgeStyle st; int style = bm_dodge_style(&g_enhanced, P, &st);
        int threat = (g_attackedAt[i] && H->frame - g_attackedAt[i] < 60) || (g_hitAt[i] && H->frame - g_hitAt[i] < 60) || d < st.closeMeters;
        if (style && seen && (nav->mode == 1 || nav->mode == 4) && d < st.maxMeters && pose != 2 && (!st.needThreat || threat)) {
            unsigned r = (unsigned)rand() ^ ((unsigned)rand() << 15);
            if (bm_dodge_next(&st, &g_dodgeDir[i], &g_dodgeLastDir[i], &g_dodgeFlipAt[i], H->frame, r)) { g_dodgeCheckAt[i] = 0; g_dodgeSteps++; }
            if (g_dodgeDir[i] && H->frame >= g_dodgeCheckAt[i]) {   // a look to the side every 0.2 s
                g_dodgeCheckAt[i] = H->frame + 6;
                if (!dodge_room(me, yaw, g_dodgeDir[i])) {
                    g_dodgeDir[i] = (signed char)-g_dodgeDir[i];
                    if (!dodge_room(me, yaw, g_dodgeDir[i])) g_dodgeDir[i] = 0;
                    g_dodgeFlipAt[i] = H->frame + (st.restMax > 0 ? st.stepMin : 20);
                }
            }
            if (g_dodgeDir[i]) {
                float level = st.level * (b->step == 2 ? 0.6f : 1.0f);   // a little less while the trigger is down
                H->botMove[i][1] = level * g_dodgeDir[i];
                g_progress[i].frame = H->frame; g_progress[i].x = me[0]; g_progress[i].z = me[2];   // (not "stuck")
            }
        } else if (g_dodgeDir[i]) { g_dodgeLastDir[i] = g_dodgeDir[i]; g_dodgeDir[i] = 0; }
        // a hop between bursts, close in, standing (V14: only under fire, three times rarer, unless SideSteps=2)
        if (P->jumpFight > 0 && st.hopScale > 0 && seen && pose == 0 && d < 40 && b->step != 2 && (!st.hopNeedsThreat || threat) && H->frame >= g_jumpFightAt[i]) {
            if (g_jumpFightAt[i]) { press_button(i, EIA_JUMP, 4); g_fightJumps++; }
            g_jumpFightAt[i] = H->frame + (unsigned)(P->jumpFight * st.hopScale * 30 * (1.0f + (rand() % 100) / 100.0f));
        }
        return;
    }
    g_dodgeDir[i] = 0;
    // running between places: an occasional hop on flat open ground (never at stairs, ladders or a drop)
    BMDodgeStyle rs; bm_dodge_style(&g_enhanced, P, &rs);   // V14: running hops three times rarer (SideSteps=1), none at 0
    if (P->jumpRun > 0 && rs.hopScale > 0 && H->frame < g_sprintUntil[i] && nav->mode == 3 && pose == 0 && b->task != TASK_ROCKET &&
        H->frame >= g_jumpRunAt[i]) {
        int first = !g_jumpRunAt[i];
        g_jumpRunAt[i] = H->frame + (unsigned)(P->jumpRun * rs.hopScale * 30 * (1.0f + (rand() % 100) / 100.0f));
        if (first) return;
        BotPath *p = &g_paths[i];
        if (p->next < p->count && p->link[p->next] >= 0) return;   // a ladder next
        float a[3] = {me[0] + sinf(yaw) * 2.5f, me[1] + 1.0f, me[2] + cosf(yaw) * 2.5f}, bt[3] = {a[0], me[1] - 1.0f, a[2]}, hit[3];
        float c[3] = {me[0], me[1] + 0.5f, me[2]}, cf[3] = {a[0], me[1] + 0.5f, a[2]};
        unsigned saved = g_rayFlags; g_rayFlags = RAY_DONT_CHECK_CHARACTER | RAY_CHECK_DETAIL_MESH;
        int flat = ray_cast(a, bt, hit, NULL) && nav_hit_class() != NAV_HIT_WATER && fabsf(hit[1] - me[1]) < 0.35f && !ray_cast(c, cf, hit, NULL);
        g_rayFlags = saved;
        if (flat) { press_button(i, EIA_JUMP, 4); g_runJumps++; }
    }
}

// Close in with an empty magazine: the sidearm is quicker than a reload (players do it). Returns 1 if it swapped.
static int fight_sidearm_swap(int i, BotBrain *b, char *soldier, float dh, int visible)
{
    const BMDifficulty *P = bot_profile(i);
    if (!P || g_fightSlot[i] != 0 || !visible || dh > 15.0f) return 0;
    int l, s;
    if (!soldier_ammo2(soldier, 0, &l, &s) || l != 0 || s <= 0) return 0;
    int sl, ss;
    if (!soldier_ammo2(soldier, 1, &sl, &ss) || sl + ss <= 0) return 0;   // no pistol rounds
    g_fightSlot[i] = 1;
    press_button(i, SLOT_ACTION(1), 3);
    b->step = 3; b->stepAt = H->frame + 12;
    g_sidearmSwaps++;
    bot_log("fight: %s swaps to its sidearm instead of reloading (%.0f m)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, dh);
    return 1;
}

// A grenade at an infantry target: one that ducked behind cover (out of sight 1.5 - 6 s, last seen 8 - 28 m away), or
// one that sits still in a standoff (no kill after 6 s of shooting, it has not moved 2 m in 3 s). Hard and above, two a
// life, one per 15 s. Returns 1 while the throw has the controls.
static int fight_grenade_tick(int i, BotBrain *b, char *soldier, char *ts, int visible, float dh)
{
    const BMDifficulty *P = bot_profile(i);
    const char *name = (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME;
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION), eye[3] = {me[0], me[1] + 1.6f, me[2]};
    if (g_nadeStep[i]) {
        float dx = g_nadeAim[i][0] - eye[0], dz = g_nadeAim[i][2] - eye[2], hd = sqrtf(dx * dx + dz * dz), pitch;
        if (!grenade_pitch(hd, g_nadeAim[i][1] - eye[1], &pitch) || H->frame - g_nadeAt[i] > 4 * 30) {
            press_button(i, SLOT_ACTION(g_fightSlot[i]), 3); g_nadeStep[i] = 0;
            bot_log("grenade: %s does not throw (out of reach)", name);
            return 0;
        }
        BotNav *nav = bot_nav(i);
        nav->mode = 1; nav->targetYaw = yaw_towards(dx, dz); g_aimPitch[i] = pitch;
        b->step = 3; b->stepAt = H->frame + 2;   // (no trigger from the fight loop meanwhile)
        if (g_nadeStep[i] == 1) {
            if (H->frame - g_nadeAt[i] >= 25) g_nadeStep[i] = 2;   // grenade in hand
            return 1;
        }
        if (g_nadeStep[i] == 2) {
            float yaw = *(float *)(soldier + OFF_SOLDIER_YAW), ty = fmodf(nav->targetYaw, 6.2831853f);
            if (ty > 0) ty -= 6.2831853f;
            float yerr = fabsf(ty - yaw);
            if (yerr > 3.14159265f) yerr = 6.2831853f - yerr;
            if (yerr < 0.07f && fabsf(pitch - *(float *)(soldier + OFF_SOLDIER_PITCH)) < 0.07f) {
                press_button(i, EIA_FIRE, 3);
                g_grenadesThisLife[i]++; g_infantryNades++;
                g_nadeStep[i] = 3; g_nadeAt[i] = H->frame;
                bot_log("grenade: %s throws at %s's position %.0f m away (%.0f deg up)", name, g_fightTarget[i] + OFF_PLAYER_ONLINE_NAME, hd, pitch * 57.2958f);
            }
            return 1;
        }
        if (H->frame - g_nadeAt[i] >= 35) {   // thrown: weapon back
            press_button(i, SLOT_ACTION(g_fightSlot[i]), 3);
            g_nadeStep[i] = 0; b->step = 1; b->stepAt = H->frame + 15;
            return 0;
        }
        return 1;
    }
    if (!P || P->engageRange < 90 || H->frame < g_nadeCooldown[i] || g_grenadesThisLife[i] >= 2) return 0;   // hard and above
    const char *why = NULL;
    float aim[3];
    if (!visible && g_fightSeenAt[i] && H->frame - g_fightSeenAt[i] > 45 && H->frame - g_fightSeenAt[i] < 6 * 30) {
        float d = sqrtf(dist2(me, g_fightLast[i]));
        if (d >= 8 && d <= 28) { why = "it ducked behind cover"; memcpy(aim, g_fightLast[i], 12); }
    } else if (visible && dh >= 10 && dh <= 26 && g_fightBursts[i] >= 4 && H->frame + FIGHT_MAX_FRAMES - b->taskUntil > 6 * 30) {
        float *tp = (float *)(ts + OFF_SOLDIER_SPAWN_POSITION);
        static float stillPos[MAX_BOTS][3]; static unsigned stillAt[MAX_BOTS];
        if (!stillAt[i] || dist2(stillPos[i], tp) > 2 * 2) { memcpy(stillPos[i], tp, 12); stillAt[i] = H->frame ? H->frame : 1; }
        else if (H->frame - stillAt[i] > 3 * 30) { why = *(int *)(ts + OFF_SOLDIER_POSE) == 2 ? "it is lying prone" : "a standoff"; memcpy(aim, tp, 12); stillAt[i] = 0; }
    }
    if (!why) return 0;
    int gs = bot_gadget_slot(i, "M67", NULL);
    if (gs < 0 || (bot_ammo(i, gs) == 0 && g_grenadesThisLife[i] > 0)) { g_nadeCooldown[i] = H->frame + 15 * 30; return 0; }
    float pitch;
    aim[1] += 0.3f;
    float dx = aim[0] - eye[0], dz = aim[2] - eye[2];
    if (!grenade_pitch(sqrtf(dx * dx + dz * dz), aim[1] - eye[1], &pitch)) { g_nadeCooldown[i] = H->frame + 5 * 30; return 0; }
    memcpy(g_nadeAim[i], aim, 12);
    g_nadeSlot[i] = gs;
    g_nadeStep[i] = 1; g_nadeAt[i] = H->frame; g_nadeCooldown[i] = H->frame + 15 * 30;
    if (g_fightCrouch[i]) { bot_stand(i); g_fightCrouch[i] = 0; }
    g_dodgeDir[i] = 0;
    press_button(i, SLOT_ACTION(gs), 3);
    bot_log("grenade: %s goes for a grenade at %s %.0f m away (%s)", name, g_fightTarget[i] + OFF_PLAYER_ONLINE_NAME, sqrtf(dx * dx + dz * dz), why);
    return 1;
}

// Once a minute: what the V9 gunfight behaviour did (only when a profile is on and something happened).
static void combat_stats_tick(void)
{
    static unsigned at, last[6];
    if (!g_enhanced.difficulty || H->frame - at < 60 * 30) return;
    at = H->frame;
    unsigned now[6] = {g_dodgeSteps, g_fightJumps, g_runJumps, g_farCrouches, g_sidearmSwaps, g_infantryNades};
    if (memcmp(now, last, sizeof now))
        bot_log("gunfight: last minute %u side steps, %u fight hops, %u run hops, %u far-fight crouches, %u sidearm swaps, %u grenades at infantry",
                now[0] - last[0], now[1] - last[1], now[2] - last[2], now[3] - last[3], now[4] - last[4], now[5] - last[5]);
    memcpy(last, now, sizeof now);
}
