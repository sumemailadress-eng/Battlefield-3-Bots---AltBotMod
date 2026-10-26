#pragma once
// Independent clocks: task changes, route retries and reversing cannot claim
// physical progress. These are module-owned, not part of the host brain ABI.
typedef struct ActivityState {
    char *soldier,*vehicle;
    float position[3],travelAnchor[3],escape[3];
    unsigned progressAt,travelAt,actionAt,absentAt,escapeUntil;
    unsigned replanAt; // Caspian Test16: last objective re-plan in this stall
    int candidate,attempts;
    float trail[12][2]; int trailN; unsigned circleAt; int circles;   // AltBotMod V18: the last 12 s, one sample a second (circling)
} ActivityState;
static ActivityState g_activity[MAX_BOTS];
static unsigned g_activityProbeFrame,g_activityRetireAt;
static int activity_grid_tick(int i,char *soldier);
