#include "stage.h"

#include "c0pack.h"
#include "pack_dma.h"
#include "rng/random.h"
#include "player/move.h"
#include "gfx/gbi_interp.h"

#include "../../overrides/lv_clock.h"

#include <stdlib.h>
#include <string.h>

#define BG_SEG_BASE 0x0F000000u
#define BG_SEG_BIAS 0xF1000000u
#define BG_ROOM_BYTES 24
#define BG_HDR_BYTES 64

typedef struct {
    int id;
    int files_id;
    const char *bg;
    const char *stan;
} StageFiles;

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
static size_t g_gdl_off;
static uint32_t g_gdl_ngfx;
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

/*
 * Rare load_bg_file: word[1] is the room table (bg_room_data[]), room 0 is
 * dummy, rooms 1..n until pPriMappingBin == 0. word[2] is the portal table.
 * Pointers stay segmented (K17); we resolve at access time.
 *
 * word[0] == PORT_BG_MAGIC_G1DL means room 1's primary mapping is an
 * uncompressed big-endian Fast3D GDL (synthetic CI only). Retail files use
 * 0 and compressed C0 — we count rooms but do not interpret those GDLs.
 */
static int fixup_bg(uint8_t *bg, size_t n)
{
    uint32_t magic, rooms_seg, portal_seg;
    uint8_t *rooms;
    int i;

    g_bg_rooms = 0;
    g_gdl_raw = 0;
    g_gdl_off = 0;
    g_gdl_ngfx = 0;

    if (n < BG_HDR_BYTES)
        return PORT_STAGE_ERR_FORMAT;
    magic = be32(bg);
    rooms_seg = be32(bg + 4);
    portal_seg = be32(bg + 8);
    rooms = (uint8_t *)maybe_ptr(bg, n, rooms_seg);
    if (!rooms)
        return PORT_STAGE_ERR_FORMAT;

    for (i = 1; i < 512; i++) {
        uint8_t *r = rooms + (size_t)i * BG_ROOM_BYTES;
        uint32_t pri, next_pri;
        uint8_t *gdl;
        size_t gdl_off, gdl_end;

        if (r + BG_ROOM_BYTES > bg + n)
            break;
        pri = be32(r + 4);
        if (pri == 0)
            break;
        gdl = (uint8_t *)maybe_ptr(bg, n, pri);
        if (!gdl)
            break;
        g_bg_rooms++;
        if (i == 1 && magic == PORT_BG_MAGIC_G1DL) {
            next_pri = 0;
            if (r + 2 * BG_ROOM_BYTES <= bg + n)
                next_pri = be32(r + BG_ROOM_BYTES + 4);
            gdl_off = (size_t)(gdl - bg);
            if (next_pri && maybe_ptr(bg, n, next_pri))
                gdl_end = seg_to_off(next_pri);
            else
                gdl_end = n;
            if (gdl_end > gdl_off) {
                g_gdl_off = gdl_off;
                g_gdl_ngfx = (uint32_t)((gdl_end - gdl_off) / 8u);
                g_gdl_raw = 1;
            }
        }
    }

    /* Portal list: walk until offset_portal == 0. Do not rewrite u32s (LP64). */
    if (portal_seg) {
        uint8_t *p = (uint8_t *)maybe_ptr(bg, n, portal_seg);
        int nport = 0;
        while (p && p + 8 <= bg + n && nport < 200) {
            uint32_t off = be32(p);
            if (off == 0)
                break;
            if (!maybe_ptr(bg, n, off))
                break;
            nport++;
            p += 8;
        }
        (void)nport;
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
    g_gdl_off = 0;
    g_gdl_ngfx = 0;
    g_first_room = NULL;
}

int port_stage_load(int level_id)
{
    const StageFiles *st = find_stage(level_id);
    uint8_t *bg = NULL, *stan = NULL;
    size_t bg_len = 0, stan_len = 0;
    int rc;

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

    g_bg = bg;
    g_bg_len = bg_len;
    g_stan = stan;
    g_stan_len = stan_len;
    g_level = level_id;
    g_CurrentStageToLoad = st->files_id;
    port_rng_on_stage_load();
    port_player_spawn();
    return PORT_STAGE_OK;
}

int port_stage_level_id(void) { return g_level; }

int port_stage_room_count(void) { return g_rooms; }

int port_stage_bg_rooms(void) { return g_bg_rooms; }

int port_stage_gdl_raw(void) { return g_gdl_raw; }

int port_stage_draw(void)
{
    if (!g_bg || !g_gdl_raw || g_gdl_ngfx == 0)
        return 1;
    g1_set_segment(0xF, (uintptr_t)g_bg);
    return g1_interpret_be_dl(g_bg + g_gdl_off, g_gdl_ngfx);
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
