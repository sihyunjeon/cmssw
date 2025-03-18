import FWCore.ParameterSet.Config as cms
from Configuration.Eras.Era_Phase2C17I13M9_cff import Phase2C17I13M9

process = cms.Process('USER', Phase2C17I13M9)

# Standard configurations
process.load('Configuration.StandardSequences.Services_cff')
process.load('SimGeneral.HepPDTESSource.pythiapdt_cfi')
process.load('FWCore.MessageService.MessageLogger_cfi')
process.load('Configuration.EventContent.EventContent_cff')
process.load('SimGeneral.MixingModule.mixNoPU_cfi')
process.load('Configuration.Geometry.GeometryExtended2026D91Reco_cff')
process.load('Configuration.StandardSequences.MagneticField_cff')
process.load('Configuration.StandardSequences.Digi_cff')
process.load('Configuration.StandardSequences.SimL1Emulator_cff')
process.load('Configuration.StandardSequences.L1TrackTrigger_cff')
process.load('Configuration.StandardSequences.DigiToRaw_cff')
process.load('HLTrigger.Configuration.HLT_Fake2_cff')
process.load('Configuration.StandardSequences.RawToDigi_cff')
process.load('Configuration.StandardSequences.L1Reco_cff')
process.load('Configuration.StandardSequences.Reconstruction_cff')
process.load('Configuration.StandardSequences.EndOfProcess_cff')
process.load('Configuration.StandardSequences.FrontierConditions_GlobalTag_cff')

process.maxEvents = cms.untracked.PSet(
    input = cms.untracked.int32(1)
)

process.source = cms.Source("PoolSource",
    fileNames = cms.untracked.vstring(
        "file:6aec09bc-1e00-4831-b90c-9b42254f627a.root"
    )
)

process.load("CondCore.CondDB.CondDB_cfi")
process.CondDB.connect = 'sqlite_file:OTandITDTCCablingMap.db'
process.PoolDBESSource = cms.ESSource("PoolDBESSource",
    process.CondDB,
    DumpStat = cms.untracked.bool(True),
    toGet = cms.VPSet(cms.PSet(
        record = cms.string('TrackerDetToDTCELinkCablingMapRcd'),
        tag = cms.string("DTCCablingMapProducerUserRun")
    ))
)
process.es_prefer_local_cabling = cms.ESPrefer("PoolDBESSource", "")

process.options = cms.untracked.PSet()

process.configurationMetadata = cms.untracked.PSet(
    annotation = cms.untracked.string('step2 nevts:1'),
    name = cms.untracked.string('Applications'),
    version = cms.untracked.string('$Revision: 1.0 $')
)

# Phase2ITQCoreProducer remains unchanged:
process.Phase2ITQCoreProducer = cms.EDProducer(
    'Phase2ITQCoreProducer',
    src = cms.InputTag("generalTracks"),
    siPixelDigi = cms.InputTag("simSiPixelDigis", "Pixel")
)

# Packer (PixelToRawProducer) remains unchanged:
process.Packer = cms.EDProducer(
    'PixelToRawProducer',
    Phase2ITChipBitStream = cms.InputTag("Phase2ITQCoreProducer")
)

# Instead of the old RawToPixelProducer, we now introduce a two‐step chain:

# 1. RawToBitStreamProducer: reads FEDRawData (from Packer) and produces intermediate bitstream
process.RawToBitstreamProducer = cms.EDProducer(
    'RawToBitstreamProducer',
    fedRawDataCollection = cms.InputTag("Packer"),
    debug = cms.untracked.bool(False)
)

# 2. BitStreamToPixelProducer: decodes the intermediate bitstream into PixelDigi objects.
process.BitstreamToPixelProducer = cms.EDProducer(
    'BitstreamToPixelProducer',
    phase2ItChipBitStream = cms.InputTag("RawToBitstreamProducer")
)

# Trajectory producer (if needed)
process.load("RecoTracker.TrackProducer.TrackRefitters_cff")
process.TrackRefitter.src = "generalTracks"
process.TrackRefitter.NavigationSchool = ""

# Output module: keep FED raw, Phase2ITChipBitStream and PixelDigi
process.FEVTDEBUGoutput = cms.OutputModule("PoolOutputModule",
    fileName = cms.untracked.string('output_file.root'),
    outputCommands = cms.untracked.vstring(
        'drop *',
        'keep FEDRawDataCollection_*_*_*',
        'keep *_Phase2IT*_*_*',  # Save intermediate Phase2ITChipBitStream
        'keep PixelDigi*_*_*_*'
    )
)

# Path definitions
process.digitisation_step = cms.Path(process.pdigi_valid)
process.user_step = cms.Path(
    process.Phase2ITQCoreProducer *
    process.Packer *
    process.RawToBitstreamProducer *
    process.BitstreamToPixelProducer
)
process.endjob_step = cms.EndPath(process.endOfProcess)
process.output_step = cms.EndPath(process.FEVTDEBUGoutput)

process.schedule = cms.Schedule(process.user_step, process.endjob_step, process.output_step)

from FWCore.Modules.logErrorHarvester_cff import customiseLogErrorHarvesterUsingOutputCommands
process = customiseLogErrorHarvesterUsingOutputCommands(process)
from Configuration.StandardSequences.earlyDeleteSettings_cff import customiseEarlyDelete
process = customiseEarlyDelete(process)

