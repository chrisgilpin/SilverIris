/*
 * Developer tool: ROM + filelist + imagelist → .c0pack.
 * Not linked into product silveriris (K18: product must not fopen a .z64).
 *
 * Image banks match the in-tab extractor (images.def names × imagelist.u.csv).
 * Without them G1 SETTEX misses and Facility paints vertex grey.
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

/* NTSC-U animation table DMA. Same numbers as web/extractor ANIM_TABLE_U.
 * filelist.u.csv: 1198784,1482432 entries; 2681216,59360 data.
 * JP is 1202144 / 2684576 — do not use. Skip if the ROM range is missing. */
#define ANIM_ENTRIES_OFF_U 1198784u
#define ANIM_ENTRIES_SIZE_U 1482432u
#define ANIM_DATA_OFF_U 2681216u
#define ANIM_DATA_SIZE_U 59360u
#define ANIM_ENTRIES_NAME "assets/animationtable_entries.bin"
#define ANIM_DATA_NAME "assets/animationtable_data.bin"

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

static int file_exists(const char *path)
{
    FILE *f;
    if (!path || !path[0])
        return 0;
    f = fopen(path, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

static int join_dir(const char *base_file, const char *rel, char *out, size_t n)
{
    const char *slash;
    int wr;
    if (!base_file || !rel || !out || n < 2)
        return -1;
    slash = strrchr(base_file, '/');
    if (!slash)
        wr = snprintf(out, n, "%s", rel);
    else
        wr = snprintf(out, n, "%.*s/%s", (int)(slash - base_file), base_file, rel);
    return (wr < 0 || (size_t)wr >= n) ? -1 : 0;
}

static char *dup_str(const char *s)
{
    size_t n;
    char *p;
    if (!s)
        return NULL;
    n = strlen(s);
    p = (char *)malloc(n + 1);
    if (!p)
        return NULL;
    memcpy(p, s, n + 1);
    return p;
}

static int is_anim_table(const char *name)
{
    return name && (strcmp(name, ANIM_ENTRIES_NAME) == 0 || strcmp(name, ANIM_DATA_NAME) == 0);
}

static int already_have(const C0File *files, size_t nfiles, const char *name)
{
    size_t i;
    for (i = 0; i < nfiles; i++) {
        if (files[i].path && strcmp(files[i].path, name) == 0)
            return 1;
    }
    return 0;
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

static int append_file(C0File **files, size_t *nfiles, size_t *cap, const char *path, uint8_t *payload,
                       size_t plen)
{
    if (*nfiles == *cap) {
        size_t ncap = *cap ? *cap * 2 : 16;
        C0File *nf = (C0File *)realloc(*files, ncap * sizeof *nf);
        if (!nf)
            return -1;
        *files = nf;
        *cap = ncap;
    }
    (*files)[*nfiles].path = path;
    (*files)[*nfiles].bytes = payload;
    (*files)[*nfiles].size = plen;
    (*nfiles)++;
    return 0;
}

static int parse_def_names(const char *text, size_t n, char ***out, size_t *nout)
{
    char **names = NULL;
    size_t cap = 0, nn = 0;
    const char *p = text, *end = text + n;

    *out = NULL;
    *nout = 0;
    if (!text)
        return 0;
    while (p < end) {
        const char *line = p, *nl;
        size_t linelen;
        const char *s, *e, *comma;
        size_t nlen;
        char *name;

        nl = memchr(p, '\n', (size_t)(end - p));
        linelen = nl ? (size_t)(nl - p) : (size_t)(end - p);
        p = nl ? nl + 1 : end;
        if (linelen && line[linelen - 1] == '\r')
            linelen--;
        s = line;
        while (linelen && (*s == ' ' || *s == '\t')) {
            s++;
            linelen--;
        }
        if (linelen < 8 || strncmp(s, "IMAGE(", 6) != 0 || s[linelen - 1] != ')')
            continue;
        s += 6;
        linelen -= 7; /* drop IMAGE( and trailing ) */
        comma = memchr(s, ',', linelen);
        nlen = comma ? (size_t)(comma - s) : linelen;
        e = s + nlen;
        while (nlen && (s[0] == ' ' || s[0] == '\t')) {
            s++;
            nlen--;
        }
        while (nlen && (e[-1] == ' ' || e[-1] == '\t')) {
            e--;
            nlen--;
        }
        if (!nlen)
            continue;
        name = (char *)malloc(nlen + 1);
        if (!name) {
            size_t i;
            for (i = 0; i < nn; i++)
                free(names[i]);
            free(names);
            return -1;
        }
        memcpy(name, s, nlen);
        name[nlen] = 0;
        if (nn == cap) {
            size_t ncap = cap ? cap * 2 : 64;
            char **tmp = (char **)realloc(names, ncap * sizeof *tmp);
            if (!tmp) {
                free(name);
                {
                    size_t i;
                    for (i = 0; i < nn; i++)
                        free(names[i]);
                }
                free(names);
                return -1;
            }
            names = tmp;
            cap = ncap;
        }
        names[nn++] = name;
    }
    *out = names;
    *nout = nn;
    return 0;
}

static int parse_u32(const char *s, const char *e, uint32_t *out)
{
    unsigned long v = 0;
    if (!s || s >= e)
        return -1;
    while (s < e && *s >= '0' && *s <= '9') {
        v = v * 10ul + (unsigned long)(*s - '0');
        s++;
    }
    if (s != e)
        return -1;
    *out = (uint32_t)v;
    return 0;
}

/* Pair images.def names with imagelist.u.csv (same as web/extractor syncImagelist). */
static int add_images(const uint8_t *rom, size_t rom_len, const char *csv_path, const char *ilist_arg,
                      const char *def_arg, C0File **files, size_t *nfiles, size_t *cap, size_t *nimg)
{
    char ilist[512], defp[512];
    uint8_t *csv = NULL, *defb = NULL;
    size_t csv_n = 0, def_n = 0;
    char **names = NULL;
    size_t nnames = 0, row = 0, added = 0;
    const char *p, *end;

    *nimg = 0;
    ilist[0] = 0;
    defp[0] = 0;
    if (ilist_arg && file_exists(ilist_arg))
        snprintf(ilist, sizeof ilist, "%s", ilist_arg);
    else if (join_dir(csv_path, "imagelist.u.csv", ilist, sizeof ilist) == 0 && file_exists(ilist))
        ;
    else if (join_dir(csv_path, "../imagelist.u.csv", ilist, sizeof ilist) == 0 && file_exists(ilist))
        ;
    else {
        fprintf(stderr, "extract: no imagelist.u.csv (SETTEX tiles will miss)\n");
        return 0;
    }
    if (def_arg && file_exists(def_arg))
        snprintf(defp, sizeof defp, "%s", def_arg);
    else if (join_dir(csv_path, "../assets/images.def", defp, sizeof defp) == 0 && file_exists(defp))
        ;
    else if (join_dir(csv_path, "images.def", defp, sizeof defp) == 0 && file_exists(defp))
        ;
    else
        defp[0] = 0;

    csv = read_all(ilist, &csv_n);
    if (!csv) {
        fprintf(stderr, "extract: read %s failed\n", ilist);
        return -1;
    }
    if (defp[0]) {
        defb = read_all(defp, &def_n);
        if (defb && parse_def_names((const char *)defb, def_n, &names, &nnames) != 0) {
            free(defb);
            free(csv);
            return -1;
        }
        free(defb);
    }

    p = (const char *)csv;
    end = p + csv_n;
    while (p < end) {
        const char *line = p, *nl, *c1, *c2, *c3, *c4;
        size_t linelen;
        uint32_t off = 0, sz = 0, comp = 0;
        char pathbuf[256];
        char *path;
        uint8_t *payload = NULL;
        size_t plen = 0;
        PortFilelistEntry e;

        nl = memchr(p, '\n', (size_t)(end - p));
        linelen = nl ? (size_t)(nl - p) : (size_t)(end - p);
        p = nl ? nl + 1 : end;
        if (linelen && line[linelen - 1] == '\r')
            linelen--;
        if (!linelen)
            continue;
        c1 = memchr(line, ',', linelen);
        if (!c1)
            continue;
        c2 = memchr(c1 + 1, ',', (size_t)((line + linelen) - (c1 + 1)));
        if (!c2)
            continue;
        c3 = memchr(c2 + 1, ',', (size_t)((line + linelen) - (c2 + 1)));
        if (!c3)
            continue;
        c4 = memchr(c3 + 1, ',', (size_t)((line + linelen) - (c3 + 1)));
        if (parse_u32(line, c1, &off) != 0 || parse_u32(c1 + 1, c2, &sz) != 0)
            continue;
        if (c4)
            (void)parse_u32(c3 + 1, c4, &comp);
        else
            (void)parse_u32(c3 + 1, line + linelen, &comp);
        if (sz == 0) {
            row++;
            continue;
        }
        if (nnames) {
            if (row >= nnames)
                break;
            snprintf(pathbuf, sizeof pathbuf, "assets/images/split/%s.bin", names[row]);
        } else {
            size_t nlen = (size_t)(c3 - (c2 + 1));
            if (nlen >= sizeof pathbuf)
                nlen = sizeof pathbuf - 1;
            memcpy(pathbuf, c2 + 1, nlen);
            pathbuf[nlen] = 0;
        }
        row++;
        if (already_have(*files, *nfiles, pathbuf))
            continue;
        memset(&e, 0, sizeof e);
        e.offset = off;
        e.size = sz;
        e.compressed = comp ? 1 : 0;
        if ((size_t)off + (size_t)sz > rom_len)
            continue;
        if (extract_row(rom, rom_len, &e, &payload, &plen) != 0) {
            fprintf(stderr, "extract failed %s\n", pathbuf);
            free(csv);
            {
                size_t i;
                for (i = 0; i < nnames; i++)
                    free(names[i]);
            }
            free(names);
            return -1;
        }
        path = dup_str(pathbuf);
        if (!path || append_file(files, nfiles, cap, path, payload, plen) != 0) {
            free(path);
            free(payload);
            free(csv);
            {
                size_t i;
                for (i = 0; i < nnames; i++)
                    free(names[i]);
            }
            free(names);
            return -1;
        }
        added++;
    }
    free(csv);
    {
        size_t i;
        for (i = 0; i < nnames; i++)
            free(names[i]);
    }
    free(names);
    *nimg = added;
    printf("extract images=%zu from %s\n", added, ilist);
    return 0;
}

static void usage(void)
{
    fprintf(stderr, "extract --rom ge007.u.z64 --filelist filelist.u.csv -o ge.u.c0pack\n");
    fprintf(stderr, "  optional --imagelist imagelist.u.csv --images-def images.def\n");
    fprintf(stderr, "Developer only. Product silveriris takes --pack, never --rom.\n");
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *csv_path = NULL, *out_path = NULL;
    const char *ilist_path = NULL, *def_path = NULL;
    uint8_t *rom = NULL;
    size_t rom_len = 0;
    C0File *files = NULL;
    size_t nfiles = 0, cap = 0, i, nimg = 0;
    uint8_t *pack = NULL;
    size_t pack_len = 0;
    uint8_t pack_hash[32];
    char hex[65];
    int rc, a;
    static const struct {
        uint32_t off;
        uint32_t sz;
        const char *name;
    } extra_u[2] = {
        {ANIM_ENTRIES_OFF_U, ANIM_ENTRIES_SIZE_U, ANIM_ENTRIES_NAME},
        {ANIM_DATA_OFF_U, ANIM_DATA_SIZE_U, ANIM_DATA_NAME},
    };

    for (a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--rom") == 0 && a + 1 < argc)
            rom_path = argv[++a];
        else if (strncmp(argv[a], "--rom=", 6) == 0)
            rom_path = argv[a] + 6;
        else if (strcmp(argv[a], "--filelist") == 0 && a + 1 < argc)
            csv_path = argv[++a];
        else if (strcmp(argv[a], "--imagelist") == 0 && a + 1 < argc)
            ilist_path = argv[++a];
        else if (strcmp(argv[a], "--images-def") == 0 && a + 1 < argc)
            def_path = argv[++a];
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
        char *path;
        if (!e || e->size == 0)
            continue;
        if ((size_t)e->offset + (size_t)e->size > rom_len) {
            if (is_anim_table(e->name))
                continue;
            fprintf(stderr, "extract failed %s\n", e->name);
            free(rom);
            return 1;
        }
        if (extract_row(rom, rom_len, e, &payload, &plen) != 0) {
            fprintf(stderr, "extract failed %s\n", e->name);
            free(rom);
            return 1;
        }
        /* Own the path so imagelist parsing cannot free filelist storage under us. */
        path = dup_str(e->name);
        if (!path || append_file(&files, &nfiles, &cap, path, payload, plen) != 0) {
            free(path);
            free(payload);
            free(rom);
            return 1;
        }
    }

    for (i = 0; i < 2; i++) {
        uint8_t *payload;
        char *path;
        if (already_have(files, nfiles, extra_u[i].name))
            continue;
        if ((size_t)extra_u[i].off + extra_u[i].sz > rom_len)
            continue;
        payload = (uint8_t *)malloc(extra_u[i].sz ? extra_u[i].sz : 1);
        if (!payload) {
            free(rom);
            return 1;
        }
        memcpy(payload, rom + extra_u[i].off, extra_u[i].sz);
        path = dup_str(extra_u[i].name);
        if (!path || append_file(&files, &nfiles, &cap, path, payload, extra_u[i].sz) != 0) {
            free(path);
            free(payload);
            free(rom);
            return 1;
        }
    }

    if (add_images(rom, rom_len, csv_path, ilist_path, def_path, &files, &nfiles, &cap, &nimg) != 0) {
        free(rom);
        return 1;
    }

    rc = c0pack_build(files, nfiles, 0, 0, &pack, &pack_len, pack_hash);
    for (i = 0; i < nfiles; i++) {
        free((void *)files[i].bytes);
        free((void *)files[i].path);
    }
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
    printf("extract ok files=%zu images=%zu packHash=%s bytes=%zu\n", nfiles, nimg, hex, pack_len);
    free(pack);
    return 0;
}
