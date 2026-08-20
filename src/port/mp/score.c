#include "score.h"

/*
 * MP score slice of reset_mp_options_for_scenario +
 * increment_num_kills_display_text_in_MP until front.c / gunfire.c compile.
 * kill_count and kills_this_life always increment (solo returns before HUD).
 * kill_counts[0] is P1 vs-nothing for a later 2P tape.
 */

static int g_scenario;
static int g_kill_count;
static int g_kills_this_life;
static int g_kill_counts[PORT_MP_SEATS];

void port_score_reset(void)
{
    int i;
    port_score_set_scenario(PORT_SCENARIO_NORMAL);
    g_kill_count = 0;
    g_kills_this_life = 0;
    for (i = 0; i < PORT_MP_SEATS; i++)
        g_kill_counts[i] = 0;
}

void port_score_set_scenario(int scenario)
{
    g_scenario = scenario;
}

void port_score_add_kill(void)
{
    g_kill_count += 1;
    g_kills_this_life += 1;
    g_kill_counts[0] += 1;
}

int port_score_scenario(void) { return g_scenario; }
int port_score_kills(void) { return g_kill_count; }
int port_score_kills_this_life(void) { return g_kills_this_life; }

int port_score_kill_counts(int seat)
{
    if (seat < 0 || seat >= PORT_MP_SEATS)
        return 0;
    return g_kill_counts[seat];
}
