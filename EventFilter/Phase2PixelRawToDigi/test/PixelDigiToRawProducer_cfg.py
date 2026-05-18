# Phase-2 IT packer/unpacker round-trip:
#   pdigi -> per-chip bitstream -> raw -> per-chip bitstream -> pdigi

import FWCore.ParameterSet.Config as cms
from Configuration.Eras.Era_Phase2C17I13M9_cff import Phase2C17I13M9

process = cms.Process('USER', Phase2C17I13M9)

process.load('Configuration.StandardSequences.Services_cff')
process.load('FWCore.MessageService.MessageLogger_cfi')
process.load('Configuration.Geometry.GeometryExtendedRun4D102Reco_cff')
process.load('Configuration.StandardSequences.MagneticField_cff')
process.load('Configuration.StandardSequences.EndOfProcess_cff')
process.load('Configuration.StandardSequences.FrontierConditions_GlobalTag_cff')

from Configuration.AlCa.GlobalTag import GlobalTag
process.GlobalTag = GlobalTag(process.GlobalTag, 'auto:phase2_realistic', '')

process.maxEvents = cms.untracked.PSet(input = cms.untracked.int32(1))

process.source = cms.Source('PoolSource',
    fileNames = cms.untracked.vstring(
        'root://cmseos.fnal.gov:1094//eos/uscms/store/user/laceyd/3be594c4-9067-4ee7-b2c7-39f8894328e2.root'
    )
)

# Local DTC cabling map (sqlite). Required by the packers/unpackers.
process.load('CondCore.CondDB.CondDB_cfi')
process.CondDB.connect = 'sqlite_file:OTandITDTCCablingMap.db'
process.PoolDBESSource = cms.ESSource('PoolDBESSource', process.CondDB,
    DumpStat = cms.untracked.bool(True),
    toGet = cms.VPSet(cms.PSet(
        record = cms.string('TrackerDetToDTCELinkCablingMapRcd'),
        tag    = cms.string('DTCCablingMapProducerUserRun'),
    )),
)
process.es_prefer_local_cabling = cms.ESPrefer('PoolDBESSource', '')

# Packer + unpacker chain.
process.PixelToBitStreamProducer = cms.EDProducer('PixelToBitStreamProducer',
    src         = cms.InputTag('generalTracks'),
    siPixelDigi = cms.InputTag('simSiPixelDigis', 'Pixel'),
)
process.BitStreamToRawProducer = cms.EDProducer('BitStreamToRawProducer',
    Phase2ITChipBitStream = cms.InputTag('PixelToBitStreamProducer'),
)
process.RawToBitStreamProducer = cms.EDProducer('RawToBitStreamProducer',
    fedRawDataCollection = cms.InputTag('BitStreamToRawProducer'),
    debug                = cms.untracked.bool(False),
)
process.BitStreamToPixelProducer = cms.EDProducer('BitStreamToPixelProducer',
    phase2ItChipBitStream = cms.InputTag('RawToBitStreamProducer'),
)

process.FEVTDEBUGoutput = cms.OutputModule('PoolOutputModule',
    fileName       = cms.untracked.string('output_file.root'),
    outputCommands = cms.untracked.vstring('drop *'),
)

process.user_step = cms.Path(
    process.PixelToBitStreamProducer
    * process.BitStreamToRawProducer
    * process.RawToBitStreamProducer
    * process.BitStreamToPixelProducer
)
process.endjob_step = cms.EndPath(process.endOfProcess)
process.output_step = cms.EndPath(process.FEVTDEBUGoutput)

process.schedule = cms.Schedule(
    process.user_step,
    process.endjob_step,
    process.output_step,
)
