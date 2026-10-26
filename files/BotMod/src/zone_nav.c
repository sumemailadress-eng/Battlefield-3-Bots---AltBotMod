// AltBotMod V34: Fun-Bots' objective walking mesh - see zone_nav.h.
#include "zone_nav.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ZN_MAX_POINTS   20000
#define ZN_MAX_EDGES    80000
#define ZN_MAX_CORNERS  32
#define ZN_MAX_ZONES    96
#define ZN_MAX_INSIDE   60000
#define ZN_REMOVE_AFTER 3

void zn_free(ZoneNet *z)
{
    if (!z) return;
    free(z->pts); free(z->edges); free(z->corners); free(z->zones); free(z->inside);
    free(z->adjStart); free(z->adj); free(z->g); free(z->from); free(z->heap); free(z->heapPos); free(z->seen);
    memset(z, 0, sizeof *z);
}

typedef struct Cur { const char *s; int bad; } Cur;
static void skip_ws(Cur *c) { while (*c->s == ' ' || *c->s == '\t' || *c->s == '\r' || *c->s == '\n') c->s++; }
static float rd_f(Cur *c)
{
    skip_ws(c); char *e; float v = strtof(c->s, &e);
    if (e == c->s || !isfinite(v)) { c->bad = 1; return 0; }
    c->s = e; return v;
}
static long rd_i(Cur *c)
{
    skip_ws(c); char *e; long v = strtol(c->s, &e, 10);
    if (e == c->s) { c->bad = 1; return 0; }
    c->s = e; return v;
}
static unsigned long rd_u(Cur *c)
{
    skip_ws(c); char *e; unsigned long v = strtoul(c->s, &e, 10);
    if (e == c->s) { c->bad = 1; return 0; }
    c->s = e; return v;
}
static int rd_word(Cur *c, char *out, int n)
{
    skip_ws(c); int k = 0;
    while (*c->s && *c->s != ' ' && *c->s != '\t' && *c->s != '\r' && *c->s != '\n') { if (k + 1 < n) out[k++] = *c->s; c->s++; }
    out[k] = 0;
    if (!k) c->bad = 1;
    return k;
}
static int expect(Cur *c, const char *word) { char w[32]; rd_word(c, w, sizeof w); if (strcmp(w, word)) c->bad = 1; return !c->bad; }

static void zn_parts(ZoneNet *z)
{
    int *stack = (int *)malloc(sizeof(int) * (size_t)(z->np ? z->np : 1));
    for (int k = 0; k < z->np; k++) z->pts[k].part = -1;
    int parts = 0;
    for (int s = 0; s < z->np && stack; s++) {
        if (z->pts[s].part >= 0) continue;
        int top = 0; stack[top++] = s; z->pts[s].part = parts;
        while (top) {
            int n = stack[--top];
            for (int a = z->adjStart[n]; a < z->adjStart[n + 1]; a++) {
                const ZnEdge *e = &z->edges[z->adj[a]]; int o = e->a == n ? e->b : e->a;
                if (z->pts[o].part < 0) { z->pts[o].part = parts; stack[top++] = o; }
            }
        }
        parts++;
    }
    free(stack);
    z->parts = parts;
}

