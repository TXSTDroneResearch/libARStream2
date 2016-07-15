/**
 * @file arstream2_stream_recorder.c
 * @brief Parrot Streaming Library - Stream Recorder
 * @date 06/01/2016
 * @author aurelien.barre@parrot.com
 */

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <math.h>

#include <libARSAL/ARSAL_Print.h>
#include <libARSAL/ARSAL_Mutex.h>
#if BUILD_LIBARMEDIA
#include <libARMedia/ARMedia.h>
#endif

#include <libARStream2/arstream2_stream_recorder.h>


#define ARSTREAM2_STREAM_RECORDER_TAG "ARSTREAM2_StreamRecorder"

#define ARSTREAM2_STREAM_RECORDER_FIFO_COND_TIMEOUT_MS (500)
#define ARSTREAM2_STREAM_RECORDER_FILE_SYNC_MAX_INTERVAL (30)


//TODO: metadata definitions should be removed when the definitions will be available in a public ARSDK library

#define ARSTREAM2_STREAM_RECORDER_GPS_ALTITUDE_MASK   (0xFFFFFF00)                          /**< GPS altitude mask */
#define ARSTREAM2_STREAM_RECORDER_GPS_ALTITUDE_SHIFT  (8)                                   /**< GPS altitude shift */
#define ARSTREAM2_STREAM_RECORDER_GPS_SV_COUNT_MASK   (0x000000FF)                          /**< GPS SV count mask */
#define ARSTREAM2_STREAM_RECORDER_GPS_SV_COUNT_SHIFT  (0)                                   /**< GPS SV count shift */
#define ARSTREAM2_STREAM_RECORDER_FLYING_STATE_MASK   (0x7F)                                /**< Flying state mask */
#define ARSTREAM2_STREAM_RECORDER_FLYING_STATE_SHIFT  (0)                                   /**< Flying state shift */
#define ARSTREAM2_STREAM_RECORDER_BINNING_MASK        (0x80)                                /**< Binning mask */
#define ARSTREAM2_STREAM_RECORDER_BINNING_SHIFT       (7)                                   /**< Binning shift */
#define ARSTREAM2_STREAM_RECORDER_PILOTING_MODE_MASK  (0x7F)                                /**< Piloting mode mask */
#define ARSTREAM2_STREAM_RECORDER_PILOTING_MODE_SHIFT (0)                                   /**< Piloting mode shift */
#define ARSTREAM2_STREAM_RECORDER_ANIMATION_MASK      (0x80)                                /**< Animation mask */
#define ARSTREAM2_STREAM_RECORDER_ANIMATION_SHIFT     (7)                                   /**< Animation shift */

/**
 * @brief Flying states.
 */
typedef enum
{
    ARSTREAM2_STREAM_RECORDER_FLYING_STATE_LANDED = 0,       /**< Landed state */
    ARSTREAM2_STREAM_RECORDER_FLYING_STATE_TAKINGOFF,        /**< Taking off state */
    ARSTREAM2_STREAM_RECORDER_FLYING_STATE_HOVERING,         /**< Hovering state */
    ARSTREAM2_STREAM_RECORDER_FLYING_STATE_FLYING,           /**< Flying state */
    ARSTREAM2_STREAM_RECORDER_FLYING_STATE_LANDING,          /**< Landing state */
    ARSTREAM2_STREAM_RECORDER_FLYING_STATE_EMERGENCY,        /**< Emergency state */

} ARSTREAM2_STREAM_RECORDER_FlyingState_t;


/**
 * @brief Flying states.
 */
typedef enum
{
    ARSTREAM2_STREAM_RECORDER_PILOTING_MODE_MANUAL = 0,      /**< Manual piloting by the user */
    ARSTREAM2_STREAM_RECORDER_PILOTING_MODE_RETURN_HOME,     /**< Automatic return home in progress */
    ARSTREAM2_STREAM_RECORDER_PILOTING_MODE_FLIGHT_PLAN,     /**< Automatic flight plan in progress */
    ARSTREAM2_STREAM_RECORDER_PILOTING_MODE_FOLLOW_ME,       /**< Automatic "follow-me" in progress */

} ARSTREAM2_STREAM_RECORDER_PilotingMode_t;


/**
 * "Parrot Video Streaming Metadata" v1 specific identifier.
 */
#define ARSTREAM2_STREAM_RECORDER_PARROT_VIDEO_STREAMING_METADATA_V1_ID 0x5031


/**
 * @brief "Parrot Video Streaming Metadata" v1 basic definition.
 */
typedef struct
{
    uint16_t specific;             /**< Identifier = 0x5031 */
    uint16_t length;               /**< Size in 32 bits words = 5 */
    int16_t  droneYaw;             /**< Drone yaw/psi (rad), Q4.12 */
    int16_t  dronePitch;           /**< Drone pitch/theta (rad), Q4.12 */
    int16_t  droneRoll;            /**< Drone roll/phi (rad), Q4.12 */
    int16_t  cameraPan;            /**< Camera pan (rad), Q4.12 */
    int16_t  cameraTilt;           /**< Camera tilt (rad), Q4.12 */
    int16_t  frameW;               /**< Frame view quaternion W, Q4.12 */
    int16_t  frameX;               /**< Frame view quaternion X, Q4.12 */
    int16_t  frameY;               /**< Frame view quaternion Y, Q4.12 */
    int16_t  frameZ;               /**< Frame view quaternion Z, Q4.12 */
    uint16_t exposureTime;         /**< Frame exposure time (ms), Q8.8 */
    uint16_t gain;                 /**< Frame ISO gain */
    int8_t   wifiRssi;             /**< Wifi RSSI (dBm) */
    uint8_t  batteryPercentage;    /**< Battery charge percentage */

} ARSTREAM2_STREAM_RECORDER_ParrotVideoStreamingMetadataV1Basic_t;


/**
 * @brief "Parrot Video Streaming Metadata" v1 extended definition.
 */
