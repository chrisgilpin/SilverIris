#include "tape.h"

#include "player/move.h"
#include "rng/random.h"
#include "vi/sim_tick.h"

#include <stdlib.h>
#include <string.h>

static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr_cs(uint8_t *p, const SimChecksum *cs)
{
    wr_u32(p + 0, cs->tick);
    wr_u32(p + 4, cs->rng_lo);
    wr_u32(p + 8, cs->rng_hi);
    wr_u32(p + 12, cs->chr_rng_lo);
    wr_u32(p + 16, cs->chr_rng_hi);
    wr_u32(p + 20, cs->crc_players);
    wr_u32(p + 24, cs->crc_chrs);
    wr_u32(p + 28, cs->crc_props);
    wr_u32(p + 32, cs->crc_objectives);
}

static void rd_cs(const uint8_t *p, SimChecksum *cs)
{
    cs->tick = rd_u32(p + 0);
    cs->rng_lo = rd_u32(p + 4);
    cs->rng_hi = rd_u32(p + 8);
    cs->chr_rng_lo = rd_u32(p + 12);
    cs->chr_rng_hi = rd_u32(p + 16);
    cs->crc_players = rd_u32(p + 20);
    cs->crc_chrs = rd_u32(p + 24);
    cs->crc_props = rd_u32(p + 28);
    cs->crc_objectives = rd_u32(p + 32);
}

static size_t frame_bytes(uint8_t nseats)
{
    return 4u + 4u * (size_t)nseats + 36u;
}

int port_tape_build(const TapeHeader *h, const TapeFrame *frames, uint8_t **out, size_t *out_len)
{
    size_t hdr = 56;
    size_t fb;
    size_t n;
    uint8_t *buf;
    uint32_t i, s;
    uint8_t *p;

    if (!h || !frames || !out || !out_len)
        return -1;
    if (h->nseats == 0 || h->nseats > PORT_MAX_PLAYERS)
        return -1;
    if (h->nframes == 0 || h->nframes > PORT_TAPE_MAX_FRAMES)
        return -1;
    fb = frame_bytes(h->nseats);
    n = hdr + fb * h->nframes;
    buf = (uint8_t *)malloc(n);
    if (!buf)
        return -1;
    memset(buf, 0, n);
    wr_u32(buf + 0, PORT_TAPE_MAGIC);
    wr_u32(buf + 4, PORT_TAPE_VERSION);
    wr_u32(buf + 8, h->region);
    wr_u32(buf + 12, h->rng_seed);
    memcpy(buf + 16, h->pack_hash, 32);
    buf[48] = h->nseats;
    wr_u32(buf + 52, h->nframes);
    p = buf + hdr;
    for (i = 0; i < h->nframes; i++) {
        wr_u32(p, frames[i].tick);
        p += 4;
        for (s = 0; s < h->nseats; s++) {
            p[0] = (uint8_t)frames[i].pads[s].stick_x;
            p[1] = (uint8_t)frames[i].pads[s].stick_y;
            p[2] = (uint8_t)frames[i].pads[s].buttons;
            p[3] = (uint8_t)(frames[i].pads[s].buttons >> 8);
            p += 4;
        }
        wr_cs(p, &frames[i].cs);
        p += 36;
    }
    *out = buf;
    *out_len = n;
    return 0;
}

int port_tape_parse(const uint8_t *bytes, size_t len, TapeHeader *h, TapeFrame **frames)
{
    size_t hdr = 56;
    size_t fb;
    uint32_t i, s;
    const uint8_t *p;
    TapeFrame *fr;

    if (!bytes || !h || !frames || len < hdr)
        return -1;
    memset(h, 0, sizeof *h);
    h->magic = rd_u32(bytes + 0);
    h->version = rd_u32(bytes + 4);
    h->region = rd_u32(bytes + 8);
    h->rng_seed = rd_u32(bytes + 12);
    memcpy(h->pack_hash, bytes + 16, 32);
    h->nseats = bytes[48];
    h->nframes = rd_u32(bytes + 52);
    if (h->magic != PORT_TAPE_MAGIC || h->version != PORT_TAPE_VERSION)
        return -1;
    if (h->nseats == 0 || h->nseats > PORT_MAX_PLAYERS)
        return -1;
    if (h->nframes == 0 || h->nframes > PORT_TAPE_MAX_FRAMES)
        return -1;
    fb = frame_bytes(h->nseats);
    if (len < hdr + fb * h->nframes)
        return -1;
    fr = (TapeFrame *)calloc(h->nframes, sizeof *fr);
    if (!fr)
        return -1;
    p = bytes + hdr;
    for (i = 0; i < h->nframes; i++) {
        fr[i].tick = rd_u32(p);
        p += 4;
        for (s = 0; s < h->nseats; s++) {
            fr[i].pads[s].stick_x = (int8_t)p[0];
            fr[i].pads[s].stick_y = (int8_t)p[1];
            fr[i].pads[s].buttons = (uint16_t)p[2] | ((uint16_t)p[3] << 8);
            p += 4;
        }
        rd_cs(p, &fr[i].cs);
        p += 36;
    }
    *frames = fr;
    return 0;
}

int port_tape_replay(const uint8_t *bytes, size_t len, uint32_t *mismatch_tick)
{
    TapeHeader h;
    TapeFrame *fr = NULL;
    uint32_t i, s;
    SimChecksum got;
    int rc;

    if (mismatch_tick)
        *mismatch_tick = 0;
    if (port_tape_parse(bytes, len, &h, &fr) != 0)
        return -1;
    port_set_player_count((int)h.nseats);
    port_rng_begin_match(h.rng_seed);
    port_player_spawn();
    rc = 0;
    for (i = 0; i < h.nframes; i++) {
        for (s = 0; s < h.nseats; s++)
            port_set_local_pad((int)s, fr[i].pads[s].stick_x, fr[i].pads[s].stick_y,
                fr[i].pads[s].buttons);
        if (port_sim_tick(fr[i].tick) != 0) {
            rc = 2;
            if (mismatch_tick)
                *mismatch_tick = fr[i].tick;
            break;
        }
        port_checksum(fr[i].tick, &got);
        if (got.rng_lo != fr[i].cs.rng_lo || got.chr_rng_lo != fr[i].cs.chr_rng_lo
            || got.crc_players != fr[i].cs.crc_players || got.crc_chrs != fr[i].cs.crc_chrs
            || got.crc_objectives != fr[i].cs.crc_objectives) {
            rc = 1;
            if (mismatch_tick)
                *mismatch_tick = fr[i].tick;
            break;
        }
    }
    free(fr);
    return rc;
}
