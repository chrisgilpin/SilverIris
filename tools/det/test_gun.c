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

static int test_pvp_hitscan(void)
{
    float th;
    int i;
    uint32_t tick = 300;

    port_stan_unload();
    port_set_player_count(2);
    port_player_spawn();
    th = atan2f(40.0f, -20.0f) * (180.0f / 3.1415927f);
    port_player_set_pose_at(0, 0.0f, 0.0f, 0.0f, th);
    port_player_set_pose_at(1, 40.0f, 0.0f, 20.0f, 0.0f);
    port_set_cur_player(0);
    if (port_player_health() != PORT_PLAYER_HEALTH_MAX)
        return fail("pvp p0 hp");
    port_set_cur_player(1);
    if (port_player_health() != PORT_PLAYER_HEALTH_MAX)
        return fail("pvp p1 hp");
    port_set_cur_player(0);
    for (i = 0; i < 20; i++) {
        port_set_cur_player(1);
        if (port_player_health() <= 0)
            break;
        port_set_cur_player(0);
        if (port_gun_mag() <= 0) {
            port_set_local_pad(0, 0, 0, (int)PORT_Z_TRIG);
            if (port_sim_tick(tick++) != 0)
                return fail("pvp reload");
            port_set_local_pad(0, 0, 0, 0);
            if (port_sim_tick(tick++) != 0)
                return fail("pvp reload idle");
        }
        port_set_local_pad(0, 0, 0, 0);
        if (port_sim_tick(tick++) != 0)
            return fail("pvp idle");
        port_set_local_pad(0, 0, 0, (int)PORT_Z_TRIG);
        if (port_sim_tick(tick++) != 0)
            return fail("pvp fire");
    }
    port_set_cur_player(0);
    {
        int hits = port_gun_hits();
        port_set_cur_player(1);
        if (port_player_health() != 0) {
            fprintf(stderr, "pvp p1 hp=%d want 0 p0hits=%d mag=%d\n",
                port_player_health(), hits, port_gun_mag());
            return fail("pvp kill");
        }
    }
    port_set_cur_player(0);
    if (port_score_kills() != 1)
        return fail("pvp kills");
    if (port_score_kill_counts(0) != 1)
        return fail("pvp kill_counts[0]");
    if (port_score_kill_counts(1) != 0)
        return fail("pvp p1 scored");
    printf("pvp ok shots=%d p1dead theta=%g\n", port_gun_hits(), (double)th);
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

    /* Walking body: cylinder follows xz. Old pad does not kill. */
    if (load_corridor() != 0)
        return 1;
    port_stan_add_guard(260.0f, 0.0f);
    port_stan_move_guard(260.0f, 0.0f, 260.0f, 40.0f);
    port_player_spawn();
    port_player_set_pose(80.0f, y, 0.0f, 90.0f);
    if (fire_once(150) != 0)
        return 1;
    if (port_score_kills() != 0)
        return fail("old pad kill after move");
    if (!port_gun_last_hit(&hx, &hy, &hz))
        return fail("old pad should still hit far tile");
    if (fabsf(hx - 400.0f) > 1.0f) {
        fprintf(stderr, "old pad hit x=%g want ~400\n", (double)hx);
        return 1;
    }
    {
        float th = atan2f(180.0f, -40.0f) * (180.0f / 3.1415927f);
        if (th < 0.0f)
            th += 360.0f;
        port_player_set_pose(80.0f, y, 0.0f, th);
    }
    if (fire_once(152) != 0)
        return 1;
    if (port_score_kills() != 1)
        return fail("moved xz kill");
    if (!port_stan_guard_was_hit(0))
        return fail("moved guard not marked");
    if (!port_stan_guard_dead_at(260.0f, 40.0f))
        return fail("dead_at moved xz");
    if (port_stan_guard_dead_at(260.0f, 0.0f))
        return fail("dead_at old pad after move");
    printf("hitscan_guard_moved kills=%d dead_new=%d dead_pad=%d (260,0 -> 260,40)\n",
           port_score_kills(), port_stan_guard_dead_at(260.0f, 40.0f),
           port_stan_guard_dead_at(260.0f, 0.0f));

    /* 0 hp: rising Z spends no mag. */
    {
        int mag0;
        port_player_spawn();
        port_player_damage(PORT_PLAYER_HEALTH_MAX);
        mag0 = port_gun_mag();
        port_gun_tick(0);
        port_gun_tick(PORT_Z_TRIG);
        if (port_gun_mag() != mag0)
            return fail("dead player still fired");
        if (port_gun_hits() != 0)
            return fail("dead player scored a hit");
        printf("gun_dead_nofire mag=%d hp=%d\n", port_gun_mag(), port_player_health());
    }

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


static int test_pitch_floor_hit(void)
{
    float hx, hy, hz, y;
    const float floor_y = 50.0f;

    if (load_corridor() != 0)
        return 1;
    if (port_stan_eye_y(200.0f, 0.0f, &y) != 0)
        return fail("floor eye");
    port_player_spawn();
    port_player_set_pose(200.0f, y, 0.0f, 90.0f);
    port_player_set_pitch(-70.0f);
    if (fire_once(200) != 0)
        return 1;
    if (!port_gun_last_hit(&hx, &hy, &hz))
        return fail("floor hit pos");
    if (fabsf(hy - floor_y) > 2.0f) {
        fprintf(stderr, "floor hit y=%g want ~50\n", (double)hy);
        return 1;
    }
    if (!(hx > 200.0f && hx < 280.0f)) {
        fprintf(stderr, "floor hit x=%g want 200..280\n", (double)hx);
        return 1;
    }
    printf("hitscan_floor x=%.1f y=%.1f z=%.1f (phi=-70 +X)\n",
           (double)hx, (double)hy, (double)hz);

    /* Pitch 0 still hits the far tile, not the floor. */
    port_player_spawn();
    port_player_set_pose(80.0f, y, 0.0f, 90.0f);
    if (fire_once(210) != 0)
        return 1;
    if (!port_gun_last_hit(&hx, &hy, &hz))
        return fail("level hit pos");
    if (fabsf(hx - 400.0f) > 1.0f) {
        fprintf(stderr, "level hit x=%g want ~400\n", (double)hx);
        return 1;
    }
    if (fabsf(hy - y) > 1.0f)
        return fail("level hit y left the eye");
    printf("hitscan_level x=%.1f y=%.1f (phi=0 far tile)\n", (double)hx, (double)hy);
    port_stan_unload();
    return 0;
}

/* A +45 look over a standing cylinder / door slab must miss; far tile still hits. */
static int test_pitch_miss_cylinder(void)
{
    float hx, hy, hz, y;

    if (load_corridor() != 0)
        return 1;
    if (port_stan_eye_y(80.0f, 0.0f, &y) != 0)
        return fail("pitch-miss eye");

    port_stan_clear_doors();
    port_stan_clear_guards();
    port_stan_add_guard(260.0f, 0.0f);
    port_player_spawn();
    port_player_set_pose(80.0f, y, 0.0f, 90.0f);
    port_player_set_pitch(45.0f);
    if (fire_once(300) != 0)
        return 1;
    if (port_stan_guard_was_hit(0))
        return fail("phi+45 still hit the guard");
    if (port_score_kills() != 0)
        return fail("phi+45 scored a kill");
    if (!port_gun_last_hit(&hx, &hy, &hz))
        return fail("phi+45 should still hit the far tile");
    if (fabsf(hx - 400.0f) > 2.0f) {
        fprintf(stderr, "phi+45 hit x=%g want ~400\n", (double)hx);
        return 1;
    }
    printf("hitscan_pitch_miss_guard x=%.1f y=%.1f kills=0 (phi=+45)\n",
           (double)hx, (double)hy);

    port_stan_clear_guards();
    port_stan_add_guard(260.0f, 0.0f);
    port_player_spawn();
    port_player_set_pose(80.0f, y, 0.0f, 90.0f);
    if (fire_once(310) != 0)
        return 1;
    if (!port_stan_guard_was_hit(0))
        return fail("phi=0 missed the guard");
    printf("hitscan_pitch0_guard still hits kills=%d\n", port_score_kills());

    port_stan_clear_guards();
    port_stan_clear_doors();
    port_stan_add_door(300.0f, 0.0f, 1.0f, 0.0f);
    port_player_spawn();
    port_player_set_pose(80.0f, y, 0.0f, 90.0f);
    port_player_set_pitch(45.0f);
    if (fire_once(320) != 0)
        return 1;
    if (!port_gun_last_hit(&hx, &hy, &hz))
        return fail("phi+45 door miss pos");
    if (fabsf(hx - 400.0f) > 2.0f) {
        fprintf(stderr, "phi+45 door hit x=%g want ~400 (miss slab)\n", (double)hx);
        return 1;
    }
    printf("hitscan_pitch_miss_door x=%.1f y=%.1f (phi=+45 over slab)\n",
           (double)hx, (double)hy);

    port_stan_unload();
    return 0;
}

/* Strong overrides of prop_ck_stubs so a divergent pickup/drop changes ck
 * without linking prop.c or a ROM. Defaults match the empty weak record. */
static int g_ck_pickup_pad = -1;
static int g_ck_pickup_kind = 0;
static int g_ck_pickup_hidden = 1;
static float g_ck_pickup_x, g_ck_pickup_y, g_ck_pickup_z;
static int g_ck_drop_model = -1;
static int g_ck_drop_hidden = 1;
static float g_ck_drop_x, g_ck_drop_y, g_ck_drop_z;

int port_prop_pickup_pad(void) { return g_ck_pickup_pad; }
int port_prop_pickup_kind(void) { return g_ck_pickup_kind; }
int port_prop_pickup_hidden(void) { return g_ck_pickup_hidden; }
int port_prop_pickup_xyz(float *x, float *y, float *z)
{
    if (x)
        *x = g_ck_pickup_x;
    if (y)
        *y = g_ck_pickup_y;
    if (z)
        *z = g_ck_pickup_z;
    return g_ck_pickup_pad < 0 ? -1 : 0;
}
int port_prop_drop_model(void) { return g_ck_drop_model; }
int port_prop_drop_hidden(void) { return g_ck_drop_hidden; }
int port_prop_drop_xyz(float *x, float *y, float *z)
{
    if (x)
        *x = g_ck_drop_x;
    if (y)
        *y = g_ck_drop_y;
    if (z)
        *z = g_ck_drop_z;
    return g_ck_drop_model < 0 ? -1 : 0;
}

static int test_crc_props(void)
{
    SimChecksum a, b, c;
    float y, gx, gz;

    if (load_corridor() != 0)
        return 1;
    if (port_stan_eye_y(80.0f, 0.0f, &y) != 0)
        return fail("ck eye");
    port_set_player_count(1);
    port_stan_add_guard(260.0f, 0.0f);
    port_stan_add_guard(260.0f, 40.0f);
    port_player_spawn();
    port_player_set_pose(80.0f, y, 0.0f, 90.0f);
    port_player_set_pitch(0.0f);
    port_set_cur_player(0);
    port_checksum(0, &a);
    if (port_stan_guard_xz(0, &gx, &gz) != 0)
        return fail("guard xz");
    port_stan_move_guard(260.0f, 0.0f, 300.0f, 0.0f);
    port_checksum(0, &b);
    if (a.crc_props == b.crc_props)
        return fail("moved guard must change crc_props");
    port_stan_move_guard(300.0f, 0.0f, 260.0f, 0.0f);
    if (fire_once(400) != 0)
        return 1;
    if (!port_stan_guard_was_hit(0))
        return fail("ck guard miss");
    port_checksum(0, &c);
    if (b.crc_props == c.crc_props)
        return fail("dead guard must change crc_props");

    g_ck_pickup_pad = 215;
    g_ck_pickup_kind = 2; /* PORT_PICKUP_ARMOUR */
    g_ck_pickup_hidden = 0;
    g_ck_pickup_x = 100.f;
    g_ck_pickup_y = 50.f;
    g_ck_pickup_z = 0.f;
    port_checksum(0, &a);
    if (a.crc_props == c.crc_props)
        return fail("pad-215 armour must change crc_props");
    g_ck_pickup_hidden = 1;
    port_checksum(0, &b);
    if (a.crc_props == b.crc_props)
        return fail("collected armour must change crc_props");

    g_ck_drop_model = 184; /* KF7 chrkalash */
    g_ck_drop_hidden = 0;
    g_ck_drop_x = 260.f;
    g_ck_drop_y = 50.f;
    g_ck_drop_z = 0.f;
    port_checksum(0, &c);
    if (b.crc_props == c.crc_props)
        return fail("KF7 drop must change crc_props");
    g_ck_drop_hidden = 1;
    port_checksum(0, &a);
    if (a.crc_props == c.crc_props)
        return fail("collected KF7 drop must change crc_props");

    g_ck_pickup_pad = -1;
    g_ck_pickup_kind = 0;
    g_ck_pickup_hidden = 1;
    g_ck_drop_model = -1;
    g_ck_drop_hidden = 1;
    printf("crc_props ok guard/pad215/kf7\n");
    return 0;
}

static int test_kf7_ammo(void)
{
    int32_t *ammo;
    int mag0, res9, nine, mag, i;

    port_stan_unload();
    port_set_player_count(1);
    port_player_spawn();
    if (port_gun_weapon() != PORT_WEAPON_PP7)
        return fail("spawn weapon");
    if (port_gun_ammo_type() != PORT_AMMO_9MM)
        return fail("spawn ammo type");
    if (port_gun_mag_size() != PORT_PP7_MAG)
        return fail("spawn mag size");
    mag0 = port_gun_mag();
    res9 = port_gun_reserve();
    ammo = port_ammoheldarr();
    if (ammo[PORT_AMMO_RIFLE] != 0)
        return fail("spawn rifle empty");
    {
        float hx, hy, hz;
        port_gun_hold(&hx, &hy, &hz);
        if (hx != PORT_PP7_HOLD_X || hy != PORT_PP7_HOLD_Y || hz != PORT_PP7_HOLD_Z)
            return fail("PP7 hold");
    }

    port_set_local_pad(0, 0, 0, (int)PORT_Z_TRIG);
    if (port_sim_tick(50) != 0)
        return fail("pre kf7 fire");
    port_set_local_pad(0, 0, 0, 0);
    if (port_sim_tick(51) != 0)
        return fail("pre kf7 idle");
    if (port_gun_mag() != mag0 - 1)
        return fail("pre kf7 mag");

    port_gun_collect_model(PORT_GUN_MODEL_KF7);
    if (port_gun_weapon() != PORT_WEAPON_KF7)
        return fail("equip KF7");
    if (port_gun_mag_size() != PORT_KF7_MAG)
        return fail("KF7 mag size");
    if (port_gun_ammo_type() != PORT_AMMO_RIFLE)
        return fail("KF7 ammo type");
    ammo = port_ammoheldarr();
    if (ammo[PORT_AMMO_9MM] != res9 + (mag0 - 1))
        return fail("9mm not converted");
    if (port_gun_mag() != PORT_GUN_PICKUP_ADD)
        return fail("KF7 mag from pickup");
    if (port_gun_reserve() != 0)
        return fail("KF7 reserve empty after load");
    {
        float hx, hy, hz;
        port_gun_hold(&hx, &hy, &hz);
        if (hx != PORT_KF7_HOLD_X || hy != PORT_KF7_HOLD_Y || hz != PORT_KF7_HOLD_Z)
            return fail("KF7 Rare hold");
    }

    nine = ammo[PORT_AMMO_9MM];
    mag = port_gun_mag();
    port_set_local_pad(0, 0, 0, (int)PORT_Z_TRIG);
    if (port_sim_tick(52) != 0)
        return fail("kf7 fire");
    if (port_gun_mag() != mag - 1)
        return fail("kf7 mag spend");
    ammo = port_ammoheldarr();
    if (ammo[PORT_AMMO_9MM] != nine)
        return fail("kf7 fire must not touch 9mm");
    if (port_gun_reserve() != 0)
        return fail("kf7 reserve still 0");

    for (i = 0; i < PORT_GUN_PICKUP_ADD - 1; i++) {
        port_set_local_pad(0, 0, 0, 0);
        port_sim_tick(60 + (uint32_t)i * 2);
        port_set_local_pad(0, 0, 0, (int)PORT_Z_TRIG);
        port_sim_tick(61 + (uint32_t)i * 2);
    }
    if (port_gun_mag() != 0)
        return fail("kf7 empty mag");
    port_set_local_pad(0, 0, 0, 0);
    port_sim_tick(90);
    port_set_local_pad(0, 0, 0, (int)PORT_Z_TRIG);
    port_sim_tick(91);
    if (port_gun_mag() != 0)
        return fail("kf7 dry reload");
    if (port_gun_reserve() != 0)
        return fail("kf7 dry reserve");

    port_gun_reset_seat(0);
    if (port_gun_weapon() != PORT_WEAPON_PP7)
        return fail("reset PP7");
    if (port_gun_mag() != PORT_PP7_MAG || port_gun_reserve() != PORT_PP7_RESERVE)
        return fail("reset ammo");
    printf("kf7 ammo ok mag_size=%d pickup=%d\n", PORT_KF7_MAG, PORT_GUN_PICKUP_ADD);
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
    if (port_gun_flash_frames() != 0)
        return fail("idle flash off");
    if (port_sim_tick(0) != 0)
        return fail("fire tick");
    if (port_gun_mag() != PORT_PP7_MAG - 1)
        return fail("mag spend");
    if (port_gun_flash_frames() != PORT_MUZZLE_FLASH_FRAMES)
        return fail("flash on fire");
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
    if (port_gun_flash_frames() != PORT_MUZZLE_FLASH_FRAMES - 1)
        return fail("flash decays on hold tick");

    port_set_local_pad(0, 0, 0, 0);
    port_sim_tick(2);
    port_sim_tick(100);
    port_sim_tick(101);
    if (port_gun_flash_frames() != 0)
        return fail("flash hides after a few ticks");
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
    if (port_gun_flash_frames() == PORT_MUZZLE_FLASH_FRAMES)
        return fail("reload is not a flash");

    printf("gun ok mag=%d reserve=%d hits=%d hitz=%g crc=%08x (no_assets z=-50)\n",
           port_gun_mag(), port_gun_reserve(), port_gun_hits(), (double)hz,
           b.crc_players);
    if (test_world_hitscan() != 0)
        return 1;
    if (test_pitch_floor_hit() != 0)
        return 1;
    if (test_pitch_miss_cylinder() != 0)
        return 1;
    if (test_pvp_hitscan() != 0)
        return 1;
    if (test_crc_props() != 0)
        return 1;
    if (test_kf7_ammo() != 0)
        return 1;
    return 0;
}
