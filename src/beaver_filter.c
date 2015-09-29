/**
 * @file beaver_filter.c
 * @brief H.264 Elementary Stream Tools Library - Filter
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include <libARSAL/ARSAL_Print.h>
#include <libARSAL/ARSAL_Mutex.h>
#include <libARStream/ARSTREAM_Reader2.h>

#include <libBeaver/beaver_filter.h>
#include <libBeaver/beaver_parser.h>
#include <libBeaver/beaver_writer.h>
#include <libBeaver/beaver_parrot.h>

#include "beaver_h264.h"

/* DEBUG */
//#include <locale.h>
/* /DEBUG */


#define BEAVER_FILTER_TAG "BEAVER_Filter"

#define BEAVER_FILTER_TEMP_AU_BUFFER_SIZE (1024 * 1024)
#define BEAVER_FILTER_TEMP_SLICE_NALU_BUFFER_SIZE (64 * 1024)

#define BEAVER_FILTER_MONITORING_OUTPUT
#ifdef BEAVER_FILTER_MONITORING_OUTPUT
    #include <stdio.h>

    #define BEAVER_FILTER_MONITORING_OUTPUT_ALLOW_NAP_USB
    #define BEAVER_FILTER_MONITORING_OUTPUT_PATH_NAP_USB "/tmp/mnt/STREAMDEBUG/frameinfo"
    //#define BEAVER_FILTER_MONITORING_OUTPUT_ALLOW_NAP_INTERNAL
    #define BEAVER_FILTER_MONITORING_OUTPUT_PATH_NAP_INTERNAL "/data/skycontroller/frameinfo"
    #define BEAVER_FILTER_MONITORING_OUTPUT_ALLOW_ANDROID_INTERNAL
    #define BEAVER_FILTER_MONITORING_OUTPUT_PATH_ANDROID_INTERNAL "/storage/emulated/legacy/FF/frameinfo"
    #define BEAVER_FILTER_MONITORING_OUTPUT_ALLOW_PCLINUX
    #define BEAVER_FILTER_MONITORING_OUTPUT_PATH_PCLINUX "./frameinfo"

    #define BEAVER_FILTER_MONITORING_OUTPUT_FILENAME "beaver_frameinfo"
#endif


typedef enum
{
    BEAVER_FILTER_H264_NALU_TYPE_UNKNOWN = 0,
    BEAVER_FILTER_H264_NALU_TYPE_SLICE = 1,
    BEAVER_FILTER_H264_NALU_TYPE_SLICE_IDR = 5,
    BEAVER_FILTER_H264_NALU_TYPE_SEI = 6,
    BEAVER_FILTER_H264_NALU_TYPE_SPS = 7,
    BEAVER_FILTER_H264_NALU_TYPE_PPS = 8,
    BEAVER_FILTER_H264_NALU_TYPE_AUD = 9,
    BEAVER_FILTER_H264_NALU_TYPE_FILLER_DATA = 12,

} BEAVER_Filter_H264NaluType_t;


typedef enum
{
    BEAVER_FILTER_H264_SLICE_TYPE_NON_VCL = 0,
    BEAVER_FILTER_H264_SLICE_TYPE_I,
    BEAVER_FILTER_H264_SLICE_TYPE_P,

} BEAVER_Filter_H264SliceType_t;


typedef struct BEAVER_Filter_Au_s
{
    uint8_t *buffer;
    uint32_t size;
    uint64_t timestamp;
    uint64_t timestampShifted;
    uint64_t firstNaluInputTime;
    BEAVER_Filter_AuSyncType_t syncType;
    BEAVER_Parrot_DragonFrameInfoV1_t frameInfo;
    void *userPtr;

    struct BEAVER_Filter_Au_s* prev;
    struct BEAVER_Filter_Au_s* next;
    int used;

} BEAVER_Filter_Au_t;


typedef struct BEAVER_Filter_s
{
    BEAVER_Filter_SpsPpsCallback_t spsPpsCallback;
    void* spsPpsCallbackUserPtr;
    BEAVER_Filter_GetAuBufferCallback_t getAuBufferCallback;
    void* getAuBufferCallbackUserPtr;
    BEAVER_Filter_AuReadyCallback_t auReadyCallback;
    void* auReadyCallbackUserPtr;
    int callbackInProgress;

    int auFifoSize;
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
    BEAVER_Filter_AuSyncType_t currentAuSyncType;
    int currentAuSlicesAllI;
    int currentAuStreamingInfoAvailable;
    int currentAuFrameInfoAvailable;
    BEAVER_Parrot_DragonStreamingV1_t currentAuStreamingInfo;
    BEAVER_Parrot_DragonFrameInfoV1_t currentAuFrameInfo;
    uint16_t currentAuStreamingSliceMbCount[BEAVER_PARROT_DRAGON_MAX_SLICE_COUNT];
    int currentAuPreviousSliceIndex;
    int currentAuPreviousSliceFirstMb;
    int currentAuCurrentSliceFirstMb;

    BEAVER_Parser_Handle parser;
    BEAVER_Writer_Handle writer;
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

    /* Access unit FIFO */
    ARSAL_Mutex_t fifoMutex;
    ARSAL_Cond_t fifoCond;
    int fifoCount;
    BEAVER_Filter_Au_t *fifoHead;
    BEAVER_Filter_Au_t *fifoTail;
    BEAVER_Filter_Au_t *fifoPool;

#ifdef BEAVER_FILTER_MONITORING_OUTPUT
    FILE* fMonitorOut;
#endif

/* DEBUG */
    //FILE *fDebug;
/* /DEBUG */

} BEAVER_Filter_t;


static int BEAVER_Filter_enqueueAu(BEAVER_Filter_t *filter, const BEAVER_Filter_Au_t *au)
{
    int i;
    BEAVER_Filter_Au_t *cur = NULL;

    if ((!filter) || (!au))
    {
        return -1;
    }

    ARSAL_Mutex_Lock(&(filter->fifoMutex));

    if (filter->fifoCount >= filter->auFifoSize)
    {
        ARSAL_Mutex_Unlock(&(filter->fifoMutex));
        ARSAL_Cond_Signal(&(filter->fifoCond));
        return -2;
    }

    for (i = 0; i < filter->auFifoSize; i++)
    {
        if (!filter->fifoPool[i].used)
        {
            cur = &filter->fifoPool[i];
            memcpy(cur, au, sizeof(BEAVER_Filter_Au_t));
            cur->prev = NULL;
            cur->next = NULL;
            cur->used = 1;
            break;
        }
    }

    if (!cur)
    {
        ARSAL_Mutex_Unlock(&(filter->fifoMutex));
        ARSAL_Cond_Signal(&(filter->fifoCond));
        return -3;
    }

    filter->fifoCount++;
    if (filter->fifoTail)
    {
        filter->fifoTail->next = cur;
        cur->prev = filter->fifoTail;
    }
    filter->fifoTail = cur; 
    if (!filter->fifoHead)
    {
        filter->fifoHead = cur;
    }

    ARSAL_Mutex_Unlock(&(filter->fifoMutex));
    ARSAL_Cond_Signal(&(filter->fifoCond));

    return 0;
}


static int BEAVER_Filter_dequeueAu(BEAVER_Filter_t *filter, BEAVER_Filter_Au_t *au)
{
    BEAVER_Filter_Au_t* cur = NULL;

    if ((!filter) || (!au))
    {
        return -1;
    }

    ARSAL_Mutex_Lock(&(filter->fifoMutex));

    if ((!filter->fifoHead) || (!filter->fifoCount))
    {
        ARSAL_Mutex_Unlock(&filter->fifoMutex);
        return -2;
    }

    cur = filter->fifoHead;
    if (cur->next)
    {
        cur->next->prev = NULL;
        filter->fifoHead = cur->next;
        filter->fifoCount--;
    }
    else
    {
        filter->fifoHead = NULL;
        filter->fifoCount = 0;
        filter->fifoTail = NULL;
    }

    cur->used = 0;
    cur->prev = NULL;
    cur->next = NULL;
    memcpy(au, cur, sizeof(BEAVER_Filter_Au_t));

    ARSAL_Mutex_Unlock (&(filter->fifoMutex));

    return 0;
}


static int BEAVER_Filter_flushAuFifo(BEAVER_Filter_t *filter)
{
    int fifoRes;
    BEAVER_Filter_Au_t au;

    if (!filter)
    {
        return -1;
    }

    while ((fifoRes = BEAVER_Filter_dequeueAu(filter, &au)) == 0)
    {
        /* TODO: recycle the AU buffer */
    }

    return 0;
}


static int BEAVER_Filter_sync(BEAVER_Filter_t *filter, uint8_t *naluBuffer, int naluSize)
{
    int ret = 0;
    BEAVER_H264_SpsContext_t *spsContext = NULL;
    BEAVER_H264_PpsContext_t *ppsContext = NULL;

    filter->sync = 1;

    if (filter->generateFirstGrayIFrame)
    {
        filter->firstGrayIFramePending = 1;
    }
    ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "SPS/PPS sync OK"); //TODO: debug
