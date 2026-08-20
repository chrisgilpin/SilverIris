#include "audio/audio.h"

#include <string.h>

#define AI_SLOTS 2
#define AI_SLOT_BYTES 16384
#define MUSIC_HZ0 196u
#define MUSIC_HZ1 294u
#define MUSIC_AMP 1600
#define GUN_HZ 880u
#define GUN_AMP 9000
#define GUN_LEN ((PORT_AUDIO_RATE * 140u) / 1000u)

#define CLAMP16(x) \
    ((int16_t)((x) > 32767 ? 32767 : ((x) < -32768 ? -32768 : (x))))

static int g_inited;
static volatile int g_music_on;
static volatile int g_gun_fire;
static uint32_t g_freq = PORT_AUDIO_RATE;

static uint32_t g_music_phase0;
static uint32_t g_music_phase1;
static uint32_t g_music_t;
static uint32_t g_gun_phase;
static uint32_t g_gun_left;

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
    g_gun_fire = 0;
    g_freq = PORT_AUDIO_RATE;
    g_music_phase0 = 0;
    g_music_phase1 = 0;
    g_music_t = 0;
    g_gun_phase = 0;
    g_gun_left = 0;
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

void port_audio_play_gun(void)
{
    g_gun_fire = 1;
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
    int i;

    if (!stereo || nframes <= 0)
        return;
    if (!g_inited)
        port_audio_init();

    if (g_gun_fire) {
        g_gun_fire = 0;
        g_gun_left = GUN_LEN;
        g_gun_phase = 0;
    }

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

        if (g_gun_left > 0) {
            int amp = (int)((uint32_t)GUN_AMP * g_gun_left / GUN_LEN);
            int s = osc_tri(g_gun_phase, amp);
            acc_l += s;
            acc_r += s;
            g_gun_phase += ginc;
            g_gun_left--;
        }

        stereo[i * 2] = CLAMP16(acc_l);
        stereo[i * 2 + 1] = CLAMP16(acc_r);
    }
}
