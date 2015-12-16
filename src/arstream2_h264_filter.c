/**
 * @file arstream2_h264_filter.c
 * @brief Parrot Reception Library - H.264 Filter
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include <libARSAL/ARSAL_Print.h>
#include <libARSAL/ARSAL_Mutex.h>

#include <libARStream2/arstream2_rtp_receiver.h>
#include <libARStream2/arstream2_h264_filter.h>
#include <libARStream2/arstream2_h264_parser.h>
#include <libARStream2/arstream2_h264_writer.h>
#include <libARStream2/arstream2_h264_sei.h>

#include "arstream2_h264.h"


#define ARSTREAM2_H264_FILTER_TAG "ARSTREAM2_H264Filter"

#define ARSTREAM2_H264_FILTER_TEMP_AU_BUFFER_SIZE (1024 * 1024)
#define ARSTREAM2_H264_FILTER_TEMP_SLICE_NALU_BUFFER_SIZE (64 * 1024)
#define ARSTREAM2_H264_FILTER_USER_DATA_BUFFER_SIZE (1024)

#define ARSTREAM2_H264_FILTER_STREAM_OUTPUT
#ifdef ARSTREAM2_H264_FILTER_STREAM_OUTPUT
    #include <stdio.h>

    #define ARSTREAM2_H264_FILTER_STREAM_OUTPUT_ALLOW_NAP_USB
    #define ARSTREAM2_H264_FILTER_STREAM_OUTPUT_PATH_NAP_USB "/tmp/mnt/STREAMDEBUG/stream"
    //#define ARSTREAM2_H264_FILTER_STREAM_OUTPUT_ALLOW_NAP_INTERNAL
    #define ARSTREAM2_H264_FILTER_STREAM_OUTPUT_PATH_NAP_INTERNAL "/data/skycontroller/stream"
    #define ARSTREAM2_H264_FILTER_STREAM_OUTPUT_ALLOW_ANDROID_INTERNAL
    #define ARSTREAM2_H264_FILTER_STREAM_OUTPUT_PATH_ANDROID_INTERNAL "/sdcard/FF/stream"
    #define ARSTREAM2_H264_FILTER_STREAM_OUTPUT_ALLOW_PCLINUX
    #define ARSTREAM2_H264_FILTER_STREAM_OUTPUT_PATH_PCLINUX "./stream"

    #define ARSTREAM2_H264_FILTER_STREAM_OUTPUT_FILENAME "stream"
#endif


typedef enum
{
    ARSTREAM2_H264_FILTER_H264_NALU_TYPE_UNKNOWN = 0,
    ARSTREAM2_H264_FILTER_H264_NALU_TYPE_SLICE = 1,
    ARSTREAM2_H264_FILTER_H264_NALU_TYPE_SLICE_IDR = 5,
    ARSTREAM2_H264_FILTER_H264_NALU_TYPE_SEI = 6,
    ARSTREAM2_H264_FILTER_H264_NALU_TYPE_SPS = 7,
    ARSTREAM2_H264_FILTER_H264_NALU_TYPE_PPS = 8,
    ARSTREAM2_H264_FILTER_H264_NALU_TYPE_AUD = 9,
    ARSTREAM2_H264_FILTER_H264_NALU_TYPE_FILLER_DATA = 12,

} ARSTREAM2_H264Filter_H264NaluType_t;


typedef enum
{
    ARSTREAM2_H264_FILTER_H264_SLICE_TYPE_NON_VCL = 0,
    ARSTREAM2_H264_FILTER_H264_SLICE_TYPE_I,
    ARSTREAM2_H264_FILTER_H264_SLICE_TYPE_P,

} ARSTREAM2_H264Filter_H264SliceType_t;


typedef struct ARSTREAM2_H264Filter_s
{
    ARSTREAM2_H264Filter_SpsPpsCallback_t spsPpsCallback;
    void* spsPpsCallbackUserPtr;
    ARSTREAM2_H264Filter_GetAuBufferCallback_t getAuBufferCallback;
    void* getAuBufferCallbackUserPtr;
    ARSTREAM2_H264Filter_AuReadyCallback_t auReadyCallback;
    void* auReadyCallbackUserPtr;
    int callbackInProgress;

    int waitForSync;
    int outputIncompleteAu;
    int filterOutSpsPps;
    int filterOutSei;
    int replaceStartCodesWithNaluSize;
    int generateSkippedPSlices;
    int generateFirstGrayIFrame;

    uint8_t *currentAuBuffer;
    int currentAuBufferSize;
    void *currentAuBufferUserPtr;
    uint8_t *currentNaluBuffer;
    int currentNaluBufferSize;
    uint8_t *tempAuBuffer;
    int tempAuBufferSize;

    int currentAuSize;
    uint64_t currentAuTimestamp;
    uint64_t currentAuTimestampShifted;
    uint64_t currentAuFirstNaluInputTime;
    int currentAuIncomplete;
    eARSTREAM2_H264_FILTER_AU_SYNC_TYPE currentAuSyncType;
    int currentAuSlicesReceived;
    int currentAuSlicesAllI;
    int currentAuStreamingInfoAvailable;
    int currentAuUserDataSize;
    uint8_t *currentAuUserData;
    ARSTREAM2_H264Sei_ParrotStreamingV1_t currentAuStreamingInfo;
    uint16_t currentAuStreamingSliceMbCount[ARSTREAM2_H264_SEI_PARROT_STREAMING_MAX_SLICE_COUNT];
    int currentAuPreviousSliceIndex;
    int currentAuPreviousSliceFirstMb;
    int currentAuCurrentSliceFirstMb;
    ARSTREAM2_H264Filter_H264SliceType_t previousSliceType;

    ARSTREAM2_H264Parser_Handle parser;
    ARSTREAM2_H264Writer_Handle writer;
    uint8_t *tempSliceNaluBuffer;
    int tempSliceNaluBufferSize;

    int sync;
    int spsSync;
    int spsSize;
    uint8_t* pSps;
    int ppsSync;
    int ppsSize;
    uint8_t* pPps;
    int firstGrayIFramePending;
    int mbWidth;
    int mbHeight;

    ARSAL_Mutex_t mutex;
    ARSAL_Cond_t startCond;
    ARSAL_Cond_t callbackCond;
    int auBufferChangePending;
    int running;
    int threadShouldStop;
    int threadStarted;

#ifdef ARSTREAM2_H264_FILTER_STREAM_OUTPUT
    FILE* fStreamOut;
#endif

} ARSTREAM2_H264Filter_t;


static int ARSTREAM2_H264Filter_sync(ARSTREAM2_H264Filter_t *filter, uint8_t *naluBuffer, int naluSize)
{
    int ret = 0;
    eARSTREAM2_ERROR err = ARSTREAM2_OK;
    ARSTREAM2_H264_SpsContext_t *spsContext = NULL;
    ARSTREAM2_H264_PpsContext_t *ppsContext = NULL;

    filter->sync = 1;

    if (filter->generateFirstGrayIFrame)
    {
        filter->firstGrayIFramePending = 1;
    }
    ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "SPS/PPS sync OK"); //TODO: debug

    /* SPS/PPS callback */
    if (filter->spsPpsCallback)
    {
        filter->callbackInProgress = 1;
        ARSAL_Mutex_Unlock(&(filter->mutex));
        eARSTREAM2_ERROR cbRet = filter->spsPpsCallback(filter->pSps, filter->spsSize, filter->pPps, filter->ppsSize, filter->spsPpsCallbackUserPtr);
        ARSAL_Mutex_Lock(&(filter->mutex));
        if (cbRet != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "spsPpsCallback failed: %s", ARSTREAM2_Error_ToString(cbRet));
        }
#ifdef ARSTREAM2_H264_FILTER_STREAM_OUTPUT
        if (filter->fStreamOut)
        {
            fwrite(filter->pSps, filter->spsSize, 1, filter->fStreamOut);
            fwrite(filter->pPps, filter->ppsSize, 1, filter->fStreamOut);
        }