/* DEBUG */
    //fprintf(filter->fDebug, "SPS/PPS sync OK\n");
    //fflush(filter->fDebug);
/* /DEBUG */

    /* SPS/PPS callback */
    if (filter->spsPpsCallback)
    {
        filter->callbackInProgress = 1;
        ARSAL_Mutex_Unlock(&(filter->mutex));
        int cbRet = filter->spsPpsCallback(filter->pSps, filter->spsSize, filter->pPps, filter->ppsSize, filter->spsPpsCallbackUserPtr);
        ARSAL_Mutex_Lock(&(filter->mutex));
        if (cbRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "spsPpsCallback failed (returned %d)", cbRet);
        }
    }

    /* Configure the writer */
    ret = BEAVER_Parser_GetSpsPpsContext(filter->parser, (void**)&spsContext, (void**)&ppsContext);
    if (ret != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "BEAVER_Parser_GetSpsPpsContext() failed (%d)", ret);
        ret = -1;
    }
    if (ret == 0)
    {
        filter->mbWidth = spsContext->pic_width_in_mbs_minus1 + 1;
        filter->mbHeight = (spsContext->pic_height_in_map_units_minus1 + 1) * ((spsContext->frame_mbs_only_flag) ? 1 : 2);
        ret = BEAVER_Writer_SetSpsPpsContext(filter->writer, (void*)spsContext, (void*)ppsContext);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "BEAVER_Parser_GetSpsPpsContext() failed (%d)", ret);
            ret = -1;
        }
    }

    if (filter->waitForSync)
    {
        filter->callbackInProgress = 1;
        ARSAL_Mutex_Unlock(&(filter->mutex));
        int cbRet = filter->getAuBufferCallback(&filter->currentAuBuffer, &filter->currentAuBufferSize, &filter->currentAuBufferUserPtr, filter->getAuBufferCallbackUserPtr);
        ARSAL_Mutex_Lock(&(filter->mutex));
        if ((cbRet != 0) || (!filter->currentAuBuffer) || (!filter->currentAuBufferSize))
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "getAuBufferCallback failed (returned %d)", cbRet);
            filter->currentAuBuffer = NULL;
            filter->currentAuBufferSize = 0;
            ret = -1;
        }
        else if ((filter->currentAuSize + naluSize) && (filter->currentAuSize + naluSize <= filter->currentAuBufferSize))
        {
            memcpy(filter->currentAuBuffer, filter->tempAuBuffer, filter->currentAuSize + naluSize);
        }
    }

    return ret;
}


static int BEAVER_Filter_processNalu(BEAVER_Filter_t *filter, uint8_t *naluBuffer, int naluSize, BEAVER_Filter_H264NaluType_t *naluType)
{
    int ret = 0;
    BEAVER_Filter_H264SliceType_t sliceType = BEAVER_FILTER_H264_SLICE_TYPE_NON_VCL;

    if ((!naluBuffer) || (naluSize <= 4))
    {
        return -1;
    }

    if (filter->replaceStartCodesWithNaluSize)
    {
        // Replace the NAL unit 4 bytes start code with the NALU size
        *((uint32_t*)naluBuffer) = htonl((uint32_t)naluSize - 4);
    }

    ret = BEAVER_Parser_SetupNalu_buffer(filter->parser, naluBuffer + 4, naluSize - 4);
    if (ret < 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "BEAVER_Parser_SetupNalu_buffer() failed (%d)", ret);
    }

    if (ret >= 0)
    {
        ret = BEAVER_Parser_ParseNalu(filter->parser);
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "BEAVER_Parser_ParseNalu() failed (%d)", ret);
        }
    }

    if (ret >= 0)
    {
        BEAVER_Filter_H264NaluType_t _naluType = BEAVER_Parser_GetLastNaluType(filter->parser);
        if (naluType) *naluType = _naluType;
        switch (_naluType)
        {
            case BEAVER_FILTER_H264_NALU_TYPE_SLICE_IDR:
                filter->currentAuSyncType = BEAVER_FILTER_AU_SYNC_TYPE_IDR;
            case BEAVER_FILTER_H264_NALU_TYPE_SLICE:
                /* Slice */
                filter->currentAuCurrentSliceFirstMb = -1;
                if (filter->sync)
                {
                    BEAVER_Parser_SliceInfo_t sliceInfo;
                    memset(&sliceInfo, 0, sizeof(sliceInfo));
                    ret = BEAVER_Parser_GetSliceInfo(filter->parser, &sliceInfo);
                    if (ret < 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "BEAVER_Parser_GetSliceInfo() failed (%d)", ret);
                    }
                    else
                    {
                        if (sliceInfo.sliceTypeMod5 == 2)
                        {
                            sliceType = BEAVER_FILTER_H264_SLICE_TYPE_I;
                        }
                        else if (sliceInfo.sliceTypeMod5 == 0)
                        {
                            sliceType = BEAVER_FILTER_H264_SLICE_TYPE_P;
                            filter->currentAuSlicesAllI = 0;
                        }
                        filter->currentAuCurrentSliceFirstMb = sliceInfo.first_mb_in_slice;
                    }
                }
                break;
            case BEAVER_FILTER_H264_NALU_TYPE_SEI:
                /* SEI */
                if (filter->sync)
                {
                    void *pUserDataSei = NULL;
                    unsigned int userDataSeiSize = 0;
                    BEAVER_Parrot_UserDataSeiTypes_t userDataSeiType;
                    ret = BEAVER_Parser_GetUserDataSei(filter->parser, &pUserDataSei, &userDataSeiSize);
                    if (ret < 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "BEAVER_Parser_GetUserDataSei() failed (%d)", ret);
                    }
                    else
                    {
                        userDataSeiType = BEAVER_Parrot_GetUserDataSeiType(pUserDataSei, userDataSeiSize);
                        switch (userDataSeiType)
                        {
                            case BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_V1:
                                ret = BEAVER_Parrot_DeserializeDragonStreamingV1(pUserDataSei, userDataSeiSize, &filter->currentAuStreamingInfo, filter->currentAuStreamingSliceMbCount);
                                if (ret < 0)
                                {
                                    ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "BEAVER_Parrot_DeserializeDragonStreamingV1() failed (%d)", ret);
                                }
                                else
                                {
                                    filter->currentAuStreamingInfoAvailable = 1;
                                }
                                break;
                            case BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1:
                                ret = BEAVER_Parrot_DeserializeUserDataSeiDragonStreamingFrameInfoV1(pUserDataSei, userDataSeiSize, &filter->currentAuFrameInfo,
                                                                                                     &filter->currentAuStreamingInfo, filter->currentAuStreamingSliceMbCount);
                                if (ret < 0)
                                {
                                    ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "BEAVER_Parrot_DeserializeUserDataSeiDragonStreamingFrameInfoV1() failed (%d)", ret);
                                }
                                else
                                {
                                    filter->currentAuStreamingInfoAvailable = 1;
                                    filter->currentAuFrameInfoAvailable = 1;
                                }
                                break;
                            default:
                                break;
                        }
                    }
                }
                break;
            case BEAVER_FILTER_H264_NALU_TYPE_SPS:
                /* SPS */
                if (!filter->spsSync)
                {
                    filter->pSps = malloc(naluSize);
                    if (!filter->pSps)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Allocation failed for SPS (size %d)", naluSize);
                    }
                    else
                    {
                        memcpy(filter->pSps, naluBuffer, naluSize);
                        filter->spsSize = naluSize;
                        filter->spsSync = 1;
                    }
                }
                break;
            case BEAVER_FILTER_H264_NALU_TYPE_PPS:
                /* PPS */
                if (!filter->ppsSync)
                {
                    filter->pPps = malloc(naluSize);
                    if (!filter->pPps)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Allocation failed for PPS (size %d)", naluSize);
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
        ret = BEAVER_Filter_sync(filter, naluBuffer, naluSize);
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "BEAVER_Filter_Sync() failed (%d)", ret);
        }
    }

    filter->callbackInProgress = 0;
    ARSAL_Mutex_Unlock(&(filter->mutex));
    ARSAL_Cond_Signal(&(filter->callbackCond));

    return (ret >= 0) ? 0 : ret;
}


