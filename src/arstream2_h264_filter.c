/**
 * @file arstream2_h264_filter.c
 * @brief Parrot Reception Library - H.264 Filter
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#include "arstream2_h264_filter.h"


#define ARSTREAM2_H264_FILTER_TAG "ARSTREAM2_H264Filter"


static int ARSTREAM2_H264Filter_AuBufferPoolInit(ARSTREAM2_H264Filter_AuBufferPool_t *pool, int maxCount, int bufferSize, int metadataBufferSize)
{
    int i;
    ARSTREAM2_H264Filter_AuBufferItem_t* cur;

    if (!pool)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Invalid pointer");
        return -1;
    }

    if (maxCount <= 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Invalid pool size (%d)", maxCount);
        return -1;
    }

    memset(pool, 0, sizeof(ARSTREAM2_H264Filter_AuBufferPool_t));
    pool->size = maxCount;
    pool->pool = malloc(maxCount * sizeof(ARSTREAM2_H264Filter_AuBufferItem_t));
    if (!pool->pool)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Pool allocation failed (size %d)", maxCount * sizeof(ARSTREAM2_H264Filter_AuBufferItem_t));
        pool->size = 0;
        return -1;
    }
    memset(pool->pool, 0, maxCount * sizeof(ARSTREAM2_H264Filter_AuBufferItem_t));

    for (i = 0; i < maxCount; i++)
    {
        cur = &pool->pool[i];
        if (pool->free)
        {
            pool->free->prev = cur;
        }
        cur->next = pool->free;
        cur->prev = NULL;
        pool->free = cur;
    }

    if (bufferSize > 0)
    {
        for (i = 0; i < maxCount; i++)
        {
            pool->pool[i].buffer = malloc(bufferSize);
            if (!pool->pool[i].buffer)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Pool buffer allocation failed (size %d)", bufferSize);
                pool->size = 0;
                return -1;
            }
            pool->pool[i].bufferSize = bufferSize;
        }
    }

    if (metadataBufferSize > 0)
    {
        for (i = 0; i < maxCount; i++)
        {
            pool->pool[i].metadataBuffer = malloc(metadataBufferSize);
            if (!pool->pool[i].metadataBuffer)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Pool buffer allocation failed (size %d)", metadataBufferSize);
                pool->size = 0;
                return -1;
            }
            pool->pool[i].metadataBufferSize = metadataBufferSize;
        }
    }

    return 0;
}


static int ARSTREAM2_H264Filter_AuBufferPoolFree(ARSTREAM2_H264Filter_AuBufferPool_t *pool)
{
    int i;

    if (!pool)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Invalid pointer");
        return -1;
    }

    if (pool->pool)
    {
        for (i = 0; i < pool->size; i++)
        {
            if (pool->pool[i].buffer)
            {
                free(pool->pool[i].buffer);
                pool->pool[i].buffer = NULL;
            }
            if (pool->pool[i].metadataBuffer)
            {
                free(pool->pool[i].metadataBuffer);
                pool->pool[i].metadataBuffer = NULL;
            }
        }

        free(pool->pool);
    }
    memset(pool, 0, sizeof(ARSTREAM2_H264Filter_AuBufferPool_t));

    return 0;
}


static ARSTREAM2_H264Filter_AuBufferItem_t* ARSTREAM2_H264Filter_AuBufferPoolPopFreeItem(ARSTREAM2_H264Filter_AuBufferPool_t *pool)
{
    if (!pool)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Invalid pointer");
        return NULL;
    }

    if (pool->free)
    {
        ARSTREAM2_H264Filter_AuBufferItem_t* cur = pool->free;
        pool->free = cur->next;
        if (cur->next) cur->next->prev = NULL;
        cur->prev = NULL;
        cur->next = NULL;
        return cur;
    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "No free item in pool");
        return NULL;
    }
}


static int ARSTREAM2_H264Filter_AuBufferPoolPushFreeItem(ARSTREAM2_H264Filter_AuBufferPool_t *pool, ARSTREAM2_H264Filter_AuBufferItem_t *item)
{
    if ((!pool) || (!item))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Invalid pointer");
        return -1;
    }

    if (pool->free)
    {
        pool->free->prev = item;
        item->next = pool->free;
    }
    else
    {
        item->next = NULL;
    }
    pool->free = item;
    item->prev = NULL;

    return 0;
}


static void ARSTREAM2_H264Filter_StreamRecorderAuCallback(eARSTREAM2_STREAM_RECORDER_AU_STATUS status, void *auUserPtr, void *userPtr)
{
    ARSTREAM2_H264Filter_t *filter = (ARSTREAM2_H264Filter_t*)userPtr;
    ARSTREAM2_H264_AuFifoItem_t *auItem = (ARSTREAM2_H264_AuFifoItem_t*)auUserPtr;
    ARSTREAM2_H264_NaluFifoItem_t *naluItem;
    int ret;

    if (!filter)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Invalid recorder auCallback user pointer");
        return;
    }
    if (!auItem)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Invalid recorder access unit user pointer");
        return;
    }

    /* free the access unit and associated NAL units */
    ARSAL_Mutex_Lock(filter->fifoMutex);
    while ((naluItem = ARSTREAM2_H264_AuDequeueNalu(&auItem->au)) != NULL)
    {
        ret = ARSTREAM2_H264_NaluFifoPushFreeItem(filter->naluFifo, naluItem);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Failed to push free item in the NALU FIFO (%d)", ret);
        }
    }
    ret = ARSTREAM2_H264_AuFifoPushFreeItem(filter->auFifo, auItem);
    ARSAL_Mutex_Unlock(filter->fifoMutex);
    if (ret != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Failed to push free item in the AU FIFO (%d)", ret);
    }
}


static int ARSTREAM2_H264Filter_StreamRecorderInit(ARSTREAM2_H264Filter_t *filter)
{
    int ret = -1;

    if ((!filter->recorder) && (filter->recordFileName))
    {
        eARSTREAM2_ERROR recErr;
        ARSTREAM2_StreamRecorder_Config_t recConfig;
        memset(&recConfig, 0, sizeof(ARSTREAM2_StreamRecorder_Config_t));
        recConfig.mediaFileName = filter->recordFileName;
        recConfig.videoFramerate = filter->framerate;
        recConfig.videoWidth = filter->mbWidth * 16; //TODO
        recConfig.videoHeight = filter->mbHeight * 16; //TODO
        recConfig.sps = filter->pSps;
        recConfig.spsSize = filter->spsSize;
        recConfig.pps = filter->pPps;
        recConfig.ppsSize = filter->ppsSize;
        recConfig.serviceType = 0; //TODO
        recConfig.auFifoSize = filter->auFifo->size;
        recConfig.auCallback = ARSTREAM2_H264Filter_StreamRecorderAuCallback;
        recConfig.auCallbackUserPtr = filter;
        recErr = ARSTREAM2_StreamRecorder_Init(&filter->recorder, &recConfig);
        if (recErr != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_StreamRecorder_Init() failed (%d): %s",
                        recErr, ARSTREAM2_Error_ToString(recErr));
        }
        else
        {
            int thErr = ARSAL_Thread_Create(&filter->recorderThread, ARSTREAM2_StreamRecorder_RunThread, (void*)filter->recorder);
            if (thErr != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Recorder thread creation failed (%d)", thErr);
            }
            else
            {
                ret = 0;
            }
        }
    }

    return ret;
}


static int ARSTREAM2_H264Filter_StreamRecorderStop(ARSTREAM2_H264Filter_t *filter)
{
    int ret = 0;

    if (filter->recorder)
    {
        eARSTREAM2_ERROR err;
        err = ARSTREAM2_StreamRecorder_Stop(filter->recorder);
        if (err != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_StreamRecorder_Stop() failed: %d (%s)",
                        err, ARSTREAM2_Error_ToString(err));
            ret = -1;
        }
    }

    return ret;
}


static int ARSTREAM2_H264Filter_StreamRecorderFree(ARSTREAM2_H264Filter_t *filter)
{
    int ret = 0;

    if (filter->recorder)
    {
        int thErr;
        eARSTREAM2_ERROR err;
        if (filter->recorderThread)
        {
            thErr = ARSAL_Thread_Join(filter->recorderThread, NULL);
            if (thErr != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSAL_Thread_Join() failed (%d)", thErr);
                ret = -1;
            }
            thErr = ARSAL_Thread_Destroy(&filter->recorderThread);
            if (thErr != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSAL_Thread_Destroy() failed (%d)", thErr);
                ret = -1;
            }
            filter->recorderThread = NULL;
        }
        err = ARSTREAM2_StreamRecorder_Free(&filter->recorder);
        if (err != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_StreamRecorder_Free() failed (%d): %s",
                        err, ARSTREAM2_Error_ToString(err));
            ret = -1;
        }
    }

    return ret;
}


static int ARSTREAM2_H264Filter_Sync(ARSTREAM2_H264Filter_t *filter)
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
    }

    /* Configure the writer */
    if (ret == 0)
    {
        err = ARSTREAM2_H264Parser_GetSpsPpsContext(filter->parser, (void**)&spsContext, (void**)&ppsContext);
        if (err != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Parser_GetSpsPpsContext() failed (%d)", err);
            ret = -1;
        }
        else
        {
            filter->mbWidth = spsContext->pic_width_in_mbs_minus1 + 1;
            filter->mbHeight = (spsContext->pic_height_in_map_units_minus1 + 1) * ((spsContext->frame_mbs_only_flag) ? 1 : 2);
            filter->mbCount = filter->mbWidth * filter->mbHeight;
            filter->framerate = (spsContext->num_units_in_tick != 0) ? (float)spsContext->time_scale / (float)(spsContext->num_units_in_tick * 2) : 30.;
            filter->maxFrameNum = 1 << (spsContext->log2_max_frame_num_minus4 + 4);
            err = ARSTREAM2_H264Writer_SetSpsPpsContext(filter->writer, (void*)spsContext, (void*)ppsContext);
            if (err != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Parser_GetSpsPpsContext() failed (%d)", err);
                ret = -1;
            }
        }
    }

    /* stream recording */
    if ((ret == 0) && (filter->recorderStartPending))
    {
        int recRet;
        recRet = ARSTREAM2_H264Filter_StreamRecorderInit(filter);
        if (recRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_StreamRecorderInit() failed (%d)", recRet);
        }
        filter->recorderStartPending = 0;
    }

    if (ret == 0)
    {
        filter->currentAuMacroblockStatus = realloc(filter->currentAuMacroblockStatus, filter->mbCount * sizeof(uint8_t));
        filter->currentAuRefMacroblockStatus = realloc(filter->currentAuRefMacroblockStatus, filter->mbCount * sizeof(uint8_t));
        if (filter->currentAuMacroblockStatus)
        {
            memset(filter->currentAuMacroblockStatus, ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_UNKNOWN, filter->mbCount);
        }
        if (filter->currentAuRefMacroblockStatus)
        {
            memset(filter->currentAuRefMacroblockStatus, ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_UNKNOWN, filter->mbCount);
        }
        filter->previousAuFrameNum = -1;
    }

    return ret;
}


