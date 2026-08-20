#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/port/rng/random.h"

static int load_vec(const char *path, uint32_t out[256])
{
    FILE *f = fopen(path, "r");
    char line[16];
    int i;
    if (!f) {
        perror(path);
        return -1;
    }
    for (i = 0; i < 256; i++) {
        if (!fgets(line, sizeof line, f)) {
            fclose(f);
            return -1;
        }
        out[i] = (uint32_t)strtoul(line, NULL, 16);
    }
    fclose(f);
    return 0;
}

static int check_stream(const char *label, void (*setseed)(uint32_t), uint32_t (*getnext)(void),
                        uint32_t seed, const uint32_t *want)
{
    int i;
    setseed(seed);
    for (i = 0; i < 256; i++) {
        uint32_t got = getnext();
        if (got != want[i]) {
            fprintf(stderr, "%s seed %u [%d]: got %08x want %08x\n", label, seed, i, got, want[i]);
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "testdata/rng";
    char path[512];
    uint32_t v0[256], v1[256];
    uint64_t local;
    int i;

    snprintf(path, sizeof path, "%s/seed0.vec", dir);
    if (load_vec(path, v0) != 0) return 1;
    snprintf(path, sizeof path, "%s/seed1.vec", dir);
    if (load_vec(path, v1) != 0) return 1;

    if (check_stream("random", randomSetSeed, randomGetNext, 0, v0) != 0) return 1;
    if (check_stream("random", randomSetSeed, randomGetNext, 1, v1) != 0) return 1;
    if (check_stream("chrObj", chrObjRandomSetSeed, chrObjRandomGetNext, 0, v0) != 0) return 1;
    if (check_stream("chrObj", chrObjRandomSetSeed, chrObjRandomGetNext, 1, v1) != 0) return 1;

    randomSetSeed(0);
    local = g_randomSeed;
    for (i = 0; i < 256; i++) {
        if (randomGetNextFrom(&local) != v0[i]) {
            fprintf(stderr, "randomGetNextFrom mismatch at %d\n", i);
            return 1;
        }
    }

    g_randomSeed = 0xAB8D9F7781280783ull;
    if (randomGetNext() != 0x40ec37cfu) {
        fprintf(stderr, "default seed first output mismatch\n");
        return 1;
    }

    port_rng_begin_match(0);
    if (g_randomSeed != 1ull) {
        fprintf(stderr, "begin_match seed 0 should store 1\n");
        return 1;
    }

    puts("rng vectors ok");
    return 0;
}
