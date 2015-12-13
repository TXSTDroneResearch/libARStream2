/**
 * @file arstream2_rtp_receiver.c
 * @brief Parrot Streaming Library - RTP Receiver
 * @date 04/16/2015
 * @author aurelien.barre@parrot.com
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <poll.h>
#include <math.h>

#include <libARStream2/arstream2_rtp_receiver.h>
#include <libARStream2/arstream2_rtp_sender.h>
#include "arstream2_rtp.h"
#include "arstream2_h264.h"

#include <libARSAL/ARSAL_Print.h>
#include <libARSAL/ARSAL_Mutex.h>
#include <libARSAL/ARSAL_Socket.h>


#define ARSTREAM2_RTP_RECEIVER_TAG "ARSTREAM2_RtpReceiver"

#define ARSTREAM2_RTP_RECEIVER_STREAM_DATAREAD_TIMEOUT_MS (500)
#define ARSTREAM2_RTP_RECEIVER_CLOCKSYNC_DATAREAD_TIMEOUT_MS (100)
#define ARSTREAM2_RTP_RECEIVER_CLOCKSYNC_PERIOD_MS (200) // 5 Hz

#define ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_MAX_COUNT (4)
#define ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_MAX_NALU_BUFFER_COUNT (1024) //TODO: tune this value
#define ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_NALU_BUFFER_MALLOC_CHUNK_SIZE (4096)

#define ARSTREAM2_RTP_RECEIVER_MONITORING_MAX_POINTS (2048)

#define ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT
#ifdef ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT
    #include <stdio.h>

    #define ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_ALLOW_DRONE
    #define ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_PATH_DRONE "/data/ftp/internal_000/streamdebug"
    #define ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_ALLOW_NAP_USB
    #define ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_PATH_NAP_USB "/tmp/mnt/STREAMDEBUG/streamdebug"
    //#define ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_ALLOW_NAP_INTERNAL
    #define ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_PATH_NAP_INTERNAL "/data/skycontroller/streamdebug"
    #define ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_ALLOW_ANDROID_INTERNAL
    #define ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_PATH_ANDROID_INTERNAL "/sdcard/FF/streamdebug"
    #define ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_ALLOW_PCLINUX
    #define ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_PATH_PCLINUX "./streamdebug"

    #define ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_FILENAME "receiver_monitor"
#endif


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


typedef struct ARSTREAM2_RtpReceiver_MonitoringPoint_s {
    uint64_t recvTimestamp;
    uint64_t timestampShifted;
    uint32_t timestamp;
    uint16_t seqNum;
    uint16_t markerBit;
    uint32_t bytes;
} ARSTREAM2_RtpReceiver_MonitoringPoint_t;


struct ARSTREAM2_RtpReceiver_RtpResender_t {
    ARSTREAM2_RtpReceiver_t *receiver;
    ARSTREAM2_RtpSender_t *sender;
    int senderRunning;
};


typedef struct ARSTREAM2_RtpReceiver_NaluBuffer_s {
    int useCount;
    uint8_t *naluBuffer;
    int naluBufferSize;
    int naluSize;
    uint64_t auTimestamp;
    int isLastNaluInAu;
} ARSTREAM2_RtpReceiver_NaluBuffer_t;


struct ARSTREAM2_RtpReceiver_t {
    /* Configuration on New */
    char *serverAddr;
    char *mcastAddr;
    char *mcastIfaceAddr;
    int serverStreamPort;
    int serverControlPort;
    int clientStreamPort;
    int clientControlPort;
    ARSTREAM2_RtpReceiver_NaluCallback_t naluCallback;
    void *naluCallbackUserPtr;
    int maxPacketSize;
    int maxBitrate;
    int maxLatencyMs;
    int maxNetworkLatencyMs;
    int insertStartCodes;

    /* Current frame storage */
    int currentNaluBufferSize; // Usable length of the buffer
    int currentNaluSize;       // Actual data length
    uint8_t *currentNaluBuffer;
    uint64_t clockDelta;
    int scheduleNaluBufferChange;

    /* Thread status */
    ARSAL_Mutex_t streamMutex;
    ARSAL_Cond_t streamCond;
    int threadsShouldStop;
    int streamThreadStarted;
    int controlThreadStarted;

    /* Sockets */
    int isMulticast;
    int streamSocket;
    int controlSocket;

    /* Monitoring */
    ARSAL_Mutex_t monitoringMutex;
    int monitoringCount;
    int monitoringIndex;
    ARSTREAM2_RtpReceiver_MonitoringPoint_t monitoringPoint[ARSTREAM2_RTP_RECEIVER_MONITORING_MAX_POINTS];

    /* RtpResenders */
    ARSTREAM2_RtpReceiver_RtpResender_t *resender[ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_MAX_COUNT];
    int resenderCount;
    ARSAL_Mutex_t resenderMutex;
    ARSAL_Mutex_t naluBufferMutex;
    ARSTREAM2_RtpReceiver_NaluBuffer_t naluBuffer[ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_MAX_NALU_BUFFER_COUNT];
    int naluBufferCount;

#ifdef ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT
    FILE* fMonitorOut;
#endif
};


static void ARSTREAM2_RtpReceiver_RtpResender_NaluCallback(eARSTREAM2_RTP_SENDER_STATUS status, void *naluUserPtr, void *userPtr)
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


static ARSTREAM2_RtpReceiver_NaluBuffer_t* ARSTREAM2_RtpReceiver_RtpResender_GetAvailableNaluBuffer(ARSTREAM2_RtpReceiver_t *receiver, int naluSize)
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
                    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Reallocated NALU buffer (size: %d) - naluBufferCount = %d", newSize, receiver->naluBufferCount);
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
            ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Allocated new NALU buffer (size: %d) - naluBufferCount = %d", newSize, receiver->naluBufferCount);
            naluBuf->naluBufferSize = newSize;
            retNaluBuf = naluBuf;
        }
    }

    return retNaluBuf;
}


static int ARSTREAM2_RtpReceiver_ResendNalu(ARSTREAM2_RtpReceiver_t *receiver, uint8_t *naluBuffer, uint32_t naluSize, uint64_t auTimestamp, int isLastNaluInAu, int missingPacketsBefore)
{
    int ret = 0, i;
    ARSTREAM2_RtpReceiver_NaluBuffer_t *naluBuf = NULL;
    ARSTREAM2_RtpSender_H264NaluDesc_t nalu;

    /* remove the byte stream format start code */
    if (receiver->insertStartCodes)
    {
        naluBuffer += ARSTREAM2_H264_BYTE_STREAM_NALU_START_CODE_LENGTH;
        naluSize -= ARSTREAM2_H264_BYTE_STREAM_NALU_START_CODE_LENGTH;
    }

    ARSAL_Mutex_Lock(&(receiver->naluBufferMutex));

    /* get a buffer from the pool */
    naluBuf = ARSTREAM2_RtpReceiver_RtpResender_GetAvailableNaluBuffer(receiver, (int)naluSize);
    if (naluBuf == NULL)
    {
        ARSAL_Mutex_Unlock(&(receiver->naluBufferMutex));
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to get available NALU buffer (naluSize=%d, naluBufferCount=%d)", naluSize, receiver->naluBufferCount);
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
            err = ARSTREAM2_RtpSender_SendNewNalu(resender->sender, &nalu);
            if (err == ARSTREAM2_OK)
            {
                ARSAL_Mutex_Lock(&(receiver->naluBufferMutex));
                naluBuf->useCount++;
                ARSAL_Mutex_Unlock(&(receiver->naluBufferMutex));
            }
            else
            {
                ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to resend NALU on resender #%d (error %d)", i, err);
            }
        }
    }
    ARSAL_Mutex_Unlock(&(receiver->resenderMutex));

    ARSAL_Mutex_Lock(&(receiver->naluBufferMutex));
    naluBuf->useCount--;
    ARSAL_Mutex_Unlock(&(receiver->naluBufferMutex));

    return ret;
}


