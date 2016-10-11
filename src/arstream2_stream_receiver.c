/**
 * @file arstream2_stream_receiver.c
 * @brief Parrot Streaming Library - Stream Receiver
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#include <stdio.h>
#include <stdlib.h>

#include <libARSAL/ARSAL_Print.h>
#include <libARSAL/ARSAL_Mutex.h>
#include <libARSAL/ARSAL_Thread.h>

#include <libARStream2/arstream2_stream_receiver.h>
#include "arstream2_stream_recorder.h"
#include "arstream2_rtp_receiver.h"
#include "arstream2_h264_filter.h"
#include "arstream2_h264.h"
#include "arstream2_stream_stats.h"


#define ARSTREAM2_STREAM_RECEIVER_TAG "ARSTREAM2_StreamReceiver"

#define ARSTREAM2_STREAM_RECEIVER_TIMEOUT_US (100 * 1000)

#define ARSTREAM2_STREAM_RECEIVER_DEFAULT_AU_FIFO_ITEM_COUNT (200)
#define ARSTREAM2_STREAM_RECEIVER_DEFAULT_AU_FIFO_ITEM_NALU_COUNT (128)
#define ARSTREAM2_STREAM_RECEIVER_DEFAULT_AU_FIFO_BUFFER_COUNT (60)

#define ARSTREAM2_STREAM_RECEIVER_AU_BUFFER_SIZE (128 * 1024)
#define ARSTREAM2_STREAM_RECEIVER_AU_METADATA_BUFFER_SIZE (1024)
#define ARSTREAM2_STREAM_RECEIVER_AU_USER_DATA_BUFFER_SIZE (1024)

#define ARSTREAM2_STREAM_RECEIVER_VIDEO_AUTOREC_OUTPUT_PATH "videorec"
#define ARSTREAM2_STREAM_RECEIVER_VIDEO_AUTOREC_OUTPUT_FILENAME "videorec"
#define ARSTREAM2_STREAM_RECEIVER_VIDEO_AUTOREC_OUTPUT_FILEEXT "mp4"

#define ARSTREAM2_STREAM_RECEIVER_VIDEO_STATS_RTCP_SEND_INTERVAL (1000000)


typedef struct ARSTREAM2_StreamReceiver_s
{
    ARSTREAM2_H264_AuFifo_t auFifo;
    ARSTREAM2_H264Filter_Handle filter;
    ARSTREAM2_RtpReceiver_t *receiver;

    int sync;
    uint8_t *pSps;
    int spsSize;
    uint8_t *pPps;
    int ppsSize;
    uint64_t lastAuNtpTimestamp;
    uint64_t lastAuNtpTimestampRaw;
    uint64_t lastAuOutputTimestamp;
    uint64_t timestampDeltaIntegral;
    uint64_t timestampDeltaIntegralSq;
    uint64_t timingErrorIntegral;
    uint64_t timingErrorIntegralSq;
    uint64_t estimatedLatencyIntegral;
    uint64_t estimatedLatencyIntegralSq;

    struct
    {
        ARSTREAM2_H264_AuFifoQueue_t auFifoQueue;
        int grayIFramePending;
        int filterOutSpsPps;
        int filterOutSei;
        int replaceStartCodesWithNaluSize;
        ARSAL_Mutex_t threadMutex;
        ARSAL_Cond_t threadCond;
        int threadRunning;
        int threadShouldStop;
        int running;
        ARSAL_Mutex_t callbackMutex;
        ARSAL_Cond_t callbackCond;
        int callbackInProgress;
        ARSTREAM2_StreamReceiver_SpsPpsCallback_t spsPpsCallback;
        void *spsPpsCallbackUserPtr;
        ARSTREAM2_StreamReceiver_GetAuBufferCallback_t getAuBufferCallback;
        void *getAuBufferCallbackUserPtr;
        ARSTREAM2_StreamReceiver_AuReadyCallback_t auReadyCallback;
        void *auReadyCallbackUserPtr;
        int mbWidth;
        int mbHeight;

    } appOutput;

    struct
    {
        ARSTREAM2_H264_AuFifoQueue_t auFifoQueue;
        char *fileName;
        int startPending;
        int running;
        int grayIFramePending;
        ARSTREAM2_StreamRecorder_Handle recorder;
        ARSTREAM2_StreamRecorder_AccessUnit_t accessUnit;
        ARSAL_Thread_t thread;
        ARSAL_Mutex_t mutex;
        int auCount;

    } recorder;

    /* Debug files */
    char *friendlyName;
    char *dateAndTime;
    char *debugPath;
    ARSTREAM2_StreamStats_VideoStats_t videoStats;

} ARSTREAM2_StreamReceiver_t;


