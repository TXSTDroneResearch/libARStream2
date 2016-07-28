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
#include "arstream2_rtp_receiver.h"
#include "arstream2_h264_filter.h"
#include "arstream2_h264.h"


#define ARSTREAM2_STREAM_RECEIVER_TAG "ARSTREAM2_StreamReceiver"

#define ARSTREAM2_STREAM_RECEIVER_TIMEOUT_US (100 * 1000)


typedef struct ARSTREAM2_StreamReceiver_s
{
    ARSTREAM2_H264_AuFifo_t auFifo;
    ARSTREAM2_H264_NaluFifo_t naluFifo;
    ARSAL_Mutex_t fifoMutex;
    ARSTREAM2_H264Filter_Handle filter;
    ARSTREAM2_RtpReceiver_t *receiver;

    struct
    {
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
        ARSTREAM2_H264Filter_SpsPpsCallback_t spsPpsCallback;
        void *spsPpsCallbackUserPtr;
        ARSTREAM2_H264Filter_GetAuBufferCallback_t getAuBufferCallback;
        void *getAuBufferCallbackUserPtr;
        ARSTREAM2_H264Filter_AuReadyCallback_t auReadyCallback;
        void *auReadyCallbackUserPtr;
    } appOutput;

} ARSTREAM2_StreamReceiver_t;


static int ARSTREAM2_StreamReceiver_RtpReceiverAuCallback(ARSTREAM2_H264_AuFifoItem_t *auItem, void *userPtr);
static int ARSTREAM2_StreamReceiver_H264FilterAuCallback(ARSTREAM2_H264_AuFifoItem_t *auItem, void *userPtr);
static int ARSTREAM2_StreamReceiver_H264FilterSpsPpsCallback(uint8_t *spsBuffer, int spsSize, uint8_t *ppsBuffer, int ppsSize, void *userPtr);


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_Init(ARSTREAM2_StreamReceiver_Handle *streamReceiverHandle,
                                               ARSTREAM2_StreamReceiver_Config_t *config,
                                               ARSTREAM2_StreamReceiver_NetConfig_t *net_config,
                                               ARSTREAM2_StreamReceiver_MuxConfig_t *mux_config)
{
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;
    ARSTREAM2_StreamReceiver_t *streamReceiver = NULL;
    int auFifoCreated = 0, naluFifoCreated = 0, fifoMutexInit = 0;
    int appOutputThreadMutexInit = 0, appOutputThreadCondInit = 0;
    int appOutputCallbackMutexInit = 0, appOutputCallbackCondInit = 0;

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

    /* Setup the access unit FIFO */
    if (ret == ARSTREAM2_OK)
    {
        int auFifoRet = ARSTREAM2_H264_AuFifoInit(&streamReceiver->auFifo, ARSTREAM2_RTP_RECEIVER_DEFAULT_AU_FIFO_SIZE,
                                                  ARSTREAM2_RTP_RECEIVER_AU_BUFFER_SIZE, ARSTREAM2_RTP_RECEIVER_AU_METADATA_BUFFER_SIZE,
                                                  ARSTREAM2_RTP_RECEIVER_AU_USER_DATA_BUFFER_SIZE);
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
        int naluFifoRet = ARSTREAM2_H264_NaluFifoInit(&streamReceiver->naluFifo, ARSTREAM2_RTP_RECEIVER_DEFAULT_NALU_FIFO_SIZE);
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


        receiverConfig.auFifo = &(streamReceiver->auFifo);
        receiverConfig.naluFifo = &(streamReceiver->naluFifo);
        receiverConfig.fifoMutex = &(streamReceiver->fifoMutex);
        receiverConfig.auCallback = ARSTREAM2_StreamReceiver_RtpReceiverAuCallback;
        receiverConfig.auCallbackUserPtr = streamReceiver;
        receiverConfig.maxPacketSize = config->maxPacketSize;
        receiverConfig.maxBitrate = config->maxBitrate;
        receiverConfig.maxLatencyMs = config->maxLatencyMs;
        receiverConfig.maxNetworkLatencyMs = config->maxNetworkLatencyMs;
        receiverConfig.insertStartCodes = 1;
        receiverConfig.generateReceiverReports = 0; //TODO: config->generateReceiverReports;

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
            if (fifoMutexInit) ARSAL_Mutex_Destroy(&(streamReceiver->fifoMutex));
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
    ARSAL_Mutex_Destroy(&(streamReceiver->fifoMutex));

    free(streamReceiver);
    *streamReceiverHandle = NULL;

    return ret;
}


static int ARSTREAM2_StreamReceiver_RtpReceiverAuCallback(ARSTREAM2_H264_AuFifoItem_t *auItem, void *userPtr)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)userPtr;
    int err = 0, ret, needFree = 0;

    if ((!auItem) || (!userPtr))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid pointer");
        return -1;
    }

    ret = ARSTREAM2_H264Filter_ProcessAu(streamReceiver->filter, &auItem->au);
    if (ret == 1)
    {
        ret = ARSTREAM2_H264_AuFifoEnqueueItem(&streamReceiver->auFifo, auItem);
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264_AuFifoEnqueueItem() failed (%d)", ret);
            needFree = 1;
        }
        else
        {
            ARSAL_Cond_Signal(&(streamReceiver->appOutput.threadCond));
        }
    }
    else
    {
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264Filter_ProcessAu() failed (%d)", ret);
        }
        needFree = 1;
    }

    if (needFree)
    {
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
        ret = ARSTREAM2_H264_AuFifoPushFreeItem(&streamReceiver->auFifo, auItem);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the AU FIFO (%d)", ret);
        }
        ARSAL_Mutex_Unlock(&(streamReceiver->fifoMutex));
    }

    return err;
}


