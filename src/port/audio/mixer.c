#include "audio/audio.h"

#include <stdlib.h>
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
#define RICO_HZ 2400u
#define RICO_AMP 7000
#define RICO_LEN ((PORT_AUDIO_RATE * 90u) / 1000u)
#define KF7_HZ 110u
#define KF7_AMP 12000
#define PICKUP_HZ 880u
#define PICKUP_AMP 8000
#define PICKUP_LEN ((PORT_AUDIO_RATE * 80u) / 1000u)
#define AMMO_HZ 660u
#define ARMOUR_HZ 440u
#define RELOAD_HZ 1180u
#define DOOR_CLOSE_HZ 62u
#define YELP_HZ 310u
#define YELP_AMP 9500
#define YELP_LEN ((PORT_AUDIO_RATE * 180u) / 1000u)
#define HURT_HZ 185u
#define HURT_AMP 8800
#define HURT_LEN ((PORT_AUDIO_RATE * 200u) / 1000u)
/* GE has no footstep SFX ID. Short damped thud on its own mixer voice so
 * walking does not cut gun / door / yelp. Alternate 72/96 Hz for L/R. */
#define STEP_HZ0 72u
#define STEP_HZ1 96u
#define STEP_AMP 5200
#define STEP_LEN ((PORT_AUDIO_RATE * 70u) / 1000u)
#define STEP_CRACK ((PORT_AUDIO_RATE * 14u) / 1000u)
/* Compact MIDI (ALCSeq) walker. Triangle voices, not ASP / wavetable. */
#define SEQ_VOICES 8
#define SEQ_TRACKS 16
#define SEQ_HDR 68u
#define SEQ_AMP 700
#define SEQ_META 0xFFu
#define SEQ_NOTEON 0x90u
#define SEQ_NOTEOFF 0x80u
#define SEQ_POLY 0xA0u
#define SEQ_CC 0xB0u
#define SEQ_PROG 0xC0u
#define SEQ_PRESS 0xD0u
#define SEQ_BEND 0xE0u
#define SEQ_TEMPO 0x51u
#define SEQ_EOT 0x2Fu
#define SEQ_LOOPSTART 0x2Eu
#define SEQ_LOOPEND 0x2Du
#define SEQ_BLOCK 0xFEu
#define SEQ_CC_VOL 7
#define SFX_KIND_MAX 15
#define YELP_VARS 25
#define FALL_VARS 11

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
static const int16_t *g_ov_pcm;
static uint32_t g_ov_pcm_n;
static uint8_t g_ov_vol;
static int g_hit_kind;
static uint32_t g_hit_left;
static uint32_t g_hit_len;
static uint32_t g_hit_pos;
static uint32_t g_hit_phase;
static int g_hit_use_pcm;
static int g_voice_kind;
static uint32_t g_voice_left;
static uint32_t g_voice_len;
static uint32_t g_voice_pos;
static uint32_t g_voice_phase;
static int g_voice_use_pcm;
static const int16_t *g_voice_pcm;
static uint32_t g_voice_pcm_n;
static uint8_t g_voice_vol;
static const int16_t *g_yelp_pcm[YELP_VARS];
static uint32_t g_yelp_n[YELP_VARS];
static uint8_t g_yelp_vol[YELP_VARS];
static int g_nyelp;
static int g_yelp_i;
static const int16_t *g_fall_pcm[FALL_VARS];
static uint32_t g_fall_n[FALL_VARS];
static uint8_t g_fall_vol[FALL_VARS];
static int g_nfall;
static int g_fall_i;
static int g_step_kind;
static uint32_t g_step_left;
static uint32_t g_step_len;
static uint32_t g_step_pos;
static uint32_t g_step_phase;
static int g_step_use_pcm;
static int g_step_foot;
static uint32_t g_step_hz;

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

