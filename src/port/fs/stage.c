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
#include <stdio.h>
#include <string.h>

#define BG_SEG_BASE 0x0F000000u
#define BG_SEG_BIAS 0xF1000000u
#define BG_ROOM_BYTES 24
#define BG_HDR_BYTES 64
#define PORT_MAX_BG_ROOMS 256
#define PORT_MAX_PORTALS 200
#define PORT_WALK_DEPTH 3
#define PORT_WALK_MAX 24
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
    uint8_t doorlike;
    float pos[3];
    float yaw;
    float width;
    float horiz, tall, thin;
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
/* Rare room_data_float2 = 1/levelscale. 1 for synthetic G1DL / 1-room C0. */
static float g_bg_inv = 1.f;
static PortBgRoom g_rm[PORT_MAX_BG_ROOMS];
static PortPortal g_portals[PORT_MAX_PORTALS];
static int g_portals_scaled;
static void scale_portal_geom(float inv);
static int g_nportals;
static int g_cur_room;
static int g_last_good_room;
static int g_rooms_walked;
static uint8_t g_walked[PORT_WALK_MAX];
static void *g_first_room;
static void bind_path_openings(void);
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

static void g1_clear_planes(void);
static void g1_clear_cutouts(void);

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
    g_portals_scaled = 0;
    g_cur_room = 0;
    g_last_good_room = 0;
    g_rooms_walked = 0;
    memset(g_walked, 0, sizeof g_walked);
    g1_clear_planes();
    g1_clear_cutouts();
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
 * Draw walks the current room plus portal neighbors (depth 3, cap 24;
 * depth 5 when current is r13/r14/r15 so neighbor-GDL rooms stay in frame;
 * depth 5 ground-only when current is r71/r7/r8 so open-path rooms walk
 * before Chris enters them, without spending extra hops on the r12 catwalk).
 * Doorlike portals with a closed bound slab (Rare PORTALFLAG_DISABLED)
 * do not enqueue the far room — closed-door occlusion. Open / frac>0
 * and non-doorlike archways still traverse.
 * Current room follows the camera eye on stacked xz so an upstairs
 * pose (r13/r15 eye ~737) is not pinned to the ground tile underfoot.
 * A ground-room BFS at the old cap never reached the catwalk. Room 1
 * stays the greyscale / SETTEX / clip path; neighbors use the same
 * interpreter with a look-at offset of (room.pos - room1.pos).
 */
/* World-space portal quad. Door-like: 80-450 wide, 80-500 tall, thin<=80. */
static void portal_geom(uint8_t *bg, size_t n, uint32_t off, PortPortal *po)
{
    uint8_t *pt = (uint8_t *)maybe_ptr(bg, n, off);
    int np, k;
    float mn[3], mx[3];
    float dx, dy, dz, horiz, thin;

    po->doorlike = 0;
    po->width = 0.f;
    po->yaw = 0.f;
    po->horiz = po->tall = po->thin = 0.f;
    po->pos[0] = po->pos[1] = po->pos[2] = 0.f;
    if (!pt || pt + 4 > bg + n)
        return;
    np = pt[0];
    if (np < 3 || np > 8 || pt + 4 + np * 12 > bg + n)
        return;
    mn[0] = mx[0] = be_f32(pt + 4);
    mn[1] = mx[1] = be_f32(pt + 8);
    mn[2] = mx[2] = be_f32(pt + 12);
    po->pos[0] = mn[0];
    po->pos[1] = mn[1];
    po->pos[2] = mn[2];
    for (k = 1; k < np; k++) {
        float x = be_f32(pt + 4 + k * 12);
        float y = be_f32(pt + 8 + k * 12);
        float z = be_f32(pt + 12 + k * 12);
        po->pos[0] += x;
        po->pos[1] += y;
        po->pos[2] += z;
        if (x < mn[0])
            mn[0] = x;
        if (y < mn[1])
            mn[1] = y;
        if (z < mn[2])
            mn[2] = z;
        if (x > mx[0])
            mx[0] = x;
        if (y > mx[1])
            mx[1] = y;
        if (z > mx[2])
            mx[2] = z;
    }
    po->pos[0] /= (float)np;
    po->pos[2] /= (float)np;
    /* Sit the model on the portal sill (Rare door origin is the pad floor). */
    po->pos[1] = mn[1];
    {
        dx = mx[0] - mn[0];
        dy = mx[1] - mn[1];
        dz = mx[2] - mn[2];
        horiz = dx > dz ? dx : dz;
        thin = dx < dz ? dx : dz;
        po->horiz = horiz;
        po->tall = dy;
        po->thin = thin;
        if (horiz < 80.f || horiz > 450.f || dy < 80.f || dy > 500.f || thin > 80.f)
            return;
        po->width = horiz;
        po->doorlike = 1;
        /* Thin-X opening faces ±X (yaw 90). First-triangle normal is 0 on a
         * vertical doorway and used to default yaw=0 — edge-on, no pixels. */
        po->yaw = (dx <= dz) ? 90.f : 0.f;
    }
}

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
    g_bg_inv = 1.f;
    g_nportals = 0;
    g_portals_scaled = 0;
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
                portal_geom(bg, n, off, &g_portals[g_nportals]);
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
    port_player_clear_spawn_origin();
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
    g_bg_inv = 1.f;
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
    /* Fitted Pgas / 4-row G1DL SETTEX 685-688,706. Load before rooms so a
     * late fill cannot miss them if slots are already stamped. */
    {
        static const unsigned k_door[] = { 685u, 686u, 687u, 688u, 706u };
        unsigned ti;
        g1_tex_begin_dl();
        for (ti = 0; ti < 5u; ti++)
            (void)g1_tex_settex(0xC0580002u, k_door[ti]);
    }
    port_rng_on_stage_load();
    {
        float level_sc = 1.0f;
        /* Rare tiles are s16 in level-scale space (bg.c). The browser
         * extractor inflates 1172 before packing, so live retail stan
         * has no 0x1172 magic — still needs 1.20648 (Facility). Small
         * uncompressed synthetics stay world units (extent < 800). */
        if (level_id == PORT_LEVEL_FACILITY || level_id == PORT_LEVEL_FACILITY_MP)
            level_sc = 1.20648f;
        else if (level_id == PORT_LEVEL_COMPLEX)
            level_sc = 0.94285715f;
        port_stan_set_scale(1.0f);
        if (g_bg_rooms >= 1)
            port_stan_set_world_origin(g_rm[1].pos[0], g_rm[1].pos[1], g_rm[1].pos[2]);
        port_stan_load(stan, stan_len);
        if (level_sc != 1.0f && (stan_1172 || port_stan_max_xz() > 800.0f)) {
            port_stan_set_scale(level_sc);
            port_stan_load(stan, stan_len);
        }
        /* Rare bgroomtrans: verts and room.pos * (1/levelscale). Pad pos
         * too (prop.c). Synthetic 1-room C0 / G1DL keep inv=1 so greyscale
         * and chris-xz tests stay bit-identical. */
        g_bg_inv = 1.f;
        if (g_gdl_c0 && g_bg_rooms > 8 && level_sc != 1.f &&
            port_stan_tile_count() > 100)
            g_bg_inv = 1.f / level_sc;
        if (g_bg_rooms >= 1)
            port_stan_set_world_origin(g_rm[1].pos[0] * g_bg_inv,
                                       g_rm[1].pos[1] * g_bg_inv,
                                       g_rm[1].pos[2] * g_bg_inv);
        /* Portal quads are authored in unscaled room.pos space. G1 draw,
         * stan tiles, and pad props already *inv; scale stored portal
         * geom into that same world so fitted door faces sit in the G1
         * hole instead of ~20% long (G1≠stan leftover). doorlike was
         * classified on the unscaled 80-450 band. */
        scale_portal_geom(g_bg_inv);
    }
    port_player_spawn();
    port_prop_load(level_id);
    port_prop_scale_world(g_bg_inv);
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
            float r1[3];
            float x, y, z, ey;
            r1[0] = r1[1] = r1[2] = 0.f;
            (void)port_stage_room1(r1);
            x = pos[0] - r1[0];
            y = pos[1] - r1[1];
            z = pos[2] - r1[2];
            if (lx * lx + lz * lz < 1e-8f)
                th = 0.f;
            else {
                /* G1 / port theta=0 faces -Z: forward=(sin θ, -cos θ). */
                th = atan2f(lx, -lz) * (180.f / 3.14159265f);
                if (th < 0.f)
                    th += 360.f;
            }
            /* Rare camera Y = stan floor + 175, in the same space as the
             * player (room-local GDL). Trying room1 origin first hit a
             * world-xz tile with Y=0 and wrote |room1.y|+175 (512 on
             * Facility) — a void above the corridor (hallway ~87). Prefer
             * origin 0 (tiles already room-local), then a nearby tile,
             * then world-space synthetics. Empty stan keeps pad Y.
             * Retail C0 already sits on r1*inv; origin 0 would miss. */
            {
                float pad_y = y;
                int got = 0;
                if (g_bg_inv != 1.f) {
                    /* Pad 167 * inv can sit on a stall sliver (live HUD
                     * x=-219 z=-2364, body in the left leaf). Rare snaps
                     * along look onto the hall. Prefer that over keeping
                     * pad xz just because eye_y hit a <200 tile. */
                    float pad_x = x, pad_z = z;
                    if (port_stan_snap_walkable(&x, &z, lx, lz, PORT_STAN_NEAR_XZ,
                                                &ey) == 0 &&
                        ey < 200.f) {
                        y = ey;
                        got = 1;
                    } else {
                        x = pad_x;
                        z = pad_z;
                        if (port_stan_eye_y(x, z, &ey) == 0 && ey < 200.f) {
                            y = ey;
                            got = 1;
                        } else if (port_stan_snap_walkable(&x, &z, 0.f, 0.f,
                                                          PORT_STAN_NEAR_XZ,
                                                          &ey) == 0) {
                            y = ey;
                            got = 1;
                        }
                    }
                } else {
                    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
                    if (port_stan_eye_y(x, z, &ey) == 0) {
                        y = ey;
                        got = 1;
                    } else if (port_stan_snap_walkable(&x, &z, lx, lz,
                                                      PORT_STAN_NEAR_XZ, &ey) ==
                               0) {
                        /* Pad 167 sits 144u past the last walkway tile; nearest
                         * any-tile is the catwalk (eye ~737), nearest low is the
                         * stair landing (white void). Snap along look onto the
                         * hallway-height tile Bond faces (eye ~87). */
                        y = ey;
                        got = 1;
                    } else if (g_bg_rooms >= 1) {
                        port_stan_set_world_origin(r1[0], r1[1], r1[2]);
                        if (port_stan_eye_y(x, z, &ey) == 0) {
                            y = ey;
                            got = 1;
                        } else if (port_stan_snap_walkable(&x, &z, lx, lz,
                                                          PORT_STAN_NEAR_XZ,
                                                          &ey) == 0) {
                            y = ey;
                            got = 1;
                        }
                    }
                }
                if (!got) {
                    if (port_stan_tile_count() > 0 || g_gdl_c0)
                        y = PORT_EYE_HEIGHT;
                }
                if (!(y == y) || y > 1.0e20f || y < -1.0e20f)
                    y = PORT_EYE_HEIGHT;
                (void)pad_y;
                (void)got;
            }
            /* Pad/hall snap can still sit on tile 147's unlinked south edge.
             * Live rAF then treated every zero-stick clip as trapped. Push
             * into the tile now so the first look-at is not in the wall. */
            (void)port_stan_nudge_off_wall(&x, &z, &y);
            port_player_set_spawn_origin(x, y, z, th);
            {
                int s, nseats = PORT_MAX_PLAYERS, ni = port_prop_intro_count();
                for (s = 1; s < nseats && s < ni; s++) {
                    float p[3], lk[3], sx, sy, sz, ths, ey2, lx2, lz2;
                    if (port_prop_intro_at(s, p, lk, NULL) != 0)
                        continue;
                    sx = p[0] - r1[0];
                    sy = p[1] - r1[1];
                    sz = p[2] - r1[2];
                    lx2 = lk[0];
                    lz2 = lk[2];
                    if (lx2 * lx2 + lz2 * lz2 < 1e-8f)
                        ths = th;
                    else {
                        ths = atan2f(lx2, -lz2) * (180.f / 3.14159265f);
                        if (ths < 0.f)
                            ths += 360.f;
                    }
                    if (g_bg_inv == 1.f)
                        port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
                    if (port_stan_eye_y(sx, sz, &ey2) == 0)
                        sy = ey2;
                    else if (port_stan_snap_walkable(&sx, &sz, lx2, lz2,
                                                     PORT_STAN_NEAR_XZ, &ey2) == 0)
                        sy = ey2;
                    port_player_set_pose_at(s, sx, sy, sz, ths);
                }
            }
        }
    }
    /* Tiles/origin are live. Sit the walk test mover on a ground-floor
     * tile around the spawn corner, then re-register stan cylinders. */
    port_prop_place_walker_near_spawn();
    port_prop_choose_pickup();
    port_stan_clear_guards();
    {
        int i, ng = port_prop_guard_count();
        for (i = 0; i < ng; i++) {
            float gx, gz;
            if (port_prop_guard_xz(i, &gx, &gz) == 0)
                port_stan_add_guard(gx, gz);
        }
    }
    bind_path_openings();
    return PORT_STAGE_OK;
}

