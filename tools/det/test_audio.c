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

    port_audio_cb(NULL, 16);
    port_audio_cb(pcm, 0);
    port_audio_shutdown();
    printf("audio ok\n");
    return 0;
}
