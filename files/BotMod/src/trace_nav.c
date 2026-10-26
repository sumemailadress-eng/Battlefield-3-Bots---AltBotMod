// Caspian V2: Fun-Bots waypoint networks as a native route graph (see trace_nav.h).
#include "trace_nav.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TN_PROX_H     1.6f   // join different paths whose points lie this close (horizontal) ...
#define TN_PROX_V     1.2f   // ... and this close vertically: recorded paths that cross or touch
#define TN_GAP_MAX    9.0f   // consecutive points further apart than this are not walked as one edge
#define TN_LINK_MAX   30.0f  // a recorded link longer than this is ignored (bad data)

typedef struct RawPoint {
    int path, point; float p[3]; unsigned input; int vehicles; int nlinks; int links[16][2];
    unsigned obj; signed char team; unsigned char vehKind;   // AltBotMod V15: this point's "Objectives" (path labels)
    unsigned char act, onlyP; float yaw, pitch, time;           // V15: its "Action" (act = TN_ACT_* + 1, 0 = none)
} RawPoint;

static float h2(const float *a, const float *b) { float x = a[0] - b[0], z = a[2] - b[2]; return x * x + z * z; }
static float d3(const float *a, const float *b) { float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return sqrtf(x * x + y * y + z * z); }

// "Links":[[28,10],[3,4]] -> pairs (path, point)
static int parse_links(const char *s, const char *end, int out[16][2])
{
    const char *k = NULL;
    for (const char *q = s; q + 8 < end; q++) if (!strncmp(q, "\"Links\"", 7)) { k = q + 7; break; }
    if (!k) return 0;
    while (k < end && *k != '[') k++;
    if (k >= end) return 0;
    k++;   // into the outer list
    int n = 0;
    while (k < end && *k != ']' && n < 16) {
        while (k < end && *k != '[' && *k != ']') k++;
        if (k >= end || *k == ']') break;
        k++;
        char *e1; long a = strtol(k, &e1, 10); if (e1 == k) break;
        k = e1; while (k < end && (*k == ',' || *k == ' ')) k++;
        char *e2; long b = strtol(k, &e2, 10); if (e2 == k) break;
        k = e2; while (k < end && *k != ']') k++;
        if (k < end) k++;
        out[n][0] = (int)a; out[n][1] = (int)b; n++;
    }
    return n;
}

static int has_key(const char *s, const char *end, const char *key)
{
    size_t n = strlen(key);
    for (const char *q = s; q + n <= end; q++) if (!strncmp(q, key, n)) return 1;
    return 0;
}

