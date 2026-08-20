#include "gfx/tex_bank.h"

#include <string.h>

#include "fs/inflate1172.h"
#include "gfx/tmem.h"

/* image.h TEXFORMAT_* channel tables (decomp image.c). */
static const int k_nchan[] = {4, 3, 3, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1};
static const int k_has1a[] = {0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
static const int k_chsize[] = {0x100, 0x20, 0x100, 0x20, 0x100, 0x10, 8, 0x100,
                               0x10, 0x100, 0x10, 0x100, 0x10};
typedef struct {
    const uint8_t *p;
    const uint8_t *end;
    uint32_t table;
    int bits;
} Bits;

static void bits_init(Bits *b, const uint8_t *p, size_t n)
{
    b->p = p;
    b->end = p + n;
    b->table = 0;
    b->bits = 0;
}

static int bits_need(Bits *b, int n)
{
    if (n <= 0 || n > 24)
        return -1;
    while (b->bits < n) {
        if (b->p >= b->end)
            return -1;
        b->table = ((uint32_t)*b->p) | (b->table << 8);
        b->p++;
        b->bits += 8;
    }
    return 0;
}

static int bits_read(Bits *b, int n, uint32_t *out)
{
    if (bits_need(b, n) != 0)
        return -1;
    b->bits -= n;
    *out = (b->table >> b->bits) & ((1u << n) - 1u);
    return 0;
}

static size_t tight_need(uint8_t fmt, unsigned w, unsigned h)
{
    unsigned n = w * h;
    if (fmt == G1_TEX_I4 || fmt == G1_TEX_CI4 || fmt == G1_TEX_IA4 || fmt == G1_TEX_IA16_CI4)
        return (n + 1u) / 2u;
    if (fmt == G1_TEX_I8 || fmt == G1_TEX_CI8 || fmt == G1_TEX_IA8 || fmt == G1_TEX_IA16_CI8)
        return n;
    if (fmt == G1_TEX_RGBA16 || fmt == G1_TEX_RGB15 || fmt == G1_TEX_IA16)
        return n * 2u;
    if (fmt == G1_TEX_RGB24)
        return n * 3u;
    if (fmt == G1_TEX_RGBA32)
        return n * 4u;
    return 0;
}

static int fmt_ok(uint8_t fmt)
{
    return fmt <= G1_TEX_IA16_CI4;
}

static int remain(const Bits *b)
{
    return (int)(b->end - b->p);
}

/* GE decompressdata: 0x11 0x72 then raw deflate (same puff as rooms). */
static int inflate_payload(const uint8_t *src, size_t n, uint8_t *dst, size_t cap,
                           size_t *out)
{
    if (!src || n < 3)
        return -1;
    if (src[0] == 0x11 && src[1] == 0x72)
        return port_inflate1172(src, n, dst, cap, out) == PORT_INFLATE1172_OK ? 0 : -1;
    return -1;
}

static int decode_zlib(Bits *b, G1TexBankOut *out)
{
    uint32_t v;
    unsigned i, ncol, w, h;
    uint8_t fmt;
    uint8_t scratch[4096];
    size_t ninf = 0, need;
    const uint8_t *pay;
    size_t payn;

    if (bits_read(b, 8, &v) != 0)
        return -1;
    fmt = (uint8_t)v;
    if (bits_read(b, 8, &v) != 0)
        return -1;
    ncol = (unsigned)v + 1u;
    if (ncol > 256)
        return -1;
    for (i = 0; i < ncol; i++) {
        if (bits_read(b, 16, &v) != 0)
            return -1;
        out->tlut[i] = (uint16_t)v;
    }
    out->ntlut = ncol;
    if (bits_read(b, 8, &v) != 0)
        return -1;
    w = (unsigned)v;
    if (bits_read(b, 8, &v) != 0)
        return -1;
    h = (unsigned)v;
    if (!w || !h || w > 64 || h > 64)
        return -1;
    /* Keep IA16-CI ids so TMEM samples the 8.8 palette, not 5551. */
    if (fmt == G1_TEX_IA16_CI8 || fmt == G1_TEX_IA16_CI4)
        out->tlut_ia = 1;
    if (!fmt_ok(fmt) || (fmt != G1_TEX_CI4 && fmt != G1_TEX_CI8 && fmt != G1_TEX_IA16_CI4 &&
                         fmt != G1_TEX_IA16_CI8))
        return -1;
    if (fmt == G1_TEX_IA16_CI8)
        fmt = G1_TEX_CI8;
    if (fmt == G1_TEX_IA16_CI4)
        fmt = G1_TEX_CI4;
    /* Bitstream is byte-aligned after 8/16-bit fields. */
    if (b->bits != 0)
        return -1;
    pay = b->p;
    payn = (size_t)remain(b);
    if (inflate_payload(pay, payn, scratch, sizeof scratch, &ninf) != 0)
        return -1;
    need = tight_need(fmt, w, h);
    if (!need || ninf < need || need > 4096)
        return -1;
    memcpy(out->texels, scratch, need);
    out->fmt = fmt;
    out->w = (uint8_t)w;
    out->h = (uint8_t)h;
    out->ntex = need;
    out->zlib = 1;
    out->method = -1;
    return 0;
}

static int read_uncomp_tight(Bits *b, uint8_t fmt, unsigned w, unsigned h, uint8_t *dst,
                             size_t *ntex)
{
    unsigned x, y, i = 0;
    uint32_t v;

    if (fmt == G1_TEX_I8) {
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                if (bits_read(b, 8, &v) != 0)
                    return -1;
                dst[i++] = (uint8_t)v;
            }
        }
        *ntex = (size_t)w * h;
        return 0;
    }
    if (fmt == G1_TEX_I4) {
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x += 2) {
                if (bits_read(b, 8, &v) != 0)
                    return -1;
                dst[i++] = (uint8_t)v;
            }
        }
        *ntex = ((size_t)w * h + 1u) / 2u;
        return 0;
    }
    if (fmt == G1_TEX_RGBA16 || fmt == G1_TEX_RGB15 || fmt == G1_TEX_IA16) {
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                if (bits_read(b, 16, &v) != 0)
                    return -1;
                dst[i++] = (uint8_t)(v >> 8);
                dst[i++] = (uint8_t)v;
            }
        }
        *ntex = (size_t)w * h * 2u;
        return 0;
    }
    if (fmt == G1_TEX_IA8) {
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                if (bits_read(b, 8, &v) != 0)
                    return -1;
                dst[i++] = (uint8_t)v;
            }
        }
        *ntex = (size_t)w * h;
        return 0;
    }
    if (fmt == G1_TEX_IA4) {
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x += 2) {
                if (bits_read(b, 8, &v) != 0)
                    return -1;
                dst[i++] = (uint8_t)v;
            }
        }
        *ntex = ((size_t)w * h + 1u) / 2u;
        return 0;
    }
    if (fmt == G1_TEX_RGBA32) {
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                int k;
                for (k = 0; k < 4; k++) {
                    if (bits_read(b, 8, &v) != 0)
                        return -1;
                    dst[i++] = (uint8_t)v;
                }
            }
        }
        *ntex = (size_t)w * h * 4u;
        return 0;
    }
    if (fmt == G1_TEX_RGB24) {
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                if (bits_read(b, 24, &v) != 0)
                    return -1;
                dst[i++] = (uint8_t)(v >> 16);
                dst[i++] = (uint8_t)(v >> 8);
                dst[i++] = (uint8_t)v;
            }
        }
        *ntex = (size_t)w * h * 3u;
        return 0;
    }
    return -1;
}

