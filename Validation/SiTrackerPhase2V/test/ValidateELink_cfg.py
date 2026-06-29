import FWCore.ParameterSet.Config as cms
import FWCore.ParameterSet.VarParsing as VarParsing

options = VarParsing.VarParsing('analysis')
options.register('inputFile', 'file:input.root',
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.string,
                 'Input EDM file containing Phase2ITAuroraBitStream')
options.register('outputDQM', 'output.root',
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.string,
                 'DQM output ROOT file')
options.register('cablingDB', 'sqlite_file:OTandITDTCCablingMap.db',
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.string,
                 'Sqlite cabling-map DB connect string')
options.parseArguments()

process = cms.Process('ITRAWDQM')
process.load('FWCore.MessageService.MessageLogger_cfi')
process.MessageLogger.cerr.FwkReport.reportEvery = 1

process.maxEvents = cms.untracked.PSet(input=cms.untracked.int32(-1))
process.source = cms.Source('PoolSource',
    fileNames=cms.untracked.vstring(options.inputFile),
)

process.load('DQMServices.Core.DQMStore_cfi')

process.load('CondCore.CondDB.CondDB_cfi')
process.CondDB.connect = options.cablingDB
process.PoolDBESSource = cms.ESSource('PoolDBESSource', process.CondDB,
    DumpStat=cms.untracked.bool(True),
    toGet=cms.VPSet(cms.PSet(
        record=cms.string('TrackerDetToDTCELinkCablingMapRcd'),
        tag=cms.string('DTCCablingMapProducerUserRun'),
    )),
)
process.es_prefer_local_cabling = cms.ESPrefer('PoolDBESSource', '')

process.itRawDQM = cms.EDProducer('Phase2ITValidateELink',
    src      = cms.InputTag('BitStreamToRawProducer'),
    #?firstFed = cms.untracked.int32(0),
    #?nFeds    = cms.untracked.int32(576),
    folder   = cms.untracked.string('Phase2IT/RawData'),
)

process.dqmout = cms.OutputModule('DQMRootOutputModule',
    fileName = cms.untracked.string(options.outputDQM),
)
process.options.numberOfThreads = cms.untracked.uint32(1)

process.p   = cms.Path(process.itRawDQM)
process.end = cms.EndPath(process.dqmout)
