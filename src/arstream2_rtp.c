/**
 * @file arstream2_rtp.c
 * @brief Parrot Streaming Library - RTP implementation
 * @date 04/25/2016
 * @author aurelien.barre@parrot.com
 */

#include "arstream2_rtp.h"
#include "arstream2_rtcp.h"

#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <libARSAL/ARSAL_Print.h>


/**
 * Tag for ARSAL_PRINT
 */
#define ARSTREAM2_RTP_TAG "ARSTREAM2_Rtp"


#ifndef HAS_MMSG
struct mmsghdr {
    struct msghdr msg_hdr;  /* Message header */
    unsigned int  msg_len;  /* Number of received bytes for header */
};
#endif


void ARSTREAM2_RTP_PacketReset(ARSTREAM2_RTP_Packet_t *packet)
{
    if (!packet)
    {
        return;
    }

    packet->inputTimestamp = 0;
    packet->timeoutTimestamp = 0;
    packet->ntpTimestamp = 0;
    packet->ntpTimestampLocal = 0;
    packet->extRtpTimestamp = 0;
    packet->rtpTimestamp = 0;
    packet->seqNum = 0;
    packet->extSeqNum = 0;
    packet->markerBit = 0;
    packet->headerExtension = NULL;
    packet->headerExtensionSize = 0;
    packet->payload = NULL;
    packet->payloadSize = 0;
    packet->importance = 0;
    packet->priority = 0;
    packet->msgIovLength = 0;
}


int ARSTREAM2_RTP_FifoInit(ARSTREAM2_RTP_PacketFifo_t *fifo, int maxCount, int packetBufferSize)
{
    int i;
    ARSTREAM2_RTP_PacketFifoItem_t* cur = NULL;

    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid pointer");
        return -1;
    }

    if (maxCount <= 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid FIFO size (%d)", maxCount);
        return -1;
    }

    memset(fifo, 0, sizeof(ARSTREAM2_RTP_PacketFifo_t));
    fifo->size = maxCount;
    fifo->pool = malloc(maxCount * sizeof(ARSTREAM2_RTP_PacketFifoItem_t));
    if (!fifo->pool)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "FIFO allocation failed (size %d)", maxCount * sizeof(ARSTREAM2_RTP_PacketFifoItem_t));
        ARSTREAM2_RTP_FifoFree(fifo);
        return -1;
    }
    memset(fifo->pool, 0, maxCount * sizeof(ARSTREAM2_RTP_PacketFifoItem_t));

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

    if (packetBufferSize > 0)
    {
        for (i = 0; i < maxCount; i++)
        {
            fifo->pool[i].packet.buffer = malloc(packetBufferSize);
            if (!fifo->pool[i].packet.buffer)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "FIFO item packet buffer allocation failed (size %d)", packetBufferSize);
                ARSTREAM2_RTP_FifoFree(fifo);
                return -1;
            }
        }
    }

    fifo->msgVec = malloc(maxCount * sizeof(struct mmsghdr));
    if (!fifo->msgVec)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "FIFO allocation failed (size %d)", maxCount * sizeof(struct mmsghdr));
        ARSTREAM2_RTP_FifoFree(fifo);
        return -1;
    }
    memset(fifo->msgVec, 0, maxCount * sizeof(struct mmsghdr));

    return 0;
}


int ARSTREAM2_RTP_FifoFree(ARSTREAM2_RTP_PacketFifo_t *fifo)
{
    int i;

    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid pointer");
        return -1;
    }

    if (fifo->pool)
    {
        for (i = 0; i < fifo->size; i++)
        {
            if (fifo->pool[i].packet.buffer)
            {
                free(fifo->pool[i].packet.buffer);
                fifo->pool[i].packet.buffer = NULL;
            }
        }

        free(fifo->pool);
    }
    if (fifo->msgVec)
    {
        free(fifo->msgVec);
    }
    memset(fifo, 0, sizeof(ARSTREAM2_RTP_PacketFifo_t));

    return 0;
}


