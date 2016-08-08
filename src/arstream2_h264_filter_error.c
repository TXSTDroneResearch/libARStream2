/**
 * @file arstream2_h264_filter_error.c
 * @brief Parrot Reception Library - H.264 Filter - error concealment
 * @date 07/21/2016
 * @author aurelien.barre@parrot.com
 */

#include "arstream2_h264_filter.h"


#define ARSTREAM2_H264_FILTER_ERROR_TAG "ARSTREAM2_H264FilterError"


int ARSTREAM2_H264FilterError_OutputGrayIdrFrame(ARSTREAM2_H264Filter_t *filter, ARSTREAM2_H264_AccessUnit_t *nextAu)
{
    int ret = 0;
    eARSTREAM2_ERROR err = ARSTREAM2_OK;
    ARSTREAM2_H264_SliceContext_t *sliceContextNext = NULL;
    ARSTREAM2_H264_SliceContext_t sliceContext;
    ARSTREAM2_H264_AuFifoBuffer_t *buffer = NULL;
    ARSTREAM2_H264_AuFifoItem_t *auItem = NULL;
    ARSTREAM2_H264_NaluFifoItem_t *naluItem = NULL, *spsItem = NULL, *ppsItem = NULL;

    err = ARSTREAM2_H264Parser_GetSliceContext(filter->parser, (void**)&sliceContextNext);
    if (err != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "ARSTREAM2_H264Parser_GetSliceContext() failed (%d)", err);
        ret = -1;
    }
    else
    {
        memcpy(&sliceContext, sliceContextNext, sizeof(sliceContext));
        sliceContext.nal_ref_idc = 3;
        sliceContext.nal_unit_type = ARSTREAM2_H264_NALU_TYPE_SLICE_IDR;
        sliceContext.idrPicFlag = 1;
        sliceContext.slice_type = ARSTREAM2_H264_SLICE_TYPE_I;
        sliceContext.frame_num = 0;
        sliceContext.idr_pic_id = 0;
        sliceContext.no_output_of_prior_pics_flag = 0;
        sliceContext.long_term_reference_flag = 0;
    }

    if (ret == 0)
    {
        ARSAL_Mutex_Lock(filter->fifoMutex);
        buffer = ARSTREAM2_H264_AuFifoGetBuffer(filter->auFifo);
        auItem = ARSTREAM2_H264_AuFifoPopFreeItem(filter->auFifo);
        ARSAL_Mutex_Unlock(filter->fifoMutex);
        if ((!buffer) || (!auItem))
        {
            ARSAL_Mutex_Lock(filter->fifoMutex);
            if (buffer)
            {
                ARSTREAM2_H264_AuFifoUnrefBuffer(filter->auFifo, buffer);
                buffer = NULL;
            }
            if (auItem)
            {
                ARSTREAM2_H264_AuFifoPushFreeItem(filter->auFifo, auItem);
                auItem = NULL;
            }
            err = ARSTREAM2_H264_AuFifoFlush(filter->auFifo);
            ARSAL_Mutex_Unlock(filter->fifoMutex);
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "AU FIFO is full, cannot generate gray I-frame => flush to recover (%d AU flushed)", err);
            ret = -1;
        }
        else
        {
            ARSTREAM2_H264_AuReset(&auItem->au);
            auItem->au.buffer = buffer;
        }
    }

    if ((ret == 0) && (!filter->filterOutSpsPps) && (auItem->au.auSize + filter->spsSize <= auItem->au.buffer->auBufferSize))
    {
        /* insert SPS before the I-frame */
        ARSAL_Mutex_Lock(filter->fifoMutex);
        spsItem = ARSTREAM2_H264_NaluFifoPopFreeItem(filter->naluFifo);
        ARSAL_Mutex_Unlock(filter->fifoMutex);
        if (spsItem)
        {
            ARSTREAM2_H264_NaluReset(&naluItem->nalu);
            spsItem->nalu.nalu = auItem->au.buffer->auBuffer + auItem->au.auSize;
            memcpy(auItem->au.buffer->auBuffer + auItem->au.auSize, filter->pSps, filter->spsSize);
            spsItem->nalu.naluSize = filter->spsSize;
            auItem->au.auSize += filter->spsSize;
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "NALU FIFO is full, cannot generate gray I-frame");
            ret = -1;
        }
    }

    if ((ret == 0) && (!filter->filterOutSpsPps) && (auItem->au.auSize + filter->ppsSize <= auItem->au.buffer->auBufferSize))
    {
        /* insert PPS before the I-frame */
        ARSAL_Mutex_Lock(filter->fifoMutex);
        ppsItem = ARSTREAM2_H264_NaluFifoPopFreeItem(filter->naluFifo);
        ARSAL_Mutex_Unlock(filter->fifoMutex);
        if (ppsItem)
        {
            ARSTREAM2_H264_NaluReset(&naluItem->nalu);
            ppsItem->nalu.nalu = auItem->au.buffer->auBuffer + auItem->au.auSize;
            memcpy(auItem->au.buffer->auBuffer + auItem->au.auSize, filter->pPps, filter->ppsSize);
            ppsItem->nalu.naluSize = filter->ppsSize;
            auItem->au.auSize += filter->ppsSize;
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "NALU FIFO is full, cannot generate gray I-frame");
            ret = -1;
        }
    }

    if (ret == 0)
    {
        ARSAL_Mutex_Lock(filter->fifoMutex);
        naluItem = ARSTREAM2_H264_NaluFifoPopFreeItem(filter->naluFifo);
        ARSAL_Mutex_Unlock(filter->fifoMutex);
        if (naluItem)
        {
            ARSTREAM2_H264_NaluReset(&naluItem->nalu);
            naluItem->nalu.nalu = auItem->au.buffer->auBuffer + auItem->au.auSize;
            naluItem->nalu.naluSize = 0;

            unsigned int outputSize;

            err = ARSTREAM2_H264Writer_WriteGrayISliceNalu(filter->writer, 0, filter->mbCount, (void*)&sliceContext,
                                                           auItem->au.buffer->auBuffer + auItem->au.auSize,
                                                           auItem->au.buffer->auBufferSize - auItem->au.auSize, &outputSize);
            if (err != ARSTREAM2_OK)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "ARSTREAM2_H264Writer_WriteGrayISliceNalu() failed (%d)", err);
                ret = -1;
            }
            else
            {
                naluItem->nalu.naluSize = outputSize;
                auItem->au.auSize += outputSize;
                ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_H264_FILTER_ERROR_TAG, "Gray I slice NALU output size: %d", outputSize);

                /* save the current AU context */
                int savedAuIncomplete = filter->currentAuIncomplete;
                int savedAuSlicesAllI = filter->currentAuSlicesAllI;
                int savedAuStreamingInfoAvailable = filter->currentAuStreamingInfoAvailable;
                int savedAuPreviousSliceIndex = filter->currentAuPreviousSliceIndex;
                int savedAuPreviousSliceFirstMb = filter->currentAuPreviousSliceFirstMb;
                int savedAuCurrentSliceFirstMb = filter->currentAuCurrentSliceFirstMb;
                int savedAuIsRef = filter->currentAuIsRef;
                int savedAuFrameNum = filter->currentAuFrameNum;
                uint8_t *savedAuMacroblockStatus = NULL;
                if (filter->currentAuMacroblockStatus)
                {
                    savedAuMacroblockStatus = malloc(filter->mbCount * sizeof(uint8_t));
                    if (savedAuMacroblockStatus) memcpy(savedAuMacroblockStatus, filter->currentAuMacroblockStatus, filter->mbCount * sizeof(uint8_t));
                }

                ARSTREAM2_H264Filter_ResetAu(filter);
                auItem->au.syncType = ARSTREAM2_H264_AU_SYNC_TYPE_IDR;
                auItem->au.rtpTimestamp = nextAu->rtpTimestamp - ((nextAu->rtpTimestamp >= 90) ? 90 : ((nextAu->rtpTimestamp >= 1) ? 1 : 0));
                auItem->au.ntpTimestamp = nextAu->ntpTimestamp - ((nextAu->ntpTimestamp >= 1000) ? 1000 : ((nextAu->ntpTimestamp >= 1) ? 1 : 0));
                auItem->au.ntpTimestampLocal = nextAu->ntpTimestampLocal - ((nextAu->ntpTimestampLocal >= 1000) ? 1000 : ((nextAu->ntpTimestampLocal >= 1) ? 1 : 0));
                if (filter->currentAuMacroblockStatus)
                {
                    memset(filter->currentAuMacroblockStatus, ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_VALID_ISLICE, filter->mbCount);
                }

                /* associate the NAL units with the access unit */
                if ((ret == 0) && (spsItem))
                {
                    ret = ARSTREAM2_H264_AuEnqueueNalu(&auItem->au, spsItem);
                    if (ret < 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "Failed to enqueue SPS (%d)", ret);
                    }
                }
                if ((ret == 0) && (ppsItem))
                {
                    ret = ARSTREAM2_H264_AuEnqueueNalu(&auItem->au, ppsItem);
                    if (ret < 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "Failed to enqueue PPS (%d)", ret);
                    }
                }
                if (ret == 0)
                {
                    ret = ARSTREAM2_H264_AuEnqueueNalu(&auItem->au, naluItem);
                    if (ret < 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "Failed to enqueue slice (%d)", ret);
                    }
                }

                if (ret == 0)
                {
                    /* output the access unit */
                    if (filter->auCallback)
                    {
                        if (filter->currentAuMacroblockStatus)
                        {
                            err = ARSTREAM2_H264_AuMbStatusCheckSizeRealloc(&auItem->au, filter->mbCount);
                            if (err == 0)
                            {
                                memcpy(auItem->au.buffer->mbStatusBuffer, filter->currentAuMacroblockStatus, filter->mbCount);
                                auItem->au.mbStatusAvailable = 1;
                            }
                            else
                            {
                                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "MB status buffer is too small");
                            }
                        }
                        int err = filter->auCallback(auItem, filter->auCallbackUserPtr);
                        if (err != 0)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "Failed to output the access unit");
                            ret = -1;
                        }
                        naluItem = NULL;
                        spsItem = NULL;
                        ppsItem = NULL;
                        auItem = NULL;
                    }
                    else
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "Invalid access unit callback function");
                        ret = -1;
                    }
                }

                /* restore the current AU context */
                ARSTREAM2_H264Filter_ResetAu(filter);
                filter->currentAuIncomplete = savedAuIncomplete;
                filter->currentAuSlicesAllI = savedAuSlicesAllI;
                filter->currentAuStreamingInfoAvailable = savedAuStreamingInfoAvailable;
                filter->currentAuPreviousSliceIndex = savedAuPreviousSliceIndex;
                filter->currentAuPreviousSliceFirstMb = savedAuPreviousSliceFirstMb;
                filter->currentAuCurrentSliceFirstMb = savedAuCurrentSliceFirstMb;
                filter->currentAuIsRef = savedAuIsRef;
                filter->currentAuFrameNum = savedAuFrameNum;
                if ((filter->currentAuMacroblockStatus) && (savedAuMacroblockStatus))
                {
                    memcpy(filter->currentAuMacroblockStatus, savedAuMacroblockStatus, filter->mbCount * sizeof(uint8_t));
                    free(savedAuMacroblockStatus);
                }
            }
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "NALU FIFO is full, cannot generate gray I-frame");
            ret = -1;
        }
    }

    /* free the resources in case of an error */
    if (ret != 0)
    {
        int ret2;
        ARSAL_Mutex_Lock(filter->fifoMutex);
        if (spsItem)
        {
            ret2 = ARSTREAM2_H264_NaluFifoPushFreeItem(filter->naluFifo, spsItem);
            if (ret2 != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "Failed to push free item in the NALU FIFO (%d)", ret2);
            }
        }
        if (ppsItem)
        {
            ret2 = ARSTREAM2_H264_NaluFifoPushFreeItem(filter->naluFifo, ppsItem);
            if (ret2 != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "Failed to push free item in the NALU FIFO (%d)", ret2);
            }
        }
        if (naluItem)
        {
            ret2 = ARSTREAM2_H264_NaluFifoPushFreeItem(filter->naluFifo, naluItem);
            if (ret2 != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "Failed to push free item in the NALU FIFO (%d)", ret2);
            }
        }
        if (buffer)
        {
            ret2 = ARSTREAM2_H264_AuFifoUnrefBuffer(filter->auFifo, buffer);
            if (ret2 != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "Failed to unref buffer (%d)", ret2);
            }
        }
        if (auItem)
        {
            ret2 = ARSTREAM2_H264_AuFifoPushFreeItem(filter->auFifo, auItem);
            if (ret2 != 0)
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "Failed to push free item in the AU FIFO (%d)", ret2);
            }
        }
        ARSAL_Mutex_Unlock(filter->fifoMutex);
    }

    return ret;
}


