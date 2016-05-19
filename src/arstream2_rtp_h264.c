/**
 * @file arstream2_rtp_h264.c
 * @brief Parrot Streaming Library - RTP H.264 payloading implementation
 * @date 04/25/2016
 * @author aurelien.barre@parrot.com
 */

#include "arstream2_rtp_h264.h"

#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <libARSAL/ARSAL_Print.h>
#include <libARStream2/arstream2_rtp_sender.h>


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


static int ARSTREAM2_RTPH264_Sender_SingleNaluPacket(ARSTREAM2_RTP_SenderContext_t *context,
                                                     ARSTREAM2_RTPH264_Nalu_t *nalu,
                                                     ARSTREAM2_RTP_PacketFifo_t *packetFifo)
{
    int ret = 0;

    ARSTREAM2_RTP_PacketFifoItem_t *item = ARSTREAM2_RTP_FifoPopFreeItem(packetFifo);
    if (item)
    {
        unsigned int offsetInBuffer = 0;
        uint8_t *payload = NULL, *headerExtension = NULL;
        unsigned int payloadSize = 0, headerExtensionSize = 0;

        if ((context->useRtpHeaderExtensions) && (nalu->metadata) && (nalu->metadataSize > 4))
        {
            uint32_t headerExtensionSize = (uint32_t)ntohs(*((uint16_t*)nalu->metadata + 1)) * 4 + 4;
            if (headerExtensionSize != nalu->metadataSize)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "RTP extension header size error: expected %d bytes, got length of %d bytes", nalu->metadataSize, headerExtensionSize);
            }
            else
            {
                if (nalu->metadataSize <= context->maxPacketSize)
                {
                    memcpy(item->packet.buffer + offsetInBuffer, nalu->metadata, nalu->metadataSize);
                    headerExtension = item->packet.buffer + offsetInBuffer;
                    headerExtensionSize = nalu->metadataSize;
                    offsetInBuffer += nalu->metadataSize;
                }
                else
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Header extension size exceeds max packet size (%d)", context->maxPacketSize);
                }
            }
        }
        if (offsetInBuffer + nalu->naluSize <= context->maxPacketSize)
        {
            memcpy(item->packet.buffer + offsetInBuffer, nalu->nalu, nalu->naluSize);
            payload = item->packet.buffer + offsetInBuffer;
            payloadSize = nalu->naluSize;
            offsetInBuffer += nalu->naluSize;
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Payload size exceeds max packet size (%d)", context->maxPacketSize);
        }

        context->seqNum += nalu->seqNumForcedDiscontinuity;
        ret = ARSTREAM2_RTP_Sender_GeneratePacket(context, &item->packet,
                                                  payload, payloadSize,
                                                  (headerExtensionSize > 0) ? headerExtension : NULL, headerExtensionSize,
                                                  nalu->ntpTimestamp, nalu->timeoutTimestamp,
                                                  context->seqNum, nalu->isLastInAu);

        context->packetCount += nalu->seqNumForcedDiscontinuity + 1;
        context->byteCount += payloadSize;
        context->seqNum++;

        if (ret == 0)
        {
            ret = ARSTREAM2_RTP_FifoEnqueueItem(packetFifo, item);
            if (ret != 0)
            {
                ARSTREAM2_RTP_FifoPushFreeItem(packetFifo, item);
            }
        }
    }

    return ret;
}


