#ifndef SILVERIRIS_TMEM_H
#define SILVERIRIS_TMEM_H

#include <stddef.h>
#include <stdint.h>

#include "fs/c0pack.h"

/* Rare TEXFORMAT_* (image.h). SITX testdata uses the same numbers. */
#define G1_TEX_RGBA16 0x01
#define G1_TEX_I8 0x07
#define G1_TEX_I4 0x08
#define G1_TEX_CI8 0x09
#define G1_TEX_CI4 0x0a

#define G1_TMEM_BYTES 4096
#define G1_SITX_MAGIC "SITX"

/* Names from images.def (generated, no texels). */
const char *g1_image_bank_name(unsigned id);
unsigned g1_image_bank_count(void);

void g1_tex_begin_dl(void);
void g1_tex_set_pack(const C0Pack *pack);
void g1_tex_set_scale(float s, float t);
void g1_tex_unload(void);

int g1_tex_load_raw(unsigned id, uint8_t fmt, unsigned w, unsigned h,
                    const uint8_t *texels, size_t ntex, const uint16_t *tlut,
                    unsigned ntlut);
int g1_tex_load_sitx(unsigned id, const uint8_t *bytes, size_t n);

/* Decode gsSPUseTexture / G_SETTEX (0xC0). texture_id = w1 & 0xfff. */
int g1_tex_settex(uint32_t w0, uint32_t w1);

int g1_tex_bound(void);
int g1_tex_sample(float s, float t, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);

unsigned g1_tex_settex_count(void);
unsigned g1_tex_ok_count(void);
unsigned g1_tex_miss_count(void);
uint16_t g1_tex_last_id(void);

#endif
