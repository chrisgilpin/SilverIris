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
#include "gfx/gbi_interp.h"

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

static uint32_t g_last_fb_adler;
static uint32_t g_spawn_fb_adler;

static uint32_t adler32(const uint8_t *p, size_t n);

/* Door-sized Rare quads on spawn r71->r7->r8->r20->r19->r18 / r3-r18 / r19-r21 / r1-r3 / r11-r71 / r8-r5 / r8-r10 / catwalk r13-r15 / r14-r13 / r14-r15 / ground r2-r3 / r3-r5 / r5-r4 / r10-r11 / r21-r22 / r72-r3 / r73-r11.
 * Far-links with no slab are not listed — do not invent doors. */
static void dump_path_doors(void)
{
    float r1[3];
    int i, no, npath = 0;
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    no = port_stage_opening_count();
    for (i = 0; i < no; i++) {
        float pos[3], yaw = 0.f, width = 0.f;
        int ra = 0, rb = 0;
        if (port_stage_opening(i, pos, &yaw, &width, &ra, &rb) != 0)
            continue;
        if (!port_stage_path_opening(ra, rb))
            continue;
        npath++;
        printf("path_opening r%d-r%d local=%.1f,%.1f yaw=%.1f w=%.1f\n",
               ra, rb, (double)(pos[0] - r1[0]), (double)(pos[2] - r1[2]),
               (double)yaw, (double)width);
    }
    printf("path_doors n=%d openings=%d stan=%d\n", npath,
           port_stage_opening_count(), port_stan_door_count());
}

/* Face a documented path portal, Z-unlatch, prove collision AND parked pose. */
static int path_unlatch_proof(void)
{
    float r1[3], pos[3], yaw = 0.f, width = 0.f;
    float ox, oz, lx, lz, px, pz, y = 86.8f, th;
    float nx, nz, ny, ax, az;
    int i, no, ra = 0, rb = 0, found = 0, used, opened;
    int closed_block = 0, open_pass = 0;
    unsigned ad_closed = 0, ad_open = 0;

    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    no = port_stage_opening_count();
    /* Prefer a documented path door (r7-r8 / r3-r5). Require a 120-unit
     * stand on a stan tile so the Z-unlatch is real. No new binds. */
    {
        static const int pick[][2] = {
            {7, 8}, {8, 7}, {3, 5}, {5, 3}, {10, 11}, {11, 10}, {5, 4}, {4, 5},
            {2, 3}, {3, 2}, {21, 22}, {22, 21}, {72, 3}, {3, 72},
            {73, 11}, {11, 73},
        };
        int p;
        for (p = 0; p < (int)(sizeof pick / sizeof pick[0]) && !found; p++) {
            for (i = 0; i < no; i++) {
                float sx, sz, slx, slz, ox0, oz0;
                if (port_stage_opening(i, pos, &yaw, &width, &ra, &rb) != 0)
                    continue;
                if (ra != pick[p][0] || rb != pick[p][1])
                    continue;
                ox0 = pos[0] - r1[0];
                oz0 = pos[2] - r1[2];
                if (yaw == 90.f) {
                    slx = 1.f;
                    slz = 0.f;
                } else {
                    slx = 0.f;
                    slz = -1.f;
                }
                sx = ox0 - slx * 120.f;
                sz = oz0 - slz * 120.f;
                if (!port_stan_on_tile(sx, sz)) {
                    sx = ox0 + slx * 120.f;
                    sz = oz0 + slz * 120.f;
                }
                if (!port_stan_on_tile(sx, sz))
                    continue;
                found = 1;
                break;
            }
        }
    }
    if (!found) {
        for (i = 0; i < no; i++) {
            if (port_stage_opening(i, pos, &yaw, &width, &ra, &rb) != 0)
                continue;
            if (port_stage_path_opening(ra, rb)) {
                found = 1;
                break;
            }
        }
    }
    if (!found) {
        printf("path_unlatch NONE (far-link, no slab)\n");
        return 0;
    }
    ox = pos[0] - r1[0];
    oz = pos[2] - r1[2];
    if (yaw == 90.f) {
        lx = 1.f;
        lz = 0.f;
    } else {
        lx = 0.f;
        lz = -1.f;
    }
    px = ox - lx * 120.f;
    pz = oz - lz * 120.f;
    if (!port_stan_on_tile(px, pz)) {
        px = ox + lx * 120.f;
        pz = oz + lz * 120.f;
        lx = -lx;
        lz = -lz;
    }
    if (port_stan_eye_y(px, pz, &y) != 0)
        y = 86.8f;
    {
        int rm = port_stan_tile_room_at_eye(px, pz, 737.4f);
        if (rm == 13 || rm == 14 || rm == 15)
            y = 737.4f;
    }
    th = atan2f(lx, -lz) * (180.f / 3.14159265f);
    if (th < 0.f)
        th += 360.f;
    port_player_set_pose(px, y, pz, th);
    port_player_set_pitch(0.f);

    nx = ox;
    nz = oz;
    ny = y;
    port_stan_clip_step(px, pz, &nx, &nz, &ny);
    ax = nx - ox;
    az = nz - oz;
    closed_block = (ax * ax + az * az > 25.f) || port_stan_door_is_open_at(pos[0], pos[2]) == 0;
    port_api_draw();
    ad_closed = adler32(port_api_fb(),
                        (size_t)port_api_fb_width() * (size_t)port_api_fb_height() * 4u);

    used = port_stan_use_door(px, pz, lx, lz);
    if (!used)
        used = port_stan_use_door(px + r1[0], pz + r1[2], lx, lz);
    opened = port_stan_door_is_open_at(pos[0], pos[2]);
    if (!opened) {
        port_api_set_pad(0, 0, 0, 0);
        if (port_api_sim_tick(5000) != 0)
            return -1;
        port_api_set_pad(0, 0, 0, 0x2000);
        if (port_api_sim_tick(5001) != 0)
            return -1;
        opened = port_stan_door_is_open_at(pos[0], pos[2]);
        if (opened)
            used = 1;
    }

    /* Collision drops on the use tick. Park swings over a few ticks so
     * the slab leaves the opening instead of teleporting / vanishing. */
    {
        int tck;
        for (tck = 0; tck < PORT_DOOR_OPEN_TICKS; tck++)
            port_stan_tick_doors();
    }
    port_player_set_pose(px, y, pz, th);
    nx = ox;
    nz = oz;
    ny = y;
    port_stan_clip_step(px, pz, &nx, &nz, &ny);
    ax = nx - ox;
    az = nz - oz;
    open_pass = (ax * ax + az * az <= 400.f);
    port_api_draw();
    ad_open = adler32(port_api_fb(),
                      (size_t)port_api_fb_width() * (size_t)port_api_fb_height() * 4u);
    {
        float pdx = 0.f, pdz = 0.f, pyaw = 0.f, frac;
        int parked;
        frac = port_stan_door_frac_at(pos[0], pos[2]);
        (void)port_prop_door_park_offset(pos[0], pos[2], yaw, &pdx, &pdz, &pyaw);
        parked = (pdx * pdx + pdz * pdz > 40.f * 40.f) || (fabsf(pyaw) > 30.f);
        if (opened)
            (void)port_stan_use_door(px, pz, lx, lz);

        printf("path_unlatch r%d-r%d local=%.1f,%.1f stand=%.1f,%.1f used=%d "
               "opened=%d closed_block=%d open_pass=%d frac=%.2f park=%.1f,%.1f "
               "yaw=%.1f adler %08x->%08x %s\n",
               ra, rb, (double)ox, (double)oz, (double)px, (double)pz, used,
               opened, closed_block, open_pass, (double)frac, (double)pdx,
               (double)pdz, (double)pyaw, ad_closed, ad_open,
               (opened && open_pass && parked) ? "OK" : "FAIL");
        if (!opened) {
            fprintf(stderr, "path_unlatch did not open\n");
            return -1;
        }
        if (!open_pass) {
            fprintf(stderr, "path_unlatch still blocked\n");
            return -1;
        }
        if (!parked) {
            fprintf(stderr, "path_unlatch slab still centered frac=%g d=%.1f,%.1f yaw=%.1f\n",
                    (double)frac, (double)pdx, (double)pdz, (double)pyaw);
            return -1;
        }
    }
    return 0;
}

/* Side-offset closed step at a wide fitted portal. A fixed 90-half slab
 * left walk-around gaps on r7-r8 / r20-r19 / r8-r5 / r1-r3. */
static int wide_door_side_proof(void)
{
    float r1[3], pos[3], yaw = 0.f, width = 0.f;
    float ox, oz, lx, lz, tx, tz, px, pz, y = 86.8f, th;
    float nx, nz, ny, side;
    int i, no, ra = 0, rb = 0, found = 0, used, opened;
    int closed_block = 0, open_pass = 0;
    static const int pick[][2] = {{7, 8}, {8, 7}, {8, 5}, {5, 8}};
    int p;

    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    no = port_stage_opening_count();
    for (p = 0; p < 4 && !found; p++) {
        for (i = 0; i < no; i++) {
            if (port_stage_opening(i, pos, &yaw, &width, &ra, &rb) != 0)
                continue;
            if (ra == pick[p][0] && rb == pick[p][1] && width > 200.f) {
                found = 1;
                break;
            }
        }
    }
    if (!found) {
        for (i = 0; i < no; i++) {
            if (port_stage_opening(i, pos, &yaw, &width, &ra, &rb) != 0)
                continue;
            if (port_stage_path_opening(ra, rb) && width > 200.f) {
                found = 1;
                break;
            }
        }
    }
    if (!found) {
        printf("wide_door_side NONE (no wide path portal)\n");
        fprintf(stderr, "wide_door_side no wide path portal\n");
        return -1;
    }
    ox = pos[0] - r1[0];
    oz = pos[2] - r1[2];
    if (yaw == 90.f) {
        lx = 1.f;
        lz = 0.f;
    } else {
        lx = 0.f;
        lz = -1.f;
    }
    tx = -lz;
    tz = lx;
    /* Midway between old 90-half and fitted half so a 90 slab would leak. */
    side = 0.5f * (90.f + 0.5f * width);
    if (side < 100.f)
        side = 100.f;
    if (side > 0.5f * width - 16.f)
        side = 0.5f * width - 16.f;

    {
        int a, b, ok = 0;
        float slx, slz;
        for (a = 0; a < 2 && !ok; a++) {
            slx = (a == 0) ? lx : -lx;
            slz = (a == 0) ? lz : -lz;
            for (b = 0; b < 2 && !ok; b++) {
                float s = (b == 0) ? side : -side;
                px = ox - slx * 120.f + tx * s;
                pz = oz - slz * 120.f + tz * s;
                if (port_stan_on_tile(px, pz)) {
                    lx = slx;
                    lz = slz;
                    side = s;
                    ok = 1;
                }
            }
        }
        if (!ok) {
            printf("wide_door_side r%d-r%d w=%.1f off-tile\n", ra, rb,
                   (double)width);
            fprintf(stderr, "wide_door_side stand off-tile\n");
            return -1;
        }
    }

    if (port_stan_eye_y(px, pz, &y) != 0)
        y = 86.8f;
    th = atan2f(lx, -lz) * (180.f / 3.14159265f);
    if (th < 0.f)
        th += 360.f;
    port_player_set_pose(px, y, pz, th);
    port_player_set_pitch(0.f);

    nx = ox + tx * side;
    nz = oz + tz * side;
    ny = y;
    port_stan_clip_step(px, pz, &nx, &nz, &ny);
    {
        float along = (nx - ox) * lx + (nz - oz) * lz;
        closed_block = (along < -8.f) && !port_stan_door_is_open_at(pos[0], pos[2]);
    }

    used = port_stan_use_door(px, pz, lx, lz);
    if (!used)
        used = port_stan_use_door(px + r1[0], pz + r1[2], lx, lz);
    opened = port_stan_door_is_open_at(pos[0], pos[2]);
    if (!opened) {
        port_api_set_pad(0, 0, 0, 0);
        if (port_api_sim_tick(5100) != 0)
            return -1;
        port_api_set_pad(0, 0, 0, 0x2000);
        if (port_api_sim_tick(5101) != 0)
            return -1;
        opened = port_stan_door_is_open_at(pos[0], pos[2]);
        if (opened)
            used = 1;
    }

    port_player_set_pose(px, y, pz, th);
    nx = ox + tx * side;
    nz = oz + tz * side;
    ny = y;
    port_stan_clip_step(px, pz, &nx, &nz, &ny);
    {
        float along = (nx - ox) * lx + (nz - oz) * lz;
        open_pass = (along >= -8.f);
    }
    if (opened)
        (void)port_stan_use_door(px, pz, lx, lz);

    printf("wide_door_side r%d-r%d w=%.1f side=%.1f stand=%.1f,%.1f used=%d "
           "opened=%d closed_block=%d open_pass=%d %s\n",
           ra, rb, (double)width, (double)side, (double)px, (double)pz, used,
           opened, closed_block, open_pass,
           (closed_block && opened && open_pass) ? "OK" : "FAIL");
    if (!closed_block) {
        fprintf(stderr, "wide_door_side closed side step leaked\n");
        return -1;
    }
    if (!opened) {
        fprintf(stderr, "wide_door_side did not open\n");
        return -1;
    }
    if (!open_pass) {
        fprintf(stderr, "wide_door_side open side step blocked\n");
        return -1;
    }
    return 0;
}

