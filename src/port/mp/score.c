#include "score.h"

#include "player/move.h"
#include "vi/tick_contract.h"

/*
 * MP score slice of reset_mp_options_for_scenario +
 * increment_num_kills_display_text_in_MP until front.c / gunfire.c compile.
 * kill_count and kills_this_life always increment (solo returns before HUD).
 * kill_counts[seat] is the shooting seat (P1 vs-nothing stays [0]).
 * gameLength follows GAMELENGTH (front.c multi_game_lengths).
 */

static int g_scenario;
static int g_kill_count;
static int g_kills_this_life;
static int g_kill_counts[PORT_MP_SEATS];
static int g_game_length;
static int g_time_limit;
static int g_point_limit;
static int g_elapsed;
static int g_over;

static void apply_length(uint32_t game_length)
{
    g_game_length = (int)game_length;
    g_time_limit = 0;
    g_point_limit = 0;
    switch (g_game_length) {
    case PORT_LEN_5MIN:
        g_time_limit = 5 * 60 * PORT_TICK_HZ;
        break;
    case PORT_LEN_10MIN:
        g_time_limit = 10 * 60 * PORT_TICK_HZ;
        break;
    case PORT_LEN_20MIN:
        g_time_limit = 20 * 60 * PORT_TICK_HZ;
        break;
    case PORT_LEN_5PT:
        g_point_limit = 5;
        break;
    case PORT_LEN_10PT:
        g_point_limit = 10;
        break;
    case PORT_LEN_20PT:
        g_point_limit = 20;
        break;
    default:
        g_game_length = PORT_LEN_UNLIMITED;
        break;
    }
}

static void check_over(void)
{
    int i;
    if (g_over)
        return;
    if (g_time_limit > 0 && g_elapsed >= g_time_limit)
        g_over = 1;
    if (g_point_limit > 0) {
        for (i = 0; i < PORT_MP_SEATS; i++) {
            if (g_kill_counts[i] >= g_point_limit)
                g_over = 1;
        }
    }
}

void port_score_reset(void)
{
    int i;
    port_score_set_scenario(PORT_SCENARIO_NORMAL);
    g_kill_count = 0;
    g_kills_this_life = 0;
    for (i = 0; i < PORT_MP_SEATS; i++)
        g_kill_counts[i] = 0;
    g_elapsed = 0;
    g_over = 0;
}

void port_score_set_scenario(int scenario)
{
    g_scenario = scenario;
}

void port_score_configure(int scenario, uint32_t game_length)
{
    port_score_set_scenario(scenario);
    apply_length(game_length);
    g_elapsed = 0;
    g_over = 0;
}

void port_score_add_kill(void)
{
    int seat = port_cur_player();
    g_kill_count += 1;
    g_kills_this_life += 1;
    if (seat < 0 || seat >= PORT_MP_SEATS)
        seat = 0;
    g_kill_counts[seat] += 1;
    check_over();
}

void port_score_tick(void)
{
    if (g_over)
        return;
    if (g_time_limit > 0)
        g_elapsed += 1;
    check_over();
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

int port_score_game_length(void) { return g_game_length; }

int port_score_remain_ticks(void)
{
    if (g_time_limit <= 0)
        return 0;
    if (g_elapsed >= g_time_limit)
        return 0;
    return g_time_limit - g_elapsed;
}

int port_score_over(void) { return g_over; }

int port_score_winner(void)
{
    int i, best = -1, best_k = 0, ties = 0;
    for (i = 0; i < PORT_MP_SEATS; i++) {
        if (g_kill_counts[i] > best_k) {
            best_k = g_kill_counts[i];
            best = i;
            ties = 0;
        } else if (g_kill_counts[i] == best_k && best_k > 0) {
            ties = 1;
        }
    }
    if (best_k <= 0 || ties)
        return -1;
    return best;
}
