// -*- C++ -*-
// Package:    EventFilter/Phase2PixelRawToDigi
// Class:      BitStreamToAuroraProducer
// Description: Pack NE events of per-chip RD53 bit-streams into Aurora-format
// Author: Si Hyun Jeon, shjeon@cern.ch
//

#include <map>
#include <vector>
#include <cstdint>
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/one/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "DataFormats/Common/interface/DetSetVector.h"
#include "DataFormats/Common/interface/Handle.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITChipBitStream.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITAuroraBitStream.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2AuroraPacker.h"

class BitStreamToAuroraProducer : public edm::one::EDProducer<> {
public:
  explicit BitStreamToAuroraProducer(const edm::ParameterSet&);
  ~BitStreamToAuroraProducer() override = default;

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);
  void produce(edm::Event&, const edm::EventSetup&) override;

private:
  const edm::EDGetTokenT<edm::DetSetVector<Phase2ITChipBitStream>> ITChipBitStreamToken_;
  const unsigned int eventsPerStream_;       // NE: events per stream group
  const unsigned int serviceBlockInterval_;  // ND: data blocks per Aurora service block
  unsigned int eventCount_;

  // detId -> chip -> per-event chip bit streams, accumulated across NE events
  std::map<uint32_t, std::vector<std::vector<std::vector<bool>>>> buffer_;
};

BitStreamToAuroraProducer::BitStreamToAuroraProducer(const edm::ParameterSet& iConfig)
    : ITChipBitStreamToken_(consumes<edm::DetSetVector<Phase2ITChipBitStream>>(
          iConfig.getParameter<edm::InputTag>("Phase2ITChipBitStream"))),
      eventsPerStream_(iConfig.getParameter<unsigned int>("eventsPerStream")),
      serviceBlockInterval_(iConfig.getParameter<unsigned int>("serviceBlockInterval")),
      eventCount_(0) {
  using namespace Phase2DAQFormatSpecification;
  if (eventsPerStream_ < (unsigned)AURORA_EVENTS_PER_STREAM_MIN ||
      eventsPerStream_ > (unsigned)AURORA_EVENTS_PER_STREAM_MAX)
    throw cms::Exception("BitStreamToAuroraProducer")
        << "eventsPerStream=" << eventsPerStream_ << " out of range ["
        << AURORA_EVENTS_PER_STREAM_MIN << ", " << AURORA_EVENTS_PER_STREAM_MAX << "]";
  if (serviceBlockInterval_ < (unsigned)AURORA_SERVICE_BLOCK_INTERVAL_MIN ||
      serviceBlockInterval_ > (unsigned)AURORA_SERVICE_BLOCK_INTERVAL_MAX)
    throw cms::Exception("BitStreamToAuroraProducer")
        << "serviceBlockInterval=" << serviceBlockInterval_ << " out of range ["
        << AURORA_SERVICE_BLOCK_INTERVAL_MIN << ", " << AURORA_SERVICE_BLOCK_INTERVAL_MAX << "]";
  produces<edm::DetSetVector<Phase2ITAuroraBitStream>>();
  produces<bool>("isComplete");
}

void BitStreamToAuroraProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("Phase2ITChipBitStream", edm::InputTag("Phase2ITQCoreProducer"));
  using Phase2DAQFormatSpecification::AURORA_EVENTS_PER_STREAM_DEFAULT;
  using Phase2DAQFormatSpecification::AURORA_SERVICE_BLOCK_INTERVAL_DEFAULT;
  desc.add<unsigned int>("eventsPerStream",
                         AURORA_EVENTS_PER_STREAM_DEFAULT);  // NE, Number of streamed events, configurable 1..64
  desc.add<unsigned int>("serviceBlockInterval",
                         AURORA_SERVICE_BLOCK_INTERVAL_DEFAULT);  // ND, Aurora service block, configurable 1..256
  descriptions.add("BitStreamToAuroraProducer", desc);
}

void BitStreamToAuroraProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
  using namespace edm;

  Handle<DetSetVector<Phase2ITChipBitStream>> handle;
  iEvent.getByToken(ITChipBitStreamToken_, handle);
  if (!handle.isValid())
    throw cms::Exception("BitStreamToAuroraProducer") << "Invalid BitStream handle";

  // Accumulate this event's chip bit streams into the buffer.
  for (const auto& detset : *handle) {
    auto& moduleBuffer = buffer_[detset.id];
    if (moduleBuffer.empty())
      moduleBuffer.resize(detset.size());
    unsigned int chipIdx = 0;
    for (const auto& chipBitStream : detset) {
      moduleBuffer[chipIdx].push_back(chipBitStream.get_bitstream());
      chipIdx++;
    }
  }

  eventCount_++;
  const bool complete = (eventCount_ % eventsPerStream_ == 0);

  if (complete) {
    // FIXME chip id place holder should be later replaced once cabling map db file is fully ready
    constexpr int CHIP_ID_PLACEHOLDER = 1;
    constexpr int EVENT_ID_PLACEHOLDER = 1;

    std::vector<edm::DetSet<Phase2ITAuroraBitStream>> detsets;
    for (auto& [detId, chips] : buffer_) {
      edm::DetSet<Phase2ITAuroraBitStream> detset(detId);
      for (unsigned int c = 0; c < chips.size(); c++) {
        Phase2ITAuroraBitStream aurora(c, eventsPerStream_);

        // Build the concatenated, per-event-tagged RD53 stream for this chip
        std::vector<bool> concat;
        for (unsigned int e = 0; e < chips[c].size(); ++e) {
          auto tag = phase2auroratools::make_event_tag(EVENT_ID_PLACEHOLDER, (e == 0)); // e==0, 8 bits, else, 11 bits
          concat.insert(concat.end(), tag.begin(), tag.end());
          concat.insert(concat.end(), chips[c][e].begin(), chips[c][e].end());
        }

        // Aurora formatting 
        auto blocked = phase2auroratools::apply_blocking(concat, CHIP_ID_PLACEHOLDER);
        auto padded = phase2auroratools::orphan_pad(blocked);
        auto eos = phase2auroratools::apply_eos_marker(padded);
        auto headered = phase2auroratools::apply_header_blocks(eos);
        auto full = phase2auroratools::apply_service_blocks(headered, serviceBlockInterval_);

        aurora.addEventBitStream(std::vector<bool>(full.size(), false));
        detset.push_back(std::move(aurora));
      }
      detsets.push_back(std::move(detset));
    }

    auto output = std::make_unique<edm::DetSetVector<Phase2ITAuroraBitStream>>(detsets);
    iEvent.put(std::move(output));
    buffer_.clear();
  }

  iEvent.put(std::make_unique<bool>(complete), "isComplete");
}

DEFINE_FWK_MODULE(BitStreamToAuroraProducer);
