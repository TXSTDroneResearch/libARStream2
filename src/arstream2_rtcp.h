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


/*
 * Functions
 */

int ARSTREAM2_Rtcp_GenerateSenderReport(ARSTREAM2_RTCP_SenderReport_t *senderReport,
                                        uint32_t ssrc, uint32_t rtpClockRate, uint32_t rtpTimestampOffset,
                                        uint32_t senderPacketCount, uint32_t senderByteCount);

int ARSTREAM2_Rtcp_IsSenderReport(const uint8_t *buffer, int bufferSize);

int ARSTREAM2_Rtcp_ParseSenderReport(const ARSTREAM2_RTCP_SenderReport_t *senderReport,
                                     uint32_t *ssrc, uint64_t *ntpTimestamp, uint32_t *rtpTimestamp,
                                     uint32_t *senderPacketCount, uint32_t *senderByteCount);

#endif /* _ARSTREAM2_RTCP_H_ */
