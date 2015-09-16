/**
 * @file beaver_parrot.h
 * @brief H.264 Elementary Stream Tools Library - Parrot user data SEI definition and serializer
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#ifndef _BEAVER_PARROT_H_
#define _BEAVER_PARROT_H_

#ifdef __cplusplus
extern "C" {
#endif /* #ifdef __cplusplus */

#include <inttypes.h>


#define BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH 9                    /**< Serial number part length */
#define BEAVER_PARROT_DRAGON_MAX_SLICE_COUNT 128                            /**< Maximum number of slices per frame */


/**
 * @brief User data SEI types.
 */
typedef enum
{
    BEAVER_PARROT_USER_DATA_SEI_UNKNOWN = 0,                                /**< Unknown user data SEI */
    BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V1,                            /**< 'Dragon Basic' v1 user data SEI */
    BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V1,                         /**< 'Dragon Extended' v1 user data SEI */
    BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V2,                            /**< 'Dragon Basic' v2 user data SEI */
    BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V2,                         /**< 'Dragon Extended' v2 user data SEI */
    BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1,                        /**< 'Dragon FrameInfo' v1 user data SEI */
    BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_V1,                        /**< 'Dragon Streaming' v1 user data SEI */
    BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1,              /**< 'Dragon Streaming FrameInfo' v1 user data SEI */

} BEAVER_Parrot_UserDataSeiTypes_t;


/**
 * "Dragon Basic" v1 user data SEI.
 * UUID: 8818b6d5-4aff-45ad-ba04-bc0cbae6a5fd
 */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V1_UUID_0 0x8818b6d5       /**< "Dragon Basic" v1 user data SEI UUID part 1 (bit 0..31) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V1_UUID_1 0x4aff45ad       /**< "Dragon Basic" v1 user data SEI UUID part 2 (bit 32..63) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V1_UUID_2 0xba04bc0c       /**< "Dragon Basic" v1 user data SEI UUID part 3 (bit 64..95) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V1_UUID_3 0xbae6a5fd       /**< "Dragon Basic" v1 user data SEI UUID part 4 (bit 96..127) */


/**
 * "Dragon Basic" v2 user data SEI.
 * UUID: f1433a75-e491-4bf5-aadf-455ddf6ac0a8
 */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V2_UUID_0 0xf1433a75       /**< "Dragon Basic" v2 user data SEI UUID part 1 (bit 0..31) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V2_UUID_1 0xe4914bf5       /**< "Dragon Basic" v2 user data SEI UUID part 2 (bit 32..63) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V2_UUID_2 0xaadf455d       /**< "Dragon Basic" v2 user data SEI UUID part 3 (bit 64..95) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V2_UUID_3 0xdf6ac0a8       /**< "Dragon Basic" v2 user data SEI UUID part 4 (bit 96..127) */


/**
 * "Dragon Extended" v1 user data SEI.
 * UUID: 5aace927-933f-41ff-b863-af7e617532cf
 */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V1_UUID_0 0x5aace927    /**< "Dragon Extended" v1 user data SEI UUID part 1 (bit 0..31) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V1_UUID_1 0x933f41ff    /**< "Dragon Extended" v1 user data SEI UUID part 2 (bit 32..63) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V1_UUID_2 0xb863af7e    /**< "Dragon Extended" v1 user data SEI UUID part 3 (bit 64..95) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V1_UUID_3 0x617532cf    /**< "Dragon Extended" v1 user data SEI UUID part 4 (bit 96..127) */


/**
 * "Dragon Extended" v2 user data SEI.
 * UUID: 937a509b-2f23-4df6-8be3-330569d3b5bb
 */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V2_UUID_0 0x937a509b    /**< "Dragon Extended" v2 user data SEI UUID part 1 (bit 0..31) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V2_UUID_1 0x2f234df6    /**< "Dragon Extended" v2 user data SEI UUID part 2 (bit 32..63) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V2_UUID_2 0x8be33305    /**< "Dragon Extended" v2 user data SEI UUID part 3 (bit 64..95) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V2_UUID_3 0x69d3b5bb    /**< "Dragon Extended" v2 user data SEI UUID part 4 (bit 96..127) */


/**
 * "Dragon FrameInfo" v1 user data SEI.
 * UUID: 3991d0df-5adf-46ec-bd68-a7096bb029a8
 */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1_UUID_0 0x3991d0df   /**< "Dragon FrameInfo" v1 user data SEI UUID part 1 (bit 0..31) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1_UUID_1 0x5adf46ec   /**< "Dragon FrameInfo" v1 user data SEI UUID part 2 (bit 32..63) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1_UUID_2 0xbd68a709   /**< "Dragon FrameInfo" v1 user data SEI UUID part 3 (bit 64..95) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1_UUID_3 0x6bb029a8   /**< "Dragon FrameInfo" v1 user data SEI UUID part 4 (bit 96..127) */


