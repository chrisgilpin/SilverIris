#ifndef SILVERIRIS_PORT_PROP_H
#define SILVERIRIS_PORT_PROP_H

#include "gfx/gbi_interp.h"

#include <stddef.h>
#include <stdint.h>

#define PORT_PROP_OK 0
#define PORT_PROP_MAX_DRAW 512

void port_prop_unload(void);
int port_prop_load(int level_id);
int port_prop_count(void);
int port_prop_models(void);
int port_prop_drawn(void);
int port_prop_guard_count(void);
/* 1 if ANIM_idle frame 0 decoded from the pack. */
int port_prop_have_idle(void);
/* 1 if ANIM_walking mid-stride decoded from the pack. */
int port_prop_have_walk(void);
/* Setup guards posed with the walk bind (0 or 1). */
int port_prop_walk_count(void);
/* First guard body npart (0 if none). */
int port_prop_guard_parts(void);
/* Short idle=1 addr=.. walk=1 addr=.. or skip reason. */
const char *port_prop_idle_info(void);
/* Test-mover pad xz. -1 if none. */
int port_prop_walk_xz(float *x, float *z);
/* Test-mover pad xyz. -1 if none. */
int port_prop_walk_xyz(float *x, float *y, float *z);
/* Loop ANIM_walking one frame and step the test mover along its strip. */
void port_prop_tick_walk(void);
/* If the player is in LOS of the one test mover: turn, fire, return 1 (stop). */
int port_prop_tick_guard_fire(void);
int port_prop_guard_shots(void);
/* Local-xz ping-pong endpoints of the test-mover strip. -1 if none. */
int port_prop_walk_path(float *ax, float *az, float *bx, float *bz);
/* NTSC units/tick (PORT_CHR_WALK * 3). 0 if the mover has no path. */
float port_prop_walk_speed(void);
/* Snap the test mover to a walk frame (loops). */
void port_prop_set_walk_frame(int frame);
/* Current walk frame, or -1. */
int port_prop_walk_frame(void);
/* crc32 of the current walk rest eulers. */
uint32_t port_prop_walk_rest_crc(void);
/* After stan/origin are live: sit the test mover on a ground-floor
 * tile just around the Facility spawn corner. 1 if moved. */
int port_prop_place_walker_near_spawn(void);

/*
 * Fill out[] with G1 passes for scenery (doors / static props / glass)
 * and standing setup guards whose pad is near a walked room. room1 is
 * bg room 1 origin. Does not increment rooms_walked. Returns the number
 * of passes written.
 */
int port_prop_fill_rooms(G1RoomDl *out, int cap, const float room1[3],
                         const float *room_xyz, int nrooms, const uint8_t *room_ids);

/* First INTROTYPE_SPAWN with demo=0 (else first spawn). pad_out is Rare's
 * index (may be 10000+ bound). Returns 0 if a pad was resolved. */
int port_prop_intro(float pos[3], float look[3], int *pad_out);
int port_prop_intro_pad(void);
int port_prop_door_count(void);
int port_prop_door_xz(int i, float *x, float *z, float *lx, float *lz);
int port_prop_guard_xz(int i, float *x, float *z);

/*
 * First-person GwppkZ (PP7) as a static camera-space viewmodel.
 * Hold is documented in prop.c (Rare wppk_stats Pos, G1 near-plane Z).
 * SKEL_FLASH cards are omitted unless port_gun_flash_frames() > 0.
 * 0 if the pack has no gun file or bind failed. Does not bump drawn.
 */
int port_prop_fill_viewgun(G1RoomDl *out, int cap);
int port_prop_viewgun_parts(void);

#endif
