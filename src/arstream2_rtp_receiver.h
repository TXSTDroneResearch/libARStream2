/**
 * @file arstream2_rtp_receiver.h
 * @brief Parrot Streaming Library - RTP Receiver
 * @date 04/16/2015
 * @author aurelien.barre@parrot.com
 */

#ifndef _ARSTREAM2_RTP_RECEIVER_H_
#define _ARSTREAM2_RTP_RECEIVER_H_

#include <config.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#define __USE_GNU
#include <sys/socket.h>
#undef __USE_GNU
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <math.h>

#include <libARStream2/arstream2_error.h>
#include "arstream2_rtp_sender.h"
#include "arstream2_rtp.h"
#include "arstream2_rtp_h264.h"
#include "arstream2_rtcp.h"
#include "arstream2_h264.h"

#include <libARSAL/ARSAL_Print.h>
#include <libARSAL/ARSAL_Mutex.h>

#if BUILD_LIBMUX
#include <libmux.h>
#include <libmux-arsdk.h>
#include <libpomp.h>
#endif


#define ARSTREAM2_RTP_RECEIVER_DEFAULT_CLIENT_STREAM_PORT     (55004)
#define ARSTREAM2_RTP_RECEIVER_DEFAULT_CLIENT_CONTROL_PORT    (55005)

#define ARSTREAM2_RTP_RECEIVER_TIMEOUT_US (100 * 1000)
#define ARSTREAM2_RTP_RECEIVER_MUX_TIMEOUT_US (10 * 1000)

#define ARSTREAM2_RTP_RECEIVER_STREAM_DATAREAD_TIMEOUT_MS (500)
#define ARSTREAM2_RTP_RECEIVER_CONTROL_DATAREAD_TIMEOUT_MS (500)

#define ARSTREAM2_RTP_RECEIVER_DEFAULT_MIN_PACKET_FIFO_BUFFER_COUNT (500)
#define ARSTREAM2_RTP_RECEIVER_DEFAULT_PACKET_FIFO_BUFFER_TO_ITEM_FACTOR (4)
#define ARSTREAM2_RTP_RECEIVER_DEFAULT_MIN_PACKET_FIFO_ITEM_COUNT (ARSTREAM2_RTP_RECEIVER_DEFAULT_MIN_PACKET_FIFO_BUFFER_COUNT * ARSTREAM2_RTP_RECEIVER_DEFAULT_PACKET_FIFO_BUFFER_TO_ITEM_FACTOR)

#define ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_MAX_COUNT (4)
#define ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_MAX_NALU_BUFFER_COUNT (1024) //TODO: tune this value
#define ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_NALU_BUFFER_MALLOC_CHUNK_SIZE (4096)

#define ARSTREAM2_RTP_RECEIVER_MONITORING_MAX_POINTS (2048)


/**
 * @brief RtpReceiver net configuration parameters
 */
typedef struct ARSTREAM2_RtpReceiver_NetConfig_t
{
    const char *serverAddr;                         /**< Server address */
    const char *mcastAddr;                          /**< Multicast receive address (optional, NULL for no multicast) */
    const char *mcastIfaceAddr;                     /**< Multicast input interface address (required if mcastAddr is not NULL) */
    int serverStreamPort;                           /**< Server stream port, @see ARSTREAM2_RTP_SENDER_DEFAULT_SERVER_STREAM_PORT */
    int serverControlPort;                          /**< Server control port, @see ARSTREAM2_RTP_SENDER_DEFAULT_SERVER_CONTROL_PORT */
    int clientStreamPort;                           /**< Client stream port */
    int clientControlPort;                          /**< Client control port */
    eARSAL_SOCKET_CLASS_SELECTOR classSelector;     /**< Type of Service class selector */
} ARSTREAM2_RtpReceiver_NetConfig_t;

// Forward declaration of the mux_ctx structure
struct mux_ctx;

/**
 * @brief RtpReceiver mux configuration parameters
 */
typedef struct ARSTREAM2_RtpReceiver_MuxConfig_t
{
    struct mux_ctx *mux;                            /**< libmux context */
} ARSTREAM2_RtpReceiver_MuxConfig_t;
/**
 * @brief RtpReceiver configuration parameters
 */
