// RCON chat bridge: a worker thread keeps a Frostbite RCON connection to this server, enables events, collects
// player.onChat lines starting with '!' and sends queued admin.say messages.
#include <winsock2.h>
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "chat.h"
#include "modes.h"

#define QUEUE_LEN  256
#define LINE_LEN   256

typedef struct ChatLine { char who[64]; char text[LINE_LEN]; } ChatLine;

static ChatLogFn g_log;
static CRITICAL_SECTION g_lock;
static ChatLine g_in[QUEUE_LEN];
static int g_inHead, g_inTail;
static char g_out[QUEUE_LEN][LINE_LEN];
static int g_outHead, g_outTail;
static HANDLE g_thread;
static volatile LONG g_stop;
static SOCKET g_sock = INVALID_SOCKET;
static char g_host[64] = "127.0.0.1", g_password[128];
static int g_port = 47900;
static unsigned g_seq;

static void read_command_line(void)
{
    const char *cl = GetCommandLineA(), *p;
    if ((p = strstr(cl, "-RemoteAdminPort ")) != NULL) {
        char addr[96] = "";
        sscanf(p + 17, "%95s", addr);
        char *colon = strrchr(addr, ':');
        if (colon) { *colon = 0; g_port = atoi(colon + 1); if (addr[0]) snprintf(g_host, sizeof g_host, "%s", addr); }
        else g_port = atoi(addr);
    }
    if ((p = strstr(cl, "+admin.password ")) != NULL) {
        p += 16;
        if (*p == '"') { p++; const char *e = strchr(p, '"'); snprintf(g_password, sizeof g_password, "%.*s", e ? (int)(e - p) : 127, p); }
        else sscanf(p, "%127s", g_password);
    }
}

static int send_words(unsigned seq, int n, const char **words)
{
    char buf[1024];
    unsigned size = 12;
    for (int i = 0; i < n; i++) size += 4 + (unsigned)strlen(words[i]) + 1;
    if (size > sizeof buf) return 0;
    memcpy(buf, &seq, 4);
    memcpy(buf + 4, &size, 4);
    memcpy(buf + 8, &n, 4);
    unsigned o = 12;
    for (int i = 0; i < n; i++) {
        unsigned len = (unsigned)strlen(words[i]);
        memcpy(buf + o, &len, 4);
        memcpy(buf + o + 4, words[i], len + 1);
        o += 4 + len + 1;
    }
    return send(g_sock, buf, (int)size, 0) == (int)size;
}

static int recv_all(char *buf, int n)
{
    int got = 0;
    while (got < n && !g_stop) {
        int r = recv(g_sock, buf + got, n - got, 0);
        if (r == SOCKET_ERROR && WSAGetLastError() == WSAETIMEDOUT) { if (got == 0) return -1; continue; }
        if (r <= 0) return 0;
        got += r;
    }
    return !g_stop;
}

// Reads one packet: 1 ok, -1 timeout before any byte, 0 connection lost. words point into buf.
static int recv_packet(unsigned *seq, int *nw, char **words, int maxWords, char *buf, int bufLen)
{
    unsigned hdr[3];
    int r = recv_all((char *)hdr, 12);
    if (r <= 0) return r;
    unsigned size = hdr[1];
    if (size < 12 || size - 12 > (unsigned)bufLen) return 0;
    if (recv_all(buf, (int)(size - 12)) <= 0) return 0;
    *seq = hdr[0];
    *nw = 0;
    unsigned o = 0;
    for (unsigned i = 0; i < hdr[2] && o + 4 <= size - 12; i++) {
        unsigned len;
        memcpy(&len, buf + o, 4);
        if (o + 4 + len + 1 > size - 12) break;
        if (*nw < maxWords) words[(*nw)++] = buf + o + 4;
        buf[o + 4 + len] = 0;
        o += 4 + len + 1;
    }
    return 1;
}

static int request(int n, const char **words)
{
    unsigned seq = ++g_seq & 0x3fffffff;
    return send_words(seq, n, words);
}

static int connect_rcon(void)
{
    g_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_sock == INVALID_SOCKET) return 0;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)g_port);
    a.sin_addr.s_addr = inet_addr(g_host);
    if (connect(g_sock, (struct sockaddr *)&a, sizeof a) != 0) { closesocket(g_sock); g_sock = INVALID_SOCKET; return 0; }
    DWORD tmo = 200;
    setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof tmo);
    const char *login[2] = {"login.plainText", g_password};
    const char *events[2] = {"admin.eventsEnabled", "true"};
    return request(2, login) && request(2, events);
}

