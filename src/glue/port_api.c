#include "port_api.h"

#include "audio/audio.h"
#include "fs/pack_dma.h"
#include "fs/prop.h"
#include "fs/stage.h"
#include "gfx/gbi_interp.h"
#include "gfx/tmem.h"
#include "gfx/sw_raster.h"
#include "overrides/lv_clock.h"
#include "chr/patrol.h"
#include "det/checksum.h"
#include "mp/score.h"
#include "player/gun.h"
#include "player/move.h"
#include "player/stan_walk.h"
#include "rng/random.h"
#include "vi/sim_tick.h"

#include <stdlib.h>
#include <string.h>

static char g_err[160];
static int g_ready;
static int g_last_draw;
static uint8_t *g_pack_copy;

static void set_err(const char *s)
{
    size_t n = strlen(s);
    if (n >= sizeof g_err)
        n = sizeof g_err - 1;
    memcpy(g_err, s, n);
    g_err[n] = 0;
}

PORT_KEEP PortErr port_api_init(const uint8_t *pack, uint32_t pack_len, const uint8_t pack_hash[32])
{
    int rc;
    const uint8_t *got;
    uint8_t *copy;

    g_ready = 0;
    g_err[0] = 0;
    if (!pack || pack_len == 0 || !pack_hash) {
        set_err("missing pack or hash");
        return PORT_E_ASSETS;
    }

    copy = (uint8_t *)malloc(pack_len);
    if (!copy) {
        set_err("oom copying pack");
        return PORT_E_OOM;
    }
    memcpy(copy, pack, pack_len);

    port_api_shutdown();
    g_pack_copy = copy;

    rc = port_init(g_pack_copy, pack_len);
    if (rc != PORT_PACK_OK) {
        set_err("pack failed validation");
        port_api_shutdown();
        return PORT_E_ASSETS;
    }
    got = port_pack_hash();
    if (!got || memcmp(got, pack_hash, 32) != 0) {
        set_err("packHash mismatch");
        port_api_shutdown();
        return PORT_E_HASH;
    }
    if (g1_run_synthetic() != 0) {
        set_err("g1 raster failed");
        port_api_shutdown();
        return PORT_E_STATE;
    }
    port_audio_init();
    port_audio_load_pack_sfx();
    port_audio_load_pack_music();
    g_ready = 1;
    g_last_draw = PORT_DRAW_NONE;
    return PORT_OK;
}

PORT_KEEP void port_api_shutdown(void)
{
    port_stage_unload();
    port_audio_unload_pack_sfx();
    port_audio_unload_pack_instruments();
    port_audio_unload_seq();
    port_audio_shutdown();
    port_shutdown();
    free(g_pack_copy);
    g_pack_copy = NULL;
    g_ready = 0;
    g_last_draw = PORT_DRAW_NONE;
}

PORT_KEEP const uint8_t *port_api_fb(void) { return g1_fb_rgba(); }

PORT_KEEP int port_api_fb_width(void) { return G1_FB_W; }

PORT_KEEP int port_api_fb_height(void) { return G1_FB_H; }

PORT_KEEP void port_api_draw(void)
{
    if (!g_ready) {
        g_last_draw = PORT_DRAW_NONE;
        return;
    }
    if (port_stage_draw() == 0) {
        g_last_draw = PORT_DRAW_STAGE;
        return;
    }
    g1_run_synthetic();
    g_last_draw = PORT_DRAW_FALLBACK;
}

PORT_KEEP int port_api_last_draw(void) { return g_last_draw; }

PORT_KEEP const char *port_api_last_error(void) { return g_err; }

PORT_KEEP int port_api_ready(void) { return g_ready; }

PORT_KEEP void port_api_audio_cb(int16_t *stereo, int nframes)
{
    port_audio_cb(stereo, nframes);
}

PORT_KEEP void port_api_audio_play_gun(void)
{
    port_audio_play_gun();
}

PORT_KEEP void port_api_audio_play_dry(void)
{
    port_audio_play_dry();
}

PORT_KEEP int port_api_audio_last_sfx(void)
{
    return port_audio_last_sfx();
}

PORT_KEEP void port_api_audio_set_music(int on)
{
    port_audio_set_placeholder_music(on);
}

PORT_KEEP int port_api_audio_seq_on(void)
{
    return port_audio_seq_on();
}

PORT_KEEP int port_api_audio_inst_on(void)
{
    return port_audio_inst_on();
}

PORT_KEEP int port_api_audio_env_on(void)
{
    return port_audio_env_on();
}

PORT_KEEP int port_api_audio_pan_on(void)
{
    return port_audio_pan_on();
}

PORT_KEEP int port_api_audio_rate(void)
{
    return (int)port_audio_rate();
}

PORT_KEEP int port_api_load_stage(int level_id)
{
    int rc;
    if (!g_ready) {
        set_err("engine not ready");
        return PORT_STAGE_ERR_PACK;
    }
    rc = port_stage_load(level_id);
    if (rc != PORT_STAGE_OK) {
        const char *s = port_stage_last_error();
        set_err(s && s[0] ? s : "stage load failed");
    }
    return rc;
}

