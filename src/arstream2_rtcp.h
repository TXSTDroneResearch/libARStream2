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
#define ARSTREAM2_RTCP_SDES_PACKET_TYPE 202
#define ARSTREAM2_RTCP_BYE_PACKET_TYPE 203
#define ARSTREAM2_RTCP_APP_PACKET_TYPE 204

#define ARSTREAM2_RTCP_SDES_CNAME_ITEM 1
#define ARSTREAM2_RTCP_SDES_NAME_ITEM 2
#define ARSTREAM2_RTCP_SDES_EMAIL_ITEM 3
#define ARSTREAM2_RTCP_SDES_PHONE_ITEM 4
#define ARSTREAM2_RTCP_SDES_LOC_ITEM 5
#define ARSTREAM2_RTCP_SDES_TOOL_ITEM 6
#define ARSTREAM2_RTCP_SDES_NOTE_ITEM 7
#define ARSTREAM2_RTCP_SDES_PRIV_ITEM 8

#define ARSTREAM2_RTCP_APP_PACKET_NAME 0x41525354
#define ARSTREAM2_RTCP_APP_PACKET_CLOCKDELTA_SUBTYPE 1

#define ARSTREAM2_RTCP_SENDER_DEFAULT_BITRATE 25000
#define ARSTREAM2_RTCP_RECEIVER_DEFAULT_BITRATE 25000
#define ARSTREAM2_RTCP_SENDER_BANDWIDTH_SHARE 0.025
#define ARSTREAM2_RTCP_RECEIVER_BANDWIDTH_SHARE 0.025
#define ARSTREAM2_RTCP_SENDER_MIN_PACKET_TIME_INTERVAL 100000
#define ARSTREAM2_RTCP_RECEIVER_MIN_PACKET_TIME_INTERVAL 100000


/*
 * Types
 */

/**
 * @brief RTCP Sender Report (SR) Packet (see RFC3550)
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
 * @brief RTCP Receiver Report (RR) Packet (see RFC3550)
 */
typedef struct {
    uint8_t flags;
    uint8_t packetType;
    uint16_t length;
    uint32_t ssrc;
} __attribute__ ((packed)) ARSTREAM2_RTCP_ReceiverReport_t;

/**
 * @brief RTCP Source Description (SDES) Packet (see RFC3550)
 */
typedef struct {
    uint8_t flags;
    uint8_t packetType;
    uint16_t length;
} __attribute__ ((packed)) ARSTREAM2_RTCP_Sdes_t;

/**
 * @brief RTCP Goodbye (BYE) Packet (see RFC3550)
 */
typedef struct {
    uint8_t flags;
    uint8_t packetType;
    uint16_t length;
    uint32_t ssrc;
} __attribute__ ((packed)) ARSTREAM2_RTCP_Goodbye_t;

/**
 * @brief RTCP Application-Defined (APP) Packet (see RFC3550)
 */
typedef struct {
    uint8_t flags;
    uint8_t packetType;
    uint16_t length;
    uint32_t ssrc;
    uint32_t name;
} __attribute__ ((packed)) ARSTREAM2_RTCP_Application_t;

/**
 * @brief Application defined clock delta data
 */
typedef struct {
    uint32_t originateTimestampH;
    uint32_t originateTimestampL;
    uint32_t receiveTimestampH;
    uint32_t receiveTimestampL;
    uint32_t transmitTimestampH;
    uint32_t transmitTimestampL;
} __attribute__ ((packed)) ARSTREAM2_RTCP_ClockDelta_t;


/**
 * @brief Application clock delta context
 */
typedef struct ARSTREAM2_RTCP_ClockDeltaContext_s {
    uint64_t nextPeerOriginateTimestamp;
    uint64_t nextReceiveTimestamp;
    int64_t clockDelta;
    int64_t clockDeltaAvg;
    int64_t rtDelay;
} ARSTREAM2_RTCP_ClockDeltaContext_t;

/**
 * @brief RTCP sender context
 */
typedef struct ARSTREAM2_RTCP_SenderContext_s {
    uint32_t senderSsrc;
    uint32_t receiverSsrc;
    uint32_t rtcpByteRate;

    uint32_t rtpClockRate;
    uint32_t rtpTimestampOffset;

    uint64_t lastRrReceptionTimestamp;
    uint32_t roundTripDelay;
    uint32_t interarrivalJitter;
    uint32_t receiverFractionLost;
    uint32_t receiverLostCount;
    uint32_t receiverExtHighestSeqNum;

    uint64_t lastSrTimestamp;
    uint32_t lastSrInterval; // in microseconds
    uint32_t prevSrPacketCount;
    uint32_t prevSrByteCount;
    uint32_t srIntervalPacketCount; // over the last SR interval
    uint32_t srIntervalByteCount; // over the last SR interval

    ARSTREAM2_RTCP_ClockDeltaContext_t clockDelta;
} ARSTREAM2_RTCP_SenderContext_t;

/**
 * @brief RTCP receiver context
 */
