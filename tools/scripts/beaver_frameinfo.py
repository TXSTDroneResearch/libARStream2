#!/usr/bin/python

# @file beaver_frameinfo.py
# @brief FrameInfo data class
# @date 09/28/2015
# @author aurelien.barre@parrot.com


frameInfoColIndex = {"frameIndex" : -1, "acquisitionTs" : -1, "systemTs" : -1, "batteryPercentage" : -1, "gpsLatitude" : -1, "gpsLongitude" : -1, "gpsAltitude" : -1, "absoluteHeight" : -1, "relativeHeight" : -1, "xSpeed" : -1, "ySpeed" : -1, "zSpeed" : -1, "distanceFromHome" : -1, "yaw" : -1, "pitch" : -1, "roll" : -1, "cameraPan" : -1, "cameraTilt" : -1, "videoStreamingTargetBitrate" : -1, "videoStreamingDecimation" : -1,  "videoStreamingGopLength" : -1, "videoStreamingPrevFrameType" : -1, "videoStreamingPrevFrameSize" : -1, "videoStreamingPrevFramePsnrY" : -1, "videoRecordingPrevFrameType" : -1, "videoRecordingPrevFrameSize" : -1, "videoRecordingPrevFramePsnrY" : -1, "wifiRssi" : -1, "wifiMcsRate" : -1, "wifiTxRate" : -1, "wifiRxRate" : -1, "wifiTxFailRate" : -1, "wifiTxErrorRate" : -1, "wifiTxFailEventCount" : -1, "preReprojTimestampDelta" : -1, "postReprojTimestampDelta" : -1, "postEeTimestampDelta" : -1, "postScalingTimestampDelta" : -1, "postStreamingEncodingTimestampDelta" : -1, "postRecordingEncodingTimestampDelta" : -1, "postNetworkInputTimestampDelta" : -1, "streamingSrcMonitorTimeInterval" : -1, "streamingSrcMeanAcqToNetworkTime" : -1, "streamingSrcAcqToNetworkJitter" : -1, "streamingSrcMeanNetworkTime" : -1, "streamingSrcNetworkJitter" : -1, "streamingSrcBytesSent" : -1, "streamingSrcMeanPacketSize" : -1, "streamingSrcPacketSizeStdDev" : -1, "streamingSrcPacketsSent" : -1, "streamingSrcBytesDropped" : -1, "streamingSrcNaluDropped" : -1, "commandsMaxTimeDeltaOnLastSec" : -1, "lastCommandTimeDelta" : -1, "lastCommandPsiValue" : -1, "frameSize" : -1, "estimatedLostFrames" : -1, "acquisitionTsShifted" : -1, "beaverFirstNaluInputTime" : -1, "beaverAuOutputTime" : -1}


def fillFrameInfoColIndex(frameInfoFile):
    with open(frameInfoFile, 'r') as csvFrameInfoFile:
        # detect the available columns from the first line
        line = csvFrameInfoFile.readline()
        row = line.split(' ')
        for idx, val in enumerate(row):
            val = val.strip()
            if val in frameInfoColIndex:
                frameInfoColIndex[val] = idx
    csvFrameInfoFile.close()


