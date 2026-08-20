#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gfx/gbi_trace.h"
#include "fs/sha256.h"

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

static int nearly(float a, float b)
{
    float d = a - b;
    if (d < 0)
        d = -d;
    return d < 0.001f;
}

static void ident_f(float mf[4][4])
{
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            mf[i][j] = (i == j) ? 1.0f : 0.0f;
}

int main(int argc, char **argv)
{
    Gfx dl[8];
    Gfx *g;
    Mtx mv, proj;
    float id[4][4], got[4][4];
    OSTask task;
    const G0Record *rec;
    uint8_t digest[32];
    char hex[65];
    const char *out_path = (argc >= 2) ? argv[1] : NULL;
    int i;

    ident_f(id);
    g0_mtx_f2l(id, &mv);
    g0_mtx_l2f(&mv, got);
    for (i = 0; i < 16; i++) {
        if (!nearly(got[i / 4][i % 4], id[i / 4][i % 4]))
            return fail("mtx roundtrip");
    }
    memset(&proj, 0, sizeof proj);
    {
        float p[4][4];
        ident_f(p);
        p[0][0] = 2.0f;
        p[1][1] = 3.0f;
        g0_mtx_f2l(p, &proj);
    }

    {
        /* Nested list is not in the task buffer; walk must still see it. */
        static Gfx child[2];
        Gfx *c = child;
        gSPSegment(c++, 2, 0x2000);
        gSPEndDisplayList(c++);

        g = dl;
        gSPSegment(g++, 1, 0x1000);
        gSPSegment(g++, 4, 0x400000);
        gSPMatrix(g++, &mv, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        gSPMatrix(g++, &proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
        gSPDisplayList(g++, child);
        gSPEndDisplayList(g++);
    }

    memset(&task, 0, sizeof task);
    task.t.type = M_GFXTASK;
    task.t.data_ptr = (u64 *)dl;
    task.t.data_size = (u32)((g - dl) * sizeof(Gfx));

    if (g0_capture_task(&task) != 0)
        return fail("capture");
    rec = g0_last();
    if (!rec)
        return fail("g0_last");
    if (rec->n_gfx != (uint32_t)(g - dl) || rec->n_seg != 16)
        return fail("n_gfx/n_seg");
    if (rec->segment[1] != 0x1000 || rec->segment[4] != 0x400000 || rec->segment[2] != 0x2000)
        return fail("segments");
    if (!rec->have_modelview || !rec->have_projection)
        return fail("mtx flags");
    {
        float mvf[4][4], pf[4][4];
        for (i = 0; i < 16; i++) {
            memcpy(&mvf[i / 4][i % 4], &rec->mtx_modelview[i], 4);
            memcpy(&pf[i / 4][i % 4], &rec->mtx_projection[i], 4);
        }
        if (!nearly(mvf[0][0], 1.0f) || !nearly(mvf[1][1], 1.0f))
            return fail("modelview ident");
        if (!nearly(pf[0][0], 2.0f) || !nearly(pf[1][1], 3.0f))
            return fail("projection scale");
    }
    if (rec->hist[(uint8_t)G_MOVEWORD] != 2)
        return fail("hist MOVEWORD");
    if (rec->hist[(uint8_t)G_MTX] != 2)
        return fail("hist MTX");
    if (rec->hist[(uint8_t)G_ENDDL] != 1)
        return fail("hist ENDDL");
    if (rec->hist[(uint8_t)G_DL] != 1)
        return fail("hist DL");

    g0_gfx_words_sha256(rec, digest);
    silveriris_sha256_hex(digest, hex);

    if (out_path && g0_write_record(rec, out_path) != 0)
        return fail("write g0");

    osSpTaskLoad(&task);
    if (!g0_last() || g0_last()->n_gfx != rec->n_gfx)
        return fail("osSpTaskLoad");

    printf("g0 ok n_gfx=%u gfx_sha256=%s\n", rec->n_gfx, hex);
    return 0;
}
