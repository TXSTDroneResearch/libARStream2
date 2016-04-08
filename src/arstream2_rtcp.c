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


int ARSTREAM2_RTCP_GenerateSenderReport(ARSTREAM2_RTCP_SenderReport_t *senderReport,
										uint32_t ssrc, uint32_t rtpClockRate, uint32_t rtpTimestampOffset,
										uint32_t senderPacketCount, uint32_t senderByteCount)
{
    struct timespec t1;
    ARSAL_Time_GetTime(&t1);
    uint64_t ntpTimestamp = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;
    uint32_t rtpTimestamp = rtpTimestampOffset + (uint32_t)((((ntpTimestamp * rtpClockRate) + 500000) / 1000000) & 0xFFFFFFFF); /* microseconds to rtpClockRate */

    if (!senderReport)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Invalid pointer");
        return -1;
    }

    senderReport->flags = (2 << 6);
    senderReport->packetType = ARSTREAM2_RTCP_SENDER_REPORT_PACKET_TYPE;
    senderReport->length = htons(6);
    senderReport->ssrc = htonl(ssrc);
    senderReport->ntpTimestampH = htonl((uint32_t)((ntpTimestamp >> 32) & 0xFFFFFFFF));
    senderReport->ntpTimestampL = htonl((uint32_t)(ntpTimestamp & 0xFFFFFFFF));
    senderReport->rtpTimestamp = htonl(rtpTimestamp);
    senderReport->senderPacketCount = htonl(senderPacketCount);
    senderReport->senderByteCount = htonl(senderByteCount);

    return 0;
}


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


int ARSTREAM2_RTCP_ParseSenderReport(const ARSTREAM2_RTCP_SenderReport_t *senderReport,
                                     uint32_t *ssrc, uint64_t *ntpTimestamp, uint32_t *rtpTimestamp,
                                     uint32_t *senderPacketCount, uint32_t *senderByteCount)
{
    if (!senderReport)
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

    if (ssrc)
    {
        *ssrc = ntohl(senderReport->ssrc);
    }

    if (ntpTimestamp)
    {
        uint64_t _ntpTimestamp;
        _ntpTimestamp = ((uint64_t)(ntohl(senderReport->ntpTimestampH)) << 32) + (uint64_t)(ntohl(senderReport->ntpTimestampL));
        *ntpTimestamp = _ntpTimestamp;
    }

    if (rtpTimestamp)
    {
        *rtpTimestamp = ntohl(senderReport->rtpTimestamp);
    }

    if (senderPacketCount)
    {
        *senderPacketCount = ntohl(senderReport->senderPacketCount);
    }

    if (senderByteCount)
    {
        *senderByteCount = ntohl(senderReport->senderByteCount);
    }

    return 0;
}


int ARSTREAM2_RTCP_UpdateRtpSenderState(ARSTREAM2_RTCP_RtpSenderState_t *sender,
                                        uint32_t ssrc, uint64_t ntpTimestamp, uint32_t rtpTimestamp,
                                        uint32_t senderPacketCount, uint32_t senderByteCount)
{
    if (sender->ssrc == 0)
    {
        sender->ssrc = ssrc;
    }

    if (ssrc != sender->ssrc)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Unexpected sender SSRC");
        return -1;
    }

    if (ntpTimestamp <= sender->prevNtpTimestamp)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTCP_TAG, "Out of order or duplicate sender report");
        return -1;
    }

    // NTP to RTP linear regression: RTP = a * NTP + b
    sender->tsAnum = (int64_t)(rtpTimestamp - sender->prevRtpTimestamp);
    sender->tsAden = (int64_t)(ntpTimestamp - sender->prevNtpTimestamp);
    sender->tsB = (int64_t)rtpTimestamp - (int64_t)((sender->tsAnum * ntpTimestamp + sender->tsAden / 2) / sender->tsAden);

    // Packet and byte rates
    sender->lastReportInterval = (uint32_t)(ntpTimestamp - sender->prevNtpTimestamp);
    if (sender->lastReportInterval > 0)
    {
        sender->intervalPacketCount = senderPacketCount - sender->prevSenderPacketCount;
        sender->intervalByteCount = senderByteCount - sender->prevSenderByteCount;
    }
    else
    {
        sender->intervalPacketCount = sender->intervalByteCount = 0;
    }

    // Update values
    sender->prevRtpTimestamp = rtpTimestamp;
    sender->prevNtpTimestamp = ntpTimestamp;
    sender->prevSenderPacketCount = senderPacketCount;
    sender->prevSenderByteCount = senderByteCount;

    return 0;
}
