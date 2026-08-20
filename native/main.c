#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "game/frametiming.h"
#include "vi/tick_contract.h"

#include <ultra64.h>

#ifdef PORT_BRINGUP_ROM_DMA
#include "fs/rom_dma.h"
#ifndef PORT_FILELIST_DEFAULT
#define PORT_FILELIST_DEFAULT "third_party/goldeneye_src/scripts/filelist.u.csv"
#endif
#else
#include "fs/pack_dma.h"
#include "fs/filelist.h"
#include "fs/sha256.h"
#ifndef PORT_FILELIST_DEFAULT
#define PORT_FILELIST_DEFAULT "third_party/goldeneye_src/scripts/filelist.u.csv"
#endif
#endif

#include "audio/audio.h"
#include "fs/stage.h"
#include "gfx/gbi_interp.h"
#include "gfx/sw_raster.h"
#include "vi/sim_tick.h"

#ifdef SILVERIRIS_HAS_SDL
#include <SDL.h>

static void silveriris_sdl_audio(void *userdata, Uint8 *stream, int len)
{
    int nframes;

    (void)userdata;
    if (!stream || len <= 0)
        return;
    nframes = len / (int)(PORT_AUDIO_CHANNELS * (int)sizeof(int16_t));
    port_audio_cb((int16_t *)stream, nframes);
}
#endif

extern s32 speedgraphframes;
void updateFrameCounters(s32 deltaFrames);

static void run_tick(uint32_t t)
{
    assert(port_sim_tick(t) == 0);
    assert(speedgraphframes == PORT_SPEEDGRAPHFRAMES);
    assert(osGetCount() == t * (u32)PORT_SPEEDGRAPHFRAMES * PORT_CYCLES_PER_VI);
}

static void sleep_ms(unsigned ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

#ifdef PORT_BRINGUP_ROM_DMA
static void bringup_usage(void)
{
    fprintf(stderr,
            "silveriris_bringup (K18 developer only, not a public download)\n"
            "  --rom <ge007.u.z64>     NTSC-U dump; fopen is compiled in only here\n"
            "  --filelist <csv>        default: decomp scripts/filelist.u.csv\n"
            "  --headless [--ticks=N]\n");
}

static int bringup_load_rom(const char *rom_path, const char *filelist_path)
{
    int rc;
    char sha1[41];
    const char *fl = filelist_path ? filelist_path : PORT_FILELIST_DEFAULT;

    rc = port_rom_open(rom_path);
    if (rc == PORT_ROM_ERR_SIZE) {
        fprintf(stderr, "ROM too small to be a matching dump\n");
        return 1;
    }
    if (rc == PORT_ROM_ERR_HEADER) {
        fprintf(stderr, "Unrecognised N64 ROM header\n");
        return 1;
    }
    if (rc == PORT_ROM_ERR_REGION) {
        fprintf(stderr, "US dump required.\n");
        return 1;
    }
    if (rc != PORT_ROM_OK) {
        fprintf(stderr, "port_rom_open %s failed (%d)\n", rom_path, rc);
        return 1;
    }
    port_rom_sha1_hex(sha1);
    rc = port_filelist_load(fl);
    if (rc != PORT_ROM_OK) {
        fprintf(stderr, "filelist %s not loaded (%d); raw DMA still available\n", fl, rc);
    }
    printf("rom dma ok sha1=%s size=%zu files=%zu\n", sha1, port_rom_size(),
           port_filelist_count());
    return 0;
}
#endif

#ifndef PORT_BRINGUP_ROM_DMA
static int product_load_pack(const char *pack_path, const char *filelist_path)
{
    int rc;
    char hex[65];
    const uint8_t *h;
    const char *fl = filelist_path ? filelist_path : PORT_FILELIST_DEFAULT;

    rc = port_init_file(pack_path);
    if (rc != PORT_PACK_OK) {
        fprintf(stderr, "port_init %s failed (%d)\n", pack_path, rc);
        return 1;
    }
    rc = port_filelist_load(fl);
    if (rc != 0)
        fprintf(stderr, "filelist %s not loaded (%d); named DMA still available\n", fl, rc);
    h = port_pack_hash();
    if (h)
        silveriris_sha256_hex(h, hex);
    else
        hex[0] = 0;
    printf("pack ok hash=%s files=%u region=%u filelist=%zu\n", hex, port_pack_file_count(),
           port_pack_region(), port_filelist_count());
    return 0;
}
#endif

int main(int argc, char **argv)
{
    uint32_t t;
    int headless = 0;
    uint32_t ticks = 8;
    int i;
#ifdef PORT_BRINGUP_ROM_DMA
    const char *rom_path = NULL;
    const char *filelist_path = NULL;
#else
    const char *pack_path = NULL;
    const char *filelist_path = NULL;
    const char *stage_name = NULL;
#endif

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--headless") == 0) {
            headless = 1;
        } else if (strncmp(argv[i], "--ticks=", 8) == 0) {
            ticks = (uint32_t)atoi(argv[i] + 8);
#ifdef PORT_BRINGUP_ROM_DMA
        } else if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc) {
            rom_path = argv[++i];
        } else if (strncmp(argv[i], "--rom=", 6) == 0) {
            rom_path = argv[i] + 6;
        } else if (strcmp(argv[i], "--filelist") == 0 && i + 1 < argc) {
            filelist_path = argv[++i];
        } else if (strncmp(argv[i], "--filelist=", 11) == 0) {
            filelist_path = argv[i] + 11;
#else
        } else if (strcmp(argv[i], "--rom") == 0 || strncmp(argv[i], "--rom=", 6) == 0) {
            fprintf(stderr, "K18: --rom is only on silveriris_bringup (PORT_BRINGUP_ROM_DMA)\n");
            return 2;
        } else if (strcmp(argv[i], "--pack") == 0 && i + 1 < argc) {
            pack_path = argv[++i];
        } else if (strncmp(argv[i], "--pack=", 7) == 0) {
            pack_path = argv[i] + 7;
        } else if (strcmp(argv[i], "--filelist") == 0 && i + 1 < argc) {
            filelist_path = argv[++i];
        } else if (strncmp(argv[i], "--filelist=", 11) == 0) {
            filelist_path = argv[i] + 11;
        } else if (strcmp(argv[i], "--stage") == 0 && i + 1 < argc) {
            stage_name = argv[++i];
        } else if (strncmp(argv[i], "--stage=", 8) == 0) {
            stage_name = argv[i] + 8;
#endif
        }
    }