// AltBotMod V15: the number after "key": in s..end (0 when absent)
static float num_after(const char *s, const char *end, const char *key, int *found)
{
    size_t n = strlen(key);
    for (const char *q = s; q + n < end; q++) if (!strncmp(q, key, n)) {
        q += n; while (q < end && (*q == ':' || *q == ' ')) q++;
        char *e; float v = strtof(q, &e);
        if (e != q && isfinite(v)) { if (found) *found = 1; return v; }
        break;
    }
    if (found) *found = 0;
    return 0;
}
static int contains(const char *s, size_t n, const char *word)
{
    size_t w = strlen(word);
    for (size_t k = 0; k + w <= n; k++) {
        size_t j = 0;
        while (j < w && (s[k + j] | 0x20) == (word[j] | 0x20)) j++;
        if (j == w) return 1;
    }
    return 0;
}
// "Objectives":["a","b"] / ["base us"] / ["vehicle tank1 ru"] -> label bits, team, vehicle kind
static void parse_objectives(const char *s, const char *end, RawPoint *r)
{
    const char *k = NULL;
    for (const char *q = s; q + 12 < end; q++) if (!strncmp(q, "\"Objectives\"", 12)) { k = q + 12; break; }
    r->team = -1;
    if (!k) return;
    while (k < end && *k != '[') k++;
    if (k >= end) return;
    k++;
    while (k < end && *k != ']') {
        while (k < end && *k != '"' && *k != ']') k++;
        if (k >= end || *k == ']') break;
        const char *a = ++k;
        while (k < end && *k != '"') k++;
        size_t n = (size_t)(k - a);
        if (k < end) k++;
        if (n == 1 && a[0] >= 'a' && a[0] <= 'p') r->obj |= 1u << (a[0] - 'a');
        else if (n >= 7 && !strncmp(a, "base us", 7)) r->obj |= TN_OBJ_BASE_US;
        else if (n >= 7 && !strncmp(a, "base ru", 7)) r->obj |= TN_OBJ_BASE_RU;
        else if (contains(a, n, "vehicle")) {
            r->vehKind = contains(a, n, "tank") ? TN_VEH_TANK :
                         (contains(a, n, "chopper") || contains(a, n, "heli") || contains(a, n, "viper") || contains(a, n, "havoc") ||
                          contains(a, n, "jet") || contains(a, n, "air") || contains(a, n, "venom") || contains(a, n, "littlebird")) ? TN_VEH_AIR : TN_VEH_OTHER;
            r->team = contains(a, n, "both") ? 0 : (n >= 3 && !strncmp(a + n - 3, " us", 3)) || contains(a, n, " us ") ? 1 :
                      (n >= 3 && !strncmp(a + n - 3, " ru", 3)) || contains(a, n, " ru ") ? 2 : 0;
        }
    }
}
// "Action":{"type":"vehicle","yaw":..,"pitch":..,"time":..,"onlyPassengers":true}
static void parse_action(const char *s, const char *end, RawPoint *r)
{
    const char *k = NULL;
    for (const char *q = s; q + 8 < end; q++) if (!strncmp(q, "\"Action\"", 8)) { k = q + 8; break; }
    if (!k) return;
    const char *t = NULL;
    for (const char *q = k; q + 7 < end; q++) if (!strncmp(q, "\"type\"", 6)) { t = q + 6; break; }
    int type = TN_ACT_OTHER;
    if (t) {
        while (t < end && *t != '"') t++;
        if (t < end) t++;
        if (t + 7 <= end && !strncmp(t, "vehicle\"", 8)) type = TN_ACT_VEHICLE;
        else if (t + 4 <= end && !strncmp(t, "exit\"", 5)) type = TN_ACT_EXIT;
        else if (t + 6 <= end && !strncmp(t, "beacon\"", 7)) type = TN_ACT_BEACON;
        else if (t + 4 <= end && !strncmp(t, "mcom\"", 5)) type = TN_ACT_MCOM;
    }
    r->act = (unsigned char)(type + 1);
    r->yaw = num_after(k, end, "\"yaw\"", NULL);
    r->pitch = num_after(k, end, "\"pitch\"", NULL);
    r->time = num_after(k, end, "\"time\"", NULL);
    r->onlyP = has_key(k, end, "\"onlyPassengers\":true") ? 1 : 0;
}

typedef struct EdgeTmp { int a, b; float c; } EdgeTmp;
static int edge_cmp(const void *x, const void *y)
{
    const EdgeTmp *a = x, *b = y;
    if (a->a != b->a) return a->a < b->a ? -1 : 1;
    return a->b < b->b ? -1 : a->b > b->b;
}
static int raw_cmp(const void *x, const void *y)
{
    const RawPoint *a = x, *b = y;
    if (a->path != b->path) return a->path < b->path ? -1 : 1;
    return a->point < b->point ? -1 : a->point > b->point;
}

void tn_free(TraceNet *t)
{
    if (!t) return;
    for (int f = 0; f < TN_MAX_FIELDS; f++) { free(t->field[f].dist); free(t->field[f].next); }
    free(t->nodes); free(t->edgeStart); free(t->edgeTo); free(t->edgeCost); free(t->actions);
    memset(t, 0, sizeof *t);
}

static int find_node(const TraceNet *t, int path, int point)
{
    int lo = 0, hi = t->count - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        const TNNode *n = &t->nodes[m];
        if (n->path == path && n->point == point) return m;
        if (n->path < path || (n->path == path && n->point < point)) lo = m + 1; else hi = m - 1;
    }
    return -1;
}