PORT_KEEP int port_api_sim_tick(uint32_t tick)
{
    return port_sim_tick(tick);
}

PORT_KEEP int port_api_clock_timer(void) { return (int)g_ClockTimer; }

PORT_KEEP int port_api_stage_rooms(void) { return port_stage_room_count(); }

PORT_KEEP int port_api_bg_rooms(void) { return port_stage_bg_rooms(); }

PORT_KEEP int port_api_gdl_raw(void) { return port_stage_gdl_raw(); }

PORT_KEEP int port_api_gdl_c0(void) { return port_stage_gdl_c0(); }

PORT_KEEP int port_api_gdl_vtx(void) { return port_stage_gdl_vtx(); }

PORT_KEEP int port_api_portal_count(void) { return port_stage_portal_count(); }
PORT_KEEP int port_api_current_room(void) { return port_stage_current_room(); }
PORT_KEEP int port_api_rooms_walked(void) { return port_stage_rooms_walked(); }

PORT_KEEP unsigned port_api_fb_nonzero(void) { return g1_fb_nonzero(); }

PORT_KEEP unsigned port_api_settex(void) { return g1_tex_settex_count(); }
PORT_KEEP unsigned port_api_tex_ok(void) { return g1_tex_ok_count(); }
PORT_KEEP unsigned port_api_tex_miss(void) { return g1_tex_miss_count(); }
PORT_KEEP unsigned port_api_tex_miss_absent(void) { return g1_tex_miss_absent_count(); }
PORT_KEEP unsigned port_api_tex_miss_decode(void) { return g1_tex_miss_decode_count(); }

PORT_KEEP int port_api_pack_files(void) { return (int)port_pack_file_count(); }

PORT_KEEP void port_api_set_pad(int seat, int x, int y, int buttons)
{
    int8_t sx = (int8_t)x;
    int8_t sy = (int8_t)y;
    if (x > 127)
        sx = 127;
    if (x < -128)
        sx = -128;
    if (y > 127)
        sy = 127;
    if (y < -128)
        sy = -128;
    port_set_local_pad(seat, sx, sy, (uint16_t)buttons);
}

PORT_KEEP void port_api_set_player_count(int n) { port_set_player_count(n); }
PORT_KEEP int port_api_player_count(void) { return port_player_count(); }
PORT_KEEP int port_api_env_players(void) { return port_env_players(); }

PORT_KEEP float port_api_player_x(void) { return port_player_x(); }
PORT_KEEP float port_api_player_y(void) { return port_player_y(); }
PORT_KEEP float port_api_player_z(void) { return port_player_z(); }
PORT_KEEP float port_api_player_theta(void) { return port_player_theta(); }
PORT_KEEP float port_api_player_phi(void) { return port_player_phi(); }
PORT_KEEP void port_api_set_look_delta(int seat, float yaw_deg, float pitch_deg)
{
    port_set_look_delta(seat, yaw_deg, pitch_deg);
}
PORT_KEEP float port_api_player_x_at(int seat) { return port_player_x_at(seat); }
PORT_KEEP float port_api_player_z_at(int seat) { return port_player_z_at(seat); }
PORT_KEEP float port_api_player_theta_at(int seat) { return port_player_theta_at(seat); }
PORT_KEEP float port_api_player_phi_at(int seat) { return port_player_phi_at(seat); }

PORT_KEEP int port_api_vp_left(int seat)
{
    int l, t, w, h;
    port_viewport(seat, &l, &t, &w, &h);
    return l;
}
PORT_KEEP int port_api_vp_top(int seat)
{
    int l, t, w, h;
    port_viewport(seat, &l, &t, &w, &h);
    return t;
}
PORT_KEEP int port_api_vp_width(int seat)
{
    int l, t, w, h;
    port_viewport(seat, &l, &t, &w, &h);
    return w;
}
PORT_KEEP int port_api_vp_height(int seat)
{
    int l, t, w, h;
    port_viewport(seat, &l, &t, &w, &h);
    return h;
}

PORT_KEEP int port_api_gun_mag(void) { return port_gun_mag(); }
PORT_KEEP int port_api_gun_reserve(void) { return port_gun_reserve(); }
PORT_KEEP int port_api_gun_weapon(void) { return port_gun_weapon(); }
PORT_KEEP int port_api_gun_hits(void) { return port_gun_hits(); }
PORT_KEEP int port_api_gun_flash_frames(void) { return port_gun_flash_frames(); }
PORT_KEEP int port_api_gun_last_action(void) { return port_gun_last_action(); }

PORT_KEEP int port_api_gun_have_hit(void)
{
    float x, y, z;
    return port_gun_last_hit(&x, &y, &z);
}

PORT_KEEP float port_api_gun_hit_x(void)
{
    float x, y, z;
    if (!port_gun_last_hit(&x, &y, &z))
        return 0.0f;
    return x;
}

PORT_KEEP float port_api_gun_hit_y(void)
{
    float x, y, z;
    if (!port_gun_last_hit(&x, &y, &z))
        return 0.0f;
    return y;
}

