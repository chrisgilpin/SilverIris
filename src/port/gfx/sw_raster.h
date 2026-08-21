#ifndef SILVERIRIS_SW_RASTER_H
#define SILVERIRIS_SW_RASTER_H

#include "gfx/gbi_ir.h"

#include <stdint.h>

#define G1_FB_W 320
#define G1_FB_H 240

void sw_raster_clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void sw_raster_set_shade_modulate(int on);
int sw_raster_shade_modulate(void);

void sw_raster_list(const GirList *list);
const uint8_t *sw_fb_rgba(void);
void sw_fb_grey_sha256(uint8_t out[32]);
unsigned sw_fb_nonzero(void);

#endif