typedef struct ARSTREAM2_RTCP_ReceiverContext_s {
    uint32_t receiverSsrc;
    uint32_t senderSsrc;
    uint32_t rtcpByteRate;

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

    ARSTREAM2_RTCP_ClockDeltaContext_t clockDelta;
} ARSTREAM2_RTCP_ReceiverContext_t;


/*
 * Functions
 */

int ARSTREAM2_RTCP_GetPacketType(const uint8_t *buffer, unsigned int bufferSize, int *receptionReportCount, unsigned int *size);

int ARSTREAM2_RTCP_Sender_ProcessReceiverReport(const ARSTREAM2_RTCP_ReceiverReport_t *receiverReport,
                                                const ARSTREAM2_RTCP_ReceptionReportBlock_t *receptionReport,
                                                uint64_t receptionTimestamp,
                                                ARSTREAM2_RTCP_SenderContext_t *context);

int ARSTREAM2_RTCP_Sender_GenerateSenderReport(ARSTREAM2_RTCP_SenderReport_t *senderReport,
                                               uint64_t sendTimestamp, uint32_t packetCount, uint32_t byteCount,
                                               ARSTREAM2_RTCP_SenderContext_t *context);

int ARSTREAM2_RTCP_Receiver_ProcessSenderReport(const ARSTREAM2_RTCP_SenderReport_t *senderReport,
                                                uint64_t receptionTimestamp,
                                                ARSTREAM2_RTCP_ReceiverContext_t *context);

int ARSTREAM2_RTCP_Receiver_GenerateReceiverReport(ARSTREAM2_RTCP_ReceiverReport_t *receiverReport,
                                                   ARSTREAM2_RTCP_ReceptionReportBlock_t *receptionReport,
                                                   uint64_t sendTimestamp,
                                                   ARSTREAM2_RTCP_ReceiverContext_t *context);

int ARSTREAM2_RTCP_GenerateSourceDescription(ARSTREAM2_RTCP_Sdes_t *sdes, unsigned int maxSize, uint32_t ssrc, const char *cname, unsigned int *size);

int ARSTREAM2_RTCP_ProcessSourceDescription(const ARSTREAM2_RTCP_Sdes_t *sdes);

int ARSTREAM2_RTCP_GenerateApplicationClockDelta(ARSTREAM2_RTCP_Application_t *app, ARSTREAM2_RTCP_ClockDelta_t *clockDelta,
                                                 uint64_t sendTimestamp, uint32_t ssrc,
                                                 ARSTREAM2_RTCP_ClockDeltaContext_t *context);

int ARSTREAM2_RTCP_ProcessApplicationClockDelta(const ARSTREAM2_RTCP_Application_t *app,
                                                const ARSTREAM2_RTCP_ClockDelta_t *clockDelta,
                                                uint64_t receptionTimestamp, uint32_t peerSsrc,
                                                ARSTREAM2_RTCP_ClockDeltaContext_t *context);

int ARSTREAM2_RTCP_Sender_GenerateCompoundPacket(uint8_t *packet, unsigned int maxPacketSize,
                                                 uint64_t sendTimestamp, int generateSenderReport,
                                                 int generateSourceDescription, int generateApplicationClockDelta,
                                                 const char *cname, uint32_t packetCount, uint32_t byteCount,
                                                 ARSTREAM2_RTCP_SenderContext_t *context,
                                                 unsigned int *size);

int ARSTREAM2_RTCP_Receiver_GenerateCompoundPacket(uint8_t *packet, unsigned int maxPacketSize,
                                                   uint64_t sendTimestamp, int generateReceiverReport,
                                                   int generateSourceDescription, int generateApplicationClockDelta,
                                                   const char *cname, ARSTREAM2_RTCP_ReceiverContext_t *context,
                                                   unsigned int *size);

int ARSTREAM2_RTCP_Sender_ProcessCompoundPacket(const uint8_t *packet, unsigned int packetSize,
                                                uint64_t receptionTimestamp,
                                                ARSTREAM2_RTCP_SenderContext_t *context,
                                                int *gotReceiverReport);

int ARSTREAM2_RTCP_Receiver_ProcessCompoundPacket(const uint8_t *packet, unsigned int packetSize,
                                                  uint64_t receptionTimestamp,
                                                  ARSTREAM2_RTCP_ReceiverContext_t *context);

static inline uint64_t ARSTREAM2_RTCP_Receiver_GetNtpTimestampFromRtpTimestamp(ARSTREAM2_RTCP_ReceiverContext_t *context, uint32_t rtpTimestamp);


/*
 * Inline functions
 */

static inline uint64_t ARSTREAM2_RTCP_Receiver_GetNtpTimestampFromRtpTimestamp(ARSTREAM2_RTCP_ReceiverContext_t *context, uint32_t rtpTimestamp)
{
    return ((context->tsAnum != 0) && (context->tsAden != 0)) ? (uint64_t)((((int64_t)rtpTimestamp - context->tsB) * context->tsAden + context->tsAnum / 2) / context->tsAnum) : 0;
}

#endif /* _ARSTREAM2_RTCP_H_ */
