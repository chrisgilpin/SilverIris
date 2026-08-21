#include "prop.h"

#include "c0pack.h"
#include "inflate1172.h"
#include "pack_dma.h"
#include "player/stan_walk.h"
#include "player/move.h"
#include "player/gun.h"
#include "stage.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT_MAX_PROPS 256
#define PORT_MAX_MODELS 80
#define PORT_MODEL_PARTS 32
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
#define PORT_NODE_MAX 128
#define PORT_GDL_MAX 512
#define PORT_SKEL_GUARD_N 16
#define PORT_ANIM_IDLE_OFF 0x1Cu
#define PORT_ANIM_WALK_OFF 0x4018u /* PTR_ANIM_walking */
#define PORT_ANIM_WALK_FRAME 8
#define PORT_WALK_ID_BASE 3000
#define PORT_RST_MAGIC 0x52535431u
#define PORT_ANIM_ENTRIES_ROM_U 1198784u
#define PORT_ANIM_DATA_PATH "assets/animationtable_data.bin"
#define PORT_ANIM_ENTRY_PATH "assets/animationtable_entries.bin"
#define TEXREC_BYTES 12
#define NODE_BYTES 24

#define PDEF_DOOR 1
#define PDEF_PROP 3
#define PDEF_ALARM 5
#define PDEF_GUARD 9
#define PDEF_RACK 12
#define PDEF_GAS 36
#define PDEF_GLASS 42
#define PDEF_TINTED 47
#define PDEF_END 48
#define PDEF_MAX 49
#define DOORTYPE_SLIDING 0
#define DOORTYPE_VERTICAL 4
#define DOORTYPE_SWINGING 5
#define PORT_DOOR_OPEN_YAW 90.f
#define PORT_DOOR_SLIDE 180.f
#define PORT_DOOR_HALF_W 90.f
/* Posed C*Z idle AABB is ~1510 (480 spine + ~900 legs), not 480. Fit to 185. */
#define PORT_CHR_STAND 185.f
/* GwppkZ MODELFILEHEADER: NUMSWITCHES=0x24 NUMTEXTURES=0xC. */
#define PORT_GUN_WPPK_ID 9001
#define PORT_GUN_WPPK_NSW 0x24
#define PORT_GUN_WPPK_NTEX 0x0C
/* Camera-space hold. Rare wppk_stats on-screen Pos is (11, -20.8, -33.5).
 * X matches Rare (was +22, too far right). Y is a bit below Rare so the
 * mesh reads bottom-center. Z stays farther than Rare because G1 near=10
 * and the grip sits toward the eye after the 180 Y; -60 keeps the AABB
 * in front of the near plane without the old -72 right-corner scale. */
#define PORT_VIEWGUN_X 11.f
#define PORT_VIEWGUN_Y (-24.f)
#define PORT_VIEWGUN_Z (-60.f)

#define PI_F 3.14159265f

typedef struct {
    uint16_t id;
    uint16_t nswitch;
    uint16_t ntex;
    float scale;
    const char *name;
} PortPropCat;

#include "prop_catalog.inc.c"
#include "chr_catalog.inc.c"

typedef struct {
    const uint8_t *pri;
    uint32_t pri_n;
    const uint8_t *sec;
    uint32_t sec_n;
    float ox, oy, oz;
    float rx, ry, rz;
    uintptr_t vtx4; /* DLCOLLISION vertex bank for G_VTX 0x04 */
    uint32_t nvtx;
} PortPart;

typedef struct {
    int id;
    uint16_t nswitch;
    uint16_t ntex;
    uint8_t *file;
    size_t file_len;
    PortPart part[PORT_MODEL_PARTS];
    int npart;
    float head_off[3];
    float head_rx, head_ry, head_rz;
    int have_head;
    float fit_scale;
    float fit_ymin;
} PortModel;

typedef struct {
    int type;
    int model;
    int pad;
    float pos[3];
    float look[3];
    float scale;
    float yaw;
    int door_type;
    float max_frac;
    PortModel *mdl;
    PortModel *head;
    float head_off[3];
    float head_rx, head_ry, head_rz;
} PortProp;

static uint8_t *g_setup;
static size_t g_setup_len;
static PortProp g_prop[PORT_MAX_PROPS];
static int g_nprop;
static PortModel g_mdl[PORT_MAX_MODELS];
static int g_nmdl;
static int g_drawn;
static PortModel g_retail_slab;
static int g_retail_slab_ok;
static int g_intro_pad = -1;
static int g_have_intro;
static float g_intro_pos[3];
static float g_intro_look[3];
static float g_idle_rest[PORT_SKEL_GUARD_N][3];
static float g_walk_rest[PORT_SKEL_GUARD_N][3];
static int g_have_idle;
static int g_have_walk;
static int g_walkers;
static int g_walk_prop;
static int g_walk_frame;
static uint32_t g_walk_nframes;
static float g_walk_fit_scale;
static float g_walk_fit_ymin;
static char g_idle_info[96];
static char g_walk_info[96];
static char g_pose_info[192];
static const float (*g_pose_rest)[3];
static int g_viewgun_parts;
static PortModel *load_wppk(void);
static const PortPropCat k_wppk_gun = { PORT_GUN_WPPK_ID, PORT_GUN_WPPK_NSW,
                                       PORT_GUN_WPPK_NTEX, 1.f, "wppk" };

/* SKELETON(guard) JOINTLIST mtxA — bitstream channel base per JointID. */
static const uint16_t k_guard_mtxa[PORT_SKEL_GUARD_N] = {
    0x00, 0x00, 0x03, 0x06, 0x09, 0x0C, 0x0F, 0x12,
    0x15, 0x18, 0x1B, 0x1E, 0x21, 0x24, 0x27, 0x2A,
};

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

static void mtx_ident4(float m[4][4])
{
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            m[i][j] = (i == j) ? 1.f : 0.f;
}

static void mtx_mul4(float out[4][4], const float a[4][4], const float b[4][4])
{
    float t[4][4];
    int i, j, k;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            float s = 0.f;
            for (k = 0; k < 4; k++)
                s += a[i][k] * b[k][j];
            t[i][j] = s;
        }
    }
    memcpy(out, t, sizeof t);
}

/* G1 column-vector XYZ Euler + translation in last column. */
static void mtx_local(float m[4][4], float x, float y, float z, float rx, float ry, float rz)
{
    float cx = cosf(rx), sx = sinf(rx);
    float cy = cosf(ry), sy = sinf(ry);
    float cz = cosf(rz), sz = sinf(rz);
    mtx_ident4(m);
    m[0][0] = cy * cz;
    m[0][1] = (sx * cz * sy) - (cx * sz);
    m[0][2] = (cx * cz * sy) + (sx * sz);
    m[1][0] = cy * sz;
    m[1][1] = (sx * sz * sy) + (cx * cz);
    m[1][2] = (cx * sz * sy) - (sx * cz);
    m[2][0] = -sy;
    m[2][1] = sx * cy;
    m[2][2] = cx * cy;
    m[0][3] = x;
    m[1][3] = y;
    m[2][3] = z;
}

static void mtx_euler(const float m[4][4], float *rx, float *ry, float *rz)
{
    *ry = atan2f(-m[2][0], sqrtf(m[0][0] * m[0][0] + m[1][0] * m[1][0]));
    *rx = atan2f(m[2][1], m[2][2]);
    *rz = atan2f(m[1][0], m[0][0]);
}

