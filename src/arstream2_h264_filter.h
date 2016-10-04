/**
 * @file arstream2_h264_filter.c
 * @brief Parrot Reception Library - H.264 Filter
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */


#ifndef _ARSTREAM2_H264_FILTER_H_
#define _ARSTREAM2_H264_FILTER_H_


#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <arpa/inet.h>

#include <libARSAL/ARSAL_Print.h>
#include <libARSAL/ARSAL_Mutex.h>
#include <libARSAL/ARSAL_Thread.h>

#include <libARStream2/arstream2_h264_parser.h>
#include <libARStream2/arstream2_h264_writer.h>
#include <libARStream2/arstream2_h264_sei.h>
#include <libARStream2/arstream2_stream_receiver.h>

#include "arstream2_h264.h"


/*
 * Macros
 */

#define ARSTREAM2_H264_FILTER_MB_STATUS_CLASS_COUNT (ARSTREAM2_STREAM_RECEIVER_MB_STATUS_CLASS_COUNT)
#define ARSTREAM2_H264_FILTER_MB_STATUS_ZONE_COUNT (ARSTREAM2_STREAM_RECEIVER_MB_STATUS_ZONE_COUNT)
#define ARSTREAM2_H264_FILTER_STATS_OUTPUT_INTERVAL (1000000)


/*
 * Types
 */

/**
 * @brief ARSTREAM2 H264Filter instance handle.
 */
typedef void* ARSTREAM2_H264Filter_Handle;


typedef int (*ARSTREAM2_H264Filter_SpsPpsSyncCallback_t)(uint8_t *spsBuffer, int spsSize, uint8_t *ppsBuffer, int ppsSize, void *userPtr);


/**
 * @brief ARSTREAM2 H264Filter configuration for initialization.
 */
typedef struct
{
    ARSTREAM2_H264_AuFifo_t *auFifo;
    ARSTREAM2_H264_NaluFifo_t *naluFifo;
    ARSAL_Mutex_t *fifoMutex;
    ARSTREAM2_H264_ReceiverAuCallback_t auCallback;
    void *auCallbackUserPtr;
    ARSTREAM2_H264Filter_SpsPpsSyncCallback_t spsPpsCallback;
    void *spsPpsCallbackUserPtr;
    int outputIncompleteAu;                                         /**< if true, output incomplete access units */
    int filterOutSpsPps;                                            /**< if true, filter out SPS and PPS NAL units */
    int filterOutSei;                                               /**< if true, filter out SEI NAL units */
    int replaceStartCodesWithNaluSize;                              /**< if true, replace the NAL units start code with the NALU size */
    int generateSkippedPSlices;                                     /**< if true, generate skipped P slices to replace missing slices */
    int generateFirstGrayIFrame;                                    /**< if true, generate a first gray I frame to initialize the decoding (waitForSync must be enabled) */

} ARSTREAM2_H264Filter_Config_t;


typedef struct ARSTREAM2_H264Filter_VideoStats_s
{
    uint32_t totalFrameCount;
    uint32_t outputFrameCount;
    uint32_t erroredOutputFrameCount;
    uint32_t discardedFrameCount;
    uint32_t missedFrameCount;
    uint32_t erroredSecondCount;
    uint64_t erroredSecondStartTime;
    uint32_t erroredSecondCountByZone[ARSTREAM2_H264_FILTER_MB_STATUS_ZONE_COUNT];
    uint64_t erroredSecondStartTimeByZone[ARSTREAM2_H264_FILTER_MB_STATUS_ZONE_COUNT];
    uint32_t macroblockStatus[ARSTREAM2_H264_FILTER_MB_STATUS_CLASS_COUNT][ARSTREAM2_H264_FILTER_MB_STATUS_ZONE_COUNT];
    uint32_t timestampDelta;
    uint64_t timestampDeltaIntegral;
    uint64_t timestampDeltaIntegralSq;
    int32_t timingError;
    uint64_t timingErrorIntegral;
    uint64_t timingErrorIntegralSq;
    uint32_t estimatedLatency;
    uint64_t estimatedLatencyIntegral;
    uint64_t estimatedLatencyIntegralSq;

} ARSTREAM2_H264Filter_VideoStats_t;