typedef struct
{
    uint16_t specific;             /**< Identifier = 0x5031 */
    uint16_t length;               /**< Size in 32 bits words = 12 */
    int16_t  droneYaw;             /**< Drone yaw/psi (rad), Q4.12 */
    int16_t  dronePitch;           /**< Drone pitch/theta (rad), Q4.12 */
    int16_t  droneRoll;            /**< Drone roll/phi (rad), Q4.12 */
    int16_t  cameraPan;            /**< Camera pan (rad), Q4.12 */
    int16_t  cameraTilt;           /**< Camera tilt (rad), Q4.12 */
    int16_t  frameW;               /**< Frame view quaternion W, Q4.12 */
    int16_t  frameX;               /**< Frame view quaternion X, Q4.12 */
    int16_t  frameY;               /**< Frame view quaternion Y, Q4.12 */
    int16_t  frameZ;               /**< Frame view quaternion Z, Q4.12 */
    uint16_t exposureTime;         /**< Frame exposure time (ms), Q8.8 */
    uint16_t gain;                 /**< Frame ISO gain */
    int8_t   wifiRssi;             /**< Wifi RSSI (dBm) */
    uint8_t  batteryPercentage;    /**< Battery charge percentage */
    int32_t  gpsLatitude;          /**< GPS latitude (deg), Q12.20 */
    int32_t  gpsLongitude;         /**< GPS longitude (deg), Q12.20 */
    int32_t  gpsAltitudeAndSv;     /**< Bits 31..8 = GPS altitude (m) Q16.8, bits 7..0 = GPS SV count */
    int32_t  altitude;             /**< Altitude relative to take-off (m), Q16.16 */
    uint32_t distanceFromHome;     /**< Distance from home (m), Q16.16 */
    int16_t  xSpeed;               /**< X speed (m/s), Q8.8 */
    int16_t  ySpeed;               /**< Y speed (m/s), Q8.8 */
    int16_t  zSpeed;               /**< Z speed (m/s), Q8.8 */
    uint8_t  state;                /**< Bit 7 = binning, bits 6..0 = flyingState */
    uint8_t  mode;                 /**< Bit 7 = animation, bits 6..0 = pilotingMode */

} ARSTREAM2_STREAM_RECORDER_ParrotVideoStreamingMetadataV1Extended_t;


/**
 * "Parrot Video Recording Metadata" v1 MIME format.
 */
#define ARSTREAM2_STREAM_RECORDER_PARROT_VIDEO_RECORDING_METADATA_V1_MIME_FORMAT "application/octet-stream;type=com.parrot.videometadata1"


/**
 * "Parrot Video Recording Metadata" v1 content encoding.
 */
#define ARSTREAM2_STREAM_RECORDER_PARROT_VIDEO_RECORDING_METADATA_V1_CONTENT_ENCODING ""


/**
 * @brief "Parrot Video Recording Metadata" v1 definition.
 */
typedef struct
{
    uint32_t frameTimestampH;      /**< Frame timestamp (µs, monotonic), high 32 bits */
    uint32_t frameTimestampL;      /**< Frame timestamp (µs, monotonic), low 32 bits */
    int16_t  droneYaw;             /**< Drone yaw/psi (rad), Q4.12 */
    int16_t  dronePitch;           /**< Drone pitch/theta (rad), Q4.12 */
    int16_t  droneRoll;            /**< Drone roll/phi (rad), Q4.12 */
    int16_t  cameraPan;            /**< Camera pan (rad), Q4.12 */
    int16_t  cameraTilt;           /**< Camera tilt (rad), Q4.12 */
    int16_t  frameW;               /**< Frame view quaternion W, Q4.12 */
    int16_t  frameX;               /**< Frame view quaternion X, Q4.12 */
    int16_t  frameY;               /**< Frame view quaternion Y, Q4.12 */
    int16_t  frameZ;               /**< Frame view quaternion Z, Q4.12 */
    uint16_t exposureTime;         /**< Frame exposure time (ms), Q8.8 */
    uint16_t gain;                 /**< Frame ISO gain */
    int8_t   wifiRssi;             /**< Wifi RSSI (dBm) */
    uint8_t  batteryPercentage;    /**< Battery charge percentage */
    int32_t  gpsLatitude;          /**< GPS latitude (deg), Q12.20 */
    int32_t  gpsLongitude;         /**< GPS longitude (deg), Q12.20 */
    int32_t  gpsAltitudeAndSv;     /**< Bits 31..8 = GPS altitude (m) Q16.8, bits 7..0 = GPS SV count */
    int32_t  altitude;             /**< Altitude relative to take-off (m), Q16.16 */
    uint32_t distanceFromHome;     /**< Distance from home (m), Q16.16 */
    int16_t  xSpeed;               /**< X speed (m/s), Q8.8 */
    int16_t  ySpeed;               /**< Y speed (m/s), Q8.8 */
    int16_t  zSpeed;               /**< Z speed (m/s), Q8.8 */
    uint8_t  state;                /**< Bit 7 = binning, bits 6..0 = flyingState */
    uint8_t  mode;                 /**< Bit 7 = animation, bits 6..0 = pilotingMode */

} ARSTREAM2_STREAM_RECORDER_ParrotVideoRecordingMetadataV1_t;


typedef enum
{
    ARSTREAM2_STREAM_RECORDER_FILE_TYPE_H264_BYTE_STREAM = 0,   /**< H.264 byte stream file format */
    ARSTREAM2_STREAM_RECORDER_FILE_TYPE_MP4,                    /**< ISO base media file format (MP4) */
    ARSTREAM2_STREAM_RECORDER_FILE_TYPE_MAX,

} eARSTREAM2_STREAM_RECORDER_FILE_TYPE;


typedef struct ARSTREAM2_StreamRecorder_AuFifoItem_s
{
    ARSTREAM2_StreamRecorder_AccessUnit_t au;

    struct ARSTREAM2_StreamRecorder_AuFifoItem_s* prev;
    struct ARSTREAM2_StreamRecorder_AuFifoItem_s* next;

} ARSTREAM2_StreamRecorder_AuFifoItem_t;


