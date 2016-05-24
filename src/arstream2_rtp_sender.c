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
#define __USE_GNU
#include <sys/socket.h>
#undef __USE_GNU
#define ARSTREAM2_HAS_SENDMMSG //TODO
#include <netinet/in.h>
#include <fcntl.h>
#include <poll.h>
#include <math.h>

#include <libARStream2/arstream2_rtp_sender.h>
#include "arstream2_rtp.h"
#include "arstream2_rtp_h264.h"
#include "arstream2_rtcp.h"

#include <libARSAL/ARSAL_Print.h>
#include <libARSAL/ARSAL_Mutex.h>
#include <libARSAL/ARSAL_Socket.h>


/**
 * Tag for ARSAL_PRINT
 */
#define ARSTREAM2_RTP_SENDER_TAG "ARSTREAM2_RtpSender"


/**
 * Timeout value (microseconds)
 */
#define ARSTREAM2_RTP_SENDER_TIMEOUT_US (100 * 1000)


/**
 * Maximum number of elements for the monitoring
 */
#define ARSTREAM2_RTP_SENDER_MONITORING_MAX_POINTS (2048)


/**
 * Default minimum stream socket send buffer size: 50ms @ 5Mbit/s
 */
#define ARSTREAM2_RTP_SENDER_DEFAULT_MIN_STREAM_SOCKET_SEND_BUFFER_SIZE (31250)


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


typedef struct ARSTREAM2_RtpSender_MonitoringPoint_s {
    uint64_t inputTimestamp;
    uint64_t outputTimestamp;
    uint64_t ntpTimestamp;
    uint32_t rtpTimestamp;
    uint16_t seqNum;
    uint16_t markerBit;
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
    ARSTREAM2_RtpSender_ReceiverReportCallback_t receiverReportCallback;
    void *receiverReportCallbackUserPtr;
    int naluFifoSize;
    int maxBitrate;
    uint32_t maxLatencyUs;
    uint32_t maxNetworkLatencyUs;
    uint8_t *rtcpMsgBuffer;

    ARSTREAM2_RTP_SenderContext_t rtpSenderContext;
    ARSTREAM2_RTCP_SenderContext_t rtcpSenderContext;
    ARSAL_Mutex_t streamMutex;

    /* Thread status */
    int threadShouldStop;
    int threadStarted;

    /* Sockets */
    int isMulticast;
    int streamSocketSendBufferSize;
    struct sockaddr_in streamSendSin;
    int streamSocket;
    int controlSocket;
    
    /* NAL unit FIFO */
    ARSAL_Mutex_t naluFifoMutex;
    int naluFifoPipe[2];
    int naluFifoBufferSize;
    ARSTREAM2_RTPH264_NaluFifo_t naluFifo;

    /* Packet FIFO */
    ARSTREAM2_RTP_PacketFifo_t packetFifo;

    /* Monitoring */
    ARSAL_Mutex_t monitoringMutex;
    int monitoringCount;
    int monitoringIndex;
    ARSTREAM2_RtpSender_MonitoringPoint_t monitoringPoint[ARSTREAM2_RTP_SENDER_MONITORING_MAX_POINTS];
#ifdef ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT
    FILE* fMonitorOut;
#endif
};


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


#ifndef ARSTREAM2_HAS_SENDMMSG
static int sendmmsg(int sockfd, struct mmsghdr *msgvec, unsigned int vlen, unsigned int flags)
{
    unsigned int i;
    ssize_t ret;

    if (!msgvec)
    {
        return -1;
    }

    for (i = 0; i < vlen; i++)
    {
        ret = sendmsg(sockfd, &msgvec[i].msg_hdr, flags);
        if (ret < 0)
        {
            return ret;
        }
        else
        {
            msgvec[i].msg_len = (unsigned int)ret;
        }
    }
}
#endif


static int ARSTREAM2_RtpSender_FlushNaluFifo(ARSTREAM2_RtpSender_t *sender)
{
    if (!sender)
    {
        return -1;
    }

    int fifoRes;
    ARSTREAM2_RTPH264_Nalu_t nalu;

    while ((fifoRes = ARSTREAM2_RTPH264_FifoDequeueNalu(&sender->naluFifo, &nalu)) == 0)
    {
        //TODO: prune the NALUs and remove them in the thread to avoid calling the AU callback multiple times due to lastAuTimestamp concurrent access

        /* call the naluCallback */
        if (sender->rtpSenderContext.naluCallback != NULL)
        {
            ((ARSTREAM2_RtpSender_NaluCallback_t)sender->rtpSenderContext.naluCallback)(ARSTREAM2_RTP_SENDER_STATUS_CANCELLED, nalu.naluUserPtr, sender->rtpSenderContext.naluCallbackUserPtr);
        }

        /* last NALU in the Access Unit: call the auCallback */
        if ((sender->rtpSenderContext.auCallback != NULL) && (nalu.isLastInAu))
        {
            if (nalu.ntpTimestamp != sender->rtpSenderContext.lastAuCallbackTimestamp)
            {
                sender->rtpSenderContext.lastAuCallbackTimestamp = nalu.ntpTimestamp;

                /* call the auCallback */
                ((ARSTREAM2_RtpSender_AuCallback_t)sender->rtpSenderContext.auCallback)(ARSTREAM2_RTP_SENDER_STATUS_CANCELLED, nalu.auUserPtr, sender->rtpSenderContext.auCallbackUserPtr);
            }
        }
    }

    return 0;
}