static int ARSTREAM2_StreamReceiver_GenerateGrayIdrFrame(ARSTREAM2_StreamReceiver_t *streamReceiver, ARSTREAM2_H264_AccessUnit_t *nextAu);
static int ARSTREAM2_StreamReceiver_RtpReceiverAuCallback(ARSTREAM2_H264_AuFifoItem_t *auItem, void *userPtr);
static int ARSTREAM2_StreamReceiver_H264FilterAuCallback(ARSTREAM2_H264_AuFifoItem_t *auItem, void *userPtr);
static int ARSTREAM2_StreamReceiver_H264FilterSpsPpsCallback(uint8_t *spsBuffer, int spsSize, uint8_t *ppsBuffer, int ppsSize, void *userPtr);
static void ARSTREAM2_StreamReceiver_StreamRecorderAuCallback(eARSTREAM2_STREAM_RECORDER_AU_STATUS status, void *auUserPtr, void *userPtr);
static int ARSTREAM2_StreamReceiver_StreamRecorderInit(ARSTREAM2_StreamReceiver_t *streamReceiver);
static int ARSTREAM2_StreamReceiver_StreamRecorderStop(ARSTREAM2_StreamReceiver_t *streamReceiver);
static int ARSTREAM2_StreamReceiver_StreamRecorderFree(ARSTREAM2_StreamReceiver_t *streamReceiver);
static void ARSTREAM2_StreamReceiver_AutoStartRecorder(ARSTREAM2_StreamReceiver_t *streamReceiver);


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_Init(ARSTREAM2_StreamReceiver_Handle *streamReceiverHandle,
                                               const ARSTREAM2_StreamReceiver_Config_t *config,
                                               const ARSTREAM2_StreamReceiver_NetConfig_t *net_config,
                                               const ARSTREAM2_StreamReceiver_MuxConfig_t *mux_config)
{
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;
    ARSTREAM2_StreamReceiver_t *streamReceiver = NULL;
    int auFifoCreated = 0;
    int appOutputThreadMutexInit = 0, appOutputThreadCondInit = 0;
    int appOutputCallbackMutexInit = 0, appOutputCallbackCondInit = 0;
    int recorderMutexInit = 0;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid pointer for handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!config)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid pointer for config");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!net_config && !mux_config)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "No net nor mux config");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (net_config && mux_config)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Both net and mux config provided");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    int usemux = mux_config != NULL;

    streamReceiver = (ARSTREAM2_StreamReceiver_t*)malloc(sizeof(*streamReceiver));
    if (!streamReceiver)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Allocation failed (size %ld)", sizeof(*streamReceiver));
        ret = ARSTREAM2_ERROR_ALLOC;
    }

    if (ret == ARSTREAM2_OK)
    {
        memset(streamReceiver, 0, sizeof(*streamReceiver));
        streamReceiver->appOutput.filterOutSpsPps = (config->filterOutSpsPps > 0) ? 1 : 0;
        streamReceiver->appOutput.filterOutSei = (config->filterOutSei > 0) ? 1 : 0;
        streamReceiver->appOutput.replaceStartCodesWithNaluSize = (config->replaceStartCodesWithNaluSize > 0) ? 1 : 0;
        if ((config->debugPath) && (strlen(config->debugPath)))
        {
            streamReceiver->debugPath = strdup(config->debugPath);
        }
        if ((config->friendlyName) && (strlen(config->friendlyName)))
        {
            streamReceiver->friendlyName = strndup(config->friendlyName, 40);
        }
        else if ((config->canonicalName) && (strlen(config->canonicalName)))
        {
            streamReceiver->friendlyName = strndup(config->canonicalName, 40);
        }
        char szDate[200];
        time_t rawtime;
        struct tm timeinfo;
        time(&rawtime);
        localtime_r(&rawtime, &timeinfo);
        /* Date format : <YYYY-MM-DDTHHMMSS+HHMM */
        strftime(szDate, 200, "%FT%H%M%S%z", &timeinfo);
        streamReceiver->dateAndTime = strndup(szDate, 200);
        ARSTREAM2_StreamReceiver_AutoStartRecorder(streamReceiver);
        ARSTREAM2_StreamStats_VideoStatsFileOpen(&streamReceiver->videoStats, streamReceiver->debugPath, streamReceiver->friendlyName, streamReceiver->dateAndTime);
    }

    if (ret == ARSTREAM2_OK)
    {
        int mutexInitRet = ARSAL_Mutex_Init(&(streamReceiver->appOutput.threadMutex));
        if (mutexInitRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Mutex creation failed (%d)", mutexInitRet);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            appOutputThreadMutexInit = 1;
        }
    }

    if (ret == ARSTREAM2_OK)
    {
        int condInitRet = ARSAL_Cond_Init(&(streamReceiver->appOutput.threadCond));
        if (condInitRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Cond creation failed (%d)", condInitRet);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            appOutputThreadCondInit = 1;
        }
    }

    if (ret == ARSTREAM2_OK)
    {
        int mutexInitRet = ARSAL_Mutex_Init(&(streamReceiver->appOutput.callbackMutex));
        if (mutexInitRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Mutex creation failed (%d)", mutexInitRet);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            appOutputCallbackMutexInit = 1;
        }
    }

    if (ret == ARSTREAM2_OK)
    {
        int condInitRet = ARSAL_Cond_Init(&(streamReceiver->appOutput.callbackCond));
        if (condInitRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Cond creation failed (%d)", condInitRet);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            appOutputCallbackCondInit = 1;
        }
    }

    if (ret == ARSTREAM2_OK)
    {
        int mutexInitRet = ARSAL_Mutex_Init(&(streamReceiver->recorder.mutex));
        if (mutexInitRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Mutex creation failed (%d)", mutexInitRet);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            recorderMutexInit = 1;
        }
    }

    /* Setup the access unit FIFO */
    if (ret == ARSTREAM2_OK)
    {
        int auFifoRet = ARSTREAM2_H264_AuFifoInit(&streamReceiver->auFifo,
                                                  ARSTREAM2_STREAM_RECEIVER_DEFAULT_AU_FIFO_ITEM_COUNT,
                                                  ARSTREAM2_STREAM_RECEIVER_DEFAULT_AU_FIFO_ITEM_NALU_COUNT,
                                                  ARSTREAM2_STREAM_RECEIVER_DEFAULT_AU_FIFO_BUFFER_COUNT,
                                                  ARSTREAM2_STREAM_RECEIVER_AU_BUFFER_SIZE,
                                                  ARSTREAM2_STREAM_RECEIVER_AU_METADATA_BUFFER_SIZE,
                                                  ARSTREAM2_STREAM_RECEIVER_AU_USER_DATA_BUFFER_SIZE,
                                                  sizeof(ARSTREAM2_H264_VideoStats_t));
        if (auFifoRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264_AuFifoInit() failed (%d)", auFifoRet);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            auFifoCreated = 1;
        }
    }

    if (ret == ARSTREAM2_OK)
    {
        ARSTREAM2_RtpReceiver_Config_t receiverConfig;
        ARSTREAM2_RtpReceiver_NetConfig_t receiver_net_config;
        ARSTREAM2_RtpReceiver_MuxConfig_t receiver_mux_config;
        memset(&receiverConfig, 0, sizeof(receiverConfig));
        memset(&receiver_net_config, 0, sizeof(receiver_net_config));
        memset(&receiver_mux_config, 0, sizeof(receiver_mux_config));


        receiverConfig.canonicalName = config->canonicalName;
        receiverConfig.friendlyName = config->friendlyName;
        receiverConfig.auFifo = &(streamReceiver->auFifo);
        receiverConfig.auCallback = ARSTREAM2_StreamReceiver_RtpReceiverAuCallback;
        receiverConfig.auCallbackUserPtr = streamReceiver;
        receiverConfig.maxPacketSize = config->maxPacketSize;
        receiverConfig.insertStartCodes = 1;
        receiverConfig.generateReceiverReports = config->generateReceiverReports;
        receiverConfig.videoStatsSendTimeInterval = ARSTREAM2_STREAM_RECEIVER_VIDEO_STATS_RTCP_SEND_INTERVAL;

        if (usemux) {
            receiver_mux_config.mux = mux_config->mux;
            streamReceiver->receiver = ARSTREAM2_RtpReceiver_New(&receiverConfig, NULL, &receiver_mux_config, &ret);
        } else {
            receiver_net_config.serverAddr = net_config->serverAddr;
            receiver_net_config.mcastAddr = net_config->mcastAddr;
            receiver_net_config.mcastIfaceAddr = net_config->mcastIfaceAddr;
            receiver_net_config.serverStreamPort = net_config->serverStreamPort;
            receiver_net_config.serverControlPort = net_config->serverControlPort;
            receiver_net_config.clientStreamPort = net_config->clientStreamPort;
            receiver_net_config.clientControlPort = net_config->clientControlPort;
            receiver_net_config.classSelector = net_config->classSelector;
            streamReceiver->receiver = ARSTREAM2_RtpReceiver_New(&receiverConfig, &receiver_net_config, NULL, &ret);
        }

        if (ret != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Error while creating receiver : %s", ARSTREAM2_Error_ToString(ret));
        }
    }

    if (ret == ARSTREAM2_OK)
    {
        ARSTREAM2_H264Filter_Config_t filterConfig;
        memset(&filterConfig, 0, sizeof(filterConfig));
        filterConfig.spsPpsCallback = ARSTREAM2_StreamReceiver_H264FilterSpsPpsCallback;
        filterConfig.spsPpsCallbackUserPtr = streamReceiver;
        filterConfig.outputIncompleteAu = config->outputIncompleteAu;
        filterConfig.generateSkippedPSlices = config->generateSkippedPSlices;

        ret = ARSTREAM2_H264Filter_Init(&streamReceiver->filter, &filterConfig);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Error while creating H264Filter: %s", ARSTREAM2_Error_ToString(ret));
        }
    }

    if (ret == ARSTREAM2_OK)
    {
        *streamReceiverHandle = (ARSTREAM2_StreamReceiver_Handle*)streamReceiver;
    }
    else
    {
        if (streamReceiver)
        {
            if (streamReceiver->receiver) ARSTREAM2_RtpReceiver_Delete(&(streamReceiver->receiver));
            if (streamReceiver->filter) ARSTREAM2_H264Filter_Free(&(streamReceiver->filter));
            if (auFifoCreated) ARSTREAM2_H264_AuFifoFree(&(streamReceiver->auFifo));
            if (appOutputThreadMutexInit) ARSAL_Mutex_Destroy(&(streamReceiver->appOutput.threadMutex));
            if (appOutputThreadCondInit) ARSAL_Cond_Destroy(&(streamReceiver->appOutput.threadCond));
            if (appOutputCallbackMutexInit) ARSAL_Mutex_Destroy(&(streamReceiver->appOutput.callbackMutex));
            if (appOutputCallbackCondInit) ARSAL_Cond_Destroy(&(streamReceiver->appOutput.callbackCond));
            if (recorderMutexInit) ARSAL_Mutex_Destroy(&(streamReceiver->recorder.mutex));
            ARSTREAM2_StreamStats_VideoStatsFileClose(&streamReceiver->videoStats);
            free(streamReceiver->debugPath);
            free(streamReceiver->friendlyName);
            free(streamReceiver->dateAndTime);
            free(streamReceiver);
        }
        *streamReceiverHandle = NULL;
    }

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_Free(ARSTREAM2_StreamReceiver_Handle *streamReceiverHandle)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;

    if ((!streamReceiverHandle) || (!*streamReceiverHandle))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid pointer for handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    streamReceiver = (ARSTREAM2_StreamReceiver_t*)*streamReceiverHandle;

    if (streamReceiver->appOutput.threadRunning == 1)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Call ARSTREAM2_H264Filter_Stop before calling this function");
        return ARSTREAM2_ERROR_BUSY;
    }

    int recErr = ARSTREAM2_StreamReceiver_StreamRecorderFree(streamReceiver);
    if (recErr != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_StreamReceiver_StreamRecorderFree() failed (%d)", recErr);
    }

    ret = ARSTREAM2_RtpReceiver_Delete(&streamReceiver->receiver);
    if (ret != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Unable to delete receiver: %s", ARSTREAM2_Error_ToString(ret));
    }

    ret = ARSTREAM2_H264Filter_Free(&streamReceiver->filter);
    if (ret != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Unable to delete H264Filter: %s", ARSTREAM2_Error_ToString(ret));
    }

    ARSTREAM2_H264_AuFifoFree(&(streamReceiver->auFifo));
    ARSAL_Mutex_Destroy(&(streamReceiver->appOutput.threadMutex));
    ARSAL_Cond_Destroy(&(streamReceiver->appOutput.threadCond));
    ARSAL_Mutex_Destroy(&(streamReceiver->appOutput.callbackMutex));
    ARSAL_Cond_Destroy(&(streamReceiver->appOutput.callbackCond));
    ARSAL_Mutex_Destroy(&(streamReceiver->recorder.mutex));
    free(streamReceiver->recorder.fileName);
    free(streamReceiver->pSps);
    free(streamReceiver->pPps);
    ARSTREAM2_StreamStats_VideoStatsFileClose(&streamReceiver->videoStats);
    free(streamReceiver->debugPath);
    free(streamReceiver->friendlyName);
    free(streamReceiver->dateAndTime);

    free(streamReceiver);
    *streamReceiverHandle = NULL;

    return ret;
}


