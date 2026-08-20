#include <stdio.h>

#include "chr/patrol.h"
#include "det/checksum.h"
#include "mp/score.h"
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
    uint64_t rng0;
    int i;

    port_rng_begin_match(1);
    port_player_spawn();
    if (port_score_scenario() != PORT_SCENARIO_NORMAL)
        return fail("SCENARIO_NORMAL");
    if (port_score_kills() != 0)
        return fail("kills start 0");
    port_checksum(0, &a);

    port_set_local_pad(0, 0, 0, (int)PORT_Z_TRIG);
    if (port_sim_tick(0) != 0)
        return fail("wall fire");
    if (port_score_kills() != 0)
        return fail("wall is not a kill");
    if (port_chr_action() != PORT_ACT_PATROL)
        return fail("guard still patrols");
    port_checksum(1, &b);
    if (a.crc_objectives != b.crc_objectives)
        return fail("wall must not change crc_objectives");

    port_player_spawn();
    rng0 = g_randomSeed;
    port_checksum(0, &a);
    for (i = 0; i < 8; i++) {
        port_set_local_pad(0, (int8_t)-70, 0, 0);
        if (port_sim_tick((uint32_t)i) != 0)
            return fail("turn");
    }
    port_set_local_pad(0, 0, 0, (int)PORT_Z_TRIG);
    if (port_sim_tick(8) != 0)
        return fail("kill shot");
    if (port_score_kills() != 1)
        return fail("kill_count");
    if (port_score_kills_this_life() != 1)
        return fail("kills_this_life");
    if (port_score_kill_counts(0) != 1)
        return fail("kill_counts[0]");
    if (port_chr_action() != PORT_ACT_DEAD)
        return fail("ACT_DEAD");
    if (port_chr_health() != 0.0f)
        return fail("health 0");
    port_checksum(8, &b);
    if (a.crc_objectives == b.crc_objectives)
        return fail("crc_objectives should include kill_count");
    if (g_randomSeed != rng0)
        return fail("kill must not call game RNG");

    port_set_local_pad(0, 0, 0, 0);
    port_sim_tick(9);
    port_set_local_pad(0, 0, 0, (int)PORT_Z_TRIG);
    port_sim_tick(10);
    if (port_score_kills() != 1)
        return fail("dead body is not a second kill");

    printf("score ok kills=%d scenario=%d act=%d crc=%08x\n", port_score_kills(),
        port_score_scenario(), port_chr_action(), b.crc_objectives);
    return 0;
}
