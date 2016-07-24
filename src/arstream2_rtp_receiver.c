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
#define __USE_GNU
#include <sys/socket.h>
#undef __USE_GNU
#define ARSTREAM2_HAS_MMSG //TODO
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <math.h>

#include <libARStream2/arstream2_rtp_receiver.h>
#include <libARStream2/arstream2_rtp_sender.h>
#include "arstream2_rtp.h"
#include "arstream2_rtp_h264.h"
#include "arstream2_rtcp.h"
#include "arstream2_h264.h"

#include <libARSAL/ARSAL_Print.h>
#include <libARSAL/ARSAL_Mutex.h>

#if BUILD_LIBMUX
#include <libmux.h>
#include <libmux-arsdk.h>
#include <libpomp.h>
#endif


#define ARSTREAM2_RTP_RECEIVER_TAG "ARSTREAM2_RtpReceiver"

#define ARSTREAM2_RTP_RECEIVER_TIMEOUT_US (100 * 1000)

#define ARSTREAM2_RTP_RECEIVER_STREAM_DATAREAD_TIMEOUT_MS (500)
#define ARSTREAM2_RTP_RECEIVER_CONTROL_DATAREAD_TIMEOUT_MS (500)

#define ARSTREAM2_RTP_RECEIVER_DEFAULT_PACKET_FIFO_SIZE (500)
#define ARSTREAM2_RTP_RECEIVER_DEFAULT_NALU_FIFO_SIZE (3000)
#define ARSTREAM2_RTP_RECEIVER_DEFAULT_AU_FIFO_SIZE (60)

#define ARSTREAM2_RTP_RECEIVER_AU_BUFFER_SIZE (128 * 1024)
#define ARSTREAM2_RTP_RECEIVER_AU_METADATA_BUFFER_SIZE (1024)
#define ARSTREAM2_RTP_RECEIVER_AU_USER_DATA_BUFFER_SIZE (1024)

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


#ifndef ARSTREAM2_HAS_MMSG
struct mmsghdr {
    struct msghdr msg_hdr;  /* Message header */
    unsigned int  msg_len;  /* Number of received bytes for header */
};
#endif


typedef struct ARSTREAM2_RtpReceiver_MonitoringPoint_s {
    uint64_t recvTimestamp;
    uint64_t ntpTimestamp;
    uint64_t ntpTimestampLocal;
    uint32_t rtpTimestamp;
    uint16_t seqNum;
    uint16_t markerBit;
    uint32_t bytes;
} ARSTREAM2_RtpReceiver_MonitoringPoint_t;


struct ARSTREAM2_RtpReceiver_RtpResender_t {
    ARSTREAM2_RtpReceiver_t *receiver;
    ARSTREAM2_RtpSender_t *sender;
    int useRtpHeaderExtensions;
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

struct ARSTREAM2_RtpReceiver_NetInfos_t {
    char *serverAddr;
    char *mcastAddr;
    char *mcastIfaceAddr;
    int serverStreamPort;
    int serverControlPort;
    int clientStreamPort;
    int clientControlPort;

    /* Sockets */
    int isMulticast;
    int streamSocket;
    int controlSocket;
};

struct ARSTREAM2_RtpReceiver_MuxInfos_t {
    struct mux_ctx *mux;
    struct mux_queue *control;
    struct mux_queue *data;
};

struct ARSTREAM2_RtpReceiver_Ops_t {
    /* Stream channel */
    int (*streamChannelSetup)(ARSTREAM2_RtpReceiver_t *);
    int (*streamChannelTeardown)(ARSTREAM2_RtpReceiver_t *);
    int (*streamChannelRead)(ARSTREAM2_RtpReceiver_t *,
                             uint8_t *,
                             int,
                             int *);
    int (*streamChannelRecvMmsg)(ARSTREAM2_RtpReceiver_t *,
                                 struct mmsghdr *,
                                 unsigned int);


    /* Control channel */
    int (*controlChannelSetup)(ARSTREAM2_RtpReceiver_t *);
    int (*controlChannelTeardown)(ARSTREAM2_RtpReceiver_t *);
    int (*controlChannelSend)(ARSTREAM2_RtpReceiver_t *,
                              uint8_t *,
                              int);
    int (*controlChannelRead)(ARSTREAM2_RtpReceiver_t *,
                              uint8_t *,
                              int,
                              int);
};

struct ARSTREAM2_RtpReceiver_t {
    /* Configuration on New */
    int useMux;
    struct ARSTREAM2_RtpReceiver_NetInfos_t net;
    struct ARSTREAM2_RtpReceiver_MuxInfos_t mux;
    struct ARSTREAM2_RtpReceiver_Ops_t ops;

    /* Process context */
    ARSTREAM2_RTP_ReceiverContext_t rtpReceiverContext;
    ARSTREAM2_RTPH264_ReceiverContext_t rtph264ReceiverContext;
    ARSTREAM2_RTCP_ReceiverContext_t rtcpReceiverContext;

    int maxBitrate;
    int maxLatencyMs;
    int maxNetworkLatencyMs;
    int insertStartCodes;
    int generateReceiverReports;
    uint8_t *rtcpMsgBuffer;

    /* Thread status */
    ARSAL_Mutex_t streamMutex;
    int threadShouldStop;
    int threadStarted;
    int pipe[2];

    /* Packet FIFO */
    ARSTREAM2_RTP_PacketFifo_t packetFifo;

    /* NAL unit and access unit FIFO */
    ARSTREAM2_H264_NaluFifo_t naluFifo;
    ARSTREAM2_H264_AuFifo_t auFifo;
    ARSAL_Mutex_t fifoMutex;

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
            err = ARSTREAM2_RtpSender_SendNewNalu(resender->sender, &nalu, 0); //TODO: input timestamp
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

static int ARSTREAM2_RtpReceiver_SetSocketReceiveBufferSize(ARSTREAM2_RtpReceiver_t *receiver, int socket, int size)
{
    int ret = 0, err;
    socklen_t size2 = sizeof(size2);

    size /= 2;
    err = setsockopt(socket, SOL_SOCKET, SO_RCVBUF, (void*)&size, sizeof(size));
    if (err != 0)
    {
        ret = -1;
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to set socket receive buffer size to 2*%d bytes: error=%d (%s)", size, errno, strerror(errno));
    }

    size = -1;
    err = getsockopt(socket, SOL_SOCKET, SO_RCVBUF, (void*)&size, &size2);
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

static int ARSTREAM2_RtpReceiver_StreamMuxSetup(ARSTREAM2_RtpReceiver_t *receiver)
{
#if BUILD_LIBMUX
    int ret, r2;

    if (receiver == NULL || receiver->mux.mux == NULL)
        return -EINVAL;

    ret = mux_channel_open(receiver->mux.mux, MUX_ARSDK_CHANNEL_ID_STREAM_DATA,
                           NULL, NULL);
    if (ret != 0)
        goto fail;

    ret = mux_channel_alloc_queue(receiver->mux.mux,
                                  MUX_ARSDK_CHANNEL_ID_STREAM_DATA,
                                  0,
                                  &(receiver->mux.data));

    if (ret != 0)
        goto close_channel;


    return 0;

close_channel:
    r2 = mux_channel_close(receiver->mux.mux, MUX_ARSDK_CHANNEL_ID_STREAM_DATA);
    if (r2 != 0)
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG,
                    "Error while closing mux channel in error handler: %s (%d)",
                    strerror(-r2), r2);
fail:
    receiver->mux.data = NULL;
    return ret;
#else
    return -ENOSYS;
#endif
}


static int ARSTREAM2_RtpReceiver_StreamSocketSetup(ARSTREAM2_RtpReceiver_t *receiver)
{
    int ret = 0;
    struct sockaddr_in recvSin;
    int err;

    /* create socket */
    receiver->net.streamSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (receiver->net.streamSocket < 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to create stream socket");
        ret = -1;
    }

#if HAVE_DECL_SO_NOSIGPIPE
    if (ret == 0)
    {
        /* remove SIGPIPE */
        int set = 1;
        err = setsockopt(receiver->net.streamSocket, SOL_SOCKET, SO_NOSIGPIPE, (void*)&set, sizeof(int));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Error on setsockopt: error=%d (%s)", errno, strerror(errno));
        }
    }
#endif

