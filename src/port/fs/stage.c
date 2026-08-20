#include "stage.h"

#include "c0pack.h"
#include "pack_dma.h"
#include "rng/random.h"
#include "player/move.h"

#include "../../overrides/lv_clock.h"

#include <stdlib.h>
#include <string.h>

#define BG_SEG_BASE 0x0F000000u
#define BG_SEG_BIAS 0xF1000000u

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

static void *maybe_ptr(uint8_t *base, size_t n, uint32_t off)
{
    uint32_t rel;
    if ((off & 0xFF000000u) == BG_SEG_BASE)
        rel = (uint32_t)(off + BG_SEG_BIAS);
    else if (off < n)
        rel = off;
    else
        return NULL;
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

static int fixup_bg(uint8_t *bg, size_t n)
{
    uint32_t off;
    void *rooms;

    if (n < 64)
        return PORT_STAGE_ERR_FORMAT;
    off = be32(bg + 4);
    rooms = maybe_ptr(bg, n, off);
    (void)rooms;
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