static int BEAVER_Filter_getNewAuBuffer(BEAVER_Filter_t *filter)
{
    int ret = 0;

    ARSAL_Mutex_Lock(&(filter->mutex));

    if ((!filter->running) || (filter->auBufferChangePending) || ((filter->waitForSync) && (!filter->sync)))
    {
        filter->currentAuBuffer = filter->tempAuBuffer;
        filter->currentAuBufferSize = filter->tempAuBufferSize;
        filter->auBufferChangePending = 0;
/* DEBUG */
        //fprintf(filter->fDebug, "getNewAuBuffer: temp buffer\n");
        //fflush(filter->fDebug);
/* /DEBUG */
    }
    else
    {
        filter->callbackInProgress = 1;
        ARSAL_Mutex_Unlock(&(filter->mutex));
        int cbRet = filter->getAuBufferCallback(&filter->currentAuBuffer, &filter->currentAuBufferSize, &filter->currentAuBufferUserPtr, filter->getAuBufferCallbackUserPtr);
        ARSAL_Mutex_Lock(&(filter->mutex));
        if ((cbRet != 0) || (!filter->currentAuBuffer) || (!filter->currentAuBufferSize))
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "getAuBufferCallback failed (returned %d)", cbRet);
            filter->currentAuBuffer = NULL;
            filter->currentAuBufferSize = 0;
            ret = -1;
        }
/* DEBUG */
        //fprintf(filter->fDebug, "getNewAuBuffer: user buffer\n");
        //fflush(filter->fDebug);
/* /DEBUG */
    }

    filter->callbackInProgress = 0;
    ARSAL_Mutex_Unlock(&(filter->mutex));
    ARSAL_Cond_Signal(&(filter->callbackCond));

    return ret;
}


static void BEAVER_Filter_resetCurrentAu(BEAVER_Filter_t *filter)
{
    filter->currentAuSize = 0;
    filter->currentAuIncomplete = 0;
    filter->currentAuSyncType = BEAVER_FILTER_AU_SYNC_TYPE_NONE;
    filter->currentAuSlicesAllI = 1;
    filter->currentAuStreamingInfoAvailable = 0;
    filter->currentAuFrameInfoAvailable = 0;
    filter->currentAuPreviousSliceIndex = -1;
    filter->currentAuPreviousSliceFirstMb = 0;
    filter->currentAuCurrentSliceFirstMb = -1;
    filter->currentAuFirstNaluInputTime = 0;
}


static void BEAVER_Filter_updateCurrentAu(BEAVER_Filter_t *filter, BEAVER_Filter_H264NaluType_t naluType)
{
    if (((naluType == BEAVER_FILTER_H264_NALU_TYPE_SLICE_IDR) || (naluType == BEAVER_FILTER_H264_NALU_TYPE_SLICE)) && (filter->currentAuStreamingInfoAvailable) && (filter->currentAuStreamingInfo.sliceCount <= BEAVER_PARROT_DRAGON_MAX_SLICE_COUNT))
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
        //ARSAL_PRINT(ARSAL_PRINT_DEBUG, BEAVER_FILTER_TAG, "previousSliceIndex: %d - previousSliceFirstMb: %d", filter->currentAuPreviousSliceIndex, filter->currentAuCurrentSliceFirstMb); //TODO: debug
/* DEBUG */
        //fprintf(filter->fDebug, "updateCurrentAu: previousSliceIndex: %d - previousSliceFirstMb: %d\n", filter->currentAuPreviousSliceIndex, filter->currentAuCurrentSliceFirstMb);
        //fflush(filter->fDebug);
/* /DEBUG */
    }
}


static void BEAVER_Filter_addNaluToCurrentAu(BEAVER_Filter_t *filter, BEAVER_Filter_H264NaluType_t naluType, int naluSize)
{
    int filterOut = 0;

    if ((filter->filterOutSpsPps) && ((naluType == BEAVER_FILTER_H264_NALU_TYPE_SPS) || (naluType == BEAVER_FILTER_H264_NALU_TYPE_PPS)))
    {
        filterOut = 1;
    }

    if ((filter->filterOutSei) && (naluType == BEAVER_FILTER_H264_NALU_TYPE_SEI))
    {
        filterOut = 1;
    }

    if (!filterOut)
    {
        filter->currentAuSize += naluSize;
    }
}


static int BEAVER_Filter_enqueueCurrentAu(BEAVER_Filter_t *filter)
{
    int ret = 0;
    int cancelAuOutput = 0;

    if (filter->currentAuSyncType != BEAVER_FILTER_AU_SYNC_TYPE_IDR)
    {
        if (filter->currentAuSlicesAllI)
        {
            filter->currentAuSyncType = BEAVER_FILTER_AU_SYNC_TYPE_IFRAME;
        }
        else if ((filter->currentAuStreamingInfoAvailable) && (filter->currentAuStreamingInfo.indexInGop == 0))
        {
            filter->currentAuSyncType = BEAVER_FILTER_AU_SYNC_TYPE_PIR_START;
        }
    }

    ARSAL_Mutex_Lock(&(filter->mutex));

    if ((filter->waitForSync) && (!filter->sync))
    {
        cancelAuOutput = 1;
        if (filter->running)
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "AU output cancelled (waitForSync)"); //TODO: debug
/* DEBUG */
            //fprintf(filter->fDebug, "AU output cancelled (waitForSync)\n");
            //fflush(filter->fDebug);
/* /DEBUG */
        }
    }

    ARSAL_Mutex_Unlock(&(filter->mutex));

    if ((!filter->outputIncompleteAu) && (filter->currentAuIncomplete))
    {
        cancelAuOutput = 1;
        ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "AU output cancelled (!outputIncompleteAu)"); //TODO: debug
/* DEBUG */
        //fprintf(filter->fDebug, "AU output cancelled (!outputIncompleteAu)\n");
        //fflush(filter->fDebug);
/* /DEBUG */
    }

    if (!cancelAuOutput)
    {
        BEAVER_Filter_Au_t au;
        memset(&au, 0, sizeof(au));
        au.buffer = filter->currentAuBuffer;
        au.size = filter->currentAuSize;
        au.timestamp = filter->currentAuTimestamp;
        au.timestampShifted = filter->currentAuTimestampShifted;
        au.firstNaluInputTime = filter->currentAuFirstNaluInputTime;
        au.syncType = filter->currentAuSyncType;
        au.userPtr = filter->currentAuBufferUserPtr;
        if (filter->currentAuFrameInfoAvailable)
        {
            memcpy(&au.frameInfo, &filter->currentAuFrameInfo, sizeof(BEAVER_Parrot_DragonFrameInfoV1_t));
        }
        else
        {
            memset(&au.frameInfo, 0, sizeof(BEAVER_Parrot_DragonFrameInfoV1_t));
        }
/* DEBUG */
        //fprintf(filter->fDebug, "Trying to enqueue AU: size=%d ts=%llu\n", filter->currentAuSize, (unsigned long long int)filter->currentAuTimestamp);
        //fflush(filter->fDebug);
/* /DEBUG */
        int fifoRes = BEAVER_Filter_enqueueAu(filter, &au);
        if (fifoRes == 0)
        {
            ret = 1;
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "Failed to enqueue the current AU (fifoRes = %d)", fifoRes);
        }
/* DEBUG */
        //fprintf(filter->fDebug, "AU enqueued (fifoRes=%d): size=%d ts=%llu\n", fifoRes, filter->currentAuSize, (unsigned long long int)filter->currentAuTimestamp);
        //fflush(filter->fDebug);
/* /DEBUG */
    }

    return ret;
}


static int BEAVER_Filter_generateGrayIFrame(BEAVER_Filter_t *filter, uint8_t *naluBuffer, int naluSize, BEAVER_Filter_H264NaluType_t naluType)
{
    int ret = 0;
    BEAVER_H264_SliceContext_t *sliceContextNext = NULL;
    BEAVER_H264_SliceContext_t sliceContext;

    if ((naluType != BEAVER_FILTER_H264_NALU_TYPE_SLICE_IDR) && (naluType != BEAVER_FILTER_H264_NALU_TYPE_SLICE))
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "Waiting for a slice to generate gray I-frame"); //TODO: debug
        return -2;
    }

    ret = BEAVER_Parser_GetSliceContext(filter->parser, (void**)&sliceContextNext);
    if (ret < 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "BEAVER_Parser_GetSliceContext() failed (%d)", ret);
        ret = -1;
    }
    memcpy(&sliceContext, sliceContextNext, sizeof(sliceContext));

    if (ret == 0)
    {
        unsigned int outputSize, mbCount;

        mbCount = filter->mbWidth * filter->mbHeight;
        sliceContext.nal_ref_idc = 3;
        sliceContext.nal_unit_type = BEAVER_H264_NALU_TYPE_SLICE_IDR;
        sliceContext.idrPicFlag = 1;
        sliceContext.slice_type = BEAVER_H264_SLICE_TYPE_I;
        sliceContext.frame_num = 0;
        sliceContext.idr_pic_id = 0;
        sliceContext.no_output_of_prior_pics_flag = 0;
        sliceContext.long_term_reference_flag = 0;

        ret = BEAVER_Writer_WriteGrayISliceNalu(filter->writer, 0, mbCount, (void*)&sliceContext, filter->tempSliceNaluBuffer, filter->tempSliceNaluBufferSize, &outputSize);
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "BEAVER_Writer_WriteGrayISliceNalu() failed (%d)", ret);
            ret = -1;
        }
        else
        {
            uint8_t *tmpBuf = NULL;
            int savedAuSize = filter->currentAuSize;
            int savedAuIncomplete = filter->currentAuIncomplete;
            BEAVER_Filter_AuSyncType_t savedAuSyncType = filter->currentAuSyncType;
            int savedAuSlicesAllI = filter->currentAuSlicesAllI;
            int savedAuStreamingInfoAvailable = filter->currentAuStreamingInfoAvailable;
            int savedAuFrameInfoAvailable = filter->currentAuFrameInfoAvailable;
            int savedAuPreviousSliceIndex = filter->currentAuPreviousSliceIndex;
            int savedAuPreviousSliceFirstMb = filter->currentAuPreviousSliceFirstMb;
            int savedAuCurrentSliceFirstMb = filter->currentAuCurrentSliceFirstMb;
            uint64_t savedAuTimestamp = filter->currentAuTimestamp;
            uint64_t savedAuTimestampShifted = filter->currentAuTimestampShifted;
            uint64_t savedAuFirstNaluInputTime = filter->currentAuFirstNaluInputTime;
            ret = 0;

            ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "Gray I slice NALU output size: %d", outputSize); //TODO: debug
