#ifndef SILVERIRIS_PORT_RANDOM_H
#define SILVERIRIS_PORT_RANDOM_H

#include <stdint.h>

extern uint64_t g_randomSeed;
extern uint64_t g_chrObjRandomSeed;

void randomSetSeed(uint32_t x);
uint32_t randomGetNext(void);
uint32_t randomGetNextFrom(uint64_t *seed);

uint32_t chrObjRandomGetNext(void);
void chrObjRandomSetSeed(uint32_t x);

void port_rng_begin_match(uint32_t rngSeed);
void port_rng_on_stage_load(void);
void port_randomSetSeed_osGetCount(uint32_t osCount);

#endif
