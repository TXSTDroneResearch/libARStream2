/**
 * @file beaver_filter.c
 * @brief H.264 Elementary Stream Tools Library - Filter
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#include <stdio.h>
#include <stdlib.h>

#include <libARSAL/ARSAL_Print.h>
#include <libARSAL/ARSAL_Mutex.h>
#include <libARStream/ARSTREAM_Reader2.h>

#include <beaver/beaver_filter.h>
#include <beaver/beaver_parser.h>
#include <beaver/beaver_parrot.h>

/* DEBUG */
#include <locale.h>
/* /DEBUG */


#define BEAVER_FILTER_TAG "BEAVER_Filter"

#define BEAVER_FILTER_TEMP_AU_BUFFER_SIZE (1024 * 1024)


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
    BEAVER_Filter_AuSyncType_t syncType;
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
    BEAVER_Filter_CancelAuBufferCallback_t cancelAuBufferCallback;
    void* cancelAuBufferCallbackUserPtr;
    BEAVER_Filter_AuReadyCallback_t auReadyCallback;
    void* auReadyCallbackUserPtr;

    int auFifoSize;
    int waitForSync;
    int outputIncompleteAu;
    int filterOutSpsPps;
    int filterOutSei;
    int replaceStartCodesWithNaluSize;

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
    int currentAuIncomplete;
    BEAVER_Filter_AuSyncType_t currentAuSyncType;
    int currentAuSlicesAllI;

    BEAVER_Parser_Handle parser;

    int sync;
    int spsSync;
    int spsSize;
    uint8_t* pSps;
    int ppsSync;
    int ppsSize;
    uint8_t* pPps;

    ARSAL_Mutex_t mutex;
    int threadShouldStop;
    int threadStarted;

    /* Access unit FIFO */
    ARSAL_Mutex_t fifoMutex;
    ARSAL_Cond_t fifoCond;
    int fifoCount;
    BEAVER_Filter_Au_t *fifoHead;
    BEAVER_Filter_Au_t *fifoTail;
    BEAVER_Filter_Au_t *fifoPool;

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
    int fifoRes, cbRet;
    BEAVER_Filter_Au_t au;

    if (!filter)
    {
        return -1;
    }

    while ((fifoRes = BEAVER_Filter_dequeueAu(filter, &au)) == 0)
    {
        /* call the cancelAuBufferCallback */
        cbRet = filter->cancelAuBufferCallback(au.buffer, au.size, au.userPtr, filter->cancelAuBufferCallbackUserPtr);
        if (cbRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "cancelAuBufferCallback failed (returned %d)", cbRet);
        }
    }

    return 0;
}