typedef struct ARSTREAM2_StreamRecorder_AuFifo_s
{
    int size;
    int count;
    ARSTREAM2_StreamRecorder_AuFifoItem_t *head;
    ARSTREAM2_StreamRecorder_AuFifoItem_t *tail;
    ARSTREAM2_StreamRecorder_AuFifoItem_t *free;
    ARSTREAM2_StreamRecorder_AuFifoItem_t *pool;

} ARSTREAM2_StreamRecorder_AuFifo_t;


typedef struct ARSTREAM2_StreamRecorder_s
{
    ARSAL_Mutex_t mutex;
    int threadShouldStop;
    int threadStarted;
    eARSTREAM2_STREAM_RECORDER_FILE_TYPE fileType;
    uint32_t videoWidth;
    uint32_t videoHeight;
    FILE *outputFile;
#if BUILD_LIBARMEDIA
    ARMEDIA_VideoEncapsuler_t* videoEncap;
#endif
    ARSTREAM2_StreamRecorder_AuFifo_t auFifo;
    ARSAL_Mutex_t fifoMutex;
    ARSAL_Cond_t fifoCond;
    ARSTREAM2_StreamRecorder_AuCallback_t auCallback;
    void *auCallbackUserPtr;
    uint32_t lastSyncIndex;
    void *recordingMetadata;
    unsigned int recordingMetadataSize;
    void *savedMetadata;
    unsigned int savedMetadataSize;

} ARSTREAM2_StreamRecorder_t;


static int ARSTREAM2_StreamRecorder_StreamingToRecordingMetadataSize(void *streamingMetadata, unsigned int streamingMetadataSize)
{
    if ((!streamingMetadata) || (streamingMetadataSize < 4))
    {
        return -1;
    }

    uint16_t specific = ntohs(*((uint16_t*)streamingMetadata));

    if (specific == ARSTREAM2_STREAM_RECORDER_PARROT_VIDEO_STREAMING_METADATA_V1_ID)
    {
        return sizeof(ARSTREAM2_STREAM_RECORDER_ParrotVideoRecordingMetadataV1_t);
    }
    else
    {
        return -1;
    }
}

static int ARSTREAM2_StreamRecorder_StreamingToRecordingMetadata(uint64_t timestamp,
                                                                 const void *streamingMetadata, unsigned int streamingMetadataSize,
                                                                 void *savedMetadata, unsigned int savedMetadataSize,
                                                                 void *recordingMetadata, unsigned int recordingMetadataSize)
{
    int ret = 0;

    if ((!streamingMetadata) || (streamingMetadataSize < 4)
            || (!recordingMetadata) || (!recordingMetadataSize)
            || (!savedMetadata) || (!savedMetadataSize))
    {
        return -1;
    }

    uint16_t specific = ntohs(*((uint16_t*)streamingMetadata));
    uint16_t length = ntohs(*((uint16_t*)streamingMetadata + 1));

    if (specific == ARSTREAM2_STREAM_RECORDER_PARROT_VIDEO_STREAMING_METADATA_V1_ID)
    {
        if (recordingMetadataSize != sizeof(ARSTREAM2_STREAM_RECORDER_ParrotVideoRecordingMetadataV1_t))
        {
            return -1;
        }
        if (savedMetadataSize != sizeof(ARSTREAM2_STREAM_RECORDER_ParrotVideoRecordingMetadataV1_t))
        {
            return -1;
        }
        ARSTREAM2_STREAM_RECORDER_ParrotVideoRecordingMetadataV1_t *recMeta = (ARSTREAM2_STREAM_RECORDER_ParrotVideoRecordingMetadataV1_t*)recordingMetadata;
        ARSTREAM2_STREAM_RECORDER_ParrotVideoRecordingMetadataV1_t *savedMeta = (ARSTREAM2_STREAM_RECORDER_ParrotVideoRecordingMetadataV1_t*)savedMetadata;
        if ((length == (sizeof(ARSTREAM2_STREAM_RECORDER_ParrotVideoStreamingMetadataV1Extended_t) - 4) / 4)
                && (streamingMetadataSize >= sizeof(ARSTREAM2_STREAM_RECORDER_ParrotVideoStreamingMetadataV1Extended_t)))
        {
            ARSTREAM2_STREAM_RECORDER_ParrotVideoStreamingMetadataV1Extended_t *streamMeta = (ARSTREAM2_STREAM_RECORDER_ParrotVideoStreamingMetadataV1Extended_t*)streamingMetadata;
            recMeta->frameTimestampH = htonl((uint32_t)(timestamp >> 32));
            recMeta->frameTimestampL = htonl((uint32_t)(timestamp & 0xFFFFFFFF));
            recMeta->droneYaw = streamMeta->droneYaw;
            recMeta->dronePitch = streamMeta->dronePitch;
            recMeta->droneRoll = streamMeta->droneRoll;
            recMeta->cameraPan = streamMeta->cameraPan;
            recMeta->cameraTilt = streamMeta->cameraTilt;
            recMeta->frameW = streamMeta->frameW;
            recMeta->frameX = streamMeta->frameX;
            recMeta->frameY = streamMeta->frameY;
            recMeta->frameZ = streamMeta->frameZ;
            recMeta->exposureTime = streamMeta->exposureTime;
            recMeta->gain = streamMeta->gain;
            recMeta->wifiRssi = streamMeta->wifiRssi;
            recMeta->batteryPercentage = streamMeta->batteryPercentage;
            recMeta->gpsLatitude = savedMeta->gpsLatitude = streamMeta->gpsLatitude;
            recMeta->gpsLongitude = savedMeta->gpsLongitude = streamMeta->gpsLongitude;
            recMeta->gpsAltitudeAndSv = savedMeta->gpsAltitudeAndSv = streamMeta->gpsAltitudeAndSv;
            recMeta->altitude = savedMeta->altitude = streamMeta->altitude;
            recMeta->distanceFromHome = savedMeta->distanceFromHome = streamMeta->distanceFromHome;
            recMeta->xSpeed = savedMeta->xSpeed = streamMeta->xSpeed;
            recMeta->ySpeed = savedMeta->ySpeed = streamMeta->ySpeed;
            recMeta->zSpeed = savedMeta->zSpeed = streamMeta->zSpeed;
            recMeta->state = savedMeta->state = streamMeta->state;
            recMeta->mode = savedMeta->mode = streamMeta->mode;
        }
        else if ((length == (sizeof(ARSTREAM2_STREAM_RECORDER_ParrotVideoStreamingMetadataV1Basic_t) - 4) / 4)
                && (streamingMetadataSize >= sizeof(ARSTREAM2_STREAM_RECORDER_ParrotVideoStreamingMetadataV1Basic_t)))
        {
            ARSTREAM2_STREAM_RECORDER_ParrotVideoStreamingMetadataV1Basic_t *streamMeta = (ARSTREAM2_STREAM_RECORDER_ParrotVideoStreamingMetadataV1Basic_t*)streamingMetadata;
            recMeta->frameTimestampH = htonl((uint32_t)(timestamp >> 32));
            recMeta->frameTimestampL = htonl((uint32_t)(timestamp & 0xFFFFFFFF));
            recMeta->droneYaw = streamMeta->droneYaw;
            recMeta->dronePitch = streamMeta->dronePitch;
            recMeta->droneRoll = streamMeta->droneRoll;
            recMeta->cameraPan = streamMeta->cameraPan;
            recMeta->cameraTilt = streamMeta->cameraTilt;
            recMeta->frameW = streamMeta->frameW;
            recMeta->frameX = streamMeta->frameX;
            recMeta->frameY = streamMeta->frameY;
            recMeta->frameZ = streamMeta->frameZ;
            recMeta->exposureTime = streamMeta->exposureTime;
            recMeta->gain = streamMeta->gain;
            recMeta->wifiRssi = streamMeta->wifiRssi;
            recMeta->batteryPercentage = streamMeta->batteryPercentage;
            recMeta->gpsLatitude = savedMeta->gpsLatitude;
            recMeta->gpsLongitude = savedMeta->gpsLongitude;
            recMeta->gpsAltitudeAndSv = savedMeta->gpsAltitudeAndSv;
            recMeta->altitude = savedMeta->altitude;
            recMeta->distanceFromHome = savedMeta->distanceFromHome;
            recMeta->xSpeed = savedMeta->xSpeed;
            recMeta->ySpeed = savedMeta->ySpeed;
            recMeta->zSpeed = savedMeta->zSpeed;
            recMeta->state = savedMeta->state;
            recMeta->mode = savedMeta->mode;
        }
        else
        {
            return -1;
        }
    }
    else
    {
        return -1;
    }

    return ret;
}


