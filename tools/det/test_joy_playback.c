#include <stdio.h>
#include <string.h>

#include "joy.h"
#include "cont/playback.h"
#include <ultra64.h>

int main(void)
{
    struct contsample sample;
    u16 buttons;

    memset(&sample, 0, sizeof sample);
    sample.pads[0].stick_x = 40;
    sample.pads[0].stick_y = -20;
    sample.pads[0].button = Z_TRIG;
    sample.pads[0].errno = 0;

    joyInit();
    port_joy_force_sample(&sample);
    port_joy_install_playback(1);
    joyConsumeSamplesWrapper();

    if (joyGetStickX(0) != 40) {
        fprintf(stderr, "stick_x got %d want 40\n", (int)joyGetStickX(0));
        return 1;
    }
    if (joyGetStickY(0) != -20) {
        fprintf(stderr, "stick_y got %d want -20\n", (int)joyGetStickY(0));
        return 1;
    }
    buttons = joyGetButtons(0, 0xffff);
    if (buttons != Z_TRIG) {
        fprintf(stderr, "buttons got 0x%x want Z_TRIG 0x%x\n", buttons, Z_TRIG);
        return 1;
    }
    if (sample.pads[0].errno != 0) {
        fprintf(stderr, "errno was not 0\n");
        return 1;
    }
    puts("joy playback ok");
    return 0;
}
