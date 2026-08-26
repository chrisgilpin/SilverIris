#include "checksum.h"

#include "chr/patrol.h"
#include "mp/score.h"
#include "player/gun.h"
#include "player/move.h"
#include "player/stan_walk.h"
#include "rng/random.h"

#include <string.h>

/* Strong bodies in prop.c. Weak empty record in prop_ck_stubs.c. */
int port_prop_guard_count(void);
int port_prop_guard_xz(int i, float *x, float *z);
int port_prop_guard_yaw(int i, float *yaw, int *alerted);
int port_prop_guard_alerted(void);
int port_prop_pickup_pad(void);
int port_prop_pickup_kind(void);
int port_prop_pickup_hidden(void);
int port_prop_pickup_xyz(float *x, float *y, float *z);
int port_prop_drop_model(void);
int port_prop_drop_hidden(void);
int port_prop_drop_xyz(float *x, float *y, float *z);
int port_prop_drop_count(void);
int port_prop_drop_model_at(int i);
int port_prop_drop_hidden_at(int i);
int port_prop_drop_xyz_at(int i, float *x, float *y, float *z);

/* CRC32C Castagnoli (reflected poly 0x82F63B78). Never fileGenerateCRC. */

uint32_t port_crc32c(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xffffffffu;
    uint32_t i, b;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (b = 0; b < 8; b++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0x82F63B78u : 0u);
    }
    return crc ^ 0xffffffffu;
}

static void wr_f32(uint8_t *p, float v)
{
    uint32_t u;
    memcpy(&u, &v, 4);
    p[0] = (uint8_t)u;
    p[1] = (uint8_t)(u >> 8);
    p[2] = (uint8_t)(u >> 16);
    p[3] = (uint8_t)(u >> 24);
}

static void wr_i32(uint8_t *p, int32_t v)
{
    uint32_t u = (uint32_t)v;
    p[0] = (uint8_t)u;
    p[1] = (uint8_t)(u >> 8);
    p[2] = (uint8_t)(u >> 16);
    p[3] = (uint8_t)(u >> 24);
}

