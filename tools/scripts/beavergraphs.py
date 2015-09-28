#!/usr/bin/python

# @file beavergraphs.py
# @brief FrameInfo and RTP monitoring graphs script
# @date 09/28/2015
# @author aurelien.barre@parrot.com


import sys, getopt, time
from numpy import *
import matplotlib.pyplot as plt
from beaver_frameinfo import *
from beaver_rtpsender import *
from beaver_rtpreader import *


frameInfoFile = ''
senderFile = ''
readerFile = ''

fig1 = plt.figure()


# FrameInfo graphs
def createFrameInfoGraphs():
    global axDistances
    global axAngles
    global axSpeeds
    global axFrameSize
    global axFramePsnrY
    global axWifiMcsRate
    global axWifiRssi
    global axWifiErrors
    global axVideoRates
    global axStreamingBitrates
    global axStreamingPacketRates
    global axFrameLatency
    global axFrameTimeDelta
    global axFramesMissed
    global axCommands
    global lineYaw
    global linePitch
    global lineRoll
    global lineDistanceFromHome
    global lineRelativeHeight
    global lineXSpeed
    global lineYSpeed
    global lineZSpeed
    global lineWifiRssi
    global lineWifiMcsRate
    global lineWifiTxFailEvents
    global lineVideoStreamingTargetBitrate
    global lineVideoStreamingFrameSize
    global lineVideoStreamingFramePsnrY
    global lineStreamingSrcSentBitrate
    global lineStreamingSrcDroppedBitrate
    global lineStreamingSrcSentPacketRate
    global lineStreamingSrcDroppedNaluRate
    global lineEstimatedLostFrames
    global lineBeaverFirstNaluInputLatency
    global lineBeaverAuOutputLatency
    global lineFrameAcquisitionTimeDelta
    global lineFrameBeaverOutputTimeDelta
    global lineLastCommandTimeDelta
    global lineCommandsMaxTimeDeltaOnLastSec

    plt.subplots_adjust(left=0.03, right=0.97, bottom=0.03, top=0.97)
    axFrameSize = fig1.add_subplot(4, 4, 4)
    axFramePsnrY = fig1.add_subplot(4, 4, 3, sharex=axFrameSize)
    axDistances = fig1.add_subplot(4, 4, 1, sharex=axFrameSize)
    axAngles = fig1.add_subplot(4, 4, 5, sharex=axFrameSize)
    axSpeeds = fig1.add_subplot(4, 4, 9, sharex=axFrameSize)
    axWifiMcsRate = fig1.add_subplot(4, 4, 6, sharex=axFrameSize)
    axWifiRssi = fig1.add_subplot(4, 4, 2, sharex=axFrameSize)
    axWifiErrors = fig1.add_subplot(4, 4, 14, sharex=axFrameSize)
    axVideoRates = fig1.add_subplot(4, 4, 10, sharex=axFrameSize)
    axStreamingBitrates = fig1.add_subplot(4, 4, 11, sharex=axFrameSize)
    axStreamingPacketRates = fig1.add_subplot(4, 4, 15, sharex=axFrameSize)
    axFrameTimeDelta = fig1.add_subplot(4, 4, 8, sharex=axFrameSize)
    axFrameLatency = fig1.add_subplot(4, 4, 12, sharex=axFrameSize)
    axFramesMissed = fig1.add_subplot(4, 4, 16, sharex=axFrameSize)
    axCommands = fig1.add_subplot(4, 4, 13, sharex=axFrameSize)

    axFrameSize.set_title("Video frame size (bytes)", color='0.4')
    axFrameSize.set_xlim(0, 10)
    axFrameSize.set_ylim(0, 1000)
    axFramePsnrY.set_title("Video frame PSNR(Y) (dB)", color='0.4')
    axFramePsnrY.set_ylim(10, 48)
    axDistances.set_title("Distance and altitude (m)", color='0.4')
    axDistances.set_ylim(0, 100)
    axSpeeds.set_title("Speeds (m/s)", color='0.4')
    axSpeeds.set_ylim(-15, 15)
    axAngles.set_title("Drone angles (rad)", color='0.4')
    axAngles.set_ylim(-3.2, 3.2)
    axWifiMcsRate.set_title("Wifi MCS rate (Mbit/s)", color='0.4')
    axWifiMcsRate.set_ylim(0, 80)
    axWifiRssi.set_title("Controller RSSI (dBm)", color='0.4')
    axWifiRssi.set_ylim(-100, 0)
    axVideoRates.set_title("Video target bitrate (Mbit/s)", color='0.4')
    axVideoRates.set_ylim(0, 1.0)
    axStreamingBitrates.set_title("Streaming sent/dropped bitrates (Mbit/s)", color='0.4')
    axStreamingBitrates.set_ylim(0, 1.0)
    axStreamingPacketRates.set_title("Streaming packets sent / NALU dropped (#/s)", color='0.4')
    axStreamingPacketRates.set_ylim(0, 10)
    axWifiErrors.set_title("Wifi error rate (#/s)", color='0.4')
    axWifiErrors.set_ylim(0, 2)
    axFrameLatency.set_title("Video frame latency (ms)", color='0.4')
    axFrameLatency.set_ylim(0, 200)
    axFrameTimeDelta.set_title("Video frame time deltas (ms)", color='0.4')
    axFrameTimeDelta.set_ylim(0, 0.02)
    axFramesMissed.set_title("Video frames missed", color='0.4')
    axFramesMissed.set_ylim(0, 30)
    axCommands.set_title("Piloting commands time delta (ms)", color='0.4')
    axCommands.set_ylim(0, 50)

    lineYaw, = axAngles.plot([], [], c='g')
    linePitch, = axAngles.plot([], [], c='r')
    lineRoll, = axAngles.plot([], [], c='b')
    lineXSpeed, = axSpeeds.plot([], [], c='b')
    lineYSpeed, = axSpeeds.plot([], [], c='g')
    lineZSpeed, = axSpeeds.plot([], [], c='r')
    lineDistanceFromHome, = axDistances.plot([], [], c='k')
    lineRelativeHeight, = axDistances.plot([], [], c='r')
    lineWifiRssi, = axWifiRssi.plot([], [], c='k')
    lineWifiMcsRate, = axWifiMcsRate.plot([], [], c='k')
    lineWifiTxFailEvents, = axWifiErrors.plot([], [], c='r')
    lineVideoStreamingTargetBitrate, = axVideoRates.plot([], [], c='g')
    lineVideoStreamingFrameSize, = axFrameSize.plot([], [], c='k')
    lineVideoStreamingFramePsnrY, = axFramePsnrY.plot([], [], c='k')
    lineStreamingSrcSentBitrate, = axStreamingBitrates.plot([], [], c='k')
    lineStreamingSrcDroppedBitrate, = axStreamingBitrates.plot([], [], c='r')
    lineStreamingSrcSentPacketRate, = axStreamingPacketRates.plot([], [], c='k')
    lineStreamingSrcDroppedNaluRate, = axStreamingPacketRates.plot([], [], c='r')
    lineBeaverFirstNaluInputLatency, = axFrameLatency.plot([], [], c='b')
    lineBeaverAuOutputLatency, = axFrameLatency.plot([], [], c='k')
    lineEstimatedLostFrames, = axFramesMissed.plot([], [], c='r')
    lineFrameBeaverOutputTimeDelta, = axFrameTimeDelta.plot([], [], c='r')
    lineFrameAcquisitionTimeDelta, = axFrameTimeDelta.plot([], [], c='k')
    lineLastCommandTimeDelta, = axCommands.plot([], [], c='k')
    lineCommandsMaxTimeDeltaOnLastSec, = axCommands.plot([], [], c='r')


