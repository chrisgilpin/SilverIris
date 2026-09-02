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
/* Max |x| or |z| of loaded tile points (after current scale). */
float port_stan_max_xz(void);
int port_stan_door_count(void);
int port_stan_ready(void);

void port_stan_clear_doors(void);
void port_stan_add_door(float world_x, float world_z, float look_x, float look_z);
/* Full Rare portal/quad width. 0 keeps the default 90 half-slab (pad doors). */
void port_stan_add_door_w(float world_x, float world_z, float look_x, float look_z,
                         float width);

/* Standing setup-guard body at the pad (world xz). Radius PORT_GUARD_RADIUS. */
#define PORT_GUARD_RADIUS 30.0f
#define PORT_GUARD_HEIGHT 185.0f
#define PORT_DOOR_HEIGHT 250.0f
void port_stan_clear_guards(void);
void port_stan_add_guard(float world_x, float world_z);
/* Follow a walking body: move the cylinder that sits at from xz. Idle pads stay. */
void port_stan_move_guard(float from_x, float from_z, float to_x, float to_z);
int port_stan_guard_count(void);
int port_stan_guard_was_hit(int i);
/* World xz of cylinder i. -1 if none. */
int port_stan_guard_xz(int i, float *x, float *z);
void port_stan_mark_ray_guard(void);
/* Mark the cylinder whose world xz matches (pad / sat body). */
void port_stan_mark_guard_at(float world_x, float world_z);
int port_stan_ray_hit_guard(void);
/* One-shot: a marked pad guard is dead (skip draw + later rays). */
int port_stan_guard_dead_at(float world_x, float world_z);

/* Rare doorTestForInteract: xz^2 < 40000 (200 units). Port use is Z_TRIG. */
#define PORT_DOOR_USE_RANGE 200.0f
/* 1 opened, 2 closed, 0 none. Both 1 and 2 are success. */
int port_stan_use_door(float local_x, float local_z, float look_x, float look_z);
/* World xz of the last successful use_door. 1 if a use has happened. */
int port_stan_last_use_xz(float *x, float *z);
/* 1 if room-local xz sits inside a closed (frac=0) door slab. */
int port_stan_closed_door_at_local(float local_x, float local_z);
/* Same test in world xz (portal centre, pad origin). Open / frac>0 is 0. */
int port_stan_closed_door_at_world(float world_x, float world_z);
/* Open a closed door in use range facing look. Never closes. Same
 * swing/side as Z-use. 1 if a door opened. */
int port_stan_unlatch_closed(float local_x, float local_z, float look_x, float look_z);
void port_stan_set_door_open(int i, int open);
int port_stan_door_is_open(int i);
float port_stan_door_frac(int i);
int port_stan_door_is_open_at(float world_x, float world_z);
/* Last use: +1 player was on +look, -1 on -look, 0 never used. */
int port_stan_door_side_at(float world_x, float world_z);
/* Visual park 0..1 over a few ticks either way. Collision is off
 * while open or frac>0 and returns only when frac hits 0. */
#define PORT_DOOR_OPEN_TICKS 6
void port_stan_tick_doors(void);
float port_stan_door_frac_at(float world_x, float world_z);
/* Fitted / Rare-quad half-width. Pad doors keep the 90 default. */
float port_stan_door_half_w_at(float world_x, float world_z);
/* Push a skip=pose body cylinder off closed door slabs onto the pad's
 * side of the leaf. Writes world xz delta. 1 if moved. */
int port_stan_push_cyl_off_doors(float world_x, float world_z, float radius,
                                 float *pdx, float *pdz);

/* Room-local xz. Returns 0 and writes floor+eye (room-local) if a tile owns xz. */
int port_stan_eye_y(float local_x, float local_z, float *y_out);
int port_stan_on_tile(float local_x, float local_z);
/* Room id of the lowest overlapping tile at room-local xz, or 0 if none. */
int port_stan_tile_room(float local_x, float local_z);
/* Overlapping tile whose floor+eye is closest to eye_y (slack 150).
 * Else the lowest-floor tile. Camera on a stacked upper (r13/r15 eye
 * ~737) must not report the ground room under the same xz. */
