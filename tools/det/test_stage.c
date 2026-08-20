#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "glue/port_api.h"
#include "fs/c0pack.h"
#include "fs/inflate1172.h"
#include "fs/pack_dma.h"
#include "fs/sha256.h"
#include "fs/stage.h"
#include "gfx/gbi_interp.h"
#include "gfx/tmem.h"
#include "gfx/tex_bank.h"
#include "gfx/gbi_trace.h"
#include "gfx/sw_raster.h"
#include "overrides/lv_clock.h"
#include "rng/random.h"
#include "vi/sim_tick.h"
#include "vi/tick_contract.h"

#include "game/frametiming.h"

#define OFF_ROOMS 0x040
#define OFF_ROOM1 0x058
#define OFF_PORTAL 0x088
#define OFF_VTX 0x0A0
#define OFF_MTX_MV 0x0D0
#define OFF_MTX_PR 0x110
#define OFF_GDL 0x150
#define BG_SIZE 0x280

#define SEG(off) (0x0F000000u | (uint32_t)(off))

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

static void wr_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void wr_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void wr_be_mtx(uint8_t *dst, const Mtx *m)
{
    const uint32_t *src = (const uint32_t *)&m->m[0][0];
    int i;
    for (i = 0; i < 16; i++)
        wr_be32(dst + (size_t)i * 4, src[i]);
}

static void wr_be_vtx(uint8_t *dst, const Vtx *v)
{
    wr_be16(dst + 0, (uint16_t)v->v.ob[0]);
    wr_be16(dst + 2, (uint16_t)v->v.ob[1]);
    wr_be16(dst + 4, (uint16_t)v->v.ob[2]);
    wr_be16(dst + 6, 0);
    wr_be16(dst + 8, (uint16_t)v->v.tc[0]);
    wr_be16(dst + 10, (uint16_t)v->v.tc[1]);
    dst[12] = v->v.cn[0];
    dst[13] = v->v.cn[1];
    dst[14] = v->v.cn[2];
    dst[15] = v->v.cn[3];
}

/* Extractor inflate1172.test.ts vector: fflate raw-deflate + 2-byte 1172 header. */
static const uint8_t k_hello_1172[] = {
    0x11, 0x72, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0x28, 0xce, 0xcc, 0x29,
    0x4b, 0x2d, 0xca, 0x2c, 0xca, 0x2c, 0x56, 0x30, 0x34, 0x34, 0x37, 0x52,
    0x28, 0x49, 0x2d, 0x2e, 0x51, 0x28, 0x4b, 0x4d, 0x2e, 0xc9, 0x2f, 0x02,
    0x00};
static const char k_hello_plain[] = "hello silveriris 1172 test vector";

static size_t wrap1172_stored(const uint8_t *src, size_t n, uint8_t *dst, size_t cap)
{
    size_t need = n + 7;
    if (n > 0xFFFFu || cap < need)
        return 0;
    dst[0] = 0x11;
    dst[1] = 0x72;
    dst[2] = 0x01; /* final stored deflate block */
    dst[3] = (uint8_t)n;
    dst[4] = (uint8_t)(n >> 8);
    dst[5] = (uint8_t)~n;
    dst[6] = (uint8_t)(~(n >> 8));
    memcpy(dst + 7, src, n);
    return need;
}

static int test_inflate_bytes(void)
{
    uint8_t out[64];
    uint8_t wrapped[80];
    uint8_t again[64];
    size_t n = 0, w = 0, n2 = 0;
    const uint8_t *plain = (const uint8_t *)k_hello_plain;
    size_t plen = strlen(k_hello_plain);

    if (bgDecompress(k_hello_1172, sizeof k_hello_1172, out, sizeof out, &n) !=
        PORT_INFLATE1172_OK)
        return fail("inflate extractor vector");
    if (n != plen || memcmp(out, plain, plen) != 0)
        return fail("extractor 1172 bytes");
    w = wrap1172_stored(plain, plen, wrapped, sizeof wrapped);
    if (!w)
        return fail("wrap 1172");
    if (bgDecompress(wrapped, w, again, sizeof again, &n2) != PORT_INFLATE1172_OK)
        return fail("inflate stored wrap");
    if (n2 != plen || memcmp(again, plain, plen) != 0)
        return fail("stored wrap bytes");
    printf("inflate1172 ok extractor_n=%zu stored_n=%zu\n", n, n2);
    return 0;
}