/**
 * "Dragon Streaming" v1 user data SEI.
 * UUID: 13dbccc7-c720-42f5-a0b7-aafaa2b3af97
 */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_V1_UUID_0 0x13dbccc7   /**< "Dragon Streaming" v1 user data SEI UUID part 1 (bit 0..31) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_V1_UUID_1 0xc72042f5   /**< "Dragon Streaming" v1 user data SEI UUID part 2 (bit 32..63) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_V1_UUID_2 0xa0b7aafa   /**< "Dragon Streaming" v1 user data SEI UUID part 3 (bit 64..95) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_V1_UUID_3 0xa2b3af97   /**< "Dragon Streaming" v1 user data SEI UUID part 4 (bit 96..127) */


/**
 * "Dragon Streaming FrameInfo" v1 user data SEI.
 * UUID: a90f2708-dc10-493a-9a34-94b6b9bab75b
 */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1_UUID_0 0xa90f2708   /**< "Dragon Streaming FrameInfo" v1 user data SEI UUID part 1 (bit 0..31) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1_UUID_1 0xdc10493a   /**< "Dragon Streaming FrameInfo" v1 user data SEI UUID part 2 (bit 32..63) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1_UUID_2 0x9a3494b6   /**< "Dragon Streaming FrameInfo" v1 user data SEI UUID part 3 (bit 64..95) */
#define BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1_UUID_3 0xb9bab75b   /**< "Dragon Streaming FrameInfo" v1 user data SEI UUID part 4 (bit 96..127) */


/**
 * @brief "Dragon Basic" v1 user data SEI.
 */
typedef struct
{
    uint32_t uuid[4];               /**< User data SEI UUID */
    uint32_t frameIndex;            /**< Frame index */
    uint32_t acquisitionTsH;        /**< Frame acquisition timestamp (high 32 bits) */
    uint32_t acquisitionTsL;        /**< Frame acquisition timestamp (low 32 bits) */
    uint32_t prevMse_fp8;           /**< Previous frame MSE, 8 bits fixed point value */

} BEAVER_Parrot_UserDataSeiDragonBasicV1_t;


/**
 * @brief "Dragon Basic" v2 user data SEI.
 */
typedef struct
{
    uint32_t uuid[4];               /**< User data SEI UUID */
    uint32_t frameIndex;            /**< Frame index */
    uint32_t acquisitionTsH;        /**< Frame acquisition timestamp (high 32 bits) */
    uint32_t acquisitionTsL;        /**< Frame acquisition timestamp (low 32 bits) */

} BEAVER_Parrot_UserDataSeiDragonBasicV2_t;


/**
 * @brief "Dragon Extended" v1 user data SEI.
 */
