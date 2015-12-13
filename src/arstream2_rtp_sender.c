/**
 * @file arstream2_rtp_sender.c
 * @brief Parrot Streaming Library - RTP Sender
 * @date 04/17/2015
 * @author aurelien.barre@parrot.com
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <poll.h>
#include <math.h>

#include <libARStream2/arstream2_rtp_sender.h>
#include "arstream2_rtp.h"

#include <libARSAL/ARSAL_Print.h>
#include <libARSAL/ARSAL_Mutex.h>
#include <libARSAL/ARSAL_Socket.h>


/**
 * Tag for ARSAL_PRINT
 */
#define ARSTREAM2_RTP_SENDER_TAG "ARSTREAM2_RtpSender"


/**
 * Minimum socket poll timeout value (milliseconds)
 */
#define ARSTREAM2_RTP_SENDER_MIN_POLL_TIMEOUT_MS (10)


/**
 * Socket timeout value for clock sync packets (milliseconds)
 */
#define ARSTREAM2_RTP_SENDER_CLOCKSYNC_DATAREAD_TIMEOUT_MS (500)


/**
 * FIFO cond timeout value (milliseconds)
 */
#define ARSTREAM2_RTP_SENDER_FIFO_COND_TIMEOUT_MS (1000)


/**
 * Maximum number of elements for the monitoring
 */
#define ARSTREAM2_RTP_SENDER_MONITORING_MAX_POINTS (2048)


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


#define ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT
#ifdef ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT
    #include <stdio.h>

    #define ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_ALLOW_DRONE
    #define ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_PATH_DRONE "/data/ftp/internal_000/streamdebug"
    #define ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_ALLOW_NAP_USB
    #define ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_PATH_NAP_USB "/tmp/mnt/STREAMDEBUG/streamdebug"
    //#define ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_ALLOW_NAP_INTERNAL
    #define ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_PATH_NAP_INTERNAL "/data/skycontroller/streamdebug"
    #define ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_ALLOW_ANDROID_INTERNAL
    #define ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_PATH_ANDROID_INTERNAL "/sdcard/FF/streamdebug"
    #define ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_ALLOW_PCLINUX
    #define ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_PATH_PCLINUX "./streamdebug"

    #define ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_FILENAME "sender_monitor"
#endif


typedef struct ARSTREAM2_RtpSender_Nalu_s {
    uint8_t *naluBuffer;
    uint32_t naluSize;
    uint64_t auTimestamp;
    uint64_t naluInputTimestamp;
    int isLastInAu;
    int seqNumForcedDiscontinuity;
    void *auUserPtr;
    void *naluUserPtr;
    int drop;

    struct ARSTREAM2_RtpSender_Nalu_s* prev;
    struct ARSTREAM2_RtpSender_Nalu_s* next;
    int used;
} ARSTREAM2_RtpSender_Nalu_t;


typedef struct ARSTREAM2_RtpSender_MonitoringPoint_s {
    uint64_t auTimestamp;
    uint64_t inputTimestamp;
    uint64_t sendTimestamp;
    uint32_t bytesSent;
    uint32_t bytesDropped;
} ARSTREAM2_RtpSender_MonitoringPoint_t;


struct ARSTREAM2_RtpSender_t {
    /* Configuration on New */
    char *clientAddr;
    char *mcastAddr;
    char *mcastIfaceAddr;
    int serverStreamPort;
    int serverControlPort;
    int clientStreamPort;
    int clientControlPort;
    ARSTREAM2_RtpSender_AuCallback_t auCallback;
    void *auCallbackUserPtr;
    ARSTREAM2_RtpSender_NaluCallback_t naluCallback;
    void *naluCallbackUserPtr;
    int naluFifoSize;
    int maxPacketSize;
    int targetPacketSize;
    int maxBitrate;
    int maxLatencyMs;
    int maxNetworkLatencyMs;

    uint64_t lastAuTimestamp;
    uint16_t seqNum;
    ARSAL_Mutex_t streamMutex;

    /* Thread status */
    int threadsShouldStop;
    int streamThreadStarted;
    int controlThreadStarted;

    /* Sockets */
    int isMulticast;
    int streamSocketSendBufferSize;
    struct sockaddr_in streamSendSin;
    int streamSocket;
    int controlSocket;
    
    /* NAL unit FIFO */
    ARSAL_Mutex_t fifoMutex;
    ARSAL_Cond_t fifoCond;
    int fifoCount;
    int naluFifoBufferSize;
    ARSTREAM2_RtpSender_Nalu_t *fifoHead;
    ARSTREAM2_RtpSender_Nalu_t *fifoTail;
    ARSTREAM2_RtpSender_Nalu_t *fifoPool;

    /* Monitoring */
    ARSAL_Mutex_t monitoringMutex;
    int monitoringCount;
    int monitoringIndex;
    ARSTREAM2_RtpSender_MonitoringPoint_t monitoringPoint[ARSTREAM2_RTP_SENDER_MONITORING_MAX_POINTS];
#ifdef ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT
    FILE* fMonitorOut;
#endif
};


static int ARSTREAM2_RtpSender_EnqueueNalu(ARSTREAM2_RtpSender_t *sender, const ARSTREAM2_RtpSender_H264NaluDesc_t *nalu, uint64_t inputTimestamp)
{
    int i;
    ARSTREAM2_RtpSender_Nalu_t* cur = NULL;

    if ((!sender) || (!nalu))
    {
        return -1;
    }

    ARSAL_Mutex_Lock(&(sender->fifoMutex));

    if (sender->fifoCount >= sender->naluFifoSize)
    {
        ARSAL_Mutex_Unlock(&(sender->fifoMutex));
        ARSAL_Cond_Signal(&(sender->fifoCond));
        return -2;
    }

    for (i = 0; i < sender->naluFifoSize; i++)
    {
        if (!sender->fifoPool[i].used)
        {
            cur = &sender->fifoPool[i];
            cur->naluBuffer = nalu->naluBuffer;
            cur->naluSize = nalu->naluSize;
            cur->auTimestamp = nalu->auTimestamp;
            cur->naluInputTimestamp = inputTimestamp;
            cur->isLastInAu = nalu->isLastNaluInAu;
            cur->seqNumForcedDiscontinuity = nalu->seqNumForcedDiscontinuity;
            cur->auUserPtr = nalu->auUserPtr;
            cur->naluUserPtr = nalu->naluUserPtr;
            cur->drop = 0;
            cur->prev = NULL;
            cur->next = NULL;
            cur->used = 1;
            break;
        }
    }

    if (!cur)
    {
        ARSAL_Mutex_Unlock(&(sender->fifoMutex));
        ARSAL_Cond_Signal(&(sender->fifoCond));
        return -3;
    }

    sender->fifoCount++;
    if (sender->fifoTail)
    {
        sender->fifoTail->next = cur;
        cur->prev = sender->fifoTail;
    }
    sender->fifoTail = cur; 
    if (!sender->fifoHead)
    {
        sender->fifoHead = cur;
    }

    ARSAL_Mutex_Unlock(&(sender->fifoMutex));
    ARSAL_Cond_Signal(&(sender->fifoCond));

    return 0;
}


static int ARSTREAM2_RtpSender_EnqueueNNalu(ARSTREAM2_RtpSender_t *sender, const ARSTREAM2_RtpSender_H264NaluDesc_t *nalu, int naluCount, uint64_t inputTimestamp)
{
    int i, k;
    ARSTREAM2_RtpSender_Nalu_t* cur = NULL;

    if ((!sender) || (!nalu) || (naluCount <= 0))
    {
        return -1;
    }

    ARSAL_Mutex_Lock(&(sender->fifoMutex));

    if (sender->fifoCount + naluCount >= sender->naluFifoSize)
    {
        ARSAL_Mutex_Unlock(&(sender->fifoMutex));
        ARSAL_Cond_Signal(&(sender->fifoCond));
        return -2;
    }

    for (k = 0; k < naluCount; k++)
    {
        for (i = 0; i < sender->naluFifoSize; i++)
        {
            if (!sender->fifoPool[i].used)
            {
                cur = &sender->fifoPool[i];
                cur->naluBuffer = nalu[k].naluBuffer;
                cur->naluSize = nalu[k].naluSize;
                cur->auTimestamp = nalu[k].auTimestamp;
                cur->naluInputTimestamp = inputTimestamp;
                cur->isLastInAu = nalu[k].isLastNaluInAu;
                cur->seqNumForcedDiscontinuity = nalu->seqNumForcedDiscontinuity;
                cur->auUserPtr = nalu[k].auUserPtr;
                cur->naluUserPtr = nalu[k].naluUserPtr;
                cur->drop = 0;
                cur->prev = NULL;
                cur->next = NULL;
                cur->used = 1;
                break;
            }
        }

        if (!cur)
        {
            ARSAL_Mutex_Unlock(&(sender->fifoMutex));
            ARSAL_Cond_Signal(&(sender->fifoCond));
            return -3;
        }

        sender->fifoCount++;
        if (sender->fifoTail)
        {
            sender->fifoTail->next = cur;
            cur->prev = sender->fifoTail;
        }
        sender->fifoTail = cur; 
        if (!sender->fifoHead)
        {
            sender->fifoHead = cur;
        }
    }

    ARSAL_Mutex_Unlock(&(sender->fifoMutex));
    ARSAL_Cond_Signal(&(sender->fifoCond));

    return 0;
}