/* DEBUG */
            //fprintf(filter->fDebug, "Gray I slice NALU output size: %d\n", outputSize);
            //fflush(filter->fDebug);
/* /DEBUG */

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

            BEAVER_Filter_resetCurrentAu(filter);
            filter->currentAuSyncType = BEAVER_FILTER_AU_SYNC_TYPE_IDR;
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
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Access unit buffer is too small for the SPS NALU (size %d, access unit size %s)", filter->spsSize, filter->currentAuSize);
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
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Access unit buffer is too small for the PPS NALU (size %d, access unit size %s)", filter->ppsSize, filter->currentAuSize);
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
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Access unit buffer is too small for the I-frame (size %d, access unit size %s)", outputSize, filter->currentAuSize);
                    ret = -1;
                }
            }

            // Output access unit
            if (ret == 0)
            {
                ret = BEAVER_Filter_enqueueCurrentAu(filter);

                if ((ret > 0) || (filter->auBufferChangePending))
                {
                    // The access unit has been enqueued or a buffer change is pending
                    ret = BEAVER_Filter_getNewAuBuffer(filter);
                    if (ret == 0)
                    {
                        BEAVER_Filter_resetCurrentAu(filter);

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
                        filter->currentAuFrameInfoAvailable = savedAuFrameInfoAvailable;
                        filter->currentAuPreviousSliceIndex = savedAuPreviousSliceIndex;
                        filter->currentAuPreviousSliceFirstMb = savedAuPreviousSliceFirstMb;
                        filter->currentAuCurrentSliceFirstMb = savedAuCurrentSliceFirstMb;

                        filter->currentNaluBuffer = filter->currentAuBuffer + filter->currentAuSize;
                        filter->currentNaluBufferSize = filter->currentAuBufferSize - filter->currentAuSize;
                    }
                    else
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "BEAVER_Filter_getNewAuBuffer() failed (%d)", ret);
                        ret = -1;
                    }
                }
                else
                {
                    // The access unit has not been enqueued: reuse current auBuffer
                    BEAVER_Filter_resetCurrentAu(filter);
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
                        filter->currentAuFrameInfoAvailable = savedAuFrameInfoAvailable;
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


static int BEAVER_Filter_fillMissingSlices(BEAVER_Filter_t *filter, uint8_t *naluBuffer, int naluSize, BEAVER_Filter_H264NaluType_t naluType, int isFirstNaluInAu)
{
    int missingMb = 0, firstMbInSlice = 0, ret = 0;

    if (isFirstNaluInAu)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "Missing NALU is probably on previous AU => OK"); //TODO: debug
/* DEBUG */
        //fprintf(filter->fDebug, "Missing NALU is probably on previous AU => OK\n");
        //fflush(filter->fDebug);
/* /DEBUG */
        if (filter->currentAuCurrentSliceFirstMb == 0)
        {
            filter->currentAuPreviousSliceFirstMb = 0;
            filter->currentAuPreviousSliceIndex = 0;
        }
        return 0;
    }
    else if (((naluType != BEAVER_FILTER_H264_NALU_TYPE_SLICE_IDR) && (naluType != BEAVER_FILTER_H264_NALU_TYPE_SLICE)) || (filter->currentAuCurrentSliceFirstMb == 0))
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "Missing NALU is probably a SPS, PPS or SEI or on previous AU => OK"); //TODO: debug
/* DEBUG */
        //fprintf(filter->fDebug, "Missing NALU is probably a SPS, PPS or SEI or on previous AU => OK\n");
        //fflush(filter->fDebug);
/* /DEBUG */
        if (filter->currentAuCurrentSliceFirstMb == 0)
        {
            filter->currentAuPreviousSliceFirstMb = 0;
            filter->currentAuPreviousSliceIndex = 0;
        }
        return 0;
    }
    else if (!filter->generateSkippedPSlices)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "Missing NALU is probably a slice"); //TODO: debug
/* DEBUG */
        //fprintf(filter->fDebug, "Missing NALU is probably a slice\n");
        //fflush(filter->fDebug);
/* /DEBUG */
        return -2;
    }

    ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "Missing NALU is probably a slice"); //TODO: debug
/* DEBUG */
    //fprintf(filter->fDebug, "Missing NALU is probably a slice\n");
    //fflush(filter->fDebug);
/* /DEBUG */

    if (!filter->sync)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "No sync, abort"); //TODO: debug
/* DEBUG */
        //fprintf(filter->fDebug, "No sync, abort\n");
        //fflush(filter->fDebug);
/* /DEBUG */
        return -2;
    }

    if ((!filter->currentAuStreamingInfoAvailable) && (filter->currentAuPreviousSliceIndex >= 0))
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "Streaming info is not available"); //TODO: debug
/* DEBUG */
        //fprintf(filter->fDebug, "Streaming info is not available\n");
        //fflush(filter->fDebug);
/* /DEBUG */
        return -2;
    }

    if (filter->currentAuPreviousSliceIndex < 0)
    {
        // No previous slice received
        if (filter->currentAuCurrentSliceFirstMb > 0)
        {
            firstMbInSlice = 0;
            missingMb = filter->currentAuCurrentSliceFirstMb;
            ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "currentSliceFirstMb: %d - missingMb: %d",
                        filter->currentAuCurrentSliceFirstMb, missingMb); //TODO: debug
/* DEBUG */
            //fprintf(filter->fDebug, "currentSliceFirstMb: %d - missingMb: %d\n", filter->currentAuCurrentSliceFirstMb, missingMb);
            //fflush(filter->fDebug);
/* /DEBUG */
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "Error: previousSliceIdx: %d - currentSliceFirstMb: %d - this should not happen!",
                        filter->currentAuPreviousSliceIndex, filter->currentAuCurrentSliceFirstMb); //TODO: debug
/* DEBUG */
            //fprintf(filter->fDebug, "Error: previousSliceIdx: %d - currentSliceFirstMb: %d - this should not happen!\n", filter->currentAuPreviousSliceIndex, filter->currentAuCurrentSliceFirstMb);
            //fflush(filter->fDebug);
/* /DEBUG */
        }
    }
    else if ((filter->currentAuCurrentSliceFirstMb > filter->currentAuPreviousSliceFirstMb + filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex]))
    {
        // Slices have been received before
        firstMbInSlice = filter->currentAuPreviousSliceFirstMb + filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex];
        missingMb = filter->currentAuCurrentSliceFirstMb - firstMbInSlice;
        ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "previousSliceFirstMb: %d - previousSliceMbCount: %d - currentSliceFirstMb: %d - missingMb: %d",
                    filter->currentAuPreviousSliceFirstMb, filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex], filter->currentAuCurrentSliceFirstMb, missingMb); //TODO: debug
/* DEBUG */
            /*fprintf(filter->fDebug, "previousSliceFirstMb: %d - previousSliceMbCount: %d - currentSliceFirstMb: %d - missingMb: %d\n",
                    filter->currentAuPreviousSliceFirstMb, filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex], filter->currentAuCurrentSliceFirstMb, missingMb);
            fflush(filter->fDebug);*/
/* /DEBUG */
    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "Error: previousSliceFirstMb: %d - previousSliceMbCount: %d - currentSliceFirstMb: %d - this should not happen!",
                    filter->currentAuPreviousSliceFirstMb, filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex], filter->currentAuCurrentSliceFirstMb); //TODO: debug