/* Facing use: hinge is fitted half-w so 90 parks off the opening
 * (along-tangent ~ half-w, not 90). Narrow 128 doors still look like
 * a door. Collision drop + 6-tick interpol are unchanged. */
static int hinge_park_one(const int pick[][2], int npick, float wlo, float whi,
                          const char *tag)
{
    float r1[3], pos[3], yaw = 0.f, width = 0.f;
    float ox, oz, lx, lz, px, pz, y = 86.8f, th;
    float tx, tz;
    int i, no, ra = 0, rb = 0, found = 0, used, opened;
    int p;

    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    no = port_stage_opening_count();
    for (p = 0; p < npick && !found; p++) {
        for (i = 0; i < no; i++) {
            if (port_stage_opening(i, pos, &yaw, &width, &ra, &rb) != 0)
                continue;
            if (ra != pick[p][0] || rb != pick[p][1])
                continue;
            if (width < wlo || width > whi)
                continue;
            found = 1;
            break;
        }
    }
    if (!found) {
        for (i = 0; i < no; i++) {
            if (port_stage_opening(i, pos, &yaw, &width, &ra, &rb) != 0)
                continue;
            if (!port_stage_path_opening(ra, rb))
                continue;
            if (width < wlo || width > whi)
                continue;
            found = 1;
            break;
        }
    }
    if (!found) {
        printf("hinge_park %s NONE\n", tag);
        fprintf(stderr, "hinge_park %s no portal\n", tag);
        return -1;
    }
    ox = pos[0] - r1[0];
    oz = pos[2] - r1[2];
    if (yaw == 90.f) {
        lx = 1.f;
        lz = 0.f;
    } else {
        lx = 0.f;
        lz = -1.f;
    }
    tx = -lz;
    tz = lx;
    px = ox - lx * 120.f;
    pz = oz - lz * 120.f;
    if (!port_stan_on_tile(px, pz)) {
        px = ox + lx * 120.f;
        pz = oz + lz * 120.f;
        lx = -lx;
        lz = -lz;
    }
    if (!port_stan_on_tile(px, pz)) {
        printf("hinge_park %s r%d-r%d w=%.1f off-tile\n", tag, ra, rb,
               (double)width);
        fprintf(stderr, "hinge_park %s stand off-tile\n", tag);
        return -1;
    }
    if (port_stan_eye_y(px, pz, &y) != 0)
        y = 86.8f;
    {
        int rm = port_stan_tile_room_at_eye(px, pz, 737.4f);
        if (rm == 13 || rm == 14 || rm == 15)
            y = 737.4f;
    }
    th = atan2f(lx, -lz) * (180.f / 3.14159265f);
    if (th < 0.f)
        th += 360.f;
    port_player_set_pose(px, y, pz, th);
    port_player_set_pitch(0.f);

    used = port_stan_use_door(px, pz, lx, lz);
    if (!used)
        used = port_stan_use_door(px + r1[0], pz + r1[2], lx, lz);
    opened = port_stan_door_is_open_at(pos[0], pos[2]);
    if (!opened) {
        port_api_set_pad(0, 0, 0, 0);
        if (port_api_sim_tick(5200) != 0)
            return -1;
        port_api_set_pad(0, 0, 0, 0x2000);
        if (port_api_sim_tick(5201) != 0)
            return -1;
        opened = port_stan_door_is_open_at(pos[0], pos[2]);
        if (opened)
            used = 1;
    }
    {
        int tck;
        for (tck = 0; tck < PORT_DOOR_OPEN_TICKS; tck++)
            port_stan_tick_doors();
    }
    {
        float pdx = 0.f, pdz = 0.f, pyaw = 0.f, frac, along, dist, hw;
        int ok;
        frac = port_stan_door_frac_at(pos[0], pos[2]);
        (void)port_prop_door_park_offset(pos[0], pos[2], yaw, &pdx, &pdz, &pyaw);
        along = pdx * tx + pdz * tz;
        dist = sqrtf(pdx * pdx + pdz * pdz);
        hw = 0.5f * width;
        /* Tangent component of a 90 park is the hinge offset (~ half-w). */
        ok = opened && frac >= 0.99f && fabsf(pyaw) > 80.f &&
             fabsf(fabsf(along) - hw) <= 24.f;
        if (hw > 140.f && fabsf(along) < 110.f)
            ok = 0; /* still the old 90-half */
        if (hw < 80.f && fabsf(along) > 110.f)
            ok = 0; /* flew across the room */
        if (opened)
            (void)port_stan_use_door(px, pz, lx, lz);
        printf("hinge_park %s r%d-r%d w=%.1f hw=%.1f stand=%.1f,%.1f used=%d "
               "opened=%d frac=%.2f park=%.1f,%.1f along=%.1f dist=%.1f "
               "yaw=%.1f %s\n",
               tag, ra, rb, (double)width, (double)hw, (double)px, (double)pz,
               used, opened, (double)frac, (double)pdx, (double)pdz,
               (double)along, (double)dist, (double)pyaw, ok ? "OK" : "FAIL");
        if (!ok) {
            fprintf(stderr,
                    "hinge_park %s want along~%.1f got %.1f (not 90-half)\n",
                    tag, (double)hw, (double)along);
            return -1;
        }
    }
    return 0;
}

static int hinge_width_park_proof(void)
{
    static const int wide[][2] = {{8, 7}, {7, 8}, {8, 5}, {5, 8}};
    static const int narrow[][2] = {{8, 20}, {20, 8}, {3, 18}, {18, 3}};
    if (hinge_park_one(wide, 4, 200.f, 400.f, "wide") != 0)
        return -1;
    if (hinge_park_one(narrow, 4, 100.f, 160.f, "narrow") != 0)
        return -1;
    return 0;
}

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

/* clip_step from the stairs foot. 8-way greedy toward room 13, preferring
 * a rising floor. Must reach a high upstairs tile (eye ~737) and not snap
 * back to 86.8. Bathroom xz is not a stair link and must stay low. */

/* r3 ground stair foot (T2565). Rare-links to T2300 r15. */
#define R3_STAIR_X 1161.1f
#define R3_STAIR_Z (-717.9f)
/* r18 ground stair foot (T2296). Rare-links to T2300/T2302 r15. */
#define R18_STAIR_X 1165.6f
#define R18_STAIR_Z (-731.2f)
/* Chris walk cameras: spawn→r7→r8→r20→r19→r18 foot. Eye 86.8. */
#define R20_CAM_X 1157.f
#define R20_CAM_Z (-1536.f)
#define R19_CAM_X 1476.f
#define R19_CAM_Z (-1232.f)
#define R18_CAM_X 1166.f
#define R18_CAM_Z (-729.f)

/* Walk clip_step from spawn to a ground stair foot. Facility r20 lab
 * north wall is an unlinked gap; Rare point.link hops r20->r19->r18.
 * Path portal slabs now span the Rare quad, so this walk stops at the
 * first closed fitted door (r7-r8). Z-unlatch is proved separately.
 * Setup PROPDEF_DOOR pads sit in the gas-plant cluster (~9ku). */
static int spawn_to_stair_note(float sx, float sz, float tx, float tz)
{
    float x = sx, z = sz, ny = 0.f;
    int k, room0, room1, blocked = 0, last, hops = 0;
    room0 = port_stan_tile_room(sx, sz);
    last = room0;
    printf("spawn_to_stair begin start=%.1f,%.1f room=%d dest=%.1f,%.1f\n",
           (double)sx, (double)sz, room0, (double)tx, (double)tz);
    for (k = 0; k < 400; k++) {
        float dx = tx - x, dz = tz - z, dist, nx, nz, jx, jz;
        int room;
        dist = sqrtf(dx * dx + dz * dz);
        if (dist < 20.f) {
            printf("spawn_to_stair REACH step=%d xz=%.1f,%.1f eye=%.1f room=%d hops=%d\n",
                   k, (double)x, (double)z, (double)ny,
                   port_stan_tile_room(x, z), hops);
            return 0;
        }
        if (dist > 12.f) {
            dx *= 12.f / dist;
            dz *= 12.f / dist;
        }
        nx = x + dx;
        nz = z + dz;
        port_stan_clip_step(x, z, &nx, &nz, &ny);
        if (nx == x && nz == z) {
            blocked = 1;
            break;
        }
        jx = nx - x;
        jz = nz - z;
        if (jx * jx + jz * jz > 80.f * 80.f) {
            hops++;
            printf("spawn_to_stair HOP step=%d from=%.1f,%.1f r%d to=%.1f,%.1f r%d\n",
                   k, (double)x, (double)z, last, (double)nx, (double)nz,
                   port_stan_tile_room(nx, nz));
        }
        x = nx;
        z = nz;
        room = port_stan_tile_room(x, z);
        if (room != last) {
            printf("spawn_to_stair room %d->%d step=%d xz=%.1f,%.1f eye=%.1f\n",
                   last, room, k, (double)x, (double)z, (double)ny);
            last = room;
        }
    }
    room1 = port_stan_tile_room(x, z);
    printf("spawn_to_stair %s step=%d start=%.1f,%.1f room=%d end=%.1f,%.1f "
           "eye=%.1f room=%d hops=%d\n",
           blocked ? "DOOR" : "SHORT", k, (double)sx, (double)sz, room0,
           (double)x, (double)z, (double)ny, room1, hops);
    if (blocked) {
        printf("spawn_to_stair note: closed path door (Z-unlatch is separate)\n");
        return 0;
    }
    fprintf(stderr, "spawn_to_stair failed (want r3/r18 foot)\n");
    return -1;
}