int port_stan_tile_room_at_eye(float local_x, float local_z, float eye_y);
/* Off-mesh: room of the nearest low-band tile within max_dist, or 0. */
int port_stan_nearest_tile_room(float local_x, float local_z, float max_dist);
/* On-mesh: same lowest-floor eye as port_stan_eye_y / tile_at_world.
 * Off-mesh: nearest non-degen centroid within max_dist (avgY+175-originY).
 * Stacked bathroom xz must not pick a high walkway centroid.
 */
#define PORT_STAN_NEAR_XZ 800.0f
int port_stan_nearest_eye_y(float local_x, float local_z, float max_dist, float *y_out);
/*
 * Off-tile spawn: snap xz onto the nearest tile whose floor sits in the
 * low band (min nearby floor + slack), not a high walkway. Writes the
 * closest point on that tile (room-local) and eye y. Returns 0 on hit.
 */
#define PORT_STAN_FLOOR_SLACK 80.0f
#define PORT_STAN_SNAP_LOOK 400.0f
int port_stan_snap_walkable(float *local_x, float *local_z, float look_x, float look_z,
                            float max_dist, float *y_out);

/* Clip a proposed room-local step. Writes a legal xz and eye y when ready.
 * Follows Rare point.link from the current tile so a stair step onto a
 * linked upper keeps that floor; stacked xz with no stair link stays low.
 */
void port_stan_clip_step(float ox, float oz, float *nx, float *nz, float *ny);
/* Draw-only camera xz. Pulls off unlinked edges / G1 walls / fitted leaves
 * then caps the offset at 16u (DRAW_SKIN 46 − WALL_SKIN 30). Any −X look-at
 * (tile 147 unlinked, G1, fitted slabs) is dropped so spawn 685 bars stay
 * face-on and the extra-idle neck is not a profile gap. clip_step /
 * PORT_WALL_SKIN stay 30. */
void port_stan_visual_xz(float lx, float lz, float *ox, float *oz);
/* Push room-local xz off the nearest unlinked stan edge along its inward
 * normal. Spawn on tile 147's south edge used to sit in the wall skin so
 * every live rAF clip looked trapped. 0 if moved. */
int port_stan_nudge_off_wall(float *lx, float *lz, float *ly);
/* Same xz clip, but always the lowest floor (guards / chase). Does not
 * touch the player's current-tile cache. */
void port_stan_clip_step_ground(float ox, float oz, float *nx, float *nz, float *ny);
/* 1 if dest sits inside a closed door slab. Chasers unlatch only then.
 * A stall G1 wall dest is not a slab. */
int port_stan_door_blocks_only(float ox, float oz, float nx, float nz);
/* Drop the current-tile cache (spawn / set_pose). */
void port_stan_clear_current(void);

/* Dump overlapping / nearby tiles at xz (harness / diagnose). */
void port_stan_debug_at(float local_x, float local_z);
/* Print one loaded tile by index, or -1 if out of range. */
void port_stan_dump_tile_i(int i);
void port_stan_dump_rare(unsigned rare);
void port_stan_dump_cross(unsigned from_room, unsigned to_room);
/* BFS Rare point.link from every tile at xz; print if upstairs is reachable. */
void port_stan_dump_stair_links(void);
void port_stan_link_reach(float local_x, float local_z);
int port_stan_climb_along_links(float start_x, float start_z,
                                float *end_x, float *end_z, float *end_y,
                                int *end_room);

/*
 * First hit along a 3D look ray from room-local origin: closed door
 * slab, exterior stan-tile edge (leave walkable), or guard cylinder.
 * Door/guard are finite-height (PORT_DOOR_HEIGHT / PORT_GUARD_HEIGHT
 * above the tile floor); tile-exit walls stay full-height xz.
 * Open doors do not block. Returns 1 and writes t in [0.05, 4000].
 */
int port_stan_ray_hit(float local_x, float local_y, float local_z,
                      float dx, float dy, float dz, float *t_out);
/* Doors + tile-exit walls only. Does not test guards or set g_ray_guard. */
int port_stan_ray_block(float local_x, float local_y, float local_z,
                        float dx, float dy, float dz, float *t_out);

#endif
