#include "lockstep.h"

#include "player/move.h"
#include "rng/random.h"
#include "vi/sim_tick.h"

#include <string.h>

typedef struct {
    uint32_t tick;
    uint8_t present;
    PortPad pads[PORT_MAX_PLAYERS];
    uint32_t sim_crc[PORT_MAX_PLAYERS];
} LockSlot;

static LockSlot g_ring[PORT_LOCKSTEP_RING];
static uint32_t g_next;
static uint8_t g_nseats;
static uint8_t g_delay;
static int g_active;

static LockSlot *slot_for(uint32_t tick)
{
    LockSlot *s = &g_ring[tick % PORT_LOCKSTEP_RING];
    if (s->present && s->tick != tick) {
        memset(s, 0, sizeof *s);
        s->tick = tick;
    } else if (!s->present) {
        s->tick = tick;
    }
    return s;
}

void port_begin_match(uint8_t nseats, uint32_t rng_seed)
{
    if (nseats < 1)
        nseats = 1;
    if (nseats > PORT_MAX_PLAYERS)
        nseats = PORT_MAX_PLAYERS;
    port_rng_begin_match(rng_seed);
    port_set_player_count((int)nseats);
    port_player_spawn();
}

void port_lockstep_reset(void)
{
    memset(g_ring, 0, sizeof g_ring);
    g_next = 0;
    g_nseats = 0;
    g_delay = 0;
    g_active = 0;
}

void port_lockstep_begin(uint8_t nseats, uint8_t delay_ticks)
{
    port_lockstep_reset();
    if (nseats < 1)
        nseats = 1;
    if (nseats > PORT_MAX_PLAYERS)
        nseats = PORT_MAX_PLAYERS;
    if (delay_ticks < 1)
        delay_ticks = 1;
    if (delay_ticks > 3)
        delay_ticks = 3;
    g_nseats = nseats;
    g_delay = delay_ticks;
    g_active = 1;
}

int port_lockstep_active(void) { return g_active; }
uint8_t port_lockstep_nseats(void) { return g_nseats; }
uint8_t port_lockstep_delay(void) { return g_delay; }
uint32_t port_lockstep_next_tick(void) { return g_next; }

int port_lockstep_submit(uint32_t tick, uint8_t seat, int8_t stick_x, int8_t stick_y,
    uint16_t buttons, uint32_t sim_crc)
{
    LockSlot *s;
    uint8_t bit;

    if (!g_active)
        return -1;
    if (seat >= g_nseats)
        return -1;
    if (tick < g_next)
        return 0;
    if (tick >= g_next + PORT_LOCKSTEP_RING)
        return -1;
    s = slot_for(tick);
    bit = (uint8_t)(1u << seat);
    if (s->present & bit)
        return 0;
    s->pads[seat].stick_x = stick_x;
    s->pads[seat].stick_y = stick_y;
    s->pads[seat].buttons = buttons;
    s->sim_crc[seat] = sim_crc;
    s->present = (uint8_t)(s->present | bit);
    return 1;
}

int port_lockstep_has_all(uint32_t tick)
{
    LockSlot *s;
    uint8_t need;

    if (!g_active || tick < g_next)
        return 0;
    s = slot_for(tick);
    if (s->tick != tick)
        return 0;
    need = (uint8_t)((1u << g_nseats) - 1u);
    return (s->present & need) == need;
}

int port_lockstep_missing_seat(uint32_t tick)
{
    LockSlot *s;
    uint8_t i;

    if (!g_active)
        return -1;
    s = slot_for(tick);
    for (i = 0; i < g_nseats; i++) {
        if ((s->present & (uint8_t)(1u << i)) == 0)
            return (int)i;
    }
    return -1;
}

int port_lockstep_run(void)
{
    LockSlot *s;
    uint8_t i;
    uint32_t tick;

    if (!g_active)
        return -1;
    tick = g_next;
    if (!port_lockstep_has_all(tick))
        return 0;
    s = slot_for(tick);
    for (i = 0; i < g_nseats; i++)
        port_set_local_pad((int)i, s->pads[i].stick_x, s->pads[i].stick_y, s->pads[i].buttons);
    if (port_sim_tick(tick) != 0)
        return -2;
    memset(s, 0, sizeof *s);
    g_next = tick + 1u;
    return 1;
}
