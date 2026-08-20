#include <stdio.h>
#include <string.h>

#include "gfx/gbi_interp.h"
#include "gfx/sw_raster.h"
#include "fs/sha256.h"

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main(int argc, char **argv)
{
    uint8_t digest[32];
    char hex[65];
    const uint8_t *fb;
    unsigned i, n = (unsigned)G1_FB_W * (unsigned)G1_FB_H;
    unsigned painted = 0;
    const GirList *ir;

    if (g1_run_synthetic() != 0)
        return fail("synthetic");
    ir = g1_last_ir();
    if (!ir || ir->ncmds < 3)
        return fail("ir cmds");

    fb = g1_fb_rgba();
    if (!fb)
        return fail("fb");
    for (i = 0; i < n; i++) {
        if (fb[i * 4] != fb[0] || fb[i * 4 + 1] != fb[1] || fb[i * 4 + 2] != fb[2])
            painted++;
    }
    if (painted < 1000)
        return fail("triangle too small");

    g1_fb_grey_sha256(digest);
    silveriris_sha256_hex(digest, hex);
    if (argc >= 2 && strcmp(hex, argv[1]) != 0) {
        fprintf(stderr, "grey hash got %s want %s\n", hex, argv[1]);
        return 1;
    }
    printf("g1 ok painted=%u grey_sha256=%s\n", painted, hex);
    return 0;
}
