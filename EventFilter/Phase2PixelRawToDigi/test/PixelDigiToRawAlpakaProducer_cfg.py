# Phase-2 IT packer/unpacker round-trip on Alpaka:
#   pdigi -> per-chip bitstream -> raw -> pdigi, unpacked both by RawToPixelProducer and by the Alpaka stages
#   (Phase2ITRawToBitStreamProducer -> Phase2ITBitStreamToPixelProducer)

import FWCore.ParameterSet.Config as cms
import FWCore.ParameterSet.VarParsing as VarParsing
from Configuration.Eras.Era_Phase2C17I13M9_cff import Phase2C17I13M9

options = VarParsing.VarParsing('analysis')
options.register('dropTot', 0,
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.int,
                 'Suppress the per-hit ToT field')
options.register('gapMode', 'DROP',
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.string,
                 'Gap pixels: DROP / KEEP / AGGREGATE')
options.register('accelerator', 'auto',
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.string,
                 'auto / gpu-nvidia / gpu-amd / cpu; anything but auto is enforced, with no fallback')
options.register('timing', 0,
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.int,
                 'Print the per-module TimeReport')
options.register('threads', 1,
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.int,
                 'Threads and concurrent events')
options.register('blockSize', 0,
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.int,
                 'Threads per block for both Alpaka stages; 0 keeps the producer default')
options.register('syncForTiming', 0,
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.int,
                 'Make each Alpaka module wait for the device at the end of acquire and produce, '
                 'so TimeReport charges the kernels to it; single thread only')
options.register('cablingDB', 'frontier://FrontierProd/CMS_CONDITIONS',
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.string,
                 'Connect string of the cabling-map DB (CondDB by default; sqlite_file:<path> for a private DB)')
options.register('cablingTag', 'TrackerDetToDTCELinkCablingMap__IT742__ITOnly',
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.string,
                 'Tag of the IT cabling map')
options.setDefault('maxEvents', 20)
options.parseArguments()
dropTot = bool(options.dropTot)

files = list(options.inputFiles) or [
    'root://cms-xrd-global.cern.ch//store/relval/CMSSW_20_1_0_pre1/RelValTTbar_14TeV/GEN-SIM-DIGI-RAW/'
    'PU_150X_mcRun4_realistic_v1_SpecialRV296_Run4D112-v1/2830000/5f358aba-be26-403b-a2ce-17c07b6b0717.root'
]

process = cms.Process('USER', Phase2C17I13M9)

process.load('Configuration.StandardSequences.Services_cff')
process.load('Configuration.StandardSequences.Accelerators_cff')
process.load('FWCore.MessageService.MessageLogger_cfi')
process.load('Configuration.Geometry.GeometryExtendedRun4D112Reco_cff')
process.load('Configuration.StandardSequences.MagneticField_cff')
process.load('Configuration.StandardSequences.FrontierConditions_GlobalTag_cff')

from Configuration.AlCa.GlobalTag import GlobalTag
process.GlobalTag = GlobalTag(process.GlobalTag, 'auto:phase2_realistic', '')

process.maxEvents = cms.untracked.PSet(input=cms.untracked.int32(options.maxEvents))
process.source = cms.Source('PoolSource', fileNames=cms.untracked.vstring(files))

# The Phase-2 GT does not carry an IT cabling map in the current class layout yet: take it from CondDB
process.GlobalTag.toGet.append(cms.PSet(
    record=cms.string('TrackerDetToDTCELinkCablingMapRcd'),
    tag=cms.string(options.cablingTag),
    connect=cms.string(options.cablingDB),
))
# The auto:phase2_realistic alignment belongs to another geometry, and the digis do not need it
process.trackerGeometry.applyAlignment = False

process.PixelToBitStreamProducer = cms.EDProducer('PixelToBitStreamProducer',
    src=cms.InputTag('generalTracks'),
    siPixelDigi=cms.InputTag('simSiPixelDigis', 'Pixel'),
    dropTot=cms.bool(dropTot),
    handleGapPixels=cms.string(options.gapMode),
)
process.BitStreamToRawProducer = cms.EDProducer('BitStreamToRawProducer',
    Phase2ITChipBitStream=cms.InputTag('PixelToBitStreamProducer'),
)
process.RawToPixelProducer = cms.EDProducer('RawToPixelProducer',
    fedRawDataCollection=cms.InputTag('BitStreamToRawProducer'),
    dropTot=cms.bool(dropTot),
    handleGapPixels=cms.string(options.gapMode),
)

# Alpaka: the backend is picked at run time, a GPU when one is visible, otherwise the serial CPU backend
process.Phase2ITModuleMapESProducer = cms.ESProducer('Phase2ITModuleMapESProducer@alpaka')
process.Phase2ITRawToBitStreamProducer = cms.EDProducer('Phase2ITRawToBitStreamProducer@alpaka',
    fedRawDataCollection=cms.InputTag('BitStreamToRawProducer'),
)
process.Phase2ITBitStreamToPixelProducer = cms.EDProducer('Phase2ITBitStreamToPixelProducer@alpaka',
    phase2ItModuleStream=cms.InputTag('Phase2ITRawToBitStreamProducer'),
    phase2ItChipStream=cms.InputTag('Phase2ITRawToBitStreamProducer'),
    phase2ItRawBytes=cms.InputTag('Phase2ITRawToBitStreamProducer'),
    dropTot=cms.bool(dropTot),
    handleGapPixels=cms.string(options.gapMode),
)
if options.blockSize:
    process.Phase2ITRawToBitStreamProducer.blockSize = cms.uint32(options.blockSize)
    process.Phase2ITBitStreamToPixelProducer.blockSize = cms.uint32(options.blockSize)
if options.syncForTiming:
    process.ProcessAcceleratorAlpaka.setSynchronize(True)

process.p = cms.Path(process.PixelToBitStreamProducer * process.BitStreamToRawProducer * process.RawToPixelProducer
                     * process.Phase2ITRawToBitStreamProducer * process.Phase2ITBitStreamToPixelProducer)

if options.timing:
    process.options.wantSummary = cms.untracked.bool(True)
    process.MessageLogger.cerr.FwkReport.reportEvery = 100
process.options.numberOfThreads = cms.untracked.uint32(options.threads)
process.options.numberOfStreams = cms.untracked.uint32(options.threads)
if options.accelerator != 'auto':
    process.options.accelerators = cms.untracked.vstring(options.accelerator)
