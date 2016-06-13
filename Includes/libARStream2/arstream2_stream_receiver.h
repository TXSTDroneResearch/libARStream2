/**
 * @file arstream2_stream_receiver.h
 * @brief Parrot Streaming Library - Stream Receiver
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#ifndef _ARSTREAM2_STREAM_RECEIVER_H_
#define _ARSTREAM2_STREAM_RECEIVER_H_

#ifdef __cplusplus
extern "C" {
#endif /* #ifdef __cplusplus */

#include <inttypes.h>
#include <libARStream2/arstream2_error.h>
#include <libARSAL/ARSAL_Socket.h>


/**
 * @brief Default client-side stream port
 */
#define ARSTREAM2_STREAM_RECEIVER_DEFAULT_CLIENT_STREAM_PORT      (55004)

/**
 * @brief Default client-side control port
 */
#define ARSTREAM2_STREAM_RECEIVER_DEFAULT_CLIENT_CONTROL_PORT     (55005)

/**
 * @brief Default server-side resender stream port
 */
#define ARSTREAM2_STREAM_RECEIVER_RESENDER_DEFAULT_SERVER_STREAM_PORT      (5004)

/**
 * @brief Default server-side resender control port
 */
#define ARSTREAM2_STREAM_RECEIVER_RESENDER_DEFAULT_SERVER_CONTROL_PORT     (5005)


/**
 * @brief ARSTREAM2 StreamReceiver instance handle.
 */
typedef void* ARSTREAM2_StreamReceiver_Handle;


/**
 * @brief ARSTREAM2 StreamReceiver resender handle.
 */
typedef void* ARSTREAM2_StreamReceiver_ResenderHandle;


/**
 * @brief AU synchronization type.
 */
typedef enum
{
    ARSTREAM2_STREAM_RECEIVER_AU_SYNC_TYPE_NONE = 0,    /**< The Access Unit is not a synchronization point */
    ARSTREAM2_STREAM_RECEIVER_AU_SYNC_TYPE_IDR,         /**< The Access Unit is an IDR picture */
    ARSTREAM2_STREAM_RECEIVER_AU_SYNC_TYPE_IFRAME,      /**< The Access Unit is an I-frame */
    ARSTREAM2_STREAM_RECEIVER_AU_SYNC_TYPE_PIR_START,   /**< The Access Unit is a Periodic Intra Refresh start */
    ARSTREAM2_STREAM_RECEIVER_AU_SYNC_TYPE_MAX,

} eARSTREAM2_STREAM_RECEIVER_AU_SYNC_TYPE;


/**
 * @brief Macroblock status.
 */
typedef enum
{
    ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_UNKNOWN = 0,        /**< The macroblock status is unknown */
    ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_VALID_ISLICE,       /**< The macroblock is valid and contained in an I-slice */
    ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_VALID_PSLICE,       /**< The macroblock is valid and contained in a P-slice */
    ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_MISSING_CONCEALED,  /**< The macroblock is missing and concealed */
    ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_MISSING,            /**< The macroblock is missing and not concealed */
    ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_ERROR_PROPAGATION,  /**< The macroblock is valid but within an error propagation */
    ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_MAX,

} eARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS;


/**
 * @brief SPS/PPS NAL units callback function
 *
 * To be used with the application output feature.
 * The optional SPS/PPS callback function is called when SPS/PPS are found in the stream.
 *
 * @param spsBuffer Pointer to the SPS NAL unit buffer
 * @param spsSize Size in bytes of the SPS NAL unit
 * @param ppsBuffer Pointer to the PPS NAL unit buffer
 * @param ppsSize Size in bytes of the PPS NAL unit
 * @param userPtr SPS/PPS callback user pointer
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return an eARSTREAM2_ERROR error code if an error occurred.
 *
 * @note This callback function is optional.
 *
 * @warning ARSTREAM2_StreamReceiver_* functions must not be called within the callback function.
 */
typedef eARSTREAM2_ERROR (*ARSTREAM2_StreamReceiver_SpsPpsCallback_t)(const uint8_t *spsBuffer, int spsSize, const uint8_t *ppsBuffer, int ppsSize, void *userPtr);


