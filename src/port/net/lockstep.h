#ifndef SILVERIRIS_LOCKSTEP_H
#define SILVERIRIS_LOCKSTEP_H

#include "net/input_block.h"

#include <stdint.h>

/* Delay-based lockstep (K4/K21). Ring holds committed InputBlocks until
 * every seat has tick T; then pads are injected via port_set_local_pad and
 * port_sim_tick. joy.c is not on the wasm path; native playback can wrap
 * the same blocks later. Stall/drop timers are wall-clock (shell). */

#define PORT_LOCKSTEP_RING 64
#define PORT_LOCKSTEP_STALL_MS 350
#define PORT_LOCKSTEP_DROP_MS 10000

void port_begin_match(uint8_t nseats, uint32_t rng_seed);
void port_lockstep_reset(void);
void port_lockstep_begin(uint8_t nseats, uint8_t delay_ticks);
int port_lockstep_active(void);
uint8_t port_lockstep_nseats(void);
uint8_t port_lockstep_delay(void);
uint32_t port_lockstep_next_tick(void);

/* 1 accepted, 0 duplicate, -1 reject. Look q is 0.1 deg (PORT_LOOK_Q). */
int port_lockstep_submit(uint32_t tick, uint8_t seat, int8_t stick_x, int8_t stick_y,
    uint16_t buttons, uint32_t sim_crc);
int port_lockstep_submit_ex(uint32_t tick, uint8_t seat, int8_t stick_x, int8_t stick_y,
    uint16_t buttons, int8_t look_yaw, int8_t look_pitch, uint32_t sim_crc);
int port_lockstep_has_all(uint32_t tick);
/* First missing seat, or -1. */
int port_lockstep_missing_seat(uint32_t tick);
/* 1 ran, 0 waiting, -1 inactive, -2 sim failed. */
int port_lockstep_run(void);

#endif
