#include "CondFormats/DataRecord/interface/TrackerDetToDTCELinkCablingMapRcd.h"
#include "CondFormats/SiPhase2TrackerObjects/interface/TrackerDetToDTCELinkCablingMap.h"
#include "DataFormats/Common/interface/Handle.h"
#include "DataFormats/FEDRawData/interface/FEDRawData.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "DQMServices/Core/interface/DQMEDAnalyzer.h"
#include "DQMServices/Core/interface/DQMStore.h"
#include "DQMServices/Core/interface/MonitorElement.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/ESGetToken.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Utilities/interface/Transition.h"

#include <algorithm>
#include <string>
#include <vector>

class Phase2ITValidateSLink: public DQMEDAnalyzer {
public:
  explicit Phase2ITValidateSLink(const edm::ParameterSet& iConfig);
  ~Phase2ITValidateSLink() override = default;
  void dqmBeginRun(const edm::Run& iRun, const edm::EventSetup& iSetup) override;
  void analyze(const edm::Event& iEvent, const edm::EventSetup& iSetup) override;
  void bookHistograms(DQMStore::IBooker& ibooker,
                      edm::Run const& iRun,
                      edm::EventSetup const& iSetup) override;
  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

private:
  void bookDTCHistos(DQMStore::IBooker& ibooker);

  const edm::EDGetTokenT<FEDRawDataCollection> fedRawToken_;
  const edm::ESGetToken<TrackerDetToDTCELinkCablingMap,
                        TrackerDetToDTCELinkCablingMapRcd> cablingMapToken_;
  const int firstFed_;
  const int nFeds_;
  const std::string folder_;

  const TrackerDetToDTCELinkCablingMap* cablingMap_ = nullptr;

  // dtcIds_ / nDTCs_ are populated from the cabling map in dqmBeginRun.
  int nDTCs_ = 36;
  int nslinksPerDTC_ = 16;
  std::vector<int> dtcIds_;
  static constexpr int nQuarters_ = 4;          // 36 DTCs = 4 quarters of 9

  // Simple histograms
  MonitorElement* me_fedSize_         = nullptr;  // FED payload (bytes)
  MonitorElement* me_slinkOccupancy_  = nullptr;  // occupancy across all SLinks

  // Per-DTC: 16 SLink bins, <occupancy> with across-event RMS error bars
  std::vector<MonitorElement*> mes_slinkOccupancyPerDTC_;

  // Combined occupancy distribution per detector quarter (DTC idx 0-8, 9-17, 18-26, 27-35)
  std::vector<MonitorElement*> mes_slinkOccupancyPerQuarter_;

  // Cross-DTC overview + 2D heatmap
  MonitorElement* me_slinkOccupancyByDTC_ = nullptr;
  MonitorElement* me_slinkOccupancyMap_   = nullptr;
};

Phase2ITValidateSLink::Phase2ITValidateSLink(const edm::ParameterSet& iConfig)
    : fedRawToken_(consumes<FEDRawDataCollection>(iConfig.getParameter<edm::InputTag>("src"))),
      cablingMapToken_(esConsumes<TrackerDetToDTCELinkCablingMap,
                                  TrackerDetToDTCELinkCablingMapRcd,
                                  edm::Transition::BeginRun>()),
      firstFed_(iConfig.getUntrackedParameter<int>("firstFed", 0)),
      nFeds_(iConfig.getUntrackedParameter<int>("nFeds", 576)),
      folder_(iConfig.getUntrackedParameter<std::string>("folder", "Phase2IT/RawData")) {
  edm::LogInfo("Phase2ITValidateSLink") << ">>> Construct Phase2ITValidateSLink";
}

void Phase2ITValidateSLink::dqmBeginRun(const edm::Run&, const edm::EventSetup& iSetup) {
  cablingMap_ = &iSetup.getData(cablingMapToken_);

  // Populate dtcIds_ from the cabling map (replace hard-coded list)
  auto known = cablingMap_->getKnownDTCIdsWithIndex();   // vector<pair<index, dtcId>>
  dtcIds_.assign(known.size(), 0);
  for (const auto& [idx, dtcId] : known) dtcIds_[idx] = dtcId;
  nDTCs_ = dtcIds_.size();
}

