#ifndef SILVERIRIS_TICK_CONTRACT_H
#define SILVERIRIS_TICK_CONTRACT_H

#include <stdint.h>

#define PORT_TICK_HZ          20
#define PORT_TICK_MS          50
#define PORT_SPEEDGRAPHFRAMES 3

#if defined(VERSION_EU) || defined(REFRESH_PAL)
#define PORT_CYCLES_PER_VI 931050u
#else
#define PORT_CYCLES_PER_VI 775875u
#endif

void port_begin_tick(uint32_t tick);
uint32_t port_current_tick(void);

#endif
