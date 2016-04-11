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
    uint32_t extHighestSeqNum;
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
 * @brief RTCP sender context
 */
typedef struct ARSTREAM2_RTCP_RtpSenderContext_s {
    uint32_t senderSsrc;
    uint32_t receiverSsrc;

    uint32_t rtpClockRate;
    uint32_t rtpTimestampOffset;
    uint32_t packetCount;
    uint32_t byteCount;
} ARSTREAM2_RTCP_RtpSenderContext_t;

/**
 * @brief RTCP receiver context
 */
typedef struct ARSTREAM2_RTCP_RtpReceiverContext_s {
    uint32_t receiverSsrc;
    uint32_t senderSsrc;

    uint32_t prevSrRtpTimestamp;
    uint64_t prevSrNtpTimestamp;
    uint32_t prevSrPacketCount;
    uint32_t prevSrByteCount;
    int64_t tsAnum;
    int64_t tsAden;
    int64_t tsB;
    uint32_t lastSrInterval; // in microseconds
    uint32_t srIntervalPacketCount; // over the last SR interval
    uint32_t srIntervalByteCount; // over the last SR interval

    uint32_t firstSeqNum;
    uint32_t extHighestSeqNum;
    uint32_t packetsReceived;
    uint32_t packetsLost;
    uint32_t interarrivalJitter;
    uint32_t lastRrExtHighestSeqNum;
    uint32_t lastRrPacketsReceived;
    uint32_t lastRrPacketsLost;
    uint64_t lastSrReceptionTimestamp;
    uint64_t lastRrTimestamp;
} ARSTREAM2_RTCP_RtpReceiverContext_t;


/*
 * Functions
 */

int ARSTREAM2_RTCP_IsSenderReport(const uint8_t *buffer, int bufferSize);

int ARSTREAM2_RTCP_IsReceiverReport(const uint8_t *buffer, int bufferSize, int *receptionReportCount);

int ARSTREAM2_RTCP_Sender_ProcessReceiverReport(ARSTREAM2_RTCP_ReceiverReport_t *receiverReport,
                                                ARSTREAM2_RTCP_ReceptionReportBlock_t *receptionReport,
                                                ARSTREAM2_RTCP_RtpSenderContext_t *context);

int ARSTREAM2_RTCP_Sender_GenerateSenderReport(ARSTREAM2_RTCP_SenderReport_t *senderReport,
                                               ARSTREAM2_RTCP_RtpSenderContext_t *context);

int ARSTREAM2_RTCP_Receiver_ProcessSenderReport(const ARSTREAM2_RTCP_SenderReport_t *senderReport,
                                                ARSTREAM2_RTCP_RtpReceiverContext_t *context);

int ARSTREAM2_RTCP_Receiver_GenerateReceiverReport(ARSTREAM2_RTCP_ReceiverReport_t *receiverReport,
                                                   ARSTREAM2_RTCP_ReceptionReportBlock_t *receptionReport,
                                                   ARSTREAM2_RTCP_RtpReceiverContext_t *context);

static inline uint64_t ARSTREAM2_RTCP_Receiver_GetNtpTimestampFromRtpTimestamp(ARSTREAM2_RTCP_RtpReceiverContext_t *context, uint32_t rtpTimestamp);


/*
 * Inline functions
 */

static inline uint64_t ARSTREAM2_RTCP_Receiver_GetNtpTimestampFromRtpTimestamp(ARSTREAM2_RTCP_RtpReceiverContext_t *context, uint32_t rtpTimestamp)
{
    return ((context->tsAnum != 0) && (context->tsAden != 0)) ? (uint64_t)((((int64_t)rtpTimestamp - context->tsB) * context->tsAden + context->tsAnum / 2) / context->tsAnum) : 0;
}

#endif /* _ARSTREAM2_RTCP_H_ */