#endif
    }

    /* Configure the writer */
    err = ARSTREAM2_H264Parser_GetSpsPpsContext(filter->parser, (void**)&spsContext, (void**)&ppsContext);
    if (err != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Parser_GetSpsPpsContext() failed (%d)", err);
        ret = -1;
    }
    if (ret == 0)
    {
        filter->mbWidth = spsContext->pic_width_in_mbs_minus1 + 1;
        filter->mbHeight = (spsContext->pic_height_in_map_units_minus1 + 1) * ((spsContext->frame_mbs_only_flag) ? 1 : 2);
        err = ARSTREAM2_H264Writer_SetSpsPpsContext(filter->writer, (void*)spsContext, (void*)ppsContext);
        if (err != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Parser_GetSpsPpsContext() failed (%d)", err);
            ret = -1;
        }
    }

    return ret;
}


static int ARSTREAM2_H264Filter_processNalu(ARSTREAM2_H264Filter_t *filter, uint8_t *naluBuffer, int naluSize, ARSTREAM2_H264Filter_H264NaluType_t *naluType, ARSTREAM2_H264Filter_H264SliceType_t *sliceType)
{
    int ret = 0;
    eARSTREAM2_ERROR err = ARSTREAM2_OK, _err = ARSTREAM2_OK;
    ARSTREAM2_H264Filter_H264SliceType_t _sliceType = ARSTREAM2_H264_FILTER_H264_SLICE_TYPE_NON_VCL;

    if ((!naluBuffer) || (naluSize <= 4))
    {
        return -1;
    }

    if (filter->replaceStartCodesWithNaluSize)
    {
        // Replace the NAL unit 4 bytes start code with the NALU size
        *((uint32_t*)naluBuffer) = htonl((uint32_t)naluSize - 4);
    }

    err = ARSTREAM2_H264Parser_SetupNalu_buffer(filter->parser, naluBuffer + 4, naluSize - 4);
    if (err != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Parser_SetupNalu_buffer() failed (%d)", err);
    }

    if (err == ARSTREAM2_OK)
    {
        err = ARSTREAM2_H264Parser_ParseNalu(filter->parser, NULL);
        if (err < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Parser_ParseNalu() failed (%d)", err);
        }
    }

    if (err == ARSTREAM2_OK)
    {
        ARSTREAM2_H264Filter_H264NaluType_t _naluType = ARSTREAM2_H264Parser_GetLastNaluType(filter->parser);
        if (naluType) *naluType = _naluType;
        switch (_naluType)
        {
            case ARSTREAM2_H264_FILTER_H264_NALU_TYPE_SLICE_IDR:
                filter->currentAuSyncType = ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_IDR;
            case ARSTREAM2_H264_FILTER_H264_NALU_TYPE_SLICE:
                /* Slice */
                filter->currentAuCurrentSliceFirstMb = -1;
                if (filter->sync)
                {
                    ARSTREAM2_H264Parser_SliceInfo_t sliceInfo;
                    memset(&sliceInfo, 0, sizeof(sliceInfo));
                    _err = ARSTREAM2_H264Parser_GetSliceInfo(filter->parser, &sliceInfo);
                    if (_err != ARSTREAM2_OK)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Parser_GetSliceInfo() failed (%d)", _err);
                    }
                    else
                    {
                        if (sliceInfo.sliceTypeMod5 == 2)
                        {
                            _sliceType = ARSTREAM2_H264_FILTER_H264_SLICE_TYPE_I;
                        }
                        else if (sliceInfo.sliceTypeMod5 == 0)
                        {
                            _sliceType = ARSTREAM2_H264_FILTER_H264_SLICE_TYPE_P;
                            filter->currentAuSlicesAllI = 0;
                        }
                        if (sliceType) *sliceType = _sliceType;
                        filter->currentAuCurrentSliceFirstMb = sliceInfo.first_mb_in_slice;
                    }
                }
                break;
            case ARSTREAM2_H264_FILTER_H264_NALU_TYPE_SEI:
                /* SEI */
                if (filter->sync)
                {
                    int i, count;
                    void *pUserDataSei = NULL;
                    unsigned int userDataSeiSize = 0;
                    count = ARSTREAM2_H264Parser_GetUserDataSeiCount(filter->parser);
                    for (i = 0; i < count; i++)
                    {
                        _err = ARSTREAM2_H264Parser_GetUserDataSei(filter->parser, (unsigned int)i, &pUserDataSei, &userDataSeiSize);
                        if (_err != ARSTREAM2_OK)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Parser_GetUserDataSei() failed (%d)", _err);
                        }
                        else
                        {
                            if (ARSTREAM2_H264Sei_IsUserDataParrotStreamingV1(pUserDataSei, userDataSeiSize) == 1)
                            {
                                _err = ARSTREAM2_H264Sei_DeserializeUserDataParrotStreamingV1(pUserDataSei, userDataSeiSize, &filter->currentAuStreamingInfo, filter->currentAuStreamingSliceMbCount);
                                if (_err != ARSTREAM2_OK)
                                {
                                    ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Sei_DeserializeUserDataParrotStreamingV1() failed (%d)", _err);
                                }
                                else
                                {
                                    filter->currentAuStreamingInfoAvailable = 1;
                                }
                            }
                            else
                            {
                                //TODO
                            }
                        }
                    }
                }
                break;
            case ARSTREAM2_H264_FILTER_H264_NALU_TYPE_SPS:
                /* SPS */
                if (!filter->spsSync)
                {
                    filter->pSps = malloc(naluSize);
                    if (!filter->pSps)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Allocation failed for SPS (size %d)", naluSize);
                    }
                    else
                    {
                        memcpy(filter->pSps, naluBuffer, naluSize);
                        filter->spsSize = naluSize;
                        filter->spsSync = 1;
                    }
                }
                break;
            case ARSTREAM2_H264_FILTER_H264_NALU_TYPE_PPS:
                /* PPS */
                if (!filter->ppsSync)
                {
                    filter->pPps = malloc(naluSize);
                    if (!filter->pPps)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Allocation failed for PPS (size %d)", naluSize);
                    }
                    else
                    {
                        memcpy(filter->pPps, naluBuffer, naluSize);
                        filter->ppsSize = naluSize;
                        filter->ppsSync = 1;
                    }
                }
                break;
            default:
                break;
        }
    }

    ARSAL_Mutex_Lock(&(filter->mutex));

    if ((filter->running) && (filter->spsSync) && (filter->ppsSync) && (!filter->sync))
    {
        ret = ARSTREAM2_H264Filter_sync(filter, naluBuffer, naluSize);
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_Sync() failed (%d)", ret);
        }
    }

    filter->callbackInProgress = 0;
    ARSAL_Mutex_Unlock(&(filter->mutex));
    ARSAL_Cond_Signal(&(filter->callbackCond));

    return (ret >= 0) ? 0 : ret;
}


static int ARSTREAM2_H264Filter_getNewAuBuffer(ARSTREAM2_H264Filter_t *filter)
{
    int ret = 0;

    filter->currentAuBuffer = filter->tempAuBuffer;
    filter->currentAuBufferSize = filter->tempAuBufferSize;
    filter->auBufferChangePending = 0;

    return ret;
}


static void ARSTREAM2_H264Filter_resetCurrentAu(ARSTREAM2_H264Filter_t *filter)
{
    filter->currentAuSize = 0;
    filter->currentAuIncomplete = 0;
    filter->currentAuSyncType = ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_NONE;
    filter->currentAuSlicesAllI = 1;
    filter->currentAuSlicesReceived = 0;
    filter->currentAuStreamingInfoAvailable = 0;
    memset(&filter->currentAuStreamingInfo, 0, sizeof(ARSTREAM2_H264Sei_ParrotStreamingV1_t));
    filter->currentAuUserDataSize = 0;
    memset(filter->currentAuUserData, 0, sizeof(ARSTREAM2_H264_FILTER_USER_DATA_BUFFER_SIZE));
    filter->currentAuPreviousSliceIndex = -1;
    filter->currentAuPreviousSliceFirstMb = 0;
    filter->currentAuCurrentSliceFirstMb = -1;
    filter->currentAuFirstNaluInputTime = 0;
    filter->previousSliceType = ARSTREAM2_H264_FILTER_H264_SLICE_TYPE_NON_VCL;
}


