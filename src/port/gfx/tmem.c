#include "gfx/tmem.h"
#include "gfx/tex_bank.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    int loaded;
    uint8_t fmt;
    uint8_t w, h;
    uint8_t cms, cmt;
    uint16_t id;
    uint8_t texels[G1_TMEM_BYTES];
    size_t ntex;
    uint16_t tlut[256];
    unsigned ntlut;
} Tile;

static Tile g_tile;
static int g_bound;
static float g_ss = 1.f, g_st = 1.f;
static const C0Pack *g_pack;
static unsigned g_settex_n, g_ok_n, g_miss_n;
static uint16_t g_last_id;

void g1_tex_begin_dl(void)
{
    g_bound = 0;
    g_ss = 1.f;
    g_st = 1.f;
    g_settex_n = 0;
    g_ok_n = 0;
    g_miss_n = 0;
    g_last_id = 0;
}

void g1_tex_set_pack(const C0Pack *pack) { g_pack = pack; }

void g1_tex_set_scale(float s, float t)
{
    g_ss = (s > 0.0001f) ? s : 1.f;
    g_st = (t > 0.0001f) ? t : 1.f;
}

void g1_tex_unload(void)
{
    memset(&g_tile, 0, sizeof g_tile);
    g_bound = 0;
}

static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t rd_be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static int fmt_ok(uint8_t fmt)
{
    return fmt == G1_TEX_I4 || fmt == G1_TEX_I8 || fmt == G1_TEX_CI4 || fmt == G1_TEX_CI8 ||
           fmt == G1_TEX_RGBA16;
}

static size_t texel_need(uint8_t fmt, unsigned w, unsigned h)
{
    unsigned n = w * h;
    if (fmt == G1_TEX_I4 || fmt == G1_TEX_CI4)
        return (n + 1u) / 2u;
    if (fmt == G1_TEX_I8 || fmt == G1_TEX_CI8)
        return n;
    if (fmt == G1_TEX_RGBA16)
        return n * 2u;
    return 0;
}

int g1_tex_load_raw(unsigned id, uint8_t fmt, unsigned w, unsigned h, const uint8_t *texels,
                    size_t ntex, const uint16_t *tlut, unsigned ntlut)
{
    size_t need;
    if (!texels || !w || !h || w > 64 || h > 64 || !fmt_ok(fmt))
        return -1;
    need = texel_need(fmt, w, h);
    if (!need || ntex < need || need > G1_TMEM_BYTES)
        return -1;
    if ((fmt == G1_TEX_CI4 || fmt == G1_TEX_CI8) && (!tlut || ntlut == 0))
        return -1;
    memset(&g_tile, 0, sizeof g_tile);
    g_tile.loaded = 1;
    g_tile.fmt = fmt;
    g_tile.w = (uint8_t)w;
    g_tile.h = (uint8_t)h;
    g_tile.id = (uint16_t)id;
    g_tile.ntex = need;
    memcpy(g_tile.texels, texels, need);
    if (tlut && ntlut) {
        if (ntlut > 256)
            ntlut = 256;
        memcpy(g_tile.tlut, tlut, ntlut * sizeof(uint16_t));
        g_tile.ntlut = ntlut;
    }
    return 0;
}

int g1_tex_load_sitx(unsigned id, const uint8_t *bytes, size_t n)
{
    uint8_t fmt, w, h;
    uint32_t tbytes;
    const uint8_t *tex;
    uint16_t tlut[256];
    unsigned ntlut = 0;
    size_t off;

    if (!bytes || n < 12 || memcmp(bytes, G1_SITX_MAGIC, 4) != 0)
        return -1;
    fmt = bytes[4];
    w = bytes[5];
    h = bytes[6];
    tbytes = rd_be32(bytes + 8);
    if ((size_t)12 + tbytes > n)
        return -1;
    tex = bytes + 12;
    off = 12 + tbytes;
    if (fmt == G1_TEX_CI4 || fmt == G1_TEX_CI8) {
        uint32_t nc, i;
        if (off + 4 > n)
            return -1;
        nc = rd_be32(bytes + off);
        off += 4;
        if (nc > 256 || off + nc * 2u > n)
            return -1;
        for (i = 0; i < nc; i++)
            tlut[i] = rd_be16(bytes + off + i * 2u);
        ntlut = (unsigned)nc;
    }
    return g1_tex_load_raw(id, fmt, w, h, tex, tbytes, ntlut ? tlut : NULL, ntlut);
}

