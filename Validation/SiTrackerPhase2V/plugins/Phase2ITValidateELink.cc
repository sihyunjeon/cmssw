// This file refers to detector sections. The ones used are TBPX L1-L4, TFPX R1-R4, TEPX R1-R5.
// The choice of these detector sections can be changed if needed.

#include <cstdint>
#include <set>
#include <vector>
#include <map>

#include "CondFormats/DataRecord/interface/TrackerDetToDTCELinkCablingMapRcd.h"
#include "CondFormats/SiPhase2TrackerObjects/interface/TrackerDetToDTCELinkCablingMap.h"
#include "DataFormats/Common/interface/DetSetVector.h"
#include "DataFormats/Common/interface/Handle.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITAuroraBitStream.h"
#include "DQMServices/Core/interface/DQMEDAnalyzer.h"
#include "DQMServices/Core/interface/DQMStore.h"
#include "DQMServices/Core/interface/MonitorElement.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/ESGetToken.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Utilities/interface/Transition.h"


class Phase2ITValidateELink : public DQMEDAnalyzer {
public:
  explicit Phase2ITValidateELink(const edm::ParameterSet& iConfig);
  ~Phase2ITValidateELink() override = default;
  void dqmBeginRun(const edm::Run& iRun, const edm::EventSetup& iSetup) override;
  void analyze(const edm::Event& iEvent, const edm::EventSetup& iSetup) override;
  void bookHistograms(DQMStore::IBooker& ibooker,
                      edm::Run const& iRun,
                      edm::EventSetup const& iSetup) override;
  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

private:
  const edm::EDGetTokenT<edm::DetSetVector<Phase2ITAuroraBitStream>> auroraToken_;
  const edm::ESGetToken<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd> cablingMapToken_;

  const double scaleTBPX_;
  const double scaleTFPX_;
  const double scaleTEPX_;
  const double trigger_rate_;
  const double elink_bandwidth_;
  const std::string folder_;

  const TrackerDetToDTCELinkCablingMap* cablingMap_ = nullptr;

  MonitorElement* me_elinkOccupancy_ = nullptr;

  // cross-region overviews + 2D maps
  std::vector<std::string> sectionLabels_;               // bin labels for the 13 sections
  MonitorElement* me_elinkOccupancyBySection_ = nullptr; // <occ> per section (1 bin/section), RMS bars
  MonitorElement* me_elinkOccupancyBySubType_ = nullptr; // <occ> per subtype (1 bin/subtype), RMS bars
  MonitorElement* me_elinkOccupancyVsSection_ = nullptr; // full-spectrum occupancy vs section (2D)
  MonitorElement* me_elinkOccupancyVsSubType_ = nullptr; // full-spectrum occupancy vs subtype (2D)
  MonitorElement* me_nEvents_ = nullptr;

  static constexpr int nSections_ = 13;                        // 4 TBPX + 4 TFPX + 5 TEPX
  std::vector<MonitorElement*> mes_elinkOccupancyPerSection_;
  std::map<std::pair<int,int>, unsigned int> sectionToIndex_;  // {sectionCode, layerOrRing} -> idx

  std::vector<int> subTypeVals_;
  std::vector<MonitorElement*> mes_elinkOccupancyPerSubType_;
  std::map<int, unsigned int> subTypeToIndex_;

  void bookSectionHistos(DQMStore::IBooker& ibooker);
  void bookSubTypeHistos(DQMStore::IBooker& ibooker);

  std::map<uint32_t, int> moduleToIndex_;            // detId -> 0-based module index
  int nModules_ = 0;
  static constexpr int maxELinksPerModule_ = 16;      // safe upper bound; surplus cells stay empty
  MonitorElement* me_elinkOccupancyMap_ = nullptr;

};