typedef struct ARSTREAM2_RtpReceiver_Config_t
{
    const char *canonicalName;                      /**< RTP participant canonical name (CNAME SDES item) */
    const char *friendlyName;                       /**< RTP participant friendly name (NAME SDES item) (optional, can be NULL) */
    ARSTREAM2_H264_AuFifo_t *auFifo;
    ARSTREAM2_H264_NaluFifo_t *naluFifo;
    ARSTREAM2_H264_ReceiverAuCallback_t auCallback;
    void *auCallbackUserPtr;
    int maxPacketSize;                              /**< Maximum network packet size in bytes (should be provided by the server, if 0 the maximum UDP packet size is used) */
    int insertStartCodes;                           /**< Boolean-like (0-1) flag: if active insert a start code prefix before NAL units */
    int generateReceiverReports;                    /**< Boolean-like (0-1) flag: if active generate RTCP receiver reports */
    uint32_t videoStatsSendTimeInterval;            /**< Time interval for sending video stats in compound RTCP packets (optional, can be null) */
} ARSTREAM2_RtpReceiver_Config_t;


/**
 * @brief RtpReceiver RtpResender configuration parameters
 */
typedef struct ARSTREAM2_RtpReceiver_RtpResender_Config_t
{
    const char *canonicalName;                      /**< RTP participant canonical name (CNAME SDES item) */
    const char *friendlyName;                       /**< RTP participant friendly name (NAME SDES item) (optional, can be NULL) */
    const char *clientAddr;                         /**< Client address */
    const char *mcastAddr;                          /**< Multicast send address (optional, NULL for no multicast) */
    const char *mcastIfaceAddr;                     /**< Multicast output interface address (required if mcastAddr is not NULL) */
    int serverStreamPort;                           /**< Server stream port, @see ARSTREAM2_RTP_SENDER_DEFAULT_SERVER_STREAM_PORT */
    int serverControlPort;                          /**< Server control port, @see ARSTREAM2_RTP_SENDER_DEFAULT_SERVER_CONTROL_PORT */
    int clientStreamPort;                           /**< Client stream port */
    int clientControlPort;                          /**< Client control port */
    eARSAL_SOCKET_CLASS_SELECTOR classSelector;     /**< Type of Service class selector */
    int streamSocketBufferSize;                     /**< Send buffer size for the stream socket (optional, can be 0) */
    int maxPacketSize;                              /**< Maximum network packet size in bytes (example: the interface MTU) */
    int maxNetworkLatencyMs;                        /**< Maximum acceptable network latency in milliseconds */
    int useRtpHeaderExtensions;                     /**< Boolean-like (0-1) flag: if active insert access unit metadata as RTP header extensions */
} ARSTREAM2_RtpReceiver_RtpResender_Config_t;


/**
 * @brief An RtpReceiver instance to allow receiving H.264 video over a network
 */
typedef struct ARSTREAM2_RtpReceiver_t ARSTREAM2_RtpReceiver_t;


/**
 * @brief An RtpReceiver RtpResender instance to allow re-streaming H.264 video over a network
 */
typedef struct ARSTREAM2_RtpReceiver_RtpResender_t ARSTREAM2_RtpReceiver_RtpResender_t;


typedef struct ARSTREAM2_RtpReceiver_MonitoringPoint_s {
    uint64_t recvTimestamp;
    uint64_t ntpTimestamp;
    uint64_t ntpTimestampLocal;
    uint32_t rtpTimestamp;
    uint16_t seqNum;
    uint16_t markerBit;
    uint32_t bytes;
} ARSTREAM2_RtpReceiver_MonitoringPoint_t;


struct ARSTREAM2_RtpReceiver_RtpResender_t {
    ARSTREAM2_RtpReceiver_t *receiver;
    ARSTREAM2_RtpSender_t *sender;
    int useRtpHeaderExtensions;
    int senderRunning;
};


