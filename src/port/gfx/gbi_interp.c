#include "gfx/gbi_interp.h"

#include "gfx/gbi_trace.h"
#include "gfx/sw_raster.h"
#include "gfx/tmem.h"

#include <math.h>
#include <string.h>

#define G1_VTX 16
#define G1_MV_STACK 10
#define G1_MAX_DEPTH 32

typedef struct {
    float clip[4];
    float s, t;
    uint8_t r, g, b, a;
} Slot;

static GirList g_ir;
static uintptr_t g_seg[16];
static float g_mv[4][4], g_proj[4][4], g_mvp[4][4];
static float g_mvstack[G1_MV_STACK][4][4];
static int g_mvsp;
static Slot g_slot[G1_VTX];
static uint8_t g_fill[4];
static Mtx g_mtx_host;
static Vtx g_vtx_host[G1_VTX];
static float g_cam_eye[3];
static float g_cam_theta;
static int g_cam_on;
#define G1_PI 3.1415927f

static uint32_t gfx_w0(const Gfx *g) { return (uint32_t)g->words.w0; }
static uint32_t gfx_w1(const Gfx *g) { return (uint32_t)g->words.w1; }
static uint8_t gfx_cmd(const Gfx *g) { return (uint8_t)(gfx_w0(g) >> 24); }

static uint16_t rd_be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void mtx_ident(float m[4][4])
{
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            m[i][j] = (i == j) ? 1.f : 0.f;
}

static void mtx_mul(float out[4][4], const float a[4][4], const float b[4][4])
{
    float t[4][4];
    int i, j, k;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            float s = 0.f;
            for (k = 0; k < 4; k++)
                s += a[i][k] * b[k][j];
            t[i][j] = s;
        }
    }
    memcpy(out, t, sizeof t);
}

static void mtx_copy(float dst[4][4], const float src[4][4])
{
    memcpy(dst, src, 16 * sizeof(float));
}

static void rebuild_mvp(void) { mtx_mul(g_mvp, g_proj, g_mv); }

static const void *resolve_addr(uint32_t addr32, uintptr_t full)
{
    uint32_t seg = (addr32 >> 24) & 0xF;
    uint32_t off = addr32 & 0x00FFFFFFu;
    if (g_seg[seg] != 0)
        return (const void *)(g_seg[seg] + off);
    if (!full)
        return NULL;
    return (const void *)full;
}

static void emit(const GirCmd *c)
{
    if (g_ir.ncmds >= G1_MAX_CMDS)
        return;
    g_ir.cmds[g_ir.ncmds++] = *c;
}

static void unpack_fill(uint32_t packed)
{
    uint16_t c = (uint16_t)packed;
    unsigned r5 = (c >> 11) & 0x1f, g5 = (c >> 6) & 0x1f, b5 = (c >> 1) & 0x1f;
    g_fill[0] = (uint8_t)((r5 << 3) | (r5 >> 2));
    g_fill[1] = (uint8_t)((g5 << 3) | (g5 >> 2));
    g_fill[2] = (uint8_t)((b5 << 3) | (b5 >> 2));
    g_fill[3] = (c & 1) ? 255 : 0;
}

static void apply_matrix(uint32_t w0, const Mtx *src)
{
    uint32_t params = (w0 >> 16) & 0xFF;
    float nf[4][4];
    if (!src)
        return;
    g0_mtx_l2f(src, nf);
    if (params & G_MTX_PROJECTION) {
        if (params & G_MTX_LOAD)
            mtx_copy(g_proj, nf);
        else
            mtx_mul(g_proj, g_proj, nf);
    } else {
        if (params & G_MTX_PUSH && g_mvsp < G1_MV_STACK) {
            mtx_copy(g_mvstack[g_mvsp], g_mv);
            g_mvsp++;
        }
        if (params & G_MTX_LOAD)
            mtx_copy(g_mv, nf);
        else
            mtx_mul(g_mv, g_mv, nf);
    }
    rebuild_mvp();
}

