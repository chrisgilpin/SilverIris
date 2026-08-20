#ifndef SILVERIRIS_MATCH_CONFIG_H
#define SILVERIRIS_MATCH_CONFIG_H

#include <stdint.h>

#define MATCH_CONFIG_BYTES 160
#define MATCH_CONFIG_PROTOCOL 1
#define MATCH_CONFIG_REGION_U 0

typedef struct {
    uint16_t protocol;
    uint8_t region;
    uint8_t nseats;
    uint8_t delayTicks;
    uint8_t speedgraphframes;
    uint8_t aimSight;
    uint8_t autoAim;
    uint8_t lookAhead;
    uint8_t aimControl;
    uint8_t radar;
    uint8_t pad0;
    uint32_t rngSeed;
    uint32_t stage;
    uint32_t scenario;
    uint32_t gameLength;
    uint32_t chars[4];
    uint32_t handicaps[4];
    uint32_t favWeapons[4][2];
    float slider007[4];
    uint8_t packHash[32];
    uint8_t buildId[20];
} MatchConfig;

/* Little-endian packed 160 bytes. Offset 11 is pad0 and must be 0. */
int encodeMatchConfig(const MatchConfig *cfg, uint8_t out[MATCH_CONFIG_BYTES]);
int decodeMatchConfig(const uint8_t in[MATCH_CONFIG_BYTES], MatchConfig *cfg);

#endif