int port_stage_level_id(void) { return g_level; }

int port_stage_room_count(void) { return g_rooms; }

int port_stage_bg_rooms(void) { return g_bg_rooms; }

int port_stage_room1(float pos[3])
{
    if (g_bg_rooms < 1)
        return -1;
    if (pos) {
        pos[0] = g_rm[1].pos[0] * g_bg_inv;
        pos[1] = g_rm[1].pos[1] * g_bg_inv;
        pos[2] = g_rm[1].pos[2] * g_bg_inv;
    }
    return 0;
}

float port_stage_bg_inv(void) { return g_bg_inv; }

int port_stage_gdl_raw(void) { return g_gdl_raw; }

int port_stage_gdl_c0(void) { return g_gdl_c0; }

int port_stage_gdl_vtx(void) { return g_gdl_vtx; }

int port_stage_gdl_sec(void) { return g_gdl_sec; }

int port_stage_portal_count(void) { return g_nportals; }

void port_stage_dump_portals(void)
{
    int i;
    float r1[3];
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    printf("portal_dump n=%d openings=%d\n", g_nportals, port_stage_opening_count());
    for (i = 0; i < g_nportals; i++) {
        PortPortal *po = &g_portals[i];
        printf("portal r%d-r%d doorlike=%d w=%.1f horiz=%.1f tall=%.1f thin=%.1f "
               "yaw=%.1f world=%.1f,%.1f,%.1f local=%.1f,%.1f\n",
               (int)po->a, (int)po->b, (int)po->doorlike, (double)po->width,
               (double)po->horiz, (double)po->tall, (double)po->thin,
               (double)po->yaw, (double)po->pos[0], (double)po->pos[1],
               (double)po->pos[2], (double)(po->pos[0] - r1[0]),
               (double)(po->pos[2] - r1[2]));
    }
}

int port_stage_path_opening(int ra, int rb)
{
    /* Door-sized Rare quads only. Dump first; do not invent slabs.
     * Added nearby ground r2-r3 / r3-r5 / r5-r4 / r10-r11 / r21-r22
     * and ground doorlike r72-r3 / r73-r11 (y=-513, not stacked).
     * Skip r15-r12 (tall=0, not doorlike). Skip stacked same-xz r8-r6
     * (over r8-r7, y=-128), r8-r9, r6-r71.
     * Unbound: r6 island, gas-plant r23+/r49+, farther r74-r77 ring. */
    return (ra == 71 && rb == 7) || (ra == 7 && rb == 71) ||
           (ra == 7 && rb == 8) || (ra == 8 && rb == 7) ||
           (ra == 8 && rb == 20) || (ra == 20 && rb == 8) ||
           (ra == 20 && rb == 19) || (ra == 19 && rb == 20) ||
           (ra == 19 && rb == 18) || (ra == 18 && rb == 19) ||
           (ra == 3 && rb == 18) || (ra == 18 && rb == 3) ||
           (ra == 19 && rb == 21) || (ra == 21 && rb == 19) ||
           (ra == 1 && rb == 3) || (ra == 3 && rb == 1) ||
           (ra == 11 && rb == 71) || (ra == 71 && rb == 11) ||
           (ra == 8 && rb == 5) || (ra == 5 && rb == 8) ||
           (ra == 8 && rb == 10) || (ra == 10 && rb == 8) ||
           (ra == 13 && rb == 15) || (ra == 15 && rb == 13) ||
           (ra == 14 && rb == 13) || (ra == 13 && rb == 14) ||
           (ra == 14 && rb == 15) || (ra == 15 && rb == 14) ||
           (ra == 2 && rb == 3) || (ra == 3 && rb == 2) ||
           (ra == 3 && rb == 5) || (ra == 5 && rb == 3) ||
           (ra == 5 && rb == 4) || (ra == 4 && rb == 5) ||
           (ra == 10 && rb == 11) || (ra == 11 && rb == 10) ||
           (ra == 21 && rb == 22) || (ra == 22 && rb == 21) ||
           (ra == 72 && rb == 3) || (ra == 3 && rb == 72) ||
           (ra == 73 && rb == 11) || (ra == 11 && rb == 73);
}

static void scale_portal_geom(float inv)
{
    int i;
    if (g_portals_scaled || !(inv > 0.f) || inv == 1.f)
        return;
    for (i = 0; i < g_nportals; i++) {
        g_portals[i].pos[0] *= inv;
        g_portals[i].pos[1] *= inv;
        g_portals[i].pos[2] *= inv;
        g_portals[i].width *= inv;
        g_portals[i].horiz *= inv;
        g_portals[i].tall *= inv;
        g_portals[i].thin *= inv;
    }
    g_portals_scaled = 1;
}

static void bind_path_openings(void)
{
    int i;
    for (i = 0; i < g_nportals; i++) {
        float lx, lz;
        if (!g_portals[i].doorlike)
            continue;
        if (!port_stage_path_opening((int)g_portals[i].a, (int)g_portals[i].b))
            continue;
        /* Stan origin is room1, so use_door maps player local -> world.
         * Bind fitted portal world xz, not gas-plant GROUP / pad origins.
         * Portal geom is already *inv (same world as G1 / stan / pads). */
        if (g_portals[i].yaw == 90.f) {
            lx = 1.f;
            lz = 0.f;
        } else {
            lx = 0.f;
            lz = -1.f;
        }
        port_stan_add_door_w(g_portals[i].pos[0], g_portals[i].pos[2], lx, lz,
                            g_portals[i].width);
    }
}

int port_stage_opening_count(void)
{
    int i, n = 0;
    for (i = 0; i < g_nportals; i++) {
        if (g_portals[i].doorlike)
            n++;
    }
    return n;
}

static PortPortal *opening_at(int want)
{
    int i, n = 0;
    if (want < 0)
        return NULL;
    for (i = 0; i < g_nportals; i++) {
        if (!g_portals[i].doorlike)
            continue;
        if (n == want)
            return &g_portals[i];
        n++;
    }
    return NULL;
}

int port_stage_opening(int want, float pos[3], float *yaw, float *width, int *ra, int *rb)
{
    PortPortal *po = opening_at(want);
    if (!po)
        return -1;
    if (pos) {
        pos[0] = po->pos[0];
        pos[1] = po->pos[1];
        pos[2] = po->pos[2];
    }
    if (yaw)
        *yaw = po->yaw;
    if (width)
        *width = po->width;
    if (ra)
        *ra = po->a;
    if (rb)
        *rb = po->b;
    return 0;
}

float port_stage_opening_height(int want)
{
    PortPortal *po = opening_at(want);
    return po ? po->tall : 0.f;
}

int port_stage_current_room(void) { return g_cur_room; }

int port_stage_rooms_walked(void) { return g_rooms_walked; }

int port_stage_walked_room(int i)
{
    if (i < 0 || i >= g_rooms_walked)
        return 0;
    return (int)g_walked[i];
}

int port_stage_walked_has(int room)
{
    int i;
    if (room < 1)
        return 0;
    for (i = 0; i < g_rooms_walked; i++) {
        if ((int)g_walked[i] == room)
            return 1;
    }
    return 0;
}

int port_stage_prop_count(void) { return port_prop_count(); }

int port_stage_prop_models(void) { return port_prop_models(); }

int port_stage_props_drawn(void) { return port_prop_drawn(); }

int port_stage_intro_pad(void) { return port_prop_intro_pad(); }

int port_stage_guard_count(void) { return port_prop_guard_count(); }