/* Same picture as g1_run_synthetic, stored as pack-faithful BE + 0x0F segs. */
static void build_g1dl_bg(uint8_t *bg)
{
    static Gfx host_dl[16];
    static Vtx vtx[3];
    static Mtx mv, proj;
    Gfx *g;
    float id[4][4];
    int i, j;
    uint32_t ngfx;

    memset(bg, 0, BG_SIZE);
    wr_be32(bg + 0, PORT_BG_MAGIC_G1DL);
    wr_be32(bg + 4, SEG(OFF_ROOMS));
    wr_be32(bg + 8, SEG(OFF_PORTAL));

    /* room 0 dummy; room 1 points at verts + primary GDL; room 2 terminator */
    wr_be32(bg + OFF_ROOM1 + 0, SEG(OFF_VTX));
    wr_be32(bg + OFF_ROOM1 + 4, SEG(OFF_GDL));
    wr_be32(bg + OFF_ROOM1 + 8, 0);

    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            id[i][j] = (i == j) ? 1.f : 0.f;
    g0_mtx_f2l(id, &mv);
    g0_mtx_f2l(id, &proj);
    wr_be_mtx(bg + OFF_MTX_MV, &mv);
    wr_be_mtx(bg + OFF_MTX_PR, &proj);

    memset(vtx, 0, sizeof vtx);
    vtx[0].v.ob[0] = -1;
    vtx[0].v.ob[1] = -1;
    vtx[0].v.cn[0] = 255;
    vtx[0].v.cn[1] = 32;
    vtx[0].v.cn[2] = 32;
    vtx[0].v.cn[3] = 255;
    vtx[1].v.ob[0] = 1;
    vtx[1].v.ob[1] = -1;
    vtx[1].v.cn[0] = 32;
    vtx[1].v.cn[1] = 255;
    vtx[1].v.cn[2] = 32;
    vtx[1].v.cn[3] = 255;
    vtx[2].v.ob[1] = 1;
    vtx[2].v.cn[0] = 32;
    vtx[2].v.cn[1] = 32;
    vtx[2].v.cn[2] = 255;
    vtx[2].v.cn[3] = 255;
    for (i = 0; i < 3; i++)
        wr_be_vtx(bg + OFF_VTX + (size_t)i * 16, &vtx[i]);

    g = host_dl;
    gDPSetFillColor(g++, GPACK_RGBA5551(12, 28, 48, 1) | (GPACK_RGBA5551(12, 28, 48, 1) << 16));
    gDPFillRectangle(g++, 0, 0, G1_FB_W, G1_FB_H);
    gSPMatrix(g++, &mv, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, &proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPVertex(g++, vtx, 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSPEndDisplayList(g++);
    ngfx = (uint32_t)(g - host_dl);
    for (i = 0; i < (int)ngfx; i++) {
        uint32_t w0 = (uint32_t)host_dl[i].words.w0;
        uint32_t w1 = (uint32_t)host_dl[i].words.w1;
        uint8_t cmd = (uint8_t)(w0 >> 24);
        if (cmd == (uint8_t)G_MTX) {
            uint32_t params = (w0 >> 16) & 0xFF;
            w1 = (params & G_MTX_PROJECTION) ? SEG(OFF_MTX_PR) : SEG(OFF_MTX_MV);
        } else if (cmd == (uint8_t)G_VTX) {
            w1 = SEG(OFF_VTX);
        }
        wr_be32(bg + OFF_GDL + (size_t)i * 8, w0);
        wr_be32(bg + OFF_GDL + (size_t)i * 8 + 4, w1);
    }
}

/*
 * Rare-shaped header (word0 == 0) + 1172-compressed C0 GDL.
 * G_SETTEX is skipped; G_TRI4(0,1,2) is the picture — same verts as G1DL.
 */
static size_t build_c0_compressed_bg(uint8_t *bg)
{
    static Vtx vtx[3];
    static Mtx mv, proj;
    uint8_t raw_gdl[80];
    uint8_t wrapped[96];
    float id[4][4];
    int i, j, ngfx;
    size_t wlen;

    memset(bg, 0, BG_SIZE);
    wr_be32(bg + 0, 0);
    wr_be32(bg + 4, SEG(OFF_ROOMS));
    wr_be32(bg + 8, SEG(OFF_PORTAL));
    wr_be32(bg + OFF_ROOM1 + 0, SEG(OFF_VTX));
    wr_be32(bg + OFF_ROOM1 + 4, SEG(OFF_GDL));
    wr_be32(bg + OFF_ROOM1 + 8, 0);

    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            id[i][j] = (i == j) ? 1.f : 0.f;
    g0_mtx_f2l(id, &mv);
    g0_mtx_f2l(id, &proj);
    wr_be_mtx(bg + OFF_MTX_MV, &mv);
    wr_be_mtx(bg + OFF_MTX_PR, &proj);

    memset(vtx, 0, sizeof vtx);
    vtx[0].v.ob[0] = -1;
    vtx[0].v.ob[1] = -1;
    vtx[0].v.cn[0] = 255;
    vtx[0].v.cn[1] = 32;
    vtx[0].v.cn[2] = 32;
    vtx[0].v.cn[3] = 255;
    vtx[1].v.ob[0] = 1;
    vtx[1].v.ob[1] = -1;
    vtx[1].v.cn[0] = 32;
    vtx[1].v.cn[1] = 255;
    vtx[1].v.cn[2] = 32;
    vtx[1].v.cn[3] = 255;
    vtx[2].v.ob[1] = 1;
    vtx[2].v.cn[0] = 32;
    vtx[2].v.cn[1] = 32;
    vtx[2].v.cn[2] = 255;
    vtx[2].v.cn[3] = 255;
    for (i = 0; i < 3; i++)
        wr_be_vtx(bg + OFF_VTX + (size_t)i * 16, &vtx[i]);

    memset(raw_gdl, 0, sizeof raw_gdl);
    ngfx = 0;
    /* G_SETFILLCOLOR + G_FILLRECT — same as Fast3D synthetic. */
    wr_be32(raw_gdl + ngfx * 8, ((uint32_t)G_SETFILLCOLOR << 24));
    wr_be32(raw_gdl + ngfx * 8 + 4,
            GPACK_RGBA5551(12, 28, 48, 1) | (GPACK_RGBA5551(12, 28, 48, 1) << 16));
    ngfx++;
    wr_be32(raw_gdl + ngfx * 8,
            ((uint32_t)G_FILLRECT << 24) | (G1_FB_W << 14) | (G1_FB_H << 2));
    wr_be32(raw_gdl + ngfx * 8 + 4, 0);
    ngfx++;
    /* G_MTX modelview / projection */
    wr_be32(raw_gdl + ngfx * 8,
            ((uint32_t)(uint8_t)G_MTX << 24) |
                ((G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH) << 16));
    wr_be32(raw_gdl + ngfx * 8 + 4, SEG(OFF_MTX_MV));
    ngfx++;
    wr_be32(raw_gdl + ngfx * 8,
            ((uint32_t)(uint8_t)G_MTX << 24) |
                ((G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH) << 16));
    wr_be32(raw_gdl + ngfx * 8 + 4, SEG(OFF_MTX_PR));
    ngfx++;
    /* G_VTX n=3 v0=0 (Fast3D param: ((n-1)<<4)|v0) */
    wr_be32(raw_gdl + ngfx * 8, ((uint32_t)(uint8_t)G_VTX << 24) | (0x20 << 16));
    wr_be32(raw_gdl + ngfx * 8 + 4, SEG(OFF_VTX));
    ngfx++;
    /* Rare G_SETTEX 0xC0 — skip (no bank). */
    wr_be32(raw_gdl + ngfx * 8, 0xC0000000u);
    wr_be32(raw_gdl + ngfx * 8 + 4, 0);
    ngfx++;
    /* Rare G_TRI4: first tri (0,1,2), others empty. */
    wr_be32(raw_gdl + ngfx * 8, 0xB1000002u);
    wr_be32(raw_gdl + ngfx * 8 + 4, 0x00000010u);
    ngfx++;
    wr_be32(raw_gdl + ngfx * 8, (uint32_t)(uint8_t)G_ENDDL << 24);
    wr_be32(raw_gdl + ngfx * 8 + 4, 0);
    ngfx++;

    wlen = wrap1172_stored(raw_gdl, (size_t)ngfx * 8u, wrapped, sizeof wrapped);
    if (!wlen || OFF_GDL + wlen > BG_SIZE)
        return 0;
    memcpy(bg + OFF_GDL, wrapped, wlen);
    return wlen;
}

static int check_grey(const char *want)
{
    uint8_t digest[32];
    char hex[65];
    const uint8_t *fb = g1_fb_rgba();
    unsigned i, n = (unsigned)G1_FB_W * (unsigned)G1_FB_H;
    unsigned painted = 0;

    if (!fb)
        return fail("fb");
    for (i = 0; i < n; i++) {
        if (fb[i * 4] != fb[0] || fb[i * 4 + 1] != fb[1] || fb[i * 4 + 2] != fb[2])
            painted++;
    }
    if (painted < 1000)
        return fail("triangle too small");
    g1_fb_grey_sha256(digest);
    silveriris_sha256_hex(digest, hex);
    if (want && want[0] && strcmp(hex, want) != 0) {
        fprintf(stderr, "grey hash got %s want %s\n", hex, want);
        return 1;
    }
    printf("stage g1dl painted=%u grey_sha256=%s\n", painted, hex);
    return 0;
}


#define VTX_SEG14 0x0E000000u

/* Retail-shaped: 1172 Vtx table + GDL with G_VTX(seg 14) + unknown + G_TRI4. No MTX. */
static size_t build_c0_seg14_bg(uint8_t *bg)
{
    static Vtx vtx[3];
    uint8_t raw_vtx[48];
    uint8_t raw_gdl[64];
    uint8_t wrapped_v[80];
    uint8_t wrapped_g[80];
    int i, ngfx;
    size_t vlen, glen;

    memset(bg, 0, BG_SIZE);
    wr_be32(bg + 0, 0);
    wr_be32(bg + 4, SEG(OFF_ROOMS));
    wr_be32(bg + 8, SEG(OFF_PORTAL));
    wr_be32(bg + OFF_ROOM1 + 0, SEG(OFF_VTX));
    wr_be32(bg + OFF_ROOM1 + 4, SEG(OFF_GDL));
    wr_be32(bg + OFF_ROOM1 + 8, 0);

    memset(vtx, 0, sizeof vtx);
    vtx[0].v.ob[0] = -40;
    vtx[0].v.ob[2] = -80;
    vtx[1].v.ob[0] = 40;
    vtx[1].v.ob[2] = -80;
    vtx[2].v.ob[1] = 40;
    vtx[2].v.ob[2] = -80;
    for (i = 0; i < 3; i++)
        wr_be_vtx(raw_vtx + (size_t)i * 16, &vtx[i]);
    vlen = wrap1172_stored(raw_vtx, sizeof raw_vtx, wrapped_v, sizeof wrapped_v);
    if (!vlen || OFF_VTX + vlen > OFF_GDL)
        return 0;
    memcpy(bg + OFF_VTX, wrapped_v, vlen);

    memset(raw_gdl, 0, sizeof raw_gdl);
    ngfx = 0;
    /* G_SETTEX skip */
    wr_be32(raw_gdl + ngfx * 8, 0xC0000000u);
    wr_be32(raw_gdl + ngfx * 8 + 4, 0);
    ngfx++;
    /* Unresolved G_DL (seg 13 empty) — must not abort the walk. */
    wr_be32(raw_gdl + ngfx * 8, ((uint32_t)(uint8_t)G_DL << 24));
    wr_be32(raw_gdl + ngfx * 8 + 4, 0x0D000000u);
    ngfx++;
    /* Unknown opcode — skip, then TRI4 still runs. */
    wr_be32(raw_gdl + ngfx * 8, 0xAB000000u);
    wr_be32(raw_gdl + ngfx * 8 + 4, 0);
    ngfx++;
    wr_be32(raw_gdl + ngfx * 8, ((uint32_t)(uint8_t)G_VTX << 24) | (0x20 << 16));
    wr_be32(raw_gdl + ngfx * 8 + 4, VTX_SEG14);
    ngfx++;
    wr_be32(raw_gdl + ngfx * 8, 0xB1000002u);
    wr_be32(raw_gdl + ngfx * 8 + 4, 0x00000010u);
    ngfx++;
    wr_be32(raw_gdl + ngfx * 8, (uint32_t)(uint8_t)G_ENDDL << 24);
    wr_be32(raw_gdl + ngfx * 8 + 4, 0);
    ngfx++;

    glen = wrap1172_stored(raw_gdl, (size_t)ngfx * 8u, wrapped_g, sizeof wrapped_g);
    if (!glen || OFF_GDL + glen > BG_SIZE)
        return 0;
    memcpy(bg + OFF_GDL, wrapped_g, glen);
    return glen;
}

static int test_seg14_camera(void)
{
    uint8_t vtx_be[48];
    uint8_t gdl[48];
    Vtx vtx[3];
    int i, ngfx;
    unsigned nz;

    memset(vtx, 0, sizeof vtx);
    vtx[0].v.ob[0] = -40;
    vtx[0].v.ob[2] = -80;
    vtx[1].v.ob[0] = 40;
    vtx[1].v.ob[2] = -80;
    vtx[2].v.ob[1] = 40;
    vtx[2].v.ob[2] = -80;
    for (i = 0; i < 3; i++)
        wr_be_vtx(vtx_be + (size_t)i * 16, &vtx[i]);

    memset(gdl, 0, sizeof gdl);
    ngfx = 0;
    wr_be32(gdl + ngfx * 8, 0xC0000000u);
    wr_be32(gdl + ngfx * 8 + 4, 0);
    ngfx++;
    wr_be32(gdl + ngfx * 8, ((uint32_t)(uint8_t)G_DL << 24));
    wr_be32(gdl + ngfx * 8 + 4, 0x0D000000u);
    ngfx++;
    wr_be32(gdl + ngfx * 8, 0xAB000000u);
    wr_be32(gdl + ngfx * 8 + 4, 0);
    ngfx++;
    wr_be32(gdl + ngfx * 8, ((uint32_t)(uint8_t)G_VTX << 24) | (0x20 << 16));
    wr_be32(gdl + ngfx * 8 + 4, VTX_SEG14);
    ngfx++;
    wr_be32(gdl + ngfx * 8, 0xB1000002u);
    wr_be32(gdl + ngfx * 8 + 4, 0x00000010u);
    ngfx++;
    wr_be32(gdl + ngfx * 8, (uint32_t)(uint8_t)G_ENDDL << 24);
    wr_be32(gdl + ngfx * 8 + 4, 0);
    ngfx++;

    /* Missing seg 14: G_VTX resolves NULL, FB stays black. */
    g1_clear_lookat();
    g1_set_lookat(0.f, 0.f, 0.f, 0.f);
    if (g1_interpret_be_dl(gdl, (uint32_t)ngfx) != 0)
        return fail("seg14 interpret no-vtx");
    nz = g1_fb_nonzero();
    if (nz != 0)
        return fail("no segment 14 must stay black");
    printf("seg14 missing-vtx fb_nonzero=%u (black)\n", nz);

    /* Identity MVP: clip=world, so +/-40 verts are a huge wrong cover (or miss). */
    g1_clear_lookat();
    g1_set_segment(14, (uintptr_t)vtx_be);
    if (g1_interpret_be_dl(gdl, (uint32_t)ngfx) != 0)
        return fail("identity interpret");
    {
        unsigned id_nz = g1_fb_nonzero();
        printf("seg14 identity fb_nonzero=%u\n", id_nz);

        /* Player look-at (theta=0 faces -Z): triangle at z=-80 is in front. */
        g1_set_lookat(0.f, 0.f, 0.f, 0.f);
        g1_set_segment(14, (uintptr_t)vtx_be);
        if (g1_interpret_be_dl(gdl, (uint32_t)ngfx) != 0)
            return fail("lookat interpret");
        nz = g1_fb_nonzero();
        if (nz < 1000)
            return fail("lookat+seg14 TRI4 should paint");
        if (nz == id_nz)
            return fail("lookat FB matches identity — camera not applied");
        printf("seg14 lookat fb_nonzero=%u (visible grey, != identity)\n", nz);
    }
    g1_clear_lookat();
    return 0;
}


static void fill_i8_checker(uint8_t *dst, int w, int h, uint8_t a, uint8_t b)
{
    int y, x;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            dst[y * w + x] = ((x ^ y) & 1) ? b : a;
}

static void fill_i4_checker(uint8_t *dst, int w, int h, uint8_t a, uint8_t b)
{
    int y, x, i = 0;
    memset(dst, 0, (size_t)((w * h + 1) / 2));
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            uint8_t v = ((x ^ y) & 1) ? b : a;
            if ((i & 1) == 0)
                dst[i >> 1] = (uint8_t)(v << 4);
            else
                dst[i >> 1] |= v & 0x0f;
            i++;
        }
    }
}

