#ifndef SILVERIRIS_TEX_BANK_H
#define SILVERIRIS_TEX_BANK_H

#include <stddef.h>
#include <stdint.h>

/*
 * Rare texture-bank blob (assets/images/split/<name>.bin after extract).
 *
 * Byte 0: u z llllll
 *   u = hasExplicitLods (ignored for G1; we take LOD0)
 *   z = zlib/rarezip paletted tile
 *   l = declared LOD count
 *
 * z=1: format8, (ncol-1)8, ncol * rgba5551, then per LOD: w8 h8 + 1172
 *      (0x11 0x72 + raw deflate) of packed CI indices. Decomp names this
 *      texInflateZlib; the stream is the same 1172 as rooms (puff).
 *
 * z=0: per LOD: format4 w8 h8 method4 + payload (uncompressed / Huffman / RLE
 *      / lookup). G1 keeps tightly packed texels (no N64 TMEM row pad).
 *
 * Not ROM bytes. Synthetic banks live in testdata/stage/.
 */
#define G1_TEX_BANK_OK 0
#define G1_TEX_BANK_ERR -1

#define G1_TEXCOMP_UNCOMPRESSED0 0
#define G1_TEXCOMP_UNCOMPRESSED1 1
#define G1_TEXCOMP_HUFFMAN 2
#define G1_TEXCOMP_HUFFMANPERHCHANNEL 3
#define G1_TEXCOMP_RLE 4
#define G1_TEXCOMP_LOOKUP 5
#define G1_TEXCOMP_HUFFMANLOOKUP 6
#define G1_TEXCOMP_RLELOOKUP 7
#define G1_TEXCOMP_HUFFMANBLUR 8
#define G1_TEXCOMP_RLEBLUR 9

#define G1_TEX_BANK_ZLIB 1
#define G1_TEX_BANK_RARE 0

typedef struct {
    uint8_t fmt;
    uint8_t w, h;
    uint8_t zlib;
    int8_t method;
    uint8_t texels[4096];
    size_t ntex;
    uint16_t tlut[256];
    unsigned ntlut;
} G1TexBankOut;

int g1_tex_bank_decode(const uint8_t *src, size_t n, G1TexBankOut *out);
int g1_tex_load_bank(unsigned id, const uint8_t *bytes, size_t n);

#endif