int zn_load_text(ZoneNet *z, const char *text)
{
    memset(z, 0, sizeof *z);
    if (!text) return 0;
    Cur c = {text, 0};
    char w[64];
    if (!expect(&c, "AltBotMod") || !expect(&c, "navzones") || rd_i(&c) != 1 || c.bad) return 0;
    rd_word(&c, z->name, sizeof z->name);
    if (!expect(&c, "P")) return 0;
    long np = rd_i(&c); if (c.bad || np < 1 || np > ZN_MAX_POINTS) return 0;
    z->pts = (ZnPoint *)calloc((size_t)np, sizeof *z->pts); if (!z->pts) return 0;
    z->np = (int)np;
    for (int k = 0; k < z->np && !c.bad; k++) {
        z->pts[k].p[0] = rd_f(&c); z->pts[k].p[1] = rd_f(&c); z->pts[k].p[2] = rd_f(&c);
        z->pts[k].flags = (unsigned char)rd_i(&c);
    }
    if (c.bad || !expect(&c, "E")) { zn_free(z); return 0; }
    long ne = rd_i(&c); if (c.bad || ne < 0 || ne > ZN_MAX_EDGES) { zn_free(z); return 0; }
    z->edges = (ZnEdge *)calloc((size_t)(ne ? ne : 1), sizeof *z->edges);
    size_t cap = 1024; z->corners = (float *)malloc(cap * 3 * sizeof(float));
    if (!z->edges || !z->corners) { zn_free(z); return 0; }
    int kept = 0;
    for (long k = 0; k < ne && !c.bad; k++) {
        ZnEdge e; memset(&e, 0, sizeof e);
        e.a = (int)rd_i(&c); e.b = (int)rd_i(&c); e.len = rd_f(&c); e.oneway = (signed char)rd_i(&c);
        e.ncorner = (int)rd_i(&c); e.jumps = (unsigned)rd_u(&c);
        if (c.bad || e.ncorner < 0 || e.ncorner > ZN_MAX_CORNERS) { c.bad = 1; break; }
        e.corner0 = z->ncorners;
        if ((size_t)(z->ncorners + e.ncorner) > cap) {
            while ((size_t)(z->ncorners + e.ncorner) > cap) cap *= 2;
            float *m = (float *)realloc(z->corners, cap * 3 * sizeof(float)); if (!m) { c.bad = 1; break; }
            z->corners = m;
        }
        for (int q = 0; q < e.ncorner; q++) for (int d = 0; d < 3; d++) z->corners[(z->ncorners + q) * 3 + d] = rd_f(&c);
        if (c.bad) break;
        // (an edge naming a point that is not there, or a loop, is left out - never trusted)
        if (e.a < 0 || e.b < 0 || e.a >= z->np || e.b >= z->np || e.a == e.b || !(e.len >= 0) || e.oneway < -1 || e.oneway > 1) continue;
        if (e.len < 0.01f) { float dx = z->pts[e.a].p[0] - z->pts[e.b].p[0], dz = z->pts[e.a].p[2] - z->pts[e.b].p[2]; e.len = sqrtf(dx * dx + dz * dz); }
        z->ncorners += e.ncorner;
        z->edges[kept++] = e;
    }
    if (c.bad) { zn_free(z); return 0; }
    z->ne = kept;
    if (!expect(&c, "Z")) { zn_free(z); return 0; }
    long nz = rd_i(&c); if (c.bad || nz < 0 || nz > ZN_MAX_ZONES) { zn_free(z); return 0; }
    z->zones = (ZnZone *)calloc((size_t)(nz ? nz : 1), sizeof *z->zones);
    size_t icap = 256; z->inside = (int *)malloc(icap * sizeof(int));
    if (!z->zones || !z->inside) { zn_free(z); return 0; }
    for (long k = 0; k < nz && !c.bad; k++) {
        ZnZone *q = &z->zones[k];
        q->kind = (int)rd_i(&c); q->radius = rd_f(&c); q->c[0] = rd_f(&c); q->c[1] = rd_f(&c); q->c[2] = rd_f(&c);
        long n = rd_i(&c); rd_word(&c, w, sizeof w);
        if (c.bad || n < 0 || z->ninside + n > ZN_MAX_INSIDE) { c.bad = 1; break; }
        w[sizeof q->name - 1] = 0; memcpy(q->name, w, strlen(w) + 1);
        for (char *p = q->name; *p; p++) if (*p == '_') *p = ' ';
        q->inside0 = z->ninside;
        while ((size_t)(z->ninside + n) > icap) { icap *= 2; int *m = (int *)realloc(z->inside, icap * sizeof(int)); if (!m) { c.bad = 1; break; } z->inside = m; }
        for (long t = 0; t < n && !c.bad; t++) {
            long idx = rd_i(&c);
            if (idx >= 0 && idx < z->np) z->inside[z->ninside + q->ninside++] = (int)idx;
        }
        z->ninside += q->ninside;
    }
    if (c.bad) { zn_free(z); return 0; }
    z->nz = (int)nz;
    // adjacency (both directions; one-way is checked when walking it)
    z->adjStart = (int *)calloc((size_t)z->np + 1, sizeof(int));
    z->adj = (int *)malloc(sizeof(int) * (size_t)(z->ne * 2 + 1));
    z->g = (float *)malloc(sizeof(float) * (size_t)z->np);
    z->from = (int *)malloc(sizeof(int) * (size_t)z->np);
    z->heap = (int *)malloc(sizeof(int) * (size_t)z->np);
    z->heapPos = (int *)malloc(sizeof(int) * (size_t)z->np);
    z->seen = (unsigned *)calloc((size_t)z->np, sizeof(unsigned));
    if (!z->adjStart || !z->adj || !z->g || !z->from || !z->heap || !z->heapPos || !z->seen) { zn_free(z); return 0; }
    for (int k = 0; k < z->ne; k++) { z->adjStart[z->edges[k].a + 1]++; z->adjStart[z->edges[k].b + 1]++; }
    for (int k = 0; k < z->np; k++) z->adjStart[k + 1] += z->adjStart[k];
    int *fill = (int *)malloc(sizeof(int) * (size_t)z->np);
    if (!fill) { zn_free(z); return 0; }
    memcpy(fill, z->adjStart, sizeof(int) * (size_t)z->np);
    for (int k = 0; k < z->ne; k++) { z->adj[fill[z->edges[k].a]++] = k; z->adj[fill[z->edges[k].b]++] = k; }
    free(fill);
    zn_parts(z);
    return 1;
}