static int ARSTREAM2_RtpSender_DequeueNalu(ARSTREAM2_RtpSender_t *sender, ARSTREAM2_RtpSender_Nalu_t* nalu)
{
    ARSTREAM2_RtpSender_Nalu_t* cur = NULL;

    if ((!sender) || (!nalu))
    {
        return -1;
    }

    ARSAL_Mutex_Lock(&(sender->fifoMutex));

    if ((!sender->fifoHead) || (!sender->fifoCount))
    {
        ARSAL_Mutex_Unlock(&sender->fifoMutex);
        return -2;
    }

    cur = sender->fifoHead;
    if (cur->next)
    {
        cur->next->prev = NULL;
        sender->fifoHead = cur->next;
        sender->fifoCount--;
    }
    else
    {
        sender->fifoHead = NULL;
        sender->fifoCount = 0;
        sender->fifoTail = NULL;
    }

    cur->used = 0;
    cur->prev = NULL;
    cur->next = NULL;
    memcpy(nalu, cur, sizeof(ARSTREAM2_RtpSender_Nalu_t));

    ARSAL_Mutex_Unlock (&(sender->fifoMutex));

    return 0;
}


static int ARSTREAM2_RtpSender_DropFromFifo(ARSTREAM2_RtpSender_t *sender, int maxTargetSize)
{
    ARSTREAM2_RtpSender_Nalu_t* cur = NULL;
    int size = 0, dropSize[4] = {0, 0, 0, 0};
    int nri, curNri, curNaluType;
    //Note: this feature is not used currently as it requires that different levels of NALU priorities are present in the stream

    if (!sender)
    {
        return -1;
    }

    ARSAL_Mutex_Lock(&(sender->fifoMutex));

    if ((!sender->fifoHead) || (!sender->fifoTail) || (!sender->fifoCount))
    {
        ARSAL_Mutex_Unlock(&sender->fifoMutex);
        return 0;
    }

    /* get the current FIFO size in bytes */
    for (cur = sender->fifoHead; cur; cur = cur->next)
    {
        if (!cur->drop)
        {
            size += cur->naluSize;
        }
    }

    ARSAL_Mutex_Unlock(&sender->fifoMutex);

    if (size <= maxTargetSize)
    {
        return 0;
    }

    for (nri = 0; nri < 4; nri++)
    {
        /* stop if the max target size has been reached */
        if (size <= maxTargetSize)
        {
            break;
        }

        ARSAL_Mutex_Lock(&(sender->fifoMutex));

        for (cur = sender->fifoTail; cur; cur = cur->prev)
        {
            /* stop if the max target size has been reached */
            if (size <= maxTargetSize)
            {
                break;
            }

            /* check the NRI bits */
            curNri = (((uint8_t)(*(cur->naluBuffer)) >> 5) & 0x3);
            curNaluType = ((uint8_t)(*(cur->naluBuffer)) & 0x1F);
            if ((curNri == nri) && (curNaluType != 6)) // do not filter out SEI
            {
                cur->drop = 1;
                size -= cur->naluSize;
                dropSize[nri] += cur->naluSize;
            }
        }

        ARSAL_Mutex_Unlock(&(sender->fifoMutex));
    }

    if (dropSize[0])
    {
        ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_SENDER_TAG, "Drop from FIFO - dropSize[0] = %d bytes", dropSize[0]);
    }
    if (dropSize[1])
    {
        ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_SENDER_TAG, "Drop from FIFO - dropSize[1] = %d bytes", dropSize[1]);
    }
    if (dropSize[2])
    {
        ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_SENDER_TAG, "Drop from FIFO - dropSize[2] = %d bytes", dropSize[2]);
    }
    if (dropSize[3])
    {
        ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_SENDER_TAG, "Drop from FIFO - dropSize[3] = %d bytes", dropSize[3]);
    }

    return dropSize[0] + dropSize[1] + dropSize[2] + dropSize[3];
}


static int ARSTREAM2_RtpSender_FlushNaluFifo(ARSTREAM2_RtpSender_t *sender)
{
    if (!sender)
    {
        return -1;
    }

    int fifoRes;
    ARSTREAM2_RtpSender_Nalu_t nalu;

    while ((fifoRes = ARSTREAM2_RtpSender_DequeueNalu(sender, &nalu)) == 0)
    {
        /* call the naluCallback */
        if (sender->naluCallback != NULL)
        {
            sender->naluCallback(ARSTREAM2_RTP_SENDER_STATUS_CANCELLED, nalu.naluUserPtr, sender->naluCallbackUserPtr);
        }

        /* last NALU in the Access Unit: call the auCallback */
        if ((sender->auCallback != NULL) && (nalu.isLastInAu))
        {
            ARSAL_Mutex_Lock(&(sender->streamMutex));
            if (nalu.auTimestamp != sender->lastAuTimestamp)
            {
                sender->lastAuTimestamp = nalu.auTimestamp;
                ARSAL_Mutex_Unlock(&(sender->streamMutex));

                /* call the auCallback */
                sender->auCallback(ARSTREAM2_RTP_SENDER_STATUS_CANCELLED, nalu.auUserPtr, sender->auCallbackUserPtr);
            }
            else
            {
                ARSAL_Mutex_Unlock(&(sender->streamMutex));
            }
        }
    }

    return 0;
}


ARSTREAM2_RtpSender_t* ARSTREAM2_RtpSender_New(ARSTREAM2_RtpSender_Config_t *config, eARSTREAM2_ERROR *error)
{
    ARSTREAM2_RtpSender_t *retSender = NULL;
    int streamMutexWasInit = 0;
    int monitoringMutexWasInit = 0;
    int fifoWasCreated = 0;
    int fifoMutexWasInit = 0;
    int fifoCondWasInit = 0;
    eARSTREAM2_ERROR internalError = ARSTREAM2_OK;

    /* ARGS Check */
    if (config == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "No config provided");
        SET_WITH_CHECK(error, ARSTREAM2_ERROR_BAD_PARAMETERS);
        return retSender;
    }
    if ((config->clientAddr == NULL) || (!strlen(config->clientAddr)))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Config: no client address provided");
        SET_WITH_CHECK(error, ARSTREAM2_ERROR_BAD_PARAMETERS);
        return retSender;
    }
    if ((config->clientStreamPort <= 0) || (config->clientControlPort <= 0))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Config: no client ports provided");
        SET_WITH_CHECK(error, ARSTREAM2_ERROR_BAD_PARAMETERS);
        return retSender;
    }

    /* Alloc new sender */
    retSender = malloc(sizeof(ARSTREAM2_RtpSender_t));
    if (retSender == NULL)
    {
        internalError = ARSTREAM2_ERROR_ALLOC;
    }

    /* Initialize the sender and copy parameters */
    if (internalError == ARSTREAM2_OK)
    {
        memset(retSender, 0, sizeof(ARSTREAM2_RtpSender_t));
        retSender->isMulticast = 0;
        retSender->streamSocket = -1;
        retSender->controlSocket = -1;
        if (config->clientAddr)
        {
            retSender->clientAddr = strndup(config->clientAddr, 16);
        }
        if (config->mcastAddr)
        {
            retSender->mcastAddr = strndup(config->mcastAddr, 16);
        }
        if (config->mcastIfaceAddr)
        {
            retSender->mcastIfaceAddr = strndup(config->mcastIfaceAddr, 16);
        }
        retSender->serverStreamPort = (config->serverStreamPort > 0) ? config->serverStreamPort : ARSTREAM2_RTP_SENDER_DEFAULT_SERVER_STREAM_PORT;
        retSender->serverControlPort = (config->serverControlPort > 0) ? config->serverControlPort : ARSTREAM2_RTP_SENDER_DEFAULT_SERVER_CONTROL_PORT;
        retSender->clientStreamPort = config->clientStreamPort;
        retSender->clientControlPort = config->clientControlPort;
        retSender->auCallback = config->auCallback;
        retSender->auCallbackUserPtr = config->auCallbackUserPtr;
        retSender->naluCallback = config->naluCallback;
        retSender->naluCallbackUserPtr = config->naluCallbackUserPtr;
        retSender->naluFifoSize = (config->naluFifoSize > 0) ? config->naluFifoSize : ARSTREAM2_RTP_SENDER_DEFAULT_NALU_FIFO_SIZE;
        retSender->maxPacketSize = (config->maxPacketSize > 0) ? config->maxPacketSize - ARSTREAM2_RTP_TOTAL_HEADERS_SIZE : ARSTREAM2_RTP_MAX_PAYLOAD_SIZE;
        retSender->targetPacketSize = (config->targetPacketSize > 0) ? config->targetPacketSize - ARSTREAM2_RTP_TOTAL_HEADERS_SIZE : retSender->maxPacketSize;
        retSender->maxBitrate = (config->maxBitrate > 0) ? config->maxBitrate : 0;
        retSender->maxLatencyMs = (config->maxLatencyMs > 0) ? config->maxLatencyMs : 0;
        retSender->maxNetworkLatencyMs = (config->maxNetworkLatencyMs > 0) ? config->maxNetworkLatencyMs : 0;
        int totalBufSize = config->maxBitrate * config->maxNetworkLatencyMs / 1000 / 8;
        int minStreamSocketSendBufferSize = config->maxBitrate * 50 / 1000 / 8;
        retSender->streamSocketSendBufferSize = (totalBufSize / 4 > minStreamSocketSendBufferSize) ? totalBufSize / 4 : minStreamSocketSendBufferSize;
        retSender->naluFifoBufferSize = totalBufSize - retSender->streamSocketSendBufferSize;
    }

    /* Setup internal mutexes/sems */
    if (internalError == ARSTREAM2_OK)
    {
        int mutexInitRet = ARSAL_Mutex_Init(&(retSender->streamMutex));
        if (mutexInitRet != 0)
        {
            internalError = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            streamMutexWasInit = 1;
        }
    }
    if (internalError == ARSTREAM2_OK)
    {
        int mutexInitRet = ARSAL_Mutex_Init(&(retSender->monitoringMutex));
        if (mutexInitRet != 0)
        {
            internalError = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            monitoringMutexWasInit = 1;
        }
    }

    /* Setup the NAL unit FIFO */
    if (internalError == ARSTREAM2_OK)
    {
        retSender->fifoPool = malloc(config->naluFifoSize * sizeof(ARSTREAM2_RtpSender_Nalu_t));
        if (retSender->fifoPool == NULL)
        {
            internalError = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            fifoWasCreated = 1;
            memset(retSender->fifoPool, 0, config->naluFifoSize * sizeof(ARSTREAM2_RtpSender_Nalu_t));
        }
    }
    if (internalError == ARSTREAM2_OK)
    {
        int mutexInitRet = ARSAL_Mutex_Init(&(retSender->fifoMutex));
        if (mutexInitRet != 0)
        {
            internalError = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            fifoMutexWasInit = 1;
        }
    }
    if (internalError == ARSTREAM2_OK)
    {
        int condInitRet = ARSAL_Cond_Init(&(retSender->fifoCond));
        if (condInitRet != 0)
        {
            internalError = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            fifoCondWasInit = 1;
        }
    }

