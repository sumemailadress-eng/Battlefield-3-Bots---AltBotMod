// BF3 BotMod loader - loaded by bf3_server_Final.exe as a proxy xinput9_1_0.dll (real exports forwarded to
// xinput9_1_0_orig.dll, see loader.def). Owns the per-frame hook, the command file and persistent bot state, and
// hot-loads the bot logic from BotModCore.dll so it can be rebuilt without restarting the server ("reload").
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "host.h"

#define ADDR_PM_UPDATE 0x00e5ef00  // ServerPlayerManager::update - runs every server frame on the main thread
static const unsigned char kUpdatePrologue[6] = {0x83, 0xEC, 0x08, 0x56, 0x8B, 0xF1}; // sub esp,8; push esi; mov esi,ecx

static BotModHost g_host;
static FILE *g_log;
static char g_dir[MAX_PATH];
static char g_cmdPath[MAX_PATH];
static CRITICAL_SECTION g_queueLock;
static char g_queue[32][128];
static int g_queueCount;

static HMODULE g_core;
static unsigned g_coreGeneration;
static BotModTick_t g_coreTick;
static BotModCommand_t g_coreCommand;
static BotModShutdown_t g_coreShutdown;

// Log rotation: at LOG_ROTATE_BYTES the log becomes BotMod_<port>.old.log (replacing the previous one) and a fresh log
// starts - at most ~2x that on disk, always at least the last LOG_ROTATE_BYTES of history.
#define LOG_ROTATE_BYTES (50L * 1024 * 1024)
static char g_logPath[MAX_PATH + 32];
static CRITICAL_SECTION g_logLock;
static void rotate_log_locked(void)
{
    char old[MAX_PATH + 48];
    snprintf(old, sizeof old, "%.*s.old.log", (int)(strlen(g_logPath) - 4), g_logPath);   // BotMod_25300.log -> BotMod_25300.old.log
    if (g_log) fclose(g_log);
    MoveFileExA(g_logPath, old, MOVEFILE_REPLACE_EXISTING);
    g_log = fopen(g_logPath, "a");
}

// Console echo: lines go through a queue to a writer thread, so no game thread ever waits on the console window.
// (Windows stops every write to a console while text is selected in it - one click in the window used to freeze
// the server's main thread until its watchdog killed the process.) When the console falls behind, lines are dropped
// from the echo only; the log file always gets everything.
#define CON_LINES 512
static char g_con[CON_LINES][1100];
static unsigned g_conHead, g_conTail, g_conDropped;
static CRITICAL_SECTION g_conLock;
static HANDLE g_conEvent, g_conOut;
static void console_push(const char *s)
{
    EnterCriticalSection(&g_conLock);
    if (g_conHead - g_conTail < CON_LINES) snprintf(g_con[g_conHead++ % CON_LINES], sizeof g_con[0], "[BotMod] %s\r\n", s);
    else g_conDropped++;
    LeaveCriticalSection(&g_conLock);
    if (g_conEvent) SetEvent(g_conEvent);
}
static DWORD WINAPI console_writer(LPVOID unused)
{
    (void)unused;
    for (;;) {
        WaitForSingleObject(g_conEvent, 1000);
        for (;;) {
            char line[1100];
            unsigned dropped;
            EnterCriticalSection(&g_conLock);
            int have = g_conTail != g_conHead;
            if (have) memcpy(line, g_con[g_conTail++ % CON_LINES], sizeof line);
            dropped = g_conDropped; g_conDropped = 0;
            LeaveCriticalSection(&g_conLock);
            DWORD w;
            if (dropped) {
                char note[80];
                int n = snprintf(note, sizeof note, "[BotMod] (%u line(s) not shown here - see the log file)\r\n", dropped);
                WriteConsoleA(g_conOut, note, n, &w, NULL);
            }
            if (!have) break;
            WriteConsoleA(g_conOut, line, (DWORD)strlen(line), &w, NULL);
        }
    }
    return 0;
}

static void host_log(const char *fmt, ...)
{
    va_list ap;
    char buf[1024];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    console_push(buf);
    EnterCriticalSection(&g_logLock);   // the core logs from several threads (chat, scans)
    if (g_log) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        fprintf(g_log, "%02d:%02d:%02d %s\n", t.wHour, t.wMinute, t.wSecond, buf);
        fflush(g_log);
        if (ftell(g_log) > LOG_ROTATE_BYTES) rotate_log_locked();
    }
    LeaveCriticalSection(&g_logLock);
}

