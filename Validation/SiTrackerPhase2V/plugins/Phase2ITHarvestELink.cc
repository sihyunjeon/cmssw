#include "DQMServices/Core/interface/DQMEDHarvester.h"
#include "DQMServices/Core/interface/DQMStore.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "DQM/SiTrackerPhase2/interface/TrackerPhase2HarvestingUtil.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"

class ElinkOccupancyHarvester : public DQMEDHarvester {
public:
  explicit ElinkOccupancyHarvester(const edm::ParameterSet&);
  ~ElinkOccupancyHarvester() override;
  void dqmEndJob(DQMStore::IBooker& ibooker, DQMStore::IGetter& igetter) override;

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

private:
  const edm::ParameterSet config_;
  const std::string topFolder_;
  const std::string occupancyMapName_;
};

ElinkOccupancyHarvester::ElinkOccupancyHarvester(const edm::ParameterSet& iConfig)
    : config_(iConfig),
      topFolder_(iConfig.getParameter<std::string>("TopFolder")),
      occupancyMapName_(iConfig.getParameter<std::string>("OccupancyMapName")) {}

ElinkOccupancyHarvester::~ElinkOccupancyHarvester() {}

void ElinkOccupancyHarvester::dqmEndJob(DQMStore::IBooker& ibooker, DQMStore::IGetter& igetter) {
  MonitorElement* occMap = igetter.get(topFolder_ + "/" + occupancyMapName_);
  if (occMap == nullptr) {
    edm::LogWarning("ElinkOccupancyHarvester") << occupancyMapName_ << " not found in " << topFolder_;
    return;
  }
  TProfile2D* prof = occMap->getTProfile2D();

  // Event count. Unlike the SLink map (one fill per SLink per event, so bin(1,1)
  // entries == nevents), the ELink map is filled per chip with several chips
  // sharing one (section, subtype) cell, so nevents is read from the counter
  MonitorElement* nEvtME = igetter.get(topFolder_ + "/nEvents");
  const double nevents = (nEvtME != nullptr) ? nEvtME->getTH1F()->GetBinContent(1) : 0.;

  // Normalize the raw 1D occupancy to per-event
  MonitorElement* elinkOcc = igetter.get(topFolder_ + "/elinkOccupancy");
  if (elinkOcc != nullptr && nevents > 0) {
    elinkOcc->getTH1F()->Scale(1.0 / nevents);
    elinkOcc->getTH1F()->SetOption("HIST");
    elinkOcc->setAxisTitle("ELink entries / nevents", 2);  // 2 = y-axis
  }

  // Normalization for per-section / per-subtype full spectrum occupancy
  for (auto* me : igetter.getAllContents(topFolder_)) {
    if (me == nullptr) continue;
    if (me->getName().rfind("eLinkOccupancyPer", 0) == 0 && nevents > 0) {
      me->getTH1F()->Scale(1.0 / nevents);
      me->getTH1F()->SetOption("HIST");
      me->setAxisTitle("ELink entries / nevents", 2);
    }
  }

  // Same normalization for 2D counterparts of above
  MonitorElement* occVsSection = igetter.get(topFolder_ + "/eLinkOccupancyVsSection");
  if (occVsSection != nullptr && nevents > 0) {
    occVsSection->getTH2F()->Scale(1.0 / nevents);
    occVsSection->getTH2F()->SetOption("COLZ");
    occVsSection->setAxisTitle("ELink entries / nevents", 3);  // 3 = z-axis
  }

  MonitorElement* occVsSubType = igetter.get(topFolder_ + "/eLinkOccupancyVsSubType");
  if (occVsSubType != nullptr && nevents > 0) {
    occVsSubType->getTH2F()->Scale(1.0 / nevents);
    occVsSubType->getTH2F()->SetOption("COLZ");
    occVsSubType->setAxisTitle("ELink entries / nevents", 3);
  }

  ibooker.cd();
  ibooker.setCurrentFolder(topFolder_);

  // Make new histo for event-averaged version of 1D occupancy
  MonitorElement* occAvg =
      phase2tkharvestutil::book1DFromPSet(config_.getParameter<edm::ParameterSet>("occupancyAvg"), ibooker);
  if (occAvg == nullptr)
    return;

  // Fill above with one entry per (section, subtype) cell = that cell's occupancy averaged over all events
  for (int ix = 1; ix <= prof->GetNbinsX(); ix++) {
    for (int iy = 1; iy <= prof->GetNbinsY(); iy++) {
      if (prof->GetBinEntries(prof->GetBin(ix, iy)) > 0)
        occAvg->Fill(prof->GetBinContent(ix, iy));
    }
  }
}

void ElinkOccupancyHarvester::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<std::string>("TopFolder", "Phase2IT/RawData");
  desc.add<std::string>("OccupancyMapName", "eLinkOccupancyChipMap");

  edm::ParameterSetDescription psd0;
  psd0.add<std::string>("name", "eLinkOccupancyAvg");
  psd0.add<std::string>("title", "Event-averaged ELink occupancy;occupancy;ELinks");
  psd0.add<int>("NxBins", 24);
  psd0.add<double>("xmax", 1.2);
  psd0.add<double>("xmin", 0.);
  psd0.add<bool>("switch", true);
  desc.add<edm::ParameterSetDescription>("occupancyAvg", psd0);

  descriptions.add("elinkOccupancyHarvester", desc);
}

DEFINE_FWK_MODULE(ElinkOccupancyHarvester);