#ifdef ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT
    if (internalError == ARSTREAM2_OK)
    {
        int i;
        char szOutputFileName[128];
        char *pszFilePath = NULL;
        szOutputFileName[0] = '\0';
        if (0)
        {
        }
#ifdef ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_ALLOW_DRONE
        else if ((access(ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_PATH_DRONE, F_OK) == 0) && (access(ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_PATH_DRONE, W_OK) == 0))
        {
            pszFilePath = ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_PATH_DRONE;
        }
#endif
#ifdef ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_ALLOW_NAP_USB
        else if ((access(ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_PATH_NAP_USB, F_OK) == 0) && (access(ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_PATH_NAP_USB, W_OK) == 0))
        {
            pszFilePath = ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_PATH_NAP_USB;
        }
#endif
#ifdef ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_ALLOW_NAP_INTERNAL
        else if ((access(ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_PATH_NAP_INTERNAL, F_OK) == 0) && (access(ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_PATH_NAP_INTERNAL, W_OK) == 0))
        {
            pszFilePath = ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_PATH_NAP_INTERNAL;
        }
#endif
#ifdef ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_ALLOW_ANDROID_INTERNAL
        else if ((access(ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_PATH_ANDROID_INTERNAL, F_OK) == 0) && (access(ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_PATH_ANDROID_INTERNAL, W_OK) == 0))
        {
            pszFilePath = ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_PATH_ANDROID_INTERNAL;
        }
#endif
#ifdef ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_ALLOW_PCLINUX
        else if ((access(ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_PATH_PCLINUX, F_OK) == 0) && (access(ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_PATH_PCLINUX, W_OK) == 0))
        {
            pszFilePath = ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_PATH_PCLINUX;
        }
#endif
        if (pszFilePath)
        {
            for (i = 0; i < 1000; i++)
            {
                snprintf(szOutputFileName, 128, "%s/%s_%03d.dat", pszFilePath, ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT_FILENAME, i);
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
            retSender->fMonitorOut = fopen(szOutputFileName, "w");
            if (!retSender->fMonitorOut)
            {
                ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTP_SENDER_TAG, "Unable to open monitor output file '%s'", szOutputFileName);
            }
        }

        if (retSender->fMonitorOut)
        {
            fprintf(retSender->fMonitorOut, "sendTimestamp inputTimestamp auTimestamp rtpTimestamp rtpSeqNum rtpMarkerBit bytesSent bytesDropped\n");
        }
    }
#endif //#ifdef ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT

    if ((internalError != ARSTREAM2_OK) &&
        (retSender != NULL))
    {
        if (streamMutexWasInit == 1)
        {
            ARSAL_Mutex_Destroy(&(retSender->streamMutex));
        }
        if (monitoringMutexWasInit == 1)
        {
            ARSAL_Mutex_Destroy(&(retSender->monitoringMutex));
        }
        if (fifoWasCreated == 1)
        {
            free(retSender->fifoPool);
        }
        if (fifoMutexWasInit == 1)
        {
            ARSAL_Mutex_Destroy(&(retSender->fifoMutex));
        }
        if (fifoCondWasInit == 1)
        {
            ARSAL_Cond_Destroy(&(retSender->fifoCond));
        }
        if ((retSender) && (retSender->clientAddr))
        {
            free(retSender->clientAddr);
        }
        if ((retSender) && (retSender->mcastAddr))
        {
            free(retSender->mcastAddr);
        }
        if ((retSender) && (retSender->mcastIfaceAddr))
        {
            free(retSender->mcastIfaceAddr);
        }
#ifdef ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT
        if ((retSender) && (retSender->fMonitorOut))
        {
            fclose(retSender->fMonitorOut);
        }
#endif
        free(retSender);
        retSender = NULL;
    }

    SET_WITH_CHECK(error, internalError);
    return retSender;
}


void ARSTREAM2_RtpSender_Stop(ARSTREAM2_RtpSender_t *sender)
{
    if (sender != NULL)
    {
        ARSAL_Mutex_Lock(&(sender->streamMutex));
        sender->threadsShouldStop = 1;
        ARSAL_Mutex_Unlock(&(sender->streamMutex));
        /* signal the sending thread to avoid a deadlock */
        ARSAL_Cond_Signal(&(sender->fifoCond));
    }
}


eARSTREAM2_ERROR ARSTREAM2_RtpSender_Delete(ARSTREAM2_RtpSender_t **sender)
{
    eARSTREAM2_ERROR retVal = ARSTREAM2_ERROR_BAD_PARAMETERS;
    if ((sender != NULL) &&
        (*sender != NULL))
    {
        int canDelete = 0;
        ARSAL_Mutex_Lock(&((*sender)->streamMutex));
        if (((*sender)->streamThreadStarted == 0) &&
            ((*sender)->controlThreadStarted == 0))
        {
            canDelete = 1;
        }
        ARSAL_Mutex_Unlock(&((*sender)->streamMutex));

        if (canDelete == 1)
        {
            ARSAL_Mutex_Destroy(&((*sender)->streamMutex));
            ARSAL_Mutex_Destroy(&((*sender)->monitoringMutex));
            free((*sender)->fifoPool);
            ARSAL_Mutex_Destroy(&((*sender)->fifoMutex));
            ARSAL_Cond_Destroy(&((*sender)->fifoCond));
            if ((*sender)->streamSocket != -1)
            {
                ARSAL_Socket_Close((*sender)->streamSocket);
                (*sender)->streamSocket = -1;
            }
            if ((*sender)->controlSocket != -1)
            {
                ARSAL_Socket_Close((*sender)->controlSocket);
                (*sender)->controlSocket = -1;
            }
            if ((*sender)->clientAddr)
            {
                free((*sender)->clientAddr);
            }
            if ((*sender)->mcastAddr)
            {
                free((*sender)->mcastAddr);
            }
            if ((*sender)->mcastIfaceAddr)
            {
                free((*sender)->mcastIfaceAddr);
            }
#ifdef ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT
            if ((*sender)->fMonitorOut)
            {
                fclose((*sender)->fMonitorOut);
            }
#endif
            free(*sender);
            *sender = NULL;
            retVal = ARSTREAM2_OK;
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Call ARSTREAM2_RtpSender_Stop before calling this function");
            retVal = ARSTREAM2_ERROR_BUSY;
        }
    }
    return retVal;
}