/* Identity-MVP TRI4 with UVs covering an 8x8 tile. SETTEX id in w1. */
static uint32_t build_tex_tri4(uint8_t *gdl, uint8_t *vtx_be, uint16_t tex_id)
{
    static Vtx vtx[3];
    static Mtx mv, proj;
    float id[4][4];
    int i, j, ngfx;

    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            id[i][j] = (i == j) ? 1.f : 0.f;
    g0_mtx_f2l(id, &mv);
    g0_mtx_f2l(id, &proj);

    memset(vtx, 0, sizeof vtx);
    vtx[0].v.ob[0] = -1;
    vtx[0].v.ob[1] = -1;
    vtx[0].v.tc[0] = 0;
    vtx[0].v.tc[1] = 0;
    vtx[1].v.ob[0] = 1;
    vtx[1].v.ob[1] = -1;
    vtx[1].v.tc[0] = 256;
    vtx[1].v.tc[1] = 0;
    vtx[2].v.ob[1] = 1;
    vtx[2].v.tc[0] = 0;
    vtx[2].v.tc[1] = 256;
    for (i = 0; i < 3; i++)
        wr_be_vtx(vtx_be + (size_t)i * 16, &vtx[i]);

    ngfx = 0;
    wr_be32(gdl + ngfx * 8, ((uint32_t)G_SETFILLCOLOR << 24));
    wr_be32(gdl + ngfx * 8 + 4,
                  GPACK_RGBA5551(12, 28, 48, 1) | (GPACK_RGBA5551(12, 28, 48, 1) << 16));
    ngfx++;
    wr_be32(gdl + ngfx * 8,
                  ((uint32_t)G_FILLRECT << 24) | (G1_FB_W << 14) | (G1_FB_H << 2));
    wr_be32(gdl + ngfx * 8 + 4, 0);
    ngfx++;
    wr_be32(gdl + ngfx * 8,
                  ((uint32_t)(uint8_t)G_MTX << 24) |
                      ((G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH) << 16));
    wr_be32(gdl + ngfx * 8 + 4, 0x0F000000u + 0x200u);
    ngfx++;
    wr_be32(gdl + ngfx * 8,
                  ((uint32_t)(uint8_t)G_MTX << 24) |
                      ((G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH) << 16));
    wr_be32(gdl + ngfx * 8 + 4, 0x0F000000u + 0x240u);
    ngfx++;
    wr_be32(gdl + ngfx * 8, ((uint32_t)(uint8_t)G_VTX << 24) | (0x20 << 16));
    wr_be32(gdl + ngfx * 8 + 4, 0x0F000000u + 0x280u);
    ngfx++;
    /* G_SETTEX: type TILE, texture_id */
    wr_be32(gdl + ngfx * 8, 0xC0000003u);
    wr_be32(gdl + ngfx * 8 + 4, (uint32_t)tex_id);
    ngfx++;
    wr_be32(gdl + ngfx * 8, 0xB1000002u);
    wr_be32(gdl + ngfx * 8 + 4, 0x00000010u);
    ngfx++;
    wr_be32(gdl + ngfx * 8, (uint32_t)(uint8_t)G_ENDDL << 24);
    wr_be32(gdl + ngfx * 8 + 4, 0);
    ngfx++;

    /* Host buffers for MTX + VTX behind segment 0xF. Caller must map them. */
    (void)mv;
    (void)proj;
    return (uint32_t)ngfx;
}

static int count_tone(int lo, int hi)
{
    const uint8_t *fb = g1_fb_rgba();
    unsigned i, n = (unsigned)G1_FB_W * (unsigned)G1_FB_H, c = 0;
    if (!fb)
        return 0;
    for (i = 0; i < n; i++) {
        unsigned r = fb[i * 4];
        if (r >= (unsigned)lo && r <= (unsigned)hi)
            c++;
    }
    return (int)c;
}

static int run_tex_dl(uint16_t tex_id, const uint8_t *vtx_be)
{
    uint8_t gdl[80];
    uint8_t host[0x300];
    static Mtx mv, proj;
    float id[4][4];
    int i, j;
    uint32_t ngfx;

    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            id[i][j] = (i == j) ? 1.f : 0.f;
    g0_mtx_f2l(id, &mv);
    g0_mtx_f2l(id, &proj);
    memset(host, 0, sizeof host);
    wr_be_mtx(host + 0x200, &mv);
    wr_be_mtx(host + 0x240, &proj);

    ngfx = build_tex_tri4(gdl, (uint8_t *)vtx_be, tex_id);
    memcpy(host + 0x280, vtx_be, 48);

    g1_clear_lookat();
    g1_set_segment(0xF, (uintptr_t)host);
    if (g1_interpret_be_dl(gdl, ngfx) != 0)
        return -1;
    return 0;
}


static size_t build_rare_i8(uint8_t *dst, const uint8_t *tex, unsigned w, unsigned h)
{
    /* flags=non-zlib lod1; format I8=7; method uncompressed. */
    dst[0] = 0x01;
    dst[1] = (uint8_t)((7u << 4) | ((w >> 4) & 0x0fu));
    dst[2] = (uint8_t)((w << 4) | ((h >> 4) & 0x0fu));
    dst[3] = (uint8_t)((h << 4) | 0u);
    memcpy(dst + 4, tex, (size_t)w * h);
    return 4u + (size_t)w * h;
}

static size_t build_zlib_ci4(uint8_t *dst, size_t cap, const uint8_t *idx, unsigned w,
                             unsigned h, uint16_t c0, uint16_t c1)
{
    uint8_t pay[80];
    size_t plen, n = 0;
    if (cap < 16)
        return 0;
    dst[n++] = 0x41;
    dst[n++] = G1_TEX_CI4;
    dst[n++] = 0x01;
    dst[n++] = (uint8_t)(c0 >> 8);
    dst[n++] = (uint8_t)c0;
    dst[n++] = (uint8_t)(c1 >> 8);
    dst[n++] = (uint8_t)c1;
    dst[n++] = (uint8_t)w;
    dst[n++] = (uint8_t)h;
    plen = wrap1172_stored(idx, (size_t)((w * h + 1u) / 2u), pay, sizeof pay);
    if (!plen || n + plen > cap)
        return 0;
    memcpy(dst + n, pay, plen);
    return n + plen;
}