/**
 * @brief Get access unit buffer callback function
 *
 * To be used with the application output feature.
 * The mandatory get AU buffer callback function is called to retreive a buffer to fill with an access unit.
 *
 * @param auBuffer Pointer to the AU buffer pointer
 * @param auBufferSize Pointer to the AU buffer size in bytes
 * @param auBufferUserPtr Pointer to the AU buffer user pointer
 * @param userPtr Get AU buffer callback user pointer
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return ARSTREAM2_ERROR_RESOURCE_UNAVAILABLE if no buffers are available.
 * @return an eARSTREAM2_ERROR error code if another error occurred.
 *
 * @warning This callback function is mandatory.
 * @warning ARSTREAM2_StreamReceiver_* functions must not be called within the callback function.
 */
typedef eARSTREAM2_ERROR (*ARSTREAM2_StreamReceiver_GetAuBufferCallback_t)(uint8_t **auBuffer, int *auBufferSize, void **auBufferUserPtr, void *userPtr);


/**
 * @brief Access unit ready callback function
 *
 * To be used with the application output feature.
 * The mandatory AU ready callback function is called to output an access unit.
 *
 * @param auBuffer Pointer to the AU buffer
 * @param auSize AU size in bytes
 * @param auExtRtpTimestamp Access unit extended RTP timestamp (90000 Hz clock)
 * @param auNtpTimestamp Access unit NTP timestamp (microseconds) in the sender's clock reference (0 if RTCP is not available)
 * @param auNtpTimestampLocal Access unit NTP timestamp (microseconds) in the local clock reference (0 if clock sync or RTCP is not available)
 * @param auSyncType AU synchronization type
 * @param auMetadata AU metadata buffer
 * @param auMetadataSize AU metadata size in bytes
 * @param auUserData AU user data SEI buffer
 * @param auUserDataSize AU user data SEI size in bytes
 * @param auBufferUserPtr AU buffer user pointer
 * @param userPtr AU readey callback user pointer
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return ARSTREAM2_ERROR_RESYNC_REQUIRED if a decoding error occurred and re-sync is needed.
 * @return an eARSTREAM2_ERROR error code if another error occurred.
 *
 * @warning This callback function is mandatory.
 * @warning ARSTREAM2_StreamReceiver_* functions must not be called within the callback function
 * except the ARSTREAM2_StreamReceiver_GetFrameMacroblockStatus() function.
 */
typedef eARSTREAM2_ERROR (*ARSTREAM2_StreamReceiver_AuReadyCallback_t)(uint8_t *auBuffer, int auSize, uint64_t auExtRtpTimestamp,
                                                                   uint64_t auNtpTimestamp, uint64_t auNtpTimestampLocal,
                                                                   eARSTREAM2_STREAM_RECEIVER_AU_SYNC_TYPE auSyncType,
                                                                   const void *auMetadata, int auMetadataSize,
                                                                   const void *auUserData, int auUserDataSize,
                                                                   void *auBufferUserPtr, void *userPtr);


/**
 * @brief ARSTREAM2 StreamReceiver net configuration for initialization.
 */
typedef struct
{
    const char *serverAddr;                                         /**< Server address */
    const char *mcastAddr;                                          /**< Multicast receive address (optional, NULL for no multicast) */
    const char *mcastIfaceAddr;                                     /**< Multicast input interface address (required if mcastAddr is not NULL) */
    int serverStreamPort;                                           /**< Server stream port, @see ARSTREAM2_STREAM_RECEIVER_DEFAULT_CLIENT_STREAM_PORT */
    int serverControlPort;                                          /**< Server control port, @see ARSTREAM2_STREAM_RECEIVER_DEFAULT_CLIENT_CONTROL_PORT */
    int clientStreamPort;                                           /**< Client stream port */
    int clientControlPort;                                          /**< Client control port */
    eARSAL_SOCKET_CLASS_SELECTOR classSelector;                     /**< Type of Service class selector */

} ARSTREAM2_StreamReceiver_NetConfig_t;


/* Forward declaration of the mux_ctx structure */
struct mux_ctx;


/**
 * @brief ARSTREAM2 StreamReceiver mux configuration for initialization.
 */