eARSTREAM2_ERROR ARSTREAM2_RtpSender_SendNewNalu(ARSTREAM2_RtpSender_t *sender, const ARSTREAM2_RtpSender_H264NaluDesc_t *nalu)
{
    eARSTREAM2_ERROR retVal = ARSTREAM2_OK;
    int res;
    struct timespec t1;
    uint64_t inputTimestamp;
    ARSAL_Time_GetTime(&t1);

    // Args check
    if ((sender == NULL) ||
        (nalu == NULL))
    {
        retVal = ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if ((nalu->naluBuffer == NULL) ||
        (nalu->naluSize == 0) ||
        (nalu->auTimestamp == 0))
    {
        retVal = ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    ARSAL_Mutex_Lock(&(sender->streamMutex));
    if (!sender->streamThreadStarted)
    {
        retVal = ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    ARSAL_Mutex_Unlock(&(sender->streamMutex));

    if (retVal == ARSTREAM2_OK)
    {
        inputTimestamp = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;
        res = ARSTREAM2_RtpSender_EnqueueNalu(sender, nalu, inputTimestamp);
        if (res < 0)
        {
            retVal = ARSTREAM2_ERROR_QUEUE_FULL;
        }
    }
    return retVal;
}


eARSTREAM2_ERROR ARSTREAM2_RtpSender_SendNNewNalu(ARSTREAM2_RtpSender_t *sender, const ARSTREAM2_RtpSender_H264NaluDesc_t *nalu, int naluCount)
{
    eARSTREAM2_ERROR retVal = ARSTREAM2_OK;
    int k, res;
    struct timespec t1;
    uint64_t inputTimestamp;
    ARSAL_Time_GetTime(&t1);

    // Args check
    if ((sender == NULL) ||
        (nalu == NULL) ||
        (naluCount <= 0))
    {
        retVal = ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    for (k = 0; k < naluCount; k++)
    {
        if ((nalu[k].naluBuffer == NULL) ||
            (nalu[k].naluSize == 0) ||
            (nalu[k].auTimestamp == 0))
        {
            retVal = ARSTREAM2_ERROR_BAD_PARAMETERS;
        }
    }

    ARSAL_Mutex_Lock(&(sender->streamMutex));
    if (!sender->streamThreadStarted)
    {
        retVal = ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    ARSAL_Mutex_Unlock(&(sender->streamMutex));

    if (retVal == ARSTREAM2_OK)
    {
        inputTimestamp = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;
        res = ARSTREAM2_RtpSender_EnqueueNNalu(sender, nalu, naluCount, inputTimestamp);
        if (res < 0)
        {
            retVal = ARSTREAM2_ERROR_QUEUE_FULL;
        }
    }
    return retVal;
}


eARSTREAM2_ERROR ARSTREAM2_RtpSender_FlushNaluQueue(ARSTREAM2_RtpSender_t *sender)
{
    eARSTREAM2_ERROR retVal = ARSTREAM2_OK;

    // Args check
    if (sender == NULL)
    {
        retVal = ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    int ret = ARSTREAM2_RtpSender_FlushNaluFifo(sender);
    if (ret != 0)
    {
        retVal = ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    return retVal;
}


static void ARSTREAM2_RtpSender_UpdateMonitoring(ARSTREAM2_RtpSender_t *sender, uint64_t inputTimestamp, uint64_t auTimestamp, uint32_t rtpTimestamp, uint16_t seqNum, uint16_t markerBit, uint32_t bytesSent, uint32_t bytesDropped)
{
    uint64_t curTime;
    struct timespec t1;
    ARSAL_Time_GetTime(&t1);
    curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

    ARSAL_Mutex_Lock(&(sender->monitoringMutex));

    if (sender->monitoringCount < ARSTREAM2_RTP_SENDER_MONITORING_MAX_POINTS)
    {
        sender->monitoringCount++;
    }
    sender->monitoringIndex = (sender->monitoringIndex + 1) % ARSTREAM2_RTP_SENDER_MONITORING_MAX_POINTS;
    sender->monitoringPoint[sender->monitoringIndex].auTimestamp = auTimestamp;
    sender->monitoringPoint[sender->monitoringIndex].inputTimestamp = inputTimestamp;
    sender->monitoringPoint[sender->monitoringIndex].sendTimestamp = curTime;
    sender->monitoringPoint[sender->monitoringIndex].bytesSent = bytesSent;
    sender->monitoringPoint[sender->monitoringIndex].bytesDropped = bytesDropped;

    ARSAL_Mutex_Unlock(&(sender->monitoringMutex));

#ifdef ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT
    if (sender->fMonitorOut)
    {
        fprintf(sender->fMonitorOut, "%llu ", (long long unsigned int)curTime);
        fprintf(sender->fMonitorOut, "%llu ", (long long unsigned int)inputTimestamp);
        fprintf(sender->fMonitorOut, "%llu ", (long long unsigned int)auTimestamp);
        fprintf(sender->fMonitorOut, "%lu %u %u %lu %lu\n", (long unsigned int)rtpTimestamp, seqNum, markerBit, (long unsigned int)bytesSent, (long unsigned int)bytesDropped);
    }
#endif
}


static int ARSTREAM2_RtpSender_SetSocketSendBufferSize(ARSTREAM2_RtpSender_t *sender, int socket, int size)
{
    int ret = 0, err;
    socklen_t size2 = sizeof(size2);

    size /= 2;
    err = ARSAL_Socket_Setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (void*)&size, sizeof(size));
    if (err != 0)
    {
        ret = -1;
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Failed to set socket send buffer size to 2*%d bytes: error=%d (%s)", size, errno, strerror(errno));
    }

    size = -1;
    err = ARSAL_Socket_Getsockopt(socket, SOL_SOCKET, SO_SNDBUF, (void*)&size, &size2);
    if (err != 0)
    {
        ret = -1;
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Failed to get socket send buffer size: error=%d (%s)", errno, strerror(errno));
    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_SENDER_TAG, "Socket send buffer size is %d bytes", size);
    }

    return ret;
}


static int ARSTREAM2_RtpSender_StreamSocketSetup(ARSTREAM2_RtpSender_t *sender)
{
    int ret = 0;
    int err;
    struct sockaddr_in sourceSin;

    /* create socket */
    sender->streamSocket = ARSAL_Socket_Create(AF_INET, SOCK_DGRAM, 0);
    if (sender->streamSocket < 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Failed to create stream socket");
        ret = -1;
    }

    /* initialize socket */
#if HAVE_DECL_SO_NOSIGPIPE
    if (ret == 0)
    {
        /* remove SIGPIPE */
        int set = 1;
        err = ARSAL_Socket_Setsockopt(sender->streamSocket, SOL_SOCKET, SO_NOSIGPIPE, (void*)&set, sizeof(int));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Error on setsockopt: error=%d (%s)", errno, strerror(errno));
        }
    }
#endif

    if (ret == 0)
    {
        /* set to non-blocking */
        int flags = fcntl(sender->streamSocket, F_GETFL, 0);
        fcntl(sender->streamSocket, F_SETFL, flags | O_NONBLOCK);

        /* source address */
        memset(&sourceSin, 0, sizeof(sourceSin));
        sourceSin.sin_family = AF_INET;
        sourceSin.sin_port = htons(sender->serverStreamPort);
        sourceSin.sin_addr.s_addr = htonl(INADDR_ANY);

        /* send address */
        memset(&sender->streamSendSin, 0, sizeof(struct sockaddr_in));
        sender->streamSendSin.sin_family = AF_INET;
        sender->streamSendSin.sin_port = htons(sender->clientStreamPort);
        err = inet_pton(AF_INET, sender->clientAddr, &(sender->streamSendSin.sin_addr));
        if (err <= 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Failed to convert address '%s'", sender->clientAddr);
            ret = -1;
        }
    }

    if (ret == 0)
    {
        if ((sender->mcastAddr) && (strlen(sender->mcastAddr)))
        {
            int addrFirst = atoi(sender->mcastAddr);
            if ((addrFirst >= 224) && (addrFirst <= 239))
            {
                /* multicast */
                err = inet_pton(AF_INET, sender->mcastAddr, &(sender->streamSendSin.sin_addr));
                if (err <= 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Failed to convert address '%s'", sender->mcastAddr);
                    ret = -1;
                }

                if ((sender->mcastIfaceAddr) && (strlen(sender->mcastIfaceAddr) > 0))
                {
                    err = inet_pton(AF_INET, sender->mcastIfaceAddr, &(sourceSin.sin_addr));
                    if (err <= 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Failed to convert address '%s'", sender->mcastIfaceAddr);
                        ret = -1;
                    }
                    sender->isMulticast = 1;
                }
                else
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Trying to send multicast to address '%s' without an interface address", sender->mcastAddr);
                    ret = -1;
                }
            }
            else
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Invalid multicast address '%s'", sender->mcastAddr);
                ret = -1;
            }
        }
    }

    if (ret == 0)
    {
        /* bind the socket */
        err = ARSAL_Socket_Bind(sender->streamSocket, (struct sockaddr*)&sourceSin, sizeof(sourceSin));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Error on stream socket bind: error=%d (%s)", errno, strerror(errno));
            ret = -1;
        }
    }

    if ((ret == 0) && (!sender->isMulticast))
    {
        /* connect the socket */
        err = ARSAL_Socket_Connect(sender->streamSocket, (struct sockaddr*)&sender->streamSendSin, sizeof(sender->streamSendSin));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Error on stream socket connect to addr='%s' port=%d: error=%d (%s)", sender->clientAddr, sender->clientStreamPort, errno, strerror(errno));
            ret = -1;
        }                
    }

    if (ret == 0)
    {
        /* set the socket buffer size and NALU FIFO buffer size */
        if (sender->streamSocketSendBufferSize)
        {
            err = ARSTREAM2_RtpSender_SetSocketSendBufferSize(sender, sender->streamSocket, sender->streamSocketSendBufferSize);
            if (err != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Failed to set the send socket buffer size");
                ret = -1;
            }
        }
    }

    if (ret != 0)
    {
        if (sender->streamSocket >= 0)
        {
            ARSAL_Socket_Close(sender->streamSocket);
        }
        sender->streamSocket = -1;
    }

    return ret;
}


static int ARSTREAM2_RtpSender_ControlSocketSetup(ARSTREAM2_RtpSender_t *sender)
{
    int ret = 0;
    struct sockaddr_in sendSin;
    struct sockaddr_in recvSin;
    int err;

    if (ret == 0)
    {
        /* create socket */
        sender->controlSocket = ARSAL_Socket_Create(AF_INET, SOCK_DGRAM, 0);
        if (sender->controlSocket < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Failed to create control socket");
            ret = -1;
        }
    }

#if HAVE_DECL_SO_NOSIGPIPE
    if (ret == 0)
    {
        /* remove SIGPIPE */
        int set = 1;
        err = ARSAL_Socket_Setsockopt(sender->controlSocket, SOL_SOCKET, SO_NOSIGPIPE, (void*)&set, sizeof(int));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Error on setsockopt: error=%d (%s)", errno, strerror(errno));
        }
    }
#endif

    if (ret == 0)
    {
        /* set to non-blocking */
        int flags = fcntl(sender->controlSocket, F_GETFL, 0);
        fcntl(sender->controlSocket, F_SETFL, flags | O_NONBLOCK);

        /* receive address */
        memset(&recvSin, 0, sizeof(struct sockaddr_in));
        recvSin.sin_family = AF_INET;
        recvSin.sin_port = htons(sender->serverControlPort);
        recvSin.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if (ret == 0)
    {
        /* allow multiple sockets to use the same port */
        unsigned int yes = 1;
        err = ARSAL_Socket_Setsockopt(sender->controlSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Failed to set socket option SO_REUSEADDR: error=%d (%s)", errno, strerror(errno));
            ret = -1;
        }
    }

    if (ret == 0)
    {
        /* bind the socket */
        err = ARSAL_Socket_Bind(sender->controlSocket, (struct sockaddr*)&recvSin, sizeof(recvSin));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Error on control socket bind port=%d: error=%d (%s)", sender->clientControlPort, errno, strerror(errno));
            ret = -1;
        }
    }

    if (ret == 0)
    {
        /* send address */
        memset(&sendSin, 0, sizeof(struct sockaddr_in));
        sendSin.sin_family = AF_INET;
        sendSin.sin_port = htons(sender->clientControlPort);
        err = inet_pton(AF_INET, sender->clientAddr, &(sendSin.sin_addr));
        if (err <= 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Failed to convert address '%s'", sender->clientAddr);
            ret = -1;
        }
    }

    if (ret == 0)
    {
        /* connect the socket */
        err = ARSAL_Socket_Connect(sender->controlSocket, (struct sockaddr*)&sendSin, sizeof(sendSin));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Error on control socket connect to addr='%s' port=%d: error=%d (%s)", sender->clientAddr, sender->clientControlPort, errno, strerror(errno));
            ret = -1;
        }
    }

    if (ret != 0)
    {
        if (sender->controlSocket >= 0)
        {
            ARSAL_Socket_Close(sender->controlSocket);
        }
        sender->controlSocket = -1;
    }

    return ret;
}


