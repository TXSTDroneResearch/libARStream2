/**
 * @file arstream2_h264_filter.c
 * @brief Parrot Reception Library - H.264 Filter
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#include "arstream2_h264_filter.h"


#define ARSTREAM2_H264_FILTER_TAG "ARSTREAM2_H264Filter"


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
    ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_H264_FILTER_TAG, "SPS/PPS sync OK"); //TODO: debug

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
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Parser_SetupNalu_buffer() failed (%d)", err);
    }

    if (err == ARSTREAM2_OK)
    {
        err = ARSTREAM2_H264Parser_ParseNalu(filter->parser, NULL);
        if (err < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Parser_ParseNalu() failed (%d)", err);
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
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Parser_GetSliceInfo() failed (%d)", _err);
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
                            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Parser_GetUserDataSei() failed (%d)", _err);
                        }
                        else
                        {
                            if (ARSTREAM2_H264Sei_IsUserDataParrotStreamingV1(pUserDataSei, userDataSeiSize) == 1)
                            {
                                _err = ARSTREAM2_H264Sei_DeserializeUserDataParrotStreamingV1(pUserDataSei, userDataSeiSize, &filter->currentAuStreamingInfo, filter->currentAuStreamingSliceMbCount);
                                if (_err != ARSTREAM2_OK)
                                {
                                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Sei_DeserializeUserDataParrotStreamingV1() failed (%d)", _err);
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
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_Sync() failed (%d)", ret);
        }
    }

    filter->callbackInProgress = 0;
    ARSAL_Mutex_Unlock(&(filter->mutex));
    ARSAL_Cond_Signal(&(filter->callbackCond));

    return (ret >= 0) ? 0 : ret;
}


void ARSTREAM2_H264Filter_ResetAu(ARSTREAM2_H264Filter_t *filter)
{
    filter->currentAuIncomplete = 0;
    filter->currentAuSyncType = ARSTREAM2_H264_FILTER_AU_SYNC_TYPE_NONE;
    filter->currentAuSlicesAllI = 1;
    filter->currentAuSlicesReceived = 0;
    filter->currentAuStreamingInfoAvailable = 0;
    memset(&filter->currentAuStreamingInfo, 0, sizeof(ARSTREAM2_H264Sei_ParrotStreamingV1_t));
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
        ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_H264_FILTER_TAG, "AU output cancelled (!outputIncompleteAu)"); //TODO: debug
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
                ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_H264_FILTER_TAG, "AU output cancelled (waitForSync)"); //TODO: debug
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

                cbRet = filter->auReadyCallback(auBuffer, auSize, /*TODO au->rtpTimestamp,*/ au->ntpTimestamp,
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

    ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_H264_FILTER_TAG, "Filter thread running");
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
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_ParseNalu() failed (%d)", ret);
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
    ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_H264_FILTER_TAG, "Filter thread has ended");

    return (void *)0;
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

    ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_H264_FILTER_TAG, "Stopping Filter...");
    ARSAL_Mutex_Lock(&(filter->mutex));
    filter->threadShouldStop = 1;
    ARSAL_Mutex_Unlock(&(filter->mutex));
    /* signal the filter thread to avoid a deadlock */
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
    int mutexWasInit = 0, callbackCondWasInit = 0;

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
            if (callbackCondWasInit) ARSAL_Cond_Destroy(&(filter->callbackCond));
            if (filter->parser) ARSTREAM2_H264Parser_Free(filter->parser);
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
        ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_H264_FILTER_TAG, "All threads are stopped");
        canDelete = 1;
    }

    if (canDelete == 1)
    {
        ARSAL_Mutex_Destroy(&(filter->mutex));
        ARSAL_Cond_Destroy(&(filter->callbackCond));
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
