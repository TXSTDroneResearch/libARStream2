/**
 * @file arstream2_stream_receiver.c
 * @brief Parrot Streaming Library - Stream Receiver
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#include <stdio.h>
#include <stdlib.h>

#include <libARSAL/ARSAL_Print.h>

#include <libARStream2/arstream2_stream_receiver.h>
#include "arstream2_stream_recorder.h"
#include "arstream2_rtp_receiver.h"
#include "arstream2_h264_filter.h"
#include "arstream2_h264.h"


#define ARSTREAM2_STREAM_RECEIVER_TAG "ARSTREAM2_StreamReceiver"

#define ARSTREAM2_STREAM_RECEIVER_TIMEOUT_US (100 * 1000)

#define ARSTREAM2_STREAM_RECEIVER_DEFAULT_NALU_FIFO_SIZE (3000)
#define ARSTREAM2_STREAM_RECEIVER_DEFAULT_AU_FIFO_ITEM_COUNT (200)
#define ARSTREAM2_STREAM_RECEIVER_DEFAULT_AU_FIFO_BUFFER_COUNT (60)

#define ARSTREAM2_STREAM_RECEIVER_AU_BUFFER_SIZE (128 * 1024)
#define ARSTREAM2_STREAM_RECEIVER_AU_METADATA_BUFFER_SIZE (1024)
#define ARSTREAM2_STREAM_RECEIVER_AU_USER_DATA_BUFFER_SIZE (1024)

#define ARSTREAM2_STREAM_RECEIVER_VIDEO_STATS_OUTPUT_PATH "videostats"
#define ARSTREAM2_STREAM_RECEIVER_VIDEO_STATS_OUTPUT_FILENAME "videostats"
#define ARSTREAM2_STREAM_RECEIVER_VIDEO_STATS_OUTPUT_FILEEXT "dat"


typedef struct ARSTREAM2_StreamReceiver_s
{
    ARSTREAM2_H264_AuFifo_t auFifo;
    ARSTREAM2_H264_NaluFifo_t naluFifo;
    ARSAL_Mutex_t fifoMutex;
    ARSTREAM2_H264Filter_Handle filter;
    ARSTREAM2_RtpReceiver_t *receiver;

    int sync;
    uint8_t *pSps;
    int spsSize;
    uint8_t *pPps;
    int ppsSize;

    struct
    {
        ARSTREAM2_H264_AuFifoQueue_t auFifoQueue;
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
        uint8_t *mbStatus;
        int mbWidth;
        int mbHeight;

    } appOutput;

    struct
    {
        ARSTREAM2_H264_AuFifoQueue_t auFifoQueue;
        char *fileName;
        int startPending;
        int running;
        ARSTREAM2_StreamRecorder_Handle recorder;
        ARSAL_Thread_t thread;
        ARSAL_Mutex_t mutex;
        int auCount;

    } recorder;

    /* Debug files */
    char *debugPath;
    char *videoStatsFileName;
    uint64_t videoStatsFileOutputTimestamp;
    FILE *videoStatsFile;

} ARSTREAM2_StreamReceiver_t;


