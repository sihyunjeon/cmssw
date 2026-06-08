import FWCore.ParameterSet.Config as cms
from Configuration.Eras.Era_Phase2C17I13M9_cff import Phase2C17I13M9

'''
Configuration file to run packer/unpacker for HL-LHC inner tracker
Packer : PixelToBitStreamProducer & BitStreamToRawProducer
Unpacker : RawToBitStreamProducer & BitStreamToPixelProducer
'''

process = cms.Process('USER', Phase2C17I13M9)

# Standard configurations
process.load('Configuration.StandardSequences.Services_cff')
process.load('SimGeneral.HepPDTESSource.pythiapdt_cfi')
process.load('FWCore.MessageService.MessageLogger_cfi')
process.load('Configuration.EventContent.EventContent_cff')
process.load('SimGeneral.MixingModule.mixNoPU_cfi')
process.load("Configuration.Geometry.GeometryExtendedRun4D102Reco_cff")
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
from Configuration.AlCa.GlobalTag import GlobalTag
process.GlobalTag = GlobalTag(process.GlobalTag, 'auto:phase2_realistic', '')


process.maxEvents = cms.untracked.PSet(
    input = cms.untracked.int32(1)
)

process.source = cms.Source("PoolSource",
    fileNames = cms.untracked.vstring("root://cmseos.fnal.gov:1094//eos/uscms/store/user/laceyd/3be594c4-9067-4ee7-b2c7-39f8894328e2.root")
)

process.load("CondCore.CondDB.CondDB_cfi")
process.CondDB.connect = "sqlite_file:OTandITDTCCablingMap.db"
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

process.PixelToBitStreamProducer = cms.EDProducer(
    'PixelToBitStreamProducer',
    src = cms.InputTag("generalTracks"),
    siPixelDigi = cms.InputTag("simSiPixelDigis", "Pixel")
)

process.BitStreamToRawProducer = cms.EDProducer(
    'BitStreamToRawProducer',
    Phase2ITChipBitStream = cms.InputTag("PixelToBitStreamProducer")
)

process.RawToBitStreamProducer = cms.EDProducer(
    'RawToBitStreamProducer',
    fedRawDataCollection = cms.InputTag("BitStreamToRawProducer"),
    debug = cms.untracked.bool(False)
)

process.BitStreamToPixelProducer = cms.EDProducer(
    'BitStreamToPixelProducer',
    phase2ItChipBitStream = cms.InputTag("RawToBitStreamProducer")
)

process.load("RecoTracker.TrackProducer.TrackRefitters_cff")
process.TrackRefitter.src = "generalTracks"
process.TrackRefitter.NavigationSchool = ""

process.FEVTDEBUGoutput = cms.OutputModule("PoolOutputModule",
    fileName = cms.untracked.string('output_file.root'),
    outputCommands = cms.untracked.vstring(
        'drop *',
        #'keep RawDataBuffer_*_*_*',
        #'keep Phase2IT*_*_*_*',  # Save intermediate Phase2ITChipBitStream
        #'keep PixelDigi*_*_*_*'
    )
)

process.digitisation_step = cms.Path(process.pdigi_valid)
process.user_step = cms.Path(
    process.PixelToBitStreamProducer
    * process.BitStreamToRawProducer
    * process.RawToBitStreamProducer
    * process.BitStreamToPixelProducer
)
process.endjob_step = cms.EndPath(process.endOfProcess)
process.output_step = cms.EndPath(process.FEVTDEBUGoutput)

process.schedule = cms.Schedule(process.user_step, process.endjob_step, process.output_step)

from FWCore.Modules.logErrorHarvester_cff import customiseLogErrorHarvesterUsingOutputCommands
process = customiseLogErrorHarvesterUsingOutputCommands(process)
from Configuration.StandardSequences.earlyDeleteSettings_cff import customiseEarlyDelete
process = customiseEarlyDelete(process)