static int ARSTREAM2_StreamRecorder_FifoInit(ARSTREAM2_StreamRecorder_AuFifo_t *fifo, int maxCount)
{
    int i;
    ARSTREAM2_StreamRecorder_AuFifoItem_t* cur;

    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Invalid pointer");
        return -1;
    }

    if (maxCount <= 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Invalid FIFO size (%d)", maxCount);
        return -1;
    }

    memset(fifo, 0, sizeof(ARSTREAM2_StreamRecorder_AuFifo_t));
    fifo->size = maxCount;
    fifo->pool = malloc(maxCount * sizeof(ARSTREAM2_StreamRecorder_AuFifoItem_t));
    if (!fifo->pool)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "FIFO allocation failed (size %d)", maxCount * sizeof(ARSTREAM2_StreamRecorder_AuFifoItem_t));
        fifo->size = 0;
        return -1;
    }
    memset(fifo->pool, 0, maxCount * sizeof(ARSTREAM2_StreamRecorder_AuFifoItem_t));

    for (i = 0; i < maxCount; i++)
    {
        cur = &fifo->pool[i];
        if (fifo->free)
        {
            fifo->free->prev = cur;
        }
        cur->next = fifo->free;
        cur->prev = NULL;
        fifo->free = cur;
    }

    return 0;
}


static int ARSTREAM2_StreamRecorder_FifoFree(ARSTREAM2_StreamRecorder_AuFifo_t *fifo)
{
    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Invalid pointer");
        return -1;
    }

    if (fifo->pool)
    {
        free(fifo->pool);
    }
    memset(fifo, 0, sizeof(ARSTREAM2_StreamRecorder_AuFifo_t));

    return 0;
}


static ARSTREAM2_StreamRecorder_AuFifoItem_t* ARSTREAM2_StreamRecorder_FifoPopFreeItem(ARSTREAM2_StreamRecorder_AuFifo_t *fifo)
{
    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Invalid pointer");
        return NULL;
    }

    if (fifo->free)
    {
        ARSTREAM2_StreamRecorder_AuFifoItem_t* cur = fifo->free;
        fifo->free = cur->next;
        if (cur->next) cur->next->prev = NULL;
        cur->prev = NULL;
        cur->next = NULL;
        return cur;
    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "NALU FIFO is full");
        return NULL;
    }
}


static int ARSTREAM2_StreamRecorder_FifoPushFreeItem(ARSTREAM2_StreamRecorder_AuFifo_t *fifo, ARSTREAM2_StreamRecorder_AuFifoItem_t *item)
{
    if ((!fifo) || (!item))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Invalid pointer");
        return -1;
    }

    if (fifo->free)
    {
        fifo->free->prev = item;
        item->next = fifo->free;
    }
    else
    {
        item->next = NULL;
    }
    fifo->free = item;
    item->prev = NULL;

    return 0;
}


static int ARSTREAM2_StreamRecorder_FifoEnqueueItem(ARSTREAM2_StreamRecorder_AuFifo_t *fifo, ARSTREAM2_StreamRecorder_AuFifoItem_t *item)
{
    if ((!fifo) || (!item))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Invalid pointer");
        return -1;
    }

    if (fifo->count >= fifo->size)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "FIFO is full");
        return -2;
    }

    item->next = NULL;
    if (fifo->tail)
    {
        fifo->tail->next = item;
        item->prev = fifo->tail;
    }
    else
    {
        item->prev = NULL;
    }
    fifo->tail = item;
    if (!fifo->head)
    {
        fifo->head = item;
    }
    fifo->count++;

    return 0;
}


