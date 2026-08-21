#include "gun.h"

#include "chr/patrol.h"
#include "move.h"
#include "mp/score.h"

#include <math.h>
#include <string.h>

/*
 * PP7 slice of gunfire.c until that file compiles.
 * wppk_stats: AmmoType AMMO_9MM, MagSize 7.
 * Fire: weapon_ammo_in_magazine -= 1 (gunfire.c) on Z_TRIG (CONT_G 0x2000)
 * rising edge. Hitscan is a PORT wall at z=-50, not stan mesh.
 */
#define PI_F 3.1415927f

typedef struct {
    int32_t ammo[PORT_AMMO_SLOTS];
    int mag;
    uint16_t prev_buttons;
    int hits;
    float hit_x, hit_y, hit_z;
    int have_hit;
    int suppress_fire;
} PortGun;

static PortGun g_gun[PORT_MAX_PLAYERS];

static PortGun *G(void) { return &g_gun[port_cur_player()]; }

static void reload(void)
{
    PortGun *g = G();
    int take;
    int space = PORT_PP7_MAG - g->mag;
    if (space <= 0)
        return;
    take = g->ammo[PORT_AMMO_9MM];
    if (take > space)
        take = space;
    if (take <= 0)
        return;
    g->ammo[PORT_AMMO_9MM] -= take;
    g->mag += take;
}

static void fire_hitscan(void)
{
    float th = port_player_theta() * (PI_F / 180.0f);
    float ox = port_player_x();
    float oy = port_player_y();
    float oz = port_player_z();
    /* Look-forward matches stick-up walk: theta=0 → dir (0,0,-1). */
    float dx = sinf(th);
    float dz = -cosf(th);
    float t_wall = 1.0e9f;
    float t_chr = 1.0e9f;
    float t;

    if (dz != 0.0f) {
        t = (PORT_WALL_Z - oz) / dz;
        if (t >= 0.05f && t <= 4000.0f)
            t_wall = t;
    }
    if (port_chr_ray_hit(ox, oz, dx, dz, &t))
        t_chr = t;
    if (t_chr < t_wall && t_chr < 1.0e8f) {
        PortGun *g = G();
        g->hit_x = ox + dx * t_chr;
        g->hit_y = oy;
        g->hit_z = oz + dz * t_chr;
        g->have_hit = 1;
        g->hits += 1;
        port_chr_kill();
        port_score_add_kill();
        return;
    }
    if (t_wall > 4000.0f)
        return;
    {
        PortGun *g = G();
        g->hit_x = ox + dx * t_wall;
        g->hit_y = oy;
        g->hit_z = oz + dz * t_wall;
        g->have_hit = 1;
        g->hits += 1;
    }
}

void port_gun_suppress_fire(void)
{
    G()->suppress_fire = 1;
}

void port_gun_reset(void)
{
    int i;
    memset(g_gun, 0, sizeof g_gun);
    for (i = 0; i < PORT_MAX_PLAYERS; i++) {
        g_gun[i].ammo[PORT_AMMO_9MM] = PORT_PP7_RESERVE;
        g_gun[i].mag = PORT_PP7_MAG;
    }
}

void port_gun_tick(uint16_t buttons)
{
    PortGun *g = G();
    int rising = ((buttons & PORT_Z_TRIG) != 0) && ((g->prev_buttons & PORT_Z_TRIG) == 0);
    int suppress = g->suppress_fire;
    g->suppress_fire = 0;
    g->prev_buttons = buttons;
    if (!rising)
        return;
    /* Door use consumed this Z (Rare B-activate vs fire). Still latch prev. */
    if (suppress)
        return;
    if (g->mag <= 0) {
        reload();
        return;
    }
    g->mag -= 1;
    fire_hitscan();
}

int32_t *port_ammoheldarr(void) { return G()->ammo; }
int port_gun_mag(void) { return G()->mag; }
int port_gun_reserve(void) { return G()->ammo[PORT_AMMO_9MM]; }
int port_gun_hits(void) { return G()->hits; }

int port_gun_last_hit(float *x, float *y, float *z)
{
    PortGun *g = G();
    if (!g->have_hit)
        return 0;
    if (x)
        *x = g->hit_x;
    if (y)
        *y = g->hit_y;
    if (z)
        *z = g->hit_z;
    return 1;
}