static int ARSTREAM2_StreamReceiver_AppOutputAuEnqueue(ARSTREAM2_StreamReceiver_t *streamReceiver, ARSTREAM2_H264_AuFifoItem_t *auItem)
{
    int err = 0, ret = 0, needUnref = 0, needFree = 0;
    ARSTREAM2_H264_AuFifoItem_t *appOutputAuItem = NULL;

    /* add ref to AU buffer */
    ret = ARSTREAM2_H264_AuFifoBufferAddRef(&streamReceiver->auFifo, auItem->au.buffer);
    if (ret < 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264_AuFifoBufferAddRef() failed (%d)", ret);
    }
    if (ret == 0)
    {
        /* duplicate the AU and associated NALUs */
        appOutputAuItem = ARSTREAM2_H264_AuFifoDuplicateItem(&streamReceiver->auFifo, auItem);
        if (appOutputAuItem)
        {
            appOutputAuItem->au.buffer = auItem->au.buffer;
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to pop free item from the AU FIFO");
            ret = -1;
            needUnref = 1;
        }
    }

    if ((ret == 0) && (appOutputAuItem))
    {
        /* enqueue the AU */
        ARSAL_Mutex_Lock(&(streamReceiver->appOutput.threadMutex));
        ret = ARSTREAM2_H264_AuFifoEnqueueItem(&streamReceiver->appOutput.auFifoQueue, appOutputAuItem);
        ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.threadMutex));
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264_AuFifoEnqueueItem() failed (%d)", ret);
            err = -1;
            needUnref = 1;
            needFree = 1;
        }
        else
        {
            ARSAL_Cond_Signal(&(streamReceiver->appOutput.threadCond));
        }
    }
    else
    {
        err = -1;
    }

    /* error handling */
    if (needFree)
    {
        ret = ARSTREAM2_H264_AuFifoPushFreeItem(&streamReceiver->auFifo, appOutputAuItem);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the AU FIFO (%d)", ret);
        }
        needFree = 0;
    }
    if (needUnref)
    {
        ret = ARSTREAM2_H264_AuFifoUnrefBuffer(&streamReceiver->auFifo, auItem->au.buffer);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to unref buffer (%d)", ret);
        }
        needUnref = 0;
    }

    return err;
}


static int ARSTREAM2_StreamReceiver_RecorderAuEnqueue(ARSTREAM2_StreamReceiver_t *streamReceiver, ARSTREAM2_H264_AuFifoItem_t *auItem)
{
    int err = 0, ret = 0, needUnref = 0, needFree = 0;
    ARSTREAM2_H264_AuFifoItem_t *recordtAuItem = NULL;

    ret = ARSTREAM2_H264_AuFifoBufferAddRef(&streamReceiver->auFifo, auItem->au.buffer);
    if (ret < 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264_AuFifoBufferAddRef() failed (%d)", ret);
    }
    if (ret == 0)
    {
        /* duplicate the AU and associated NALUs */
        recordtAuItem = ARSTREAM2_H264_AuFifoDuplicateItem(&streamReceiver->auFifo, auItem);
        if (recordtAuItem)
        {
            recordtAuItem->au.buffer = auItem->au.buffer;
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to pop free item from the AU FIFO");
            ret = -1;
            needUnref = 1;
        }
    }

    if ((ret == 0) && (recordtAuItem))
    {
        ARSTREAM2_H264_AccessUnit_t *au = &recordtAuItem->au;
        ARSTREAM2_H264_NaluFifoItem_t *naluItem;
        memset(&streamReceiver->recorder.accessUnit, 0, sizeof(ARSTREAM2_StreamRecorder_AccessUnit_t));
        streamReceiver->recorder.accessUnit.timestamp = au->ntpTimestamp;
        streamReceiver->recorder.accessUnit.index = streamReceiver->recorder.auCount++;
        for (naluItem = au->naluHead, streamReceiver->recorder.accessUnit.naluCount = 0; naluItem; naluItem = naluItem->next, streamReceiver->recorder.accessUnit.naluCount++)
        {
            streamReceiver->recorder.accessUnit.naluData[streamReceiver->recorder.accessUnit.naluCount] = naluItem->nalu.nalu;
            streamReceiver->recorder.accessUnit.naluSize[streamReceiver->recorder.accessUnit.naluCount] = naluItem->nalu.naluSize;
        }
        /* map the access unit sync type */
        switch (au->syncType)
        {
            default:
            case ARSTREAM2_H264_AU_SYNC_TYPE_NONE:
                streamReceiver->recorder.accessUnit.auSyncType = ARSTREAM2_STREAM_RECEIVER_AU_SYNC_TYPE_NONE;
                break;
            case ARSTREAM2_H264_AU_SYNC_TYPE_IDR:
                streamReceiver->recorder.accessUnit.auSyncType = ARSTREAM2_STREAM_RECEIVER_AU_SYNC_TYPE_IDR;
                break;
            case ARSTREAM2_H264_AU_SYNC_TYPE_IFRAME:
                streamReceiver->recorder.accessUnit.auSyncType = ARSTREAM2_STREAM_RECEIVER_AU_SYNC_TYPE_IFRAME;
                break;
            case ARSTREAM2_H264_AU_SYNC_TYPE_PIR_START:
                streamReceiver->recorder.accessUnit.auSyncType = ARSTREAM2_STREAM_RECEIVER_AU_SYNC_TYPE_PIR_START;
                break;
        }
        streamReceiver->recorder.accessUnit.auMetadata = au->buffer->metadataBuffer;
        streamReceiver->recorder.accessUnit.auMetadataSize = au->metadataSize;
        streamReceiver->recorder.accessUnit.auUserPtr = recordtAuItem;
        eARSTREAM2_ERROR recErr = ARSTREAM2_StreamRecorder_PushAccessUnit(streamReceiver->recorder.recorder, &streamReceiver->recorder.accessUnit);
        if (recErr != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_StreamRecorder_PushAccessUnit() failed: %d (%s)",
                        recErr, ARSTREAM2_Error_ToString(recErr));
            err = -1;
            needUnref = 1;
            needFree = 1;
        }
    }
    else
    {
        err = -1;
    }

    /* error handling */
    if (needFree)
    {
        ret = ARSTREAM2_H264_AuFifoPushFreeItem(&streamReceiver->auFifo, recordtAuItem);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the AU FIFO (%d)", ret);
        }
        needFree = 0;
    }
    if (needUnref)
    {
        ret = ARSTREAM2_H264_AuFifoUnrefBuffer(&streamReceiver->auFifo, auItem->au.buffer);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to unref buffer (%d)", ret);
        }
        needUnref = 0;
    }

    return err;
}


static int ARSTREAM2_StreamReceiver_GenerateGrayIdrFrame(ARSTREAM2_StreamReceiver_t *streamReceiver, ARSTREAM2_H264_AccessUnit_t *nextAu)
{
    int err = 0, ret;

    ARSTREAM2_H264_AuFifoBuffer_t *auBuffer = ARSTREAM2_H264_AuFifoGetBuffer(&streamReceiver->auFifo);
    ARSTREAM2_H264_AuFifoItem_t *auItem = ARSTREAM2_H264_AuFifoPopFreeItem(&streamReceiver->auFifo);

    if ((!auBuffer) || (!auItem))
    {
        if (auBuffer)
        {
            ret = ARSTREAM2_H264_AuFifoUnrefBuffer(&streamReceiver->auFifo, auBuffer);
            if (ret != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to unref buffer (%d)", ret);
            }
            auBuffer = NULL;
        }
        if (auItem)
        {
            ret = ARSTREAM2_H264_AuFifoPushFreeItem(&streamReceiver->auFifo, auItem);
            if (ret != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the AU FIFO (%d)", ret);
            }
            auItem = NULL;
        }
        ret = ARSTREAM2_H264_AuFifoFlush(&streamReceiver->auFifo);
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "AU FIFO is full, cannot generate gray I-frame => flush to recover (%d AU flushed)", ret);
    }
    else
    {
        ARSTREAM2_H264_AuReset(&auItem->au);
        auItem->au.buffer = auBuffer;

        ret = ARSTREAM2_H264FilterError_GenerateGrayIdrFrame(streamReceiver->filter, nextAu, auItem);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264FilterError_GenerateGrayIdrFrame() failed (%d)", ret);

            if (auBuffer)
            {
                ret = ARSTREAM2_H264_AuFifoUnrefBuffer(&streamReceiver->auFifo, auBuffer);
                if (ret != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to unref buffer (%d)", ret);
                }
                auBuffer = NULL;
            }
            if (auItem)
            {
                ret = ARSTREAM2_H264_AuFifoPushFreeItem(&streamReceiver->auFifo, auItem);
                if (ret != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the AU FIFO (%d)", ret);
                }
                auItem = NULL;
            }
        }
    }

    if (auItem)
    {
        /* application output */
        ARSAL_Mutex_Lock(&(streamReceiver->appOutput.threadMutex));
        int appOutputRunning = streamReceiver->appOutput.running;
        ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.threadMutex));
        if ((appOutputRunning) && (streamReceiver->appOutput.grayIFramePending))
        {
            ret = ARSTREAM2_StreamReceiver_AppOutputAuEnqueue(streamReceiver, auItem);
            if (ret < 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_StreamReceiver_AppOutputAuEnqueue() failed (%d)", ret);
            }
            else
            {
                streamReceiver->appOutput.grayIFramePending = 0;
            }
        }

        /* stream recording */
        ARSAL_Mutex_Lock(&(streamReceiver->recorder.mutex));
        int recorderRunning = streamReceiver->recorder.running;
        ARSAL_Mutex_Unlock(&(streamReceiver->recorder.mutex));
        if ((recorderRunning) && (streamReceiver->recorder.grayIFramePending))
        {
            ret = ARSTREAM2_StreamReceiver_RecorderAuEnqueue(streamReceiver, auItem);
            if (ret < 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_StreamReceiver_RecorderAuEnqueue() failed (%d)", ret);
            }
            else
            {
                streamReceiver->recorder.grayIFramePending = 0;
            }
        }

        /* free the access unit */
        ret = ARSTREAM2_H264_AuFifoUnrefBuffer(&streamReceiver->auFifo, auItem->au.buffer);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to unref buffer (%d)", ret);
        }
        ret = ARSTREAM2_H264_AuFifoPushFreeItem(&streamReceiver->auFifo, auItem);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the AU FIFO (%d)", ret);
        }
    }

    return err;
}


