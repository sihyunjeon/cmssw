# Phase-2 IT e-link DQM in one job: RelVal digis -> chip bitstream -> Aurora -> DQMIO

import os
import FWCore.ParameterSet.Config as cms
import FWCore.ParameterSet.VarParsing as VarParsing
from Configuration.Eras.Era_Phase2C17I13M9_cff import Phase2C17I13M9

def _findInSearchPath(rel):
    for base in os.environ.get('CMSSW_SEARCH_PATH', '').split(':'):
        path = os.path.join(base, rel)
        if os.path.exists(path):
            return path
    raise FileNotFoundError(rel + ' not found in CMSSW_SEARCH_PATH')

options = VarParsing.VarParsing('analysis')
options.register('outputDQM', 'elink_all.root',
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.string,
                 'DQMIO output file')
options.register('cablingDB', '',
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.string,
                 'Sqlite cabling-map DB connect string (default: the test DB found in CMSSW_SEARCH_PATH)')
options.register('eventsPerStream', 16,
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.int,
                 'Aurora NE events per stream group, 1-64; 1 reproduces the elink_ne1 run')
options.register('dropTot', 0,
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.int,
                 'Pack without the per-hit ToT; 1 reproduces the elink_binary run')
options.parseArguments()

# The default DB is only looked up when no cablingDB is given
cablingDB = options.cablingDB or 'sqlite_file:' + _findInSearchPath(
    'EventFilter/Phase2PixelRawToDigi/test/OTandITDTCCablingMap.db')

files = list(options.inputFiles)
if not files:
    for line in open(_findInSearchPath('Validation/SiTrackerPhase2V/test/filelist.txt')):
        line = line.strip()
        if line and not line.startswith('#'):
            files.append(line if '://' in line or line.startswith('file:') else 'root://cms-xrd-global.cern.ch/' + line)

process = cms.Process('ITELINKDQM', Phase2C17I13M9)

process.load('Configuration.StandardSequences.Services_cff')
process.load('FWCore.MessageService.MessageLogger_cfi')
process.load('Configuration.Geometry.GeometryExtendedRun4D112Reco_cff')
process.load('Configuration.StandardSequences.MagneticField_cff')
process.load('Configuration.StandardSequences.FrontierConditions_GlobalTag_cff')
process.load('DQMServices.Core.DQMStore_cfi')
process.load('Configuration.StandardSequences.DQMSaverAtRunEnd_cff')
process.MessageLogger.cerr.FwkReport.reportEvery = 200

from Configuration.AlCa.GlobalTag import GlobalTag
process.GlobalTag = GlobalTag(process.GlobalTag, 'auto:phase2_realistic', '')

process.maxEvents = cms.untracked.PSet(input=cms.untracked.int32(options.maxEvents))
process.source = cms.Source('PoolSource', fileNames=cms.untracked.vstring(files))

process.load('CondCore.CondDB.CondDB_cfi')
process.CondDB.connect = cablingDB
process.PoolDBESSource = cms.ESSource('PoolDBESSource', process.CondDB,
    toGet=cms.VPSet(cms.PSet(
        record=cms.string('TrackerDetToDTCELinkCablingMapRcd'),
        tag=cms.string('DTCCablingMapProducerUserRun'),
    )),
)
process.es_prefer_local_cabling = cms.ESPrefer('PoolDBESSource', '')

process.PixelToBitStreamProducer = cms.EDProducer('PixelToBitStreamProducer',
    siPixelDigi=cms.InputTag('simSiPixelDigis', 'Pixel'),
)
if options.dropTot:
    process.PixelToBitStreamProducer.dropTot = cms.bool(True)
process.BitStreamToAuroraProducer = cms.EDProducer('BitStreamToAuroraProducer',
    Phase2ITChipBitStream=cms.InputTag('PixelToBitStreamProducer'),
    eventsPerStream=cms.uint32(options.eventsPerStream),
    serviceBlockInterval=cms.uint32(50),
)

process.itElinkDQM = cms.EDProducer('Phase2ITValidateELink',
    auroraBitStream=cms.InputTag('BitStreamToAuroraProducer'),
    scaleTBPX=cms.untracked.double(1.07),
    scaleTFPX=cms.untracked.double(1.07),
    scaleTEPX=cms.untracked.double(1.17),
    trigger_rate=cms.double(750.0e3),
    elink_bandwidth=cms.double(1.28e9),
    folder=cms.untracked.string('InnerTrackerV/ELink'),
)

process.dqmout = cms.OutputModule('DQMRootOutputModule',
    fileName=cms.untracked.string(options.outputDQM),
)

process.p = cms.Path(process.PixelToBitStreamProducer * process.BitStreamToAuroraProducer * process.itElinkDQM)
process.e = cms.EndPath(process.dqmout)
process.schedule = cms.Schedule(process.p, process.e)
process.options.numberOfThreads = cms.untracked.uint32(1)
