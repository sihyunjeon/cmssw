// -*- C++ -*-
// Package:    Validation/SiTrackerPhase2V
// Class:      Phase2ITHarvestSLink
// Description: Harvest the per-slink occupancy DQM
//
// Author: Lacey Dishman, Sihyun Jeon (Boston University)
// Written: August 2026

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "DQMServices/Core/interface/DQMEDHarvester.h"
#include "DQMServices/Core/interface/DQMStore.h"
#include "DQM/SiTrackerPhase2/interface/TrackerPhase2HarvestingUtil.h"
#include "Validation/SiTrackerPhase2V/plugins/TrackerPhase2PlotUtil.h"

#include "TColor.h"
#include "TF1.h"
#include "TH1F.h"
#include "TProfile2D.h"

class Phase2ITHarvestSLink : public DQMEDHarvester {
public:
  explicit Phase2ITHarvestSLink(const edm::ParameterSet&);
  ~Phase2ITHarvestSLink() override;
  void dqmEndJob(DQMStore::IBooker& ibooker, DQMStore::IGetter& igetter) override;

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

private:
  const edm::ParameterSet config_;
  const std::string topFolder_;
  const std::string occupancyMapName_;
  const bool savePlots_;
  const std::string plotDir_;
  const std::vector<std::string> plotFormats_;
  const double zMax_;
  const double dataSizeYMax_;
};

Phase2ITHarvestSLink::Phase2ITHarvestSLink(const edm::ParameterSet& iConfig)
    : config_(iConfig),
      topFolder_(iConfig.getParameter<std::string>("TopFolder")),
      occupancyMapName_(iConfig.getParameter<std::string>("OccupancyMapName")),
      savePlots_(iConfig.getUntrackedParameter<bool>("savePlots", false)),
      plotDir_(iConfig.getUntrackedParameter<std::string>("plotDir", ".")),
      plotFormats_(iConfig.getUntrackedParameter<std::vector<std::string>>("plotFormats", {"png", "pdf"})),
      zMax_(iConfig.getUntrackedParameter<double>("plotZMax", 1.6)),
      dataSizeYMax_(iConfig.getUntrackedParameter<double>("dataSizeYMax", 0.)) {}

Phase2ITHarvestSLink::~Phase2ITHarvestSLink() {}

