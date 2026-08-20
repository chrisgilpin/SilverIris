#ifndef SILVERIRIS_LV_CLOCK_H
#define SILVERIRIS_LV_CLOCK_H

#include <stdint.h>

/* PORT stand-in for the timer preamble of lvlManageMpGame (lv.c NTSC 0x7F0BEB88).
 * Full lvlManageMpGame still needs the rest of the stage. Do not write these
 * scalars from port_sim_tick — call this after updateFrameCounters(3). */

extern int32_t g_ClockTimer;
extern float g_GlobalTimerDelta;
extern int32_t g_GlobalTimer;
extern int32_t g_ControlsLockedFlag;
extern int32_t g_CurrentStageToLoad;

void lvlApplyClockFromSpeedgraph(void);

#endif