static int test_tex_bank_synthetic(void)
{
    uint8_t i8[64], ci4[32], rare[80], zbank[80];
    G1TexBankOut d;
    size_t n;
    unsigned i;

    fill_i8_checker(i8, 8, 8, 0x20, 0xE0);
    n = build_rare_i8(rare, i8, 8, 8);
    if (!n || g1_tex_bank_decode(rare, n, &d) != G1_TEX_BANK_OK)
        return fail("decode rare I8");
    if (d.zlib || d.fmt != G1_TEX_I8 || d.w != 8 || d.h != 8 || d.ntex != 64)
        return fail("rare I8 header");
    if (memcmp(d.texels, i8, 64) != 0)
        return fail("rare I8 texels");
    printf("tex_bank rare I8 8x8 method=%d n=%zu\n", (int)d.method, d.ntex);

    fill_i4_checker(ci4, 8, 8, 0x0, 0x1);
    n = build_zlib_ci4(zbank, sizeof zbank, ci4, 8, 8, 0xF801, 0x07C1);
    if (!n || g1_tex_bank_decode(zbank, n, &d) != G1_TEX_BANK_OK)
        return fail("decode zlib CI4 1172");
    if (!d.zlib || d.fmt != G1_TEX_CI4 || d.ntlut != 2 || d.ntex != 32)
        return fail("zlib CI4 header");
    if (d.tlut[0] != 0xF801 || d.tlut[1] != 0x07C1)
        return fail("zlib CI4 tlut");
    if (memcmp(d.texels, ci4, 32) != 0)
        return fail("zlib CI4 indices");
    printf("tex_bank zlib CI4 8x8 ncol=%u 1172 ok\n", d.ntlut);

    /* Disk synthetic (real raw-deflate, not stored). */
    {
        uint8_t buf[128];
        FILE *f = fopen("testdata/stage/checker.rare.bin", "rb");
        size_t nr;
        if (!f)
            f = fopen("/home/grok/GoldenEye/testdata/stage/checker.rare.bin", "rb");
        if (!f)
            return fail("open checker.rare.bin");
        nr = fread(buf, 1, sizeof buf, f);
        fclose(f);
        if (g1_tex_bank_decode(buf, nr, &d) != G1_TEX_BANK_OK)
            return fail("disk rare I8");
        if (d.fmt != G1_TEX_I8 || memcmp(d.texels, i8, 64) != 0)
            return fail("disk rare texels");
        f = fopen("testdata/stage/checker.zbank.bin", "rb");
        if (!f)
            f = fopen("/home/grok/GoldenEye/testdata/stage/checker.zbank.bin", "rb");
        if (!f)
            return fail("open checker.zbank.bin");
        nr = fread(buf, 1, sizeof buf, f);
        fclose(f);
        if (g1_tex_bank_decode(buf, nr, &d) != G1_TEX_BANK_OK)
            return fail("disk zlib CI4");
        if (!d.zlib || d.fmt != G1_TEX_CI4 || d.ntex != 32)
            return fail("disk zlib header");
        for (i = 0; i < 32; i++) {
            if (d.texels[i] != ci4[i])
                return fail("disk zlib indices");
        }
        printf("tex_bank disk rare+zbank ok\n");
    }
    return 0;
}

static int test_settex_rare_bank_pack(void)
{
    uint8_t rare[80], zbank[80], i8[64], ci4[32], vtx_be[48];
    C0File files[1];
    uint8_t *pack = NULL;
    size_t pack_len = 0, n;
    uint8_t hash[32];
    C0Pack opened;
    int dark, lit;

    fill_i8_checker(i8, 8, 8, 0x20, 0xE0);
    n = build_rare_i8(rare, i8, 8, 8);
    files[0].path = "assets/images/split/image7.bin";
    files[0].bytes = rare;
    files[0].size = n;
    if (c0pack_build(files, 1, 0, 0, &pack, &pack_len, hash) != 0)
        return fail("rare pack build");
    if (c0pack_open(pack, pack_len, &opened) != 0)
        return fail("rare pack open");
    g1_tex_unload();
    g1_tex_set_pack(&opened);
    if (run_tex_dl(7, vtx_be) != 0)
        return fail("rare pack dl");
    if (g1_tex_ok_count() < 1)
        return fail("rare SETTEX id 7 texOk");
    dark = count_tone(0, 80);
    lit = count_tone(180, 255);
    if (dark < 200 || lit < 200)
        return fail("rare I8 not sampled");
    printf("settex pack image7.bin RARE-I8 dark=%d lit=%d ok=%u\n", dark, lit, g1_tex_ok_count());
    g1_tex_set_pack(NULL);
    g1_tex_unload();
    c0pack_close(&opened);
    free(pack);
    pack = NULL;

    fill_i4_checker(ci4, 8, 8, 0x0, 0x1);
    n = build_zlib_ci4(zbank, sizeof zbank, ci4, 8, 8, 0xF801, 0x07C1);
    files[0].path = "assets/images/split/image9.bin";
    files[0].bytes = zbank;
    files[0].size = n;
    if (c0pack_build(files, 1, 0, 0, &pack, &pack_len, hash) != 0)
        return fail("zbank pack build");
    if (c0pack_open(pack, pack_len, &opened) != 0)
        return fail("zbank pack open");
    g1_tex_unload();
    g1_tex_set_pack(&opened);
    if (run_tex_dl(9, vtx_be) != 0)
        return fail("zbank pack dl");
    if (g1_tex_ok_count() < 1)
        return fail("zlib SETTEX id 9 texOk");
    {
        const uint8_t *fb = g1_fb_rgba();
        unsigned i, np = (unsigned)G1_FB_W * (unsigned)G1_FB_H, red = 0, grn = 0;
        for (i = 0; i < np; i++) {
            if (fb[i * 4] > 160 && fb[i * 4 + 1] < 80)
                red++;
            if (fb[i * 4 + 1] > 160 && fb[i * 4] < 80)
                grn++;
        }
        if (red < 200 || grn < 200)
            return fail("zlib CI4 not red+green");
        printf("settex pack image9.bin ZLIB-CI4 red=%u green=%u ok=%u\n", red, grn,
               g1_tex_ok_count());
    }
    g1_tex_set_pack(NULL);
    g1_tex_unload();
    c0pack_close(&opened);
    free(pack);
    return 0;
}

static int test_settex_formats(void)
{
    uint8_t i8[64], i4[32], ci4[32], vtx_be[48];
    uint16_t tlut[2];
    int dark, lit;

    fill_i8_checker(i8, 8, 8, 0x20, 0xE0);
    if (g1_tex_load_raw(1, G1_TEX_I8, 8, 8, i8, 64, NULL, 0) != 0)
        return fail("load i8");
    if (run_tex_dl(1, vtx_be) != 0)
        return fail("i8 dl");
    if (g1_tex_settex_count() < 1 || g1_tex_ok_count() < 1)
        return fail("i8 settex not ok");
    dark = count_tone(0, 80);
    lit = count_tone(180, 255);
    if (dark < 200 || lit < 200)
        return fail("i8 checker not both tones");
    printf("settex I8 checker dark=%d lit=%d settex=%u ok=%u\n", dark, lit, g1_tex_settex_count(),
           g1_tex_ok_count());

    fill_i4_checker(i4, 8, 8, 0x2, 0xE);
    if (g1_tex_load_raw(2, G1_TEX_I4, 8, 8, i4, 32, NULL, 0) != 0)
        return fail("load i4");
    if (run_tex_dl(2, vtx_be) != 0)
        return fail("i4 dl");
    dark = count_tone(0, 80);
    lit = count_tone(180, 255);
    if (dark < 200 || lit < 200)
        return fail("i4 checker not both tones");
    printf("settex I4 checker dark=%d lit=%d\n", dark, lit);

    fill_i4_checker(ci4, 8, 8, 0x0, 0x1);
    tlut[0] = 0xF801; /* red */
    tlut[1] = 0x07C1; /* green */
    if (g1_tex_load_raw(3, G1_TEX_CI4, 8, 8, ci4, 32, tlut, 2) != 0)
        return fail("load ci4");
    if (run_tex_dl(3, vtx_be) != 0)
        return fail("ci4 dl");
    {
        const uint8_t *fb = g1_fb_rgba();
        unsigned i, n = (unsigned)G1_FB_W * (unsigned)G1_FB_H, red = 0, grn = 0;
        for (i = 0; i < n; i++) {
            if (fb[i * 4] > 160 && fb[i * 4 + 1] < 80)
                red++;
            if (fb[i * 4 + 1] > 160 && fb[i * 4] < 80)
                grn++;
        }
        if (red < 200 || grn < 200)
            return fail("ci4 tlut not red+green");
        printf("settex CI4 tlut red=%u green=%u\n", red, grn);
    }
    g1_tex_unload();
    return 0;
}

static int test_settex_pack_lookup(void)
{
    uint8_t sitx[76];
    uint8_t vtx_be[48];
    C0File files[1];
    uint8_t *pack = NULL;
    size_t pack_len = 0;
    uint8_t hash[32];
    C0Pack opened;
    int dark, lit;
    FILE *f;
    size_t nread;

    f = fopen("testdata/stage/checker.sitx", "rb");
    if (!f)
        f = fopen("/home/grok/GoldenEye/testdata/stage/checker.sitx", "rb");
    if (!f)
        return fail("open checker.sitx");
    nread = fread(sitx, 1, sizeof sitx, f);
    fclose(f);
    if (nread != sizeof sitx || memcmp(sitx, "SITX", 4) != 0)
        return fail("checker.sitx bytes");

    files[0].path = "assets/images/split/image7.bin";
    files[0].bytes = sitx;
    files[0].size = sizeof sitx;
    if (c0pack_build(files, 1, 0, 0, &pack, &pack_len, hash) != 0)
        return fail("sitx pack build");
    if (c0pack_open(pack, pack_len, &opened) != 0)
        return fail("sitx pack open");
    g1_tex_unload();
    g1_tex_set_pack(&opened);
    if (run_tex_dl(7, vtx_be) != 0)
        return fail("pack settex dl");
    if (g1_tex_ok_count() < 1)
        return fail("pack SETTEX id 7 did not load SITX");
    dark = count_tone(0, 80);
    lit = count_tone(180, 255);
    if (dark < 200 || lit < 200)
        return fail("pack SITX checker not sampled");
    printf("settex pack image7.bin SITX dark=%d lit=%d ok=%u\n", dark, lit, g1_tex_ok_count());

    /* Named bank path: id 0 is COPYICON in images.def. */
    files[0].path = "assets/images/split/COPYICON.bin";
    g1_tex_unload();
    c0pack_close(&opened);
    free(pack);
    pack = NULL;
    if (c0pack_build(files, 1, 0, 0, &pack, &pack_len, hash) != 0)
        return fail("name pack build");
    if (c0pack_open(pack, pack_len, &opened) != 0)
        return fail("name pack open");
    g1_tex_set_pack(&opened);
    if (run_tex_dl(0, vtx_be) != 0)
        return fail("name settex dl");
    if (g1_tex_ok_count() < 1)
        return fail("named COPYICON SETTEX miss");
    dark = count_tone(0, 80);
    lit = count_tone(180, 255);
    if (dark < 200 || lit < 200)
        return fail("named SITX not sampled");
    printf("settex pack COPYICON.bin (id 0) dark=%d lit=%d\n", dark, lit);

    g1_tex_set_pack(NULL);
    g1_tex_unload();
    c0pack_close(&opened);
    free(pack);
    return 0;
}

