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
#include "audio/audio.h"
#include "gfx/tmem.h"
#include "gfx/gbi_interp.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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
static void door_tick_n(int n);
static void place(float x, float z, float th);

static int sfx_bank_proof(void)
{
    int16_t gun[512], dry[512], door[512];
    int i, gn, dn, on, fn, hn, kn, pn, cn, rn, an, vn, ln;
    long long eg = 0, ed = 0, eo = 0;

    gn = port_audio_sfx_frames(PORT_SFX_GUN);
    dn = port_audio_sfx_frames(PORT_SFX_DRY);
    on = port_audio_sfx_frames(PORT_SFX_DOOR);
    fn = port_audio_sfx_frames(PORT_SFX_FALL);
    hn = port_audio_sfx_frames(PORT_SFX_HIT);
    kn = port_audio_sfx_frames(PORT_SFX_KF7);
    pn = port_audio_sfx_frames(PORT_SFX_PICKUP);
    cn = port_audio_sfx_frames(PORT_SFX_DOOR_CLOSE);
    rn = port_audio_sfx_frames(PORT_SFX_RICO);
    an = port_audio_sfx_frames(PORT_SFX_AMMO);
    vn = port_audio_sfx_frames(PORT_SFX_ARMOUR);
    ln = port_audio_sfx_frames(PORT_SFX_RELOAD);
    printf("sfx_bank ready=%d gun_n=%d dry_n=%d door_n=%d fall_n=%d hit_n=%d "
           "kf7_n=%d pickup_n=%d close_n=%d rico_n=%d ammo_n=%d armour_n=%d "
           "reload_n=%d\n",
           port_audio_bank_ready(), gn, dn, on, fn, hn, kn, pn, cn, rn, an, vn, ln);
    if (!port_audio_bank_ready() || !port_audio_sfx_from_bank(PORT_SFX_GUN) ||
        !port_audio_sfx_from_bank(PORT_SFX_DRY) ||
        !port_audio_sfx_from_bank(PORT_SFX_DOOR) ||
        !port_audio_sfx_from_bank(PORT_SFX_FALL) ||
        !port_audio_sfx_from_bank(PORT_SFX_HIT) ||
        !port_audio_sfx_from_bank(PORT_SFX_KF7) ||
        !port_audio_sfx_from_bank(PORT_SFX_PICKUP) ||
        !port_audio_sfx_from_bank(PORT_SFX_DOOR_CLOSE) ||
        !port_audio_sfx_from_bank(PORT_SFX_RICO) ||
        !port_audio_sfx_from_bank(PORT_SFX_AMMO) ||
        !port_audio_sfx_from_bank(PORT_SFX_ARMOUR) ||
        !port_audio_sfx_from_bank(PORT_SFX_RELOAD) || gn < 2000 || dn < 500 ||
        on < 1000 || fn < 500 || hn < 200 || kn < 500 || pn < 200 || cn < 500 ||
        rn < 200 || an < 200 || vn < 200 || ln < 200 || gn <= dn) {
        fprintf(stderr,
                "sfx_bank missing pack VADPCM gun=%d dry=%d door=%d fall=%d hit=%d "
                "kf7=%d pickup=%d close=%d rico=%d ammo=%d armour=%d reload=%d\n",
                gn, dn, on, fn, hn, kn, pn, cn, rn, an, vn, ln);
        return -1;
    }
    memset(gun, 0, sizeof gun);
    memset(dry, 0, sizeof dry);
    memset(door, 0, sizeof door);
    port_audio_play_gun();
    port_audio_cb(gun, 256);
    port_audio_play_dry();
    port_audio_cb(dry, 256);
    port_audio_play_door();
    port_audio_cb(door, 256);
    for (i = 0; i < 512; i++) {
        eg += (long long)gun[i] * gun[i];
        ed += (long long)dry[i] * dry[i];
        eo += (long long)door[i] * door[i];
    }
    printf("sfx_pcm gun_e=%lld dry_e=%lld door_e=%lld last=%d\n", eg, ed, eo,
           port_audio_last_sfx());
    if (eg < 1000000ll || ed < 100000ll || eo < 100000ll || eg == ed || eg == eo ||
        ed == eo) {
        fprintf(stderr, "sfx_pcm not distinct pack one-shots\n");
        return -1;
    }
    {
        int16_t kf7[512], pickup[512];
        long long ek = 0, ep = 0;
        int diffk = 0, diffp = 0;
        memset(kf7, 0, sizeof kf7);
        memset(pickup, 0, sizeof pickup);
        port_audio_play_kf7();
        port_audio_cb(kf7, 256);
        port_audio_play_pickup();
        port_audio_cb(pickup, 256);
        for (i = 0; i < 512; i++) {
            ek += (long long)kf7[i] * kf7[i];
            ep += (long long)pickup[i] * pickup[i];
            if (kf7[i] != gun[i])
                diffk++;
            if (pickup[i] != gun[i])
                diffp++;
        }
        printf("sfx_kf7 n=%d e=%lld last=%d mix_diff=%d\n", kn, ek,
               port_audio_last_sfx(), diffk);
        /* last is pickup: played after kf7. */
        printf("sfx_pickup n=%d e=%lld last=%d mix_diff=%d\n", pn, ep,
               port_audio_last_sfx(), diffp);
        if (port_audio_last_sfx() != PORT_SFX_PICKUP || ek < 100000ll ||
            ep < 10000ll || diffk < 16 || diffp < 16 || ek == eg) {
            fprintf(stderr,
                    "sfx_kf7/pickup not distinct pack one-shots last=%d ek=%lld "
                    "ep=%lld diffk=%d diffp=%d\n",
                    port_audio_last_sfx(), ek, ep, diffk, diffp);
            return -1;
        }
        port_audio_play_kf7();
        if (port_audio_last_sfx() != PORT_SFX_KF7) {
            fprintf(stderr, "sfx_kf7 last=%d\n", port_audio_last_sfx());
            return -1;
        }
        {
            int16_t closeb[512];
            long long ec = 0;
            int diffc = 0;
            memset(closeb, 0, sizeof closeb);
            port_audio_play_door();
            port_audio_cb(gun, 256); /* reuse as open snapshot */
            port_audio_play_door_close();
            port_audio_cb(closeb, 256);
            for (i = 0; i < 512; i++) {
                ec += (long long)closeb[i] * closeb[i];
                if (closeb[i] != gun[i])
                    diffc++;
            }
            printf("sfx_door_close n=%d e=%lld last=%d mix_diff=%d\n", cn, ec,
                   port_audio_last_sfx(), diffc);
            if (port_audio_last_sfx() != PORT_SFX_DOOR_CLOSE || ec < 100000ll ||
                diffc < 16) {
                fprintf(stderr, "sfx_door_close last=%d e=%lld diff=%d\n",
                        port_audio_last_sfx(), ec, diffc);
                return -1;
            }
        }
    }
    {
        int16_t fall[512];
        long long ef = 0;
        memset(fall, 0, sizeof fall);
        port_audio_play_gun();
        port_audio_play_fall();
        port_audio_cb(fall, 256);
        for (i = 0; i < 512; i++)
            ef += (long long)fall[i] * fall[i];
        {
            int diff = 0;
            for (i = 0; i < 512; i++) {
                if (fall[i] != gun[i])
                    diff++;
            }
            printf("sfx_fall n=%d e=%lld last=%d mix_diff=%d\n", fn, ef,
                   port_audio_last_sfx(), diff);
            if (port_audio_last_sfx() != PORT_SFX_FALL || ef < 1000000ll || diff < 16) {
                fprintf(stderr,
                        "sfx_fall did not overlay gun last=%d e=%lld diff=%d\n",
                        port_audio_last_sfx(), ef, diff);
                return -1;
            }
        }
    }
    {
        int16_t hit[512];
        long long eh = 0;
        int diff = 0;
        memset(hit, 0, sizeof hit);
        port_audio_play_gun();
        port_audio_play_hit();
        port_audio_cb(hit, 256);
        for (i = 0; i < 512; i++) {
            eh += (long long)hit[i] * hit[i];
            if (hit[i] != gun[i])
                diff++;
        }
        printf("sfx_hit n=%d e=%lld last=%d mix_diff=%d\n", hn, eh,
               port_audio_last_sfx(), diff);
        if (port_audio_last_sfx() != PORT_SFX_HIT || eh < 100000ll || diff < 16) {
            fprintf(stderr, "sfx_hit did not overlay gun last=%d e=%lld diff=%d\n",
                    port_audio_last_sfx(), eh, diff);
            return -1;
        }
    }
    {
        int16_t rico[512];
        long long er = 0;
        int diff = 0;
        memset(rico, 0, sizeof rico);
        port_audio_play_gun();
        port_audio_play_rico();
        port_audio_cb(rico, 256);
        for (i = 0; i < 512; i++) {
            er += (long long)rico[i] * rico[i];
            if (rico[i] != gun[i])
                diff++;
        }
        printf("sfx_rico n=%d e=%lld last=%d mix_diff=%d\n", rn, er,
               port_audio_last_sfx(), diff);
        if (port_audio_last_sfx() != PORT_SFX_RICO || er < 100000ll || diff < 16) {
            fprintf(stderr, "sfx_rico did not overlay gun last=%d e=%lld diff=%d\n",
                    port_audio_last_sfx(), er, diff);
            return -1;
        }
    }
    {
        int16_t ammo[512], pickup[512];
        long long ea = 0;
        int diff = 0;
        memset(ammo, 0, sizeof ammo);
        memset(pickup, 0, sizeof pickup);
        port_audio_play_pickup();
        port_audio_cb(pickup, 256);
        port_audio_play_ammo();
        port_audio_cb(ammo, 256);
        for (i = 0; i < 512; i++) {
            ea += (long long)ammo[i] * ammo[i];
            if (ammo[i] != pickup[i])
                diff++;
        }
        printf("sfx_ammo n=%d e=%lld last=%d mix_diff=%d\n", an, ea,
               port_audio_last_sfx(), diff);
        if (port_audio_last_sfx() != PORT_SFX_AMMO || ea < 10000ll || diff < 16) {
            fprintf(stderr, "sfx_ammo last=%d e=%lld diff=%d\n",
                    port_audio_last_sfx(), ea, diff);
            return -1;
        }
    }
    {
        int16_t armour[512], pickup[512];
        long long ev = 0;
        int diff = 0;
        memset(armour, 0, sizeof armour);
        memset(pickup, 0, sizeof pickup);
        port_audio_play_pickup();
        port_audio_cb(pickup, 256);
        port_audio_play_armour();
        port_audio_cb(armour, 256);
        for (i = 0; i < 512; i++) {
            ev += (long long)armour[i] * armour[i];
            if (armour[i] != pickup[i])
                diff++;
        }
        printf("sfx_armour n=%d e=%lld last=%d mix_diff=%d\n", vn, ev,
               port_audio_last_sfx(), diff);
        if (port_audio_last_sfx() != PORT_SFX_ARMOUR || ev < 10000ll || diff < 16) {
            fprintf(stderr, "sfx_armour last=%d e=%lld diff=%d\n",
                    port_audio_last_sfx(), ev, diff);
            return -1;
        }
    }
    {
        int16_t reload[512], pickup[512];
        long long el = 0;
        int diff = 0;
        memset(reload, 0, sizeof reload);
        memset(pickup, 0, sizeof pickup);
        port_audio_play_pickup();
        port_audio_cb(pickup, 256);
        port_audio_play_reload();
        port_audio_cb(reload, 256);
        for (i = 0; i < 512; i++) {
            el += (long long)reload[i] * reload[i];
            if (reload[i] != pickup[i])
                diff++;
        }
        printf("sfx_reload n=%d e=%lld last=%d mix_diff=%d\n", ln, el,
               port_audio_last_sfx(), diff);
        if (port_audio_last_sfx() != PORT_SFX_RELOAD || el < 10000ll || diff < 16) {
            fprintf(stderr, "sfx_reload last=%d e=%lld diff=%d\n",
                    port_audio_last_sfx(), el, diff);
            return -1;
        }
    }
    return 0;
}

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

/* Stand just outside the fitted slab. Hard 120 sat inside scaled
 * ~132-half doors once portal geom matched G1/stan. +24 clears the
 * 15u slab thickness and keeps a side-stand inside Z-use 200. */
