/**
 * @file arstream2_stream_recorder.h
 * @brief Parrot Streaming Library - Stream Recorder
 * @date 06/01/2016
 * @author aurelien.barre@parrot.com
 */

#ifndef _ARSTREAM2_STREAM_RECORDER_H_
#define _ARSTREAM2_STREAM_RECORDER_H_

#ifdef __cplusplus
extern "C" {
#endif /* #ifdef __cplusplus */

#include <inttypes.h>
#include <libARStream2/arstream2_error.h>
#include <libARStream2/arstream2_h264_filter.h>


/**
 * @brief Maximum number of NAL units per access unit.
 */
#define ARSTREAM2_STREAM_RECORDER_NALU_MAX_COUNT   (128)


/**
 * @brief ARSTREAM2 StreamRecorder instance handle.
 */
typedef void* ARSTREAM2_StreamRecorder_Handle;


/**
 * @brief Access unit callback status values
 */
typedef enum {
    ARSTREAM2_STREAM_RECORDER_AU_STATUS_SUCCESS = 0,    /**< Access unit was recorded */
    ARSTREAM2_STREAM_RECORDER_AU_STATUS_FAILED,         /**< Access unit recording has failed */
    ARSTREAM2_STREAM_RECORDER_AU_STATUS_MAX,
} eARSTREAM2_STREAM_RECORDER_AU_STATUS;


/**
 * @brief Callback function for access units
 * This callback function is called when buffers associated with an access unit are no longer used by the recorder.
 *
 * @param[in] status Access unit recording status
 * @param[in] auUserPtr Access unit user pointer associated with the current access unit
 * @param[in] userPtr Global access unit callback user pointer
 * @see eARSTREAM2_STREAM_RECORDER_AU_STATUS
 */
typedef void (*ARSTREAM2_StreamRecorder_AuCallback_t)(eARSTREAM2_STREAM_RECORDER_AU_STATUS status, void *auUserPtr, void *userPtr);


/**
 * @brief ARSTREAM2 StreamRecorder configuration for initialization.
 */
typedef struct
{
    const char *mediaFileName;              /**< Destination media file name */
    float videoFramerate;                   /**< Video framerate (frame/s) */
    uint32_t videoWidth;                    /**< Video width (pixels) */
    uint32_t videoHeight;                   /**< Video height (pixels) */
    const uint8_t *sps;                     /**< H.264 video SPS buffer pointer */
    uint32_t spsSize;                       /**< H.264 video SPS buffer size in bytes */
    const uint8_t *pps;                     /**< H.264 video PPS buffer pointer */
    uint32_t ppsSize;                       /**< H.264 video PPS buffer size in bytes */
    int serviceType;                        /**< ARDiscovery service type */
    int auFifoSize;                         /**< Access unit FIFO size */
    ARSTREAM2_StreamRecorder_AuCallback_t auCallback;   /**< Access unit callback function (optional, can be NULL) */
    void *auCallbackUserPtr;                /**< Access unit callback function user pointer (optional, can be NULL) */

} ARSTREAM2_StreamRecorder_Config_t;


/**
 * @brief ARSTREAM2 StreamRecorder access unit data.
 */
typedef struct
{
    uint64_t timestamp;                     /**< Access unit timestamp (microseconds) */
    uint32_t index;                         /**< Access unit index */
    uint8_t *auData;                        /**< Access unit data pointer */
    uint32_t auSize;                        /**< Access unit size (bytes) */
    uint32_t naluCount;                     /**< NAL unit count */
    uint32_t naluSize[ARSTREAM2_STREAM_RECORDER_NALU_MAX_COUNT];    /**< NAL unit size array */
    uint8_t *naluData[ARSTREAM2_STREAM_RECORDER_NALU_MAX_COUNT];    /**< NAL unit data pointer array */
    eARSTREAM2_H264_FILTER_AU_SYNC_TYPE auSyncType;                 /**< Access unit sync type */
    uint8_t *auMetadata;                    /**< Access unit metadata pointer (optional, can be NULL) */
    uint32_t auMetadataSize;                /**< Access unit metadata size (bytes) (optional, can be NULL) */
    void *auUserPtr;                        /**< Access unit user pointer for the access unit callback function (optional, can be NULL) */

} ARSTREAM2_StreamRecorder_AccessUnit_t;


/**
 * @brief Initialize a StreamRecorder instance.
 *
 * The library allocates the required resources. The user must call ARSTREAM2_StreamRecorder_Free() to free the resources.
 *
 * @param streamRecorderHandle Pointer to the handle used in future calls to the library.
 * @param config The instance configuration.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return a eARSTREAM2_ERROR if an error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_StreamRecorder_Init(ARSTREAM2_StreamRecorder_Handle *streamRecorderHandle,
                                               ARSTREAM2_StreamRecorder_Config_t *config);


/**
 * @brief Free a StreamRecorder instance.
 *
 * The library frees the allocated resources. On success the streamRecorderHandle is set to NULL.
 *
 * @param streamRecorderHandle Pointer to the instance handle.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return a eARSTREAM2_ERROR if an error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_StreamRecorder_Free(ARSTREAM2_StreamRecorder_Handle *streamRecorderHandle);


/**
 * @brief Stop a StreamRecorder instance.
 *
 * The function ends the thread before it can be joined.
 *
 * @param streamRecorderHandle Instance handle.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return a eARSTREAM2_ERROR if an error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_StreamRecorder_Stop(ARSTREAM2_StreamRecorder_Handle streamRecorderHandle);


/**
 * @brief Push a new access unit.
 *
 * Add a new access unit to the recording.
 *
 * @param streamRecorderHandle Instance handle.
 * @param accessUnit Access unit data.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return a eARSTREAM2_ERROR if an error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_StreamRecorder_PushAccessUnit(ARSTREAM2_StreamRecorder_Handle streamRecorderHandle,
                                                         ARSTREAM2_StreamRecorder_AccessUnit_t *accessUnit);


/**
 * @brief Flush the access unit queue.
 *
 * Flush the access unit queue (and call the callback function).
 *
 * @param streamRecorderHandle Instance handle.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return a eARSTREAM2_ERROR if an error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_StreamRecorder_Flush(ARSTREAM2_StreamRecorder_Handle streamRecorderHandle);


/**
 * @brief Run a StreamRecorder thread.
 *
 * The instance must be correctly allocated using ARSTREAM2_StreamRecorder_Init().
 * @warning This function never returns until ARSTREAM2_StreamRecorder_Stop() is called. The tread can then be joined.
 *
 * @param streamRecorderHandle Instance handle casted as (void*).
 *
 * @return NULL in all cases.
 */
void* ARSTREAM2_StreamRecorder_RunThread(void *streamRecorderHandle);


#ifdef __cplusplus
}
#endif /* #ifdef __cplusplus */

#endif /* #ifndef _ARSTREAM2_STREAM_RECORDER_H_ */