// The server writes a crash / stall dump into <instance>\Logs every time it goes down: keep the newest few.
#define KEEP_DUMPS 10
static void prune_dumps(void)
{
    char inst[MAX_PATH] = "", pattern[MAX_PATH + 32], *cl = GetCommandLineA(), *p = strstr(cl, "-serverInstancePath ");
    if (p) {
        p += 20;
        while (*p == ' ') p++;
        if (*p == '"') { p++; char *e = strchr(p, '"'); snprintf(inst, sizeof inst, "%.*s", e ? (int)(e - p) : MAX_PATH - 1, p); }
        else sscanf(p, "%259s", inst);
    }
    if (!inst[0]) snprintf(inst, sizeof inst, "%s\\Instance", g_dir);
    size_t L = strlen(inst);
    while (L && (inst[L - 1] == '\\' || inst[L - 1] == '/')) inst[--L] = 0;
    snprintf(pattern, sizeof pattern, "%s\\Logs\\*.dmp", inst);
    static struct { char name[MAX_PATH]; FILETIME t; } list[256];
    int n = 0;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do { if (n < 256) { snprintf(list[n].name, MAX_PATH, "%s", fd.cFileName); list[n].t = fd.ftLastWriteTime; n++; } } while (FindNextFileA(h, &fd));
    FindClose(h);
    int removed = 0;
    while (n > KEEP_DUMPS) {   // drop the oldest
        int o = 0;
        for (int k = 1; k < n; k++) if (CompareFileTime(&list[k].t, &list[o].t) < 0) o = k;
        char path[MAX_PATH * 2 + 16];
        snprintf(path, sizeof path, "%s\\Logs\\%s", inst, list[o].name);
        if (DeleteFileA(path)) removed++;
        list[o] = list[--n];
    }
    if (removed) host_log("pruned %d old crash dump(s) from %s\\Logs (keeping the newest %d)", removed, inst, KEEP_DUMPS);
}

static void unload_core(void)
{
    if (!g_core) return;
    if (g_coreShutdown) g_coreShutdown();
    FreeLibrary(g_core);
    g_core = NULL;
    g_coreTick = NULL;
    g_coreCommand = NULL;
    g_coreShutdown = NULL;
}

// Copies BotModCore.dll to a per-generation name before loading so the build output never stays locked.
static void load_core(void)
{
    char src[MAX_PATH], dst[MAX_PATH];
    snprintf(src, sizeof src, "%s\\BotModCore.dll", g_dir);
    if (GetFileAttributesA(src) == INVALID_FILE_ATTRIBUTES) { host_log("core: %s not found", src); return; }

    unload_core();
    g_coreGeneration++;
    snprintf(dst, sizeof dst, "%s\\BotModCore.loaded%u_%d.dll", g_dir, g_coreGeneration % 2, g_host.gamePort);
    if (!CopyFileA(src, dst, FALSE)) { host_log("core: copy to %s failed (%lu)", dst, GetLastError()); return; }

    HMODULE m = LoadLibraryA(dst);
    if (!m) { host_log("core: LoadLibrary failed (%lu)", GetLastError()); return; }
    BotModInit_t init = (BotModInit_t)GetProcAddress(m, "botmod_init");
    BotModTick_t tick = (BotModTick_t)GetProcAddress(m, "botmod_tick");
    BotModCommand_t cmd = (BotModCommand_t)GetProcAddress(m, "botmod_command");
    BotModShutdown_t shut = (BotModShutdown_t)GetProcAddress(m, "botmod_shutdown");
    if (!init || !tick || !cmd || !shut) { host_log("core: missing exports"); FreeLibrary(m); return; }
    if (!init(&g_host)) { host_log("core: init failed"); FreeLibrary(m); return; }
    g_core = m;
    g_coreTick = tick;
    g_coreCommand = cmd;
    g_coreShutdown = shut;
    host_log("core: loaded generation %u", g_coreGeneration);
}

static void run_command(char *line)
{
    char *nl = strpbrk(line, "\r\n");
    if (nl) *nl = 0;
    while (*line == ' ') line++;
    if (!*line) return;
    if (!strcmp(line, "reload")) { host_log("> reload"); load_core(); return; }
    if (!strcmp(line, "unload")) { host_log("> unload"); unload_core(); return; }
    if (g_coreCommand) g_coreCommand(line);
    else host_log("no core loaded; command ignored: %s", line);
}

static void poll_command_file(void)
{
    FILE *f = fopen(g_cmdPath, "r");
    if (!f) return;
    char lines[32][128];
    int n = 0;
    while (n < 32 && fgets(lines[n], sizeof lines[n], f)) n++;
    fclose(f);
    DeleteFileA(g_cmdPath);
    for (int i = 0; i < n; i++) run_command(lines[i]);
}