class FrameInfoFileLine:
    def __init__(self, line):
        self.row = []
        if line:
            self.row = line.split(' ')

    def getStringVal(self, key):
        if frameInfoColIndex[key] >= 0 and len(self.row) > frameInfoColIndex[key]:
            return self.row[frameInfoColIndex[key]]
        else:
            return "0"

    def getIntVal(self, key):
        return int(self.getStringVal(key))

    def getFloatVal(self, key):
        return float(self.getStringVal(key))

    def getFrameIndex(self):
        return self.getIntVal("frameIndex")

    def getAcquisitionTs(self):
        return self.getIntVal("acquisitionTs")

    def getSystemTs(self):
        return self.getIntVal("systemTs")

    def getBatteryPercentage(self):
        return self.getIntVal("batteryPercentage")

    def getGpsLatitude(self):
        return self.getFloatVal("gpsLatitude")

    def getGpsLongitude(self):
        return self.getFloatVal("gpsLongitude")

    def getGpsAltitude(self):
        return self.getFloatVal("gpsAltitude")

    def getAbsoluteHeight(self):
        return self.getFloatVal("absoluteHeight")

    def getRelativeHeight(self):
        return self.getFloatVal("relativeHeight")

    def getXSpeed(self):
        return self.getFloatVal("xSpeed")

    def getYSpeed(self):
        return self.getFloatVal("ySpeed")

    def getZSpeed(self):
        return self.getFloatVal("zSpeed")

    def getDistanceFromHome(self):
        return self.getFloatVal("distanceFromHome")

    def getYaw(self):
        return self.getFloatVal("yaw")

    def getPitch(self):
        return self.getFloatVal("pitch")

    def getRoll(self):
        return self.getFloatVal("roll")

    def getCameraPan(self):
        return self.getFloatVal("cameraPan")

    def getCameraTilt(self):
        return self.getFloatVal("cameraTilt")

    def getVideoStreamingTargetBitrate(self):
        return self.getIntVal("videoStreamingTargetBitrate")

    def getVideoStreamingGopLength(self):
        return self.getIntVal("videoStreamingGopLength")

    def getVideoStreamingPrevFrameType(self):
        return self.getIntVal("videoStreamingPrevFrameType")

    def getVideoStreamingPrevFrameSize(self):
        return self.getIntVal("videoStreamingPrevFrameSize")

    def getVideoStreamingPrevFramePsnrY(self):
        return self.getFloatVal("videoStreamingPrevFramePsnrY")

    def getVideoRecordingPrevFrameType(self):
        return self.getIntVal("videoRecordingPrevFrameType")

    def getVideoRecordingPrevFrameSize(self):
        return self.getIntVal("videoRecordingPrevFrameSize")

    def getVideoRecordingPrevFramePsnrY(self):
        return self.getFloatVal("videoRecordingPrevFramePsnrY")
 
    def getWifiRssi(self):
        return self.getIntVal("wifiRssi")

    def getWifiMcsRate(self):
        return self.getIntVal("wifiMcsRate")

    def getWifiTxRate(self):
        return self.getIntVal("wifiTxRate")

    def getWifiRxRate(self):
        return self.getIntVal("wifiRxRate")

    def getWifiTxFailRate(self):
        return self.getIntVal("wifiTxFailRate")

    def getWifiTxErrorRate(self):
        return self.getIntVal("wifiTxErrorRate")

    def getWifiTxFailEventCount(self):
        return self.getIntVal("wifiTxFailEventCount")

    def getPreReprojTimestampDelta(self):
        return self.getIntVal("preReprojTimestampDelta")

    def getPostReprojTimestampDelta(self):
        return self.getIntVal("postReprojTimestampDelta")

    def getPostEeTimestampDelta(self):
        return self.getIntVal("postEeTimestampDelta")

    def getPostScalingTimestampDelta(self):
        return self.getIntVal("postScalingTimestampDelta")

    def getPostStreamingEncodingTimestampDelta(self):
        return self.getIntVal("postStreamingEncodingTimestampDelta")

    def getPostRecordingEncodingTimestampDelta(self):
        return self.getIntVal("postRecordingEncodingTimestampDelta")

    def getPostNetworkInputTimestampDelta(self):
        return self.getIntVal("postNetworkInputTimestampDelta")

    def getStreamingSrcMonitorTimeInterval(self):
        return self.getIntVal("streamingSrcMonitorTimeInterval")

    def getStreamingSrcMeanAcqToNetworkTime(self):
        return self.getIntVal("streamingSrcMeanAcqToNetworkTime")

    def getStreamingSrcAcqToNetworkJitter(self):
        return self.getIntVal("streamingSrcAcqToNetworkJitter")

    def getStreamingSrcMeanNetworkTime(self):
        return self.getIntVal("streamingSrcMeanNetworkTime")

    def getStreamingSrcNetworkJitter(self):
        return self.getIntVal("streamingSrcNetworkJitter")

    def getStreamingSrcBytesSent(self):
        return self.getIntVal("streamingSrcBytesSent")

    def getStreamingSrcMeanPacketSize(self):
        return self.getIntVal("streamingSrcMeanPacketSize")

    def getStreamingSrcPacketSizeStdDev(self):
        return self.getIntVal("streamingSrcPacketSizeStdDev")

    def getStreamingSrcPacketsSent(self):
        return self.getIntVal("streamingSrcPacketsSent")

    def getStreamingSrcBytesDropped(self):
        return self.getIntVal("streamingSrcBytesDropped")

    def getStreamingSrcNaluDropped(self):
        return self.getIntVal("streamingSrcNaluDropped")

    def getCommandsMaxTimeDeltaOnLastSec(self):
        return self.getIntVal("commandsMaxTimeDeltaOnLastSec")

    def getLastCommandTimeDelta(self):
        return self.getIntVal("lastCommandTimeDelta")

    def getLastCommandPsiValue(self):
        return self.getIntVal("lastCommandPsiValue")

    def getFrameSize(self):
        return self.getIntVal("frameSize")

    def getEstimatedLostFrames(self):
        return self.getIntVal("estimatedLostFrames")

    def getAcquisitionTsShifted(self):
        return self.getIntVal("acquisitionTsShifted")

    def getBeaverFirstNaluInputTime(self):
        return self.getIntVal("beaverFirstNaluInputTime")

    def getBeaverAuOutputTime(self):
        return self.getIntVal("beaverAuOutputTime")


