#include "joy.h"

#include <string.h>

static struct contsample g_ForcedSample;
static s32 g_HaveForcedSample;

static s32 port_playback_cb(struct contsample *samples, s32 curlast)
{
    s32 next = (curlast + 1) % CONTSAMPLE_LEN;
    s32 i;
    if (g_HaveForcedSample) {
        samples[next] = g_ForcedSample;
    } else {
        memset(&samples[next], 0, sizeof(samples[next]));
    }
    for (i = 0; i < MAXCONTROLLERS; i++) {
        samples[next].pads[i].errno = 0;
    }
    return next;
}

void port_joy_force_sample(const struct contsample *sample)
{
    g_ForcedSample = *sample;
    g_HaveForcedSample = 1;
}

void port_joy_install_playback(s32 nplayers)
{
    joySetPlaybackFunc(port_playback_cb, nplayers);
    joySetContDataIndex(1);
}
