#ifndef SILVERIRIS_PORT_API_H
#define SILVERIRIS_PORT_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define PORT_KEEP EMSCRIPTEN_KEEPALIVE
#else
#define PORT_KEEP
#endif

typedef enum {
    PORT_OK = 0,
    PORT_E_ASSETS = 1,
    PORT_E_HASH = 2,
    PORT_E_STATE = 3,
    PORT_E_DESYNC = 4,
    PORT_E_OOM = 5
} PortErr;

PORT_KEEP PortErr port_api_init(const uint8_t *pack, uint32_t pack_len, const uint8_t pack_hash[32]);
PORT_KEEP void port_api_shutdown(void);
PORT_KEEP const uint8_t *port_api_fb(void);
PORT_KEEP int port_api_fb_width(void);
PORT_KEEP int port_api_fb_height(void);
PORT_KEEP void port_api_draw(void);
#define PORT_DRAW_NONE 0
#define PORT_DRAW_STAGE 1
#define PORT_DRAW_FALLBACK 2
PORT_KEEP int port_api_last_draw(void);
PORT_KEEP const char *port_api_last_error(void);
PORT_KEEP int port_api_ready(void);

PORT_KEEP void port_api_audio_cb(int16_t *stereo, int nframes);
PORT_KEEP void port_api_audio_play_gun(void);
PORT_KEEP void port_api_audio_set_music(int on);
PORT_KEEP int port_api_audio_rate(void);

PORT_KEEP int port_api_load_stage(int level_id);
PORT_KEEP int port_api_sim_tick(uint32_t tick);
PORT_KEEP int port_api_clock_timer(void);
PORT_KEEP int port_api_stage_rooms(void);
PORT_KEEP int port_api_bg_rooms(void);
PORT_KEEP int port_api_gdl_raw(void);
PORT_KEEP int port_api_gdl_c0(void);
PORT_KEEP int port_api_gdl_vtx(void);
PORT_KEEP int port_api_portal_count(void);
PORT_KEEP int port_api_current_room(void);
PORT_KEEP int port_api_rooms_walked(void);
PORT_KEEP unsigned port_api_fb_nonzero(void);
PORT_KEEP unsigned port_api_settex(void);
PORT_KEEP unsigned port_api_tex_ok(void);
PORT_KEEP unsigned port_api_tex_miss(void);
PORT_KEEP unsigned port_api_tex_miss_absent(void);
PORT_KEEP unsigned port_api_tex_miss_decode(void);
PORT_KEEP int port_api_pack_files(void);

PORT_KEEP void port_api_set_pad(int seat, int x, int y, int buttons);
PORT_KEEP void port_api_set_player_count(int n);
PORT_KEEP int port_api_player_count(void);
PORT_KEEP int port_api_env_players(void);
PORT_KEEP float port_api_player_x(void);
PORT_KEEP float port_api_player_y(void);
PORT_KEEP float port_api_player_z(void);
PORT_KEEP float port_api_player_theta(void);
PORT_KEEP float port_api_player_phi(void);
PORT_KEEP void port_api_set_look_delta(int seat, float yaw_deg, float pitch_deg);
PORT_KEEP float port_api_player_x_at(int seat);
PORT_KEEP float port_api_player_z_at(int seat);
PORT_KEEP float port_api_player_theta_at(int seat);
PORT_KEEP float port_api_player_phi_at(int seat);
PORT_KEEP int port_api_vp_left(int seat);
PORT_KEEP int port_api_vp_top(int seat);
PORT_KEEP int port_api_vp_width(int seat);
PORT_KEEP int port_api_vp_height(int seat);

PORT_KEEP int port_api_gun_mag(void);
PORT_KEEP int port_api_gun_reserve(void);
PORT_KEEP int port_api_gun_weapon(void);
PORT_KEEP int port_api_gun_hits(void);
PORT_KEEP int port_api_gun_have_hit(void);
PORT_KEEP float port_api_gun_hit_x(void);
PORT_KEEP float port_api_gun_hit_y(void);
PORT_KEEP float port_api_gun_hit_z(void);
PORT_KEEP uint32_t port_api_crc_players(void);

PORT_KEEP int port_api_chr_count(void);
PORT_KEEP float port_api_chr_x(void);
PORT_KEEP float port_api_chr_z(void);
PORT_KEEP float port_api_chr_theta(void);
PORT_KEEP int port_api_chr_action(void);
PORT_KEEP uint32_t port_api_crc_chrs(void);

PORT_KEEP int port_api_kills(void);
PORT_KEEP int port_api_kill_counts(int seat);
PORT_KEEP void port_api_configure_match(int scenario, uint32_t game_length);
PORT_KEEP int port_api_score_remain(void);
PORT_KEEP int port_api_score_over(void);
PORT_KEEP int port_api_score_winner(void);
PORT_KEEP int port_api_dead_ticks(void);
/* Packed i32 [mag, reserve, hits, kills, health]. JS reads getInt32/HEAP32, not HEAPF32. */
PORT_KEEP int32_t *port_api_hud_i32(void);
PORT_KEEP int port_api_health(void);
PORT_KEEP int port_api_armour(void);
PORT_KEEP int port_api_guard_los(void);
PORT_KEEP int port_api_guard_shots(void);
PORT_KEEP int port_api_setup_guards(void);
PORT_KEEP float port_api_setup_guard_x(int i);
PORT_KEEP float port_api_setup_guard_z(int i);
PORT_KEEP int port_api_setup_guard_dead(int i);
PORT_KEEP int port_api_stan_tiles(void);
PORT_KEEP int port_api_stan_on_tile(void);
PORT_KEEP uint32_t port_api_crc_objectives(void);
PORT_KEEP uint32_t port_api_crc_props(void);
PORT_KEEP uint32_t port_api_rng_lo(void);
PORT_KEEP uint32_t port_api_chr_rng_lo(void);

PORT_KEEP void port_api_begin_match(int nseats, uint32_t rng_seed);

PORT_KEEP void port_api_set_view_seat(int seat);
PORT_KEEP int port_api_view_seat(void);
PORT_KEEP int port_api_view_unsplit(void);
PORT_KEEP void port_api_set_screen_size(float width, float height);
PORT_KEEP void port_api_set_screen_position(float left, float top);
PORT_KEEP void port_api_set_perspective(float near, float fovy, float aspect);
PORT_KEEP float port_api_view_hfov(void);


#endif
