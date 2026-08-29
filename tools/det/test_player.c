#include <math.h>
#include <stdio.h>
#include <stdint.h>

#include "overrides/lv_clock.h"
#include "player/gun.h"
#include "player/move.h"
#include "player/stan_walk.h"
#include <string.h>
#include "vi/sim_tick.h"
#include "vi/tick_contract.h"

#include "game/frametiming.h"

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}


static void wr_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void wr_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void wr_s16(uint8_t *p, int v)
{
    wr_be16(p, (uint16_t)(int16_t)v);
}

/* One quad tile: x 0..400, z -50..50, floor y=50. First tile at 0x80. */
static void build_corridor_stan(uint8_t *s, size_t n)
{
    memset(s, 0, n);
    wr_be32(s + 4, 0x0F000080u);
    s[0x80 + 2] = 1;
    s[0x80 + 3] = 1;
    wr_be16(s + 0x80 + 6, (uint16_t)((4u << 12) | (0u << 8) | (1u << 4) | 2u));
    wr_s16(s + 0x88 + 0, 0);
    wr_s16(s + 0x88 + 2, 50);
    wr_s16(s + 0x88 + 4, -50);
    wr_s16(s + 0x90 + 0, 400);
    wr_s16(s + 0x90 + 2, 50);
    wr_s16(s + 0x90 + 4, -50);
    wr_s16(s + 0x98 + 0, 400);
    wr_s16(s + 0x98 + 2, 50);
    wr_s16(s + 0x98 + 4, 50);
    wr_s16(s + 0xA0 + 0, 0);
    wr_s16(s + 0xA0 + 2, 50);
    wr_s16(s + 0xA0 + 4, 50);
}

#define CHRIS_SC 1.20648f

static int sc16_pl(float world)
{
    return (int)(world * CHRIS_SC + (world >= 0.0f ? 0.5f : -0.5f));
}

static void build_chris_scale_stan(uint8_t *s, size_t n)
{
    memset(s, 0, n);
    wr_be32(s + 4, 0x0F000080u);
    s[0x80 + 2] = 1;
    s[0x80 + 3] = 1;
    wr_be16(s + 0x80 + 6, (uint16_t)((4u << 12) | (0u << 8) | (1u << 4) | 2u));
    wr_s16(s + 0x88 + 0, sc16_pl(57.0f));
    wr_s16(s + 0x88 + 2, sc16_pl(562.0f));
    wr_s16(s + 0x88 + 4, sc16_pl(-1234.0f));
    wr_s16(s + 0x90 + 0, sc16_pl(217.0f));
    wr_s16(s + 0x90 + 2, sc16_pl(562.0f));
    wr_s16(s + 0x90 + 4, sc16_pl(-1234.0f));
    wr_s16(s + 0x98 + 0, sc16_pl(217.0f));
    wr_s16(s + 0x98 + 2, sc16_pl(562.0f));
    wr_s16(s + 0x98 + 4, sc16_pl(-1074.0f));
    wr_s16(s + 0xA0 + 0, sc16_pl(57.0f));
    wr_s16(s + 0xA0 + 2, sc16_pl(562.0f));
    wr_s16(s + 0xA0 + 4, sc16_pl(-1074.0f));
}