// AltBotMod V6: kind 0 = the infantry graph (vehicle paths left out), TN_KIND_LAND / TN_KIND_WATER = only the
// Fun-Bots vehicle paths recorded for land vehicles / boats ("Vehicles":["land"] / ["water"] on a path's point).
// Vehicle recordings are sparser and rarely touch exactly, so their crossings join within 6 m.
int tn_load_text_kind(TraceNet *t, const char *text, size_t length, int kind)
{
    memset(t, 0, sizeof *t);
    const float proxH = kind ? 6.0f : TN_PROX_H, proxV = kind ? 3.0f : TN_PROX_V, gapMax = kind ? 12.0f : TN_GAP_MAX;
    size_t cap = 1024; int nraw = 0;
    RawPoint *raw = malloc(cap * sizeof *raw);
    if (!raw) return 0;
    const char *s = text, *end = text + length;
    // Header line
    while (s < end && *s != '\n') s++;
    while (s < end) {
        const char *line = s; while (s < end && *s != '\n') s++;
        const char *lend = s; if (s < end) s++;
        if (lend - line < 9) continue;
        RawPoint r; memset(&r, 0, sizeof r);
        char *e; const char *q = line;
        r.path = (int)strtol(q, &e, 10); if (e == q || *e != ';') continue; q = e + 1;
        r.point = (int)strtol(q, &e, 10); if (e == q || *e != ';') continue; q = e + 1;
        int ok = 1;
        for (int k = 0; k < 3; k++) { r.p[k] = strtof(q, &e); if (e == q || *e != ';' || !isfinite(r.p[k])) { ok = 0; break; } q = e + 1; }
        if (!ok) continue;
        r.input = (unsigned)strtoul(q, &e, 10); if (e == q) continue; q = e;
        if (*q == ';') q++;
        if (q < lend && *q == '{') {
            if (has_key(q, lend, "\"Vehicles\"")) {
                r.vehicles = 4;   // (other / air)
                if (has_key(q, lend, "\"land\"")) r.vehicles |= TN_KIND_LAND;
                if (has_key(q, lend, "\"water\"")) r.vehicles |= TN_KIND_WATER;
            }
            r.nlinks = parse_links(q, lend, r.links);
            parse_objectives(q, lend, &r);   // AltBotMod V15
            parse_action(q, lend, &r);
        } else r.team = -1;
        if (nraw == (int)cap) { cap *= 2; RawPoint *g = realloc(raw, cap * sizeof *raw); if (!g) { free(raw); return 0; } raw = g; }
        raw[nraw++] = r;
    }
    if (nraw <= 0) { free(raw); return 0; }
    qsort(raw, (size_t)nraw, sizeof *raw, raw_cmp);
    // Vehicle-only paths: the flag sits on a path's first point.
    int maxPath = 0; for (int k = 0; k < nraw; k++) if (raw[k].path > maxPath) maxPath = raw[k].path;
    unsigned char *vehicle = calloc((size_t)maxPath + 1, 1);
    if (!vehicle) { free(raw); return 0; }
    for (int k = 0; k < nraw; k++) if (raw[k].vehicles && raw[k].path >= 0) vehicle[raw[k].path] |= (unsigned char)raw[k].vehicles;
    // AltBotMod V15: a path's labels sit on its first point (sometimes on others): every point of the path gets them
    unsigned *pathObj = calloc((size_t)maxPath + 1, sizeof *pathObj);
    signed char *pathTeam = malloc((size_t)maxPath + 1); unsigned char *pathKind = calloc((size_t)maxPath + 1, 1);
    if (!pathObj || !pathTeam || !pathKind) { free(pathObj); free(pathTeam); free(pathKind); free(vehicle); free(raw); return 0; }
    memset(pathTeam, -1, (size_t)maxPath + 1);
    int nact = 0;
    for (int k = 0; k < nraw; k++) {
        if (raw[k].act) nact++;
        if (raw[k].path < 0) continue;
        pathObj[raw[k].path] |= raw[k].obj;
        if (raw[k].vehKind) pathKind[raw[k].path] = raw[k].vehKind;
        if (raw[k].team >= 0) pathTeam[raw[k].path] = raw[k].team;
    }
    if (nact && (t->actions = malloc((size_t)nact * sizeof *t->actions))) {
        for (int k = 0; k < nraw; k++) {
            if (!raw[k].act || raw[k].path < 0) continue;
            TNAction *a = &t->actions[t->actionCount++];
            memcpy(a->p, raw[k].p, 12); a->yaw = raw[k].yaw; a->pitch = raw[k].pitch; a->time = raw[k].time;
            a->path = raw[k].path; a->point = raw[k].point; a->type = (unsigned char)(raw[k].act - 1); a->onlyPassengers = raw[k].onlyP;
            a->vehKind = pathKind[raw[k].path]; a->team = pathTeam[raw[k].path] < 0 ? 0 : pathTeam[raw[k].path];
        }
    }
    for (int p = 0; p <= maxPath; p++) if (pathObj[p] & 0xFFFFu) t->labelledPaths++;
    // skip[path]: not part of this graph
#define TN_SKIP(P) (kind ? !(vehicle[P] & kind) : vehicle[P] != 0)
    t->nodes = malloc((size_t)nraw * sizeof *t->nodes);
    if (!t->nodes) { free(pathObj); free(pathTeam); free(pathKind); free(vehicle); free(raw); tn_free(t); return 0; }
    int lastPath = -1;
    for (int k = 0; k < nraw; k++) {
        if (raw[k].path < 0) continue;
        if (TN_SKIP(raw[k].path)) { if (raw[k].path != lastPath && vehicle[raw[k].path]) t->vehiclePaths++; lastPath = raw[k].path; continue; }
        if (raw[k].path != lastPath) { t->pathCount++; lastPath = raw[k].path; }
        TNNode *n = &t->nodes[t->count++];
        memcpy(n->p, raw[k].p, 12); n->path = raw[k].path; n->point = raw[k].point;
        unsigned speed = raw[k].input & 0xF, extra = (raw[k].input >> 4) & 0xF;
        unsigned wait = speed == 0 ? (raw[k].input >> 8) & 0xFF : 0;   // V15: Fun-Bots OptValue - the wait in seconds
        if (wait > 15) wait = 15;
        n->flags = (unsigned char)((extra == 1 ? TN_FLAG_JUMP : 0) | (speed == 1 || speed == 2 ? TN_FLAG_CROUCH : 0) | (speed == 0 ? TN_FLAG_WAIT : 0) |
                                   (wait << TN_WAIT_SHIFT));
        n->obj = pathObj[raw[k].path];
    }
    free(pathObj); free(pathTeam); free(pathKind);
    if (!t->count) { free(vehicle); free(raw); tn_free(t); return 0; }
    // Edges: along each path, recorded links, and touching paths.
    size_t ecap = (size_t)t->count * 4 + 64; int ne = 0;
    EdgeTmp *edges = malloc(ecap * sizeof *edges);
    if (!edges) { free(vehicle); free(raw); tn_free(t); return 0; }
#define ADD_EDGE(A, B, C) do { if (ne + 2 > (int)ecap) { ecap *= 2; EdgeTmp *g_ = realloc(edges, ecap * sizeof *edges); if (!g_) goto fail; edges = g_; } \
        edges[ne].a = (A); edges[ne].b = (B); edges[ne].c = (C); ne++; edges[ne].a = (B); edges[ne].b = (A); edges[ne].c = (C); ne++; } while (0)
    for (int k = 0; k + 1 < t->count; k++) {
        TNNode *a = &t->nodes[k], *b = &t->nodes[k + 1];
        if (a->path != b->path) continue;
        float d = d3(a->p, b->p);
        if (d > gapMax) continue;
        ADD_EDGE(k, k + 1, d + fabsf(a->p[1] - b->p[1]) * 0.5f + (b->flags & TN_FLAG_CROUCH ? d * 0.5f : 0));
    }
    for (int k = 0; k < nraw; k++) {
        if (raw[k].path < 0 || TN_SKIP(raw[k].path)) continue;
        int from = -1;
        for (int l = 0; l < raw[k].nlinks; l++) {
            if (from < 0) from = find_node(t, raw[k].path, raw[k].point);
            int to = find_node(t, raw[k].links[l][0], raw[k].links[l][1]);
            if (from < 0 || to < 0 || to == from) { t->skippedLinks++; continue; }
            float d = d3(t->nodes[from].p, t->nodes[to].p);
            if (d > TN_LINK_MAX) { t->skippedLinks++; continue; }
            ADD_EDGE(from, to, d + 0.5f);
            t->linkCount++;
        }
    }
    {   // Touching paths: a 2 m spatial hash.
        float minx = 1e30f, minz = 1e30f, maxx = -1e30f, maxz = -1e30f;
        for (int k = 0; k < t->count; k++) { float *p = t->nodes[k].p; if (p[0] < minx) minx = p[0]; if (p[0] > maxx) maxx = p[0]; if (p[2] < minz) minz = p[2]; if (p[2] > maxz) maxz = p[2]; }
        const float cell = proxH > 2.0f ? proxH : 2.0f;
        int gw = (int)((maxx - minx) / cell) + 1, gh = (int)((maxz - minz) / cell) + 1;
        if (gw > 0 && gh > 0 && (long long)gw * gh < 64LL * 1024 * 1024) {
            int *head = malloc((size_t)gw * gh * sizeof *head), *nxt = malloc((size_t)t->count * sizeof *nxt);
            if (!head || !nxt) { free(head); free(nxt); goto fail; }
            for (int k = 0; k < gw * gh; k++) head[k] = -1;
            for (int k = 0; k < t->count; k++) {
                int cx = (int)((t->nodes[k].p[0] - minx) / cell), cz = (int)((t->nodes[k].p[2] - minz) / cell);
                nxt[k] = head[cz * gw + cx]; head[cz * gw + cx] = k;
            }
            for (int k = 0; k < t->count; k++) {
                TNNode *a = &t->nodes[k];
                int cx = (int)((a->p[0] - minx) / cell), cz = (int)((a->p[2] - minz) / cell);
                int best[8], nb = 0;
                for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
                    int x = cx + dx, z = cz + dz; if (x < 0 || z < 0 || x >= gw || z >= gh) continue;
                    for (int m = head[z * gw + x]; m >= 0; m = nxt[m]) {
                        if (m <= k) continue;
                        TNNode *b = &t->nodes[m];
                        if (b->path == a->path || fabsf(a->p[1] - b->p[1]) > proxV || h2(a->p, b->p) > proxH * proxH) continue;
                        int dup = 0; for (int q = 0; q < nb; q++) if (t->nodes[best[q]].path == b->path) dup = 1;
                        if (dup || nb >= 8) continue;   // one join per neighbouring path keeps the graph small
                        best[nb++] = m;
                    }
                }
                for (int q = 0; q < nb; q++) { ADD_EDGE(k, best[q], d3(a->p, t->nodes[best[q]].p) + 0.5f); t->proximityLinks++; }
            }
            free(head); free(nxt);
        }
    }
    qsort(edges, (size_t)ne, sizeof *edges, edge_cmp);
    t->edgeStart = calloc((size_t)t->count + 1, sizeof *t->edgeStart);
    t->edgeTo = malloc(((size_t)ne + 1) * sizeof *t->edgeTo);
    t->edgeCost = malloc(((size_t)ne + 1) * sizeof *t->edgeCost);
    if (!t->edgeStart || !t->edgeTo || !t->edgeCost) goto fail;
    int w = 0;
    for (int k = 0; k < ne; k++) {
        if (w && edges[k].a == edges[k - 1].a && edges[k].b == edges[k - 1].b) { if (edges[k].c < t->edgeCost[w - 1]) t->edgeCost[w - 1] = edges[k].c; continue; }
        t->edgeTo[w] = edges[k].b; t->edgeCost[w] = edges[k].c; t->edgeStart[edges[k].a + 1]++; w++;
    }
    for (int k = 0; k < t->count; k++) t->edgeStart[k + 1] += t->edgeStart[k];
    t->edgeCount = w;
    free(edges); free(vehicle); free(raw);
    return 1;