ARSTREAM2_RTP_PacketFifoItem_t* ARSTREAM2_RTP_FifoPopFreeItem(ARSTREAM2_RTP_PacketFifo_t *fifo)
{
    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid pointer");
        return NULL;
    }

    if (fifo->free)
    {
        ARSTREAM2_RTP_PacketFifoItem_t* cur = fifo->free;
        fifo->free = cur->next;
        if (cur->next) cur->next->prev = NULL;
        cur->prev = NULL;
        cur->next = NULL;
        return cur;
    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Packet FIFO is full");
        return NULL;
    }
}


int ARSTREAM2_RTP_FifoPushFreeItem(ARSTREAM2_RTP_PacketFifo_t *fifo, ARSTREAM2_RTP_PacketFifoItem_t *item)
{
    if ((!fifo) || (!item))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid pointer");
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


int ARSTREAM2_RTP_FifoEnqueueItem(ARSTREAM2_RTP_PacketFifo_t *fifo, ARSTREAM2_RTP_PacketFifoItem_t *item)
{
    if ((!fifo) || (!item))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid pointer");
        return -1;
    }

    if (fifo->count >= fifo->size)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Packet FIFO is full");
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


int ARSTREAM2_RTP_FifoEnqueueItemOrdered(ARSTREAM2_RTP_PacketFifo_t *fifo, ARSTREAM2_RTP_PacketFifoItem_t *item)
{
    ARSTREAM2_RTP_PacketFifoItem_t* cur;
    int outOfOrder = 0, duplicate = 0;

    if ((!fifo) || (!item))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid pointer");
        return -1;
    }

    if (fifo->count >= fifo->size)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Packet FIFO is full");
        return -2;
    }

    for (cur = fifo->tail; cur; cur = cur->prev)
    {
        if (cur->packet.extSeqNum == item->packet.extSeqNum)
        {
            duplicate = 1;
            break;
        }
        else if (cur->packet.extSeqNum < item->packet.extSeqNum)
        {
            break;
        }
        else
        {
            outOfOrder = 1;
        }
    }

    if (duplicate)
    {
        return -3;
    }

    if (cur)
    {
        /* insert after cur */
        item->next = cur->next;
        if (item->next)
        {
            item->next->prev = item;
        }
        else
        {
            fifo->tail = item;
        }
        item->prev = cur;
        cur->next = item;
        fifo->count++;
    }
    else
    {
        /* insert at head */
        item->next = fifo->head;
        if (fifo->head)
        {
            fifo->head->prev = item;
        }
        item->prev = NULL;
        fifo->head = item;
        if (!fifo->tail)
        {
            fifo->tail = item;
        }
        fifo->count++;
    }

    return (outOfOrder) ? 1 : 0;
}


ARSTREAM2_RTP_PacketFifoItem_t* ARSTREAM2_RTP_FifoDequeueItem(ARSTREAM2_RTP_PacketFifo_t *fifo)
{
    ARSTREAM2_RTP_PacketFifoItem_t* cur;

    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid pointer");
        return NULL;
    }

    if ((!fifo->head) || (!fifo->count))
    {
        ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_TAG, "Packet FIFO is empty");
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


ARSTREAM2_RTP_PacketFifoItem_t* ARSTREAM2_RTP_FifoPeekItem(ARSTREAM2_RTP_PacketFifo_t *fifo)
{
    ARSTREAM2_RTP_PacketFifoItem_t* cur;

    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid pointer");
        return NULL;
    }

    if ((!fifo->head) || (!fifo->count))
    {
        ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_TAG, "Packet FIFO is empty");
        return NULL;
    }

    cur = fifo->head;

    return cur;
}


int ARSTREAM2_RTP_FifoEnqueuePacket(ARSTREAM2_RTP_PacketFifo_t *fifo, const ARSTREAM2_RTP_Packet_t *packet)
{
    if ((!fifo) || (!packet))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid pointer");
        return -1;
    }

    if (fifo->count >= fifo->size)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Packet FIFO is full");
        return -2;
    }

    ARSTREAM2_RTP_PacketFifoItem_t* cur = ARSTREAM2_RTP_FifoPopFreeItem(fifo);
    if (!cur)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "No free element found in packet FIFO, this should not happen!");
        return -3;
    }
    memcpy(&cur->packet, packet, sizeof(ARSTREAM2_RTP_Packet_t));

    return ARSTREAM2_RTP_FifoEnqueueItem(fifo, cur);
}


