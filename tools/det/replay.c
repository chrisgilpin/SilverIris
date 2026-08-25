#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "det/tape.h"

#ifdef PORT_REPLAY_PACK
#include "fs/pack_dma.h"
#include "fs/stage.h"
#endif

/*
 * Replay a TAPE1. Public CI uses the synth tape with no pack.
 * Private Facility minutes: replay_pack --pack <local.c0pack> --stage facility.
 * Never commit a retail pack or ROM.
 *
 * usage: replay [--pack file.c0pack] [--stage facility|complex|none] file.tape
 * SILVERIRIS_PACK is an alias for --pack.
 */

static void usage(void)
{
    fprintf(stderr,
        "usage: replay [--pack file.c0pack] [--stage facility|complex|none] file.tape\n");
}

int main(int argc, char **argv)
{
    FILE *f;
    uint8_t *bytes;
    long sz;
    uint32_t miss = 0;
    int rc;
    const char *tape_path = NULL;
    const char *pack_path = NULL;
    const char *stage_name = NULL;
    int i;

    pack_path = getenv("SILVERIRIS_PACK");
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pack") == 0 && i + 1 < argc)
            pack_path = argv[++i];
        else if (strncmp(argv[i], "--pack=", 7) == 0)
            pack_path = argv[i] + 7;
        else if (strcmp(argv[i], "--stage") == 0 && i + 1 < argc)
            stage_name = argv[++i];
        else if (strncmp(argv[i], "--stage=", 8) == 0)
            stage_name = argv[i] + 8;
        else if (argv[i][0] == '-') {
            usage();
            return 2;
        } else if (!tape_path)
            tape_path = argv[i];
        else {
            usage();
            return 2;
        }
    }
    if (!tape_path) {
        usage();
        return 2;
    }

    if (pack_path) {
#ifdef PORT_REPLAY_PACK
        rc = port_init_file(pack_path);
        if (rc != 0) {
            fprintf(stderr, "replay: pack %s rc=%d\n", pack_path, rc);
            return 1;
        }
        if (!stage_name)
            stage_name = "facility";
#else
        fprintf(stderr, "replay: --pack needs replay_pack (make -C native replay-pack)\n");
        return 2;
#endif
    }
#ifdef PORT_REPLAY_PACK
    if (stage_name && strcmp(stage_name, "none") != 0) {
        int id = PORT_LEVEL_FACILITY;
        if (strcmp(stage_name, "complex") == 0)
            id = PORT_LEVEL_COMPLEX;
        else if (strcmp(stage_name, "facility") != 0) {
            fprintf(stderr, "replay: unknown stage %s\n", stage_name);
            return 2;
        }
        rc = port_stage_load(id);
        if (rc != 0) {
            fprintf(stderr, "replay: stage %s load rc=%d\n", stage_name, rc);
            return 1;
        }
    }
#else
    if (stage_name && strcmp(stage_name, "none") != 0) {
        fprintf(stderr, "replay: --stage needs replay_pack (make -C native replay-pack)\n");
        return 2;
    }
    (void)stage_name;
#endif

    f = fopen(tape_path, "rb");
    if (!f) {
        perror(tape_path);
        return 1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 1;
    }
    sz = ftell(f);
    if (sz <= 0 || sz > 8 * 1024 * 1024) {
        fclose(f);
        return 1;
    }
    rewind(f);
    bytes = (uint8_t *)malloc((size_t)sz);
    if (!bytes || fread(bytes, 1, (size_t)sz, f) != (size_t)sz) {
        free(bytes);
        fclose(f);
        return 1;
    }
    fclose(f);
    rc = port_tape_replay(bytes, (size_t)sz, &miss);
    free(bytes);
    if (rc != 0) {
        fprintf(stderr, "mismatch rc=%d tick=%u\n", rc, miss);
        return 1;
    }
    printf("replay ok %s\n", tape_path);
    return 0;
}
