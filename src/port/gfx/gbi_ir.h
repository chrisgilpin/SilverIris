#ifndef SILVERIRIS_GBI_IR_H
#define SILVERIRIS_GBI_IR_H

#include <stdint.h>

/*
 * Shared G1/G2 command stream. The interpreter owns RSP T&L and emits
 * clip-space tris; the raster (G1) or GL backend (G2) never sees gSPMatrix.
 */

typedef enum {
    GIR_SET_VIEWPORT,
    GIR_SET_SCISSOR,
    GIR_SET_COMBINE,
    GIR_SET_OTHERMODE,
    GIR_BIND_TEX,
    GIR_SET_FOG,
    GIR_DRAW_TRIS,
    GIR_DRAW_RECT,
    GIR_SYNC
} GirOp;

typedef struct {
    float x, y, z, w;
    uint8_t r, g, b, a;
} GirVert;

#define G1_MAX_CMDS 4096

typedef struct {
    GirOp op;
    union {
        struct {
            float scale_x, scale_y, trans_x, trans_y;
        } viewport;
        struct {
            int ulx, uly, lrx, lry;
            uint8_t r, g, b, a;
        } rect;
        struct {
            GirVert v[3];
        } tri;
    } u;
} GirCmd;

typedef struct {
    GirCmd cmds[G1_MAX_CMDS];
    uint32_t ncmds;
} GirList;

#endif
