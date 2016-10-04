/**
 * @file arstream2_stream_stats.c
 * @brief Parrot Streaming Library - Stream Stats
 * @date 10/04/2016
 * @author aurelien.barre@parrot.com
 */

#include <stdio.h>
#include <stdlib.h>

#include <libARSAL/ARSAL_Print.h>

#include "arstream2_stream_stats.h"


#define ARSTREAM2_STREAM_STATS_TAG "ARSTREAM2_StreamStats"

#define ARSTREAM2_STREAM_STATS_VIDEO_STATS_OUTPUT_PATH "videostats"
#define ARSTREAM2_STREAM_STATS_VIDEO_STATS_OUTPUT_FILENAME "videostats"
#define ARSTREAM2_STREAM_STATS_VIDEO_STATS_OUTPUT_FILEEXT "dat"

#define ARSTREAM2_STREAM_STATS_VIDEO_STATS_OUTPUT_INTERVAL (1000000)


void ARSTREAM2_StreamStats_VideoStatsFileOpen(ARSTREAM2_StreamStats_VideoStats_t *context, const char *debugPath, const char *friendlyName, const char *dateAndTime)
{
    char szOutputFileName[500];
    szOutputFileName[0] = '\0';

    if ((!context) || (!dateAndTime))
    {
        return;
    }

    if ((debugPath) && (strlen(debugPath)))
    {
        snprintf(szOutputFileName, 500, "%s/%s", debugPath,
                 ARSTREAM2_STREAM_STATS_VIDEO_STATS_OUTPUT_PATH);
        if ((access(szOutputFileName, F_OK) == 0) && (access(szOutputFileName, W_OK) == 0))
        {
            // directory exists and we have write permission
            snprintf(szOutputFileName, 500, "%s/%s/%s_%s.%s", debugPath,
                     ARSTREAM2_STREAM_STATS_VIDEO_STATS_OUTPUT_PATH,
                     ARSTREAM2_STREAM_STATS_VIDEO_STATS_OUTPUT_FILENAME,
                     dateAndTime,
                     ARSTREAM2_STREAM_STATS_VIDEO_STATS_OUTPUT_FILEEXT);
            if (access(szOutputFileName, F_OK) != -1)
            {
                // the file already exists
                szOutputFileName[0] = '\0';
            }
        }
        else
        {
            szOutputFileName[0] = '\0';
        }
    }

    if (strlen(szOutputFileName))
    {
        context->outputFile = fopen(szOutputFileName, "w");
        if (!context->outputFile)
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, ARSTREAM2_STREAM_STATS_TAG, "Unable to open video stats output file '%s'", szOutputFileName);
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_STREAM_STATS_TAG, "Opened video stats output file '%s'", szOutputFileName);
        }
    }

    if (context->outputFile)
    {
        char szTitle[200];
        int titleLen = 0;
        szTitle[0] = '\0';
        if ((friendlyName) && (strlen(friendlyName)))
        {
            titleLen += snprintf(szTitle + titleLen, 200 - titleLen, "%s ", friendlyName);
        }
        titleLen += snprintf(szTitle + titleLen, 200 - titleLen, "%s", dateAndTime);
        ARSAL_PRINT(ARSAL_PRINT_INFO, ARSTREAM2_STREAM_STATS_TAG, "Video stats output file title: '%s'", szTitle);
        fprintf(context->outputFile, "# %s\n", szTitle);
        fprintf(context->outputFile, "timestamp rssi totalFrameCount outputFrameCount erroredOutputFrameCount discardedFrameCount missedFrameCount erroredSecondCount");
        int i, j;
        for (i = 0; i < ARSTREAM2_H264_MB_STATUS_ZONE_COUNT; i++)
        {
            fprintf(context->outputFile, " erroredSecondCountByZone[%d]", i);
        }
        for (j = 0; j < ARSTREAM2_H264_MB_STATUS_CLASS_COUNT; j++)
        {
            for (i = 0; i < ARSTREAM2_H264_MB_STATUS_ZONE_COUNT; i++)
            {
                fprintf(context->outputFile, " macroblockStatus[%d][%d]", j, i);
            }
        }
        fprintf(context->outputFile, " timestampDeltaIntegral timestampDeltaIntegralSq timingErrorIntegral timingErrorIntegralSq estimatedLatencyIntegral estimatedLatencyIntegralSq");
        fprintf(context->outputFile, "\n");
        context->fileOutputTimestamp = 0;
    }
}


void ARSTREAM2_StreamStats_VideoStatsFileClose(ARSTREAM2_StreamStats_VideoStats_t *context)
{
    if (context->outputFile)
    {
        fclose(context->outputFile);
        context->outputFile = NULL;
    }
}


void ARSTREAM2_StreamStats_VideoStatsFileWrite(ARSTREAM2_StreamStats_VideoStats_t *context, const ARSTREAM2_H264_VideoStats_t *videoStats)
{
    if ((!context) || (!videoStats))
    {
        return;
    }

    if (!context->outputFile)
    {
        return;
    }

    if (context->fileOutputTimestamp == 0)
    {
        /* init */
        context->fileOutputTimestamp = videoStats->timestamp;
    }
    if (videoStats->timestamp >= context->fileOutputTimestamp + ARSTREAM2_STREAM_STATS_VIDEO_STATS_OUTPUT_INTERVAL)
    {
        if (context->outputFile)
        {
            fprintf(context->outputFile, "%llu %i %lu %lu %lu %lu %lu %lu", (long long unsigned int)videoStats->timestamp, videoStats->rssi,
                    (long unsigned int)videoStats->totalFrameCount, (long unsigned int)videoStats->outputFrameCount,
                    (long unsigned int)videoStats->erroredOutputFrameCount, (long unsigned int)videoStats->discardedFrameCount,
                    (long unsigned int)videoStats->missedFrameCount, (long unsigned int)videoStats->erroredSecondCount);
            int i, j;
            for (i = 0; i < ARSTREAM2_H264_MB_STATUS_ZONE_COUNT; i++)
            {
                fprintf(context->outputFile, " %lu", (long unsigned int)videoStats->erroredSecondCountByZone[i]);
            }
            for (j = 0; j < ARSTREAM2_H264_MB_STATUS_CLASS_COUNT; j++)
            {
                for (i = 0; i < ARSTREAM2_H264_MB_STATUS_ZONE_COUNT; i++)
                {
                    fprintf(context->outputFile, " %lu", (long unsigned int)videoStats->macroblockStatus[j][i]);
                }
            }
            fprintf(context->outputFile, " %llu %llu %llu %llu %llu %llu",
                    (long long unsigned int)videoStats->timestampDeltaIntegral, (long long unsigned int)videoStats->timestampDeltaIntegralSq,
                    (long long unsigned int)videoStats->timingErrorIntegral,(long long unsigned int)videoStats->timingErrorIntegralSq,
                    (long long unsigned int)videoStats->estimatedLatencyIntegral, (long long unsigned int)videoStats->estimatedLatencyIntegralSq);
            fprintf(context->outputFile, "\n");
        }
        context->fileOutputTimestamp = videoStats->timestamp;
    }
}
