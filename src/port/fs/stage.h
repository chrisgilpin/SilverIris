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
/* Bg room 1 origin (world). 0 if present. */
int port_stage_room1(float pos[3]);
int port_stage_gdl_raw(void);
int port_stage_gdl_c0(void);
int port_stage_gdl_vtx(void);
int port_stage_gdl_sec(void);
int port_stage_portal_count(void);
/* Door-sized portal openings (world center xz, floor y, look-yaw, width). */
int port_stage_opening_count(void);
int port_stage_opening(int i, float pos[3], float *yaw, float *width, int *ra, int *rb);
int port_stage_current_room(void);
/* Lowest-floor tile, else nearby ground tile, else nearest bg centre. */
int port_stage_room_at_local(float lx, float ly, float lz);
/* 0 if room has a primary GDL. Writes command count and Rare room pos. */
int port_stage_room_gdl(int room, uint32_t *ngfx, float pos[3]);
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
