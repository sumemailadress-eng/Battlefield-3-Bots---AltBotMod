// AltBotMod V34: squad orders.
//  - A player gives the bots in his squad an objective: chat "!attack B" / "!defend A" ("!attack" / "!defend" alone: the
//    objective he is looking at), or the commo rose's Attack order on an objective (radio order 17, the objective he looks
//    at). The squad's bots take it ahead of the team's own plan for 4 minutes, until it is done (taken / lost), or
//    "!orders off". In Rush, A / B are the two M-COMs of the current stage.
//  - A bot squad leader announces its squad's objective in the squad's chat when it changes ("[Squad 2] xFoo: attack B"),
//    so a player in a bot-led squad knows the plan.
#define SQUAD_ORDER_FRAMES   (240 * 30)
#define SQUAD_ANNOUNCE_GAP   (40 * 30)
typedef struct SquadOrder { int flag, defend, rush; unsigned until; char by[32]; } SquadOrder;
static SquadOrder g_squadOrder[3][33];
static int g_squadAnnounced[3][33]; static unsigned g_squadAnnouncedAt[3][33];
static unsigned g_squadOrdersGiven;

// The letter players see for flag k: the Fun-Bots mesh names its capture zones a, b, c ...; else A + k.
static char flag_letter(int k)
{
    if (k < 0) return '?';
    if (g_zone.np && k < g_flagCount) {
        int zi = zn_zone_near(&g_zone, flag_pos(k), ZN_KIND_CAPTURE, 40.0f);
        if (zi >= 0 && g_zone.zones[zi].name[0] >= 'a' && g_zone.zones[zi].name[0] <= 'z' && !g_zone.zones[zi].name[1])
            return (char)(g_zone.zones[zi].name[0] - 'a' + 'A');
    }
    return (char)('A' + k);
}
static int flag_by_letter(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c < 'A' || c > 'Z') return -1;
    for (int k = 0; k < g_flagCount; k++) if (flag_letter(k) == c) return k;
    return -1;
}
// The objective a player looks at: the smallest angle off his facing within 1.5 km (25 deg at most), else -1.
static int flag_looked_at(char *pl)
{
    char *s = pl ? *(char **)(pl + OFF_PLAYER_SOLDIER) : NULL;
    if (!s || !g_flagCount) return -1;
    float *p = (float *)(s + OFF_SOLDIER_SPAWN_POSITION), yaw = *(float *)(s + OFF_SOLDIER_YAW), fx = sinf(yaw), fz = cosf(yaw);
    int best = -1; float bestA = 0.44f;
    for (int k = 0; k < g_flagCount; k++) {
        float *q = flag_pos(k), dx = q[0] - p[0], dz = q[2] - p[2], d = sqrtf(dx * dx + dz * dz);
        if (d > 1500.0f) continue;
        if (d < 25.0f) return k;   // standing on it
        float a = acosf(fmaxf(-1.0f, fminf(1.0f, (dx * fx + dz * fz) / d)));
        if (a < bestA) { bestA = a; best = k; }
    }
    return best;
}
static void chat_say_squad(int team, int squad, const char *fmt, ...)
{
    char msg[128]; va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    for (char *t = msg; *t; t++) if (*t == '\t') *t = ' ';
    chat_rcon("admin.say\t%.120s\tsquad\t%d\t%d", msg, team, squad);
}
// The order for this squad, if one is running: flag (-1 none), *defend.
static int squad_order_flag(int team, int squad, int *defend)
{
    if (team < 1 || team > 2 || squad < 1 || squad > 31) return -1;
    SquadOrder *o = &g_squadOrder[team][squad];
    if (!o->until || H->frame > o->until || o->rush) return -1;
    if (o->flag < 0 || o->flag >= g_flagCount) { o->until = 0; return -1; }
    int owned = flag_owner(o->flag) == team;
    if (!o->defend && owned) {   // taken: done
        chat_say_squad(team, squad, "[AltBotMod] Squad: %c is ours - back to the team's plan", flag_letter(o->flag));
        o->until = 0; return -1;
    }
    if (defend) *defend = o->defend && owned;
    return o->flag;
}
// Rush: the M-COM (1..16) the squad was told to go for, 0 none.
static int squad_order_rush(int i)
{
    int team = player_team(H->bots[i]), squad = player_squad(H->bots[i]);
    if (g_mode != BM_MODE_RUSH || team < 1 || team > 2 || squad < 1 || squad > 31 || !g_modeProfile) return 0;
    SquadOrder *o = &g_squadOrder[team][squad];
    if (!o->until || H->frame > o->until || !o->rush) return 0;
    int k = (g_rushStage - 1) * 2 + 1 + o->flag;
    if (k < 1 || k > 16 || !g_modeProfile->mcom[k].present || !bm_rush_active(&g_rush, k)) { o->until = 0; return 0; }
    return k;
}
static void mode_reassign_idle(int i, int objective);
// Give the order (from chat or the radio). kind: 1 attack, 2 defend, 0 attack an enemy flag / defend an own one.
static void squad_order_give(char *pl, int flag, int kind, const char *how)
{
    int team = player_team(pl), squad = player_squad(pl);
    const char *who = (char *)pl + OFF_PLAYER_ONLINE_NAME;
    if (team < 1 || team > 2) return;
    if (squad < 1 || squad > 31) { chat_say("[AltBotMod] %s: join a squad first - squad orders go to the bots in your squad", who); return; }
    int bots = 0;
    for (int i = 0; i < H->botCount; i++) if (player_team(H->bots[i]) == team && player_squad(H->bots[i]) == squad) bots++;
    if (!bots) { chat_say_squad(team, squad, "[AltBotMod] %s: there are no bots in your squad", who); return; }
    SquadOrder *o = &g_squadOrder[team][squad];
    memset(o, 0, sizeof *o);
    snprintf(o->by, sizeof o->by, "%s", who);
    o->until = H->frame + SQUAD_ORDER_FRAMES;
    if (g_mode == BM_MODE_RUSH) {
        o->rush = 1; o->flag = flag; o->defend = team == 2;
        int k = (g_rushStage - 1) * 2 + 1 + flag;
        if (flag < 0 || flag > 1 || k > 16 || !g_modeProfile || !g_modeProfile->mcom[k].present || !bm_rush_active(&g_rush, k)) {
            o->until = 0; chat_say_squad(team, squad, "[AltBotMod] %s: that M-COM is not in play", who); return;
        }
        for (int i = 0; i < H->botCount; i++)
            if (player_team(H->bots[i]) == team && player_squad(H->bots[i]) == squad && *(char **)(H->bots[i] + OFF_PLAYER_SOLDIER)) {
                mode_reassign_idle(i, flag); bot_say_later(i, ORDER_ROGERTHAT, SAY_DELAY_FRAMES + (unsigned)i % 20);
            }
        g_squadOrdersGiven++;
        chat_say_squad(team, squad, "[AltBotMod] Squad: %s M-COM %c (%s)", team == 1 ? "attack" : "defend", 'A' + flag, who);
        bot_log("squad: %s orders squad %d (team %d) to %s M-COM %c (%s; %d bot(s))", who, squad, team, team == 1 ? "attack" : "defend", 'A' + flag, how, bots);
        return;
    }
    if (flag < 0 || flag >= g_flagCount) { o->until = 0; chat_say_squad(team, squad, "[AltBotMod] %s: no such objective", who); return; }
    int owned = flag_owner(flag) == team;
    o->flag = flag; o->defend = kind == 2 || (kind == 0 && owned);
    if (o->defend && !owned) o->defend = 0;   // (an enemy flag is attacked first, then held)
    for (int i = 0; i < H->botCount; i++) {
        if (player_team(H->bots[i]) != team || player_squad(H->bots[i]) != squad) continue;
        BotBrain *b = bot_brain(i);
        if (b->task == TASK_NONE || b->task == TASK_DEFEND) { b->targetFlag = -1; b->enroute = b->waiting = 0; clear_local_objective(i); }
        if (*(char **)(H->bots[i] + OFF_PLAYER_SOLDIER)) bot_say_later(i, ORDER_ROGERTHAT, SAY_DELAY_FRAMES + (unsigned)i % 20);
    }
    g_squadOrdersGiven++;
    chat_say_squad(team, squad, "[AltBotMod] Squad: %s %c (%s)", o->defend ? "defend" : "attack", flag_letter(flag), who);
    bot_log("squad: %s orders squad %d (team %d) to %s flag %d (%c; %s; %d bot(s))", who, squad, team, o->defend ? "defend" : "attack", flag, flag_letter(flag), how, bots);
}
static void squad_orders_cancel(char *pl)
{
    int team = player_team(pl), squad = player_squad(pl);
    if (team < 1 || team > 2 || squad < 1 || squad > 31) return;
    g_squadOrder[team][squad].until = 0;
    chat_say_squad(team, squad, "[AltBotMod] Squad: orders off - the bots follow the team's plan (%s)", (char *)pl + OFF_PLAYER_ONLINE_NAME);
}
// chat: !attack [A..] / !defend [A..] / !orders [off]
static int squad_order_chat(const char *who, const char *cmd, const char *arg)
{
    int attack = !_stricmp(cmd, "attack"), defend = !_stricmp(cmd, "defend");
    if (!attack && !defend && _stricmp(cmd, "orders")) return 0;
    char *pl = (char *)find_player(who);
    if (!pl || is_bot(pl)) return 1;
    if (!attack && !defend) {
        if (!_stricmp(arg, "off")) { squad_orders_cancel(pl); return 1; }
        int team = player_team(pl), squad = player_squad(pl);
        SquadOrder *o = team >= 1 && team <= 2 && squad >= 1 && squad <= 31 ? &g_squadOrder[team][squad] : NULL;
        if (o && o->until && H->frame < o->until)
            chat_say_squad(team, squad, "[AltBotMod] Squad order: %s %c, %.0f s left (by %s) - !orders off to cancel", o->defend ? "defend" : "attack",
                           o->rush ? 'A' + o->flag : flag_letter(o->flag), (o->until - H->frame) / 30.0f, o->by);
        else chat_say("[AltBotMod] %s: no squad order - !attack B / !defend A, or look at an objective and type !attack", who);
        return 1;
    }
    int flag = -1;
    if (arg[0] && !arg[1]) flag = g_mode == BM_MODE_RUSH ? ((arg[0] | 32) == 'a' ? 0 : (arg[0] | 32) == 'b' ? 1 : -1) : flag_by_letter(arg[0]);
    else if (!arg[0]) {
        int k = flag_looked_at(pl);
        if (g_mode == BM_MODE_RUSH && g_modeProfile) {   // the M-COM looked at
            char *s = *(char **)(pl + OFF_PLAYER_SOLDIER);
            if (s) {
                float *p = (float *)(s + OFF_SOLDIER_SPAWN_POSITION), yaw = *(float *)(s + OFF_SOLDIER_YAW), fx = sinf(yaw), fz = cosf(yaw), bestA = 0.6f;
                for (int o = 0; o < 2; o++) {
                    int m = (g_rushStage - 1) * 2 + 1 + o; if (m > 16 || !g_modeProfile->mcom[m].present) continue;
                    const float *q = g_modeProfile->mcom[m].pos; float dx = q[0] - p[0], dz = q[2] - p[2], d = sqrtf(dx * dx + dz * dz);
                    float a = d < 20 ? 0 : acosf(fmaxf(-1.0f, fminf(1.0f, (dx * fx + dz * fz) / d)));
                    if (a < bestA) { bestA = a; k = o; }
                }
            }
        }
        flag = k;
    }
    if (flag < 0) { chat_say("[AltBotMod] %s: which objective? !%s B - or look at it and type !%s", who, cmd, cmd); return 1; }
    squad_order_give(pl, flag, attack ? 1 : 2, "chat");
    return 1;
}
// Radio order 17 (Attack, from the commo rose): the objective the player looks at.
static void squad_order_radio(char *pl)
{
    int k = flag_looked_at(pl);
    if (g_mode == BM_MODE_RUSH) { if (k > 1) k = -1; }
    if (k >= 0) squad_order_give(pl, k, 0, "commo rose");
}
// A bot squad leader's announcement: its squad's objective changed (a player in the squad sees it in chat).
static void squad_announce_tick(void)
{
    if (H->frame % 45) return;
    for (int t = 1; t <= 2; t++)
        for (int q = 1; q <= 31; q++) {
            int f = g_squadTarget[t][q] - 1;
            if (f < 0 || f >= g_flagCount || g_squadOrder[t][q].until > H->frame) continue;
            if (g_squadAnnounced[t][q] == f + 1 || (g_squadAnnouncedAt[t][q] && H->frame - g_squadAnnouncedAt[t][q] < SQUAD_ANNOUNCE_GAP)) continue;
            char *leader = NULL, *human = NULL;
            char *pm = player_manager(); void **arr = pm ? *(void ***)(pm + 0xac) : NULL; unsigned count = pm ? *(unsigned *)(pm + 8) : 0;
            for (unsigned k = 0; arr && k < count && k < 128; k++) {
                char *pl = arr[k];
                if (!pl || player_team(pl) != t || player_squad(pl) != q) continue;
                if (is_bot(pl) && pl[0x788]) leader = pl;   // (0x788: squad leader)
                if (!is_bot(pl)) human = pl;
            }
            if (!leader || !human) continue;   // (nobody to tell)
            g_squadAnnounced[t][q] = f + 1; g_squadAnnouncedAt[t][q] = H->frame ? H->frame : 1;
            chat_say_squad(t, q, "[Squad] %s: %s %c", leader + OFF_PLAYER_ONLINE_NAME, g_squadDefend[t][q] && flag_owner(f) == t ? "defend" : "attack", flag_letter(f));
            int li = bot_index(leader); if (li >= 0) bot_say_later(li, ORDER_GOGOGO, 0);
        }
}
