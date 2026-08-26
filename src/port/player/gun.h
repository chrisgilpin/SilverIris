#ifndef SILVERIRIS_PORT_GUN_H
#define SILVERIRIS_PORT_GUN_H

#include <stdint.h>

/* PP7 / wppk_stats: AMMO_9MM, MagSize 7. Fire: gunfire.c mag -= 1.
 * KF7 / ak47_stats: AMMO_RIFLE, MagSize 0x1E. AMMOTYPE in bondconstants.h. */
#define PORT_AMMO_NONE 0
#define PORT_AMMO_9MM 1
#define PORT_AMMO_RIFLE 3
#define PORT_AMMO_SLOTS 30
#define PORT_WEAPON_PP7 0
#define PORT_WEAPON_KF7 1
#define PORT_GUN_MODEL_KF7 184 /* PROP_CHRKALASH */
#define PORT_PP7_MAG 7
#define PORT_PP7_RESERVE 21
#define PORT_PP7_DAMAGE 1
#define PORT_KF7_MAG 30
#define PORT_GUN_PICKUP_ADD 7
#define PORT_Z_TRIG 0x2000u
/* SKEL_FLASH cards stay visible this many ticks after a spent shot. */
#define PORT_MUZZLE_FLASH_FRAMES 3
/* Fake PORT wall. Hitscan uses this only when no stan tiles/doors
 * are loaded (no_assets / empty synthetic). */
#define PORT_WALL_Z (-50.0f)

void port_gun_reset(void);
void port_gun_reset_seat(int seat);
void port_gun_tick(uint16_t buttons);
/* Skip this rising Z: it was a door use, not a shot. */
void port_gun_suppress_fire(void);

int32_t *port_ammoheldarr(void);
int port_gun_weapon(void);
int port_gun_ammo_type(void);
int port_gun_mag_size(void);
int port_gun_mag(void);
int port_gun_reserve(void);
void port_gun_add_reserve(int n);
/* Equip a death-drop model (184 KF7). Unloads the current mag into its
 * ammo type, adds PORT_GUN_PICKUP_ADD to the new type, reloads. */
void port_gun_collect_model(int model);
int port_gun_hits(void);
int port_gun_last_hit(float *x, float *y, float *z);
/* Remaining viewmodel SKEL_FLASH frames (0 = hidden). Visual only. */
int port_gun_flash_frames(void);

#endif
