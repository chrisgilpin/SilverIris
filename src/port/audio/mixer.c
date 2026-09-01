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

#define CLAMP16(x) \
    ((int16_t)((x) > 32767 ? 32767 : ((x) < -32768 ? -32768 : (x))))

static int g_inited;
static volatile int g_music_on;
static volatile int g_sfx_kind;
static volatile int g_last_sfx;
static uint32_t g_freq = PORT_AUDIO_RATE;

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

/* Triangle in -amp..amp. Squares buzz; this is the placeholder until bank HLE. */
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

static void queue_sfx(int kind, uint32_t len)
{
    g_sfx_kind = kind;
    g_last_sfx = kind;
    g_sfx_left = len;
    g_sfx_len = len;
    g_sfx_phase = 0;
    g_sfx_noise = 0xC0FFEEu ^ ((uint32_t)kind * 0x9E3779B9u);
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

int port_audio_last_sfx(void)
{
    return g_last_sfx;
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
            int amp = (int)((uint32_t)((kind == PORT_SFX_DRY) ? DRY_AMP
                    : (kind == PORT_SFX_DOOR) ? DOOR_AMP
                    : GUN_AMP) * g_sfx_left / g_sfx_len);
            int s = 0;
            if (kind == PORT_SFX_DRY)
                s = osc_tri(g_sfx_phase, amp);
            else if (kind == PORT_SFX_DOOR)
                s = osc_tri(g_sfx_phase, amp);
            else {
                uint32_t spent = g_sfx_len - g_sfx_left;
                s = osc_tri(g_sfx_phase, amp / 2);
                if (spent < GUN_CRACK)
                    s += osc_noise(&g_sfx_noise, amp);
            }
            acc_l += s;
            acc_r += s;
            if (kind == PORT_SFX_DRY)
                g_sfx_phase += dinc;
            else if (kind == PORT_SFX_DOOR)
                g_sfx_phase += oinc;
            else
                g_sfx_phase += ginc + ninc / 8u;
            g_sfx_left--;
        }

        stereo[i * 2] = CLAMP16(acc_l);
        stereo[i * 2 + 1] = CLAMP16(acc_r);
    }
}
