#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "glue/port_api.h"
#include "fs/c0pack.h"
#include "fs/sha256.h"
#include "gfx/gbi_interp.h"

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main(int argc, char **argv)
{
    const uint8_t hello[] = "hello";
    const uint8_t abc[] = "abc";
    C0File files[2];
    uint8_t *pack = NULL;
    size_t pack_len = 0;
    uint8_t pack_hash[32], bad[32];
    uint8_t grey[32];
    char hex[65];
    PortErr rc;

    files[0].path = "assets/hello.bin";
    files[0].bytes = hello;
    files[0].size = 5;
    files[1].path = "assets/test.bin";
    files[1].bytes = abc;
    files[1].size = 3;
    if (c0pack_build(files, 2, 0, 0, &pack, &pack_len, pack_hash) != 0)
        return fail("build pack");

    memset(bad, 0, 32);
    rc = port_api_init(pack, (uint32_t)pack_len, bad);
    if (rc != PORT_E_HASH)
        return fail("expected HASH");
    if (port_api_ready())
        return fail("ready after bad hash");

    rc = port_api_init((const uint8_t *)"NOPE", 4, pack_hash);
    if (rc != PORT_E_ASSETS)
        return fail("expected ASSETS");

    rc = port_api_init(pack, (uint32_t)pack_len, pack_hash);
    if (rc != PORT_OK)
        return fail(port_api_last_error());
    if (!port_api_ready())
        return fail("not ready");
    if (port_api_fb_width() != 320 || port_api_fb_height() != 240)
        return fail("fb size");
    if (!port_api_fb())
        return fail("fb ptr");
    port_api_draw();
    if (port_api_last_draw() != PORT_DRAW_FALLBACK)
        return fail("init-only draw is fallback synthetic");
    g1_fb_grey_sha256(grey);
    silveriris_sha256_hex(grey, hex);
    if (argc >= 2 && strcmp(hex, argv[1]) != 0) {
        fprintf(stderr, "grey hash got %s want %s\n", hex, argv[1]);
        return 1;
    }
    {
        int16_t pcm[64];
        int i;
        memset(pcm, 0x7f, sizeof pcm);
        if (port_api_audio_rate() != 22050)
            return fail("audio rate");
        port_api_audio_cb(pcm, 32);
        for (i = 0; i < 64; i++) {
            if (pcm[i] != 0)
                return fail("audio default is silence");
        }
    }
    if (port_api_sim_tick(0) != 0)
        return fail("sim tick");
    if (port_api_clock_timer() != 3)
        return fail("g_ClockTimer pin");
    printf("port_api ok grey_sha256=%s clock=%d\n", hex, port_api_clock_timer());
    port_api_shutdown();
    free(pack);
    return 0;
}
