#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/port/fs/c0pack.h"
#include "../../src/port/fs/sha256.h"

int main(int argc, char **argv) {
    const uint8_t hello[] = "hello";
    const uint8_t abc[] = "abc";
    uint8_t digest[32];
    char hex[65];
    C0File files[2];
    uint8_t *pack = NULL;
    size_t pack_len = 0;
    uint8_t pack_hash[32];
    int rc;

    silveriris_sha256(abc, 3, digest);
    silveriris_sha256_hex(digest, hex);
    printf("SHA256_ABC=%s\n", hex);

    files[0].path = "assets/hello.bin";
    files[0].bytes = hello;
    files[0].size = 5;
    files[1].path = "assets/test.bin";
    files[1].bytes = abc;
    files[1].size = 3;

    rc = c0pack_build(files, 2, 0 /* U */, 0, &pack, &pack_len, pack_hash);
    if (rc != 0) {
        fprintf(stderr, "build failed %d\n", rc);
        return 1;
    }
    silveriris_sha256_hex(pack_hash, hex);
    printf("PACKHASH=%s\n", hex);
    printf("PACKLEN=%zu\n", pack_len);

    rc = c0pack_validate(pack, pack_len, digest);
    if (rc != 0) {
        fprintf(stderr, "validate failed %d\n", rc);
        return 1;
    }
    if (memcmp(digest, pack_hash, 32) != 0) {
        fprintf(stderr, "hash mismatch after validate\n");
        return 1;
    }

    if (argc >= 2) {
        FILE *f = fopen(argv[1], "wb");
        if (!f) return 1;
        if (fwrite(pack, 1, pack_len, f) != pack_len) return 1;
        fclose(f);
    }
    free(pack);
    return 0;
}