static int ARSTREAM2_H264Filter_ParseNalu(ARSTREAM2_H264Filter_t *filter, ARSTREAM2_H264_AccessUnit_t *au, ARSTREAM2_H264_NalUnit_t *nalu)
{
    int ret = 0;
    eARSTREAM2_ERROR err = ARSTREAM2_OK, _err = ARSTREAM2_OK;

    if (nalu->naluSize <= 4)
    {
        return -1;
    }

    err = ARSTREAM2_H264Parser_SetupNalu_buffer(filter->parser, nalu->nalu + 4, nalu->naluSize - 4);
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
        nalu->naluType = ARSTREAM2_H264Parser_GetLastNaluType(filter->parser);
        switch (nalu->naluType)
        {
            case ARSTREAM2_H264_NALU_TYPE_SLICE_IDR:
                filter->currentAuSyncType = ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_IDR;
            case ARSTREAM2_H264_NALU_TYPE_SLICE:
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
                        filter->currentAuIsRef = (sliceInfo.nal_ref_idc != 0) ? 1 : 0;
                        if (sliceInfo.sliceTypeMod5 == 2)
                        {
                            nalu->sliceType = ARSTREAM2_H264_SLICE_TYPE_I;
                        }
                        else if (sliceInfo.sliceTypeMod5 == 0)
                        {
                            nalu->sliceType = ARSTREAM2_H264_SLICE_TYPE_P;
                            filter->currentAuSlicesAllI = 0;
                        }
                        filter->currentAuCurrentSliceFirstMb = sliceInfo.first_mb_in_slice;
                        if (filter->currentAuFrameNum == -1)
                        {
                            filter->currentAuFrameNum = sliceInfo.frame_num;
                            if ((filter->currentAuIsRef) && (filter->previousAuFrameNum != -1) && (filter->currentAuFrameNum != (filter->previousAuFrameNum + 1) % filter->maxFrameNum))
                            {
                                /* count missed frames (missing non-ref frames are not counted as missing) */
                                filter->stats.totalFrameCount++;
                                filter->stats.missedFrameCount++;
                                if (filter->currentAuRefMacroblockStatus)
                                {
                                    memset(filter->currentAuRefMacroblockStatus, ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_MISSING, filter->mbCount);
                                }
                            }
                        }
                    }
                }
                break;
            case ARSTREAM2_H264_NALU_TYPE_SEI:
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
                                    filter->currentAuInferredSliceMbCount = filter->currentAuStreamingSliceMbCount[0];
                                }
                            }
                            else if (userDataSeiSize <= au->userDataBufferSize)
                            {
                                memcpy(au->userDataBuffer, pUserDataSei, userDataSeiSize);
                                au->userDataSize = userDataSeiSize;
                            }
                        }
                    }
                }
                break;
            case ARSTREAM2_H264_NALU_TYPE_SPS:
                /* SPS */
                if (!filter->spsSync)
                {
                    filter->pSps = malloc(nalu->naluSize);
                    if (!filter->pSps)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Allocation failed for SPS (size %d)", nalu->naluSize);
                    }
                    else
                    {
                        memcpy(filter->pSps, nalu->nalu, nalu->naluSize);
                        filter->spsSize = nalu->naluSize;
                        filter->spsSync = 1;
                    }
                }
                break;
            case ARSTREAM2_H264_NALU_TYPE_PPS:
                /* PPS */
                if (!filter->ppsSync)
                {
                    filter->pPps = malloc(nalu->naluSize);
                    if (!filter->pPps)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Allocation failed for PPS (size %d)", nalu->naluSize);
                    }
                    else
                    {
                        memcpy(filter->pPps, nalu->nalu, nalu->naluSize);
                        filter->ppsSize = nalu->naluSize;
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
        ret = ARSTREAM2_H264Filter_Sync(filter);
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

    if (filter->recorder)
    {
        if (filter->recordAuBufferItem->pushed)
        {
            ARSAL_Mutex_Lock(&(filter->recordAuBufferPoolMutex));
            filter->recordAuBufferItem = ARSTREAM2_H264Filter_AuBufferPoolPopFreeItem(&filter->recordAuBufferPool);
            ARSAL_Mutex_Unlock(&(filter->recordAuBufferPoolMutex));
            if (!filter->recordAuBufferItem)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Failed to get free item in AU buffer pool");
                ARSTREAM2_StreamRecorder_Flush(filter->recorder);
            }
            else
            {
                filter->recordAuBufferItem->pushed = 0;
            }
        }
        else
        {
            ARSAL_Mutex_Unlock(&(filter->recordAuBufferPoolMutex));
        }
    }

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
    filter->currentAuMetadataSize = 0;
    memset(filter->currentAuMetadata, 0, sizeof(ARSTREAM2_H264_FILTER_AU_METADATA_BUFFER_SIZE));
    filter->currentAuUserDataSize = 0;
    memset(filter->currentAuUserData, 0, sizeof(ARSTREAM2_H264_FILTER_AU_USER_DATA_BUFFER_SIZE));
    filter->currentAuPreviousSliceIndex = -1;
    filter->currentAuPreviousSliceFirstMb = 0;
    filter->currentAuInferredPreviousSliceFirstMb = 0;
    filter->currentAuCurrentSliceFirstMb = -1;
    filter->currentAuFirstNaluInputTime = 0;
    filter->previousSliceType = ARSTREAM2_H264_SLICE_TYPE_NON_VCL;
    if (filter->currentAuMacroblockStatus)
    {
        memset(filter->currentAuMacroblockStatus, ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_UNKNOWN, filter->mbCount);
    }
    if (filter->currentAuIsRef) filter->previousAuFrameNum = filter->currentAuFrameNum;
    filter->currentAuFrameNum = -1;
    filter->currentAuIsRef = 0;
    if (filter->recordAuBufferItem)
    {
        filter->recordAuBufferItem->auSize = 0;
        filter->recordAuBufferItem->naluCount = 0;
    }
}


static void ARSTREAM2_H264Filter_updateCurrentAu(ARSTREAM2_H264Filter_t *filter, uint8_t naluType, uint8_t sliceType)
{
    int sliceMbCount = 0, sliceFirstMb = 0;

    if ((naluType == ARSTREAM2_H264_NALU_TYPE_SLICE_IDR) || (naluType == ARSTREAM2_H264_NALU_TYPE_SLICE))
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
            sliceFirstMb = filter->currentAuPreviousSliceFirstMb = filter->currentAuCurrentSliceFirstMb;
            sliceMbCount = filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex];
            //ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_H264_FILTER_TAG, "previousSliceIndex: %d - previousSliceFirstMb: %d", filter->currentAuPreviousSliceIndex, filter->currentAuCurrentSliceFirstMb); //TODO: debug
        }
        else if (filter->currentAuCurrentSliceFirstMb >= 0)
        {
            sliceFirstMb = filter->currentAuInferredPreviousSliceFirstMb = filter->currentAuCurrentSliceFirstMb;
            sliceMbCount = (filter->currentAuInferredSliceMbCount > 0) ? filter->currentAuInferredSliceMbCount : 0;
        }
        if ((filter->currentAuMacroblockStatus) && (sliceMbCount > 0))
        {
            int i, idx;
            uint8_t status;
            if (sliceFirstMb + sliceMbCount > filter->mbCount) sliceMbCount = filter->mbCount - sliceFirstMb;
            for (i = 0, idx = sliceFirstMb; i < sliceMbCount; i++, idx++)
            {
                if (sliceType == ARSTREAM2_H264_SLICE_TYPE_I)
                {
                    status = ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_VALID_ISLICE;
                }
                else
                {
                    if ((!filter->currentAuRefMacroblockStatus)
                            || ((filter->currentAuRefMacroblockStatus[idx] != ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_VALID_ISLICE) && (filter->currentAuRefMacroblockStatus[idx] != ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_VALID_PSLICE)))
                    {
                        status = ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_ERROR_PROPAGATION;
                    }
                    else
                    {
                        status = ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_VALID_PSLICE;
                    }
                }
                filter->currentAuMacroblockStatus[idx] = status;
            }
        }
    }
}


static void ARSTREAM2_H264Filter_addNaluToCurrentAu(ARSTREAM2_H264Filter_t *filter, uint8_t naluType, uint8_t *naluBuffer, int naluSize)
{
    int filterOut = 0;

    if ((filter->recorder) && (filter->recordAuBufferItem)
            && (filter->recordAuBufferItem->auSize + naluSize <= filter->recordAuBufferItem->bufferSize))
    {
        memcpy(filter->recordAuBufferItem->buffer + filter->recordAuBufferItem->auSize, naluBuffer, naluSize);
        if (filter->recordAuBufferItem->naluCount < ARSTREAM2_STREAM_RECORDER_NALU_MAX_COUNT)
        {
            filter->recordAuBufferItem->naluData[filter->recordAuBufferItem->naluCount] = filter->recordAuBufferItem->buffer + filter->recordAuBufferItem->auSize;
            filter->recordAuBufferItem->naluSize[filter->recordAuBufferItem->naluCount] = naluSize;
            filter->recordAuBufferItem->naluCount++;
        }
        filter->recordAuBufferItem->auSize += naluSize;
    }

    if ((filter->filterOutSpsPps) && ((naluType == ARSTREAM2_H264_NALU_TYPE_SPS) || (naluType == ARSTREAM2_H264_NALU_TYPE_PPS)))
    {
        filterOut = 1;
    }

    if ((filter->filterOutSei) && (naluType == ARSTREAM2_H264_NALU_TYPE_SEI))
    {
        filterOut = 1;
    }

    if (!filterOut)
    {
        if (filter->replaceStartCodesWithNaluSize)
        {
            // Replace the NAL unit 4 bytes start code with the NALU size
            *((uint32_t*)naluBuffer) = htonl((uint32_t)naluSize - 4);
        }

        filter->currentAuSize += naluSize;
    }
}