// An event from the server (sent unasked): acknowledge it, and queue "!" chat lines for the game thread.
static volatile LONG g_levelLoads,g_roundsOver;
static char g_serverMap[64],g_serverMode[64];
static struct {char name[40],weapon[64];}g_deaths[QUEUE_LEN];static int g_deathHead,g_deathTail;
void chat_server_state(char *map,int mapLen,char *mode,int modeLen){EnterCriticalSection(&g_lock);snprintf(map,mapLen,"%s",g_serverMap);snprintf(mode,modeLen,"%s",g_serverMode);LeaveCriticalSection(&g_lock);}
// AltBotMod V13 (live map): every kill, with the killer and head shot, in its own queue so the round code's
// death queue above is unchanged. Full: new kills are dropped (the live map drains it every frame).
static struct {char killer[40],victim[40],weapon[64];int headshot;}g_kills[QUEUE_LEN];static int g_killHead,g_killTail;
int chat_poll_kill(char *killer,int killerLen,char *victim,int victimLen,char *weapon,int weaponLen,int *headshot){EnterCriticalSection(&g_lock);int have=g_killHead!=g_killTail;if(have){snprintf(killer,killerLen,"%s",g_kills[g_killTail].killer);snprintf(victim,victimLen,"%s",g_kills[g_killTail].victim);snprintf(weapon,weaponLen,"%s",g_kills[g_killTail].weapon);*headshot=g_kills[g_killTail].headshot;g_killTail=(g_killTail+1)%QUEUE_LEN;}LeaveCriticalSection(&g_lock);return have;}
int chat_poll_death(char *name,int nameLen,char *weapon,int weaponLen){EnterCriticalSection(&g_lock);int have=g_deathHead!=g_deathTail;if(have){snprintf(name,nameLen,"%s",g_deaths[g_deathTail].name);snprintf(weapon,weaponLen,"%s",g_deaths[g_deathTail].weapon);g_deathTail=(g_deathTail+1)%QUEUE_LEN;}LeaveCriticalSection(&g_lock);return have;}

void chat_level_events(long *loads, long *roundsOver) { *loads = g_levelLoads; *roundsOver = g_roundsOver; }

static void handle_event(unsigned seq, int nw, char **w)
{
    const char *ok[1] = {"OK"};
    send_words(seq | 0x40000000 | 0x80000000, 1, ok);
    if(nw>=3&&!strcmp(w[0],"server.onLevelLoaded")){EnterCriticalSection(&g_lock);snprintf(g_serverMap,sizeof g_serverMap,"%s",w[1]);snprintf(g_serverMode,sizeof g_serverMode,"%s",w[2]);LeaveCriticalSection(&g_lock);InterlockedIncrement(&g_levelLoads);}
    if(nw>=4&&!strcmp(w[0],"player.onKill")){EnterCriticalSection(&g_lock);int next=(g_deathHead+1)%QUEUE_LEN;if(next!=g_deathTail){snprintf(g_deaths[g_deathHead].name,sizeof g_deaths[0].name,"%s",w[2]);snprintf(g_deaths[g_deathHead].weapon,sizeof g_deaths[0].weapon,"%s",w[3]);g_deathHead=next;}
        next=(g_killHead+1)%QUEUE_LEN;if(next!=g_killTail){snprintf(g_kills[g_killHead].killer,sizeof g_kills[0].killer,"%s",w[1]);snprintf(g_kills[g_killHead].victim,sizeof g_kills[0].victim,"%s",w[2]);
            snprintf(g_kills[g_killHead].weapon,sizeof g_kills[0].weapon,"%s",w[3]);g_kills[g_killHead].headshot=nw>=5&&!_stricmp(w[4],"true");g_killHead=next;}
        LeaveCriticalSection(&g_lock);}

    if (nw >= 1 && !strcmp(w[0], "server.onRoundOver")) InterlockedIncrement(&g_roundsOver);
    if (nw >= 3 && !strcmp(w[0], "player.onChat") && w[2][0] == '!' && strcmp(w[1], "Server")) {
        EnterCriticalSection(&g_lock);
        int next = (g_inHead + 1) % QUEUE_LEN;
        if (next != g_inTail) {
            snprintf(g_in[g_inHead].who, sizeof g_in[g_inHead].who, "%s", w[1]);
            snprintf(g_in[g_inHead].text, sizeof g_in[g_inHead].text, "%s", w[2]);
            g_inHead = next;
        }
        LeaveCriticalSection(&g_lock);
    }
}

// Sends a request and waits (up to ~3 s) for its response; events arriving meanwhile are handled.
// Returns the number of response words (pointing into buf), 0 on failure.
static int rcon_call(int n, const char **words, char **out, int maxOut, char *buf, int bufLen)
{
    unsigned seq = ++g_seq & 0x3fffffff;
    if (!send_words(seq, n, words)) return 0;
    for (int waited = 0; waited < 15; ) {
        unsigned rs;
        int nw;
        int r = recv_packet(&rs, &nw, out, maxOut, buf, bufLen);
        if (r == -1) { waited++; continue; }
        if (r == 0) return 0;
        if (!(rs & 0x40000000)) { handle_event(rs, nw, out); continue; }
        if ((rs & 0x3fffffff) == seq) return nw;
    }
    return 0;
}

