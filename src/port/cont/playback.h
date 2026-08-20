#ifndef SILVERIRIS_PORT_PLAYBACK_H
#define SILVERIRIS_PORT_PLAYBACK_H

#include "joy.h"

void port_joy_force_sample(const struct contsample *sample);
void port_joy_install_playback(s32 nplayers);

#endif