int zn_load(ZoneNet *z, const char *file)
{
    memset(z, 0, sizeof *z);
    FILE *f = fopen(file, "rb"); if (!f) return 0;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return 0; }
    long n = ftell(f);
    if (n <= 0 || n > 64L * 1024 * 1024 || fseek(f, 0, SEEK_SET)) { fclose(f); return 0; }
    char *buf = (char *)malloc((size_t)n + 1); if (!buf) { fclose(f); return 0; }
    size_t got = fread(buf, 1, (size_t)n, f); fclose(f);
    buf[got] = 0;
    int ok = got == (size_t)n && zn_load_text(z, buf);
    free(buf);
    return ok;
}

int zn_closest(const ZoneNet *z, const float *pos, float maxH, float maxV)
{
    if (!z || !z->np || !pos) return -1;
    int best = -1, bestFloor = 0; float bestD = 0;
    for (int k = 0; k < z->np; k++) {
        const float *p = z->pts[k].p;
        float dy = fabsf(p[1] - pos[1]); if (dy > maxV) continue;
        float dx = p[0] - pos[0], dz = p[2] - pos[2], d = dx * dx + dz * dz;
        if (d > maxH * maxH) continue;
        int floor = dy <= 1.5f;   // a point on the same floor beats a closer one on the floor above / below
        if (best < 0 || floor > bestFloor || (floor == bestFloor && d < bestD)) { best = k; bestD = d; bestFloor = floor; }
    }
    return best;
}

int zn_zone_near(const ZoneNet *z, const float *pos, int kind, float maxDist)
{
    if (!z || !pos) return -1;
    int best = -1; float bestD = maxDist * maxDist;
    for (int k = 0; k < z->nz; k++) {
        if (kind && z->zones[k].kind != kind) continue;
        float dx = z->zones[k].c[0] - pos[0], dz = z->zones[k].c[2] - pos[2], d = dx * dx + dz * dz;
        if (d <= bestD) { bestD = d; best = k; }
    }
    return best;
}

int zn_zone_point(const ZoneNet *z, int zone, unsigned seed)
{
    if (!z || zone < 0 || zone >= z->nz || !z->zones[zone].ninside) return -1;
    return z->inside[z->zones[zone].inside0 + (int)(seed % (unsigned)z->zones[zone].ninside)];
}

// ---- A* (binary heap keyed by g + h) ----
static float zn_h(const ZoneNet *z, int a, int b)
{
    const float *p = z->pts[a].p, *q = z->pts[b].p;
    float dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}
