#include "prop.h"

#include "c0pack.h"
#include "inflate1172.h"
#include "pack_dma.h"
#include "stage.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT_MAX_PROPS 256
#define PORT_MAX_MODELS 48
#define PORT_MODEL_SEG 5
#define PORT_MODEL_BASE 0x05000000u
#define PORT_PAD_BYTES 44
#define PORT_BOUND_BYTES 68
#define PORT_BOUND_BASE 10000
#define PORT_SETUP_PTRS 10
#define INTRO_SPAWN 0
#define INTRO_END 9
#define INTRO_MAX 10
#define PORT_PROP_NEAR 4000.f
#define PORT_NODE_MAX 96
#define PORT_GDL_MAX 512
#define TEXREC_BYTES 12
#define NODE_BYTES 24

#define PDEF_DOOR 1
#define PDEF_PROP 3
#define PDEF_ALARM 5
#define PDEF_RACK 12
#define PDEF_GAS 36
#define PDEF_GLASS 42
#define PDEF_TINTED 47
#define PDEF_END 48
#define PDEF_MAX 49

typedef struct {
    uint16_t id;
    uint16_t nswitch;
    uint16_t ntex;
    float scale;
    const char *name;
} PortPropCat;

#include "prop_catalog.inc.c"

typedef struct {
    int id;
    uint8_t *file;
    size_t file_len;
    const uint8_t *pri;
    uint32_t pri_n;
    const uint8_t *sec;
    uint32_t sec_n;
} PortModel;

typedef struct {
    int type;
    int model;
    int pad;
    float pos[3];
    float look[3];
    float scale;
    float yaw;
    PortModel *mdl;
} PortProp;

static uint8_t *g_setup;
static size_t g_setup_len;
static PortProp g_prop[PORT_MAX_PROPS];
static int g_nprop;
static PortModel g_mdl[PORT_MAX_MODELS];
static int g_nmdl;
static int g_drawn;
static int g_intro_pad = -1;
static int g_have_intro;
static float g_intro_pos[3];
static float g_intro_look[3];

static const uint8_t k_intro_words[INTRO_MAX] = {
    3, 4, 4, 8, 2, 2, 10, 3, 2, 1,
};

