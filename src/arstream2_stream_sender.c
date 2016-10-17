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
#include "arstream2_stream_stats_internal.h"


/**
 * Tag for ARSAL_PRINT
 */
#define ARSTREAM2_STREAM_SENDER_TAG "ARSTREAM2_StreamSender"

#define ARSTREAM2_STREAM_SENDER_UNTIMED_METADATA_DEFAULT_SEND_INTERVAL (5000000)


typedef struct ARSTREAM2_StreamSender_s
{
    ARSTREAM2_RtpSender_t *sender;
    ARSTREAM2_StreamSender_RtpStatsCallback_t rtpStatsCallback;
    void *rtpStatsCallbackUserPtr;
    ARSTREAM2_StreamSender_VideoStatsCallback_t videoStatsCallback;
    void *videoStatsCallbackUserPtr;
    ARSTREAM2_StreamStats_VideoStats_t videoStatsForCb;

    /* Debug files */
    char *friendlyName;
    char *dateAndTime;
    char *debugPath;
    ARSTREAM2_StreamStats_RtpStatsContext_t rtpStatsCtx;
    int8_t lastKnownRssi;
    ARSTREAM2_StreamStats_VideoStatsContext_t videoStatsCtx;
    int videoStatsInitPending;

} ARSTREAM2_StreamSender_t;


static void ARSTREAM2_StreamSender_RtpStatsCallback(const ARSTREAM2_RTP_RtpStats_t *rtpStats, void *userPtr);
static void ARSTREAM2_StreamSender_VideoStatsCallback(const ARSTREAM2_H264_VideoStats_t *videoStats, void *userPtr);


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
        streamSender->rtpStatsCallback = config->rtpStatsCallback;
        streamSender->rtpStatsCallbackUserPtr = config->rtpStatsCallbackUserPtr;
        streamSender->videoStatsCallback = config->videoStatsCallback;
        streamSender->videoStatsCallbackUserPtr = config->videoStatsCallbackUserPtr;
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
        ARSTREAM2_StreamStats_RtpStatsFileOpen(&streamSender->rtpStatsCtx, streamSender->debugPath,
                                               streamSender->friendlyName, streamSender->dateAndTime);
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
        senderConfig.rtpStatsCallback = ARSTREAM2_StreamSender_RtpStatsCallback;
        senderConfig.rtpStatsCallbackUserPtr = streamSender;
        senderConfig.videoStatsCallback = ARSTREAM2_StreamSender_VideoStatsCallback;
        senderConfig.videoStatsCallbackUserPtr = streamSender;
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
            ARSTREAM2_StreamStats_RtpStatsFileClose(&streamSender->rtpStatsCtx);
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

    ARSTREAM2_StreamStats_VideoStatsFileClose(&streamSender->videoStatsCtx);
    ARSTREAM2_StreamStats_RtpStatsFileClose(&streamSender->rtpStatsCtx);
    free(streamSender->debugPath);
    free(streamSender->friendlyName);
    free(streamSender->dateAndTime);
    free(streamSender->videoStatsForCb.erroredSecondCountByZone);
    free(streamSender->videoStatsForCb.macroblockStatus);

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


