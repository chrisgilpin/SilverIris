#include "stage.h"

#include "c0pack.h"
#include "gfx/tmem.h"
#include "inflate1172.h"
#include "pack_dma.h"
#include "rng/random.h"
#include "player/move.h"
#include "gfx/gbi_interp.h"
#include "prop.h"
#include "player/stan_walk.h"

#include "../../overrides/lv_clock.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BG_SEG_BASE 0x0F000000u
#define BG_SEG_BIAS 0xF1000000u
#define BG_ROOM_BYTES 24
#define BG_HDR_BYTES 64
#define PORT_MAX_BG_ROOMS 256
#define PORT_MAX_PORTALS 200
#define PORT_WALK_DEPTH 2
#define PORT_WALK_MAX 12
#define PORT_DRAW_MAX (PORT_WALK_MAX + PORT_PROP_MAX_DRAW)

typedef struct {
    int id;
    int files_id;
    const char *bg;
    const char *stan;
} StageFiles;

typedef struct {
    float pos[3];
    size_t pri_off;
    size_t pri_csize;
    uint32_t pri_ngfx;
    uint8_t *pri;
    size_t pri_len;
    int pri_raw;
    int pri_c0;
    size_t vtx_off;
    size_t vtx_csize;
    uint8_t *vtx;
    size_t vtx_len;
    size_t sec_off;
    size_t sec_csize;
    uint32_t sec_ngfx;
    uint8_t *sec;
    size_t sec_len;
} PortBgRoom;

typedef struct {
    uint8_t a, b;
} PortPortal;

static const StageFiles k_stages[] = {
    {PORT_LEVEL_FACILITY, PORT_LEVEL_FACILITY, "assets/obseg/bg/bg_ark_all_p.bin",
     "assets/obseg/stan/Tbg_ark_all_p_stanZ.bin"},
    {PORT_LEVEL_FACILITY_MP, PORT_LEVEL_FACILITY, "assets/obseg/bg/bg_ark_all_p.bin",
     "assets/obseg/stan/Tbg_ark_all_p_stanZ.bin"},
    {PORT_LEVEL_COMPLEX, PORT_LEVEL_COMPLEX, "assets/obseg/bg/bg_ref_all_p.bin",
     "assets/obseg/stan/Tbg_ref_all_p_stanZ.bin"},
};

static uint8_t *g_bg;
static size_t g_bg_len;
static uint8_t *g_stan;
static size_t g_stan_len;
static int g_level = -1;
static int g_rooms;
static int g_bg_rooms;
static int g_gdl_raw;
static int g_gdl_c0;
static int g_gdl_vtx;
static int g_gdl_sec;
static PortBgRoom g_rm[PORT_MAX_BG_ROOMS];
static PortPortal g_portals[PORT_MAX_PORTALS];
static int g_nportals;
static int g_cur_room;
static int g_rooms_walked;
static void *g_first_room;
static char g_stage_err[160];

