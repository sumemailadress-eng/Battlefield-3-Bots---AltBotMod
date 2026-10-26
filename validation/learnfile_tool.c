// AltBotMod V21 test tool: writes a learned file with the real store (every kind of entry), or reads one and prints
// its counts - so Server Control's reader / editor is tested against what the server really writes and reads.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../files/BotMod/src/learn.h"
static BLStore S;
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    if (!strcmp(argv[1], "write")) {
        bl_init(&S, "MP_001_ConquestLarge0");
        float h[2] = {1, 0};
        float a[3] = {10, 1, 20}; int k = bl_note_stuck(&S, BL_FOOT, a, h, 1760000000); for (int j = 0; j < 3; j++) bl_note_result(&S, k, 0, 0, 1760000100);   // avoided
        float b[3] = {40, 1, 20}; k = bl_note_stuck(&S, BL_FOOT, b, h, 1760000000); bl_note_result(&S, k, 1, 0, 1760000200);                                 // kept open
        float c[3] = {70, 1, 20}; k = bl_note_stuck(&S, BL_FOOT, c, h, 1760000000); bl_note_result(&S, k, 1, 1, 1760000300);                                 // jump
        float d[3] = {100, 1, 20}; bl_note_stuck(&S, BL_FOOT, d, h, 1760000000);                                                                             // learning
        float e[3] = {300, 2, 300}; k = bl_note_stuck(&S, BL_GROUND, e, h, 1760000000); bl_note_result(&S, k, 0, 0, 1); bl_note_result(&S, k, 0, 0, 2);   // driving: avoided
        float f[3] = {900, 30, 900}; k = bl_note_stuck(&S, BL_AIR, f, NULL, 1760000000); bl_note_result(&S, k, 0, 0, 3);                                     // aircraft
        for (int j = 0; j < 8; j++) { float g[3] = {505, 0, 505}; bl_note_death(&S, g); }
        float r[20][3]; for (int j = 0; j < 20; j++) { r[j][0] = -100 + j * 3.0f; r[j][1] = 5; r[j][2] = -50; } bl_add_route(&S, BL_FOOT, (const float (*)[3])r, 20, 1760000400);
        float p[3] = {1200, 4, 1300}; bl_note_pad(&S, p, 1, 1760000500); bl_note_pad(&S, p, 1, 1760000600);
        S.extra[S.nExtra++] = strdup("map\tmap_MP_001.bmp\t-1000.0 -900.0 2000.0 2100.0\t1024 1024");
        size_t cap = bl_text_size(&S); char *buf = malloc(cap); size_t n = bl_write(&S, buf, cap);
        FILE *o = fopen(argv[2], "wb"); fwrite(buf, 1, n, o); fclose(o); free(buf);
        return 0;
    }
    FILE *in = fopen(argv[2], "rb"); if (!in) return 3;
    fseek(in, 0, SEEK_END); long n = ftell(in); rewind(in); char *t = malloc((size_t)n + 1); fread(t, 1, (size_t)n, in); fclose(in);
    bl_init(&S, "x"); int r = bl_load_text(&S, t, (size_t)n);
    printf("records=%d spots=%d heat=%d routes=%d pads=%d extra=%d\n", r, S.nSpots, S.nHeat, S.nRoutes, S.nPads, S.nExtra);
    return 0;
}
