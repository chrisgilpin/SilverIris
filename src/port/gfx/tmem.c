#include "gfx/tmem.h"
#include "gfx/tex_bank.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    int loaded;
    uint8_t fmt;
    uint8_t w, h;
    uint8_t cms, cmt;
    uint8_t tlut_ia;
    uint16_t id;
    uint8_t texels[G1_TEX_MAX_BYTES];
    size_t ntex;
    uint16_t tlut[256];
    unsigned ntlut;
    float ss, st;
    unsigned stamp;
} Tile;

static Tile g_slots[G1_TEX_SLOTS];
static int g_cur = -1;
static float g_ss = 1.f, g_st = 1.f;
static const C0Pack *g_pack;
static unsigned g_settex_n, g_ok_n, g_miss_n, g_miss_abs, g_miss_dec, g_use;
static unsigned g_frame_base;
static uint8_t g_last_miss_why;
static uint16_t g_last_id;

void g1_tex_begin_dl(void)
{
    /* Slots stamped after this must not be evicted: GIR_DRAW_TRIS keep the
     * integer slot they were emitted with. A later SETTEX that reused that
     * slot would sample A=0 / wrong texels and punch the FB back to clear.
     */
    g_frame_base = g_use;
    g_cur = -1;
    g_ss = 1.f;
    g_st = 1.f;
    g_settex_n = 0;
    g_ok_n = 0;
    g_miss_n = 0;
    g_miss_abs = 0;
    g_miss_dec = 0;
    g_last_id = 0;
    g_last_miss_why = 0;
}

void g1_tex_set_pack(const C0Pack *pack) { g_pack = pack; }

void g1_tex_set_scale(float s, float t)
{
    g_ss = (s > 0.0001f) ? s : 1.f;
    g_st = (t > 0.0001f) ? t : 1.f;
}

void g1_tex_unload(void)
{
    memset(g_slots, 0, sizeof g_slots);
    g_cur = -1;
    g_use = 0;
}

static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t rd_be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static int fmt_ok(uint8_t fmt)
{
    return fmt <= G1_TEX_IA16_CI4;
}

static size_t texel_need(uint8_t fmt, unsigned w, unsigned h)
{
    unsigned n = w * h;
    switch (fmt) {
    case G1_TEX_I4:
    case G1_TEX_CI4:
    case G1_TEX_IA4:
    case G1_TEX_IA16_CI4:
        return (n + 1u) / 2u;
    case G1_TEX_I8:
    case G1_TEX_CI8:
    case G1_TEX_IA8:
    case G1_TEX_IA16_CI8:
        return n;
    case G1_TEX_RGBA16:
    case G1_TEX_RGB15:
    case G1_TEX_IA16:
        return n * 2u;
    case G1_TEX_RGB24:
        return n * 3u;
    case G1_TEX_RGBA32:
        return n * 4u;
    default:
        return 0;
    }
}

static int find_slot(unsigned id)
{
    int i;
    for (i = 0; i < G1_TEX_SLOTS; i++) {
        if (g_slots[i].loaded && g_slots[i].id == (uint16_t)id)
            return i;
    }
    return -1;
}

static int alloc_slot(void)
{
    int i, best = -1;
    unsigned oldest = 0xffffffffu;

    for (i = 0; i < G1_TEX_SLOTS; i++) {
        if (!g_slots[i].loaded)
            return i;
    }
    /* Reuse only tiles not bound since g1_tex_begin_dl. Evicting a
     * this-frame slot rewrites texels under already-emitted tris. */
    for (i = 0; i < G1_TEX_SLOTS; i++) {
        if (g_slots[i].stamp > g_frame_base)
            continue;
        if (g_slots[i].stamp < oldest) {
            oldest = g_slots[i].stamp;
            best = i;
        }
    }
    return best;
}