static int test_settex_miss_stays_grey(void)
{
    uint8_t vtx_be[48];
    unsigned grey;

    g1_tex_unload();
    g1_tex_set_pack(NULL);
    if (run_tex_dl(99, vtx_be) != 0)
        return fail("miss dl");
    if (g1_tex_ok_count() != 0)
        return fail("missing bank must not bind");
    if (g1_tex_miss_count() < 1)
        return fail("missing bank must count miss");
    grey = (unsigned)count_tone(160, 200);
    if (grey < 1000)
        return fail("unbound SETTEX should stay vertex grey");
    printf("settex miss stays grey pixels=%u miss=%u\n", grey, g1_tex_miss_count());
    return 0;
}


static void fill_ia8_checker(uint8_t *dst, int w, int h, uint8_t a, uint8_t b)
{
    int y, x;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            dst[y * w + x] = ((x ^ y) & 1) ? b : a;
}

static size_t build_rare_ia8(uint8_t *dst, const uint8_t *tex, unsigned w, unsigned h)
{
    dst[0] = 0x01;
    dst[1] = (uint8_t)((5u << 4) | ((w >> 4) & 0x0fu));
    dst[2] = (uint8_t)((w << 4) | ((h >> 4) & 0x0fu));
    dst[3] = (uint8_t)((h << 4) | 0u);
    memcpy(dst + 4, tex, (size_t)w * h);
    return 4u + (size_t)w * h;
}

static size_t build_rare_ia4(uint8_t *dst, const uint8_t *tex, unsigned w, unsigned h)
{
    size_t n = ((size_t)w * h + 1u) / 2u;
    dst[0] = 0x01;
    dst[1] = (uint8_t)((6u << 4) | ((w >> 4) & 0x0fu));
    dst[2] = (uint8_t)((w << 4) | ((h >> 4) & 0x0fu));
    dst[3] = (uint8_t)((h << 4) | 0u);
    memcpy(dst + 4, tex, n);
    return 4u + n;
}

static int test_settex_ia_formats(void)
{
    uint8_t ia8[64], ia4[32], vtx_be[48];
    int dark, lit;

    fill_ia8_checker(ia8, 8, 8, 0x2F, 0xEF);
    if (g1_tex_load_raw(11, G1_TEX_IA8, 8, 8, ia8, 64, NULL, 0) != 0)
        return fail("load ia8");
    if (run_tex_dl(11, vtx_be) != 0)
        return fail("ia8 dl");
    if (g1_tex_ok_count() < 1)
        return fail("ia8 settex not ok");
    dark = count_tone(0, 80);
    lit = count_tone(180, 255);
    if (dark < 200 || lit < 200)
        return fail("ia8 checker not both tones");
    printf("settex IA8 checker dark=%d lit=%d ok=%u\n", dark, lit, g1_tex_ok_count());

    fill_i4_checker(ia4, 8, 8, 0x5, 0xD);
    if (g1_tex_load_raw(12, G1_TEX_IA4, 8, 8, ia4, 32, NULL, 0) != 0)
        return fail("load ia4");
    if (run_tex_dl(12, vtx_be) != 0)
        return fail("ia4 dl");
    if (g1_tex_ok_count() < 1)
        return fail("ia4 settex not ok");
    dark = count_tone(0, 100);
    lit = count_tone(180, 255);
    if (dark < 200 || lit < 200)
        return fail("ia4 checker not both tones");
    printf("settex IA4 checker dark=%d lit=%d ok=%u\n", dark, lit, g1_tex_ok_count());

    {
        uint8_t rare[80];
        G1TexBankOut d;
        size_t n = build_rare_ia8(rare, ia8, 8, 8);
        if (g1_tex_bank_decode(rare, n, &d) != G1_TEX_BANK_OK || d.fmt != G1_TEX_IA8)
            return fail("decode rare IA8");
        if (g1_tex_load_bank(13, rare, n) != 0)
            return fail("load rare IA8");
        if (run_tex_dl(13, vtx_be) != 0)
            return fail("rare ia8 dl");
        dark = count_tone(0, 80);
        lit = count_tone(180, 255);
        if (dark < 200 || lit < 200)
            return fail("rare IA8 not sampled");
        printf("settex pack-like RARE-IA8 dark=%d lit=%d\n", dark, lit);
    }
    g1_tex_unload();
    return 0;
}

/* Two SETTEX ids in one DL: I8 checker then CI4 red/green. Last-wins would drop I8. */
static int test_settex_two_ids(void)
{
    uint8_t i8[64], ci4[32], host[0x400], gdl[128];
    uint16_t tlut[2];
    static Vtx vtx[6];
    static Mtx mv, proj;
    float id[4][4];
    int i, j, ngfx;
    const uint8_t *fb;
    unsigned n, red = 0, grn = 0, grey = 0;

    fill_i8_checker(i8, 8, 8, 0x20, 0xE0);
    fill_i4_checker(ci4, 8, 8, 0x0, 0x1);
    tlut[0] = 0xF801;
    tlut[1] = 0x07C1;
    g1_tex_unload();
    if (g1_tex_load_raw(1, G1_TEX_I8, 8, 8, i8, 64, NULL, 0) != 0)
        return fail("two-id load i8");
    if (g1_tex_load_raw(2, G1_TEX_CI4, 8, 8, ci4, 32, tlut, 2) != 0)
        return fail("two-id load ci4");

    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            id[i][j] = (i == j) ? 1.f : 0.f;
    g0_mtx_f2l(id, &mv);
    g0_mtx_f2l(id, &proj);
    memset(vtx, 0, sizeof vtx);
    vtx[0].v.ob[0] = -1;
    vtx[0].v.ob[1] = -1;
    vtx[0].v.tc[0] = 0;
    vtx[0].v.tc[1] = 0;
    vtx[1].v.ob[0] = 0;
    vtx[1].v.ob[1] = -1;
    vtx[1].v.tc[0] = 256;
    vtx[1].v.tc[1] = 0;
    vtx[2].v.ob[0] = -1;
    vtx[2].v.ob[1] = 1;
    vtx[2].v.tc[0] = 0;
    vtx[2].v.tc[1] = 256;
    vtx[3].v.ob[0] = 0;
    vtx[3].v.ob[1] = -1;
    vtx[3].v.tc[0] = 0;
    vtx[3].v.tc[1] = 0;
    vtx[4].v.ob[0] = 1;
    vtx[4].v.ob[1] = -1;
    vtx[4].v.tc[0] = 256;
    vtx[4].v.tc[1] = 0;
    vtx[5].v.ob[0] = 1;
    vtx[5].v.ob[1] = 1;
    vtx[5].v.tc[0] = 256;
    vtx[5].v.tc[1] = 256;

    memset(host, 0, sizeof host);
    wr_be_mtx(host + 0x200, &mv);
    wr_be_mtx(host + 0x240, &proj);
    for (i = 0; i < 6; i++)
        wr_be_vtx(host + 0x280 + (size_t)i * 16, &vtx[i]);

    ngfx = 0;
    wr_be32(gdl + ngfx * 8, ((uint32_t)G_SETFILLCOLOR << 24));
    wr_be32(gdl + ngfx * 8 + 4,
            GPACK_RGBA5551(12, 28, 48, 1) | (GPACK_RGBA5551(12, 28, 48, 1) << 16));
    ngfx++;
    wr_be32(gdl + ngfx * 8, ((uint32_t)G_FILLRECT << 24) | (G1_FB_W << 14) | (G1_FB_H << 2));
    wr_be32(gdl + ngfx * 8 + 4, 0);
    ngfx++;
    wr_be32(gdl + ngfx * 8,
            ((uint32_t)(uint8_t)G_MTX << 24) | ((G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH) << 16));
    wr_be32(gdl + ngfx * 8 + 4, 0x0F000200u);
    ngfx++;
    wr_be32(gdl + ngfx * 8,
            ((uint32_t)(uint8_t)G_MTX << 24) | ((G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH) << 16));
    wr_be32(gdl + ngfx * 8 + 4, 0x0F000240u);
    ngfx++;
    wr_be32(gdl + ngfx * 8, ((uint32_t)(uint8_t)G_VTX << 24) | (0x50 << 16));
    wr_be32(gdl + ngfx * 8 + 4, 0x0F000280u);
    ngfx++;
    wr_be32(gdl + ngfx * 8, 0xC0000003u);
    wr_be32(gdl + ngfx * 8 + 4, 1);
    ngfx++;
    wr_be32(gdl + ngfx * 8, 0xB1000002u);
    wr_be32(gdl + ngfx * 8 + 4, 0x00000010u);
    ngfx++;
    wr_be32(gdl + ngfx * 8, 0xC0000003u);
    wr_be32(gdl + ngfx * 8 + 4, 2);
    ngfx++;
    wr_be32(gdl + ngfx * 8, 0xB1000005u);
    wr_be32(gdl + ngfx * 8 + 4, 0x00000034u);
    ngfx++;
    wr_be32(gdl + ngfx * 8, (uint32_t)(uint8_t)G_ENDDL << 24);
    wr_be32(gdl + ngfx * 8 + 4, 0);
    ngfx++;

    g1_clear_lookat();
    g1_set_segment(0xF, (uintptr_t)host);
    if (g1_interpret_be_dl(gdl, (uint32_t)ngfx) != 0)
        return fail("two-id interpret");
    if (g1_tex_ok_count() < 2)
        return fail("two-id expected texOk>=2");
    fb = g1_fb_rgba();
    n = (unsigned)G1_FB_W * (unsigned)G1_FB_H;
    for (i = 0; i < (int)n; i++) {
        unsigned r = fb[i * 4], g = fb[i * 4 + 1], b = fb[i * 4 + 2];
        if (r > 160 && g < 80)
            red++;
        else if (g > 160 && r < 80)
            grn++;
        else if (r > 160 && g > 160 && b > 160)
            grey++;
        else if (r < 80 && g < 80 && b < 80 && (r | g | b))
            grey++;
    }
    if (red < 80 || grn < 80)
        return fail("two-id missing CI4 red/green (second SETTEX)");
    if (grey < 80)
        return fail("two-id missing I8 checker (first SETTEX last-wins)");
    printf("settex two-id I8+CI4 grey=%u red=%u green=%u ok=%u\n", grey, red, grn,
           g1_tex_ok_count());
    g1_tex_unload();
    return 0;
}

