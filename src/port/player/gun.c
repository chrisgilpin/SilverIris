#include "gun.h"

#include "chr/patrol.h"
#include "move.h"
#include "mp/score.h"
#include "stan_walk.h"

#include <math.h>
#include <string.h>

__attribute__((weak)) void port_prop_hear_player_shot(void) {}
__attribute__((weak)) void port_audio_play_gun(void) {}
__attribute__((weak)) void port_audio_play_dry(void) {}
__attribute__((weak)) void port_audio_play_fall(void) {}
__attribute__((weak)) void port_audio_play_hit(void) {}
__attribute__((weak)) void port_audio_play_rico(void) {}
__attribute__((weak)) void port_audio_play_kf7(void) {}
__attribute__((weak)) void port_audio_play_pickup(void) {}
__attribute__((weak)) void port_audio_play_reload(void) {}
__attribute__((weak)) void port_audio_play_yelp(void) {}
__attribute__((weak)) void port_audio_set_sfx_pan(uint8_t pan) { (void)pan; }
__attribute__((weak)) void port_audio_set_sfx_vol(uint8_t vol) { (void)vol; }
__attribute__((weak)) void port_prop_viewgun_sync(void) {}
__attribute__((weak)) int port_prop_chr_ray_hit(float ox, float oy, float oz, float dx,
                                                float dy, float dz, float *t_out)
{
    (void)ox;
    (void)oy;
    (void)oz;
    (void)dx;
    (void)dy;
    (void)dz;
    (void)t_out;
    return 0;
}
__attribute__((weak)) int port_prop_chr_hit_xz(float *x, float *z)
{
    if (x)
        *x = 0.f;
    if (z)
        *z = 0.f;
    return -1;
}

/*
 * PP7 / KF7 slice of gunfire.c until that file compiles.
 * wppk_stats: AmmoType AMMO_9MM, MagSize 7.
 * ak47_stats: AmmoType AMMO_RIFLE, MagSize 30.
 * Fire: weapon_ammo_in_magazine -= 1 (gunfire.c) on Z_TRIG (CONT_G 0x2000)
 * rising edge. Hitscan is eye + look vs closed door slabs, stan tile
 * exits, and guard cylinders. A patrol or setup-guard hit kill+scores
 * (one-shot; setup body is then skipped — no ragdoll). Fake z=-50 only
 * if no stan is loaded. Damage stays 1 (PORT_PP7_DAMAGE).
 */
typedef struct {
    int32_t ammo[PORT_AMMO_SLOTS];
    int mag;
    int weapon;
    uint16_t prev_buttons;
    int hits;
    float hit_x, hit_y, hit_z;
    int have_hit;
    int suppress_fire;
    int flash_frames;
    int last_action;
} PortGun;

static PortGun g_gun[PORT_MAX_PLAYERS];

static PortGun *G(void) { return &g_gun[port_cur_player()]; }

static int weapon_ammo_type(int weapon)
{
    if (weapon == PORT_WEAPON_KF7)
        return PORT_AMMO_RIFLE;
    return PORT_AMMO_9MM;
}

static int weapon_mag_size(int weapon)
{
    if (weapon == PORT_WEAPON_KF7)
        return PORT_KF7_MAG;
    if (weapon == PORT_WEAPON_MP5K)
        return PORT_MP5K_MAG;
    return PORT_PP7_MAG;
}

static void reload(void)
{
    PortGun *g = G();
    int take;
    int at = weapon_ammo_type(g->weapon);
    int space = weapon_mag_size(g->weapon) - g->mag;
    if (space <= 0)
        return;
    take = g->ammo[at];
    if (take > space)
        take = space;
    if (take <= 0)
        return;
    g->ammo[at] -= take;
    g->mag += take;
}

/* Mixer pan 0..127 from listener xz vs source. Center-unity at 64.
 * Not ASP HLE. */
static uint8_t sfx_pan_at(float sx, float sz)
{
    float fx, fy, fz;
    float dx, dz, side, fwd, ang;
    int pan;

    port_player_look_dir(&fx, &fy, &fz);
    dx = sx - port_player_x();
    dz = sz - port_player_z();
    side = dx * fz - dz * fx;
    fwd = dx * fx + dz * fz;
    if (side * side + fwd * fwd < 1.0f)
        return 64u;
    ang = atan2f(side, fwd);
    pan = 64 + (int)(ang * (63.0f / 3.14159265f) + (ang >= 0.f ? 0.5f : -0.5f));
    if (pan < 0)
        pan = 0;
    if (pan > 127)
        pan = 127;
    return (uint8_t)pan;
}