typedef struct
{
    uint32_t uuid[4];                                                           /**< User data SEI UUID */
    uint32_t frameIndex;                                                        /**< Frame index */
    uint32_t acquisitionTsH;                                                    /**< Frame acquisition timestamp (high 32 bits) */
    uint32_t acquisitionTsL;                                                    /**< Frame acquisition timestamp (low 32 bits) */
    uint32_t prevMse_fp8;                                                       /**< Previous frame MSE, 8 bits fixed point value */
    uint32_t batteryPercentage;                                                 /**< Battery charge percentage */
    int32_t  latitude_fp20;                                                     /**< GPS latitude (deg), 20 bits fixed point value */
    int32_t  longitude_fp20;                                                    /**< GPS longitude (deg), 20 bits fixed point value */
    int32_t  altitude_fp16;                                                     /**< GPS altitude (m), 16 bits fixed point value */
    int32_t  absoluteHeight_fp16;                                               /**< Absolute height (m), 16 bits fixed point value */
    int32_t  relativeHeight_fp16;                                               /**< Relative height (m), 16 bits fixed point value */
    int32_t  xSpeed_fp16;                                                       /**< X speed (m/s), 16 bits fixed point value */
    int32_t  ySpeed_fp16;                                                       /**< Y speed (m/s), 16 bits fixed point value */
    int32_t  zSpeed_fp16;                                                       /**< Z speed (m/s), 16 bits fixed point value */
    uint32_t distance_fp16;                                                     /**< Distance from home (m), 16 bits fixed point value */
    int32_t  heading_fp16;                                                      /**< Heading (rad), 16 bits fixed point value */
    int32_t  yaw_fp16;                                                          /**< Yaw/psi (rad), 16 bits fixed point value */
    int32_t  pitch_fp16;                                                        /**< Pitch/theta (rad), 16 bits fixed point value */
    int32_t  roll_fp16;                                                         /**< Roll/phi (rad), 16 bits fixed point value */
    int32_t  cameraPan_fp16;                                                    /**< Camera pan (rad), 16 bits fixed point value */
    int32_t  cameraTilt_fp16;                                                   /**< Camera tilt (rad), 16 bits fixed point value */
    uint32_t videoStreamingTargetBitrate;                                       /**< Video srteaming encoder target bitrate (bit/s) */
    int32_t  wifiRssi;                                                          /**< Wifi RSSI (dBm) */
    uint32_t wifiMcsRate;                                                       /**< Wifi MCS rate (bit/s) */
    uint32_t wifiTxRate;                                                        /**< Wifi Tx rate (bit/s) */
    uint32_t wifiRxRate;                                                        /**< Wifi Rx rate (bit/s) */
    uint32_t wifiTxFailRate;                                                    /**< Wifi Tx fail rate (#/s) */
    uint32_t wifiTxErrorRate;                                                   /**< Wifi Tx error rate (#/s) */
    uint32_t postReprojTimestampDelta;                                          /**< Post-reprojection time */
    uint32_t postEeTimestampDelta;                                              /**< Post-EE time */
    uint32_t postScalingTimestampDelta;                                         /**< Post-scaling time */
    uint32_t postStreamingEncodingTimestampDelta;                               /**< Post-streaming-encoding time */
    uint32_t postNetworkInputTimestampDelta;                                    /**< Post-network-input time */
    uint32_t systemTsH;                                                         /**< Frame acquisition timestamp (high 32 bits) */
    uint32_t systemTsL;                                                         /**< Frame acquisition timestamp (low 32 bits) */
    char     serialNumberH[BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH + 1]; /**< Serial number high part */
    char     serialNumberL[BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH + 1]; /**< Serial number low part */

} BEAVER_Parrot_UserDataSeiDragonExtendedV1_t;


/**
 * @brief "Dragon Extended" v2 user data SEI.
 */