static int ARSTREAM2_RtpSender_SendData(ARSTREAM2_RtpSender_t *sender, uint8_t *sendBuffer, uint32_t sendSize, uint64_t inputTimestamp, uint64_t auTimestamp, int isLastInAu, int maxLatencyUs, int maxNetworkLatencyUs, int seqNumForcedDiscontinuity)
{
    int ret = 0, totalLatencyDrop = 0, networkLatencyDrop = 0;
    ARSTREAM2_RTP_Header_t *header = (ARSTREAM2_RTP_Header_t *)sendBuffer;
    uint16_t flags;
    uint32_t rtpTimestamp;

    /* header */
    flags = 0x8060; /* with PT=96 */
    if (isLastInAu)
    {
        /* set the marker bit */
        flags |= (1 << 7);
    }
    header->flags = htons(flags);
    sender->seqNum = sender->seqNum + seqNumForcedDiscontinuity;
    header->seqNum = htons(sender->seqNum++);
    rtpTimestamp = (uint32_t)((((auTimestamp * 90) + 500) / 1000) & 0xFFFFFFFF); /* microseconds to 90000 Hz clock */
    //TODO: handle the timestamp 32 bits loopback
    header->timestamp = htonl(rtpTimestamp);
    header->ssrc = htonl(ARSTREAM2_RTP_SSRC);

    /* send to the network layer */
    ssize_t bytes;
    struct timespec t1;
    ARSAL_Time_GetTime(&t1);
    uint64_t curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;
    if ((maxLatencyUs > 0) && (curTime - auTimestamp > maxLatencyUs))
    {
        totalLatencyDrop = 1;
    }
    else if ((maxNetworkLatencyUs > 0) && (curTime - inputTimestamp > maxNetworkLatencyUs))
    {
        networkLatencyDrop = 1;
    }

    if ((!totalLatencyDrop) && (!networkLatencyDrop))
    {
        if (sender->isMulticast)
        {
            bytes = ARSAL_Socket_Sendto(sender->streamSocket, sendBuffer, sendSize, 0, (struct sockaddr*)&sender->streamSendSin, sizeof(sender->streamSendSin));
        }
        else
        {
            bytes = ARSAL_Socket_Send(sender->streamSocket, sendBuffer, sendSize, 0);
        }
        if (bytes < 0)
        {
            /* socket send failed */
            switch (errno)
            {
            case EAGAIN:
            {
                struct pollfd p;
                p.fd = sender->streamSocket;
                p.events = POLLOUT;
                p.revents = 0;
                int pollTimeMs = (maxNetworkLatencyUs - (int)(curTime - inputTimestamp)) / 1000;
                if (pollTimeMs >= ARSTREAM2_RTP_SENDER_MIN_POLL_TIMEOUT_MS)
                {
                    int pollRet = poll(&p, 1, pollTimeMs);
                    if (pollRet == 0)
                    {
                        /* failed: poll timeout */
                        ARSTREAM2_RtpSender_UpdateMonitoring(sender, inputTimestamp, auTimestamp, rtpTimestamp, sender->seqNum - 1, isLastInAu, 0, sendSize);
                        ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_SENDER_TAG, "Time %llu: dropped packet (polling timed out: timeout = %dms) (seqNum = %d)",
                                    auTimestamp, pollTimeMs, sender->seqNum - 1);
                        ret = -2;
                    }
                    else if (pollRet < 0)
                    {
                        /* failed: poll error */
                        ARSTREAM2_RtpSender_UpdateMonitoring(sender, inputTimestamp, auTimestamp, rtpTimestamp, sender->seqNum - 1, isLastInAu, 0, sendSize);
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Poll error: error=%d (%s)", errno, strerror(errno));
                        ret = -1;
                    }
                    else if (p.revents & POLLOUT)
                    {
                        ARSAL_Time_GetTime(&t1);
                        curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;
                        if ((maxLatencyUs > 0) && (curTime - auTimestamp > maxLatencyUs))
                        {
                            totalLatencyDrop = 1;
                        }
                        else if ((maxNetworkLatencyUs > 0) && (curTime - inputTimestamp > maxNetworkLatencyUs))
                        {
                            networkLatencyDrop = 1;
                        }

                        if ((!totalLatencyDrop) && (!networkLatencyDrop))
                        {
                            if (sender->isMulticast)
                            {
                                bytes = ARSAL_Socket_Sendto(sender->streamSocket, sendBuffer, sendSize, 0, (struct sockaddr*)&sender->streamSendSin, sizeof(sender->streamSendSin));
                            }
                            else
                            {
                                bytes = ARSAL_Socket_Send(sender->streamSocket, sendBuffer, sendSize, 0);
                            }
                            if (bytes > -1)
                            {
                                /* socket send successful */
                                ARSTREAM2_RtpSender_UpdateMonitoring(sender, inputTimestamp, auTimestamp, rtpTimestamp, sender->seqNum - 1, isLastInAu, (uint32_t)bytes, 0);
                                ret = 0;
                            }
                            else if (errno == EAGAIN)
                            {
                                /* failed: socket buffer full */
                                ARSTREAM2_RtpSender_UpdateMonitoring(sender, inputTimestamp, auTimestamp, rtpTimestamp, sender->seqNum - 1, isLastInAu, 0, sendSize);
                                ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_SENDER_TAG, "Time %llu: dropped packet (socket buffer full #2) (seqNum = %d)",
                                            auTimestamp, sender->seqNum - 1);
                                ret = -2;
                            }
                            else
                            {
                                /* failed: socket error */
                                ARSTREAM2_RtpSender_UpdateMonitoring(sender, inputTimestamp, auTimestamp, rtpTimestamp, sender->seqNum - 1, isLastInAu, 0, sendSize);
                                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Socket send error #2 error=%d (%s)", errno, strerror(errno));
                                ret = -1;
                            }
                        }
                        else
                        {
                            /* packet dropped: too late */
                            ARSTREAM2_RtpSender_UpdateMonitoring(sender, inputTimestamp, auTimestamp, rtpTimestamp, sender->seqNum - 1, isLastInAu, 0, sendSize);
                            if (totalLatencyDrop)
                            {
                                ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_SENDER_TAG, "Time %llu: dropped packet (after poll total latency %.1fms > %.1fms) (seqNum = %d)",
                                            auTimestamp, (float)(curTime - auTimestamp) / 1000., (float)maxLatencyUs / 1000., sender->seqNum - 1);
                            }
                            if (networkLatencyDrop)
                            {
                                ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_SENDER_TAG, "Time %llu: dropped packet (after poll network latency %.1fms > %.1fms) (seqNum = %d)",
                                            auTimestamp, (float)(curTime - inputTimestamp) / 1000., (float)maxNetworkLatencyUs / 1000., sender->seqNum - 1);
                            }
                            ret = -2;
                        }
                    }
                }
                else
                {
                    /* packet dropped: poll timeout too short */
                    ARSTREAM2_RtpSender_UpdateMonitoring(sender, inputTimestamp, auTimestamp, rtpTimestamp, sender->seqNum - 1, isLastInAu, 0, sendSize);
                    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_SENDER_TAG, "Time %llu: dropped packet (poll timeout too short: timeout = %dms) (seqNum = %d)",
                                auTimestamp, pollTimeMs, sender->seqNum - 1);
                    ret = -2;
                }
                break;
            }
            default:
                /* failed: socket error */
                ARSTREAM2_RtpSender_UpdateMonitoring(sender, inputTimestamp, auTimestamp, rtpTimestamp, sender->seqNum - 1, isLastInAu, 0, sendSize);
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Socket send error: error=%d (%s)", errno, strerror(errno));
                ret = -1;
                break;
            }
        }
        else
        {
            /* socket send successful */
            ARSTREAM2_RtpSender_UpdateMonitoring(sender, inputTimestamp, auTimestamp, rtpTimestamp, sender->seqNum - 1, isLastInAu, (uint32_t)bytes, 0);
        }
    }
    else
    {
        /* packet dropped: too late */
        ARSTREAM2_RtpSender_UpdateMonitoring(sender, inputTimestamp, auTimestamp, rtpTimestamp, sender->seqNum - 1, isLastInAu, 0, sendSize);
        if (totalLatencyDrop)
        {
            ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_SENDER_TAG, "Time %llu: dropped packet (total latency %.1fms > %.1fms) (seqNum = %d)",
                        auTimestamp, (float)(curTime - auTimestamp) / 1000., (float)maxLatencyUs / 1000., sender->seqNum - 1);
        }
        if (networkLatencyDrop)
        {
            ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_SENDER_TAG, "Time %llu: dropped packet (network latency %.1fms > %.1fms) (seqNum = %d)",
                        auTimestamp, (float)(curTime - inputTimestamp) / 1000., (float)maxNetworkLatencyUs / 1000., sender->seqNum - 1);
        }
        ret = -2;
    }

    return ret;
}


