// AltBotMod V21 - learning. What bots learn on a map (and from the human players on it), kept in
// BotMod_learned\<Level>_<Mode>.learn next to the server, one text record per line:
//
//   AltBotMod learned<TAB>1<TAB><Level>_<Mode>                 header (format 1)
//   spot<TAB>foot|ground|boat|air<TAB>x y z<TAB>hx hz<TAB>seen passed failed jumps<TAB>first last   a stuck spot
//   heat<TAB>cx cz<TAB>deaths                                   deaths in a 10 m cell
//   route<TAB>foot|ground|boat<TAB>uses made<TAB>x,y,z x,y,z ... a path a human player walked / drove
//   pad<TAB>heli<TAB>x y z<TAB>landed failed<TAB>first last      a helicopter landing spot (players' and bots' landings)
//   map<TAB>map_<Level>.bmp<TAB>minX minZ maxX maxZ<TAB>w h      the live map picture's bounds (Server Control draws on it)
//
// Anything else (a later version's records, comments) is kept as it is and written back, so a file goes forward and
// back between versions without losing anything. Deleting a line deletes what was learned (Server Control's Learning
// tab does this); the running server notices the file changed and reloads it before it next saves.
// Pure C: no game or Windows calls (the offline tests build it on Linux).
#pragma once
#include <stddef.h>

#define BL_FORMAT 1
enum { BL_FOOT, BL_GROUND, BL_BOAT, BL_AIR, BL_KINDS };
#define BL_MAX_SPOTS   768
#define BL_MAX_HEAT    6000
#define BL_MAX_ROUTES  96
#define BL_ROUTE_PTS   240
#define BL_MAX_EXTRA   256
#define BL_MAX_PADS    64

typedef struct BLSpot {
    int kind;
    float p[3], h[2];            // where, and the way the bot was trying to go (unit vector, 0 0 = unknown)
    unsigned seen, passed, failed, jumps;   // stuck here / got through afterwards / still stuck / got through with a jump
    long first, last;            // unix time
    unsigned base[4];            // the counts at the last load / save (what this session added = counts - base)
} BLSpot;
typedef struct BLHeat { int cx, cz; float deaths; unsigned fresh; } BLHeat;
typedef struct BLRoute {
    int kind, n;
    unsigned uses; long made;
    float pts[BL_ROUTE_PTS][3];
    unsigned usesBase, isNew;    // uses at the last load / save; learned this session (not on disk yet)
} BLRoute;
typedef struct BLPad {
    float p[3];
    unsigned landed, failed;     // touchdowns there / times a transport found it blocked
    long first, last;
    unsigned base[2], isNew;     // counts at the last load / save; learned this session
} BLPad;
typedef struct BLStore {
    char key[96];
    BLSpot spots[BL_MAX_SPOTS]; int nSpots;
    BLHeat heat[BL_MAX_HEAT]; int nHeat;
    BLRoute routes[BL_MAX_ROUTES]; int nRoutes;
    BLPad pads[BL_MAX_PADS]; int nPads;
    char *extra[BL_MAX_EXTRA]; int nExtra;   // records this version does not know (kept)
    int dirty, loadedFormat;
} BLStore;

const char *bl_kind_name(int kind);
int bl_kind_parse(const char *s);
void bl_init(BLStore *s, const char *key);
void bl_free(BLStore *s);
// Text in / out. bl_load_text replaces the store's contents; it returns the number of records read (-1: not a
// learned file). bl_write returns the bytes written into buf (needs bl_text_size bytes).
int bl_load_text(BLStore *s, const char *text, size_t length);
size_t bl_text_size(const BLStore *s);
size_t bl_write(const BLStore *s, char *buf, size_t size);
// After the file changed on disk (someone deleted entries): reload it, then put back what this session learned since
// the last save (fresh records), so neither the edit nor the new learning is lost.
int bl_reload_keep_fresh(BLStore *s, const char *text, size_t length);
void bl_mark_saved(BLStore *s);
// Ageing once per map load: death heat x0.8 (cells under 0.3 dropped); spots and routes stay until deleted.
void bl_fade(BLStore *s);

// Stuck spots. Merge radius: foot 2.5 m, ground / boat 8 m, air 60 m (and a heading within 60 deg, when both known).
int bl_note_stuck(BLStore *s, int kind, const float p[3], const float h[2], long now);
void bl_note_result(BLStore *s, int spot, int passed, int jumped, long now);
int bl_find(const BLStore *s, int kind, const float p[3], const float h[2], float radius);
int bl_spot_bad(const BLSpot *sp);        // stuck again and again, nobody ever got through: avoid it
int bl_spot_passable(const BLSpot *sp);   // bots do get through here: keep the way open
int bl_spot_jump(const BLSpot *sp);       // got through mostly with a jump: jump on the way in
// Deaths: 10 m cells. bl_danger: 0 (safe) .. 1 (a killing ground).
void bl_note_death(BLStore *s, const float p[3]);
float bl_danger(const BLStore *s, float x, float z);
// Routes from human players. kind foot / ground / boat. Returns the route index, -1 when too short or (mostly) the
// same as one already learned - then that one counts another use.
int bl_add_route(BLStore *s, int kind, const float (*pts)[3], int n, long now);
float bl_route_length(const BLRoute *r);
// The learned routes of one kind as Fun-Bots .map lines (a header line first; paths numbered from 0), for the trace
// loader. Returns the bytes written (buf may be NULL to measure).
size_t bl_routes_map_text(const BLStore *s, int kind, char *buf, size_t size);
// Helicopter landing spots: a touchdown merges with a pad within 15 m. bl_pad_near: the nth best usable pad within
// `radius` of `center` (closest first; pads that were found blocked more often than landed on are skipped).
int bl_note_pad(BLStore *s, const float p[3], int landed, long now);
int bl_pad_near(const BLStore *s, const float center[3], float radius, int nth, float out[3]);
// Delete record `index` of type 's' (spot), 'h' (heat), 'r' (route), 'p' (pad).
int bl_delete(BLStore *s, char type, int index);
