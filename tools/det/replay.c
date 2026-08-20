#include <stdio.h>
#include <stdlib.h>

#include "det/tape.h"

int main(int argc, char **argv)
{
    FILE *f;
    uint8_t *bytes;
    long sz;
    uint32_t miss = 0;
    int rc;

    if (argc < 2) {
        fprintf(stderr, "usage: replay <file.tape>\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
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
    printf("replay ok %s\n", argv[1]);
    return 0;
}
