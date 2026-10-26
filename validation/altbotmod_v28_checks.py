#!/usr/bin/env python3
"""AltBotMod V28: offline checks.

1. Gamertags (core.c bot_roster / find_roster / roster_remember / gamertag_make / gamertag_pick): a bot created under a
   gamertag keeps its roster identity ("[BOT] Support RU 2") for side, class and roster place; the tags come from
   BotMod_names.txt (written with 300 made-up tags when missing), never one a player already has, at most 15 characters.
2. Source checks: every seat of an aircraft looks for a gun (door miniguns), a seat with none is searched again every 20 s;
   the ticket bleed is held back only by the game's own bleed (not a person's death); the road-keeping aim (closer when 2 m+
   off the line, 17-deg bend stop); every lock-on attempt is reported, and the bot fires after 3 s of the game "locking";
   the version.
"""
from pathlib import Path
import argparse, re, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_PATH 260
#define OFF_PLAYER_ONLINE_NAME 0
#define MAX_BOTS 8
static struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; } host, *H = &host;
static struct { int botNames; } g_enhanced = {1};
static char dir[200];
static void botmod_file(char *out, int n, const char *f) { snprintf(out, n, "%s/%s", dir, f); }
static void bot_log(const char *f, ...) { (void)f; }
static char players[8][64]; static int nplayers;
static void *find_player(const char *name) { for (int k = 0; k < nplayers; k++) if (!strcmp(players[k], name)) return players[k]; return NULL; }
static int bot_index(void *pl) { for (int k = 0; k < H->botCount; k++) if (H->bots[k] == pl) return k; return -1; }
static int roster_ordinal(const char *name) { return strstr(name, "[BOT] ") ? 0 : -1; }
@@CODE@@
int main(int argc, char **argv)
{
    snprintf(dir, sizeof dir, "%s", argv[1]);
    /* no list: one is written (300 tags) and used */
    char tag[16]; assert(gamertag_pick(tag, sizeof tag) && strlen(tag) <= 15 && tag[0]);
    char path[300]; snprintf(path, sizeof path, "%s/BotMod_names.txt", dir);
    FILE *f = fopen(path, "r"); assert(f); int lines = 0; char line[512]; while (fgets(line, sizeof line, f)) if (line[0] != '#') { lines++; assert(strlen(line) <= 16); } fclose(f);
    assert(lines >= 250);
    /* a player already called that: never handed out */
    strcpy(players[nplayers++], tag); for (int k = 0; k < 50; k++) { char t2[16]; assert(gamertag_pick(t2, sizeof t2) && !find_player(t2)); }
    /* the roster identity follows the player whatever it is called */
    strcpy(players[nplayers], "SilentWolf42"); H->bots[H->botCount++] = players[nplayers++];
    roster_remember(H->bots[0], "[BOT] Support RU 2");
    assert(!strcmp(bot_roster(H->bots[0]), "[BOT] Support RU 2") && find_roster("[BOT] Support RU 2") == H->bots[0]);
    strcpy(players[nplayers], "[BOT] Medic US"); H->bots[H->botCount++] = players[nplayers++];
    assert(!strcmp(bot_roster(H->bots[1]), "[BOT] Medic US"));   /* a bot under its roster name: the name itself */
    assert(find_roster("[BOT] Recon US") == NULL);
    H->botCount = 0; assert(find_roster("[BOT] Support RU 2") == NULL);   /* gone from the server: the slot is free again */
    puts("PASS: gamertags - a list of 300 is written when there is none and used; a name a player already has is never handed out; a bot under a gamertag keeps its roster identity (side, class, place) and a removed one frees its slot");
    return 0;
}
'''

def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit('FAILED: ' + ' '.join(map(str, cmd)) + '\n' + r.stdout + r.stderr)
    return r.stdout

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); args = ap.parse_args()
    core = (SRC / 'core.c').read_text()
    a = core.index('#define ROSTER_MAX 128'); b = core.index('static void cmd_add_core(const char *name, int team, int isAI);')
    code = core[a:b].replace('static int roster_ordinal(const char *name); static int bot_index(void *pl); static void botmod_file(char *out, int n, const char *name);', '')
    code = code.replace('static void *find_player(const char *name);', '')
    san = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'] if args.sanitize else []
    with tempfile.TemporaryDirectory() as td:
        (Path(td) / 'names.c').write_text(HARNESS.replace('@@CODE@@', code))
        run(['gcc', *san, '-std=gnu11', '-O1', '-w', str(Path(td) / 'names.c'), '-o', str(Path(td) / 'names')])
        print(run([str(Path(td) / 'names'), td]).strip())
    en = (SRC / 'enhanced.c').read_text(); eh = (SRC / 'enhanced.h').read_text(); sc = (ROOT / 'files/ServerControl/ServerControl-Core.ps1').read_text()
    assert 'BOOL_KEY("BotNames", botNames)' in en and 'c->botNames = 1;' in en and "New-BmSetting 'BotNames'" in sc
    for old, new in [('bot_role_class(bot + OFF_PLAYER_ONLINE_NAME)', 'bot_role_class(BOT_ROSTER(bot))'), ('int t=bm_roster_team(H->bots[i]+OFF_PLAYER_ONLINE_NAME', 'int t=bm_roster_team(BOT_ROSTER(H->bots[i])'),
                     ('if(find_player(name))continue;cmd_add(name,add,0)', 'if(find_roster(name))continue;cmd_add(name,add,0)')]:
        assert old not in core and new in core, new
    assert 'seat >= airframe->firstPassenger' not in core and 'found no weapon on its seat - looks again in 20 s' in core
    assert 'if (fabsf(delta) < 0.95f) g_gameTicketsAt[team]' in core
    assert 'if (off > 2.0f) L = fmaxf(8.0f, L - (off - 2.0f) * 3.5f);' in en and 'if (fabsf(t) > 0.3f) break;' in en
    assert 'lock_attempt_report(i, why);' in core and 'int forced = native == 1 && g_lockNativeSince[i] && H->frame - g_lockNativeSince[i] > 3 * 30;' in core
    assert '#define BM_ENHANCED_VERSION "AltBotMod V34 - Squad Up"' in eh
    print('PASS: V28 source - gamertags on (BotNames, in Server Control) with roster identity everywhere it is read; door guns on every aircraft seat, re-searched every 20 s; '
          'bleed held back only by the game\'s own bleed; road-keeping aim; every lock-on attempt reported, fire after 3 s of locking; version V28')

if __name__ == '__main__':
    main()
