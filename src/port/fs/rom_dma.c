#ifndef PORT_BRINGUP_ROM_DMA
#error "rom_dma.c is the K18 exception; compile only for silveriris_bringup (-DPORT_BRINGUP_ROM_DMA)"
#endif

#include "rom_dma.h"
#include "filelist.h"
#include "sha1.h"

#include <ultra64.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *g_rom;
static size_t g_rom_size;
static char g_rom_sha1[41];

int port_rom_detect_endian(const uint8_t *bytes, size_t n, PortRomEndian *out)
{
    if (!bytes || !out || n < 4)
        return PORT_ROM_ERR_HEADER;
    if (bytes[0] == 0x80 && bytes[1] == 0x37) {
        *out = PORT_ROM_Z64;
        return PORT_ROM_OK;
    }
    if (bytes[0] == 0x40 && bytes[1] == 0x12) {
        *out = PORT_ROM_N64;
        return PORT_ROM_OK;
    }
    if (bytes[0] == 0x37 && bytes[1] == 0x80) {
        *out = PORT_ROM_V64;
        return PORT_ROM_OK;
    }
    return PORT_ROM_ERR_HEADER;
}

int port_rom_to_z64(uint8_t *bytes, size_t n)
{
    PortRomEndian kind;
    size_t i;
    int rc = port_rom_detect_endian(bytes, n, &kind);
    if (rc != PORT_ROM_OK)
        return rc;
    if (kind == PORT_ROM_Z64)
        return PORT_ROM_OK;
    if (kind == PORT_ROM_V64) {
        if (n % 2 != 0)
            return PORT_ROM_ERR_HEADER;
        for (i = 0; i + 1 < n; i += 2) {
            uint8_t t = bytes[i];
            bytes[i] = bytes[i + 1];
            bytes[i + 1] = t;
        }
        return PORT_ROM_OK;
    }
    if (n % 4 != 0)
        return PORT_ROM_ERR_HEADER;
    for (i = 0; i + 3 < n; i += 4) {
        uint8_t a = bytes[i], b = bytes[i + 1], c = bytes[i + 2], d = bytes[i + 3];
        bytes[i] = d;
        bytes[i + 1] = c;
        bytes[i + 2] = b;
        bytes[i + 3] = a;
    }
    return PORT_ROM_OK;
}

PortRomRegion port_rom_classify_sha1(const char *hex)
{
    if (!hex)
        return PORT_ROM_REGION_UNKNOWN;
    if (strcmp(hex, PORT_ROM_SHA1_NTSC_U) == 0)
        return PORT_ROM_REGION_U;
    if (strcmp(hex, PORT_ROM_SHA1_NTSC_J) == 0)
        return PORT_ROM_REGION_J;
    if (strcmp(hex, PORT_ROM_SHA1_PAL_E) == 0)
        return PORT_ROM_REGION_E;
    return PORT_ROM_REGION_UNKNOWN;
}

static void digest_rom(void)
{
    uint8_t d[20];
    silveriris_sha1(g_rom, g_rom_size, d);
    silveriris_sha1_hex(d, g_rom_sha1);
}

void port_rom_close(void)
{
    free(g_rom);
    g_rom = NULL;
    g_rom_size = 0;
    g_rom_sha1[0] = 0;
}

int port_rom_attach(uint8_t *bytes, size_t n)
{
    if (!bytes || n == 0)
        return PORT_ROM_ERR_IO;
    port_rom_close();
    g_rom = bytes;
    g_rom_size = n;
    digest_rom();
    return PORT_ROM_OK;
}

