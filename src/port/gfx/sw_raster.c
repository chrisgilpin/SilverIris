#include "gfx/sw_raster.h"

#include "fs/sha256.h"
#include "gfx/tmem.h"

#include <string.h>

static uint8_t g_fb[G1_FB_H][G1_FB_W][4];
static uint16_t g_zb[G1_FB_H][G1_FB_W];
static float g_sx = 160.f, g_sy = 120.f, g_tx = 160.f, g_ty = 120.f;
/* Rare room verts already carry Vtx.cn. Default SHADE*TEXEL; cn=0 is identity. */
static int g_shade_mod = 1;

void sw_raster_set_shade_modulate(int on) { g_shade_mod = on ? 1 : 0; }
int sw_raster_shade_modulate(void) { return g_shade_mod; }

static void apply_untextured_grey(uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    /* Black Vtx.cn still paints so the G1 synthetic / SETTEX-miss path is visible. */
    if (!(*r | *g | *b)) {
        *r = 180;
        *g = 180;
        *b = 180;
        if (!*a)
            *a = 255;
    }
}

void sw_raster_clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    int y, x;
    for (y = 0; y < G1_FB_H; y++) {
        for (x = 0; x < G1_FB_W; x++) {
            g_fb[y][x][0] = r;
            g_fb[y][x][1] = g;
            g_fb[y][x][2] = b;
            g_fb[y][x][3] = a;
            g_zb[y][x] = 0xffff;
        }
    }
}

const uint8_t *sw_fb_rgba(void) { return &g_fb[0][0][0]; }

unsigned sw_fb_nonzero(void)
{
    unsigned i, n = (unsigned)G1_FB_W * (unsigned)G1_FB_H, c = 0;
    const uint8_t *p = &g_fb[0][0][0];
    for (i = 0; i < n; i++) {
        if (p[i * 4] | p[i * 4 + 1] | p[i * 4 + 2])
            c++;
    }
    return c;
}

void sw_fb_grey_sha256(uint8_t out[32])
{
    uint8_t grey[G1_FB_W * G1_FB_H];
    int i, n = G1_FB_W * G1_FB_H;
    const uint8_t *p = &g_fb[0][0][0];
    for (i = 0; i < n; i++) {
        unsigned r = p[i * 4], g = p[i * 4 + 1], b = p[i * 4 + 2];
        grey[i] = (uint8_t)((r * 77u + g * 150u + b * 29u) >> 8);
    }
    silveriris_sha256(grey, (size_t)n, out);
}

static void put_px(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint16_t z)
{
    if ((unsigned)x >= G1_FB_W || (unsigned)y >= G1_FB_H)
        return;
    /* Alpha 0 is a punch-through miss / portal — do not stamp black. */
    if (a == 0)
        return;
    /* Nearer (smaller 16-bit z) wins. Equal depth keeps last-wins. */
    if (z > g_zb[y][x])
        return;
    g_zb[y][x] = z;
    g_fb[y][x][0] = r;
    g_fb[y][x][1] = g;
    g_fb[y][x][2] = b;
    g_fb[y][x][3] = a;
}

static void fill_rect(int ulx, int uly, int lrx, int lry, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    int x, y;
    if (ulx < 0)
        ulx = 0;
    if (uly < 0)
        uly = 0;
    if (lrx > G1_FB_W)
        lrx = G1_FB_W;
    if (lry > G1_FB_H)
        lry = G1_FB_H;
    for (y = uly; y < lry; y++)
        for (x = ulx; x < lrx; x++)
            put_px(x, y, r, g, b, a, 0xffff);
}

static float edge(float ax, float ay, float bx, float by, float cx, float cy)
{
    return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
}

#define TRI_BLOCK 8
#define TRI_BLOCK2 16
#define BOX_MIX 0
#define BOX_OUT 1
#define BOX_IN -1

static int tri_pix_in(float w0, float w1, float w2, float area)
{
    if (area > 0)
        return w0 >= 0 && w1 >= 0 && w2 >= 0;
    return w0 <= 0 && w1 <= 0 && w2 <= 0;
}

