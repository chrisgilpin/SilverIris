#ifndef SILVERIRIS_SHA256_H
#define SILVERIRIS_SHA256_H

#include <stddef.h>
#include <stdint.h>

void silveriris_sha256(const uint8_t *data, size_t len, uint8_t out[32]);
void silveriris_sha256_hex(const uint8_t digest[32], char hex[65]);

#endif
