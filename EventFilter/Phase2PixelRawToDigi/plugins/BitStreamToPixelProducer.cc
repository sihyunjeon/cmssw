// EDProducer that takes ITChipBitStream and fully decodes it back to FEDRawData
// Second and final step of unpacker

#include <memory>
#include <vector>
#include <algorithm>
#include <functional>

#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include "DataFormats/Common/interface/DetSetVectorNew.h"  // input bitstream uses new container
#include "DataFormats/Common/interface/DetSetVector.h"     // output bitstream uses old container

#include "DataFormats/DetId/interface/DetId.h"
#include "DataFormats/SiPixelDigi/interface/PixelDigi.h"

#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITChip.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITQCore.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITChipBitStream.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2DAQFormatSpecification.h"

using namespace Phase2DAQFormatSpecification;

class BitStreamToPixelProducer : public edm::stream::EDProducer<> {
public:
  explicit BitStreamToPixelProducer(const edm::ParameterSet&);
  ~BitStreamToPixelProducer() override = default;
  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

private:
  void produce(edm::Event&, const edm::EventSetup&) override;

  uint32_t binaryToInt(const std::vector<bool>& binary, size_t& bitPos, int length);

  // Decode a single chip's bitstream into PixelDigi objects.
  void decodeBitStream(const std::vector<bool>& bitstream,
                       uint32_t detId,
                       int chipId,
                       edm::DetSetVector<PixelDigi>& outputDigis);

  const edm::EDGetTokenT<edmNew::DetSetVector<Phase2ITChipBitStream>> bitstreamToken_;
};

BitStreamToPixelProducer::BitStreamToPixelProducer(const edm::ParameterSet& iConfig)
    : bitstreamToken_(consumes<edmNew::DetSetVector<Phase2ITChipBitStream>>(
          iConfig.getParameter<edm::InputTag>("phase2ItChipBitStream"))) {
  produces<edm::DetSetVector<PixelDigi>>();
}

void BitStreamToPixelProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("phase2ItChipBitStream", edm::InputTag("rawToBitStreamProducer"));
  descriptions.add("bitstreamToPixelProducer", desc);
}

struct DecoderState {
  size_t bitPos = 0;
  int currentCol = 0;
  int currentRow = 0;
  int previousRow = -1;
  int previousCol = -1;
  bool previousIsLast = true;
  int qcoreCount = 0;

  DecoderState() = default;
};

uint32_t BitStreamToPixelProducer::binaryToInt(const std::vector<bool>& binary, size_t& bitPos, int length) {
  uint32_t result = 0;
  for (int i = 0; i < length; i++) {
    if (bitPos < binary.size()) {
      result = (result << 1) | (binary[bitPos] ? 1 : 0);
      bitPos++;
    } else
      break;
  }
  return result;
}

void BitStreamToPixelProducer::decodeBitStream(const std::vector<bool>& bitstream,
                                               uint32_t detId,
                                               int chipId,
                                               edm::DetSetVector<PixelDigi>& outputDigis) {
  if (bitstream.empty()) {
    return;
  }
  edm::DetSet<PixelDigi> detSet(detId);

  DecoderState state;

  while (state.bitPos < bitstream.size()) {
    // Read a fresh ccol only at the start of a new column group (previous QCore was islast, or this is the first QCore in the chip stream). Otherwise the current QCore is in the same column as the previous one, so we keep currentCol unchanged.
    if (state.previousIsLast) {
      state.currentCol = binaryToInt(bitstream, state.bitPos, 6);
    }

    bool islast = bitstream[state.bitPos++];
    bool isneighbor = bitstream[state.bitPos++];

    // isneighbor=1 means the previous qrow address is current_qrow - 1, so the qrow field is omitted and we give previous + 1.
    if (isneighbor) {
      state.currentRow = state.previousRow + 1;
    } else {
      state.currentRow = binaryToInt(bitstream, state.bitPos, 8);
    }

    std::vector<bool> hitmap = Phase2ITQCore::decodeHitmap(bitstream, state.bitPos);
    int numHits = std::count(hitmap.begin(), hitmap.end(), true);
    std::vector<int> adcValues = Phase2ITQCore::decodeADCs(bitstream, state.bitPos, numHits);

    int adcIndex = 0;
    for (int i = 0; i < HITMAP_SIZE; i++) {
      if (!hitmap[i])
        continue;
      int rocRow = i / 8;
      int rocCol = i % 8;
      int shiftedRow = rocRow * 2 + (rocCol % 2);
      int shiftedCol = rocCol / 2;
      int shiftedIndex = shiftedRow * HITMAP_COL + shiftedCol;
      auto [localRow, localCol] = Phase2ITChip::decodeQCoreIndex(shiftedIndex);
      auto [globalRow, globalCol] =
          Phase2ITChip::getGlobalPixelCoordinate(chipId, state.currentCol, state.currentRow, localCol, localRow);
      detSet.push_back(PixelDigi(globalRow, globalCol, adcValues[adcIndex++]));
    }

    state.previousIsLast = islast;
    state.previousCol = state.currentCol;
    state.previousRow = state.currentRow;
    state.qcoreCount++;
  }

  if (detSet.empty())
    throw cms::Exception("BitStreamToPixelProducer") << "Empty detSet";

  outputDigis.insert(detSet);
}

void BitStreamToPixelProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
  auto outputPixelDigis = std::make_unique<edm::DetSetVector<PixelDigi>>();
  edm::Handle<edmNew::DetSetVector<Phase2ITChipBitStream>> bitstreamHandle;
  iEvent.getByToken(bitstreamToken_, bitstreamHandle);
  if (!bitstreamHandle.isValid()) {
    throw cms::Exception("BitStreamToPixelProducer") << "Invalid BitStream handle";
  }

  // Loop over each DetSet in the input bitstream collection
  for (const auto& detSet : *bitstreamHandle) {
    DetId tkId = detSet.id();
    uint32_t detId = tkId.rawId();
    for (const auto& chipBS : detSet) {
      decodeBitStream(chipBS.get_bitstream(), detId, chipBS.get_rocid(), *outputPixelDigis);
    }
  }

  iEvent.put(std::move(outputPixelDigis));
}

DEFINE_FWK_MODULE(BitStreamToPixelProducer);