typedef struct ARSTREAM2_H264Filter_s
{
    int outputIncompleteAu;
    int filterOutSpsPps;
    int filterOutSei;
    int replaceStartCodesWithNaluSize;
    int generateSkippedPSlices;
    int generateFirstGrayIFrame;

    int currentAuOutputIndex;
    int currentAuSize;
    int currentAuIncomplete;
    int currentAuFrameNum;
    int previousAuFrameNum;
    int currentAuSlicesReceived;
    int currentAuSlicesAllI;
    int currentAuStreamingInfoAvailable;
    uint16_t currentAuStreamingSliceMbCount[ARSTREAM2_H264_SEI_PARROT_STREAMING_MAX_SLICE_COUNT];
    int currentAuStreamingSliceCount;
    int currentAuStreamingInfoV1Available;
    ARSTREAM2_H264Sei_ParrotStreamingV1_t currentAuStreamingInfoV1;
    int currentAuStreamingInfoV2Available;
    ARSTREAM2_H264Sei_ParrotStreamingV2_t currentAuStreamingInfoV2;
    int currentAuIsRecoveryPoint;
    int currentAuPreviousSliceIndex;
    int currentAuPreviousSliceFirstMb;
    int currentAuCurrentSliceFirstMb;
    uint8_t previousSliceType;

    uint8_t *currentAuRefMacroblockStatus;
    uint8_t *currentAuMacroblockStatus;
    int currentAuIsRef;
    int currentAuInferredSliceMbCount;
    int currentAuInferredPreviousSliceFirstMb;

    /* H.264-level stats */
    ARSTREAM2_H264Filter_VideoStats_t stats;

    ARSTREAM2_H264Parser_Handle parser;
    ARSTREAM2_H264Writer_Handle writer;

    int sync;
    int spsSync;
    int spsSize;
    uint8_t* pSps;
    int ppsSync;
    int ppsSize;
    uint8_t* pPps;
    ARSTREAM2_H264Filter_SpsPpsSyncCallback_t spsPpsCallback;
    void *spsPpsCallbackUserPtr;
    int firstGrayIFramePending;
    int resyncPending;
    int mbWidth;
    int mbHeight;
    int mbCount;
    float framerate;
    int maxFrameNum;

    /* NAL unit and access unit FIFO */
    ARSTREAM2_H264_NaluFifo_t *naluFifo;
    ARSTREAM2_H264_AuFifo_t *auFifo;
    ARSAL_Mutex_t *fifoMutex;
    ARSTREAM2_H264_ReceiverAuCallback_t auCallback;
    void *auCallbackUserPtr;

} ARSTREAM2_H264Filter_t;


/*
 * Functions
 */

/**
 * @brief Initialize an H264Filter instance.
 *
 * The library allocates the required resources. The user must call ARSTREAM2_H264Filter_Free() to free the resources.
 *
 * @param filterHandle Pointer to the handle used in future calls to the library.
 * @param config The instance configuration.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return an eARSTREAM2_ERROR error code if an error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_H264Filter_Init(ARSTREAM2_H264Filter_Handle *filterHandle, ARSTREAM2_H264Filter_Config_t *config);


/**
 * @brief Free an H264Filter instance.
 *
 * The library frees the allocated resources. On success the filterHandle is set to NULL.
 *
 * @param filterHandle Pointer to the instance handle.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return an eARSTREAM2_ERROR error code if an error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_H264Filter_Free(ARSTREAM2_H264Filter_Handle *filterHandle);


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
 * @return ARSTREAM2_OK if no error occurred.
 * @return ARSTREAM2_ERROR_WAITING_FOR_SYNC if SPS/PPS are not available (no sync).
 * @return an eARSTREAM2_ERROR error code if another error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_H264Filter_GetSpsPps(ARSTREAM2_H264Filter_Handle filterHandle, uint8_t *spsBuffer, int *spsSize, uint8_t *ppsBuffer, int *ppsSize);


int ARSTREAM2_H264Filter_GetVideoParams(ARSTREAM2_H264Filter_Handle filterHandle, int *mbWidth, int *mbHeight, int *width, int *height, float *framerate);


int ARSTREAM2_H264Filter_ProcessAu(ARSTREAM2_H264Filter_t *filter, ARSTREAM2_H264_AccessUnit_t *au);


void ARSTREAM2_H264Filter_ResetAu(ARSTREAM2_H264Filter_t *filter);


int ARSTREAM2_H264Filter_ForceResync(ARSTREAM2_H264Filter_t *filter);


int ARSTREAM2_H264Filter_ForceIdr(ARSTREAM2_H264Filter_t *filter);


/*
 * Error concealment functions
 */

int ARSTREAM2_H264FilterError_OutputGrayIdrFrame(ARSTREAM2_H264Filter_t *filter, ARSTREAM2_H264_AccessUnit_t *nextAu);


int ARSTREAM2_H264FilterError_HandleMissingSlices(ARSTREAM2_H264Filter_t *filter, ARSTREAM2_H264_AccessUnit_t *au,
                                                  ARSTREAM2_H264_NaluFifoItem_t *nextNaluItem);


int ARSTREAM2_H264FilterError_HandleMissingEndOfFrame(ARSTREAM2_H264Filter_t *filter, ARSTREAM2_H264_AccessUnit_t *au,
                                                      ARSTREAM2_H264_NaluFifoItem_t *prevNaluItem);


#endif /* #ifndef _ARSTREAM2_H264_FILTER_H_ */