static ARSTREAM2_StreamRecorder_AuFifoItem_t* ARSTREAM2_StreamRecorder_FifoDequeueItem(ARSTREAM2_StreamRecorder_AuFifo_t *fifo)
{
    ARSTREAM2_StreamRecorder_AuFifoItem_t* cur;

    if (!fifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Invalid pointer");
        return NULL;
    }

    if ((!fifo->head) || (!fifo->count))
    {
        //ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_STREAM_RECORDER_TAG, "FIFO is empty"); //TODO: debug
        return NULL;
    }

    cur = fifo->head;
    if (cur->next)
    {
        cur->next->prev = NULL;
        fifo->head = cur->next;
        fifo->count--;
    }
    else
    {
        fifo->head = NULL;
        fifo->count = 0;
        fifo->tail = NULL;
    }
    cur->prev = NULL;
    cur->next = NULL;

    return cur;
}


static void ARSTREAM2_StreamRecorder_FifoFlush(ARSTREAM2_StreamRecorder_t* streamRecorder)
{
    ARSTREAM2_StreamRecorder_AuFifoItem_t* item;

    do
    {
        item = ARSTREAM2_StreamRecorder_FifoDequeueItem(&streamRecorder->auFifo);
        if (item)
        {
            /* Call the auCallback */
            if (streamRecorder->auCallback != NULL)
            {
                streamRecorder->auCallback(ARSTREAM2_STREAM_RECORDER_AU_STATUS_SUCCESS,
                                                  item->au.auUserPtr, streamRecorder->auCallbackUserPtr);
            }
            int fifoErr = ARSTREAM2_StreamRecorder_FifoPushFreeItem(&streamRecorder->auFifo, item);
            if (fifoErr != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "ARSTREAM2_StreamRecorder_FifoPushFreeItem() failed (%d)", fifoErr);
            }
        }
    }
    while (item);
}


eARSTREAM2_ERROR ARSTREAM2_StreamRecorder_Init(ARSTREAM2_StreamRecorder_Handle *streamRecorderHandle,
                                               ARSTREAM2_StreamRecorder_Config_t *config)
{
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;
    ARSTREAM2_StreamRecorder_t *streamRecorder = NULL;
    int mutexWasInit = 0, fifoMutexWasInit = 0, fifoCondWasInit = 0;

    if (!streamRecorderHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Invalid pointer for handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!config)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Invalid pointer for config");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if ((!config->mediaFileName) || (strlen(config->mediaFileName) < 4))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Invalid media file name");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    int mediaFileNameLen = strlen(config->mediaFileName);
    if ((strcasecmp(config->mediaFileName + mediaFileNameLen - 4, ".mp4") != 0)
            && (strcasecmp(config->mediaFileName + mediaFileNameLen - 4, ".264") != 0)
            && (strcasecmp(config->mediaFileName + mediaFileNameLen - 5, ".h264") != 0))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Invalid media file name extension");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if ((!config->sps) || (!config->spsSize))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Invalid SPS");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if ((!config->pps) || (!config->ppsSize))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Invalid PPS");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (config->auFifoSize <= 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Invalid access unit FIFO size");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    streamRecorder = (ARSTREAM2_StreamRecorder_t*)malloc(sizeof(*streamRecorder));
    if (!streamRecorder)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Allocation failed (size %ld)", sizeof(*streamRecorder));
        ret = ARSTREAM2_ERROR_ALLOC;
    }

    if (ret == ARSTREAM2_OK)
    {
        memset(streamRecorder, 0, sizeof(*streamRecorder));
        streamRecorder->auCallback = config->auCallback;
        streamRecorder->auCallbackUserPtr = config->auCallbackUserPtr;
        streamRecorder->videoWidth = config->videoWidth;
        streamRecorder->videoHeight = config->videoHeight;
        if (strcasecmp(config->mediaFileName + mediaFileNameLen - 4, ".mp4") == 0)
        {
            streamRecorder->fileType = ARSTREAM2_STREAM_RECORDER_FILE_TYPE_MP4;
        }
        else
        {
            streamRecorder->fileType = ARSTREAM2_STREAM_RECORDER_FILE_TYPE_H264_BYTE_STREAM;
        }
    }

    if (ret == ARSTREAM2_OK)
    {
        int fifoErr = ARSTREAM2_StreamRecorder_FifoInit(&streamRecorder->auFifo, config->auFifoSize);
        if (fifoErr != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "ARSTREAM2_StreamRecorder_FifoInit() failed (%d)", fifoErr);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
    }

#if BUILD_LIBARMEDIA
    if ((ret == ARSTREAM2_OK) && (streamRecorder->fileType == ARSTREAM2_STREAM_RECORDER_FILE_TYPE_MP4))
    {
        eARMEDIA_ERROR err = ARMEDIA_OK;
        streamRecorder->videoEncap = ARMEDIA_VideoEncapsuler_New(config->mediaFileName,
                                                                 round(config->videoFramerate),
                                                                 "", //VIDEO_RECORD_DEFAULT_SESSON_ID, //TODO
                                                                 "", //VIDEO_RECORD_DEFAULT_STARTDATE, //TODO
                                                                 config->serviceType,
                                                                 &err);
        if (streamRecorder->videoEncap == NULL)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "ARMEDIA_VideoEncapsuler_New() failed: %d (%s)", err, ARMEDIA_Error_ToString(err));
            ret = ARSTREAM2_ERROR_UNSUPPORTED;
        }
    }

    if ((ret == ARSTREAM2_OK) && (streamRecorder->fileType == ARSTREAM2_STREAM_RECORDER_FILE_TYPE_MP4))
    {
        eARMEDIA_ERROR err;
        err = ARMEDIA_VideoEncapsuler_SetAvcParameterSets(streamRecorder->videoEncap, config->sps, config->spsSize, config->pps, config->ppsSize);
        if (err != ARMEDIA_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "ARMEDIA_VideoEncapsuler_SetAvcParameterSets() failed: %d (%s)", err, ARMEDIA_Error_ToString(err));
            ret = ARSTREAM2_ERROR_UNSUPPORTED;
        }
    }