static void ARSTREAM2_H264Filter_updateCurrentAu(ARSTREAM2_H264Filter_t *filter, ARSTREAM2_H264Filter_H264NaluType_t naluType)
{
    if ((naluType == ARSTREAM2_H264_FILTER_H264_NALU_TYPE_SLICE_IDR) || (naluType == ARSTREAM2_H264_FILTER_H264_NALU_TYPE_SLICE))
    {
        filter->currentAuSlicesReceived = 1;
        if ((filter->currentAuStreamingInfoAvailable) && (filter->currentAuStreamingInfo.sliceCount <= ARSTREAM2_H264_SEI_PARROT_STREAMING_MAX_SLICE_COUNT))
        {
            // Update slice index and firstMb
            if (filter->currentAuPreviousSliceIndex < 0)
            {
                filter->currentAuPreviousSliceFirstMb = 0;
                filter->currentAuPreviousSliceIndex = 0;
            }
            while ((filter->currentAuPreviousSliceIndex < filter->currentAuStreamingInfo.sliceCount) && (filter->currentAuPreviousSliceFirstMb < filter->currentAuCurrentSliceFirstMb))
            {
                filter->currentAuPreviousSliceFirstMb += filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex];
                filter->currentAuPreviousSliceIndex++;
            }
            filter->currentAuPreviousSliceFirstMb = filter->currentAuCurrentSliceFirstMb;
            //ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_H264_FILTER_TAG, "previousSliceIndex: %d - previousSliceFirstMb: %d", filter->currentAuPreviousSliceIndex, filter->currentAuCurrentSliceFirstMb); //TODO: debug
        }
    }
}


static void ARSTREAM2_H264Filter_addNaluToCurrentAu(ARSTREAM2_H264Filter_t *filter, ARSTREAM2_H264Filter_H264NaluType_t naluType, int naluSize)
{
    int filterOut = 0;

    if ((filter->filterOutSpsPps) && ((naluType == ARSTREAM2_H264_FILTER_H264_NALU_TYPE_SPS) || (naluType == ARSTREAM2_H264_FILTER_H264_NALU_TYPE_PPS)))
    {
        filterOut = 1;
    }

    if ((filter->filterOutSei) && (naluType == ARSTREAM2_H264_FILTER_H264_NALU_TYPE_SEI))
    {
        filterOut = 1;
    }

    if (!filterOut)
    {
        filter->currentAuSize += naluSize;
    }
}


static int ARSTREAM2_H264Filter_enqueueCurrentAu(ARSTREAM2_H264Filter_t *filter)
{
    int ret = 0;
    int cancelAuOutput = 0;

    if (filter->currentAuSyncType != ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_IDR)
    {
        if (filter->currentAuSlicesAllI)
        {
            filter->currentAuSyncType = ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_IFRAME;
        }
        else if ((filter->currentAuStreamingInfoAvailable) && (filter->currentAuStreamingInfo.indexInGop == 0))
        {
            filter->currentAuSyncType = ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_PIR_START;
        }
    }

    if ((!filter->outputIncompleteAu) && (filter->currentAuIncomplete))
    {
        cancelAuOutput = 1;
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "AU output cancelled (!outputIncompleteAu)"); //TODO: debug
    }

    if (!cancelAuOutput)
    {
        ARSAL_Mutex_Lock(&(filter->mutex));

        if ((filter->waitForSync) && (!filter->sync))
        {
            cancelAuOutput = 1;
            if (filter->running)
            {
                ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "AU output cancelled (waitForSync)"); //TODO: debug
            }
            ARSAL_Mutex_Unlock(&(filter->mutex));
        }
    }

    if (!cancelAuOutput)
    {
        eARSTREAM2_ERROR cbRet;
        uint64_t curTime;
        struct timespec t1;
        uint8_t *auBuffer = NULL;
        int auBufferSize = 0;
        void *auBufferUserPtr = NULL;

        /* call the getAuBufferCallback */
        filter->callbackInProgress = 1;
        ARSAL_Mutex_Unlock(&(filter->mutex));

        cbRet = filter->getAuBufferCallback(&auBuffer, &auBufferSize, &auBufferUserPtr, filter->getAuBufferCallbackUserPtr);

        ARSAL_Mutex_Lock(&(filter->mutex));

        if ((cbRet != ARSTREAM2_OK) || (!auBuffer) || (!auBufferSize))
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "getAuBufferCallback failed: %s", ARSTREAM2_Error_ToString(cbRet));
            filter->callbackInProgress = 0;
            ARSAL_Mutex_Unlock(&(filter->mutex));
            ARSAL_Cond_Signal(&(filter->callbackCond));
            ret = -1;
        }
        else
        {
            int auSize = (filter->currentAuSize < auBufferSize) ? filter->currentAuSize : auBufferSize;
            memcpy(auBuffer, filter->currentAuBuffer, auSize);

            ARSAL_Time_GetTime(&t1);
            curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

            /* call the auReadyCallback */
            ARSAL_Mutex_Unlock(&(filter->mutex));

            cbRet = filter->auReadyCallback(auBuffer, auSize, filter->currentAuTimestamp, filter->currentAuTimestampShifted, filter->currentAuSyncType,
                                            (filter->currentAuUserDataSize > 0) ? &filter->currentAuUserData : NULL, filter->currentAuUserDataSize,
                                            auBufferUserPtr, filter->auReadyCallbackUserPtr);

            ARSAL_Mutex_Lock(&(filter->mutex));
            filter->callbackInProgress = 0;
            ARSAL_Mutex_Unlock(&(filter->mutex));
            ARSAL_Cond_Signal(&(filter->callbackCond));

            if (cbRet != ARSTREAM2_OK)
            {
                ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "auReadyCallback failed: %s", ARSTREAM2_Error_ToString(cbRet));
                if ((cbRet == ARSTREAM2_ERROR_RESYNC_REQUIRED) && (filter->generateFirstGrayIFrame))
                {
                    filter->firstGrayIFramePending = 1;
                }
            }
            else
            {
                ret = 1;
            }

#ifdef ARSTREAM2_H264_FILTER_STREAM_OUTPUT
            if (filter->fStreamOut)
            {
                fwrite(auBuffer, auSize, 1, filter->fStreamOut);
            }
#endif
        }
    }

    return ret;
}


