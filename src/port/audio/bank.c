#include "audio/audio.h"
#include "fs/pack_dma.h"

#include <stdlib.h>
#include <string.h>

/*
 * Decode pack sfx.ctl / sfx.tbl VADPCM one-shots. Not ASP HLE — no
 * envelopes, pitch, or RSP mixer. Gun / dry / door / body-fall / hit /
 * KF7 bolt / pickup / door-close / wall ricochet / ammo crate / armour /
 * rifle-cock reload / male yelp / Bond hurt.
 *
 * SFX_ID n is ALInstrument.soundArray[n-1] (sndPlaySfx skips 0).
 * GET_HIT_MALE0–24 (134–158) cycle like Rare male_guard_yelp_counter.
 * BODY_FALL_C1–E3 + BODY_ROLLOVER (123–133) cycle like Rare thud_index.
 */

#define PACK_SFX_EMPTY_GUN_FIRE 89
#define PACK_SFX_PP7 107 /* GUN_B2_HEAVY / PPK */
#define PACK_SFX_KF7 109 /* GUN_B4_BOLTACTION / AK47 */
#define PACK_SFX_PICKUP_GUN 232
#define PACK_SFX_PICKUP_AMMO 234
#define PACK_SFX_ARMOUR_COLLECT 81
#define PACK_SFX_GUN_RIFLECOCK 50 /* reload animation */
#define PACK_SFX_DOOR_METAL_OPEN 196
#define PACK_SFX_DOOR_METAL_CLOSE 197
#define PACK_SFX_BODY_FALL_C1 123 /* body_hit_SFX[0]; 123–133 wrap at 11 */
#define PACK_SFX_BODY_FALL_N 11
#define PACK_SFX_HIT_FLESH 69 /* HIT_BULLET_FLESH */
#define PACK_SFX_RICO_8_AFDM_A 27 /* ricochet_sounds_small */
#define PACK_SFX_BOND_GET_HIT1 68 /* BOND_GET_HIT1 */
#define PACK_SFX_GET_HIT_MALE0 134 /* GET_HIT_MALE0 */
#define PACK_SFX_GET_HIT_MALE_N 25
#define PACK_SFX_MAX_SAMPLES 44100u
#define PACK_SFX_MAX_BOOK (8 * 2 * 8)

static int16_t *g_owned[15];
static int16_t *g_yelp_owned[PACK_SFX_GET_HIT_MALE_N];
static int16_t *g_fall_owned[PACK_SFX_BODY_FALL_N];

__attribute__((weak)) const C0Pack *port_pack(void)
{
    return NULL;
}

