#include "audio/audio.h"

#include <string.h>

#define AI_SLOTS 2
#define AI_SLOT_BYTES 16384
#define MUSIC_HZ0 196u
#define MUSIC_HZ1 294u
#define MUSIC_AMP 1600
#define GUN_HZ 140u
#define GUN_NOISE_HZ 1800u
#define GUN_AMP 11000
#define GUN_LEN ((PORT_AUDIO_RATE * 140u) / 1000u)
#define GUN_CRACK ((PORT_AUDIO_RATE * 28u) / 1000u)
#define DRY_HZ 2100u
#define DRY_AMP 7000
#define DRY_LEN ((PORT_AUDIO_RATE * 22u) / 1000u)
#define DOOR_HZ 78u
#define DOOR_AMP 8000
#define DOOR_LEN ((PORT_AUDIO_RATE * 160u) / 1000u)
#define FALL_HZ 90u
#define FALL_AMP 9000
#define FALL_LEN ((PORT_AUDIO_RATE * 220u) / 1000u)
#define HIT_HZ 1600u
#define HIT_AMP 8000
#define HIT_LEN ((PORT_AUDIO_RATE * 50u) / 1000u)
#define KF7_HZ 110u
#define KF7_AMP 12000
#define PICKUP_HZ 880u
#define PICKUP_AMP 8000
#define PICKUP_LEN ((PORT_AUDIO_RATE * 80u) / 1000u)
#define DOOR_CLOSE_HZ 62u
#define SFX_KIND_MAX 8

#define CLAMP16(x) \
    ((int16_t)((x) > 32767 ? 32767 : ((x) < -32768 ? -32768 : (x))))

static int g_inited;
static volatile int g_music_on;
static volatile int g_sfx_kind;
static volatile int g_last_sfx;
static uint32_t g_freq = PORT_AUDIO_RATE;
static const int16_t *g_sfx_pcm[SFX_KIND_MAX + 1];
static uint32_t g_sfx_pcm_n[SFX_KIND_MAX + 1];
static uint8_t g_sfx_pcm_vol[SFX_KIND_MAX + 1];
static uint32_t g_sfx_pcm_pos;
static int g_sfx_use_pcm;
static int g_ov_kind;
static uint32_t g_ov_left;
static uint32_t g_ov_len;
static uint32_t g_ov_pos;
static uint32_t g_ov_phase;
static int g_ov_use_pcm;
static int g_hit_kind;
static uint32_t g_hit_left;
static uint32_t g_hit_len;
static uint32_t g_hit_pos;
static uint32_t g_hit_phase;
static int g_hit_use_pcm;

static uint32_t g_music_phase0;
static uint32_t g_music_phase1;
static uint32_t g_music_t;
static uint32_t g_sfx_phase;
static uint32_t g_sfx_noise;
static uint32_t g_sfx_left;
static uint32_t g_sfx_len;

static uint8_t g_ai_buf[AI_SLOTS][AI_SLOT_BYTES];
static uint32_t g_ai_len[AI_SLOTS];
static uint32_t g_ai_pos[AI_SLOTS];
static int g_ai_full[AI_SLOTS];
static int g_ai_cur = -1;

static uint32_t phase_inc(uint32_t hz)
{
    return (uint32_t)(((uint64_t)hz << 32) / (uint64_t)PORT_AUDIO_RATE);
}

/* Triangle in -amp..amp. Used only when pack VADPCM is not installed. */
static int osc_tri(uint32_t phase, int amp)
{
    uint32_t u = phase >> 16;
    int t;
    if (u < 32768u)
        t = (int)u * 2 - 32768;
    else
        t = 98304 - (int)u * 2;
    return (t * amp) / 32768;
}

/* Deterministic crackle. Independent of the game RNG. */
static int osc_noise(uint32_t *state, int amp)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    if (x == 0)
        x = 0xA5A5A5A5u;
    *state = x;
    return ((int)(x & 65535u) - 32768) * amp / 32768;
}