static void ARSTREAM2_RtpSender_UpdateMonitoring(uint64_t inputTimestamp, uint64_t outputTimestamp, uint64_t ntpTimestamp,
                                                 uint32_t rtpTimestamp, uint16_t seqNum, uint16_t markerBit,
                                                 uint32_t bytesSent, uint32_t bytesDropped, void *userPtr)
{
    ARSTREAM2_RtpSender_t *sender = (ARSTREAM2_RtpSender_t*)userPtr;

    if (!sender)
    {
        return;
    }

    ARSAL_Mutex_Lock(&(sender->monitoringMutex));

    if (sender->monitoringCount < ARSTREAM2_RTP_SENDER_MONITORING_MAX_POINTS)
    {
        sender->monitoringCount++;
    }
    sender->monitoringIndex = (sender->monitoringIndex + 1) % ARSTREAM2_RTP_SENDER_MONITORING_MAX_POINTS;
    sender->monitoringPoint[sender->monitoringIndex].inputTimestamp = inputTimestamp;
    sender->monitoringPoint[sender->monitoringIndex].outputTimestamp = outputTimestamp;
    sender->monitoringPoint[sender->monitoringIndex].ntpTimestamp = ntpTimestamp;
    sender->monitoringPoint[sender->monitoringIndex].rtpTimestamp = rtpTimestamp;
    sender->monitoringPoint[sender->monitoringIndex].seqNum = seqNum;
    sender->monitoringPoint[sender->monitoringIndex].markerBit = markerBit;
    sender->monitoringPoint[sender->monitoringIndex].bytesSent = bytesSent;
    sender->monitoringPoint[sender->monitoringIndex].bytesDropped = bytesDropped;

    ARSAL_Mutex_Unlock(&(sender->monitoringMutex));

#ifdef ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT
    if (sender->fMonitorOut)
    {
        fprintf(sender->fMonitorOut, "%llu ", (long long unsigned int)ntpTimestamp);
        fprintf(sender->fMonitorOut, "%llu ", (long long unsigned int)inputTimestamp);
        fprintf(sender->fMonitorOut, "%llu ", (long long unsigned int)outputTimestamp);
        fprintf(sender->fMonitorOut, "%lu %u %u %lu %lu\n", (long unsigned int)rtpTimestamp, seqNum, markerBit, (long unsigned int)bytesSent, (long unsigned int)bytesDropped);
    }
#endif
}


