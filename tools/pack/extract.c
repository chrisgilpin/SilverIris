/*
 * Developer tool: ROM + filelist → .c0pack.
 * Not linked into product silveriris (K18: product must not fopen a .z64).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/port/fs/c0pack.h"
#include "../../src/port/fs/filelist.h"
#include "../../src/port/fs/sha256.h"

#include "puff.h"

#define GE_1172_HEADER_LENGTH 2
#define MAX_INFLATE (2u * 1024u * 1024u)

static int detect_z64(uint8_t *rom, size_t n)
{
    size_t i;
    if (n < 4)
        return -1;
    if (rom[0] == 0x80 && rom[1] == 0x37)
        return 0;
    if (rom[0] == 0x37 && rom[1] == 0x80) {
        if (n % 2)
            return -1;
        for (i = 0; i + 1 < n; i += 2) {
            uint8_t t = rom[i];
            rom[i] = rom[i + 1];
            rom[i + 1] = t;
        }
        return 0;
    }
    if (rom[0] == 0x40 && rom[1] == 0x12) {
        if (n % 4)
            return -1;
        for (i = 0; i + 3 < n; i += 4) {
            uint8_t a = rom[i], b = rom[i + 1], c = rom[i + 2], d = rom[i + 3];
            rom[i] = d;
            rom[i + 1] = c;
            rom[i + 2] = b;
            rom[i + 3] = a;
        }
        return 0;
    }
    return -1;
}

static uint8_t *read_all(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    long sz;
    uint8_t *buf;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *n = (size_t)sz;
    return buf;
}

static int extract_row(const uint8_t *rom, size_t rom_len, const PortFilelistEntry *e, uint8_t **out,
                       size_t *out_len)
{
    const uint8_t *src;
    if ((size_t)e->offset + (size_t)e->size > rom_len)
        return -1;
    src = rom + e->offset;
    if (!e->compressed) {
        uint8_t *copy = (uint8_t *)malloc(e->size ? e->size : 1);
        if (!copy)
            return -1;
        memcpy(copy, src, e->size);
        *out = copy;
        *out_len = e->size;
        return 0;
    }
    {
        unsigned long destlen = MAX_INFLATE;
        unsigned long outlen = 0;
        uint8_t *dst;
        int prc;
        if (e->size <= GE_1172_HEADER_LENGTH)
            return -1;
        dst = (uint8_t *)malloc(MAX_INFLATE);
        if (!dst)
            return -1;
        prc = puff(dst, destlen, src + GE_1172_HEADER_LENGTH, (unsigned long)e->size - GE_1172_HEADER_LENGTH,
                   &outlen);
        if (prc != 0) {
            free(dst);
            return -1;
        }
        *out = dst;
        *out_len = (size_t)outlen;
        return 0;
    }
}

static void usage(void)
{
    fprintf(stderr, "extract --rom ge007.u.z64 --filelist filelist.u.csv -o ge.u.c0pack\n");
    fprintf(stderr, "Developer only. Product silveriris takes --pack, never --rom.\n");
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *csv_path = NULL, *out_path = NULL;
    uint8_t *rom = NULL;
    size_t rom_len = 0;
    C0File *files = NULL;
    size_t nfiles = 0, cap = 0, i;
    uint8_t *pack = NULL;
    size_t pack_len = 0;
    uint8_t pack_hash[32];
    char hex[65];
    int rc, a;

    for (a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--rom") == 0 && a + 1 < argc)
            rom_path = argv[++a];
        else if (strncmp(argv[a], "--rom=", 6) == 0)
            rom_path = argv[a] + 6;
        else if (strcmp(argv[a], "--filelist") == 0 && a + 1 < argc)
            csv_path = argv[++a];
        else if (strcmp(argv[a], "-o") == 0 && a + 1 < argc)
            out_path = argv[++a];
        else if (strncmp(argv[a], "-o=", 3) == 0)
            out_path = argv[a] + 3;
        else {
            usage();
            return 2;
        }
    }
    if (!rom_path || !csv_path || !out_path) {
        usage();
        return 2;
    }

    rom = read_all(rom_path, &rom_len);
    if (!rom) {
        fprintf(stderr, "read ROM failed\n");
        return 1;
    }
    if (detect_z64(rom, rom_len) != 0) {
        fprintf(stderr, "unrecognised N64 header\n");
        free(rom);
        return 1;
    }
    if (port_filelist_load(csv_path) != 0) {
        fprintf(stderr, "filelist parse failed\n");
        free(rom);
        return 1;
    }

    for (i = 0; i < port_filelist_count(); i++) {
        const PortFilelistEntry *e = port_filelist_at(i);
        uint8_t *payload = NULL;
        size_t plen = 0;
        if (!e || e->size == 0)
            continue;
        if (extract_row(rom, rom_len, e, &payload, &plen) != 0) {
            fprintf(stderr, "extract failed %s\n", e->name);
            free(rom);
            return 1;
        }
        if (nfiles == cap) {
            size_t ncap = cap ? cap * 2 : 16;
            C0File *nf = (C0File *)realloc(files, ncap * sizeof *nf);
            if (!nf) {
                free(payload);
                free(rom);
                return 1;
            }
            files = nf;
            cap = ncap;
        }
        files[nfiles].path = e->name;
        files[nfiles].bytes = payload;
        files[nfiles].size = plen;
        nfiles++;
    }

    rc = c0pack_build(files, nfiles, 0, 0, &pack, &pack_len, pack_hash);
    for (i = 0; i < nfiles; i++)
        free((void *)files[i].bytes);
    free(files);
    free(rom);
    port_filelist_clear();
    if (rc != 0) {
        fprintf(stderr, "c0pack_build %d\n", rc);
        return 1;
    }
    {
        FILE *out = fopen(out_path, "wb");
        if (!out || fwrite(pack, 1, pack_len, out) != pack_len) {
            fprintf(stderr, "write pack failed\n");
            free(pack);
            return 1;
        }
        fclose(out);
    }
    silveriris_sha256_hex(pack_hash, hex);
    printf("extract ok files=%zu packHash=%s bytes=%zu\n", nfiles, hex, pack_len);
    free(pack);
    return 0;
}
