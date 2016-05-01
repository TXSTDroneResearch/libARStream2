/**
 * @file arstream2_rtp_h264.c
 * @brief Parrot Streaming Library - RTP H.264 payloading implementation
 * @date 04/25/2016
 * @author aurelien.barre@parrot.com
 */

#include "arstream2_rtp_h264.h"

#include <stdlib.h>
#include <string.h>
#include <libARSAL/ARSAL_Print.h>


/**
 * Tag for ARSAL_PRINT
 */
#define ARSTREAM2_RTPH264_TAG "ARSTREAM2_Rtp"



int ARSTREAM2_RTPH264_FifoInit(ARSTREAM2_RTPH264_NaluFifo_t *fifo, int maxCount)
{
    int i;
    ARSTREAM2_RTPH264_NaluFifoItem_t* cur;

    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Invalid pointer");
        return -1;
    }

    if (maxCount <= 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Invalid FIFO size (%d)", maxCount);
        return -1;
    }

    memset(fifo, 0, sizeof(ARSTREAM2_RTPH264_NaluFifo_t));
    fifo->size = maxCount;
    fifo->pool = malloc(maxCount * sizeof(ARSTREAM2_RTPH264_NaluFifoItem_t));
    if (!fifo->pool)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "FIFO allocation failed (size %d)", maxCount * sizeof(ARSTREAM2_RTPH264_NaluFifoItem_t));
        return -1;
    }
    memset(fifo->pool, 0, maxCount * sizeof(ARSTREAM2_RTPH264_NaluFifoItem_t));

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


int ARSTREAM2_RTPH264_FifoFree(ARSTREAM2_RTPH264_NaluFifo_t *fifo)
{
    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Invalid pointer");
        return -1;
    }

    if (fifo->pool)
    {
        free(fifo->pool);
    }
    memset(fifo, 0, sizeof(ARSTREAM2_RTPH264_NaluFifo_t));

    return 0;
}


ARSTREAM2_RTPH264_NaluFifoItem_t* ARSTREAM2_RTPH264_FifoPopFreeItem(ARSTREAM2_RTPH264_NaluFifo_t *fifo)
{
    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Invalid pointer");
        return NULL;
    }

    if (fifo->free)
    {
        ARSTREAM2_RTPH264_NaluFifoItem_t* cur = fifo->free;
        fifo->free = cur->next;
        if (cur->next) cur->next->prev = NULL;
        cur->prev = NULL;
        cur->next = NULL;
        return cur;
    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "NALU FIFO is full");
        return NULL;
    }
}


int ARSTREAM2_RTPH264_FifoPushFreeItem(ARSTREAM2_RTPH264_NaluFifo_t *fifo, ARSTREAM2_RTPH264_NaluFifoItem_t *item)
{
    if ((!fifo) || (!item))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Invalid pointer");
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


int ARSTREAM2_RTPH264_FifoEnqueueItem(ARSTREAM2_RTPH264_NaluFifo_t *fifo, ARSTREAM2_RTPH264_NaluFifoItem_t *item)
{
    if ((!fifo) || (!item))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Invalid pointer");
        return -1;
    }

    if (fifo->count >= fifo->size)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "NALU FIFO is full");
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


ARSTREAM2_RTPH264_NaluFifoItem_t* ARSTREAM2_RTPH264_FifoDequeueItem(ARSTREAM2_RTPH264_NaluFifo_t *fifo)
{
    ARSTREAM2_RTPH264_NaluFifoItem_t* cur;

    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Invalid pointer");
        return NULL;
    }

    if ((!fifo->head) || (!fifo->count))
    {
        ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTPH264_TAG, "NALU FIFO is empty");
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


