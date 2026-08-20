#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/rom_dma.h"
#include "fs/sha1.h"
#include <ultra64.h>

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

static int check_sha1(void)
{
    uint8_t d[20];
    char hex[41];
    const uint8_t abc[] = "abc";

    silveriris_sha1(abc, 3, d);
    silveriris_sha1_hex(d, hex);
    if (strcmp(hex, "a9993e364706816aba3e25717850c26c9cd0d89d") != 0) {
        fprintf(stderr, "sha1(abc)=%s\n", hex);
        return fail("sha1 abc");
    }
    silveriris_sha1((const uint8_t *)"", 0, d);
    silveriris_sha1_hex(d, hex);
    if (strcmp(hex, "da39a3ee5e6b4b0d3255bfef95601890afd80709") != 0)
        return fail("sha1 empty");
    if (port_rom_classify_sha1(PORT_ROM_SHA1_NTSC_U) != PORT_ROM_REGION_U)
        return fail("classify U");
    if (port_rom_classify_sha1(PORT_ROM_SHA1_NTSC_J) != PORT_ROM_REGION_J)
        return fail("classify J");
    if (port_rom_classify_sha1(PORT_ROM_SHA1_PAL_E) != PORT_ROM_REGION_E)
        return fail("classify E");
    if (port_rom_classify_sha1("deadbeef") != PORT_ROM_REGION_UNKNOWN)
        return fail("classify unknown");
    return 0;
}

static int check_byteswap(void)
{
    uint8_t z64[8] = {0x80, 0x37, 0x12, 0x40, 1, 2, 3, 4};
    uint8_t n64[8] = {0x40, 0x12, 0x37, 0x80, 4, 3, 2, 1};
    uint8_t v64[8] = {0x37, 0x80, 0x40, 0x12, 2, 1, 4, 3};
    PortRomEndian kind;
    const uint8_t want[8] = {0x80, 0x37, 0x12, 0x40, 1, 2, 3, 4};

    if (port_rom_detect_endian(z64, 8, &kind) != 0 || kind != PORT_ROM_Z64)
        return fail("detect z64");
    if (port_rom_detect_endian(n64, 8, &kind) != 0 || kind != PORT_ROM_N64)
        return fail("detect n64");
    if (port_rom_detect_endian(v64, 8, &kind) != 0 || kind != PORT_ROM_V64)
        return fail("detect v64");
    if (port_rom_to_z64(z64, 8) != 0 || memcmp(z64, want, 8) != 0)
        return fail("to_z64 identity");
    if (port_rom_to_z64(n64, 8) != 0 || memcmp(n64, want, 8) != 0)
        return fail("to_z64 n64");
    if (port_rom_to_z64(v64, 8) != 0 || memcmp(v64, want, 8) != 0)
        return fail("to_z64 v64");
    return 0;
}

static int check_filelist(const char *csv_path)
{
    const char embedded[] =
        "100,3,assets/test.bin,0,1\n"
        "200,37,assets/hello.bin,1,1\n"
        "0,0,assets/skip.bin,0,0\n";
    const PortFilelistEntry *e;

    if (port_filelist_parse(embedded, strlen(embedded)) != 0)
        return fail("parse embedded csv");
    if (port_filelist_count() != 3)
        return fail("csv count");
    e = port_filelist_find("assets/test.bin");
    if (!e || e->offset != 100 || e->size != 3 || e->compressed || !e->extract)
        return fail("find test.bin");
    e = port_filelist_find("assets/hello.bin");
    if (!e || !e->compressed || !e->extract)
        return fail("find hello.bin compressed");
    e = port_filelist_find("assets/skip.bin");
    if (!e || e->extract)
        return fail("skip extract flag");
    if (port_filelist_find("nope") != NULL)
        return fail("missing name");

    if (csv_path) {
        if (port_filelist_load(csv_path) != 0)
            return fail("load synthetic.csv");
        if (port_filelist_count() != 3)
            return fail("file csv count");
        if (!port_filelist_find("assets/test.bin"))
            return fail("file csv find");
    }
    return 0;
}

