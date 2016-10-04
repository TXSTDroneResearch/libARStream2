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


int ARSTREAM2_RTCP_GetPacketType(const uint8_t *buffer, unsigned int bufferSize, int *receptionReportCount, unsigned int *size)
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
        if (bufferSize != 24) /* workaround to avoid logging when it's an old clockSync packet with old FF or SC versions */
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid RTCP protocol version (%d)", version);
        }
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
        *size = ((unsigned int)length + 1) * 4;
    }

    return type;
}


int ARSTREAM2_RTCP_Sender_ProcessReceiverReport(const ARSTREAM2_RTCP_ReceiverReport_t *receiverReport,
                                                const ARSTREAM2_RTCP_ReceptionReportBlock_t *receptionReport,
                                                uint64_t receptionTimestamp,
                                                ARSTREAM2_RTCP_SenderContext_t *context,
                                                int *gotReceptionReport)
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

    unsigned int rrCount = receiverReport->flags & 0x1F;
    if (rrCount != 1)
    {
        if (rrCount > 1)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Unsupported reception report count (%d)", rrCount);
            return -1;
        }
        else
        {
            /* No reception report available in the receiver report */
            if (gotReceptionReport) *gotReceptionReport = 0;
            return 0;
        }
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
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTCP_TAG, "Unexpected receiver SSRC");
        return -1;
    }

    if (ssrc_1 != context->senderSsrc)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTCP_TAG, "Unexpected sender SSRC");
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

    if (gotReceptionReport) *gotReceptionReport = 1;
    return 0;
}


int ARSTREAM2_RTCP_Sender_GenerateSenderReport(ARSTREAM2_RTCP_SenderReport_t *senderReport,
                                               uint64_t sendTimestamp, uint32_t packetCount, uint32_t byteCount,
                                               ARSTREAM2_RTCP_SenderContext_t *context)
{
    if ((!senderReport) || (!context))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid pointer");
        return -1;
    }

    uint64_t ntpTimestampL = (sendTimestamp << 32) / (uint64_t)1000000;
    uint32_t rtpTimestamp = context->rtpTimestampOffset + (uint32_t)((((sendTimestamp * context->rtpClockRate) + 500000) / 1000000) & 0xFFFFFFFF); /* microseconds to rtpClockRate */

    senderReport->flags = (2 << 6);
    senderReport->packetType = ARSTREAM2_RTCP_SENDER_REPORT_PACKET_TYPE;
    senderReport->length = htons(6);
    senderReport->ssrc = htonl(context->senderSsrc);
    senderReport->ntpTimestampH = htonl((uint32_t)(sendTimestamp / 1000000));
    senderReport->ntpTimestampL = htonl((uint32_t)(ntpTimestampL & 0xFFFFFFFF));
    senderReport->rtpTimestamp = htonl(rtpTimestamp);
    senderReport->senderPacketCount = htonl(packetCount);
    senderReport->senderByteCount = htonl(byteCount);

    // Packet and byte rates
    if (context->lastSrTimestamp)
    {
        context->lastSrInterval = (uint32_t)(sendTimestamp - context->lastSrTimestamp);
        if (context->lastSrInterval > 0)
        {
            context->srIntervalPacketCount = packetCount - context->prevSrPacketCount;
            context->srIntervalByteCount = byteCount - context->prevSrByteCount;
        }
        else
        {
            context->srIntervalPacketCount = context->srIntervalByteCount = 0;
        }

        // Update values
        context->prevSrPacketCount = packetCount;
        context->prevSrByteCount = byteCount;
    }

    context->lastSrTimestamp = sendTimestamp;

    return 0;
}