class FrameInfoMonitoring:
    def __init__(self, csvFile):
        refTime = 0
        prevTime = 0
        prevIndex = 0

        self.dataFrameTime = []
        self.dataFrameIndex = []
        self.dataAcquisitionTs = []
        self.dataSystemTs = []
        self.dataBatteryPercentage = []
        self.dataGpsLatitude = []
        self.dataGpsLongitude = []
        self.dataGpsAltitude = []
        self.dataAbsoluteHeight = []
        self.dataRelativeHeight = []
        self.dataXSpeed = []
        self.dataYSpeed = []
        self.dataZSpeed = []
        self.dataDistanceFromHome = []
        self.dataYaw = []
        self.dataPitch = []
        self.dataRoll = []
        self.dataCameraPan = []
        self.dataCameraTilt = []
        self.dataVideoStreamingTargetBitrate = []
        self.dataVideoStreamingGopLength = []
        self.dataVideoStreamingPrevFrameType = []
        self.dataVideoStreamingPrevFrameSize = []
        self.dataVideoStreamingPrevFramePsnrY = []
        self.dataVideoRecordingPrevFrameType = []
        self.dataVideoRecordingPrevFrameSize = []
        self.dataVideoRecordingPrevFramePsnrY = []
        self.dataWifiRssi = []
        self.dataWifiMcsRate = []
        self.dataWifiTxRate = []
        self.dataWifiRxRate = []
        self.dataWifiTxFailRate = []
        self.dataWifiTxErrorRate = []
        self.dataWifiTxFailEventCount = []
        self.dataPreReprojTimestampDelta = []
        self.dataPostReprojTimestampDelta = []
        self.dataPostEeTimestampDelta = []
        self.dataPostScalingTimestampDelta = []
        self.dataPostStreamingEncodingTimestampDelta = []
        self.dataPostRecordingEncodingTimestampDelta = []
        self.dataPostNetworkInputTimestampDelta = []
        self.dataStreamingSrcMonitorTimeInterval = []
        self.dataStreamingSrcMeanAcqToNetworkTime = []
        self.dataStreamingSrcAcqToNetworkJitter = []
        self.dataStreamingSrcMeanNetworkTime = []
        self.dataStreamingSrcNetworkJitter = []
        self.dataStreamingSrcBytesSent = []
        self.dataStreamingSrcMeanPacketSize = []
        self.dataStreamingSrcPacketSizeStdDev = []
        self.dataStreamingSrcPacketsSent = []
        self.dataStreamingSrcBytesDropped = []
        self.dataStreamingSrcNaluDropped = []
        self.dataCommandsMaxTimeDeltaOnLastSec = []
        self.dataLastCommandTimeDelta = []
        self.dataLastCommandPsiValue = []
        self.dataFrameSize = []
        self.dataEstimatedLostFrames = []
        self.dataAcquisitionTsShifted = []
        self.dataBeaverFirstNaluInputTime = []
        self.dataBeaverAuOutputTime = []

        lineLoop = 1
        while lineLoop:
            line = csvFile.readline()
            if line:
                frameInfoLine = FrameInfoFileLine(line)
                frameTime = frameInfoLine.getAcquisitionTs()
                if refTime == 0:
                    refTime = frameTime
                if frameTime > prevTime:
                    prevTime = frameTime
                    self.dataFrameTime.append(float(frameTime) / 1000000.)
                    self.dataFrameIndex.append(frameInfoLine.getFrameIndex())
                    self.dataAcquisitionTs.append(frameInfoLine.getAcquisitionTs())
                    self.dataSystemTs.append(frameInfoLine.getSystemTs())
                    self.dataBatteryPercentage.append(frameInfoLine.getBatteryPercentage())
                    self.dataGpsLatitude.append(frameInfoLine.getGpsLatitude())
                    self.dataGpsLongitude.append(frameInfoLine.getGpsLongitude())
                    self.dataGpsAltitude.append(frameInfoLine.getGpsAltitude())
                    self.dataAbsoluteHeight.append(frameInfoLine.getAbsoluteHeight())
                    self.dataRelativeHeight.append(frameInfoLine.getRelativeHeight())
                    self.dataXSpeed.append(frameInfoLine.getXSpeed())
                    self.dataYSpeed.append(frameInfoLine.getYSpeed())
                    self.dataZSpeed.append(frameInfoLine.getZSpeed())
                    self.dataDistanceFromHome.append(frameInfoLine.getDistanceFromHome())
                    self.dataYaw.append(frameInfoLine.getYaw())
                    self.dataPitch.append(frameInfoLine.getPitch())
                    self.dataRoll.append(frameInfoLine.getRoll())
                    self.dataCameraPan.append(frameInfoLine.getCameraPan())
                    self.dataCameraTilt.append(frameInfoLine.getCameraTilt())
                    self.dataVideoStreamingTargetBitrate.append(float(frameInfoLine.getVideoStreamingTargetBitrate()) / 1000000.)
                    self.dataVideoStreamingGopLength.append(frameInfoLine.getVideoStreamingGopLength())
                    self.dataVideoStreamingPrevFrameType.append(frameInfoLine.getVideoStreamingPrevFrameType())
                    self.dataVideoStreamingPrevFrameSize.append(frameInfoLine.getVideoStreamingPrevFrameSize())
                    self.dataVideoStreamingPrevFramePsnrY.append(frameInfoLine.getVideoStreamingPrevFramePsnrY())
                    self.dataVideoRecordingPrevFrameType.append(frameInfoLine.getVideoRecordingPrevFrameType())
                    self.dataVideoRecordingPrevFrameSize.append(frameInfoLine.getVideoRecordingPrevFrameSize())
                    self.dataVideoRecordingPrevFramePsnrY.append(frameInfoLine.getVideoRecordingPrevFramePsnrY())
                    self.dataWifiRssi.append(frameInfoLine.getWifiRssi())
                    self.dataWifiMcsRate.append(float(frameInfoLine.getWifiMcsRate()) / 1000000.)
                    self.dataWifiTxRate.append(float(frameInfoLine.getWifiTxRate()) / 1000000.)
                    self.dataWifiRxRate.append(float(frameInfoLine.getWifiRxRate()) / 1000000.)
                    self.dataWifiTxFailRate.append(frameInfoLine.getWifiTxFailRate())
                    self.dataWifiTxErrorRate.append(frameInfoLine.getWifiTxErrorRate())
                    self.dataWifiTxFailEventCount.append(frameInfoLine.getWifiTxFailEventCount())
                    self.dataPreReprojTimestampDelta.append(frameInfoLine.getPreReprojTimestampDelta())
                    self.dataPostReprojTimestampDelta.append(frameInfoLine.getPostReprojTimestampDelta())
                    self.dataPostEeTimestampDelta.append(frameInfoLine.getPostEeTimestampDelta())
                    self.dataPostScalingTimestampDelta.append(frameInfoLine.getPostScalingTimestampDelta())
                    self.dataPostStreamingEncodingTimestampDelta.append(frameInfoLine.getPostStreamingEncodingTimestampDelta())
                    self.dataPostRecordingEncodingTimestampDelta.append(frameInfoLine.getPostRecordingEncodingTimestampDelta())
                    self.dataPostNetworkInputTimestampDelta.append(frameInfoLine.getPostNetworkInputTimestampDelta())
                    self.dataStreamingSrcMonitorTimeInterval.append(frameInfoLine.getStreamingSrcMonitorTimeInterval())
                    self.dataStreamingSrcMeanAcqToNetworkTime.append(frameInfoLine.getStreamingSrcMeanAcqToNetworkTime())
                    self.dataStreamingSrcAcqToNetworkJitter.append(frameInfoLine.getStreamingSrcAcqToNetworkJitter())
                    self.dataStreamingSrcMeanNetworkTime.append(frameInfoLine.getStreamingSrcMeanNetworkTime())
                    self.dataStreamingSrcNetworkJitter.append(frameInfoLine.getStreamingSrcNetworkJitter())
                    self.dataStreamingSrcBytesSent.append(frameInfoLine.getStreamingSrcBytesSent())
                    self.dataStreamingSrcMeanPacketSize.append(frameInfoLine.getStreamingSrcMeanPacketSize())
                    self.dataStreamingSrcPacketSizeStdDev.append(frameInfoLine.getStreamingSrcPacketSizeStdDev())
                    self.dataStreamingSrcPacketsSent.append(frameInfoLine.getStreamingSrcPacketsSent())
                    self.dataStreamingSrcBytesDropped.append(frameInfoLine.getStreamingSrcBytesDropped())
                    self.dataStreamingSrcNaluDropped.append(frameInfoLine.getStreamingSrcNaluDropped())
                    self.dataCommandsMaxTimeDeltaOnLastSec.append(frameInfoLine.getCommandsMaxTimeDeltaOnLastSec())
                    self.dataLastCommandTimeDelta.append(frameInfoLine.getLastCommandTimeDelta())
                    self.dataLastCommandPsiValue.append(frameInfoLine.getLastCommandPsiValue())
                    self.dataFrameSize.append(frameInfoLine.getFrameSize())
                    if frameInfoColIndex["estimatedLostFrames"] >= 0:
                        self.dataEstimatedLostFrames.append(frameInfoLine.getEstimatedLostFrames())
                    elif prevIndex > 0:
                        self.dataEstimatedLostFrames.append(frameInfoLine.getFrameIndex() - prevIndex - 1)
                    else:
                        self.dataEstimatedLostFrames.append(0)
                    self.dataAcquisitionTsShifted.append(frameInfoLine.getAcquisitionTsShifted())
                    self.dataBeaverFirstNaluInputTime.append(frameInfoLine.getBeaverFirstNaluInputTime())
                    self.dataBeaverAuOutputTime.append(frameInfoLine.getBeaverAuOutputTime())
                    prevIndex = frameInfoLine.getFrameIndex()
                else:
                    print "Inconsistent timestamp: " + str(frameTime) + " (line ignored)"
            else:
                break


    def getFrameTime(self):
        return self.dataFrameTime

    def getFrameIndex(self):
        return self.dataFrameIndex

    def getAcquisitionTs(self):
        return self.dataAcquisitionTs

    def getSystemTs(self):
        return self.dataSystemTs

    def getBatteryPercentage(self):
        return self.dataBatteryPercentage

    def getGpsLatitude(self):
        return self.dataGpsLatitude

    def getGpsLongitude(self):
        return self.dataGpsLongitude

    def getGpsAltitude(self):
        return self.dataGpsAltitude

    def getAbsoluteHeight(self):
        return self.dataAbsoluteHeight

    def getRelativeHeight(self):
        return self.dataRelativeHeight

    def getXSpeed(self):
        return self.dataXSpeed

    def getYSpeed(self):
        return self.dataYSpeed

    def getZSpeed(self):
        return self.dataZSpeed

    def getDistanceFromHome(self):
        return self.dataDistanceFromHome

    def getYaw(self):
        return self.dataYaw

    def getPitch(self):
        return self.dataPitch

    def getRoll(self):
        return self.dataRoll

    def getCameraPan(self):
        return self.dataCameraPan

    def getCameraTilt(self):
        return self.dataCameraTilt

    def getVideoStreamingTargetBitrate(self):
        return self.dataVideoStreamingTargetBitrate

    def getVideoStreamingGopLength(self):
        return self.dataVideoStreamingGopLength

    def getVideoStreamingPrevFrameType(self):
        return self.dataVideoStreamingPrevFrameType

    def getVideoStreamingPrevFrameSize(self):
        return self.dataVideoStreamingPrevFrameSize

    def getVideoStreamingPrevFramePsnrY(self):
        return self.dataVideoStreamingPrevFramePsnrY

    def getVideoRecordingPrevFrameType(self):
        return self.dataVideoRecordingPrevFrameType

    def getVideoRecordingPrevFrameSize(self):
        return self.dataVideoRecordingPrevFrameSize

    def getVideoRecordingPrevFramePsnrY(self):
        return self.dataVideoRecordingPrevFramePsnrY
 
    def getWifiRssi(self):
        return self.dataWifiRssi

    def getWifiMcsRate(self):
        return self.dataWifiMcsRate

    def getWifiTxRate(self):
        return self.dataWifiTxRate

    def getWifiRxRate(self):
        return self.dataWifiRxRate

    def getWifiTxFailRate(self):
        return self.dataWifiTxFailRate

    def getWifiTxErrorRate(self):
        return self.dataWifiTxErrorRate

    def getWifiTxFailEventCount(self):
        return self.dataWifiTxFailEventCount

    def getPreReprojTimestampDelta(self):
        return self.dataPreReprojTimestampDelta

    def getPostReprojTimestampDelta(self):
        return self.dataPostReprojTimestampDelta

    def getPostEeTimestampDelta(self):
        return self.dataPostEeTimestampDelta

    def getPostScalingTimestampDelta(self):
        return self.dataPostScalingTimestampDelta

    def getPostStreamingEncodingTimestampDelta(self):
        return self.dataPostStreamingEncodingTimestampDelta

    def getPostRecordingEncodingTimestampDelta(self):
        return self.dataPostRecordingEncodingTimestampDelta

    def getPostNetworkInputTimestampDelta(self):
        return self.dataPostNetworkInputTimestampDelta

    def getStreamingSrcMonitorTimeInterval(self):
        return self.dataStreamingSrcMonitorTimeInterval

    def getStreamingSrcMeanAcqToNetworkTime(self):
        return self.dataStreamingSrcMeanAcqToNetworkTime

    def getStreamingSrcAcqToNetworkJitter(self):
        return self.dataStreamingSrcAcqToNetworkJitter

    def getStreamingSrcMeanNetworkTime(self):
        return self.dataStreamingSrcMeanNetworkTime

    def getStreamingSrcNetworkJitter(self):
        return self.dataStreamingSrcNetworkJitter

    def getStreamingSrcBytesSent(self):
        return self.dataStreamingSrcBytesSent

    def getStreamingSrcMeanPacketSize(self):
        return self.dataStreamingSrcMeanPacketSize

    def getStreamingSrcPacketSizeStdDev(self):
        return self.dataStreamingSrcPacketSizeStdDev

    def getStreamingSrcPacketsSent(self):
        return self.dataStreamingSrcPacketsSent

    def getStreamingSrcBytesDropped(self):
        return self.dataStreamingSrcBytesDropped

    def getStreamingSrcNaluDropped(self):
        return self.dataStreamingSrcNaluDropped

    def getCommandsMaxTimeDeltaOnLastSec(self):
        return self.dataCommandsMaxTimeDeltaOnLastSec

    def getLastCommandTimeDelta(self):
        return self.dataLastCommandTimeDelta

    def getLastCommandPsiValue(self):
        return self.dataLastCommandPsiValue

    def getFrameSize(self):
        return self.dataFrameSize

    def getEstimatedLostFrames(self):
        return self.dataEstimatedLostFrames

    def getAcquisitionTsShifted(self):
        return self.dataAcquisitionTsShifted

    def getBeaverFirstNaluInputTime(self):
        return self.dataBeaverFirstNaluInputTime

    def getBeaverAuOutputTime(self):
        return self.dataBeaverAuOutputTime

