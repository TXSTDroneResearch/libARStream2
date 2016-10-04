/**
 * @file arstream2_stream_stats.h
 * @brief Parrot Streaming Library - Stream Stats
 * @date 10/04/2016
 * @author aurelien.barre@parrot.com
 */

#ifndef _ARSTREAM2_STREAM_STATS_H_
#define _ARSTREAM2_STREAM_STATS_H_

#include "arstream2_h264.h"


typedef struct ARSTREAM2_StreamStats_VideoStats_s
{
    uint64_t fileOutputTimestamp;
    FILE *outputFile;

} ARSTREAM2_StreamStats_VideoStats_t;


void ARSTREAM2_StreamStats_VideoStatsFileOpen(ARSTREAM2_StreamStats_VideoStats_t *context, const char *debugPath, const char *friendlyName, const char *dateAndTime);
void ARSTREAM2_StreamStats_VideoStatsFileClose(ARSTREAM2_StreamStats_VideoStats_t *context);
void ARSTREAM2_StreamStats_VideoStatsFileWrite(ARSTREAM2_StreamStats_VideoStats_t *context, const ARSTREAM2_H264_VideoStats_t *videoStats);


#endif /* _ARSTREAM2_STREAM_STATS_H_ */