static const uint8_t k_pdef_words[PDEF_MAX] = {
    1,  64, 2,  32, 33, 32, 0x3b, 0x21, 0x22, 7,  0x40, 0x95, 32, 0x36, 3,
    32, 1,  32, 3,  4,  0x2d, 0x22, 4,  4,    1,  2,    2,    2,  2,    2,
    4,  1,  4,  5,  1,  4,    32,  10,  4,    0x2c, 0x2d, 1,  32, 32,   5,
    0x38, 7, 37, 1,
};

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static float be_f32(const uint8_t *p)
{
    uint32_t u = be32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static int scenery_type(int t)
{
    return t == PDEF_DOOR || t == PDEF_PROP || t == PDEF_ALARM || t == PDEF_RACK ||
           t == PDEF_GAS || t == PDEF_GLASS || t == PDEF_TINTED;
}

static const PortPropCat *cat_by_id(int id)
{
    if (id < 0 || id >= PORT_PROP_CAT_N)
        return NULL;
    return &k_prop_cat[id];
}

static uint32_t file_off(uint32_t p, size_t n)
{
    uint32_t off;
    if (!p)
        return 0;
    if ((p & 0xFF000000u) == PORT_MODEL_BASE)
        off = p & 0x00FFFFFFu;
    else
        off = p;
    if (off >= n)
        return 0;
    return off;
}

static uint32_t gdl_count(const uint8_t *base, size_t n, uint32_t off)
{
    uint32_t i;
    if (!base || off + 8 > n)
        return 0;
    for (i = 0; i < PORT_GDL_MAX && off + (i + 1u) * 8u <= n; i++) {
        if (base[off + i * 8u] == (uint8_t)0xDF)
            return i + 1u;
    }
    return i;
}

static int walk_nodes(const uint8_t *base, size_t n, uint32_t root, uint32_t *pri,
                      uint32_t *sec)
{
    uint32_t q[PORT_NODE_MAX];
    uint8_t seen[PORT_NODE_MAX];
    int qh = 0, qt = 0, i;

    *pri = 0;
    *sec = 0;
    if (root + NODE_BYTES > n)
        return -1;
    memset(seen, 0, sizeof seen);
    q[qt++] = root;
    while (qh < qt) {
        uint32_t off = q[qh++];
        uint8_t op;
        uint32_t data, child, next;
        if (off + NODE_BYTES > n)
            continue;
        op = base[off + 1];
        data = file_off(be32(base + off + 4), n);
        child = file_off(be32(base + off + 20), n);
        next = file_off(be32(base + off + 12), n);
        if ((op == 4 || op == 22 || op == 24) && data) {
            uint32_t p = 0, s = 0;
            if (op == 24 && data + 8 <= n) {
                p = file_off(be32(base + data), n);
                s = file_off(be32(base + data + 4), n);
            } else if (op == 4 && data + 8 <= n) {
                p = file_off(be32(base + data), n);
                s = file_off(be32(base + data + 4), n);
            } else if (op == 22 && data + 12 <= n) {
                p = file_off(be32(base + data + 8), n);
            }
            if (p && !*pri) {
                *pri = p;
                *sec = s;
            }
        }
        for (i = 0; i < 2; i++) {
            uint32_t c = i ? next : child;
            int j, have = 0;
            if (!c)
                continue;
            for (j = 0; j < qt; j++) {
                if (q[j] == c) {
                    have = 1;
                    break;
                }
            }
            if (!have && qt < PORT_NODE_MAX)
                q[qt++] = c;
        }
        (void)seen;
    }
    return *pri ? 0 : -1;
}

static int bind_model_gdl(PortModel *m)
{
    uint32_t pri = 0, sec = 0, root;
    const PortPropCat *cat;

    if (!m->file || m->file_len < 8)
        return -1;
    if (be32(m->file) == PORT_BG_MAGIC_G1DL) {
        m->pri = m->file + 4;
        m->pri_n = gdl_count(m->file, m->file_len, 4);
        return m->pri_n ? 0 : -1;
    }
    cat = cat_by_id(m->id);
    root = 0;
    /* Synthetic node trees start with a DL opcode; retail files start with
     * a texture table or 0x05 switch pointers. */
    if (m->file_len >= NODE_BYTES &&
        (m->file[1] == 4 || m->file[1] == 22 || m->file[1] == 24))
        root = 0;
    else if (cat)
        root = (uint32_t)cat->nswitch * 4u + (uint32_t)cat->ntex * TEXREC_BYTES;
    if (walk_nodes(m->file, m->file_len, root, &pri, &sec) != 0 && root != 0)
        walk_nodes(m->file, m->file_len, 0, &pri, &sec);
    if (!pri)
        return -1;
    m->pri = m->file + pri;
    m->pri_n = gdl_count(m->file, m->file_len, pri);
    if (sec) {
        m->sec = m->file + sec;
        m->sec_n = gdl_count(m->file, m->file_len, sec);
    }
    return m->pri_n ? 0 : -1;
}

static int inflate_or_copy(const uint8_t *src, size_t n, uint8_t **out, size_t *out_len)
{
    size_t need = 0;
    uint8_t *exp;
    int rc;

    if (n >= PORT_INFLATE1172_HEADER && src[0] == 0x11 && src[1] == 0x72) {
        rc = bgDecompress(src, n, NULL, 0, &need);
        if (rc != PORT_INFLATE1172_OK || need == 0)
            return -1;
        exp = (uint8_t *)malloc(need);
        if (!exp)
            return -2;
        rc = bgDecompress(src, n, exp, need, &need);
        if (rc != PORT_INFLATE1172_OK) {
            free(exp);
            return -1;
        }
        *out = exp;
        *out_len = need;
        return 0;
    }
    exp = (uint8_t *)malloc(n);
    if (!exp)
        return -2;
    memcpy(exp, src, n);
    *out = exp;
    *out_len = n;
    return 0;
}

static PortModel *load_model(int id)
{
    const PortPropCat *cat;
    const C0Pack *pack;
    const C0PackEntry *e;
    char path[160];
    PortModel *m;
    uint8_t *file = NULL;
    size_t flen = 0;
    int i, rc;

    for (i = 0; i < g_nmdl; i++) {
        if (g_mdl[i].id == id)
            return g_mdl[i].pri ? &g_mdl[i] : NULL;
    }
    if (g_nmdl >= PORT_MAX_MODELS)
        return NULL;
    cat = cat_by_id(id);
    if (!cat)
        return NULL;
    pack = port_pack();
    if (!pack)
        return NULL;
    snprintf(path, sizeof path, "assets/obseg/prop/P%sZ.bin", cat->name);
    e = c0pack_find(pack, path);
    if (!e)
        e = c0pack_find_tail(pack, path);
    if (!e || e->size == 0)
        return NULL;
    rc = inflate_or_copy(e->bytes, e->size, &file, &flen);
    if (rc != 0)
        return NULL;
    m = &g_mdl[g_nmdl];
    memset(m, 0, sizeof *m);
    m->id = id;
    m->file = file;
    m->file_len = flen;
    if (bind_model_gdl(m) != 0) {
        free(file);
        memset(m, 0, sizeof *m);
        return NULL;
    }
    g_nmdl++;
    return m;
}

static const char *setup_name(int level_id)
{
    if (level_id == PORT_LEVEL_FACILITY)
        return "assets/obseg/setup/UsetuparkZ.bin";
    if (level_id == PORT_LEVEL_FACILITY_MP)
        return "assets/obseg/setup/Ump_setuparkZ.bin";
    return NULL;
}

static float yaw_from_look(float lx, float lz)
{
    if (lx * lx + lz * lz < 1e-8f)
        return 0.f;
    return atan2f(lx, lz) * (180.f / 3.14159265f);
}

static const uint8_t *pad_bytes(const uint8_t *st, size_t n, uint32_t pad_off,
                                uint32_t bound_off, int npad, int nbound, int pad)
{
    size_t off;
    if (pad >= PORT_BOUND_BASE) {
        pad -= PORT_BOUND_BASE;
        if (pad < 0 || pad >= nbound || !bound_off)
            return NULL;
        off = (size_t)bound_off + (size_t)pad * PORT_BOUND_BYTES;
        if (off + PORT_PAD_BYTES > n)
            return NULL;
        return st + off;
    }
    /* Negative / attached: no object-parent walk. Facility intro is pad 167. */
    if (pad < 0 || pad >= npad || !pad_off)
        return NULL;
    off = (size_t)pad_off + (size_t)pad * PORT_PAD_BYTES;
    if (off + PORT_PAD_BYTES > n)
        return NULL;
    return st + off;
}

static void parse_intro(const uint8_t *st, size_t n, uint32_t pad_off, uint32_t bound_off,
                        int npad, int nbound)
{
    uint32_t ioff;
    const uint8_t *p;
    int chosen = -1, first = -1;

    g_have_intro = 0;
    g_intro_pad = -1;
    if (n < PORT_SETUP_PTRS * 4)
        return;
    ioff = be32(st + 8);
    if (!ioff || ioff >= n)
        return;
    p = st + ioff;
    while (p + 4 <= st + n) {
        int type = (int)be32(p);
        uint16_t words;
        size_t bytes;
        if (type == INTRO_END)
            break;
        if (type < 0 || type >= INTRO_END)
            break;
        words = k_intro_words[type];
        bytes = (size_t)words * 4u;
        if (p + bytes > st + n)
            break;
        if (type == INTRO_SPAWN && bytes >= 12) {
            int pad = (int)be32(p + 4);
            int demo = (int)be32(p + 8);
            if (first < 0)
                first = pad;
            if (demo == 0 && chosen < 0)
                chosen = pad;
        }
        p += bytes;
    }
    if (chosen < 0)
        chosen = first;
    if (chosen >= 0) {
        const uint8_t *pd = pad_bytes(st, n, pad_off, bound_off, npad, nbound, chosen);
        if (pd) {
            g_intro_pad = chosen;
            g_intro_pos[0] = be_f32(pd);
            g_intro_pos[1] = be_f32(pd + 4);
            g_intro_pos[2] = be_f32(pd + 8);
            g_intro_look[0] = be_f32(pd + 24);
            g_intro_look[1] = be_f32(pd + 28);
            g_intro_look[2] = be_f32(pd + 32);
            g_have_intro = 1;
        }
    }
}

static int parse_setup(const uint8_t *st, size_t n)
{
    uint32_t poff, pad_off, pad3_off, names_off;
    const uint8_t *p;
    int npad, nbound;

    g_nprop = 0;
    g_have_intro = 0;
    g_intro_pad = -1;
    if (n < PORT_SETUP_PTRS * 4)
        return -1;
    poff = be32(st + 12);
    pad_off = be32(st + 24);
    pad3_off = be32(st + 28);
    names_off = be32(st + 32);
    if (!poff || poff >= n)
        return -1;
    npad = 0;
    nbound = 0;
    if (pad_off && pad_off < n) {
        size_t end = pad3_off && pad3_off > pad_off && pad3_off <= n ? pad3_off : n;
        npad = (int)((end - pad_off) / PORT_PAD_BYTES);
    }
    if (pad3_off && pad3_off < n) {
        size_t end = names_off && names_off > pad3_off && names_off <= n ? names_off : n;
        nbound = (int)((end - pad3_off) / PORT_BOUND_BYTES);
    }
    parse_intro(st, n, pad_off, pad3_off, npad, nbound);
    p = st + poff;
    while (p + 4 <= st + n && g_nprop < PORT_MAX_PROPS) {
        uint8_t type = p[3];
        uint16_t words;
        size_t bytes;
        if (type == PDEF_END)
            break;
        words = type < PDEF_MAX ? k_pdef_words[type] : 1;
        if (words < 1)
            words = 1;
        bytes = (size_t)words * 4u;
        if (p + bytes > st + n)
            break;
        if (scenery_type(type) && bytes >= 8) {
            int16_t model = (int16_t)be16(p + 4);
            int16_t pad = (int16_t)be16(p + 6);
            uint16_t extra = be16(p);
            if (model >= 0 && pad >= 0 && pad < npad) {
                const uint8_t *pd = st + pad_off + (size_t)pad * PORT_PAD_BYTES;
                PortProp *pr = &g_prop[g_nprop];
                const PortPropCat *cat = cat_by_id(model);
                memset(pr, 0, sizeof *pr);
                pr->type = type;
                pr->model = model;
                pr->pad = pad;
                pr->pos[0] = be_f32(pd);
                pr->pos[1] = be_f32(pd + 4);
                pr->pos[2] = be_f32(pd + 8);
                pr->look[0] = be_f32(pd + 24);
                pr->look[1] = be_f32(pd + 28);
                pr->look[2] = be_f32(pd + 32);
                pr->scale = (extra ? (float)extra / 256.f : 1.f) * (cat ? cat->scale : 1.f);
                pr->yaw = yaw_from_look(pr->look[0], pr->look[2]);
                pr->mdl = load_model(model);
                if (pr->mdl)
                    g_nprop++;
            }
        }
        p += bytes;
    }
    return 0;
}

void port_prop_unload(void)
{
    int i;
    for (i = 0; i < g_nmdl; i++)
        free(g_mdl[i].file);
    memset(g_mdl, 0, sizeof g_mdl);
    memset(g_prop, 0, sizeof g_prop);
    free(g_setup);
    g_setup = NULL;
    g_setup_len = 0;
    g_nprop = 0;
    g_nmdl = 0;
    g_drawn = 0;
    g_have_intro = 0;
    g_intro_pad = -1;
}

int port_prop_load(int level_id)
{
    const C0Pack *pack;
    const C0PackEntry *e;
    const char *name = setup_name(level_id);
    uint8_t *copy = NULL;
    size_t clen = 0;
    int rc;

    port_prop_unload();
    if (!name)
        return PORT_PROP_OK;
    pack = port_pack();
    if (!pack)
        return PORT_PROP_OK;
    e = c0pack_find(pack, name);
    if (!e)
        e = c0pack_find_tail(pack, name);
    if (!e || e->size == 0)
        return PORT_PROP_OK;
    rc = inflate_or_copy(e->bytes, e->size, &copy, &clen);
    if (rc != 0)
        return PORT_PROP_OK;
    g_setup = copy;
    g_setup_len = clen;
    parse_setup(g_setup, g_setup_len);
    return PORT_PROP_OK;
}

int port_prop_count(void) { return g_nprop; }

int port_prop_models(void) { return g_nmdl; }

int port_prop_drawn(void) { return g_drawn; }

int port_prop_intro_pad(void) { return g_have_intro ? g_intro_pad : -1; }

int port_prop_intro(float pos[3], float look[3], int *pad_out)
{
    if (!g_have_intro)
        return -1;
    if (pos) {
        pos[0] = g_intro_pos[0];
        pos[1] = g_intro_pos[1];
        pos[2] = g_intro_pos[2];
    }
    if (look) {
        look[0] = g_intro_look[0];
        look[1] = g_intro_look[1];
        look[2] = g_intro_look[2];
    }
    if (pad_out)
        *pad_out = g_intro_pad;
    return 0;
}

int port_prop_fill_rooms(G1RoomDl *out, int cap, const float room1[3],
                         const float *room_xyz, int nrooms, const uint8_t *room_ids)
{
    int i, k = 0;
    g_drawn = 0;
    if (!out || cap < 1 || !room1)
        return 0;
    for (i = 0; i < g_nprop && k < cap; i++) {
        PortProp *pr = &g_prop[i];
        float dx, dy, dz, best, d;
        int r, near = 0;
        if (!pr->mdl || !pr->mdl->pri || pr->mdl->pri_n == 0)
            continue;
        best = PORT_PROP_NEAR * PORT_PROP_NEAR;
        for (r = 0; r < nrooms; r++) {
            const float *rp;
            int id = room_ids ? room_ids[r] : r + 1;
            if (id < 1)
                continue;
            rp = room_xyz + (size_t)id * 3;
            dx = pr->pos[0] - rp[0];
            dy = pr->pos[1] - rp[1];
            dz = pr->pos[2] - rp[2];
            d = dx * dx + dy * dy + dz * dz;
            if (d <= best) {
                best = d;
                near = 1;
            }
        }
        if (!near && nrooms > 0)
            continue;
        if (nrooms <= 0) {
            /* No room filter: still require a sane radius from room 1. */
            dx = pr->pos[0] - room1[0];
            dy = pr->pos[1] - room1[1];
            dz = pr->pos[2] - room1[2];
            if (dx * dx + dy * dy + dz * dz > PORT_PROP_NEAR * PORT_PROP_NEAR)
                continue;
        }
        memset(&out[k], 0, sizeof out[k]);
        out[k].pri = pr->mdl->pri;
        out[k].pri_n = pr->mdl->pri_n;
        out[k].sec = pr->mdl->sec;
        out[k].sec_n = pr->mdl->sec_n;
        out[k].ox = pr->pos[0] - room1[0];
        out[k].oy = pr->pos[1] - room1[1];
        out[k].oz = pr->pos[2] - room1[2];
        out[k].yaw = pr->yaw;
        out[k].scale = pr->scale;
        out[k].seg5 = (uintptr_t)pr->mdl->file;
        k++;
    }
    g_drawn = k;
    return k;
}

int port_prop_door_count(void)
{
    int i, n = 0;
    for (i = 0; i < g_nprop; i++) {
        if (g_prop[i].type == PDEF_DOOR)
            n++;
    }
    return n;
}

int port_prop_door_xz(int want, float *x, float *z, float *lx, float *lz)
{
    int i, n = 0;
    for (i = 0; i < g_nprop; i++) {
        if (g_prop[i].type != PDEF_DOOR)
            continue;
        if (n == want) {
            if (x)
                *x = g_prop[i].pos[0];
            if (z)
                *z = g_prop[i].pos[2];
            if (lx)
                *lx = g_prop[i].look[0];
            if (lz)
                *lz = g_prop[i].look[2];
            return 0;
        }
        n++;
    }
    return -1;
}
