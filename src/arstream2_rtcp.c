/**
 * @file arstream2_rtcp.c
 * @brief Parrot Streaming Library - RTCP implementation
 * @date 04/06/2016
 * @author aurelien.barre@parrot.com
 */

#include "arstream2_rtcp.h"

#include <netinet/in.h>
#include <libARSAL/ARSAL_Print.h>


/**
 * Tag for ARSAL_PRINT
 */
#define ARSTREAM2_RTCP_TAG "ARSTREAM2_Rtcp"


int ARSTREAM2_RTCP_IsSenderReport(const uint8_t *buffer, int bufferSize)
{
    if (!buffer)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid pointer");
        return 0;
    }

    if (bufferSize < 28)
    {
        return 0;
    }

    uint8_t version = (*buffer >> 6) & 0x3;
    if (version != 2)
    {
        return 0;
    }

    if (*(buffer + 1) != ARSTREAM2_RTCP_SENDER_REPORT_PACKET_TYPE)
    {
        return 0;
    }

    return 1;
}


int ARSTREAM2_RTCP_IsReceiverReport(const uint8_t *buffer, int bufferSize, int *receptionReportCount)
{
    if (!buffer)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid pointer");
        return 0;
    }

    if (bufferSize < 8)
    {
        return 0;
    }

    uint8_t version = (*buffer >> 6) & 0x3;
    if (version != 2)
    {
        return 0;
    }

    if (*(buffer + 1) != ARSTREAM2_RTCP_RECEIVER_REPORT_PACKET_TYPE)
    {
        return 0;
    }

    uint8_t rc = *buffer & 0x1F;
    if (receptionReportCount)
    {
        *receptionReportCount = (int)rc;
    }

    return 1;
}


int ARSTREAM2_RTCP_Sender_ProcessReceiverReport(ARSTREAM2_RTCP_ReceiverReport_t *receiverReport,
                                                ARSTREAM2_RTCP_ReceptionReportBlock_t *receptionReport,
                                                ARSTREAM2_RTCP_RtpSenderContext_t *context)
{
    return -1;
}


int ARSTREAM2_RTCP_Sender_GenerateSenderReport(ARSTREAM2_RTCP_SenderReport_t *senderReport,
                                               ARSTREAM2_RTCP_RtpSenderContext_t *context)
{
    struct timespec t1;
    ARSAL_Time_GetTime(&t1);

    if ((!senderReport) || (!context))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid pointer");
        return -1;
    }

    uint64_t ntpTimestamp = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;
    uint64_t ntpTimestampL = ((uint64_t)t1.tv_nsec << 32) / (uint64_t)1000000000;
    uint32_t rtpTimestamp = context->rtpTimestampOffset + (uint32_t)((((ntpTimestamp * context->rtpClockRate) + 500000) / 1000000) & 0xFFFFFFFF); /* microseconds to rtpClockRate */

    senderReport->flags = (2 << 6);
    senderReport->packetType = ARSTREAM2_RTCP_SENDER_REPORT_PACKET_TYPE;
    senderReport->length = htons(6);
    senderReport->ssrc = htonl(context->senderSsrc);
    senderReport->ntpTimestampH = htonl((uint32_t)t1.tv_sec);
    senderReport->ntpTimestampL = htonl((uint32_t)(ntpTimestampL & 0xFFFFFFFF));
    senderReport->rtpTimestamp = htonl(rtpTimestamp);
    senderReport->senderPacketCount = htonl(context->packetCount);
    senderReport->senderByteCount = htonl(context->byteCount);

    return 0;
}