int ARSTREAM2_H264FilterError_HandleMissingSlices(ARSTREAM2_H264Filter_t *filter, ARSTREAM2_H264_AccessUnit_t *au,
                                                  ARSTREAM2_H264_NaluFifoItem_t *nextNaluItem)
{
    int missingMb = 0, firstMbInSlice = 0, ret = 0;

    if (((nextNaluItem->nalu.naluType != ARSTREAM2_H264_NALU_TYPE_SLICE_IDR) && (nextNaluItem->nalu.naluType != ARSTREAM2_H264_NALU_TYPE_SLICE)) || (filter->currentAuCurrentSliceFirstMb == 0))
    {
        //ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_H264_FILTER_ERROR_TAG, "#%d AUTS:%llu Missing NALU is probably a SPS, PPS or SEI or on previous AU => OK", filter->currentAuOutputIndex, au->ntpTimestamp);
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
                    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_H264_FILTER_ERROR_TAG, "#%d AUTS:%llu currentSliceFirstMb:%d missingMb:%d",
                                filter->currentAuOutputIndex, au->ntpTimestamp, filter->currentAuCurrentSliceFirstMb, missingMb); //TODO: debug
                }
                else
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "FIXME! #%d AUTS:%llu previousSliceIdx:%d currentSliceFirstMb:%d this should not happen!",
                                filter->currentAuOutputIndex, au->ntpTimestamp, filter->currentAuPreviousSliceIndex, filter->currentAuCurrentSliceFirstMb);
                    missingMb = 0;
                    ret = -1;
                }
            }
            else if ((filter->currentAuCurrentSliceFirstMb > filter->currentAuPreviousSliceFirstMb + filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex]))
            {
                // Slices have been received before
                firstMbInSlice = filter->currentAuPreviousSliceFirstMb + filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex];
                missingMb = filter->currentAuCurrentSliceFirstMb - firstMbInSlice;
                ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_H264_FILTER_ERROR_TAG, "#%d AUTS:%llu previousSliceFirstMb:%d previousSliceMbCount:%d currentSliceFirstMb:%d missingMb:%d firstMbInSlice:%d",
                            filter->currentAuOutputIndex, au->ntpTimestamp, filter->currentAuPreviousSliceFirstMb, filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex], filter->currentAuCurrentSliceFirstMb, missingMb, firstMbInSlice); //TODO: debug
            }
            else
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "FIXME! #%d AUTS:%llu previousSliceFirstMb:%d previousSliceMbCount:%d currentSliceFirstMb:%d this should not happen!",
                            filter->currentAuOutputIndex, au->ntpTimestamp, filter->currentAuPreviousSliceFirstMb, filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex], filter->currentAuCurrentSliceFirstMb);
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
                           ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_MISSING, missingMb);
                }
            }
        }
    }

    if (ret == 0)
    {
        //ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_H264_FILTER_ERROR_TAG, "#%d AUTS:%llu Missing NALU is probably a slice", filter->currentAuOutputIndex, au->ntpTimestamp);

        if (!filter->sync)
        {
            //ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_H264_FILTER_ERROR_TAG, "#%d AUTS:%llu No sync, abort", filter->currentAuOutputIndex, au->ntpTimestamp);
            ret = -2;
        }
    }

    if (ret == 0)
    {
        if (!filter->generateSkippedPSlices)
        {
            //ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_H264_FILTER_ERROR_TAG, "#%d AUTS:%llu Missing NALU is probably a slice", filter->currentAuOutputIndex, au->ntpTimestamp);
            if (missingMb > 0)
            {
                if (firstMbInSlice + missingMb > filter->mbCount) missingMb = filter->mbCount - firstMbInSlice;
                memset(filter->currentAuMacroblockStatus + firstMbInSlice,
                       ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_MISSING, missingMb);
            }
            ret = -2;
        }
    }

    if (ret == 0)
    {
        if (!filter->currentAuStreamingInfoAvailable)
        {
            //ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_H264_FILTER_ERROR_TAG, "#%d AUTS:%llu Streaming info is not available", filter->currentAuOutputIndex, au->ntpTimestamp);
            if (missingMb > 0)
            {
                if (firstMbInSlice + missingMb > filter->mbCount) missingMb = filter->mbCount - firstMbInSlice;
                memset(filter->currentAuMacroblockStatus + firstMbInSlice,
                       ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_MISSING, missingMb);
            }
            ret = -2;
        }
    }

    if (ret == 0)
    {
        if (nextNaluItem->nalu.sliceType != ARSTREAM2_H264_SLICE_TYPE_P)
        {
            //ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_H264_FILTER_ERROR_TAG, "#%d AUTS:%llu Current slice is not a P-slice, aborting", filter->currentAuOutputIndex, au->ntpTimestamp);
            if (missingMb > 0)
            {
                if (firstMbInSlice + missingMb > filter->mbCount) missingMb = filter->mbCount - firstMbInSlice;
                memset(filter->currentAuMacroblockStatus + firstMbInSlice,
                       ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_MISSING, missingMb);
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
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "ARSTREAM2_H264Parser_GetSliceContext() failed (%d)", err);
            ret = -1;
        }
        if (ret == 0)
        {
            ARSAL_Mutex_Lock(filter->fifoMutex);
            ARSTREAM2_H264_NaluFifoItem_t *item = ARSTREAM2_H264_NaluFifoPopFreeItem(filter->naluFifo);
            ARSAL_Mutex_Unlock(filter->fifoMutex);
            if (item)
            {
                ARSTREAM2_H264_NaluReset(&item->nalu);
                err = ARSTREAM2_H264_AuCheckSizeRealloc(au, 16);
                if (err != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "Access unit buffer is too small");
                    ret = -1;
                }

                if (ret == 0)
                {
                    /* NALU data */
                    item->nalu.nalu = au->buffer->auBuffer + au->auSize;
                    item->nalu.naluSize = 0;

                    unsigned int outputSize;
                    err = ARSTREAM2_H264Writer_WriteSkippedPSliceNalu(filter->writer, firstMbInSlice, missingMb, sliceContext,
                                                                      au->buffer->auBuffer + au->auSize, au->buffer->auBufferSize - au->auSize, &outputSize);
                    if (err != ARSTREAM2_OK)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "ARSTREAM2_H264Writer_WriteSkippedPSliceNalu() failed (%d)", err);
                        ret = -1;
                    }
                    else
                    {
                        //ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_H264_FILTER_ERROR_TAG, "#%d AUTS:%llu Skipped P slice NALU output size: %d", filter->currentAuOutputIndex, au->ntpTimestamp, outputSize);
                        item->nalu.naluSize = outputSize;
                        au->auSize += outputSize;

                        item->nalu.inputTimestamp = nextNaluItem->nalu.inputTimestamp;
                        item->nalu.timeoutTimestamp = nextNaluItem->nalu.timeoutTimestamp;
                        item->nalu.ntpTimestamp = nextNaluItem->nalu.ntpTimestamp;
                        item->nalu.ntpTimestampLocal = nextNaluItem->nalu.ntpTimestampLocal;
                        item->nalu.extRtpTimestamp = nextNaluItem->nalu.extRtpTimestamp;
                        item->nalu.rtpTimestamp = nextNaluItem->nalu.rtpTimestamp;
                        item->nalu.missingPacketsBefore = 0;
                        item->nalu.naluType = ARSTREAM2_H264_NALU_TYPE_SLICE;
                        item->nalu.sliceType = ARSTREAM2_H264_SLICE_TYPE_P;

                        err = ARSTREAM2_H264_AuEnqueueNaluBefore(au, item, nextNaluItem);
                        if (err != 0)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "Failed to enqueue NALU item in AU");
                            ret = -1;
                        }
                        else if (filter->currentAuMacroblockStatus)
                        {
                            if (firstMbInSlice + missingMb > filter->mbCount) missingMb = filter->mbCount - firstMbInSlice;
                            memset(filter->currentAuMacroblockStatus + firstMbInSlice,
                                   ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_MISSING_CONCEALED, missingMb);
                        }
                    }
                }

                if (ret != 0)
                {
                    ARSAL_Mutex_Lock(filter->fifoMutex);
                    err = ARSTREAM2_H264_NaluFifoPushFreeItem(filter->naluFifo, item);
                    ARSAL_Mutex_Unlock(filter->fifoMutex);
                    if (err < 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "Failed to push free FIFO item");
                    }
                }
            }
            else
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "NALU FIFO is full, cannot generate skipped P slice");
                ret = -1;
            }
        }

        if ((ret != 0) && (filter->currentAuMacroblockStatus))
        {
            if (firstMbInSlice + missingMb > filter->mbCount) missingMb = filter->mbCount - firstMbInSlice;
            memset(filter->currentAuMacroblockStatus + firstMbInSlice,
                   ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_MISSING, missingMb);
        }
    }

    return ret;
}


