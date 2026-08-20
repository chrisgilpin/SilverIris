#ifndef SILVERIRIS_GBI_TRACE_H
#define SILVERIRIS_GBI_TRACE_H

/*
 * G0 dump: segment table + matrices + raw Gfx[] per osSpTaskLoad (§3.6.1).
 * Developer-only files. CI hashes gfx words; never commit asset-bearing dumps.
 */

#include <stddef.h>
#include <stdint.h>
#include <ultra64.h>

#define G0_MAGIC "G0T1"
#define G0_NSEG 16
#define G0_MAX_GFX 8192

typedef struct {
    uint32_t n_gfx;
    uint32_t n_seg;
    uint32_t segment[G0_NSEG];
    uint32_t mtx_modelview[16];
    uint32_t mtx_projection[16];
    uint32_t gfx_w0[G0_MAX_GFX];
    uint32_t gfx_w1[G0_MAX_GFX];
    uint32_t hist[256];
    int have_modelview;
    int have_projection;
} G0Record;

void g0_set_dump_dir(const char *dir);
void g0_reset(void);
int g0_capture_task(OSTask *task);
int g0_write_record(const G0Record *rec, const char *path);
const G0Record *g0_last(void);
void g0_gfx_words_sha256(const G0Record *rec, uint8_t out[32]);
void g0_mtx_f2l(const float mf[4][4], Mtx *m);
void g0_mtx_l2f(const Mtx *m, float mf[4][4]);

#endif