static int ARSTREAM2_StreamReceiver_RtpReceiverAuCallback(ARSTREAM2_H264_AuFifoItem_t *auItem, void *userPtr)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)userPtr;
    int err = 0, ret;

    if ((!auItem) || (!userPtr))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid pointer");
        return -1;
    }

    ret = ARSTREAM2_H264Filter_ProcessAu(streamReceiver->filter, &auItem->au);
    if (ret == 1)
    {
        /* gray IDR frame generation */
        if ((streamReceiver->appOutput.grayIFramePending) || (streamReceiver->recorder.grayIFramePending))
        {
            ret = ARSTREAM2_StreamReceiver_GenerateGrayIdrFrame(streamReceiver, &auItem->au);
            if (ret < 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_StreamReceiver_GenerateGrayIdrFrame() failed (%d)", ret);
            }
        }

        if (auItem->au.videoStatsAvailable)
        {
            ARSTREAM2_H264_VideoStats_t *vs = (ARSTREAM2_H264_VideoStats_t*)auItem->au.buffer->videoStatsBuffer;
            uint32_t ntpTimestampDelta = ((auItem->au.ntpTimestamp) && (streamReceiver->lastAuNtpTimestamp))
                    ? (uint32_t)(auItem->au.ntpTimestamp - streamReceiver->lastAuNtpTimestamp) : 0;
            uint32_t ntpTimestampRawDelta = ((auItem->au.ntpTimestampRaw) && (streamReceiver->lastAuNtpTimestampRaw))
                    ? (uint32_t)(auItem->au.ntpTimestampRaw - streamReceiver->lastAuNtpTimestampRaw) : 0;
            vs->timestampDelta = ((auItem->au.ntpTimestamp) && (streamReceiver->lastAuNtpTimestamp))
                    ? ntpTimestampDelta : ((auItem->au.ntpTimestampRaw) && (streamReceiver->lastAuNtpTimestampRaw))
                        ? ntpTimestampRawDelta : 0;
            streamReceiver->timestampDeltaIntegral += vs->timestampDelta;
            vs->timestampDeltaIntegral = streamReceiver->timestampDeltaIntegral;
            streamReceiver->timestampDeltaIntegralSq += (uint64_t)vs->timestampDelta * (uint64_t)vs->timestampDelta;
            vs->timestampDeltaIntegralSq = streamReceiver->timestampDeltaIntegralSq;
        }
        streamReceiver->lastAuNtpTimestamp = auItem->au.ntpTimestamp;
        streamReceiver->lastAuNtpTimestampRaw = auItem->au.ntpTimestampRaw;

        /* application output */
        ARSAL_Mutex_Lock(&(streamReceiver->appOutput.threadMutex));
        int appOutputRunning = streamReceiver->appOutput.running;
        ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.threadMutex));
        if (appOutputRunning)
        {
            ret = ARSTREAM2_StreamReceiver_AppOutputAuEnqueue(streamReceiver, auItem);
            if (ret < 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_StreamReceiver_AppOutputAuEnqueue() failed (%d)", ret);
            }
        }
        else if (auItem->au.videoStatsAvailable)
        {
            struct timespec t1;
            ARSAL_Time_GetTime(&t1);
            uint64_t curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;
            ARSTREAM2_H264_VideoStats_t *vs = (ARSTREAM2_H264_VideoStats_t*)auItem->au.buffer->videoStatsBuffer;
            uint32_t outputTimestampDelta = (streamReceiver->lastAuOutputTimestamp)
                    ? (uint32_t)(curTime - streamReceiver->lastAuOutputTimestamp) : 0;
            uint32_t estimatedLatency = ((auItem->au.ntpTimestampLocal) && (curTime > auItem->au.ntpTimestampLocal))
                    ? (uint32_t)(curTime - auItem->au.ntpTimestampLocal) : 0;
            int32_t timingError = ((vs->timestampDelta) && (streamReceiver->lastAuOutputTimestamp))
                    ? ((int32_t)vs->timestampDelta - (int32_t)outputTimestampDelta) : 0;
            vs->timingError = timingError;
            streamReceiver->timingErrorIntegral += (timingError < 0) ? (uint32_t)(-timingError) : (uint32_t)timingError;
            vs->timingErrorIntegral = streamReceiver->timingErrorIntegral;
            streamReceiver->timingErrorIntegralSq += (int64_t)timingError * (int64_t)timingError;
            vs->timingErrorIntegralSq = streamReceiver->timingErrorIntegralSq;
            vs->estimatedLatency = estimatedLatency;
            streamReceiver->estimatedLatencyIntegral += estimatedLatency;
            vs->estimatedLatencyIntegral = streamReceiver->estimatedLatencyIntegral;
            streamReceiver->estimatedLatencyIntegralSq += (uint64_t)estimatedLatency * (uint64_t)estimatedLatency;
            vs->estimatedLatencyIntegralSq = streamReceiver->estimatedLatencyIntegralSq;
            streamReceiver->lastAuOutputTimestamp = curTime;
            vs->timestamp = auItem->au.ntpTimestampRaw;

            /* get the RSSI from the streaming metadata */
            //TODO: remove this hack once we have a better way of getting the RSSI
            if ((auItem->au.metadataSize >= 27) && (ntohs(*((uint16_t*)auItem->au.buffer->metadataBuffer)) == 0x5031))
            {
                vs->rssi = (int8_t)auItem->au.buffer->metadataBuffer[26];
            }
            if ((auItem->au.metadataSize >= 55) && (ntohs(*((uint16_t*)auItem->au.buffer->metadataBuffer)) == 0x5032))
            {
                vs->rssi = (int8_t)auItem->au.buffer->metadataBuffer[54];
            }

            eARSTREAM2_ERROR recvErr = ARSTREAM2_RtpReceiver_UpdateVideoStats(streamReceiver->receiver, vs);
            if (recvErr != ARSTREAM2_OK)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_RtpReceiver_UpdateVideoStats() failed (%d)", recvErr);
            }
            ARSTREAM2_StreamStats_VideoStatsFileWrite(&streamReceiver->videoStats, vs);
        }

        /* stream recording */
        ARSAL_Mutex_Lock(&(streamReceiver->recorder.mutex));
        int recorderRunning = streamReceiver->recorder.running;
        ARSAL_Mutex_Unlock(&(streamReceiver->recorder.mutex));
        if (recorderRunning)
        {
            ret = ARSTREAM2_StreamReceiver_RecorderAuEnqueue(streamReceiver, auItem);
            if (ret < 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_StreamReceiver_RecorderAuEnqueue() failed (%d)", ret);
            }
        }
    }
    else
    {
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264Filter_ProcessAu() failed (%d)", ret);
        }
    }

    /* free the access unit */
    ret = ARSTREAM2_H264_AuFifoUnrefBuffer(&streamReceiver->auFifo, auItem->au.buffer);
    if (ret != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to unref buffer (%d)", ret);
    }
    ret = ARSTREAM2_H264_AuFifoPushFreeItem(&streamReceiver->auFifo, auItem);
    if (ret != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the AU FIFO (%d)", ret);
    }

    return err;
}


static int ARSTREAM2_StreamReceiver_H264FilterAuCallback(ARSTREAM2_H264_AuFifoItem_t *auItem, void *userPtr)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)userPtr;
    int err = 0, ret;

    if ((!auItem) || (!userPtr))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid pointer");
        return -1;
    }

    streamReceiver->lastAuNtpTimestamp = auItem->au.ntpTimestamp;
    streamReceiver->lastAuNtpTimestampRaw = auItem->au.ntpTimestampRaw;

    /* application output */
    ARSAL_Mutex_Lock(&(streamReceiver->appOutput.threadMutex));
    int appOutputRunning = streamReceiver->appOutput.running;
    ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.threadMutex));
    if (appOutputRunning)
    {
        ret = ARSTREAM2_StreamReceiver_AppOutputAuEnqueue(streamReceiver, auItem);
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_StreamReceiver_AppOutputAuEnqueue() failed (%d)", ret);
        }
    }

    /* stream recording */
    ARSAL_Mutex_Lock(&(streamReceiver->recorder.mutex));
    int recorderRunning = streamReceiver->recorder.running;
    ARSAL_Mutex_Unlock(&(streamReceiver->recorder.mutex));
    if (recorderRunning)
    {
        ret = ARSTREAM2_StreamReceiver_RecorderAuEnqueue(streamReceiver, auItem);
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_StreamReceiver_RecorderAuEnqueue() failed (%d)", ret);
        }
    }

    /* free the access unit */
    ret = ARSTREAM2_H264_AuFifoUnrefBuffer(&streamReceiver->auFifo, auItem->au.buffer);
    if (ret != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to unref buffer (%d)", ret);
    }
    ret = ARSTREAM2_H264_AuFifoPushFreeItem(&streamReceiver->auFifo, auItem);
    if (ret != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the AU FIFO (%d)", ret);
    }

    return err;
}