static float door_stand_dist(float width)
{
    float d = 0.5f * width + 24.f;
    if (d < 80.f)
        d = 80.f;
    return d;
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
                {
                    float stand = door_stand_dist(width);
                    sx = ox0 - slx * stand;
                    sz = oz0 - slz * stand;
                    if (!port_stan_on_tile(sx, sz)) {
                        sx = ox0 + slx * stand;
                        sz = oz0 + slz * stand;
                    }
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
    {
        float stand = door_stand_dist(width);
        px = ox - lx * stand;
        pz = oz - lz * stand;
        if (!port_stan_on_tile(px, pz)) {
            px = ox + lx * stand;
            pz = oz + lz * stand;
            lx = -lx;
            lz = -lz;
        }
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

    /* Dest is the far tile, not the portal seam (tile-less after *inv). */
    nx = ox + lx * 40.f;
    nz = oz + lz * 40.f;
    if (!port_stan_on_tile(nx, nz)) {
        nx = ox + lx * 80.f;
        nz = oz + lz * 80.f;
    }
    ny = y;
    port_stan_clip_step(px, pz, &nx, &nz, &ny);
    ax = (nx - ox) * lx + (nz - oz) * lz;
    closed_block = (ax < 8.f) || port_stan_door_is_open_at(pos[0], pos[2]) == 0;
    port_api_draw();
    ad_closed = adler32(port_api_fb(),
                        (size_t)port_api_fb_width() * (size_t)port_api_fb_height() * 4u);

    /* P0-B: fire (Z) must not unlatch; A must. */
    {
        int mag0 = port_gun_mag();
        int flash0;
        int open0 = port_stan_door_is_open_at(pos[0], pos[2]);
        port_api_set_pad(0, 0, 0, 0);
        if (port_api_sim_tick(4998) != 0)
            return -1;
        port_api_set_pad(0, 0, 0, (int)PORT_Z_TRIG);
        if (port_api_sim_tick(4999) != 0)
            return -1;
        flash0 = port_gun_flash_frames();
        printf("pad_fire_no_unlatch mag=%d->%d flash=%d open=%d->%d act=%d sfx=%d\n",
               mag0, port_gun_mag(), flash0, open0,
               port_stan_door_is_open_at(pos[0], pos[2]), port_gun_last_action(),
               port_audio_last_sfx());
        if (port_stan_door_is_open_at(pos[0], pos[2]) != open0) {
            fprintf(stderr, "pad fire unlatched the door\n");
            return -1;
        }
        if (port_gun_mag() != mag0 - 1 || flash0 <= 0) {
            fprintf(stderr, "pad fire facing door did not shoot mag %d->%d flash=%d\n",
                    mag0, port_gun_mag(), flash0);
            return -1;
        }
        if (port_gun_weapon() == PORT_WEAPON_PP7 &&
            port_audio_last_sfx() != PORT_SFX_GUN) {
            fprintf(stderr, "pad PP7 fire sfx=%d (want gun)\n", port_audio_last_sfx());
            return -1;
        }
        /* Door slab is a wall hit: rico overlays gun on the hit channel.
         * last_sfx stays GUN (play_gun after hitscan) so fire≠use holds. */
        {
            int16_t mix[512];
            int i;
            long long em = 0;
            memset(mix, 0, sizeof mix);
            port_audio_cb(mix, 256);
            for (i = 0; i < 512; i++)
                em += (long long)mix[i] * mix[i];
            printf("sfx_rico_wall e=%lld last=%d\n", em, port_audio_last_sfx());
            if (em < 100000ll || port_audio_last_sfx() != PORT_SFX_GUN) {
                fprintf(stderr, "sfx_rico_wall silent or cut gun e=%lld last=%d\n",
                        em, port_audio_last_sfx());
                return -1;
            }
        }
        while (port_gun_flash_frames() > 0) {
            port_api_set_pad(0, 0, 0, 0);
            if (port_api_sim_tick(5000) != 0)
                return -1;
        }
        mag0 = port_gun_mag();
        port_api_set_pad(0, 0, 0, 0);
        if (port_api_sim_tick(5001) != 0)
            return -1;
        port_api_set_pad(0, 0, 0, (int)PORT_A_BUTTON);
        if (port_api_sim_tick(5002) != 0)
            return -1;
        printf("pad_use_no_fire mag=%d->%d flash=%d open=%d sfx=%d\n", mag0,
               port_gun_mag(), port_gun_flash_frames(),
               port_stan_door_is_open_at(pos[0], pos[2]), port_audio_last_sfx());
        if (port_gun_mag() != mag0) {
            fprintf(stderr, "pad use spent mag\n");
            return -1;
        }
        if (port_gun_flash_frames() != 0) {
            fprintf(stderr, "pad use flashed muzzle\n");
            return -1;
        }
        if (port_audio_last_sfx() != PORT_SFX_DOOR) {
            fprintf(stderr, "pad use sfx=%d (want door open)\n", port_audio_last_sfx());
            return -1;
        }
        mag0 = port_gun_mag();
        port_api_set_pad(0, 0, 0, 0);
        if (port_api_sim_tick(5005) != 0)
            return -1;
        port_api_set_pad(0, 0, 0, (int)PORT_A_BUTTON);
        if (port_api_sim_tick(5006) != 0)
            return -1;
        printf("pad_close_no_fire mag=%d->%d flash=%d open=%d sfx=%d\n", mag0,
               port_gun_mag(), port_gun_flash_frames(),
               port_stan_door_is_open_at(pos[0], pos[2]), port_audio_last_sfx());
        if (port_gun_mag() != mag0) {
            fprintf(stderr, "pad close spent mag\n");
            return -1;
        }
        if (port_stan_door_is_open_at(pos[0], pos[2])) {
            fprintf(stderr, "pad close did not shut the door\n");
            return -1;
        }
        if (port_audio_last_sfx() != PORT_SFX_DOOR_CLOSE) {
            fprintf(stderr, "pad close sfx=%d (want door close)\n",
                    port_audio_last_sfx());
            return -1;
        }
    }

    used = port_stan_use_door(px, pz, lx, lz);
    if (!used)
        used = port_stan_use_door(px + r1[0], pz + r1[2], lx, lz);
    opened = port_stan_door_is_open_at(pos[0], pos[2]);
    if (!opened) {
        port_api_set_pad(0, 0, 0, 0);
        if (port_api_sim_tick(5003) != 0)
            return -1;
        port_api_set_pad(0, 0, 0, (int)PORT_A_BUTTON);
        if (port_api_sim_tick(5004) != 0)
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
    nx = ox + lx * 40.f;
    nz = oz + lz * 40.f;
    if (!port_stan_on_tile(nx, nz)) {
        nx = ox + lx * 80.f;
        nz = oz + lz * 80.f;
    }
    ny = y;
    port_stan_clip_step(px, pz, &nx, &nz, &ny);
    ax = (nx - ox) * lx + (nz - oz) * lz;
    open_pass = (ax > 8.f);
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
        door_tick_n(PORT_DOOR_OPEN_TICKS);

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

static void door_tick_n(int n)
{
    int tck;
    for (tck = 0; tck < n; tck++)
        port_stan_tick_doors();
}

/* Open a documented path door (prefer r8-r7), then Z-close: after a
 * few ticks frac is mid and collision is still off; after 6 close
 * ticks frac=0, pose is closed, collision on. No auto-close timer. */
static int path_close_swing_proof(void)
{
    float r1[3], pos[3], yaw = 0.f, width = 0.f;
    float ox, oz, lx, lz, px, pz, y = 86.8f, th;
    float nx, nz, ny, ax, az;
    float frac_mid = -1.f, frac_end = -1.f, pdx = 0.f, pdz = 0.f, pyaw = 0.f;
    int i, no, ra = 0, rb = 0, found = 0, used, opened;
    int mid_pass = 0, closed_block = 0, pose_closed = 0, still_shut = 0;
    static const int pick[][2] = {{8, 7}, {7, 8}};
    int p;

    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    no = port_stage_opening_count();
    for (p = 0; p < 2 && !found; p++) {
        for (i = 0; i < no; i++) {
            if (port_stage_opening(i, pos, &yaw, &width, &ra, &rb) != 0)
                continue;
            if (ra != pick[p][0] || rb != pick[p][1])
                continue;
            found = 1;
            break;
        }
    }
    if (!found) {
        printf("path_close_swing NONE (no r8-r7)\n");
        fprintf(stderr, "path_close_swing no r8-r7 portal\n");
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
    {
        float stand = door_stand_dist(width);
        px = ox - lx * stand;
        pz = oz - lz * stand;
        if (!port_stan_on_tile(px, pz)) {
            px = ox + lx * stand;
            pz = oz + lz * stand;
            lx = -lx;
            lz = -lz;
        }
    }
    if (!port_stan_on_tile(px, pz)) {
        printf("path_close_swing r%d-r%d off-tile\n", ra, rb);
        fprintf(stderr, "path_close_swing stand off-tile\n");
        return -1;
    }
    if (port_stan_eye_y(px, pz, &y) != 0)
        y = 86.8f;
    th = atan2f(lx, -lz) * (180.f / 3.14159265f);
    if (th < 0.f)
        th += 360.f;
    port_player_set_pose(px, y, pz, th);
    port_player_set_pitch(0.f);

    /* Park open first so close has a full 6-tick reverse. */
    used = port_stan_use_door(px, pz, lx, lz);
    if (!used)
        used = port_stan_use_door(px + r1[0], pz + r1[2], lx, lz);
    opened = port_stan_door_is_open_at(pos[0], pos[2]);
    if (!opened) {
        port_api_set_pad(0, 0, 0, 0);
        if (port_api_sim_tick(5300) != 0)
            return -1;
        port_api_set_pad(0, 0, 0, (int)PORT_A_BUTTON);
        if (port_api_sim_tick(5301) != 0)
            return -1;
        opened = port_stan_door_is_open_at(pos[0], pos[2]);
        if (opened)
            used = 1;
    }
    if (!opened) {
        printf("path_close_swing r%d-r%d did not open\n", ra, rb);
        fprintf(stderr, "path_close_swing did not open\n");
        return -1;
    }
    door_tick_n(PORT_DOOR_OPEN_TICKS);

    /* Z-close (or the existing close path). */
    (void)port_stan_use_door(px, pz, lx, lz);
    if (port_stan_door_is_open_at(pos[0], pos[2])) {
        fprintf(stderr, "path_close_swing did not close\n");
        return -1;
    }

    door_tick_n(3);
    frac_mid = port_stan_door_frac_at(pos[0], pos[2]);
    port_player_set_pose(px, y, pz, th);
    nx = ox + lx * 40.f;
    nz = oz + lz * 40.f;
    if (!port_stan_on_tile(nx, nz)) {
        nx = ox + lx * 80.f;
        nz = oz + lz * 80.f;
    }
    ny = y;
    port_stan_clip_step(px, pz, &nx, &nz, &ny);
    ax = (nx - ox) * lx + (nz - oz) * lz;
    mid_pass = (frac_mid > 0.15f && frac_mid < 0.99f) && (ax > 8.f);

    door_tick_n(PORT_DOOR_OPEN_TICKS - 3);
    frac_end = port_stan_door_frac_at(pos[0], pos[2]);
    (void)port_prop_door_park_offset(pos[0], pos[2], yaw, &pdx, &pdz, &pyaw);
    port_player_set_pose(px, y, pz, th);
    nx = ox + lx * 40.f;
    nz = oz + lz * 40.f;
    if (!port_stan_on_tile(nx, nz)) {
        nx = ox + lx * 80.f;
        nz = oz + lz * 80.f;
    }
    ny = y;
    port_stan_clip_step(px, pz, &nx, &nz, &ny);
    ax = (nx - ox) * lx + (nz - oz) * lz;
    closed_block = (frac_end <= 0.01f) && (ax < 8.f);
    pose_closed = (pdx * pdx + pdz * pdz < 16.f) && (fabsf(pyaw) < 2.f);

    /* Extra ticks must not auto-reopen. */
    door_tick_n(PORT_DOOR_OPEN_TICKS);
    still_shut = (port_stan_door_frac_at(pos[0], pos[2]) <= 0.01f) &&
                 !port_stan_door_is_open_at(pos[0], pos[2]);

    printf("path_close_swing r%d-r%d stand=%.1f,%.1f used=%d mid_frac=%.2f "
           "mid_pass=%d end_frac=%.2f closed_block=%d pose=%.1f,%.1f "
           "yaw=%.1f still_shut=%d spawn_adler=%08x %s\n",
           ra, rb, (double)px, (double)pz, used, (double)frac_mid, mid_pass,
           (double)frac_end, closed_block, (double)pdx, (double)pdz,
           (double)pyaw, still_shut, g_spawn_fb_adler,
           (mid_pass && closed_block && pose_closed && still_shut) ? "OK"
                                                                   : "FAIL");
    if (!mid_pass) {
        fprintf(stderr, "path_close_swing mid frac=%g not walkable\n",
                (double)frac_mid);
        return -1;
    }
    if (!closed_block || !pose_closed) {
        fprintf(stderr, "path_close_swing end frac=%g park=%.1f,%.1f yaw=%.1f\n",
                (double)frac_end, (double)pdx, (double)pdz, (double)pyaw);
        return -1;
    }
    if (!still_shut) {
        fprintf(stderr, "path_close_swing auto-moved after close\n");
        return -1;
    }
    return 0;
}

/* Walker on one side of a documented closed path door (prefer r8-r7):
 * clip_step_ground and live chase both stop while closed; after the
 * door is open (auto-unlatch or Z) the next chase step passes. Restore
 * the walker so spawn-room SAFE / walk shots stay put. */
static int chase_door_proof(void)
{
    float r1[3], pos[3], yaw = 0.f, width = 0.f;
    float ox, oz, lx, lz, wx, wz, px, pz, y = 86.8f, th;
    float save_x = 0.f, save_z = 0.f;
    float gx0 = 0.f, gz0 = 0.f, gx1 = 0.f, gz1 = 0.f, gx2 = 0.f, gz2 = 0.f;
    float nx, nz, ny, ax, az, wside, side;
    int i, no, ra = 0, rb = 0, found = 0, have_save = 0;
    int closed_block = 0, opened = 0, passed = 0, used = 0;
    int crossed_closed = 0, auto_unlatch = 0, placed;
    int tck;
    static const int pick[][2] = {{8, 7}, {7, 8}, {71, 7}, {7, 71}};
    int p;

    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    if (port_prop_walk_xz(&save_x, &save_z) == 0) {
        save_x -= r1[0];
        save_z -= r1[2];
        have_save = 1;
    }
    no = port_stage_opening_count();
    for (p = 0; p < 4 && !found; p++) {
        for (i = 0; i < no; i++) {
            if (port_stage_opening(i, pos, &yaw, &width, &ra, &rb) != 0)
                continue;
            if (ra != pick[p][0] || rb != pick[p][1])
                continue;
            found = 1;
            break;
        }
    }
    if (!found) {
        printf("chase_door NONE (no r8-r7 / r71-r7)\n");
        fprintf(stderr, "chase_door no documented path door\n");
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
    {
        float stand = door_stand_dist(width);
        wx = ox - lx * stand;
        wz = oz - lz * stand;
        if (!port_stan_on_tile(wx, wz)) {
            wx = ox + lx * stand;
            wz = oz + lz * stand;
            lx = -lx;
            lz = -lz;
        }
    }
    /* Player far enough that |dz| stays >200 even when the walker
     * reaches the slab — otherwise they enter LOS and stop chasing. */
    {
        static const float pdist[] = { 260.f, 240.f, 220.f, 200.f, 180.f, 160.f, 120.f };
        int k, got_p = 0;
        for (k = 0; k < (int)(sizeof pdist / sizeof pdist[0]); k++) {
            float cand_x = ox + lx * pdist[k];
            float cand_z = oz + lz * pdist[k];
            if (!port_stan_on_tile(cand_x, cand_z))
                continue;
            px = cand_x;
            pz = cand_z;
            got_p = 1;
            if (fabsf(pz - wz) > 200.f)
                break;
        }
        if (!got_p || !port_stan_on_tile(wx, wz)) {
            printf("chase_door r%d-r%d off-tile\n", ra, rb);
            fprintf(stderr, "chase_door stand off-tile\n");
            return -1;
        }
    }
    if (port_stan_door_is_open_at(pos[0], pos[2])) {
        (void)port_stan_use_door(wx, wz, lx, lz);
        door_tick_n(PORT_DOOR_OPEN_TICKS);
    }

    placed = port_prop_place_walker_at(wx, wz);
    if (!placed) {
        printf("chase_door r%d-r%d no walker\n", ra, rb);
        fprintf(stderr, "chase_door place walker failed\n");
        return -1;
    }
    if (port_stan_eye_y(px, pz, &y) != 0)
        y = 86.8f;
    /* From the player side, look toward the door (-look). */
    th = atan2f(-lx, lz) * (180.f / 3.14159265f);
    if (th < 0.f)
        th += 360.f;
    port_player_set_pose(px, y, pz, th);
    port_player_set_pitch(0.f);

    /* clip_step_ground from walker toward the portal: closed must block. */
    nx = ox;
    nz = oz;
    ny = y;
    port_stan_clip_step_ground(wx, wz, &nx, &nz, &ny);
    ax = nx - ox;
    az = nz - oz;
    closed_block = (ax * ax + az * az > 25.f) &&
                   !port_stan_door_is_open_at(pos[0], pos[2]);

    wside = (wx - ox) * lx + (wz - oz) * lz;
    (void)port_prop_alert_walker();
    if (port_prop_walk_xz(&gx0, &gz0) == 0) {
        gx0 -= r1[0];
        gz0 -= r1[2];
    }

    for (tck = 0; tck < 80; tck++) {
        int was_open = port_stan_door_is_open_at(pos[0], pos[2]);
        (void)port_prop_tick_guard_fire();
        if (port_prop_walk_xz(&gx1, &gz1) == 0) {
            gx1 -= r1[0];
            gz1 -= r1[2];
            side = (gx1 - ox) * lx + (gz1 - oz) * lz;
            /* Same-tick unlatch drops collision so the retry sit can
             * pass. Only a still-closed cross is a leak. */
            if (!was_open && !port_stan_door_is_open_at(pos[0], pos[2]) &&
                (wside * side < 0.f) && fabsf(side) > 15.f)
                crossed_closed = 1;
        }
        if (port_stan_door_is_open_at(pos[0], pos[2])) {
            opened = 1;
            used = 1;
            auto_unlatch = 1;
            break;
        }
    }
    if (!opened) {
        used = port_stan_use_door(px, pz, -lx, -lz);
        if (!used)
            used = port_stan_use_door(wx, wz, lx, lz);
        opened = port_stan_door_is_open_at(pos[0], pos[2]);
    }
    door_tick_n(PORT_DOOR_OPEN_TICKS);
    for (tck = 0; tck < 80; tck++) {
        (void)port_prop_tick_guard_fire();
        if (port_prop_walk_xz(&gx2, &gz2) == 0) {
            gx2 -= r1[0];
            gz2 -= r1[2];
            side = (gx2 - ox) * lx + (gz2 - oz) * lz;
            if ((wside * side < 0.f) && fabsf(side) > 15.f) {
                passed = 1;
                break;
            }
        }
    }

    printf("chase_door r%d-r%d walk=%.1f,%.1f player=%.1f,%.1f "
           "clip_closed=%d auto=%d opened=%d used=%d crossed_closed=%d "
           "pass=%d gz=%.1f->%.1f->%.1f %s\n",
           ra, rb, (double)wx, (double)wz, (double)px, (double)pz,
           closed_block, auto_unlatch, opened, used, crossed_closed, passed,
           (double)gz0, (double)gz1, (double)gz2,
           (closed_block && opened && passed && !crossed_closed) ? "OK"
                                                                : "FAIL");

    /* Restore: close the door and sit the walker back for spawn SAFE. */
    if (port_stan_door_is_open_at(pos[0], pos[2])) {
        (void)port_stan_use_door(wx, wz, lx, lz);
        door_tick_n(PORT_DOOR_OPEN_TICKS);
    }
    if (have_save)
        (void)port_prop_place_walker_at(save_x, save_z);

    if (!closed_block) {
        fprintf(stderr, "chase_door clip_step_ground did not block closed\n");
        return -1;
    }
    if (crossed_closed) {
        fprintf(stderr, "chase_door crossed while closed\n");
        return -1;
    }
    if (!opened) {
        fprintf(stderr, "chase_door did not open\n");
        return -1;
    }
    if (!passed) {
        fprintf(stderr, "chase_door did not pass after open\n");
        return -1;
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
    float inv;
    int i, no, ra = 0, rb = 0, found = 0, used, opened;
    int closed_block = 0, open_pass = 0;
    static const int pick[][2] = {{7, 8}, {8, 7}, {8, 5}, {5, 8}};
    int p;

    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    inv = port_stage_bg_inv();
    if (inv <= 0.f)
        inv = 1.f;
    no = port_stage_opening_count();
    for (p = 0; p < 4 && !found; p++) {
        for (i = 0; i < no; i++) {
            if (port_stage_opening(i, pos, &yaw, &width, &ra, &rb) != 0)
                continue;
            if (ra == pick[p][0] && rb == pick[p][1] && width > 200.f * inv) {
                found = 1;
                break;
            }
        }
    }
    if (!found) {
        for (i = 0; i < no; i++) {
            if (port_stage_opening(i, pos, &yaw, &width, &ra, &rb) != 0)
                continue;
            if (port_stage_path_opening(ra, rb) && width > 200.f * inv) {
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
    /* Midway between old 90-half (scaled) and fitted half so a 90 slab would leak. */
    {
        float inv = port_stage_bg_inv();
        float half90;
        if (inv <= 0.f)
            inv = 1.f;
        half90 = 90.f * inv;
        side = 0.5f * (half90 + 0.5f * width);
        if (side < 100.f * inv)
            side = 100.f * inv;
        if (side > 0.5f * width - 16.f)
            side = 0.5f * width - 16.f;
    }

    {
        int a, b, ok = 0;
        float slx, slz;
        for (a = 0; a < 2 && !ok; a++) {
            slx = (a == 0) ? lx : -lx;
            slz = (a == 0) ? lz : -lz;
            for (b = 0; b < 2 && !ok; b++) {
                float s = (b == 0) ? side : -side;
                px = ox - slx * door_stand_dist(width) + tx * s;
                pz = oz - slz * door_stand_dist(width) + tz * s;
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
        port_api_set_pad(0, 0, 0, (int)PORT_A_BUTTON);
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
    door_tick_n(PORT_DOOR_OPEN_TICKS);

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
    {
        float stand = door_stand_dist(width);
        px = ox - lx * stand;
        pz = oz - lz * stand;
        if (!port_stan_on_tile(px, pz)) {
            px = ox + lx * stand;
            pz = oz + lz * stand;
            lx = -lx;
            lz = -lz;
        }
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
        port_api_set_pad(0, 0, 0, (int)PORT_A_BUTTON);
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
        {
            float inv = port_stage_bg_inv();
            if (inv <= 0.f)
                inv = 1.f;
            if (hw > 140.f * inv && fabsf(along) < 110.f * inv)
                ok = 0; /* still the old 90-half */
            if (hw < 80.f * inv && fabsf(along) > 110.f * inv)
                ok = 0; /* flew across the room */
        }
        if (opened)
            (void)port_stan_use_door(px, pz, lx, lz);
        door_tick_n(PORT_DOOR_OPEN_TICKS);
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
    float inv = port_stage_bg_inv();
    if (inv <= 0.f)
        inv = 1.f;
    if (hinge_park_one(wide, 4, 200.f * inv, 400.f * inv, "wide") != 0)
        return -1;
    if (hinge_park_one(narrow, 4, 100.f * inv, 160.f * inv, "narrow") != 0)
        return -1;
    return 0;
}

static void usage(void)
{
    fprintf(stderr, "shot --pack ge.u.c0pack --out .local/shots [--bench N] [--diag]\n");
}

static double bench_draw_tag(const char *tag, int n)
{
    struct timespec t0, t1;
    int i, seen = 0, skip_r = 0, skip_l = 0, n380 = 0, ng;
    double total_ms, frame_ms;
    unsigned ncmds = 0;
    const GirList *ir;
    float r1[3], px, pz;

    if (n < 1)
        n = 8;
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    px = port_api_player_x();
    pz = port_api_player_z();
    ng = port_prop_guard_count();
    for (i = 0; i < ng; i++) {
        float x = 0.f, y = 0.f, z = 0.f, lx, lz, dx, dz;
        if (port_prop_guard_xyz(i, &x, &y, &z) != 0)
            continue;
        lx = x - r1[0];
        lz = z - r1[2];
        dx = lx - px;
        dz = lz - pz;
        if (dx * dx + dz * dz <= 380.f * 380.f)
            n380++;
    }
    port_api_draw();
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < n; i++)
        port_api_draw();
    clock_gettime(CLOCK_MONOTONIC, &t1);
    total_ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0 +
               (double)(t1.tv_nsec - t0.tv_nsec) / 1000000.0;
    frame_ms = total_ms / (double)n;
    port_prop_last_emit_stats(&seen, &skip_r, &skip_l);
    ir = g1_last_ir();
    ncmds = ir ? ir->ncmds : 0;
    printf("bench %s xz=%.1f,%.1f y=%.1f frames=%d total_ms=%.1f frame_ms=%.2f fps=%.1f "
           "drawn=%d seen=%d skip_range=%d skip_leaf=%d within380=%d rooms=%d cur=%d "
           "settex=%u ok=%u miss=%u ncmds=%u alerted=%d mag=%d/%d\n",
           tag, (double)px, (double)pz, (double)port_api_player_y(), n, total_ms, frame_ms,
           (frame_ms > 0.0) ? (1000.0 / frame_ms) : 0.0, port_prop_drawn(), seen, skip_r,
           skip_l, n380, port_api_rooms_walked(), port_api_current_room(), port_api_settex(),
           port_api_tex_ok(), port_api_tex_miss(), ncmds, port_prop_guard_alerted(),
           port_gun_mag(), port_gun_reserve());
    return frame_ms;
}

static int bench_fps(int n)
{
    float r1[3], px, pz, wx, wz, wy;
    int i, ng, n380 = 0, s, tick = 1;
    double spawn_ms, walk_ms, hall_ms, tick_ms;

    if (n < 1)
        n = 20;
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    px = port_api_player_x();
    pz = port_api_player_z();
    ng = port_prop_guard_count();
    printf("bench spawn=%.1f,%.1f y=%.1f guards=%d doors=%d props=%d r1=%.1f,%.1f idle=%d\n",
           (double)px, (double)pz, (double)port_api_player_y(), ng, port_prop_door_count(),
           port_prop_count(), (double)r1[0], (double)r1[2], port_prop_idle_guard());
    for (i = 0; i < ng; i++) {
        float x = 0.f, y = 0.f, z = 0.f, lx, lz, dx, dz, d;
        if (port_prop_guard_xyz(i, &x, &y, &z) != 0)
            continue;
        lx = x - r1[0];
        lz = z - r1[2];
        dx = lx - px;
        dz = lz - pz;
        d = sqrtf(dx * dx + dz * dz);
        if (d <= 380.f)
            n380++;
        if (i < 12)
            printf("bench_guard %d world=%.1f,%.1f local=%.1f,%.1f d=%.1f\n", i, (double)x,
                   (double)z, (double)lx, (double)lz, (double)d);
    }
    printf("bench within380=%d of %d\n", n380, ng);
    spawn_ms = bench_draw_tag("play_spawn", n);
    /* Time passing at spawn (wasm growth / tick leak). */
    for (s = 0; s < 120; s++)
        (void)port_api_sim_tick((uint32_t)tick++);
    tick_ms = bench_draw_tag("spawn_ticks", n);
    /* Chris 2026-09-01 live door poses (still r71). */
    place(-139.2f, -2336.6f, 260.f);
    (void)bench_draw_tag("chris_door1", n);
    place(-354.5f, -2107.1f, 289.f);
    hall_ms = bench_draw_tag("chris_door2", n);
    /* Walk from spawn toward that pose with clip + ticks, like live WASD. */
    place(px, pz, 270.f);
    wx = px;
    wz = pz;
    wy = port_api_player_y();
    for (s = 0; s < 400; s++) {
        float tx = -354.5f - wx, tz = -2107.1f - wz, len, nx, nz, ny;
        len = sqrtf(tx * tx + tz * tz);
        if (len < 12.f)
            break;
        nx = wx + tx / len * 5.f;
        nz = wz + tz / len * 5.f;
        ny = wy;
        port_stan_clip_step(wx, wz, &nx, &nz, &ny);
        if ((nx - wx) * (nx - wx) + (nz - wz) * (nz - wz) < 0.01f)
            break;
        wx = nx;
        wz = nz;
        wy = ny;
        port_player_set_pose(wx, wy, wz, 289.f);
        (void)port_api_sim_tick((uint32_t)tick++);
    }
    walk_ms = bench_draw_tag("long_walk", n);
    printf("bench_fps spawn_ms=%.2f tick_ms=%.2f hall_ms=%.2f long_walk_ms=%.2f\n", spawn_ms,
           tick_ms, hall_ms, walk_ms);
    if (spawn_ms > 80.0 || walk_ms > 80.0 || hall_ms > 80.0) {
        fprintf(stderr, "bench_fps frame_ms spawn=%.2f long_walk=%.2f (want ~27-35ms class)\n",
                spawn_ms, walk_ms);
        return -1;
    }
    return 0;
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

static unsigned count_olive(const uint8_t *rgba, int w, int h)
{
    unsigned n = 0;
    int i;
    for (i = 0; i < w * h; i++) {
        unsigned pr = rgba[i * 4], pg = rgba[i * 4 + 1], pb = rgba[i * 4 + 2];
        if (pg < 50u || pg <= pr + 8u || pg <= pb + 8u)
            continue;
        if (pr + pb > pg + 40u)
            continue;
        n++;
    }
    return n;
}

/* Olive camo bbox + near-plane (bottom third) count. Look-down along the
 * fetal +Z put a huge head in the near plane; across-yaw should not. */
static unsigned olive_lookdown(const uint8_t *rgba, int w, int h, int *x0, int *y0,
                               int *x1, int *y1, unsigned *nbot)
{
    unsigned n = 0;
    int x, y, bx0 = w, by0 = h, bx1 = 0, by1 = 0;
    unsigned bot = 0;
    int ycut = (h * 2) / 3;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            const uint8_t *p = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4u;
            unsigned pr = p[0], pg = p[1], pb = p[2];
            if (pg < 50u || pg <= pr + 8u || pg <= pb + 8u)
                continue;
            if (pr + pb > pg + 40u)
                continue;
            n++;
            if (x < bx0)
                bx0 = x;
            if (y < by0)
                by0 = y;
            if (x > bx1)
                bx1 = x;
            if (y > by1)
                by1 = y;
            if (y >= ycut)
                bot++;
        }
    }
    if (x0)
        *x0 = (n ? bx0 : 0);
    if (y0)
        *y0 = (n ? by0 : 0);
    if (x1)
        *x1 = (n ? bx1 : 0);
    if (y1)
        *y1 = (n ? by1 : 0);
    if (nbot)
        *nbot = bot;
    return n;
}

static unsigned count_dark_rect(const uint8_t *rgba, int w, int h, int x0, int y0, int x1,
                               int y1)
{
    unsigned n = 0;
    int x, y;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > w)
        x1 = w;
    if (y1 > h)
        y1 = h;
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            const uint8_t *p = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4u;
            if ((unsigned)p[0] + (unsigned)p[1] + (unsigned)p[2] < 24u)
                n++;
        }
    }
    return n;
}

/* Unshaded 685-688 albedo (~115,99,99 dusty rose). N64 metal is darker. */
static unsigned count_mauve_rect(const uint8_t *rgba, int w, int h, int x0, int y0, int x1,
                                 int y1)
{
    unsigned n = 0;
    int x, y;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > w)
        x1 = w;
    if (y1 > h)
        y1 = h;
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            const uint8_t *p = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4u;
            unsigned pr = p[0], pg = p[1], pb = p[2];
            if (pr < 95u || pr > 160u)
                continue;
            if (pg + 6u > pr || pr > pg + 28u)
                continue;
            if (pb + 6u > pr || pr > pb + 28u)
                continue;
            n++;
        }
    }
    return n;
}

/* SETTEX 685 metal / brown door face. Skip olive camo. */
static unsigned count_metal_rect(const uint8_t *rgba, int w, int h, int x0, int y0, int x1,
                                int y1)
{
    unsigned n = 0;
    int x, y;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > w)
        x1 = w;
    if (y1 > h)
        y1 = h;
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            const uint8_t *p = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4u;
            unsigned pr = p[0], pg = p[1], pb = p[2];
            if (pr < 50u || pg > pr + 8u || pb > pr + 16u)
                continue;
            if (pg >= 50u && pg > pr + 8u && pg > pb + 8u)
                continue;
            n++;
        }
    }
    return n;
}

/* Death-drop KF7 wood/metal: tan, not olive camo, not grey tile. */
static unsigned count_tan_rect(const uint8_t *rgba, int w, int h, int x0, int y0, int x1,
                               int y1)
{
    unsigned n = 0;
    int x, y;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > w)
        x1 = w;
    if (y1 > h)
        y1 = h;
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            const uint8_t *p = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4u;
            unsigned pr = p[0], pg = p[1], pb = p[2];
            if (pr < 70u || pr > 210u)
                continue;
            if (pg < 30u || pg + 6u > pr)
                continue;
            if (pb > pg + 8u)
                continue;
            n++;
        }
    }
    return n;
}