typedef struct
{
    uint32_t uuid[4];                                                           /**< User data SEI UUID */
    uint32_t frameIndex;                                                        /**< Frame index */
    uint32_t acquisitionTsH;                                                    /**< Frame acquisition timestamp (high 32 bits) */
    uint32_t acquisitionTsL;                                                    /**< Frame acquisition timestamp (low 32 bits) */
    uint32_t batteryPercentage;                                                 /**< Battery charge percentage */
    int32_t  latitude_fp20;                                                     /**< GPS latitude (deg), 20 bits fixed point value */
    int32_t  longitude_fp20;                                                    /**< GPS longitude (deg), 20 bits fixed point value */
    int32_t  altitude_fp16;                                                     /**< GPS altitude (m), 16 bits fixed point value */
    int32_t  absoluteHeight_fp16;                                               /**< Absolute height (m), 16 bits fixed point value */
    int32_t  relativeHeight_fp16;                                               /**< Relative height (m), 16 bits fixed point value */
    int32_t  xSpeed_fp16;                                                       /**< X speed (m/s), 16 bits fixed point value */
    int32_t  ySpeed_fp16;                                                       /**< Y speed (m/s), 16 bits fixed point value */
    int32_t  zSpeed_fp16;                                                       /**< Z speed (m/s), 16 bits fixed point value */
    uint32_t distance_fp16;                                                     /**< Distance from home (m), 16 bits fixed point value */
    int32_t  yaw_fp16;                                                          /**< Yaw/psi (rad), 16 bits fixed point value */
    int32_t  pitch_fp16;                                                        /**< Pitch/theta (rad), 16 bits fixed point value */
    int32_t  roll_fp16;                                                         /**< Roll/phi (rad), 16 bits fixed point value */
    int32_t  cameraPan_fp16;                                                    /**< Camera pan (rad), 16 bits fixed point value */
    int32_t  cameraTilt_fp16;                                                   /**< Camera tilt (rad), 16 bits fixed point value */
    uint32_t videoStreamingTargetBitrate;                                       /**< Video streaming encoder target bitrate (bit/s) */
    uint32_t videoStreamingDecimation;                                          /**< Video streaming encoder decimation factor */
    uint32_t videoStreamingGopLength;                                           /**< Video streaming encoder GOP length */
    int32_t  videoStreamingPrevFrameType;                                       /**< Video streaming previous frame type */
    uint32_t videoStreamingPrevFrameSize;                                       /**< Video streaming previous frame size (bytes) */
    uint32_t videoStreamingPrevFrameMseY_fp8;                                   /**< Video streaming previous frame MSE, 8 bits fixed point value */
    int32_t  videoRecordingPrevFrameType;                                       /**< Video recording previous frame type */
    uint32_t videoRecordingPrevFrameSize;                                       /**< Video recording previous frame size (bytes) */
    uint32_t videoRecordingPrevFrameMseY_fp8;                                   /**< Video recording previous frame MSE, 8 bits fixed point value */
    int32_t  wifiRssi;                                                          /**< Wifi RSSI (dBm) */
    uint32_t wifiMcsRate;                                                       /**< Wifi MCS rate (bit/s) */
    uint32_t wifiTxRate;                                                        /**< Wifi Tx rate (bit/s) */
    uint32_t wifiRxRate;                                                        /**< Wifi Rx rate (bit/s) */
    uint32_t wifiTxFailRate;                                                    /**< Wifi Tx fail rate (#/s) */
    uint32_t wifiTxErrorRate;                                                   /**< Wifi Tx error rate (#/s) */
    uint32_t preReprojTimestampDelta;                                           /**< Pre-reprojection time */
    uint32_t postReprojTimestampDelta;                                          /**< Post-reprojection time */
    uint32_t postEeTimestampDelta;                                              /**< Post-EE time */
    uint32_t postScalingTimestampDelta;                                         /**< Post-scaling time */
    uint32_t postStreamingEncodingTimestampDelta;                               /**< Post-streaming-encoding time */
    uint32_t postRecordingEncodingTimestampDelta;                               /**< Post-recording-encoding time */
    uint32_t postNetworkInputTimestampDelta;                                    /**< Post-network-input time */
    uint32_t systemTsH;                                                         /**< Frame acquisition timestamp (high 32 bits) */
    uint32_t systemTsL;                                                         /**< Frame acquisition timestamp (low 32 bits) */
    uint32_t streamingMonitorTimeInterval;                                      /**< Streaming monitoring time interval in microseconds */
    uint32_t streamingMeanAcqToNetworkTime;                                     /**< Streaming mean acquisition to network time */
    uint32_t streamingAcqToNetworkJitter;                                       /**< Streaming acquisition to network time jitter */
    uint32_t streamingMeanNetworkTime;                                          /**< Streaming mean acquisition to network time */
    uint32_t streamingNetworkJitter;                                            /**< Streaming acquisition to network time jitter */
    uint32_t streamingBytesSent;                                                /**< Streaming bytes sent during the interval */
    uint32_t streamingMeanPacketSize;                                           /**< Streaming mean packet size */
    uint32_t streamingPacketSizeStdDev;                                         /**< Streaming packet size standard deviation */
    uint32_t streamingPacketsSent;                                              /**< Streaming packets sent during the interval */
    uint32_t streamingBytesDropped;                                             /**< Streaming bytes dropped during the interval */
    uint32_t streamingNaluDropped;                                              /**< Streaming NAL units dropped during the interval */
    char     serialNumberH[BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH + 1]; /**< Serial number high part */
    char     serialNumberL[BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH + 1]; /**< Serial number low part */

} BEAVER_Parrot_UserDataSeiDragonExtendedV2_t;


/**
 * @brief "Dragon FrameInfo" v1 definition.
 */