static int ARSTREAM2_StreamReceiver_RtpReceiverAuCallback(ARSTREAM2_H264_AuFifoItem_t *auItem, void *userPtr);
static int ARSTREAM2_StreamReceiver_H264FilterAuCallback(ARSTREAM2_H264_AuFifoItem_t *auItem, void *userPtr);
static int ARSTREAM2_StreamReceiver_H264FilterSpsPpsCallback(uint8_t *spsBuffer, int spsSize, uint8_t *ppsBuffer, int ppsSize, void *userPtr);
static void ARSTREAM2_StreamReceiver_StreamRecorderAuCallback(eARSTREAM2_STREAM_RECORDER_AU_STATUS status, void *auUserPtr, void *userPtr);
static int ARSTREAM2_StreamReceiver_StreamRecorderInit(ARSTREAM2_StreamReceiver_t *streamReceiver);
static int ARSTREAM2_StreamReceiver_StreamRecorderStop(ARSTREAM2_StreamReceiver_t *streamReceiver);
static int ARSTREAM2_StreamReceiver_StreamRecorderFree(ARSTREAM2_StreamReceiver_t *streamReceiver);
static void ARSTREAM2_StreamReceiver_VideoStatsFileOpen(ARSTREAM2_StreamReceiver_t *streamReceiver);
static void ARSTREAM2_StreamReceiver_VideoStatsFileClose(ARSTREAM2_StreamReceiver_t *streamReceiver);
static void ARSTREAM2_StreamReceiver_VideoStatsFileWrite(ARSTREAM2_StreamReceiver_t *streamReceiver, ARSTREAM2_H264Filter_VideoStats_t *videoStats, ARSTREAM2_H264_AccessUnit_t *au);


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_Init(ARSTREAM2_StreamReceiver_Handle *streamReceiverHandle,
                                               const ARSTREAM2_StreamReceiver_Config_t *config,
                                               const ARSTREAM2_StreamReceiver_NetConfig_t *net_config,
                                               const ARSTREAM2_StreamReceiver_MuxConfig_t *mux_config)
{
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;
    ARSTREAM2_StreamReceiver_t *streamReceiver = NULL;
    int auFifoCreated = 0, naluFifoCreated = 0, fifoMutexInit = 0;
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
        ARSTREAM2_StreamReceiver_VideoStatsFileOpen(streamReceiver);
    }

    if (ret == ARSTREAM2_OK)
    {
        int mutexInitRet = ARSAL_Mutex_Init(&(streamReceiver->fifoMutex));
        if (mutexInitRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Mutex creation failed (%d)", mutexInitRet);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            fifoMutexInit = 1;
        }
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
        int auFifoRet = ARSTREAM2_H264_AuFifoInit(&streamReceiver->auFifo, ARSTREAM2_STREAM_RECEIVER_DEFAULT_AU_FIFO_ITEM_COUNT,
                                                  ARSTREAM2_STREAM_RECEIVER_DEFAULT_AU_FIFO_BUFFER_COUNT,
                                                  ARSTREAM2_STREAM_RECEIVER_AU_BUFFER_SIZE, ARSTREAM2_STREAM_RECEIVER_AU_METADATA_BUFFER_SIZE,
                                                  ARSTREAM2_STREAM_RECEIVER_AU_USER_DATA_BUFFER_SIZE, sizeof(ARSTREAM2_H264Filter_VideoStats_t));
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

    /* Setup the NAL unit FIFO */
    if (ret == ARSTREAM2_OK)
    {
        int naluFifoRet = ARSTREAM2_H264_NaluFifoInit(&streamReceiver->naluFifo, ARSTREAM2_STREAM_RECEIVER_DEFAULT_NALU_FIFO_SIZE);
        if (naluFifoRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264_NaluFifoInit() failed (%d)", naluFifoRet);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            naluFifoCreated = 1;
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
        receiverConfig.naluFifo = &(streamReceiver->naluFifo);
        receiverConfig.fifoMutex = &(streamReceiver->fifoMutex);
        receiverConfig.auCallback = ARSTREAM2_StreamReceiver_RtpReceiverAuCallback;
        receiverConfig.auCallbackUserPtr = streamReceiver;
        receiverConfig.maxPacketSize = config->maxPacketSize;
        receiverConfig.insertStartCodes = 1;
        receiverConfig.generateReceiverReports = config->generateReceiverReports;

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
        filterConfig.auFifo = &(streamReceiver->auFifo);
        filterConfig.naluFifo = &(streamReceiver->naluFifo);
        filterConfig.fifoMutex = &(streamReceiver->fifoMutex);
        filterConfig.auCallback = ARSTREAM2_StreamReceiver_H264FilterAuCallback;
        filterConfig.auCallbackUserPtr = streamReceiver;
        filterConfig.spsPpsCallback = ARSTREAM2_StreamReceiver_H264FilterSpsPpsCallback;
        filterConfig.spsPpsCallbackUserPtr = streamReceiver;
        filterConfig.outputIncompleteAu = config->outputIncompleteAu;
        filterConfig.filterOutSpsPps = config->filterOutSpsPps;
        filterConfig.filterOutSei = config->filterOutSei;
        filterConfig.replaceStartCodesWithNaluSize = config->replaceStartCodesWithNaluSize;
        filterConfig.generateSkippedPSlices = config->generateSkippedPSlices;
        filterConfig.generateFirstGrayIFrame = config->generateFirstGrayIFrame;

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
            if (naluFifoCreated) ARSTREAM2_H264_NaluFifoFree(&(streamReceiver->naluFifo));
            if (appOutputThreadMutexInit) ARSAL_Mutex_Destroy(&(streamReceiver->appOutput.threadMutex));
            if (appOutputThreadCondInit) ARSAL_Cond_Destroy(&(streamReceiver->appOutput.threadCond));
            if (appOutputCallbackMutexInit) ARSAL_Mutex_Destroy(&(streamReceiver->appOutput.callbackMutex));
            if (appOutputCallbackCondInit) ARSAL_Cond_Destroy(&(streamReceiver->appOutput.callbackCond));
            if (recorderMutexInit) ARSAL_Mutex_Destroy(&(streamReceiver->recorder.mutex));
            if (fifoMutexInit) ARSAL_Mutex_Destroy(&(streamReceiver->fifoMutex));
            ARSTREAM2_StreamReceiver_VideoStatsFileClose(streamReceiver);
            free(streamReceiver->debugPath);
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
    ARSTREAM2_H264_NaluFifoFree(&(streamReceiver->naluFifo));
    ARSAL_Mutex_Destroy(&(streamReceiver->appOutput.threadMutex));
    ARSAL_Cond_Destroy(&(streamReceiver->appOutput.threadCond));
    ARSAL_Mutex_Destroy(&(streamReceiver->appOutput.callbackMutex));
    ARSAL_Cond_Destroy(&(streamReceiver->appOutput.callbackCond));
    ARSAL_Mutex_Destroy(&(streamReceiver->recorder.mutex));
    ARSAL_Mutex_Destroy(&(streamReceiver->fifoMutex));
    free(streamReceiver->recorder.fileName);
    free(streamReceiver->pSps);
    free(streamReceiver->pPps);
    ARSTREAM2_StreamReceiver_VideoStatsFileClose(streamReceiver);
    free(streamReceiver->debugPath);

    free(streamReceiver);
    *streamReceiverHandle = NULL;

    return ret;
}


static void ARSTREAM2_StreamReceiver_VideoStatsFileOpen(ARSTREAM2_StreamReceiver_t *streamReceiver)
{
    int i;
    char szOutputFileName[500];
    szOutputFileName[0] = '\0';

    if ((streamReceiver->debugPath) && (strlen(streamReceiver->debugPath)))
    {
        for (i = 0; i < 1000; i++)
        {
            snprintf(szOutputFileName, 128, "%s/%s/%s_%03d.%s", streamReceiver->debugPath,
                     ARSTREAM2_STREAM_RECEIVER_VIDEO_STATS_OUTPUT_PATH,
                     ARSTREAM2_STREAM_RECEIVER_VIDEO_STATS_OUTPUT_FILENAME, i,
                     ARSTREAM2_STREAM_RECEIVER_VIDEO_STATS_OUTPUT_FILEEXT);
            if (access(szOutputFileName, F_OK) == -1)
            {
                // file does not exist
                break;
            }
            szOutputFileName[0] = '\0';
        }
    }

    if (strlen(szOutputFileName))
    {
        streamReceiver->videoStatsFileName = strdup(szOutputFileName);
        if (!streamReceiver->videoStatsFileName)
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_STREAM_RECEIVER_TAG, "Allocation failed for video stats output file '%s'", szOutputFileName);
            return;
        }
        streamReceiver->videoStatsFile = fopen(szOutputFileName, "w");
        if (!streamReceiver->videoStatsFile)
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_STREAM_RECEIVER_TAG, "Unable to open video stats output file '%s'", szOutputFileName);
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_STREAM_RECEIVER_TAG, "Opened video stats output file '%s'", szOutputFileName);
        }
    }

    if (streamReceiver->videoStatsFile)
    {
        fprintf(streamReceiver->videoStatsFile, "timestamp rssi totalFrameCount outputFrameCount discardedFrameCount missedFrameCount errorSecondCount");
        int i, j;
        for (i = 0; i < ARSTREAM2_H264_FILTER_MB_STATUS_ZONE_COUNT; i++)
        {
            fprintf(streamReceiver->videoStatsFile, " errorSecondCountByZone[%d]", i);
        }
        for (j = 0; j < ARSTREAM2_H264_FILTER_MB_STATUS_CLASS_COUNT; j++)
        {
            for (i = 0; i < ARSTREAM2_H264_FILTER_MB_STATUS_ZONE_COUNT; i++)
            {
                fprintf(streamReceiver->videoStatsFile, " macroblockStatus[%d][%d]", j, i);
            }
        }
        fprintf(streamReceiver->videoStatsFile, "\n");
    }
}


