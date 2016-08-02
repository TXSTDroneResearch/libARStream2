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
    nalu->ntpTimestampLocal = 0;
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


void ARSTREAM2_H264_NaluCopy(ARSTREAM2_H264_NalUnit_t *dst, const ARSTREAM2_H264_NalUnit_t *src)
{
    if ((!src) || (!dst))
    {
        return;
    }

    dst->inputTimestamp = src->inputTimestamp;
    dst->timeoutTimestamp = src->timeoutTimestamp;
    dst->ntpTimestamp = src->ntpTimestamp;
    dst->ntpTimestampLocal = src->ntpTimestampLocal;
    dst->extRtpTimestamp = src->extRtpTimestamp;
    dst->rtpTimestamp = src->rtpTimestamp;
    dst->isLastInAu = src->isLastInAu;
    dst->seqNumForcedDiscontinuity = src->seqNumForcedDiscontinuity;
    dst->missingPacketsBefore = src->missingPacketsBefore;
    dst->importance = src->importance;
    dst->priority = src->priority;
    dst->metadata = src->metadata;
    dst->metadataSize = src->metadataSize;
    dst->nalu = src->nalu;
    dst->naluSize = src->naluSize;
    dst->auUserPtr = src->auUserPtr;
    dst->naluUserPtr = src->naluUserPtr;
    dst->naluType = src->naluType;
    dst->sliceType = src->sliceType;
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
    au->mbStatusAvailable = 0;
    au->syncType = ARSTREAM2_H264_AU_SYNC_TYPE_NONE;
    au->inputTimestamp = 0;
    au->timeoutTimestamp = 0;
    au->ntpTimestamp = 0;
    au->ntpTimestampLocal = 0;
    au->extRtpTimestamp = 0;
    au->rtpTimestamp = 0;
    au->naluCount = 0;
    au->naluHead = NULL;
    au->naluTail = NULL;
}