static int test_stan_scale_chris_unit(void)
{
    uint8_t stan[256];
    float y, z1;
    uint32_t t;
    const float want_y = 562.0f + PORT_EYE_HEIGHT - 562.0f;

    port_stan_unload();
    build_chris_scale_stan(stan, sizeof stan);
    port_stan_set_scale(CHRIS_SC);
    port_stan_set_world_origin(497.0f, 562.0f, 1539.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("chris unit load");
    if (!port_stan_on_tile(-360.0f, -2693.0f))
        return fail("chris unit on tile");
    if (port_stan_eye_y(-360.0f, -2693.0f, &y) != 0)
        return fail("chris unit eye");
    if (fabsf(y - want_y) > 1.5f) {
        fprintf(stderr, "chris unit y=%g want %g\n", (double)y, (double)want_y);
        return 1;
    }
    /* Zero pointer: Rare 0x80 fallback. */
    wr_be32(stan + 4, 0);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("chris unit 0x80 fallback");
    if (!port_stan_on_tile(-360.0f, -2693.0f))
        return fail("chris unit 0x80 tile");

    port_player_spawn();
    port_player_set_pose(-360.0f, y, -2693.0f, 0.0f);
    port_set_local_pad(0, 0, (int8_t)70, 0);
    for (t = 0; t < 80; t++) {
        if (port_sim_tick(t) != 0)
            return fail("chris unit wall tick");
    }
    z1 = port_player_z();
    if (z1 > (-1074.0f - 1539.0f) + 1.5f) {
        fprintf(stderr, "chris unit wall z=%g\n", (double)z1);
        return fail("chris unit wall");
    }
    printf("stan_scale_chris_unit y=%.1f z_wall=%.1f tiles=%d\n", (double)port_player_y(),
           (double)z1, port_stan_tile_count());
    port_stan_unload();
    return 0;
}


/* Live Facility: room1 (-240,-337,2051), pad (137,562,-1154) → local
 * xz (377,-3205). A Y=0 tile at world pad + room1 origin writes
 * |room1.y|+175 = 512. Hallway tile at local xz, floor -88 → eye 87. */
#define LIVE_R1X (-240.0f)
#define LIVE_R1Y (-337.0f)
#define LIVE_R1Z 2051.0f
#define LIVE_PAD_X 137.0f
#define LIVE_PAD_Y 562.0f
#define LIVE_PAD_Z (-1154.0f)
#define LIVE_LX (LIVE_PAD_X - LIVE_R1X)
#define LIVE_LZ (LIVE_PAD_Z - LIVE_R1Z)
#define HALL_FLOOR (-88.0f)
#define HALL_EYE (HALL_FLOOR + PORT_EYE_HEIGHT)

static void wr_scaled_quad(uint8_t *s, size_t hdr, float x0, float x1, float y,
                           float z0, float z1)
{
    s[hdr + 2] = 1;
    s[hdr + 3] = 1;
    wr_be16(s + hdr + 6, (uint16_t)((4u << 12) | (0u << 8) | (1u << 4) | 2u));
    wr_s16(s + hdr + 8 + 0, sc16_pl(x0));
    wr_s16(s + hdr + 8 + 2, sc16_pl(y));
    wr_s16(s + hdr + 8 + 4, sc16_pl(z0));
    wr_s16(s + hdr + 8 + 8, sc16_pl(x1));
    wr_s16(s + hdr + 8 + 10, sc16_pl(y));
    wr_s16(s + hdr + 8 + 12, sc16_pl(z0));
    wr_s16(s + hdr + 8 + 16, sc16_pl(x1));
    wr_s16(s + hdr + 8 + 18, sc16_pl(y));
    wr_s16(s + hdr + 8 + 20, sc16_pl(z1));
    wr_s16(s + hdr + 8 + 24, sc16_pl(x0));
    wr_s16(s + hdr + 8 + 26, sc16_pl(y));
    wr_s16(s + hdr + 8 + 28, sc16_pl(z1));
}

static void build_live_hall_stan(uint8_t *s, size_t n, int with_decoy)
{
    memset(s, 0, n);
    wr_be32(s + 4, 0x0F000080u);
    /* Tile 0: hallway around intro local xz, floor -88. */
    wr_scaled_quad(s, 0x80, 300.0f, 460.0f, HALL_FLOOR, -3310.0f, -3100.0f);
    if (with_decoy) {
        /* Tile 1 at 0xA8: world pad xz, Y=0 — the 512 trap. */
        wr_scaled_quad(s, 0xA8, 57.0f, 217.0f, 0.0f, -1234.0f, -1074.0f);
    }
}

static int test_intro_spawn_y_hallway_unit(void)
{
    uint8_t stan[512];
    float y0, y1, yn, yoff;
    float pad_local_y = LIVE_PAD_Y - LIVE_R1Y;
    const float want = HALL_EYE;

    port_stan_unload();
    build_live_hall_stan(stan, sizeof stan, 1);
    port_stan_set_scale(CHRIS_SC);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("hall unit load");
    if (port_stan_tile_count() < 2)
        return fail("hall unit tiles");

    /* Why 512: room1 origin maps local xz to world pad, hits Y=0 decoy. */
    port_stan_set_world_origin(LIVE_R1X, LIVE_R1Y, LIVE_R1Z);
    if (port_stan_eye_y(LIVE_LX, LIVE_LZ, &y1) != 0)
        return fail("hall unit decoy eye");
    printf("why512 tile_floor@world_pad+room1 eye=%.1f |r1y|+175=%.1f pad_local+175=%.1f\n",
           (double)y1, (double)(-LIVE_R1Y + PORT_EYE_HEIGHT),
           (double)(pad_local_y + PORT_EYE_HEIGHT));
    if (fabsf(y1 - 512.0f) > 2.0f) {
        fprintf(stderr, "expected decoy 512 got %g\n", (double)y1);
        return fail("hall unit decoy not 512");
    }

    /* Room-local (origin 0) at intro xz is the hallway tile. */
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (!port_stan_on_tile(LIVE_LX, LIVE_LZ))
        return fail("hall unit on tile");
    if (port_stan_eye_y(LIVE_LX, LIVE_LZ, &y0) != 0)
        return fail("hall unit eye");
    if (fabsf(y0 - want) > 2.0f) {
        fprintf(stderr, "hall unit y=%g want %g\n", (double)y0, (double)want);
        return fail("hall unit y");
    }
    /* Same band as a nearby hallway sample. */
    if (port_stan_eye_y(400.0f, -3200.0f, &yn) != 0)
        return fail("hall unit nearby");
    if (fabsf(yn - y0) > 2.0f)
        return fail("hall unit nearby band");

    /* Pad just off the tile: nearest still hallway, not 512. */
    port_stan_unload();
    build_live_hall_stan(stan, sizeof stan, 1);
    port_stan_set_scale(CHRIS_SC);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("hall near load");
    if (port_stan_on_tile(LIVE_LX, -3400.0f))
        return fail("hall near should be off tile");
    if (port_stan_nearest_eye_y(LIVE_LX, -3400.0f, PORT_STAN_NEAR_XZ, &yoff) != 0)
        return fail("hall near miss");
    if (fabsf(yoff - want) > 2.0f) {
        fprintf(stderr, "hall near y=%g want %g\n", (double)yoff, (double)want);
        return fail("hall near y");
    }
    printf("intro_spawn_y_hallway_unit eye=%.1f nearby=%.1f nearest_off=%.1f (not 512)\n",
           (double)y0, (double)yn, (double)yoff);
    port_stan_unload();
    return 0;
}

/* Walkway closer than hallway, pad off both. Snap must prefer hall height. */
static void wr_unit_quad(uint8_t *s, size_t hdr, int x0, int x1, int y, int z0, int z1)
{
    s[hdr + 2] = 1;
    s[hdr + 3] = 1;
    wr_be16(s + hdr + 6, (uint16_t)((4u << 12) | (0u << 8) | (1u << 4) | 2u));
    wr_s16(s + hdr + 8 + 0, x0);
    wr_s16(s + hdr + 8 + 2, y);
    wr_s16(s + hdr + 8 + 4, z0);
    wr_s16(s + hdr + 8 + 8, x1);
    wr_s16(s + hdr + 8 + 10, y);
    wr_s16(s + hdr + 8 + 12, z0);
    wr_s16(s + hdr + 8 + 16, x1);
    wr_s16(s + hdr + 8 + 18, y);
    wr_s16(s + hdr + 8 + 20, z1);
    wr_s16(s + hdr + 8 + 24, x0);
    wr_s16(s + hdr + 8 + 26, y);
    wr_s16(s + hdr + 8 + 28, z1);
}

static void wr_quad_link(uint8_t *s, size_t hdr, int i, uint16_t link)
{
    s[hdr + 8 + (size_t)i * 8 + 6] = (uint8_t)(link >> 8);
    s[hdr + 8 + (size_t)i * 8 + 7] = (uint8_t)link;
}

/* Overlapping floors: lowest tile's room wins (Facility hall 71 over 14). */
static int test_tile_room_lowest_floor(void)
{
    uint8_t stan[512];
    float y;

    memset(stan, 0, sizeof stan);
    wr_be32(stan + 4, 0x0F000080u);
    wr_unit_quad(stan, 0x80, 0, 100, 225, 0, 100);
    stan[0x80 + 3] = 14;
    wr_unit_quad(stan, 0xA8, 0, 100, -88, 0, 100);
    stan[0xA8 + 3] = 71;
    port_stan_unload();
    port_stan_set_scale(1.0f);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("tile_room load");
    if (port_stan_tile_count() != 2)
        return fail("tile_room count");
    if (port_stan_tile_room(50.0f, 50.0f) != 71)
        return fail("tile_room lowest not 71");
    if (port_stan_eye_y(50.0f, 50.0f, &y) != 0)
        return fail("tile_room eye");
    if (fabsf(y - (-88.0f + PORT_EYE_HEIGHT)) > 1.0f)
        return fail("tile_room eye not low floor");
    /* Same stacked xz: nearest centroid used to pick the high walkway. */
    {
        float ny;
        if (port_stan_nearest_eye_y(50.0f, 50.0f, PORT_STAN_NEAR_XZ, &ny) != 0)
            return fail("tile_room nearest");
        if (fabsf(ny - y) > 1.0f)
            return fail("tile_room nearest not low floor");
    }
    printf("tile_room lowest room=%d eye=%.1f (high decoy 14/225 ignored)\n",
           port_stan_tile_room(50.0f, 50.0f), (double)y);
    port_stan_unload();
    return 0;
}

/* Same stacked xz: G1 current room follows the camera eye, not always
 * the lowest tile. Low eye stays 71; high eye (upper floor+175) is 14. */
static int test_tile_room_at_eye_stacked(void)
{
    uint8_t stan[512];
    float lo, hi;

    memset(stan, 0, sizeof stan);
    wr_be32(stan + 4, 0x0F000080u);
    wr_unit_quad(stan, 0x80, 0, 100, 225, 0, 100);
    stan[0x80 + 3] = 14;
    wr_unit_quad(stan, 0xA8, 0, 100, -88, 0, 100);
    stan[0xA8 + 3] = 71;
    port_stan_unload();
    port_stan_set_scale(1.0f);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("eye_room load");
    lo = -88.0f + PORT_EYE_HEIGHT;
    hi = 225.0f + PORT_EYE_HEIGHT;
    if (port_stan_tile_room_at_eye(50.0f, 50.0f, lo) != 71)
        return fail("eye_room low not 71");
    if (port_stan_tile_room_at_eye(50.0f, 50.0f, hi) != 14)
        return fail("eye_room high not 14");
    /* Lowest-floor pin is unchanged. */
    if (port_stan_tile_room(50.0f, 50.0f) != 71)
        return fail("eye_room lowest pin lost");
    printf("tile_room_at_eye low=%d high=%d (lowest still %d)\n",
           port_stan_tile_room_at_eye(50.0f, 50.0f, lo),
           port_stan_tile_room_at_eye(50.0f, 50.0f, hi),
           port_stan_tile_room(50.0f, 50.0f));
    port_stan_unload();
    return 0;
}

/* Linked upstairs tile with no low overlap must keep the high eye.
 * Do not flatten every Facility walkway to ground. */
static int test_nearest_eye_keeps_linked_upper(void)
{
    uint8_t stan[512];
    float nx, nz, ny, ey, near_y;

    memset(stan, 0, sizeof stan);
    wr_be32(stan + 4, 0x0F000080u);
    /* Low hall. East edge linked to the ramp. */
    wr_unit_quad(stan, 0x80, 0, 100, -88, 0, 100);
    stan[0x80 + 3] = 71;
    wr_quad_link(stan, 0x80, 1, 0x16);
    /* Upper landing, no stacked low tile at this xz. */
    wr_unit_quad(stan, 0xA8, 100, 200, 225, 0, 100);
    stan[0xA8 + 3] = 12;
    wr_quad_link(stan, 0xA8, 3, 0x11);
    port_stan_unload();
    port_stan_set_scale(1.0f);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("upper load");
    if (port_stan_tile_count() != 2)
        return fail("upper tiles");
    if (port_stan_tile_room(150.0f, 50.0f) != 12)
        return fail("upper room not 12");
    if (port_stan_eye_y(150.0f, 50.0f, &ey) != 0)
        return fail("upper eye");
    if (fabsf(ey - (225.0f + PORT_EYE_HEIGHT)) > 1.0f)
        return fail("upper eye not high");
    if (port_stan_nearest_eye_y(150.0f, 50.0f, PORT_STAN_NEAR_XZ, &near_y) != 0)
        return fail("upper nearest");
    if (fabsf(near_y - ey) > 1.0f)
        return fail("upper nearest flattened");
    nx = 150.0f;
    nz = 50.0f;
    ny = 0.0f;
    port_stan_clip_step(50.0f, 50.0f, &nx, &nz, &ny);
    if (nx < 140.0f)
        return fail("upper clip blocked");
    if (fabsf(ny - ey) > 1.0f)
        return fail("upper clip y flattened");
    printf("linked_upper eye=%.1f nearest=%.1f clip=%.1f,%.1f y=%.1f\n",
           (double)ey, (double)near_y, (double)nx, (double)nz, (double)ny);
    port_stan_unload();
    return 0;
}

/* Off-mesh pad nearer a high room-14 walkway: nearest_tile_room must
 * still take the low room-71 hall (same pin as snap_walkable). */
static int test_nearest_tile_room_prefers_hall(void)
{
    uint8_t stan[512];
    const float pad_x = 377.0f, pad_z = -3205.0f;

    memset(stan, 0, sizeof stan);
    wr_be32(stan + 4, 0x0F000080u);
    wr_unit_quad(stan, 0x80, 300, 460, 225, -3100, -2940);
    stan[0x80 + 3] = 14;
    wr_unit_quad(stan, 0xA8, 300, 460, -88, -2680, -2520);
    stan[0xA8 + 3] = 71;
    port_stan_unload();
    port_stan_set_scale(1.0f);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("near_room load");
    if (port_stan_tile_count() != 2)
        return fail("near_room tiles");
    if (port_stan_on_tile(pad_x, pad_z))
        return fail("near_room pad should be off tile");
    if (port_stan_tile_room(pad_x, pad_z) != 0)
        return fail("near_room on-tile should be 0");
    if (port_stan_nearest_tile_room(pad_x, pad_z, PORT_STAN_NEAR_XZ) != 71)
        return fail("near_room off-mesh not 71");
    if (port_stan_tile_room(380.0f, -2600.0f) != 71)
        return fail("near_room hall tile");
    if (port_stan_nearest_tile_room(380.0f, -2600.0f, PORT_STAN_NEAR_XZ) != 71)
        return fail("near_room hall nearest");
    printf("nearest_tile_room off-mesh pad -> 71 (high decoy 14 ignored)\n");
    port_stan_unload();
    return 0;
}

/* Adjacent tiles, shared edge unlinked: clip_step must not cross (chase
 * through a Facility stall G1). At least one other link enables walls. */
static int test_clip_unlinked_wall(void)
{
    uint8_t stan[512];
    float nx, nz, ny, lx, lz;

    memset(stan, 0, sizeof stan);
    wr_be32(stan + 4, 0x0F000080u);
    /* Tile 0: x 0..100. East edge (i=1) unlinked = wall. West linked. */
    wr_unit_quad(stan, 0x80, 0, 100, -88, 0, 100);
    wr_quad_link(stan, 0x80, 3, 0x10);
    /* Tile 1: x 100..200. West edge unlinked. */
    wr_unit_quad(stan, 0xA8, 100, 200, -88, 0, 100);
    wr_quad_link(stan, 0xA8, 1, 0x10);
    port_stan_unload();
    port_stan_set_scale(1.0f);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("wall link load");
    if (port_stan_tile_count() != 2)
        return fail("wall link tiles");
    nx = 150.0f;
    nz = 50.0f;
    ny = 0.0f;
    port_stan_clip_step(50.0f, 50.0f, &nx, &nz, &ny);
    if (nx > 100.5f) {
        fprintf(stderr, "unlinked wall leaked nx=%g\n", (double)nx);
        return fail("unlinked wall cross");
    }
    if (nx < 40.0f)
        return fail("unlinked wall snapped away");
    /* Linked path: mark the shared edge as a neighbor and recross. */
    wr_quad_link(stan, 0x80, 1, 0x16);
    wr_quad_link(stan, 0xA8, 3, 0x11);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("linked reload");
    nx = 150.0f;
    nz = 50.0f;
    port_stan_clip_step(50.0f, 50.0f, &nx, &nz, &ny);
    if (nx < 140.0f) {
        fprintf(stderr, "linked edge blocked nx=%g\n", (double)nx);
        return fail("linked edge should pass");
    }
    /* Off-tile / inside the wall: snap onto a walkable tile. */
    lx = 100.0f;
    lz = 200.0f;
    if (port_stan_on_tile(lx, lz))
        return fail("snap start should be off-tile");
    if (port_stan_snap_walkable(&lx, &lz, 0.f, 0.f, PORT_STAN_NEAR_XZ, &ny) != 0)
        return fail("wall snap miss");
    if (!port_stan_on_tile(lx, lz))
        return fail("wall snap still off");
    printf("clip_unlinked wall nx=blocked linked=%.1f snap=%.1f,%.1f\n",
           (double)nx, (double)lx, (double)lz);
    port_stan_unload();
    return 0;
}

static int test_snap_walkable_prefers_hall(void)
{
    uint8_t stan[512];
    float x, z, y, near_y;
    const float pad_x = 377.0f, pad_z = -3205.0f;
    const float want_y = -88.0f + PORT_EYE_HEIGHT;

    memset(stan, 0, sizeof stan);
    wr_be32(stan + 4, 0x0F000080u);
    /* Closer catwalk at floor 225. */
    wr_unit_quad(stan, 0x80, 300, 460, 225, -3100, -2940);
    /* Hallway further at floor -88. */
    wr_unit_quad(stan, 0xA8, 300, 460, -88, -2680, -2520);
    port_stan_unload();
    port_stan_set_scale(1.0f);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("snap load");
    if (port_stan_tile_count() != 2)
        return fail("snap tiles");
    if (port_stan_on_tile(pad_x, pad_z))
        return fail("snap pad should be off tile");
    if (port_stan_nearest_eye_y(pad_x, pad_z, PORT_STAN_NEAR_XZ, &near_y) != 0)
        return fail("snap nearest");
    if (fabsf(near_y - (225.0f + PORT_EYE_HEIGHT)) > 2.0f) {
        fprintf(stderr, "nearest should be walkway y=%g\n", (double)near_y);
        return fail("snap nearest not walkway");
    }
    x = pad_x;
    z = pad_z;
    if (port_stan_snap_walkable(&x, &z, 0.f, 0.f, PORT_STAN_NEAR_XZ, &y) != 0)
        return fail("snap miss");
    if (fabsf(y - want_y) > 2.0f) {
        fprintf(stderr, "snap y=%g want %g xz=%g,%g\n", (double)y, (double)want_y,
                (double)x, (double)z);
        return fail("snap y not hall");
    }
    if (!port_stan_on_tile(x, z))
        return fail("snap xz off tile");
    if (z > -2518.0f || z < -2682.0f || x < 298.0f || x > 462.0f) {
        fprintf(stderr, "snap xz=%g,%g not on hall\n", (double)x, (double)z);
        return fail("snap xz not hall");
    }
    printf("snap_walkable hall x=%.1f z=%.1f y=%.1f (nearest was %.1f)\n", (double)x,
           (double)z, (double)y, (double)near_y);
    port_stan_unload();
    return 0;
}


/* Off-tile (or inside a closed door) must snap onto the corridor, not stay stuck. */
static int test_offtile_recover(void)
{
    uint8_t stan[256];
    float y, x1, z1;
    uint32_t t;

    port_stan_unload();
    build_corridor_stan(stan, sizeof stan);
    port_stan_set_scale(1.0f);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("recover load");
    port_player_spawn();
    /* Corridor is x 0..400, z -50..50. Sit 80u west, off-mesh, high Y. */
    port_player_set_pose(-80.0f, 405.9f, 0.0f, 90.0f);
    if (port_stan_on_tile(port_player_x(), port_player_z()))
        return fail("recover start should be off-tile");
    port_set_local_pad(0, 0, 0, 0);
    if (port_sim_tick(0) != 0)
        return fail("recover tick");
    x1 = port_player_x();
    z1 = port_player_z();
    y = port_player_y();
    if (!port_stan_on_tile(x1, z1))
        return fail("recover still off-tile");
    if (y > 250.0f)
        return fail("recover y still high");
    if (fabsf(y - (50.0f + PORT_EYE_HEIGHT)) > 1.0f) {
        fprintf(stderr, "recover y=%g want 225\n", (double)y);
        return fail("recover eye y");
    }
    /* Must be able to walk after the snap. */
    port_player_set_pose(x1, y, z1, 90.0f);
    port_set_local_pad(0, 0, (int8_t)-70, 0);
    for (t = 1; t < 8; t++) {
        if (port_sim_tick(t) != 0)
            return fail("recover walk tick");
    }
    if (!(port_player_x() > x1 + 8.0f))
        return fail("recover cannot walk");
    printf("offtile_recover xz=%.1f,%.1f y=%.1f -> x=%.1f\n", (double)x1, (double)z1,
           (double)y, (double)port_player_x());
    return 0;
}

static int test_stan_eye_and_clip(void)
{
    uint8_t stan[256];
    float y, x0, z0, x1, z1;
    uint32_t t;
    const float want_y = 50.0f + PORT_EYE_HEIGHT;

    port_stan_unload();
    build_corridor_stan(stan, sizeof stan);
    port_stan_set_scale(1.0f);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("stan load tiles");
    if (port_stan_tile_count() != 1)
        return fail("stan tile count");
    if (!port_stan_on_tile(200.0f, 0.0f))
        return fail("spawn on tile");
    if (port_stan_on_tile(200.0f, 80.0f))
        return fail("outside tile should be empty");
    if (port_stan_eye_y(200.0f, 0.0f, &y) != 0)
        return fail("eye y");
    if (fabsf(y - want_y) > 0.05f) {
        fprintf(stderr, "eye y=%g want %g\n", (double)y, (double)want_y);
        return 1;
    }

    port_player_spawn();
    port_player_set_pose(200.0f, y, 0.0f, 90.0f);
    if (fabsf(port_player_y() - want_y) > 0.05f)
        return fail("spawn eye y");

    /* theta=90, stick up walks +X along the corridor. */
    x0 = port_player_x();
    port_set_local_pad(0, 0, (int8_t)-70, 0);
    for (t = 0; t < 20; t++) {
        if (port_sim_tick(t) != 0)
            return fail("corridor tick");
    }
    x1 = port_player_x();
    if (!(x1 > x0 + 40.0f) || x1 > 400.0f) {
        fprintf(stderr, "corridor x0=%g x1=%g\n", (double)x0, (double)x1);
        return fail("corridor step");
    }
    if (fabsf(port_player_y() - want_y) > 0.5f)
        return fail("corridor eye y");

    /* Reset at centre, walk +Z into the stan edge. */
    port_player_set_pose(200.0f, y, 0.0f, 0.0f);
    z0 = port_player_z();
    port_set_local_pad(0, 0, (int8_t)70, 0);
    for (t = 0; t < 80; t++) {
        if (port_sim_tick(200 + t) != 0)
            return fail("wall tick");
    }
    z1 = port_player_z();
    if (z1 > 50.0f) {
        fprintf(stderr, "wall leaked z=%g\n", (double)z1);
        return fail("wall clip");
    }
    if (!(z1 > z0) || z1 > 50.0f) {
        /* may sit on the edge; must not pass it */
    }
    if (z1 > 50.01f)
        return fail("wall z");
    /* Unconstrained 80 ticks would be ~240. */
    if (z1 > 55.0f)
        return fail("wall almost through");
    printf("stan_eye y=%.1f floor=50 eye=175 tiles=%d\n", (double)port_player_y(),
           port_stan_tile_count());
    printf("stan_corridor x0=%.1f x1=%.1f\n", (double)x0, (double)x1);
    printf("stan_wall z0=%.1f z1=%.1f (edge=50)\n", (double)z0, (double)z1);

    /* Closed door slab at x=300, look +X. Walk +X must stop short of 300. */
    port_stan_clear_doors();
    port_stan_add_door(300.0f, 0.0f, 1.0f, 0.0f);
    port_player_set_pose(200.0f, y, 0.0f, 90.0f);
    port_set_local_pad(0, 0, (int8_t)-70, 0);
    for (t = 0; t < 80; t++) {
        if (port_sim_tick(400 + t) != 0)
            return fail("door tick");
    }
    x1 = port_player_x();
    if (x1 >= 300.0f - 15.0f + 0.5f) {
        fprintf(stderr, "door leaked x=%g\n", (double)x1);
        return fail("door slab");
    }
    if (!(x1 > 200.0f))
        return fail("door approach");
    printf("stan_door x=%.1f (slab=300 half_t=15)\n", (double)x1);
    port_stan_unload();
    return 0;
}

static int test_door_use_open(void)
{
    uint8_t stan[256];
    float y, x1;
    uint32_t t;
    const float want_y = 50.0f + PORT_EYE_HEIGHT;

    (void)want_y;
    port_stan_unload();
    build_corridor_stan(stan, sizeof stan);
    port_stan_set_scale(1.0f);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("use stan load");
    port_stan_clear_doors();
    port_stan_add_door(300.0f, 0.0f, 1.0f, 0.0f);
    if (port_stan_door_count() != 1 || port_stan_door_is_open(0))
        return fail("use door starts closed");

    port_player_spawn();
    if (port_stan_eye_y(200.0f, 0.0f, &y) != 0)
        return fail("use eye");

    /* Closed slab still blocks the same +X walk. */
    port_player_set_pose(200.0f, y, 0.0f, 90.0f);
    port_set_local_pad(0, 0, (int8_t)-70, 0);
    for (t = 0; t < 80; t++) {
        if (port_sim_tick(600 + t) != 0)
            return fail("use closed tick");
    }
    x1 = port_player_x();
    if (x1 >= 300.0f - 15.0f + 0.5f) {
        fprintf(stderr, "use closed leaked x=%g\n", (double)x1);
        return fail("use closed still blocks");
    }
    printf("door_use closed x=%.1f\n", (double)x1);

    /* Facing away must not open. */
    port_player_set_pose(250.0f, y, 0.0f, 270.0f);
    port_set_local_pad(0, 0, 0, 0);
    if (port_sim_tick(700) != 0)
        return fail("use away idle");
    port_set_local_pad(0, 0, 0, 0x2000); /* PORT_Z_TRIG */
    if (port_sim_tick(701) != 0)
        return fail("use away z");
    if (port_stan_door_is_open(0))
        return fail("use behind opened");

    /* Face +X and press Z. */
    port_player_set_pose(250.0f, y, 0.0f, 90.0f);
    port_set_local_pad(0, 0, 0, 0);
    if (port_sim_tick(702) != 0)
        return fail("use face idle");
    port_set_local_pad(0, 0, 0, 0x2000);
    if (port_sim_tick(703) != 0)
        return fail("use face z");
    if (!port_stan_door_is_open(0))
        return fail("use did not open");
    {
        float f = port_stan_door_frac_at(300.0f, 0.0f);
        if (f <= 0.f || f > 1.f)
            return fail("use frac not started");
    }

    port_player_set_pose(200.0f, y, 0.0f, 90.0f);
    port_set_local_pad(0, 0, (int8_t)-70, 0);
    for (t = 0; t < 80; t++) {
        if (port_sim_tick(710 + t) != 0)
            return fail("use open tick");
    }
    x1 = port_player_x();
    if (x1 <= 300.0f) {
        fprintf(stderr, "open door blocked x=%g\n", (double)x1);
        return fail("use open walk");
    }
    printf("door_use open x=%.1f\n", (double)x1);

    /* Second Z closes. */
    port_player_set_pose(250.0f, y, 0.0f, 90.0f);
    port_set_local_pad(0, 0, 0, 0);
    if (port_sim_tick(800) != 0)
        return fail("use close idle");
    port_set_local_pad(0, 0, 0, 0x2000);
    if (port_sim_tick(801) != 0)
        return fail("use close z");
    if (port_stan_door_is_open(0))
        return fail("use did not close");
    {
        float f = port_stan_door_frac_at(300.0f, 0.0f);
        if (f <= 0.15f || f >= 0.99f)
            return fail("use close frac not mid");
    }

    port_player_set_pose(200.0f, y, 0.0f, 90.0f);
    port_set_local_pad(0, 0, (int8_t)-70, 0);
    for (t = 0; t < 80; t++) {
        if (port_sim_tick(810 + t) != 0)
            return fail("use reclose tick");
    }
    x1 = port_player_x();
    if (x1 >= 300.0f - 15.0f + 0.5f) {
        fprintf(stderr, "reclose leaked x=%g\n", (double)x1);
        return fail("use reclose blocks");
    }
    printf("door_use reclosed x=%.1f\n", (double)x1);
    port_stan_unload();
    return 0;
}

/* Chase uses clip_step_ground: closed slab blocks, unlatch opens, next step passes. */
static int test_clip_ground_door(void)
{
    uint8_t stan[256];
    float nx, nz, ny;

    port_stan_unload();
    build_corridor_stan(stan, sizeof stan);
    port_stan_set_scale(1.0f);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("ground door stan");
    port_stan_clear_doors();
    port_stan_add_door(300.0f, 0.0f, 1.0f, 0.0f);
    if (port_stan_door_count() != 1 || port_stan_door_is_open(0))
        return fail("ground door starts closed");

    nx = 310.0f;
    nz = 0.0f;
    ny = 0.0f;
    port_stan_clip_step_ground(280.0f, 0.0f, &nx, &nz, &ny);
    if (nx >= 300.0f - 15.0f + 0.5f)
        return fail("ground clip leaked closed");
    if (!port_stan_closed_door_at_local(300.0f, 0.0f))
        return fail("closed_door_at_local miss");
    if (port_stan_unlatch_closed(250.0f, 0.0f, -1.0f, 0.0f))
        return fail("unlatch behind");
    if (!port_stan_unlatch_closed(250.0f, 0.0f, 1.0f, 0.0f))
        return fail("unlatch facing");
    if (!port_stan_door_is_open(0))
        return fail("unlatch did not open");
    if (port_stan_unlatch_closed(250.0f, 0.0f, 1.0f, 0.0f))
        return fail("unlatch closed an open door");

    nx = 330.0f;
    nz = 0.0f;
    ny = 0.0f;
    port_stan_clip_step_ground(280.0f, 0.0f, &nx, &nz, &ny);
    if (nx <= 300.0f)
        return fail("ground clip blocked open");
    printf("clip_ground_door closed_block open_pass nx=%.1f\n", (double)nx);
    port_stan_unload();
    return 0;
}

/* Same rising Z that uses a door must not spend a PP7 shot. */
static int test_door_use_does_not_fire(void)
{
    uint8_t stan[256];
    float y;
    int mag0, hits0;

    port_stan_unload();
    build_corridor_stan(stan, sizeof stan);
    port_stan_set_scale(1.0f);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("nofire stan load");
    port_stan_clear_doors();
    port_stan_add_door(300.0f, 0.0f, 1.0f, 0.0f);

    port_player_spawn();
    if (port_stan_eye_y(250.0f, 0.0f, &y) != 0)
        return fail("nofire eye");

    /* Facing door in range: Z opens, mag/hits stay put. */
    port_player_set_pose(250.0f, y, 0.0f, 90.0f);
    port_set_local_pad(0, 0, 0, 0);
    if (port_sim_tick(900) != 0)
        return fail("nofire idle");
    mag0 = port_gun_mag();
    hits0 = port_gun_hits();
    port_set_local_pad(0, 0, 0, (int)PORT_Z_TRIG);
    if (port_sim_tick(901) != 0)
        return fail("nofire use z");
    if (!port_stan_door_is_open(0))
        return fail("nofire did not open");
    if (port_gun_mag() != mag0)
        return fail("nofire use spent mag");
    if (port_gun_hits() != hits0)
        return fail("nofire use scored hit");

    /* Close is also a use. */
    port_set_local_pad(0, 0, 0, 0);
    if (port_sim_tick(902) != 0)
        return fail("nofire close idle");
    port_set_local_pad(0, 0, 0, (int)PORT_Z_TRIG);
    if (port_sim_tick(903) != 0)
        return fail("nofire close z");
    if (port_stan_door_is_open(0))
        return fail("nofire did not close");
    if (port_gun_mag() != mag0)
        return fail("nofire close spent mag");
    if (port_gun_hits() != hits0)
        return fail("nofire close scored hit");

    /* Facing away: not a use, so Z still fires (mag, not necessarily a wall). */
    port_player_set_pose(250.0f, y, 0.0f, 270.0f);
    port_set_local_pad(0, 0, 0, 0);
    if (port_sim_tick(904) != 0)
        return fail("nofire away idle");
    port_set_local_pad(0, 0, 0, (int)PORT_Z_TRIG);
    if (port_sim_tick(905) != 0)
        return fail("nofire away z");
    if (port_stan_door_is_open(0))
        return fail("nofire away opened");
    if (port_gun_mag() != mag0 - 1)
        return fail("nofire away did not fire");

    /* No door in range, face -Z: corridor tile ends at z=-50, so the
     * ray still records a hit (real tile edge, not the no_assets wall).
     * Spawn resets mag/hits so a glancing away-shot cannot pollute this. */
    port_stan_clear_doors();
    port_player_spawn();
    port_player_set_pose(0.0f, 0.0f, 0.0f, 0.0f);
    port_set_local_pad(0, 0, 0, 0);
    if (port_sim_tick(906) != 0)
        return fail("nofire fire idle");
    port_set_local_pad(0, 0, 0, (int)PORT_Z_TRIG);
    if (port_sim_tick(907) != 0)
        return fail("nofire fire z");
    if (port_gun_mag() != PORT_PP7_MAG - 1)
        return fail("nofire no-door did not fire");
    if (port_gun_hits() != 1)
        return fail("nofire no-door did not hit");

    printf("door_use nofire open/close mag=%d hits=%d; away mag=%d; shot mag=%d hits=%d\n",
           mag0, hits0, mag0 - 1, port_gun_mag(), port_gun_hits());
    port_stan_unload();
    return 0;
}



/* Zero-area line tile (collinear xz) plus a good corridor. Walking onto
 * the sliver must keep a finite Y — old (0,1,2)/ny wrote NaN. */
static void build_degen_pair_stan(uint8_t *s, size_t n)
{
    memset(s, 0, n);
    wr_be32(s + 4, 0x0F000080u);
    /* Tile 0: good corridor x 0..200, z -50..50, y=50. */
    s[0x80 + 2] = 1;
    s[0x80 + 3] = 1;
    wr_be16(s + 0x80 + 6, (uint16_t)((4u << 12) | (0u << 8) | (1u << 4) | 2u));
    wr_s16(s + 0x88 + 0, 0);
    wr_s16(s + 0x88 + 2, 50);
    wr_s16(s + 0x88 + 4, -50);
    wr_s16(s + 0x90 + 0, 200);
    wr_s16(s + 0x90 + 2, 50);
    wr_s16(s + 0x90 + 4, -50);
    wr_s16(s + 0x98 + 0, 200);
    wr_s16(s + 0x98 + 2, 50);
    wr_s16(s + 0x98 + 4, 50);
    wr_s16(s + 0xA0 + 0, 0);
    wr_s16(s + 0xA0 + 2, 50);
    wr_s16(s + 0xA0 + 4, 50);
    /* Tile 1 at 0x80+0x28=0xA8: zero-area line x=200, y=50, z -50..50.
     * Three collinear points. Must not own the plane or NaN Y. */
    s[0xA8 + 2] = 1;
    s[0xA8 + 3] = 1;
    wr_be16(s + 0xA8 + 6, (uint16_t)((3u << 12) | (0u << 8) | (1u << 4) | 2u));
    wr_s16(s + 0xB0 + 0, 200);
    wr_s16(s + 0xB0 + 2, 50);
    wr_s16(s + 0xB0 + 4, -50);
    wr_s16(s + 0xB8 + 0, 200);
    wr_s16(s + 0xB8 + 2, 50);
    wr_s16(s + 0xB8 + 4, 0);
    wr_s16(s + 0xC0 + 0, 200);
    wr_s16(s + 0xC0 + 2, 50);
    wr_s16(s + 0xC0 + 4, 50);
}

/* Stair-like sloped quad: y=50 at x=0 -> y=150 at x=400, z -50..50.
 * Plus a 6-pt tile whose first three points are a near-vertical riser
 * (the Chris NaN case) sitting on the same footprint. */
static void build_stair_stan(uint8_t *s, size_t n)
{
    memset(s, 0, n);
    wr_be32(s + 4, 0x0F000080u);
    /* Tile 0: clean ramp. */
    s[0x80 + 2] = 1;
    s[0x80 + 3] = 1;
    wr_be16(s + 0x80 + 6, (uint16_t)((4u << 12) | (0u << 8) | (1u << 4) | 2u));
    wr_s16(s + 0x88 + 0, 0);
    wr_s16(s + 0x88 + 2, 50);
    wr_s16(s + 0x88 + 4, -50);
    wr_s16(s + 0x90 + 0, 400);
    wr_s16(s + 0x90 + 2, 150);
    wr_s16(s + 0x90 + 4, -50);
    wr_s16(s + 0x98 + 0, 400);
    wr_s16(s + 0x98 + 2, 150);
    wr_s16(s + 0x98 + 4, 50);
    wr_s16(s + 0xA0 + 0, 0);
    wr_s16(s + 0xA0 + 2, 50);
    wr_s16(s + 0xA0 + 4, 50);
    /* Tile 1 at 0xA8: same quad but first 3 pts are a riser (ny~0). */
    s[0xA8 + 2] = 1;
    s[0xA8 + 3] = 1;
    wr_be16(s + 0xA8 + 6, (uint16_t)((6u << 12) | (0u << 8) | (1u << 4) | 2u));
    wr_s16(s + 0xB0 + 0, 0);
    wr_s16(s + 0xB0 + 2, 50);
    wr_s16(s + 0xB0 + 4, -50);
    wr_s16(s + 0xB8 + 0, 0);
    wr_s16(s + 0xB8 + 2, 150);
    wr_s16(s + 0xB8 + 4, -50);
    wr_s16(s + 0xC0 + 0, 1);
    wr_s16(s + 0xC0 + 2, 150);
    wr_s16(s + 0xC0 + 4, -49);
    wr_s16(s + 0xC8 + 0, 400);
    wr_s16(s + 0xC8 + 2, 150);
    wr_s16(s + 0xC8 + 4, -50);
    wr_s16(s + 0xD0 + 0, 400);
    wr_s16(s + 0xD0 + 2, 150);
    wr_s16(s + 0xD0 + 4, 50);
    wr_s16(s + 0xD8 + 0, 0);
    wr_s16(s + 0xD8 + 2, 50);
    wr_s16(s + 0xD8 + 4, 50);
}

static int y_finite(float y)
{
    return y == y && y < 1.0e20f && y > -1.0e20f;
}

static int test_stan_degen_y_finite(void)
{
    uint8_t stan[512];
    float y0, y1;
    uint32_t t;

    port_stan_unload();
    build_degen_pair_stan(stan, sizeof stan);
    port_stan_set_scale(1.0f);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("degen load");
    if (port_stan_tile_count() < 1)
        return fail("degen tiles");
    /* Line tile must not own xz. Good corridor still does. */
    if (!port_stan_on_tile(100.0f, 0.0f))
        return fail("degen good tile");
    if (port_stan_eye_y(100.0f, 0.0f, &y0) != 0 || !y_finite(y0))
        return fail("degen good y");
    port_player_spawn();
    port_player_set_pose(100.0f, y0, 0.0f, 90.0f);
    /* Walk +X onto the line x=200. Stay finite even if the sliver matches. */
    port_set_local_pad(0, 0, (int8_t)-70, 0);
    for (t = 0; t < 40; t++) {
        if (port_sim_tick(t) != 0)
            return fail("degen tick");
        y1 = port_player_y();
        if (!y_finite(y1))
            return fail("degen y became NaN");
    }
    y1 = port_player_y();
    if (!y_finite(y1))
        return fail("degen final y");
    printf("stan_degen_y_finite y0=%.1f y1=%.1f x=%.1f\n", (double)y0, (double)y1,
           (double)port_player_x());
    port_stan_unload();
    return 0;
}

static int test_stan_slope_y_finite(void)
{
    uint8_t stan[512];
    float y0, y1;
    uint32_t t;

    port_stan_unload();
    build_stair_stan(stan, sizeof stan);
    port_stan_set_scale(1.0f);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("slope load");
    if (port_stan_eye_y(40.0f, 0.0f, &y0) != 0 || !y_finite(y0))
        return fail("slope y0");
    /* Floor at x=0 is 50; at x=400 is 150. x=40 -> ~60+175. */
    if (y0 < 220.0f || y0 > 250.0f) {
        fprintf(stderr, "slope y0=%g want ~235\n", (double)y0);
        return fail("slope y0 range");
    }
    {
        float yr;
        if (port_stan_eye_y(200.0f, 0.0f, &yr) != 0 || !y_finite(yr))
            return fail("slope riser sample");
        if (yr < 250.0f || yr > 310.0f) {
            fprintf(stderr, "slope mid y=%g want ~275\n", (double)yr);
            return fail("slope mid range");
        }
    }
    port_player_spawn();
    port_player_set_pose(40.0f, y0, 0.0f, 90.0f);
    port_set_local_pad(0, 0, (int8_t)-70, 0);
    for (t = 0; t < 80; t++) {
        if (port_sim_tick(t) != 0)
            return fail("slope tick");
        if (!y_finite(port_player_y()))
            return fail("slope y NaN");
    }
    y1 = port_player_y();
    if (!y_finite(y1))
        return fail("slope y1");
    if (!(y1 > y0 + 15.0f)) {
        fprintf(stderr, "slope y0=%g y1=%g x=%g — expected rise\n", (double)y0,
                (double)y1, (double)port_player_x());
        return fail("slope y did not rise");
    }
    if (y1 > 400.0f)
        return fail("slope y exploded");
    printf("stan_slope_y_finite y0=%.1f y1=%.1f x=%.1f\n", (double)y0, (double)y1,
           (double)port_player_x());
    port_stan_unload();
    return 0;
}

static int test_look_pitch(void)
{
    uint32_t t;
    float ph;

    port_player_spawn();
    if (port_player_phi() != 0.0f)
        return fail("spawn pitch");
    port_set_local_pad(0, 0, 0, (int)PORT_C_UP);
    if (port_sim_tick(1000) != 0)
        return fail("c-up tick 0");
    if (port_sim_tick(1001) != 0)
        return fail("c-up tick 1");
    ph = port_player_phi();
    if (!(ph > 20.0f && ph < 22.0f)) {
        fprintf(stderr, "c-up phi=%g want ~21\n", (double)ph);
        return 1;
    }
    for (t = 1002; t < 1080; t++) {
        if (port_sim_tick(t) != 0)
            return fail("c-up clamp tick");
    }
    if (fabsf(port_player_phi() - PORT_PITCH_MAX) > 0.01f)
        return fail("clamp +70");
    port_set_local_pad(0, 0, 0, (int)PORT_C_DOWN);
    for (t = 1080; t < 1200; t++) {
        if (port_sim_tick(t) != 0)
            return fail("c-down clamp tick");
    }
    if (fabsf(port_player_phi() + PORT_PITCH_MAX) > 0.01f)
        return fail("clamp -70");
    port_player_set_pose(0.0f, 0.0f, 0.0f, 0.0f);
    if (port_player_phi() != 0.0f)
        return fail("set_pose resets pitch");
    port_player_set_pitch(45.0f);
    if (fabsf(port_player_phi() - 45.0f) > 0.01f)
        return fail("set_pitch");
    port_player_set_pitch(90.0f);
    if (fabsf(port_player_phi() - PORT_PITCH_MAX) > 0.01f)
        return fail("set_pitch clamp");
    {
        float dx, dy, dz;
        port_player_set_pitch(0.0f);
        port_player_look_dir(&dx, &dy, &dz);
        if (fabsf(dx) > 0.01f || fabsf(dy) > 0.01f || fabsf(dz + 1.0f) > 0.01f)
            return fail("look dir pitch0");
        port_player_set_pitch(45.0f);
        port_player_look_dir(&dx, &dy, &dz);
        if (fabsf(dx) > 0.01f || !(dy > 0.70f && dy < 0.72f) || !(dz < -0.70f && dz > -0.72f))
            return fail("look dir +45");
    }
    printf("look_pitch c-up 2t=21 clamp=+70/-70 look+45 dy>0\n");
    return 0;
}


static int test_player_health(void)
{
    float t;
    port_set_player_count(1);
    port_player_spawn();
    if (port_player_health() != PORT_PLAYER_HEALTH_MAX)
        return fail("spawn health");
    port_player_damage(1);
    if (port_player_health() != PORT_PLAYER_HEALTH_MAX - 1)
        return fail("damage 1");
    port_player_damage(100);
    if (port_player_health() != 0)
        return fail("clamp 0");
    port_player_damage(1);
    if (port_player_health() != 0)
        return fail("dead stay 0");
    {
        float x0 = port_player_x();
        float z0 = port_player_z();
        port_player_tick(0, 80, 0);
        if (port_player_x() != x0 || port_player_z() != z0)
            return fail("dead player still walked");
    }
    port_player_spawn();
    if (port_player_health() != PORT_PLAYER_HEALTH_MAX)
        return fail("respawn health");
    port_player_set_pose(0.f, PORT_EYE_HEIGHT, 0.f, 0.f);
    if (!port_player_ray_hit(-100.f, PORT_EYE_HEIGHT, 0.f, 1.f, 0.f, 0.f, &t))
        return fail("player ray");
    if (t < 60.f || t > 80.f) {
        fprintf(stderr, "player ray t=%g want ~70\n", (double)t);
        return fail("player ray t");
    }
    if (port_player_ray_hit(-100.f, PORT_EYE_HEIGHT + 200.f, 0.f, 1.f, 0.f, 0.f, &t))
        return fail("player ray high miss");
    printf("player health ok hp=%d ray_t=%.1f\n", port_player_health(), (double)t);
    return 0;
}

/* Overlapping floors, no stair link: clip_step must stay on the low tile. */
static int test_clip_stack_unlinked_stays_low(void)
{
    uint8_t stan[512];
    float nx, nz, ny, ey;

    memset(stan, 0, sizeof stan);
    wr_be32(stan + 4, 0x0F000080u);
    wr_unit_quad(stan, 0x80, 0, 100, -88, 0, 100);
    stan[0x80 + 3] = 71;
    wr_quad_link(stan, 0x80, 0, 0x11); /* dummy so walls are live */
    wr_unit_quad(stan, 0xA8, 0, 100, 225, 0, 100);
    stan[0xA8 + 3] = 14;
    wr_quad_link(stan, 0xA8, 0, 0x16);
    port_stan_unload();
    port_stan_set_scale(1.0f);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("stack load");
    if (port_stan_eye_y(50.0f, 50.0f, &ey) != 0)
        return fail("stack eye");
    if (fabsf(ey - (-88.0f + PORT_EYE_HEIGHT)) > 1.0f)
        return fail("stack eye not low");
    nx = 60.0f;
    nz = 50.0f;
    ny = ey;
    port_stan_clip_step(50.0f, 50.0f, &nx, &nz, &ny);
    if (fabsf(ny - ey) > 1.0f)
        return fail("stack clip jumped high");
    printf("stack_unlinked clip y=%.1f (high 400 ignored)\n", (double)ny);
    port_stan_unload();
    return 0;
}

/* Low hall --link--> high landing --link--> stacked high over a low decoy.
 * clip_step must keep the high eye on the stacked xz. */
static int test_clip_stair_link_keeps_high(void)
{
    uint8_t stan[768];
    float nx, nz, ny, ey, low_y;

    memset(stan, 0, sizeof stan);
    wr_be32(stan + 4, 0x0F000080u);
    /* A low 0..100. East -> B (0xA8 rare 0x15). */
    wr_unit_quad(stan, 0x80, 0, 100, -88, 0, 100);
    stan[0x80 + 3] = 71;
    wr_quad_link(stan, 0x80, 1, 0x16);
    /* B high 100..200. West -> A, east -> C (0xD0 rare 0x1A). */
    wr_unit_quad(stan, 0xA8, 100, 200, 225, 0, 100);
    stan[0xA8 + 3] = 12;
    wr_quad_link(stan, 0xA8, 3, 0x11);
    wr_quad_link(stan, 0xA8, 1, 0x1B);
    /* C high 200..300. West -> B. */
    wr_unit_quad(stan, 0xD0, 200, 300, 225, 0, 100);
    stan[0xD0 + 3] = 12;
    wr_quad_link(stan, 0xD0, 3, 0x16);
    /* D low decoy under C. No link to C. */
    wr_unit_quad(stan, 0xF8, 200, 300, -88, 0, 100);
    stan[0xF8 + 3] = 71;
    port_stan_unload();
    port_stan_set_scale(1.0f);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("stair link load");
    if (port_stan_tile_count() != 4)
        return fail("stair link tiles");
    if (port_stan_eye_y(250.0f, 50.0f, &low_y) != 0)
        return fail("stair stack eye");
    if (fabsf(low_y - (-88.0f + PORT_EYE_HEIGHT)) > 1.0f)
        return fail("stair stack stateless not low");
    nx = 250.0f;
    nz = 50.0f;
    ny = 0.0f;
    port_stan_clip_step(50.0f, 50.0f, &nx, &nz, &ny);
    if (nx < 240.0f)
        return fail("stair clip blocked");
    ey = 225.0f + PORT_EYE_HEIGHT;
    if (fabsf(ny - ey) > 1.0f) {
        fprintf(stderr, "stair clip y=%g want %g\n", (double)ny, (double)ey);
        return fail("stair clip flattened");
    }
    /* Next step still on the stack must keep high (cache). */
    {
        float n2x = 260.0f, n2z = 50.0f, n2y = ny;
        port_stan_clip_step(nx, nz, &n2x, &n2z, &n2y);
        if (fabsf(n2y - ey) > 1.0f)
            return fail("stair second step snapped");
    }
    printf("stair_link clip y=%.1f (stateless stack %.1f ignored)\n",
           (double)ny, (double)low_y);
    port_stan_unload();
    return 0;
}


static void build_wide_corridor_stan(uint8_t *s, size_t n)
{
    memset(s, 0, n);
    wr_be32(s + 4, 0x0F000080u);
    s[0x80 + 2] = 1;
    s[0x80 + 3] = 1;
    wr_be16(s + 0x80 + 6, (uint16_t)((4u << 12) | (0u << 8) | (1u << 4) | 2u));
    wr_s16(s + 0x88 + 0, 0);
    wr_s16(s + 0x88 + 2, 50);
    wr_s16(s + 0x88 + 4, -200);
    wr_s16(s + 0x90 + 0, 400);
    wr_s16(s + 0x90 + 2, 50);
    wr_s16(s + 0x90 + 4, -200);
    wr_s16(s + 0x98 + 0, 400);
    wr_s16(s + 0x98 + 2, 50);
    wr_s16(s + 0x98 + 4, 200);
    wr_s16(s + 0xA0 + 0, 0);
    wr_s16(s + 0xA0 + 2, 50);
    wr_s16(s + 0xA0 + 4, 200);
}

/* Fitted 320-wide slab must block a |across|=120 step the old 90-half leaked. */
static int test_door_fitted_width(void)
{
    uint8_t stan[256];
    float nx, nz, ny, y;

    port_stan_unload();
    build_wide_corridor_stan(stan, sizeof stan);
    port_stan_set_scale(1.0f);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("wide stan load");
    port_stan_clear_doors();
    port_stan_add_door_w(300.0f, 0.0f, 1.0f, 0.0f, 320.0f);
    if (fabsf(port_stan_door_half_w_at(300.0f, 0.0f) - 160.0f) > 0.5f)
        return fail("wide half_w");
    if (port_stan_eye_y(200.0f, 120.0f, &y) != 0)
        return fail("wide eye");

    nx = 310.0f;
    nz = 120.0f;
    ny = y;
    port_stan_clip_step(200.0f, 120.0f, &nx, &nz, &ny);
    if (nx >= 300.0f - 15.0f + 0.5f) {
        fprintf(stderr, "wide closed leaked x=%g z=%g\n", (double)nx, (double)nz);
        return fail("wide closed side");
    }
    printf("door_fitted closed side x=%.1f (want <285)\n", (double)nx);

    port_stan_set_door_open(0, 1);
    nx = 310.0f;
    nz = 120.0f;
    ny = y;
    port_stan_clip_step(200.0f, 120.0f, &nx, &nz, &ny);
    if (nx < 300.0f) {
        fprintf(stderr, "wide open blocked x=%g\n", (double)nx);
        return fail("wide open side");
    }
    printf("door_fitted open side x=%.1f\n", (double)nx);
    port_stan_unload();
    return 0;
}


/* Chase uses clip_step_ground: same closed slab block / open pass as
 * the player. door_blocks_only is the auto-unlatch gate (not a wall). */
static int test_chase_clip_door(void)
{
    uint8_t stan[256];
    float nx, nz, ny, y;

    port_stan_unload();
    build_corridor_stan(stan, sizeof stan);
    port_stan_set_scale(1.0f);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("chase door stan");
    port_stan_clear_doors();
    port_stan_add_door(300.0f, 0.0f, 1.0f, 0.0f);
    if (port_stan_eye_y(280.0f, 0.0f, &y) != 0)
        return fail("chase door eye");

    nx = 310.0f;
    nz = 0.0f;
    ny = y;
    port_stan_clip_step_ground(280.0f, 0.0f, &nx, &nz, &ny);
    if (nx >= 300.0f - 15.0f + 0.5f) {
        fprintf(stderr, "chase ground closed leaked x=%g\n", (double)nx);
        return fail("chase ground closed");
    }
    if (!port_stan_door_blocks_only(280.0f, 0.0f, 310.0f, 0.0f))
        return fail("chase door_blocks_only closed");
    printf("chase_clip closed x=%.1f\n", (double)nx);

    port_stan_set_door_open(0, 1);
    nx = 310.0f;
    nz = 0.0f;
    ny = y;
    port_stan_clip_step_ground(280.0f, 0.0f, &nx, &nz, &ny);
    if (nx < 300.0f) {
        fprintf(stderr, "chase ground open blocked x=%g\n", (double)nx);
        return fail("chase ground open");
    }
    if (port_stan_door_blocks_only(280.0f, 0.0f, 310.0f, 0.0f))
        return fail("chase door_blocks_only open");
    printf("chase_clip open x=%.1f\n", (double)nx);

    port_stan_set_door_open(0, 0);
    if (port_stan_door_blocks_only(200.0f, 0.0f, 200.0f, 80.0f))
        return fail("chase door_blocks_only wall");
    port_stan_unload();
    return 0;
}

int main(void)
{
    uint32_t t;
    float z200, z1;

    port_player_spawn();
    port_set_local_pad(0, 0, 0, 0);
    if (port_sim_tick(0) != 0)
        return fail("idle tick");
    if (g_ClockTimer != 3)
        return fail("clock");
    if (port_player_x() != 0.0f || port_player_z() != 0.0f)
        return fail("idle drift");

    port_player_spawn();
    port_set_local_pad(0, 0, (int8_t)-70, 0);
    if (port_sim_tick(0) != 0)
        return fail("tick 0");
    z1 = port_player_z();
    if (!(z1 < -2.5f && z1 > -4.0f)) {
        fprintf(stderr, "1-tick z=%g want ~-3\n", (double)z1);
        return 1;
    }

    for (t = 1; t < 200; t++) {
        if (port_sim_tick(t) != 0)
            return fail("walk tick");
    }
    z200 = port_player_z();
    /* 200 ticks × dt=3. |z| ~600; dt=1 would be ~200. */
    if (!(z200 < -500.0f)) {
        fprintf(stderr, "10s z=%g — too slow (dt not 3?)\n", (double)z200);
        return 1;
    }
    if (fabsf(z200 / z1 - 200.0f) > 2.0f) {
        fprintf(stderr, "z200/z1=%g want ~200\n", (double)(z200 / z1));
        return 1;
    }
    printf("player walk ok z1=%g z200=%g clock=%d\n", (double)z1, (double)z200, g_ClockTimer);

    /* Hold-Shift run is 1.9× analog. Default analog must stay ~3 u/tick. */
    {
        float z_run;
        port_player_spawn();
        port_set_local_pad(0, 0, (int8_t)-70, (uint16_t)PORT_RUN);
        if (port_sim_tick(0) != 0)
            return fail("run tick");
        z_run = port_player_z();
        if (!(z_run < z1 * 1.75f && z_run > z1 * 2.05f)) {
            fprintf(stderr, "run z=%g walk z=%g ratio=%g want ~1.9\n",
                (double)z_run, (double)z1, (double)(z_run / z1));
            return fail("run mul");
        }
        printf("player run ok z_run=%g ratio=%.2f\n", (double)z_run,
            (double)(z_run / z1));
    }
    if (test_stan_scale_chris_unit() != 0)
        return 1;
    if (test_intro_spawn_y_hallway_unit() != 0)
        return 1;
    if (test_snap_walkable_prefers_hall() != 0)
        return 1;
    if (test_tile_room_lowest_floor() != 0)
        return 1;
    if (test_tile_room_at_eye_stacked() != 0)
        return 1;
    if (test_nearest_eye_keeps_linked_upper() != 0)
        return 1;
    if (test_nearest_tile_room_prefers_hall() != 0)
        return 1;
    if (test_clip_unlinked_wall() != 0)
        return 1;
    if (test_clip_stack_unlinked_stays_low() != 0)
        return 1;
    if (test_clip_stair_link_keeps_high() != 0)
        return 1;
    if (test_stan_eye_and_clip() != 0)
        return 1;
    if (test_offtile_recover() != 0)
        return 1;
    if (test_door_use_open() != 0)
        return 1;
    if (test_door_fitted_width() != 0)
        return 1;
    if (test_clip_ground_door() != 0)
        return 1;
    if (test_chase_clip_door() != 0)
        return 1;
    if (test_door_use_does_not_fire() != 0)
        return 1;
    if (test_look_pitch() != 0)
        return 1;
    if (test_stan_degen_y_finite() != 0)
        return 1;
    if (test_stan_slope_y_finite() != 0)
        return 1;
    if (test_player_health() != 0)
        return 1;
    return 0;
}