ARSTREAM2_RtpReceiver_t* ARSTREAM2_RtpReceiver_New(ARSTREAM2_RtpReceiver_Config_t *config, eARSTREAM2_ERROR *error)
{
    ARSTREAM2_RtpReceiver_t *retReceiver = NULL;
    int streamMutexWasInit = 0, streamCondWasInit = 0;
    int monitoringMutexWasInit = 0;
    int resenderMutexWasInit = 0;
    int naluBufferMutexWasInit = 0;
    eARSTREAM2_ERROR internalError = ARSTREAM2_OK;

    /* ARGS Check */
    if (config == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "No config provided");
        SET_WITH_CHECK(error, ARSTREAM2_ERROR_BAD_PARAMETERS);
        return retReceiver;
    }
    if ((config->serverAddr == NULL) || (!strlen(config->serverAddr)))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Config: no server address provided");
        SET_WITH_CHECK(error, ARSTREAM2_ERROR_BAD_PARAMETERS);
        return retReceiver;
    }
    if ((config->serverStreamPort <= 0) || (config->serverControlPort <= 0))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Config: no server ports provided");
        SET_WITH_CHECK(error, ARSTREAM2_ERROR_BAD_PARAMETERS);
        return retReceiver;
    }
    if (config->naluCallback == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Config: no NAL unit callback provided");
        SET_WITH_CHECK(error, ARSTREAM2_ERROR_BAD_PARAMETERS);
        return retReceiver;
    }

    /* Alloc new receiver */
    retReceiver = malloc(sizeof(ARSTREAM2_RtpReceiver_t));
    if (retReceiver == NULL)
    {
        internalError = ARSTREAM2_ERROR_ALLOC;
    }

    /* Initialize the receiver and copy parameters */
    if (internalError == ARSTREAM2_OK)
    {
        memset(retReceiver, 0, sizeof(ARSTREAM2_RtpReceiver_t));
        retReceiver->isMulticast = 0;
        retReceiver->streamSocket = -1;
        retReceiver->controlSocket = -1;

        if (config->serverAddr)
        {
            retReceiver->serverAddr = strndup(config->serverAddr, 16);
        }
        if (config->mcastAddr)
        {
            retReceiver->mcastAddr = strndup(config->mcastAddr, 16);
        }
        if (config->mcastIfaceAddr)
        {
            retReceiver->mcastIfaceAddr = strndup(config->mcastIfaceAddr, 16);
        }
        retReceiver->serverStreamPort = config->serverStreamPort;
        retReceiver->serverControlPort = config->serverControlPort;
        retReceiver->clientStreamPort = (config->clientStreamPort > 0) ? config->clientStreamPort : ARSTREAM2_RTP_RECEIVER_DEFAULT_CLIENT_STREAM_PORT;
        retReceiver->clientControlPort = (config->clientControlPort > 0) ? config->clientControlPort : ARSTREAM2_RTP_RECEIVER_DEFAULT_CLIENT_CONTROL_PORT;
        retReceiver->naluCallback = config->naluCallback;
        retReceiver->naluCallbackUserPtr = config->naluCallbackUserPtr;
        retReceiver->maxPacketSize = (config->maxPacketSize > 0) ? config->maxPacketSize - ARSTREAM2_RTP_TOTAL_HEADERS_SIZE : ARSTREAM2_RTP_MAX_PAYLOAD_SIZE;
        retReceiver->maxBitrate = (config->maxBitrate > 0) ? config->maxBitrate : 0;
        retReceiver->maxLatencyMs = (config->maxLatencyMs > 0) ? config->maxLatencyMs : 0;
        retReceiver->maxNetworkLatencyMs = (config->maxNetworkLatencyMs > 0) ? config->maxNetworkLatencyMs : 0;
        retReceiver->insertStartCodes = (config->insertStartCodes > 0) ? 1 : 0;
    }

    /* Setup internal mutexes/sems */
    if (internalError == ARSTREAM2_OK)
    {
        int mutexInitRet = ARSAL_Mutex_Init(&(retReceiver->streamMutex));
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
        int condInitRet = ARSAL_Cond_Init(&(retReceiver->streamCond));
        if (condInitRet != 0)
        {
            internalError = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            streamCondWasInit = 1;
        }
    }
    if (internalError == ARSTREAM2_OK)
    {
        int mutexInitRet = ARSAL_Mutex_Init(&(retReceiver->monitoringMutex));
        if (mutexInitRet != 0)
        {
            internalError = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            monitoringMutexWasInit = 1;
        }
    }
    if (internalError == ARSTREAM2_OK)
    {
        int mutexInitRet = ARSAL_Mutex_Init(&(retReceiver->resenderMutex));
        if (mutexInitRet != 0)
        {
            internalError = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            resenderMutexWasInit = 1;
        }
    }
    if (internalError == ARSTREAM2_OK)
    {
        int mutexInitRet = ARSAL_Mutex_Init(&(retReceiver->naluBufferMutex));
        if (mutexInitRet != 0)
        {
            internalError = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            naluBufferMutexWasInit = 1;
        }
    }

#ifdef ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT
    if (internalError == ARSTREAM2_OK)
    {
        int i;
        char szOutputFileName[128];
        char *pszFilePath = NULL;
        szOutputFileName[0] = '\0';
        if (0)
        {
        }
#ifdef ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_ALLOW_DRONE
        else if ((access(ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_PATH_DRONE, F_OK) == 0) && (access(ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_PATH_DRONE, W_OK) == 0))
        {
            pszFilePath = ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_PATH_DRONE;
        }
#endif
#ifdef ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_ALLOW_NAP_USB
        else if ((access(ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_PATH_NAP_USB, F_OK) == 0) && (access(ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_PATH_NAP_USB, W_OK) == 0))
        {
            pszFilePath = ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_PATH_NAP_USB;
        }
#endif
#ifdef ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_ALLOW_NAP_INTERNAL
        else if ((access(ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_PATH_NAP_INTERNAL, F_OK) == 0) && (access(ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_PATH_NAP_INTERNAL, W_OK) == 0))
        {
            pszFilePath = ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_PATH_NAP_INTERNAL;
        }
#endif
#ifdef ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_ALLOW_ANDROID_INTERNAL
        else if ((access(ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_PATH_ANDROID_INTERNAL, F_OK) == 0) && (access(ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_PATH_ANDROID_INTERNAL, W_OK) == 0))
        {
            pszFilePath = ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_PATH_ANDROID_INTERNAL;
        }
#endif
#ifdef ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_ALLOW_PCLINUX
        else if ((access(ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_PATH_PCLINUX, F_OK) == 0) && (access(ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_PATH_PCLINUX, W_OK) == 0))
        {
            pszFilePath = ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_PATH_PCLINUX;
        }
#endif
        if (pszFilePath)
        {
            for (i = 0; i < 1000; i++)
            {
                snprintf(szOutputFileName, 128, "%s/%s_%03d.dat", pszFilePath, ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT_FILENAME, i);
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
            retReceiver->fMonitorOut = fopen(szOutputFileName, "w");
            if (!retReceiver->fMonitorOut)
            {
                ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTP_RECEIVER_TAG, "Unable to open monitor output file '%s'", szOutputFileName);
            }
        }

        if (retReceiver->fMonitorOut)
        {
            fprintf(retReceiver->fMonitorOut, "recvTimestamp rtpTimestamp rtpTimestampShifted rtpSeqNum rtpMarkerBit bytes\n");
        }
    }
#endif //#ifdef ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT

    if ((internalError != ARSTREAM2_OK) &&
        (retReceiver != NULL))
    {
        if (streamMutexWasInit == 1)
        {
            ARSAL_Mutex_Destroy(&(retReceiver->streamMutex));
        }
        if (streamCondWasInit == 1)
        {
            ARSAL_Cond_Destroy(&(retReceiver->streamCond));
        }
        if (monitoringMutexWasInit == 1)
        {
            ARSAL_Mutex_Destroy(&(retReceiver->monitoringMutex));
        }
        if (resenderMutexWasInit == 1)
        {
            ARSAL_Mutex_Destroy(&(retReceiver->resenderMutex));
        }
        if (naluBufferMutexWasInit == 1)
        {
            ARSAL_Mutex_Destroy(&(retReceiver->naluBufferMutex));
        }
        if ((retReceiver) && (retReceiver->serverAddr))
        {
            free(retReceiver->serverAddr);
        }
        if ((retReceiver) && (retReceiver->mcastAddr))
        {
            free(retReceiver->mcastAddr);
        }
        if ((retReceiver) && (retReceiver->mcastIfaceAddr))
        {
            free(retReceiver->mcastIfaceAddr);
        }
#ifdef ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT
        if ((retReceiver) && (retReceiver->fMonitorOut))
        {
            fclose(retReceiver->fMonitorOut);
        }
#endif
        free(retReceiver);
        retReceiver = NULL;
    }

    SET_WITH_CHECK(error, internalError);
    return retReceiver;
}