typedef struct
{
    uint32_t frameIndex;                                                    /**< Frame index */
    uint32_t acquisitionTsH;                                                /**< Frame acquisition timestamp (monotonic) (high 32 bits) */
    uint32_t acquisitionTsL;                                                /**< Frame acquisition timestamp (monotonic) (low 32 bits) */
    uint32_t systemTsH;                                                     /**< Frame acquisition timestamp (system) (high 32 bits) */
    uint32_t systemTsL;                                                     /**< Frame acquisition timestamp (system) (low 32 bits) */
    uint32_t batteryPercentage;                                             /**< Battery charge percentage */
    int32_t  gpsLatitude_fp20;                                              /**< GPS latitude (deg), 20 bits fixed point value */
    int32_t  gpsLongitude_fp20;                                             /**< GPS longitude (deg), 20 bits fixed point value */
    int32_t  gpsAltitude_fp16;                                              /**< GPS altitude (m), 16 bits fixed point value */
    int32_t  absoluteHeight_fp16;                                           /**< Absolute height (m), 16 bits fixed point value */
    int32_t  relativeHeight_fp16;                                           /**< Relative height (m), 16 bits fixed point value */
    int32_t  xSpeed_fp16;                                                   /**< X speed (m/s), 16 bits fixed point value */
    int32_t  ySpeed_fp16;                                                   /**< Y speed (m/s), 16 bits fixed point value */
    int32_t  zSpeed_fp16;                                                   /**< Z speed (m/s), 16 bits fixed point value */
    uint32_t distanceFromHome_fp16;                                         /**< Distance from home (m), 16 bits fixed point value */
    int32_t  yaw_fp16;                                                      /**< Yaw/psi (rad), 16 bits fixed point value */
    int32_t  pitch_fp16;                                                    /**< Pitch/theta (rad), 16 bits fixed point value */
    int32_t  roll_fp16;                                                     /**< Roll/phi (rad), 16 bits fixed point value */
    int32_t  cameraPan_fp16;                                                /**< Camera pan (rad), 16 bits fixed point value */
    int32_t  cameraTilt_fp16;                                               /**< Camera tilt (rad), 16 bits fixed point value */
    int32_t  wifiRssi;                                                      /**< Wifi RSSI (dBm) */
    uint32_t wifiMcsRate;                                                   /**< Wifi MCS rate (bit/s) */
    uint32_t wifiTxRate;                                                    /**< Wifi Tx rate (bit/s) */
    uint32_t wifiRxRate;                                                    /**< Wifi Rx rate (bit/s) */
    uint32_t wifiTxFailRate;                                                /**< Wifi Tx fail rate (#/s) */
    uint32_t wifiTxErrorRate;                                               /**< Wifi Tx error rate (#/s) */
    uint32_t wifiTxFailEventCount;                                          /**< Wifi Tx fail events count */
    uint32_t videoStreamingTargetBitrate;                                   /**< Video streaming encoder target bitrate (bit/s) */
    uint32_t videoStreamingDecimation;                                      /**< Video streaming encoder decimation factor */
    uint32_t videoStreamingGopLength;                                       /**< Video streaming encoder GOP length */
    int32_t  videoStreamingPrevFrameType;                                   /**< Video streaming previous frame type */
    uint32_t videoStreamingPrevFrameSize;                                   /**< Video streaming previous frame size (bytes) */
    uint32_t videoStreamingPrevFrameMseY_fp8;                               /**< Video streaming previous frame MSE, 8 bits fixed point value */
    int32_t  videoRecordingPrevFrameType;                                   /**< Video recording previous frame type */
    uint32_t videoRecordingPrevFrameSize;                                   /**< Video recording previous frame size (bytes) */
    uint32_t videoRecordingPrevFrameMseY_fp8;                               /**< Video recording previous frame MSE, 8 bits fixed point value */
    uint32_t streamingMonitorTimeInterval;                                  /**< Streaming monitoring time interval (microseconds) */
    uint32_t streamingMeanAcqToNetworkTime;                                 /**< Streaming mean acquisition to network time (microseconds) */
    uint32_t streamingAcqToNetworkJitter;                                   /**< Streaming acquisition to network time jitter (microseconds) */
    uint32_t streamingMeanNetworkTime;                                      /**< Streaming mean acquisition to network time (microseconds) */
    uint32_t streamingNetworkJitter;                                        /**< Streaming acquisition to network time jitter (microseconds) */
    uint32_t streamingBytesSent;                                            /**< Streaming bytes sent during the interval */
    uint32_t streamingMeanPacketSize;                                       /**< Streaming mean packet size (bytes) */
    uint32_t streamingPacketSizeStdDev;                                     /**< Streaming packet size standard deviation (bytes) */
    uint32_t streamingPacketsSent;                                          /**< Streaming packets sent during the interval */
    uint32_t streamingBytesDropped;                                         /**< Streaming bytes dropped during the interval */
    uint32_t streamingNaluDropped;                                          /**< Streaming NAL units dropped during the interval */
    uint32_t commandsMaxTimeDeltaOnLastSec;                                 /**< Commands max reception time delta during the last second (microseconds) */
    uint32_t preReprojTimestampDelta;                                       /**< Pre-reprojection time (microseconds) */
    uint32_t postReprojTimestampDelta;                                      /**< Post-reprojection time (microseconds) */
    uint32_t postEeTimestampDelta;                                          /**< Post-EE time (microseconds) */
    uint32_t postScalingTimestampDelta;                                     /**< Post-scaling time (microseconds) */
    uint32_t postStreamingEncodingTimestampDelta;                           /**< Post-streaming-encoding time (microseconds) */
    uint32_t postRecordingEncodingTimestampDelta;                           /**< Post-recording-encoding time (microseconds) */
    uint32_t postNetworkInputTimestampDelta;                                /**< Post-network-input time (microseconds) */
    char serialNumberH[BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH + 1]; /**< Serial number high part */
    char serialNumberL[BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH + 1]; /**< Serial number low part */

} BEAVER_Parrot_DragonFrameInfoV1_t;


