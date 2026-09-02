#ifndef SILVERIRIS_PORT_PLAYER_H
#define SILVERIRIS_PORT_PLAYER_H

#include <stdint.h>

#ifndef PORT_MAX_PLAYERS
#define PORT_MAX_PLAYERS 4
#endif
#define PORT_ENV_PLAYERS_1 0
#define PORT_ENV_PLAYERS_2 200
#define PORT_ENV_PLAYERS_3 300
#define PORT_ENV_PLAYERS_4 400
/* N64 CONT_E / CONT_D. Held C-up raises phi (look up). */
#define PORT_C_UP 0x0008u
#define PORT_C_DOWN 0x0004u
/* CONT_L: WASD A/D is strafe, not turn (mouse-look). */
#define PORT_STRAFE 0x0020u
/* CONT_R: hold-Shift run. Lockstep pad bit. Rare speedboost caps at 1.25
 * after 3s of full analog; keyboard Shift is a dedicated sprint. */
#define PORT_RUN 0x0010u
#define PORT_RUN_MUL 1.9f
/* Analog/70 * 1.08 * dt is not MoveBond. On-foot displacement is the
 * walk-anim root translate (bondheadmatrices[0].m[3] * dt). A cycle is
 * ~one body length (185u) over frames 9.5–27, which is ~4.5× analog*dt. */
#define PORT_WALK_MUL 4.5f
/* Gait cycle ~185u / two steps. Accumulated xz after clip. */
#define PORT_STEP_DIST 90.0f
#define PORT_PITCH_MAX 70.0f
/* Quantized look on the pad: degrees = q / PORT_LOOK_Q. */
#define PORT_LOOK_Q 10

void port_player_spawn(void);
void port_player_set_pose(float x, float y, float z, float theta);
void port_player_set_pose_at(int seat, float x, float y, float z, float theta);
/* Stage intro: seat 0 at this pose; extra seats k_spawn if on-tile, else
 * the next walkable offset. begin_match re-applies this origin. */
void port_player_set_spawn_origin(float x, float y, float z, float theta);
void port_player_clear_spawn_origin(void);
void port_player_set_y(float y);
void port_player_set_pitch(float phi);
void port_set_look_delta(int seat, float yaw_deg, float pitch_deg);
void port_player_tick(int8_t stick_x, int8_t stick_y, uint16_t buttons);
void port_set_local_pad(int seat, int8_t x, int8_t y, uint16_t buttons);
void port_set_local_look(int seat, int8_t yaw_q, int8_t pitch_q);
void port_get_local_pad(int8_t *x, int8_t *y, uint16_t *buttons);

void port_set_player_count(int n);
int port_player_count(void);
int port_env_players(void);
void port_set_cur_player(int seat);
int port_cur_player(void);
void port_viewport(int seat, int *left, int *top, int *width, int *height);

/* Remote presenter: full-frame Hor+ for one seat. Local split-screen does not use this. */
void port_set_view_seat(int seat);
int port_view_seat(void);
int port_view_unsplit(void);
void currentPlayerSetScreenSize(float width, float height);
void currentPlayerSetScreenPosition(float left, float top);
void currentPlayerSetPerspective(float near, float fovy, float aspect);
float port_screen_width(void);
float port_screen_height(void);
float port_screen_left(void);
float port_screen_top(void);
float port_persp_near(void);
float port_persp_fovy(void);
float port_persp_aspect(void);
float port_view_hfov(void);

int port_player_spawned(void);
/* Campaign-sized i32 so a hitscan is visible on the HUD. */
#define PORT_PLAYER_HEALTH_MAX 8
#define PORT_PLAYER_ARMOUR_MAX 8
#define PORT_PLAYER_RADIUS 30.0f
int port_player_health(void);
int port_player_health_at(int seat);
int port_player_armour(void);
void port_player_add_armour(int amount);
void port_player_damage(int amount);
/* MP: 20 ticks (1 s) Z-rising respawn; 40 ticks auto. */
#define PORT_RESPAWN_Z_TICKS 20
#define PORT_RESPAWN_AUTO_TICKS 40
int port_player_dead_ticks(void);
int port_player_dead_ticks_at(int seat);
void port_player_respawn_seat(int seat);
/* Cylinder at the current seat (local xz, eye-relative height). */
int port_player_ray_hit(float ox, float oy, float oz, float dx, float dy, float dz,
                        float *t_out);
float port_player_x(void);
float port_player_y(void);
float port_player_z(void);
float port_player_theta(void);
float port_player_phi(void);
void port_player_look_dir(float *dx, float *dy, float *dz);
float port_player_x_at(int seat);
float port_player_y_at(int seat);
float port_player_z_at(int seat);
float port_player_theta_at(int seat);
float port_player_phi_at(int seat);

#endif