static int test_near_clip_floor(void)
{
    uint8_t vtx_be[48], gdl[40];
    Vtx vtx[3];
    int i, ngfx;
    unsigned nz;

    memset(vtx, 0, sizeof vtx);
    vtx[0].v.ob[0] = -80;
    vtx[0].v.ob[1] = -20;
    vtx[0].v.ob[2] = 40;
    vtx[1].v.ob[0] = 80;
    vtx[1].v.ob[1] = -20;
    vtx[1].v.ob[2] = -80;
    vtx[2].v.ob[0] = -80;
    vtx[2].v.ob[1] = -20;
    vtx[2].v.ob[2] = -80;
    for (i = 0; i < 3; i++) {
        vtx[i].v.cn[0] = 200;
        vtx[i].v.cn[1] = 200;
        vtx[i].v.cn[2] = 200;
        vtx[i].v.cn[3] = 255;
        wr_be_vtx(vtx_be + (size_t)i * 16, &vtx[i]);
    }
    ngfx = 0;
    wr_be32(gdl + ngfx * 8, ((uint32_t)(uint8_t)G_VTX << 24) | (0x20 << 16));
    wr_be32(gdl + ngfx * 8 + 4, VTX_SEG14);
    ngfx++;
    wr_be32(gdl + ngfx * 8, 0xB1000002u);
    wr_be32(gdl + ngfx * 8 + 4, 0x00000010u);
    ngfx++;
    wr_be32(gdl + ngfx * 8, (uint32_t)(uint8_t)G_ENDDL << 24);
    wr_be32(gdl + ngfx * 8 + 4, 0);
    ngfx++;

    g1_tex_unload();
    g1_set_lookat(0.f, 0.f, 0.f, 0.f);
    g1_set_segment(14, (uintptr_t)vtx_be);
    if (g1_interpret_be_dl(gdl, (uint32_t)ngfx) != 0)
        return fail("near-clip interpret");
    nz = g1_fb_nonzero();
    if (nz < 400)
        return fail("near-clip floor still discarded");
    if (nz > 70000)
        return fail("near-clip floor filled the FB (w-only sliver)");
    {
        const uint8_t *fb = g1_fb_rgba();
        unsigned top = 0, x, y;
        for (y = 0; y < 12; y++)
            for (x = 0; x < (unsigned)G1_FB_W; x++) {
                const uint8_t *px = fb + ((y * (unsigned)G1_FB_W + x) * 4u);
                if (px[0] | px[1] | px[2])
                    top++;
            }
        if (top > 80)
            return fail("near-clip floor painted the top strip (full-screen sliver)");
    }
    printf("near-clip floor fb_nonzero=%u (not fullscreen)\n", nz);
    g1_clear_lookat();
    return 0;
}

/* Crossing floor + distant red marker. w-only clip at 0.01 projected a
 * full-FB sliver that erased the marker (and the gun on the live canvas). */
static int test_near_clip_keeps_marker(void)
{
    uint8_t vtx_be[96], gdl[40];
    Vtx vtx[6];
    int i, ngfx;
    unsigned nz, red = 0, grey = 0, top = 0;
    const uint8_t *fb;

    memset(vtx, 0, sizeof vtx);
    vtx[0].v.ob[0] = -80;
    vtx[0].v.ob[1] = -20;
    vtx[0].v.ob[2] = 40;
    vtx[1].v.ob[0] = 80;
    vtx[1].v.ob[1] = -20;
    vtx[1].v.ob[2] = -80;
    vtx[2].v.ob[0] = -80;
    vtx[2].v.ob[1] = -20;
    vtx[2].v.ob[2] = -80;
    vtx[3].v.ob[0] = -10;
    vtx[3].v.ob[1] = 20;
    vtx[3].v.ob[2] = -80;
    vtx[4].v.ob[0] = 10;
    vtx[4].v.ob[1] = 20;
    vtx[4].v.ob[2] = -80;
    vtx[5].v.ob[0] = 0;
    vtx[5].v.ob[1] = 40;
    vtx[5].v.ob[2] = -80;
    for (i = 0; i < 3; i++) {
        /* cn=0 → interpreter vertex grey. SETTEX 99 misses. */
        vtx[i].v.cn[3] = 255;
        wr_be_vtx(vtx_be + (size_t)i * 16, &vtx[i]);
    }
    for (i = 3; i < 6; i++) {
        vtx[i].v.cn[0] = 255;
        vtx[i].v.cn[1] = 16;
        vtx[i].v.cn[2] = 16;
        vtx[i].v.cn[3] = 255;
        wr_be_vtx(vtx_be + (size_t)i * 16, &vtx[i]);
    }

    ngfx = 0;
    wr_be32(gdl + ngfx * 8, ((uint32_t)(uint8_t)G_VTX << 24) | (0x50 << 16));
    wr_be32(gdl + ngfx * 8 + 4, VTX_SEG14);
    ngfx++;
    wr_be32(gdl + ngfx * 8, 0xC0000003u);
    wr_be32(gdl + ngfx * 8 + 4, 99);
    ngfx++;
    wr_be32(gdl + ngfx * 8, 0xB1000052u);
    wr_be32(gdl + ngfx * 8 + 4, 0x00004310u);
    ngfx++;
    wr_be32(gdl + ngfx * 8, (uint32_t)(uint8_t)G_ENDDL << 24);
    wr_be32(gdl + ngfx * 8 + 4, 0);
    ngfx++;

    g1_tex_unload();
    g1_tex_set_pack(NULL);
    g1_set_lookat(0.f, 0.f, 0.f, 0.f);
    g1_set_segment(14, (uintptr_t)vtx_be);
    if (g1_interpret_be_dl(gdl, (uint32_t)ngfx) != 0)
        return fail("marker-clip interpret");
    if (g1_tex_ok_count() != 0 || g1_tex_miss_count() < 1)
        return fail("marker-clip SETTEX 99 must miss");
    nz = g1_fb_nonzero();
    fb = g1_fb_rgba();
    for (i = 0; i < G1_FB_W * G1_FB_H; i++) {
        unsigned r = fb[i * 4], g = fb[i * 4 + 1], b = fb[i * 4 + 2];
        int row = i / G1_FB_W;
        if (r > 200 && g < 40 && b < 40)
            red++;
        else if (r > 140 && r < 220 && g > 140 && g < 220 && b > 140 && b < 220)
            grey++;
        if (row < 12 && (r | g | b))
            top++;
    }
    if (grey < 400)
        return fail("SETTEX-miss floor must stay vertex grey");
    if (red < 40)
        return fail("near-clip sliver wiped the distant red marker");
    if (nz > 70000)
        return fail("near-clip floor filled the FB");
    if (top > 80)
        return fail("near-clip sliver painted the top strip");
    printf("near-clip keeps marker red=%u grey=%u nz=%u miss=%u\n", red, grey, nz,
           g1_tex_miss_count());
    g1_clear_lookat();
    return 0;
}