static uint8_t *g_seq;
static uint32_t g_seq_n;
static int g_seq_on;
static uint32_t g_seq_valid;
static uint32_t g_seq_div;
static uint32_t g_seq_tempo;
static uint32_t g_seq_loc[SEQ_TRACKS];
static uint32_t g_seq_bu[SEQ_TRACKS];
static uint8_t g_seq_bulen[SEQ_TRACKS];
static uint8_t g_seq_status[SEQ_TRACKS];
static uint32_t g_seq_delta[SEQ_TRACKS];
static uint32_t g_seq_last_delta;
static int g_seq_delta_flag;
static uint32_t g_seq_wait;
static int g_seq_pending;
static uint64_t g_seq_frac;
static uint8_t g_seq_vol[SEQ_TRACKS];
static uint32_t g_seq_vleft[SEQ_VOICES];
static uint32_t g_seq_vphase[SEQ_VOICES];
static uint32_t g_seq_vinc[SEQ_VOICES];
static int g_seq_vamp[SEQ_VOICES];
static uint8_t g_seq_vnote[SEQ_VOICES];

/* Rounded 12-TET Hz for MIDI notes 0..127 (A4=440). */
static const uint16_t k_midi_hz[128] = {
    8,    9,    9,    10,   10,   11,   12,   12,   13,   14,   15,   15,   16,   17,
    18,   19,   21,   22,   23,   24,   26,   28,   29,   31,   33,   35,   37,   39,
    41,   44,   46,   49,   52,   55,   58,   62,   65,   69,   73,   78,   82,   87,
    92,   98,   104,  110,  117,  123,  131,  139,  147,  156,  165,  175,  185,  196,
    208,  220,  233,  247,  262,  277,  294,  311,  330,  349,  370,  392,  415,  440,
    466,  494,  523,  554,  587,  622,  659,  698,  740,  784,  831,  880,  932,  988,
    1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661, 1760, 1865, 1976, 2093, 2217,
    2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951, 4186, 4435, 4699, 4978,
    5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902, 8372, 8870, 9397, 9956, 10548,11175,
    11840,12544
};

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
    port_audio_unload_seq();
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
    g_ov_pcm = 0;
    g_ov_pcm_n = 0;
    g_ov_vol = 127u;
    g_hit_kind = 0;
    g_hit_left = 0;
    g_hit_len = 0;
    g_hit_pos = 0;
    g_hit_phase = 0;
    g_hit_use_pcm = 0;
    g_voice_kind = 0;
    g_voice_left = 0;
    g_voice_len = 0;
    g_voice_pos = 0;
    g_voice_phase = 0;
    g_voice_use_pcm = 0;
    g_voice_pcm = 0;
    g_voice_pcm_n = 0;
    g_voice_vol = 127u;
    g_yelp_i = 0;
    g_fall_i = 0;
    g_step_kind = 0;
    g_step_left = 0;
    g_step_len = 0;
    g_step_pos = 0;
    g_step_phase = 0;
    g_step_use_pcm = 0;
    g_step_foot = 0;
    g_step_hz = STEP_HZ0;
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

static uint32_t seq_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void seq_voices_clear(void)
{
    int i;
    for (i = 0; i < SEQ_VOICES; i++) {
        g_seq_vleft[i] = 0;
        g_seq_vphase[i] = 0;
        g_seq_vinc[i] = 0;
        g_seq_vamp[i] = 0;
        g_seq_vnote[i] = 0;
    }
}

static void seq_walker_clear(void)
{
    int i;
    g_seq_on = 0;
    g_seq_valid = 0;
    g_seq_div = 48;
    g_seq_tempo = 500000u;
    g_seq_last_delta = 0;
    g_seq_delta_flag = 1;
    g_seq_wait = 0;
    g_seq_pending = 0;
    g_seq_frac = 0;
    for (i = 0; i < SEQ_TRACKS; i++) {
        g_seq_loc[i] = 0;
        g_seq_bu[i] = 0;
        g_seq_bulen[i] = 0;
        g_seq_status[i] = 0;
        g_seq_delta[i] = 0;
        g_seq_vol[i] = 100u;
    }
    seq_voices_clear();
}

void port_audio_unload_seq(void)
{
    seq_walker_clear();
    free(g_seq);
    g_seq = 0;
    g_seq_n = 0;
}

int port_audio_seq_on(void)
{
    return g_seq_on != 0;
}

