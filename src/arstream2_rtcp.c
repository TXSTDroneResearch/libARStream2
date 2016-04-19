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


static const char *ARSTREAM2_RTCP_SdesItemName[8] =
{
    "CNAME",
    "NAME",
    "EMAIL",
    "PHONE",
    "LOC",
    "TOOL",
    "NOTE",
    "PRIV",
};


int ARSTREAM2_RTCP_GetPacketType(const uint8_t *buffer, int bufferSize, int *receptionReportCount, int *size)
{
    if (!buffer)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid pointer");
        return -1;
    }

    if (bufferSize < 8)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid buffer size (%d)", bufferSize);
        return -1;
    }

    uint8_t version = (*buffer >> 6) & 0x3;
    if (version != 2)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid RTCP protocol version (%d)", version);
        return -1;
    }

    uint8_t type = *(buffer + 1);
    if ((type == ARSTREAM2_RTCP_SENDER_REPORT_PACKET_TYPE) || (type == ARSTREAM2_RTCP_RECEIVER_REPORT_PACKET_TYPE))
    {
        uint8_t rc = *buffer & 0x1F;
        if (receptionReportCount)
        {
            *receptionReportCount = (int)rc;
        }
    }

    uint16_t length = ntohs(*((uint16_t*)(buffer + 2)));
    if (size)
    {
        *size = (length + 1) * 4;
    }

    return type;
}


int ARSTREAM2_RTCP_Sender_ProcessReceiverReport(ARSTREAM2_RTCP_ReceiverReport_t *receiverReport,
                                                ARSTREAM2_RTCP_ReceptionReportBlock_t *receptionReport,
                                                uint64_t receptionTimestamp,
                                                ARSTREAM2_RTCP_RtpSenderContext_t *context)
{
    uint32_t ssrc, ssrc_1;
    uint32_t lost, extHighestSeqNum, interarrivalJitter;
    uint32_t lsr, dlsr;
    uint64_t lsr_us, dlsr_us;

    if ((!receiverReport) || (!receptionReport) || (!context))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid pointer");
        return -1;
    }

    uint8_t version = (receiverReport->flags >> 6) & 0x3;
    if (version != 2)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid receiver report protocol version (%d)", version);
        return -1;
    }

    if (receiverReport->packetType != ARSTREAM2_RTCP_RECEIVER_REPORT_PACKET_TYPE)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid receiver report packet type (%d)", receiverReport->packetType);
        return -1;
    }

    uint16_t length = ntohs(receiverReport->length);
    if (length < 7)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid receiver report length");
        return -1;
    }

    ssrc = ntohl(receiverReport->ssrc);
    ssrc_1 = ntohl(receptionReport->ssrc);
    lost = ntohl(receptionReport->lost);
    extHighestSeqNum = ntohl(receptionReport->extHighestSeqNum);
    interarrivalJitter = ntohl(receptionReport->interarrivalJitter);
    lsr = ntohl(receptionReport->lsr);
    dlsr = ntohl(receptionReport->dlsr);

    if (context->receiverSsrc == 0)
    {
        context->receiverSsrc = ssrc;
    }

    if (ssrc != context->receiverSsrc)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Unexpected receiver SSRC");
        return -1;
    }

    if (ssrc_1 != context->senderSsrc)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Unexpected sender SSRC");
        return -1;
    }

    context->receiverFractionLost = ((lost >> 24) & 0xFF);
    context->receiverLostCount = (lost & 0x00FFFFFF);
    context->receiverExtHighestSeqNum = extHighestSeqNum;
    context->interarrivalJitter = (uint32_t)(((uint64_t)interarrivalJitter * 1000000 + context->rtpClockRate / 2) / context->rtpClockRate);
    if ((lsr > 0) || (dlsr > 0))
    {
        lsr_us = ((uint64_t)lsr * 1000000) >> 16;
        //TODO: handle the LSR timestamp loopback after 18h
        dlsr_us = ((uint64_t)dlsr * 1000000) >> 16;
        context->roundTripDelay = (uint32_t)(receptionTimestamp - lsr_us - dlsr_us);
    }
    else
    {
        lsr_us = dlsr_us = 0;
        context->roundTripDelay = 0;
    }
    context->lastRrReceptionTimestamp = receptionTimestamp;

    return 0;
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

    context->lastSrTimestamp = ntpTimestamp;

    return 0;
}


