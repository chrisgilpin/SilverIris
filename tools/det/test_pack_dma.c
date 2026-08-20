#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/c0pack.h"
#include "fs/filelist.h"
#include "fs/pack_dma.h"
#include "fs/sha256.h"
#include <ultra64.h>

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main(int argc, char **argv)
{
    uint8_t *rom;
    uint8_t hello[37];
    C0File files[2];
    uint8_t *pack = NULL;
    size_t pack_len = 0;
    uint8_t pack_hash[32];
    char hex[65];
    uint8_t out[40];
    OSMesg msgbuf;
    OSMesgQueue mq;
    OSIoMesg iomsg;
    const char *csv = (argc >= 2) ? argv[1] : NULL;

    rom = (uint8_t *)calloc(256, 1);
    if (!rom)
        return fail("calloc");
    rom[0] = 0x80;
    rom[1] = 0x37;
    rom[2] = 0x12;
    rom[3] = 0x40;
    rom[100] = 'A';
    rom[101] = 'B';
    rom[102] = 'C';
    memset(hello, 0x11, sizeof hello);
    memcpy(rom + 200, hello, sizeof hello);

    files[0].path = "assets/test.bin";
    files[0].bytes = rom + 100;
    files[0].size = 3;
    files[1].path = "assets/hello.bin";
    files[1].bytes = hello;
    files[1].size = sizeof hello;

    if (c0pack_build(files, 2, 0, 0, &pack, &pack_len, pack_hash) != 0)
        return fail("build");
    if (port_init(pack, pack_len) != 0)
        return fail("port_init");
    if (port_pack_file_count() != 2)
        return fail("file count");
    silveriris_sha256_hex(pack_hash, hex);

    if (csv && port_filelist_load(csv) != 0)
        return fail("filelist");
    memset(out, 0, sizeof out);
    if (port_pack_dma_named(out, "assets/test.bin", 0, 3) != 0 || memcmp(out, "ABC", 3) != 0)
        return fail("named dma");
    memset(out, 0, sizeof out);
    if (port_pack_dma(out, 100, 3) != 0 || memcmp(out, "ABC", 3) != 0)
        return fail("offset dma");
    memset(out, 0, sizeof out);
    if (port_pack_dma(out, 0xB0000064u, 3) != 0 || memcmp(out, "ABC", 3) != 0)
        return fail("kseg1 dma");
    if (port_pack_dma(out, 50, 3) != PORT_PACK_ERR_RANGE)
        return fail("oob offset");

    osCreateMesgQueue(&mq, &msgbuf, 1);
    memset(out, 0, sizeof out);
    memset(&iomsg, 0, sizeof iomsg);
    if (osPiStartDma(&iomsg, OS_MESG_PRI_NORMAL, OS_READ, 200, out, 37, &mq) != 0)
        return fail("osPiStartDma");
    if (osRecvMesg(&mq, NULL, OS_MESG_NOBLOCK) != 0)
        return fail("pi mesg");
    if (out[0] != 0x11 || out[36] != 0x11)
        return fail("pi payload");

    port_shutdown();
    free(pack);
    free(rom);
    port_filelist_clear();
    printf("pack dma ok packHash=%s\n", hex);
    return 0;
}
