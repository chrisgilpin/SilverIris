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

void port_player_spawn(void);
void port_player_set_pose(float x, float y, float z, float theta);
void port_player_tick(int8_t stick_x, int8_t stick_y, uint16_t buttons);
void port_set_local_pad(int seat, int8_t x, int8_t y, uint16_t buttons);
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
float port_player_x(void);
float port_player_y(void);
float port_player_z(void);
float port_player_theta(void);
float port_player_x_at(int seat);
float port_player_y_at(int seat);
float port_player_z_at(int seat);
float port_player_theta_at(int seat);

#endif