static int ARSTREAM2_RTPH264_Sender_FuAPackets(ARSTREAM2_RTP_SenderContext_t *context,
                                               ARSTREAM2_RTPH264_Nalu_t *nalu,
                                               unsigned int fragmentCount,
                                               ARSTREAM2_RTP_PacketFifo_t *packetFifo)
{
    int ret, status = 0;
    unsigned int i, offset;
    uint8_t fuIndicator, fuHeader;
    fuIndicator = fuHeader = *nalu->nalu;
    fuIndicator &= ~0x1F;
    fuIndicator |= ARSTREAM2_RTPH264_NALU_TYPE_FUA;
    fuHeader &= ~0xE0;
    unsigned int meanFragmentSize = (nalu->naluSize + ((context->useRtpHeaderExtensions) ? nalu->metadataSize : 0) + fragmentCount / 2) / fragmentCount;

    for (i = 0, offset = 1; i < fragmentCount; i++)
    {
        unsigned int fragmentSize = (i == fragmentCount - 1) ? nalu->naluSize - offset : meanFragmentSize;
        unsigned int fragmentOffset = 0;
        do
        {
            unsigned int packetSize = (fragmentSize - fragmentOffset > context->maxPacketSize - 2) ? context->maxPacketSize - 2 : fragmentSize - fragmentOffset;
            if ((context->useRtpHeaderExtensions) && (offset == 1) && (nalu->metadataSize < packetSize))
            {
                packetSize -= nalu->metadataSize;
            }

            if (packetSize + 2 <= context->maxPacketSize)
            {
                ARSTREAM2_RTP_PacketFifoItem_t *item = ARSTREAM2_RTP_FifoPopFreeItem(packetFifo);
                if (item)
                {
                    unsigned int offsetInBuffer = 0;
                    uint8_t *payload = NULL, *headerExtension = NULL;
                    unsigned int payloadSize = 0, headerExtensionSize = 0;
                    uint8_t startBit = 0, endBit = 0;

                    if ((context->useRtpHeaderExtensions) && (offset == 1) && (nalu->metadata) && (nalu->metadataSize > 4) && (nalu->metadataSize < packetSize))
                    {
                        uint32_t headerExtensionSize = (uint32_t)ntohs(*((uint16_t*)nalu->metadata + 1)) * 4 + 4;
                        if (headerExtensionSize != nalu->metadataSize)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "RTP extension header size error: expected %d bytes, got length of %d bytes", nalu->metadataSize, headerExtensionSize);
                        }
                        else
                        {
                            if (nalu->metadataSize <= context->maxPacketSize)
                            {
                                memcpy(item->packet.buffer + offsetInBuffer, nalu->metadata, nalu->metadataSize);
                                headerExtension = item->packet.buffer + offsetInBuffer;
                                headerExtensionSize = nalu->metadataSize;
                                offsetInBuffer += nalu->metadataSize;
                            }
                            else
                            {
                                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Header extension size exceeds max packet size (%d)", context->maxPacketSize);
                            }
                        }
                    }
                    if (offsetInBuffer + packetSize + 2 <= context->maxPacketSize)
                    {
                        memcpy(item->packet.buffer + offsetInBuffer + 2, nalu->nalu + offset, packetSize);
                        *(item->packet.buffer + offsetInBuffer) = fuIndicator;
                        startBit = (offset == 1) ? 0x80 : 0;
                        endBit = ((i == fragmentCount - 1) && (fragmentOffset + packetSize == fragmentSize)) ? 0x40 : 0;
                        *(item->packet.buffer + offsetInBuffer + 1) = fuHeader | startBit | endBit;
                        payload = item->packet.buffer + offsetInBuffer;
                        payloadSize = packetSize + 2;
                        offsetInBuffer += packetSize + 2;
                    }
                    else
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Payload size exceeds max packet size (%d)", context->maxPacketSize);
                    }

                    if (offset == 1) context->seqNum += nalu->seqNumForcedDiscontinuity;
                    ret = ARSTREAM2_RTP_Sender_GeneratePacket(context, &item->packet,
                                                              payload, payloadSize,
                                                              (headerExtensionSize > 0) ? headerExtension : NULL, headerExtensionSize,
                                                              nalu->ntpTimestamp, nalu->timeoutTimestamp,
                                                              context->seqNum, ((nalu->isLastInAu) && (endBit)) ? 1 : 0);

                    context->packetCount += nalu->seqNumForcedDiscontinuity + 1;
                    context->byteCount += payloadSize;
                    context->seqNum++;

                    if (ret == 0)
                    {
                        ret = ARSTREAM2_RTP_FifoEnqueueItem(packetFifo, item);
                        if (ret != 0)
                        {
                            ARSTREAM2_RTP_FifoPushFreeItem(packetFifo, item);
                        }
                    }
                }
            }
            else
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Time %llu: FU-A, packetSize + 2 > maxPacketSize (packetSize=%d)", nalu->ntpTimestamp, packetSize);
            }

            fragmentOffset += packetSize;
            offset += packetSize;
        }
        while (fragmentOffset != fragmentSize);
    }

    return status;
}