int ARSTREAM2_H264FilterError_HandleMissingEndOfFrame(ARSTREAM2_H264Filter_t *filter, ARSTREAM2_H264_AccessUnit_t *au,
                                                      ARSTREAM2_H264_NaluFifoItem_t *prevNaluItem)
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
                //ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_H264_FILTER_ERROR_TAG, "#%d AUTS:%llu No previous slice received, aborting", filter->currentAuOutputIndex, au->ntpTimestamp);
                ret = -1;
            }
            else
            {
                // Slices have been received before
                firstMbInSlice = filter->currentAuPreviousSliceFirstMb + filter->currentAuStreamingSliceMbCount[filter->currentAuPreviousSliceIndex];
                missingMb = filter->mbCount - firstMbInSlice;
                ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_H264_FILTER_ERROR_TAG, "#%d AUTS:%llu missingMb:%d firstMbInSlice:%d", filter->currentAuOutputIndex, au->ntpTimestamp, missingMb, firstMbInSlice); //TODO: debug
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
                           ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_MISSING, missingMb);
                }
            }
        }
    }

    if (ret == 0)
    {
        //ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_H264_FILTER_ERROR_TAG, "#%d AUTS:%llu Missing NALU is probably a slice", filter->currentAuOutputIndex, au->ntpTimestamp);

        if (!filter->sync)
        {
            //ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_H264_FILTER_ERROR_TAG, "#%d AUTS:%llu No sync, abort", filter->currentAuOutputIndex, au->ntpTimestamp);
            ret = -2;
        }
    }

    if (ret == 0)
    {
        if (!filter->generateSkippedPSlices)
        {
            //ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_H264_FILTER_ERROR_TAG, "#%d AUTS:%llu Missing NALU is probably a slice", filter->currentAuOutputIndex, au->ntpTimestamp);
            if (missingMb > 0)
            {
                if (firstMbInSlice + missingMb > filter->mbCount) missingMb = filter->mbCount - firstMbInSlice;
                memset(filter->currentAuMacroblockStatus + firstMbInSlice,
                       ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_MISSING, missingMb);
            }
            ret = -2;
        }
    }

    if (ret == 0)
    {
        if (!filter->currentAuStreamingInfoAvailable)
        {
            //ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_H264_FILTER_ERROR_TAG, "#%d AUTS:%llu Streaming info is not available", filter->currentAuOutputIndex, au->ntpTimestamp);
            if (missingMb > 0)
            {
                if (firstMbInSlice + missingMb > filter->mbCount) missingMb = filter->mbCount - firstMbInSlice;
                memset(filter->currentAuMacroblockStatus + firstMbInSlice,
                       ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_MISSING, missingMb);
            }
            ret = -2;
        }
    }

    if (ret == 0)
    {
        if (prevNaluItem->nalu.sliceType != ARSTREAM2_H264_SLICE_TYPE_P)
        {
            //ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_H264_FILTER_ERROR_TAG, "#%d AUTS:%llu Previous slice is not a P-slice, aborting", filter->currentAuOutputIndex, au->ntpTimestamp);
            if (missingMb > 0)
            {
                if (firstMbInSlice + missingMb > filter->mbCount) missingMb = filter->mbCount - firstMbInSlice;
                memset(filter->currentAuMacroblockStatus + firstMbInSlice,
                       ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_MISSING, missingMb);
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
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "ARSTREAM2_H264Parser_GetSliceContext() failed (%d)", err);
            ret = -1;
        }
        if (ret == 0)
        {
            ARSAL_Mutex_Lock(filter->fifoMutex);
            ARSTREAM2_H264_NaluFifoItem_t *item = ARSTREAM2_H264_NaluFifoPopFreeItem(filter->naluFifo);
            ARSAL_Mutex_Unlock(filter->fifoMutex);
            if (item)
            {
                ARSTREAM2_H264_NaluReset(&item->nalu);
                err = ARSTREAM2_H264_AuCheckSizeRealloc(au, 16);
                if (err != 0)
                {
                    ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "Access unit buffer is too small");
                    ret = -1;
                }

                if (ret == 0)
                {
                    /* NALU data */
                    item->nalu.nalu = au->buffer->auBuffer + au->auSize;
                    item->nalu.naluSize = 0;

                    unsigned int outputSize;
                    err = ARSTREAM2_H264Writer_WriteSkippedPSliceNalu(filter->writer, firstMbInSlice, missingMb, sliceContext,
                                                                      au->buffer->auBuffer + au->auSize, au->buffer->auBufferSize - au->auSize, &outputSize);
                    if (err != ARSTREAM2_OK)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "ARSTREAM2_H264Writer_WriteSkippedPSliceNalu() failed (%d)", err);
                        ret = -1;
                    }
                    else
                    {
                        //ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_H264_FILTER_ERROR_TAG, "#%d AUTS:%llu Skipped P slice NALU output size: %d", filter->currentAuOutputIndex, au->ntpTimestamp, outputSize);
                        item->nalu.naluSize = outputSize;
                        au->auSize += outputSize;

                        item->nalu.inputTimestamp = prevNaluItem->nalu.inputTimestamp;
                        item->nalu.timeoutTimestamp = prevNaluItem->nalu.timeoutTimestamp;
                        item->nalu.ntpTimestamp = prevNaluItem->nalu.ntpTimestamp;
                        item->nalu.ntpTimestampLocal = prevNaluItem->nalu.ntpTimestampLocal;
                        item->nalu.extRtpTimestamp = prevNaluItem->nalu.extRtpTimestamp;
                        item->nalu.rtpTimestamp = prevNaluItem->nalu.rtpTimestamp;
                        item->nalu.isLastInAu = 1;
                        item->nalu.missingPacketsBefore = 0;
                        item->nalu.naluType = ARSTREAM2_H264_NALU_TYPE_SLICE;
                        item->nalu.sliceType = ARSTREAM2_H264_SLICE_TYPE_P;

                        err = ARSTREAM2_H264_AuEnqueueNalu(au, item);
                        if (err != 0)
                        {
                            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "Failed to enqueue NALU item in AU");
                            ret = -1;
                        }
                        else if (filter->currentAuMacroblockStatus)
                        {
                            if (firstMbInSlice + missingMb > filter->mbCount) missingMb = filter->mbCount - firstMbInSlice;
                            memset(filter->currentAuMacroblockStatus + firstMbInSlice,
                                   ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_MISSING_CONCEALED, missingMb);
                        }
                    }
                }

                if (ret != 0)
                {
                    ARSAL_Mutex_Lock(filter->fifoMutex);
                    err = ARSTREAM2_H264_NaluFifoPushFreeItem(filter->naluFifo, item);
                    ARSAL_Mutex_Unlock(filter->fifoMutex);
                    if (err < 0)
                    {
                        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "Failed to push free FIFO item");
                    }
                }
            }
            else
            {
                ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_H264_FILTER_ERROR_TAG, "NALU FIFO is full, cannot generate skipped P slice");
                ret = -1;
            }
        }

        if ((ret != 0) && (filter->currentAuMacroblockStatus))
        {
            if (firstMbInSlice + missingMb > filter->mbCount) missingMb = filter->mbCount - firstMbInSlice;
            memset(filter->currentAuMacroblockStatus + firstMbInSlice,
                   ARSTREAM2_STREAM_RECEIVER_MACROBLOCK_STATUS_MISSING, missingMb);
        }
    }

    return ret;
}
