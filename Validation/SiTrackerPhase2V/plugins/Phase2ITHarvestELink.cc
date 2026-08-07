#include "DQMServices/Core/interface/DQMEDHarvester.h"
#include "DQMServices/Core/interface/DQMStore.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "DQM/SiTrackerPhase2/interface/TrackerPhase2HarvestingUtil.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include <algorithm>
#include <cmath>

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

  MonitorElement* nGroupME = igetter.get(topFolder_ + "/nStreamGroups");
  const double nGroups = (nGroupME != nullptr) ? nGroupME->getTH1F()->GetBinContent(1) : 0.;
  if (nGroups <= 0) {
    edm::LogWarning("ElinkOccupancyHarvester")
        << "nStreamGroups missing or empty in " << topFolder_ << "; skipping all normalization.";
  }

  // Normalize the raw 1D occupancy to per stream group
  MonitorElement* elinkOcc = igetter.get(topFolder_ + "/eLinkOccupancy");
  if (elinkOcc != nullptr && nGroups > 0) {
    elinkOcc->getTH1F()->Scale(1.0 / nGroups);
    elinkOcc->getTH1F()->SetOption("HIST");
    elinkOcc->setAxisTitle("ELink entries / stream group", 2);  // 2 = y-axis
  }

  // Normalization for per-section / per-subtype full spectrum occupancy
  for (auto* me : igetter.getAllContents(topFolder_)) {
    if (me == nullptr) continue;
    if (me->getName().rfind("eLinkOccupancyPer", 0) == 0 && nGroups > 0) {
      me->getTH1F()->Scale(1.0 / nGroups);
      me->getTH1F()->SetOption("HIST");
      me->setAxisTitle("ELink entries / stream group", 2);
    }
  }

  // Same normalization for 2D counterparts of above
  MonitorElement* occVsSection = igetter.get(topFolder_ + "/eLinkOccupancyVsSection");
  if (occVsSection != nullptr && nGroups > 0) {
    occVsSection->getTH2F()->Scale(1.0 / nGroups);
    occVsSection->getTH2F()->SetOption("COLZ");
    occVsSection->setAxisTitle("ELink entries / stream group", 3);  // 3 = z-axis
  }

  MonitorElement* occVsSubType = igetter.get(topFolder_ + "/eLinkOccupancyVsSubType");
  if (occVsSubType != nullptr && nGroups > 0) {
    occVsSubType->getTH2F()->Scale(1.0 / nGroups);
    occVsSubType->getTH2F()->SetOption("COLZ");
    occVsSubType->setAxisTitle("ELink entries / stream group", 3);
  }

  ibooker.cd();
  ibooker.setCurrentFolder(topFolder_);

  // Make new histo for event-averaged version of 1D occupancy
  MonitorElement* occAvg =
      phase2tkharvestutil::book1DFromPSet(config_.getParameter<edm::ParameterSet>("occupancyAvg"), ibooker);
  if (occAvg == nullptr)
    return;

  // Fill above with one entry per ELink = that ELink's occupancy averaged over all events
  for (int ix = 1; ix <= prof->GetNbinsX(); ix++) {
    for (int iy = 1; iy <= prof->GetNbinsY(); iy++) {
      if (prof->GetBinEntries(prof->GetBin(ix, iy)) > 0)
        occAvg->Fill(prof->GetBinContent(ix, iy));
    }
  }

  // Occupancy on z, ELink counts per occupancy band on y
  auto bookCountVs = [&](const std::string& srcName, const std::string& dstName, const std::string& xTitle) {
    MonitorElement* vsSrcME = igetter.get(topFolder_ + "/" + srcName);
    if (vsSrcME != nullptr) {
      TH2* src = vsSrcME->getTH2F();                 // X=section/subtype, Y=occupancy, content=count/nStreamGroups
      const int nXB = src->GetNbinsX();
      const int nOccB = src->GetNbinsY();
    
      // find the tallest column so the new count-axis spans the data
      double maxCount = 1.0;
      for (int ix = 1; ix <= nXB; ++ix)
        for (int iy = 1; iy <= nOccB; ++iy)
          maxCount = std::max(maxCount, src->GetBinContent(ix, iy));
    
      MonitorElement* flipME = ibooker.bookProfile2D(
          dstName.c_str(),
          ("ELink Count per Occupancy;" + xTitle + ";ELink entries / stream group;Occupancy").c_str(),
          nXB, -0.5, nXB - 0.5,
          100, 0., std::ceil(maxCount) + 1.0,
          0., 1.2);
      TProfile2D* dst = flipME->getTProfile2D();
      dst->SetStats(0);
      dst->SetOption("COLZ");
      dst->SetMinimum(0.);
      dst->SetMaximum(1.2);
      for (int i = 0; i < nXB; ++i)
        dst->GetXaxis()->SetBinLabel(i + 1, src->GetXaxis()->GetBinLabel(i + 1));
    
      for (int ix = 1; ix <= nXB; ++ix) {
        for (int iy = 1; iy <= nOccB; ++iy) {
          const double count = src->GetBinContent(ix, iy);   // # ELinks (per-event) at this occupancy
          if (count <= 0) continue;
          const double occ = src->GetYaxis()->GetBinCenter(iy);
          dst->Fill(ix - 1, count, occ);                     // Y=count, Z=occupancy
        }
      }
    }
  };

  bookCountVs("eLinkOccupancyVsSection", "eLinkCountVsSection", "Section");
  bookCountVs("eLinkOccupancyVsSubType", "eLinkCountVsSubType", "SubType");

}

void ElinkOccupancyHarvester::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<std::string>("TopFolder", "Phase2IT/RawData");
  desc.add<std::string>("OccupancyMapName", "eLinkOccupancyMap");

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
