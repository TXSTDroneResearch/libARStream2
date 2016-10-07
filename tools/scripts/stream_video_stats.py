#!/usr/bin/python

# @file stream_video_stats.py
# @brief Streaming video stats script
# @date 09/21/2016
# @author aurelien.barre@parrot.com


import sys, getopt, math
import numpy as np
import matplotlib.pyplot as plt
import pandas as pd


def videoStats(inFile, outFile):
    fig1 = plt.figure()
    plt.subplots_adjust(left=0.03, right=0.97, bottom=0.03, top=0.92)
    f = open(inFile, 'r')
    firstLine = f.readline()
    f.close()
    if firstLine != '' and firstLine[0] == '#':
        title = firstLine[1:]
        title = title.strip()
    else:
        title = inFile
    plt.suptitle('Video stats (' + title + ')', fontsize=16, color='0.4')

    data = pd.read_csv(inFile, sep=' ', comment='#', skip_blank_lines=True)


    # for compatibility with old stats files
    if 'erroredSecondCount' in data.columns:
        lblEsr = 'erroredSecondCount'
        lblEsrZ0 = 'erroredSecondCountByZone[0]'
        lblEsrZ1 = 'erroredSecondCountByZone[1]'
        lblEsrZ2 = 'erroredSecondCountByZone[2]'
        lblEsrZ3 = 'erroredSecondCountByZone[3]'
        lblEsrZ4 = 'erroredSecondCountByZone[4]'
    else:
        lblEsr = 'errorSecondCount'
        lblEsrZ0 = 'errorSecondCountByZone[0]'
        lblEsrZ1 = 'errorSecondCountByZone[1]'
        lblEsrZ2 = 'errorSecondCountByZone[2]'
        lblEsrZ3 = 'errorSecondCountByZone[3]'
        lblEsrZ4 = 'errorSecondCountByZone[4]'
    if 'erroredOutputFrameCount' in data.columns:
        lblErroredOutputFrameCount = 'erroredOutputFrameCount'
    else:
        lblErroredOutputFrameCount = ''


    # overall frames
    overallTotalFrameCount = float(data['totalFrameCount'].iat[-1])
    if lblErroredOutputFrameCount != '':
        overallErroredOutputFrameCount = float(data[lblErroredOutputFrameCount].iat[-1])
    else:
        overallErroredOutputFrameCount = 0
    overallNoErrorOutputFrameCount = float(data['outputFrameCount'].iat[-1]) - overallErroredOutputFrameCount
    overallDiscardedFrameCount = float(data['discardedFrameCount'].iat[-1])
    overallMissedFrameCount = float(data['missedFrameCount'].iat[-1]) - overallDiscardedFrameCount
    overallTotalFrameCount2 = overallErroredOutputFrameCount + overallNoErrorOutputFrameCount + overallDiscardedFrameCount + overallMissedFrameCount
    if overallTotalFrameCount2 != overallTotalFrameCount:
        print "Warning - overall frame count missmatch: " + str(int(overallTotalFrameCount)) + " vs. " + str(int(overallTotalFrameCount2))

    axOverallFrames = fig1.add_subplot(3, 4, 1)
    axOverallFrames.set_title("Overall frames", color='0.4')
    axOverallFrames.pie([overallNoErrorOutputFrameCount, overallErroredOutputFrameCount, overallDiscardedFrameCount, overallMissedFrameCount], labels=['Output (no errors)', 'Output (with errors)', 'Discarded', 'Missed'], colors=['lightgreen', 'cadetblue', 'salmon', 'firebrick'], autopct='%1.1f%%', startangle=90)
    axOverallFrames.set_aspect('equal')


    # frames by RSSI
    dataTotalFrameDelta = np.subtract(data['totalFrameCount'][1:], data['totalFrameCount'][:-1])
    if lblErroredOutputFrameCount != '':
        dataErroredOutputFrameDelta = np.subtract(data[lblErroredOutputFrameCount][1:], data[lblErroredOutputFrameCount][:-1])
    else:
        dataErroredOutputFrameDelta = np.empty(len(dataTotalFrameDelta))
        dataErroredOutputFrameDelta.fill(0)
    dataNoErrorOutputFrameDelta = np.subtract(np.subtract(data['outputFrameCount'][1:], data['outputFrameCount'][:-1]), dataErroredOutputFrameDelta)
    dataDiscardedFrameDelta = np.subtract(data['discardedFrameCount'][1:], data['discardedFrameCount'][:-1])
    dataMissedFrameDelta = np.subtract(np.subtract(data['missedFrameCount'][1:], data['missedFrameCount'][:-1]), dataDiscardedFrameDelta)
    rssiMin = np.amin(data['rssi'])
    rssiMax = np.amax(data['rssi'])
    rssiCount = rssiMax - rssiMin + 1
    dataRssi = np.array(data['rssi'][1:])

    totalFrameCountByRssi = np.empty(rssiCount)
    totalFrameCountByRssi.fill(0)
    for i, val in enumerate(dataTotalFrameDelta):
        totalFrameCountByRssi[dataRssi[i] - rssiMin] += float(val)

    erroredOutputFrameCountByRssi = np.empty(rssiCount)
    erroredOutputFrameCountByRssi.fill(0)
    for i, val in enumerate(dataErroredOutputFrameDelta):
        erroredOutputFrameCountByRssi[dataRssi[i] - rssiMin] += float(val)
    erroredOutputFrameCountByRssi = np.divide(erroredOutputFrameCountByRssi, totalFrameCountByRssi)

    noErrorOutputFrameCountByRssi = np.empty(rssiCount)
    noErrorOutputFrameCountByRssi.fill(0)
    for i, val in enumerate(dataNoErrorOutputFrameDelta):
        noErrorOutputFrameCountByRssi[dataRssi[i] - rssiMin] += float(val)
    noErrorOutputFrameCountByRssi = np.divide(noErrorOutputFrameCountByRssi, totalFrameCountByRssi)

    discardedFrameCountByRssi = np.empty(rssiCount)
    discardedFrameCountByRssi.fill(0)
    for i, val in enumerate(dataDiscardedFrameDelta):
        discardedFrameCountByRssi[dataRssi[i] - rssiMin] += float(val)
    discardedFrameCountByRssi = np.divide(discardedFrameCountByRssi, totalFrameCountByRssi)

    missedFrameCountByRssi = np.empty(rssiCount)
    missedFrameCountByRssi.fill(0)
    for i, val in enumerate(dataMissedFrameDelta):
        missedFrameCountByRssi[dataRssi[i] - rssiMin] += float(val)
    missedFrameCountByRssi = np.divide(missedFrameCountByRssi, totalFrameCountByRssi)

    totalFrameCountByRssi = np.divide(totalFrameCountByRssi, overallTotalFrameCount)
    sum1 = np.add(missedFrameCountByRssi, discardedFrameCountByRssi)
    sum2 = np.add(sum1, erroredOutputFrameCountByRssi)

    axFramesByRssi = fig1.add_subplot(3, 2, 2)
    axFramesByRssi.set_title("Frames by RSSI", color='0.4')
    width = 0.4
    rssiVal = np.arange(rssiMin, rssiMax + 1)
    axFramesByRssi.bar(rssiVal - width, missedFrameCountByRssi * 100., width, color='firebrick')
    axFramesByRssi.bar(rssiVal - width, discardedFrameCountByRssi * 100., width, color='salmon', bottom=missedFrameCountByRssi*100.)
    axFramesByRssi.bar(rssiVal - width, erroredOutputFrameCountByRssi * 100., width, color='cadetblue', bottom=sum1*100.)
    axFramesByRssi.bar(rssiVal - width, noErrorOutputFrameCountByRssi * 100., width, color='lightgreen', bottom=sum2*100.)
    axFramesByRssi.bar(rssiVal, totalFrameCountByRssi * 100., width, color='0.4')
    rssiMax2 = rssiMin
    rssiMin2 = 0
    for i, val in enumerate(totalFrameCountByRssi):
        if val > 0:
            rssiMax2 = rssiVal[i]
            if rssiMin2 == 0:
                rssiMin2 = rssiVal[i]
    axFramesByRssi.set_ylim(0, 100)
    axFramesByRssi.set_xlim(rssiMin2 - 1, rssiMax2 + 1)
    axFramesByRssi.set_xlabel('RSSI (dBm)')
    axFramesByRssi.set_ylabel('(%)')


    # ESR by zone
    totalTime = float(data['timestamp'].iat[-1] - data['timestamp'].iat[0] + 1000000) / 1000000.
    zoneEsr = [float(data[lblEsrZ4].iat[-1]) / totalTime * 100., float(data[lblEsrZ3].iat[-1]) / totalTime * 100., float(data[lblEsrZ2].iat[-1]) / totalTime * 100., float(data[lblEsrZ1].iat[-1]) / totalTime * 100., float(data[lblEsrZ0].iat[-1]) / totalTime * 100.]
    zoneEsrYPos = np.arange(5)
    zoneEsrLabel = ('5', '4', '3', '2', '1')

    axZoneEsr = fig1.add_subplot(3, 4, 2)
    axZoneEsr.set_xlim(0, 100)
    axZoneEsr.set_title("ESR by zone", color='0.4')
    axZoneEsr.barh(zoneEsrYPos, zoneEsr, align='center', alpha=0.8, color='cadetblue')
    axZoneEsr.set_yticks(zoneEsrYPos)
    axZoneEsr.set_yticklabels(zoneEsrLabel)
    axZoneEsr.set_xlabel('ESR (%)')
    axZoneEsr.set_ylabel('Zone')


    # timings by RSSI
    dataOutputFrameCount = np.subtract(data['outputFrameCount'][1:], data['outputFrameCount'][:-1])
    outputFrameCountByRssi = np.empty(rssiCount)
    outputFrameCountByRssi.fill(0)
    for i, val in enumerate(dataOutputFrameCount):
        outputFrameCountByRssi[dataRssi[i] - rssiMin] += float(val)

    if 'timestampDeltaIntegral' in data.columns:
        dataTimestampDelta = np.subtract(data['timestampDeltaIntegral'][1:], data['timestampDeltaIntegral'][:-1])
        dataTimestampDeltaSq = np.subtract(data['timestampDeltaIntegralSq'][1:], data['timestampDeltaIntegralSq'][:-1])
    else:
        dataTimestampDelta = []
        dataTimestampDeltaSq = []
    timestampDeltaByRssi = np.empty(rssiCount)
    timestampDeltaByRssi.fill(0)
    for i, val in enumerate(dataTimestampDelta):
        timestampDeltaByRssi[dataRssi[i] - rssiMin] += float(val) / 1000.
    timestampDeltaByRssi = np.divide(timestampDeltaByRssi, outputFrameCountByRssi)
    timestampDeltaByRssiStd = np.empty(rssiCount)
    timestampDeltaByRssiStd.fill(0)
    for i, val in enumerate(dataTimestampDeltaSq):
        timestampDeltaByRssiStd[dataRssi[i] - rssiMin] += float(val) / 1000000.
    timestampDeltaByRssiStd = np.sqrt(np.subtract(np.divide(timestampDeltaByRssiStd, outputFrameCountByRssi), np.square(timestampDeltaByRssi)))

    if 'timingErrorIntegral' in data.columns:
        dataTimingError = np.subtract(data['timingErrorIntegral'][1:], data['timingErrorIntegral'][:-1])
        dataTimingErrorSq = np.subtract(data['timingErrorIntegralSq'][1:], data['timingErrorIntegralSq'][:-1])
    else:
        dataTimingError = []
        dataTimingErrorSq = []
    timingErrorByRssi = np.empty(rssiCount)
    timingErrorByRssi.fill(0)
    for i, val in enumerate(dataTimingError):
        timingErrorByRssi[dataRssi[i] - rssiMin] += float(val) / 1000.
    timingErrorByRssi = np.divide(timingErrorByRssi, outputFrameCountByRssi)
    timingErrorByRssiStd = np.empty(rssiCount)
    timingErrorByRssiStd.fill(0)
    for i, val in enumerate(dataTimingErrorSq):
        timingErrorByRssiStd[dataRssi[i] - rssiMin] += float(val) / 1000000.
    timingErrorByRssiStd = np.sqrt(np.subtract(np.divide(timingErrorByRssiStd, outputFrameCountByRssi), np.square(timingErrorByRssi)))

    if 'estimatedLatencyIntegral' in data.columns:
        dataEstimatedLatency = np.subtract(data['estimatedLatencyIntegral'][1:], data['estimatedLatencyIntegral'][:-1])
        dataEstimatedLatencySq = np.subtract(data['estimatedLatencyIntegralSq'][1:], data['estimatedLatencyIntegralSq'][:-1])
    else:
        dataEstimatedLatency = []
        dataEstimatedLatencySq = []
    estimatedLatencyByRssi = np.empty(rssiCount)
    estimatedLatencyByRssi.fill(0)
    for i, val in enumerate(dataEstimatedLatency):
        estimatedLatencyByRssi[dataRssi[i] - rssiMin] += float(val) / 1000.
    estimatedLatencyByRssi = np.divide(estimatedLatencyByRssi, outputFrameCountByRssi)
    estimatedLatencyByRssiStd = np.empty(rssiCount)
    estimatedLatencyByRssiStd.fill(0)
    for i, val in enumerate(dataEstimatedLatencySq):
        estimatedLatencyByRssiStd[dataRssi[i] - rssiMin] += float(val) / 1000000.
    estimatedLatencyByRssiStd = np.sqrt(np.subtract(np.divide(estimatedLatencyByRssiStd, outputFrameCountByRssi), np.square(estimatedLatencyByRssi)))

    axTimingsByRssi = fig1.add_subplot(3, 2, 4)
    axTimingsByRssi.set_title("Timings by RSSI", color='0.4')
    width = 0.2
    rssiVal = np.arange(rssiMin, rssiMax + 1)
    axTimingsByRssi.bar(rssiVal - width * 2, timestampDeltaByRssi, width, yerr=timestampDeltaByRssiStd, color='mediumseagreen')
    axTimingsByRssi.bar(rssiVal - width, timingErrorByRssi, width, yerr=timingErrorByRssiStd, color='firebrick')
    axTimingsByRssi.bar(rssiVal, estimatedLatencyByRssi, width, yerr=estimatedLatencyByRssiStd, color='cadetblue')
    axTimingsByRssi2 = axTimingsByRssi.twinx()
    axTimingsByRssi2.bar(rssiVal + width, outputFrameCountByRssi / float(data['outputFrameCount'].iat[-1]) * 100., width, color='0.4')
    rssiMax2 = rssiMin
    rssiMin2 = 0
    for i, val in enumerate(outputFrameCountByRssi):
        if val > 0:
            rssiMax2 = rssiVal[i]
            if rssiMin2 == 0:
                rssiMin2 = rssiVal[i]
    axTimingsByRssi.set_xlim(rssiMin2 - 1, rssiMax2 + 1)
    axTimingsByRssi.set_xlabel('RSSI (dBm)')
    a, b = axTimingsByRssi.get_ylim()
    axTimingsByRssi.set_ylim(0, b)
    axTimingsByRssi2.set_ylim(0, 100)
    axTimingsByRssi2.set_yticks([0, 50, 100])
    axTimingsByRssi.set_ylabel('(ms)')
    axTimingsByRssi2.set_ylabel('(%)')


    # overall timings
    if 'timestampDeltaIntegral' in data.columns:
        overallTimestampDelta = float(data['timestampDeltaIntegral'].iat[-1]) / float(data['outputFrameCount'].iat[-1]) / 1000.
        overallTimestampDeltaStd = math.sqrt(float(data['timestampDeltaIntegralSq'].iat[-1]) / float(data['outputFrameCount'].iat[-1]) / 1000000. - overallTimestampDelta * overallTimestampDelta)
        overallTimingError = float(data['timingErrorIntegral'].iat[-1]) / float(data['outputFrameCount'].iat[-1]) / 1000.
        overallTimingErrorStd = math.sqrt(float(data['timingErrorIntegralSq'].iat[-1]) / float(data['outputFrameCount'].iat[-1]) / 1000000. - overallTimingError * overallTimingError)
        overallEstimatedLatency = float(data['estimatedLatencyIntegral'].iat[-1]) / float(data['outputFrameCount'].iat[-1]) / 1000.
        overallEstimatedLatencyStd = math.sqrt(float(data['estimatedLatencyIntegralSq'].iat[-1]) / float(data['outputFrameCount'].iat[-1]) / 1000000. - overallEstimatedLatency * overallEstimatedLatency)
    else:
        overallTimestampDelta = 0.
        overallTimestampDeltaStd = 0.
        overallTimingError = 0.
        overallTimingErrorStd = 0.
        overallEstimatedLatency = 0.
        overallEstimatedLatencyStd = 0.
    axTimings = fig1.add_subplot(3, 4, 6)
    axTimings.set_title("Overall timings", color='0.4')
    width = 0.8
    axTimings.bar(1 - width / 2, overallTimestampDelta, width, yerr=overallTimestampDeltaStd, color='mediumseagreen')
    axTimings.bar(2 - width / 2, overallTimingError, width, yerr=overallTimingErrorStd, color='firebrick')
    axTimings.bar(3 - width / 2, overallEstimatedLatency, width, yerr=overallEstimatedLatencyStd, color='cadetblue')
    axTimings.set_xticks([1, 2, 3])
    axTimings.set_xticklabels(['Time delta', 'Output error', 'Est. latency'])
    a, b = axTimingsByRssi.get_ylim()
    axTimings.set_ylim(a, b)
    axTimings.set_ylabel('(ms)')


    # overall macroblocks
    overallUnknownMbCount = float(data['macroblockStatus[0][0]'].iat[-1] + data['macroblockStatus[0][1]'].iat[-1] + data['macroblockStatus[0][2]'].iat[-1] + data['macroblockStatus[0][3]'].iat[-1] + data['macroblockStatus[0][4]'].iat[-1])
    overallValidISliceMbCount = float(data['macroblockStatus[1][0]'].iat[-1] + data['macroblockStatus[1][1]'].iat[-1] + data['macroblockStatus[1][2]'].iat[-1] + data['macroblockStatus[1][3]'].iat[-1] + data['macroblockStatus[1][4]'].iat[-1])
    overallValidPSliceMbCount = float(data['macroblockStatus[2][0]'].iat[-1] + data['macroblockStatus[2][1]'].iat[-1] + data['macroblockStatus[2][2]'].iat[-1] + data['macroblockStatus[2][3]'].iat[-1] + data['macroblockStatus[2][4]'].iat[-1])
    overallMissingConcealedMbCount = float(data['macroblockStatus[3][0]'].iat[-1] + data['macroblockStatus[3][1]'].iat[-1] + data['macroblockStatus[3][2]'].iat[-1] + data['macroblockStatus[3][3]'].iat[-1] + data['macroblockStatus[3][4]'].iat[-1])
    overallMissingMbCount = float(data['macroblockStatus[4][0]'].iat[-1] + data['macroblockStatus[4][1]'].iat[-1] + data['macroblockStatus[4][2]'].iat[-1] + data['macroblockStatus[4][3]'].iat[-1] + data['macroblockStatus[4][4]'].iat[-1])
    overallErrorPropagationMbCount = float(data['macroblockStatus[5][0]'].iat[-1] + data['macroblockStatus[5][1]'].iat[-1] + data['macroblockStatus[5][2]'].iat[-1] + data['macroblockStatus[5][3]'].iat[-1] + data['macroblockStatus[5][4]'].iat[-1])
    overallValidTotalMbCount = overallValidISliceMbCount + overallValidPSliceMbCount
    overallInvalidTotalMbCount = overallUnknownMbCount + overallMissingConcealedMbCount + overallMissingMbCount + overallErrorPropagationMbCount
    overallTotalMbCount = overallValidTotalMbCount + overallInvalidTotalMbCount

    axOverallMbs = fig1.add_subplot(3, 4, 9)
    axOverallMbs.set_title("Overall macroblocks", color='0.4')
    axOverallMbs.pie([overallValidISliceMbCount, overallValidPSliceMbCount, overallMissingConcealedMbCount, overallMissingMbCount, overallErrorPropagationMbCount, overallUnknownMbCount], labels=['Valid (I)', 'Valid (P)', 'Missing (concealed)', 'Missing', 'Error propagation', 'Unknown'], colors=['mediumseagreen', 'lightgreen', 'salmon', 'firebrick', 'cadetblue', '0.3'], autopct='%1.1f%%', startangle=90)
    axOverallMbs.set_aspect('equal')

    axOverallErrorMbs = fig1.add_subplot(3, 4, 10)
    axOverallErrorMbs.set_title("Overall error macroblocks", color='0.4')
    axOverallErrorMbs.pie([overallMissingConcealedMbCount, overallMissingMbCount, overallErrorPropagationMbCount, overallUnknownMbCount], labels=['Missing (concealed)', 'Missing', 'Error propagation', 'Unknown'], colors=['salmon', 'firebrick', 'cadetblue', 'k'], autopct='%1.1f%%', startangle=90)
    axOverallErrorMbs.set_aspect('equal')


    # macroblocks by RSSI
    dataUnknownMb0 = np.subtract(data['macroblockStatus[0][0]'][1:], data['macroblockStatus[0][0]'][:-1])
    dataUnknownMb1 = np.subtract(data['macroblockStatus[0][1]'][1:], data['macroblockStatus[0][1]'][:-1])
    dataUnknownMb2 = np.subtract(data['macroblockStatus[0][2]'][1:], data['macroblockStatus[0][2]'][:-1])
    dataUnknownMb3 = np.subtract(data['macroblockStatus[0][3]'][1:], data['macroblockStatus[0][3]'][:-1])
    dataUnknownMb4 = np.subtract(data['macroblockStatus[0][4]'][1:], data['macroblockStatus[0][4]'][:-1])
    dataUnknownMb = np.add(dataUnknownMb0, np.add(dataUnknownMb1, np.add(dataUnknownMb2, np.add(dataUnknownMb3, dataUnknownMb4))))
    dataValidISliceMb0 = np.subtract(data['macroblockStatus[1][0]'][1:], data['macroblockStatus[1][0]'][:-1])
    dataValidISliceMb1 = np.subtract(data['macroblockStatus[1][1]'][1:], data['macroblockStatus[1][1]'][:-1])
    dataValidISliceMb2 = np.subtract(data['macroblockStatus[1][2]'][1:], data['macroblockStatus[1][2]'][:-1])
    dataValidISliceMb3 = np.subtract(data['macroblockStatus[1][3]'][1:], data['macroblockStatus[1][3]'][:-1])
    dataValidISliceMb4 = np.subtract(data['macroblockStatus[1][4]'][1:], data['macroblockStatus[1][4]'][:-1])
    dataValidISliceMb = np.add(dataValidISliceMb0, np.add(dataValidISliceMb1, np.add(dataValidISliceMb2, np.add(dataValidISliceMb3, dataValidISliceMb4))))
    dataValidPSliceMb0 = np.subtract(data['macroblockStatus[2][0]'][1:], data['macroblockStatus[2][0]'][:-1])
    dataValidPSliceMb1 = np.subtract(data['macroblockStatus[2][1]'][1:], data['macroblockStatus[2][1]'][:-1])
    dataValidPSliceMb2 = np.subtract(data['macroblockStatus[2][2]'][1:], data['macroblockStatus[2][2]'][:-1])
    dataValidPSliceMb3 = np.subtract(data['macroblockStatus[2][3]'][1:], data['macroblockStatus[2][3]'][:-1])
    dataValidPSliceMb4 = np.subtract(data['macroblockStatus[2][4]'][1:], data['macroblockStatus[2][4]'][:-1])
    dataValidPSliceMb = np.add(dataValidPSliceMb0, np.add(dataValidPSliceMb1, np.add(dataValidPSliceMb2, np.add(dataValidPSliceMb3, dataValidPSliceMb4))))
    dataMissingConcealedMb0 = np.subtract(data['macroblockStatus[3][0]'][1:], data['macroblockStatus[3][0]'][:-1])
    dataMissingConcealedMb1 = np.subtract(data['macroblockStatus[3][1]'][1:], data['macroblockStatus[3][1]'][:-1])
    dataMissingConcealedMb2 = np.subtract(data['macroblockStatus[3][2]'][1:], data['macroblockStatus[3][2]'][:-1])
    dataMissingConcealedMb3 = np.subtract(data['macroblockStatus[3][3]'][1:], data['macroblockStatus[3][3]'][:-1])
    dataMissingConcealedMb4 = np.subtract(data['macroblockStatus[3][4]'][1:], data['macroblockStatus[3][4]'][:-1])
    dataMissingConcealedMb = np.add(dataMissingConcealedMb0, np.add(dataMissingConcealedMb1, np.add(dataMissingConcealedMb2, np.add(dataMissingConcealedMb3, dataMissingConcealedMb4))))
    dataMissingMb0 = np.subtract(data['macroblockStatus[4][0]'][1:], data['macroblockStatus[4][0]'][:-1])
    dataMissingMb1 = np.subtract(data['macroblockStatus[4][1]'][1:], data['macroblockStatus[4][1]'][:-1])
    dataMissingMb2 = np.subtract(data['macroblockStatus[4][2]'][1:], data['macroblockStatus[4][2]'][:-1])
    dataMissingMb3 = np.subtract(data['macroblockStatus[4][3]'][1:], data['macroblockStatus[4][3]'][:-1])
    dataMissingMb4 = np.subtract(data['macroblockStatus[4][4]'][1:], data['macroblockStatus[4][4]'][:-1])
    dataMissingMb = np.add(dataMissingMb0, np.add(dataMissingMb1, np.add(dataMissingMb2, np.add(dataMissingMb3, dataMissingMb4))))
    dataErrorPropagationMb0 = np.subtract(data['macroblockStatus[5][0]'][1:], data['macroblockStatus[5][0]'][:-1])
    dataErrorPropagationMb1 = np.subtract(data['macroblockStatus[5][1]'][1:], data['macroblockStatus[5][1]'][:-1])
    dataErrorPropagationMb2 = np.subtract(data['macroblockStatus[5][2]'][1:], data['macroblockStatus[5][2]'][:-1])
    dataErrorPropagationMb3 = np.subtract(data['macroblockStatus[5][3]'][1:], data['macroblockStatus[5][3]'][:-1])
    dataErrorPropagationMb4 = np.subtract(data['macroblockStatus[5][4]'][1:], data['macroblockStatus[5][4]'][:-1])
    dataErrorPropagationMb = np.add(dataErrorPropagationMb0, np.add(dataErrorPropagationMb1, np.add(dataErrorPropagationMb2, np.add(dataErrorPropagationMb3, dataErrorPropagationMb4))))
    dataTotalMb = np.add(dataUnknownMb, np.add(dataValidISliceMb, np.add(dataValidPSliceMb, np.add(dataMissingConcealedMb, np.add(dataMissingMb, dataErrorPropagationMb)))))

    totalMbCountByRssi = np.empty(rssiCount)
    totalMbCountByRssi.fill(0)
    for i, val in enumerate(dataTotalMb):
        totalMbCountByRssi[dataRssi[i] - rssiMin] += float(val)

    unknownMbByRssi = np.empty(rssiCount)
    unknownMbByRssi.fill(0)
    for i, val in enumerate(dataUnknownMb):
        unknownMbByRssi[dataRssi[i] - rssiMin] += float(val)
    unknownMbByRssi = np.divide(unknownMbByRssi, totalMbCountByRssi)

    validISliceMbByRssi = np.empty(rssiCount)
    validISliceMbByRssi.fill(0)
    for i, val in enumerate(dataValidISliceMb):
        validISliceMbByRssi[dataRssi[i] - rssiMin] += float(val)
    validISliceMbByRssi = np.divide(validISliceMbByRssi, totalMbCountByRssi)

    validPSliceMbByRssi = np.empty(rssiCount)
    validPSliceMbByRssi.fill(0)
    for i, val in enumerate(dataValidPSliceMb):
        validPSliceMbByRssi[dataRssi[i] - rssiMin] += float(val)
    validPSliceMbByRssi = np.divide(validPSliceMbByRssi, totalMbCountByRssi)

    missingConcealedMbByRssi = np.empty(rssiCount)
    missingConcealedMbByRssi.fill(0)
    for i, val in enumerate(dataMissingConcealedMb):
        missingConcealedMbByRssi[dataRssi[i] - rssiMin] += float(val)
    missingConcealedMbByRssi = np.divide(missingConcealedMbByRssi, totalMbCountByRssi)

    missingMbByRssi = np.empty(rssiCount)
    missingMbByRssi.fill(0)
    for i, val in enumerate(dataMissingMb):
        missingMbByRssi[dataRssi[i] - rssiMin] += float(val)
    missingMbByRssi = np.divide(missingMbByRssi, totalMbCountByRssi)

    errorPropagationMbByRssi = np.empty(rssiCount)
    errorPropagationMbByRssi.fill(0)
    for i, val in enumerate(dataErrorPropagationMb):
        errorPropagationMbByRssi[dataRssi[i] - rssiMin] += float(val)
    errorPropagationMbByRssi = np.divide(errorPropagationMbByRssi, totalMbCountByRssi)

    totalMbCountByRssi = np.divide(totalMbCountByRssi, overallTotalMbCount)
    sum1 = np.add(unknownMbByRssi, missingMbByRssi)
    sum2 = np.add(sum1, missingConcealedMbByRssi)
    sum3 = np.add(sum2, errorPropagationMbByRssi)
    sum4 = np.add(sum3, validPSliceMbByRssi)

    axMacroblocksByRssi = fig1.add_subplot(3, 2, 6)
    axMacroblocksByRssi.set_title("Macroblocks by RSSI", color='0.4')
    width = 0.4
    axMacroblocksByRssi.bar(rssiVal - width, unknownMbByRssi * 100., width, color='0.3')
    axMacroblocksByRssi.bar(rssiVal - width, missingMbByRssi * 100., width, color='firebrick', bottom=unknownMbByRssi*100.)
    axMacroblocksByRssi.bar(rssiVal - width, missingConcealedMbByRssi * 100., width, color='salmon', bottom=sum1*100.)
    axMacroblocksByRssi.bar(rssiVal - width, errorPropagationMbByRssi * 100., width, color='cadetblue', bottom=sum2*100.)
    axMacroblocksByRssi.bar(rssiVal - width, validPSliceMbByRssi * 100., width, color='lightgreen', bottom=sum3*100.)
    axMacroblocksByRssi.bar(rssiVal - width, validISliceMbByRssi * 100., width, color='mediumseagreen', bottom=sum4*100.)
    axMacroblocksByRssi.bar(rssiVal, totalMbCountByRssi * 100., width, color='0.4')
    rssiMax2 = rssiMin
    rssiMin2 = 0
    for i, val in enumerate(totalMbCountByRssi):
        if val > 0:
            rssiMax2 = rssiVal[i]
            if rssiMin2 == 0:
                rssiMin2 = rssiVal[i]
    axMacroblocksByRssi.set_ylim(0, 100)
    axMacroblocksByRssi.set_xlim(rssiMin2 - 1, rssiMax2 + 1)
    axMacroblocksByRssi.set_xlabel('RSSI (dBm)')
    axMacroblocksByRssi.set_ylabel('(%)')


    plt.draw()
    if outFile != '':
        plt.savefig(outFile)
    else:
        plt.show()


