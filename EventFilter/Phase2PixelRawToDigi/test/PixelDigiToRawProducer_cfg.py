import FWCore.ParameterSet.Config as cms
from Configuration.Eras.Era_Phase2C17I13M9_cff import Phase2C17I13M9

process = cms.Process('USER', Phase2C17I13M9)

# Standard configurations
process.load('Configuration.StandardSequences.Services_cff')
process.load('SimGeneral.HepPDTESSource.pythiapdt_cfi')
process.load('FWCore.MessageService.MessageLogger_cfi')
process.load('Configuration.EventContent.EventContent_cff')
process.load('SimGeneral.MixingModule.mixNoPU_cfi')
#process.load('Configuration.Geometry.GeometryExtended2026D91Reco_cff')
process.load('Configuration.Geometry.GeometryExtendedRun4D110Reco_cff')
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
#process.GlobalTag = GlobalTag(process.GlobalTag, 'auto:phase2_realistic', '')


process.maxEvents = cms.untracked.PSet(
    input = cms.untracked.int32(1)
)

process.source = cms.Source("PoolSource",
    #fileNames = cms.untracked.vstring("file:/eos/cms/store/group/dpg_tracker_upgrade/shjeon/RelValTTbar_14TeV_TuneCP5_CMSSW_15_1_0_pre5-PU_150X_mcRun4_realistic_v1_RV269_Run4D110_PU-v2/4f8965fd-fda6-414c-bc3e-92598ba7b251.root")
#    fileNames = cms.untracked.vstring("/store/relval/CMSSW_15_1_0_pre5/RelValTTbar_14TeV_TuneCP5/GEN-SIM-DIGI-RAW/PU_150X_mcRun4_realistic_v1_RV269_Run4D110_PU-v2/2590000/0f0bcfd3-dafe-4dda-8d39-9765f6eae68e.root")
     fileNames = cms.untracked.vstring("/store/relval/CMSSW_15_1_0_pre5/RelValDoubleMuFlatPt1p5To8/GEN-SIM-DIGI-RAW/150X_mcRun4_realistic_v1_RV269_Run4D110_noPU-v1/2590000/1172421f-823f-420f-8ec9-3de20dd6dda4.root")
#    fileNames = cms.untracked.vstring("file:/eos/cms/store/group/phase2tracker/IT/samples/RelValTTbar_14TeV__CMSSW_13_1_0_pre3-PU_131X_mcRun4_realistic_v2_PDMVRELVALS146-v7__GEN-SIM-DIGI-RAW/6aec09bc-1e00-4831-b90c-9b42254f627a.root")
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
        'keep FEDRawDataCollection_*_*_*',
        'keep *_Phase2IT*_*_*',  # Save intermediate Phase2ITChipBitStream
        'keep PixelDigi*_*_*_*'
    )
)

process.digitisation_step = cms.Path(process.pdigi_valid)
process.user_step = cms.Path(
    process.PixelToBitStreamProducer *
    process.BitStreamToRawProducer *
    process.RawToBitStreamProducer *
    process.BitStreamToPixelProducer
)
process.endjob_step = cms.EndPath(process.endOfProcess)
process.output_step = cms.EndPath(process.FEVTDEBUGoutput)

process.schedule = cms.Schedule(process.user_step, process.endjob_step, process.output_step)

from FWCore.Modules.logErrorHarvester_cff import customiseLogErrorHarvesterUsingOutputCommands
process = customiseLogErrorHarvesterUsingOutputCommands(process)
from Configuration.StandardSequences.earlyDeleteSettings_cff import customiseEarlyDelete
process = customiseEarlyDelete(process)

