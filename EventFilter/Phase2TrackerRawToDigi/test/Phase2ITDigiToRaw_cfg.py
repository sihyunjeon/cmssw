import FWCore.ParameterSet.Config as cms


from Configuration.Eras.Era_Phase2C17I13M9_cff import Phase2C17I13M9
process = cms.Process('USER',Phase2C17I13M9)

# import of standard configurations
process.load('Configuration.StandardSequences.Services_cff')
process.load('SimGeneral.HepPDTESSource.pythiapdt_cfi')
process.load('FWCore.MessageService.MessageLogger_cfi')
process.load('Configuration.EventContent.EventContent_cff')
process.load('SimGeneral.MixingModule.mixNoPU_cfi')
#process.load('SimGeneral.MixingModule.mix_POISSON_average_cfi')
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
        "file:/eos/cms/store/relval/CMSSW_13_1_0_pre3/RelValTTbar_14TeV/GEN-SIM-DIGI-RAW/PU_131X_mcRun4_realistic_v2_PDMVRELVALS146-v6/2580000/0147ae96-1577-4b6f-9ef7-e605461931e6.root"
    )
)


process.options = cms.untracked.PSet(

)

# Production Info
process.configurationMetadata = cms.untracked.PSet(
    annotation = cms.untracked.string('step2 nevts:10'),
    name = cms.untracked.string('Applications'),
    version = cms.untracked.string('$Revision: 1.19 $')
)

# MC vertice analyzer
process.load("Validation.RecoVertex.mcverticesanalyzer_cfi")
process.mcverticesanalyzer.pileupSummaryCollection = cms.InputTag("addPileupInfo","","HLT")

process.PixelQCore = cms.EDProducer('PixelQCoreProducer', src=cms.InputTag('generalTracks'), siPixelDigi = cms.InputTag("simSiPixelDigis", "Pixel"))


# # # -- Trajectory producer
process.load("RecoTracker.TrackProducer.TrackRefitters_cff")
process.TrackRefitter.src = 'generalTracks'
process.TrackRefitter.NavigationSchool = ""

# Additional output definition

# Other statements
process.mix.digitizers = cms.PSet(process.theDigitizersValid)

# This pset is specific for producing simulated events for the designers of the PROC (InnerTracker)
# They need pixel RecHits where the charge is stored with high-granularity and large dinamic range

from Configuration.AlCa.GlobalTag import GlobalTag
process.GlobalTag = GlobalTag(process.GlobalTag, 'auto:phase2_realistic_T30', '')

# Path and EndPath definitions
process.digitisation_step = cms.Path(process.pdigi_valid)
#process.L1simulation_step = cms.Path(process.SimL1Emulator)
#process.L1TrackTrigger_step = cms.Path(process.L1TrackTrigger)
process.digi2raw_step = cms.Path(process.DigiToRaw)
process.raw2digi_step = cms.Path(process.RawToDigi)
#process.L1Reco_step = cms.Path(process.L1Reco)
#process.reconstruction_step = cms.Path(process.reconstruction)
#process.user_step = cms.Path(process.TrackRefitter * process.PixelQCore)
process.user_step = cms.Path(process.PixelQCore)
process.endjob_step = cms.EndPath(process.endOfProcess)
#process.FEVTDEBUGHLToutput_step = cms.EndPath(process.FEVTDEBUGHLToutput)

# Schedule definition
#process.schedule = cms.Schedule(process.digitisation_step, process.endjob_step)
#process.schedule = cms.Schedule(process.digitisation_step,process.L1simulation_step,process.L1TrackTrigger_step,process.digi2raw_step)
#process.schedule = cms.Schedule(process.digitisation_step, process.digi2raw_step, process.raw2digi_step)
process.schedule.extend([process.user_step, process.endjob_step])

process.TFileService = cms.Service('TFileService',
    fileName = cms.string("output.root")
)