int ARSTREAM2_RTCP_Receiver_ProcessSenderReport(const ARSTREAM2_RTCP_SenderReport_t *senderReport,
                                                uint64_t receptionTimestamp,
                                                ARSTREAM2_RTCP_ReceiverContext_t *context)
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
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTCP_TAG, "Unexpected sender SSRC");
        return -1;
    }

    if (!context->prevSrNtpTimestamp)
    {
        context->prevSrNtpTimestamp = ntpTimestamp;
    }
    else if (ntpTimestamp <= context->prevSrNtpTimestamp)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTCP_TAG, "Out of order or duplicate sender report (%llu vs. %llu)", ntpTimestamp, context->prevSrNtpTimestamp);
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
                                                   uint64_t sendTimestamp,
                                                   ARSTREAM2_RTCP_ReceiverContext_t *context,
                                                   unsigned int *size)
{
    if ((!receiverReport) || (!receptionReport) || (!context))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid pointer");
        return -1;
    }

    if (context->lastSrReceptionTimestamp == 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "No sender report received");
        return -1;
    }

    int rrCount = ((context->packetsReceived > 0) && (context->packetsReceived > context->lastRrPacketsReceived)) ? 1 : 0;
    unsigned int _size = sizeof(ARSTREAM2_RTCP_ReceiverReport_t) + rrCount * sizeof(ARSTREAM2_RTCP_ReceptionReportBlock_t);

    receiverReport->flags = (2 << 6) | (rrCount & 0x1F);
    receiverReport->packetType = ARSTREAM2_RTCP_RECEIVER_REPORT_PACKET_TYPE;
    receiverReport->length = htons(1 + 6 * rrCount);
    receiverReport->ssrc = htonl(context->receiverSsrc);

    if (rrCount == 1)
    {
        //uint32_t cumulativeLost = context->extHighestSeqNum - context->firstSeqNum + 1 - context->packetsReceived;
        // NB: cumulativeLost is unreliable because (context->extHighestSeqNum - context->firstSeqNum) is before jitter buffer and context->packetsReceived is after
        uint32_t fractionLost = 0;
        if ((context->lastRrExtHighestSeqNum != 0) && (context->extHighestSeqNum > context->lastRrExtHighestSeqNum))
        {
            fractionLost = (context->packetsLost - context->lastRrPacketsLost) * 256 / (context->extHighestSeqNum - context->lastRrExtHighestSeqNum);
            if (fractionLost > 256) fractionLost = 0;
        }
        receptionReport->ssrc = htonl(context->senderSsrc);
        receptionReport->lost = htonl(((fractionLost & 0xFF) << 24) | (context->packetsLost & 0x00FFFFFF));
        receptionReport->extHighestSeqNum = htonl(context->extHighestSeqNum);
        receptionReport->interarrivalJitter = htonl(context->interarrivalJitter);
        receptionReport->lsr = htonl((uint32_t)(((context->prevSrNtpTimestamp << 16) / 1000000) & 0xFFFFFFFF));
        receptionReport->dlsr = htonl(((sendTimestamp - context->lastSrReceptionTimestamp) << 16) / 1000000);

        context->lastRrExtHighestSeqNum = context->extHighestSeqNum;
        context->lastRrPacketsReceived = context->packetsReceived;
        context->lastRrPacketsLost = context->packetsLost;
        context->lastRrTimestamp = sendTimestamp;
    }
    else if (rrCount > 1)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTCP_TAG, "Unsupported receiver report count");
        return -1;
    }

    if (size)
        *size = _size;

    return 0;
}