int ARSTREAM2_RTCP_Receiver_ProcessSenderReport(const ARSTREAM2_RTCP_SenderReport_t *senderReport,
                                                uint64_t receptionTimestamp,
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

    if (!context->prevSrNtpTimestamp)
    {
        context->prevSrNtpTimestamp = ntpTimestamp;
    }
    else if (ntpTimestamp <= context->prevSrNtpTimestamp)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Out of order or duplicate sender report");
        return -1;
    }
    if (!context->prevSrRtpTimestamp)
    {
        context->prevSrRtpTimestamp = rtpTimestamp;
    }

    // NTP to RTP linear regression: RTP = a * NTP + b
    context->tsAnum = (int64_t)(rtpTimestamp - context->prevSrRtpTimestamp);
    context->tsAden = (int64_t)(ntpTimestamp - context->prevSrNtpTimestamp);
    context->tsB = (context->tsAden) ? (int64_t)rtpTimestamp - (int64_t)((context->tsAnum * ntpTimestamp + context->tsAden / 2) / context->tsAden) : 0;

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

    context->lastSrReceptionTimestamp = receptionTimestamp;

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
        receptionReport->interarrivalJitter = htonl(context->interarrivalJitter);
        receptionReport->lsr = htonl((uint32_t)(((context->prevSrNtpTimestamp << 16) / 1000000) & 0xFFFFFFFF));
        receptionReport->dlsr = htonl(((curTime - context->lastSrReceptionTimestamp) << 16) / 1000000);

        context->lastRrExtHighestSeqNum = context->extHighestSeqNum;
        context->lastRrPacketsReceived = context->packetsReceived;
        context->lastRrPacketsLost = context->packetsLost;
        context->lastRrTimestamp = curTime;
    }
    else if (rrCount > 1)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Unsupported receiver report count");
        return -1;
    }

    return 0;
}


int ARSTREAM2_RTCP_GenerateSourceDescription(ARSTREAM2_RTCP_Sdes_t *sdes, int maxSize, uint32_t ssrc, const char *cname, int *size)
{
    if ((!sdes) || (!cname))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid pointer");
        return -1;
    }
    int _size = (sizeof(ARSTREAM2_RTCP_Sdes_t) + 4 + 2 + strlen(cname) + 1 + 3) & ~0x3;
    if (maxSize < _size)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Buffer is too small for SDES");
        return -1;
    }

    uint32_t *ssrc_1 = (uint32_t*)((uint8_t*)sdes + sizeof(ARSTREAM2_RTCP_Sdes_t));
    uint8_t *cnameItem = (uint8_t*)sdes + sizeof(ARSTREAM2_RTCP_Sdes_t) + 4;

    int sourceCount = 1;
    sdes->flags = (2 << 6) | (sourceCount & 0x1F);
    sdes->packetType = ARSTREAM2_RTCP_SDES_PACKET_TYPE;
    sdes->length = htons((4 + 2 + strlen(cname) + 1 + 3) / 4 * sourceCount);
    *ssrc_1 = htonl(ssrc);
    *cnameItem = ARSTREAM2_RTCP_SDES_CNAME_ITEM;
    *(cnameItem + 1) = strlen(cname);
    memcpy(cnameItem + 2, cname, strlen(cname));
    memset(cnameItem + 2 + strlen(cname), 0, _size - (sizeof(ARSTREAM2_RTCP_Sdes_t) + 4 + 2 + strlen(cname)));

    if (size)
        *size = _size;

    return 0;
}


int ARSTREAM2_RTCP_ProcessSourceDescription(ARSTREAM2_RTCP_Sdes_t *sdes)
{
    uint32_t ssrc;
    uint8_t *ptr = (uint8_t*)sdes + 4;

    if (!sdes)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid pointer");
        return -1;
    }

    uint8_t version = (sdes->flags >> 6) & 0x3;
    if (version != 2)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid source description protocol version (%d)", version);
        return -1;
    }

    if (sdes->packetType != ARSTREAM2_RTCP_SDES_PACKET_TYPE)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid source description packet type (%d)", sdes->packetType);
        return -1;
    }

    uint8_t sc = sdes->flags & 0x1F;
    uint16_t length = ntohs(sdes->length);
    int remLength = length * 4, i;

    if (length < sc)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid length (%d) for %d source count", length, sc);
        return -1;
    }

    for (i = 0; i < sc; i++)
    {
        // read the SSRC
        ssrc = ntohl(*((uint32_t*)ptr));
        ptr += 4;
        remLength -= 4;
        // read the SDES items
        while ((*ptr != 0) && (remLength >= 3))
        {
            uint8_t id = *ptr;
            uint8_t len = *(ptr + 1);
            ptr += 2;
            remLength -= 2;
            char str[256];
            memcpy(str, ptr, len);
            str[len] = '\0';
            if (id <= 8)
                ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTCP_TAG, "SDES SSRC=0x%08X %s=%s", ssrc, ARSTREAM2_RTCP_SdesItemName[id - 1], str);
            ptr += len;
            remLength -= len;
        }
        // align to multiple of 4 bytes
        if ((*ptr == 0) && (remLength))
        {
            int align = ((remLength + 3) & ~3) - remLength;
            remLength -= align;
            ptr += align;
        }
    }

    return 0;
}