/* Decomp texInflateHuffman — frequencies then canonical tree walk. */
static int inflate_huffman(Bits *b, uint8_t *dst, int niter, int chansize)
{
    uint16_t freq[2048];
    int16_t nodes[2048][2];
    int i, root = 0, done = 0;
    uint32_t v;

    if (chansize <= 0 || chansize > 2048 || niter <= 0 || niter > 4096)
        return -1;
    for (i = 0; i < chansize; i++) {
        if (bits_read(b, 8, &v) != 0)
            return -1;
        freq[i] = (uint16_t)v;
    }
    for (; i < 2048; i++)
        freq[i] = 0;
    for (i = 0; i < 2048; i++) {
        nodes[i][0] = -1;
        nodes[i][1] = -1;
    }
    while (!done) {
        uint16_t minf1 = 9999, minf2 = 9999;
        int mini1 = 0, mini2 = 1, sum, r;

        for (i = 0; i < chansize; i++) {
            if (freq[i] < minf1) {
                if (minf2 < minf1) {
                    minf1 = freq[i];
                    mini1 = i;
                } else {
                    minf2 = freq[i];
                    mini2 = i;
                }
            } else if (freq[i] < minf2) {
                minf2 = freq[i];
                mini2 = i;
            }
        }
        if (minf1 == 9999 || minf2 == 9999) {
            done = 1;
            break;
        }
        sum = (int)freq[mini1] + (int)freq[mini2];
        if (sum == 0)
            sum = 1;
        freq[mini1] = 9999;
        freq[mini2] = 9999;
        if (nodes[mini1][0] < 0 && nodes[mini1][1] < 0) {
            nodes[mini1][0] = (int16_t)(mini1 + 10000);
            root = mini1;
            freq[mini1] = (uint16_t)sum;
            if (nodes[mini2][0] < 0 && nodes[mini2][1] < 0)
                nodes[mini1][1] = (int16_t)(mini2 + 10000);
            else
                nodes[mini1][1] = (int16_t)mini2;
        } else if (nodes[mini2][0] < 0 && nodes[mini2][1] < 0) {
            nodes[mini2][0] = (int16_t)(mini2 + 10000);
            root = mini2;
            freq[mini2] = (uint16_t)sum;
            if (nodes[mini1][0] < 0 && nodes[mini1][1] < 0)
                nodes[mini2][1] = (int16_t)(mini1 + 10000);
            else
                nodes[mini2][1] = (int16_t)mini1;
        } else {
            for (r = 0; r < 2048; r++) {
                if (nodes[r][0] < 0 && nodes[r][1] < 0 && freq[r] >= 9999)
                    break;
            }
            if (r >= 2048)
                return -1;
            root = r;
            freq[r] = (uint16_t)sum;
            nodes[r][0] = (int16_t)mini1;
            nodes[r][1] = (int16_t)mini2;
        }
        minf1 = 9999;
        minf2 = 9999;
        for (i = 0; i < chansize; i++) {
            if (freq[i] < minf1) {
                if (minf1 > minf2) {
                    minf1 = freq[i];
                    mini1 = i;
                } else {
                    minf2 = freq[i];
                    mini2 = i;
                }
            } else if (freq[i] < minf2) {
                minf2 = freq[i];
                mini2 = i;
            }
        }
        if (minf1 == 9999 || minf2 == 9999)
            done = 1;
    }
    for (i = 0; i < niter; i++) {
        int idx = root;
        while (idx < 10000) {
            if (bits_read(b, 1, &v) != 0)
                return -1;
            if (idx < 0 || idx >= 2048)
                return -1;
            idx = nodes[idx][(int)v];
        }
        if (chansize <= 256)
            dst[i] = (uint8_t)(idx - 10000);
        else {
            uint16_t *tmp = (uint16_t *)(void *)dst;
            tmp[i] = (uint16_t)(idx - 10000);
        }
    }
    return 0;
}