// !map: find (or add) the map in the rotation with the current game mode, make it next and start it.
static void do_map(const char *code,const char *who,const char *requested)
{
    static char buf[65536];char *w[1024];const char *mode=bm_map_mode(bm_map(code),requested);
    if(!mode){chat_say("[AltBotMod] unsupported map/mode combination");return;}
    int n;
    const char *list[1] = {"mapList.list"};
    n = rcon_call(1, list, w, 1024, buf, sizeof buf);
    int idx = -1, count = 0;
    if (n >= 3 && !strcmp(w[0], "OK")) {
        count = atoi(w[1]);
        int per = atoi(w[2]);
        if(per<3||count<0||count>300){chat_say("[AltBotMod] invalid map rotation reply");return;}
        for (int k = 0; k < count && 3 + k * per + 1 < n; k++)
            if (!_stricmp(w[3 + k * per], code) && (!mode[0] || !_stricmp(w[3 + k * per + 1], mode))) { idx = k; break; }
    }
    char say[200];
    if (idx < 0) {
        if (!mode[0]) { chat_say("[AltBotMod] %s: cannot read the server's game mode", who); return; }
        const char *add[4] = {"mapList.add", code, mode, "2"};
        n = rcon_call(4, add, w, 1024, buf, sizeof buf);
        if (n < 1 || strcmp(w[0], "OK")) { chat_say("[AltBotMod] %s: the server refused map %s (%s)", who, code, n ? w[0] : "no answer"); return; }
        idx = count;
    }
    char idxs[16];
    snprintf(idxs, sizeof idxs, "%d", idx);
    const char *next[2] = {"mapList.setNextMapIndex", idxs};
    n = rcon_call(2, next, w, 1024, buf, sizeof buf);
    if (n < 1 || strcmp(w[0], "OK")) { chat_say("[AltBotMod] %s: could not select %s (%s)", who, code, n ? w[0] : "no answer"); return; }
    snprintf(say, sizeof say, "[AltBotMod] %s is changing the map to %s (%s)", who, code, mode);
    const char *s[3] = {"admin.say", say, "all"};
    rcon_call(3, s, w, 1024, buf, sizeof buf);
    Sleep(3000);   // let players read it: the map change clears the chat box (this thread only; the game keeps running)
    const char *run[1] = {"mapList.runNextRound"};
    rcon_call(1, run, w, 1024, buf, sizeof buf);
    g_log("chat: %s changed the map to %s (rotation index %d, %s)", who, code, idx, mode);
}

static volatile int g_maxPlayers;
int chat_max_players(void) { return g_maxPlayers; }
// serverInfo: OK, name, players, maxPlayers, ... - asked now and then so admin changes to vars.maxPlayers are picked up
static void poll_server_info(void)
{
    static DWORD lastTick;
    DWORD now = GetTickCount();
    if (g_maxPlayers && now - lastTick < 5000) return;
    lastTick = now;
    static char buf[4096];
    char *w[16];
    const char *info[1] = {"serverInfo"};
    int n = rcon_call(1, info, w, 16, buf, sizeof buf);
    if (n >= 4 && !strcmp(w[0], "OK")) {
        if(n>=6){EnterCriticalSection(&g_lock);snprintf(g_serverMode,sizeof g_serverMode,"%s",w[4]);snprintf(g_serverMap,sizeof g_serverMap,"%s",w[5]);LeaveCriticalSection(&g_lock);}
        int m = atoi(w[3]);
        // AltBotMod V16.2.1: serverInfo's player limit is the level's (64 on RushLarge0) even when vars.maxPlayers is
        // lower. With vars.maxPlayers 32 the bots filled 60 places; the server then kept telling the Blaze backend
        // "capacity 32" with 60 players in, the BF-Portable emulator threw on it (setPlayerCapacity, index out of range),
        // the backend link timed out after ~2 min, the server dropped every client and then crashed (2026-10-15 09:51 /
        // 09:54). The bots now go by the smaller of the two.
        const char *mp[1] = {"vars.maxPlayers"};
        static char buf2[512]; char *w2[4];
        int n2 = rcon_call(1, mp, w2, 4, buf2, sizeof buf2);
        int v = n2 >= 2 && !strcmp(w2[0], "OK") ? atoi(w2[1]) : 0;
        if (v > 0 && (m <= 0 || v < m)) m = v;
        if (m > 0 && m != g_maxPlayers) { g_log("chat: server allows %d players (serverInfo %s, vars.maxPlayers %d)", m, w[3], v); g_maxPlayers = m; }
    }
}

