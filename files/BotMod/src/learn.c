// AltBotMod V21 - the learning store (see learn.h). Pure C, no game calls.
#include "learn.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *kKind[BL_KINDS] = {"foot", "ground", "boat", "air"};
const char *bl_kind_name(int kind) { return kind >= 0 && kind < BL_KINDS ? kKind[kind] : "?"; }
int bl_kind_parse(const char *s)
{
    for (int k = 0; k < BL_KINDS; k++) { size_t n = strlen(kKind[k]); if (!strncmp(s, kKind[k], n) && (s[n] == '\t' || s[n] == ' ' || !s[n] || s[n] == '\r' || s[n] == '\n')) return k; }
    return -1;
}
static int finite3(const float *p) { return isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]) && fabsf(p[0]) < 100000 && fabsf(p[1]) < 100000 && fabsf(p[2]) < 100000; }

void bl_init(BLStore *s, const char *key)
{
    memset(s, 0, sizeof *s);
    snprintf(s->key, sizeof s->key, "%s", key ? key : "");
}
void bl_free(BLStore *s)
{
    for (int k = 0; k < s->nExtra; k++) free(s->extra[k]);
    s->nExtra = 0;
}
static void clear_records(BLStore *s) { bl_free(s); s->nSpots = s->nHeat = s->nRoutes = s->nPads = 0; }