Phase2ITValidateELink::Phase2ITValidateELink(const edm::ParameterSet& iConfig)
    : auroraToken_(
          consumes<edm::DetSetVector<Phase2ITAuroraBitStream>>(iConfig.getParameter<edm::InputTag>("auroraBitStream"))),
      cablingMapToken_(esConsumes<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd, edm::Transition::BeginRun>()),
      scaleTBPX_(iConfig.getUntrackedParameter<double>("scaleTBPX", 1)),
      scaleTFPX_(iConfig.getUntrackedParameter<double>("scaleTFPX", 1)),
      scaleTEPX_(iConfig.getUntrackedParameter<double>("scaleTEPX", 1)),
      trigger_rate_(iConfig.getUntrackedParameter<double>("trigger_rate", 750.0e3)),
      elink_bandwidth_(iConfig.getUntrackedParameter<double>("elink_bandwidth", 1.28e9)),
      folder_(iConfig.getUntrackedParameter<std::string>("folder", "Phase2IT/RawData")) {
      edm::LogInfo("Phase2ITValidateELink") << ">>> Construct Phase2ITValidateELink";
}

void Phase2ITValidateELink::dqmBeginRun(const edm::Run&, const edm::EventSetup& iSetup) {
  cablingMap_ = &iSetup.getData(cablingMapToken_);

  std::set<int> subtypes;
  std::set<uint32_t> modules;                        // sorted, unique detIds with module info
  for (uint32_t detId : cablingMap_->getKnownDetIds()) {
    if (!cablingMap_->hasModuleInfo(detId)) continue;
    subtypes.insert(static_cast<int>(cablingMap_->getModuleInfo(detId).subtype));
    modules.insert(detId);
  }
  subTypeVals_.assign(subtypes.begin(), subtypes.end());   // sorted, unique

  int m = 0;
  for (uint32_t detId : modules) moduleToIndex_[detId] = m++;   // detId -> 0-based index
  nModules_ = m;
}

void Phase2ITValidateELink::bookHistograms(DQMStore::IBooker& ibooker,
                           edm::Run const&,
                           edm::EventSetup const&) {
  ibooker.setCurrentFolder(folder_);

  me_nEvents_ = ibooker.book1D("nEvents", "Processed events;;Events", 1, 0., 1.);

  me_elinkOccupancy_ = ibooker.book1D("eLinkOccupancy",
                                      "Full Spectrum ELink Occupancy;Occupancy;Per-event ELink entries",
                                      60, 0., 1.2);

  bookSectionHistos(ibooker);
  bookSubTypeHistos(ibooker);

  // cross-region overviews + 2D maps 
  const int nSub = static_cast<int>(subTypeVals_.size());

  // Per-ELink occupancy map: module index x ELink-in-module. Source for the event-averaged
  // plot in the harvester (one column per module, so not meant for direct viewing)
  me_elinkOccupancyMap_ = ibooker.bookProfile2D(
      "eLinkOccupancyMap",
      "Mean ELink Occupancy;Module Index;ELink Index;<Occupancy>",
      nModules_, -0.5, nModules_ - 0.5,
      maxELinksPerModule_, -0.5, maxELinksPerModule_ - 0.5,
      0., 1.2);
  me_elinkOccupancyMap_->getTH1()->SetStats(0);

  // Cross-section overview: 1 bin per section, <occupancy> with across-event RMS error bars
  me_elinkOccupancyBySection_ = ibooker.bookProfile(
      "eLinkOccupancyBySection",
      "Mean ELink Occupancy Averaged Over Sections;Section;<Occupancy>",
      nSections_, -0.5, nSections_ - 0.5,
      0., 1.2);
  me_elinkOccupancyBySection_->getTH1()->SetMinimum(0);
  me_elinkOccupancyBySection_->getTH1()->SetMaximum(1.2);

  // Cross-subtype overview: 1 bin per subtype, <occupancy> with across-event RMS error bars
  me_elinkOccupancyBySubType_ = ibooker.bookProfile(
      "eLinkOccupancyBySubType",
      "Mean ELink Occupancy Averaged Over SubTypes;SubType;<Occupancy>",
      nSub, -0.5, nSub - 0.5,
      0., 1.2);
  me_elinkOccupancyBySubType_->getTH1()->SetMinimum(0);
  me_elinkOccupancyBySubType_->getTH1()->SetMaximum(1.2);

  // 2D full spectrum: occupancy distribution vs section (color = entry count)
  me_elinkOccupancyVsSection_ = ibooker.book2D(
      "eLinkOccupancyVsSection",
      "Full Spectrum ELink Occupancy;Section;Occupancy",
      nSections_, -0.5, nSections_ - 0.5,
      60, 0., 1.2);
  me_elinkOccupancyVsSection_->getTH1()->SetStats(0);
  me_elinkOccupancyVsSection_->getTH1()->SetOption("COLZ");

  // 2D full spectrum: occupancy distribution vs subtype (color = entry count)
  me_elinkOccupancyVsSubType_ = ibooker.book2D(
      "eLinkOccupancyVsSubType",
      "Full Spectrum ELink Occupancy;SubType;Occupancy",
      nSub, -0.5, nSub - 0.5,
      60, 0., 1.2);
  me_elinkOccupancyVsSubType_->getTH1()->SetStats(0);
  me_elinkOccupancyVsSubType_->getTH1()->SetOption("COLZ");

  // Label section axes (TBPX_L1.., TFPX_R1.., TEPX_R1..) and subtype axes (subtype IDs)
  for (int i = 0; i < nSections_; ++i) {
    me_elinkOccupancyBySection_->setBinLabel(i + 1, sectionLabels_[i], 1);
    me_elinkOccupancyVsSection_->setBinLabel(i + 1, sectionLabels_[i], 1);
  }

  for (int j = 0; j < nSub; ++j) {
    me_elinkOccupancyBySubType_->setBinLabel(j + 1, std::to_string(subTypeVals_[j]), 1);
    me_elinkOccupancyVsSubType_->setBinLabel(j + 1, std::to_string(subTypeVals_[j]), 1);
  }

}