typedef struct ARSTREAM2_RtpReceiver_NaluBuffer_s {
    int useCount;
    uint8_t *naluBuffer;
    int naluBufferSize;
    int naluSize;
    uint64_t auTimestamp;
    int isLastNaluInAu;
} ARSTREAM2_RtpReceiver_NaluBuffer_t;

struct ARSTREAM2_RtpReceiver_NetInfos_t {
    char *serverAddr;
    char *mcastAddr;
    char *mcastIfaceAddr;
    int serverStreamPort;
    int serverControlPort;
    int clientStreamPort;
    int clientControlPort;
    int classSelector;

    /* Sockets */
    int isMulticast;
    int streamSocket;
    int controlSocket;
};

struct ARSTREAM2_RtpReceiver_MuxInfos_t {
    struct mux_ctx *mux;
    struct mux_queue *control;
    struct mux_queue *data;
};

struct ARSTREAM2_RtpReceiver_Ops_t {
    /* Stream channel */
    int (*streamChannelSetup)(ARSTREAM2_RtpReceiver_t *);
    int (*streamChannelTeardown)(ARSTREAM2_RtpReceiver_t *);
    int (*streamChannelRead)(ARSTREAM2_RtpReceiver_t *,
                             uint8_t *,
                             int,
                             int *);
    int (*streamChannelRecvMmsg)(ARSTREAM2_RtpReceiver_t *,
                                 struct mmsghdr *,
                                 unsigned int,
                                 int);


    /* Control channel */
    int (*controlChannelSetup)(ARSTREAM2_RtpReceiver_t *);
    int (*controlChannelTeardown)(ARSTREAM2_RtpReceiver_t *);
    int (*controlChannelSend)(ARSTREAM2_RtpReceiver_t *,
                              uint8_t *,
                              int);
    int (*controlChannelRead)(ARSTREAM2_RtpReceiver_t *,
                              uint8_t *,
                              int,
                              int);
};

struct ARSTREAM2_RtpReceiver_t {
    /* Configuration on New */
    int useMux;
    struct ARSTREAM2_RtpReceiver_NetInfos_t net;
    struct ARSTREAM2_RtpReceiver_MuxInfos_t mux;
    struct ARSTREAM2_RtpReceiver_Ops_t ops;

    /* Process context */
    ARSTREAM2_RTP_ReceiverContext_t rtpReceiverContext;
    ARSTREAM2_RTPH264_ReceiverContext_t rtph264ReceiverContext;
    ARSTREAM2_RTCP_ReceiverContext_t rtcpReceiverContext;

    char *canonicalName;
    char *friendlyName;
    int insertStartCodes;
    int generateReceiverReports;
    uint8_t *rtcpMsgBuffer;

    /* Thread status */
    ARSAL_Mutex_t streamMutex;
    int threadShouldStop;
    int threadStarted;
    int pipe[2];

    /* Packet FIFO */
    ARSTREAM2_RTP_PacketFifo_t packetFifo;
    ARSTREAM2_RTP_PacketFifoQueue_t packetFifoQueue;
    struct mmsghdr *msgVec;
    unsigned int msgVecCount;

    /* NAL unit and access unit FIFO */
    ARSTREAM2_H264_NaluFifo_t *naluFifo;
    ARSTREAM2_H264_AuFifo_t *auFifo;

    /* Monitoring */
    ARSAL_Mutex_t monitoringMutex;
    int monitoringCount;
    int monitoringIndex;
    ARSTREAM2_RtpReceiver_MonitoringPoint_t monitoringPoint[ARSTREAM2_RTP_RECEIVER_MONITORING_MAX_POINTS];

    /* RtpResenders */
    ARSTREAM2_RtpReceiver_RtpResender_t *resender[ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_MAX_COUNT];
    int resenderCount;
    ARSAL_Mutex_t resenderMutex;
    ARSAL_Mutex_t naluBufferMutex;
    ARSTREAM2_RtpReceiver_NaluBuffer_t naluBuffer[ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_MAX_NALU_BUFFER_COUNT];
    int naluBufferCount;
};


