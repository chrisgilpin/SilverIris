#ifndef SILVERIRIS_INFLATE1172_H
#define SILVERIRIS_INFLATE1172_H

#include <stddef.h>
#include <stdint.h>

/* Rare "1172": 2-byte header then raw deflate (extractor inflate1172.ts / puff). */
#define PORT_INFLATE1172_HEADER 2
#define PORT_INFLATE1172_MAX (2u * 1024u * 1024u)

#define PORT_INFLATE1172_OK 0
#define PORT_INFLATE1172_ERR_SRC -1
#define PORT_INFLATE1172_ERR_PUFF -2
#define PORT_INFLATE1172_ERR_SIZE -3

/*
 * Inflate a 1172 blob. dst may be NULL to size-scan (out_len is filled).
 * Same stream as web/extractor inflate1172.ts (skip 2, raw deflate).
 */
int port_inflate1172(const uint8_t *src, size_t src_len, uint8_t *dst,
                     size_t dst_cap, size_t *out_len);

/* Rare name. src_len / dst_cap are PORT (N64 used a scratch heap). */
int bgDecompress(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap,
                 size_t *out_len);

#endif