void Phase2ITValidateELink::bookSectionHistos(DQMStore::IBooker& ibooker) {
  using Section = TrackerDetToDTCELinkCablingMap::Section;
  mes_elinkOccupancyPerSection_.assign(nSections_, nullptr);
  sectionLabels_.assign(nSections_, "");
  int idx = 0;
  for (int L = 1; L <= 4; ++L) {  // TBPX L1-->L4
    mes_elinkOccupancyPerSection_[idx] =
        ibooker.book1D(("eLinkOccupancyPerSection_TBPX_L" + std::to_string(L)).c_str(),
                       ("Full Spectrum ELink Occupancy, TBPX_L" + std::to_string(L) +
                        ";Occupancy;Per-event ELink entries").c_str(),
                       60, 0., 1.2);
    sectionLabels_[idx] = "TBPX_L" + std::to_string(L);
    sectionToIndex_[{static_cast<int>(Section::TBPX), L}] = idx++;
  }

  for (int R = 1; R <= 4; ++R) {  // TFPX R1-->R4
    mes_elinkOccupancyPerSection_[idx] =
        ibooker.book1D(("eLinkOccupancyPerSection_TFPX_R" + std::to_string(R)).c_str(),
                       ("Full Spectrum ELink Occupancy, TFPX_R" + std::to_string(R) +
                        ";Occupancy;Per-event ELink entries").c_str(),
                       60, 0., 1.2);
    sectionLabels_[idx] = "TFPX_R" + std::to_string(R);
    sectionToIndex_[{static_cast<int>(Section::TFPX), R}] = idx++;
  }

  for (int R = 1; R <= 5; ++R) {  // TEPX R1-->R5
    mes_elinkOccupancyPerSection_[idx] =
        ibooker.book1D(("eLinkOccupancyPerSection_TEPX_R" + std::to_string(R)).c_str(),
                       ("Full Spectrum ELink Occupancy, TEPX_R" + std::to_string(R) +
                        ";Occupancy;Per-event ELink entries").c_str(),
                       60, 0., 1.2);
    sectionLabels_[idx] = "TEPX_R" + std::to_string(R);
    sectionToIndex_[{static_cast<int>(Section::TEPX), R}] = idx++;
  }

}

void Phase2ITValidateELink::bookSubTypeHistos(DQMStore::IBooker& ibooker) {
  edm::LogInfo("ELink") << "subtypes found: " << subTypeVals_.size();
  mes_elinkOccupancyPerSubType_.assign(subTypeVals_.size(), nullptr);
  for (size_t i = 0; i < subTypeVals_.size(); ++i) {
    mes_elinkOccupancyPerSubType_[i] =
        ibooker.book1D(("eLinkOccupancyPerSubType_" + std::to_string(subTypeVals_[i])).c_str(),
                       ("Full Spectrum ELink Occupancy, SubType " + std::to_string(subTypeVals_[i]) +
                        ";Occupancy;Per-event ELink entries").c_str(),
                       60, 0., 1.2);
    subTypeToIndex_[subTypeVals_[i]] = i;
  }

}

