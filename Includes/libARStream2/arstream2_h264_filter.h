/**
 * @file arstream2_h264_filter.h
 * @brief Parrot Streaming Library - H.264 Filter
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#ifndef _ARSTREAM2_H264_FILTER_H_
#define _ARSTREAM2_H264_FILTER_H_

#ifdef __cplusplus
extern "C" {
#endif /* #ifdef __cplusplus */

#include <inttypes.h>
#include <libARStream2/arstream2_rtp_receiver.h>


/**
 * @brief ARSTREAM2 H264Filter instance handle.
 */
typedef void* ARSTREAM2_H264Filter_Handle;


typedef enum
{
    ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_NONE = 0,    /**< The Access Unit is not a synchronization point */
    ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_IDR,         /**< The Access Unit is an IDR picture */
    ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_IFRAME,      /**< The Access Unit is an I-frame */
    ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_PIR_START,   /**< The Access Unit is a Periodic Intra Refresh start */
    ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_MAX,

} ARSTREAM2_H264Filter_AuSyncType_t;


/* ARSTREAM2_H264Filter functions must not be called within the callback function */
typedef int (*ARSTREAM2_H264Filter_SpsPpsCallback_t)(uint8_t *spsBuffer, int spsSize, uint8_t *ppsBuffer, int ppsSize, void *userPtr);


/* ARSTREAM2_H264Filter functions must not be called within the callback function */
typedef int (*ARSTREAM2_H264Filter_GetAuBufferCallback_t)(uint8_t **auBuffer, int *auBufferSize, void **auBufferUserPtr, void *userPtr);


/* ARSTREAM2_H264Filter functions must not be called within the callback function */
typedef int (*ARSTREAM2_H264Filter_AuReadyCallback_t)(uint8_t *auBuffer, int auSize, uint64_t auTimestamp, uint64_t auTimestampShifted, ARSTREAM2_H264Filter_AuSyncType_t auSyncType, void *auMetadata, void *auBufferUserPtr, void *userPtr);


uint8_t* ARSTREAM2_H264Filter_RtpReceiverNaluCallback(eARSTREAM2_RTP_RECEIVER_CAUSE cause, uint8_t *naluBuffer, int naluSize, uint64_t auTimestamp,
                                               uint64_t auTimestampShifted, int isFirstNaluInAu, int isLastNaluInAu,
                                               int missingPacketsBefore, int *newNaluBufferSize, void *custom);


/**
 * @brief ARSTREAM2 H264Filter configuration for initialization.
 */
typedef struct
{
    int waitForSync;                                                /**< if true, wait for SPS/PPS sync before outputting access anits */
    int outputIncompleteAu;                                         /**< if true, output incomplete access units */
    int filterOutSpsPps;                                            /**< if true, filter out SPS and PPS NAL units */
    int filterOutSei;                                               /**< if true, filter out SEI NAL units */
    int replaceStartCodesWithNaluSize;                              /**< if true, replace the NAL units start code with the NALU size */
    int generateSkippedPSlices;                                     /**< if true, generate skipped P slices to replace missing slices */
    int generateFirstGrayIFrame;                                    /**< if true, generate a first gray I frame to initialize the decoding (waitForSync must be enabled) */

} ARSTREAM2_H264Filter_Config_t;


/**
 * @brief Initialize an H264Filter instance.
 *
 * The library allocates the required resources. The user must call ARSTREAM2_H264Filter_Free() to free the resources.
 *
 * @param filterHandle Pointer to the handle used in future calls to the library.
 * @param config The instance configuration.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int ARSTREAM2_H264Filter_Init(ARSTREAM2_H264Filter_Handle *filterHandle, ARSTREAM2_H264Filter_Config_t *config);


/**
 * @brief Free an H264Filter instance.
 *
 * The library frees the allocated resources. On success the filterHandle is set to NULL.
 *
 * @param filterHandle Pointer to the instance handle.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int ARSTREAM2_H264Filter_Free(ARSTREAM2_H264Filter_Handle *filterHandle);


/**
 * @brief Run an H264Filter main thread.
 *
 * The instance must be correctly allocated using ARSTREAM2_H264Filter_Init().
 * @warning This function never returns until ARSTREAM2_H264Filter_Stop() is called. The tread can then be joined.
 *
 * @param filterHandle Instance handle casted as (void*).
 *
 * @return NULL in all cases.
 */
void* ARSTREAM2_H264Filter_RunFilterThread(void *filterHandle);


/**
 * @brief Start an H264Filter instance.
 *
 * The function starts processing the ARSTREAM2_RtpReceiver input.
 * The processing can be stopped using ARSTREAM2_H264Filter_Pause().
 *
 * @param filterHandle Instance handle.
 * @param spsPpsCallback SPS/PPS callback function.
 * @param spsPpsCallbackUserPtr SPS/PPS callback user pointer.
 * @param getAuBufferCallback Get access unit buffer callback function.
 * @param getAuBufferCallbackUserPtr Get access unit buffer callback user pointer.
 * @param auReadyCallback Access unit ready callback function.
 * @param auReadyCallbackUserPtr Access unit ready callback user pointer.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int ARSTREAM2_H264Filter_Start(ARSTREAM2_H264Filter_Handle filterHandle, ARSTREAM2_H264Filter_SpsPpsCallback_t spsPpsCallback, void* spsPpsCallbackUserPtr,
                        ARSTREAM2_H264Filter_GetAuBufferCallback_t getAuBufferCallback, void* getAuBufferCallbackUserPtr,
                        ARSTREAM2_H264Filter_AuReadyCallback_t auReadyCallback, void* auReadyCallbackUserPtr);


/**
 * @brief Pause an H264Filter instance.
 *
 * The function stops processing the ARSTREAM2_RtpReceiver input.
 * The callback functions provided to ARSTREAM2_H264Filter_Start() will not be called any more.
 * The filter can be started again by a new call to ARSTREAM2_H264Filter_Start().
 *
 * @param filterHandle Instance handle.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int ARSTREAM2_H264Filter_Pause(ARSTREAM2_H264Filter_Handle filterHandle);


/**
 * @brief Stop an H264Filter instance.
 *
 * The function ends the filter thread before it can be joined.
 * A stopped filter cannot be restarted.
 *
 * @param filterHandle Instance handle.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int ARSTREAM2_H264Filter_Stop(ARSTREAM2_H264Filter_Handle filterHandle);


/**
 * @brief Get the SPS and PPS buffers.
 *
 * The buffers are filled by the function and must be provided by the user. The size of the buffers are given
 * by a first call to the function with both buffer pointers null.
 * When the buffer pointers are not null the size pointers must point to the values of the user-allocated buffer sizes.
 *
 * @param filterHandle Instance handle.
 * @param spsBuffer SPS buffer pointer.
 * @param spsSize pointer to the SPS size.
 * @param ppsBuffer PPS buffer pointer.
 * @param ppsSize pointer to the PPS size.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 * @return -2 if SPS/PPS are not available (no sync).
 */
int ARSTREAM2_H264Filter_GetSpsPps(ARSTREAM2_H264Filter_Handle filterHandle, uint8_t *spsBuffer, int *spsSize, uint8_t *ppsBuffer, int *ppsSize);


#ifdef __cplusplus
}
#endif /* #ifdef __cplusplus */

#endif /* #ifndef _ARSTREAM2_H264_FILTER_H_ */