void ARSTREAM2_RtpReceiver_InvalidateNaluBuffer(ARSTREAM2_RtpReceiver_t *receiver)
{
    if (receiver != NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Invalidating NALU buffer...");
        ARSAL_Mutex_Lock(&(receiver->streamMutex));
        if (receiver->streamThreadStarted)
        {
            receiver->scheduleNaluBufferChange = 1;
            ARSAL_Cond_Wait(&(receiver->streamCond), &(receiver->streamMutex));
        }
        ARSAL_Mutex_Unlock(&(receiver->streamMutex));
        ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "NALU buffer invalidated");
    }
}


void ARSTREAM2_RtpReceiver_Stop(ARSTREAM2_RtpReceiver_t *receiver)
{
    int i;

    if (receiver != NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Stopping receiver...");
        ARSAL_Mutex_Lock(&(receiver->streamMutex));
        receiver->threadsShouldStop = 1;
        ARSAL_Mutex_Unlock(&(receiver->streamMutex));
        ARSAL_Cond_Signal(&(receiver->streamCond));

        /* stop all resenders */
        ARSAL_Mutex_Lock(&(receiver->resenderMutex));
        for (i = 0; i < receiver->resenderCount; i++)
        {
            if (receiver->resender[i] != NULL)
            {
                ARSTREAM2_RtpSender_Stop(receiver->resender[i]->sender);
                receiver->resender[i]->senderRunning = 0;
            }
        }
        ARSAL_Mutex_Unlock(&(receiver->resenderMutex));
    }
}


eARSTREAM2_ERROR ARSTREAM2_RtpReceiver_Delete(ARSTREAM2_RtpReceiver_t **receiver)
{
    eARSTREAM2_ERROR retVal = ARSTREAM2_ERROR_BAD_PARAMETERS;
    if ((receiver != NULL) &&
        (*receiver != NULL))
    {
        int canDelete = 0;
        ARSAL_Mutex_Lock(&((*receiver)->streamMutex));
        if (((*receiver)->streamThreadStarted == 0) &&
            ((*receiver)->controlThreadStarted == 0))
        {
            ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "All threads stopped");
            canDelete = 1;
        }
        ARSAL_Mutex_Unlock(&((*receiver)->streamMutex));

        if (canDelete == 1)
        {
            int i;

            /* delete all resenders */
            ARSAL_Mutex_Lock(&((*receiver)->resenderMutex));
            for (i = 0; i < (*receiver)->resenderCount; i++)
            {
                ARSTREAM2_RtpReceiver_RtpResender_t *resender = (*receiver)->resender[i];
                if (resender == NULL)
                {
                    continue;
                }

                if (!resender->senderRunning)
                {
                    retVal = ARSTREAM2_RtpSender_Delete(&(resender->sender));
                    if (retVal == ARSTREAM2_OK)
                    {
                        free(resender);
                        (*receiver)->resender[i] = NULL;
                    }
                    else
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "RtpResender: failed to delete Sender (%d)", retVal);
                    }
                }
                else
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "RtpResender #%d is still running", i);
                }
            }
            ARSAL_Mutex_Unlock(&((*receiver)->resenderMutex));

            for (i = 0; i < (*receiver)->naluBufferCount; i++)
            {
                ARSTREAM2_RtpReceiver_NaluBuffer_t *naluBuf = &(*receiver)->naluBuffer[i];
                if (naluBuf->naluBuffer)
                {
                    free(naluBuf->naluBuffer);
                }
            }

#ifdef ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT
            if ((*receiver)->fMonitorOut)
            {
                fclose((*receiver)->fMonitorOut);
            }
#endif

            ARSAL_Mutex_Destroy(&((*receiver)->streamMutex));
            ARSAL_Cond_Destroy(&((*receiver)->streamCond));
            ARSAL_Mutex_Destroy(&((*receiver)->monitoringMutex));
            ARSAL_Mutex_Destroy(&((*receiver)->resenderMutex));
            ARSAL_Mutex_Destroy(&((*receiver)->naluBufferMutex));
            if ((*receiver)->controlSocket != -1)
            {
                ARSAL_Socket_Close((*receiver)->controlSocket);
                (*receiver)->controlSocket = -1;
            }
            if ((*receiver)->streamSocket != -1)
            {
                ARSAL_Socket_Close((*receiver)->streamSocket);
                (*receiver)->streamSocket = -1;
            }
            if ((*receiver)->serverAddr)
            {
                free((*receiver)->serverAddr);
            }
            if ((*receiver)->mcastAddr)
            {
                free((*receiver)->mcastAddr);
            }
            if ((*receiver)->mcastIfaceAddr)
            {
                free((*receiver)->mcastIfaceAddr);
            }
            free(*receiver);
            *receiver = NULL;
            retVal = ARSTREAM2_OK;
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Call ARSTREAM2_RtpReceiver_StopRtpReceiver before calling this function");
            retVal = ARSTREAM2_ERROR_BUSY;
        }
    }
    return retVal;
}


static int ARSTREAM2_RtpReceiver_SetSocketReceiveBufferSize(ARSTREAM2_RtpReceiver_t *receiver, int socket, int size)
{
    int ret = 0, err;
    socklen_t size2 = sizeof(size2);

    size /= 2;
    err = ARSAL_Socket_Setsockopt(socket, SOL_SOCKET, SO_RCVBUF, (void*)&size, sizeof(size));
    if (err != 0)
    {
        ret = -1;
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to set socket receive buffer size to 2*%d bytes: error=%d (%s)", size, errno, strerror(errno));
    }

    size = -1;
    err = ARSAL_Socket_Getsockopt(socket, SOL_SOCKET, SO_RCVBUF, (void*)&size, &size2);
    if (err != 0)
    {
        ret = -1;
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to get socket receive buffer size: error=%d (%s)", errno, strerror(errno));
    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Socket receive buffer size is %d bytes", size);
    }

    return ret;
}


static int ARSTREAM2_RtpReceiver_StreamSocketSetup(ARSTREAM2_RtpReceiver_t *receiver)
{
    int ret = 0;
    struct sockaddr_in recvSin;
    int err;

    /* create socket */
    receiver->streamSocket = ARSAL_Socket_Create(AF_INET, SOCK_DGRAM, 0);
    if (receiver->streamSocket < 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to create stream socket");
        ret = -1;
    }

#if HAVE_DECL_SO_NOSIGPIPE
    if (ret == 0)
    {
        /* remove SIGPIPE */
        int set = 1;
        err = ARSAL_Socket_Setsockopt(receiver->streamSocket, SOL_SOCKET, SO_NOSIGPIPE, (void*)&set, sizeof(int));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Error on setsockopt: error=%d (%s)", errno, strerror(errno));
        }
    }
#endif

    if (ret == 0)
    {
        /* set to non-blocking */
        int flags = fcntl(receiver->streamSocket, F_GETFL, 0);
        fcntl(receiver->streamSocket, F_SETFL, flags | O_NONBLOCK);

        memset(&recvSin, 0, sizeof(struct sockaddr_in));
        recvSin.sin_family = AF_INET;
        recvSin.sin_port = htons(receiver->clientStreamPort);
        recvSin.sin_addr.s_addr = htonl(INADDR_ANY);

        if ((receiver->mcastAddr) && (strlen(receiver->mcastAddr)))
        {
            int addrFirst = atoi(receiver->mcastAddr);
            if ((addrFirst >= 224) && (addrFirst <= 239))
            {
                /* multicast */
                struct ip_mreq mreq;
                memset(&mreq, 0, sizeof(mreq));
                err = inet_pton(AF_INET, receiver->mcastAddr, &(mreq.imr_multiaddr.s_addr));
                if (err <= 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to convert address '%s'", receiver->mcastAddr);
                    ret = -1;
                }

                if (ret == 0)
                {
                    if ((receiver->mcastIfaceAddr) && (strlen(receiver->mcastIfaceAddr) > 0))
                    {
                        err = inet_pton(AF_INET, receiver->mcastIfaceAddr, &(mreq.imr_interface.s_addr));
                        if (err <= 0)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to convert address '%s'", receiver->mcastIfaceAddr);
                            ret = -1;
                        }
                    }
                    else
                    {
                        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
                    }
                }

                if (ret == 0)
                {
                    /* join the multicast group */
                    err = ARSAL_Socket_Setsockopt(receiver->streamSocket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq));
                    if (err != 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to join multacast group: error=%d (%s)", errno, strerror(errno));
                        ret = -1;
                    }
                }

                receiver->isMulticast = 1;
            }
            else
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Invalid multicast address '%s'", receiver->mcastAddr);
                ret = -1;
            }
        }
    }

    if (ret == 0)
    {
        /* allow multiple sockets to use the same port */
        unsigned int yes = 1;
        err = ARSAL_Socket_Setsockopt(receiver->streamSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to set socket option SO_REUSEADDR: error=%d (%s)", errno, strerror(errno));
            ret = -1;
        }
    }

    if (ret == 0)
    {
        /* bind the socket */
        err = ARSAL_Socket_Bind(receiver->streamSocket, (struct sockaddr*)&recvSin, sizeof(recvSin));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Error on stream socket bind port=%d: error=%d (%s)", receiver->clientStreamPort, errno, strerror(errno));
            ret = -1;
        }
    }

    if (ret == 0)
    {
        /* set the socket buffer size */
        if ((receiver->maxNetworkLatencyMs) && (receiver->maxBitrate))
        {
            err = ARSTREAM2_RtpReceiver_SetSocketReceiveBufferSize(receiver, receiver->streamSocket, (receiver->maxNetworkLatencyMs * receiver->maxBitrate * 2) / 8000); //TODO: should not be x2
            if (err != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to set the socket buffer size (%d)", err);
                ret = -1;
            }
        }
    }

    if (ret != 0)
    {
        if (receiver->streamSocket >= 0)
        {
            ARSAL_Socket_Close(receiver->streamSocket);
        }
        receiver->streamSocket = -1;
    }

    return ret;
}