/* Extra-idle camo: olive pixels must not be one SHADE slab. SETTEX 1916
 * is splotchy CI8; greyscale cn flattening used ~a handful of greens. */
static int camo_not_flat(const uint8_t *rgba, int w, int h, const char *tag)
{
    unsigned n = 0, uniq = 0, bits[128];
    unsigned long sum = 0, sq = 0;
    int i;
    memset(bits, 0, sizeof bits);
    for (i = 0; i < w * h; i++) {
        unsigned pr = rgba[i * 4], pg = rgba[i * 4 + 1], pb = rgba[i * 4 + 2];
        unsigned key, luma;
        if (pg < 50u || pg <= pr + 8u || pg <= pb + 8u)
            continue;
        if (pr + pb > pg + 40u)
            continue;
        n++;
        luma = (pr + pg + pb) / 3u;
        sum += luma;
        sq += (unsigned long)luma * luma;
        key = ((pr >> 4) << 8) | ((pg >> 4) << 4) | (pb >> 4);
        if (key < 128u * 32u) {
            unsigned *wrd = &bits[key >> 5];
            unsigned bit = 1u << (key & 31u);
            if (!(*wrd & bit)) {
                *wrd |= bit;
                uniq++;
            }
        }
    }
    printf("camo %s olive=%u uniq=%u", tag, n, uniq);
    if (n >= 80u) {
        double mean = (double)sum / (double)n;
        double var = (double)sq / (double)n - mean * mean;
        printf(" luma=%.1f var=%.1f", mean, var);
        if (uniq < 10u || var < 20.0) {
            printf(" FLAT\n");
            fprintf(stderr, "%s camo flat olive=%u uniq=%u var=%.1f\n", tag, n, uniq,
                    var);
            return -1;
        }
    }
    printf("\n");
    return 0;
}

/* First-person PP7: skin + dark metal in the lower-right. Pitch 0 used to
 * leave only a muzzle sliver (n<80) because .view followed look. */
static unsigned viewgun_lr(const uint8_t *rgba, int w, int h)
{
    int x, y, x0 = w / 2, y0 = h / 2;
    unsigned n = 0;
    for (y = y0; y < h; y++) {
        for (x = x0; x < w; x++) {
            const uint8_t *p = rgba + ((y * w) + x) * 4;
            unsigned r = p[0], g = p[1], b = p[2];
            int skin = (r > 90u && g > 40u && r > b + 15u && r > g);
            unsigned d_rg = r > g ? r - g : g - r;
            unsigned d_gb = g > b ? g - b : b - g;
            int metal = (r + g + b < 90u && r + g + b > 20u && d_rg < 30u && d_gb < 30u);
            if (skin || metal)
                n++;
        }
    }
    return n;
}

