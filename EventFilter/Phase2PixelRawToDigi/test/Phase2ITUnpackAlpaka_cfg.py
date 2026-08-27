# Round-trip test for the Alpaka Phase-2 IT unpacker:
#   digis -> chip bitstreams -> RawDataBuffer -> two unpacking chains
#     legacy: RawToBitStreamProducer -> BitStreamToPixelProducer
#     alpaka: Phase2ITRawToBitStream  -> Phase2ITBitStreamToDigi
#   and a digi-by-digi comparison of the alpaka chain against legacy
#   (the job fails on any mismatch).
#
# The alpaka modules are declared '<name>@alpaka', so the backend is chosen at
# runtime: a GPU is used when one is visible, otherwise the serial CPU backend.
#   cmsRun Phase2ITUnpackAlpaka_cfg.py                        # whatever is available
#   cmsRun Phase2ITUnpackAlpaka_cfg.py accelerator=gpu-nvidia # require a GPU
#   cmsRun Phase2ITUnpackAlpaka_cfg.py accelerator=cpu        # require the CPU backend

import os
import FWCore.ParameterSet.Config as cms
import FWCore.ParameterSet.VarParsing as VarParsing

# encoding knobs, applied consistently to the encoder and both decoders
opts = VarParsing.VarParsing('analysis')
opts.register('dropTot', 0, VarParsing.VarParsing.multiplicity.singleton,
              VarParsing.VarParsing.varType.int, 'suppress the per-hit ToT field')
opts.register('gapMode', 'DROP', VarParsing.VarParsing.multiplicity.singleton,
              VarParsing.VarParsing.varType.string, 'DROP / KEEP / AGGREGATE')
opts.register('accelerator', 'auto', VarParsing.VarParsing.multiplicity.singleton,
              VarParsing.VarParsing.varType.string,
              'auto / gpu-nvidia / gpu-amd / cpu. Anything but auto is enforced, so the '
              'job fails instead of silently falling back to another backend.')
opts.register('timing', 0, VarParsing.VarParsing.multiplicity.singleton,
              VarParsing.VarParsing.varType.int,
              'drop the comparison analyzer and print the per-module TimeReport. '
              'timing=2 keeps the analyzer, so the device-to-host transfer of the '
              'digis is charged to the job instead of never happening.')
opts.register('threads', 1, VarParsing.VarParsing.multiplicity.singleton,
              VarParsing.VarParsing.varType.int, 'threads and concurrent events')
opts.parseArguments()
_dropTot = bool(opts.dropTot)
from Configuration.Eras.Era_Phase2C17I13M9_cff import Phase2C17I13M9

process = cms.Process('ITRAWTODIGI', Phase2C17I13M9)

process.load('Configuration.StandardSequences.Services_cff')
process.load('Configuration.StandardSequences.Accelerators_cff')
process.load('FWCore.MessageService.MessageLogger_cfi')
process.load('Configuration.Geometry.GeometryExtendedRun4D112Reco_cff')
process.load('Configuration.StandardSequences.MagneticField_cff')
process.load('Configuration.StandardSequences.FrontierConditions_GlobalTag_cff')
process.MessageLogger.cerr.FwkReport.reportEvery = 1

from Configuration.AlCa.GlobalTag import GlobalTag
process.GlobalTag = GlobalTag(process.GlobalTag, 'auto:phase2_realistic', '')

# maxEvents follows the VarParsing convention: -1, which is also the default,
# means every event in the file. Pass a small number for quick validation runs.
process.maxEvents = cms.untracked.PSet(input=cms.untracked.int32(opts.maxEvents))
process.source = cms.Source('PoolSource',
    fileNames=cms.untracked.vstring(
        'root://cms-xrd-global.cern.ch//store/relval/CMSSW_14_1_0_pre3/RelValTTbar_14TeV/GEN-SIM-DIGI-RAW/PU_140X_mcRun4_realistic_v3_SpecialRV296_Run4D112-v1/2590000/0060c957-0236-4b79-abe3-8410dec69b26.root',
    ),
)

