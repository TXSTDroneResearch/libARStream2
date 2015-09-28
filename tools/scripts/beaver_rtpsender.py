#!/usr/bin/python

# @file beaver_rtpsender.py
# @brief RTP Sender data class
# @date 09/28/2015
# @author aurelien.barre@parrot.com


rtpSenderColIndex = {"sendTimestamp" : -1, "inputTimestamp" : -1, "auTimestamp" : -1, "rtpTimestamp" : -1, "rtpSeqNum" : -1, "rtpMarkerBit" : -1, "bytesSent" : -1, "bytesDropped" : -1}


def fillRtpSenderColIndex(senderFile):
    with open(senderFile, 'r') as csvSenderFile:
        # detect the available columns from the first line
        line = csvSenderFile.readline()
        row = line.split(' ')
        for idx, val in enumerate(row):
            val = val.strip()
            if val in rtpSenderColIndex:
                rtpSenderColIndex[val] = idx
    csvSenderFile.close()


class RtpSenderStatFileLine:
    def __init__(self, line):
        self.row = []
        if line:
            self.row = line.split(' ')

    def getStringVal(self, key):
        if rtpSenderColIndex[key] >= 0 and len(self.row) > rtpSenderColIndex[key]:
            return self.row[rtpSenderColIndex[key]]
        else:
            return "0"

    def getIntVal(self, key):
        return int(self.getStringVal(key))

    def getFloatVal(self, key):
        return float(self.getStringVal(key))

    def getSendTimestamp(self):
        return self.getIntVal("sendTimestamp")

    def getInputTimestamp(self):
        return self.getIntVal("inputTimestamp")

    def getAuTimestamp(self):
        return self.getIntVal("auTimestamp")

    def getRtpTimestamp(self):
        return self.getIntVal("rtpTimestamp")

    def getRtpSeqNum(self):
        return self.getIntVal("rtpSeqNum")

    def getRtpMarkerBit(self):
        return self.getIntVal("rtpMarkerBit")

    def getBytesSent(self):
        return self.getIntVal("bytesSent")

    def getBytesDropped(self):
        return self.getIntVal("bytesDropped")


class RtpSenderStat:
    def __init__(self, csvFile):
        refTime = 0
        seqNumOffset = 0
        previousSeqNum = -1
        droppedPacketsCounter = 0
        self.dataPacketTime = []
        self.dataPacketIndex = []
        self.dataAcqToNetworkSendTime = []
        self.dataAcqToNetworkDropTime = []
        self.dataNetworkSendTime = []
        self.dataNetworkDropTime = []
        self.dataBytesSent = []
        self.dataBytesDropped = []
        self.dataDroppedPacketsBefore = []
        lineLoop = 1
        while lineLoop:
            line = csvFile.readline()
            if line:
                statLine = RtpSenderStatFileLine(line)

                sendTimestamp = statLine.getSendTimestamp()
                inputTimestamp = statLine.getInputTimestamp()
                auTimestamp = statLine.getAuTimestamp()
                rtpTimestamp = statLine.getRtpTimestamp()
                rtpSeqNum = statLine.getRtpSeqNum()
                rtpMarkerBit = statLine.getRtpMarkerBit()
                bytesSent = statLine.getBytesSent()
                bytesDropped = statLine.getBytesDropped()
                if refTime == 0:
                    refTime = auTimestamp
                packetTime = (sendTimestamp - refTime) / 1000000.
                self.dataPacketTime.append(packetTime)
                if bytesSent > 0:
                    acqToNetworkSendTime = (sendTimestamp - auTimestamp) / 1000.
                    networkSendTime = (sendTimestamp - inputTimestamp) / 1000.
                    acqToNetworkDropTime = 0
                    networkDropTime = 0
                    droppedPacketsBefore = droppedPacketsCounter
                    droppedPacketsCounter = 0
                else:
                    acqToNetworkSendTime = 0
                    networkSendTime = 0
                    acqToNetworkDropTime = (sendTimestamp - auTimestamp) / 1000.
                    networkDropTime = (sendTimestamp - inputTimestamp) / 1000.
                    droppedPacketsBefore = 0
                    droppedPacketsCounter += 1
                if previousSeqNum != -1:
                    seqNumDelta = rtpSeqNum - previousSeqNum
                    if seqNumDelta < -32768:
                        seqNumDelta += 65536 #handle seqNum 16 bits loopback
                        seqNumOffset += 65536
                packetIndex = rtpSeqNum + seqNumOffset
                self.dataPacketIndex.append(packetIndex)
                previousSeqNum = rtpSeqNum
                self.dataDroppedPacketsBefore.append(droppedPacketsBefore)
                self.dataAcqToNetworkSendTime.append(acqToNetworkSendTime)
                self.dataAcqToNetworkDropTime.append(acqToNetworkDropTime)
                self.dataNetworkSendTime.append(networkSendTime)
                self.dataNetworkDropTime.append(networkDropTime)
                self.dataBytesSent.append(bytesSent)
                self.dataBytesDropped.append(bytesDropped)
            else:
                break

    def getPacketTime(self):
        return self.dataPacketTime

    def getAcqToNetworkSendTime(self):
        return self.dataAcqToNetworkSendTime

    def getAcqToNetworkDropTime(self):
        return self.dataAcqToNetworkDropTime

    def getNetworkSendTime(self):
        return self.dataNetworkSendTime

    def getNetworkDropTime(self):
        return self.dataNetworkDropTime

    def getPacketIndex(self):
        return self.dataPacketIndex

    def getBytesSent(self):
        return self.dataBytesSent

    def getBytesDropped(self):
        return self.dataBytesDropped

    def getDroppedPacketsBefore(self):
        return self.dataDroppedPacketsBefore

