#include "c0pack.h"
#include "sha256.h"

#include <stdlib.h>
#include <string.h>

static int path_ok(const char *path) {
    size_t n;
    if (!path) return 0;
    n = strlen(path);
    if (n == 0 || n > 65535) return 0;
    if (path[0] == '/' || strstr(path, "..") || strchr(path, '\\')) return 0;
    if (strncmp(path, "assets/", 7) != 0 && strncmp(path, "bin/", 4) != 0) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = path[i];
        int ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                 c == '_' || c == '.' || c == '/' || c == '-';
        if (!ok) return 0;
    }
    return 1;
}

static int cmp_file(const void *a, const void *b) {
    const C0File *fa = (const C0File *)a;
    const C0File *fb = (const C0File *)b;
    return strcmp(fa->path, fb->path);
}

static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static void wr64(uint8_t *p, uint64_t v) {
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int c0pack_build(const C0File *files, size_t nfiles, uint8_t region, uint8_t flags,
                 uint8_t **out, size_t *out_len, uint8_t pack_hash[32]) {
    C0File *sorted;
    size_t canon_len = 0, manifest_len = 0, blob_len = 0, total, i;
    uint8_t *canon, *buf;
    size_t o, blob_off, blob_start;

    if (flags & 1) return -1;
    if (region > 2) return -1;
    sorted = (C0File *)malloc(nfiles * sizeof(C0File));
    if (!sorted) return -1;
    memcpy(sorted, files, nfiles * sizeof(C0File));
    qsort(sorted, nfiles, sizeof(C0File), cmp_file);

    for (i = 0; i < nfiles; i++) {
        size_t plen;
        uint8_t digest[32];
        if (!path_ok(sorted[i].path)) {
            free(sorted);
            return -2;
        }
        plen = strlen(sorted[i].path);
        canon_len += 2 + plen + 4 + 32 + 1;
        manifest_len += 2 + plen + 8 + 4 + 32 + 1;
        blob_len += sorted[i].size;
        (void)digest;
    }

    canon = (uint8_t *)malloc(canon_len ? canon_len : 1);
    if (!canon) {
        free(sorted);
        return -1;
    }
    o = 0;
    for (i = 0; i < nfiles; i++) {
        size_t plen = strlen(sorted[i].path);
        uint8_t digest[32];
        silveriris_sha256(sorted[i].bytes, sorted[i].size, digest);
        wr16(canon + o, (uint16_t)plen);
        o += 2;
        memcpy(canon + o, sorted[i].path, plen);
        o += plen;
        wr32(canon + o, (uint32_t)sorted[i].size);
        o += 4;
        memcpy(canon + o, digest, 32);
        o += 32;
        canon[o] = 0;
        o += 1;
    }
    silveriris_sha256(canon, canon_len, pack_hash);
    free(canon);

    total = C0PACK_HEADER_SIZE + manifest_len + blob_len + 32;
    buf = (uint8_t *)calloc(1, total);
    if (!buf) {
        free(sorted);
        return -1;
    }
    memcpy(buf, C0PACK_MAGIC, 4);
    wr16(buf + 4, C0PACK_VERSION);
    buf[6] = region;
    buf[7] = flags;
    wr32(buf + 8, (uint32_t)nfiles);
    memcpy(buf + 12, pack_hash, 32);

    o = C0PACK_HEADER_SIZE;
    blob_off = 0;
    for (i = 0; i < nfiles; i++) {
        size_t plen = strlen(sorted[i].path);
        uint8_t digest[32];
        silveriris_sha256(sorted[i].bytes, sorted[i].size, digest);
        wr16(buf + o, (uint16_t)plen);
        o += 2;
        memcpy(buf + o, sorted[i].path, plen);
        o += plen;
        wr64(buf + o, (uint64_t)blob_off);
        o += 8;
        wr32(buf + o, (uint32_t)sorted[i].size);
        o += 4;
        memcpy(buf + o, digest, 32);
        o += 32;
        buf[o] = 0;
        o += 1;
        blob_off += sorted[i].size;
    }
    blob_start = o;
    blob_off = 0;
    for (i = 0; i < nfiles; i++) {
        memcpy(buf + blob_start + blob_off, sorted[i].bytes, sorted[i].size);
        blob_off += sorted[i].size;
    }
    memcpy(buf + blob_start + blob_len, pack_hash, 32);
    free(sorted);
    *out = buf;
    *out_len = total;
    return 0;
}

int c0pack_validate(const uint8_t *buf, size_t len, uint8_t pack_hash[32]) {
    uint32_t nfiles, i;
    size_t o, blob_start, blob_len = 0;
    const uint8_t *header_hash, *trailer;
    uint8_t *canon;
    size_t canon_len = 0, canon_o;
    uint8_t recomputed[32];

    if (len < C0PACK_HEADER_SIZE + 32) return -1;
    if (memcmp(buf, C0PACK_MAGIC, 4) != 0) return -2;
    if (rd16(buf + 4) != C0PACK_VERSION) return -3;
    if (buf[6] > 2) return -4;
    nfiles = rd32(buf + 8);
    header_hash = buf + 12;
    o = C0PACK_HEADER_SIZE;
    for (i = 0; i < nfiles; i++) {
        uint16_t plen;
        uint32_t size;
        if (o + 2 > len) return -5;
        plen = rd16(buf + o);
        o += 2;
        if (o + plen + 8 + 4 + 32 + 1 > len) return -5;
        o += plen;
        o += 8;
        size = rd32(buf + o);
        o += 4;
        o += 32;
        if (buf[o] != 0) return -6;
        o += 1;
        blob_len += size;
        canon_len += 2 + plen + 4 + 32 + 1;
    }
    blob_start = o;
    if (blob_start + blob_len + 32 > len) return -5;
    trailer = buf + blob_start + blob_len;
    if (memcmp(header_hash, trailer, 32) != 0) return -7;

    canon = (uint8_t *)malloc(canon_len ? canon_len : 1);
    if (!canon) return -1;
    o = C0PACK_HEADER_SIZE;
    canon_o = 0;
    for (i = 0; i < nfiles; i++) {
        uint16_t plen = rd16(buf + o);
        uint32_t size;
        uint8_t digest[32];
        const uint8_t *payload;
        uint64_t off;
        o += 2;
        memcpy(canon + canon_o, buf + o - 2, 2);
        canon_o += 2;
        memcpy(canon + canon_o, buf + o, plen);
        canon_o += plen;
        o += plen;
        off = (uint64_t)rd32(buf + o) | ((uint64_t)rd32(buf + o + 4) << 32);
        o += 8;
        size = rd32(buf + o);
        wr32(canon + canon_o, size);
        canon_o += 4;
        o += 4;
        memcpy(canon + canon_o, buf + o, 32);
        canon_o += 32;
        payload = buf + blob_start + (size_t)off;
        silveriris_sha256(payload, size, digest);
        if (memcmp(digest, buf + o, 32) != 0) {
            free(canon);
            return -8;
        }
        o += 32;
        canon[canon_o++] = 0;
        o += 1;
    }
    silveriris_sha256(canon, canon_len, recomputed);
    free(canon);
    if (memcmp(recomputed, header_hash, 32) != 0) return -9;
    memcpy(pack_hash, header_hash, 32);
    return 0;
}

int c0pack_open(const uint8_t *buf, size_t len, C0Pack *out)
{
    uint8_t hash[32];
    uint32_t nfiles, i;
    size_t o, blob_start, blob_len = 0;
    C0PackEntry *ents;
    int rc;

    if (!buf || !out)
        return -1;
    memset(out, 0, sizeof *out);
    rc = c0pack_validate(buf, len, hash);
    if (rc != 0)
        return rc;

    nfiles = rd32(buf + 8);
    ents = NULL;
    if (nfiles) {
        ents = (C0PackEntry *)calloc(nfiles, sizeof *ents);
        if (!ents)
            return -1;
    }
    o = C0PACK_HEADER_SIZE;
    for (i = 0; i < nfiles; i++) {
        uint16_t plen = rd16(buf + o);
        uint32_t size;
        o += 2;
        o += plen;
        o += 8;
        size = rd32(buf + o);
        o += 4 + 32 + 1;
        blob_len += size;
    }
    blob_start = o;

    o = C0PACK_HEADER_SIZE;
    for (i = 0; i < nfiles; i++) {
        uint16_t plen = rd16(buf + o);
        uint64_t off;
        uint32_t size;
        o += 2;
        ents[i].path = (const char *)(buf + o);
        ents[i].path_len = plen;
        o += plen;
        off = (uint64_t)rd32(buf + o) | ((uint64_t)rd32(buf + o + 4) << 32);
        o += 8;
        size = rd32(buf + o);
        o += 4 + 32 + 1;
        ents[i].size = size;
        ents[i].bytes = buf + blob_start + (size_t)off;
    }

    out->buf = buf;
    out->len = len;
    out->region = buf[6];
    out->flags = buf[7];
    memcpy(out->pack_hash, hash, 32);
    out->nfiles = nfiles;
    out->files = ents;
    (void)blob_len;
    return 0;
}

void c0pack_close(C0Pack *p)
{
    if (!p)
        return;
    free(p->files);
    memset(p, 0, sizeof *p);
}

const C0PackEntry *c0pack_find(const C0Pack *p, const char *path)
{
    uint32_t i;
    size_t n;
    if (!p || !path)
        return NULL;
    n = strlen(path);
    for (i = 0; i < p->nfiles; i++) {
        if (p->files[i].path_len == n && memcmp(p->files[i].path, path, n) == 0)
            return &p->files[i];
    }
    return c0pack_find_tail(p, path);
}

const C0PackEntry *c0pack_find_tail(const C0Pack *p, const char *suffix)
{
    uint32_t i;
    size_t n, plen;
    const char *slash;
    if (!p || !suffix)
        return NULL;
    slash = strrchr(suffix, '/');
    if (slash && slash[1])
        suffix = slash + 1;
    n = strlen(suffix);
    if (n == 0)
        return NULL;
    for (i = 0; i < p->nfiles; i++) {
        plen = p->files[i].path_len;
        if (plen >= n && memcmp(p->files[i].path + (plen - n), suffix, n) == 0)
            return &p->files[i];
    }
    return NULL;
}