#ifdef PORT_BRINGUP_ROM_DMA
    if (!rom_path) {
        bringup_usage();
        return 2;
    }
    if (bringup_load_rom(rom_path, filelist_path) != 0)
        return 1;
#else
    if (pack_path && product_load_pack(pack_path, filelist_path) != 0)
        return 1;
    if (stage_name) {
        int id = PORT_LEVEL_FACILITY;
        int rc;
        if (strcmp(stage_name, "complex") == 0)
            id = PORT_LEVEL_COMPLEX;
        rc = port_stage_load(id);
        printf("stage %s load rc=%d rooms=%d\n", stage_name, rc, port_stage_room_count());
        if (rc != PORT_STAGE_OK && getenv("SILVERIRIS_REQUIRE_STAGE"))
            return 1;
    }
#endif
    if (getenv("SILVERIRIS_HEADLESS")) {
        headless = 1;
    }

    if (headless) {
        for (t = 0; t < ticks; t++) {
            run_tick(t);
        }
        printf("tick-test ok (%u ticks, speedgraphframes=%d, osGetCount@last=%u)\n",
               ticks, speedgraphframes, (unsigned)osGetCount());
        return 0;
    }

#ifdef SILVERIRIS_HAS_SDL
    {
        SDL_Window *win;
        SDL_Renderer *ren;
        SDL_Texture *tex = NULL;
        SDL_AudioDeviceID adev = 0;
        int running = 1;
        int have_g1;
        t = 0;
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
            if (SDL_Init(SDL_INIT_VIDEO) != 0) {
                fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
                return 1;
            }
        }
        win = SDL_CreateWindow("SilverIris", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               G1_FB_W, G1_FB_H, 0);
        ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : NULL;
        if (!win || !ren) {
            fprintf(stderr, "SDL window: %s\n", SDL_GetError());
            return 1;
        }
        port_audio_init();
        {
            SDL_AudioSpec want, have;
            SDL_zero(want);
            want.freq = (int)port_audio_rate();
            want.format = AUDIO_S16SYS;
            want.channels = PORT_AUDIO_CHANNELS;
            want.samples = 1024;
            want.callback = silveriris_sdl_audio;
            adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
            if (adev) {
                port_audio_set_placeholder_music(1);
                SDL_PauseAudioDevice(adev, 0);
            }
        }
        have_g1 = (g1_run_synthetic() == 0);
        if (have_g1) {
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                    G1_FB_W, G1_FB_H);
            if (tex)
                SDL_UpdateTexture(tex, NULL, g1_fb_rgba(), G1_FB_W * 4);
        }
        while (running) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) {
                    running = 0;
                } else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                    if (ev.key.keysym.sym == SDLK_z || ev.key.keysym.sym == SDLK_SPACE)
                        port_audio_play_gun();
                }
            }
            run_tick(t++);
            if (tex) {
                SDL_RenderCopy(ren, tex, NULL, NULL);
            } else {
                SDL_SetRenderDrawColor(ren, 12, 28, 48, 255);
                SDL_RenderClear(ren);
            }
            SDL_RenderPresent(ren);
            sleep_ms(PORT_TICK_MS);
        }
        if (adev) {
            SDL_PauseAudioDevice(adev, 1);
            SDL_CloseAudioDevice(adev);
        }
        port_audio_shutdown();
        if (tex)
            SDL_DestroyTexture(tex);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }
#else
    fprintf(stderr, "no SDL2; use --headless\n");
    return 1;
#endif
}