static int ARSTREAM2_H264Filter_generateGrayIFrame(ARSTREAM2_H264Filter_t *filter, uint8_t *naluBuffer, int naluSize, ARSTREAM2_H264Filter_H264NaluType_t naluType)
{
    int ret = 0;
    eARSTREAM2_ERROR err = ARSTREAM2_OK;
    ARSTREAM2_H264_SliceContext_t *sliceContextNext = NULL;
    ARSTREAM2_H264_SliceContext_t sliceContext;

    if ((naluType != ARSTREAM2_H264_FILTER_H264_NALU_TYPE_SLICE_IDR) && (naluType != ARSTREAM2_H264_FILTER_H264_NALU_TYPE_SLICE))
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "Waiting for a slice to generate gray I-frame"); //TODO: debug
        return -2;
    }

    err = ARSTREAM2_H264Parser_GetSliceContext(filter->parser, (void**)&sliceContextNext);
    if (err != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Parser_GetSliceContext() failed (%d)", err);
        ret = -1;
    }
    memcpy(&sliceContext, sliceContextNext, sizeof(sliceContext));

    if (ret == 0)
    {
        unsigned int outputSize, mbCount;

        mbCount = filter->mbWidth * filter->mbHeight;
        sliceContext.nal_ref_idc = 3;
        sliceContext.nal_unit_type = ARSTREAM2_H264_NALU_TYPE_SLICE_IDR;
        sliceContext.idrPicFlag = 1;
        sliceContext.slice_type = ARSTREAM2_H264_SLICE_TYPE_I;
        sliceContext.frame_num = 0;
        sliceContext.idr_pic_id = 0;
        sliceContext.no_output_of_prior_pics_flag = 0;
        sliceContext.long_term_reference_flag = 0;

        err = ARSTREAM2_H264Writer_WriteGrayISliceNalu(filter->writer, 0, mbCount, (void*)&sliceContext, filter->tempSliceNaluBuffer, filter->tempSliceNaluBufferSize, &outputSize);
        if (err != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Writer_WriteGrayISliceNalu() failed (%d)", err);
            ret = -1;
        }
        else
        {
            uint8_t *tmpBuf = NULL;
            int savedAuSize = filter->currentAuSize;
            int savedAuIncomplete = filter->currentAuIncomplete;
            eARSTREAM2_H264_FILTER_AU_SYNC_TYPE savedAuSyncType = filter->currentAuSyncType;
            int savedAuSlicesAllI = filter->currentAuSlicesAllI;
            int savedAuStreamingInfoAvailable = filter->currentAuStreamingInfoAvailable;
            int savedAuUserDataSize = filter->currentAuUserDataSize;
            int savedAuPreviousSliceIndex = filter->currentAuPreviousSliceIndex;
            int savedAuPreviousSliceFirstMb = filter->currentAuPreviousSliceFirstMb;
            int savedAuCurrentSliceFirstMb = filter->currentAuCurrentSliceFirstMb;
            uint64_t savedAuTimestamp = filter->currentAuTimestamp;
            uint64_t savedAuTimestampShifted = filter->currentAuTimestampShifted;
            uint64_t savedAuFirstNaluInputTime = filter->currentAuFirstNaluInputTime;
            ret = 0;

            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "Gray I slice NALU output size: %d", outputSize); //TODO: debug

            if (filter->replaceStartCodesWithNaluSize)
            {
                // Replace the NAL unit 4 bytes start code with the NALU size
                *((uint32_t*)filter->tempSliceNaluBuffer) = htonl((uint32_t)outputSize - 4);
            }

            if (filter->currentAuSize + naluSize > 0)
            {
                tmpBuf = malloc(filter->currentAuSize + naluSize); //TODO
                if (tmpBuf)
                {
                    memcpy(tmpBuf, filter->currentAuBuffer, filter->currentAuSize + naluSize);
                }
            }

            ARSTREAM2_H264Filter_resetCurrentAu(filter);
            filter->currentAuSyncType = ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_IDR;
            filter->currentAuTimestamp -= ((filter->currentAuTimestamp >= 1000) ? 1000 : ((filter->currentAuTimestamp >= 1) ? 1 : 0));
            filter->currentAuTimestampShifted -= ((filter->currentAuTimestampShifted >= 1000) ? 1000 : ((filter->currentAuTimestampShifted >= 1) ? 1 : 0));
            filter->currentAuFirstNaluInputTime -= ((filter->currentAuFirstNaluInputTime >= 1000) ? 1000 : ((filter->currentAuFirstNaluInputTime >= 1) ? 1 : 0));

            if (!filter->filterOutSpsPps)
            {
                // Insert SPS+PPS before the I-frame
                if (ret == 0)
                {
                    if (filter->currentAuSize + filter->spsSize <= filter->currentAuBufferSize)
                    {
                        memcpy(filter->currentAuBuffer + filter->currentAuSize, filter->pSps, filter->spsSize);
                        filter->currentAuSize += filter->spsSize;
                    }
                    else
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Access unit buffer is too small for the SPS NALU (size %d, access unit size %s)", filter->spsSize, filter->currentAuSize);
                        ret = -1;
                    }
                }
                if (ret == 0)
                {
                    if (filter->currentAuSize + filter->ppsSize <= filter->currentAuBufferSize)
                    {
                        memcpy(filter->currentAuBuffer + filter->currentAuSize, filter->pPps, filter->ppsSize);
                        filter->currentAuSize += filter->ppsSize;
                    }
                    else
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Access unit buffer is too small for the PPS NALU (size %d, access unit size %s)", filter->ppsSize, filter->currentAuSize);
                        ret = -1;
                    }
                }
            }

            // Copy the gray I-frame
            if (ret == 0)
            {
                if (filter->currentAuSize + (int)outputSize <= filter->currentAuBufferSize)
                {
                    memcpy(filter->currentAuBuffer + filter->currentAuSize, filter->tempSliceNaluBuffer, outputSize);
                    filter->currentAuSize += outputSize;
                }
                else
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Access unit buffer is too small for the I-frame (size %d, access unit size %s)", outputSize, filter->currentAuSize);
                    ret = -1;
                }
            }

            // Output access unit
            if (ret == 0)
            {
                ret = ARSTREAM2_H264Filter_enqueueCurrentAu(filter);

                if ((ret > 0) || (filter->auBufferChangePending))
                {
                    // The access unit has been enqueued or a buffer change is pending
                    ret = ARSTREAM2_H264Filter_getNewAuBuffer(filter);
                    if (ret == 0)
                    {
                        ARSTREAM2_H264Filter_resetCurrentAu(filter);

                        if (tmpBuf)
                        {
                            memcpy(filter->currentAuBuffer, tmpBuf, savedAuSize + naluSize);
                        }

                        filter->currentAuTimestamp = savedAuTimestamp;
                        filter->currentAuTimestampShifted = savedAuTimestampShifted;
                        filter->currentAuFirstNaluInputTime = savedAuFirstNaluInputTime;
                        filter->currentAuSize = savedAuSize;
                        filter->currentAuIncomplete = savedAuIncomplete;
                        filter->currentAuSyncType = savedAuSyncType;
                        filter->currentAuSlicesAllI = savedAuSlicesAllI;
                        filter->currentAuStreamingInfoAvailable = savedAuStreamingInfoAvailable;
                        filter->currentAuUserDataSize = savedAuUserDataSize;
                        filter->currentAuPreviousSliceIndex = savedAuPreviousSliceIndex;
                        filter->currentAuPreviousSliceFirstMb = savedAuPreviousSliceFirstMb;
                        filter->currentAuCurrentSliceFirstMb = savedAuCurrentSliceFirstMb;

                        filter->currentNaluBuffer = filter->currentAuBuffer + filter->currentAuSize;
                        filter->currentNaluBufferSize = filter->currentAuBufferSize - filter->currentAuSize;
                    }
                    else
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_getNewAuBuffer() failed (%d)", ret);
                        ret = -1;
                    }
                }
                else
                {
                    // The access unit has not been enqueued: reuse current auBuffer
                    ARSTREAM2_H264Filter_resetCurrentAu(filter);
                    filter->currentAuTimestamp = savedAuTimestamp;
                    filter->currentAuTimestampShifted = savedAuTimestampShifted;
                    filter->currentAuFirstNaluInputTime = savedAuFirstNaluInputTime;

                    if (tmpBuf)
                    {
                        memcpy(filter->currentAuBuffer, tmpBuf, savedAuSize + naluSize);

                        filter->currentAuSize = savedAuSize;
                        filter->currentAuIncomplete = savedAuIncomplete;
                        filter->currentAuSyncType = savedAuSyncType;
                        filter->currentAuSlicesAllI = savedAuSlicesAllI;
                        filter->currentAuStreamingInfoAvailable = savedAuStreamingInfoAvailable;
                        filter->currentAuUserDataSize = savedAuUserDataSize;
                        filter->currentAuPreviousSliceIndex = savedAuPreviousSliceIndex;
                        filter->currentAuPreviousSliceFirstMb = savedAuPreviousSliceFirstMb;
                        filter->currentAuCurrentSliceFirstMb = savedAuCurrentSliceFirstMb;
                    }
                }
            }

            if (tmpBuf) free(tmpBuf);
        }
    }

    return ret;
}


