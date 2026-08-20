#include <stdio.h>

#include "det/checksum.h"
#include "overrides/lv_clock.h"
#include "player/gun.h"
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

    printf("gun ok mag=%d reserve=%d hits=%d hitz=%g crc=%08x\n", port_gun_mag(),
           port_gun_reserve(), port_gun_hits(), (double)hz, b.crc_players);
    return 0;
}