/* clip_step along Rare rising links from a ground stair onto r15/r13. */
static int upper_stair_proof(const char *tag, float sx, float sz)
{
    float ex = sx, ez = sz, ey = 0.f, bath = 0.f;
    int er = 0, rc;
    if (port_stan_eye_y(sx, sz, &ey) != 0)
        ey = PORT_EYE_HEIGHT;
    printf("upper_stair %s start xz=%.1f,%.1f eye=%.1f room=%d\n",
           tag, (double)sx, (double)sz, (double)ey, port_stan_tile_room(sx, sz));
    rc = port_stan_climb_along_links(sx, sz, &ex, &ez, &ey, &er);
    printf("upper_stair %s rc=%d end xz=%.1f,%.1f eye=%.1f room=%d\n",
           tag, rc, (double)ex, (double)ez, (double)ey, er);
    if (port_stan_eye_y(-491.9f, -2238.5f, &bath) != 0 || bath < 70.f || bath > 110.f) {
        fprintf(stderr, "upper_stair %s bathroom eye=%.1f (want ~86.8)\n",
                tag, (double)bath);
        return -1;
    }
    if (rc != 0 || !(ey == ey) || ey < 600.f || (er != 13 && er != 15)) {
        fprintf(stderr, "upper_stair %s failed rc=%d eye=%.1f room=%d "
                "(want r15/r13 eye~737)\n", tag, rc, (double)ey, er);
        return -1;
    }
    printf("upper_stair %s OK bath=%.1f (r6 island still unlinked; no invented hop)\n",
           tag, (double)bath);
    return 0;
}

static int stairs_climb_proof(float spawn_x, float spawn_z)
{
    static const float kdx[8] = { 8.f, 8.f, 0.f, -8.f, -8.f, -8.f, 0.f, 8.f };
    static const float kdz[8] = { 0.f, 8.f, 8.f, 8.f, 0.f, -8.f, -8.f, -8.f };
    const float r13x = -650.f, r13z = -3050.f;
    const float bathx = -491.9f, bathz = -2238.5f;
    float x = STAIR_X, z = STAIR_Z, ny = 0.f, ey = 0.f;
    int step, high = 0, r13 = 0, snap = 0, d;

    if (port_stan_eye_y(x, z, &ny) != 0)
        ny = PORT_EYE_HEIGHT;
    printf("stairs_climb start xz=%.1f,%.1f eye=%.1f\n", (double)x, (double)z, (double)ny);
    for (step = 0; step < 400; step++) {
        float best_s = -1.0e30f, bx = x, bz = z, by = ny;
        int moved = 0;
        for (d = 0; d < 8; d++) {
            float nx = x + kdx[d], nz = z + kdz[d], ty = ny, s, ddx, ddz;
            port_stan_clip_step(x, z, &nx, &nz, &ty);
            if (nx == x && nz == z)
                continue;
            if (!(ty == ty) || ty > 1.0e20f || ty < -1.0e20f) {
                fprintf(stderr, "stairs_climb NaN step=%d\n", step);
                return -1;
            }
            if (ny > 200.f && ty < 200.f)
                continue;
            {
                float tx = r13x, tz = r13z;
                /* Stair link is just +z of the foot (room 7 -> room 6). */
                if (ty < 200.f && ny < 200.f) {
                    tx = 600.f;
                    tz = -2300.f;
                }
                ddx = tx - nx;
                ddz = tz - nz;
            }
            s = -(ddx * ddx + ddz * ddz) * 0.001f;
            if (ty > ny + 4.f)
                s += 40000.f;
            if (ty > 200.f)
                s += 80000.f;
            if (ty > 600.f)
                s += 200000.f;
            if (s > best_s) {
                best_s = s;
                bx = nx;
                bz = nz;
                by = ty;
                moved = 1;
            }
        }
        if (!moved) {
            printf("stairs_climb stuck step=%d xz=%.1f,%.1f eye=%.1f room=%d\n",
                   step, (double)x, (double)z, (double)ny, port_stan_tile_room(x, z));
            break;
        }
        x = bx;
        z = bz;
        ny = by;
        if (ny > 380.f) {
            if (!high)
                printf("stairs_climb HIGH step=%d xz=%.1f,%.1f eye=%.1f room=%d "
                       "(linked upper floor+175)\n",
                       step, (double)x, (double)z, (double)ny, port_stan_tile_room(x, z));
            high = 1;
            if (port_stan_tile_room(x, z) == 13 || ny > 600.f) {
                r13 = 1;
                printf("stairs_climb ROOM13 step=%d xz=%.1f,%.1f eye=%.1f\n",
                       step, (double)x, (double)z, (double)ny);
                break;
            }
            /* Held the stacked upper: bathroom-style snap would have dropped to 86.8. */
            if (high && step > 8)
                break;
        }
        if (high && ny < 200.f) {
            printf("stairs_climb SNAPDOWN step=%d xz=%.1f,%.1f eye=%.1f\n",
                   step, (double)x, (double)z, (double)ny);
            snap = 1;
            break;
        }
        if (step < 6 || step % 80 == 0)
            printf("stairs_climb[%d] xz=%.1f,%.1f eye=%.1f room=%d\n",
                   step, (double)x, (double)z, (double)ny, port_stan_tile_room(x, z));
    }
    if (port_stan_eye_y(bathx, bathz, &ey) != 0 || ey < 70.f || ey > 110.f) {
        fprintf(stderr, "stairs_climb bathroom eye=%.1f (want ~86.8)\n", (double)ey);
        return -1;
    }
    printf("stairs_climb end xz=%.1f,%.1f eye=%.1f room=%d high=%d r13=%d snap=%d bath=%.1f\n",
           (double)x, (double)z, (double)ny, port_stan_tile_room(x, z),
           high, r13, snap, (double)ey);
    if (!high || snap) {
        fprintf(stderr, "stairs_climb failed high=%d snap=%d\n", high, snap);
        return -1;
    }
    /* r6 landing is a mid-height island (405.9) with no Rare link to r13/15.
     * Real ground stairs r3/r18 and the r12 ramp Rare-link onto r15. */
    if (spawn_to_stair_note(spawn_x, spawn_z, R3_STAIR_X, R3_STAIR_Z) != 0)
        return -1;
    if (spawn_to_stair_note(spawn_x, spawn_z, R18_STAIR_X, R18_STAIR_Z) != 0)
        return -1;
    if (upper_stair_proof("r3_T2565", R3_STAIR_X, R3_STAIR_Z) != 0)
        return -1;
    if (upper_stair_proof("r18_T2296", R18_STAIR_X, R18_STAIR_Z) != 0)
        return -1;
    /* r12 T2374 is a mid ramp (ay=126.5), not a ground stair. r6 landing
     * stays an island — no invented hop onto r13/15. */
    return 0;
}


static int shot_one(const char *out_dir, const char *tag);

/* Hold the camera on a climbed upstairs tile and draw that room GDL.
 * place() would snap Y to the lowest stacked floor (~86.8). */
static void place_at_eye(float x, float y, float z, float th)
{
    if (!(y == y) || y > 1.0e20f || y < -1.0e20f)
        y = PORT_EYE_HEIGHT;
    port_player_set_pose(x, y, z, th);
}

