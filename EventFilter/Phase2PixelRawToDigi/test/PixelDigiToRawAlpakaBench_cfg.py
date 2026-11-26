# PixelDigiToRawAlpakaProducer_cfg.py cut down for Phase2ITUnpackBench.sh. The benchmark imports a cfg without its
# arguments, so the cfg's options come from BENCH_OPTS, and the part to run from:
#   BENCH_PACK=<file>  the packer only, which writes its raw data to <file> uncompressed
#   BENCH_FLOW         otherwise cpu, alpaka or input (reading the raw data only), on BENCH_INPUT read BENCH_PASSES times
import os
import sys

import FWCore.ParameterSet.Config as cms

cfg = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'PixelDigiToRawAlpakaProducer_cfg.py')
argv = sys.argv
sys.argv = [cfg] + os.environ.get('BENCH_OPTS', '').split()
exec(compile(open(cfg).read(), cfg, 'exec'))
sys.argv = argv

if os.environ.get('BENCH_PACK'):
    process.p = cms.Path(process.PixelToBitStreamProducer * process.BitStreamToRawProducer)
    process.out = cms.OutputModule('PoolOutputModule',
        fileName=cms.untracked.string(os.environ['BENCH_PACK']),
        outputCommands=cms.untracked.vstring('drop *', 'keep *_BitStreamToRawProducer_*_*'),
        compressionLevel=cms.untracked.int32(0),
    )
    process.outPath = cms.EndPath(process.out)
else:
    # the raw data was written by a process of the same name
    process.setName_('BENCH')
    process.source.fileNames = cms.untracked.vstring([os.environ['BENCH_INPUT']] * int(os.environ.get('BENCH_PASSES', '1')))
    process.source.duplicateCheckMode = cms.untracked.string('noDuplicateCheck')
    flow = os.environ.get('BENCH_FLOW', 'alpaka')
    if flow == 'cpu':
        process.p = cms.Path(process.RawToPixelProducer)
    elif flow == 'alpaka':
        process.p = cms.Path(process.Phase2ITRawToBitStreamProducer * process.Phase2ITBitStreamToPixelProducer)
    elif flow == 'input':
        process.read = cms.EDAnalyzer('GenericConsumer', eventProducts=cms.untracked.vstring('BitStreamToRawProducer'))
        process.p = cms.Path(process.read)
    else:
        raise RuntimeError('BENCH_FLOW must be cpu, alpaka or input, not ' + flow)
    del process.PixelToBitStreamProducer, process.BitStreamToRawProducer
    process.MessageLogger.cerr.FwkReport.reportEvery = 1000
