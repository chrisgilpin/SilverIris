#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fs/c0pack.h"
#include "fs/pack_dma.h"
#include "fs/stage.h"
#include "overrides/lv_clock.h"
#include "rng/random.h"
#include "vi/sim_tick.h"
#include "vi/tick_contract.h"

#include "game/frametiming.h"

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

static void wr_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

int main(void)
{
    uint8_t bg[128];
    uint8_t stan[256];
    C0File files[2];
    uint8_t *pack = NULL;
    size_t pack_len = 0;
    uint8_t hash[32];
    uint64_t seed_a, seed_b;
    size_t n = 0;
    const uint8_t *loaded;
    int rc;

    memset(bg, 0, sizeof bg);
    memset(stan, 0, sizeof stan);
    wr_be32(bg + 4, 0x0F000040u);
    wr_be32(stan + 4, 0x0F000080u);

    files[0].path = "assets/obseg/bg/bg_ark_all_p.bin";
    files[0].bytes = bg;
    files[0].size = sizeof bg;
    files[1].path = "assets/obseg/stan/Tbg_ark_all_p_stanZ.bin";
    files[1].bytes = stan;
    files[1].size = sizeof stan;
    if (c0pack_build(files, 2, 0, 0, &pack, &pack_len, hash) != 0)
        return fail("build pack");
    if (port_init(pack, (uint32_t)pack_len) != PORT_PACK_OK)
        return fail("port_init");

    port_rng_begin_match(1);
    seed_a = g_randomSeed;
    seed_b = g_chrObjRandomSeed;

    rc = port_stage_load(PORT_LEVEL_FACILITY);
    if (rc != PORT_STAGE_OK) {
        fprintf(stderr, "load rc=%d\n", rc);
        return fail("stage load");
    }
    if (port_stage_level_id() != PORT_LEVEL_FACILITY)
        return fail("level id");
    if (port_stage_room_count() != 1)
        return fail("room count");
    loaded = port_stage_bg(&n);
    if (!loaded || n != sizeof bg)
        return fail("bg size");
    if (port_stage_stan_first_room() != (void *)(port_stage_stan(&n) + 0x80))
        return fail("stan first room");
    if (g_randomSeed == seed_a && g_chrObjRandomSeed == seed_b)
        return fail("K16 did not reseed");
    if (g_CurrentStageToLoad != PORT_LEVEL_FACILITY)
        return fail("g_CurrentStageToLoad");
    printf("stage facility rooms=%d first=%p\n", port_stage_room_count(),
           port_stage_stan_first_room());

    if (port_sim_tick(0) != 0)
        return fail("sim tick 0");
    if (speedgraphframes != 3 || g_ClockTimer != 3)
        return fail("clock pin");
    if (g_GlobalTimerDelta != 3.0f)
        return fail("dt");
    if (port_sim_tick(1) != 0)
        return fail("sim tick 1");
    if (g_GlobalTimer != 6)
        return fail("global timer");

    if (port_stage_load(99) != PORT_STAGE_ERR_FORMAT)
        return fail("unknown level");
    if (port_stage_load(PORT_LEVEL_COMPLEX) != PORT_STAGE_ERR_MISSING)
        return fail("complex missing");

    printf("stage ok rooms=%d clock=%d dt=%g\n", port_stage_room_count(), g_ClockTimer,
           (double)g_GlobalTimerDelta);
    port_stage_unload();
    port_shutdown();
    free(pack);
    return 0;
}