static int ARSTREAM2_StreamReceiver_H264FilterAuCallback(ARSTREAM2_H264_AuFifoItem_t *auItem, void *userPtr)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)userPtr;
    ARSTREAM2_H264_NaluFifoItem_t *naluItem;
    int err = 0, ret, needFree = 0;

    if ((!auItem) || (!userPtr))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid pointer");
        return -1;
    }

    ret = ARSTREAM2_H264_AuFifoEnqueueItem(&streamReceiver->auFifo, auItem);
    if (ret < 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264_AuFifoEnqueueItem() failed (%d)", ret);
        needFree = 1;
    }
    else
    {
        ARSAL_Cond_Signal(&(streamReceiver->appOutput.threadCond));
    }


    if (needFree)
    {
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
        ret = ARSTREAM2_H264_AuFifoPushFreeItem(&streamReceiver->auFifo, auItem);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the AU FIFO (%d)", ret);
        }
        ARSAL_Mutex_Unlock(&(streamReceiver->fifoMutex));
    }

    return err;
}


static int ARSTREAM2_StreamReceiver_H264FilterSpsPpsCallback(uint8_t *spsBuffer, int spsSize, uint8_t *ppsBuffer, int ppsSize, void *userPtr)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)userPtr;
    int err = 0;
    eARSTREAM2_ERROR cbRet;

    if (!userPtr)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid pointer");
        return -1;
    }

    ARSAL_Mutex_Lock(&(streamReceiver->appOutput.callbackMutex));
    streamReceiver->appOutput.callbackInProgress = 1;
    if (streamReceiver->appOutput.getAuBufferCallback)
    {
        /* call the getAuBufferCallback */
        ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.callbackMutex));

        cbRet = streamReceiver->appOutput.spsPpsCallback(spsBuffer, spsSize, ppsBuffer, ppsSize, streamReceiver->appOutput.spsPpsCallbackUserPtr);
        if (cbRet != ARSTREAM2_OK)
        {
            err = -1;
        }

        ARSAL_Mutex_Lock(&(streamReceiver->appOutput.callbackMutex));
    }
    streamReceiver->appOutput.callbackInProgress = 0;
    ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.callbackMutex));
    ARSAL_Cond_Signal(&(streamReceiver->appOutput.callbackCond));

    return err;
}


