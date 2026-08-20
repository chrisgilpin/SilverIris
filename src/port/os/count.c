#include "vi/tick_contract.h"

#include <ultra64.h>

static uint32_t g_portTick;

void port_begin_tick(uint32_t tick)
{
    g_portTick = tick;
}

uint32_t port_current_tick(void)
{
    return g_portTick;
}

u32 osGetCount(void)
{
    return g_portTick * (u32)PORT_SPEEDGRAPHFRAMES * PORT_CYCLES_PER_VI;
}

OSTime osGetTime(void)
{
    return (OSTime)osGetCount();
}
