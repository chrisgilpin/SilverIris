#include "sha1.h"

#include <stdlib.h>
#include <string.h>

static uint32_t rotl(uint32_t x, uint32_t n) { return (x << n) | (x >> (32 - n)); }

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

void silveriris_sha1(const uint8_t *data, size_t len, uint8_t out[20])
{
    uint32_t h[5] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u};
    size_t pad_len = (len % 64 < 56) ? (56 - (len % 64)) : (120 - (len % 64));
    size_t total = len + pad_len + 8;
    uint8_t *buf = (uint8_t *)malloc(total);
    uint32_t w[80];
    size_t off;
    int i;

    if (!buf) {
        memset(out, 0, 20);
        return;
    }
    memcpy(buf, data, len);
    buf[len] = 0x80;
    memset(buf + len + 1, 0, pad_len - 1);
    {
        uint64_t bits = (uint64_t)len * 8u;
        for (i = 0; i < 8; i++)
            buf[total - 1 - i] = (uint8_t)(bits >> (8 * i));
    }

    for (off = 0; off < total; off += 64) {
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (i = 0; i < 16; i++)
            w[i] = be32(buf + off + (size_t)i * 4);
        for (i = 16; i < 80; i++)
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        for (i = 0; i < 80; i++) {
            uint32_t f, k, temp;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5a827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ed9eba1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdcu;
            } else {
                f = b ^ c ^ d;
                k = 0xca62c1d6u;
            }
            temp = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = temp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    free(buf);
    for (i = 0; i < 5; i++) {
        out[i * 4] = (uint8_t)(h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)h[i];
    }
}

void silveriris_sha1_hex(const uint8_t digest[20], char hex[41])
{
    static const char *digits = "0123456789abcdef";
    int i;
    for (i = 0; i < 20; i++) {
        hex[i * 2] = digits[digest[i] >> 4];
        hex[i * 2 + 1] = digits[digest[i] & 0xf];
    }
    hex[40] = 0;
}
