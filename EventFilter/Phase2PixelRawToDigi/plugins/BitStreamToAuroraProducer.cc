// EDProducer that packs N events of bitstream into Phase2ITAuroraBitStream (Aurora Packer)
// isComplete flag is stored to filter out N-th events with valid Phase2ITAuroraBitStream
#include <map>
#include <vector>
#include <cstdint>
#include <iostream>

#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/one/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITChipBitStream.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITAuroraBitStream.h"
#include "DataFormats/Common/interface/DetSetVector.h"
#include "DataFormats/Common/interface/Handle.h"

class BitStreamToAuroraProducer : public edm::one::EDProducer<> {
public:
  explicit BitStreamToAuroraProducer(const edm::ParameterSet&);
  ~BitStreamToAuroraProducer() override = default;

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);
  void produce(edm::Event&, const edm::EventSetup&) override;

private:
  const edm::EDGetTokenT<edm::DetSetVector<Phase2ITChipBitStream>> ITChipBitStreamToken_;
  const unsigned int blockSize_;
  const unsigned int serviceSize_;
  unsigned int eventCount_;

  // Buffer: detId -> [chip][event] -> bit stream data
  std::map<uint32_t, std::vector<std::vector<std::vector<bool>>>> buffer_;
};

BitStreamToAuroraProducer::BitStreamToAuroraProducer(const edm::ParameterSet& iConfig)
    : ITChipBitStreamToken_(consumes<edm::DetSetVector<Phase2ITChipBitStream>>(
          iConfig.getParameter<edm::InputTag>("Phase2ITChipBitStream"))),
      blockSize_(iConfig.getParameter<unsigned int>("blockSize")),
      serviceSize_(iConfig.getParameter<unsigned int>("serviceSize")),
      eventCount_(0) {
  produces<edm::DetSetVector<Phase2ITAuroraBitStream>>();
  produces<bool>("isComplete");
}

void BitStreamToAuroraProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("Phase2ITChipBitStream", edm::InputTag("Phase2ITQCoreProducer"));
  desc.add<unsigned int>("blockSize", 16); // default event block size set to 16 (configurable 1-64)
  desc.add<unsigned int>("serviceSize", 50);
  descriptions.add("BitStreamToAuroraProducer", desc);
}

void BitStreamToAuroraProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
  using namespace edm;

  Handle<DetSetVector<Phase2ITChipBitStream>> handle;
  iEvent.getByToken(ITChipBitStreamToken_, handle);

  if (!handle.isValid()) {
    throw cms::Exception("BitStreamToAuroraProducer") << "Invalid BitStream handle";
  }

  // Accumulate this event's chip bit streams into the buffer
  unsigned int mod = 0;
  for (const auto& detset : *handle) {
    uint32_t detId = detset.id;
    auto& moduleBuffer = buffer_[detId];

    if (moduleBuffer.empty()) {
      moduleBuffer.resize(detset.size());
    }

    unsigned int chipIdx = 0;
    unsigned int bitCount;
    if (eventCount_ % blockSize_ == 0) bitCount = 0;

    // print some examples -- all chips in first 2 modules and last 2 modules for each event
    if ((mod < 2) | (mod > handle->size() - 3)) std::cout << "\n\nEVENT NUMBER: " << eventCount_ << ", Module: " << mod << "\n\n";

    for (const auto& chipBitStream : detset) {
      auto bits = chipBitStream.get_bitstream();
      // check bit sizes before altering
      if ((mod < 2) | (mod > handle->size() - 3)) std::cout << "bits size before = " << bits.size() << std::endl;

      // get first chip in first module of first event in aurora block
      bool isFirstChipEvent = (eventCount_ % blockSize_ == 0 && mod == 0 && chipIdx == 0);
      // get last chip in last module of last event in aurora block
      bool isLastChipEvent = (eventCount_ % blockSize_ == blockSize_ - 1 && mod == handle->size() - 1 && chipIdx == detset.size() - 1);
      // get last chip in last module of Nth event > 0
      bool isService = (eventCount_ > 0 && eventCount_ % serviceSize_ == (serviceSize_ - 1) && mod == handle->size() - 1 && chipIdx == detset.size() - 1);
    
      size_t chipIdBits = 2;   // add 2 bits for each chip's chipId
      size_t tagBits = isFirstChipEvent ? 8 : 11;   // add 8 bits to tag first chip in aurora block / stream, 11 bits for other chip tags

      bits.insert(bits.begin(), chipIdBits + tagBits, 0);
      bitCount += bits.size();
      
      size_t orphanBits = isLastChipEvent ? ((64 - (bitCount % 64)) % 64) : 0;   // pad entire stream to a multiple of 64 bits
      size_t serviceBits = isService ? 66 : 0;   // add aurora service blocks every N (50) events -- 2 bits for header + 64 scrambled bits
      if (isLastChipEvent) std::cout << "total bitstream before orphan padding: " << bitCount << "\n";
      bits.insert(bits.end(), orphanBits, 0);
      bitCount += orphanBits;
      if (isLastChipEvent) std::cout << "total bitstream after orphan padding: " << bitCount << "\n";
      bits.insert(bits.end(), serviceBits, 0);
      bitCount += serviceBits;

      moduleBuffer[chipIdx].push_back(bits);
      chipIdx++;

      // see if changes are as expected
      if ((mod < 2) | (mod > handle->size() - 3)) std::cout << "bits size after = " << bits.size() << std::endl;

    }
  mod++;
  }

  eventCount_++;
  bool complete = (eventCount_ % blockSize_ == 0);

  if (complete) {
    std::vector<edm::DetSet<Phase2ITAuroraBitStream>> detsets;

    for (auto& [detId, chips] : buffer_) {
      edm::DetSet<Phase2ITAuroraBitStream> detset(detId);

      for (unsigned int c = 0; c < chips.size(); c++) {
        Phase2ITAuroraBitStream aurora(c, blockSize_);
        for (auto& evtBits : chips[c]) {
          aurora.addEventBitStream(evtBits);
        }
        detset.push_back(std::move(aurora));
      }

      detsets.push_back(std::move(detset));
    }

    auto output = std::make_unique<edm::DetSetVector<Phase2ITAuroraBitStream>>(detsets);
    iEvent.put(std::move(output));
    buffer_.clear(); // Clear out buffers if the Phase2ITAuroraBitStream is filled in
  }

  iEvent.put(std::make_unique<bool>(complete), "isComplete");
}

DEFINE_FWK_MODULE(BitStreamToAuroraProducer);