/* DEBUG */
            /*fprintf(filter->fDebug, "Error: previousSliceFirstMb: %d - previousSliceMbCount: %d - currentSliceFirstMb: %d - this should not happen!\n",
                    filter->currentAuPreviousSliceFirstMb, filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex], filter->currentAuCurrentSliceFirstMb);
            fflush(filter->fDebug);*/
/* /DEBUG */
    }

    if (missingMb > 0)
    {
        void *sliceContext;
        ret = BEAVER_Parser_GetSliceContext(filter->parser, &sliceContext);
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "BEAVER_Parser_GetSliceContext() failed (%d)", ret);
            ret = -1;
        }
        if (ret == 0)
        {
            unsigned int outputSize;
            ret = BEAVER_Writer_WriteSkippedPSliceNalu(filter->writer, firstMbInSlice, missingMb, sliceContext, filter->tempSliceNaluBuffer, filter->tempSliceNaluBufferSize, &outputSize);
            if (ret < 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "BEAVER_Writer_WriteSkippedPSliceNalu() failed (%d)", ret);
                ret = -1;
            }
            else
            {
                ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "Skipped P slice NALU output size: %d", outputSize); //TODO: debug
/* DEBUG */
                //fprintf(filter->fDebug, "Skipped P slice NALU generated size=%d (firstMb=%d, mbCount=%d)\n", outputSize, firstMbInSlice, missingMb);
                //fflush(filter->fDebug);
/* /DEBUG */
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
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Access unit buffer is too small for the generated skipped P slice (size %d, access unit size %s)", outputSize, filter->currentAuSize + naluSize);
                    ret = -1;
                }
            }
        }
    }

    return ret;
}


static int BEAVER_Filter_fillMissingEndOfFrame(BEAVER_Filter_t *filter)
{
    int missingMb = 0, firstMbInSlice = 0, ret = 0;

    if (!filter->generateSkippedPSlices)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "Missing NALU is probably a slice"); //TODO: debug
/* DEBUG */
        //fprintf(filter->fDebug, "Missing NALU is probably a slice\n");
        //fflush(filter->fDebug);
/* /DEBUG */
        return -2;
    }

    ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "Missing NALU is probably a slice"); //TODO: debug
/* DEBUG */
    //fprintf(filter->fDebug, "Missing NALU is probably a slice\n");
    //fflush(filter->fDebug);
/* /DEBUG */

    if (!filter->sync)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "No sync, abort"); //TODO: debug
/* DEBUG */
        //fprintf(filter->fDebug, "No sync, abort\n");
        //fflush(filter->fDebug);
/* /DEBUG */
        return -2;
    }

    if ((!filter->currentAuStreamingInfoAvailable) && (filter->currentAuPreviousSliceIndex >= 0))
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "Streaming info is not available"); //TODO: debug
/* DEBUG */
        //fprintf(filter->fDebug, "Streaming info is not available\n");
        //fflush(filter->fDebug);
/* /DEBUG */
        return -2;
    }

    if (filter->currentAuPreviousSliceIndex < 0)
    {
        // No previous slice received
        firstMbInSlice = 0;
        missingMb = filter->mbWidth * filter->mbHeight;

        //TODO: slice context
        //UNSUPPORTED
        return -1;
    }
    else
    {
        // Slices have been received before
        firstMbInSlice = filter->currentAuPreviousSliceFirstMb + filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex];
        missingMb = filter->mbWidth * filter->mbHeight - firstMbInSlice;
    }

    if (missingMb > 0)
    {
        void *sliceContext;
        ret = BEAVER_Parser_GetSliceContext(filter->parser, &sliceContext);
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "BEAVER_Parser_GetSliceContext() failed (%d)", ret);
            ret = -1;
        }
        if (ret == 0)
        {
            unsigned int outputSize;
            ret = BEAVER_Writer_WriteSkippedPSliceNalu(filter->writer, firstMbInSlice, missingMb, sliceContext, filter->tempSliceNaluBuffer, filter->tempSliceNaluBufferSize, &outputSize);
            if (ret < 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "BEAVER_Writer_WriteSkippedPSliceNalu() failed (%d)", ret);
                ret = -1;
            }
            else
            {
                ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "Skipped P slice NALU output size: %d", outputSize); //TODO: debug
/* DEBUG */
                //fprintf(filter->fDebug, "Skipped P slice NALU generated size=%d (firstMb=%d, mbCount=%d)\n", outputSize, firstMbInSlice, missingMb);
                //fflush(filter->fDebug);
/* /DEBUG */
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
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Access unit buffer is too small for the generated skipped P slice (size %d, access unit size %s)", outputSize, filter->currentAuSize);
                    ret = -1;
                }
            }
        }
    }

    return ret;
}


uint8_t* BEAVER_Filter_ArstreamReader2NaluCallback(eARSTREAM_READER2_CAUSE cause, uint8_t *naluBuffer, int naluSize, uint64_t auTimestamp,
                                                   uint64_t auTimestampShifted, int isFirstNaluInAu, int isLastNaluInAu,
                                                   int missingPacketsBefore, int *newNaluBufferSize, void *custom)
{
    BEAVER_Filter_t* filter = (BEAVER_Filter_t*)custom;
    BEAVER_Filter_H264NaluType_t naluType = BEAVER_FILTER_H264_NALU_TYPE_UNKNOWN;
    int ret = 0;
    uint8_t *retPtr = NULL;
    uint64_t curTime;
    struct timespec t1;

    if (!filter)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Invalid filter instance");
        return retPtr;
    }

    switch (cause)
    {
        case ARSTREAM_READER2_CAUSE_NALU_COMPLETE:
            ARSAL_Time_GetTime(&t1);
            curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

            if ((!naluBuffer) || (naluSize <= 0))
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Invalid NALU buffer");
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
                ret = BEAVER_Filter_fillMissingEndOfFrame(filter);
                if (ret < 0)
                {
                    if (ret != -2)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "BEAVER_Filter_fillMissingEndOfFrame() failed (%d)", ret);
                    }
                    filter->currentAuIncomplete = 1;
                }

                // Output the access unit
                ret = BEAVER_Filter_enqueueCurrentAu(filter);
                BEAVER_Filter_resetCurrentAu(filter);

                if ((ret > 0) || (filter->auBufferChangePending))
                {
                    // The access unit has been enqueued or a buffer change is pending
                    ret = BEAVER_Filter_getNewAuBuffer(filter);
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
                                ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Failed to copy the pending NALU to the currentNaluBuffer (size=%d)", naluSize);
                            }
                        }
                    }
                    else
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "BEAVER_Filter_getNewAuBuffer() failed (%d)", ret);
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

            ret = BEAVER_Filter_processNalu(filter, naluBuffer, naluSize, &naluType);
            if (ret < 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "BEAVER_Filter_processNalu() failed (%d)", ret);
                if ((!filter->currentAuBuffer) || (filter->currentAuBufferSize <= 0))
                {
                    filter->currentNaluBuffer = NULL;
                    filter->currentNaluBufferSize = 0;
                }
            }

            /*ARSAL_PRINT(ARSAL_PRINT_DEBUG, BEAVER_FILTER_TAG, "Received NALU type=%d size=%d ts=%llu (first=%d, last=%d, missingBefore=%d)",
                        naluType, naluSize, (long long unsigned int)auTimestamp, isFirstNaluInAu, isLastNaluInAu, missingPacketsBefore);*/ //TODO: debug
/* DEBUG */
            /*fprintf(filter->fDebug, "Received NALU type=%d size=%d ts=%llu (first=%d, last=%d, missingBefore=%d)\n",
                    naluType, naluSize, (long long unsigned int)auTimestamp, isFirstNaluInAu, isLastNaluInAu, missingPacketsBefore);
            fflush(filter->fDebug);*/
