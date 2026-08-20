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
    wr_be16(dst + 8, 0);
    wr_be16(dst + 10, 0);
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
    return 0;
}