/**
 * @brief "Dragon Streaming" v1 definition.
 */
typedef struct
{
    uint8_t indexInGop;                                 /**< Frame index in GOP */
    uint8_t sliceCount;                                 /**< Frame slice count */
    /* uint16_t sliceMbCount[sliceCount]; */            /**< Slice macroblock count */

} BEAVER_Parrot_DragonStreamingV1_t;


/**
 * @brief "Dragon FrameInfo" v1 user data SEI.
 */
typedef struct
{
    uint32_t uuid[4];                                   /**< User data SEI UUID */
    BEAVER_Parrot_DragonFrameInfoV1_t frameInfo;        /**< Dragon FrameInfo v1 */

} BEAVER_Parrot_UserDataSeiDragonFrameInfoV1_t;


/**
 * @brief "Dragon Streaming" v1 user data SEI.
 */
typedef struct
{
    uint32_t uuid[4];                                   /**< User data SEI UUID */
    BEAVER_Parrot_DragonStreamingV1_t streaming;        /**< Dragon Streaming v1 */

} BEAVER_Parrot_UserDataSeiDragonStreamingV1_t;


/**
 * @brief "Dragon Streaming FrameInfo" v1 user data SEI.
 */
typedef struct
{
    uint32_t uuid[4];                                   /**< User data SEI UUID */
    BEAVER_Parrot_DragonFrameInfoV1_t frameInfo;        /**< Dragon FrameInfo v1 */
    BEAVER_Parrot_DragonStreamingV1_t streaming;        /**< Dragon Streaming v1 */

} BEAVER_Parrot_UserDataSeiDragonStreamingFrameInfoV1_t;


/**
 * @brief Get the user data SEI type.
 *
 * The function returns the user data SEI type if it is recognized.
 *
 * @param pBuf Pointer to the user data SEI buffer.
 * @param bufSize Size of the user data SEI.
 *
 * @return The user data SEI type if it is recognized.
 * @return BEAVER_PARROT_USER_DATA_SEI_UNKNOWN if the user data SEI type is not recognized.
 * @return -1 if an error occurred.
 */
BEAVER_Parrot_UserDataSeiTypes_t BEAVER_Parrot_GetUserDataSeiType(const void* pBuf, unsigned int bufSize);


