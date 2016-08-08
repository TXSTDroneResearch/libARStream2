/**
 * @file arstream2_h264_filter.c
 * @brief Parrot Reception Library - H.264 Filter
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#include "arstream2_h264_filter.h"


#define ARSTREAM2_H264_FILTER_TAG "ARSTREAM2_H264Filter"


static int ARSTREAM2_H264Filter_Sync(ARSTREAM2_H264Filter_t *filter)
{
    int ret = 0;
    eARSTREAM2_ERROR err = ARSTREAM2_OK;

    /* Configure the writer */
    if (ret == 0)
    {
        ARSTREAM2_H264_SpsContext_t *spsContext = NULL;
        ARSTREAM2_H264_PpsContext_t *ppsContext = NULL;
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

    if (ret == 0)
    {
        filter->currentAuMacroblockStatus = realloc(filter->currentAuMacroblockStatus, filter->mbCount * sizeof(uint8_t));
        filter->currentAuRefMacroblockStatus = realloc(filter->currentAuRefMacroblockStatus, filter->mbCount * sizeof(uint8_t));
        if (filter->currentAuMacroblockStatus)
        {
            memset(filter->currentAuMacroblockStatus, ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_UNKNOWN, filter->mbCount);
        }
        if (filter->currentAuRefMacroblockStatus)
        {
            memset(filter->currentAuRefMacroblockStatus, ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_UNKNOWN, filter->mbCount);
        }
        filter->previousAuFrameNum = -1;
    }

    if (ret == 0)
    {
        filter->sync = 1;
        if (filter->generateFirstGrayIFrame)
        {
            filter->firstGrayIFramePending = 1;
        }

        ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_H264_FILTER_TAG, "SPS/PPS sync OK");

        /* SPS/PPS callback */
        if (filter->spsPpsCallback)
        {
            int cbRet = filter->spsPpsCallback(filter->pSps, filter->spsSize, filter->pPps, filter->ppsSize, filter->spsPpsCallbackUserPtr);
            if (cbRet != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "spsPpsCallback failed: %s", ARSTREAM2_Error_ToString(cbRet));
            }
        }
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
                au->syncType = ARSTREAM2_H264_AU_SYNC_TYPE_IDR;
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
                        if ((filter->sync) && (filter->currentAuFrameNum == -1))
                        {
                            filter->currentAuFrameNum = sliceInfo.frame_num;
                            if ((filter->currentAuIsRef) && (filter->previousAuFrameNum != -1) && (filter->currentAuFrameNum != (filter->previousAuFrameNum + 1) % filter->maxFrameNum))
                            {
                                /* count missed frames (missing non-ref frames are not counted as missing) */
                                filter->stats.totalFrameCount++;
                                filter->stats.missedFrameCount++;
                                if (filter->currentAuRefMacroblockStatus)
                                {
                                    memset(filter->currentAuRefMacroblockStatus, ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_MISSING, filter->mbCount);
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
                            else if (userDataSeiSize <= au->buffer->userDataBufferSize)
                            {
                                memcpy(au->buffer->userDataBuffer, pUserDataSei, userDataSeiSize);
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

    if ((filter->spsSync) && (filter->ppsSync) && (!filter->sync))
    {
        ret = ARSTREAM2_H264Filter_Sync(filter);
        if (ret < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_Sync() failed (%d)", ret);
        }
    }

    return (ret >= 0) ? 0 : ret;
}


void ARSTREAM2_H264Filter_ResetAu(ARSTREAM2_H264Filter_t *filter)
{
    filter->currentAuIncomplete = 0;
    filter->currentAuSlicesAllI = 1;
    filter->currentAuSlicesReceived = 0;
    filter->currentAuStreamingInfoAvailable = 0;
    memset(&filter->currentAuStreamingInfo, 0, sizeof(ARSTREAM2_H264Sei_ParrotStreamingV1_t));
    filter->currentAuPreviousSliceIndex = -1;
    filter->currentAuPreviousSliceFirstMb = 0;
    filter->currentAuInferredPreviousSliceFirstMb = 0;
    filter->currentAuCurrentSliceFirstMb = -1;
    filter->previousSliceType = ARSTREAM2_H264_SLICE_TYPE_NON_VCL;
    if ((filter->sync) && (filter->currentAuMacroblockStatus))
    {
        memset(filter->currentAuMacroblockStatus, ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_UNKNOWN, filter->mbCount);
    }
    if (filter->currentAuIsRef) filter->previousAuFrameNum = filter->currentAuFrameNum;
    filter->currentAuFrameNum = -1;
    filter->currentAuIsRef = 0;
}


static int ARSTREAM2_H264Filter_ProcessNalu(ARSTREAM2_H264Filter_t *filter, ARSTREAM2_H264_NalUnit_t *nalu)
{
    int ret = 0;

    if ((nalu->naluType == ARSTREAM2_H264_NALU_TYPE_SLICE_IDR) || (nalu->naluType == ARSTREAM2_H264_NALU_TYPE_SLICE))
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
        if ((filter->sync) && (filter->currentAuMacroblockStatus) && (sliceFirstMb > 0) && (sliceMbCount > 0))
        {
            int i, idx;
            uint8_t status;
            if (sliceFirstMb + sliceMbCount > filter->mbCount) sliceMbCount = filter->mbCount - sliceFirstMb;
            for (i = 0, idx = sliceFirstMb; i < sliceMbCount; i++, idx++)
            {
                if (nalu->sliceType == ARSTREAM2_H264_SLICE_TYPE_I)
                {
                    status = ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_VALID_ISLICE;
                }
                else
                {
                    if ((!filter->currentAuRefMacroblockStatus)
                            || ((filter->currentAuRefMacroblockStatus[idx] != ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_VALID_ISLICE) && (filter->currentAuRefMacroblockStatus[idx] != ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_VALID_PSLICE)))
                    {
                        status = ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_ERROR_PROPAGATION;
                    }
                    else
                    {
                        status = ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_VALID_PSLICE;
                    }
                }
                filter->currentAuMacroblockStatus[idx] = status;
            }
        }
    }

    return ret;
}


int ARSTREAM2_H264Filter_ProcessAu(ARSTREAM2_H264Filter_t *filter, ARSTREAM2_H264_AccessUnit_t *au)
{
    ARSTREAM2_H264_NaluFifoItem_t *naluItem, *prevNaluItem = NULL;
    int cancelAuOutput = 0, discarded = 0;
    int ret = 0, err;

    if ((!filter) || (!au))
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Invalid pointer");
        return -1;
    }

    if (filter->resyncPending)
    {
        filter->sync = 0;
        filter->resyncPending = 0;
    }

    ARSTREAM2_H264Filter_ResetAu(filter);

    /* process the NAL units */
    for (naluItem = au->naluHead; naluItem; naluItem = naluItem->next)
    {
        err = ARSTREAM2_H264Filter_ParseNalu(filter, au, &naluItem->nalu);
        if (err < 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_ParseNalu() failed (%d)", err);
        }

        if (err == 0)
        {
            filter->previousSliceType = naluItem->nalu.sliceType;

            if ((filter->firstGrayIFramePending)
                    && ((naluItem->nalu.naluType == ARSTREAM2_H264_NALU_TYPE_SLICE_IDR)
                        || (naluItem->nalu.naluType == ARSTREAM2_H264_NALU_TYPE_SLICE)))
            {
                int err = ARSTREAM2_H264FilterError_OutputGrayIdrFrame(filter, au);
                if (err != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264FilterError_OutputGrayIdrFrame() failed (%d)", err);
                }
                else
                {
                    filter->firstGrayIFramePending = 0;
                }
            }

            if (naluItem->nalu.missingPacketsBefore)
            {
                /* error concealment: missing slices before the current slice */
                err = ARSTREAM2_H264FilterError_HandleMissingSlices(filter, au, naluItem);
                if (err < 0)
                {
                    if (err != -2)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264FilterError_HandleMissingSlices() failed (%d)", ret);
                    }
                    filter->currentAuIncomplete = 1;
                }
            }
            else if (filter->sync)
            {
                if (filter->currentAuPreviousSliceIndex < 0)
                {
                    if (filter->currentAuCurrentSliceFirstMb > 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "FIXME! missingPacketsBefore==0 but currentSliceFirstMb=%d and expectedSliceFirstMb=0, this should not happen!",
                                    filter->currentAuCurrentSliceFirstMb);
                    }
                }
                else if (filter->currentAuStreamingInfoAvailable)
                {
                    if (filter->currentAuCurrentSliceFirstMb != filter->currentAuPreviousSliceFirstMb + filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex])
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "FIXME! missingPacketsBefore==0 but currentSliceFirstMb=%d and expectedSliceFirstMb=%d, this should not happen!",
                                    filter->currentAuCurrentSliceFirstMb, filter->currentAuPreviousSliceFirstMb + filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex]);
                    }
                }
            }

            err = ARSTREAM2_H264Filter_ProcessNalu(filter, &naluItem->nalu);
            if (err != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264Filter_ProcessNalu() failed (%d)", err);
            }
        }

        prevNaluItem = naluItem;
    }

    if ((prevNaluItem) && (prevNaluItem->nalu.isLastInAu == 0))
    {
        /* error concealment: missing slices at the end of frame */
        err = ARSTREAM2_H264FilterError_HandleMissingEndOfFrame(filter, au, prevNaluItem);
        if (err < 0)
        {
            if (err != -2)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "ARSTREAM2_H264FilterError_HandleMissingEndOfFrame() failed (%d)", ret);
            }
            filter->currentAuIncomplete = 1;
        }
    }

    if (au->syncType != ARSTREAM2_H264_AU_SYNC_TYPE_IDR)
    {
        if (filter->currentAuSlicesAllI)
        {
            au->syncType = ARSTREAM2_H264_AU_SYNC_TYPE_IFRAME;
        }
        else if ((filter->currentAuStreamingInfoAvailable) && (filter->currentAuStreamingInfo.indexInGop == 0))
        {
            au->syncType = ARSTREAM2_H264_AU_SYNC_TYPE_PIR_START;
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
        if (!filter->sync)
        {
            /* cancel if not synchronized */
            cancelAuOutput = 1;
            ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_H264_FILTER_TAG, "AU output cancelled (waitForSync)"); //TODO: debug
        }
    }

    if (!cancelAuOutput)
    {
        if (filter->currentAuMacroblockStatus)
        {
            err = ARSTREAM2_H264_AuMbStatusCheckSizeRealloc(au, filter->mbCount);
            if (err == 0)
            {
                memcpy(au->buffer->mbStatusBuffer, filter->currentAuMacroblockStatus, filter->mbCount);
                au->mbStatusAvailable = 1;
            }
            else
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "MB status buffer is too small");
            }
        }

        ret = 1;
        filter->currentAuOutputIndex++;
    }

    struct timespec t1;
    ARSAL_Time_GetTime(&t1);
    uint64_t curTime = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

    /* update the stats */
    if (filter->sync)
    {
        if ((filter->currentAuMacroblockStatus) && ((discarded) || (ret != 1)) && (filter->currentAuIsRef))
        {
            /* missed frame (missing non-ref frames are not counted as missing) */
            memset(filter->currentAuMacroblockStatus, ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_MISSING, filter->mbCount);
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
                    if ((ret == 1) && (filter->currentAuMacroblockStatus[k] != ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_VALID_ISLICE)
                            && (filter->currentAuMacroblockStatus[k] != ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_VALID_PSLICE))
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
                if ((au->metadataSize >= 27) && (ntohs(*((uint16_t*)au->buffer->metadataBuffer)) == 0x5031))
                {
                    /* get the RSSI from the streaming metadata */
                    //TODO: remove this hack once we have a better way of getting the RSSI
                    rssi = (int8_t)au->buffer->metadataBuffer[26];
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


int ARSTREAM2_H264Filter_ForceResync(ARSTREAM2_H264Filter_t *filter)
{
    int ret = 0;

    if (!filter)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Invalid pointer");
        return -1;
    }

    filter->resyncPending = 1;

    return ret;
}


int ARSTREAM2_H264Filter_ForceIdr(ARSTREAM2_H264Filter_t *filter)
{
    int ret = 0;

    if (!filter)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "Invalid pointer");
        return -1;
    }

    if (filter->generateFirstGrayIFrame)
    {
        filter->firstGrayIFramePending = 1;
    }

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

    return ret;
}