def updateFrameInfoGraphs(frameInfoFile):
    global axDistances
    global axAngles
    global axSpeeds
    global axFrameSize
    global axFramePsnrY
    global axWifiMcsRate
    global axWifiRssi
    global axWifiErrors
    global axVideoRates
    global axStreamingBitrates
    global axStreamingPacketRates
    global axFrameLatency
    global axFrameTimeDelta
    global axFramesMissed
    global axCommands
    global lineYaw
    global linePitch
    global lineRoll
    global lineDistanceFromHome
    global lineRelativeHeight
    global lineXSpeed
    global lineYSpeed
    global lineZSpeed
    global lineWifiRssi
    global lineWifiMcsRate
    global lineWifiTxFailEvents
    global lineVideoStreamingTargetBitrate
    global lineVideoStreamingFrameSize
    global lineVideoStreamingFramePsnrY
    global lineStreamingSrcSentBitrate
    global lineStreamingSrcDroppedBitrate
    global lineStreamingSrcSentPacketRate
    global lineStreamingSrcDroppedNaluRate
    global lineEstimatedLostFrames
    global lineBeaverFirstNaluInputLatency
    global lineBeaverAuOutputLatency
    global lineFrameAcquisitionTimeDelta
    global lineFrameBeaverOutputTimeDelta
    global lineLastCommandTimeDelta
    global lineCommandsMaxTimeDeltaOnLastSec

    csvFrameInfoFile = open(frameInfoFile, 'r')
    csvFrameInfoFile.readline()
    frameInfoMonitoring = FrameInfoMonitoring(csvFrameInfoFile)
    csvFrameInfoFile.close()
    wifiTxFailEvents = subtract(frameInfoMonitoring.getWifiTxFailEventCount()[1:], frameInfoMonitoring.getWifiTxFailEventCount()[:-1])
    frameAcquisitionTimeDelta = divide(subtract(frameInfoMonitoring.getAcquisitionTs()[1:], frameInfoMonitoring.getAcquisitionTs()[:-1]), 1000.)
    frameBeaverOutputTimeDelta = divide(subtract(frameInfoMonitoring.getBeaverAuOutputTime()[1:], frameInfoMonitoring.getBeaverAuOutputTime()[:-1]), 1000.)
    beaverFirstNaluInputLatency = divide(subtract(frameInfoMonitoring.getBeaverFirstNaluInputTime(), frameInfoMonitoring.getAcquisitionTsShifted()), 1000.)
    beaverAuOutputLatency = divide(subtract(frameInfoMonitoring.getBeaverAuOutputTime(), frameInfoMonitoring.getAcquisitionTsShifted()), 1000.)
    streamingSrcSentBitrate = divide(frameInfoMonitoring.getStreamingSrcBytesSent(), 1000000. / 8.)
    streamingSrcDroppedBitrate = divide(frameInfoMonitoring.getStreamingSrcBytesDropped(), 1000000. / 8.)
    lastCommandTimeDelta = divide(frameInfoMonitoring.getLastCommandTimeDelta(), 1000.)
    commandsMaxTimeDeltaOnLastSec = divide(frameInfoMonitoring.getCommandsMaxTimeDeltaOnLastSec(), 1000.)

    frameTimeMax = amax(frameInfoMonitoring.getFrameTime())
    xmin, xmax = axFrameSize.get_xlim()
    if frameTimeMax > xmax:
        axFrameSize.set_xlim(xmin, frameTimeMax * 1.2)

    videoStreamingTargetBitrateMax = amax(frameInfoMonitoring.getVideoStreamingTargetBitrate())
    ymin, ymax = axVideoRates.get_ylim()
    if videoStreamingTargetBitrateMax > ymax:
        axVideoRates.set_ylim(ymin, videoStreamingTargetBitrateMax * 1.2)

    lastCommandTimeDeltaMax = amax(lastCommandTimeDelta)
    commandsMaxTimeDeltaOnLastSecMax = amax(commandsMaxTimeDeltaOnLastSec)
    ymin, ymax = axCommands.get_ylim()
    if lastCommandTimeDeltaMax > ymax:
        ymax = lastCommandTimeDeltaMax * 1.2
    if commandsMaxTimeDeltaOnLastSecMax > ymax:
        ymax = commandsMaxTimeDeltaOnLastSecMax * 1.2
    axCommands.set_ylim(ymin, ymax)

    streamingSrcSentBitrateMax = amax(streamingSrcSentBitrate)
    streamingSrcDroppedBitrateMax = amax(streamingSrcDroppedBitrate)
    ymin, ymax = axStreamingBitrates.get_ylim()
    if streamingSrcSentBitrateMax > ymax:
        ymax = streamingSrcSentBitrateMax * 1.2
    if streamingSrcDroppedBitrateMax > ymax:
        ymax = streamingSrcDroppedBitrateMax * 1.2
    axStreamingBitrates.set_ylim(ymin, ymax)

    streamingSrcPacketsSentMax = amax(frameInfoMonitoring.getStreamingSrcPacketsSent())
    streamingSrcNaluDroppedMax = amax(frameInfoMonitoring.getStreamingSrcNaluDropped())
    ymin, ymax = axStreamingPacketRates.get_ylim()
    if streamingSrcPacketsSentMax > ymax:
        ymax = streamingSrcPacketsSentMax * 1.2
    if streamingSrcNaluDroppedMax > ymax:
        ymax = streamingSrcNaluDroppedMax * 1.2
    axStreamingPacketRates.set_ylim(ymin, ymax)

    wifiMcsRateMax = amax(frameInfoMonitoring.getWifiMcsRate())
    ymin, ymax = axWifiMcsRate.get_ylim()
    if wifiMcsRateMax > ymax:
        axWifiMcsRate.set_ylim(ymin, wifiMcsRateMax * 1.2)

    videoStreamingPrevFrameSizeMax = amax(frameInfoMonitoring.getVideoStreamingPrevFrameSize())
    ymin, ymax = axFrameSize.get_ylim()
    if videoStreamingPrevFrameSizeMax > ymax:
        axFrameSize.set_ylim(ymin, videoStreamingPrevFrameSizeMax * 1.2)

    distanceFromHomeMax = amax(frameInfoMonitoring.getDistanceFromHome())
    relativeHeightMax = amax(frameInfoMonitoring.getRelativeHeight())
    ymin, ymax = axDistances.get_ylim()
    if distanceFromHomeMax > ymax:
        ymax = distanceFromHomeMax * 1.2
    if relativeHeightMax > ymax:
        ymax = relativeHeightMax * 1.2
    axDistances.set_ylim(ymin, ymax)

    frameAcquisitionTimeDeltaMax = amax(frameAcquisitionTimeDelta)
    frameBeaverOutputTimeDeltaMax = amax(frameBeaverOutputTimeDelta)
    ymin, ymax = axFrameTimeDelta.get_ylim()
    if frameAcquisitionTimeDeltaMax > ymax:
        ymax = frameAcquisitionTimeDeltaMax * 1.2
    if frameBeaverOutputTimeDeltaMax > ymax:
        ymax = frameBeaverOutputTimeDeltaMax * 1.2
    axFrameTimeDelta.set_ylim(ymin, ymax)

    beaverFirstNaluInputLatencyMax = amax(beaverFirstNaluInputLatency)
    beaverAuOutputLatencyMax = amax(beaverAuOutputLatency)
    ymin, ymax = axFrameLatency.get_ylim()
    if beaverFirstNaluInputLatencyMax > ymax:
        ymax = beaverFirstNaluInputLatencyMax * 1.2
    if beaverAuOutputLatencyMax > ymax:
        ymax = beaverAuOutputLatencyMax * 1.2
    axFrameLatency.set_ylim(ymin, ymax)

    wifiTxFailEventsMax = amax(wifiTxFailEvents)
    ymin, ymax = axWifiErrors.get_ylim()
    if wifiTxFailEventsMax > ymax:
        axWifiErrors.set_ylim(ymin, wifiTxFailEventsMax * 1.2)

    axFrameSize.figure.canvas.draw()
    axFramePsnrY.figure.canvas.draw()
    axDistances.figure.canvas.draw()
    axAngles.figure.canvas.draw()
    axSpeeds.figure.canvas.draw()
    axWifiMcsRate.figure.canvas.draw()
    axWifiRssi.figure.canvas.draw()
    axVideoRates.figure.canvas.draw()
    axStreamingBitrates.figure.canvas.draw()
    axStreamingPacketRates.figure.canvas.draw()
    axWifiErrors.figure.canvas.draw()
    axFrameLatency.figure.canvas.draw()
    axFrameTimeDelta.figure.canvas.draw()
    axFramesMissed.figure.canvas.draw()
    axCommands.figure.canvas.draw()
    lineYaw.set_data(frameInfoMonitoring.getFrameTime(), frameInfoMonitoring.getYaw())
    linePitch.set_data(frameInfoMonitoring.getFrameTime(), frameInfoMonitoring.getPitch())
    lineRoll.set_data(frameInfoMonitoring.getFrameTime(), frameInfoMonitoring.getRoll())
    lineXSpeed.set_data(frameInfoMonitoring.getFrameTime(), frameInfoMonitoring.getXSpeed())
    lineYSpeed.set_data(frameInfoMonitoring.getFrameTime(), frameInfoMonitoring.getYSpeed())
    lineZSpeed.set_data(frameInfoMonitoring.getFrameTime(), frameInfoMonitoring.getZSpeed())
    lineDistanceFromHome.set_data(frameInfoMonitoring.getFrameTime(), frameInfoMonitoring.getDistanceFromHome())
    lineRelativeHeight.set_data(frameInfoMonitoring.getFrameTime(), frameInfoMonitoring.getRelativeHeight())
    lineWifiRssi.set_data(frameInfoMonitoring.getFrameTime(), frameInfoMonitoring.getWifiRssi())
    lineWifiMcsRate.set_data(frameInfoMonitoring.getFrameTime(), frameInfoMonitoring.getWifiMcsRate())
    lineWifiTxFailEvents.set_data(frameInfoMonitoring.getFrameTime()[1:], wifiTxFailEvents)
    lineVideoStreamingTargetBitrate.set_data(frameInfoMonitoring.getFrameTime(), frameInfoMonitoring.getVideoStreamingTargetBitrate())
    lineVideoStreamingFrameSize.set_data(frameInfoMonitoring.getFrameTime()[1:], frameInfoMonitoring.getVideoStreamingPrevFrameSize()[:-1])
    lineVideoStreamingFramePsnrY.set_data(frameInfoMonitoring.getFrameTime()[1:], frameInfoMonitoring.getVideoStreamingPrevFramePsnrY()[:-1])
    lineStreamingSrcSentBitrate.set_data(frameInfoMonitoring.getFrameTime(), streamingSrcSentBitrate)
    lineStreamingSrcDroppedBitrate.set_data(frameInfoMonitoring.getFrameTime(), streamingSrcDroppedBitrate)
    lineStreamingSrcSentPacketRate.set_data(frameInfoMonitoring.getFrameTime(), frameInfoMonitoring.getStreamingSrcPacketsSent())
    lineStreamingSrcDroppedNaluRate.set_data(frameInfoMonitoring.getFrameTime(), frameInfoMonitoring.getStreamingSrcNaluDropped())
    lineEstimatedLostFrames.set_data(frameInfoMonitoring.getFrameTime(), frameInfoMonitoring.getEstimatedLostFrames())
    lineBeaverFirstNaluInputLatency.set_data(frameInfoMonitoring.getFrameTime(), beaverFirstNaluInputLatency)
    lineBeaverAuOutputLatency.set_data(frameInfoMonitoring.getFrameTime(), beaverAuOutputLatency)
    lineFrameAcquisitionTimeDelta.set_data(frameInfoMonitoring.getFrameTime()[1:], frameAcquisitionTimeDelta)
    lineFrameBeaverOutputTimeDelta.set_data(frameInfoMonitoring.getFrameTime()[1:], frameBeaverOutputTimeDelta)
    lineLastCommandTimeDelta.set_data(frameInfoMonitoring.getFrameTime(), lastCommandTimeDelta)
    lineCommandsMaxTimeDeltaOnLastSec.set_data(frameInfoMonitoring.getFrameTime(), commandsMaxTimeDeltaOnLastSec)
    plt.draw()


