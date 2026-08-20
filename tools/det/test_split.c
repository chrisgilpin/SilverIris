#include <stdio.h>

#include "det/checksum.h"
#include "overrides/lv_clock.h"
#include "player/move.h"
#include "rng/random.h"
#include "vi/sim_tick.h"

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main(void)
{
    int l, t, w, h;
    SimChecksum a, b;
    uint32_t i;

    port_rng_begin_match(1);
    port_set_player_count(1);
    port_player_spawn();
    if (port_env_players() != PORT_ENV_PLAYERS_1)
        return fail("ENV 1");
    port_viewport(0, &l, &t, &w, &h);
    if (l != 0 || w != 320 || h != 220 || t != 10)
        return fail("1P viewport");

    port_set_player_count(2);
    if (port_env_players() != PORT_ENV_PLAYERS_2)
        return fail("ENV 2");
    port_viewport(0, &l, &t, &w, &h);
    if (l != 0 || t != 10 || w != 320 || h != 109)
        return fail("2P P0 viewport");
    port_viewport(1, &l, &t, &w, &h);
    if (l != 0 || t != 121 || w != 320 || h != 109)
        return fail("2P P1 viewport");

    port_set_player_count(4);
    if (port_env_players() != PORT_ENV_PLAYERS_4)
        return fail("ENV 4");
    port_viewport(1, &l, &t, &w, &h);
    if (l != 0xA1 || t != 10 || w != 159 || h != 109)
        return fail("4P P1 viewport");
    port_viewport(3, &l, &t, &w, &h);
    if (l != 0xA1 || t != 121)
        return fail("4P P3 viewport");

    port_set_player_count(2);
    port_player_spawn();
    port_checksum(0, &a);
    if (port_player_z_at(0) != 0.0f)
        return fail("P0 spawn z");
    if (port_player_z_at(1) != 20.0f)
        return fail("P1 spawn z");

    port_set_local_pad(0, 0, 0, 0);
    port_set_local_pad(1, 0, (int8_t)-70, 0);
    for (i = 0; i < 200; i++) {
        if (port_sim_tick(i) != 0)
            return fail("2P tick");
    }
    if (port_player_z_at(0) != 0.0f)
        return fail("P0 should stay");
    if (!(port_player_z_at(1) < -480.0f)) {
        fprintf(stderr, "P1 z=%g\n", (double)port_player_z_at(1));
        return fail("P1 walk");
    }
    port_checksum(200, &b);
    if (a.crc_players == b.crc_players)
        return fail("crc_players should include all seats");
    if (port_cur_player() != 0)
        return fail("cur player restored");

    printf("split ok env2=%d env4=%d p1z=%g crc=%08x\n", PORT_ENV_PLAYERS_2,
        PORT_ENV_PLAYERS_4, (double)port_player_z_at(1), b.crc_players);
    return 0;
}
