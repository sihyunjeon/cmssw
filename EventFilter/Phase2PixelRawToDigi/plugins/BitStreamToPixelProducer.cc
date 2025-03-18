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

#include "DataFormats/Common/interface/DetSetVectorNew.h" // input bitstream uses new container
#include "DataFormats/Common/interface/DetSetVector.h" // output bitstream uses old container

#include "DataFormats/DetId/interface/DetId.h"
#include "DataFormats/SiPixelDigi/interface/PixelDigi.h"

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
  std::vector<bool> decodeHuffmanHitmap(const std::vector<bool>& bitstream, size_t& bitPos);
  void convertToSensorCoordinates(std::vector<bool>& hitmap);
  std::pair<int,int> reverseHitmapIndexMapping(int index);

  // Decode a single chip's bitstream into PixelDigi objects.
  void decodeBitstream(const std::vector<bool>& bitstream, uint32_t detId, int chipId,
                       edm::DetSetVector<PixelDigi>& outputDigis);

  const edm::EDGetTokenT<edmNew::DetSetVector<Phase2ITChipBitStream>> bitstreamToken_;
};

BitstreamToPixelProducer::BitstreamToPixelProducer(const edm::ParameterSet& iConfig) :
  bitstreamToken_(consumes<edmNew::DetSetVector<Phase2ITChipBitStream>>(iConfig.getParameter<edm::InputTag>("phase2ItChipBitStream")))
{
  produces<edm::DetSetVector<PixelDigi>>();
}

void BitstreamToPixelProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("phase2ItChipBitStream", edm::InputTag("rawToBitStreamProducer"));
  descriptions.add("bitstreamToPixelProducer", desc);
}

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
    } else break;
  }
  return result;
}

std::vector<bool> BitstreamToPixelProducer::decodeHuffmanHitmap(const std::vector<bool>& bitstream, size_t& bitPos) {
  // function that recursively decodes Huffman encoded hitmap
  std::vector<bool> hitmap(HITMAP_SIZE, false);
  std::function<void(size_t, size_t)> decode_recursive = [&](size_t start, size_t length) {
    if (length == 1) {
      if (start < hitmap.size())
        hitmap[start] = true;
      return;
    }
    if (bitPos >= bitstream.size())
      return;
    bool firstBit = bitstream[bitPos++];
    size_t half = length / 2;
    size_t mid = start + half;
    if (!firstBit) {
      decode_recursive(mid, half);
    } else {
      if (bitPos >= bitstream.size())
        return;
      bool secondBit = bitstream[bitPos++];
      if (!secondBit) {
        decode_recursive(start, half);
      } else {
        decode_recursive(start, half);
        decode_recursive(mid, half);
      }
    }
  };
  decode_recursive(0, HITMAP_SIZE);
  return hitmap;
}

void BitstreamToPixelProducer::convertToSensorCoordinates(std::vector<bool>& hitmap) {

  std::vector<bool> temp = hitmap;
  hitmap.assign(HITMAP_SIZE, false);
  for (size_t i = 0; i < HITMAP_SIZE; i++) {
    int rocRow = i / (HITMAP_SIZE/2);
    int rocCol = i % (HITMAP_SIZE/2);
    int sensorRow, sensorCol;
    if (rocCol % 2 == 0) {
      sensorRow = rocRow * 2;
      sensorCol = rocCol / 2;
    } else {
      sensorRow = rocRow * 2 + 1;
      sensorCol = (rocCol - 1) / 2;
    }
    int sensorIndex = sensorRow * (HITMAP_SIZE/4) + sensorCol;
    hitmap[sensorIndex] = temp[i];
  }
}

std::pair<int,int> BitstreamToPixelProducer::reverseHitmapIndexMapping(int index) {
  // Needed as 4x4 hitmap looks like, we have to invert the hitmap ordering
  //  8  9 10 11
  // 12 13 14 15
  //  0  1  2  3
  //  4  5  6  7
  int adjusted = (index + HITMAP_SIZE/2) % HITMAP_SIZE;
  int row_mod = adjusted / HITMAP_ROW;
  int col_mod = adjusted % HITMAP_COL;
  return {row_mod, col_mod};
}

void BitstreamToPixelProducer::decodeBitstream(const std::vector<bool>& bitstream, uint32_t detId, int chipId,
                                               edm::DetSetVector<PixelDigi>& outputDigis) {
  edm::DetSet<PixelDigi> detSet(detId);
  
  bool debug = detId == 303058948;
  if (debug) {
    std::cout << "Decoding bitstream for detId " << detId << ", chip " << chipId 
              << ", size: " << bitstream.size() << " bits" << std::endl;
    std::cout << "First 32 bits: " << getBitString(bitstream, 0, 32) << std::endl;
  }
  
  size_t bitPos = 0;
  int currentCol = 0;
  int currentRow = 0;
  int previousRow = -1;
  int previousCol = -1;
  bool previousIsLast = true; // initialize to true as the very first qcore also needs to read the column
  
  int qcoreCount = 0;
  while (bitPos < bitstream.size()) {
    if (previousIsLast) {
      currentCol = binaryToInt(bitstream, bitPos, 6);
      previousCol = currentCol;
      if (debug) {
        std::cout << "DEBUG: DetId " << detId << ", chip " << chipId
                  << " new QCore column = " << currentCol << std::endl;
      }
    }
    bool islast = bitstream[bitPos++];
    bool isneighbor = bitstream[bitPos++];
    
    if (isneighbor) {
      if (!previousIsLast)
        currentCol = previousCol + 1;
      currentRow = previousRow;
    } else {
      currentRow = binaryToInt(bitstream, bitPos, 8);
      previousRow = currentRow;
      if (debug) {
        std::cout << "DEBUG: DetId " << detId << ", chip " << chipId
                  << " new QCore row = " << currentRow << std::endl;
      }
    }
    
    std::vector<bool> hitmap = decodeHuffmanHitmap(bitstream, bitPos);
    int numHits = std::count(hitmap.begin(), hitmap.end(), true);
    
    // Extract ADC values (assume 4 bits per hit)
    std::vector<int> adcValues;
    for (int i = 0; i < numHits; i++) {
      int adc = binaryToInt(bitstream, bitPos, 4);
      adcValues.push_back(adc);
    }
    
    // Convert hitmap to sensor coordinates
    convertToSensorCoordinates(hitmap);
    int adcIndex = 0;
    for (int i = 0; i < HITMAP_COL; i++) {
      for (int j = 0; j < HITMAP_ROW; j++) {
        int hitIndex = i * HITMAP_ROW + j;
        auto mapping = reverseHitmapIndexMapping(hitIndex);
        if (hitIndex < static_cast<int>(hitmap.size()) && hitmap[hitIndex]) {
          int globalRow = currentRow * HITMAP_COL + mapping.first;
          int globalCol = (currentCol + 54 * chipId) * HITMAP_ROW + mapping.second;
          int adc = adcValues[adcIndex++];
          detSet.push_back(PixelDigi(globalRow, globalCol, adc));
          if (debug) {
            std::cout << "Created PixelDigi: row=" << globalRow << ", col=" << globalCol 
                      << ", adc=" << adc << std::endl;
          }
        }
      }
    }
    
    previousIsLast = islast;
    previousCol = currentCol;
    qcoreCount++;
    if (qcoreCount >= 20)
      debug = false;
  }
  
  if (!detSet.empty()) {
    outputDigis.insert(detSet);
  }
}

void BitstreamToPixelProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
  std::cout << "===========================================" <<std::endl;
  std::cout << "BitstreamToPixelProducer" <<std::endl;
  std::cout << "===========================================" <<std::endl;

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