int ARSTREAM2_RTCP_GenerateSourceDescription(ARSTREAM2_RTCP_Sdes_t *sdes, unsigned int maxSize, uint32_t ssrc,
                                             const char *cname, const char *name, unsigned int *size)
{
    if ((!sdes) || (!cname))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid pointer");
        return -1;
    }
    unsigned int cname_len = strlen(cname);
    unsigned int name_len = (name && strlen(name)) ? strlen(name) : 0;
    unsigned int _size = (sizeof(ARSTREAM2_RTCP_Sdes_t) + 4 + 2 + cname_len + ((name_len > 0) ? 2 + name_len : 0) + 1 + 3) & ~0x3;
    if (maxSize < _size)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Buffer is too small for SDES");
        return -1;
    }
    memset(sdes, 0, _size);

    uint32_t *ssrc_1 = (uint32_t*)((uint8_t*)sdes + sizeof(ARSTREAM2_RTCP_Sdes_t));
    uint8_t *cnameItem = (uint8_t*)sdes + sizeof(ARSTREAM2_RTCP_Sdes_t) + 4;
    uint8_t *nameItem = (uint8_t*)sdes + sizeof(ARSTREAM2_RTCP_Sdes_t) + 4 + 2 + cname_len;

    int sourceCount = 1;
    sdes->flags = (2 << 6) | (sourceCount & 0x1F);
    sdes->packetType = ARSTREAM2_RTCP_SDES_PACKET_TYPE;
    sdes->length = htons((4 + 2 + cname_len + ((name_len > 0) ? 2 + name_len : 0) + 1 + 3) / 4 * sourceCount);
    *ssrc_1 = htonl(ssrc);

    *cnameItem = ARSTREAM2_RTCP_SDES_CNAME_ITEM;
    *(cnameItem + 1) = strlen(cname);
    memcpy(cnameItem + 2, cname, strlen(cname));

    if (name_len > 0)
    {
        *nameItem = ARSTREAM2_RTCP_SDES_NAME_ITEM;
        *(nameItem + 1) = strlen(name);
        memcpy(nameItem + 2, name, strlen(name));
    }

    if (size)
        *size = _size;

    return 0;
}


int ARSTREAM2_RTCP_ProcessSourceDescription(const ARSTREAM2_RTCP_Sdes_t *sdes)
{

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

    if (length < sc)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid length (%d) for %d source count", length, sc);
        return -1;
    }

#if 0 // uncomment to log the SDES values received
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

    const uint8_t *ptr = (uint8_t*)sdes + 4;
    uint32_t ssrc;
    int remLength = length * 4, i;
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
#endif

    return 0;
}


int ARSTREAM2_RTCP_GetApplicationPacketSubtype(const ARSTREAM2_RTCP_Application_t *app)
{
    if (!app)
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

    return subType;
}