def onClickFrameInfo(event, frameInfoFile):
    updateFrameInfoGraphs(frameInfoFile)


# Sender graphs
def createSenderGraphs():
    global axSenderPacketSendTime
    global axSenderPacketDropTime
    global axSenderPacketSendSize
    global axSenderPacketDropSize
    global lineSenderAcqToNetworkInputSendTime
    global lineSenderAcqToNetworkInputDropTime
    global lineSenderAcqToNetworkSendTime
    global lineSenderAcqToNetworkDropTime
    global lineSenderNetworkSendTime
    global lineSenderNetworkDropTime
    global lineSenderBytesSent
    global lineSenderBytesDropped

    plt.subplots_adjust(left=0.03, right=0.97, bottom=0.03, top=0.97)
    axSenderPacketSendTime = fig1.add_subplot(2, 2, 1)
    axSenderPacketDropTime = fig1.add_subplot(2, 2, 3, sharex=axSenderPacketSendTime)
    axSenderPacketSendSize = fig1.add_subplot(2, 2, 2, sharex=axSenderPacketSendTime)
    axSenderPacketDropSize = fig1.add_subplot(2, 2, 4, sharex=axSenderPacketSendTime)
    axSenderPacketSendTime.set_title("Packet sent time (ms)", color='0.4')
    axSenderPacketSendTime.set_xlim(0, 10)
    axSenderPacketSendTime.set_ylim(0, 100)
    axSenderPacketDropTime.set_title("Packet dropped time (ms)", color='0.4')
    axSenderPacketDropTime.set_ylim(0, 100)
    axSenderPacketSendSize.set_title("Packet sent size (bytes)", color='0.4')
    axSenderPacketSendSize.set_ylim(0, 1500)
    axSenderPacketDropSize.set_title("Packet dropped size (bytes)", color='0.4')
    axSenderPacketDropSize.set_ylim(0, 1500)

    lineSenderAcqToNetworkSendTime, = axSenderPacketSendTime.plot([], [], c='k')
    lineSenderAcqToNetworkDropTime, = axSenderPacketDropTime.plot([], [], c='k')
    lineSenderAcqToNetworkInputSendTime, = axSenderPacketSendTime.plot([], [], c='g')
    lineSenderAcqToNetworkInputDropTime, = axSenderPacketDropTime.plot([], [], c='g')
    lineSenderNetworkSendTime, = axSenderPacketSendTime.plot([], [], c='b')
    lineSenderNetworkDropTime, = axSenderPacketDropTime.plot([], [], c='b')
    lineSenderBytesSent, = axSenderPacketSendSize.plot([], [], c='k')
    lineSenderBytesDropped, = axSenderPacketDropSize.plot([], [], c='r')


