#include "gfx/sw_raster.h"

#include "fs/sha256.h"

#include <string.h>

static uint8_t g_fb[G1_FB_H][G1_FB_W][4];
static float g_sx = 160.f, g_sy = 120.f, g_tx = 160.f, g_ty = 120.f;

void sw_raster_clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    int y, x;
    for (y = 0; y < G1_FB_H; y++) {
        for (x = 0; x < G1_FB_W; x++) {
            g_fb[y][x][0] = r;
            g_fb[y][x][1] = g;
            g_fb[y][x][2] = b;
            g_fb[y][x][3] = a;
        }
    }
}

const uint8_t *sw_fb_rgba(void) { return &g_fb[0][0][0]; }

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

static void put_px(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if ((unsigned)x >= G1_FB_W || (unsigned)y >= G1_FB_H)
        return;
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
            put_px(x, y, r, g, b, a);
}

static float edge(float ax, float ay, float bx, float by, float cx, float cy)
{
    return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
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
    return 1;
}

static void draw_tri(const GirVert *v0, const GirVert *v1, const GirVert *v2)
{
    float x0, y0, x1, y1, x2, y2, area;
    int minx, maxx, miny, maxy, x, y;

    if (!clip_to_screen(v0, &x0, &y0) || !clip_to_screen(v1, &x1, &y1) ||
        !clip_to_screen(v2, &x2, &y2))
        return;
    area = edge(x0, y0, x1, y1, x2, y2);
    if (area > -0.5f && area < 0.5f)
        return;

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

    for (y = miny; y <= maxy; y++) {
        for (x = minx; x <= maxx; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float w0 = edge(x1, y1, x2, y2, px, py);
            float w1 = edge(x2, y2, x0, y0, px, py);
            float w2 = edge(x0, y0, x1, y1, px, py);
            int inside;
            if (area > 0)
                inside = (w0 >= 0 && w1 >= 0 && w2 >= 0);
            else
                inside = (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (inside) {
                float a = w0 / area, b = w1 / area, c = w2 / area;
                uint8_t r = (uint8_t)(a * v0->r + b * v1->r + c * v2->r);
                uint8_t g = (uint8_t)(a * v0->g + b * v1->g + c * v2->g);
                uint8_t bl = (uint8_t)(a * v0->b + b * v1->b + c * v2->b);
                uint8_t al = (uint8_t)(a * v0->a + b * v1->a + c * v2->a);
                put_px(x, y, r, g, bl, al);
            }
        }
    }
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
            draw_tri(&c->u.tri.v[0], &c->u.tri.v[1], &c->u.tri.v[2]);
        }
    }
}