static int ARSTREAM2_StreamReceiver_H264FilterSpsPpsCallback(uint8_t *spsBuffer, int spsSize, uint8_t *ppsBuffer, int ppsSize, void *userPtr)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)userPtr;
    int ret = 0;
    eARSTREAM2_ERROR cbRet;

    if (!userPtr)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid pointer");
        return -1;
    }

    /* sync */
    if ((spsSize > 0) && (ppsSize > 0))
    {
        streamReceiver->pSps = realloc(streamReceiver->pSps, spsSize);
        if (!streamReceiver->pSps)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Allocation failed");
            return -1;
        }
        streamReceiver->pPps = realloc(streamReceiver->pPps, ppsSize);
        if (!streamReceiver->pPps)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Allocation failed");
            return -1;
        }
        memcpy(streamReceiver->pSps, spsBuffer, spsSize);
        streamReceiver->spsSize = spsSize;
        memcpy(streamReceiver->pPps, ppsBuffer, ppsSize);
        streamReceiver->ppsSize = ppsSize;
        streamReceiver->sync = 1;
    }

    /* stream recording */
    if (streamReceiver->recorder.startPending)
    {
        int recRet;
        recRet = ARSTREAM2_StreamReceiver_StreamRecorderInit(streamReceiver);
        if (recRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264Filter_StreamRecorderInit() failed (%d)", recRet);
        }
        streamReceiver->recorder.startPending = 0;
    }

    ARSAL_Mutex_Lock(&(streamReceiver->appOutput.callbackMutex));
    streamReceiver->appOutput.callbackInProgress = 1;
    if (streamReceiver->appOutput.getAuBufferCallback)
    {
        /* call the spsPpsCallback */
        ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.callbackMutex));

        cbRet = streamReceiver->appOutput.spsPpsCallback(spsBuffer, spsSize, ppsBuffer, ppsSize, streamReceiver->appOutput.spsPpsCallbackUserPtr);
        if (cbRet != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_STREAM_RECEIVER_TAG, "Application SPS/PPS callback failed");
        }

        ARSAL_Mutex_Lock(&(streamReceiver->appOutput.callbackMutex));
    }
    streamReceiver->appOutput.callbackInProgress = 0;
    ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.callbackMutex));
    ARSAL_Cond_Signal(&(streamReceiver->appOutput.callbackCond));

    return ret;
}


static void ARSTREAM2_StreamReceiver_StreamRecorderAuCallback(eARSTREAM2_STREAM_RECORDER_AU_STATUS status, void *auUserPtr, void *userPtr)
{
    ARSTREAM2_StreamReceiver_t *streamReceiver = (ARSTREAM2_StreamReceiver_t*)userPtr;
    ARSTREAM2_H264_AuFifoItem_t *auItem = (ARSTREAM2_H264_AuFifoItem_t*)auUserPtr;
    int ret;

    if (!streamReceiver)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid recorder auCallback user pointer");
        return;
    }
    if (!auItem)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid recorder access unit user pointer");
        return;
    }

    /* free the access unit */
    ret = ARSTREAM2_H264_AuFifoUnrefBuffer(&streamReceiver->auFifo, auItem->au.buffer);
    if (ret != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to unref buffer (%d)", ret);
    }
    ret = ARSTREAM2_H264_AuFifoPushFreeItem(&streamReceiver->auFifo, auItem);
    if (ret != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the AU FIFO (%d)", ret);
    }
}


static int ARSTREAM2_StreamReceiver_StreamRecorderInit(ARSTREAM2_StreamReceiver_t *streamReceiver)
{
    int ret = -1;

    if (!streamReceiver->sync)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "No sync");
        return -1;
    }

    if ((!streamReceiver->recorder.recorder) && (streamReceiver->recorder.fileName))
    {
        eARSTREAM2_ERROR recErr;
        int width = 0, height = 0;
        float framerate = 0.0;
        ARSTREAM2_StreamRecorder_Config_t recConfig;
        memset(&recConfig, 0, sizeof(ARSTREAM2_StreamRecorder_Config_t));
        recConfig.mediaFileName = streamReceiver->recorder.fileName;
        int err = ARSTREAM2_H264Filter_GetVideoParams(streamReceiver->filter, NULL, NULL, &width, &height, &framerate);
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264Filter_GetVideoParams() failed (%d)",err);
        }
        recConfig.videoFramerate = framerate;
        recConfig.videoWidth = (uint32_t)width;
        recConfig.videoHeight = (uint32_t)height;
        recConfig.sps = streamReceiver->pSps;
        recConfig.spsSize = streamReceiver->spsSize;
        recConfig.pps = streamReceiver->pPps;
        recConfig.ppsSize = streamReceiver->ppsSize;
        recConfig.serviceType = 0; //TODO
        recConfig.auFifoSize = streamReceiver->auFifo.bufferPoolSize;
        recConfig.auCallback = ARSTREAM2_StreamReceiver_StreamRecorderAuCallback;
        recConfig.auCallbackUserPtr = streamReceiver;
        recErr = ARSTREAM2_StreamRecorder_Init(&streamReceiver->recorder.recorder, &recConfig);
        if (recErr != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_StreamRecorder_Init() failed (%d): %s",
                        recErr, ARSTREAM2_Error_ToString(recErr));
        }
        else
        {
            int thErr = ARSAL_Thread_Create(&streamReceiver->recorder.thread, ARSTREAM2_StreamRecorder_RunThread, (void*)streamReceiver->recorder.recorder);
            if (thErr != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Recorder thread creation failed (%d)", thErr);
            }
            else
            {
                ret = 0;
                streamReceiver->recorder.auCount = 0;
                ARSAL_Mutex_Lock(&(streamReceiver->recorder.mutex));
                streamReceiver->recorder.grayIFramePending = 1;
                streamReceiver->recorder.running = 1;
                ARSAL_Mutex_Unlock(&(streamReceiver->recorder.mutex));
            }
        }
    }

    return ret;
}


static int ARSTREAM2_StreamReceiver_StreamRecorderStop(ARSTREAM2_StreamReceiver_t *streamReceiver)
{
    int ret = 0;

    if (streamReceiver->recorder.recorder)
    {
        eARSTREAM2_ERROR err;
        err = ARSTREAM2_StreamRecorder_Stop(streamReceiver->recorder.recorder);
        if (err != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_StreamRecorder_Stop() failed: %d (%s)",
                        err, ARSTREAM2_Error_ToString(err));
            ret = -1;
        }
        else
        {
            ARSAL_Mutex_Lock(&(streamReceiver->recorder.mutex));
            streamReceiver->recorder.running = 0;
            ARSAL_Mutex_Unlock(&(streamReceiver->recorder.mutex));
        }
    }

    return ret;
}


static int ARSTREAM2_StreamReceiver_StreamRecorderFree(ARSTREAM2_StreamReceiver_t *streamReceiver)
{
    int ret = 0;

    if (streamReceiver->recorder.recorder)
    {
        int thErr;
        eARSTREAM2_ERROR err;
        ARSAL_Mutex_Lock(&(streamReceiver->recorder.mutex));
        int recRunning = streamReceiver->recorder.running;
        ARSAL_Mutex_Unlock(&(streamReceiver->recorder.mutex));
        if (recRunning)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Stream recorder is not stopped, cannot free");
            return -1;
        }
        if (streamReceiver->recorder.thread)
        {
            thErr = ARSAL_Thread_Join(streamReceiver->recorder.thread, NULL);
            if (thErr != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSAL_Thread_Join() failed (%d)", thErr);
                ret = -1;
            }
            thErr = ARSAL_Thread_Destroy(&streamReceiver->recorder.thread);
            if (thErr != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSAL_Thread_Destroy() failed (%d)", thErr);
                ret = -1;
            }
            streamReceiver->recorder.thread = NULL;
        }
        err = ARSTREAM2_StreamRecorder_Free(&streamReceiver->recorder.recorder);
        if (err != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_StreamRecorder_Free() failed (%d): %s",
                        err, ARSTREAM2_Error_ToString(err));
            ret = -1;
        }
    }

    return ret;
}


