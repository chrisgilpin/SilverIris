#include "stan_walk.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define BG_SEG_BASE 0x0F000000u
#define BG_SEG_BIAS 0xF1000000u
/* Facility retail is 2599 tiles; 2048 dropped the intro walkway. */
#define PORT_STAN_MAX_TILES 8192
#define PORT_STAN_MAX_PTS 10
#define PORT_STAN_MAX_DOORS 128
#define PORT_STAN_MAX_GUARDS 128
/* Pad-door default. Fitted path portals store Rare quad half-width. */
#define PORT_DOOR_HALF_W 90.0f
#define PORT_DOOR_HALF_T 15.0f
#define PORT_RAY_TMIN 0.05f
#define PORT_RAY_TMAX 4000.0f

/* list_of_tilesizes[] in stan.c — index is point count (N64 tail >> 12). */
static const uint8_t k_tile_bytes[16] = {
    0x20, 0x20, 0x20, 0x20, 0x28, 0x30, 0x38, 0x40,
    0x48, 0x50, 0x58, 0x00, 0x00, 0x00, 0x00, 0x00
};

typedef struct {
    int n;
    float x[PORT_STAN_MAX_PTS];
    float y[PORT_STAN_MAX_PTS];
    float z[PORT_STAN_MAX_PTS];
    uint16_t link[PORT_STAN_MAX_PTS];
    uint16_t rare; /* file offset / 8; Rare standTileStart[link] */
    int16_t nb[PORT_STAN_MAX_PTS]; /* resolved neighbor tile, or -1 */
    uint8_t room;
} StanTile;

typedef struct {
    float x, z, nx, nz, tx, tz;
    float half_w;
    float frac;
    int open;
    int side;
} StanDoor;

typedef struct {
    float x, z;
    int hit;
} StanGuard;

static StanTile g_tile[PORT_STAN_MAX_TILES];
static int g_ntile;
static StanDoor g_door[PORT_STAN_MAX_DOORS];
static int g_ndoor;
static StanGuard g_guard[PORT_STAN_MAX_GUARDS];
static int g_nguard;
static int g_ray_guard;
static float g_scale = 1.0f;
static float g_inv_scale = 1.0f;
static float g_ox, g_oy, g_oz;
/* Retail tiles store a neighbor in point.link; 0 means a wall edge.
 * Synthetic unit tiles leave link=0 on every edge — only honor walls
 * when the loaded mesh actually has at least one neighbor. */
static int g_have_links;
/* Last clip_step dest tile, keyed by world xz so a guard step cannot
 * steal the player's upstairs tile. Rare keeps current_tile_ptr per actor. */
#define PORT_CUR_CACHE 16
static struct {
    float x, z;
    int i;
} g_cur[PORT_CUR_CACHE];
static int g_ncur;
static int follow_clip;

static void cur_clear(void) { g_ncur = 0; }

void port_stan_clear_current(void) { cur_clear(); }

static void local_to_world(float lx, float lz, float *wx, float *wz);
static int hit_door_world(float wx, float wz);
static int find_use_door(float wx, float wz, float look_x, float look_z);

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
           p[3];
}

