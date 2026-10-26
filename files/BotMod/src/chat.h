// In-game text chat for BotMod through the server's own RCON port: reads player chat (for !commands) and posts
// admin messages. Runs on a background thread; the game thread only polls / queues.
#pragma once

typedef void (*ChatLogFn)(const char *fmt, ...);

// Reads the RCON address and password from the server's command line (-RemoteAdminPort, +admin.password).
void chat_start(ChatLogFn log);
void chat_stop(void);
// Next chat line that starts with '!' (game thread). Returns 0 when none is waiting.
int chat_poll(char *who, int whoLen, char *text, int textLen);
// Queues a message for everyone (split to the 128-character chat limit).
void chat_say(const char *fmt, ...);
// Queues any RCON command; words separated by tabs, e.g. "admin.movePlayer\t<name>\t1\t2\tfalse".
void chat_rcon(const char *fmt, ...);
// The server's current player limit (vars.maxPlayers via RCON serverInfo, refreshed every 30 s); 0 = not known yet.
int chat_max_players(void);
// How many server.onLevelLoaded / server.onRoundOver events RCON has delivered so far (level reloads of the same map
// are invisible otherwise).
void chat_level_events(long *loads, long *roundsOver);
// Switches the server to map `code` (e.g. MP_017) right away, via the rotation; announces it in chat.
void chat_map(const char *code,const char *who,const char *mode);
void chat_server_state(char *map,int mapLen,char *mode,int modeLen);
int chat_poll_death(char *name,int nameLen,char *weapon,int weaponLen);
// AltBotMod V13: next player.onKill (killer may be empty: the world / a fall). Returns 0 when none is waiting.
int chat_poll_kill(char *killer,int killerLen,char *victim,int victimLen,char *weapon,int weaponLen,int *headshot);