/* modelAnimReadBitsAsU16Angle — width bits at bitOffset, left-justified to u16. */
static uint16_t anim_u16(const uint8_t *bits, size_t n, uint8_t width, uint32_t bitoff)
{
    uint32_t value = 0, mask;
    uint8_t remain = width, here;
    size_t byte = (size_t)(bitoff / 8u);
    uint32_t skip = bitoff % 8u;
    if (!bits || width == 0 || width > 16)
        return 0;
    if (byte >= n)
        return 0;
    bits += byte;
    n -= byte;
    here = (uint8_t)(8u - skip);
    while (remain >= here) {
        if (!n)
            break;
        remain = (uint8_t)(remain - here);
        mask = (1u << here) - 1u;
        value |= ((uint32_t)(*bits) & mask) << remain;
        value &= 0xffffu;
        bits++;
        n--;
        here = 8;
    }
    if (remain > 0 && n) {
        mask = (1u << remain) - 1u;
        value |= ((uint32_t)(*bits) >> (here - remain)) & mask;
        value &= 0xffffu;
    }
    value <<= (16u - width);
    return (uint16_t)(value & 0xffffu);
}

static int decode_anim_frame(uint32_t data_off, int want_frame, float rest[PORT_SKEL_GUARD_N][3],
                             uint32_t *addr_out, uint32_t *off_out, uint32_t *frames_out,
                             uint8_t *width_out, char *err, size_t errn)
{
    const C0Pack *pack;
    const C0PackEntry *data, *ent;
    const uint8_t *hdr, *frame;
    uint32_t addr, frames, frame_bits, frame_bytes, off, take;
    uint8_t width;
    int j;

    memset(rest, 0, sizeof(float) * PORT_SKEL_GUARD_N * 3);
    pack = port_pack();
    if (!pack) {
        snprintf(err, errn, "skip=no_pack");
        return 0;
    }
    data = c0pack_find(pack, PORT_ANIM_DATA_PATH);
    if (!data)
        data = c0pack_find_tail(pack, PORT_ANIM_DATA_PATH);
    ent = c0pack_find(pack, PORT_ANIM_ENTRY_PATH);
    if (!ent)
        ent = c0pack_find_tail(pack, PORT_ANIM_ENTRY_PATH);
    if (!data || !ent || data->size < data_off + 16u || ent->size == 0) {
        snprintf(err, errn, "skip=no_bins data=%d ent=%d", data ? (int)data->size : -1,
                 ent ? (int)ent->size : -1);
        return 0;
    }
    hdr = data->bytes + data_off;
    addr = be32(hdr);
    frames = be16(hdr + 4);
    width = hdr[6];
    frame_bits = be16(hdr + 14);
    if (frames < 1 || width < 8 || width > 16 || frame_bits < (uint32_t)width) {
        snprintf(err, errn, "skip=hdr addr=0x%x fr=%u w=%u bits=%u", addr, frames, width,
                 frame_bits);
        return 0;
    }
    frame_bytes = frame_bits >> 3;
    if (frame_bytes == 0 || frame_bytes * 8u < 45u * (uint32_t)width) {
        snprintf(err, errn, "skip=frame addr=0x%x bytes=%u w=%u", addr, frame_bytes, width);
        return 0;
    }
    take = (uint32_t)want_frame;
    if (take >= frames)
        take = frames / 4u;
    off = 0xFFFFFFFFu;
    /* PTR_ANIM_ENTRY_idle is 0: relative offset into entries.bin, not a skip. */
    if (addr >= PORT_ANIM_ENTRIES_ROM_U &&
        addr + (take + 1u) * frame_bytes <= PORT_ANIM_ENTRIES_ROM_U + (uint32_t)ent->size)
        off = addr - PORT_ANIM_ENTRIES_ROM_U;
    else if ((addr & 0x00FFFFFFu) >= PORT_ANIM_ENTRIES_ROM_U &&
             (addr & 0x00FFFFFFu) + (take + 1u) * frame_bytes <=
                 PORT_ANIM_ENTRIES_ROM_U + (uint32_t)ent->size)
        off = (addr & 0x00FFFFFFu) - PORT_ANIM_ENTRIES_ROM_U;
    else if (addr + (take + 1u) * frame_bytes <= (uint32_t)ent->size)
        off = addr;
    if (off == 0xFFFFFFFFu || off + (take + 1u) * frame_bytes > (uint32_t)ent->size) {
        snprintf(err, errn, "skip=region addr=0x%x ent=%u need=%u", addr, (unsigned)ent->size,
                 (take + 1u) * frame_bytes);
        return 0;
    }
    frame = ent->bytes + off + take * frame_bytes;
    for (j = 0; j < PORT_SKEL_GUARD_N; j++) {
        uint32_t bo = (uint32_t)k_guard_mtxa[j] * (uint32_t)width;
        uint16_t ax = anim_u16(frame, frame_bytes, width, bo);
        uint16_t ay = anim_u16(frame, frame_bytes, width, bo + width);
        uint16_t az = anim_u16(frame, frame_bytes, width, bo + 2u * width);
        rest[j][0] = (float)ax * (2.f * PI_F) / 65536.f;
        rest[j][1] = (float)ay * (2.f * PI_F) / 65536.f;
        rest[j][2] = (float)az * (2.f * PI_F) / 65536.f;
    }
    if (addr_out)
        *addr_out = addr;
    if (off_out)
        *off_out = off;
    if (frames_out)
        *frames_out = frames;
    if (width_out)
        *width_out = width;
    snprintf(err, errn, "addr=0x%x off=%u fr=%u w=%u f=%u", addr, off, frames, width, take);
    return 1;
}

static void load_idle_rest(void)
{
    char err[80];
    uint32_t addr = 0, off = 0, frames = 0;
    uint8_t width = 0;

    g_have_idle = 0;
    g_have_walk = 0;
    g_walkers = 0;
    g_walk_prop = -1;
    g_walk_frame = 0;
    g_walk_nframes = 0;
    g_walk_fit_scale = 0.f;
    g_walk_fit_ymin = 0.f;
    g_viewgun_parts = 0;
    g_pose_rest = NULL;
    memset(g_idle_rest, 0, sizeof g_idle_rest);
    memset(g_walk_rest, 0, sizeof g_walk_rest);
    g_idle_info[0] = 0;
    g_walk_info[0] = 0;
    snprintf(g_idle_info, sizeof g_idle_info, "idle=0 skip=no_pack");
    snprintf(g_walk_info, sizeof g_walk_info, "walk=0 skip=no_pack");
    if (decode_anim_frame(PORT_ANIM_IDLE_OFF, 0, g_idle_rest, &addr, &off, &frames, &width, err,
                          sizeof err)) {
        g_have_idle = 1;
        snprintf(g_idle_info, sizeof g_idle_info, "idle=1 addr=0x%x off=%u fr=%u w=%u", addr, off,
                 frames, width);
    } else {
        snprintf(g_idle_info, sizeof g_idle_info, "idle=0 %s", err);
    }
    addr = off = frames = 0;
    width = 0;
    if (decode_anim_frame(PORT_ANIM_WALK_OFF, PORT_ANIM_WALK_FRAME, g_walk_rest, &addr, &off,
                          &frames, &width, err, sizeof err)) {
        g_have_walk = 1;
        g_walk_frame = PORT_ANIM_WALK_FRAME;
        g_walk_nframes = frames;
        snprintf(g_walk_info, sizeof g_walk_info, "walk=1 addr=0x%x off=%u fr=%u w=%u f=%u n=0",
                 addr, off, frames, width, (unsigned)PORT_ANIM_WALK_FRAME);
    } else {
        snprintf(g_walk_info, sizeof g_walk_info, "walk=0 %s", err);
    }
}