void* ARSTREAM2_RtpSender_RunStreamThread (void *ARSTREAM2_RtpSender_t_Param)
{
    /* Local declarations */
    ARSTREAM2_RtpSender_t *sender = (ARSTREAM2_RtpSender_t*)ARSTREAM2_RtpSender_t_Param;
    uint8_t *sendBuffer = NULL;
    uint32_t sendSize = 0;
    ARSTREAM2_RtpSender_Nalu_t nalu;
    uint64_t previousTimestamp = 0, stapAuTimestamp = 0, stapFirstNaluInputTimestamp = 0;
    void *previousAuUserPtr = NULL;
    int shouldStop;
    int targetPacketSize, maxPacketSize, packetSize, fragmentSize, meanFragmentSize, offset, fragmentOffset, fragmentCount;
    int ret, totalLatencyDrop, networkLatencyDrop;
    int stapPending = 0, stapNaluCount = 0, stapSeqNumForcedDiscontinuity = 0, maxLatencyUs, maxNetworkLatencyUs;
    uint8_t stapMaxNri = 0;
    uint64_t curTime;
    struct timespec t1;

    /* Parameters check */
    if (sender == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Error while starting %s, bad parameters", __FUNCTION__);
        return (void *)0;
    }

    /* Alloc and check */
    sendBuffer = malloc(sender->maxPacketSize + sizeof(ARSTREAM2_RTP_Header_t));
    if (sendBuffer == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Error while starting %s, cannot allocate memory", __FUNCTION__);
        return (void*)0;
    }

    /* Connection */
    ret = ARSTREAM2_RtpSender_StreamSocketSetup(sender);
    if (ret != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Failed to setup the stream socket (error %d) - aborting", ret);
        free(sendBuffer);
        return (void*)0;
    }

    targetPacketSize = sender->targetPacketSize;
    maxPacketSize = sender->maxPacketSize;
    maxLatencyUs = (sender->maxLatencyMs) ? sender->maxLatencyMs * 1000 - ((sender->maxBitrate) ? (int)((uint64_t)sender->streamSocketSendBufferSize * 8 * 1000000 / sender->maxBitrate) : 0) : 0;
    maxNetworkLatencyUs = (sender->maxNetworkLatencyMs) ? sender->maxNetworkLatencyMs * 1000 - ((sender->maxBitrate) ? (int)((uint64_t)sender->streamSocketSendBufferSize * 8 * 1000000 / sender->maxBitrate) : 0) : 0;

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_SENDER_TAG, "Sender stream thread running");
    ARSAL_Mutex_Lock(&(sender->streamMutex));
    sender->streamThreadStarted = 1;
    shouldStop = sender->threadsShouldStop;
    ARSAL_Mutex_Unlock(&(sender->streamMutex));

    while (shouldStop == 0)
    {
        int fifoRes = ARSTREAM2_RtpSender_DequeueNalu(sender, &nalu);

        if (fifoRes == 0)
        {
            ARSAL_Time_GetTime(&t1);
            curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

            if ((previousTimestamp != 0) && (nalu.auTimestamp != previousTimestamp))
            {
                if (stapPending)
                {
                    /* Finish the previous STAP-A packet */
                    uint8_t stapHeader = ARSTREAM2_RTP_NALU_TYPE_STAPA | ((stapMaxNri & 3) << 5);
                    *(sendBuffer + sizeof(ARSTREAM2_RTP_Header_t)) = stapHeader;
                    ret = ARSTREAM2_RtpSender_SendData(sender, sendBuffer, sendSize, stapFirstNaluInputTimestamp, stapAuTimestamp, 0, maxLatencyUs, maxNetworkLatencyUs, stapSeqNumForcedDiscontinuity); // do not set the marker bit
                    stapPending = 0;
                    sendSize = 0;
                }

                if (sender->auCallback != NULL)
                {
                    /* new Access Unit: do we need to call the auCallback? */
                    ARSAL_Mutex_Lock(&(sender->streamMutex));
                    if (previousTimestamp != sender->lastAuTimestamp)
                    {
                        sender->lastAuTimestamp = previousTimestamp;
                        ARSAL_Mutex_Unlock(&(sender->streamMutex));

                        /* call the auCallback */
                        sender->auCallback(ARSTREAM2_RTP_SENDER_STATUS_SENT, previousAuUserPtr, sender->auCallbackUserPtr);
                    }
                    else
                    {
                        ARSAL_Mutex_Unlock(&(sender->streamMutex));
                    }
                }
            }

            /* check that the NALU is not older than the max latency */
            totalLatencyDrop = networkLatencyDrop = 0;
            if ((maxLatencyUs > 0) && (curTime - nalu.auTimestamp > maxLatencyUs))
            {
                totalLatencyDrop = 1;
            }
            else if ((maxNetworkLatencyUs > 0) && (curTime - nalu.naluInputTimestamp > maxNetworkLatencyUs))
            {
                networkLatencyDrop = 1;
            }

            /* check that the NALU has not been flagged for dropping */
            if ((!nalu.drop) && (!totalLatencyDrop) && (!networkLatencyDrop))
            {
                /* A NALU is ready to send */
                fragmentCount = (nalu.naluSize + targetPacketSize / 2) / targetPacketSize;
                if (fragmentCount < 1) fragmentCount = 1;

                if ((fragmentCount > 1) || (nalu.naluSize > maxPacketSize))
                {
                    /* Fragmentation (FU-A) */

                    if (stapPending)
                    {
                        /* Finish the previous STAP-A packet */
                        uint8_t stapHeader = ARSTREAM2_RTP_NALU_TYPE_STAPA | ((stapMaxNri & 3) << 5);
                        *(sendBuffer + sizeof(ARSTREAM2_RTP_Header_t)) = stapHeader;
                        ret = ARSTREAM2_RtpSender_SendData(sender, sendBuffer, sendSize, stapFirstNaluInputTimestamp, stapAuTimestamp, 0, maxLatencyUs, maxNetworkLatencyUs, stapSeqNumForcedDiscontinuity); // do not set the marker bit
                        stapPending = 0;
                        sendSize = 0;
                    }

                    int i;
                    uint8_t fuIndicator, fuHeader, startBit, endBit;
                    fuIndicator = fuHeader = *nalu.naluBuffer;
                    fuIndicator &= ~0x1F;
                    fuIndicator |= ARSTREAM2_RTP_NALU_TYPE_FUA;
                    fuHeader &= ~0xE0;
                    meanFragmentSize = (nalu.naluSize + fragmentCount / 2) / fragmentCount;

                    for (i = 0, offset = 1; i < fragmentCount; i++)
                    {
                        fragmentSize = (i == fragmentCount - 1) ? nalu.naluSize - offset : meanFragmentSize;
                        fragmentOffset = 0;
                        do
                        {
                            packetSize = (fragmentSize - fragmentOffset > maxPacketSize - 2) ? maxPacketSize - 2 : fragmentSize - fragmentOffset;

                            if (packetSize + 2 <= maxPacketSize)
                            {
                                memcpy(sendBuffer + sizeof(ARSTREAM2_RTP_Header_t) + 2, nalu.naluBuffer + offset, packetSize);
                                sendSize = packetSize + 2 + sizeof(ARSTREAM2_RTP_Header_t);
                                *(sendBuffer + sizeof(ARSTREAM2_RTP_Header_t)) = fuIndicator;
                                startBit = (offset == 1) ? 0x80 : 0;
                                endBit = ((i == fragmentCount - 1) && (fragmentOffset + packetSize == fragmentSize)) ? 0x40 : 0;
                                *(sendBuffer + sizeof(ARSTREAM2_RTP_Header_t) + 1) = fuHeader | startBit | endBit;

                                ret = ARSTREAM2_RtpSender_SendData(sender, sendBuffer, sendSize, nalu.naluInputTimestamp, nalu.auTimestamp, ((nalu.isLastInAu) && (endBit)) ? 1 : 0, maxLatencyUs, maxNetworkLatencyUs, (startBit) ? nalu.seqNumForcedDiscontinuity : 0);
                                sendSize = 0;
                            }
                            else
                            {
                                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Time %llu: FU-A, packetSize + 2 > maxPacketSize (packetSize=%d)", nalu.auTimestamp, packetSize);
                            }

                            fragmentOffset += packetSize;
                            offset += packetSize;
                        }
                        while (fragmentOffset != fragmentSize);
                    }
                }
                else
                {
                    int stapSize = nalu.naluSize + 2 + ((!stapPending) ? sizeof(ARSTREAM2_RTP_Header_t) + 1 : 0);
                    if ((sendSize + stapSize >= maxPacketSize) || (sendSize + stapSize > targetPacketSize) || (nalu.seqNumForcedDiscontinuity))
                    {
                        if (stapPending)
                        {
                            /* Finish the previous STAP-A packet */
                            uint8_t stapHeader = ARSTREAM2_RTP_NALU_TYPE_STAPA | ((stapMaxNri & 3) << 5);
                            *(sendBuffer + sizeof(ARSTREAM2_RTP_Header_t)) = stapHeader;
                            ret = ARSTREAM2_RtpSender_SendData(sender, sendBuffer, sendSize, stapFirstNaluInputTimestamp, stapAuTimestamp, 0, maxLatencyUs, maxNetworkLatencyUs, stapSeqNumForcedDiscontinuity); // do not set the marker bit
                            stapPending = 0;
                            sendSize = 0;

                            stapSize = nalu.naluSize + 2 + ((!stapPending) ? sizeof(ARSTREAM2_RTP_Header_t) + 1 : 0);
                        }
                    }

                    if ((sendSize + stapSize >= maxPacketSize) || (sendSize + stapSize > targetPacketSize))
                    {
                        /* Single NAL unit */
                        memcpy(sendBuffer + sizeof(ARSTREAM2_RTP_Header_t), nalu.naluBuffer, nalu.naluSize);
                        sendSize = nalu.naluSize + sizeof(ARSTREAM2_RTP_Header_t);
                        
                        ret = ARSTREAM2_RtpSender_SendData(sender, sendBuffer, sendSize, nalu.naluInputTimestamp, nalu.auTimestamp, nalu.isLastInAu, maxLatencyUs, maxNetworkLatencyUs, nalu.seqNumForcedDiscontinuity);
                        sendSize = 0;
                    }
                    else
                    {
                        /* Aggregation (STAP-A) */
                        if (!stapPending)
                        {
                            stapPending = 1;
                            stapMaxNri = 0;
                            stapNaluCount = 0;
                            sendSize = sizeof(ARSTREAM2_RTP_Header_t) + 1;
                            stapAuTimestamp = nalu.auTimestamp;
                            stapSeqNumForcedDiscontinuity = nalu.seqNumForcedDiscontinuity;
                            stapFirstNaluInputTimestamp = nalu.naluInputTimestamp;
                        }
                        uint8_t nri = ((uint8_t)(*(nalu.naluBuffer)) >> 5) & 0x3;
                        if (nri > stapMaxNri)
                        {
                            stapMaxNri = nri;
                        }
                        *(sendBuffer + sendSize) = ((nalu.naluSize >> 8) & 0xFF);
                        *(sendBuffer + sendSize + 1) = (nalu.naluSize & 0xFF);
                        sendSize += 2;
                        memcpy(sendBuffer + sendSize, nalu.naluBuffer, nalu.naluSize);
                        sendSize += nalu.naluSize;
                        stapNaluCount++;
                        if (nalu.isLastInAu)
                        {
                            /* Finish the STAP-A packet */
                            uint8_t stapHeader = ARSTREAM2_RTP_NALU_TYPE_STAPA | ((stapMaxNri & 3) << 5);
                            *(sendBuffer + sizeof(ARSTREAM2_RTP_Header_t)) = stapHeader;
                            ret = ARSTREAM2_RtpSender_SendData(sender, sendBuffer, sendSize, stapFirstNaluInputTimestamp, stapAuTimestamp, 1, maxLatencyUs, maxNetworkLatencyUs, stapSeqNumForcedDiscontinuity); // set the marker bit
                            stapPending = 0;
                            sendSize = 0;
                        }
                    }
                }

                /* call the naluCallback */
                if (sender->naluCallback != NULL)
                {
                    sender->naluCallback(ARSTREAM2_RTP_SENDER_STATUS_SENT, nalu.naluUserPtr, sender->naluCallbackUserPtr);
                }
            }
            else
            {
                uint32_t rtpTimestamp = (uint32_t)((((nalu.auTimestamp * 90) + 500) / 1000) & 0xFFFFFFFF);

                /* increment the sequence number to let the receiver know that we dropped something */
                sender->seqNum = sender->seqNum + nalu.seqNumForcedDiscontinuity + 1;

                ARSTREAM2_RtpSender_UpdateMonitoring(sender, nalu.naluInputTimestamp, nalu.auTimestamp, rtpTimestamp, sender->seqNum - 1, nalu.isLastInAu, 0, nalu.naluSize);
                if (nalu.drop)
                {
                    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_SENDER_TAG, "Time %llu: dropped NALU in FIFO (seqNum = %d)", nalu.auTimestamp, sender->seqNum - 1);
                }
                else if (totalLatencyDrop)
                {
                    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_SENDER_TAG, "Time %llu: dropped NALU (total latency %.1fms > %.1fms) (seqNum = %d)",
                                nalu.auTimestamp, (float)(curTime - nalu.auTimestamp) / 1000., (float)maxLatencyUs / 1000., sender->seqNum - 1);
                }
                else if (networkLatencyDrop)
                {
                    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_RTP_SENDER_TAG, "Time %llu: dropped NALU (network latency %.1fms > %.1fms) (seqNum = %d)",
                                nalu.auTimestamp, (float)(curTime - nalu.naluInputTimestamp) / 1000., (float)maxNetworkLatencyUs / 1000., sender->seqNum - 1);
                }

                /* call the naluCallback */
                if (sender->naluCallback != NULL)
                {
                    sender->naluCallback(ARSTREAM2_RTP_SENDER_STATUS_CANCELLED, nalu.naluUserPtr, sender->naluCallbackUserPtr);
                }
            }

            /* last NALU in the Access Unit: call the auCallback */
            if ((sender->auCallback != NULL) && (nalu.isLastInAu))
            {
                ARSAL_Mutex_Lock(&(sender->streamMutex));
                if (nalu.auTimestamp != sender->lastAuTimestamp)
                {
                    sender->lastAuTimestamp = nalu.auTimestamp;
                    ARSAL_Mutex_Unlock(&(sender->streamMutex));

                    /* call the auCallback */
                    sender->auCallback(ARSTREAM2_RTP_SENDER_STATUS_SENT, nalu.auUserPtr, sender->auCallbackUserPtr);
                }
                else
                {
                    ARSAL_Mutex_Unlock(&(sender->streamMutex));
                }
            }

            previousTimestamp = nalu.auTimestamp;
            previousAuUserPtr = nalu.auUserPtr;
        }
        
        ARSAL_Mutex_Lock(&(sender->streamMutex));
        shouldStop = sender->threadsShouldStop;
        ARSAL_Mutex_Unlock(&(sender->streamMutex));
        
        if ((fifoRes != 0) && (shouldStop == 0))
        {
            /* Wake up when a new NALU is in the FIFO or when we need to exit */
            ARSAL_Mutex_Lock(&(sender->fifoMutex));
            ARSAL_Cond_Timedwait(&(sender->fifoCond), &(sender->fifoMutex), ARSTREAM2_RTP_SENDER_FIFO_COND_TIMEOUT_MS);
            ARSAL_Mutex_Unlock(&(sender->fifoMutex));
        }
    }

    ARSAL_Mutex_Lock(&(sender->streamMutex));
    sender->streamThreadStarted = 0;
    ARSAL_Mutex_Unlock(&(sender->streamMutex));

    /* cancel the last Access Unit */
    if (sender->auCallback != NULL)
    {
        ARSAL_Mutex_Lock(&(sender->streamMutex));
        if (previousTimestamp != sender->lastAuTimestamp)
        {
            sender->lastAuTimestamp = previousTimestamp;
            ARSAL_Mutex_Unlock(&(sender->streamMutex));

            /* call the auCallback */
            sender->auCallback(ARSTREAM2_RTP_SENDER_STATUS_CANCELLED, previousAuUserPtr, sender->auCallbackUserPtr);
        }
        else
        {
            ARSAL_Mutex_Unlock(&(sender->streamMutex));
        }
    }

    /* flush the NALU FIFO */
    ARSTREAM2_RtpSender_FlushNaluFifo(sender);

    free(sendBuffer);

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_SENDER_TAG, "Sender stream thread ended");

    return (void*)0;
}