eARSTREAM2_ERROR ARSTREAM2_StreamSender_GetUntimedMetadata(ARSTREAM2_StreamSender_Handle streamSenderHandle,
                                                           ARSTREAM2_StreamSender_UntimedMetadata_t *metadata, uint32_t *sendInterval)
{
    ARSTREAM2_StreamSender_t *streamSender = (ARSTREAM2_StreamSender_t*)streamSenderHandle;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK, _ret;
    uint32_t _sendInterval = 0, minSendInterval = (uint32_t)(-1);
    char *ptr;

    if (!streamSenderHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!metadata)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid metadata");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    _ret = ARSTREAM2_RtpSender_GetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_CNAME_ITEM, NULL, &metadata->canonicalName, &_sendInterval);
    if (_ret == ARSTREAM2_OK)
    {
        if (_sendInterval < minSendInterval)
        {
            minSendInterval = _sendInterval;
        }
    }
    else
    {
        metadata->canonicalName = NULL;
    }

    _ret = ARSTREAM2_RtpSender_GetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_NAME_ITEM, NULL, &metadata->friendlyName, &_sendInterval);
    if (_ret == ARSTREAM2_OK)
    {
        if (_sendInterval < minSendInterval)
        {
            minSendInterval = _sendInterval;
        }
    }
    else
    {
        metadata->friendlyName = NULL;
    }

    _ret = ARSTREAM2_RtpSender_GetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_TOOL_ITEM, NULL, &metadata->applicationName, &_sendInterval);
    if (_ret == ARSTREAM2_OK)
    {
        if (_sendInterval < minSendInterval)
        {
            minSendInterval = _sendInterval;
        }
    }
    else
    {
        metadata->applicationName = NULL;
    }

    ptr = NULL;
    _ret = ARSTREAM2_RtpSender_GetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_LOC_ITEM, NULL, &ptr, &_sendInterval);
    if (_ret == ARSTREAM2_OK)
    {
        if (_sendInterval < minSendInterval)
        {
            minSendInterval = _sendInterval;
        }
        if (ptr)
        {
            if (sscanf(ptr, "%lf,%lf,%f", &metadata->takeoffLatitude, &metadata->takeoffLongitude, &metadata->takeoffAltitude) != 3)
            {
                metadata->takeoffLatitude = 500.;
                metadata->takeoffLongitude = 500.;
                metadata->takeoffAltitude = 0.;
            }
        }
    }
    else
    {
        metadata->takeoffLatitude = 500.;
        metadata->takeoffLongitude = 500.;
        metadata->takeoffAltitude = 0.;
    }

    ptr = NULL;
    _ret = ARSTREAM2_RtpSender_GetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_PRIV_ITEM, "picture_hfov", &ptr, &_sendInterval);
    if (_ret == ARSTREAM2_OK)
    {
        if (_sendInterval < minSendInterval)
        {
            minSendInterval = _sendInterval;
        }
        if (ptr)
        {
            if (sscanf(ptr, "%f", &metadata->pictureHFov) != 1)
            {
                metadata->pictureHFov = 0.;
            }
        }
    }
    else
    {
        metadata->pictureHFov = 0.;
    }

    ptr = NULL;
    _ret = ARSTREAM2_RtpSender_GetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_PRIV_ITEM, "picture_vfov", &ptr, &_sendInterval);
    if (_ret == ARSTREAM2_OK)
    {
        if (_sendInterval < minSendInterval)
        {
            minSendInterval = _sendInterval;
        }
        if (ptr)
        {
            if (sscanf(ptr, "%f", &metadata->pictureVFov) != 1)
            {
                metadata->pictureVFov = 0.;
            }
        }
    }
    else
    {
        metadata->pictureVFov = 0.;
    }

    _ret = ARSTREAM2_RtpSender_GetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_PRIV_ITEM, "run_date", &metadata->runDate, &_sendInterval);
    if (_ret == ARSTREAM2_OK)
    {
        if (_sendInterval < minSendInterval)
        {
            minSendInterval = _sendInterval;
        }
    }
    else
    {
        metadata->runDate = NULL;
    }

    _ret = ARSTREAM2_RtpSender_GetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_PRIV_ITEM, "run_uuid", &metadata->runUuid, &_sendInterval);
    if (_ret == ARSTREAM2_OK)
    {
        if (_sendInterval < minSendInterval)
        {
            minSendInterval = _sendInterval;
        }
    }
    else
    {
        metadata->runUuid = NULL;
    }

    if (sendInterval)
    {
        *sendInterval = minSendInterval;
    }

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_StreamSender_SetUntimedMetadata(ARSTREAM2_StreamSender_Handle streamSenderHandle,
                                                           const ARSTREAM2_StreamSender_UntimedMetadata_t *metadata, uint32_t sendInterval)
{
    ARSTREAM2_StreamSender_t *streamSender = (ARSTREAM2_StreamSender_t*)streamSenderHandle;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK, _ret;
    char *ptr;

    if (!streamSenderHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!metadata)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid metadata");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    if (sendInterval == 0)
    {
        sendInterval = ARSTREAM2_STREAM_SENDER_UNTIMED_METADATA_DEFAULT_SEND_INTERVAL;
    }

    if ((metadata->canonicalName) && (strlen(metadata->canonicalName)))
    {
        ptr = NULL;
        _ret = ARSTREAM2_RtpSender_GetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_CNAME_ITEM, NULL, &ptr, NULL);
        if ((_ret != ARSTREAM2_OK) || (strncmp(ptr, metadata->canonicalName, 256)))
        {
            ARSTREAM2_RtpSender_SetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_CNAME_ITEM, NULL, metadata->canonicalName, sendInterval);
        }
    }

    if ((metadata->friendlyName) && (strlen(metadata->friendlyName)))
    {
        ptr = NULL;
        _ret = ARSTREAM2_RtpSender_GetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_NAME_ITEM, NULL, &ptr, NULL);
        if ((_ret != ARSTREAM2_OK) || (strncmp(ptr, metadata->friendlyName, 256)))
        {
            ARSTREAM2_RtpSender_SetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_NAME_ITEM, NULL, metadata->friendlyName, sendInterval);
        }
    }

    if ((metadata->applicationName) && (strlen(metadata->applicationName)))
    {
        ptr = NULL;
        _ret = ARSTREAM2_RtpSender_GetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_TOOL_ITEM, NULL, &ptr, NULL);
        if ((_ret != ARSTREAM2_OK) || (strncmp(ptr, metadata->applicationName, 256)))
        {
            ARSTREAM2_RtpSender_SetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_TOOL_ITEM, NULL, metadata->applicationName, sendInterval);
        }
    }

    if ((metadata->takeoffLatitude != 500.) && (metadata->takeoffLongitude != 500.))
    {
        double takeoffLatitude = 500.;
        double takeoffLongitude = 500.;
        float takeoffAltitude = 0.;
        ptr = NULL;
        _ret = ARSTREAM2_RtpSender_GetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_LOC_ITEM, NULL, &ptr, NULL);
        if (_ret == ARSTREAM2_OK)
        {
            if (ptr)
            {
                if (sscanf(ptr, "%lf,%lf,%f", &takeoffLatitude, &takeoffLongitude, &takeoffAltitude) != 3)
                {
                    takeoffLatitude = 500.;
                    takeoffLongitude = 500.;
                    takeoffAltitude = 0.;
                }
            }
        }
        if ((takeoffLatitude != metadata->takeoffLatitude) || (takeoffLongitude != metadata->takeoffLongitude) || (takeoffAltitude != metadata->takeoffAltitude))
        {
            char str[100];
            snprintf(str, sizeof(str), "%.8f,%.8f,%.8f", metadata->takeoffLatitude, metadata->takeoffLongitude, metadata->takeoffAltitude);
            ARSTREAM2_RtpSender_SetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_LOC_ITEM, NULL, str, sendInterval);
        }
    }

    if (metadata->pictureHFov != 0.)
    {
        float pictureHFov = 0.;
        ptr = NULL;
        _ret = ARSTREAM2_RtpSender_GetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_PRIV_ITEM, "picture_hfov", &ptr, NULL);
        if (_ret == ARSTREAM2_OK)
        {
            if (ptr)
            {
                if (sscanf(ptr, "%f", &pictureHFov) != 1)
                {
                    pictureHFov = 0.;
                }
            }
        }
        if (pictureHFov != metadata->pictureHFov)
        {
            char str[100];
            snprintf(str, sizeof(str), "%.2f", metadata->pictureHFov);
            ARSTREAM2_RtpSender_SetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_PRIV_ITEM, "picture_hfov", str, sendInterval);
        }
    }

    if (metadata->pictureVFov != 0.)
    {
        float pictureVFov = 0.;
        ptr = NULL;
        _ret = ARSTREAM2_RtpSender_GetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_PRIV_ITEM, "picture_vfov", &ptr, NULL);
        if (_ret == ARSTREAM2_OK)
        {
            if (ptr)
            {
                if (sscanf(ptr, "%f", &pictureVFov) != 1)
                {
                    pictureVFov = 0.;
                }
            }
        }
        if (pictureVFov != metadata->pictureVFov)
        {
            char str[100];
            snprintf(str, sizeof(str), "%.2f", metadata->pictureVFov);
            ARSTREAM2_RtpSender_SetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_PRIV_ITEM, "picture_vfov", str, sendInterval);
        }
    }

    if ((metadata->runDate) && (strlen(metadata->runDate)))
    {
        ptr = NULL;
        _ret = ARSTREAM2_RtpSender_GetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_PRIV_ITEM, "run_date", &ptr, NULL);
        if ((_ret != ARSTREAM2_OK) || (strncmp(ptr, metadata->runDate, 256)))
        {
            ARSTREAM2_RtpSender_SetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_PRIV_ITEM, "run_date", metadata->runDate, sendInterval);
        }
    }

    if ((metadata->runUuid) && (strlen(metadata->runUuid)))
    {
        ptr = NULL;
        _ret = ARSTREAM2_RtpSender_GetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_PRIV_ITEM, "run_uuid", &ptr, NULL);
        if ((_ret != ARSTREAM2_OK) || (strncmp(ptr, metadata->runUuid, 256)))
        {
            ARSTREAM2_RtpSender_SetSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_PRIV_ITEM, "run_uuid", metadata->runUuid, sendInterval);
        }
    }

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_StreamSender_GetPeerUntimedMetadata(ARSTREAM2_StreamSender_Handle streamSenderHandle,
                                                               ARSTREAM2_StreamSender_UntimedMetadata_t *metadata)
{
    ARSTREAM2_StreamSender_t *streamSender = (ARSTREAM2_StreamSender_t*)streamSenderHandle;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK, _ret;
    char *ptr;

    if (!streamSenderHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!metadata)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_SENDER_TAG, "Invalid metadata");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    _ret = ARSTREAM2_RtpSender_GetPeerSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_CNAME_ITEM, NULL, &metadata->canonicalName);
    if (_ret != ARSTREAM2_OK)
    {
        metadata->canonicalName = NULL;
    }

    _ret = ARSTREAM2_RtpSender_GetPeerSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_NAME_ITEM, NULL, &metadata->friendlyName);
    if (_ret != ARSTREAM2_OK)
    {
        metadata->friendlyName = NULL;
    }

    _ret = ARSTREAM2_RtpSender_GetPeerSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_TOOL_ITEM, NULL, &metadata->applicationName);
    if (_ret != ARSTREAM2_OK)
    {
        metadata->applicationName = NULL;
    }

    ptr = NULL;
    _ret = ARSTREAM2_RtpSender_GetPeerSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_LOC_ITEM, NULL, &ptr);
    if (_ret == ARSTREAM2_OK)
    {
        if (ptr)
        {
            if (sscanf(ptr, "%lf,%lf,%f", &metadata->takeoffLatitude, &metadata->takeoffLongitude, &metadata->takeoffAltitude) != 3)
            {
                metadata->takeoffLatitude = 500.;
                metadata->takeoffLongitude = 500.;
                metadata->takeoffAltitude = 0.;
            }
        }
    }
    else
    {
        metadata->takeoffLatitude = 500.;
        metadata->takeoffLongitude = 500.;
        metadata->takeoffAltitude = 0.;
    }

    ptr = NULL;
    _ret = ARSTREAM2_RtpSender_GetPeerSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_PRIV_ITEM, "picture_hfov", &ptr);
    if (_ret == ARSTREAM2_OK)
    {
        if (ptr)
        {
            if (sscanf(ptr, "%f", &metadata->pictureHFov) != 1)
            {
                metadata->pictureHFov = -1.;
            }
        }
    }
    else
    {
        metadata->pictureHFov = -1.;
    }

    ptr = NULL;
    _ret = ARSTREAM2_RtpSender_GetPeerSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_PRIV_ITEM, "picture_vfov", &ptr);
    if (_ret == ARSTREAM2_OK)
    {
        if (ptr)
        {
            if (sscanf(ptr, "%f", &metadata->pictureVFov) != 1)
            {
                metadata->pictureVFov = -1.;
            }
        }
    }
    else
    {
        metadata->pictureVFov = -1.;
    }

    _ret = ARSTREAM2_RtpSender_GetPeerSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_PRIV_ITEM, "run_date", &metadata->runDate);
    if (_ret != ARSTREAM2_OK)
    {
        metadata->runDate = NULL;
    }

    _ret = ARSTREAM2_RtpSender_GetPeerSdesItem(streamSender->sender, ARSTREAM2_RTCP_SDES_PRIV_ITEM, "run_uuid", &metadata->runUuid);
    if (_ret != ARSTREAM2_OK)
    {
        metadata->runUuid = NULL;
    }

    return ret;
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


