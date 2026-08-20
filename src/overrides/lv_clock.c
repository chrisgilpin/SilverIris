#include "overrides/lv_clock.h"

#include "game/frametiming.h"

int32_t g_ClockTimer;
float g_GlobalTimerDelta;
int32_t g_GlobalTimer;
int32_t g_ControlsLockedFlag;
int32_t g_CurrentStageToLoad;
static int32_t g_clockPassCount;

void lvlApplyClockFromSpeedgraph(void)
{
    if (g_ControlsLockedFlag != 0) {
        g_ClockTimer = 0;
    } else {
        g_ClockTimer = speedgraphframes;
        g_clockPassCount += 1;
    }
#ifdef VERSION_EU
    /* EU: g_JP_GlobalTimerDelta = (f32)g_ClockTimer; then * 1.2f. Not this build. */
    g_GlobalTimerDelta = (float)g_ClockTimer * 1.2f;
#else
    g_GlobalTimerDelta = (float)g_ClockTimer;
#endif
    g_GlobalTimer += g_ClockTimer;
}