void ARSTREAM2_H264_AuCopy(ARSTREAM2_H264_AccessUnit_t *dst, const ARSTREAM2_H264_AccessUnit_t *src)
{
    if ((!src) || (!dst))
    {
        return;
    }

    dst->auSize = src->auSize;
    dst->metadataSize = src->metadataSize;
    dst->userDataSize = src->userDataSize;
    dst->syncType = src->syncType;
    dst->inputTimestamp = src->inputTimestamp;
    dst->timeoutTimestamp = src->timeoutTimestamp;
    dst->ntpTimestamp = src->ntpTimestamp;
    dst->ntpTimestampLocal = src->ntpTimestampLocal;
    dst->extRtpTimestamp = src->extRtpTimestamp;
    dst->rtpTimestamp = src->rtpTimestamp;
    dst->naluCount = 0;
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

    free(fifo->pool);
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
        //ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_H264_TAG, "NALU FIFO is empty");
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


int ARSTREAM2_H264_AuFifoInit(ARSTREAM2_H264_AuFifo_t *fifo, int itemMaxCount, int bufferMaxCount,
                              int auBufferSize, int metadataBufferSize, int userDataBufferSize)
{
    int i;
    ARSTREAM2_H264_AuFifoItem_t* curItem;
    ARSTREAM2_H264_AuFifoBuffer_t* curBuffer;

    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return -1;
    }
    if (itemMaxCount <= 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid item max count (%d)", itemMaxCount);
        return -1;
    }
    if (bufferMaxCount <= 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid buffer max count (%d)", bufferMaxCount);
        return -1;
    }

    memset(fifo, 0, sizeof(ARSTREAM2_H264_AuFifo_t));

    fifo->itemPoolSize = itemMaxCount;
    fifo->itemPool = malloc(itemMaxCount * sizeof(ARSTREAM2_H264_AuFifoItem_t));
    if (!fifo->itemPool)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "FIFO allocation failed (size %d)", itemMaxCount * sizeof(ARSTREAM2_H264_AuFifoItem_t));
        fifo->itemPoolSize = 0;
        return -1;
    }
    memset(fifo->itemPool, 0, itemMaxCount * sizeof(ARSTREAM2_H264_AuFifoItem_t));

    for (i = 0; i < itemMaxCount; i++)
    {
        curItem = &fifo->itemPool[i];
        if (fifo->itemFree)
        {
            fifo->itemFree->prev = curItem;
        }
        curItem->next = fifo->itemFree;
        curItem->prev = NULL;
        fifo->itemFree = curItem;
    }

    fifo->bufferPoolSize = bufferMaxCount;
    fifo->bufferPool = malloc(bufferMaxCount * sizeof(ARSTREAM2_H264_AuFifoBuffer_t));
    if (!fifo->bufferPool)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "FIFO allocation failed (size %d)", bufferMaxCount * sizeof(ARSTREAM2_H264_AuFifoBuffer_t));
        fifo->bufferPoolSize = 0;
        return -1;
    }
    memset(fifo->bufferPool, 0, bufferMaxCount * sizeof(ARSTREAM2_H264_AuFifoBuffer_t));

    for (i = 0; i < bufferMaxCount; i++)
    {
        curBuffer = &fifo->bufferPool[i];
        if (fifo->bufferFree)
        {
            fifo->bufferFree->prev = curBuffer;
        }
        curBuffer->next = fifo->bufferFree;
        curBuffer->prev = NULL;
        fifo->bufferFree = curBuffer;
    }

    if (auBufferSize > 0)
    {
        for (i = 0; i < bufferMaxCount; i++)
        {
            fifo->bufferPool[i].auBuffer = malloc(auBufferSize);
            if (!fifo->bufferPool[i].auBuffer)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "FIFO buffer allocation failed (size %d)", auBufferSize);
                ARSTREAM2_H264_AuFifoFree(fifo);
                return -1;
            }
            fifo->bufferPool[i].auBufferSize = auBufferSize;
        }
    }

    if (metadataBufferSize > 0)
    {
        for (i = 0; i < bufferMaxCount; i++)
        {
            fifo->bufferPool[i].metadataBuffer = malloc(metadataBufferSize);
            if (!fifo->bufferPool[i].metadataBuffer)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "FIFO buffer allocation failed (size %d)", metadataBufferSize);
                ARSTREAM2_H264_AuFifoFree(fifo);
                return -1;
            }
            fifo->bufferPool[i].metadataBufferSize = metadataBufferSize;
        }
    }

    if (userDataBufferSize > 0)
    {
        for (i = 0; i < bufferMaxCount; i++)
        {
            fifo->bufferPool[i].userDataBuffer = malloc(userDataBufferSize);
            if (!fifo->bufferPool[i].userDataBuffer)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "FIFO buffer allocation failed (size %d)", userDataBufferSize);
                ARSTREAM2_H264_AuFifoFree(fifo);
                return -1;
            }
            fifo->bufferPool[i].userDataBufferSize = userDataBufferSize;
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

    free(fifo->itemPool);

    if (fifo->bufferPool)
    {
        for (i = 0; i < fifo->bufferPoolSize; i++)
        {
            free(fifo->bufferPool[i].auBuffer);
            fifo->bufferPool[i].auBuffer = NULL;
            free(fifo->bufferPool[i].metadataBuffer);
            fifo->bufferPool[i].metadataBuffer = NULL;
            free(fifo->bufferPool[i].userDataBuffer);
            fifo->bufferPool[i].userDataBuffer = NULL;
            free(fifo->bufferPool[i].mbStatusBuffer);
            fifo->bufferPool[i].mbStatusBuffer = NULL;
        }

        free(fifo->bufferPool);
    }

    memset(fifo, 0, sizeof(ARSTREAM2_H264_AuFifo_t));

    return 0;
}


int ARSTREAM2_H264_AuFifoAddQueue(ARSTREAM2_H264_AuFifo_t *fifo, ARSTREAM2_H264_AuFifoQueue_t *queue)
{
    if ((!fifo) || (!queue))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return -1;
    }

    queue->count = 0;
    queue->head = NULL;
    queue->tail = NULL;

    queue->prev = NULL;
    queue->next = fifo->queue;
    if (queue->next)
    {
        queue->next->prev = queue;
    }
    fifo->queue = queue;
    fifo->queueCount++;

    return 0;
}


int ARSTREAM2_H264_AuFifoRemoveQueue(ARSTREAM2_H264_AuFifo_t *fifo, ARSTREAM2_H264_AuFifoQueue_t *queue)
{
    if ((!fifo) || (!queue))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return -1;
    }

    if (queue->prev)
    {
        queue->prev->next = queue->next;
    }
    if (queue->next)
    {
        queue->next->prev = queue->prev;
    }
    if ((!queue->prev) && (!queue->next))
    {
        fifo->queue = NULL;
    }
    fifo->queueCount--;
    queue->prev = NULL;
    queue->next = NULL;
    queue->count = 0;
    queue->head = NULL;
    queue->tail = NULL;

    return 0;
}


ARSTREAM2_H264_AuFifoBuffer_t* ARSTREAM2_H264_AuFifoGetBuffer(ARSTREAM2_H264_AuFifo_t *fifo)
{
    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return NULL;
    }

    if (fifo->bufferFree)
    {
        ARSTREAM2_H264_AuFifoBuffer_t* cur = fifo->bufferFree;
        fifo->bufferFree = cur->next;
        if (cur->next) cur->next->prev = NULL;
        cur->prev = NULL;
        cur->next = NULL;
        cur->refCount = 1;
        return cur;
    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "No free buffer in pool");
        return NULL;
    }
}