static float *g_znF;   // (the f values of the search in progress)
static void heap_swap(ZoneNet *z, int i, int j) { int t = z->heap[i]; z->heap[i] = z->heap[j]; z->heap[j] = t; z->heapPos[z->heap[i]] = i; z->heapPos[z->heap[j]] = j; }
static void heap_up(ZoneNet *z, int i) { while (i > 0) { int p = (i - 1) / 2; if (g_znF[z->heap[p]] <= g_znF[z->heap[i]]) break; heap_swap(z, i, p); i = p; } }
static void heap_down(ZoneNet *z, int n, int i)
{
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < n && g_znF[z->heap[l]] < g_znF[z->heap[m]]) m = l;
        if (r < n && g_znF[z->heap[r]] < g_znF[z->heap[m]]) m = r;
        if (m == i) return;
        heap_swap(z, i, m); i = m;
    }
}
static int zn_edge_between(const ZoneNet *z, int u, int v, float *cost)
{
    int best = -1; float bc = 1e30f;
    for (int a = z->adjStart[u]; a < z->adjStart[u + 1]; a++) {
        const ZnEdge *e = &z->edges[z->adj[a]];
        if (e->removed) continue;
        int o = e->a == u ? e->b : e->a;
        if (o != v) continue;
        if ((e->a == u && e->oneway == -1) || (e->b == u && e->oneway == 1)) continue;
        float c = e->len + e->penalty;
        if (c < bc) { bc = c; best = z->adj[a]; }
    }
    if (cost) *cost = bc;
    return best;
}
int zn_route(ZoneNet *z, int a, int b, float *out, unsigned char *flags, int maxPts, int *partial, float *length)
{
    if (partial) *partial = 0;
    if (length) *length = 0;
    if (!z || !z->np || a < 0 || b < 0 || a >= z->np || b >= z->np || maxPts < 1 || !out) return 0;
    if (z->pts[a].part != z->pts[b].part) return 0;
    if (a == b) { memcpy(out, z->pts[b].p, 12); if (flags) flags[0] = (z->pts[b].flags & ZN_PT_CROUCH) ? ZN_ROUTE_CROUCH : 0; return 1; }
    static float *fbuf; static int fcap;
    if (fcap < z->np) { free(fbuf); fbuf = (float *)malloc(sizeof(float) * (size_t)z->np); fcap = fbuf ? z->np : 0; if (!fbuf) return 0; }
    g_znF = fbuf;
    if (++z->stamp == 0) { memset(z->seen, 0, sizeof(unsigned) * (size_t)z->np); z->stamp = 1; }
    unsigned closedTag = 0x80000000u;   // seen[n] == stamp: open / known; seen[n] == (stamp | closed): done
    unsigned st = z->stamp & 0x7fffffffu; if (!st) st = 1;
    int n = 0;
    z->seen[a] = st; z->g[a] = 0; z->from[a] = -1; fbuf[a] = zn_h(z, a, b);
    z->heap[n] = a; z->heapPos[a] = n; n++;
    int found = 0;
    while (n) {
        int u = z->heap[0];
        heap_swap(z, 0, n - 1); n--; heap_down(z, n, 0);
        z->seen[u] = st | closedTag;
        if (u == b) { found = 1; break; }
        for (int k = z->adjStart[u]; k < z->adjStart[u + 1]; k++) {
            const ZnEdge *e = &z->edges[z->adj[k]];
            if (e->removed) continue;
            int v = e->a == u ? e->b : e->a;
            if ((e->a == u && e->oneway == -1) || (e->b == u && e->oneway == 1)) continue;
            if (z->seen[v] == (st | closedTag)) continue;
            float gv = z->g[u] + e->len + e->penalty;
            if (z->seen[v] != st) { z->seen[v] = st; z->g[v] = gv; z->from[v] = u; fbuf[v] = gv + zn_h(z, v, b); z->heap[n] = v; z->heapPos[v] = n; n++; heap_up(z, n - 1); }
            else if (gv < z->g[v]) { z->g[v] = gv; z->from[v] = u; fbuf[v] = gv + zn_h(z, v, b); heap_up(z, z->heapPos[v]); }
        }
    }
    if (!found) return 0;
    // the point chain b <- ... <- a, reversed
    int chainN = 0;
    for (int v = b; v >= 0 && chainN <= z->np; v = z->from[v]) chainN++;
    int *chain = (int *)malloc(sizeof(int) * (size_t)chainN); if (!chain) return 0;
    { int k = chainN; for (int v = b; v >= 0 && k > 0; v = z->from[v]) chain[--k] = v; }
    int cnt = 0; float len = 0, last[3]; memcpy(last, z->pts[a].p, 12);
    for (int k = 1; k < chainN && cnt < maxPts; k++) {
        int u = chain[k - 1], v = chain[k];
        int ei = zn_edge_between(z, u, v, NULL);
        if (ei >= 0) {
            const ZnEdge *e = &z->edges[ei]; int fwd = e->a == u;
            for (int q = 0; q < e->ncorner && cnt < maxPts; q++) {
                int ci = fwd ? q : e->ncorner - 1 - q;
                const float *cp = &z->corners[(e->corner0 + ci) * 3];
                float dx = cp[0] - last[0], dy = cp[1] - last[1], dz = cp[2] - last[2], d = sqrtf(dx * dx + dy * dy + dz * dz);
                if (d < 0.3f) continue;
                memcpy(&out[cnt * 3], cp, 12);
                if (flags) flags[cnt] = (ci < 32 && (e->jumps >> ci) & 1u) ? ZN_ROUTE_JUMP : 0;
                cnt++; len += d; memcpy(last, cp, 12);
            }
        }
        if (cnt >= maxPts) break;
        const float *vp = z->pts[v].p;
        float dx = vp[0] - last[0], dy = vp[1] - last[1], dz = vp[2] - last[2], d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d < 0.3f && k + 1 < chainN) continue;
        memcpy(&out[cnt * 3], vp, 12);
        if (flags) flags[cnt] = (z->pts[v].flags & ZN_PT_CROUCH) ? ZN_ROUTE_CROUCH : 0;
        cnt++; len += d; memcpy(last, vp, 12);
    }
    int cut = cnt >= maxPts && (out[(cnt - 1) * 3] != z->pts[b].p[0] || out[(cnt - 1) * 3 + 2] != z->pts[b].p[2]);
    free(chain);
    if (partial) *partial = cut;
    if (length) *length = len;
    return cnt;
}

