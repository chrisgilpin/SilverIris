#ifndef SILVERIRIS_PORT_PATROL_H
#define SILVERIRIS_PORT_PATROL_H

#include <stdint.h>

/* ACT_DEAD = 5, ACT_PATROL = 14 in bondconstants.h ACT_TYPE. */
#define PORT_ACT_INIT 0
#define PORT_ACT_DEAD 5
#define PORT_ACT_PATROL 14
#define PORT_CHR_ARRIVE 30.0f
#define PORT_CHR_WIDTH 20.0f
#define PORT_PATH_LOOP 1

void port_chr_reset(void);
void port_chr_tick(void);
void port_chr_kill(void);
int port_chr_ray_hit(float ox, float oz, float dx, float dz, float *t_out);

int port_chr_count(void);
int port_chr_action(void);
int port_chr_nextstep(void);
float port_chr_x(void);
float port_chr_y(void);
float port_chr_z(void);
float port_chr_theta(void);
float port_chr_health(void);

#endif