static void set_stage_err(const char *s)
{
    size_t n = strlen(s);
    if (n >= sizeof g_stage_err)
        n = sizeof g_stage_err - 1;
    memcpy(g_stage_err, s, n);
    g_stage_err[n] = 0;
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static float be_f32(const uint8_t *p)
{
    uint32_t u = be32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static uint32_t seg_to_off(uint32_t off)
{
    if ((off & 0xFF000000u) == BG_SEG_BASE)
        return (uint32_t)(off + BG_SEG_BIAS);
    return off;
}

static void *maybe_ptr(uint8_t *base, size_t n, uint32_t off)
{
    uint32_t rel = seg_to_off(off);
    if (rel >= n)
        return NULL;
    return base + rel;
}

static const StageFiles *find_stage(int level_id)
{
    size_t i;
    for (i = 0; i < sizeof k_stages / sizeof k_stages[0]; i++) {
        if (k_stages[i].id == level_id)
            return &k_stages[i];
    }
    return NULL;
}

static int copy_named(const char *name, uint8_t **out, size_t *out_len)
{
    const C0Pack *pack = port_pack();
    const C0PackEntry *e;
    uint8_t *copy;

    if (!pack)
        return PORT_STAGE_ERR_PACK;
    e = c0pack_find(pack, name);
    if (!e)
        e = c0pack_find_tail(pack, name);
    if (!e || e->size == 0)
        return PORT_STAGE_ERR_MISSING;
    copy = (uint8_t *)malloc(e->size);
    if (!copy)
        return PORT_STAGE_ERR_OOM;
    memcpy(copy, e->bytes, e->size);
    *out = copy;
    *out_len = e->size;
    return PORT_STAGE_OK;
}

static void clear_rooms(void)
{
    int i;
    for (i = 0; i < PORT_MAX_BG_ROOMS; i++) {
        if (g_rm[i].pri_c0)
            free(g_rm[i].pri);
        free(g_rm[i].vtx);
        if (g_rm[i].sec && g_rm[i].sec_csize)
            free(g_rm[i].sec);
        memset(&g_rm[i], 0, sizeof g_rm[i]);
    }
    g_nportals = 0;
    g_cur_room = 0;
    g_rooms_walked = 0;
    port_prop_unload();
}

static size_t next_field_end(uint8_t *bg, size_t n, uint8_t *rooms, int i, size_t start)
{
    size_t end = n;
    int j, f;
    for (j = i; j < PORT_MAX_BG_ROOMS; j++) {
        uint8_t *r = rooms + (size_t)j * BG_ROOM_BYTES;
        if (r + BG_ROOM_BYTES > bg + n)
            break;
        for (f = 0; f < 3; f++) {
            uint32_t cand = be32(r + (size_t)f * 4);
            uint32_t rel;
            if (!cand || !maybe_ptr(bg, n, cand))
                continue;
            rel = seg_to_off(cand);
            if (rel > start && rel < end)
                end = rel;
        }
        if (j > i)
            break;
    }
    return end;
}

/*
 * Rare load_bg_file: word[1] is the room table (bg_room_data[]), room 0 is
 * dummy, rooms 1..n until pPriMappingBin == 0. word[2] is the portal table
 * (bg_portal_data_entry: offset, roomA, roomB, control). Pointers stay
 * segmented (K17); we resolve at access time.
 *
 * word[0] == PORT_BG_MAGIC_G1DL means primary mappings are uncompressed
 * big-endian Fast3D GDLs (synthetic CI only). Retail files use 0 and a
 * 1172-compressed C0/4Tri GDL — we inflate those and walk G1.
 *
 * Draw walks the current room plus portal neighbors (depth 2). Room 1
 * stays the greyscale / SETTEX / clip path; neighbors use the same
 * interpreter with a look-at offset of (room.pos - room1.pos).
 */
static int fixup_bg(uint8_t *bg, size_t n)
{
    uint32_t magic, rooms_seg, portal_seg;
    uint8_t *rooms;
    int i;

    g_bg_rooms = 0;
    g_gdl_raw = 0;
    g_gdl_c0 = 0;
    g_gdl_vtx = 0;
    g_gdl_sec = 0;
    g_nportals = 0;
    memset(g_rm, 0, sizeof g_rm);

    if (n < BG_HDR_BYTES)
        return PORT_STAGE_ERR_FORMAT;
    magic = be32(bg);
    rooms_seg = be32(bg + 4);
    portal_seg = be32(bg + 8);
    rooms = (uint8_t *)maybe_ptr(bg, n, rooms_seg);
    if (!rooms)
        return PORT_STAGE_ERR_FORMAT;

    for (i = 1; i < PORT_MAX_BG_ROOMS; i++) {
        uint8_t *r = rooms + (size_t)i * BG_ROOM_BYTES;
        uint32_t pri, point, sec;
        uint8_t *gdl, *pt, *sp;
        size_t gdl_off, gdl_end;
        PortBgRoom *rm = &g_rm[i];

        if (r + BG_ROOM_BYTES > bg + n)
            break;
        pri = be32(r + 4);
        if (pri == 0)
            break;
        gdl = (uint8_t *)maybe_ptr(bg, n, pri);
        if (!gdl)
            break;
        g_bg_rooms++;
        rm->pos[0] = be_f32(r + 12);
        rm->pos[1] = be_f32(r + 16);
        rm->pos[2] = be_f32(r + 20);

        gdl_off = (size_t)(gdl - bg);
        gdl_end = next_field_end(bg, n, rooms, i, gdl_off);
        if (gdl_end > gdl_off) {
            if (magic == PORT_BG_MAGIC_G1DL) {
                rm->pri_off = gdl_off;
                rm->pri_ngfx = (uint32_t)((gdl_end - gdl_off) / 8u);
                rm->pri_raw = 1;
                if (i == 1)
                    g_gdl_raw = 1;
            } else if (magic == 0 && gdl_end > gdl_off + PORT_INFLATE1172_HEADER &&
                       gdl[0] == 0x11 && gdl[1] == 0x72) {
                rm->pri_off = gdl_off;
                rm->pri_csize = gdl_end - gdl_off;
            }
        }

        point = be32(r + 0);
        pt = (uint8_t *)maybe_ptr(bg, n, point);
        if (pt && pt + PORT_INFLATE1172_HEADER <= bg + n && pt[0] == 0x11 &&
            pt[1] == 0x72) {
            size_t vtx_off = (size_t)(pt - bg);
            size_t vtx_end = next_field_end(bg, n, rooms, i, vtx_off);
            if (vtx_end > vtx_off + PORT_INFLATE1172_HEADER) {
                rm->vtx_off = vtx_off;
                rm->vtx_csize = vtx_end - vtx_off;
            }
        }

        sec = be32(r + 8);
        sp = sec ? (uint8_t *)maybe_ptr(bg, n, sec) : NULL;
        if (sec && sp) {
            size_t sec_off = (size_t)(sp - bg);
            size_t sec_end = next_field_end(bg, n, rooms, i, sec_off);
            if (sec_end > sec_off + 8) {
                rm->sec_off = sec_off;
                if (magic == 0 && sp[0] == 0x11 && sp[1] == 0x72)
                    rm->sec_csize = sec_end - sec_off;
                else if (magic == PORT_BG_MAGIC_G1DL) {
                    rm->sec_ngfx = (uint32_t)((sec_end - sec_off) / 8u);
                    if (i == 1)
                        g_gdl_sec = 1;
                }
            }
        }
    }

    /* Portal list: walk until offset_portal == 0. Do not rewrite u32s (LP64). */
    if (portal_seg) {
        uint8_t *p = (uint8_t *)maybe_ptr(bg, n, portal_seg);
        while (p && p + 8 <= bg + n && g_nportals < PORT_MAX_PORTALS) {
            uint32_t off = be32(p);
            uint8_t a, b;
            if (off == 0)
                break;
            if (!maybe_ptr(bg, n, off))
                break;
            a = p[4];
            b = p[5];
            if (a && b) {
                g_portals[g_nportals].a = a;
                g_portals[g_nportals].b = b;
                g_nportals++;
            }
            p += 8;
        }
    }
    return PORT_STAGE_OK;
}

static int fixup_stan(uint8_t *stan, size_t n)
{
    uint32_t off;
    uint8_t *p;
    int rooms = 0;
    void *first;

    if (n < 12)
        return PORT_STAGE_ERR_FORMAT;
    off = be32(stan + 4);
    if (off == 0) {
        g_first_room = NULL;
        g_rooms = 0;
        return PORT_STAGE_OK;
    }
    first = maybe_ptr(stan, n, off);
    g_first_room = first;
    rooms = 1;
    p = stan + 8;
    while (p + 4 <= stan + n) {
        uint32_t w = be32(p);
        if (w == 0)
            break;
        if (!maybe_ptr(stan, n, w))
            break;
        rooms++;
        p += 4;
        if (rooms > 512)
            break;
    }
    g_rooms = rooms;
    return PORT_STAGE_OK;
}

void port_stage_unload(void)
{
    g1_tex_set_pack(NULL);
    g1_tex_unload();
    clear_rooms();
    port_stan_unload();
    free(g_bg);
    free(g_stan);
    g_bg = NULL;
    g_stan = NULL;
    g_bg_len = 0;
    g_stan_len = 0;
    g_level = -1;
    g_rooms = 0;
    g_bg_rooms = 0;
    g_gdl_raw = 0;
    g_gdl_c0 = 0;
    g_gdl_vtx = 0;
    g_gdl_sec = 0;
    g_first_room = NULL;
}

static int inflate_blob(const uint8_t *src, size_t csize, uint8_t **out, size_t *out_len)
{
    size_t need = 0;
    uint8_t *exp;
    int rc;

    rc = bgDecompress(src, csize, NULL, 0, &need);
    if (rc != PORT_INFLATE1172_OK || need == 0)
        return -1;
    exp = (uint8_t *)malloc(need);
    if (!exp)
        return -2;
    rc = bgDecompress(src, csize, exp, need, &need);
    if (rc != PORT_INFLATE1172_OK) {
        free(exp);
        return -1;
    }
    *out = exp;
    *out_len = need;
    return 0;
}

int port_stage_load(int level_id)
{
    const StageFiles *st = find_stage(level_id);
    uint8_t *bg = NULL, *stan = NULL;
    size_t bg_len = 0, stan_len = 0;
    int rc, i, stan_1172 = 0;

    if (!st) {
        set_stage_err("unknown level id");
        return PORT_STAGE_ERR_FORMAT;
    }
    port_stage_unload();
    g_stage_err[0] = 0;

    rc = copy_named(st->bg, &bg, &bg_len);
    if (rc != PORT_STAGE_OK) {
        set_stage_err("missing bg (pack has no bg_ark/bg_ref blob — re-extract DMA files)");
        return rc;
    }
    rc = copy_named(st->stan, &stan, &stan_len);
    if (rc != PORT_STAGE_OK) {
        free(bg);
        set_stage_err("missing stan (pack has no Tbg_*_stanZ blob — re-extract DMA files)");
        return rc;
    }
    if (stan_len >= 2 && stan[0] == 0x11 && stan[1] == 0x72) {
        uint8_t *exp = NULL;
        size_t elen = 0;
        if (inflate_blob(stan, stan_len, &exp, &elen) == 0) {
            free(stan);
            stan = exp;
            stan_len = elen;
            stan_1172 = 1;
        }
    }
    rc = fixup_bg(bg, bg_len);
    if (rc != PORT_STAGE_OK) {
        free(bg);
        free(stan);
        set_stage_err("bg header too small");
        return rc;
    }
    rc = fixup_stan(stan, stan_len);
    if (rc != PORT_STAGE_OK) {
        free(bg);
        free(stan);
        set_stage_err("stan header too small");
        return rc;
    }

    for (i = 1; i <= g_bg_rooms; i++) {
        PortBgRoom *rm = &g_rm[i];
        if (rm->pri_csize) {
            rc = inflate_blob(bg + rm->pri_off, rm->pri_csize, &rm->pri, &rm->pri_len);
            if (rc == 0) {
                rm->pri_ngfx = (uint32_t)(rm->pri_len / 8u);
                rm->pri_c0 = 1;
                if (i == 1)
                    g_gdl_c0 = 1;
            } else if (i == 1) {
                free(bg);
                free(stan);
                set_stage_err(rc == -2 ? "bg 1172 oom" : "bg 1172 inflate failed");
                return rc == -2 ? PORT_STAGE_ERR_OOM : PORT_STAGE_ERR_FORMAT;
            }
        }
        if (rm->vtx_csize) {
            if (inflate_blob(bg + rm->vtx_off, rm->vtx_csize, &rm->vtx, &rm->vtx_len) == 0) {
                if (i == 1)
                    g_gdl_vtx = 1;
            }
        }
        if (rm->sec_csize) {
            if (inflate_blob(bg + rm->sec_off, rm->sec_csize, &rm->sec, &rm->sec_len) == 0) {
                rm->sec_ngfx = (uint32_t)(rm->sec_len / 8u);
                if (i == 1)
                    g_gdl_sec = 1;
            }
        }
    }

    g_bg = bg;
    g_bg_len = bg_len;
    g_stan = stan;
    g_stan_len = stan_len;
    g_level = level_id;
    g_CurrentStageToLoad = st->files_id;
    g1_tex_set_pack(port_pack());
    port_rng_on_stage_load();
    {
        float sc = 1.0f;
        /* Retail 1172 tiles are s16 in level-scale space (bg.c). Synthetic
         * uncompressed stans in tests are already world units. */
        if (stan_1172) {
            if (level_id == PORT_LEVEL_FACILITY || level_id == PORT_LEVEL_FACILITY_MP)
                sc = 1.20648f;
            else if (level_id == PORT_LEVEL_COMPLEX)
                sc = 0.94285715f;
        }
        port_stan_set_scale(sc);
        if (g_bg_rooms >= 1)
            port_stan_set_world_origin(g_rm[1].pos[0], g_rm[1].pos[1], g_rm[1].pos[2]);
        port_stan_load(stan, stan_len);
    }
    port_player_spawn();
    port_prop_load(level_id);
    port_stan_clear_doors();
    port_stan_clear_guards();
    {
        int i, nd = port_prop_door_count();
        for (i = 0; i < nd; i++) {
            float dx, dz, lx, lz;
            if (port_prop_door_xz(i, &dx, &dz, &lx, &lz) == 0)
                port_stan_add_door(dx, dz, lx, lz);
        }
    }
    {
        int i, ng = port_prop_guard_count();
        for (i = 0; i < ng; i++) {
            float gx, gz;
            if (port_prop_guard_xz(i, &gx, &gz) == 0)
                port_stan_add_guard(gx, gz);
        }
    }
    {
        float pos[3], look[3];
        if (g_bg_rooms >= 1 && port_prop_intro(pos, look, NULL) == 0) {
            float lx = look[0], lz = look[2], th;
            float x = pos[0] - g_rm[1].pos[0];
            float y = pos[1] - g_rm[1].pos[1];
            float z = pos[2] - g_rm[1].pos[2];
            float ey;
            if (lx * lx + lz * lz < 1e-8f)
                th = 0.f;
            else {
                /* G1 / port theta=0 faces -Z: forward=(sin θ, -cos θ). */
                th = atan2f(lx, -lz) * (180.f / 3.14159265f);
                if (th < 0.f)
                    th += 360.f;
            }
            /* Rare camera Y = stan floor + 175. Empty synthetic stan keeps
             * pad Y so G1 greyscale / intro magenta tests stay put. Retail
             * C0 with no parsed tiles still lifts the camera off the floor. */
            if (port_stan_eye_y(x, z, &ey) == 0)
                y = ey;
            else if (g_gdl_c0)
                y += PORT_EYE_HEIGHT;
            port_player_set_pose(x, y, z, th);
        }
    }
    return PORT_STAGE_OK;
}

int port_stage_level_id(void) { return g_level; }

int port_stage_room_count(void) { return g_rooms; }

int port_stage_bg_rooms(void) { return g_bg_rooms; }

int port_stage_gdl_raw(void) { return g_gdl_raw; }

int port_stage_gdl_c0(void) { return g_gdl_c0; }

int port_stage_gdl_vtx(void) { return g_gdl_vtx; }

int port_stage_gdl_sec(void) { return g_gdl_sec; }

int port_stage_portal_count(void) { return g_nportals; }

int port_stage_current_room(void) { return g_cur_room; }

int port_stage_rooms_walked(void) { return g_rooms_walked; }

int port_stage_prop_count(void) { return port_prop_count(); }

int port_stage_prop_models(void) { return port_prop_models(); }

int port_stage_props_drawn(void) { return port_prop_drawn(); }

int port_stage_intro_pad(void) { return port_prop_intro_pad(); }

int port_stage_guard_count(void) { return port_prop_guard_count(); }

static int pick_current_room(void)
{
    float ox, oy, oz, px, py, pz, best, d;
    int i, cur;

    if (g_bg_rooms < 1)
        return 0;
    ox = g_rm[1].pos[0];
    oy = g_rm[1].pos[1];
    oz = g_rm[1].pos[2];
    px = port_player_x() + ox;
    py = port_player_y() + oy;
    pz = port_player_z() + oz;
    cur = 1;
    best = (g_rm[1].pos[0] - px) * (g_rm[1].pos[0] - px) +
           (g_rm[1].pos[1] - py) * (g_rm[1].pos[1] - py) +
           (g_rm[1].pos[2] - pz) * (g_rm[1].pos[2] - pz);
    for (i = 2; i <= g_bg_rooms; i++) {
        float dx = g_rm[i].pos[0] - px;
        float dy = g_rm[i].pos[1] - py;
        float dz = g_rm[i].pos[2] - pz;
        d = dx * dx + dy * dy + dz * dz;
        if (d < best) {
            best = d;
            cur = i;
        }
    }
    return cur;
}

static int select_rooms(uint8_t *out, int cap)
{
    uint8_t seen[PORT_MAX_BG_ROOMS];
    uint8_t q[PORT_MAX_BG_ROOMS];
    uint8_t depth[PORT_MAX_BG_ROOMS];
    int qh = 0, qt = 0, n = 0, i;
    int cur = pick_current_room();

    g_cur_room = cur;
    if (cur < 1 || cap < 1)
        return 0;
    memset(seen, 0, sizeof seen);
    q[qt] = (uint8_t)cur;
    depth[qt] = 0;
    qt++;
    seen[cur] = 1;
    while (qh < qt && n < cap) {
        int r = q[qh];
        int d = depth[qh];
        qh++;
        out[n++] = (uint8_t)r;
        if (d >= PORT_WALK_DEPTH)
            continue;
        for (i = 0; i < g_nportals; i++) {
            int o = 0;
            if (g_portals[i].a == r)
                o = g_portals[i].b;
            else if (g_portals[i].b == r)
                o = g_portals[i].a;
            else
                continue;
            if (o < 1 || o > g_bg_rooms || seen[o])
                continue;
            seen[o] = 1;
            if (qt >= PORT_MAX_BG_ROOMS)
                break;
            q[qt] = (uint8_t)o;
            depth[qt] = (uint8_t)(d + 1);
            qt++;
        }
    }
    return n;
}

static const uint8_t *room_pri(const PortBgRoom *rm)
{
    if (rm->pri_c0 && rm->pri)
        return rm->pri;
    if (rm->pri_raw && g_bg && rm->pri_off)
        return g_bg + rm->pri_off;
    return NULL;
}

static const uint8_t *room_sec(const PortBgRoom *rm)
{
    if (rm->sec && rm->sec_ngfx)
        return rm->sec;
    if (rm->sec_ngfx && g_bg && rm->sec_off)
        return g_bg + rm->sec_off;
    return NULL;
}

int port_stage_draw(void)
{
    G1RoomDl passes[PORT_DRAW_MAX];
    uint8_t ids[PORT_WALK_MAX];
    float rpos[PORT_MAX_BG_ROOMS * 3];
    int nsel, i, k;
    float ox, oy, oz;

    g_rooms_walked = 0;
    g_cur_room = 0;
    if (!g_bg || g_bg_rooms < 1)
        return 1;

    nsel = select_rooms(ids, PORT_WALK_MAX);
    ox = g_rm[1].pos[0];
    oy = g_rm[1].pos[1];
    oz = g_rm[1].pos[2];
    g1_set_segment(0xF, (uintptr_t)g_bg);
    if (g_rm[1].vtx)
        g1_set_segment(14, (uintptr_t)g_rm[1].vtx);
    g1_set_lookat(port_player_x(), port_player_y(), port_player_z(), port_player_theta());
    g1_set_pitch(port_player_phi());

    memset(passes, 0, sizeof passes);
    k = 0;
    for (i = 0; i < nsel; i++) {
        PortBgRoom *rm = &g_rm[ids[i]];
        const uint8_t *dl = room_pri(rm);
        if (!dl || rm->pri_ngfx == 0)
            continue;
        passes[k].pri = dl;
        passes[k].pri_n = rm->pri_ngfx;
        passes[k].sec = room_sec(rm);
        passes[k].sec_n = rm->sec_ngfx;
        passes[k].vtx = rm->vtx ? (uintptr_t)rm->vtx : 0;
        passes[k].ox = rm->pos[0] - ox;
        passes[k].oy = rm->pos[1] - oy;
        passes[k].oz = rm->pos[2] - oz;
        k++;
    }
    g_rooms_walked = k;
    for (i = 1; i <= g_bg_rooms; i++) {
        rpos[i * 3 + 0] = g_rm[i].pos[0];
        rpos[i * 3 + 1] = g_rm[i].pos[1];
        rpos[i * 3 + 2] = g_rm[i].pos[2];
    }
    {
        float room1[3];
        room1[0] = ox;
        room1[1] = oy;
        room1[2] = oz;
        k += port_prop_fill_rooms(passes + k, PORT_DRAW_MAX - k, room1, rpos, nsel, ids);
    }
    if (k == 0)
        return 1;
    if (k == 1)
        return g1_interpret_be_dl2(passes[0].pri, passes[0].pri_n, passes[0].sec, passes[0].sec_n);
    return g1_interpret_rooms(passes, k);
}

const uint8_t *port_stage_bg(size_t *size_out)
{
    if (size_out)
        *size_out = g_bg_len;
    return g_bg;
}

const uint8_t *port_stage_stan(size_t *size_out)
{
    if (size_out)
        *size_out = g_stan_len;
    return g_stan;
}

void *port_stage_stan_first_room(void) { return g_first_room; }

const char *port_stage_last_error(void) { return g_stage_err; }