static void ARSTREAM2_StreamReceiver_VideoStatsFileClose(ARSTREAM2_StreamReceiver_t *streamReceiver)
{
    if (streamReceiver->videoStatsFile)
    {
        fclose(streamReceiver->videoStatsFile);
        streamReceiver->videoStatsFile = NULL;
    }
    free(streamReceiver->videoStatsFileName);
    streamReceiver->videoStatsFileName = NULL;
}


static void ARSTREAM2_StreamReceiver_VideoStatsFileWrite(ARSTREAM2_StreamReceiver_t *streamReceiver, ARSTREAM2_H264Filter_VideoStats_t *videoStats, ARSTREAM2_H264_AccessUnit_t *au)
{
    if ((!videoStats) || (!au))
    {
        return;
    }

    if (!streamReceiver->videoStatsFile)
    {
        return;
    }

    struct timespec t1;
    ARSAL_Time_GetTime(&t1);
    uint64_t curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

    if (streamReceiver->videoStatsFileOutputTimestamp == 0)
    {
        /* init */
        streamReceiver->videoStatsFileOutputTimestamp = curTime;
    }
    if (curTime >= streamReceiver->videoStatsFileOutputTimestamp + ARSTREAM2_H264_FILTER_STATS_OUTPUT_INTERVAL)
    {
        if (streamReceiver->videoStatsFile)
        {
            /* get the RSSI from the streaming metadata */
            //TODO: remove this hack once we have a better way of getting the RSSI
            int rssi = 0;
            if ((au->metadataSize >= 27) && (ntohs(*((uint16_t*)au->buffer->metadataBuffer)) == 0x5031))
            {
                rssi = (int8_t)au->buffer->metadataBuffer[26];
            }
            if ((au->metadataSize >= 55) && (ntohs(*((uint16_t*)au->buffer->metadataBuffer)) == 0x5032))
            {
                rssi = (int8_t)au->buffer->metadataBuffer[54];
            }

            fprintf(streamReceiver->videoStatsFile, "%llu %i %lu %lu %lu %lu %lu", (long long unsigned int)curTime, rssi,
                    (long unsigned int)videoStats->totalFrameCount, (long unsigned int)videoStats->outputFrameCount,
                    (long unsigned int)videoStats->discardedFrameCount, (long unsigned int)videoStats->missedFrameCount,
                    (long unsigned int)videoStats->errorSecondCount);
            int i, j;
            for (i = 0; i < ARSTREAM2_H264_FILTER_MB_STATUS_ZONE_COUNT; i++)
            {
                fprintf(streamReceiver->videoStatsFile, " %lu", (long unsigned int)videoStats->errorSecondCountByZone[i]);
            }
            for (j = 0; j < ARSTREAM2_H264_FILTER_MB_STATUS_CLASS_COUNT; j++)
            {
                for (i = 0; i < ARSTREAM2_H264_FILTER_MB_STATUS_ZONE_COUNT; i++)
                {
                    fprintf(streamReceiver->videoStatsFile, " %lu", (long unsigned int)videoStats->macroblockStatus[j][i]);
                }
            }
            fprintf(streamReceiver->videoStatsFile, "\n");
        }
        streamReceiver->videoStatsFileOutputTimestamp = curTime;
    }
}