static int load_from_pack(unsigned id)
{
    char path[160];
    const C0PackEntry *e = NULL;
    const char *name;

    if (!g_pack)
        return -1;
    name = g1_image_bank_name(id);
    if (name) {
        snprintf(path, sizeof path, "assets/images/split/%s.bin", name);
        e = c0pack_find(g_pack, path);
    }
    if (!e) {
        snprintf(path, sizeof path, "assets/images/split/image%u.bin", id);
        e = c0pack_find(g_pack, path);
    }
    if (!e || !e->bytes || e->size == 0)
        return -1;
    if (g1_tex_load_sitx(id, e->bytes, e->size) == 0)
        return 0;
    return g1_tex_load_bank(id, e->bytes, e->size);
}

int g1_tex_settex(uint32_t w0, uint32_t w1)
{
    unsigned id = (unsigned)(w1 & 0xfffu);
    uint8_t cms = (uint8_t)((w0 >> 22) & 3u);
    uint8_t cmt = (uint8_t)((w0 >> 20) & 3u);

    g_settex_n++;
    g_last_id = (uint16_t)id;
    if (!(g_tile.loaded && g_tile.id == (uint16_t)id)) {
        if (load_from_pack(id) != 0) {
            g_bound = 0;
            g_miss_n++;
            return 0;
        }
    }
    g_tile.cms = cms;
    g_tile.cmt = cmt;
    g_bound = 1;
    g_ok_n++;
    return 1;
}

int g1_tex_bound(void) { return g_bound && g_tile.loaded; }

unsigned g1_tex_settex_count(void) { return g_settex_n; }
unsigned g1_tex_ok_count(void) { return g_ok_n; }
unsigned g1_tex_miss_count(void) { return g_miss_n; }
uint16_t g1_tex_last_id(void) { return g_last_id; }

static int wrap_coord(int x, int size, int mode)
{
    int p;
    if (size <= 0)
        return 0;
    if (mode == 2) {
        if (x < 0)
            return 0;
        if (x >= size)
            return size - 1;
        return x;
    }
    if (mode == 1) {
        p = size * 2;
        x %= p;
        if (x < 0)
            x += p;
        if (x >= size)
            x = p - 1 - x;
        return x;
    }
    x %= size;
    if (x < 0)
        x += size;
    return x;
}

static void rgba5551(uint16_t c, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    unsigned r5 = (c >> 11) & 0x1f, g5 = (c >> 6) & 0x1f, b5 = (c >> 1) & 0x1f;
    *r = (uint8_t)((r5 << 3) | (r5 >> 2));
    *g = (uint8_t)((g5 << 3) | (g5 >> 2));
    *b = (uint8_t)((b5 << 3) | (b5 >> 2));
    *a = (c & 1) ? 255 : 0;
}

int g1_tex_sample(float s, float t, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    int x, y, idx;
    unsigned i;
    uint8_t pix;

    if (!g1_tex_bound())
        return 0;
    /* Vtx.tc is 10.5; gSPTexture scale is 0..1 (0xffff ~ 1). */
    x = (int)(s * g_ss / 32.f);
    y = (int)(t * g_st / 32.f);
    x = wrap_coord(x, g_tile.w, g_tile.cms);
    y = wrap_coord(y, g_tile.h, g_tile.cmt);
    idx = y * (int)g_tile.w + x;
    if (g_tile.fmt == G1_TEX_I8) {
        pix = g_tile.texels[idx];
        *r = *g = *b = pix;
        *a = 255;
        return 1;
    }
    if (g_tile.fmt == G1_TEX_I4) {
        pix = g_tile.texels[idx >> 1];
        pix = (idx & 1) ? (uint8_t)(pix & 0x0f) : (uint8_t)(pix >> 4);
        pix = (uint8_t)(pix * 17u);
        *r = *g = *b = pix;
        *a = 255;
        return 1;
    }
    if (g_tile.fmt == G1_TEX_CI4 || g_tile.fmt == G1_TEX_CI8) {
        if (g_tile.fmt == G1_TEX_CI8)
            i = g_tile.texels[idx];
        else {
            pix = g_tile.texels[idx >> 1];
            i = (idx & 1) ? (unsigned)(pix & 0x0f) : (unsigned)(pix >> 4);
        }
        if (i >= g_tile.ntlut)
            i = 0;
        rgba5551(g_tile.tlut[i], r, g, b, a);
        return 1;
    }
    if (g_tile.fmt == G1_TEX_RGBA16) {
        uint16_t c = (uint16_t)((g_tile.texels[idx * 2] << 8) | g_tile.texels[idx * 2 + 1]);
        rgba5551(c, r, g, b, a);
        return 1;
    }
    return 0;
}
