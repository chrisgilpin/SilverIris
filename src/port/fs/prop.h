#ifndef SILVERIRIS_PORT_PROP_H
#define SILVERIRIS_PORT_PROP_H

#include "gfx/gbi_interp.h"

#include <stddef.h>
#include <stdint.h>

#define PORT_PROP_OK 0
#define PORT_PROP_MAX_DRAW 24

void port_prop_unload(void);
int port_prop_load(int level_id);
int port_prop_count(void);
int port_prop_models(void);
int port_prop_drawn(void);

/*
 * Fill out[] with G1 passes for scenery (doors / static props / glass)
 * whose pad is near a walked room. room1 is bg room 1 origin. Does not
 * increment rooms_walked. Returns the number of passes written.
 */
int port_prop_fill_rooms(G1RoomDl *out, int cap, const float room1[3],
                         const float *room_xyz, int nrooms, const uint8_t *room_ids);

/* First INTROTYPE_SPAWN with demo=0 (else first spawn). pad_out is Rare's
 * index (may be 10000+ bound). Returns 0 if a pad was resolved. */
int port_prop_intro(float pos[3], float look[3], int *pad_out);
int port_prop_intro_pad(void);

#endif