/* IA8 all-zero (I=0,A=0) over a red marker must not stamp black. */
static int test_ia_alpha0_no_black(void)
{
    uint8_t ia8[64], gdl[64];
    Vtx vtx[6];
    static Mtx mv, proj;
    float id[4][4];
    uint8_t host[0x400];
    int i, j, ngfx;
    unsigned red = 0;
    const uint8_t *fb;

    memset(ia8, 0, sizeof ia8);
    g1_tex_unload();
    if (g1_tex_load_raw(21, G1_TEX_IA8, 8, 8, ia8, 64, NULL, 0) != 0)
        return fail("load ia8 zero");

    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            id[i][j] = (i == j) ? 1.f : 0.f;
    g0_mtx_f2l(id, &mv);
    g0_mtx_f2l(id, &proj);
    memset(vtx, 0, sizeof vtx);
    /* Red marker, left half. */
    vtx[0].v.ob[0] = -1;
    vtx[0].v.ob[1] = -1;
    vtx[1].v.ob[0] = 0;
    vtx[1].v.ob[1] = -1;
    vtx[2].v.ob[0] = -1;
    vtx[2].v.ob[1] = 1;
    for (i = 0; i < 3; i++) {
        vtx[i].v.cn[0] = 255;
        vtx[i].v.cn[1] = 16;
        vtx[i].v.cn[2] = 16;
        vtx[i].v.cn[3] = 255;
    }
    /* IA overlay, right half — would be a black rect if A=0 wrote RGB. */
    vtx[3].v.ob[0] = 0;
    vtx[3].v.ob[1] = -1;
    vtx[4].v.ob[0] = 1;
    vtx[4].v.ob[1] = -1;
    vtx[5].v.ob[0] = 1;
    vtx[5].v.ob[1] = 1;
    for (i = 3; i < 6; i++) {
        vtx[i].v.cn[0] = 180;
        vtx[i].v.cn[1] = 180;
        vtx[i].v.cn[2] = 180;
        vtx[i].v.cn[3] = 255;
        vtx[i].v.tc[0] = 256;
        vtx[i].v.tc[1] = 256;
    }

    memset(host, 0, sizeof host);
    wr_be_mtx(host + 0x200, &mv);
    wr_be_mtx(host + 0x240, &proj);
    for (i = 0; i < 6; i++)
        wr_be_vtx(host + 0x280 + (size_t)i * 16, &vtx[i]);

    ngfx = 0;
    wr_be32(gdl + ngfx * 8, ((uint32_t)(uint8_t)G_MTX << 24) |
                                ((G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH) << 16));
    wr_be32(gdl + ngfx * 8 + 4, 0x0F000200u);
    ngfx++;
    wr_be32(gdl + ngfx * 8, ((uint32_t)(uint8_t)G_MTX << 24) |
                                ((G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH) << 16));
    wr_be32(gdl + ngfx * 8 + 4, 0x0F000240u);
    ngfx++;
    wr_be32(gdl + ngfx * 8, ((uint32_t)(uint8_t)G_VTX << 24) | (0x50 << 16));
    wr_be32(gdl + ngfx * 8 + 4, 0x0F000280u);
    ngfx++;
    wr_be32(gdl + ngfx * 8, 0xB1000002u);
    wr_be32(gdl + ngfx * 8 + 4, 0x00000010u);
    ngfx++;
    wr_be32(gdl + ngfx * 8, 0xC0000003u);
    wr_be32(gdl + ngfx * 8 + 4, 21);
    ngfx++;
    wr_be32(gdl + ngfx * 8, 0xB1000005u);
    wr_be32(gdl + ngfx * 8 + 4, 0x00000034u);
    ngfx++;
    wr_be32(gdl + ngfx * 8, (uint32_t)(uint8_t)G_ENDDL << 24);
    wr_be32(gdl + ngfx * 8 + 4, 0);
    ngfx++;

    g1_clear_lookat();
    g1_set_segment(0xF, (uintptr_t)host);
    if (g1_interpret_be_dl(gdl, (uint32_t)ngfx) != 0)
        return fail("ia0 interpret");
    fb = g1_fb_rgba();
    for (i = 0; i < G1_FB_W * G1_FB_H; i++) {
        unsigned r = fb[i * 4], g = fb[i * 4 + 1], b = fb[i * 4 + 2];
        if (r > 200 && g < 40 && b < 40)
            red++;
    }
    if (red < 80)
        return fail("IA alpha-0 stamped over the red marker (or marker missing)");
    printf("ia alpha0 no-black red=%u ok=%u\n", red, g1_tex_ok_count());
    g1_tex_unload();
    return 0;
}

static int test_secondary_gdl(void)
{
    uint8_t pri[32], sec[32], bg[0x300], stan[256];
    C0File files[2];
    uint8_t *pack = NULL;
    size_t pack_len = 0, plen, slen;
    uint8_t hash[32];
    unsigned nz;
    Vtx vtx[3];
    uint8_t raw_v[48], wrapped_v[80], raw_p[40], raw_s[40], wp[80], ws[80];
    int i, ng;

#define OFF_SEC 0x1C0
    memset(bg, 0, sizeof bg);
    memset(stan, 0, sizeof stan);
    wr_be32(stan + 4, 0x0F000080u);
    wr_be32(bg + 0, 0);
    wr_be32(bg + 4, SEG(OFF_ROOMS));
    wr_be32(bg + 8, SEG(OFF_PORTAL));
    wr_be32(bg + OFF_ROOM1 + 0, SEG(OFF_VTX));
    wr_be32(bg + OFF_ROOM1 + 4, SEG(OFF_GDL));
    wr_be32(bg + OFF_ROOM1 + 8, SEG(OFF_SEC));

    memset(vtx, 0, sizeof vtx);
    vtx[0].v.ob[0] = -40;
    vtx[0].v.ob[2] = -80;
    vtx[1].v.ob[0] = 40;
    vtx[1].v.ob[2] = -80;
    vtx[2].v.ob[1] = 40;
    vtx[2].v.ob[2] = -80;
    for (i = 0; i < 3; i++) {
        vtx[i].v.cn[0] = 220;
        vtx[i].v.cn[1] = 40;
        vtx[i].v.cn[2] = 40;
        vtx[i].v.cn[3] = 255;
        wr_be_vtx(raw_v + (size_t)i * 16, &vtx[i]);
    }
    plen = wrap1172_stored(raw_v, sizeof raw_v, wrapped_v, sizeof wrapped_v);
    if (!plen || OFF_VTX + plen > OFF_GDL)
        return fail("sec vtx wrap");
    memcpy(bg + OFF_VTX, wrapped_v, plen);

    ng = 0;
    wr_be32(raw_p + ng * 8, ((uint32_t)(uint8_t)G_VTX << 24) | (0x20 << 16));
    wr_be32(raw_p + ng * 8 + 4, VTX_SEG14);
    ng++;
    wr_be32(raw_p + ng * 8, 0xB1000002u);
    wr_be32(raw_p + ng * 8 + 4, 0x00000010u);
    ng++;
    wr_be32(raw_p + ng * 8, (uint32_t)(uint8_t)G_ENDDL << 24);
    wr_be32(raw_p + ng * 8 + 4, 0);
    ng++;
    plen = wrap1172_stored(raw_p, (size_t)ng * 8u, wp, sizeof wp);
    if (!plen || OFF_GDL + plen > OFF_SEC)
        return fail("sec pri wrap");
    memcpy(bg + OFF_GDL, wp, plen);

    /* Secondary: a second TRI4 using the same verts (must still walk). */
    slen = wrap1172_stored(raw_p, (size_t)ng * 8u, ws, sizeof ws);
    if (!slen || OFF_SEC + slen > sizeof bg)
        return fail("sec wrap");
    memcpy(bg + OFF_SEC, ws, slen);

    files[0].path = "assets/obseg/bg/bg_ark_all_p.bin";
    files[0].bytes = bg;
    files[0].size = sizeof bg;
    files[1].path = "assets/obseg/stan/Tbg_ark_all_p_stanZ.bin";
    files[1].bytes = stan;
    files[1].size = sizeof stan;
    if (c0pack_build(files, 2, 0, 0, &pack, &pack_len, hash) != 0)
        return fail("sec pack build");
    if (port_api_init(pack, (uint32_t)pack_len, hash) != PORT_OK)
        return fail("sec port_api init");
    if (port_api_load_stage(PORT_LEVEL_FACILITY) != PORT_STAGE_OK)
        return fail("sec stage load");
    if (!port_stage_gdl_c0())
        return fail("sec expected pri c0");
    if (!port_stage_gdl_sec())
        return fail("sec expected inflated pSecMappingBin");
    port_api_draw();
    if (port_api_last_draw() != PORT_DRAW_STAGE)
        return fail("sec last_draw");
    nz = port_api_fb_nonzero();
    if (nz < 1000)
        return fail("sec draw still black");
    printf("secondary GDL inflated gdl_sec=1 fb_nonzero=%u\n", nz);
    port_api_shutdown();
    free(pack);
    return 0;
#undef OFF_SEC
}

