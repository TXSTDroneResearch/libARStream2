#!/usr/bin/python

# @file beaver_rtpreader.py
# @brief RTP Reader data class
# @date 09/28/2015
# @author aurelien.barre@parrot.com


rtpReaderColIndex = {"recvTimestamp" : -1, "rtpTimestamp" : -1, "rtpTimestampShifted" : -1, "rtpSeqNum" : -1, "rtpMarkerBit" : -1, "bytes" : -1}


def fillRtpReaderColIndex(readerFile):
    with open(readerFile, 'r') as csvReaderFile:
        # detect the available columns from the first line
        line = csvReaderFile.readline()
        row = line.split(' ')
        for idx, val in enumerate(row):
            val = val.strip()
            if val in rtpReaderColIndex:
                rtpReaderColIndex[val] = idx
    csvReaderFile.close()


class RtpReaderStatFileLine:
    def __init__(self, line):
        self.row = []
        if line:
            self.row = line.split(' ')

    def getStringVal(self, key):
        if rtpReaderColIndex[key] >= 0 and len(self.row) > rtpReaderColIndex[key]:
            return self.row[rtpReaderColIndex[key]]
        else:
            return "0"

    def getIntVal(self, key):
        return int(self.getStringVal(key))

    def getFloatVal(self, key):
        return float(self.getStringVal(key))

    def getRecvTimestamp(self):
        return self.getIntVal("recvTimestamp")

    def getRtpTimestamp(self):
        return self.getIntVal("rtpTimestamp")

    def getRtpTimestampShifted(self):
        return self.getIntVal("rtpTimestampShifted")

    def getRtpSeqNum(self):
        return self.getIntVal("rtpSeqNum")

    def getRtpMarkerBit(self):
        return self.getIntVal("rtpMarkerBit")

    def getBytes(self):
        return self.getIntVal("bytes")


class RtpReaderStat:
    def __init__(self, csvFile):
        refTime1 = 0
        refTime2 = 0
        seqNumOffset = 0
        previousSeqNum = -1
        self.dataPacketTime = []
        self.dataNetworkRecvTime = []
        self.dataBytes = []
        self.dataMissingPacketsBefore = []
        self.dataPacketIndex = []
        lineLoop = 1
        while lineLoop:
            line = csvFile.readline()
            if line:
                statLine = RtpReaderStatFileLine(line)

                recvTimestamp = statLine.getRecvTimestamp()
                rtpTimestamp = statLine.getRtpTimestamp()
                rtpTimestampShifted = statLine.getRtpTimestampShifted()
                rtpSeqNum = statLine.getRtpSeqNum()
                rtpMarkerBit = statLine.getRtpMarkerBit()
                bytes = statLine.getBytes()
                auTimestamp = rtpTimestamp * 1000. / 90.
                if refTime1 == 0:
                    refTime1 = recvTimestamp
                if refTime2 == 0:
                    refTime2 = recvTimestamp - auTimestamp
                packetTime = (recvTimestamp - refTime1) / 1000000.
                self.dataPacketTime.append(packetTime)
                if rtpTimestampShifted != 0:
                    networkRecvTime = (recvTimestamp - rtpTimestampShifted) / 1000.
                else:
                    networkRecvTime = (recvTimestamp - auTimestamp - refTime2) / 1000.;
                self.dataNetworkRecvTime.append(networkRecvTime)
                self.dataBytes.append(bytes)
                if previousSeqNum != -1:
                    seqNumDelta = rtpSeqNum - previousSeqNum
                    if seqNumDelta < -32768:
                        seqNumDelta += 65536 #handle seqNum 16 bits loopback
                        seqNumOffset += 65536
                    if seqNumDelta > 0:
                        missingPacketsBefore = seqNumDelta - 1
                    else:
                        missingPacketsBefore = 0
                else:
                    missingPacketsBefore = 0
                self.dataMissingPacketsBefore.append(missingPacketsBefore)
                packetIndex = rtpSeqNum + seqNumOffset
                self.dataPacketIndex.append(packetIndex)
                previousSeqNum = rtpSeqNum
            else:
                break

    def getPacketTime(self):
        return self.dataPacketTime

    def getNetworkRecvTime(self):
        return self.dataNetworkRecvTime

    def getPacketIndex(self):
        return self.dataPacketIndex

    def getBytes(self):
        return self.dataBytes

    def getMissingPacketsBefore(self):
        return self.dataMissingPacketsBefore