int g1_tex_load_raw(unsigned id, uint8_t fmt, unsigned w, unsigned h, const uint8_t *texels,
                    size_t ntex, const uint16_t *tlut, unsigned ntlut)
{
    size_t need;
    int slot;
    Tile *t;
    uint8_t store = fmt;

    if (!texels || !w || !h || w > G1_TEX_MAX_W || h > G1_TEX_MAX_H || !fmt_ok(fmt))
        return -1;
    need = texel_need(fmt, w, h);
    if (!need || ntex < need || need > G1_TEX_MAX_BYTES)
        return -1;
    if ((fmt == G1_TEX_CI4 || fmt == G1_TEX_CI8 || fmt == G1_TEX_IA16_CI4 ||
         fmt == G1_TEX_IA16_CI8) &&
        (!tlut || ntlut == 0))
        return -1;

    slot = find_slot(id);
    if (slot < 0)
        slot = alloc_slot();
    if (slot < 0)
        return -1;
    t = &g_slots[slot];
    memset(t, 0, sizeof *t);
    t->loaded = 1;
    if (fmt == G1_TEX_IA16_CI8) {
        store = G1_TEX_CI8;
        t->tlut_ia = 1;
    } else if (fmt == G1_TEX_IA16_CI4) {
        store = G1_TEX_CI4;
        t->tlut_ia = 1;
    }
    t->fmt = store;
    t->w = (uint8_t)w;
    t->h = (uint8_t)h;
    t->id = (uint16_t)id;
    t->ntex = need;
    t->ss = g_ss;
    t->st = g_st;
    t->stamp = ++g_use;
    memcpy(t->texels, texels, need);
    if (tlut && ntlut) {
        if (ntlut > 256)
            ntlut = 256;
        memcpy(t->tlut, tlut, ntlut * sizeof(uint16_t));
        t->ntlut = ntlut;
    }
    g_cur = slot;
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
    if (fmt == G1_TEX_CI4 || fmt == G1_TEX_CI8 || fmt == G1_TEX_IA16_CI4 ||
        fmt == G1_TEX_IA16_CI8) {
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
    /* Pack extract names files by TextureID (`1916.bin`). images.def names
     * are an index; numeric fallback is dump-verified, not a palette invent. */
    if (!e) {
        snprintf(path, sizeof path, "assets/images/split/%u.bin", id);
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
    if (g1_tex_load_bank(id, e->bytes, e->size) == 0)
        return 0;
    return -2; /* in pack, decode failed */
}

int g1_tex_settex(uint32_t w0, uint32_t w1)
{
    unsigned id = (unsigned)(w1 & 0xfffu);
    uint8_t cms = (uint8_t)((w0 >> 22) & 3u);
    uint8_t cmt = (uint8_t)((w0 >> 20) & 3u);
    int slot;

    /* F3D G_NOOP is also 0xC0. gsSPUseTexture(id=0) would be COPYICON;
     * a zeroed 0xC0000000/0 is a no-op, not texture 0. */
    if (w0 == 0xC0000000u && w1 == 0u)
        return 0;

    g_settex_n++;
    g_last_id = (uint16_t)id;
    slot = find_slot(id);
    if (slot < 0) {
        {
            int rc = load_from_pack(id);
            if (rc != 0) {
                /* Miss unbinds only subsequent tris; already-emitted slots stay. */
                g_cur = -1;
                g_miss_n++;
                if (rc == -2) {
                    g_miss_dec++;
                    g_last_miss_why = G1_TEX_MISS_DECODE;
                } else {
                    g_miss_abs++;
                    g_last_miss_why = G1_TEX_MISS_ABSENT;
                }
                return 0;
            }
        }
        slot = g_cur;
    }
    if (slot < 0) {
        g_cur = -1;
        g_miss_n++;
        return 0;
    }
    g_slots[slot].cms = cms;
    g_slots[slot].cmt = cmt;
    g_slots[slot].ss = g_ss;
    g_slots[slot].st = g_st;
    g_slots[slot].stamp = ++g_use;
    g_cur = slot;
    g_ok_n++;
    return 1;
}

int g1_tex_bound(void) { return g_cur >= 0 && g_slots[g_cur].loaded; }

int g1_tex_current_slot(void) { return g1_tex_bound() ? g_cur : -1; }

unsigned g1_tex_settex_count(void) { return g_settex_n; }
unsigned g1_tex_ok_count(void) { return g_ok_n; }
unsigned g1_tex_miss_count(void) { return g_miss_n; }
unsigned g1_tex_miss_absent_count(void) { return g_miss_abs; }
unsigned g1_tex_miss_decode_count(void) { return g_miss_dec; }
uint16_t g1_tex_last_id(void) { return g_last_id; }
uint8_t g1_tex_last_miss_why(void) { return g_last_miss_why; }

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

int g1_tex_sample_slot(int slot, float s, float t, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    Tile *tile;
    int x, y, idx;
    unsigned i;
    uint8_t pix;

    if (slot < 0 || slot >= G1_TEX_SLOTS || !g_slots[slot].loaded)
        return 0;
    tile = &g_slots[slot];
    /* Vtx.tc is 10.5; gSPTexture scale is 0..1 (0xffff ~ 1). Snapshot at SETTEX. */
    x = (int)(s * tile->ss / 32.f);
    y = (int)(t * tile->st / 32.f);
    x = wrap_coord(x, tile->w, tile->cms);
    y = wrap_coord(y, tile->h, tile->cmt);
    idx = y * (int)tile->w + x;
    if (tile->fmt == G1_TEX_I8) {
        pix = tile->texels[idx];
        *r = *g = *b = pix;
        *a = 255;
        return 1;
    }
    if (tile->fmt == G1_TEX_I4) {
        pix = tile->texels[idx >> 1];
        pix = (idx & 1) ? (uint8_t)(pix & 0x0f) : (uint8_t)(pix >> 4);
        pix = (uint8_t)(pix * 17u);
        *r = *g = *b = pix;
        *a = 255;
        return 1;
    }
    if (tile->fmt == G1_TEX_IA8) {
        pix = tile->texels[idx];
        *r = *g = *b = (uint8_t)((pix >> 4) * 17u);
        *a = (uint8_t)((pix & 0x0f) * 17u);
        return 1;
    }
    if (tile->fmt == G1_TEX_IA4) {
        pix = tile->texels[idx >> 1];
        pix = (idx & 1) ? (uint8_t)(pix & 0x0f) : (uint8_t)(pix >> 4);
        *r = *g = *b = (uint8_t)(((pix >> 1) * 255u) / 7u);
        *a = (pix & 1) ? 255 : 0;
        return 1;
    }
    if (tile->fmt == G1_TEX_IA16) {
        *r = *g = *b = tile->texels[idx * 2];
        *a = tile->texels[idx * 2 + 1];
        return 1;
    }
    if (tile->fmt == G1_TEX_CI4 || tile->fmt == G1_TEX_CI8) {
        if (tile->fmt == G1_TEX_CI8)
            i = tile->texels[idx];
        else {
            pix = tile->texels[idx >> 1];
            i = (idx & 1) ? (unsigned)(pix & 0x0f) : (unsigned)(pix >> 4);
        }
        if (i >= tile->ntlut)
            i = 0;
        if (tile->tlut_ia) {
            uint16_t c = tile->tlut[i];
            *r = *g = *b = (uint8_t)(c >> 8);
            *a = (uint8_t)c;
        } else {
            rgba5551(tile->tlut[i], r, g, b, a);
        }
        return 1;
    }
    if (tile->fmt == G1_TEX_RGBA16 || tile->fmt == G1_TEX_RGB15) {
        uint16_t c = (uint16_t)((tile->texels[idx * 2] << 8) | tile->texels[idx * 2 + 1]);
        rgba5551(c, r, g, b, a);
        if (tile->fmt == G1_TEX_RGB15)
            *a = 255;
        return 1;
    }
    if (tile->fmt == G1_TEX_RGB24) {
        *r = tile->texels[idx * 3];
        *g = tile->texels[idx * 3 + 1];
        *b = tile->texels[idx * 3 + 2];
        *a = 255;
        return 1;
    }
    if (tile->fmt == G1_TEX_RGBA32) {
        *r = tile->texels[idx * 4];
        *g = tile->texels[idx * 4 + 1];
        *b = tile->texels[idx * 4 + 2];
        *a = tile->texels[idx * 4 + 3];
        return 1;
    }
    return 0;
}

int g1_tex_sample(float s, float t, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    return g1_tex_sample_slot(g_cur, s, t, r, g, b, a);
}