static void apply_vtx(uint32_t w0, const Vtx *src)
{
    uint32_t param = (w0 >> 16) & 0xFF;
    uint32_t n = (param >> 4) + 1;
    uint32_t v0 = param & 0xF;
    uint32_t i;
    if (!src)
        return;
    for (i = 0; i < n && v0 + i < G1_VTX; i++) {
        const Vtx_t *v = &src[i].v;
        float x = (float)v->ob[0], y = (float)v->ob[1], z = (float)v->ob[2];
        Slot *s = &g_slot[v0 + i];
        s->clip[0] = g_mvp[0][0] * x + g_mvp[0][1] * y + g_mvp[0][2] * z + g_mvp[0][3];
        s->clip[1] = g_mvp[1][0] * x + g_mvp[1][1] * y + g_mvp[1][2] * z + g_mvp[1][3];
        s->clip[2] = g_mvp[2][0] * x + g_mvp[2][1] * y + g_mvp[2][2] * z + g_mvp[2][3];
        s->clip[3] = g_mvp[3][0] * x + g_mvp[3][1] * y + g_mvp[3][2] * z + g_mvp[3][3];
        s->s = (float)v->tc[0];
        s->t = (float)v->tc[1];
        if (!v->cn[0] && !v->cn[1] && !v->cn[2]) {
            /* Untextured grey so a black Vtx.cn still paints. */
            s->r = 180;
            s->g = 180;
            s->b = 180;
            s->a = 255;
        } else {
            s->r = v->cn[0];
            s->g = v->cn[1];
            s->b = v->cn[2];
            s->a = v->cn[3];
        }
    }
}

static const Mtx *mtx_from_be(const uint8_t *p)
{
    uint32_t *dst = (uint32_t *)&g_mtx_host.m[0][0];
    int i;
    if (!p)
        return NULL;
    for (i = 0; i < 16; i++)
        dst[i] = rd_be32(p + (size_t)i * 4);
    return &g_mtx_host;
}

static const Vtx *vtx_from_be(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    if (!p)
        return NULL;
    memset(g_vtx_host, 0, sizeof g_vtx_host);
    for (i = 0; i < n && i < G1_VTX; i++) {
        const uint8_t *s = p + i * 16u;
        g_vtx_host[i].v.ob[0] = (s16)rd_be16(s);
        g_vtx_host[i].v.ob[1] = (s16)rd_be16(s + 2);
        g_vtx_host[i].v.ob[2] = (s16)rd_be16(s + 4);
        g_vtx_host[i].v.tc[0] = (s16)rd_be16(s + 8);
        g_vtx_host[i].v.tc[1] = (s16)rd_be16(s + 10);
        g_vtx_host[i].v.cn[0] = s[12];
        g_vtx_host[i].v.cn[1] = s[13];
        g_vtx_host[i].v.cn[2] = s[14];
        g_vtx_host[i].v.cn[3] = s[15];
    }
    return g_vtx_host;
}

static void load_matrix(uint32_t w0, uintptr_t full)
{
    const Mtx *src = (const Mtx *)resolve_addr((uint32_t)full, full);
    apply_matrix(w0, src);
}

static void load_vtx(uint32_t w0, uintptr_t full)
{
    const Vtx *src = (const Vtx *)resolve_addr((uint32_t)full, full);
    apply_vtx(w0, src);
}

/* Fast3D G_TRI1 stores indices * 10. Rare G_TRI4 stores raw 0..15. */
#ifndef G_TRI4
#define G_TRI4 (G_IMMFIRST - 14)
#endif
#ifndef G_SETTEX
#define G_SETTEX 0xc0
#endif