void* ARSTREAM2_StreamReceiver_RunFilterThread(void *streamReceiverHandle)
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
        ARSAL_Mutex_Lock(&(streamReceiver->fifoMutex));
        auItem = ARSTREAM2_H264_AuFifoDequeueItem(&streamReceiver->auFifo);
        ARSAL_Mutex_Unlock(&(streamReceiver->fifoMutex));

        while (auItem != NULL)
        {
            ARSTREAM2_H264_NaluFifoItem_t *naluItem;

            if (running)
            {
                eARSTREAM2_ERROR cbRet = ARSTREAM2_OK;
                uint8_t *auBuffer = NULL;
                int auBufferSize = 0;
                void *auBufferUserPtr = NULL;

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
                    ARSTREAM2_H264_AccessUnit_t *au = &auItem->au;
                    ARSTREAM2_H264_NaluFifoItem_t *naluItem;
                    unsigned int auSize = 0;

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
                    }

                    /* map the access unit sync type */
                    eARSTREAM2_H264_FILTER_AU_SYNC_TYPE auSyncType;
                    switch (au->syncType)
                    {
                        default:
                        case ARSTREAM2_H264_AU_SYNC_TYPE_NONE:
                            auSyncType = ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_NONE;
                            break;
                        case ARSTREAM2_H264_AU_SYNC_TYPE_IDR:
                            auSyncType = ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_IDR;
                            break;
                        case ARSTREAM2_H264_AU_SYNC_TYPE_IFRAME:
                            auSyncType = ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_IFRAME;
                            break;
                        case ARSTREAM2_H264_AU_SYNC_TYPE_PIR_START:
                            auSyncType = ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_PIR_START;
                            break;
                    }

                    ARSAL_Time_GetTime(&t1);
                    curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

                    if (streamReceiver->appOutput.auReadyCallback)
                    {
                        /* call the auReadyCallback */
                        ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.callbackMutex));

                        cbRet = streamReceiver->appOutput.auReadyCallback(auBuffer, auSize, /*TODO au->rtpTimestamp,*/ au->ntpTimestamp,
                                                        au->ntpTimestampLocal, auSyncType,
                                                        (au->metadataSize > 0) ? au->metadataBuffer : NULL, au->metadataSize,
                                                        (au->userDataSize > 0) ? au->userDataBuffer : NULL, au->userDataSize,
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

            ARSAL_Mutex_Lock(&(streamReceiver->fifoMutex));

            /* free the access unit and associated NAL units */
            while ((naluItem = ARSTREAM2_H264_AuDequeueNalu(&auItem->au)) != NULL)
            {
                ret = ARSTREAM2_H264_NaluFifoPushFreeItem(&streamReceiver->naluFifo, naluItem);
                if (ret != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the NALU FIFO (%d)", ret);
                }
            }
            ret = ARSTREAM2_H264_AuFifoPushFreeItem(&streamReceiver->auFifo, auItem);
            if (ret != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Failed to push free item in the AU FIFO (%d)", ret);
            }

            /* dequeue the next access unit */
            auItem = ARSTREAM2_H264_AuFifoDequeueItem(&streamReceiver->auFifo);

            ARSAL_Mutex_Unlock(&(streamReceiver->fifoMutex));
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


void* ARSTREAM2_StreamReceiver_RunStreamThread(void *streamReceiverHandle)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)streamReceiverHandle;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid handle");
        return NULL;
    }

    return ARSTREAM2_RtpReceiver_RunThread((void*)streamReceiver->receiver);
}