static int ARSTREAM2_H264Filter_enqueueCurrentAu(ARSTREAM2_H264Filter_t *filter)
{
    int ret = 0;
    int cancelAuOutput = 0, discarded = 0;
    struct timespec t1;
    uint64_t curTime = 0;

    ARSAL_Time_GetTime(&t1);
    curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

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
        discarded = 1;
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
        eARSTREAM2_ERROR cbRet = ARSTREAM2_OK;
        uint8_t *auBuffer = NULL;
        int auBufferSize = 0;
        void *auBufferUserPtr = NULL;

        filter->callbackInProgress = 1;
        if (filter->getAuBufferCallback)
        {
            /* call the getAuBufferCallback */
            ARSAL_Mutex_Unlock(&(filter->mutex));

            cbRet = filter->getAuBufferCallback(&auBuffer, &auBufferSize, &auBufferUserPtr, filter->getAuBufferCallbackUserPtr);

            ARSAL_Mutex_Lock(&(filter->mutex));
        }

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

            if (filter->auReadyCallback)
            {
                /* call the auReadyCallback */
                ARSAL_Mutex_Unlock(&(filter->mutex));

                cbRet = filter->auReadyCallback(auBuffer, auSize, filter->currentAuRtpTimestamp, filter->currentAuNtpTimestamp,
                                                filter->currentAuNtpTimestampLocal, filter->currentAuSyncType,
                                                (filter->currentAuMetadataSize > 0) ? filter->currentAuMetadata : NULL, filter->currentAuMetadataSize,
                                                (filter->currentAuUserDataSize > 0) ? filter->currentAuUserData : NULL, filter->currentAuUserDataSize,
                                                auBufferUserPtr, filter->auReadyCallbackUserPtr);

                ARSAL_Mutex_Lock(&(filter->mutex));
            }
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
                filter->currentAuOutputIndex++;
                ret = 1;
            }
        }
    }

    /* stream recording */
    if ((ret == 1) && (filter->recorder) && (filter->recordAuBufferItem) && (filter->recordAuBufferItem->auSize > 0))
    {
        ARSTREAM2_StreamRecorder_AccessUnit_t accessUnit;
        memset(&accessUnit, 0, sizeof(ARSTREAM2_StreamRecorder_AccessUnit_t));
        accessUnit.timestamp = filter->currentAuNtpTimestamp;
        accessUnit.index = filter->currentAuOutputIndex;
        if (filter->recordAuBufferItem->naluCount > 0)
        {
            accessUnit.naluCount = filter->recordAuBufferItem->naluCount;
            unsigned int i;
            for (i = 0; i < filter->recordAuBufferItem->naluCount; i++)
            {
                accessUnit.naluData[i] = filter->recordAuBufferItem->naluData[i];
                accessUnit.naluSize[i] = filter->recordAuBufferItem->naluSize[i];
            }
            accessUnit.auData = NULL;
            accessUnit.auSize = 0;
        }
        else
        {
            accessUnit.naluCount = 0;
            accessUnit.auData = filter->recordAuBufferItem->buffer;
            accessUnit.auSize = filter->recordAuBufferItem->auSize;
        }
        accessUnit.auSyncType = filter->currentAuSyncType;
        if ((filter->currentAuMetadataSize > 0) && ((unsigned)filter->currentAuMetadataSize <= filter->recordAuBufferItem->metadataBufferSize))
        {
            memcpy(filter->recordAuBufferItem->metadataBuffer, filter->currentAuMetadata, filter->currentAuMetadataSize);
            accessUnit.auMetadata = filter->recordAuBufferItem->metadataBuffer;
            accessUnit.auMetadataSize = filter->currentAuMetadataSize;
        }
        else
        {
            accessUnit.auMetadata = NULL;
            accessUnit.auMetadataSize = 0;
        }
        accessUnit.auUserPtr = filter->recordAuBufferItem;
        eARSTREAM2_ERROR err;
        err = ARSTREAM2_StreamRecorder_PushAccessUnit(filter->recorder, &accessUnit);
        if (err != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_StreamRecorder_PushAccessUnit() failed: %d (%s)",
                        err, ARSTREAM2_Error_ToString(err));
        }
        else
        {
            filter->recordAuBufferItem->pushed = 1;
        }
    }

    /* update the stats */
    if (filter->sync)
    {
        if ((filter->currentAuMacroblockStatus) && ((discarded) || (ret != 1)) && (filter->currentAuIsRef))
        {
            /* missed frame (missing non-ref frames are not counted as missing) */
            memset(filter->currentAuMacroblockStatus, ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_MISSING, filter->mbCount);
        }
        if (filter->currentAuMacroblockStatus)
        {
            /* update macroblock status and error second counters */
            int i, j, k;
            for (j = 0, k = 0; j < filter->mbHeight; j++)
            {
                for (i = 0; i < filter->mbWidth; i++, k++)
                {
                    int zone = j * ARSTREAM2_H264_FILTER_MB_STATUS_ZONE_COUNT / filter->mbHeight;
                    filter->stats.macroblockStatus[filter->currentAuMacroblockStatus[k]][zone]++;
                    if ((ret == 1) && (filter->currentAuMacroblockStatus[k] != ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_VALID_ISLICE)
                            && (filter->currentAuMacroblockStatus[k] != ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_VALID_PSLICE))
                    {
                        //TODO: we should not use curTime but an AU timestamp
                        if (curTime > filter->stats.errorSecondStartTime + 1000000)
                        {
                            filter->stats.errorSecondStartTime = curTime;
                            filter->stats.errorSecondCount++;
                        }
                        if (curTime > filter->stats.errorSecondStartTimeByZone[zone] + 1000000)
                        {
                            filter->stats.errorSecondStartTimeByZone[zone] = curTime;
                            filter->stats.errorSecondCountByZone[zone]++;
                        }
                    }
                }
            }
        }
        /* count all frames (totally missing non-ref frames are not counted) */
        filter->stats.totalFrameCount++;
        if (ret == 1)
        {
            /* count all output frames (including non-ref frames) */
            filter->stats.outputFrameCount++;
        }
        if (discarded)
        {
            /* count discarded frames (including partial missing non-ref frames) */
            filter->stats.discardedFrameCount++;
        }
        if (((discarded) || (ret != 1)) && (filter->currentAuIsRef))
        {
            /* count missed frames (missing non-ref frames are not counted as missing) */
            filter->stats.missedFrameCount++;
        }
        if (filter->lastStatsOutputTimestamp == 0)
        {
            /* init */
            filter->lastStatsOutputTimestamp = curTime;
        }
        if (curTime >= filter->lastStatsOutputTimestamp + ARSTREAM2_H264_FILTER_STATS_OUTPUT_INTERVAL)
        {
#ifdef ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT
            if (filter->fStatsOut)
            {
                int rssi = 0;
                if ((filter->currentAuMetadataSize >= 27) && (ntohs(*((uint16_t*)filter->currentAuMetadata)) == 0x5031))
                {
                    /* get the RSSI from the streaming metadata */
                    //TODO: remove this hack once we have a better way of getting the RSSI
                    rssi = (int8_t)filter->currentAuMetadata[26];
                }
                fprintf(filter->fStatsOut, "%llu %i %lu %lu %lu %lu %lu", (long long unsigned int)curTime, rssi,
                        (long unsigned int)filter->stats.totalFrameCount, (long unsigned int)filter->stats.outputFrameCount,
                        (long unsigned int)filter->stats.discardedFrameCount, (long unsigned int)filter->stats.missedFrameCount,
                        (long unsigned int)filter->stats.errorSecondCount);
                int i, j;
                for (i = 0; i < ARSTREAM2_H264_FILTER_MB_STATUS_ZONE_COUNT; i++)
                {
                    fprintf(filter->fStatsOut, " %lu", (long unsigned int)filter->stats.errorSecondCountByZone[i]);
                }
                for (j = 0; j < ARSTREAM2_H264_FILTER_MB_STATUS_CLASS_COUNT; j++)
                {
                    for (i = 0; i < ARSTREAM2_H264_FILTER_MB_STATUS_ZONE_COUNT; i++)
                    {
                        fprintf(filter->fStatsOut, " %lu", (long unsigned int)filter->stats.macroblockStatus[j][i]);
                    }
                }
                fprintf(filter->fStatsOut, "\n");
            }
#endif
            filter->lastStatsOutputTimestamp = curTime;
        }
        if (filter->currentAuIsRef)
        {
            /* reference frame => exchange macroblock status buffers */
            uint8_t *tmp = filter->currentAuMacroblockStatus;
            filter->currentAuMacroblockStatus = filter->currentAuRefMacroblockStatus;
            filter->currentAuRefMacroblockStatus = tmp;
        }
    }

    return ret;
}