def updateSenderGraphs(senderFile):
    global axSenderPacketSendTime
    global axSenderPacketDropTime
    global axSenderPacketSendSize
    global axSenderPacketDropSize
    global lineSenderAcqToNetworkInputSendTime
    global lineSenderAcqToNetworkInputDropTime
    global lineSenderAcqToNetworkSendTime
    global lineSenderAcqToNetworkDropTime
    global lineSenderNetworkSendTime
    global lineSenderNetworkDropTime
    global lineSenderBytesSent
    global lineSenderBytesDropped

    csvSenderFile = open(senderFile, 'r')
    csvSenderFile.readline()
    senderStats = RtpSenderStat(csvSenderFile)
    csvSenderFile.close()

    packetTimeMax = amax(senderStats.getPacketTime())
    xmin, xmax = axSenderPacketSendTime.get_xlim()
    if packetTimeMax > xmax:
        axSenderPacketSendTime.set_xlim(xmin, packetTimeMax * 1.2)

    acqToNetworkSendTimeMax = amax(senderStats.getAcqToNetworkSendTime())
    networkSendTimeMax = amax(senderStats.getNetworkSendTime())
    ymin, ymax = axSenderPacketSendTime.get_ylim()
    if acqToNetworkSendTimeMax > ymax:
        ymax = acqToNetworkSendTimeMax * 1.2
    if networkSendTimeMax > ymax:
        ymax = networkSendTimeMax * 1.2
    axSenderPacketSendTime.set_ylim(ymin, ymax)

    acqToNetworkDropTimeMax = amax(senderStats.getAcqToNetworkDropTime())
    networkDropTimeMax = amax(senderStats.getNetworkDropTime())
    ymin, ymax = axSenderPacketDropTime.get_ylim()
    if acqToNetworkDropTimeMax > ymax:
        ymax = acqToNetworkDropTimeMax * 1.2
    if networkDropTimeMax > ymax:
        ymax = networkDropTimeMax * 1.2
    axSenderPacketDropTime.set_ylim(ymin, ymax)

    bytesSentMax = amax(senderStats.getBytesSent())
    ymin, ymax = axSenderPacketSendSize.get_ylim()
    if bytesSentMax > ymax:
        axSenderPacketSendSize.set_ylim(ymin, bytesSentMax * 1.2)

    bytesDroppedMax = amax(senderStats.getBytesDropped())
    ymin, ymax = axSenderPacketDropSize.get_ylim()
    if bytesDroppedMax > ymax:
        axSenderPacketDropSize.set_ylim(ymin, bytesDroppedMax * 1.2)

    axSenderPacketSendTime.figure.canvas.draw()
    axSenderPacketDropTime.figure.canvas.draw()
    axSenderPacketSendSize.figure.canvas.draw()
    axSenderPacketDropSize.figure.canvas.draw()
    lineSenderAcqToNetworkInputSendTime.set_data(senderStats.getPacketTime(), subtract(senderStats.getAcqToNetworkSendTime(), senderStats.getNetworkSendTime()))
    lineSenderAcqToNetworkInputDropTime.set_data(senderStats.getPacketTime(), subtract(senderStats.getAcqToNetworkDropTime(), senderStats.getNetworkDropTime()))
    lineSenderAcqToNetworkSendTime.set_data(senderStats.getPacketTime(), senderStats.getAcqToNetworkSendTime())
    lineSenderAcqToNetworkDropTime.set_data(senderStats.getPacketTime(), senderStats.getAcqToNetworkDropTime())
    lineSenderNetworkSendTime.set_data(senderStats.getPacketTime(), senderStats.getNetworkSendTime())
    lineSenderNetworkDropTime.set_data(senderStats.getPacketTime(), senderStats.getNetworkDropTime())
    lineSenderBytesSent.set_data(senderStats.getPacketTime(), senderStats.getBytesSent())
    lineSenderBytesDropped.set_data(senderStats.getPacketTime(), senderStats.getBytesDropped())
    plt.draw()


