#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "det/checksum.h"
#include "det/tape.h"
#include "player/gun.h"
#include "player/move.h"
#include "rng/random.h"
#include "vi/sim_tick.h"

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

static void fill_script(TapeHeader *h, TapeFrame *fr, uint32_t n)
{
    uint32_t t;

    memset(h, 0, sizeof *h);
    h->magic = PORT_TAPE_MAGIC;
    h->version = PORT_TAPE_VERSION;
    h->region = 0;
    h->rng_seed = 1;
    h->nseats = 1;
    h->nframes = n;
    port_set_player_count(1);
    port_rng_begin_match(1);
    port_player_spawn();
    for (t = 0; t < n; t++) {
        PortPad pad;
        memset(&pad, 0, sizeof pad);
        if (t < 20)
            pad.stick_y = (int8_t)-70;
        if (t == 25)
            pad.buttons = (uint16_t)PORT_Z_TRIG;
        port_set_local_pad(0, pad.stick_x, pad.stick_y, pad.buttons);
        port_sim_tick(t);
        fr[t].tick = t;
        fr[t].pads[0] = pad;
        port_checksum(t, &fr[t].cs);
    }
}

int main(int argc, char **argv)
{
    TapeHeader h;
    TapeFrame fr[40];
    uint8_t *bytes = NULL;
    size_t len = 0;
    uint32_t miss = 0;
    int rc;
    const char *out_path;

    fill_script(&h, fr, 40);
    if (fr[0].cs.rng_lo == 0 && fr[0].cs.chr_rng_lo == 0)
        return fail("both RNG seeds should be in SimChecksum");
    if (fr[19].cs.crc_players == fr[0].cs.crc_players)
        return fail("walk should change crc_players");
    if (port_tape_build(&h, fr, &bytes, &len) != 0)
        return fail("build tape");
    rc = port_tape_replay(bytes, len, &miss);
    if (rc != 0) {
        fprintf(stderr, "replay rc=%d tick=%u\n", rc, miss);
        return fail("replay");
    }
    rc = port_tape_replay(bytes, len, &miss);
    if (rc != 0)
        return fail("second replay");

    bytes[61] ^= 1; /* corrupt first-frame stick_y */
    rc = port_tape_replay(bytes, len, &miss);
    if (rc == 0)
        return fail("corrupt tape should mismatch");
    bytes[61] ^= 1;

    out_path = argc >= 2 ? argv[1] : NULL;
    if (out_path) {
        FILE *f = fopen(out_path, "wb");
        if (!f)
            return fail("write tape");
        if (fwrite(bytes, 1, len, f) != len) {
            fclose(f);
            return fail("fwrite");
        }
        fclose(f);
    }
    printf("tape ok frames=%u bytes=%u rng=%08x chr_rng=%08x crc=%08x\n", h.nframes,
        (unsigned)len, fr[39].cs.rng_lo, fr[39].cs.chr_rng_lo, fr[39].cs.crc_players);
    free(bytes);
    return 0;
}