def simpleVideoStats(inFile, outFile):
    fig1 = plt.figure()
    plt.subplots_adjust(left=0.03, right=0.97, bottom=0.03, top=0.92)
    f = open(inFile, 'r')
    firstLine = f.readline()
    f.close()
    if firstLine != '' and firstLine[0] == '#':
        title = firstLine[1:]
        title = title.strip()
    else:
        title = inFile
    plt.suptitle('Video stats (' + title + ')', fontsize=16, color='0.4')

    data = pd.read_csv(inFile, sep=' ', comment='#', skip_blank_lines=True)


    # for compatibility with old stats files
    if 'erroredSecondCount' in data.columns:
        lblEsr = 'erroredSecondCount'
        lblEsrZ0 = 'erroredSecondCountByZone[0]'
        lblEsrZ1 = 'erroredSecondCountByZone[1]'
        lblEsrZ2 = 'erroredSecondCountByZone[2]'
        lblEsrZ3 = 'erroredSecondCountByZone[3]'
        lblEsrZ4 = 'erroredSecondCountByZone[4]'
    else:
        lblEsr = 'errorSecondCount'
        lblEsrZ0 = 'errorSecondCountByZone[0]'
        lblEsrZ1 = 'errorSecondCountByZone[1]'
        lblEsrZ2 = 'errorSecondCountByZone[2]'
        lblEsrZ3 = 'errorSecondCountByZone[3]'
        lblEsrZ4 = 'errorSecondCountByZone[4]'
    if 'erroredOutputFrameCount' in data.columns:
        lblErroredOutputFrameCount = 'erroredOutputFrameCount'
    else:
        lblErroredOutputFrameCount = ''


    # overall frames
    overallTotalFrameCount = float(data['totalFrameCount'].iat[-1])
    if lblErroredOutputFrameCount != '':
        overallErroredOutputFrameCount = float(data[lblErroredOutputFrameCount].iat[-1])
    else:
        overallErroredOutputFrameCount = 0
    overallNoErrorOutputFrameCount = float(data['outputFrameCount'].iat[-1]) - overallErroredOutputFrameCount
    overallDiscardedFrameCount = float(data['discardedFrameCount'].iat[-1])
    overallMissedFrameCount = float(data['missedFrameCount'].iat[-1]) - overallDiscardedFrameCount
    overallTotalFrameCount2 = overallErroredOutputFrameCount + overallNoErrorOutputFrameCount + overallDiscardedFrameCount + overallMissedFrameCount
    if overallTotalFrameCount2 != overallTotalFrameCount:
        print "Warning - overall frame count missmatch: " + str(int(overallTotalFrameCount)) + " vs. " + str(int(overallTotalFrameCount2))

    axOverallFrames = fig1.add_subplot(2, 2, 1)
    axOverallFrames.set_title("Overall frames", color='0.4')
    axOverallFrames.pie([overallNoErrorOutputFrameCount, overallErroredOutputFrameCount, overallDiscardedFrameCount, overallMissedFrameCount], labels=['Output (no errors)', 'Output (with errors)', 'Discarded', 'Missed'], colors=['lightgreen', 'cadetblue', 'salmon', 'firebrick'], autopct='%1.1f%%', startangle=90)
    axOverallFrames.set_aspect('equal')


    # ESR by zone
    totalTime = float(data['timestamp'].iat[-1] - data['timestamp'].iat[0] + 1000000) / 1000000.
    zoneEsr = [float(data[lblEsrZ4].iat[-1]) / totalTime * 100., float(data[lblEsrZ3].iat[-1]) / totalTime * 100., float(data[lblEsrZ2].iat[-1]) / totalTime * 100., float(data[lblEsrZ1].iat[-1]) / totalTime * 100., float(data[lblEsrZ0].iat[-1]) / totalTime * 100.]
    zoneEsrYPos = np.arange(5)
    zoneEsrLabel = ('5', '4', '3', '2', '1')

    axZoneEsr = fig1.add_subplot(2, 2, 2)
    axZoneEsr.set_xlim(0, 100)
    axZoneEsr.set_title("ESR by zone", color='0.4')
    axZoneEsr.barh(zoneEsrYPos, zoneEsr, align='center', alpha=0.8, color='cadetblue')
    axZoneEsr.set_yticks(zoneEsrYPos)
    axZoneEsr.set_yticklabels(zoneEsrLabel)
    axZoneEsr.set_xlabel('ESR (%)')
    axZoneEsr.set_ylabel('Zone')


    # overall macroblocks
    overallUnknownMbCount = float(data['macroblockStatus[0][0]'].iat[-1] + data['macroblockStatus[0][1]'].iat[-1] + data['macroblockStatus[0][2]'].iat[-1] + data['macroblockStatus[0][3]'].iat[-1] + data['macroblockStatus[0][4]'].iat[-1])
    overallValidISliceMbCount = float(data['macroblockStatus[1][0]'].iat[-1] + data['macroblockStatus[1][1]'].iat[-1] + data['macroblockStatus[1][2]'].iat[-1] + data['macroblockStatus[1][3]'].iat[-1] + data['macroblockStatus[1][4]'].iat[-1])
    overallValidPSliceMbCount = float(data['macroblockStatus[2][0]'].iat[-1] + data['macroblockStatus[2][1]'].iat[-1] + data['macroblockStatus[2][2]'].iat[-1] + data['macroblockStatus[2][3]'].iat[-1] + data['macroblockStatus[2][4]'].iat[-1])
    overallMissingConcealedMbCount = float(data['macroblockStatus[3][0]'].iat[-1] + data['macroblockStatus[3][1]'].iat[-1] + data['macroblockStatus[3][2]'].iat[-1] + data['macroblockStatus[3][3]'].iat[-1] + data['macroblockStatus[3][4]'].iat[-1])
    overallMissingMbCount = float(data['macroblockStatus[4][0]'].iat[-1] + data['macroblockStatus[4][1]'].iat[-1] + data['macroblockStatus[4][2]'].iat[-1] + data['macroblockStatus[4][3]'].iat[-1] + data['macroblockStatus[4][4]'].iat[-1])
    overallErrorPropagationMbCount = float(data['macroblockStatus[5][0]'].iat[-1] + data['macroblockStatus[5][1]'].iat[-1] + data['macroblockStatus[5][2]'].iat[-1] + data['macroblockStatus[5][3]'].iat[-1] + data['macroblockStatus[5][4]'].iat[-1])
    overallValidTotalMbCount = overallValidISliceMbCount + overallValidPSliceMbCount
    overallInvalidTotalMbCount = overallUnknownMbCount + overallMissingConcealedMbCount + overallMissingMbCount + overallErrorPropagationMbCount
    overallTotalMbCount = overallValidTotalMbCount + overallInvalidTotalMbCount

    axOverallMbs = fig1.add_subplot(2, 2, 3)
    axOverallMbs.set_title("Overall macroblocks", color='0.4')
    axOverallMbs.pie([overallValidISliceMbCount, overallValidPSliceMbCount, overallMissingConcealedMbCount, overallMissingMbCount, overallErrorPropagationMbCount, overallUnknownMbCount], labels=['Valid (I)', 'Valid (P)', 'Missing (concealed)', 'Missing', 'Error propagation', 'Unknown'], colors=['mediumseagreen', 'lightgreen', 'salmon', 'firebrick', 'cadetblue', '0.3'], autopct='%1.1f%%', startangle=90)
    axOverallMbs.set_aspect('equal')

    axOverallErrorMbs = fig1.add_subplot(2, 2, 4)
    axOverallErrorMbs.set_title("Overall error macroblocks", color='0.4')
    axOverallErrorMbs.pie([overallMissingConcealedMbCount, overallMissingMbCount, overallErrorPropagationMbCount, overallUnknownMbCount], labels=['Missing (concealed)', 'Missing', 'Error propagation', 'Unknown'], colors=['salmon', 'firebrick', 'cadetblue', 'k'], autopct='%1.1f%%', startangle=90)
    axOverallErrorMbs.set_aspect('equal')


    plt.draw()
    if outFile != '':
        plt.savefig(outFile)
    else:
        plt.show()


def usage():
    print "Usage:"
    print "    " + sys.argv[0] + " -i | --infile <input_file>"
    print "Options:"
    print "    -o | --outfile <output_file>    Output to file instead of GUI"
    print "    -h | --help                     Print this message"
    print "    -s | --simple                   Simple output (less graphs)"


def main(argv):
    inFile = ''
    outFile = ''
    simple = False

    # command-line arguments
    try:
        opts, args = getopt.getopt(argv,"hsi:o:",["help", "infile=", "outfile=", "simple"])
    except getopt.GetoptError:
        usage()
        sys.exit(2)

    for opt, arg in opts:
        if opt in ("-h", "--help"):
            usage()
            sys.exit()
        elif opt in ("-i", "--infile"):
            inFile = arg
        elif opt in ("-o", "--outfile"):
            outFile = arg
        elif opt in ("-s", "--simple"):
            simple = True

    if inFile == '':
        usage()
        sys.exit(2)

    if inFile != '':
        if simple:
            simpleVideoStats(inFile, outFile)
        else:
            videoStats(inFile, outFile)


if __name__ == '__main__':
    main(sys.argv[1:])

