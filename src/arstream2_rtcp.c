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


int ARSTREAM2_Rtcp_GenerateSenderReport(ARSTREAM2_RTCP_SenderReport_t *senderReport,
										uint32_t ssrc, uint32_t rtpClockRate, uint32_t rtpTimestampOffset,
										uint32_t senderPacketCount, uint32_t senderByteCount)
{
    struct timespec t1;
    ARSAL_Time_GetTime(&t1);
    uint64_t ntpTimestamp = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;
    uint32_t rtpTimestamp = rtpTimestampOffset + (uint32_t)((((ntpTimestamp * rtpClockRate) + 500000) / 1000000) & 0xFFFFFFFF); /* microseconds to rtpClockRate */

    if (!senderReport)
    {
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