static int ARSTREAM2_H264Filter_generateGrayIFrame(ARSTREAM2_H264Filter_t *filter, uint8_t *naluBuffer, int naluSize, uint8_t naluType)
{
    int ret = 0;
    eARSTREAM2_ERROR err = ARSTREAM2_OK;
    ARSTREAM2_H264_SliceContext_t *sliceContextNext = NULL;
    ARSTREAM2_H264_SliceContext_t sliceContext;

    if ((naluType != ARSTREAM2_H264_NALU_TYPE_SLICE_IDR) && (naluType != ARSTREAM2_H264_NALU_TYPE_SLICE))
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
        unsigned int outputSize;

        sliceContext.nal_ref_idc = 3;
        sliceContext.nal_unit_type = ARSTREAM2_H264_NALU_TYPE_SLICE_IDR;
        sliceContext.idrPicFlag = 1;
        sliceContext.slice_type = ARSTREAM2_H264_SLICE_TYPE_I;
        sliceContext.frame_num = 0;
        sliceContext.idr_pic_id = 0;
        sliceContext.no_output_of_prior_pics_flag = 0;
        sliceContext.long_term_reference_flag = 0;

        err = ARSTREAM2_H264Writer_WriteGrayISliceNalu(filter->writer, 0, filter->mbCount, (void*)&sliceContext, filter->tempSliceNaluBuffer, filter->tempSliceNaluBufferSize, &outputSize);
        if (err != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Writer_WriteGrayISliceNalu() failed (%d)", err);
            ret = -1;
        }
        else
        {
            uint8_t *tmpBuf = NULL;
            uint8_t *tmpBuf2 = NULL;
            int savedAuSize = filter->currentAuSize;
            int savedAuIncomplete = filter->currentAuIncomplete;
            eARSTREAM2_H264_FILTER_AU_SYNC_TYPE savedAuSyncType = filter->currentAuSyncType;
            int savedAuSlicesAllI = filter->currentAuSlicesAllI;
            int savedAuStreamingInfoAvailable = filter->currentAuStreamingInfoAvailable;
            int savedAuMetadataSize = filter->currentAuMetadataSize;
            int savedAuUserDataSize = filter->currentAuUserDataSize;
            int savedAuPreviousSliceIndex = filter->currentAuPreviousSliceIndex;
            int savedAuPreviousSliceFirstMb = filter->currentAuPreviousSliceFirstMb;
            int savedAuCurrentSliceFirstMb = filter->currentAuCurrentSliceFirstMb;
            uint32_t savedAuRtpTimestamp = filter->currentAuRtpTimestamp;
            uint64_t savedAuNtpTimestamp = filter->currentAuNtpTimestamp;
            uint64_t savedAuNtpTimestampLocal = filter->currentAuNtpTimestampLocal;
            int savedAuIsRef = filter->currentAuIsRef;
            int savedAuFrameNum = filter->currentAuFrameNum;
            uint64_t savedAuFirstNaluInputTime = filter->currentAuFirstNaluInputTime;
            unsigned int savedRecAuSize = 0;
            uint32_t savedNaluCount = 0;
            uint32_t savedNaluSize[ARSTREAM2_STREAM_RECORDER_NALU_MAX_COUNT];
            if (filter->recordAuBufferItem)
            {
                savedRecAuSize = filter->recordAuBufferItem->auSize;
                savedNaluCount = filter->recordAuBufferItem->naluCount;
                if (savedNaluCount > 0)
                {
                    unsigned int i;
                    for (i = 0; i < filter->recordAuBufferItem->naluCount; i++)
                    {
                        savedNaluSize[i] = filter->recordAuBufferItem->naluSize[i];
                    }
                }
            }
            ret = 0;

            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "Gray I slice NALU output size: %d", outputSize); //TODO: debug

            if (filter->currentAuSize + naluSize > 0)
            {
                tmpBuf = malloc(filter->currentAuSize + naluSize); //TODO
                if (tmpBuf)
                {
                    memcpy(tmpBuf, filter->currentAuBuffer, filter->currentAuSize + naluSize);
                }
            }
            if ((filter->recorder) && (filter->recordAuBufferItem) && (filter->recordAuBufferItem->auSize > 0))
            {
                tmpBuf2 = malloc(filter->recordAuBufferItem->auSize); //TODO
                if (tmpBuf2)
                {
                    memcpy(tmpBuf2, filter->recordAuBufferItem->buffer, filter->recordAuBufferItem->auSize);
                }
            }

            ARSTREAM2_H264Filter_resetCurrentAu(filter);
            filter->currentAuSyncType = ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_IDR;
            filter->currentAuRtpTimestamp -= ((filter->currentAuRtpTimestamp >= 90) ? 90 : ((filter->currentAuRtpTimestamp >= 1) ? 1 : 0));
            filter->currentAuNtpTimestamp -= ((filter->currentAuNtpTimestamp >= 1000) ? 1000 : ((filter->currentAuNtpTimestamp >= 1) ? 1 : 0));
            filter->currentAuNtpTimestampLocal -= ((filter->currentAuNtpTimestampLocal >= 1000) ? 1000 : ((filter->currentAuNtpTimestampLocal >= 1) ? 1 : 0));
            filter->currentAuFirstNaluInputTime -= ((filter->currentAuFirstNaluInputTime >= 1000) ? 1000 : ((filter->currentAuFirstNaluInputTime >= 1) ? 1 : 0));

            // Insert SPS+PPS before the I-frame
            if (ret == 0)
            {
                if ((filter->recorder) && (filter->recordAuBufferItem)
                        && (filter->recordAuBufferItem->auSize + filter->spsSize <= filter->recordAuBufferItem->bufferSize))
                {
                    memcpy(filter->recordAuBufferItem->buffer + filter->recordAuBufferItem->auSize, filter->pSps, filter->spsSize);
                    if (filter->recordAuBufferItem->naluCount < ARSTREAM2_STREAM_RECORDER_NALU_MAX_COUNT)
                    {
                        filter->recordAuBufferItem->naluData[filter->recordAuBufferItem->naluCount] = filter->recordAuBufferItem->buffer + filter->recordAuBufferItem->auSize;
                        filter->recordAuBufferItem->naluSize[filter->recordAuBufferItem->naluCount] = filter->spsSize;
                        filter->recordAuBufferItem->naluCount++;
                    }
                    filter->recordAuBufferItem->auSize += filter->spsSize;
                }
                if (!filter->filterOutSpsPps)
                {
                    if (filter->currentAuSize + filter->spsSize <= filter->currentAuBufferSize)
                    {
                        memcpy(filter->currentAuBuffer + filter->currentAuSize, filter->pSps, filter->spsSize);
                        if (filter->replaceStartCodesWithNaluSize)
                        {
                            // Replace the NAL unit 4 bytes start code with the NALU size
                            *((uint32_t*)(filter->currentAuBuffer + filter->currentAuSize)) = htonl((uint32_t)filter->spsSize - 4);
                        }
                        filter->currentAuSize += filter->spsSize;
                    }
                    else
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Access unit buffer is too small for the SPS NALU (size %d, access unit size %s)", filter->spsSize, filter->currentAuSize);
                        ret = -1;
                    }
                }
            }
            if (ret == 0)
            {
                if ((filter->recorder) && (filter->recordAuBufferItem)
                        && (filter->recordAuBufferItem->auSize + filter->ppsSize <= filter->recordAuBufferItem->bufferSize))
                {
                    memcpy(filter->recordAuBufferItem->buffer + filter->recordAuBufferItem->auSize, filter->pPps, filter->ppsSize);
                    if (filter->recordAuBufferItem->naluCount < ARSTREAM2_STREAM_RECORDER_NALU_MAX_COUNT)
                    {
                        filter->recordAuBufferItem->naluData[filter->recordAuBufferItem->naluCount] = filter->recordAuBufferItem->buffer + filter->recordAuBufferItem->auSize;
                        filter->recordAuBufferItem->naluSize[filter->recordAuBufferItem->naluCount] = filter->ppsSize;
                        filter->recordAuBufferItem->naluCount++;
                    }
                    filter->recordAuBufferItem->auSize += filter->ppsSize;
                }
                if (!filter->filterOutSpsPps)
                {
                    if (filter->currentAuSize + filter->ppsSize <= filter->currentAuBufferSize)
                    {
                        memcpy(filter->currentAuBuffer + filter->currentAuSize, filter->pPps, filter->ppsSize);
                        if (filter->replaceStartCodesWithNaluSize)
                        {
                            // Replace the NAL unit 4 bytes start code with the NALU size
                            *((uint32_t*)(filter->currentAuBuffer + filter->currentAuSize)) = htonl((uint32_t)filter->spsSize - 4);
                        }
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
                if ((filter->recorder) && (filter->recordAuBufferItem)
                        && (filter->recordAuBufferItem->auSize + outputSize <= filter->recordAuBufferItem->bufferSize))
                {
                    memcpy(filter->recordAuBufferItem->buffer + filter->recordAuBufferItem->auSize, filter->tempSliceNaluBuffer, outputSize);
                    if (filter->recordAuBufferItem->naluCount < ARSTREAM2_STREAM_RECORDER_NALU_MAX_COUNT)
                    {
                        filter->recordAuBufferItem->naluData[filter->recordAuBufferItem->naluCount] = filter->recordAuBufferItem->buffer + filter->recordAuBufferItem->auSize;
                        filter->recordAuBufferItem->naluSize[filter->recordAuBufferItem->naluCount] = outputSize;
                        filter->recordAuBufferItem->naluCount++;
                    }
                    filter->recordAuBufferItem->auSize += outputSize;
                }
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
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Access unit buffer is too small for the I-frame (size %d, access unit size %s)", outputSize, filter->currentAuSize);
                    ret = -1;
                }
            }

            // Output access unit
            if (ret == 0)
            {
                if (filter->currentAuMacroblockStatus)
                {
                    memset(filter->currentAuMacroblockStatus, ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_VALID_ISLICE, filter->mbCount);
                }
                ret = ARSTREAM2_H264Filter_enqueueCurrentAu(filter);

                if ((ret > 0) || (filter->auBufferChangePending))
                {
                    // The access unit has been enqueued or a buffer change is pending
                    ret = ARSTREAM2_H264Filter_getNewAuBuffer(filter);
                    if (ret == 0)
                    {
                        ARSTREAM2_H264Filter_resetCurrentAu(filter);
                        filter->currentAuRtpTimestamp = savedAuRtpTimestamp;
                        filter->currentAuNtpTimestamp = savedAuNtpTimestamp;
                        filter->currentAuNtpTimestampLocal = savedAuNtpTimestampLocal;
                        filter->currentAuFirstNaluInputTime = savedAuFirstNaluInputTime;
                        filter->currentAuSize = savedAuSize;
                        filter->currentAuIncomplete = savedAuIncomplete;
                        filter->currentAuSyncType = savedAuSyncType;
                        filter->currentAuSlicesAllI = savedAuSlicesAllI;
                        filter->currentAuStreamingInfoAvailable = savedAuStreamingInfoAvailable;
                        filter->currentAuMetadataSize = savedAuMetadataSize;
                        filter->currentAuUserDataSize = savedAuUserDataSize;
                        filter->currentAuPreviousSliceIndex = savedAuPreviousSliceIndex;
                        filter->currentAuPreviousSliceFirstMb = savedAuPreviousSliceFirstMb;
                        filter->currentAuCurrentSliceFirstMb = savedAuCurrentSliceFirstMb;
                        filter->currentAuIsRef = savedAuIsRef;
                        filter->currentAuFrameNum = savedAuFrameNum;
                        if (filter->recordAuBufferItem)
                        {
                            filter->recordAuBufferItem->auSize = savedRecAuSize;
                            filter->recordAuBufferItem->naluCount = savedNaluCount;
                            if (savedNaluCount > 0)
                            {
                                unsigned int i, offset;
                                for (i = 0, offset = 0; i < filter->recordAuBufferItem->naluCount; i++)
                                {
                                    filter->recordAuBufferItem->naluData[i] = filter->recordAuBufferItem->buffer + offset;
                                    filter->recordAuBufferItem->naluSize[i] = savedNaluSize[i];
                                    offset += savedNaluSize[i];
                                }
                            }
                        }

                        if (tmpBuf)
                        {
                            memcpy(filter->currentAuBuffer, tmpBuf, savedAuSize + naluSize);
                        }
                        if ((filter->recorder) && (filter->recordAuBufferItem) && (savedRecAuSize > 0) && (tmpBuf2))
                        {
                            memcpy(filter->recordAuBufferItem->buffer, tmpBuf2, savedRecAuSize);
                        }

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
                    filter->currentAuRtpTimestamp = savedAuRtpTimestamp;
                    filter->currentAuNtpTimestamp = savedAuNtpTimestamp;
                    filter->currentAuNtpTimestampLocal = savedAuNtpTimestampLocal;
                    filter->currentAuFirstNaluInputTime = savedAuFirstNaluInputTime;
                    filter->currentAuSize = savedAuSize;
                    filter->currentAuIncomplete = savedAuIncomplete;
                    filter->currentAuSyncType = savedAuSyncType;
                    filter->currentAuSlicesAllI = savedAuSlicesAllI;
                    filter->currentAuStreamingInfoAvailable = savedAuStreamingInfoAvailable;
                    filter->currentAuMetadataSize = savedAuMetadataSize;
                    filter->currentAuUserDataSize = savedAuUserDataSize;
                    filter->currentAuPreviousSliceIndex = savedAuPreviousSliceIndex;
                    filter->currentAuPreviousSliceFirstMb = savedAuPreviousSliceFirstMb;
                    filter->currentAuCurrentSliceFirstMb = savedAuCurrentSliceFirstMb;
                    filter->currentAuIsRef = savedAuIsRef;
                    filter->currentAuFrameNum = savedAuFrameNum;
                    if (filter->recordAuBufferItem)
                    {
                        filter->recordAuBufferItem->auSize = savedRecAuSize;
                        filter->recordAuBufferItem->naluCount = savedNaluCount;
                        if (savedNaluCount > 0)
                        {
                            unsigned int i, offset;
                            for (i = 0, offset = 0; i < filter->recordAuBufferItem->naluCount; i++)
                            {
                                filter->recordAuBufferItem->naluData[i] = filter->recordAuBufferItem->buffer + offset;
                                filter->recordAuBufferItem->naluSize[i] = savedNaluSize[i];
                                offset += savedNaluSize[i];
                            }
                        }
                    }

                    if (tmpBuf)
                    {
                        memcpy(filter->currentAuBuffer, tmpBuf, savedAuSize + naluSize);
                    }
                    if ((filter->recorder) && (filter->recordAuBufferItem) && (savedRecAuSize > 0) && (tmpBuf2))
                    {
                        memcpy(filter->recordAuBufferItem->buffer, tmpBuf2, savedRecAuSize);
                    }
                }
            }

            if (tmpBuf) free(tmpBuf);
            if (tmpBuf2) free(tmpBuf2);
        }
    }

    return ret;
}