static DWORD WINAPI chat_worker(LPVOID unused)
{
    (void)unused;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    static char buf[16384];
    int warned = 0;
    while (!g_stop) {
        if (g_sock == INVALID_SOCKET) {
            if (!connect_rcon()) {
                if (!warned++) g_log("chat: cannot reach RCON at %s:%d yet - retrying", g_host, g_port);
                for (int t = 0; t < 30 && !g_stop; t++) Sleep(100);
                continue;
            }
            g_log("chat: connected to RCON %s:%d", g_host, g_port);
            warned = 0;
        }
        poll_server_info();
        // outgoing messages
        for (;;) {
            char msg[LINE_LEN];
            EnterCriticalSection(&g_lock);
            int have = g_outHead != g_outTail;
            if (have) { memcpy(msg, g_out[g_outTail], LINE_LEN); g_outTail = (g_outTail + 1) % QUEUE_LEN; }
            LeaveCriticalSection(&g_lock);
            if (!have) break;
            if (msg[0] == '\x01') {   // "\x01map\t<code>\t<who>": needs replies, done here
                char *code = strchr(msg, '\t'), *who = code ? strchr(code + 1, '\t') : NULL;
                if(code&&who){char *mode=strchr(who+1,'\t');*who=0;if(mode){*mode=0;do_map(code+1,who+1,mode+1);}}
                continue;
            }
            const char *words[12];
            int n = 0;
            for (char *s = msg; s && n < 12; ) {   // queued commands are tab-separated words
                words[n++] = s;
                char *tab = strchr(s, '\t');
                if (tab) { *tab = 0; s = tab + 1; } else s = NULL;
            }
            request(n, words);
        }
        // incoming packets (responses and events)
        unsigned seq;
        int nw;
        char *w[8];
        int r = recv_packet(&seq, &nw, w, 8, buf, sizeof buf);
        if (r == -1) continue;
        if (r == 0) {
            if (!g_stop) g_log("chat: RCON connection lost - reconnecting");
            closesocket(g_sock);
            g_sock = INVALID_SOCKET;
            continue;
        }
        if (seq & 0x40000000) continue;   // response to one of our requests
        handle_event(seq, nw, w);
    }
    if (g_sock != INVALID_SOCKET) { closesocket(g_sock); g_sock = INVALID_SOCKET; }
    WSACleanup();
    return 0;
}

void chat_start(ChatLogFn log)
{
    g_log = log;
    InitializeCriticalSection(&g_lock);
    read_command_line();
    if (!g_password[0]) { g_log("chat: no +admin.password on the server command line - chat disabled"); return; }
    g_stop = 0;
    g_thread = CreateThread(NULL, 0, chat_worker, NULL, 0, NULL);
}

void chat_stop(void)
{
    if (!g_thread) return;
    g_stop = 1;
    WaitForSingleObject(g_thread, 5000);
    CloseHandle(g_thread);
    g_thread = NULL;
    DeleteCriticalSection(&g_lock);
}

int chat_poll(char *who, int whoLen, char *text, int textLen)
{
    if (!g_thread) return 0;
    int have = 0;
    EnterCriticalSection(&g_lock);
    if (g_inHead != g_inTail) {
        snprintf(who, whoLen, "%s", g_in[g_inTail].who);
        snprintf(text, textLen, "%s", g_in[g_inTail].text);
        g_inTail = (g_inTail + 1) % QUEUE_LEN;
        have = 1;
    }
    LeaveCriticalSection(&g_lock);
    return have;
}

void chat_say(const char *fmt, ...)
{
    if (!g_thread) return;
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    for (char *t = msg; *t; t++) if (*t == '\t') *t = ' ';
    EnterCriticalSection(&g_lock);
    for (const char *p = msg; *p; ) {   // chat lines are limited to 128 characters
        int next = (g_outHead + 1) % QUEUE_LEN;
        if (next == g_outTail) break;
        snprintf(g_out[g_outHead], LINE_LEN, "admin.say\t%.120s\tall", p);
        g_outHead = next;
        p += strlen(p) > 120 ? 120 : strlen(p);
    }
    LeaveCriticalSection(&g_lock);
}

void chat_map(const char *code,const char *who,const char *mode)
{
    chat_rcon("\x01map\t%s\t%s\t%s",code,who,mode);
}

void chat_rcon(const char *fmt, ...)
{
    if (!g_thread) return;
    char cmd[LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof cmd, fmt, ap);
    va_end(ap);
    EnterCriticalSection(&g_lock);
    int next = (g_outHead + 1) % QUEUE_LEN;
    if (next != g_outTail) { memcpy(g_out[g_outHead], cmd, LINE_LEN); g_outHead = next; }
    LeaveCriticalSection(&g_lock);
}