static int check_dma(void)
{
    uint8_t *rom = (uint8_t *)calloc(256, 1);
    uint8_t out[40];
    OSMesg msgbuf;
    OSMesgQueue mq;
    OSIoMesg iomsg;

    if (!rom)
        return fail("calloc rom");
    rom[0] = 0x80;
    rom[1] = 0x37;
    rom[2] = 0x12;
    rom[3] = 0x40;
    rom[100] = 'A';
    rom[101] = 'B';
    rom[102] = 'C';
    memset(rom + 200, 0x11, 37);

    if (port_rom_attach(rom, 256) != 0)
        return fail("attach");
    if (port_rom_devaddr_to_offset(100) != 100)
        return fail("offset passthrough");
    if (port_rom_devaddr_to_offset(0xB0000064u) != 100)
        return fail("kseg1 cart mask");
    if (port_rom_devaddr_to_offset(0x10000064u) != 100)
        return fail("pi phys mask");

    memset(out, 0, sizeof out);
    if (port_rom_dma(out, 100, 3) != 0 || memcmp(out, "ABC", 3) != 0)
        return fail("dma offset");
    memset(out, 0, sizeof out);
    if (port_rom_dma(out, 0xB0000064u, 3) != 0 || memcmp(out, "ABC", 3) != 0)
        return fail("dma kseg1");
    memset(out, 0, sizeof out);
    if (port_filelist_dma_named(out, "assets/test.bin", 0, 3) != 0 || memcmp(out, "ABC", 3) != 0)
        return fail("dma named");
    memset(out, 0, sizeof out);
    if (port_filelist_dma_named(out, "assets/hello.bin", 0, 37) != 0)
        return fail("dma hello");
    if (out[0] != 0x11 || out[36] != 0x11)
        return fail("dma hello payload");
    if (port_rom_dma(out, 250, 16) != PORT_ROM_ERR_RANGE)
        return fail("dma oob");
    if (port_filelist_dma_named(out, "assets/test.bin", 0, 4) != PORT_ROM_ERR_RANGE)
        return fail("named oob");

    osCreateMesgQueue(&mq, &msgbuf, 1);
    memset(out, 0, sizeof out);
    memset(&iomsg, 0, sizeof iomsg);
    if (osPiStartDma(&iomsg, OS_MESG_PRI_NORMAL, OS_READ, 100, out, 3, &mq) != 0)
        return fail("osPiStartDma");
    if (osRecvMesg(&mq, NULL, OS_MESG_NOBLOCK) != 0)
        return fail("pi mesg");
    if (memcmp(out, "ABC", 3) != 0)
        return fail("pi payload");
    memset(out, 0, sizeof out);
    if (osPiRawStartDma(OS_READ, 100, out, 3) != 0 || memcmp(out, "ABC", 3) != 0)
        return fail("osPiRawStartDma");

    port_rom_close();
    if (port_rom_dma(out, 100, 3) != PORT_ROM_ERR_NOT_LOADED)
        return fail("dma after close");
    return 0;
}

static int check_real_filelist(const char *path)
{
    const PortFilelistEntry *e;
    if (port_filelist_load(path) != 0)
        return fail("load decomp filelist.u.csv");
    if (port_filelist_count() < 100)
        return fail("filelist.u.csv too small");
    e = port_filelist_find("assets/obseg/bg/bg_sev_all_p.bin");
    if (!e || e->offset != 4425312u || e->size != 69104u)
        return fail("bg_sev_all_p.bin offsets");
    return 0;
}

int main(int argc, char **argv)
{
    const char *csv = (argc >= 2) ? argv[1] : NULL;
    const char *real_csv = (argc >= 3) ? argv[2] : NULL;

    if (check_sha1())
        return 1;
    if (check_byteswap())
        return 1;
    if (check_filelist(csv))
        return 1;
    if (check_dma())
        return 1;
    if (real_csv && check_real_filelist(real_csv))
        return 1;

    puts("rom dma ok");
    return 0;
}
