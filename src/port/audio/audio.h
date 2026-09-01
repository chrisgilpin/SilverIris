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
int port_audio_music_on(void);
uint32_t port_audio_rate(void);
/* 0 none, 1 gun shot, 2 dry click, 3 door. Last one-shot queued. */
int port_audio_last_sfx(void);
#define PORT_SFX_NONE 0
#define PORT_SFX_GUN 1
#define PORT_SFX_DRY 2
#define PORT_SFX_DOOR 3

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