static int ARSTREAM2_H264Filter_fillMissingSlices(ARSTREAM2_H264Filter_t *filter, uint8_t *naluBuffer, int naluSize, ARSTREAM2_H264Filter_H264NaluType_t naluType, ARSTREAM2_H264Filter_H264SliceType_t sliceType, int isFirstNaluInAu)
{
    int missingMb = 0, firstMbInSlice = 0, ret = 0;

    if (isFirstNaluInAu)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "Missing NALU is probably on previous AU => OK"); //TODO: debug
        if (filter->currentAuCurrentSliceFirstMb == 0)
        {
            filter->currentAuPreviousSliceFirstMb = 0;
            filter->currentAuPreviousSliceIndex = 0;
        }
        return 0;
    }
    else if (((naluType != ARSTREAM2_H264_FILTER_H264_NALU_TYPE_SLICE_IDR) && (naluType != ARSTREAM2_H264_FILTER_H264_NALU_TYPE_SLICE)) || (filter->currentAuCurrentSliceFirstMb == 0))
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "Missing NALU is probably a SPS, PPS or SEI or on previous AU => OK"); //TODO: debug
        if (filter->currentAuCurrentSliceFirstMb == 0)
        {
            filter->currentAuPreviousSliceFirstMb = 0;
            filter->currentAuPreviousSliceIndex = 0;
        }
        return 0;
    }
    else if (!filter->generateSkippedPSlices)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "Missing NALU is probably a slice"); //TODO: debug
        return -2;
    }

    ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "Missing NALU is probably a slice"); //TODO: debug

    if (!filter->sync)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "No sync, abort"); //TODO: debug
        return -2;
    }

    if ((!filter->currentAuStreamingInfoAvailable) && (filter->currentAuSlicesReceived))
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "Streaming info is not available"); //TODO: debug
        return -2;
    }

    if (sliceType != ARSTREAM2_H264_FILTER_H264_SLICE_TYPE_P)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "Current slice is not a P-slice, aborting"); //TODO: debug
        return -2;
    }

    if (filter->currentAuPreviousSliceIndex < 0)
    {
        // No previous slice received
        if (filter->currentAuCurrentSliceFirstMb > 0)
        {
            firstMbInSlice = 0;
            missingMb = filter->currentAuCurrentSliceFirstMb;
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "currentSliceFirstMb: %d - missingMb: %d",
                        filter->currentAuCurrentSliceFirstMb, missingMb); //TODO: debug
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "Error: previousSliceIdx: %d - currentSliceFirstMb: %d - this should not happen!",
                        filter->currentAuPreviousSliceIndex, filter->currentAuCurrentSliceFirstMb); //TODO: debug
            missingMb = 0;
            ret = -1;
        }
    }
    else if ((filter->currentAuCurrentSliceFirstMb > filter->currentAuPreviousSliceFirstMb + filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex]))
    {
        // Slices have been received before
        firstMbInSlice = filter->currentAuPreviousSliceFirstMb + filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex];
        missingMb = filter->currentAuCurrentSliceFirstMb - firstMbInSlice;
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "previousSliceFirstMb: %d - previousSliceMbCount: %d - currentSliceFirstMb: %d - missingMb: %d - firstMbInSlice: %d",
                    filter->currentAuPreviousSliceFirstMb, filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex], filter->currentAuCurrentSliceFirstMb, missingMb, firstMbInSlice); //TODO: debug
    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "Error: previousSliceFirstMb: %d - previousSliceMbCount: %d - currentSliceFirstMb: %d - this should not happen!",
                    filter->currentAuPreviousSliceFirstMb, filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex], filter->currentAuCurrentSliceFirstMb); //TODO: debug
        missingMb = 0;
        ret = -1;
    }

    if (missingMb > 0)
    {
        void *sliceContext;
        eARSTREAM2_ERROR err = ARSTREAM2_OK;
        err = ARSTREAM2_H264Parser_GetSliceContext(filter->parser, &sliceContext);
        if (err != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Parser_GetSliceContext() failed (%d)", err);
            ret = -1;
        }
        if (ret == 0)
        {
            unsigned int outputSize;
            err = ARSTREAM2_H264Writer_WriteSkippedPSliceNalu(filter->writer, firstMbInSlice, missingMb, sliceContext, filter->tempSliceNaluBuffer, filter->tempSliceNaluBufferSize, &outputSize);
            if (err != ARSTREAM2_OK)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Writer_WriteSkippedPSliceNalu() failed (%d)", err);
                ret = -1;
            }
            else
            {
                ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "Skipped P slice NALU output size: %d", outputSize); //TODO: debug
                if (filter->currentAuSize + naluSize + (int)outputSize <= filter->currentAuBufferSize)
                {
                    if (filter->replaceStartCodesWithNaluSize)
                    {
                        // Replace the NAL unit 4 bytes start code with the NALU size
                        *((uint32_t*)filter->tempSliceNaluBuffer) = htonl((uint32_t)outputSize - 4);
                    }
                    memmove(naluBuffer + outputSize, naluBuffer, naluSize); //TODO
                    memcpy(naluBuffer, filter->tempSliceNaluBuffer, outputSize);
                    filter->currentAuSize += outputSize;
                }
                else
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Access unit buffer is too small for the generated skipped P slice (size %d, access unit size %s)", outputSize, filter->currentAuSize + naluSize);
                    ret = -1;
                }
            }
        }
    }

    return ret;
}


static int ARSTREAM2_H264Filter_fillMissingEndOfFrame(ARSTREAM2_H264Filter_t *filter, ARSTREAM2_H264Filter_H264SliceType_t sliceType)
{
    int missingMb = 0, firstMbInSlice = 0, ret = 0;

    if (!filter->generateSkippedPSlices)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "Missing NALU is probably a slice"); //TODO: debug
        return -2;
    }

    ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "Missing NALU is probably a slice"); //TODO: debug

    if (!filter->sync)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "No sync, abort"); //TODO: debug
        return -2;
    }

    if (!filter->currentAuStreamingInfoAvailable)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "Streaming info is not available"); //TODO: debug
        return -2;
    }

    if (filter->currentAuPreviousSliceIndex < 0)
    {
        // No previous slice received
        firstMbInSlice = 0;
        missingMb = filter->mbWidth * filter->mbHeight;

        //TODO: slice context
        //UNSUPPORTED
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "No previous slice received"); //TODO: debug
        return -1;
    }
    else
    {
        // Slices have been received before
        firstMbInSlice = filter->currentAuPreviousSliceFirstMb + filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex];
        missingMb = filter->mbWidth * filter->mbHeight - firstMbInSlice;
        if (sliceType != ARSTREAM2_H264_FILTER_H264_SLICE_TYPE_P)
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "Previous slice is not a P-slice, aborting"); //TODO: debug
            return -2;
        }
    }

    if (missingMb > 0)
    {
        void *sliceContext;
        eARSTREAM2_ERROR err = ARSTREAM2_OK;
        err = ARSTREAM2_H264Parser_GetSliceContext(filter->parser, &sliceContext);
        if (err != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Parser_GetSliceContext() failed (%d)", err);
            ret = -1;
        }
        if (ret == 0)
        {
            unsigned int outputSize;
            err = ARSTREAM2_H264Writer_WriteSkippedPSliceNalu(filter->writer, firstMbInSlice, missingMb, sliceContext, filter->tempSliceNaluBuffer, filter->tempSliceNaluBufferSize, &outputSize);
            if (err != ARSTREAM2_OK)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Writer_WriteSkippedPSliceNalu() failed (%d)", err);
                ret = -1;
            }
            else
            {
                ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "Skipped P slice NALU output size: %d", outputSize); //TODO: debug
                if (filter->currentAuSize + (int)outputSize <= filter->currentAuBufferSize)
                {
                    if (filter->replaceStartCodesWithNaluSize)
                    {
                        // Replace the NAL unit 4 bytes start code with the NALU size
                        *((uint32_t*)filter->tempSliceNaluBuffer) = htonl((uint32_t)outputSize - 4);
                    }
                    memcpy(filter->currentAuBuffer + filter->currentAuSize, filter->tempSliceNaluBuffer, outputSize);
                    filter->currentAuSize += outputSize;
                }
                else
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Access unit buffer is too small for the generated skipped P slice (size %d, access unit size %s)", outputSize, filter->currentAuSize);
                    ret = -1;
                }
            }
        }
    }

    return ret;
}


