#include <ultra64.h>
#include <string.h>

void osCreateMesgQueue(OSMesgQueue *mq, OSMesg *msg, s32 count)
{
    mq->mtqueue = NULL;
    mq->fullqueue = NULL;
    mq->validCount = 0;
    mq->first = 0;
    mq->msgCount = count;
    mq->msg = msg;
}

s32 osSendMesg(OSMesgQueue *mq, OSMesg msg, s32 flags)
{
    s32 idx;
    (void)flags;
    if (mq->validCount >= mq->msgCount) {
        return -1;
    }
    idx = (mq->first + mq->validCount) % mq->msgCount;
    mq->msg[idx] = msg;
    mq->validCount++;
    return 0;
}

s32 osJamMesg(OSMesgQueue *mq, OSMesg msg, s32 flags)
{
    return osSendMesg(mq, msg, flags);
}

s32 osRecvMesg(OSMesgQueue *mq, OSMesg *msg, s32 flags)
{
    (void)flags;
    if (mq->validCount <= 0) {
        return -1;
    }
    if (msg) {
        *msg = mq->msg[mq->first];
    }
    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;
    return 0;
}