static int BEAVER_Filter_Sync(BEAVER_Filter_t *filter, uint8_t *naluBuffer, int naluSize)
{
    int ret = 0;

    if (filter->sync)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Already synchronized!");
        return -1;
    }

    filter->sync = 1;
    ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "SPS/PPS sync OK"); //TODO: debug

    /* SPS/PPS callback */
    if (filter->spsPpsCallback)
    {
        int cbRet = filter->spsPpsCallback(filter->pSps, filter->spsSize, filter->pPps, filter->ppsSize, filter->spsPpsCallbackUserPtr);
        if (cbRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "spsPpsCallback failed (returned %d)", cbRet);
        }
    }

    if ((filter->waitForSync) && (filter->tempAuBuffer))
    {
        int cbRet = filter->getAuBufferCallback(&filter->currentAuBuffer, &filter->currentAuBufferSize, &filter->currentAuBufferUserPtr, filter->getAuBufferCallbackUserPtr);
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

    if ((!naluBuffer) || (naluSize <= 0))
    {
        return -1;
    }

    if (filter->replaceStartCodesWithNaluSize)
    {
        // Replace the NAL unit 4 bytes start code with the NALU size
        *((uint32_t*)naluBuffer) = (uint32_t)naluSize;
    }

    //TODO: use a more efficient function than BEAVER_Parser_ReadNextNalu_buffer
    ret = BEAVER_Parser_ReadNextNalu_buffer(filter->parser, naluBuffer, naluSize, NULL);
    if (ret < 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "BEAVER_Parser_ReadNextNalu_buffer() failed (%d)", ret);
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
                if (filter->sync)
                {
                    BEAVER_Parser_SliceInfo_t sliceInfo;
                    memset(&sliceInfo, 0, sizeof(sliceInfo));
                    ret = BEAVER_Parser_GetSliceInfo(filter->parser, &sliceInfo);
                    if (ret < 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "BEAVER_Parser_GetSliceInfo() failed (%d)", ret);
                    }
                    if (sliceInfo.sliceTypeMod5 == 2)
                    {
                        sliceType = BEAVER_FILTER_H264_SLICE_TYPE_I;
                    }
                    else if (sliceInfo.sliceTypeMod5 == 0)
                    {
                        sliceType = BEAVER_FILTER_H264_SLICE_TYPE_P;
                        filter->currentAuSlicesAllI = 0;
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
                    BEAVER_Parrot_DragonFrameInfoV1_t frameInfo;
                    BEAVER_Parrot_DragonStreamingV1_t streaming;
                    uint16_t sliceMbCount[BEAVER_PARROT_DRAGON_MAX_SLICE_COUNT];
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
                                ret = BEAVER_Parrot_DeserializeDragonStreamingV1(pUserDataSei, userDataSeiSize, &streaming, sliceMbCount);
                                if (ret < 0)
                                {
                                    ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "BEAVER_Parrot_DeserializeDragonStreamingV1() failed (%d)", ret);
                                }
                                else
                                {
                                    //ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "indexInGop: %d - sliceCount: %d", streaming.indexInGop, streaming.sliceCount); //TODO: debug
                                    //TODO
                                }
                                break;
                            case BEAVER_PARROT_USER_DATA_SEI_DRAGON_STREAMING_FRAMEINFO_V1:
                                ret = BEAVER_Parrot_DeserializeUserDataSeiDragonStreamingFrameInfoV1(pUserDataSei, userDataSeiSize, &frameInfo, &streaming, sliceMbCount);
                                if (ret < 0)
                                {
                                    ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "BEAVER_Parrot_DeserializeUserDataSeiDragonStreamingFrameInfoV1() failed (%d)", ret);
                                }
                                else
                                {
                                    //ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "indexInGop: %d - sliceCount: %d", streaming.indexInGop, streaming.sliceCount); //TODO: debug
                                    //TODO
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
                        if ((filter->spsSync) && (filter->ppsSync) && (!filter->sync))
                        {
                            ret = BEAVER_Filter_Sync(filter, naluBuffer, naluSize);
                            if (ret < 0)
                            {
                                ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "BEAVER_Filter_Sync() failed (%d)", ret);
                            }
                        }
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
                        if ((filter->spsSync) && (filter->ppsSync) && (!filter->sync))
                        {
                            ret = BEAVER_Filter_Sync(filter, naluBuffer, naluSize);
                            if (ret < 0)
                            {
                                ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "BEAVER_Filter_Sync() failed (%d)", ret);
                            }
                        }
                    }
                }
                break;
            default:
                break;
        }
    }

#if 0 //TODO
#ifdef ARSTREAM_READER2_DEBUG
            ARSTREAM_Reader2Debug_ProcessNalu(reader->rdbg, reader->currentNaluBuffer, reader->currentNaluSize, auTimestamp, receptionTs,
                                              missingPacketsBefore, 1, isFirstNaluInAu, isLastNaluInAu);
#endif
#endif

    return (ret >= 0) ? 0 : ret;
}


static int BEAVER_Filter_getNewAuBuffer(BEAVER_Filter_t *filter)
{
    int ret = 0;

    if ((filter->waitForSync) && (!filter->sync) && (filter->tempAuBuffer))
    {
        filter->currentAuBuffer = filter->tempAuBuffer;
        filter->currentAuBufferSize = filter->tempAuBufferSize;
    }
    else
    {
        int cbRet = filter->getAuBufferCallback(&filter->currentAuBuffer, &filter->currentAuBufferSize, &filter->currentAuBufferUserPtr, filter->getAuBufferCallbackUserPtr);
        if ((cbRet != 0) || (!filter->currentAuBuffer) || (!filter->currentAuBufferSize))
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "getAuBufferCallback failed (returned %d)", cbRet);
            filter->currentAuBuffer = NULL;
            filter->currentAuBufferSize = 0;
            ret = -1;
        }
    }

    return ret;
}


