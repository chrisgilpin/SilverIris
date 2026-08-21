#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "det/checksum.h"
#include "mp/score.h"
#include "overrides/lv_clock.h"
#include "player/gun.h"
#include "player/move.h"
#include "player/stan_walk.h"
#include "rng/random.h"
#include "vi/sim_tick.h"

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

/* Same corridor as player-test: x 0..400, z -50..50, floor y=50. */
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

static int load_corridor(void)
{
    uint8_t stan[256];
    port_stan_unload();
    build_corridor_stan(stan, sizeof stan);
    port_stan_set_scale(1.0f);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("world stan load");
    port_stan_clear_doors();
    port_stan_clear_guards();
    return 0;
}

static int fire_once(uint32_t tick)
{
    port_set_local_pad(0, 0, 0, 0);
    if (port_sim_tick(tick) != 0)
        return fail("world idle");
    port_set_local_pad(0, 0, 0, (int)PORT_Z_TRIG);
    if (port_sim_tick(tick + 1) != 0)
        return fail("world fire");
    return 0;
}

/* Corridor +X used to "hit" the fake z=-50 plane; now it must hit the
 * far tile edge (x=400) or miss — never report z=-50 as the only hit. */
static int test_world_hitscan(void)
{
    float hx, hy, hz, y;
    const float want_y = 50.0f + PORT_EYE_HEIGHT;

    if (load_corridor() != 0)
        return 1;
    if (port_stan_eye_y(80.0f, 0.0f, &y) != 0)
        return fail("world eye");
    if (fabsf(y - want_y) > 0.05f)
        return fail("world eye y");

    port_player_spawn();
    port_player_set_pose(80.0f, y, 0.0f, 90.0f);
    if (fire_once(100) != 0)
        return 1;
    if (port_gun_hits() != 1)
        return fail("corridor far wall hit");
    if (!port_gun_last_hit(&hx, &hy, &hz))
        return fail("corridor hit pos");
    if (fabsf(hx - 400.0f) > 1.0f) {
        fprintf(stderr, "corridor hit x=%g want ~400\n", (double)hx);
        return 1;
    }
    if (fabsf(hz - PORT_WALL_Z) < 0.5f && fabsf(hx) < 1.0f)
        return fail("corridor still fake z=-50");
    if (fabsf(hz) > 2.0f) {
        fprintf(stderr, "corridor hit z=%g want ~0\n", (double)hz);
        return 1;
    }
    printf("hitscan_corridor x=%.1f z=%.1f (far tile=400, not z=-50)\n",
           (double)hx, (double)hz);

    /* Closed door slab at x=300, look +X. */
    port_stan_clear_doors();
    port_stan_add_door(300.0f, 0.0f, 1.0f, 0.0f);
    port_player_spawn();
    port_player_set_pose(80.0f, y, 0.0f, 90.0f);
    if (fire_once(110) != 0)
        return 1;
    if (port_gun_hits() != 1)
        return fail("door hit");
    if (!port_gun_last_hit(&hx, &hy, &hz))
        return fail("door hit pos");
    if (fabsf(hx - (300.0f - 15.0f)) > 1.0f) {
        fprintf(stderr, "door hit x=%g want ~285\n", (double)hx);
        return 1;
    }
    printf("hitscan_door x=%.1f (slab=300 half_t=15)\n", (double)hx);

    /* Open door must not block — far tile again. */
    port_stan_set_door_open(0, 1);
    port_player_spawn();
    port_player_set_pose(80.0f, y, 0.0f, 90.0f);
    if (fire_once(120) != 0)
        return 1;
    if (!port_gun_last_hit(&hx, &hy, &hz))
        return fail("open door hit pos");
    if (fabsf(hx - 400.0f) > 1.0f) {
        fprintf(stderr, "open door hit x=%g want ~400\n", (double)hx);
        return 1;
    }
    printf("hitscan_open_door x=%.1f (pass-through)\n", (double)hx);

    /* Guard A r=30 at x=260 on the +X ray; B off-axis at z=80. */
    port_stan_clear_doors();
    port_stan_clear_guards();
    port_stan_add_guard(260.0f, 0.0f);
    port_stan_add_guard(260.0f, 40.0f);
    port_player_spawn();
    port_player_set_pose(80.0f, y, 0.0f, 90.0f);
    if (fire_once(130) != 0)
        return 1;
    if (port_gun_hits() != 1)
        return fail("guard hit");
    if (!port_gun_last_hit(&hx, &hy, &hz))
        return fail("guard hit pos");
    if (fabsf(hx - (260.0f - PORT_GUARD_RADIUS)) > 1.0f) {
        fprintf(stderr, "guard hit x=%g want ~230\n", (double)hx);
        return 1;
    }
    if (!port_stan_guard_was_hit(0))
        return fail("guard not marked");
    if (port_stan_guard_was_hit(1))
        return fail("off-axis guard marked");
    if (port_score_kills() != 1)
        return fail("guard kill");
    printf("hitscan_guard x=%.1f marked=%d kills=%d (pad=260 r=30)\n", (double)hx,
           port_stan_guard_was_hit(0), port_score_kills());

    /* Dead body no longer blocks: same ray hits the far tile. */
    if (fire_once(132) != 0)
        return 1;
    if (port_gun_hits() != 2)
        return fail("dead-guard second hits");
    if (port_score_kills() != 1)
        return fail("dead body is not a second kill");
    if (!port_gun_last_hit(&hx, &hy, &hz))
        return fail("dead-guard second pos");
    if (fabsf(hx - 400.0f) > 1.0f) {
        fprintf(stderr, "dead-guard second x=%g want ~400\n", (double)hx);
        return 1;
    }
    printf("hitscan_guard_dead x2=%.1f kills=%d (far tile)\n", (double)hx,
           port_score_kills());

    /* Alive guard B still blocks a ray aimed at the pad. */
    {
        float th = atan2f(180.0f, -40.0f) * (180.0f / 3.1415927f);
        if (th < 0.0f)
            th += 360.0f;
        port_player_set_pose(80.0f, y, 0.0f, th);
    }
    if (fire_once(134) != 0)
        return 1;
    if (!port_stan_guard_was_hit(1))
        return fail("alive guard not hit");
    if (port_score_kills() != 2)
        return fail("second guard kill");
    printf("hitscan_guard_alive B marked=%d kills=%d\n", port_stan_guard_was_hit(1),
           port_score_kills());

    /* Door-only, no tiles, look away: miss (fake wall is off). */
    port_stan_unload();
    port_stan_add_door(300.0f, 0.0f, 1.0f, 0.0f);
    if (!port_stan_ready())
        return fail("door-only should be ready");
    port_player_spawn();
    port_player_set_pose(200.0f, 0.0f, 0.0f, 0.0f); /* look -Z, door is +X */
    if (fire_once(140) != 0)
        return 1;
    if (port_gun_hits() != 0)
        return fail("open-space miss scored a hit");
    if (port_gun_last_hit(&hx, &hy, &hz))
        return fail("open-space should not keep a hit");
    if (port_gun_mag() != PORT_PP7_MAG - 1)
        return fail("open-space still spends mag");
    printf("hitscan_miss mag=%d hits=0 (door-only look -Z)\n", port_gun_mag());

    /* Use-door still does not fire (corridor + facing door). */
    if (load_corridor() != 0)
        return 1;
    port_stan_add_door(300.0f, 0.0f, 1.0f, 0.0f);
    port_player_spawn();
    port_player_set_pose(250.0f, y, 0.0f, 90.0f);
    port_set_local_pad(0, 0, 0, 0);
    if (port_sim_tick(150) != 0)
        return fail("use idle");
    {
        int mag0 = port_gun_mag();
        int hits0 = port_gun_hits();
        port_set_local_pad(0, 0, 0, (int)PORT_Z_TRIG);
        if (port_sim_tick(151) != 0)
            return fail("use z");
        if (!port_stan_door_is_open(0))
            return fail("use did not open");
        if (port_gun_mag() != mag0)
            return fail("use spent mag");
        if (port_gun_hits() != hits0)
            return fail("use scored hit");
        printf("hitscan_use_nofire mag=%d hits=%d\n", mag0, hits0);
    }
    port_stan_unload();
    return 0;
}

