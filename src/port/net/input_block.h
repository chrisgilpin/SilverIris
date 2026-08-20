#ifndef SILVERIRIS_INPUT_BLOCK_H
#define SILVERIRIS_INPUT_BLOCK_H

#include <stdint.h>

#ifndef PORT_MAX_PLAYERS
#define PORT_MAX_PLAYERS 4
#endif

#define PORT_INPUT_MAGIC 0x49314E42u /* "BIN1" */
#define PORT_INPUT_DG_MAGIC 0x524E4942u /* "BINR" */
#define PORT_INPUT_REDUNDANCY 8

typedef struct {
    int8_t stick_x, stick_y;
    uint16_t buttons;
} PortPad;

typedef struct {
    uint32_t magic;
    uint32_t tick;
    uint8_t seat;
    uint8_t nseats;
    uint8_t delay;
    uint8_t reserved;
    PortPad local;
    uint32_t sim_crc;
} InputBlock;

#endif
