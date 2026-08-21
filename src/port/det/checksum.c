#include "checksum.h"

#include "chr/patrol.h"
#include "mp/score.h"
#include "player/gun.h"
#include "player/move.h"
#include "rng/random.h"

#include <string.h>

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
    uint8_t buf[16 + 4 * PORT_AMMO_SLOTS + 16 + 4 * PORT_MAX_PLAYERS * 40];
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
        wr_i32(buf + o, 0); /* bonddead */
        o += 4;
        wr_f32(buf + o, 1.0f); /* health */
        o += 4;
        wr_f32(buf + o, 0.0f); /* armour */
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
    out->crc_objectives = port_crc32c(buf, o);
}