static int ARSTREAM2_StreamReceiver_AppOutputAuEnqueue(ARSTREAM2_StreamReceiver_t *streamReceiver, ARSTREAM2_H264_AuFifoItem_t *auItem)
{
    int err = 0, ret = 0, needUnref = 0, needFree = 0;
    ARSTREAM2_H264_AuFifoItem_t *appOutputAuItem = NULL;

    /* add ref to AU buffer */
    ARSAL_Mutex_Lock(&(streamReceiver->fifoMutex));
    ret = ARSTREAM2_H264_AuFifoBufferAddRef(auItem->au.buffer);
    if (ret < 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264_AuFifoBufferAddRef() failed (%d)", ret);
    }
    if (ret == 0)
    {
        /* duplicate the AU and associated NALUs */
        appOutputAuItem = ARSTREAM2_H264_AuFifoDuplicateItem(&streamReceiver->auFifo, &streamReceiver->naluFifo, auItem);
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
    ARSAL_Mutex_Unlock(&(streamReceiver->fifoMutex));

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
        ARSTREAM2_H264_NaluFifoItem_t *naluItem;
        ARSAL_Mutex_Lock(&(streamReceiver->fifoMutex));
        while ((naluItem = ARSTREAM2_H264_AuDequeueNalu(&appOutputAuItem->au)) != NULL)
        {
            ret = ARSTREAM2_H264_NaluFifoPushFreeItem(&streamReceiver->naluFifo, naluItem);
            if (ret != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the NALU FIFO (%d)", ret);
            }
        }
        ret = ARSTREAM2_H264_AuFifoPushFreeItem(&streamReceiver->auFifo, appOutputAuItem);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the AU FIFO (%d)", ret);
        }
        ARSAL_Mutex_Unlock(&(streamReceiver->fifoMutex));
        needFree = 0;
    }
    if (needUnref)
    {
        ARSAL_Mutex_Lock(&(streamReceiver->fifoMutex));
        ret = ARSTREAM2_H264_AuFifoUnrefBuffer(&streamReceiver->auFifo, auItem->au.buffer);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to unref buffer (%d)", ret);
        }
        ARSAL_Mutex_Unlock(&(streamReceiver->fifoMutex));
        needUnref = 0;
    }

    return err;
}


