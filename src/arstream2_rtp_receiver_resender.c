/**
 * @file arstream2_rtp_receiver_resender.c
 * @brief Parrot Streaming Library - RTP Receiver Resender
 * @date 04/16/2015
 * @author aurelien.barre@parrot.com
 */


#include "arstream2_rtp_receiver.h"


#define ARSTREAM2_RTP_RECEIVER_RESENDER_TAG "ARSTREAM2_RtpReceiverResender"


/**
 * Sets *PTR to VAL if PTR is not null
 */
#define SET_WITH_CHECK(PTR,VAL)                 \
    do                                          \
    {                                           \
        if (PTR != NULL)                        \
        {                                       \
            *PTR = VAL;                         \
        }                                       \
    } while (0)


static void ARSTREAM2_RtpReceiverResender_NaluCallback(eARSTREAM2_RTP_SENDER_STATUS status, void *naluUserPtr, void *userPtr)
{
    ARSTREAM2_RtpReceiver_RtpResender_t *resender = (ARSTREAM2_RtpReceiver_RtpResender_t *)userPtr;
    ARSTREAM2_RtpReceiver_t *receiver;
    ARSTREAM2_RtpReceiver_NaluBuffer_t *naluBuf = (ARSTREAM2_RtpReceiver_NaluBuffer_t *)naluUserPtr;

    if ((resender == NULL) || (naluUserPtr == NULL))
    {
        return;
    }

    receiver = resender->receiver;
    if (receiver == NULL)
    {
        return;
    }

    ARSAL_Mutex_Lock(&(receiver->naluBufferMutex));
    naluBuf->useCount--;
    ARSAL_Mutex_Unlock(&(receiver->naluBufferMutex));
}


static ARSTREAM2_RtpReceiver_NaluBuffer_t* ARSTREAM2_RtpReceiverResender_GetAvailableNaluBuffer(ARSTREAM2_RtpReceiver_t *receiver, int naluSize)
{
    int i, availableCount;
    ARSTREAM2_RtpReceiver_NaluBuffer_t *retNaluBuf = NULL;

    for (i = 0, availableCount = 0; i < receiver->naluBufferCount; i++)
    {
        ARSTREAM2_RtpReceiver_NaluBuffer_t *naluBuf = &receiver->naluBuffer[i];

        if (naluBuf->useCount <= 0)
        {
            availableCount++;
            if (naluBuf->naluBufferSize >= naluSize)
            {
                retNaluBuf = naluBuf;
                break;
            }
        }
    }

    if ((retNaluBuf == NULL) && (availableCount > 0))
    {
        for (i = 0; i < receiver->naluBufferCount; i++)
        {
            ARSTREAM2_RtpReceiver_NaluBuffer_t *naluBuf = &receiver->naluBuffer[i];

            if (naluBuf->useCount <= 0)
            {
                /* round naluSize up to nearest multiple of ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_NALU_BUFFER_MALLOC_CHUNK_SIZE */
                int newSize = ((naluSize + ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_NALU_BUFFER_MALLOC_CHUNK_SIZE - 1) / ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_NALU_BUFFER_MALLOC_CHUNK_SIZE) * ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_NALU_BUFFER_MALLOC_CHUNK_SIZE;
                free(naluBuf->naluBuffer);
                naluBuf->naluBuffer = malloc(newSize);
                if (naluBuf->naluBuffer == NULL)
                {
                    naluBuf->naluBufferSize = 0;
                }
                else
                {
                    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_RESENDER_TAG, "Reallocated NALU buffer (size: %d) - naluBufferCount = %d", newSize, receiver->naluBufferCount);
                    naluBuf->naluBufferSize = newSize;
                    retNaluBuf = naluBuf;
                    break;
                }
            }
        }
    }

    if ((retNaluBuf == NULL) && (receiver->naluBufferCount < ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_MAX_NALU_BUFFER_COUNT))
    {
        ARSTREAM2_RtpReceiver_NaluBuffer_t *naluBuf = &receiver->naluBuffer[receiver->naluBufferCount];
        receiver->naluBufferCount++;
        naluBuf->useCount = 0;

        /* round naluSize up to nearest multiple of ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_NALU_BUFFER_MALLOC_CHUNK_SIZE */
        int newSize = ((naluSize + ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_NALU_BUFFER_MALLOC_CHUNK_SIZE - 1) / ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_NALU_BUFFER_MALLOC_CHUNK_SIZE) * ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_NALU_BUFFER_MALLOC_CHUNK_SIZE;
        naluBuf->naluBuffer = malloc(newSize);
        if (naluBuf->naluBuffer == NULL)
        {
            naluBuf->naluBufferSize = 0;
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_RESENDER_TAG, "Allocated new NALU buffer (size: %d) - naluBufferCount = %d", newSize, receiver->naluBufferCount);
            naluBuf->naluBufferSize = newSize;
            retNaluBuf = naluBuf;
        }
    }

    return retNaluBuf;
}


