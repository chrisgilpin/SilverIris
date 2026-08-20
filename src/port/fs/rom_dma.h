#ifndef SILVERIRIS_ROM_DMA_H
#define SILVERIRIS_ROM_DMA_H

/*
 * K18: native bring-up may fopen a local NTSC-U .z64.
 * This header and rom_dma.c are compiled only with -DPORT_BRINGUP_ROM_DMA
 * (silveriris_bringup). Product silveriris / game.wasm must not define the
 * flag and must not fopen a ROM.
 */

#include <stddef.h>
#include <stdint.h>

#include "filelist.h"

#define PORT_ROM_SHA1_NTSC_U "abe01e4aeb033b6c0836819f549c791b26cfde83"
#define PORT_ROM_SHA1_NTSC_J "2a5dade32f7fad6c73c659d2026994632c1b3174"
#define PORT_ROM_SHA1_PAL_E "167c3c433dec1f1eb921736f7d53fac8cb45ee31"
#define PORT_MIN_ROM_SIZE (12u * 1024u * 1024u)

#define PORT_ROM_OK 0
#define PORT_ROM_ERR_IO -1
#define PORT_ROM_ERR_HEADER -2
#define PORT_ROM_ERR_SIZE -3
#define PORT_ROM_ERR_REGION -4
#define PORT_ROM_ERR_RANGE -5
#define PORT_ROM_ERR_PARSE -6
#define PORT_ROM_ERR_NOT_LOADED -7

typedef enum {
    PORT_ROM_Z64 = 0,
    PORT_ROM_N64,
    PORT_ROM_V64
} PortRomEndian;

typedef enum {
    PORT_ROM_REGION_U = 0,
    PORT_ROM_REGION_J,
    PORT_ROM_REGION_E,
    PORT_ROM_REGION_UNKNOWN
} PortRomRegion;

int port_rom_detect_endian(const uint8_t *bytes, size_t n, PortRomEndian *out);
int port_rom_to_z64(uint8_t *bytes, size_t n);
PortRomRegion port_rom_classify_sha1(const char *hex);

/* Verify-on-open: fopen, byteswap, require NTSC-U SHA-1 and 12 MiB. */
int port_rom_open(const char *path);
/* Attach an already-canonical (z64) buffer. Does not verify SHA-1/size.
 * Used by the public CI fixture. Takes ownership of `bytes` (free on close). */
int port_rom_attach(uint8_t *bytes, size_t n);
void port_rom_close(void);

const uint8_t *port_rom_bytes(void);
size_t port_rom_size(void);
void port_rom_sha1_hex(char hex[41]);

/* Cart/KSEG1 addresses collapse to a file offset. Bare offsets pass through. */
uint32_t port_rom_devaddr_to_offset(uint32_t devAddr);

int port_rom_dma(void *dest, uint32_t devAddr, uint32_t size);
int port_filelist_dma_named(void *dest, const char *name, uint32_t extra_offset, uint32_t size);

#endif
