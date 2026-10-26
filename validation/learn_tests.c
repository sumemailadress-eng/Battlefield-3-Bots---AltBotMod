// AltBotMod V21: the learning store (learn.c) - format round trip, merging, judgement rules, an outside edit (Server
// Control deleting lines) while the server keeps learning, forward compatibility, routes, danger.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../files/BotMod/src/learn.h"
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static BLStore A, B;
static char *text_of(const BLStore *s, size_t *n)
{
    size_t cap = bl_text_size(s); char *b = malloc(cap); *n = bl_write(s, b, cap); return b;
}
int main(void)
{
    bl_init(&A, "MP_001_ConquestLarge0");
    float p[3] = {100, 20, -50}, h[2] = {1, 0}, q[3] = {101.5f, 20.3f, -50.5f};
    // merging: a second stuck event 1.6 m away, same heading = the same spot; the other way round = another spot
    int s1 = bl_note_stuck(&A, BL_FOOT, p, h, 1000);
    CHECK(s1 == 0 && bl_note_stuck(&A, BL_FOOT, q, h, 1001) == 0 && A.spots[0].seen == 2);
    float back[2] = {-1, 0};
    CHECK(bl_note_stuck(&A, BL_FOOT, q, back, 1002) == 1 && A.nSpots == 2);
    // ground spots merge within 8 m, air within 60 m; kinds never merge
    float g[3] = {300, 5, 300}, g2[3] = {306, 5, 302};
    int gs = bl_note_stuck(&A, BL_GROUND, g, NULL, 1003);
    CHECK(bl_note_stuck(&A, BL_GROUND, g2, NULL, 1004) == gs && bl_note_stuck(&A, BL_BOAT, g2, NULL, 1005) != gs);
    float air[3] = {0, 400, 0}, air2[3] = {40, 420, 10};
    int as = bl_note_stuck(&A, BL_AIR, air, NULL, 1006);
    CHECK(bl_note_stuck(&A, BL_AIR, air2, NULL, 1007) == as);
    // judgement: three fails, never through = bad (foot); one pass makes it passable, never bad
    for (int k = 0; k < 3; k++) bl_note_result(&A, s1, 0, 0, 1010);
    CHECK(bl_spot_bad(&A.spots[s1]) && !bl_spot_passable(&A.spots[s1]));
    bl_note_result(&A, s1, 1, 1, 1011);
    CHECK(!bl_spot_bad(&A.spots[s1]) && !bl_spot_passable(&A.spots[s1]));   // 1 pass vs 3 fails: not yet "passable"
    bl_note_result(&A, s1, 1, 1, 1012); bl_note_result(&A, s1, 1, 0, 1013);
    CHECK(bl_spot_passable(&A.spots[s1]) && bl_spot_jump(&A.spots[s1]));     // 3 passes, 2 with a jump
    bl_note_result(&A, gs, 0, 0, 1014); bl_note_result(&A, gs, 0, 0, 1015);
    CHECK(bl_spot_bad(&A.spots[gs]));
    // deaths / danger
    float d[3] = {55, 0, 55};
    for (int k = 0; k < 12; k++) bl_note_death(&A, d);
    CHECK(bl_danger(&A, 55, 55) > 0.99f && bl_danger(&A, 500, 500) == 0 && bl_danger(&A, 65, 55) > 0.3f);
    // routes: a short walk is ignored, a 60 m walk is learned, walking it again is a use, not a copy
    float walk[30][3];
    for (int k = 0; k < 30; k++) { walk[k][0] = k * 3.0f; walk[k][1] = 10; walk[k][2] = 0; }
    CHECK(bl_add_route(&A, BL_FOOT, (const float (*)[3])walk, 5, 1020) < 0);
    int r = bl_add_route(&A, BL_FOOT, (const float (*)[3])walk, 30, 1021);
    CHECK(r == 0 && A.nRoutes == 1);
    for (int k = 0; k < 30; k++) walk[k][2] = 1.5f;
    CHECK(bl_add_route(&A, BL_FOOT, (const float (*)[3])walk, 30, 1022) < 0 && A.routes[0].uses == 2);
    float drive[40][3];
    for (int k = 0; k < 40; k++) { drive[k][0] = 0; drive[k][1] = 5; drive[k][2] = k * 10.0f; }
    CHECK(bl_add_route(&A, BL_GROUND, (const float (*)[3])drive, 40, 1023) == 1);
    CHECK(bl_add_route(&A, BL_AIR, (const float (*)[3])drive, 40, 1023) < 0);   // no air routes
    // routes as Fun-Bots lines: foot paths plain, vehicle paths tagged for the land / water graphs
    char mt[20000];
    size_t mn = bl_routes_map_text(&A, BL_FOOT, mt, sizeof mt);
    CHECK(mn > 0 && strstr(mt, "0;29;87.000;10.000;0.000;3;") && !strstr(mt, "Vehicles"));
    CHECK(bl_routes_map_text(&A, BL_GROUND, NULL, 0) > 0);
    bl_routes_map_text(&A, BL_GROUND, mt, sizeof mt);
    CHECK(strstr(mt, "{\"Vehicles\":[\"land\"]}") != NULL);
    // round trip, with an unknown record from a later version and a comment kept
    size_t n; char *t = text_of(&A, &n);
    char *t2 = malloc(n + 200); memcpy(t2, t, n); strcpy(t2 + n, "future\tsomething a later version writes\r\n# my note\r\n");
    bl_init(&B, "x");
    CHECK(bl_load_text(&B, t2, strlen(t2)) > 0);
    CHECK(B.nSpots == A.nSpots && B.nHeat == A.nHeat && B.nRoutes == A.nRoutes && B.nExtra == 2);
    CHECK(B.spots[s1].passed == 3 && B.spots[s1].jumps == 2 && B.routes[0].uses == 2 && B.routes[1].n == 40);
    size_t n2; char *t3 = text_of(&B, &n2);
    CHECK(strstr(t3, "future\tsomething a later version writes") && strstr(t3, "# my note"));
    CHECK(bl_load_text(&B, "not a learned file\n", 19) < 0);
    // a format-1 reader accepts a later format number and fields it does not have (missing ones default)
    const char *v2 = "AltBotMod learned\t2\tX\r\nspot\tfoot\t1 2 3\r\nspot\tair\t5 400 5\t0 0\t1 0 2 0\t10 20\textra\r\nheat\t1 1\t3\r\n";
    CHECK(bl_load_text(&B, v2, strlen(v2)) == 3 && B.loadedFormat == 2 && B.nSpots == 2 && B.spots[0].seen == 1 && B.spots[1].failed == 2);
    // outside edit while learning: Server Control deletes the ground spot and the route; the server meanwhile got
    // stuck at a new spot and once more at s1. The reload keeps the deletions and the session's new learning.
    bl_mark_saved(&A);
    float nw[3] = {-200, 30, 40};
    bl_note_stuck(&A, BL_FOOT, nw, h, 1100);
    bl_note_result(&A, s1, 1, 0, 1101);
    bl_init(&B, A.key); CHECK(bl_load_text(&B, t, n) > 0);
    CHECK(bl_delete(&B, 's', gs) && bl_delete(&B, 'r', 1));
    size_t ne; char *edited = text_of(&B, &ne);
    int keptS1Passed = 0;
    CHECK(bl_reload_keep_fresh(&A, edited, ne) > 0);
    CHECK(bl_find(&A, BL_GROUND, g, NULL, 8) < 0);                         // the deletion stands
    CHECK(bl_find(&A, BL_FOOT, nw, h, 2.5f) >= 0);                         // the new spot is kept
    int j = bl_find(&A, BL_FOOT, p, h, 2.5f); CHECK(j >= 0); keptS1Passed = A.spots[j].passed;
    CHECK(keptS1Passed == 4);                                              // 3 on disk + 1 this session
    int ground = 0; for (int k = 0; k < A.nRoutes; k++) if (A.routes[k].kind == BL_GROUND) ground++;
    CHECK(ground == 0 && A.dirty);
    // fading: deaths x0.8 per load; spots never fade
    bl_init(&B, "f"); for (int k = 0; k < 2; k++) bl_note_death(&B, d);
    float d2[3] = {900, 0, 900}; bl_note_death(&B, d2);
    for (int k = 0; k < 7; k++) bl_fade(&B);
    CHECK(B.nHeat == 1 && fabsf(B.heat[0].deaths - 2 * powf(0.8f, 7)) < 0.01f);
    // the store is bounded: thousands of spots / deaths / routes never overflow
    bl_init(&B, "big");
    for (int k = 0; k < 3000; k++) { float x[3] = {k * 10.0f, 0, k * 7.0f}; bl_note_stuck(&B, k % 4, x, h, k); bl_note_death(&B, x); }
    CHECK(B.nSpots <= BL_MAX_SPOTS && B.nHeat <= BL_MAX_HEAT);
    for (int k = 0; k < 200; k++) { float w2[30][3]; for (int m = 0; m < 30; m++) { w2[m][0] = m * 3.0f; w2[m][1] = 0; w2[m][2] = k * 50.0f; } bl_add_route(&B, BL_FOOT, (const float (*)[3])w2, 30, k); }
    CHECK(B.nRoutes <= 56);
    size_t nb; char *tb = text_of(&B, &nb); CHECK(nb > 0 && nb < bl_text_size(&B));
    bl_init(&A, "rt"); CHECK(bl_load_text(&A, tb, nb) > 0 && A.nSpots == B.nSpots && A.nRoutes == B.nRoutes);
    // V21: helicopter landing spots - merge within 15 m, closest first, a spot found blocked more than used is skipped,
    // a blocked try where nobody ever landed teaches nothing, round trip, kept through an outside edit
    bl_init(&B, "pads");
    float c[3] = {0, 10, 0}, pa[3] = {50, 10, 0}, pb[3] = {58, 10, 6}, pc[3] = {150, 12, 0}, pz[3] = {400, 0, 0}, out[3];
    CHECK(bl_note_pad(&B, pa, 1, 10) == 0 && bl_note_pad(&B, pb, 1, 11) == 0 && B.pads[0].landed == 2);
    CHECK(bl_note_pad(&B, pc, 1, 12) == 1 && bl_note_pad(&B, pz, 0, 13) == -1 && B.nPads == 2);
    CHECK(bl_pad_near(&B, c, 220, 0, out) && out[0] < 60 && bl_pad_near(&B, c, 220, 1, out) && out[0] > 140 && !bl_pad_near(&B, c, 220, 2, out));
    CHECK(!bl_pad_near(&B, c, 100, 1, out));
    for (int k = 0; k < 3; k++) bl_note_pad(&B, pc, 0, 20);
    CHECK(!bl_pad_near(&B, c, 220, 1, out));                                // blocked 3x, landed 1x: skipped
    size_t np; char *tp = text_of(&B, &np);
    CHECK(strstr(tp, "pad\theli\t") != NULL);
    bl_init(&A, "pads2"); CHECK(bl_load_text(&A, tp, np) >= 2 && A.nPads == 2 && A.pads[0].landed == 2 && A.pads[1].failed == 3);
    bl_mark_saved(&A); bl_note_pad(&A, pa, 1, 30);                           // this session: one more landing
    CHECK(bl_reload_keep_fresh(&A, tp, np) >= 0 && A.pads[0].landed == 3);  // (the file without it: merged back)
    // V21: a learned route is split where bots never get through, so bots are not sent through it
    bl_init(&B, "split"); float wr[20][3]; for (int k = 0; k < 20; k++) { wr[k][0] = k * 3.0f; wr[k][1] = 0; wr[k][2] = 0; }
    CHECK(bl_add_route(&B, BL_FOOT, (const float (*)[3])wr, 20, 1) == 0);
    size_t m0 = bl_routes_map_text(&B, BL_FOOT, NULL, 0); char *rmt = malloc(m0 + 1); bl_routes_map_text(&B, BL_FOOT, rmt, m0 + 1);
    CHECK(strstr(rmt, "\n0;19;") && !strstr(rmt, "\n1;0;")); free(rmt);
    float bad[3] = {30, 0, 0}, east[2] = {1, 0}; int bs = bl_note_stuck(&B, BL_FOOT, bad, east, 2);
    for (int k = 0; k < 3; k++) bl_note_result(&B, bs, 0, 0, 3);
    m0 = bl_routes_map_text(&B, BL_FOOT, NULL, 0); rmt = malloc(m0 + 1); bl_routes_map_text(&B, BL_FOOT, rmt, m0 + 1);
    CHECK(strstr(rmt, "\n1;0;") && !strstr(rmt, "30.000;0.000;0.000"));    // two pieces, the bad point left out
    free(rmt);
    // V21: the map picture line (an unknown record to this store) survives load / write
    const char *withMap = "AltBotMod learned\t1\tX\r\nmap\tmap_X.bmp\t-100.0 -100.0 100.0 100.0\t1024 1024\r\n";
    bl_init(&A, "X"); CHECK(bl_load_text(&A, withMap, strlen(withMap)) == 0 && A.nExtra == 1);
    size_t nm; char *tm = text_of(&A, &nm); CHECK(strstr(tm, "map\tmap_X.bmp\t-100.0") != NULL); free(tm);
    free(tp);
    free(t); free(t2); free(t3); free(edited); free(tb); bl_free(&A); bl_free(&B);
    printf("PASS: %u learning store checks (format, merge, judgement, danger, routes, outside edits, forward compatibility, bounds, landing spots, route splits, map line)\n", checks);
    return 0;
}