static int seq_ok_off(uint32_t off)
{
    return g_seq && off < g_seq_n;
}

static uint8_t seq_get_byte(uint32_t tr)
{
    uint8_t b;
    uint8_t nxt;
    uint32_t backup;
    uint32_t src;

    if (tr >= SEQ_TRACKS || !g_seq)
        return 0;
    if (g_seq_bulen[tr]) {
        if (!seq_ok_off(g_seq_bu[tr])) {
            g_seq_bulen[tr] = 0;
            g_seq_valid &= ~(1u << tr);
            return 0;
        }
        b = g_seq[g_seq_bu[tr]];
        g_seq_bu[tr]++;
        g_seq_bulen[tr]--;
        return b;
    }
    if (!seq_ok_off(g_seq_loc[tr])) {
        g_seq_valid &= ~(1u << tr);
        return 0;
    }
    b = g_seq[g_seq_loc[tr]];
    g_seq_loc[tr]++;
    if (b != SEQ_BLOCK)
        return b;
    if (!seq_ok_off(g_seq_loc[tr])) {
        g_seq_valid &= ~(1u << tr);
        return 0;
    }
    nxt = g_seq[g_seq_loc[tr]];
    g_seq_loc[tr]++;
    if (nxt == SEQ_BLOCK)
        return SEQ_BLOCK;
    if (g_seq_loc[tr] + 1u >= g_seq_n) {
        g_seq_valid &= ~(1u << tr);
        return 0;
    }
    backup = ((uint32_t)nxt << 8) | g_seq[g_seq_loc[tr]];
    g_seq_loc[tr]++;
    g_seq_bulen[tr] = g_seq[g_seq_loc[tr]];
    g_seq_loc[tr]++;
    if (g_seq_loc[tr] < backup + 4u) {
        g_seq_bulen[tr] = 0;
        g_seq_valid &= ~(1u << tr);
        return 0;
    }
    src = g_seq_loc[tr] - (backup + 4u);
    if (!seq_ok_off(src) || !g_seq_bulen[tr]) {
        g_seq_bulen[tr] = 0;
        g_seq_valid &= ~(1u << tr);
        return 0;
    }
    g_seq_bu[tr] = src;
    b = g_seq[g_seq_bu[tr]];
    g_seq_bu[tr]++;
    g_seq_bulen[tr]--;
    return b;
}

static uint32_t seq_varlen(uint32_t tr)
{
    uint32_t v;
    uint32_t c;
    int n;

    v = seq_get_byte(tr);
    if (!(v & 0x80u))
        return v;
    v &= 0x7fu;
    for (n = 0; n < 4; n++) {
        c = seq_get_byte(tr);
        v = (v << 7) + (c & 0x7fu);
        if (!(c & 0x80u))
            break;
    }
    return v;
}

static int seq_next_delta(uint32_t *ticks)
{
    uint32_t i;
    uint32_t first = 0xFFFFFFFFu;
    uint32_t last = g_seq_last_delta;

    if (!g_seq_valid)
        return 0;
    for (i = 0; i < SEQ_TRACKS; i++) {
        if (!((g_seq_valid >> i) & 1u))
            continue;
        if (g_seq_delta_flag) {
            if (g_seq_delta[i] >= last)
                g_seq_delta[i] -= last;
            else
                g_seq_delta[i] = 0;
        }
        if (g_seq_delta[i] < first)
            first = g_seq_delta[i];
    }
    g_seq_delta_flag = 0;
    *ticks = first;
    return 1;
}

static uint32_t seq_ticks_to_samples(uint32_t ticks)
{
    uint64_t den;
    uint64_t num;
    uint32_t samples;

    if (!ticks || !g_seq_div || !g_seq_tempo)
        return 0;
    den = (uint64_t)g_seq_div * 1000000ull;
    num = (uint64_t)ticks * (uint64_t)PORT_AUDIO_RATE * (uint64_t)g_seq_tempo + g_seq_frac;
    samples = (uint32_t)(num / den);
    g_seq_frac = num % den;
    return samples;
}