__attribute__((weak)) const C0PackEntry *c0pack_find(const C0Pack *p, const char *path)
{
    (void)p;
    (void)path;
    return NULL;
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int16_t bes16(const uint8_t *p)
{
    return (int16_t)be16(p);
}

static int32_t bes32(const uint8_t *p)
{
    return (int32_t)be32(p);
}

static void drop_kind(int kind)
{
    if (kind < 1 || kind > PORT_SFX_HURT)
        return;
    port_audio_install_sfx(kind, NULL, 0, 0);
    free(g_owned[kind]);
    g_owned[kind] = NULL;
}

static void drop_yelps(void)
{
    int i;
    port_audio_clear_yelps();
    for (i = 0; i < PACK_SFX_GET_HIT_MALE_N; i++) {
        free(g_yelp_owned[i]);
        g_yelp_owned[i] = NULL;
    }
}

static void drop_falls(void)
{
    int i;
    port_audio_clear_falls();
    for (i = 0; i < PACK_SFX_BODY_FALL_N; i++) {
        free(g_fall_owned[i]);
        g_fall_owned[i] = NULL;
    }
}

void port_audio_unload_pack_sfx(void)
{
    drop_kind(PORT_SFX_GUN);
    drop_kind(PORT_SFX_DRY);
    drop_kind(PORT_SFX_DOOR);
    drop_kind(PORT_SFX_FALL);
    drop_falls();
    drop_kind(PORT_SFX_HIT);
    drop_kind(PORT_SFX_KF7);
    drop_kind(PORT_SFX_PICKUP);
    drop_kind(PORT_SFX_DOOR_CLOSE);
    drop_kind(PORT_SFX_RICO);
    drop_kind(PORT_SFX_AMMO);
    drop_kind(PORT_SFX_ARMOUR);
    drop_kind(PORT_SFX_RELOAD);
    drop_kind(PORT_SFX_YELP);
    drop_yelps();
    drop_kind(PORT_SFX_HURT);
}

static int decode_id_pcm(const uint8_t *ctl, uint32_t ctl_n, const uint8_t *tbl,
                         uint32_t tbl_n, int sfx_id, int16_t **out_pcm, uint32_t *out_n,
                         uint8_t *out_vol)
{
    uint32_t bank, inst, sound, wave, book_off, base, src_n, i;
    int16_t book[PACK_SFX_MAX_BOOK];
    int16_t *pcm;
    int order, npred, nbook, ns, vol;
    uint32_t scount, sound_off;

    if (out_pcm)
        *out_pcm = NULL;
    if (out_n)
        *out_n = 0;
    if (out_vol)
        *out_vol = 0;
    if (sfx_id < 1)
        return -1;
    if (ctl_n < 8 || be16(ctl) != 0x4231 || be16(ctl + 2) < 1)
        return -1;
    bank = be32(ctl + 4);
    if (bank + 16 > ctl_n)
        return -1;
    if (bes16(ctl + bank) < 1)
        return -1;
    inst = be32(ctl + bank + 12);
    if (inst + 20 > ctl_n)
        return -1;
    scount = (uint32_t)(uint16_t)bes16(ctl + inst + 14);
    if ((uint32_t)sfx_id > scount)
        return -1;
    sound_off = inst + 16u + 4u * (uint32_t)(sfx_id - 1);
    if (sound_off + 4 > ctl_n)
        return -1;
    sound = be32(ctl + sound_off);
    if (sound + 16 > ctl_n)
        return -1;
    vol = (int)ctl[sound + 13];
    if (vol < 1)
        vol = 127;
    if (vol > 127)
        vol = 127;
    wave = be32(ctl + sound + 8);
    if (wave + 20 > ctl_n)
        return -1;
    if (ctl[wave + 8] != 0) /* AL_ADPCM_WAVE */
        return -1;
    base = be32(ctl + wave);
    src_n = (uint32_t)bes32(ctl + wave + 4);
    book_off = be32(ctl + wave + 16);
    if (book_off + 8 > ctl_n)
        return -1;
    if (base > tbl_n || src_n > tbl_n - base || src_n < 9)
        return -1;
    order = bes32(ctl + book_off);
    npred = bes32(ctl + book_off + 4);
    if (order != 2 || npred < 1 || npred > 8)
        return -1;
    nbook = npred * order * 8;
    if (nbook > (int)PACK_SFX_MAX_BOOK)
        return -1;
    if (book_off + 8u + (uint32_t)nbook * 2u > ctl_n)
        return -1;
    for (i = 0; i < (uint32_t)nbook; i++)
        book[i] = bes16(ctl + book_off + 8u + i * 2u);

    pcm = (int16_t *)malloc((size_t)PACK_SFX_MAX_SAMPLES * sizeof(int16_t));
    if (!pcm)
        return -1;
    ns = port_audio_adpcm_decode(tbl + base, src_n, book, order, npred, pcm,
                                 PACK_SFX_MAX_SAMPLES);
    if (ns < 64) {
        free(pcm);
        return -1;
    }
    if (out_pcm)
        *out_pcm = pcm;
    else
        free(pcm);
    if (out_n)
        *out_n = (uint32_t)ns;
    if (out_vol)
        *out_vol = (uint8_t)vol;
    return 0;
}

static int decode_id(const uint8_t *ctl, uint32_t ctl_n, const uint8_t *tbl,
                     uint32_t tbl_n, int sfx_id, int kind)
{
    int16_t *pcm;
    uint32_t ns;
    uint8_t vol;

    if (decode_id_pcm(ctl, ctl_n, tbl, tbl_n, sfx_id, &pcm, &ns, &vol) != 0)
        return -1;
    drop_kind(kind);
    g_owned[kind] = pcm;
    port_audio_install_sfx(kind, pcm, ns, vol);
    return 0;
}

int port_audio_load_pack_sfx(void)
{
    const C0Pack *pack;
    const C0PackEntry *ctl;
    const C0PackEntry *tbl;
    int n = 0;

    port_audio_unload_pack_sfx();
    pack = port_pack();
    if (!pack)
        return 0;
    ctl = c0pack_find(pack, "assets/music/sfx.ctl");
    tbl = c0pack_find(pack, "assets/music/sfx.tbl");
    if (!ctl || !tbl || !ctl->bytes || !tbl->bytes || ctl->size < 64 || tbl->size < 64)
        return 0;
    if (decode_id(ctl->bytes, ctl->size, tbl->bytes, tbl->size, PACK_SFX_PP7,
                  PORT_SFX_GUN) == 0)
        n++;
    if (decode_id(ctl->bytes, ctl->size, tbl->bytes, tbl->size, PACK_SFX_EMPTY_GUN_FIRE,
                  PORT_SFX_DRY) == 0)
        n++;
    if (decode_id(ctl->bytes, ctl->size, tbl->bytes, tbl->size, PACK_SFX_DOOR_METAL_OPEN,
                  PORT_SFX_DOOR) == 0)
        n++;
    {
        int fi, got = 0;
        for (fi = 0; fi < PACK_SFX_BODY_FALL_N; fi++) {
            int16_t *pcm = NULL;
            uint32_t ns = 0;
            uint8_t vol = 0;
            if (decode_id_pcm(ctl->bytes, ctl->size, tbl->bytes, tbl->size,
                              PACK_SFX_BODY_FALL_C1 + fi, &pcm, &ns, &vol) != 0)
                continue;
            g_fall_owned[got] = pcm;
            port_audio_push_fall(pcm, ns, vol);
            if (got == 0)
                port_audio_install_sfx(PORT_SFX_FALL, pcm, ns, vol);
            got++;
        }
        if (got > 0)
            n++;
    }
    if (decode_id(ctl->bytes, ctl->size, tbl->bytes, tbl->size, PACK_SFX_HIT_FLESH,
                  PORT_SFX_HIT) == 0)
        n++;
    if (decode_id(ctl->bytes, ctl->size, tbl->bytes, tbl->size, PACK_SFX_KF7,
                  PORT_SFX_KF7) == 0)
        n++;
    if (decode_id(ctl->bytes, ctl->size, tbl->bytes, tbl->size, PACK_SFX_PICKUP_GUN,
                  PORT_SFX_PICKUP) == 0)
        n++;
    if (decode_id(ctl->bytes, ctl->size, tbl->bytes, tbl->size, PACK_SFX_DOOR_METAL_CLOSE,
                  PORT_SFX_DOOR_CLOSE) == 0)
        n++;
    if (decode_id(ctl->bytes, ctl->size, tbl->bytes, tbl->size, PACK_SFX_RICO_8_AFDM_A,
                  PORT_SFX_RICO) == 0)
        n++;
    if (decode_id(ctl->bytes, ctl->size, tbl->bytes, tbl->size, PACK_SFX_PICKUP_AMMO,
                  PORT_SFX_AMMO) == 0)
        n++;
    if (decode_id(ctl->bytes, ctl->size, tbl->bytes, tbl->size, PACK_SFX_ARMOUR_COLLECT,
                  PORT_SFX_ARMOUR) == 0)
        n++;
    if (decode_id(ctl->bytes, ctl->size, tbl->bytes, tbl->size, PACK_SFX_GUN_RIFLECOCK,
                  PORT_SFX_RELOAD) == 0)
        n++;
    {
        int yi, got = 0;
        for (yi = 0; yi < PACK_SFX_GET_HIT_MALE_N; yi++) {
            int16_t *pcm = NULL;
            uint32_t ns = 0;
            uint8_t vol = 0;
            if (decode_id_pcm(ctl->bytes, ctl->size, tbl->bytes, tbl->size,
                              PACK_SFX_GET_HIT_MALE0 + yi, &pcm, &ns, &vol) != 0)
                continue;
            g_yelp_owned[got] = pcm;
            port_audio_push_yelp(pcm, ns, vol);
            if (got == 0)
                port_audio_install_sfx(PORT_SFX_YELP, pcm, ns, vol);
            got++;
        }
        if (got > 0)
            n++;
    }
    if (decode_id(ctl->bytes, ctl->size, tbl->bytes, tbl->size, PACK_SFX_BOND_GET_HIT1,
                  PORT_SFX_HURT) == 0)
        n++;
    return n;
}