uint8_t* ARSTREAM2_H264Filter_RtpReceiverNaluCallback(eARSTREAM2_RTP_RECEIVER_CAUSE cause, uint8_t *naluBuffer, int naluSize, uint64_t auTimestamp,
                                                      uint64_t auTimestampShifted, int isFirstNaluInAu, int isLastNaluInAu,
                                                      int missingPacketsBefore, int *newNaluBufferSize, void *custom)
{
    ARSTREAM2_H264Filter_t* filter = (ARSTREAM2_H264Filter_t*)custom;
    ARSTREAM2_H264Filter_H264NaluType_t naluType = ARSTREAM2_H264_FILTER_H264_NALU_TYPE_UNKNOWN;
    ARSTREAM2_H264Filter_H264SliceType_t sliceType = ARSTREAM2_H264_FILTER_H264_SLICE_TYPE_NON_VCL;
    int ret = 0;
    uint8_t *retPtr = NULL;
    uint64_t curTime;
    struct timespec t1;

    if (!filter)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Invalid filter instance");
        return retPtr;
    }

    switch (cause)
    {
        case ARSTREAM2_RTP_RECEIVER_CAUSE_NALU_COMPLETE:
            ARSAL_Time_GetTime(&t1);
            curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

            if ((!naluBuffer) || (naluSize <= 0))
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Invalid NALU buffer");
                return retPtr;
            }

            // Handle a previous access unit that has not been output
            if ((filter->currentAuSize > 0)
                    && ((isFirstNaluInAu) || ((filter->currentAuTimestamp != 0) && (auTimestamp != filter->currentAuTimestamp))))
            {
                uint8_t *tmpBuf = malloc(naluSize); //TODO
                if (tmpBuf)
                {
                    memcpy(tmpBuf, naluBuffer, naluSize);
                }

                // Fill the missing slices with fake bitstream
                ret = ARSTREAM2_H264Filter_fillMissingEndOfFrame(filter, filter->previousSliceType);
                if (ret < 0)
                {
                    if (ret != -2)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_fillMissingEndOfFrame() failed (%d)", ret);
                    }
                    filter->currentAuIncomplete = 1;
                }

                // Output the access unit
                ret = ARSTREAM2_H264Filter_enqueueCurrentAu(filter);
                ARSTREAM2_H264Filter_resetCurrentAu(filter);

                if ((ret > 0) || (filter->auBufferChangePending))
                {
                    // The access unit has been enqueued or a buffer change is pending
                    ret = ARSTREAM2_H264Filter_getNewAuBuffer(filter);
                    if (ret == 0)
                    {
                        filter->currentNaluBuffer = filter->currentAuBuffer + filter->currentAuSize;
                        filter->currentNaluBufferSize = filter->currentAuBufferSize - filter->currentAuSize;
                        if (tmpBuf)
                        {
                            if (naluSize <= filter->currentNaluBufferSize)
                            {
                                memcpy(filter->currentNaluBuffer, tmpBuf, naluSize);
                                naluBuffer = filter->currentNaluBuffer;
                            }
                            else
                            {
                                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Failed to copy the pending NALU to the currentNaluBuffer (size=%d)", naluSize);
                            }
                        }
                    }
                    else
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_getNewAuBuffer() failed (%d)", ret);
                        filter->currentNaluBuffer = NULL;
                        filter->currentNaluBufferSize = 0;
                    }
                }
                else
                {
                    // The access unit has not been enqueued: reuse current auBuffer
                    filter->currentNaluBuffer = filter->currentAuBuffer + filter->currentAuSize;
                    filter->currentNaluBufferSize = filter->currentAuBufferSize - filter->currentAuSize;
                    if (tmpBuf)
                    {
                        memcpy(filter->currentNaluBuffer, tmpBuf, naluSize); //TODO: filter->currentNaluBufferSize must be > naluSize
                        naluBuffer = filter->currentNaluBuffer;
                    }
                }

                if (tmpBuf) free(tmpBuf);
            }

            ret = ARSTREAM2_H264Filter_processNalu(filter, naluBuffer, naluSize, &naluType, &sliceType);
            if (ret < 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_processNalu() failed (%d)", ret);
                if ((!filter->currentAuBuffer) || (filter->currentAuBufferSize <= 0))
                {
                    filter->currentNaluBuffer = NULL;
                    filter->currentNaluBufferSize = 0;
                }
            }

            /*ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_H264_FILTER_TAG, "Received NALU type=%d sliceType=%d size=%d ts=%llu (first=%d, last=%d, missingBefore=%d)",
                        naluType, sliceType, naluSize, (long long unsigned int)auTimestamp, isFirstNaluInAu, isLastNaluInAu, missingPacketsBefore);*/ //TODO: debug

            if ((filter->currentNaluBuffer == NULL) || (filter->currentNaluBufferSize == 0))
            {
                // We failed to get a new AU buffer previously; drop the NALU
                *newNaluBufferSize = filter->currentNaluBufferSize;
                retPtr = filter->currentNaluBuffer;
            }
            else
            {
                filter->currentAuTimestamp = auTimestamp;
                filter->currentAuTimestampShifted = auTimestampShifted;
                if (filter->currentAuFirstNaluInputTime == 0) filter->currentAuFirstNaluInputTime = curTime;
                filter->previousSliceType = sliceType;

                if (filter->firstGrayIFramePending)
                {
                    // Generate fake bitstream
                    ret = ARSTREAM2_H264Filter_generateGrayIFrame(filter, naluBuffer, naluSize, naluType);
                    if (ret < 0)
                    {
                        if (ret != -2)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_generateGrayIFrame() failed (%d)", ret);
                            if ((!filter->currentAuBuffer) || (filter->currentAuBufferSize <= 0))
                            {
                                filter->currentNaluBuffer = NULL;
                                filter->currentNaluBufferSize = 0;
                            }
                        }
                    }
                    else
                    {
                        filter->firstGrayIFramePending = 0;
                    }
                }

                if ((filter->currentNaluBuffer == NULL) || (filter->currentNaluBufferSize == 0))
                {
                    // We failed to get a new AU buffer previously; drop the NALU
                    *newNaluBufferSize = filter->currentNaluBufferSize;
                    retPtr = filter->currentNaluBuffer;
                }
                else
                {
                    if (missingPacketsBefore)
                    {
                        // Fill the missing slices with fake bitstream
                        ret = ARSTREAM2_H264Filter_fillMissingSlices(filter, naluBuffer, naluSize, naluType, sliceType, isFirstNaluInAu);
                        if (ret < 0)
                        {
                            if (ret != -2)
                            {
                                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_fillMissingSlices() failed (%d)", ret);
                            }
                            filter->currentAuIncomplete = 1;
                        }
                    }
                    ARSTREAM2_H264Filter_updateCurrentAu(filter, naluType);

                    ARSTREAM2_H264Filter_addNaluToCurrentAu(filter, naluType, naluSize);

                    if (isLastNaluInAu)
                    {
                        // Output the access unit
                        ret = ARSTREAM2_H264Filter_enqueueCurrentAu(filter);
                        ARSTREAM2_H264Filter_resetCurrentAu(filter);

                        if ((ret > 0) || (filter->auBufferChangePending))
                        {
                            // The access unit has been enqueued or a buffer change is pending
                            ret = ARSTREAM2_H264Filter_getNewAuBuffer(filter);
                            if (ret == 0)
                            {
                                filter->currentNaluBuffer = filter->currentAuBuffer + filter->currentAuSize;
                                filter->currentNaluBufferSize = filter->currentAuBufferSize - filter->currentAuSize;
                            }
                            else
                            {
                                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_getNewAuBuffer() failed (%d)", ret);
                                filter->currentNaluBuffer = NULL;
                                filter->currentNaluBufferSize = 0;
                            }
                        }
                        else
                        {
                            // The access unit has not been enqueued: reuse current auBuffer
                            filter->currentNaluBuffer = filter->currentAuBuffer + filter->currentAuSize;
                            filter->currentNaluBufferSize = filter->currentAuBufferSize - filter->currentAuSize;
                        }
                    }
                    else
                    {
                        filter->currentNaluBuffer = filter->currentAuBuffer + filter->currentAuSize;
                        filter->currentNaluBufferSize = filter->currentAuBufferSize - filter->currentAuSize;
                    }
                    *newNaluBufferSize = filter->currentNaluBufferSize;
                    retPtr = filter->currentNaluBuffer;
                }
            }
            break;
        case ARSTREAM2_RTP_RECEIVER_CAUSE_NALU_BUFFER_TOO_SMALL:
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_RtpReceiver NALU buffer is too small, truncated AU (or maybe it's the first call)");

            ret = 1;
            if ((filter->currentAuBuffer) && (filter->currentAuSize > 0))
            {
                // Output the access unit
                ret = ARSTREAM2_H264Filter_enqueueCurrentAu(filter);
            }
            ARSTREAM2_H264Filter_resetCurrentAu(filter);

            if ((ret > 0) || (filter->auBufferChangePending))
            {
                // The access unit has been enqueued or no AU was pending or a buffer change is pending
                ret = ARSTREAM2_H264Filter_getNewAuBuffer(filter);
                if (ret == 0)
                {
                    filter->currentNaluBuffer = filter->currentAuBuffer + filter->currentAuSize;
                    filter->currentNaluBufferSize = filter->currentAuBufferSize - filter->currentAuSize;
                }
                else
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_getNewAuBuffer() failed (%d)", ret);
                    filter->currentNaluBuffer = NULL;
                    filter->currentNaluBufferSize = 0;
                }
            }
            else
            {
                // The access unit has not been enqueued: reuse current auBuffer
                filter->currentNaluBuffer = filter->currentAuBuffer + filter->currentAuSize;
                filter->currentNaluBufferSize = filter->currentAuBufferSize - filter->currentAuSize;
            }
            *newNaluBufferSize = filter->currentNaluBufferSize;
            retPtr = filter->currentNaluBuffer;
            break;
        case ARSTREAM2_RTP_RECEIVER_CAUSE_NALU_COPY_COMPLETE:
            *newNaluBufferSize = filter->currentNaluBufferSize;
            retPtr = filter->currentNaluBuffer;
            break;
        case ARSTREAM2_RTP_RECEIVER_CAUSE_CANCEL:
            //TODO?
            *newNaluBufferSize = 0;
            retPtr = NULL;
            break;
        default:
            break;
    }

    return retPtr;
}