static void emit_indexed_tri(uint32_t i0, uint32_t i1, uint32_t i2)
{
    GirCmd c;
    int k;
    Slot *idx[3];
    if (i0 == 0 && i1 == 0 && i2 == 0)
        return;
    if (i0 >= G1_VTX || i1 >= G1_VTX || i2 >= G1_VTX)
        return;
    idx[0] = &g_slot[i0];
    idx[1] = &g_slot[i1];
    idx[2] = &g_slot[i2];
    memset(&c, 0, sizeof c);
    c.op = GIR_DRAW_TRIS;
    for (k = 0; k < 3; k++) {
        c.u.tri.v[k].x = idx[k]->clip[0];
        c.u.tri.v[k].y = idx[k]->clip[1];
        c.u.tri.v[k].z = idx[k]->clip[2];
        c.u.tri.v[k].w = idx[k]->clip[3];
        c.u.tri.v[k].s = idx[k]->s;
        c.u.tri.v[k].t = idx[k]->t;
        c.u.tri.v[k].r = idx[k]->r;
        c.u.tri.v[k].g = idx[k]->g;
        c.u.tri.v[k].b = idx[k]->b;
        c.u.tri.v[k].a = idx[k]->a;
    }
    c.u.tri.tex_slot = (int8_t)g1_tex_current_slot();
    emit(&c);
}

static void emit_tri(uint32_t w1)
{
    uint32_t i0 = ((w1 >> 16) & 0xFF) / 10;
    uint32_t i1 = ((w1 >> 8) & 0xFF) / 10;
    uint32_t i2 = (w1 & 0xFF) / 10;
    emit_indexed_tri(i0, i1, i2);
}

/* Rare G_TRI4 (0xB1): four nibble-indexed tris. All-zero slots are skipped. */
static void emit_tri4(uint32_t w0, uint32_t w1)
{
    emit_indexed_tri(w1 & 0xF, (w1 >> 4) & 0xF, w0 & 0xF);
    emit_indexed_tri((w1 >> 8) & 0xF, (w1 >> 12) & 0xF, (w0 >> 4) & 0xF);
    emit_indexed_tri((w1 >> 16) & 0xF, (w1 >> 20) & 0xF, (w0 >> 8) & 0xF);
    emit_indexed_tri((w1 >> 24) & 0xF, (w1 >> 28) & 0xF, (w0 >> 12) & 0xF);
}

static void emit_fillrect(uint32_t w0, uint32_t w1)
{
    GirCmd c;
    memset(&c, 0, sizeof c);
    c.op = GIR_DRAW_RECT;
    c.u.rect.lrx = (int)((w0 >> 14) & 0x3FF);
    c.u.rect.lry = (int)((w0 >> 2) & 0x3FF);
    c.u.rect.ulx = (int)((w1 >> 14) & 0x3FF);
    c.u.rect.uly = (int)((w1 >> 2) & 0x3FF);
    c.u.rect.r = g_fill[0];
    c.u.rect.g = g_fill[1];
    c.u.rect.b = g_fill[2];
    c.u.rect.a = g_fill[3];
    emit(&c);
}

static void reset_state(void)
{
    GirCmd vp;
    memset(&g_ir, 0, sizeof g_ir);
    memset(g_seg, 0, sizeof g_seg);
    memset(g_slot, 0, sizeof g_slot);
    g1_tex_begin_dl();
    mtx_ident(g_mv);
    mtx_ident(g_proj);
    rebuild_mvp();
    g_mvsp = 0;
    g_fill[0] = g_fill[1] = g_fill[2] = 0;
    g_fill[3] = 255;
    memset(&vp, 0, sizeof vp);
    vp.op = GIR_SET_VIEWPORT;
    vp.u.viewport.scale_x = 160.f;
    vp.u.viewport.scale_y = 120.f;
    vp.u.viewport.trans_x = 160.f;
    vp.u.viewport.trans_y = 120.f;
    emit(&vp);
}

void g1_set_segment(unsigned seg, uintptr_t base)
{
    if (seg < 16)
        g_seg[seg] = base;
}

