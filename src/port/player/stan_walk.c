#include "stan_walk.h"

#include <math.h>
#include <string.h>

#define BG_SEG_BASE 0x0F000000u
#define BG_SEG_BIAS 0xF1000000u
/* Facility retail is 2599 tiles; 2048 dropped the intro walkway. */
#define PORT_STAN_MAX_TILES 8192
#define PORT_STAN_MAX_PTS 10
#define PORT_STAN_MAX_DOORS 128
#define PORT_STAN_MAX_GUARDS 128
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
} StanTile;

typedef struct {
    float x, z, nx, nz, tx, tz;
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

static void local_to_world(float lx, float lz, float *wx, float *wz);

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

int port_stan_guard_count(void) { return g_nguard; }

int port_stan_guard_was_hit(int i)
{
    if (i < 0 || i >= g_nguard)
        return 0;
    return g_guard[i].hit;
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

void port_stan_add_door(float world_x, float world_z, float look_x, float look_z)
{
    StanDoor *d;
    float len, inv;
    if (g_ndoor >= PORT_STAN_MAX_DOORS)
        return;
    len = sqrtf(look_x * look_x + look_z * look_z);
    if (len < 1e-4f)
        return;
    inv = 1.0f / len;
    d = &g_door[g_ndoor++];
    d->x = world_x;
    d->z = world_z;
    d->nx = look_x * inv;
    d->nz = look_z * inv;
    d->tx = -d->nz;
    d->tz = d->nx;
    d->open = 0;
    d->side = 0;
}

int port_stan_door_is_open(int i)
{
    if (i < 0 || i >= g_ndoor)
        return 0;
    return g_door[i].open;
}

void port_stan_set_door_open(int i, int open)
{
    if (i < 0 || i >= g_ndoor)
        return;
    g_door[i].open = open ? 1 : 0;
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

int port_stan_use_door(float local_x, float local_z, float look_x, float look_z)
{
    float wx, wz, llen, inv, nearest;
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
    if (best < 0)
        return 0;
    if (!g_door[best].open) {
        float along = (wx - g_door[best].x) * g_door[best].nx +
                      (wz - g_door[best].z) * g_door[best].nz;
        g_door[best].side = (along >= 0.0f) ? 1 : -1;
    }
    g_door[best].open = !g_door[best].open;
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
            }
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
    for (i = 0; i < g_ntile; i++) {
        if (point_in_tile(&g_tile[i], wx, wz))
            return &g_tile[i];
    }
    return NULL;
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
        if (d->open)
            continue;
        if (along <= PORT_DOOR_HALF_T && across <= PORT_DOOR_HALF_W)
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

int port_stan_eye_y(float local_x, float local_z, float *y_out)
{
    float wx, wz, y;
    const StanTile *t;
    if (!y_out || g_ntile <= 0)
        return -1;
    local_to_world(local_x, local_z, &wx, &wz);
    t = tile_at_world(wx, wz);
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

void port_stan_clip_step(float ox, float oz, float *nx, float *nz, float *ny)
{
    float owx, owz, cwx, cwz;
    float cx, cz;
    int start_door;
    float ey;

    if (!nx || !nz)
        return;
    cx = *nx;
    cz = *nz;
    if (g_ntile <= 0 && g_ndoor <= 0)
        return;

    local_to_world(ox, oz, &owx, &owz);
    start_door = hit_door_world(owx, owz);

    local_to_world(cx, cz, &cwx, &cwz);
    if (!legal_world(cwx, cwz, start_door)) {
        local_to_world(cx, oz, &cwx, &cwz);
        if (legal_world(cwx, cwz, start_door)) {
            cz = oz;
        } else {
            local_to_world(ox, cz, &cwx, &cwz);
            if (legal_world(cwx, cwz, start_door)) {
                cx = ox;
            } else {
                cx = ox;
                cz = oz;
            }
        }
        *nx = cx;
        *nz = cz;
    }
    if (ny && port_stan_eye_y(cx, cz, &ey) == 0 && finite_f(ey))
        *ny = ey;
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
        if (d->open)
            continue;
        ox = (wx - d->x) * d->nx + (wz - d->z) * d->nz;
        oz = (wx - d->x) * d->tx + (wz - d->z) * d->tz;
        odx = dx * d->nx + dz * d->nz;
        odz = dx * d->tx + dz * d->tz;
        t0 = PORT_RAY_TMIN;
        t1 = PORT_RAY_TMAX;
        if (!ray_aabb_1d(ox, odx, -PORT_DOOR_HALF_T, PORT_DOOR_HALF_T, &t0, &t1))
            continue;
        if (!ray_aabb_1d(oz, odz, -PORT_DOOR_HALF_W, PORT_DOOR_HALF_W, &t0, &t1))
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

