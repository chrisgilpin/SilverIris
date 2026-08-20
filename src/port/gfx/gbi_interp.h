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

/* K17: pack DLs stay big-endian; 0x0Nxxxxxx resolves via the 16-entry table. */
void g1_set_segment(unsigned seg, uintptr_t base);
int g1_interpret_be_dl(const uint8_t *bytes, uint32_t n_gfx);
int g1_interpret_be_dl2(const uint8_t *a, uint32_t na, const uint8_t *b, uint32_t nb);

/* Player look-at (theta=0 faces -Z). Identity when cleared. */
void g1_set_lookat(float x, float y, float z, float theta_deg);
void g1_clear_lookat(void);
unsigned g1_fb_nonzero(void);

#endif
