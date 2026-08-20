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

    printf("lockstep ok delay=1 p0z=%g crc=%08x\n", (double)z0, a.crc_players);
    return 0;
}