def onClickSender(event, senderFile):
    updateSenderGraphs(senderFile)


# Reader graphs
def createReaderGraphs():
    global axReaderPacketTime
    global axReaderPacketSize
    global axReaderMissingPackets
    global lineReaderNetworkRecvTime
    global lineReaderBytes
    global lineReaderMissingPacketsBefore

    plt.subplots_adjust(left=0.03, right=0.97, bottom=0.03, top=0.97)
    axReaderPacketTime = fig1.add_subplot(2, 2, 1)
    axReaderPacketSize = fig1.add_subplot(2, 2, 2, sharex=axReaderPacketTime)
    axReaderMissingPackets = fig1.add_subplot(2, 2, 3, sharex=axReaderPacketTime)
    axReaderPacketTime.set_title("Packet time (ms)", color='0.4')
    axReaderPacketTime.set_xlim(0, 10)
    axReaderPacketTime.set_ylim(0, 100)
    axReaderPacketSize.set_title("Packet size (bytes)", color='0.4')
    axReaderPacketSize.set_ylim(0, 1500)
    axReaderMissingPackets.set_title("Missing packets", color='0.4')
    axReaderMissingPackets.set_ylim(0, 10)

    lineReaderNetworkRecvTime, = axReaderPacketTime.plot([], [], c='k')
    lineReaderBytes, = axReaderPacketSize.plot([], [], c='k')
    lineReaderMissingPacketsBefore, = axReaderMissingPackets.plot([], [], c='r')