eARSTREAM2_ERROR ARSTREAM2_H264Filter_GetSpsPps(ARSTREAM2_H264Filter_Handle filterHandle, uint8_t *spsBuffer, int *spsSize, uint8_t *ppsBuffer, int *ppsSize)
{
    ARSTREAM2_H264Filter_t* filter = (ARSTREAM2_H264Filter_t*)filterHandle;
    int ret = ARSTREAM2_OK;

    if (!filterHandle)
    {
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    if ((!spsSize) || (!ppsSize))
    {
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    ARSAL_Mutex_Lock(&(filter->mutex));

    if (!filter->sync)
    {
        ret = ARSTREAM2_ERROR_WAITING_FOR_SYNC;
    }

    if (ret == ARSTREAM2_OK)
    {
        if ((!spsBuffer) || (*spsSize < filter->spsSize))
        {
            *spsSize = filter->spsSize;
        }
        else
        {
            memcpy(spsBuffer, filter->pSps, filter->spsSize);
            *spsSize = filter->spsSize;
        }

        if ((!ppsBuffer) || (*ppsSize < filter->ppsSize))
        {
            *ppsSize = filter->ppsSize;
        }
        else
        {
            memcpy(ppsBuffer, filter->pPps, filter->ppsSize);
            *ppsSize = filter->ppsSize;
        }
    }

    ARSAL_Mutex_Unlock(&(filter->mutex));

    return ret;
}


void* ARSTREAM2_H264Filter_RunFilterThread(void *filterHandle)
{
    ARSTREAM2_H264Filter_t* filter = (ARSTREAM2_H264Filter_t*)filterHandle;

    if (!filter)
    {
        return (void *)0;
    }

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_H264_FILTER_TAG, "Filter thread is started");
    ARSAL_Mutex_Lock(&(filter->mutex));
    filter->threadStarted = 1;
    ARSAL_Mutex_Unlock(&(filter->mutex));

    // The thread is unused for now

    ARSAL_Mutex_Lock(&(filter->mutex));
    filter->threadStarted = 0;
    ARSAL_Mutex_Unlock(&(filter->mutex));
    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_H264_FILTER_TAG, "Filter thread has ended");

    return (void *)0;
}


eARSTREAM2_ERROR ARSTREAM2_H264Filter_Start(ARSTREAM2_H264Filter_Handle filterHandle, ARSTREAM2_H264Filter_SpsPpsCallback_t spsPpsCallback, void* spsPpsCallbackUserPtr,
                        ARSTREAM2_H264Filter_GetAuBufferCallback_t getAuBufferCallback, void* getAuBufferCallbackUserPtr,
                        ARSTREAM2_H264Filter_AuReadyCallback_t auReadyCallback, void* auReadyCallbackUserPtr)
{
    ARSTREAM2_H264Filter_t* filter = (ARSTREAM2_H264Filter_t*)filterHandle;
    int ret = ARSTREAM2_OK;

    if (!filterHandle)
    {
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!getAuBufferCallback)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Invalid getAuBufferCallback function pointer");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!auReadyCallback)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Invalid auReadyCallback function pointer");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    ARSAL_Mutex_Lock(&(filter->mutex));
    filter->spsPpsCallback = spsPpsCallback;
    filter->spsPpsCallbackUserPtr = spsPpsCallbackUserPtr;
    filter->getAuBufferCallback = getAuBufferCallback;
    filter->getAuBufferCallbackUserPtr = getAuBufferCallbackUserPtr;
    filter->auReadyCallback = auReadyCallback;
    filter->auReadyCallbackUserPtr = auReadyCallbackUserPtr;
    filter->running = 1;
    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_H264_FILTER_TAG, "Filter is running");
    ARSAL_Mutex_Unlock(&(filter->mutex));
    ARSAL_Cond_Signal(&(filter->startCond));

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_H264Filter_Pause(ARSTREAM2_H264Filter_Handle filterHandle)
{
    ARSTREAM2_H264Filter_t* filter = (ARSTREAM2_H264Filter_t*)filterHandle;
    int ret = ARSTREAM2_OK;

    if (!filterHandle)
    {
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    ARSAL_Mutex_Lock(&(filter->mutex));
    if (filter->callbackInProgress)
    {
        ARSAL_Cond_Wait(&(filter->callbackCond), &(filter->mutex));
    }
    filter->spsPpsCallback = NULL;
    filter->spsPpsCallbackUserPtr = NULL;
    filter->getAuBufferCallback = NULL;
    filter->getAuBufferCallbackUserPtr = NULL;
    filter->auReadyCallback = NULL;
    filter->auReadyCallbackUserPtr = NULL;
    filter->running = 0;
    filter->sync = 0;
    filter->auBufferChangePending = 1;
    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_H264_FILTER_TAG, "Filter is paused");
    ARSAL_Mutex_Unlock(&(filter->mutex));

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_H264Filter_Stop(ARSTREAM2_H264Filter_Handle filterHandle)
{
    ARSTREAM2_H264Filter_t* filter = (ARSTREAM2_H264Filter_t*)filterHandle;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;

    if (!filterHandle)
    {
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_H264_FILTER_TAG, "Stopping Filter...");
    ARSAL_Mutex_Lock(&(filter->mutex));
    filter->threadShouldStop = 1;
    ARSAL_Mutex_Unlock(&(filter->mutex));
    /* signal the filter thread to avoid a deadlock */
    ARSAL_Cond_Signal(&(filter->startCond));

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_H264Filter_Init(ARSTREAM2_H264Filter_Handle *filterHandle, ARSTREAM2_H264Filter_Config_t *config)
{
    ARSTREAM2_H264Filter_t* filter;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;
    int mutexWasInit = 0, startCondWasInit = 0, callbackCondWasInit = 0;

    if (!filterHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Invalid pointer for handle");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!config)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Invalid pointer for config");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    filter = (ARSTREAM2_H264Filter_t*)malloc(sizeof(*filter));
    if (!filter)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Allocation failed (size %ld)", sizeof(*filter));
        ret = ARSTREAM2_ERROR_ALLOC;
    }

    if (ret == ARSTREAM2_OK)
    {
        memset(filter, 0, sizeof(*filter));

        filter->waitForSync = (config->waitForSync > 0) ? 1 : 0;
        filter->outputIncompleteAu = (config->outputIncompleteAu > 0) ? 1 : 0;
        filter->filterOutSpsPps = (config->filterOutSpsPps > 0) ? 1 : 0;
        filter->filterOutSei = (config->filterOutSei > 0) ? 1 : 0;
        filter->replaceStartCodesWithNaluSize = (config->replaceStartCodesWithNaluSize > 0) ? 1 : 0;
        filter->generateSkippedPSlices = (config->generateSkippedPSlices > 0) ? 1 : 0;
        filter->generateFirstGrayIFrame = (config->generateFirstGrayIFrame > 0) ? 1 : 0;
    }

    if (ret == ARSTREAM2_OK)
    {
        filter->tempAuBuffer = malloc(ARSTREAM2_H264_FILTER_TEMP_AU_BUFFER_SIZE);
        if (filter->tempAuBuffer == NULL)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Allocation failed (size %d)", ARSTREAM2_H264_FILTER_TEMP_AU_BUFFER_SIZE);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            filter->tempAuBufferSize = ARSTREAM2_H264_FILTER_TEMP_AU_BUFFER_SIZE;
        }
    }

    if (ret == ARSTREAM2_OK)
    {
        int mutexInitRet = ARSAL_Mutex_Init(&(filter->mutex));
        if (mutexInitRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Mutex creation failed (%d)", mutexInitRet);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            mutexWasInit = 1;
        }
    }
    if (ret == ARSTREAM2_OK)
    {
        int condInitRet = ARSAL_Cond_Init(&(filter->startCond));
        if (condInitRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Cond creation failed (%d)", condInitRet);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            startCondWasInit = 1;
        }
    }
    if (ret == ARSTREAM2_OK)
    {
        int condInitRet = ARSAL_Cond_Init(&(filter->callbackCond));
        if (condInitRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Cond creation failed (%d)", condInitRet);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            callbackCondWasInit = 1;
        }
    }

    if (ret == ARSTREAM2_OK)
    {
        ARSTREAM2_H264Parser_Config_t parserConfig;
        memset(&parserConfig, 0, sizeof(parserConfig));
        parserConfig.extractUserDataSei = 1;
        parserConfig.printLogs = 0;

        ret = ARSTREAM2_H264Parser_Init(&(filter->parser), &parserConfig);
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Parser_Init() failed (%d)", ret);
        }
    }

    if (ret == ARSTREAM2_OK)
    {
        ARSTREAM2_H264Writer_Config_t writerConfig;
        memset(&writerConfig, 0, sizeof(writerConfig));
        writerConfig.naluPrefix = 1;

        ret = ARSTREAM2_H264Writer_Init(&(filter->writer), &writerConfig);
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Writer_Init() failed (%d)", ret);
        }
    }

    if (ret == ARSTREAM2_OK)
    {
        filter->tempSliceNaluBuffer = malloc(ARSTREAM2_H264_FILTER_TEMP_SLICE_NALU_BUFFER_SIZE);
        if (!filter->tempSliceNaluBuffer)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Allocation failed (size %d)", ARSTREAM2_H264_FILTER_TEMP_SLICE_NALU_BUFFER_SIZE);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
        filter->tempSliceNaluBufferSize = ARSTREAM2_H264_FILTER_TEMP_SLICE_NALU_BUFFER_SIZE;
    }

    if (ret == ARSTREAM2_OK)
    {
        filter->currentAuUserData = malloc(ARSTREAM2_H264_FILTER_USER_DATA_BUFFER_SIZE);
        if (!filter->currentAuUserData)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Allocation failed (size %d)", ARSTREAM2_H264_FILTER_USER_DATA_BUFFER_SIZE);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
    }