static void ai_reset(void)
{
    int i;
    for (i = 0; i < AI_SLOTS; i++) {
        g_ai_len[i] = 0;
        g_ai_pos[i] = 0;
        g_ai_full[i] = 0;
    }
    g_ai_cur = -1;
}

static void dma_promote(int i)
{
    g_ai_full[i] = 0;
    g_ai_len[i] = 0;
    g_ai_pos[i] = 0;
    g_ai_cur = g_ai_full[i ^ 1] ? (i ^ 1) : -1;
}

static void dma_pop(int *l, int *r)
{
    int i;
    int16_t s[2];

    *l = 0;
    *r = 0;
    if (g_ai_cur < 0)
        return;
    i = g_ai_cur;
    if (!g_ai_full[i] || g_ai_pos[i] + 4 > g_ai_len[i]) {
        dma_promote(i);
        if (g_ai_cur < 0)
            return;
        i = g_ai_cur;
        if (g_ai_pos[i] + 4 > g_ai_len[i])
            return;
    }
    memcpy(s, g_ai_buf[i] + g_ai_pos[i], 4);
    *l = s[0];
    *r = s[1];
    g_ai_pos[i] += 4;
    if (g_ai_pos[i] + 4 > g_ai_len[i])
        dma_promote(i);
}

void port_audio_init(void)
{
    g_music_on = 0;
    g_sfx_kind = 0;
    g_last_sfx = 0;
    g_freq = PORT_AUDIO_RATE;
    g_music_phase0 = 0;
    g_music_phase1 = 0;
    g_music_t = 0;
    g_sfx_phase = 0;
    g_sfx_noise = 0xC0FFEEu;
    g_sfx_left = 0;
    g_sfx_len = 0;
    g_sfx_pcm_pos = 0;
    g_sfx_use_pcm = 0;
    g_ov_kind = 0;
    g_ov_left = 0;
    g_ov_len = 0;
    g_ov_pos = 0;
    g_ov_phase = 0;
    g_ov_use_pcm = 0;
    g_hit_kind = 0;
    g_hit_left = 0;
    g_hit_len = 0;
    g_hit_pos = 0;
    g_hit_phase = 0;
    g_hit_use_pcm = 0;
    ai_reset();
    g_inited = 1;
}

void port_audio_shutdown(void)
{
    port_audio_init();
    g_inited = 0;
}

uint32_t port_audio_rate(void)
{
    return PORT_AUDIO_RATE;
}

int port_audio_music_on(void)
{
    return g_music_on != 0;
}

void port_audio_set_placeholder_music(int on)
{
    g_music_on = on ? 1 : 0;
}

static uint32_t placeholder_len(int kind)
{
    if (kind == PORT_SFX_DRY)
        return DRY_LEN;
    if (kind == PORT_SFX_DOOR)
        return DOOR_LEN;
    if (kind == PORT_SFX_FALL)
        return FALL_LEN;
    if (kind == PORT_SFX_HIT)
        return HIT_LEN;
    if (kind == PORT_SFX_PICKUP)
        return PICKUP_LEN;
    if (kind == PORT_SFX_DOOR_CLOSE)
        return DOOR_LEN;
    return GUN_LEN;
}

static void queue_sfx(int kind, uint32_t len)
{
    g_sfx_kind = kind;
    g_last_sfx = kind;
    g_sfx_phase = 0;
    g_sfx_noise = 0xC0FFEEu ^ ((uint32_t)kind * 0x9E3779B9u);
    g_sfx_pcm_pos = 0;
    if (kind >= 1 && kind <= SFX_KIND_MAX && g_sfx_pcm[kind] && g_sfx_pcm_n[kind] > 0) {
        g_sfx_use_pcm = 1;
        g_sfx_left = g_sfx_pcm_n[kind];
        g_sfx_len = g_sfx_pcm_n[kind];
    } else {
        g_sfx_use_pcm = 0;
        g_sfx_left = len;
        g_sfx_len = len;
    }
}

void port_audio_play_gun(void)
{
    queue_sfx(PORT_SFX_GUN, GUN_LEN);
}

void port_audio_play_dry(void)
{
    queue_sfx(PORT_SFX_DRY, DRY_LEN);
}