int ARSTREAM2_H264Filter_GetVideoParams(ARSTREAM2_H264Filter_Handle filterHandle, int *mbWidth, int *mbHeight, int *width, int *height, float *framerate)
{
    ARSTREAM2_H264Filter_t* filter = (ARSTREAM2_H264Filter_t*)filterHandle;
    int ret = 0;

    if (!filterHandle)
    {
        return -1;
    }

    if (!filter->sync)
    {
        return -1;
    }

    if (mbWidth) *mbWidth = filter->mbWidth; //TODO
    if (mbHeight) *mbHeight = filter->mbHeight; //TODO
    if (width) *width = filter->mbWidth * 16; //TODO
    if (height) *height = filter->mbHeight * 16; //TODO
    if (framerate) *framerate = filter->framerate;

    return ret;
}


eARSTREAM2_ERROR ARSTREAM2_H264Filter_Init(ARSTREAM2_H264Filter_Handle *filterHandle, ARSTREAM2_H264Filter_Config_t *config)
{
    ARSTREAM2_H264Filter_t* filter;
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;

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
    if (!config->auFifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "No access unit FIFO provided");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!config->naluFifo)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "No NAL unit FIFO provided");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!config->fifoMutex)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "No FIFO mutex provided");
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }
    if (!config->auCallback)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_TAG, "No access unit callback function provided");
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

        filter->outputIncompleteAu = (config->outputIncompleteAu > 0) ? 1 : 0;
        filter->filterOutSpsPps = (config->filterOutSpsPps > 0) ? 1 : 0;
        filter->filterOutSei = (config->filterOutSei > 0) ? 1 : 0;
        filter->replaceStartCodesWithNaluSize = (config->replaceStartCodesWithNaluSize > 0) ? 1 : 0;
        filter->generateSkippedPSlices = (config->generateSkippedPSlices > 0) ? 1 : 0;
        filter->generateFirstGrayIFrame = (config->generateFirstGrayIFrame > 0) ? 1 : 0;
        filter->auFifo = config->auFifo;
        filter->naluFifo = config->naluFifo;
        filter->fifoMutex = config->fifoMutex;
        filter->auCallback = config->auCallback;
        filter->auCallbackUserPtr = config->auCallbackUserPtr;
        filter->spsPpsCallback = config->spsPpsCallback;
        filter->spsPpsCallbackUserPtr = config->spsPpsCallbackUserPtr;
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
    eARSTREAM2_ERROR ret = ARSTREAM2_OK;

    if ((!filterHandle) || (!*filterHandle))
    {
        return ARSTREAM2_ERROR_BAD_PARAMETERS;
    }

    filter = (ARSTREAM2_H264Filter_t*)*filterHandle;

    ARSTREAM2_H264Parser_Free(filter->parser);
    ARSTREAM2_H264Writer_Free(filter->writer);

    free(filter->currentAuMacroblockStatus);
    free(filter->currentAuRefMacroblockStatus);
    free(filter->pSps);
    free(filter->pPps);
#ifdef ARSTREAM2_H264_FILTER_STATS_FILE_OUTPUT
    if (filter->fStatsOut) fclose(filter->fStatsOut);
#endif

    free(filter);
    *filterHandle = NULL;

    return ret;
}
