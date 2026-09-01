#ifndef SILVERIRIS_GBI_INTERP_H
#define SILVERIRIS_GBI_INTERP_H

#include <stddef.h>
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

/* One raster: walk each room DL into the same IR. vtx is SPSEGMENT_BG_VTX (14).
 * ox/oy/oz shift the stored look-at so room-local verts sit next to room 1. */
typedef struct {
    const uint8_t *pri;
    uint32_t pri_n;
    const uint8_t *sec;
    uint32_t sec_n;
    uintptr_t vtx;
    float ox, oy, oz;
    float yaw;     /* degrees; model +Z onto pad.look */
    float scale;   /* 0 or 1 = identity */
    uintptr_t seg5; /* model file base (0x05) */
    float rx, ry, rz; /* radians; Rare XYZ Euler rest/joint, identity=0 */
    uintptr_t seg4; /* node vertex bank (G_VTX 0x04); 0 = leave unbound */
    int view; /* 1 = camera-space viewmodel; ox/oy/oz after look pitch */
    /* 1 = ignore G_MTX/G_POPMTX. skip=pose rest=skel is already T*R_yaw*R_pose
     * in ox/rx/ry/rz; a LOAD replaces look-at and draws a ceiling slab.
     * G_DL may only follow into seg5 (chr file). Leftover room/BG segs
     * would blit another map area onto the camera.
     * When joints/njoints is set, seg-3 G_MTX is view*base*joint instead of
     * skip — chr DLs palette-switch limb matrices (T-pose arms otherwise). */
    int no_mtx;
    size_t seg5_len; /* model file bytes; skip=pose G_DL stays inside */
    /* Model-space 4x4 column-vector joints (16 floats each). 0 = skip G_MTX. */
    const float *joints;
    int njoints;
    float joint_ymin; /* subtract from joint Y before scale; 0 if unused */
    int joint0;       /* starting MatrixID (enclosing GROUP); 0 = root */
} G1RoomDl;
int g1_interpret_rooms(const G1RoomDl *rooms, int n);

/* Player look-at (theta=0 faces -Z, phi=0 is level). Identity when cleared. */
void g1_set_lookat(float x, float y, float z, float theta_deg);
void g1_set_pitch(float pitch_deg);
void g1_clear_lookat(void);
/* Hor+: vertical FOV stays native; aspect widens hfov. Default 60° / 4:3 so
 * G1 greyscale and the 320x240 harness stay bit-identical until the
 * presenter calls this (live 16:9). */
void g1_set_perspective(float fovy_deg, float aspect);
float g1_persp_fovy(void);
float g1_persp_aspect(void);
/* SHADE*TEXEL (Vtx.cn). Default on. cn=0 skips modulate (G1/SETTEX no-light). */
void g1_set_shade_modulate(int on);
int g1_shade_modulate(void);

unsigned g1_fb_nonzero(void);

#endif