static void rest_for_group(const uint8_t *base, size_t n, uint32_t data, int use_guard,
                           float *rx, float *ry, float *rz)
{
    uint16_t joint;
    *rx = *ry = *rz = 0.f;
    if (!data || data + 14 > n)
        return;
    joint = be16(base + data + 12);
    if (data + 0x1C + 16 <= n && be32(base + data + 0x1C) == PORT_RST_MAGIC) {
        *rx = be_f32(base + data + 0x20);
        *ry = be_f32(base + data + 0x24);
        *rz = be_f32(base + data + 0x28);
        return;
    }
    if (use_guard && g_pose_rest && joint < PORT_SKEL_GUARD_N) {
        *rx = g_pose_rest[joint][0];
        *ry = g_pose_rest[joint][1];
        *rz = g_pose_rest[joint][2];
    }
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

static const PortPropCat *chr_by_id(int id)
{
    if (id < 0 || id >= PORT_CHR_CAT_N)
        return NULL;
    return &k_chr_cat[id];
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
        /* F3D G_ENDDL is 0xB8; F3DEX2 is 0xDF. Retail P*Z uses B8. */
        if (base[off + i * 8u] == (uint8_t)0xDF || base[off + i * 8u] == (uint8_t)0xB8)
            return i + 1u;
    }
    return i;
}

static int add_part_gdl(PortModel *m, const uint8_t *base, size_t n, uint8_t op,
                        uint32_t data, float ox, float oy, float oz, float rx, float ry,
                        float rz)
{
    uint32_t p = 0, s = 0;
    PortPart *pt;

    if (!data || m->npart >= PORT_MODEL_PARTS)
        return 0;
    if (op == 24 && data + 8 <= n) {
        p = file_off(be32(base + data), n);
        s = file_off(be32(base + data + 4), n);
    } else if (op == 4 && data + 8 <= n) {
        p = file_off(be32(base + data), n);
        s = file_off(be32(base + data + 4), n);
    } else if (op == 22 && data + 12 <= n) {
        p = file_off(be32(base + data + 8), n);
    }
    if (!p)
        return 0;
    pt = &m->part[m->npart];
    memset(pt, 0, sizeof *pt);
    pt->pri = base + p;
    pt->pri_n = gdl_count(base, n, p);
    if (!pt->pri_n)
        return 0;
    if (s) {
        pt->sec = base + s;
        pt->sec_n = gdl_count(base, n, s);
    }
    pt->ox = ox;
    pt->oy = oy;
    pt->oz = oz;
    pt->rx = rx;
    pt->ry = ry;
    pt->rz = rz;
    if ((op == 24 || op == 4) && data + 12 <= n) {
        uint32_t v = file_off(be32(base + data + 8), n);
        if (v) {
            pt->vtx4 = (uintptr_t)(base + v);
            pt->nvtx = (uint32_t)be16(base + data + 12);
            if (pt->nvtx > 256u)
                pt->nvtx = 256u;
        }
    }
    m->npart++;
    return 1;
}

/* Walk Rare nodes. GROUP / GROUPSIMPLE Origin is T; guard bodies also
 * apply rest-pose R from RST1 (synthetic) or ANIM_idle frame 0 via
 * SKELETON(guard) JointID → mtxA. Identity rest = old bind-pose. */
static int walk_parts(PortModel *m, uint32_t root, int use_guard)
{
    uint32_t q[PORT_NODE_MAX];
    float qm[PORT_NODE_MAX][4][4];
    int qh = 0, qt = 0, i;
    const uint8_t *base = m->file;
    size_t n = m->file_len;

    if (root + NODE_BYTES > n)
        return -1;
    q[qt] = root;
    mtx_ident4(qm[qt]);
    qt++;
    while (qh < qt) {
        uint32_t off = q[qh];
        float parent[4][4], childm[4][4], local[4][4];
        float ox, oy, oz, rx, ry, rz;
        uint8_t op;
        uint32_t data, child, next;
        memcpy(parent, qm[qh], sizeof parent);
        qh++;
        if (off + NODE_BYTES > n)
            continue;
        op = base[off + 1];
        data = file_off(be32(base + off + 4), n);
        child = file_off(be32(base + off + 20), n);
        next = file_off(be32(base + off + 12), n);
        memcpy(childm, parent, sizeof childm);
        if ((op == 2 || op == 21) && data && data + 12 <= n) {
            ox = be_f32(base + data);
            oy = be_f32(base + data + 4);
            oz = be_f32(base + data + 8);
            rx = ry = rz = 0.f;
            if (op == 2)
                rest_for_group(base, n, data, use_guard, &rx, &ry, &rz);
            mtx_local(local, ox, oy, oz, rx, ry, rz);
            mtx_mul4(childm, parent, local);
        }
        ox = childm[0][3];
        oy = childm[1][3];
        oz = childm[2][3];
        mtx_euler(childm, &rx, &ry, &rz);
        if ((op == 4 || op == 22 || op == 24) && data)
            add_part_gdl(m, base, n, op, data, ox, oy, oz, rx, ry, rz);
        if (op == 23 && !m->have_head) {
            m->head_off[0] = ox;
            m->head_off[1] = oy;
            m->head_off[2] = oz;
            m->head_rx = rx;
            m->head_ry = ry;
            m->head_rz = rz;
            m->have_head = 1;
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
            if (!have && qt < PORT_NODE_MAX) {
                q[qt] = c;
                memcpy(qm[qt], i ? parent : childm, sizeof qm[qt]);
                qt++;
            }
        }
    }
    return m->npart ? 0 : -1;
}