static int ARSTREAM2_StreamReceiver_RecorderAuEnqueue(ARSTREAM2_StreamReceiver_t *streamReceiver, ARSTREAM2_H264_AuFifoItem_t *auItem)
{
    int err = 0, ret = 0, needUnref = 0, needFree = 0;
    ARSTREAM2_H264_AuFifoItem_t *recordtAuItem = NULL;

    ARSAL_Mutex_Lock(&(streamReceiver->fifoMutex));
    ret = ARSTREAM2_H264_AuFifoBufferAddRef(auItem->au.buffer);
    if (ret < 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264_AuFifoBufferAddRef() failed (%d)", ret);
    }
    if (ret == 0)
    {
        /* duplicate the AU and associated NALUs */
        recordtAuItem = ARSTREAM2_H264_AuFifoDuplicateItem(&streamReceiver->auFifo, &streamReceiver->naluFifo, auItem);
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
    ARSAL_Mutex_Unlock(&(streamReceiver->fifoMutex));

    if ((ret == 0) && (recordtAuItem))
    {
        ARSTREAM2_H264_AccessUnit_t *au = &recordtAuItem->au;
        ARSTREAM2_H264_NaluFifoItem_t *naluItem;
        ARSTREAM2_StreamRecorder_AccessUnit_t accessUnit;
        memset(&accessUnit, 0, sizeof(ARSTREAM2_StreamRecorder_AccessUnit_t));
        accessUnit.timestamp = au->ntpTimestamp;
        accessUnit.index = streamReceiver->recorder.auCount++;
        for (naluItem = au->naluHead, accessUnit.naluCount = 0; naluItem; naluItem = naluItem->next, accessUnit.naluCount++)
        {
            accessUnit.naluData[accessUnit.naluCount] = naluItem->nalu.nalu;
            accessUnit.naluSize[accessUnit.naluCount] = naluItem->nalu.naluSize;
        }
        /* map the access unit sync type */
        switch (au->syncType)
        {
            default:
            case ARSTREAM2_H264_AU_SYNC_TYPE_NONE:
                accessUnit.auSyncType = ARSTREAM2_STREAM_RECEIVER_AU_SYNC_TYPE_NONE;
                break;
            case ARSTREAM2_H264_AU_SYNC_TYPE_IDR:
                accessUnit.auSyncType = ARSTREAM2_STREAM_RECEIVER_AU_SYNC_TYPE_IDR;
                break;
            case ARSTREAM2_H264_AU_SYNC_TYPE_IFRAME:
                accessUnit.auSyncType = ARSTREAM2_STREAM_RECEIVER_AU_SYNC_TYPE_IFRAME;
                break;
            case ARSTREAM2_H264_AU_SYNC_TYPE_PIR_START:
                accessUnit.auSyncType = ARSTREAM2_STREAM_RECEIVER_AU_SYNC_TYPE_PIR_START;
                break;
        }
        accessUnit.auMetadata = au->buffer->metadataBuffer;
        accessUnit.auMetadataSize = au->metadataSize;
        accessUnit.auUserPtr = recordtAuItem;
        eARSTREAM2_ERROR recErr = ARSTREAM2_StreamRecorder_PushAccessUnit(streamReceiver->recorder.recorder, &accessUnit);
        if (recErr != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_StreamRecorder_PushAccessUnit() failed: %d (%s)",
                        recErr, ARSTREAM2_Error_ToString(recErr));
        }
    }
    else
    {
        err = -1;
    }

    /* error handling */
    if (needFree)
    {
        ARSTREAM2_H264_NaluFifoItem_t *naluItem;
        ARSAL_Mutex_Lock(&(streamReceiver->fifoMutex));
        while ((naluItem = ARSTREAM2_H264_AuDequeueNalu(&recordtAuItem->au)) != NULL)
        {
            ret = ARSTREAM2_H264_NaluFifoPushFreeItem(&streamReceiver->naluFifo, naluItem);
            if (ret != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the NALU FIFO (%d)", ret);
            }
        }
        ret = ARSTREAM2_H264_AuFifoPushFreeItem(&streamReceiver->auFifo, recordtAuItem);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the AU FIFO (%d)", ret);
        }
        ARSAL_Mutex_Unlock(&(streamReceiver->fifoMutex));
        needFree = 0;
    }
    if (needUnref)
    {
        ARSAL_Mutex_Lock(&(streamReceiver->fifoMutex));
        ret = ARSTREAM2_H264_AuFifoUnrefBuffer(&streamReceiver->auFifo, auItem->au.buffer);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to unref buffer (%d)", ret);
        }
        ARSAL_Mutex_Unlock(&(streamReceiver->fifoMutex));
        needUnref = 0;
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

        if (auItem->au.videoStatsAvailable)
        {
            ARSTREAM2_StreamReceiver_VideoStatsFileWrite(streamReceiver, (ARSTREAM2_H264Filter_VideoStats_t*)auItem->au.buffer->videoStatsBuffer, &auItem->au);
        }
    }
    else
    {
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264Filter_ProcessAu() failed (%d)", ret);
        }
    }

    /* free the access unit and associated NAL units */
    ARSTREAM2_H264_NaluFifoItem_t *naluItem;
    ARSAL_Mutex_Lock(&(streamReceiver->fifoMutex));
    while ((naluItem = ARSTREAM2_H264_AuDequeueNalu(&auItem->au)) != NULL)
    {
        ret = ARSTREAM2_H264_NaluFifoPushFreeItem(&streamReceiver->naluFifo, naluItem);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the NALU FIFO (%d)", ret);
        }
    }
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
    ARSAL_Mutex_Unlock(&(streamReceiver->fifoMutex));

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

    /* free the access unit and associated NAL units */
    ARSTREAM2_H264_NaluFifoItem_t *naluItem;
    ARSAL_Mutex_Lock(&(streamReceiver->fifoMutex));
    while ((naluItem = ARSTREAM2_H264_AuDequeueNalu(&auItem->au)) != NULL)
    {
        ret = ARSTREAM2_H264_NaluFifoPushFreeItem(&streamReceiver->naluFifo, naluItem);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the NALU FIFO (%d)", ret);
        }
    }
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
    ARSAL_Mutex_Unlock(&(streamReceiver->fifoMutex));

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
    ARSTREAM2_H264_NaluFifoItem_t *naluItem;
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

    /* free the access unit and associated NAL units */
    ARSAL_Mutex_Lock(&(streamReceiver->fifoMutex));
    while ((naluItem = ARSTREAM2_H264_AuDequeueNalu(&auItem->au)) != NULL)
    {
        ret = ARSTREAM2_H264_NaluFifoPushFreeItem(&streamReceiver->naluFifo, naluItem);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the NALU FIFO (%d)", ret);
        }
    }
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
    ARSAL_Mutex_Unlock(&(streamReceiver->fifoMutex));
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

                    streamReceiver->appOutput.mbStatus = au->buffer->mbStatusBuffer;

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

                    /* timestamps and metadata */
                    memset(&auTimestamps, 0, sizeof(auTimestamps));
                    memset(&auMetadata, 0, sizeof(auMetadata));
                    auTimestamps.auNtpTimestamp = au->ntpTimestamp;
                    auTimestamps.auNtpTimestampRaw = au->ntpTimestampRaw;
                    auTimestamps.auNtpTimestampLocal = au->ntpTimestampLocal;
                    auMetadata.auMetadata = (au->metadataSize > 0) ? au->buffer->metadataBuffer : NULL;
                    auMetadata.auMetadataSize = au->metadataSize;
                    auMetadata.auUserData = (au->userDataSize > 0) ? au->buffer->userDataBuffer : NULL;
                    auMetadata.auUserDataSize = au->userDataSize;
                    auMetadata.debugString = NULL; //TODO

                    ARSAL_Time_GetTime(&t1);
                    curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

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
                            ret = ARSTREAM2_H264Filter_ForceIdr(streamReceiver->filter);
                            if (ret != 0)
                            {
                                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264Filter_ForceIdr() failed (%d)", ret);
                            }
                        }
                    }
                }
            }

            /* free the access unit and associated NAL units */
            ARSAL_Mutex_Lock(&(streamReceiver->fifoMutex));
            while ((naluItem = ARSTREAM2_H264_AuDequeueNalu(&auItem->au)) != NULL)
            {
                ret = ARSTREAM2_H264_NaluFifoPushFreeItem(&streamReceiver->naluFifo, naluItem);
                if (ret != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the NALU FIFO (%d)", ret);
                }
            }
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
            ARSAL_Mutex_Unlock(&(streamReceiver->fifoMutex));

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


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_GetFrameMacroblockStatus(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle, uint8_t **macroblocks, int *mbWidth, int *mbHeight)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)streamReceiverHandle;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    ARSAL_Mutex_Lock(&(streamReceiver->appOutput.threadMutex));
    int appOutputRunning = streamReceiver->appOutput.running;
    int callbackInProgress = streamReceiver->appOutput.callbackInProgress;
    ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.threadMutex));
    if ((!appOutputRunning) || (!callbackInProgress))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid state");
        return ARSTREAM2_ERROR_INVALID_STATE;
    }

    if (macroblocks) *macroblocks = streamReceiver->appOutput.mbStatus;
    if (mbWidth) *mbWidth = streamReceiver->appOutput.mbWidth;
    if (mbHeight) *mbHeight = streamReceiver->appOutput.mbHeight;

    return ARSTREAM2_OK;
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
