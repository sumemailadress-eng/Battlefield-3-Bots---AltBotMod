// ---- AltBotMod V16: gadget variety. The captured kits carry one gadget per slot (engineers an SMAW / RPG-7, support a
// claymore, recon a T-UGS), so bots never had C4, a Stinger / Igla, a Javelin or a SOFLAM. Each life a bot's gadget slots
// now take one of the class's real options (asset names read from the level data: SoldierWeaponUnlockAsset each), in turn
// across the team, and AA launchers only where the level has aircraft. ----
static int kit_item_skipped(const char *asset); static const char *bot_role_class(const char *name); static const char *bot_roster(void *pl); static int vehicle_list(char ***out); static int vehicle_ok(char *v); static const char *vehicle_name(char *v);
typedef struct BMGadgetDef { const char *cls; int slot, team, weight; const char *name; } BMGadgetDef;
static const BMGadgetDef g_gadgetDefs[] = {
    {"Engineer", 2, 1, 3, "Weapons/SMAW/U_SMAW"},           {"Engineer", 2, 2, 3, "Weapons/RPG7/U_RPG7"},
    {"Engineer", 2, 0, 2, "Weapons/FGM148/U_FGM148"},       // Javelin: lock-on, both sides
    {"Engineer", 2, 1, 2, "Weapons/FIM92A/U_FIM92"},        {"Engineer", 2, 2, 2, "Weapons/Sa18IGLA/U_Sa18IGLA"},   // AA
    {"Support", 5, 0, 1, "Weapons/Gadgets/Claymore/U_Claymore"}, {"Support", 5, 0, 1, "Weapons/Gadgets/C4/U_C4"},
    {"Recon", 2, 0, 2, "Weapons/Gadgets/T-UGS/U_UGS"},      {"Recon", 2, 0, 1, "Weapons/Gadgets/SOFLAM/U_SOFLAM"},
};
#define BM_GADGET_DEFS ((int)(sizeof g_gadgetDefs / sizeof g_gadgetDefs[0]))
static void *g_gadgetAsset[BM_GADGET_DEFS];
static void *g_botGadget[MAX_BOTS][8];        // the gadget each bot carries in each slot this life (NULL: the captured one)
static unsigned g_gadgetLife[MAX_BOTS];
static int gadget_is_aa(const char *n) { return strstr(n, "FIM92") || strstr(n, "IGLA"); }
static void gadget_variety_load(void)
{
    memset(g_gadgetAsset, 0, sizeof g_gadgetAsset);
    if (!g_enhanced.gadgetVariety) return;
    Fixup fx[BM_GADGET_DEFS]; memset(fx, 0, sizeof fx);
    for (int k = 0; k < BM_GADGET_DEFS; k++) {
        fx[k].slot = &g_gadgetAsset[k]; *fx[k].slot = (void *)-1;
        snprintf(fx[k].name, sizeof fx[k].name, "%s", g_gadgetDefs[k].name);
        fx[k].vtable = ADDR_WEAPON_UNLOCK_VTABLE;
    }
    fast_resolve_assets(fx, BM_GADGET_DEFS);
    asset_cache_save(fx, BM_GADGET_DEFS);
    int ready = 0;
    for (int k = 0; k < BM_GADGET_DEFS; k++) {
        void *a = g_gadgetAsset[k];
        if (!a || a == (void *)-1 || IsBadReadPtr(a, 4) || *(unsigned *)a != ADDR_WEAPON_UNLOCK_VTABLE || kit_item_skipped(g_gadgetDefs[k].name)) g_gadgetAsset[k] = NULL;
        else ready++;
    }
    bot_log("gadgets: %d of %d class gadget options on this level (C4, Javelin, Stinger / Igla, SOFLAM ...)", ready, BM_GADGET_DEFS);
}
static int level_has_aircraft(void)
{
    char **list; int n = vehicle_list(&list);
    for (int k = 0; k < n && k < 256; k++) if (vehicle_ok(list[k]) && bm_air_profile(vehicle_name(list[k]))) return 1;
    return 0;
}
// AltBotMod V17: an enemy aircraft in the air right now (occupied, off the ground) - engineers then carry more Stingers / Iglas
static char *vehicle_occupant(char *v); static int player_team(char *p); static float *vehicle_pos(char *v, int *team); static int vehicle_on_ground(const float *p);
static int enemy_air_up(int team)
{
    char **list; int n = vehicle_list(&list);
    for (int k = 0; k < n && k < 256; k++) {
        char *v = list[k], *crew; float *p;
        if (!vehicle_ok(v) || !bm_air_profile(vehicle_name(v)) || !(crew = vehicle_occupant(v)) || player_team(crew) == team) continue;
        if ((p = vehicle_pos(v, NULL)) && !vehicle_on_ground(p)) return 1;
    }
    return 0;
}
// Pick the gadget for one slot of a bot's kit this life; NULL keeps the captured one.
static void *gadget_variant_pick(char *bot, const char *cls, int slot, int team)
{
    if (!g_enhanced.gadgetVariety || !cls) return NULL;
    int cand[BM_GADGET_DEFS], nc = 0, total = 0, air = -1;
    for (int k = 0; k < BM_GADGET_DEFS; k++) {
        const BMGadgetDef *d = &g_gadgetDefs[k];
        if (!g_gadgetAsset[k] || d->slot != slot || strcmp(d->cls, cls) || (d->team && d->team != team)) continue;
        if (gadget_is_aa(d->name)) { if (air < 0) air = level_has_aircraft() ? 1 + enemy_air_up(team) : 0; if (!air) continue; }
        cand[nc++] = k; total += gadget_is_aa(d->name) && air == 2 ? d->weight * 3 : d->weight;   // V17: enemy aircraft up - more AA
    }
    if (nc < 2 || total <= 0) return NULL;
    int nth = 0, bi = bot_index(bot);
    for (int k = 0; k < H->botCount && H->bots[k] != bot; k++) if (player_team(H->bots[k]) == team && bot_role_class(BOT_ROSTER(H->bots[k])) &&
        !strcmp(bot_role_class(BOT_ROSTER(H->bots[k])), cls)) nth++;
    unsigned life = bi >= 0 ? g_gadgetLife[bi] : 0;
    int ticket = (int)((nth * 7u + life * 3u) % (unsigned)total);
    for (int c = 0; c < nc; c++) { ticket -= gadget_is_aa(g_gadgetDefs[cand[c]].name) && air == 2 ? g_gadgetDefs[cand[c]].weight * 3 : g_gadgetDefs[cand[c]].weight; if (ticket < 0) return g_gadgetAsset[cand[c]]; }
    return g_gadgetAsset[cand[nc - 1]];
}
