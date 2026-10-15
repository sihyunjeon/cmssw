// -*- C++ -*-
// Package:    Validation/SiTrackerPhase2V
// Class:      Phase2ITValidateSLink
// Description: Validate the per-slink occupancy DQM
//
// Author: Lacey Dishman, Sihyun Jeon (Boston University)
// Written: August 2026

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "CondFormats/DataRecord/interface/TrackerDetToDTCELinkCablingMapRcd.h"
#include "CondFormats/SiPhase2TrackerObjects/interface/TrackerDetToDTCELinkCablingMap.h"
#include "DataFormats/Common/interface/Handle.h"
#include "DataFormats/FEDRawData/interface/RawDataBuffer.h"
#include "DQM/SiTrackerPhase2/interface/TrackerPhase2DQMUtil.h"
#include "DQMServices/Core/interface/DQMEDAnalyzer.h"
#include "DQMServices/Core/interface/DQMStore.h"
#include "DQMServices/Core/interface/MonitorElement.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2DAQFormatSpecification.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2ITUnpacker.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/SLinkModuleMap.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/ESGetToken.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Utilities/interface/Transition.h"
#include "Validation/SiTrackerPhase2V/plugins/TrackerPhase2PlotUtil.h"

class Phase2ITValidateSLink : public DQMEDAnalyzer {
public:
  explicit Phase2ITValidateSLink(const edm::ParameterSet& iConfig);
  ~Phase2ITValidateSLink() override = default;
  void dqmBeginRun(const edm::Run& iRun, const edm::EventSetup& iSetup) override;
  void analyze(const edm::Event& iEvent, const edm::EventSetup& iSetup) override;
  void bookHistograms(DQMStore::IBooker& ibooker, edm::Run const& iRun, edm::EventSetup const& iSetup) override;
  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

private:
  void bookDTCHistos(DQMStore::IBooker& ibooker);
  double scaledFragmentBits(int fedId, const RawFragmentWrapper& frag) const;

  const edm::EDGetTokenT<RawDataBuffer> rawDataToken_;
  const edm::ESGetToken<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd> cablingMapToken_;

  const double scaleTBPX_;
  const double scaleTFPX_;
  const double scaleTEPX_;
  const double trigger_rate_;
  const double slink_bandwidth_;
  const std::string folder_;
  const edm::ParameterSet occupancyH_;
  const edm::ParameterSet dataSizeH_;
  const int nOccupancyBins_;
  const double occupancyMin_;
  const double occupancyMax_;
  const int nDataSizeBins_;
  const double dataSizeMin_;
  const double dataSizeMax_;

  const TrackerDetToDTCELinkCablingMap* cablingMap_ = nullptr;

  int nDTCs_ = 36;
  // Same FED ID convention as the packer: fedId = dtcIndex * SLINKS_PER_DTC + slinkId
  static constexpr int nslinksPerDTC_ = Phase2DAQFormatSpecification::SLINKS_PER_DTC;
  std::vector<int> dtcIds_;

  // Section scale of each module per fedId, in the packer's module order
  std::vector<std::vector<double>> moduleScales_;

  MonitorElement* me_nEvents_ = nullptr;  // event counter, used by the harvester for normalization
  MonitorElement* me_slinkOccupancy_ = nullptr;

  // Per-DTC: 1 bin per SLink, <occupancy> over events, error bars = spread (RMS) across events
  std::vector<MonitorElement*> mes_slinkOccupancyPerDTC_;
  std::vector<MonitorElement*> mes_slinkSpectrumOccupancyPerDTC_;

  // Per-DTC data size per event, summed over the SLinks of the DTC
  std::vector<MonitorElement*> mes_dataSizePerDTC_;

  // Cross-DTC overview + 2D heatmap
  MonitorElement* me_slinkOccupancyByDTC_ = nullptr;
  MonitorElement* me_slinkOccupancyMap_ = nullptr;

  // Full spectrum occupancy per DTC
  MonitorElement* me_slinkOccupancyVsDTC_ = nullptr;
};