#else
    if ((ret == ARSTREAM2_OK) && (streamRecorder->fileType == ARSTREAM2_STREAM_RECORDER_FILE_TYPE_MP4))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Unsupported file format: MP4");
        ret = ARSTREAM2_ERROR_UNSUPPORTED;
    }
#endif

    if ((ret == ARSTREAM2_OK) && (streamRecorder->fileType == ARSTREAM2_STREAM_RECORDER_FILE_TYPE_H264_BYTE_STREAM))
    {
        streamRecorder->outputFile = fopen(config->mediaFileName, "wb");
        if (streamRecorder->outputFile == NULL)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Failed to open file '%s'", config->mediaFileName);
            ret = ARSTREAM2_ERROR_ALLOC;
        }

        if (streamRecorder->outputFile)
        {
            fwrite(config->sps, config->spsSize, 1, streamRecorder->outputFile);
            fwrite(config->pps, config->ppsSize, 1, streamRecorder->outputFile);
            fflush(streamRecorder->outputFile);
        }
    }

    if (ret == ARSTREAM2_OK)
    {
        int mutexInitRet = ARSAL_Mutex_Init(&(streamRecorder->mutex));
        if (mutexInitRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Mutex creation failed (%d)", mutexInitRet);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            mutexWasInit = 1;
        }
    }

    if (ret == ARSTREAM2_OK)
    {
        int mutexInitRet = ARSAL_Mutex_Init(&(streamRecorder->fifoMutex));
        if (mutexInitRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Mutex creation failed (%d)", mutexInitRet);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            fifoMutexWasInit = 1;
        }
    }

    if (ret == ARSTREAM2_OK)
    {
        int condInitRet = ARSAL_Cond_Init(&(streamRecorder->fifoCond));
        if (condInitRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Cond creation failed (%d)", condInitRet);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            fifoCondWasInit = 1;
        }
    }

    if (ret == ARSTREAM2_OK)
    {
        *streamRecorderHandle = (ARSTREAM2_StreamRecorder_Handle*)streamRecorder;
    }
    else
    {
        if (streamRecorder)
        {
            if (streamRecorder->auFifo.size > 0) ARSTREAM2_StreamRecorder_FifoFree(&streamRecorder->auFifo);
            if (fifoCondWasInit == 1) ARSAL_Cond_Destroy(&(streamRecorder->fifoCond));
            if (fifoMutexWasInit) ARSAL_Mutex_Destroy(&(streamRecorder->fifoMutex));
            if (mutexWasInit) ARSAL_Mutex_Destroy(&(streamRecorder->mutex));
            if (streamRecorder->outputFile) fclose(streamRecorder->outputFile);
            free(streamRecorder);
        }
        *streamRecorderHandle = NULL;
    }

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_StreamRecorder_Free(ARSTREAM2_StreamRecorder_Handle *streamRecorderHandle)
{
    ARSTREAM2_StreamRecorder_t* streamRecorder;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;
    int canDelete = 0;

    if ((!streamRecorderHandle) || (!*streamRecorderHandle))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Invalid pointer for handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    streamRecorder = (ARSTREAM2_StreamRecorder_t*)*streamRecorderHandle;

    ARSAL_Mutex_Lock(&(streamRecorder->mutex));
    if (streamRecorder->threadStarted == 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_STREAM_RECORDER_TAG, "Thread is stopped");
        canDelete = 1;
    }

    if (canDelete == 1)
    {
        ARSTREAM2_StreamRecorder_FifoFree(&streamRecorder->auFifo);
        ARSAL_Cond_Destroy(&(streamRecorder->fifoCond));
        ARSAL_Mutex_Destroy(&(streamRecorder->fifoMutex));
        ARSAL_Mutex_Destroy(&(streamRecorder->mutex));
        if (streamRecorder->outputFile) fclose(streamRecorder->outputFile);
        if (streamRecorder->recordingMetadata) free(streamRecorder->recordingMetadata);
        if (streamRecorder->savedMetadata) free(streamRecorder->savedMetadata);

        free(streamRecorder);
        *streamRecorderHandle = NULL;
    }
    else
    {
        ARSAL_Mutex_Unlock(&(streamRecorder->mutex));
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Call ARSTREAM2_StreamRecorder_Stop before calling this function");
    }

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_StreamRecorder_Stop(ARSTREAM2_StreamRecorder_Handle streamRecorderHandle)
{
    ARSTREAM2_StreamRecorder_t* streamRecorder = (ARSTREAM2_StreamRecorder_t*)streamRecorderHandle;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;

    if (!streamRecorderHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_STREAM_RECORDER_TAG, "Stopping stream recorder...");
    ARSAL_Mutex_Lock(&(streamRecorder->mutex));
    streamRecorder->threadShouldStop = 1;
    ARSAL_Mutex_Unlock(&(streamRecorder->mutex));
    /* signal the thread to avoid a deadlock */
    ARSAL_Cond_Signal(&(streamRecorder->fifoCond));

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_StreamRecorder_PushAccessUnit(ARSTREAM2_StreamRecorder_Handle streamRecorderHandle,
                                                         ARSTREAM2_StreamRecorder_AccessUnit_t *accessUnit)
{
    ARSTREAM2_StreamRecorder_t* streamRecorder = (ARSTREAM2_StreamRecorder_t*)streamRecorderHandle;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;

    if (!streamRecorderHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!accessUnit)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Invalid access unit");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    ARSTREAM2_StreamRecorder_AuFifoItem_t *item;
    ARSAL_Mutex_Lock(&(streamRecorder->fifoMutex));
    item = ARSTREAM2_StreamRecorder_FifoPopFreeItem(&streamRecorder->auFifo);
    if (item)
    {
        memcpy(&item->au, accessUnit, sizeof(ARSTREAM2_StreamRecorder_AccessUnit_t));
        int fifoErr = ARSTREAM2_StreamRecorder_FifoEnqueueItem(&streamRecorder->auFifo, item);
        ARSAL_Mutex_Unlock(&(streamRecorder->fifoMutex));
        if (fifoErr != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "ARSTREAM2_StreamRecorder_FifoEnqueueItem() failed (%d)", fifoErr);
            ARSAL_Mutex_Lock(&(streamRecorder->fifoMutex));
            fifoErr = ARSTREAM2_StreamRecorder_FifoPushFreeItem(&streamRecorder->auFifo, item);
            /* Flush the FIFO */
            ARSTREAM2_StreamRecorder_FifoFlush(streamRecorder);
            ARSAL_Mutex_Unlock(&(streamRecorder->fifoMutex));
            if (fifoErr != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "ARSTREAM2_StreamRecorder_FifoPushFreeItem() failed (%d)", fifoErr);
            }
        }
    }
    else
    {
        ARSAL_Mutex_Unlock(&(streamRecorder->fifoMutex));
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Access unit FIFO is full");
        return ARSTREAM2_ERROR_QUEUE_FULL;
    }
    ARSAL_Cond_Signal(&(streamRecorder->fifoCond));

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_StreamRecorder_Flush(ARSTREAM2_StreamRecorder_Handle streamRecorderHandle)
{
    ARSTREAM2_StreamRecorder_t* streamRecorder = (ARSTREAM2_StreamRecorder_t*)streamRecorderHandle;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;

    if (!streamRecorderHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Invalid handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    ARSAL_Mutex_Lock(&(streamRecorder->fifoMutex));
    ARSTREAM2_StreamRecorder_FifoFlush(streamRecorder);
    ARSAL_Mutex_Unlock(&(streamRecorder->fifoMutex));
    ARSAL_Cond_Signal(&(streamRecorder->fifoCond));

    return ret;
}


void* ARSTREAM2_StreamRecorder_RunThread(void *param)
{
    ARSTREAM2_StreamRecorder_t* streamRecorder = (ARSTREAM2_StreamRecorder_t*)param;
    ARSTREAM2_StreamRecorder_AuFifoItem_t *item;
    int shouldStop;

    if (!param)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Invalid handle");
        return 0;
    }

    ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_STREAM_RECORDER_TAG, "Stream recorder thread is started");
    ARSAL_Mutex_Lock(&(streamRecorder->mutex));
    streamRecorder->threadStarted = 1;
    shouldStop = streamRecorder->threadShouldStop;
    ARSAL_Mutex_Unlock(&(streamRecorder->mutex));

    while (!shouldStop)
    {
        ARSAL_Mutex_Lock(&(streamRecorder->fifoMutex));
        item = ARSTREAM2_StreamRecorder_FifoDequeueItem(&streamRecorder->auFifo);
        ARSAL_Mutex_Unlock(&(streamRecorder->fifoMutex));
        while (item)
        {
            /* Record the frame */
            switch (streamRecorder->fileType)
            {
            case ARSTREAM2_STREAM_RECORDER_FILE_TYPE_H264_BYTE_STREAM:
            {
                if (streamRecorder->outputFile)
                {
                    if (item->au.naluCount)
                    {
                        unsigned int i;
                        for (i = 0; i < item->au.naluCount; i++)
                        {
                            fwrite(item->au.naluData[i], item->au.naluSize[i], 1, streamRecorder->outputFile);
                        }
                    }
                    else
                    {
                        fwrite(item->au.auData, item->au.auSize, 1, streamRecorder->outputFile);
                    }
                    if ((item->au.auSyncType != ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_NONE)
                            || (item->au.index >= streamRecorder->lastSyncIndex + ARSTREAM2_STREAM_RECORDER_FILE_SYNC_MAX_INTERVAL))
                    {
                        fflush(streamRecorder->outputFile);
                        fsync(fileno(streamRecorder->outputFile));
                        streamRecorder->lastSyncIndex = item->au.index;
                    }
                }
                break;
            }
#if BUILD_LIBARMEDIA
            case ARSTREAM2_STREAM_RECORDER_FILE_TYPE_MP4:
            {
                int gotMetadata = 0;
                ARMEDIA_Frame_Header_t frameHeader;
                memset(&frameHeader, 0, sizeof(ARMEDIA_Frame_Header_t));
                frameHeader.codec = CODEC_MPEG4_AVC;
                frameHeader.frame_size = item->au.auSize;
                frameHeader.frame_number = item->au.index;
                frameHeader.width = streamRecorder->videoWidth;
                frameHeader.height = streamRecorder->videoHeight;
                frameHeader.timestamp = item->au.timestamp;
                frameHeader.frame_type = (item->au.auSyncType == ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_NONE) ? ARMEDIA_ENCAPSULER_FRAME_TYPE_P_FRAME : ARMEDIA_ENCAPSULER_FRAME_TYPE_I_FRAME;
                frameHeader.frame = item->au.auData;
                frameHeader.avc_insert_ps = 0;
                frameHeader.avc_nalu_count = item->au.naluCount;
                if (item->au.naluCount)
                {
                    unsigned int i;
                    for (i = 0; i < item->au.naluCount; i++)
                    {
                        frameHeader.avc_nalu_size[i] = item->au.naluSize[i];
                        frameHeader.avc_nalu_data[i] = item->au.naluData[i];
                    }
                }

                if ((item->au.auMetadata) && (item->au.auMetadataSize) && (streamRecorder->recordingMetadataSize == 0))
                {
                    /* Setup the metadata */
                    int size = ARSTREAM2_StreamRecorder_StreamingToRecordingMetadataSize(item->au.auMetadata, item->au.auMetadataSize);
                    if (size > 0)
                    {
                        int ret = 0;
                        streamRecorder->recordingMetadataSize = (unsigned)size;
                        streamRecorder->recordingMetadata = malloc(streamRecorder->recordingMetadataSize);
                        if (!streamRecorder->recordingMetadata)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Metadata buffer allocation failed (size: %d)", streamRecorder->recordingMetadataSize);
                            ret = -1;
                        }
                        if (ret == 0)
                        {
                            streamRecorder->savedMetadataSize = streamRecorder->recordingMetadataSize;
                            streamRecorder->savedMetadata = malloc(streamRecorder->savedMetadataSize);
                            if (!streamRecorder->savedMetadata)
                            {
                                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "Metadata buffer allocation failed (size: %d)", streamRecorder->savedMetadataSize);
                                ret = -1;
                            }
                        }
                        if (ret == 0)
                        {
                            eARMEDIA_ERROR err;
                            err = ARMEDIA_VideoEncapsuler_SetMetadataInfo(streamRecorder->videoEncap,
                                                                          ARSTREAM2_STREAM_RECORDER_PARROT_VIDEO_RECORDING_METADATA_V1_CONTENT_ENCODING,
                                                                          ARSTREAM2_STREAM_RECORDER_PARROT_VIDEO_RECORDING_METADATA_V1_MIME_FORMAT,
                                                                          sizeof(ARSTREAM2_STREAM_RECORDER_ParrotVideoRecordingMetadataV1_t));
                            if (err != ARMEDIA_OK)
                            {
                                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "ARMEDIA_VideoEncapsuler_SetMetadataInfo() failed: %d (%s)", err, ARMEDIA_Error_ToString(err));
                                ret = -1;
                            }
                        }
                        if (ret != 0)
                        {
                            if (streamRecorder->recordingMetadata) free(streamRecorder->recordingMetadata);
                            streamRecorder->recordingMetadata = NULL;
                            streamRecorder->recordingMetadataSize = 0;
                            if (streamRecorder->savedMetadata) free(streamRecorder->savedMetadata);
                            streamRecorder->savedMetadata = NULL;
                            streamRecorder->savedMetadataSize = 0;
                        }
                    }
                }
                if ((item->au.auMetadata) && (item->au.auMetadataSize) && (streamRecorder->recordingMetadataSize > 0))
                {
                    /* Convert the metadata */
                    int ret = ARSTREAM2_StreamRecorder_StreamingToRecordingMetadata(item->au.timestamp,
                                                                                    item->au.auMetadata, item->au.auMetadataSize,
                                                                                    streamRecorder->savedMetadata, streamRecorder->savedMetadataSize,
                                                                                    streamRecorder->recordingMetadata, streamRecorder->recordingMetadataSize);
                    if (ret != 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "ARSTREAM2_StreamRecorder_StreamingToRecordingMetadata() failed: %d", ret);
                    }
                    else
                    {
                        gotMetadata = 1;
                    }
                }

                eARMEDIA_ERROR err = ARMEDIA_VideoEncapsuler_AddFrame(streamRecorder->videoEncap, &frameHeader, ((gotMetadata) ? streamRecorder->recordingMetadata : NULL));
                if (err != ARMEDIA_OK)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "ARMEDIA_VideoEncapsuler_AddFrame() failed: %d (%s)", err, ARMEDIA_Error_ToString(err));
                }
                break;
            }
