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
#define PORT_WEAPON_MP5K 2
#define PORT_GUN_MODEL_KF7 184 /* PROP_CHRKALASH */
#define PORT_GUN_MODEL_MP5K 189 /* PROP_CHRM5K */
#define PORT_PP7_MAG 7
#define PORT_PP7_RESERVE 21
#define PORT_PP7_DAMAGE 1
#define PORT_KF7_MAG 30
#define PORT_MP5K_MAG 30 /* mp5k_stats MagSize 0x1E */
#define PORT_GUN_PICKUP_ADD 7
/* Camera-space hold after 180° Y. Rare gunfire.c: PosXYZ then
 * matrix_scalar_multiply(IDO_POINT_ONE) on the 3x3 (translation stays).
 * Without that 0.1 the GwppkZ / Gak47Z mesh fills the near plane. */
#define PORT_GUN_MODEL_SCALE 0.10000001f
/* MODELFILEHEADER BoundingVolumeRadius. G1 near=10 cannot host KF7's
 * 941u mesh at Rare hold Z -16 after 0.1 (PP7 294u at Z -33.5 is fine).
 * Hold XYZ stays Rare; only G1 mesh scale uses the radius ratio. */
#define PORT_GUN_WPPK_RADIUS 293.60767f
#define PORT_GUN_AK47_RADIUS 941.9339f
#define PORT_PP7_HOLD_X 11.f
#define PORT_PP7_HOLD_Y (-20.8f)
#define PORT_PP7_HOLD_Z (-33.5f)
#define PORT_KF7_HOLD_X 11.f
#define PORT_KF7_HOLD_Y (-19.f)
#define PORT_KF7_HOLD_Z (-16.f)
#define PORT_MP5K_HOLD_X 11.f
#define PORT_MP5K_HOLD_Y (-26.4f)
#define PORT_MP5K_HOLD_Z (-35.f)
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
void port_gun_hold(float *x, float *y, float *z);
int port_gun_mag(void);
int port_gun_reserve(void);
void port_gun_add_reserve(int n);
/* Equip a death-drop model (184 KF7 / 189 MP5K). Unloads the current mag
 * into its ammo type, adds PORT_GUN_PICKUP_ADD to the new type, reloads. */
void port_gun_collect_model(int model);
int port_gun_hits(void);
int port_gun_last_hit(float *x, float *y, float *z);
/* Remaining viewmodel SKEL_FLASH frames (0 = hidden). Visual only. */
int port_gun_flash_frames(void);

#endif
