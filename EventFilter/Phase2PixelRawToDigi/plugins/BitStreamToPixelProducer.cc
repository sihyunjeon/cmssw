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

class BitStreamToPixelProducer : public edm::stream::EDProducer<> {
public:
  explicit BitStreamToPixelProducer(const edm::ParameterSet&);
  ~BitStreamToPixelProducer() override = default;
  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

private:
  void produce(edm::Event&, const edm::EventSetup&) override;

  std::string getBitString(const std::vector<bool>& bits, size_t start, size_t len) const;
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

std::string BitStreamToPixelProducer::getBitString(const std::vector<bool>& bits, size_t start, size_t len) const {
  std::string result;
  for (size_t i = 0; i < len && (start + i) < bits.size(); i++) {
    result += (bits[start + i] ? "1" : "0");
    if ((i + 1) % 8 == 0)
      result += " ";
  }
  return result;
}

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
  std::cout << "==========================" <<std::endl;
  std::cout << "DECODER chip=" << chipId 
          << " size=" << bitstream.size() 
          << " first32=" << getBitString(bitstream, 0, 32) << std::endl;
  std::cout << "==========================" <<std::endl;
  //std::cout << "Decoding chip detId=" << detId 
  //          << " chipId=" << chipId 
  //          << " bitstreamSize=" << bitstream.size() << std::endl;
  if (bitstream.empty()) {
    //std::cout << "WARNING: Empty bitstream for detId=" << detId << " chipId=" << chipId << std::endl;
    return;
  }
  edm::DetSet<PixelDigi> detSet(detId);

  DecoderState state;

  while (state.bitPos < bitstream.size()) {
    if (state.previousIsLast) {
      state.currentCol = binaryToInt(bitstream, state.bitPos, 6);
      state.previousCol = state.currentCol;
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
    }

    std::vector<bool> hitmap = Phase2ITQCore::decodeHitmap(bitstream, state.bitPos);
    int numHits = std::count(hitmap.begin(), hitmap.end(), true);
    std::vector<int> adcValues = Phase2ITQCore::decodeADCs(bitstream, state.bitPos, numHits);

    hitmap = Phase2ITQCore::toSensorCoordinates(hitmap);
std::cout << "DECODE QCore col=" << state.currentCol << " row=" << state.currentRow << std::endl;
for (int i = 0; i < 16; i++) {
    if (hitmap[i]) {
        std::cout << "  hit at sensorIndex=" << i 
                  << " sensorRow=" << i/4 
                  << " sensorCol=" << i%4 << std::endl;
    }
}
    int adcIndex = 0;
for (int i = 0; i < HITMAP_ROW; i++) {    // i = row
  for (int j = 0; j < HITMAP_COL; j++) {  // j = col
    int hitIndex = i * HITMAP_COL + j;     // = row*4 + col  ← matches toSensorCoordinates
    auto [localRow, localCol] = Phase2ITChip::decodeQCoreIndex(hitIndex);
    if (hitIndex < static_cast<int>(hitmap.size()) && hitmap[hitIndex]) {
      auto [globalRow, globalCol] =
          Phase2ITChip::getGlobalPixelCoordinate(chipId, state.currentCol, state.currentRow, localCol, localRow);
          int adc = adcValues[adcIndex++];
if (detId == 353383448) {
    std::cout << "OUTPUT detId=353383448"
              << " col=" << globalCol 
              << " row=" << globalRow 
              << " adc=" << adc << std::endl;
}
          detSet.push_back(PixelDigi(globalRow, globalCol, adc));
          if (detId == 303042594){
std::cout << "  Decoded hit: col=" << globalCol 
          << " row=" << globalRow 
          << " adc=" << adc << std::endl;
          }
        }
      }
    }

    state.previousIsLast = islast;
    state.previousCol = state.currentCol;
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