ARSTREAM2_RtpSender_t* ARSTREAM2_RtpSender_New(ARSTREAM2_RtpSender_Config_t *config, eARSTREAM2_ERROR *error)
{
    ARSTREAM2_RtpSender_t *retSender = NULL;
    int streamMutexWasInit = 0;
    int monitoringMutexWasInit = 0;
    int packetFifoWasCreated = 0;
    int naluFifoWasCreated = 0;
    int naluFifoMutexWasInit = 0;
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
        retSender->naluFifoPipe[0] = -1;
        retSender->naluFifoPipe[1] = -1;
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
        retSender->rtpSenderContext.auCallback = config->auCallback;
        retSender->rtpSenderContext.auCallbackUserPtr = config->auCallbackUserPtr;
        retSender->rtpSenderContext.naluCallback = config->naluCallback;
        retSender->rtpSenderContext.naluCallbackUserPtr = config->naluCallbackUserPtr;
        retSender->rtpSenderContext.monitoringCallback = ARSTREAM2_RtpSender_UpdateMonitoring;
        retSender->rtpSenderContext.monitoringCallbackUserPtr = retSender;
        retSender->receiverReportCallback = config->receiverReportCallback;
        retSender->receiverReportCallbackUserPtr = config->receiverReportCallbackUserPtr;
        retSender->naluFifoSize = (config->naluFifoSize > 0) ? config->naluFifoSize : ARSTREAM2_RTP_SENDER_DEFAULT_NALU_FIFO_SIZE;
        retSender->rtpSenderContext.maxPacketSize = (config->maxPacketSize > 0) ? (uint32_t)config->maxPacketSize - ARSTREAM2_RTP_TOTAL_HEADERS_SIZE : ARSTREAM2_RTP_MAX_PAYLOAD_SIZE;
        retSender->rtpSenderContext.targetPacketSize = (config->targetPacketSize > 0) ? (uint32_t)config->targetPacketSize - ARSTREAM2_RTP_TOTAL_HEADERS_SIZE : retSender->rtpSenderContext.maxPacketSize;
        retSender->maxBitrate = (config->maxBitrate > 0) ? config->maxBitrate : 0;
        int totalBufSize = 0, minStreamSocketSendBufferSize = 0;
        if (config->maxNetworkLatencyMs > 0)
        {
            totalBufSize = (config->maxNetworkLatencyMs > 0) ? retSender->maxBitrate * config->maxNetworkLatencyMs / 1000 / 8 : 0;
            minStreamSocketSendBufferSize = (retSender->maxBitrate > 0) ? retSender->maxBitrate * 50 / 1000 / 8 : ARSTREAM2_RTP_SENDER_DEFAULT_MIN_STREAM_SOCKET_SEND_BUFFER_SIZE;
            retSender->streamSocketSendBufferSize = (totalBufSize / 4 > minStreamSocketSendBufferSize) ? totalBufSize / 4 : minStreamSocketSendBufferSize;
        }
        else
        {
            totalBufSize = (config->maxLatencyMs > 0) ? retSender->maxBitrate * config->maxLatencyMs / 1000 / 8 : 0;
            minStreamSocketSendBufferSize = (retSender->maxBitrate > 0) ? retSender->maxBitrate * 50 / 1000 / 8 : ARSTREAM2_RTP_SENDER_DEFAULT_MIN_STREAM_SOCKET_SEND_BUFFER_SIZE;
            retSender->streamSocketSendBufferSize = (totalBufSize / 6 > minStreamSocketSendBufferSize) ? totalBufSize / 6 : minStreamSocketSendBufferSize;
        }
        retSender->maxLatencyUs = (config->maxLatencyMs > 0) ? config->maxLatencyMs * 1000 - ((retSender->maxBitrate > 0) ? (int)((uint64_t)retSender->streamSocketSendBufferSize * 8 * 1000000 / retSender->maxBitrate) : 0) : 0;
        retSender->maxNetworkLatencyUs = (config->maxNetworkLatencyMs > 0) ? config->maxNetworkLatencyMs * 1000 - ((retSender->maxBitrate > 0) ? (int)((uint64_t)retSender->streamSocketSendBufferSize * 8 * 1000000 / retSender->maxBitrate) : 0) : 0;
        retSender->naluFifoBufferSize = totalBufSize - retSender->streamSocketSendBufferSize;
        retSender->rtpSenderContext.senderSsrc = ARSTREAM2_RTP_SENDER_SSRC;
        retSender->rtpSenderContext.rtpClockRate = 90000;
        retSender->rtpSenderContext.rtpTimestampOffset = 0;
        retSender->rtcpSenderContext.senderSsrc = ARSTREAM2_RTP_SENDER_SSRC;
        retSender->rtcpSenderContext.rtcpByteRate = (retSender->maxBitrate > 0) ? retSender->maxBitrate * ARSTREAM2_RTCP_SENDER_BANDWIDTH_SHARE / 8 : ARSTREAM2_RTCP_SENDER_DEFAULT_BITRATE / 8;
        retSender->rtcpSenderContext.rtpClockRate = 90000;
        retSender->rtcpSenderContext.rtpTimestampOffset = 0;

        if (retSender->rtpSenderContext.maxPacketSize < sizeof(ARSTREAM2_RTCP_SenderReport_t))
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Config: max packet size is too small to hold a sender report");
            internalError = ARSTREAM2_ERROR_BAD_PARAMETERS;
        }
    }

    if (internalError == ARSTREAM2_OK)
    {
        if (pipe(retSender->naluFifoPipe) != 0)
        {
            internalError = ARSTREAM2_ERROR_RESOURCE_UNAVAILABLE;
        }
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
        int naluFifoRet = ARSTREAM2_RTPH264_FifoInit(&retSender->naluFifo, config->naluFifoSize);
        if (naluFifoRet != 0)
        {
            internalError = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            naluFifoWasCreated = 1;
        }
    }
    if (internalError == ARSTREAM2_OK)
    {
        int mutexInitRet = ARSAL_Mutex_Init(&(retSender->naluFifoMutex));
        if (mutexInitRet != 0)
        {
            internalError = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            naluFifoMutexWasInit = 1;
        }
    }

    /* Setup the packet FIFO */
    if (internalError == ARSTREAM2_OK)
    {
        int packetFifoRet = ARSTREAM2_RTP_FifoInit(&retSender->packetFifo, config->naluFifoSize, retSender->rtpSenderContext.maxPacketSize); //TODO: size?
        if (packetFifoRet != 0)
        {
            internalError = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            packetFifoWasCreated = 1;
        }
    }

    /* Stream socket setup */
    if (internalError == ARSTREAM2_OK)
    {
        int socketRet = ARSTREAM2_RtpSender_StreamSocketSetup(retSender);
        if (socketRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Failed to setup the stream socket (error %d)", socketRet);
            internalError = ARSTREAM2_ERROR_RESOURCE_UNAVAILABLE;
        }
    }

    /* Control socket setup */
    if (internalError == ARSTREAM2_OK)
    {
        int socketRet = ARSTREAM2_RtpSender_ControlSocketSetup(retSender);
        if (socketRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Failed to setup the control socket (error %d)", socketRet);
            internalError = ARSTREAM2_ERROR_RESOURCE_UNAVAILABLE;
        }
    }

    /* RTCP message buffer */
    if (internalError == ARSTREAM2_OK)
    {
        retSender->rtcpMsgBuffer = malloc(retSender->rtpSenderContext.maxPacketSize);
        if (retSender->rtcpMsgBuffer == NULL)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Memory allocation failed (%d)", retSender->rtpSenderContext.maxPacketSize);
            internalError = ARSTREAM2_ERROR_ALLOC;
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
            fprintf(retSender->fMonitorOut, "ntpTimestamp inputTimestamp outputTimestamp rtpTimestamp rtpSeqNum rtpMarkerBit bytesSent bytesDropped\n");
        }
    }