#undef TN_SKIP
fail:
    free(edges); free(vehicle); free(raw); tn_free(t);
    return 0;
#undef ADD_EDGE
}

int tn_load_text(TraceNet *t, const char *text, size_t length) { return tn_load_text_kind(t, text, length, 0); }
static char *read_all(const char *file, size_t *len)
{
    FILE *f = fopen(file, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0 || n > 64L * 1024 * 1024) { fclose(f); return NULL; }
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f); fclose(f);
    buf[got] = 0; *len = got;
    return buf;
}
static void set_name(TraceNet *t, const char *file)
{
    const char *base = strrchr(file, '\\'), *b2 = strrchr(file, '/');
    if (!base || (b2 && b2 > base)) base = b2;
    snprintf(t->name, sizeof t->name, "%s", base ? base + 1 : file);
}
static int max_path_of(const char *a, size_t n)
{
    int mx = -1;
    for (const char *s = a; s < a + n; ) {
        char *e; long v = strtol(s, &e, 10);
        if (e != s && *e == ';' && v > mx) mx = (int)v;
        while (s < a + n && *s != '\n') s++;
        if (s < a + n) s++;
    }
    return mx;
}
int tn_load_layers(TraceNet *t, const char *file, const char *companion, const char *learned, size_t learnedLength, int kind)
{
    memset(t, 0, sizeof *t);
    size_t n = 0, m = 0;
    char *a = read_all(file, &n);
    if (!a) return 0;
    char *b = companion && !kind ? read_all(companion, &m) : NULL;
    if (b) {   // built for this file? "Primary":"<name>.map","PrimaryBytes":<size> in its first 600 bytes
        const char *base = strrchr(file, '\\'), *b2 = strrchr(file, '/');
        if (!base || (b2 && b2 > base)) base = b2;
        base = base ? base + 1 : file;
        char want[160]; snprintf(want, sizeof want, "\"Primary\":\"%s\",\"PrimaryBytes\":%lu", base, (unsigned long)n);
        char head[601]; size_t hn = m < 600 ? m : 600; memcpy(head, b, hn); head[hn] = 0;
        if (!strstr(head, want)) { free(b); b = NULL; m = 0; }
    }
    int primaryMax = max_path_of(a, n);   // the primary's highest path number: companion paths are numbered after it
    // (learned paths after the companion's: lines "<path>;" rewritten with an offset)
    const char *lbody = learned && learnedLength ? memchr(learned, '\n', learnedLength) : NULL;
    size_t lcap = lbody ? (size_t)(learned + learnedLength - lbody) * 2 + 64 : 0;
    char *text = malloc(n + m + lcap + 4);
    if (!text) { free(a); free(b); return 0; }
    memcpy(text, a, n); size_t len = n;
    if (len && text[len - 1] != '\n') text[len++] = '\n';
    if (b) {
        const char *body = memchr(b, '\n', m);   // (its header line left out)
        if (body) { size_t k = (size_t)(b + m - (body + 1)); memcpy(text + len, body + 1, k); len += k; }
        if (len && text[len - 1] != '\n') text[len++] = '\n';
    }
    int learnedBase = max_path_of(text, len) + 1;
    if (lbody) {
        for (const char *s = lbody + 1, *end = learned + learnedLength; s < end; ) {
            const char *le = s; while (le < end && *le != '\n') le++;
            char *e; long v = strtol(s, &e, 10);
            if (e != s && *e == ';' && v >= 0 && le > e) {
                int w = snprintf(text + len, 24, "%ld", v + learnedBase);   // (lcap leaves room: twice the learned text)
                if (w > 0) len += (size_t)w;
                memcpy(text + len, e, (size_t)(le - e)); len += (size_t)(le - e);
                text[len++] = '\n';
            }
            s = le < end ? le + 1 : end;
        }
    }
    text[len] = 0;
    int ok = tn_load_text_kind(t, text, len, kind);
    free(text); free(a); free(b);
    if (!ok) return 0;
    for (int k = 0; k < t->count; k++) {
        if (lbody && t->nodes[k].path >= learnedBase) t->learnedNodes++;
        else if (t->nodes[k].path <= primaryMax) t->primaryNodes++; else t->companionNodes++;
    }
    set_name(t, file);
    return 1;
}
int tn_load_with_companion(TraceNet *t, const char *file, const char *companion) { return tn_load_layers(t, file, companion, NULL, 0, 0); }
int tn_load(TraceNet *t, const char *file) { return tn_load_kind(t, file, 0); }
int tn_load_kind(TraceNet *t, const char *file, int kind)
{
    memset(t, 0, sizeof *t);
    FILE *f = fopen(file, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return 0; }
    long n = ftell(f);
    if (n <= 0 || n > 64L * 1024 * 1024) { fclose(f); return 0; }
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return 0; }
    size_t got = fread(buf, 1, (size_t)n, f); fclose(f);
    buf[got] = 0;
    int ok = tn_load_text_kind(t, buf, got, kind);
    free(buf);
    if (ok) {
        const char *base = strrchr(file, '\\'), *b2 = strrchr(file, '/');
        if (!base || (b2 && b2 > base)) base = b2;
        snprintf(t->name, sizeof t->name, "%s", base ? base + 1 : file);
    }
    return ok;
}

