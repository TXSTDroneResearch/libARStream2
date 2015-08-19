/**
 * @file beaver_parrot.c
 * @brief H.264 Elementary Stream Tools Library - Parrot user data SEI definition and parser
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#include <string.h>
#include <arpa/inet.h>

#include <beaver/beaver_parrot.h>


int BEAVER_Parrot_ParseDragonBasicUserDataSeiV1(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_DragonBasicUserDataSeiV1_t *userDataSei)
{
    const uint32_t* pdwBuf = (uint32_t*)pBuf;
    
    if (!pBuf)
    {
        return -1;
    }
    
    if (bufSize < sizeof(BEAVER_Parrot_DragonBasicUserDataSeiV1_t))
    {
        return -1;
    }

    userDataSei->uuid[0] = ntohl(*(pdwBuf++));
    userDataSei->uuid[1] = ntohl(*(pdwBuf++));
    userDataSei->uuid[2] = ntohl(*(pdwBuf++));
    userDataSei->uuid[3] = ntohl(*(pdwBuf++));
    if ((userDataSei->uuid[0] != BEAVER_PARROT_DRAGON_BASIC_USER_DATA_SEI_V1_UUID_0) || (userDataSei->uuid[1] != BEAVER_PARROT_DRAGON_BASIC_USER_DATA_SEI_V1_UUID_1) 
            || (userDataSei->uuid[2] != BEAVER_PARROT_DRAGON_BASIC_USER_DATA_SEI_V1_UUID_2) || (userDataSei->uuid[3] != BEAVER_PARROT_DRAGON_BASIC_USER_DATA_SEI_V1_UUID_3))
    {
        return -1;
    }
    
    userDataSei->frameIndex = ntohl(*(pdwBuf++));
    userDataSei->acquisitionTsH = ntohl(*(pdwBuf++));
    userDataSei->acquisitionTsL = ntohl(*(pdwBuf++));
    userDataSei->prevMse_fp8 = ntohl(*(pdwBuf++));
    
    return 0;
}


int BEAVER_Parrot_ParseDragonBasicUserDataSeiV2(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_DragonBasicUserDataSeiV2_t *userDataSei)
{
    const uint32_t* pdwBuf = (uint32_t*)pBuf;
    
    if (!pBuf)
    {
        return -1;
    }
    
    if (bufSize < sizeof(BEAVER_Parrot_DragonBasicUserDataSeiV2_t))
    {
        return -1;
    }

    userDataSei->uuid[0] = ntohl(*(pdwBuf++));
    userDataSei->uuid[1] = ntohl(*(pdwBuf++));
    userDataSei->uuid[2] = ntohl(*(pdwBuf++));
    userDataSei->uuid[3] = ntohl(*(pdwBuf++));
    if ((userDataSei->uuid[0] != BEAVER_PARROT_DRAGON_BASIC_USER_DATA_SEI_V2_UUID_0) || (userDataSei->uuid[1] != BEAVER_PARROT_DRAGON_BASIC_USER_DATA_SEI_V2_UUID_1) 
            || (userDataSei->uuid[2] != BEAVER_PARROT_DRAGON_BASIC_USER_DATA_SEI_V2_UUID_2) || (userDataSei->uuid[3] != BEAVER_PARROT_DRAGON_BASIC_USER_DATA_SEI_V2_UUID_3))
    {
        return -1;
    }
    
    userDataSei->frameIndex = ntohl(*(pdwBuf++));
    userDataSei->acquisitionTsH = ntohl(*(pdwBuf++));
    userDataSei->acquisitionTsL = ntohl(*(pdwBuf++));
    
    return 0;
}


int BEAVER_Parrot_ParseDragonExtendedUserDataSeiV1(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_DragonExtendedUserDataSeiV1_t *userDataSei)
{
    const uint32_t* pdwBuf = (uint32_t*)pBuf;
    const char* pszBuf;
    
    if (!pBuf)
    {
        return -1;
    }
    
    if (bufSize < sizeof(BEAVER_Parrot_DragonExtendedUserDataSeiV1_t))
    {
        return -1;
    }

    userDataSei->uuid[0] = ntohl(*(pdwBuf++));
    userDataSei->uuid[1] = ntohl(*(pdwBuf++));
    userDataSei->uuid[2] = ntohl(*(pdwBuf++));
    userDataSei->uuid[3] = ntohl(*(pdwBuf++));
    if ((userDataSei->uuid[0] != BEAVER_PARROT_DRAGON_EXTENDED_USER_DATA_SEI_V1_UUID_0) || (userDataSei->uuid[1] != BEAVER_PARROT_DRAGON_EXTENDED_USER_DATA_SEI_V1_UUID_1) 
            || (userDataSei->uuid[2] != BEAVER_PARROT_DRAGON_EXTENDED_USER_DATA_SEI_V1_UUID_2) || (userDataSei->uuid[3] != BEAVER_PARROT_DRAGON_EXTENDED_USER_DATA_SEI_V1_UUID_3))
    {
        return -1;
    }
    
    userDataSei->frameIndex = ntohl(*(pdwBuf++));
    userDataSei->acquisitionTsH = ntohl(*(pdwBuf++));
    userDataSei->acquisitionTsL = ntohl(*(pdwBuf++));
    userDataSei->prevMse_fp8 = ntohl(*(pdwBuf++));
    userDataSei->batteryPercentage = ntohl(*(pdwBuf++));
    userDataSei->latitude_fp20 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->longitude_fp20 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->altitude_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->absoluteHeight_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->relativeHeight_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->xSpeed_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->ySpeed_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->zSpeed_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->distance_fp16 = ntohl(*(pdwBuf++));
    userDataSei->heading_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->yaw_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->pitch_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->roll_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->cameraPan_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->cameraTilt_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->videoStreamingTargetBitrate = ntohl(*(pdwBuf++));
    userDataSei->wifiRssi = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->wifiMcsRate = ntohl(*(pdwBuf++));
    userDataSei->wifiTxRate = ntohl(*(pdwBuf++));
    userDataSei->wifiRxRate = ntohl(*(pdwBuf++));
    userDataSei->wifiTxFailRate = ntohl(*(pdwBuf++));
    userDataSei->wifiTxErrorRate = ntohl(*(pdwBuf++));
    userDataSei->postReprojTimestampDelta = ntohl(*(pdwBuf++));
    userDataSei->postEeTimestampDelta = ntohl(*(pdwBuf++));
    userDataSei->postScalingTimestampDelta = ntohl(*(pdwBuf++));
    userDataSei->postStreamingEncodingTimestampDelta = ntohl(*(pdwBuf++));
    userDataSei->postNetworkInputTimestampDelta = ntohl(*(pdwBuf++));
    userDataSei->systemTsH = ntohl(*(pdwBuf++));
    userDataSei->systemTsL = ntohl(*(pdwBuf++));

    pszBuf = (char*)pdwBuf;
    strncpy(userDataSei->serialNumberH, pszBuf, BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH + 1);
    pszBuf += BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH + 1;
    strncpy(userDataSei->serialNumberL, pszBuf, BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH + 1);

    return 0;
}


int BEAVER_Parrot_ParseDragonExtendedUserDataSeiV2(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_DragonExtendedUserDataSeiV2_t *userDataSei)
{
    const uint32_t* pdwBuf = (uint32_t*)pBuf;
    const char* pszBuf;
    
    if (!pBuf)
    {
        return -1;
    }
    
    if (bufSize < sizeof(BEAVER_Parrot_DragonExtendedUserDataSeiV2_t))
    {
        return -1;
    }

    userDataSei->uuid[0] = ntohl(*(pdwBuf++));
    userDataSei->uuid[1] = ntohl(*(pdwBuf++));
    userDataSei->uuid[2] = ntohl(*(pdwBuf++));
    userDataSei->uuid[3] = ntohl(*(pdwBuf++));
    if ((userDataSei->uuid[0] != BEAVER_PARROT_DRAGON_EXTENDED_USER_DATA_SEI_V2_UUID_0) || (userDataSei->uuid[1] != BEAVER_PARROT_DRAGON_EXTENDED_USER_DATA_SEI_V2_UUID_1) 
            || (userDataSei->uuid[2] != BEAVER_PARROT_DRAGON_EXTENDED_USER_DATA_SEI_V2_UUID_2) || (userDataSei->uuid[3] != BEAVER_PARROT_DRAGON_EXTENDED_USER_DATA_SEI_V2_UUID_3))
    {
        return -1;
    }
    
    userDataSei->frameIndex = ntohl(*(pdwBuf++));
    userDataSei->acquisitionTsH = ntohl(*(pdwBuf++));
    userDataSei->acquisitionTsL = ntohl(*(pdwBuf++));
    userDataSei->batteryPercentage = ntohl(*(pdwBuf++));
    userDataSei->latitude_fp20 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->longitude_fp20 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->altitude_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->absoluteHeight_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->relativeHeight_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->xSpeed_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->ySpeed_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->zSpeed_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->distance_fp16 = ntohl(*(pdwBuf++));
    userDataSei->yaw_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->pitch_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->roll_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->cameraPan_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->cameraTilt_fp16 = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->videoStreamingTargetBitrate = ntohl(*(pdwBuf++));
    userDataSei->videoStreamingDecimation = ntohl(*(pdwBuf++));
    userDataSei->videoStreamingGopLength = ntohl(*(pdwBuf++));
    userDataSei->videoStreamingPrevFrameType = ntohl(*(pdwBuf++));
    userDataSei->videoStreamingPrevFrameSize = ntohl(*(pdwBuf++));
    userDataSei->videoStreamingPrevFrameMseY_fp8 = ntohl(*(pdwBuf++));
    userDataSei->videoRecordingPrevFrameType = ntohl(*(pdwBuf++));
    userDataSei->videoRecordingPrevFrameSize = ntohl(*(pdwBuf++));
    userDataSei->videoRecordingPrevFrameMseY_fp8 = ntohl(*(pdwBuf++));
    userDataSei->wifiRssi = (int32_t)ntohl(*(pdwBuf++));
    userDataSei->wifiMcsRate = ntohl(*(pdwBuf++));
    userDataSei->wifiTxRate = ntohl(*(pdwBuf++));
    userDataSei->wifiRxRate = ntohl(*(pdwBuf++));
    userDataSei->wifiTxFailRate = ntohl(*(pdwBuf++));
    userDataSei->wifiTxErrorRate = ntohl(*(pdwBuf++));
    userDataSei->preReprojTimestampDelta = ntohl(*(pdwBuf++));
    userDataSei->postReprojTimestampDelta = ntohl(*(pdwBuf++));
    userDataSei->postEeTimestampDelta = ntohl(*(pdwBuf++));
    userDataSei->postScalingTimestampDelta = ntohl(*(pdwBuf++));
    userDataSei->postStreamingEncodingTimestampDelta = ntohl(*(pdwBuf++));
    userDataSei->postRecordingEncodingTimestampDelta = ntohl(*(pdwBuf++));
    userDataSei->postNetworkInputTimestampDelta = ntohl(*(pdwBuf++));
    userDataSei->systemTsH = ntohl(*(pdwBuf++));
    userDataSei->systemTsL = ntohl(*(pdwBuf++));
    userDataSei->streamingMonitorTimeInterval = ntohl(*(pdwBuf++));
    userDataSei->streamingMeanAcqToNetworkTime = ntohl(*(pdwBuf++));
    userDataSei->streamingAcqToNetworkJitter = ntohl(*(pdwBuf++));
    userDataSei->streamingMeanNetworkTime = ntohl(*(pdwBuf++));
    userDataSei->streamingNetworkJitter = ntohl(*(pdwBuf++));
    userDataSei->streamingBytesSent = ntohl(*(pdwBuf++));
    userDataSei->streamingMeanPacketSize = ntohl(*(pdwBuf++));
    userDataSei->streamingPacketSizeStdDev = ntohl(*(pdwBuf++));
    userDataSei->streamingPacketsSent = ntohl(*(pdwBuf++));
    userDataSei->streamingBytesDropped = ntohl(*(pdwBuf++));
    userDataSei->streamingNaluDropped = ntohl(*(pdwBuf++));

    pszBuf = (char*)pdwBuf;
    strncpy(userDataSei->serialNumberH, pszBuf, BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH + 1);
    pszBuf += BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH + 1;
    strncpy(userDataSei->serialNumberL, pszBuf, BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH + 1);

    return 0;
}


BEAVER_Parrot_UserDataSeiTypes_t BEAVER_Parrot_GetUserDataSeiType(const void* pBuf, unsigned int bufSize)
{
    uint32_t uuid0, uuid1, uuid2, uuid3;
    
    if (!pBuf)
    {
        return -1;
    }
    
    if (bufSize < 16)
    {
        return -1;
    }

    uuid0 = ntohl(*((uint32_t*)pBuf));
    uuid1 = ntohl(*((uint32_t*)pBuf + 1));
    uuid2 = ntohl(*((uint32_t*)pBuf + 2));
    uuid3 = ntohl(*((uint32_t*)pBuf + 3));

    if ((uuid0 == BEAVER_PARROT_DRAGON_BASIC_USER_DATA_SEI_V2_UUID_0) && (uuid1 == BEAVER_PARROT_DRAGON_BASIC_USER_DATA_SEI_V2_UUID_1) 
            && (uuid2 == BEAVER_PARROT_DRAGON_BASIC_USER_DATA_SEI_V2_UUID_2) && (uuid3 == BEAVER_PARROT_DRAGON_BASIC_USER_DATA_SEI_V2_UUID_3))
    {
        return BEAVER_PARROT_DRAGON_BASIC_USER_DATA_SEI_V2;
    }
    else if ((uuid0 == BEAVER_PARROT_DRAGON_EXTENDED_USER_DATA_SEI_V2_UUID_0) && (uuid1 == BEAVER_PARROT_DRAGON_EXTENDED_USER_DATA_SEI_V2_UUID_1) 
            && (uuid2 == BEAVER_PARROT_DRAGON_EXTENDED_USER_DATA_SEI_V2_UUID_2) && (uuid3 == BEAVER_PARROT_DRAGON_EXTENDED_USER_DATA_SEI_V2_UUID_3))
    {
        return BEAVER_PARROT_DRAGON_EXTENDED_USER_DATA_SEI_V2;
    }
    else if ((uuid0 == BEAVER_PARROT_DRAGON_BASIC_USER_DATA_SEI_V1_UUID_0) && (uuid1 == BEAVER_PARROT_DRAGON_BASIC_USER_DATA_SEI_V1_UUID_1) 
            && (uuid2 == BEAVER_PARROT_DRAGON_BASIC_USER_DATA_SEI_V1_UUID_2) && (uuid3 == BEAVER_PARROT_DRAGON_BASIC_USER_DATA_SEI_V1_UUID_3))
    {
        return BEAVER_PARROT_DRAGON_BASIC_USER_DATA_SEI_V1;
    }
    else if ((uuid0 == BEAVER_PARROT_DRAGON_EXTENDED_USER_DATA_SEI_V1_UUID_0) && (uuid1 == BEAVER_PARROT_DRAGON_EXTENDED_USER_DATA_SEI_V1_UUID_1) 
            && (uuid2 == BEAVER_PARROT_DRAGON_EXTENDED_USER_DATA_SEI_V1_UUID_2) && (uuid3 == BEAVER_PARROT_DRAGON_EXTENDED_USER_DATA_SEI_V1_UUID_3))
    {
        return BEAVER_PARROT_DRAGON_EXTENDED_USER_DATA_SEI_V1;
    }

    return BEAVER_PARROT_UNKNOWN_USER_DATA_SEI;
}