int port_rom_open(const char *path)
{
    FILE *f;
    long sz;
    uint8_t *buf;
    int rc;
    PortRomRegion region;

    if (!path)
        return PORT_ROM_ERR_IO;

    /* K18: fopen of a .z64 is allowed only in this translation unit, and
     * only because PORT_BRINGUP_ROM_DMA is defined. */
    f = fopen(path, "rb");
    if (!f)
        return PORT_ROM_ERR_IO;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return PORT_ROM_ERR_IO;
    }
    sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return PORT_ROM_ERR_IO;
    }
    if ((size_t)sz < PORT_MIN_ROM_SIZE) {
        fclose(f);
        return PORT_ROM_ERR_SIZE;
    }
    buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return PORT_ROM_ERR_IO;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return PORT_ROM_ERR_IO;
    }
    fclose(f);

    rc = port_rom_to_z64(buf, (size_t)sz);
    if (rc != PORT_ROM_OK) {
        free(buf);
        return rc;
    }
    rc = port_rom_attach(buf, (size_t)sz);
    if (rc != PORT_ROM_OK) {
        free(buf);
        return rc;
    }
    region = port_rom_classify_sha1(g_rom_sha1);
    if (region != PORT_ROM_REGION_U) {
        port_rom_close();
        return PORT_ROM_ERR_REGION;
    }
    return PORT_ROM_OK;
}

const uint8_t *port_rom_bytes(void) { return g_rom; }
size_t port_rom_size(void) { return g_rom_size; }

void port_rom_sha1_hex(char hex[41])
{
    if (!hex)
        return;
    memcpy(hex, g_rom_sha1, 41);
}

uint32_t port_rom_devaddr_to_offset(uint32_t devAddr)
{
    uint32_t top = devAddr & 0xF0000000u;
    if (top == 0xB0000000u || top == 0x90000000u)
        return devAddr & 0x0FFFFFFFu;
    if (devAddr >= 0x10000000u && devAddr < 0x1F000000u)
        return devAddr - 0x10000000u;
    return devAddr;
}

int port_rom_dma(void *dest, uint32_t devAddr, uint32_t size)
{
    uint32_t off;
    if (!g_rom)
        return PORT_ROM_ERR_NOT_LOADED;
    if (!dest && size != 0)
        return PORT_ROM_ERR_IO;
    off = port_rom_devaddr_to_offset(devAddr);
    if ((size_t)off > g_rom_size || (size_t)size > g_rom_size - (size_t)off)
        return PORT_ROM_ERR_RANGE;
    if (size)
        memcpy(dest, g_rom + off, size);
    return PORT_ROM_OK;
}

int port_filelist_dma_named(void *dest, const char *name, uint32_t extra_offset, uint32_t size)
{
    const PortFilelistEntry *e = port_filelist_find(name);
    if (!e)
        return PORT_ROM_ERR_PARSE;
    if (extra_offset > e->size || size > e->size - extra_offset)
        return PORT_ROM_ERR_RANGE;
    return port_rom_dma(dest, e->offset + extra_offset, size);
}

s32 osPiStartDma(OSIoMesg *mb, s32 priority, s32 direction, u32 devAddr, void *dramAddr, u32 size,
                 OSMesgQueue *mq)
{
    (void)priority;
    if (!mb)
        return -1;
    mb->dramAddr = dramAddr;
    mb->devAddr = devAddr;
    mb->size = size;
    mb->hdr.retQueue = mq;
    if (direction == OS_READ) {
        if (port_rom_dma(dramAddr, devAddr, size) != PORT_ROM_OK)
            return -1;
        mb->hdr.status = 0;
    } else if (direction == OS_WRITE) {
        uint32_t off = port_rom_devaddr_to_offset(devAddr);
        if (!g_rom || (size_t)off > g_rom_size || (size_t)size > g_rom_size - (size_t)off)
            return -1;
        if (size)
            memcpy(g_rom + off, dramAddr, size);
        mb->hdr.status = 0;
    } else {
        return -1;
    }
    if (mq)
        osSendMesg(mq, (OSMesg)mb, OS_MESG_NOBLOCK);
    return 0;
}

s32 osPiRawStartDma(s32 direction, u32 devAddr, void *dramAddr, u32 size)
{
    OSIoMesg mb;
    memset(&mb, 0, sizeof mb);
    return osPiStartDma(&mb, OS_MESG_PRI_NORMAL, direction, devAddr, dramAddr, size, NULL);
}
