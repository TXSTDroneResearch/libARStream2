/**
 * @file beaver_parrot.c
 * @brief Parrot Streaming Library - Parrot H.264 user data SEI definition and serializer
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <arpa/inet.h>

#include <libBeaver/beaver_parrot.h>

#define BEAVER_PARROT_FRAMEINFO_PSNR_MAX 48.130803609


int BEAVER_Parrot_DeserializeUserDataSeiDragonBasicV1(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_UserDataSeiDragonBasicV1_t *userDataSei)
{
    const uint32_t* pdwBuf = (uint32_t*)pBuf;

    if (!pBuf)
    {
        return -1;
    }

    if (bufSize < sizeof(BEAVER_Parrot_UserDataSeiDragonBasicV1_t))
    {
        return -1;
    }

    userDataSei->uuid[0] = ntohl(*(pdwBuf++));
    userDataSei->uuid[1] = ntohl(*(pdwBuf++));
    userDataSei->uuid[2] = ntohl(*(pdwBuf++));
    userDataSei->uuid[3] = ntohl(*(pdwBuf++));
    if ((userDataSei->uuid[0] != BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V1_UUID_0) || (userDataSei->uuid[1] != BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V1_UUID_1) 
            || (userDataSei->uuid[2] != BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V1_UUID_2) || (userDataSei->uuid[3] != BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V1_UUID_3))
    {
        return -1;
    }

    userDataSei->frameIndex = ntohl(*(pdwBuf++));
    userDataSei->acquisitionTsH = ntohl(*(pdwBuf++));
    userDataSei->acquisitionTsL = ntohl(*(pdwBuf++));
    userDataSei->prevMse_fp8 = ntohl(*(pdwBuf++));

    return 0;
}


int BEAVER_Parrot_DeserializeUserDataSeiDragonBasicV2(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_UserDataSeiDragonBasicV2_t *userDataSei)
{
    const uint32_t* pdwBuf = (uint32_t*)pBuf;

    if (!pBuf)
    {
        return -1;
    }

    if (bufSize < sizeof(BEAVER_Parrot_UserDataSeiDragonBasicV2_t))
    {
        return -1;
    }

    userDataSei->uuid[0] = ntohl(*(pdwBuf++));
    userDataSei->uuid[1] = ntohl(*(pdwBuf++));
    userDataSei->uuid[2] = ntohl(*(pdwBuf++));
    userDataSei->uuid[3] = ntohl(*(pdwBuf++));
    if ((userDataSei->uuid[0] != BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V2_UUID_0) || (userDataSei->uuid[1] != BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V2_UUID_1) 
            || (userDataSei->uuid[2] != BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V2_UUID_2) || (userDataSei->uuid[3] != BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V2_UUID_3))
    {
        return -1;
    }

    userDataSei->frameIndex = ntohl(*(pdwBuf++));
    userDataSei->acquisitionTsH = ntohl(*(pdwBuf++));
    userDataSei->acquisitionTsL = ntohl(*(pdwBuf++));

    return 0;
}


int BEAVER_Parrot_DeserializeUserDataSeiDragonExtendedV1(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_UserDataSeiDragonExtendedV1_t *userDataSei)
{
    const uint32_t* pdwBuf = (uint32_t*)pBuf;
    const char* pszBuf;

    if (!pBuf)
    {
        return -1;
    }

    if (bufSize < sizeof(BEAVER_Parrot_UserDataSeiDragonExtendedV1_t))
    {
        return -1;
    }

    userDataSei->uuid[0] = ntohl(*(pdwBuf++));
    userDataSei->uuid[1] = ntohl(*(pdwBuf++));
    userDataSei->uuid[2] = ntohl(*(pdwBuf++));
    userDataSei->uuid[3] = ntohl(*(pdwBuf++));
    if ((userDataSei->uuid[0] != BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V1_UUID_0) || (userDataSei->uuid[1] != BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V1_UUID_1) 
            || (userDataSei->uuid[2] != BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V1_UUID_2) || (userDataSei->uuid[3] != BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V1_UUID_3))
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
    strncpy(userDataSei->serialNumberH, pszBuf, BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH);
    userDataSei->serialNumberH[BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH] = '\0';
    pszBuf += BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH + 1;
    strncpy(userDataSei->serialNumberL, pszBuf, BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH);
    userDataSei->serialNumberL[BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH] = '\0';

    return 0;
}


int BEAVER_Parrot_DeserializeUserDataSeiDragonExtendedV2(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_UserDataSeiDragonExtendedV2_t *userDataSei)
{
    const uint32_t* pdwBuf = (uint32_t*)pBuf;
    const char* pszBuf;

    if (!pBuf)
    {
        return -1;
    }

    if (bufSize < sizeof(BEAVER_Parrot_UserDataSeiDragonExtendedV2_t))
    {
        return -1;
    }

    userDataSei->uuid[0] = ntohl(*(pdwBuf++));
    userDataSei->uuid[1] = ntohl(*(pdwBuf++));
    userDataSei->uuid[2] = ntohl(*(pdwBuf++));
    userDataSei->uuid[3] = ntohl(*(pdwBuf++));
    if ((userDataSei->uuid[0] != BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V2_UUID_0) || (userDataSei->uuid[1] != BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V2_UUID_1) 
            || (userDataSei->uuid[2] != BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V2_UUID_2) || (userDataSei->uuid[3] != BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V2_UUID_3))
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
    strncpy(userDataSei->serialNumberH, pszBuf, BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH);
    userDataSei->serialNumberH[BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH] = '\0';
    pszBuf += BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH + 1;
    strncpy(userDataSei->serialNumberL, pszBuf, BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH);
    userDataSei->serialNumberL[BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH] = '\0';

    return 0;
}


int BEAVER_Parrot_SerializeUserDataSeiDragonFrameInfoV1(const BEAVER_Parrot_DragonFrameInfoV1_t *frameInfo, void* pBuf, unsigned int bufSize, unsigned int *size)
{
    int ret = 0;
    uint8_t* pbBuf = (uint8_t*)pBuf;
    uint32_t* pdwBuf = (uint32_t*)pBuf;
    unsigned int _size = 0, outSize;

    if (!pBuf)
    {
        return -1;
    }

    if (bufSize < sizeof(BEAVER_Parrot_UserDataSeiDragonFrameInfoV1_t))
    {
        return -1;
    }

    *(pdwBuf++) = htonl(BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1_UUID_0);
    _size += 4;
    *(pdwBuf++) = htonl(BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1_UUID_1);
    _size += 4;
    *(pdwBuf++) = htonl(BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1_UUID_2);
    _size += 4;
    *(pdwBuf++) = htonl(BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1_UUID_3);
    _size += 4;

    pbBuf = (uint8_t*)pdwBuf;
    bufSize -= _size;
    ret = BEAVER_Parrot_SerializeDragonFrameInfoV1(frameInfo, (void*)pbBuf, bufSize, &outSize);
    _size += outSize;

    if (size)
    {
        *size = _size;
    }

    return ret;
}


int BEAVER_Parrot_DeserializeUserDataSeiDragonFrameInfoV1(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_DragonFrameInfoV1_t *frameInfo)
{
    int ret = 0;
    const uint8_t* pbBuf = (uint8_t*)pBuf;
    const uint32_t* pdwBuf = (uint32_t*)pBuf;
    uint32_t uuid0, uuid1, uuid2, uuid3;

    if (!pBuf)
    {
        return -1;
    }

    if (bufSize < sizeof(BEAVER_Parrot_UserDataSeiDragonFrameInfoV1_t))
    {
        return -1;
    }

    uuid0 = ntohl(*(pdwBuf++));
    bufSize -=4;
    uuid1 = ntohl(*(pdwBuf++));
    bufSize -=4;
    uuid2 = ntohl(*(pdwBuf++));
    bufSize -=4;
    uuid3 = ntohl(*(pdwBuf++));
    bufSize -=4;
    if ((uuid0 != BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1_UUID_0) || (uuid1 != BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1_UUID_1) 
            || (uuid2 != BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1_UUID_2) || (uuid3 != BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1_UUID_3))
    {
        return -1;
    }

    pbBuf = (uint8_t*)pdwBuf;
    ret = BEAVER_Parrot_DeserializeDragonFrameInfoV1((const void*)pbBuf, bufSize, frameInfo);

    return ret;
}


int BEAVER_Parrot_SerializeUserDataSeiDragonStreamingV1(const BEAVER_Parrot_DragonStreamingV1_t *streaming, const uint16_t *sliceMbCount, void* pBuf, unsigned int bufSize, unsigned int *size)
{
    int ret = 0;
    uint8_t* pbBuf = (uint8_t*)pBuf;
    uint32_t* pdwBuf = (uint32_t*)pBuf;
    unsigned int _size = 0, outSize;

    if (!pBuf)
    {
        return -1;
    }

    if (bufSize < sizeof(BEAVER_Parrot_UserDataSeiDragonStreamingV1_t) + streaming->sliceCount * sizeof(uint16_t))
    {
        return -1;
    }

    *(pdwBuf++) = htonl(BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_V1_UUID_0);
    _size += 4;
    *(pdwBuf++) = htonl(BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_V1_UUID_1);
    _size += 4;
    *(pdwBuf++) = htonl(BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_V1_UUID_2);
    _size += 4;
    *(pdwBuf++) = htonl(BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_V1_UUID_3);
    _size += 4;

    pbBuf = (uint8_t*)pdwBuf;
    bufSize -= _size;
    ret = BEAVER_Parrot_SerializeDragonStreamingV1(streaming, sliceMbCount, (void*)pbBuf, bufSize, &outSize);
    _size += outSize;

    if (size)
    {
        *size = _size;
    }

    return ret;
}


int BEAVER_Parrot_DeserializeUserDataSeiDragonStreamingV1(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_DragonStreamingV1_t *streaming, uint16_t *sliceMbCount)
{
    int ret = 0;
    const uint8_t* pbBuf = (uint8_t*)pBuf;
    const uint32_t* pdwBuf = (uint32_t*)pBuf;
    uint32_t uuid0, uuid1, uuid2, uuid3;

    if (!pBuf)
    {
        return -1;
    }

    if (bufSize < sizeof(BEAVER_Parrot_UserDataSeiDragonStreamingV1_t))
    {
        return -1;
    }

    uuid0 = ntohl(*(pdwBuf++));
    bufSize -=4;
    uuid1 = ntohl(*(pdwBuf++));
    bufSize -=4;
    uuid2 = ntohl(*(pdwBuf++));
    bufSize -=4;
    uuid3 = ntohl(*(pdwBuf++));
    bufSize -=4;
    if ((uuid0 != BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_V1_UUID_0) || (uuid1 != BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_V1_UUID_1) 
            || (uuid2 != BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_V1_UUID_2) || (uuid3 != BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_V1_UUID_3))
    {
        return -1;
    }

    pbBuf = (uint8_t*)pdwBuf;
    ret = BEAVER_Parrot_DeserializeDragonStreamingV1((const void*)pbBuf, bufSize, streaming, sliceMbCount);

    return ret;
}


int BEAVER_Parrot_SerializeUserDataSeiDragonStreamingFrameInfoV1(const BEAVER_Parrot_DragonFrameInfoV1_t *frameInfo, const BEAVER_Parrot_DragonStreamingV1_t *streaming, const uint16_t *sliceMbCount, void* pBuf, unsigned int bufSize, unsigned int *size)
{
    int ret = 0;
    uint8_t* pbBuf = (uint8_t*)pBuf;
    uint32_t* pdwBuf = (uint32_t*)pBuf;
    unsigned int _size = 0, outSize;

    if (!pBuf)
    {
        return -1;
    }

    if (bufSize < sizeof(BEAVER_Parrot_UserDataSeiDragonStreamingFrameInfoV1_t) + streaming->sliceCount * sizeof(uint16_t))
    {
        return -1;
    }

    *(pdwBuf++) = htonl(BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1_UUID_0);
    _size += 4;
    *(pdwBuf++) = htonl(BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1_UUID_1);
    _size += 4;
    *(pdwBuf++) = htonl(BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1_UUID_2);
    _size += 4;
    *(pdwBuf++) = htonl(BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1_UUID_3);
    _size += 4;

    pbBuf = (uint8_t*)pdwBuf;
    bufSize -= _size;
    ret = BEAVER_Parrot_SerializeDragonFrameInfoV1(frameInfo, (void*)pbBuf, bufSize, &outSize);
    _size += outSize;

    pbBuf += outSize;
    bufSize -= outSize;
    ret = BEAVER_Parrot_SerializeDragonStreamingV1(streaming, sliceMbCount, (void*)pbBuf, bufSize, &outSize);
    _size += outSize;

    if (size)
    {
        *size = _size;
    }

    return ret;
}


int BEAVER_Parrot_DeserializeUserDataSeiDragonStreamingFrameInfoV1(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_DragonFrameInfoV1_t *frameInfo, BEAVER_Parrot_DragonStreamingV1_t *streaming, uint16_t *sliceMbCount)
{
    int ret = 0;
    const uint8_t* pbBuf = (uint8_t*)pBuf;
    const uint32_t* pdwBuf = (uint32_t*)pBuf;
    uint32_t uuid0, uuid1, uuid2, uuid3;

    if (!pBuf)
    {
        return -1;
    }

    if (bufSize < sizeof(BEAVER_Parrot_UserDataSeiDragonFrameInfoV1_t))
    {
        return -1;
    }

    uuid0 = ntohl(*(pdwBuf++));
    bufSize -=4;
    uuid1 = ntohl(*(pdwBuf++));
    bufSize -=4;
    uuid2 = ntohl(*(pdwBuf++));
    bufSize -=4;
    uuid3 = ntohl(*(pdwBuf++));
    bufSize -=4;
    if ((uuid0 != BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1_UUID_0) || (uuid1 != BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1_UUID_1) 
            || (uuid2 != BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1_UUID_2) || (uuid3 != BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1_UUID_3))
    {
        return -1;
    }

    pbBuf = (uint8_t*)pdwBuf;
    ret = BEAVER_Parrot_DeserializeDragonFrameInfoV1((const void*)pbBuf, bufSize, frameInfo);
    bufSize -= sizeof(BEAVER_Parrot_DragonFrameInfoV1_t);
    pbBuf += sizeof(BEAVER_Parrot_DragonFrameInfoV1_t);

    if (ret != 0)
    {
        return ret;
    }

    ret = BEAVER_Parrot_DeserializeDragonStreamingV1((const void*)pbBuf, bufSize, streaming, sliceMbCount);

    return ret;
}


int BEAVER_Parrot_SerializeDragonFrameInfoV1(const BEAVER_Parrot_DragonFrameInfoV1_t *frameInfo, void* pBuf, unsigned int bufSize, unsigned int *size)
{
    uint32_t* pdwBuf = (uint32_t*)pBuf;
    char* pszBuf;
    unsigned int _size;

    if (!pBuf)
    {
        return -1;
    }

    _size = sizeof(BEAVER_Parrot_DragonFrameInfoV1_t);
    if (bufSize < _size)
    {
        return -1;
    }

    *(pdwBuf++) = htonl(frameInfo->frameIndex);
    *(pdwBuf++) = htonl(frameInfo->acquisitionTsH);
    *(pdwBuf++) = htonl(frameInfo->acquisitionTsL);
    *(pdwBuf++) = htonl(frameInfo->systemTsH);
    *(pdwBuf++) = htonl(frameInfo->systemTsL);
    *(pdwBuf++) = htonl(frameInfo->batteryPercentage);
    *(pdwBuf++) = htonl((uint32_t)frameInfo->gpsLatitude_fp20);
    *(pdwBuf++) = htonl((uint32_t)frameInfo->gpsLongitude_fp20);
    *(pdwBuf++) = htonl((uint32_t)frameInfo->gpsAltitude_fp16);
    *(pdwBuf++) = htonl((uint32_t)frameInfo->absoluteHeight_fp16);
    *(pdwBuf++) = htonl((uint32_t)frameInfo->relativeHeight_fp16);
    *(pdwBuf++) = htonl((uint32_t)frameInfo->xSpeed_fp16);
    *(pdwBuf++) = htonl((uint32_t)frameInfo->ySpeed_fp16);
    *(pdwBuf++) = htonl((uint32_t)frameInfo->zSpeed_fp16);
    *(pdwBuf++) = htonl(frameInfo->distanceFromHome_fp16);
    *(pdwBuf++) = htonl((uint32_t)frameInfo->yaw_fp16);
    *(pdwBuf++) = htonl((uint32_t)frameInfo->pitch_fp16);
    *(pdwBuf++) = htonl((uint32_t)frameInfo->roll_fp16);
    *(pdwBuf++) = htonl((uint32_t)frameInfo->cameraPan_fp16);
    *(pdwBuf++) = htonl((uint32_t)frameInfo->cameraTilt_fp16);
    *(pdwBuf++) = htonl((uint32_t)frameInfo->wifiRssi);
    *(pdwBuf++) = htonl(frameInfo->wifiMcsRate);
    *(pdwBuf++) = htonl(frameInfo->wifiTxRate);
    *(pdwBuf++) = htonl(frameInfo->wifiRxRate);
    *(pdwBuf++) = htonl(frameInfo->wifiTxFailRate);
    *(pdwBuf++) = htonl(frameInfo->wifiTxErrorRate);
    *(pdwBuf++) = htonl(frameInfo->wifiTxFailEventCount);
    *(pdwBuf++) = htonl(frameInfo->videoStreamingTargetBitrate);
    *(pdwBuf++) = htonl(frameInfo->videoStreamingDecimation);
    *(pdwBuf++) = htonl(frameInfo->videoStreamingGopLength);
    *(pdwBuf++) = htonl((uint32_t)frameInfo->videoStreamingPrevFrameType);
    *(pdwBuf++) = htonl(frameInfo->videoStreamingPrevFrameSize);
    *(pdwBuf++) = htonl(frameInfo->videoStreamingPrevFrameMseY_fp8);
    *(pdwBuf++) = htonl((uint32_t)frameInfo->videoRecordingPrevFrameType);
    *(pdwBuf++) = htonl(frameInfo->videoRecordingPrevFrameSize);
    *(pdwBuf++) = htonl(frameInfo->videoRecordingPrevFrameMseY_fp8);
    *(pdwBuf++) = htonl(frameInfo->streamingMonitorTimeInterval);
    *(pdwBuf++) = htonl(frameInfo->streamingMeanAcqToNetworkTime);
    *(pdwBuf++) = htonl(frameInfo->streamingAcqToNetworkJitter);
    *(pdwBuf++) = htonl(frameInfo->streamingMeanNetworkTime);
    *(pdwBuf++) = htonl(frameInfo->streamingNetworkJitter);
    *(pdwBuf++) = htonl(frameInfo->streamingBytesSent);
    *(pdwBuf++) = htonl(frameInfo->streamingMeanPacketSize);
    *(pdwBuf++) = htonl(frameInfo->streamingPacketSizeStdDev);
    *(pdwBuf++) = htonl(frameInfo->streamingPacketsSent);
    *(pdwBuf++) = htonl(frameInfo->streamingBytesDropped);
    *(pdwBuf++) = htonl(frameInfo->streamingNaluDropped);
    *(pdwBuf++) = htonl(frameInfo->commandsMaxTimeDeltaOnLastSec);
    *(pdwBuf++) = htonl(frameInfo->lastCommandTimeDelta);
    *(pdwBuf++) = htonl(frameInfo->lastCommandPsiValue);
    *(pdwBuf++) = htonl(frameInfo->preReprojTimestampDelta);
    *(pdwBuf++) = htonl(frameInfo->postReprojTimestampDelta);
    *(pdwBuf++) = htonl(frameInfo->postEeTimestampDelta);
    *(pdwBuf++) = htonl(frameInfo->postScalingTimestampDelta);
    *(pdwBuf++) = htonl(frameInfo->postStreamingEncodingTimestampDelta);
    *(pdwBuf++) = htonl(frameInfo->postRecordingEncodingTimestampDelta);
    *(pdwBuf++) = htonl(frameInfo->postNetworkInputTimestampDelta);

    pszBuf = (char*)pdwBuf;
    strncpy(pszBuf, frameInfo->serialNumberH, BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH);
    pszBuf[BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH] = '\0';
    pszBuf += BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH + 1;
    strncpy(pszBuf, frameInfo->serialNumberL, BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH);
    pszBuf[BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH] = '\0';

    if (size)
    {
        *size = _size;
    }

    return 0;
}


int BEAVER_Parrot_DeserializeDragonFrameInfoV1(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_DragonFrameInfoV1_t *frameInfo)
{
    const uint32_t* pdwBuf = (const uint32_t*)pBuf;
    const char* pszBuf;

    if (!pBuf)
    {
        return -1;
    }

    if (bufSize < sizeof(BEAVER_Parrot_DragonFrameInfoV1_t))
    {
        return -1;
    }

    frameInfo->frameIndex = ntohl(*(pdwBuf++));
    frameInfo->acquisitionTsH = ntohl(*(pdwBuf++));
    frameInfo->acquisitionTsL = ntohl(*(pdwBuf++));
    frameInfo->systemTsH = ntohl(*(pdwBuf++));
    frameInfo->systemTsL = ntohl(*(pdwBuf++));
    frameInfo->batteryPercentage = ntohl(*(pdwBuf++));
    frameInfo->gpsLatitude_fp20 = (int32_t)ntohl(*(pdwBuf++));
    frameInfo->gpsLongitude_fp20 = (int32_t)ntohl(*(pdwBuf++));
    frameInfo->gpsAltitude_fp16 = (int32_t)ntohl(*(pdwBuf++));
    frameInfo->absoluteHeight_fp16 = (int32_t)ntohl(*(pdwBuf++));
    frameInfo->relativeHeight_fp16 = (int32_t)ntohl(*(pdwBuf++));
    frameInfo->xSpeed_fp16 = (int32_t)ntohl(*(pdwBuf++));
    frameInfo->ySpeed_fp16 = (int32_t)ntohl(*(pdwBuf++));
    frameInfo->zSpeed_fp16 = (int32_t)ntohl(*(pdwBuf++));
    frameInfo->distanceFromHome_fp16 = ntohl(*(pdwBuf++));
    frameInfo->yaw_fp16 = (int32_t)ntohl(*(pdwBuf++));
    frameInfo->pitch_fp16 = (int32_t)ntohl(*(pdwBuf++));
    frameInfo->roll_fp16 = (int32_t)ntohl(*(pdwBuf++));
    frameInfo->cameraPan_fp16 = (int32_t)ntohl(*(pdwBuf++));
    frameInfo->cameraTilt_fp16 = (int32_t)ntohl(*(pdwBuf++));
    frameInfo->wifiRssi = (int32_t)ntohl(*(pdwBuf++));
    frameInfo->wifiMcsRate = ntohl(*(pdwBuf++));
    frameInfo->wifiTxRate = ntohl(*(pdwBuf++));
    frameInfo->wifiRxRate = ntohl(*(pdwBuf++));
    frameInfo->wifiTxFailRate = ntohl(*(pdwBuf++));
    frameInfo->wifiTxErrorRate = ntohl(*(pdwBuf++));
    frameInfo->wifiTxFailEventCount = ntohl(*(pdwBuf++));
    frameInfo->videoStreamingTargetBitrate = ntohl(*(pdwBuf++));
    frameInfo->videoStreamingDecimation = ntohl(*(pdwBuf++));
    frameInfo->videoStreamingGopLength = ntohl(*(pdwBuf++));
    frameInfo->videoStreamingPrevFrameType = (int32_t)ntohl(*(pdwBuf++));
    frameInfo->videoStreamingPrevFrameSize = ntohl(*(pdwBuf++));
    frameInfo->videoStreamingPrevFrameMseY_fp8 = ntohl(*(pdwBuf++));
    frameInfo->videoRecordingPrevFrameType = (int32_t)ntohl(*(pdwBuf++));
    frameInfo->videoRecordingPrevFrameSize = ntohl(*(pdwBuf++));
    frameInfo->videoRecordingPrevFrameMseY_fp8 = ntohl(*(pdwBuf++));
    frameInfo->streamingMonitorTimeInterval = ntohl(*(pdwBuf++));
    frameInfo->streamingMeanAcqToNetworkTime = ntohl(*(pdwBuf++));
    frameInfo->streamingAcqToNetworkJitter = ntohl(*(pdwBuf++));
    frameInfo->streamingMeanNetworkTime = ntohl(*(pdwBuf++));
    frameInfo->streamingNetworkJitter = ntohl(*(pdwBuf++));
    frameInfo->streamingBytesSent = ntohl(*(pdwBuf++));
    frameInfo->streamingMeanPacketSize = ntohl(*(pdwBuf++));
    frameInfo->streamingPacketSizeStdDev = ntohl(*(pdwBuf++));
    frameInfo->streamingPacketsSent = ntohl(*(pdwBuf++));
    frameInfo->streamingBytesDropped = ntohl(*(pdwBuf++));
    frameInfo->streamingNaluDropped = ntohl(*(pdwBuf++));
    frameInfo->commandsMaxTimeDeltaOnLastSec = ntohl(*(pdwBuf++));
    frameInfo->lastCommandTimeDelta = ntohl(*(pdwBuf++));
    frameInfo->lastCommandPsiValue = ntohl(*(pdwBuf++));
    frameInfo->preReprojTimestampDelta = ntohl(*(pdwBuf++));
    frameInfo->postReprojTimestampDelta = ntohl(*(pdwBuf++));
    frameInfo->postEeTimestampDelta = ntohl(*(pdwBuf++));
    frameInfo->postScalingTimestampDelta = ntohl(*(pdwBuf++));
    frameInfo->postStreamingEncodingTimestampDelta = ntohl(*(pdwBuf++));
    frameInfo->postRecordingEncodingTimestampDelta = ntohl(*(pdwBuf++));
    frameInfo->postNetworkInputTimestampDelta = ntohl(*(pdwBuf++));

    pszBuf = (const char*)pdwBuf;
    strncpy(frameInfo->serialNumberH, pszBuf, BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH);
    frameInfo->serialNumberH[BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH] = '\0';
    pszBuf += BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH + 1;
    strncpy(frameInfo->serialNumberL, pszBuf, BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH);
    frameInfo->serialNumberL[BEAVER_PARROT_DRAGON_SERIAL_NUMBER_PART_LENGTH] = '\0';

    return 0;
}


int BEAVER_Parrot_SerializeDragonStreamingV1(const BEAVER_Parrot_DragonStreamingV1_t *streaming, const uint16_t *sliceMbCount, void* pBuf, unsigned int bufSize, unsigned int *size)
{
    uint8_t* pbBuf = (uint8_t*)pBuf;
    uint16_t* pwBuf;
    int i;
    unsigned int _size;

    if (!pBuf)
    {
        return -1;
    }

    _size = sizeof(BEAVER_Parrot_DragonStreamingV1_t) + streaming->sliceCount * sizeof(uint16_t);
    if (bufSize < _size)
    {
        return -1;
    }

    *(pbBuf++) = streaming->indexInGop;
    *(pbBuf++) = streaming->sliceCount;

    pwBuf = (uint16_t*)pbBuf;
    for (i = 0; i < streaming->sliceCount; i++)
    {
        *(pwBuf++) = htons(sliceMbCount[i]);
    }

    if (size)
    {
        *size = _size;
    }

    return 0;
}


int BEAVER_Parrot_DeserializeDragonStreamingV1(const void* pBuf, unsigned int bufSize, BEAVER_Parrot_DragonStreamingV1_t *streaming, uint16_t *sliceMbCount)
{
    const uint8_t* pbBuf = (const uint8_t*)pBuf;
    const uint16_t* pwBuf;
    int i;

    if (!pBuf)
    {
        return -1;
    }

    if (bufSize < sizeof(BEAVER_Parrot_DragonStreamingV1_t))
    {
        return -1;
    }

    streaming->indexInGop = *(pbBuf++);
    streaming->sliceCount = *(pbBuf++);

    if (streaming->sliceCount > BEAVER_PARROT_DRAGON_MAX_SLICE_COUNT)
    {
        return -1;
    }
    if (bufSize < sizeof(BEAVER_Parrot_DragonStreamingV1_t) + streaming->sliceCount * sizeof(uint16_t))
    {
        return -1;
    }

    pwBuf = (const uint16_t*)pbBuf;
    for (i = 0; i < streaming->sliceCount; i++)
    {
        sliceMbCount[i] = ntohs(*(pwBuf++));
    }

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

    if ((uuid0 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_V1_UUID_0) && (uuid1 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_V1_UUID_1) 
            && (uuid2 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_V1_UUID_2) && (uuid3 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_V1_UUID_3))
    {
        return BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_V1;
    }
    else if ((uuid0 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1_UUID_0) && (uuid1 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1_UUID_1) 
            && (uuid2 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1_UUID_2) && (uuid3 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1_UUID_3))
    {
        return BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1;
    }
    else if ((uuid0 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1_UUID_0) && (uuid1 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1_UUID_1) 
            && (uuid2 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1_UUID_2) && (uuid3 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1_UUID_3))
    {
        return BEAVER_PARROT_USER_DATA_SEI_DRAGON_FRAMEINFO_V1;
    }
    else if ((uuid0 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V2_UUID_0) && (uuid1 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V2_UUID_1) 
            && (uuid2 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V2_UUID_2) && (uuid3 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V2_UUID_3))
    {
        return BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V2;
    }
    else if ((uuid0 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V2_UUID_0) && (uuid1 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V2_UUID_1) 
            && (uuid2 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V2_UUID_2) && (uuid3 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V2_UUID_3))
    {
        return BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V2;
    }
    else if ((uuid0 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V1_UUID_0) && (uuid1 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V1_UUID_1) 
            && (uuid2 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V1_UUID_2) && (uuid3 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V1_UUID_3))
    {
        return BEAVER_PARROT_USER_DATA_SEI_DRAGON_BASIC_V1;
    }
    else if ((uuid0 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V1_UUID_0) && (uuid1 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V1_UUID_1) 
            && (uuid2 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V1_UUID_2) && (uuid3 == BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V1_UUID_3))
    {
        return BEAVER_PARROT_USER_DATA_SEI_DRAGON_EXTENDED_V1;
    }

    return BEAVER_PARROT_USER_DATA_SEI_UNKNOWN;
}


int BEAVER_Parrot_WriteDragonFrameInfoV1HeaderToFile(FILE *frameInfoFile)
{
    if (!frameInfoFile)
    {
        return -1;
    }

    fprintf(frameInfoFile, "frameIndex acquisitionTs systemTs ");
    fprintf(frameInfoFile, "batteryPercentage gpsLatitude gpsLongitude gpsAltitude absoluteHeight relativeHeight xSpeed ySpeed zSpeed distanceFromHome yaw pitch roll cameraPan cameraTilt ");
    fprintf(frameInfoFile, "videoStreamingTargetBitrate videoStreamingDecimation videoStreamingGopLength videoStreamingPrevFrameType videoStreamingPrevFrameSize videoStreamingPrevFramePsnrY ");
    fprintf(frameInfoFile, "videoRecordingPrevFrameType videoRecordingPrevFrameSize videoRecordingPrevFramePsnrY ");
    fprintf(frameInfoFile, "wifiRssi wifiMcsRate wifiTxRate wifiRxRate wifiTxFailRate wifiTxErrorRate wifiTxFailEventCount ");
    fprintf(frameInfoFile, "preReprojTimestampDelta postReprojTimestampDelta postEeTimestampDelta postScalingTimestampDelta ");
    fprintf(frameInfoFile, "postStreamingEncodingTimestampDelta postRecordingEncodingTimestampDelta postNetworkInputTimestampDelta ");
    fprintf(frameInfoFile, "streamingSrcMonitorTimeInterval streamingSrcMeanAcqToNetworkTime streamingSrcAcqToNetworkJitter streamingSrcMeanNetworkTime streamingSrcNetworkJitter ");
    fprintf(frameInfoFile, "streamingSrcBytesSent streamingSrcMeanPacketSize streamingSrcPacketSizeStdDev streamingSrcPacketsSent streamingSrcBytesDropped streamingSrcNaluDropped ");
    fprintf(frameInfoFile, "commandsMaxTimeDeltaOnLastSec lastCommandTimeDelta lastCommandPsiValue");

    return 0;
}


int BEAVER_Parrot_WriteDragonFrameInfoV1ToFile(const BEAVER_Parrot_DragonFrameInfoV1_t* frameInfo, FILE *frameInfoFile)
{
    if ((!frameInfo) || (!frameInfoFile))
    {
        return -1;
    }

    fprintf(frameInfoFile, "%lu ", (long unsigned int)frameInfo->frameIndex);
    fprintf(frameInfoFile, "%llu ", (long long unsigned int)(((uint64_t)frameInfo->acquisitionTsH << 32) + (uint64_t)frameInfo->acquisitionTsL));
    fprintf(frameInfoFile, "%llu ", (long long unsigned int)(((uint64_t)frameInfo->systemTsH << 32) + (uint64_t)frameInfo->systemTsL));
    fprintf(frameInfoFile, "%lu %.8f %.8f %.3f ",
            (long unsigned int)frameInfo->batteryPercentage,
            (double)frameInfo->gpsLatitude_fp20 / 1048576.,
            (double)frameInfo->gpsLongitude_fp20 / 1048576.,
            (double)frameInfo->gpsAltitude_fp16 / 65536.);
    fprintf(frameInfoFile, "%.3f %.3f %.3f %.3f %.3f %.3f ",
            (float)frameInfo->absoluteHeight_fp16 / 65536.,
            (float)frameInfo->relativeHeight_fp16 / 65536.,
            (float)frameInfo->xSpeed_fp16 / 65536.,
            (float)frameInfo->ySpeed_fp16 / 65536.,
            (float)frameInfo->zSpeed_fp16 / 65536.,
            (float)frameInfo->distanceFromHome_fp16 / 65536.);
    fprintf(frameInfoFile, "%.3f %.3f %.3f %.3f %.3f ",
            (float)frameInfo->yaw_fp16 / 65536.,
            (float)frameInfo->pitch_fp16 / 65536.,
            (float)frameInfo->roll_fp16 / 65536.,
            (float)frameInfo->cameraPan_fp16 / 65536.,
            (float)frameInfo->cameraTilt_fp16 / 65536.);
    fprintf(frameInfoFile, "%d %d %d %d %d ",
            frameInfo->videoStreamingTargetBitrate,
            frameInfo->videoStreamingDecimation,
            frameInfo->videoStreamingGopLength,
            frameInfo->videoStreamingPrevFrameType,
            frameInfo->videoStreamingPrevFrameSize);
    if (frameInfo->videoStreamingPrevFrameMseY_fp8)
    {
        fprintf(frameInfoFile, "%.3f ", BEAVER_PARROT_FRAMEINFO_PSNR_MAX - 10. * log10((double)frameInfo->videoStreamingPrevFrameMseY_fp8 / 256.));
    }
    else
    {
        fprintf(frameInfoFile, "%.3f ", 0.);
    }
    fprintf(frameInfoFile, "%d %d ",
            frameInfo->videoRecordingPrevFrameType,
            frameInfo->videoRecordingPrevFrameSize);
    if (frameInfo->videoRecordingPrevFrameMseY_fp8)
    {
        fprintf(frameInfoFile, "%.3f ", BEAVER_PARROT_FRAMEINFO_PSNR_MAX - 10. * log10((double)frameInfo->videoRecordingPrevFrameMseY_fp8 / 256.));
    }
    else
    {
        fprintf(frameInfoFile, "%.3f ", 0.);
    }
    fprintf(frameInfoFile, "%d %d %d %d %d %d %d ",
            frameInfo->wifiRssi,
            frameInfo->wifiMcsRate,
            frameInfo->wifiTxRate,
            frameInfo->wifiRxRate,
            frameInfo->wifiTxFailRate,
            frameInfo->wifiTxErrorRate,
            frameInfo->wifiTxFailEventCount);
    fprintf(frameInfoFile, "%lu %lu %lu %lu %lu %lu %lu ",
            (long unsigned int)frameInfo->preReprojTimestampDelta,
            (long unsigned int)frameInfo->postReprojTimestampDelta,
            (long unsigned int)frameInfo->postEeTimestampDelta,
            (long unsigned int)frameInfo->postScalingTimestampDelta,
            (long unsigned int)frameInfo->postStreamingEncodingTimestampDelta,
            (long unsigned int)frameInfo->postRecordingEncodingTimestampDelta,
            (long unsigned int)frameInfo->postNetworkInputTimestampDelta);
    fprintf(frameInfoFile, "%lu ",
            (long unsigned int)frameInfo->streamingMonitorTimeInterval);
    if (frameInfo->streamingMonitorTimeInterval)
    {
        fprintf(frameInfoFile, "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu ",
                (long unsigned int)(((float)frameInfo->streamingMeanAcqToNetworkTime / (float)frameInfo->streamingMonitorTimeInterval) * 1000000.),
                (long unsigned int)(((float)frameInfo->streamingAcqToNetworkJitter / (float)frameInfo->streamingMonitorTimeInterval) * 1000000.),
                (long unsigned int)(((float)frameInfo->streamingMeanNetworkTime / (float)frameInfo->streamingMonitorTimeInterval) * 1000000.),
                (long unsigned int)(((float)frameInfo->streamingNetworkJitter / (float)frameInfo->streamingMonitorTimeInterval) * 1000000.),
                (long unsigned int)(((float)frameInfo->streamingBytesSent / (float)frameInfo->streamingMonitorTimeInterval) * 1000000.),
                (long unsigned int)(((float)frameInfo->streamingMeanPacketSize / (float)frameInfo->streamingMonitorTimeInterval) * 1000000.),
                (long unsigned int)(((float)frameInfo->streamingPacketSizeStdDev / (float)frameInfo->streamingMonitorTimeInterval) * 1000000.),
                (long unsigned int)(((float)frameInfo->streamingPacketsSent / (float)frameInfo->streamingMonitorTimeInterval) * 1000000.),
                (long unsigned int)(((float)frameInfo->streamingBytesDropped / (float)frameInfo->streamingMonitorTimeInterval) * 1000000.),
                (long unsigned int)(((float)frameInfo->streamingNaluDropped / (float)frameInfo->streamingMonitorTimeInterval) * 1000000.));
    }
    else
    {
        fprintf(frameInfoFile, "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu ",
                (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0, (long unsigned int)0);
    }
    fprintf(frameInfoFile, "%lu %lu %lu", 
            (long unsigned int)frameInfo->commandsMaxTimeDeltaOnLastSec,
            (long unsigned int)frameInfo->lastCommandTimeDelta,
            (long unsigned int)frameInfo->lastCommandPsiValue);

    return 0;
}