static uint32_t seq_dur_samples(uint32_t ticks)
{
    uint64_t den;
    uint64_t num;

    if (!ticks || !g_seq_div || !g_seq_tempo)
        return 1;
    den = (uint64_t)g_seq_div * 1000000ull;
    num = (uint64_t)ticks * (uint64_t)PORT_AUDIO_RATE * (uint64_t)g_seq_tempo;
    return (uint32_t)(num / den);
}

static void seq_note_off(uint8_t note)
{
    int i;
    for (i = 0; i < SEQ_VOICES; i++) {
        if (g_seq_vleft[i] && g_seq_vnote[i] == note)
            g_seq_vleft[i] = 0;
    }
}

static void seq_note_on(uint8_t ch, uint8_t note, uint8_t vel, uint32_t dur_ticks)
{
    int i;
    int slot = 0;
    uint32_t oldest = 0xFFFFFFFFu;
    uint32_t left;
    int amp;
    uint32_t hz;
    uint8_t vol;

    if (note > 127u || vel == 0) {
        seq_note_off(note);
        return;
    }
    for (i = 0; i < SEQ_VOICES; i++) {
        if (g_seq_vleft[i] == 0) {
            slot = i;
            break;
        }
        if (g_seq_vleft[i] < oldest) {
            oldest = g_seq_vleft[i];
            slot = i;
        }
    }
    vol = g_seq_vol[ch & 15u];
    amp = (int)((uint32_t)SEQ_AMP * (uint32_t)vel * (uint32_t)vol / (127u * 127u));
    if (amp < 1)
        amp = 1;
    hz = k_midi_hz[note];
    left = seq_dur_samples(dur_ticks);
    if (left < 2u)
        left = 2u;
    g_seq_vleft[slot] = left;
    g_seq_vphase[slot] = 0;
    g_seq_vinc[slot] = phase_inc(hz);
    g_seq_vamp[slot] = amp;
    g_seq_vnote[slot] = note;
}

static void seq_apply_event(uint32_t tr)
{
    uint8_t status;
    uint8_t typ;
    uint8_t cmd;
    uint8_t b1;
    uint8_t b2;
    uint8_t ch;
    uint32_t dur;
    uint32_t tmp;
    uint32_t off;
    uint8_t loop_ct;
    uint8_t cur_lp;
    uint32_t us;

    if (tr >= SEQ_TRACKS)
        return;
    status = seq_get_byte(tr);
    if (status == SEQ_META) {
        typ = seq_get_byte(tr);
        if (typ == SEQ_TEMPO) {
            us = ((uint32_t)seq_get_byte(tr) << 16) | ((uint32_t)seq_get_byte(tr) << 8) |
                 seq_get_byte(tr);
            if (us)
                g_seq_tempo = us;
            g_seq_status[tr] = 0;
            return;
        }
        if (typ == SEQ_EOT) {
            g_seq_valid &= ~(1u << tr);
            return;
        }
        if (typ == SEQ_LOOPSTART) {
            (void)seq_get_byte(tr);
            (void)seq_get_byte(tr);
            g_seq_status[tr] = 0;
            return;
        }
        if (typ == SEQ_LOOPEND) {
            tmp = g_seq_loc[tr];
            if (tmp + 6u > g_seq_n) {
                g_seq_valid &= ~(1u << tr);
                return;
            }
            loop_ct = g_seq[tmp];
            cur_lp = g_seq[tmp + 1u];
            if (cur_lp == 0) {
                g_seq[tmp + 1u] = loop_ct;
                g_seq_loc[tr] = tmp + 6u;
            } else {
                if (cur_lp != 0xFFu)
                    g_seq[tmp + 1u] = (uint8_t)(cur_lp - 1u);
                off = seq_be32(g_seq + tmp + 2u);
                if (tmp + 6u < off) {
                    g_seq_valid &= ~(1u << tr);
                    return;
                }
                g_seq_loc[tr] = tmp + 6u - off;
            }
            g_seq_status[tr] = 0;
            return;
        }
        return;
    }
    if (status & 0x80u) {
        g_seq_status[tr] = status;
        b1 = seq_get_byte(tr);
    } else {
        b1 = status;
        status = g_seq_status[tr];
        if (!(status & 0x80u))
            return;
    }
    cmd = (uint8_t)(status & 0xF0u);
    ch = (uint8_t)(status & 0x0Fu);
    b2 = 0;
    dur = 0;
    if (cmd != SEQ_PROG && cmd != SEQ_PRESS) {
        b2 = seq_get_byte(tr);
        if (cmd == SEQ_NOTEON)
            dur = seq_varlen(tr);
    }
    if (cmd == SEQ_NOTEON)
        seq_note_on(ch, b1, b2, dur);
    else if (cmd == SEQ_NOTEOFF)
        seq_note_off(b1);
    else if (cmd == SEQ_CC && b1 == SEQ_CC_VOL)
        g_seq_vol[ch] = b2;
}

