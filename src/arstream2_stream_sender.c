/**
 * @file arstream2_stream_sender.c
 * @brief Parrot Streaming Library - Stream Sender
 * @date 08/03/2016
 * @author aurelien.barre@parrot.com
 */

#include <stdio.h>
#include <stdlib.h>

#include <libARSAL/ARSAL_Print.h>

#include <libARStream2/arstream2_stream_sender.h>
#include "arstream2_rtp_sender.h"
#include "arstream2_stream_stats.h"


/**
 * Tag for ARSAL_PRINT
 */
#define ARSTREAM2_STREAM_SENDER_TAG "ARSTREAM2_StreamSender"


typedef struct ARSTREAM2_StreamSender_s
{
    ARSTREAM2_RtpSender_t *sender;
    ARSTREAM2_StreamSender_ReceiverReportCallback_t receiverReportCallback;
    void *receiverReportCallbackUserPtr;

    /* Debug files */
    char *friendlyName;
    char *dateAndTime;
    char *debugPath;
    ARSTREAM2_StreamStats_VideoStats_t videoStats;
    int videoStatsInitPending;

} ARSTREAM2_StreamSender_t;


static void ARSTREAM2_StreamSender_ReceiverReportCallback(const ARSTREAM2_StreamSender_ReceiverReportData_t *report, void *userPtr);


