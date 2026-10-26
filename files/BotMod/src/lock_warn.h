// AltBotMod V34: locks ON vehicles (see on_lock_update in core.c): the vehicle a seeker is on, how far the lock is.
// A header so the air / passenger modules (and their offline tests) have it whichever is compiled first.
#pragma once
#define INLOCK_SLOTS 24
static struct { char *veh; unsigned at; float progress; } g_inLock[INLOCK_SLOTS];
__attribute__((unused)) static unsigned g_lockWarnFlares, g_inLockSeen;
__attribute__((unused)) static void incoming_lock_note(char *veh, float progress)
{
    int slot = -1;
    for (int k = 0; k < INLOCK_SLOTS; k++) {
        if (g_inLock[k].veh == veh) { slot = k; break; }
        if (slot < 0 && (!g_inLock[k].veh || H->frame - g_inLock[k].at > 30)) slot = k;
    }
    if (slot < 0) return;
    if (g_inLock[slot].veh != veh || H->frame - g_inLock[slot].at > 30) { g_inLock[slot].progress = 0; g_inLockSeen++; }
    g_inLock[slot].veh = veh; g_inLock[slot].at = H->frame ? H->frame : 1;
    if (progress > g_inLock[slot].progress || H->frame - g_inLock[slot].at > 2) g_inLock[slot].progress = progress;
}
__attribute__((unused)) static float incoming_lock(char *veh)
{
    if (!veh) return 0;
    for (int k = 0; k < INLOCK_SLOTS; k++) if (g_inLock[k].veh == veh && H->frame - g_inLock[k].at <= 15) return g_inLock[k].progress;
    return 0;
}
