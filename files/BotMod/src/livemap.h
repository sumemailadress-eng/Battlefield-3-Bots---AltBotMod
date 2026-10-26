// AltBotMod V13 live map: the portable half (no engine reads). The game side is livemap_native.inc in core.c.
// What the server writes to BotMod\LiveMap (all tab-separated text, read by Server Control's Live map page):
//   live_<port>.txt     the current picture, replaced every LiveMapSeconds (players, goals, routes, flags)
//   events_<port>.txt   appended: round starts, kills (who killed whom, where, weapon), stuck spots, movement samples
//   map_<level>.bmp     a top-down picture of the map drawn from the bots' navigation grid (once per map)
#pragma once
#include "nav.h"

#define LM_FORMAT 1

// A name or weapon made safe for one tab-separated field: tabs, line breaks and control characters become '_',
// an empty text becomes "-".
void lm_clean(char *out, int n, const char *text);

// Top-down picture of a navigation grid, at most maxSide pixels on its long side. RGB, 3 bytes per pixel, first row
// = north edge (largest z), first column = west edge (smallest x). bounds = minX, minZ, maxX, maxZ (metres) of the
// picture. Walkable ground is shaded by height and slope, water is blue, floors stacked over each other (buildings,
// bridges) are grey, the rest is dark. Returns malloc'd pixels (caller frees) or NULL.
unsigned char *lm_render_grid(const NavGrid *g, int maxSide, int *w, int *h, float bounds[4]);
// The same picture a strip of rows at a time (the game thread draws a few rows a frame):
typedef struct LMRender { const NavGrid *g; int w, h, row; float px, minX, top, lo, hi; float bounds[4]; unsigned char *rgb; } LMRender;
int lm_render_begin(LMRender *r, const NavGrid *g, int maxSide);   // 0: nothing to draw
int lm_render_rows(LMRender *r, int rows);                         // 1 once every row is drawn
void lm_render_free(LMRender *r);
// 24-bit BMP (Windows reads it with no extra code). 1 written, 0 failed.
int lm_write_bmp(const char *path, const unsigned char *rgb, int w, int h);

// "No progress" watch for one bot: it has a goal more than LM_FAR_GOAL metres away but has not moved LM_MOVE_METRES
// in `frames`. Returns 1 once per stuck spot (again only after it has moved on).
#define LM_FAR_GOAL 20.0f
#define LM_MOVE_METRES 4.0f
typedef struct LMProgress { float x, z; unsigned since; int reported, valid; } LMProgress;
int lm_progress(LMProgress *p, const float *pos, float goalMetres, unsigned now, unsigned frames);

// Appending to an events file with a size cap: past maxBytes the file becomes <name>.old.txt (one generation kept)
// and a new one starts. The FILE is kept open between calls; lm_events_close flushes and closes it.
typedef struct LMEvents { void *file; char path[300]; long size, maxBytes; } LMEvents;
int lm_events_open(LMEvents *e, const char *path, long maxBytes);
int lm_events_line(LMEvents *e, const char *line);   // line without the newline
void lm_events_flush(LMEvents *e);
void lm_events_close(LMEvents *e);