/* Mixer vol 0..127 from listener xz. Full within 400u so stall
 * first-enemy (~230u) stays loud. Floor 32 at 4000u. Not ASP HLE. */
static uint8_t sfx_vol_at(float sx, float sz)
{
    float dx, dz, d, t;
    int vol;

    dx = sx - port_player_x();
    dz = sz - port_player_z();
    d = sqrtf(dx * dx + dz * dz);
    if (d <= 400.f)
        return 127u;
    if (d >= 4000.f)
        return 32u;
    t = (d - 400.f) / (4000.f - 400.f);
    vol = 127 - (int)(t * (127 - 32) + 0.5f);
    if (vol < 32)
        vol = 32;
    if (vol > 127)
        vol = 127;
    return (uint8_t)vol;
}

static void play_world(void (*fn)(void), float sx, float sz)
{
    if (!fn)
        return;
    port_audio_set_sfx_pan(sfx_pan_at(sx, sz));
    port_audio_set_sfx_vol(sfx_vol_at(sx, sz));
    fn();
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
    int src = 0; /* 1 stan, 2 patrol, 3 fake z=-50, 4 floor, 5 other seat, 6 chr vis */
    int hit_seat = -1;
    int shooter = port_cur_player();
    int seat, n;

    port_player_look_dir(&dx, &dy, &dz);
    if (port_stan_ray_hit(ox, oy, oz, dx, dy, dz, &t) && t < t_best) {
        t_best = t;
        src = 1;
    }
    /* Drawn setup-chr body (pose AABB), not only the 30u pad cylinder. */
    if (port_prop_chr_ray_hit(ox, oy, oz, dx, dy, dz, &t) && t < t_best) {
        t_best = t;
        src = 6;
    }
    if (port_chr_ray_hit(ox, oy, oz, dx, dy, dz, &t) && t < t_best) {
        t_best = t;
        src = 2;
    }
    n = port_player_count();
    for (seat = 0; seat < n; seat++) {
        if (seat == shooter)
            continue;
        port_set_cur_player(seat);
        if (port_player_health() <= 0) {
            port_set_cur_player(shooter);
            continue;
        }
        if (port_player_ray_hit(ox, oy, oz, dx, dy, dz, &t) && t < t_best) {
            t_best = t;
            src = 5;
            hit_seat = seat;
        }
        port_set_cur_player(shooter);
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
        {
            int guard = (src == 1) ? port_stan_ray_hit_guard() : 0;
            if ((src == 2 || src == 6 || guard || src == 5) && port_audio_play_hit)
                play_world(port_audio_play_hit, g->hit_x, g->hit_z);
            else if ((src == 1 || src == 3 || src == 4) && port_audio_play_rico)
                play_world(port_audio_play_rico, g->hit_x, g->hit_z);
            if ((src == 2 || src == 6 || guard) && port_audio_play_yelp)
                play_world(port_audio_play_yelp, g->hit_x, g->hit_z);
        }
        if (src == 2) {
            port_chr_kill();
            port_score_add_kill();
            if (port_audio_play_fall)
                play_world(port_audio_play_fall, g->hit_x, g->hit_z);
        } else if (src == 6) {
            float hx = 0.f, hz = 0.f;
            if (port_prop_chr_hit_xz(&hx, &hz) == 0)
                port_stan_mark_guard_at(hx, hz);
            port_score_add_kill();
            if (port_audio_play_fall)
                play_world(port_audio_play_fall, g->hit_x, g->hit_z);
        } else if (src == 1 && port_stan_ray_hit_guard()) {
            port_stan_mark_ray_guard();
            port_score_add_kill();
            if (port_audio_play_fall)
                play_world(port_audio_play_fall, g->hit_x, g->hit_z);
        } else if (src == 5 && hit_seat >= 0) {
            int dead;
            port_set_cur_player(hit_seat);
            port_player_damage(PORT_PP7_DAMAGE);
            dead = port_player_health() <= 0;
            port_set_cur_player(shooter);
            if (dead) {
                port_score_add_kill();
                if (port_audio_play_fall)
                    play_world(port_audio_play_fall, g->hit_x, g->hit_z);
            }
        }
    }
}

void port_gun_suppress_fire(void)
{
    G()->suppress_fire = 1;
}