int ARSTREAM2_RTP_FifoEnqueuePackets(ARSTREAM2_RTP_PacketFifo_t *fifo, const ARSTREAM2_RTP_Packet_t *packets, int packetCount)
{
    int i, ret = 0;

    if ((!fifo) || (!packets) || (packetCount <= 0))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid pointer");
        return -1;
    }

    if (fifo->count + packetCount >= fifo->size)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Packet FIFO would be full");
        return -2;
    }

    for (i = 0; i < packetCount; i++)
    {
        ARSTREAM2_RTP_PacketFifoItem_t* cur = ARSTREAM2_RTP_FifoPopFreeItem(fifo);
        if (!cur)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "No free element found in packet FIFO, this should not happen!");
            return -3;
        }
        memcpy(&cur->packet, &packets[i], sizeof(ARSTREAM2_RTP_Packet_t));

        ret = ARSTREAM2_RTP_FifoEnqueueItem(fifo, cur);
        if (ret != 0)
        {
            break;
        }
    }

    return 0;
}


int ARSTREAM2_RTP_FifoDequeuePacket(ARSTREAM2_RTP_PacketFifo_t *fifo, ARSTREAM2_RTP_Packet_t *packet)
{
    ARSTREAM2_RTP_PacketFifoItem_t* cur;

    if ((!fifo) || (!packet))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid pointer");
        return -1;
    }

    if ((!fifo->head) || (!fifo->count))
    {
        ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_TAG, "Packet FIFO is empty");
        return -2;
    }

    cur = ARSTREAM2_RTP_FifoDequeueItem(fifo);
    if (!cur)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Failed to dequeue FIFO item");
        return -1;
    }

    memcpy(packet, &cur->packet, sizeof(ARSTREAM2_RTP_Packet_t));

    int ret = ARSTREAM2_RTP_FifoPushFreeItem(fifo, cur);
    if (ret < 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Failed to push free FIFO item");
        return -1;
    }

    return 0;
}


int ARSTREAM2_RTP_FifoDequeuePackets(ARSTREAM2_RTP_PacketFifo_t *fifo, ARSTREAM2_RTP_Packet_t *packets, int maxPacketCount, int *packetCount)
{
    ARSTREAM2_RTP_PacketFifoItem_t* cur;
    int count = 0;

    if ((!fifo) || (!packets) || (!packetCount))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid pointer");
        return -1;
    }

    if (maxPacketCount <= 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid array size");
        return -1;
    }

    if ((!fifo->head) || (!fifo->count))
    {
        ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_TAG, "Packet FIFO is empty");
        return -2;
    }

    while ((fifo->head) && (count < maxPacketCount))
    {
        cur = ARSTREAM2_RTP_FifoDequeueItem(fifo);
        if (!cur)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Failed to dequeue FIFO item");
            return -1;
        }

        memcpy(&packets[count], &cur->packet, sizeof(ARSTREAM2_RTP_Packet_t));

        int ret = ARSTREAM2_RTP_FifoPushFreeItem(fifo, cur);
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Failed to push free FIFO item");
            return -1;
        }

        count++;
    }

    *packetCount = count;

    return 0;
}