def updateReaderGraphs(readerFile):
    global axReaderPacketTime
    global axReaderPacketSize
    global axReaderMissingPackets
    global lineReaderNetworkRecvTime
    global lineReaderBytes
    global lineReaderMissingPacketsBefore

    csvReaderFile = open(readerFile, 'r')
    csvReaderFile.readline()
    readerStats = RtpReaderStat(csvReaderFile)
    csvReaderFile.close()

    packetTimeMax = amax(readerStats.getPacketTime())
    xmin, xmax = axReaderPacketTime.get_xlim()
    if packetTimeMax > xmax:
        axReaderPacketTime.set_xlim(xmin, packetTimeMax * 1.2)

    networkRecvTimeMax = amax(readerStats.getNetworkRecvTime())
    ymin, ymax = axReaderPacketTime.get_ylim()
    if networkRecvTimeMax > ymax:
        axReaderPacketTime.set_ylim(ymin, networkRecvTimeMax * 1.2)

    bytesMax = amax(readerStats.getBytes())
    ymin, ymax = axReaderPacketSize.get_ylim()
    if bytesMax > ymax:
        axReaderPacketSize.set_ylim(ymin, bytesMax * 1.2)

    missingPacketsBeforeMax = amax(readerStats.getMissingPacketsBefore())
    ymin, ymax = axReaderMissingPackets.get_ylim()
    if missingPacketsBeforeMax > ymax:
        axReaderMissingPackets.set_ylim(ymin, missingPacketsBeforeMax * 1.2)

    axReaderPacketTime.figure.canvas.draw()
    axReaderPacketSize.figure.canvas.draw()
    axReaderMissingPackets.figure.canvas.draw()
    lineReaderNetworkRecvTime.set_data(readerStats.getPacketTime(), readerStats.getNetworkRecvTime())
    lineReaderBytes.set_data(readerStats.getPacketTime(), readerStats.getBytes())
    lineReaderMissingPacketsBefore.set_data(readerStats.getPacketTime(), readerStats.getMissingPacketsBefore())
    plt.draw()


def onClickReader(event, readerFile):
    updateReaderGraphs(readerFile)


# Sender+reader graphs
def createSenderReaderGraphs():
    global axPacketSendRecvTime
    global axPacketSendRecvJitter
    global axMissingPackets
    #global axPacketSize
    global lineSenderAcqToNetworkTime
    global lineSenderAcqToNetworkInputTime
    global lineSenderNetworkSendTime
    global lineReaderRecvTime
    global lineReaderRecvJitter
    global lineSenderSendJitter
    global lineSenderDroppedPacketsBefore
    global lineReaderMissingPacketsBefore
    #global linePacketSize

    plt.subplots_adjust(left=0.03, right=0.97, bottom=0.03, top=0.97)
    axPacketSendRecvTime = fig1.add_subplot(3, 1, 1)
    axPacketSendRecvJitter = fig1.add_subplot(3, 1, 2, sharex=axPacketSendRecvTime)
    axMissingPackets = fig1.add_subplot(3, 1, 3, sharex=axPacketSendRecvTime)
    #axPacketSize = fig1.add_subplot(2, 2, 2, sharex=axPacketSendRecvTime)
    axPacketSendRecvTime.set_title("Packet times (ms)", color='0.4')
    axPacketSendRecvTime.set_xlim(0, 10)
    axPacketSendRecvTime.set_ylim(0, 100)
    axPacketSendRecvJitter.set_title("Packet jitter (ms)", color='0.4')
    axPacketSendRecvJitter.set_ylim(-10, 10)
    axMissingPackets.set_title("Missing packets", color='0.4')
    axMissingPackets.set_ylim(0, 100)
    #axPacketSize.set_title("Packet size (bytes)", color='0.4')
    #axPacketSize.set_ylim(0, 1500)

    lineReaderRecvTime, = axPacketSendRecvTime.plot([], [], c='r')
    lineSenderAcqToNetworkTime, = axPacketSendRecvTime.plot([], [], c='k')
    lineSenderAcqToNetworkInputTime, = axPacketSendRecvTime.plot([], [], c='g')
    lineSenderNetworkSendTime, = axPacketSendRecvTime.plot([], [], c='b')
    lineReaderRecvJitter, = axPacketSendRecvJitter.plot([], [], c='r')
    lineSenderSendJitter, = axPacketSendRecvJitter.plot([], [], c='k')
    lineReaderMissingPacketsBefore, = axMissingPackets.plot([], [], c='r')
    lineSenderDroppedPacketsBefore, = axMissingPackets.plot([], [], c='b')
    #linePacketSize, = axPacketSize.plot([], [], c='k')