int ARSTREAM2_RTCP_GenerateApplicationClockDelta(ARSTREAM2_RTCP_Application_t *app, ARSTREAM2_RTCP_ClockDelta_t *clockDelta,
                                                 uint64_t sendTimestamp, uint32_t ssrc,
                                                 ARSTREAM2_RTCP_ClockDeltaContext_t *context)
{
    if ((!app) || (!clockDelta) || (!context))
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


int ARSTREAM2_RTCP_ProcessApplicationClockDelta(const ARSTREAM2_RTCP_Application_t *app,
                                                const ARSTREAM2_RTCP_ClockDelta_t *clockDelta,
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
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTCP_TAG, "Unexpected peer SSRC");
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


int ARSTREAM2_RTCP_GenerateApplicationVideoStats(ARSTREAM2_RTCP_Application_t *app, ARSTREAM2_RTCP_VideoStats_t *videoStats,
                                                 uint64_t sendTimestamp, uint32_t ssrc,
                                                 ARSTREAM2_RTCP_VideoStatsContext_t *context)
{
    int i, j;

    if ((!app) || (!videoStats) || (!context))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid pointer");
        return -1;
    }

    app->flags = (2 << 6) | (ARSTREAM2_RTCP_APP_PACKET_VIDEOSTATS_SUBTYPE & 0x1F);
    app->packetType = ARSTREAM2_RTCP_APP_PACKET_TYPE;
    app->length = htons((sizeof(ARSTREAM2_RTCP_Application_t) + sizeof(ARSTREAM2_RTCP_VideoStats_t)) / 4 - 1);
    app->ssrc = htonl(ssrc);
    app->name = htonl(ARSTREAM2_RTCP_APP_PACKET_NAME);

    videoStats->timestampH = htonl((uint32_t)(context->timestamp >> 32));
    videoStats->timestampL = htonl((uint32_t)(context->timestamp & 0xFFFFFFFF));
    videoStats->totalFrameCount = htonl(context->totalFrameCount);
    videoStats->outputFrameCount = htonl(context->outputFrameCount);
    videoStats->erroredOutputFrameCount = htonl(context->erroredOutputFrameCount);
    videoStats->missedFrameCount = htonl(context->missedFrameCount);
    videoStats->discardedFrameCount = htonl(context->discardedFrameCount);
    videoStats->erroredSecondCount = htonl(context->erroredSecondCount);
    for (i = 0; i < ARSTREAM2_RTCP_VIDEOSTATS_MB_STATUS_ZONE_COUNT; i++)
    {
        videoStats->erroredSecondCountByZone[i] = htonl(context->erroredSecondCountByZone[i]);
    }
    for (j = 0; j < ARSTREAM2_RTCP_VIDEOSTATS_MB_STATUS_CLASS_COUNT; j++)
    {
        for (i = 0; i < ARSTREAM2_RTCP_VIDEOSTATS_MB_STATUS_ZONE_COUNT; i++)
        {
            videoStats->macroblockStatus[j][i] = htonl(context->macroblockStatus[j][i]);
        }
    }
    videoStats->timestampDeltaIntegralH = htonl((uint32_t)(context->timestampDeltaIntegral >> 32));
    videoStats->timestampDeltaIntegralL = htonl((uint32_t)(context->timestampDeltaIntegral & 0xFFFFFFFF));
    videoStats->timestampDeltaIntegralSqH = htonl((uint32_t)(context->timestampDeltaIntegralSq >> 32));
    videoStats->timestampDeltaIntegralSqL = htonl((uint32_t)(context->timestampDeltaIntegralSq & 0xFFFFFFFF));
    videoStats->timingErrorIntegralH = htonl((uint32_t)(context->timingErrorIntegral >> 32));
    videoStats->timingErrorIntegralL = htonl((uint32_t)(context->timingErrorIntegral & 0xFFFFFFFF));
    videoStats->timingErrorIntegralSqH = htonl((uint32_t)(context->timingErrorIntegralSq >> 32));
    videoStats->timingErrorIntegralSqL = htonl((uint32_t)(context->timingErrorIntegralSq & 0xFFFFFFFF));
    videoStats->estimatedLatencyIntegralH = htonl((uint32_t)(context->estimatedLatencyIntegral >> 32));
    videoStats->estimatedLatencyIntegralL = htonl((uint32_t)(context->estimatedLatencyIntegral & 0xFFFFFFFF));
    videoStats->estimatedLatencyIntegralSqH = htonl((uint32_t)(context->estimatedLatencyIntegralSq >> 32));
    videoStats->estimatedLatencyIntegralSqL = htonl((uint32_t)(context->estimatedLatencyIntegralSq & 0xFFFFFFFF));
    videoStats->rssi = context->rssi;
    videoStats->reserved1 = videoStats->reserved2 = videoStats->reserved3 = 0;

    return 0;
}