static int ARSTREAM2_H264Filter_fillMissingSlices(ARSTREAM2_H264Filter_t *filter, uint8_t *naluBuffer, int naluSize, uint8_t naluType, uint8_t sliceType, int isFirstNaluInAu, uint64_t auTimestamp, int *offset)
{
    int missingMb = 0, firstMbInSlice = 0, ret = 0;

    if (isFirstNaluInAu)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "#%d AUTS:%llu Missing NALU is probably on previous AU => OK", filter->currentAuOutputIndex, auTimestamp); //TODO: debug
        if (filter->currentAuCurrentSliceFirstMb == 0)
        {
            filter->currentAuPreviousSliceFirstMb = 0;
            filter->currentAuPreviousSliceIndex = 0;
        }
        return 0;
    }
    else if (((naluType != ARSTREAM2_H264_NALU_TYPE_SLICE_IDR) && (naluType != ARSTREAM2_H264_NALU_TYPE_SLICE)) || (filter->currentAuCurrentSliceFirstMb == 0))
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "#%d AUTS:%llu Missing NALU is probably a SPS, PPS or SEI or on previous AU => OK", filter->currentAuOutputIndex, auTimestamp); //TODO: debug
        if (filter->currentAuCurrentSliceFirstMb == 0)
        {
            filter->currentAuPreviousSliceFirstMb = 0;
            filter->currentAuPreviousSliceIndex = 0;
        }
        return 0;
    }

    if (filter->sync)
    {
        if (filter->currentAuStreamingInfoAvailable)
        {
            if (filter->currentAuPreviousSliceIndex < 0)
            {
                // No previous slice received
                if (filter->currentAuCurrentSliceFirstMb > 0)
                {
                    firstMbInSlice = 0;
                    missingMb = filter->currentAuCurrentSliceFirstMb;
                    ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "#%d AUTS:%llu currentSliceFirstMb:%d missingMb:%d",
                                filter->currentAuOutputIndex, auTimestamp, filter->currentAuCurrentSliceFirstMb, missingMb); //TODO: debug
                }
                else
                {
                    ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "#%d AUTS:%llu previousSliceIdx:%d currentSliceFirstMb:%d Error: this should not happen!",
                                filter->currentAuOutputIndex, auTimestamp, filter->currentAuPreviousSliceIndex, filter->currentAuCurrentSliceFirstMb); //TODO: debug
                    missingMb = 0;
                    ret = -1;
                }
            }
            else if ((filter->currentAuCurrentSliceFirstMb > filter->currentAuPreviousSliceFirstMb + filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex]))
            {
                // Slices have been received before
                firstMbInSlice = filter->currentAuPreviousSliceFirstMb + filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex];
                missingMb = filter->currentAuCurrentSliceFirstMb - firstMbInSlice;
                ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "#%d AUTS:%llu previousSliceFirstMb:%d previousSliceMbCount:%d currentSliceFirstMb:%d missingMb:%d firstMbInSlice:%d",
                            filter->currentAuOutputIndex, auTimestamp, filter->currentAuPreviousSliceFirstMb, filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex], filter->currentAuCurrentSliceFirstMb, missingMb, firstMbInSlice); //TODO: debug
            }
            else
            {
                ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "#%d AUTS:%llu previousSliceFirstMb:%d previousSliceMbCount:%d currentSliceFirstMb:%d Error: this should not happen!",
                            filter->currentAuOutputIndex, auTimestamp, filter->currentAuPreviousSliceFirstMb, filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex], filter->currentAuCurrentSliceFirstMb); //TODO: debug
                missingMb = 0;
                ret = -1;
            }
        }
        else
        {
            /* macroblock status */
            if ((filter->currentAuCurrentSliceFirstMb > 0) && (filter->currentAuMacroblockStatus))
            {
                if (!filter->currentAuSlicesReceived)
                {
                    // No previous slice received
                    firstMbInSlice = 0;
                    missingMb = filter->currentAuCurrentSliceFirstMb;
                }
                else if ((filter->currentAuInferredPreviousSliceFirstMb >= 0) && (filter->currentAuInferredSliceMbCount > 0))
                {
                    // Slices have been received before
                    firstMbInSlice = filter->currentAuInferredPreviousSliceFirstMb + filter->currentAuInferredSliceMbCount;
                    missingMb = filter->currentAuCurrentSliceFirstMb - firstMbInSlice;
                }
                if (missingMb > 0)
                {
                    if (firstMbInSlice + missingMb > filter->mbCount) missingMb = filter->mbCount - firstMbInSlice;
                    memset(filter->currentAuMacroblockStatus + firstMbInSlice,
                           ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_MISSING, missingMb);
                }
            }
        }
    }

    if (ret == 0)
    {
        if (!filter->generateSkippedPSlices)
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "#%d AUTS:%llu Missing NALU is probably a slice", filter->currentAuOutputIndex, auTimestamp); //TODO: debug
            if (missingMb > 0)
            {
                if (firstMbInSlice + missingMb > filter->mbCount) missingMb = filter->mbCount - firstMbInSlice;
                memset(filter->currentAuMacroblockStatus + firstMbInSlice,
                       ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_MISSING, missingMb);
            }
            ret = -2;
        }
    }

    if (ret == 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "#%d AUTS:%llu Missing NALU is probably a slice", filter->currentAuOutputIndex, auTimestamp); //TODO: debug

        if (!filter->sync)
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "#%d AUTS:%llu No sync, abort", filter->currentAuOutputIndex, auTimestamp); //TODO: debug
            ret = -2;
        }
    }

    if (ret == 0)
    {
        if (!filter->currentAuStreamingInfoAvailable)
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "#%d AUTS:%llu Streaming info is not available", filter->currentAuOutputIndex, auTimestamp); //TODO: debug
            if (missingMb > 0)
            {
                if (firstMbInSlice + missingMb > filter->mbCount) missingMb = filter->mbCount - firstMbInSlice;
                memset(filter->currentAuMacroblockStatus + firstMbInSlice,
                       ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_MISSING, missingMb);
            }
            ret = -2;
        }
    }

    if (ret == 0)
    {
        if (sliceType != ARSTREAM2_H264_SLICE_TYPE_P)
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "#%d AUTS:%llu Current slice is not a P-slice, aborting", filter->currentAuOutputIndex, auTimestamp); //TODO: debug
            if (missingMb > 0)
            {
                if (firstMbInSlice + missingMb > filter->mbCount) missingMb = filter->mbCount - firstMbInSlice;
                memset(filter->currentAuMacroblockStatus + firstMbInSlice,
                       ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_MISSING, missingMb);
            }
            ret = -2;
        }
    }

    if ((ret == 0) && (missingMb > 0))
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
                ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "#%d AUTS:%llu Skipped P slice NALU output size: %d", filter->currentAuOutputIndex, auTimestamp, outputSize); //TODO: debug
                if ((filter->recorder) && (filter->recordAuBufferItem)
                        && (filter->recordAuBufferItem->auSize + outputSize <= filter->recordAuBufferItem->bufferSize))
                {
                    memcpy(filter->recordAuBufferItem->buffer + filter->recordAuBufferItem->auSize, filter->tempSliceNaluBuffer, outputSize);
                    if (filter->recordAuBufferItem->naluCount < ARSTREAM2_STREAM_RECORDER_NALU_MAX_COUNT)
                    {
                        filter->recordAuBufferItem->naluData[filter->recordAuBufferItem->naluCount] = filter->recordAuBufferItem->buffer + filter->recordAuBufferItem->auSize;
                        filter->recordAuBufferItem->naluSize[filter->recordAuBufferItem->naluCount] = outputSize;
                        filter->recordAuBufferItem->naluCount++;
                    }
                    filter->recordAuBufferItem->auSize += outputSize;
                }
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
                    if (offset) *offset = outputSize;
                    if (filter->currentAuMacroblockStatus)
                    {
                        if (firstMbInSlice + missingMb > filter->mbCount) missingMb = filter->mbCount - firstMbInSlice;
                        memset(filter->currentAuMacroblockStatus + firstMbInSlice,
                               ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_MISSING_CONCEALED, missingMb);
                    }
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