def updateSenderReaderGraphs(senderFile, readerFile):
    global axPacketSendRecvTime
    global axPacketSendRecvJitter
    global axMissingPackets
    #global axPacketSize
    global lineSenderAcqToNetworkTime
    global lineSenderAcqToNetworkInputTime
    global lineSenderNetworkSendTime
    global lineReaderRecvTime
    global lineReaderRecvJitter
    global lineSenderSendJitter
    global lineSenderDroppedPacketsBefore
    global lineReaderMissingPacketsBefore
    #global linePacketSize

    csvSenderFile = open(senderFile, 'r')
    csvSenderFile.readline()
    senderStats = RtpSenderStat(csvSenderFile)
    csvSenderFile.close()

    csvReaderFile = open(readerFile, 'r')
    csvReaderFile.readline()
    readerStats = RtpReaderStat(csvReaderFile)
    csvReaderFile.close()

    dataSenderAcqToNetworkTime = add(senderStats.getAcqToNetworkSendTime(), senderStats.getAcqToNetworkDropTime())
    senderAcqToNetworkTimeMin = amin(dataSenderAcqToNetworkTime)
    senderAcqToNetworkTimeMax = amax(dataSenderAcqToNetworkTime)

    dataSenderAcqToNetworkInputTime = subtract(dataSenderAcqToNetworkTime, add(senderStats.getNetworkSendTime(), senderStats.getNetworkDropTime()))
    senderAcqToNetworkInputTimeMin = amin(dataSenderAcqToNetworkInputTime)
    senderAcqToNetworkInputTimeMax = amax(dataSenderAcqToNetworkInputTime)

    dataSenderNetworkSendTime = add(senderStats.getNetworkSendTime(), senderStats.getNetworkDropTime())
    senderNetworkSendTimeMin = amin(dataSenderNetworkSendTime)
    senderNetworkSendTimeMax = amax(dataSenderNetworkSendTime)

    dataSenderSendJitter = absolute(subtract(dataSenderAcqToNetworkTime, mean(dataSenderAcqToNetworkTime)))
    senderSendJitterMin = amin(dataSenderSendJitter)
    senderSendJitterMax = amax(dataSenderSendJitter)
    senderSendJitterMean = mean(absolute(dataSenderSendJitter))
    senderDroppedPackets = float(sum(senderStats.getDroppedPacketsBefore())) * 100. / (amax(senderStats.getPacketIndex()) - amin(senderStats.getPacketIndex()))
    print "Mean sender jitter: " + str(senderSendJitterMean) + " ms"
    print "Dropped packets: " + str(senderDroppedPackets) + "%"

    dataReaderRecvJitter = absolute(subtract(readerStats.getNetworkRecvTime(), mean(readerStats.getNetworkRecvTime())))
    readerRecvJitterMin = amin(dataReaderRecvJitter)
    readerRecvJitterMax = amax(dataReaderRecvJitter)
    readerRecvJitterMean = mean(absolute(dataReaderRecvJitter))
    readerMissingPackets = float(sum(readerStats.getMissingPacketsBefore())) * 100. / (amax(readerStats.getPacketIndex()) - amin(readerStats.getPacketIndex()))
    if senderSendJitterMean != 0:
        jitterRatio = float(readerRecvJitterMean) / float(senderSendJitterMean)
    else:
        jitterRatio = 0.
    if senderDroppedPackets != 0:
        lossRatio = readerMissingPackets / senderDroppedPackets
    else:
        lossRatio = 0.
    print "Mean reader jitter: " + str(readerRecvJitterMean) + " ms (x" + str(jitterRatio) + ")"
    print "Missing packets: " + str(readerMissingPackets) + "% (x" + str(lossRatio) + ")"

    packetIndexMax = amax(senderStats.getPacketIndex())
    xmin, xmax = axPacketSendRecvTime.get_xlim()
    if packetIndexMax > xmax:
        axPacketSendRecvTime.set_xlim(xmin, packetIndexMax * 1.2)

    networkRecvTimeMin = amin(readerStats.getNetworkRecvTime())
    networkRecvTimeMax = amax(readerStats.getNetworkRecvTime())
    ymin, ymax = axPacketSendRecvTime.get_ylim()
    if senderAcqToNetworkTimeMax > ymax:
        ymax = senderAcqToNetworkTimeMax * 1.2
    if senderAcqToNetworkInputTimeMax > ymax:
        ymax = senderAcqToNetworkInputTimeMax * 1.2
    if senderNetworkSendTimeMax > ymax:
        ymax = senderNetworkSendTimeMax * 1.2
    if networkRecvTimeMin < ymin:
        ymin = networkRecvTimeMin * 1.2
    if networkRecvTimeMax > ymax:
        ymax = networkRecvTimeMax * 1.2
    axPacketSendRecvTime.set_ylim(ymin, ymax)

    ymin, ymax = axPacketSendRecvJitter.get_ylim()
    if senderSendJitterMin < ymin:
        ymin = senderSendJitterMin * 1.2
    if senderSendJitterMax > ymax:
        ymax = senderSendJitterMax * 1.2
    if readerRecvJitterMin < ymin:
        ymin = readerRecvJitterMin * 1.2
    if readerRecvJitterMax > ymax:
        ymax = readerRecvJitterMax * 1.2
    axPacketSendRecvJitter.set_ylim(ymin, ymax)

    bytesSentMax = amax(senderStats.getBytesSent())
    #ymin, ymax = axPacketSize.get_ylim()
    #if bytesSentMax > ymax:
    #    axPacketSize.set_ylim(ymin, bytesSentMax * 1.2)

    droppedPacketsBeforeMax = amax(senderStats.getDroppedPacketsBefore())
    missingPacketsBeforeMax = amax(readerStats.getMissingPacketsBefore())
    ymin, ymax = axMissingPackets.get_ylim()
    if droppedPacketsBeforeMax > ymax:
        ymax = droppedPacketsBeforeMax * 1.2
    if missingPacketsBeforeMax > ymax:
        ymax = missingPacketsBeforeMax * 1.2
    axMissingPackets.set_ylim(ymin, ymax)

    axPacketSendRecvTime.figure.canvas.draw()
    axPacketSendRecvJitter.figure.canvas.draw()
    axMissingPackets.figure.canvas.draw()
    #axPacketSize.figure.canvas.draw()
    lineSenderAcqToNetworkTime.set_data(senderStats.getPacketIndex(), dataSenderAcqToNetworkTime)
    lineSenderAcqToNetworkInputTime.set_data(senderStats.getPacketIndex(), dataSenderAcqToNetworkInputTime)
    lineSenderNetworkSendTime.set_data(senderStats.getPacketIndex(), dataSenderNetworkSendTime)
    lineReaderRecvTime.set_data(readerStats.getPacketIndex(), readerStats.getNetworkRecvTime())
    lineSenderSendJitter.set_data(senderStats.getPacketIndex(), dataSenderSendJitter)
    lineReaderRecvJitter.set_data(readerStats.getPacketIndex(), dataReaderRecvJitter)
    lineSenderDroppedPacketsBefore.set_data(senderStats.getPacketIndex(), senderStats.getDroppedPacketsBefore())
    lineReaderMissingPacketsBefore.set_data(readerStats.getPacketIndex(), readerStats.getMissingPacketsBefore())
    #linePacketSize.set_data(senderStats.getPacketIndex(), senderStats.getBytesSent())
    plt.draw()