Phase2ITValidateSLink::Phase2ITValidateSLink(const edm::ParameterSet& iConfig)
    : rawDataToken_(consumes<RawDataBuffer>(iConfig.getParameter<edm::InputTag>("src"))),
      cablingMapToken_(
          esConsumes<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd, edm::Transition::BeginRun>()),
      scaleTBPX_(iConfig.getUntrackedParameter<double>("scaleTBPX", 1)),
      scaleTFPX_(iConfig.getUntrackedParameter<double>("scaleTFPX", 1)),
      scaleTEPX_(iConfig.getUntrackedParameter<double>("scaleTEPX", 1)),
      trigger_rate_(iConfig.getParameter<double>("trigger_rate")),
      slink_bandwidth_(iConfig.getParameter<double>("slink_bandwidth")),
      folder_(iConfig.getUntrackedParameter<std::string>("folder", "InnerTrackerV/SLink")),
      occupancyH_(iConfig.getParameter<edm::ParameterSet>("OccupancyH")),
      dataSizeH_(iConfig.getParameter<edm::ParameterSet>("DataSizeH")),
      nOccupancyBins_(occupancyH_.getParameter<int>("NxBins")),
      occupancyMin_(occupancyH_.getParameter<double>("xmin")),
      occupancyMax_(occupancyH_.getParameter<double>("xmax")),
      nDataSizeBins_(dataSizeH_.getParameter<int>("NxBins")),
      dataSizeMin_(dataSizeH_.getParameter<double>("xmin")),
      dataSizeMax_(dataSizeH_.getParameter<double>("xmax")) {
  edm::LogInfo("Phase2ITValidateSLink") << ">>> Construct Phase2ITValidateSLink";
}

void Phase2ITValidateSLink::dqmBeginRun(const edm::Run&, const edm::EventSetup& iSetup) {
  cablingMap_ = &iSetup.getData(cablingMapToken_);

  auto known = cablingMap_->getKnownDTCIdsWithIndex();
  dtcIds_.assign(known.size(), 0);
  for (const auto& [idx, dtcId] : known)
    dtcIds_[idx] = dtcId;
  nDTCs_ = dtcIds_.size();

  using Section = TrackerDetToDTCELinkCablingMap::Section;
  const SLinkModuleMap slinkMap(*cablingMap_);
  moduleScales_.assign(nDTCs_ * nslinksPerDTC_, std::vector<double>());
  for (const auto& [fedId, detIds] : slinkMap.fedIdToDetIds()) {
    for (const uint32_t detId : detIds) {
      const int section = cablingMap_->hasModuleInfo(detId) ? cablingMap_->getModuleInfo(detId).section : 0;
      moduleScales_[fedId].push_back((section == static_cast<int>(Section::TBPX))   ? scaleTBPX_
                                     : (section == static_cast<int>(Section::TFPX)) ? scaleTFPX_
                                     : (section == static_cast<int>(Section::TEPX)) ? scaleTEPX_
                                                                                    : 1.0);
    }
  }
}

void Phase2ITValidateSLink::bookHistograms(DQMStore::IBooker& ibooker, edm::Run const&, edm::EventSetup const&) {
  ibooker.setCurrentFolder(folder_);

  me_nEvents_ = ibooker.book1D("nEvents", "Processed events;;Events", 1, 0., 1.);

  me_slinkOccupancy_ = phase2tkutil::book1DFromPSet(occupancyH_, ibooker);

  bookDTCHistos(ibooker);

  // Cross-DTC overview: 1 bin per DTC, mean over its SLinks and events, error bars = spread (RMS) of those entries
  me_slinkOccupancyByDTC_ = ibooker.bookProfile(
      "slinkOccupancyByDTC", "Mean SLink Occupancy per DTC;DTC;<Occupancy>", nDTCs_, -0.5, nDTCs_ - 0.5, 0., 0.);
  me_slinkOccupancyByDTC_->getTH1()->SetMinimum(0);
  me_slinkOccupancyByDTC_->getTH1()->SetMaximum(occupancyMax_);

  me_slinkOccupancyMap_ = ibooker.bookProfile2D("slinkOccupancyMap",
                                                "Mean SLink Occupancy;DTC;SLink Index;<Occupancy>",
                                                nDTCs_,
                                                -0.5,
                                                nDTCs_ - 0.5,
                                                nslinksPerDTC_,
                                                -0.5,
                                                nslinksPerDTC_ - 0.5,
                                                0.,
                                                0.);
  me_slinkOccupancyMap_->getTH1()->SetStats(false);
  me_slinkOccupancyMap_->getTH1()->SetMinimum(0);
  me_slinkOccupancyMap_->getTH1()->SetMaximum(occupancyMax_);
  me_slinkOccupancyMap_->getTH1()->SetOption("COLZ");

  me_slinkOccupancyVsDTC_ = ibooker.book2D("slinkOccupancyVsDTC",
                                           "Full Spectrum SLink Occupancy;DTC;Occupancy",
                                           nDTCs_,
                                           -0.5,
                                           nDTCs_ - 0.5,
                                           nOccupancyBins_,
                                           occupancyMin_,
                                           occupancyMax_);
  me_slinkOccupancyVsDTC_->getTH1()->SetStats(false);
  me_slinkOccupancyVsDTC_->getTH1()->SetOption("COLZ");

  // Label DTC axes with the real DTC numbers (11-19, 21-29, ...) instead of index
  for (int i = 0; i < nDTCs_; ++i) {
    me_slinkOccupancyByDTC_->setBinLabel(i + 1, std::to_string(dtcIds_[i]), 1);
    me_slinkOccupancyMap_->setBinLabel(i + 1, std::to_string(dtcIds_[i]), 1);
    me_slinkOccupancyVsDTC_->setBinLabel(i + 1, std::to_string(dtcIds_[i]), 1);
  }
}