static void BEAVER_Filter_resetCurrentAu(BEAVER_Filter_t *filter)
{
    filter->currentAuSize = 0;
    filter->currentAuIncomplete = 0;
    filter->currentAuSyncType = BEAVER_FILTER_AU_SYNC_TYPE_NONE;
    filter->currentAuSlicesAllI = 1;
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
        //TODO: BEAVER_FILTER_AU_SYNC_TYPE_PIR_START
    }

    if ((filter->waitForSync) && (!filter->sync))
    {
        cancelAuOutput = 1;
        ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "AU output cancelled (waitForSync)"); //TODO: debug
    }

    if ((!filter->outputIncompleteAu) && (filter->currentAuIncomplete))
    {
        cancelAuOutput = 1;
        ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "AU output cancelled (!outputIncompleteAu)"); //TODO: debug
    }

    if (!cancelAuOutput)
    {
        BEAVER_Filter_Au_t au;
        memset(&au, 0, sizeof(au));
        au.buffer = filter->currentAuBuffer;
        au.size = filter->currentAuSize;
        au.timestamp = filter->currentAuTimestamp;
        au.timestampShifted = filter->currentAuTimestampShifted;
        au.syncType = filter->currentAuSyncType;
        au.userPtr = filter->currentAuBufferUserPtr;
        int fifoRes = BEAVER_Filter_enqueueAu(filter, &au);
        if (fifoRes == 0)
        {
            ret = 1;
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "Failed to enqueue the current AU (fifoRes = %d)", fifoRes);
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
    int ret = 0, cbRet;
    uint8_t *retPtr = NULL;

    if (!filter)
    {
        return retPtr;
    }

    ARSAL_Mutex_Lock(&(filter->mutex));

    switch (cause)
    {
        case ARSTREAM_READER2_CAUSE_NALU_COMPLETE:
            ret = BEAVER_Filter_processNalu(filter, naluBuffer, naluSize, &naluType);
            if (ret < 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "BEAVER_Filter_processNalu() failed (%d)", ret);
            }

            if (missingPacketsBefore) filter->currentAuIncomplete = 1;
            filter->currentAuTimestamp = auTimestamp; //TODO: handle changing TS
            filter->currentAuTimestampShifted = auTimestampShifted; //TODO: handle changing TS

            if (isLastNaluInAu)
            {
                BEAVER_Filter_addNaluToCurrentAu(filter, naluType, naluSize);

                /* Output access unit */
                ret = BEAVER_Filter_enqueueCurrentAu(filter);

                if (ret > 0)
                {
                    /* auReadyCallback has been called */
                    ret = BEAVER_Filter_getNewAuBuffer(filter);
                    if (ret == 0)
                    {
                        BEAVER_Filter_resetCurrentAu(filter);

                        filter->currentNaluBuffer = filter->currentAuBuffer + filter->currentAuSize;
                        filter->currentNaluBufferSize = filter->currentAuBufferSize - filter->currentAuSize;
                    }
                    else
                    {
                        filter->currentNaluBuffer = NULL;
                        filter->currentNaluBufferSize = 0;
                    }
                }
                else
                {
                    /* auReadyCallback has not been called: reuse current auBuffer */
                    BEAVER_Filter_resetCurrentAu(filter);

                    filter->currentNaluBuffer = filter->currentAuBuffer + filter->currentAuSize;
                    filter->currentNaluBufferSize = filter->currentAuBufferSize - filter->currentAuSize;
                }
                *newNaluBufferSize = filter->currentNaluBufferSize;
                retPtr = filter->currentNaluBuffer;
            }
            else
            {
                if ((isFirstNaluInAu) && (filter->currentAuSize > 0))
                {
                    /* Handle previous access unit that has not been output */
                    uint8_t *tmpBuf = malloc(naluSize); //TODO
                    if (tmpBuf)
                    {
                        memcpy(tmpBuf, naluBuffer, naluSize);
                    }

                    /* Output access unit */
                    ret = BEAVER_Filter_enqueueCurrentAu(filter);

                    if (ret > 0)
                    {
                        /* auReadyCallback has been called */
                        ret = BEAVER_Filter_getNewAuBuffer(filter);
                        if (ret == 0)
                        {
                            BEAVER_Filter_resetCurrentAu(filter);

                            filter->currentNaluBuffer = filter->currentAuBuffer + filter->currentAuSize;
                            filter->currentNaluBufferSize = filter->currentAuBufferSize - filter->currentAuSize;
                            if (tmpBuf)
                            {
                                memcpy(filter->currentNaluBuffer, tmpBuf, naluSize); //TODO: filter->currentNaluBufferSize must be > naluSize
                                free(tmpBuf);
                            }
                        }
                        else
                        {
                            filter->currentNaluBuffer = NULL;
                            filter->currentNaluBufferSize = 0;
                        }
                    }
                    else
                    {
                        /* auReadyCallback has not been called: reuse current auBuffer */
                        BEAVER_Filter_resetCurrentAu(filter);

                        filter->currentNaluBuffer = filter->currentAuBuffer + filter->currentAuSize;
                        filter->currentNaluBufferSize = filter->currentAuBufferSize - filter->currentAuSize;
                        if (tmpBuf)
                        {
                            memcpy(filter->currentNaluBuffer, tmpBuf, naluSize); //TODO: filter->currentNaluBufferSize must be > naluSize
                            free(tmpBuf);
                        }
                    }
                }

                if ((filter->currentNaluBuffer) && (filter->currentNaluBufferSize))
                {
                    BEAVER_Filter_addNaluToCurrentAu(filter, naluType, naluSize);

                    filter->currentNaluBuffer = filter->currentAuBuffer + filter->currentAuSize;
                    filter->currentNaluBufferSize = filter->currentAuBufferSize - filter->currentAuSize;
                }
                *newNaluBufferSize = filter->currentNaluBufferSize;
                retPtr = filter->currentNaluBuffer;
            }
            break;
        case ARSTREAM_READER2_CAUSE_NALU_BUFFER_TOO_SMALL:
            ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "ARStream_Reader2 NALU buffer is too small, truncated AU (or maybe it's the first call)");

            ret = 1;
            if (filter->currentAuSize > 0)
            {
                /* Output access unit */
                ret = BEAVER_Filter_enqueueCurrentAu(filter);
            }

            if (ret > 0)
            {
                /* auReadyCallback has been called, or no AU was pending */
                ret = BEAVER_Filter_getNewAuBuffer(filter);
                if (ret == 0)
                {
                    BEAVER_Filter_resetCurrentAu(filter);

                    filter->currentNaluBuffer = filter->currentAuBuffer + filter->currentAuSize;
                    filter->currentNaluBufferSize = filter->currentAuBufferSize - filter->currentAuSize;
                }
                else
                {
                    filter->currentNaluBuffer = NULL;
                    filter->currentNaluBufferSize = 0;
                }
            }
            else
            {
                /* auReadyCallback has not been called: reuse current auBuffer */
                BEAVER_Filter_resetCurrentAu(filter);

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
            cbRet = filter->cancelAuBufferCallback(filter->currentAuBuffer, filter->currentAuBufferSize, filter->currentAuBufferUserPtr, filter->cancelAuBufferCallbackUserPtr);
            if (cbRet != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "cancelAuBufferCallback failed (returned %d)", cbRet);
            }
            *newNaluBufferSize = 0;
            retPtr = NULL;
            break;
        default:
            break;
    }

    ARSAL_Mutex_Unlock(&(filter->mutex));

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
    int shouldStop, late, early, tooLate;
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

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, BEAVER_FILTER_TAG, "Filter thread is running");
    ARSAL_Mutex_Lock(&(filter->mutex));
    filter->threadStarted = 1;
    shouldStop = filter->threadShouldStop;
    ARSAL_Mutex_Unlock(&(filter->mutex));

    while (!shouldStop)
    {
        fifoRes = BEAVER_Filter_dequeueAu(filter, &au);

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

                /* call the cancelAuBufferCallback */
                cbRet = filter->cancelAuBufferCallback(au.buffer, au.size, au.userPtr, filter->cancelAuBufferCallbackUserPtr);
                if (cbRet != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "cancelAuBufferCallback failed (returned %d)", cbRet);
                }

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

                /* call the auReadyCallback */
                cbRet = filter->auReadyCallback(au.buffer, au.size, au.timestamp, au.timestampShifted, au.syncType, au.userPtr, filter->auReadyCallbackUserPtr);
                if (cbRet != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_WARNING, BEAVER_FILTER_TAG, "auReadyCallback failed (returned %d)", cbRet);
                }

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
            }
        }

        ARSAL_Mutex_Lock(&(filter->mutex));
        shouldStop = filter->threadShouldStop;
        ARSAL_Mutex_Unlock(&(filter->mutex));

        if ((fifoRes != 0) && (shouldStop == 0))
        {
            /* Wake up when a new AU is in the FIFO or when we need to exit */
            ARSAL_Mutex_Lock(&(filter->fifoMutex));
            ARSAL_Cond_Wait(&(filter->fifoCond), &(filter->fifoMutex));
            ARSAL_Mutex_Unlock(&(filter->fifoMutex));
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

    return ret;
}


