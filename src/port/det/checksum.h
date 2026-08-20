#ifndef SILVERIRIS_CHECKSUM_H
#define SILVERIRIS_CHECKSUM_H

#include <stdint.h>

typedef struct {
    uint32_t tick;
    uint32_t rng_lo, rng_hi;
    uint32_t chr_rng_lo, chr_rng_hi;
    uint32_t crc_players;
    uint32_t crc_chrs;
    uint32_t crc_props;
    uint32_t crc_objectives;
} SimChecksum;

uint32_t port_crc32c(const uint8_t *data, uint32_t len);
void port_checksum(uint32_t tick, SimChecksum *out);

#endif