int ARSTREAM2_RTP_Sender_FifoFillMsgVec(ARSTREAM2_RTP_PacketFifo_t *fifo)
{
    ARSTREAM2_RTP_PacketFifoItem_t* cur = NULL;
    int i;

    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid pointer");
        return -1;
    }

    if ((!fifo->head) || (!fifo->count))
    {
        ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_TAG, "Packet FIFO is empty");
        return -2;
    }

    for (cur = fifo->head, i = 0; ((cur) && (i < fifo->size)); cur = cur->next, i++)
    {
        fifo->msgVec[i].msg_hdr.msg_name = NULL;
        fifo->msgVec[i].msg_hdr.msg_namelen = 0;
        fifo->msgVec[i].msg_hdr.msg_iov = cur->packet.msgIov;
        fifo->msgVec[i].msg_hdr.msg_iovlen = cur->packet.msgIovLength;
        fifo->msgVec[i].msg_hdr.msg_control = NULL;
        fifo->msgVec[i].msg_hdr.msg_controllen = 0;
        fifo->msgVec[i].msg_hdr.msg_flags = 0;
        fifo->msgVec[i].msg_len = 0;
    }

    return i;
}


int ARSTREAM2_RTP_Sender_FifoCleanFromMsgVec(ARSTREAM2_RTP_SenderContext_t *context,
                                             ARSTREAM2_RTP_PacketFifo_t *fifo, unsigned int msgVecCount, uint64_t curTime)
{
    ARSTREAM2_RTP_PacketFifoItem_t* cur = NULL;
    unsigned int i;

    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid pointer");
        return -1;
    }

    if ((!fifo->head) || (!fifo->count))
    {
        ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_TAG, "Packet FIFO is empty");
        return -2;
    }

    for (cur = fifo->head, i = 0; ((cur != NULL) && (i < msgVecCount)); cur = fifo->head, i++)
    {
        size_t k, len;
        for (k = 0, len = 0; k < fifo->msgVec[i].msg_hdr.msg_iovlen; k++)
        {
            len += fifo->msgVec[i].msg_hdr.msg_iov[k].iov_len;
        }
        if (fifo->msgVec[i].msg_len != len)
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTP_TAG, "Sent size (%d) does not match message iov total size (%d)", fifo->msgVec[i].msg_len, len);
        }

        /* call the monitoringCallback */
        if (context->monitoringCallback != NULL)
        {
            context->monitoringCallback(cur->packet.inputTimestamp, curTime, cur->packet.ntpTimestamp, cur->packet.rtpTimestamp,
                                        cur->packet.seqNum, cur->packet.markerBit, cur->packet.payloadSize, 0, context->monitoringCallbackUserPtr);
        }

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

        int ret = ARSTREAM2_RTP_FifoPushFreeItem(fifo, cur);
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Failed to push free FIFO item");
            return -1;
        }
    }

    return (int)i;
}


int ARSTREAM2_RTP_Sender_FifoCleanFromTimeout(ARSTREAM2_RTP_SenderContext_t *context,
                                              ARSTREAM2_RTP_PacketFifo_t *fifo, uint64_t curTime)
{
    ARSTREAM2_RTP_PacketFifoItem_t *cur = NULL, *next = NULL;
    int count;

    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid pointer");
        return -1;
    }

    if (!curTime)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid current time");
        return -1;
    }

    if ((!fifo->head) || (!fifo->count))
    {
        ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_TAG, "Packet FIFO is empty");
        return -2;
    }

    for (cur = fifo->head, count = 0; cur != NULL; cur = next)
    {
        if ((cur->packet.timeoutTimestamp != 0) && (cur->packet.timeoutTimestamp <= curTime))
        {
            /* call the monitoringCallback */
            if (context->monitoringCallback != NULL)
            {
                context->monitoringCallback(cur->packet.inputTimestamp, curTime, cur->packet.ntpTimestamp, cur->packet.rtpTimestamp, cur->packet.seqNum,
                                            cur->packet.markerBit, 0, cur->packet.payloadSize, context->monitoringCallbackUserPtr);
            }

            if (cur->next)
            {
                cur->next->prev = cur->prev;
            }
            else
            {
                fifo->tail = cur->prev;
            }
            if (cur->prev)
            {
                cur->prev->next = cur->next;
            }
            else
            {
                fifo->head = cur->next;
            }
            fifo->count--;
            count++;

            next = cur->next;

            int ret = ARSTREAM2_RTP_FifoPushFreeItem(fifo, cur);
            if (ret < 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Failed to push free FIFO item");
                return -1;
            }
        }
        else
        {
            next = cur->next;
        }
    }

    return count;
}