    if (ret == 0)
    {
        /* set to non-blocking */
        int flags = fcntl(receiver->net.streamSocket, F_GETFL, 0);
        fcntl(receiver->net.streamSocket, F_SETFL, flags | O_NONBLOCK);

        memset(&recvSin, 0, sizeof(struct sockaddr_in));
        recvSin.sin_family = AF_INET;
        recvSin.sin_port = htons(receiver->net.clientStreamPort);
        recvSin.sin_addr.s_addr = htonl(INADDR_ANY);

        if ((receiver->net.mcastAddr) && (strlen(receiver->net.mcastAddr)))
        {
            int addrFirst = atoi(receiver->net.mcastAddr);
            if ((addrFirst >= 224) && (addrFirst <= 239))
            {
                /* multicast */
                struct ip_mreq mreq;
                memset(&mreq, 0, sizeof(mreq));
                err = inet_pton(AF_INET, receiver->net.mcastAddr, &(mreq.imr_multiaddr.s_addr));
                if (err <= 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to convert address '%s'", receiver->net.mcastAddr);
                    ret = -1;
                }

                if (ret == 0)
                {
                    if ((receiver->net.mcastIfaceAddr) && (strlen(receiver->net.mcastIfaceAddr) > 0))
                    {
                        err = inet_pton(AF_INET, receiver->net.mcastIfaceAddr, &(mreq.imr_interface.s_addr));
                        if (err <= 0)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to convert address '%s'", receiver->net.mcastIfaceAddr);
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
                    err = setsockopt(receiver->net.streamSocket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq));
                    if (err != 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to join multacast group: error=%d (%s)", errno, strerror(errno));
                        ret = -1;
                    }
                }

                receiver->net.isMulticast = 1;
            }
            else
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Invalid multicast address '%s'", receiver->net.mcastAddr);
                ret = -1;
            }
        }
    }

    if (ret == 0)
    {
        /* allow multiple sockets to use the same port */
        unsigned int yes = 1;
        err = setsockopt(receiver->net.streamSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to set socket option SO_REUSEADDR: error=%d (%s)", errno, strerror(errno));
            ret = -1;
        }
    }

    if (ret == 0)
    {
        /* bind the socket */
        err = bind(receiver->net.streamSocket, (struct sockaddr*)&recvSin, sizeof(recvSin));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Error on stream socket bind port=%d: error=%d (%s)", receiver->net.clientStreamPort, errno, strerror(errno));
            ret = -1;
        }
    }

    if (ret == 0)
    {
        /* set the socket buffer size */
        if ((receiver->maxNetworkLatencyMs) && (receiver->maxBitrate))
        {
            err = ARSTREAM2_RtpReceiver_SetSocketReceiveBufferSize(receiver, receiver->net.streamSocket, (receiver->maxNetworkLatencyMs * receiver->maxBitrate * 2) / 8000); //TODO: should not be x2
            if (err != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to set the socket buffer size (%d)", err);
                ret = -1;
            }
        }
    }

    if (ret != 0)
    {
        if (receiver->net.streamSocket >= 0)
        {
            close(receiver->net.streamSocket);
        }
        receiver->net.streamSocket = -1;
    }

    return ret;
}

static int ARSTREAM2_RtpReceiver_StreamMuxTeardown(ARSTREAM2_RtpReceiver_t *receiver)
{
#if BUILD_LIBMUX
    int ret;
    if (receiver == NULL || receiver->mux.mux == NULL)
        return -EINVAL;

    if (receiver->mux.data == NULL)
        return 0;

    ret = mux_channel_close(receiver->mux.mux,
                            MUX_ARSDK_CHANNEL_ID_STREAM_DATA);
    if (ret == 0)
        receiver->mux.data = NULL;

    return ret;
#else
    return -ENOSYS;
#endif
}

static int ARSTREAM2_RtpReceiver_StreamSocketTeardown(ARSTREAM2_RtpReceiver_t *receiver)
{
    if (receiver == NULL)
        return -EINVAL;

    if (receiver->net.streamSocket != -1)
    {
        close(receiver->net.streamSocket);
        receiver->net.streamSocket = -1;
    }

    return 0;
}

static int ARSTREAM2_RtpReceiver_ControlMuxSetup(ARSTREAM2_RtpReceiver_t *receiver)
{
#if BUILD_LIBMUX
    int ret, r2;
    if (receiver == NULL || receiver->mux.mux == NULL)
        return -EINVAL;

    ret = mux_channel_open(receiver->mux.mux,
                           MUX_ARSDK_CHANNEL_ID_STREAM_CONTROL,
                           NULL, NULL);
    if (ret != 0)
        goto fail;

    ret = mux_channel_alloc_queue(receiver->mux.mux,
                                  MUX_ARSDK_CHANNEL_ID_STREAM_CONTROL,
                                  0,
                                  &(receiver->mux.control));

    if (ret != 0)
        goto close_channel;


    return 0;

close_channel:
    r2 = mux_channel_close(receiver->mux.mux,
                           MUX_ARSDK_CHANNEL_ID_STREAM_CONTROL);
    if (r2 != 0)
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG,
                    "Error while closing mux channel in error handler: %s (%d)",
                    strerror(-r2), r2);
fail:
    receiver->mux.control = NULL;
    return ret;
#else
    return -ENOSYS;
#endif
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
        receiver->net.controlSocket = socket(AF_INET, SOCK_DGRAM, 0);
        if (receiver->net.controlSocket < 0)
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
        err = setsockopt(receiver->net.controlSocket, SOL_SOCKET, SO_NOSIGPIPE, (void*)&set, sizeof(int));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Error on setsockopt: error=%d (%s)", errno, strerror(errno));
        }
    }