static int dispatch(uint8_t cmd, uint32_t w0, uint32_t w1, uintptr_t w1_full, int be)
{
    if (cmd == (uint8_t)G_MOVEWORD && (w0 & 0xFF) == G_MW_SEGMENT) {
        uint32_t seg = ((w0 >> 8) & 0xFFFF) / 4;
        if (seg < 16)
            g_seg[seg] = (uintptr_t)w1;
        return 0;
    }
    if (cmd == (uint8_t)G_MTX) {
        if (be) {
            const uint8_t *p = (const uint8_t *)resolve_addr(w1, 0);
            apply_matrix(w0, mtx_from_be(p));
        } else {
            load_matrix(w0, w1_full);
        }
        return 0;
    }
    if (cmd == (uint8_t)G_VTX) {
        if (be) {
            uint32_t param = (w0 >> 16) & 0xFF;
            uint32_t n = (param >> 4) + 1;
            const uint8_t *p = (const uint8_t *)resolve_addr(w1, 0);
            apply_vtx(w0, vtx_from_be(p, n));
        } else {
            load_vtx(w0, w1_full);
        }
        return 0;
    }
    if (cmd == (uint8_t)G_TRI1) {
        emit_tri(w1);
        return 0;
    }
    if (cmd == (uint8_t)G_TRI4) {
        emit_tri4(w0, w1);
        return 0;
    }
    if (cmd == (uint8_t)G_SETTEX) {
        g1_tex_settex(w0, w1);
        return 0;
    }
    if (cmd == (uint8_t)G_TEXTURE) {
        float ss = (float)((w1 >> 16) & 0xffff) / 65536.f;
        float st = (float)(w1 & 0xffff) / 65536.f;
        g1_tex_set_scale(ss, st);
        return 0;
    }
    if (cmd == G_SETFILLCOLOR) {
        unpack_fill(w1);
        return 0;
    }
    if (cmd == G_FILLRECT) {
        emit_fillrect(w0, w1);
        return 0;
    }
    if (cmd == (uint8_t)G_POPMTX) {
        if (g_mvsp > 0) {
            g_mvsp--;
            mtx_copy(g_mv, g_mvstack[g_mvsp]);
            rebuild_mvp();
        }
        return 0;
    }
    return 1;
}

static void walk(const Gfx *start, uint32_t n_hint)
{
    const Gfx *stack[G1_MAX_DEPTH];
    int sp = 0;
    const Gfx *ip = start;
    uint32_t steps = 0;
    uint32_t limit = n_hint ? n_hint * 4u + 64u : 4096u;

    while (ip && steps < limit) {
        uint8_t cmd = gfx_cmd(ip);
        uint32_t w0 = gfx_w0(ip);
        uint32_t w1 = gfx_w1(ip);
        steps++;

        if (cmd == (uint8_t)G_DL) {
            uint32_t push = (w0 >> 16) & 0xFF;
            const Gfx *child = (const Gfx *)resolve_addr(w1, ip->words.w1);
            if (!child) {
                /* Unresolved branch: do not abort the walk. */
                ip++;
                continue;
            }
            if (push == G_DL_NOPUSH) {
                ip = child;
                continue;
            }
            if (sp < G1_MAX_DEPTH) {
                stack[sp++] = ip + 1;
                ip = child;
                continue;
            }
        } else if (cmd == (uint8_t)G_ENDDL) {
            if (sp == 0)
                break;
            ip = stack[--sp];
            continue;
        } else {
            dispatch(cmd, w0, w1, ip->words.w1, 0);
        }
        ip++;
        if (sp == 0 && n_hint && (ip < start || ip >= start + n_hint))
            break;
    }
}

static void walk_be(const uint8_t *start, uint32_t n_gfx)
{
    const uint8_t *stack[G1_MAX_DEPTH];
    int sp = 0;
    const uint8_t *ip = start;
    const uint8_t *end = start + (size_t)n_gfx * 8u;
    uint32_t steps = 0;
    uint32_t limit = n_gfx * 4u + 64u;

    while (ip && ip + 8 <= end && steps < limit) {
        uint32_t w0 = rd_be32(ip);
        uint32_t w1 = rd_be32(ip + 4);
        uint8_t cmd = (uint8_t)(w0 >> 24);
        steps++;

        if (cmd == (uint8_t)G_DL) {
            uint32_t push = (w0 >> 16) & 0xFF;
            const uint8_t *child = (const uint8_t *)resolve_addr(w1, 0);
            if (!child) {
                ip += 8;
                continue;
            }
            if (push == G_DL_NOPUSH) {
                ip = child;
                continue;
            }
            if (sp < G1_MAX_DEPTH) {
                stack[sp++] = ip + 8;
                ip = child;
                continue;
            }
        } else if (cmd == (uint8_t)G_ENDDL) {
            if (sp == 0)
                break;
            ip = stack[--sp];
            continue;
        } else {
            dispatch(cmd, w0, w1, 0, 1);
        }
        ip += 8;
        if (sp == 0 && ip >= end)
            break;
    }
}

