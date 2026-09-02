#ifndef SILVERIRIS_PORT_AUDIO_H
#define SILVERIRIS_PORT_AUDIO_H

#include <stdint.h>

/* Mixer rate matches audi.c OUTPUT_RATE (22050). Hosts may resample. */
#define PORT_AUDIO_RATE 22050
#define PORT_AUDIO_CHANNELS 2

void port_audio_init(void);
void port_audio_shutdown(void);

/* Interleaved s16 stereo. Must not call randomGetNext / chrObjRandomGetNext. */
void port_audio_cb(int16_t *stereo, int nframes);

void port_audio_set_placeholder_music(int on);
void port_audio_play_gun(void);
void port_audio_play_dry(void);
void port_audio_play_door(void);
void port_audio_play_fall(void);
void port_audio_play_hit(void);
void port_audio_play_kf7(void);
void port_audio_play_pickup(void);
void port_audio_play_door_close(void);
void port_audio_play_rico(void);
void port_audio_play_ammo(void);
void port_audio_play_armour(void);
void port_audio_play_reload(void);
void port_audio_play_yelp(void);
void port_audio_play_hurt(void);
/* GET_HIT_MALE0–24 one-shots. Mixer does not own `pcm`. Cycle is Rare
 * male_guard_yelp_counter (wrap at count), not game RNG. */
void port_audio_push_yelp(const int16_t *pcm, uint32_t n, uint8_t vol);
void port_audio_clear_yelps(void);
int port_audio_yelp_variants(void);
/* BODY_FALL_C1–E3 + BODY_ROLLOVER one-shots. Mixer does not own `pcm`.
 * Cycle is Rare thud_index / body_hit_SFX (wrap at 11), not game RNG. */
void port_audio_push_fall(const int16_t *pcm, uint32_t n, uint8_t vol);
void port_audio_clear_falls(void);
int port_audio_fall_variants(void);
int port_audio_music_on(void);
uint32_t port_audio_rate(void);
/* 0 none, 1 gun, 2 dry, 3 door open, 4 body-fall, 5 flesh-hit, 6 KF7,
 * 7 pickup, 8 door close, 9 wall ricochet, 10 ammo crate, 11 armour,
 * 12 rifle-cock reload, 13 male yelp, 14 Bond hurt. */
int port_audio_last_sfx(void);
#define PORT_SFX_NONE 0
#define PORT_SFX_GUN 1
#define PORT_SFX_DRY 2
#define PORT_SFX_DOOR 3
#define PORT_SFX_FALL 4
#define PORT_SFX_HIT 5
#define PORT_SFX_KF7 6
#define PORT_SFX_PICKUP 7
#define PORT_SFX_DOOR_CLOSE 8
#define PORT_SFX_RICO 9
#define PORT_SFX_AMMO 10
#define PORT_SFX_ARMOUR 11
#define PORT_SFX_RELOAD 12
#define PORT_SFX_YELP 13
#define PORT_SFX_HURT 14

/* Host-endian PCM one-shot. Mixer does not own `pcm`. vol 0..127 (N64). */
void port_audio_install_sfx(int kind, const int16_t *pcm, uint32_t n, uint8_t vol);
int port_audio_sfx_frames(int kind);
int port_audio_sfx_from_bank(int kind);
/* 1 if gun + dry + door are pack VADPCM one-shots. */
int port_audio_bank_ready(void);
/* Decode N64 VADPCM. Returns sample count, or -1. book is host-endian
 * npredictors * order * 8 coefficients (order-2 / 8-tap). */
int port_audio_adpcm_decode(const uint8_t *src, uint32_t src_bytes,
                            const int16_t *book, int order, int npredictors,
                            int16_t *out, uint32_t out_max);
/* Load sfx.ctl / sfx.tbl from the open pack. No-op without a pack. */
int port_audio_load_pack_sfx(void);
void port_audio_unload_pack_sfx(void);

/* osAi* implementations (ai.c). Types match libultra (u32/s32). */
uint32_t osAiGetStatus(void);
uint32_t osAiGetLength(void);
int32_t osAiSetFrequency(uint32_t frequency);
int32_t osAiSetNextBuffer(void *bufPtr, uint32_t size);
int32_t __osAiDeviceBusy(void);

/* Shared with ai.c — DMA FIFO of two host-endian s16 stereo buffers. */
int port_audio_ai_push(const void *buf, uint32_t bytes);
uint32_t port_audio_ai_length(void);
int port_audio_ai_busy(void);
int32_t port_audio_set_frequency(uint32_t frequency);
uint32_t port_audio_ai_frequency(void);

#endif