static int upstairs_gdl_proof(const char *out_dir)
{
    float p13[3], p15[3], ex = 0.f, ez = 0.f, ey = 0.f;
    uint32_t n13 = 0, n15 = 0;
    int er = 0, rc13, rc15, cur, walked, i, nbg, ok = 0;
    unsigned texok, nz;
    const float pads[2][2] = { {158.f, -2777.f}, {-650.f, -3050.f} };

    memset(p13, 0, sizeof p13);
    memset(p15, 0, sizeof p15);
    rc13 = port_stage_room_gdl(13, &n13, p13);
    rc15 = port_stage_room_gdl(15, &n15, p15);
    printf("upstairs_gdl r13 rc=%d ngfx=%u pos=%.1f,%.1f,%.1f  r15 rc=%d ngfx=%u pos=%.1f,%.1f,%.1f\n",
           rc13, n13, (double)p13[0], (double)p13[1], (double)p13[2],
           rc15, n15, (double)p15[0], (double)p15[1], (double)p15[2]);
    nbg = port_stage_bg_rooms();
    for (i = 1; i <= nbg; i++) {
        if (port_stage_rooms_adjacent(13, i))
            printf("upstairs_portal r13-%d\n", i);
        if (port_stage_rooms_adjacent(15, i))
            printf("upstairs_portal r15-%d\n", i);
    }
    if ((rc13 != 0 || n13 == 0) && (rc15 != 0 || n15 == 0)) {
        fprintf(stderr, "upstairs rooms 13/15 have no GDL — stop\n");
        return -2;
    }

    if (port_stan_climb_along_links(R3_STAIR_X, R3_STAIR_Z, &ex, &ez, &ey, &er) == 0 &&
        ey > 600.f && (er == 13 || er == 15)) {
        ok = 1;
        printf("upstairs_cam climb xz=%.1f,%.1f eye=%.1f room=%d\n",
               (double)ex, (double)ez, (double)ey, er);
    }
    if (!ok) {
        for (i = 0; i < 2; i++) {
            float py = 737.4f;
            int rm = port_stan_tile_room_at_eye(pads[i][0], pads[i][1], py);
            printf("upstairs_pad %.1f,%.1f eye=737.4 tile_eye=%d lowest=%d\n",
                   (double)pads[i][0], (double)pads[i][1], rm,
                   port_stan_tile_room(pads[i][0], pads[i][1]));
            port_stan_debug_at(pads[i][0], pads[i][1]);
            if (rm == 13 || rm == 15) {
                ex = pads[i][0];
                ez = pads[i][1];
                ey = py;
                er = rm;
                ok = 1;
                break;
            }
        }
    }
    if (!ok) {
        fprintf(stderr, "upstairs_cam no r13/r15 pose\n");
        return -1;
    }
    port_stan_debug_at(ex, ez);

    /* Aim at neighbor r12 GDL from the climbed r13 tile. Pad-to-pad yaw
     * looked along the catwalk into empty space. Yaw 200 (vs exact GDL
     * 218) keeps the r12 walls in frame. */
    {
        float r1[3], p12[3], tx, ty, tz, dx, dy, dz, th, ph, horiz;
        uint32_t n12 = 0;

        memset(r1, 0, sizeof r1);
        memset(p12, 0, sizeof p12);
        (void)port_stage_room1(r1);
        if (port_stage_room_gdl(12, &n12, p12) != 0 || n12 == 0) {
            if (n15) {
                p12[0] = p15[0];
                p12[1] = p15[1];
                p12[2] = p15[2];
                n12 = n15;
            } else {
                p12[0] = p13[0];
                p12[1] = p13[1];
                p12[2] = p13[2];
            }
        }
        tx = p12[0] - r1[0];
        ty = p12[1] - r1[1];
        tz = p12[2] - r1[2];
        dx = tx - ex;
        dy = ty - ey;
        dz = tz - ez;
        th = atan2f(dx, -dz) * (180.f / 3.14159265f);
        if (th < 0.f)
            th += 360.f;
        horiz = sqrtf(dx * dx + dz * dz);
        ph = (horiz < 1e-4f) ? -22.f : atan2f(dy, horiz) * (180.f / 3.14159265f);
        if (ph > 20.f)
            ph = 20.f;
        if (ph < -40.f)
            ph = -40.f;
        if (th > 210.f && th < 230.f)
            th = 200.f;
        place_at_eye(ex, ey, ez, th);
        port_player_set_pitch(ph);
        printf("upstairs_aim nbr=12 xz=%.1f,%.1f eye=%.1f th=%.1f ph=%.1f tgt=%.1f,%.1f,%.1f ngfx=%u\n",
               (double)ex, (double)ez, (double)ey, (double)th, (double)ph,
               (double)tx, (double)ty, (double)tz, n12);
    }

    if (shot_one(out_dir, "upstairs") != 0)
        return -1;
    cur = port_api_current_room();
    walked = port_api_rooms_walked();
    texok = port_api_tex_ok();
    nz = port_api_fb_nonzero();
    printf("upstairs_proof cur=%d walked=%d texOk=%u fb_nz=%u eye=%.1f xz=%.1f,%.1f\n",
           cur, walked, texok, nz, (double)port_api_player_y(),
           (double)port_api_player_x(), (double)port_api_player_z());
    if (cur != 13 && cur != 15) {
        fprintf(stderr, "upstairs cur=%d (want 13 or 15)\n", cur);
        return -1;
    }
    if (texok == 0 || nz == 0) {
        fprintf(stderr, "upstairs blank texOk=%u nz=%u\n", texok, nz);
        return -1;
    }
    return 0;
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

/* On-mesh stacked xz: nearest_eye_y must match tile_at_world, not a
 * high walkway centroid. Low Facility floors sit near 86.8. */
static int probe_eye_band(const char *tag, float x, float z, float lo, float hi)
{
    float ey = 0.f, ny = 0.f;
    int got_e, got_n;
    got_e = port_stan_eye_y(x, z, &ey) == 0;
    got_n = port_stan_nearest_eye_y(x, z, PORT_STAN_NEAR_XZ, &ny) == 0;
    printf("eye_probe %s xz=%.1f,%.1f on=%d eye=%s%.1f nearest=%s%.1f\n",
           tag, (double)x, (double)z, port_stan_on_tile(x, z),
           got_e ? "" : "off/", (double)ey, got_n ? "" : "off/", (double)ny);
    if (!got_n) {
        fprintf(stderr, "%s nearest_eye_y miss\n", tag);
        return -1;
    }
    if (ny < lo || ny > hi) {
        fprintf(stderr, "%s nearest_eye_y=%.1f want %.1f..%.1f\n", tag,
                (double)ny, (double)lo, (double)hi);
        return -1;
    }
    if (got_e && (ey < lo || ey > hi)) {
        fprintf(stderr, "%s eye_y=%.1f want %.1f..%.1f\n", tag, (double)ey,
                (double)lo, (double)hi);
        return -1;
    }
    return 0;
}

/* Look at a world-space standing mid-torso from a local camera xz. */
static void aim_world(float wx, float wy, float wz, float cam_x, float cam_z)
{
    float r1[3], tx, ty, tz, dx, dy, dz, th, ph, horiz, ey;
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    tx = wx - r1[0];
    ty = wy - r1[1] + 90.f;
    tz = wz - r1[2];
    place(cam_x, cam_z, 0.f);
    ey = port_api_player_y();
    dx = tx - cam_x;
    dy = ty - ey;
    dz = tz - cam_z;
    th = atan2f(dx, -dz) * (180.f / 3.14159265f);
    if (th < 0.f)
        th += 360.f;
    horiz = sqrtf(dx * dx + dz * dz);
    ph = (horiz < 1e-4f) ? 0.f : atan2f(dy, horiz) * (180.f / 3.14159265f);
    if (ph > 70.f)
        ph = 70.f;
    if (ph < -70.f)
        ph = -70.f;
    place(cam_x, cam_z, th);
    port_player_set_pitch(ph);
}

/* Distance from point to the xz ray (ox,oz) + t*(dx,dz), t>=0. */
static float ray_xz_dist(float ox, float oz, float dx, float dz, float px, float pz)
{
    float lx = px - ox, lz = pz - oz, t, qx, qz;
    float inv = dx * dx + dz * dz;
    if (inv < 1e-8f)
        return sqrtf(lx * lx + lz * lz);
    t = (lx * dx + lz * dz) / inv;
    if (t < 0.f)
        t = 0.f;
    qx = ox + dx * t;
    qz = oz + dz * t;
    qx = px - qx;
    qz = pz - qz;
    return sqrtf(qx * qx + qz * qz);
}

/* Open room floor, not a stall cubicle (neighbors walkable). */
static int cam_open_floor(float x, float z)
{
    float ey = 0.f;
    if (!port_stan_on_tile(x, z))
        return 0;
    if (port_stan_eye_y(x, z, &ey) != 0 || ey < 50.f || ey > 160.f)
        return 0;
    if (!port_stan_on_tile(x + 40.f, z) || !port_stan_on_tile(x - 40.f, z))
        return 0;
    if (!port_stan_on_tile(x, z + 40.f) || !port_stan_on_tile(x, z - 40.f))
        return 0;
    return 1;
}

static int shot_one(const char *out_dir, const char *tag)
{
    char png[512], hud[512], extra[256];
    const uint8_t *fb;
    FILE *hf;
    int mag, reserve, tiles, on, hp;
    unsigned nz;

    port_api_draw();
    fb = port_api_fb();
    if (!fb)
        return -1;
    g_last_fb_adler = adler32(fb, (size_t)port_api_fb_width() * (size_t)port_api_fb_height() * 4u);
    nz = port_api_fb_nonzero();
    mag = port_api_gun_mag();
    reserve = port_api_gun_reserve();
    tiles = port_api_stan_tiles();
    on = port_api_stan_on_tile();
    hp = port_api_health();
    snprintf(hud, sizeof hud,
             "%s x=%.2f z=%.2f y=%.2f th=%.1f ph=%.1f fb=%u stan=%d/%d mag=%d/%d "
             "hp=%d%s kills=%d gfire=%d alert=%d settex=%u texOk=%u texMiss=%u abs=%u dec=%u last=%u %s "
             "guards=%d parts=%d drawn=%d viewgun=%d flash=%d",
             tag, (double)port_api_player_x(), (double)port_api_player_z(),
             (double)port_api_player_y(), (double)port_api_player_theta(),
             (double)port_api_player_phi(), nz, on, tiles, mag, reserve,
             hp, hp <= 0 ? " DEAD" : "", port_api_kills(), port_prop_guard_shots(),
             port_prop_guard_alerted(),
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

/* Ground-lab cameras on the walked path. cur must match the tile room,
 * the frame must be textured and not the spawn stall. Look along the
 * walk so a doorway into the next lab is in frame. */
static int lab_cam_proof(const char *out_dir, const char *tag,
                         float x, float z, int want, float lx, float lz)
{
    float dx, dz, th, ey;
    int cur, tile, at;
    unsigned texok, nz;

    dx = lx - x;
    dz = lz - z;
    th = atan2f(dx, -dz) * (180.f / 3.14159265f);
    if (th < 0.f)
        th += 360.f;
    if (probe_eye_band(tag, x, z, 70.f, 110.f) != 0)
        return -1;
    place(x, z, th);
    port_player_set_pitch(0.f);
    port_stan_debug_at(x, z);
    if (shot_one(out_dir, tag) != 0)
        return -1;
    cur = port_api_current_room();
    tile = port_stan_tile_room(x, z);
    ey = port_api_player_y();
    at = port_stage_room_at_local(x, ey, z);
    texok = port_api_tex_ok();
    nz = port_api_fb_nonzero();
    printf("lab_cam %s xz=%.1f,%.1f eye=%.1f th=%.1f cur=%d tile=%d at=%d "
           "walked=%d texOk=%u fb_nz=%u adler=%08x spawn_adler=%08x %s\n",
           tag, (double)x, (double)z, (double)ey, (double)th, cur, tile, at,
           port_api_rooms_walked(), texok, nz, (unsigned)g_last_fb_adler,
           (unsigned)g_spawn_fb_adler,
           (g_spawn_fb_adler && g_last_fb_adler == g_spawn_fb_adler)
               ? "SPAWNSTALL"
               : "DIFF");
    if (cur != tile || cur != want) {
        fprintf(stderr, "%s cur=%d tile=%d (want %d)\n", tag, cur, tile, want);
        return -1;
    }
    if (texok == 0 || nz == 0) {
        fprintf(stderr, "%s blank texOk=%u nz=%u\n", tag, texok, nz);
        return -1;
    }
    if (g_spawn_fb_adler && g_last_fb_adler == g_spawn_fb_adler) {
        fprintf(stderr, "%s spawn stall pixels adler=%08x\n", tag,
                (unsigned)g_last_fb_adler);
        return -1;
    }
    return 0;
}

static int lab_path_proof(const char *out_dir)
{
    float p18[3], p19[3], p20[3];
    uint32_t n18 = 0, n19 = 0, n20 = 0;
    int i, nbg;

    memset(p18, 0, sizeof p18);
    memset(p19, 0, sizeof p19);
    memset(p20, 0, sizeof p20);
    (void)port_stage_room_gdl(18, &n18, p18);
    (void)port_stage_room_gdl(19, &n19, p19);
    (void)port_stage_room_gdl(20, &n20, p20);
    printf("lab_gdl r18 ngfx=%u pos=%.1f,%.1f,%.1f  r19 ngfx=%u pos=%.1f,%.1f,%.1f  "
           "r20 ngfx=%u pos=%.1f,%.1f,%.1f\n",
           n18, (double)p18[0], (double)p18[1], (double)p18[2],
           n19, (double)p19[0], (double)p19[1], (double)p19[2],
           n20, (double)p20[0], (double)p20[1], (double)p20[2]);
    nbg = port_stage_bg_rooms();
    for (i = 1; i <= nbg; i++) {
        if (port_stage_rooms_adjacent(18, i))
            printf("lab_portal r18-%d\n", i);
        if (port_stage_rooms_adjacent(19, i))
            printf("lab_portal r19-%d\n", i);
        if (port_stage_rooms_adjacent(20, i))
            printf("lab_portal r20-%d\n", i);
        if (port_stage_rooms_adjacent(8, i))
            printf("lab_portal r8-%d\n", i);
        if (port_stage_rooms_adjacent(7, i))
            printf("lab_portal r7-%d\n", i);
    }
    /* Look along the walk so the next/prev lab is in frame. */
    if (lab_cam_proof(out_dir, "r20", R20_CAM_X, R20_CAM_Z, 20, R19_CAM_X,
                      R19_CAM_Z) != 0)
        return -1;
    if (lab_cam_proof(out_dir, "r19", R19_CAM_X, R19_CAM_Z, 19, R18_CAM_X,
                      R18_CAM_Z) != 0)
        return -1;
    if (lab_cam_proof(out_dir, "r18", R18_CAM_X, R18_CAM_Z, 18, R19_CAM_X,
                      R19_CAM_Z) != 0)
        return -1;
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
        else if (strcmp(argv[a], "--probe") == 0)
            ; /* handled after stage load */
        else if (strcmp(argv[a], "--doors") == 0)
            ; /* handled after stage load */
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
    {
        int doors = 0;
        int aa;
        for (aa = 1; aa < argc; aa++)
            if (strcmp(argv[aa], "--doors") == 0)
                doors = 1;
        dump_path_doors();
        if (doors) {
            float sx = port_api_player_x(), sz = port_api_player_z();
            int urc;
            port_stage_dump_portals();
            urc = path_unlatch_proof();
            if (urc == 0)
                urc = wide_door_side_proof();
            if (urc == 0)
                urc = hinge_width_park_proof();
            if (urc == 0)
                urc = spawn_to_stair_note(sx, sz, R18_STAIR_X, R18_STAIR_Z);
            port_api_shutdown();
            free(pack);
            return urc != 0 ? 3 : 0;
        }
    }

    {
        int probe = 0;
        for (a = 1; a < argc; a++)
            if (strcmp(argv[a], "--probe") == 0)
                probe = 1;
        if (probe) {
            port_stan_dump_stair_links();
            printf("stairs_probe cross-room links\n");
            port_stan_dump_cross(18, 15);
            port_stan_dump_cross(3, 15);
            port_stan_dump_cross(12, 15);
            port_stan_dump_cross(13, 12);
            port_stan_dump_cross(15, 13);
            port_stan_dump_cross(6, 15);
            port_stan_dump_cross(6, 13);
            port_stan_dump_cross(18, 0);
            port_stan_dump_cross(3, 0);
            port_stan_dump_cross(12, 0);
            port_stan_dump_cross(15, 0);
            port_stan_dump_cross(13, 0);
            {
                static const int ktiles[] = {
                    2292, 2296, 2298, 2299, 2300, 2301, 2302, 2304,
                    2334, 2335, 2336, 2374, 2565
                };
                int ti;
                printf("stairs_probe named tiles\n");
                for (ti = 0; ti < (int)(sizeof ktiles / sizeof ktiles[0]); ti++) {
                    port_stan_dump_tile_i(ktiles[ti]);
                    port_stan_dump_rare((unsigned)ktiles[ti]);
                }
            }
            printf("stairs_probe foot\n");
            port_stan_debug_at(STAIR_X, STAIR_Z);
            port_stan_link_reach(STAIR_X, STAIR_Z);
            printf("stairs_probe bathroom\n");
            port_stan_debug_at(-491.9f, -2238.5f);
            port_stan_link_reach(-491.9f, -2238.5f);
            printf("stairs_probe room13\n");
            port_stan_debug_at(-650.f, -3050.f);
            port_stan_link_reach(-650.f, -3050.f);
            {
                float ex, ez, ey;
                int er, rc;
                static const float kfeet[][2] = {
                    {1161.1f, -717.9f}, /* T2565 r3 */
                    {1134.6f, -864.6f}, /* T2292 r18 */
                    {1165.6f, -731.2f}, /* T2296 r18 */
                    {1165.6f, -762.7f}, /* T2298 r18 */
                    {1205.3f, -749.1f}, /* T2299 r18 */
                    {-147.9f, -2703.3f}, /* T2374 r12 ramp */
                    {-89.9f, -2670.7f}, /* T2304 r15 */
                    {-131.9f, -2675.1f}, /* T2302 r15 */
                };
                static const char *ktag[] = {"r3_T2565","r18_T2292","r18_T2296","r18_T2298","r18_T2299","r12_T2374","r15_T2304","r15_T2302"};
                int fi;
                for (fi = 0; fi < 8; fi++) {
                    printf("climb_try %s start=%.1f,%.1f\n", ktag[fi],
                           (double)kfeet[fi][0], (double)kfeet[fi][1]);
                    port_stan_debug_at(kfeet[fi][0], kfeet[fi][1]);
                    rc = port_stan_climb_along_links(kfeet[fi][0], kfeet[fi][1],
                                                     &ex, &ez, &ey, &er);
                    printf("climb_try %s rc=%d end=%.1f,%.1f eye=%.1f room=%d\n",
                           ktag[fi], rc, (double)ex, (double)ez, (double)ey, er);
                }
                printf("ramp_dump\n");
                { int di; for (di = 2370; di <= 2382; di++) port_stan_dump_tile_i(di); }
                { int di; for (di = 2300; di <= 2313; di++) port_stan_dump_tile_i(di); }
                printf("probe_spawn xz=%.1f,%.1f y=%.1f room=%d\n",
                       (double)port_api_player_x(), (double)port_api_player_z(),
                       (double)port_api_player_y(), port_api_current_room());
            }
            if (stairs_climb_proof(port_api_player_x(), port_api_player_z()) != 0) {
                port_api_shutdown();
                free(pack);
                return 3;
            }
            port_api_shutdown();
            free(pack);
            return 0;
        }
    }
    printf("%s guards=%d parts=%d walkers=%d\n", port_prop_idle_info(),
           port_prop_guard_count(), port_prop_guard_parts(), port_prop_walk_count());
    printf("aim_decode have=%d idle_crc=%08x aim_crc=%08x %s\n",
           port_prop_have_aim(), (unsigned)port_prop_idle_rest_crc(),
           (unsigned)port_prop_aim_rest_crc(),
           (port_prop_have_aim() &&
            port_prop_idle_rest_crc() != port_prop_aim_rest_crc()) ? "DIFF" :
           (port_prop_idle_rest_crc() != port_prop_aim_rest_crc()) ? "DECODE" : "SAME");
    {
        int i, ng = port_prop_guard_count();
        float wx, wy, wz;
        for (i = 0; i < ng && i < 16; i++) {
            float gx, gz;
            if (port_prop_guard_xz(i, &gx, &gz) == 0)
                printf("guard[%d] xz=%.1f,%.1f\n", i, (double)gx, (double)gz);
        }
        if (port_prop_walk_xyz(&wx, &wy, &wz) == 0) {
            float r1[3];
            r1[0] = r1[1] = r1[2] = 0.f;
            (void)port_stage_room1(r1);
            printf("walker xyz=%.1f,%.1f,%.1f (local %.1f,%.1f,%.1f) f=%d\n",
                   (double)wx, (double)wy, (double)wz,
                   (double)(wx - r1[0]), (double)(wy - r1[1]), (double)(wz - r1[2]),
                   port_prop_walk_frame());
        }
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
    printf("spawn_first y=%.1f xz=%.1f,%.1f on=%d\n",
           (double)port_api_player_y(), (double)spawn_x, (double)spawn_z,
           port_stan_on_tile(spawn_x, spawn_z));
    if (shot_one(out_dir, "spawn") != 0)
        goto done;
    g_spawn_fb_adler = g_last_fb_adler;
    {
        int cur = port_api_current_room();
        int tile = port_stan_tile_room(spawn_x, spawn_z);
        int dark = (cur == 12 || cur == 14);
        printf("spawn_room cur=%d tile=%d drawn=%d (stall must stay tiled, "
               "not 12/14) %s\n",
               cur, tile, port_prop_drawn(), dark ? "DARK14" : "STALL");
        {
            int wi, wr;
            int has19 = port_stage_walked_has(19);
            int has18 = port_stage_walked_has(18);
            printf("spawn_walked n=%d", port_stage_rooms_walked());
            for (wi = 0; wi < port_stage_rooms_walked(); wi++) {
                wr = port_stage_walked_room(wi);
                printf(" %d", wr);
            }
            printf(" r19=%d r18=%d\n", has19, has18);
            if (!has19) {
                fprintf(stderr, "spawn walked set missing r19\n");
                goto done;
            }
        }
    }
    if (probe_eye_band("spawn", spawn_x, spawn_z, 70.f, 110.f) != 0)
        goto done;
    {
        float x, z, ey, best = 0.f, bx = 0.f, bz = 0.f, ny = 0.f;
        int n = 0;
        for (z = -3400.f; z <= -1800.f; z += 50.f) {
            for (x = -800.f; x <= 1200.f; x += 50.f) {
                if (!port_stan_on_tile(x, z))
                    continue;
                if (port_stan_eye_y(x, z, &ey) != 0 || ey < 200.f)
                    continue;
                n++;
                if (ey > best) {
                    best = ey;
                    bx = x;
                    bz = z;
                }
            }
        }
        printf("high_floor_scan n=%d best xz=%.1f,%.1f eye=%.1f\n", n, (double)bx,
               (double)bz, (double)best);
        if (n < 1) {
            fprintf(stderr, "high_floor_scan found no upstairs-linked tile\n");
            goto done;
        }
        (void)port_stan_nearest_eye_y(bx, bz, PORT_STAN_NEAR_XZ, &ny);
        printf("high_floor_best nearest=%.1f room=%d (must stay high)\n",
               (double)ny, port_stan_tile_room(bx, bz));
        if (ny < 200.f) {
            fprintf(stderr, "high_floor flattened nearest=%.1f\n", (double)ny);
            goto done;
        }
        port_stan_debug_at(bx, bz);
    }
    /* Chris Chrome: bathroom xz y=406, WASD dead, fire still works. */
    {
        const float stuck_x = -491.9f, stuck_z = -2238.5f;
        float x1, z1, y1, ey = 0.f;
        int on1, nb = 0;
        if (probe_eye_band("bathroom", stuck_x, stuck_z, 70.f, 110.f) != 0)
            goto done;
        printf("stuck_probe\n");
        port_stan_debug_at(stuck_x, stuck_z);
        port_player_set_pose(stuck_x, 405.9f, stuck_z, 181.f);
        port_api_set_pad(0, 0, 0, 0);
        if (port_api_sim_tick(1) != 0) {
            fprintf(stderr, "unstick tick failed\n");
            goto done;
        }
        x1 = port_api_player_x();
        z1 = port_api_player_z();
        y1 = port_api_player_y();
        on1 = port_stan_on_tile(x1, z1);
        if (port_stan_on_tile(x1 + 4.f, z1)) nb++;
        if (port_stan_on_tile(x1 - 4.f, z1)) nb++;
        if (port_stan_on_tile(x1, z1 + 4.f)) nb++;
        if (port_stan_on_tile(x1, z1 - 4.f)) nb++;
        if (port_stan_eye_y(x1, z1, &ey) != 0)
            ey = y1;
        printf("unstick xz=%.1f,%.1f y=%.1f (was 405.9) on=%d nb=%d eye=%.1f\n",
               (double)x1, (double)z1, (double)y1, on1, nb, (double)ey);
        if (!on1 || nb < 1) {
            fprintf(stderr, "unstick still trapped on=%d nb=%d\n", on1, nb);
            goto done;
        }
        if (y1 > 200.f && ey > 200.f) {
            fprintf(stderr, "unstick y=%.1f still high (bathroom not upper)\n",
                    (double)y1);
            goto done;
        }
        if (shot_one(out_dir, "unstick") != 0)
            goto done;
        place(spawn_x, spawn_z, spawn_th);
        port_player_set_pitch(0.f);
    }
    /* First-frame spawn HUD is gfire=0. A few fire ticks must stay hp=8. */
    {
        int tck, hp0, hp1, s0, s1, combat = 0;
        hp0 = port_api_health();
        s0 = port_prop_guard_shots();
        place(spawn_x, spawn_z, spawn_th);
        port_player_set_pitch(0.f);
        for (tck = 0; tck < 8; tck++)
            combat |= port_prop_tick_guard_fire();
        hp1 = port_api_health();
        s1 = port_prop_guard_shots();
        printf("spawn_safe ticks=8 combat=%d hp=%d->%d shots=%d->%d los=%d %s\n",
               combat, hp0, hp1, s0, s1, port_prop_guard_los(),
               (hp1 == 8 && s1 == 0) ? "SAFE" : "HIT");
    }
    port_player_set_pitch(-35.f);
    if (shot_one(out_dir, "spawn_lookdown") != 0)
        goto done;
    port_player_set_pitch(0.f);
    place(HALL_X, HALL_Z, HALL_TH);
    if (shot_one(out_dir, "hallway") != 0)
        goto done;
    {
        int cur = port_api_current_room();
        int tile = port_stan_tile_room(HALL_X, HALL_Z);
        int dark = (cur == 12 || cur == 14);
        printf("hall_room xz=%.1f,%.1f cur=%d tile=%d (must not snap 12/14) %s\n",
               (double)HALL_X, (double)HALL_Z, cur, tile,
               dark ? "DARK14" : "PINNED");
    }
    /* Chris Chrome clip: x -454.6 z -2694.9 y 86.8 snapped cur=14. */
    {
        const float bx = -454.6f, bz = -2694.9f;
        int cur, tile, at, near, dark;
        float by;
        if (probe_eye_band("bathhall", bx, bz, 70.f, 110.f) != 0)
            goto done;
        place(bx, bz, HALL_TH);
        if (shot_one(out_dir, "bathhall") != 0)
            goto done;
        by = port_api_player_y();
        cur = port_api_current_room();
        tile = port_stan_tile_room(bx, bz);
        near = port_stan_nearest_tile_room(bx, bz, PORT_STAN_NEAR_XZ);
        at = port_stage_room_at_local(bx, by, bz);
        dark = (cur == 12 || cur == 14 || at == 12 || at == 14);
        printf("bathhall_room xz=%.1f,%.1f y=%.1f cur=%d tile=%d near=%d at=%d %s\n",
               (double)bx, (double)bz, (double)by, cur, tile, near, at,
               dark ? "DARK14" : "PINNED");
        if (at != cur || at != 71 || dark) {
            fprintf(stderr, "bathhall room_at=%d cur=%d (want 71, not 12/14)\n",
                    at, cur);
            goto done;
        }
    }
    place(STAIR_X, STAIR_Z, STAIR_TH);
    {
        float sey = 0.f, sny = 0.f;
        (void)port_stan_eye_y(STAIR_X, STAIR_Z, &sey);
        (void)port_stan_nearest_eye_y(STAIR_X, STAIR_Z, PORT_STAN_NEAR_XZ, &sny);
        printf("stairs_eye xz=%.1f,%.1f on=%d eye=%.1f nearest=%.1f (foot of stairs; stacked 405.9 ignored)\n",
               (double)STAIR_X, (double)STAIR_Z, port_stan_on_tile(STAIR_X, STAIR_Z),
               (double)sey, (double)sny);
        port_stan_debug_at(STAIR_X, STAIR_Z);
    }
    if (shot_one(out_dir, "stairs") != 0)
        goto done;
    if (stairs_climb_proof(spawn_x, spawn_z) != 0)
        goto done;
    {
        int urc = upstairs_gdl_proof(out_dir);
        if (urc != 0)
            goto done;
    }
    if (probe_eye_band("bathroom_after_stairs", -491.9f, -2238.5f, 70.f, 110.f) != 0)
        goto done;
    if (lab_path_proof(out_dir) != 0)
        goto done;
    if (path_unlatch_proof() != 0)
        goto done;
    if (wide_door_side_proof() != 0)
        goto done;
    if (hinge_width_park_proof() != 0)
        goto done;
    /* Flash cards at spawn. Tick the gun directly so a facing door does
     * not swallow Z. Not committed. */
    place(spawn_x, spawn_z, spawn_th);
    port_player_set_pitch(0.f);
    {
        int k0 = port_api_kills();
        float ix = 0.f, iz = 0.f, wx = 0.f, wz = 0.f;
        int have_i = 0, have_w = 0, gi, ng;
        float best = 1e18f;
        float sx = port_api_player_x(), sz = port_api_player_z();
        float r1[3];
        r1[0] = r1[1] = r1[2] = 0.f;
        (void)port_stage_room1(r1);
        ng = port_prop_guard_count();
        for (gi = 0; gi < ng; gi++) {
            float gx, gz, lx, lz, d;
            if (port_prop_guard_xz(gi, &gx, &gz) != 0)
                continue;
            lx = gx - r1[0];
            lz = gz - r1[2];
            d = (lx - sx) * (lx - sx) + (lz - sz) * (lz - sz);
            if (d < best) {
                best = d;
                ix = gx;
                iz = gz;
                have_i = 1;
            }
        }
        if (port_prop_walk_xz(&wx, &wz) == 0)
            have_w = 1;
        {
            int gi, ng2 = port_prop_guard_count();
            float yaw0[128];
            int al0[128];
            float far_d = -1.f;
            int far_i = -1, far_al = 0, hear_n = 0, turn_n = 0, box_n = 0;
            float chase_x0[8], chase_z0[8];
            int chase_i[8], chase_n = 0;
            int tck, hp0, hp1, s0, s1, combat = 0;
            float py = port_api_player_y();
            int pr;

            if (ng2 > 128)
                ng2 = 128;
            for (gi = 0; gi < ng2; gi++) {
                yaw0[gi] = 0.f;
                al0[gi] = 0;
                (void)port_prop_guard_yaw(gi, &yaw0[gi], &al0[gi]);
            }
            pr = port_stage_room_at_local(sx, py, sz);
            port_gun_tick(PORT_Z_TRIG);
            printf("spawn_flash kills=%d->%d hits=%d idle_dead=%d walk_dead=%d "
                   "idle=%.1f,%.1f walk=%.1f,%.1f die=%d\n",
                   k0, port_api_kills(), port_api_gun_hits(),
                   have_i ? port_stan_guard_dead_at(ix, iz) : -1,
                   have_w ? port_stan_guard_dead_at(wx, wz) : -1,
                   (double)ix, (double)iz, (double)wx, (double)wz,
                   port_prop_have_die());
            for (gi = 0; gi < ng2; gi++) {
                float gx, gy, gz, glx, gly, glz, ddx, ddz, d2, yaw1;
                int al1, gr, dead, inbox, adj;
                if (port_prop_guard_xyz(gi, &gx, &gy, &gz) != 0)
                    continue;
                if (port_prop_guard_yaw(gi, &yaw1, &al1) != 0)
                    continue;
                glx = gx - r1[0];
                gly = gy - r1[1];
                glz = gz - r1[2];
                ddx = sx - glx;
                ddz = sz - glz;
                d2 = sqrtf(ddx * ddx + ddz * ddz);
                dead = port_stan_guard_dead_at(gx, gz);
                if (dead)
                    continue;
                if (d2 > far_d) {
                    far_d = d2;
                    far_i = gi;
                    far_al = al1;
                }
                gr = port_stage_room_at_local(glx, gly, glz);
                adj = (pr == gr) || port_stage_rooms_adjacent(pr, gr);
                inbox = (d2 >= 40.f && d2 <= 400.f && fabsf(ddz) <= 200.f && adj);
                if (d2 <= 800.f || al1) {
                    printf("hear_cand[%d] local=%.1f,%.1f dist=%.1f dz=%.1f "
                           "room=%d adj=%d inbox=%d yaw=%.1f->%.1f alert=%d->%d\n",
                           gi, (double)glx, (double)glz, (double)d2, (double)ddz,
                           gr, adj, inbox, (double)yaw0[gi], (double)yaw1,
                           al0[gi], al1);
                    hear_n++;
                    if (al1 && fabsf(yaw1 - yaw0[gi]) > 0.5f)
                        turn_n++;
                    if (inbox)
                        box_n++;
                    /* Outside the fire box: these are the chase candidates
                     * (the two |Δz|≈260 corner guards plus anyone else). */
                    if (al1 && !inbox && !dead && chase_n < 8) {
                        chase_i[chase_n] = gi;
                        chase_x0[chase_n] = glx;
                        chase_z0[chase_n] = glz;
                        chase_n++;
                    }
                }
            }
            printf("hear_far[%d] dist=%.1f alert=%d (2000u sniper must stay 0)\n",
                   far_i, (double)far_d, far_al);
            printf("chase_zfloor spawn=%.1f,%.1f |dz|>200 stall fire box "
                   "(approach corner, do not enter |dz|<=200 of spawn)\n",
                   (double)sx, (double)sz);
            hp0 = port_api_health();
            s0 = port_prop_guard_shots();
            /* Two-tick xz + extra-idle[0] walk-frame proof, then the
             * remaining 6 of the 8-tick window. */
            {
                int ci, idle0 = port_prop_idle_guard();
                int f1 = -1, f2 = -1, b1 = 0, b2 = 0;
                uint32_t c1 = 0, c2 = 0;
                float fit1 = 0.f, fit2 = 0.f;
                float i0x = 0.f, i0z = 0.f, i2x = 0.f, i2z = 0.f;
                if (idle0 >= 0) {
                    float gx, gy, gz;
                    if (port_prop_guard_xyz(idle0, &gx, &gy, &gz) == 0) {
                        i0x = gx - r1[0];
                        i0z = gz - r1[2];
                    }
                }
                combat |= port_prop_tick_guard_fire();
                if (idle0 >= 0) {
                    b1 = port_prop_guard_walk_bound(idle0);
                    f1 = b1 ? port_prop_walk_frame() : -1;
                    c1 = port_prop_walk_rest_crc();
                    fit1 = port_prop_guard_fit_scale(idle0);
                }
                combat |= port_prop_tick_guard_fire();
                if (idle0 >= 0) {
                    float gx, gy, gz;
                    b2 = port_prop_guard_walk_bound(idle0);
                    f2 = b2 ? port_prop_walk_frame() : -1;
                    c2 = port_prop_walk_rest_crc();
                    fit2 = port_prop_guard_fit_scale(idle0);
                    if (port_prop_guard_xyz(idle0, &gx, &gy, &gz) == 0) {
                        i2x = gx - r1[0];
                        i2z = gz - r1[2];
                    }
                }
                for (ci = 0; ci < chase_n; ci++) {
                    float gx, gy, gz, glx, glz, ddx, ddz, d0, step, toward;
                    if (port_prop_guard_xyz(chase_i[ci], &gx, &gy, &gz) != 0)
                        continue;
                    glx = gx - r1[0];
                    glz = gz - r1[2];
                    ddx = glx - chase_x0[ci];
                    ddz = glz - chase_z0[ci];
                    step = sqrtf(ddx * ddx + ddz * ddz);
                    d0 = (sx - chase_x0[ci]);
                    toward = ddx * d0 + ddz * (sz - chase_z0[ci]);
                    printf("chase_tick2[%d] local=%.1f,%.1f -> %.1f,%.1f "
                           "dxz=%.1f toward=%.1f %s\n",
                           chase_i[ci], (double)chase_x0[ci], (double)chase_z0[ci],
                           (double)glx, (double)glz, (double)step, (double)toward,
                           (toward > 0.5f && step > 0.5f) ? "CLOSE" :
                           (step < 0.5f) ? "ZFLOOR" : "AWAY");
                }
                if (chase_n < 1)
                    printf("chase_tick2 none (no alerted out-of-box hear cands)\n");
                {
                    float ddx = i2x - i0x, ddz = i2z - i0z;
                    float step = sqrtf(ddx * ddx + ddz * ddz);
                    float toward = ddx * (sx - i0x) + ddz * (sz - i0z);
                    int framediff = (f1 >= 0 && f2 >= 0 && f1 != f2 && c1 != c2);
                    int fitted = (fit2 > 0.05f && fit2 < 0.5f);
                    printf("chase_walk idle[0] gi=%d bound=%d/%d f=%d crc=%08x "
                           "f=%d crc=%08x fit=%.3f/%.3f dxz=%.1f toward=%.1f "
                           "walkers=%d %s %s %s\n",
                           idle0, b1, b2, f1, (unsigned)c1, f2, (unsigned)c2,
                           (double)fit1, (double)fit2, (double)step,
                           (double)toward, port_prop_walk_count(),
                           (toward > 0.5f && step > 0.5f) ? "CLOSE" :
                           (step < 0.5f) ? "ZFLOOR" : "AWAY",
                           framediff ? "WALK" : "SLIDE",
                           fitted ? "FIT185" : "BLOB1510");
                }
            }
            for (tck = 0; tck < 6; tck++)
                combat |= port_prop_tick_guard_fire();
            hp1 = port_api_health();
            s1 = port_prop_guard_shots();
            printf("spawn_hear ticks=8 combat=%d hp=%d->%d shots=%d->%d "
                   "alert=%d los=%d cands=%d turns=%d inbox=%d %s\n",
                   combat, hp0, hp1, s0, s1, port_prop_guard_alerted(),
                   port_prop_guard_los(), hear_n, turn_n, box_n,
                   (hp1 == 8 && s1 == s0 && !combat) ? "SAFE" : "HIT");
            if (!(hp1 == 8 && s1 == s0 && !combat) ||
                port_prop_guard_alerted() < 2) {
                fprintf(stderr, "spawn_hear want SAFE + two room-71 alerts\n");
                goto done;
            }
            /* Walk clip_step toward spawn without moving bodies. Same
             * rules as chase_step: Rare walls + spawn fire box. Must
             * not enter xz near (-27,-2740). */
            {
                int ci, crossed = 0;
                for (ci = 0; ci < chase_n; ci++) {
                    float lx = chase_x0[ci], lz = chase_z0[ci];
                    float ny = 0.f;
                    int k;
                    for (k = 0; k < 200; k++) {
                        float dx = sx - lx, dz = sz - lz;
                        float dist = sqrtf(dx * dx + dz * dz);
                        float nx, nz, ax, az;
                        int moved = 0;
                        if (dist < 40.f)
                            break;
                        nx = lx + dx / dist * 3.f;
                        nz = lz + dz / dist * 3.f;
                        port_stan_clip_step(lx, lz, &nx, &nz, &ny);
                        /* Fire box: |d spawn|<=400 and |dz|<=200. */
                        if ((nx - sx) * (nx - sx) + (nz - sz) * (nz - sz) <= 400.f * 400.f &&
                            fabsf(nz - sz) <= 200.f) {
                            ax = nx;
                            az = lz;
                            port_stan_clip_step(lx, lz, &ax, &az, &ny);
                            if (!((ax - sx) * (ax - sx) + (az - sz) * (az - sz) <= 400.f * 400.f &&
                                  fabsf(az - sz) <= 200.f) &&
                                (ax != lx || az != lz)) {
                                nx = ax;
                                nz = az;
                            } else {
                                ax = lx;
                                az = nz;
                                port_stan_clip_step(lx, lz, &ax, &az, &ny);
                                if (!((ax - sx) * (ax - sx) + (az - sz) * (az - sz) <= 400.f * 400.f &&
                                      fabsf(az - sz) <= 200.f) &&
                                    (ax != lx || az != lz)) {
                                    nx = ax;
                                    nz = az;
                                } else {
                                    break;
                                }
                            }
                        }
                        if (nx == lx && nz == lz)
                            break;
                        lx = nx;
                        lz = nz;
                        moved = 1;
                        (void)moved;
                    }
                    {
                        float ddx = lx - sx, ddz = lz - sz;
                        float near = sqrtf(ddx * ddx + ddz * ddz);
                        int inbox = (near >= 40.f && near <= 400.f &&
                                     fabsf(lz - sz) <= 200.f);
                        int stall = (near < 80.f);
                        if (stall || inbox)
                            crossed = 1;
                        printf("chase_clip[%d] start=%.1f,%.1f end=%.1f,%.1f "
                               "near_spawn=%.1f dz=%.1f %s\n",
                               chase_i[ci], (double)chase_x0[ci],
                               (double)chase_z0[ci], (double)lx, (double)lz,
                               (double)near, (double)(lz - sz),
                               stall ? "STALL" : inbox ? "INBOX" : "HELD");
                    }
                }
                if (chase_n < 1)
                    printf("chase_clip none\n");
                else
                    printf("chase_clip cands=%d %s\n", chase_n,
                           crossed ? "CROSSED" : "HELD");
            }
        }
    }
    if (shot_one(out_dir, "flash") != 0)
        goto done;
    {
        int i;
        for (i = 0; i < 4; i++)
            port_gun_tick(0);
    }
    if (shot_one(out_dir, "flash_off") != 0)
        goto done;
    /* Two frames of the posed-walk cycle. Pull the camera ~185u back on
     * open floor so walk.png is a full 185u figure, not a stall clip. */
    {
        float wx, wy, wz;
        if (port_prop_walk_xyz(&wx, &wy, &wz) == 0) {
            float r1[3], lx, lz, cx, cz, th;
            uint32_t h8, h20, r8, r20;
            int f8, f20;
            const float walk_pitch = -16.f;
            r1[0] = r1[1] = r1[2] = 0.f;
            (void)port_stage_room1(r1);
            lx = wx - r1[0];
            lz = wz - r1[2];
            wy = wy - r1[1];
            {
                float pax, paz, pbx, pbz;
                if (port_prop_walk_path(&pax, &paz, &pbx, &pbz) == 0) {
                    lx = 0.5f * (pax + pbx);
                    lz = 0.5f * (paz + pbz);
                    printf("walk_path local %.1f,%.1f -> %.1f,%.1f mid=%.1f,%.1f speed=%.1f\n",
                           (double)pax, (double)paz, (double)pbx, (double)pbz,
                           (double)lx, (double)lz, (double)port_prop_walk_speed());
                }
            }
            /* Prefer ~240u, open floor, f=8 vs f=20 FBDIFF. Skip spawn
             * corridor and 90-140u stall clips. */
            {
                /* Room-71 ground floor is wide in X (~560u) and shallow in Z
                 * (~160u). 240u north is the upper floor; 240u south is spawn.
                 * Look along X so walk.png is a full 185u figure in room 71. */
                static const float try_c[][2] = {
                    { -100.f, -160.f }, { -80.f, -160.f }, { -120.f, -160.f },
                    { -100.f, -200.f }, { -60.f, -160.f },
                    { -200.f, 0.f }, { -160.f, 0.f }, { -240.f, 0.f },
                    { -200.f, -80.f }, { -160.f, -80.f }, { -100.f, -80.f },
                    { -100.f, 0.f }, { -280.f, 0.f },
                    { 200.f, 0.f }, { 240.f, 0.f }, { 160.f, 0.f },
                    { 200.f, -80.f }, { 160.f, 80.f },
                };
                int k, best = -1, best_open = 0, best_in71 = 0;
                uint32_t best_h8 = 0, best_h20 = 0, best_r8 = 0, best_r20 = 0;
                float best_cx = 0.f, best_cz = 0.f, best_th = 0.f, best_dist = 0.f;
                int best_f8 = 0, best_f20 = 0, best_diff = 0;
                for (k = 0; k < (int)(sizeof try_c / sizeof try_c[0]); k++) {
                    float ey = 0.f, tcx, tcz, tth, lookx, lookz, dist, ds;
                    uint32_t a8, a20, rr8, rr20;
                    int ff8, ff20, open, diff;
                    tcx = lx + try_c[k][0];
                    tcz = lz + try_c[k][1];
                    dist = sqrtf(try_c[k][0] * try_c[k][0] + try_c[k][1] * try_c[k][1]);
                    ds = (tcx - spawn_x) * (tcx - spawn_x) +
                         (tcz - spawn_z) * (tcz - spawn_z);
                    if (ds < 180.f * 180.f)
                        continue;
                    /* Stall cubicle ~x=-220. East-of-stall cameras look
                     * west through beige partitions. */
                    if (tcx > -200.f)
                        continue;
                    if (!port_stan_on_tile(tcx, tcz))
                        continue;
                    if (port_stan_eye_y(tcx, tcz, &ey) != 0 || ey < 50.f || ey > 160.f)
                        continue;
                    lookx = lx - tcx;
                    lookz = lz - tcz;
                    tth = atan2f(lookx, -lookz) * (180.f / 3.14159265f);
                    if (tth < 0.f)
                        tth += 360.f;
                    open = cam_open_floor(tcx, tcz);
                    port_prop_set_walk_frame(8);
                    ff8 = port_prop_walk_frame();
                    rr8 = port_prop_walk_rest_crc();
                    place(tcx, tcz, tth);
                    port_player_set_pitch(walk_pitch);
                    port_api_draw();
                    a8 = adler32(port_api_fb(),
                                 (size_t)port_api_fb_width() * (size_t)port_api_fb_height() * 4u);
                    port_prop_set_walk_frame(20);
                    ff20 = port_prop_walk_frame();
                    rr20 = port_prop_walk_rest_crc();
                    place(tcx, tcz, tth);
                    port_player_set_pitch(walk_pitch);
                    port_api_draw();
                    a20 = adler32(port_api_fb(),
                                  (size_t)port_api_fb_width() * (size_t)port_api_fb_height() * 4u);
                    diff = (a8 != a20);
                    printf("walk_try k=%d cam=%.1f,%.1f th=%.1f d=%.0f open=%d room=%d/%d nz=%u %s drawn=%d\n",
                           k, (double)tcx, (double)tcz, (double)tth, (double)dist, open,
                           port_api_rooms_walked(), port_api_current_room(),
                           port_api_fb_nonzero(),
                           diff ? "FBDIFF" : "fbsame", port_prop_drawn());
                    {
                        int room = port_api_current_room();
                        int in71 = (room == 71);
                        int better = 0;
                        if (best < 0)
                            better = 1;
                        else if (in71 && !best_in71)
                            better = 1;
                        else if (in71 == best_in71 && diff && !best_diff)
                            better = 1;
                        else if (in71 == best_in71 && diff == best_diff) {
                            if (open && !best_open)
                                better = 1;
                            else if (open == best_open) {
                                float d0 = dist, d1 = best_dist;
                                int far0 = (d0 >= 185.f), far1 = (d1 >= 185.f);
                                if (far0 && !far1)
                                    better = 1;
                                else if (far0 == far1) {
                                    float e0 = fabsf(d0 - 185.f), e1 = fabsf(d1 - 185.f);
                                    if (e0 < e1)
                                        better = 1;
                                }
                            }
                        }
                        if (better) {
                            best = k;
                            best_cx = tcx;
                            best_cz = tcz;
                            best_th = tth;
                            best_h8 = a8;
                            best_h20 = a20;
                            best_r8 = rr8;
                            best_r20 = rr20;
                            best_f8 = ff8;
                            best_f20 = ff20;
                            best_open = open;
                            best_dist = dist;
                            best_diff = diff;
                            best_in71 = in71;
                        }
                    }
                }
                if (best < 0) {
                    cx = lx + 240.f;
                    cz = lz;
                    {
                        float lookx = lx - cx, lookz = lz - cz;
                        th = atan2f(lookx, -lookz) * (180.f / 3.14159265f);
                        if (th < 0.f)
                            th += 360.f;
                    }
                    f8 = 8;
                    f20 = 20;
                    r8 = r20 = h8 = h20 = 0;
                } else {
                    cx = best_cx;
                    cz = best_cz;
                    th = best_th;
                    f8 = best_f8;
                    f20 = best_f20;
                    r8 = best_r8;
                    r20 = best_r20;
                    h8 = best_h8;
                    h20 = best_h20;
                }
            }
            /* Two harness frames at different walk ticks. Same camera.
             * 40 ticks * 3u = 120u (~1.5 tiles) so xz must move and the
             * figure stays in the ~185u room-71 lens. */
            {
                float x0, y0, z0, x1, y1, z1, r10[3];
                int t, nt = 40;
                r10[0] = r10[1] = r10[2] = 0.f;
                (void)port_stage_room1(r10);
                port_prop_set_walk_frame(8);
                if (port_prop_walk_xyz(&x0, &y0, &z0) != 0)
                    x0 = y0 = z0 = 0.f;
                place(cx, cz, th);
                port_player_set_pitch(walk_pitch);
                if (shot_one(out_dir, "walk") != 0)
                    goto done;
                h8 = g_last_fb_adler;
                r8 = port_prop_walk_rest_crc();
                f8 = port_prop_walk_frame();
                for (t = 0; t < nt; t++)
                    port_prop_tick_walk();
                if (port_prop_walk_xyz(&x1, &y1, &z1) != 0)
                    x1 = y1 = z1 = 0.f;
                place(cx, cz, th);
                port_player_set_pitch(walk_pitch);
                if (shot_one(out_dir, "walk20") != 0)
                    goto done;
                h20 = g_last_fb_adler;
                r20 = port_prop_walk_rest_crc();
                f20 = port_prop_walk_frame();
                printf("walk_move t0 local=%.1f,%.1f,%.1f t%d=%.1f,%.1f,%.1f dxz=%.1f speed=%.1f f=%d/%d %s\n",
                       (double)(x0 - r10[0]), (double)(y0 - r10[1]), (double)(z0 - r10[2]),
                       nt,
                       (double)(x1 - r10[0]), (double)(y1 - r10[1]), (double)(z1 - r10[2]),
                       (double)sqrtf((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0)),
                       (double)port_prop_walk_speed(), f8, f20,
                       (fabsf(x1 - x0) + fabsf(z1 - z0) > 8.f) ? "XZMOVE" : "xzstuck");
                printf("walk_cycle f=%d crc=%08x rest=%08x f=%d crc=%08x rest=%08x %s walker=%.1f,%.1f,%.1f cam=%.1f,%.1f th=%.1f ph=%.1f ir=%u spawn=%.1f,%.1f\n",
                       f8, h8, r8, f20, h20, r20, (h8 != h20) ? "FBDIFF" : "fbsame",
                       (double)(x1 - r10[0]), (double)(y1 - r10[1]), (double)(z1 - r10[2]),
                       (double)cx, (double)cz, (double)th,
                       (double)walk_pitch,
                       g1_last_ir() ? g1_last_ir()->ncmds : 0u,
                       (double)spawn_x, (double)spawn_z);
                {
                    float wx, wy, wz, ddx, ddz, dist;
                    int hp0, hp1, shots0, combat, pr, wr;
                    hp0 = port_api_health();
                    shots0 = port_prop_guard_shots();
                    place(cx, cz, th);
                    port_player_set_pitch(walk_pitch);
                    if (port_prop_walk_xyz(&wx, &wy, &wz) == 0) {
                        ddx = cx - (wx - r10[0]);
                        ddz = cz - (wz - r10[2]);
                        dist = sqrtf(ddx * ddx + ddz * ddz);
                        pr = port_stage_room_at_local(cx, port_api_player_y(), cz);
                        wr = port_stage_room_at_local(wx - r10[0], wy - r10[1], wz - r10[2]);
                    } else {
                        dist = -1.f;
                        pr = wr = 0;
                    }
                    {
                        int gi, ng = port_prop_guard_count();
                        int near_n = 0;
                        for (gi = 0; gi < ng; gi++) {
                            float gx, gy, gz, glx, gly, glz, ddx2, ddz2, d2;
                            int gr, dead;
                            if (port_prop_guard_xyz(gi, &gx, &gy, &gz) != 0)
                                continue;
                            glx = gx - r10[0];
                            gly = gy - r10[1];
                            glz = gz - r10[2];
                            ddx2 = cx - glx;
                            ddz2 = cz - glz;
                            d2 = sqrtf(ddx2 * ddx2 + ddz2 * ddz2);
                            if (d2 > 400.f || fabsf(ddz2) > 200.f)
                                continue;
                            gr = port_stage_room_at_local(glx, gly, glz);
                            dead = port_stan_guard_dead_at(gx, gz);
                            printf("near_guard[%d] local=%.1f,%.1f,%.1f dist=%.1f dz=%.1f room=%d dead=%d adj=%d walk=%d aim=%d\n",
                                   gi, (double)glx, (double)gly, (double)glz, (double)d2,
                                   (double)ddz2, gr, dead,
                                   port_stage_rooms_adjacent(pr, gr),
                                   port_prop_guard_walk_bound(gi),
                                   port_prop_guard_aim_bound(gi));
                            near_n++;
                        }
                        printf("near_guards n=%d walk_cam=%.1f,%.1f room_p=%d\n",
                               near_n, (double)cx, (double)cz, pr);
                    }
                    combat = port_prop_tick_guard_fire();
                    hp1 = port_api_health();
                    printf("return_fire combat=%d hp=%d->%d shots=%d->%d los=%d dist=%.1f room_p=%d room_w=%d %s\n",
                           combat, hp0, hp1, shots0, port_prop_guard_shots(),
                           port_prop_guard_los(),
                           (double)dist, pr, wr,
                           (port_prop_guard_los() > 1) ? "SECOND" :
                           (port_prop_guard_los() == 1) ? "ONE" : "NONE");
                    {
                        int gi2, ng2 = port_prop_guard_count(), ab = 0, wb = 0;
                        uint32_t ic = port_prop_idle_rest_crc();
                        uint32_t ac = port_prop_aim_rest_crc();
                        for (gi2 = 0; gi2 < ng2; gi2++) {
                            if (port_prop_guard_aim_bound(gi2))
                                ab++;
                            if (port_prop_guard_walk_bound(gi2))
                                wb++;
                        }
                        printf("aim_rest idle=%08x aim=%08x have=%d bound=%d walkers=%d %s\n",
                               (unsigned)ic, (unsigned)ac, port_prop_have_aim(), ab, wb,
                               (port_prop_have_aim() && ic != ac && ab > 0) ? "DIFF" :
                               (ic != ac) ? "DECODE" : "SAME");
                    }
                    if (shot_one(out_dir, "return") != 0)
                        goto done;
                    /* Player hitscan from this camera: body xz kills and
                     * hides; the t0 sit pad does not kill the walker. */
                    {
                        float padx = x0, pady = y0, padz = z0;
                        float bx, by, bz, kcx = cx, kcz = cz;
                        float pdx, pdz, plen;
                        int k0, k1, k2, drawn0, drawn1, drawn2;
                        int dead_pad, dead_body, nudged = 0;

                        if (port_prop_walk_xyz(&bx, &by, &bz) != 0)
                            bx = by = bz = 0.f;
                        /* If pad and body share the look ray, step south so
                         * aiming at the empty pad cannot clip the walker. */
                        pdx = (padx - r10[0]) - kcx;
                        pdz = (padz - r10[2]) - kcz;
                        plen = sqrtf(pdx * pdx + pdz * pdz);
                        if (plen > 1.f) {
                            float dbody = ray_xz_dist(kcx, kcz, pdx / plen, pdz / plen,
                                                      bx - r10[0], bz - r10[2]);
                            if (dbody <= PORT_GUARD_RADIUS + 8.f) {
                                kcz -= 80.f;
                                if (!port_stan_on_tile(kcx, kcz))
                                    kcz += 160.f;
                                nudged = 1;
                            }
                        }
                        port_api_draw();
                        drawn0 = port_prop_drawn();
                        k0 = port_api_kills();
                        aim_world(padx, pady, padz, kcx, kcz);
                        port_gun_tick(0);
                        port_gun_tick(PORT_Z_TRIG);
                        port_gun_tick(0);
                        k1 = port_api_kills();
                        dead_body = port_stan_guard_dead_at(bx, bz);
                        dead_pad = port_stan_guard_dead_at(padx, padz);
                        port_api_draw();
                        drawn1 = port_prop_drawn();
                        printf("walk_kill_pad kills=%d->%d dead_body=%d dead_pad=%d "
                               "drawn=%d->%d pad=%.1f,%.1f body=%.1f,%.1f cam=%.1f,%.1f "
                               "nudge=%d %s\n",
                               k0, k1, dead_body, dead_pad, drawn0, drawn1,
                               (double)(padx - r10[0]), (double)(padz - r10[2]),
                               (double)(bx - r10[0]), (double)(bz - r10[2]),
                               (double)kcx, (double)kcz, nudged,
                               (k1 == k0 && !dead_body) ? "PADMISS" : "padhit");
                        aim_world(bx, by, bz, kcx, kcz);
                        port_gun_tick(0);
                        port_gun_tick(PORT_Z_TRIG);
                        port_gun_tick(0);
                        k2 = port_api_kills();
                        dead_body = port_stan_guard_dead_at(bx, bz);
                        dead_pad = port_stan_guard_dead_at(padx, padz);
                        place(cx, cz, th);
                        port_player_set_pitch(walk_pitch);
                        {
                            float xk = bx, yk = by, zk = bz;
                            uint32_t r0, r1, h0, h1;
                            int f0, f1, lastf, ti, drawn_early;
                            int ticks_to_last = 0, ticks_need = 0;

                            /* Early rest (frame 0). Feet stay on the death tile. */
                            port_prop_set_die_frame(0);
                            if (shot_one(out_dir, "death0") != 0)
                                goto done;
                            drawn_early = port_prop_drawn();
                            r0 = port_prop_die_rest_crc();
                            f0 = port_prop_die_frame();
                            h0 = g_last_fb_adler;
                            /* 89-frame fall at PORT_DIE_FRAMES_PER_TICK (4):
                             * last=88 -> ceil(88/4)=22 ticks to last ~ 1.1s @ 20 Hz.
                             * +4 extra ticks prove last-frame hold. */
                            lastf = port_prop_die_last_frame();
                            if (lastf > 0)
                                ticks_need = (lastf + PORT_DIE_FRAMES_PER_TICK - 1) /
                                             PORT_DIE_FRAMES_PER_TICK;
                            for (ti = 0; ti < ticks_need + 4; ti++) {
                                port_prop_tick_die();
                                if (ticks_to_last == 0 &&
                                    port_prop_die_frame() >= lastf)
                                    ticks_to_last = ti + 1;
                            }
                            if (port_prop_walk_xyz(&bx, &by, &bz) != 0)
                                bx = by = bz = 0.f;
                            place(cx, cz, th);
                            port_player_set_pitch(walk_pitch);
                            /* Expire the kill bloom so death.png is the body. */
                            while (port_gun_flash_frames() > 0)
                                port_gun_tick(0);
                            if (shot_one(out_dir, "death") != 0)
                                goto done;
                            if (shot_one(out_dir, "dead") != 0)
                                goto done;
                            drawn2 = port_prop_drawn();
                            r1 = port_prop_die_rest_crc();
                            f1 = port_prop_die_frame();
                            h1 = g_last_fb_adler;
                            printf("death_cycle f=%d rest=%08x adler=%08x drawn=%d "
                                   "f=%d rest=%08x adler=%08x drawn=%d %s %s "
                                   "last=%d fpt=%d ticks=%d need=%d tick_ok "
                                   "dxz=%.2f dy=%.2f\n",
                                   f0, r0, h0, drawn_early, f1, r1, h1, drawn2,
                                   (r0 != r1) ? "RESTDIFF" : "restsame",
                                   (h0 != h1) ? "FBDIFF" : "fbsame", lastf,
                                   PORT_DIE_FRAMES_PER_TICK, ticks_to_last, ticks_need,
                                   (double)((bx - xk) * (bx - xk) + (bz - zk) * (bz - zk) > 0.f
                                                ? sqrtf((bx - xk) * (bx - xk) +
                                                        (bz - zk) * (bz - zk))
                                                : 0.f),
                                   (double)(by - yk));
                        }
                        drawn2 = port_prop_drawn();
                        printf("walk_kill_body kills=%d->%d dead_body=%d dead_pad=%d "
                               "drawn=%d->%d body=%.1f,%.1f hp=%d %s\n",
                               k1, k2, dead_body, dead_pad, drawn1, drawn2,
                               (double)(bx - r10[0]), (double)(bz - r10[2]),
                               port_api_health(),
                               (k2 == k1 + 1 && dead_body) ? "BODYKILL" : "bodymiss");
                    }
                }
            }
        }
    }
    /* After living-player cameras: drain hp, show DEAD, prove no fire.
     * Do not respawn; later ticks stay frozen. */
    {
        char path[512], line[512];
        FILE *sf;
        int spawn_hp8 = 0, spawn_dead = 0, mag0, mag1, dhp;
        int have_line = 0;

        snprintf(path, sizeof path, "%s/spawn.hud", out_dir);
        sf = fopen(path, "r");
        if (sf) {
            if (fgets(line, sizeof line, sf)) {
                have_line = 1;
                spawn_hp8 = strstr(line, "hp=8") != NULL;
                spawn_dead = strstr(line, "DEAD") != NULL;
            }
            fclose(sf);
        }
        printf("spawn_alive line=%d hp8=%d dead_token=%d %s\n", have_line,
               spawn_hp8, spawn_dead,
               (have_line && spawn_hp8 && !spawn_dead) ? "ALIVE" : "BADSPAWN");

        port_player_damage(PORT_PLAYER_HEALTH_MAX);
        dhp = port_api_health();
        if (shot_one(out_dir, "player_dead") != 0)
            goto done;
        mag0 = port_api_gun_mag();
        port_gun_tick(0);
        port_gun_tick(PORT_Z_TRIG);
        port_gun_tick(0);
        mag1 = port_api_gun_mag();
        snprintf(path, sizeof path, "%s/player_dead.hud", out_dir);
        have_line = 0;
        spawn_dead = 0;
        sf = fopen(path, "r");
        if (sf) {
            if (fgets(line, sizeof line, sf)) {
                have_line = 1;
                spawn_dead = strstr(line, "DEAD") != NULL;
            }
            fclose(sf);
        }
        printf("player_dead hp=%d mag=%d->%d hud=%d dead_token=%d %s %s\n", dhp,
               mag0, mag1, have_line, spawn_dead, dhp <= 0 ? "DEAD" : "alive",
               (dhp <= 0 && mag1 == mag0 && spawn_dead) ? "NOFIRE" : "STILLFIRE");
    }
    rc = 0;

done:
    port_api_shutdown();
    free(pack);
    return rc;
}
