#ifndef SILVERIRIS_PACK_DMA_H
#define SILVERIRIS_PACK_DMA_H

/*
 * Product DMA: byte source is a validated .c0pack. Compiled into silveriris
 * without PORT_BRINGUP_ROM_DMA — this file must not fopen a .z64.
 */

#include <stddef.h>
#include <stdint.h>

#include "c0pack.h"

#define PORT_PACK_OK 0
#define PORT_PACK_ERR_IO -1
#define PORT_PACK_ERR_RANGE -5
#define PORT_PACK_ERR_PARSE -6
#define PORT_PACK_ERR_NOT_LOADED -7

/* Require a valid pack. Missing/mismatched hash → refuse. */
int port_init(const uint8_t *pack, size_t len);
int port_init_file(const char *path);
void port_shutdown(void);

const uint8_t *port_pack_hash(void);
uint8_t port_pack_region(void);
uint32_t port_pack_file_count(void);
const C0Pack *port_pack(void);

uint32_t port_pack_devaddr_to_offset(uint32_t devAddr);
int port_pack_dma(void *dest, uint32_t devAddr, uint32_t size);
int port_pack_dma_named(void *dest, const char *name, uint32_t extra_offset, uint32_t size);

#endif