#endif

    if (ret == 0)
    {
        /* set to non-blocking */
        int flags = fcntl(receiver->net.controlSocket, F_GETFL, 0);
        fcntl(receiver->net.controlSocket, F_SETFL, flags | O_NONBLOCK);

        /* receive address */
        memset(&recvSin, 0, sizeof(struct sockaddr_in));
        recvSin.sin_family = AF_INET;
        recvSin.sin_port = htons(receiver->net.clientControlPort);
        recvSin.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if (ret == 0)
    {
        /* allow multiple sockets to use the same port */
        unsigned int yes = 1;
        err = setsockopt(receiver->net.controlSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to set socket option SO_REUSEADDR: error=%d (%s)", errno, strerror(errno));
            ret = -1;
        }
    }

    if (ret == 0)
    {
        /* bind the socket */
        err = bind(receiver->net.controlSocket, (struct sockaddr*)&recvSin, sizeof(recvSin));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Error on control socket bind port=%d: error=%d (%s)", receiver->net.clientControlPort, errno, strerror(errno));
            ret = -1;
        }
    }

    if (ret == 0)
    {
        /* send address */
        memset(&sendSin, 0, sizeof(struct sockaddr_in));
        sendSin.sin_family = AF_INET;
        sendSin.sin_port = htons(receiver->net.serverControlPort);
        err = inet_pton(AF_INET, receiver->net.serverAddr, &(sendSin.sin_addr));
        if (err <= 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to convert address '%s'", receiver->net.serverAddr);
            ret = -1;
        }
    }

    if (ret == 0)
    {
        /* connect the socket */
        err = connect(receiver->net.controlSocket, (struct sockaddr*)&sendSin, sizeof(sendSin));
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Error on control socket connect to addr='%s' port=%d: error=%d (%s)", receiver->net.serverAddr, receiver->net.serverControlPort, errno, strerror(errno));
            ret = -1;
        }
    }

    if (ret != 0)
    {
        if (receiver->net.controlSocket >= 0)
        {
            close(receiver->net.controlSocket);
        }
        receiver->net.controlSocket = -1;
    }

    return ret;
}

static int ARSTREAM2_RtpReceiver_ControlMuxTeardown(ARSTREAM2_RtpReceiver_t *receiver)
{
#if BUILD_LIBMUX
    int ret;
    if (receiver == NULL || receiver->mux.mux == NULL)
        return -EINVAL;

    if (receiver->mux.control == NULL)
        return 0;

    ret = mux_channel_close(receiver->mux.mux,
                            MUX_ARSDK_CHANNEL_ID_STREAM_CONTROL);
    if (ret == 0)
        receiver->mux.control = NULL;

    return ret;
#else
    return -ENOSYS;
#endif
}

static int ARSTREAM2_RtpReceiver_ControlSocketTeardown(ARSTREAM2_RtpReceiver_t *receiver)
{
    if (receiver == NULL)
        return -EINVAL;

    if (receiver->net.controlSocket != -1)
    {
        close(receiver->net.controlSocket);
        receiver->net.controlSocket = -1;
    }

    return 0;
}

static void ARSTREAM2_RtpReceiver_UpdateMonitoring(ARSTREAM2_RtpReceiver_t *receiver, uint64_t recvTimestamp, uint32_t rtpTimestamp, uint64_t ntpTimestamp, uint64_t ntpTimestampLocal, uint16_t seqNum, uint16_t markerBit, uint32_t bytes)
{
    ARSAL_Mutex_Lock(&(receiver->monitoringMutex));

    if (receiver->monitoringCount < ARSTREAM2_RTP_RECEIVER_MONITORING_MAX_POINTS)
    {
        receiver->monitoringCount++;
    }
    receiver->monitoringIndex = (receiver->monitoringIndex + 1) % ARSTREAM2_RTP_RECEIVER_MONITORING_MAX_POINTS;
    receiver->monitoringPoint[receiver->monitoringIndex].bytes = bytes;
    receiver->monitoringPoint[receiver->monitoringIndex].rtpTimestamp = rtpTimestamp;
    receiver->monitoringPoint[receiver->monitoringIndex].ntpTimestamp = ntpTimestamp;
    receiver->monitoringPoint[receiver->monitoringIndex].ntpTimestampLocal = ntpTimestampLocal;
    receiver->monitoringPoint[receiver->monitoringIndex].seqNum = seqNum;
    receiver->monitoringPoint[receiver->monitoringIndex].markerBit = markerBit;
    receiver->monitoringPoint[receiver->monitoringIndex].recvTimestamp = recvTimestamp;

    ARSAL_Mutex_Unlock(&(receiver->monitoringMutex));

#ifdef ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT
    if (receiver->fMonitorOut)
    {
        fprintf(receiver->fMonitorOut, "%llu %lu %llu %llu %u %u %lu\n", (long long unsigned int)recvTimestamp, (long unsigned int)rtpTimestamp, (long long unsigned int)ntpTimestamp, (long long unsigned int)ntpTimestampLocal, seqNum, markerBit, (long unsigned int)bytes);
    }
#endif
}

static int ARSTREAM2_RtpReceiver_MuxReadData(ARSTREAM2_RtpReceiver_t *receiver, uint8_t *recvBuffer, int recvBufferSize, int *recvSize)
{
#if BUILD_LIBMUX
    int ret;
    struct pomp_buffer *buffer;
    const void *pb_data;
    size_t pb_len;

    if (receiver == NULL ||
        receiver->mux.data == NULL ||
        recvBuffer == NULL ||
        recvSize == NULL)
        return -EINVAL;

    ret = mux_queue_get_buf(receiver->mux.data,
                            &buffer);

    if (ret != 0)
        return ret;

    ret = pomp_buffer_get_cdata(buffer,
                                &pb_data,
                                &pb_len,
                                NULL);

    if (ret != 0)
        goto unref_buffer;

    if (pb_len > (size_t)recvBufferSize) {
        ret = -E2BIG;
        goto unref_buffer;
    }

    *recvSize = pb_len;
    memcpy(recvBuffer, pb_data, pb_len);

unref_buffer:
    pomp_buffer_unref(buffer);
    return ret;
#else
    return -ENOSYS;
#endif
}

static int ARSTREAM2_RtpReceiver_MuxRecvMmsg(ARSTREAM2_RtpReceiver_t *receiver, struct mmsghdr *msgvec, unsigned int vlen)
{
#if BUILD_LIBMUX
    int ret = 0;
    unsigned int i, count;

    if ((!receiver) || (!receiver->mux.data) || (!msgvec))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Invalid pointer");
        return -1;
    }

    for (i = 0, count = 0; i < vlen; i++)
    {
        int ret2, k;
        struct pomp_buffer *buffer;
        const void *pb_data;
        size_t pb_len, left, offset;

        ret2 = mux_queue_try_get_buf(receiver->mux.data, &buffer);
        if (ret2 != 0)
        {
            if (ret2 == -EPIPE)
            {
                ret = ret2;
            }
            break;
        }

        ret = pomp_buffer_get_cdata(buffer, &pb_data, &pb_len, NULL);
        if (ret != 0)
        {
            pomp_buffer_unref(buffer);
            break;
        }

        for (k = 0, offset = 0, left = pb_len; ((k < msgvec[i].msg_hdr.msg_iovlen) && (left > 0)); k++)
        {
            size_t sz = (msgvec[i].msg_hdr.msg_iov[0].iov_len < left) ? msgvec[i].msg_hdr.msg_iov[k].iov_len : left;
            memcpy(msgvec[i].msg_hdr.msg_iov[k].iov_base, pb_data + offset, sz);
            offset += sz;
            left -= sz;
        }

        if (left != 0)
        {
            pomp_buffer_unref(buffer);
            ret = -E2BIG;
            break;
        }

        pomp_buffer_unref(buffer);
        msgvec[i].msg_len = (unsigned int)pb_len;
        count++;
    }

    return (ret == 0) ? (int)count : ret;
#else
    return -ENOSYS;
#endif
}