/**
 * @brief Creates a new RtpReceiver
 * @warning This function allocates memory. The receiver must be deleted by a call to ARSTREAM2_RtpReceiver_Delete()
 *
 * @param[in] config Pointer to a configuration parameters structure
 * @param[out] error Optionnal pointer to an eARSTREAM2_ERROR to hold any error information
 *
 * @return A pointer to the new ARSTREAM2_RtpReceiver_t, or NULL if an error occured
 *
 * @see ARSTREAM2_RtpReceiver_Stop()
 * @see ARSTREAM2_RtpReceiver_Delete()
 */
ARSTREAM2_RtpReceiver_t* ARSTREAM2_RtpReceiver_New(ARSTREAM2_RtpReceiver_Config_t *config,
                                                   ARSTREAM2_RtpReceiver_NetConfig_t *net_config,
                                                   ARSTREAM2_RtpReceiver_MuxConfig_t *mux_config,
                                                   eARSTREAM2_ERROR *error);


/**
 * @brief Stops a running RtpReceiver
 * @warning Once stopped, a receiver cannot be restarted
 *
 * @param[in] receiver The receiver instance
 *
 * @note Calling this function multiple times has no effect
 */
void ARSTREAM2_RtpReceiver_Stop(ARSTREAM2_RtpReceiver_t *receiver);


/**
 * @brief Deletes an RtpReceiver
 * @warning This function should NOT be called on a running receiver
 *
 * @param receiver Pointer to the ARSTREAM2_RtpReceiver_t* to delete
 *
 * @return ARSTREAM2_OK if the receiver was deleted
 * @return ARSTREAM2_ERROR_BUSY if the receiver is still busy and can not be stopped now (probably because ARSTREAM2_RtpReceiver_Stop() has not been called yet)
 * @return ARSTREAM2_ERROR_BAD_PARAMETERS if receiver does not point to a valid ARSTREAM2_RtpReceiver_t
 *
 * @note The function uses a double pointer, so it can set *receiver to NULL after freeing it
 */
eARSTREAM2_ERROR ARSTREAM2_RtpReceiver_Delete(ARSTREAM2_RtpReceiver_t **receiver);


/**
 * @brief Runs the main loop of the RtpReceiver
 * @warning This function never returns until ARSTREAM2_RtpReceiver_Stop() is called. Thus, it should be called on its own thread.
 * @post Stop the receiver by calling ARSTREAM2_RtpReceiver_Stop() before joining the thread calling this function.
 *
 * @param[in] ARSTREAM2_RtpReceiver_t_Param A valid (ARSTREAM2_RtpReceiver_t *) casted as a (void *)
 */
void* ARSTREAM2_RtpReceiver_RunThread(void *ARSTREAM2_RtpReceiver_t_Param);


/**
 * @brief Update the video stats
 * The video stats are provided by the upper layer to be sent in RTCP compound packets.
 *
 * @param[in] receiver The receiver instance
 * @param[in] videoStats Video stats data
 *
 * @return ARSTREAM2_OK if no error occured.
 * @return ARSTREAM2_ERROR_BAD_PARAMETERS if either the receiver or videoStats is invalid.
 */
eARSTREAM2_ERROR ARSTREAM2_RtpReceiver_UpdateVideoStats(ARSTREAM2_RtpReceiver_t *receiver, const ARSTREAM2_H264_VideoStats_t *videoStats);


/**
 * @brief Get the stream monitoring
 * The monitoring data is computed form the time startTime and back timeIntervalUs microseconds at most.
 * If startTime is 0 the start time is the current time.
 * If monitoring data is not available up to timeIntervalUs, the monitoring is computed on less time and the real interval is output to realTimeIntervalUs.
 * Pointers to monitoring parameters that are not required can be left NULL.
 *
 * @param[in] receiver The receiver instance
 * @param[in] startTime Monitoring start time in microseconds (0 means current time)
 * @param[in] timeIntervalUs Monitoring time interval (back from startTime) in microseconds
 * @param[out] realTimeIntervalUs Real monitoring time interval in microseconds (optional, can be NULL)
 * @param[out] receptionTimeJitter Network reception time jitter during realTimeIntervalUs in microseconds (optional, can be NULL)
 * @param[out] bytesReceived Bytes received during realTimeIntervalUs (optional, can be NULL)
 * @param[out] meanPacketSize Mean packet size during realTimeIntervalUs (optional, can be NULL)
 * @param[out] packetSizeStdDev Packet size standard deviation during realTimeIntervalUs (optional, can be NULL)
 * @param[out] packetsReceived Packets received during realTimeIntervalUs (optional, can be NULL)
 * @param[out] packetsMissed Packets missed during realTimeIntervalUs (optional, can be NULL)
 *
 * @return ARSTREAM2_OK if no error occured.
 * @return ARSTREAM2_ERROR_BAD_PARAMETERS if the receiver is invalid or if timeIntervalUs is 0.
 */