PORT_KEEP float port_api_gun_hit_z(void)
{
    float x, y, z;
    if (!port_gun_last_hit(&x, &y, &z))
        return 0.0f;
    return z;
}

PORT_KEEP uint32_t port_api_crc_players(void)
{
    SimChecksum cs;
    port_checksum(0, &cs);
    return cs.crc_players;
}

PORT_KEEP int port_api_chr_count(void) { return port_chr_count(); }
PORT_KEEP float port_api_chr_x(void) { return port_chr_x(); }
PORT_KEEP float port_api_chr_z(void) { return port_chr_z(); }
PORT_KEEP float port_api_chr_theta(void) { return port_chr_theta(); }
PORT_KEEP int port_api_chr_action(void) { return port_chr_action(); }

PORT_KEEP uint32_t port_api_crc_chrs(void)
{
    SimChecksum cs;
    port_checksum(0, &cs);
    return cs.crc_chrs;
}

static int32_t g_hud_i32[5];

PORT_KEEP int port_api_kills(void) { return port_score_kills(); }

PORT_KEEP int port_api_kill_counts(int seat) { return port_score_kill_counts(seat); }

PORT_KEEP void port_api_configure_match(int scenario, uint32_t game_length)
{
    port_score_configure(scenario, game_length);
}

PORT_KEEP int port_api_score_remain(void) { return port_score_remain_ticks(); }

PORT_KEEP int port_api_score_over(void) { return port_score_over(); }

PORT_KEEP int port_api_score_winner(void) { return port_score_winner(); }

PORT_KEEP int port_api_dead_ticks(void) { return port_player_dead_ticks(); }

PORT_KEEP int port_api_health(void) { return port_player_health(); }

PORT_KEEP int port_api_armour(void) { return port_player_armour(); }

PORT_KEEP int port_api_guard_los(void) { return port_prop_guard_los(); }

PORT_KEEP int port_api_guard_shots(void) { return port_prop_guard_shots(); }

PORT_KEEP int port_api_setup_guards(void) { return port_prop_guard_count(); }

PORT_KEEP float port_api_setup_guard_x(int i)
{
    float x = 0.f, z = 0.f;
    if (port_prop_guard_xz(i, &x, &z) != 0)
        return 0.f;
    return x;
}

PORT_KEEP float port_api_setup_guard_z(int i)
{
    float x = 0.f, z = 0.f;
    if (port_prop_guard_xz(i, &x, &z) != 0)
        return 0.f;
    return z;
}

PORT_KEEP int port_api_setup_guard_dead(int i)
{
    float x = 0.f, z = 0.f;
    if (port_prop_guard_xz(i, &x, &z) != 0)
        return 1;
    return port_stan_guard_dead_at(x, z);
}

PORT_KEEP int32_t *port_api_hud_i32(void)
{
    g_hud_i32[0] = (int32_t)port_gun_mag();
    g_hud_i32[1] = (int32_t)port_gun_reserve();
    g_hud_i32[2] = (int32_t)port_gun_hits();
    g_hud_i32[3] = (int32_t)port_score_kills();
    g_hud_i32[4] = (int32_t)port_player_health();
    return g_hud_i32;
}

PORT_KEEP int port_api_stan_tiles(void) { return port_stan_tile_count(); }

PORT_KEEP int port_api_stan_on_tile(void)
{
    return port_stan_on_tile(port_player_x(), port_player_z());
}

PORT_KEEP uint32_t port_api_crc_objectives(void)
{
    SimChecksum cs;
    port_checksum(0, &cs);
    return cs.crc_objectives;
}

PORT_KEEP uint32_t port_api_crc_props(void)
{
    SimChecksum cs;
    port_checksum(0, &cs);
    return cs.crc_props;
}

PORT_KEEP uint32_t port_api_rng_lo(void)
{
    SimChecksum cs;
    port_checksum(0, &cs);
    return cs.rng_lo;
}

PORT_KEEP uint32_t port_api_chr_rng_lo(void)
{
    SimChecksum cs;
    port_checksum(0, &cs);
    return cs.chr_rng_lo;
}

PORT_KEEP void port_api_begin_match(int nseats, uint32_t rng_seed)
{
    port_rng_begin_match(rng_seed);
    port_set_player_count(nseats);
    port_player_spawn();
}

PORT_KEEP void port_api_set_view_seat(int seat)
{
    port_set_view_seat(seat);
}

PORT_KEEP int port_api_view_seat(void)
{
    return port_view_seat();
}

PORT_KEEP int port_api_view_unsplit(void)
{
    return port_view_unsplit();
}

PORT_KEEP void port_api_set_screen_size(float width, float height)
{
    currentPlayerSetScreenSize(width, height);
}

PORT_KEEP void port_api_set_screen_position(float left, float top)
{
    currentPlayerSetScreenPosition(left, top);
}

PORT_KEEP void port_api_set_perspective(float near, float fovy, float aspect)
{
    currentPlayerSetPerspective(near, fovy, aspect);
}

PORT_KEEP float port_api_view_hfov(void)
{
    return port_view_hfov();
}