static int ARSTREAM2_RtpReceiver_ControlSocketSetup(ARSTREAM2_RtpReceiver_t *receiver)
{
    int ret = 0;
    struct sockaddr_in sendSin;
    struct sockaddr_in recvSin;
    int err;

    if (ret == 0)
    {
        /* create socket */
        receiver->controlSocket = ARSAL_Socket_Create(AF_INET, SOCK_DGRAM, 0);
        if (receiver->controlSocket < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to create control socket");
            ret = -1;
        }
    }

#if HAVE_DECL_SO_NOSIGPIPE
    if (ret == 0)
    {
        /* remove SIGPIPE */
        int set = 1;
        err = ARSAL_Socket_Setsockopt(receiver->controlSocket, SOL_SOCKET, SO_NOSIGPIPE, (void*)&set, sizeof(int));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Error on setsockopt: error=%d (%s)", errno, strerror(errno));
        }
    }
#endif

    if (ret == 0)
    {
        /* set to non-blocking */
        int flags = fcntl(receiver->controlSocket, F_GETFL, 0);
        fcntl(receiver->controlSocket, F_SETFL, flags | O_NONBLOCK);

        /* receive address */
        memset(&recvSin, 0, sizeof(struct sockaddr_in));
        recvSin.sin_family = AF_INET;
        recvSin.sin_port = htons(receiver->clientControlPort);
        recvSin.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if (ret == 0)
    {
        /* allow multiple sockets to use the same port */
        unsigned int yes = 1;
        err = ARSAL_Socket_Setsockopt(receiver->controlSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to set socket option SO_REUSEADDR: error=%d (%s)", errno, strerror(errno));
            ret = -1;
        }
    }

    if (ret == 0)
    {
        /* bind the socket */
        err = ARSAL_Socket_Bind(receiver->controlSocket, (struct sockaddr*)&recvSin, sizeof(recvSin));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Error on control socket bind port=%d: error=%d (%s)", receiver->clientControlPort, errno, strerror(errno));
            ret = -1;
        }
    }

    if (ret == 0)
    {
        /* send address */
        memset(&sendSin, 0, sizeof(struct sockaddr_in));
        sendSin.sin_family = AF_INET;
        sendSin.sin_port = htons(receiver->serverControlPort);
        err = inet_pton(AF_INET, receiver->serverAddr, &(sendSin.sin_addr));
        if (err <= 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to convert address '%s'", receiver->serverAddr);
            ret = -1;
        }
    }

    if (ret == 0)
    {
        /* connect the socket */
        err = ARSAL_Socket_Connect(receiver->controlSocket, (struct sockaddr*)&sendSin, sizeof(sendSin));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Error on control socket connect to addr='%s' port=%d: error=%d (%s)", receiver->serverAddr, receiver->serverControlPort, errno, strerror(errno));
            ret = -1;
        }
    }

    if (ret != 0)
    {
        if (receiver->controlSocket >= 0)
        {
            ARSAL_Socket_Close(receiver->controlSocket);
        }
        receiver->controlSocket = -1;
    }

    return ret;
}


static void ARSTREAM2_RtpReceiver_UpdateMonitoring(ARSTREAM2_RtpReceiver_t *receiver, uint32_t timestamp, uint16_t seqNum, uint16_t markerBit, uint32_t bytes)
{
    uint64_t curTime, timestampShifted;
    struct timespec t1;
    ARSAL_Time_GetTime(&t1);
    curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

    //TODO: handle the timestamp 32 bits loopback
    timestampShifted = (receiver->clockDelta != 0) ? (((((uint64_t)timestamp * 1000) + 45) / 90) - receiver->clockDelta) : 0; /* 90000 Hz clock to microseconds */

    ARSAL_Mutex_Lock(&(receiver->monitoringMutex));

    if (receiver->monitoringCount < ARSTREAM2_RTP_RECEIVER_MONITORING_MAX_POINTS)
    {
        receiver->monitoringCount++;
    }
    receiver->monitoringIndex = (receiver->monitoringIndex + 1) % ARSTREAM2_RTP_RECEIVER_MONITORING_MAX_POINTS;
    receiver->monitoringPoint[receiver->monitoringIndex].bytes = bytes;
    receiver->monitoringPoint[receiver->monitoringIndex].timestamp = timestamp;
    receiver->monitoringPoint[receiver->monitoringIndex].timestampShifted = timestampShifted;
    receiver->monitoringPoint[receiver->monitoringIndex].seqNum = seqNum;
    receiver->monitoringPoint[receiver->monitoringIndex].markerBit = markerBit;
    receiver->monitoringPoint[receiver->monitoringIndex].recvTimestamp = curTime;

    ARSAL_Mutex_Unlock(&(receiver->monitoringMutex));

#ifdef ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT
    if (receiver->fMonitorOut)
    {
        fprintf(receiver->fMonitorOut, "%llu %lu %llu %u %u %lu\n", (long long unsigned int)curTime, (long unsigned int)timestamp, (long long unsigned int)timestampShifted, seqNum, markerBit, (long unsigned int)bytes);
    }
#endif
}


static int ARSTREAM2_RtpReceiver_ReadData(ARSTREAM2_RtpReceiver_t *receiver, uint8_t *recvBuffer, int recvBufferSize, int *recvSize)
{
    int ret = 0, pollRet;
    ssize_t bytes;
    struct pollfd p;

    if ((!recvBuffer) || (!recvSize))
    {
        return -1;
    }

    bytes = ARSAL_Socket_Recv(receiver->streamSocket, recvBuffer, recvBufferSize, 0);
    if (bytes < 0)
    {
        /* socket receive failed */
        switch (errno)
        {
            case EAGAIN:
                /* poll */
                p.fd = receiver->streamSocket;
                p.events = POLLIN;
                p.revents = 0;
                pollRet = poll(&p, 1, ARSTREAM2_RTP_RECEIVER_STREAM_DATAREAD_TIMEOUT_MS);
                if (pollRet == 0)
                {
                    /* failed: poll timeout */
                    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Polling timed out");
                    ret = -2;
                    *recvSize = 0;
                }
                else if (pollRet < 0)
                {
                    /* failed: poll error */
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Poll error: error=%d (%s)", errno, strerror(errno));
                    ret = -1;
                    *recvSize = 0;
                }
                else if (p.revents & POLLIN)
                {
                    bytes = ARSAL_Socket_Recv(receiver->streamSocket, recvBuffer, recvBufferSize, 0);
                    if (bytes >= 0)
                    {
                        /* success: save the number of bytes read */
                        *recvSize = bytes;
                    }
                    else if (errno == EAGAIN)
                    {
                        /* failed: socket not ready (this should not happen) */
                        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTP_RECEIVER_TAG, "Socket not ready for reading");
                        ret = -2;
                        *recvSize = 0;
                    }
                    else
                    {
                        /* failed: socket error */
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Socket receive error #2 %d ('%s')", errno, strerror(errno));
                        ret = -1;
                        *recvSize = 0;
                    }
                }
                else
                {
                    /* no poll error, no timeout, but socket is not ready */
                    int error = 0;
                    socklen_t errlen = sizeof(error);
                    ARSAL_Socket_Getsockopt(receiver->streamSocket, SOL_SOCKET, SO_ERROR, (void *)&error, &errlen);
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "No poll error, no timeout, but socket is not ready (revents = %d, error = %d)", p.revents, error);
                    ret = -1;
                    *recvSize = 0;
                }
                break;
            default:
                /* failed: socket error */
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Socket receive error %d ('%s')", errno, strerror(errno));
                ret = -1;
                *recvSize = 0;
                break;
        }
    }
    else
    {
        /* success: save the number of bytes read */
        *recvSize = bytes;
    }

    return ret;
}


