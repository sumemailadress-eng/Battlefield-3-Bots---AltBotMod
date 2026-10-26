// Caspian V2: Fun-Bots waypoint ("trace") networks as a native route graph.
//
// Fun-Bots ships one plain-text file per level and game mode (for example
// MP_007_ConquestLarge0.map): human-recorded waypoint paths with explicit links
// between them. BotMod loads the file for the current level, joins the paths
// into one graph and, per flag, precomputes the shortest route from every
// waypoint to that flag (one Dijkstra per flag, on first use). A bot then gets
// a hand-walked route by following "next" pointers: no search at run time.
//
// Pure C, no engine dependency; trace_nav.c is compiled into BotModCore.dll and
// into the offline tests.
#pragma once
#include <stddef.h>

#define TN_MAX_FIELDS 48    // Caspian V3: 0..15 flags, 16..47 cached routes to other points (vehicles, cover, squad spots)
#define TN_FLAG_FIELDS 16
#define TN_FLAG_JUMP  1u   // Fun-Bots ExtraMode 1: a jump was recorded here
#define TN_FLAG_CROUCH 2u  // SpeedMode 1/2: prone or crouch section
#define TN_FLAG_WAIT  4u   // SpeedMode 0: recorded wait
// AltBotMod V15: the wait's length in seconds (Fun-Bots OptValue, capped at 15) in the top 4 bits of the flags
#define TN_WAIT_SHIFT 4
#define TN_WAIT_SECONDS(f) ((unsigned)(f) >> TN_WAIT_SHIFT)

// AltBotMod V15: Fun-Bots path labels. Single letters ("a".."p") are the capture points a path serves (bit 0 = a);
// "base us" / "base ru" the bases.
#define TN_OBJ_BASE_US (1u << 16)
#define TN_OBJ_BASE_RU (1u << 17)
typedef struct TNNode { float p[3]; int path, point; unsigned char flags; unsigned obj; } TNNode;

// AltBotMod V15: the actions recorded on waypoints ("Action":{"type":...}).
enum { TN_ACT_OTHER = 0, TN_ACT_VEHICLE = 1, TN_ACT_EXIT = 2, TN_ACT_BEACON = 3, TN_ACT_MCOM = 4 };
// What the path leading to a boarding spot was labelled ("vehicle tank1 us", "vehicle spawn ru havoc", ...)
enum { TN_VEH_UNKNOWN = 0, TN_VEH_TANK = 1, TN_VEH_OTHER = 2, TN_VEH_AIR = 3 };
typedef struct TNAction {
    float p[3], yaw, pitch, time;
    int path, point;
    unsigned char type, onlyPassengers, vehKind;
    signed char team;   // 1 US, 2 RU, 0 both / not said
} TNAction;
typedef struct TNField { int ready; float goal[3]; float radius; float *dist; int *next; } TNField;
typedef struct TraceNet {
    int count, edgeCount, pathCount, vehiclePaths, linkCount, proximityLinks, skippedLinks;
    TNNode *nodes;
    int *edgeStart;      // CSR: edges of node n are edgeTo[edgeStart[n] .. edgeStart[n+1]-1]
    int *edgeTo;
    float *edgeCost;
    TNField field[TN_MAX_FIELDS];
    char name[64];
    TNAction *actions; int actionCount;   // AltBotMod V15: every recorded action in the file (all paths, any kind)
    unsigned flagPrefer[TN_FLAG_FIELDS];   // V15: the label bit of each flag (tn_match_flags) - routes along its paths cost less
    int labelledPaths;
    int primaryNodes, companionNodes;   // AltBotMod V18.2: waypoints from the mode's own file / from its companion file
    int learnedNodes;                   // AltBotMod V21: waypoints from routes human players showed (BotMod_learned)
} TraceNet;

// Parse a Fun-Bots .map file. Vehicle-only paths (first point lists
// "Vehicles") are left out: infantry graph only. Returns 1 on success.
int tn_load(TraceNet *t, const char *file);
// AltBotMod V6: the Fun-Bots VEHICLE paths as their own graph - TN_KIND_LAND (jeeps, tanks) or TN_KIND_WATER (boats).
#define TN_KIND_LAND  1
#define TN_KIND_WATER 2
int tn_load_kind(TraceNet *t, const char *file, int kind);
// AltBotMod V18.2: the infantry graph of `file` plus, when `companion` exists and was built for this very file (its
// header names the file and its size), the companion's paths: the same level's other Fun-Bots recordings within the
// mode's area (tools/build_route_companions.py). primaryNodes / companionNodes say how many waypoints came from each.
int tn_load_with_companion(TraceNet *t, const char *file, const char *companion);
int tn_load_text_kind(TraceNet *t, const char *text, size_t length, int kind);
// AltBotMod V21: as tn_load_with_companion (kind 0) / tn_load_kind (vehicle kinds), plus `learned`: Fun-Bots lines (a
// header line first) of routes human players showed. Their paths are renumbered after the files' own, so they join
// the network wherever they cross it, like any recording. companion is only used for kind 0. learned may be NULL.
// AltBotMod V21.1: every link with an end (or its middle) within `radius` of p (horizontal; 6 m up / down) costs
// `factor` times more, and the cached routes (fields) are dropped so the next route request sees it. Returns the links
// changed. Used for learned spots drivers never get out of: the Fun-Bots route avoids them when another road exists.
int tn_penalize_near(TraceNet *t, const float p[3], float radius, float factor);
int tn_load_layers(TraceNet *t, const char *file, const char *companion, const char *learned, size_t learnedLength, int kind);
// Same, from text already in memory (offline tests).
int tn_load_text(TraceNet *t, const char *text, size_t length);
void tn_free(TraceNet *t);

// Shortest distance from every node to the nodes within `radius` (horizontal)
// of `goal`. Cached in slot `id`; recomputed only when the goal changes.
int tn_field(TraceNet *t, int id, const float goal[3], float radius);

// Up to `max` nodes within maxH horizontal / maxV vertical metres of `pos`
// that can reach field `id`, best (leg + remaining route) first.
int tn_candidates(const TraceNet *t, int id, const float pos[3], float maxH, float maxV, int *out, int max);

// The route from node `start` to field `id`, simplified to points at least
// `spacing` metres apart (jumps, crouch sections and sharp turns are kept).
// Writes up to maxPts points; *truncated is set when the route was longer.
// Returns the number of points written, or 0 when the node cannot reach the field.
int tn_route(const TraceNet *t, int id, int start, float spacing, float *pts, unsigned char *flags, int maxPts, int *truncated, float *lengthOut);

// AltBotMod V15: which flag each Fun-Bots objective letter is. Paths labelled with letters end at those flags: each
// path end votes for the flags within 80 m of it, the strongest letter/flag pairs win (one flag per letter).
// letterOut[f] = 0..15 (a..p) or -1. Fields of matched flags then prefer their own labelled paths (cost x0.8).
// Returns the number of flags matched.
int tn_match_flags(TraceNet *t, const float (*flags)[3], int count, int *letterOut);
// The recorded action of `type` nearest `pos` within maxH horizontal / maxV vertical metres, -1 = none.
int tn_action_near(const TraceNet *t, int type, const float pos[3], float maxH, float maxV);