void* ARSTREAM2_StreamReceiver_RunControlThread(void *streamReceiverHandle)
{
    return (void*)0;
}


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_StartFilter(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle, ARSTREAM2_H264Filter_SpsPpsCallback_t spsPpsCallback, void* spsPpsCallbackUserPtr,
                                                      ARSTREAM2_H264Filter_GetAuBufferCallback_t getAuBufferCallback, void* getAuBufferCallbackUserPtr,
                                                      ARSTREAM2_H264Filter_AuReadyCallback_t auReadyCallback, void* auReadyCallbackUserPtr)
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
    streamReceiver->appOutput.running = 1;
    ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.callbackMutex));

    ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_STREAM_RECEIVER_TAG, "App output is running");

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_PauseFilter(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)streamReceiverHandle;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

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
    streamReceiver->appOutput.running = 0;
    int err = ARSTREAM2_H264Filter_ForceResync(streamReceiver->filter);
    if (err != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "ARSTREAM2_H264Filter_ForceResync() failed (%d)", err);
    }
    ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.callbackMutex));

    ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_STREAM_RECEIVER_TAG, "App output is paused");

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

    ARSTREAM2_RtpReceiver_Stop(streamReceiver->receiver);

    ARSAL_Mutex_Lock(&(streamReceiver->appOutput.threadMutex));
    streamReceiver->appOutput.threadShouldStop = 1;
    ARSAL_Mutex_Unlock(&(streamReceiver->appOutput.threadMutex));
    /* signal the thread to avoid a deadlock */
    ARSAL_Cond_Signal(&(streamReceiver->appOutput.threadCond));

    //TODO
    ret = ARSTREAM2_H264Filter_Stop(streamReceiver->filter);
    if (ret != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Unable to stop H264Filter: %s", ARSTREAM2_Error_ToString(ret));
        return ret;
    }

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_GetSpsPps(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle, uint8_t *spsBuffer, int *spsSize, uint8_t *ppsBuffer, int *ppsSize)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)streamReceiverHandle;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    //TODO

    return ARSTREAM2_H264Filter_GetSpsPps(streamReceiver->filter, spsBuffer, spsSize, ppsBuffer, ppsSize);
}


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_GetFrameMacroblockStatus(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle, uint8_t **macroblocks, int *mbWidth, int *mbHeight)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)streamReceiverHandle;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    //TODO

    return ARSTREAM2_H264Filter_GetFrameMacroblockStatus(streamReceiver->filter, macroblocks, mbWidth, mbHeight);
}


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_InitResender(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle, ARSTREAM2_StreamReceiver_ResenderHandle *resenderHandle, ARSTREAM2_StreamReceiver_ResenderConfig_t *config)
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

    resenderConfig.clientAddr = config->clientAddr;
    resenderConfig.mcastAddr = config->mcastAddr;
    resenderConfig.mcastIfaceAddr = config->mcastIfaceAddr;
    resenderConfig.serverStreamPort = config->serverStreamPort;
    resenderConfig.serverControlPort = config->serverControlPort;
    resenderConfig.clientStreamPort = config->clientStreamPort;
    resenderConfig.clientControlPort = config->clientControlPort;
    resenderConfig.maxPacketSize = config->maxPacketSize;
    resenderConfig.targetPacketSize = config->targetPacketSize;
    resenderConfig.streamSocketBufferSize = config->streamSocketBufferSize;
    resenderConfig.maxLatencyMs = config->maxLatencyMs;
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


void* ARSTREAM2_StreamReceiver_RunResenderStreamThread(void *resenderHandle)
{
    return ARSTREAM2_RtpReceiverResender_RunThread(resenderHandle);
}


void* ARSTREAM2_StreamReceiver_RunResenderControlThread(void *resenderHandle)
{
    return (void*)0;
}


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_StopResender(ARSTREAM2_StreamReceiver_ResenderHandle resenderHandle)
{
    ARSTREAM2_RtpReceiverResender_Stop((ARSTREAM2_RtpReceiver_RtpResender_t*)resenderHandle);

    return ARSTREAM2_OK;
}


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_StartRecorder(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle, const char *recordFileName)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)streamReceiverHandle;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    return ARSTREAM2_H264Filter_StartRecorder(streamReceiver->filter, recordFileName);
}


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_StopRecorder(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)streamReceiverHandle;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    return ARSTREAM2_H264Filter_StopRecorder(streamReceiver->filter);
}
