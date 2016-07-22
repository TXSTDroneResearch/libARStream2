/**
 * @file arstream2_h264.c
 * @brief Parrot Streaming Library - H.264 definitions
 * @date 07/18/2016
 * @author aurelien.barre@parrot.com
 */

#include "arstream2_h264.h"

#include <stdlib.h>
#include <string.h>
#include <libARSAL/ARSAL_Print.h>


/**
 * Tag for ARSAL_PRINT
 */
#define ARSTREAM2_H264_TAG "ARSTREAM2_H264"


void ARSTREAM2_H264_NaluReset(ARSTREAM2_H264_NalUnit_t *nalu)
{
    if (!nalu)
    {
        return;
    }

    nalu->inputTimestamp = 0;
    nalu->timeoutTimestamp = 0;
    nalu->ntpTimestamp = 0;
    nalu->extRtpTimestamp = 0;
    nalu->rtpTimestamp = 0;
    nalu->isLastInAu = 0;
    nalu->seqNumForcedDiscontinuity = 0;
    nalu->missingPacketsBefore = 0;
    nalu->importance = 0;
    nalu->priority = 0;
    nalu->metadata = NULL;
    nalu->metadataSize = 0;
    nalu->nalu = NULL;
    nalu->naluSize = 0;
    nalu->auUserPtr = NULL;
    nalu->naluUserPtr = NULL;
    nalu->naluType = ARSTREAM2_H264_NALU_TYPE_UNKNOWN;
    nalu->sliceType = ARSTREAM2_H264_SLICE_TYPE_NON_VCL;
}


void ARSTREAM2_H264_AuReset(ARSTREAM2_H264_AccessUnit_t *au)
{
    if (!au)
    {
        return;
    }

    au->auSize = 0;
    au->metadataSize = 0;
    au->userDataSize = 0;
    au->inputTimestamp = 0;
    au->timeoutTimestamp = 0;
    au->ntpTimestamp = 0;
    au->extRtpTimestamp = 0;
    au->rtpTimestamp = 0;
    au->naluCount = 0;
    au->naluHead = NULL;
    au->naluTail = NULL;
}


int ARSTREAM2_H264_NaluFifoInit(ARSTREAM2_H264_NaluFifo_t *fifo, int maxCount)
{
    int i;
    ARSTREAM2_H264_NaluFifoItem_t* cur;

    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return -1;
    }

    if (maxCount <= 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid FIFO size (%d)", maxCount);
        return -1;
    }

    memset(fifo, 0, sizeof(ARSTREAM2_H264_NaluFifo_t));
    fifo->size = maxCount;
    fifo->pool = malloc(maxCount * sizeof(ARSTREAM2_H264_NaluFifoItem_t));
    if (!fifo->pool)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "FIFO allocation failed (size %d)", maxCount * sizeof(ARSTREAM2_H264_NaluFifoItem_t));
        return -1;
    }
    memset(fifo->pool, 0, maxCount * sizeof(ARSTREAM2_H264_NaluFifoItem_t));

    for (i = 0; i < maxCount; i++)
    {
        cur = &fifo->pool[i];
        if (fifo->free)
        {
            fifo->free->prev = cur;
        }
        cur->next = fifo->free;
        cur->prev = NULL;
        fifo->free = cur;
    }

    return 0;
}


int ARSTREAM2_H264_NaluFifoFree(ARSTREAM2_H264_NaluFifo_t *fifo)
{
    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return -1;
    }

    if (fifo->pool)
    {
        free(fifo->pool);
    }
    memset(fifo, 0, sizeof(ARSTREAM2_H264_NaluFifo_t));

    return 0;
}


ARSTREAM2_H264_NaluFifoItem_t* ARSTREAM2_H264_NaluFifoPopFreeItem(ARSTREAM2_H264_NaluFifo_t *fifo)
{
    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return NULL;
    }

    if (fifo->free)
    {
        ARSTREAM2_H264_NaluFifoItem_t* cur = fifo->free;
        fifo->free = cur->next;
        if (cur->next) cur->next->prev = NULL;
        cur->prev = NULL;
        cur->next = NULL;
        return cur;
    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "NALU FIFO is full");
        return NULL;
    }
}


