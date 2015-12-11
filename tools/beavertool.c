/**
 * @file beaver.c
 * @brief Parrot Streaming Library Tools
 * @date 09/21/2015
 * @author aurelien.barre@parrot.com
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "../Includes/libBeaver/beaver_parser.h"
#include "../Includes/libBeaver/beaver_parrot.h"


#define BEAVER_FRAMEINFO_PSNR_MAX 48.130803609


static void BEAVER_PrintKmlHeader(FILE *kmlFile)
{
    fprintf(kmlFile, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(kmlFile, "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n");
    fprintf(kmlFile, "  <Document>\n");
    fprintf(kmlFile, "    <Placemark>\n");
    fprintf(kmlFile, "      <name>Flight</name>\n");
    fprintf(kmlFile, "      <description>...</description>\n");
    fprintf(kmlFile, "      <LineString>\n");
    fprintf(kmlFile, "        <altitudeMode>relativeToGround</altitudeMode>\n");
    fprintf(kmlFile, "        <coordinates>\n");
}


static void BEAVER_PrintKml(FILE *kmlFile, BEAVER_Parrot_UserDataSeiTypes_t userDataType, void *frameInfo)
{
    double latitude = 500., longitude = 500., height = 0.;

    switch (userDataType)
    {
        case BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V1:
        {
            BEAVER_Parrot_UserDataSeiDragonExtendedV1_t *userDataSeiDragonExtendedV1 = (BEAVER_Parrot_UserDataSeiDragonExtendedV1_t*)frameInfo;
            longitude = (double)userDataSeiDragonExtendedV1->longitude_fp20 / 1048576.;
            latitude = (double)userDataSeiDragonExtendedV1->latitude_fp20 / 1048576.;
            height = (double)userDataSeiDragonExtendedV1->relativeHeight_fp16 / 65536.;
            break;
        }
        case BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V2:
        {
            BEAVER_Parrot_UserDataSeiDragonExtendedV2_t *userDataSeiDragonExtendedV2 = (BEAVER_Parrot_UserDataSeiDragonExtendedV2_t*)frameInfo;
            longitude = (double)userDataSeiDragonExtendedV2->longitude_fp20 / 1048576.;
            latitude = (double)userDataSeiDragonExtendedV2->latitude_fp20 / 1048576.;
            height = (double)userDataSeiDragonExtendedV2->relativeHeight_fp16 / 65536.;
            break;
        }
        case BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1:
        case BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1:
        {
            BEAVER_Parrot_DragonFrameInfoV1_t *_frameInfo = (BEAVER_Parrot_DragonFrameInfoV1_t*)frameInfo;
            longitude = (double)_frameInfo->gpsLongitude_fp20 / 1048576.;
            latitude = (double)_frameInfo->gpsLatitude_fp20 / 1048576.;
            height = (double)_frameInfo->relativeHeight_fp16 / 65536.;
            break;
        }
        case BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V1:
        case BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V2:
        default:
            break;
    }


    if ((longitude != 500.) && (latitude != 500.))
    {
        fprintf(kmlFile, "          %.8f,%.8f,%.8f\n", longitude, latitude, height);
    }
}


static void BEAVER_PrintKmlFooter(FILE *kmlFile)
{
    fprintf(kmlFile, "        </coordinates>\n");
    fprintf(kmlFile, "      </LineString>\n");
    fprintf(kmlFile, "      <Style>\n");
    fprintf(kmlFile, "        <LineStyle>\n");
    fprintf(kmlFile, "          <color>#ff0000ff</color>\n");
    fprintf(kmlFile, "          <width>3</width>\n");
    fprintf(kmlFile, "        </LineStyle>\n");
    fprintf(kmlFile, "      </Style>\n");
    fprintf(kmlFile, "    </Placemark>\n");
    fprintf(kmlFile, "  </Document>\n");
    fprintf(kmlFile, "</kml>\n");
}


static void BEAVER_PrintFrameInfo(FILE *frameInfoFile, BEAVER_Parrot_UserDataSeiTypes_t userDataType, void *frameInfo, uint64_t acquisitionTs, uint64_t systemTs,
                                  unsigned int frameSize, double videoStreamingPrevFramePsnrY, double videoRecordingPrevFramePsnrY, int estimatedLostFrames)
{
    int ret;

    switch (userDataType)
    {
        case BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V1:
        {
            BEAVER_Parrot_UserDataSeiDragonBasicV1_t *userDataSeiDragonBasicV1 = (BEAVER_Parrot_UserDataSeiDragonBasicV1_t*)frameInfo;
            fprintf(frameInfoFile, "%lu ", (long unsigned int)userDataSeiDragonBasicV1->frameIndex);
            fprintf(frameInfoFile, "%llu ", (long long unsigned int)acquisitionTs);
            fprintf(frameInfoFile, "%llu ", (long long unsigned int)0);
            fprintf(frameInfoFile, "%lu %.8f %.8f %.3f ", 
                    (long unsigned int)0, 0., 0., 0.);
            fprintf(frameInfoFile, "%.3f %.3f %.3f %.3f %.3f %.3f ", 
                    0., 0., 0., 0., 0., 0.);
            fprintf(frameInfoFile, "%.3f %.3f %.3f %.3f %.3f ", 
                    0., 0., 0., 0., 0.);
            fprintf(frameInfoFile, "%d %d %d %d %d %.3f ", 
                    0, 0, 0, 0, 0, videoStreamingPrevFramePsnrY);
            fprintf(frameInfoFile, "%d %d %.3f ", 
                    0, 0, 0.);
            fprintf(frameInfoFile, "%d %d %d %d %d %d ", 
                    0, 0, 0, 0, 0, 0);
            fprintf(frameInfoFile, "%lu %lu %lu %lu %lu %lu %lu ", 
                    (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0);
            fprintf(frameInfoFile, "%lu ", 
                    (long unsigned int)0);
            fprintf(frameInfoFile, "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu ", 
                    (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0);
            fprintf(frameInfoFile, "%d %d %d ", 
                    0, 0, 0);
            fprintf(frameInfoFile, "%lu %d\n", 
                    (long unsigned int)frameSize, 
                    estimatedLostFrames);
            break;
        }
        case BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V2:
        {
            BEAVER_Parrot_UserDataSeiDragonBasicV2_t *userDataSeiDragonBasicV2 = (BEAVER_Parrot_UserDataSeiDragonBasicV2_t*)frameInfo;
            fprintf(frameInfoFile, "%lu ", (long unsigned int)userDataSeiDragonBasicV2->frameIndex);
            fprintf(frameInfoFile, "%llu ", (long long unsigned int)acquisitionTs);
            fprintf(frameInfoFile, "%llu ", (long long unsigned int)0);
            fprintf(frameInfoFile, "%lu %.8f %.8f %.3f ", 
                    (long unsigned int)0, 0., 0., 0.);
            fprintf(frameInfoFile, "%.3f %.3f %.3f %.3f %.3f %.3f ", 
                    0., 0., 0., 0., 0., 0.);
            fprintf(frameInfoFile, "%.3f %.3f %.3f %.3f %.3f ", 
                    0., 0., 0., 0., 0.);
            fprintf(frameInfoFile, "%d %d %d %d %d %.3f ", 
                    0, 0, 0, 0, 0, 0.);
            fprintf(frameInfoFile, "%d %d %.3f ", 
                    0, 0, 0.);
            fprintf(frameInfoFile, "%d %d %d %d %d %d ", 
                    0, 0, 0, 0, 0, 0);
            fprintf(frameInfoFile, "%lu %lu %lu %lu %lu %lu %lu ", 
                    (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0);
            fprintf(frameInfoFile, "%lu ", 
                    (long unsigned int)0);
            fprintf(frameInfoFile, "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu ", 
                    (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0);
            fprintf(frameInfoFile, "%d %d %d ", 
                    0, 0, 0);
            fprintf(frameInfoFile, "%lu %d\n", 
                    (long unsigned int)frameSize, 
                    estimatedLostFrames);
            break;
        }
        case BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V1:
        {
            BEAVER_Parrot_UserDataSeiDragonExtendedV1_t *userDataSeiDragonExtendedV1 = (BEAVER_Parrot_UserDataSeiDragonExtendedV1_t*)frameInfo;
            fprintf(frameInfoFile, "%lu ", (long unsigned int)userDataSeiDragonExtendedV1->frameIndex);
            fprintf(frameInfoFile, "%llu ", (long long unsigned int)acquisitionTs);
            fprintf(frameInfoFile, "%llu ", (long long unsigned int)systemTs);
            fprintf(frameInfoFile, "%lu %.8f %.8f %.3f ", 
                    (long unsigned int)userDataSeiDragonExtendedV1->batteryPercentage, 
                    (double)userDataSeiDragonExtendedV1->latitude_fp20 / 1048576., 
                    (double)userDataSeiDragonExtendedV1->longitude_fp20 / 1048576., 
                    (double)userDataSeiDragonExtendedV1->altitude_fp16 / 65536.);
            fprintf(frameInfoFile, "%.3f %.3f %.3f %.3f %.3f %.3f ", 
                    (float)userDataSeiDragonExtendedV1->absoluteHeight_fp16 / 65536., 
                    (float)userDataSeiDragonExtendedV1->relativeHeight_fp16 / 65536., 
                    (float)userDataSeiDragonExtendedV1->xSpeed_fp16 / 65536., 
                    (float)userDataSeiDragonExtendedV1->ySpeed_fp16 / 65536., 
                    (float)userDataSeiDragonExtendedV1->zSpeed_fp16 / 65536., 
                    (float)userDataSeiDragonExtendedV1->distance_fp16 / 65536.);
            fprintf(frameInfoFile, "%.3f %.3f %.3f %.3f %.3f ", 
                    (float)userDataSeiDragonExtendedV1->yaw_fp16 / 65536., 
                    (float)userDataSeiDragonExtendedV1->pitch_fp16 / 65536., 
                    (float)userDataSeiDragonExtendedV1->roll_fp16 / 65536., 
                    (float)userDataSeiDragonExtendedV1->cameraPan_fp16 / 65536., 
                    (float)userDataSeiDragonExtendedV1->cameraTilt_fp16 / 65536.);
            fprintf(frameInfoFile, "%d %d %d %d %d %.3f ", 
                    userDataSeiDragonExtendedV1->videoStreamingTargetBitrate,
                    0, 0, 0, 0,
                    videoStreamingPrevFramePsnrY);
            fprintf(frameInfoFile, "%d %d %.3f ", 
                    0, 0, 0.);
            fprintf(frameInfoFile, "%d %d %d %d %d %d ", 
                    userDataSeiDragonExtendedV1->wifiRssi, 
                    userDataSeiDragonExtendedV1->wifiMcsRate, 
                    userDataSeiDragonExtendedV1->wifiTxRate,
                    userDataSeiDragonExtendedV1->wifiRxRate,
                    userDataSeiDragonExtendedV1->wifiTxFailRate,
                    userDataSeiDragonExtendedV1->wifiTxErrorRate);
            fprintf(frameInfoFile, "%lu %lu %lu %lu %lu %lu %lu ", 
                    (long unsigned int)0, 
                    (long unsigned int)userDataSeiDragonExtendedV1->postReprojTimestampDelta, 
                    (long unsigned int)userDataSeiDragonExtendedV1->postEeTimestampDelta, 
                    (long unsigned int)userDataSeiDragonExtendedV1->postScalingTimestampDelta, 
                    (long unsigned int)userDataSeiDragonExtendedV1->postStreamingEncodingTimestampDelta, 
                    (long unsigned int)0, 
                    (long unsigned int)userDataSeiDragonExtendedV1->postNetworkInputTimestampDelta);
            fprintf(frameInfoFile, "%lu ", 
                    (long unsigned int)0);
            fprintf(frameInfoFile, "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu ", 
                    (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0);
            fprintf(frameInfoFile, "%d %d %d ", 
                    0, 0, 0);
            fprintf(frameInfoFile, "%lu %d\n", 
                    (long unsigned int)frameSize, 
                    estimatedLostFrames);
            break;
        }
        case BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V2:
        {
            BEAVER_Parrot_UserDataSeiDragonExtendedV2_t *userDataSeiDragonExtendedV2 = (BEAVER_Parrot_UserDataSeiDragonExtendedV2_t*)frameInfo;
            fprintf(frameInfoFile, "%lu ", (long unsigned int)userDataSeiDragonExtendedV2->frameIndex);
            fprintf(frameInfoFile, "%llu ", (long long unsigned int)acquisitionTs);
            fprintf(frameInfoFile, "%llu ", (long long unsigned int)systemTs);
            fprintf(frameInfoFile, "%lu %.8f %.8f %.3f ", 
                    (long unsigned int)userDataSeiDragonExtendedV2->batteryPercentage, 
                    (double)userDataSeiDragonExtendedV2->latitude_fp20 / 1048576., 
                    (double)userDataSeiDragonExtendedV2->longitude_fp20 / 1048576., 
                    (double)userDataSeiDragonExtendedV2->altitude_fp16 / 65536.);
            fprintf(frameInfoFile, "%.3f %.3f %.3f %.3f %.3f %.3f ", 
                    (float)userDataSeiDragonExtendedV2->absoluteHeight_fp16 / 65536., 
                    (float)userDataSeiDragonExtendedV2->relativeHeight_fp16 / 65536., 
                    (float)userDataSeiDragonExtendedV2->xSpeed_fp16 / 65536., 
                    (float)userDataSeiDragonExtendedV2->ySpeed_fp16 / 65536., 
                    (float)userDataSeiDragonExtendedV2->zSpeed_fp16 / 65536., 
                    (float)userDataSeiDragonExtendedV2->distance_fp16 / 65536.);
            fprintf(frameInfoFile, "%.3f %.3f %.3f %.3f %.3f ", 
                    (float)userDataSeiDragonExtendedV2->yaw_fp16 / 65536., 
                    (float)userDataSeiDragonExtendedV2->pitch_fp16 / 65536., 
                    (float)userDataSeiDragonExtendedV2->roll_fp16 / 65536., 
                    (float)userDataSeiDragonExtendedV2->cameraPan_fp16 / 65536., 
                    (float)userDataSeiDragonExtendedV2->cameraTilt_fp16 / 65536.);
            fprintf(frameInfoFile, "%d %d %d %d %d %.3f ", 
                    userDataSeiDragonExtendedV2->videoStreamingTargetBitrate,
                    userDataSeiDragonExtendedV2->videoStreamingDecimation,
                    userDataSeiDragonExtendedV2->videoStreamingGopLength,
                    userDataSeiDragonExtendedV2->videoStreamingPrevFrameType,
                    userDataSeiDragonExtendedV2->videoStreamingPrevFrameSize,
                    videoStreamingPrevFramePsnrY);
            fprintf(frameInfoFile, "%d %d %.3f ", 
                    userDataSeiDragonExtendedV2->videoRecordingPrevFrameType,
                    userDataSeiDragonExtendedV2->videoRecordingPrevFrameSize,
                    videoRecordingPrevFramePsnrY);
            fprintf(frameInfoFile, "%d %d %d %d %d %d ", 
                    userDataSeiDragonExtendedV2->wifiRssi, 
                    userDataSeiDragonExtendedV2->wifiMcsRate, 
                    userDataSeiDragonExtendedV2->wifiTxRate,
                    userDataSeiDragonExtendedV2->wifiRxRate,
                    userDataSeiDragonExtendedV2->wifiTxFailRate,
                    userDataSeiDragonExtendedV2->wifiTxErrorRate);
            fprintf(frameInfoFile, "%lu %lu %lu %lu %lu %lu %lu ", 
                    (long unsigned int)userDataSeiDragonExtendedV2->preReprojTimestampDelta, 
                    (long unsigned int)userDataSeiDragonExtendedV2->postReprojTimestampDelta, 
                    (long unsigned int)userDataSeiDragonExtendedV2->postEeTimestampDelta, 
                    (long unsigned int)userDataSeiDragonExtendedV2->postScalingTimestampDelta, 
                    (long unsigned int)userDataSeiDragonExtendedV2->postStreamingEncodingTimestampDelta, 
                    (long unsigned int)userDataSeiDragonExtendedV2->postRecordingEncodingTimestampDelta, 
                    (long unsigned int)userDataSeiDragonExtendedV2->postNetworkInputTimestampDelta);
            fprintf(frameInfoFile, "%lu ", 
                    (long unsigned int)userDataSeiDragonExtendedV2->streamingMonitorTimeInterval);
            if (userDataSeiDragonExtendedV2->streamingMonitorTimeInterval)
            {
                fprintf(frameInfoFile, "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n", 
                        (long unsigned int)((float)userDataSeiDragonExtendedV2->streamingMeanAcqToNetworkTime / (float)userDataSeiDragonExtendedV2->streamingMonitorTimeInterval * 1000000.), 
                        (long unsigned int)((float)userDataSeiDragonExtendedV2->streamingAcqToNetworkJitter / (float)userDataSeiDragonExtendedV2->streamingMonitorTimeInterval * 1000000.), 
                        (long unsigned int)((float)userDataSeiDragonExtendedV2->streamingMeanNetworkTime / (float)userDataSeiDragonExtendedV2->streamingMonitorTimeInterval * 1000000.), 
                        (long unsigned int)((float)userDataSeiDragonExtendedV2->streamingNetworkJitter / (float)userDataSeiDragonExtendedV2->streamingMonitorTimeInterval * 1000000.), 
                        (long unsigned int)((float)userDataSeiDragonExtendedV2->streamingBytesSent / (float)userDataSeiDragonExtendedV2->streamingMonitorTimeInterval * 1000000.), 
                        (long unsigned int)((float)userDataSeiDragonExtendedV2->streamingMeanPacketSize / (float)userDataSeiDragonExtendedV2->streamingMonitorTimeInterval * 1000000.), 
                        (long unsigned int)((float)userDataSeiDragonExtendedV2->streamingPacketSizeStdDev / (float)userDataSeiDragonExtendedV2->streamingMonitorTimeInterval * 1000000.), 
                        (long unsigned int)((float)userDataSeiDragonExtendedV2->streamingPacketsSent / (float)userDataSeiDragonExtendedV2->streamingMonitorTimeInterval * 1000000.), 
                        (long unsigned int)((float)userDataSeiDragonExtendedV2->streamingBytesDropped / (float)userDataSeiDragonExtendedV2->streamingMonitorTimeInterval * 1000000.), 
                        (long unsigned int)((float)userDataSeiDragonExtendedV2->streamingNaluDropped / (float)userDataSeiDragonExtendedV2->streamingMonitorTimeInterval * 1000000.));
            }
            else
            {
                fprintf(frameInfoFile, "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu ", 
                        (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0);
            }
            fprintf(frameInfoFile, "%d %d %d ", 
                    0, 0, 0);
            fprintf(frameInfoFile, "%lu %d\n", 
                    (long unsigned int)frameSize, 
                    estimatedLostFrames);
            break;
        }
        case BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1:
        case BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1:
        {
            BEAVER_Parrot_DragonFrameInfoV1_t *_frameInfo = (BEAVER_Parrot_DragonFrameInfoV1_t*)frameInfo;
            ret = BEAVER_Parrot_WriteDragonFrameInfoV1ToFile(_frameInfo, frameInfoFile);
            if (ret < 0)
            {
                fprintf(stderr, "Error: BEAVER_Parrot_WriteDragonFrameInfoV1ToFile() failed (%d)\n", ret);
            }
            fprintf(frameInfoFile, " %lu %d\n", 
                    (long unsigned int)frameSize, 
                    estimatedLostFrames);
            break;
        }
        default:
        {
            fprintf(frameInfoFile, "%lu ", (long unsigned int)0);
            fprintf(frameInfoFile, "%llu ", (long long unsigned int)0);
            fprintf(frameInfoFile, "%llu ", (long long unsigned int)0);
            fprintf(frameInfoFile, "%lu %.8f %.8f %.3f ", 
                    (long unsigned int)0, 0., 0., 0.);
            fprintf(frameInfoFile, "%.3f %.3f %.3f %.3f %.3f %.3f ", 
                    0., 0., 0., 0., 0., 0.);
            fprintf(frameInfoFile, "%.3f %.3f %.3f %.3f %.3f ", 
                    0., 0., 0., 0., 0.);
            fprintf(frameInfoFile, "%d %d %d %d %d %.3f ", 
                    0, 0, 0, 0, 0, 0.);
            fprintf(frameInfoFile, "%d %d %.3f ", 
                    0, 0, 0.);
            fprintf(frameInfoFile, "%d %d %d %d %d %d ", 
                    0, 0, 0, 0, 0, 0);
            fprintf(frameInfoFile, "%lu %lu %lu %lu %lu %lu %lu ", 
                    (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0);
            fprintf(frameInfoFile, "%lu ", 
                    (long unsigned int)0);
            fprintf(frameInfoFile, "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu ", 
                    (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0);
            fprintf(frameInfoFile, "%d %d %d ", 
                    0, 0, 0);
            fprintf(frameInfoFile, "%lu %d\n", 
                    (long unsigned int)frameSize, 
                    estimatedLostFrames);
            break;
        }
    }
}


static int BEAVER_ExtractFrameInfo(char *pszVideoFile, char *pszFrameInfoFile, char *pszKmlFile)
{
    int ret, result = EXIT_SUCCESS;
    int i, seiCount;
    BEAVER_Parser_Handle parser = NULL;
    BEAVER_Parser_Config_t parserConfig;
    FILE* fVideoFile = NULL;
    FILE* fFrameInfoFile = NULL;
    FILE* fKmlFile = NULL;
    unsigned long long videoFileSize;
    unsigned int userDataSize, frameSize = 0, frameSizeCumul = 0, frameCount = 0, naluSize;
    int auPending = 0, audPresent = 0, userDataSeiPresent = 0, estimatedLostFrames = 0;
    void* pUserDataBuf, *pFrameInfo = NULL;
    BEAVER_Parrot_UserDataSeiDragonBasicV1_t userDataSeiDragonBasicV1;
    BEAVER_Parrot_UserDataSeiDragonBasicV2_t userDataSeiDragonBasicV2;
    BEAVER_Parrot_UserDataSeiDragonExtendedV1_t userDataSeiDragonExtendedV1;
    BEAVER_Parrot_UserDataSeiDragonExtendedV2_t userDataSeiDragonExtendedV2;
    BEAVER_Parrot_DragonFrameInfoV1_t dragonFrameInfoV1;
    BEAVER_Parrot_DragonStreamingV1_t streamingInfo;
    uint16_t sliceMbCount[BEAVER_PARROT_DRAGON_MAX_SLICE_COUNT];
    BEAVER_Parrot_UserDataSeiTypes_t userDataType = BEAVER_PARROT_USER_DATA_SEI_UNKNOWN;
    double videoStreamingPrevFramePsnrY = 0., videoRecordingPrevFramePsnrY = 0., psnrStreamingCumul = 0., psnrRecordingCumul = 0.;
    int psnrStreamingCount = 0, psnrRecordingCount = 0;
    uint64_t acquisitionTs = 0, systemTs = 0;
    uint64_t firstTs = 0, lastTs = 0;
    uint32_t previousFrameIndex = 0;
    float duration;

    memset(&userDataSeiDragonBasicV1, 0, sizeof(userDataSeiDragonBasicV1));
    memset(&userDataSeiDragonBasicV2, 0, sizeof(userDataSeiDragonBasicV2));
    memset(&userDataSeiDragonExtendedV1, 0, sizeof(userDataSeiDragonExtendedV1));
    memset(&userDataSeiDragonExtendedV2, 0, sizeof(userDataSeiDragonExtendedV2));
    memset(&dragonFrameInfoV1, 0, sizeof(dragonFrameInfoV1));

    if ((!pszVideoFile) || (!strlen(pszVideoFile)))
    {
        fprintf(stderr, "Error: invalid input file name\n");
        result = EXIT_FAILURE;
        goto cleanup;
    }

    printf("Analyzing file '%s'...\n", pszVideoFile);

    fVideoFile = fopen(pszVideoFile, "rb");
    if (!fVideoFile)
    {
        fprintf(stderr, "Error: unable to open input file '%s'\n", pszVideoFile);
        result = EXIT_FAILURE;
        goto cleanup;
    }

    fseek(fVideoFile, 0, SEEK_END);
    videoFileSize = ftell(fVideoFile);
    fseek(fVideoFile, 0, SEEK_SET);

    if ((pszFrameInfoFile) && (strlen(pszFrameInfoFile)))
    {
        fFrameInfoFile = fopen(pszFrameInfoFile, "w");
        if (!fFrameInfoFile)
        {
            fprintf(stderr, "Error: unable to open file '%s'\n", pszFrameInfoFile);
            result = EXIT_FAILURE;
            goto cleanup;
        }
    }

    if ((pszKmlFile) && (strlen(pszKmlFile)))
    {
        fKmlFile = fopen(pszKmlFile, "w");
        if (!fKmlFile)
        {
            fprintf(stderr, "Error: unable to open file '%s'\n", pszKmlFile);
            result = EXIT_FAILURE;
            goto cleanup;
        }
    }

    if (fFrameInfoFile)
    {
        ret = BEAVER_Parrot_WriteDragonFrameInfoV1HeaderToFile(fFrameInfoFile);
        if (ret < 0)
        {
            fprintf(stderr, "Error: BEAVER_Parrot_WriteDragonFrameInfoV1HeaderToFile() failed (%d)\n", ret);
            result = EXIT_FAILURE;
            goto cleanup;
        }
        fprintf(fFrameInfoFile, " frameSize estimatedLostFrames\n");
    }

    if (fKmlFile)
    {
        BEAVER_PrintKmlHeader(fKmlFile);
    }

    memset(&parserConfig, 0, sizeof(parserConfig));
    parserConfig.extractUserDataSei = 1;
    parserConfig.printLogs = 0;

    ret = BEAVER_Parser_Init(&parser, &parserConfig);
    if (ret < 0)
    {
        fprintf(stderr, "Error: BEAVER_Parser_Init() failed (%d)\n", ret);
        result = EXIT_FAILURE;
        goto cleanup;
    }

    while ((ret = BEAVER_Parser_ReadNextNalu_file(parser, fVideoFile, videoFileSize, &naluSize)) >= 0)
    {
        ret = BEAVER_Parser_ParseNalu(parser);
        if (ret < 0)
        {
            fprintf(stderr, "Error: H264P_ParseNalu() failed (%d)\n", ret);
            result = EXIT_FAILURE;
            goto cleanup;
        }

        if (BEAVER_Parser_GetLastNaluType(parser) == 9)
        {
            // Access Unit Delimiter
            audPresent = 1;

            if (auPending)
            {
                // write data for the previous access unit
                if (fFrameInfoFile) BEAVER_PrintFrameInfo(fFrameInfoFile, userDataType, pFrameInfo, acquisitionTs, systemTs, frameSize,
                                                          videoStreamingPrevFramePsnrY, videoRecordingPrevFramePsnrY, estimatedLostFrames);
                if (fKmlFile) BEAVER_PrintKml(fKmlFile, userDataType, pFrameInfo);
            }

            frameSizeCumul += frameSize;
            frameCount++;
            frameSize = 0;
        }
        else if (BEAVER_Parser_GetLastNaluType(parser) == 6)
        {
            // SEI NAL unit
            if (!audPresent)
            {
                if (auPending)
                {
                    // write data for the previous access unit
                    if (fFrameInfoFile) BEAVER_PrintFrameInfo(fFrameInfoFile, userDataType, pFrameInfo, acquisitionTs, systemTs, frameSize,
                                                              videoStreamingPrevFramePsnrY, videoRecordingPrevFramePsnrY, estimatedLostFrames);
                    if (fKmlFile) BEAVER_PrintKml(fKmlFile, userDataType, pFrameInfo);
                }

                frameSizeCumul += frameSize;
                frameCount++;
                frameSize = 0;
            }

            seiCount = BEAVER_Parser_GetUserDataSeiCount(parser);
            for (i = 0; i < seiCount; i++)
            {
                ret = BEAVER_Parser_GetUserDataSei(parser, i, &pUserDataBuf, &userDataSize);
                if (ret >= 0)
                {
                    userDataSeiPresent = 1;
                    userDataType = BEAVER_Parrot_GetUserDataSeiType(pUserDataBuf, userDataSize);
                    pFrameInfo = NULL;

                    switch (userDataType)
                    {
                        case BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V1:
                            pFrameInfo = (void*)&userDataSeiDragonBasicV1;
                            ret = BEAVER_Parrot_DeserializeUserDataSeiDragonBasicV1(pUserDataBuf, userDataSize, &userDataSeiDragonBasicV1);
                            if (ret >= 0)
                            {
                                acquisitionTs = ((uint64_t)userDataSeiDragonBasicV1.acquisitionTsH << 32) + (uint64_t)userDataSeiDragonBasicV1.acquisitionTsL;
                                if ((acquisitionTs) && (!firstTs)) firstTs = acquisitionTs;
                                if (acquisitionTs) lastTs = acquisitionTs;
                                if (previousFrameIndex != 0)
                                {
                                    estimatedLostFrames = userDataSeiDragonBasicV1.frameIndex - previousFrameIndex - 1;
                                }
                                previousFrameIndex = userDataSeiDragonBasicV1.frameIndex;
                                if (userDataSeiDragonBasicV1.prevMse_fp8)
                                {
                                    videoStreamingPrevFramePsnrY = BEAVER_FRAMEINFO_PSNR_MAX - 10. * log10((double)userDataSeiDragonBasicV1.prevMse_fp8 / 256.);
                                    psnrStreamingCumul += videoStreamingPrevFramePsnrY;
                                    psnrStreamingCount++;
                                }
                            }
                            else
                            {
                                fprintf(stderr, "Error: BEAVER_Parrot_DeserializeUserDataSeiDragonBasicV1() failed (%d)\n", ret);
                            }
                            break;
                        case BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V2:
                            pFrameInfo = (void*)&userDataSeiDragonBasicV2;
                            ret = BEAVER_Parrot_DeserializeUserDataSeiDragonBasicV2(pUserDataBuf, userDataSize, &userDataSeiDragonBasicV2);
                            if (ret >= 0)
                            {
                                acquisitionTs = ((uint64_t)userDataSeiDragonBasicV2.acquisitionTsH << 32) + (uint64_t)userDataSeiDragonBasicV2.acquisitionTsL;
                                if ((acquisitionTs) && (!firstTs)) firstTs = acquisitionTs;
                                if (acquisitionTs) lastTs = acquisitionTs;
                                if (previousFrameIndex != 0)
                                {
                                    estimatedLostFrames = userDataSeiDragonBasicV2.frameIndex - previousFrameIndex - 1;
                                }
                                previousFrameIndex = userDataSeiDragonBasicV2.frameIndex;
                            }
                            else
                            {
                                fprintf(stderr, "Error: BEAVER_Parrot_DeserializeUserDataSeiDragonBasicV2() failed (%d)\n", ret);
                            }
                            break;
                        case BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V1:
                            pFrameInfo = (void*)&userDataSeiDragonExtendedV1;
                            ret = BEAVER_Parrot_DeserializeUserDataSeiDragonExtendedV1(pUserDataBuf, userDataSize, &userDataSeiDragonExtendedV1);
                            if (ret >= 0)
                            {
                                acquisitionTs = ((uint64_t)userDataSeiDragonExtendedV1.acquisitionTsH << 32) + (uint64_t)userDataSeiDragonExtendedV1.acquisitionTsL;
                                if ((acquisitionTs) && (!firstTs)) firstTs = acquisitionTs;
                                if (acquisitionTs) lastTs = acquisitionTs;
                                systemTs = ((uint64_t)userDataSeiDragonExtendedV1.systemTsH << 32) + (uint64_t)userDataSeiDragonExtendedV1.systemTsL;
                                if (previousFrameIndex != 0)
                                {
                                    estimatedLostFrames = userDataSeiDragonExtendedV1.frameIndex - previousFrameIndex - 1;
                                }
                                previousFrameIndex = userDataSeiDragonExtendedV1.frameIndex;
                                if (userDataSeiDragonExtendedV1.prevMse_fp8)
                                {
                                    videoStreamingPrevFramePsnrY = BEAVER_FRAMEINFO_PSNR_MAX - 10. * log10((double)userDataSeiDragonExtendedV1.prevMse_fp8 / 256.);
                                    psnrStreamingCumul += videoStreamingPrevFramePsnrY;
                                    psnrStreamingCount++;
                                }
                            }
                            else
                            {
                                fprintf(stderr, "Error: BEAVER_Parrot_DeserializeUserDataSeiDragonExtendedV1() failed (%d)\n", ret);
                            }
                            break;
                        case BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V2:
                            pFrameInfo = (void*)&userDataSeiDragonExtendedV2;
                            ret = BEAVER_Parrot_DeserializeUserDataSeiDragonExtendedV2(pUserDataBuf, userDataSize, &userDataSeiDragonExtendedV2);
                            if (ret >= 0)
                            {
                                acquisitionTs = ((uint64_t)userDataSeiDragonExtendedV2.acquisitionTsH << 32) + (uint64_t)userDataSeiDragonExtendedV2.acquisitionTsL;
                                if ((acquisitionTs) && (!firstTs)) firstTs = acquisitionTs;
                                if (acquisitionTs) lastTs = acquisitionTs;
                                systemTs = ((uint64_t)userDataSeiDragonExtendedV2.systemTsH << 32) + (uint64_t)userDataSeiDragonExtendedV2.systemTsL;
                                if (previousFrameIndex != 0)
                                {
                                    estimatedLostFrames = userDataSeiDragonExtendedV2.frameIndex - previousFrameIndex - 1;
                                }
                                previousFrameIndex = userDataSeiDragonExtendedV2.frameIndex;
                                if (userDataSeiDragonExtendedV2.videoStreamingPrevFrameMseY_fp8)
                                {
                                    videoStreamingPrevFramePsnrY = BEAVER_FRAMEINFO_PSNR_MAX - 10. * log10((double)userDataSeiDragonExtendedV2.videoStreamingPrevFrameMseY_fp8 / 256.);
                                    psnrStreamingCumul += videoStreamingPrevFramePsnrY;
                                    psnrStreamingCount++;
                                }
                                if (userDataSeiDragonExtendedV2.videoRecordingPrevFrameMseY_fp8)
                                {
                                    videoRecordingPrevFramePsnrY = BEAVER_FRAMEINFO_PSNR_MAX - 10. * log10((double)userDataSeiDragonExtendedV2.videoRecordingPrevFrameMseY_fp8 / 256.);
                                    psnrRecordingCumul += videoRecordingPrevFramePsnrY;
                                    psnrRecordingCount++;
                                }
                            }
                            else
                            {
                                fprintf(stderr, "Error: BEAVER_Parrot_DeserializeUserDataSeiDragonExtendedV2() failed (%d)\n", ret);
                            }
                            break;
                        case BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1:
                            pFrameInfo = (void*)&dragonFrameInfoV1;
                            ret = BEAVER_Parrot_DeserializeUserDataSeiDragonFrameInfoV1(pUserDataBuf, userDataSize, &dragonFrameInfoV1);
                            if (ret >= 0)
                            {
                                acquisitionTs = ((uint64_t)dragonFrameInfoV1.acquisitionTsH << 32) + (uint64_t)dragonFrameInfoV1.acquisitionTsL;
                                if ((acquisitionTs) && (!firstTs)) firstTs = acquisitionTs;
                                if (acquisitionTs) lastTs = acquisitionTs;
                                systemTs = ((uint64_t)dragonFrameInfoV1.systemTsH << 32) + (uint64_t)dragonFrameInfoV1.systemTsL;
                                if (previousFrameIndex != 0)
                                {
                                    estimatedLostFrames = dragonFrameInfoV1.frameIndex - previousFrameIndex - 1;
                                }
                                previousFrameIndex = dragonFrameInfoV1.frameIndex;
                                if (dragonFrameInfoV1.videoStreamingPrevFrameMseY_fp8)
                                {
                                    videoStreamingPrevFramePsnrY = BEAVER_FRAMEINFO_PSNR_MAX - 10. * log10((double)dragonFrameInfoV1.videoStreamingPrevFrameMseY_fp8 / 256.);
                                    psnrStreamingCumul += videoStreamingPrevFramePsnrY;
                                    psnrStreamingCount++;
                                }
                                if (dragonFrameInfoV1.videoRecordingPrevFrameMseY_fp8)
                                {
                                    videoRecordingPrevFramePsnrY = BEAVER_FRAMEINFO_PSNR_MAX - 10. * log10((double)dragonFrameInfoV1.videoRecordingPrevFrameMseY_fp8 / 256.);
                                    psnrRecordingCumul += videoRecordingPrevFramePsnrY;
                                    psnrRecordingCount++;
                                }
                            }
                            else
                            {
                                fprintf(stderr, "Error: BEAVER_Parrot_DeserializeUserDataSeiDragonFrameInfoV1() failed (%d)\n", ret);
                            }
                            break;
                        case BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1:
                            pFrameInfo = (void*)&dragonFrameInfoV1;
                            ret = BEAVER_Parrot_DeserializeUserDataSeiDragonStreamingFrameInfoV1(pUserDataBuf, userDataSize, &dragonFrameInfoV1, &streamingInfo, sliceMbCount);
                            if (ret >= 0)
                            {
                                acquisitionTs = ((uint64_t)dragonFrameInfoV1.acquisitionTsH << 32) + (uint64_t)dragonFrameInfoV1.acquisitionTsL;
                                if ((acquisitionTs) && (!firstTs)) firstTs = acquisitionTs;
                                if (acquisitionTs) lastTs = acquisitionTs;
                                systemTs = ((uint64_t)dragonFrameInfoV1.systemTsH << 32) + (uint64_t)dragonFrameInfoV1.systemTsL;
                                if (previousFrameIndex != 0)
                                {
                                    estimatedLostFrames = dragonFrameInfoV1.frameIndex - previousFrameIndex - 1;
                                }
                                previousFrameIndex = dragonFrameInfoV1.frameIndex;
                                if (dragonFrameInfoV1.videoStreamingPrevFrameMseY_fp8)
                                {
                                    videoStreamingPrevFramePsnrY = BEAVER_FRAMEINFO_PSNR_MAX - 10. * log10((double)dragonFrameInfoV1.videoStreamingPrevFrameMseY_fp8 / 256.);
                                    psnrStreamingCumul += videoStreamingPrevFramePsnrY;
                                    psnrStreamingCount++;
                                }
                                if (dragonFrameInfoV1.videoRecordingPrevFrameMseY_fp8)
                                {
                                    videoRecordingPrevFramePsnrY = BEAVER_FRAMEINFO_PSNR_MAX - 10. * log10((double)dragonFrameInfoV1.videoRecordingPrevFrameMseY_fp8 / 256.);
                                    psnrRecordingCumul += videoRecordingPrevFramePsnrY;
                                    psnrRecordingCount++;
                                }
                            }
                            else
                            {
                                fprintf(stderr, "Error: BEAVER_Parrot_DeserializeUserDataSeiDragonStreamingFrameInfoV1() failed (%d)\n", ret);
                            }
                            break;
                        default:
                            break;
                    }
                }
                else
                {
                    fprintf(stderr, "Error: BEAVER_Parser_GetUserDataSei() failed (%d)\n", ret);
                }
            }
        }
        else if ((BEAVER_Parser_GetLastNaluType(parser) == 1) || (BEAVER_Parser_GetLastNaluType(parser) == 5))
        {
            // Slice NAL unit
            if ((!audPresent) && (!userDataSeiPresent))
            {
                //TODO: this does not work in multislices!
                if (auPending)
                {
                    // write data for the previous access unit
                    if (fFrameInfoFile) BEAVER_PrintFrameInfo(fFrameInfoFile, userDataType, pFrameInfo, acquisitionTs, systemTs, frameSize,
                                                              videoStreamingPrevFramePsnrY, videoRecordingPrevFramePsnrY, estimatedLostFrames);
                    if (fKmlFile) BEAVER_PrintKml(fKmlFile, userDataType, pFrameInfo);
                }

                frameSizeCumul += frameSize;
                frameCount++;
                frameSize = 0;
            }

            auPending = 1;
        }

        frameSize += naluSize;
    }
    if (ret == -2)
    {
        printf("No more start codes in the file\n");
    }
    else if (ret < 0)
    {
        fprintf(stderr, "Error: H264P_ReadNextNalu_file() failed (%d)\n", ret);
    }

    if (auPending)
    {
        // write data for the last access unit
        if (fFrameInfoFile) BEAVER_PrintFrameInfo(fFrameInfoFile, userDataType, pFrameInfo, acquisitionTs, systemTs, frameSize,
                                                  videoStreamingPrevFramePsnrY, videoRecordingPrevFramePsnrY, estimatedLostFrames);
        if (fKmlFile) BEAVER_PrintKml(fKmlFile, userDataType, pFrameInfo);

        frameSizeCumul += frameSize;
        frameCount++;
    }

    if (fKmlFile)
    {
        BEAVER_PrintKmlFooter(fKmlFile);
    }

    duration = (float)(lastTs - firstTs) / 1000000.;
    printf("\nDuration: %.1f seconds\n", duration);
    if (duration != 0.) printf("Mean framerate: %.2f fps\n", (float)frameCount / duration);
    if (duration != 0.) printf("Mean bitrate: %d bit/s\n", (int)((float)frameSizeCumul * 8 / duration));
    if (psnrStreamingCount) printf("Mean streaming PSNR(Y): %.2f dB\n", psnrStreamingCumul / psnrStreamingCount);
    if (psnrRecordingCount) printf("Mean recording PSNR(Y): %.2f dB\n", psnrRecordingCumul / psnrRecordingCount);

    printf("\nDone!\n");

cleanup:
    if (parser) BEAVER_Parser_Free(parser);
    if (fVideoFile) fclose(fVideoFile);
    if (fFrameInfoFile) fclose(fFrameInfoFile);
    if (fKmlFile) fclose(fKmlFile);

    return result;
}


static void BEAVER_Usage(int argc, char **argv)
{
    printf("Parrot Beaver Tool\n\n");
    printf("Usage:\n");
    printf("\t%s extract <h264_es_file> <frameinfo_file> [<kml_file>]\n", argv[0]);
    printf("\n");
}


int main(int argc, char **argv)
{
    int result = EXIT_SUCCESS;

    if (argc < 2)
    {
        BEAVER_Usage(argc, argv);
        exit(EXIT_FAILURE);
    }

    if (!strncmp(argv[1], "extract", 7))
    {
        if (argc < 4)
        {
            BEAVER_Usage(argc, argv);
            exit(EXIT_FAILURE);
        }
        else
        {
            result = BEAVER_ExtractFrameInfo(argv[2], argv[3], (argc >= 5) ? argv[4] : NULL);
        }
    }
    else
    {
        BEAVER_Usage(argc, argv);
        exit(EXIT_FAILURE);
    }

    exit(result);
}