typedef struct
{
    struct mux_ctx *mux;                                            /**< libmux context */

} ARSTREAM2_StreamReceiver_MuxConfig_t;


/**
 * @brief ARSTREAM2 StreamReceiver configuration for initialization.
 */
typedef struct
{
    const char *canonicalName;                                      /**< RTP participant canonical name (CNAME SDES item) */
    const char *friendlyName;                                       /**< RTP participant friendly name (NAME SDES item) (optional, can be NULL) */
    int maxPacketSize;                                              /**< Maximum network packet size in bytes (should be provided by the server, if 0 the maximum UDP packet size is used) */
    int maxBitrate;                                                 /**< Maximum streaming bitrate in bit/s (should be provided by the server, can be 0) */
    int maxLatencyMs;                                               /**< Maximum acceptable total latency in milliseconds (should be provided by the server, can be 0) */
    int maxNetworkLatencyMs;                                        /**< Maximum acceptable network latency in milliseconds (should be provided by the server, can be 0) */
    int generateReceiverReports;                                    /**< if true, generate RTCP receiver reports */
    int waitForSync;                                                /**< if true, wait for SPS/PPS sync before outputting access anits */
    int outputIncompleteAu;                                         /**< if true, output incomplete access units */
    int filterOutSpsPps;                                            /**< if true, filter out SPS and PPS NAL units */
    int filterOutSei;                                               /**< if true, filter out SEI NAL units */
    int replaceStartCodesWithNaluSize;                              /**< if true, replace the NAL units start code with the NALU size */
    int generateSkippedPSlices;                                     /**< if true, generate skipped P slices to replace missing slices */
    int generateFirstGrayIFrame;                                    /**< if true, generate a first gray I frame to initialize the decoding (waitForSync must be enabled) */

} ARSTREAM2_StreamReceiver_Config_t;


/**
 * @brief ARSTREAM2 StreamReceiver resender configuration parameters.
 */
typedef struct ARSTREAM2_StreamReceiver_ResenderConfig_t
{
    const char *clientAddr;                         /**< Client address */
    const char *mcastAddr;                          /**< Multicast send address (optional, NULL for no multicast) */
    const char *mcastIfaceAddr;                     /**< Multicast output interface address (required if mcastAddr is not NULL) */
    int serverStreamPort;                           /**< Server stream port, @see ARSTREAM2_STREAM_RECEIVER_RESENDER_DEFAULT_SERVER_STREAM_PORT */
    int serverControlPort;                          /**< Server control port, @see ARSTREAM2_STREAM_RECEIVER_RESENDER_DEFAULT_SERVER_CONTROL_PORT */
    int clientStreamPort;                           /**< Client stream port */
    int clientControlPort;                          /**< Client control port */
    int maxPacketSize;                              /**< Maximum network packet size in bytes (example: the interface MTU) */
    int targetPacketSize;                           /**< Target network packet size in bytes */
    int streamSocketBufferSize;                     /**< Send buffer size for the stream socket (optional, can be 0) */
    int maxLatencyMs;                               /**< Maximum acceptable total latency in milliseconds (optional, can be 0) */
    int maxNetworkLatencyMs;                        /**< Maximum acceptable network latency in milliseconds */
    int useRtpHeaderExtensions;                     /**< Boolean-like (0-1) flag: if active insert access unit metadata as RTP header extensions */

} ARSTREAM2_StreamReceiver_ResenderConfig_t;