#ifndef ARSTREAM2_HAS_MMSG
static int recvmmsg(int sockfd, struct mmsghdr *msgvec, unsigned int vlen,
                    unsigned int flags, struct timespec *timeout)
{
    unsigned int i, count;
    ssize_t ret;

    if (!msgvec)
    {
        return -1;
    }

    for (i = 0, count = 0; i < vlen; i++)
    {
        ret = recvmsg(sockfd, &msgvec[i].msg_hdr, flags);
        if (ret < 0)
        {
            if (count == 0)
            {
                return ret;
            }
            else
            {
                break;
            }
        }
        else
        {
            count++;
            msgvec[i].msg_len = (unsigned int)ret;
        }
    }

    return count;
}
#endif

static int ARSTREAM2_RtpReceiver_NetRecvMmsg(ARSTREAM2_RtpReceiver_t *receiver, struct mmsghdr *msgvec, unsigned int vlen)
{
    int ret;

    if ((!receiver) || (!msgvec))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Invalid pointer");
        return -1;
    }

    ret = recvmmsg(receiver->net.streamSocket, msgvec, vlen, 0, NULL);
    if (ret < 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Stream socket - recvmmsg error (%d): %s", errno, strerror(errno));
        return -1;
    }
    else
    {
        return ret;
    }
}

static int ARSTREAM2_RtpReceiver_NetReadData(ARSTREAM2_RtpReceiver_t *receiver, uint8_t *recvBuffer, int recvBufferSize, int *recvSize)
{
    int ret = 0, pollRet;
    ssize_t bytes;
    struct pollfd p;

    if ((!recvBuffer) || (!recvSize))
    {
        if (recvSize)
            *recvSize = 0;
        return -1;
    }

    bytes = recv(receiver->net.streamSocket, recvBuffer, recvBufferSize, 0);
    if (bytes < 0)
    {
        /* socket receive failed */
        switch (errno)
        {
        case EAGAIN:
            /* poll */
            p.fd = receiver->net.streamSocket;
            p.events = POLLIN;
            p.revents = 0;
            pollRet = poll(&p, 1, ARSTREAM2_RTP_RECEIVER_STREAM_DATAREAD_TIMEOUT_MS);
            if (pollRet == 0)
            {
                /* failed: poll timeout */
                ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Polling timed out");
                ret = -ETIMEDOUT;
                *recvSize = 0;
            }
            else if (pollRet < 0)
            {
                /* failed: poll error */
                ret = -errno;
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Poll error: error=%d (%s)", -ret, strerror(-ret));
                *recvSize = 0;
            }
            else if (p.revents & POLLIN)
            {
                bytes = recv(receiver->net.streamSocket, recvBuffer, recvBufferSize, 0);
                if (bytes >= 0)
                {
                    /* success: save the number of bytes read */
                    *recvSize = bytes;
                }
                else if (errno == EAGAIN)
                {
                    /* failed: socket not ready (this should not happen) */
                    ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTP_RECEIVER_TAG, "Socket not ready for reading");
                    ret = -EAGAIN;
                    *recvSize = 0;
                }
                else
                {
                    /* failed: socket error */
                    ret = -errno;
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Socket receive error #2 %d ('%s')", -ret, strerror(-ret));
                    *recvSize = 0;
                }
            }
            else
            {
                /* no poll error, no timeout, but socket is not ready */
                int error = 0;
                socklen_t errlen = sizeof(error);
                getsockopt(receiver->net.streamSocket, SOL_SOCKET, SO_ERROR, (void *)&error, &errlen);
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "No poll error, no timeout, but socket is not ready (revents = %d, error = %d)", p.revents, error);
                ret = -EIO;
                *recvSize = 0;
            }
            break;
        default:
            /* failed: socket error */
            ret = -errno;
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Socket receive error %d ('%s')", -ret, strerror(-ret));
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

static int ARSTREAM2_RtpReceiver_MuxSendControlData(ARSTREAM2_RtpReceiver_t *receiver,
                                                    uint8_t *buffer,
                                                    int size)
{
#if BUILD_LIBMUX
    int ret;
    struct pomp_buffer *pbuffer;

    if (receiver == NULL ||
        receiver->mux.mux == NULL ||
        buffer == NULL)
        return -EINVAL;

    pbuffer = pomp_buffer_new_with_data(buffer,
                                        size);

    if (pbuffer == NULL)
        return -ENOMEM;

    ret = mux_encode(receiver->mux.mux,
                     MUX_ARSDK_CHANNEL_ID_STREAM_CONTROL,
                     pbuffer);

    pomp_buffer_unref(pbuffer);

    /* On success, return the number of bytes sent */
    if (ret == 0)
        ret = size;

    return ret;
#else
    return -ENOSYS;
#endif
}

static int ARSTREAM2_RtpReceiver_NetSendControlData(ARSTREAM2_RtpReceiver_t *receiver, uint8_t *buffer, int size)
{
    int ret = send(receiver->net.controlSocket, buffer, size, 0);
    if (ret < 0)
        ret = -errno;
    return ret;
}

static int ARSTREAM2_RtpReceiver_MuxReadControlData(ARSTREAM2_RtpReceiver_t *receiver,
                                                    uint8_t *buffer,
                                                    int size,
                                                    int blocking)
{
#if BUILD_LIBMUX
    int ret;
    struct pomp_buffer *pbuffer;
    const void *pb_data;
    size_t pb_len;

    if (receiver == NULL ||
        receiver->mux.control == NULL ||
        buffer == NULL)
        return -EINVAL;

    if (blocking)
        ret = mux_queue_get_buf(receiver->mux.control,
                                &pbuffer);
    else
        ret = mux_queue_try_get_buf(receiver->mux.control,
                                    &pbuffer);

    if (ret != 0)
        return ret;

    ret = pomp_buffer_get_cdata(pbuffer,
                                &pb_data,
                                &pb_len,
                                NULL);

    if (ret != 0)
        goto unref_buffer;

    if (pb_len > (size_t)size) {
        ret = -E2BIG;
        goto unref_buffer;
    }

    memcpy(buffer, pb_data, pb_len);

    /* On success, return the number of bytes read */
    if (ret == 0)
        ret = pb_len;

unref_buffer:
    pomp_buffer_unref(pbuffer);
    return ret;
#else
    return -ENOSYS;
#endif
}

static int ARSTREAM2_RtpReceiver_NetReadControlData(ARSTREAM2_RtpReceiver_t *receiver, uint8_t *buffer, int size, int blocking)
{
    struct pollfd p;
    int pollRet;
    int bytes;

    if (blocking)
    {
        p.fd = receiver->net.controlSocket;
        p.events = POLLIN;
        p.revents = 0;
        pollRet = poll(&p, 1, ARSTREAM2_RTP_RECEIVER_CONTROL_DATAREAD_TIMEOUT_MS);
        if (pollRet == 0)
        {
            /* failed: poll timeout */
            ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Polling timed out");
            return -ETIMEDOUT;
        }
        else if (pollRet < 0)
        {
            /* failed: poll error */
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Poll error: error=%d (%s)", errno, strerror(errno));
            return -errno;
        }
        else if (p.revents & POLLIN)
        {
            bytes = recv(receiver->net.controlSocket, buffer, size, 0);
        }
        else
        {
            /* no poll error, no timeout, but socket is not ready */
            int error = 0;
            socklen_t errlen = sizeof(error);
            getsockopt(receiver->net.controlSocket, SOL_SOCKET, SO_ERROR, (void *)&error, &errlen);
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "No poll error, no timeout, but socket is not ready (revents = %d, error = %d)", p.revents, error);
            bytes = -EIO;
        }
    } else {
        bytes = recv(receiver->net.controlSocket, buffer, size, 0);
    }
    return bytes;
}