static unsigned viewgun_top_right(const uint8_t *rgba, int w, int h)
{
    int x, y;
    unsigned n = 0;
    for (y = 0; y < h / 3; y++) {
        for (x = w / 2; x < w; x++) {
            const uint8_t *p = rgba + ((y * w) + x) * 4;
            unsigned r = p[0], g = p[1], b = p[2];
            if (r > 90u && g > 40u && r > b + 15u && r > g)
                n++;
        }
    }
    return n;
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
static void place(float x, float z, float th);

/* Stand just outside the Rare pad radius, prove the item is drawn, step
 * in, collect, prove it is gone and HUD ammo or armour changed. */
static int pickup_proof(const char *out_dir)
{
    float r1[3], wx, wy, wz, lx, lz, sx, sz, th;
    float dx, dz, dist, nx, nz, ny;
    int dir, found = 0;
    int drawn0, drawn1, hid0, hid1;
    int res0, res1, arm0, arm1, kind;
    int present, gone, hud_ok;

    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    if (port_prop_pickup_xyz(&wx, &wy, &wz) != 0) {
        printf("pickup_proof NONE\n");
        fprintf(stderr, "pickup_proof none (pack had no resolvable pad)\n");
        return -1;
    }
    kind = port_prop_pickup_kind();
    lx = wx - r1[0];
    lz = wz - r1[2];
    /* Walkable stand ~100u from the pad so we are outside the 80u radius. */
    for (dir = 0; dir < 8 && !found; dir++) {
        float ang = (float)dir * 0.78539816f;
        sx = lx + 100.f * cosf(ang);
        sz = lz + 100.f * sinf(ang);
        if (port_stan_on_tile(sx, sz))
            found = 1;
    }
    if (!found) {
        if (port_stan_snap_walkable(&sx, &sz, 0.f, 1.f, PORT_STAN_NEAR_XZ, &ny) != 0) {
            sx = lx;
            sz = lz;
        }
    }
    dx = lx - sx;
    dz = lz - sz;
    dist = sqrtf(dx * dx + dz * dz);
    th = atan2f(dx, -dz) * (180.f / 3.14159265f);
    if (th < 0.f)
        th += 360.f;
    place(sx, sz, th);
    if (shot_one(out_dir, "pickup_before") != 0)
        return -1;
    drawn0 = port_prop_pickup_drawn();
    hid0 = port_prop_pickup_hidden();
    res0 = port_api_gun_reserve();
    arm0 = port_api_armour();
    /* One step into the radius: sit on the pad tile (or 40u closer). */
    nx = lx;
    nz = lz;
    ny = port_api_player_y();
    if (!port_stan_on_tile(nx, nz)) {
        nx = sx + dx * 0.7f;
        nz = sz + dz * 0.7f;
    }
    port_stan_clip_step(sx, sz, &nx, &nz, &ny);
    place(nx, nz, th);
    port_prop_tick_pickup();
    if (shot_one(out_dir, "pickup_after") != 0)
        return -1;
    drawn1 = port_prop_pickup_drawn();
    hid1 = port_prop_pickup_hidden();
    res1 = port_api_gun_reserve();
    arm1 = port_api_armour();
    present = (drawn0 && !hid0);
    gone = (hid1 && !drawn1);
    if (kind == PORT_PICKUP_ARMOUR)
        hud_ok = (arm1 > arm0);
    else
        hud_ok = (res1 > res0);
    printf("pickup_proof pad=%d type=%d model=%d kind=%d stand=%.1f,%.1f "
           "step=%.1f,%.1f d0=%.1f drawn=%d->%d hidden=%d->%d res=%d->%d "
           "arm=%d->%d sfx=%d %s %s %s\n",
           port_prop_pickup_pad(), port_prop_pickup_type(),
           port_prop_pickup_model(), kind, (double)sx, (double)sz,
           (double)nx, (double)nz, (double)dist, drawn0, drawn1, hid0, hid1,
           res0, res1, arm0, arm1, port_audio_last_sfx(),
           present ? "PRESENT" : "absent",
           gone ? "GONE" : "still", hud_ok ? "HUD" : "hudsame");
    if (!present) {
        fprintf(stderr, "pickup_proof item not drawn before collect\n");
        return -1;
    }
    if (!gone) {
        fprintf(stderr, "pickup_proof item still present after collect\n");
        return -1;
    }
    if (!hud_ok) {
        fprintf(stderr, "pickup_proof HUD ammo/armour unchanged\n");
        return -1;
    }
    if (kind == PORT_PICKUP_AMMO && port_audio_last_sfx() != PORT_SFX_AMMO) {
        fprintf(stderr, "pickup_proof ammo sfx=%d (want ammo)\n",
                port_audio_last_sfx());
        return -1;
    }
    if (kind == PORT_PICKUP_ARMOUR && port_audio_last_sfx() != PORT_SFX_ARMOUR) {
        fprintf(stderr, "pickup_proof armour sfx=%d (want armour)\n",
                port_audio_last_sfx());
        return -1;
    }
    return 0;
}

static int drop_proof(const char *out_dir, float death_wx, float death_wz)
{
    float r1[3], wx, wy, wz, lx, lz, sx, sz, th;
    float dx, dz, dist, nx, nz, ny, ddx, ddz, ddist;
    int dir, found = 0;
    int drawn0, drawn1, hid0, hid1;
    int res0, res1;
    int present, gone, hud_ok, at_death;
    int vg0, vg1;

    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    if (port_prop_drop_xyz(&wx, &wy, &wz) != 0) {
        printf("drop_proof NONE\n");
        fprintf(stderr, "drop_proof none (no assigned weapon at death xz)\n");
        return -1;
    }
    ddx = wx - death_wx;
    ddz = wz - death_wz;
    ddist = sqrtf(ddx * ddx + ddz * ddz);
    at_death = (ddist <= PORT_PICKUP_RADIUS);
    lx = wx - r1[0];
    lz = wz - r1[2];
    for (dir = 0; dir < 8 && !found; dir++) {
        float ang = (float)dir * 0.78539816f;
        sx = lx + 100.f * cosf(ang);
        sz = lz + 100.f * sinf(ang);
        if (port_stan_on_tile(sx, sz))
            found = 1;
    }
    if (!found) {
        if (port_stan_snap_walkable(&sx, &sz, 0.f, 1.f, PORT_STAN_NEAR_XZ, &ny) != 0) {
            sx = lx;
            sz = lz;
        }
    }
    dx = lx - sx;
    dz = lz - sz;
    dist = sqrtf(dx * dx + dz * dz);
    th = atan2f(dx, -dz) * (180.f / 3.14159265f);
    if (th < 0.f)
        th += 360.f;
    place(sx, sz, th);
    if (shot_one(out_dir, "drop_before") != 0)
        return -1;
    drawn0 = port_prop_drop_drawn();
    hid0 = port_prop_drop_hidden();
    res0 = port_api_gun_reserve();
    vg0 = port_prop_viewgun_id();
    nx = lx;
    nz = lz;
    ny = port_api_player_y();
    if (!port_stan_on_tile(nx, nz)) {
        nx = sx + dx * 0.7f;
        nz = sz + dz * 0.7f;
    }
    port_stan_clip_step(sx, sz, &nx, &nz, &ny);
    place(nx, nz, th);
    port_prop_tick_pickup();
    if (shot_one(out_dir, "drop_after") != 0)
        return -1;
    drawn1 = port_prop_drop_drawn();
    hid1 = port_prop_drop_hidden();
    res1 = port_api_gun_reserve();
    vg1 = port_prop_viewgun_id();
    present = (drawn0 && !hid0);
    gone = (hid1 && !drawn1);
    hud_ok = (res1 > res0);
    printf("drop_proof model=%d death=%.1f,%.1f drop=%.1f,%.1f ddeath=%.1f "
           "stand=%.1f,%.1f step=%.1f,%.1f drawn=%d->%d hidden=%d->%d "
           "res=%d->%d viewgun=%d->%d %s %s %s %s %s\n",
           port_prop_drop_model(),
           (double)(death_wx - r1[0]), (double)(death_wz - r1[2]),
           (double)lx, (double)lz, (double)ddist,
           (double)sx, (double)sz, (double)nx, (double)nz,
           drawn0, drawn1, hid0, hid1, res0, res1, vg0, vg1,
           at_death ? "ATDEATH" : "away",
           present ? "PRESENT" : "absent",
           gone ? "GONE" : "still",
           hud_ok ? "RESERVE" : "ressame",
           vg1 == PORT_GUN_AK47_ID ? "KF7FP" : "nofpkf7");
    if (!at_death) {
        fprintf(stderr, "drop_proof drop not at death xz\n");
        return -1;
    }
    if (!present) {
        fprintf(stderr, "drop_proof item not drawn before collect\n");
        return -1;
    }
    if (!gone) {
        fprintf(stderr, "drop_proof item still present after collect\n");
        return -1;
    }
    if (!hud_ok) {
        fprintf(stderr, "drop_proof HUD reserve unchanged\n");
        return -1;
    }
    if (vg0 != PORT_GUN_WPPK_ID) {
        fprintf(stderr, "drop_proof viewgun was not PP7 before collect id=%d\n", vg0);
        return -1;
    }
    if (vg1 != PORT_GUN_AK47_ID) {
        fprintf(stderr, "drop_proof viewgun id=%d (no FP KF7 Gak47Z bind)\n", vg1);
        return -1;
    }
    if (port_audio_last_sfx() != PORT_SFX_PICKUP) {
        fprintf(stderr, "drop_proof collect sfx=%d (want pickup)\n",
                port_audio_last_sfx());
        return -1;
    }
    if (port_gun_weapon() != PORT_WEAPON_KF7) {
        fprintf(stderr, "drop_proof weapon=%d (want KF7)\n", port_gun_weapon());
        return -1;
    }
    {
        int mag0 = port_gun_mag();
        port_gun_tick(0);
        port_gun_tick(PORT_Z_TRIG);
        printf("kf7_fire mag=%d->%d act=%d sfx=%d weapon=%d\n", mag0, port_gun_mag(),
               port_gun_last_action(), port_audio_last_sfx(), port_gun_weapon());
        if (port_gun_mag() != mag0 - 1 || port_gun_last_action() != PORT_GUN_ACT_SHOT ||
            port_audio_last_sfx() != PORT_SFX_KF7) {
            fprintf(stderr, "kf7_fire mag=%d->%d act=%d sfx=%d\n", mag0, port_gun_mag(),
                    port_gun_last_action(), port_audio_last_sfx());
            return -1;
        }
    }
    return 0;
}

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

/* Chris 2026-08-31 live poses, converted into retail (s16/levelscale)
 * local: world_old - r1*inv. Same stan tiles as the unscaled-local shots. */
#define PLAY_CORNER_X 76.5f
#define PLAY_CORNER_Z (-2096.0f)
#define PLAY_CORNER_TH 244.0f
#define PLAY_STAIR_X (-571.8f)
#define PLAY_STAIR_Z (-2229.3f)
#define PLAY_STAIR_TH 80.0f
#define PLAY_WALL_X (-728.1f)
#define PLAY_WALL_Z (-2362.9f)
#define PLAY_WALL_TH 271.0f
#define PLAY_BATH_X (-533.0f)
#define PLAY_BATH_Z (-1887.5f)
#define PLAY_HALL_EYE_LO 15.f
#define PLAY_HALL_EYE_HI 50.f

static void playtest_forward(float th, float step, float *dx, float *dz)
{
    float rad = th * (3.14159265f / 180.0f);
    /* Camera look (sin θ, −cos θ). W is N64 stick-up (negative Y) along look. */
    *dx = sinf(rad) * step;
    *dz = -cosf(rad) * step;
}

static void playtest_pose(const char *tag, float x, float z, float th)
{
    float ey = 86.8f, vx, vz, nx, nz, ny, fdx, fdz, tblk = 0.f;
    int on, room, i;

    place(x, z, th);
    port_player_set_pitch(0.f);
    (void)port_stan_eye_y(x, z, &ey);
    on = port_stan_on_tile(x, z);
    room = port_stan_tile_room_at_eye(x, z, ey);
    vx = x;
    vz = z;
    port_stan_visual_xz(x, z, &vx, &vz);
    playtest_forward(th, 12.f, &fdx, &fdz);
    nx = x + fdx;
    nz = z + fdz;
    ny = ey;
    port_stan_clip_step(x, z, &nx, &nz, &ny);
    (void)port_stan_ray_block(x, ey, z, fdx / 12.f, 0.f, fdz / 12.f, &tblk);
    printf("playtest %s pose=%.1f,%.1f th=%.1f eye=%.1f on=%d room=%d "
           "visual_d=%.1f,%.1f clip=%.1f,%.1f eye1=%.1f ray_t=%.1f\n",
           tag, (double)x, (double)z, (double)th, (double)ey, on, room,
           (double)(vx - x), (double)(vz - z), (double)(nx - x), (double)(nz - z),
           (double)ny, (double)tblk);
    port_stan_debug_at(x, z);
    port_stage_dump_walls_at(x, ey, z);
    port_stan_link_reach(x, z);
    /* 8-way clip_step one tick: see if a rising floor exists. */
    {
        static const float kdx[8] = { 12.f, 12.f, 0.f, -12.f, -12.f, -12.f, 0.f, 12.f };
        static const float kdz[8] = { 0.f, 12.f, 12.f, 12.f, 0.f, -12.f, -12.f, -12.f };
        for (i = 0; i < 8; i++) {
            float cx = x + kdx[i], cz = z + kdz[i], cy = ey;
            port_stan_clip_step(x, z, &cx, &cz, &cy);
            if (cx == x && cz == z)
                continue;
            printf("playtest %s step8 d=%.0f,%.0f -> %.1f,%.1f eye=%.1f room=%d\n",
                   tag, (double)kdx[i], (double)kdz[i], (double)cx, (double)cz,
                   (double)cy, port_stan_tile_room(cx, cz));
        }
    }
}

static void dump_guard_leaf(const char *tag, int gi);

static void dump_walked(const char *tag)
{
    int wi;
    printf("%s walked n=%d", tag, port_stage_rooms_walked());
    for (wi = 0; wi < port_stage_rooms_walked(); wi++)
        printf(" %d", port_stage_walked_room(wi));
    printf(" cur=%d\n", port_stage_current_room());
}

static void dump_slabs_local(const char *tag)
{
    float r1[3];
    int i, n, kind;
    float x, z, yaw, ax = 0.f, az = 0.f, ay = 0.f;
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    n = port_prop_slab_emit_count();
    printf("%s slabs n=%d room1=%.1f,%.1f\n", tag, n, (double)r1[0], (double)r1[2]);
    for (i = 0; i < n; i++) {
        if (port_prop_slab_emit_at(i, &x, &z, &yaw, &kind) != 0)
            continue;
        printf("%s slab[%d] local=%.1f,%.1f yaw=%.0f kind=%d\n", tag, i,
               (double)(x - r1[0]), (double)(z - r1[2]), (double)yaw, kind);
    }
    if (port_prop_alcove_xz(&ax, &az, &ay) == 0)
        printf("%s alcove local=%.1f,%.1f yaw=%.0f\n", tag, (double)ax, (double)az,
               (double)ay);
    else
        printf("%s alcove none\n", tag);
}

static void dump_near_guards(const char *tag, float px, float pz)
{
    float r1[3];
    int i, ng;
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    ng = port_prop_guard_count();
    for (i = 0; i < ng; i++) {
        float gx = 0.f, gy = 0.f, gz = 0.f, lx, lz, d;
        float cx = 0.f, cz = 0.f, vr = 0.f, y0 = 0.f, vh = 0.f;
        float x0 = 0.f, z0 = 0.f, x1 = 0.f, z1 = 0.f;
        int rm, dead, block;
        if (port_prop_guard_xyz(i, &gx, &gy, &gz) != 0)
            continue;
        lx = gx - r1[0];
        lz = gz - r1[2];
        d = sqrtf((lx - px) * (lx - px) + (lz - pz) * (lz - pz));
        if (d > 520.f)
            continue;
        rm = port_stan_tile_room(lx, lz);
        dead = port_stan_guard_dead_at(gx, gz);
        block = port_stage_g1_leaf_blocks(px, pz, lx, lz);
        printf("%s g[%d] local=%.1f,%.1f d=%.1f rm=%d dead=%d block=%d walked=%d", tag, i,
               (double)lx, (double)lz, (double)d, rm, dead, block,
               rm >= 1 ? port_stage_walked_has(rm) : 0);
        if (port_prop_guard_visual_cyl(i, &cx, &cz, &vr, &y0, &vh) == 0)
            printf(" vis=%.1f,%.1f r=%.1f", (double)cx, (double)cz, (double)vr);
        if (port_prop_guard_visual_aabb(i, &x0, &z0, &x1, &z1) == 0)
            printf(" aabb=%.1f,%.1f..%.1f,%.1f", (double)x0, (double)z0, (double)x1,
                   (double)z1);
        printf("\n");
    }
}

static int diag_chris(const char *out_dir)
{
    float spawn_x, spawn_z, spawn_y, ipos[3], ilook[3];
    int ipad = -1, di;
    static const struct {
        const char *tag;
        float x, z, th, ph;
    } kpose[] = {
        { "play_spawn", 0.f, 0.f, 270.f, 0.f },
        { "play_door_chris1", -139.2f, -2336.6f, 260.f, -3.f },
        { "play_door_chris2", -354.5f, -2107.1f, 289.f, -5.f },
        { "play_door_mihok", -161.f, -2382.f, 290.f, -5.f },
        { "door_jump_249", -219.0f, -2364.3f, 249.f, -1.f },
        { "play_mihok_block", -219.0f, -2093.6f, 270.f, 0.f },
    };

    spawn_x = port_api_player_x();
    spawn_z = port_api_player_z();
    spawn_y = port_api_player_y();
    printf("diag spawn xz=%.1f,%.1f y=%.1f cur=%d inv=%.6f\n", (double)spawn_x,
           (double)spawn_z, (double)spawn_y, port_api_current_room(),
           (double)port_stage_bg_inv());
    if (port_prop_intro(ipos, ilook, &ipad) == 0) {
        float r1[3];
        r1[0] = r1[1] = r1[2] = 0.f;
        (void)port_stage_room1(r1);
        printf("diag intro pad=%d world=%.1f,%.1f,%.1f local=%.1f,%.1f look=%.2f,%.2f,%.2f\n",
               ipad, (double)ipos[0], (double)ipos[1], (double)ipos[2],
               (double)(ipos[0] - r1[0]), (double)(ipos[2] - r1[2]), (double)ilook[0],
               (double)ilook[1], (double)ilook[2]);
    }
    dump_path_doors();
    for (di = 0; di < (int)(sizeof kpose / sizeof kpose[0]); di++) {
        float x = kpose[di].x, z = kpose[di].z;
        if (x == 0.f && z == 0.f) {
            x = spawn_x;
            z = spawn_z;
        }
        place(x, z, kpose[di].th);
        port_player_set_pitch(kpose[di].ph);
        if (shot_one(out_dir, kpose[di].tag) != 0)
            return -1;
        dump_walked(kpose[di].tag);
        dump_slabs_local(kpose[di].tag);
        dump_near_guards(kpose[di].tag, x, z);
        port_stage_dump_g1_cutouts(x, port_api_player_y(), z, kpose[di].th, kpose[di].ph);
        port_stage_dump_walls_at(x, port_api_player_y(), z);
        dump_guard_leaf(kpose[di].tag, port_prop_idle_guard());
    }
    return 0;
}

static void dump_guard_leaf(const char *tag, int gi)
{
    float gx = 0.f, gz = 0.f, cx = 0.f, cz = 0.f, vr = 0.f, y0 = 0.f, vh = 0.f;
    float x0 = 0.f, z0 = 0.f, x1 = 0.f, z1 = 0.f;
    float r1[3], padx, padz;

    if (gi < 0 || port_prop_guard_xz(gi, &gx, &gz) != 0) {
        printf("%s gi=%d no pad\n", tag, gi);
        return;
    }
    r1[0] = r1[1] = r1[2] = 0.f;
    (void)port_stage_room1(r1);
    padx = gx - r1[0];
    padz = gz - r1[2];
    (void)port_prop_guard_visual_cyl(gi, &cx, &cz, &vr, &y0, &vh);
    if (port_prop_guard_visual_aabb(gi, &x0, &z0, &x1, &z1) != 0) {
        printf("%s gi=%d pad=%.1f,%.1f vis=%.1f,%.1f r=%.1f aabb fail\n", tag, gi,
               (double)padx, (double)padz, (double)cx, (double)cz, (double)vr);
        return;
    }
    printf("%s gi=%d pad=%.1f,%.1f vis=%.1f,%.1f r=%.1f aabb=%.1f,%.1f..%.1f,%.1f "
           "dead=%d block=%d\n",
           tag, gi, (double)padx, (double)padz, (double)cx, (double)cz, (double)vr,
           (double)x0, (double)z0, (double)x1, (double)z1,
           port_stan_guard_dead_at(gx, gz),
           port_stage_g1_leaf_blocks(port_player_x(), port_player_z(), padx, padz));
    port_stage_dump_chr_vs_g1(port_player_x(), port_player_z(), padx, padz, x0, z0, x1, z1);
}

/* Chris live 2026-09-01: same xz, slight yaw → brown door beside the
 * player. Pad from the shot HUD (x -219 z -2364.3 y 29.1 θ 249°). */
#define DOOR_JUMP_X (-219.0f)
#define DOOR_JUMP_Z (-2364.3f)
#define DOOR_JUMP_TH 249.0f
#define DOOR_JUMP_PH (-1.0f)

static int door_jump_yaw_proof(const char *out_dir, float spawn_x, float spawn_z)
{
    static const float kth[] = { 219.f, 234.f, 249.f, 264.f, 279.f };
    float ax0 = 0.f, az0 = 0.f, ay0 = 0.f;
    float px = DOOR_JUMP_X, pz = DOOR_JUMP_Z;
    int i, n = (int)(sizeof kth / sizeof kth[0]);
    struct timespec t0, t1;
    double ms;

    place(px, pz, DOOR_JUMP_TH);
    port_player_set_pitch(DOOR_JUMP_PH);
    if (!port_stan_on_tile(px, pz) || port_stan_tile_room(px, pz) != 71) {
        fprintf(stderr, "door_jump pad not r71 on-tile xz=%.1f,%.1f r=%d on=%d\n",
                (double)px, (double)pz, port_stan_tile_room(px, pz),
                port_stan_on_tile(px, pz));
        return -1;
    }
    for (i = 0; i < n; i++) {
        float ax = 0.f, az = 0.f, ay = 0.f, dx, dz, pdx, pdz;
        char tag[32];
        port_player_set_pose(px, port_api_player_y(), pz, kth[i]);
        port_player_set_pitch(DOOR_JUMP_PH);
        snprintf(tag, sizeof tag, "door_jump_%.0f", (double)kth[i]);
        if (shot_one(out_dir, tag) != 0)
            return -1;
        if (port_prop_alcove_xz(&ax, &az, &ay) != 0) {
            fprintf(stderr, "door_jump th=%.0f alcove not emitted\n", (double)kth[i]);
            return -1;
        }
        dx = ax - spawn_x;
        dz = az - spawn_z;
        pdx = ax - px;
        pdz = az - pz;
        printf("door_jump th=%.0f alcove=%.1f,%.1f yaw=%.0f spawn_d=%.1f,%.1f "
               "player_d=%.1f,%.1f slabs=%d\n",
               (double)kth[i], (double)ax, (double)az, (double)ay, (double)dx, (double)dz,
               (double)pdx, (double)pdz, port_prop_slab_emit_count());
        /* Hall-left is spawn +Z, not the current camera. |dx|~0 dz~116 from
         * spawn; a player-relative stamp was the chris2 glancing leaf. */
        if (dx * dx > 20.f * 20.f || dz < 80.f || dz > 160.f) {
            fprintf(stderr,
                    "door_jump th=%.0f alcove not spawn-left spawn_d=%.1f,%.1f "
                    "(want ~0,+116)\n",
                    (double)kth[i], (double)dx, (double)dz);
            return -1;
        }
        if (pdx * pdx + (pdz - 116.f) * (pdz - 116.f) < 20.f * 20.f &&
            (px - spawn_x) * (px - spawn_x) + (pz - spawn_z) * (pz - spawn_z) >
                40.f * 40.f) {
            fprintf(stderr,
                    "door_jump th=%.0f alcove followed player player_d=%.1f,%.1f\n",
                    (double)kth[i], (double)pdx, (double)pdz);
            return -1;
        }
        if (i == 0) {
            ax0 = ax;
            az0 = az;
            ay0 = ay;
        } else if ((ax - ax0) * (ax - ax0) + (az - az0) * (az - az0) > 4.f ||
                   fabsf(ay - ay0) > 1.f) {
            fprintf(stderr,
                    "door_jump th=%.0f alcove jumped %.1f,%.1f yaw=%.0f -> %.1f,%.1f "
                    "yaw=%.0f\n",
                    (double)kth[i], (double)ax0, (double)az0, (double)ay0, (double)ax,
                    (double)az, (double)ay);
            return -1;
        }
    }
    port_player_set_pose(px, port_api_player_y(), pz, DOOR_JUMP_TH);
    port_player_set_pitch(DOOR_JUMP_PH);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < 8; i++)
        port_api_draw();
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0 +
         (double)(t1.tv_nsec - t0.tv_nsec) / 1000000.0;
    printf("door_jump frame_ms=%.2f fps=%.1f alcove=%.1f,%.1f yaw=%.0f\n", ms / 8.0,
           (ms > 0.0) ? (8000.0 / ms) : 0.0, (double)ax0, (double)az0, (double)ay0);
    if (ms / 8.0 > 40.0) {
        fprintf(stderr, "door_jump frame_ms=%.2f (want ~27ms class)\n", ms / 8.0);
        return -1;
    }
    return 0;
}