/* Pixel-center rectangle vs triangle. w is linear, so:
 * BOX_OUT: all four corners miss the same half-plane (no inside pixel).
 * BOX_IN: all four corners are inside (convex → every pixel-center is).
 * BOX_MIX: the block still has work. Never drops an inside pixel. */
static int tri_box_class(int x0, int y0, int x1, int y1, float area, float ax, float ay,
                         float bx, float by, float cx, float cy)
{
    float px[4], py[4];
    int i, n0 = 0, n1 = 0, n2 = 0;

    px[0] = (float)x0 + 0.5f;
    py[0] = (float)y0 + 0.5f;
    px[1] = (float)x1 + 0.5f;
    py[1] = (float)y0 + 0.5f;
    px[2] = (float)x0 + 0.5f;
    py[2] = (float)y1 + 0.5f;
    px[3] = (float)x1 + 0.5f;
    py[3] = (float)y1 + 0.5f;
    for (i = 0; i < 4; i++) {
        float w0 = edge(bx, by, cx, cy, px[i], py[i]);
        float w1 = edge(cx, cy, ax, ay, px[i], py[i]);
        float w2 = edge(ax, ay, bx, by, px[i], py[i]);
        if (area > 0) {
            if (w0 < 0)
                n0++;
            if (w1 < 0)
                n1++;
            if (w2 < 0)
                n2++;
        } else {
            if (w0 > 0)
                n0++;
            if (w1 > 0)
                n1++;
            if (w2 > 0)
                n2++;
        }
    }
    if (n0 == 4 || n1 == 4 || n2 == 4)
        return BOX_OUT;
    if (n0 == 0 && n1 == 0 && n2 == 0)
        return BOX_IN;
    return BOX_MIX;
}

static int finite_xy(float x, float y)
{
    return x == x && y == y && x > -1.0e6f && x < 1.0e6f && y > -1.0e6f && y < 1.0e6f;
}

static int clip_to_screen(const GirVert *v, float *sx, float *sy)
{
    float w = v->w;
    float x, y;
    if (w <= 0.0001f)
        return 0;
    x = v->x / w;
    y = v->y / w;
    *sx = x * g_sx + g_tx;
    *sy = y * g_sy + g_ty;
    return finite_xy(*sx, *sy);
}

static uint16_t pack_depth(float ndc_z)
{
    int d = (int)((ndc_z + 1.f) * 32767.5f + 0.5f);
    if (d < 0)
        d = 0;
    if (d > 65535)
        d = 65535;
    return (uint16_t)d;
}