static int ARSTREAM2_RtpReceiver_CheckBufferSize(ARSTREAM2_RtpReceiver_t *receiver, int payloadSize)
{
    int ret = 0;

    if ((receiver->currentNaluBuffer == NULL) || (receiver->currentNaluSize + payloadSize > receiver->currentNaluBufferSize))
    {
        int32_t nextNaluBufferSize = receiver->currentNaluSize + payloadSize, dummy = 0;
        uint8_t *nextNaluBuffer = receiver->naluCallback(ARSTREAM2_RTP_RECEIVER_CAUSE_NALU_BUFFER_TOO_SMALL, receiver->currentNaluBuffer, 0, 0, 0, 0, 0, 0, &nextNaluBufferSize, receiver->naluCallbackUserPtr);
        ret = -1;
        if ((nextNaluBuffer != NULL) && (nextNaluBufferSize > 0) && (nextNaluBufferSize >= receiver->currentNaluSize + payloadSize))
        {
            if ((receiver->currentNaluBuffer != NULL) && (receiver->currentNaluSize != 0))
            {
                memcpy(nextNaluBuffer, receiver->currentNaluBuffer, receiver->currentNaluSize);
            }
            receiver->naluCallback(ARSTREAM2_RTP_RECEIVER_CAUSE_NALU_COPY_COMPLETE, receiver->currentNaluBuffer, 0, 0, 0, 0, 0, 0, &dummy, receiver->naluCallbackUserPtr);
            ret = 0;
        }
        receiver->currentNaluBuffer = nextNaluBuffer;
        receiver->currentNaluBufferSize = nextNaluBufferSize;
    }

    return ret;
}


static void ARSTREAM2_RtpReceiver_OutputNalu(ARSTREAM2_RtpReceiver_t *receiver, uint32_t timestamp, int isFirstNaluInAu, int isLastNaluInAu, int missingPacketsBefore)
{
    uint64_t timestampScaled, timestampScaledShifted;

    timestampScaled = ((((uint64_t)timestamp * 1000) + 45) / 90); /* 90000 Hz clock to microseconds */
    //TODO: handle the timestamp 32 bits loopback
    timestampScaledShifted = (receiver->clockDelta != 0) ? (timestampScaled - receiver->clockDelta) : 0; /* 90000 Hz clock to microseconds */

    if (receiver->resenderCount > 0)
    {
        ARSTREAM2_RtpReceiver_ResendNalu(receiver, receiver->currentNaluBuffer, receiver->currentNaluSize, timestampScaled, isLastNaluInAu, missingPacketsBefore);
    }

    receiver->currentNaluBuffer = receiver->naluCallback(ARSTREAM2_RTP_RECEIVER_CAUSE_NALU_COMPLETE, receiver->currentNaluBuffer, receiver->currentNaluSize,
                                                     timestampScaled, timestampScaledShifted,
                                                     isFirstNaluInAu, isLastNaluInAu, missingPacketsBefore, &(receiver->currentNaluBufferSize), receiver->naluCallbackUserPtr);
}


