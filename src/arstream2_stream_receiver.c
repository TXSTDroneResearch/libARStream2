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



eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_Init(ARSTREAM2_StreamReceiver_Handle *streamReceiverHandle,
                                               ARSTREAM2_StreamReceiver_Config_t *config,
                                               ARSTREAM2_StreamReceiver_NetConfig_t *net_config,
                                               ARSTREAM2_StreamReceiver_MuxConfig_t *mux_config)
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
        ARSTREAM2_RtpReceiver_NetConfig_t receiver_net_config;
        ARSTREAM2_RtpReceiver_MuxConfig_t receiver_mux_config;
        memset(&receiverConfig, 0, sizeof(receiverConfig));
        memset(&receiver_net_config, 0, sizeof(receiver_net_config));
        memset(&receiver_mux_config, 0, sizeof(receiver_mux_config));


        receiverConfig.naluCallback = ARSTREAM2_H264Filter_RtpReceiverNaluCallback;
        receiverConfig.naluCallbackUserPtr = (void*)streamReceiver->filter;
        receiverConfig.maxPacketSize = config->maxPacketSize;
        receiverConfig.maxBitrate = config->maxBitrate;
        receiverConfig.maxLatencyMs = config->maxLatencyMs;
        receiverConfig.maxNetworkLatencyMs = config->maxNetworkLatencyMs;
        receiverConfig.insertStartCodes = 1;

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

    ret = ARSTREAM2_H264Filter_Free(&streamReceiver->filter);
    if (ret != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Unable to delete H264Filter: %s", ARSTREAM2_Error_ToString(ret));
    }

    free(streamReceiver);
    *streamReceiverHandle = NULL;

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


eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_GetFrameMacroblockStatus(ARSTREAM2_StreamReceiver_Handle streamReceiverHandle, uint8_t **macroblocks, int *mbWidth, int *mbHeight)
{
    ARSTREAM2_StreamReceiver_t* streamReceiver = (ARSTREAM2_StreamReceiver_t*)streamReceiverHandle;

    if (!streamReceiverHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

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
