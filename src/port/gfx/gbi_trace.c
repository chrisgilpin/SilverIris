#include "gfx/gbi_trace.h"
#include "gfx/gbi_interp.h"

#include "fs/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define G0_MAX_DEPTH 32

static G0Record g_last;
static int g_have;
static unsigned g_task_seq;
static char g_dump_dir[512];

static void wr_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t gfx_w0(const Gfx *g) { return (uint32_t)g->words.w0; }
static uint32_t gfx_w1(const Gfx *g) { return (uint32_t)g->words.w1; }
static uint8_t gfx_cmd(const Gfx *g) { return (uint8_t)(gfx_w0(g) >> 24); }

void g0_mtx_f2l(const float mf[4][4], Mtx *m)
{
    int i, j;
    int *ai = (int *)&m->m[0][0];
    int *af = (int *)&m->m[2][0];
    memset(m, 0, sizeof *m);
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 2; j++) {
            int e1 = FTOFIX32(mf[i][j * 2]);
            int e2 = FTOFIX32(mf[i][j * 2 + 1]);
            *ai++ = (e1 & 0xffff0000) | ((e2 >> 16) & 0xffff);
            *af++ = ((e1 << 16) & 0xffff0000) | (e2 & 0xffff);
        }
    }
}

void g0_mtx_l2f(const Mtx *m, float mf[4][4])
{
    int i, j;
    unsigned int *ai = (unsigned int *)&m->m[0][0];
    unsigned int *af = (unsigned int *)&m->m[2][0];
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 2; j++) {
            unsigned int e1, e2;
            int q1, q2;
            e1 = (*ai & 0xffff0000u) | ((*af >> 16) & 0xffffu);
            e2 = ((*(ai++) << 16) & 0xffff0000u) | (*(af++) & 0xffffu);
            q1 = *(int *)&e1;
            q2 = *(int *)&e2;
            mf[i][j * 2] = FIX32TOF(q1);
            mf[i][j * 2 + 1] = FIX32TOF(q2);
        }
    }
}

static void f32_to_bits(const float mf[4][4], uint32_t out[16])
{
    int i;
    for (i = 0; i < 16; i++) {
        float f = mf[i / 4][i % 4];
        memcpy(&out[i], &f, 4);
    }
}

void g0_set_dump_dir(const char *dir)
{
    if (!dir) {
        g_dump_dir[0] = 0;
        return;
    }
    strncpy(g_dump_dir, dir, sizeof g_dump_dir - 1);
    g_dump_dir[sizeof g_dump_dir - 1] = 0;
}

void g0_reset(void)
{
    memset(&g_last, 0, sizeof g_last);
    g_last.n_seg = G0_NSEG;
    g_have = 0;
}

const G0Record *g0_last(void) { return g_have ? &g_last : NULL; }

void g0_gfx_words_sha256(const G0Record *rec, uint8_t out[32])
{
    uint8_t *buf;
    size_t n, i;
    if (!rec) {
        memset(out, 0, 32);
        return;
    }
    n = (size_t)rec->n_gfx * 8u;
    buf = (uint8_t *)malloc(n ? n : 1);
    if (!buf) {
        memset(out, 0, 32);
        return;
    }
    for (i = 0; i < rec->n_gfx; i++) {
        wr_u32le(buf + i * 8, rec->gfx_w0[i]);
        wr_u32le(buf + i * 8 + 4, rec->gfx_w1[i]);
    }
    silveriris_sha256(buf, n, out);
    free(buf);
}

static int g0_serialize(const G0Record *rec, uint8_t **out, size_t *out_len)
{
    size_t n = 4 + 8 + (16 + 16 + 16) * 4 + (size_t)rec->n_gfx * 8;
    uint8_t *buf = (uint8_t *)malloc(n);
    uint8_t *p;
    uint32_t i;
    if (!buf)
        return -1;
    p = buf;
    memcpy(p, G0_MAGIC, 4);
    p += 4;
    wr_u32le(p, rec->n_gfx);
    p += 4;
    wr_u32le(p, rec->n_seg);
    p += 4;
    for (i = 0; i < 16; i++, p += 4)
        wr_u32le(p, rec->segment[i]);
    for (i = 0; i < 16; i++, p += 4)
        wr_u32le(p, rec->mtx_modelview[i]);
    for (i = 0; i < 16; i++, p += 4)
        wr_u32le(p, rec->mtx_projection[i]);
    for (i = 0; i < rec->n_gfx; i++) {
        wr_u32le(p, rec->gfx_w0[i]);
        p += 4;
        wr_u32le(p, rec->gfx_w1[i]);
        p += 4;
    }
    *out = buf;
    *out_len = n;
    return 0;
}

