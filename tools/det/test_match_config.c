#include <stdio.h>
#include <string.h>

#include "net/match_config.h"

/* Fixture hex is the TS/C lock. Offset 11 must be 00. */
static const char k_hex[] =
    "010000020203000100000100"
    "d4c3b2a1"
    "22000000"
    "00000000"
    "02000000"
    "01000000020000000300000004000000"
    "00000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000"
    "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
    "73696c766572697269732d6275696c6469642121";

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

static void hex_of(const uint8_t *b, char *out)
{
    static const char *d = "0123456789abcdef";
    int i;
    for (i = 0; i < MATCH_CONFIG_BYTES; i++) {
        out[i * 2] = d[b[i] >> 4];
        out[i * 2 + 1] = d[b[i] & 0xf];
    }
    out[320] = 0;
}

int main(void)
{
    MatchConfig c, d;
    uint8_t packed[MATCH_CONFIG_BYTES];
    char hex[321];
    int i;

    memset(&c, 0, sizeof c);
    c.protocol = 1;
    c.region = 0;
    c.nseats = 2;
    c.delayTicks = 2;
    c.speedgraphframes = 3;
    c.aimSight = 0;
    c.autoAim = 1;
    c.lookAhead = 0;
    c.aimControl = 0;
    c.radar = 1;
    c.pad0 = 0;
    c.rngSeed = 0xA1B2C3D4u;
    c.stage = 34;
    c.scenario = 0;
    c.gameLength = 2;
    c.chars[0] = 1;
    c.chars[1] = 2;
    c.chars[2] = 3;
    c.chars[3] = 4;
    for (i = 0; i < 32; i++)
        c.packHash[i] = (uint8_t)i;
    memcpy(c.buildId, "silveriris-buildid!!", 20);

    if (encodeMatchConfig(&c, packed) != MATCH_CONFIG_BYTES)
        return fail("encode size");
    if (packed[11] != 0)
        return fail("pad0");
    hex_of(packed, hex);
    if (strlen(k_hex) != 320)
        return fail("fixture length");
    if (strcmp(hex, k_hex) != 0) {
        fprintf(stderr, "got  %s\nwant %s\n", hex, k_hex);
        return fail("hex mismatch");
    }
    if (decodeMatchConfig(packed, &d) != 0)
        return fail("decode");
    if (d.rngSeed != c.rngSeed || d.stage != 34 || d.nseats != 2 || d.pad0 != 0)
        return fail("roundtrip");
    c.pad0 = 1;
    if (encodeMatchConfig(&c, packed) == MATCH_CONFIG_BYTES)
        return fail("pad0 must reject");
    printf("match_config ok bytes=%d hex=%s\n", MATCH_CONFIG_BYTES, hex);
    return 0;
}