int ARSTREAM2_RTCP_ProcessApplicationVideoStats(const ARSTREAM2_RTCP_Application_t *app,
                                                const ARSTREAM2_RTCP_VideoStats_t *videoStats,
                                                uint64_t receptionTimestamp, uint32_t peerSsrc,
                                                ARSTREAM2_RTCP_VideoStatsContext_t *context)
{
    int i, j;

    if ((!app) || (!videoStats) || (!context))
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
    if (subType != ARSTREAM2_RTCP_APP_PACKET_VIDEOSTATS_SUBTYPE)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid application packet subtype (%d)", subType);
        return -1;
    }

    uint32_t ssrc = ntohl(app->ssrc);
    if (ssrc != peerSsrc)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTCP_TAG, "Unexpected peer SSRC");
        return -1;
    }

    uint16_t length = ntohs(app->length);
    if (length != (sizeof(ARSTREAM2_RTCP_Application_t) + sizeof(ARSTREAM2_RTCP_VideoStats_t)) / 4 - 1)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid application packet length (%d)", length);
        return -1;
    }

    context->timestamp = ((uint64_t)ntohl(videoStats->timestampH) << 32) + (uint64_t)ntohl(videoStats->timestampL);
    context->totalFrameCount = ntohl(videoStats->totalFrameCount);
    context->outputFrameCount = ntohl(videoStats->outputFrameCount);
    context->erroredOutputFrameCount = ntohl(videoStats->erroredOutputFrameCount);
    context->missedFrameCount = ntohl(videoStats->missedFrameCount);
    context->discardedFrameCount = ntohl(videoStats->discardedFrameCount);
    context->erroredSecondCount = ntohl(videoStats->erroredSecondCount);
    for (i = 0; i < ARSTREAM2_RTCP_VIDEOSTATS_MB_STATUS_ZONE_COUNT; i++)
    {
        context->erroredSecondCountByZone[i] = ntohl(videoStats->erroredSecondCountByZone[i]);
    }
    for (j = 0; j < ARSTREAM2_RTCP_VIDEOSTATS_MB_STATUS_CLASS_COUNT; j++)
    {
        for (i = 0; i < ARSTREAM2_RTCP_VIDEOSTATS_MB_STATUS_ZONE_COUNT; i++)
        {
            context->macroblockStatus[j][i] = ntohl(videoStats->macroblockStatus[j][i]);
        }
    }
    context->timestampDeltaIntegral = ((uint64_t)ntohl(videoStats->timestampDeltaIntegralH) << 32) + (uint64_t)ntohl(videoStats->timestampDeltaIntegralL);
    context->timestampDeltaIntegralSq = ((uint64_t)ntohl(videoStats->timestampDeltaIntegralSqH) << 32) + (uint64_t)ntohl(videoStats->timestampDeltaIntegralSqL);
    context->timingErrorIntegral = ((uint64_t)ntohl(videoStats->timingErrorIntegralH) << 32) + (uint64_t)ntohl(videoStats->timingErrorIntegralL);
    context->timingErrorIntegralSq = ((uint64_t)ntohl(videoStats->timingErrorIntegralSqH) << 32) + (uint64_t)ntohl(videoStats->timingErrorIntegralSqL);
    context->estimatedLatencyIntegral = ((uint64_t)ntohl(videoStats->estimatedLatencyIntegralH) << 32) + (uint64_t)ntohl(videoStats->estimatedLatencyIntegralL);
    context->estimatedLatencyIntegralSq = ((uint64_t)ntohl(videoStats->estimatedLatencyIntegralSqH) << 32) + (uint64_t)ntohl(videoStats->estimatedLatencyIntegralSqL);
    context->rssi = videoStats->rssi;

    context->lastReceivedTime = receptionTimestamp;
    context->updatedSinceLastTime = 1;

    return 0;
}


int ARSTREAM2_RTCP_Sender_GenerateCompoundPacket(uint8_t *packet, unsigned int maxPacketSize,
                                                 uint64_t sendTimestamp, int generateSenderReport,
                                                 int generateSourceDescription, int generateApplicationClockDelta,
                                                 uint32_t packetCount, uint32_t byteCount,
                                                 ARSTREAM2_RTCP_SenderContext_t *context,
                                                 unsigned int *size)
{
    int ret = 0;
    unsigned int totalSize = 0;

    if ((!packet) || (!context))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid pointer");
        return -1;
    }

    if (maxPacketSize == 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid max packet size");
        return -1;
    }

    if ((ret == 0) && (generateSenderReport) && (totalSize + sizeof(ARSTREAM2_RTCP_SenderReport_t) <= maxPacketSize))
    {
        ret = ARSTREAM2_RTCP_Sender_GenerateSenderReport((ARSTREAM2_RTCP_SenderReport_t*)(packet + totalSize),
                                                         sendTimestamp, packetCount, byteCount, context);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Failed to generate sender report (%d)", ret);
        }
        else
        {
            totalSize += sizeof(ARSTREAM2_RTCP_SenderReport_t);
        }
    }

    if ((ret == 0) && (generateSourceDescription) && (context->cname))
    {
        unsigned int sdesSize = 0;
        ret = ARSTREAM2_RTCP_GenerateSourceDescription((ARSTREAM2_RTCP_Sdes_t*)(packet + totalSize), maxPacketSize - totalSize,
                                                       context->senderSsrc, context->cname, context->name, &sdesSize);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Failed to generate source description (%d)", ret);
        }
        else
        {
            totalSize += sdesSize;
        }
    }

    if ((ret == 0) && (generateApplicationClockDelta) && (totalSize + sizeof(ARSTREAM2_RTCP_Application_t) + sizeof(ARSTREAM2_RTCP_ClockDelta_t) <= maxPacketSize))
    {
        ret = ARSTREAM2_RTCP_GenerateApplicationClockDelta((ARSTREAM2_RTCP_Application_t*)(packet + totalSize),
                                                           (ARSTREAM2_RTCP_ClockDelta_t*)(packet + totalSize + sizeof(ARSTREAM2_RTCP_Application_t)),
                                                           sendTimestamp, context->senderSsrc,
                                                           &context->clockDelta);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Failed to generate application defined clock delta (%d)", ret);
        }
        else
        {
            totalSize += sizeof(ARSTREAM2_RTCP_Application_t) + sizeof(ARSTREAM2_RTCP_ClockDelta_t);
        }
    }

    if (size) *size = totalSize;
    return ret;
}