void* ARSTREAM2_StreamReceiver_RunAppOutputThread(void *streamReceiverHandle)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)streamReceiverHandle;
    int shouldStop, running, ret;
    struct timespec t1;
    uint64_t curTime;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid handle");
        return NULL;
    }

    streamReceiver->appOutput.threadRunning = 1;
    ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_STREAM_RECEIVER_TAG, "App output thread running");

    ARSAL_Mutex_Lock(&(streamReceiver->appOutput.threadMutex));
    shouldStop = streamReceiver->appOutput.threadShouldStop;
    running = streamReceiver->appOutput.running;
    ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.threadMutex));

    while (shouldStop == 0)
    {
        ARSTREAM2_H264_AuFifoItem_t *auItem;

        ARSAL_Time_GetTime(&t1);
        curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

        /* dequeue an access unit */
        ARSAL_Mutex_Lock(&(streamReceiver->appOutput.threadMutex));
        auItem = ARSTREAM2_H264_AuFifoDequeueItem(&streamReceiver->appOutput.auFifoQueue);
        ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.threadMutex));

        while (auItem != NULL)
        {
            ARSTREAM2_H264_AccessUnit_t *au = &auItem->au;
            ARSTREAM2_H264_NaluFifoItem_t *naluItem;
            unsigned int auSize = 0;

            if ((streamReceiver->appOutput.mbWidth == 0) || (streamReceiver->appOutput.mbHeight == 0))
            {
                int mbWidth = 0, mbHeight = 0;
                int err = ARSTREAM2_H264Filter_GetVideoParams(streamReceiver->filter, &mbWidth, &mbHeight, NULL, NULL, NULL);
                if (err != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264Filter_GetVideoParams() failed (%d)",err);
                }
                streamReceiver->appOutput.mbWidth = mbWidth;
                streamReceiver->appOutput.mbHeight = mbHeight;
            }

            /* pre-check the access unit size to avoid calling getAuBufferCallback+auReadyCallback for null sized frames */
            for (naluItem = au->naluHead; naluItem; naluItem = naluItem->next)
            {
                /* filter out unwanted NAL units */
                if ((streamReceiver->appOutput.filterOutSpsPps) && ((naluItem->nalu.naluType == ARSTREAM2_H264_NALU_TYPE_SPS) || (naluItem->nalu.naluType == ARSTREAM2_H264_NALU_TYPE_PPS)))
                {
                    continue;
                }
                if ((streamReceiver->appOutput.filterOutSei) && (naluItem->nalu.naluType == ARSTREAM2_H264_NALU_TYPE_SEI))
                {
                    continue;
                }

                auSize += naluItem->nalu.naluSize;
            }

            if ((running) && (auSize > 0))
            {
                eARSTREAM2_ERROR cbRet = ARSTREAM2_OK;
                uint8_t *auBuffer = NULL;
                int auBufferSize = 0;
                void *auBufferUserPtr = NULL;
                ARSTREAM2_StreamReceiver_AuReadyCallbackTimestamps_t auTimestamps;
                ARSTREAM2_StreamReceiver_AuReadyCallbackMetadata_t auMetadata;
                ARSTREAM2_StreamReceiver_VideoStats_t videoStats;

                ARSAL_Mutex_Lock(&(streamReceiver->appOutput.callbackMutex));
                streamReceiver->appOutput.callbackInProgress = 1;
                if (streamReceiver->appOutput.getAuBufferCallback)
                {
                    /* call the getAuBufferCallback */
                    ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.callbackMutex));

                    cbRet = streamReceiver->appOutput.getAuBufferCallback(&auBuffer, &auBufferSize, &auBufferUserPtr, streamReceiver->appOutput.getAuBufferCallbackUserPtr);

                    ARSAL_Mutex_Lock(&(streamReceiver->appOutput.callbackMutex));
                }

                if ((cbRet != ARSTREAM2_OK) || (!auBuffer) || (auBufferSize <= 0))
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "getAuBufferCallback failed: %s", ARSTREAM2_Error_ToString(cbRet));
                    streamReceiver->appOutput.callbackInProgress = 0;
                    ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.callbackMutex));
                    ARSAL_Cond_Signal(&(streamReceiver->appOutput.callbackCond));
                }
                else
                {
                    auSize = 0;

                    for (naluItem = au->naluHead; naluItem; naluItem = naluItem->next)
                    {
                        /* filter out unwanted NAL units */
                        if ((streamReceiver->appOutput.filterOutSpsPps) && ((naluItem->nalu.naluType == ARSTREAM2_H264_NALU_TYPE_SPS) || (naluItem->nalu.naluType == ARSTREAM2_H264_NALU_TYPE_PPS)))
                        {
                            continue;
                        }

                        if ((streamReceiver->appOutput.filterOutSei) && (naluItem->nalu.naluType == ARSTREAM2_H264_NALU_TYPE_SEI))
                        {
                            continue;
                        }

                        /* copy to output buffer */
                        if (auSize + naluItem->nalu.naluSize <= (unsigned)auBufferSize)
                        {
                            memcpy(auBuffer + auSize, naluItem->nalu.nalu, naluItem->nalu.naluSize);

                            if ((naluItem->nalu.naluSize >= 4) && (streamReceiver->appOutput.replaceStartCodesWithNaluSize))
                            {
                                /* replace the NAL unit 4 bytes start code with the NALU size */
                                *(auBuffer + auSize + 0) = ((naluItem->nalu.naluSize - 4) >> 24) & 0xFF;
                                *(auBuffer + auSize + 1) = ((naluItem->nalu.naluSize - 4) >> 16) & 0xFF;
                                *(auBuffer + auSize + 2) = ((naluItem->nalu.naluSize - 4) >>  8) & 0xFF;
                                *(auBuffer + auSize + 3) = ((naluItem->nalu.naluSize - 4) >>  0) & 0xFF;
                            }

                            auSize += naluItem->nalu.naluSize;
                        }
                        else
                        {
                            break;
                        }
                    }

                    /* map the access unit sync type */
                    eARSTREAM2_STREAM_RECEIVER_AU_SYNC_TYPE auSyncType;
                    switch (au->syncType)
                    {
                        default:
                        case ARSTREAM2_H264_AU_SYNC_TYPE_NONE:
                            auSyncType = ARSTREAM2_STREAM_RECEIVER_AU_SYNC_TYPE_NONE;
                            break;
                        case ARSTREAM2_H264_AU_SYNC_TYPE_IDR:
                            auSyncType = ARSTREAM2_STREAM_RECEIVER_AU_SYNC_TYPE_IDR;
                            break;
                        case ARSTREAM2_H264_AU_SYNC_TYPE_IFRAME:
                            auSyncType = ARSTREAM2_STREAM_RECEIVER_AU_SYNC_TYPE_IFRAME;
                            break;
                        case ARSTREAM2_H264_AU_SYNC_TYPE_PIR_START:
                            auSyncType = ARSTREAM2_STREAM_RECEIVER_AU_SYNC_TYPE_PIR_START;
                            break;
                    }

                    ARSAL_Time_GetTime(&t1);
                    curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;
                    if (au->videoStatsAvailable)
                    {
                        ARSTREAM2_H264_VideoStats_t *vs = (ARSTREAM2_H264_VideoStats_t*)au->buffer->videoStatsBuffer;
                        uint32_t outputTimestampDelta = (streamReceiver->lastAuOutputTimestamp)
                                ? (uint32_t)(curTime - streamReceiver->lastAuOutputTimestamp) : 0;
                        uint32_t estimatedLatency = ((au->ntpTimestampLocal) && (curTime > au->ntpTimestampLocal))
                                ? (uint32_t)(curTime - au->ntpTimestampLocal) : 0;
                        int32_t timingError = ((vs->timestampDelta) && (streamReceiver->lastAuOutputTimestamp))
                                ? ((int32_t)vs->timestampDelta - (int32_t)outputTimestampDelta) : 0;
                        vs->timingError = timingError;
                        streamReceiver->timingErrorIntegral += (timingError < 0) ? (uint32_t)(-timingError) : (uint32_t)timingError;
                        vs->timingErrorIntegral = streamReceiver->timingErrorIntegral;
                        streamReceiver->timingErrorIntegralSq += (int64_t)timingError * (int64_t)timingError;
                        vs->timingErrorIntegralSq = streamReceiver->timingErrorIntegralSq;
                        vs->estimatedLatency = estimatedLatency;
                        streamReceiver->estimatedLatencyIntegral += estimatedLatency;
                        vs->estimatedLatencyIntegral = streamReceiver->estimatedLatencyIntegral;
                        streamReceiver->estimatedLatencyIntegralSq += (uint64_t)estimatedLatency * (uint64_t)estimatedLatency;
                        vs->estimatedLatencyIntegralSq = streamReceiver->estimatedLatencyIntegralSq;
                        vs->timestamp = au->ntpTimestampRaw;

                        /* get the RSSI from the streaming metadata */
                        //TODO: remove this hack once we have a better way of getting the RSSI
                        if ((au->metadataSize >= 27) && (ntohs(*((uint16_t*)au->buffer->metadataBuffer)) == 0x5031))
                        {
                            vs->rssi = (int8_t)au->buffer->metadataBuffer[26];
                        }
                        if ((au->metadataSize >= 55) && (ntohs(*((uint16_t*)au->buffer->metadataBuffer)) == 0x5032))
                        {
                            vs->rssi = (int8_t)au->buffer->metadataBuffer[54];
                        }

                        eARSTREAM2_ERROR recvErr = ARSTREAM2_RtpReceiver_UpdateVideoStats(streamReceiver->receiver, vs);
                        if (recvErr != ARSTREAM2_OK)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_RtpReceiver_UpdateVideoStats() failed (%d)", recvErr);
                        }
                        ARSTREAM2_StreamStats_VideoStatsFileWrite(&streamReceiver->videoStats, vs);
                    }

                    /* timestamps and metadata */
                    memset(&auTimestamps, 0, sizeof(auTimestamps));
                    memset(&auMetadata, 0, sizeof(auMetadata));
                    auTimestamps.auNtpTimestamp = au->ntpTimestamp;
                    auTimestamps.auNtpTimestampRaw = au->ntpTimestampRaw;
                    auTimestamps.auNtpTimestampLocal = au->ntpTimestampLocal;
                    auMetadata.isComplete = au->isComplete;
                    auMetadata.hasErrors = au->hasErrors;
                    auMetadata.isRef = au->isRef;
                    auMetadata.auMetadata = (au->metadataSize > 0) ? au->buffer->metadataBuffer : NULL;
                    auMetadata.auMetadataSize = au->metadataSize;
                    auMetadata.auUserData = (au->userDataSize > 0) ? au->buffer->userDataBuffer : NULL;
                    auMetadata.auUserDataSize = au->userDataSize;
                    auMetadata.mbWidth = streamReceiver->appOutput.mbWidth;
                    auMetadata.mbHeight = streamReceiver->appOutput.mbHeight;
                    auMetadata.mbStatus = (au->mbStatusAvailable) ? au->buffer->mbStatusBuffer : NULL;
                    if (au->videoStatsAvailable)
                    {
                        /* Map the video stats */
                        ARSTREAM2_H264_VideoStats_t *vs = (ARSTREAM2_H264_VideoStats_t*)au->buffer->videoStatsBuffer;
                        int i, j;
                        memset(&videoStats, 0, sizeof(videoStats));
                        videoStats.timestamp = vs->timestamp;
                        videoStats.rssi = vs->rssi;
                        videoStats.totalFrameCount = vs->totalFrameCount;
                        videoStats.outputFrameCount = vs->outputFrameCount;
                        videoStats.erroredOutputFrameCount = vs->erroredOutputFrameCount;
                        videoStats.missedFrameCount = vs->missedFrameCount;
                        videoStats.discardedFrameCount = vs->discardedFrameCount;
                        videoStats.erroredSecondCount = vs->erroredSecondCount;
                        videoStats.timestampDeltaIntegral = vs->timestampDeltaIntegral;
                        videoStats.timestampDeltaIntegralSq = vs->timestampDeltaIntegralSq;
                        videoStats.timingErrorIntegral = vs->timingErrorIntegral;
                        videoStats.timingErrorIntegralSq = vs->timingErrorIntegralSq;
                        videoStats.estimatedLatencyIntegral = vs->estimatedLatencyIntegral;
                        videoStats.estimatedLatencyIntegralSq = vs->estimatedLatencyIntegralSq;

#if ARSTREAM2_STREAM_RECEIVER_MB_STATUS_ZONE_COUNT != ARSTREAM2_H264_MB_STATUS_ZONE_COUNT
    #error "MB_STATUS_ZONE_COUNT mismatch!"
#endif
#if ARSTREAM2_STREAM_RECEIVER_MB_STATUS_CLASS_COUNT != ARSTREAM2_H264_MB_STATUS_CLASS_COUNT
    #error "MB_STATUS_CLASS_COUNT mismatch!"
#endif

                        for (i = 0; i < ARSTREAM2_STREAM_RECEIVER_MB_STATUS_ZONE_COUNT; i++)
                        {
                            videoStats.erroredSecondCountByZone[i] = vs->erroredSecondCountByZone[i];
                        }
                        for (j = 0; j < ARSTREAM2_STREAM_RECEIVER_MB_STATUS_CLASS_COUNT; j++)
                        {
                            for (i = 0; i < ARSTREAM2_STREAM_RECEIVER_MB_STATUS_ZONE_COUNT; i++)
                            {
                                videoStats.macroblockStatus[j][i] = vs->macroblockStatus[j][i];
                            }
                        }
                        auMetadata.videoStats = &videoStats;
                    }
                    else
                    {
                        auMetadata.videoStats = NULL;
                    }
                    auMetadata.debugString = NULL; //TODO

                    if (streamReceiver->appOutput.auReadyCallback)
                    {
                        /* call the auReadyCallback */
                        ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.callbackMutex));

                        cbRet = streamReceiver->appOutput.auReadyCallback(auBuffer, auSize, &auTimestamps, auSyncType, &auMetadata,
                                                                          auBufferUserPtr, streamReceiver->appOutput.auReadyCallbackUserPtr);

                        ARSAL_Mutex_Lock(&(streamReceiver->appOutput.callbackMutex));
                    }
                    streamReceiver->appOutput.callbackInProgress = 0;
                    ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.callbackMutex));
                    ARSAL_Cond_Signal(&(streamReceiver->appOutput.callbackCond));

                    if (cbRet != ARSTREAM2_OK)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_STREAM_RECEIVER_TAG, "auReadyCallback failed: %s", ARSTREAM2_Error_ToString(cbRet));
                        if (cbRet == ARSTREAM2_ERROR_RESYNC_REQUIRED)
                        {
                            /* schedule gray IDR frame */
                            streamReceiver->appOutput.grayIFramePending = 1;
                        }
                    }
                    streamReceiver->lastAuOutputTimestamp = curTime;
                }
            }

            /* free the access unit */
            ret = ARSTREAM2_H264_AuFifoUnrefBuffer(&streamReceiver->auFifo, auItem->au.buffer);
            if (ret != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to unref buffer (%d)", ret);
            }
            ret = ARSTREAM2_H264_AuFifoPushFreeItem(&streamReceiver->auFifo, auItem);
            if (ret != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the AU FIFO (%d)", ret);
            }

            /* dequeue the next access unit */
            ARSAL_Mutex_Lock(&(streamReceiver->appOutput.threadMutex));
            auItem = ARSTREAM2_H264_AuFifoDequeueItem(&streamReceiver->appOutput.auFifoQueue);
            ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.threadMutex));
        }

        ARSAL_Mutex_Lock(&(streamReceiver->appOutput.threadMutex));
        shouldStop = streamReceiver->appOutput.threadShouldStop;
        running = streamReceiver->appOutput.running;
        if (!shouldStop)
        {
            ARSAL_Cond_Wait(&(streamReceiver->appOutput.threadCond), &(streamReceiver->appOutput.threadMutex));
        }
        ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.threadMutex));
    }

    ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_STREAM_RECEIVER_TAG, "App output thread has ended");
    streamReceiver->appOutput.threadRunning = 0;

    return (void*)0;
}