static void apply_stored_camera(void)
{
    float th, fx, fz, rx, rz;
    float V[4][4], P[4][4];
    float fovy, aspect, n, f, ft;
    if (!g_cam_on)
        return;
    th = g_cam_theta * (G1_PI / 180.f);
    fx = sinf(th);
    fz = -cosf(th);
    rx = cosf(th);
    rz = sinf(th);
    mtx_ident(V);
    V[0][0] = rx;
    V[0][2] = rz;
    V[0][3] = -(rx * g_cam_eye[0] + rz * g_cam_eye[2]);
    V[1][1] = 1.f;
    V[1][3] = -g_cam_eye[1];
    V[2][0] = -fx;
    V[2][2] = -fz;
    V[2][3] = fx * g_cam_eye[0] + fz * g_cam_eye[2];
    fovy = 60.f * (G1_PI / 180.f);
    aspect = 320.f / 240.f;
    n = 10.f;
    f = 8000.f;
    ft = 1.f / tanf(fovy * 0.5f);
    mtx_ident(P);
    P[0][0] = ft / aspect;
    /* Viewport +Y is down; negate so +Y world is up on the canvas. */
    P[1][1] = -ft;
    P[2][2] = (n + f) / (n - f);
    P[2][3] = (2.f * n * f) / (n - f);
    P[3][2] = -1.f;
    P[3][3] = 0.f;
    mtx_copy(g_mv, V);
    mtx_copy(g_proj, P);
    rebuild_mvp();
}

void g1_set_lookat(float x, float y, float z, float theta_deg)
{
    g_cam_eye[0] = x;
    g_cam_eye[1] = y;
    g_cam_eye[2] = z;
    g_cam_theta = theta_deg;
    g_cam_on = 1;
}

void g1_clear_lookat(void) { g_cam_on = 0; }

unsigned g1_fb_nonzero(void) { return sw_fb_nonzero(); }

int g1_interpret_dl(const Gfx *dl, uint32_t n_gfx)
{
    if (!dl)
        return -1;
    reset_state();
    apply_stored_camera();
    walk(dl, n_gfx);
    sw_raster_clear(0, 0, 0, 255);
    sw_raster_list(&g_ir);
    return 0;
}

static int interpret_be_common(const uint8_t *a, uint32_t na, const uint8_t *b, uint32_t nb)
{
    uintptr_t saved[16];
    if (!a || na == 0)
        return -1;
    memcpy(saved, g_seg, sizeof saved);
    reset_state();
    memcpy(g_seg, saved, sizeof saved);
    apply_stored_camera();
    walk_be(a, na);
    if (b && nb)
        walk_be(b, nb);
    sw_raster_clear(0, 0, 0, 255);
    sw_raster_list(&g_ir);
    return 0;
}

int g1_interpret_be_dl(const uint8_t *bytes, uint32_t n_gfx)
{
    return interpret_be_common(bytes, n_gfx, NULL, 0);
}

int g1_interpret_be_dl2(const uint8_t *a, uint32_t na, const uint8_t *b, uint32_t nb)
{
    return interpret_be_common(a, na, b, nb);
}