// Binary heap of (cost, node)
typedef struct HeapItem { float c; int n; } HeapItem;
static void heap_push(HeapItem *h, int *n, float c, int node)
{
    int k = (*n)++; h[k].c = c; h[k].n = node;
    while (k) { int p = (k - 1) / 2; if (h[p].c <= h[k].c) break; HeapItem s = h[p]; h[p] = h[k]; h[k] = s; k = p; }
}
static HeapItem heap_pop(HeapItem *h, int *n)
{
    HeapItem top = h[0]; h[0] = h[--(*n)];
    int k = 0;
    for (;;) {
        int l = 2 * k + 1, r = l + 1, m = k;
        if (l < *n && h[l].c < h[m].c) m = l;
        if (r < *n && h[r].c < h[m].c) m = r;
        if (m == k) break;
        HeapItem s = h[m]; h[m] = h[k]; h[k] = s; k = m;
    }
    return top;
}

int tn_penalize_near(TraceNet *t, const float p[3], float radius, float factor)
{
    if (!t || !t->count || !t->edgeCost || !p || !(factor > 1)) return 0;
    int changed = 0; float r2 = radius * radius;
    for (int a = 0; a < t->count; a++)
        for (int e = t->edgeStart[a]; e < t->edgeStart[a + 1]; e++) {
            const float *u = t->nodes[a].p, *v = t->nodes[t->edgeTo[e]].p;
            float m[3] = {(u[0] + v[0]) * 0.5f, (u[1] + v[1]) * 0.5f, (u[2] + v[2]) * 0.5f};
            const float *q[3] = {u, v, m}; int hit = 0;
            for (int k = 0; k < 3 && !hit; k++) hit = h2(q[k], p) <= r2 && fabsf(q[k][1] - p[1]) < 6.0f;
            if (hit) { t->edgeCost[e] *= factor; changed++; }
        }
    if (changed) for (int k = 0; k < TN_MAX_FIELDS; k++) t->field[k].ready = 0;
    return changed;
}
int tn_field(TraceNet *t, int id, const float goal[3], float radius)
{
    if (!t || !t->count || id < 0 || id >= TN_MAX_FIELDS) return 0;
    TNField *f = &t->field[id];
    if (f->ready && h2(f->goal, goal) < 1.0f && fabsf(f->goal[1] - goal[1]) < 1.0f && f->radius == radius) return f->ready > 0;
    if (!f->dist) f->dist = malloc((size_t)t->count * sizeof *f->dist);
    if (!f->next) f->next = malloc((size_t)t->count * sizeof *f->next);
    if (!f->dist || !f->next) { f->ready = 0; return 0; }
    memcpy(f->goal, goal, 12); f->radius = radius;
    HeapItem *heap = malloc(((size_t)t->edgeCount + (size_t)t->count + 1) * sizeof *heap);
    if (!heap) { f->ready = 0; return 0; }
    int hn = 0, seeds = 0;
    unsigned prefer = id < TN_FLAG_FIELDS ? t->flagPrefer[id] : 0;   // V15: this flag's own labelled paths cost less
    for (int k = 0; k < t->count; k++) {
        f->dist[k] = INFINITY; f->next[k] = -1;
        if (h2(t->nodes[k].p, goal) <= radius * radius && fabsf(t->nodes[k].p[1] - goal[1]) < 12.0f) {
            f->dist[k] = sqrtf(h2(t->nodes[k].p, goal)); heap_push(heap, &hn, f->dist[k], k); seeds++;
        }
    }
    while (hn) {
        HeapItem it = heap_pop(heap, &hn);
        if (it.c > f->dist[it.n]) continue;
        for (int e = t->edgeStart[it.n]; e < t->edgeStart[it.n + 1]; e++) {
            int m = t->edgeTo[e]; float c = it.c + t->edgeCost[e] * (prefer && (t->nodes[m].obj & t->nodes[it.n].obj & prefer) ? 0.8f : 1.0f);   // edges are symmetric
            if (c < f->dist[m]) { f->dist[m] = c; f->next[m] = it.n; heap_push(heap, &hn, c, m); }
        }
    }
    free(heap);
    f->ready = seeds ? 1 : -1;
    return seeds > 0;
}

