#include "audio/audio.h"
#include "fs/pack_dma.h"

#include <stdlib.h>
#include <string.h>

/*
 * Decode pack sfx.ctl / sfx.tbl VADPCM one-shots. Not ASP HLE — no
 * envelopes, pitch, or RSP mixer. Gun / dry / door / body-fall / hit /
 * KF7 bolt / pickup / door-close / wall ricochet / ammo crate / armour /
 * rifle-cock reload / male yelp / Bond hurt. Walk steps are a mixer
 * placeholder (GE has no footstep SFX ID); pack install is optional.
 * Music seq may also decode instruments.ctl / instruments.tbl VADPCM
 * into host PCM for pitched loop playback. Still not ASP HLE.
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
#define PACK_INST_WAVES 128
#define PACK_INST_PCM_MAX 65536u
#define PACK_INST_PROGS 80

static int16_t *g_owned[16];
static int16_t *g_yelp_owned[PACK_SFX_GET_HIT_MALE_N];
static int16_t *g_fall_owned[PACK_SFX_BODY_FALL_N];
static int16_t *g_iwave[PACK_INST_WAVES];
static uint32_t g_iwave_n[PACK_INST_WAVES];
static uint32_t g_iwave_base[PACK_INST_WAVES];
static uint32_t g_iwave_srcn[PACK_INST_WAVES];
static uint32_t g_iwave_book[PACK_INST_WAVES];
static int g_niwave;

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
    if (kind < 1 || kind > PORT_SFX_STEP)
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
    drop_kind(PORT_SFX_STEP);
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

void port_audio_unload_pack_instruments(void)
{
    int i;

    port_audio_unload_inst();
    for (i = 0; i < g_niwave; i++) {
        free(g_iwave[i]);
        g_iwave[i] = NULL;
        g_iwave_n[i] = 0;
        g_iwave_base[i] = 0;
        g_iwave_srcn[i] = 0;
        g_iwave_book[i] = 0;
    }
    g_niwave = 0;
}

static int decode_wave_cached(const uint8_t *ctl, uint32_t ctl_n, const uint8_t *tbl,
                              uint32_t tbl_n, uint32_t wave)
{
    uint32_t base, src_n, book_off, i;
    uint32_t ns_cap;
    int16_t book[PACK_SFX_MAX_BOOK];
    int16_t *pcm;
    int order, npred, nbook, ns;
    int slot;

    if (wave + 20 > ctl_n)
        return -1;
    if (ctl[wave + 8] != 0)
        return -1;
    base = be32(ctl + wave);
    src_n = (uint32_t)bes32(ctl + wave + 4);
    book_off = be32(ctl + wave + 16);
    if (book_off + 8 > ctl_n)
        return -1;
    if (base > tbl_n || src_n > tbl_n - base || src_n < 9)
        return -1;
    for (i = 0; i < (uint32_t)g_niwave; i++) {
        if (g_iwave_base[i] == base && g_iwave_srcn[i] == src_n &&
            g_iwave_book[i] == book_off && g_iwave[i])
            return (int)i;
    }
    if (g_niwave >= PACK_INST_WAVES)
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
    ns_cap = (src_n / 9u) * 16u + 16u;
    if (ns_cap > PACK_INST_PCM_MAX)
        ns_cap = PACK_INST_PCM_MAX;
    pcm = (int16_t *)malloc((size_t)ns_cap * sizeof(int16_t));
    if (!pcm)
        return -1;
    ns = port_audio_adpcm_decode(tbl + base, src_n, book, order, npred, pcm, ns_cap);
    if (ns < 32) {
        free(pcm);
        return -1;
    }
    slot = g_niwave++;
    g_iwave[slot] = pcm;
    g_iwave_n[slot] = (uint32_t)ns;
    g_iwave_base[slot] = base;
    g_iwave_srcn[slot] = src_n;
    g_iwave_book[slot] = book_off;
    return slot;
}

static int load_pack_instruments(const uint8_t *ctl, uint32_t ctl_n, const uint8_t *tbl,
                                 uint32_t tbl_n)
{
    uint32_t bank, inst, sound, wave, keymap, loop_off;
    int16_t ninst, scount;
    int i, s, n = 0, wi;
    uint8_t ivol, svol, kmin, kmax, kbase, vmin, vmax, vol;
    uint32_t loop0, loop1, loop_ct;

    port_audio_unload_pack_instruments();
    if (ctl_n < 8 || be16(ctl) != 0x4231 || be16(ctl + 2) < 1)
        return 0;
    bank = be32(ctl + 4);
    if (bank + 16 > ctl_n)
        return 0;
    ninst = bes16(ctl + bank);
    if (ninst < 1 || ninst > PACK_INST_PROGS)
        return 0;
    if (bank + 12u + 4u * (uint32_t)ninst > ctl_n)
        return 0;
    for (i = 0; i < ninst; i++) {
        inst = be32(ctl + bank + 12u + 4u * (uint32_t)i);
        if (inst + 16 > ctl_n)
            continue;
        ivol = ctl[inst];
        if (ivol < 1)
            ivol = 127;
        scount = bes16(ctl + inst + 14);
        if (scount < 1)
            continue;
        if (inst + 16u + 4u * (uint32_t)scount > ctl_n)
            continue;
        for (s = 0; s < scount; s++) {
            sound = be32(ctl + inst + 16u + 4u * (uint32_t)s);
            if (sound + 16 > ctl_n)
                continue;
            keymap = be32(ctl + sound + 4);
            wave = be32(ctl + sound + 8);
            svol = ctl[sound + 13];
            if (svol < 1)
                svol = 127;
            if (keymap + 6 > ctl_n)
                continue;
            vmin = ctl[keymap];
            vmax = ctl[keymap + 1];
            kmin = ctl[keymap + 2];
            kmax = ctl[keymap + 3];
            kbase = ctl[keymap + 4];
            if (kmin > kmax)
                continue;
            wi = decode_wave_cached(ctl, ctl_n, tbl, tbl_n, wave);
            if (wi < 0)
                continue;
            loop0 = 0;
            loop1 = 0;
            loop_off = (wave + 20 <= ctl_n) ? be32(ctl + wave + 12) : 0;
            if (loop_off && loop_off + 12 <= ctl_n) {
                loop0 = be32(ctl + loop_off);
                loop1 = be32(ctl + loop_off + 4);
                loop_ct = be32(ctl + loop_off + 8);
                (void)loop_ct;
                if (loop1 > g_iwave_n[wi])
                    loop1 = g_iwave_n[wi];
                if (loop0 >= loop1) {
                    loop0 = 0;
                    loop1 = 0;
                }
            }
            vol = (uint8_t)(((uint32_t)ivol * (uint32_t)svol) / 127u);
            if (vol < 1)
                vol = 1;
            if (port_audio_inst_push(i, kmin, kmax, kbase, vmin, vmax, vol, g_iwave[wi],
                                     g_iwave_n[wi], loop0, loop1) != 0)
                continue;
            n++;
        }
    }
    return n;
}

int port_audio_load_pack_music(void)
{
    const C0Pack *pack;
    const C0PackEntry *seq;
    const C0PackEntry *ctl;
    const C0PackEntry *tbl;
    int ok;

    port_audio_unload_seq();
    port_audio_unload_pack_instruments();
    pack = port_pack();
    if (!pack)
        return 0;
    seq = c0pack_find(pack, "assets/music/Mfacility.bin");
    if (!seq || !seq->bytes || seq->size < 68)
        return 0;
    ok = port_audio_load_seq(seq->bytes, seq->size) == 0 ? 1 : 0;
    if (!ok)
        return 0;
    ctl = c0pack_find(pack, "assets/music/instruments.ctl");
    tbl = c0pack_find(pack, "assets/music/instruments.tbl");
    if (ctl && tbl && ctl->bytes && tbl->bytes && ctl->size >= 64 && tbl->size >= 64)
        (void)load_pack_instruments(ctl->bytes, ctl->size, tbl->bytes, tbl->size);
    return 1;
}