int ARSTREAM2_RTCP_Receiver_GenerateCompoundPacket(uint8_t *packet, unsigned int maxPacketSize,
                                                   uint64_t sendTimestamp, int generateReceiverReport,
                                                   int generateSourceDescription, int generateApplicationClockDelta,
                                                   int generateApplicationVideoStats, ARSTREAM2_RTCP_ReceiverContext_t *context,
                                                   unsigned int *size)
{
    int ret = 0;
    unsigned int totalSize = 0;

    if ((!packet) || (!context))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid pointer");
        return -1;
    }

    if (maxPacketSize == 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid max packet size");
        return -1;
    }

    if ((ret == 0) && (generateReceiverReport) && (totalSize + sizeof(ARSTREAM2_RTCP_ReceiverReport_t) + sizeof(ARSTREAM2_RTCP_ReceptionReportBlock_t) <= maxPacketSize))
    {
        unsigned int rrSize = 0;
        ret = ARSTREAM2_RTCP_Receiver_GenerateReceiverReport((ARSTREAM2_RTCP_ReceiverReport_t*)(packet + totalSize),
                                                             (ARSTREAM2_RTCP_ReceptionReportBlock_t*)(packet + totalSize + sizeof(ARSTREAM2_RTCP_ReceiverReport_t)),
                                                             sendTimestamp, context, &rrSize);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Failed to generate receiver report (%d)", ret);
        }
        else
        {
            totalSize += rrSize;
        }
    }

    if ((ret == 0) && (generateSourceDescription) && (context->cname))
    {
        unsigned int sdesSize = 0;
        ret = ARSTREAM2_RTCP_GenerateSourceDescription((ARSTREAM2_RTCP_Sdes_t*)(packet + totalSize), maxPacketSize - totalSize,
                                                       context->receiverSsrc, context->cname, context->name, &sdesSize);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Failed to generate source description (%d)", ret);
        }
        else
        {
            totalSize += sdesSize;
        }
    }

    if ((ret == 0) && (generateApplicationClockDelta) && (totalSize + sizeof(ARSTREAM2_RTCP_Application_t) + sizeof(ARSTREAM2_RTCP_ClockDelta_t) <= maxPacketSize))
    {
        ret = ARSTREAM2_RTCP_GenerateApplicationClockDelta((ARSTREAM2_RTCP_Application_t*)(packet + totalSize),
                                                           (ARSTREAM2_RTCP_ClockDelta_t*)(packet + totalSize + sizeof(ARSTREAM2_RTCP_Application_t)),
                                                           sendTimestamp, context->receiverSsrc,
                                                           &context->clockDelta);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Failed to generate application defined clock delta (%d)", ret);
        }
        else
        {
            totalSize += sizeof(ARSTREAM2_RTCP_Application_t) + sizeof(ARSTREAM2_RTCP_ClockDelta_t);
        }
    }

    if ((ret == 0) && (generateApplicationVideoStats) && (totalSize + sizeof(ARSTREAM2_RTCP_Application_t) + sizeof(ARSTREAM2_RTCP_VideoStats_t) <= maxPacketSize))
    {
        ret = ARSTREAM2_RTCP_GenerateApplicationVideoStats((ARSTREAM2_RTCP_Application_t*)(packet + totalSize),
                                                           (ARSTREAM2_RTCP_VideoStats_t*)(packet + totalSize + sizeof(ARSTREAM2_RTCP_Application_t)),
                                                           sendTimestamp, context->receiverSsrc,
                                                           &context->videoStats);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Failed to generate application defined video stats (%d)", ret);
        }
        else
        {
            totalSize += sizeof(ARSTREAM2_RTCP_Application_t) + sizeof(ARSTREAM2_RTCP_VideoStats_t);
        }
    }

    if (size) *size = totalSize;
    return ret;
}