static int room_nearest_world(float px, float py, float pz)
{
    float best, d;
    int i, cur;

    if (g_bg_rooms < 1)
        return 0;
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

int port_stage_room_at_local(float lx, float ly, float lz)
{
    int tile_rm;
    if (g_bg_rooms < 1)
        return 0;
    /* Same pin as the camera: lowest-floor stan tile, then a nearby
     * ground tile. Nearest bg-room centre snaps the bathroom hall
     * onto 12/14 so hear/fire treat a ground actor as upstairs. */
    tile_rm = port_stan_tile_room(lx, lz);
    if (tile_rm >= 1 && tile_rm <= g_bg_rooms)
        return tile_rm;
    tile_rm = port_stan_nearest_tile_room(lx, lz, PORT_STAN_NEAR_XZ);
    if (tile_rm >= 1 && tile_rm <= g_bg_rooms)
        return tile_rm;
    return room_nearest_world(lx + g_rm[1].pos[0] * g_bg_inv,
                              ly + g_rm[1].pos[1] * g_bg_inv,
                              lz + g_rm[1].pos[2] * g_bg_inv);
}

int port_stage_rooms_adjacent(int a, int b)
{
    int i;
    if (a < 1 || b < 1 || a == b)
        return 0;
    for (i = 0; i < g_nportals; i++) {
        if ((g_portals[i].a == a && g_portals[i].b == b) ||
            (g_portals[i].a == b && g_portals[i].b == a))
            return 1;
    }
    return 0;
}

/* Rare doorActivatePortal clears PORTALFLAG_DISABLED while opening.
 * Bound path/pad slabs at the portal centre: closed (frac=0) seals vis.
 * No slab (open archway / stair) does not invent a seal. */
static int portal_vis_closed(const PortPortal *po)
{
    if (!po->doorlike)
        return 0;
    return port_stan_closed_door_at_world(po->pos[0], po->pos[2]);
}

static int pick_current_room(void)
{
    int rm;
    if (g_bg_rooms < 1)
        return 0;
    /* Eye-matched tile: stacked catwalk xz must draw r13/r14/r15, not the
     * ground hall under the same xz. Hear/fire keep room_at_local
     * (lowest floor) so bathroom hall cannot snap to 12/14. */
    rm = port_stan_tile_room_at_eye(port_player_x(), port_player_z(),
                                    port_player_y());
    if (rm >= 1 && rm <= g_bg_rooms)
        return rm;
    rm = port_stage_room_at_local(port_player_x(), port_player_y(),
                                    port_player_z());
    if (rm >= 1 && rm <= g_bg_rooms)
        return rm;
    /* Stay on the last drawable room rather than clearing the FB. */
    if (g_last_good_room >= 1 && g_last_good_room <= g_bg_rooms)
        return g_last_good_room;
    return 0;
}

static int select_rooms(uint8_t *out, int cap)
{
    uint8_t seen[PORT_MAX_BG_ROOMS];
    uint8_t q[PORT_MAX_BG_ROOMS];
    uint8_t depth[PORT_MAX_BG_ROOMS];
    int qh = 0, qt = 0, n = 0, i, pass, maxd, ground_extra;
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
        /* r13/r14/r15 need extra depth so a neighbor GDL (r12/r14) and its
         * rooms stay in frame. r12 landing looks down the start stairs
         * into r71 (portal r6-r71 at d4); depth 3 left r71 undrawn and
         * the stair well was a black slab. Spawn r71 / r7 / r8 need
         * extra ground depth so r19 (d4) and r18 (d5) walk; r12 stays
         * off those extra hops so the stall frame does not pick up the
         * catwalk. */
        maxd = PORT_WALK_DEPTH;
        ground_extra = 0;
        if (cur == 12 || cur == 13 || cur == 14 || cur == 15)
            maxd = 5;
        else if (cur == 71 || cur == 7 || cur == 8) {
            maxd = 5;
            ground_extra = 1;
        }
        if (d >= maxd)
            continue;
        /* Same-floor portals first so a 24-room cap cannot fill with
         * downstairs halls when the camera is on r13/r15. */
        for (pass = 0; pass < 2; pass++) {
            for (i = 0; i < g_nportals; i++) {
                int o = 0;
                float dy;
                int same;
                if (g_portals[i].a == r)
                    o = g_portals[i].b;
                else if (g_portals[i].b == r)
                    o = g_portals[i].a;
                else
                    continue;
                if (o < 1 || o > g_bg_rooms || seen[o])
                    continue;
                if (portal_vis_closed(&g_portals[i]))
                    continue;
                dy = g_rm[o].pos[1] - g_rm[r].pos[1];
                if (dy < 0.f)
                    dy = -dy;
                same = dy <= 400.f;
                if (pass == 0 && !same)
                    continue;
                if (pass == 1 && same)
                    continue;
                /* Extra hops from spawn/r7/r8 stay on the lab floor
                 * (pos.y < 0). r9/r6 already walked at depth 3; do not
                 * spend d4/d5 on r12/r15. */
                if (ground_extra && d >= PORT_WALK_DEPTH && g_rm[o].pos[1] >= 0.f)
                    continue;
                seen[o] = 1;
                if (qt >= PORT_MAX_BG_ROOMS)
                    break;
                q[qt] = (uint8_t)o;
                depth[qt] = (uint8_t)(d + 1);
                qt++;
            }
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

int port_stage_room_gdl(int room, uint32_t *ngfx, float pos[3])
{
    const PortBgRoom *rm;
    if (room < 1 || room > g_bg_rooms)
        return -1;
    rm = &g_rm[room];
    if (pos) {
        pos[0] = rm->pos[0];
        pos[1] = rm->pos[1];
        pos[2] = rm->pos[2];
    }
    if (ngfx)
        *ngfx = rm->pri_ngfx;
    if (!room_pri(rm) || rm->pri_ngfx == 0)
        return -1;
    return 0;
}

/* Rare bgroomtrans: room_data_float2 = 1/levelscale. Facility 1/1.20648.
 * world = (vtx + room.pos) * float2. Port currently draws vtx + (pos-r1)
 * unscaled, so G1 walls can sit inside scaled stan tiles. */
#define PORT_G1_VTX_CACHE 16
#define PORT_LEVEL_SC_FACILITY 1.20648f

static int16_t dump_be16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void dump_one_room_walls(int room, float lx, float ly, float lz, float inv)
{
    const PortBgRoom *rm;
    const uint8_t *dl, *vtxbase;
    uint32_t ncmd, i;
    float ox, oy, oz, r1x, r1y, r1z;
    float slot[PORT_G1_VTX_CACHE][3];
    int have[PORT_G1_VTX_CACHE];
    float amin[3], amax[3], pmin[3], pmax[3], rmin[3], rmax[3];
    int nv = 0, ntri = 0, nwall = 0;
    float best_port = 1.0e30f, best_rare = 1.0e30f;
    float best_px = 0.f, best_pz = 0.f, best_rx = 0.f, best_rz = 0.f;

    if (room < 1 || room > g_bg_rooms)
        return;
    rm = &g_rm[room];
    dl = room_pri(rm);
    if (!dl || rm->pri_ngfx == 0 || !rm->vtx || rm->vtx_len < 16)
        return;
    vtxbase = rm->vtx;
    r1x = g_rm[1].pos[0];
    r1y = g_rm[1].pos[1];
    r1z = g_rm[1].pos[2];
    ox = rm->pos[0] - r1x;
    oy = rm->pos[1] - r1y;
    oz = rm->pos[2] - r1z;
    memset(have, 0, sizeof have);
    amin[0] = amin[1] = amin[2] = 1.0e30f;
    amax[0] = amax[1] = amax[2] = -1.0e30f;
    pmin[0] = pmin[1] = pmin[2] = 1.0e30f;
    pmax[0] = pmax[1] = pmax[2] = -1.0e30f;
    rmin[0] = rmin[1] = rmin[2] = 1.0e30f;
    rmax[0] = rmax[1] = rmax[2] = -1.0e30f;
    ncmd = rm->pri_ngfx;
    for (i = 0; i < ncmd; i++) {
        const uint8_t *p = dl + i * 8u;
        uint8_t cmd = p[0];
        uint32_t w0 = be32(p);
        uint32_t w1 = be32(p + 4);
        if (cmd == 0x04) {
            uint32_t param = (w0 >> 16) & 0xFFu;
            uint32_t n = (param >> 4) + 1u;
            uint32_t v0 = param & 0xFu;
            uint32_t seg = w1 >> 24;
            uint32_t off = w1 & 0x00FFFFFFu;
            uint32_t k;
            const uint8_t *src;
            if (seg != 14 || off + n * 16u > rm->vtx_len)
                continue;
            src = vtxbase + off;
            for (k = 0; k < n && v0 + k < PORT_G1_VTX_CACHE; k++) {
                const uint8_t *s = src + k * 16u;
                float x = (float)dump_be16(s);
                float y = (float)dump_be16(s + 2);
                float z = (float)dump_be16(s + 4);
                float px = x + ox, py = y + oy, pz = z + oz;
                float rx = (x + rm->pos[0]) * inv;
                float ry = (y + rm->pos[1]) * inv;
                float rz = (z + rm->pos[2]) * inv;
                slot[v0 + k][0] = x;
                slot[v0 + k][1] = y;
                slot[v0 + k][2] = z;
                have[v0 + k] = 1;
                nv++;
                if (x < amin[0])
                    amin[0] = x;
                if (y < amin[1])
                    amin[1] = y;
                if (z < amin[2])
                    amin[2] = z;
                if (x > amax[0])
                    amax[0] = x;
                if (y > amax[1])
                    amax[1] = y;
                if (z > amax[2])
                    amax[2] = z;
                if (px < pmin[0])
                    pmin[0] = px;
                if (py < pmin[1])
                    pmin[1] = py;
                if (pz < pmin[2])
                    pmin[2] = pz;
                if (px > pmax[0])
                    pmax[0] = px;
                if (py > pmax[1])
                    pmax[1] = py;
                if (pz > pmax[2])
                    pmax[2] = pz;
                if (rx < rmin[0])
                    rmin[0] = rx;
                if (ry < rmin[1])
                    rmin[1] = ry;
                if (rz < rmin[2])
                    rmin[2] = rz;
                if (rx > rmax[0])
                    rmax[0] = rx;
                if (ry > rmax[1])
                    rmax[1] = ry;
                if (rz > rmax[2])
                    rmax[2] = rz;
            }
            continue;
        }
        if (cmd != 0xB1)
            continue;
        {
            uint32_t tris[4][3];
            int t;
            tris[0][0] = w1 & 0xF;
            tris[0][1] = (w1 >> 4) & 0xF;
            tris[0][2] = w0 & 0xF;
            tris[1][0] = (w1 >> 8) & 0xF;
            tris[1][1] = (w1 >> 12) & 0xF;
            tris[1][2] = (w0 >> 4) & 0xF;
            tris[2][0] = (w1 >> 16) & 0xF;
            tris[2][1] = (w1 >> 20) & 0xF;
            tris[2][2] = (w0 >> 8) & 0xF;
            tris[3][0] = (w1 >> 24) & 0xF;
            tris[3][1] = (w1 >> 28) & 0xF;
            tris[3][2] = (w0 >> 12) & 0xF;
            for (t = 0; t < 4; t++) {
                float x0, y0, z0, x1, y1, z1, x2, y2, z2;
                float e1x, e1y, e1z, e2x, e2y, e2z, nx, ny, nz, nlen;
                float cx, cz, d, yspan;
                int a, b, c;
                a = (int)tris[t][0];
                b = (int)tris[t][1];
                c = (int)tris[t][2];
                if (a == 0 && b == 0 && c == 0)
                    continue;
                if (a >= PORT_G1_VTX_CACHE || b >= PORT_G1_VTX_CACHE ||
                    c >= PORT_G1_VTX_CACHE)
                    continue;
                if (!have[a] || !have[b] || !have[c])
                    continue;
                ntri++;
                x0 = slot[a][0];
                y0 = slot[a][1];
                z0 = slot[a][2];
                x1 = slot[b][0];
                y1 = slot[b][1];
                z1 = slot[b][2];
                x2 = slot[c][0];
                y2 = slot[c][1];
                z2 = slot[c][2];
                e1x = x1 - x0;
                e1y = y1 - y0;
                e1z = z1 - z0;
                e2x = x2 - x0;
                e2y = y2 - y0;
                e2z = z2 - z0;
                nx = e1y * e2z - e1z * e2y;
                ny = e1z * e2x - e1x * e2z;
                nz = e1x * e2y - e1y * e2x;
                nlen = sqrtf(nx * nx + ny * ny + nz * nz);
                if (nlen < 1.f)
                    continue;
                /* Vertical-ish wall: |ny| small vs xz. */
                if (fabsf(ny) > 0.35f * nlen)
                    continue;
                yspan = y0;
                if (y1 > yspan)
                    yspan = y1;
                if (y2 > yspan)
                    yspan = y2;
                if (yspan < 20.f)
                    continue;
                nwall++;
                cx = (x0 + x1 + x2) / 3.f + ox;
                cz = (z0 + z1 + z2) / 3.f + oz;
                d = (cx - lx) * (cx - lx) + (cz - lz) * (cz - lz);
                if (d < best_port) {
                    best_port = d;
                    best_px = cx;
                    best_pz = cz;
                }
                cx = (x0 + x1 + x2) / 3.f;
                cz = (z0 + z1 + z2) / 3.f;
                cx = (cx + rm->pos[0]) * inv;
                cz = (cz + rm->pos[2]) * inv;
                d = (cx - lx) * (cx - lx) + (cz - lz) * (cz - lz);
                if (d < best_rare) {
                    best_rare = d;
                    best_rx = cx;
                    best_rz = cz;
                }
                (void)ly;
                (void)oy;
            }
        }
    }
    printf("g1wall r%d pos=%.1f,%.1f,%.1f ox=%.1f,%.1f,%.1f nv=%d ntri=%d nwall=%d "
           "rawAABB x=%.1f..%.1f y=%.1f..%.1f z=%.1f..%.1f\n",
           room, (double)rm->pos[0], (double)rm->pos[1], (double)rm->pos[2],
           (double)ox, (double)oy, (double)oz, nv, ntri, nwall,
           (double)amin[0], (double)amax[0], (double)amin[1], (double)amax[1],
           (double)amin[2], (double)amax[2]);
    printf("g1wall r%d portAABB x=%.1f..%.1f y=%.1f..%.1f z=%.1f..%.1f "
           "rareAABB x=%.1f..%.1f y=%.1f..%.1f z=%.1f..%.1f\n",
           room, (double)pmin[0], (double)pmax[0], (double)pmin[1], (double)pmax[1],
           (double)pmin[2], (double)pmax[2], (double)rmin[0], (double)rmax[0],
           (double)rmin[1], (double)rmax[1], (double)rmin[2], (double)rmax[2]);
    if (best_port < 1.0e29f)
        printf("g1wall r%d closest_port xz=%.1f,%.1f d=%.1f  closest_rare xz=%.1f,%.1f d=%.1f "
               "cam=%.1f,%.1f\n",
               room, (double)best_px, (double)best_pz, (double)sqrtf(best_port),
               (double)best_rx, (double)best_rz, (double)sqrtf(best_rare),
               (double)lx, (double)lz);
}

void port_stage_dump_walls_at(float lx, float ly, float lz)
{
    float inv = 1.0f / PORT_LEVEL_SC_FACILITY;
    int ids[8];
    int n = 0, i, r, cur;

    if (g_bg_rooms < 1)
        return;
    printf("g1wall cam=%.1f,%.1f,%.1f r1=%.1f,%.1f,%.1f inv=%.6f rooms=%d\n",
           (double)lx, (double)ly, (double)lz, (double)g_rm[1].pos[0],
           (double)g_rm[1].pos[1], (double)g_rm[1].pos[2], (double)inv, g_bg_rooms);
    cur = port_stage_current_room();
    if (cur < 1)
        cur = port_stan_tile_room(lx, lz);
    ids[n++] = cur;
    if (cur != 71 && n < 8)
        ids[n++] = 71;
    if (cur != 1 && n < 8)
        ids[n++] = 1;
    for (i = 0; i < g_nportals && n < 8; i++) {
        int o = 0;
        if (g_portals[i].a == cur)
            o = g_portals[i].b;
        else if (g_portals[i].b == cur)
            o = g_portals[i].a;
        else
            continue;
        for (r = 0; r < n; r++) {
            if (ids[r] == o)
                break;
        }
        if (r == n)
            ids[n++] = o;
    }
    for (i = 0; i < n; i++)
        dump_one_room_walls(ids[i], lx, ly, lz, inv);
}

#define PORT_G1_LEAF_SKIN 8.0f
#define PORT_G1_LEAF_PUSH_CAP 180.0f
#define PORT_G1_LEAF_PROBE 55.0f
#define PORT_G1_LEAF_DUMP 8

typedef struct {
    float px, pz;
    float nx, nz;
    float tx, tz;
    float half_w;
    float thick;
    float pad_along;
    float amin, amax;
    float need;
    float dcam;
    int both;
    int straddle;
    int ray;
} G1LeafHit;

#define PORT_G1_PLANE_MAX 384
#define PORT_G1_PLANE_PER 32

typedef struct {
    float pcx, pcz;
    float wx, wz;
    float tx, tz;
    float half_w;
    float thick;
} G1LeafPlane;

static G1LeafPlane g_planes[PORT_G1_PLANE_MAX];
static int g_nplanes;
static uint16_t g_rm_p0[PORT_MAX_BG_ROOMS];
static uint8_t g_rm_pn[PORT_MAX_BG_ROOMS];
static uint8_t g_rm_ph[PORT_MAX_BG_ROOMS];

/* Interior G1 walls (half_w up to 800). Separate from door-leaf planes so
 * guard viscyl push does not start sliding bodies along room partitions. */
#define PORT_G1_WALL_MAX 512
#define PORT_G1_WALL_PER 48
static G1LeafPlane g_walls[PORT_G1_WALL_MAX];
static int g_nwalls;
static uint16_t g_rm_w0[PORT_MAX_BG_ROOMS];
static uint8_t g_rm_wn[PORT_MAX_BG_ROOMS];
static uint8_t g_rm_wh[PORT_MAX_BG_ROOMS];

static void g1_clear_planes(void)
{
    g_nplanes = 0;
    g_nwalls = 0;
    memset(g_rm_p0, 0, sizeof g_rm_p0);
    memset(g_rm_pn, 0, sizeof g_rm_pn);
    memset(g_rm_ph, 0, sizeof g_rm_ph);
    memset(g_rm_w0, 0, sizeof g_rm_w0);
    memset(g_rm_wn, 0, sizeof g_rm_wn);
    memset(g_rm_wh, 0, sizeof g_rm_wh);
}

static int g1_add_room(int *rooms, int n, int max, int r)
{
    int j;
    if (r < 1)
        return n;
    for (j = 0; j < n; j++) {
        if (rooms[j] == r)
            return n;
    }
    if (n < max)
        rooms[n++] = r;
    return n;
}

static int g1_chr_rooms(float pad_lx, float pad_lz, float cam_lx, float cam_lz, int *rooms,
                        int max)
{
    int n = 0, i, r;
    n = g1_add_room(rooms, n, max, port_stan_tile_room(pad_lx, pad_lz));
    n = g1_add_room(rooms, n, max, port_stan_tile_room(cam_lx, cam_lz));
    n = g1_add_room(rooms, n, max, g_cur_room);
    n = g1_add_room(rooms, n, max, port_stage_current_room());
    for (i = 0; i < g_rooms_walked; i++)
        n = g1_add_room(rooms, n, max, g_walked[i]);
    for (i = 0; i < g_nportals && n < max; i++) {
        int a = g_portals[i].a, b = g_portals[i].b, j, keep = 0;
        for (j = 0; j < n; j++) {
            if (rooms[j] == a || rooms[j] == b) {
                keep = 1;
                break;
            }
        }
        if (!keep)
            continue;
        n = g1_add_room(rooms, n, max, a);
        n = g1_add_room(rooms, n, max, b);
    }
    (void)cam_lx;
    (void)cam_lz;
    return n;
}

static int g1_plane_from_tri_hw(float x0, float y0, float z0, float x1, float y1, float z1,
                                float x2, float y2, float z2, float sc, float ox, float oz,
                                float max_hw, G1LeafPlane *out)
{
    float e1x, e1y, e1z, e2x, e2y, e2z, nx, ny, nz, nlen, wlen, wx, wz;
    float pcx, pcz, ymin, ymax, yspan;
    float vx[3], vz[3], along[3], thick, half_w, tx, tz;
    int i;

    e1x = x1 - x0;
    e1y = y1 - y0;
    e1z = z1 - z0;
    e2x = x2 - x0;
    e2y = y2 - y0;
    e2z = z2 - z0;
    nx = e1y * e2z - e1z * e2y;
    ny = e1z * e2x - e1x * e2z;
    nz = e1x * e2y - e1y * e2x;
    nlen = sqrtf(nx * nx + ny * ny + nz * nz);
    if (nlen < 1.f)
        return 0;
    if (fabsf(ny) > 0.35f * nlen)
        return 0;
    ymin = y0;
    ymax = y0;
    if (y1 < ymin)
        ymin = y1;
    if (y2 < ymin)
        ymin = y2;
    if (y1 > ymax)
        ymax = y1;
    if (y2 > ymax)
        ymax = y2;
    yspan = (ymax - ymin) * sc;
    if (yspan < 80.f || ymax * sc < 40.f)
        return 0;
    wx = nx;
    wz = nz;
    wlen = sqrtf(wx * wx + wz * wz);
    if (wlen < 1e-3f)
        return 0;
    wx /= wlen;
    wz /= wlen;
    tx = -wz;
    tz = wx;
    vx[0] = x0 * sc + ox;
    vz[0] = z0 * sc + oz;
    vx[1] = x1 * sc + ox;
    vz[1] = z1 * sc + oz;
    vx[2] = x2 * sc + ox;
    vz[2] = z2 * sc + oz;
    pcx = (vx[0] + vx[1] + vx[2]) / 3.f;
    pcz = (vz[0] + vz[1] + vz[2]) / 3.f;
    thick = 0.f;
    half_w = 0.f;
    for (i = 0; i < 3; i++) {
        along[i] = (vx[i] - pcx) * wx + (vz[i] - pcz) * wz;
        if (along[i] < 0.f) {
            if (-along[i] > thick)
                thick = -along[i];
        } else if (along[i] > thick)
            thick = along[i];
        {
            float ta = (vx[i] - pcx) * tx + (vz[i] - pcz) * tz;
            if (ta < 0.f)
                ta = -ta;
            if (ta > half_w)
                half_w = ta;
        }
    }
    if (thick > 50.f)
        return 0;
    /* Door leaf ~90 half-w; room partitions are wider. Clip-door guard 36
     * sat behind a ~159 half-w wall. Extra idle cam-pad is hall-parallel.
     * Player G1 clip passes 800 so a bathroom partition is not dropped. */
    if (half_w < 20.f || half_w > max_hw)
        return 0;
    out->pcx = pcx;
    out->pcz = pcz;
    out->wx = wx;
    out->wz = wz;
    out->tx = tx;
    out->tz = tz;
    out->half_w = half_w;
    out->thick = thick;
    return 1;
}

static int g1_plane_from_tri(float x0, float y0, float z0, float x1, float y1, float z1,
                             float x2, float y2, float z2, float sc, float ox, float oz,
                             G1LeafPlane *out)
{
    return g1_plane_from_tri_hw(x0, y0, z0, x1, y1, z1, x2, y2, z2, sc, ox, oz, 220.f,
                                out);
}

static int g1_hit_from_plane(const G1LeafPlane *pl, float cam_lx, float cam_lz,
                             float pad_lx, float pad_lz, float ax0, float az0, float ax1,
                             float az1, G1LeafHit *out)
{
    float pcx = pl->pcx, pcz = pl->pcz, wx = pl->wx, wz = pl->wz;
    float tx = pl->tx, tz = pl->tz, half_w = pl->half_w, thick = pl->thick;
    float pad_along, amin, amax, tmin, tmax, side, min_pad, need;
    float rdx, rdz, denom, t, hx, hz, across;
    float pax, paz, fpx, fpz;
    int i, both, straddle, ray;

    pad_along = (pad_lx - pcx) * wx + (pad_lz - pcz) * wz;
    pax = pcx + wx * ((pad_along >= 0.f) ? PORT_G1_LEAF_PROBE : -PORT_G1_LEAF_PROBE);
    paz = pcz + wz * ((pad_along >= 0.f) ? PORT_G1_LEAF_PROBE : -PORT_G1_LEAF_PROBE);
    fpx = pcx - wx * ((pad_along >= 0.f) ? PORT_G1_LEAF_PROBE : -PORT_G1_LEAF_PROBE);
    fpz = pcz - wz * ((pad_along >= 0.f) ? PORT_G1_LEAF_PROBE : -PORT_G1_LEAF_PROBE);
    both = port_stan_on_tile(pax, paz) && port_stan_on_tile(fpx, fpz);
    {
        float cx[4], cz[4];
        cx[0] = ax0;
        cz[0] = az0;
        cx[1] = ax0;
        cz[1] = az1;
        cx[2] = ax1;
        cz[2] = az0;
        cx[3] = ax1;
        cz[3] = az1;
        amin = amax = (cx[0] - pcx) * wx + (cz[0] - pcz) * wz;
        tmin = tmax = (cx[0] - pcx) * tx + (cz[0] - pcz) * tz;
        for (i = 1; i < 4; i++) {
            float a = (cx[i] - pcx) * wx + (cz[i] - pcz) * wz;
            float ta = (cx[i] - pcx) * tx + (cz[i] - pcz) * tz;
            if (a < amin)
                amin = a;
            if (a > amax)
                amax = a;
            if (ta < tmin)
                tmin = ta;
            if (ta > tmax)
                tmax = ta;
        }
    }
    straddle = (amin < -1.f && amax > 1.f);
    side = (pad_along >= 0.f) ? 1.f : -1.f;
    min_pad = (side > 0.f) ? amin : -amax;
    need = 0.f;
    /* Push only the leaf the camera-to-pad ray hits, onto the pad side.
     * Other parallel walls (the far side of a 126u skip=pose AABB in a
     * 106u gap) must not pull the body back through the door. */
    ray = 0;
    rdx = pad_lx - cam_lx;
    rdz = pad_lz - cam_lz;
    denom = rdx * wx + rdz * wz;
    if (fabsf(denom) > 1e-4f) {
        t = ((pcx - cam_lx) * wx + (pcz - cam_lz) * wz) / denom;
        if (t > 0.02f && t < 0.98f) {
            hx = cam_lx + t * rdx;
            hz = cam_lz + t * rdz;
            across = (hx - pcx) * tx + (hz - pcz) * tz;
            if (across < 0.f)
                across = -across;
            if (across <= half_w + 10.f) {
                ray = 1;
                if (straddle || min_pad < PORT_G1_LEAF_SKIN) {
                    need = PORT_G1_LEAF_SKIN - min_pad;
                    if (need < 0.f)
                        need = 0.f;
                }
            }
        }
    }
    if (need <= 0.f && !ray && !(both && straddle))
        return 0;
    out->px = pcx;
    out->pz = pcz;
    out->nx = wx * side;
    out->nz = wz * side;
    out->tx = tx;
    out->tz = tz;
    out->half_w = half_w;
    out->thick = thick;
    out->pad_along = pad_along;
    out->amin = amin;
    out->amax = amax;
    out->need = need;
    out->dcam = sqrtf((cam_lx - pcx) * (cam_lx - pcx) + (cam_lz - pcz) * (cam_lz - pcz));
    out->both = both;
    out->straddle = straddle;
    out->ray = ray;
    return 1;
}

static void g1_collect_room_planes(int room)
{
    const PortBgRoom *rm;
    const uint8_t *dl, *vtxbase;
    uint32_t ncmd, i;
    float ox, oz, sc, r1x, r1z;
    float slot[PORT_G1_VTX_CACHE][3];
    int have[PORT_G1_VTX_CACHE];

    if (room < 1 || room > g_bg_rooms || room >= PORT_MAX_BG_ROOMS)
        return;
    if (g_rm_ph[room])
        return;
    g_rm_ph[room] = 1;
    g_rm_p0[room] = (uint16_t)g_nplanes;
    g_rm_pn[room] = 0;
    rm = &g_rm[room];
    dl = room_pri(rm);
    if (!dl || rm->pri_ngfx == 0 || !rm->vtx)
        return;
    vtxbase = rm->vtx;
    sc = (g_bg_inv != 0.f) ? g_bg_inv : 1.f;
    r1x = g_rm[1].pos[0] * sc;
    r1z = g_rm[1].pos[2] * sc;
    ox = rm->pos[0] * sc - r1x;
    oz = rm->pos[2] * sc - r1z;
    memset(have, 0, sizeof have);
    ncmd = rm->pri_ngfx;
    for (i = 0; i < ncmd; i++) {
        const uint8_t *p = dl + i * 8u;
        uint8_t cmd = p[0];
        uint32_t w0 = be32(p);
        uint32_t w1 = be32(p + 4);
        if (cmd == 0x04) {
            uint32_t param = (w0 >> 16) & 0xFFu;
            uint32_t n = (param >> 4) + 1u;
            uint32_t v0 = param & 0xFu;
            uint32_t seg = w1 >> 24;
            uint32_t off = w1 & 0x00FFFFFFu;
            uint32_t k;
            const uint8_t *src;
            if (seg != 14 || off + n * 16u > rm->vtx_len)
                continue;
            src = vtxbase + off;
            for (k = 0; k < n && v0 + k < PORT_G1_VTX_CACHE; k++) {
                const uint8_t *s = src + k * 16u;
                slot[v0 + k][0] = (float)dump_be16(s);
                slot[v0 + k][1] = (float)dump_be16(s + 2);
                slot[v0 + k][2] = (float)dump_be16(s + 4);
                have[v0 + k] = 1;
            }
            continue;
        }
        if (cmd != 0xB1)
            continue;
        {
            uint32_t tris[4][3];
            int t;
            tris[0][0] = w1 & 0xF;
            tris[0][1] = (w1 >> 4) & 0xF;
            tris[0][2] = w0 & 0xF;
            tris[1][0] = (w1 >> 8) & 0xF;
            tris[1][1] = (w1 >> 12) & 0xF;
            tris[1][2] = (w0 >> 4) & 0xF;
            tris[2][0] = (w1 >> 16) & 0xF;
            tris[2][1] = (w1 >> 20) & 0xF;
            tris[2][2] = (w0 >> 8) & 0xF;
            tris[3][0] = (w1 >> 24) & 0xF;
            tris[3][1] = (w1 >> 28) & 0xF;
            tris[3][2] = (w0 >> 12) & 0xF;
            for (t = 0; t < 4; t++) {
                G1LeafPlane pl;
                float x0, y0, z0, x1, y1, z1, x2, y2, z2;
                int a, b, c;
                a = (int)tris[t][0];
                b = (int)tris[t][1];
                c = (int)tris[t][2];
                if (a == 0 && b == 0 && c == 0)
                    continue;
                if (a >= PORT_G1_VTX_CACHE || b >= PORT_G1_VTX_CACHE ||
                    c >= PORT_G1_VTX_CACHE)
                    continue;
                if (!have[a] || !have[b] || !have[c])
                    continue;
                x0 = slot[a][0];
                y0 = slot[a][1];
                z0 = slot[a][2];
                x1 = slot[b][0];
                y1 = slot[b][1];
                z1 = slot[b][2];
                x2 = slot[c][0];
                y2 = slot[c][1];
                z2 = slot[c][2];
                if (g_nplanes >= PORT_G1_PLANE_MAX || g_rm_pn[room] >= PORT_G1_PLANE_PER)
                    return;
                if (!g1_plane_from_tri(x0, y0, z0, x1, y1, z1, x2, y2, z2, sc, ox, oz,
                                       &pl))
                    continue;
                g_planes[g_nplanes++] = pl;
                g_rm_pn[room]++;
            }
        }
    }
}

static void g1_scan_room_leaves(int room, float cam_lx, float cam_lz, float pad_lx,
                                float pad_lz, float ax0, float az0, float ax1, float az1,
                                G1LeafHit *hits, int *nhits, int max_hits, float *best_need,
                                float *bnx, float *bnz, int *blocks)
{
    int i, p0, pn;

    g1_collect_room_planes(room);
    if (room < 1 || room >= PORT_MAX_BG_ROOMS)
        return;
    p0 = (int)g_rm_p0[room];
    pn = (int)g_rm_pn[room];
    for (i = 0; i < pn; i++) {
        G1LeafHit hit;
        if (!g1_hit_from_plane(&g_planes[p0 + i], cam_lx, cam_lz, pad_lx, pad_lz, ax0, az0,
                               ax1, az1, &hit))
            continue;
        if (hit.ray && blocks)
            *blocks = 1;
        if (hit.need > *best_need) {
            *best_need = hit.need;
            *bnx = hit.nx;
            *bnz = hit.nz;
        }
        if (hits && nhits && *nhits < max_hits) {
            hits[*nhits] = hit;
            (*nhits)++;
        }
    }
}

static void g1_scan_chr_leaves(float cam_lx, float cam_lz, float pad_lx, float pad_lz,
                               float ax0, float az0, float ax1, float az1, G1LeafHit *hits,
                               int *nhits, int max_hits, float *best_need, float *bnx,
                               float *bnz, int *blocks)
{
    int rooms[8], nr, j;
    if (g_bg_rooms < 1)
        return;
    nr = g1_chr_rooms(pad_lx, pad_lz, cam_lx, cam_lz, rooms, 8);
    for (j = 0; j < nr; j++)
        g1_scan_room_leaves(rooms[j], cam_lx, cam_lz, pad_lx, pad_lz, ax0, az0, ax1, az1,
                            hits, nhits, max_hits, best_need, bnx, bnz, blocks);
}

int port_stage_g1_chr_push(float cam_lx, float cam_lz, float pad_lx, float pad_lz, float x0,
                           float z0, float x1, float z1, float *pdx, float *pdz)
{
    float dx = 0.f, dz = 0.f;
    int iter;
    const float cap = PORT_G1_LEAF_PUSH_CAP;

    if (pdx)
        *pdx = 0.f;
    if (pdz)
        *pdz = 0.f;
    if (g_bg_rooms < 1)
        return 0;
    if (x1 < x0) {
        float t = x0;
        x0 = x1;
        x1 = t;
    }
    if (z1 < z0) {
        float t = z0;
        z0 = z1;
        z1 = t;
    }
    for (iter = 0; iter < 4; iter++) {
        float best_need = 0.f, bnx = 0.f, bnz = 0.f;
        int dummy = 0;
        float sx0 = x0 + dx, sz0 = z0 + dz, sx1 = x1 + dx, sz1 = z1 + dz;
        g1_scan_chr_leaves(cam_lx, cam_lz, pad_lx + dx, pad_lz + dz, sx0, sz0, sx1, sz1,
                           NULL, NULL, 0, &best_need, &bnx, &bnz, &dummy);
        if (best_need <= 0.f)
            break;
        if (best_need > cap)
            best_need = cap;
        dx += bnx * best_need;
        dz += bnz * best_need;
        if (dx * dx + dz * dz > cap * cap) {
            float len = sqrtf(dx * dx + dz * dz);
            dx *= cap / len;
            dz *= cap / len;
            break;
        }
    }
    if (dx == 0.f && dz == 0.f)
        return 0;
    if (pdx)
        *pdx = dx;
    if (pdz)
        *pdz = dz;
    return 1;
}

int port_stage_g1_leaf_blocks(float cam_lx, float cam_lz, float pad_lx, float pad_lz)
{
    float dummy_n = 0.f, dummy_x = 0.f, dummy_z = 0.f;
    int blocks = 0;
    float x0 = pad_lx - 20.f, z0 = pad_lz - 20.f, x1 = pad_lx + 20.f, z1 = pad_lz + 20.f;
    if (g_bg_rooms < 1)
        return 0;
    g1_scan_chr_leaves(cam_lx, cam_lz, pad_lx, pad_lz, x0, z0, x1, z1, NULL, NULL, 0,
                       &dummy_n, &dummy_x, &dummy_z, &blocks);
    return blocks;
}

void port_stage_dump_chr_vs_g1(float cam_lx, float cam_lz, float pad_lx, float pad_lz,
                               float x0, float z0, float x1, float z1)
{
    G1LeafHit hits[PORT_G1_LEAF_DUMP];
    int nh = 0, blocks = 0, i;
    float best_need = 0.f, bnx = 0.f, bnz = 0.f, pdx = 0.f, pdz = 0.f;
    if (x1 < x0) {
        float t = x0;
        x0 = x1;
        x1 = t;
    }
    if (z1 < z0) {
        float t = z0;
        z0 = z1;
        z1 = t;
    }
    g1_scan_chr_leaves(cam_lx, cam_lz, pad_lx, pad_lz, x0, z0, x1, z1, hits, &nh,
                       PORT_G1_LEAF_DUMP, &best_need, &bnx, &bnz, &blocks);
    (void)port_stage_g1_chr_push(cam_lx, cam_lz, pad_lx, pad_lz, x0, z0, x1, z1, &pdx, &pdz);
    printf("g1leaf cam=%.1f,%.1f pad=%.1f,%.1f aabb=%.1f,%.1f..%.1f,%.1f span=%.1f,%.1f "
           "hits=%d block=%d push=%.1f,%.1f need=%.1f\n",
           (double)cam_lx, (double)cam_lz, (double)pad_lx, (double)pad_lz, (double)x0,
           (double)z0, (double)x1, (double)z1, (double)(x1 - x0), (double)(z1 - z0), nh,
           blocks, (double)pdx, (double)pdz, (double)best_need);
    for (i = 0; i < nh; i++) {
        const G1LeafHit *h = &hits[i];
        printf("g1leaf[%d] pc=%.1f,%.1f n=%.2f,%.2f hw=%.1f thick=%.1f pad_along=%.1f "
               "amin=%.1f amax=%.1f straddle=%d ray=%d both=%d dcam=%.1f need=%.1f\n",
               i, (double)h->px, (double)h->pz, (double)h->nx, (double)h->nz,
               (double)h->half_w, (double)h->thick, (double)h->pad_along, (double)h->amin,
               (double)h->amax, h->straddle, h->ray, h->both, (double)h->dcam,
               (double)h->need);
    }
}

static void g1_collect_room_walls(int room)
{
    const PortBgRoom *rm;
    const uint8_t *dl, *vtxbase;
    uint32_t ncmd, i;
    float ox, oz, sc, r1x, r1z;
    float slot[PORT_G1_VTX_CACHE][3];
    int have[PORT_G1_VTX_CACHE];

    if (room < 1 || room > g_bg_rooms || room >= PORT_MAX_BG_ROOMS)
        return;
    if (g_rm_wh[room])
        return;
    g_rm_wh[room] = 1;
    g_rm_w0[room] = (uint16_t)g_nwalls;
    g_rm_wn[room] = 0;
    rm = &g_rm[room];
    dl = room_pri(rm);
    if (!dl || rm->pri_ngfx == 0 || !rm->vtx)
        return;
    vtxbase = rm->vtx;
    sc = (g_bg_inv != 0.f) ? g_bg_inv : 1.f;
    r1x = g_rm[1].pos[0] * sc;
    r1z = g_rm[1].pos[2] * sc;
    ox = rm->pos[0] * sc - r1x;
    oz = rm->pos[2] * sc - r1z;
    memset(have, 0, sizeof have);
    ncmd = rm->pri_ngfx;
    for (i = 0; i < ncmd; i++) {
        const uint8_t *p = dl + i * 8u;
        uint8_t cmd = p[0];
        uint32_t w0 = be32(p);
        uint32_t w1 = be32(p + 4);
        if (cmd == 0x04) {
            uint32_t param = (w0 >> 16) & 0xFFu;
            uint32_t n = (param >> 4) + 1u;
            uint32_t v0 = param & 0xFu;
            uint32_t seg = w1 >> 24;
            uint32_t off = w1 & 0x00FFFFFFu;
            uint32_t k;
            const uint8_t *src;
            if (seg != 14 || off + n * 16u > rm->vtx_len)
                continue;
            src = vtxbase + off;
            for (k = 0; k < n && v0 + k < PORT_G1_VTX_CACHE; k++) {
                const uint8_t *s = src + k * 16u;
                slot[v0 + k][0] = (float)dump_be16(s);
                slot[v0 + k][1] = (float)dump_be16(s + 2);
                slot[v0 + k][2] = (float)dump_be16(s + 4);
                have[v0 + k] = 1;
            }
            continue;
        }
        if (cmd != 0xB1)
            continue;
        {
            uint32_t tris[4][3];
            int t;
            tris[0][0] = w1 & 0xF;
            tris[0][1] = (w1 >> 4) & 0xF;
            tris[0][2] = w0 & 0xF;
            tris[1][0] = (w1 >> 8) & 0xF;
            tris[1][1] = (w1 >> 12) & 0xF;
            tris[1][2] = (w0 >> 4) & 0xF;
            tris[2][0] = (w1 >> 16) & 0xF;
            tris[2][1] = (w1 >> 20) & 0xF;
            tris[2][2] = (w0 >> 8) & 0xF;
            tris[3][0] = (w1 >> 24) & 0xF;
            tris[3][1] = (w1 >> 28) & 0xF;
            tris[3][2] = (w0 >> 12) & 0xF;
            for (t = 0; t < 4; t++) {
                G1LeafPlane pl;
                float x0, y0, z0, x1, y1, z1, x2, y2, z2;
                int a, b, c;
                a = (int)tris[t][0];
                b = (int)tris[t][1];
                c = (int)tris[t][2];
                if (a == 0 && b == 0 && c == 0)
                    continue;
                if (a >= PORT_G1_VTX_CACHE || b >= PORT_G1_VTX_CACHE ||
                    c >= PORT_G1_VTX_CACHE)
                    continue;
                if (!have[a] || !have[b] || !have[c])
                    continue;
                x0 = slot[a][0];
                y0 = slot[a][1];
                z0 = slot[a][2];
                x1 = slot[b][0];
                y1 = slot[b][1];
                z1 = slot[b][2];
                x2 = slot[c][0];
                y2 = slot[c][1];
                z2 = slot[c][2];
                if (g_nwalls >= PORT_G1_WALL_MAX || g_rm_wn[room] >= PORT_G1_WALL_PER)
                    return;
                if (!g1_plane_from_tri_hw(x0, y0, z0, x1, y1, z1, x2, y2, z2, sc, ox, oz,
                                          800.f, &pl))
                    continue;
                g_walls[g_nwalls++] = pl;
                g_rm_wn[room]++;
            }
        }
    }
}

static int g1_wall_is_opening(float pcx, float pcz)
{
    float r1[3];
    int i, n;

    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    n = port_stage_opening_count();
    for (i = 0; i < n; i++) {
        float pos[3], yaw = 0.f, w = 0.f, dx, dz;
        int ra = 0, rb = 0;
        if (port_stage_opening(i, pos, &yaw, &w, &ra, &rb) != 0)
            continue;
        if (!port_stage_path_opening(ra, rb))
            continue;
        dx = (pos[0] - r1[0]) - pcx;
        dz = (pos[2] - r1[2]) - pcz;
        if (dx * dx + dz * dz < 90.f * 90.f)
            return 1;
    }
    return 0;
}

/* 1 if this vertical is a room portal (walkable both sides, different rooms)
 * or a path opening. Interior partitions (same tile / one-sided) stay solid. */
static int g1_wall_is_portal(const G1LeafPlane *pl)
{
    float fx, fz, bx, bz;
    int fr, br;

    if (g1_wall_is_opening(pl->pcx, pl->pcz))
        return 1;
    fx = pl->pcx + pl->wx * 40.f;
    fz = pl->pcz + pl->wz * 40.f;
    bx = pl->pcx - pl->wx * 40.f;
    bz = pl->pcz - pl->wz * 40.f;
    if (!port_stan_on_tile(fx, fz) || !port_stan_on_tile(bx, bz))
        return 0;
    fr = port_stan_tile_room(fx, fz);
    br = port_stan_tile_room(bx, bz);
    if (fr >= 1 && br >= 1 && fr != br)
        return 1;
    return 0;
}

static int g1_wall_rooms(float lx, float lz, int *rooms, int max)
{
    /* Same walked + portal-adjacent set as door-leaf scan, so a bathroom
     * G1 face that lives in the neighbor room still clips. */
    return g1_chr_rooms(lx, lz, lx, lz, rooms, max);
}

int port_stage_g1_wall_push(float lx, float lz, float radius, float *pdx, float *pdz)
{
    float dx = 0.f, dz = 0.f;
    int rooms[8], nr, iter;
    const float skin = 8.f;
    const float cap = 80.f;
    float clear;

    if (pdx)
        *pdx = 0.f;
    if (pdz)
        *pdz = 0.f;
    if (g_bg_rooms < 8 || radius < 1.f)
        return 0;
    clear = radius + skin;
    nr = g1_wall_rooms(lx, lz, rooms, 8);
    for (iter = 0; iter < 4; iter++) {
        int j, hit = 0;
        float best_need = 0.f, bnx = 0.f, bnz = 0.f;
        float cx = lx + dx, cz = lz + dz;
        for (j = 0; j < nr; j++) {
            int room = rooms[j], i, p0, pn;
            g1_collect_room_walls(room);
            if (room < 1 || room >= PORT_MAX_BG_ROOMS)
                continue;
            p0 = (int)g_rm_w0[room];
            pn = (int)g_rm_wn[room];
            for (i = 0; i < pn; i++) {
                const G1LeafPlane *pl = &g_walls[p0 + i];
                float rx, rz, along, across, need, side, tcx, tcz;
                int k;
                if (g1_wall_is_portal(pl))
                    continue;
                rx = cx - pl->pcx;
                rz = cz - pl->pcz;
                along = rx * pl->wx + rz * pl->wz;
                across = rx * pl->tx + rz * pl->tz;
                if (across < 0.f)
                    across = -across;
                if (across > pl->half_w + radius)
                    continue;
                if (along < 0.f) {
                    if (-along >= clear)
                        continue;
                } else if (along >= clear)
                    continue;
                /* Pull toward the current-tile centroid so a camera that
                 * already walked through the G1 face is unstuck into the
                 * room, not shoved further out. */
                tcx = cx;
                tcz = cz;
                {
                    /* Approximate centroid: average of tile verts in local. */
                    /* port_stan has no centroid export; probe 4 offsets. */
                    float sx = 0.f, sz = 0.f;
                    int n = 0;
                    static const float kdx[4] = { 24.f, -24.f, 0.f, 0.f };
                    static const float kdz[4] = { 0.f, 0.f, 24.f, -24.f };
                    for (k = 0; k < 4; k++) {
                        float px = cx + kdx[k], pz = cz + kdz[k];
                        if (!port_stan_on_tile(px, pz))
                            continue;
                        if (port_stan_tile_room(px, pz) != port_stan_tile_room(cx, cz) &&
                            port_stan_tile_room(cx, cz) >= 1)
                            continue;
                        sx += px;
                        sz += pz;
                        n++;
                    }
                    if (n > 0) {
                        tcx = sx / (float)n;
                        tcz = sz / (float)n;
                    }
                }
                side = ((tcx - pl->pcx) * pl->wx + (tcz - pl->pcz) * pl->wz >= 0.f) ? 1.f
                                                                                    : -1.f;
                need = clear - side * along;
                if (need <= 0.f)
                    continue;
                if (need > best_need) {
                    best_need = need;
                    bnx = pl->wx * side;
                    bnz = pl->wz * side;
                    hit = 1;
                }
            }
        }
        if (!hit)
            break;
        if (best_need > cap)
            best_need = cap;
        dx += bnx * best_need;
        dz += bnz * best_need;
        if (dx * dx + dz * dz > cap * cap) {
            float len = sqrtf(dx * dx + dz * dz);
            dx *= cap / len;
            dz *= cap / len;
            break;
        }
    }
    if (dx == 0.f && dz == 0.f)
        return 0;
    if (pdx)
        *pdx = dx;
    if (pdz)
        *pdz = dz;
    return 1;
}

int port_stage_g1_wall_ray(float lx, float lz, float dx, float dz, float *t_out)
{
    int rooms[8], nr, j;
    float best = 4000.f;
    int hit = 0;
    float len, ux, uz;

    if (g_bg_rooms < 8)
        return 0;
    len = sqrtf(dx * dx + dz * dz);
    if (len < 1e-4f)
        return 0;
    ux = dx / len;
    uz = dz / len;
    nr = g1_wall_rooms(lx, lz, rooms, 8);
    for (j = 0; j < nr; j++) {
        int room = rooms[j], i, p0, pn;
        g1_collect_room_walls(room);
        if (room < 1 || room >= PORT_MAX_BG_ROOMS)
            continue;
        p0 = (int)g_rm_w0[room];
        pn = (int)g_rm_wn[room];
        for (i = 0; i < pn; i++) {
            const G1LeafPlane *pl = &g_walls[p0 + i];
            float denom, t, hx, hz, across;
            if (g1_wall_is_portal(pl))
                continue;
            denom = ux * pl->wx + uz * pl->wz;
            if (fabsf(denom) < 1e-4f)
                continue;
            t = ((pl->pcx - lx) * pl->wx + (pl->pcz - lz) * pl->wz) / denom;
            if (t < 0.05f || t > 4000.f)
                continue;
            hx = lx + ux * t;
            hz = lz + uz * t;
            across = (hx - pl->pcx) * pl->tx + (hz - pl->pcz) * pl->tz;
            if (across < 0.f)
                across = -across;
            if (across > pl->half_w + 10.f)
                continue;
            if (t < best) {
                best = t;
                hit = 1;
            }
        }
    }
    if (!hit)
        return 0;
    if (t_out)
        *t_out = best;
    return 1;
}

void port_stage_dump_g1_walls(float lx, float lz)
{
    int rooms[8], nr, j, npr = 0;
    float pdx = 0.f, pdz = 0.f, t = 0.f;

    if (g_bg_rooms < 8) {
        printf("g1clip cam=%.1f,%.1f walls=0 (no retail G1)\n", (double)lx, (double)lz);
        return;
    }
    nr = g1_wall_rooms(lx, lz, rooms, 8);
    (void)port_stage_g1_wall_push(lx, lz, 30.f, &pdx, &pdz);
    printf("g1clip cam=%.1f,%.1f rooms=%d push=%.1f,%.1f ray=%d", (double)lx, (double)lz, nr,
           (double)pdx, (double)pdz, port_stage_g1_wall_ray(lx, lz, 0.f, 1.f, &t));
    if (t > 0.f)
        printf(" t_z+=%.1f", (double)t);
    printf("\n");
    for (j = 0; j < nr; j++) {
        int room = rooms[j], i, p0, pn;
        g1_collect_room_walls(room);
        if (room < 1 || room >= PORT_MAX_BG_ROOMS)
            continue;
        p0 = (int)g_rm_w0[room];
        pn = (int)g_rm_wn[room];
        for (i = 0; i < pn && npr < 8; i++) {
            const G1LeafPlane *pl = &g_walls[p0 + i];
            float rx = lx - pl->pcx, rz = lz - pl->pcz;
            float along = rx * pl->wx + rz * pl->wz;
            float across = rx * pl->tx + rz * pl->tz;
            int portal = g1_wall_is_portal(pl);
            if (across < 0.f)
                across = -across;
            if (across > pl->half_w + 40.f)
                continue;
            printf("g1clip[%d] r%d pc=%.1f,%.1f n=%.2f,%.2f hw=%.1f along=%.1f across=%.1f "
                   "portal=%d\n",
                   npr, room, (double)pl->pcx, (double)pl->pcz, (double)pl->wx,
                   (double)pl->wz, (double)pl->half_w, (double)along, (double)across, portal);
            npr++;
        }
    }
}

/* Door-sized holes in G1 walls. Rare portals sit elsewhere; after closed-door
 * portal cull these cutouts read as black. Player-local verts (v*inv + ox). */
#define PORT_G1_CUT_MAX 24
#define PORT_G1_VTRI_MAX 768
#define PORT_G1_CLUS_MAX 48

typedef struct {
    float px, py, pz;
    float yaw;
    float width, tall;
    int room;
} G1Cutout;

typedef struct {
    float amin, amax, ymin, ymax;
    float nx, nz, d, tx, tz;
    float cx, cy, cz;
} G1VTri;

static G1Cutout g_cuts[PORT_G1_CUT_MAX];
static int g_ncuts;
static uint8_t g_rm_ch[PORT_MAX_BG_ROOMS];

static void g1_clear_cutouts(void)
{
    g_ncuts = 0;
    memset(g_rm_ch, 0, sizeof g_rm_ch);
}

static int g1_project(float x, float y, float z, float ex, float ey, float ez, float th,
                      float ph, float fovy, float aspect, float *sx, float *sy, float *tz)
{
    float rad = 3.14159265f / 180.f;
    float cph, sph, fx, fy, fz, rx, rz, ux, uy, uz, relx, rely, relz;
    float vx, vy, vz, ft, ndx, ndy, cw;

    th *= rad;
    ph *= rad;
    cph = cosf(ph);
    sph = sinf(ph);
    fx = sinf(th) * cph;
    fy = sph;
    fz = -cosf(th) * cph;
    rx = cosf(th);
    rz = sinf(th);
    ux = -sinf(th) * sph;
    uy = cph;
    uz = cosf(th) * sph;
    relx = x - ex;
    rely = y - ey;
    relz = z - ez;
    vx = rx * relx + rz * relz;
    vy = ux * relx + uy * rely + uz * relz;
    /* View Z is negative in front (look-at V[2] = -forward). */
    vz = -(fx * relx + fy * rely + fz * relz);
    cw = -vz;
    if (tz)
        *tz = cw;
    if (cw < 8.f)
        return 0;
    ft = 1.f / tanf((fovy * rad) * 0.5f);
    ndx = (ft / aspect) * vx / cw;
    /* P[1][1] = -ft so +Y world is up (small sy). */
    ndy = -ft * vy / cw;
    *sx = ndx * 160.f + 160.f;
    *sy = ndy * 120.f + 120.f;
    return 1;
}

static int g1_cut_add(float px, float py, float pz, float yaw, float width, float tall,
                      int room)
{
    int i;
    if (width < 80.f || width > 450.f || tall < 80.f || tall > 500.f)
        return 0;
    for (i = 0; i < g_ncuts; i++) {
        float dx = g_cuts[i].px - px, dz = g_cuts[i].pz - pz;
        if (dx * dx + dz * dz < 60.f * 60.f)
            return 0;
    }
    if (g_ncuts >= PORT_G1_CUT_MAX)
        return 0;
    g_cuts[g_ncuts].px = px;
    g_cuts[g_ncuts].py = py;
    g_cuts[g_ncuts].pz = pz;
    g_cuts[g_ncuts].yaw = yaw;
    g_cuts[g_ncuts].width = width;
    g_cuts[g_ncuts].tall = tall;
    g_cuts[g_ncuts].room = room;
    g_ncuts++;
    return 1;
}

static void g1_cluster_cutouts(int room, const G1VTri *tri, int ntri, float camx, float camz)
{
    static int used[PORT_G1_VTRI_MAX];
    static int idx[PORT_G1_VTRI_MAX];
    int c, i, j;

    if (ntri < 4)
        return;
    memset(used, 0, sizeof(int) * (size_t)ntri);
    for (c = 0; c < PORT_G1_CLUS_MAX; c++) {
        int nn = 0;
        float nx, nz, d, tx, tz, amin, amax, ymin, ymax;
        int seed = -1;
        for (i = 0; i < ntri; i++) {
            if (!used[i]) {
                seed = i;
                break;
            }
        }
        if (seed < 0)
            break;
        nx = tri[seed].nx;
        nz = tri[seed].nz;
        d = tri[seed].d;
        tx = tri[seed].tx;
        tz = tri[seed].tz;
        for (i = 0; i < ntri; i++) {
            float dn, dd;
            if (used[i])
                continue;
            dn = nx * tri[i].nx + nz * tri[i].nz;
            dd = tri[i].d - d;
            if (dd < 0.f)
                dd = -dd;
            if (dn < 0.92f || dd > 18.f)
                continue;
            used[i] = 1;
            idx[nn++] = i;
        }
        if (nn < 4)
            continue;
        amin = tri[idx[0]].amin;
        amax = tri[idx[0]].amax;
        ymin = tri[idx[0]].ymin;
        ymax = tri[idx[0]].ymax;
        for (i = 1; i < nn; i++) {
            const G1VTri *t = &tri[idx[i]];
            if (t->amin < amin)
                amin = t->amin;
            if (t->amax > amax)
                amax = t->amax;
            if (t->ymin < ymin)
                ymin = t->ymin;
            if (t->ymax > ymax)
                ymax = t->ymax;
        }
        if (amax - amin < 140.f || ymax - ymin < 120.f)
            continue;
        /* Interior band: look for an along-gap the jambs enclose. */
        {
            float y0 = ymin + 0.28f * (ymax - ymin);
            float y1 = ymin + 0.72f * (ymax - ymin);
            float iv0[32], iv1[32];
            int niv = 0, g;
            for (i = 0; i < nn; i++) {
                const G1VTri *t = &tri[idx[i]];
                if (t->ymax < y0 || t->ymin > y1)
                    continue;
                if (niv < 32) {
                    iv0[niv] = t->amin;
                    iv1[niv] = t->amax;
                    niv++;
                }
            }
            for (i = 0; i < niv; i++) {
                int best = i;
                for (j = i + 1; j < niv; j++) {
                    if (iv0[j] < iv0[best])
                        best = j;
                }
                if (best != i) {
                    float a = iv0[i], b = iv1[i];
                    iv0[i] = iv0[best];
                    iv1[i] = iv1[best];
                    iv0[best] = a;
                    iv1[best] = b;
                }
            }
            {
                float m0[32], m1[32];
                int nm = 0;
                for (i = 0; i < niv; i++) {
                    if (nm == 0 || iv0[i] > m1[nm - 1] + 12.f) {
                        if (nm < 32) {
                            m0[nm] = iv0[i];
                            m1[nm] = iv1[i];
                            nm++;
                        }
                    } else if (iv1[i] > m1[nm - 1])
                        m1[nm - 1] = iv1[i];
                }
                for (g = 0; g + 1 < nm; g++) {
                    float g0 = m1[g], g1 = m0[g + 1], gw = g1 - g0, amid, sill, lint;
                    float pcx, pcz, tocam, yaw, hx, hz;
                    int k, nsill = 0, nlint = 0;
                    if (gw < 80.f || gw > 420.f)
                        continue;
                    amid = 0.5f * (g0 + g1);
                    sill = ymin;
                    lint = ymax;
                    for (k = 0; k < nn; k++) {
                        const G1VTri *t = &tri[idx[k]];
                        if (t->amax < amid - 8.f || t->amin > amid + 8.f)
                            continue;
                        if (t->ymax < y0) {
                            if (!nsill || t->ymax > sill)
                                sill = t->ymax;
                            nsill = 1;
                        }
                        if (t->ymin > y1) {
                            if (!nlint || t->ymin < lint)
                                lint = t->ymin;
                            nlint = 1;
                        }
                    }
                    if (!nsill)
                        sill = ymin;
                    if (!nlint)
                        lint = ymax;
                    if (lint - sill < 80.f || lint - sill > 500.f)
                        continue;
                    /* Plane point at along=amid: origin is not stored; recover
                     * from any tri centroid projected onto the plane. */
                    {
                        const G1VTri *s = &tri[idx[0]];
                        float a0 = (s->cx * tx + s->cz * tz);
                        pcx = s->cx + (amid - a0) * tx;
                        pcz = s->cz + (amid - a0) * tz;
                    }
                    hx = camx - pcx;
                    hz = camz - pcz;
                    tocam = hx * nx + hz * nz;
                    if (tocam < 0.f) {
                        nx = -nx;
                        nz = -nz;
                        tocam = -tocam;
                    }
                    /* Face the camera so the XY slab is not edge-on. */
                    if (fabsf(nx) >= fabsf(nz))
                        yaw = (pcx < camx) ? 90.f : -90.f;
                    else
                        yaw = (pcz < camz) ? 0.f : 180.f;
                    (void)tocam;
                    g1_cut_add(pcx, sill, pcz, yaw, gw, lint - sill, room);
                }
            }
        }
    }
}

static void g1_collect_room_cutouts(int room)
{
    const PortBgRoom *rm;
    const uint8_t *dl, *vtxbase;
    uint32_t ncmd, i;
    float ox, oy, oz, sc, r1x, r1y, r1z;
    float slot[PORT_G1_VTX_CACHE][3];
    int have[PORT_G1_VTX_CACHE];
    static G1VTri tri[PORT_G1_VTRI_MAX];
    int ntri = 0;
    float camx, camz;

    if (room < 1 || room > g_bg_rooms || room >= PORT_MAX_BG_ROOMS)
        return;
    if (g_rm_ch[room])
        return;
    g_rm_ch[room] = 1;
    rm = &g_rm[room];
    dl = room_pri(rm);
    if (!dl || rm->pri_ngfx == 0 || !rm->vtx)
        return;
    vtxbase = rm->vtx;
    sc = (g_bg_inv != 0.f) ? g_bg_inv : 1.f;
    r1x = g_rm[1].pos[0] * sc;
    r1y = g_rm[1].pos[1] * sc;
    r1z = g_rm[1].pos[2] * sc;
    ox = rm->pos[0] * sc - r1x;
    oy = rm->pos[1] * sc - r1y;
    oz = rm->pos[2] * sc - r1z;
    camx = port_player_x();
    camz = port_player_z();
    memset(have, 0, sizeof have);
    ncmd = rm->pri_ngfx;
    for (i = 0; i < ncmd; i++) {
        const uint8_t *p = dl + i * 8u;
        uint8_t cmd = p[0];
        uint32_t w0 = be32(p);
        uint32_t w1 = be32(p + 4);
        if (cmd == 0x04) {
            uint32_t param = (w0 >> 16) & 0xFFu;
            uint32_t n = (param >> 4) + 1u;
            uint32_t v0 = param & 0xFu;
            uint32_t seg = w1 >> 24;
            uint32_t off = w1 & 0x00FFFFFFu;
            uint32_t k;
            const uint8_t *src;
            if (seg != 14 || off + n * 16u > rm->vtx_len)
                continue;
            src = vtxbase + off;
            for (k = 0; k < n && v0 + k < PORT_G1_VTX_CACHE; k++) {
                const uint8_t *s = src + k * 16u;
                slot[v0 + k][0] = (float)dump_be16(s);
                slot[v0 + k][1] = (float)dump_be16(s + 2);
                slot[v0 + k][2] = (float)dump_be16(s + 4);
                have[v0 + k] = 1;
            }
            continue;
        }
        if (cmd != 0xB1)
            continue;
        {
            uint32_t tris[4][3];
            int t;
            tris[0][0] = w1 & 0xF;
            tris[0][1] = (w1 >> 4) & 0xF;
            tris[0][2] = w0 & 0xF;
            tris[1][0] = (w1 >> 8) & 0xF;
            tris[1][1] = (w1 >> 12) & 0xF;
            tris[1][2] = (w0 >> 4) & 0xF;
            tris[2][0] = (w1 >> 16) & 0xF;
            tris[2][1] = (w1 >> 20) & 0xF;
            tris[2][2] = (w0 >> 8) & 0xF;
            tris[3][0] = (w1 >> 24) & 0xF;
            tris[3][1] = (w1 >> 28) & 0xF;
            tris[3][2] = (w0 >> 12) & 0xF;
            for (t = 0; t < 4; t++) {
                float x0, y0, z0, x1, y1, z1, x2, y2, z2;
                float e1x, e1y, e1z, e2x, e2y, e2z, nx, ny, nz, nlen, wlen, wx, wz;
                float px0, py0, pz0, px1, py1, pz1, px2, py2, pz2;
                float tx, tz, amin, amax, ymin, ymax;
                int a, b, c;
                a = (int)tris[t][0];
                b = (int)tris[t][1];
                c = (int)tris[t][2];
                if (a == 0 && b == 0 && c == 0)
                    continue;
                if (a >= PORT_G1_VTX_CACHE || b >= PORT_G1_VTX_CACHE ||
                    c >= PORT_G1_VTX_CACHE)
                    continue;
                if (!have[a] || !have[b] || !have[c])
                    continue;
                x0 = slot[a][0];
                y0 = slot[a][1];
                z0 = slot[a][2];
                x1 = slot[b][0];
                y1 = slot[b][1];
                z1 = slot[b][2];
                x2 = slot[c][0];
                y2 = slot[c][1];
                z2 = slot[c][2];
                e1x = x1 - x0;
                e1y = y1 - y0;
                e1z = z1 - z0;
                e2x = x2 - x0;
                e2y = y2 - y0;
                e2z = z2 - z0;
                nx = e1y * e2z - e1z * e2y;
                ny = e1z * e2x - e1x * e2z;
                nz = e1x * e2y - e1y * e2x;
                nlen = sqrtf(nx * nx + ny * ny + nz * nz);
                if (nlen < 1.f)
                    continue;
                if (fabsf(ny) > 0.35f * nlen)
                    continue;
                wx = nx;
                wz = nz;
                wlen = sqrtf(wx * wx + wz * wz);
                if (wlen < 1e-3f)
                    continue;
                wx /= wlen;
                wz /= wlen;
                px0 = x0 * sc + ox;
                py0 = y0 * sc + oy;
                pz0 = z0 * sc + oz;
                px1 = x1 * sc + ox;
                py1 = y1 * sc + oy;
                pz1 = z1 * sc + oz;
                px2 = x2 * sc + ox;
                py2 = y2 * sc + oy;
                pz2 = z2 * sc + oz;
                tx = -wz;
                tz = wx;
                amin = amax = px0 * tx + pz0 * tz;
                ymin = ymax = py0;
                {
                    float aa = px1 * tx + pz1 * tz;
                    if (aa < amin)
                        amin = aa;
                    if (aa > amax)
                        amax = aa;
                    aa = px2 * tx + pz2 * tz;
                    if (aa < amin)
                        amin = aa;
                    if (aa > amax)
                        amax = aa;
                    if (py1 < ymin)
                        ymin = py1;
                    if (py1 > ymax)
                        ymax = py1;
                    if (py2 < ymin)
                        ymin = py2;
                    if (py2 > ymax)
                        ymax = py2;
                }
                if (ymax - ymin < 20.f)
                    continue;
                if (ntri >= PORT_G1_VTRI_MAX)
                    goto clustered;
                tri[ntri].amin = amin;
                tri[ntri].amax = amax;
                tri[ntri].ymin = ymin;
                tri[ntri].ymax = ymax;
                tri[ntri].nx = wx;
                tri[ntri].nz = wz;
                tri[ntri].d = wx * px0 + wz * pz0;
                tri[ntri].tx = tx;
                tri[ntri].tz = tz;
                tri[ntri].cx = (px0 + px1 + px2) / 3.f;
                tri[ntri].cy = (py0 + py1 + py2) / 3.f;
                tri[ntri].cz = (pz0 + pz1 + pz2) / 3.f;
                ntri++;
            }
        }
    }
clustered:
    g1_cluster_cutouts(room, tri, ntri, camx, camz);
}

static void g1_ensure_cutouts(void)
{
    int i, r;
    /* Synthetic 1-room G1DL packs have no Facility wall cutouts; filling
     * them covered magenta prop tests. Retail C0 Facility has many rooms. */
    if (g_bg_rooms < 8 || !g_gdl_c0)
        return;
    r = g_cur_room;
    if (r < 1)
        r = port_stan_tile_room(port_player_x(), port_player_z());
    if (r >= 1)
        g1_collect_room_cutouts(r);
    for (i = 0; i < g_rooms_walked; i++)
        g1_collect_room_cutouts((int)g_walked[i]);
}

int port_stage_g1_cutout_count(void)
{
    g1_ensure_cutouts();
    return g_ncuts;
}

int port_stage_g1_cutout(int i, float pos[3], float *yaw, float *width, float *tall)
{
    float r1[3];
    g1_ensure_cutouts();
    if (i < 0 || i >= g_ncuts)
        return -1;
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    /* World = player-local + room1 (same as spawn-alcove fill). */
    if (pos) {
        pos[0] = g_cuts[i].px + r1[0];
        pos[1] = g_cuts[i].py + r1[1];
        pos[2] = g_cuts[i].pz + r1[2];
    }
    if (yaw)
        *yaw = g_cuts[i].yaw;
    if (width)
        *width = g_cuts[i].width;
    if (tall)
        *tall = g_cuts[i].tall;
    return 0;
}

void port_stage_dump_g1_cutouts(float cam_lx, float cam_ly, float cam_lz, float theta_deg,
                                float pitch_deg)
{
    int i;
    float fovy = port_persp_fovy();
    float aspect = port_persp_aspect();
    float vx = cam_lx, vz = cam_lz;

    port_stan_visual_xz(vx, vz, &vx, &vz);
    g_cur_room = port_stan_tile_room_at_eye(cam_lx, cam_lz, cam_ly);
    if (g_cur_room < 1)
        g_cur_room = port_stan_tile_room(cam_lx, cam_lz);
    g1_ensure_cutouts();
    printf("g1cut cam=%.1f,%.1f,%.1f vis=%.1f,%.1f th=%.1f ph=%.1f cur=%d n=%d "
           "fovy=%.1f aspect=%.3f\n",
           (double)cam_lx, (double)cam_ly, (double)cam_lz, (double)vx, (double)vz,
           (double)theta_deg, (double)pitch_deg, g_cur_room, g_ncuts, (double)fovy,
           (double)aspect);
    for (i = 0; i < g_ncuts; i++) {
        float sx, sy, tz, dx, dz, d;
        int onscr;
        dx = g_cuts[i].px - cam_lx;
        dz = g_cuts[i].pz - cam_lz;
        d = sqrtf(dx * dx + dz * dz);
        onscr = g1_project(g_cuts[i].px, g_cuts[i].py + 0.5f * g_cuts[i].tall, g_cuts[i].pz,
                           vx, cam_ly, vz, theta_deg, pitch_deg, fovy, aspect, &sx, &sy, &tz);
        {
            float lookx = sinf(theta_deg * (3.14159265f / 180.f));
            float lookz = -cosf(theta_deg * (3.14159265f / 180.f));
            float along = dx * lookx + dz * lookz;
            float c0x, c0y, c1x, c1y, c2x, c2y, c3x, c3y, tz0;
            float hw = 0.5f * g_cuts[i].width;
            float tx, tz2, y0, y1;
            if (g_cuts[i].yaw == 90.f || g_cuts[i].yaw == -90.f) {
                tx = 0.f;
                tz2 = 1.f;
            } else {
                tx = 1.f;
                tz2 = 0.f;
            }
            y0 = g_cuts[i].py;
            y1 = g_cuts[i].py + g_cuts[i].tall;
            (void)g1_project(g_cuts[i].px - hw * tx, y0, g_cuts[i].pz - hw * tz2, vx, cam_ly,
                             vz, theta_deg, pitch_deg, fovy, aspect, &c0x, &c0y, &tz0);
            (void)g1_project(g_cuts[i].px + hw * tx, y0, g_cuts[i].pz + hw * tz2, vx, cam_ly,
                             vz, theta_deg, pitch_deg, fovy, aspect, &c1x, &c1y, &tz0);
            (void)g1_project(g_cuts[i].px - hw * tx, y1, g_cuts[i].pz - hw * tz2, vx, cam_ly,
                             vz, theta_deg, pitch_deg, fovy, aspect, &c2x, &c2y, &tz0);
            (void)g1_project(g_cuts[i].px + hw * tx, y1, g_cuts[i].pz + hw * tz2, vx, cam_ly,
                             vz, theta_deg, pitch_deg, fovy, aspect, &c3x, &c3y, &tz0);
            printf("g1cut[%d] r%d local=%.1f,%.1f,%.1f yaw=%.0f w=%.1f h=%.1f d=%.1f "
                   "along=%.1f mid=%.0f,%.0f on=%d corners=%.0f,%.0f %.0f,%.0f %.0f,%.0f "
                   "%.0f,%.0f\n",
                   i, g_cuts[i].room, (double)g_cuts[i].px, (double)g_cuts[i].py,
                   (double)g_cuts[i].pz, (double)g_cuts[i].yaw, (double)g_cuts[i].width,
                   (double)g_cuts[i].tall, (double)d, (double)along, (double)sx, (double)sy,
                   onscr, (double)c0x, (double)c0y, (double)c1x, (double)c1y, (double)c2x,
                   (double)c2y, (double)c3x, (double)c3y);
        }
    }
}

int port_stage_draw(void)
{
    /* Static: wasm default stack is 64KB; passes+rpos+cutout tris overflow
     * it (~85KB+), corrupt the heap, detach HEAPU8, and freeze the tab
     * after a few walked rooms / mallocs. Draw is not re-entrant. */
    static G1RoomDl passes[PORT_DRAW_MAX];
    static float rpos[PORT_MAX_BG_ROOMS * 3];
    uint8_t ids[PORT_WALK_MAX];
    int nsel, i, k;
    float ox, oy, oz;

    g_rooms_walked = 0;
    memset(g_walked, 0, sizeof g_walked);
    g_cur_room = 0;
    if (!g_bg || g_bg_rooms < 1)
        return 1;

    nsel = select_rooms(ids, PORT_WALK_MAX);
    if (nsel < 1 && g_last_good_room >= 1 && g_last_good_room <= g_bg_rooms) {
        ids[0] = (uint8_t)g_last_good_room;
        nsel = 1;
        g_cur_room = g_last_good_room;
    }
    for (i = 0; i < nsel && i < PORT_WALK_MAX; i++)
        g_walked[i] = ids[i];
    g_rooms_walked = nsel;
    if (g_cur_room >= 1)
        g_last_good_room = g_cur_room;
    ox = g_rm[1].pos[0] * g_bg_inv;
    oy = g_rm[1].pos[1] * g_bg_inv;
    oz = g_rm[1].pos[2] * g_bg_inv;
    g1_set_segment(0xF, (uintptr_t)g_bg);
    if (g_rm[1].vtx)
        g1_set_segment(14, (uintptr_t)g_rm[1].vtx);
    {
        float py = port_player_y();
        float ey;
        /* First frame after spawn, or a stair NaN, must not feed look-at. */
        if (!(py == py) || py > 1.0e20f || py < -1.0e20f) {
            if (port_stan_eye_y(port_player_x(), port_player_z(), &ey) != 0)
                ey = PORT_EYE_HEIGHT;
            port_player_set_y(ey);
        }
    }
    {
        float vx = port_player_x(), vz = port_player_z();
        port_stan_visual_xz(vx, vz, &vx, &vz);
        g1_set_perspective(port_persp_fovy(), port_persp_aspect());
        g1_set_lookat(vx, port_player_y(), vz, port_player_theta());
    }
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
        passes[k].ox = rm->pos[0] * g_bg_inv - ox;
        passes[k].oy = rm->pos[1] * g_bg_inv - oy;
        passes[k].oz = rm->pos[2] * g_bg_inv - oz;
        /* Rare room matrix scale is room_data_float2. 0 means identity. */
        passes[k].scale = (g_bg_inv != 1.f) ? g_bg_inv : 0.f;
        k++;
    }
    for (i = 1; i <= g_bg_rooms; i++) {
        rpos[i * 3 + 0] = g_rm[i].pos[0] * g_bg_inv;
        rpos[i * 3 + 1] = g_rm[i].pos[1] * g_bg_inv;
        rpos[i * 3 + 2] = g_rm[i].pos[2] * g_bg_inv;
    }
    {
        float room1[3];
        room1[0] = ox;
        room1[1] = oy;
        room1[2] = oz;
        {
            int room_cap = PORT_DRAW_MAX - k;
            if (room_cap > 24)
                room_cap -= 24;
            k += port_prop_fill_rooms(passes + k, room_cap, room1, rpos, nsel, ids);
            if (k < PORT_DRAW_MAX)
                k += port_prop_fill_viewgun(passes + k, PORT_DRAW_MAX - k);
        }
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