static void seq_next_event(void)
{
    uint32_t i;
    uint32_t first = 0xFFFFFFFFu;
    uint32_t first_tr = SEQ_TRACKS;
    uint32_t last = g_seq_last_delta;

    if (!g_seq_valid)
        return;
    for (i = 0; i < SEQ_TRACKS; i++) {
        if (!((g_seq_valid >> i) & 1u))
            continue;
        if (g_seq_delta_flag) {
            if (g_seq_delta[i] >= last)
                g_seq_delta[i] -= last;
            else
                g_seq_delta[i] = 0;
        }
        if (g_seq_delta[i] < first) {
            first = g_seq_delta[i];
            first_tr = i;
        }
    }
    if (first_tr >= SEQ_TRACKS)
        return;
    seq_apply_event(first_tr);
    g_seq_last_delta = first;
    if ((g_seq_valid >> first_tr) & 1u)
        g_seq_delta[first_tr] += seq_varlen(first_tr);
    g_seq_delta_flag = 1;
}

static int seq_prime(void)
{
    uint32_t i;
    uint32_t off;

    seq_walker_clear();
    if (!g_seq || g_seq_n < SEQ_HDR)
        return -1;
    g_seq_div = seq_be32(g_seq + 64);
    if (g_seq_div == 0)
        return -1;
    for (i = 0; i < SEQ_TRACKS; i++) {
        off = seq_be32(g_seq + i * 4u);
        if (!off)
            continue;
        if (off < SEQ_HDR || off >= g_seq_n)
            return -1;
        g_seq_valid |= 1u << i;
        g_seq_loc[i] = off;
        g_seq_delta[i] = seq_varlen(i);
    }
    if (!g_seq_valid)
        return -1;
    g_seq_on = 1;
    g_seq_wait = 0;
    g_seq_pending = 0;
    g_seq_frac = 0;
    g_seq_delta_flag = 1;
    g_seq_last_delta = 0;
    return 0;
}

int port_audio_load_seq(const uint8_t *bytes, uint32_t n)
{
    uint8_t *copy;

    port_audio_unload_seq();
    if (!bytes || n < SEQ_HDR)
        return -1;
    copy = (uint8_t *)malloc(n);
    if (!copy)
        return -1;
    memcpy(copy, bytes, n);
    g_seq = copy;
    g_seq_n = n;
    if (seq_prime() != 0) {
        port_audio_unload_seq();
        return -1;
    }
    return 0;
}

static void seq_pump_due(void)
{
    uint32_t ticks;
    int n = 0;

    if (!g_seq_on || !g_seq || g_seq_wait > 0)
        return;
    while (n++ < 64) {
        if (g_seq_pending) {
            seq_next_event();
            g_seq_pending = 0;
            if (!g_seq_valid) {
                g_seq_on = 0;
                return;
            }
        }
        if (!seq_next_delta(&ticks)) {
            g_seq_on = 0;
            return;
        }
        if (ticks > 0) {
            g_seq_wait = seq_ticks_to_samples(ticks);
            g_seq_pending = 1;
            if (g_seq_wait == 0)
                continue;
            return;
        }
        seq_next_event();
        if (!g_seq_valid) {
            g_seq_on = 0;
            return;
        }
    }
}