void* ARSTREAM2_RtpReceiver_RunStreamThread(void *ARSTREAM2_RtpReceiver_t_Param)
{
    ARSTREAM2_RtpReceiver_t *receiver = (ARSTREAM2_RtpReceiver_t *)ARSTREAM2_RtpReceiver_t_Param;
    uint8_t *recvBuffer = NULL;
    int recvBufferSize;
    int recvSize, payloadSize;
    int fuPending = 0, currentAuSize = 0;
    ARSTREAM2_RTP_Header_t *header = NULL;
    uint32_t rtpTimestamp = 0, previousTimestamp = 0;
    uint16_t currentFlags;
    int auStartSeqNum = -1, naluStartSeqNum = -1, previousSeqNum = -1, currentSeqNum, seqNumDelta;
    int gapsInSeqNum = 0, gapsInSeqNumAu = 0, uncertainAuChange = 0;
    uint32_t startCode = 0;
    int startCodeLength = 0;
    int shouldStop;
    int ret;

    /* Parameters check */
    if (receiver == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Error while starting %s, bad parameters", __FUNCTION__);
        return (void *)0;
    }

    recvBufferSize = receiver->maxPacketSize + sizeof(ARSTREAM2_RTP_Header_t);
    if (receiver->insertStartCodes)
    {
        startCode = htonl(ARSTREAM2_H264_BYTE_STREAM_NALU_START_CODE);
        startCodeLength = ARSTREAM2_H264_BYTE_STREAM_NALU_START_CODE_LENGTH;
    }

    /* Alloc and check */
    recvBuffer = malloc(recvBufferSize);
    if (recvBuffer == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Error while starting %s, cannot allocate memory", __FUNCTION__);
        return (void *)0;
    }
    header = (ARSTREAM2_RTP_Header_t*)recvBuffer;

    /* Bind */
    ret = ARSTREAM2_RtpReceiver_StreamSocketSetup(receiver);
    if (ret != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to setup the stream socket (error %d) - aborting", ret);
        free(recvBuffer);
        return (void*)0;
    }

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "RtpReceiver stream thread running");
    ARSAL_Mutex_Lock(&(receiver->streamMutex));
    receiver->streamThreadStarted = 1;
    shouldStop = receiver->threadsShouldStop;
    ARSAL_Mutex_Unlock(&(receiver->streamMutex));

    while (shouldStop == 0)
    {
        ret = ARSTREAM2_RtpReceiver_ReadData(receiver, recvBuffer, recvBufferSize, &recvSize);
        if (ret != 0)
        {
            if (ret != -2)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to read data (%d)", ret);
            }
        }
        else if (recvSize >= sizeof(ARSTREAM2_RTP_Header_t))
        {
            rtpTimestamp = ntohl(header->timestamp);
            currentSeqNum = (int)ntohs(header->seqNum);
            currentFlags = ntohs(header->flags);
            ARSTREAM2_RtpReceiver_UpdateMonitoring(receiver, rtpTimestamp, currentSeqNum, (currentFlags & (1 << 7)) ? 1 : 0, (uint32_t)recvSize);

            if (previousSeqNum != -1)
            {
                seqNumDelta = currentSeqNum - previousSeqNum;
                if (seqNumDelta < -32768)
                {
                    seqNumDelta += 65536; /* handle seqNum 16 bits loopback */
                }
                if (seqNumDelta > 0)
                {
                    gapsInSeqNum += seqNumDelta - 1;
                    gapsInSeqNumAu += seqNumDelta - 1;
                }
            }
            else
            {
                seqNumDelta = 1;
            }

            if (seqNumDelta > 0)
            {
                if ((previousTimestamp != 0) && (rtpTimestamp != previousTimestamp))
                {
                    if (gapsInSeqNumAu)
                    {
                        /*ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Incomplete access unit before seqNum %d, size %d bytes (missing %d of %d packets)", 
                                    currentSeqNum, currentAuSize, gapsInSeqNumAu, currentSeqNum - auStartSeqNum + 1);*/
                    }
                    if ((currentAuSize != 0) || (gapsInSeqNum != 0))
                    {
                        uncertainAuChange = 1;
                    }
                    gapsInSeqNumAu = 0;
                    currentAuSize = 0;
                }

                if (currentAuSize == 0)
                {
                    auStartSeqNum = currentSeqNum;
                }

                payloadSize = recvSize - sizeof(ARSTREAM2_RTP_Header_t);

                if (payloadSize >= 1)
                {
                    uint8_t headByte = *(recvBuffer + sizeof(ARSTREAM2_RTP_Header_t));

                    if ((headByte & 0x1F) == ARSTREAM2_RTP_NALU_TYPE_FUA)
                    {
                        /* Fragmentation (FU-A) */
                        if (payloadSize >= 2)
                        {
                            uint8_t fuIndicator, fuHeader, startBit, endBit;
                            int outputSize = payloadSize - 2;
                            fuIndicator = headByte;
                            fuHeader = *(recvBuffer + sizeof(ARSTREAM2_RTP_Header_t) + 1);
                            startBit = fuHeader & 0x80;
                            endBit = fuHeader & 0x40;

                            if ((fuPending) && (startBit))
                            {
                                //TODO: drop the previous incomplete FU-A?
                                fuPending = 0;
                                //ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Incomplete FU-A packet before FU-A at seqNum %d ((fuPending) && (startBit))", currentSeqNum);
                            }
                            if (startBit)
                            {
                                fuPending = 1;
                                receiver->currentNaluSize = 0;
                                naluStartSeqNum = currentSeqNum;
                            }
                            if (fuPending)
                            {
                                outputSize += (startBit) ? startCodeLength + 1 : 0;
                                if (!ARSTREAM2_RtpReceiver_CheckBufferSize(receiver, outputSize))
                                {
                                    if ((startCodeLength > 0) && (startBit))
                                    {
                                        memcpy(receiver->currentNaluBuffer + receiver->currentNaluSize, &startCode, startCodeLength);
                                        receiver->currentNaluSize += startCodeLength;
                                        currentAuSize += startCodeLength;
                                    }
                                    memcpy(receiver->currentNaluBuffer + receiver->currentNaluSize + ((startBit) ? 1 : 0), recvBuffer + sizeof(ARSTREAM2_RTP_Header_t) + 2, payloadSize - 2);
                                    if (startBit)
                                    {
                                        /* restore the NALU header byte */
                                        *(receiver->currentNaluBuffer + receiver->currentNaluSize) = (fuIndicator & 0xE0) | (fuHeader & 0x1F);
                                        receiver->currentNaluSize++;
                                        currentAuSize++;
                                    }
                                    receiver->currentNaluSize += payloadSize - 2;
                                    currentAuSize += payloadSize - 2;
                                    if (endBit)
                                    {
                                        int isFirst = 0, isLast = 0;
                                        if ((!uncertainAuChange) && (auStartSeqNum == currentSeqNum))
                                        {
                                            isFirst = 1;
                                        }
                                        if (currentFlags & (1 << 7))
                                        {
                                            isLast = 1;
                                        }
                                        /*ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Output FU-A NALU (seqNum %d->%d) isFirst=%d isLast=%d gapsInSeqNum=%d",
                                                    naluStartSeqNum, currentSeqNum, isFirst, isLast, gapsInSeqNum);*/ //TODO debug
                                        ARSTREAM2_RtpReceiver_OutputNalu(receiver, rtpTimestamp, isFirst, isLast, gapsInSeqNum);
                                        gapsInSeqNum = 0;
                                    }
                                }
                                else
                                {
                                    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to get a larger buffer (output size %d) for FU-A NALU at seqNum %d", outputSize, currentSeqNum);
                                }
                            }
                            if (endBit)
                            {
                                fuPending = 0;
                            }
                        }
                        else
                        {
                            ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Invalid payload size (%d) for FU-A packet at seqNum %d", payloadSize, currentSeqNum);
                        }
                    }
                    else if ((headByte & 0x1F) == ARSTREAM2_RTP_NALU_TYPE_STAPA)
                    {
                        /* Aggregation (STAP-A) */
                        if (fuPending)
                        {
                            //TODO: drop the previous incomplete FU-A?
                            fuPending = 0;
                            //ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Incomplete FU-A packet before STAP-A at seqNum %d (fuPending)", currentSeqNum);
                        }

                        if (payloadSize >= 3)
                        {
                            uint8_t *curBuf = recvBuffer + sizeof(ARSTREAM2_RTP_Header_t) + 1;
                            int sizeLeft = payloadSize - 1, naluCount = 0;
                            uint16_t naluSize = ((uint16_t)(*curBuf) << 8) | ((uint16_t)(*(curBuf + 1))), nextNaluSize = 0;
                            curBuf += 2;
                            sizeLeft -= 2;
                            while ((naluSize > 0) && (sizeLeft >= naluSize))
                            {
                                naluCount++;
                                nextNaluSize = (sizeLeft >= naluSize + 2) ? ((uint16_t)(*(curBuf + naluSize)) << 8) | ((uint16_t)(*(curBuf + naluSize + 1))) : 0;
                                receiver->currentNaluSize = 0;
                                if (!ARSTREAM2_RtpReceiver_CheckBufferSize(receiver, naluSize + startCodeLength))
                                {
                                    int isFirst = 0, isLast = 0;
                                    if ((!uncertainAuChange) && (auStartSeqNum == currentSeqNum) && (naluCount == 1))
                                    {
                                        isFirst = 1;
                                    }
                                    if ((currentFlags & (1 << 7)) && (nextNaluSize == 0))
                                    {
                                        isLast = 1;
                                    }
                                    if (startCodeLength > 0)
                                    {
                                        memcpy(receiver->currentNaluBuffer, &startCode, startCodeLength);
                                        receiver->currentNaluSize += startCodeLength;
                                        currentAuSize += startCodeLength;
                                    }
                                    memcpy(receiver->currentNaluBuffer + receiver->currentNaluSize, curBuf, naluSize);
                                    receiver->currentNaluSize += naluSize;
                                    currentAuSize += naluSize;
                                    /*ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Output STAP-A NALU (seqNum %d) isFirst=%d isLast=%d gapsInSeqNum=%d",
                                                currentSeqNum, isFirst, isLast, gapsInSeqNum);*/ //TODO debug
                                    ARSTREAM2_RtpReceiver_OutputNalu(receiver, rtpTimestamp, isFirst, isLast, gapsInSeqNum);
                                    gapsInSeqNum = 0;
                                }
                                else
                                {
                                    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to get a larger buffer (output size %d) for STAP-A NALU at seqNum %d", naluSize + startCodeLength, currentSeqNum);
                                }
                                curBuf += naluSize;
                                sizeLeft -= naluSize;
                                curBuf += 2;
                                sizeLeft -= 2;
                                naluSize = nextNaluSize;
                            }
                        }
                    }
                    else
                    {
                        /* Single NAL unit */
                        if (fuPending)
                        {
                            //TODO: drop the previous incomplete FU-A?
                            fuPending = 0;
                            //ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Incomplete FU-A packet before single NALU at seqNum %d (fuPending)", currentSeqNum);
                        }

                        receiver->currentNaluSize = 0;
                        if (!ARSTREAM2_RtpReceiver_CheckBufferSize(receiver, payloadSize + startCodeLength))
                        {
                            int isFirst = 0, isLast = 0;
                            if ((!uncertainAuChange) && (auStartSeqNum == currentSeqNum))
                            {
                                isFirst = 1;
                            }
                            if (currentFlags & (1 << 7))
                            {
                                isLast = 1;
                            }
                            if (startCodeLength > 0)
                            {
                                memcpy(receiver->currentNaluBuffer, &startCode, startCodeLength);
                                receiver->currentNaluSize += startCodeLength;
                                currentAuSize += startCodeLength;
                            }
                            memcpy(receiver->currentNaluBuffer + receiver->currentNaluSize, recvBuffer + sizeof(ARSTREAM2_RTP_Header_t), payloadSize);
                            receiver->currentNaluSize += payloadSize;
                            currentAuSize += payloadSize;
                            /*ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Output single NALU (seqNum %d) isFirst=%d isLast=%d gapsInSeqNum=%d",
                                        currentSeqNum, isFirst, isLast, gapsInSeqNum);*/ //TODO debug
                            ARSTREAM2_RtpReceiver_OutputNalu(receiver, rtpTimestamp, isFirst, isLast, gapsInSeqNum);
                            gapsInSeqNum = 0;
                        }
                        else
                        {
                            ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to get a larger buffer (output size %d) for single NALU at seqNum %d", payloadSize + startCodeLength, currentSeqNum);
                        }
                    }
                }
                else
                {
                    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Invalid payload size (%d) for packet at seqNum %d", payloadSize, currentSeqNum);
                }

                if (currentFlags & (1 << 7))
                {
                    /* the marker bit is set: complete access unit */
                    //ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Complete access unit at seqNum %d, size %d bytes (missing %d of %d packets)", currentSeqNum, currentAuSize, gapsInSeqNumAu, currentSeqNum - auStartSeqNum + 1);
                    uncertainAuChange = 0;
                    gapsInSeqNumAu = 0;
                    currentAuSize = 0;
                }

                previousSeqNum = currentSeqNum;
                previousTimestamp = rtpTimestamp;
            }
            else
            {
                /* out of order packet */
                //TODO
                ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTP_RECEIVER_TAG, "Out of order sequence number (currentSeqNum=%d, previousSeqNum=%d, seqNumDelta=%d)", currentSeqNum, previousSeqNum, seqNumDelta); //TODO: debug
            }
        }

        ARSAL_Mutex_Lock(&(receiver->streamMutex));
        shouldStop = receiver->threadsShouldStop;
        if (receiver->scheduleNaluBufferChange)
        {
            receiver->currentNaluBuffer = NULL;
            receiver->currentNaluBufferSize = 0;
            receiver->scheduleNaluBufferChange = 0;
            ARSAL_Mutex_Unlock(&(receiver->streamMutex));
            ARSAL_Cond_Signal(&(receiver->streamCond));
        }
        else
        {
            ARSAL_Mutex_Unlock(&(receiver->streamMutex));
        }
    }

    receiver->naluCallback(ARSTREAM2_RTP_RECEIVER_CAUSE_CANCEL, receiver->currentNaluBuffer, 0, 0, 0, 0, 0, 0, &(receiver->currentNaluBufferSize), receiver->naluCallbackUserPtr);

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "RtpReceiver stream thread ended");
    ARSAL_Mutex_Lock(&(receiver->streamMutex));
    receiver->streamThreadStarted = 0;
    ARSAL_Mutex_Unlock(&(receiver->streamMutex));

    if (recvBuffer)
    {
        free(recvBuffer);
        recvBuffer = NULL;
    }

    return (void *)0;
}


