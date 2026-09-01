#ifndef SILVERIRIS_PORT_STAGE_H
#define SILVERIRIS_PORT_STAGE_H

#include <stddef.h>
#include <stdint.h>

#define PORT_STAGE_OK 0
#define PORT_STAGE_ERR_PACK -1
#define PORT_STAGE_ERR_MISSING -2
#define PORT_STAGE_ERR_FORMAT -3
#define PORT_STAGE_ERR_OOM -4

/* Matching LEVELID_* (bondconstants.h). MP Facility shares Facility bg/stan. */
#define PORT_LEVEL_COMPLEX 31
#define PORT_LEVEL_FACILITY 34
#define PORT_LEVEL_FACILITY_MP 434

/* PORT-only bg header word0. Rare retail files use 0 and compressed C0 GDLs. */
#define PORT_BG_MAGIC_G1DL 0x4731444Cu

int port_stage_load(int level_id);
void port_stage_unload(void);
const char *port_stage_last_error(void);

int port_stage_level_id(void);
int port_stage_room_count(void);
int port_stage_bg_rooms(void);
/* Bg room 1 origin (world). Retail C0 is already * 1/levelscale. 0 if present. */
int port_stage_room1(float pos[3]);
/* 1/levelscale for retail C0 Facility/Complex, else 1. */
float port_stage_bg_inv(void);
int port_stage_gdl_raw(void);
int port_stage_gdl_c0(void);
int port_stage_gdl_vtx(void);
int port_stage_gdl_sec(void);
int port_stage_portal_count(void);
void port_stage_dump_portals(void);
/* Door-sized portal openings (scaled world center xz, floor y, look-yaw, width). */
int port_stage_opening_count(void);
int port_stage_opening(int i, float pos[3], float *yaw, float *width, int *ra, int *rb);
/* Rare portal quad height (world). 0 if i is not a doorlike opening. */
float port_stage_opening_height(int i);
/* 1 if this door-sized portal sits on spawn r71->r7->r8->r20->r19->r18 / r3-r18 / r19-r21 / r1-r3 / r11-r71 / r8-r5 / r8-r10 / catwalk r13-r15 / r14-r13 / r14-r15 / ground r2-r3 / r3-r5 / r5-r4 / r10-r11 / r21-r22 / r72-r3 / r73-r11. */
int port_stage_path_opening(int ra, int rb);
int port_stage_current_room(void);
/* Lowest-floor tile, else nearby ground tile, else nearest bg centre. */
int port_stage_room_at_local(float lx, float ly, float lz);
/* 0 if room has a primary GDL. Writes command count and Rare room pos. */
int port_stage_room_gdl(int room, uint32_t *ngfx, float pos[3]);
/* Dump G1 room-vert vs stan spaces at player xz (harness / diagnose). */
void port_stage_dump_walls_at(float lx, float ly, float lz);
/* Push a skip=pose visual AABB off in-room G1 door leaves onto the pad's
 * walkable side. Leaves are vertical, door-sized, and walkable on both
 * sides (corridor walls are not). Writes local xz delta. 1 if moved. */
int port_stage_g1_chr_push(float cam_lx, float cam_lz, float pad_lx, float pad_lz,
                           float x0, float z0, float x1, float z1, float *pdx,
                           float *pdz);
/* 1 if the camera-to-pad segment hits a closed G1 door rectangle (pad is
 * behind the leaf). Extra idle in the same hall does not hit. */
int port_stage_g1_leaf_blocks(float cam_lx, float cam_lz, float pad_lx, float pad_lz);
/* Harness: print AABB vs nearby G1 door leaves (straddle / ray-rect). */
void port_stage_dump_chr_vs_g1(float cam_lx, float cam_lz, float pad_lx, float pad_lz,
                               float x0, float z0, float x1, float z1);
/* Door-sized holes in the G1 wall mesh (not Rare portals). pos is world
 * (room1-scaled + player-local sill). yaw faces the camera-side. */
int port_stage_g1_cutout_count(void);
int port_stage_g1_cutout(int i, float pos[3], float *yaw, float *width, float *tall);
/* Scan walked rooms; print screen-mapped G1 tris + cutouts at this camera. */
void port_stage_dump_g1_cutouts(float cam_lx, float cam_ly, float cam_lz,
                                float theta_deg, float pitch_deg);
/* 1 if a portal lists both rooms (either order). 0 if a==b or none. */
int port_stage_rooms_adjacent(int a, int b);
int port_stage_rooms_walked(void);
/* Walked primary-GDL room at index i after the last draw. 0 if oob. */
int port_stage_walked_room(int i);
/* 1 if room was in the last walked primary set. */
int port_stage_walked_has(int room);
int port_stage_prop_count(void);
int port_stage_prop_models(void);
int port_stage_props_drawn(void);
int port_stage_intro_pad(void);
int port_stage_guard_count(void);
int port_stage_draw(void);
const uint8_t *port_stage_bg(size_t *size_out);
const uint8_t *port_stage_stan(size_t *size_out);
void *port_stage_stan_first_room(void);

#endif
