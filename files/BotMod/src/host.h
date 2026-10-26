// Shared between the permanent loader (xinput9_1_0.dll) and the hot-reloadable core (BotModCore.dll).
// Anything that must survive a core reload lives in BotModHost. Bump BOTMOD_HOST_VERSION when the layout changes
// (a mismatched core refuses to start; restart the server in that case).
#pragma once

#define BOTMOD_HOST_VERSION 2
#define MAX_BOTS 64          // a 64-player server keeps 2 slots per team for people: 60 bots in practice

typedef struct BotTeamSpawn {
    void *entity;              // ServerCharacterSpawnEntity a human of this team last deployed from
    unsigned a2, a3, a4;       // the args it was called with
} BotTeamSpawn;

typedef struct BotModHost {
    int version;
    void (*log)(const char *fmt, ...);
    unsigned frame;            // incremented by the loader every ServerPlayerManager::update
    int gamePort;

    int botCount;
    void *bots[MAX_BOTS];      // ServerPlayer*
    void *botInput[MAX_BOTS];  // EntryInput* we own, attached at player+0x3c0
    float botMove[MAX_BOTS][3];// throttle, strafe, yaw

    BotTeamSpawn teamSpawn[3]; // indexed by team id 1/2
    void *teamBlueprint[3];    // SoldierBlueprint* seen on a human of that team

    int traceGhosts, traceInput, stepBots;
    unsigned char reserved[4096];   // per-bot blocks sized for MAX_BOTS start at +1024 (see core.c)
} BotModHost;

// Core exports (cdecl, undecorated).
typedef int (*BotModInit_t)(BotModHost *host);
typedef void (*BotModTick_t)(void);
typedef void (*BotModCommand_t)(char *line);
typedef void (*BotModShutdown_t)(void);
