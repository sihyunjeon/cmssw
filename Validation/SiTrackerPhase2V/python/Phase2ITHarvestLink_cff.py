import FWCore.ParameterSet.Config as cms

# Harvesting of the Phase-2 IT e-link and s-link validation (Phase2ITValidateLink_cff)
from Validation.SiTrackerPhase2V.Phase2ITHarvestELink_cfi import Phase2ITHarvestELink
from Validation.SiTrackerPhase2V.Phase2ITHarvestSLink_cfi import Phase2ITHarvestSLink

Phase2ITELinkHarvesting = cms.Sequence(Phase2ITHarvestELink)
Phase2ITSLinkHarvesting = cms.Sequence(Phase2ITHarvestSLink)
