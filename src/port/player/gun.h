#ifndef SILVERIRIS_PORT_GUN_H
#define SILVERIRIS_PORT_GUN_H

#include <stdint.h>

/* PP7 / wppk_stats: AMMO_9MM, MagSize 7. Fire: gunfire.c mag -= 1. */
#define PORT_AMMO_NONE 0
#define PORT_AMMO_9MM 1
#define PORT_AMMO_SLOTS 30
#define PORT_PP7_MAG 7
#define PORT_PP7_RESERVE 21
#define PORT_Z_TRIG 0x2000u
#define PORT_WALL_Z (-50.0f)

void port_gun_reset(void);
void port_gun_tick(uint16_t buttons);

int32_t *port_ammoheldarr(void);
int port_gun_mag(void);
int port_gun_reserve(void);
int port_gun_hits(void);
int port_gun_last_hit(float *x, float *y, float *z);

#endif