static int ARSTREAM2_RtpReceiver_ResendNalu(ARSTREAM2_RtpReceiver_t *receiver, uint8_t *naluBuffer, uint32_t naluSize, uint64_t auTimestamp,
                                            uint8_t *naluMetadata, int naluMetadataSize, int isLastNaluInAu, int missingPacketsBefore)
{
    int ret = 0, i;
    ARSTREAM2_RtpReceiver_NaluBuffer_t *naluBuf = NULL;
    ARSTREAM2_RtpSender_H264NaluDesc_t nalu;
    memset(&nalu, 0, sizeof(ARSTREAM2_RtpSender_H264NaluDesc_t));

    /* remove the byte stream format start code */
    if (receiver->insertStartCodes)
    {
        naluBuffer += ARSTREAM2_H264_BYTE_STREAM_NALU_START_CODE_LENGTH;
        naluSize -= ARSTREAM2_H264_BYTE_STREAM_NALU_START_CODE_LENGTH;
    }

    ARSAL_Mutex_Lock(&(receiver->naluBufferMutex));

    /* get a buffer from the pool */
    naluBuf = ARSTREAM2_RtpReceiverResender_GetAvailableNaluBuffer(receiver, (int)naluSize);
    if (naluBuf == NULL)
    {
        ARSAL_Mutex_Unlock(&(receiver->naluBufferMutex));
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTP_RECEIVER_RESENDER_TAG, "Failed to get available NALU buffer (naluSize=%d, naluBufferCount=%d)", naluSize, receiver->naluBufferCount);
        return -1;
    }
    naluBuf->useCount++;
    ARSAL_Mutex_Unlock(&(receiver->naluBufferMutex));

    memcpy(naluBuf->naluBuffer, naluBuffer, naluSize);
    naluBuf->naluSize = naluSize;
    naluBuf->auTimestamp = auTimestamp;
    naluBuf->isLastNaluInAu = isLastNaluInAu;

    nalu.naluBuffer = naluBuf->naluBuffer;
    nalu.naluSize = naluSize;
    nalu.auTimestamp = auTimestamp;
    nalu.isLastNaluInAu = isLastNaluInAu;
    nalu.seqNumForcedDiscontinuity = missingPacketsBefore;
    nalu.auUserPtr = NULL;
    nalu.naluUserPtr = naluBuf;

    /* send the NALU to all resenders */
    ARSAL_Mutex_Lock(&(receiver->resenderMutex));
    for (i = 0; i < receiver->resenderCount; i++)
    {
        ARSTREAM2_RtpReceiver_RtpResender_t *resender = receiver->resender[i];
        eARSTREAM2_ERROR err;

        if ((resender) && (resender->senderRunning))
        {
            if (resender->useRtpHeaderExtensions)
            {
                nalu.auMetadata = naluMetadata;
                nalu.auMetadataSize = naluMetadataSize;
            }
            else
            {
                nalu.auMetadata = NULL;
                nalu.auMetadataSize = 0;
            }
            err = ARSTREAM2_RtpSender_SendNewNalu(resender->sender, &nalu /*, 0*/); //TODO: input timestamp
            if (err == ARSTREAM2_OK)
            {
                ARSAL_Mutex_Lock(&(receiver->naluBufferMutex));
                naluBuf->useCount++;
                ARSAL_Mutex_Unlock(&(receiver->naluBufferMutex));
            }
            else
            {
                ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTP_RECEIVER_RESENDER_TAG, "Failed to resend NALU on resender #%d (error %d)", i, err);
            }
        }
    }
    ARSAL_Mutex_Unlock(&(receiver->resenderMutex));

    ARSAL_Mutex_Lock(&(receiver->naluBufferMutex));
    naluBuf->useCount--;
    ARSAL_Mutex_Unlock(&(receiver->naluBufferMutex));

    return ret;
}