void port_audio_play_door(void)
{
    queue_sfx(PORT_SFX_DOOR, DOOR_LEN);
}

static void queue_overlay(int kind, uint32_t len)
{
    g_ov_kind = kind;
    g_last_sfx = kind;
    g_ov_phase = 0;
    g_ov_pos = 0;
    if (kind >= 1 && kind <= SFX_KIND_MAX && g_sfx_pcm[kind] && g_sfx_pcm_n[kind] > 0) {
        g_ov_use_pcm = 1;
        g_ov_left = g_sfx_pcm_n[kind];
        g_ov_len = g_sfx_pcm_n[kind];
    } else {
        g_ov_use_pcm = 0;
        g_ov_left = len;
        g_ov_len = len;
    }
}

void port_audio_play_fall(void)
{
    queue_overlay(PORT_SFX_FALL, FALL_LEN);
}

static void queue_hit(int kind, uint32_t len)
{
    g_hit_kind = kind;
    g_last_sfx = kind;
    g_hit_phase = 0;
    g_hit_pos = 0;
    if (kind >= 1 && kind <= SFX_KIND_MAX && g_sfx_pcm[kind] && g_sfx_pcm_n[kind] > 0) {
        g_hit_use_pcm = 1;
        g_hit_left = g_sfx_pcm_n[kind];
        g_hit_len = g_sfx_pcm_n[kind];
    } else {
        g_hit_use_pcm = 0;
        g_hit_left = len;
        g_hit_len = len;
    }
}

void port_audio_play_hit(void)
{
    queue_hit(PORT_SFX_HIT, HIT_LEN);
}

void port_audio_play_kf7(void)
{
    queue_sfx(PORT_SFX_KF7, GUN_LEN);
}

void port_audio_play_pickup(void)
{
    queue_sfx(PORT_SFX_PICKUP, PICKUP_LEN);
}

void port_audio_play_door_close(void)
{
    queue_sfx(PORT_SFX_DOOR_CLOSE, DOOR_LEN);
}

int port_audio_last_sfx(void)
{
    return g_last_sfx;
}

void port_audio_install_sfx(int kind, const int16_t *pcm, uint32_t n, uint8_t vol)
{
    if (kind < 1 || kind > SFX_KIND_MAX)
        return;
    if (g_sfx_kind == kind) {
        g_sfx_left = 0;
        g_sfx_use_pcm = 0;
    }
    g_sfx_pcm[kind] = (pcm && n > 0) ? pcm : 0;
    g_sfx_pcm_n[kind] = (pcm && n > 0) ? n : 0;
    g_sfx_pcm_vol[kind] = vol ? vol : 127u;
}

int port_audio_sfx_frames(int kind)
{
    if (kind < 1 || kind > SFX_KIND_MAX)
        return 0;
    if (g_sfx_pcm[kind] && g_sfx_pcm_n[kind] > 0)
        return (int)g_sfx_pcm_n[kind];
    return (int)placeholder_len(kind);
}

int port_audio_sfx_from_bank(int kind)
{
    return (kind >= 1 && kind <= SFX_KIND_MAX && g_sfx_pcm[kind] && g_sfx_pcm_n[kind] > 0) ? 1
                                                                                         : 0;
}

int port_audio_bank_ready(void)
{
    return port_audio_sfx_from_bank(PORT_SFX_GUN) &&
           port_audio_sfx_from_bank(PORT_SFX_DRY) &&
           port_audio_sfx_from_bank(PORT_SFX_DOOR);
}