int tn_candidates(const TraceNet *t, int id, const float pos[3], float maxH, float maxV, int *out, int max)
{
    if (!t || id < 0 || id >= TN_MAX_FIELDS || t->field[id].ready <= 0 || max <= 0) return 0;
    const TNField *f = &t->field[id];
    float score[32]; int n = 0; if (max > 32) max = 32;
    for (int k = 0; k < t->count; k++) {
        if (!isfinite(f->dist[k]) || fabsf(t->nodes[k].p[1] - pos[1]) > maxV) continue;
        float d2 = h2(t->nodes[k].p, pos);
        if (d2 > maxH * maxH) continue;
        float s = sqrtf(d2) * 1.5f + f->dist[k];
        int j;
        if (n < max) j = n++;
        else { if (s >= score[max - 1]) continue; j = max - 1; }
        while (j > 0 && score[j - 1] > s) { score[j] = score[j - 1]; out[j] = out[j - 1]; j--; }
        score[j] = s; out[j] = k;
    }
    return n;
}

int tn_route(const TraceNet *t, int id, int start, float spacing, float *pts, unsigned char *flags, int maxPts, int *truncated, float *lengthOut)
{
    if (truncated) *truncated = 0;
    if (lengthOut) *lengthOut = 0;
    if (!t || id < 0 || id >= TN_MAX_FIELDS || start < 0 || start >= t->count || maxPts < 1) return 0;
    const TNField *f = &t->field[id];
    if (f->ready <= 0 || !isfinite(f->dist[start])) return 0;
    int n = 0, steps = 0, k = start, prev = -1;
    float last[3]; memcpy(last, t->nodes[k].p, 12);
    float length = 0;
    pts[0] = last[0]; pts[1] = last[1]; pts[2] = last[2]; flags[0] = t->nodes[k].flags; n = 1;
    while (f->next[k] >= 0 && steps++ < t->count) {
        int nx = f->next[k];
        length += d3(t->nodes[k].p, t->nodes[nx].p);
        prev = k; k = nx;
        const TNNode *node = &t->nodes[k];
        int end = f->next[k] < 0;
        float gap = sqrtf(h2(node->p, last)), rise = fabsf(node->p[1] - last[1]);
        int keep = end || gap >= spacing || rise >= 0.8f || (node->flags & (TN_FLAG_JUMP | TN_FLAG_CROUCH)) || TN_WAIT_SECONDS(node->flags);
        if (!keep && f->next[k] >= 0 && n >= 1) {   // keep corners: direction change > ~35 degrees
            const float *a = last, *b = node->p, *c = t->nodes[f->next[k]].p;
            float ux = b[0] - a[0], uz = b[2] - a[2], vx = c[0] - b[0], vz = c[2] - b[2];
            float lu = sqrtf(ux * ux + uz * uz), lv = sqrtf(vx * vx + vz * vz);
            if (lu > 0.5f && lv > 0.3f && (ux * vx + uz * vz) / (lu * lv) < 0.82f) keep = 1;
        }
        if (!keep) continue;
        if (n >= maxPts) { if (truncated) *truncated = 1; break; }
        pts[n * 3] = node->p[0]; pts[n * 3 + 1] = node->p[1]; pts[n * 3 + 2] = node->p[2]; flags[n] = node->flags;
        memcpy(last, node->p, 12); n++;
    }
    (void)prev;
    if (lengthOut) *lengthOut = length;
    return n;
}