static int inflate_rle(Bits *b, uint8_t *dst, int total)
{
    uint32_t v;
    int bt, rl, bs, cost, fudge, done, i;

    if (bits_read(b, 3, &v) != 0)
        return -1;
    bt = (int)v;
    if (bits_read(b, 3, &v) != 0)
        return -1;
    rl = (int)v;
    if (bits_read(b, 4, &v) != 0)
        return -1;
    bs = (int)v;
    if (bs <= 0 || bs > 8 || total <= 0 || total > 4096)
        return -1;
    cost = bt + rl + bs + 1;
    fudge = 0;
    while (cost > 0) {
        cost = cost - bs - 1;
        fudge++;
    }
    done = 0;
    while (done < total) {
        if (bits_read(b, 1, &v) != 0)
            return -1;
        if (v == 0) {
            if (bits_read(b, bs, &v) != 0)
                return -1;
            dst[done++] = (uint8_t)v;
        } else {
            int start, run;
            if (bits_read(b, bt, &v) != 0)
                return -1;
            start = done - (int)v - 1;
            if (bits_read(b, rl, &v) != 0)
                return -1;
            run = (int)v + fudge;
            if (start < 0 || run < 0 || done + run + 1 > total)
                return -1;
            for (i = start; i < start + run; i++)
                dst[done++] = dst[i];
            if (bits_read(b, bs, &v) != 0)
                return -1;
            dst[done++] = (uint8_t)v;
        }
    }
    return 0;
}