#endif
            default:
                break;
            }

            /* Call the auCallback */
            if (streamRecorder->auCallback != NULL)
            {
                streamRecorder->auCallback(ARSTREAM2_STREAM_RECORDER_AU_STATUS_SUCCESS,
                                                  item->au.auUserPtr, streamRecorder->auCallbackUserPtr);
            }

            ARSAL_Mutex_Lock(&(streamRecorder->fifoMutex));
            int fifoErr = ARSTREAM2_StreamRecorder_FifoPushFreeItem(&streamRecorder->auFifo, item);
            item = ARSTREAM2_StreamRecorder_FifoDequeueItem(&streamRecorder->auFifo);
            ARSAL_Mutex_Unlock(&(streamRecorder->fifoMutex));
            if (fifoErr != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "ARSTREAM2_StreamRecorder_FifoPushFreeItem() failed (%d)", fifoErr);
            }
        }

        ARSAL_Mutex_Lock(&(streamRecorder->mutex));
        shouldStop = streamRecorder->threadShouldStop;
        ARSAL_Mutex_Unlock(&(streamRecorder->mutex));

        if (!shouldStop)
        {
            /* Wake up when a new AU is in the FIFO or when we need to exit */
            ARSAL_Mutex_Lock(&(streamRecorder->fifoMutex));
            ARSAL_Cond_Timedwait(&(streamRecorder->fifoCond), &(streamRecorder->fifoMutex), ARSTREAM2_STREAM_RECORDER_FIFO_COND_TIMEOUT_MS);
            ARSAL_Mutex_Unlock(&(streamRecorder->fifoMutex));
        }
    }

    /* Flush the FIFO */
    ARSAL_Mutex_Lock(&(streamRecorder->fifoMutex));
    ARSTREAM2_StreamRecorder_FifoFlush(streamRecorder);
    ARSAL_Mutex_Unlock(&(streamRecorder->fifoMutex));

#if BUILD_LIBARMEDIA
    if (streamRecorder->fileType == ARSTREAM2_STREAM_RECORDER_FILE_TYPE_MP4)
    {
        eARMEDIA_ERROR err = ARMEDIA_VideoEncapsuler_Finish(&streamRecorder->videoEncap);
        if (err != ARMEDIA_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECORDER_TAG, "ARMEDIA_VideoEncapsuler_AddFrame() failed: %d (%s)", err, ARMEDIA_Error_ToString(err));
        }
    }
#endif

    ARSAL_Mutex_Lock(&(streamRecorder->mutex));
    streamRecorder->threadStarted = 0;
    ARSAL_Mutex_Unlock(&(streamRecorder->mutex));
    ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_STREAM_RECORDER_TAG, "Stream recorder thread has ended");

    return (void*)0;
}
