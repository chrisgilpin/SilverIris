#include <math.h>
#include <stdio.h>

#include "chr/patrol.h"
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
    SimChecksum a, b, c;
    uint64_t rng0, chr0;
    float x0, z0, x1, z1;
    int i, saw_loop;

    port_rng_begin_match(1);
    port_player_spawn();
    if (port_chr_count() != 1)
        return fail("one guard");
    if (port_chr_action() != PORT_ACT_PATROL)
        return fail("ACT_PATROL");
    x0 = port_chr_x();
    z0 = port_chr_z();
    rng0 = g_randomSeed;
    chr0 = g_chrObjRandomSeed;
    port_checksum(0, &a);
    if (a.chr_rng_lo != (uint32_t)chr0 || a.chr_rng_hi != (uint32_t)(chr0 >> 32))
        return fail("checksum missing chrObjRandom seed");

    port_set_local_pad(0, 0, 0, 0);
    for (i = 0; i < 200; i++) {
        if (port_sim_tick((uint32_t)i) != 0)
            return fail("tick");
    }
    x1 = port_chr_x();
    z1 = port_chr_z();
    if (fabsf(x1 - x0) + fabsf(z1 - z0) < 40.0f) {
        fprintf(stderr, "guard did not walk x %g->%g z %g->%g\n", (double)x0,
            (double)x1, (double)z0, (double)z1);
        return 1;
    }
    port_checksum(200, &b);
    if (a.crc_chrs == b.crc_chrs)
        return fail("crc_chrs should include pos/action");
    if (g_randomSeed != rng0)
        return fail("patrol tick must not call game RNG");
    if (g_chrObjRandomSeed != chr0)
        return fail("patrol tick must not call chrObjRandom");
    if (b.chr_rng_lo != (uint32_t)chr0)
        return fail("chr seed snapshot drifted");

    chrObjRandomGetNext();
    port_checksum(201, &c);
    if (c.chr_rng_lo == b.chr_rng_lo)
        return fail("checksum should follow chrObjRandomGetNext");

    saw_loop = 0;
    for (i = 0; i < 400; i++) {
        port_sim_tick((uint32_t)(200 + i));
        if (port_chr_nextstep() == 0 && i > 10)
            saw_loop = 1;
    }
    if (!saw_loop)
        return fail("path flags&1 should loop to step 0");

    printf("chr ok start=(%.1f,%.1f) t200=(%.1f,%.1f) step=%d crc=%08x chr_rng=%08x\n",
        (double)x0, (double)z0, (double)x1, (double)z1, port_chr_nextstep(),
        b.crc_chrs, b.chr_rng_lo);
    return 0;
}