int main(int argc, char **argv)
{
    uint8_t bg[BG_SIZE];
    uint8_t stan[256];
    uint8_t rare_bg[128];
    C0File files[2];
    uint8_t *pack = NULL;
    size_t pack_len = 0;
    uint8_t hash[32];
    uint64_t seed_a, seed_b;
    size_t n = 0;
    const uint8_t *loaded;
    const char *want = argc >= 2 ? argv[1] : NULL;
    int rc;
    size_t clen;
    uint8_t c0_bg[BG_SIZE];
    uint8_t gdl_plain[80];
    uint8_t gdl_again[80];
    size_t gdl_n = 0;

    if (test_inflate_bytes() != 0)
        return 1;

    memset(stan, 0, sizeof stan);
    wr_be32(stan + 4, 0x0F000080u);
    build_g1dl_bg(bg);

    files[0].path = "assets/obseg/bg/bg_ark_all_p.bin";
    files[0].bytes = bg;
    files[0].size = sizeof bg;
    files[1].path = "assets/obseg/stan/Tbg_ark_all_p_stanZ.bin";
    files[1].bytes = stan;
    files[1].size = sizeof stan;
    if (c0pack_build(files, 2, 0, 0, &pack, &pack_len, hash) != 0)
        return fail("build pack");
    if (port_init(pack, (uint32_t)pack_len) != PORT_PACK_OK)
        return fail("port_init");

    port_rng_begin_match(1);
    seed_a = g_randomSeed;
    seed_b = g_chrObjRandomSeed;

    rc = port_stage_load(PORT_LEVEL_FACILITY);
    if (rc != PORT_STAGE_OK) {
        fprintf(stderr, "load rc=%d %s\n", rc, port_stage_last_error());
        return fail("stage load");
    }
    if (port_stage_level_id() != PORT_LEVEL_FACILITY)
        return fail("level id");
    if (port_stage_room_count() != 1)
        return fail("stan room count");
    if (port_stage_bg_rooms() != 1)
        return fail("bg room count");
    if (!port_stage_gdl_raw())
        return fail("expected raw G1DL");
    loaded = port_stage_bg(&n);
    if (!loaded || n != sizeof bg)
        return fail("bg size");
    if (port_stage_stan_first_room() != (void *)(port_stage_stan(&n) + 0x80))
        return fail("stan first room");
    if (g_randomSeed == seed_a && g_chrObjRandomSeed == seed_b)
        return fail("K16 did not reseed");
    if (g_CurrentStageToLoad != PORT_LEVEL_FACILITY)
        return fail("g_CurrentStageToLoad");

    if (port_stage_draw() != 0)
        return fail("stage draw");
    if (check_grey(want) != 0)
        return 1;

    printf("stage facility rooms=%d bg_rooms=%d first=%p\n", port_stage_room_count(),
           port_stage_bg_rooms(), port_stage_stan_first_room());

    if (port_sim_tick(0) != 0)
        return fail("sim tick 0");
    if (speedgraphframes != 3 || g_ClockTimer != 3)
        return fail("clock pin");
    if (g_GlobalTimerDelta != 3.0f)
        return fail("dt");
    if (port_sim_tick(1) != 0)
        return fail("sim tick 1");
    if (g_GlobalTimer != 6)
        return fail("global timer");

    if (port_stage_load(99) != PORT_STAGE_ERR_FORMAT)
        return fail("unknown level");
    if (port_stage_load(PORT_LEVEL_COMPLEX) != PORT_STAGE_ERR_MISSING)
        return fail("complex missing");

    port_stage_unload();
    port_shutdown();
    free(pack);
    pack = NULL;

    /* Compressed C0 room: inflate + G_TRI4 must match the G1 greyscale hash. */
    clen = build_c0_compressed_bg(c0_bg);
    if (!clen)
        return fail("build c0 1172");
    if (bgDecompress(c0_bg + OFF_GDL, clen, gdl_again, sizeof gdl_again, &gdl_n) !=
        PORT_INFLATE1172_OK)
        return fail("c0 gdl inflate");
    /* First opcode after inflate is G_SETFILLCOLOR (0xF7). */
    if (gdl_n < 16 || gdl_again[0] != (uint8_t)G_SETFILLCOLOR)
        return fail("c0 gdl bytes");
    memcpy(gdl_plain, gdl_again, gdl_n);
    files[0].bytes = c0_bg;
    files[0].size = sizeof c0_bg;
    files[1].bytes = stan;
    files[1].size = sizeof stan;
    if (c0pack_build(files, 2, 0, 0, &pack, &pack_len, hash) != 0)
        return fail("build c0 pack");
    if (port_init(pack, (uint32_t)pack_len) != PORT_PACK_OK)
        return fail("port_init c0");
    if (port_stage_load(PORT_LEVEL_FACILITY) != PORT_STAGE_OK)
        return fail("c0 stage load");
    if (port_stage_bg_rooms() != 1)
        return fail("c0 bg rooms");
    if (port_stage_gdl_raw())
        return fail("c0 must not be G1DL magic");
    if (!port_stage_gdl_c0())
        return fail("expected inflated C0");
    if (port_stage_draw() != 0)
        return fail("c0 stage draw");
    if (check_grey(want) != 0)
        return 1;
    printf("stage c0 1172 inflated=%zu gdl_c0=1\n", gdl_n);

    /* port_api_draw must blit the inflated C0 stage FB, not only init synthetic. */
    port_stage_unload();
    port_shutdown();
    if (port_api_init(pack, (uint32_t)pack_len, hash) != PORT_OK)
        return fail(port_api_last_error()[0] ? port_api_last_error() : "port_api_init c0");
    if (port_api_load_stage(PORT_LEVEL_FACILITY) != PORT_STAGE_OK)
        return fail("port_api load c0");
    if (!port_api_gdl_c0())
        return fail("port_api expected gdl_c0");
    port_api_draw();
    if (port_api_last_draw() != PORT_DRAW_STAGE)
        return fail("port_api_draw did not use stage FB");
    if (check_grey(want) != 0)
        return 1;
    printf("port_api_draw c0 last_draw=%d\n", port_api_last_draw());
    port_api_shutdown();
    free(pack);
    pack = NULL;


    if (test_seg14_camera() != 0)
        return 1;

    /* Pack path: 1172 Vtx table + seg-14 G_VTX + player camera. */
    {
        uint8_t s14_bg[BG_SIZE];
        size_t s14_len;
        unsigned nz;
        s14_len = build_c0_seg14_bg(s14_bg);
        if (!s14_len)
            return fail("build c0 seg14");
        files[0].bytes = s14_bg;
        files[0].size = sizeof s14_bg;
        files[1].bytes = stan;
        files[1].size = sizeof stan;
        if (c0pack_build(files, 2, 0, 0, &pack, &pack_len, hash) != 0)
            return fail("build seg14 pack");
        if (port_api_init(pack, (uint32_t)pack_len, hash) != PORT_OK)
            return fail("port_api seg14 init");
        if (port_api_load_stage(PORT_LEVEL_FACILITY) != PORT_STAGE_OK)
            return fail("port_api seg14 load");
        if (!port_api_gdl_c0())
            return fail("seg14 expected gdl_c0");
        if (!port_api_gdl_vtx())
            return fail("seg14 expected inflated vtx table");
        port_api_draw();
        if (port_api_last_draw() != PORT_DRAW_STAGE)
            return fail("seg14 last_draw not STAGE");
        nz = port_api_fb_nonzero();
        if (nz < 1000)
            return fail("seg14 pack draw still black");
        printf("port_api_draw seg14 last_draw=%d gdl_vtx=1 fb_nonzero=%u\n",
               port_api_last_draw(), nz);
        port_api_shutdown();
        free(pack);
        pack = NULL;
    }

    /* Rare-shaped header (word0 == 0) without 1172: rooms walk, no interpret. */
    memset(rare_bg, 0, sizeof rare_bg);
    wr_be32(rare_bg + 4, 0x0F000040u);
    wr_be32(rare_bg + 0x58, 0x0F000070u); /* room1 pPointTable */
    wr_be32(rare_bg + 0x5C, 0x0F000070u); /* room1 pPriMapping (not G1DL) */
    files[0].bytes = rare_bg;
    files[0].size = sizeof rare_bg;
    files[1].bytes = stan;
    files[1].size = sizeof stan;
    if (c0pack_build(files, 2, 0, 0, &pack, &pack_len, hash) != 0)
        return fail("build rare pack");
    if (port_init(pack, (uint32_t)pack_len) != PORT_PACK_OK)
        return fail("port_init rare");
    if (port_stage_load(PORT_LEVEL_FACILITY) != PORT_STAGE_OK)
        return fail("rare stage load");
    if (port_stage_bg_rooms() != 1)
        return fail("rare bg rooms");
    if (port_stage_gdl_raw())
        return fail("rare must not look like raw G1DL");
    if (port_stage_draw() == 0)
        return fail("must not interpret compressed/unknown GDL");

    if (port_stage_gdl_c0())
        return fail("junk rare must not inflate");
    printf("stage ok rooms=%d bg_rooms=%d clock=%d dt=%g gdl_raw=%d gdl_c0=%d\n",
           port_stage_room_count(), port_stage_bg_rooms(), g_ClockTimer,
           (double)g_GlobalTimerDelta, port_stage_gdl_raw(), port_stage_gdl_c0());

    /* No drawable GDL: port_api_draw falls back. Presenter must not treat this as stage blit. */
    port_stage_unload();
    port_shutdown();
    if (port_api_init(pack, (uint32_t)pack_len, hash) != PORT_OK)
        return fail("port_api rare init");
    if (port_api_load_stage(PORT_LEVEL_FACILITY) != PORT_STAGE_OK)
        return fail("port_api rare load");
    if (port_api_gdl_raw() || port_api_gdl_c0())
        return fail("junk rare not drawable");
    port_api_draw();
    if (port_api_last_draw() != PORT_DRAW_FALLBACK)
        return fail("junk rare must fallback, not stage blit");
    printf("port_api_draw rare last_draw=%d (fallback)\n", port_api_last_draw());
    port_api_shutdown();
    free(pack);

    if (test_tex_bank_synthetic() != 0)
        return 1;
    if (test_settex_formats() != 0)
        return 1;
    if (test_settex_ia_formats() != 0)
        return 1;
    if (test_settex_two_ids() != 0)
        return 1;
    if (test_near_clip_floor() != 0)
        return 1;
    if (test_near_clip_keeps_marker() != 0)
        return 1;
    if (test_ia_alpha0_no_black() != 0)
        return 1;
    if (test_secondary_gdl() != 0)
        return 1;
    if (test_settex_pack_lookup() != 0)
        return 1;
    if (test_settex_rare_bank_pack() != 0)
        return 1;
    if (test_settex_miss_stays_grey() != 0)
        return 1;
    return 0;
}
