#include <ultra64.h>

void debTryAdd(void *data, const char *name)
{
    (void)data;
    (void)name;
}

void osSetEventMesg(OSEvent e, OSMesgQueue *mq, OSMesg msg)
{
    (void)e;
    (void)mq;
    (void)msg;
}

void osInvalDCache(void *vaddr, s32 nbytes)
{
    (void)vaddr;
    (void)nbytes;
}

void osInvalICache(void *vaddr, s32 nbytes)
{
    (void)vaddr;
    (void)nbytes;
}

void osWritebackDCache(void *vaddr, s32 nbytes)
{
    (void)vaddr;
    (void)nbytes;
}

void osWritebackDCacheAll(void) {}

void osCreatePiManager(OSPri pri, OSMesgQueue *cmdQ, OSMesg *cmdBuf, s32 cmdMsgCnt)
{
    (void)pri;
    (void)cmdQ;
    (void)cmdBuf;
    (void)cmdMsgCnt;
}

s32 osContInit(OSMesgQueue *mq, u8 *pattern, OSContStatus *status)
{
    (void)mq;
    (void)pattern;
    (void)status;
    return 0;
}

s32 osContStartQuery(OSMesgQueue *mq)
{
    (void)mq;
    return 0;
}

void osContGetQuery(OSContStatus *status)
{
    (void)status;
}

s32 osContStartReadData(OSMesgQueue *mq)
{
    (void)mq;
    return 0;
}

void osContGetReadData(OSContPad *pad)
{
    (void)pad;
}

s32 osMotorInit(OSMesgQueue *mq, OSPfs *pfs, int channel)
{
    (void)mq;
    (void)pfs;
    (void)channel;
    return 0;
}

s32 osMotorStart(OSPfs *pfs)
{
    (void)pfs;
    return 0;
}

s32 osMotorStop(OSPfs *pfs)
{
    (void)pfs;
    return 0;
}

s32 osPfsInit(OSMesgQueue *mq, OSPfs *pfs, int channel)
{
    (void)mq;
    (void)pfs;
    (void)channel;
    return 0;
}

s32 osEepromProbe(OSMesgQueue *mq)
{
    (void)mq;
    return 0;
}

s32 osEepromRead(OSMesgQueue *mq, u8 address, u8 *buffer)
{
    (void)mq;
    (void)address;
    (void)buffer;
    return 0;
}

s32 osEepromWrite(OSMesgQueue *mq, u8 address, u8 *buffer)
{
    (void)mq;
    (void)address;
    (void)buffer;
    return 0;
}

s32 osEepromLongRead(OSMesgQueue *mq, u8 address, u8 *buffer, int nbytes)
{
    (void)mq;
    (void)address;
    (void)buffer;
    (void)nbytes;
    return 0;
}

s32 osEepromLongWrite(OSMesgQueue *mq, u8 address, u8 *buffer, int nbytes)
{
    (void)mq;
    (void)address;
    (void)buffer;
    (void)nbytes;
    return 0;
}
