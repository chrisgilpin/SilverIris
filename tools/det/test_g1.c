#include <stdio.h>
#include <string.h>

#include "gfx/gbi_interp.h"
#include "gfx/sw_raster.h"
#include "fs/sha256.h"

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main(int argc, char **argv)
{
    uint8_t digest[32];
    char hex[65];
    const uint8_t *fb;
    unsigned i, n = (unsigned)G1_FB_W * (unsigned)G1_FB_H;
    unsigned painted = 0;
    const GirList *ir;

    if (g1_run_synthetic() != 0)
        return fail("synthetic");
    ir = g1_last_ir();
    if (!ir || ir->ncmds < 3)
        return fail("ir cmds");

    fb = g1_fb_rgba();
    if (!fb)
        return fail("fb");
    for (i = 0; i < n; i++) {
        if (fb[i * 4] != fb[0] || fb[i * 4 + 1] != fb[1] || fb[i * 4 + 2] != fb[2])
            painted++;
    }
    if (painted < 1000)
        return fail("triangle too small");

    g1_fb_grey_sha256(digest);
    silveriris_sha256_hex(digest, hex);
    if (argc >= 2 && strcmp(hex, argv[1]) != 0) {
        fprintf(stderr, "grey hash got %s want %s\n", hex, argv[1]);
        return 1;
    }
    printf("g1 ok painted=%u grey_sha256=%s\n", painted, hex);

    /* Camera-space viewmodel: a red tri below center, in front of the eye.
     * Must paint the bottom-middle, not the top-left. Look pitch does not
     * swing it (N64 gun stays put). */
    {
        uint8_t blob[128];
        G1RoomDl room;
        unsigned bot = 0, top = 0;
        int x, y;
        /* BE Vtx at blob+32: three verts. G_VTX w1 = 0x05000020 */
        memset(blob, 0, sizeof blob);
        /* G_VTX n=3 v0=0 */
        blob[0] = 0x04;
        blob[1] = 0x20;
        blob[4] = 0x05;
        blob[7] = 32;
        /* G_TRI1 0,1,2 (*10) */
        blob[8] = 0xBF;
        blob[12] = 0x00;
        blob[13] = 0x00;
        blob[14] = 0x0A;
        blob[15] = 0x14;
        /* G_ENDDL F3D */
        blob[16] = 0xB8;
        /* verts: (-20,-8,-40), (20,-8,-40), (0,12,-40) red */
        {
            int16_t v[3][3] = { { -18, -32, -40 }, { 18, -32, -40 }, { 0, -8, -40 } };
            int vi;
            for (vi = 0; vi < 3; vi++) {
                uint8_t *q = blob + 32 + vi * 16;
                int16_t vx = v[vi][0], vy = v[vi][1], vz = v[vi][2];
                q[0] = (uint8_t)(vx >> 8);
                q[1] = (uint8_t)vx;
                q[2] = (uint8_t)(vy >> 8);
                q[3] = (uint8_t)vy;
                q[4] = (uint8_t)(vz >> 8);
                q[5] = (uint8_t)vz;
                q[12] = 220;
                q[13] = 40;
                q[14] = 40;
                q[15] = 255;
            }
        }
        memset(&room, 0, sizeof room);
        room.pri = blob;
        room.pri_n = 3;
        room.seg5 = (uintptr_t)blob;
        room.view = 1;
        g1_set_lookat(0.f, 0.f, 0.f, 0.f);
        g1_set_pitch(0.f);
        if (g1_interpret_rooms(&room, 1) != 0)
            return fail("viewgun geom");
        fb = g1_fb_rgba();
        for (y = 0; y < 40; y++) {
            for (x = 0; x < 40; x++) {
                const uint8_t *px = fb + ((y * G1_FB_W) + x) * 4;
                if (px[0] > 40)
                    top++;
            }
        }
        for (y = G1_FB_H - 80; y < G1_FB_H; y++) {
            for (x = G1_FB_W / 2 - 40; x < G1_FB_W / 2 + 40; x++) {
                const uint8_t *px = fb + ((y * G1_FB_W) + x) * 4;
                if (px[0] > 80)
                    bot++;
            }
        }
        {
            unsigned long sy0 = 0, n0 = 0, sy1 = 0, n1 = 0;
            /* Look pitch is not applied to camera-space viewmodels. */
            g1_set_pitch(0.f);
            if (g1_interpret_rooms(&room, 1) != 0)
                return fail("viewgun geom2");
            fb = g1_fb_rgba();
            for (y = 0; y < G1_FB_H; y++) {
                for (x = 0; x < G1_FB_W; x++) {
                    const uint8_t *px = fb + ((y * G1_FB_W) + x) * 4;
                    if (px[0] > 80) {
                        sy0 += (unsigned long)y;
                        n0++;
                    }
                }
            }
            g1_set_pitch(12.f);
            if (g1_interpret_rooms(&room, 1) != 0)
                return fail("viewgun pitch");
            fb = g1_fb_rgba();
            for (y = 0; y < G1_FB_H; y++) {
                for (x = 0; x < G1_FB_W; x++) {
                    const uint8_t *px = fb + ((y * G1_FB_W) + x) * 4;
                    if (px[0] > 80) {
                        sy1 += (unsigned long)y;
                        n1++;
                    }
                }
            }
            if (bot < 80)
                return fail("viewgun not in bottom-center");
            if (top > 10)
                return fail("viewgun painted top-left");
            if (!n0 || !n1)
                return fail("viewgun pitch empty");
            if (sy1 / n1 != sy0 / n0)
                return fail("viewgun pitch swung with look");
            printf("g1 viewgun geom bot=%u top=%u mean_y %lu -> %lu\n", bot, top,
                   sy0 / n0, sy1 / n1);
        }
        g1_clear_lookat();
    }

    /* Hor+: 16:9 aspect (same vfov) pulls a right-side camera-space tri
     * toward center vs 4:3. Stretching 4:3 into 16:9 is not this. */
    {
        uint8_t blob[128];
        G1RoomDl room;
        unsigned long sx43 = 0, n43 = 0, sx169 = 0, n169 = 0;
        int x, y;
        memset(blob, 0, sizeof blob);
        blob[0] = 0x04;
        blob[1] = 0x20;
        blob[4] = 0x05;
        blob[7] = 32;
        blob[8] = 0xBF;
        blob[12] = 0x00;
        blob[13] = 0x00;
        blob[14] = 0x0A;
        blob[15] = 0x14;
        blob[16] = 0xB8;
        {
            int16_t v[3][3] = { { 18, -6, -40 }, { 28, -6, -40 }, { 23, 6, -40 } };
            int vi;
            for (vi = 0; vi < 3; vi++) {
                uint8_t *q = blob + 32 + vi * 16;
                int16_t vx = v[vi][0], vy = v[vi][1], vz = v[vi][2];
                q[0] = (uint8_t)(vx >> 8);
                q[1] = (uint8_t)vx;
                q[2] = (uint8_t)(vy >> 8);
                q[3] = (uint8_t)vy;
                q[4] = (uint8_t)(vz >> 8);
                q[5] = (uint8_t)vz;
                q[12] = 40;
                q[13] = 220;
                q[14] = 40;
                q[15] = 255;
            }
        }
        memset(&room, 0, sizeof room);
        room.pri = blob;
        room.pri_n = 3;
        room.seg5 = (uintptr_t)blob;
        room.view = 1;
        g1_set_lookat(0.f, 0.f, 0.f, 0.f);
        g1_set_pitch(0.f);
        g1_set_perspective(60.f, 320.f / 240.f);
        if (g1_interpret_rooms(&room, 1) != 0)
            return fail("hor+ 4:3");
        fb = g1_fb_rgba();
        for (y = 0; y < G1_FB_H; y++) {
            for (x = 0; x < G1_FB_W; x++) {
                const uint8_t *px = fb + ((y * G1_FB_W) + x) * 4;
                if (px[1] > 80) {
                    sx43 += (unsigned long)x;
                    n43++;
                }
            }
        }
        g1_set_perspective(60.f, 16.f / 9.f);
        if (g1_interpret_rooms(&room, 1) != 0)
            return fail("hor+ 16:9");
        fb = g1_fb_rgba();
        for (y = 0; y < G1_FB_H; y++) {
            for (x = 0; x < G1_FB_W; x++) {
                const uint8_t *px = fb + ((y * G1_FB_W) + x) * 4;
                if (px[1] > 80) {
                    sx169 += (unsigned long)x;
                    n169++;
                }
            }
        }
        g1_set_perspective(60.f, 320.f / 240.f);
        g1_clear_lookat();
        if (!n43 || !n169)
            return fail("hor+ empty");
        if (sx169 / n169 >= sx43 / n43)
            return fail("hor+ did not widen (right tri should move toward center)");
        printf("g1 hor+ mean_x %lu -> %lu (n %lu -> %lu)\n", sx43 / n43, sx169 / n169, n43,
               n169);
    }
    return 0;
}