void Phase2ITValidateSLink::bookHistograms(DQMStore::IBooker& ibooker,
                           edm::Run const&,
                           edm::EventSetup const&) {
  ibooker.setCurrentFolder(folder_);

  me_fedSize_ = ibooker.book1D("fedSize",
                               "FED payload size;bytes;FED entries",
                               200, 0., 16000.);

  // Coarser binning; SLink-occupancy axis runs 0 -> 1.2 (the one exception).
  me_slinkOccupancy_ = ibooker.book1D("slinkOccupancy",
                                       "Per-Event SLink occupancy;occupancy;SLink entries",
                                       24, 0., 1.2);

  bookDTCHistos(ibooker);

  // Four combined occupancy distributions, one per detector quarter.
  mes_slinkOccupancyPerQuarter_.resize(nQuarters_, nullptr);
  for (int q = 0; q < nQuarters_; q++) {
    const int firstIdx = q * 9;
    const int lastIdx = std::min(q * 9 + 8, nDTCs_ - 1);
    const int firstDtc = (firstIdx < nDTCs_) ? dtcIds_[firstIdx] : 0;
    const int lastDtc = (lastIdx >= 0) ? dtcIds_[lastIdx] : 0;
    mes_slinkOccupancyPerQuarter_[q] = ibooker.book1D(
        ("slinkOccupancyQuarter_" + std::to_string(firstDtc) + "_" + std::to_string(lastDtc)).c_str(),
        ("Per-Event SLink occupancy, DTCs " + std::to_string(firstDtc) + "-" + std::to_string(lastDtc) +
         ";occupancy;SLink entries").c_str(),
        50, 0., 1.0);
  }

  // Cross-DTC overview: 1 bin per DTC, <occupancy> with across-event RMS error bars.
  me_slinkOccupancyByDTC_ = ibooker.bookProfile(
      "slinkOccupancyByDTC",
      "Mean SLink occupancy by DTC;DTC;<occupancy>",
      nDTCs_, -0.5, nDTCs_ - 0.5,
      0., 1.0);

  // 2D heatmap of all 576 SLinks in (DTC, SLink index).
  me_slinkOccupancyMap_ = ibooker.bookProfile2D(
      "slinkOccupancyMap",
      "Mean SLink occupancy;DTC;SLink index;<occupancy>",
      nDTCs_, -0.5, nDTCs_ - 0.5,
      nslinksPerDTC_, -0.5, nslinksPerDTC_ - 0.5,
      0., 1.0);

  // Label DTC axes with the real DTC numbers (11-19, 21-29, ...) instead of index.
  for (int i = 0; i < nDTCs_; i++) {
    me_slinkOccupancyByDTC_->setBinLabel(i + 1, std::to_string(dtcIds_[i]), 1);
    me_slinkOccupancyMap_->setBinLabel(i + 1, std::to_string(dtcIds_[i]), 1);
  }

  // Draw the 2D map with a Z-axis colour bar by default.
  me_slinkOccupancyMap_->getTH1()->SetOption("COLZ");
}

void Phase2ITValidateSLink::bookDTCHistos(DQMStore::IBooker& ibooker) {
  mes_slinkOccupancyPerDTC_.resize(nDTCs_, nullptr);

  for (int i = 0; i < nDTCs_; i++) {
    mes_slinkOccupancyPerDTC_[i] = ibooker.bookProfile(
        ("slinkOccupancyPerDTC_" + std::to_string(dtcIds_[i])).c_str(),
        ("Mean SLink occupancy, DTC " + std::to_string(dtcIds_[i]) +
         ";SLink index;<occupancy>").c_str(),
        nslinksPerDTC_, -0.5, nslinksPerDTC_ - 0.5,
        0., 1.0);
  }
}

void Phase2ITValidateSLink::analyze(const edm::Event& iEvent, const edm::EventSetup&) {
  edm::Handle<FEDRawDataCollection> raw;
  iEvent.getByToken(fedRawToken_, raw);
  if (!raw.isValid()) return;

  const double trigger_rate    = 750.0e3;   // Hz
  const double slink_bandwidth = 25.0e9;    // bits/s

  for (int fid = firstFed_; fid < firstFed_ + nFeds_; ++fid) {
    const size_t bytes = raw->FEDData(fid).size();
    me_fedSize_->Fill(static_cast<double>(bytes));

    const int dtcIdx  = fid / nslinksPerDTC_;
    const int slinkId = fid % nslinksPerDTC_;
    if (dtcIdx >= nDTCs_) continue;

    const double occupancy =
        (static_cast<double>(bytes) * 8.0 * trigger_rate) / slink_bandwidth;

    me_slinkOccupancy_->Fill(occupancy);
    mes_slinkOccupancyPerDTC_[dtcIdx]->Fill(slinkId, occupancy);
    me_slinkOccupancyByDTC_->Fill(dtcIdx, occupancy);
    me_slinkOccupancyMap_->Fill(dtcIdx, slinkId, occupancy);

    const int quarter = dtcIdx / 9;
    if (quarter < nQuarters_) mes_slinkOccupancyPerQuarter_[quarter]->Fill(occupancy);
  }
}

void Phase2ITValidateSLink::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("src", edm::InputTag("BitStreamToRawProducer"));
  desc.addUntracked<int>("firstFed", 0);
  desc.addUntracked<int>("nFeds", 576);
  desc.addUntracked<std::string>("folder", "Phase2IT/RawData");
  descriptions.add("Phase2ITValidateSLink", desc);
}

DEFINE_FWK_MODULE(Phase2ITValidateSLink);
