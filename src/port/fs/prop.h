#ifndef SILVERIRIS_PORT_PROP_H
#define SILVERIRIS_PORT_PROP_H

#include "gfx/gbi_interp.h"

#include <stddef.h>
#include <stdint.h>

#define PORT_PROP_OK 0
#define PORT_PROP_MAX_DRAW 512

void port_prop_unload(void);
int port_prop_load(int level_id);
/* Rare prop.c: pad->pos *= get_room_data_float2() (1/levelscale). */
void port_prop_scale_world(float s);
int port_prop_count(void);
int port_prop_models(void);
int port_prop_drawn(void);
/* Last fill_rooms: aiming guards that emitted a parented Pchr*Z. */
int port_prop_held_drawn(void);
/* Last r71 spawn-alcove slab in player-local xz + yaw. -1 if not emitted. */
int port_prop_alcove_xz(float *x, float *z, float *yaw);
/* Last fill_rooms living-guard emit: considered / 380u skips / leaf skips. */
void port_prop_last_emit_stats(int *seen, int *skip_range, int *skip_leaf);
int port_prop_guard_count(void);
/* 1 if ANIM_idle frame 0 decoded from the pack. */
int port_prop_have_idle(void);
/* 1 if ANIM_walking mid-stride decoded from the pack. */
int port_prop_have_walk(void);
/* 1 if a pack death rest (PTR_ANIM_death_*) decoded. Else hide-body is skip-draw. */
int port_prop_have_die(void);
/* 1 if a pack aim/fire rest decoded AND bound (else idle). */
int port_prop_have_aim(void);
/* Living setup guards currently posed with the walk bind (test mover +
 * any stepping chaser). Shared by body (PORT_WALK_ID_BASE + body). */
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
/* Nearby living setup guards in LOS (same room or portal-adjacent,
 * xz<=400, |dz|<=200): turn, fire the shared hitscan, return 1 (stop).
 * Alerted guards outside the box still turn and walk toward the player
 * (3.0 u/tick, ground-floor tiles). They stop at fire-box range.
 * Z-floor: they cannot enter the spawn stall fire box. */
int port_prop_tick_guard_fire(void);
int port_prop_guard_shots(void);
/* Guards that passed LOS on the last fire tick. */
int port_prop_guard_los(void);
/* Player Z_TRIG: mark living setup guards in hear range (same/adj,
 * xz<=800) alerted and face the player. No alarm. Chase is the fire
 * tick (alerted, not in the fire box, not in the spawn Z-floor). */
void port_prop_hear_player_shot(void);
/* Living setup guards currently alerted. */
int port_prop_guard_alerted(void);
/* 1 if the posed-walk test mover is living and alerted (chase owns xz). */
int port_prop_walker_alerted(void);
/* Guard i yaw (deg) and alerted flag. -1 if none. */
int port_prop_guard_yaw(int i, float *yaw, int *alerted);
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
/* crc32 of idle / aim rest eulers (load-time frames). */
uint32_t port_prop_idle_rest_crc(void);
uint32_t port_prop_aim_rest_crc(void);
/* Guard-enum index of the extra idle (closest-to-spawn). -1 if none. */
int port_prop_idle_guard(void);
/* 1 if guard i is currently on the walk model (not idle rest). */
int port_prop_guard_walk_bound(int i);
/* 1 if guard i is currently on the aim model. */
int port_prop_guard_aim_bound(int i);
/* Idle-fit scale on guard i (0.123 = 185u / 1510u). 0 if none. */
float port_prop_guard_fit_scale(int i);
/* 1 if guard i has a dump-verified Chead*Z on the neck placeholder. */
int port_prop_guard_have_head(int i);
/* Unscaled neck attach (opcode 23 / Switches[4]). -1 if none. */
int port_prop_guard_head_off(int i, float *x, float *y, float *z);
/* Last fill_rooms: Chead*Z parented to a posed neck 4x4 (not skip=pose Euler). */
int port_prop_head_joint_drawn(void);
/* Advance PTR_ANIM_death_* this many frames per sim tick (starts at 0
 * on first dead guard, holds last). 4/tick: 0->88 in 22 ticks ~ 1.1s @ 20 Hz. */
#define PORT_DIE_FRAMES_PER_TICK 4
void port_prop_tick_die(void);
/* Snap death rest to a frame (clamps to last). Marks the fall started. */
void port_prop_set_die_frame(int frame);
/* Current death frame, or -1. */
int port_prop_die_frame(void);
/* Last death frame (nframes-1), or -1. */
int port_prop_die_last_frame(void);
/* Extra yaw (deg) on the nearest oriented dead joint rest so fetal +Z
 * lies across the shooter. 0 if none has locked yet. */
