#include "DQMServices/Core/interface/DQMEDHarvester.h"
#include "DQMServices/Core/interface/DQMStore.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "DQM/SiTrackerPhase2/interface/TrackerPhase2HarvestingUtil.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"

class SlinkOccupancyHarvester : public DQMEDHarvester {
public:
  explicit SlinkOccupancyHarvester(const edm::ParameterSet&);
  ~SlinkOccupancyHarvester() override;
  void dqmEndJob(DQMStore::IBooker& ibooker, DQMStore::IGetter& igetter) override;

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

private:
  const edm::ParameterSet config_;
  const std::string topFolder_;
  const std::string occupancyMapName_;
};

SlinkOccupancyHarvester::SlinkOccupancyHarvester(const edm::ParameterSet& iConfig)
    : config_(iConfig),
      topFolder_(iConfig.getParameter<std::string>("TopFolder")),
      occupancyMapName_(iConfig.getParameter<std::string>("OccupancyMapName")) {}

SlinkOccupancyHarvester::~SlinkOccupancyHarvester() {}

void SlinkOccupancyHarvester::dqmEndJob(DQMStore::IBooker& ibooker, DQMStore::IGetter& igetter) {
  MonitorElement* occMap = igetter.get(topFolder_ + "/" + occupancyMapName_);
  if (occMap == nullptr) {
    edm::LogWarning("SlinkOccupancyHarvester") << occupancyMapName_ << " not found in " << topFolder_;
    return;
  }
  TProfile2D* prof = occMap->getTProfile2D();

  // Normalize the raw 1D occupancy to per-event
  MonitorElement* slinkOcc = igetter.get(topFolder_ + "/slinkOccupancy");
  const double nevents = prof->GetBinEntries(prof->GetBin(1, 1));
  if (slinkOcc != nullptr && nevents > 0) {
    slinkOcc->getTH1F()->Scale(1.0 / nevents);
    slinkOcc->getTH1F()->SetOption("HIST");
    slinkOcc->setAxisTitle("SLink entries / nevents", 2);  // 2 = y-axis
  }

  // Normalization for per-DTC full spectrum occupancy
  for (auto* me : igetter.getAllContents(topFolder_)) {
    if (me == nullptr) continue;
    if (me->getName().rfind("slinkOccupancySpectrumPerDTC_", 0) == 0 && nevents > 0) {
      me->getTH1F()->Scale(1.0 / nevents);
      me->getTH1F()->SetOption("HIST");
      me->setAxisTitle("SLink entries / nevents", 2);
    }
  }

  // Same normalization for 2D counterpart of above
  MonitorElement* occVsDTC = igetter.get(topFolder_ + "/slinkOccupancyVsDTC");
  if (occVsDTC != nullptr && nevents > 0) {
    occVsDTC->getTH2F()->Scale(1.0 / nevents);
    occVsDTC->getTH2F()->SetOption("COLZ");
    occVsDTC->setAxisTitle("SLink entries / nevents", 3);  // 3 = z-axis
  }

  // Make new histo for event-averaged version of 1D occupancy
  ibooker.cd();
  ibooker.setCurrentFolder(topFolder_);
  MonitorElement* occAvg =
      phase2tkharvestutil::book1DFromPSet(config_.getParameter<edm::ParameterSet>("occupancyAvg"), ibooker);
  if (occAvg == nullptr)
    return;

  // Fill above with one entry per SLink = that SLink's occupancy averaged over all events
  for (int ix = 1; ix <= prof->GetNbinsX(); ix++) {
    for (int iy = 1; iy <= prof->GetNbinsY(); iy++) {
      if (prof->GetBinEntries(prof->GetBin(ix, iy)) > 0)
        occAvg->Fill(prof->GetBinContent(ix, iy));
    }
  }
}

void SlinkOccupancyHarvester::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<std::string>("TopFolder", "Phase2IT/RawData");
  desc.add<std::string>("OccupancyMapName", "slinkOccupancyMap");

  edm::ParameterSetDescription psd0;
  psd0.add<std::string>("name", "slinkOccupancyAvg");
  psd0.add<std::string>("title", "Event-averaged SLink occupancy;occupancy;SLinks");
  psd0.add<int>("NxBins", 24);
  psd0.add<double>("xmax", 1.2);
  psd0.add<double>("xmin", 0.);
  psd0.add<bool>("switch", true);
  desc.add<edm::ParameterSetDescription>("occupancyAvg", psd0);

  descriptions.add("slinkOccupancyHarvester", desc);
}

DEFINE_FWK_MODULE(SlinkOccupancyHarvester);
