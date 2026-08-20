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

int port_stage_load(int level_id);
void port_stage_unload(void);
const char *port_stage_last_error(void);

int port_stage_level_id(void);
int port_stage_room_count(void);
const uint8_t *port_stage_bg(size_t *size_out);
const uint8_t *port_stage_stan(size_t *size_out);
void *port_stage_stan_first_room(void);

#endif