int ARSTREAM2_RTCP_Receiver_ProcessSenderReport(const ARSTREAM2_RTCP_SenderReport_t *senderReport,
                                                ARSTREAM2_RTCP_RtpReceiverContext_t *context)
{
    uint32_t ssrc, rtpTimestamp, senderPacketCount, senderByteCount;
    uint64_t ntpTimestamp;

    if ((!senderReport) || (!context))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid pointer");
        return -1;
    }

    uint8_t version = (senderReport->flags >> 6) & 0x3;
    if (version != 2)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid sender report protocol version (%d)", version);
        return -1;
    }

    if (senderReport->packetType != ARSTREAM2_RTCP_SENDER_REPORT_PACKET_TYPE)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid sender report packet type (%d)", senderReport->packetType);
        return -1;
    }

    uint16_t length = ntohs(senderReport->length);
    if (length < 6)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid sender report length");
        return -1;
    }

    ssrc = ntohl(senderReport->ssrc);
    ntpTimestamp = ((uint64_t)(ntohl(senderReport->ntpTimestampH)) * 1000000) + (((uint64_t)(ntohl(senderReport->ntpTimestampL)) * 1000000) >> 32);
    rtpTimestamp = ntohl(senderReport->rtpTimestamp);
    senderPacketCount = ntohl(senderReport->senderPacketCount);
    senderByteCount = ntohl(senderReport->senderByteCount);

    if (ssrc != context->senderSsrc)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Unexpected sender SSRC");
        return -1;
    }

    if (ntpTimestamp <= context->prevSrNtpTimestamp)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Out of order or duplicate sender report");
        return -1;
    }

    // NTP to RTP linear regression: RTP = a * NTP + b
    context->tsAnum = (int64_t)(rtpTimestamp - context->prevSrRtpTimestamp);
    context->tsAden = (int64_t)(ntpTimestamp - context->prevSrNtpTimestamp);
    context->tsB = (int64_t)rtpTimestamp - (int64_t)((context->tsAnum * ntpTimestamp + context->tsAden / 2) / context->tsAden);

    // Packet and byte rates
    context->lastSrInterval = (uint32_t)(ntpTimestamp - context->prevSrNtpTimestamp);
    if (context->lastSrInterval > 0)
    {
        context->srIntervalPacketCount = senderPacketCount - context->prevSrPacketCount;
        context->srIntervalByteCount = senderByteCount - context->prevSrByteCount;
    }
    else
    {
        context->srIntervalPacketCount = context->srIntervalByteCount = 0;
    }

    // Update values
    context->prevSrRtpTimestamp = rtpTimestamp;
    context->prevSrNtpTimestamp = ntpTimestamp;
    context->prevSrPacketCount = senderPacketCount;
    context->prevSrByteCount = senderByteCount;

    return 0;
}


int ARSTREAM2_RTCP_Receiver_GenerateReceiverReport(ARSTREAM2_RTCP_ReceiverReport_t *receiverReport,
                                                   ARSTREAM2_RTCP_ReceptionReportBlock_t *receptionReport,
                                                   ARSTREAM2_RTCP_RtpReceiverContext_t *context)
{
    struct timespec t1;
    ARSAL_Time_GetTime(&t1);
    uint64_t curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

    if ((!receiverReport) || (!receptionReport) || (!context))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid pointer");
        return -1;
    }

    int rrCount = ((context->packetsReceived > 0) && (context->packetsReceived > context->lastRrPacketsReceived)) ? 1 : 0;

    receiverReport->flags = (2 << 6) | (rrCount & 0x1F);
    receiverReport->packetType = ARSTREAM2_RTCP_RECEIVER_REPORT_PACKET_TYPE;
    receiverReport->length = htons(1 + 6 * rrCount);
    receiverReport->ssrc = htonl(context->receiverSsrc);

    if (rrCount == 1)
    {
        uint32_t cumulativeLost = context->extHighestSeqNum - context->firstSeqNum + 1 - context->packetsReceived;
        if (cumulativeLost != context->packetsLost)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Receiver report mismatch: cumulativeLost=%d - context->packetsLost=%d", cumulativeLost, context->packetsLost);
        }
        uint32_t fractionLost = 0;
        if ((context->lastRrExtHighestSeqNum != 0) && (context->extHighestSeqNum > context->lastRrExtHighestSeqNum))
        {
            fractionLost = (context->packetsLost - context->lastRrPacketsLost) * 256 / (context->extHighestSeqNum - context->lastRrExtHighestSeqNum);
            if (fractionLost > 256) fractionLost = 0;
        }
        receptionReport->ssrc = htonl(context->senderSsrc);
        receptionReport->lost = htonl(((fractionLost & 0xFF) << 24) | (cumulativeLost & 0x00FFFFFF));
        receptionReport->extHighestSeqNum = htonl(context->extHighestSeqNum);
        receptionReport->interarrivalJitter = htonl(0); //TODO
        receptionReport->lsr = htonl((uint32_t)(((context->prevSrNtpTimestamp << 16) / 1000000) & 0xFFFFFFFF));
        receptionReport->dlsr = htonl(((curTime - context->lastSrReceptionTimestamp) << 16) / 1000000);

        context->lastRrExtHighestSeqNum = context->extHighestSeqNum;
        context->lastRrPacketsReceived = context->packetsReceived;
        context->lastRrPacketsLost = context->packetsLost;
        context->lastRrTimestamp = curTime;
    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Unsupported receiver report count");
        return -1;
    }

    return 0;
}
