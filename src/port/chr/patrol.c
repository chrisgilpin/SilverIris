#include "patrol.h"

#include "overrides/lv_clock.h"
#include "rng/random.h"

#include <math.h>

/*
 * Patrol slice of set_actor_on_path / chrlvTickPatrol until chr.c and
 * chraction.c compile. Path flags bit 0 = loop (chrlvPatrolCalculateStep).
 * Arrive range 30 (chrlvIsArrivingLaterallyAtPos). waydata.age =
 * randomGetNext() % 100 (set_actor_on_path). Walk is toward the pad at
 * PORT_CHR_WALK * g_GlobalTimerDelta; matching D_80030984 is anim-table
 * derived. Pads are PORT coords, not setup waypoints.
 */

#define PI_F 3.1415927f
#define PORT_CHR_WALK 1.0f
#define PORT_PATH_LEN 4

static const float k_pad_x[PORT_PATH_LEN] = {-80.0f, 80.0f, 80.0f, -80.0f};
static const float k_pad_z[PORT_PATH_LEN] = {-20.0f, -20.0f, 80.0f, 80.0f};
static const int32_t k_path_data[] = {0, 1, 2, 3, -1};

static int g_alive;
static int g_action;
static int g_nextstep;
static int g_forward;
static uint8_t g_path_flags;
static uint16_t g_path_len;
static int g_age;
static float g_x, g_y, g_z, g_theta;
static float g_health;

static int patrol_step(int *forward, int numsteps)
{
    int nextstep = g_nextstep;
    int isforward = *forward;

    if (numsteps < 0) {
        isforward = !isforward;
        numsteps = -numsteps;
    }
    while (numsteps > 0) {
        numsteps--;
        if (isforward) {
            nextstep++;
            if (k_path_data[nextstep] < 0) {
                nextstep -= 2;
                if (g_path_flags & PORT_PATH_LOOP)
                    nextstep = 0;
                else
                    isforward = 0;
            }
        } else {
            nextstep--;
            if (nextstep < 0) {
                nextstep = 1;
                if (g_path_flags & PORT_PATH_LOOP)
                    nextstep = (int)g_path_len - 1;
                else
                    isforward = 1;
            }
        }
    }
    *forward = isforward;
    return nextstep;
}

static int arriving(float tx, float tz)
{
    float dx = g_x - tx;
    float dz = g_z - tz;
    return (dx * dx + dz * dz) <= (PORT_CHR_ARRIVE * PORT_CHR_ARRIVE);
}

void port_chr_reset(void)
{
    g_alive = 1;
    g_action = PORT_ACT_PATROL;
    g_path_flags = PORT_PATH_LOOP;
    g_path_len = PORT_PATH_LEN;
    g_nextstep = 0;
    g_forward = 1;
    g_age = (int)(randomGetNext() % 100u);
    g_x = k_pad_x[0];
    g_y = 0.0f;
    g_z = k_pad_z[0];
    g_theta = 90.0f;
    g_health = 1.0f;
}

void port_chr_kill(void)
{
    if (!g_alive || g_health <= 0.0f)
        return;
    g_action = PORT_ACT_DEAD;
    g_health = 0.0f;
}

int port_chr_ray_hit(float ox, float oz, float dx, float dz, float *t_out)
{
    float fx, fz, a, b, c, disc, t, r;

    if (!g_alive || g_health <= 0.0f)
        return 0;
    r = PORT_CHR_WIDTH;
    fx = ox - g_x;
    fz = oz - g_z;
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
    if (t_out)
        *t_out = t;
    return 1;
}

void port_chr_tick(void)
{
    float tx, tz, dx, dz, dist, step, dt;

    if (!g_alive || g_action != PORT_ACT_PATROL)
        return;
    g_age += 1;
    tx = k_pad_x[k_path_data[g_nextstep]];
    tz = k_pad_z[k_path_data[g_nextstep]];
    if (arriving(tx, tz)) {
        g_nextstep = patrol_step(&g_forward, 1);
        tx = k_pad_x[k_path_data[g_nextstep]];
        tz = k_pad_z[k_path_data[g_nextstep]];
    }
    dx = tx - g_x;
    dz = tz - g_z;
    dist = sqrtf(dx * dx + dz * dz);
    if (dist < 0.001f)
        return;
    g_theta = atan2f(dx, -dz) * (180.0f / PI_F);
    if (g_theta < 0.0f)
        g_theta += 360.0f;
    dt = g_GlobalTimerDelta;
    step = PORT_CHR_WALK * dt;
    if (step > dist)
        step = dist;
    g_x += dx / dist * step;
    g_z += dz / dist * step;
}

int port_chr_count(void) { return g_alive ? 1 : 0; }
int port_chr_action(void) { return g_alive ? g_action : PORT_ACT_INIT; }
int port_chr_nextstep(void) { return g_alive ? g_nextstep : -1; }
float port_chr_x(void) { return g_x; }
float port_chr_y(void) { return g_y; }
float port_chr_z(void) { return g_z; }
float port_chr_theta(void) { return g_theta; }
float port_chr_health(void) { return g_alive ? g_health : 0.0f; }