int ARSTREAM2_RTCP_Sender_ProcessCompoundPacket(const uint8_t *packet, unsigned int packetSize,
                                                uint64_t receptionTimestamp,
                                                ARSTREAM2_RTCP_SenderContext_t *context,
                                                int *gotReceptionReport)
{
    unsigned int readSize = 0, size = 0;
    int receptionReportCount = 0, type, subType, ret, _ret = 0;

    if ((!packet) || (!context))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid pointer");
        return -1;
    }

    while (readSize < packetSize)
    {
        type = ARSTREAM2_RTCP_GetPacketType(packet, packetSize - readSize, &receptionReportCount, &size);
        if (type < 0)
        {
            _ret = -1;
            break;
        }
        switch (type)
        {
            case ARSTREAM2_RTCP_RECEIVER_REPORT_PACKET_TYPE:
                if (receptionReportCount > 0)
                {
                    ret = ARSTREAM2_RTCP_Sender_ProcessReceiverReport((ARSTREAM2_RTCP_ReceiverReport_t*)packet,
                                                                      (ARSTREAM2_RTCP_ReceptionReportBlock_t*)(packet + sizeof(ARSTREAM2_RTCP_ReceiverReport_t)),
                                                                      receptionTimestamp,
                                                                      context, gotReceptionReport);
                    if (ret != 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Failed to process receiver report (%d)", ret);
                    }
                    else
                    {
                        /*ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTCP_TAG, "Receiver state: RTD=%.1fms interarrivalJitter=%.1fms lost=%d lastLossRate=%.1f%% highestSeqNum=%d",
                                    (float)context->roundTripDelay / 1000., (float)context->interarrivalJitter / 1000.,
                                    context->receiverLostCount, (float)context->receiverFractionLost * 100. / 256.,
                                    context->receiverExtHighestSeqNum);*/
                    }
                }
                break;
            case ARSTREAM2_RTCP_SDES_PACKET_TYPE:
                ret = ARSTREAM2_RTCP_ProcessSourceDescription((ARSTREAM2_RTCP_Sdes_t*)packet);
                if (ret != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Failed to process source description (%d)", ret);
                }
                break;
            case ARSTREAM2_RTCP_APP_PACKET_TYPE:
                subType = ARSTREAM2_RTCP_GetApplicationPacketSubtype((ARSTREAM2_RTCP_Application_t*)packet);
                switch (subType)
                {
                    case ARSTREAM2_RTCP_APP_PACKET_CLOCKDELTA_SUBTYPE:
                        ret = ARSTREAM2_RTCP_ProcessApplicationClockDelta((ARSTREAM2_RTCP_Application_t*)packet,
                                                                          (ARSTREAM2_RTCP_ClockDelta_t*)(packet + sizeof(ARSTREAM2_RTCP_Application_t)),
                                                                          receptionTimestamp, context->receiverSsrc,
                                                                          &context->clockDelta);
                        if (ret != 0)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Failed to process application clock delta (%d)", ret);
                        }
                        else
                        {
                            /*ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTCP_TAG, "Clock delta: delta=%lli RTD=%lli",
                                        context->clockDelta.clockDelta, context->clockDelta.rtDelay);*/
                        }
                        break;
                    case ARSTREAM2_RTCP_APP_PACKET_VIDEOSTATS_SUBTYPE:
                        ret = ARSTREAM2_RTCP_ProcessApplicationVideoStats((ARSTREAM2_RTCP_Application_t*)packet,
                                                                          (ARSTREAM2_RTCP_VideoStats_t*)(packet + sizeof(ARSTREAM2_RTCP_Application_t)),
                                                                          receptionTimestamp, context->receiverSsrc,
                                                                          &context->videoStats);
                        if (ret != 0)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Failed to process application video stats (%d)", ret);
                        }
                        else
                        {
                            /*ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTCP_TAG, "Video stats");*/
                        }
                        break;
                    default:
                        break;
                }
                break;
            default:
                break;
        }
        readSize += size;
        packet += size;
    }

    return _ret;
}