static void fill_tri_box(int minx, int miny, int maxx, int maxy, int all_in, float area,
                         float x0, float y0, float x1, float y1, float x2, float y2,
                         float z0, float z1, float z2, float iw0, float iw1, float iw2,
                         const GirVert *v0, const GirVert *v1, const GirVert *v2,
                         int tex_slot, int keep_al, int backdrop)
{
    int x, y, x0s, x1s;

    for (y = miny; y <= maxy; y++) {
        if (all_in) {
            x0s = minx;
            x1s = maxx;
        } else {
            /* Convex span: first/last inside pixel-center, then stamp that run. */
            for (x0s = minx; x0s <= maxx; x0s++) {
                float px = (float)x0s + 0.5f, py = (float)y + 0.5f;
                if (tri_pix_in(edge(x1, y1, x2, y2, px, py), edge(x2, y2, x0, y0, px, py),
                               edge(x0, y0, x1, y1, px, py), area))
                    break;
            }
            if (x0s > maxx)
                continue;
            for (x1s = maxx; x1s > x0s; x1s--) {
                float px = (float)x1s + 0.5f, py = (float)y + 0.5f;
                if (tri_pix_in(edge(x1, y1, x2, y2, px, py), edge(x2, y2, x0, y0, px, py),
                               edge(x0, y0, x1, y1, px, py), area))
                    break;
            }
        }
        for (x = x0s; x <= x1s; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float w0 = edge(x1, y1, x2, y2, px, py);
            float w1 = edge(x2, y2, x0, y0, px, py);
            float w2 = edge(x0, y0, x1, y1, px, py);
            float a = w0 / area, b = w1 / area, c = w2 / area;
            uint8_t r, g, bl, al;
            uint16_t z;
            /* Same test as put_px. Skip tex/shade on occluded pixels. */
            z = backdrop ? (uint16_t)0xffff : pack_depth(a * z0 + b * z1 + c * z2);
            if (z > g_zb[y][x])
                continue;
            r = (uint8_t)(a * v0->r + b * v1->r + c * v2->r);
            g = (uint8_t)(a * v0->g + b * v1->g + c * v2->g);
            bl = (uint8_t)(a * v0->b + b * v1->b + c * v2->b);
            al = (uint8_t)(a * v0->a + b * v1->a + c * v2->a);
            if (tex_slot >= 0) {
                uint8_t sr = r, sg = g, sb = bl, sa = al;
                float ia = a * iw0, ib = b * iw1, ic = c * iw2;
                float inv = ia + ib + ic;
                float ss, tt;
                /* Perspective-correct ST. Identity-MVP (w=1) matches affine. */
                if (inv > 1.0e-12f) {
                    ss = (ia * v0->s + ib * v1->s + ic * v2->s) / inv;
                    tt = (ia * v0->t + ib * v1->t + ic * v2->t) / inv;
                } else {
                    ss = a * v0->s + b * v1->s + c * v2->s;
                    tt = a * v0->t + b * v1->t + c * v2->t;
                }
                if (g1_tex_sample_slot(tex_slot, ss, tt, &sr, &sg, &sb, &sa)) {
                    /* G_CC_MODULATERGB: texel * Vtx.cn. cn=0 keeps albedo
                     * (G1 greyscale / SETTEX checkers use zeroed verts).
                     * oliveguard / Cheadjim keep texel off no_mtx. Door
                     * 685-688/706 modulate so the leaf is brown metal. */
                    if (g_shade_mod && (r | g | bl) && !keep_al) {
                        r = (uint8_t)(((unsigned)sr * (unsigned)r) / 255u);
                        g = (uint8_t)(((unsigned)sg * (unsigned)g) / 255u);
                        bl = (uint8_t)(((unsigned)sb * (unsigned)bl) / 255u);
                        al = sa;
                    } else {
                        r = sr;
                        g = sg;
                        bl = sb;
                        al = sa;
                    }
                } else {
                    apply_untextured_grey(&r, &g, &bl, &al);
                }
                /* Sample miss keeps vertex shade (grey), never forced black. */
            } else {
                apply_untextured_grey(&r, &g, &bl, &al);
            }
            /* Alpha 0 is a punch-through miss / portal — do not stamp black. */
            if (al == 0)
                continue;
            g_zb[y][x] = z;
            g_fb[y][x][0] = r;
            g_fb[y][x][1] = g;
            g_fb[y][x][2] = bl;
            g_fb[y][x][3] = al;
        }
    }
}

