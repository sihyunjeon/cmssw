# Harvest the e-link DQMIO: occupancy summaries plus rendered pdf/png via TrackerPhase2PlotUtil

import os
import re
import FWCore.ParameterSet.Config as cms
import FWCore.ParameterSet.VarParsing as VarParsing

options = VarParsing.VarParsing('analysis')
options.register('inputDQM', 'file:elink_all.root',
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.string,
                 'DQMIO file(s)')
options.register('plotDir', 'pdf_elink_all',
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.string,
                 'Directory for the rendered pdf/png files')
options.register('referenceFile', '',
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.string,
                 'Harvested DQM file to compare against; enables the delta plot')
options.register('outputTag', '',
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.string,
                 'Tag of the harvested file DQM_V0001_R*__Phase2IT__ELink__<outputTag>.root (default: input file name)')
options.register('stackedYMax', 0.,
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.float,
                 'y-axis maximum of the stacked occupancy plots (default : automatic)')
options.parseArguments()

_inputName = options.inputDQM.split(',')[0]
_inputName = _inputName[len('file:'):] if _inputName.startswith('file:') else _inputName
_outputTag = re.sub(r'[^A-Za-z0-9_-]', '_',
                    options.outputTag or os.path.splitext(os.path.basename(_inputName))[0])
if options.referenceFile and \
        os.path.basename(options.referenceFile).endswith('__Phase2IT__ELink__' + _outputTag + '.root') and \
        os.path.realpath(os.path.dirname(os.path.abspath(options.referenceFile))) == os.path.realpath(os.getcwd()):
    raise RuntimeError('This harvest would overwrite referenceFile ' + options.referenceFile +
                       '; run it elsewhere or pass a different `outputTag=`')

process = cms.Process('HARVEST')
process.load('FWCore.MessageService.MessageLogger_cfi')
process.load('DQMServices.Core.DQMStore_cfi')
process.load('Configuration.StandardSequences.DQMSaverAtRunEnd_cff')
process.dqmSaver.workflow = '/Phase2IT/ELink/' + _outputTag

process.maxEvents = cms.untracked.PSet(input=cms.untracked.int32(-1))
_inputFiles = [f if '://' in f or f.startswith('file:') or f.startswith('/store/') else 'file:' + f
               for f in options.inputDQM.split(',') if f]
process.source = cms.Source('DQMRootSource', fileNames=cms.untracked.vstring(_inputFiles))

process.phase2ITHarvestELink = cms.EDProducer('Phase2ITHarvestELink',
    TopFolder        = cms.string('InnerTrackerV/ELink'),
    OccupancyMapName = cms.string('eLinkOccupancyMap'),
    savePlots     = cms.untracked.bool(True),
    plotDir       = cms.untracked.string(options.plotDir),
    plotFormats   = cms.untracked.vstring('pdf', 'png'),
    plotZMax      = cms.untracked.double(1.6),
    stackedYMax   = cms.untracked.double(options.stackedYMax),
    referenceFile = cms.untracked.string(options.referenceFile),
    occupancyAvg = cms.PSet(
        switch = cms.bool(True),
        name   = cms.string('eLinkOccupancyAvg'),
        title  = cms.string('Event-Averaged ELink Occupancy;<Occupancy>;ELink entries'),
        NxBins = cms.int32(32),
        xmin   = cms.double(0.),
        xmax   = cms.double(1.6),
    ),
)

process.harvest_step = cms.Path(process.phase2ITHarvestELink)
process.dqmsave_step = cms.Path(process.DQMSaver)
process.schedule = cms.Schedule(process.harvest_step, process.dqmsave_step)
process.options.numberOfThreads = cms.untracked.uint32(1)