int ARSTREAM2_RTCP_GenerateApplicationClockDelta(ARSTREAM2_RTCP_Application_t *app, ARSTREAM2_RTCP_ClockDelta_t *clockDelta,
                                                 uint64_t sendTimestamp, uint32_t ssrc,
                                                 ARSTREAM2_RTCP_ClockDeltaContext_t *context)
{
    if ((!app) || (!clockDelta))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid pointer");
        return -1;
    }

    app->flags = (2 << 6) | (ARSTREAM2_RTCP_APP_PACKET_CLOCKDELTA_SUBTYPE & 0x1F);
    app->packetType = ARSTREAM2_RTCP_APP_PACKET_TYPE;
    app->length = htons((sizeof(ARSTREAM2_RTCP_Application_t) + sizeof(ARSTREAM2_RTCP_ClockDelta_t)) / 4 - 1);
    app->ssrc = htonl(ssrc);
    app->name = htonl(ARSTREAM2_RTCP_APP_PACKET_NAME);

    clockDelta->originateTimestampH = htonl((uint32_t)(context->nextPeerOriginateTimestamp >> 32));
    clockDelta->originateTimestampL = htonl((uint32_t)(context->nextPeerOriginateTimestamp & 0xFFFFFFFF));
    clockDelta->receiveTimestampH = htonl((uint32_t)(context->nextReceiveTimestamp >> 32));
    clockDelta->receiveTimestampL = htonl((uint32_t)(context->nextReceiveTimestamp & 0xFFFFFFFF));
    clockDelta->transmitTimestampH = htonl((uint32_t)(sendTimestamp >> 32));
    clockDelta->transmitTimestampL = htonl((uint32_t)(sendTimestamp & 0xFFFFFFFF));

    return 0;
}


int ARSTREAM2_RTCP_ProcessApplicationClockDelta(ARSTREAM2_RTCP_Application_t *app, ARSTREAM2_RTCP_ClockDelta_t *clockDelta,
                                                uint64_t receptionTimestamp, uint32_t peerSsrc,
                                                ARSTREAM2_RTCP_ClockDeltaContext_t *context)
{
    if ((!app) || (!clockDelta) || (!context))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid pointer");
        return -1;
    }

    uint8_t version = (app->flags >> 6) & 0x3;
    if (version != 2)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid application packet protocol version (%d)", version);
        return -1;
    }

    if (app->packetType != ARSTREAM2_RTCP_APP_PACKET_TYPE)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid application packet type (%d)", app->packetType);
        return -1;
    }

    uint32_t name = ntohl(app->name);
    if (name != ARSTREAM2_RTCP_APP_PACKET_NAME)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid application packet name (0x%08X)", name);
        return -1;
    }

    uint8_t subType = (app->flags & 0x1F);
    if (subType != ARSTREAM2_RTCP_APP_PACKET_CLOCKDELTA_SUBTYPE)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid application packet subtype (%d)", subType);
        return -1;
    }

    uint32_t ssrc = ntohl(app->ssrc);
    if (ssrc != peerSsrc)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Unexpected peer SSRC");
        return -1;
    }

    uint16_t length = ntohs(app->length);
    if (length != (sizeof(ARSTREAM2_RTCP_Application_t) + sizeof(ARSTREAM2_RTCP_ClockDelta_t)) / 4 - 1)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid application packet length (%d)", length);
        return -1;
    }

    uint32_t originateTimestampH = ntohl(clockDelta->originateTimestampH);
    uint32_t originateTimestampL = ntohl(clockDelta->originateTimestampL);
    uint64_t originateTimestamp = ((uint64_t)originateTimestampH << 32) + ((uint64_t)originateTimestampL & 0xFFFFFFFF);
    uint32_t receiveTimestampH = ntohl(clockDelta->receiveTimestampH);
    uint32_t receiveTimestampL = ntohl(clockDelta->receiveTimestampL);
    uint64_t peerReceiveTimestamp = ((uint64_t)receiveTimestampH << 32) + ((uint64_t)receiveTimestampL & 0xFFFFFFFF);
    uint32_t transmitTimestampH = ntohl(clockDelta->transmitTimestampH);
    uint32_t transmitTimestampL = ntohl(clockDelta->transmitTimestampL);
    uint64_t peerTransmitTimestamp = ((uint64_t)transmitTimestampH << 32) + ((uint64_t)transmitTimestampL & 0xFFFFFFFF);

    if ((originateTimestamp != 0) && (peerReceiveTimestamp != 0) && (peerTransmitTimestamp != 0))
    {
        context->clockDelta = (int64_t)(peerReceiveTimestamp + peerTransmitTimestamp) / 2 - (int64_t)(originateTimestamp + receptionTimestamp) / 2;
        context->rtDelay = (receptionTimestamp - originateTimestamp) - (peerTransmitTimestamp - peerReceiveTimestamp);
        if (context->clockDeltaAvg == 0)
        {
            context->clockDeltaAvg = context->clockDelta;
        }
        else
        {
            // sliding average, alpha = 1/32
            context->clockDeltaAvg = context->clockDeltaAvg + (context->clockDelta - context->clockDeltaAvg) / 32;
        }
    }

    context->nextPeerOriginateTimestamp = peerTransmitTimestamp;
    context->nextReceiveTimestamp = receptionTimestamp;

    return 0;
}
