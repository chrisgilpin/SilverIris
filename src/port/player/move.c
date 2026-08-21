#include "move.h"

#include "stan_walk.h"

#include "chr/patrol.h"
#include "gun.h"
#include "mp/score.h"
#include "overrides/lv_clock.h"

#include <math.h>

/*
 * Analog walk slice of bondviewProcessInput + MoveBond (bondview2.c).
 * NTSC: deadzone 5, analogWalk/70, *1.08, vv_theta += speedtheta * dt * 3.5.
 * Displacement matches tank/on-foot apply: dz += fwd * cos(theta) * dt
 * with theta=0 facing +Z and N64 stick-up (negative Y) walking -Z.
 * Full MoveBond still needs bondview2.c.
 */

#define STICK_DEAD 5
#define STICK_DIV 70.0f
#define FWD_BOOST 1.08f
#define TURN_PER_DT 3.5f
#define LOOK_PER_DT 3.5f
#define PI_F 3.1415927f

/* NTSC viewports from bondview2.c / fr.h. */
#define PORT_VP_FULL_W 320
#define PORT_VP_1P_H 220
#define PORT_VP_SPLIT_W 159
#define PORT_VP_SPLIT_H 109
#define PORT_VP_ULY_TOP 10
#define PORT_VP_ULY_BOT 121
#define PORT_VP_ULX_RIGHT 0xA1
#define PORT_VP_ULY_1P 10

static const float k_spawn_x[PORT_MAX_PLAYERS] = {0.0f, 40.0f, -40.0f, 0.0f};
static const float k_spawn_z[PORT_MAX_PLAYERS] = {0.0f, 20.0f, 20.0f, 40.0f};

typedef struct {
    float x, y, z, theta, phi;
    float look_yaw, look_pitch;
    int8_t pad_x, pad_y;
    uint16_t pad_buttons;
    uint16_t prev_buttons;
    int spawned;
    int32_t health;
    int32_t armour;
} PortPly;

static PortPly g_p[PORT_MAX_PLAYERS];
static int g_nplayers = 1;
static int g_cur;
static int g_view_seat;
static int g_view_unsplit;
static float g_screen_w = (float)PORT_VP_FULL_W;
static float g_screen_h = (float)PORT_VP_1P_H;
static float g_screen_left;
static float g_screen_top;
static float g_persp_near = 30.0f;
static float g_persp_fovy = 60.0f; /* native vi fovy; Hor+ keeps this */
static float g_persp_aspect = 320.0f / 240.0f;

