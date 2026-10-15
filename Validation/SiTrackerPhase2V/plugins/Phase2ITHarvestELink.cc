// -*- C++ -*-
// Package:    Validation/SiTrackerPhase2V
// Class:      Phase2ITHarvestELink
// Description: Harvest the per-elink occupancy DQM
//
// Author: Lacey Dishman, Sihyun Jeon (Boston University)
// Written: August 2026

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "DQMServices/Core/interface/DQMEDHarvester.h"
#include "DQMServices/Core/interface/DQMStore.h"
#include "DQM/SiTrackerPhase2/interface/TrackerPhase2HarvestingUtil.h"
#include "Validation/SiTrackerPhase2V/plugins/TrackerPhase2PlotUtil.h"

#include "TColor.h"
#include "TFile.h"
#include "TH1F.h"
#include "TProfile2D.h"

namespace {
  const std::vector<std::pair<std::string, std::string>> kSectionColors = {{"TBPX_L1", "#000080"},
                                                                           {"TBPX_L2", "#0000FF"},
                                                                           {"TBPX_L3", "#6666FF"},
                                                                           {"TBPX_L4", "#87CEEB"},
                                                                           {"TFPX_R1", "#800000"},
                                                                           {"TFPX_R2", "#FF0000"},
                                                                           {"TFPX_R3", "#FF8000"},
                                                                           {"TFPX_R4", "#FFCC00"},
                                                                           {"TEPX_R1", "#003300"},
                                                                           {"TEPX_R2", "#00AA00"},
                                                                           {"TEPX_R3", "#33EE33"},
                                                                           {"TEPX_R4", "#B0B0B0"},
                                                                           {"TEPX_R5", "#DDDDDD"}};
}  // namespace

class Phase2ITHarvestELink : public DQMEDHarvester {
public:
  explicit Phase2ITHarvestELink(const edm::ParameterSet&);
  ~Phase2ITHarvestELink() override;
  void dqmEndJob(DQMStore::IBooker& ibooker, DQMStore::IGetter& igetter) override;

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

private:
  TProfile2D* sectionMap(DQMStore::IGetter& igetter, const std::string& section) const;

  const edm::ParameterSet config_;
  const std::string topFolder_;
  const std::string occupancyMapName_;
  const bool savePlots_;
  const std::string plotDir_;
  const std::vector<std::string> plotFormats_;
  const double zMax_;
  const double stackedYMax_;
  // Harvested DQM file to compare and draw the delta plots
  const std::string referenceFile_;
  const int deltaBins_;
  const double deltaMin_;
  const double deltaMax_;
};

Phase2ITHarvestELink::Phase2ITHarvestELink(const edm::ParameterSet& iConfig)
    : config_(iConfig),
      topFolder_(iConfig.getParameter<std::string>("TopFolder")),
      occupancyMapName_(iConfig.getParameter<std::string>("OccupancyMapName")),
      savePlots_(iConfig.getUntrackedParameter<bool>("savePlots", false)),
      plotDir_(iConfig.getUntrackedParameter<std::string>("plotDir", ".")),
      plotFormats_(iConfig.getUntrackedParameter<std::vector<std::string>>("plotFormats", {"png", "pdf"})),
      zMax_(iConfig.getUntrackedParameter<double>("plotZMax", 1.6)),
      stackedYMax_(iConfig.getUntrackedParameter<double>("stackedYMax", 0.)),
      referenceFile_(iConfig.getUntrackedParameter<std::string>("referenceFile", "")),
      deltaBins_(iConfig.getUntrackedParameter<int>("deltaBins", 80)),
      deltaMin_(iConfig.getUntrackedParameter<double>("deltaMin", -0.45)),
      deltaMax_(iConfig.getUntrackedParameter<double>("deltaMax", 0.15)) {}

Phase2ITHarvestELink::~Phase2ITHarvestELink() {}

TProfile2D* Phase2ITHarvestELink::sectionMap(DQMStore::IGetter& igetter, const std::string& section) const {
  MonitorElement* me = igetter.get(topFolder_ + "/eLinkOccupancyMapPerSection_" + section);
  return (me == nullptr) ? nullptr : me->getTProfile2D();
}