int port_audio_adpcm_decode(const uint8_t *src, uint32_t src_bytes,
                            const int16_t *book, int order, int npredictors,
                            int16_t *out, uint32_t out_max)
{
    uint32_t i, nout;
    int l1, l2;

    if (!src || !book || !out || order != 2 || npredictors < 1 || npredictors > 8)
        return -1;
    nout = 0;
    l1 = 0;
    l2 = 0;
    i = 0;
    while (i + 9u <= src_bytes && nout + 16u <= out_max) {
        int shift, index, sub, j;
        const int16_t *coef;
        uint8_t hdr = src[i++];

        shift = (int)(hdr >> 4);
        index = (int)(hdr & 0xFu);
        if (index >= npredictors)
            index = 0;
        coef = book + index * order * 8;
        for (sub = 0; sub < 2; sub++) {
            for (j = 0; j < 8; j++) {
                int nib, sample;
                int64_t acc;
                uint8_t byte = src[i + (uint32_t)sub * 4u + (uint32_t)(j / 2)];
                nib = (j & 1) ? (int)(byte & 0xFu) : (int)(byte >> 4);
                if (nib >= 8)
                    nib -= 16;
                acc = ((int64_t)nib << shift) << 11;
                acc += (int64_t)coef[j] * l1;
                acc += (int64_t)coef[j + 8] * l2;
                sample = (int)(acc >> 11);
                if (sample > 32767)
                    sample = 32767;
                if (sample < -32768)
                    sample = -32768;
                out[nout++] = (int16_t)sample;
                l2 = l1;
                l1 = sample;
            }
        }
        i += 8u;
    }
    return (int)nout;
}

int32_t port_audio_set_frequency(uint32_t frequency)
{
    if (frequency < 4000u || frequency > 96000u)
        return -1;
    g_freq = frequency;
    return (int32_t)g_freq;
}

uint32_t port_audio_ai_frequency(void)
{
    return g_freq;
}

int port_audio_ai_push(const void *buf, uint32_t bytes)
{
    int i;

    if (!buf || bytes < 4)
        return -1;
    if (bytes > AI_SLOT_BYTES)
        bytes = AI_SLOT_BYTES;
    bytes &= ~3u;
    for (i = 0; i < AI_SLOTS; i++) {
        if (!g_ai_full[i]) {
            memcpy(g_ai_buf[i], buf, bytes);
            g_ai_len[i] = bytes;
            g_ai_pos[i] = 0;
            g_ai_full[i] = 1;
            if (g_ai_cur < 0)
                g_ai_cur = i;
            return 0;
        }
    }
    return -1;
}

uint32_t port_audio_ai_length(void)
{
    int i = g_ai_cur;
    if (i < 0 || !g_ai_full[i])
        return 0;
    return g_ai_len[i] - g_ai_pos[i];
}

int port_audio_ai_busy(void)
{
    return g_ai_full[0] && g_ai_full[1];
}