int main(void)
{
    SimChecksum a, b;
    float hx, hy, hz;
    int i;

    port_rng_begin_match(1);
    port_player_spawn();
    if (port_gun_mag() != PORT_PP7_MAG || port_gun_reserve() != PORT_PP7_RESERVE)
        return fail("spawn ammo");
    port_checksum(0, &a);

    port_set_local_pad(0, 0, 0, (int)PORT_Z_TRIG);
    if (port_sim_tick(0) != 0)
        return fail("fire tick");
    if (port_gun_mag() != PORT_PP7_MAG - 1)
        return fail("mag spend");
    if (port_gun_reserve() != PORT_PP7_RESERVE)
        return fail("reserve unchanged");
    if (port_gun_hits() != 1)
        return fail("wall hit");
    if (!port_gun_last_hit(&hx, &hy, &hz))
        return fail("hit pos");
    if (hz > PORT_WALL_Z + 0.1f || hz < PORT_WALL_Z - 0.1f) {
        fprintf(stderr, "hit z=%g wall=%g\n", (double)hz, (double)PORT_WALL_Z);
        return 1;
    }
    port_checksum(0, &b);
    if (a.crc_players == b.crc_players)
        return fail("checksum should include ammoheldarr/mag");
    if (a.rng_lo != b.rng_lo)
        return fail("fire must not touch game RNG");

    /* Hold Z: no extra shot (rising edge). */
    if (port_sim_tick(1) != 0)
        return fail("hold");
    if (port_gun_mag() != PORT_PP7_MAG - 1)
        return fail("no auto on hold");

    port_set_local_pad(0, 0, 0, 0);
    port_sim_tick(2);
    for (i = 0; i < 6; i++) {
        port_set_local_pad(0, 0, 0, (int)PORT_Z_TRIG);
        port_sim_tick((uint32_t)(3 + i * 2));
        port_set_local_pad(0, 0, 0, 0);
        port_sim_tick((uint32_t)(4 + i * 2));
    }
    if (port_gun_mag() != 0)
        return fail("empty mag");
    if (port_gun_hits() != 7)
        return fail("7 hits");

    port_set_local_pad(0, 0, 0, (int)PORT_Z_TRIG);
    port_sim_tick(20);
    if (port_gun_mag() != PORT_PP7_MAG)
        return fail("reload mag");
    if (port_gun_reserve() != PORT_PP7_RESERVE - PORT_PP7_MAG)
        return fail("reload reserve");
    if (port_gun_hits() != 7)
        return fail("reload is not a shot");

    printf("gun ok mag=%d reserve=%d hits=%d hitz=%g crc=%08x (no_assets z=-50)\n",
           port_gun_mag(), port_gun_reserve(), port_gun_hits(), (double)hz,
           b.crc_players);
    if (test_world_hitscan() != 0)
        return 1;
    return 0;
}