static int analog_deadzone(int v)
{
    if (v < -STICK_DEAD)
        return v + STICK_DEAD;
    if (v > STICK_DEAD)
        return v - STICK_DEAD;
    return 0;
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

/* Last finite eye Y. Spawn / stair NaN must not stick in the look-at. */
static float g_safe_y = PORT_EYE_HEIGHT;

static int y_ok(float v)
{
    return v == v && v < 1.0e20f && v > -1.0e20f;
}

static void store_y(PortPly *p, float y)
{
    if (y_ok(y)) {
        p->y = y;
        g_safe_y = y;
    } else {
        p->y = g_safe_y;
    }
}

void port_set_player_count(int n)
{
    if (n < 1)
        n = 1;
    if (n > PORT_MAX_PLAYERS)
        n = PORT_MAX_PLAYERS;
    g_nplayers = n;
    if (g_cur >= g_nplayers)
        g_cur = 0;
}

int port_player_count(void) { return g_nplayers; }

int port_env_players(void)
{
    if (g_nplayers <= 1)
        return PORT_ENV_PLAYERS_1;
    if (g_nplayers == 2)
        return PORT_ENV_PLAYERS_2;
    if (g_nplayers == 3)
        return PORT_ENV_PLAYERS_3;
    return PORT_ENV_PLAYERS_4;
}

void port_set_cur_player(int seat)
{
    if (seat < 0)
        seat = 0;
    if (seat >= g_nplayers)
        seat = g_nplayers - 1;
    g_cur = seat;
}

int port_cur_player(void) { return g_cur; }

void currentPlayerSetScreenSize(float width, float height)
{
    if (width < 1.0f)
        width = 1.0f;
    if (height < 1.0f)
        height = 1.0f;
    g_screen_w = width;
    g_screen_h = height;
    if (g_screen_h > 0.0f)
        g_persp_aspect = g_screen_w / g_screen_h;
}

void currentPlayerSetScreenPosition(float left, float top)
{
    g_screen_left = left;
    g_screen_top = top;
}

void currentPlayerSetPerspective(float near, float fovy, float aspect)
{
    if (near < 0.01f)
        near = 0.01f;
    if (fovy < 1.0f)
        fovy = 1.0f;
    if (aspect < 0.05f)
        aspect = 0.05f;
    g_persp_near = near;
    g_persp_fovy = fovy;
    g_persp_aspect = aspect;
}

float port_screen_width(void) { return g_screen_w; }
float port_screen_height(void) { return g_screen_h; }
float port_screen_left(void) { return g_screen_left; }
float port_screen_top(void) { return g_screen_top; }
float port_persp_near(void) { return g_persp_near; }
float port_persp_fovy(void) { return g_persp_fovy; }
float port_persp_aspect(void) { return g_persp_aspect; }

float port_view_hfov(void)
{
    float half = g_persp_fovy * (PI_F / 180.0f) * 0.5f;
    return 2.0f * atanf(tanf(half) * g_persp_aspect) * (180.0f / PI_F);
}

void port_set_view_seat(int seat)
{
    if (seat < 0) {
        g_view_unsplit = 0;
        g_view_seat = 0;
        return;
    }
    if (seat >= g_nplayers)
        seat = g_nplayers > 0 ? g_nplayers - 1 : 0;
    g_view_seat = seat;
    g_view_unsplit = 1;
    port_set_cur_player(seat);
}

int port_view_seat(void) { return g_view_seat; }

int port_view_unsplit(void) { return g_view_unsplit; }

void port_viewport(int seat, int *left, int *top, int *width, int *height)
{
    int l = 0, t = PORT_VP_ULY_1P, w = PORT_VP_FULL_W, h = PORT_VP_1P_H;

    if (seat < 0)
        seat = 0;
    if (g_view_unsplit && seat == g_view_seat) {
        l = (int)g_screen_left;
        t = (int)g_screen_top;
        w = (int)g_screen_w;
        h = (int)g_screen_h;
        if (w < 1)
            w = 1;
        if (h < 1)
            h = 1;
        if (left)
            *left = l;
        if (top)
            *top = t;
        if (width)
            *width = w;
        if (height)
            *height = h;
        return;
    }
    if (g_nplayers == 2) {
        w = PORT_VP_FULL_W;
        h = PORT_VP_SPLIT_H;
        t = (seat == 0) ? PORT_VP_ULY_TOP : PORT_VP_ULY_BOT;
    } else if (g_nplayers >= 3) {
        w = PORT_VP_SPLIT_W;
        h = PORT_VP_SPLIT_H;
        if (seat == 1 || seat == 3)
            l = PORT_VP_ULX_RIGHT;
        t = (seat < 2) ? PORT_VP_ULY_TOP : PORT_VP_ULY_BOT;
    }
    if (left)
        *left = l;
    if (top)
        *top = t;
    if (width)
        *width = w;
    if (height)
        *height = h;
}

void port_player_spawn(void)
{
    int i;
    if (g_nplayers < 1)
        g_nplayers = 1;
    g_cur = 0;
    g_view_seat = 0;
    g_view_unsplit = 0;
    for (i = 0; i < PORT_MAX_PLAYERS; i++) {
        g_p[i].x = k_spawn_x[i];
        g_p[i].y = 0.0f;
        g_p[i].z = k_spawn_z[i];
        g_p[i].theta = 0.0f;
        g_p[i].phi = 0.0f;
        g_p[i].look_yaw = 0.0f;
        g_p[i].look_pitch = 0.0f;
        g_p[i].pad_x = 0;
        g_p[i].pad_y = 0;
        g_p[i].pad_buttons = 0;
        g_p[i].prev_buttons = 0;
        g_p[i].spawned = 1;
        g_p[i].health = PORT_PLAYER_HEALTH_MAX;
        g_p[i].armour = 0;
    }
    g_safe_y = PORT_EYE_HEIGHT;
    port_stan_clear_current();
    port_gun_reset();
    port_chr_reset();
    port_score_reset();
}

void port_player_set_pose(float x, float y, float z, float theta)
{
    int i;
    while (theta < 0.0f)
        theta += 360.0f;
    while (theta >= 360.0f)
        theta -= 360.0f;
    for (i = 0; i < PORT_MAX_PLAYERS; i++) {
        g_p[i].x = x + k_spawn_x[i];
        store_y(&g_p[i], y);
        g_p[i].z = z + k_spawn_z[i];
        g_p[i].theta = theta;
        g_p[i].phi = 0.0f;
        g_p[i].look_yaw = 0.0f;
        g_p[i].look_pitch = 0.0f;
        g_p[i].spawned = 1;
    }
    port_stan_clear_current();
}

void port_player_set_y(float y)
{
    store_y(&g_p[g_cur], y);
}

void port_player_set_pitch(float phi)
{
    int i;
    phi = clampf(phi, -PORT_PITCH_MAX, PORT_PITCH_MAX);
    for (i = 0; i < PORT_MAX_PLAYERS; i++)
        g_p[i].phi = phi;
}

void port_set_look_delta(int seat, float yaw_deg, float pitch_deg)
{
    if (seat < 0 || seat >= PORT_MAX_PLAYERS)
        return;
    g_p[seat].look_yaw += yaw_deg;
    g_p[seat].look_pitch += pitch_deg;
}

void port_set_local_pad(int seat, int8_t x, int8_t y, uint16_t buttons)
{
    if (seat < 0 || seat >= PORT_MAX_PLAYERS)
        return;
    g_p[seat].pad_x = x;
    g_p[seat].pad_y = y;
    g_p[seat].pad_buttons = buttons;
}

void port_get_local_pad(int8_t *x, int8_t *y, uint16_t *buttons)
{
    if (x)
        *x = g_p[g_cur].pad_x;
    if (y)
        *y = g_p[g_cur].pad_y;
    if (buttons)
        *buttons = g_p[g_cur].pad_buttons;
}

void port_player_tick(int8_t stick_x, int8_t stick_y, uint16_t buttons)
{
    PortPly *p = &g_p[g_cur];
    float dt = g_GlobalTimerDelta;
    float walk, turn, fwd, rad;

    if (!p->spawned)
        return;
    if (p->health <= 0)
        return;

    walk = (float)analog_deadzone((int)stick_y) / STICK_DIV;
    turn = (float)analog_deadzone((int)stick_x) / STICK_DIV;
    walk = clampf(walk, -1.0f, 1.0f);
    turn = clampf(turn, -1.0f, 1.0f);
    fwd = walk * FWD_BOOST;

    p->theta += p->look_yaw + turn * TURN_PER_DT * dt;
    p->look_yaw = 0.0f;
    while (p->theta < 0.0f)
        p->theta += 360.0f;
    while (p->theta >= 360.0f)
        p->theta -= 360.0f;

    p->phi += p->look_pitch;
    p->look_pitch = 0.0f;
    if (buttons & PORT_C_UP)
        p->phi += LOOK_PER_DT * dt;
    if (buttons & PORT_C_DOWN)
        p->phi -= LOOK_PER_DT * dt;
    p->phi = clampf(p->phi, -PORT_PITCH_MAX, PORT_PITCH_MAX);

    rad = p->theta * (PI_F / 180.0f);
    {
        float ox = p->x, oz = p->z, nx, nz, ny;
        p->x += fwd * -sinf(rad) * dt;
        p->z += fwd * cosf(rad) * dt;
        if (port_stan_ready()) {
            nx = p->x;
            nz = p->z;
            ny = p->y;
            port_stan_clip_step(ox, oz, &nx, &nz, &ny);
            p->x = nx;
            p->z = nz;
            store_y(p, ny);
        }
    }
    /* Rare bond_pressed_reload_activate -> propdoorInteract. HUD is Z/Space. */
    if ((buttons & PORT_Z_TRIG) && !(p->prev_buttons & PORT_Z_TRIG)) {
        float lx = sinf(rad);
        float lz = -cosf(rad);
        if (port_stan_use_door(p->x, p->z, lx, lz))
            port_gun_suppress_fire();
    }
    p->prev_buttons = buttons;
}

int port_player_health(void) { return g_p[g_cur].health; }

int port_player_armour(void) { return g_p[g_cur].armour; }

void port_player_add_armour(int amount)
{
    if (amount <= 0)
        return;
    g_p[g_cur].armour += amount;
    if (g_p[g_cur].armour > PORT_PLAYER_ARMOUR_MAX)
        g_p[g_cur].armour = PORT_PLAYER_ARMOUR_MAX;
}

void port_player_damage(int amount)
{
    if (amount <= 0)
        return;
    if (g_p[g_cur].health <= 0)
        return;
    if (g_p[g_cur].armour > 0) {
        if (amount >= g_p[g_cur].armour) {
            amount -= g_p[g_cur].armour;
            g_p[g_cur].armour = 0;
        } else {
            g_p[g_cur].armour -= amount;
            return;
        }
    }
    if (amount <= 0)
        return;
    if (amount >= g_p[g_cur].health)
        g_p[g_cur].health = 0;
    else
        g_p[g_cur].health -= amount;
}

int port_player_ray_hit(float ox, float oy, float oz, float dx, float dy, float dz,
                        float *t_out)
{
    float cx = g_p[g_cur].x;
    float cz = g_p[g_cur].z;
    float y0 = g_p[g_cur].y - PORT_EYE_HEIGHT;
    float y1 = g_p[g_cur].y + 10.0f;
    float fx, fz, a, b, c, disc, t, hy, r;

    r = PORT_PLAYER_RADIUS;
    fx = ox - cx;
    fz = oz - cz;
    a = dx * dx + dz * dz;
    if (a < 1.0e-12f)
        return 0;
    b = 2.0f * (fx * dx + fz * dz);
    c = fx * fx + fz * fz - r * r;
    disc = b * b - 4.0f * a * c;
    if (disc < 0.0f)
        return 0;
    t = (-b - sqrtf(disc)) / (2.0f * a);
    if (t < 0.05f)
        t = (-b + sqrtf(disc)) / (2.0f * a);
    if (t < 0.05f || t > 4000.0f)
        return 0;
    hy = oy + dy * t;
    if (hy < y0 || hy > y1)
        return 0;
    if (t_out)
        *t_out = t;
    return 1;
}

int port_player_spawned(void) { return g_p[g_cur].spawned; }
float port_player_x(void) { return g_p[g_cur].x; }
float port_player_y(void) { return g_p[g_cur].y; }
float port_player_z(void) { return g_p[g_cur].z; }
float port_player_theta(void) { return g_p[g_cur].theta; }
float port_player_phi(void) { return g_p[g_cur].phi; }

void port_player_look_dir(float *dx, float *dy, float *dz)
{
    float th = g_p[g_cur].theta * (PI_F / 180.0f);
    float ph = g_p[g_cur].phi * (PI_F / 180.0f);
    float cph = cosf(ph);
    if (dx)
        *dx = sinf(th) * cph;
    if (dy)
        *dy = sinf(ph);
    if (dz)
        *dz = -cosf(th) * cph;
}

static int clamp_seat(int seat)
{
    if (seat < 0)
        return 0;
    if (seat >= PORT_MAX_PLAYERS)
        return PORT_MAX_PLAYERS - 1;
    return seat;
}

float port_player_x_at(int seat) { return g_p[clamp_seat(seat)].x; }
float port_player_y_at(int seat) { return g_p[clamp_seat(seat)].y; }
float port_player_z_at(int seat) { return g_p[clamp_seat(seat)].z; }
float port_player_theta_at(int seat) { return g_p[clamp_seat(seat)].theta; }
float port_player_phi_at(int seat) { return g_p[clamp_seat(seat)].phi; }