static int ARSTREAM2_H264Filter_fillMissingEndOfFrame(ARSTREAM2_H264Filter_t *filter, uint8_t sliceType, uint64_t auTimestamp)
{
    int missingMb = 0, firstMbInSlice = 0, ret = 0;

    if (filter->sync)
    {
        if (filter->currentAuStreamingInfoAvailable)
        {
            if (filter->currentAuPreviousSliceIndex < 0)
            {
                // No previous slice received
                firstMbInSlice = 0;
                missingMb = filter->mbCount;

                //TODO: slice context
                //UNSUPPORTED
                ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "#%d AUTS:%llu No previous slice received, aborting", filter->currentAuOutputIndex, auTimestamp); //TODO: debug
                ret = -1;
            }
            else
            {
                // Slices have been received before
                firstMbInSlice = filter->currentAuPreviousSliceFirstMb + filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex];
                missingMb = filter->mbCount - firstMbInSlice;
                ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "#%d AUTS:%llu missingMb:%d firstMbInSlice:%d", filter->currentAuOutputIndex, auTimestamp, missingMb, firstMbInSlice); //TODO: debug
            }
        }
        else
        {
            /* macroblock status */
            if (filter->currentAuMacroblockStatus)
            {
                if (!filter->currentAuSlicesReceived)
                {
                    // No previous slice received
                    firstMbInSlice = 0;
                    missingMb = filter->mbCount;
                }
                else
                {
                    // Slices have been received before
                    firstMbInSlice = filter->currentAuInferredPreviousSliceFirstMb + filter->currentAuInferredSliceMbCount;
                    missingMb = filter->mbCount - firstMbInSlice;
                }
                if (missingMb > 0)
                {
                    if (firstMbInSlice + missingMb > filter->mbCount) missingMb = filter->mbCount - firstMbInSlice;
                    memset(filter->currentAuMacroblockStatus + firstMbInSlice,
                           ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_MISSING, missingMb);
                }
            }
        }
    }

    if (ret == 0)
    {
        if (!filter->generateSkippedPSlices)
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "#%d AUTS:%llu Missing NALU is probably a slice", filter->currentAuOutputIndex, auTimestamp); //TODO: debug
            if (missingMb > 0)
            {
                if (firstMbInSlice + missingMb > filter->mbCount) missingMb = filter->mbCount - firstMbInSlice;
                memset(filter->currentAuMacroblockStatus + firstMbInSlice,
                       ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_MISSING, missingMb);
            }
            ret = -2;
        }
    }

    if (ret == 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "#%d AUTS:%llu Missing NALU is probably a slice", filter->currentAuOutputIndex, auTimestamp); //TODO: debug

        if (!filter->sync)
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "#%d AUTS:%llu No sync, abort", filter->currentAuOutputIndex, auTimestamp); //TODO: debug
            ret = -2;
        }
    }

    if (ret == 0)
    {
        if (!filter->currentAuStreamingInfoAvailable)
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "#%d AUTS:%llu Streaming info is not available", filter->currentAuOutputIndex, auTimestamp); //TODO: debug
            if (missingMb > 0)
            {
                if (firstMbInSlice + missingMb > filter->mbCount) missingMb = filter->mbCount - firstMbInSlice;
                memset(filter->currentAuMacroblockStatus + firstMbInSlice,
                       ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_MISSING, missingMb);
            }
            ret = -2;
        }
    }

    if (ret == 0)
    {
        if (sliceType != ARSTREAM2_H264_SLICE_TYPE_P)
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "#%d AUTS:%llu Previous slice is not a P-slice, aborting", filter->currentAuOutputIndex, auTimestamp); //TODO: debug
            if (missingMb > 0)
            {
                if (firstMbInSlice + missingMb > filter->mbCount) missingMb = filter->mbCount - firstMbInSlice;
                memset(filter->currentAuMacroblockStatus + firstMbInSlice,
                       ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_MISSING, missingMb);
            }
            ret = -2;
        }
    }

    if ((ret == 0) && (missingMb > 0))
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
                ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "#%d AUTS:%llu Skipped P slice NALU output size: %d", filter->currentAuOutputIndex, auTimestamp, outputSize); //TODO: debug
                if ((filter->recorder) && (filter->recordAuBufferItem)
                        && (filter->recordAuBufferItem->auSize + outputSize <= filter->recordAuBufferItem->bufferSize))
                {
                    memcpy(filter->recordAuBufferItem->buffer + filter->recordAuBufferItem->auSize, filter->tempSliceNaluBuffer, outputSize);
                    if (filter->recordAuBufferItem->naluCount < ARSTREAM2_STREAM_RECORDER_NALU_MAX_COUNT)
                    {
                        filter->recordAuBufferItem->naluData[filter->recordAuBufferItem->naluCount] = filter->recordAuBufferItem->buffer + filter->recordAuBufferItem->auSize;
                        filter->recordAuBufferItem->naluSize[filter->recordAuBufferItem->naluCount] = outputSize;
                        filter->recordAuBufferItem->naluCount++;
                    }
                    filter->recordAuBufferItem->auSize += outputSize;
                }
                if (filter->currentAuSize + (int)outputSize <= filter->currentAuBufferSize)
                {
                    if (filter->replaceStartCodesWithNaluSize)
                    {
                        // Replace the NAL unit 4 bytes start code with the NALU size
                        *((uint32_t*)filter->tempSliceNaluBuffer) = htonl((uint32_t)outputSize - 4);
                    }
                    memcpy(filter->currentAuBuffer + filter->currentAuSize, filter->tempSliceNaluBuffer, outputSize);
                    filter->currentAuSize += outputSize;
                    if (filter->currentAuMacroblockStatus)
                    {
                        if (firstMbInSlice + missingMb > filter->mbCount) missingMb = filter->mbCount - firstMbInSlice;
                        memset(filter->currentAuMacroblockStatus + firstMbInSlice,
                               ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_MISSING_CONCEALED, missingMb);
                    }
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


uint8_t* ARSTREAM2_H264Filter_RtpReceiverNaluCallback(eARSTREAM2_RTP_RECEIVER_CAUSE cause, uint8_t *naluBuffer, int naluSize, uint32_t auRtpTimestamp,
                                                      uint64_t auNtpTimestamp, uint64_t auNtpTimestampLocal, uint8_t *naluMetadata, int naluMetadataSize,
                                                      int isFirstNaluInAu, int isLastNaluInAu, int missingPacketsBefore, int *newNaluBufferSize, void *custom)
{
    ARSTREAM2_H264Filter_t* filter = (ARSTREAM2_H264Filter_t*)custom;
    uint8_t naluType = ARSTREAM2_H264_NALU_TYPE_UNKNOWN;
    uint8_t sliceType = ARSTREAM2_H264_SLICE_TYPE_NON_VCL;
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
                    && ((isFirstNaluInAu) || ((filter->currentAuRtpTimestamp != 0) && (auRtpTimestamp != filter->currentAuRtpTimestamp))))
            {
                uint8_t *tmpBuf = malloc(naluSize); //TODO
                if (tmpBuf)
                {
                    memcpy(tmpBuf, naluBuffer, naluSize);
                }

                // Fill the missing slices with fake bitstream
                ret = ARSTREAM2_H264Filter_fillMissingEndOfFrame(filter, filter->previousSliceType, filter->currentAuNtpTimestamp);
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

            ret = -1; //ARSTREAM2_H264Filter_ParseNalu(filter, naluBuffer, naluSize, &naluType, &sliceType);
            if (ret < 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_ParseNalu() failed (%d)", ret);
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
                filter->currentAuRtpTimestamp = auRtpTimestamp;
                filter->currentAuNtpTimestamp = auNtpTimestamp;
                filter->currentAuNtpTimestampLocal = auNtpTimestampLocal;
                if (filter->currentAuFirstNaluInputTime == 0) filter->currentAuFirstNaluInputTime = curTime;
                filter->previousSliceType = sliceType;
                if ((naluMetadataSize > 0) && (naluMetadataSize <= ARSTREAM2_H264_FILTER_AU_METADATA_BUFFER_SIZE))
                {
                    memcpy(filter->currentAuMetadata, naluMetadata, naluMetadataSize);
                    filter->currentAuMetadataSize = naluMetadataSize;
                }

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
                        int offset = 0;
                        ret = ARSTREAM2_H264Filter_fillMissingSlices(filter, naluBuffer, naluSize, naluType, sliceType, isFirstNaluInAu, filter->currentAuNtpTimestamp, &offset);
                        if (ret < 0)
                        {
                            if (ret != -2)
                            {
                                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_fillMissingSlices() failed (%d)", ret);
                            }
                            filter->currentAuIncomplete = 1;
                        }
                        else
                        {
                            naluBuffer += offset;
                        }
                    }
                    ARSTREAM2_H264Filter_updateCurrentAu(filter, naluType, sliceType);

                    ARSTREAM2_H264Filter_addNaluToCurrentAu(filter, naluType, naluBuffer, naluSize);

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


eARSTREAM2_ERROR ARSTREAM2_H264Filter_StartRecorder(ARSTREAM2_H264Filter_Handle filterHandle, const char *recordFileName)
{
    ARSTREAM2_H264Filter_t* filter = (ARSTREAM2_H264Filter_t*)filterHandle;
    int ret = ARSTREAM2_OK;

    if (!filterHandle)
    {
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if ((!recordFileName) || (!strlen(recordFileName)))
    {
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (filter->recorder)
    {
        return ARSTREAM2_ERROR_INVALID_STATE;
    }

    filter->recordFileName = strdup(recordFileName);
    if (!filter->recordFileName)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "String allocation failed");
        ret = ARSTREAM2_ERROR_ALLOC;
    }
    else
    {
        ARSAL_Mutex_Lock(&(filter->mutex));
        if (filter->sync)
        {
            filter->recorderStartPending = 0;
            ARSAL_Mutex_Unlock(&(filter->mutex));
            int recRet;
            recRet = ARSTREAM2_H264Filter_StreamRecorderInit(filter);
            if (recRet != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_StreamRecorderInit() failed (%d)", recRet);
            }
        }
        else
        {
            filter->recorderStartPending = 1;
            ARSAL_Mutex_Unlock(&(filter->mutex));
        }
    }

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_H264Filter_StopRecorder(ARSTREAM2_H264Filter_Handle filterHandle)
{
    ARSTREAM2_H264Filter_t* filter = (ARSTREAM2_H264Filter_t*)filterHandle;
    int ret = ARSTREAM2_OK;

    if (!filterHandle)
    {
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!filter->recorder)
    {
        return ARSTREAM2_ERROR_INVALID_STATE;
    }

    int recRet = ARSTREAM2_H264Filter_StreamRecorderStop(filter);
    if (recRet != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_StreamRecorderStop() failed (%d)", recRet);
        ret = ARSTREAM2_ERROR_INVALID_STATE;
    }

    recRet = ARSTREAM2_H264Filter_StreamRecorderFree(filter);
    if (recRet != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_StreamRecorderFree() failed (%d)", recRet);
        ret = ARSTREAM2_ERROR_INVALID_STATE;
    }

    if (filter->recordFileName) free(filter->recordFileName);
    filter->recordFileName = NULL;

    return ret;
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


eARSTREAM2_ERROR ARSTREAM2_H264Filter_GetFrameMacroblockStatus(ARSTREAM2_H264Filter_Handle filterHandle, uint8_t **macroblocks, int *mbWidth, int *mbHeight)
{
    ARSTREAM2_H264Filter_t* filter = (ARSTREAM2_H264Filter_t*)filterHandle;
    int ret = ARSTREAM2_OK;

    if (!filterHandle)
    {
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    if ((!macroblocks) || (!mbWidth) || (!mbHeight))
    {
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    ARSAL_Mutex_Lock(&(filter->mutex));

    if (!filter->sync)
    {
        ret = ARSTREAM2_ERROR_WAITING_FOR_SYNC;
    }

    if (!filter->currentAuMacroblockStatus)
    {
        ret = ARSTREAM2_ERROR_RESOURCE_UNAVAILABLE;
    }

    if (ret == ARSTREAM2_OK)
    {
        *macroblocks = filter->currentAuMacroblockStatus;
        *mbWidth = filter->mbWidth;
        *mbHeight = filter->mbHeight;
    }

    ARSAL_Mutex_Unlock(&(filter->mutex));

    return ret;
}


void ARSTREAM2_H264Filter_ResetAu(ARSTREAM2_H264Filter_t *filter)
{
    filter->currentAuIncomplete = 0;
    filter->currentAuSyncType = ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_NONE;
    filter->currentAuSlicesAllI = 1;
    filter->currentAuSlicesReceived = 0;
    filter->currentAuStreamingInfoAvailable = 0;
    memset(&filter->currentAuStreamingInfo, 0, sizeof(ARSTREAM2_H264Sei_ParrotStreamingV1_t));
    filter->currentAuMetadataSize = 0;
    memset(filter->currentAuMetadata, 0, sizeof(ARSTREAM2_H264_FILTER_AU_METADATA_BUFFER_SIZE));
    filter->currentAuUserDataSize = 0;
    memset(filter->currentAuUserData, 0, sizeof(ARSTREAM2_H264_FILTER_AU_USER_DATA_BUFFER_SIZE));
    filter->currentAuPreviousSliceIndex = -1;
    filter->currentAuPreviousSliceFirstMb = 0;
    filter->currentAuInferredPreviousSliceFirstMb = 0;
    filter->currentAuCurrentSliceFirstMb = -1;
    filter->previousSliceType = ARSTREAM2_H264_SLICE_TYPE_NON_VCL;
    if (filter->currentAuMacroblockStatus)
    {
        memset(filter->currentAuMacroblockStatus, ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_UNKNOWN, filter->mbCount);
    }
    if (filter->currentAuIsRef) filter->previousAuFrameNum = filter->currentAuFrameNum;
    filter->currentAuFrameNum = -1;
    filter->currentAuIsRef = 0;
}


static int ARSTREAM2_H264Filter_ProcessNalu(ARSTREAM2_H264Filter_t *filter, ARSTREAM2_H264_NaluFifoItem_t *naluItem)
{
    int ret = 0;

    if ((naluItem->nalu.naluType == ARSTREAM2_H264_NALU_TYPE_SLICE_IDR) || (naluItem->nalu.naluType == ARSTREAM2_H264_NALU_TYPE_SLICE))
    {
        int sliceMbCount = 0, sliceFirstMb = 0;
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
            sliceFirstMb = filter->currentAuPreviousSliceFirstMb = filter->currentAuCurrentSliceFirstMb;
            sliceMbCount = filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex];
            //ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_H264_FILTER_TAG, "previousSliceIndex: %d - previousSliceFirstMb: %d", filter->currentAuPreviousSliceIndex, filter->currentAuCurrentSliceFirstMb); //TODO: debug
        }
        else if (filter->currentAuCurrentSliceFirstMb >= 0)
        {
            sliceFirstMb = filter->currentAuInferredPreviousSliceFirstMb = filter->currentAuCurrentSliceFirstMb;
            sliceMbCount = (filter->currentAuInferredSliceMbCount > 0) ? filter->currentAuInferredSliceMbCount : 0;
        }
        if ((filter->currentAuMacroblockStatus) && (sliceMbCount > 0))
        {
            int i, idx;
            uint8_t status;
            if (sliceFirstMb + sliceMbCount > filter->mbCount) sliceMbCount = filter->mbCount - sliceFirstMb;
            for (i = 0, idx = sliceFirstMb; i < sliceMbCount; i++, idx++)
            {
                if (naluItem->nalu.sliceType == ARSTREAM2_H264_SLICE_TYPE_I)
                {
                    status = ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_VALID_ISLICE;
                }
                else
                {
                    if ((!filter->currentAuRefMacroblockStatus)
                            || ((filter->currentAuRefMacroblockStatus[idx] != ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_VALID_ISLICE) && (filter->currentAuRefMacroblockStatus[idx] != ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_VALID_PSLICE)))
                    {
                        status = ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_ERROR_PROPAGATION;
                    }
                    else
                    {
                        status = ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_VALID_PSLICE;
                    }
                }
                filter->currentAuMacroblockStatus[idx] = status;
            }
        }
    }

    return ret;
}


int ARSTREAM2_H264Filter_OutputAu(ARSTREAM2_H264Filter_t *filter, ARSTREAM2_H264_AuFifoItem_t *auItem, int *auLocked)
{
    ARSTREAM2_H264_AccessUnit_t *au = &auItem->au;
    int ret = 0;
    int cancelAuOutput = 0, discarded = 0;
    struct timespec t1;
    uint64_t curTime = 0;

    ARSAL_Time_GetTime(&t1);
    curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

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
        /* filter out incomplete access units */
        cancelAuOutput = 1;
        discarded = 1;
        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "AU output cancelled (!outputIncompleteAu)"); //TODO: debug
    }

    if (!cancelAuOutput)
    {
        ARSAL_Mutex_Lock(&(filter->mutex));

        if ((filter->waitForSync) && (!filter->sync))
        {
            /* cancel if not synchronized */
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
        eARSTREAM2_ERROR cbRet = ARSTREAM2_OK;
        uint8_t *auBuffer = NULL;
        int auBufferSize = 0;
        void *auBufferUserPtr = NULL;

        filter->callbackInProgress = 1;
        if (filter->getAuBufferCallback)
        {
            /* call the getAuBufferCallback */
            ARSAL_Mutex_Unlock(&(filter->mutex));

            cbRet = filter->getAuBufferCallback(&auBuffer, &auBufferSize, &auBufferUserPtr, filter->getAuBufferCallbackUserPtr);

            ARSAL_Mutex_Lock(&(filter->mutex));
        }

        if ((cbRet != ARSTREAM2_OK) || (!auBuffer) || (auBufferSize <= 0))
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "getAuBufferCallback failed: %s", ARSTREAM2_Error_ToString(cbRet));
            filter->callbackInProgress = 0;
            ARSAL_Mutex_Unlock(&(filter->mutex));
            ARSAL_Cond_Signal(&(filter->callbackCond));
            ret = -1;
        }
        else
        {
            ARSTREAM2_H264_NaluFifoItem_t *naluItem;
            unsigned int auSize = 0;

            for (naluItem = au->naluHead; naluItem; naluItem = naluItem->next)
            {
                /* filter out unwanted NAL units */
                if ((filter->filterOutSpsPps) && ((naluItem->nalu.naluType == ARSTREAM2_H264_NALU_TYPE_SPS) || (naluItem->nalu.naluType == ARSTREAM2_H264_NALU_TYPE_PPS)))
                {
                    continue;
                }

                if ((filter->filterOutSei) && (naluItem->nalu.naluType == ARSTREAM2_H264_NALU_TYPE_SEI))
                {
                    continue;
                }

                /* copy to output buffer */
                if (auSize + naluItem->nalu.naluSize <= (unsigned)auBufferSize)
                {
                    memcpy(auBuffer + auSize, naluItem->nalu.nalu, naluItem->nalu.naluSize);

                    if ((naluItem->nalu.naluSize >= 4) && (filter->replaceStartCodesWithNaluSize))
                    {
                        /* replace the NAL unit 4 bytes start code with the NALU size */
                        *(auBuffer + auSize + 0) = ((naluItem->nalu.naluSize - 4) >> 24) & 0xFF;
                        *(auBuffer + auSize + 1) = ((naluItem->nalu.naluSize - 4) >> 16) & 0xFF;
                        *(auBuffer + auSize + 2) = ((naluItem->nalu.naluSize - 4) >>  8) & 0xFF;
                        *(auBuffer + auSize + 3) = ((naluItem->nalu.naluSize - 4) >>  0) & 0xFF;
                    }

                    auSize += naluItem->nalu.naluSize;
                }
            }

            ARSAL_Time_GetTime(&t1);
            curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

            if (filter->auReadyCallback)
            {
                /* call the auReadyCallback */
                ARSAL_Mutex_Unlock(&(filter->mutex));

                cbRet = filter->auReadyCallback(auBuffer, auSize, au->rtpTimestamp, au->ntpTimestamp,
                                                au->ntpTimestampLocal, filter->currentAuSyncType,
                                                (au->metadataSize > 0) ? au->metadataBuffer : NULL, au->metadataSize,
                                                (au->userDataSize > 0) ? au->userDataBuffer : NULL, au->userDataSize,
                                                auBufferUserPtr, filter->auReadyCallbackUserPtr);

                ARSAL_Mutex_Lock(&(filter->mutex));
            }
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
                filter->currentAuOutputIndex++;
                ret = 1;
            }
        }
    }

    /* stream recording */
    if ((ret == 1) && (filter->recorder))
    {
        int i;
        ARSTREAM2_H264_NaluFifoItem_t *naluItem;
        ARSTREAM2_StreamRecorder_AccessUnit_t accessUnit;
        memset(&accessUnit, 0, sizeof(ARSTREAM2_StreamRecorder_AccessUnit_t));
        accessUnit.auUserPtr = auItem;
        accessUnit.timestamp = au->ntpTimestamp;
        accessUnit.index = filter->currentAuOutputIndex;
        accessUnit.auSyncType = filter->currentAuSyncType;
        accessUnit.auMetadata = au->metadataBuffer;
        accessUnit.auMetadataSize = au->metadataSize;
        accessUnit.auData = NULL;
        accessUnit.auSize = 0;
        accessUnit.naluCount = au->naluCount;
        for (naluItem = au->naluHead, i = 0; naluItem; naluItem = naluItem->next, i++)
        {
            accessUnit.naluData[i] = naluItem->nalu.nalu;
            accessUnit.naluSize[i] = naluItem->nalu.naluSize;
        }

        eARSTREAM2_ERROR err;
        err = ARSTREAM2_StreamRecorder_PushAccessUnit(filter->recorder, &accessUnit);
        if (err != ARSTREAM2_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_StreamRecorder_PushAccessUnit() failed: %d (%s)",
                        err, ARSTREAM2_Error_ToString(err));
            *auLocked = 0;
        }
        else
        {
            *auLocked = 1;
        }
    }
    else
    {
        *auLocked = 0;
    }

    /* update the stats */
    if (filter->sync)
    {
        if ((filter->currentAuMacroblockStatus) && ((discarded) || (ret != 1)) && (filter->currentAuIsRef))
        {
            /* missed frame (missing non-ref frames are not counted as missing) */
            memset(filter->currentAuMacroblockStatus, ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_MISSING, filter->mbCount);
        }
        if (filter->currentAuMacroblockStatus)
        {
            /* update macroblock status and error second counters */
            int i, j, k;
            for (j = 0, k = 0; j < filter->mbHeight; j++)
            {
                for (i = 0; i < filter->mbWidth; i++, k++)
                {
                    int zone = j * ARSTREAM2_H264_FILTER_MB_STATUS_ZONE_COUNT / filter->mbHeight;
                    filter->stats.macroblockStatus[filter->currentAuMacroblockStatus[k]][zone]++;
                    if ((ret == 1) && (filter->currentAuMacroblockStatus[k] != ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_VALID_ISLICE)
                            && (filter->currentAuMacroblockStatus[k] != ARSTREAM2_H264_FILTER_MACROBLOCK_STATUS_VALID_PSLICE))
                    {
                        //TODO: we should not use curTime but an AU timestamp
                        if (curTime > filter->stats.errorSecondStartTime + 1000000)
                        {
                            filter->stats.errorSecondStartTime = curTime;
                            filter->stats.errorSecondCount++;
                        }
                        if (curTime > filter->stats.errorSecondStartTimeByZone[zone] + 1000000)
                        {
                            filter->stats.errorSecondStartTimeByZone[zone] = curTime;
                            filter->stats.errorSecondCountByZone[zone]++;
                        }
                    }
                }
            }
        }
        /* count all frames (totally missing non-ref frames are not counted) */
        filter->stats.totalFrameCount++;
        if (ret == 1)
        {
            /* count all output frames (including non-ref frames) */
            filter->stats.outputFrameCount++;
        }
        if (discarded)
        {
            /* count discarded frames (including partial missing non-ref frames) */
            filter->stats.discardedFrameCount++;
        }
        if (((discarded) || (ret != 1)) && (filter->currentAuIsRef))
        {
            /* count missed frames (missing non-ref frames are not counted as missing) */
            filter->stats.missedFrameCount++;
        }
        if (filter->lastStatsOutputTimestamp == 0)
        {
            /* init */
            filter->lastStatsOutputTimestamp = curTime;
        }
        if (curTime >= filter->lastStatsOutputTimestamp + ARSTREAM2_H264_FILTER_STATS_OUTPUT_INTERVAL)
        {
#ifdef ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT
            if (filter->fStatsOut)
            {
                int rssi = 0;
                if ((au->metadataSize >= 27) && (ntohs(*((uint16_t*)au->metadataBuffer)) == 0x5031))
                {
                    /* get the RSSI from the streaming metadata */
                    //TODO: remove this hack once we have a better way of getting the RSSI
                    rssi = (int8_t)au->metadataBuffer[26];
                }
                fprintf(filter->fStatsOut, "%llu %i %lu %lu %lu %lu %lu", (long long unsigned int)curTime, rssi,
                        (long unsigned int)filter->stats.totalFrameCount, (long unsigned int)filter->stats.outputFrameCount,
                        (long unsigned int)filter->stats.discardedFrameCount, (long unsigned int)filter->stats.missedFrameCount,
                        (long unsigned int)filter->stats.errorSecondCount);
                int i, j;
                for (i = 0; i < ARSTREAM2_H264_FILTER_MB_STATUS_ZONE_COUNT; i++)
                {
                    fprintf(filter->fStatsOut, " %lu", (long unsigned int)filter->stats.errorSecondCountByZone[i]);
                }
                for (j = 0; j < ARSTREAM2_H264_FILTER_MB_STATUS_CLASS_COUNT; j++)
                {
                    for (i = 0; i < ARSTREAM2_H264_FILTER_MB_STATUS_ZONE_COUNT; i++)
                    {
                        fprintf(filter->fStatsOut, " %lu", (long unsigned int)filter->stats.macroblockStatus[j][i]);
                    }
                }
                fprintf(filter->fStatsOut, "\n");
            }
#endif
            filter->lastStatsOutputTimestamp = curTime;
        }
        if (filter->currentAuIsRef)
        {
            /* reference frame => exchange macroblock status buffers */
            uint8_t *tmp = filter->currentAuMacroblockStatus;
            filter->currentAuMacroblockStatus = filter->currentAuRefMacroblockStatus;
            filter->currentAuRefMacroblockStatus = tmp;
        }
    }

    return ret;
}