static int chans_to_i8(const uint8_t *src, unsigned w, unsigned h, uint8_t *dst)
{
    memcpy(dst, src, (size_t)w * h);
    return (int)((size_t)w * h);
}

static int chans_to_i4(const uint8_t *src, unsigned w, unsigned h, uint8_t *dst)
{
    unsigned x, y, i = 0, pos = 0;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x += 2) {
            uint8_t hi = src[pos];
            uint8_t lo = (x + 1 < w) ? src[pos + 1] : 0;
            dst[i++] = (uint8_t)((hi << 4) | (lo & 0x0f));
            pos += 2;
        }
        if (w & 1)
            pos--;
    }
    return (int)i;
}

static int chans_to_rgba16(const uint8_t *src, unsigned w, unsigned h, uint8_t *dst)
{
    unsigned x, y, i = 0, pos = 0;
    unsigned mult = w * h;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            uint16_t c = (uint16_t)((src[pos] << 11) | (src[pos + mult] << 6) |
                                    (src[pos + mult * 2] << 1) | src[pos + mult * 3]);
            dst[i++] = (uint8_t)(c >> 8);
            dst[i++] = (uint8_t)c;
            pos++;
        }
    }
    return (int)i;
}

static int chans_to_ia8(const uint8_t *src, unsigned w, unsigned h, uint8_t *dst)
{
    unsigned n = w * h, i;
    for (i = 0; i < n; i++)
        dst[i] = (uint8_t)((src[i] << 4) | (src[n + i] & 0x0f));
    return (int)n;
}

static int chans_to_ia16(const uint8_t *src, unsigned w, unsigned h, uint8_t *dst)
{
    unsigned n = w * h, i;
    for (i = 0; i < n; i++) {
        dst[i * 2] = src[i];
        dst[i * 2 + 1] = src[n + i];
    }
    return (int)(n * 2u);
}

static int chans_to_ia4(const uint8_t *src, unsigned w, unsigned h, uint8_t *dst)
{
    unsigned x, y, i = 0, pos = 0, n = w * h;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x += 2) {
            uint8_t a0 = src[pos] & 7;
            uint8_t a1 = (x + 1 < w) ? (src[pos + 1] & 7) : 0;
            uint8_t al0 = src[n + pos] ? 1 : 0;
            uint8_t al1 = (x + 1 < w && src[n + pos + 1]) ? 1 : 0;
            dst[i++] = (uint8_t)((a0 << 5) | (al0 << 4) | (a1 << 1) | al1);
            pos += 2;
        }
        if (w & 1)
            pos--;
    }
    return (int)i;
}

static int chans_to_rgba32(const uint8_t *src, unsigned w, unsigned h, uint8_t *dst)
{
    unsigned n = w * h, i;
    for (i = 0; i < n; i++) {
        dst[i * 4] = src[i];
        dst[i * 4 + 1] = src[n + i];
        dst[i * 4 + 2] = src[2 * n + i];
        dst[i * 4 + 3] = src[3 * n + i];
    }
    return (int)(n * 4u);
}

static int bitsize(int n)
{
    int c = 0;
    n--;
    while (n > 0) {
        n >>= 1;
        c++;
    }
    return c;
}

