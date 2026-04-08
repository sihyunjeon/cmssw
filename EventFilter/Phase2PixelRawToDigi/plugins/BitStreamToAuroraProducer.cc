// EDProducer that packs N events of bitstream into Phase2ITAuroraBitStream (Aurora Packer)
// isComplete flag is stored to filter out N-th events with valid Phase2ITAuroraBitStream
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
  desc.add<unsigned int>("serviceSize", 50); // default number of blocks until service block set to 50
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
  for (const auto& detset : *handle) {
    uint32_t detId = detset.id;
    auto& moduleBuffer = buffer_[detId];

    if (moduleBuffer.empty()) {
      moduleBuffer.resize(detset.size());
    }

    unsigned int chipIdx = 0;
    for (const auto& chipBitStream : detset) {
      moduleBuffer[chipIdx].push_back(chipBitStream.get_bitstream());
      chipIdx++;
    }
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
          //aurora.addEventBitStream(evtBits);
          
          bool isStart = (&evtBits == &chips[c].front());  // true if this is the first event in this chip block
          BitPacker p(isStart);

          for (bool bit : evtBits) p.write(bit);  // write all event bits w/ headers (chipId and tag)
          p.align(); // pad last partially filled word

          // record total bits written in Aurora (actual bit values not needed)  
          int totalBits = static_cast<int>(p.words.size() * 64);
          aurora.addEventBitStream(std::vector<bool>(totalBits, false));
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