int ARSTREAM2_H264_NaluFifoPushFreeItem(ARSTREAM2_H264_NaluFifo_t *fifo, ARSTREAM2_H264_NaluFifoItem_t *item)
{
    if ((!fifo) || (!item))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return -1;
    }

    if (fifo->free)
    {
        fifo->free->prev = item;
        item->next = fifo->free;
    }
    else
    {
        item->next = NULL;
    }
    fifo->free = item;
    item->prev = NULL;

    return 0;
}


int ARSTREAM2_H264_NaluFifoEnqueueItem(ARSTREAM2_H264_NaluFifo_t *fifo, ARSTREAM2_H264_NaluFifoItem_t *item)
{
    if ((!fifo) || (!item))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return -1;
    }

    if (fifo->count >= fifo->size)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "NALU FIFO is full");
        return -2;
    }

    item->next = NULL;
    if (fifo->tail)
    {
        fifo->tail->next = item;
        item->prev = fifo->tail;
    }
    else
    {
        item->prev = NULL;
    }
    fifo->tail = item;
    if (!fifo->head)
    {
        fifo->head = item;
    }
    fifo->count++;

    return 0;
}


ARSTREAM2_H264_NaluFifoItem_t* ARSTREAM2_H264_NaluFifoDequeueItem(ARSTREAM2_H264_NaluFifo_t *fifo)
{
    ARSTREAM2_H264_NaluFifoItem_t* cur;

    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return NULL;
    }

    if ((!fifo->head) || (!fifo->count))
    {
        ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_H264_TAG, "NALU FIFO is empty");
        return NULL;
    }

    cur = fifo->head;
    if (cur->next)
    {
        cur->next->prev = NULL;
        fifo->head = cur->next;
        fifo->count--;
    }
    else
    {
        fifo->head = NULL;
        fifo->count = 0;
        fifo->tail = NULL;
    }
    cur->prev = NULL;
    cur->next = NULL;

    return cur;
}


int ARSTREAM2_H264_NaluFifoFlush(ARSTREAM2_H264_NaluFifo_t *fifo)
{
    ARSTREAM2_H264_NaluFifoItem_t* item;
    int count = 0;

    do
    {
        item = ARSTREAM2_H264_NaluFifoDequeueItem(fifo);
        if (item)
        {
            int fifoErr = ARSTREAM2_H264_NaluFifoPushFreeItem(fifo, item);
            if (fifoErr != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "ARSTREAM2_H264_NaluFifoPushFreeItem() failed (%d)", fifoErr);
            }
            count++;
        }
    }
    while (item);

    return count;
}


int ARSTREAM2_H264_AuFifoInit(ARSTREAM2_H264_AuFifo_t *fifo, int maxCount, int bufferSize,
                              int metadataBufferSize, int userDataBufferSize)
{
    int i;
    ARSTREAM2_H264_AuFifoItem_t* cur;

    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return -1;
    }

    if (maxCount <= 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid FIFO size (%d)", maxCount);
        return -1;
    }

    memset(fifo, 0, sizeof(ARSTREAM2_H264_AuFifo_t));
    fifo->size = maxCount;
    fifo->pool = malloc(maxCount * sizeof(ARSTREAM2_H264_AuFifoItem_t));
    if (!fifo->pool)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "FIFO allocation failed (size %d)", maxCount * sizeof(ARSTREAM2_H264_AuFifoItem_t));
        fifo->size = 0;
        return -1;
    }
    memset(fifo->pool, 0, maxCount * sizeof(ARSTREAM2_H264_AuFifoItem_t));

    for (i = 0; i < maxCount; i++)
    {
        cur = &fifo->pool[i];
        if (fifo->free)
        {
            fifo->free->prev = cur;
        }
        cur->next = fifo->free;
        cur->prev = NULL;
        fifo->free = cur;
    }

    if (bufferSize > 0)
    {
        for (i = 0; i < maxCount; i++)
        {
            fifo->pool[i].au.buffer = malloc(bufferSize);
            if (!fifo->pool[i].au.buffer)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "FIFO buffer allocation failed (size %d)", bufferSize);
                ARSTREAM2_H264_AuFifoFree(fifo);
                return -1;
            }
            fifo->pool[i].au.bufferSize = bufferSize;
        }
    }

    if (metadataBufferSize > 0)
    {
        for (i = 0; i < maxCount; i++)
        {
            fifo->pool[i].au.metadataBuffer = malloc(metadataBufferSize);
            if (!fifo->pool[i].au.metadataBuffer)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "FIFO buffer allocation failed (size %d)", metadataBufferSize);
                ARSTREAM2_H264_AuFifoFree(fifo);
                return -1;
            }
            fifo->pool[i].au.metadataBufferSize = metadataBufferSize;
        }
    }

    if (userDataBufferSize > 0)
    {
        for (i = 0; i < maxCount; i++)
        {
            fifo->pool[i].au.userDataBuffer = malloc(userDataBufferSize);
            if (!fifo->pool[i].au.userDataBuffer)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "FIFO buffer allocation failed (size %d)", userDataBufferSize);
                ARSTREAM2_H264_AuFifoFree(fifo);
                return -1;
            }
            fifo->pool[i].au.userDataBufferSize = userDataBufferSize;
        }
    }

    return 0;
}