void* ARSTREAM2_RtpSender_RunControlThread(void *ARSTREAM2_RtpSender_t_Param)
{
    ARSTREAM2_RtpSender_t *sender = (ARSTREAM2_RtpSender_t *)ARSTREAM2_RtpSender_t_Param;
    uint8_t *msgBuffer;
    int msgBufferSize = sizeof(ARSTREAM2_RTP_ClockFrame_t);
    ARSTREAM2_RTP_ClockFrame_t *clockFrame;
    uint64_t originateTimestamp = 0;
    uint64_t receiveTimestamp = 0;
    uint64_t transmitTimestamp = 0;
    //uint64_t receiveTimestamp2 = 0;
    //int64_t clockDelta = 0;
    //int64_t rtDelay = 0;
    uint32_t tsH, tsL;
    ssize_t bytes;
    struct timespec t1;
    struct pollfd p;
    struct sockaddr_in src_addr;
    socklen_t addrlen = sizeof(src_addr);
    int shouldStop, ret, pollRet;

    /* Parameters check */
    if (sender == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Error while starting %s, bad parameters", __FUNCTION__);
        return (void *)0;
    }

    /* Alloc and check */
    msgBuffer = malloc(msgBufferSize);
    if (msgBuffer == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Error while starting %s, cannot allocate memory", __FUNCTION__);
        return (void *)0;
    }
    clockFrame = (ARSTREAM2_RTP_ClockFrame_t*)msgBuffer;

    /* Socket setup */
    ret = ARSTREAM2_RtpSender_ControlSocketSetup(sender);
    if (ret != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Failed to setup the control socket (error %d) - aborting - aborting", ret);
        free(msgBuffer);
        return (void*)0;
    }

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_SENDER_TAG, "Sender control thread running");
    ARSAL_Mutex_Lock(&(sender->streamMutex));
    sender->controlThreadStarted = 1;
    shouldStop = sender->threadsShouldStop;
    ARSAL_Mutex_Unlock(&(sender->streamMutex));

    while (shouldStop == 0)
    {
        /* poll */
        p.fd = sender->controlSocket;
        p.events = POLLIN;
        p.revents = 0;
        pollRet = poll(&p, 1, ARSTREAM2_RTP_SENDER_CLOCKSYNC_DATAREAD_TIMEOUT_MS);
        if (pollRet == 0)
        {
            /* failed: poll timeout */
            //ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_SENDER_TAG, "Polling timed out");
        }
        else if (pollRet < 0)
        {
            /* failed: poll error */
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Poll error: error=%d (%s)", errno, strerror(errno));
        }
        else if (p.revents & POLLIN)
        {
            bytes = ARSAL_Socket_Recvfrom(sender->controlSocket, msgBuffer, msgBufferSize, 0, (struct sockaddr*)&src_addr, &addrlen);
            if (bytes >= 0)
            {
                /* success */
                ARSAL_Time_GetTime(&t1);
                receiveTimestamp = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

                originateTimestamp = ((uint64_t)ntohl(clockFrame->transmitTimestampH) << 32) + (uint64_t)ntohl(clockFrame->transmitTimestampL);
                /*ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTP_SENDER_TAG, "Clock message received - originateTimestamp: %llu", (long long unsigned int)originateTimestamp);*/ //TODO: debug

                memset(clockFrame, 0, sizeof(ARSTREAM2_RTP_ClockFrame_t));

                tsH = (uint32_t)(receiveTimestamp >> 32);
                tsL = (uint32_t)(receiveTimestamp & 0xFFFFFFFF);
                clockFrame->receiveTimestampH = htonl(tsH);
                clockFrame->receiveTimestampL = htonl(tsL);

                tsH = (uint32_t)(originateTimestamp >> 32);
                tsL = (uint32_t)(originateTimestamp & 0xFFFFFFFF);
                clockFrame->originateTimestampH = htonl(tsH);
                clockFrame->originateTimestampL = htonl(tsL);

                ARSAL_Time_GetTime(&t1);
                transmitTimestamp = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

                tsH = (uint32_t)(transmitTimestamp >> 32);
                tsL = (uint32_t)(transmitTimestamp & 0xFFFFFFFF);
                clockFrame->transmitTimestampH = htonl(tsH);
                clockFrame->transmitTimestampL = htonl(tsL);

                bytes = ARSAL_Socket_Sendto(sender->controlSocket, msgBuffer, msgBufferSize, 0, (struct sockaddr*)&src_addr, addrlen);
                if (bytes < 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Socket send error: error=%d (%s)", errno, strerror(errno));
                }
            }
            else
            {
                /* failed: socket error */
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Socket receive error %d ('%s')", errno, strerror(errno));
            }
        }
        else
        {
            /* no poll error, no timeout, but socket is not ready */
            int error = 0;
            socklen_t errlen = sizeof(error);
            ARSAL_Socket_Getsockopt(sender->controlSocket, SOL_SOCKET, SO_ERROR, (void *)&error, &errlen);
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "No poll error, no timeout, but socket is not ready (revents = %d, error = %d)", p.revents, error);
        }

        ARSAL_Mutex_Lock(&(sender->streamMutex));
        shouldStop = sender->threadsShouldStop;
        ARSAL_Mutex_Unlock(&(sender->streamMutex));
    }

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_SENDER_TAG, "Sender control thread ended");
    ARSAL_Mutex_Lock(&(sender->streamMutex));
    sender->controlThreadStarted = 0;
    ARSAL_Mutex_Unlock(&(sender->streamMutex));

    if (msgBuffer)
    {
        free(msgBuffer);
        msgBuffer = NULL;
    }

    return (void *)0;
}