/**
 * @brief Initialize a StreamReceiver instance.
 *
 * The library allocates the required resources. The user must call ARSTREAM2_StreamReceiver_Free() to free the resources.
 *
 * @param streamReceiverHandle Pointer to the handle used in future calls to the library.
 * @param config The instance configuration.
 * @param net_config The instance network configuration, or NULL if libmux is used.
 * @param mux_config The instance libmux configuration, or NULL if network is used.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return an eARSTREAM2_ERROR error code if an error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_Init(ARSTREAM2_StreamReceiver_Handle *streamReceiverHandle,
                                               const ARSTREAM2_StreamReceiver_Config_t *config,
                                               const ARSTREAM2_StreamReceiver_NetConfig_t *net_config,
                                               const ARSTREAM2_StreamReceiver_MuxConfig_t *mux_config);


/**
 * @brief Stop a StreamReceiver instance.
 *
 * The function ends the threads before they can be joined.
 *
 * @param streamReceiverHandle Instance handle.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return an eARSTREAM2_ERROR error code if an error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_Stop(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle);


/**
 * @brief Free a StreamReceiver instance.
 *
 * The library frees the allocated resources. On success the streamReceiverHandle is set to NULL.
 *
 * @param streamReceiverHandle Pointer to the instance handle.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return an eARSTREAM2_ERROR error code if an error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_Free(ARSTREAM2_StreamReceiver_Handle *streamReceiverHandle);


/**
 * @brief Run a StreamReceiver stream thread.
 *
 * The instance must be correctly allocated using ARSTREAM2_StreamReceiver_Init().
 * @warning This function never returns until ARSTREAM2_StreamReceiver_Stop() is called. The tread can then be joined.
 *
 * @param streamReceiverHandle Instance handle casted as (void*).
 *
 * @return NULL in all cases.
 */
void* ARSTREAM2_StreamReceiver_RunNetworkThread(void *streamReceiverHandle);


/**
 * @brief Start the application output.
 *
 * The function starts the output to the application though callback functions.
 * The processing can be stopped using ARSTREAM2_StreamReceiver_StopAppOutput().
 *
 * @param streamReceiverHandle Instance handle.
 * @param spsPpsCallback SPS/PPS callback function.
 * @param spsPpsCallbackUserPtr SPS/PPS callback user pointer.
 * @param getAuBufferCallback Get access unit buffer callback function.
 * @param getAuBufferCallbackUserPtr Get access unit buffer callback user pointer.
 * @param auReadyCallback Access unit ready callback function.
 * @param auReadyCallbackUserPtr Access unit ready callback user pointer.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return an eARSTREAM2_ERROR error code if an error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_StartAppOutput(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle,
                                                         ARSTREAM2_StreamReceiver_SpsPpsCallback_t spsPpsCallback, void* spsPpsCallbackUserPtr,
                                                         ARSTREAM2_StreamReceiver_GetAuBufferCallback_t getAuBufferCallback, void* getAuBufferCallbackUserPtr,
                                                         ARSTREAM2_StreamReceiver_AuReadyCallback_t auReadyCallback, void* auReadyCallbackUserPtr);


/**
 * @brief Stop the applicaiton output.
 *
 * The function stops the output to the application.
 * The callback functions provided to ARSTREAM2_StreamReceiver_StartAppOutput() will not be called any more.
 * The filter can be started again by a new call to ARSTREAM2_StreamReceiver_StartAppOutput().
 *
 * @param streamReceiverHandle Instance handle.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return an eARSTREAM2_ERROR error code if an error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_StopAppOutput(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle);


/**
 * @brief Run a StreamReceiver application output thread.
 *
 * The instance must be correctly allocated using ARSTREAM2_StreamReceiver_Init().
 * @warning This function never returns until ARSTREAM2_StreamReceiver_Stop() is called. The tread can then be joined.
 *
 * @param streamReceiverHandle Instance handle casted as (void*).
 *
 * @return NULL in all cases.
 */
void* ARSTREAM2_StreamReceiver_RunAppOutputThread(void *streamReceiverHandle);


/**
 * @brief Start a stream recorder.
 *
 * The function starts recording the received stream to a file.
 * The recording can be stopped using ARSTREAM2_StreamReceiver_StopRecording().
 * The filter must be previously started using ARSTREAM2_StreamReceiver_StartAppOutput().
 * @note Only one recording can be done at a time.
 *
 * @param streamReceiverHandle Instance handle.
 * @param recordFileName Record file absolute path.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return an eARSTREAM2_ERROR error code if an error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_StartRecorder(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle, const char *recordFileName);


/**
 * @brief Stop a stream recorder.
 *
 * The function stops the current recording.
 * If no recording is in progress nothing happens.
 *
 * @param streamReceiverHandle Instance handle.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return an eARSTREAM2_ERROR error code if an error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_StopRecorder(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle);


/**
 * @brief Initialize a new resender.
 *
 * The library allocates the required resources. The user must call ARSTREAM2_StreamReceiver_Free()
 * or ARSTREAM2_StreamReceiver_FreeResender() to free the resources.
 *
 * @param streamReceiverHandle StreamReceiver instance handle.
 * @param resenderHandle Pointer to the resender handle used in future calls to the library.
 * @param config The resender configuration.
 *
 * @return 0 if no error occurred.
 * @return an eARSTREAM2_ERROR error code if an error occurred.
 *
 * @see ARSTREAM2_StreamReceiver_StopResender()
 * @see ARSTREAM2_StreamReceiver_FreeResender()
 */
eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_StartResender(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle,
                                                        ARSTREAM2_StreamReceiver_ResenderHandle *resenderHandle,
                                                        const ARSTREAM2_StreamReceiver_ResenderConfig_t *config);


/**
 * @brief Stop a resender.
 *
 * The function ends the resender threads before they can be joined.
 *
 * @param resenderHandle Resender handle.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return an eARSTREAM2_ERROR error code if an error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_StopResender(ARSTREAM2_StreamReceiver_ResenderHandle resenderHandle);


/**
 * @brief Free a resender.
 *
 * The library frees the allocated resources. On success the resenderHandle is set to NULL.
 *
 * @param resenderHandle Pointer to the resender handle.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return an eARSTREAM2_ERROR error code if an error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_FreeResender(ARSTREAM2_StreamReceiver_ResenderHandle *resenderHandle);


/**
 * @brief Run a resender thread.
 *
 * The resender must be correctly allocated using ARSTREAM2_StreamReceiver_InitResender().
 * @warning This function never returns until ARSTREAM2_StreamReceiver_StopResender() is called. The tread can then be joined.
 *
 * @param resenderHandle Resender handle casted as (void*).
 *
 * @return NULL in all cases.
 */
void* ARSTREAM2_StreamReceiver_RunResenderThread(void *resenderHandle);


/**
 * @brief Get the SPS and PPS buffers.
 *
 * The buffers are filled by the function and must be provided by the user. The size of the buffers are given
 * by a first call to the function with both buffer pointers null.
 * When the buffer pointers are not null the size pointers must point to the values of the user-allocated buffer sizes.
 *
 * @param streamReceiverHandle Instance handle.
 * @param spsBuffer SPS buffer pointer.
 * @param spsSize pointer to the SPS size.
 * @param ppsBuffer PPS buffer pointer.
 * @param ppsSize pointer to the PPS size.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return ARSTREAM2_ERROR_BAD_PARAMETERS if arguments are invalid.
 * @return ARSTREAM2_ERROR_WAITING_FOR_SYNC if SPS/PPS are not available (no sync).
 */
eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_GetSpsPps(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle, uint8_t *spsBuffer, int *spsSize, uint8_t *ppsBuffer, int *ppsSize);


/**
 * @brief Get the frame macroblocks status.
 *
 * This function returns pointers to a macroblock status array for the current frame and image
 * macroblock width and height.
 * Macroblock statuses are of type eARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS.
 * This function must be called only within the ARSTREAM2_StreamReceiver_AuReadyCallback_t function.
 * The valididy of the data returned is only during the call to ARSTREAM2_StreamReceiver_AuReadyCallback_t
 * and the user must copy the macroblock status array to its own buffer for further use.
 *
 * @param streamReceiverHandle Instance handle.
 * @param macroblocks Pointer to the macroblock status array.
 * @param mbWidth pointer to the image macroblock-width.
 * @param mbHeight pointer to the image macroblock-height.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return ARSTREAM2_ERROR_WAITING_FOR_SYNC if SPS/PPS have not been received (no sync).
 * @return ARSTREAM2_ERROR_RESOURCE_UNAVAILABLE if macroblocks status is not available.
 * @return an eARSTREAM2_ERROR error code if another error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_GetFrameMacroblockStatus(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle, uint8_t **macroblocks, int *mbWidth, int *mbHeight);


#ifdef __cplusplus
}
#endif /* #ifdef __cplusplus */

#endif /* #ifndef _ARSTREAM2_STREAM_RECEIVER_H_ */