int ARSTREAM2_RTCP_Receiver_ProcessCompoundPacket(const uint8_t *packet, unsigned int packetSize,
                                                  uint64_t receptionTimestamp,
                                                  ARSTREAM2_RTCP_ReceiverContext_t *context)
{
    unsigned int readSize = 0, size = 0;
    int type, subType, ret, _ret = 0;

    if ((!packet) || (!context))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid pointer");
        return -1;
    }

    while (readSize < packetSize)
    {
        type = ARSTREAM2_RTCP_GetPacketType(packet, packetSize - readSize, NULL, &size);
        if (type < 0)
        {
            _ret = -1;
            break;
        }
        switch (type)
        {
            case ARSTREAM2_RTCP_SENDER_REPORT_PACKET_TYPE:
                ret = ARSTREAM2_RTCP_Receiver_ProcessSenderReport((ARSTREAM2_RTCP_SenderReport_t*)packet,
                                                                  receptionTimestamp, context);
                if (ret != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Failed to process sender report (%d)", ret);
                }
                else
                {
                    /*ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTCP_TAG, "Sender state: interval=%.1fms packetRate=%.1fpacket/s bitrate=%.2fkbit/s",
                                (float)context->lastSrInterval / 1000., (float)context->srIntervalPacketCount * 1000000. / (float)context->lastSrInterval,
                                (float)context->srIntervalByteCount * 8000. / (float)context->lastSrInterval);*/
                }
                break;
            case ARSTREAM2_RTCP_SDES_PACKET_TYPE:
                ret = ARSTREAM2_RTCP_ProcessSourceDescription((ARSTREAM2_RTCP_Sdes_t*)packet);
                if (ret != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Failed to process source description (%d)", ret);
                }
                break;
            case ARSTREAM2_RTCP_APP_PACKET_TYPE:
                subType = ARSTREAM2_RTCP_GetApplicationPacketSubtype((ARSTREAM2_RTCP_Application_t*)packet);
                switch (subType)
                {
                    case ARSTREAM2_RTCP_APP_PACKET_CLOCKDELTA_SUBTYPE:
                        ret = ARSTREAM2_RTCP_ProcessApplicationClockDelta((ARSTREAM2_RTCP_Application_t*)packet,
                                                                          (ARSTREAM2_RTCP_ClockDelta_t*)(packet + sizeof(ARSTREAM2_RTCP_Application_t)),
                                                                          receptionTimestamp, context->senderSsrc,
                                                                          &context->clockDelta);
                        if (ret != 0)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Failed to process application clock delta (%d)", ret);
                        }
                        else
                        {
                            /*ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTCP_TAG, "Clock delta: delta=%lli RTD=%lli",
                                        context->clockDelta.clockDeltaAvg, context->clockDelta.rtDelay);*/
                        }
                        break;
                    default:
                        break;
                }
                break;
            default:
                break;
        }
        readSize += size;
        packet += size;
    }

    return _ret;
}