void ARSTREAM2_RtpReceiver_Stop(ARSTREAM2_RtpReceiver_t *receiver)
{
    int i, ret;

    if (receiver != NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Stopping receiver...");
        ARSAL_Mutex_Lock(&(receiver->streamMutex));
        receiver->threadShouldStop = 1;
        ARSAL_Mutex_Unlock(&(receiver->streamMutex));

        if (receiver->pipe[1] != -1)
        {
            char * buff = "x";
            write(receiver->pipe[1], buff, 1);
        }

        if (receiver->useMux) {
            /* To stop the mux threads, we have to teardown the channels here.
               The second teardown, done at the end of the thread loops, will
               have no effect, but can be useful when using net backend */
            ret = receiver->ops.streamChannelTeardown(receiver);
            if (ret != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to teardown the stream channel (error %d : %s).\n", -ret, strerror(-ret));
            }
            ret = receiver->ops.controlChannelTeardown(receiver);
            if (ret != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to teardown the control channel (error %d : %s).", -ret, strerror(-ret));
            }
        }

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


ARSTREAM2_RtpReceiver_t* ARSTREAM2_RtpReceiver_New(ARSTREAM2_RtpReceiver_Config_t *config,
                                                   ARSTREAM2_RtpReceiver_NetConfig_t *net_config,
                                                   ARSTREAM2_RtpReceiver_MuxConfig_t *mux_config,
                                                   eARSTREAM2_ERROR *error)
{
    ARSTREAM2_RtpReceiver_t *retReceiver = NULL;
    int streamMutexWasInit = 0;
    int fifoMutexWasInit = 0;
    int monitoringMutexWasInit = 0;
    int resenderMutexWasInit = 0;
    int naluBufferMutexWasInit = 0;
    int packetFifoWasCreated = 0;
    int naluFifoWasCreated = 0;
    int auFifoWasCreated = 0;
    eARSTREAM2_ERROR internalError = ARSTREAM2_OK;

    /* ARGS Check */
    if (config == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "No config provided");
        SET_WITH_CHECK(error, ARSTREAM2_ERROR_BAD_PARAMETERS);
        return retReceiver;
    }

    if (net_config == NULL && mux_config == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "No net/mux config provided");
        SET_WITH_CHECK(error, ARSTREAM2_ERROR_BAD_PARAMETERS);
        return retReceiver;
    }

    if (net_config != NULL && mux_config != NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Both net/mux config provided. Cannot use both !");
        SET_WITH_CHECK(error, ARSTREAM2_ERROR_BAD_PARAMETERS);
        return retReceiver;
    }


    if (net_config != NULL)
    {
        if ((net_config->serverAddr == NULL) || (!strlen(net_config->serverAddr)))
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Config: no server address provided");
            SET_WITH_CHECK(error, ARSTREAM2_ERROR_BAD_PARAMETERS);
            return retReceiver;
        }
        if ((net_config->serverStreamPort <= 0) || (net_config->serverControlPort <= 0))
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Config: no server ports provided");
            SET_WITH_CHECK(error, ARSTREAM2_ERROR_BAD_PARAMETERS);
            return retReceiver;
        }
    }

    if (mux_config != NULL)
    {
#if BUILD_LIBMUX
        if (mux_config->mux == NULL)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Config: no mux context provided");
            SET_WITH_CHECK(error, ARSTREAM2_ERROR_BAD_PARAMETERS);
            return retReceiver;
        }
#else
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Config: library built without mux support");
        SET_WITH_CHECK(error, ARSTREAM2_ERROR_BAD_PARAMETERS);
        return retReceiver;
#endif
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
        retReceiver->pipe[0] = -1;
        retReceiver->pipe[1] = -1;
        retReceiver->rtpReceiverContext.maxPacketSize = (config->maxPacketSize > 0) ? config->maxPacketSize - ARSTREAM2_RTP_TOTAL_HEADERS_SIZE : ARSTREAM2_RTP_MAX_PAYLOAD_SIZE;
        retReceiver->maxBitrate = (config->maxBitrate > 0) ? config->maxBitrate : 0;
        retReceiver->maxLatencyMs = (config->maxLatencyMs > 0) ? config->maxLatencyMs : 0;
        retReceiver->maxNetworkLatencyMs = (config->maxNetworkLatencyMs > 0) ? config->maxNetworkLatencyMs : 0;
        retReceiver->insertStartCodes = (config->insertStartCodes > 0) ? 1 : 0;
        retReceiver->generateReceiverReports = (config->generateReceiverReports > 0) ? 1 : 0;
        retReceiver->rtpReceiverContext.rtpClockRate = 90000;
        retReceiver->rtpReceiverContext.nominalDelay = 30000; //TODO
        retReceiver->rtpReceiverContext.previousExtSeqNum = -1;
        retReceiver->rtph264ReceiverContext.auPipe = config->filterPipe[1];
        retReceiver->rtph264ReceiverContext.previousDepayloadExtSeqNum = -1;
        retReceiver->rtph264ReceiverContext.previousDepayloadExtRtpTimestamp = 0;
        retReceiver->rtph264ReceiverContext.startCode = (retReceiver->insertStartCodes) ? htonl(ARSTREAM2_H264_BYTE_STREAM_NALU_START_CODE) : 0;
        retReceiver->rtph264ReceiverContext.startCodeLength = (retReceiver->insertStartCodes) ? ARSTREAM2_H264_BYTE_STREAM_NALU_START_CODE_LENGTH : 0;
        retReceiver->rtcpReceiverContext.receiverSsrc = ARSTREAM2_RTP_RECEIVER_SSRC;
        retReceiver->rtcpReceiverContext.rtcpByteRate = (retReceiver->maxBitrate > 0) ? retReceiver->maxBitrate * ARSTREAM2_RTCP_RECEIVER_BANDWIDTH_SHARE / 8 : ARSTREAM2_RTCP_RECEIVER_DEFAULT_BITRATE / 8;
        retReceiver->rtcpReceiverContext.cname = ARSTREAM2_RTP_RECEIVER_CNAME;
        retReceiver->rtcpReceiverContext.name = NULL;

        if (retReceiver->rtpReceiverContext.maxPacketSize < sizeof(ARSTREAM2_RTCP_ReceiverReport_t) + sizeof(ARSTREAM2_RTCP_ReceptionReportBlock_t))
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Config: max packet size is too small to hold a receiver report");
            internalError = ARSTREAM2_ERROR_BAD_PARAMETERS;
        }

        if (net_config)
        {
            ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "New RTP Receiver using sockets");
            retReceiver->net.isMulticast = 0;
            retReceiver->net.streamSocket = -1;
            retReceiver->net.controlSocket = -1;

            if (net_config->serverAddr)
            {
                retReceiver->net.serverAddr = strndup(net_config->serverAddr, 16);
            }
            if (net_config->mcastAddr)
            {
                retReceiver->net.mcastAddr = strndup(net_config->mcastAddr, 16);
            }
            if (net_config->mcastIfaceAddr)
            {
                retReceiver->net.mcastIfaceAddr = strndup(net_config->mcastIfaceAddr, 16);
            }
            retReceiver->net.serverStreamPort = net_config->serverStreamPort;
            retReceiver->net.serverControlPort = net_config->serverControlPort;
            retReceiver->net.clientStreamPort = (net_config->clientStreamPort > 0) ? net_config->clientStreamPort : ARSTREAM2_RTP_RECEIVER_DEFAULT_CLIENT_STREAM_PORT;
            retReceiver->net.clientControlPort = (net_config->clientControlPort > 0) ? net_config->clientControlPort : ARSTREAM2_RTP_RECEIVER_DEFAULT_CLIENT_CONTROL_PORT;

            retReceiver->useMux = 0;

            retReceiver->ops.streamChannelSetup = ARSTREAM2_RtpReceiver_StreamSocketSetup;
            retReceiver->ops.streamChannelRead = ARSTREAM2_RtpReceiver_NetReadData;
            retReceiver->ops.streamChannelRecvMmsg = ARSTREAM2_RtpReceiver_NetRecvMmsg;
            retReceiver->ops.streamChannelTeardown = ARSTREAM2_RtpReceiver_StreamSocketTeardown;

            retReceiver->ops.controlChannelSetup = ARSTREAM2_RtpReceiver_ControlSocketSetup;
            retReceiver->ops.controlChannelSend = ARSTREAM2_RtpReceiver_NetSendControlData;
            retReceiver->ops.controlChannelRead = ARSTREAM2_RtpReceiver_NetReadControlData;
            retReceiver->ops.controlChannelTeardown = ARSTREAM2_RtpReceiver_ControlSocketTeardown;
        }