static int inflate_lookup(Bits *b, unsigned w, unsigned h, uint8_t fmt, uint8_t *dst,
                          size_t *ntex)
{
    uint32_t v;
    unsigned ncol, i, x, y;
    uint16_t lut[256];
    int bpc;
    unsigned o = 0;

    if (bits_read(b, 11, &v) != 0)
        return -1;
    ncol = (unsigned)v;
    if (ncol == 0 || ncol > 256)
        return -1;
    bpc = bitsize((int)ncol);
    if (bpc <= 0)
        bpc = 1;
    for (i = 0; i < ncol; i++) {
        if (fmt == G1_TEX_RGBA16) {
            if (bits_read(b, 16, &v) != 0)
                return -1;
        } else if (fmt == G1_TEX_I8 || fmt == G1_TEX_IA8) {
            if (bits_read(b, 8, &v) != 0)
                return -1;
        } else if (fmt == G1_TEX_I4 || fmt == G1_TEX_IA4) {
            if (bits_read(b, 4, &v) != 0)
                return -1;
        } else if (fmt == G1_TEX_IA16) {
            if (bits_read(b, 16, &v) != 0)
                return -1;
        } else
            return -1;
        lut[i] = (uint16_t)v;
    }
    if (fmt == G1_TEX_I8 || fmt == G1_TEX_IA8) {
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                if (bits_read(b, bpc, &v) != 0)
                    return -1;
                if (v >= ncol)
                    v = 0;
                dst[o++] = (uint8_t)lut[v];
            }
        *ntex = o;
        return 0;
    }
    if (fmt == G1_TEX_I4 || fmt == G1_TEX_IA4) {
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x += 2) {
                uint8_t pix;
                if (bits_read(b, bpc, &v) != 0)
                    return -1;
                if (v >= ncol)
                    v = 0;
                pix = (uint8_t)((lut[v] & 0x0f) << 4);
                if (x + 1 < w) {
                    if (bits_read(b, bpc, &v) != 0)
                        return -1;
                    if (v >= ncol)
                        v = 0;
                    pix |= (uint8_t)(lut[v] & 0x0f);
                }
                dst[o++] = pix;
            }
        }
        *ntex = o;
        return 0;
    }
    if (fmt == G1_TEX_RGBA16 || fmt == G1_TEX_IA16) {
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                if (bits_read(b, bpc, &v) != 0)
                    return -1;
                if (v >= ncol)
                    v = 0;
                dst[o++] = (uint8_t)(lut[v] >> 8);
                dst[o++] = (uint8_t)lut[v];
            }
        *ntex = o;
        return 0;
    }
    return -1;
}