void port_audio_cb(int16_t *stereo, int nframes)
{
    uint32_t inc0 = phase_inc(MUSIC_HZ0);
    uint32_t inc1 = phase_inc(MUSIC_HZ1);
    uint32_t ginc = phase_inc(GUN_HZ);
    uint32_t ninc = phase_inc(GUN_NOISE_HZ);
    uint32_t dinc = phase_inc(DRY_HZ);
    uint32_t oinc = phase_inc(DOOR_HZ);
    uint32_t finc = phase_inc(FALL_HZ);
    uint32_t hinc = phase_inc(HIT_HZ);
    uint32_t kinc = phase_inc(KF7_HZ);
    uint32_t pinc = phase_inc(PICKUP_HZ);
    uint32_t cinc = phase_inc(DOOR_CLOSE_HZ);
    int i;
    int kind = g_sfx_kind;

    if (!stereo || nframes <= 0)
        return;
    if (!g_inited)
        port_audio_init();

    for (i = 0; i < nframes; i++) {
        int acc_l = 0;
        int acc_r = 0;
        int dl, dr;

        dma_pop(&dl, &dr);
        acc_l += dl;
        acc_r += dr;

        if (g_music_on) {
            int s = osc_tri(g_music_phase0, MUSIC_AMP) + osc_tri(g_music_phase1, MUSIC_AMP / 2);
            acc_l += s;
            acc_r += s;
            g_music_phase0 += inc0;
            g_music_phase1 += inc1;
            g_music_t++;
        }

        if (g_sfx_left > 0 && g_sfx_len > 0) {
            int s = 0;
            if (g_sfx_use_pcm && kind >= 1 && kind <= SFX_KIND_MAX && g_sfx_pcm[kind] &&
                g_sfx_pcm_pos < g_sfx_pcm_n[kind]) {
                int vol = (int)g_sfx_pcm_vol[kind];
                int32_t v = ((int32_t)g_sfx_pcm[kind][g_sfx_pcm_pos] * vol) / 127;
                g_sfx_pcm_pos++;
                s = (int)v;
            } else {
                int amp = (int)((uint32_t)((kind == PORT_SFX_DRY) ? DRY_AMP
                        : (kind == PORT_SFX_DOOR || kind == PORT_SFX_DOOR_CLOSE) ? DOOR_AMP
                        : (kind == PORT_SFX_PICKUP) ? PICKUP_AMP
                        : (kind == PORT_SFX_KF7) ? KF7_AMP
                        : GUN_AMP) * g_sfx_left / g_sfx_len);
                if (kind == PORT_SFX_DRY)
                    s = osc_tri(g_sfx_phase, amp);
                else if (kind == PORT_SFX_DOOR || kind == PORT_SFX_DOOR_CLOSE)
                    s = osc_tri(g_sfx_phase, amp);
                else if (kind == PORT_SFX_PICKUP)
                    s = osc_tri(g_sfx_phase, amp);
                else {
                    uint32_t spent = g_sfx_len - g_sfx_left;
                    s = osc_tri(g_sfx_phase, amp / 2);
                    if (spent < GUN_CRACK)
                        s += osc_noise(&g_sfx_noise, amp);
                }
                if (kind == PORT_SFX_DRY)
                    g_sfx_phase += dinc;
                else if (kind == PORT_SFX_DOOR)
                    g_sfx_phase += oinc;
                else if (kind == PORT_SFX_DOOR_CLOSE)
                    g_sfx_phase += cinc;
                else if (kind == PORT_SFX_PICKUP)
                    g_sfx_phase += pinc;
                else if (kind == PORT_SFX_KF7)
                    g_sfx_phase += kinc + ninc / 8u;
                else
                    g_sfx_phase += ginc + ninc / 8u;
            }
            acc_l += s;
            acc_r += s;
            g_sfx_left--;
        }

        if (g_ov_left > 0 && g_ov_len > 0) {
            int s = 0;
            int okind = g_ov_kind;
            if (g_ov_use_pcm && okind >= 1 && okind <= SFX_KIND_MAX && g_sfx_pcm[okind] &&
                g_ov_pos < g_sfx_pcm_n[okind]) {
                int vol = (int)g_sfx_pcm_vol[okind];
                int32_t v = ((int32_t)g_sfx_pcm[okind][g_ov_pos] * vol) / 127;
                g_ov_pos++;
                s = (int)v;
            } else {
                int amp = (int)((uint32_t)FALL_AMP * g_ov_left / g_ov_len);
                s = osc_tri(g_ov_phase, amp);
                g_ov_phase += finc;
            }
            acc_l += s;
            acc_r += s;
            g_ov_left--;
        }

        if (g_hit_left > 0 && g_hit_len > 0) {
            int s = 0;
            int hkind = g_hit_kind;
            if (g_hit_use_pcm && hkind >= 1 && hkind <= SFX_KIND_MAX && g_sfx_pcm[hkind] &&
                g_hit_pos < g_sfx_pcm_n[hkind]) {
                int vol = (int)g_sfx_pcm_vol[hkind];
                int32_t v = ((int32_t)g_sfx_pcm[hkind][g_hit_pos] * vol) / 127;
                g_hit_pos++;
                s = (int)v;
            } else {
                int amp = (int)((uint32_t)HIT_AMP * g_hit_left / g_hit_len);
                s = osc_tri(g_hit_phase, amp) + osc_noise(&g_sfx_noise, amp / 2);
                g_hit_phase += hinc;
            }
            acc_l += s;
            acc_r += s;
            g_hit_left--;
        }

        stereo[i * 2] = CLAMP16(acc_l);
        stereo[i * 2 + 1] = CLAMP16(acc_r);
    }
}