void port_gun_reset_seat(int seat)
{
    if (seat < 0 || seat >= PORT_MAX_PLAYERS)
        return;
    memset(&g_gun[seat], 0, sizeof g_gun[seat]);
    g_gun[seat].weapon = PORT_WEAPON_PP7;
    g_gun[seat].ammo[PORT_AMMO_9MM] = PORT_PP7_RESERVE;
    g_gun[seat].mag = PORT_PP7_MAG;
    if (seat == port_cur_player() && port_prop_viewgun_sync)
        port_prop_viewgun_sync();
}

void port_gun_reset(void)
{
    int i;
    memset(g_gun, 0, sizeof g_gun);
    for (i = 0; i < PORT_MAX_PLAYERS; i++)
        port_gun_reset_seat(i);
}

void port_gun_tick(uint16_t buttons)
{
    PortGun *g = G();
    int rising = ((buttons & PORT_FIRE_MASK) != 0) && ((g->prev_buttons & PORT_FIRE_MASK) == 0);
    int suppress = g->suppress_fire;
    g->suppress_fire = 0;
    g->prev_buttons = buttons;
    if (g->flash_frames > 0)
        g->flash_frames--;
    if (!rising)
        return;
    /* Door use used to share Z; A is interact now. Keep suppress for
     * a same-tick fire+use mash. Still latch prev. */
    if (suppress)
        return;
    if (port_player_health() <= 0)
        return;
    if (g->mag <= 0) {
        reload();
        if (g->mag > 0) {
            g->last_action = PORT_GUN_ACT_RELOAD;
            if (port_audio_play_reload)
                port_audio_play_reload();
            return;
        }
        /* Empty mag + empty reserve: dry click, no muzzle, no shot. */
        g->last_action = PORT_GUN_ACT_DRY;
        g->flash_frames = 0;
        if (port_audio_play_dry)
            port_audio_play_dry();
        return;
    }
    g->mag -= 1;
    g->flash_frames = PORT_MUZZLE_FLASH_FRAMES;
    g->last_action = PORT_GUN_ACT_SHOT;
    fire_hitscan();
    if (g->weapon == PORT_WEAPON_KF7) {
        if (port_audio_play_kf7)
            port_audio_play_kf7();
    } else if (port_audio_play_gun)
        port_audio_play_gun();
    if (port_prop_hear_player_shot)
        port_prop_hear_player_shot();
}

int port_gun_flash_frames(void) { return G()->flash_frames; }

int port_gun_last_action(void) { return G()->last_action; }

int32_t *port_ammoheldarr(void) { return G()->ammo; }
int port_gun_weapon(void) { return G()->weapon; }
int port_gun_ammo_type(void) { return weapon_ammo_type(G()->weapon); }
int port_gun_mag_size(void) { return weapon_mag_size(G()->weapon); }

void port_gun_hold(float *x, float *y, float *z)
{
    int w = G()->weapon;
    if (x) {
        if (w == PORT_WEAPON_KF7)
            *x = PORT_KF7_HOLD_X;
        else if (w == PORT_WEAPON_MP5K)
            *x = PORT_MP5K_HOLD_X;
        else
            *x = PORT_PP7_HOLD_X;
    }
    if (y) {
        if (w == PORT_WEAPON_KF7)
            *y = PORT_KF7_HOLD_Y;
        else if (w == PORT_WEAPON_MP5K)
            *y = PORT_MP5K_HOLD_Y;
        else
            *y = PORT_PP7_HOLD_Y;
    }
    if (z) {
        if (w == PORT_WEAPON_KF7)
            *z = PORT_KF7_HOLD_Z;
        else if (w == PORT_WEAPON_MP5K)
            *z = PORT_MP5K_HOLD_Z;
        else
            *z = PORT_PP7_HOLD_Z;
    }
}
int port_gun_mag(void) { return G()->mag; }
int port_gun_reserve(void) { return G()->ammo[weapon_ammo_type(G()->weapon)]; }

void port_gun_add_reserve(int n)
{
    if (n <= 0)
        return;
    G()->ammo[PORT_AMMO_9MM] += n;
}

void port_gun_collect_model(int model)
{
    PortGun *g = G();
    int at;
    int next;

    if (model == PORT_GUN_MODEL_KF7)
        next = PORT_WEAPON_KF7;
    else if (model == PORT_GUN_MODEL_MP5K)
        next = PORT_WEAPON_MP5K;
    else
        return;
    at = weapon_ammo_type(g->weapon);
    g->ammo[at] += g->mag;
    g->mag = 0;
    g->weapon = next;
    g->ammo[weapon_ammo_type(next)] += PORT_GUN_PICKUP_ADD;
    reload();
    if (port_audio_play_pickup)
        port_audio_play_pickup();
    if (port_prop_viewgun_sync)
        port_prop_viewgun_sync();
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
