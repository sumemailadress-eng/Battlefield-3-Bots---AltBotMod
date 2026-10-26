// Reuse the captured kit messages; the loader owns a fixed-size LoadoutLib.
typedef struct BMWeaponDefinition {
    const char *role, *label, *weapon, *attachments[4];
} BMWeaponDefinition;
#include "weapon_catalog.inc"
#define BM_WEAPON_VARIANTS ((int)(sizeof g_weaponDefinitions / sizeof g_weaponDefinitions[0]))
typedef struct BMWeaponVariant {
    void *weapon, *parts[4];
    void **array;
    int ready, attached;
} BMWeaponVariant;
static BMWeaponVariant g_weaponVariants[BM_WEAPON_VARIANTS];
static void *g_stockAttachments[4];
static unsigned g_weaponVariantLife[MAX_BOTS];
static const BMWeaponProfile *g_primaryDoctrine[MAX_BOTS];

static void weapon_variety_load(void)
{
    memset(g_weaponVariants, 0, sizeof g_weaponVariants);
    memset(g_stockAttachments, 0, sizeof g_stockAttachments);
    if (!g_enhanced.weaponVariety) return;
    const char *stock[4] = {"Weapons/Common/NoOptics", "Weapons/Common/NoPrimaryAccessory",
                           "Weapons/Common/NoSecondaryAccessory", "Weapons/Common/NoCamo"};
    LoadoutLib *lib = loadout_lib();
    for (int i = 0; i < lib->count; i++) for (int w = 0; w < lib->l[i].nWeapons; w++) {
        void **a = NULL;
        memcpy(&a,lib->l[i].weaponMsg[w] + 0x40,sizeof a);
        for (unsigned k = 0, n = fb_count(a); k < n && k < 16; k++) for (int p = 0; p < 4; p++)
            if (!strcmp(asset_name(a[k]), stock[p])) g_stockAttachments[p] = a[k];
    }
    for (int p = 0; p < 4; p++) if (!g_stockAttachments[p] || IsBadReadPtr(g_stockAttachments[p], 4)) {
        bot_log("weapons: stock attachment template unavailable; keeping captured kits"); return;
    }
    Fixup fx[BM_WEAPON_VARIANTS * 5];
    int n = 0, ready = 0, attached = 0;
    memset(fx, 0, sizeof fx);
    for (int k = 0; k < BM_WEAPON_VARIANTS; k++) {
        BMWeaponVariant *v = &g_weaponVariants[k];
        const BMWeaponDefinition *def = &g_weaponDefinitions[k];
        for (int p = -1; p < 4; p++) {
            fx[n].slot = p < 0 ? &v->weapon : &v->parts[p];
            *fx[n].slot = (void *)-1;
            snprintf(fx[n].name, sizeof fx[n].name, "%s", p < 0 ? def->weapon : def->attachments[p]);
            unsigned cached=p<0?0:asset_cache_vt(def->attachments[p]);
            fx[n].vtable = p < 0 ? ADDR_WEAPON_UNLOCK_VTABLE : cached?cached:*(unsigned *)g_stockAttachments[p];
            n++;
        }
    }
    // Optional guns use only the typed discovery scan, never the slow name scan.
    fast_resolve_assets(fx, n);
    asset_cache_save(fx, n);
    for (int k = 0; k < BM_WEAPON_VARIANTS; k++) {
        BMWeaponVariant *v = &g_weaponVariants[k];
        if (!v->weapon || v->weapon == (void *)-1 || IsBadReadPtr(v->weapon, 4) ||
            *(unsigned *)v->weapon != ADDR_WEAPON_UNLOCK_VTABLE) continue;
        v->attached = 1;
        for (int p = 0; p < 4; p++)
            if (!v->parts[p] || v->parts[p] == (void *)-1 || IsBadReadPtr(v->parts[p], 4)) v->attached = 0;
        unsigned char *storage = malloc(4 + 4 * sizeof(void *));
        if (!storage) continue;
        unsigned count = 4;
        memcpy(storage, &count, 4);
        v->array = (void **)(storage + 4);
        memcpy(v->array, v->attached ? v->parts : g_stockAttachments, 4 * sizeof(void *));
        v->ready = 1; ready++; attached += v->attached;
    }
    bot_log("weapons: %d optional primaries, %d complete attachment sets (captured kits preserved)", ready, attached);
}

static int weapon_variant_pick(char *bot, const char *role)
{
    if (!g_enhanced.weaponVariety || !role) return -1;
    int candidates[BM_WEAPON_VARIANTS], nc = 0, nth = 0;
    for (int k = 0; k < BM_WEAPON_VARIANTS; k++)
        if (g_weaponVariants[k].ready && !strcmp(g_weaponDefinitions[k].role, role)) candidates[nc++] = k;
    if (!nc) return -1;
    for (int k = 0; k < H->botCount && H->bots[k] != bot; k++) {
        const char *name = H->bots[k] + OFF_PLAYER_ONLINE_NAME;
        if (player_team(H->bots[k]) == player_team(bot) &&
            (strstr(name, role) || (!strcmp(role, "Assault") && strstr(name, "Medic")))) nth++;
    }
    int bi = bot_index(bot);
    unsigned life = bi >= 0 ? g_weaponVariantLife[bi]++ : 0;
    return candidates[(nth + life) % (unsigned)nc];
}

#ifndef BM_PRIMARY_MESSAGE_OFFSET
#define BM_PRIMARY_MESSAGE_OFFSET 0x3c
#define BM_ATTACHMENT_MESSAGE_OFFSET 0x40
#endif
static void weapon_variant_message(unsigned char *msg, int variant)
{
    if (variant < 0 || variant >= BM_WEAPON_VARIANTS || !g_weaponVariants[variant].ready) return;
    BMWeaponVariant *v = &g_weaponVariants[variant];
    static unsigned char stockStorage[4 + 4 * sizeof(void *)];
    void **array = v->array;
    if (!g_enhanced.weaponAttachments) {
        unsigned count = 4;
        memcpy(stockStorage, &count, 4);
        memcpy(stockStorage + 4, g_stockAttachments, 4 * sizeof(void *));
        array = (void **)(stockStorage + 4);
    }
    memcpy(msg + BM_PRIMARY_MESSAGE_OFFSET, &v->weapon, sizeof v->weapon);
    memcpy(msg + BM_ATTACHMENT_MESSAGE_OFFSET, &array, sizeof array);
}