/**
 * @brief Deserialize a "Dragon Basic" v1 user data SEI.
 *
 * The function parses a "Dragon Basic" v1 user data SEI and fills the userDataSei structure.
 *
 * @param pBuf Pointer to the user data SEI buffer.
 * @param bufSize Size of the user data SEI.
 * @param userDataSei Pointer to the structure to fill.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parrot_DeserializeUserDataSeiDragonBasicV1(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_UserDataSeiDragonBasicV1_t *userDataSei);


/**
 * @brief Deserialize a "Dragon Basic" v2 user data SEI.
 *
 * The function parses a "Dragon Basic" v2 user data SEI and fills the userDataSei structure.
 *
 * @param pBuf Pointer to the user data SEI buffer.
 * @param bufSize Size of the user data SEI.
 * @param userDataSei Pointer to the structure to fill.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parrot_DeserializeUserDataSeiDragonBasicV2(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_UserDataSeiDragonBasicV2_t *userDataSei);


/**
 * @brief Deserialize a "Dragon Extended" v1 user data SEI.
 *
 * The function parses a "Dragon Extended" v1 user data SEI and fills the userDataSei structure.
 *
 * @param pBuf Pointer to the user data SEI buffer.
 * @param bufSize Size of the user data SEI.
 * @param userDataSei Pointer to the structure to fill.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parrot_DeserializeUserDataSeiDragonExtendedV1(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_UserDataSeiDragonExtendedV1_t *userDataSei);


/**
 * @brief Deserialize a "Dragon Extended" v2 user data SEI.
 *
 * The function parses a "Dragon Extended" v2 user data SEI and fills the userDataSei structure.
 *
 * @param pBuf Pointer to the user data SEI buffer.
 * @param bufSize Size of the user data SEI.
 * @param userDataSei Pointer to the structure to fill.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parrot_DeserializeUserDataSeiDragonExtendedV2(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_UserDataSeiDragonExtendedV2_t *userDataSei);


/**
 * @brief Serialize a "Dragon FrameInfo" v1 user data SEI.
 *
 * The function parses a "Dragon FrameInfo" v1 structure and fills the user data SEI buffer.
 * bufSize must be at least sizeof(BEAVER_Parrot_UserDataSeiDragonFrameInfoV1_t).
 *
 * @param frameInfo Pointer to the FrameInfo structure.
 * @param pBuf Pointer to the user data SEI buffer to fill.
 * @param bufSize Size of the user data SEI buffer.
 * @param size Pointer to the final user data SEI buffer size.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parrot_SerializeUserDataSeiDragonFrameInfoV1(const BEAVER_Parrot_DragonFrameInfoV1_t *frameInfo, void* pBuf, unsigned int bufSize, unsigned int *size);


/**
 * @brief Deserialize a "Dragon FrameInfo" v1 user data SEI.
 *
 * The function parses a "Dragon FrameInfo" v1 user data SEI and fills the frameInfo structure.
 *
 * @param pBuf Pointer to the user data SEI buffer.
 * @param bufSize Size of the user data SEI.
 * @param frameInfo Pointer to the FrameInfo structure to fill.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parrot_DeserializeUserDataSeiDragonFrameInfoV1(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_DragonFrameInfoV1_t *frameInfo);


/**
 * @brief Serialize a "Dragon Streaming" v1 user data SEI.
 *
 * The function parses a "Dragon Streaming" v1 structure and fills the user data SEI buffer.
 * bufSize must be at least (sizeof(BEAVER_Parrot_UserDataSeiDragonStreamingV1_t) + streaming->sliceCount * sizeof(uint16_t)).
 *
 * @param streaming Pointer to the Streaming structure.
 * @param sliceMbCount Pointer to the sliceMbCount array.
 * @param pBuf Pointer to the user data SEI buffer to fill.
 * @param bufSize Size of the user data SEI buffer.
 * @param size Pointer to the final user data SEI buffer size.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parrot_SerializeUserDataSeiDragonStreamingV1(const BEAVER_Parrot_DragonStreamingV1_t *streaming, const uint16_t *sliceMbCount, void* pBuf, unsigned int bufSize, unsigned int *size);


/**
 * @brief Deserialize a "Dragon Streaming" v1 user data SEI.
 *
 * The function parses a "Dragon Streaming" v1 user data SEI and fills the streaming structure and sliceMbCount array.
 *
 * @param pBuf Pointer to the user data SEI buffer.
 * @param bufSize Size of the user data SEI.
 * @param streaming Pointer to the Streaming structure to fill.
 * @param sliceMbCount Pointer to the sliceMbCount array to fill (array size must be BEAVER_PARROT_DRAGON_MAX_SLICE_COUNT).
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parrot_DeserializeUserDataSeiDragonStreamingV1(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_DragonStreamingV1_t *streaming, uint16_t *sliceMbCount);


/**
 * @brief Serialize a "Dragon Streaming FrameInfo" v1 user data SEI.
 *
 * The function parses a "Dragon Streaming" v1 structure and a "Dragon FrameInfo" v1 structure and fills the user data SEI buffer.
 * bufSize must be at least (sizeof(BEAVER_Parrot_UserDataSeiDragonStreamingFrameInfoV1_t) + streaming->sliceCount * sizeof(uint16_t)).
 *
 * @param frameInfo Pointer to the FrameInfo structure.
 * @param streaming Pointer to the Streaming structure.
 * @param sliceMbCount Pointer to the sliceMbCount array.
 * @param pBuf Pointer to the user data SEI buffer to fill.
 * @param bufSize Size of the user data SEI buffer.
 * @param size Pointer to the final user data SEI buffer size.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parrot_SerializeUserDataSeiDragonStreamingFrameInfoV1(const BEAVER_Parrot_DragonFrameInfoV1_t *frameInfo, const BEAVER_Parrot_DragonStreamingV1_t *streaming, const uint16_t *sliceMbCount, void* pBuf, unsigned int bufSize, unsigned int *size);


/**
 * @brief Deserialize a "Dragon Streaming FrameInfo" v1 user data SEI.
 *
 * The function parses a "Dragon Streaming FrameInfo" v1 user data SEI and fills the frameInfo and streaming structures and the sliceMbCount array.
 *
 * @param pBuf Pointer to the user data SEI buffer.
 * @param bufSize Size of the user data SEI.
 * @param frameInfo Pointer to the FrameInfo structure to fill.
 * @param streaming Pointer to the Streaming structure to fill.
 * @param sliceMbCount Pointer to the sliceMbCount array to fill (array size must be BEAVER_PARROT_DRAGON_MAX_SLICE_COUNT).
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parrot_DeserializeUserDataSeiDragonStreamingFrameInfoV1(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_DragonFrameInfoV1_t *frameInfo, BEAVER_Parrot_DragonStreamingV1_t *streaming, uint16_t *sliceMbCount);


/**
 * @brief Serialize a "Dragon FrameInfo" v1 structure.
 *
 * The function parses a "Dragon FrameInfo" v1 structure and fills the buffer.
 * bufSize must be at least sizeof(BEAVER_Parrot_DragonFrameInfoV1_t).
 *
 * @param frameInfo Pointer to the FrameInfo structure.
 * @param pBuf Pointer to the FrameInfo buffer to fill.
 * @param bufSize Size of the FrameInfo buffer.
 * @param size Pointer to the final FrameInfo buffer size.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parrot_SerializeDragonFrameInfoV1(const BEAVER_Parrot_DragonFrameInfoV1_t *frameInfo, void* pBuf, unsigned int bufSize, unsigned int *size);


/**
 * @brief Deserialize a "Dragon FrameInfo" v1 buffer.
 *
 * The function parses a "Dragon FrameInfo" v1 buffer and fills the frameInfo structure.
 *
 * @param pBuf Pointer to the FrameInfo buffer.
 * @param bufSize Size of the FrameInfo buffer.
 * @param frameInfo Pointer to the FrameInfo structure to fill.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parrot_DeserializeDragonFrameInfoV1(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_DragonFrameInfoV1_t *frameInfo);


/**
 * @brief Serialize a "Dragon Streaming" v1 structure.
 *
 * The function parses a "Dragon Streaming" v1 structure and fills the buffer.
 * bufSize must be at least (sizeof(BEAVER_Parrot_DragonStreamingV1_t) + streaming->sliceCount * sizeof(uint16_t)).
 *
 * @param streaming Pointer to the Streaming structure.
 * @param sliceMbCount Pointer to the sliceMbCount array.
 * @param pBuf Pointer to the Streaming buffer to fill.
 * @param bufSize Size of the Streaming buffer.
 * @param size Pointer to the final Streaming buffer size.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parrot_SerializeDragonStreamingV1(const BEAVER_Parrot_DragonStreamingV1_t *streaming, const uint16_t *sliceMbCount, void* pBuf, unsigned int bufSize, unsigned int *size);


/**
 * @brief Deserialize a "Dragon Streaming" v1 buffer.
 *
 * The function parses a "Dragon Streaming" v1 buffer and fills the streaming structure and sliceMbCount array.
 *
 * @param pBuf Pointer to the Streaming buffer.
 * @param bufSize Size of the Streaming buffer.
 * @param streaming Pointer to the Streaming structure to fill.
 * @param sliceMbCount Pointer to the sliceMbCount array to fill (array size must be BEAVER_PARROT_DRAGON_MAX_SLICE_COUNT).
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parrot_DeserializeDragonStreamingV1(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_DragonStreamingV1_t *streaming, uint16_t *sliceMbCount);


/**
 * @brief Write a "Dragon FrameInfo" v1 header to a file.
 *
 * The function writes the "Dragon FrameInfo" v1 field names to a file in CSV line format.
 * No new line is appened at the end of the line.
 * The file must have been previously opened.
 *
 * @param frameInfoFile Output file.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parrot_WriteDragonFrameInfoV1HeaderToFile(FILE *frameInfoFile);


/**
 * @brief Write a "Dragon FrameInfo" v1 structure to a file.
 *
 * The function writes a "Dragon FrameInfo" v1 structure to a file in CSV line format.
 * No new line is appened at the end of the line.
 * The file must have been previously opened.
 *
 * @param frameInfo Pointer to the input FrameInfo structure.
 * @param frameInfoFile Output file.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parrot_WriteDragonFrameInfoV1ToFile(const BEAVER_Parrot_DragonFrameInfoV1_t* frameInfo, FILE *frameInfoFile);


#ifdef __cplusplus
}
#endif /* #ifdef __cplusplus */

#endif /* #ifndef _BEAVER_PARROT_H_ */