def onClickSenderReader(event, senderFile, readerFile):
    updateSenderReaderGraphs(senderFile, readerFile)


def usage():
    print 'Usage:'
    print '    ' + sys.argv[0] + ' -f <frameinfo_file>'
    print '    ' + sys.argv[0] + ' -s <rtp_sender_file>'
    print '    ' + sys.argv[0] + ' -r <rtp_reader_file>'
    print '    ' + sys.argv[0] + ' -s <rtp_sender_file> -r <rtp_reader_file>'


def main(argv):
    global frameInfoFile
    global senderFile
    global readerFile
    
    # command-line arguments
    try:
        opts, args = getopt.getopt(argv,"hf:s:r:",["frameinfo=", "sender=", "reader="])
    except getopt.GetoptError:
        usage()
        sys.exit(2)

    for opt, arg in opts:
        if opt == '-h':
            usage()
            sys.exit()
        elif opt in ("-f", "--frameinfo"):
            frameInfoFile = arg
        elif opt in ("-s", "--sender"):
            senderFile = arg
        elif opt in ("-r", "--reader"):
            readerFile = arg

    if frameInfoFile == '' and senderFile == '' and readerFile == '':
        usage()
        sys.exit(2)


    if senderFile != '' and readerFile != '':
        fillRtpReaderColIndex(readerFile)
        fillRtpSenderColIndex(senderFile)
        createSenderReaderGraphs()
        updateSenderReaderGraphs(senderFile, readerFile)
        cid = fig1.canvas.mpl_connect('button_press_event', lambda event: onClickSenderReader(event, senderFile, readerFile))
        plt.show()


    elif frameInfoFile != '':
        fillFrameInfoColIndex(frameInfoFile)
        createFrameInfoGraphs()
        updateFrameInfoGraphs(frameInfoFile)
        cid = fig1.canvas.mpl_connect('button_press_event', lambda event: onClickFrameInfo(event, frameInfoFile))
        plt.show()


    elif senderFile != '':
        fillRtpSenderColIndex(senderFile)
        createSenderGraphs()
        updateSenderGraphs(senderFile)
        cid = fig1.canvas.mpl_connect('button_press_event', lambda event: onClickSender(event, senderFile))
        plt.show()


    elif readerFile != '':
        fillRtpReaderColIndex(readerFile)
        createReaderGraphs()
        updateReaderGraphs(readerFile)
        cid = fig1.canvas.mpl_connect('button_press_event', lambda event: onClickReader(event, readerFile))
        plt.show()


if __name__ == '__main__':
    main(sys.argv[1:])