void Phase2ITValidateELink::analyze(const edm::Event& iEvent, const edm::EventSetup& ) {
  edm::Handle<edm::DetSetVector<Phase2ITAuroraBitStream>> handle;
  iEvent.getByToken(auroraToken_, handle);
  if (!handle.isValid() || handle->empty())
    return;
  me_nEvents_->Fill(0.5);   // placeholder fill for counts

  using Section = TrackerDetToDTCELinkCablingMap::Section;

  // Loop over modules
  for (const auto& detset : *handle) {
    if (!cablingMap_->hasModuleInfo(detset.id))   // guard: getModuleInfo may throw on missing DetId
      continue;
    const auto& info = cablingMap_->getModuleInfo(detset.id);
    const int section = static_cast<int>(info.section);
    const double sectionScale = (section == static_cast<int>(Section::TBPX)) ? scaleTBPX_
                              : (section == static_cast<int>(Section::TFPX)) ? scaleTFPX_
                              : (section == static_cast<int>(Section::TEPX)) ? scaleTEPX_
                                                                            : 1.0;
    const int layer   = static_cast<int>(info.layer);
    const int ring    = static_cast<int>(info.ring);
    const int subtype = static_cast<int>(info.subtype);

    // TBPX keys on layer; TFPX/TEPX key on ring
    std::pair<int,int> key = (section == static_cast<int>(Section::TBPX))
                                 ? std::make_pair(section, layer)
                                 : std::make_pair(section, ring);
    auto secIt = sectionToIndex_.find(key);
    auto subIt = subTypeToIndex_.find(subtype);
    auto modIt = moduleToIndex_.find(detset.id);

    // Loop over ELinks in module
    for (const auto& aurora : detset) {           // aurora = one ELink
      const int elinkIdx = aurora.get_elinkId();
      const int ne = aurora.get_eventsPerStream();   // this is per-elink, not per-stream
      
      double elinkBits = 0.;
      // Loop over streams in ELink
      for (const auto& stream : aurora.get_auroraStreams()) {
        const int bits = static_cast<int>(stream.size());
        elinkBits += bits;
      }

      const double elinkBitsPerEvent = (ne > 0) ? elinkBits / ne : elinkBits;
      const double occupancy = ((elinkBitsPerEvent * trigger_rate_) / (elink_bandwidth_)) * sectionScale;
      me_elinkOccupancy_->Fill(occupancy);

      if (modIt != moduleToIndex_.end())
        me_elinkOccupancyMap_->Fill(modIt->second, elinkIdx, occupancy);

      if (secIt != sectionToIndex_.end()) {
        mes_elinkOccupancyPerSection_[secIt->second]->Fill(occupancy);
        me_elinkOccupancyBySection_->Fill(secIt->second, occupancy);
        me_elinkOccupancyVsSection_->Fill(secIt->second, occupancy);
      }

      if (subIt != subTypeToIndex_.end()) {
        mes_elinkOccupancyPerSubType_[subIt->second]->Fill(occupancy);
        me_elinkOccupancyBySubType_->Fill(subIt->second, occupancy);
        me_elinkOccupancyVsSubType_->Fill(subIt->second, occupancy);
      }

    }
  }

}

void Phase2ITValidateELink::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("auroraBitStream", edm::InputTag("BitStreamToAuroraProducer"));
  desc.addUntracked<double>("scaleTBPX", 1);
  desc.addUntracked<double>("scaleTFPX", 1);
  desc.addUntracked<double>("scaleTEPX", 1);
  desc.addUntracked<double>("trigger_rate", 750.0e3);
  desc.addUntracked<double>("elink_bandwidth", 1.28e9);
  desc.addUntracked<std::string>("folder", "Phase2IT/RawData");
  descriptions.add("Phase2ITValidateELink", desc);
}

DEFINE_FWK_MODULE(Phase2ITValidateELink);