void Phase2ITValidateSLink::bookDTCHistos(DQMStore::IBooker& ibooker) {
  mes_slinkOccupancyPerDTC_.assign(nDTCs_, nullptr);
  mes_slinkSpectrumOccupancyPerDTC_.assign(nDTCs_, nullptr);
  mes_dataSizePerDTC_.assign(nDTCs_, nullptr);

  for (int i = 0; i < nDTCs_; ++i) {
    mes_slinkOccupancyPerDTC_[i] = ibooker.bookProfile(
        ("slinkOccupancyPerDTC_" + std::to_string(dtcIds_[i])).c_str(),
        ("Mean SLink Occupancy, DTC " + std::to_string(dtcIds_[i]) + ";SLink Index;<Occupancy>").c_str(),
        nslinksPerDTC_,
        -0.5,
        nslinksPerDTC_ - 0.5,
        0.,
        0.);
    mes_slinkOccupancyPerDTC_[i]->getTH1()->SetMinimum(0);
    mes_slinkOccupancyPerDTC_[i]->getTH1()->SetMaximum(occupancyMax_);

    const std::string dtc = "DTC " + std::to_string(dtcIds_[i]);
    if (occupancyH_.getParameter<bool>("switch"))
      mes_slinkSpectrumOccupancyPerDTC_[i] =
          TrackerPhase2PlotUtil::book1DLike(occupancyH_,
                                            ibooker,
                                            "slinkSpectrumOccupancyPerDTC_" + std::to_string(dtcIds_[i]),
                                            TrackerPhase2PlotUtil::titleWithSuffix(occupancyH_, dtc));

    if (dataSizeH_.getParameter<bool>("switch"))
      mes_dataSizePerDTC_[i] =
          TrackerPhase2PlotUtil::book1DLike(dataSizeH_,
                                            ibooker,
                                            "dataSizePerDTC_" + std::to_string(dtcIds_[i]),
                                            TrackerPhase2PlotUtil::titleWithSuffix(dataSizeH_, dtc));
  }
}

void Phase2ITValidateSLink::analyze(const edm::Event& iEvent, const edm::EventSetup&) {
  edm::Handle<RawDataBuffer> raw;
  iEvent.getByToken(rawDataToken_, raw);
  if (!raw.isValid())
    return;

  me_nEvents_->Fill(0.5);

  // Data size of this event per DTC, accumulated over its SLinks
  std::vector<double> dtcBits(nDTCs_, 0.);

  // fedId = dtcIndex * SLINKS_PER_DTC + slinkId.
  for (auto it = raw->map().cbegin(); it != raw->map().cend(); ++it) {
    const int fid = static_cast<int>(it->first);

    const int dtcIdx = fid / nslinksPerDTC_;
    const int slinkId = fid % nslinksPerDTC_;
    if (fid < 0 || dtcIdx >= nDTCs_)
      continue;

    const auto frag = raw->fragmentData(it);
    double fragBits = scaledFragmentBits(fid, frag);
    if (fragBits < 0.)
      fragBits = static_cast<double>(frag.size()) * 8.0;
    const double occupancy = (fragBits * trigger_rate_) / slink_bandwidth_;
    // Get under/overflow filled in
    const double occupancyInRange =
        TrackerPhase2PlotUtil::clampToRange(occupancy, occupancyMin_, occupancyMax_, nOccupancyBins_);

    if (me_slinkOccupancy_ != nullptr)
      me_slinkOccupancy_->Fill(occupancyInRange);
    mes_slinkOccupancyPerDTC_[dtcIdx]->Fill(slinkId, occupancy);
    if (mes_slinkSpectrumOccupancyPerDTC_[dtcIdx] != nullptr)
      mes_slinkSpectrumOccupancyPerDTC_[dtcIdx]->Fill(occupancyInRange);
    me_slinkOccupancyByDTC_->Fill(dtcIdx, occupancy);
    me_slinkOccupancyMap_->Fill(dtcIdx, slinkId, occupancy);
    me_slinkOccupancyVsDTC_->Fill(dtcIdx, occupancyInRange);
    dtcBits[dtcIdx] += fragBits;
  }

  for (int i = 0; i < nDTCs_; ++i) {
    if (dtcBits[i] > 0. && mes_dataSizePerDTC_[i] != nullptr)
      mes_dataSizePerDTC_[i]->Fill(TrackerPhase2PlotUtil::clampToRange(
          dtcBits[i] / 1000., dataSizeMin_, dataSizeMax_, nDataSizeBins_));  // bits -> kb
  }
}

