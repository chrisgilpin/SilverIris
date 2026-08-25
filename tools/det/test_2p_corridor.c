#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "det/checksum.h"
#include "det/tape.h"
#include "net/lockstep.h"
#include "player/gun.h"
#include "player/move.h"
#include "player/stan_walk.h"
#include "vi/sim_tick.h"

/*
 * M2: 2P lockstep on a synthetic on-tile corridor. Walk, Z-unlatch a
 * door, one PvP shot. No ROM. Replay checksums must match.
 */

#define NFRAMES 48
#define DOOR_X 150.0f

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

static void wr_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void wr_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void wr_s16(uint8_t *p, int v)
{
    wr_be16(p, (uint16_t)(int16_t)v);
}

static void build_corridor_stan(uint8_t *s, size_t n)
{
    memset(s, 0, n);
    wr_be32(s + 4, 0x0F000080u);
    s[0x80 + 2] = 1;
    s[0x80 + 3] = 1;
    wr_be16(s + 0x80 + 6, (uint16_t)((4u << 12) | (0u << 8) | (1u << 4) | 2u));
    wr_s16(s + 0x88 + 0, 0);
    wr_s16(s + 0x88 + 2, 50);
    wr_s16(s + 0x88 + 4, -50);
    wr_s16(s + 0x90 + 0, 400);
    wr_s16(s + 0x90 + 2, 50);
    wr_s16(s + 0x90 + 4, -50);
    wr_s16(s + 0x98 + 0, 400);
    wr_s16(s + 0x98 + 2, 50);
    wr_s16(s + 0x98 + 4, 50);
    wr_s16(s + 0xA0 + 0, 0);
    wr_s16(s + 0xA0 + 2, 50);
    wr_s16(s + 0xA0 + 4, 50);
}

static int setup_world(void)
{
    uint8_t stan[256];
    float y;

    port_stan_unload();
    build_corridor_stan(stan, sizeof stan);
    port_stan_set_scale(1.0f);
    port_stan_set_world_origin(0.0f, 0.0f, 0.0f);
    if (port_stan_load(stan, sizeof stan) != PORT_STAN_OK)
        return fail("stan");
    port_stan_clear_doors();
    port_stan_clear_guards();
    port_stan_add_door(DOOR_X, 0.0f, 1.0f, 0.0f);
    if (port_stan_eye_y(60.0f, 0.0f, &y) != 0)
        return fail("eye");
    port_begin_match(2, 1);
    port_player_set_pose_at(0, 60.0f, y, 0.0f, 90.0f);
    port_player_set_pose_at(1, 280.0f, y, 0.0f, 270.0f);
    return 0;
}

/* Tick 0 idle (delay-1). 1–20 P0 walk +X. 21 Z-unlatch. 22–24 idle
 * while the slab swings. 25–40 walk through. 41 PvP Z. Rest idle. */
static void pads_for(uint32_t t, PortPad out[2])
{
    memset(out, 0, sizeof(PortPad) * 2);
    if (t >= 1 && t <= 20)
        out[0].stick_y = (int8_t)-70;
    else if (t == 21)
        out[0].buttons = (uint16_t)PORT_Z_TRIG;
    else if (t >= 25 && t <= 40)
        out[0].stick_y = (int8_t)-70;
    else if (t == 41)
        out[0].buttons = (uint16_t)PORT_Z_TRIG;
}

static int run_script(SimChecksum *last, TapeFrame *fr)
{
    uint32_t t;
    PortPad pads[2];

    port_lockstep_begin(2, 1);
    for (t = 0; t < NFRAMES; t++) {
        pads_for(t, pads);
        if (port_lockstep_submit(t, 0, pads[0].stick_x, pads[0].stick_y,
                pads[0].buttons, 0) != 1)
            return fail("submit 0");
        if (port_lockstep_submit(t, 1, pads[1].stick_x, pads[1].stick_y,
                pads[1].buttons, 0) != 1)
            return fail("submit 1");
        if (port_lockstep_run() != 1)
            return fail("run");
        if (fr) {
            fr[t].tick = t;
            fr[t].pads[0] = pads[0];
            fr[t].pads[1] = pads[1];
            port_checksum(t, &fr[t].cs);
        }
    }
    if (last)
        port_checksum(NFRAMES, last);
    return 0;
}

int main(int argc, char **argv)
{
    SimChecksum a, b;
    TapeHeader h;
    TapeFrame fr[NFRAMES];
    uint8_t *bytes = NULL;
    size_t len = 0;
    uint32_t props0;
    const char *out_path;
    int hp1, hits0;

    if (setup_world() != 0)
        return 1;
    port_checksum(0, &a);
    props0 = a.crc_props;
    if (run_script(&a, fr) != 0)
        return 1;

    if (!port_stan_door_is_open(0) && port_stan_door_frac(0) <= 0.f)
        return fail("door still shut");
    if (a.crc_props == props0)
        return fail("door must change crc_props");
    if (!(port_player_x_at(0) > DOOR_X)) {
        fprintf(stderr, "P0 x=%g want > %g\n", (double)port_player_x_at(0),
            (double)DOOR_X);
        return fail("P0 did not walk through");
    }
    port_set_cur_player(0);
    hits0 = port_gun_hits();
    port_set_cur_player(1);
    hp1 = port_player_health();
    if (hits0 < 1)
        return fail("P0 PvP miss");
    if (hp1 != PORT_PLAYER_HEALTH_MAX - PORT_PP7_DAMAGE) {
        fprintf(stderr, "P1 hp=%d hits=%d\n", hp1, hits0);
        return fail("P1 took one shot");
    }
    if (port_player_x_at(1) < 270.f)
        return fail("P1 should stay");

    if (setup_world() != 0)
        return 1;
    if (run_script(&b, NULL) != 0)
        return 1;
    if (a.crc_players != b.crc_players || a.crc_props != b.crc_props
        || a.crc_objectives != b.crc_objectives || a.rng_lo != b.rng_lo)
        return fail("replay crc");
    if (port_player_x_at(0) != port_player_x_at(0))
        return fail("nan");
    {
        float x0 = port_player_x_at(0);
        if (setup_world() != 0)
            return 1;
        if (run_script(&b, NULL) != 0)
            return 1;
        if (port_player_x_at(0) != x0)
            return fail("replay pos");
    }

    memset(&h, 0, sizeof h);
    h.magic = PORT_TAPE_MAGIC;
    h.version = PORT_TAPE_VERSION;
    h.rng_seed = 1;
    h.nseats = 2;
    h.nframes = NFRAMES;
    if (port_tape_build(&h, fr, &bytes, &len) != 0)
        return fail("tape build");
    out_path = argc >= 2 ? argv[1] : NULL;
    if (out_path) {
        FILE *f = fopen(out_path, "wb");
        if (!f)
            return fail("tape open");
        if (fwrite(bytes, 1, len, f) != len) {
            fclose(f);
            return fail("tape write");
        }
        fclose(f);
    }
    free(bytes);
    printf("2p-corridor ok P0x=%.1f door_frac=%.2f P1hp=%d hits=%d crc=%08x props=%08x\n",
        (double)port_player_x_at(0), (double)port_stan_door_frac(0), hp1, hits0,
        a.crc_players, a.crc_props);
    return 0;
}