void* ARSTREAM2_StreamReceiver_RunNetworkThread(void *streamReceiverHandle)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)streamReceiverHandle;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid handle");
        return NULL;
    }

    return ARSTREAM2_RtpReceiver_RunThread((void*)streamReceiver->receiver);
}


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_StartAppOutput(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle,
                                                         ARSTREAM2_StreamReceiver_SpsPpsCallback_t spsPpsCallback, void* spsPpsCallbackUserPtr,
                                                         ARSTREAM2_StreamReceiver_GetAuBufferCallback_t getAuBufferCallback, void* getAuBufferCallbackUserPtr,
                                                         ARSTREAM2_StreamReceiver_AuReadyCallback_t auReadyCallback, void* auReadyCallbackUserPtr)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)streamReceiverHandle;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!getAuBufferCallback)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid getAuBufferCallback function pointer");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!auReadyCallback)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid auReadyCallback function pointer");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    ARSAL_Mutex_Lock(&(streamReceiver->appOutput.threadMutex));
    int running = streamReceiver->appOutput.running;
    ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.threadMutex));
    if (running)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Application output is already running");
        return ARSTREAM2_ERROR_INVALID_STATE;
    }

    ARSAL_Mutex_Lock(&(streamReceiver->appOutput.threadMutex));
    int auFifoRet = ARSTREAM2_H264_AuFifoAddQueue(&streamReceiver->auFifo, &streamReceiver->appOutput.auFifoQueue);
    if (auFifoRet != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264_AuFifoAddQueue() failed (%d)", auFifoRet);
        ret = ARSTREAM2_ERROR_ALLOC;
    }
    ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.threadMutex));

    ARSAL_Mutex_Lock(&(streamReceiver->appOutput.callbackMutex));
    while (streamReceiver->appOutput.callbackInProgress)
    {
        ARSAL_Cond_Wait(&(streamReceiver->appOutput.callbackCond), &(streamReceiver->appOutput.callbackMutex));
    }
    streamReceiver->appOutput.spsPpsCallback = spsPpsCallback;
    streamReceiver->appOutput.spsPpsCallbackUserPtr = spsPpsCallbackUserPtr;
    streamReceiver->appOutput.getAuBufferCallback = getAuBufferCallback;
    streamReceiver->appOutput.getAuBufferCallbackUserPtr = getAuBufferCallbackUserPtr;
    streamReceiver->appOutput.auReadyCallback = auReadyCallback;
    streamReceiver->appOutput.auReadyCallbackUserPtr = auReadyCallbackUserPtr;
    ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.callbackMutex));

    ARSAL_Mutex_Lock(&(streamReceiver->appOutput.threadMutex));
    streamReceiver->appOutput.grayIFramePending = 1;
    streamReceiver->appOutput.running = 1;
    ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.threadMutex));

    ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_STREAM_RECEIVER_TAG, "App output is running");

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_StopAppOutput(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)streamReceiverHandle;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    ARSAL_Mutex_Lock(&(streamReceiver->appOutput.threadMutex));
    streamReceiver->appOutput.running = 0;
    ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.threadMutex));

    ARSAL_Mutex_Lock(&(streamReceiver->appOutput.callbackMutex));
    while (streamReceiver->appOutput.callbackInProgress)
    {
        ARSAL_Cond_Wait(&(streamReceiver->appOutput.callbackCond), &(streamReceiver->appOutput.callbackMutex));
    }
    streamReceiver->appOutput.spsPpsCallback = NULL;
    streamReceiver->appOutput.spsPpsCallbackUserPtr = NULL;
    streamReceiver->appOutput.getAuBufferCallback = NULL;
    streamReceiver->appOutput.getAuBufferCallbackUserPtr = NULL;
    streamReceiver->appOutput.auReadyCallback = NULL;
    streamReceiver->appOutput.auReadyCallbackUserPtr = NULL;
    int err = ARSTREAM2_H264Filter_ForceResync(streamReceiver->filter);
    if (err != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264Filter_ForceResync() failed (%d)", err);
    }
    ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.callbackMutex));

    ARSAL_Mutex_Lock(&(streamReceiver->appOutput.threadMutex));
    int auFifoRet = ARSTREAM2_H264_AuFifoRemoveQueue(&streamReceiver->auFifo, &streamReceiver->appOutput.auFifoQueue);
    if (auFifoRet != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264_AuFifoRemoveQueue() failed (%d)", auFifoRet);
        ret = ARSTREAM2_ERROR_ALLOC;
    }
    ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.threadMutex));

    ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_STREAM_RECEIVER_TAG, "App output is stopped");

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_Stop(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)streamReceiverHandle;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    int recErr = ARSTREAM2_StreamReceiver_StreamRecorderStop(streamReceiver);
    if (recErr != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_StreamReceiver_StreamRecorderStop() failed (%d)", recErr);
    }

    ARSTREAM2_RtpReceiver_Stop(streamReceiver->receiver);

    ARSAL_Mutex_Lock(&(streamReceiver->appOutput.threadMutex));
    streamReceiver->appOutput.threadShouldStop = 1;
    ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.threadMutex));
    /* signal the thread to avoid a deadlock */
    ARSAL_Cond_Signal(&(streamReceiver->appOutput.threadCond));

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_GetSpsPps(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle, uint8_t *spsBuffer, int *spsSize, uint8_t *ppsBuffer, int *ppsSize)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)streamReceiverHandle;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    if ((!spsSize) || (!ppsSize))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid size pointers");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    if (!streamReceiver->sync)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "No sync");
        ret = ARSTREAM2_ERROR_WAITING_FOR_SYNC;
    }

    if (ret == ARSTREAM2_OK)
    {
        if ((!spsBuffer) || (*spsSize < streamReceiver->spsSize))
        {
            *spsSize = streamReceiver->spsSize;
        }
        else
        {
            memcpy(spsBuffer, streamReceiver->pSps, streamReceiver->spsSize);
            *spsSize = streamReceiver->spsSize;
        }

        if ((!ppsBuffer) || (*ppsSize < streamReceiver->ppsSize))
        {
            *ppsSize = streamReceiver->ppsSize;
        }
        else
        {
            memcpy(ppsBuffer, streamReceiver->pPps, streamReceiver->ppsSize);
            *ppsSize = streamReceiver->ppsSize;
        }
    }

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_StartResender(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle,
                                                        ARSTREAM2_StreamReceiver_ResenderHandle *resenderHandle,
                                                        const ARSTREAM2_StreamReceiver_ResenderConfig_t *config)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)streamReceiverHandle;
    ARSTREAM2_RtpReceiver_RtpResender_t* retResender = NULL;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!resenderHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid pointer for resender");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!config)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid pointer for config");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    ARSTREAM2_RtpReceiver_RtpResender_Config_t resenderConfig;
    memset(&resenderConfig, 0, sizeof(resenderConfig));

    resenderConfig.canonicalName = config->canonicalName;
    resenderConfig.friendlyName = config->friendlyName;
    resenderConfig.clientAddr = config->clientAddr;
    resenderConfig.mcastAddr = config->mcastAddr;
    resenderConfig.mcastIfaceAddr = config->mcastIfaceAddr;
    resenderConfig.serverStreamPort = config->serverStreamPort;
    resenderConfig.serverControlPort = config->serverControlPort;
    resenderConfig.clientStreamPort = config->clientStreamPort;
    resenderConfig.clientControlPort = config->clientControlPort;
    resenderConfig.classSelector = config->classSelector;
    resenderConfig.maxPacketSize = config->maxPacketSize;
    resenderConfig.streamSocketBufferSize = config->streamSocketBufferSize;
    resenderConfig.maxNetworkLatencyMs = config->maxNetworkLatencyMs;
    resenderConfig.useRtpHeaderExtensions = config->useRtpHeaderExtensions;

    retResender = ARSTREAM2_RtpReceiverResender_New(streamReceiver->receiver, &resenderConfig, &ret);
    if (ret != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Error while creating resender : %s", ARSTREAM2_Error_ToString(ret));
    }
    else
    {
        *resenderHandle = (ARSTREAM2_StreamReceiver_ResenderHandle)retResender;
    }

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_StopResender(ARSTREAM2_StreamReceiver_ResenderHandle resenderHandle)
{
    ARSTREAM2_RtpReceiverResender_Stop((ARSTREAM2_RtpReceiver_RtpResender_t*)resenderHandle);

    return ARSTREAM2_OK;
}


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_FreeResender(ARSTREAM2_StreamReceiver_ResenderHandle *resenderHandle)
{
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;

    ret = ARSTREAM2_RtpReceiverResender_Delete((ARSTREAM2_RtpReceiver_RtpResender_t**)resenderHandle);
    if (ret != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Error while deleting resender : %s", ARSTREAM2_Error_ToString(ret));
    }

    return ret;
}