void* ARSTREAM2_H264Filter_RunFilterThread(void *filterHandle)
{
    ARSTREAM2_H264Filter_t* filter = (ARSTREAM2_H264Filter_t*)filterHandle;
    int shouldStop, running, ret, selectRet;
    struct timespec t1;
    uint64_t curTime;
    fd_set readSet, readSetSaved;
    int maxFd;
    struct timeval tv;

    if (filter == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Error while starting %s, bad parameters", __FUNCTION__);
        return (void *)0;
    }

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_H264_FILTER_TAG, "Filter thread running");
    ARSAL_Mutex_Lock(&(filter->mutex));
    filter->threadStarted = 1;
    shouldStop = filter->threadShouldStop;
    running = filter->running;
    ARSAL_Mutex_Unlock(&(filter->mutex));

    FD_ZERO(&readSetSaved);
    FD_SET(filter->pipe[0], &readSetSaved);
    maxFd = filter->pipe[0];
    maxFd++;
    readSet = readSetSaved;
    tv.tv_sec = 0;
    tv.tv_usec = ARSTREAM2_H264_FILTER_TIMEOUT_US;

    while (shouldStop == 0)
    {
        selectRet = select(maxFd, &readSet, NULL, NULL, &tv);

        ARSAL_Time_GetTime(&t1);
        curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

        if (selectRet < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Select error (%d): %s", errno, strerror(errno));
        }

        ARSTREAM2_H264_AuFifoItem_t *auItem;

        /* dequeue an access unit */
        ARSAL_Mutex_Lock(filter->fifoMutex);
        auItem = ARSTREAM2_H264_AuFifoDequeueItem(filter->auFifo);
        ARSAL_Mutex_Unlock(filter->fifoMutex);

        while (auItem != NULL)
        {
            ARSTREAM2_H264_NaluFifoItem_t *naluItem;
            int auLocked = 0;

            if (running)
            {
                ARSTREAM2_H264_NaluFifoItem_t *prevNaluItem = NULL;
                int isFirst = 1;

                ARSTREAM2_H264Filter_ResetAu(filter);

                /* process the NAL units */
                for (naluItem = auItem->au.naluHead; naluItem; naluItem = naluItem->next)
                {
                    ret = ARSTREAM2_H264Filter_ParseNalu(filter, &auItem->au, &naluItem->nalu);
                    if (ret < 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_ParseNalu() failed (%d)", ret);
                    }

                    if (ret == 0)
                    {
                        filter->previousSliceType = naluItem->nalu.sliceType;

                        if ((filter->firstGrayIFramePending)
                                && ((naluItem->nalu.naluType == ARSTREAM2_H264_NALU_TYPE_SLICE_IDR)
                                    || (naluItem->nalu.naluType == ARSTREAM2_H264_NALU_TYPE_SLICE)))
                        {
                            int ret2 = ARSTREAM2_H264FilterError_OutputGrayIdrFrame(filter, &auItem->au);
                            if (ret2 != 0)
                            {
                                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264FilterError_OutputGrayIdrFrame() failed (%d)", ret2);
                            }
                            else
                            {
                                filter->firstGrayIFramePending = 0;
                            }
                        }

                        if (naluItem->nalu.missingPacketsBefore)
                        {
                            /* error concealment: missing slices before the current slice */
                            ret = ARSTREAM2_H264FilterError_HandleMissingSlices(filter, &auItem->au, naluItem, isFirst);
                            if (ret < 0)
                            {
                                if (ret != -2)
                                {
                                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264FilterError_HandleMissingSlices() failed (%d)", ret);
                                }
                                filter->currentAuIncomplete = 1;
                            }
                        }

                        ret = ARSTREAM2_H264Filter_ProcessNalu(filter, naluItem);
                        if (ret != 0)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_ProcessNalu() failed (%d)", ret);
                        }
                    }

                    prevNaluItem = naluItem;
                    isFirst = 0;
                }

                if ((prevNaluItem) && (prevNaluItem->nalu.isLastInAu == 0))
                {
                    /* error concealment: missing slices at the end of frame */
                    ret = ARSTREAM2_H264FilterError_HandleMissingEndOfFrame(filter, &auItem->au, prevNaluItem);
                    if (ret < 0)
                    {
                        if (ret != -2)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264FilterError_HandleMissingEndOfFrame() failed (%d)", ret);
                        }
                        filter->currentAuIncomplete = 1;
                    }
                }

                /* output the access unit */
                ret = ARSTREAM2_H264Filter_OutputAu(filter, auItem, &auLocked);
                if (ret < 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_OutputAu() failed (%d)", ret);
                }
            }

            ARSAL_Mutex_Lock(filter->fifoMutex);
            if (!auLocked)
            {
                /* free the access unit and associated NAL units */
                while ((naluItem = ARSTREAM2_H264_AuDequeueNalu(&auItem->au)) != NULL)
                {
                    ret = ARSTREAM2_H264_NaluFifoPushFreeItem(filter->naluFifo, naluItem);
                    if (ret != 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Failed to push free item in the NALU FIFO (%d)", ret);
                    }
                }
                ret = ARSTREAM2_H264_AuFifoPushFreeItem(filter->auFifo, auItem);
                if (ret != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Failed to push free item in the AU FIFO (%d)", ret);
                }
            }

            /* dequeue the next access unit */
            auItem = ARSTREAM2_H264_AuFifoDequeueItem(filter->auFifo);
            ARSAL_Mutex_Unlock(filter->fifoMutex);
        }

        if ((selectRet >= 0) && (FD_ISSET(filter->pipe[0], &readSet)))
        {
            /* Dump bytes (so it won't be ready next time) */
            char dump[10];
            read(filter->pipe[0], &dump, 10);
        }

        ARSAL_Mutex_Lock(&(filter->mutex));
        shouldStop = filter->threadShouldStop;
        running = filter->running;
        ARSAL_Mutex_Unlock(&(filter->mutex));

        if (!shouldStop)
        {
            /* Prepare the next select */
            readSet = readSetSaved;
            tv.tv_sec = 0;
            tv.tv_usec = ARSTREAM2_H264_FILTER_TIMEOUT_US;
        }
    }

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
    ARSAL_Mutex_Unlock(&(filter->mutex));
    ARSAL_Cond_Signal(&(filter->startCond));
    if (filter->pipe[1] != -1)
    {
        char * buff = "x";
        write(filter->pipe[1], buff, 1);
    }    
    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_H264_FILTER_TAG, "Filter is running");

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
    while (filter->callbackInProgress)
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
    ARSAL_Mutex_Unlock(&(filter->mutex));
    if (filter->pipe[1] != -1)
    {
        char * buff = "x";
        write(filter->pipe[1], buff, 1);
    }    
    ARSAL_PRINT(ARSAL_PRINT_DEBUG, ARSTREAM2_H264_FILTER_TAG, "Filter is paused");

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
    if (filter->pipe[1] != -1)
    {
        char * buff = "x";
        write(filter->pipe[1], buff, 1);
    }    
    int recRet = ARSTREAM2_H264Filter_StreamRecorderStop(filter);
    if (recRet != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_StreamRecorderStop() failed (%d)", recRet);
        ret = ARSTREAM2_ERROR_INVALID_STATE;
    }

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_H264Filter_Init(ARSTREAM2_H264Filter_Handle *filterHandle, ARSTREAM2_H264Filter_Config_t *config)
{
    ARSTREAM2_H264Filter_t* filter;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;
    int mutexWasInit = 0, startCondWasInit = 0, callbackCondWasInit = 0, recordAuBufferPoolMutexWasInit = 0;

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
    if ((!config->auFifo) || (!config->naluFifo) || (!config->mutex))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Invalid shared context in config");
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
        filter->auFifo = config->auFifo;
        filter->naluFifo = config->naluFifo;
        filter->fifoMutex = config->mutex;
        filter->pipe[0] = config->filterPipe[0];
        filter->pipe[1] = config->filterPipe[1];
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
        int mutexInitRet = ARSAL_Mutex_Init(&(filter->recordAuBufferPoolMutex));
        if (mutexInitRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Mutex creation failed (%d)", mutexInitRet);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
        else
        {
            recordAuBufferPoolMutexWasInit = 1;
        }
    }

    if (ret == ARSTREAM2_OK)
    {
        int poolRet = ARSTREAM2_H264Filter_AuBufferPoolInit(&filter->recordAuBufferPool, ARSTREAM2_H264_FILTER_AU_BUFFER_POOL_SIZE,
                                                            ARSTREAM2_H264_FILTER_AU_BUFFER_SIZE, ARSTREAM2_H264_FILTER_AU_METADATA_BUFFER_SIZE);
        if (poolRet != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Access unit buffer pool creation failed (%d)", poolRet);
            ret = ARSTREAM2_ERROR_ALLOC;
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
        filter->currentAuMetadata = malloc(ARSTREAM2_H264_FILTER_AU_METADATA_BUFFER_SIZE);
        if (!filter->currentAuMetadata)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Allocation failed (size %d)", ARSTREAM2_H264_FILTER_AU_METADATA_BUFFER_SIZE);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
    }

    if (ret == ARSTREAM2_OK)
    {
        filter->currentAuUserData = malloc(ARSTREAM2_H264_FILTER_AU_USER_DATA_BUFFER_SIZE);
        if (!filter->currentAuUserData)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Allocation failed (size %d)", ARSTREAM2_H264_FILTER_AU_USER_DATA_BUFFER_SIZE);
            ret = ARSTREAM2_ERROR_ALLOC;
        }
    }

#ifdef ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT
    if (ret == ARSTREAM2_OK)
    {
        int i;
        char szOutputFileName[128];
        char *pszFilePath = NULL;
        szOutputFileName[0] = '\0';
        if (0)
        {
        }
#ifdef ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_ALLOW_DRONE
        else if ((access(ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_PATH_DRONE, F_OK) == 0) && (access(ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_PATH_DRONE, W_OK) == 0))
        {
            pszFilePath = ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_PATH_DRONE;
        }
#endif
#ifdef ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_ALLOW_NAP_USB
        else if ((access(ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_PATH_NAP_USB, F_OK) == 0) && (access(ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_PATH_NAP_USB, W_OK) == 0))
        {
            pszFilePath = ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_PATH_NAP_USB;
        }
#endif
#ifdef ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_ALLOW_NAP_INTERNAL
        else if ((access(ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_PATH_NAP_INTERNAL, F_OK) == 0) && (access(ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_PATH_NAP_INTERNAL, W_OK) == 0))
        {
            pszFilePath = ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_PATH_NAP_INTERNAL;
        }
#endif
#ifdef ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_ALLOW_ANDROID_INTERNAL
        else if ((access(ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_PATH_ANDROID_INTERNAL, F_OK) == 0) && (access(ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_PATH_ANDROID_INTERNAL, W_OK) == 0))
        {
            pszFilePath = ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_PATH_ANDROID_INTERNAL;
        }
#endif
#ifdef ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_ALLOW_PCLINUX
        else if ((access(ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_PATH_PCLINUX, F_OK) == 0) && (access(ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_PATH_PCLINUX, W_OK) == 0))
        {
            pszFilePath = ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_PATH_PCLINUX;
        }
#endif
        if (pszFilePath)
        {
            for (i = 0; i < 1000; i++)
            {
                snprintf(szOutputFileName, 128, "%s/%s_%03d.dat", pszFilePath, ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT_FILENAME, i);
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
            filter->fStatsOut = fopen(szOutputFileName, "w");
            if (!filter->fStatsOut)
            {
                ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_TAG, "Unable to open stats output file '%s'", szOutputFileName);
            }
        }

        if (filter->fStatsOut)
        {
            fprintf(filter->fStatsOut, "timestamp rssi totalFrameCount outputFrameCount discardedFrameCount missedFrameCount errorSecondCount");
            int i, j;
            for (i = 0; i < ARSTREAM2_H264_FILTER_MB_STATUS_ZONE_COUNT; i++)
            {
                fprintf(filter->fStatsOut, " errorSecondCountByZone[%d]", i);
            }
            for (j = 0; j < ARSTREAM2_H264_FILTER_MB_STATUS_CLASS_COUNT; j++)
            {
                for (i = 0; i < ARSTREAM2_H264_FILTER_MB_STATUS_ZONE_COUNT; i++)
                {
                    fprintf(filter->fStatsOut, " macroblockStatus[%d][%d]", j, i);
                }
            }
            fprintf(filter->fStatsOut, "\n");
        }
    }
#endif //#ifdef ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT

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
            if (recordAuBufferPoolMutexWasInit) ARSAL_Mutex_Destroy(&(filter->recordAuBufferPoolMutex));
            if (filter->recordAuBufferPool.size != 0) ARSTREAM2_H264Filter_AuBufferPoolFree(&filter->recordAuBufferPool);
            if (filter->parser) ARSTREAM2_H264Parser_Free(filter->parser);
            if (filter->tempAuBuffer) free(filter->tempAuBuffer);
            if (filter->tempSliceNaluBuffer) free(filter->tempSliceNaluBuffer);
            if (filter->currentAuMetadata) free(filter->currentAuMetadata);
            if (filter->currentAuUserData) free(filter->currentAuUserData);
            if (filter->writer) ARSTREAM2_H264Writer_Free(filter->writer);
#ifdef ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT
            if (filter->fStatsOut) fclose(filter->fStatsOut);
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
        ARSAL_Mutex_Destroy(&(filter->recordAuBufferPoolMutex));
        ARSTREAM2_H264Filter_AuBufferPoolFree(&filter->recordAuBufferPool);
        ARSTREAM2_H264Parser_Free(filter->parser);
        ARSTREAM2_H264Writer_Free(filter->writer);
        int recErr = ARSTREAM2_H264Filter_StreamRecorderFree(filter);
        if (recErr != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_StreamRecorderFree() failed (%d)", recErr);
        }

        if (filter->currentAuMacroblockStatus) free(filter->currentAuMacroblockStatus);
        if (filter->currentAuRefMacroblockStatus) free(filter->currentAuRefMacroblockStatus);
        if (filter->pSps) free(filter->pSps);
        if (filter->pPps) free(filter->pPps);
        if (filter->tempAuBuffer) free(filter->tempAuBuffer);
        if (filter->tempSliceNaluBuffer) free(filter->tempSliceNaluBuffer);
        if (filter->currentAuMetadata) free(filter->currentAuMetadata);
        if (filter->currentAuUserData) free(filter->currentAuUserData);
        if (filter->recordFileName) free(filter->recordFileName);
#ifdef ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT
        if (filter->fStatsOut) fclose(filter->fStatsOut);
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