# Local DTC cabling map (sqlite next to this cfg)
_dbpath = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'OTandITDTCCablingMap.db')
process.load('CondCore.CondDB.CondDB_cfi')
process.CondDB.connect = 'sqlite_file:' + _dbpath
process.PoolDBESSource = cms.ESSource('PoolDBESSource', process.CondDB,
    toGet=cms.VPSet(cms.PSet(
        record=cms.string('TrackerDetToDTCELinkCablingMapRcd'),
        tag=cms.string('DTCCablingMapProducerUserRun'),
    )),
)
process.es_prefer_local_cabling = cms.ESPrefer('PoolDBESSource', '')

# The auto:phase2_realistic alignment payloads correspond to a different
# default geometry; alignment is irrelevant for the digi-level comparison.
process.trackerGeometry.applyAlignment = False

# Pack
process.PixelToBitStreamProducer = cms.EDProducer('PixelToBitStreamProducer',
    src=cms.InputTag('generalTracks'),
    siPixelDigi=cms.InputTag('simSiPixelDigis', 'Pixel'),
    dropTot=cms.untracked.bool(_dropTot),
    handleGapPixels=cms.untracked.string(opts.gapMode),
)
process.BitStreamToRawProducer = cms.EDProducer('BitStreamToRawProducer',
    Phase2ITChipBitStream=cms.InputTag('PixelToBitStreamProducer'),
)

# Legacy unpacking chain
process.rawToBitStreamProducer = cms.EDProducer('RawToBitStreamProducer',
    fedRawDataCollection=cms.InputTag('BitStreamToRawProducer'),
)
process.bitstreamToPixelProducer = cms.EDProducer('BitStreamToPixelProducer',
    phase2ItChipBitStream=cms.InputTag('rawToBitStreamProducer'),
    dropTot=cms.untracked.bool(_dropTot),
    handleGapPixels=cms.untracked.string(opts.gapMode),
)

# Alpaka chain (explicit CPU serial backend)
# Flattens the cabling map and geometry into the tables the kernels index,
# once per IOV rather than on the first event.
process.phase2ITModuleMap = cms.ESProducer('Phase2ITModuleMapESProducer@alpaka')

process.phase2ITRawToBitStream = cms.EDProducer('Phase2ITRawToBitStreamProducer@alpaka',
    fedRawDataCollection=cms.InputTag("BitStreamToRawProducer"),
)
process.phase2ITBitStreamToPixel = cms.EDProducer('Phase2ITBitStreamToPixelProducer@alpaka',
    phase2ItChipBitStream=cms.InputTag("phase2ITRawToBitStream"),
    phase2ItRawBytes=cms.InputTag("phase2ITRawToBitStream"),
    dropTot=cms.untracked.bool(_dropTot),
    handleGapPixels=cms.untracked.string(opts.gapMode),
)

process.phase2ITDigiCompare = cms.EDAnalyzer('Phase2ITDigiCompare',
    legacy=cms.InputTag('bitstreamToPixelProducer'),
    soa=cms.InputTag('phase2ITBitStreamToPixel'),
)

process.TFileService = cms.Service('TFileService', fileName=cms.string('phase2ITDigiCompare_%s%s.root' % (opts.gapMode, '_dropTot' if _dropTot else '')))

_chain = (process.PixelToBitStreamProducer
          * process.BitStreamToRawProducer
          * process.rawToBitStreamProducer
          * process.bitstreamToPixelProducer
          * process.phase2ITRawToBitStream
          * process.phase2ITBitStreamToPixel)
# The comparison walks every digi of both collections, so it would dominate any
# timing measurement; drop it and ask for the per-module TimeReport instead.
if opts.timing:
    process.options.wantSummary = cms.untracked.bool(True)
    process.MessageLogger.cerr.FwkReport.reportEvery = 100
if opts.timing != 1:
    # The analyzer consumes SiPixelDigisHost, which is what pulls the digi SoA
    # back off the device; without it the transfer never happens at all.
    _chain = _chain * process.phase2ITDigiCompare
process.p = cms.Path(_chain)

process.options.numberOfThreads = cms.untracked.uint32(opts.threads)
process.options.numberOfStreams = cms.untracked.uint32(opts.threads)
if opts.accelerator != 'auto':
    process.options.accelerators = cms.untracked.vstring(opts.accelerator)
