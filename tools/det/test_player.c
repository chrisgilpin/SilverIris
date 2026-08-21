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
    if (test_stan_scale_chris_unit() != 0)
        return 1;
    if (test_stan_eye_and_clip() != 0)
        return 1;
    if (test_door_use_open() != 0)
        return 1;
    if (test_door_use_does_not_fire() != 0)
        return 1;
    if (test_look_pitch() != 0)
        return 1;
    return 0;
}
