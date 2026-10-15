import FWCore.ParameterSet.Config as cms

from EventFilter.Phase2PixelRawToDigi.phase2ITPacker_cff import *
from Validation.SiTrackerPhase2V.Phase2ITValidateELink_cfi import Phase2ITValidateELink
from Validation.SiTrackerPhase2V.Phase2ITValidateSLink_cfi import Phase2ITValidateSLink

# Linear corrections for the unsimulated effects: afterglow (7%) everywhere plus the luminosity triggers (10%) in TEPX
_unsimulatedScales = dict(scaleTBPX = 1.07, scaleTFPX = 1.07, scaleTEPX = 1.17)
elinkValidIT = Phase2ITValidateELink.clone(**_unsimulatedScales)
slinkValidIT = Phase2ITValidateSLink.clone(**_unsimulatedScales)

itLinkValidation = cms.Sequence(phase2ITPacker + elinkValidIT + slinkValidIT)
