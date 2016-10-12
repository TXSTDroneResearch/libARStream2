/**
 * @file arstream2_rtp_sender.h
 * @brief Parrot Streaming Library - RTP Sender
 * @date 04/17/2015
 * @author aurelien.barre@parrot.com
 */

#ifndef _ARSTREAM2_RTP_SENDER_H_
#define _ARSTREAM2_RTP_SENDER_H_

#ifdef __cplusplus
extern "C" {
#endif /* #ifdef __cplusplus */

#include <inttypes.h>
#include <libARStream2/arstream2_error.h>
#include <libARStream2/arstream2_stream_sender.h>


/**
 * @brief Default server-side stream port
 */
#define ARSTREAM2_RTP_SENDER_DEFAULT_SERVER_STREAM_PORT     (5004)


/**
 * @brief Default server-side control port
 */
#define ARSTREAM2_RTP_SENDER_DEFAULT_SERVER_CONTROL_PORT    (5005)


/**
 * @brief Default H.264 NAL unit FIFO size
 */
#define ARSTREAM2_RTP_SENDER_DEFAULT_NALU_FIFO_SIZE         (1024)


/**
 * @brief RtpSender configuration parameters
 */
typedef struct ARSTREAM2_RtpSender_Config_t
{
    const char *canonicalName;                      /**< RTP participant canonical name (CNAME SDES item) */
    const char *friendlyName;                       /**< RTP participant friendly name (NAME SDES item) (optional, can be NULL) */
    const char *applicationName;                    /**< RTP participant application name (TOOL SDES item) (optional, can be NULL) */
    const char *clientAddr;                         /**< Client address */
    const char *mcastAddr;                          /**< Multicast send address (optional, NULL for no multicast) */
    const char *mcastIfaceAddr;                     /**< Multicast output interface address (required if mcastAddr is not NULL) */
    int serverStreamPort;                           /**< Server stream port, @see ARSTREAM2_RTP_SENDER_DEFAULT_SERVER_STREAM_PORT */
    int serverControlPort;                          /**< Server control port, @see ARSTREAM2_RTP_SENDER_DEFAULT_SERVER_CONTROL_PORT */
    int clientStreamPort;                           /**< Client stream port */
    int clientControlPort;                          /**< Client control port */
    eARSAL_SOCKET_CLASS_SELECTOR classSelector;     /**< Type of Service class selector */
    int streamSocketBufferSize;                     /**< Send buffer size for the stream socket (optional, can be 0) */
    ARSTREAM2_StreamSender_AuCallback_t auCallback;       /**< Access unit callback function (optional, can be NULL) */
    void *auCallbackUserPtr;                        /**< Access unit callback function user pointer (optional, can be NULL) */
    ARSTREAM2_StreamSender_NaluCallback_t naluCallback;   /**< NAL unit callback function (optional, can be NULL) */
    void *naluCallbackUserPtr;                      /**< NAL unit callback function user pointer (optional, can be NULL) */
    ARSTREAM2_StreamSender_ReceiverReportCallback_t receiverReportCallback;   /**< NAL unit callback function (optional, can be NULL) */
    void *receiverReportCallbackUserPtr;            /**< NAL unit callback function user pointer (optional, can be NULL) */
    ARSTREAM2_StreamSender_DisconnectionCallback_t disconnectionCallback;     /**< Disconnection callback function (optional, can be NULL) */
    void *disconnectionCallbackUserPtr;             /**< Disconnection callback function user pointer (optional, can be NULL) */
    int naluFifoSize;                               /**< NAL unit FIFO size, @see ARSTREAM2_RTP_SENDER_DEFAULT_NALU_FIFO_SIZE */
    int maxPacketSize;                              /**< Maximum network packet size in bytes (example: the interface MTU) */
    int targetPacketSize;                           /**< Target network packet size in bytes */
    int maxBitrate;                                 /**< Maximum streaming bitrate in bit/s (optional, can be 0) */
    int maxLatencyMs;                               /**< Maximum acceptable total latency in milliseconds (optional, can be 0) */
    int maxNetworkLatencyMs[ARSTREAM2_STREAM_SENDER_MAX_IMPORTANCE_LEVELS];  /**< Maximum acceptable network latency in milliseconds for each NALU importance level */
    int useRtpHeaderExtensions;                     /**< Boolean-like (0-1) flag: if active insert access unit metadata as RTP header extensions */
    const char *dateAndTime;
    const char *debugPath;

} ARSTREAM2_RtpSender_Config_t;


/**
 * @brief RtpSender dynamic configuration parameters
 */
typedef struct ARSTREAM2_RtpSender_DynamicConfig_t
{
    int targetPacketSize;                           /**< Target network packet size in bytes */
    int streamSocketBufferSize;                     /**< Send buffer size for the stream socket (optional, can be 0) */
    int maxBitrate;                                 /**< Maximum streaming bitrate in bit/s (optional, can be 0) */
    int maxLatencyMs;                               /**< Maximum acceptable total latency in milliseconds (optional, can be 0) */
    int maxNetworkLatencyMs[ARSTREAM2_STREAM_SENDER_MAX_IMPORTANCE_LEVELS];  /**< Maximum acceptable network latency in milliseconds for each NALU importance level */

} ARSTREAM2_RtpSender_DynamicConfig_t;


/**
 * @brief An RtpSender instance to allow streaming H.264 video over a network
 */
typedef struct ARSTREAM2_RtpSender_t ARSTREAM2_RtpSender_t;


/**
 * @brief Creates a new RtpSender
 * @warning This function allocates memory. The sender must be deleted by a call to ARSTREAM2_RtpSender_Delete()
 *
 * @param[in] config Pointer to a configuration parameters structure
 * @param[out] error Optionnal pointer to an eARSTREAM2_ERROR to hold any error information
 *
 * @return A pointer to the new ARSTREAM2_RtpSender_t, or NULL if an error occured
 *
 * @see ARSTREAM2_RtpSender_Stop()
 * @see ARSTREAM2_RtpSender_Delete()
 */
ARSTREAM2_RtpSender_t* ARSTREAM2_RtpSender_New(const ARSTREAM2_RtpSender_Config_t *config, eARSTREAM2_ERROR *error);


/**
 * @brief Stops a running RtpSender
 * @warning Once stopped, a sender cannot be restarted
 *
 * @param[in] sender The sender instance
 *
 * @note Calling this function multiple times has no effect
 */
void ARSTREAM2_RtpSender_Stop(ARSTREAM2_RtpSender_t *sender);


/**
 * @brief Deletes an RtpSender
 * @warning This function should NOT be called on a running sender
 *
 * @param sender Pointer to the ARSTREAM2_RtpSender_t* to delete
 *
 * @return ARSTREAM2_OK if the sender was deleted
 * @return ARSTREAM2_ERROR_BUSY if the sender is still busy and can not be stopped now (probably because ARSTREAM2_RtpSender_Stop() has not been called yet)
 * @return ARSTREAM2_ERROR_BAD_PARAMETERS if sender does not point to a valid ARSTREAM2_RtpSender_t
 *
 * @note The function uses a double pointer, so it can set *sender to NULL after freeing it
 */
eARSTREAM2_ERROR ARSTREAM2_RtpSender_Delete(ARSTREAM2_RtpSender_t **sender);


/**
 * @brief Sends a new NAL unit
 * @warning The NAL unit buffer must remain available for the sender until the NAL unit or access unit callback functions are called.
 *
 * @param[in] sender The sender instance
 * @param[in] nalu Pointer to a NAL unit descriptor
 * @param[in] inputTime Optional input timestamp in microseconds
 *
 * @return ARSTREAM2_OK if no error happened
 * @return ARSTREAM2_ERROR_BAD_PARAMETERS if the sender, nalu or naluBuffer pointers are invalid, or if naluSize or auTimestamp is zero
 * @return ARSTREAM2_ERROR_QUEUE_FULL if the NAL unit FIFO is full
 */
eARSTREAM2_ERROR ARSTREAM2_RtpSender_SendNewNalu(ARSTREAM2_RtpSender_t *sender, const ARSTREAM2_StreamSender_H264NaluDesc_t *nalu, uint64_t inputTime);


/**
 * @brief Sends multiple new NAL units
 * @warning The NAL unit buffers must remain available for the sender until the NAL unit or access unit callback functions are called.
 *
 * @param[in] sender The sender instance
 * @param[in] nalu Pointer to a NAL unit descriptor array
 * @param[in] naluCount Number of NAL units in the array
 * @param[in] inputTime Optional input timestamp in microseconds
 *
 * @return ARSTREAM2_OK if no error happened
 * @return ARSTREAM2_ERROR_BAD_PARAMETERS if the sender, nalu or naluBuffer pointers are invalid, or if a naluSize or auTimestamp is zero
 * @return ARSTREAM2_ERROR_QUEUE_FULL if the NAL unit FIFO is full
 */
eARSTREAM2_ERROR ARSTREAM2_RtpSender_SendNNewNalu(ARSTREAM2_RtpSender_t *sender, const ARSTREAM2_StreamSender_H264NaluDesc_t *nalu, int naluCount, uint64_t inputTime);


/**
 * @brief Flush all currently queued NAL units
 *
 * @param[in] sender The sender instance
 *
 * @return ARSTREAM2_OK if no error occured.
 * @return ARSTREAM2_ERROR_BAD_PARAMETERS if the sender is invalid.
 */
eARSTREAM2_ERROR ARSTREAM2_RtpSender_FlushNaluQueue(ARSTREAM2_RtpSender_t *sender);


/**
 * @brief Runs the main loop of the RtpSender
 * @warning This function never returns until ARSTREAM2_RtpSender_Stop() is called. Thus, it should be called on its own thread.
 * @post Stop the Sender by calling ARSTREAM2_RtpSender_Stop() before joining the thread calling this function.
 *
 * @param[in] ARSTREAM2_RtpSender_t_Param A valid (ARSTREAM2_RtpSender_t *) casted as a (void *)
 */
void* ARSTREAM2_RtpSender_RunThread(void *ARSTREAM2_RtpSender_t_Param);


/**
 * @brief Get the current dynamic configuration parameters
 *
 * @param[in] sender The sender instance
 * @param[out] config Pointer to a dynamic config structure to fill
 *
 * @return ARSTREAM2_OK if no error happened
 * @return ARSTREAM2_ERROR_BAD_PARAMETERS if the sender or config pointers are invalid
 */
eARSTREAM2_ERROR ARSTREAM2_RtpSender_GetDynamicConfig(ARSTREAM2_RtpSender_t *sender, ARSTREAM2_RtpSender_DynamicConfig_t *config);


/**
 * @brief Set the current dynamic configuration parameters
 *
 * @param[in] sender The sender instance
 * @param[in] config Pointer to a dynamic config structure
 *
 * @return ARSTREAM2_OK if no error happened
 * @return ARSTREAM2_ERROR_BAD_PARAMETERS if the sender or config pointers are invalid
 */
eARSTREAM2_ERROR ARSTREAM2_RtpSender_SetDynamicConfig(ARSTREAM2_RtpSender_t *sender, const ARSTREAM2_RtpSender_DynamicConfig_t *config);


/**
 * @brief Get the stream monitoring
 * The monitoring data is computed form the time startTime and back timeIntervalUs microseconds at most.
 * If startTime is 0 the start time is the current time.
 * If monitoring data is not available up to timeIntervalUs, the monitoring is computed on less time and the real interval is output to realTimeIntervalUs.
 * Pointers to monitoring parameters that are not required can be left NULL.
 *
 * @param[in] sender The sender instance
 * @param[in] startTime Monitoring start time in microseconds (0 means current time)
 * @param[in] timeIntervalUs Monitoring time interval (back from startTime) in microseconds
 * @param[out] monitoringData Pointer to a monitoring data structure to fill
 *
 * @return ARSTREAM2_OK if no error occured.
 * @return ARSTREAM2_ERROR_BAD_PARAMETERS if the sender is invalid or if timeIntervalUs is 0.
 */
eARSTREAM2_ERROR ARSTREAM2_RtpSender_GetMonitoring(ARSTREAM2_RtpSender_t *sender, uint64_t startTime, uint32_t timeIntervalUs,
                                                   ARSTREAM2_StreamSender_MonitoringData_t *monitoringData);


#ifdef __cplusplus
}
#endif /* #ifdef __cplusplus */

#endif /* _ARSTREAM2_RTP_SENDER_H_ */
