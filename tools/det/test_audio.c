#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "audio/audio.h"
#include "fs/sha256.h"
#include "rng/random.h"

#define NFRAMES 4096
#define NSAMPLES (NFRAMES * 2)

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

static int load_hex(const char *path, char out[65])
{
    FILE *f = fopen(path, "r");
    size_t n;
    if (!f)
        return -1;
    if (!fgets(out, 65, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    n = strlen(out);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r'))
        out[--n] = 0;
    return n == 64 ? 0 : -1;
}

static void hash_pcm(const int16_t *pcm, char hex[65])
{
    uint8_t digest[32];
    silveriris_sha256((const uint8_t *)pcm, (size_t)NSAMPLES * sizeof(int16_t), digest);
    silveriris_sha256_hex(digest, hex);
}

static int all_zero(const int16_t *pcm)
{
    int i;
    for (i = 0; i < NSAMPLES; i++) {
        if (pcm[i] != 0)
            return 0;
    }
    return 1;
}

static int check_hash(const char *dir, const char *name, const char *got)
{
    char path[512];
    char want[65];
    if (!dir)
        return 0;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (load_hex(path, want) != 0) {
        fprintf(stderr, "FAIL: missing %s\n", path);
        return -1;
    }
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "FAIL: %s got %s want %s\n", name, got, want);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : NULL;
    int16_t pcm[NSAMPLES];
    int16_t dma[200];
    char hex[65];
    uint64_t seed_a, seed_b;
    int i;
    uint32_t left;

    port_audio_init();
    memset(pcm, 0x5a, sizeof pcm);
    port_audio_cb(pcm, NFRAMES);
    if (!all_zero(pcm))
        return fail("silence");
    hash_pcm(pcm, hex);
    printf("silence sha256=%s\n", hex);
    if (check_hash(dir, "silence.pcm.sha256", hex) != 0)
        return 1;

    if (osAiSetFrequency(0) != -1)
        return fail("freq 0");
    if (osAiSetFrequency(22050) != 22050)
        return fail("freq 22050");
    if (port_audio_rate() != 22050)
        return fail("mixer rate");
    if (osAiGetStatus() != 0)
        return fail("status idle");
    if (osAiGetLength() != 0)
        return fail("length empty");

    randomSetSeed(1);
    chrObjRandomSetSeed(2);
    seed_a = g_randomSeed;
    seed_b = g_chrObjRandomSeed;

    port_audio_init();
    port_audio_set_placeholder_music(1);
    if (!port_audio_music_on())
        return fail("music flag");
    port_audio_cb(pcm, NFRAMES);
    if (all_zero(pcm))
        return fail("title was silence");
    if (g_randomSeed != seed_a || g_chrObjRandomSeed != seed_b)
        return fail("music touched game RNG");
    hash_pcm(pcm, hex);
    printf("title sha256=%s\n", hex);
    if (check_hash(dir, "title.pcm.sha256", hex) != 0)
        return 1;

    port_audio_init();
    port_audio_play_gun();
    port_audio_cb(pcm, NFRAMES);
    if (all_zero(pcm))
        return fail("gun was silence");
    if (g_randomSeed != seed_a || g_chrObjRandomSeed != seed_b)
        return fail("gun touched game RNG");
    hash_pcm(pcm, hex);
    printf("gun sha256=%s\n", hex);
    if (check_hash(dir, "gun.pcm.sha256", hex) != 0)
        return 1;
    if (port_audio_last_sfx() != PORT_SFX_GUN)
        return fail("gun last_sfx");

    {
        char gun_hex[65];
        memcpy(gun_hex, hex, 65);
        port_audio_init();
        port_audio_play_dry();
        port_audio_cb(pcm, NFRAMES);
        if (all_zero(pcm))
            return fail("dry was silence");
        if (port_audio_last_sfx() != PORT_SFX_DRY)
            return fail("dry last_sfx");
        hash_pcm(pcm, hex);
        printf("dry sha256=%s\n", hex);
        if (strcmp(hex, gun_hex) == 0)
            return fail("dry pcm matches gun");

        port_audio_init();
        port_audio_play_door();
        port_audio_cb(pcm, NFRAMES);
        if (all_zero(pcm))
            return fail("door was silence");
        if (port_audio_last_sfx() != PORT_SFX_DOOR)
            return fail("door last_sfx");
        hash_pcm(pcm, hex);
        printf("door sha256=%s\n", hex);
        if (strcmp(hex, gun_hex) == 0)
            return fail("door pcm matches gun");

        port_audio_init();
        port_audio_play_fall();
        port_audio_cb(pcm, NFRAMES);
        if (all_zero(pcm))
            return fail("fall was silence");
        if (port_audio_last_sfx() != PORT_SFX_FALL)
            return fail("fall last_sfx");
        hash_pcm(pcm, hex);
        printf("fall sha256=%s\n", hex);
        if (strcmp(hex, gun_hex) == 0)
            return fail("fall pcm matches gun");

        port_audio_init();
        port_audio_play_hit();
        port_audio_cb(pcm, NFRAMES);
        if (all_zero(pcm))
            return fail("hit was silence");
        if (port_audio_last_sfx() != PORT_SFX_HIT)
            return fail("hit last_sfx");
        hash_pcm(pcm, hex);
        printf("hit sha256=%s\n", hex);
        if (strcmp(hex, gun_hex) == 0)
            return fail("hit pcm matches gun");
        if (check_hash(dir, "hit.pcm.sha256", hex) != 0)
            return 1;

        {
            char hit_hex[65];
            memcpy(hit_hex, hex, 65);
            port_audio_init();
            port_audio_play_rico();
            port_audio_cb(pcm, NFRAMES);
            if (all_zero(pcm))
                return fail("rico was silence");
            if (port_audio_last_sfx() != PORT_SFX_RICO)
                return fail("rico last_sfx");
            hash_pcm(pcm, hex);
            printf("rico sha256=%s\n", hex);
            if (strcmp(hex, gun_hex) == 0)
                return fail("rico pcm matches gun");
            if (strcmp(hex, hit_hex) == 0)
                return fail("rico pcm matches hit");
            if (check_hash(dir, "rico.pcm.sha256", hex) != 0)
                return 1;
        }

        port_audio_init();
        port_audio_play_kf7();
        port_audio_cb(pcm, NFRAMES);
        if (all_zero(pcm))
            return fail("kf7 was silence");
        if (port_audio_last_sfx() != PORT_SFX_KF7)
            return fail("kf7 last_sfx");
        hash_pcm(pcm, hex);
        printf("kf7 sha256=%s\n", hex);
        if (strcmp(hex, gun_hex) == 0)
            return fail("kf7 pcm matches gun");

        port_audio_init();
        port_audio_play_pickup();
        port_audio_cb(pcm, NFRAMES);
        if (all_zero(pcm))
            return fail("pickup was silence");
        if (port_audio_last_sfx() != PORT_SFX_PICKUP)
            return fail("pickup last_sfx");
        hash_pcm(pcm, hex);
        printf("pickup sha256=%s\n", hex);
        if (strcmp(hex, gun_hex) == 0)
            return fail("pickup pcm matches gun");

        port_audio_init();
        port_audio_play_door_close();
        port_audio_cb(pcm, NFRAMES);
        if (all_zero(pcm))
            return fail("door_close was silence");
        if (port_audio_last_sfx() != PORT_SFX_DOOR_CLOSE)
            return fail("door_close last_sfx");
        hash_pcm(pcm, hex);
        printf("door_close sha256=%s\n", hex);
        if (strcmp(hex, gun_hex) == 0)
            return fail("door_close pcm matches gun");

        {
            char pickup_hex[65];
            port_audio_init();
            port_audio_play_pickup();
            port_audio_cb(pcm, NFRAMES);
            hash_pcm(pcm, pickup_hex);
            port_audio_init();
            port_audio_play_ammo();
            port_audio_cb(pcm, NFRAMES);
            if (all_zero(pcm))
                return fail("ammo was silence");
            if (port_audio_last_sfx() != PORT_SFX_AMMO)
                return fail("ammo last_sfx");
            hash_pcm(pcm, hex);
            printf("ammo sha256=%s\n", hex);
            if (strcmp(hex, gun_hex) == 0)
                return fail("ammo pcm matches gun");
            if (strcmp(hex, pickup_hex) == 0)
                return fail("ammo pcm matches pickup");
            if (check_hash(dir, "ammo.pcm.sha256", hex) != 0)
                return 1;
        }

        {
            char pickup_hex[65];
            port_audio_init();
            port_audio_play_pickup();
            port_audio_cb(pcm, NFRAMES);
            hash_pcm(pcm, pickup_hex);
            port_audio_init();
            port_audio_play_armour();
            port_audio_cb(pcm, NFRAMES);
            if (all_zero(pcm))
                return fail("armour was silence");
            if (port_audio_last_sfx() != PORT_SFX_ARMOUR)
                return fail("armour last_sfx");
            hash_pcm(pcm, hex);
            printf("armour sha256=%s\n", hex);
            if (strcmp(hex, gun_hex) == 0)
                return fail("armour pcm matches gun");
            if (strcmp(hex, pickup_hex) == 0)
                return fail("armour pcm matches pickup");
            if (check_hash(dir, "armour.pcm.sha256", hex) != 0)
                return 1;
        }

        {
            char pickup_hex[65];
            port_audio_init();
            port_audio_play_pickup();
            port_audio_cb(pcm, NFRAMES);
            hash_pcm(pcm, pickup_hex);
            port_audio_init();
            port_audio_play_reload();
            port_audio_cb(pcm, NFRAMES);
            if (all_zero(pcm))
                return fail("reload was silence");
            if (port_audio_last_sfx() != PORT_SFX_RELOAD)
                return fail("reload last_sfx");
            hash_pcm(pcm, hex);
            printf("reload sha256=%s\n", hex);
            if (strcmp(hex, gun_hex) == 0)
                return fail("reload pcm matches gun");
            if (strcmp(hex, pickup_hex) == 0)
                return fail("reload pcm matches pickup");
            if (check_hash(dir, "reload.pcm.sha256", hex) != 0)
                return 1;
        }

        port_audio_init();
        port_audio_play_yelp();
        port_audio_cb(pcm, NFRAMES);
        if (all_zero(pcm))
            return fail("yelp was silence");
        if (port_audio_last_sfx() != PORT_SFX_YELP)
            return fail("yelp last_sfx");
        hash_pcm(pcm, hex);
        printf("yelp sha256=%s\n", hex);
        if (strcmp(hex, gun_hex) == 0)
            return fail("yelp pcm matches gun");
        if (check_hash(dir, "yelp.pcm.sha256", hex) != 0)
            return 1;

        {
            char yelp_hex[65];
            memcpy(yelp_hex, hex, 65);
            port_audio_init();
            port_audio_play_hurt();
            port_audio_cb(pcm, NFRAMES);
            if (all_zero(pcm))
                return fail("hurt was silence");
            if (port_audio_last_sfx() != PORT_SFX_HURT)
                return fail("hurt last_sfx");
            hash_pcm(pcm, hex);
            printf("hurt sha256=%s\n", hex);
            if (strcmp(hex, gun_hex) == 0)
                return fail("hurt pcm matches gun");
            if (strcmp(hex, yelp_hex) == 0)
                return fail("hurt pcm matches yelp");
            if (check_hash(dir, "hurt.pcm.sha256", hex) != 0)
                return 1;
        }

        {
            char hurt_hex[65];
            memcpy(hurt_hex, hex, 65);
            port_audio_init();
            port_audio_play_step();
            port_audio_cb(pcm, NFRAMES);
            if (all_zero(pcm))
                return fail("step was silence");
            if (port_audio_last_sfx() != PORT_SFX_STEP)
                return fail("step last_sfx");
            hash_pcm(pcm, hex);
            printf("step sha256=%s\n", hex);
            if (strcmp(hex, gun_hex) == 0)
                return fail("step pcm matches gun");
            if (strcmp(hex, hurt_hex) == 0)
                return fail("step pcm matches hurt");
            if (check_hash(dir, "step.pcm.sha256", hex) != 0)
                return 1;
        }

        {
            char gun_only[65], mixed[65];
            port_audio_init();
            port_audio_play_gun();
            port_audio_cb(pcm, NFRAMES);
            hash_pcm(pcm, gun_only);
            port_audio_init();
            port_audio_play_gun();
            port_audio_play_step();
            port_audio_cb(pcm, NFRAMES);
            if (port_audio_last_sfx() != PORT_SFX_STEP)
                return fail("step overlay last_sfx");
            hash_pcm(pcm, mixed);
            if (strcmp(mixed, gun_only) == 0)
                return fail("step did not overlay gun");
        }

        {
            char gun_hex2[65];
            int li, lsum, rsum;

            port_audio_init();
            port_audio_play_gun();
            port_audio_cb(pcm, NFRAMES);
            hash_pcm(pcm, gun_hex2);
            port_audio_init();
            port_audio_set_sfx_pan(0);
            port_audio_play_gun();
            if (!port_audio_spat_on())
                return fail("spat flag");
            port_audio_cb(pcm, NFRAMES);
            hash_pcm(pcm, hex);
            if (strcmp(hex, gun_hex2) == 0)
                return fail("left sfx pan matches center gun");
            lsum = 0;
            rsum = 0;
            for (li = 0; li < NFRAMES; li++) {
                int l = pcm[li * 2];
                int r = pcm[li * 2 + 1];
                if (l < 0)
                    l = -l;
                if (r < 0)
                    r = -r;
                lsum += l;
                rsum += r;
            }
            if (rsum != 0)
                return fail("left sfx pan leaked right");
            if (lsum < 1000)
                return fail("left sfx pan quiet");
            port_audio_init();
            port_audio_play_gun();
            port_audio_cb(pcm, NFRAMES);
            hash_pcm(pcm, hex);
            if (strcmp(hex, gun_hex2) != 0)
                return fail("gun after spat unload");
            if (port_audio_spat_on())
                return fail("spat after center gun");
            printf("spat sfx distinct=1 restore=1\n");
        }

        {
            int li, l0, r0, l1, r1;

            port_audio_init();
            port_audio_play_step();
            if (!port_audio_spat_on())
                return fail("step spat");
            port_audio_cb(pcm, NFRAMES);
            l0 = 0;
            r0 = 0;
            for (li = 0; li < NFRAMES; li++) {
                int l = pcm[li * 2];
                int r = pcm[li * 2 + 1];
                if (l < 0)
                    l = -l;
                if (r < 0)
                    r = -r;
                l0 += l;
                r0 += r;
            }
            port_audio_play_step();
            port_audio_cb(pcm, NFRAMES);
            l1 = 0;
            r1 = 0;
            for (li = 0; li < NFRAMES; li++) {
                int l = pcm[li * 2];
                int r = pcm[li * 2 + 1];
                if (l < 0)
                    l = -l;
                if (r < 0)
                    r = -r;
                l1 += l;
                r1 += r;
            }
            if (!(r0 > l0 && l1 > r1))
                return fail("step L/R feet");
            printf("step pan lr=1\n");
        }

        {
            char gun_hex2[65];
            int li, e127, e32;

            port_audio_init();
            port_audio_play_gun();
            port_audio_cb(pcm, NFRAMES);
            hash_pcm(pcm, gun_hex2);
            e127 = 0;
            for (li = 0; li < NSAMPLES; li++) {
                int v = pcm[li];
                if (v < 0)
                    v = -v;
                e127 += v;
            }
            port_audio_init();
            port_audio_set_sfx_vol(32);
            port_audio_play_gun();
            if (!port_audio_dist_on())
                return fail("dist flag");
            port_audio_cb(pcm, NFRAMES);
            hash_pcm(pcm, hex);
            if (strcmp(hex, gun_hex2) == 0)
                return fail("quiet sfx vol matches full gun");
            e32 = 0;
            for (li = 0; li < NSAMPLES; li++) {
                int v = pcm[li];
                if (v < 0)
                    v = -v;
                e32 += v;
            }
            if (e32 * 2 >= e127)
                return fail("quiet sfx vol not quieter");
            if (e32 < 200)
                return fail("quiet sfx vol silent");
            port_audio_init();
            port_audio_play_gun();
            port_audio_cb(pcm, NFRAMES);
            hash_pcm(pcm, hex);
            if (strcmp(hex, gun_hex2) != 0)
                return fail("gun after dist unload");
            if (port_audio_dist_on())
                return fail("dist after full gun");
            printf("dist sfx distinct=1 restore=1\n");
        }

        {
            char gun_only[65], mixed[65];
            port_audio_init();
            port_audio_play_gun();
            port_audio_cb(pcm, NFRAMES);
            hash_pcm(pcm, gun_only);
            port_audio_init();
            port_audio_play_gun();
            port_audio_play_yelp();
            port_audio_cb(pcm, NFRAMES);
            if (port_audio_last_sfx() != PORT_SFX_YELP)
                return fail("yelp overlay last_sfx");
            hash_pcm(pcm, mixed);
            if (strcmp(mixed, gun_only) == 0)
                return fail("yelp did not overlay gun");
        }

        {
            char three[65], four[65];
            port_audio_init();
            port_audio_play_gun();
            port_audio_play_hit();
            port_audio_play_fall();
            port_audio_cb(pcm, NFRAMES);
            hash_pcm(pcm, three);
            port_audio_init();
            port_audio_play_gun();
            port_audio_play_hit();
            port_audio_play_yelp();
            port_audio_play_fall();
            port_audio_cb(pcm, NFRAMES);
            if (port_audio_last_sfx() != PORT_SFX_FALL)
                return fail("yelp+fall last_sfx");
            hash_pcm(pcm, four);
            if (strcmp(four, three) == 0)
                return fail("yelp cut by fall overlay");
        }

        {
            int16_t y0[256], y1[256];
            char h0[65], h1[65], hw[65];
            int k, v;
            for (k = 0; k < 256; k++) {
                y0[k] = 12000;
                y1[k] = -12000;
            }
            port_audio_init();
            port_audio_clear_yelps();
            port_audio_push_yelp(y0, 256, 127);
            port_audio_push_yelp(y1, 256, 127);
            if (port_audio_yelp_variants() != 2)
                return fail("yelp variants");
            port_audio_play_yelp();
            port_audio_cb(pcm, NFRAMES);
            if (port_audio_last_sfx() != PORT_SFX_YELP)
                return fail("yelp cycle last_sfx");
            hash_pcm(pcm, h0);
            port_audio_play_yelp();
            port_audio_cb(pcm, NFRAMES);
            hash_pcm(pcm, h1);
            if (strcmp(h0, h1) == 0)
                return fail("yelp cycle same pcm");
            if (g_randomSeed != seed_a || g_chrObjRandomSeed != seed_b)
                return fail("yelp cycle touched game RNG");
            port_audio_init();
            port_audio_play_yelp();
            port_audio_cb(pcm, NFRAMES);
            hash_pcm(pcm, hw);
            if (strcmp(hw, h0) != 0)
                return fail("yelp cycle reset");
            v = port_audio_yelp_variants();
            port_audio_init();
            port_audio_play_yelp();
            port_audio_cb(pcm, NFRAMES);
            for (k = 1; k < v; k++) {
                port_audio_play_yelp();
                port_audio_cb(pcm, NFRAMES);
            }
            port_audio_play_yelp();
            port_audio_cb(pcm, NFRAMES);
            hash_pcm(pcm, hw);
            if (strcmp(hw, h0) != 0)
                return fail("yelp cycle wrap");
            printf("yelp_cycle variants=%d distinct=1 wrap=1\n", v);
            port_audio_clear_yelps();
            port_audio_install_sfx(PORT_SFX_YELP, NULL, 0, 0);
        }

        {
            int16_t f0[256], f1[256];
            char h0[65], h1[65], hw[65];
            int k, v;
            for (k = 0; k < 256; k++) {
                f0[k] = 11000;
                f1[k] = -11000;
            }
            port_audio_init();
            port_audio_clear_falls();
            port_audio_push_fall(f0, 256, 127);
            port_audio_push_fall(f1, 256, 127);
            if (port_audio_fall_variants() != 2)
                return fail("fall variants");
            port_audio_play_fall();
            port_audio_cb(pcm, NFRAMES);
            if (port_audio_last_sfx() != PORT_SFX_FALL)
                return fail("fall cycle last_sfx");
            hash_pcm(pcm, h0);
            port_audio_play_fall();
            port_audio_cb(pcm, NFRAMES);
            hash_pcm(pcm, h1);
            if (strcmp(h0, h1) == 0)
                return fail("fall cycle same pcm");
            if (g_randomSeed != seed_a || g_chrObjRandomSeed != seed_b)
                return fail("fall cycle touched game RNG");
            port_audio_init();
            port_audio_play_fall();
            port_audio_cb(pcm, NFRAMES);
            hash_pcm(pcm, hw);
            if (strcmp(hw, h0) != 0)
                return fail("fall cycle reset");
            v = port_audio_fall_variants();
            port_audio_init();
            port_audio_play_fall();
            port_audio_cb(pcm, NFRAMES);
            for (k = 1; k < v; k++) {
                port_audio_play_fall();
                port_audio_cb(pcm, NFRAMES);
            }
            port_audio_play_fall();
            port_audio_cb(pcm, NFRAMES);
            hash_pcm(pcm, hw);
            if (strcmp(hw, h0) != 0)
                return fail("fall cycle wrap");
            printf("fall_cycle variants=%d distinct=1 wrap=1\n", v);
            port_audio_clear_falls();
            port_audio_install_sfx(PORT_SFX_FALL, NULL, 0, 0);
        }
    }

    port_audio_init();
    for (i = 0; i < 100; i++) {
        dma[i * 2] = (int16_t)(i + 1);
        dma[i * 2 + 1] = (int16_t)(-(i + 1));
    }
    if (osAiSetNextBuffer(dma, 100 * 4) != 0)
        return fail("set next");
    if (osAiGetLength() != 400)
        return fail("length after push");
    port_audio_cb(pcm, 50);
    for (i = 0; i < 50; i++) {
        if (pcm[i * 2] != (int16_t)(i + 1) || pcm[i * 2 + 1] != (int16_t)(-(i + 1)))
            return fail("dma samples");
    }
    left = osAiGetLength();
    if (left != 200)
        return fail("length after 50 frames");
    if (__osAiDeviceBusy() != 0)
        return fail("busy with one slot");
    if (osAiSetNextBuffer(dma, 100 * 4) != 0)
        return fail("second slot");
    if (!port_audio_ai_busy() || __osAiDeviceBusy() == 0)
        return fail("two slots should be busy");
    if (osAiSetNextBuffer(dma, 4) != -1)
        return fail("third push should fail");
    if (osAiGetStatus() != 0x80000000u)
        return fail("fifo full status");

    {
        uint8_t frame[9];
        int16_t book[16];
        int16_t decoded[16];
        int ns, i;
        memset(frame, 0, sizeof frame);
        memset(book, 0, sizeof book);
        frame[1] = 0x10; /* first nibble = 1 */
        ns = port_audio_adpcm_decode(frame, 9, book, 2, 1, decoded, 16);
        if (ns != 16)
            return fail("adpcm frame samples");
        if (decoded[0] != 1)
            return fail("adpcm nibble 1");
        for (i = 1; i < 16; i++) {
            if (decoded[i] != 0)
                return fail("adpcm rest");
        }
        if (port_audio_adpcm_decode(NULL, 9, book, 2, 1, decoded, 16) != -1)
            return fail("adpcm null src");
    }

    {
        int16_t tone[64];
        char ph_hex[65];
        int i;
        for (i = 0; i < 64; i++)
            tone[i] = (int16_t)((i & 1) ? 12000 : -12000);
        port_audio_init();
        port_audio_play_gun();
        port_audio_cb(pcm, NFRAMES);
        hash_pcm(pcm, ph_hex);
        port_audio_init();
        port_audio_install_sfx(PORT_SFX_GUN, tone, 64, 127);
        if (!port_audio_sfx_from_bank(PORT_SFX_GUN))
            return fail("install gun");
        if (port_audio_sfx_frames(PORT_SFX_GUN) != 64)
            return fail("install frames");
        port_audio_play_gun();
        port_audio_cb(pcm, NFRAMES);
        hash_pcm(pcm, hex);
        if (strcmp(hex, ph_hex) == 0)
            return fail("installed pcm matches placeholder");
        port_audio_install_sfx(PORT_SFX_GUN, NULL, 0, 0);
        if (port_audio_sfx_from_bank(PORT_SFX_GUN))
            return fail("uninstall");
        if (port_audio_bank_ready())
            return fail("bank ready without pack");
    }

    {
        /* One-track compact MIDI: tempo 500000, C4 vel 100 dur 96 ticks, EOT. */
        static const uint8_t k_seq[] = {
            0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0xFF, 0x51, 0x07,
            0xA1, 0x20, 0x00, 0x90, 0x3C, 0x64, 0x60, 0x00, 0xFF, 0x2F,
        };
        char seq_hex[65];
        char gun_only[65];
        char mixed[65];

        port_audio_init();
        if (port_audio_seq_on())
            return fail("seq on before load");
        if (port_audio_load_seq(k_seq, (uint32_t)sizeof k_seq) != 0)
            return fail("seq load");
        if (!port_audio_seq_on())
            return fail("seq flag");
        port_audio_cb(pcm, NFRAMES);
        if (all_zero(pcm))
            return fail("seq was silence");
        if (g_randomSeed != seed_a || g_chrObjRandomSeed != seed_b)
            return fail("seq touched game RNG");
        hash_pcm(pcm, seq_hex);
        printf("seq sha256=%s\n", seq_hex);
        if (check_hash(dir, "seq.pcm.sha256", seq_hex) != 0)
            return 1;
        if (port_audio_last_sfx() != PORT_SFX_NONE)
            return fail("seq last_sfx");

        port_audio_init();
        port_audio_play_gun();
        port_audio_cb(pcm, NFRAMES);
        hash_pcm(pcm, gun_only);
        port_audio_init();
        if (port_audio_load_seq(k_seq, (uint32_t)sizeof k_seq) != 0)
            return fail("seq load overlay");
        port_audio_play_gun();
        port_audio_cb(pcm, NFRAMES);
        hash_pcm(pcm, mixed);
        if (strcmp(mixed, gun_only) == 0)
            return fail("seq did not overlay gun");
        if (g_randomSeed != seed_a || g_chrObjRandomSeed != seed_b)
            return fail("seq overlay touched game RNG");

        {
            int16_t wave[128];
            char tri_hex[65];
            char wav_hex[65];
            int wi;

            for (wi = 0; wi < 128; wi++)
                wave[wi] = 12000;
            port_audio_init();
            port_audio_unload_inst();
            if (port_audio_inst_on())
                return fail("inst on before push");
            if (port_audio_load_seq(k_seq, (uint32_t)sizeof k_seq) != 0)
                return fail("seq load tri");
            port_audio_cb(pcm, NFRAMES);
            hash_pcm(pcm, tri_hex);
            port_audio_unload_seq();
            if (port_audio_inst_push(0, 0, 127, 60, 0, 127, 100, wave, 128, 0, 0) != 0)
                return fail("inst push");
            if (!port_audio_inst_on())
                return fail("inst flag");
            if (port_audio_load_seq(k_seq, (uint32_t)sizeof k_seq) != 0)
                return fail("seq load wav");
            port_audio_cb(pcm, NFRAMES);
            if (all_zero(pcm))
                return fail("wav seq silence");
            hash_pcm(pcm, wav_hex);
            if (strcmp(wav_hex, tri_hex) == 0)
                return fail("wav pcm matches triangle");
            if (g_randomSeed != seed_a || g_chrObjRandomSeed != seed_b)
                return fail("wav seq touched game RNG");
            port_audio_unload_inst();
            if (port_audio_inst_on())
                return fail("inst after unload");
            if (port_audio_load_seq(k_seq, (uint32_t)sizeof k_seq) != 0)
                return fail("seq load after unload");
            port_audio_cb(pcm, NFRAMES);
            hash_pcm(pcm, hex);
            if (strcmp(hex, tri_hex) != 0)
                return fail("triangle after inst unload");
            printf("wavetable seq distinct=1 restore=1\n");

            {
                char env_hex[65];

                port_audio_unload_inst();
                if (port_audio_env_on())
                    return fail("env on before set");
                if (port_audio_inst_push(0, 0, 127, 60, 0, 127, 100, wave, 128, 0, 128) != 0)
                    return fail("inst push env");
                if (port_audio_env_on())
                    return fail("env default");
                port_audio_inst_set_env(0, -1, 0, 127, 127);
                if (port_audio_env_on())
                    return fail("env default set");
                {
                    char loop_hex[65];

                    if (port_audio_load_seq(k_seq, (uint32_t)sizeof k_seq) != 0)
                        return fail("seq load loop");
                    port_audio_cb(pcm, NFRAMES);
                    hash_pcm(pcm, loop_hex);
                    port_audio_inst_set_env(1000000, -1, 0, 127, 127);
                    if (!port_audio_env_on())
                        return fail("env flag");
                    if (port_audio_load_seq(k_seq, (uint32_t)sizeof k_seq) != 0)
                        return fail("seq load env");
                    port_audio_cb(pcm, NFRAMES);
                    if (all_zero(pcm))
                        return fail("env seq silence");
                    hash_pcm(pcm, env_hex);
                    if (strcmp(env_hex, loop_hex) == 0)
                        return fail("env pcm matches unenveloped wav");
                }
                if (g_randomSeed != seed_a || g_chrObjRandomSeed != seed_b)
                    return fail("env seq touched game RNG");
                port_audio_unload_inst();
                if (port_audio_env_on())
                    return fail("env after unload");
                if (port_audio_inst_push(0, 0, 127, 60, 0, 127, 100, wave, 128, 0, 0) != 0)
                    return fail("inst push after env");
                if (port_audio_load_seq(k_seq, (uint32_t)sizeof k_seq) != 0)
                    return fail("seq load after env");
                port_audio_cb(pcm, NFRAMES);
                hash_pcm(pcm, hex);
                if (strcmp(hex, wav_hex) != 0)
                    return fail("wav after env unload");
                printf("envelope seq distinct=1 restore=1\n");
            }

            {
                char pan_hex[65];
                int li, lsum, rsum;

                port_audio_unload_inst();
                if (port_audio_pan_on())
                    return fail("pan on before set");
                if (port_audio_inst_push(0, 0, 127, 60, 0, 127, 100, wave, 128, 0, 0) != 0)
                    return fail("inst push pan");
                if (port_audio_pan_on())
                    return fail("pan default");
                port_audio_inst_set_pan(64);
                if (port_audio_pan_on())
                    return fail("pan center set");
                if (port_audio_load_seq(k_seq, (uint32_t)sizeof k_seq) != 0)
                    return fail("seq load pan center");
                port_audio_cb(pcm, NFRAMES);
                hash_pcm(pcm, hex);
                if (strcmp(hex, wav_hex) != 0)
                    return fail("center pan changed wav");
                port_audio_inst_set_pan(0);
                if (!port_audio_pan_on())
                    return fail("pan flag");
                if (port_audio_load_seq(k_seq, (uint32_t)sizeof k_seq) != 0)
                    return fail("seq load pan left");
                port_audio_cb(pcm, NFRAMES);
                if (all_zero(pcm))
                    return fail("pan seq silence");
                hash_pcm(pcm, pan_hex);
                if (strcmp(pan_hex, wav_hex) == 0)
                    return fail("left pan matches center wav");
                lsum = 0;
                rsum = 0;
                for (li = 0; li < NFRAMES; li++) {
                    int l = pcm[li * 2];
                    int r = pcm[li * 2 + 1];
                    if (l < 0)
                        l = -l;
                    if (r < 0)
                        r = -r;
                    lsum += l;
                    rsum += r;
                }
                if (rsum != 0)
                    return fail("left pan leaked right");
                if (lsum < 1000)
                    return fail("left pan quiet");
                if (g_randomSeed != seed_a || g_chrObjRandomSeed != seed_b)
                    return fail("pan seq touched game RNG");
                port_audio_unload_inst();
                if (port_audio_pan_on())
                    return fail("pan after unload");
                if (port_audio_inst_push(0, 0, 127, 60, 0, 127, 100, wave, 128, 0, 0) != 0)
                    return fail("inst push after pan");
                if (port_audio_load_seq(k_seq, (uint32_t)sizeof k_seq) != 0)
                    return fail("seq load after pan");
                port_audio_cb(pcm, NFRAMES);
                hash_pcm(pcm, hex);
                if (strcmp(hex, wav_hex) != 0)
                    return fail("wav after pan unload");
                printf("pan seq distinct=1 restore=1\n");
            }

            {
                char det_hex[65];

                port_audio_unload_inst();
                if (port_audio_det_on())
                    return fail("det on before set");
                if (port_audio_inst_push(0, 0, 127, 60, 0, 127, 100, wave, 128, 0, 0) != 0)
                    return fail("inst push det");
                if (port_audio_det_on())
                    return fail("det default");
                port_audio_inst_set_detune(0);
                if (port_audio_det_on())
                    return fail("det zero set");
                if (port_audio_load_seq(k_seq, (uint32_t)sizeof k_seq) != 0)
                    return fail("seq load det zero");
                port_audio_cb(pcm, NFRAMES);
                hash_pcm(pcm, hex);
                if (strcmp(hex, wav_hex) != 0)
                    return fail("zero detune changed wav");
                port_audio_inst_set_detune(40);
                if (!port_audio_det_on())
                    return fail("det flag");
                if (port_audio_load_seq(k_seq, (uint32_t)sizeof k_seq) != 0)
                    return fail("seq load det");
                port_audio_cb(pcm, NFRAMES);
                if (all_zero(pcm))
                    return fail("det seq silence");
                hash_pcm(pcm, det_hex);
                if (strcmp(det_hex, wav_hex) == 0)
                    return fail("detune 40 matches zero wav");
                if (g_randomSeed != seed_a || g_chrObjRandomSeed != seed_b)
                    return fail("det seq touched game RNG");
                port_audio_unload_inst();
                if (port_audio_det_on())
                    return fail("det after unload");
                if (port_audio_inst_push(0, 0, 127, 60, 0, 127, 100, wave, 128, 0, 0) != 0)
                    return fail("inst push after det");
                if (port_audio_load_seq(k_seq, (uint32_t)sizeof k_seq) != 0)
                    return fail("seq load after det");
                port_audio_cb(pcm, NFRAMES);
                hash_pcm(pcm, hex);
                if (strcmp(hex, wav_hex) != 0)
                    return fail("wav after det unload");
                printf("detune seq distinct=1 restore=1\n");
            }

            {
                /* Same one-track seq with pitch bend before the note. */
                static const uint8_t k_seq_bend0[] = {
                    0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0xFF, 0x51, 0x07,
                    0xA1, 0x20, 0x00, 0xE0, 0x00, 0x00, 0x00, 0x90, 0x3C, 0x64, 0x60, 0x00,
                    0xFF, 0x2F,
                };
                static const uint8_t k_seq_bendc[] = {
                    0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0xFF, 0x51, 0x07,
                    0xA1, 0x20, 0x00, 0xE0, 0x00, 0x40, 0x00, 0x90, 0x3C, 0x64, 0x60, 0x00,
                    0xFF, 0x2F,
                };
                char bend_hex[65];
                char wide_hex[65];

                port_audio_unload_inst();
                if (port_audio_bend_on())
                    return fail("bend on before seq");
                if (port_audio_load_seq(k_seq_bendc, (uint32_t)sizeof k_seq_bendc) != 0)
                    return fail("seq load bend center tri");
                port_audio_cb(pcm, NFRAMES);
                hash_pcm(pcm, hex);
                if (strcmp(hex, seq_hex) != 0)
                    return fail("center bend changed triangle seq");
                if (port_audio_bend_on())
                    return fail("center bend flag");
                if (port_audio_load_seq(k_seq_bend0, (uint32_t)sizeof k_seq_bend0) != 0)
                    return fail("seq load bend down tri");
                port_audio_cb(pcm, NFRAMES);
                if (all_zero(pcm))
                    return fail("bend tri silence");
                hash_pcm(pcm, bend_hex);
                if (strcmp(bend_hex, seq_hex) == 0)
                    return fail("down bend matches triangle seq");
                if (!port_audio_bend_on())
                    return fail("bend flag tri");
                if (g_randomSeed != seed_a || g_chrObjRandomSeed != seed_b)
                    return fail("bend tri touched game RNG");

                if (port_audio_inst_push(0, 0, 127, 60, 0, 127, 100, wave, 128, 0, 0) != 0)
                    return fail("inst push bend");
                if (port_audio_load_seq(k_seq_bendc, (uint32_t)sizeof k_seq_bendc) != 0)
                    return fail("seq load bend center wav");
                port_audio_cb(pcm, NFRAMES);
                hash_pcm(pcm, hex);
                if (strcmp(hex, wav_hex) != 0)
                    return fail("center bend changed wav");
                if (port_audio_bend_on())
                    return fail("center wav bend flag");
                if (port_audio_load_seq(k_seq_bend0, (uint32_t)sizeof k_seq_bend0) != 0)
                    return fail("seq load bend down wav");
                port_audio_cb(pcm, NFRAMES);
                if (all_zero(pcm))
                    return fail("bend wav silence");
                hash_pcm(pcm, bend_hex);
                if (strcmp(bend_hex, wav_hex) == 0)
                    return fail("down bend matches center wav");
                if (!port_audio_bend_on())
                    return fail("bend flag wav");
                port_audio_inst_set_bend_range(1200);
                if (port_audio_load_seq(k_seq_bend0, (uint32_t)sizeof k_seq_bend0) != 0)
                    return fail("seq load bend 1200");
                port_audio_cb(pcm, NFRAMES);
                hash_pcm(pcm, wide_hex);
                if (strcmp(wide_hex, bend_hex) == 0)
                    return fail("bendRange 1200 matches 200");
                if (g_randomSeed != seed_a || g_chrObjRandomSeed != seed_b)
                    return fail("bend wav touched game RNG");
                port_audio_unload_inst();
                if (port_audio_load_seq(k_seq, (uint32_t)sizeof k_seq) != 0)
                    return fail("seq load after bend");
                port_audio_cb(pcm, NFRAMES);
                hash_pcm(pcm, hex);
                if (strcmp(hex, seq_hex) != 0)
                    return fail("triangle after bend unload");
                if (port_audio_inst_push(0, 0, 127, 60, 0, 127, 100, wave, 128, 0, 0) != 0)
                    return fail("inst push after bend");
                if (port_audio_load_seq(k_seq, (uint32_t)sizeof k_seq) != 0)
                    return fail("seq load wav after bend");
                port_audio_cb(pcm, NFRAMES);
                hash_pcm(pcm, hex);
                if (strcmp(hex, wav_hex) != 0)
                    return fail("wav after bend unload");
                printf("bend seq distinct=1 restore=1\n");
            }

            {
                /* Same one-track seq with CC10=0 before the note. */
                static const uint8_t k_seq_ccpan[] = {
                    0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0xFF, 0x51, 0x07,
                    0xA1, 0x20, 0x00, 0xB0, 0x0A, 0x00, 0x00, 0x90, 0x3C, 0x64, 0x60, 0x00,
                    0xFF, 0x2F,
                };
                int li, rsum;

                port_audio_unload_inst();
                if (port_audio_load_seq(k_seq_ccpan, (uint32_t)sizeof k_seq_ccpan) != 0)
                    return fail("seq load cc10");
                port_audio_cb(pcm, NFRAMES);
                if (all_zero(pcm))
                    return fail("cc10 seq silence");
                rsum = 0;
                for (li = 0; li < NFRAMES; li++) {
                    int r = pcm[li * 2 + 1];
                    if (r < 0)
                        r = -r;
                    rsum += r;
                }
                if (rsum != 0)
                    return fail("cc10 pan 0 leaked right");
                printf("cc10 pan left=1\n");
            }

            {
                /* CC7=1 after the note is on (16 ticks ~3675 frames). */
                static const uint8_t k_seq_cc7live[] = {
                    0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0xFF, 0x51, 0x07,
                    0xA1, 0x20, 0x00, 0x90, 0x3C, 0x64, 0x60, 0x10, 0xB0, 0x07, 0x01, 0x00,
                    0xFF, 0x2F,
                };
                static const uint8_t k_seq_cc7pre[] = {
                    0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0xFF, 0x51, 0x07,
                    0xA1, 0x20, 0x00, 0xB0, 0x07, 0x01, 0x00, 0x90, 0x3C, 0x64, 0x60, 0x00,
                    0xFF, 0x2F,
                };
                char live_hex[65];
                char pre_hex[65];
                int li, eabs, labs;

                port_audio_unload_inst();
                if (port_audio_load_seq(k_seq, (uint32_t)sizeof k_seq) != 0)
                    return fail("seq load cc7 base");
                port_audio_cb(pcm, NFRAMES);
                hash_pcm(pcm, hex);
                if (strcmp(hex, seq_hex) != 0)
                    return fail("cc7 base changed seq");
                if (port_audio_load_seq(k_seq_cc7pre, (uint32_t)sizeof k_seq_cc7pre) != 0)
                    return fail("seq load cc7 pre");
                port_audio_cb(pcm, NFRAMES);
                if (all_zero(pcm))
                    return fail("cc7 pre silence");
                hash_pcm(pcm, pre_hex);
                if (strcmp(pre_hex, seq_hex) == 0)
                    return fail("cc7 pre matches default vol");
                if (port_audio_load_seq(k_seq_cc7live, (uint32_t)sizeof k_seq_cc7live) != 0)
                    return fail("seq load cc7 live");
                port_audio_cb(pcm, NFRAMES);
                if (all_zero(pcm))
                    return fail("cc7 live silence");
                hash_pcm(pcm, live_hex);
                if (strcmp(live_hex, seq_hex) == 0)
                    return fail("cc7 live matches default vol");
                if (strcmp(live_hex, pre_hex) == 0)
                    return fail("cc7 live matches pre");
                eabs = 0;
                labs = 0;
                for (li = 0; li < 2000; li++) {
                    int s = pcm[li * 2];
                    if (s < 0)
                        s = -s;
                    eabs += s;
                }
                for (li = 3800; li < NFRAMES; li++) {
                    int s = pcm[li * 2];
                    if (s < 0)
                        s = -s;
                    labs += s;
                }
                if (eabs < 1000)
                    return fail("cc7 live early quiet");
                if (labs >= eabs)
                    return fail("cc7 live did not drop");
                if (g_randomSeed != seed_a || g_chrObjRandomSeed != seed_b)
                    return fail("cc7 live touched game RNG");
                printf("cc7 live distinct=1 drop=1\n");
            }

            {
                /* CC10=0 after the note is on. Early frames keep both channels. */
                static const uint8_t k_seq_cc10live[] = {
                    0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0xFF, 0x51, 0x07,
                    0xA1, 0x20, 0x00, 0x90, 0x3C, 0x64, 0x60, 0x10, 0xB0, 0x0A, 0x00, 0x00,
                    0xFF, 0x2F,
                };
                int li, r_early, r_late;

                port_audio_unload_inst();
                if (port_audio_load_seq(k_seq_cc10live, (uint32_t)sizeof k_seq_cc10live) != 0)
                    return fail("seq load cc10 live");
                port_audio_cb(pcm, NFRAMES);
                if (all_zero(pcm))
                    return fail("cc10 live silence");
                r_early = 0;
                r_late = 0;
                for (li = 0; li < 2000; li++) {
                    int r = pcm[li * 2 + 1];
                    if (r < 0)
                        r = -r;
                    r_early += r;
                }
                for (li = 3800; li < NFRAMES; li++) {
                    int r = pcm[li * 2 + 1];
                    if (r < 0)
                        r = -r;
                    r_late += r;
                }
                if (r_early < 1000)
                    return fail("cc10 live early right quiet");
                if (r_late != 0)
                    return fail("cc10 live late leaked right");
                if (g_randomSeed != seed_a || g_chrObjRandomSeed != seed_b)
                    return fail("cc10 live touched game RNG");
                printf("cc10 live distinct=1\n");
            }
        }
    }

    port_audio_cb(NULL, 16);
    port_audio_cb(pcm, 0);
    port_audio_shutdown();
    printf("audio ok\n");
    return 0;
}