int ARSTREAM2_RTP_Sender_FifoFlush(ARSTREAM2_RTP_SenderContext_t *context,
                                   ARSTREAM2_RTP_PacketFifo_t *fifo, uint64_t curTime)
{
    ARSTREAM2_RTP_PacketFifoItem_t *cur = NULL, *next = NULL;
    int count;

    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid pointer");
        return -1;
    }

    if (!curTime)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid current time");
        return -1;
    }

    if ((!fifo->head) || (!fifo->count))
    {
        ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_TAG, "Packet FIFO is empty");
        return -2;
    }

    for (cur = fifo->head, count = 0; cur != NULL; cur = next)
    {
        /* call the monitoringCallback */
        if (context->monitoringCallback != NULL)
        {
            context->monitoringCallback(cur->packet.inputTimestamp, curTime, cur->packet.ntpTimestamp, cur->packet.rtpTimestamp, cur->packet.seqNum,
                                        cur->packet.markerBit, 0, cur->packet.payloadSize, context->monitoringCallbackUserPtr);
        }

        if (cur->next)
        {
            cur->next->prev = cur->prev;
        }
        else
        {
            fifo->tail = cur->prev;
        }
        if (cur->prev)
        {
            cur->prev->next = cur->next;
        }
        else
        {
            fifo->head = cur->next;
        }
        fifo->count--;
        count++;

        next = cur->next;

        int ret = ARSTREAM2_RTP_FifoPushFreeItem(fifo, cur);
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Failed to push free FIFO item");
            return -1;
        }
    }

    return count;
}


int ARSTREAM2_RTP_Sender_GeneratePacket(ARSTREAM2_RTP_SenderContext_t *context, ARSTREAM2_RTP_Packet_t *packet,
                                        uint8_t *payload, unsigned int payloadSize,
                                        uint8_t *headerExtension, unsigned int headerExtensionSize,
                                        uint64_t ntpTimestamp, uint64_t inputTimestamp,
                                        uint64_t timeoutTimestamp, uint16_t seqNum, uint32_t markerBit,
                                        uint32_t importance, uint32_t priority)
{
    uint16_t flags;

    if ((!context) || (!packet) || (!payload))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid pointer");
        return -1;
    }

    if (payloadSize == 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid payload size (%d)", payloadSize);
        return -1;
    }

    /* Timestamps and sequence number */
    packet->inputTimestamp = inputTimestamp;
    packet->timeoutTimestamp = timeoutTimestamp;
    packet->ntpTimestamp = ntpTimestamp;
    packet->rtpTimestamp = (ntpTimestamp * context->rtpClockRate + (uint64_t)context->rtpTimestampOffset + 500000) / 1000000;
    packet->seqNum = seqNum;
    packet->markerBit = markerBit;
    packet->importance = importance;
    packet->priority = priority;

    /* Data */
    if ((headerExtension) && (headerExtensionSize > 0))
    {
        packet->headerExtension = headerExtension;
        packet->headerExtensionSize = headerExtensionSize;
    }
    packet->payload = payload;
    packet->payloadSize = payloadSize;

    /* Fill RTP packet header */
    flags = 0x8060; /* with PT=96 */
    if (headerExtensionSize > 0)
    {
        /* set the extention bit */
        flags |= (1 << 12);
    }
    if (markerBit)
    {
        /* set the marker bit */
        flags |= (1 << 7);
    }
    packet->header.flags = htons(flags);
    packet->header.seqNum = htons(seqNum);
    packet->header.timestamp = htonl(packet->rtpTimestamp);
    packet->header.ssrc = htonl(context->senderSsrc);

    /* Fill the IOV array */
    packet->msgIovLength = 0;
    packet->msgIov[packet->msgIovLength].iov_base = (void*)&packet->header;
    packet->msgIov[packet->msgIovLength].iov_len = (size_t)sizeof(ARSTREAM2_RTP_Header_t);
    packet->msgIovLength++;
    if (headerExtensionSize > 0)
    {
        packet->msgIov[packet->msgIovLength].iov_base = (void*)packet->headerExtension;
        packet->msgIov[packet->msgIovLength].iov_len = (size_t)headerExtensionSize;
        packet->msgIovLength++;
    }
    packet->msgIov[packet->msgIovLength].iov_base = (void*)packet->payload;
    packet->msgIov[packet->msgIovLength].iov_len = (size_t)payloadSize;
    packet->msgIovLength++;

    return 0;
}