ARSTREAM2_RtpReceiver_RtpResender_t* ARSTREAM2_RtpReceiverResender_New(ARSTREAM2_RtpReceiver_t *receiver, ARSTREAM2_RtpReceiver_RtpResender_Config_t *config, eARSTREAM2_ERROR *error)
{
    ARSTREAM2_RtpReceiver_RtpResender_t *retResender = NULL;
    eARSTREAM2_ERROR internalError = ARSTREAM2_OK;

    /* ARGS Check */
    if ((receiver == NULL) ||
        (config == NULL) ||
        (receiver->resenderCount >= ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_MAX_COUNT))
    {
        SET_WITH_CHECK(error, ARSTREAM2_ERROR_BAD_PARAMETERS);
        return retResender;
    }

    /* Alloc new resender */
    retResender = malloc(sizeof(ARSTREAM2_RtpReceiver_RtpResender_t));
    if (retResender == NULL)
    {
        internalError = ARSTREAM2_ERROR_ALLOC;
    }

    /* Initialize the resender and copy parameters */
    if (internalError == ARSTREAM2_OK)
    {
        memset(retResender, 0, sizeof(ARSTREAM2_RtpReceiver_RtpResender_t));
    }

    /* Setup ARSTREAM2_RtpSender */
    if (internalError == ARSTREAM2_OK)
    {
        eARSTREAM2_ERROR error2;
        ARSTREAM2_RtpSender_Config_t senderConfig;
        memset(&senderConfig, 0, sizeof(senderConfig));
        senderConfig.clientAddr = config->clientAddr;
        senderConfig.mcastAddr = config->mcastAddr;
        senderConfig.mcastIfaceAddr = config->mcastIfaceAddr;
        senderConfig.serverStreamPort = config->serverStreamPort;
        senderConfig.serverControlPort = config->serverControlPort;
        senderConfig.clientStreamPort = config->clientStreamPort;
        senderConfig.clientControlPort = config->clientControlPort;
        senderConfig.naluCallback = ARSTREAM2_RtpReceiverResender_NaluCallback;
        senderConfig.naluCallbackUserPtr = (void*)retResender;
        senderConfig.naluFifoSize = ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_MAX_NALU_BUFFER_COUNT;
        senderConfig.maxPacketSize = config->maxPacketSize;
        senderConfig.targetPacketSize = config->targetPacketSize;
        senderConfig.streamSocketBufferSize = config->streamSocketBufferSize;
        senderConfig.maxBitrate = receiver->maxBitrate;
        senderConfig.maxLatencyMs = config->maxLatencyMs;
        senderConfig.maxNetworkLatencyMs = config->maxNetworkLatencyMs;
        senderConfig.useRtpHeaderExtensions = config->useRtpHeaderExtensions;
        retResender->useRtpHeaderExtensions = config->useRtpHeaderExtensions;
        retResender->sender = ARSTREAM2_RtpSender_New(&senderConfig, &error2);
        if (error2 != ARSTREAM2_OK)
        {
            internalError = error2;
        }
    }

    if (internalError == ARSTREAM2_OK)
    {
        ARSAL_Mutex_Lock(&(receiver->resenderMutex));
        if (receiver->resenderCount < ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_MAX_COUNT)
        {
            retResender->receiver = receiver;
            retResender->senderRunning = 1;
            receiver->resender[receiver->resenderCount] = retResender;
            receiver->resenderCount++;
        }
        else
        {
            internalError = ARSTREAM2_ERROR_BAD_PARAMETERS;
        }
        ARSAL_Mutex_Unlock(&(receiver->resenderMutex));
    }

    if ((internalError != ARSTREAM2_OK) &&
        (retResender != NULL))
    {
        if (retResender->sender) ARSTREAM2_RtpSender_Delete(&(retResender->sender));
        free(retResender);
        retResender = NULL;
    }

    SET_WITH_CHECK(error, internalError);
    return retResender;
}