void* ARSTREAM2_RtpSender_GetAuCallbackUserPtr(ARSTREAM2_RtpSender_t *sender)
{
    void *ret = NULL;
    if (sender != NULL)
    {
        ret = sender->auCallbackUserPtr;
    }
    return ret;
}


void* ARSTREAM2_RtpSender_GetNaluCallbackUserPtr(ARSTREAM2_RtpSender_t *sender)
{
    void *ret = NULL;
    if (sender != NULL)
    {
        ret = sender->naluCallbackUserPtr;
    }
    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_RtpSender_GetMonitoring(ARSTREAM2_RtpSender_t *sender, uint64_t startTime, uint32_t timeIntervalUs, uint32_t *realTimeIntervalUs, uint32_t *meanAcqToNetworkTime,
                                                   uint32_t *acqToNetworkJitter, uint32_t *meanNetworkTime, uint32_t *networkJitter, uint32_t *bytesSent, uint32_t *meanPacketSize,
                                                   uint32_t *packetSizeStdDev, uint32_t *packetsSent, uint32_t *bytesDropped, uint32_t *naluDropped)
{
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;
    uint64_t endTime, curTime, previousTime, acqToNetworkSum = 0, networkSum = 0, acqToNetworkVarSum = 0, networkVarSum = 0, packetSizeVarSum = 0;
    uint32_t _bytesSent, _bytesDropped, bytesSentSum = 0, bytesDroppedSum = 0, _meanPacketSize = 0, acqToNetworkTime = 0, networkTime = 0;
    uint32_t _acqToNetworkJitter = 0, _networkJitter = 0, _meanAcqToNetworkTime = 0, _meanNetworkTime = 0, _packetSizeStdDev = 0;
    int points = 0, usefulPoints = 0, _packetsSent = 0, _naluDropped = 0, idx, i, firstUsefulIdx = -1;

    if ((sender == NULL) || (timeIntervalUs == 0))
    {
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    if (startTime == 0)
    {
        struct timespec t1;
        ARSAL_Time_GetTime(&t1);
        startTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;
    }
    endTime = startTime;

    ARSAL_Mutex_Lock(&(sender->monitoringMutex));

    if (sender->monitoringCount > 0)
    {
        idx = sender->monitoringIndex;
        previousTime = startTime;

        while (points < sender->monitoringCount)
        {
            curTime = sender->monitoringPoint[idx].sendTimestamp;
            if (curTime > startTime)
            {
                points++;
                idx = (idx - 1 >= 0) ? idx - 1 : ARSTREAM2_RTP_SENDER_MONITORING_MAX_POINTS - 1;
                continue;
            }
            if (startTime - curTime > timeIntervalUs)
            {
                break;
            }
            if (firstUsefulIdx == -1)
            {
                firstUsefulIdx = idx;
            }
            _bytesSent = sender->monitoringPoint[idx].bytesSent;
            bytesSentSum += _bytesSent;
            if (_bytesSent)
            {
                _packetsSent++;
                acqToNetworkTime = curTime - sender->monitoringPoint[idx].auTimestamp;
                acqToNetworkSum += acqToNetworkTime;
                networkTime = curTime - sender->monitoringPoint[idx].inputTimestamp;
                networkSum += networkTime;
            }
            _bytesDropped = sender->monitoringPoint[idx].bytesDropped;
            bytesDroppedSum += _bytesDropped;
            if (_bytesDropped)
            {
                _naluDropped++;
            }
            previousTime = curTime;
            usefulPoints++;
            points++;
            idx = (idx - 1 >= 0) ? idx - 1 : ARSTREAM2_RTP_SENDER_MONITORING_MAX_POINTS - 1;
        }

        endTime = previousTime;
        _meanPacketSize = (_packetsSent > 0) ? (bytesSentSum / _packetsSent) : 0;
        _meanAcqToNetworkTime = (_packetsSent > 0) ? (uint32_t)(acqToNetworkSum / _packetsSent) : 0;
        _meanNetworkTime = (_packetsSent > 0) ? (uint32_t)(networkSum / _packetsSent) : 0;

        if ((acqToNetworkJitter) || (networkJitter) || (packetSizeStdDev))
        {
            for (i = 0, idx = firstUsefulIdx; i < usefulPoints; i++)
            {
                idx = (idx - 1 >= 0) ? idx - 1 : ARSTREAM2_RTP_SENDER_MONITORING_MAX_POINTS - 1;
                curTime = sender->monitoringPoint[idx].sendTimestamp;
                _bytesSent = sender->monitoringPoint[idx].bytesSent;
                if (_bytesSent)
                {
                    acqToNetworkTime = curTime - sender->monitoringPoint[idx].auTimestamp;
                    networkTime = curTime - sender->monitoringPoint[idx].inputTimestamp;
                    packetSizeVarSum += ((_bytesSent - _meanPacketSize) * (_bytesSent - _meanPacketSize));
                    acqToNetworkVarSum += ((acqToNetworkTime - _meanAcqToNetworkTime) * (acqToNetworkTime - _meanAcqToNetworkTime));
                    networkVarSum += ((networkTime - _meanNetworkTime) * (networkTime - _meanNetworkTime));
                }
            }
            _acqToNetworkJitter = (_packetsSent > 0) ? (uint32_t)(sqrt((double)acqToNetworkVarSum / _packetsSent)) : 0;
            _networkJitter = (_packetsSent > 0) ? (uint32_t)(sqrt((double)networkVarSum / _packetsSent)) : 0;
            _packetSizeStdDev = (_packetsSent > 0) ? (uint32_t)(sqrt((double)packetSizeVarSum / _packetsSent)) : 0;
        }
    }

    ARSAL_Mutex_Unlock(&(sender->monitoringMutex));

    if (realTimeIntervalUs)
    {
        *realTimeIntervalUs = (startTime - endTime);
    }
    if (meanAcqToNetworkTime)
    {
        *meanAcqToNetworkTime = _meanAcqToNetworkTime;
    }
    if (acqToNetworkJitter)
    {
        *acqToNetworkJitter = _acqToNetworkJitter;
    }
    if (meanNetworkTime)
    {
        *meanNetworkTime = _meanNetworkTime;
    }
    if (networkJitter)
    {
        *networkJitter = _networkJitter;
    }
    if (bytesSent)
    {
        *bytesSent = bytesSentSum;
    }
    if (meanPacketSize)
    {
        *meanPacketSize = _meanPacketSize;
    }
    if (packetSizeStdDev)
    {
        *packetSizeStdDev = _packetSizeStdDev;
    }
    if (packetsSent)
    {
        *packetsSent = _packetsSent;
    }
    if (bytesDropped)
    {
        *bytesDropped = bytesDroppedSum;
    }
    if (naluDropped)
    {
        *naluDropped = _naluDropped;
    }

    return ret;
}