int ARSTREAM2_H264_AuFifoBufferAddRef(ARSTREAM2_H264_AuFifoBuffer_t *buffer)
{
    if (!buffer)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return -1;
    }

    buffer->refCount++;

    return 0;
}


int ARSTREAM2_H264_AuFifoUnrefBuffer(ARSTREAM2_H264_AuFifo_t *fifo, ARSTREAM2_H264_AuFifoBuffer_t *buffer)
{
    if ((!fifo) || (!buffer))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return -1;
    }

    if (buffer->refCount != 0)
    {
        buffer->refCount--;
    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_TAG, "Ref count is already null, this should not happen!");
    }

    if (buffer->refCount == 0)
    {
        if (fifo->bufferFree)
        {
            fifo->bufferFree->prev = buffer;
            buffer->next = fifo->bufferFree;
        }
        else
        {
            buffer->next = NULL;
        }
        fifo->bufferFree = buffer;
        buffer->prev = NULL;
    }

    return 0;
}


ARSTREAM2_H264_AuFifoItem_t* ARSTREAM2_H264_AuFifoPopFreeItem(ARSTREAM2_H264_AuFifo_t *fifo)
{
    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return NULL;
    }

    if (fifo->itemFree)
    {
        ARSTREAM2_H264_AuFifoItem_t* cur = fifo->itemFree;
        fifo->itemFree = cur->next;
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

    if (fifo->itemFree)
    {
        fifo->itemFree->prev = item;
        item->next = fifo->itemFree;
    }
    else
    {
        item->next = NULL;
    }
    fifo->itemFree = item;
    item->prev = NULL;

    return 0;
}


int ARSTREAM2_H264_AuFifoEnqueueItem(ARSTREAM2_H264_AuFifoQueue_t *queue, ARSTREAM2_H264_AuFifoItem_t *item)
{
    if ((!queue) || (!item))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return -1;
    }

    item->next = NULL;
    if (queue->tail)
    {
        queue->tail->next = item;
        item->prev = queue->tail;
    }
    else
    {
        item->prev = NULL;
    }
    queue->tail = item;
    if (!queue->head)
    {
        queue->head = item;
    }
    queue->count++;

    return 0;
}


ARSTREAM2_H264_AuFifoItem_t* ARSTREAM2_H264_AuFifoDequeueItem(ARSTREAM2_H264_AuFifoQueue_t *queue)
{
    ARSTREAM2_H264_AuFifoItem_t* cur;

    if (!queue)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return NULL;
    }

    if ((!queue->head) || (!queue->count))
    {
        //ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_H264_TAG, "FIFO is empty");
        return NULL;
    }

    cur = queue->head;
    if (cur->next)
    {
        cur->next->prev = NULL;
        queue->head = cur->next;
        queue->count--;
    }
    else
    {
        queue->head = NULL;
        queue->count = 0;
        queue->tail = NULL;
    }
    cur->prev = NULL;
    cur->next = NULL;

    return cur;
}


int ARSTREAM2_H264_AuFifoFlushQueue(ARSTREAM2_H264_AuFifo_t *fifo, ARSTREAM2_H264_AuFifoQueue_t *queue)
{
    ARSTREAM2_H264_AuFifoItem_t* item;
    int count = 0, fifoErr;

    if ((!fifo) || (!queue))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return -1;
    }

    do
    {
        item = ARSTREAM2_H264_AuFifoDequeueItem(queue);
        if (item)
        {
            if (item->au.buffer)
            {
                fifoErr = ARSTREAM2_H264_AuFifoUnrefBuffer(fifo, item->au.buffer);
                if (fifoErr != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "ARSTREAM2_H264_AuFifoUnrefBuffer() failed (%d)", fifoErr);
                }
            }
            fifoErr = ARSTREAM2_H264_AuFifoPushFreeItem(fifo, item);
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


int ARSTREAM2_H264_AuFifoFlush(ARSTREAM2_H264_AuFifo_t *fifo)
{
    ARSTREAM2_H264_AuFifoQueue_t *queue;
    ARSTREAM2_H264_AuFifoItem_t* item;
    int count = 0, fifoErr;

    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Invalid pointer");
        return -1;
    }

    if (!fifo->queue)
    {
        return 0;
    }

    for (queue = fifo->queue; queue; queue = queue->next)
    {
        do
        {
            item = ARSTREAM2_H264_AuFifoDequeueItem(queue);
            if (item)
            {
                if (item->au.buffer)
                {
                    fifoErr = ARSTREAM2_H264_AuFifoUnrefBuffer(fifo, item->au.buffer);
                    if (fifoErr != 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "ARSTREAM2_H264_AuFifoUnrefBuffer() failed (%d)", fifoErr);
                    }
                }
                int fifoErr = ARSTREAM2_H264_AuFifoPushFreeItem(fifo, item);
                if (fifoErr != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "ARSTREAM2_H264_AuFifoPushFreeItem() failed (%d)", fifoErr);
                }
                count++;
            }
        }
        while (item);
    }

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
        au->ntpTimestampLocal = naluItem->nalu.ntpTimestampLocal;
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