void ARSTREAM2_RtpReceiverResender_Stop(ARSTREAM2_RtpReceiver_RtpResender_t *resender)
{
    if (resender != NULL)
    {
        ARSAL_Mutex_Lock(&(resender->receiver->resenderMutex));
        if (resender->sender != NULL)
        {
            ARSTREAM2_RtpSender_Stop(resender->sender);
            resender->senderRunning = 0;
        }
        ARSAL_Mutex_Unlock(&(resender->receiver->resenderMutex));
    }
}


eARSTREAM2_ERROR ARSTREAM2_RtpReceiverResender_Delete(ARSTREAM2_RtpReceiver_RtpResender_t **resender)
{
    ARSTREAM2_RtpReceiver_t *receiver;
    int i, idx;
    eARSTREAM2_ERROR retVal = ARSTREAM2_ERROR_BAD_PARAMETERS;

    if ((resender != NULL) &&
        (*resender != NULL))
    {
        receiver = (*resender)->receiver;
        if (receiver != NULL)
        {
            ARSAL_Mutex_Lock(&(receiver->resenderMutex));

            /* find the resender in the array */
            for (i = 0, idx = -1; i < receiver->resenderCount; i++)
            {
                if (*resender == receiver->resender[i])
                {
                    idx = i;
                    break;
                }
            }
            if (idx < 0)
            {
                ARSAL_Mutex_Unlock(&(receiver->resenderMutex));
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_RESENDER_TAG, "Failed to find the resender");
                return retVal;
            }

            if (!(*resender)->senderRunning)
            {
                retVal = ARSTREAM2_RtpSender_Delete(&((*resender)->sender));
                if (retVal == ARSTREAM2_OK)
                {
                    /* remove the resender: shift the values in the resender array */
                    receiver->resender[idx] = NULL;
                    for (i = idx; i < receiver->resenderCount - 1; i++)
                    {
                        receiver->resender[i] = receiver->resender[i + 1];
                    }
                    receiver->resenderCount--;

                    free(*resender);
                    *resender = NULL;
                }
                else
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_RESENDER_TAG, "RtpResender: failed to delete Sender (%d)", retVal);
                }
            }
            else
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_RESENDER_TAG, "RtpResender: sender is still running");
            }

            ARSAL_Mutex_Unlock(&(receiver->resenderMutex));
        }
    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_RESENDER_TAG, "Invalid resender");
    }

    return retVal;
}


void* ARSTREAM2_RtpReceiverResender_RunThread(void *ARSTREAM2_RtpReceiver_RtpResender_t_Param)
{
    ARSTREAM2_RtpReceiver_RtpResender_t *resender = (ARSTREAM2_RtpReceiver_RtpResender_t *)ARSTREAM2_RtpReceiver_RtpResender_t_Param;

    if (resender == NULL)
    {
        return (void *)0;
    }

    return ARSTREAM2_RtpSender_RunStreamThread((void *)resender->sender);
}
