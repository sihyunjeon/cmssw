// EDProducer that takes ITChipBitStream and fully decodes it back to FEDRawData
// Second and final step of unpacker

#include <memory>
#include <vector>
#include <iostream>
#include <iomanip>
#include <sstream>
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

class BitstreamToPixelProducer : public edm::stream::EDProducer<> {
public:
  explicit BitstreamToPixelProducer(const edm::ParameterSet&);
  ~BitstreamToPixelProducer() override = default;
  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

private:
  void produce(edm::Event&, const edm::EventSetup&) override;

  std::string getBitString(const std::vector<bool>& bits, size_t start, size_t len) const;
  uint32_t binaryToInt(const std::vector<bool>& binary, size_t& bitPos, int length);

  // Decode a single chip's bitstream into PixelDigi objects.
  void decodeBitstream(const std::vector<bool>& bitstream,
                       uint32_t detId,
                       int chipId,
                       edm::DetSetVector<PixelDigi>& outputDigis);

  const edm::EDGetTokenT<edmNew::DetSetVector<Phase2ITChipBitStream>> bitstreamToken_;
};

BitstreamToPixelProducer::BitstreamToPixelProducer(const edm::ParameterSet& iConfig)
    : bitstreamToken_(consumes<edmNew::DetSetVector<Phase2ITChipBitStream>>(
          iConfig.getParameter<edm::InputTag>("phase2ItChipBitStream"))) {
  produces<edm::DetSetVector<PixelDigi>>();
}

void BitstreamToPixelProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
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

std::string BitstreamToPixelProducer::getBitString(const std::vector<bool>& bits, size_t start, size_t len) const {
  std::string result;
  for (size_t i = 0; i < len && (start + i) < bits.size(); i++) {
    result += (bits[start + i] ? "1" : "0");
    if ((i + 1) % 8 == 0)
      result += " ";
  }
  return result;
}

uint32_t BitstreamToPixelProducer::binaryToInt(const std::vector<bool>& binary, size_t& bitPos, int length) {
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

void BitstreamToPixelProducer::decodeBitstream(const std::vector<bool>& bitstream,
                                               uint32_t detId,
                                               int chipId,
                                               edm::DetSetVector<PixelDigi>& outputDigis) {
  edm::DetSet<PixelDigi> detSet(detId);

  bool debug = detId == 303058948;
  if (debug) {
    std::cout << "Decoding bitstream for detId " << detId << ", chip " << chipId << ", size: " << bitstream.size()
              << " bits" << std::endl;
    std::cout << "First 32 bits: " << getBitString(bitstream, 0, 32) << std::endl;
  }

  DecoderState state;

  while (state.bitPos < bitstream.size()) {
    if (state.previousIsLast) {
      state.currentCol = binaryToInt(bitstream, state.bitPos, 6);
      state.previousCol = state.currentCol;

      if (debug) {
        std::cout << "DEBUG: DetId " << detId << ", chip " << chipId << " new QCore column = " << state.currentCol
                  << std::endl;
      }
    }

    bool islast = bitstream[state.bitPos++];
    bool isneighbor = bitstream[state.bitPos++];

    if (isneighbor) {
      if (!state.previousIsLast)
        state.currentCol = state.previousCol + 1;
      state.currentRow = state.previousRow;
    } else {
      state.currentRow = binaryToInt(bitstream, state.bitPos, 8);
      state.previousRow = state.currentRow;

      if (debug) {
        std::cout << "DEBUG: DetId " << detId << ", chip " << chipId << " new QCore row = " << state.currentRow
                  << std::endl;
      }
    }

    std::vector<bool> hitmap = Phase2ITQCore::decodeHitmap(bitstream, state.bitPos);
    int numHits = std::count(hitmap.begin(), hitmap.end(), true);
    std::vector<int> adcValues = Phase2ITQCore::decodeADCs(bitstream, state.bitPos, numHits);

    hitmap = Phase2ITQCore::toSensorCoordinates(hitmap);
    int adcIndex = 0;

    for (int i = 0; i < HITMAP_COL; i++) {
      for (int j = 0; j < HITMAP_ROW; j++) {
        int hitIndex = i * HITMAP_ROW + j;
        auto [localRow, localCol] = Phase2ITChip::decodeQCoreIndex(hitIndex);
        if (hitIndex < static_cast<int>(hitmap.size()) && hitmap[hitIndex]) {
          auto [globalRow, globalCol] =
              Phase2ITChip::getGlobalPixelCoordinate(chipId, state.currentCol, state.currentRow, localCol, localRow);
          int adc = adcValues[adcIndex++];
          detSet.push_back(PixelDigi(globalRow, globalCol, adc));
        }
      }
    }

    state.previousIsLast = islast;
    state.previousCol = state.currentCol;
    state.qcoreCount++;
    if (state.qcoreCount >= 20)
      debug = false;
  }

  if (!detSet.empty()) {
    outputDigis.insert(detSet);
  }
}

void BitstreamToPixelProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
  std::cout << "===========================================" << std::endl;
  std::cout << "BitstreamToPixelProducer" << std::endl;
  std::cout << "===========================================" << std::endl;

  auto outputPixelDigis = std::make_unique<edm::DetSetVector<PixelDigi>>();
  edm::Handle<edmNew::DetSetVector<Phase2ITChipBitStream>> bitstreamHandle;
  iEvent.getByToken(bitstreamToken_, bitstreamHandle);
  if (!bitstreamHandle.isValid()) {
    std::cout << "ERROR: Phase2ITChipBitStream collection not found!" << std::endl;
    iEvent.put(std::move(outputPixelDigis));
    return;
  }

  // Loop over each DetSet in the input bitstream collection
  for (const auto& detSet : *bitstreamHandle) {
    DetId tkId = detSet.id();
    uint32_t detId = tkId.rawId();
    for (const auto& chipBS : detSet) {
      decodeBitstream(chipBS.get_bitstream(), detId, chipBS.get_rocid(), *outputPixelDigis);
    }
  }

  iEvent.put(std::move(outputPixelDigis));
}

DEFINE_FWK_MODULE(BitstreamToPixelProducer);