void Phase2ITHarvestSLink::dqmEndJob(DQMStore::IBooker& ibooker, DQMStore::IGetter& igetter) {
  MonitorElement* occMap = igetter.get(topFolder_ + "/" + occupancyMapName_);
  if (occMap == nullptr) {
    edm::LogWarning("Phase2ITHarvestSLink") << occupancyMapName_ << " not found in " << topFolder_;
    return;
  }
  TProfile2D* prof = occMap->getTProfile2D();

  MonitorElement* nEvtME = igetter.get(topFolder_ + "/nEvents");
  const double nEvents = (nEvtME != nullptr) ? nEvtME->getTH1F()->GetBinContent(1) : 0.;
  if (nEvents <= 0) {
    edm::LogWarning("Phase2ITHarvestSLink")
        << "nEvents missing or empty in " << topFolder_ << "; skipping all normalization.";
  }

  // Normalize the raw 1D occupancy to per-event
  MonitorElement* slinkOcc = igetter.get(topFolder_ + "/slinkOccupancy");
  if (slinkOcc != nullptr && nEvents > 0) {
    slinkOcc->getTH1F()->Scale(1.0 / nEvents);
    slinkOcc->getTH1F()->SetOption("HIST");
    slinkOcc->setAxisTitle("SLink entries / nevents", 2);
  }

  // Normalization for per-DTC full spectrum occupancy
  for (auto* me : igetter.getAllContents(topFolder_)) {
    if (me == nullptr)
      continue;
    if (me->getName().rfind("slinkSpectrumOccupancyPerDTC_", 0) == 0 && nEvents > 0) {
      me->getTH1F()->Scale(1.0 / nEvents);
      me->getTH1F()->SetOption("HIST");
      me->setAxisTitle("SLink entries / nevents", 2);
    }
  }

  MonitorElement* occVsDTC = igetter.get(topFolder_ + "/slinkOccupancyVsDTC");
  if (occVsDTC != nullptr && nEvents > 0) {
    occVsDTC->getTH2F()->Scale(1.0 / nEvents);
    occVsDTC->getTH2F()->SetOption("COLZ");
    occVsDTC->setAxisTitle("SLink entries / nevents", 3);
  }

  // Event-averaged 1D occupancy, one entry per SLink
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

  // The same per DTC, with the binning of occupancyAvg; the DTC numbers are the labels of the map's x axis
  std::vector<std::string> dtcs;
  std::vector<MonitorElement*> avgPerDTC;
  for (int ix = 1; ix <= prof->GetNbinsX(); ix++) {
    const std::string dtc = prof->GetXaxis()->GetBinLabel(ix);
    if (dtc.empty())
      continue;
    MonitorElement* me =
        TrackerPhase2PlotUtil::book1DLike(avgPSet,
                                          ibooker,
                                          "slinkOccupancyAvgPerDTC_" + dtc,
                                          TrackerPhase2PlotUtil::titleWithSuffix(avgPSet, "DTC " + dtc));
    for (int iy = 1; iy <= prof->GetNbinsY(); iy++) {
      if (prof->GetBinEntries(prof->GetBin(ix, iy)) > 0)
        me->Fill(TrackerPhase2PlotUtil::clampToAxis(prof->GetBinContent(ix, iy), *me->getTH1F()->GetXaxis()));
    }
    dtcs.push_back(dtc);
    avgPerDTC.push_back(me);
  }

  // Gaussian fit of the data size per event of each DTC: the mean [kb], with the width as error bar
  const int nDTCs = std::max(1, static_cast<int>(dtcs.size()));
  MonitorElement* dataSizeFit = ibooker.book1D(
      "dataSizeFitPerDTC", "Gaussian fit of the data size per event;DTC;mean data size [kb]", nDTCs, -0.5, nDTCs - 0.5);
  for (size_t i = 0; i < dtcs.size(); ++i) {
    dataSizeFit->setBinLabel(i + 1, dtcs[i], 1);
    MonitorElement* dataSize = igetter.get(topFolder_ + "/dataSizePerDTC_" + dtcs[i]);
    if (dataSize == nullptr)
      continue;
    if (auto fit = TrackerPhase2PlotUtil::fitGaussian(*dataSize->getTH1F())) {
      dataSizeFit->setBinContent(i + 1, fit->GetParameter(1));
      dataSizeFit->setBinError(i + 1, std::abs(fit->GetParameter(2)));
    }
  }

  if (savePlots_) {
    TrackerPhase2PlotUtil::PlotConfig plotCfg;
    plotCfg.plotDir = plotDir_;
    plotCfg.formats = plotFormats_;
    plotCfg.occMapNamePrefix = "slinkOccupancyMap";
    plotCfg.zMax = zMax_;
    plotCfg.gaussFitNamePrefix = "dataSizePerDTC";
    plotCfg.gaussFitLabelPrefix = "DTC ";
    plotCfg.gaussFitYMax = dataSizeYMax_;
    int nFiles = TrackerPhase2PlotUtil::saveFolderPlots(igetter, topFolder_, plotCfg);

    // Event-averaged occupancy per DTC, stacked per quadrant (DTC 11-19, ..., 41-49)
    std::map<int, std::vector<std::pair<std::string, TH1F*>>> byQuad;
    for (size_t i = 0; i < dtcs.size(); ++i)
      byQuad[std::stoi(dtcs[i]) / 10].emplace_back("DTC " + dtcs[i], avgPerDTC[i]->getTH1F());
    for (const auto& [decade, hists] : byQuad) {
      std::vector<std::pair<std::string, const TH1*>> components;
      for (size_t i = 0; i < hists.size(); ++i) {
        // spread the quadrant's DTCs over the palette so neighbours stay distinguishable
        hists[i].second->SetLineColor(TColor::GetColorPalette(
            static_cast<int>(i * (TColor::GetNumberOfColors() - 1) / std::max<size_t>(1, hists.size() - 1))));
        components.emplace_back(hists[i].first, hists[i].second);
      }
      nFiles += TrackerPhase2PlotUtil::saveStackedPlots(
          components, "slinkOccupancyAvgPerDTC_Q" + std::to_string(decade), plotCfg);
    }

    edm::LogInfo("Phase2ITHarvestSLink") << "wrote " << nFiles << " plot files to " << plotDir_;
  }
}

void Phase2ITHarvestSLink::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<std::string>("TopFolder", "InnerTrackerV/SLink");
  desc.add<std::string>("OccupancyMapName", "slinkOccupancyMap");
  desc.addUntracked<bool>("savePlots", false);
  desc.addUntracked<std::string>("plotDir", ".");
  desc.addUntracked<std::vector<std::string>>("plotFormats", {"png", "pdf"});
  desc.addUntracked<double>("plotZMax", 1.6);
  desc.addUntracked<double>("dataSizeYMax", 0.);

  edm::ParameterSetDescription psd0;
  psd0.add<std::string>("name", "slinkOccupancyAvg");
  psd0.add<std::string>("title", "Event-averaged SLink occupancy;occupancy;SLinks");
  psd0.add<int>("NxBins", 32);
  psd0.add<double>("xmax", 1.6);
  psd0.add<double>("xmin", 0.);
  psd0.add<bool>("switch", true);
  desc.add<edm::ParameterSetDescription>("occupancyAvg", psd0);

  descriptions.add("Phase2ITHarvestSLink", desc);
}

DEFINE_FWK_MODULE(Phase2ITHarvestSLink);
