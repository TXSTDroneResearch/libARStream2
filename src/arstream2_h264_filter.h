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
#include <libARStream2/arstream2_stream_recorder.h>
#include <libARStream2/arstream2_stream_receiver.h>

#include "arstream2_h264.h"


/*
 * Macros
 */

#define ARSTREAM2_H264_FILTER_MB_STATUS_CLASS_COUNT (ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_MAX)
#define ARSTREAM2_H264_FILTER_MB_STATUS_ZONE_COUNT (5)
#define ARSTREAM2_H264_FILTER_STATS_OUTPUT_INTERVAL (1000000)

#define ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT

#ifdef ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT
#define ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_ALLOW_DRONE
#define ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_PATH_DRONE "/data/ftp/internal_000/streamstats"
#define ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_ALLOW_NAP_USB
#define ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_PATH_NAP_USB "/tmp/mnt/STREAMDEBUG/streamstats"
//#define ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_ALLOW_NAP_INTERNAL
#define ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_PATH_NAP_INTERNAL "/data/skycontroller/streamstats"
#define ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_ALLOW_ANDROID_INTERNAL
#define ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_PATH_ANDROID_INTERNAL "/sdcard/FF/streamstats"
#define ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_ALLOW_PCLINUX
#define ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_PATH_PCLINUX "./streamstats"

#define ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_FILENAME "videostats"
#endif


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
    uint32_t discardedFrameCount;
    uint32_t missedFrameCount;
    uint32_t errorSecondCount;
    uint64_t errorSecondStartTime;
    uint32_t errorSecondCountByZone[ARSTREAM2_H264_FILTER_MB_STATUS_ZONE_COUNT];
    uint64_t errorSecondStartTimeByZone[ARSTREAM2_H264_FILTER_MB_STATUS_ZONE_COUNT];
    uint32_t macroblockStatus[ARSTREAM2_H264_FILTER_MB_STATUS_CLASS_COUNT][ARSTREAM2_H264_FILTER_MB_STATUS_ZONE_COUNT];

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
    ARSTREAM2_H264Sei_ParrotStreamingV1_t currentAuStreamingInfo;
    uint16_t currentAuStreamingSliceMbCount[ARSTREAM2_H264_SEI_PARROT_STREAMING_MAX_SLICE_COUNT];
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
    uint64_t lastStatsOutputTimestamp;
#ifdef ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT
    FILE* fStatsOut;
#endif

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

    char *recordFileName;
    int recorderStartPending;
    ARSTREAM2_StreamRecorder_Handle recorder;
    ARSAL_Thread_t recorderThread;

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
 * @brief Stop an H264Filter instance.
 *
 * The function ends the filter thread before it can be joined.
 * A stopped filter cannot be restarted.
 *
 * @param filterHandle Instance handle.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return an eARSTREAM2_ERROR error code if an error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_H264Filter_Stop(ARSTREAM2_H264Filter_Handle filterHandle);


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


/**
 * @brief Get the frame macroblocks status.
 *
 * This function returns pointers to a macroblock status array for the current frame and image
 * macroblock width and height.
 * Macroblock statuses are of type eARSTREAM2_H264_FILTER_MACROBLOCK_STATUS.
 * This function must be called only within the ARSTREAM2_H264Filter_AuReadyCallback_t function.
 * The valididy of the data returned is only during the call to ARSTREAM2_H264Filter_AuReadyCallback_t
 * and the user must copy the macroblock status array to its own buffer for further use.
 *
 * @param filterHandle Instance handle.
 * @param macroblocks Pointer to the macroblock status array.
 * @param mbWidth pointer to the image macroblock-width.
 * @param mbHeight pointer to the image macroblock-height.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return ARSTREAM2_ERROR_WAITING_FOR_SYNC if SPS/PPS have not been received (no sync).
 * @return ARSTREAM2_ERROR_RESOURCE_UNAVAILABLE if macroblocks status is not available.
 * @return an eARSTREAM2_ERROR error code if another error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_H264Filter_GetFrameMacroblockStatus(ARSTREAM2_H264Filter_Handle filterHandle, uint8_t **macroblocks, int *mbWidth, int *mbHeight);


/**
 * @brief Start a stream recorder.
 *
 * The function starts recording the received stream to a file.
 * The recording can be stopped using ARSTREAM2_H264Filter_StopRecording().
 * The filter must be previously started using ARSTREAM2_H264Filter_Start().
 * @note Only one recording can be done at a time.
 *
 * @param filterHandle Instance handle.
 * @param recordFileName Record file absolute path.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return an eARSTREAM2_ERROR error code if an error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_H264Filter_StartRecorder(ARSTREAM2_H264Filter_Handle filterHandle, const char *recordFileName);


/**
 * @brief Stop a stream recorder.
 *
 * The function stops the current recording.
 * If no recording is in progress nothing happens.
 *
 * @param filterHandle Instance handle.
 *
 * @return ARSTREAM2_OK if no error occurred.
 * @return an eARSTREAM2_ERROR error code if an error occurred.
 */
eARSTREAM2_ERROR ARSTREAM2_H264Filter_StopRecorder(ARSTREAM2_H264Filter_Handle filterHandle);


int ARSTREAM2_H264Filter_ProcessAu(ARSTREAM2_H264Filter_t *filter, ARSTREAM2_H264_AccessUnit_t *au);


void ARSTREAM2_H264Filter_ResetAu(ARSTREAM2_H264Filter_t *filter);


int ARSTREAM2_H264Filter_ForceResync(ARSTREAM2_H264Filter_t *filter);


int ARSTREAM2_H264Filter_ForceIdr(ARSTREAM2_H264Filter_t *filter);


/*
 * Error concealment functions
 */

int ARSTREAM2_H264FilterError_OutputGrayIdrFrame(ARSTREAM2_H264Filter_t *filter, ARSTREAM2_H264_AccessUnit_t *nextAu);


int ARSTREAM2_H264FilterError_HandleMissingSlices(ARSTREAM2_H264Filter_t *filter, ARSTREAM2_H264_AccessUnit_t *au,
                                                  ARSTREAM2_H264_NaluFifoItem_t *nextNaluItem, int isFirstNaluInAu);


int ARSTREAM2_H264FilterError_HandleMissingEndOfFrame(ARSTREAM2_H264Filter_t *filter, ARSTREAM2_H264_AccessUnit_t *au,
                                                      ARSTREAM2_H264_NaluFifoItem_t *prevNaluItem);


#endif /* #ifndef _ARSTREAM2_H264_FILTER_H_ */