void* ARSTREAM2_StreamReceiver_RunResenderThread(void *resenderHandle)
{
    return ARSTREAM2_RtpReceiverResender_RunThread(resenderHandle);
}


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_StartRecorder(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle, const char *recordFileName)
{
    ARSTREAM2_StreamReceiver_t *streamReceiver = (ARSTREAM2_StreamReceiver_t*)streamReceiverHandle;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if ((!recordFileName) || (!strlen(recordFileName)))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid record file name");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (streamReceiver->recorder.recorder)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Recorder is already started");
        return ARSTREAM2_ERROR_INVALID_STATE;
    }

    streamReceiver->recorder.fileName = strdup(recordFileName);
    if (!streamReceiver->recorder.fileName)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "String allocation failed");
        ret = ARSTREAM2_ERROR_ALLOC;
    }
    else
    {
        if (streamReceiver->sync)
        {
            streamReceiver->recorder.startPending = 0;
            int recRet;
            recRet = ARSTREAM2_StreamReceiver_StreamRecorderInit(streamReceiver);
            if (recRet != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_StreamReceiver_StreamRecorderInit() failed (%d)", recRet);
            }
        }
        else
        {
            streamReceiver->recorder.startPending = 1;
        }
    }

    return ret;
}


static void ARSTREAM2_StreamReceiver_AutoStartRecorder(ARSTREAM2_StreamReceiver_t *streamReceiver)
{
    char szOutputFileName[500];
    szOutputFileName[0] = '\0';

    if ((streamReceiver->debugPath) && (strlen(streamReceiver->debugPath)))
    {
        snprintf(szOutputFileName, 500, "%s/%s", streamReceiver->debugPath,
                 ARSTREAM2_STREAM_RECEIVER_VIDEO_AUTOREC_OUTPUT_PATH);
        if ((access(szOutputFileName, F_OK) == 0) && (access(szOutputFileName, W_OK) == 0))
        {
            // directory exists and we have write permission
            snprintf(szOutputFileName, 500, "%s/%s/%s_%s.%s", streamReceiver->debugPath,
                     ARSTREAM2_STREAM_RECEIVER_VIDEO_AUTOREC_OUTPUT_PATH,
                     ARSTREAM2_STREAM_RECEIVER_VIDEO_AUTOREC_OUTPUT_FILENAME,
                     streamReceiver->dateAndTime,
                     ARSTREAM2_STREAM_RECEIVER_VIDEO_AUTOREC_OUTPUT_FILEEXT);
        }
        else
        {
            szOutputFileName[0] = '\0';
        }
    }

    if (strlen(szOutputFileName))
    {
        if (streamReceiver->recorder.recorder)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Auto record failed: recorder is already started");
            return;
        }

        streamReceiver->recorder.fileName = strdup(szOutputFileName);
        if (!streamReceiver->recorder.fileName)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Auto record failed: string allocation failed");
            return;
        }
        else
        {
            if (streamReceiver->sync)
            {
                streamReceiver->recorder.startPending = 0;
                int recRet;
                recRet = ARSTREAM2_StreamReceiver_StreamRecorderInit(streamReceiver);
                if (recRet != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Auto record failed: ARSTREAM2_StreamReceiver_StreamRecorderInit() failed (%d)", recRet);
                }
            }
            else
            {
                streamReceiver->recorder.startPending = 1;
            }
            ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_STREAM_RECEIVER_TAG, "Auto record started (file '%s')", streamReceiver->recorder.fileName);
        }
    }
}


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_StopRecorder(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle)
{
    ARSTREAM2_StreamReceiver_t *streamReceiver = (ARSTREAM2_StreamReceiver_t*)streamReceiverHandle;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!streamReceiver->recorder.recorder)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Recorder not started");
        return ARSTREAM2_ERROR_INVALID_STATE;
    }

    int recRet = ARSTREAM2_StreamReceiver_StreamRecorderStop(streamReceiver);
    if (recRet != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_StreamReceiver_StreamRecorderStop() failed (%d)", recRet);
        ret = ARSTREAM2_ERROR_INVALID_STATE;
    }

    recRet = ARSTREAM2_StreamReceiver_StreamRecorderFree(streamReceiver);
    if (recRet != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_StreamReceiver_StreamRecorderFree() failed (%d)", recRet);
        ret = ARSTREAM2_ERROR_INVALID_STATE;
    }

    free(streamReceiver->recorder.fileName);
    streamReceiver->recorder.fileName = NULL;

    return ret;
}