static void draw_tri_raw(const GirVert *v0, const GirVert *v1, const GirVert *v2, int tex_slot)
{
    float x0, y0, x1, y1, x2, y2, area;
    float z0, z1, z2, iw0, iw1, iw2;
    int minx, maxx, miny, maxy, backdrop, keep_al;
    int bx, by, bx1, by1, sx, sy, sx1, sy1, cls2, cls;

    if (!clip_to_screen(v0, &x0, &y0) || !clip_to_screen(v1, &x1, &y1) ||
        !clip_to_screen(v2, &x2, &y2))
        return;
    area = edge(x0, y0, x1, y1, x2, y2);
    if (area > -0.5f && area < 0.5f)
        return;

    /* ndc z = clip.z/clip.w is linear in screen space. */
    z0 = v0->z / v0->w;
    z1 = v1->z / v1->w;
    z2 = v2->z / v2->w;
    iw0 = 1.f / v0->w;
    iw1 = 1.f / v1->w;
    iw2 = 1.f / v2->w;
    /* Identity-MVP (G1 synthetic, leftover SETTEX quad) is clip z=0,w=1.
     * Mid-frustum depth let that slab punch over distant walls and hide
     * props behind the hash triangle. Park it at far so 3D wins. */
    backdrop = (v0->z > -1.0e-3f && v0->z < 1.0e-3f && v0->w > 0.99f &&
                v0->w < 1.01f && v1->z > -1.0e-3f && v1->z < 1.0e-3f &&
                v1->w > 0.99f && v1->w < 1.01f && v2->z > -1.0e-3f &&
                v2->z < 1.0e-3f && v2->w > 0.99f && v2->w < 1.01f);

    minx = (int)x0;
    if ((int)x1 < minx)
        minx = (int)x1;
    if ((int)x2 < minx)
        minx = (int)x2;
    maxx = (int)x0;
    if ((int)x1 > maxx)
        maxx = (int)x1;
    if ((int)x2 > maxx)
        maxx = (int)x2;
    miny = (int)y0;
    if ((int)y1 < miny)
        miny = (int)y1;
    if ((int)y2 < miny)
        miny = (int)y2;
    maxy = (int)y0;
    if ((int)y1 > maxy)
        maxy = (int)y1;
    if ((int)y2 > maxy)
        maxy = (int)y2;
    if (minx < 0)
        minx = 0;
    if (miny < 0)
        miny = 0;
    if (maxx >= G1_FB_W)
        maxx = G1_FB_W - 1;
    if (maxy >= G1_FB_H)
        maxy = G1_FB_H - 1;

    /* Slot albedo is per-triangle; bbox x/y are already clipped. */
    keep_al = (tex_slot >= 0) ? g1_tex_slot_keep_albedo(tex_slot) : 0;

    for (by = miny; by <= maxy; by += TRI_BLOCK2) {
        by1 = by + TRI_BLOCK2 - 1;
        if (by1 > maxy)
            by1 = maxy;
        for (bx = minx; bx <= maxx; bx += TRI_BLOCK2) {
            bx1 = bx + TRI_BLOCK2 - 1;
            if (bx1 > maxx)
                bx1 = maxx;
            cls2 = tri_box_class(bx, by, bx1, by1, area, x0, y0, x1, y1, x2, y2);
            if (cls2 == BOX_OUT)
                continue;
            if (cls2 == BOX_IN) {
                fill_tri_box(bx, by, bx1, by1, 1, area, x0, y0, x1, y1, x2, y2, z0, z1, z2,
                             iw0, iw1, iw2, v0, v1, v2, tex_slot, keep_al, backdrop);
                continue;
            }
            for (sy = by; sy <= by1; sy += TRI_BLOCK) {
                sy1 = sy + TRI_BLOCK - 1;
                if (sy1 > by1)
                    sy1 = by1;
                for (sx = bx; sx <= bx1; sx += TRI_BLOCK) {
                    sx1 = sx + TRI_BLOCK - 1;
                    if (sx1 > bx1)
                        sx1 = bx1;
                    cls = tri_box_class(sx, sy, sx1, sy1, area, x0, y0, x1, y1, x2, y2);
                    if (cls == BOX_OUT)
                        continue;
                    fill_tri_box(sx, sy, sx1, sy1, cls == BOX_IN, area, x0, y0, x1, y1, x2,
                                 y2, z0, z1, z2, iw0, iw1, iw2, v0, v1, v2, tex_slot,
                                 keep_al, backdrop);
                }
            }
        }
    }
}

#define W_EPS 1.0f
#define CLIP_MAX 16

static GirVert lerp_vert(const GirVert *a, const GirVert *b, float t)
{
    GirVert o;
    if (t < 0.f)
        t = 0.f;
    if (t > 1.f)
        t = 1.f;
    o.x = a->x + t * (b->x - a->x);
    o.y = a->y + t * (b->y - a->y);
    o.z = a->z + t * (b->z - a->z);
    o.w = a->w + t * (b->w - a->w);
    o.s = a->s + t * (b->s - a->s);
    o.t = a->t + t * (b->t - a->t);
    o.r = (uint8_t)(a->r + t * ((float)b->r - (float)a->r));
    o.g = (uint8_t)(a->g + t * ((float)b->g - (float)a->g));
    o.b = (uint8_t)(a->b + t * ((float)b->b - (float)a->b));
    o.a = (uint8_t)(a->a + t * ((float)b->a - (float)a->a));
    return o;
}