#endif //#ifdef ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT

    if ((internalError != ARSTREAM2_OK) &&
        (retSender != NULL))
    {
        if (retSender->streamSocket != -1)
        {
            ARSAL_Socket_Close(retSender->streamSocket);
            retSender->streamSocket = -1;
        }
        if (retSender->controlSocket != -1)
        {
            ARSAL_Socket_Close(retSender->controlSocket);
            retSender->controlSocket = -1;
        }
        if (retSender->naluFifoPipe[0] != -1)
        {
            close(retSender->naluFifoPipe[0]);
            retSender->naluFifoPipe[0] = -1;
        }
        if (retSender->naluFifoPipe[1] != -1)
        {
            close(retSender->naluFifoPipe[1]);
            retSender->naluFifoPipe[1] = -1;
        }
        if (streamMutexWasInit == 1)
        {
            ARSAL_Mutex_Destroy(&(retSender->streamMutex));
        }
        if (monitoringMutexWasInit == 1)
        {
            ARSAL_Mutex_Destroy(&(retSender->monitoringMutex));
        }
        if (packetFifoWasCreated == 1)
        {
            ARSTREAM2_RTP_FifoFree(&retSender->packetFifo);
        }
        if (naluFifoWasCreated == 1)
        {
            ARSTREAM2_RTPH264_FifoFree(&retSender->naluFifo);
        }
        if (naluFifoMutexWasInit == 1)
        {
            ARSAL_Mutex_Destroy(&(retSender->naluFifoMutex));
        }
        if (retSender->rtcpMsgBuffer)
        {
            free(retSender->rtcpMsgBuffer);
        }
        if (retSender->clientAddr)
        {
            free(retSender->clientAddr);
        }
        if (retSender->mcastAddr)
        {
            free(retSender->mcastAddr);
        }
        if (retSender->mcastIfaceAddr)
        {
            free(retSender->mcastIfaceAddr);
        }
#ifdef ARSTREAM2_RTP_SENDER_MONITORING_OUTPUT
        if (retSender->fMonitorOut)
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
        sender->threadShouldStop = 1;
        ARSAL_Mutex_Unlock(&(sender->streamMutex));
        /* signal the sending thread to avoid a deadlock */
        if (sender->naluFifoPipe[1] != -1)
        {
            char * buff = "x";
            write(sender->naluFifoPipe[1], buff, 1);
        }
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
        if ((*sender)->threadStarted == 0)
        {
            canDelete = 1;
        }
        ARSAL_Mutex_Unlock(&((*sender)->streamMutex));

        if (canDelete == 1)
        {
            ARSAL_Mutex_Destroy(&((*sender)->streamMutex));
            ARSAL_Mutex_Destroy(&((*sender)->monitoringMutex));
            ARSTREAM2_RTP_FifoFree(&(*sender)->packetFifo);
            ARSTREAM2_RTPH264_FifoFree(&(*sender)->naluFifo);
            ARSAL_Mutex_Destroy(&((*sender)->naluFifoMutex));
            if ((*sender)->naluFifoPipe[0] != -1)
            {
                close((*sender)->naluFifoPipe[0]);
                (*sender)->naluFifoPipe[0] = -1;
            }
            if ((*sender)->naluFifoPipe[1] != -1)
            {
                close((*sender)->naluFifoPipe[1]);
                (*sender)->naluFifoPipe[1] = -1;
            }
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
            if ((*sender)->rtcpMsgBuffer)
            {
                free((*sender)->rtcpMsgBuffer);
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


eARSTREAM2_ERROR ARSTREAM2_RtpSender_SendNewNalu(ARSTREAM2_RtpSender_t *sender, const ARSTREAM2_RtpSender_H264NaluDesc_t *nalu, uint64_t inputTime)
{
    eARSTREAM2_ERROR retVal = ARSTREAM2_OK;
    int res;

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

    if (retVal == ARSTREAM2_OK)
    {
        ARSAL_Mutex_Lock(&(sender->streamMutex));
        if (!sender->threadStarted)
        {
            retVal = ARSTREAM2_ERROR_BAD_PARAMETERS;
        }
        ARSAL_Mutex_Unlock(&(sender->streamMutex));
    }

    if (retVal == ARSTREAM2_OK)
    {
        ARSAL_Mutex_Lock(&(sender->naluFifoMutex));

        ARSTREAM2_RTPH264_NaluFifoItem_t *item = ARSTREAM2_RTPH264_FifoPopFreeItem(&sender->naluFifo);
        if (item)
        {
            item->nalu.inputTimestamp = inputTime;
            uint64_t timeoutTimestamp1 = (sender->maxLatencyUs > 0) ? nalu->auTimestamp + sender->maxLatencyUs : 0;
            uint64_t timeoutTimestamp2 = ((sender->maxNetworkLatencyUs > 0) && (inputTime > 0)) ? inputTime + sender->maxNetworkLatencyUs : 0;
            item->nalu.timeoutTimestamp = (timeoutTimestamp1 > timeoutTimestamp2) ? timeoutTimestamp1 : timeoutTimestamp2;
            item->nalu.ntpTimestamp = nalu->auTimestamp;
            item->nalu.isLastInAu = nalu->isLastNaluInAu;
            item->nalu.seqNumForcedDiscontinuity = nalu->seqNumForcedDiscontinuity;
            item->nalu.metadata = nalu->auMetadata;
            item->nalu.metadataSize = nalu->auMetadataSize;
            item->nalu.nalu = nalu->naluBuffer;
            item->nalu.naluSize = nalu->naluSize;
            item->nalu.auUserPtr = nalu->auUserPtr;
            item->nalu.naluUserPtr = nalu->naluUserPtr;

            res = ARSTREAM2_RTPH264_FifoEnqueueItem(&sender->naluFifo, item);
            if (res != 0)
            {
                ARSTREAM2_RTPH264_FifoPushFreeItem(&sender->naluFifo, item);
                retVal = ARSTREAM2_ERROR_INVALID_STATE;
            }
        }
        else
        {
            retVal = ARSTREAM2_ERROR_QUEUE_FULL;
        }

        ARSAL_Mutex_Unlock(&(sender->naluFifoMutex));
        if (sender->naluFifoPipe[1] != -1)
        {
            char * buff = "x";
            write(sender->naluFifoPipe[1], buff, 1);
        }
    }

    return retVal;
}


eARSTREAM2_ERROR ARSTREAM2_RtpSender_SendNNewNalu(ARSTREAM2_RtpSender_t *sender, const ARSTREAM2_RtpSender_H264NaluDesc_t *nalu, int naluCount, uint64_t inputTime)
{
    eARSTREAM2_ERROR retVal = ARSTREAM2_OK;
    int k, res;

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

    if (retVal == ARSTREAM2_OK)
    {
        ARSAL_Mutex_Lock(&(sender->streamMutex));
        if (!sender->threadStarted)
        {
            retVal = ARSTREAM2_ERROR_BAD_PARAMETERS;
        }
        ARSAL_Mutex_Unlock(&(sender->streamMutex));
    }

    if (retVal == ARSTREAM2_OK)
    {
        ARSAL_Mutex_Lock(&(sender->naluFifoMutex));

        for (k = 0; k < naluCount; k++)
        {
            ARSTREAM2_RTPH264_NaluFifoItem_t *item = ARSTREAM2_RTPH264_FifoPopFreeItem(&sender->naluFifo);
            if (item)
            {
                item->nalu.inputTimestamp = inputTime;
                uint64_t timeoutTimestamp1 = (sender->maxLatencyUs > 0) ? nalu[k].auTimestamp + sender->maxLatencyUs : 0;
                uint64_t timeoutTimestamp2 = ((sender->maxNetworkLatencyUs > 0) && (inputTime > 0)) ? inputTime + sender->maxNetworkLatencyUs : 0;
                item->nalu.timeoutTimestamp = (timeoutTimestamp1 > timeoutTimestamp2) ? timeoutTimestamp1 : timeoutTimestamp2;
                item->nalu.ntpTimestamp = nalu[k].auTimestamp;
                item->nalu.isLastInAu = nalu[k].isLastNaluInAu;
                item->nalu.seqNumForcedDiscontinuity = nalu[k].seqNumForcedDiscontinuity;
                item->nalu.metadata = nalu[k].auMetadata;
                item->nalu.metadataSize = nalu[k].auMetadataSize;
                item->nalu.nalu = nalu[k].naluBuffer;
                item->nalu.naluSize = nalu[k].naluSize;
                item->nalu.auUserPtr = nalu[k].auUserPtr;
                item->nalu.naluUserPtr = nalu[k].naluUserPtr;

                res = ARSTREAM2_RTPH264_FifoEnqueueItem(&sender->naluFifo, item);
                if (res != 0)
                {
                    ARSTREAM2_RTPH264_FifoPushFreeItem(&sender->naluFifo, item);
                    retVal = ARSTREAM2_ERROR_INVALID_STATE;
                    break;
                }
            }
            else
            {
                retVal = ARSTREAM2_ERROR_QUEUE_FULL;
                break;
            }
        }

        ARSAL_Mutex_Unlock(&(sender->naluFifoMutex));
        if (sender->naluFifoPipe[1] != -1)
        {
            char * buff = "x";
            write(sender->naluFifoPipe[1], buff, 1);
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


void* ARSTREAM2_RtpSender_RunThread(void *ARSTREAM2_RtpSender_t_Param)
{
    /* Local declarations */
    ARSTREAM2_RtpSender_t *sender = (ARSTREAM2_RtpSender_t*)ARSTREAM2_RtpSender_t_Param;
    int shouldStop, ret, selectRet, packetsPending = 0;
    struct timespec t1;
    uint64_t curTime;
    uint32_t nextSrDelay = ARSTREAM2_RTCP_SENDER_MIN_PACKET_TIME_INTERVAL;
    fd_set readSet, readSetSaved;
    fd_set writeSet;
    fd_set exceptSet, exceptSetSaved;
    int maxFd;
    struct timeval tv;

    /* Parameters check */
    if (sender == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Cannot start thread: bad context parameter");
        return (void *)0;
    }

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_SENDER_TAG, "Sender thread running");
    ARSAL_Mutex_Lock(&(sender->streamMutex));
    sender->threadStarted = 1;
    shouldStop = sender->threadShouldStop;
    ARSAL_Mutex_Unlock(&(sender->streamMutex));

    FD_ZERO(&readSetSaved);
    FD_SET(sender->naluFifoPipe[0], &readSetSaved);
    FD_SET(sender->controlSocket, &readSetSaved);
    FD_ZERO(&writeSet);
    FD_ZERO(&exceptSetSaved);
    FD_SET(sender->naluFifoPipe[0], &exceptSetSaved);
    FD_SET(sender->streamSocket, &exceptSetSaved);
    FD_SET(sender->controlSocket, &exceptSetSaved);
    maxFd = sender->naluFifoPipe[0];
    if (sender->streamSocket > maxFd) maxFd = sender->streamSocket;
    if (sender->controlSocket > maxFd) maxFd = sender->controlSocket;
    maxFd++;
    readSet = readSetSaved;
    exceptSet = exceptSetSaved;
    tv.tv_sec = 0;
    tv.tv_usec = ARSTREAM2_RTP_SENDER_TIMEOUT_US;

    while (shouldStop == 0)
    {
        selectRet = select(maxFd, &readSet, &writeSet, &exceptSet, &tv);

        ARSAL_Time_GetTime(&t1);
        curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

        if (FD_ISSET(sender->streamSocket, &exceptSet))
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Exception on stream socket");
        }
        if (FD_ISSET(sender->controlSocket, &exceptSet))
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Exception on control socket");
        }
        if (selectRet < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Select error (%d): %s", errno, strerror(errno));
        }

        /* RTCP receiver reports */
        if ((selectRet >= 0) && (FD_ISSET(sender->controlSocket, &readSet)))
        {
            /* The control socket is ready for reading */
            //TODO: recvmmsg?
            ssize_t bytes = recv(sender->controlSocket, sender->rtcpMsgBuffer, sender->rtpSenderContext.maxPacketSize, 0);
            if ((bytes < 0) && (errno != EAGAIN))
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Control socket - read error (%d): %s", errno, strerror(errno));
            }
            while (bytes > 0)
            {
                int gotReceiverReport = 0;

                ret = ARSTREAM2_RTCP_Sender_ProcessCompoundPacket(sender->rtcpMsgBuffer, (unsigned int)bytes,
                                                                  curTime, &sender->rtcpSenderContext,
                                                                  &gotReceiverReport);
                if (ret != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Failed to process compound RTCP packet (%d)", ret);
                }

                if ((gotReceiverReport) && (sender->receiverReportCallback != NULL))
                {
                    ARSTREAM2_RtpSender_ReceiverReportData_t report;
                    memset(&report, 0, sizeof(ARSTREAM2_RtpSender_ReceiverReportData_t));
                    report.lastReceiverReportReceptionTimestamp = sender->rtcpSenderContext.lastRrReceptionTimestamp;
                    report.roundTripDelay = sender->rtcpSenderContext.roundTripDelay;
                    report.interarrivalJitter = sender->rtcpSenderContext.interarrivalJitter;
                    report.receiverLostCount = sender->rtcpSenderContext.receiverLostCount;
                    report.receiverFractionLost = sender->rtcpSenderContext.receiverFractionLost;
                    report.receiverExtHighestSeqNum = sender->rtcpSenderContext.receiverExtHighestSeqNum;
                    report.lastSenderReportInterval = sender->rtcpSenderContext.lastSrInterval;
                    report.senderReportIntervalPacketCount = sender->rtcpSenderContext.srIntervalPacketCount;
                    report.senderReportIntervalByteCount = sender->rtcpSenderContext.srIntervalByteCount;
                    report.peerClockDelta = sender->rtcpSenderContext.clockDelta.clockDeltaAvg;
                    report.roundTripDelayFromClockDelta = (uint32_t)sender->rtcpSenderContext.clockDelta.rtDelay;

                    // Call the receiver report callback function
                    sender->receiverReportCallback(&report, sender->receiverReportCallbackUserPtr);
                }

                bytes = recv(sender->controlSocket, sender->rtcpMsgBuffer, sender->rtpSenderContext.maxPacketSize, 0);
                if ((bytes < 0) && (errno != EAGAIN))
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Control socket - read error (%d): %s", errno, strerror(errno));
                }
            }
        }

        /* RTP packet FIFO cleanup (packets on timeout) */
        ret = ARSTREAM2_RTP_Sender_FifoCleanFromTimeout(&sender->rtpSenderContext, &sender->packetFifo, curTime);
        if (ret < 0)
        {
            if (ret != -2)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Failed to clean FIFO from timeout (%d)", ret);
            }
        }
        else if (ret > 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTP_SENDER_TAG, "Dropped %d packets from FIFO on timeout", ret);
        }

        /* RTP packets creation */
        ARSAL_Mutex_Lock(&(sender->naluFifoMutex));
        ret = ARSTREAM2_RTPH264_Sender_NaluFifoToPacketFifo(&sender->rtpSenderContext, &sender->naluFifo, &sender->packetFifo, curTime);
        ARSAL_Mutex_Unlock(&(sender->naluFifoMutex));
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "ARSTREAM2_RTPH264_Sender_NaluFifoToPacketFifo() failed (%d)", ret);
        }

        /* RTP packets sending */
        if ((!packetsPending) || ((packetsPending) && ((selectRet >= 0) && (FD_ISSET(sender->streamSocket, &writeSet)))))
        {
            ret = ARSTREAM2_RTP_Sender_FifoFillMsgVec(&sender->packetFifo);
            if (ret < 0)
            {
                if (ret != -2)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Failed to fill msgVec (%d)", ret);
                }
            }
            else if (ret > 0)
            {
                int msgVecCount = ret;
                int msgVecSentCount = 0;

                packetsPending = 1;
                ret = sendmmsg(sender->streamSocket, sender->packetFifo.msgVec, msgVecCount, 0);
                if (ret < 0)
                {
                    if (errno == EAGAIN)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTP_SENDER_TAG, "Stream socket buffer full (no packets dropped, will retry later) - sendmmsg error (%d): %s", errno, strerror(errno));
                        int i;
                        for (i = 0, msgVecSentCount = 0; i < msgVecCount; i++)
                        {
                            if (sender->packetFifo.msgVec[i].msg_len > 0) msgVecSentCount++;
                        }
                        packetsPending = (msgVecSentCount < msgVecCount) ? 1 : 0;
                        //if (packetsPending) ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTP_SENDER_TAG, "Sent %d packets out of %d (socket buffer is full)", msgVecSentCount, msgVecCount); //TODO: debug
                    }
                    else
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Stream socket - sendmmsg error (%d): %s", errno, strerror(errno));
                    }
                }
                else
                {
                    msgVecSentCount = ret;
                    packetsPending = (msgVecSentCount < msgVecCount) ? 1 : 0;
                    //if (packetsPending) ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_RTP_SENDER_TAG, "Sent %d packets out of %d", msgVecSentCount, msgVecCount); //TODO: debug
                }

                ret = ARSTREAM2_RTP_Sender_FifoCleanFromMsgVec(&sender->rtpSenderContext, &sender->packetFifo, msgVecSentCount, curTime);
                if (ret < 0)
                {
                    if (ret != -2)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Failed to clean FIFO from msgVec (%d)", ret);
                    }
                }
            }
        }

        /* RTCP sender reports */
        uint32_t srDelay = (uint32_t)(curTime - sender->rtcpSenderContext.lastSrTimestamp);
        if (srDelay >= nextSrDelay)
        {
            unsigned int size = 0;

            ret = ARSTREAM2_RTCP_Sender_GenerateCompoundPacket(sender->rtcpMsgBuffer, sender->rtpSenderContext.maxPacketSize, curTime, 1, 1, 1,
                                                               ARSTREAM2_RTP_SENDER_CNAME, sender->rtpSenderContext.packetCount,
                                                               sender->rtpSenderContext.byteCount, &sender->rtcpSenderContext, &size);

            if ((ret == 0) && (size > 0))
            {
                ssize_t bytes = send(sender->controlSocket, sender->rtcpMsgBuffer, size, 0);
                if (bytes < 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_RTP_SENDER_TAG, "Control socket - send error (%d): %s", errno, strerror(errno));
                }
            }

            nextSrDelay = (size + ARSTREAM2_RTP_UDP_HEADER_SIZE + ARSTREAM2_RTP_IP_HEADER_SIZE) * 1000000 / sender->rtcpSenderContext.rtcpByteRate;
            if (nextSrDelay < ARSTREAM2_RTCP_SENDER_MIN_PACKET_TIME_INTERVAL) nextSrDelay = ARSTREAM2_RTCP_SENDER_MIN_PACKET_TIME_INTERVAL;
        }

        if ((selectRet >= 0) && (FD_ISSET(sender->naluFifoPipe[0], &readSet)))
        {
            /* Dump bytes (so it won't be ready next time) */
            char dump[10];
            read(sender->naluFifoPipe[0], &dump, 10);
        }

        ARSAL_Mutex_Lock(&(sender->streamMutex));
        shouldStop = sender->threadShouldStop;
        ARSAL_Mutex_Unlock(&(sender->streamMutex));
        
        if (!shouldStop)
        {
            /* Prepare the next select */
            readSet = readSetSaved;
            exceptSet = exceptSetSaved;
            FD_ZERO(&writeSet);
            if (packetsPending) FD_SET(sender->streamSocket, &writeSet);
            tv.tv_sec = 0;
            tv.tv_usec = ARSTREAM2_RTP_SENDER_TIMEOUT_US;
        }
    }

    ARSAL_Mutex_Lock(&(sender->streamMutex));
    sender->threadStarted = 0;
    ARSAL_Mutex_Unlock(&(sender->streamMutex));

    /* cancel the last Access Unit */
    if (sender->rtpSenderContext.auCallback != NULL)
    {
        if (sender->rtpSenderContext.previousTimestamp != sender->rtpSenderContext.lastAuCallbackTimestamp)
        {
            sender->rtpSenderContext.lastAuCallbackTimestamp = sender->rtpSenderContext.previousTimestamp;

            /* call the auCallback */
            ((ARSTREAM2_RtpSender_AuCallback_t)sender->rtpSenderContext.auCallback)(ARSTREAM2_RTP_SENDER_STATUS_CANCELLED, sender->rtpSenderContext.previousAuUserPtr, sender->rtpSenderContext.auCallbackUserPtr);
        }
    }

    /* flush the NALU FIFO */
    ARSTREAM2_RtpSender_FlushNaluFifo(sender);

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_RTP_SENDER_TAG, "Sender thread ended");

    return (void*)0;
}