static int16_t be_s16(const uint8_t *p)
{
    return (int16_t)((uint16_t)((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t seg_to_off(uint32_t off)
{
    if ((off & 0xFF000000u) == BG_SEG_BASE)
        return (uint32_t)(off + BG_SEG_BIAS);
    return off;
}

void port_stan_unload(void)
{
    g_ntile = 0;
    g_ndoor = 0;
    g_nguard = 0;
    g_ray_guard = -1;
    g_scale = 1.0f;
    g_inv_scale = 1.0f;
    g_ox = g_oy = g_oz = 0.0f;
    g_have_links = 0;
    cur_clear();
}

void port_stan_set_scale(float scale)
{
    if (scale < 1e-6f)
        scale = 1.0f;
    g_scale = scale;
    g_inv_scale = 1.0f / scale;
}

void port_stan_set_world_origin(float x, float y, float z)
{
    g_ox = x;
    g_oy = y;
    g_oz = z;
}

void port_stan_clear_doors(void) { g_ndoor = 0; }

void port_stan_clear_guards(void)
{
    g_nguard = 0;
    g_ray_guard = -1;
}

void port_stan_add_guard(float world_x, float world_z)
{
    StanGuard *g;
    if (g_nguard >= PORT_STAN_MAX_GUARDS)
        return;
    g = &g_guard[g_nguard++];
    g->x = world_x;
    g->z = world_z;
    g->hit = 0;
}

void port_stan_move_guard(float from_x, float from_z, float to_x, float to_z)
{
    int i;
    for (i = 0; i < g_nguard; i++) {
        float dx = from_x - g_guard[i].x;
        float dz = from_z - g_guard[i].z;
        if (dx * dx + dz * dz <= 1.0f) {
            g_guard[i].x = to_x;
            g_guard[i].z = to_z;
            return;
        }
    }
}

int port_stan_guard_count(void) { return g_nguard; }

int port_stan_guard_was_hit(int i)
{
    if (i < 0 || i >= g_nguard)
        return 0;
    return g_guard[i].hit;
}

int port_stan_guard_xz(int i, float *x, float *z)
{
    if (i < 0 || i >= g_nguard)
        return -1;
    if (x)
        *x = g_guard[i].x;
    if (z)
        *z = g_guard[i].z;
    return 0;
}

int port_stan_ray_hit_guard(void) { return g_ray_guard >= 0; }

int port_stan_guard_dead_at(float world_x, float world_z)
{
    int i;
    for (i = 0; i < g_nguard; i++) {
        float dx = world_x - g_guard[i].x;
        float dz = world_z - g_guard[i].z;
        if (dx * dx + dz * dz <= 1.0f)
            return g_guard[i].hit;
    }
    return 0;
}

void port_stan_mark_ray_guard(void)
{
    if (g_ray_guard >= 0 && g_ray_guard < g_nguard)
        g_guard[g_ray_guard].hit = 1;
}

void port_stan_mark_guard_at(float world_x, float world_z)
{
    int i;
    for (i = 0; i < g_nguard; i++) {
        float dx = world_x - g_guard[i].x;
        float dz = world_z - g_guard[i].z;
        if (dx * dx + dz * dz <= 1.0f) {
            g_guard[i].hit = 1;
            return;
        }
    }
}

void port_stan_add_door(float world_x, float world_z, float look_x, float look_z)
{
    port_stan_add_door_w(world_x, world_z, look_x, look_z, 0.0f);
}

void port_stan_add_door_w(float world_x, float world_z, float look_x, float look_z,
                         float width)
{
    StanDoor *d;
    float len, inv, half_w;
    int i;
    len = sqrtf(look_x * look_x + look_z * look_z);
    if (len < 1e-4f)
        return;
    inv = 1.0f / len;
    half_w = (width > 1.0f) ? (0.5f * width) : PORT_DOOR_HALF_W;
    if (half_w < 1.0f)
        half_w = PORT_DOOR_HALF_W;
    /* Same xz as an existing pad/path door: keep one slab, take the wider
     * half so Z-unlatch cannot leave a second closed 90-wide blocker. */
    for (i = 0; i < g_ndoor; i++) {
        float dx = world_x - g_door[i].x;
        float dz = world_z - g_door[i].z;
        if (dx * dx + dz * dz <= 1.0f) {
            if (half_w > g_door[i].half_w)
                g_door[i].half_w = half_w;
            return;
        }
    }
    if (g_ndoor >= PORT_STAN_MAX_DOORS)
        return;
    d = &g_door[g_ndoor++];
    d->x = world_x;
    d->z = world_z;
    d->nx = look_x * inv;
    d->nz = look_z * inv;
    d->tx = -d->nz;
    d->tz = d->nx;
    d->half_w = half_w;
    d->open = 0;
    d->frac = 0.f;
    d->side = 0;
}

int port_stan_door_is_open(int i)
{
    if (i < 0 || i >= g_ndoor)
        return 0;
    return g_door[i].open;
}

float port_stan_door_frac(int i)
{
    if (i < 0 || i >= g_ndoor)
        return 0.f;
    return g_door[i].frac;
}

void port_stan_set_door_open(int i, int open)
{
    if (i < 0 || i >= g_ndoor)
        return;
    g_door[i].open = open ? 1 : 0;
    /* Forced open parks immediately (tests). Z-use animates from 0. */
    g_door[i].frac = open ? 1.f : 0.f;
}

int port_stan_door_is_open_at(float world_x, float world_z)
{
    int i;
    for (i = 0; i < g_ndoor; i++) {
        float dx = world_x - g_door[i].x;
        float dz = world_z - g_door[i].z;
        if (dx * dx + dz * dz <= 1.0f)
            return g_door[i].open;
    }
    return 0;
}

int port_stan_door_side_at(float world_x, float world_z)
{
    int i;
    for (i = 0; i < g_ndoor; i++) {
        float dx = world_x - g_door[i].x;
        float dz = world_z - g_door[i].z;
        if (dx * dx + dz * dz <= 1.0f)
            return g_door[i].side;
    }
    return 0;
}

float port_stan_door_frac_at(float world_x, float world_z)
{
    int i;
    for (i = 0; i < g_ndoor; i++) {
        float dx = world_x - g_door[i].x;
        float dz = world_z - g_door[i].z;
        if (dx * dx + dz * dz <= 1.0f)
            return g_door[i].frac;
    }
    return 0.f;
}

float port_stan_door_half_w_at(float world_x, float world_z)
{
    int i;
    for (i = 0; i < g_ndoor; i++) {
        float dx = world_x - g_door[i].x;
        float dz = world_z - g_door[i].z;
        if (dx * dx + dz * dz <= 1.0f)
            return g_door[i].half_w;
    }
    return PORT_DOOR_HALF_W;
}

int port_stan_push_cyl_off_doors(float world_x, float world_z, float radius,
                                 float *pdx, float *pdz)
{
    float ox = world_x, oz = world_z;
    float dx = 0.f, dz = 0.f;
    int iter;
    const float skin = 8.f;
    const float cap = 180.f;

    if (pdx)
        *pdx = 0.f;
    if (pdz)
        *pdz = 0.f;
    if (radius < 1.f)
        return 0;
    for (iter = 0; iter < 6; iter++) {
        int i, hit = 0;
        float best_need = 0.f, bnx = 0.f, bnz = 0.f;
        float cx = ox + dx, cz = oz + dz;
        for (i = 0; i < g_ndoor; i++) {
            const StanDoor *d = &g_door[i];
            float rx, rz, along, across, need, clear, pad_along, side;
            if (d->open || d->frac > 0.f)
                continue;
            rx = cx - d->x;
            rz = cz - d->z;
            along = rx * d->nx + rz * d->nz;
            across = rx * d->tx + rz * d->tz;
            if (across < 0.f)
                across = -across;
            if (across > d->half_w + radius)
                continue;
            clear = PORT_DOOR_HALF_T + radius + skin;
            if (along < 0.f) {
                if (-along >= clear)
                    continue;
            } else if (along >= clear)
                continue;
            pad_along = (ox - d->x) * d->nx + (oz - d->z) * d->nz;
            side = (pad_along >= 0.f) ? 1.f : -1.f;
            need = clear - side * along;
            if (need <= 0.f)
                continue;
            if (need > best_need) {
                best_need = need;
                bnx = d->nx * side;
                bnz = d->nz * side;
                hit = 1;
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

void port_stan_tick_doors(void)
{
    int i;
    const float step = 1.f / (float)PORT_DOOR_OPEN_TICKS;
    for (i = 0; i < g_ndoor; i++) {
        if (g_door[i].open) {
            g_door[i].frac += step;
            if (g_door[i].frac > 1.f)
                g_door[i].frac = 1.f;
        } else if (g_door[i].frac > 0.f) {
            /* Reverse-swing/slide. Spawn stays frac=0 (no auto-close). */
            g_door[i].frac -= step;
            if (g_door[i].frac < 1e-5f)
                g_door[i].frac = 0.f;
        }
    }
}

static int find_use_door(float wx, float wz, float look_x, float look_z)
{
    float nearest;
    int i, best;

    if (g_ndoor <= 0)
        return -1;
    nearest = PORT_DOOR_USE_RANGE * PORT_DOOR_USE_RANGE + 1.0f;
    best = -1;
    for (i = 0; i < g_ndoor; i++) {
        float dx = g_door[i].x - wx;
        float dz = g_door[i].z - wz;
        float dsq = dx * dx + dz * dz;
        float dist, face;
        if (dsq > PORT_DOOR_USE_RANGE * PORT_DOOR_USE_RANGE)
            continue;
        dist = sqrtf(dsq);
        if (dist < 1e-3f)
            face = 1.0f;
        else
            face = (dx * look_x + dz * look_z) / dist;
        if (face < 0.25f)
            continue;
        if (dsq < nearest) {
            nearest = dsq;
            best = i;
        }
    }
    return best;
}

int port_stan_closed_door_at_local(float local_x, float local_z)
{
    float wx, wz;
    local_to_world(local_x, local_z, &wx, &wz);
    return hit_door_world(wx, wz);
}

int port_stan_unlatch_closed(float local_x, float local_z, float look_x, float look_z)
{
    float wx, wz, llen, inv;
    int best;

    llen = sqrtf(look_x * look_x + look_z * look_z);
    if (llen < 1e-4f)
        return 0;
    inv = 1.0f / llen;
    look_x *= inv;
    look_z *= inv;
    local_to_world(local_x, local_z, &wx, &wz);
    best = find_use_door(wx, wz, look_x, look_z);
    if (best < 0 || g_door[best].open)
        return 0;
    return port_stan_use_door(local_x, local_z, look_x, look_z);
}

int port_stan_use_door(float local_x, float local_z, float look_x, float look_z)
{
    float wx, wz, llen, inv;
    int i, best;

    if (g_ndoor <= 0)
        return 0;
    llen = sqrtf(look_x * look_x + look_z * look_z);
    if (llen < 1e-4f)
        return 0;
    inv = 1.0f / llen;
    look_x *= inv;
    look_z *= inv;
    local_to_world(local_x, local_z, &wx, &wz);
    best = find_use_door(wx, wz, look_x, look_z);
    if (best < 0)
        return 0;
    if (!g_door[best].open) {
        float along = (wx - g_door[best].x) * g_door[best].nx +
                      (wz - g_door[best].z) * g_door[best].nz;
        g_door[best].side = (along >= 0.0f) ? 1 : -1;
    }
    {
        int open = !g_door[best].open;
        float bx = g_door[best].x, bz = g_door[best].z;
        int side = g_door[best].side;
        for (i = 0; i < g_ndoor; i++) {
            float dx = g_door[i].x - bx;
            float dz = g_door[i].z - bz;
            if (dx * dx + dz * dz > 1.0f)
                continue;
            g_door[i].open = open;
            g_door[i].side = side;
            /* Open: restart swing/slide from closed. Close: leave frac
             * so tick_doors reverse-swings over the same ticks. */
            if (open)
                g_door[i].frac = 0.f;
        }
    }
    return 1;
}

int port_stan_tile_count(void) { return g_ntile; }
int port_stan_door_count(void) { return g_ndoor; }
int port_stan_ready(void) { return g_ntile > 0 || g_ndoor > 0; }

static int parse_tiles(const uint8_t *s, size_t n, int pc_shift, uint32_t off_override)
{
    uint32_t off;
    size_t p;
    int tiles = 0;

    if (n < 12)
        return 0;
    if (off_override) {
        off = off_override;
    } else {
        off = seg_to_off(be32(s + 4));
        if (off < 8 || off + 8 > n)
            return 0;
    }
    if (off + 8 > n)
        return 0;
    p = (size_t)off;
    while (p + 8 <= n) {
        const uint8_t *t = s + p;
        uint16_t tail;
        int npc, i, nbytes;
        StanTile *dst;

        if (be32(t) == 0)
            break;
        tail = (uint16_t)be_s16(t + 6);
        npc = (int)((tail >> pc_shift) & 0xF);
        if (npc < 3)
            npc = 3;
        if (npc > PORT_STAN_MAX_PTS)
            break;
        nbytes = (int)k_tile_bytes[(tail >> 12) & 0xF];
        if (nbytes <= 0)
            nbytes = 8 + 8 * npc;
        if (p + (size_t)nbytes > n)
            break;
        if (tiles < PORT_STAN_MAX_TILES) {
            dst = &g_tile[tiles];
            dst->n = npc;
            for (i = 0; i < npc; i++) {
                const uint8_t *pt = t + 8 + (size_t)i * 8;
                dst->x[i] = (float)be_s16(pt + 0) * g_inv_scale;
                dst->y[i] = (float)be_s16(pt + 2) * g_inv_scale;
                dst->z[i] = (float)be_s16(pt + 4) * g_inv_scale;
                dst->link[i] = (uint16_t)(((uint16_t)pt[6] << 8) | pt[7]);
            }
            dst->room = t[3];
            dst->rare = (uint16_t)(p / 8);
            for (i = 0; i < PORT_STAN_MAX_PTS; i++)
                dst->nb[i] = -1;
        }
        tiles++;
        p += (size_t)nbytes;
    }
    return tiles;
}

int port_stan_load(const uint8_t *bytes, size_t n)
{
    int a;
    g_ntile = 0;
    if (!bytes || n < 12)
        return PORT_STAN_EMPTY;
    a = parse_tiles(bytes, n, 12, 0);
    if (a == 0 && n > 256)
        a = parse_tiles(bytes, n, 0, 0);
    /* Rare first tile is often file+0x80. A zero/bad pointer left us
     * reading the prefix (or nothing). */
    if (a == 0 && n > 0x88) {
        a = parse_tiles(bytes, n, 12, 0x80);
        if (a == 0)
            a = parse_tiles(bytes, n, 0, 0x80);
    }
    if (a > PORT_STAN_MAX_TILES) {
        /* File had more tiles than the table. Keep the first cap. */
        g_ntile = PORT_STAN_MAX_TILES;
    } else {
        g_ntile = a;
    }
    g_have_links = 0;
    {
        int i, k;
        for (i = 0; i < g_ntile && !g_have_links; i++) {
            for (k = 0; k < g_tile[i].n; k++) {
                if (g_tile[i].link[k] >> 4) {
                    g_have_links = 1;
                    break;
                }
            }
        }
    }
    cur_clear();
    if (a > 0) {
        int i, k, j;
        /* Rare standTileStart + (link<<3) lands on a point of the neighbor
         * (header or vertex), not always the tile header. */
        for (i = 0; i < g_ntile; i++) {
            for (k = 0; k < g_tile[i].n; k++) {
                uint16_t lk = g_tile[i].link[k];
                g_tile[i].nb[k] = -1;
                if (!(lk >> 4))
                    continue;
                for (j = 0; j < g_ntile; j++) {
                    int span = 1 + g_tile[j].n;
                    if (g_tile[j].rare <= lk && (int)lk < (int)g_tile[j].rare + span) {
                        g_tile[i].nb[k] = (int16_t)j;
                        break;
                    }
                }
            }
        }
    }
    return a > 0 ? PORT_STAN_OK : PORT_STAN_EMPTY;
}

float port_stan_max_xz(void)
{
    float m = 0.0f;
    int i, j;
    for (i = 0; i < g_ntile; i++) {
        for (j = 0; j < g_tile[i].n; j++) {
            float ax = g_tile[i].x[j];
            float az = g_tile[i].z[j];
            if (ax < 0.0f)
                ax = -ax;
            if (az < 0.0f)
                az = -az;
            if (ax > m)
                m = ax;
            if (az > m)
                m = az;
        }
    }
    return m;
}

static float tile_xz_twice_area(const StanTile *t);
static int finite_f(float v);
static const StanTile *tile_for_walk(float wx, float wz);

static int point_in_tile(const StanTile *t, float x, float z)
{
    int i, pos = 0, neg = 0;
    if (t->n < 3)
        return 0;
    /* Zero-area / collinear tiles match the whole xz plane (all cross
     * products ~0). That made intro-pad and stair samples hit a riser
     * whose (0,1,2) plane divided by zero and wrote NaN into player Y. */
    if (tile_xz_twice_area(t) < 1.0f)
        return 0;
    for (i = 0; i < t->n; i++) {
        int j = (i + 1 == t->n) ? 0 : i + 1;
        float ex = t->x[j] - t->x[i];
        float ez = t->z[j] - t->z[i];
        float cr = ex * (z - t->z[i]) - ez * (x - t->x[i]);
        if (cr > 1.0f)
            pos = 1;
        else if (cr < -1.0f)
            neg = 1;
    }
    return !(pos && neg);
}

/* NaN != NaN. Also drop Inf / exploded stair planes so look-at stays finite. */
static int finite_f(float v)
{
    return v == v && v < 1.0e20f && v > -1.0e20f;
}

static float tile_xz_twice_area(const StanTile *t)
{
    int i;
    float a = 0.0f;
    if (t->n < 3)
        return 0.0f;
    for (i = 0; i < t->n; i++) {
        int j = (i + 1 == t->n) ? 0 : i + 1;
        a += t->x[i] * t->z[j] - t->x[j] * t->z[i];
    }
    if (a < 0.0f)
        a = -a;
    return a;
}

static float tile_avg_y(const StanTile *t)
{
    int i;
    float s = 0.0f;
    if (t->n <= 0)
        return 0.0f;
    for (i = 0; i < t->n; i++) {
        if (!finite_f(t->y[i]))
            return 0.0f;
        s += t->y[i];
    }
    return s / (float)t->n;
}

/* Plane through i0,i1,i2. Returns |ny| if Y at (x,z) is usable, else 0. */
static float tile_tri_y(const StanTile *t, int i0, int i1, int i2, float x, float z,
                        float *y_out)
{
    float ax, ay, az, bx, by, bz, nx, ny, nz, y;
    ax = t->x[i1] - t->x[i0];
    ay = t->y[i1] - t->y[i0];
    az = t->z[i1] - t->z[i0];
    bx = t->x[i2] - t->x[i0];
    by = t->y[i2] - t->y[i0];
    bz = t->z[i2] - t->z[i0];
    nx = ay * bz - az * by;
    ny = az * bx - ax * bz;
    nz = ax * by - ay * bx;
    /* Stairs often list a near-vertical riser first; |ny|<1e-3 is a
     * divide-by-zero (NaN) or a huge plane that throws the camera. */
    if (ny > -1.0e-3f && ny < 1.0e-3f)
        return 0.0f;
    y = t->y[i0] - (nx * (x - t->x[i0]) + nz * (z - t->z[i0])) / ny;
    if (!finite_f(y))
        return 0.0f;
    if (y_out)
        *y_out = y;
    return (ny < 0.0f) ? -ny : ny;
}

static float tile_floor_y(const StanTile *t, float x, float z)
{
    float best_a = 0.0f, best_y, y, avg;
    int i, i1, i2;

    if (t->n < 3) {
        y = t->y[0];
        return finite_f(y) ? y : 0.0f;
    }
    best_y = t->y[0];
    /* Consecutive triples, then a fan from v0. A stair tile whose first
     * three points are a degenerate riser must still pick the tread. */
    for (i = 0; i < t->n; i++) {
        float a;
        i1 = (i + 1 == t->n) ? 0 : i + 1;
        i2 = (i + 2 >= t->n) ? (i + 2 - t->n) : i + 2;
        a = tile_tri_y(t, i, i1, i2, x, z, &y);
        if (a > best_a) {
            best_a = a;
            best_y = y;
        }
    }
    for (i = 1; i + 1 < t->n; i++) {
        float a = tile_tri_y(t, 0, i, i + 1, x, z, &y);
        if (a > best_a) {
            best_a = a;
            best_y = y;
        }
    }
    if (best_a > 0.0f && finite_f(best_y))
        return best_y;
    avg = tile_avg_y(t);
    return finite_f(avg) ? avg : 0.0f;
}

static const StanTile *tile_at_world(float wx, float wz)
{
    int i;
    const StanTile *best = NULL;
    float best_y = 1.0e30f;

    /* Overlapping floors: first-match picked a high walkway / wall
     * plane and wrote eye y=406 in the Facility bathroom. Prefer the
     * lowest finite floor at this xz. */
    for (i = 0; i < g_ntile; i++) {
        float fy;
        if (!point_in_tile(&g_tile[i], wx, wz))
            continue;
        fy = tile_floor_y(&g_tile[i], wx, wz);
        if (!finite_f(fy))
            continue;
        if (!best || fy < best_y) {
            best = &g_tile[i];
            best_y = fy;
        }
    }
    return best;
}

static int hit_door_world(float wx, float wz)
{
    int i;
    for (i = 0; i < g_ndoor; i++) {
        const StanDoor *d = &g_door[i];
        float rx = wx - d->x;
        float rz = wz - d->z;
        float along = rx * d->nx + rz * d->nz;
        float across = rx * d->tx + rz * d->tz;
        if (along < 0.0f)
            along = -along;
        if (across < 0.0f)
            across = -across;
        /* Walkable while opening or reverse-closing. Collision
         * returns only when frac hits 0 (spawn first frame). */
        if (d->open || d->frac > 0.f)
            continue;
        /* Strict along so a 3u chase step cannot land on the face
         * and then walk through via start_in_door. */
        if (along < PORT_DOOR_HALF_T && across <= d->half_w)
            return 1;
    }
    return 0;
}

static void local_to_world(float lx, float lz, float *wx, float *wz)
{
    *wx = lx + g_ox;
    *wz = lz + g_oz;
}

static int legal_world(float wx, float wz, int start_in_door)
{
    if (g_ntile > 0 && !tile_at_world(wx, wz))
        return 0;
    if (g_ndoor > 0 && hit_door_world(wx, wz) && !start_in_door)
        return 0;
    return 1;
}

int port_stan_on_tile(float local_x, float local_z)
{
    float wx, wz;
    if (g_ntile <= 0)
        return 0;
    local_to_world(local_x, local_z, &wx, &wz);
    return tile_at_world(wx, wz) != NULL;
}

int port_stan_tile_room(float local_x, float local_z)
{
    float wx, wz;
    const StanTile *t;
    if (g_ntile <= 0)
        return 0;
    local_to_world(local_x, local_z, &wx, &wz);
    t = tile_for_walk(wx, wz);
    if (!t || t->room < 1)
        return 0;
    return (int)t->room;
}

#define PORT_STAN_EYE_SLACK 150.0f

int port_stan_tile_room_at_eye(float local_x, float local_z, float eye_y)
{
    float wx, wz, want, best_d;
    const StanTile *best;
    int i;

    if (g_ntile <= 0 || !(eye_y == eye_y) || eye_y > 1.0e20f || eye_y < -1.0e20f)
        return port_stan_tile_room(local_x, local_z);
    local_to_world(local_x, local_z, &wx, &wz);
    want = (eye_y + g_oy) - PORT_EYE_HEIGHT;
    best = NULL;
    best_d = 1.0e30f;
    for (i = 0; i < g_ntile; i++) {
        float fy, d;
        if (!point_in_tile(&g_tile[i], wx, wz))
            continue;
        fy = tile_floor_y(&g_tile[i], wx, wz);
        if (!finite_f(fy))
            continue;
        d = fy - want;
        if (d < 0.0f)
            d = -d;
        if (d < best_d) {
            best_d = d;
            best = &g_tile[i];
        }
    }
    if (!best || best->room < 1 || best_d > PORT_STAN_EYE_SLACK)
        return port_stan_tile_room(local_x, local_z);
    return (int)best->room;
}

int port_stan_eye_y(float local_x, float local_z, float *y_out)
{
    float wx, wz, y;
    const StanTile *t;
    if (!y_out || g_ntile <= 0)
        return -1;
    local_to_world(local_x, local_z, &wx, &wz);
    t = tile_for_walk(wx, wz);
    if (!t)
        return -1;
    y = (tile_floor_y(t, wx, wz) + PORT_EYE_HEIGHT) - g_oy;
    if (!finite_f(y))
        return -1;
    *y_out = y;
    return 0;
}

int port_stan_nearest_eye_y(float local_x, float local_z, float max_dist, float *y_out)
{
    float wx, wz, best_d, lim, y;
    int i, best;

    if (!y_out || g_ntile <= 0)
        return -1;
    /* On-mesh: same lowest-floor tile as clip_step / tile_at_world.
     * Bathroom stacked xz used to pick the high walkway centroid
     * (eye 405.9) even though the player walks the low floor (86.8).
     * A linked upstairs tile with no low overlap still returns high. */
    if (port_stan_eye_y(local_x, local_z, y_out) == 0)
        return 0;
    if (max_dist < 0.0f)
        max_dist = 0.0f;
    local_to_world(local_x, local_z, &wx, &wz);
    lim = max_dist * max_dist;
    best_d = lim + 1.0f;
    best = -1;
    for (i = 0; i < g_ntile; i++) {
        const StanTile *t = &g_tile[i];
        float cx = 0.0f, cz = 0.0f, d2, fy;
        int k;
        if (t->n < 3 || tile_xz_twice_area(t) < 1.0f)
            continue;
        for (k = 0; k < t->n; k++) {
            cx += t->x[k];
            cz += t->z[k];
        }
        cx /= (float)t->n;
        cz /= (float)t->n;
        d2 = (cx - wx) * (cx - wx) + (cz - wz) * (cz - wz);
        if (d2 > lim)
            continue;
        fy = tile_avg_y(t);
        if (!finite_f(fy))
            continue;
        if (d2 < best_d) {
            best_d = d2;
            best = i;
        }
    }
    if (best < 0)
        return -1;
    y = (tile_avg_y(&g_tile[best]) + PORT_EYE_HEIGHT) - g_oy;
    if (!finite_f(y))
        return -1;
    *y_out = y;
    return 0;
}

static float dist2_point_seg(float px, float pz, float ax, float az, float bx, float bz,
                             float *ox, float *oz)
{
    float abx = bx - ax, abz = bz - az;
    float apx = px - ax, apz = pz - az;
    float ab2 = abx * abx + abz * abz;
    float t, dx, dz;
    if (ab2 < 1.0e-8f) {
        *ox = ax;
        *oz = az;
        return apx * apx + apz * apz;
    }
    t = (apx * abx + apz * abz) / ab2;
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    *ox = ax + t * abx;
    *oz = az + t * abz;
    dx = px - *ox;
    dz = pz - *oz;
    return dx * dx + dz * dz;
}

static float closest_on_tile(const StanTile *t, float px, float pz, float *ox, float *oz)
{
    float best, qx, qz;
    int k;
    if (point_in_tile(t, px, pz)) {
        *ox = px;
        *oz = pz;
        return 0.0f;
    }
    best = 1.0e30f;
    *ox = t->x[0];
    *oz = t->z[0];
    for (k = 0; k < t->n; k++) {
        int n = (k + 1 == t->n) ? 0 : k + 1;
        float d = dist2_point_seg(px, pz, t->x[k], t->z[k], t->x[n], t->z[n], &qx, &qz);
        if (d < best) {
            best = d;
            *ox = qx;
            *oz = qz;
        }
    }
    return best;
}

int port_stan_snap_walkable(float *local_x, float *local_z, float look_x, float look_z,
                            float max_dist, float *y_out)
{
    float wx, wz, ax, az, lim, min_y, best_d, sx, sz, y, llen;
    int i, best;

    if (!local_x || !local_z || !y_out || g_ntile <= 0)
        return -1;
    if (max_dist < 0.0f)
        max_dist = 0.0f;
    local_to_world(*local_x, *local_z, &wx, &wz);
    /* Aim along pad look so a pad past the catwalk lands in the hall it
     * faces, not on the stair landing behind. Zero look = pad xz. */
    ax = wx;
    az = wz;
    llen = look_x * look_x + look_z * look_z;
    if (llen > 1.0e-6f) {
        llen = sqrtf(llen);
        ax = wx + look_x * (PORT_STAN_SNAP_LOOK / llen);
        az = wz + look_z * (PORT_STAN_SNAP_LOOK / llen);
    }
    lim = max_dist * max_dist;
    min_y = 1.0e30f;
    for (i = 0; i < g_ntile; i++) {
        const StanTile *t = &g_tile[i];
        float ox, oz, d2, fy;
        if (t->n < 3 || tile_xz_twice_area(t) < 1.0f)
            continue;
        fy = tile_avg_y(t);
        if (!finite_f(fy))
            continue;
        d2 = closest_on_tile(t, wx, wz, &ox, &oz);
        if (d2 > lim)
            continue;
        if (fy < min_y)
            min_y = fy;
    }
    if (!(min_y < 1.0e29f))
        return -1;
    best_d = lim + 1.0f;
    best = -1;
    sx = wx;
    sz = wz;
    for (i = 0; i < g_ntile; i++) {
        const StanTile *t = &g_tile[i];
        float ox, oz, d2, fy, pad_d2;
        if (t->n < 3 || tile_xz_twice_area(t) < 1.0f)
            continue;
        fy = tile_avg_y(t);
        if (!finite_f(fy) || fy > min_y + PORT_STAN_FLOOR_SLACK)
            continue;
        pad_d2 = closest_on_tile(t, wx, wz, &ox, &oz);
        if (pad_d2 > lim)
            continue;
        d2 = closest_on_tile(t, ax, az, &ox, &oz);
        if (d2 < best_d) {
            best_d = d2;
            best = i;
            sx = ox;
            sz = oz;
        }
    }
    if (best < 0)
        return -1;
    /* Nudge 2 units toward the centroid so an edge snap stays on-tile. */
    {
        const StanTile *t = &g_tile[best];
        float cx = 0.0f, cz = 0.0f, dx, dz, len;
        int k;
        for (k = 0; k < t->n; k++) {
            cx += t->x[k];
            cz += t->z[k];
        }
        cx /= (float)t->n;
        cz /= (float)t->n;
        dx = cx - sx;
        dz = cz - sz;
        len = dx * dx + dz * dz;
        if (len > 1.0f) {
            len = sqrtf(len);
            sx += dx * (2.0f / len);
            sz += dz * (2.0f / len);
        }
    }
    y = (tile_floor_y(&g_tile[best], sx, sz) + PORT_EYE_HEIGHT) - g_oy;
    if (!finite_f(y))
        return -1;
    *local_x = sx - g_ox;
    *local_z = sz - g_oz;
    *y_out = y;
    return 0;
}

int port_stan_nearest_tile_room(float local_x, float local_z, float max_dist)
{
    float sx, sz, y;
    int rm;

    /* On-mesh: same lowest-floor tile as port_stan_tile_room. */
    rm = port_stan_tile_room(local_x, local_z);
    if (rm >= 1)
        return rm;
    /* Off-mesh: snap onto the low-band tile (not a high 12/14 walkway)
     * and read that tile's room. */
    sx = local_x;
    sz = local_z;
    if (port_stan_snap_walkable(&sx, &sz, 0.0f, 0.0f, max_dist, &y) != 0)
        return 0;
    return port_stan_tile_room(sx, sz);
}

/* Closed segments (ox,oz)->(nx,nz) vs tile edge (ax,az)->(bx,bz). */
static int seg_cross(float ax, float az, float bx, float bz, float cx, float cz,
                     float dx, float dz)
{
    float ex = bx - ax, ez = bz - az;
    float fx = dx - cx, fz = dz - cz;
    float den = ex * fz - ez * fx;
    float t, u;
    if (den > -1.0e-8f && den < 1.0e-8f)
        return 0;
    t = ((cx - ax) * fz - (cz - az) * fx) / den;
    u = ((cx - ax) * ez - (cz - az) * ex) / den;
    return t > 0.02f && t < 0.98f && u > 0.02f && u < 0.98f;
}

static const StanTile *cur_at(float wx, float wz)
{
    int i;
    for (i = 0; i < g_ncur; i++) {
        float dx, dz;
        int ti;
        dx = g_cur[i].x - wx;
        dz = g_cur[i].z - wz;
        if (dx * dx + dz * dz > 4.0f)
            continue;
        ti = g_cur[i].i;
        if (ti < 0 || ti >= g_ntile)
            continue;
        if (point_in_tile(&g_tile[ti], wx, wz))
            return &g_tile[ti];
    }
    return NULL;
}

static void cur_put(float wx, float wz, const StanTile *t)
{
    int i, ti;
    if (!t)
        return;
    ti = (int)(t - g_tile);
    if (ti < 0 || ti >= g_ntile)
        return;
    for (i = 0; i < g_ncur; i++) {
        float dx = g_cur[i].x - wx, dz = g_cur[i].z - wz;
        if (dx * dx + dz * dz <= 4.0f) {
            g_cur[i].x = wx;
            g_cur[i].z = wz;
            g_cur[i].i = ti;
            return;
        }
    }
    if (g_ncur < PORT_CUR_CACHE) {
        g_cur[g_ncur].x = wx;
        g_cur[g_ncur].z = wz;
        g_cur[g_ncur].i = ti;
        g_ncur++;
        return;
    }
    memmove(&g_cur[0], &g_cur[1], sizeof(g_cur[0]) * (PORT_CUR_CACHE - 1));
    g_cur[PORT_CUR_CACHE - 1].x = wx;
    g_cur[PORT_CUR_CACHE - 1].z = wz;
    g_cur[PORT_CUR_CACHE - 1].i = ti;
}

static const StanTile *tile_for_walk(float wx, float wz)
{
    const StanTile *t = cur_at(wx, wz);
    if (t)
        return t;
    return tile_at_world(wx, wz);
}

/* Follow Rare point.link from start along (ox,oz)->(dx,dz).
 * Dest still inside the current tile stays there (stacked bathroom / stair
 * foot keep the floor we are on). A linked neighbor is the stair step. */
static void tile_centroid(const StanTile *t, float *cx, float *cz)
{
    float x = 0.f, z = 0.f;
    int k;
    *cx = *cz = 0.f;
    if (!t || t->n < 1)
        return;
    for (k = 0; k < t->n; k++) {
        x += t->x[k];
        z += t->z[k];
    }
    *cx = x / (float)t->n;
    *cz = z / (float)t->n;
}

static float tile_centroid_dist(const StanTile *a, const StanTile *b)
{
    float ax, az, bx, bz, dx, dz;
    if (!a || !b || a->n < 1 || b->n < 1)
        return 0.f;
    tile_centroid(a, &ax, &az);
    tile_centroid(b, &bx, &bz);
    dx = ax - bx;
    dz = az - bz;
    return sqrtf(dx * dx + dz * dz);
}

/*
 * Facility lab door r20->r19 (and r19->r18) is a same-floor Rare
 * point.link whose polygons do not meet: unlinked north wall, ~50u
 * gap, neighbor centroid ~300-500u away. clip_step used to die on
 * that wall. Follow the link when the wanted step faces the neighbor.
 * Setup PROPDEF_DOOR pads sit in the gas-plant cluster (~9ku) and are
 * not this portal. Closed bound door slabs still block the landing.
 * Bathroom stacked xz has ~0 centroid delta so it does not hop.
 */
static const StanTile *cross_room_portal(const StanTile *t, float ox, float oz,
                                         float dx, float dz)
{
    const StanTile *best = NULL;
    float best_s = 0.25f;
    float vx = dx - ox, vz = dz - oz, vlen;
    int k;
    if (!t || !g_have_links)
        return NULL;
    vlen = sqrtf(vx * vx + vz * vz);
    if (vlen < 1e-4f)
        return NULL;
    vx /= vlen;
    vz /= vlen;
    for (k = 0; k < t->n; k++) {
        const StanTile *nb;
        float ncx, ncz, tx, tz, tlen, s;
        if (t->nb[k] < 0)
            continue;
        nb = &g_tile[t->nb[k]];
        if (nb == t || nb->room == t->room)
            continue;
        tile_centroid(nb, &ncx, &ncz);
        tx = ncx - ox;
        tz = ncz - oz;
        tlen = sqrtf(tx * tx + tz * tz);
        /* Same floor only: r7->r6 is a stacked 405.9 island. */
        if (tile_avg_y(nb) > tile_avg_y(t) + 40.0f ||
            tile_avg_y(nb) < tile_avg_y(t) - 40.0f)
            continue;
        if (tlen < 200.0f)
            continue;
        s = (tx * vx + tz * vz) / tlen;
        if (s > best_s) {
            best_s = s;
            best = nb;
        }
    }
    return best;
}

static int faces_centroid(const StanTile *nb, float ox, float oz, float dx, float dz)
{
    float ncx, ncz, vx, vz, tx, tz, vlen, tlen;
    if (!nb)
        return 0;
    tile_centroid(nb, &ncx, &ncz);
    vx = dx - ox;
    vz = dz - oz;
    vlen = sqrtf(vx * vx + vz * vz);
    tx = ncx - ox;
    tz = ncz - oz;
    tlen = sqrtf(tx * tx + tz * tz);
    if (vlen < 1e-4f || tlen < 1e-4f)
        return 0;
    return ((tx * vx + tz * vz) / (tlen * vlen)) > 0.25f;
}

/* Ignore a far rising portal unless the step faces it. Same-floor
 * walk toward the r3/r18 foot must not telehop r18->r15. */
static int take_link(const StanTile *from, const StanTile *nb, float ox, float oz,
                     float dx, float dz)
{
    if (!from || !nb || nb == from)
        return 0;
    if (tile_avg_y(nb) > tile_avg_y(from) + 200.0f &&
        tile_centroid_dist(from, nb) > 400.0f &&
        !faces_centroid(nb, ox, oz, dx, dz))
        return 0;
    return 1;
}

/*
 * Facility start stairs: r12 landing 2391 overlaps r71 ground 152 with
 * no Rare up-link (landing 2390 links down to r11). Dest xz enters the
 * high polygon; origin is not inside it. Bathroom stacked xz already
 * contains the high tile at the origin — skip so clip stays low.
 * Rise dump (avgY): stair landing 2391 is +319 over r71 152; spawn-hall
 * r12 2378 over r71 147 is +380 (not a Rare stair — walking that hall
 * must stay at eye 29). r13 catwalk is +650. Cap 350 keeps the foot
 * hop and blocks the hall +380 launch.
 * Dest still inside the from-tile (147 overlapping 2393) is hall walk,
 * not a stair step. The real foot leaves 152 onto 2391.
 */
#define PORT_RISE_MIN 80.0f
#define PORT_RISE_MAX 350.0f
#define PORT_RISE_XZ 300.0f

/* Facility start-stair foot (Chris / playtest PLAY_STAIR). */
static int near_stair_foot(float lx, float lz)
{
    float dx = lx + 571.8f, dz = lz + 2229.3f;
    return dx * dx + dz * dz < 120.0f * 120.0f;
}

static const StanTile *enter_rise_tile(const StanTile *from, float ox, float oz,
                                       float dx, float dz)
{
    const StanTile *best = NULL;
    float from_ay, best_ay = 1.0e30f;
    int i;

    if (!from || !g_have_links)
        return NULL;
    from_ay = tile_avg_y(from);
    for (i = 0; i < g_ntile; i++) {
        const StanTile *t = &g_tile[i];
        float ay, d;
        if (t == from || t->n < 3)
            continue;
        ay = tile_avg_y(t);
        if (ay < from_ay + PORT_RISE_MIN || ay > from_ay + PORT_RISE_MAX)
            continue;
        if (!point_in_tile(t, dx, dz))
            continue;
        if (point_in_tile(t, ox, oz))
            continue;
        /* Still on the low tile: overlapping r12 in the spawn hall. */
        if (point_in_tile(from, dx, dz))
            continue;
        /* Retail Facility: r71 overlaps r12 +319 all through the hall.
         * Only the stair foot may climb. 2–4 tile unit tests still hop. */
        if (g_ntile > 100 && from->room == 71 && t->room == 12 &&
            !near_stair_foot(ox - g_ox, oz - g_oz))
            continue;
        d = tile_centroid_dist(from, t);
        if (d > PORT_RISE_XZ)
            continue;
        if (ay < best_ay) {
            best_ay = ay;
            best = t;
        }
    }
    return best;
}

static const StanTile *walk_tiles(const StanTile *start, float ox, float oz,
                                  float dx, float dz)
{
    const StanTile *tile = start;
    const StanTile *prev = NULL;
    int iter;
    if (!tile)
        return NULL;
    for (iter = 0; iter < 128; iter++) {
        const StanTile *next = NULL;
        int k;
        /* Crossed a linked edge: take that neighbor even if dest is still
         * inside `tile` (Facility stair foot 2511->2516 is stacked). */
        for (k = 0; k < tile->n; k++) {
            int nn = (k + 1 == tile->n) ? 0 : k + 1;
            const StanTile *nb;
            if (tile->nb[k] < 0)
                continue;
            if (!seg_cross(tile->x[k], tile->z[k], tile->x[nn], tile->z[nn], ox, oz, dx, dz))
                continue;
            nb = &g_tile[tile->nb[k]];
            if (nb != tile && nb != prev && point_in_tile(nb, dx, dz))
                return nb;
            if (nb != tile && nb != prev && take_link(tile, nb, ox, oz, dx, dz))
                next = nb;
        }
        if (point_in_tile(tile, dx, dz))
            return tile;
        /* Rising Rare link whose polygon is far from this tile and does
         * not contain dest: Facility r18/r3 ground portals onto r15.
         * Only when the step faces that neighbor — a walk toward the
         * r3/r18 foot must not telehop upstairs and loop. Spatial stair
         * chains (unit A->B->C, r7->r6 stacked foot) stay on the graph.
         * Do not hop the r6 island (no Rare rise to r13/15). */
        if (next && tile_avg_y(next) > tile_avg_y(tile) + 200.0f &&
            tile_centroid_dist(tile, next) > 400.0f &&
            faces_centroid(next, ox, oz, dx, dz))
            return next;
        for (k = 0; k < tile->n && !next; k++) {
            int nn = (k + 1 == tile->n) ? 0 : k + 1;
            const StanTile *nb;
            if (!seg_cross(tile->x[k], tile->z[k], tile->x[nn], tile->z[nn], ox, oz, dx, dz))
                continue;
            if (tile->nb[k] < 0)
                return NULL;
            nb = &g_tile[tile->nb[k]];
            if (nb != tile && nb != prev && take_link(tile, nb, ox, oz, dx, dz))
                next = nb;
        }
        if (!next) {
            for (k = 0; k < tile->n; k++) {
                const StanTile *nb;
                if (tile->nb[k] < 0)
                    continue;
                nb = &g_tile[tile->nb[k]];
                if (nb != tile && point_in_tile(nb, dx, dz))
                    return nb;
            }
            return NULL;
        }
        prev = tile;
        tile = next;
    }
    return NULL;
}

/* Rare point.link >> 4 is a neighbor. Crossing an unlinked edge is a wall. */
static int step_hits_wall(float owx, float owz, float cwx, float cwz)
{
    const StanTile *t;
    int k;
    if (!g_have_links)
        return 0;
    t = follow_clip ? tile_for_walk(owx, owz) : tile_at_world(owx, owz);
    if (!t)
        return 0;
    for (k = 0; k < t->n; k++) {
        int n = (k + 1 == t->n) ? 0 : k + 1;
        if (t->link[k] >> 4)
            continue;
        if (seg_cross(t->x[k], t->z[k], t->x[n], t->z[n], owx, owz, cwx, cwz))
            return 1;
    }
    return 0;
}

static float dist_seg(float px, float pz, float ax, float az, float bx, float bz)
{
    float vx = bx - ax, vz = bz - az;
    float wx = px - ax, wz = pz - az;
    float vv = vx * vx + vz * vz;
    float t, dx, dz;
    t = (vv > 1e-8f) ? (wx * vx + wz * vz) / vv : 0.f;
    if (t < 0.f)
        t = 0.f;
    if (t > 1.f)
        t = 1.f;
    dx = ax + t * vx - px;
    dz = az + t * vz - pz;
    return sqrtf(dx * dx + dz * dz);
}

/* MoveBond collision_radius is 30. Visual G1 walls sit on unlinked
 * tile edges; 18 let the near plane eat those walls. */
#define PORT_WALL_SKIN 30.0f

static int dest_too_close_to_wall(float wx, float wz)
{
    const StanTile *t;
    int k, n;
    if (!g_have_links)
        return 0;
    t = follow_clip ? tile_for_walk(wx, wz) : tile_at_world(wx, wz);
    if (!t)
        return 0;
    for (k = 0; k < t->n; k++) {
        n = (k + 1 == t->n) ? 0 : k + 1;
        if (t->link[k] >> 4)
            continue;
        if (dist_seg(wx, wz, t->x[k], t->z[k], t->x[n], t->z[n]) < PORT_WALL_SKIN)
            return 1;
    }
    return 0;
}

/* Draw-only: G1 walls sit inside walkable tiles. Pull the camera off
 * unlinked edges by extra slack. clip_step / PORT_WALL_SKIN stay 30. */
#define PORT_DRAW_SKIN 46.0f

void port_stan_visual_xz(float lx, float lz, float *ox, float *oz)
{
    float wx, wz, cx = 0.f, cz = 0.f;
    const StanTile *t;
    int k, n, hits = 0;

    if (ox)
        *ox = lx;
    if (oz)
        *oz = lz;
    if (!g_have_links || g_ntile <= 0)
        return;
    local_to_world(lx, lz, &wx, &wz);
    t = tile_for_walk(wx, wz);
    if (!t)
        t = tile_at_world(wx, wz);
    if (!t || t->n < 3)
        return;
    for (k = 0; k < t->n; k++) {
        cx += t->x[k];
        cz += t->z[k];
    }
    cx /= (float)t->n;
    cz /= (float)t->n;
    for (k = 0; k < t->n; k++) {
        float d, need, dx, dz, len;
        n = (k + 1 == t->n) ? 0 : k + 1;
        if (t->link[k] >> 4)
            continue;
        d = dist_seg(wx, wz, t->x[k], t->z[k], t->x[n], t->z[n]);
        if (d >= PORT_DRAW_SKIN)
            continue;
        need = PORT_DRAW_SKIN - d;
        dx = cx - wx;
        dz = cz - wz;
        len = sqrtf(dx * dx + dz * dz);
        if (len < 0.1f)
            continue;
        wx += dx / len * need;
        wz += dz / len * need;
        hits++;
    }
    if (!hits)
        return;
    if (ox)
        *ox = wx - g_ox;
    if (oz)
        *oz = wz - g_oz;
}

static int legal_step(float owx, float owz, float cwx, float cwz, int start_door)
{
    if (!legal_world(cwx, cwz, start_door))
        return 0;
    if (step_hits_wall(owx, owz, cwx, cwz))
        return 0;
    if (dest_too_close_to_wall(cwx, cwz))
        return 0;
    return 1;
}

int port_stan_door_blocks_only(float ox, float oz, float nx, float nz)
{
    float cwx, cwz;

    (void)ox;
    (void)oz;
    if (g_ndoor <= 0)
        return 0;
    local_to_world(nx, nz, &cwx, &cwz);
    /* Dest in a closed slab is the door. The unlinked portal edge
     * under a bound door is that door, not a stall G1 wall. */
    return hit_door_world(cwx, cwz);
}

/* Hop landing sits past the 15u slab, so hit_door_world(landing) misses.
 * Refuse a same-floor hop that crosses a closed door plane. */
static int hop_hits_closed_door(float ox, float oz, float hx, float hz)
{
    int i;
    for (i = 0; i < g_ndoor; i++) {
        const StanDoor *d = &g_door[i];
        float a, b, mx, mz, across;
        if (d->open || d->frac > 0.f)
            continue;
        a = (ox - d->x) * d->nx + (oz - d->z) * d->nz;
        b = (hx - d->x) * d->nx + (hz - d->z) * d->nz;
        if (a * b > 0.f)
            continue;
        mx = 0.5f * (ox + hx) - d->x;
        mz = 0.5f * (oz + hz) - d->z;
        across = mx * d->tx + mz * d->tz;
        if (across < 0.f)
            across = -across;
        if (across <= d->half_w + 20.f)
            return 1;
    }
    return 0;
}

static int trapped_world(float wx, float wz)
{
    if (g_ntile > 0 && !tile_at_world(wx, wz))
        return 1;
    if (g_ndoor > 0 && hit_door_world(wx, wz))
        return 1;
    return 0;
}

static int has_walkable_neighbor(float wx, float wz, int start_door)
{
    const float d = 4.0f;
    if (legal_step(wx, wz, wx + d, wz, start_door))
        return 1;
    if (legal_step(wx, wz, wx - d, wz, start_door))
        return 1;
    if (legal_step(wx, wz, wx, wz + d, start_door))
        return 1;
    if (legal_step(wx, wz, wx, wz - d, start_door))
        return 1;
    return 0;
}

static int try_snap_local(float *lx, float *lz, float *ly)
{
    float x = *lx, z = *lz, y;
    /* Recover onto nearby floor (offtile_recover ~97u). Never 800u
     * across the map (Chris r11 dump). */
    if (port_stan_snap_walkable(&x, &z, 0.0f, 0.0f, 120.0f, &y) != 0)
        return 0;
    *lx = x;
    *lz = z;
    if (ly)
        *ly = y;
    return 1;
}


/* Landing tile of a rising portal may be a zero-area sliver (T2300).
 * Prefer a spacious neighbor at the same high floor. */
static const StanTile *rising_landing(const StanTile *t)
{
    const StanTile *best = t;
    float best_a, ay;
    int k;
    if (!t)
        return NULL;
    best_a = fabsf(tile_xz_twice_area(t));
    ay = tile_avg_y(t);
    if (best_a > 40.0f)
        return t;
    for (k = 0; k < t->n; k++) {
        const StanTile *nb;
        float a, by;
        if (t->nb[k] < 0)
            continue;
        nb = &g_tile[t->nb[k]];
        by = tile_avg_y(nb);
        if (by < ay - 20.0f)
            continue;
        /* Same floor as t. A same-floor door hop (r20->r19) must not
         * upgrade onto stacked r12 (eye 348, 300-500u snap). */
        if (by > ay + 40.0f)
            continue;
        a = fabsf(tile_xz_twice_area(nb));
        if (a > best_a) {
            best = nb;
            best_a = a;
        }
    }
    return best;
}

static float min_unlinked_edge(const StanTile *t, float wx, float wz)
{
    float best = 1.0e30f;
    int k, n;
    if (!t)
        return best;
    for (k = 0; k < t->n; k++) {
        float d;
        n = (k + 1 == t->n) ? 0 : k + 1;
        if (t->link[k] >> 4)
            continue;
        d = dist_seg(wx, wz, t->x[k], t->z[k], t->x[n], t->z[n]);
        if (d < best)
            best = d;
    }
    return best;
}

/*
 * Overlap hop lands on r12 2391 (two unlinked walls, 1.5u from the
 * edge — camera inside G1). Rare 2390 links same-floor to 2367, the
 * landing corridor. Pick a same-room same-floor tile near the rise
 * whose centroid is off unlinked edges and not over the from-tile.
 */
static const StanTile *landing_interior(const StanTile *t, const StanTile *from)
{
    const StanTile *best = t;
    float ay, tcx, tcz, best_s;
    int i;

    if (!t)
        return NULL;
    ay = tile_avg_y(t);
    tile_centroid(t, &tcx, &tcz);
    best_s = min_unlinked_edge(t, tcx, tcz);
    if (best_s > 200.0f)
        best_s = 200.0f;
    if (from && point_in_tile(from, tcx, tcz))
        best_s -= 400.0f;
    for (i = 0; i < g_ntile; i++) {
        const StanTile *nb = &g_tile[i];
        float nay, ncx, ncz, dx, dz, d, s, fdx, fdz;
        if (nb == t || nb->room != t->room || nb->n < 3)
            continue;
        nay = tile_avg_y(nb);
        if (nay < ay - 40.0f || nay > ay + 40.0f)
            continue;
        tile_centroid(nb, &ncx, &ncz);
        dx = ncx - tcx;
        dz = ncz - tcz;
        d = sqrtf(dx * dx + dz * dz);
        if (d > PORT_RISE_XZ)
            continue;
        s = min_unlinked_edge(nb, ncx, ncz);
        if (s > 200.0f)
            s = 200.0f;
        if (from && point_in_tile(from, ncx, ncz))
            s -= 400.0f;
        /* Do not walk back toward the from-tile (linked A->C test
         * snapped to B). Prefer farther from the ground overlap. */
        if (from) {
            float fcx, fcz, tdf;
            tile_centroid(from, &fcx, &fcz);
            fdx = ncx - fcx;
            fdz = ncz - fcz;
            tdf = sqrtf((tcx - fcx) * (tcx - fcx) + (tcz - fcz) * (tcz - fcz));
            if (sqrtf(fdx * fdx + fdz * fdz) < tdf)
                continue;
            s += 0.25f * sqrtf(fdx * fdx + fdz * fdz);
        }
        if (s > best_s) {
            best_s = s;
            best = nb;
        }
    }
    return best;
}

static void snap_to_centroid(const StanTile *t, float *wx, float *wz)
{
    float cx, cz;
    if (!t || !wx || !wz)
        return;
    tile_centroid(t, &cx, &cz);
    *wx = cx;
    *wz = cz;
}

static void snap_onto_tile(const StanTile *t, float *wx, float *wz)
{
    float cx = 0.0f, cz = 0.0f, ox, oz, vx, vz, len;
    int k;
    if (!t || t->n < 1 || !wx || !wz)
        return;
    for (k = 0; k < t->n; k++) {
        cx += t->x[k];
        cz += t->z[k];
    }
    cx /= (float)t->n;
    cz /= (float)t->n;
    if (point_in_tile(t, *wx, *wz))
        return;
    closest_on_tile(t, *wx, *wz, &ox, &oz);
    vx = cx - ox;
    vz = cz - oz;
    len = sqrtf(vx * vx + vz * vz);
    if (len > 1.0f) {
        ox += vx * (8.0f / len);
        oz += vz * (8.0f / len);
    } else {
        ox = cx;
        oz = cz;
    }
    *wx = ox;
    *wz = oz;
}

/* landing_interior may snap ~350u onto r12 2367 from the stair foot.
 * A 12u hall step that then snaps 300-500u onto r12 is a teleport.
 * Requested dest itself may be far (unit A→C 200u along Rare links). */
static int snap_ok(float ox, float oz, float snap_x, float snap_z, float want_x,
                   float want_z)
{
    float jx = snap_x - want_x, jz = snap_z - want_z;
    if (jx * jx + jz * jz <= 120.0f * 120.0f)
        return 1;
    return near_stair_foot(ox, oz);
}

/* Dest sitting only in stacked r12 would raise eye +319 on a 12u hall
 * step. Unit tests hop +319 on 2–4 tiles; Facility only at the foot.
 * Hall eye (~29) must not launch to overlapping r12 (~409). */
static int climb_ok(float ox, float oz, float from_y, float to_y)
{
    if (g_ntile > 100 && from_y < 120.0f && to_y > 200.0f &&
        !near_stair_foot(ox, oz))
        return 0;
    if (!(to_y > from_y + 80.0f))
        return 1;
    if (g_ntile <= 100)
        return 1;
    return near_stair_foot(ox, oz);
}

static void clip_step_ex(float ox, float oz, float *nx, float *nz, float *ny, int follow)
{
    float owx, owz, cwx, cwz;
    float cx, cz;
    int start_door;
    float ey;

    if (!nx || !nz)
        return;
    follow_clip = follow;
    cx = *nx;
    cz = *nz;
    if (g_ntile <= 0 && g_ndoor <= 0)
        return;

    local_to_world(ox, oz, &owx, &owz);
    start_door = hit_door_world(owx, owz);
    /* Wanted dest (before axis slide) for a same-floor door hop. */
    {
        float want_wx, want_wz;
        local_to_world(cx, cz, &want_wx, &want_wz);

    /* Off-tile or inside a closed door slab: do not stay stuck. */
    if (trapped_world(owx, owz)) {
        if (try_snap_local(&cx, &cz, ny)) {
            *nx = cx;
            *nz = cz;
            return;
        }
    }

    local_to_world(cx, cz, &cwx, &cwz);
    if (!legal_step(owx, owz, cwx, cwz, start_door)) {
        /* Prefer a same-floor Rare door portal over sliding along the
         * unlinked gap wall (Facility r20 lab -> r19). Axis slide would
         * burn the 400-step cap without getting closer. */
        if (follow && g_have_links && !start_door) {
            const StanTile *ot = tile_for_walk(owx, owz);
            const StanTile *hop = cross_room_portal(ot, owx, owz, want_wx, want_wz);
            if (hop && ot && hop->room != ot->room) {
                float hx = want_wx, hz = want_wz;
                hop = rising_landing(hop);
                if (hop && hop->room != ot->room) {
                    snap_onto_tile(hop, &hx, &hz);
                    if (!(g_ndoor > 0 &&
                          (hit_door_world(hx, hz) ||
                           hop_hits_closed_door(owx, owz, hx, hz))) &&
                        snap_ok(ox, oz, hx - g_ox, hz - g_oz, cx, cz)) {
                        cx = hx - g_ox;
                        cz = hz - g_oz;
                        *nx = cx;
                        *nz = cz;
                        if (ny) {
                            ey = (tile_floor_y(hop, hx, hz) + PORT_EYE_HEIGHT) - g_oy;
                            if (finite_f(ey))
                                *ny = ey;
                        }
                        if (follow)
                            cur_put(hx, hz, hop);
                        return;
                    }
                }
            }
        }
        local_to_world(cx, oz, &cwx, &cwz);
        if (legal_step(owx, owz, cwx, cwz, start_door)) {
            cz = oz;
        } else {
            local_to_world(ox, cz, &cwx, &cwz);
            if (legal_step(owx, owz, cwx, cwz, start_door)) {
                cx = ox;
            } else {
                cx = ox;
                cz = oz;
            }
        }
        *nx = cx;
        *nz = cz;
    }

    local_to_world(cx, cz, &cwx, &cwz);
    /* On a sliver / stall wall: every step died. Snap to open floor. */
    if (cx == ox && cz == oz && trapped_world(cwx, cwz) == 0 &&
        !has_walkable_neighbor(cwx, cwz, start_door)) {
        if (try_snap_local(&cx, &cz, ny)) {
            *nx = cx;
            *nz = cz;
            return;
        }
    }

    {
        float sdx = cx - ox, sdz = cz - oz;
        float slen = sqrtf(sdx * sdx + sdz * sdz);
        float tblk = 0.f, eyb = 0.f;
        if (slen > 0.25f && port_stan_eye_y(ox, oz, &eyb) == 0 &&
            port_stan_ray_block(ox, eyb, oz, sdx / slen, 0.f, sdz / slen, &tblk) &&
            tblk < slen + PORT_WALL_SKIN) {
            float stop = tblk - PORT_WALL_SKIN;
            if (stop < 0.f)
                stop = 0.f;
            cx = ox + sdx * (stop / slen);
            cz = oz + sdz * (stop / slen);
            *nx = cx;
            *nz = cz;
        }
    }

    if (ny) {
        float cwx2, cwz2, from_y = *ny;
        const StanTile *ot, *dt;
        local_to_world(cx, cz, &cwx2, &cwz2);
        ot = (follow && g_have_links) ? tile_for_walk(owx, owz) : NULL;
        /* Hall eye on overlapping r12 cache would keep y=409. Prefer the
         * lowest tile while the current eye is still on the ground floor. */
        if (follow && ot && from_y < 120.0f && g_ntile > 100) {
            const StanTile *lo = tile_at_world(owx, owz);
            if (lo && tile_avg_y(ot) > tile_avg_y(lo) + 80.0f)
                ot = lo;
        }
        dt = (follow && g_have_links && ot) ? walk_tiles(ot, owx, owz, cwx2, cwz2) : NULL;
        if (follow && ot) {
            const StanTile *rise = enter_rise_tile(ot, owx, owz, cwx2, cwz2);
            if (rise) {
                float hx, hz, want_lx = cx, want_lz = cz;
                dt = landing_interior(rise, ot);
                snap_to_centroid(dt, &cwx2, &cwz2);
                hx = cwx2 - g_ox;
                hz = cwz2 - g_oz;
                if (snap_ok(ox, oz, hx, hz, want_lx, want_lz)) {
                    cx = hx;
                    cz = hz;
                    *nx = cx;
                    *nz = cz;
                } else {
                    dt = NULL;
                }
            }
        }
        if (dt) {
            if (!point_in_tile(dt, cwx2, cwz2) &&
                ot && tile_avg_y(dt) > tile_avg_y(ot) + 40.0f) {
                float hx, hz, want_lx = cx, want_lz = cz;
                dt = rising_landing(dt);
                snap_onto_tile(dt, &cwx2, &cwz2);
                hx = cwx2 - g_ox;
                hz = cwz2 - g_oz;
                if (snap_ok(ox, oz, hx, hz, want_lx, want_lz)) {
                    cx = hx;
                    cz = hz;
                    *nx = cx;
                    *nz = cz;
                }
            }
            ey = (tile_floor_y(dt, cwx2, cwz2) + PORT_EYE_HEIGHT) - g_oy;
            if (finite_f(ey)) {
                if (!climb_ok(ox, oz, from_y, ey)) {
                    *nx = ox;
                    *nz = oz;
                    *ny = from_y;
                    return;
                }
                *ny = ey;
                cur_put(cwx2, cwz2, dt);
                return;
            }
        }
        if (port_stan_eye_y(cx, cz, &ey) == 0 && finite_f(ey)) {
            if (!climb_ok(ox, oz, from_y, ey)) {
                *nx = ox;
                *nz = oz;
                *ny = from_y;
                return;
            }
            *ny = ey;
        }
    }
    }
}

void port_stan_clip_step(float ox, float oz, float *nx, float *nz, float *ny)
{
    clip_step_ex(ox, oz, nx, nz, ny, 1);
}

void port_stan_clip_step_ground(float ox, float oz, float *nx, float *nz, float *ny)
{
    clip_step_ex(ox, oz, nx, nz, ny, 0);
}

static int tile_has_low_overlap(const StanTile *t)
{
    float cx = 0.0f, cz = 0.0f, ay, fy;
    const StanTile *lo;
    int k;
    if (!t || t->n < 1)
        return 0;
    for (k = 0; k < t->n; k++) {
        cx += t->x[k];
        cz += t->z[k];
    }
    cx /= (float)t->n;
    cz /= (float)t->n;
    ay = tile_avg_y(t);
    lo = tile_at_world(cx, cz);
    if (!lo)
        return 0;
    fy = tile_avg_y(lo);
    return fy < ay - 40.0f;
}

int port_stan_climb_along_links(float start_x, float start_z,
                                float *end_x, float *end_z, float *end_y,
                                int *end_room)
{
    int parent[PORT_STAN_MAX_TILES];
    unsigned char seen[PORT_STAN_MAX_TILES];
    int q[PORT_STAN_MAX_TILES];
    int path[256];
    int qh, qt, i, k, start, goal, npath, hop, p, high0;
    float wx, wz, x, z, y;
    const StanTile *lo;

    if (!end_x || !end_z || !end_y || !end_room || g_ntile <= 0)
        return -1;
    local_to_world(start_x, start_z, &wx, &wz);
    /* Honor stand_on_tile so a stacked ramp (T2374 over r71) can start high. */
    lo = tile_for_walk(wx, wz);
    if (!lo)
        lo = tile_at_world(wx, wz);
    if (!lo)
        return -1;
    start = (int)(lo - g_tile);
    cur_clear();
    cur_put(wx, wz, lo);

    /* Phase 1: first upward landing (eye>200), never walk down. */
    for (i = 0; i < g_ntile; i++) {
        seen[i] = 0;
        parent[i] = -1;
    }
    qh = qt = 0;
    q[qt++] = start;
    seen[start] = 1;
    high0 = -1;
    while (qh < qt) {
        const StanTile *tt;
        float e, ay;
        i = q[qh++];
        tt = &g_tile[i];
        ay = tile_avg_y(tt);
        e = (ay + PORT_EYE_HEIGHT) - g_oy;
        if (e > 200.0f) {
            high0 = i;
            break;
        }
        for (k = 0; k < tt->n; k++) {
            int nbi = tt->nb[k];
            float by;
            if (nbi < 0 || nbi >= g_ntile || seen[nbi])
                continue;
            by = tile_avg_y(&g_tile[nbi]);
            if (by < ay - 80.0f)
                continue;
            seen[nbi] = 1;
            parent[nbi] = i;
            q[qt++] = nbi;
        }
    }
    /* Phase 2: from the first high tile, stay on mid/upper (avgY > -200)
     * and hunt room 13. */
    goal = -1;
    if (high0 >= 0) {
        int parent2[PORT_STAN_MAX_TILES];
        for (i = 0; i < g_ntile; i++) {
            seen[i] = 0;
            parent2[i] = -1;
        }
        qh = qt = 0;
        q[qt++] = high0;
        seen[high0] = 1;
        while (qh < qt) {
            const StanTile *tt;
            i = q[qh++];
            tt = &g_tile[i];
            if (tt->room == 13 && ((tile_avg_y(tt) + PORT_EYE_HEIGHT) - g_oy) > 200.0f) {
                goal = i;
                break;
            }
            for (k = 0; k < tt->n; k++) {
                int nbi = tt->nb[k];
                float by;
                if (nbi < 0 || nbi >= g_ntile || seen[nbi])
                    continue;
                by = tile_avg_y(&g_tile[nbi]);
                if (by < -200.0f)
                    continue;
                seen[nbi] = 1;
                parent2[nbi] = i;
                q[qt++] = nbi;
            }
        }
        if (goal >= 0) {
            /* stitch start..high0 + high0..goal */
            int tail[256], nt = 0, head[256], nh = 0;
            for (p = goal; p >= 0 && nt < 256; p = parent2[p])
                tail[nt++] = p;
            for (p = high0; p >= 0 && nh < 256; p = parent[p])
                head[nh++] = p;
            npath = 0;
            for (i = nh - 1; i >= 0 && npath < 256; i--)
                path[npath++] = head[i];
            for (i = nt - 2; i >= 0 && npath < 256; i--)
                path[npath++] = tail[i];
        }
    }
    if (goal < 0) {
        if (high0 < 0)
            return -1;
        goal = high0;
        npath = 0;
        for (p = goal; p >= 0 && npath < 256; p = parent[p])
            path[npath++] = p;
        for (i = 0; i < npath / 2; i++) {
            int tmp = path[i];
            path[i] = path[npath - 1 - i];
            path[npath - 1 - i] = tmp;
        }
    }
    printf("climb_path n=%d start_tile=%d room=%u -> goal_tile=%d room=%u\n",
           npath, start, (unsigned)g_tile[start].room, goal,
           (unsigned)g_tile[goal].room);

    cur_put(wx, wz, &g_tile[start]);
    x = start_x;
    z = start_z;
    y = (tile_floor_y(&g_tile[start], wx, wz) + PORT_EYE_HEIGHT) - g_oy;
    for (i = 1; i < npath; i++) {
        const StanTile *from = &g_tile[path[i - 1]];
        const StanTile *nb = &g_tile[path[i]];
        float tcx = 0.0f, tcz = 0.0f, lx, lz, mx = 0.f, mz = 0.f;
        int edge = -1;
        for (k = 0; k < from->n; k++) {
            if (from->nb[k] == path[i]) {
                int nn = (k + 1 == from->n) ? 0 : k + 1;
                mx = 0.5f * (from->x[k] + from->x[nn]);
                mz = 0.5f * (from->z[k] + from->z[nn]);
                edge = k;
                break;
            }
        }
        for (k = 0; k < nb->n; k++) {
            tcx += nb->x[k];
            tcz += nb->z[k];
        }
        tcx /= (float)nb->n;
        tcz /= (float)nb->n;
        if (edge >= 0) {
            float vx = tcx - mx, vz = tcz - mz, len;
            len = sqrtf(vx * vx + vz * vz);
            if (len > 1.0f) {
                mx += vx * (8.0f / len);
                mz += vz * (8.0f / len);
            }
            lx = mx - g_ox;
            lz = mz - g_oz;
        } else {
            lx = tcx - g_ox;
            lz = tcz - g_oz;
        }
        for (hop = 0; hop < 48; hop++) {
            float ddx = lx - x, ddz = lz - z, dist, nx, nz, ny;
            dist = sqrtf(ddx * ddx + ddz * ddz);
            if (dist < 6.0f)
                break;
            if (dist > 12.0f) {
                ddx *= 12.0f / dist;
                ddz *= 12.0f / dist;
            }
            nx = x + ddx;
            nz = z + ddz;
            ny = y;
            port_stan_clip_step(x, z, &nx, &nz, &ny);
            if (!(ny == ny) || ny > 1.0e20f || ny < -1.0e20f)
                return -1;
            if (nx == x && nz == z)
                break;
            x = nx;
            z = nz;
            y = ny;
        }
        /* Then ease toward the neighbor centroid if we are on it. */
        lx = tcx - g_ox;
        lz = tcz - g_oz;
        for (hop = 0; hop < 24; hop++) {
            float ddx = lx - x, ddz = lz - z, dist, nx, nz, ny;
            dist = sqrtf(ddx * ddx + ddz * ddz);
            if (dist < 8.0f)
                break;
            if (dist > 12.0f) {
                ddx *= 12.0f / dist;
                ddz *= 12.0f / dist;
            }
            nx = x + ddx;
            nz = z + ddz;
            ny = y;
            port_stan_clip_step(x, z, &nx, &nz, &ny);
            if (!(ny == ny))
                return -1;
            if (nx == x && nz == z)
                break;
            x = nx;
            z = nz;
            y = ny;
        }
        printf("climb_hop %d->%d room=%u xz=%.1f,%.1f eye=%.1f\n",
               path[i - 1], path[i], (unsigned)nb->room, (double)x, (double)z,
               (double)y);
        if (y > 200.0f && (nb->room == 13 || i == npath - 1))
            break;
    }
    *end_x = x;
    *end_z = z;
    *end_y = y;
    *end_room = port_stan_tile_room(x, z);
    if (!(y == y) || y < 200.0f)
        return -1;
    return 0;
}

void port_stan_debug_at(float local_x, float local_z)
{
    float wx, wz, ey, sx, sz, sy;
    int i, n_hit = 0, n_near = 0;

    local_to_world(local_x, local_z, &wx, &wz);
    printf("stan_debug local=%.1f,%.1f world=%.1f,%.1f tiles=%d links=%d doors=%d on=%d\n",
           (double)local_x, (double)local_z, (double)wx, (double)wz, g_ntile,
           g_have_links, g_ndoor, tile_at_world(wx, wz) != NULL);
    if (port_stan_eye_y(local_x, local_z, &ey) == 0)
        printf("stan_debug eye_y=%.1f\n", (double)ey);
    else
        printf("stan_debug eye_y=off\n");
    {
        float ny;
        if (port_stan_nearest_eye_y(local_x, local_z, PORT_STAN_NEAR_XZ, &ny) == 0)
            printf("stan_debug nearest_eye_y=%.1f\n", (double)ny);
        else
            printf("stan_debug nearest_eye_y=off\n");
    }
    for (i = 0; i < g_ntile; i++) {
        const StanTile *t = &g_tile[i];
        float ox, oz, d2, fy, avg;
        int k, nlink = 0;
        if (t->n < 3)
            continue;
        d2 = closest_on_tile(t, wx, wz, &ox, &oz);
        if (!point_in_tile(t, wx, wz) && d2 > 80.0f * 80.0f)
            continue;
        fy = tile_floor_y(t, wx, wz);
        avg = tile_avg_y(t);
        for (k = 0; k < t->n; k++) {
            if (t->link[k] >> 4)
                nlink++;
        }
        printf("stan_tile[%d] hit=%d d=%.1f n=%d room=%u rare=0x%04x area=%.1f avgY=%.1f floorY=%.1f eye=%.1f links=%d/%d\n",
               i, point_in_tile(t, wx, wz), (double)sqrtf(d2), t->n,
               (unsigned)t->room, (unsigned)t->rare, (double)tile_xz_twice_area(t) * 0.5,
               (double)avg, (double)fy, (double)((fy + PORT_EYE_HEIGHT) - g_oy),
               nlink, t->n);
        for (k = 0; k < t->n; k++) {
            int nbi = t->nb[k];
            int nn = (k + 1 == t->n) ? 0 : k + 1;
            printf("  edge%d (%.1f,%.1f,%.1f)->(%.1f,%.1f,%.1f) link=0x%04x nb=%d",
                   k, (double)t->x[k], (double)t->y[k], (double)t->z[k],
                   (double)t->x[nn], (double)t->y[nn], (double)t->z[nn],
                   (unsigned)t->link[k], nbi);
            if (nbi >= 0)
                printf(" -> room=%u avgY=%.1f", (unsigned)g_tile[nbi].room,
                       (double)tile_avg_y(&g_tile[nbi]));
            printf("\n");
        }
        if (point_in_tile(t, wx, wz))
            n_hit++;
        else
            n_near++;
    }
    sx = local_x;
    sz = local_z;
    if (port_stan_snap_walkable(&sx, &sz, 0.f, 0.f, PORT_STAN_NEAR_XZ, &sy) == 0)
        printf("stan_debug snap xz=%.1f,%.1f y=%.1f hits=%d near=%d\n",
               (double)sx, (double)sz, (double)sy, n_hit, n_near);
    else
        printf("stan_debug snap=fail hits=%d near=%d\n", n_hit, n_near);
}


void port_stan_dump_tile_i(int i)
{
    const StanTile *t;
    float cx = 0.f, cz = 0.f, ay, ey;
    int k, nlink = 0;
    if (i < 0 || i >= g_ntile) {
        printf("dump_tile[%d] out (ntile=%d)\n", i, g_ntile);
        return;
    }
    t = &g_tile[i];
    for (k = 0; k < t->n; k++) {
        cx += t->x[k];
        cz += t->z[k];
        if (t->link[k] >> 4)
            nlink++;
    }
    if (t->n > 0) {
        cx /= (float)t->n;
        cz /= (float)t->n;
    }
    ay = tile_avg_y(t);
    ey = (ay + PORT_EYE_HEIGHT) - g_oy;
    printf("dump_tile[%d] room=%u rare=0x%04x n=%d links=%d/%d "
           "centroid_w=%.1f,%.1f local=%.1f,%.1f avgY=%.1f eye=%.1f\n",
           i, (unsigned)t->room, (unsigned)t->rare, t->n, nlink, t->n,
           (double)cx, (double)cz, (double)(cx - g_ox), (double)(cz - g_oz),
           (double)ay, (double)ey);
    for (k = 0; k < t->n; k++) {
        int nbi = t->nb[k];
        int nn = (k + 1 == t->n) ? 0 : k + 1;
        printf("  e%d (%.1f,%.1f)->(%.1f,%.1f) link=0x%04x nb=%d",
               k, (double)t->x[k], (double)t->z[k],
               (double)t->x[nn], (double)t->z[nn],
               (unsigned)t->link[k], nbi);
        if (nbi >= 0)
            printf(" -> r%u ay=%.1f", (unsigned)g_tile[nbi].room,
                   (double)tile_avg_y(&g_tile[nbi]));
        printf("\n");
    }
}

void port_stan_dump_rare(unsigned rare)
{
    int i, n = 0;
    printf("dump_rare 0x%04x (%u)\n", rare, rare);
    for (i = 0; i < g_ntile; i++) {
        if (g_tile[i].rare != (uint16_t)rare)
            continue;
        port_stan_dump_tile_i(i);
        n++;
    }
    if (!n)
        printf("dump_rare 0x%04x not found ntile=%d\n", rare, g_ntile);
}

void port_stan_dump_cross(unsigned from_room, unsigned to_room)
{
    int i, k, n = 0;
    printf("dump_cross r%u -> r%u ntile=%d\n", from_room, to_room, g_ntile);
    for (i = 0; i < g_ntile; i++) {
        if (from_room && g_tile[i].room != (uint8_t)from_room)
            continue;
        for (k = 0; k < g_tile[i].n; k++) {
            int nbi = g_tile[i].nb[k];
            if (nbi < 0)
                continue;
            if (to_room && g_tile[nbi].room != (uint8_t)to_room)
                continue;
            if (!to_room && g_tile[nbi].room == g_tile[i].room)
                continue;
            printf("  [%d] rare=0x%04x r%u ay=%.1f --e%d--> [%d] rare=0x%04x r%u ay=%.1f\n",
                   i, (unsigned)g_tile[i].rare, (unsigned)g_tile[i].room,
                   (double)tile_avg_y(&g_tile[i]), k, nbi,
                   (unsigned)g_tile[nbi].rare, (unsigned)g_tile[nbi].room,
                   (double)tile_avg_y(&g_tile[nbi]));
            n++;
        }
    }
    printf("dump_cross r%u -> r%u count=%d\n", from_room, to_room, n);
}

void port_stan_dump_stair_links(void)
{
    int i, k, n = 0;
    printf("stair_links ntile=%d (low->high, dest within 400 xz)\n", g_ntile);
    for (i = 0; i < g_ntile; i++) {
        const StanTile *t = &g_tile[i];
        float ay = tile_avg_y(t), tcx = 0.f, tcz = 0.f;
        int p;
        if (t->n < 3)
            continue;
        for (p = 0; p < t->n; p++) {
            tcx += t->x[p];
            tcz += t->z[p];
        }
        tcx /= (float)t->n;
        tcz /= (float)t->n;
        for (k = 0; k < t->n; k++) {
            int nbi = t->nb[k];
            const StanTile *nb;
            float by, ncx = 0.f, ncz = 0.f, dx, dz, d, miny, maxy;
            int q;
            if (nbi < 0)
                continue;
            nb = &g_tile[nbi];
            by = tile_avg_y(nb);
            if (!(ay < -200.f && by > ay + 80.f) && !(ay > -200.f && by > ay + 80.f))
                continue;
            for (q = 0; q < nb->n; q++) {
                ncx += nb->x[q];
                ncz += nb->z[q];
            }
            if (nb->n > 0) {
                ncx /= (float)nb->n;
                ncz /= (float)nb->n;
            }
            dx = ncx - tcx;
            dz = ncz - tcz;
            d = sqrtf(dx * dx + dz * dz);
            miny = maxy = t->y[0];
            for (q = 1; q < t->n; q++) {
                if (t->y[q] < miny)
                    miny = t->y[q];
                if (t->y[q] > maxy)
                    maxy = t->y[q];
            }
            printf("stair_link %d->%d r%u->r%u ay=%.1f->%.1f d=%.1f yspan=%.1f "
                   "from_w=%.1f,%.1f to_w=%.1f,%.1f from_l=%.1f,%.1f to_l=%.1f,%.1f%s\n",
                   i, nbi, (unsigned)t->room, (unsigned)nb->room,
                   (double)ay, (double)by, (double)d, (double)(maxy - miny),
                   (double)tcx, (double)tcz, (double)ncx, (double)ncz,
                   (double)(tcx - g_ox), (double)(tcz - g_oz),
                   (double)(ncx - g_ox), (double)(ncz - g_oz),
                   d < 400.f ? " NEAR" : " FAR");
            n++;
        }
    }
    printf("stair_links count=%d\n", n);
}

void port_stan_link_reach(float local_x, float local_z)
{
    float wx, wz;
    int i, qh, qt, found_high = 0, found_r13 = 0;
    unsigned char seen[PORT_STAN_MAX_TILES];
    int q[PORT_STAN_MAX_TILES];
    float hi_y = -1.0e30f;
    int hi_i = -1, seeds = 0;

    if (g_ntile <= 0 || g_ntile > PORT_STAN_MAX_TILES)
        return;
    local_to_world(local_x, local_z, &wx, &wz);
    memset(seen, 0, (size_t)g_ntile);
    qh = qt = 0;
    for (i = 0; i < g_ntile; i++) {
        if (!point_in_tile(&g_tile[i], wx, wz))
            continue;
        if (seen[i])
            continue;
        seen[i] = 1;
        q[qt++] = i;
        seeds++;
        printf("link_seed tile[%d] room=%u rare=0x%04x avgY=%.1f eye=%.1f\n",
               i, (unsigned)g_tile[i].room, (unsigned)g_tile[i].rare,
               (double)tile_avg_y(&g_tile[i]),
               (double)((tile_floor_y(&g_tile[i], wx, wz) + PORT_EYE_HEIGHT) - g_oy));
    }
    while (qh < qt) {
        const StanTile *t;
        int k;
        i = q[qh++];
        t = &g_tile[i];
        {
            float ay = tile_avg_y(t);
            float ey = (ay + PORT_EYE_HEIGHT) - g_oy;
            if (ey > hi_y) {
                hi_y = ey;
                hi_i = i;
            }
            if (ey > 200.0f)
                found_high++;
            if (t->room == 13)
                found_r13++;
        }
        for (k = 0; k < t->n; k++) {
            int nbi = t->nb[k];
            if (nbi < 0 || nbi >= g_ntile || seen[nbi])
                continue;
            seen[nbi] = 1;
            q[qt++] = nbi;
        }
    }
    printf("link_reach xz=%.1f,%.1f seeds=%d visited=%d high=%d room13=%d best_eye=%.1f tile=%d room=%u\n",
           (double)local_x, (double)local_z, seeds, qt, found_high, found_r13,
           (double)hi_y, hi_i, hi_i >= 0 ? (unsigned)g_tile[hi_i].room : 0u);
}

static int ray_aabb_1d(float o, float d, float lo, float hi, float *t0, float *t1)
{
    if (d > -1e-8f && d < 1e-8f) {
        if (o < lo || o > hi)
            return 0;
        return 1;
    }
    {
        float ta = (lo - o) / d;
        float tb = (hi - o) / d;
        if (ta > tb) {
            float tmp = ta;
            ta = tb;
            tb = tmp;
        }
        if (ta > *t0)
            *t0 = ta;
        if (tb < *t1)
            *t1 = tb;
        return *t0 <= *t1;
    }
}

static float object_floor_local(float wx, float wz)
{
    const StanTile *t = tile_at_world(wx, wz);
    if (!t)
        return 0.0f;
    return tile_floor_y(t, wx, wz) - g_oy;
}

static int door_ray_hit(float wx, float wy, float wz, float dx, float dy, float dz,
                        float *t_out)
{
    int i, hit = 0;
    float best = PORT_RAY_TMAX + 1.0f;

    for (i = 0; i < g_ndoor; i++) {
        const StanDoor *d = &g_door[i];
        float ox, oz, odx, odz, t0, t1;
        if (d->open || d->frac > 0.f)
            continue;
        ox = (wx - d->x) * d->nx + (wz - d->z) * d->nz;
        oz = (wx - d->x) * d->tx + (wz - d->z) * d->tz;
        odx = dx * d->nx + dz * d->nz;
        odz = dx * d->tx + dz * d->tz;
        t0 = PORT_RAY_TMIN;
        t1 = PORT_RAY_TMAX;
        if (!ray_aabb_1d(ox, odx, -PORT_DOOR_HALF_T, PORT_DOOR_HALF_T, &t0, &t1))
            continue;
        if (!ray_aabb_1d(oz, odz, -d->half_w, d->half_w, &t0, &t1))
            continue;
        {
            float floor = object_floor_local(d->x, d->z);
            if (!ray_aabb_1d(wy, dy, floor, floor + PORT_DOOR_HEIGHT, &t0, &t1))
                continue;
        }
        if (t0 < best) {
            best = t0;
            hit = 1;
        }
    }
    if (hit && t_out)
        *t_out = best;
    return hit;
}

/* First time a ray that starts on a tile leaves walkable tiles. */
static int tile_exit_hit(float wx, float wz, float dx, float dz, float *t_out)
{
    float lo, hi, mid;
    int i;

    if (g_ntile <= 0)
        return 0;
    if (!tile_at_world(wx + dx * PORT_RAY_TMIN, wz + dz * PORT_RAY_TMIN))
        return 0;
    if (tile_at_world(wx + dx * PORT_RAY_TMAX, wz + dz * PORT_RAY_TMAX))
        return 0;
    lo = PORT_RAY_TMIN;
    hi = PORT_RAY_TMAX;
    for (i = 0; i < 24; i++) {
        mid = 0.5f * (lo + hi);
        if (tile_at_world(wx + dx * mid, wz + dz * mid))
            lo = mid;
        else
            hi = mid;
    }
    if (t_out)
        *t_out = hi;
    return 1;
}

static int cyl_ray_hit(float ox, float oy, float oz, float dx, float dy, float dz,
                       float cx, float cz, float r, float y0, float y1, float *t_out)
{
    float fx, fz, a, b, c, disc, t, hy;

    fx = ox - cx;
    fz = oz - cz;
    a = dx * dx + dz * dz;
    if (a < 1.0e-12f)
        return 0;
    b = 2.0f * (fx * dx + fz * dz);
    c = fx * fx + fz * fz - r * r;
    disc = b * b - 4.0f * a * c;
    if (disc < 0.0f)
        return 0;
    t = (-b - sqrtf(disc)) / (2.0f * a);
    if (t < PORT_RAY_TMIN)
        t = (-b + sqrtf(disc)) / (2.0f * a);
    if (t < PORT_RAY_TMIN || t > PORT_RAY_TMAX)
        return 0;
    hy = oy + dy * t;
    if (hy < y0 || hy > y1)
        return 0;
    if (t_out)
        *t_out = t;
    return 1;
}

static int guard_ray_hit(float wx, float wy, float wz, float dx, float dy, float dz,
                         float *t_out, int *idx)
{
    int i, best_i = -1;
    float best = PORT_RAY_TMAX + 1.0f;

    for (i = 0; i < g_nguard; i++) {
        float t, floor;
        if (g_guard[i].hit)
            continue;
        floor = object_floor_local(g_guard[i].x, g_guard[i].z);
        if (!cyl_ray_hit(wx, wy, wz, dx, dy, dz, g_guard[i].x, g_guard[i].z,
                         PORT_GUARD_RADIUS, floor, floor + PORT_GUARD_HEIGHT, &t))
            continue;
        if (t < best) {
            best = t;
            best_i = i;
        }
    }
    if (best_i < 0)
        return 0;
    if (t_out)
        *t_out = best;
    if (idx)
        *idx = best_i;
    return 1;
}

int port_stan_ray_block(float local_x, float local_y, float local_z,
                        float dx, float dy, float dz, float *t_out)
{
    float wx, wz, t, best;
    int hit = 0;

    local_to_world(local_x, local_z, &wx, &wz);
    best = PORT_RAY_TMAX + 1.0f;
    if (door_ray_hit(wx, local_y, wz, dx, dy, dz, &t) && t < best) {
        best = t;
        hit = 1;
    }
    if (tile_exit_hit(wx, wz, dx, dz, &t) && t < best) {
        best = t;
        hit = 1;
    }
    if (!hit)
        return 0;
    if (t_out)
        *t_out = best;
    return 1;
}

int port_stan_ray_hit(float local_x, float local_y, float local_z,
                      float dx, float dy, float dz, float *t_out)
{
    float wx, wz, t, best;
    int hit = 0;
    int gi = -1;

    g_ray_guard = -1;
    local_to_world(local_x, local_z, &wx, &wz);
    best = PORT_RAY_TMAX + 1.0f;
    if (door_ray_hit(wx, local_y, wz, dx, dy, dz, &t) && t < best) {
        best = t;
        hit = 1;
        gi = -1;
    }
    if (tile_exit_hit(wx, wz, dx, dz, &t) && t < best) {
        best = t;
        hit = 1;
        gi = -1;
    }
    {
        int idx = -1;
        if (guard_ray_hit(wx, local_y, wz, dx, dy, dz, &t, &idx) && t < best) {
            best = t;
            hit = 1;
            gi = idx;
        }
    }
    if (!hit)
        return 0;
    g_ray_guard = gi;
    if (t_out)
        *t_out = best;
    return 1;
}