void Phase2ITHarvestELink::dqmEndJob(DQMStore::IBooker& ibooker, DQMStore::IGetter& igetter) {
  MonitorElement* occMap = igetter.get(topFolder_ + "/" + occupancyMapName_);
  if (occMap == nullptr) {
    edm::LogWarning("Phase2ITHarvestELink") << occupancyMapName_ << " not found in " << topFolder_;
    return;
  }
  TProfile2D* prof = occMap->getTProfile2D();

  MonitorElement* nGroupME = igetter.get(topFolder_ + "/nStreamGroups");
  const double nGroups = (nGroupME != nullptr) ? nGroupME->getTH1F()->GetBinContent(1) : 0.;
  if (nGroups <= 0) {
    edm::LogWarning("Phase2ITHarvestELink")
        << "nStreamGroups missing or empty in " << topFolder_ << "; skipping all normalization.";
  }

  // Normalize the raw 1D occupancy to per stream group
  MonitorElement* elinkOcc = igetter.get(topFolder_ + "/eLinkOccupancy");
  if (elinkOcc != nullptr && nGroups > 0) {
    elinkOcc->getTH1F()->Scale(1.0 / nGroups);
    elinkOcc->getTH1F()->SetOption("HIST");
    elinkOcc->setAxisTitle("ELink entries / stream group", 2);
  }

  // Normalization for per-section / per-subtype
  for (auto* me : igetter.getAllContents(topFolder_)) {
    if (me == nullptr)
      continue;
    if (me->getName().rfind("eLinkOccupancyPer", 0) == 0 && nGroups > 0) {
      me->getTH1F()->Scale(1.0 / nGroups);
      me->getTH1F()->SetOption("HIST");
      me->setAxisTitle("ELink entries / stream group", 2);
    }
  }

  MonitorElement* occVsSection = igetter.get(topFolder_ + "/eLinkOccupancyVsSection");
  if (occVsSection != nullptr && nGroups > 0) {
    occVsSection->getTH2F()->Scale(1.0 / nGroups);
    occVsSection->getTH2F()->SetOption("COLZ");
    occVsSection->setAxisTitle("ELink entries / stream group", 3);
  }

  MonitorElement* occVsSubType = igetter.get(topFolder_ + "/eLinkOccupancyVsSubType");
  if (occVsSubType != nullptr && nGroups > 0) {
    occVsSubType->getTH2F()->Scale(1.0 / nGroups);
    occVsSubType->getTH2F()->SetOption("COLZ");
    occVsSubType->setAxisTitle("ELink entries / stream group", 3);
  }

  // Event-averaged 1D occupancy, one entry per ELink
  ibooker.cd();
  ibooker.setCurrentFolder(topFolder_);
  const edm::ParameterSet avgPSet = config_.getParameter<edm::ParameterSet>("occupancyAvg");
  MonitorElement* occAvg = phase2tkharvestutil::book1DFromPSet(avgPSet, ibooker);
  if (occAvg != nullptr) {
    const TAxis& avgAxis = *occAvg->getTH1F()->GetXaxis();
    for (int ix = 1; ix <= prof->GetNbinsX(); ix++) {
      for (int iy = 1; iy <= prof->GetNbinsY(); iy++) {
        if (prof->GetBinEntries(prof->GetBin(ix, iy)) > 0)
          occAvg->Fill(TrackerPhase2PlotUtil::clampToAxis(prof->GetBinContent(ix, iy), avgAxis));
      }
    }
  }

  // The same per detector section with the binning of occupancyAvg
  const int nSections = static_cast<int>(kSectionColors.size());
  MonitorElement* avgBySection = ibooker.book1D("eLinkOccupancyAvgBySection",
                                                "Event-averaged ELink occupancy per section;Section;<occupancy>",
                                                nSections,
                                                -0.5,
                                                nSections - 0.5);
  std::vector<std::pair<std::string, const TH1*>> components;
  for (int i = 0; i < nSections; ++i) {
    const auto& [section, color] = kSectionColors[i];
    avgBySection->setBinLabel(i + 1, section, 1);
    TProfile2D* secProf = sectionMap(igetter, section);
    if (secProf == nullptr)
      continue;
    MonitorElement* me = TrackerPhase2PlotUtil::book1DLike(avgPSet,
                                                           ibooker,
                                                           "eLinkOccupancyAvgPerSection_" + section,
                                                           TrackerPhase2PlotUtil::titleWithSuffix(avgPSet, section));
    me->getTH1F()->SetLineColor(TColor::GetColor(color.c_str()));
    double sum = 0.;
    double sum2 = 0.;
    int n = 0;
    for (int ix = 1; ix <= secProf->GetNbinsX(); ix++) {
      for (int iy = 1; iy <= secProf->GetNbinsY(); iy++) {
        if (secProf->GetBinEntries(secProf->GetBin(ix, iy)) <= 0)
          continue;
        const double occupancy = secProf->GetBinContent(ix, iy);
        me->Fill(TrackerPhase2PlotUtil::clampToAxis(occupancy, *me->getTH1F()->GetXaxis()));
        sum += occupancy;
        sum2 += occupancy * occupancy;
        ++n;
      }
    }
    if (n > 0) {
      const double mean = sum / n;
      avgBySection->setBinContent(i + 1, mean);
      avgBySection->setBinError(i + 1, std::sqrt(std::max(0., sum2 / n - mean * mean)));
    }
    components.emplace_back(section, me->getTH1F());
  }

  // Relative difference against a reference run, one entry per ELink
  std::vector<std::pair<std::string, const TH1*>> deltaComponents;
  if (!referenceFile_.empty()) {
    std::unique_ptr<TFile> refFile(TFile::Open(referenceFile_.c_str(), "READ"));
    if (refFile == nullptr || refFile->IsZombie())
      throw cms::Exception("Phase2ITHarvestELink") << "referenceFile '" << referenceFile_ << "' cannot be opened";

    for (const auto& [section, color] : kSectionColors) {
      TProfile2D* cur = sectionMap(igetter, section);
      std::unique_ptr<TH1> refHist =
          TrackerPhase2PlotUtil::readHistFromDQMFile(*refFile, topFolder_, "eLinkOccupancyMapPerSection_" + section);
      TProfile2D* ref = dynamic_cast<TProfile2D*>(refHist.get());
      if (cur == nullptr || ref == nullptr)
        continue;
      // Bins are paired by index, so the elink and quadrant layout must match the reference
      if (cur->GetNbinsX() != ref->GetNbinsX() || cur->GetNbinsY() != ref->GetNbinsY()) {
        edm::LogWarning("Phase2ITHarvestELink")
            << "eLinkOccupancyMapPerSection_" << section << " binning " << cur->GetNbinsX() << "x" << cur->GetNbinsY()
            << " differs from the reference " << ref->GetNbinsX() << "x" << ref->GetNbinsY()
            << "; section not compared";
        continue;
      }
      MonitorElement* me = ibooker.book1D(
          "eLinkOccupancyDeltaPerSection_" + section,
          "Relative ELink occupancy difference, " + section + ";(this #minus reference) / reference;ELinks",
          deltaBins_,
          deltaMin_,
          deltaMax_);
      me->getTH1F()->SetLineColor(TColor::GetColor(color.c_str()));
      for (int ix = 1; ix <= cur->GetNbinsX(); ix++) {
        for (int iy = 1; iy <= cur->GetNbinsY(); iy++) {
          const int bin = cur->GetBin(ix, iy);
          if (cur->GetBinEntries(bin) <= 0 || ref->GetBinEntries(bin) <= 0 || ref->GetBinContent(bin) <= 0.)
            continue;
          me->Fill(TrackerPhase2PlotUtil::clampToAxis(
              (cur->GetBinContent(bin) - ref->GetBinContent(bin)) / ref->GetBinContent(bin),
              *me->getTH1F()->GetXaxis()));
        }
      }
      deltaComponents.emplace_back(section, me->getTH1F());
    }
    if (deltaComponents.empty())
      throw cms::Exception("Phase2ITHarvestELink")
          << "referenceFile '" << referenceFile_ << "' has no per-section occupancy maps to compare against";
  }

  if (savePlots_) {
    TrackerPhase2PlotUtil::PlotConfig plotCfg;
    plotCfg.plotDir = plotDir_;
    plotCfg.formats = plotFormats_;
    plotCfg.occMapNamePrefix = "eLinkOccupancyMap";
    plotCfg.zMax = zMax_;
    plotCfg.stackedYMax = stackedYMax_;
    int nFiles = TrackerPhase2PlotUtil::saveFolderPlots(igetter, topFolder_, plotCfg);

    // The module x ELink map, one quadrant per row
    std::vector<std::pair<std::string, const TH1*>> quadPanels;
    for (int q = 1; q <= 4; ++q)
      if (MonitorElement* me = igetter.get(topFolder_ + "/eLinkOccupancyMap_Q" + std::to_string(q)))
        quadPanels.emplace_back("Q" + std::to_string(q), me->getTH1());
    nFiles += TrackerPhase2PlotUtil::savePanelPlots(quadPanels, "eLinkOccupancyMapByQuadrant", plotCfg);

    nFiles += TrackerPhase2PlotUtil::saveStackedPlots(components, "eLinkOccupancyAvgPerSection", plotCfg);
    if (!deltaComponents.empty())
      nFiles += TrackerPhase2PlotUtil::saveStackedPlots(deltaComponents, "eLinkOccupancyDeltaPerSection", plotCfg);

    edm::LogInfo("Phase2ITHarvestELink") << "wrote " << nFiles << " plot files to " << plotDir_;
  }
}

