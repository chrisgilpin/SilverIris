/*
 * Bit-exact C of src/random.s and src/game/chrObjRandom.s (same MIPS64 LCG).
 * Default seed words: 0xAB8D9F77 0x81280783 (BE ld -> 0xAB8D9F7781280783).
 * randomSetSeed(x) / chrObjRandomSetSeed(x) store (u64)x + 1.
 * Do not call fileGenerateCRC from this module.
 */
#include <stdint.h>

#include "random.h"

uint64_t g_randomSeed = 0xAB8D9F7781280783ull;
uint64_t g_chrObjRandomSeed = 0xAB8D9F7781280783ull;

static uint32_t g_matchRngSeed;
static uint32_t g_loadOrdinal;
static int g_matchBound;

static uint32_t lcg_step(uint64_t *seed)
{
    uint64_t s = *seed;
    uint64_t a2 = (s << 63) >> 31;
    uint64_t a1 = (s << 31) >> 32;
    uint64_t a0 = (s << 44) >> 32;
    a2 = a2 | a1;
    a2 = a2 ^ a0;
    a0 = (a2 >> 20) & 0xfffu;
    a0 = a0 ^ a2;
    *seed = a0;
    return (uint32_t)a0;
}

uint32_t randomGetNext(void) { return lcg_step(&g_randomSeed); }

uint32_t randomGetNextFrom(uint64_t *s) { return lcg_step(s); }

void randomSetSeed(uint32_t x) { g_randomSeed = (uint64_t)x + 1u; }

uint32_t chrObjRandomGetNext(void) { return lcg_step(&g_chrObjRandomSeed); }

void chrObjRandomSetSeed(uint32_t x) { g_chrObjRandomSeed = (uint64_t)x + 1u; }

void port_rng_begin_match(uint32_t rngSeed)
{
    g_matchBound = 1;
    g_matchRngSeed = rngSeed;
    g_loadOrdinal = 0;
    port_rng_on_stage_load();
}

void port_rng_on_stage_load(void)
{
    uint32_t mix = g_loadOrdinal * 0x9E3779B9u;
    randomSetSeed(g_matchRngSeed ^ mix);
    chrObjRandomSetSeed((g_matchRngSeed ^ 0x4A1B2C3Du) ^ mix);
    g_loadOrdinal++;
}

/* Drop-in for bossMainloop's randomSetSeed(osGetCount()) once wired. */
void port_randomSetSeed_osGetCount(uint32_t osCount)
{
    if (g_matchBound) {
        uint32_t ordinal = g_loadOrdinal ? g_loadOrdinal - 1u : 0u;
        randomSetSeed(g_matchRngSeed ^ (ordinal * 0x9E3779B9u));
        return;
    }
    randomSetSeed(osCount);
}