/* WARNING: the call sequence ARSTREAM2_RTP_Receiver_FifoFillMsgVec -> recvmmsg -> ARSTREAM2_RTP_Receiver_FifoAddFromMsgVec
   must not be broken (no change made to the free items list) */
int ARSTREAM2_RTP_Receiver_FifoFillMsgVec(ARSTREAM2_RTP_ReceiverContext_t *context, ARSTREAM2_RTP_PacketFifo_t *fifo)
{
    ARSTREAM2_RTP_PacketFifoItem_t* cur = NULL;
    int i;

    if ((!context) || (!fifo))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid pointer");
        return -1;
    }

    if (!fifo->free)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Packet FIFO is full => flush to recover (count = %d)", fifo->count);
        int ret = ARSTREAM2_RTP_Receiver_FifoFlush(fifo);
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "ARSTREAM2_RTP_Receiver_FifoFlush() failed (%d)", ret);
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "%d packets flushed", ret);
        }
    }

    for (cur = fifo->free, i = 0; ((cur) && (i < fifo->size)); cur = cur->next, i++)
    {
        /* RTP header */
        cur->packet.msgIov[0].iov_base = &cur->packet.header;
        cur->packet.msgIov[0].iov_len = sizeof(ARSTREAM2_RTP_Header_t);

        /* RTP payload */
        cur->packet.msgIov[1].iov_base = cur->packet.buffer;
        cur->packet.msgIov[1].iov_len = context->maxPacketSize;

        fifo->msgVec[i].msg_hdr.msg_name = NULL;
        fifo->msgVec[i].msg_hdr.msg_namelen = 0;
        fifo->msgVec[i].msg_hdr.msg_iov = cur->packet.msgIov;
        fifo->msgVec[i].msg_hdr.msg_iovlen = 2;
        fifo->msgVec[i].msg_hdr.msg_control = NULL;
        fifo->msgVec[i].msg_hdr.msg_controllen = 0;
        fifo->msgVec[i].msg_hdr.msg_flags = 0;
        fifo->msgVec[i].msg_len = 0;
    }

    return i;
}


/* WARNING: the call sequence ARSTREAM2_RTP_Receiver_FifoFillMsgVec -> recvmmsg -> ARSTREAM2_RTP_Receiver_FifoAddFromMsgVec
   must not be broken (no change made to the free items list) */