static int decode_rare(Bits *b, G1TexBankOut *out)
{
    uint32_t v;
    uint8_t fmt, method;
    unsigned w, h;
    uint8_t scratch[0x2000];
    int nchan, chsz, niter, nbytes;

    if (bits_read(b, 4, &v) != 0)
        return -1;
    fmt = (uint8_t)v;
    if (bits_read(b, 8, &v) != 0)
        return -1;
    w = (unsigned)v;
    if (bits_read(b, 8, &v) != 0)
        return -1;
    h = (unsigned)v;
    if (bits_read(b, 4, &v) != 0)
        return -1;
    method = (uint8_t)v;
    if (!w || !h || w > 64 || h > 64 || !fmt_ok(fmt))
        return -1;
    if ((unsigned)fmt >= sizeof k_nchan / sizeof k_nchan[0])
        return -1;
    nchan = k_nchan[fmt];
    chsz = k_chsize[fmt];
    niter = nchan * (int)w * (int)h;
    if (niter <= 0 || niter > 0x2000)
        return -1;

    out->zlib = 0;
    out->method = (int8_t)method;
    out->fmt = fmt;
    out->w = (uint8_t)w;
    out->h = (uint8_t)h;
    out->ntlut = 0;

    switch (method) {
    case G1_TEXCOMP_UNCOMPRESSED0:
    case G1_TEXCOMP_UNCOMPRESSED1:
        return read_uncomp_tight(b, fmt, w, h, out->texels, &out->ntex);
    case G1_TEXCOMP_HUFFMAN:
    case G1_TEXCOMP_HUFFMANPERHCHANNEL:
        if (method == G1_TEXCOMP_HUFFMANPERHCHANNEL) {
            int c;
            for (c = 0; c < nchan; c++) {
                if (inflate_huffman(b, scratch + (int)w * (int)h * c, (int)w * (int)h, chsz) !=
                    0)
                    return -1;
            }
        } else {
            if (inflate_huffman(b, scratch, niter, chsz) != 0)
                return -1;
        }
        if (k_has1a[fmt]) {
            int i;
            for (i = 0; i < (int)w * (int)h; i++) {
                if (bits_read(b, 1, &v) != 0)
                    return -1;
                scratch[(int)w * (int)h * 3 + i] = (uint8_t)v;
            }
        }
        if (fmt == G1_TEX_I8)
            nbytes = chans_to_i8(scratch, w, h, out->texels);
        else if (fmt == G1_TEX_I4)
            nbytes = chans_to_i4(scratch, w, h, out->texels);
        else if (fmt == G1_TEX_RGBA16)
            nbytes = chans_to_rgba16(scratch, w, h, out->texels);
        else if (fmt == G1_TEX_IA8)
            nbytes = chans_to_ia8(scratch, w, h, out->texels);
        else if (fmt == G1_TEX_IA16)
            nbytes = chans_to_ia16(scratch, w, h, out->texels);
        else if (fmt == G1_TEX_IA4)
            nbytes = chans_to_ia4(scratch, w, h, out->texels);
        else if (fmt == G1_TEX_RGBA32)
            nbytes = chans_to_rgba32(scratch, w, h, out->texels);
        else
            return -1;
        if (nbytes <= 0)
            return -1;
        out->ntex = (size_t)nbytes;
        return 0;
    case G1_TEXCOMP_RLE:
        if (inflate_rle(b, scratch, niter) != 0)
            return -1;
        if (k_has1a[fmt]) {
            int i;
            for (i = 0; i < (int)w * (int)h; i++) {
                if (bits_read(b, 1, &v) != 0)
                    return -1;
                scratch[(int)w * (int)h * 3 + i] = (uint8_t)v;
            }
        }
        if (fmt == G1_TEX_I8)
            nbytes = chans_to_i8(scratch, w, h, out->texels);
        else if (fmt == G1_TEX_I4)
            nbytes = chans_to_i4(scratch, w, h, out->texels);
        else if (fmt == G1_TEX_RGBA16)
            nbytes = chans_to_rgba16(scratch, w, h, out->texels);
        else if (fmt == G1_TEX_IA8)
            nbytes = chans_to_ia8(scratch, w, h, out->texels);
        else if (fmt == G1_TEX_IA16)
            nbytes = chans_to_ia16(scratch, w, h, out->texels);
        else if (fmt == G1_TEX_IA4)
            nbytes = chans_to_ia4(scratch, w, h, out->texels);
        else if (fmt == G1_TEX_RGBA32)
            nbytes = chans_to_rgba32(scratch, w, h, out->texels);
        else
            return -1;
        if (nbytes <= 0)
            return -1;
        out->ntex = (size_t)nbytes;
        return 0;
    case G1_TEXCOMP_LOOKUP:
        return inflate_lookup(b, w, h, fmt, out->texels, &out->ntex);
    default:
        /* Blur / lookup-huffman exist on retail; not needed for synthetic proof. */
        return -1;
    }
}

int g1_tex_bank_decode(const uint8_t *src, size_t n, G1TexBankOut *out)
{
    Bits b;
    uint8_t flags;
    int zlib;

    if (!src || !out || n < 2)
        return G1_TEX_BANK_ERR;
    memset(out, 0, sizeof *out);
    flags = src[0];
    zlib = (flags >> 6) & 1;
    bits_init(&b, src + 1, n - 1);
    if (zlib) {
        if (decode_zlib(&b, out) != 0)
            return G1_TEX_BANK_ERR;
        return G1_TEX_BANK_OK;
    }
    if (decode_rare(&b, out) != 0)
        return G1_TEX_BANK_ERR;
    return G1_TEX_BANK_OK;
}

int g1_tex_load_bank(unsigned id, const uint8_t *bytes, size_t n)
{
    G1TexBankOut d;
    if (g1_tex_bank_decode(bytes, n, &d) != G1_TEX_BANK_OK)
        return -1;
    if (d.tlut_ia) {
        if (d.fmt == G1_TEX_CI8)
            d.fmt = G1_TEX_IA16_CI8;
        else if (d.fmt == G1_TEX_CI4)
            d.fmt = G1_TEX_IA16_CI4;
    }
    return g1_tex_load_raw(id, d.fmt, d.w, d.h, d.texels, d.ntex,
                           d.ntlut ? d.tlut : NULL, d.ntlut);
}