int g1_interpret_rooms(const G1RoomDl *rooms, int n)
{
    uintptr_t saved[16];
    float eye[3];
    int i, walked;

    if (!rooms || n < 1)
        return -1;

    /* One room, no neighbor offset: identical to the pre-walk path so the
     * G1 greyscale hash and room-1 SETTEX/clip tests stay bit-identical. */
    if (n == 1 && rooms[0].pri && rooms[0].pri_n) {
        if (rooms[0].vtx)
            g1_set_segment(14, rooms[0].vtx);
        if (rooms[0].ox == 0.f && rooms[0].oy == 0.f && rooms[0].oz == 0.f)
            return interpret_be_common(rooms[0].pri, rooms[0].pri_n, rooms[0].sec,
                                       rooms[0].sec_n);
    }

    memcpy(saved, g_seg, sizeof saved);
    eye[0] = g_cam_eye[0];
    eye[1] = g_cam_eye[1];
    eye[2] = g_cam_eye[2];
    reset_state();
    memcpy(g_seg, saved, sizeof saved);

    walked = 0;
    for (i = 0; i < n; i++) {
        if (!rooms[i].pri || rooms[i].pri_n == 0)
            continue;
        if (rooms[i].vtx)
            g_seg[14] = rooms[i].vtx;
        g_cam_eye[0] = eye[0] - rooms[i].ox;
        g_cam_eye[1] = eye[1] - rooms[i].oy;
        g_cam_eye[2] = eye[2] - rooms[i].oz;
        apply_stored_camera();
        walk_be(rooms[i].pri, rooms[i].pri_n);
        if (rooms[i].sec && rooms[i].sec_n)
            walk_be(rooms[i].sec, rooms[i].sec_n);
        walked++;
    }
    g_cam_eye[0] = eye[0];
    g_cam_eye[1] = eye[1];
    g_cam_eye[2] = eye[2];
    if (!walked)
        return -1;
    sw_raster_clear(0, 0, 0, 255);
    sw_raster_list(&g_ir);
    return 0;
}

int g1_interpret_task(OSTask *task)
{
    uint32_t n;
    if (!task || task->t.type != M_GFXTASK || !task->t.data_ptr)
        return -1;
    n = task->t.data_size / (uint32_t)sizeof(Gfx);
    return g1_interpret_dl((const Gfx *)task->t.data_ptr, n);
}

int g1_run_synthetic(void)
{
    static Gfx dl[16];
    static Vtx vtx[3];
    static Mtx mv, proj;
    Gfx *g;
    float id[4][4];
    int i, j;

    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            id[i][j] = (i == j) ? 1.f : 0.f;
    g0_mtx_f2l(id, &mv);
    g0_mtx_f2l(id, &proj);

    memset(vtx, 0, sizeof vtx);
    vtx[0].v.ob[0] = -1;
    vtx[0].v.ob[1] = -1;
    vtx[0].v.cn[0] = 255;
    vtx[0].v.cn[1] = 32;
    vtx[0].v.cn[2] = 32;
    vtx[0].v.cn[3] = 255;
    vtx[1].v.ob[0] = 1;
    vtx[1].v.ob[1] = -1;
    vtx[1].v.cn[0] = 32;
    vtx[1].v.cn[1] = 255;
    vtx[1].v.cn[2] = 32;
    vtx[1].v.cn[3] = 255;
    vtx[2].v.ob[1] = 1;
    vtx[2].v.cn[0] = 32;
    vtx[2].v.cn[1] = 32;
    vtx[2].v.cn[2] = 255;
    vtx[2].v.cn[3] = 255;

    g = dl;
    gDPSetFillColor(g++, GPACK_RGBA5551(12, 28, 48, 1) | (GPACK_RGBA5551(12, 28, 48, 1) << 16));
    gDPFillRectangle(g++, 0, 0, G1_FB_W, G1_FB_H);
    gSPMatrix(g++, &mv, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, &proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPVertex(g++, vtx, 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSPEndDisplayList(g++);
    return g1_interpret_dl(dl, (uint32_t)(g - dl));
}

const GirList *g1_last_ir(void) { return &g_ir; }
const uint8_t *g1_fb_rgba(void) { return sw_fb_rgba(); }
void g1_fb_grey_sha256(uint8_t out[32]) { sw_fb_grey_sha256(out); }