void* ARSTREAM2_RtpReceiver_RunControlThread(void *ARSTREAM2_RtpReceiver_t_Param)
{
    ARSTREAM2_RtpReceiver_t *receiver = (ARSTREAM2_RtpReceiver_t *)ARSTREAM2_RtpReceiver_t_Param;
    uint8_t *msgBuffer;
    int msgBufferSize = sizeof(ARSTREAM2_RTP_ClockFrame_t);
    ARSTREAM2_RTP_ClockFrame_t *clockFrame;
    uint64_t originateTimestamp = 0;
    uint64_t receiveTimestamp = 0;
    uint64_t transmitTimestamp = 0;
    uint64_t receiveTimestamp2 = 0;
    uint64_t originateTimestamp2 = 0;
    uint64_t loopStartTime = 0;
    int64_t clockDelta = 0;
    int64_t rtDelay = 0;
    uint32_t tsH, tsL;
    ssize_t bytes;
    struct timespec t1;
    struct pollfd p;
    int shouldStop, ret, pollRet, timeElapsed, sleepDuration;

    /* Parameters check */
    if (receiver == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Error while starting %s, bad parameters", __FUNCTION__);
        return (void *)0;
    }

    /* Alloc and check */
    msgBuffer = malloc(msgBufferSize);
    if (msgBuffer == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Error while starting %s, cannot allocate memory", __FUNCTION__);
        return (void *)0;
    }
    clockFrame = (ARSTREAM2_RTP_ClockFrame_t*)msgBuffer;

    /* Socket setup */
    ret = ARSTREAM2_RtpReceiver_ControlSocketSetup(receiver);
    if (ret != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to setup the control socket (error %d) - aborting", ret);
        free(msgBuffer);
        return (void*)0;
    }

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "RtpReceiver control thread running");
    ARSAL_Mutex_Lock(&(receiver->streamMutex));
    receiver->controlThreadStarted = 1;
    shouldStop = receiver->threadsShouldStop;
    ARSAL_Mutex_Unlock(&(receiver->streamMutex));

    while (shouldStop == 0)
    {
        ARSAL_Time_GetTime(&t1);
        loopStartTime = transmitTimestamp = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

        memset(clockFrame, 0, sizeof(ARSTREAM2_RTP_ClockFrame_t));

        tsH = (uint32_t)(transmitTimestamp >> 32);
        tsL = (uint32_t)(transmitTimestamp & 0xFFFFFFFF);
        clockFrame->transmitTimestampH = htonl(tsH);
        clockFrame->transmitTimestampL = htonl(tsL);

        bytes = ARSAL_Socket_Send(receiver->controlSocket, msgBuffer, msgBufferSize, 0);
        if (bytes < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Socket send error: error=%d (%s)", errno, strerror(errno));
        }
        else
        {
            originateTimestamp2 = transmitTimestamp;

            /* poll */
            p.fd = receiver->controlSocket;
            p.events = POLLIN;
            p.revents = 0;
            pollRet = poll(&p, 1, ARSTREAM2_RTP_RECEIVER_CLOCKSYNC_DATAREAD_TIMEOUT_MS);
            if (pollRet == 0)
            {
                /* failed: poll timeout */
                ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Polling timed out");
            }
            else if (pollRet < 0)
            {
                /* failed: poll error */
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Poll error: error=%d (%s)", errno, strerror(errno));
            }
            else if (p.revents & POLLIN)
            {
                do
                {
                    bytes = ARSAL_Socket_Recv(receiver->controlSocket, msgBuffer, msgBufferSize, 0);
                    if (bytes >= 0)
                    {
                        /* success */
                        ARSAL_Time_GetTime(&t1);
                        receiveTimestamp2 = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;
                        originateTimestamp = ((uint64_t)ntohl(clockFrame->originateTimestampH) << 32) + (uint64_t)ntohl(clockFrame->originateTimestampL);
                        receiveTimestamp = ((uint64_t)ntohl(clockFrame->receiveTimestampH) << 32) + (uint64_t)ntohl(clockFrame->receiveTimestampL);
                        transmitTimestamp = ((uint64_t)ntohl(clockFrame->transmitTimestampH) << 32) + (uint64_t)ntohl(clockFrame->transmitTimestampL);

                        if (originateTimestamp == originateTimestamp2)
                        {
                            clockDelta = (int64_t)(receiveTimestamp + transmitTimestamp) / 2 - (int64_t)(originateTimestamp + receiveTimestamp2) / 2;
                            receiver->clockDelta = clockDelta;
                            rtDelay = (receiveTimestamp2 - originateTimestamp) - (transmitTimestamp - receiveTimestamp);

                            /*ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTP_RECEIVER_TAG, "Clock - originateTS: %llu | receiveTS: %llu | transmitTS: %llu | receiveTS2: %llu | delta: %lld | rtDelay: %lld",
                                        (long long unsigned int)originateTimestamp, (long long unsigned int)receiveTimestamp, (long long unsigned int)transmitTimestamp, (long long unsigned int)receiveTimestamp2,
                                        (long long int)clockDelta, (long long int)rtDelay);*/ //TODO: debug
                        }
                        else
                        {
                            /*ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Originate timestamp missmatch (%llu vs. %llu)", (long long unsigned int)originateTimestamp2, (long long unsigned int)originateTimestamp);*/ //TODO: debug
                        }
                    }
                    else if (errno != EAGAIN)
                    {
                        /* failed: socket error */
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Socket receive error %d ('%s')", errno, strerror(errno));
                    }
                }
                while (bytes >= 0);
            }
            else
            {
                /* no poll error, no timeout, but socket is not ready */
                int error = 0;
                socklen_t errlen = sizeof(error);
                ARSAL_Socket_Getsockopt(receiver->controlSocket, SOL_SOCKET, SO_ERROR, (void *)&error, &errlen);
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "No poll error, no timeout, but socket is not ready (revents = %d, error = %d)", p.revents, error);
            }
        }

        ARSAL_Time_GetTime(&t1);
        timeElapsed = (int)((uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000 - loopStartTime);

        sleepDuration = ARSTREAM2_RTP_RECEIVER_CLOCKSYNC_PERIOD_MS * 1000 - timeElapsed;
        if (sleepDuration > 0)
        {
            usleep(sleepDuration);
        }

        ARSAL_Mutex_Lock(&(receiver->streamMutex));
        shouldStop = receiver->threadsShouldStop;
        ARSAL_Mutex_Unlock(&(receiver->streamMutex));
    }

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "RtpReceiver control thread ended");
    ARSAL_Mutex_Lock(&(receiver->streamMutex));
    receiver->controlThreadStarted = 0;
    ARSAL_Mutex_Unlock(&(receiver->streamMutex));

    if (msgBuffer)
    {
        free(msgBuffer);
        msgBuffer = NULL;
    }

    return (void *)0;
}


