// AltBotMod V13 live map - portable half (see livemap.h). Builds on Windows (mingw) and in the Linux tests.
#include "livemap.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void lm_clean(char *out, int n, const char *text)
{
    if (n <= 0) return;
    int k = 0;
    for (const unsigned char *s = (const unsigned char *)(text ? text : ""); *s && k < n - 1; s++)
        out[k++] = (*s < 32 || *s == 127) ? '_' : (char)*s;
    out[k] = 0;
    if (!k && n >= 2) { out[0] = '-'; out[1] = 0; }
}

// Topmost walkable floor of a cell: its height, how many separate floors it has and whether it is water.
static int cell_floor(const NavGrid *g, int cx, int cz, float *height, int *floors, int *water)
{
    if (cx < 0 || cz < 0 || cx >= g->w || cz >= g->h) return 0;
    size_t base = ((size_t)cz * g->w + cx) * NAV_LAYERS;
    int found = 0, n = 0; float top = 0, low = 0;
    for (int l = 0; l < NAV_LAYERS; l++) {
        if (!(g->flags[base + l] & NAV_WALKABLE)) continue;
        float y = g->height[base + l];
        if (!isfinite(y)) continue;
        if (!found) { top = low = y; found = 1; *water = (g->flags[base + l] & NAV_WATER) != 0; }
        else { if (y > top) top = y; if (y < low) low = y; }
        n++;
    }
    if (!found) return 0;
    *height = top; *floors = n > 1 && top - low > 2.0f ? n : 1;
    return 1;
}

int lm_render_begin(LMRender *r, const NavGrid *g, int maxSide)
{
    memset(r, 0, sizeof *r);
    if (!g || !g->height || !g->flags || g->w <= 0 || g->h <= 0 || !(g->cell > 0) || maxSide < 16) return 0;
    float minX = g->x0 - g->cell * 0.5f, minZ = g->z0 - g->cell * 0.5f;
    float spanX = g->w * g->cell, spanZ = g->h * g->cell;
    float px = fmaxf(spanX, spanZ) / maxSide;
    if (px < g->cell * 0.5f) px = g->cell * 0.5f;   // (no more than 2 pixels per cell)
    int W = (int)ceilf(spanX / px), Hh = (int)ceilf(spanZ / px);
    if (W < 1 || Hh < 1 || W > 8192 || Hh > 8192) return 0;
    // height range of the walkable ground, sampled (for the colour ramp)
    float lo = INFINITY, hi = -INFINITY;
    int stepX = g->w > 512 ? g->w / 512 : 1, stepZ = g->h > 512 ? g->h / 512 : 1;
    for (int cz = 0; cz < g->h; cz += stepZ)
        for (int cx = 0; cx < g->w; cx += stepX) {
            float y; int f, wat;
            if (cell_floor(g, cx, cz, &y, &f, &wat) && !wat) { if (y < lo) lo = y; if (y > hi) hi = y; }
        }
    if (!(hi > lo)) { lo = 0; hi = 1; }
    r->rgb = malloc((size_t)W * Hh * 3);
    if (!r->rgb) return 0;
    r->g = g; r->w = W; r->h = Hh; r->px = px; r->minX = minX; r->top = minZ + spanZ; r->lo = lo; r->hi = hi;
    // (pixel centres run from the north-west corner: the picture is exactly W x Hh pixels of px metres from there)
    r->bounds[0] = minX; r->bounds[3] = r->top; r->bounds[2] = minX + W * px; r->bounds[1] = r->top - Hh * px;
    return 1;
}
int lm_render_rows(LMRender *r, int rows)
{
    const NavGrid *g = r->g;
    if (!g || !r->rgb) return 1;
    const float lx = -0.5f, lz = 0.5f, ly = 0.707f;   // light from the north-west, 45 degrees up
    for (int end = r->row + rows; r->row < r->h && r->row < end; r->row++) {
        int y = r->row;
        float wz = r->top - (y + 0.5f) * r->px;
        int cz = (int)floorf((wz - g->z0) / g->cell + 0.5f);
        for (int x = 0; x < r->w; x++) {
            float wx = r->minX + (x + 0.5f) * r->px;
            int cx = (int)floorf((wx - g->x0) / g->cell + 0.5f);
            unsigned char *o = r->rgb + ((size_t)y * r->w + x) * 3;
            float top; int floors, water;
            if (!cell_floor(g, cx, cz, &top, &floors, &water)) { o[0] = 30; o[1] = 32; o[2] = 36; continue; }
            float e, wv, n2, s2; int f2, w2;
            float east = cell_floor(g, cx + 1, cz, &e, &f2, &w2) ? e : top, west = cell_floor(g, cx - 1, cz, &wv, &f2, &w2) ? wv : top;
            float north = cell_floor(g, cx, cz + 1, &n2, &f2, &w2) ? n2 : top, south = cell_floor(g, cx, cz - 1, &s2, &f2, &w2) ? s2 : top;
            float dx = (east - west) / (2 * g->cell), dz = (north - south) / (2 * g->cell);
            dx = dx > 4 ? 4 : dx < -4 ? -4 : dx; dz = dz > 4 ? 4 : dz < -4 ? -4 : dz;
            float shade = (-dx * lx - dz * lz + ly) / sqrtf(1 + dx * dx + dz * dz) / ly;
            shade = shade < 0.35f ? 0.35f : shade > 1.25f ? 1.25f : shade;
            float t = (top - r->lo) / (r->hi - r->lo), cr, cg, cb;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            // low green -> olive -> light tan
            if (t < 0.5f) { float u = t * 2; cr = 70 + u * 60; cg = 100 + u * 25; cb = 56 + u * 14; }
            else { float u = (t - 0.5f) * 2; cr = 130 + u * 55; cg = 125 + u * 45; cb = 70 + u * 60; }
            if (water) { cr = 46; cg = 86; cb = 140; shade = 1; }
            else if (floors > 1) { cr = cr * 0.4f + 150 * 0.6f; cg = cg * 0.4f + 150 * 0.6f; cb = cb * 0.4f + 160 * 0.6f; }   // floors over floors: grey
            cr *= shade; cg *= shade; cb *= shade;
            o[0] = (unsigned char)(cr > 255 ? 255 : cr); o[1] = (unsigned char)(cg > 255 ? 255 : cg); o[2] = (unsigned char)(cb > 255 ? 255 : cb);
        }
    }
    return r->row >= r->h;
}
void lm_render_free(LMRender *r) { free(r->rgb); r->rgb = NULL; r->g = NULL; }
unsigned char *lm_render_grid(const NavGrid *g, int maxSide, int *w, int *h, float bounds[4])
{
    LMRender r;
    if (!lm_render_begin(&r, g, maxSide)) return NULL;
    lm_render_rows(&r, r.h);
    *w = r.w; *h = r.h; memcpy(bounds, r.bounds, sizeof r.bounds);
    return r.rgb;
}

