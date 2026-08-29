#include <math.h>
#include <stdio.h>

#include "det/checksum.h"
#include "net/lockstep.h"
#include "player/move.h"
#include "rng/random.h"

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

static int run_n(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (port_lockstep_run() != 1)
            return -1;
    }
    return 0;
}

int main(void)
{
    SimChecksum a, b;
    uint32_t i;
    float z0, z1;

    port_begin_match(2, 1);
    port_lockstep_begin(2, 1);
    if (!port_lockstep_active())
        return fail("active");
    if (port_lockstep_delay() != 1 || port_lockstep_nseats() != 2)
        return fail("meta");
    if (port_player_count() != 2)
        return fail("seats");
    if (port_env_players() != PORT_ENV_PLAYERS_2)
        return fail("ENV 2");

    /* delay 1: prefill tick 0 idle both seats, then live tick 1. */
    if (port_lockstep_submit(0, 0, 0, 0, 0, 0) != 1)
        return fail("pre 0/0");
    if (port_lockstep_has_all(0))
        return fail("tick 0 incomplete");
    if (port_lockstep_missing_seat(0) != 1)
        return fail("missing P1");
    if (port_lockstep_run() != 0)
        return fail("must wait");
    if (port_lockstep_submit(0, 1, 0, 0, 0, 0) != 1)
        return fail("pre 0/1");
    if (port_lockstep_submit(0, 1, 0, 0, 0, 0) != 0)
        return fail("dup");
    if (!port_lockstep_has_all(0))
        return fail("tick 0 ready");
    if (port_lockstep_run() != 1)
        return fail("run 0");
    if (port_lockstep_next_tick() != 1)
        return fail("next");

    for (i = 1; i < 80; i++) {
        if (port_lockstep_submit(i, 0, 0, (int8_t)-70, 0, 0) != 1)
            return fail("p0 walk");
        if (port_lockstep_submit(i, 1, 0, 0, 0, 0) != 1)
            return fail("p1 idle");
        if (port_lockstep_run() != 1)
            return fail("run walk");
    }
    z0 = port_player_z_at(0);
    z1 = port_player_z_at(1);
    if (!(z0 < -100.0f)) {
        fprintf(stderr, "P0 z=%g\n", (double)z0);
        return fail("P0 should walk");
    }
    if (z1 != 20.0f)
        return fail("P1 spawn z idle");

    port_checksum(80, &a);

    /* Replay the same tape on a fresh match; checksums must match. */
    port_begin_match(2, 1);
    port_lockstep_begin(2, 1);
    if (port_lockstep_submit(0, 0, 0, 0, 0, 0) != 1)
        return fail("r2 pre0");
    if (port_lockstep_submit(0, 1, 0, 0, 0, 0) != 1)
        return fail("r2 pre1");
    if (run_n(1) != 0)
        return fail("r2 t0");
    for (i = 1; i < 80; i++) {
        port_lockstep_submit(i, 0, 0, (int8_t)-70, 0, 0);
        port_lockstep_submit(i, 1, 0, 0, 0, 0);
        if (port_lockstep_run() != 1)
            return fail("r2 run");
    }
    port_checksum(80, &b);
    if (a.crc_players != b.crc_players || a.rng_lo != b.rng_lo || a.crc_chrs != b.crc_chrs)
        return fail("replay crc");
    if (port_player_z_at(0) != z0 || port_player_z_at(1) != z1)
        return fail("replay pos");

    /* Divergent pad must change crc_players. */
    port_begin_match(2, 1);
    port_lockstep_begin(2, 1);
    port_lockstep_submit(0, 0, 0, 0, 0, 0);
    port_lockstep_submit(0, 1, 0, 0, 0, 0);
    run_n(1);
    for (i = 1; i < 80; i++) {
        port_lockstep_submit(i, 0, 0, (int8_t)-70, 0, 0);
        port_lockstep_submit(i, 1, 0, (int8_t)-70, 0, 0);
        port_lockstep_run();
    }
    port_checksum(80, &b);
    if (a.crc_players == b.crc_players)
        return fail("divergent pads should desync crc");

    /* Quantized look is on the pad: same tape must replay, a look flick must not. */
    port_begin_match(2, 1);
    port_lockstep_begin(2, 1);
    port_lockstep_submit(0, 0, 0, 0, 0, 0);
    port_lockstep_submit(0, 1, 0, 0, 0, 0);
    run_n(1);
    if (port_lockstep_submit_ex(1, 0, 0, 0, 0, 50, 0, 0) != 1)
        return fail("look submit");
    if (port_lockstep_submit(1, 1, 0, 0, 0, 0) != 1)
        return fail("look p1");
    if (port_lockstep_run() != 1)
        return fail("look run");
    if (port_player_theta_at(0) < 4.9f || port_player_theta_at(0) > 5.1f) {
        fprintf(stderr, "look theta=%g want 5\n", (double)port_player_theta_at(0));
        return fail("look yaw 5 deg");
    }
    port_checksum(2, &a);
    port_begin_match(2, 1);
    port_lockstep_begin(2, 1);
    port_lockstep_submit(0, 0, 0, 0, 0, 0);
    port_lockstep_submit(0, 1, 0, 0, 0, 0);
    run_n(1);
    port_lockstep_submit_ex(1, 0, 0, 0, 0, 50, 0, 0);
    port_lockstep_submit(1, 1, 0, 0, 0, 0);
    port_lockstep_run();
    port_checksum(2, &b);
    if (a.crc_players != b.crc_players)
        return fail("look replay crc");
    port_begin_match(2, 1);
    port_lockstep_begin(2, 1);
    port_lockstep_submit(0, 0, 0, 0, 0, 0);
    port_lockstep_submit(0, 1, 0, 0, 0, 0);
    run_n(1);
    port_lockstep_submit(1, 0, 0, 0, 0, 0);
    port_lockstep_submit(1, 1, 0, 0, 0, 0);
    port_lockstep_run();
    port_checksum(2, &b);
    if (a.crc_players == b.crc_players)
        return fail("look vs idle should desync crc");

    /* Strafe: PORT_STRAFE + stick_x changes x, not theta. */
    port_begin_match(1, 1);
    port_lockstep_begin(1, 1);
    if (port_lockstep_submit(0, 0, 0, 0, 0, 0) != 1)
        return fail("strafe pre");
    if (port_lockstep_run() != 1)
        return fail("strafe t0");
    {
        float x0 = port_player_x_at(0), th0 = port_player_theta_at(0);
        for (i = 1; i < 20; i++) {
            if (port_lockstep_submit((uint32_t)i, 0, 70, 0, PORT_STRAFE, 0) != 1)
                return fail("strafe submit");
            if (port_lockstep_run() != 1)
                return fail("strafe run");
        }
        if (!(port_player_x_at(0) > x0 + 10.0f)) {
            fprintf(stderr, "strafe x=%g from %g\n", (double)port_player_x_at(0), (double)x0);
            return fail("strafe +x");
        }
        if (fabsf(port_player_theta_at(0) - th0) > 0.5f)
            return fail("strafe must not turn");
    }

    /* PORT_RUN on the pad: farther than analog, crc includes the bit. */
    port_begin_match(1, 1);
    port_lockstep_begin(1, 1);
    port_lockstep_submit(0, 0, 0, 0, 0, 0);
    port_lockstep_run();
    for (i = 1; i < 20; i++) {
        port_lockstep_submit((uint32_t)i, 0, 0, (int8_t)-70, 0, 0);
        port_lockstep_run();
    }
    {
        float z_walk = port_player_z_at(0);
        SimChecksum walk_ck;
        port_checksum(20, &walk_ck);
        port_begin_match(1, 1);
        port_lockstep_begin(1, 1);
        port_lockstep_submit(0, 0, 0, 0, 0, 0);
        port_lockstep_run();
        for (i = 1; i < 20; i++) {
            port_lockstep_submit((uint32_t)i, 0, 0, (int8_t)-70, PORT_RUN, 0);
            port_lockstep_run();
        }
        port_checksum(20, &b);
        if (!(port_player_z_at(0) < z_walk * 1.75f)) {
            fprintf(stderr, "run z=%g walk z=%g\n", (double)port_player_z_at(0),
                (double)z_walk);
            return fail("run farther");
        }
        if (b.crc_players == walk_ck.crc_players)
            return fail("run bit must change crc_players");
    }

    printf("lockstep ok delay=1 p0z=%g crc=%08x look=5\n", (double)z0, a.crc_players);
    return 0;
}
