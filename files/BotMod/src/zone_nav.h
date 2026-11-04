// AltBotMod V34: Fun-Bots' walking mesh around the objectives ("navzones", Fun-Bots 3.6.0-dev).
//
// Fun-Bots 3.6 dev bots walk a mesh of points at the capture points, M-COMs and bases instead of following recorded
// waypoints there (ext/Server/NavZones.lua). The meshes ship as JSON (navzones/<Level>_<Mode>.json); AltBotMod's
// package converts them to a plain text file per level and mode (BotMod_traces\navzones\<Level>_<Mode>.zon, written
// by validation/tools/navzones_convert.py):
//   AltBotMod navzones 1 <Level>_<Mode>
//   P <points>                      then per point:  x y z flags        (flags: 1 in a zone, 2 indoors, 4 crouch)
//   E <edges>                       then per edge:   a b length oneway corners jumpmask [x y z per corner]
//                                   (corners lie between a and b in the a->b direction; oneway 1 only a->b, -1 only b->a;
//                                    jumpmask: bit k = the recording soldier jumped at corner k)
//   Z <zones>                       then per zone:   kind radius cx cy cz inside name, and on the next line its points
//                                   (kind 1 capture point, 2 M-COM, 3 base)
// Pure C, no engine dependency: compiled into BotModCore.dll and into the offline tests.
#pragma once

#define ZN_KIND_CAPTURE 1
#define ZN_KIND_MCOM    2
#define ZN_KIND_BASE    3
#define ZN_PT_ZONE      1u
#define ZN_PT_INDOOR    2u
#define ZN_PT_CROUCH    4u
// route point flags handed back with a route (the same bits as trace_nav.h's TN_FLAG_JUMP / TN_FLAG_CROUCH)
#define ZN_ROUTE_JUMP   1u
#define ZN_ROUTE_CROUCH 2u

typedef struct ZnPoint { float p[3]; unsigned char flags; int part; } ZnPoint;
typedef struct ZnEdge { int a, b, corner0, ncorner; float len, penalty; signed char oneway; unsigned char removed, stuck; unsigned jumps; } ZnEdge;
typedef struct ZnZone { char name[24]; int kind; float c[3], radius; int inside0, ninside; } ZnZone;
typedef struct ZoneNet {
    int np, ne, nz, ncorners, ninside, parts;
    ZnPoint *pts; ZnEdge *edges; float *corners; ZnZone *zones; int *inside;
    int *adjStart, *adj;       // CSR: edges touching point n are adj[adjStart[n] .. adjStart[n+1]-1] (edge indices)
    char name[64];
    // A* scratch (one search at a time - the game thread)
    float *g; int *from, *heap, *heapPos; unsigned *seen; unsigned stamp;
    // AltBotMod V35 (Fun-Bots V3.6.0-RC2): metres added per point for team 1 / 2 where its soldiers died lately
    // (zn_on_killed), fading by half a minute (zn_decay); NULL until the first death.
    float *danger[3]; float dangerTime; int dangerCount[3];
} ZoneNet;

int  zn_load(ZoneNet *z, const char *file);          // 1 loaded
int  zn_load_text(ZoneNet *z, const char *text);     // (tests) the same from a string
void zn_free(ZoneNet *z);
// The mesh point closest to pos within maxH metres across and maxV up / down (-1: none). sameFloor prefers points within
// 1.5 m of the height first.
int  zn_closest(const ZoneNet *z, const float *pos, float maxH, float maxV);
// The zone of this kind (0: any) whose centre is closest to pos and within maxDist of it (-1: none).
int  zn_zone_near(const ZoneNet *z, const float *pos, int kind, float maxDist);
// One of the zone's points (seed picks which); -1 when the zone has none.
int  zn_zone_point(const ZoneNet *z, int zone, unsigned seed);
// Route from point a to point b: the positions to walk (corners and points, without a), their flags, at most maxPts.
// Returns the count (0: no way on the mesh); *partial = 1 when maxPts cut it short; *length its length in metres.
int  zn_route(ZoneNet *z, int a, int b, float *out, unsigned char *flags, int maxPts, int *partial, float *length);
// AltBotMod V35 (Fun-Bots V3.6.0-RC2, NavZones.lua): the same with the bot's own way among similar ones and its team's
// danger. seed (not 0): each 30 m region of the mesh seems 1 .. 1+ZN_SPREAD longer to this bot, so the bots spread over
// the ways instead of all walking one line. team 1 / 2: the metres where that team was killed lately are added (0: none).
// *length is always the real length of the way.
#define ZN_SPREAD          0.25f   // (Registry.BOT.NAV_ROUTE_SPREAD)
#define ZN_SPREAD_REGION  30.0f
int  zn_route_ex(ZoneNet *z, int a, int b, unsigned seed, int team, float *out, unsigned char *flags, int maxPts, int *partial, float *length);
// A soldier of the team died on foot at pos: the points up to 6 m around it on its floor (2.5 m up / down) cost the team
// 15 m more, at most 50 m a point. now: seconds (any clock that only goes up). Returns the points marked.
#define ZN_DANGER_RANGE     6.0f
#define ZN_DANGER_FLOOR     2.5f
#define ZN_DANGER_PER_DEATH 15.0f
#define ZN_DANGER_MAX       50.0f
#define ZN_DANGER_HALF_LIFE 60.0f
#define ZN_DANGER_STEP       5.0f
#define ZN_DANGER_MIN        2.0f
int  zn_on_killed(ZoneNet *z, int team, const float *pos, float now);
// Lets the danger fade: half of it is gone after ZN_DANGER_HALF_LIFE seconds (in steps of ZN_DANGER_STEP), below
// ZN_DANGER_MIN a point is clear again.
void zn_decay(ZoneNet *z, float now);
// The danger of the team at point n in metres (0: none).
float zn_danger(const ZoneNet *z, int team, int n);
// Whether a bot's life (by its seed) weighs the danger: 3 of 4 lives do, the rest take the shortest way so the attack goes
// on there as well (NavRoutes.lua CAUTIOUS_SHARE).
int  zn_cautious(unsigned seed);
// A bot got stuck near pos: the connection there costs `amount` more from now on (after 3 times it is closed for the
// level, as Fun-Bots does). Returns the edge index or -1.
int  zn_penalize(ZoneNet *z, const float *pos, float amount);
