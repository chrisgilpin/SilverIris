#include "gun.h"

#include "chr/patrol.h"
#include "move.h"
#include "mp/score.h"
#include "stan_walk.h"

#include <math.h>
#include <string.h>

void port_prop_hear_player_shot(void) __attribute__((weak));

/*
 * PP7 slice of gunfire.c until that file compiles.
 * wppk_stats: AmmoType AMMO_9MM, MagSize 7.
 * Fire: weapon_ammo_in_magazine -= 1 (gunfire.c) on Z_TRIG (CONT_G 0x2000)
 * rising edge. Hitscan is eye + look vs closed door slabs, stan tile
 * exits, and guard cylinders. A patrol or setup-guard hit kill+scores
 * (one-shot; setup body is then skipped — no ragdoll). Fake z=-50 only
 * if no stan is loaded.
 */
typedef struct {
    int32_t ammo[PORT_AMMO_SLOTS];
    int mag;
    uint16_t prev_buttons;
    int hits;
    float hit_x, hit_y, hit_z;
    int have_hit;
    int suppress_fire;
    int flash_frames;
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
    float ox = port_player_x();
    float oy = port_player_y();
    float oz = port_player_z();
    /* 3D look: phi=0 is (sin θ, 0, -cos θ). Door/guard use the same t
     * as the floor ray so a high/low shot can miss a standing cylinder. */
    float dx, dy, dz;
    float t_best = 1.0e9f;
    float t;
    int src = 0; /* 1 stan, 2 patrol, 3 fake z=-50, 4 floor */

    port_player_look_dir(&dx, &dy, &dz);
    if (port_stan_ray_hit(ox, oy, oz, dx, dy, dz, &t) && t < t_best) {
        t_best = t;
        src = 1;
    }
    if (port_chr_ray_hit(ox, oy, oz, dx, dy, dz, &t) && t < t_best) {
        t_best = t;
        src = 2;
    }
    /* Fake PORT wall only for no_assets / empty synthetic (no tiles/doors). */
    if (!port_stan_ready() && dz != 0.0f) {
        t = (PORT_WALL_Z - oz) / dz;
        if (t >= 0.05f && t <= 4000.0f && t < t_best) {
            t_best = t;
            src = 3;
        }
    }
    {
        float ey;
        if (port_stan_eye_y(ox, oz, &ey) == 0 && dy != 0.0f) {
            float fy = ey - PORT_EYE_HEIGHT;
            t = (fy - oy) / dy;
            if (t >= 0.05f && t <= 4000.0f && t < t_best) {
                t_best = t;
                src = 4;
            }
        }
    }
    if (src == 0 || t_best > 4000.0f)
        return;
    {
        PortGun *g = G();
        g->hit_x = ox + dx * t_best;
        g->hit_y = oy + dy * t_best;
        g->hit_z = oz + dz * t_best;
        g->have_hit = 1;
        g->hits += 1;
        if (src == 2) {
            port_chr_kill();
            port_score_add_kill();
        } else if (src == 1 && port_stan_ray_hit_guard()) {
            port_stan_mark_ray_guard();
            port_score_add_kill();
        }
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
    if (g->flash_frames > 0)
        g->flash_frames--;
    if (!rising)
        return;
    /* Door use consumed this Z (Rare B-activate vs fire). Still latch prev. */
    if (suppress)
        return;
    if (port_player_health() <= 0)
        return;
    if (g->mag <= 0) {
        reload();
        return;
    }
    g->mag -= 1;
    g->flash_frames = PORT_MUZZLE_FLASH_FRAMES;
    fire_hitscan();
    if (port_prop_hear_player_shot)
        port_prop_hear_player_shot();
}

int port_gun_flash_frames(void) { return G()->flash_frames; }

int32_t *port_ammoheldarr(void) { return G()->ammo; }
int port_gun_mag(void) { return G()->mag; }
int port_gun_reserve(void) { return G()->ammo[PORT_AMMO_9MM]; }

void port_gun_add_reserve(int n)
{
    if (n <= 0)
        return;
    G()->ammo[PORT_AMMO_9MM] += n;
}
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