ARSTREAM2_H264_AuFifoItem_t* ARSTREAM2_H264_AuFifoDuplicateItem(ARSTREAM2_H264_AuFifo_t *auFifo,
                                                                ARSTREAM2_H264_NaluFifo_t *naluFifo,
                                                                ARSTREAM2_H264_AuFifoItem_t *auItem)
{
    int ret = 0, needFree = 0;
    ARSTREAM2_H264_AuFifoItem_t *auCopyItem;

    auCopyItem = ARSTREAM2_H264_AuFifoPopFreeItem(auFifo);
    if (auCopyItem)
    {
        ARSTREAM2_H264_AuCopy(&auCopyItem->au, &auItem->au);
        ARSTREAM2_H264_NaluFifoItem_t *naluItem, *naluCopyItem;
        for (naluItem = auItem->au.naluHead; naluItem; naluItem = naluItem->next)
        {
            naluCopyItem = ARSTREAM2_H264_NaluFifoPopFreeItem(naluFifo);
            if (naluCopyItem)
            {
                ARSTREAM2_H264_NaluCopy(&naluCopyItem->nalu, &naluItem->nalu);
                ret = ARSTREAM2_H264_AuEnqueueNalu(&auCopyItem->au, naluCopyItem);
                if (ret != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Failed to enqueue NALU item in AU");
                    ret = ARSTREAM2_H264_NaluFifoPushFreeItem(naluFifo, naluCopyItem);
                    if (ret != 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Failed to push free FIFO item");
                    }
                    needFree = 1;
                }
            }
            else
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Failed to pop free item from the NALU FIFO");
                ret = -1;
                needFree = 1;
            }
        }
        auCopyItem->au.buffer = auItem->au.buffer;
    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Failed to pop free item from the AU FIFO");
        ret = -1;
    }

    if (needFree)
    {
        ARSTREAM2_H264_NaluFifoItem_t *naluItem;
        while ((naluItem = ARSTREAM2_H264_AuDequeueNalu(&auCopyItem->au)) != NULL)
        {
            ret = ARSTREAM2_H264_NaluFifoPushFreeItem(naluFifo, naluItem);
            if (ret != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Failed to push free item in the NALU FIFO (%d)", ret);
            }
        }
        ret = ARSTREAM2_H264_AuFifoPushFreeItem(auFifo, auCopyItem);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Failed to push free item in the AU FIFO (%d)", ret);
        }
        needFree = 0;
        auCopyItem = NULL;
    }

    return auCopyItem;
}


int ARSTREAM2_H264_AuCheckSizeRealloc(ARSTREAM2_H264_AccessUnit_t *au, unsigned int size)
{
    if ((!au) || (!au->buffer))
    {
        return -1;
    }

    if (au->auSize + size > au->buffer->auBufferSize)
    {
        unsigned int newSize = au->auSize + size;
        if (newSize < au->buffer->auBufferSize + ARSTREAM2_H264_AU_MIN_REALLOC_SIZE) newSize = au->buffer->auBufferSize + ARSTREAM2_H264_AU_MIN_REALLOC_SIZE;
        au->buffer->auBuffer = realloc(au->buffer->auBuffer, newSize);
        if (au->buffer->auBuffer == NULL)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Access unit realloc failed (size %u)", newSize);
            return -1;
        }
        else
        {
            au->buffer->auBufferSize = newSize;
        }
    }

    return 0;
}


int ARSTREAM2_H264_AuMbStatusCheckSizeRealloc(ARSTREAM2_H264_AccessUnit_t *au, unsigned int mbCount)
{
    if ((!au) || (!au->buffer))
    {
        return -1;
    }

    if (mbCount * sizeof(uint8_t) > au->buffer->mbStatusBufferSize)
    {
        unsigned int newSize = mbCount * sizeof(uint8_t);
        au->buffer->mbStatusBuffer = realloc(au->buffer->mbStatusBuffer, newSize);
        if (au->buffer->mbStatusBuffer == NULL)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_TAG, "Access unit realloc failed (size %u)", newSize);
            return -1;
        }
        else
        {
            au->buffer->mbStatusBufferSize = newSize;
        }
    }

    return 0;
}