// AltBotMod V15: Fun-Bots objective letters -> this level's flags (see trace_nav.h).
int tn_match_flags(TraceNet *t, const float (*flags)[3], int count, int *letterOut)
{
    if (!t || count <= 0) return 0;
    if (count > TN_FLAG_FIELDS) count = TN_FLAG_FIELDS;
    static int votes[16][TN_FLAG_FIELDS];
    memset(votes, 0, sizeof votes);
    for (int f = 0; f < count; f++) if (letterOut) letterOut[f] = -1;
    for (int k = 0; k < t->count; ) {
        int e = k; while (e + 1 < t->count && t->nodes[e + 1].path == t->nodes[k].path) e++;
        unsigned m = t->nodes[k].obj & 0xFFFFu;
        int letters = 0; for (int L = 0; L < 16; L++) if (m & (1u << L)) letters++;
        if (letters) {
            int ends[2] = {k, e};
            for (int q = 0; q < 2; q++) for (int f = 0; f < count; f++) {
                if (h2(t->nodes[ends[q]].p, flags[f]) > 80.0f * 80.0f) continue;
                for (int L = 0; L < 16; L++) if (m & (1u << L)) votes[L][f] += letters == 1 ? 2 : 1;
            }
        }
        k = e + 1;
    }
    int matched = 0, usedL[16] = {0}, usedF[TN_FLAG_FIELDS] = {0};
    memset(t->flagPrefer, 0, sizeof t->flagPrefer);
    for (;;) {
        int bl = -1, bf = -1, bv = 1;
        for (int L = 0; L < 16; L++) if (!usedL[L]) for (int f = 0; f < count; f++) if (!usedF[f] && votes[L][f] > bv) { bv = votes[L][f]; bl = L; bf = f; }
        if (bl < 0) break;
        usedL[bl] = usedF[bf] = 1;
        t->flagPrefer[bf] = 1u << bl;
        t->field[bf].ready = 0;   // (its route field is rebuilt with the preference)
        if (letterOut) letterOut[bf] = bl;
        matched++;
    }
    return matched;
}

int tn_action_near(const TraceNet *t, int type, const float pos[3], float maxH, float maxV)
{
    int best = -1; float bd = maxH * maxH;
    if (!t) return -1;
    for (int k = 0; k < t->actionCount; k++) {
        const TNAction *a = &t->actions[k];
        if (a->type != type || fabsf(a->p[1] - pos[1]) > maxV) continue;
        float d = h2(a->p, pos);
        if (d <= bd) { bd = d; best = k; }
    }
    return best;
}