static void ARSTREAM2_StreamSender_RtpStatsCallback(const ARSTREAM2_RTP_RtpStats_t *rtpStats, void *userPtr)
{
    ARSTREAM2_StreamSender_t *streamSender = (ARSTREAM2_StreamSender_t*)userPtr;

    if (!streamSender)
    {
        return;
    }

    if (rtpStats)
    {
        ARSTREAM2_RTP_RtpStats_t s;
        memcpy(&s, rtpStats, sizeof(ARSTREAM2_RTP_RtpStats_t));
        s.rssi = streamSender->lastKnownRssi;
        ARSTREAM2_StreamStats_RtpStatsFileWrite(&streamSender->rtpStatsCtx, &s);

        if (streamSender->rtpStatsCallback)
        {
            ARSTREAM2_StreamStats_RtpStats_t rtpsOut;
            memset(&rtpsOut, 0, sizeof(ARSTREAM2_StreamStats_RtpStats_t));

            /* Map the RTP stats */
            rtpsOut.timestamp = rtpStats->timestamp;
            rtpsOut.rssi = streamSender->lastKnownRssi;
            rtpsOut.roundTripDelay = rtpStats->roundTripDelay;
            rtpsOut.interarrivalJitter = rtpStats->interarrivalJitter;
            rtpsOut.receiverLostCount = rtpStats->receiverLostCount;
            rtpsOut.receiverFractionLost = rtpStats->receiverFractionLost;
            rtpsOut.receiverExtHighestSeqNum = rtpStats->receiverExtHighestSeqNum;
            rtpsOut.lastSenderReportInterval = rtpStats->lastSenderReportInterval;
            rtpsOut.senderReportIntervalPacketCount = rtpStats->senderReportIntervalPacketCount;
            rtpsOut.senderReportIntervalByteCount = rtpStats->senderReportIntervalByteCount;
            rtpsOut.senderPacketCount = rtpStats->senderPacketCount;
            rtpsOut.senderByteCount = rtpStats->senderByteCount;
            rtpsOut.peerClockDelta = rtpStats->peerClockDelta;
            rtpsOut.roundTripDelayFromClockDelta = rtpStats->roundTripDelayFromClockDelta;

            /* Call the receiver report callback function */
            streamSender->rtpStatsCallback(&rtpsOut, streamSender->rtpStatsCallbackUserPtr);
        }
    }
}


