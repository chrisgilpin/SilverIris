#include "tick_contract.h"

#include "../../overrides/lv_clock.h"
#include "chr/patrol.h"
#include "player/gun.h"
#include "player/move.h"

#include "game/frametiming.h"

void updateFrameCounters(s32 deltaFrames);

int port_sim_tick(uint32_t tick)
{
    int8_t x, y;
    uint16_t buttons;

    port_begin_tick(tick);
    updateFrameCounters(PORT_SPEEDGRAPHFRAMES);
    lvlApplyClockFromSpeedgraph();
    if (speedgraphframes != PORT_SPEEDGRAPHFRAMES)
        return -1;
    if (g_ClockTimer != PORT_SPEEDGRAPHFRAMES)
        return -2;
#ifdef VERSION_EU
    if (g_GlobalTimerDelta != 3.6f)
        return -3;
#else
    if (g_GlobalTimerDelta != 3.0f)
        return -3;
#endif
    {
        int i, n, saved;
        n = port_player_count();
        saved = port_cur_player();
        for (i = 0; i < n; i++) {
            port_set_cur_player(i);
            port_get_local_pad(&x, &y, &buttons);
            port_player_tick(x, y, buttons);
            port_gun_tick(buttons);
        }
        port_set_cur_player(saved);
    }
    port_chr_tick();
    return 0;
}