/* /DEBUG */

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

                if (filter->firstGrayIFramePending)
                {
                    // Generate fake bitstream
                    ret = BEAVER_Filter_generateGrayIFrame(filter, naluBuffer, naluSize, naluType);
                    if (ret < 0)
                    {
                        if (ret != -2)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "BEAVER_Filter_generateGrayIFrame() failed (%d)", ret);
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
                        ret = BEAVER_Filter_fillMissingSlices(filter, naluBuffer, naluSize, naluType, isFirstNaluInAu);
                        if (ret < 0)
                        {
                            if (ret != -2)
                            {
                                ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "BEAVER_Filter_fillMissingSlices() failed (%d)", ret);
                            }
                            filter->currentAuIncomplete = 1;
                        }
                    }
                    BEAVER_Filter_updateCurrentAu(filter, naluType);

                    BEAVER_Filter_addNaluToCurrentAu(filter, naluType, naluSize);

                    if (isLastNaluInAu)
                    {
                        // Output the access unit
                        ret = BEAVER_Filter_enqueueCurrentAu(filter);
                        BEAVER_Filter_resetCurrentAu(filter);

                        if ((ret > 0) || (filter->auBufferChangePending))
                        {
                            // The access unit has been enqueued or a buffer change is pending
                            ret = BEAVER_Filter_getNewAuBuffer(filter);
                            if (ret == 0)
                            {
                                filter->currentNaluBuffer = filter->currentAuBuffer + filter->currentAuSize;
                                filter->currentNaluBufferSize = filter->currentAuBufferSize - filter->currentAuSize;
                            }
                            else
                            {
                                ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "BEAVER_Filter_getNewAuBuffer() failed (%d)", ret);
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
        case ARSTREAM_READER2_CAUSE_NALU_BUFFER_TOO_SMALL:
            ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "ARStream_Reader2 NALU buffer is too small, truncated AU (or maybe it's the first call)");

            ret = 1;
            if ((filter->currentAuBuffer) && (filter->currentAuSize > 0))
            {
                // Output the access unit
                ret = BEAVER_Filter_enqueueCurrentAu(filter);
            }
            BEAVER_Filter_resetCurrentAu(filter);

            if ((ret > 0) || (filter->auBufferChangePending))
            {
                // The access unit has been enqueued or no AU was pending or a buffer change is pending
                ret = BEAVER_Filter_getNewAuBuffer(filter);
                if (ret == 0)
                {
                    filter->currentNaluBuffer = filter->currentAuBuffer + filter->currentAuSize;
                    filter->currentNaluBufferSize = filter->currentAuBufferSize - filter->currentAuSize;
                }
                else
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "BEAVER_Filter_getNewAuBuffer() failed (%d)", ret);
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
        case ARSTREAM_READER2_CAUSE_NALU_COPY_COMPLETE:
            *newNaluBufferSize = filter->currentNaluBufferSize;
            retPtr = filter->currentNaluBuffer;
            break;
        case ARSTREAM_READER2_CAUSE_CANCEL:
            //TODO?
            *newNaluBufferSize = 0;
            retPtr = NULL;
            break;
        default:
            break;
    }

    return retPtr;
}


int BEAVER_Filter_GetSpsPps(BEAVER_Filter_Handle filterHandle, uint8_t *spsBuffer, int *spsSize, uint8_t *ppsBuffer, int *ppsSize)
{
    BEAVER_Filter_t* filter = (BEAVER_Filter_t*)filterHandle;
    int ret = 0;

    if (!filterHandle)
    {
        return -1;
    }

    if ((!spsSize) || (!ppsSize))
    {
        return -1;
    }

    ARSAL_Mutex_Lock(&(filter->mutex));

    if (!filter->sync)
    {
        ret = -2;
    }

    if (ret == 0)
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


void* BEAVER_Filter_RunFilterThread(void *filterHandle)
{
    BEAVER_Filter_t* filter = (BEAVER_Filter_t*)filterHandle;
    BEAVER_Filter_Au_t au;
    int shouldStop, running, oldRunning = -1, late, early, tooLate;
    int enableJitterBuf = 0; //TODO
    int maxLatencyUs = 200000; //TODO
    int acquisitionDelta, decodingDelta, decodingDelta2;
    int sleepTimeUs, latencyUs = 0;
    int fifoRes, cbRet;
    uint64_t lastAuTimestamp = 0, lastAuCallbackTime = 0;
    uint64_t curTime, curTime2;
    int sleepTimeOffsetUs = 0;
    float sleepTimeSlope = 1.; //TODO
    struct timespec t1;

/* DEBUG */
    /*FILE *fDebug;
    fDebug = fopen("jitterbuf.dat", "w");*/
/* /DEBUG */

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, BEAVER_FILTER_TAG, "Filter thread is started");
    ARSAL_Mutex_Lock(&(filter->mutex));
    filter->threadStarted = 1;
    shouldStop = filter->threadShouldStop;
    running = filter->running;
    ARSAL_Mutex_Unlock(&(filter->mutex));

    while (!shouldStop)
    {
        if (running != oldRunning)
        {
            ARSAL_PRINT(ARSAL_PRINT_DEBUG, BEAVER_FILTER_TAG, "Filter thread is %s", (running) ? "running" : "paused");
            oldRunning = running;
        }

        if (running)
        {
            fifoRes = BEAVER_Filter_dequeueAu(filter, &au);
/* DEBUG */
            //fprintf(filter->fDebug, "dequeueAu: fifoRes=%d\n", fifoRes);
            //fflush(filter->fDebug);
/* /DEBUG */

            if (fifoRes == 0)
            {
                ARSAL_Time_GetTime(&t1);
                curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;
                if ((lastAuTimestamp != 0) && (lastAuCallbackTime != 0))
                {
                    acquisitionDelta = (int)(au.timestamp - lastAuTimestamp);
                    decodingDelta = (int)(curTime - lastAuCallbackTime);
                }
                else
                {
                    acquisitionDelta = decodingDelta = 0;
                }

                if (enableJitterBuf)
                {
                    early = ((float)decodingDelta < (float)acquisitionDelta * 0.95) ? 1 : 0;
                    late = ((float)decodingDelta > (float)acquisitionDelta * 1.05) ? 1 : 0;
                    sleepTimeUs = (early) ? (float)(acquisitionDelta - decodingDelta) * sleepTimeSlope - sleepTimeOffsetUs : 0;
                    //if ((lastAuTimestamp == 0) || (lastAuCallbackTime == 0)) sleepTimeUs = 50000; //TODO
                    tooLate = (latencyUs + sleepTimeUs + decodingDelta - acquisitionDelta > maxLatencyUs) ? 1 : 0;
                }
                else
                {
                    early = 0;
                    late = 0;
                    sleepTimeUs = 0;
                    tooLate = 0;
                }

                if (tooLate)
                {
                    curTime2 = curTime;
                    decodingDelta2 = decodingDelta;

                    /* TODO: recycle the AU buffer */

                    ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "TS delta: %.2fms - CbTime delta: %.2fms - too late - error: %.2f%% - latency would be: %.2fms - DROPPED",
                                (float)acquisitionDelta / 1000., (float)decodingDelta2 / 1000.,
                                (acquisitionDelta) ? ((float)decodingDelta2 - (float)acquisitionDelta) / (float)acquisitionDelta * 100. : 0.,
                                (float)(latencyUs + sleepTimeUs + decodingDelta - acquisitionDelta) / 1000.); //TODO: debug
                }
                else
                {
                    if (sleepTimeUs > 0)
                    {
                        usleep(sleepTimeUs);
                    }

                    ARSAL_Time_GetTime(&t1);
                    curTime2 = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;
                    decodingDelta2 = (lastAuCallbackTime != 0) ? (int)(curTime2 - lastAuCallbackTime) : 0;

                    ARSAL_Mutex_Lock(&(filter->mutex));
                    if (filter->running)
                    {
/* DEBUG */
                        //fprintf(filter->fDebug, "AU dequeued: size=%d ts=%llu\n", au.size, (unsigned long long int)au.timestamp);
                        //fflush(filter->fDebug);
/* /DEBUG */
                        /* call the auReadyCallback */
                        filter->callbackInProgress = 1;
                        ARSAL_Mutex_Unlock(&(filter->mutex));
                        cbRet = filter->auReadyCallback(au.buffer, au.size, au.timestamp, au.timestampShifted, au.syncType, au.userPtr, filter->auReadyCallbackUserPtr);
                        ARSAL_Mutex_Lock(&(filter->mutex));
                        if (cbRet != 0)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "auReadyCallback failed (returned %d)", cbRet);
                        }
/* DEBUG */
                        //fprintf(filter->fDebug, "AU callback returned: size=%d ts=%llu\n", au.size, (unsigned long long int)au.timestamp);
                        //fflush(filter->fDebug);
/* /DEBUG */
                    }
                    else
                    {
                        ARSAL_PRINT(ARSAL_PRINT_DEBUG, BEAVER_FILTER_TAG, "Filter is not running, auReadyCallback was not called, acces unit was dropped");
                    }
                    filter->callbackInProgress = 0;
                    ARSAL_Mutex_Unlock(&(filter->mutex));
                    ARSAL_Cond_Signal(&(filter->callbackCond));

                    if (early) sleepTimeOffsetUs += (decodingDelta2 - acquisitionDelta);
                    latencyUs += (((lastAuTimestamp != 0) && (lastAuCallbackTime != 0)) ? (decodingDelta2 - acquisitionDelta) : 0);

                    /*ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "TS delta: %.2fms - CbTime delta: %.2fms - %s - sleep: %.2fms - CbTime delta after: %.2fms - error: %.2f%% - latency: %.2fms - total latency: %.2fms",
                                (float)acquisitionDelta / 1000., (float)decodingDelta / 1000.,
                                (late) ? "late" : ((early) ? "early" : "on time"), (float)sleepTimeUs / 1000., (float)decodingDelta2 / 1000.,
                                (acquisitionDelta) ? ((float)decodingDelta2 - (float)acquisitionDelta) / (float)acquisitionDelta * 100. : 0., (float)latencyUs / 1000., ((float)curTime2 - (float)au.timestampShifted) / 1000.);*/ //TODO: debug
/* DEBUG */
                    /*setlocale(LC_ALL, "C");
                    fprintf(fDebug, "%llu %llu %llu %d %d %d %d ",
                            (long long unsigned int)au.timestamp, (long long unsigned int)curTime, (long long unsigned int)curTime2, acquisitionDelta, decodingDelta, sleepTimeUs, decodingDelta2);
                    fprintf(fDebug, "%.2f %d %d %d\n",
                            (acquisitionDelta) ? ((float)decodingDelta2 - (float)acquisitionDelta) / (float)acquisitionDelta * 100. : 0., latencyUs, sleepTimeOffsetUs, curTime2 - au.timestampShifted);
                    setlocale(LC_ALL, "");*/
/* /DEBUG */

                    lastAuTimestamp = au.timestamp;
                    lastAuCallbackTime = curTime2;

#ifdef BEAVER_FILTER_MONITORING_OUTPUT
                    if (filter->fMonitorOut)
                    {
                        int beaverRet = BEAVER_Parrot_WriteDragonFrameInfoV1ToFile(&au.frameInfo, filter->fMonitorOut);
                        if (beaverRet != 0)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "Failed to write the frameInfo to the file");
                        }
                        fprintf(filter->fMonitorOut, " %llu ", (long long unsigned int)au.timestampShifted);
                        fprintf(filter->fMonitorOut, "%llu ", (long long unsigned int)au.firstNaluInputTime);
                        fprintf(filter->fMonitorOut, "%llu\n", (long long unsigned int)curTime2);
                    }
