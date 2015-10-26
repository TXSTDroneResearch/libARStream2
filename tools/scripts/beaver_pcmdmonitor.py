#!/usr/bin/python

# @file beaver_pcmdmonitor.py
# @brief PCMD Monitoring data class
# @date 10/05/2015
# @author aurelien.barre@parrot.com


pcmdMonitorColIndex = {"seqNum" : -1, "creationTimestamp" : -1, "creationTimestampShifted" : -1, "receptionTimestamp" : -1, "useTimestamp" : -1, "netmonTimestamp" : -1}


def fillPcmdMonitorColIndex(pcmdFile):
    with open(pcmdFile, 'r') as csvPcmdFile:
        # detect the available columns from the first line
        line = csvPcmdFile.readline()
        row = line.split(' ')
        for idx, val in enumerate(row):
            val = val.strip()
            if val in pcmdMonitorColIndex:
                pcmdMonitorColIndex[val] = idx
    csvPcmdFile.close()


class PcmdMonitorStatFileLine:
    def __init__(self, line):
        self.row = []
        if line:
            self.row = line.split(' ')

    def getStringVal(self, key):
        if pcmdMonitorColIndex[key] >= 0 and len(self.row) > pcmdMonitorColIndex[key]:
            return self.row[pcmdMonitorColIndex[key]]
        else:
            return "0"

    def getIntVal(self, key):
        return int(self.getStringVal(key))

    def getFloatVal(self, key):
        return float(self.getStringVal(key))

    def getSeqNum(self):
        return self.getIntVal("seqNum")

    def getCreationTimestamp(self):
        return self.getIntVal("creationTimestamp")

    def getCreationTimestampShifted(self):
        return self.getIntVal("creationTimestampShifted")

    def getReceptionTimestamp(self):
        return self.getIntVal("receptionTimestamp")

    def getUseTimestamp(self):
        return self.getIntVal("useTimestamp")

    def getNetmonTimestamp(self):
        return self.getIntVal("netmonTimestamp")


class PcmdMonitorStat:
    def __init__(self, csvFile):
        refTime = 0
        seqNumOffset = 0
        previousSeqNum = -1
        droppedCmdCounter = 0
        self.dataPcmdIndex = []
        self.dataSeqNum = []
        self.dataCreationTimestamp = []
        self.dataCreationTimestampShifted = []
        self.dataReceptionTimestamp = []
        self.dataUseTimestamp = []
        self.dataNetmonTimestamp = []
        self.dataMissingPcmdBefore = []
        lineLoop = 1
        while lineLoop:
            line = csvFile.readline()
            if line:
                statLine = PcmdMonitorStatFileLine(line)

                seqNum = statLine.getSeqNum()
                creationTimestamp = statLine.getCreationTimestamp()
                creationTimestampShifted = statLine.getCreationTimestampShifted()
                receptionTimestamp = statLine.getReceptionTimestamp()
                useTimestamp = statLine.getUseTimestamp()
                netmonTimestamp = statLine.getNetmonTimestamp()
                if refTime == 0:
                    refTime = creationTimestamp
                if previousSeqNum != -1:
                    seqNumDelta = seqNum - previousSeqNum
                    if seqNumDelta < -128:
                        seqNumDelta += 256 #handle seqNum 8 bits loopback
                        seqNumOffset += 256
                    if seqNumDelta > 0:
                        missingPcmdBefore = seqNumDelta - 1
                    else:
                        missingPcmdBefore = 0
                else:
                    missingPcmdBefore = 0
                pcmdIndex = seqNum + seqNumOffset
                self.dataPcmdIndex.append(pcmdIndex)
                previousSeqNum = seqNum
                self.dataSeqNum.append(seqNum)
                self.dataCreationTimestamp.append(creationTimestamp)
                self.dataCreationTimestampShifted.append(creationTimestampShifted)
                self.dataReceptionTimestamp.append(receptionTimestamp)
                self.dataUseTimestamp.append(useTimestamp)
                self.dataNetmonTimestamp.append(netmonTimestamp)
                self.dataMissingPcmdBefore.append(missingPcmdBefore)
            else:
                break

    def getPcmdIndex(self):
        return self.dataPcmdIndex

    def getSeqNum(self):
        return self.dataSeqNum

    def getCreationTimestamp(self):
        return self.dataCreationTimestamp

    def getCreationTimestampShifted(self):
        return self.dataCreationTimestampShifted

    def getReceptionTimestamp(self):
        return self.dataReceptionTimestamp

    def getUseTimestamp(self):
        return self.dataUseTimestamp

    def getNetmonTimestamp(self):
        return self.dataNetmonTimestamp

    def getMissingPcmdBefore(self):
        return self.dataMissingPcmdBefore