static int playtest_chris(const char *out_dir)
{
    float x, z, ny, fdx, fdz;
    int step, high = 0;
    float spawn_x, spawn_z, spawn_y;

    spawn_x = port_api_player_x();
    spawn_z = port_api_player_z();
    spawn_y = port_api_player_y();
    printf("playtest spawn xz=%.1f,%.1f y=%.1f cur=%d inv=%.6f\n",
           (double)spawn_x, (double)spawn_z, (double)spawn_y,
           port_api_current_room(), (double)port_stage_bg_inv());
    port_stan_debug_at(spawn_x, spawn_z);
    if (shot_one(out_dir, "play_spawn") != 0)
        return -1;
    if (port_prop_head_joint_drawn() < 1) {
        fprintf(stderr, "play_spawn headj=%d (idle Chead not on neck 4x4)\n",
                port_prop_head_joint_drawn());
        return -1;
    }
    /* Hor+ 16:9 (same vfov): anamorphic 320x240. Live blit stretches to 640x360
     * so circles stay circles. 4:3 play_spawn is the bit-stable default. */
    {
        unsigned long cx43 = 0, n43 = 0, cx169 = 0, n169 = 0;
        const uint8_t *fb;
        int px, py, w, h;
        w = port_api_fb_width();
        h = port_api_fb_height();
        fb = port_api_fb();
        if (fb) {
            for (py = 0; py < h; py++) {
                for (px = 0; px < w; px++) {
                    const uint8_t *p = fb + ((py * w) + px) * 4;
                    if (p[1] > 50u && p[1] > p[0] + 8u && p[1] > p[2] + 8u) {
                        cx43 += (unsigned long)px;
                        n43++;
                    }
                }
            }
        }
        currentPlayerSetPerspective(10.f, 60.f, 16.f / 9.f);
        if (shot_one(out_dir, "play_spawn_wide") != 0)
            return -1;
        fb = port_api_fb();
        if (fb) {
            for (py = 0; py < h; py++) {
                for (px = 0; px < w; px++) {
                    const uint8_t *p = fb + ((py * w) + px) * 4;
                    if (p[1] > 50u && p[1] > p[0] + 8u && p[1] > p[2] + 8u) {
                        cx169 += (unsigned long)px;
                        n169++;
                    }
                }
            }
        }
        printf("hor+ spawn olive_cx %lu n=%lu -> %lu n=%lu aspect=%.3f hfov=%.1f\n",
               n43 ? cx43 / n43 : 0ul, n43, n169 ? cx169 / n169 : 0ul, n169,
               (double)port_persp_aspect(), (double)port_view_hfov());
        if (n43 < 80u || n169 < 80u) {
            fprintf(stderr, "hor+ spawn olive empty n43=%lu n169=%lu\n", n43, n169);
            currentPlayerSetPerspective(10.f, 60.f, 320.f / 240.f);
            return -1;
        }
        /* Wider hfov packs the left extra-idle toward center (higher mean x).
         * A fitted Pgas leaf on the left can eat the old extra left olive so
         * 4:3 and 16:9 means tie; only a leftward shift is a FOV miss. */
        if (cx169 / n169 + 2ul < cx43 / n43) {
            fprintf(stderr, "hor+ spawn did not widen olive_cx %lu -> %lu\n",
                    cx43 / n43, cx169 / n169);
            currentPlayerSetPerspective(10.f, 60.f, 320.f / 240.f);
            return -1;
        }
        currentPlayerSetPerspective(10.f, 60.f, 320.f / 240.f);
    }
    {
        struct timespec t0, t1;
        double ms;
        int fi, seen = 0, skip_r = 0, skip_l = 0;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (fi = 0; fi < 8; fi++)
            port_api_draw();
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0 +
             (double)(t1.tv_nsec - t0.tv_nsec) / 1000000.0;
        port_prop_last_emit_stats(&seen, &skip_r, &skip_l);
        printf("play_spawn frame_ms=%.2f fps=%.1f drawn=%d seen=%d skip_range=%d skip_leaf=%d\n",
               ms / 8.0, (ms > 0.0) ? (8000.0 / ms) : 0.0, port_prop_drawn(), seen, skip_r,
               skip_l);
        if (ms / 8.0 > 80.0) {
            fprintf(stderr, "play_spawn frame_ms=%.2f (want ~27-35ms class)\n", ms / 8.0);
            return -1;
        }
    }
    /* Chris / Mihok live door poses after ff37828. Not fail-gated. */
    {
        static const struct {
            const char *tag;
            float x, z, th, ph;
        } kdoor[3] = {
            { "play_door_chris1", -139.2f, -2336.6f, 260.f, -3.f },
            { "play_door_chris2", -354.5f, -2107.1f, 289.f, -5.f },
            { "play_door_mihok", -161.f, -2382.f, 290.f, -5.f },
        };
        int di;
        for (di = 0; di < 3; di++) {
            place(kdoor[di].x, kdoor[di].z, kdoor[di].th);
            port_player_set_pitch(kdoor[di].ph);
            if (shot_one(out_dir, kdoor[di].tag) != 0)
                return -1;
            dump_guard_leaf(kdoor[di].tag, port_prop_idle_guard());
            printf("door_pose %s walked=%d cur=%d slabs=%d headj=%d\n", kdoor[di].tag,
                   port_stage_rooms_walked(), port_stage_current_room(),
                   port_prop_slab_emit_count(), port_prop_head_joint_drawn());
            if (!strcmp(kdoor[di].tag, "play_door_chris2")) {
                float r1[3], ax = 0.f, az = 0.f, ay = 0.f;
                int gi, ng;
                r1[0] = r1[1] = r1[2] = 0.f;
                (void)port_stage_room1(r1);
                ng = port_prop_guard_count();
                if (port_prop_alcove_xz(&ax, &az, &ay) == 0) {
                    fprintf(stderr,
                            "play_door_chris2 alcove followed to %.1f,%.1f (spawn stamp)\n",
                            (double)ax, (double)az);
                    return -1;
                }
                for (gi = 0; gi < ng; gi++) {
                    float gx = 0.f, gz = 0.f, lx, lz, d, cx = 0.f, cz = 0.f, vr = 0.f,
                          y0 = 0.f, vh = 0.f, pdx, pdz, pd;
                    if (gi == port_prop_idle_guard())
                        continue;
                    if (port_prop_guard_xz(gi, &gx, &gz) != 0)
                        continue;
                    lx = gx - r1[0];
                    lz = gz - r1[2];
                    d = sqrtf((lx - kdoor[di].x) * (lx - kdoor[di].x) +
                              (lz - kdoor[di].z) * (lz - kdoor[di].z));
                    if (d > 420.f)
                        continue;
                    if (port_stan_guard_dead_at(gx, gz))
                        continue;
                    if (port_prop_guard_visual_cyl(gi, &cx, &cz, &vr, &y0, &vh) != 0)
                        continue;
                    pdx = cx - lx;
                    pdz = cz - lz;
                    pd = sqrtf(pdx * pdx + pdz * pdz);
                    printf("chris2 vis gi=%d pad=%.1f,%.1f vis=%.1f,%.1f dpad=%.1f\n", gi,
                           (double)lx, (double)lz, (double)cx, (double)cz, (double)pd);
                    if (pd > 80.f) {
                        fprintf(stderr,
                                "play_door_chris2 gi=%d vis shoved %.1f from pad "
                                "(want <80)\n",
                                gi, (double)pd);
                        return -1;
                    }
                }
            }
        }
        /* Mihok live: W stuck at x=-219 z=-2093 θ270 until strafe. */
        {
            float mx = -219.f, mz = -2093.6f, my = 29.1f, nx, nz, ny, ddx, ddz;
            place(mx, mz, 270.f);
            port_player_set_pitch(0.f);
            (void)port_stan_eye_y(mx, mz, &my);
            playtest_forward(270.f, 12.f, &ddx, &ddz);
            nx = mx + ddx;
            nz = mz + ddz;
            ny = my;
            port_stan_clip_step(mx, mz, &nx, &nz, &ny);
            printf("mihok_block from=%.1f,%.1f y=%.1f to=%.1f,%.1f y=%.1f d=%.1f on=%d r=%d\n",
                   (double)mx, (double)mz, (double)my, (double)nx, (double)nz, (double)ny,
                   (double)sqrtf((nx - mx) * (nx - mx) + (nz - mz) * (nz - mz)),
                   port_stan_on_tile(mx, mz), port_stan_tile_room(mx, mz));
            if (shot_one(out_dir, "play_mihok_block") != 0)
                return -1;
        }
        place(spawn_x, spawn_z, 270.f);
        port_player_set_pitch(0.f);
    }
    if (sfx_bank_proof() != 0)
        return -1;
    if (door_jump_yaw_proof(out_dir, spawn_x, spawn_z) != 0)
        return -1;
    place(spawn_x, spawn_z, 270.f);
    port_player_set_pitch(0.f);
    printf("walkhall spawn_shot cur=%d walked=%d c0=%d vtx=%d y=%.1f\n",
           port_stage_current_room(), port_stage_rooms_walked(),
           port_stage_gdl_c0(), port_stage_gdl_vtx(), (double)spawn_y);
    if (spawn_y > 200.f || port_stage_current_room() != 71 ||
        !port_stage_gdl_c0() || !port_stage_gdl_vtx()) {
        fprintf(stderr, "walkhall spawn not low G1 stall\n");
        return -1;
    }
    /* ~2s WASD around spawn: must not launch to r12. */
    {
        static const float kdx[8] = { 12.f, 12.f, 0.f, -12.f, -12.f, -12.f, 0.f, 12.f };
        static const float kdz[8] = { 0.f, 12.f, 12.f, 12.f, 0.f, -12.f, -12.f, -12.f };
        float wx = spawn_x, wz = spawn_z, wy = spawn_y;
        int s, d;
        place(wx, wz, 270.f);
        for (s = 0; s < 24; s++) {
            d = s % 8;
            {
                float nx = wx + kdx[d], nz = wz + kdz[d], ny = wy;
                port_stan_clip_step(wx, wz, &nx, &nz, &ny);
                if (ny > 200.f) {
                    fprintf(stderr, "walkhall spawn WASD hop step=%d y=%.1f xz=%.1f,%.1f\n",
                            s, (double)ny, (double)nx, (double)nz);
                    return -1;
                }
                if ((nx - wx) * (nx - wx) + (nz - wz) * (nz - wz) > 0.25f) {
                    wx = nx;
                    wz = nz;
                    wy = ny;
                }
            }
        }
        port_player_set_pose(wx, wy, wz, 270.f);
        port_player_set_pitch(0.f);
        if (shot_one(out_dir, "play_hall_walk") != 0)
            return -1;
        dump_guard_leaf("hallwalk idle", port_prop_idle_guard());
        printf("walkhall wasd xz=%.1f,%.1f y=%.1f cur=%d walked=%d c0=%d vtx=%d\n",
               (double)wx, (double)wz, (double)wy, port_stage_current_room(),
               port_stage_rooms_walked(), port_stage_gdl_c0(),
               port_stage_gdl_vtx());
        if (wy > 200.f || !port_stage_gdl_c0() || !port_stage_gdl_vtx() ||
            port_stage_rooms_walked() > 30) {
            fprintf(stderr, "walkhall WASD G1/y dump\n");
            return -1;
        }
        {
            unsigned nz0, nz1, dark = 0;
            const uint8_t *fb;
            int px, py, w, h, s2;
            nz0 = port_api_fb_nonzero();
            for (s2 = 0; s2 < 80; s2++) {
                float nx = wx + kdx[s2 % 8], nz = wz + kdz[s2 % 8], ny = wy;
                port_stan_clip_step(wx, wz, &nx, &nz, &ny);
                if (ny > 200.f)
                    break;
                if ((nx - wx) * (nx - wx) + (nz - wz) * (nz - wz) > 0.25f) {
                    wx = nx;
                    wz = nz;
                    wy = ny;
                }
            }
            port_player_set_pose(wx, wy, wz, 270.f);
            port_player_set_pitch(0.f);
            port_api_draw();
            nz1 = port_api_fb_nonzero();
            fb = port_api_fb();
            w = port_api_fb_width();
            h = port_api_fb_height();
            if (fb) {
                for (py = 0; py < h; py++) {
                    for (px = 0; px < w; px++) {
                        const uint8_t *p = fb + ((py * w) + px) * 4;
                        if ((unsigned)p[0] + p[1] + p[2] < 24u)
                            dark++;
                    }
                }
            }
            printf("long_walk fb=%u->%u dark=%u cur=%d walked=%d xz=%.1f,%.1f y=%.1f\n",
                   nz0, nz1, dark, port_stage_current_room(),
                   port_stage_rooms_walked(), (double)wx, (double)wz, (double)wy);
            if (nz1 == 0 || dark >= (unsigned)(w * h)) {
                fprintf(stderr, "long_walk black frame fb=%u dark=%u/%d\n", nz1, dark,
                        w * h);
                return -1;
            }
            {
                double walk_ms = bench_draw_tag("long_walk", 8);
                if (walk_ms > 80.0) {
                    fprintf(stderr, "long_walk frame_ms=%.2f (want ~27-35ms class, not 500+)\n",
                            walk_ms);
                    return -1;
                }
            }
            /* Chris 2026-09-01 live: walk the spawn hall to the far door. */
            {
                float hx = wx, hz = wz, hy = wy;
                int hs, tick = 1;
                double hall_ms;
                for (hs = 0; hs < 400; hs++) {
                    float tx = -354.5f - hx, tz = -2107.1f - hz, len, nx, nz, ny;
                    len = sqrtf(tx * tx + tz * tz);
                    if (len < 12.f)
                        break;
                    nx = hx + tx / len * 5.f;
                    nz = hz + tz / len * 5.f;
                    ny = hy;
                    port_stan_clip_step(hx, hz, &nx, &nz, &ny);
                    if ((nx - hx) * (nx - hx) + (nz - hz) * (nz - hz) < 0.01f)
                        break;
                    hx = nx;
                    hz = nz;
                    hy = ny;
                    port_player_set_pose(hx, hy, hz, 289.f);
                    (void)port_api_sim_tick((uint32_t)tick++);
                }
                hall_ms = bench_draw_tag("long_walk_hall", 8);
                if (hall_ms > 80.0) {
                    fprintf(stderr,
                            "long_walk_hall frame_ms=%.2f (want ~35ms class, not 500+)\n",
                            hall_ms);
                    return -1;
                }
                place(wx, wz, 270.f);
            }
        }
    }

    /* Chris 2026-08-31 live hall hop: A y=29.1 -> B y=409.6 r12. */
#define PLAY_HALL_A_X (-233.7f)
#define PLAY_HALL_A_Z (-2312.1f)
#define PLAY_HALL_A_TH 264.0f
#define PLAY_HALL_B_X (-246.6f)
#define PLAY_HALL_B_Z (-2347.8f)
#define PLAY_HALL_B_TH 250.0f
    {
        float ax = PLAY_HALL_A_X, az = PLAY_HALL_A_Z, ay = 29.1f;
        float bx = PLAY_HALL_B_X, bz = PLAY_HALL_B_Z, by = 29.1f;
        float nx, nz, ny, fdx, fdz;
        int i, hopped = 0;
        printf("walkhall dump A\n");
        place(ax, az, PLAY_HALL_A_TH);
        (void)port_stan_eye_y(ax, az, &ay);
        port_stan_debug_at(ax, az);
        printf("walkhall dump B-xz at low y\n");
        port_stan_debug_at(bx, bz);
        place(ax, az, PLAY_HALL_A_TH);
        nx = bx;
        nz = bz;
        ny = ay;
        port_stan_clip_step(ax, az, &nx, &nz, &ny);
        printf("walkhall A->B clip=%.1f,%.1f eye=%.1f room=%d (from eye=%.1f r%d)\n",
               (double)nx, (double)nz, (double)ny, port_stan_tile_room(nx, nz),
               (double)ay, port_stan_tile_room(ax, az));
        playtest_forward(PLAY_HALL_A_TH, 12.f, &fdx, &fdz);
        nx = ax;
        nz = az;
        ny = ay;
        for (i = 0; i < 40; i++) {
            float cx = nx + fdx, cz = nz + fdz, cy = ny;
            port_stan_clip_step(nx, nz, &cx, &cz, &cy);
            if ((cx - nx) * (cx - nx) + (cz - nz) * (cz - nz) < 0.01f)
                break;
            nx = cx;
            nz = cz;
            ny = cy;
            if (i < 12 || ny > 200.f)
                printf("walkhall step[%d] xz=%.1f,%.1f eye=%.1f room=%d\n",
                       i, (double)nx, (double)nz, (double)ny,
                       port_stan_tile_room(nx, nz));
            if (ny > 200.f) {
                hopped = 1;
                printf("walkhall hop step=%d xz=%.1f,%.1f eye=%.1f room=%d\n",
                       i, (double)nx, (double)nz, (double)ny,
                       port_stan_tile_room(nx, nz));
                port_stan_debug_at(nx, nz);
                break;
            }
        }
        printf("walkhall walkA step=%d xz=%.1f,%.1f eye=%.1f hopped=%d "
               "gdlc0=%d vtx=%d walked=%d cur=%d\n",
               i, (double)nx, (double)nz, (double)ny, hopped,
               port_stage_gdl_c0(), port_stage_gdl_vtx(),
               port_stage_rooms_walked(), port_stage_current_room());
        if (hopped || ny > 200.f) {
            fprintf(stderr, "walkhall hopped to y=%.1f room=%d (want stay ~29)\n",
                    (double)ny, port_stan_tile_room(nx, nz));
            return -1;
        }
        /* Chris live A→B: -233.7,-2312.1 y=29 → -246.6,-2347.8 y=409.6.
         * Look-264 walk is -X; B is -Z. Analog ticks are ~5u. */
        {
            float tdx = PLAY_HALL_B_X - ax, tdz = PLAY_HALL_B_Z - az;
            float tlen = sqrtf(tdx * tdx + tdz * tdz);
            float step, wx, wz, wy;
            int s, hopped_b = 0;
            if (tlen < 1.f)
                tlen = 1.f;
            for (step = 4.f; step <= 12.f + 0.1f; step += 8.f) {
                tdx = (PLAY_HALL_B_X - ax) / tlen * step;
                tdz = (PLAY_HALL_B_Z - az) / tlen * step;
                wx = ax;
                wz = az;
                wy = ay;
                place(wx, wz, PLAY_HALL_B_TH);
                for (s = 0; s < 24; s++) {
                    float cx = wx + tdx, cz = wz + tdz, cy = wy;
                    port_stan_clip_step(wx, wz, &cx, &cz, &cy);
                    if (cy > 200.f) {
                        printf("walkhall A->B hop step=%d sz=%.0f from=%.1f,%.1f y=%.1f "
                               "to=%.1f,%.1f y=%.1f r=%d\n",
                               s, (double)step, (double)wx, (double)wz, (double)wy,
                               (double)cx, (double)cz, (double)cy,
                               port_stan_tile_room_at_eye(cx, cz, cy));
                        hopped_b = 1;
                        break;
                    }
                    if ((cx - wx) * (cx - wx) + (cz - wz) * (cz - wz) < 0.01f)
                        break;
                    wx = cx;
                    wz = cz;
                    wy = cy;
                }
                printf("walkhall A->B step=%.0f n=%d end=%.1f,%.1f y=%.1f r=%d\n",
                       (double)step, s, (double)wx, (double)wz, (double)wy,
                       port_stan_tile_room_at_eye(wx, wz, wy));
                if (hopped_b || wy > 80.f) {
                    fprintf(stderr, "walkhall A->B y=%.1f (want stay ~29)\n", (double)wy);
                    return -1;
                }
            }
        }
        place(ax, az, PLAY_HALL_A_TH);
        port_player_set_pitch(3.f);
        if (shot_one(out_dir, "play_hall_a") != 0)
            return -1;
        printf("walkhall shot A cur=%d walked=%d c0=%d vtx=%d texOk\n",
               port_stage_current_room(), port_stage_rooms_walked(),
               port_stage_gdl_c0(), port_stage_gdl_vtx());
        if (!port_stage_gdl_c0() || !port_stage_gdl_vtx() ||
            port_stage_rooms_walked() > 30 || port_stage_current_room() != 71) {
            fprintf(stderr, "walkhall G1 dump cur=%d walked=%d c0=%d vtx=%d\n",
                    port_stage_current_room(), port_stage_rooms_walked(),
                    port_stage_gdl_c0(), port_stage_gdl_vtx());
            return -1;
        }
    }

    /* Chris 2026-08-31 live shoot: crosshair on spawn-hall guard, hits
     * increment, body stays up. */
#define PLAY_SHOOT_X (-219.0f)
#define PLAY_SHOOT_Z (-2364.3f)
#define PLAY_SHOOT_TH 264.0f
    {
        float r1[3], ey = 29.1f, ldx, ldz, ldy, tblk = 0.f, thit = 0.f;
        int gi, ng, dead0 = 0, dead1 = 0, hits0;
        r1[0] = r1[1] = r1[2] = 0.f;
        (void)port_stage_room1(r1);
        place(PLAY_SHOOT_X, PLAY_SHOOT_Z, PLAY_SHOOT_TH);
        port_player_set_pitch(0.f);
        (void)port_stan_eye_y(PLAY_SHOOT_X, PLAY_SHOOT_Z, &ey);
        port_player_look_dir(&ldx, &ldy, &ldz);
        {
            float vx = PLAY_SHOOT_X, vz = PLAY_SHOOT_Z;
            port_stan_visual_xz(PLAY_SHOOT_X, PLAY_SHOOT_Z, &vx, &vz);
            printf("shoot dump player local=%.1f,%.1f,%.1f world=%.1f,%.1f look=%.3f,%.3f,%.3f visual=%.1f,%.1f dvis=%.1f idle=%d\n",
                   (double)PLAY_SHOOT_X, (double)ey, (double)PLAY_SHOOT_Z,
                   (double)(PLAY_SHOOT_X + r1[0]), (double)(PLAY_SHOOT_Z + r1[2]),
                   (double)ldx, (double)ldy, (double)ldz, (double)vx, (double)vz,
                   (double)sqrtf((vx - PLAY_SHOOT_X) * (vx - PLAY_SHOOT_X) +
                                 (vz - PLAY_SHOOT_Z) * (vz - PLAY_SHOOT_Z)),
                   port_prop_idle_guard());
        }
        ng = port_prop_guard_count();
        for (gi = 0; gi < ng; gi++) {
            float gx, gy, gz, lx, lz, dx, dz, d, along, perp;
            if (port_prop_guard_xyz(gi, &gx, &gy, &gz) != 0)
                continue;
            lx = gx - r1[0];
            lz = gz - r1[2];
            dx = lx - PLAY_SHOOT_X;
            dz = lz - PLAY_SHOOT_Z;
            d = sqrtf(dx * dx + dz * dz);
            if (d > 500.f)
                continue;
            along = dx * ldx + dz * ldz;
            perp = sqrtf(fmaxf(0.f, d * d - along * along));
            printf("shoot guard[%d] world=%.1f,%.1f local=%.1f,%.1f d=%.1f along=%.1f perp=%.1f dead=%d idle=%d\n",
                   gi, (double)gx, (double)gz, (double)lx, (double)lz, (double)d,
                   (double)along, (double)perp,
                   port_stan_guard_dead_at(gx, gz),
                   gi == port_prop_idle_guard());
            (void)gy;
        }
        (void)port_stan_ray_block(PLAY_SHOOT_X, ey, PLAY_SHOOT_Z, ldx, ldy, ldz,
                                  &tblk);
        (void)port_stan_ray_hit(PLAY_SHOOT_X, ey, PLAY_SHOOT_Z, ldx, ldy, ldz,
                                &thit);
        printf("shoot ray coll block_t=%.1f hit_t=%.1f guard=%d hits0=%d\n",
               (double)tblk, (double)thit, port_stan_ray_hit_guard(),
               port_gun_hits());
        {
            float vx = PLAY_SHOOT_X, vz = PLAY_SHOOT_Z, tvis = 0.f;
            port_stan_visual_xz(PLAY_SHOOT_X, PLAY_SHOOT_Z, &vx, &vz);
            (void)port_stan_ray_hit(vx, ey, vz, ldx, ldy, ldz, &tvis);
            printf("shoot ray vis origin=%.1f,%.1f hit_t=%.1f guard=%d\n",
                   (double)vx, (double)vz, (double)tvis, port_stan_ray_hit_guard());
        }
        dump_guard_leaf("shoot idle", port_prop_idle_guard());
        {
            int ig = port_prop_idle_guard();
            float cx = 0.f, cz = 0.f, vr = 0.f, y0 = 0.f, vh = 0.f, tchr = 0.f;
            if (ig >= 0 &&
                port_prop_guard_visual_cyl(ig, &cx, &cz, &vr, &y0, &vh) == 0) {
                float dx = cx - PLAY_SHOOT_X, dz = cz - PLAY_SHOOT_Z;
                float along = dx * ldx + dz * ldz;
                float d = sqrtf(dx * dx + dz * dz);
                float perp = sqrtf(fmaxf(0.f, d * d - along * along));
                printf("shoot viscyl idle local=%.1f,%.1f r=%.1f y=%.1f..%.1f d=%.1f along=%.1f perp=%.1f\n",
                       (double)cx, (double)cz, (double)vr, (double)y0, (double)(y0 + vh),
                       (double)d, (double)along, (double)perp);
            }
            printf("shoot chr_ray hit=%d t=%.1f\n",
                   port_prop_chr_ray_hit(PLAY_SHOOT_X, ey, PLAY_SHOOT_Z, ldx, ldy, ldz,
                                         &tchr),
                   (double)tchr);
        }
        place(PLAY_SHOOT_X, PLAY_SHOOT_Z, PLAY_SHOOT_TH);
        port_player_set_pitch(0.f);
        if (shot_one(out_dir, "play_shoot_before") != 0)
            return -1;
        /* Pack fire_standing on the extra idle (weapon up), then look. */
        {
            int ig = port_prop_idle_guard();
            int bound;
            float gx, gy, gz, r1[3], lx, lz, dx, dz, dist;
            float look_x, look_z, look_y, look_th;
            r1[0] = r1[1] = r1[2] = 0.f;
            (void)port_stage_room1(r1);
            if (ig < 0)
                ig = 0;
            if (port_prop_guard_xyz(ig, &gx, &gy, &gz) != 0) {
                fprintf(stderr, "aim_look no guard xyz\n");
                return -1;
            }
            lx = gx - r1[0];
            lz = gz - r1[2];
            port_prop_hear_player_shot();
            (void)port_prop_tick_guard_fire();
            bound = port_prop_guard_aim_bound(ig);
            printf("aim_look have=%d bound=%d ig=%d %s\n", port_prop_have_aim(), bound, ig,
                   port_prop_idle_info());
            if (!port_prop_have_aim() || !bound) {
                fprintf(stderr, "aim_look not bound have=%d bound=%d %s\n",
                        port_prop_have_aim(), bound, port_prop_idle_info());
                return -1;
            }
            dx = lx - PLAY_SHOOT_X;
            dz = lz - PLAY_SHOOT_Z;
            dist = sqrtf(dx * dx + dz * dz);
            look_x = PLAY_SHOOT_X;
            look_z = PLAY_SHOOT_Z;
            look_y = ey;
            look_th = PLAY_SHOOT_TH;
            if (port_stan_on_tile(look_x, look_z) &&
                port_stan_eye_y(look_x, look_z, &look_y) != 0)
                look_y = ey;
            printf("aim_look from=%.1f,%.1f to=%.1f,%.1f th=%.1f dist=%.1f\n",
                   (double)look_x, (double)look_z, (double)lx, (double)lz, (double)look_th,
                   (double)dist);
            port_player_set_pose(look_x, look_y, look_z, look_th);
            port_player_set_pitch(0.f);
            if (shot_one(out_dir, "play_aim_look") != 0)
                return -1;
            printf("aim_held drawn=%d headj=%d ig=%d %s\n", port_prop_held_drawn(),
                   port_prop_head_joint_drawn(), ig, port_prop_idle_info());
            if (port_prop_head_joint_drawn() < 1) {
                fprintf(stderr, "aim_look headj=%d (aim Chead not on neck 4x4)\n",
                        port_prop_head_joint_drawn());
                return -1;
            }
            if (port_prop_held_drawn() < 1) {
                fprintf(stderr, "aim_look no held KF7 drawn=%d\n", port_prop_held_drawn());
                return -1;
            }
            /* Shoot-pad look is down the barrel (~5px). 3/4 from ~200u so the
             * parented KF7 is a silhouette in the fire_standing grip. */
            {
                float px = -dz / dist, pz = dx / dist;
                float side_x = lx - dx * (200.f / dist) + px * 90.f;
                float side_z = lz - dz * (200.f / dist) + pz * 90.f;
                float side_y = look_y, side_th;
                if (!port_stan_on_tile(side_x, side_z)) {
                    side_x = lx - dx * (200.f / dist);
                    side_z = lz - dz * (200.f / dist);
                }
                if (port_stan_on_tile(side_x, side_z) &&
                    port_stan_eye_y(side_x, side_z, &side_y) != 0)
                    side_y = look_y;
                side_th = atan2f(lx - side_x, -(lz - side_z)) * (180.f / 3.14159265f);
                if (side_th < 0.f)
                    side_th += 360.f;
                printf("aim_grip from=%.1f,%.1f to=%.1f,%.1f th=%.1f\n", (double)side_x,
                       (double)side_z, (double)lx, (double)lz, (double)side_th);
                port_player_set_pose(side_x, side_y, side_z, side_th);
                port_player_set_pitch(0.f);
                if (shot_one(out_dir, "play_aim_grip") != 0)
                    return -1;
                printf("aim_grip held=%d drawn=%d\n", port_prop_held_drawn(),
                       port_prop_drawn());
            }
            place(PLAY_SHOOT_X, PLAY_SHOOT_Z, PLAY_SHOOT_TH);
            port_player_set_pitch(0.f);
        }
        hits0 = port_gun_hits();
        dead0 = 0;
        for (gi = 0; gi < ng; gi++) {
            float gx, gz;
            if (port_prop_guard_xz(gi, &gx, &gz) == 0 &&
                port_stan_guard_dead_at(gx, gz))
                dead0++;
        }
        {
            int s;
            struct timespec tf0, tf1;
            double fire_ms, draw_ms;
            /* Miss hitch: look away from the body so viscyl is a reject. */
            port_player_set_pitch(70.f);
            clock_gettime(CLOCK_MONOTONIC, &tf0);
            port_gun_tick(0);
            port_gun_tick(PORT_Z_TRIG);
            clock_gettime(CLOCK_MONOTONIC, &tf1);
            fire_ms = (double)(tf1.tv_sec - tf0.tv_sec) * 1000.0 +
                      (double)(tf1.tv_nsec - tf0.tv_nsec) / 1000000.0;
            port_player_set_pitch(0.f);
            place(PLAY_SHOOT_X, PLAY_SHOOT_Z, PLAY_SHOOT_TH);
            clock_gettime(CLOCK_MONOTONIC, &tf0);
            port_gun_tick(0);
            port_gun_tick(PORT_Z_TRIG);
            clock_gettime(CLOCK_MONOTONIC, &tf1);
            printf("fire_hitch miss_ms=%.2f hit_ms=%.2f hits=%d\n", fire_ms,
                   (double)(tf1.tv_sec - tf0.tv_sec) * 1000.0 +
                       (double)(tf1.tv_nsec - tf0.tv_nsec) / 1000000.0,
                   port_gun_hits());
            clock_gettime(CLOCK_MONOTONIC, &tf0);
            port_api_draw();
            clock_gettime(CLOCK_MONOTONIC, &tf1);
            draw_ms = (double)(tf1.tv_sec - tf0.tv_sec) * 1000.0 +
                      (double)(tf1.tv_nsec - tf0.tv_nsec) / 1000000.0;
            printf("fire_hitch draw_after_ms=%.2f\n", draw_ms);
            for (s = 0; s < 6; s++) {
                port_gun_tick(0);
                port_gun_tick(PORT_Z_TRIG);
            }
        }
        dead1 = 0;
        for (gi = 0; gi < ng; gi++) {
            float gx, gz;
            if (port_prop_guard_xz(gi, &gx, &gz) == 0 &&
                port_stan_guard_dead_at(gx, gz))
                dead1++;
        }
        printf("shoot after hits=%d->%d kills=%d dead=%d->%d mag=%d die=%d\n",
               hits0, port_gun_hits(), port_api_kills(), dead0, dead1,
               port_gun_mag(), port_prop_have_die());
        if (port_prop_have_die()) {
            int lastf = port_prop_die_last_frame();
            int ti, need = 0;
            if (lastf > 0)
                need = (lastf + PORT_DIE_FRAMES_PER_TICK - 1) /
                       PORT_DIE_FRAMES_PER_TICK;
            for (ti = 0; ti < need + 4; ti++)
                port_prop_tick_die();
        }
        while (port_gun_flash_frames() > 0)
            port_gun_tick(0);
        place(PLAY_SHOOT_X, PLAY_SHOOT_Z, PLAY_SHOOT_TH);
        port_player_set_pitch(0.f);
        if (shot_one(out_dir, "play_shoot_after") != 0)
            return -1;
        /* Look down at the corpse: fetal long axis across the look, not
         * along it (head in the near plane). +pitch is up. */
        port_player_set_pitch(-40.f);
        if (shot_one(out_dir, "play_shoot_after_down") != 0)
            return -1;
        port_player_set_pitch(0.f);
        if (dead1 <= dead0) {
            fprintf(stderr, "shoot guard still up dead=%d hits=%d (want body drop)\n",
                    dead1, port_gun_hits());
            return -1;
        }
        {
            int ig = port_prop_idle_guard();
            float gx = 0.f, gz = 0.f;
            if (ig < 0 || port_prop_guard_xz(ig, &gx, &gz) != 0 ||
                !port_stan_guard_dead_at(gx, gz)) {
                fprintf(stderr, "shoot extra idle still up ig=%d dead=%d hits=%d\n",
                        ig, dead1, port_gun_hits());
                return -1;
            }
        }
        /* Chris locker-adjacent rest look (x -564 z -742 y 29.1). */
        {
            float ey = 29.1f;
            place(-564.f, -742.f, 90.f);
            port_player_set_pitch(20.f);
            (void)port_stan_eye_y(-564.f, -742.f, &ey);
            printf("locker pose local=-564.0,-742.0 eye=%.1f on=%d room=%d\n", (double)ey,
                   port_stan_on_tile(-564.f, -742.f),
                   port_stan_tile_room_at_eye(-564.f, -742.f, ey));
            if (shot_one(out_dir, "play_die_lockers") != 0)
                return -1;
            place(PLAY_SHOOT_X, PLAY_SHOOT_Z, PLAY_SHOOT_TH);
            port_player_set_pitch(0.f);
        }
    }

    /* Chris 2026-08-31 live clip-door: r11 y=29.1. Teleport vs walk. */
#define PLAY_CLIP_X (-651.1f)
#define PLAY_CLIP_Z (-1311.4f)
#define PLAY_CLIP_TH 24.0f
    {
        float ey = 29.1f, nx, nz, ny, ddx, ddz, slen;
        int on, room;
        place(PLAY_CLIP_X, PLAY_CLIP_Z, PLAY_CLIP_TH);
        port_player_set_pitch(3.f);
        (void)port_stan_eye_y(PLAY_CLIP_X, PLAY_CLIP_Z, &ey);
        on = port_stan_on_tile(PLAY_CLIP_X, PLAY_CLIP_Z);
        room = port_stan_tile_room_at_eye(PLAY_CLIP_X, PLAY_CLIP_Z, ey);
        printf("clipdoor pose local=%.1f,%.1f eye=%.1f on=%d room=%d cur=%d\n",
               (double)PLAY_CLIP_X, (double)PLAY_CLIP_Z, (double)ey, on, room,
               port_stage_current_room());
        {
            float r1[3];
            int o, no, di, nd;
            r1[0] = r1[1] = r1[2] = 0.f;
            (void)port_stage_room1(r1);
            no = port_stage_opening_count();
            for (o = 0; o < no; o++) {
                float pos[3], yaw = 0.f, width = 0.f, lx, lz, ddx, ddz, d;
                int ra = 0, rb = 0;
                if (port_stage_opening(o, pos, &yaw, &width, &ra, &rb) != 0)
                    continue;
                lx = pos[0] - r1[0];
                lz = pos[2] - r1[2];
                ddx = lx - PLAY_CLIP_X;
                ddz = lz - PLAY_CLIP_Z;
                d = sqrtf(ddx * ddx + ddz * ddz);
                if (d > 300.f)
                    continue;
                printf("clipdoor open[%d] local=%.1f,%.1f d=%.1f yaw=%.0f w=%.0f ra=%d rb=%d frac=%.2f\n",
                       o, (double)lx, (double)lz, (double)d, (double)yaw, (double)width, ra,
                       rb, (double)port_stan_door_frac_at(pos[0], pos[2]));
            }
            nd = port_prop_door_count();
            for (di = 0; di < nd; di++) {
                float dx, dz, lx, lz, ddx, ddz, d;
                if (port_prop_door_xz(di, &dx, &dz, &lx, &lz) != 0)
                    continue;
                ddx = (dx - r1[0]) - PLAY_CLIP_X;
                ddz = (dz - r1[2]) - PLAY_CLIP_Z;
                d = sqrtf(ddx * ddx + ddz * ddz);
                if (d > 300.f)
                    continue;
                printf("clipdoor propdoor[%d] local=%.1f,%.1f d=%.1f frac=%.2f\n",
                       di, (double)(dx - r1[0]), (double)(dz - r1[2]), (double)d,
                       (double)port_stan_door_frac_at(dx, dz));
            }
        }
        {
            float r1[3], ldx, ldy, ldz;
            int gi, ng;
            r1[0] = r1[1] = r1[2] = 0.f;
            (void)port_stage_room1(r1);
            port_player_look_dir(&ldx, &ldy, &ldz);
            ng = port_prop_guard_count();
            for (gi = 0; gi < ng; gi++) {
                float gx, gy, gz, lx, lz, dx, dz, d, along, tblk = 0.f;
                if (port_prop_guard_xyz(gi, &gx, &gy, &gz) != 0)
                    continue;
                lx = gx - r1[0];
                lz = gz - r1[2];
                dx = lx - PLAY_CLIP_X;
                dz = lz - PLAY_CLIP_Z;
                d = sqrtf(dx * dx + dz * dz);
                if (d > 800.f)
                    continue;
                along = dx * ldx + dz * ldz;
                (void)port_stan_ray_block(PLAY_CLIP_X, ey, PLAY_CLIP_Z,
                                          dx / (d > 1.f ? d : 1.f), 0.f,
                                          dz / (d > 1.f ? d : 1.f), &tblk);
                {
                    char tag[32];
                    snprintf(tag, sizeof tag, "clipdoor guard[%d]", gi);
                    dump_guard_leaf(tag, gi);
                    printf("clipdoor guard[%d] local=%.1f,%.1f d=%.1f along=%.1f block_t=%.1f dead=%d\n",
                           gi, (double)lx, (double)lz, (double)d, (double)along,
                           (double)tblk, port_stan_guard_dead_at(gx, gz));
                }
                (void)gy;
            }
        }
        port_stan_debug_at(PLAY_CLIP_X, PLAY_CLIP_Z);
        playtest_forward(PLAY_CLIP_TH, 12.f, &ddx, &ddz);
        nx = PLAY_CLIP_X + ddx;
        nz = PLAY_CLIP_Z + ddz;
        ny = ey;
        port_stan_clip_step(PLAY_CLIP_X, PLAY_CLIP_Z, &nx, &nz, &ny);
        slen = sqrtf((nx - PLAY_CLIP_X) * (nx - PLAY_CLIP_X) +
                     (nz - PLAY_CLIP_Z) * (nz - PLAY_CLIP_Z));
        printf("clipdoor step d=%.1f,%.1f y=%.1f->%.1f jump=%.1f\n",
               (double)(nx - PLAY_CLIP_X), (double)(nz - PLAY_CLIP_Z), (double)ey,
               (double)ny, (double)slen);
        if (shot_one(out_dir, "play_clip_door") != 0)
            return -1;
        {
            int wi, far_walked = 0, far_rm = 0, nclosed = 0;
            int cur = port_stage_current_room();
            float r1[3];
            int o, no;
            r1[0] = r1[1] = r1[2] = 0.f;
            (void)port_stage_room1(r1);
            printf("clipdoor walked n=%d", port_stage_rooms_walked());
            for (wi = 0; wi < port_stage_rooms_walked(); wi++)
                printf(" %d", port_stage_walked_room(wi));
            printf(" cur=%d\n", cur);
            no = port_stage_opening_count();
            for (o = 0; o < no; o++) {
                float pos[3], yaw = 0.f, width = 0.f, lx, lz, ddx, ddz, d, frac;
                int ra = 0, rb = 0, far;
                if (port_stage_opening(o, pos, &yaw, &width, &ra, &rb) != 0)
                    continue;
                lx = pos[0] - r1[0];
                lz = pos[2] - r1[2];
                ddx = lx - PLAY_CLIP_X;
                ddz = lz - PLAY_CLIP_Z;
                d = sqrtf(ddx * ddx + ddz * ddz);
                if (d > 300.f)
                    continue;
                frac = port_stan_door_frac_at(pos[0], pos[2]);
                if (frac > 0.01f)
                    continue;
                if (!port_stan_closed_door_at_world(pos[0], pos[2]))
                    continue;
                nclosed++;
                far = (ra == cur) ? rb : (rb == cur) ? ra : 0;
                if (far && port_stage_walked_has(far)) {
                    far_walked++;
                    far_rm = far;
                }
            }
            printf("clipdoor closed_near=%d far_walked=%d far_rm=%d\n", nclosed, far_walked,
                   far_rm);
            if (nclosed > 0 && far_walked > 0) {
                fprintf(stderr, "clipdoor far room %d walked through closed door\n", far_rm);
                return -1;
            }
        }
    }
    {
        float wx = spawn_x, wz = spawn_z, wy = spawn_y;
        float tx = PLAY_CLIP_X - spawn_x, tz = PLAY_CLIP_Z - spawn_z;
        float tlen = sqrtf(tx * tx + tz * tz);
        float maxj = 0.f, maxdy = 0.f;
        int s, leaps = 0;
        if (tlen < 1.f)
            tlen = 1.f;
        tx = tx / tlen * 12.f;
        tz = tz / tlen * 12.f;
        place(wx, wz, 270.f);
        for (s = 0; s < 200; s++) {
            float nx = wx + tx, nz = wz + tz, ny = wy, j, dy;
            port_stan_clip_step(wx, wz, &nx, &nz, &ny);
            j = sqrtf((nx - wx) * (nx - wx) + (nz - wz) * (nz - wz));
            dy = ny - wy;
            if (j > maxj)
                maxj = j;
            if (fabsf(dy) > fabsf(maxdy))
                maxdy = dy;
            if (j > 40.f || (fabsf(dy) > 40.f && !(wy < 80.f && ny > 200.f))) {
                printf("clipdoor leap step=%d from=%.1f,%.1f y=%.1f to=%.1f,%.1f y=%.1f j=%.1f dy=%.1f room=%d\n",
                       s, (double)wx, (double)wz, (double)wy, (double)nx, (double)nz,
                       (double)ny, (double)j, (double)dy, port_stan_tile_room(nx, nz));
                leaps++;
            }
            if (j < 0.25f && s > 4)
                break;
            wx = nx;
            wz = nz;
            wy = ny;
        }
        printf("clipdoor walk steps=%d end=%.1f,%.1f y=%.1f room=%d maxj=%.1f maxdy=%.1f leaps=%d\n",
               s, (double)wx, (double)wz, (double)wy, port_stan_tile_room(wx, wz),
               (double)maxj, (double)maxdy, leaps);
        if (leaps > 0 && maxj > 80.f) {
            fprintf(stderr, "clipdoor teleport maxj=%.1f leaps=%d\n", (double)maxj, leaps);
            return -1;
        }
    }
    /* r12 landing (ca673cf) toward Chris clip-door: must not drop through
     * stacked r11 (eye 348 -> 29) or snap 800u. */
    {
        float wx = -244.0f, wz = -2098.7f, wy = 348.2f;
        float tx = PLAY_CLIP_X - wx, tz = PLAY_CLIP_Z - wz;
        float tlen = sqrtf(tx * tx + tz * tz);
        float maxj = 0.f, maxdy = 0.f, mindy = 0.f;
        int s, drops = 0;
        if (tlen < 1.f)
            tlen = 1.f;
        tx = tx / tlen * 12.f;
        tz = tz / tlen * 12.f;
        place(wx, wz, 24.f);
        (void)port_stan_eye_y(wx, wz, &wy);
        printf("clipdoor from_landing xz=%.1f,%.1f y=%.1f room=%d\n",
               (double)wx, (double)wz, (double)wy, port_stan_tile_room(wx, wz));
        for (s = 0; s < 160; s++) {
            float nx = wx + tx, nz = wz + tz, ny = wy, j, dy;
            port_stan_clip_step(wx, wz, &nx, &nz, &ny);
            j = sqrtf((nx - wx) * (nx - wx) + (nz - wz) * (nz - wz));
            dy = ny - wy;
            if (j > maxj)
                maxj = j;
            if (dy > maxdy)
                maxdy = dy;
            if (dy < mindy)
                mindy = dy;
            if (j > 40.f || dy < -40.f) {
                printf("clipdoor drop step=%d from=%.1f,%.1f y=%.1f to=%.1f,%.1f y=%.1f j=%.1f dy=%.1f room=%d\n",
                       s, (double)wx, (double)wz, (double)wy, (double)nx, (double)nz,
                       (double)ny, (double)j, (double)dy,
                       port_stan_tile_room_at_eye(nx, nz, ny));
                drops++;
            }
            if (j < 0.25f && s > 4)
                break;
            wx = nx;
            wz = nz;
            wy = ny;
        }
        printf("clipdoor landing_walk steps=%d end=%.1f,%.1f y=%.1f room=%d maxj=%.1f maxdy=%.1f mindy=%.1f drops=%d\n",
               s, (double)wx, (double)wz, (double)wy,
               port_stan_tile_room_at_eye(wx, wz, wy), (double)maxj, (double)maxdy,
               (double)mindy, drops);
    }
    /* Hunt clip_step teleports: 36 headings from spawn, 80 steps. */
    {
        int h, found = 0;
        for (h = 0; h < 36; h++) {
            float th = (float)h * 10.f, fdx, fdz;
            float wx = spawn_x, wz = spawn_z, wy = spawn_y;
            int s;
            playtest_forward(th, 12.f, &fdx, &fdz);
            place(wx, wz, th);
            for (s = 0; s < 80; s++) {
                float nx = wx + fdx, nz = wz + fdz, ny = wy, j, dy;
                port_stan_clip_step(wx, wz, &nx, &nz, &ny);
                j = sqrtf((nx - wx) * (nx - wx) + (nz - wz) * (nz - wz));
                dy = ny - wy;
                if (j > 120.f || dy < -80.f || dy > 80.f) {
                    float sdx = wx + 571.8f, sdz = wz + 2229.3f;
                    int stair = (dy > 80.f && sdx * sdx + sdz * sdz < 120.f * 120.f);
                    if (!stair) {
                        printf("clipdoor hunt th=%.0f step=%d from=%.1f,%.1f y=%.1f to=%.1f,%.1f y=%.1f j=%.1f dy=%.1f r0=%d r1=%d\n",
                               (double)th, s, (double)wx, (double)wz, (double)wy,
                               (double)nx, (double)nz, (double)ny, (double)j,
                               (double)dy, port_stan_tile_room(wx, wz),
                               port_stan_tile_room_at_eye(nx, nz, ny));
                        if (found == 0)
                            port_stan_debug_at(wx, wz);
                        found++;
                    }
                }
                if (j < 0.25f)
                    break;
                wx = nx;
                wz = nz;
                wy = ny;
            }
        }
        printf("clipdoor hunt teleports=%d\n", found);
        if (found) {
            fprintf(stderr, "clipdoor hunt teleports=%d (want 0 except stair foot)\n",
                    found);
            return -1;
        }
    }

    playtest_pose("corner", PLAY_CORNER_X, PLAY_CORNER_Z, PLAY_CORNER_TH);
    port_player_set_pitch(-3.f);
    if (shot_one(out_dir, "play_corner") != 0)
        return -1;

    playtest_pose("wall", PLAY_WALL_X, PLAY_WALL_Z, PLAY_WALL_TH);
    port_player_set_pitch(-35.f);
    if (shot_one(out_dir, "play_wall") != 0)
        return -1;
    /* Walk collision body into the wall until clip_step stops. */
    {
        float wx = PLAY_WALL_X, wz = PLAY_WALL_Z, wy = 86.8f;
        float vx, vz;
        int blocked = 0;
        place(wx, wz, PLAY_WALL_TH);
        (void)port_stan_eye_y(wx, wz, &wy);
        playtest_forward(PLAY_WALL_TH, 8.f, &fdx, &fdz);
        for (step = 0; step < 40; step++) {
            float nx = wx + fdx, nz = wz + fdz, ey = wy;
            port_stan_clip_step(wx, wz, &nx, &nz, &ey);
            if ((nx - wx) * (nx - wx) + (nz - wz) * (nz - wz) < 0.25f) {
                blocked = 1;
                break;
            }
            wx = nx;
            wz = nz;
            wy = ey;
        }
        vx = wx;
        vz = wz;
        port_stan_visual_xz(wx, wz, &vx, &vz);
        printf("playtest wall_close step=%d blocked=%d body=%.1f,%.1f eye=%.1f "
               "visual=%.1f,%.1f dvis=%.1f\n",
               step, blocked, (double)wx, (double)wz, (double)wy, (double)vx,
               (double)vz, (double)sqrtf((vx - wx) * (vx - wx) + (vz - wz) * (vz - wz)));
        port_player_set_pose(wx, wy, wz, PLAY_WALL_TH);
        port_player_set_pitch(-35.f);
        if (shot_one(out_dir, "play_wall_close") != 0)
            return -1;
        port_player_set_pose(vx, wy, vz, PLAY_WALL_TH);
        port_player_set_pitch(-35.f);
        if (shot_one(out_dir, "play_wall_visual") != 0)
            return -1;
    }

    playtest_pose("stairs", PLAY_STAIR_X, PLAY_STAIR_Z, PLAY_STAIR_TH);
    port_player_set_pitch(4.f);
    if (shot_one(out_dir, "play_stairs") != 0)
        return -1;
    /* Walk forward + greedy-up from Chris's stair foot. */
    x = PLAY_STAIR_X;
    z = PLAY_STAIR_Z;
    ny = 86.8f;
    place(x, z, PLAY_STAIR_TH);
    (void)port_stan_eye_y(x, z, &ny);
    playtest_forward(PLAY_STAIR_TH, 8.f, &fdx, &fdz);
    for (step = 0; step < 80; step++) {
        static const float kdx[8] = { 8.f, 8.f, 0.f, -8.f, -8.f, -8.f, 0.f, 8.f };
        static const float kdz[8] = { 0.f, 8.f, 8.f, 8.f, 0.f, -8.f, -8.f, -8.f };
        float best_s = -1.0e30f, bx = x, bz = z, by = ny;
        int d, moved = 0;
        float nx = x + fdx, nz = z + fdz, ty = ny;
        port_stan_clip_step(x, z, &nx, &nz, &ty);
        if (!(nx == x && nz == z) && ty >= ny - 1.f) {
            best_s = ty - ny + 10.f;
            bx = nx;
            bz = nz;
            by = ty;
            moved = 1;
        }
        for (d = 0; d < 8; d++) {
            float cx = x + kdx[d], cz = z + kdz[d], cy = ny, s;
            port_stan_clip_step(x, z, &cx, &cz, &cy);
            if (cx == x && cz == z)
                continue;
            if (ny > 200.f && cy < 200.f)
                continue;
            s = (cy - ny) * 1000.f - ((cx - (x + fdx)) * (cx - (x + fdx)) +
                                      (cz - (z + fdz)) * (cz - (z + fdz))) * 0.01f;
            if (s > best_s) {
                best_s = s;
                bx = cx;
                bz = cz;
                by = cy;
                moved = 1;
            }
        }
        if (!moved) {
            printf("playtest stairs stuck step=%d xz=%.1f,%.1f eye=%.1f room=%d\n",
                   step, (double)x, (double)z, (double)ny, port_stan_tile_room(x, z));
            break;
        }
        x = bx;
        z = bz;
        ny = by;
        if (step < 8 || step % 10 == 0 || ny > 200.f)
            printf("playtest stairs[%d] xz=%.1f,%.1f eye=%.1f room=%d\n",
                   step, (double)x, (double)z, (double)ny, port_stan_tile_room(x, z));
        if (ny > 200.f) {
            high = 1;
            if (step > 4)
                break;
        }
    }
    printf("playtest stairs_end xz=%.1f,%.1f eye=%.1f room=%d high=%d\n",
           (double)x, (double)z, (double)ny, port_stan_tile_room(x, z), high);
    port_player_set_pose(x, ny, z, PLAY_STAIR_TH);
    port_player_set_pitch(4.f);
    if (shot_one(out_dir, "play_stairs_end") != 0)
        return -1;
    {
        const uint8_t *fb = g1_fb_rgba();
        unsigned long s = 0;
        int pi, wi;
        unsigned mean, dark16 = 0;
        for (pi = 0; pi < 320 * 240; pi++) {
            unsigned luma = ((unsigned)fb[pi * 4] + fb[pi * 4 + 1] +
                             fb[pi * 4 + 2]) / 3u;
            s += (unsigned)fb[pi * 4] + fb[pi * 4 + 1] + fb[pi * 4 + 2];
            if (luma < 16u)
                dark16++;
        }
        mean = (unsigned)(s / (320ul * 240ul * 3ul));
        printf("stairs_end after_draw cur=%d walked=%d c0=%d vtx=%d nz=%u "
               "mean=%u dark16=%u\n",
               port_stage_current_room(), port_stage_rooms_walked(),
               port_stage_gdl_c0(), port_stage_gdl_vtx(),
               (unsigned)g1_fb_nonzero(), mean, dark16);
        printf("stairs_end walked:");
        for (wi = 0; wi < port_stage_rooms_walked(); wi++)
            printf(" %d", port_stage_walked_room(wi));
        printf("\n");
        if (mean < 40u || dark16 > (320u * 240u * 15u) / 100u) {
            fprintf(stderr, "stairs_end mean=%u dark16=%u (void slab)\n", mean,
                    dark16);
            return -1;
        }
    }
    if (!high || ny < 200.f) {
        fprintf(stderr, "playtest stairs stayed y=%.1f (want landing ~348)\n",
                (double)ny);
        return -1;
    }

    place(spawn_x, spawn_z, 0.f);
    printf("playtest spawn_back y=%.1f cur=%d\n", (double)port_api_player_y(),
           port_api_current_room());
    {
        float bx = PLAY_BATH_X, bz = PLAY_BATH_Z, by = 0.f, nx, nz, ny;
        place(bx, bz, 0.f);
        if (port_stan_eye_y(bx, bz, &by) != 0 || by < PLAY_HALL_EYE_LO ||
            by > PLAY_HALL_EYE_HI) {
            fprintf(stderr, "playtest bathroom eye=%.1f (want hall ~29)\n", (double)by);
            return -1;
        }
        nx = bx + 8.f;
        nz = bz;
        ny = by;
        port_stan_clip_step(bx, bz, &nx, &nz, &ny);
        printf("playtest bathroom eye=%.1f clip_y=%.1f\n", (double)by, (double)ny);
        if (ny < PLAY_HALL_EYE_LO || ny > PLAY_HALL_EYE_HI) {
            fprintf(stderr, "playtest bathroom hopped y=%.1f\n", (double)ny);
            return -1;
        }
    }
    if (path_unlatch_proof() != 0)
        return -1;
    {
        int mag0, res0, n = 0;
        while (port_gun_mag() > 0 && n < 20) {
            port_gun_tick(0);
            port_gun_tick(PORT_Z_TRIG);
            n++;
        }
        while (port_gun_flash_frames() > 0)
            port_gun_tick(0);
        mag0 = port_gun_mag();
        res0 = port_gun_reserve();
        port_gun_tick(0);
        port_gun_tick(PORT_Z_TRIG);
        printf("reload_cock mag=%d->%d res=%d->%d flash=%d act=%d sfx=%d n=%d\n",
               mag0, port_gun_mag(), res0, port_gun_reserve(),
               port_gun_flash_frames(), port_gun_last_action(),
               port_audio_last_sfx(), n);
        if (mag0 != 0 || res0 < PORT_PP7_MAG || port_gun_mag() != PORT_PP7_MAG ||
            port_gun_reserve() != res0 - PORT_PP7_MAG ||
            port_gun_flash_frames() != 0 ||
            port_gun_last_action() != PORT_GUN_ACT_RELOAD ||
            port_audio_last_sfx() != PORT_SFX_RELOAD) {
            fprintf(stderr,
                    "reload_cock mag=%d->%d res=%d->%d flash=%d act=%d sfx=%d\n",
                    mag0, port_gun_mag(), res0, port_gun_reserve(),
                    port_gun_flash_frames(), port_gun_last_action(),
                    port_audio_last_sfx());
            return -1;
        }
    }
    {
        int mag0, n = 0;
        while ((port_gun_mag() > 0 || port_gun_reserve() > 0) && n < 80) {
            port_gun_tick(0);
            port_gun_tick(PORT_Z_TRIG);
            n++;
        }
        while (port_gun_flash_frames() > 0)
            port_gun_tick(0);
        mag0 = port_gun_mag();
        port_gun_tick(0);
        port_gun_tick(PORT_Z_TRIG);
        printf("dry_fire mag=%d->%d flash=%d act=%d sfx=%d n=%d\n", mag0,
               port_gun_mag(), port_gun_flash_frames(), port_gun_last_action(),
               port_audio_last_sfx(), n);
        if (port_gun_mag() != mag0 || port_gun_flash_frames() != 0 ||
            port_gun_last_action() != PORT_GUN_ACT_DRY ||
            port_audio_last_sfx() != PORT_SFX_DRY) {
            fprintf(stderr, "dry_fire still shot mag=%d flash=%d act=%d sfx=%d\n",
                    port_gun_mag(), port_gun_flash_frames(), port_gun_last_action(),
                    port_audio_last_sfx());
            return -1;
        }
        port_gun_collect_model(PORT_GUN_MODEL_KF7);
        printf("kf7_pickup weapon=%d mag=%d sfx=%d\n", port_gun_weapon(), port_gun_mag(),
               port_audio_last_sfx());
        if (port_gun_weapon() != PORT_WEAPON_KF7 ||
            port_audio_last_sfx() != PORT_SFX_PICKUP) {
            fprintf(stderr, "kf7_pickup weapon=%d sfx=%d\n", port_gun_weapon(),
                    port_audio_last_sfx());
            return -1;
        }
        mag0 = port_gun_mag();
        port_gun_tick(0);
        port_gun_tick(PORT_Z_TRIG);
        printf("kf7_fire mag=%d->%d act=%d sfx=%d weapon=%d\n", mag0, port_gun_mag(),
               port_gun_last_action(), port_audio_last_sfx(), port_gun_weapon());
        if (port_gun_mag() != mag0 - 1 || port_gun_last_action() != PORT_GUN_ACT_SHOT ||
            port_audio_last_sfx() != PORT_SFX_KF7) {
            fprintf(stderr, "kf7_fire mag=%d->%d act=%d sfx=%d\n", mag0, port_gun_mag(),
                    port_gun_last_action(), port_audio_last_sfx());
            return -1;
        }
    }
    return 0;
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
    char png[512], hud[768], extra[256];
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
             "hp=%d armour=%d%s kills=%d gfire=%d alert=%d settex=%u texOk=%u texMiss=%u abs=%u dec=%u last=%u %s "
             "guards=%d parts=%d drawn=%d held=%d headj=%d viewgun=%d viewid=%d flash=%d pickup=%d drop=%d "
             "aspect=%.3f hfov=%.1f sfx=%d",
             tag, (double)port_api_player_x(), (double)port_api_player_z(),
             (double)port_api_player_y(), (double)port_api_player_theta(),
             (double)port_api_player_phi(), nz, on, tiles, mag, reserve,
             hp, port_api_armour(), hp <= 0 ? " DEAD" : "", port_api_kills(), port_prop_guard_shots(),
             port_prop_guard_alerted(),
             port_api_settex(), port_api_tex_ok(), port_api_tex_miss(),
             port_api_tex_miss_absent(), port_api_tex_miss_decode(),
             (unsigned)g1_tex_last_id(), port_prop_idle_info(), port_prop_guard_count(),
             port_prop_guard_parts(), port_prop_drawn(), port_prop_held_drawn(),
             port_prop_head_joint_drawn(),
             port_prop_viewgun_parts(),
             port_prop_viewgun_id(),
             port_gun_flash_frames(), port_prop_pickup_drawn(),
             port_prop_drop_drawn(),
             (double)port_persp_aspect(), (double)port_view_hfov(),
             port_audio_last_sfx());
    describe_fb(fb, port_api_fb_width(), port_api_fb_height(), extra, sizeof extra);
    printf("%s  draw=%d rooms=%d/%d %s\n", hud, port_api_last_draw(),
           port_api_rooms_walked(), port_api_current_room(), extra);
    if (!strcmp(tag, "play_spawn")) {
        if (mag < 1) {
            fprintf(stderr, "play_spawn empty mag %d/%d\n", mag, reserve);
            return -1;
        }
    }
    if (!strcmp(tag, "play_spawn") || !strcmp(tag, "play_spawn_wide") ||
        !strcmp(tag, "play_shoot_before")) {
        unsigned tan;
        int w = port_api_fb_width(), h = port_api_fb_height();
        /* Extra-idle right-hip hang; skip the left door leaf. */
        tan = count_tan_rect(fb, w, h, w / 8, h / 4, w / 2, (h * 7) / 8);
        printf("idle_hang %s held=%d tan=%u\n", tag, port_prop_held_drawn(), tan);
        if (port_prop_held_drawn() < 1) {
            fprintf(stderr, "%s idle hang empty held=%d\n", tag, port_prop_held_drawn());
            return -1;
        }
        if (tan < 40u) {
            fprintf(stderr, "%s idle hang no tan KF7 tan=%u\n", tag, tan);
            return -1;
        }
    }
    if (!strcmp(tag, "play_shoot_after") || !strcmp(tag, "play_shoot_after_down")) {
        if (port_prop_held_drawn() > 0) {
            fprintf(stderr, "%s dead still holding KF7 held=%d\n", tag,
                    port_prop_held_drawn());
            return -1;
        }
    }
    if (!strcmp(tag, "play_spawn") || !strcmp(tag, "play_hall_a") ||
        !strcmp(tag, "play_shoot_before") || !strcmp(tag, "play_hall_walk") ||
        !strcmp(tag, "play_aim_look") || !strcmp(tag, "play_aim_grip") ||
        !strcmp(tag, "aim_look")) {
        if (camo_not_flat(fb, port_api_fb_width(), port_api_fb_height(), tag) != 0)
            return -1;
    }
    if (!strcmp(tag, "play_clip_door")) {
        unsigned olive = count_olive(fb, port_api_fb_width(), port_api_fb_height());
        unsigned dark, metal, area;
        int w = port_api_fb_width(), h = port_api_fb_height();
        printf("clipdoor_olive n=%u (closed door must not show next-room camo)\n", olive);
        /* Through-door guard + EXIT used to paint thousands of olive
         * pixels. The door slab is brown; current-room tiles are grey. */
        if (olive > 400u) {
            fprintf(stderr, "%s clipdoor_olive=%u (see-through closed door)\n", tag, olive);
            return -1;
        }
        /* G1 mesh hole (not a Rare path portal). Was ~2525/7488 black at
         * 48,72..120,176; the hole itself is ~x=64-128 y=96-176. */
        dark = count_dark_rect(fb, w, h, 64, 96, 128, 176);
        metal = count_metal_rect(fb, w, h, 64, 96, 128, 176);
        area = (unsigned)((128 - 64) * (176 - 96));
        printf("clipdoor_fill dark=%u metal=%u area=%u mauve=%u\n", dark, metal, area,
               count_mauve_rect(fb, w, h, 64, 96, 128, 176));
        if (dark > area / 5u) {
            fprintf(stderr, "%s G1 opening still black dark=%u/%u\n", tag, dark, area);
            return -1;
        }
        if (metal < area / 4u) {
            fprintf(stderr, "%s G1 opening has no door face metal=%u/%u\n", tag, metal,
                    area);
            return -1;
        }
        if (count_mauve_rect(fb, w, h, 64, 96, 128, 176) > area / 3u) {
            fprintf(stderr, "%s G1 opening still unshaded mauve\n", tag);
            return -1;
        }
    }
    if (!strcmp(tag, "play_shoot_after") || !strcmp(tag, "play_shoot_after_down")) {
        unsigned tan;
        int w = port_api_fb_width(), h = port_api_fb_height();
        /* Floor around the extra-idle corpse; skip the left door leaf. */
        tan = count_tan_rect(fb, w, h, w / 4, h / 3, (w * 2) / 3, (h * 7) / 8);
        printf("drop_floor %s drawn=%d tan=%u\n", tag, port_prop_drop_drawn(), tan);
        if (port_prop_drop_drawn() < 1) {
            fprintf(stderr, "%s floor KF7 not drawn drop=%d\n", tag,
                    port_prop_drop_drawn());
            return -1;
        }
    }
    if (!strcmp(tag, "play_spawn") || !strcmp(tag, "play_hall_a")) {
        unsigned dark, metal, area;
        int w = port_api_fb_width(), h = port_api_fb_height();
        dark = count_dark_rect(fb, w, h, 0, 48, 88, 200);
        metal = count_metal_rect(fb, w, h, 0, 48, 88, 200);
        area = (unsigned)(88 * (200 - 48));
        {
            unsigned mauve = count_mauve_rect(fb, w, h, 0, 48, 88, 200);
            printf("spawn_fill %s dark=%u metal=%u area=%u mauve=%u\n", tag, dark, metal,
                   area, mauve);
            if (dark > area / 2u) {
                fprintf(stderr, "%s left sealed opening still black dark=%u/%u\n", tag, dark,
                        area);
                return -1;
            }
            if (metal < area / 12u) {
                fprintf(stderr, "%s left sealed opening has no door face metal=%u/%u\n",
                        tag, metal, area);
                return -1;
            }
            if (mauve > area / 3u) {
                fprintf(stderr, "%s left door still unshaded mauve mauve=%u/%u\n", tag,
                        mauve, area);
                return -1;
            }
        }
    }
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
    if (!strcmp(tag, "play_spawn") || !strcmp(tag, "play_spawn_wide") ||
        !strcmp(tag, "spawn_lookdown") || !strcmp(tag, "play_shoot_after_down")) {
        unsigned lr = viewgun_lr(fb, port_api_fb_width(), port_api_fb_height());
        unsigned top = viewgun_top_right(fb, port_api_fb_width(), port_api_fb_height());
        printf("viewgun_lr %s n=%u top_r=%u\n", tag, lr, top);
        if (lr < 400u) {
            fprintf(stderr, "%s viewgun_lr=%u (PP7 not in lower-right)\n", tag, lr);
            return -1;
        }
        /* Look-down used to swing the PP7 into the top half. Rest Rx keeps
         * it lower-right; corpse flesh is left/center, not top-right. */
        if (!strcmp(tag, "play_shoot_after_down") && top > lr / 2u) {
            fprintf(stderr, "%s viewgun top_r=%u lr=%u (look-down swung the PP7 up)\n",
                    tag, top, lr);
            return -1;
        }
        if (!strcmp(tag, "play_shoot_after_down")) {
            int ox0 = 0, oy0 = 0, ox1 = 0, oy1 = 0;
            unsigned nbot = 0, n;
            float add = port_prop_die_add_yaw();
            n = olive_lookdown(fb, port_api_fb_width(), port_api_fb_height(), &ox0,
                               &oy0, &ox1, &oy1, &nbot);
            printf("die_across %s add=%.0f olive=%u bbox=%d,%d-%d,%d bot=%u\n", tag,
                   (double)add, n, ox0, oy0, ox1, oy1, nbot);
            if (add != 90.f && add != -90.f) {
                fprintf(stderr, "%s die_across add=%.0f (want ±90 so fetal +Z is across)\n",
                        tag, (double)add);
                return -1;
            }
            if (n < 400u) {
                fprintf(stderr, "%s die_across olive=%u (corpse missing)\n", tag, n);
                return -1;
            }
            /* Across the look: camo bbox should be wider than tall. The
             * extra-idle down the hall can stretch y; still require a
             * real x span so this is not a near-plane head blob. */
            if ((ox1 - ox0) < 40) {
                fprintf(stderr, "%s die_across bbox %d,%d-%d,%d (not across)\n", tag, ox0,
                        oy0, ox1, oy1);
                return -1;
            }
        }
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
    float spawn_x = 0.f, spawn_y = 0.f, spawn_z = 0.f, spawn_th = 0.f;

    for (a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--pack") == 0 && a + 1 < argc)
            pack_path = argv[++a];
        else if (strcmp(argv[a], "--out") == 0 && a + 1 < argc)
            out_dir = argv[++a];
        else if (strcmp(argv[a], "--probe") == 0)
            ; /* handled after stage load */
        else if (strcmp(argv[a], "--doors") == 0)
            ; /* handled after stage load */
        else if (strcmp(argv[a], "--pickups") == 0)
            ; /* handled after stage load */
        else if (strcmp(argv[a], "--playtest") == 0)
            ; /* handled after stage load */
        else if (strcmp(argv[a], "--clipmap") == 0)
            ; /* handled after stage load */
        else if (strcmp(argv[a], "--diag") == 0)
            ; /* handled after stage load */
        else if (strcmp(argv[a], "--bench") == 0) {
            if (a + 1 < argc && argv[a + 1][0] != '-')
                a++;
        } else if (strcmp(argv[a], "-h") == 0 || strcmp(argv[a], "--help") == 0) {
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
        {
            int pickups = 0;
            for (aa = 1; aa < argc; aa++)
                if (strcmp(argv[aa], "--pickups") == 0)
                    pickups = 1;
            if (pickups) {
                int prc = pickup_proof(out_dir);
                port_api_shutdown();
                free(pack);
                return prc != 0 ? 3 : 0;
            }
        }
        {
            int play = 0;
            for (aa = 1; aa < argc; aa++)
                if (strcmp(argv[aa], "--playtest") == 0)
                    play = 1;
            if (play) {
                int prc = playtest_chris(out_dir);
                port_api_shutdown();
                free(pack);
                return prc != 0 ? 3 : 0;
            }
        }
        {
            int diag = 0;
            for (aa = 1; aa < argc; aa++)
                if (strcmp(argv[aa], "--diag") == 0)
                    diag = 1;
            if (diag) {
                int prc = diag_chris(out_dir);
                port_api_shutdown();
                free(pack);
                return prc != 0 ? 3 : 0;
            }
        }
        {
            int clipmap = 0;
            for (aa = 1; aa < argc; aa++)
                if (strcmp(argv[aa], "--clipmap") == 0)
                    clipmap = 1;
            if (clipmap) {
                float ey = 29.1f;
                const float cx = -651.1f, cz = -1311.4f, th = 24.0f;
                place(cx, cz, th);
                port_player_set_pitch(3.f);
                (void)port_stan_eye_y(cx, cz, &ey);
                printf("clipmap pose local=%.1f,%.1f eye=%.1f on=%d room=%d\n", (double)cx,
                       (double)cz, (double)ey, port_stan_on_tile(cx, cz),
                       port_stan_tile_room_at_eye(cx, cz, ey));
                port_api_draw();
                port_stage_dump_g1_cutouts(cx, ey, cz, th, 3.f);
                if (shot_one(out_dir, "play_clip_door") != 0) {
                    port_api_shutdown();
                    free(pack);
                    return 3;
                }
                port_api_shutdown();
                free(pack);
                return 0;
            }
        }
        {
            int bench = 0, nframes = 20, aa;
            for (aa = 1; aa < argc; aa++) {
                if (strcmp(argv[aa], "--bench") == 0) {
                    bench = 1;
                    if (aa + 1 < argc && argv[aa + 1][0] != '-')
                        nframes = atoi(argv[aa + 1]);
                }
            }
            if (bench) {
                int brc = bench_fps(nframes);
                port_api_shutdown();
                free(pack);
                return brc != 0 ? 3 : 0;
            }
        }
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
                urc = path_close_swing_proof();
            if (urc == 0)
                urc = chase_door_proof();
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
            {
                float lx = wx - r1[0], lz = wz - r1[2];
                float sx = port_api_player_x(), sz = port_api_player_z();
                float dx = lx - sx, dz = lz - sz, fwd = -dx;
                int slab = 0;
                /* Same cone as spawn_look_slab: spawn looks 270 (-X). */
                if (dx <= 40.f) {
                    if (fwd < 80.f && dx * dx + dz * dz < 180.f * 180.f)
                        slab = 1;
                    if (fwd > 0.f && fwd < 700.f &&
                        dz * dz < (0.65f * fwd) * (0.65f * fwd))
                        slab = 1;
                }
                printf("walker_offslab local=%.1f,%.1f spawn=%.1f,%.1f slab=%d %s\n",
                       (double)lx, (double)lz, (double)sx, (double)sz, slab,
                       slab ? "ONSLAB" : "OFFSLAB");
                if (slab) {
                    fprintf(stderr, "walker sat on spawn look slab\n");
                    free(pack);
                    return 3;
                }
            }
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
    spawn_y = port_api_player_y();
    spawn_z = port_api_player_z();
    spawn_th = port_api_player_theta();
    printf("spawn_first y=%.1f xz=%.1f,%.1f on=%d\n",
           (double)port_api_player_y(), (double)spawn_x, (double)spawn_z,
           port_stan_on_tile(spawn_x, spawn_z));
    if (shot_one(out_dir, "spawn") != 0)
        goto done;
    g_spawn_fb_adler = g_last_fb_adler;
    printf("spawn_viewgun id=%d parts=%d %s\n", port_prop_viewgun_id(),
           port_prop_viewgun_parts(),
           port_prop_viewgun_id() == PORT_GUN_WPPK_ID ? "PP7" : "not_pp7");
    if (port_prop_viewgun_id() != PORT_GUN_WPPK_ID) {
        fprintf(stderr, "spawn viewgun id=%d want PP7 %d\n",
                port_prop_viewgun_id(), PORT_GUN_WPPK_ID);
        goto done;
    }
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
            printf(" r19=%d r18=%d (closed-door vis may omit far rooms)\n", has19, has18);
            if (cur != 71) {
                fprintf(stderr, "spawn cur=%d want r71\n", cur);
                goto done;
            }
        }
    }
    if (probe_eye_band("spawn", spawn_x, spawn_z, 70.f, 110.f) != 0)
        goto done;
    {
        const char *info = port_prop_idle_info();
        int ig;
        float gx, gy, gz, r1[3], lx, lz, dx, dz, dist;
        float look_x, look_z, look_y, look_th;
        if (!info || !strstr(info, "rest=skel") || strstr(info, "skip=aabb")) {
            fprintf(stderr, "idle rest not skel: %s\n", info ? info : "(null)");
            goto done;
        }
        r1[0] = r1[1] = r1[2] = 0.f;
        (void)port_stage_room1(r1);
        ig = port_prop_idle_guard();
        if (ig < 0)
            ig = 0;
        if (port_prop_guard_xyz(ig, &gx, &gy, &gz) != 0) {
            fprintf(stderr, "idle_look no guard xyz\n");
            goto done;
        }
        lx = gx - r1[0];
        lz = gz - r1[2];
        dx = lx - spawn_x;
        dz = lz - spawn_z;
        dist = sqrtf(dx * dx + dz * dz);
        if (dist < 80.f)
            dist = 80.f;
        /* Stand ~220u from the extra idle / first living body, face it. */
        look_x = lx - dx * (220.f / dist);
        look_z = lz - dz * (220.f / dist);
        if (port_stan_eye_y(look_x, look_z, &look_y) != 0)
            look_y = spawn_y;
        look_th = atan2f(dx, -dz) * (180.f / 3.14159265f);
        if (look_th < 0.f)
            look_th += 360.f;
        printf("idle_look from=%.1f,%.1f to=%.1f,%.1f th=%.1f dist=%.1f %s\n",
               (double)look_x, (double)look_z, (double)lx, (double)lz,
               (double)look_th, (double)dist, info);
        {
            float hx = 0.f, hy = 0.f, hz = 0.f;
            int have = port_prop_guard_have_head(ig);
            (void)port_prop_guard_head_off(ig, &hx, &hy, &hz);
            printf("idle_look head have=%d off=%.1f,%.1f,%.1f\n", have,
                   (double)hx, (double)hy, (double)hz);
            if (!have || hy <= 0.f) {
                fprintf(stderr, "idle_look head missing have=%d hy=%.1f\n",
                        have, (double)hy);
                goto done;
            }
        }
        port_player_set_pose(look_x, look_y, look_z, look_th);
        if (shot_one(out_dir, "idle_look") != 0)
            goto done;
        {
            int bound;
            port_prop_hear_player_shot();
            (void)port_prop_tick_guard_fire();
            bound = port_prop_guard_aim_bound(ig);
            printf("aim_look have=%d bound=%d ig=%d %s\n", port_prop_have_aim(), bound, ig,
                   port_prop_idle_info());
            if (!port_prop_have_aim() || !bound) {
                fprintf(stderr, "aim_look not bound have=%d bound=%d %s\n",
                        port_prop_have_aim(), bound, port_prop_idle_info());
                goto done;
            }
            if (shot_one(out_dir, "aim_look") != 0)
                goto done;
            printf("aim_held drawn=%d ig=%d %s\n", port_prop_held_drawn(), ig,
                   port_prop_idle_info());
            if (port_prop_held_drawn() < 1) {
                fprintf(stderr, "aim_look no held KF7 drawn=%d\n", port_prop_held_drawn());
                goto done;
            }
        }
        port_player_set_pose(spawn_x, spawn_y, spawn_z, spawn_th);
    }
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
    {
        float save_x = spawn_x, save_z = spawn_z, save_th = spawn_th;
        if (pickup_proof(out_dir) != 0)
            goto done;
        place(save_x, save_z, save_th);
    }
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
    if (path_close_swing_proof() != 0)
        goto done;
    if (chase_door_proof() != 0)
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
                        if (k2 == k1 + 1 && dead_body) {
                            if (drop_proof(out_dir, bx, bz) != 0)
                                goto done;
                        }
                    }
                }
            }
        }
    }
    /* P0-A: empty mag dry-fire — no muzzle, no ammo drain. */
    {
        int mag0, n = 0;
        while ((port_gun_mag() > 0 || port_gun_reserve() > 0) && n < 80) {
            port_gun_tick(0);
            port_gun_tick(PORT_Z_TRIG);
            n++;
        }
        while (port_gun_flash_frames() > 0)
            port_gun_tick(0);
        mag0 = port_gun_mag();
        port_gun_tick(0);
        port_gun_tick(PORT_Z_TRIG);
        printf("dry_fire mag=%d->%d flash=%d act=%d sfx=%d n=%d\n", mag0,
               port_gun_mag(), port_gun_flash_frames(), port_gun_last_action(),
               port_audio_last_sfx(), n);
        if (port_gun_mag() != mag0 || port_gun_flash_frames() != 0 ||
            port_gun_last_action() != PORT_GUN_ACT_DRY ||
            port_audio_last_sfx() != PORT_SFX_DRY) {
            fprintf(stderr, "dry_fire still shot mag=%d flash=%d act=%d sfx=%d\n",
                    port_gun_mag(), port_gun_flash_frames(), port_gun_last_action(),
                    port_audio_last_sfx());
            goto done;
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

        port_player_damage(PORT_PLAYER_HEALTH_MAX + port_api_armour() + 8);
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
