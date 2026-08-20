#ifndef SILVERIRIS_PORT_SCORE_H
#define SILVERIRIS_PORT_SCORE_H

#include <stdint.h>

/* SCENARIO_NORMAL = 0 in bondconstants.h MPSCENARIOS. */
#define PORT_SCENARIO_NORMAL 0
#define PORT_MP_SEATS 4

void port_score_reset(void);
void port_score_set_scenario(int scenario);
void port_score_add_kill(void);

int port_score_scenario(void);
int port_score_kills(void);
int port_score_kills_this_life(void);
int port_score_kill_counts(int seat);

#endif