#endif
                }
            }
            else if (fifoRes != -2)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Failed to dequeue an AU (%d)", fifoRes);
            }

            ARSAL_Mutex_Lock(&(filter->mutex));
            shouldStop = filter->threadShouldStop;
            running = filter->running;
            ARSAL_Mutex_Unlock(&(filter->mutex));

            if ((fifoRes != 0) && (shouldStop == 0))
            {
                /* Wake up when a new AU is in the FIFO or when we need to exit */
                ARSAL_Mutex_Lock(&(filter->fifoMutex));
                ARSAL_Cond_Wait(&(filter->fifoCond), &(filter->fifoMutex));
                ARSAL_Mutex_Unlock(&(filter->fifoMutex));
            }
        }
        else
        {
            ARSAL_Mutex_Lock(&(filter->mutex));
            shouldStop = filter->threadShouldStop;
            running = filter->running;
            if ((running == 0) && (shouldStop == 0))
            {
                /* Wake up when running state or when we need to exit */
                ARSAL_Cond_Wait(&(filter->startCond), &(filter->mutex));
            }
            ARSAL_Mutex_Unlock(&(filter->mutex));
        }
    }

    ARSAL_Mutex_Lock(&(filter->mutex));
    filter->threadStarted = 0;
    ARSAL_Mutex_Unlock(&(filter->mutex));

    /* flush the AU FIFO */
    BEAVER_Filter_flushAuFifo(filter);

/* DEBUG */
    //fclose(fDebug);
/* /DEBUG */

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, BEAVER_FILTER_TAG, "Filter thread has ended");

    return (void *)0;
}


int BEAVER_Filter_Start(BEAVER_Filter_Handle filterHandle, BEAVER_Filter_SpsPpsCallback_t spsPpsCallback, void* spsPpsCallbackUserPtr,
                        BEAVER_Filter_GetAuBufferCallback_t getAuBufferCallback, void* getAuBufferCallbackUserPtr,
                        BEAVER_Filter_AuReadyCallback_t auReadyCallback, void* auReadyCallbackUserPtr)
{
    BEAVER_Filter_t* filter = (BEAVER_Filter_t*)filterHandle;
    int ret = 0;

    if (!filterHandle)
    {
        return -1;
    }
    if (!getAuBufferCallback)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Invalid getAuBufferCallback function pointer");
        return -1;
    }
    if (!auReadyCallback)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Invalid auReadyCallback function pointer");
        return -1;
    }

    ARSAL_Mutex_Lock(&(filter->mutex));
    filter->spsPpsCallback = spsPpsCallback;
    filter->spsPpsCallbackUserPtr = spsPpsCallbackUserPtr;
    filter->getAuBufferCallback = getAuBufferCallback;
    filter->getAuBufferCallbackUserPtr = getAuBufferCallbackUserPtr;
    filter->auReadyCallback = auReadyCallback;
    filter->auReadyCallbackUserPtr = auReadyCallbackUserPtr;
    filter->running = 1;
    ARSAL_PRINT(ARSAL_PRINT_DEBUG, BEAVER_FILTER_TAG, "Filter is running");
    ARSAL_Mutex_Unlock(&(filter->mutex));
    ARSAL_Cond_Signal(&(filter->startCond));

    return ret;
}


int BEAVER_Filter_Pause(BEAVER_Filter_Handle filterHandle)
{
    BEAVER_Filter_t* filter = (BEAVER_Filter_t*)filterHandle;
    int ret = 0;

    if (!filterHandle)
    {
        return -1;
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
    ARSAL_PRINT(ARSAL_PRINT_DEBUG, BEAVER_FILTER_TAG, "Filter is paused");
    ARSAL_Mutex_Unlock(&(filter->mutex));

    return ret;
}


int BEAVER_Filter_Stop(BEAVER_Filter_Handle filterHandle)
{
    BEAVER_Filter_t* filter = (BEAVER_Filter_t*)filterHandle;
    int ret = 0;

    if (!filterHandle)
    {
        return -1;
    }

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, BEAVER_FILTER_TAG, "Stopping Filter...");
    ARSAL_Mutex_Lock(&(filter->mutex));
    filter->threadShouldStop = 1;
    ARSAL_Mutex_Unlock(&(filter->mutex));
    /* signal the filter thread to avoid a deadlock */
    ARSAL_Cond_Signal(&(filter->fifoCond));
    ARSAL_Cond_Signal(&(filter->startCond));

    return ret;
}


int BEAVER_Filter_Init(BEAVER_Filter_Handle *filterHandle, BEAVER_Filter_Config_t *config)
{
    BEAVER_Filter_t* filter;
    int ret = 0, mutexWasInit = 0, startCondWasInit = 0, callbackCondWasInit = 0, fifoMutexWasInit = 0, fifoCondWasInit = 0;

    if (!filterHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Invalid pointer for handle");
        return -1;
    }
    if (!config)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Invalid pointer for config");
        return -1;
    }

    filter = (BEAVER_Filter_t*)malloc(sizeof(*filter));
    if (!filter)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Allocation failed (size %ld)", sizeof(*filter));
        ret = -1;
    }

    if (ret == 0)
    {
        memset(filter, 0, sizeof(*filter));

        filter->auFifoSize = config->auFifoSize;
        filter->waitForSync = (config->waitForSync > 0) ? 1 : 0;
        filter->outputIncompleteAu = (config->outputIncompleteAu > 0) ? 1 : 0;
        filter->filterOutSpsPps = (config->filterOutSpsPps > 0) ? 1 : 0;
        filter->filterOutSei = (config->filterOutSei > 0) ? 1 : 0;
        filter->replaceStartCodesWithNaluSize = (config->replaceStartCodesWithNaluSize > 0) ? 1 : 0;
        filter->generateSkippedPSlices = (config->generateSkippedPSlices > 0) ? 1 : 0;
        filter->generateFirstGrayIFrame = (config->generateFirstGrayIFrame > 0) ? 1 : 0;

/* DEBUG */
        //filter->fDebug = fopen("debug.dat", "w");
/* /DEBUG */
    }

    if (ret == 0)
    {
        filter->tempAuBuffer = malloc(BEAVER_FILTER_TEMP_AU_BUFFER_SIZE);
        if (filter->tempAuBuffer == NULL)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Allocation failed (size %d)", BEAVER_FILTER_TEMP_AU_BUFFER_SIZE);
            ret = -1;
        }
        else
        {
            filter->tempAuBufferSize = BEAVER_FILTER_TEMP_AU_BUFFER_SIZE;
        }
    }

    if (ret == 0)
    {
        int mutexInitRet = ARSAL_Mutex_Init(&(filter->mutex));
        if (mutexInitRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Mutex creation failed (%d)", mutexInitRet);
            ret = -1;
        }
        else
        {
            mutexWasInit = 1;
        }
    }
    if (ret == 0)
    {
        int condInitRet = ARSAL_Cond_Init(&(filter->startCond));
        if (condInitRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Cond creation failed (%d)", condInitRet);
            ret = -1;
        }
        else
        {
            startCondWasInit = 1;
        }
    }
    if (ret == 0)
    {
        int condInitRet = ARSAL_Cond_Init(&(filter->callbackCond));
        if (condInitRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Cond creation failed (%d)", condInitRet);
            ret = -1;
        }
        else
        {
            callbackCondWasInit = 1;
        }
    }

    if (ret == 0)
    {
        filter->fifoPool = malloc(config->auFifoSize * sizeof(BEAVER_Filter_Au_t));
        if (filter->fifoPool == NULL)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Allocation failed (size %d)", config->auFifoSize * sizeof(BEAVER_Filter_Au_t));
            ret = -1;
        }
        else
        {
            memset(filter->fifoPool, 0, config->auFifoSize * sizeof(BEAVER_Filter_Au_t));
        }
    }
    if (ret == 0)
    {
        int mutexInitRet = ARSAL_Mutex_Init(&(filter->fifoMutex));
        if (mutexInitRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Mutex creation failed (%d)", mutexInitRet);
            ret = -1;
        }
        else
        {
            fifoMutexWasInit = 1;
        }
    }
    if (ret == 0)
    {
        int condInitRet = ARSAL_Cond_Init(&(filter->fifoCond));
        if (condInitRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Cond creation failed (%d)", condInitRet);
            ret = -1;
        }
        else
        {
            fifoCondWasInit = 1;
        }
    }

    if (ret == 0)
    {
        BEAVER_Parser_Config_t parserConfig;
        memset(&parserConfig, 0, sizeof(parserConfig));
        parserConfig.extractUserDataSei = 1;
        parserConfig.printLogs = 0;

        ret = BEAVER_Parser_Init(&(filter->parser), &parserConfig);
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "BEAVER_Parser_Init() failed (%d)", ret);
            ret = -1;
        }
    }

    if (ret == 0)
    {
        BEAVER_Writer_Config_t writerConfig;
        memset(&writerConfig, 0, sizeof(writerConfig));
        writerConfig.naluPrefix = 1;

        ret = BEAVER_Writer_Init(&(filter->writer), &writerConfig);
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "BEAVER_Writer_Init() failed (%d)", ret);
            ret = -1;
        }
    }

    if (ret == 0)
    {
        filter->tempSliceNaluBuffer = malloc(BEAVER_FILTER_TEMP_SLICE_NALU_BUFFER_SIZE);
        if (!filter->tempSliceNaluBuffer)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Allocation failed (size %d)", BEAVER_FILTER_TEMP_SLICE_NALU_BUFFER_SIZE);
            ret = -1;
        }
        filter->tempSliceNaluBufferSize = BEAVER_FILTER_TEMP_SLICE_NALU_BUFFER_SIZE;
    }

