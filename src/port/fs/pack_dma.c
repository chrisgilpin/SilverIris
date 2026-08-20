#include "pack_dma.h"

#include "filelist.h"

#include <ultra64.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *g_owned;
static C0Pack g_pack;
static int g_loaded;

uint32_t port_pack_devaddr_to_offset(uint32_t devAddr)
{
    uint32_t top = devAddr & 0xF0000000u;
    if (top == 0xB0000000u || top == 0x90000000u)
        return devAddr & 0x0FFFFFFFu;
    if (devAddr >= 0x10000000u && devAddr < 0x1F000000u)
        return devAddr - 0x10000000u;
    return devAddr;
}

void port_shutdown(void)
{
    c0pack_close(&g_pack);
    free(g_owned);
    g_owned = NULL;
    g_loaded = 0;
}

int port_init(const uint8_t *pack, size_t len)
{
    int rc;
    port_shutdown();
    if (!pack || len == 0)
        return PORT_PACK_ERR_IO;
    rc = c0pack_open(pack, len, &g_pack);
    if (rc != 0)
        return rc;
    g_loaded = 1;
    return PORT_PACK_OK;
}

int port_init_file(const char *path)
{
    FILE *f;
    long sz;
    uint8_t *buf;
    int rc;

    if (!path)
        return PORT_PACK_ERR_IO;
    f = fopen(path, "rb");
    if (!f)
        return PORT_PACK_ERR_IO;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return PORT_PACK_ERR_IO;
    }
    sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return PORT_PACK_ERR_IO;
    }
    buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return PORT_PACK_ERR_IO;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return PORT_PACK_ERR_IO;
    }
    fclose(f);
    rc = port_init(buf, (size_t)sz);
    if (rc != PORT_PACK_OK) {
        free(buf);
        return rc;
    }
    g_owned = buf;
    return PORT_PACK_OK;
}

const uint8_t *port_pack_hash(void) { return g_loaded ? g_pack.pack_hash : NULL; }
uint8_t port_pack_region(void) { return g_loaded ? g_pack.region : 0xff; }
uint32_t port_pack_file_count(void) { return g_loaded ? g_pack.nfiles : 0; }
const C0Pack *port_pack(void) { return g_loaded ? &g_pack : NULL; }

int port_pack_dma_named(void *dest, const char *name, uint32_t extra_offset, uint32_t size)
{
    const C0PackEntry *e;
    if (!g_loaded)
        return PORT_PACK_ERR_NOT_LOADED;
    if (!dest && size != 0)
        return PORT_PACK_ERR_IO;
    e = c0pack_find(&g_pack, name);
    if (!e)
        return PORT_PACK_ERR_PARSE;
    if (extra_offset > e->size || size > e->size - extra_offset)
        return PORT_PACK_ERR_RANGE;
    if (size)
        memcpy(dest, e->bytes + extra_offset, size);
    return PORT_PACK_OK;
}

int port_pack_dma(void *dest, uint32_t devAddr, uint32_t size)
{
    uint32_t off = port_pack_devaddr_to_offset(devAddr);
    const PortFilelistEntry *fl;
    uint32_t local;
    if (!g_loaded)
        return PORT_PACK_ERR_NOT_LOADED;
    fl = port_filelist_find_offset(off, size);
    if (!fl)
        return PORT_PACK_ERR_RANGE;
    local = off - fl->offset;
    return port_pack_dma_named(dest, fl->name, local, size);
}

/* Product-only PI hooks. rom_dma.c owns these symbols when
 * PORT_BRINGUP_ROM_DMA is set; do not link this object into silveriris_bringup. */
#ifndef PORT_BRINGUP_ROM_DMA
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
        if (port_pack_dma(dramAddr, devAddr, size) != PORT_PACK_OK)
            return -1;
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
#endif