int ARSTREAM2_RTPH264_FifoEnqueueNalu(ARSTREAM2_RTPH264_NaluFifo_t *fifo, const ARSTREAM2_RTPH264_Nalu_t *nalu)
{
    if ((!fifo) || (!nalu))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Invalid pointer");
        return -1;
    }

    if (fifo->count >= fifo->size)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "NALU FIFO is full");
        return -2;
    }

    ARSTREAM2_RTPH264_NaluFifoItem_t* cur = ARSTREAM2_RTPH264_FifoPopFreeItem(fifo);
    if (!cur)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "No free element found in NALU FIFO, this should not happen!");
        return -3;
    }
    memcpy(&cur->nalu, nalu, sizeof(ARSTREAM2_RTPH264_Nalu_t));

    return ARSTREAM2_RTPH264_FifoEnqueueItem(fifo, cur);
}


int ARSTREAM2_RTPH264_FifoEnqueueNalus(ARSTREAM2_RTPH264_NaluFifo_t *fifo, const ARSTREAM2_RTPH264_Nalu_t *nalus, int naluCount)
{
    int i, ret = 0;

    if ((!fifo) || (!nalus) || (naluCount <= 0))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Invalid pointer");
        return -1;
    }

    if (fifo->count + naluCount >= fifo->size)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "NALU FIFO would be full");
        return -2;
    }

    for (i = 0; i < naluCount; i++)
    {
        ARSTREAM2_RTPH264_NaluFifoItem_t* cur = ARSTREAM2_RTPH264_FifoPopFreeItem(fifo);
        if (!cur)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "No free element found in NALU FIFO, this should not happen!");
            return -3;
        }
        memcpy(&cur->nalu, &nalus[i], sizeof(ARSTREAM2_RTPH264_Nalu_t));

        ret = ARSTREAM2_RTPH264_FifoEnqueueItem(fifo, cur);
        if (ret != 0)
        {
            break;
        }
    }

    return ret;
}


int ARSTREAM2_RTPH264_FifoDequeueNalu(ARSTREAM2_RTPH264_NaluFifo_t *fifo, ARSTREAM2_RTPH264_Nalu_t *nalu)
{
    ARSTREAM2_RTPH264_NaluFifoItem_t* cur;

    if ((!fifo) || (!nalu))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Invalid pointer");
        return -1;
    }

    if ((!fifo->head) || (!fifo->count))
    {
        ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTPH264_TAG, "NALU FIFO is empty");
        return -2;
    }

    cur = ARSTREAM2_RTPH264_FifoDequeueItem(fifo);
    if (!cur)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Failed to dequeue FIFO item");
        return -1;
    }

    memcpy(nalu, &cur->nalu, sizeof(ARSTREAM2_RTPH264_Nalu_t));

    int ret = ARSTREAM2_RTPH264_FifoPushFreeItem(fifo, cur);
    if (ret < 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Failed to push free FIFO item");
        return -1;
    }

    return 0;
}


int ARSTREAM2_RTPH264_FifoDequeueNalus(ARSTREAM2_RTPH264_NaluFifo_t *fifo, ARSTREAM2_RTPH264_Nalu_t *nalus, int maxNaluCount, int *naluCount)
{
    ARSTREAM2_RTPH264_NaluFifoItem_t* cur;
    int count = 0;

    if ((!fifo) || (!nalus) || (!naluCount))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Invalid pointer");
        return -1;
    }

    if (maxNaluCount <= 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Invalid array size");
        return -1;
    }

    if ((!fifo->head) || (!fifo->count))
    {
        ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTPH264_TAG, "NALU FIFO is empty");
        return -2;
    }

    while ((fifo->head) && (count < maxNaluCount))
    {
        cur = ARSTREAM2_RTPH264_FifoDequeueItem(fifo);
        if (!cur)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Failed to dequeue FIFO item");
            return -1;
        }

        memcpy(&nalus[count], &cur->nalu, sizeof(ARSTREAM2_RTPH264_Nalu_t));

        int ret = ARSTREAM2_RTPH264_FifoPushFreeItem(fifo, cur);
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Failed to push free FIFO item");
            return -1;
        }

        count++;
    }

    *naluCount = count;

    return 0;
}