#ifdef BEAVER_FILTER_MONITORING_OUTPUT
    if (ret == 0)
    {
        int i;
        char szOutputFileName[128];
        char *pszFilePath = NULL;
        szOutputFileName[0] = '\0';
        if (0)
        {
        }
#ifdef BEAVER_FILTER_MONITORING_OUTPUT_ALLOW_NAP_USB
        else if ((access(BEAVER_FILTER_MONITORING_OUTPUT_PATH_NAP_USB, F_OK) == 0) && (access(BEAVER_FILTER_MONITORING_OUTPUT_PATH_NAP_USB, W_OK) == 0))
        {
            pszFilePath = BEAVER_FILTER_MONITORING_OUTPUT_PATH_NAP_USB;
        }
#endif
#ifdef BEAVER_FILTER_MONITORING_OUTPUT_ALLOW_NAP_INTERNAL
        else if ((access(BEAVER_FILTER_MONITORING_OUTPUT_PATH_NAP_INTERNAL, F_OK) == 0) && (access(BEAVER_FILTER_MONITORING_OUTPUT_PATH_NAP_INTERNAL, W_OK) == 0))
        {
            pszFilePath = BEAVER_FILTER_MONITORING_OUTPUT_PATH_NAP_INTERNAL;
        }
#endif
#ifdef BEAVER_FILTER_MONITORING_OUTPUT_ALLOW_ANDROID_INTERNAL
        else if ((access(BEAVER_FILTER_MONITORING_OUTPUT_PATH_ANDROID_INTERNAL, F_OK) == 0) && (access(BEAVER_FILTER_MONITORING_OUTPUT_PATH_ANDROID_INTERNAL, W_OK) == 0))
        {
            pszFilePath = BEAVER_FILTER_MONITORING_OUTPUT_PATH_ANDROID_INTERNAL;
        }
#endif
#ifdef BEAVER_FILTER_MONITORING_OUTPUT_ALLOW_PCLINUX
        else if ((access(BEAVER_FILTER_MONITORING_OUTPUT_PATH_PCLINUX, F_OK) == 0) && (access(BEAVER_FILTER_MONITORING_OUTPUT_PATH_PCLINUX, W_OK) == 0))
        {
            pszFilePath = BEAVER_FILTER_MONITORING_OUTPUT_PATH_PCLINUX;
        }
#endif
        if (pszFilePath)
        {
            for (i = 0; i < 1000; i++)
            {
                snprintf(szOutputFileName, 128, "%s/%s_%03d.dat", pszFilePath, BEAVER_FILTER_MONITORING_OUTPUT_FILENAME, i);
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
            filter->fMonitorOut = fopen(szOutputFileName, "w");
            if (!filter->fMonitorOut)
            {
                ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "Unable to open monitor output file '%s'", szOutputFileName);
            }
        }

        if (filter->fMonitorOut)
        {
            int beaverRet = BEAVER_Parrot_WriteDragonFrameInfoV1HeaderToFile(filter->fMonitorOut);
            if (beaverRet != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "Failed to write the frameInfo header to the file");
            }
            fprintf(filter->fMonitorOut, " acquisitionTsShifted beaverFirstNaluInputTime beaverAuOutputTime\n");
        }
    }
#endif //#ifdef BEAVER_FILTER_MONITORING_OUTPUT

    if (ret == 0)
    {
        *filterHandle = (BEAVER_Filter_Handle*)filter;
    }
    else
    {
        if (filter)
        {
            if (mutexWasInit) ARSAL_Mutex_Destroy(&(filter->mutex));
            if (startCondWasInit) ARSAL_Cond_Destroy(&(filter->startCond));
            if (callbackCondWasInit) ARSAL_Cond_Destroy(&(filter->callbackCond));
            if (filter->fifoPool) free(filter->fifoPool);
            if (fifoMutexWasInit) ARSAL_Mutex_Destroy(&(filter->fifoMutex));
            if (fifoCondWasInit) ARSAL_Cond_Destroy(&(filter->fifoCond));
            if (filter->parser) BEAVER_Parser_Free(filter->parser);
            if (filter->tempAuBuffer) free(filter->tempAuBuffer);
            if (filter->tempSliceNaluBuffer) free(filter->tempSliceNaluBuffer);
            if (filter->writer) BEAVER_Writer_Free(filter->writer);
#ifdef BEAVER_FILTER_MONITORING_OUTPUT
            if (filter->fMonitorOut) fclose(filter->fMonitorOut);
#endif
            free(filter);
        }
        *filterHandle = NULL;
    }

    return ret;
}


int BEAVER_Filter_Free(BEAVER_Filter_Handle *filterHandle)
{
    BEAVER_Filter_t* filter;
    int ret = -1, canDelete = 0;

    if ((!filterHandle) || (!*filterHandle))
    {
        return ret;
    }

    filter = (BEAVER_Filter_t*)*filterHandle;

    ARSAL_Mutex_Lock(&(filter->mutex));
    if (filter->threadStarted == 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_DEBUG, BEAVER_FILTER_TAG, "All threads are stopped");
        canDelete = 1;
    }

    if (canDelete == 1)
    {
        ARSAL_Mutex_Destroy(&(filter->mutex));
        ARSAL_Cond_Destroy(&(filter->startCond));
        ARSAL_Cond_Destroy(&(filter->callbackCond));
        free(filter->fifoPool);
        ARSAL_Mutex_Destroy(&(filter->fifoMutex));
        ARSAL_Cond_Destroy(&(filter->fifoCond));
        BEAVER_Parser_Free(filter->parser);
        BEAVER_Writer_Free(filter->writer);

        if (filter->pSps) free(filter->pSps);
        if (filter->pPps) free(filter->pPps);
        if (filter->tempAuBuffer) free(filter->tempAuBuffer);
        if (filter->tempSliceNaluBuffer) free(filter->tempSliceNaluBuffer);
#ifdef BEAVER_FILTER_MONITORING_OUTPUT
        if (filter->fMonitorOut) fclose(filter->fMonitorOut);
#endif

/* DEBUG */
        //fclose(filter->fDebug);
/* /DEBUG */

        free(filter);
        *filterHandle = NULL;
        ret = 0;
    }
    else
    {
        ARSAL_Mutex_Unlock(&(filter->mutex));
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Call BEAVER_Filter_Stop before calling this function");
    }

    return ret;
}