static int seq_mix_sample(void)
{
    int i;
    int acc = 0;

    if (g_seq_on) {
        if (g_seq_wait > 0)
            g_seq_wait--;
        if (g_seq_wait == 0)
            seq_pump_due();
    }
    for (i = 0; i < SEQ_VOICES; i++) {
        if (g_seq_vleft[i] == 0)
            continue;
        acc += osc_tri(g_seq_vphase[i], g_seq_vamp[i]);
        g_seq_vphase[i] += g_seq_vinc[i];
        g_seq_vleft[i]--;
    }
    return acc;
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
    if (kind == PORT_SFX_RICO)
        return RICO_LEN;
    if (kind == PORT_SFX_PICKUP || kind == PORT_SFX_AMMO || kind == PORT_SFX_ARMOUR ||
        kind == PORT_SFX_RELOAD)
        return PICKUP_LEN;
    if (kind == PORT_SFX_DOOR_CLOSE)
        return DOOR_LEN;
    if (kind == PORT_SFX_YELP)
        return YELP_LEN;
    if (kind == PORT_SFX_HURT)
        return HURT_LEN;
    if (kind == PORT_SFX_STEP)
        return STEP_LEN;
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
    g_ov_pcm = 0;
    g_ov_pcm_n = 0;
    g_ov_vol = 127u;
    if (kind >= 1 && kind <= SFX_KIND_MAX && g_sfx_pcm[kind] && g_sfx_pcm_n[kind] > 0) {
        g_ov_use_pcm = 1;
        g_ov_pcm = g_sfx_pcm[kind];
        g_ov_pcm_n = g_sfx_pcm_n[kind];
        g_ov_vol = g_sfx_pcm_vol[kind] ? g_sfx_pcm_vol[kind] : 127u;
        g_ov_left = g_sfx_pcm_n[kind];
        g_ov_len = g_sfx_pcm_n[kind];
    } else {
        g_ov_use_pcm = 0;
        g_ov_left = len;
        g_ov_len = len;
    }
}

void port_audio_push_fall(const int16_t *pcm, uint32_t n, uint8_t vol)
{
    if (g_nfall >= FALL_VARS || !pcm || n == 0)
        return;
    g_fall_pcm[g_nfall] = pcm;
    g_fall_n[g_nfall] = n;
    g_fall_vol[g_nfall] = vol ? vol : 127u;
    g_nfall++;
}

void port_audio_clear_falls(void)
{
    int i;
    for (i = 0; i < FALL_VARS; i++) {
        g_fall_pcm[i] = 0;
        g_fall_n[i] = 0;
        g_fall_vol[i] = 0;
    }
    g_nfall = 0;
    g_fall_i = 0;
}

int port_audio_fall_variants(void)
{
    return g_nfall;
}