// Called on the server main thread at the start of every ServerPlayerManager::update.
void __cdecl loader_tick(void *pm)
{
    (void)pm;
    g_host.frame++;

    EnterCriticalSection(&g_queueLock);
    int n = g_queueCount;
    char local[32][128];
    memcpy(local, g_queue, sizeof(g_queue[0]) * n);
    g_queueCount = 0;
    LeaveCriticalSection(&g_queueLock);
    for (int i = 0; i < n; i++) run_command(local[i]);

    static int s_triedInitialLoad;
    if (!s_triedInitialLoad) { s_triedInitialLoad = 1; load_core(); }   // not from DllMain (loader lock)
    if (g_coreTick) g_coreTick();
    if (g_host.frame % 30 == 0) poll_command_file();
}

extern void hook_pm_update(void);
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _hook_pm_update\n"
    "_hook_pm_update:\n"
    "  pushad\n"
    "  pushfd\n"
    "  push ecx\n"
    "  call _loader_tick\n"
    "  add esp, 4\n"
    "  popfd\n"
    "  popad\n"
    "  sub esp, 8\n"
    "  push esi\n"
    "  mov esi, ecx\n"
    "  push 0x00e5ef06\n"
    "  ret\n"
    ".att_syntax prefix\n");

static int install_update_hook(void)
{
    unsigned char *target = (unsigned char *)ADDR_PM_UPDATE;
    if (memcmp(target, kUpdatePrologue, sizeof kUpdatePrologue) != 0) {
        host_log("ERROR: ServerPlayerManager::update prologue mismatch - wrong server build? BotMod disabled.");
        return 0;
    }
    DWORD old;
    VirtualProtect(target, 6, PAGE_EXECUTE_READWRITE, &old);
    target[0] = 0xE9;
    *(DWORD *)(target + 1) = (DWORD)((unsigned char *)hook_pm_update - (target + 5));
    target[5] = 0x90;
    VirtualProtect(target, 6, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, 6);
    host_log("hooked ServerPlayerManager::update @ %p", target);
    return 1;
}

static DWORD WINAPI console_thread(LPVOID unused)
{
    (void)unused;
    char line[128];
    while (fgets(line, sizeof line, stdin)) {
        EnterCriticalSection(&g_queueLock);
        if (g_queueCount < 32) strcpy(g_queue[g_queueCount++], line);
        LeaveCriticalSection(&g_queueLock);
    }
    return 0;
}

static int parse_game_port(void)
{
    const char *p = strstr(GetCommandLineA(), "-GamePort ");
    return p ? atoi(p + 10) : 0;
}

static int is_bf3_final_server(void)
{
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, sizeof exe);
    const char *base = strrchr(exe, '\\');
    base = base ? base + 1 : exe;
    return _stricmp(base, "bf3_server_Final.exe") == 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    if (!is_bf3_final_server()) return TRUE;

    InitializeCriticalSection(&g_queueLock);
    InitializeCriticalSection(&g_conLock);
    g_host.version = BOTMOD_HOST_VERSION;
    g_host.log = host_log;
    g_host.gamePort = parse_game_port();

    GetModuleFileNameA(NULL, g_dir, sizeof g_dir);
    *strrchr(g_dir, '\\') = 0;
    char path[MAX_PATH + 32];
    snprintf(path, sizeof path, "%s\\BotMod_%d.log", g_dir, g_host.gamePort);
    InitializeCriticalSection(&g_logLock);
    snprintf(g_logPath, sizeof g_logPath, "%s", path);
    g_log = fopen(path, "a");
    if (g_log) { fseek(g_log, 0, SEEK_END); if (ftell(g_log) > LOG_ROTATE_BYTES) rotate_log_locked(); }   // already big from an earlier run
    snprintf(g_cmdPath, sizeof g_cmdPath, "%s\\BotMod_%d.cmd", g_dir, g_host.gamePort);

    if (AllocConsole()) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONIN$", "r", stdin);
        char title[64];
        snprintf(title, sizeof title, "BF3 BotMod - port %d", g_host.gamePort);
        SetConsoleTitleA(title);
        HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode;
        if (GetConsoleMode(in, &mode)) SetConsoleMode(in, (mode | ENABLE_EXTENDED_FLAGS) & ~ENABLE_QUICK_EDIT_MODE);   // no click-to-select
        g_conOut = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (g_conOut != INVALID_HANDLE_VALUE) {
            g_conEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
            CloseHandle(CreateThread(NULL, 0, console_writer, NULL, 0, NULL));
        }
    }
    host_log("loader started (port %d). Commands via console or %s ('reload' hot-swaps BotModCore.dll)", g_host.gamePort, g_cmdPath);
    prune_dumps();

    // The core is loaded on the first server frame (main thread), never from DllMain.
    if (install_update_hook())
        CloseHandle(CreateThread(NULL, 0, console_thread, NULL, 0, NULL));
    return TRUE;
}