eARSTREAM2_ERROR ARSTREAM2_StreamSender_Init(ARSTREAM2_StreamSender_Handle *streamSenderHandle,
                                             const ARSTREAM2_StreamSender_Config_t *config)
{
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;
    ARSTREAM2_StreamSender_t *streamSender = NULL;

    if (!streamSenderHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid pointer for handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!config)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid pointer for config");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    streamSender = (ARSTREAM2_StreamSender_t*)malloc(sizeof(*streamSender));
    if (!streamSender)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Allocation failed (size %ld)", sizeof(*streamSender));
        ret = ARSTREAM2_ERROR_ALLOC;
    }

    if (ret == ARSTREAM2_OK)
    {
        memset(streamSender, 0, sizeof(*streamSender));
        streamSender->receiverReportCallback = config->receiverReportCallback;
        streamSender->receiverReportCallbackUserPtr = config->receiverReportCallbackUserPtr;
        if ((config->debugPath) && (strlen(config->debugPath)))
        {
            streamSender->debugPath = strdup(config->debugPath);
        }
        if ((config->friendlyName) && (strlen(config->friendlyName)))
        {
            streamSender->friendlyName = strndup(config->friendlyName, 40);
        }
        else if ((config->canonicalName) && (strlen(config->canonicalName)))
        {
            streamSender->friendlyName = strndup(config->canonicalName, 40);
        }
        char szDate[200];
        time_t rawtime;
        struct tm timeinfo;
        time(&rawtime);
        localtime_r(&rawtime, &timeinfo);
        /* Date format : <YYYY-MM-DDTHHMMSS+HHMM */
        strftime(szDate, 200, "%FT%H%M%S%z", &timeinfo);
        streamSender->dateAndTime = strndup(szDate, 200);
        streamSender->videoStatsInitPending = 1;
    }

    if (ret == ARSTREAM2_OK)
    {
        ARSTREAM2_RtpSender_Config_t senderConfig;
        int i;
        memset(&senderConfig, 0, sizeof(senderConfig));
        senderConfig.canonicalName = config->canonicalName;
        senderConfig.friendlyName = config->friendlyName;
        senderConfig.applicationName = config->applicationName;
        senderConfig.clientAddr = config->clientAddr;
        senderConfig.mcastAddr = config->mcastAddr;
        senderConfig.mcastIfaceAddr = config->mcastIfaceAddr;
        senderConfig.serverStreamPort = config->serverStreamPort;
        senderConfig.serverControlPort = config->serverControlPort;
        senderConfig.clientStreamPort = config->clientStreamPort;
        senderConfig.clientControlPort = config->clientControlPort;
        senderConfig.classSelector = config->classSelector;
        senderConfig.auCallback = config->auCallback;
        senderConfig.auCallbackUserPtr = config->auCallbackUserPtr;
        senderConfig.naluCallback = config->naluCallback;
        senderConfig.naluCallbackUserPtr = config->naluCallbackUserPtr;
        senderConfig.receiverReportCallback = ARSTREAM2_StreamSender_ReceiverReportCallback;
        senderConfig.receiverReportCallbackUserPtr = streamSender;
        senderConfig.disconnectionCallback = config->disconnectionCallback;
        senderConfig.disconnectionCallbackUserPtr = config->disconnectionCallbackUserPtr;
        senderConfig.naluFifoSize = config->naluFifoSize;
        senderConfig.maxPacketSize = config->maxPacketSize;
        senderConfig.targetPacketSize = config->targetPacketSize;
        senderConfig.streamSocketBufferSize = config->streamSocketBufferSize;
        senderConfig.maxBitrate = config->maxBitrate;
        senderConfig.maxLatencyMs = config->maxLatencyMs;
        senderConfig.debugPath = streamSender->debugPath;
        senderConfig.dateAndTime = streamSender->dateAndTime;
        for (i = 0; i < ARSTREAM2_STREAM_SENDER_MAX_IMPORTANCE_LEVELS; i++)
        {
            senderConfig.maxNetworkLatencyMs[i] = config->maxNetworkLatencyMs[i];
        }
        senderConfig.useRtpHeaderExtensions = config->useRtpHeaderExtensions;

        streamSender->sender = ARSTREAM2_RtpSender_New(&senderConfig, &ret);
        if (ret != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Error while creating sender : %s", ARSTREAM2_Error_ToString(ret));
        }
    }

    if (ret == ARSTREAM2_OK)
    {
        *streamSenderHandle = (ARSTREAM2_StreamSender_Handle*)streamSender;
    }
    else
    {
        if (streamSender)
        {
            if (streamSender->sender) ARSTREAM2_RtpSender_Delete(&(streamSender->sender));
            ARSTREAM2_StreamStats_VideoStatsFileClose(&streamSender->videoStats);
            free(streamSender->debugPath);
            free(streamSender->friendlyName);
            free(streamSender->dateAndTime);
            free(streamSender);
        }
        *streamSenderHandle = NULL;
    }

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_StreamSender_Stop(ARSTREAM2_StreamSender_Handle streamSenderHandle)
{
    ARSTREAM2_StreamSender_t *streamSender = (ARSTREAM2_StreamSender_t*)streamSenderHandle;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;

    if (!streamSenderHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    ARSTREAM2_RtpSender_Stop(streamSender->sender);

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_StreamSender_Free(ARSTREAM2_StreamSender_Handle *streamSenderHandle)
{
    ARSTREAM2_StreamSender_t* streamSender;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;

    if ((!streamSenderHandle) || (!*streamSenderHandle))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid pointer for handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    streamSender = (ARSTREAM2_StreamSender_t*)*streamSenderHandle;

    ret = ARSTREAM2_RtpSender_Delete(&streamSender->sender);
    if (ret != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Unable to delete sender: %s", ARSTREAM2_Error_ToString(ret));
    }

    ARSTREAM2_StreamStats_VideoStatsFileClose(&streamSender->videoStats);
    free(streamSender->debugPath);
    free(streamSender->friendlyName);
    free(streamSender->dateAndTime);

    free(streamSender);
    *streamSenderHandle = NULL;

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_StreamSender_SendNewNalu(ARSTREAM2_StreamSender_Handle streamSenderHandle,
                                                    const ARSTREAM2_StreamSender_H264NaluDesc_t *nalu,
                                                    uint64_t inputTime)
{
    ARSTREAM2_StreamSender_t *streamSender = (ARSTREAM2_StreamSender_t*)streamSenderHandle;

    if (!streamSenderHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!nalu)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid pointer");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    return ARSTREAM2_RtpSender_SendNewNalu(streamSender->sender, nalu, inputTime);
}


eARSTREAM2_ERROR ARSTREAM2_StreamSender_SendNNewNalu(ARSTREAM2_StreamSender_Handle streamSenderHandle,
                                                     const ARSTREAM2_StreamSender_H264NaluDesc_t *nalu,
                                                     int naluCount, uint64_t inputTime)
{
    ARSTREAM2_StreamSender_t *streamSender = (ARSTREAM2_StreamSender_t*)streamSenderHandle;

    if (!streamSenderHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!nalu)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid pointer");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    return ARSTREAM2_RtpSender_SendNNewNalu(streamSender->sender, nalu, naluCount, inputTime);
}


eARSTREAM2_ERROR ARSTREAM2_StreamSender_FlushNaluQueue(ARSTREAM2_StreamSender_Handle streamSenderHandle)
{
    ARSTREAM2_StreamSender_t *streamSender = (ARSTREAM2_StreamSender_t*)streamSenderHandle;

    if (!streamSenderHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    return ARSTREAM2_RtpSender_FlushNaluQueue(streamSender->sender);
}


void* ARSTREAM2_StreamSender_RunThread(void *streamSenderHandle)
{
    ARSTREAM2_StreamSender_t *streamSender = (ARSTREAM2_StreamSender_t*)streamSenderHandle;

    if (!streamSenderHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid handle");
        return (void*)NULL;
    }

    return ARSTREAM2_RtpSender_RunThread(streamSender->sender);
}


eARSTREAM2_ERROR ARSTREAM2_StreamSender_GetDynamicConfig(ARSTREAM2_StreamSender_Handle streamSenderHandle,
                                                         ARSTREAM2_StreamSender_DynamicConfig_t *config)
{
    ARSTREAM2_StreamSender_t *streamSender = (ARSTREAM2_StreamSender_t*)streamSenderHandle;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;
    int i;

    if (!streamSenderHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!config)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid config");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    ARSTREAM2_RtpSender_DynamicConfig_t senderConfig;
    memset(&senderConfig, 0, sizeof(senderConfig));
    ret = ARSTREAM2_RtpSender_GetDynamicConfig(streamSender->sender, &senderConfig);
    if (ret != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid config");
        return ret;
    }

    config->targetPacketSize = senderConfig.targetPacketSize;
    config->streamSocketBufferSize = senderConfig.streamSocketBufferSize;
    config->maxBitrate = senderConfig.maxBitrate;
    config->maxLatencyMs = senderConfig.maxLatencyMs;
    for (i = 0; i < ARSTREAM2_STREAM_SENDER_MAX_IMPORTANCE_LEVELS; i++)
    {
        config->maxNetworkLatencyMs[i] = senderConfig.maxNetworkLatencyMs[i];
    }

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_StreamSender_SetDynamicConfig(ARSTREAM2_StreamSender_Handle streamSenderHandle,
                                                         const ARSTREAM2_StreamSender_DynamicConfig_t *config)
{
    ARSTREAM2_StreamSender_t *streamSender = (ARSTREAM2_StreamSender_t*)streamSenderHandle;
    int i;

    if (!streamSenderHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!config)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid config");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    ARSTREAM2_RtpSender_DynamicConfig_t senderConfig;
    memset(&senderConfig, 0, sizeof(senderConfig));
    senderConfig.targetPacketSize = config->targetPacketSize;
    senderConfig.streamSocketBufferSize = config->streamSocketBufferSize;
    senderConfig.maxBitrate = config->maxBitrate;
    senderConfig.maxLatencyMs = config->maxLatencyMs;
    for (i = 0; i < ARSTREAM2_STREAM_SENDER_MAX_IMPORTANCE_LEVELS; i++)
    {
        senderConfig.maxNetworkLatencyMs[i] = config->maxNetworkLatencyMs[i];
    }

    return ARSTREAM2_RtpSender_SetDynamicConfig(streamSender->sender, &senderConfig);
}


eARSTREAM2_ERROR ARSTREAM2_StreamSender_GetSdesItem(ARSTREAM2_StreamSender_Handle streamSenderHandle,
                                                      uint8_t type, const char *prefix, char **value, uint32_t *sendInterval)
{
    ARSTREAM2_StreamSender_t* streamSender = (ARSTREAM2_StreamSender_t*)streamSenderHandle;

    if (!streamSenderHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    return ARSTREAM2_RtpSender_GetSdesItem(streamSender->sender, type, prefix, value, sendInterval);
}


eARSTREAM2_ERROR ARSTREAM2_StreamSender_SetSdesItem(ARSTREAM2_StreamSender_Handle streamSenderHandle,
                                                      uint8_t type, const char *prefix, const char *value, uint32_t sendInterval)
{
    ARSTREAM2_StreamSender_t* streamSender = (ARSTREAM2_StreamSender_t*)streamSenderHandle;

    if (!streamSenderHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    return ARSTREAM2_RtpSender_SetSdesItem(streamSender->sender, type, prefix, value, sendInterval);
}


eARSTREAM2_ERROR ARSTREAM2_StreamSender_GetPeerSdesItem(ARSTREAM2_StreamSender_Handle streamSenderHandle,
                                                          uint8_t type, const char *prefix, char **value)
{
    ARSTREAM2_StreamSender_t* streamSender = (ARSTREAM2_StreamSender_t*)streamSenderHandle;

    if (!streamSenderHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    return ARSTREAM2_RtpSender_GetPeerSdesItem(streamSender->sender, type, prefix, value);
}


eARSTREAM2_ERROR ARSTREAM2_StreamSender_GetMonitoring(ARSTREAM2_StreamSender_Handle streamSenderHandle,
                                                      uint64_t startTime, uint32_t timeIntervalUs,
                                                      ARSTREAM2_StreamSender_MonitoringData_t *monitoringData)
{
    ARSTREAM2_StreamSender_t *streamSender = (ARSTREAM2_StreamSender_t*)streamSenderHandle;

    if (!streamSenderHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!monitoringData)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid pointer");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    return ARSTREAM2_RtpSender_GetMonitoring(streamSender->sender, startTime, timeIntervalUs, monitoringData);
}


static void ARSTREAM2_StreamSender_ReceiverReportCallback(const ARSTREAM2_StreamSender_ReceiverReportData_t *report, void *userPtr)
{
    ARSTREAM2_StreamSender_t *streamSender = (ARSTREAM2_StreamSender_t*)userPtr;

    if (!streamSender)
    {
        return;
    }

    if ((report) && (report->videoStats))
    {
        /* Map the video stats */
        ARSTREAM2_H264_VideoStats_t vs;
        uint32_t i, j;
        memset(&vs, 0, sizeof(ARSTREAM2_H264_VideoStats_t));
        if (streamSender->videoStatsInitPending)
        {
            ARSTREAM2_StreamStats_VideoStatsFileOpen(&streamSender->videoStats, streamSender->debugPath, streamSender->friendlyName,
                                                     streamSender->dateAndTime, report->videoStats->mbStatusZoneCount, report->videoStats->mbStatusClassCount);
            streamSender->videoStatsInitPending = 0;
        }
        vs.timestamp = report->videoStats->timestamp;
        vs.rssi = report->videoStats->rssi;
        vs.totalFrameCount = report->videoStats->totalFrameCount;
        vs.outputFrameCount = report->videoStats->outputFrameCount;
        vs.erroredOutputFrameCount = report->videoStats->erroredOutputFrameCount;
        vs.missedFrameCount = report->videoStats->missedFrameCount;
        vs.discardedFrameCount = report->videoStats->discardedFrameCount;
        vs.timestampDeltaIntegral = report->videoStats->timestampDeltaIntegral;
        vs.timestampDeltaIntegralSq = report->videoStats->timestampDeltaIntegralSq;
        vs.timingErrorIntegral = report->videoStats->timingErrorIntegral;
        vs.timingErrorIntegralSq = report->videoStats->timingErrorIntegralSq;
        vs.estimatedLatencyIntegral = report->videoStats->estimatedLatencyIntegral;
        vs.estimatedLatencyIntegralSq = report->videoStats->estimatedLatencyIntegralSq;
        vs.erroredSecondCount = report->videoStats->erroredSecondCount;
        if (report->videoStats->mbStatusZoneCount <= ARSTREAM2_H264_MB_STATUS_ZONE_MAX_COUNT)
        {
            vs.mbStatusZoneCount = report->videoStats->mbStatusZoneCount;
            for (i = 0; i < vs.mbStatusZoneCount; i++)
            {
                vs.erroredSecondCountByZone[i] = report->videoStats->erroredSecondCountByZone[i];
            }
            if (report->videoStats->mbStatusClassCount <= ARSTREAM2_H264_MB_STATUS_CLASS_MAX_COUNT)
            {
                vs.mbStatusClassCount = report->videoStats->mbStatusClassCount;
                for (j = 0; j < vs.mbStatusClassCount; j++)
                {
                    for (i = 0; i < vs.mbStatusZoneCount; i++)
                    {
                        vs.macroblockStatus[j][i] = report->videoStats->macroblockStatus[j * vs.mbStatusZoneCount + i];
                    }
                }
            }
        }

        ARSTREAM2_StreamStats_VideoStatsFileWrite(&streamSender->videoStats, &vs);
    }

    if (streamSender->receiverReportCallback)
    {
        /* Call the receiver report callback function */
        streamSender->receiverReportCallback(report, streamSender->receiverReportCallbackUserPtr);
    }
}