void port_audio_play_fall(void)
{
    if (g_nfall > 0) {
        int i = g_fall_i;
        g_fall_i++;
        if (g_fall_i >= g_nfall)
            g_fall_i = 0;
        g_sfx_pcm[PORT_SFX_FALL] = g_fall_pcm[i];
        g_sfx_pcm_n[PORT_SFX_FALL] = g_fall_n[i];
        g_sfx_pcm_vol[PORT_SFX_FALL] = g_fall_vol[i];
    }
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

void port_audio_play_rico(void)
{
    queue_hit(PORT_SFX_RICO, RICO_LEN);
}

void port_audio_play_ammo(void)
{
    queue_sfx(PORT_SFX_AMMO, PICKUP_LEN);
}

void port_audio_play_armour(void)
{
    queue_sfx(PORT_SFX_ARMOUR, PICKUP_LEN);
}

void port_audio_play_reload(void)
{
    queue_sfx(PORT_SFX_RELOAD, PICKUP_LEN);
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

static void queue_voice(int kind, uint32_t len)
{
    g_voice_kind = kind;
    g_last_sfx = kind;
    g_voice_phase = 0;
    g_voice_pos = 0;
    g_voice_pcm = 0;
    g_voice_pcm_n = 0;
    g_voice_vol = 127u;
    if (kind >= 1 && kind <= SFX_KIND_MAX && g_sfx_pcm[kind] && g_sfx_pcm_n[kind] > 0) {
        g_voice_use_pcm = 1;
        g_voice_pcm = g_sfx_pcm[kind];
        g_voice_pcm_n = g_sfx_pcm_n[kind];
        g_voice_vol = g_sfx_pcm_vol[kind] ? g_sfx_pcm_vol[kind] : 127u;
        g_voice_left = g_sfx_pcm_n[kind];
        g_voice_len = g_sfx_pcm_n[kind];
    } else {
        g_voice_use_pcm = 0;
        g_voice_left = len;
        g_voice_len = len;
    }
}

void port_audio_push_yelp(const int16_t *pcm, uint32_t n, uint8_t vol)
{
    if (g_nyelp >= YELP_VARS || !pcm || n == 0)
        return;
    g_yelp_pcm[g_nyelp] = pcm;
    g_yelp_n[g_nyelp] = n;
    g_yelp_vol[g_nyelp] = vol ? vol : 127u;
    g_nyelp++;
}

void port_audio_clear_yelps(void)
{
    int i;
    for (i = 0; i < YELP_VARS; i++) {
        g_yelp_pcm[i] = 0;
        g_yelp_n[i] = 0;
        g_yelp_vol[i] = 0;
    }
    g_nyelp = 0;
    g_yelp_i = 0;
}

int port_audio_yelp_variants(void)
{
    return g_nyelp;
}

void port_audio_play_yelp(void)
{
    if (g_nyelp > 0) {
        int i = g_yelp_i;
        g_yelp_i++;
        if (g_yelp_i >= g_nyelp)
            g_yelp_i = 0;
        g_sfx_pcm[PORT_SFX_YELP] = g_yelp_pcm[i];
        g_sfx_pcm_n[PORT_SFX_YELP] = g_yelp_n[i];
        g_sfx_pcm_vol[PORT_SFX_YELP] = g_yelp_vol[i];
    }
    queue_voice(PORT_SFX_YELP, YELP_LEN);
}

void port_audio_play_hurt(void)
{
    queue_voice(PORT_SFX_HURT, HURT_LEN);
}

static void queue_step(int kind, uint32_t len)
{
    g_step_kind = kind;
    g_last_sfx = kind;
    g_step_phase = 0;
    g_step_pos = 0;
    g_step_foot ^= 1;
    g_step_hz = g_step_foot ? STEP_HZ1 : STEP_HZ0;
    if (kind >= 1 && kind <= SFX_KIND_MAX && g_sfx_pcm[kind] && g_sfx_pcm_n[kind] > 0) {
        g_step_use_pcm = 1;
        g_step_left = g_sfx_pcm_n[kind];
        g_step_len = g_sfx_pcm_n[kind];
    } else {
        g_step_use_pcm = 0;
        g_step_left = len;
        g_step_len = len;
    }
}

void port_audio_play_step(void)
{
    queue_step(PORT_SFX_STEP, STEP_LEN);
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
    uint32_t rinc = phase_inc(RICO_HZ);
    uint32_t kinc = phase_inc(KF7_HZ);
    uint32_t pinc = phase_inc(PICKUP_HZ);
    uint32_t ainc = phase_inc(AMMO_HZ);
    uint32_t arinc = phase_inc(ARMOUR_HZ);
    uint32_t rlinc = phase_inc(RELOAD_HZ);
    uint32_t cinc = phase_inc(DOOR_CLOSE_HZ);
    uint32_t yinc = phase_inc(YELP_HZ);
    uint32_t uinc = phase_inc(HURT_HZ);
    uint32_t sinc = phase_inc(g_step_hz ? g_step_hz : STEP_HZ0);
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

        {
            int ms = seq_mix_sample();
            acc_l += ms;
            acc_r += ms;
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
                        : (kind == PORT_SFX_PICKUP || kind == PORT_SFX_AMMO ||
                           kind == PORT_SFX_ARMOUR || kind == PORT_SFX_RELOAD)
                              ? PICKUP_AMP
                        : (kind == PORT_SFX_KF7) ? KF7_AMP
                        : GUN_AMP) * g_sfx_left / g_sfx_len);
                if (kind == PORT_SFX_DRY)
                    s = osc_tri(g_sfx_phase, amp);
                else if (kind == PORT_SFX_DOOR || kind == PORT_SFX_DOOR_CLOSE)
                    s = osc_tri(g_sfx_phase, amp);
                else if (kind == PORT_SFX_PICKUP || kind == PORT_SFX_AMMO ||
                         kind == PORT_SFX_ARMOUR || kind == PORT_SFX_RELOAD)
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
                else if (kind == PORT_SFX_AMMO)
                    g_sfx_phase += ainc;
                else if (kind == PORT_SFX_ARMOUR)
                    g_sfx_phase += arinc;
                else if (kind == PORT_SFX_RELOAD)
                    g_sfx_phase += rlinc;
                else if (kind == PORT_SFX_KF7)
                    g_sfx_phase += kinc + ninc / 8u;
                else
                    g_sfx_phase += ginc + ninc / 8u;
            }
            acc_l += s;
            acc_r += s;
            g_sfx_left--;
        }

        if (g_ov_kind && g_ov_left > 0 && g_ov_len > 0) {
            int s = 0;
            if (g_ov_use_pcm && g_ov_pcm && g_ov_pos < g_ov_pcm_n) {
                int vol = (int)g_ov_vol;
                int32_t v = ((int32_t)g_ov_pcm[g_ov_pos] * vol) / 127;
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
            } else if (hkind == PORT_SFX_RICO) {
                int amp = (int)((uint32_t)RICO_AMP * g_hit_left / g_hit_len);
                s = osc_tri(g_hit_phase, amp);
                g_hit_phase += rinc;
            } else {
                int amp = (int)((uint32_t)HIT_AMP * g_hit_left / g_hit_len);
                s = osc_tri(g_hit_phase, amp) + osc_noise(&g_sfx_noise, amp / 2);
                g_hit_phase += hinc;
            }
            acc_l += s;
            acc_r += s;
            g_hit_left--;
        }

        if (g_voice_left > 0 && g_voice_len > 0) {
            int s = 0;
            int vkind = g_voice_kind;
            if (g_voice_use_pcm && g_voice_pcm && g_voice_pos < g_voice_pcm_n) {
                int vol = (int)g_voice_vol;
                int32_t v = ((int32_t)g_voice_pcm[g_voice_pos] * vol) / 127;
                g_voice_pos++;
                s = (int)v;
            } else if (vkind == PORT_SFX_HURT) {
                int amp = (int)((uint32_t)HURT_AMP * g_voice_left / g_voice_len);
                s = osc_tri(g_voice_phase, amp);
                g_voice_phase += uinc;
            } else {
                int amp = (int)((uint32_t)YELP_AMP * g_voice_left / g_voice_len);
                s = osc_tri(g_voice_phase, amp);
                g_voice_phase += yinc;
            }
            acc_l += s;
            acc_r += s;
            g_voice_left--;
        }

        if (g_step_left > 0 && g_step_len > 0) {
            int s = 0;
            int skind = g_step_kind;
            if (g_step_use_pcm && skind >= 1 && skind <= SFX_KIND_MAX &&
                g_sfx_pcm[skind] && g_step_pos < g_sfx_pcm_n[skind]) {
                int vol = (int)g_sfx_pcm_vol[skind];
                int32_t v = ((int32_t)g_sfx_pcm[skind][g_step_pos] * vol) / 127;
                g_step_pos++;
                s = (int)v;
            } else {
                int amp = (int)((uint32_t)STEP_AMP * g_step_left / g_step_len);
                uint32_t spent = g_step_len - g_step_left;
                s = osc_tri(g_step_phase, amp);
                if (spent < STEP_CRACK)
                    s += osc_noise(&g_sfx_noise, amp / 2);
                g_step_phase += sinc;
            }
            acc_l += s;
            acc_r += s;
            g_step_left--;
        }

        stereo[i * 2] = CLAMP16(acc_l);
        stereo[i * 2 + 1] = CLAMP16(acc_r);
    }
}