// Unpack the fragment, scale, and sum it up again.
// Negative if the fragment does not parse.
double Phase2ITValidateSLink::scaledFragmentBits(int fedId, const RawFragmentWrapper& frag) const {
  using namespace Phase2DAQFormatSpecification;
  constexpr unsigned int kMinFragBytes =
      sizeof(SLinkRocketHeader_v3) + sizeof(SLinkRocketTrailer_v3) + 2 * HEADER_TRAILER_LINES * BYTES_PER_WORD;
  constexpr int kWordsPerChunk = BITS_PER_CHUNK / BITS_PER_WORD;
  auto chunkWords = [](int words) { return (words + kWordsPerChunk - 1) / kWordsPerChunk * kWordsPerChunk; };

  if (frag.size() < kMinFragBytes)
    return -1.;
  int fedSizeInWords = 0;
  const unsigned char* dataPtr = nullptr;
  try {
    dataPtr = Phase2ITUnpacker::stripSLinkWrapper(frag.data().data(), frag.size(), fedId, fedSizeInWords);
  } catch (cms::Exception const&) {
    return -1.;
  }

  const std::vector<double>& scales = moduleScales_[fedId];
  const int numModules = scales.size();
  const int dataStart = HEADER_TRAILER_LINES + chunkWords(numModules);
  const int trailerStart = fedSizeInWords - HEADER_TRAILER_LINES;
  if (dataStart > trailerStart || !Phase2ITUnpacker::verifyHeaderTrailerPattern(dataPtr, 0) ||
      !Phase2ITUnpacker::verifyHeaderTrailerPattern(dataPtr, trailerStart))
    return -1.;

  bool valid = true;
  int dataWords = 0;
  int scaledWords = 0;
  Phase2ITUnpacker::forEachModule(
      dataPtr, fedSizeInWords, trailerStart, numModules, [&](int modIdx, Phase2ITUnpacker::ModuleSpan span) {
        int words = 0;
        int wordsScaled = 0;
        Phase2ITUnpacker::forEachChip(dataPtr, span, fedSizeInWords, [&](int, int, uint32_t nBits) {
          words += 1 + static_cast<int>((nBits + BITS_PER_WORD - 1) / BITS_PER_WORD);
          wordsScaled += 1 + static_cast<int>(std::ceil(nBits * scales[modIdx] / BITS_PER_WORD));
        });
        // Repacking unscaled has to give back the module as written
        valid = valid && chunkWords(words) == span.end - span.start;
        dataWords += span.end - span.start;
        scaledWords += chunkWords(wordsScaled);
      });
  if (!valid || dataWords != trailerStart - dataStart)
    return -1.;
  const int fragWords = frag.size() / BYTES_PER_WORD;
  return static_cast<double>(fragWords - dataWords + scaledWords) * BITS_PER_WORD;
}

void Phase2ITValidateSLink::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("src", edm::InputTag("pixelToRawProducer"));
  desc.addUntracked<double>("scaleTBPX", 1);
  desc.addUntracked<double>("scaleTFPX", 1);
  desc.addUntracked<double>("scaleTEPX", 1);
  desc.add<double>("trigger_rate", 750.0e3);
  desc.add<double>("slink_bandwidth", 25.0e9);
  desc.addUntracked<std::string>("folder", "InnerTrackerV/SLink");

  edm::ParameterSetDescription occupancyH;
  occupancyH.add<std::string>("name", "slinkOccupancy");
  occupancyH.add<std::string>("title", "Full Spectrum SLink Occupancy;Occupancy;SLink entries");
  occupancyH.add<int>("NxBins", 80);
  occupancyH.add<double>("xmin", 0.);
  occupancyH.add<double>("xmax", 1.6);
  occupancyH.add<bool>("switch", true);
  desc.add<edm::ParameterSetDescription>("OccupancyH", occupancyH);

  edm::ParameterSetDescription dataSizeH;
  dataSizeH.add<std::string>("title", "Data Size per Event;data size [kb];Events");
  dataSizeH.add<int>("NxBins", 100);
  dataSizeH.add<double>("xmin", 0.);
  dataSizeH.add<double>("xmax", 800.);
  dataSizeH.add<bool>("switch", true);
  desc.add<edm::ParameterSetDescription>("DataSizeH", dataSizeH);

  descriptions.add("Phase2ITValidateSLink", desc);
}

DEFINE_FWK_MODULE(Phase2ITValidateSLink);
