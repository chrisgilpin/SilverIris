#ifndef SILVERIRIS_PORT_SCORE_H
#define SILVERIRIS_PORT_SCORE_H

#include <stdint.h>

/* SCENARIO_NORMAL = 0 in bondconstants.h MPSCENARIOS. */
#define PORT_SCENARIO_NORMAL 0
#define PORT_MP_SEATS 4

/* GAMELENGTH in bondconstants.h. Time limits are wall minutes at 20 Hz. */
#define PORT_LEN_UNLIMITED 0
#define PORT_LEN_5MIN 1
#define PORT_LEN_10MIN 2
#define PORT_LEN_20MIN 3
#define PORT_LEN_5PT 4
#define PORT_LEN_10PT 5
#define PORT_LEN_20PT 6

void port_score_reset(void);
void port_score_set_scenario(int scenario);
void port_score_configure(int scenario, uint32_t game_length);
void port_score_add_kill(void);
void port_score_tick(void);

int port_score_scenario(void);
int port_score_kills(void);
int port_score_kills_this_life(void);
int port_score_kill_counts(int seat);
int port_score_game_length(void);
int port_score_remain_ticks(void);
int port_score_over(void);
/* Seat with the most kills, or -1 on a tie / no kills. */
int port_score_winner(void);

#endif