int ARSTREAM2_H264_AuFifoFree(ARSTREAM2_H264_AuFifo_t *fifo)
{
    int i;

    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return -1;
    }

    if (fifo->pool)
    {
        for (i = 0; i < fifo->size; i++)
        {
            if (fifo->pool[i].au.buffer)
            {
                free(fifo->pool[i].au.buffer);
                fifo->pool[i].au.buffer = NULL;
            }
            if (fifo->pool[i].au.metadataBuffer)
            {
                free(fifo->pool[i].au.metadataBuffer);
                fifo->pool[i].au.metadataBuffer = NULL;
            }
            if (fifo->pool[i].au.userDataBuffer)
            {
                free(fifo->pool[i].au.userDataBuffer);
                fifo->pool[i].au.userDataBuffer = NULL;
            }
        }

        free(fifo->pool);
    }
    memset(fifo, 0, sizeof(ARSTREAM2_H264_AuFifo_t));

    return 0;
}


ARSTREAM2_H264_AuFifoItem_t* ARSTREAM2_H264_AuFifoPopFreeItem(ARSTREAM2_H264_AuFifo_t *fifo)
{
    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return NULL;
    }

    if (fifo->free)
    {
        ARSTREAM2_H264_AuFifoItem_t* cur = fifo->free;
        fifo->free = cur->next;
        if (cur->next) cur->next->prev = NULL;
        cur->prev = NULL;
        cur->next = NULL;
        return cur;
    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "AU FIFO is full");
        return NULL;
    }
}


int ARSTREAM2_H264_AuFifoPushFreeItem(ARSTREAM2_H264_AuFifo_t *fifo, ARSTREAM2_H264_AuFifoItem_t *item)
{
    if ((!fifo) || (!item))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return -1;
    }

    if (fifo->free)
    {
        fifo->free->prev = item;
        item->next = fifo->free;
    }
    else
    {
        item->next = NULL;
    }
    fifo->free = item;
    item->prev = NULL;

    return 0;
}


int ARSTREAM2_H264_AuFifoEnqueueItem(ARSTREAM2_H264_AuFifo_t *fifo, ARSTREAM2_H264_AuFifoItem_t *item)
{
    if ((!fifo) || (!item))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return -1;
    }

    if (fifo->count >= fifo->size)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "FIFO is full");
        return -2;
    }

    item->next = NULL;
    if (fifo->tail)
    {
        fifo->tail->next = item;
        item->prev = fifo->tail;
    }
    else
    {
        item->prev = NULL;
    }
    fifo->tail = item;
    if (!fifo->head)
    {
        fifo->head = item;
    }
    fifo->count++;

    return 0;
}


ARSTREAM2_H264_AuFifoItem_t* ARSTREAM2_H264_AuFifoDequeueItem(ARSTREAM2_H264_AuFifo_t *fifo)
{
    ARSTREAM2_H264_AuFifoItem_t* cur;

    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return NULL;
    }

    if ((!fifo->head) || (!fifo->count))
    {
        //ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_H264_TAG, "FIFO is empty"); //TODO: debug
        return NULL;
    }

    cur = fifo->head;
    if (cur->next)
    {
        cur->next->prev = NULL;
        fifo->head = cur->next;
        fifo->count--;
    }
    else
    {
        fifo->head = NULL;
        fifo->count = 0;
        fifo->tail = NULL;
    }
    cur->prev = NULL;
    cur->next = NULL;

    return cur;
}