eARSTREAM2_ERROR ARSTREAM2_RtpReceiver_GetMonitoring(ARSTREAM2_RtpReceiver_t *receiver, uint64_t startTime, uint32_t timeIntervalUs, uint32_t *realTimeIntervalUs, uint32_t *receptionTimeJitter,
                                                     uint32_t *bytesReceived, uint32_t *meanPacketSize, uint32_t *packetSizeStdDev, uint32_t *packetsReceived, uint32_t *packetsMissed);


/**
 * @brief Creates a new RtpReceiver RtpResender
 * @warning This function allocates memory. The resender must be deleted by a call to ARSTREAM2_RtpReceiver_Delete() or ARSTREAM2_RtpReceiverResender_Delete()
 *
 * @param[in] receiver The receiver instance
 * @param[in] config Pointer to a resender configuration parameters structure
 * @param[out] error Optionnal pointer to an eARSTREAM2_ERROR to hold any error information
 *
 * @return A pointer to the new ARSTREAM2_RtpReceiver_RtpResender_t, or NULL if an error occured
 *
 * @see ARSTREAM2_RtpReceiverResender_Stop()
 * @see ARSTREAM2_RtpReceiverResender_Delete()
 */
ARSTREAM2_RtpReceiver_RtpResender_t* ARSTREAM2_RtpReceiverResender_New(ARSTREAM2_RtpReceiver_t *receiver, ARSTREAM2_RtpReceiver_RtpResender_Config_t *config, eARSTREAM2_ERROR *error);


/**
 * @brief Stops a running RtpReceiver RtpResender
 * @warning Once stopped, a resender cannot be restarted
 *
 * @param[in] resender The resender instance
 *
 * @note Calling this function multiple times has no effect
 */
void ARSTREAM2_RtpReceiverResender_Stop(ARSTREAM2_RtpReceiver_RtpResender_t *resender);


/**
 * @brief Deletes an RtpReceiver RtpResender
 * @warning This function should NOT be called on a running resender
 *
 * @param resender Pointer to the ARSTREAM2_RtpReceiver_RtpResender_t* to delete
 *
 * @return ARSTREAM2_OK if the resender was deleted
 * @return ARSTREAM2_ERROR_BUSY if the resender is still busy and can not be stopped now (probably because ARSTREAM2_RtpReceiverResender_Stop() has not been called yet)
 * @return ARSTREAM2_ERROR_BAD_PARAMETERS if resender does not point to a valid ARSTREAM2_RtpReceiver_RtpResender_t
 *
 * @note The function uses a double pointer, so it can set *resender to NULL after freeing it
 */
eARSTREAM2_ERROR ARSTREAM2_RtpReceiverResender_Delete(ARSTREAM2_RtpReceiver_RtpResender_t **resender);


/**
 * @brief Runs the main loop of the RtpReceiver RtpResender
 * @warning This function never returns until ARSTREAM2_RtpReceiverResender_Stop() is called. Thus, it should be called on its own thread.
 * @post Stop the resender by calling ARSTREAM2_RtpReceiverResender_Stop() before joining the thread calling this function.
 *
 * @param[in] ARSTREAM2_RtpReceiver_RtpResender_t_Param A valid (ARSTREAM2_RtpReceiver_RtpResender_t *) casted as a (void *)
 */
void* ARSTREAM2_RtpReceiverResender_RunThread(void *ARSTREAM2_RtpReceiver_RtpResender_t_Param);


#endif /* _ARSTREAM2_RTP_RECEIVER_H_ */