#if BUILD_LIBMUX
        if (mux_config)
        {
            ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "New RTP Receiver using mux");
            retReceiver->mux.mux = mux_config->mux;
            mux_ref(retReceiver->mux.mux);
            retReceiver->useMux = 1;

            retReceiver->ops.streamChannelSetup = ARSTREAM2_RtpReceiver_StreamMuxSetup;
            retReceiver->ops.streamChannelRead = ARSTREAM2_RtpReceiver_MuxReadData;
            retReceiver->ops.streamChannelRecvMmsg = ARSTREAM2_RtpReceiver_MuxRecvMmsg;
            retReceiver->ops.streamChannelTeardown = ARSTREAM2_RtpReceiver_StreamMuxTeardown;

            retReceiver->ops.controlChannelSetup = ARSTREAM2_RtpReceiver_ControlMuxSetup;
            retReceiver->ops.controlChannelSend = ARSTREAM2_RtpReceiver_MuxSendControlData;
            retReceiver->ops.controlChannelRead = ARSTREAM2_RtpReceiver_MuxReadControlData;
            retReceiver->ops.controlChannelTeardown = ARSTREAM2_RtpReceiver_ControlMuxTeardown;
        }
#endif

    }

    if (internalError == ARSTREAM2_OK)
    {
        if (pipe(retReceiver->pipe) != 0)
        {
            internalError = ARSTREAM2_ERROR_RESOURCE_UNAVAILABLE;
        }
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
        int mutexInitRet = ARSAL_Mutex_Init(&(retReceiver->fifoMutex));
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

    /* Setup the access unit FIFO */
    if (internalError == ARSTREAM2_OK)
    {
        int auFifoRet = ARSTREAM2_H264_AuFifoInit(&retReceiver->auFifo, ARSTREAM2_RTP_RECEIVER_DEFAULT_AU_FIFO_SIZE,
                                                  ARSTREAM2_RTP_RECEIVER_AU_BUFFER_SIZE, ARSTREAM2_RTP_RECEIVER_AU_METADATA_BUFFER_SIZE,
                                                  ARSTREAM2_RTP_RECEIVER_AU_USER_DATA_BUFFER_SIZE);
        if (auFifoRet != 0)
        {
            internalError = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            auFifoWasCreated = 1;
        }
    }

    /* Setup the NAL unit FIFO */
    if (internalError == ARSTREAM2_OK)
    {
        int naluFifoRet = ARSTREAM2_H264_NaluFifoInit(&retReceiver->naluFifo, ARSTREAM2_RTP_RECEIVER_DEFAULT_NALU_FIFO_SIZE);
        if (naluFifoRet != 0)
        {
            internalError = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            naluFifoWasCreated = 1;
        }
    }

    /* Setup the packet FIFO */
    if (internalError == ARSTREAM2_OK)
    {
        int packetFifoRet = ARSTREAM2_RTP_FifoInit(&retReceiver->packetFifo, ARSTREAM2_RTP_RECEIVER_DEFAULT_PACKET_FIFO_SIZE,
                                                   retReceiver->rtpReceiverContext.maxPacketSize);
        if (packetFifoRet != 0)
        {
            internalError = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            packetFifoWasCreated = 1;
        }
    }

    /* Stream channel setup */
    if (internalError == ARSTREAM2_OK)
    {
        int ret = retReceiver->ops.streamChannelSetup(retReceiver);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to setup the stream channel (error %d)", ret);
            internalError = ARSTREAM2_ERROR_RESOURCE_UNAVAILABLE;
        }
    }

    /* Control channel setup */
    if (internalError == ARSTREAM2_OK)
    {
        int ret = retReceiver->ops.controlChannelSetup(retReceiver);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to setup the control channel (error %d)", ret);
            internalError = ARSTREAM2_ERROR_RESOURCE_UNAVAILABLE;
        }
    }

    /* RTCP message buffer */
    if (internalError == ARSTREAM2_OK)
    {
        retReceiver->rtcpMsgBuffer = malloc(retReceiver->rtpReceiverContext.maxPacketSize);
        if (retReceiver->rtcpMsgBuffer == NULL)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Memory allocation failed (%d)", retReceiver->rtpReceiverContext.maxPacketSize);
            internalError = ARSTREAM2_ERROR_ALLOC;
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
            fprintf(retReceiver->fMonitorOut, "recvTimestamp rtpTimestamp ntpTimestamp ntpTimestampLocal rtpSeqNum rtpMarkerBit bytes\n");
        }
    }
