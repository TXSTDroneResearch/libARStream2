/**
 * @file beaver_readerfilter.h
 * @brief H.264 Elementary Stream Reader and Filter
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#ifndef _BEAVER_READERFILTER_H_
#define _BEAVER_READERFILTER_H_

#ifdef __cplusplus
extern "C" {
#endif /* #ifdef __cplusplus */

#include <inttypes.h>
#include <libARStream/ARSTREAM_Reader2.h>
#include "beaver_filter.h"


/**
 * @brief Default client-side stream port
 */
#define BEAVER_READERFILTER_DEFAULT_CLIENT_STREAM_PORT      ARSTREAM_READER2_DEFAULT_CLIENT_STREAM_PORT

/**
 * @brief Default client-side control port
 */
#define BEAVER_READERFILTER_DEFAULT_CLIENT_CONTROL_PORT     ARSTREAM_READER2_DEFAULT_CLIENT_CONTROL_PORT


/**
 * @brief Beaver ReaderFilter instance handle.
 */
typedef void* BEAVER_ReaderFilter_Handle;


/**
 * @brief Beaver ReaderFilter configuration for initialization.
 */
typedef struct
{
    const char *serverAddr;                                         /**< Server address */
    const char *mcastAddr;                                          /**< Multicast receive address (optional, NULL for no multicast) */
    const char *mcastIfaceAddr;                                     /**< Multicast input interface address (required if mcastAddr is not NULL) */
    int serverStreamPort;                                           /**< Server stream port, @see BEAVER_READERFILTER_DEFAULT_CLIENT_STREAM_PORT */
    int serverControlPort;                                          /**< Server control port, @see BEAVER_READERFILTER_DEFAULT_CLIENT_CONTROL_PORT */
    int clientStreamPort;                                           /**< Client stream port */
    int clientControlPort;                                          /**< Client control port */
    int maxPacketSize;                                              /**< Maximum network packet size in bytes (should be provided by the server, if 0 the maximum UDP packet size is used) */
    int maxBitrate;                                                 /**< Maximum streaming bitrate in bit/s (should be provided by the server, can be 0) */
    int maxLatencyMs;                                               /**< Maximum acceptable total latency in milliseconds (should be provided by the server, can be 0) */
    int maxNetworkLatencyMs;                                        /**< Maximum acceptable network latency in milliseconds (should be provided by the server, can be 0) */
    BEAVER_Filter_SpsPpsCallback_t spsPpsCallback;                  /**< SPS/PPS callback */
    void* spsPpsCallbackUserPtr;                                    /**< SPS/PPS callback user pointer */
    BEAVER_Filter_GetAuBufferCallback_t getAuBufferCallback;        /**< get access unit buffer callback */
    void* getAuBufferCallbackUserPtr;                               /**< get access unit buffer callback user pointer */
    BEAVER_Filter_CancelAuBufferCallback_t cancelAuBufferCallback;  /**< cancel access unit buffer callback */
    void* cancelAuBufferCallbackUserPtr;                            /**< cancel access unit buffer callback user pointer */
    BEAVER_Filter_AuReadyCallback_t auReadyCallback;                /**< access unit ready callback */
    void* auReadyCallbackUserPtr;                                   /**< access unit ready callback user pointer */
    int auFifoSize;                                                 /**< access unit FIFO size (should match the number of decoder buffers - 2 */
    int waitForSync;                                                /**< if true, wait for SPS/PPS sync before outputting access anits */
    int outputIncompleteAu;                                         /**< if true, output incomplete access units */
    int filterOutSpsPps;                                            /**< if true, filter out SPS and PPS NAL units */
    int filterOutSei;                                               /**< if true, filter out SEI NAL units */
    int replaceStartCodesWithNaluSize;                              /**< if true, replace the NAL units start code with the NALU size */

} BEAVER_ReaderFilter_Config_t;


/**
 * @brief Initialize a Beaver ReaderFilter instance.
 *
 * The library allocates the required resources. The user must call BEAVER_ReaderFilter_Free() to free the resources.
 *
 * @param readerFilterHandle Pointer to the handle used in future calls to the library.
 * @param config The instance configuration.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_ReaderFilter_Init(BEAVER_ReaderFilter_Handle *readerFilterHandle, BEAVER_ReaderFilter_Config_t *config);


/**
 * @brief Free a Beaver ReaderFilter instance.
 *
 * The library frees the allocated resources. On success the readerFilterHandle is set to NULL.
 *
 * @param readerFilterHandle Pointer to the instance handle.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_ReaderFilter_Free(BEAVER_ReaderFilter_Handle *readerFilterHandle);


/**
 * @brief Run a Beaver ReaderFilter filter thread.
 *
 * The instance must be correctly allocated using BEAVER_ReaderFilter_Init().
 * @warning This function never returns until BEAVER_ReaderFilter_Stop() is called. The tread can then be joined.
 *
 * @param readerFilterHandle Instance handle casted as (void*).
 *
 * @return NULL in all cases.
 */
void* BEAVER_ReaderFilter_RunFilterThread(void *readerFilterHandle);


/**
 * @brief Run a Beaver ReaderFilter stream thread.
 *
 * The instance must be correctly allocated using BEAVER_ReaderFilter_Init().
 * @warning This function never returns until BEAVER_ReaderFilter_Stop() is called. The tread can then be joined.
 *
 * @param readerFilterHandle Instance handle casted as (void*).
 *
 * @return NULL in all cases.
 */
void* BEAVER_ReaderFilter_RunStreamThread(void *readerFilterHandle);


/**
 * @brief Run a Beaver ReaderFilter control thread.
 *
 * The instance must be correctly allocated using BEAVER_ReaderFilter_Init().
 * @warning This function never returns until BEAVER_ReaderFilter_Stop() is called. The tread can then be joined.
 *
 * @param readerFilterHandle Instance handle casted as (void*).
 *
 * @return NULL in all cases.
 */
void* BEAVER_ReaderFilter_RunControlThread(void *readerFilterHandle);


/**
 * @brief Stop a Beaver ReaderFilter instance.
 *
 * The function ends the threads before they can be joined.
 *
 * @param readerFilterHandle Instance handle.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_ReaderFilter_Stop(BEAVER_ReaderFilter_Handle readerFilterHandle);


/**
 * @brief Get the SPS and PPS buffers.
 *
 * The buffers are filled by the function and must be provided by the user. The size of the buffers are given
 * by a first call to the function with both buffer pointers null.
 * When the buffer pointers are not null the size pointers must point to the values of the user-allocated buffer sizes.
 *
 * @param readerFilterHandle Instance handle.
 * @param spsBuffer SPS buffer pointer.
 * @param spsSize pointer to the SPS size.
 * @param ppsBuffer PPS buffer pointer.
 * @param ppsSize pointer to the PPS size.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 * @return -2 if SPS/PPS are not available (no sync).
 */
int BEAVER_ReaderFilter_GetSpsPps(BEAVER_ReaderFilter_Handle readerFilterHandle, uint8_t *spsBuffer, int *spsSize, uint8_t *ppsBuffer, int *ppsSize);


#ifdef __cplusplus
}
#endif /* #ifdef __cplusplus */

#endif /* #ifndef _BEAVER_READERFILTER_H_ */

