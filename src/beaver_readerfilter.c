/**
 * @file beaver_readerfilter.c
 * @brief H.264 Elementary Stream Reader and Filter
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#include <stdio.h>
#include <stdlib.h>

#include <libARSAL/ARSAL_Print.h>

#include <beaver/beaver_readerfilter.h>


#define BEAVER_READERFILTER_TAG "BEAVER_ReaderFilter"


typedef struct BEAVER_ReaderFilter_s
{
    BEAVER_Filter_Handle filter;
    ARSTREAM_Reader2_t *reader;

} BEAVER_ReaderFilter_t;



int BEAVER_ReaderFilter_Init(BEAVER_ReaderFilter_Handle *readerFilterHandle, BEAVER_ReaderFilter_Config_t *config)
{
    int ret = 0;
    eARSTREAM_ERROR err = ARSTREAM_OK;
    BEAVER_ReaderFilter_t *readerFilter = NULL;

    if (!readerFilterHandle)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_READERFILTER_TAG, "Invalid pointer for handle");
        return -1;
    }
    if (!config)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_READERFILTER_TAG, "Invalid pointer for config");
        return -1;
    }

    readerFilter = (BEAVER_ReaderFilter_t*)malloc(sizeof(*readerFilter));
    if (!readerFilter)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_READERFILTER_TAG, "Allocation failed (size %ld)", sizeof(*readerFilter));
        ret = -1;
    }

    if (ret == 0)
    {
        memset(readerFilter, 0, sizeof(*readerFilter));

        BEAVER_Filter_Config_t filterConfig;
        memset(&filterConfig, 0, sizeof(filterConfig));
        filterConfig.spsPpsCallback = config->spsPpsCallback;
        filterConfig.spsPpsCallbackUserPtr = config->spsPpsCallbackUserPtr;
        filterConfig.getAuBufferCallback = config->getAuBufferCallback;
        filterConfig.getAuBufferCallbackUserPtr = config->getAuBufferCallbackUserPtr;
        filterConfig.cancelAuBufferCallback = config->cancelAuBufferCallback;
        filterConfig.cancelAuBufferCallbackUserPtr = config->cancelAuBufferCallbackUserPtr;
        filterConfig.auReadyCallback = config->auReadyCallback;
        filterConfig.auReadyCallbackUserPtr = config->auReadyCallbackUserPtr;
        filterConfig.auFifoSize = config->auFifoSize;
        filterConfig.waitForSync = config->waitForSync;
        filterConfig.outputIncompleteAu = config->outputIncompleteAu;
        filterConfig.filterOutSpsPps = config->filterOutSpsPps;
        filterConfig.filterOutSei = config->filterOutSei;
        filterConfig.replaceStartCodesWithNaluSize = config->replaceStartCodesWithNaluSize;

        ret = BEAVER_Filter_Init(&readerFilter->filter, &filterConfig);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_READERFILTER_TAG, "Error while creating beaver_filter: %d", ret);
            ret = -1;
        }
    }

    if (ret == 0)
    {
        ARSTREAM_Reader2_Config_t readerConfig;
        memset(&readerConfig, 0, sizeof(readerConfig));
        readerConfig.serverAddr = config->serverAddr;
        readerConfig.mcastAddr = config->mcastAddr;
        readerConfig.mcastIfaceAddr = config->mcastIfaceAddr;
        readerConfig.serverStreamPort = config->serverStreamPort;
        readerConfig.serverControlPort = config->serverControlPort;
        readerConfig.clientStreamPort = config->clientStreamPort;
        readerConfig.clientControlPort = config->clientControlPort;
        readerConfig.naluCallback = BEAVER_Filter_ArstreamReader2NaluCallback;
        readerConfig.naluCallbackUserPtr = (void*)readerFilter->filter;
        readerConfig.maxPacketSize = config->maxPacketSize;
        readerConfig.maxBitrate = config->maxBitrate;
        readerConfig.maxLatencyMs = config->maxLatencyMs;
        readerConfig.maxNetworkLatencyMs = config->maxNetworkLatencyMs;
        readerConfig.insertStartCodes = 1;

        readerFilter->reader = ARSTREAM_Reader2_New(&readerConfig, &err);
        if (err != ARSTREAM_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_READERFILTER_TAG, "Error while creating reader : %s", ARSTREAM_Error_ToString(err));
            ret = -1;
        }
    }

    if (ret == 0)
    {
        *readerFilterHandle = (BEAVER_ReaderFilter_Handle*)readerFilter;
    }
    else
    {
        if (readerFilter)
        {
            if (readerFilter->reader) ARSTREAM_Reader2_Delete(&(readerFilter->reader));
            if (readerFilter->filter) BEAVER_Filter_Free(&(readerFilter->filter));
            free(readerFilter);
        }
        *readerFilterHandle = NULL;
    }

    return ret;
}


int BEAVER_ReaderFilter_Free(BEAVER_ReaderFilter_Handle *readerFilterHandle)
{
    BEAVER_ReaderFilter_t* readerFilter;
    int ret = 0;
    eARSTREAM_ERROR err;

    if ((!readerFilterHandle) || (!*readerFilterHandle))
    {
        return -1;
    }

    readerFilter = (BEAVER_ReaderFilter_t*)*readerFilterHandle;

    err = ARSTREAM_Reader2_Delete(&readerFilter->reader);
    if (err != ARSTREAM_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_READERFILTER_TAG, "Unable to delete reader: %s", ARSTREAM_Error_ToString(err));
        ret = -1;
    }

    if (ret == 0)
    {
        ret = BEAVER_Filter_Free(&readerFilter->filter);
        if (ret != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_READERFILTER_TAG, "Unable to delete beaver_filter: %d", ret);
            ret = -1;
        }
    }

    return ret;
}


void* BEAVER_ReaderFilter_RunFilterThread(void *readerFilterHandle)
{
    BEAVER_ReaderFilter_t* readerFilter = (BEAVER_ReaderFilter_t*)readerFilterHandle;

    return BEAVER_Filter_RunFilterThread((void*)readerFilter->filter);
}


void* BEAVER_ReaderFilter_RunStreamThread(void *readerFilterHandle)
{
    BEAVER_ReaderFilter_t* readerFilter = (BEAVER_ReaderFilter_t*)readerFilterHandle;

    return ARSTREAM_Reader2_RunStreamThread((void*)readerFilter->reader);
}


void* BEAVER_ReaderFilter_RunControlThread(void *readerFilterHandle)
{
    BEAVER_ReaderFilter_t* readerFilter = (BEAVER_ReaderFilter_t*)readerFilterHandle;

    return ARSTREAM_Reader2_RunControlThread((void*)readerFilter->reader);
}


int BEAVER_ReaderFilter_Stop(BEAVER_ReaderFilter_Handle readerFilterHandle)
{
    BEAVER_ReaderFilter_t* readerFilter = (BEAVER_ReaderFilter_t*)readerFilterHandle;
    int ret = 0;

    ARSTREAM_Reader2_StopReader(readerFilter->reader);

    ret = BEAVER_Filter_Stop(readerFilter->filter);
    if (ret != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_READERFILTER_TAG, "Unable to stop beaver_filter: %d", ret);
        return ret;
    }

    return ret;
}


int BEAVER_ReaderFilter_GetSpsPps(BEAVER_ReaderFilter_Handle readerFilterHandle, uint8_t *spsBuffer, int *spsSize, uint8_t *ppsBuffer, int *ppsSize)
{
    BEAVER_ReaderFilter_t* readerFilter = (BEAVER_ReaderFilter_t*)readerFilterHandle;

    if (!readerFilterHandle)
    {
        return -1;
    }

    return BEAVER_Filter_GetSpsPps(readerFilter->filter, spsBuffer, spsSize, ppsBuffer, ppsSize);
}

