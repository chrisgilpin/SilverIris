#include "audio/audio.h"

/*
 * libultra AI: the game submits s16 stereo DMA buffers and polls remaining
 * length. We copy into a two-slot FIFO and consume from port_audio_cb.
 * FIFO-full is 0x80000000 (AI_STATUS_FIFO_FULL).
 */

uint32_t osAiGetStatus(void)
{
    return port_audio_ai_busy() ? 0x80000000u : 0u;
}

uint32_t osAiGetLength(void)
{
    return port_audio_ai_length();
}

int32_t osAiSetFrequency(uint32_t frequency)
{
    return port_audio_set_frequency(frequency);
}

int32_t osAiSetNextBuffer(void *bufPtr, uint32_t size)
{
    if (port_audio_ai_busy())
        return -1;
    return port_audio_ai_push(bufPtr, size);
}

int32_t __osAiDeviceBusy(void)
{
    return port_audio_ai_busy();
}
