#include "filelist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PortFilelistEntry *g_files;
static size_t g_nfiles;

void port_filelist_clear(void)
{
    free(g_files);
    g_files = NULL;
    g_nfiles = 0;
}

static int parse_u32(const char *s, uint32_t *out)
{
    char *end = NULL;
    unsigned long v;
    if (!s || !*s)
        return -1;
    v = strtoul(s, &end, 10);
    if (!end || end == s || *end != 0)
        return -1;
    *out = (uint32_t)v;
    return 0;
}

int port_filelist_parse(const char *csv, size_t len)
{
    size_t cap = 0;
    size_t n = 0;
    PortFilelistEntry *rows = NULL;
    const char *p = csv;
    const char *end = csv + len;

    port_filelist_clear();
    if (!csv)
        return PORT_FILELIST_ERR_PARSE;

    while (p < end) {
        const char *line = p;
        const char *nl;
        char buf[512];
        size_t linelen;
        char *f0, *f1, *f2, *f3, *f4;
        PortFilelistEntry e;
        uint32_t compressed, extract;

        nl = memchr(p, '\n', (size_t)(end - p));
        if (nl) {
            linelen = (size_t)(nl - p);
            p = nl + 1;
        } else {
            linelen = (size_t)(end - p);
            p = end;
        }
        if (linelen > 0 && line[linelen - 1] == '\r')
            linelen--;
        if (linelen == 0)
            continue;
        if (linelen >= sizeof buf)
            goto fail;
        memcpy(buf, line, linelen);
        buf[linelen] = 0;

        f0 = buf;
        f1 = strchr(f0, ',');
        if (!f1)
            goto fail;
        *f1++ = 0;
        f2 = strchr(f1, ',');
        if (!f2)
            goto fail;
        *f2++ = 0;
        f3 = strchr(f2, ',');
        if (!f3)
            goto fail;
        *f3++ = 0;
        f4 = strchr(f3, ',');
        if (!f4)
            goto fail;
        *f4++ = 0;

        memset(&e, 0, sizeof e);
        if (parse_u32(f0, &e.offset) != 0 || parse_u32(f1, &e.size) != 0)
            goto fail;
        if (parse_u32(f3, &compressed) != 0 || parse_u32(f4, &extract) != 0)
            goto fail;
        if (!f2[0] || strlen(f2) >= sizeof e.name)
            goto fail;
        memcpy(e.name, f2, strlen(f2) + 1);
        e.compressed = (uint8_t)(compressed == 1);
        e.extract = (uint8_t)(extract == 1);

        if (n == cap) {
            size_t ncap = cap ? cap * 2 : 32;
            PortFilelistEntry *nr = (PortFilelistEntry *)realloc(rows, ncap * sizeof *nr);
            if (!nr)
                goto fail;
            rows = nr;
            cap = ncap;
        }
        rows[n++] = e;
        continue;
    fail:
        free(rows);
        return PORT_FILELIST_ERR_PARSE;
    }

    g_files = rows;
    g_nfiles = n;
    return PORT_FILELIST_OK;
}

int port_filelist_load(const char *path)
{
    FILE *f;
    long sz;
    char *buf;
    int rc;

    if (!path)
        return PORT_FILELIST_ERR_IO;
    f = fopen(path, "rb");
    if (!f)
        return PORT_FILELIST_ERR_IO;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return PORT_FILELIST_ERR_IO;
    }
    sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return PORT_FILELIST_ERR_IO;
    }
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return PORT_FILELIST_ERR_IO;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return PORT_FILELIST_ERR_IO;
    }
    fclose(f);
    buf[sz] = 0;
    rc = port_filelist_parse(buf, (size_t)sz);
    free(buf);
    return rc;
}

const PortFilelistEntry *port_filelist_find(const char *name)
{
    size_t i;
    if (!name)
        return NULL;
    for (i = 0; i < g_nfiles; i++) {
        if (strcmp(g_files[i].name, name) == 0)
            return &g_files[i];
    }
    return NULL;
}

const PortFilelistEntry *port_filelist_find_offset(uint32_t rom_off, uint32_t size)
{
    size_t i;
    for (i = 0; i < g_nfiles; i++) {
        uint32_t b = g_files[i].offset;
        uint32_t local;
        if (rom_off < b)
            continue;
        local = rom_off - b;
        if (local > g_files[i].size)
            continue;
        if (size > g_files[i].size - local)
            continue;
        return &g_files[i];
    }
    return NULL;
}

const PortFilelistEntry *port_filelist_at(size_t i)
{
    if (i >= g_nfiles)
        return NULL;
    return &g_files[i];
}

size_t port_filelist_count(void) { return g_nfiles; }