eARSTREAM2_ERROR ARSTREAM2_RtpSender_GetMonitoring(ARSTREAM2_RtpSender_t *sender, uint64_t startTime, uint32_t timeIntervalUs,
                                                   ARSTREAM2_RtpSender_MonitoringData_t *monitoringData)
{
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;
    uint64_t endTime, curTime, previousTime, acqToNetworkSum = 0, networkSum = 0, acqToNetworkVarSum = 0, networkVarSum = 0, packetSizeVarSum = 0;
    uint32_t bytesSent, bytesDropped, bytesSentSum = 0, bytesDroppedSum = 0, meanPacketSize = 0, acqToNetworkTime = 0, networkTime = 0;
    uint32_t acqToNetworkJitter = 0, networkJitter = 0, meanAcqToNetworkTime = 0, meanNetworkTime = 0, packetSizeStdDev = 0;
    uint32_t minAcqToNetworkTime = (uint32_t)(-1), minNetworkTime = (uint32_t)(-1), minPacketSize = (uint32_t)(-1);
    uint32_t maxAcqToNetworkTime = 0, maxNetworkTime = 0, maxPacketSize = 0;
    int points = 0, usefulPoints = 0, packetsSent = 0, packetsDropped = 0, idx, i, firstUsefulIdx = -1;

    if ((sender == NULL) || (timeIntervalUs == 0) || (monitoringData == NULL))
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
            curTime = sender->monitoringPoint[idx].outputTimestamp;
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
            bytesSent = sender->monitoringPoint[idx].bytesSent;
            bytesSentSum += bytesSent;
            if (bytesSent)
            {
                packetsSent++;
                acqToNetworkTime = curTime - sender->monitoringPoint[idx].ntpTimestamp;
                acqToNetworkSum += acqToNetworkTime;
                networkTime = curTime - sender->monitoringPoint[idx].inputTimestamp;
                networkSum += networkTime;
                if (acqToNetworkTime < minAcqToNetworkTime) minAcqToNetworkTime = acqToNetworkTime;
                if (acqToNetworkTime > maxAcqToNetworkTime) maxAcqToNetworkTime = acqToNetworkTime;
                if (networkTime < minNetworkTime) minNetworkTime = networkTime;
                if (networkTime > maxNetworkTime) maxNetworkTime = networkTime;
                if (bytesSent < minPacketSize) minPacketSize = bytesSent;
                if (bytesSent > maxPacketSize) maxPacketSize = bytesSent;
            }
            bytesDropped = sender->monitoringPoint[idx].bytesDropped;
            bytesDroppedSum += bytesDropped;
            if (bytesDropped)
            {
                packetsDropped++;
            }
            previousTime = curTime;
            usefulPoints++;
            points++;
            idx = (idx - 1 >= 0) ? idx - 1 : ARSTREAM2_RTP_SENDER_MONITORING_MAX_POINTS - 1;
        }

        endTime = previousTime;
        meanPacketSize = (packetsSent > 0) ? (bytesSentSum / packetsSent) : 0;
        meanAcqToNetworkTime = (packetsSent > 0) ? (uint32_t)(acqToNetworkSum / packetsSent) : 0;
        meanNetworkTime = (packetsSent > 0) ? (uint32_t)(networkSum / packetsSent) : 0;

        for (i = 0, idx = firstUsefulIdx; i < usefulPoints; i++)
        {
            idx = (idx - 1 >= 0) ? idx - 1 : ARSTREAM2_RTP_SENDER_MONITORING_MAX_POINTS - 1;
            curTime = sender->monitoringPoint[idx].outputTimestamp;
            bytesSent = sender->monitoringPoint[idx].bytesSent;
            if (bytesSent)
            {
                acqToNetworkTime = curTime - sender->monitoringPoint[idx].ntpTimestamp;
                networkTime = curTime - sender->monitoringPoint[idx].inputTimestamp;
                packetSizeVarSum += ((bytesSent - meanPacketSize) * (bytesSent - meanPacketSize));
                acqToNetworkVarSum += ((acqToNetworkTime - meanAcqToNetworkTime) * (acqToNetworkTime - meanAcqToNetworkTime));
                networkVarSum += ((networkTime - meanNetworkTime) * (networkTime - meanNetworkTime));
            }
        }
        acqToNetworkJitter = (packetsSent > 0) ? (uint32_t)(sqrt((double)acqToNetworkVarSum / packetsSent)) : 0;
        networkJitter = (packetsSent > 0) ? (uint32_t)(sqrt((double)networkVarSum / packetsSent)) : 0;
        packetSizeStdDev = (packetsSent > 0) ? (uint32_t)(sqrt((double)packetSizeVarSum / packetsSent)) : 0;
    }

    ARSAL_Mutex_Unlock(&(sender->monitoringMutex));

    monitoringData->realTimeIntervalUs = (startTime - endTime);
    monitoringData->acqToNetworkTimeMin = minAcqToNetworkTime;
    monitoringData->acqToNetworkTimeMax = maxAcqToNetworkTime;
    monitoringData->acqToNetworkTimeMean = meanAcqToNetworkTime;
    monitoringData->acqToNetworkTimeJitter = acqToNetworkJitter;
    monitoringData->networkTimeMin = minNetworkTime;
    monitoringData->networkTimeMax = maxNetworkTime;
    monitoringData->networkTimeMean = meanNetworkTime;
    monitoringData->networkTimeJitter = networkJitter;
    monitoringData->bytesSent = bytesSentSum;
    monitoringData->packetSizeMin = minPacketSize;
    monitoringData->packetSizeMax = maxPacketSize;
    monitoringData->packetSizeMean = meanPacketSize;
    monitoringData->packetSizeStdDev = packetSizeStdDev;
    monitoringData->packetsSent = packetsSent;
    monitoringData->bytesDropped = bytesDroppedSum;
    monitoringData->packetsDropped = packetsDropped;

    return ret;
}