static int bind_model_gdl(PortModel *m, int use_guard)
{
    uint32_t root;

    m->npart = 0;
    m->have_head = 0;
    m->head_rx = m->head_ry = m->head_rz = 0.f;
    m->fit_scale = 1.f;
    m->fit_ymin = 0.f;
    if (!m->file || m->file_len < 8)
        return -1;
    if (be32(m->file) == PORT_BG_MAGIC_G1DL) {
        m->part[0].pri = m->file + 4;
        m->part[0].pri_n = gdl_count(m->file, m->file_len, 4);
        m->npart = m->part[0].pri_n ? 1 : 0;
        return m->npart ? 0 : -1;
    }
    root = 0;
    /* Synthetic node trees start with a DL opcode; retail files start with
     * a texture table or 0x05 switch pointers. */
    if (m->file_len >= NODE_BYTES &&
        (m->file[1] == 2 || m->file[1] == 4 || m->file[1] == 22 || m->file[1] == 24))
        root = 0;
    else
        root = (uint32_t)m->nswitch * 4u + (uint32_t)m->ntex * TEXREC_BYTES;
    if (walk_parts(m, root, use_guard) != 0 && root != 0) {
        m->npart = 0;
        m->have_head = 0;
        m->head_rx = m->head_ry = m->head_rz = 0.f;
        walk_parts(m, 0, use_guard);
    }
    if (use_guard && g_pose_rest && m->npart) {
        int dp;
        float ymin = 1e9f, ymax = -1e9f;
        for (dp = 0; dp < m->npart; dp++) {
            if (m->part[dp].oy < ymin)
                ymin = m->part[dp].oy;
            if (m->part[dp].oy > ymax)
                ymax = m->part[dp].oy;
        }
        if (ymax > ymin + 1.f) {
            m->fit_ymin = ymin;
            m->fit_scale = PORT_CHR_STAND / (ymax - ymin);
            if (!strstr(g_idle_info, " fit=")) {
                char base[96];
                snprintf(base, sizeof base, "%s", g_idle_info);
                snprintf(g_idle_info, sizeof g_idle_info, "%s fit=%.3f h=%.0f",
                         base, m->fit_scale, ymax - ymin);
            }
        }
    }
    return m->npart ? 0 : -1;
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

static PortModel *load_named(int id, const char *dir, const char *prefix, const PortPropCat *cat,
                             int use_guard, const float (*rest)[3])
{
    const C0Pack *pack;
    const C0PackEntry *e;
    const float (*save)[3];
    char path[160];
    PortModel *m;
    uint8_t *file = NULL;
    size_t flen = 0;
    int i, rc;

    save = g_pose_rest;
    g_pose_rest = rest;

    for (i = 0; i < g_nmdl; i++) {
        if (g_mdl[i].id == id) {
            g_pose_rest = save;
            return g_mdl[i].npart ? &g_mdl[i] : NULL;
        }
    }
    if (g_nmdl >= PORT_MAX_MODELS || !cat) {
        g_pose_rest = save;
        return NULL;
    }
    pack = port_pack();
    if (!pack) {
        g_pose_rest = save;
        return NULL;
    }
    snprintf(path, sizeof path, "assets/obseg/%s/%s%sZ.bin", dir, prefix, cat->name);
    e = c0pack_find(pack, path);
    if (!e)
        e = c0pack_find_tail(pack, path);
    if (!e || e->size == 0) {
        g_pose_rest = save;
        return NULL;
    }
    rc = inflate_or_copy(e->bytes, e->size, &file, &flen);
    if (rc != 0) {
        g_pose_rest = save;
        return NULL;
    }
    m = &g_mdl[g_nmdl];
    memset(m, 0, sizeof *m);
    m->id = id;
    m->nswitch = cat->nswitch;
    m->ntex = cat->ntex;
    m->file = file;
    m->file_len = flen;
    if (bind_model_gdl(m, use_guard) != 0) {
        free(file);
        memset(m, 0, sizeof *m);
        g_pose_rest = save;
        return NULL;
    }
    g_nmdl++;
    g_pose_rest = save;
    return m;
}

static PortModel *load_model(int id)
{
    return load_named(id, "prop", "P", cat_by_id(id), 0, NULL);
}

/* BodyID is the BODIES / c_item_entries index. Missing pack blob → NULL. */
static PortModel *load_chr(int body)
{
    int use_guard = body >= 0 && body < PORT_CHR_HEAD_START;
    return load_named(1000 + body, "chr", "C", chr_by_id(body), use_guard,
                      use_guard && g_have_idle ? g_idle_rest : NULL);
}

static PortModel *load_chr_walk(int body)
{
    int use_guard = body >= 0 && body < PORT_CHR_HEAD_START;
    if (!g_have_walk)
        return NULL;
    return load_named(PORT_WALK_ID_BASE + body, "chr", "C", chr_by_id(body), use_guard,
                      g_walk_rest);
}

/* Closest-to-spawn setup guard stays idle (start around the corner).
 * Next-closest is a single posed-walk test mover on its pad. No pathing. */
static void assign_walkers(void)
{
    int i, best = -1, next = -1;
    float best_d = 1e18f, next_d = 1e18f;
    float sx = g_have_intro ? g_intro_pos[0] : 0.f;
    float sz = g_have_intro ? g_intro_pos[2] : 0.f;
    PortModel *wm;
    const PortPropCat *cat;
    size_t wn;

    g_walkers = 0;
    g_walk_prop = -1;
    if (!g_have_walk)
        return;
    for (i = 0; i < g_nprop; i++) {
        float dx, dz, d;
        if (g_prop[i].type != PDEF_GUARD || !g_prop[i].mdl)
            continue;
        dx = g_prop[i].pos[0] - sx;
        dz = g_prop[i].pos[2] - sz;
        d = dx * dx + dz * dz;
        if (d < best_d) {
            next = best;
            next_d = best_d;
            best = i;
            best_d = d;
        } else if (d < next_d) {
            next = i;
            next_d = d;
        }
    }
    if (next < 0)
        return;
    wm = load_chr_walk(g_prop[next].model);
    if (!wm)
        return;
    cat = chr_by_id(g_prop[next].model);
    g_prop[next].mdl = wm;
    g_prop[next].scale =
        (cat ? cat->scale : 1.f) * (wm->fit_scale != 0.f ? wm->fit_scale : 1.f);
    if (wm->have_head) {
        g_prop[next].head_off[0] = wm->head_off[0];
        g_prop[next].head_off[1] = wm->head_off[1] - wm->fit_ymin;
        g_prop[next].head_off[2] = wm->head_off[2];
        g_prop[next].head_rx = wm->head_rx;
        g_prop[next].head_ry = wm->head_ry;
        g_prop[next].head_rz = wm->head_rz;
    }
    g_walkers = 1;
    g_walk_prop = next;
    if (wm) {
        g_walk_fit_scale = wm->fit_scale;
        g_walk_fit_ymin = wm->fit_ymin;
    }
    wn = strlen(g_walk_info);
    if (wn >= 3 && g_walk_info[wn - 3] == 'n' && g_walk_info[wn - 1] == '0')
        g_walk_info[wn - 1] = '1';
    (void)next_d;
}

/* Spawn looks 270 (-X). Player-space corridor is a west slab around
 * spawn z. Stay off that so spawn.png stays a tiled corridor, not a
 * 1510u blob or a walker in the lens. */
static int spawn_look_slab(float x, float z, float sx, float sz)
{
    float dx = x - sx, dz = z - sz;
    if (dx > 40.f)
        return 0;
    if (dx > -80.f && dx * dx + dz * dz < 180.f * 180.f)
        return 1;
    if (dx < 0.f && dx > -520.f && dz * dz < 95.f * 95.f)
        return 1;
    return 0;
}

static int try_sit_walker(float lx, float lz, float sx, float sz, const float r1[3])
{
    float ey = 0.f, floor_y, wy;
    if (!port_stan_on_tile(lx, lz))
        return 0;
    if (port_stan_eye_y(lx, lz, &ey) != 0)
        return 0;
    if (!(ey == ey) || ey < 50.f || ey > 160.f)
        return 0;
    if (spawn_look_slab(lx, lz, sx, sz))
        return 0;
    floor_y = ey - PORT_EYE_HEIGHT;
    wy = floor_y + r1[1];
    if (!(wy == wy) || wy > 1.0e20f || wy < -1.0e20f)
        return 0;
    g_prop[g_walk_prop].pos[0] = lx + r1[0];
    g_prop[g_walk_prop].pos[1] = wy;
    g_prop[g_walk_prop].pos[2] = lz + r1[2];
    return 1;
}

/* After intro snap: origin/tiles match the player. Sit on a ground-floor
 * tile around the spawn corner (hallway turn). Do not retouch stan origin.
 * If every candidate clips / NaN Y, leave them on the setup pad. */
int port_prop_place_walker_near_spawn(void)
{
    float sx, sz, r1[3];
    int i;
    static const float pref[][2] = {
        { -220.f, -2640.f },
        { -300.f, -2640.f },
        { -220.f, -2560.f },
        { -300.f, -2560.f },
        { -380.f, -2560.f },
        { -380.f, -2640.f },
        { -220.f, -2480.f },
        { -300.f, -2480.f },
    };

    if (g_walk_prop < 0 || g_walk_prop >= g_nprop)
        return 0;
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    sx = port_player_x();
    sz = port_player_z();
    for (i = 0; i < (int)(sizeof pref / sizeof pref[0]); i++) {
        if (try_sit_walker(pref[i][0], pref[i][1], sx, sz, r1))
            return 1;
    }
    return 0;
}

static void apply_walk_bind(void)
{
    PortModel *m;
    const float (*save)[3];
    const PortPropCat *cat;
    int body, use_guard;

    if (g_walk_prop < 0 || g_walk_prop >= g_nprop)
        return;
    m = g_prop[g_walk_prop].mdl;
    if (!m || !m->file)
        return;
    body = g_prop[g_walk_prop].model;
    use_guard = body >= 0 && body < PORT_CHR_HEAD_START;
    save = g_pose_rest;
    g_pose_rest = g_walk_rest;
    bind_model_gdl(m, use_guard);
    if (g_walk_fit_scale != 0.f) {
        m->fit_scale = g_walk_fit_scale;
        m->fit_ymin = g_walk_fit_ymin;
    } else {
        g_walk_fit_scale = m->fit_scale;
        g_walk_fit_ymin = m->fit_ymin;
    }
    g_pose_rest = save;
    cat = chr_by_id(body);
    g_prop[g_walk_prop].scale =
        (cat ? cat->scale : 1.f) * (m->fit_scale != 0.f ? m->fit_scale : 1.f);
    if (m->have_head) {
        g_prop[g_walk_prop].head_off[0] = m->head_off[0];
        g_prop[g_walk_prop].head_off[1] = m->head_off[1] - m->fit_ymin;
        g_prop[g_walk_prop].head_off[2] = m->head_off[2];
        g_prop[g_walk_prop].head_rx = m->head_rx;
        g_prop[g_walk_prop].head_ry = m->head_ry;
        g_prop[g_walk_prop].head_rz = m->head_rz;
    }
}

static int set_walk_frame(int frame)
{
    char err[80];
    uint32_t addr = 0, off = 0, frames = 0;
    uint8_t width = 0;
    unsigned take;

    if (!g_have_walk)
        return -1;
    if (g_walk_nframes > 0) {
        int n = (int)g_walk_nframes;
        frame %= n;
        if (frame < 0)
            frame += n;
    }
    if (!decode_anim_frame(PORT_ANIM_WALK_OFF, frame, g_walk_rest, &addr, &off,
                           &frames, &width, err, sizeof err))
        return -1;
    if (frames > 0)
        g_walk_nframes = frames;
    take = (unsigned)frame;
    if (g_walk_nframes > 0 && take >= g_walk_nframes)
        take %= g_walk_nframes;
    g_walk_frame = (int)take;
    snprintf(g_walk_info, sizeof g_walk_info, "walk=1 addr=0x%x off=%u fr=%u w=%u f=%u n=%d",
             addr, off, frames, width, take, g_walkers);
    if (g_walk_prop >= 0)
        apply_walk_bind();
    return 0;
}

void port_prop_tick_walk(void)
{
    if (!g_have_walk || g_walkers < 1 || g_walk_nframes < 1)
        return;
    set_walk_frame(g_walk_frame + 1);
}

void port_prop_set_walk_frame(int frame)
{
    if (!g_have_walk)
        return;
    set_walk_frame(frame);
}

int port_prop_walk_frame(void)
{
    if (!g_have_walk || g_walkers < 1)
        return -1;
    return g_walk_frame;
}

uint32_t port_prop_walk_rest_crc(void)
{
    const uint8_t *p = (const uint8_t *)g_walk_rest;
    uint32_t crc = 0xFFFFFFFFu;
    size_t i, n = sizeof g_walk_rest;
    for (i = 0; i < n; i++)
        crc = (crc >> 8) ^ ((crc ^ p[i]) * 16777619u);
    return crc ^ 0xFFFFFFFFu;
}

int port_prop_walk_xyz(float *x, float *y, float *z)
{
    if (g_walk_prop < 0 || g_walk_prop >= g_nprop)
        return -1;
    if (x)
        *x = g_prop[g_walk_prop].pos[0];
    if (y)
        *y = g_prop[g_walk_prop].pos[1];
    if (z)
        *z = g_prop[g_walk_prop].pos[2];
    return 0;
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
    return atan2f(lx, lz) * (180.f / PI_F);
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

static void fill_pad_prop(PortProp *pr, int type, int model, int pad, const uint8_t *pd,
                          float scale)
{
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
    pr->scale = scale;
    pr->yaw = yaw_from_look(pr->look[0], pr->look[2]);
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
                fill_pad_prop(pr, type, model, pad, pd,
                              (extra ? (float)extra / 256.f : 1.f) * (cat ? cat->scale : 1.f));
                if (type == PDEF_DOOR && bytes >= 0x9c) {
                    /* Rare 16.16 at 0x84; doorType u16 at 0x9a. */
                    pr->max_frac = (float)(int32_t)be32(p + 0x84) / 65536.f;
                    pr->door_type = (int)be16(p + 0x9a);
                }
                pr->mdl = load_model(model);
                if (pr->mdl)
                    g_nprop++;
            }
        } else if (type == PDEF_GUARD && bytes >= 28) {
            /* GuardRecord: chrnum@4 pad@6 BodyID@8 HeadID@16 (s16). */
            int16_t pad = (int16_t)be16(p + 6);
            int16_t body = (int16_t)be16(p + 8);
            int16_t head = (int16_t)be16(p + 0x16);
            const uint8_t *pd = pad_bytes(st, n, pad_off, pad3_off, npad, nbound, pad);
            const PortPropCat *cat = chr_by_id(body);
            PortModel *mdl;
            if (pd && body >= 0 && (mdl = load_chr(body)) != NULL) {
                PortProp *pr = &g_prop[g_nprop];
                fill_pad_prop(pr, type, body, pad, pd,
                              (cat ? cat->scale : 1.f) *
                                  (mdl->fit_scale != 0.f ? mdl->fit_scale : 1.f));
                pr->mdl = mdl;
                if (mdl->have_head) {
                    pr->head_off[0] = mdl->head_off[0];
                    pr->head_off[1] = mdl->head_off[1] - mdl->fit_ymin;
                    pr->head_off[2] = mdl->head_off[2];
                    pr->head_rx = mdl->head_rx;
                    pr->head_ry = mdl->head_ry;
                    pr->head_rz = mdl->head_rz;
                }
                if (head >= PORT_CHR_HEAD_START)
                    pr->head = load_chr(head);
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
    g_have_idle = 0;
    g_have_walk = 0;
    g_walkers = 0;
    g_walk_prop = -1;
    g_walk_frame = 0;
    g_walk_nframes = 0;
    g_walk_fit_scale = 0.f;
    g_walk_fit_ymin = 0.f;
    g_pose_rest = NULL;
    memset(g_idle_rest, 0, sizeof g_idle_rest);
    memset(g_walk_rest, 0, sizeof g_walk_rest);
    g_walk_info[0] = 0;
    g_retail_slab_ok = 0;
    memset(&g_retail_slab, 0, sizeof g_retail_slab);
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
    load_idle_rest();
    parse_setup(g_setup, g_setup_len);
    assign_walkers();
    (void)load_wppk();
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

int port_prop_guard_count(void)
{
    int i, n = 0;
    for (i = 0; i < g_nprop; i++) {
        if (g_prop[i].type == PDEF_GUARD)
            n++;
    }
    return n;
}

int port_prop_have_idle(void) { return g_have_idle; }

int port_prop_have_walk(void) { return g_have_walk; }

int port_prop_walk_count(void) { return g_walkers; }

int port_prop_guard_parts(void)
{
    int i;
    for (i = 0; i < g_nprop; i++) {
        if (g_prop[i].type == PDEF_GUARD && g_prop[i].mdl)
            return g_prop[i].mdl->npart;
    }
    return 0;
}

const char *port_prop_idle_info(void)
{
    if (!g_walk_info[0])
        return g_idle_info[0] ? g_idle_info : "idle=0";
    snprintf(g_pose_info, sizeof g_pose_info, "%s %s",
             g_idle_info[0] ? g_idle_info : "idle=0", g_walk_info);
    return g_pose_info;
}

int port_prop_walk_xz(float *x, float *z)
{
    if (g_walk_prop < 0 || g_walk_prop >= g_nprop)
        return -1;
    if (x)
        *x = g_prop[g_walk_prop].pos[0];
    if (z)
        *z = g_prop[g_walk_prop].pos[2];
    return 0;
}

int port_prop_guard_xz(int want, float *x, float *z)
{
    int i, n = 0;
    for (i = 0; i < g_nprop; i++) {
        if (g_prop[i].type != PDEF_GUARD)
            continue;
        if (n == want) {
            if (x)
                *x = g_prop[i].pos[0];
            if (z)
                *z = g_prop[i].pos[2];
            return 0;
        }
        n++;
    }
    return -1;
}


static PortModel *load_wppk(void)
{
    PortModel *m = load_named(PORT_GUN_WPPK_ID, "gun", "G", &k_wppk_gun, 0, NULL);
    if (m && m->npart)
        return m;
    return NULL;
}

int port_prop_viewgun_parts(void) { return g_viewgun_parts; }

/*
 * Static first-person PP7. Walk Rare nodes (no recoil/reload). Model +Z is
 * Rare forward; G1 looks -Z so hold * R180 * part. Camera-space via .view.
 */
static int viewgun_is_flash(int p, const PortPart *pt)
{
    /* SKEL_FLASH: two ~20-cmd z=0 cards walked first. Idle hides them. */
    return p < 2 && pt && pt->pri_n <= 22u;
}

int port_prop_fill_viewgun(G1RoomDl *out, int cap)
{
    PortModel *m;
    int p, k = 0;
    float hold[4][4], r180[4][4];
    int show_flash;

    g_viewgun_parts = 0;
    if (!out || cap < 1)
        return 0;
    m = load_wppk();
    if (!m || m->npart == 0)
        return 0;
    show_flash = port_gun_flash_frames() > 0;
    mtx_local(hold, PORT_VIEWGUN_X, PORT_VIEWGUN_Y, PORT_VIEWGUN_Z, 0.f, 0.f, 0.f);
    mtx_local(r180, 0.f, 0.f, 0.f, 0.f, PI_F, 0.f);
    for (p = 0; p < m->npart && k < cap; p++) {
        const PortPart *pt = &m->part[p];
        float part[4][4], tmp[4][4], world[4][4];
        float rx, ry, rz;
        if (!pt->pri || pt->pri_n == 0)
            continue;
        if (viewgun_is_flash(p, pt) && !show_flash)
            continue;
        mtx_local(part, pt->ox, pt->oy, pt->oz, pt->rx, pt->ry, pt->rz);
        mtx_mul4(tmp, r180, part);
        mtx_mul4(world, hold, tmp);
        mtx_euler(world, &rx, &ry, &rz);
        if (rx * rx + ry * ry + rz * rz < 1e-8f)
            rx = ry = rz = 0.f;
        memset(&out[k], 0, sizeof out[k]);
        out[k].pri = pt->pri;
        out[k].pri_n = pt->pri_n;
        out[k].sec = pt->sec;
        out[k].sec_n = pt->sec_n;
        out[k].ox = world[0][3];
        out[k].oy = world[1][3];
        out[k].oz = world[2][3];
        out[k].rx = rx;
        out[k].ry = ry;
        out[k].rz = rz;
        out[k].seg5 = (uintptr_t)m->file;
        out[k].seg4 = pt->vtx4;
        out[k].view = 1;
        k++;
    }
    g_viewgun_parts = k;
    return k;
}

static int near_room(const PortProp *pr, const float room1[3], const float *room_xyz,
                     int nrooms, const uint8_t *room_ids)
{
    float dx, dy, dz, best, d;
    int r, near = 0;

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
    /* Player is room-1 local; pads/portals are world. Same 4000 cap. */
    {
        float px = port_player_x() + room1[0];
        float py = port_player_y() + room1[1];
        float pz = port_player_z() + room1[2];
        dx = pr->pos[0] - px;
        dy = pr->pos[1] - py;
        dz = pr->pos[2] - pz;
        if (dx * dx + dy * dy + dz * dz <= PORT_PROP_NEAR * PORT_PROP_NEAR)
            return 1;
    }
    if (!near && nrooms > 0)
        return 0;
    if (nrooms <= 0) {
        dx = pr->pos[0] - room1[0];
        dy = pr->pos[1] - room1[1];
        dz = pr->pos[2] - room1[2];
        if (dx * dx + dy * dy + dz * dz > PORT_PROP_NEAR * PORT_PROP_NEAR)
            return 0;
    }
    return 1;
}


/* Closed-door slab. Prefer retail Pgas_plant_met1_do1 (96-vert mesh,
 * SETTEX 685-688,706). G_VTX is 0x04xxxxxx — bind the node vertex bank
 * at file+0xC0 as seg 4. Do not bind seg 3 (G_MTX LOAD would replace
 * the camera). Root GROUP origin is gas-plant world; zero it and use
 * the fitted portal pose. Fallback: fitted G1DL quad, SETTEX 685. */
static uint8_t g_slab_file[192];
static PortModel g_slab_mdl;
static int g_slab_ok;
#define SLAB_DOOR_TEX 685u /* Pgas_plant_met1_do1 tile 0; imagelist "685" */
#define SLAB_RETAIL_ID 158
#define SLAB_RETAIL_HALF_W 350.f
#define SLAB_RETAIL_BOTTOM -787.f

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void wr_vtx(uint8_t *v, int16_t x, int16_t y, int16_t z, int16_t s, int16_t t)
{
    wr16(v + 0, (uint16_t)x);
    wr16(v + 2, (uint16_t)y);
    wr16(v + 4, (uint16_t)z);
    wr16(v + 8, (uint16_t)s);
    wr16(v + 10, (uint16_t)t);
    v[12] = 118;
    v[13] = 112;
    v[14] = 98;
    v[15] = 255;
}

static int gdl_has_seg4_vtx(const uint8_t *pri, uint32_t n)
{
    uint32_t i;
    if (!pri || n == 0)
        return 0;
    for (i = 0; i < n; i++) {
        if (pri[i * 8u] == (uint8_t)G_VTX && pri[i * 8u + 4] == 4)
            return 1;
    }
    return 0;
}

static int slab_is_retail(const PortModel *m)
{
    return m && m->npart > 0 && m->part[0].vtx4 != 0 &&
           gdl_has_seg4_vtx(m->part[0].pri, m->part[0].pri_n);
}

static void slab_bounds(const PortModel *m, float *half_w, float *bottom)
{
    const uint8_t *v;
    uint32_t i, n;
    int16_t minx, maxx, miny;

    *half_w = SLAB_RETAIL_HALF_W;
    *bottom = SLAB_RETAIL_BOTTOM;
    if (!m || !m->part[0].vtx4 || m->part[0].nvtx < 3)
        return;
    v = (const uint8_t *)m->part[0].vtx4;
    n = m->part[0].nvtx;
    minx = maxx = (int16_t)((v[0] << 8) | v[1]);
    miny = (int16_t)((v[2] << 8) | v[3]);
    for (i = 1; i < n; i++) {
        const uint8_t *p = v + i * 16u;
        int16_t x = (int16_t)((p[0] << 8) | p[1]);
        int16_t y = (int16_t)((p[2] << 8) | p[3]);
        if (x < minx)
            minx = x;
        if (x > maxx)
            maxx = x;
        if (y < miny)
            miny = y;
    }
    *half_w = 0.5f * (float)(maxx - minx);
    if (*half_w < 1.f)
        *half_w = SLAB_RETAIL_HALF_W;
    *bottom = (float)miny;
}

static PortModel *slab_quad(void)
{
    /* Two proven 3-vert G_VTX+G_TRI4 packets (same encoding as the magenta
     * door test). A 4-vert packet left stale room verts on one triangle. */
    uint32_t vtx = ((uint32_t)(uint8_t)G_VTX << 24) | (0x20u << 16);
    uint32_t end = (uint32_t)(uint8_t)G_ENDDL << 24;
    const uint32_t v1 = 4 + 7 * 8, v2 = 4 + 7 * 8 + 48;
    const int16_t us = 32 * 32, vt = 33 * 32; /* tile 0 is 32x33 */

    if (g_slab_ok)
        return &g_slab_mdl;
    memset(g_slab_file, 0, sizeof g_slab_file);
    wr32(g_slab_file, PORT_BG_MAGIC_G1DL);
    wr32(g_slab_file + 4, 0xBB002801u); /* G_TEXTURE scale 1,1 */
    wr32(g_slab_file + 8, 0xFFFFFFFFu);
    wr32(g_slab_file + 12, 0xC0000003u); /* G_SETTEX TILE */
    wr32(g_slab_file + 16, SLAB_DOOR_TEX);
    wr32(g_slab_file + 20, vtx);
    wr32(g_slab_file + 24, 0x05000000u | v1);
    wr32(g_slab_file + 28, 0xB1000002u);
    wr32(g_slab_file + 32, 0x00000010u);
    wr32(g_slab_file + 36, vtx);
    wr32(g_slab_file + 40, 0x05000000u | v2);
    wr32(g_slab_file + 44, 0xB1000002u);
    wr32(g_slab_file + 48, 0x00000010u);
    wr32(g_slab_file + 52, end);
    wr_vtx(g_slab_file + v1 + 0, -90, 0, 0, 0, vt);
    wr_vtx(g_slab_file + v1 + 16, 90, 0, 0, us, vt);
    wr_vtx(g_slab_file + v1 + 32, 90, 260, 0, us, 0);
    wr_vtx(g_slab_file + v2 + 0, -90, 0, 0, 0, vt);
    wr_vtx(g_slab_file + v2 + 16, 90, 260, 0, us, 0);
    wr_vtx(g_slab_file + v2 + 32, -90, 260, 0, 0, 0);
    memset(&g_slab_mdl, 0, sizeof g_slab_mdl);
    g_slab_mdl.file = g_slab_file;
    g_slab_mdl.file_len = v2 + 48;
    g_slab_mdl.part[0].pri = g_slab_file + 4;
    g_slab_mdl.part[0].pri_n = 7;
    g_slab_mdl.npart = 1;
    g_slab_ok = 1;
    return &g_slab_mdl;
}

static PortModel *slab_door(void)
{
    PortModel *m;
    int p;

    if (g_retail_slab_ok)
        return &g_retail_slab;
    m = load_model(SLAB_RETAIL_ID);
    if (m && be32(m->file) != PORT_BG_MAGIC_G1DL && slab_is_retail(m)) {
        g_retail_slab = *m;
        for (p = 0; p < g_retail_slab.npart; p++) {
            /* Replace gas-plant GROUP origin with the fitted portal pose. */
            g_retail_slab.part[p].ox = 0.f;
            g_retail_slab.part[p].oy = 0.f;
            g_retail_slab.part[p].oz = 0.f;
            g_retail_slab.part[p].rx = 0.f;
            g_retail_slab.part[p].ry = 0.f;
            g_retail_slab.part[p].rz = 0.f;
        }
        g_retail_slab_ok = 1;
        return &g_retail_slab;
    }
    return slab_quad();
}

static int emit_parts(G1RoomDl *out, int cap, int k, const PortProp *pr, const PortModel *mdl,
                      const float room1[3], float extra_x, float extra_y, float extra_z,
                      float extra_rx, float extra_ry, float extra_rz, float add_yaw,
                      float wdx, float wdy, float wdz)
{
    int p;
    float th, c, s;
    float extra[4][4];

    if (!mdl)
        return k;
    th = pr->yaw * (PI_F / 180.f);
    c = cosf(th);
    s = sinf(th);
    mtx_local(extra, extra_x, extra_y, extra_z, extra_rx, extra_ry, extra_rz);
    for (p = 0; p < mdl->npart && k < cap; p++) {
        const PortPart *pt = &mdl->part[p];
        float loc[4][4], world[4][4];
        float lx, ly, lz, wx, wz, rx, ry, rz;
        if (!pt->pri || pt->pri_n == 0)
            continue;
        mtx_local(loc, pt->ox, pt->oy, pt->oz, pt->rx, pt->ry, pt->rz);
        mtx_mul4(world, extra, loc);
        lx = world[0][3];
        ly = world[1][3];
        lz = world[2][3];
        mtx_euler(world, &rx, &ry, &rz);
        if (rx * rx + ry * ry + rz * rz < 1e-8f)
            rx = ry = rz = 0.f;
        if (mdl->fit_scale != 1.f || mdl->fit_ymin != 0.f) {
            float sc = (pr->scale != 0.f) ? pr->scale : 1.f;
            lx *= sc;
            ly = (ly - mdl->fit_ymin) * sc;
            lz *= sc;
        }
        wx = c * lx + s * lz;
        wz = -s * lx + c * lz;
        memset(&out[k], 0, sizeof out[k]);
        out[k].pri = pt->pri;
        out[k].pri_n = pt->pri_n;
        out[k].sec = pt->sec;
        out[k].sec_n = pt->sec_n;
        out[k].ox = pr->pos[0] - room1[0] + wx + wdx;
        out[k].oy = pr->pos[1] - room1[1] + ly + wdy;
        out[k].oz = pr->pos[2] - room1[2] + wz + wdz;

        out[k].yaw = pr->yaw + add_yaw;
        out[k].scale = pr->scale;
        out[k].seg5 = (uintptr_t)mdl->file;
        out[k].seg4 = pt->vtx4;
        out[k].rx = rx;
        out[k].ry = ry;
        out[k].rz = rz;
        k++;
    }
    return k;
}

/*
 * Instant open pose from pad + Rare doorType. Facility start doors
 * (UsetuparkZ index 32+, pads 66+) are DOORTYPE_SWINGING / maxFrac=90.
 * Hinge is pad + HALF_W along look-tangent (not boundpad bbox). Swing
 * sign is away from the player recorded at use. Sliding parks along
 * that tangent by 2*HALF_W. Vertical lifts. No accel / clip-to-bbox.
 */
static void door_open_pose(const PortProp *pr, float *add_yaw, float *dx, float *dy,
                           float *dz)
{
    float lx = pr->look[0], lz = pr->look[2];
    float len = sqrtf(lx * lx + lz * lz);
    float nx, nz, tx, tz, hw = PORT_DOOR_HALF_W;
    int side = port_stan_door_side_at(pr->pos[0], pr->pos[2]);
    int dtype = pr->door_type;

    *add_yaw = *dx = *dy = *dz = 0.f;
    if (len < 1e-4f) {
        nx = 0.f;
        nz = 1.f;
    } else {
        nx = lx / len;
        nz = lz / len;
    }
    tx = -nz;
    tz = nx;

    if (dtype == DOORTYPE_SLIDING) {
        float s = (side >= 0) ? 1.f : -1.f;
        *dx = tx * PORT_DOOR_SLIDE * s;
        *dz = tz * PORT_DOOR_SLIDE * s;
        return;
    }
    if (dtype == DOORTYPE_VERTICAL) {
        *dy = PORT_DOOR_SLIDE;
        return;
    }
    /* SWINGING and every other type: 90° around the +T hinge. */
    {
        float ang = (side > 0) ? PORT_DOOR_OPEN_YAW : -PORT_DOOR_OPEN_YAW;
        float th, c, s, relx, relz, rx, rz;
        if (pr->max_frac > 1.f && pr->max_frac <= 180.f)
            ang = (ang < 0.f) ? -pr->max_frac : pr->max_frac;
        th = ang * (PI_F / 180.f);
        c = cosf(th);
        s = sinf(th);
        /* H = P + hw T; new = H + R*(P-H); delta = hw T - R*(hw T). */
        relx = hw * tx;
        relz = hw * tz;
        rx = c * relx + s * relz;
        rz = -s * relx + c * relz;
        *dx = relx - rx;
        *dz = relz - rz;
        *add_yaw = ang;
    }
}

int port_prop_fill_rooms(G1RoomDl *out, int cap, const float room1[3],
                         const float *room_xyz, int nrooms, const uint8_t *room_ids)
{
    int i, k = 0;
    g_drawn = 0;
    if (!out || cap < 1 || !room1)
        return 0;
    /* Pad doors first so Facility openings are not eaten by guard parts. */
    for (i = 0; i < g_nprop && k < cap; i++) {
        PortProp *pr = &g_prop[i];
        float add_yaw = 0.f, odx = 0.f, ody = 0.f, odz = 0.f;
        if (pr->type != PDEF_DOOR || !pr->mdl || pr->mdl->npart == 0)
            continue;
        if (!near_room(pr, room1, room_xyz, nrooms, room_ids))
            continue;
        if (port_stan_door_is_open_at(pr->pos[0], pr->pos[2]))
            door_open_pose(pr, &add_yaw, &odx, &ody, &odz);
        k = emit_parts(out, cap, k, pr, pr->mdl, room1, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                       add_yaw, odx, ody, odz);
    }
    /*
     * Start-hallway openings have no PROPDEF_DOOR (those pads sit in the
     * gas-plant cluster). Fit a closed G1DL slab on door-sized portals.
     */
    {
        PortModel *slab = slab_door();
        int retail = slab_is_retail(slab);
        int o, no = port_stage_opening_count();
        float half_w = PORT_DOOR_HALF_W, bottom = 0.f;
        float pwx = room1[0] + port_player_x();
        float pwz = room1[2] + port_player_z();
        float floor_y = room1[1] + (port_player_y() - 175.f);
        float th = port_player_theta() * (PI_F / 180.f);
        float lookx = sinf(th), lookz = -cosf(th);
        if (retail)
            slab_bounds(slab, &half_w, &bottom);
        for (o = 0; o < no && k < cap; o++) {
            float pos[3], yaw = 0.f, width = 0.f;
            PortProp tmp, probe;
            int j, covered = 0;
            float lx, lz, tox, toz;
            int ra = 0, rb = 0, in_walk = 0, r;
            if (port_stage_opening(o, pos, &yaw, &width, &ra, &rb) != 0)
                continue;
            for (r = 0; r < nrooms; r++) {
                if (room_ids && (room_ids[r] == ra || room_ids[r] == rb)) {
                    in_walk = 1;
                    break;
                }
            }
            if (!in_walk)
                continue;
            memset(&probe, 0, sizeof probe);
            probe.pos[0] = pos[0];
            probe.pos[1] = pos[1];
            probe.pos[2] = pos[2];
            if (!near_room(&probe, room1, room_xyz, nrooms, room_ids))
                continue;
            for (j = 0; j < g_nprop; j++) {
                float dx, dy, dz;
                if (g_prop[j].type != PDEF_DOOR)
                    continue;
                dx = g_prop[j].pos[0] - pos[0];
                dy = g_prop[j].pos[1] - pos[1];
                dz = g_prop[j].pos[2] - pos[2];
                if (dx * dx + dy * dy + dz * dz < 250.f * 250.f) {
                    covered = 1;
                    break;
                }
            }
            if (covered)
                continue;
            lx = pos[0] - pwx;
            lz = pos[2] - pwz;
            if (lx * lx + lz * lz > 900.f * 900.f)
                continue;
            if (lx * lx + lz * lz < 250.f * 250.f)
                continue;
            tox = pos[0] - pwx;
            toz = pos[2] - pwz;
            if (tox * lookx + toz * lookz < 40.f)
                continue;
            /* Upper/catwalk portals sit hundreds of units above the floor. */
            if (pos[1] > floor_y + 200.f)
                continue;
            memset(&tmp, 0, sizeof tmp);
            tmp.pos[0] = pos[0];
            tmp.pos[1] = floor_y;
            tmp.pos[2] = pos[2];
            /* Face the player so the XY slab is not edge-on / backface. */
            if (yaw == 90.f)
                tmp.yaw = (pwx > pos[0]) ? 90.f : -90.f;
            else
                tmp.yaw = (pwz > pos[2]) ? 0.f : 180.f;
            tmp.scale = (width > 1.f) ? (width / (2.f * half_w)) : (PORT_DOOR_HALF_W / half_w);
            tmp.mdl = slab;
            k = emit_parts(out, cap, k, &tmp, slab, room1, 0.f, -bottom * tmp.scale, 0.f,
                           0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
        }
        /* Start alcoves are in-room GDL, not portals. One left-wall slab
         * when no door-sized portal already sits on that wall. */
        if (k < cap && no > 0) {
            float leftx = lookz, leftz = -lookx;
            int o, have_left = 0, no = port_stage_opening_count();
            for (o = 0; o < no; o++) {
                float pos[3], yaw, width, tox, toz, d2;
                int ra, rb;
                if (port_stage_opening(o, pos, &yaw, &width, &ra, &rb) != 0)
                    continue;
                tox = pos[0] - pwx;
                toz = pos[2] - pwz;
                d2 = tox * tox + toz * toz;
                if (d2 < 400.f * 400.f && tox * leftx + toz * leftz > 50.f)
                    have_left = 1;
            }
            if (!have_left) {
                PortProp tmp;
                memset(&tmp, 0, sizeof tmp);
                tmp.pos[0] = pwx + leftx * 160.f;
                tmp.pos[1] = floor_y;
                tmp.pos[2] = pwz + leftz * 160.f;
                tmp.yaw = (pwz + leftz * 160.f > pwz) ? 0.f : 180.f;
                /* Face the player: left wall is +Z at spawn, yaw 0 faces +Z. */
                if (leftz > 0.f)
                    tmp.yaw = 180.f;
                else if (leftz < 0.f)
                    tmp.yaw = 0.f;
                else
                    tmp.yaw = (leftx > 0.f) ? -90.f : 90.f;
                tmp.scale = 1.15f * (PORT_DOOR_HALF_W / half_w);
                tmp.mdl = slab;
                k = emit_parts(out, cap, k, &tmp, slab, room1, 0.f, -bottom * tmp.scale,
                               0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
            }
        }
    }
    for (i = 0; i < g_nprop && k < cap; i++) {
        PortProp *pr = &g_prop[i];
        if (!pr->mdl || pr->mdl->npart == 0 || pr->type == PDEF_DOOR)
            continue;
        if (!near_room(pr, room1, room_xyz, nrooms, room_ids))
            continue;
        if (pr->type == PDEF_GUARD &&
            port_stan_guard_dead_at(pr->pos[0], pr->pos[2]))
            continue;
        k = emit_parts(out, cap, k, pr, pr->mdl, room1, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                       0.f, 0.f, 0.f, 0.f);
        if (pr->head && pr->head->npart)
            k = emit_parts(out, cap, k, pr, pr->head, room1, pr->head_off[0], pr->head_off[1],
                           pr->head_off[2], pr->head_rx, pr->head_ry, pr->head_rz, 0.f,
                           0.f, 0.f, 0.f);
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