void* ARSTREAM2_RtpReceiver_GetNaluCallbackUserPtr(ARSTREAM2_RtpReceiver_t *receiver)
{
    void *ret = NULL;
    if (receiver != NULL)
    {
        ret = receiver->naluCallbackUserPtr;
    }
    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_RtpReceiver_GetMonitoring(ARSTREAM2_RtpReceiver_t *receiver, uint64_t startTime, uint32_t timeIntervalUs, uint32_t *realTimeIntervalUs, uint32_t *receptionTimeJitter,
                                               uint32_t *bytesReceived, uint32_t *meanPacketSize, uint32_t *packetSizeStdDev, uint32_t *packetsReceived, uint32_t *packetsMissed)
{
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;
    uint64_t endTime, curTime, previousTime, auTimestamp, receptionTimeSum = 0, receptionTimeVarSum = 0, packetSizeVarSum = 0;
    uint32_t bytes, bytesSum = 0, _meanPacketSize = 0, receptionTime = 0, meanReceptionTime = 0, _receptionTimeJitter = 0, _packetSizeStdDev = 0;
    int currentSeqNum, previousSeqNum = -1, seqNumDelta, gapsInSeqNum;
    int points = 0, usefulPoints = 0, idx, i, firstUsefulIdx = -1;

    if ((receiver == NULL) || (timeIntervalUs == 0))
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

    ARSAL_Mutex_Lock(&(receiver->monitoringMutex));

    if (receiver->monitoringCount > 0)
    {
        idx = receiver->monitoringIndex;
        previousTime = startTime;

        while (points < receiver->monitoringCount)
        {
            curTime = receiver->monitoringPoint[idx].recvTimestamp;
            if (curTime > startTime)
            {
                points++;
                idx = (idx - 1 >= 0) ? idx - 1 : ARSTREAM2_RTP_RECEIVER_MONITORING_MAX_POINTS - 1;
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
            idx = (idx - 1 >= 0) ? idx - 1 : ARSTREAM2_RTP_RECEIVER_MONITORING_MAX_POINTS - 1;
            curTime = receiver->monitoringPoint[idx].recvTimestamp;
            bytes = receiver->monitoringPoint[idx].bytes;
            bytesSum += bytes;
            auTimestamp = ((((uint64_t)(receiver->monitoringPoint[idx].timestamp /*TODO*/) * 1000) + 45) / 90) - receiver->clockDelta; /* 90000 Hz clock to microseconds */
            receptionTime = curTime - auTimestamp;
            receptionTimeSum += receptionTime;
            currentSeqNum = receiver->monitoringPoint[idx].seqNum;
            seqNumDelta = (previousSeqNum != -1) ? (previousSeqNum - currentSeqNum) : 1;
            if (seqNumDelta < -32768) seqNumDelta += 65536; /* handle seqNum 16 bits loopback */
            gapsInSeqNum += seqNumDelta - 1;
            previousSeqNum = currentSeqNum;
            previousTime = curTime;
            usefulPoints++;
            points++;
            idx = (idx - 1 >= 0) ? idx - 1 : ARSTREAM2_RTP_RECEIVER_MONITORING_MAX_POINTS - 1;
        }

        endTime = previousTime;
        _meanPacketSize = (usefulPoints) ? (bytesSum / usefulPoints) : 0;
        meanReceptionTime = (usefulPoints) ? (uint32_t)(receptionTimeSum / usefulPoints) : 0;

        if ((receptionTimeJitter) || (packetSizeStdDev))
        {
            for (i = 0, idx = firstUsefulIdx; i < usefulPoints; i++)
            {
                idx = (idx - 1 >= 0) ? idx - 1 : ARSTREAM2_RTP_RECEIVER_MONITORING_MAX_POINTS - 1;
                curTime = receiver->monitoringPoint[idx].recvTimestamp;
                bytes = receiver->monitoringPoint[idx].bytes;
                auTimestamp = ((((uint64_t)(receiver->monitoringPoint[idx].timestamp /*TODO*/) * 1000) + 45) / 90) - receiver->clockDelta; /* 90000 Hz clock to microseconds */
                receptionTime = curTime - auTimestamp;
                packetSizeVarSum += ((bytes - _meanPacketSize) * (bytes - _meanPacketSize));
                receptionTimeVarSum += ((receptionTime - meanReceptionTime) * (receptionTime - meanReceptionTime));
            }
            _receptionTimeJitter = (usefulPoints) ? (uint32_t)(sqrt((double)receptionTimeVarSum / usefulPoints)) : 0;
            _packetSizeStdDev = (usefulPoints) ? (uint32_t)(sqrt((double)packetSizeVarSum / usefulPoints)) : 0;
        }
    }

    ARSAL_Mutex_Unlock(&(receiver->monitoringMutex));

    if (realTimeIntervalUs)
    {
        *realTimeIntervalUs = (startTime - endTime);
    }
    if (receptionTimeJitter)
    {
        *receptionTimeJitter = _receptionTimeJitter;
    }
    if (bytesReceived)
    {
        *bytesReceived = bytesSum;
    }
    if (meanPacketSize)
    {
        *meanPacketSize = _meanPacketSize;
    }
    if (packetSizeStdDev)
    {
        *packetSizeStdDev = _packetSizeStdDev;
    }
    if (packetsReceived)
    {
        *packetsReceived = usefulPoints;
    }
    if (packetsMissed)
    {
        *packetsMissed = gapsInSeqNum;
    }

    return ret;
}


ARSTREAM2_RtpReceiver_RtpResender_t* ARSTREAM2_RtpReceiver_RtpResender_New(ARSTREAM2_RtpReceiver_t *receiver, ARSTREAM2_RtpReceiver_RtpResender_Config_t *config, eARSTREAM2_ERROR *error)
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
        senderConfig.naluCallback = ARSTREAM2_RtpReceiver_RtpResender_NaluCallback;
        senderConfig.naluCallbackUserPtr = (void*)retResender;
        senderConfig.naluFifoSize = ARSTREAM2_RTP_RECEIVER_RTP_RESENDER_MAX_NALU_BUFFER_COUNT;
        senderConfig.maxPacketSize = config->maxPacketSize;
        senderConfig.targetPacketSize = config->targetPacketSize;
        senderConfig.maxBitrate = receiver->maxBitrate;
        senderConfig.maxLatencyMs = config->maxLatencyMs;
        senderConfig.maxNetworkLatencyMs = config->maxNetworkLatencyMs;
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


void ARSTREAM2_RtpReceiver_RtpResender_Stop(ARSTREAM2_RtpReceiver_RtpResender_t *resender)
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


eARSTREAM2_ERROR ARSTREAM2_RtpReceiver_RtpResender_Delete(ARSTREAM2_RtpReceiver_RtpResender_t **resender)
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
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to find the resender");
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
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "RtpResender: failed to delete Sender (%d)", retVal);
                }
            }
            else
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "RtpResender: sender is still running");
            }

            ARSAL_Mutex_Unlock(&(receiver->resenderMutex));
        }
    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Invalid resender");
    }

    return retVal;
}


void* ARSTREAM2_RtpReceiver_RtpResender_RunStreamThread (void *ARSTREAM2_RtpReceiver_RtpResender_t_Param)
{
    ARSTREAM2_RtpReceiver_RtpResender_t *resender = (ARSTREAM2_RtpReceiver_RtpResender_t *)ARSTREAM2_RtpReceiver_RtpResender_t_Param;

    if (resender == NULL)
    {
        return (void *)0;
    }

    return ARSTREAM2_RtpSender_RunStreamThread((void *)resender->sender);
}


void* ARSTREAM2_RtpReceiver_RtpResender_RunControlThread (void *ARSTREAM2_RtpReceiver_RtpResender_t_Param)
{
    ARSTREAM2_RtpReceiver_RtpResender_t *resender = (ARSTREAM2_RtpReceiver_RtpResender_t *)ARSTREAM2_RtpReceiver_RtpResender_t_Param;

    if (resender == NULL)
    {
        return (void *)0;
    }

    return ARSTREAM2_RtpSender_RunControlThread((void *)resender->sender);
}
