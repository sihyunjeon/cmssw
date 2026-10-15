# Harvest the s-link DQMIO: occupancy summaries plus rendered pdf/png via TrackerPhase2PlotUtil

import os
import re
import FWCore.ParameterSet.Config as cms
import FWCore.ParameterSet.VarParsing as VarParsing

options = VarParsing.VarParsing('analysis')
options.register('inputDQM', 'file:slink_all.root',
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.string,
                 'DQMIO file(s)')
options.register('plotDir', 'pdf_slink_all',
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.string,
                 'Directory for the rendered pdf/png files')
options.register('outputTag', '',
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.string,
                 'Tag of the harvested file DQM_V0001_R*__Phase2IT__SLink__<outputTag>.root (default: input file name)')
options.register('dataSizeYMax', 0.,
                 VarParsing.VarParsing.multiplicity.singleton,
                 VarParsing.VarParsing.varType.float,
                 'y-axis maximum of the per-DTC data size plots (default: automatic)')
options.parseArguments()

_inputName = options.inputDQM.split(',')[0]
_inputName = _inputName[len('file:'):] if _inputName.startswith('file:') else _inputName
_outputTag = re.sub(r'[^A-Za-z0-9_-]', '_',
                    options.outputTag or os.path.splitext(os.path.basename(_inputName))[0])

process = cms.Process('HARVEST')
process.load('FWCore.MessageService.MessageLogger_cfi')
process.load('DQMServices.Core.DQMStore_cfi')
process.load('Configuration.StandardSequences.DQMSaverAtRunEnd_cff')
process.dqmSaver.workflow = '/Phase2IT/SLink/' + _outputTag

process.maxEvents = cms.untracked.PSet(input=cms.untracked.int32(-1))
_inputFiles = [f if '://' in f or f.startswith('file:') or f.startswith('/store/') else 'file:' + f
               for f in options.inputDQM.split(',') if f]
process.source = cms.Source('DQMRootSource', fileNames=cms.untracked.vstring(_inputFiles))

process.phase2ITHarvestSLink = cms.EDProducer('Phase2ITHarvestSLink',
    TopFolder        = cms.string('InnerTrackerV/SLink'),
    OccupancyMapName = cms.string('slinkOccupancyMap'),
    savePlots     = cms.untracked.bool(True),
    plotDir       = cms.untracked.string(options.plotDir),
    plotFormats   = cms.untracked.vstring('pdf', 'png'),
    plotZMax      = cms.untracked.double(1.6),
    dataSizeYMax  = cms.untracked.double(options.dataSizeYMax),
    occupancyAvg = cms.PSet(
        switch = cms.bool(True),
        name   = cms.string('slinkOccupancyAvg'),
        title  = cms.string('Event-Averaged SLink Occupancy;<Occupancy>;SLink entries'),
        NxBins = cms.int32(32),
        xmin   = cms.double(0.),
        xmax   = cms.double(1.6),
    ),
)

process.harvest_step = cms.Path(process.phase2ITHarvestSLink)
process.dqmsave_step = cms.Path(process.DQMSaver)
process.schedule = cms.Schedule(process.harvest_step, process.dqmsave_step)
process.options.numberOfThreads = cms.untracked.uint32(1)