#ifdef ARSTREAM2_H264_FILTER_STREAM_OUTPUT
    if (ret == ARSTREAM2_OK)
    {
        int i;
        char szOutputFileName[128];
        char *pszFilePath = NULL;
        szOutputFileName[0] = '\0';
        if (0)
        {
        }
#ifdef ARSTREAM2_H264_FILTER_STREAM_OUTPUT_ALLOW_NAP_USB
        else if ((access(ARSTREAM2_H264_FILTER_STREAM_OUTPUT_PATH_NAP_USB, F_OK) == 0) && (access(ARSTREAM2_H264_FILTER_STREAM_OUTPUT_PATH_NAP_USB, W_OK) == 0))
        {
            pszFilePath = ARSTREAM2_H264_FILTER_STREAM_OUTPUT_PATH_NAP_USB;
        }
#endif
#ifdef ARSTREAM2_H264_FILTER_STREAM_OUTPUT_ALLOW_NAP_INTERNAL
        else if ((access(ARSTREAM2_H264_FILTER_STREAM_OUTPUT_PATH_NAP_INTERNAL, F_OK) == 0) && (access(ARSTREAM2_H264_FILTER_STREAM_OUTPUT_PATH_NAP_INTERNAL, W_OK) == 0))
        {
            pszFilePath = ARSTREAM2_H264_FILTER_STREAM_OUTPUT_PATH_NAP_INTERNAL;
        }
#endif
#ifdef ARSTREAM2_H264_FILTER_STREAM_OUTPUT_ALLOW_ANDROID_INTERNAL
        else if ((access(ARSTREAM2_H264_FILTER_STREAM_OUTPUT_PATH_ANDROID_INTERNAL, F_OK) == 0) && (access(ARSTREAM2_H264_FILTER_STREAM_OUTPUT_PATH_ANDROID_INTERNAL, W_OK) == 0))
        {
            pszFilePath = ARSTREAM2_H264_FILTER_STREAM_OUTPUT_PATH_ANDROID_INTERNAL;
        }
#endif
#ifdef ARSTREAM2_H264_FILTER_STREAM_OUTPUT_ALLOW_PCLINUX
        else if ((access(ARSTREAM2_H264_FILTER_STREAM_OUTPUT_PATH_PCLINUX, F_OK) == 0) && (access(ARSTREAM2_H264_FILTER_STREAM_OUTPUT_PATH_PCLINUX, W_OK) == 0))
        {
            pszFilePath = ARSTREAM2_H264_FILTER_STREAM_OUTPUT_PATH_PCLINUX;
        }
#endif
        if (pszFilePath)
        {
            for (i = 0; i < 1000; i++)
            {
                snprintf(szOutputFileName, 128, "%s/%s_%03d.264", pszFilePath, ARSTREAM2_H264_FILTER_STREAM_OUTPUT_FILENAME, i);
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
            filter->fStreamOut = fopen(szOutputFileName, "w");
            if (!filter->fStreamOut)
            {
                ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "Unable to open stream output file '%s'", szOutputFileName);
            }
        }
    }
#endif //#ifdef ARSTREAM2_H264_FILTER_STREAM_OUTPUT

    if (ret == ARSTREAM2_OK)
    {
        *filterHandle = (ARSTREAM2_H264Filter_Handle*)filter;
    }
    else
    {
        if (filter)
        {
            if (mutexWasInit) ARSAL_Mutex_Destroy(&(filter->mutex));
            if (startCondWasInit) ARSAL_Cond_Destroy(&(filter->startCond));
            if (callbackCondWasInit) ARSAL_Cond_Destroy(&(filter->callbackCond));
            if (filter->parser) ARSTREAM2_H264Parser_Free(filter->parser);
            if (filter->tempAuBuffer) free(filter->tempAuBuffer);
            if (filter->tempSliceNaluBuffer) free(filter->tempSliceNaluBuffer);
            if (filter->currentAuUserData) free(filter->currentAuUserData);
            if (filter->writer) ARSTREAM2_H264Writer_Free(filter->writer);
#ifdef ARSTREAM2_H264_FILTER_STREAM_OUTPUT
            if (filter->fStreamOut) fclose(filter->fStreamOut);
#endif
            free(filter);
        }
        *filterHandle = NULL;
    }

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_H264Filter_Free(ARSTREAM2_H264Filter_Handle *filterHandle)
{
    ARSTREAM2_H264Filter_t* filter;
    eARSTREAM2_ERROR ret = ARSTREAM2_ERROR_BUSY, canDelete = 0;

    if ((!filterHandle) || (!*filterHandle))
    {
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    filter = (ARSTREAM2_H264Filter_t*)*filterHandle;

    ARSAL_Mutex_Lock(&(filter->mutex));
    if (filter->threadStarted == 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_H264_FILTER_TAG, "All threads are stopped");
        canDelete = 1;
    }

    if (canDelete == 1)
    {
        ARSAL_Mutex_Destroy(&(filter->mutex));
        ARSAL_Cond_Destroy(&(filter->startCond));
        ARSAL_Cond_Destroy(&(filter->callbackCond));
        ARSTREAM2_H264Parser_Free(filter->parser);
        ARSTREAM2_H264Writer_Free(filter->writer);

        if (filter->pSps) free(filter->pSps);
        if (filter->pPps) free(filter->pPps);
        if (filter->tempAuBuffer) free(filter->tempAuBuffer);
        if (filter->tempSliceNaluBuffer) free(filter->tempSliceNaluBuffer);
        if (filter->currentAuUserData) free(filter->currentAuUserData);
#ifdef ARSTREAM2_H264_FILTER_STREAM_OUTPUT
        if (filter->fStreamOut) fclose(filter->fStreamOut);
#endif

        free(filter);
        *filterHandle = NULL;
        ret = ARSTREAM2_OK;
    }
    else
    {
        ARSAL_Mutex_Unlock(&(filter->mutex));
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Call ARSTREAM2_H264Filter_Stop before calling this function");
    }

    return ret;
}
