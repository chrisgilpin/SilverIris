#ifndef SILVERIRIS_GBI_INTERP_H
#define SILVERIRIS_GBI_INTERP_H

#include <stdint.h>
#include <ultra64.h>

#include "gfx/gbi_ir.h"

int g1_interpret_dl(const Gfx *dl, uint32_t n_gfx);
int g1_interpret_task(OSTask *task);
int g1_run_synthetic(void);
const GirList *g1_last_ir(void);
const uint8_t *g1_fb_rgba(void);
void g1_fb_grey_sha256(uint8_t out[32]);

#endif