static int ARSTREAM2_RTPH264_Sender_BeginStapAPacket(ARSTREAM2_RTP_SenderContext_t *context,
                                                     ARSTREAM2_RTPH264_Nalu_t *nalu,
                                                     ARSTREAM2_RTP_PacketFifo_t *packetFifo)
{
    int ret = 0;

    context->stapItem = ARSTREAM2_RTP_FifoPopFreeItem(packetFifo);
    if (context->stapItem)
    {
        context->stapMaxNri = 0;
        context->stapOffsetInBuffer = 0;
        context->stapHeaderExtension = NULL;
        context->stapHeaderExtensionSize = 0;
        context->stapPayload = NULL;
        context->stapPayloadSize = 0;
        context->stapSeqNumForcedDiscontinuity = nalu->seqNumForcedDiscontinuity;
        context->stapNtpTimestamp = nalu->ntpTimestamp;
        context->stapTimeoutTimestamp = nalu->timeoutTimestamp;
        if ((context->useRtpHeaderExtensions) && (nalu->metadata) && (nalu->metadataSize > 4))
        {
            uint32_t headerExtensionSize = (uint32_t)ntohs(*((uint16_t*)nalu->metadata + 1)) * 4 + 4;
            if (headerExtensionSize != nalu->metadataSize)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "RTP extension header size error: expected %d bytes, got length of %d bytes", nalu->metadataSize, headerExtensionSize);
            }
            else
            {
                if (nalu->metadataSize <= context->maxPacketSize)
                {
                    memcpy(context->stapItem->packet.buffer + context->stapOffsetInBuffer, nalu->metadata, nalu->metadataSize);
                    context->stapHeaderExtension = context->stapItem->packet.buffer + context->stapOffsetInBuffer;
                    context->stapHeaderExtensionSize = nalu->metadataSize;
                    context->stapOffsetInBuffer += nalu->metadataSize;
                }
                else
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Header extension size exceeds max packet size (%d)", context->maxPacketSize);
                }
            }
        }
        if (context->stapOffsetInBuffer + 1 <= context->maxPacketSize)
        {
            context->stapPayload = context->stapItem->packet.buffer + context->stapOffsetInBuffer;
            context->stapPayloadSize = 1;
            context->stapOffsetInBuffer++;
            context->stapPending = 1;
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Payload size exceeds max packet size (%d)", context->maxPacketSize);
        }
    }

    return ret;
}


static int ARSTREAM2_RTPH264_Sender_AppendToStapAPacket(ARSTREAM2_RTP_SenderContext_t *context,
                                                        ARSTREAM2_RTPH264_Nalu_t *nalu)
{
    int ret = 0;

    if (context->stapOffsetInBuffer + 2 + nalu->naluSize <= context->maxPacketSize)
    {
        uint8_t nri = ((uint8_t)(*(nalu->nalu)) >> 5) & 0x3;
        if (nri > context->stapMaxNri) context->stapMaxNri = nri;
        *(context->stapItem->packet.buffer + context->stapOffsetInBuffer) = ((nalu->naluSize >> 8) & 0xFF);
        *(context->stapItem->packet.buffer + context->stapOffsetInBuffer + 1) = (nalu->naluSize & 0xFF);
        context->stapPayloadSize += 2;
        context->stapOffsetInBuffer += 2;
        memcpy(context->stapItem->packet.buffer + context->stapOffsetInBuffer, nalu->nalu, nalu->naluSize);
        context->stapPayloadSize += nalu->naluSize;
        context->stapOffsetInBuffer += nalu->naluSize;
    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "Payload size exceeds max packet size (%d)", context->maxPacketSize);
    }

    return ret;
}


static int ARSTREAM2_RTPH264_Sender_FinishStapAPacket(ARSTREAM2_RTP_SenderContext_t *context,
                                                      ARSTREAM2_RTP_PacketFifo_t *packetFifo, int markerBit)
{
    int ret = 0;
    uint8_t stapHeader;

    stapHeader = ARSTREAM2_RTPH264_NALU_TYPE_STAPA | ((context->stapMaxNri & 3) << 5);
    *(context->stapPayload) = stapHeader;
    context->seqNum += context->stapSeqNumForcedDiscontinuity;
    ret = ARSTREAM2_RTP_Sender_GeneratePacket(context, &context->stapItem->packet,
                                              context->stapPayload, context->stapPayloadSize,
                                              (context->stapHeaderExtensionSize > 0) ? context->stapHeaderExtension : NULL, context->stapHeaderExtensionSize,
                                              context->stapNtpTimestamp, context->stapTimeoutTimestamp,
                                              context->seqNum, markerBit);

    context->packetCount += context->stapSeqNumForcedDiscontinuity + 1;
    context->byteCount += context->stapPayloadSize;
    context->seqNum++;

    if (ret == 0)
    {
        ret = ARSTREAM2_RTP_FifoEnqueueItem(packetFifo, context->stapItem);
        if (ret != 0)
        {
            ARSTREAM2_RTP_FifoPushFreeItem(packetFifo, context->stapItem);
        }
    }
    context->stapPayloadSize = 0;
    context->stapHeaderExtensionSize = 0;
    context->stapPending = 0;

    return ret;
}