int ARSTREAM2_H264_AuFifoFlush(ARSTREAM2_H264_AuFifo_t *fifo)
{
    ARSTREAM2_H264_AuFifoItem_t* item;
    int count = 0;

    do
    {
        item = ARSTREAM2_H264_AuFifoDequeueItem(fifo);
        if (item)
        {
            int fifoErr = ARSTREAM2_H264_AuFifoPushFreeItem(fifo, item);
            if (fifoErr != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "ARSTREAM2_H264_AuFifoPushFreeItem() failed (%d)", fifoErr);
            }
            count++;
        }
    }
    while (item);

    return count;
}


int ARSTREAM2_H264_AuEnqueueNalu(ARSTREAM2_H264_AccessUnit_t *au, ARSTREAM2_H264_NaluFifoItem_t *naluItem)
{
    if ((!au) || (!naluItem))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return -1;
    }

    if (au->naluCount == 0)
    {
        au->inputTimestamp = naluItem->nalu.inputTimestamp;
        au->timeoutTimestamp = naluItem->nalu.timeoutTimestamp;
        au->ntpTimestamp = naluItem->nalu.ntpTimestamp;
        au->extRtpTimestamp = naluItem->nalu.extRtpTimestamp;
        au->rtpTimestamp = naluItem->nalu.rtpTimestamp;
    }

    naluItem->next = NULL;
    if (au->naluTail)
    {
        au->naluTail->next = naluItem;
        naluItem->prev = au->naluTail;
    }
    else
    {
        naluItem->prev = NULL;
    }
    au->naluTail = naluItem;
    if (!au->naluHead)
    {
        au->naluHead = naluItem;
    }
    au->naluCount++;

    return 0;
}


int ARSTREAM2_H264_AuEnqueueNaluBefore(ARSTREAM2_H264_AccessUnit_t *au, ARSTREAM2_H264_NaluFifoItem_t *naluItem,
                                       ARSTREAM2_H264_NaluFifoItem_t *nextNaluItem)
{
    if ((!au) || (!naluItem) || (!nextNaluItem))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return -1;
    }

    naluItem->next = nextNaluItem;
    if (nextNaluItem->prev)
    {
        naluItem->prev = nextNaluItem->prev;
    }
    else
    {
        naluItem->prev = NULL;
        au->naluHead = naluItem;
    }
    nextNaluItem->prev = naluItem;
    au->naluCount++;

    return 0;
}


ARSTREAM2_H264_NaluFifoItem_t* ARSTREAM2_H264_AuDequeueNalu(ARSTREAM2_H264_AccessUnit_t *au)
{
    ARSTREAM2_H264_NaluFifoItem_t* cur;

    if (!au)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return NULL;
    }

    if ((!au->naluHead) || (!au->naluCount))
    {
        return NULL;
    }

    cur = au->naluHead;
    if (cur->next)
    {
        cur->next->prev = NULL;
        au->naluHead = cur->next;
        au->naluCount--;
    }
    else
    {
        au->naluHead = NULL;
        au->naluCount = 0;
        au->naluTail = NULL;
    }
    cur->prev = NULL;
    cur->next = NULL;

    return cur;
}


int ARSTREAM2_H264_AuCheckSizeRealloc(ARSTREAM2_H264_AccessUnit_t *au, unsigned int size)
{
    if (au->auSize + size > au->bufferSize)
    {
        unsigned int newSize = au->auSize + size;
        if (newSize < au->bufferSize + ARSTREAM2_H264_AU_MIN_REALLOC_SIZE) newSize = au->bufferSize + ARSTREAM2_H264_AU_MIN_REALLOC_SIZE;
        au->buffer = realloc(au->buffer, newSize);
        if (au->buffer == NULL)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Access unit realloc failed (size %u)", newSize);
            return -1;
        }
    }

    return 0;
}