int g0_write_record(const G0Record *rec, const char *path)
{
    FILE *f;
    uint8_t *buf = NULL;
    size_t n = 0;
    int rc;
    if (!rec || !path)
        return -1;
    if (g0_serialize(rec, &buf, &n) != 0)
        return -1;
    f = fopen(path, "wb");
    if (!f) {
        free(buf);
        return -1;
    }
    rc = (fwrite(buf, 1, n, f) == n) ? 0 : -1;
    fclose(f);
    free(buf);
    return rc;
}

static const void *resolve_addr(const G0Record *rec, uint32_t addr32, uintptr_t full)
{
    uint32_t seg = (addr32 >> 24) & 0xF;
    uint32_t off = addr32 & 0x00FFFFFFu;
    if (rec->segment[seg] != 0)
        return (const void *)(uintptr_t)(rec->segment[seg] + off);
    (void)addr32;
    return (const void *)full;
}

static void load_mtx(G0Record *rec, uint32_t w0, uintptr_t mtx_full)
{
    uint32_t params = (w0 >> 16) & 0xFF;
    const Mtx *src = (const Mtx *)resolve_addr(rec, (uint32_t)mtx_full, mtx_full);
    float mf[4][4];
    if (!src)
        return;
    g0_mtx_l2f(src, mf);
    if (params & G_MTX_PROJECTION) {
        f32_to_bits(mf, rec->mtx_projection);
        rec->have_projection = 1;
    } else {
        f32_to_bits(mf, rec->mtx_modelview);
        rec->have_modelview = 1;
    }
}

static void walk_dl(G0Record *rec, const Gfx *start, uint32_t n_hint)
{
    const Gfx *stack[G0_MAX_DEPTH];
    int sp = 0;
    const Gfx *ip = start;
    uint32_t steps = 0;
    uint32_t limit = n_hint ? n_hint * 4u + 64u : 4096u;

    while (ip && steps < limit) {
        uint8_t cmd = gfx_cmd(ip);
        uint32_t w0 = gfx_w0(ip);
        uint32_t w1 = gfx_w1(ip);
        steps++;

        if (cmd == (uint8_t)G_MOVEWORD && (w0 & 0xFF) == G_MW_SEGMENT) {
            uint32_t off = (w0 >> 8) & 0xFFFF;
            uint32_t seg = off / 4;
            if (seg < G0_NSEG)
                rec->segment[seg] = w1;
        } else if (cmd == (uint8_t)G_MTX) {
            load_mtx(rec, w0, ip->words.w1);
        } else if (cmd == (uint8_t)G_DL) {
            uint32_t push = (w0 >> 16) & 0xFF;
            const Gfx *child = (const Gfx *)resolve_addr(rec, w1, ip->words.w1);
            if (push == G_DL_NOPUSH) {
                ip = child;
                continue;
            }
            if (sp < G0_MAX_DEPTH && child) {
                stack[sp++] = ip + 1;
                ip = child;
                continue;
            }
        } else if (cmd == (uint8_t)G_ENDDL) {
            if (sp == 0)
                break;
            ip = stack[--sp];
            continue;
        }
        ip++;
        if (sp == 0 && n_hint && (ip < start || ip >= start + n_hint))
            break;
    }
}

int g0_capture_task(OSTask *task)
{
    const Gfx *dl;
    uint32_t n, i;
    const char *env;

    g0_reset();
    if (!task || task->t.type != M_GFXTASK || !task->t.data_ptr)
        return -1;

    dl = (const Gfx *)task->t.data_ptr;
    n = task->t.data_size / (uint32_t)sizeof(Gfx);
    if (n > G0_MAX_GFX)
        n = G0_MAX_GFX;

    g_last.n_gfx = n;
    g_last.n_seg = G0_NSEG;
    for (i = 0; i < n; i++) {
        uintptr_t full = dl[i].words.w1;
        g_last.gfx_w0[i] = gfx_w0(dl + i);
        /* LP64 host pointers do not fit in the N64 u32 w1; zero them so the
         * gfx-word hash is ASLR-stable. Segmented 32-bit addresses pass. */
        g_last.gfx_w1[i] = (full > 0xFFFFFFFFu) ? 0u : (uint32_t)full;
        g_last.hist[gfx_cmd(dl + i)]++;
    }
    walk_dl(&g_last, dl, n);
    g_have = 1;

    env = getenv("SILVERIRIS_G0_DIR");
    if (env && env[0] && !g_dump_dir[0])
        g0_set_dump_dir(env);
    if (g_dump_dir[0]) {
        char path[768];
        snprintf(path, sizeof path, "%s/g0_task_%04u.g0", g_dump_dir, g_task_seq);
        g0_write_record(&g_last, path);
    }
    g_task_seq++;
    return 0;
}

void osSpTaskLoad(OSTask *task)
{
    g0_capture_task(task);
    g1_interpret_task(task);
}

void osSpTaskStartGo(OSTask *task)
{
    (void)task;
}