int ARSTREAM2_RTP_Receiver_FifoAddFromMsgVec(ARSTREAM2_RTP_ReceiverContext_t *context,
                                             ARSTREAM2_RTP_PacketFifo_t *fifo, unsigned int msgVecCount, uint64_t curTime,
                                             ARSTREAM2_RTCP_ReceiverContext_t *rtcpContext)
{
    ARSTREAM2_RTP_PacketFifoItem_t* item = NULL;
    ARSTREAM2_RTP_PacketFifoItem_t* garbage = NULL;
    int ret = 0, garbageCount = 0, garbageCount2 = 0;
    unsigned int i, popCount = 0, enqueueCount = 0;

    if ((!context) || (!fifo) || (!rtcpContext))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Invalid pointer");
        return -1;
    }

    if (!msgVecCount)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Empty msgVec array");
        return -2;
    }

    uint64_t recvRtpTimestamp = (curTime * context->rtpClockRate + 500000) / 1000000;

    for (i = 0; i < msgVecCount; i++)
    {
        item = ARSTREAM2_RTP_FifoPopFreeItem(fifo);
        if (item)
        {
            ARSTREAM2_RTP_PacketReset(&item->packet);
            popCount++;
            if (fifo->msgVec[i].msg_len > sizeof(ARSTREAM2_RTP_Header_t))
            {
                uint16_t flags;
                int seqNumDelta = 0;

                item->packet.inputTimestamp = curTime;
                item->packet.rtpTimestamp = ntohl(item->packet.header.timestamp);
                item->packet.ntpTimestamp = ARSTREAM2_RTCP_Receiver_GetNtpTimestampFromRtpTimestamp(rtcpContext, item->packet.rtpTimestamp);
                item->packet.ntpTimestampLocal = ((rtcpContext->clockDelta.clockDeltaAvg != 0) && (item->packet.ntpTimestamp != 0)) ? (item->packet.ntpTimestamp - rtcpContext->clockDelta.clockDeltaAvg) : 0;
                item->packet.timeoutTimestamp = curTime + context->nominalDelay; //TODO: compute the expected arrival time
                item->packet.seqNum = ntohs(item->packet.header.seqNum);
                if (context->previousExtSeqNum != -1)
                {
                    item->packet.extSeqNum = (context->extHighestSeqNum & 0xFFFF0000) | ((uint32_t)item->packet.seqNum & 0xFFFF);
                    if ((int64_t)item->packet.extSeqNum - (int64_t)context->previousExtSeqNum < -32768)
                    {
                        item->packet.extSeqNum += 65536;
                    }
                    else if ((int64_t)item->packet.extSeqNum - (int64_t)context->previousExtSeqNum > 32768)
                    {
                        item->packet.extSeqNum -= 65536;
                    }
                    seqNumDelta = item->packet.extSeqNum - context->extHighestSeqNum;
                    if (item->packet.extSeqNum > context->extHighestSeqNum)
                    {
                        context->extHighestSeqNum = item->packet.extSeqNum;
                        rtcpContext->extHighestSeqNum = context->extHighestSeqNum;
                    }
                    item->packet.extRtpTimestamp = (context->extHighestRtpTimestamp & 0xFFFFFFFF00000000LL) | ((uint64_t)item->packet.rtpTimestamp & 0xFFFFFFFFLL);
                    if ((int64_t)item->packet.extRtpTimestamp - (int64_t)context->previousExtRtpTimestamp < -2147483648)
                    {
                        item->packet.extSeqNum += 0x100000000LL;
                    }
                    else if ((int64_t)item->packet.extRtpTimestamp - (int64_t)context->previousExtRtpTimestamp > 2147483648)
                    {
                        item->packet.extSeqNum -= 0x100000000LL;
                    }
                    if (item->packet.extRtpTimestamp > context->extHighestRtpTimestamp)
                    {
                        context->extHighestRtpTimestamp = item->packet.extRtpTimestamp;
                    }
                }
                else
                {
                    item->packet.extSeqNum = item->packet.seqNum;
                    item->packet.extRtpTimestamp = item->packet.rtpTimestamp;
                    context->extHighestSeqNum = item->packet.extSeqNum;
                    context->extHighestRtpTimestamp = item->packet.extRtpTimestamp;
                    context->previousRecvRtpTimestamp = recvRtpTimestamp;
                    context->previousExtRtpTimestamp = item->packet.extRtpTimestamp;
                    rtcpContext->senderSsrc = ntohl(item->packet.header.ssrc);
                    rtcpContext->firstSeqNum = item->packet.seqNum;
                    rtcpContext->extHighestSeqNum = context->extHighestSeqNum;
                    rtcpContext->packetsReceived = 0;
                    rtcpContext->packetsLost = 0;
                }
                context->previousExtSeqNum = (int32_t)item->packet.extSeqNum;
                flags = ntohs(item->packet.header.flags);
                if (flags & (1 << 7))
                {
                    /* the marker bit is set */
                    item->packet.markerBit = 1;
                }
                if (flags & (1 << 12))
                {
                    /* the extention bit is set */
                    item->packet.headerExtension = item->packet.buffer;
                    uint16_t length = ntohs(*((uint16_t*)(item->packet.headerExtension + 2)));
                    item->packet.headerExtensionSize = length * 4 + 4;
                }
                else
                {
                    item->packet.headerExtension = NULL;
                    item->packet.headerExtensionSize = 0;
                }
                item->packet.payload = item->packet.buffer + item->packet.headerExtensionSize;
                item->packet.payloadSize = fifo->msgVec[i].msg_len - sizeof(ARSTREAM2_RTP_Header_t) - item->packet.headerExtensionSize;

                ret = ARSTREAM2_RTP_FifoEnqueueItemOrdered(fifo, item);
                if (ret < 0)
                {
                    if (ret == -3)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTP_TAG, "Duplicate RTP packet received (seqNum %d, extSeqNum %d)",
                                    item->packet.seqNum, item->packet.extSeqNum); //TODO: debug
                    }
                    else
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "ARSTREAM2_RTP_FifoEnqueueItem() failed (%d)", ret);
                    }
                    /* failed to enqueue, flag the item for garbage collection */
                    garbageCount++;
                    if (!garbage)
                    {
                        garbage = item;
                    }
                    else
                    {
                        item->next = garbage;
                        garbage->prev = item;
                        garbage = item;
                    }
                }
                else if (ret == 1)
                {
                    ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTP_TAG, "Out of order RTP packet received (seqNum %d, extSeqNum %d, delta %d)",
                                item->packet.seqNum, item->packet.extSeqNum, -seqNumDelta); //TODO: debug
                    enqueueCount++;
                }
                else
                {
                    /* interarrival jitter computation */
                    int64_t d;
                    d = ((int64_t)context->previousRecvRtpTimestamp - (int64_t)context->previousExtRtpTimestamp)
                        - ((int64_t)recvRtpTimestamp - (int64_t)item->packet.extRtpTimestamp);
                    if (d < 0) d = -d;
                    rtcpContext->interarrivalJitter = (uint32_t)((int64_t)rtcpContext->interarrivalJitter
                                                      + (d - (int64_t)rtcpContext->interarrivalJitter) / 16);
                    context->previousRecvRtpTimestamp = recvRtpTimestamp;
                    context->previousExtRtpTimestamp = item->packet.extRtpTimestamp;
                    enqueueCount++;
                }
            }
            else
            {
                /* invalid payload, flag the item for garbage collection */
                garbageCount++;
                if (!garbage)
                {
                    garbage = item;
                }
                else
                {
                    item->next = garbage;
                    garbage->prev = item;
                    garbage = item;
                }
            }
        }
        else
        {
            break;
        }
    }

    while (garbage)
    {
        garbageCount2++;
        ARSTREAM2_RTP_PacketFifoItem_t* next = garbage->next;
        int pushRet = ARSTREAM2_RTP_FifoPushFreeItem(fifo, garbage);
        if (pushRet < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "ARSTREAM2_RTP_FifoPushFreeItem() failed (%d)", pushRet);
        }
        garbage = next;
    }
    if (garbageCount != garbageCount2)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Garbage count mismatch: %d vs. %d", garbageCount, garbageCount2);
    }
    if (popCount != enqueueCount + garbageCount)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "Pop count mismatch: %d vs. %d", popCount, enqueueCount + garbageCount);
    }
    //ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTP_TAG, "popCount=%d, enqueueCount=%d, garbageCount=%d", popCount, enqueueCount, garbageCount); //TODO: debug

    return ret;
}


int ARSTREAM2_RTP_Receiver_FifoFlush(ARSTREAM2_RTP_PacketFifo_t *fifo)
{
    ARSTREAM2_RTP_PacketFifoItem_t* item;
    int count = 0;

    do
    {
        item = ARSTREAM2_RTP_FifoDequeueItem(fifo);
        if (item)
        {
            int fifoErr = ARSTREAM2_RTP_FifoPushFreeItem(fifo, item);
            if (fifoErr != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_TAG, "ARSTREAM2_RTP_FifoPushFreeItem() failed (%d)", fifoErr);
            }
            count++;
        }
    }
    while (item);

    return count;
}