int BEAVER_Filter_Init(BEAVER_Filter_Handle *filterHandle, BEAVER_Filter_Config_t *config)
{
    BEAVER_Filter_t* filter;
    int ret = 0, mutexWasInit = 0, fifoMutexWasInit = 0, fifoCondWasInit = 0;

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
    if (!config->getAuBufferCallback)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Invalid getAuBufferCallback function pointer");
        return -1;
    }
    if (!config->cancelAuBufferCallback)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Invalid cancelAuBufferCallback function pointer");
        return -1;
    }
    if (!config->auReadyCallback)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_FILTER_TAG, "Invalid auReadyCallback function pointer");
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

        filter->spsPpsCallback = config->spsPpsCallback;
        filter->spsPpsCallbackUserPtr = config->spsPpsCallbackUserPtr;
        filter->getAuBufferCallback = config->getAuBufferCallback;
        filter->getAuBufferCallbackUserPtr = config->getAuBufferCallbackUserPtr;
        filter->cancelAuBufferCallback = config->cancelAuBufferCallback;
        filter->cancelAuBufferCallbackUserPtr = config->cancelAuBufferCallbackUserPtr;
        filter->auReadyCallback = config->auReadyCallback;
        filter->auReadyCallbackUserPtr = config->auReadyCallbackUserPtr;

        filter->auFifoSize = config->auFifoSize;
        filter->waitForSync = (config->waitForSync > 0) ? 1 : 0;
        filter->outputIncompleteAu = (config->outputIncompleteAu > 0) ? 1 : 0;
        filter->filterOutSpsPps = (config->filterOutSpsPps > 0) ? 1 : 0;
        filter->filterOutSei = (config->filterOutSei > 0) ? 1 : 0;
        filter->replaceStartCodesWithNaluSize = (config->replaceStartCodesWithNaluSize > 0) ? 1 : 0;

        filter->threadStarted = 0;
        filter->threadShouldStop = 0;
        filter->sync = 0;
        filter->spsSync = 0;
        filter->ppsSync = 0;
    }

    if (ret == 0)
    {
        if (filter->waitForSync)
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
        *filterHandle = (BEAVER_Filter_Handle*)filter;
    }
    else
    {
        if (filter)
        {
            if (mutexWasInit) ARSAL_Mutex_Destroy(&(filter->mutex));
            if (filter->fifoPool) free(filter->fifoPool);
            if (fifoMutexWasInit) ARSAL_Mutex_Destroy(&(filter->fifoMutex));
            if (fifoCondWasInit) ARSAL_Cond_Destroy(&(filter->fifoCond));
            if (filter->parser) BEAVER_Parser_Free(filter->parser);
            if (filter->tempAuBuffer) free(filter->tempAuBuffer);
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
        free(filter->fifoPool);
        ARSAL_Mutex_Destroy(&(filter->fifoMutex));
        ARSAL_Cond_Destroy(&(filter->fifoCond));
        BEAVER_Parser_Free(filter->parser);

        if (filter->pSps) free(filter->pSps);
        if (filter->pPps) free(filter->pPps);
        if (filter->tempAuBuffer) free(filter->tempAuBuffer);

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