static void put32(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
int lm_write_bmp(const char *path, const unsigned char *rgb, int w, int h)
{
    if (!path || !rgb || w <= 0 || h <= 0) return 0;
    unsigned row = ((unsigned)w * 3 + 3) & ~3u, image = row * (unsigned)h;
    unsigned char hd[54] = {'B', 'M'};
    put32(hd + 2, 54 + image); put32(hd + 10, 54); put32(hd + 14, 40); put32(hd + 18, (unsigned)w); put32(hd + 22, (unsigned)h);
    hd[26] = 1; hd[28] = 24; put32(hd + 34, image); put32(hd + 38, 2835); put32(hd + 42, 2835);
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(hd, 1, 54, f) == 54;
    unsigned char *line = calloc(1, row);
    if (!line) ok = 0;
    for (int y = h - 1; ok && y >= 0; y--) {   // BMP rows run bottom-up
        const unsigned char *s = rgb + (size_t)y * w * 3;
        for (int x = 0; x < w; x++) { line[x * 3] = s[x * 3 + 2]; line[x * 3 + 1] = s[x * 3 + 1]; line[x * 3 + 2] = s[x * 3]; }   // BGR
        ok = fwrite(line, 1, row, f) == row;
    }
    free(line);
    if (fclose(f) != 0) ok = 0;
    return ok;
}

int lm_progress(LMProgress *p, const float *pos, float goalMetres, unsigned now, unsigned frames)
{
    if (!pos || goalMetres < LM_FAR_GOAL) { p->valid = 0; p->reported = 0; return 0; }   // no far goal: holding on purpose
    float dx = pos[0] - p->x, dz = pos[2] - p->z;
    if (!p->valid || dx * dx + dz * dz > LM_MOVE_METRES * LM_MOVE_METRES) {
        p->x = pos[0]; p->z = pos[2]; p->since = now; p->valid = 1; p->reported = 0;
        return 0;
    }
    if (!p->reported && now - p->since >= frames) { p->reported = 1; return 1; }
    return 0;
}

static void old_name(char *out, size_t n, const char *path)
{
    snprintf(out, n, "%s", path);
    char *dot = strrchr(out, '.'), *slash = strrchr(out, '\\'), *fwd = strrchr(out, '/');
    if (fwd > slash) slash = fwd;
    if (dot && (!slash || dot > slash)) snprintf(dot, n - (size_t)(dot - out), ".old%s", strrchr(path, '.'));
    else snprintf(out + strlen(out), n - strlen(out), ".old");
}
int lm_events_open(LMEvents *e, const char *path, long maxBytes)
{
    lm_events_close(e);
    snprintf(e->path, sizeof e->path, "%s", path);
    e->maxBytes = maxBytes;
    FILE *f = fopen(path, "ab");
    if (!f) { e->file = NULL; return 0; }
    fseek(f, 0, SEEK_END);
    e->size = ftell(f);
    if (e->size < 0) e->size = 0;
    e->file = f;
    return 1;
}
int lm_events_line(LMEvents *e, const char *line)
{
    if (!e->file || !line) return 0;
    size_t n = strlen(line);
    if (e->maxBytes > 0 && e->size + (long)n + 2 > e->maxBytes) {
        char old[320];
        old_name(old, sizeof old, e->path);
        fclose((FILE *)e->file); e->file = NULL;
        remove(old);
        if (rename(e->path, old) != 0) {   // (being read just now: keep appending, try again 256 KB later)
            e->file = fopen(e->path, "ab");
            if (!e->file) return 0;
            e->size = e->maxBytes > 262144 ? e->maxBytes - 262144 : 0;
        } else {
            e->file = fopen(e->path, "ab");
            if (!e->file) return 0;
            e->size = 0;
        }
    }
    if (fwrite(line, 1, n, (FILE *)e->file) != n || fwrite("\r\n", 1, 2, (FILE *)e->file) != 2) return 0;
    e->size += (long)n + 2;
    return 1;
}
void lm_events_flush(LMEvents *e) { if (e->file) fflush((FILE *)e->file); }
void lm_events_close(LMEvents *e) { if (e->file) { fclose((FILE *)e->file); e->file = NULL; } }
