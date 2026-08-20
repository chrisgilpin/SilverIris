#include <math.h>
#include <stdio.h>
#include <stdint.h>

#include "overrides/lv_clock.h"
#include "player/move.h"
#include "vi/sim_tick.h"
#include "vi/tick_contract.h"

#include "game/frametiming.h"

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
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
    return 0;
}