void port_checksum(uint32_t tick, SimChecksum *out)
{
    uint8_t buf[16 + 4 * PORT_AMMO_SLOTS + 16 + 4 * PORT_MAX_PLAYERS * 44];
    uint32_t o = 0;
    int i, s, saved, n;
    int32_t *ammo;

    if (!out)
        return;
    memset(out, 0, sizeof *out);
    out->tick = tick;
    out->rng_lo = (uint32_t)g_randomSeed;
    out->rng_hi = (uint32_t)(g_randomSeed >> 32);
    out->chr_rng_lo = (uint32_t)g_chrObjRandomSeed;
    out->chr_rng_hi = (uint32_t)(g_chrObjRandomSeed >> 32);

    saved = port_cur_player();
    n = port_player_count();
    wr_i32(buf + o, (int32_t)n);
    o += 4;
    for (s = 0; s < n; s++) {
        port_set_cur_player(s);
        wr_f32(buf + o, port_player_x());
        o += 4;
        wr_f32(buf + o, port_player_y());
        o += 4;
        wr_f32(buf + o, port_player_z());
        o += 4;
        wr_f32(buf + o, port_player_theta());
        o += 4;
        wr_f32(buf + o, port_player_phi());
        o += 4;
        ammo = port_ammoheldarr();
        for (i = 0; i < PORT_AMMO_SLOTS; i++) {
            wr_i32(buf + o, ammo[i]);
            o += 4;
        }
        wr_i32(buf + o, (int32_t)port_gun_mag());
        o += 4;
        wr_i32(buf + o, (int32_t)port_gun_weapon());
        o += 4;
        wr_i32(buf + o, port_player_health() <= 0 ? 1 : 0); /* bonddead */
        o += 4;
        wr_f32(buf + o, (float)port_player_health() / (float)PORT_PLAYER_HEALTH_MAX);
        o += 4;
        wr_f32(buf + o, (float)port_player_armour() / (float)PORT_PLAYER_ARMOUR_MAX);
        o += 4;
        wr_i32(buf + o, (int32_t)port_player_dead_ticks());
        o += 4;
    }
    port_set_cur_player(saved);
    out->crc_players = port_crc32c(buf, o);

    o = 0;
    wr_f32(buf + o, port_chr_x());
    o += 4;
    wr_f32(buf + o, port_chr_y());
    o += 4;
    wr_f32(buf + o, port_chr_z());
    o += 4;
    wr_f32(buf + o, port_chr_theta());
    o += 4;
    wr_f32(buf + o, port_chr_health());
    o += 4;
    wr_i32(buf + o, (int32_t)port_chr_action());
    o += 4;
    wr_i32(buf + o, (int32_t)port_chr_nextstep());
    o += 4;
    out->crc_chrs = port_crc32c(buf, o);

    {
        /* doors + stan cylinders + setup-guard xz/alert/dead + pad-215 + KF7 drop */
        uint8_t pbuf[4 + 128 * 8 + 4 + 128 * 12 + 4 + 128 * 16 + 80 + 32 * 24];
        int nd = port_stan_door_count();
        int ng = port_stan_guard_count();
        int np = port_prop_guard_count();
        uint32_t po = 0;
        float gx, gy, gz, yaw;
        int alerted;

        if (nd > 128)
            nd = 128;
        if (ng > 128)
            ng = 128;
        if (np > 128)
            np = 128;
        wr_i32(pbuf + po, (int32_t)nd);
        po += 4;
        for (i = 0; i < nd; i++) {
            wr_i32(pbuf + po, (int32_t)port_stan_door_is_open(i));
            po += 4;
            wr_f32(pbuf + po, port_stan_door_frac(i));
            po += 4;
        }
        wr_i32(pbuf + po, (int32_t)ng);
        po += 4;
        for (i = 0; i < ng; i++) {
            gx = gz = 0.f;
            (void)port_stan_guard_xz(i, &gx, &gz);
            wr_f32(pbuf + po, gx);
            po += 4;
            wr_f32(pbuf + po, gz);
            po += 4;
            wr_i32(pbuf + po, (int32_t)port_stan_guard_was_hit(i));
            po += 4;
        }
        wr_i32(pbuf + po, (int32_t)np);
        po += 4;
        wr_i32(pbuf + po, (int32_t)port_prop_guard_alerted());
        po += 4;
        for (i = 0; i < np; i++) {
            gx = gz = yaw = 0.f;
            alerted = 0;
            (void)port_prop_guard_xz(i, &gx, &gz);
            (void)port_prop_guard_yaw(i, &yaw, &alerted);
            wr_f32(pbuf + po, gx);
            po += 4;
            wr_f32(pbuf + po, gz);
            po += 4;
            wr_i32(pbuf + po, alerted);
            po += 4;
            wr_i32(pbuf + po, port_stan_guard_dead_at(gx, gz));
            po += 4;
        }
        wr_i32(pbuf + po, port_prop_pickup_pad());
        po += 4;
        wr_i32(pbuf + po, port_prop_pickup_kind());
        po += 4;
        wr_i32(pbuf + po, port_prop_pickup_hidden());
        po += 4;
        gx = gy = gz = 0.f;
        (void)port_prop_pickup_xyz(&gx, &gy, &gz);
        wr_f32(pbuf + po, gx);
        po += 4;
        wr_f32(pbuf + po, gy);
        po += 4;
        wr_f32(pbuf + po, gz);
        po += 4;
        wr_i32(pbuf + po, port_prop_drop_model());
        po += 4;
        wr_i32(pbuf + po, port_prop_drop_hidden());
        po += 4;
        gx = gy = gz = 0.f;
        (void)port_prop_drop_xyz(&gx, &gy, &gz);
        wr_f32(pbuf + po, gx);
        po += 4;
        wr_f32(pbuf + po, gy);
        po += 4;
        wr_f32(pbuf + po, gz);
        po += 4;
        {
            int nd = port_prop_drop_count();
            /* n<=1 keeps the last-wins bytes above bit-identical. */
            if (nd > 1) {
                int di;
                if (nd > 32)
                    nd = 32;
                wr_i32(pbuf + po, (int32_t)nd);
                po += 4;
                for (di = 0; di < nd; di++) {
                    wr_i32(pbuf + po, port_prop_drop_model_at(di));
                    po += 4;
                    wr_i32(pbuf + po, port_prop_drop_hidden_at(di));
                    po += 4;
                    gx = gy = gz = 0.f;
                    (void)port_prop_drop_xyz_at(di, &gx, &gy, &gz);
                    wr_f32(pbuf + po, gx);
                    po += 4;
                    wr_f32(pbuf + po, gy);
                    po += 4;
                    wr_f32(pbuf + po, gz);
                    po += 4;
                }
            }
        }
        out->crc_props = port_crc32c(pbuf, po);
    }

    o = 0;
    wr_i32(buf + o, (int32_t)port_score_scenario());
    o += 4;
    wr_i32(buf + o, (int32_t)port_score_kills());
    o += 4;
    wr_i32(buf + o, (int32_t)port_score_kills_this_life());
    o += 4;
    for (i = 0; i < PORT_MP_SEATS; i++) {
        wr_i32(buf + o, (int32_t)port_score_kill_counts(i));
        o += 4;
    }
    wr_i32(buf + o, (int32_t)port_score_game_length());
    o += 4;
    wr_i32(buf + o, (int32_t)port_score_remain_ticks());
    o += 4;
    wr_i32(buf + o, (int32_t)port_score_over());
    o += 4;
    wr_i32(buf + o, (int32_t)port_score_winner());
    o += 4;
    out->crc_objectives = port_crc32c(buf, o);
}