static void ARSTREAM2_StreamSender_VideoStatsCallback(const ARSTREAM2_H264_VideoStats_t *videoStats, void *userPtr)
{
    ARSTREAM2_StreamSender_t *streamSender = (ARSTREAM2_StreamSender_t*)userPtr;

    if (!streamSender)
    {
        return;
    }

    if (videoStats)
    {
        if (streamSender->videoStatsInitPending)
        {
            ARSTREAM2_StreamStats_VideoStatsFileOpen(&streamSender->videoStatsCtx, streamSender->debugPath, streamSender->friendlyName,
                                                     streamSender->dateAndTime, videoStats->mbStatusZoneCount, videoStats->mbStatusClassCount);
            streamSender->videoStatsInitPending = 0;
        }
        streamSender->lastKnownRssi = videoStats->rssi;

        ARSTREAM2_StreamStats_VideoStatsFileWrite(&streamSender->videoStatsCtx, videoStats);

        if (streamSender->videoStatsCallback)
        {
            /* Map the video stats */
            ARSTREAM2_StreamStats_VideoStats_t *vsOut = &streamSender->videoStatsForCb;
            uint32_t i, j;

            vsOut->timestamp = videoStats->timestamp;
            vsOut->rssi = videoStats->rssi;
            vsOut->totalFrameCount = videoStats->totalFrameCount;
            vsOut->outputFrameCount = videoStats->outputFrameCount;
            vsOut->erroredOutputFrameCount = videoStats->erroredOutputFrameCount;
            vsOut->missedFrameCount = videoStats->missedFrameCount;
            vsOut->discardedFrameCount = videoStats->discardedFrameCount;
            vsOut->timestampDeltaIntegral = videoStats->timestampDeltaIntegral;
            vsOut->timestampDeltaIntegralSq = videoStats->timestampDeltaIntegralSq;
            vsOut->timingErrorIntegral = videoStats->timingErrorIntegral;
            vsOut->timingErrorIntegralSq = videoStats->timingErrorIntegralSq;
            vsOut->estimatedLatencyIntegral = videoStats->estimatedLatencyIntegral;
            vsOut->estimatedLatencyIntegralSq = videoStats->estimatedLatencyIntegralSq;
            vsOut->erroredSecondCount = videoStats->erroredSecondCount;
            if (videoStats->mbStatusZoneCount)
            {
                if ((!vsOut->erroredSecondCountByZone) || (videoStats->mbStatusZoneCount > vsOut->mbStatusZoneCount))
                {
                    vsOut->erroredSecondCountByZone = realloc(vsOut->erroredSecondCountByZone, videoStats->mbStatusZoneCount * sizeof(uint32_t));
                    if (!vsOut->erroredSecondCountByZone)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_STREAM_SENDER_TAG, "Allocation failed");
                    }
                    vsOut->mbStatusZoneCount = videoStats->mbStatusZoneCount;
                }
                if (vsOut->erroredSecondCountByZone)
                {
                    for (i = 0; i < vsOut->mbStatusZoneCount; i++)
                    {
                        vsOut->erroredSecondCountByZone[i] = videoStats->erroredSecondCountByZone[i];
                    }
                }

                if (videoStats->mbStatusClassCount)
                {
                    if ((!vsOut->macroblockStatus) || (videoStats->mbStatusClassCount > vsOut->mbStatusClassCount))
                    {
                        vsOut->macroblockStatus = realloc(vsOut->macroblockStatus, videoStats->mbStatusClassCount * vsOut->mbStatusZoneCount * sizeof(uint32_t));
                        if (!vsOut->macroblockStatus)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_STREAM_SENDER_TAG, "Allocation failed");
                        }
                        vsOut->mbStatusClassCount = videoStats->mbStatusClassCount;
                    }
                    if (vsOut->macroblockStatus)
                    {
                        for (j = 0; j < vsOut->mbStatusClassCount; j++)
                        {
                            for (i = 0; i < vsOut->mbStatusZoneCount; i++)
                            {
                                vsOut->macroblockStatus[j * vsOut->mbStatusZoneCount + i] = videoStats->macroblockStatus[j][i];
                            }
                        }
                    }
                }
            }

            /* Call the video stats reception callback function */
            streamSender->videoStatsCallback(vsOut, streamSender->videoStatsCallbackUserPtr);
        }
    }
}
