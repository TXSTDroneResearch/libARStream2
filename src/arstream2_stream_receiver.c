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


#define ARSTREAM2_STREAM_RECEIVER_TAG "ARSTREAM2_StreamReceiver"


typedef struct ARSTREAM2_StreamReceiver_s
{
    ARSTREAM2_H264Filter_Handle filter;
    ARSTREAM2_RtpReceiver_t *receiver;

} ARSTREAM2_StreamReceiver_t;



eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_Init(ARSTREAM2_StreamReceiver_Handle *streamReceiverHandle, ARSTREAM2_StreamReceiver_Config_t *config)
{
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;
    ARSTREAM2_StreamReceiver_t *streamReceiver = NULL;

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

    streamReceiver = (ARSTREAM2_StreamReceiver_t*)malloc(sizeof(*streamReceiver));
    if (!streamReceiver)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Allocation failed (size %ld)", sizeof(*streamReceiver));
        ret = ARSTREAM2_ERROR_ALLOC;
    }

    if (ret == ARSTREAM2_OK)
    {
        memset(streamReceiver, 0, sizeof(*streamReceiver));

        ARSTREAM2_H264Filter_Config_t filterConfig;
        memset(&filterConfig, 0, sizeof(filterConfig));
        filterConfig.waitForSync = config->waitForSync;
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
        ARSTREAM2_RtpReceiver_Config_t receiverConfig;
        memset(&receiverConfig, 0, sizeof(receiverConfig));

        receiverConfig.serverAddr = config->serverAddr;
        receiverConfig.mcastAddr = config->mcastAddr;
        receiverConfig.mcastIfaceAddr = config->mcastIfaceAddr;
        receiverConfig.serverStreamPort = config->serverStreamPort;
        receiverConfig.serverControlPort = config->serverControlPort;
        receiverConfig.clientStreamPort = config->clientStreamPort;
        receiverConfig.clientControlPort = config->clientControlPort;
        receiverConfig.naluCallback = ARSTREAM2_H264Filter_RtpReceiverNaluCallback;
        receiverConfig.naluCallbackUserPtr = (void*)streamReceiver->filter;
        receiverConfig.maxPacketSize = config->maxPacketSize;
        receiverConfig.maxBitrate = config->maxBitrate;
        receiverConfig.maxLatencyMs = config->maxLatencyMs;
        receiverConfig.maxNetworkLatencyMs = config->maxNetworkLatencyMs;
        receiverConfig.insertStartCodes = 1;

        streamReceiver->receiver = ARSTREAM2_RtpReceiver_New(&receiverConfig, &ret);
        if (ret != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Error while creating receiver : %s", ARSTREAM2_Error_ToString(ret));
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

    ret = ARSTREAM2_RtpReceiver_Delete(&streamReceiver->receiver);
    if (ret != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Unable to delete receiver: %s", ARSTREAM2_Error_ToString(ret));
    }

    if (ret == ARSTREAM2_OK)
    {
        ret = ARSTREAM2_H264Filter_Free(&streamReceiver->filter);
        if (ret != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Unable to delete H264Filter: %s", ARSTREAM2_Error_ToString(ret));
        }
    }

    return ret;
}


void* ARSTREAM2_StreamReceiver_RunFilterThread(void *streamReceiverHandle)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)streamReceiverHandle;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid handle");
        return NULL;
    }

    return ARSTREAM2_H264Filter_RunFilterThread((void*)streamReceiver->filter);
}


void* ARSTREAM2_StreamReceiver_RunStreamThread(void *streamReceiverHandle)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)streamReceiverHandle;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid handle");
        return NULL;
    }

    return ARSTREAM2_RtpReceiver_RunStreamThread((void*)streamReceiver->receiver);
}


void* ARSTREAM2_StreamReceiver_RunControlThread(void *streamReceiverHandle)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)streamReceiverHandle;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid handle");
        return NULL;
    }

    return ARSTREAM2_RtpReceiver_RunControlThread((void*)streamReceiver->receiver);
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

    ret = ARSTREAM2_H264Filter_Start(streamReceiver->filter, spsPpsCallback, spsPpsCallbackUserPtr,
                              getAuBufferCallback, getAuBufferCallbackUserPtr,
                              auReadyCallback, auReadyCallbackUserPtr);
    if (ret != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Unable to start H264Filter: %s", ARSTREAM2_Error_ToString(ret));
        return ret;
    }

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

    ret = ARSTREAM2_H264Filter_Pause(streamReceiver->filter);
    if (ret != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Unable to pause H264Filter: %s", ARSTREAM2_Error_ToString(ret));
        return ret;
    }

    ARSTREAM2_RtpReceiver_InvalidateNaluBuffer(streamReceiver->receiver);

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

    return ARSTREAM2_H264Filter_GetSpsPps(streamReceiver->filter, spsBuffer, spsSize, ppsBuffer, ppsSize);
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
    resenderConfig.maxLatencyMs = config->maxLatencyMs;
    resenderConfig.maxNetworkLatencyMs = config->maxNetworkLatencyMs;

    retResender = ARSTREAM2_RtpReceiver_RtpResender_New(streamReceiver->receiver, &resenderConfig, &ret);
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

    ret = ARSTREAM2_RtpReceiver_RtpResender_Delete((ARSTREAM2_RtpReceiver_RtpResender_t**)resenderHandle);
    if (ret != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Error while deleting resender : %s", ARSTREAM2_Error_ToString(ret));
    }

    return ret;
}


void* ARSTREAM2_StreamReceiver_RunResenderStreamThread(void *resenderHandle)
{
    return ARSTREAM2_RtpReceiver_RtpResender_RunStreamThread(resenderHandle);
}


void* ARSTREAM2_StreamReceiver_RunResenderControlThread(void *resenderHandle)
{
    return ARSTREAM2_RtpReceiver_RtpResender_RunControlThread(resenderHandle);
}


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_StopResender(ARSTREAM2_StreamReceiver_ResenderHandle resenderHandle)
{
    ARSTREAM2_RtpReceiver_RtpResender_Stop((ARSTREAM2_RtpReceiver_RtpResender_t*)resenderHandle);

    return ARSTREAM2_OK;
}
