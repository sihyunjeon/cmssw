import FWCore.ParameterSet.Config as cms
import FWCore.ParameterSet.VarParsing as VarParsing

options = VarParsing.VarParsing('analysis')
##options.register('inputFile', 'file:input.root',
#                 VarParsing.VarParsing.multiplicity.singleton,
#                 VarParsing.VarParsing.varType.string,
#                 'Input EDM file containing RawDataBuffer')
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
    #fileNames=cms.untracked.vstring(options.inputFile),
    fileNames=cms.untracked.vstring(
        "root://maite.iihe.ac.be:1094//pnfs/iihe/cms/store/user/shjeon/tmp/output_file1.root",
        "root://maite.iihe.ac.be:1094//pnfs/iihe/cms/store/user/shjeon/tmp/output_file2.root",
        "root://maite.iihe.ac.be:1094//pnfs/iihe/cms/store/user/shjeon/tmp/output_file3.root",
        "root://maite.iihe.ac.be:1094//pnfs/iihe/cms/store/user/shjeon/tmp/output_file4.root",
        "root://maite.iihe.ac.be:1094//pnfs/iihe/cms/store/user/shjeon/tmp/output_file5.root",
    ),
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

process.itRawDQM = cms.EDProducer('Phase2ITValidateSLink',
    src      = cms.InputTag('BitStreamToRawProducer'),
    firstRawData = cms.untracked.int32(0),
    nRawDatas    = cms.untracked.int32(576),
    folder   = cms.untracked.string('Phase2IT/RawData'),
)

process.dqmout = cms.OutputModule('DQMRootOutputModule',
    fileName = cms.untracked.string(options.outputDQM),
)
process.options.numberOfThreads = cms.untracked.uint32(1)

process.p   = cms.Path(process.itRawDQM)
process.end = cms.EndPath(process.dqmout)
