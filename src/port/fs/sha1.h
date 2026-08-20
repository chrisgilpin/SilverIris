#ifndef SILVERIRIS_SHA1_H
#define SILVERIRIS_SHA1_H

#include <stddef.h>
#include <stdint.h>

void silveriris_sha1(const uint8_t *data, size_t len, uint8_t out[20]);
void silveriris_sha1_hex(const uint8_t digest[20], char hex[41]);

#endif