float port_prop_die_add_yaw(void);
/* crc32 of the current death rest eulers. */
uint32_t port_prop_die_rest_crc(void);
/* After stan/origin are live: sit the test mover on a ground-floor
 * tile just around the Facility spawn corner. 1 if moved. */
int port_prop_place_walker_near_spawn(void);
/* Sit the test mover on a ground-floor tile at room-local xz. No spawn
 * cone and no fire-box retarget. 1 if sat. */
int port_prop_place_walker_at(float local_x, float local_z);
/* Mark the test mover alerted and face the player. Does not hear others. */
int port_prop_alert_walker(void);

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
int port_prop_intro_count(void);
int port_prop_intro_at(int i, float pos[3], float look[3], int *pad_out);
int port_prop_door_count(void);
int port_prop_door_xz(int i, float *x, float *z, float *lx, float *lz);
/* Fitted-slab park at current frac: world xz delta + extra yaw. 0 if closed. */
int port_prop_door_park_offset(float world_x, float world_z, float portal_yaw,
                               float *dx, float *dz, float *add_yaw);
int port_prop_guard_xz(int i, float *x, float *z);
int port_prop_guard_xyz(int i, float *x, float *y, float *z);
/* Drawn-body cylinder (same T*R_yaw*R_pose + fit as emit). Local xz,
 * radius, y0..y0+h. -1 if no parts. Pad cylinder stays PORT_GUARD_RADIUS. */
int port_prop_guard_visual_cyl(int i, float *lx, float *lz, float *radius,
                               float *y0, float *h);
/* skip=pose vertex AABB in player-local xz. -1 if no parts. */
int port_prop_guard_visual_aabb(int i, float *x0, float *z0, float *x1, float *z1);
/* Hitscan vs living setup-chr visual cylinders (player-local ray). 1 on
 * hit; port_prop_chr_hit_xz is the prop world xz to mark. */
int port_prop_chr_ray_hit(float local_x, float local_y, float local_z,
                          float dx, float dy, float dz, float *t_out);
int port_prop_chr_hit_xz(float *world_x, float *world_z);

/*
 * First-person pack gun as a static camera-space viewmodel.
 * Spawn is GwppkZ (PP7). Collecting a KF7 death-drop (chrkalash 184)
 * switches to pack Gak47Z if that blob bound. Never PchrkalashZ.
 * Collecting MP5K (chrmp5k 189) switches to pack Gmp5kZ.
 * Hold is port_gun_hold (Rare Pos). Mesh is IDO_POINT_ONE (0.1).
 * SKEL_FLASH cards are omitted unless port_gun_flash_frames() > 0.
 * 0 if the pack has no gun file or bind failed. Does not bump drawn.
 */
#define PORT_GUN_WPPK_ID 9001
#define PORT_GUN_AK47_ID 9002
#define PORT_GUN_MP5K_ID 9003
int port_prop_fill_viewgun(G1RoomDl *out, int cap);
int port_prop_viewgun_parts(void);
int port_prop_viewgun_id(void);

/* One setup-placed on-mesh ground PROPDEF (Facility: armour pad 215).
 * Embedded/assigned stay skipped at load. A dead guard's ASSIGNEDTOCHR
 * weapon (chrkalash 184 / chrmp5k 189) may spawn at death xz. */
#define PORT_PICKUP_AMMO 1
#define PORT_PICKUP_ARMOUR 2
#define PORT_PICKUP_RADIUS 80.0f
int port_prop_pickup_pad(void);
int port_prop_pickup_type(void);
int port_prop_pickup_model(void);
int port_prop_pickup_kind(void);
int port_prop_pickup_hidden(void);
int port_prop_pickup_drawn(void);
int port_prop_pickup_xyz(float *x, float *y, float *z);
void port_prop_tick_pickup(void);
void port_prop_choose_pickup(void);
/* Death-drops of assigned-to-chr weapons. Last-wins helpers keep the
 * single-drop record; count/at hash every drop. -1/1 if none. */
int port_prop_drop_count(void);
int port_prop_drop_model_at(int i);
int port_prop_drop_hidden_at(int i);
int port_prop_drop_xyz_at(int i, float *x, float *y, float *z);
int port_prop_drop_model(void);
int port_prop_drop_hidden(void);
int port_prop_drop_drawn(void);
int port_prop_drop_xyz(float *x, float *y, float *z);

/* Last fill_rooms fitted door faces (path / spawn-alcove / G1 cutout).
 * World xz + yaw. kind 0=path 1=alcove 2=cutout. */
int port_prop_slab_emit_count(void);
int port_prop_slab_emit_at(int i, float *x, float *z, float *yaw, int *kind);

#endif
