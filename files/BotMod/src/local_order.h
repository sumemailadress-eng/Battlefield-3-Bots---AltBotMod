// Caspian Test16: context of a bot's active local/recovery order.
// Included after g_localFlag/g_localUntil, player_team() and flag_owner().
// An order ends on expiry, on capture of the flag it attacks, on a new life
// or on a team change; while it is active the objective director leaves the
// bot alone (see director_native.inc).
#pragma once
typedef struct LocalOrder { unsigned setAt; int team, attack; } LocalOrder;
static LocalOrder g_localOrder[MAX_BOTS];
static inline void local_order_note(int i, int flag)
{
    g_localOrder[i].setAt = H->frame ? H->frame : 1;
    g_localOrder[i].team = player_team(H->bots[i]);
    g_localOrder[i].attack = flag >= 0 && flag_owner(flag) != g_localOrder[i].team;
}
static inline int local_order_active(int i)
{
    int f = g_localFlag[i] - 1;
    return f >= 0 && f < g_flagCount && H->frame <= g_localUntil[i] &&
           g_localOrder[i].team == player_team(H->bots[i]);
}
