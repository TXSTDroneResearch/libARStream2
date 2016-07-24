/**
 * @file arstream2_rtp_receiver.h
 * @brief Parrot Streaming Library - RTP Receiver
 * @date 04/16/2015
 * @author aurelien.barre@parrot.com
 */

#ifndef _ARSTREAM2_RTP_RECEIVER_H_
#define _ARSTREAM2_RTP_RECEIVER_H_

#ifdef __cplusplus
extern "C" {
#endif /* #ifdef __cplusplus */

#include <inttypes.h>
#include <libARStream2/arstream2_error.h>


/**
 * @brief Default client-side stream port
 */
#define ARSTREAM2_RTP_RECEIVER_DEFAULT_CLIENT_STREAM_PORT     (55004)


/**
 * @brief Default client-side control port
 */
#define ARSTREAM2_RTP_RECEIVER_DEFAULT_CLIENT_CONTROL_PORT    (55005)


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
    int filterPipe[2];                              /**< Filter signaling pipe file desciptors */
    int maxPacketSize;                              /**< Maximum network packet size in bytes (should be provided by the server, if 0 the maximum UDP packet size is used) */
    int maxBitrate;                                 /**< Maximum streaming bitrate in bit/s (should be provided by the server, can be 0) */
    int maxLatencyMs;                               /**< Maximum acceptable total latency in milliseconds (should be provided by the server, can be 0) */
    int maxNetworkLatencyMs;                        /**< Maximum acceptable network latency in milliseconds (should be provided by the server, can be 0) */
    int insertStartCodes;                           /**< Boolean-like (0-1) flag: if active insert a start code prefix before NAL units */
    int generateReceiverReports;                    /**< Boolean-like (0-1) flag: if active generate RTCP receiver reports */
} ARSTREAM2_RtpReceiver_Config_t;


/**
 * @brief RtpReceiver RtpResender configuration parameters
 */
typedef struct ARSTREAM2_RtpReceiver_RtpResender_Config_t
{
    const char *clientAddr;                         /**< Client address */
    const char *mcastAddr;                          /**< Multicast send address (optional, NULL for no multicast) */
    const char *mcastIfaceAddr;                     /**< Multicast output interface address (required if mcastAddr is not NULL) */
    int serverStreamPort;                           /**< Server stream port, @see ARSTREAM2_RTP_SENDER_DEFAULT_SERVER_STREAM_PORT */
    int serverControlPort;                          /**< Server control port, @see ARSTREAM2_RTP_SENDER_DEFAULT_SERVER_CONTROL_PORT */
    int clientStreamPort;                           /**< Client stream port */
    int clientControlPort;                          /**< Client control port */
    int maxPacketSize;                              /**< Maximum network packet size in bytes (example: the interface MTU) */
    int targetPacketSize;                           /**< Target network packet size in bytes */
    int streamSocketBufferSize;                     /**< Send buffer size for the stream socket (optional, can be 0) */
    int maxLatencyMs;                               /**< Maximum acceptable total latency in milliseconds (optional, can be 0) */
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
 * @brief Get the receiver shared context
 *
 * @param[in] receiver The receiver instance
 * @param[out] auFifo Pointer to the access unit FIFO
 * @param[out] naluFifo Pointer to the NAL unit FIFO
 * @param[out] mutex Pointer to the mutex
 *
 * @return ARSTREAM2_OK if no error occured.
 * @return ARSTREAM2_ERROR_BAD_PARAMETERS if the receiver is invalid or if timeIntervalUs is 0.
 */
eARSTREAM2_ERROR ARSTREAM2_RtpReceiver_GetSharedContext(ARSTREAM2_RtpReceiver_t *receiver, void **auFifo, void **naluFifo, void **mutex);


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
 * @warning This function allocates memory. The resender must be deleted by a call to ARSTREAM2_RtpReceiver_Delete() or ARSTREAM2_RtpReceiver_RtpResender_Delete()
 *
 * @param[in] receiver The receiver instance
 * @param[in] config Pointer to a resender configuration parameters structure
 * @param[out] error Optionnal pointer to an eARSTREAM2_ERROR to hold any error information
 *
 * @return A pointer to the new ARSTREAM2_RtpReceiver_RtpResender_t, or NULL if an error occured
 *
 * @see ARSTREAM2_RtpReceiver_RtpResender_Stop()
 * @see ARSTREAM2_RtpReceiver_RtpResender_Delete()
 */
ARSTREAM2_RtpReceiver_RtpResender_t* ARSTREAM2_RtpReceiver_RtpResender_New(ARSTREAM2_RtpReceiver_t *receiver, ARSTREAM2_RtpReceiver_RtpResender_Config_t *config, eARSTREAM2_ERROR *error);


/**
 * @brief Stops a running RtpReceiver RtpResender
 * @warning Once stopped, a resender cannot be restarted
 *
 * @param[in] resender The resender instance
 *
 * @note Calling this function multiple times has no effect
 */
void ARSTREAM2_RtpReceiver_RtpResender_Stop(ARSTREAM2_RtpReceiver_RtpResender_t *resender);


/**
 * @brief Deletes an RtpReceiver RtpResender
 * @warning This function should NOT be called on a running resender
 *
 * @param resender Pointer to the ARSTREAM2_RtpReceiver_RtpResender_t* to delete
 *
 * @return ARSTREAM2_OK if the resender was deleted
 * @return ARSTREAM2_ERROR_BUSY if the resender is still busy and can not be stopped now (probably because ARSTREAM2_RtpReceiver_RtpResender_Stop() has not been called yet)
 * @return ARSTREAM2_ERROR_BAD_PARAMETERS if resender does not point to a valid ARSTREAM2_RtpReceiver_RtpResender_t
 *
 * @note The function uses a double pointer, so it can set *resender to NULL after freeing it
 */
eARSTREAM2_ERROR ARSTREAM2_RtpReceiver_RtpResender_Delete(ARSTREAM2_RtpReceiver_RtpResender_t **resender);


/**
 * @brief Runs the main loop of the RtpReceiver RtpResender
 * @warning This function never returns until ARSTREAM2_RtpReceiver_RtpResender_Stop() is called. Thus, it should be called on its own thread.
 * @post Stop the resender by calling ARSTREAM2_RtpReceiver_RtpResender_Stop() before joining the thread calling this function.
 *
 * @param[in] ARSTREAM2_RtpReceiver_RtpResender_t_Param A valid (ARSTREAM2_RtpReceiver_RtpResender_t *) casted as a (void *)
 */
void* ARSTREAM2_RtpReceiver_RtpResender_RunThread(void *ARSTREAM2_RtpReceiver_RtpResender_t_Param);


#ifdef __cplusplus
}
#endif /* #ifdef __cplusplus */

#endif /* _ARSTREAM2_RTP_RECEIVER_H_ */
