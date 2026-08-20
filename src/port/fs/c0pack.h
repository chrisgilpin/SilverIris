#ifndef SILVERIRIS_C0PACK_H
#define SILVERIRIS_C0PACK_H

#include <stddef.h>
#include <stdint.h>

#define C0PACK_MAGIC "C0PK"
#define C0PACK_VERSION 1
#define C0PACK_HEADER_SIZE 44

typedef struct {
    const char *path;
    const uint8_t *bytes;
    size_t size;
} C0File;

typedef struct {
    const char *path;
    uint16_t path_len;
    const uint8_t *bytes;
    uint32_t size;
} C0PackEntry;

typedef struct {
    const uint8_t *buf;
    size_t len;
    uint8_t region;
    uint8_t flags;
    uint8_t pack_hash[32];
    uint32_t nfiles;
    C0PackEntry *files;
} C0Pack;

/* Build a v1 store-only pack. Caller frees *out. Returns 0 on success. */
int c0pack_build(const C0File *files, size_t nfiles, uint8_t region, uint8_t flags,
                 uint8_t **out, size_t *out_len, uint8_t pack_hash[32]);

/* Validate pack bytes. Returns 0 if header/trailer/canonical hash match. */
int c0pack_validate(const uint8_t *buf, size_t len, uint8_t pack_hash[32]);

/* Index an already-validated pack. files[] path/bytes point into buf. */
int c0pack_open(const uint8_t *buf, size_t len, C0Pack *out);
void c0pack_close(C0Pack *p);
const C0PackEntry *c0pack_find(const C0Pack *p, const char *path);
const C0PackEntry *c0pack_find_tail(const C0Pack *p, const char *suffix);

#endif