#endif //#ifdef ARSTREAM2_RTP_RECEIVER_MONITORING_OUTPUT

    if ((internalError != ARSTREAM2_OK) &&
        (retReceiver != NULL))
    {
        int ret = retReceiver->ops.streamChannelTeardown(retReceiver);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to teardown the stream channel (error %d : %s).\n", -ret, strerror(-ret));
        }
        ret = retReceiver->ops.controlChannelTeardown(retReceiver);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to teardown the control channel (error %d : %s).\n", -ret, strerror(-ret));
        }
        if (retReceiver->pipe[0] != -1)
        {
            close(retReceiver->pipe[0]);
            retReceiver->pipe[0] = -1;
        }
        if (retReceiver->pipe[1] != -1)
        {
            close(retReceiver->pipe[1]);
            retReceiver->pipe[1] = -1;
        }
        if (streamMutexWasInit == 1)
        {
            ARSAL_Mutex_Destroy(&(retReceiver->streamMutex));
        }
        if (fifoMutexWasInit == 1)
        {
            ARSAL_Mutex_Destroy(&(retReceiver->fifoMutex));
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
        if (retReceiver->rtcpMsgBuffer)
        {
            free(retReceiver->rtcpMsgBuffer);
        }
        if (packetFifoWasCreated == 1)
        {
            ARSTREAM2_RTP_FifoFree(&retReceiver->packetFifo);
        }
        if (naluFifoWasCreated == 1)
        {
            ARSTREAM2_H264_NaluFifoFree(&retReceiver->naluFifo);
        }
        if (auFifoWasCreated == 1)
        {
            ARSTREAM2_H264_AuFifoFree(&retReceiver->auFifo);
        }
        if ((retReceiver) && (retReceiver->net.serverAddr))
        {
            free(retReceiver->net.serverAddr);
        }
        if ((retReceiver) && (retReceiver->net.mcastAddr))
        {
            free(retReceiver->net.mcastAddr);
        }
        if ((retReceiver) && (retReceiver->net.mcastIfaceAddr))
        {
            free(retReceiver->net.mcastIfaceAddr);
        }

#if BUILD_LIBMUX
        if ((retReceiver) && (retReceiver->mux.mux))
        {
            mux_unref(retReceiver->mux.mux);
        }
#endif

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


eARSTREAM2_ERROR ARSTREAM2_RtpReceiver_Delete(ARSTREAM2_RtpReceiver_t **receiver)
{
    eARSTREAM2_ERROR retVal = ARSTREAM2_ERROR_BAD_PARAMETERS;
    if ((receiver != NULL) &&
        (*receiver != NULL))
    {
        int canDelete = 0;
        ARSAL_Mutex_Lock(&((*receiver)->streamMutex));
        if ((*receiver)->threadStarted == 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Thread is stopped");
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

            int ret = (*receiver)->ops.streamChannelTeardown((*receiver));
            if (ret != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to teardown the stream channel (error %d : %s).\n", -ret, strerror(-ret));
            }
            ret = (*receiver)->ops.controlChannelTeardown((*receiver));
            if (ret != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to teardown the control channel (error %d : %s).\n", -ret, strerror(-ret));
            }
            if ((*receiver)->pipe[0] != -1)
            {
                close((*receiver)->pipe[0]);
                (*receiver)->pipe[0] = -1;
            }
            if ((*receiver)->pipe[1] != -1)
            {
                close((*receiver)->pipe[1]);
                (*receiver)->pipe[1] = -1;
            }
            ARSAL_Mutex_Destroy(&((*receiver)->streamMutex));
            ARSAL_Mutex_Destroy(&((*receiver)->fifoMutex));
            ARSAL_Mutex_Destroy(&((*receiver)->monitoringMutex));
            ARSAL_Mutex_Destroy(&((*receiver)->resenderMutex));
            ARSAL_Mutex_Destroy(&((*receiver)->naluBufferMutex));
            ARSTREAM2_RTP_FifoFree(&(*receiver)->packetFifo);
            ARSTREAM2_H264_NaluFifoFree(&(*receiver)->naluFifo);
            ARSTREAM2_H264_AuFifoFree(&(*receiver)->auFifo);
            if ((*receiver)->rtcpMsgBuffer)
            {
                free((*receiver)->rtcpMsgBuffer);
            }
            if ((*receiver)->net.serverAddr)
            {
                free((*receiver)->net.serverAddr);
            }
            if ((*receiver)->net.mcastAddr)
            {
                free((*receiver)->net.mcastAddr);
            }
            if ((*receiver)->net.mcastIfaceAddr)
            {
                free((*receiver)->net.mcastIfaceAddr);
            }

#if BUILD_LIBMUX
            if ((*receiver)->mux.mux)
            {
                mux_unref((*receiver)->mux.mux);
            }
#endif

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


void* ARSTREAM2_RtpReceiver_RunThread(void *ARSTREAM2_RtpReceiver_t_Param)
{
    /* Local declarations */
    ARSTREAM2_RtpReceiver_t *receiver = (ARSTREAM2_RtpReceiver_t *)ARSTREAM2_RtpReceiver_t_Param;
    int shouldStop, ret, selectRet;
    struct timespec t1;
    uint64_t curTime;
    uint32_t nextRrDelay = ARSTREAM2_RTCP_RECEIVER_MIN_PACKET_TIME_INTERVAL, rrDelay = 0;
    fd_set readSet, readSetSaved;
    fd_set exceptSet, exceptSetSaved;
    int maxFd;
    struct timeval tv;

    /* Parameters check */
    if (receiver == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Cannot start thread: bad context parameter");
        return (void *)0;
    }

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Receiver thread running");
    ARSAL_Mutex_Lock(&(receiver->streamMutex));
    receiver->threadStarted = 1;
    shouldStop = receiver->threadShouldStop;
    ARSAL_Mutex_Unlock(&(receiver->streamMutex));

    FD_ZERO(&readSetSaved);
    FD_SET(receiver->pipe[0], &readSetSaved);
    FD_SET(receiver->net.streamSocket, &readSetSaved);
    FD_SET(receiver->net.controlSocket, &readSetSaved);
    FD_ZERO(&exceptSetSaved);
    FD_SET(receiver->pipe[0], &exceptSetSaved);
    FD_SET(receiver->net.streamSocket, &exceptSetSaved);
    FD_SET(receiver->net.controlSocket, &exceptSetSaved);
    maxFd = receiver->pipe[0];
    if (receiver->net.streamSocket > maxFd) maxFd = receiver->net.streamSocket;
    if (receiver->net.controlSocket > maxFd) maxFd = receiver->net.controlSocket;
    maxFd++;
    readSet = readSetSaved;
    exceptSet = exceptSetSaved;
    tv.tv_sec = 0;
    tv.tv_usec = ARSTREAM2_RTP_RECEIVER_TIMEOUT_US;

    while (shouldStop == 0)
    {
        selectRet = select(maxFd, &readSet, NULL, &exceptSet, &tv);

        ARSAL_Time_GetTime(&t1);
        curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

        if (FD_ISSET(receiver->net.streamSocket, &exceptSet))
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Exception on stream socket");
        }
        if (FD_ISSET(receiver->net.controlSocket, &exceptSet))
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Exception on control socket");
        }
        if (selectRet < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Select error (%d): %s", errno, strerror(errno));
        }

        /* RTCP sender reports */
        if ((selectRet >= 0) && (FD_ISSET(receiver->net.controlSocket, &readSet)))
        {
            /* The control channel is ready for reading */
            ssize_t bytes = receiver->ops.controlChannelRead(receiver, receiver->rtcpMsgBuffer, receiver->rtpReceiverContext.maxPacketSize, 0);
            if ((bytes < 0) && (errno != EAGAIN))
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Control channel - read error (%d): %s", errno, strerror(errno));
                if (bytes == -EPIPE && receiver->useMux == 1)
                {
                    /* For the mux case, EPIPE means that the channel should not be used again */
                    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Got an EPIPE for control channel, stopping thread");
                    shouldStop = 1;
                }
            }
            while (bytes > 0)
            {
                ret = ARSTREAM2_RTCP_Receiver_ProcessCompoundPacket(receiver->rtcpMsgBuffer, (unsigned int)bytes,
                                                                    curTime, &receiver->rtcpReceiverContext);
                if (ret != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to process compound RTCP packet (%d)", ret);
                }

                bytes = receiver->ops.controlChannelRead(receiver, receiver->rtcpMsgBuffer, receiver->rtpReceiverContext.maxPacketSize, 0);
                if ((bytes < 0) && (errno != EAGAIN))
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Control channel - read error (%d): %s", errno, strerror(errno));
                    if (bytes == -EPIPE && receiver->useMux == 1)
                    {
                        /* For the mux case, EPIPE means that the channel should not be used again */
                        ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Got an EPIPE for control channel, stopping thread");
                        shouldStop = 1;
                    }
                }
            }
        }

        /* RTP packets reception */
        if ((selectRet >= 0) && (FD_ISSET(receiver->net.streamSocket, &readSet)))
        {
            ret = ARSTREAM2_RTP_Receiver_FifoFillMsgVec(&receiver->rtpReceiverContext, &receiver->packetFifo);
            if (ret < 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "ARSTREAM2_RTP_Receiver_FifoFillMsgVec() failed (%d)", ret);
                ret = -1;
            }
            else if (ret > 0)
            {
                unsigned int msgCount = (unsigned  int)ret;

                ret = receiver->ops.streamChannelRecvMmsg(receiver, receiver->packetFifo.msgVec, msgCount);
                if (ret < 0)
                {
                    if (ret == -EPIPE && receiver->useMux == 1)
                    {
                        /* EPIPE with the mux means that we should no longer use the channel */
                        ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Got an EPIPE for stream channel, stopping thread");
                        shouldStop = 1;
                    }
                    if (ret != -ETIMEDOUT)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Failed to read data (%d)", ret);
                    }
                }
                else
                {
                    unsigned int recvMsgCount = (unsigned int)ret;

                    ret = ARSTREAM2_RTP_Receiver_FifoAddFromMsgVec(&receiver->rtpReceiverContext, &receiver->packetFifo,
                                                                   recvMsgCount, curTime, &receiver->rtcpReceiverContext);
                    if (ret < 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "ARSTREAM2_RTP_Receiver_FifoAddFromMsgVec() failed (%d)", ret);
                        ret = -1;
                    }
                }
            }
        }

        /* RTP packets processing */
        ARSAL_Mutex_Lock(&(receiver->fifoMutex));
        ret = ARSTREAM2_RTPH264_Receiver_PacketFifoToAuFifo(&receiver->rtph264ReceiverContext,
                                                            &receiver->packetFifo, &receiver->naluFifo, &receiver->auFifo,
                                                            curTime, &receiver->rtcpReceiverContext);
        ARSAL_Mutex_Unlock(&(receiver->fifoMutex));
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "ARSTREAM2_RTPH264_Receiver_PacketFifoToAuFifo() failed (%d)", ret);
        }

        /* RTCP receiver reports */
        if (receiver->generateReceiverReports)
        {
            rrDelay = (uint32_t)(curTime - receiver->rtcpReceiverContext.lastRrTimestamp);
            if ((rrDelay >= nextRrDelay) && (receiver->rtcpReceiverContext.prevSrNtpTimestamp != 0))
            {
                unsigned int size = 0;

                ret = ARSTREAM2_RTCP_Receiver_GenerateCompoundPacket(receiver->rtcpMsgBuffer, receiver->rtpReceiverContext.maxPacketSize,
                                                                     curTime, 1, 1, 1, &receiver->rtcpReceiverContext, &size);
                if ((ret == 0) && (size > 0))
                {
                    ssize_t bytes = receiver->ops.controlChannelSend(receiver, receiver->rtcpMsgBuffer, size);
                    if (bytes < 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_RECEIVER_TAG, "Control channel - send error (%d): %s", errno, strerror(errno));
                    }
                }

                nextRrDelay = (size + ARSTREAM2_RTP_UDP_HEADER_SIZE + ARSTREAM2_RTP_IP_HEADER_SIZE) * 1000000 / receiver->rtcpReceiverContext.rtcpByteRate;
                if (nextRrDelay < ARSTREAM2_RTCP_RECEIVER_MIN_PACKET_TIME_INTERVAL) nextRrDelay = ARSTREAM2_RTCP_RECEIVER_MIN_PACKET_TIME_INTERVAL;
            }
        }

        if ((selectRet >= 0) && (FD_ISSET(receiver->pipe[0], &readSet)))
        {
            /* Dump bytes (so it won't be ready next time) */
            char dump[10];
            read(receiver->pipe[0], &dump, 10);
        }

        ARSAL_Mutex_Lock(&(receiver->streamMutex));
        shouldStop = receiver->threadShouldStop;
        ARSAL_Mutex_Unlock(&(receiver->streamMutex));

        if (!shouldStop)
        {
            /* Prepare the next select */
            readSet = readSetSaved;
            exceptSet = exceptSetSaved;
            tv.tv_sec = 0;
            if (receiver->generateReceiverReports)
            {
                tv.tv_usec = (nextRrDelay - rrDelay < ARSTREAM2_RTP_RECEIVER_TIMEOUT_US) ? nextRrDelay - rrDelay : ARSTREAM2_RTP_RECEIVER_TIMEOUT_US;
            }
            else
            {
                tv.tv_usec = ARSTREAM2_RTP_RECEIVER_TIMEOUT_US;
            }
        }
    }

    ARSAL_Mutex_Lock(&(receiver->streamMutex));
    receiver->threadStarted = 0;
    ARSAL_Mutex_Unlock(&(receiver->streamMutex));

    /* flush the NALU FIFO and packet FIFO */
    ARSAL_Mutex_Lock(&(receiver->fifoMutex));
    ARSTREAM2_H264_AuFifoFlush(&receiver->auFifo);
    ARSTREAM2_H264_NaluFifoFlush(&receiver->naluFifo);
    ARSAL_Mutex_Unlock(&(receiver->fifoMutex));
    ARSTREAM2_RTP_Receiver_FifoFlush(&receiver->packetFifo);

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_RECEIVER_TAG, "Receiver thread ended");

    return (void*)0;
}