// ---- reading ----
static const char *next_line(const char *p, const char *end, const char **lineEnd)
{
    const char *q = p; while (q < end && *q != '\n') q++;
    *lineEnd = q; if (q > p && q[-1] == '\r') (*lineEnd)--;
    return q < end ? q + 1 : end;
}
static int parse_spot(BLStore *s, const char *a, const char *e)
{
    char buf[256]; size_t n = (size_t)(e - a); if (n >= sizeof buf) return 0;
    memcpy(buf, a, n); buf[n] = 0;
    char kind[16]; BLSpot sp; memset(&sp, 0, sizeof sp);
    long first = 0, last = 0;
    int got = sscanf(buf, "spot\t%15s\t%f %f %f\t%f %f\t%u %u %u %u\t%ld %ld", kind, &sp.p[0], &sp.p[1], &sp.p[2], &sp.h[0], &sp.h[1],
                     &sp.seen, &sp.passed, &sp.failed, &sp.jumps, &first, &last);
    if (got < 4) return 0;
    sp.kind = bl_kind_parse(kind);
    if (sp.kind < 0 || !finite3(sp.p) || s->nSpots >= BL_MAX_SPOTS) return 0;
    if (got < 6 || !isfinite(sp.h[0]) || !isfinite(sp.h[1])) sp.h[0] = sp.h[1] = 0;
    if (got < 7) sp.seen = 1;
    sp.first = first; sp.last = last ? last : first;
    sp.base[0] = sp.seen; sp.base[1] = sp.passed; sp.base[2] = sp.failed; sp.base[3] = sp.jumps;
    s->spots[s->nSpots++] = sp;
    return 1;
}
static int parse_heat(BLStore *s, const char *a, const char *e)
{
    char buf[96]; size_t n = (size_t)(e - a); if (n >= sizeof buf) return 0;
    memcpy(buf, a, n); buf[n] = 0;
    BLHeat h; memset(&h, 0, sizeof h);
    if (sscanf(buf, "heat\t%d %d\t%f", &h.cx, &h.cz, &h.deaths) != 3 || !isfinite(h.deaths) || h.deaths <= 0 || s->nHeat >= BL_MAX_HEAT) return 0;
    s->heat[s->nHeat++] = h;
    return 1;
}
static int parse_route(BLStore *s, const char *a, const char *e)
{
    if (s->nRoutes >= BL_MAX_ROUTES) return 0;
    const char *p = a + 6;   // after "route\t"
    int kind = bl_kind_parse(p);
    if (kind < 0 || kind == BL_AIR) return 0;
    while (p < e && *p != '\t') p++;
    if (p >= e) return 0;
    p++;
    BLRoute *r = &s->routes[s->nRoutes]; memset(r, 0, sizeof *r); r->kind = kind;
    char *q;
    r->uses = (unsigned)strtoul(p, &q, 10); if (q == p) return 0; p = q;
    r->made = strtol(p, &q, 10); p = q;
    while (p < e && *p != '\t') p++;
    if (p >= e) return 0;
    p++;
    while (p < e && r->n < BL_ROUTE_PTS) {
        float v[3]; int ok = 1;
        for (int k = 0; k < 3; k++) {
            v[k] = strtof(p, &q); if (q == p) { ok = 0; break; }
            p = q; if (k < 2) { if (p < e && *p == ',') p++; else { ok = 0; break; } }
        }
        if (!ok) break;
        if (finite3(v)) { memcpy(r->pts[r->n], v, 12); r->n++; }
        while (p < e && *p == ' ') p++;
    }
    if (r->n < 2) return 0;
    if (!r->uses) r->uses = 1;
    r->usesBase = r->uses;
    s->nRoutes++;
    return 1;
}
static int parse_pad(BLStore *s, const char *a, const char *e)
{
    char buf[160]; size_t n = (size_t)(e - a); if (n >= sizeof buf || s->nPads >= BL_MAX_PADS) return 0;
    memcpy(buf, a, n); buf[n] = 0;
    char kind[16]; BLPad pd; memset(&pd, 0, sizeof pd);
    int got = sscanf(buf, "pad\t%15s\t%f %f %f\t%u %u\t%ld %ld", kind, &pd.p[0], &pd.p[1], &pd.p[2], &pd.landed, &pd.failed, &pd.first, &pd.last);
    if (got < 4 || strcmp(kind, "heli") || !finite3(pd.p)) return 0;
    if (got < 5) pd.landed = 1;
    if (!pd.last) pd.last = pd.first;
    pd.base[0] = pd.landed; pd.base[1] = pd.failed;
    s->pads[s->nPads++] = pd;
    return 1;
}
int bl_load_text(BLStore *s, const char *text, size_t length)
{
    clear_records(s);
    s->loadedFormat = 0;
    if (!text) return -1;
    const char *p = text, *end = text + length, *le;
    if (length >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;   // (a BOM from an editor)
    const char *line = p; p = next_line(p, end, &le);
    if ((size_t)(le - line) < 17 || strncmp(line, "AltBotMod learned", 17)) return -1;
    s->loadedFormat = atoi(line + 18 <= le ? line + 18 : "1"); if (s->loadedFormat <= 0) s->loadedFormat = 1;
    int records = 0;
    while (p < end) {
        line = p; p = next_line(p, end, &le);
        size_t n = (size_t)(le - line);
        if (!n) continue;
        int ok = 0;
        if (n > 5 && !strncmp(line, "spot\t", 5)) ok = parse_spot(s, line, le);
        else if (n > 5 && !strncmp(line, "heat\t", 5)) ok = parse_heat(s, line, le);
        else if (n > 6 && !strncmp(line, "route\t", 6)) ok = parse_route(s, line, le);
        else if (n > 4 && !strncmp(line, "pad\t", 4)) ok = parse_pad(s, line, le);
        else if (n >= 23 && !strncmp(line, "# What the bots learned", 23)) continue;   // (our own header comment)
        else if (s->nExtra < BL_MAX_EXTRA) {   // unknown / comment: kept as it is
            char *c = malloc(n + 1);
            if (c) { memcpy(c, line, n); c[n] = 0; s->extra[s->nExtra++] = c; }
            continue;
        }
        records += ok;
    }
    s->dirty = 0;
    return records;
}

// ---- writing ----
size_t bl_text_size(const BLStore *s)
{
    size_t n = 200 + (size_t)s->nSpots * 160 + (size_t)s->nHeat * 48 + (size_t)s->nPads * 96;
    for (int k = 0; k < s->nRoutes; k++) n += 64 + (size_t)s->routes[k].n * 40;
    for (int k = 0; k < s->nExtra; k++) n += strlen(s->extra[k]) + 2;
    return n;
}
#define PUT(...) do { if (o < size) { int w = snprintf(buf + o, size - o, __VA_ARGS__); if (w > 0) o += (size_t)w; } } while (0)
size_t bl_write(const BLStore *s, char *buf, size_t size)
{
    size_t o = 0;
    if (!buf || !size) return 0;
    buf[0] = 0;
    PUT("AltBotMod learned\t%d\t%s\r\n", BL_FORMAT, s->key);
    PUT("# What the bots learned on this map. Delete a line (or use Server Control's Learning tab) to forget it.\r\n");
    for (int k = 0; k < s->nExtra; k++) {
        PUT("%s\r\n", s->extra[k]);
    }
    for (int k = 0; k < s->nSpots; k++) {
        const BLSpot *p = &s->spots[k];
        PUT("spot\t%s\t%.1f %.1f %.1f\t%.2f %.2f\t%u %u %u %u\t%ld %ld\r\n", bl_kind_name(p->kind), p->p[0], p->p[1], p->p[2], p->h[0], p->h[1],
            p->seen, p->passed, p->failed, p->jumps, p->first, p->last);
    }
    for (int k = 0; k < s->nHeat; k++) PUT("heat\t%d %d\t%.2f\r\n", s->heat[k].cx, s->heat[k].cz, s->heat[k].deaths);
    for (int k = 0; k < s->nRoutes; k++) {
        const BLRoute *r = &s->routes[k];
        PUT("route\t%s\t%u %ld\t", bl_kind_name(r->kind), r->uses, r->made);
        for (int j = 0; j < r->n; j++) PUT("%s%.1f,%.1f,%.1f", j ? " " : "", r->pts[j][0], r->pts[j][1], r->pts[j][2]);
        PUT("\r\n");
    }
    for (int k = 0; k < s->nPads; k++) {
        const BLPad *d = &s->pads[k];
        PUT("pad\theli\t%.1f %.1f %.1f\t%u %u\t%ld %ld\r\n", d->p[0], d->p[1], d->p[2], d->landed, d->failed, d->first, d->last);
    }
    if (o >= size) { buf[size - 1] = 0; return size - 1; }
    return o;
}
#undef PUT
void bl_mark_saved(BLStore *s)
{
    for (int k = 0; k < s->nSpots; k++) { BLSpot *p = &s->spots[k]; p->base[0] = p->seen; p->base[1] = p->passed; p->base[2] = p->failed; p->base[3] = p->jumps; }
    for (int k = 0; k < s->nHeat; k++) s->heat[k].fresh = 0;
    for (int k = 0; k < s->nRoutes; k++) { s->routes[k].usesBase = s->routes[k].uses; s->routes[k].isNew = 0; }
    for (int k = 0; k < s->nPads; k++) { s->pads[k].base[0] = s->pads[k].landed; s->pads[k].base[1] = s->pads[k].failed; s->pads[k].isNew = 0; }
    s->dirty = 0;
}
int bl_reload_keep_fresh(BLStore *s, const char *text, size_t length)
{
    // The session's learning since the last save: whole new records, and the increments on old ones (fresh counts).
    BLStore *keep = malloc(sizeof *keep);
    if (!keep) return -1;
    memcpy(keep, s, sizeof *keep);
    for (int k = 0; k < s->nExtra; k++) s->extra[k] = NULL;   // (owned by keep now; freed below)
    s->nExtra = 0;
    int r = bl_load_text(s, text, length);
    if (r < 0) { clear_records(s); }
    for (int k = 0; k < keep->nSpots; k++) {
        const BLSpot *o = &keep->spots[k];
        unsigned d[4] = {o->seen - o->base[0], o->passed - o->base[1], o->failed - o->base[2], o->jumps - o->base[3]};
        if (!d[0] && !d[1] && !d[2] && !d[3]) continue;   // nothing new here this session
        int j = bl_find(s, o->kind, o->p, o->h, o->kind == BL_FOOT ? 2.5f : o->kind == BL_AIR ? 60.0f : 8.0f);
        if (j < 0) {   // new this session, or deleted from the file meanwhile: only the new counts come back
            if (s->nSpots >= BL_MAX_SPOTS) continue;
            j = s->nSpots++; s->spots[j] = *o;
            s->spots[j].seen = d[0]; s->spots[j].passed = d[1]; s->spots[j].failed = d[2]; s->spots[j].jumps = d[3];
            memset(s->spots[j].base, 0, sizeof s->spots[j].base);
            if (!s->spots[j].seen) s->spots[j].seen = 1;
        } else {
            BLSpot *t = &s->spots[j];
            t->seen += d[0]; t->passed += d[1]; t->failed += d[2]; t->jumps += d[3]; if (o->last > t->last) t->last = o->last;
        }
    }
    for (int k = 0; k < keep->nHeat; k++) if (keep->heat[k].fresh) {
        float p[3] = {keep->heat[k].cx * 10.0f + 5, 0, keep->heat[k].cz * 10.0f + 5};
        for (unsigned d = 0; d < keep->heat[k].fresh; d++) bl_note_death(s, p);
    }
    for (int k = 0; k < keep->nRoutes; k++) {
        const BLRoute *o = &keep->routes[k];
        if (o->isNew) { if (s->nRoutes < BL_MAX_ROUTES) s->routes[s->nRoutes++] = *o; continue; }
        if (o->uses <= o->usesBase) continue;
        for (int j = 0; j < s->nRoutes; j++) {   // the same route on disk (same kind, ends and length): add the new uses
            BLRoute *t = &s->routes[j];
            if (t->kind == o->kind && t->n == o->n && fabsf(t->pts[0][0] - o->pts[0][0]) < 0.2f && fabsf(t->pts[0][2] - o->pts[0][2]) < 0.2f &&
                fabsf(t->pts[o->n - 1][0] - o->pts[o->n - 1][0]) < 0.2f) { t->uses += o->uses - o->usesBase; break; }
        }
    }
    for (int k = 0; k < keep->nPads; k++) {
        const BLPad *o = &keep->pads[k];
        unsigned dl = o->landed - o->base[0], df = o->failed - o->base[1];
        if (!dl && !df) continue;
        int j = -1; for (int m = 0; m < s->nPads && j < 0; m++) { float dx = s->pads[m].p[0] - o->p[0], dz = s->pads[m].p[2] - o->p[2]; if (dx * dx + dz * dz < 15 * 15) j = m; }
        if (j < 0) { if (s->nPads >= BL_MAX_PADS) continue; j = s->nPads++; s->pads[j] = *o; s->pads[j].landed = dl; s->pads[j].failed = df; memset(s->pads[j].base, 0, sizeof s->pads[j].base); }
        else { s->pads[j].landed += dl; s->pads[j].failed += df; if (o->last > s->pads[j].last) s->pads[j].last = o->last; }
    }
    for (int k = 0; k < keep->nExtra; k++) free(keep->extra[k]);
    free(keep);
    s->dirty = 1;
    return r;
}
void bl_fade(BLStore *s)
{
    int w = 0;
    for (int k = 0; k < s->nHeat; k++) {
        s->heat[k].deaths *= 0.8f;
        if (s->heat[k].deaths >= 0.3f) s->heat[w++] = s->heat[k];
    }
    if (w != s->nHeat) s->dirty = 1;
    s->nHeat = w;
}

// ---- stuck spots ----
static float kind_radius(int kind) { return kind == BL_FOOT ? 2.5f : kind == BL_AIR ? 60.0f : 8.0f; }
int bl_find(const BLStore *s, int kind, const float p[3], const float h[2], float radius)
{
    int best = -1; float bd = radius * radius;
    for (int k = 0; k < s->nSpots; k++) {
        const BLSpot *sp = &s->spots[k];
        if (sp->kind != kind) continue;
        float dx = sp->p[0] - p[0], dz = sp->p[2] - p[2], dy = sp->p[1] - p[1];
        float d = dx * dx + dz * dz;
        if (d > bd || fabsf(dy) > (kind == BL_AIR ? 80.0f : kind == BL_FOOT ? 2.0f : 4.0f)) continue;
        if (h && (h[0] || h[1]) && (sp->h[0] || sp->h[1]) && h[0] * sp->h[0] + h[1] * sp->h[1] < 0.5f) continue;
        best = k; bd = d;
    }
    return best;
}
int bl_note_stuck(BLStore *s, int kind, const float p[3], const float h[2], long now)
{
    if (kind < 0 || kind >= BL_KINDS || !p || !finite3(p)) return -1;
    float hh[2] = {0, 0};
    if (h && isfinite(h[0]) && isfinite(h[1])) { float l = sqrtf(h[0] * h[0] + h[1] * h[1]); if (l > 0.01f) { hh[0] = h[0] / l; hh[1] = h[1] / l; } }
    int k = bl_find(s, kind, p, hh, kind_radius(kind));
    if (k < 0) {
        if (s->nSpots >= BL_MAX_SPOTS) {   // full: the oldest spot nobody has been stuck at for longest goes
            int old = 0; for (int j = 1; j < s->nSpots; j++) if (s->spots[j].last < s->spots[old].last) old = j;
            s->spots[old] = s->spots[--s->nSpots];
        }
        k = s->nSpots++;
        BLSpot *sp = &s->spots[k]; memset(sp, 0, sizeof *sp);
        sp->kind = kind; memcpy(sp->p, p, 12); sp->h[0] = hh[0]; sp->h[1] = hh[1]; sp->first = now;
    } else {   // the spot drifts a little towards where bots really get stuck
        BLSpot *sp = &s->spots[k];
        for (int a = 0; a < 3; a++) sp->p[a] = sp->p[a] * 0.8f + p[a] * 0.2f;
        if (!sp->h[0] && !sp->h[1]) { sp->h[0] = hh[0]; sp->h[1] = hh[1]; }
    }
    BLSpot *sp = &s->spots[k];
    sp->seen++; sp->last = now;
    s->dirty = 1;
    return k;
}
void bl_note_result(BLStore *s, int spot, int passed, int jumped, long now)
{
    if (spot < 0 || spot >= s->nSpots) return;
    BLSpot *sp = &s->spots[spot];
    if (passed) { sp->passed++; if (jumped) sp->jumps++; } else sp->failed++;
    sp->last = now; s->dirty = 1;
}
int bl_spot_bad(const BLSpot *sp)
{
    if (sp->passed) return 0;
    return sp->kind == BL_FOOT ? sp->failed >= 3 : sp->kind == BL_AIR ? sp->failed >= 2 : sp->failed >= 2;
}
int bl_spot_passable(const BLSpot *sp) { return sp->passed >= 1 && sp->passed >= sp->failed; }
int bl_spot_jump(const BLSpot *sp) { return sp->passed >= 1 && sp->jumps * 2 >= sp->passed; }

// ---- deaths ----
static int cell_of(float v) { return (int)floorf(v / 10.0f); }
void bl_note_death(BLStore *s, const float p[3])
{
    if (!p || !finite3(p)) return;
    int cx = cell_of(p[0]), cz = cell_of(p[2]);
    for (int k = 0; k < s->nHeat; k++) if (s->heat[k].cx == cx && s->heat[k].cz == cz) { s->heat[k].deaths += 1; s->heat[k].fresh++; s->dirty = 1; return; }
    if (s->nHeat >= BL_MAX_HEAT) {   // full: the coldest cell goes
        int low = 0; for (int k = 1; k < s->nHeat; k++) if (s->heat[k].deaths < s->heat[low].deaths) low = k;
        s->heat[low] = s->heat[--s->nHeat];
    }
    BLHeat *h = &s->heat[s->nHeat++]; h->cx = cx; h->cz = cz; h->deaths = 1; h->fresh = 1;
    s->dirty = 1;
}
float bl_danger(const BLStore *s, float x, float z)
{
    int cx = cell_of(x), cz = cell_of(z); float sum = 0;
    for (int k = 0; k < s->nHeat; k++) {
        int dx = s->heat[k].cx - cx, dz = s->heat[k].cz - cz;
        if (dx < -1 || dx > 1 || dz < -1 || dz > 1) continue;
        sum += s->heat[k].deaths * (dx == 0 && dz == 0 ? 1.0f : 0.35f);
    }
    float d = sum / 12.0f;   // a dozen deaths around a cell: a killing ground
    return d > 1 ? 1 : d;
}

// ---- routes from human players ----
float bl_route_length(const BLRoute *r)
{
    float L = 0;
    for (int k = 1; k < r->n; k++) { float dx = r->pts[k][0] - r->pts[k - 1][0], dy = r->pts[k][1] - r->pts[k - 1][1], dz = r->pts[k][2] - r->pts[k - 1][2]; L += sqrtf(dx * dx + dy * dy + dz * dz); }
    return L;
}
static float near_route(const BLRoute *r, const float *p)
{
    float best = 1e30f;
    for (int k = 0; k < r->n; k++) { float dx = r->pts[k][0] - p[0], dz = r->pts[k][2] - p[2], dy = r->pts[k][1] - p[1]; float d = dx * dx + dz * dz + 4 * dy * dy; if (d < best) best = d; }
    return sqrtf(best);
}
int bl_add_route(BLStore *s, int kind, const float (*pts)[3], int n, long now)
{
    if (kind < 0 || kind == BL_AIR || kind >= BL_KINDS || !pts || n < 2) return -1;
    if (n > BL_ROUTE_PTS) n = BL_ROUTE_PTS;
    BLRoute tmp; memset(&tmp, 0, sizeof tmp); tmp.kind = kind;
    for (int k = 0; k < n; k++) if (finite3(pts[k])) memcpy(tmp.pts[tmp.n++], pts[k], 12);
    float minLen = kind == BL_FOOT ? 40.0f : kind == BL_BOAT ? 120.0f : 150.0f;
    if (tmp.n < 2 || bl_route_length(&tmp) < minLen) return -1;
    float same = kind == BL_FOOT ? 4.0f : 10.0f;
    for (int r = 0; r < s->nRoutes; r++) {   // mostly the way of one already learned: that one gets a use
        if (s->routes[r].kind != kind) continue;
        int close = 0; for (int k = 0; k < tmp.n; k++) if (near_route(&s->routes[r], tmp.pts[k]) < same) close++;
        if (close * 10 >= tmp.n * 7) { s->routes[r].uses++; s->dirty = 1; return -1; }
    }
    int perKind = kind == BL_FOOT ? 56 : kind == BL_GROUND ? 28 : 12, have = 0;
    for (int r = 0; r < s->nRoutes; r++) if (s->routes[r].kind == kind) have++;
    if (have >= perKind || s->nRoutes >= BL_MAX_ROUTES) {   // full: the least used, oldest route of this kind goes
        int drop = -1;
        for (int r = 0; r < s->nRoutes; r++) if (s->routes[r].kind == kind &&
            (drop < 0 || s->routes[r].uses < s->routes[drop].uses || (s->routes[r].uses == s->routes[drop].uses && s->routes[r].made < s->routes[drop].made))) drop = r;
        if (drop < 0) return -1;
        s->routes[drop] = s->routes[--s->nRoutes];
    }
    tmp.uses = 1; tmp.made = now; tmp.isNew = 1;
    s->routes[s->nRoutes] = tmp;
    s->dirty = 1;
    return s->nRoutes++;
}
#define PUT(...) do { int w = snprintf(buf ? buf + o : NULL, buf && o < size ? size - o : 0, __VA_ARGS__); if (w > 0) o += (size_t)w; } while (0)
size_t bl_routes_map_text(const BLStore *s, int kind, char *buf, size_t size)
{
    size_t o = 0;
    if (buf && size) buf[0] = 0;
    PUT("pathIndex;pointIndex;transX;transY;transZ;inputVar;data\n");
    int path = 0;
    float cut = kind == BL_FOOT ? 3.0f : 10.0f, cutY = kind == BL_FOOT ? 2.0f : 5.0f;
    for (int r = 0; r < s->nRoutes; r++) {
        const BLRoute *rt = &s->routes[r];
        if (rt->kind != kind) continue;
        // A spot of this kind nobody gets through (learned) breaks the route there: bots are not sent through a
        // place a player managed and they cannot. The pieces (2 points or more) stay.
        int point = 0;
        for (int k = 0; k < rt->n; k++) {
            int bad = 0;
            for (int j = 0; j < s->nSpots && !bad; j++) {
                const BLSpot *sp = &s->spots[j];
                if (sp->kind != kind || !bl_spot_bad(sp)) continue;
                float dx = sp->p[0] - rt->pts[k][0], dz = sp->p[2] - rt->pts[k][2];
                bad = dx * dx + dz * dz < cut * cut && fabsf(sp->p[1] - rt->pts[k][1]) < cutY;
            }
            if (bad) { if (point) { path++; point = 0; } continue; }
            if (kind == BL_FOOT) PUT("%d;%d;%.3f;%.3f;%.3f;3;\n", path, point, rt->pts[k][0], rt->pts[k][1], rt->pts[k][2]);
            else PUT("%d;%d;%.3f;%.3f;%.3f;3;{\"Vehicles\":[\"%s\"]}\n", path, point, rt->pts[k][0], rt->pts[k][1], rt->pts[k][2], kind == BL_BOAT ? "water" : "land");
            point++;
        }
        if (point) path++;
    }
    return o;
}
#undef PUT
// ---- helicopter landing spots ----
int bl_note_pad(BLStore *s, const float p[3], int landed, long now)
{
    if (!p || !finite3(p)) return -1;
    int j = -1; float best = 15 * 15;
    for (int k = 0; k < s->nPads; k++) { float dx = s->pads[k].p[0] - p[0], dz = s->pads[k].p[2] - p[2], d = dx * dx + dz * dz; if (d < best && fabsf(s->pads[k].p[1] - p[1]) < 6) { best = d; j = k; } }
    if (j < 0) {
        if (!landed) return -1;   // (a failed try somewhere never landed on teaches nothing)
        if (s->nPads >= BL_MAX_PADS) {   // full: the least proven pad goes
            int drop = 0; for (int k = 1; k < s->nPads; k++) if ((int)s->pads[k].landed - (int)s->pads[k].failed < (int)s->pads[drop].landed - (int)s->pads[drop].failed) drop = k;
            s->pads[drop] = s->pads[--s->nPads];
        }
        j = s->nPads++; memset(&s->pads[j], 0, sizeof s->pads[j]); memcpy(s->pads[j].p, p, 12); s->pads[j].first = now; s->pads[j].isNew = 1;
    } else if (landed) for (int a = 0; a < 3; a++) s->pads[j].p[a] = s->pads[j].p[a] * 0.8f + p[a] * 0.2f;
    if (landed) s->pads[j].landed++; else s->pads[j].failed++;
    s->pads[j].last = now; s->dirty = 1;
    return j;
}
int bl_pad_near(const BLStore *s, const float center[3], float radius, int nth, float out[3])
{
    int order[BL_MAX_PADS], n = 0;
    for (int k = 0; k < s->nPads; k++) {
        const BLPad *d = &s->pads[k];
        if (!d->landed || d->failed > d->landed) continue;
        float dx = d->p[0] - center[0], dz = d->p[2] - center[2];
        if (dx * dx + dz * dz > radius * radius) continue;
        int at = n++;   // insertion sort by distance
        while (at > 0) {
            const BLPad *e = &s->pads[order[at - 1]]; float ex = e->p[0] - center[0], ez = e->p[2] - center[2];
            if (ex * ex + ez * ez <= dx * dx + dz * dz) break;
            order[at] = order[at - 1]; at--;
        }
        order[at] = k;
    }
    if (nth < 0 || nth >= n) return 0;
    memcpy(out, s->pads[order[nth]].p, 12);
    return 1;
}
int bl_delete(BLStore *s, char type, int index)
{
    if (type == 'p' && index >= 0 && index < s->nPads) { s->pads[index] = s->pads[--s->nPads]; s->dirty = 1; return 1; }
    if (type == 's' && index >= 0 && index < s->nSpots) { s->spots[index] = s->spots[--s->nSpots]; s->dirty = 1; return 1; }
    if (type == 'h' && index >= 0 && index < s->nHeat) { s->heat[index] = s->heat[--s->nHeat]; s->dirty = 1; return 1; }
    if (type == 'r' && index >= 0 && index < s->nRoutes) { s->routes[index] = s->routes[--s->nRoutes]; s->dirty = 1; return 1; }
    return 0;
}
