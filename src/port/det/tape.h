#ifndef SILVERIRIS_TAPE_H
#define SILVERIRIS_TAPE_H

#include <stddef.h>
#include <stdint.h>

#include "det/checksum.h"
#include "net/input_block.h"

/* TAPE1: synthetic or golden controller stream + SimChecksum. No ROM. */
#define PORT_TAPE_MAGIC 0x45504154u /* 'TAPE' little-endian */
#define PORT_TAPE_VERSION 1
#define PORT_TAPE_MAX_FRAMES 4096

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t region; /* 0 = U */
    uint32_t rng_seed;
    uint8_t pack_hash[32];
    uint8_t nseats;
    uint8_t reserved[3];
    uint32_t nframes;
} TapeHeader;

typedef struct {
    uint32_t tick;
    PortPad pads[PORT_MAX_PLAYERS];
    SimChecksum cs;
} TapeFrame;

int port_tape_build(const TapeHeader *h, const TapeFrame *frames, uint8_t **out, size_t *out_len);
int port_tape_parse(const uint8_t *bytes, size_t len, TapeHeader *h, TapeFrame **frames);
/* Replay pads; compare checksums. Returns 0 or 1+mismatch_tick. */
int port_tape_replay(const uint8_t *bytes, size_t len, uint32_t *mismatch_tick);

#endif