void Phase2ITHarvestELink::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<std::string>("TopFolder", "InnerTrackerV/ELink");
  desc.add<std::string>("OccupancyMapName", "eLinkOccupancyMap");
  desc.addUntracked<bool>("savePlots", false);
  desc.addUntracked<std::string>("plotDir", ".");
  desc.addUntracked<std::vector<std::string>>("plotFormats", {"png", "pdf"});
  desc.addUntracked<double>("plotZMax", 1.6);
  desc.addUntracked<double>("stackedYMax", 0.);
  desc.addUntracked<std::string>("referenceFile", "");
  desc.addUntracked<int>("deltaBins", 80);
  desc.addUntracked<double>("deltaMin", -0.45);
  desc.addUntracked<double>("deltaMax", 0.15);

  edm::ParameterSetDescription psd0;
  psd0.add<std::string>("name", "eLinkOccupancyAvg");
  psd0.add<std::string>("title", "Event-averaged ELink occupancy;occupancy;ELinks");
  psd0.add<int>("NxBins", 32);
  psd0.add<double>("xmax", 1.6);
  psd0.add<double>("xmin", 0.);
  psd0.add<bool>("switch", true);
  desc.add<edm::ParameterSetDescription>("occupancyAvg", psd0);

  descriptions.add("Phase2ITHarvestELink", desc);
}

DEFINE_FWK_MODULE(Phase2ITHarvestELink);