int ARSTREAM2_RTPH264_Sender_NaluFifoToPacketFifo(ARSTREAM2_RTP_SenderContext_t *context,
                                                  ARSTREAM2_RTPH264_NaluFifo_t *naluFifo,
                                                  ARSTREAM2_RTP_PacketFifo_t *packetFifo,
                                                  uint64_t curTime)
{
    ARSTREAM2_RTPH264_Nalu_t nalu;
    int ret = 0, fifoRes, naluCount = 0;

    fifoRes = ARSTREAM2_RTPH264_FifoDequeueNalu(naluFifo, &nalu);

    while (fifoRes == 0)
    {
        naluCount++;
        if ((context->previousTimestamp != 0) && (nalu.ntpTimestamp != context->previousTimestamp))
        {
            if (context->stapPending)
            {
                /* Finish the previous STAP-A packet */
                ret = ARSTREAM2_RTPH264_Sender_FinishStapAPacket(context, packetFifo, 0); // do not set the marker bit
                if (ret != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "ARSTREAM2_RTPH264_Sender_FinishStapAPacket() failed (%d)", ret);
                }
            }

            if (context->auCallback != NULL)
            {
                /* new Access Unit: do we need to call the auCallback? */
                if (context->previousTimestamp != context->lastAuCallbackTimestamp)
                {
                    context->lastAuCallbackTimestamp = context->previousTimestamp;

                    /* call the auCallback */
                    ((ARSTREAM2_RtpSender_AuCallback_t)context->auCallback)(ARSTREAM2_RTP_SENDER_STATUS_SENT, context->previousAuUserPtr, context->auCallbackUserPtr);
                }
            }
        }

        /* check that the NALU is not too old */
        if ((nalu.timeoutTimestamp == 0) || (nalu.timeoutTimestamp > curTime))
        {
            /* Fragments count evaluation */
            unsigned int fragmentCount = (nalu.naluSize + ((context->useRtpHeaderExtensions) ? nalu.metadataSize : 0) + context->targetPacketSize / 2) / context->targetPacketSize;
            if (fragmentCount < 1) fragmentCount = 1;

            if ((fragmentCount > 1) || (nalu.naluSize > context->maxPacketSize))
            {
                /* Fragmentation (FU-A) */

                if (context->stapPending)
                {
                    /* Finish the previous STAP-A packet */
                    ret = ARSTREAM2_RTPH264_Sender_FinishStapAPacket(context, packetFifo, 0); // do not set the marker bit
                    if (ret != 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "ARSTREAM2_RTPH264_Sender_FinishStapAPacket() failed (%d)", ret);
                    }
                }

                ret = ARSTREAM2_RTPH264_Sender_FuAPackets(context, &nalu, fragmentCount, packetFifo);
                if (ret != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "ARSTREAM2_RTPH264_Sender_FuAPackets() failed (%d)", ret);
                }
            }
            else
            {
                unsigned int newStapSize = ((!context->stapPending) ? sizeof(ARSTREAM2_RTP_Header_t) + ((context->useRtpHeaderExtensions) ? nalu.metadataSize : 0) + 1 : 0) + 2 + nalu.naluSize;
                if ((context->stapPayloadSize + context->stapHeaderExtensionSize + newStapSize >= context->maxPacketSize)
                        || (context->stapPayloadSize + context->stapHeaderExtensionSize + newStapSize > context->targetPacketSize)
                        || (nalu.seqNumForcedDiscontinuity))
                {
                    if (context->stapPending)
                    {
                        /* Finish the previous STAP-A packet */
                        ret = ARSTREAM2_RTPH264_Sender_FinishStapAPacket(context, packetFifo, 0); // do not set the marker bit
                        if (ret != 0)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "ARSTREAM2_RTPH264_Sender_FinishStapAPacket() failed (%d)", ret);
                        }
                    }
                }

                if ((context->stapPayloadSize + context->stapHeaderExtensionSize + newStapSize >= context->maxPacketSize)
                        || (context->stapPayloadSize + context->stapHeaderExtensionSize + newStapSize > context->targetPacketSize))
                {
                    /* Single NAL unit */
                    ret = ARSTREAM2_RTPH264_Sender_SingleNaluPacket(context, &nalu, packetFifo);
                    if (ret != 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "ARSTREAM2_RTPH264_Sender_SingleNaluPacket() failed (%d)", ret);
                    }
                }
                else
                {
                    /* Aggregation (STAP-A) */
                    if (!context->stapPending)
                    {
                        /* Start a new STAP-A packet */
                        ret = ARSTREAM2_RTPH264_Sender_BeginStapAPacket(context, &nalu, packetFifo);
                        if (ret != 0)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "ARSTREAM2_RTPH264_Sender_BeginStapAPacket() failed (%d)", ret);
                        }
                    }
                    if (context->stapPending)
                    {
                        /* Append to the current STAP-A packet */
                        ret = ARSTREAM2_RTPH264_Sender_AppendToStapAPacket(context, &nalu);
                        if (ret != 0)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "ARSTREAM2_RTPH264_Sender_AppendToStapAPacket() failed (%d)", ret);
                        }

                        if (nalu.isLastInAu)
                        {
                            /* Finish the STAP-A packet */
                            ret = ARSTREAM2_RTPH264_Sender_FinishStapAPacket(context, packetFifo, 1); // set the marker bit
                            if (ret != 0)
                            {
                                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTPH264_TAG, "ARSTREAM2_RTPH264_Sender_FinishStapAPacket() failed (%d)", ret);
                            }
                        }
                    }
                }
            }

            /* call the naluCallback */
            if (context->naluCallback != NULL)
            {
                ((ARSTREAM2_RtpSender_NaluCallback_t)context->naluCallback)(ARSTREAM2_RTP_SENDER_STATUS_SENT, nalu.naluUserPtr, context->naluCallbackUserPtr);
            }
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTPH264_TAG, "Time %llu: dropped NALU (%.1fms late) (seqNum = %d)",
                        nalu.ntpTimestamp, (float)(curTime - nalu.timeoutTimestamp) / 1000., context->seqNum - 1);

            /* call the monitoringCallback */
            if (context->monitoringCallback != NULL)
            {
                uint32_t rtpTimestamp = (nalu.ntpTimestamp * context->rtpClockRate + (uint64_t)context->rtpTimestampOffset + 500000) / 1000000;

                /* increment the sequence number to let the receiver know that we dropped something */
                context->seqNum += nalu.seqNumForcedDiscontinuity + 1;
                context->packetCount += nalu.seqNumForcedDiscontinuity + 1;
                context->byteCount += nalu.naluSize;

                context->monitoringCallback(curTime, nalu.ntpTimestamp, rtpTimestamp, context->seqNum - 1,
                                            nalu.isLastInAu, 0, nalu.naluSize, context->monitoringCallbackUserPtr);
            }

            /* call the naluCallback */
            if (context->naluCallback != NULL)
            {
                ((ARSTREAM2_RtpSender_NaluCallback_t)context->naluCallback)(ARSTREAM2_RTP_SENDER_STATUS_CANCELLED, nalu.naluUserPtr, context->naluCallbackUserPtr);
            }
        }

        /* last NALU in the Access Unit: call the auCallback */
        if ((context->auCallback != NULL) && (nalu.isLastInAu))
        {
            if (nalu.ntpTimestamp != context->lastAuCallbackTimestamp)
            {
                context->lastAuCallbackTimestamp = nalu.ntpTimestamp;

                /* call the auCallback */
                ((ARSTREAM2_RtpSender_AuCallback_t)context->auCallback)(ARSTREAM2_RTP_SENDER_STATUS_SENT, nalu.auUserPtr, context->auCallbackUserPtr);
            }
        }

        context->previousTimestamp = nalu.ntpTimestamp;
        context->previousAuUserPtr = nalu.auUserPtr;

        fifoRes = ARSTREAM2_RTPH264_FifoDequeueNalu(naluFifo, &nalu);
    }

    /*ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTP_SENDER_TAG, "Processed %d NALUs (packet FIFO count: %d)",
                naluCount, sender->packetFifo.count);*/ //TODO: debug

    return ret;
}
