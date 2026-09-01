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
opts.register('blockSize', 0, VarParsing.VarParsing.multiplicity.singleton,
              VarParsing.VarParsing.varType.int,
              'threads per block for the alpaka kernels, 0 keeps each producer default. '
              'Pass "<stage1>,<stage2>" via blockSize1/blockSize2 to tune them apart.')
opts.register('blockSize1', 0, VarParsing.VarParsing.multiplicity.singleton,
              VarParsing.VarParsing.varType.int, 'block size for the stage-1 (per module) kernels')
opts.register('blockSize2', 0, VarParsing.VarParsing.multiplicity.singleton,
              VarParsing.VarParsing.varType.int, 'block size for the stage-2 (per chip) kernels')
opts.register('recovery', 0, VarParsing.VarParsing.multiplicity.singleton,
              VarParsing.VarParsing.varType.int,
              'also compare the digis fed to the packer against what each of the three '
              'flows gives back, as pixel occupancy maps plus a digi-by-digi tally')
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
# inputFiles overrides the default sample, e.g. a local copy for timing scans
process.source = cms.Source('PoolSource',
    fileNames=cms.untracked.vstring(
        'root://cms-xrd-global.cern.ch//store/relval/CMSSW_14_1_0_pre3/RelValTTbar_14TeV/GEN-SIM-DIGI-RAW/PU_140X_mcRun4_realistic_v3_SpecialRV296_Run4D112-v1/2590000/0060c957-0236-4b79-abe3-8410dec69b26.root',
    ),
)
if opts.inputFiles:
    process.source.fileNames = cms.untracked.vstring(opts.inputFiles)

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
    dropTot=cms.bool(_dropTot),
    handleGapPixels=cms.string(opts.gapMode),
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
    dropTot=cms.bool(_dropTot),
    handleGapPixels=cms.string(opts.gapMode),
)

# Fused unpacker: raw -> digi in one step, through the same shared walk and
# decode (Phase2ITUnpacker) as the split chain above.
process.rawToPixelProducer = cms.EDProducer('RawToPixelProducer',
    fedRawDataCollection=cms.InputTag('BitStreamToRawProducer'),
    dropTot=cms.bool(_dropTot),
    handleGapPixels=cms.string(opts.gapMode),
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
    dropTot=cms.bool(_dropTot),
    handleGapPixels=cms.string(opts.gapMode),
)

# Block size is a pure performance knob: it changes how the work is spread over the
# device, never the digis that come out. Left at the producer default unless asked.
_bs1 = opts.blockSize1 or opts.blockSize
_bs2 = opts.blockSize2 or opts.blockSize
if _bs1:
    process.phase2ITRawToBitStream.blockSize = cms.uint32(_bs1)
if _bs2:
    process.phase2ITBitStreamToPixel.blockSize = cms.uint32(_bs2)

process.phase2ITDigiCompare = cms.EDAnalyzer('Phase2ITDigiCompare',
    legacy=cms.InputTag('bitstreamToPixelProducer'),
    soa=cms.InputTag('phase2ITBitStreamToPixel'),
)
# Same comparison for the fused flow, so both CPU paths are held to the SoA digis.
process.phase2ITFusedCompare = cms.EDAnalyzer('Phase2ITDigiCompare',
    legacy=cms.InputTag('rawToPixelProducer'),
    soa=cms.InputTag('phase2ITBitStreamToPixel'),
)

# Recovery: each flow against the digis the packer was given, rather than against
# each other. Every analyzer writes its own TDirectory, named after its label.
_recovery = cms.EDAnalyzer('Phase2ITDigiRecovery', digis=cms.InputTag('simSiPixelDigis', 'Pixel'))
process.recoveryLegacy = _recovery.clone(unpacked=cms.InputTag('bitstreamToPixelProducer'))
process.recoveryFused = _recovery.clone(unpacked=cms.InputTag('rawToPixelProducer'))
process.recoveryAlpaka = _recovery.clone(unpackedSoA=cms.InputTag('phase2ITBitStreamToPixel'))

# Only the comparison analyzer fills it, so it is created only when that analyzer
# runs. A pure timing job then writes no ROOT file at all, which keeps the job off
# the filesystem entirely -- writing it was a way to fail at shutdown for nothing.
if opts.timing != 1:
    _stem = 'phase2ITDigiRecovery' if opts.recovery else 'phase2ITDigiCompare'
    process.TFileService = cms.Service(
        'TFileService',
        fileName=cms.string('%s_%s%s.root' % (_stem, opts.gapMode, '_dropTot' if _dropTot else '')))

_chain = (process.PixelToBitStreamProducer
          * process.BitStreamToRawProducer
          * process.rawToBitStreamProducer
          * process.bitstreamToPixelProducer
          * process.phase2ITRawToBitStream
          * process.phase2ITBitStreamToPixel
          * process.rawToPixelProducer)
# The comparison walks every digi of both collections, so it would dominate any
# timing measurement; drop it and ask for the per-module TimeReport instead.
if opts.timing:
    process.options.wantSummary = cms.untracked.bool(True)
    process.MessageLogger.cerr.FwkReport.reportEvery = 100
if opts.timing != 1:
    # The analyzer consumes SiPixelDigisHost, which is what pulls the digi SoA
    # back off the device; without it the transfer never happens at all.
    _chain = _chain * process.phase2ITDigiCompare * process.phase2ITFusedCompare
    if opts.recovery:
        _chain = _chain * process.recoveryLegacy * process.recoveryFused * process.recoveryAlpaka
process.p = cms.Path(_chain)

process.options.numberOfThreads = cms.untracked.uint32(opts.threads)
process.options.numberOfStreams = cms.untracked.uint32(opts.threads)
if opts.accelerator != 'auto':
    process.options.accelerators = cms.untracked.vstring(opts.accelerator)
