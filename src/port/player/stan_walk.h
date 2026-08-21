#ifndef SILVERIRIS_PORT_STAN_WALK_H
#define SILVERIRIS_PORT_STAN_WALK_H

#include <stddef.h>
#include <stdint.h>

/*
 * Rare eye height from bondviewPlayerBeginLife:
 *   eyeheight = player_perspective_height * 185 - 10
 * Campaign default perspective is 1, so 175. Camera Y is
 *   eyeheight + stanGetPositionYValue(tile, x, z)
 * (bondview2.c start_pos.y). Collision radius in MoveBond is 30;
 * this slice tests the player centre against the tile polygon.
 */
#define PORT_EYE_HEIGHT 175.0f
#define PORT_STAN_OK 0
#define PORT_STAN_EMPTY 1

void port_stan_unload(void);
void port_stan_set_scale(float scale);
void port_stan_set_world_origin(float x, float y, float z);
int port_stan_load(const uint8_t *bytes, size_t n);
int port_stan_tile_count(void);
int port_stan_door_count(void);
int port_stan_ready(void);

void port_stan_clear_doors(void);
void port_stan_add_door(float world_x, float world_z, float look_x, float look_z);

/* Standing setup-guard body at the pad (world xz). Radius PORT_GUARD_RADIUS. */
#define PORT_GUARD_RADIUS 30.0f
void port_stan_clear_guards(void);
void port_stan_add_guard(float world_x, float world_z);
int port_stan_guard_count(void);
int port_stan_guard_was_hit(int i);
void port_stan_mark_ray_guard(void);
int port_stan_ray_hit_guard(void);
/* One-shot: a marked pad guard is dead (skip draw + later rays). */
int port_stan_guard_dead_at(float world_x, float world_z);

/* Rare doorTestForInteract: xz^2 < 40000 (200 units). Port use is Z_TRIG. */
#define PORT_DOOR_USE_RANGE 200.0f
int port_stan_use_door(float local_x, float local_z, float look_x, float look_z);
void port_stan_set_door_open(int i, int open);
int port_stan_door_is_open(int i);
int port_stan_door_is_open_at(float world_x, float world_z);
/* Last use: +1 player was on +look, -1 on -look, 0 never used. */
int port_stan_door_side_at(float world_x, float world_z);

/* Room-local xz. Returns 0 and writes floor+eye (room-local) if a tile owns xz. */
int port_stan_eye_y(float local_x, float local_z, float *y_out);
int port_stan_on_tile(float local_x, float local_z);

/* Clip a proposed room-local step. Writes a legal xz and eye y when ready. */
void port_stan_clip_step(float ox, float oz, float *nx, float *nz, float *ny);

/*
 * First hit along a unit xz look ray from room-local origin: closed door
 * slab, exterior stan-tile edge (leave walkable), or guard cylinder.
 * Open doors do not block. Returns 1 and writes t in [0.05, 4000].
 */
int port_stan_ray_hit(float local_x, float local_z, float dx, float dz, float *t_out);

#endif