static float seg_dist2(const float *p, const float *a, const float *b)
{
    float abx = b[0] - a[0], abz = b[2] - a[2], apx = p[0] - a[0], apz = p[2] - a[2], l = abx * abx + abz * abz;
    float t = l > 1e-6f ? (apx * abx + apz * abz) / l : 0; if (t < 0) t = 0; if (t > 1) t = 1;
    float dx = apx - abx * t, dz = apz - abz * t;
    return dx * dx + dz * dz;
}
int zn_penalize(ZoneNet *z, const float *pos, float amount)
{
    if (!z || !pos) return -1;
    int best = -1; float bestD = 3.0f * 3.0f;
    for (int k = 0; k < z->ne; k++) {
        ZnEdge *e = &z->edges[k];
        if (e->removed) continue;
        const float *prev = z->pts[e->a].p;
        if (fabsf(prev[1] - pos[1]) > 3.0f && fabsf(z->pts[e->b].p[1] - pos[1]) > 3.0f) continue;
        for (int q = 0; q <= e->ncorner; q++) {
            const float *nx = q < e->ncorner ? &z->corners[(e->corner0 + q) * 3] : z->pts[e->b].p;
            float d = seg_dist2(pos, prev, nx);
            if (d < bestD) { bestD = d; best = k; }
            prev = nx;
        }
    }
    if (best >= 0) {
        ZnEdge *e = &z->edges[best];
        e->penalty += amount;
        if (++e->stuck >= ZN_REMOVE_AFTER) e->removed = 1;
    }
    return best;
}
