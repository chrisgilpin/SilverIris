#ifndef SILVERIRIS_PORT_PROP_H
#define SILVERIRIS_PORT_PROP_H

#include "gfx/gbi_interp.h"

#include <stddef.h>
#include <stdint.h>

#define PORT_PROP_OK 0
#define PORT_PROP_MAX_DRAW 128

void port_prop_unload(void);
int port_prop_load(int level_id);
int port_prop_count(void);
int port_prop_models(void);
int port_prop_drawn(void);
int port_prop_guard_count(void);
/* 1 if ANIM_idle frame 0 decoded from the pack. */
int port_prop_have_idle(void);
/* First guard body npart (0 if none). */
int port_prop_guard_parts(void);
/* Short idle=1 addr=.. off=.. or skip reason. */
const char *port_prop_idle_info(void);

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
 * 0 if the pack has no gun file or bind failed. Does not bump drawn.
 */
int port_prop_fill_viewgun(G1RoomDl *out, int cap);
int port_prop_viewgun_parts(void);

#endif
