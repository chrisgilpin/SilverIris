/*
 * Developer visual harness: load a gitignored retail pack, draw a few
 * Facility cameras, write PNG + one-line HUD under .local/shots/.
 * Never commit the pack, ROM, or PNGs.
 *
 *   make -C tools/shots
 *   ./tools/shots/run.sh
 */
#include "port_api.h"

#include "fs/c0pack.h"
#include "fs/stage.h"
#include "fs/prop.h"
#include "player/move.h"
#include "player/gun.h"
#include "player/stan_walk.h"
#include "gfx/tmem.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define PACK_DEFAULT "/home/grok/GoldenEye/.local/pack/ge.u.c0pack"
#define OUT_DEFAULT "/home/grok/GoldenEye/.local/shots"

#define HALL_X (-400.0f)
#define HALL_Z (-2600.0f)
#define HALL_TH 270.0f
#define STAIR_X 783.0f
#define STAIR_Z (-2394.0f)
#define STAIR_TH 0.0f

static void usage(void)
{
    fprintf(stderr, "shot --pack ge.u.c0pack --out .local/shots\n");
}

static uint8_t *read_all(const char *path, size_t *n)
{
    FILE *f;
    long sz;
    uint8_t *buf;
    f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *n = (size_t)sz;
    return buf;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    static uint32_t tbl[256];
    static int ready;
    size_t i;
    if (!ready) {
        uint32_t c;
        int b, k;
        for (b = 0; b < 256; b++) {
            c = (uint32_t)b;
            for (k = 0; k < 8; k++)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            tbl[b] = c;
        }
        ready = 1;
    }
    crc ^= 0xFFFFFFFFu;
    for (i = 0; i < n; i++)
        crc = tbl[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int wr_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n)
{
    uint8_t hdr[8];
    uint8_t tail[4];
    uint32_t crc;
    be32(hdr, n);
    memcpy(hdr + 4, type, 4);
    if (fwrite(hdr, 1, 8, f) != 8)
        return -1;
    if (n && fwrite(data, 1, n, f) != n)
        return -1;
    crc = crc32_update(0, hdr + 4, 4);
    if (n)
        crc = crc32_update(crc ^ 0xFFFFFFFFu, data, n) ^ 0xFFFFFFFFu;
    /* crc32_update always does init+final XOR; chain type then data. */
    {
        uint32_t c = 0xFFFFFFFFu;
        static uint32_t tbl[256];
        static int ready;
        uint32_t i;
        if (!ready) {
            uint32_t v;
            int b, k;
            for (b = 0; b < 256; b++) {
                v = (uint32_t)b;
                for (k = 0; k < 8; k++)
                    v = (v & 1u) ? (0xEDB88320u ^ (v >> 1)) : (v >> 1);
                tbl[b] = v;
            }
            ready = 1;
        }
        for (i = 0; i < 4; i++)
            c = tbl[(c ^ (uint8_t)type[i]) & 0xFFu] ^ (c >> 8);
        for (i = 0; i < n; i++)
            c = tbl[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
        crc = c ^ 0xFFFFFFFFu;
    }
    be32(tail, crc);
    if (fwrite(tail, 1, 4, f) != 4)
        return -1;
    return 0;
}

static uint32_t adler32(const uint8_t *p, size_t n)
{
    uint32_t a = 1, b = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        a = (a + p[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

/* Uncompressed PNG (RGBA8). No libpng. */
static int write_png(const char *path, const uint8_t *rgba, int w, int h)
{
    FILE *f;
    uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    uint8_t ihdr[13];
    uint8_t *raw;
    uint8_t *z;
    size_t raw_n, z_n, o;
    size_t row = (size_t)w * 4u;
    int y;
    uint32_t ad;
    const size_t max_store = 65535u;

    raw_n = (size_t)h * (1u + row);
    raw = (uint8_t *)malloc(raw_n);
    if (!raw)
        return -1;
    for (y = 0; y < h; y++) {
        raw[(size_t)y * (1u + row)] = 0;
        memcpy(raw + (size_t)y * (1u + row) + 1, rgba + (size_t)y * row, row);
    }
    /* zlib stored blocks */
    z_n = 2 + raw_n + ((raw_n + max_store - 1) / max_store) * 5 + 4;
    z = (uint8_t *)malloc(z_n);
    if (!z) {
        free(raw);
        return -1;
    }
    z[0] = 0x78;
    z[1] = 0x01;
    o = 2;
    {
        size_t left = raw_n;
        size_t src = 0;
        while (left) {
            size_t chunk = left > max_store ? max_store : left;
            uint8_t last = (left == chunk) ? 1u : 0u;
            z[o++] = last;
            z[o++] = (uint8_t)(chunk);
            z[o++] = (uint8_t)(chunk >> 8);
            z[o++] = (uint8_t)(~chunk);
            z[o++] = (uint8_t)((~chunk) >> 8);
            memcpy(z + o, raw + src, chunk);
            o += chunk;
            src += chunk;
            left -= chunk;
        }
    }
    ad = adler32(raw, raw_n);
    be32(z + o, ad);
    o += 4;
    free(raw);

    f = fopen(path, "wb");
    if (!f) {
        free(z);
        return -1;
    }
    if (fwrite(sig, 1, 8, f) != 8) {
        fclose(f);
        free(z);
        return -1;
    }
    be32(ihdr + 0, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    if (wr_chunk(f, "IHDR", ihdr, 13) != 0 || wr_chunk(f, "IDAT", z, (uint32_t)o) != 0 ||
        wr_chunk(f, "IEND", NULL, 0) != 0) {
        fclose(f);
        free(z);
        return -1;
    }
    fclose(f);
    free(z);
    return 0;
}

static void describe_fb(const uint8_t *rgba, int w, int h, char *out, size_t outn)
{
    unsigned nz = 0, dark = 0;
    unsigned long r = 0, g = 0, b = 0;
    int i, n = w * h;
    int cx, cy;
    for (i = 0; i < n; i++) {
        unsigned pr = rgba[i * 4 + 0];
        unsigned pg = rgba[i * 4 + 1];
        unsigned pb = rgba[i * 4 + 2];
        if (pr | pg | pb)
            nz++;
        if (pr + pg + pb < 24)
            dark++;
        r += pr;
        g += pg;
        b += pb;
    }
    cx = (w / 2) * 4 + (h / 2) * w * 4;
    snprintf(out, outn, "nz=%u dark=%u mean_rgb=%lu,%lu,%lu center=%u,%u,%u", nz, dark,
             r / (unsigned long)n, g / (unsigned long)n, b / (unsigned long)n, rgba[cx],
             rgba[cx + 1], rgba[cx + 2]);
}

static void place(float x, float z, float th)
{
    float ey = PORT_EYE_HEIGHT;
    if (port_stan_eye_y(x, z, &ey) != 0)
        (void)port_stan_nearest_eye_y(x, z, PORT_STAN_NEAR_XZ, &ey);
    if (!(ey == ey) || ey > 1.0e20f || ey < -1.0e20f)
        ey = PORT_EYE_HEIGHT;
    port_player_set_pose(x, ey, z, th);
}

static int shot_one(const char *out_dir, const char *tag)
{
    char png[512], hud[512], extra[256];
    const uint8_t *fb;
    FILE *hf;
    int mag, reserve, tiles, on;
    unsigned nz;

    port_api_draw();
    fb = port_api_fb();
    if (!fb)
        return -1;
    nz = port_api_fb_nonzero();
    mag = port_api_gun_mag();
    reserve = port_api_gun_reserve();
    tiles = port_api_stan_tiles();
    on = port_api_stan_on_tile();
    snprintf(hud, sizeof hud,
             "%s x=%.2f z=%.2f y=%.2f th=%.1f ph=%.1f fb=%u stan=%d/%d mag=%d/%d "
             "settex=%u texOk=%u texMiss=%u abs=%u dec=%u last=%u %s guards=%d parts=%d "
             "drawn=%d viewgun=%d flash=%d",
             tag, (double)port_api_player_x(), (double)port_api_player_z(),
             (double)port_api_player_y(), (double)port_api_player_theta(),
             (double)port_api_player_phi(), nz, on, tiles, mag, reserve,
             port_api_settex(), port_api_tex_ok(), port_api_tex_miss(),
             port_api_tex_miss_absent(), port_api_tex_miss_decode(),
             (unsigned)g1_tex_last_id(), port_prop_idle_info(), port_prop_guard_count(),
             port_prop_guard_parts(), port_prop_drawn(), port_prop_viewgun_parts(),
             port_gun_flash_frames());
    describe_fb(fb, port_api_fb_width(), port_api_fb_height(), extra, sizeof extra);
    printf("%s  draw=%d rooms=%d/%d %s\n", hud, port_api_last_draw(),
           port_api_rooms_walked(), port_api_current_room(), extra);
    snprintf(png, sizeof png, "%s/%s.png", out_dir, tag);
    if (write_png(png, fb, port_api_fb_width(), port_api_fb_height()) != 0) {
        fprintf(stderr, "write %s failed: %s\n", png, strerror(errno));
        return -1;
    }
    snprintf(png, sizeof png, "%s/%s.hud", out_dir, tag);
    hf = fopen(png, "w");
    if (hf) {
        fprintf(hf, "%s\n", hud);
        fclose(hf);
    }
    return 0;
}

static void list_anim(const uint8_t *pack, size_t n)
{
    C0Pack p;
    uint32_t i;
    int found = 0;
    if (c0pack_open(pack, n, &p) != 0) {
        fprintf(stderr, "c0pack_open failed\n");
        return;
    }
    for (i = 0; i < p.nfiles; i++) {
        const char *path = p.files[i].path;
        if (path && strstr(path, "animationtable")) {
            printf("pack %.*s %u\n", (int)p.files[i].path_len, path, p.files[i].size);
            found++;
        }
    }
    if (!found)
        printf("pack: no animationtable bins\n");
    c0pack_close(&p);
}

int main(int argc, char **argv)
{
    const char *pack_path = PACK_DEFAULT;
    const char *out_dir = OUT_DEFAULT;
    uint8_t *pack = NULL;
    size_t pack_len = 0;
    uint8_t hash[32];
    int a, rc = 1;
    float spawn_x = 0.f, spawn_z = 0.f, spawn_th = 0.f;

    for (a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--pack") == 0 && a + 1 < argc)
            pack_path = argv[++a];
        else if (strcmp(argv[a], "--out") == 0 && a + 1 < argc)
            out_dir = argv[++a];
        else if (strcmp(argv[a], "-h") == 0 || strcmp(argv[a], "--help") == 0) {
            usage();
            return 0;
        } else {
            usage();
            return 2;
        }
    }

    if (mkdir(out_dir, 0700) != 0 && errno != EEXIST) {
        fprintf(stderr, "mkdir %s: %s\n", out_dir, strerror(errno));
        return 1;
    }

    pack = read_all(pack_path, &pack_len);
    if (!pack) {
        fprintf(stderr, "read pack %s failed\n", pack_path);
        return 1;
    }
    if (c0pack_validate(pack, pack_len, hash) != 0) {
        fprintf(stderr, "pack validate failed\n");
        free(pack);
        return 1;
    }
    list_anim(pack, pack_len);

    if (port_api_init(pack, (uint32_t)pack_len, hash) != PORT_OK) {
        fprintf(stderr, "init: %s\n", port_api_last_error());
        free(pack);
        return 1;
    }
    if (port_api_load_stage(PORT_LEVEL_FACILITY) != PORT_STAGE_OK) {
        fprintf(stderr, "load_stage: %s\n", port_stage_last_error());
        port_api_shutdown();
        free(pack);
        return 1;
    }

    printf("%s guards=%d parts=%d walkers=%d\n", port_prop_idle_info(),
           port_prop_guard_count(), port_prop_guard_parts(), port_prop_walk_count());
    {
        int i, ng = port_prop_guard_count();
        float wx, wz;
        for (i = 0; i < ng && i < 16; i++) {
            float gx, gz;
            if (port_prop_guard_xz(i, &gx, &gz) == 0)
                printf("guard[%d] xz=%.1f,%.1f\n", i, (double)gx, (double)gz);
        }
        if (port_prop_walk_xz(&wx, &wz) == 0)
            printf("walker xz=%.1f,%.1f\n", (double)wx, (double)wz);
        {
            float sx = port_api_player_x(), sz = port_api_player_z();
            float best = 1e18f, bx = 0.f, bz = 0.f;
            int bi = -1;
            for (i = 0; i < ng; i++) {
                float gx, gz, d;
                if (port_prop_guard_xz(i, &gx, &gz) != 0)
                    continue;
                d = (gx - sx) * (gx - sx) + (gz - sz) * (gz - sz);
                if (d < best) {
                    best = d;
                    bx = gx;
                    bz = gz;
                    bi = i;
                }
            }
            if (bi >= 0)
                printf("idle_corner[%d] xz=%.1f,%.1f dist=%.1f\n", bi, (double)bx,
                       (double)bz, (double)sqrtf(best));
        }
    }
    spawn_x = port_api_player_x();
    spawn_z = port_api_player_z();
    spawn_th = port_api_player_theta();
    if (shot_one(out_dir, "spawn") != 0)
        goto done;
    port_player_set_pitch(-35.f);
    if (shot_one(out_dir, "spawn_lookdown") != 0)
        goto done;
    port_player_set_pitch(0.f);
    place(HALL_X, HALL_Z, HALL_TH);
    if (shot_one(out_dir, "hallway") != 0)
        goto done;
    place(STAIR_X, STAIR_Z, STAIR_TH);
    if (shot_one(out_dir, "stairs") != 0)
        goto done;
    /* Flash cards at spawn. Tick the gun directly so a facing door does
     * not swallow Z. Not committed. */
    place(spawn_x, spawn_z, spawn_th);
    port_player_set_pitch(0.f);
    port_gun_tick(PORT_Z_TRIG);
    if (shot_one(out_dir, "flash") != 0)
        goto done;
    {
        int i;
        for (i = 0; i < 4; i++)
            port_gun_tick(0);
    }
    if (shot_one(out_dir, "flash_off") != 0)
        goto done;
    /* Profile of the posed-walk test mover (240u +X, look -X). */
    {
        float wx, wz;
        if (port_prop_walk_xz(&wx, &wz) == 0) {
            float cx = wx + 240.f, cz = wz;
            float lx = wx - cx, lz = wz - cz;
            float th = atan2f(lx, -lz) * (180.f / 3.14159265f);
            if (th < 0.f)
                th += 360.f;
            place(cx, cz, th);
            port_player_set_pitch(0.f);
            if (shot_one(out_dir, "walk") != 0)
                goto done;
        }
    }
    rc = 0;

done:
    port_api_shutdown();
    free(pack);
    return rc;
}