static float plane_w(const GirVert *v) { return v->w - W_EPS; }
static float plane_xp(const GirVert *v) { return v->w - v->x; }
static float plane_xn(const GirVert *v) { return v->w + v->x; }
static float plane_yp(const GirVert *v) { return v->w - v->y; }
static float plane_yn(const GirVert *v) { return v->w + v->y; }
static float plane_zp(const GirVert *v) { return v->w - v->z; }
static float plane_zn(const GirVert *v) { return v->w + v->z; }

typedef float (*ClipPlane)(const GirVert *);

/* Sutherland–Hodgman: keep f(v) >= 0. */
static int clip_poly(GirVert *poly, int n, ClipPlane f)
{
    GirVert tmp[CLIP_MAX];
    int nout = 0, i;

    if (n < 3)
        return 0;
    for (i = 0; i < n; i++) {
        const GirVert *a = &poly[i];
        const GirVert *b = &poly[(i + 1) % n];
        float fa = f(a), fb = f(b);
        int ia = fa >= 0.f, ib = fb >= 0.f;
        if (ia) {
            if (nout < CLIP_MAX)
                tmp[nout++] = *a;
            if (!ib && nout < CLIP_MAX) {
                float den = fa - fb;
                float t = (den > 1.0e-8f || den < -1.0e-8f) ? (fa / den) : 0.5f;
                tmp[nout++] = lerp_vert(a, b, t);
            }
        } else if (ib && nout < CLIP_MAX) {
            float den = fa - fb;
            float t = (den > 1.0e-8f || den < -1.0e-8f) ? (fa / den) : 0.5f;
            tmp[nout++] = lerp_vert(a, b, t);
        }
    }
    memcpy(poly, tmp, (size_t)nout * sizeof(GirVert));
    return nout;
}

/*
 * Clip to the view frustum (w, ±x, ±y, ±z). w/±x/±y stopped a near-floor
 * sliver (x/w exploded). A door/portal closer than the projection near
 * plane is still in-frustum in x/y and projected to a center-covering
 * rectangle; |z|<=w rejects those (NDC z < -1). W_EPS stays 1 so
 * identity-MVP synthetics (w=1, z=0) still raster.
 */
static void draw_tri(const GirVert *v0, const GirVert *v1, const GirVert *v2, int tex_slot)
{
    GirVert poly[CLIP_MAX];
    int n, i;

    poly[0] = *v0;
    poly[1] = *v1;
    poly[2] = *v2;
    n = 3;
    n = clip_poly(poly, n, plane_w);
    n = clip_poly(poly, n, plane_xp);
    n = clip_poly(poly, n, plane_xn);
    n = clip_poly(poly, n, plane_yp);
    n = clip_poly(poly, n, plane_yn);
    n = clip_poly(poly, n, plane_zp);
    n = clip_poly(poly, n, plane_zn);
    if (n < 3)
        return;
    for (i = 1; i + 1 < n; i++)
        draw_tri_raw(&poly[0], &poly[i], &poly[i + 1], tex_slot);
}

void sw_raster_list(const GirList *list)
{
    uint32_t i;
    if (!list)
        return;
    for (i = 0; i < list->ncmds; i++) {
        const GirCmd *c = &list->cmds[i];
        if (c->op == GIR_SET_VIEWPORT) {
            g_sx = c->u.viewport.scale_x;
            g_sy = c->u.viewport.scale_y;
            g_tx = c->u.viewport.trans_x;
            g_ty = c->u.viewport.trans_y;
        } else if (c->op == GIR_DRAW_RECT) {
            fill_rect(c->u.rect.ulx, c->u.rect.uly, c->u.rect.lrx, c->u.rect.lry,
                      c->u.rect.r, c->u.rect.g, c->u.rect.b, c->u.rect.a);
        } else if (c->op == GIR_DRAW_TRIS) {
            draw_tri(&c->u.tri.v[0], &c->u.tri.v[1], &c->u.tri.v[2], c->u.tri.tex_slot);
        }
    }
}
