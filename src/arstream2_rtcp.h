/**
 * @file arstream2_rtcp.h
 * @brief Parrot Streaming Library - RTCP implementation
 * @date 04/06/2016
 * @author aurelien.barre@parrot.com
 */

#ifndef _ARSTREAM2_RTCP_H_
#define _ARSTREAM2_RTCP_H_

#include <inttypes.h>


/*
 * Macros
 */

#define ARSTREAM2_RTCP_SENDER_REPORT_PACKET_TYPE 200
#define ARSTREAM2_RTCP_RECEIVER_REPORT_PACKET_TYPE 201


/*
 * Types
 */

/**
 * @brief RTCP Reception Report Block (see RFC3550)
 */
typedef struct {
    uint32_t ssrc;
    uint32_t lost;
    uint32_t highestSeqNum;
    uint32_t interarrivalJitter;
    uint32_t lsr;
    uint32_t dlsr;
} __attribute__ ((packed)) ARSTREAM2_RTCP_ReceptionReportBlock_t;

/**
 * @brief RTCP Receiver Report Packet (see RFC3550)
 */
typedef struct {
    uint8_t flags;
    uint8_t packetType;
    uint16_t length;
    uint32_t ssrc;
} __attribute__ ((packed)) ARSTREAM2_RTCP_ReceiverReport_t;

/**
 * @brief RTCP Sender Report Packet (see RFC3550)
 */
typedef struct {
    uint8_t flags;
    uint8_t packetType;
    uint16_t length;
    uint32_t ssrc;
    uint32_t ntpTimestampH;
    uint32_t ntpTimestampL;
    uint32_t rtpTimestamp;
    uint32_t senderPacketCount;
    uint32_t senderByteCount;
} __attribute__ ((packed)) ARSTREAM2_RTCP_SenderReport_t;

/**
 * @brief RTP sender state data
 */
typedef struct ARSTREAM2_RTCP_RtpSenderState_s {
    uint32_t ssrc;
    uint32_t prevRtpTimestamp;
    uint64_t prevNtpTimestamp;
    uint32_t prevSenderPacketCount;
    uint32_t prevSenderByteCount;
    int64_t tsAnum;
    int64_t tsAden;
    int64_t tsB;
} ARSTREAM2_RTCP_RtpSenderState_t;


/*
 * Functions
 */

int ARSTREAM2_RTCP_GenerateSenderReport(ARSTREAM2_RTCP_SenderReport_t *senderReport,
                                        uint32_t ssrc, uint32_t rtpClockRate, uint32_t rtpTimestampOffset,
                                        uint32_t senderPacketCount, uint32_t senderByteCount);

int ARSTREAM2_RTCP_IsSenderReport(const uint8_t *buffer, int bufferSize);

int ARSTREAM2_RTCP_ParseSenderReport(const ARSTREAM2_RTCP_SenderReport_t *senderReport,
                                     uint32_t *ssrc, uint64_t *ntpTimestamp, uint32_t *rtpTimestamp,
                                     uint32_t *senderPacketCount, uint32_t *senderByteCount);

int ARSTREAM2_RTCP_UpdateRtpSenderState(ARSTREAM2_RTCP_RtpSenderState_t *sender,
                                        uint32_t ssrc, uint64_t ntpTimestamp, uint32_t rtpTimestamp,
                                        uint32_t senderPacketCount, uint32_t senderByteCount);

static inline uint64_t ARSTREAM2_RTCP_GetNtpTimestampFromRtpTimestamp(ARSTREAM2_RTCP_RtpSenderState_t *sender, uint32_t rtpTimestamp);


/*
 * Inline functions
 */

static inline uint64_t ARSTREAM2_RTCP_GetNtpTimestampFromRtpTimestamp(ARSTREAM2_RTCP_RtpSenderState_t *sender, uint32_t rtpTimestamp)
{
    return (uint64_t)((((int64_t)rtpTimestamp - sender->tsB) * sender->tsAden + sender->tsAnum / 2) / sender->tsAnum);
}

#endif /* _ARSTREAM2_RTCP_H_ */