eARSTREAM2_ERROR ARSTREAM2_RtpReceiver_GetSharedContext(ARSTREAM2_RtpReceiver_t *receiver, void **auFifo, void **naluFifo, void **mutex)
{
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;

    if ((receiver == NULL) || (auFifo == 0) || (naluFifo == 0) || (mutex == 0))
    {
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    *auFifo = &receiver->auFifo;
    *naluFifo = &receiver->naluFifo;
    *mutex = &receiver->fifoMutex;

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_RtpReceiver_GetMonitoring(ARSTREAM2_RtpReceiver_t *receiver, uint64_t startTime, uint32_t timeIntervalUs, uint32_t *realTimeIntervalUs, uint32_t *receptionTimeJitter,
                                                     uint32_t *bytesReceived, uint32_t *meanPacketSize, uint32_t *packetSizeStdDev, uint32_t *packetsReceived, uint32_t *packetsMissed)
{
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;
    uint64_t endTime, curTime, previousTime, auTimestamp, receptionTimeSum = 0, receptionTimeVarSum = 0, packetSizeVarSum = 0;
    uint32_t bytes, bytesSum = 0, _meanPacketSize = 0, receptionTime = 0, meanReceptionTime = 0, _receptionTimeJitter = 0, _packetSizeStdDev = 0;
    int currentSeqNum, previousSeqNum = -1, seqNumDelta, gapsInSeqNum = 0;
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
            auTimestamp = receiver->monitoringPoint[idx].ntpTimestampLocal;
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
                auTimestamp = receiver->monitoringPoint[idx].ntpTimestampLocal;
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


void* ARSTREAM2_RtpReceiver_RtpResender_RunThread (void *ARSTREAM2_RtpReceiver_RtpResender_t_Param)
{
    ARSTREAM2_RtpReceiver_RtpResender_t *resender = (ARSTREAM2_RtpReceiver_RtpResender_t *)ARSTREAM2_RtpReceiver_RtpResender_t_Param;

    if (resender == NULL)
    {
        return (void *)0;
    }

    return ARSTREAM2_RtpSender_RunThread((void *)resender->sender);
}
