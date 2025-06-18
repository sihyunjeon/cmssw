// EDProducer that takes FEDRawData and produces ITChipBitStream
// Very first step of unpacker

#include <memory>
#include <vector>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <map>
#include <utility>
#include <numeric>

#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include "CondFormats/SiPhase2TrackerObjects/interface/TrackerDetToDTCELinkCablingMap.h"
#include "CondFormats/DataRecord/interface/TrackerDetToDTCELinkCablingMapRcd.h"

#include "DataFormats/Common/interface/DetSetVectorNew.h"

#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITChipBitStream.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2DAQFormatSpecification.h"

using namespace Phase2DAQFormatSpecification;

class RawToBitstreamProducer : public edm::stream::EDProducer<> {
public:
  explicit RawToBitstreamProducer(const edm::ParameterSet&);
  ~RawToBitstreamProducer() override = default;

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);
  void beginRun(const edm::Run&, const edm::EventSetup&) override;

private:
  void produce(edm::Event&, const edm::EventSetup&) override;

  uint16_t readWord(const unsigned char* dataPtr, int wordIdx) const;
  std::string wordToHexString(uint16_t word) const;
  std::string bitPattern(uint16_t word) const;
  void dumpMemorySection(const unsigned char* dataPtr, int startWord, int numWords) const;

  // Debugging functions for helper methods
  std::string getBitString(const std::vector<bool>& bits, size_t start, size_t len) const;
  void dumpBitstream(const std::vector<bool>& bits, size_t position) const;

  bool verifyHeaderTrailerPattern(const unsigned char* dataPtr, int wordIdx) const;
  int findTrailerStart(const unsigned char* dataPtr, int fedSizeInWords) const;
  std::vector<uint32_t> extractChipOffsets(const unsigned char* dataPtr, int offsetStartWord, int maxWords) const;
  std::vector<bool> extractBitstream(const unsigned char* dataPtr, int startWord, int bitstreamSize) const;

  void processFED(const unsigned char* dataPtr,
                  int fedSizeInWords,
                  int fedId,
                  int dtcId,
                  int slinkId,
                  edmNew::DetSetVector<Phase2ITChipBitStream>& output);
  void processChip(const unsigned char* dataPtr,
                   int chipStartWord,
                   int chipEndWord,
                   uint32_t detId,
                   int chipId,
                   edmNew::DetSetVector<Phase2ITChipBitStream>::FastFiller& filler);

  void buildFedToModuleMapping();

  const edm::EDGetTokenT<FEDRawDataCollection> fedRawDataToken_;
  const edm::ESGetToken<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd> cablingMapToken_;

  const TrackerDetToDTCELinkCablingMap* cablingMap_ = nullptr;

  // Cached module mapping: FED (calculated from DTC/SLink) -> vector of detector IDs
  std::map<int, std::map<int, std::vector<uint32_t>>> fedToModuleMap_;

  bool debug_ = false;
};

RawToBitstreamProducer::RawToBitstreamProducer(const edm::ParameterSet& iConfig)
    : fedRawDataToken_(consumes<FEDRawDataCollection>(iConfig.getParameter<edm::InputTag>("fedRawDataCollection"))),
      cablingMapToken_(
          esConsumes<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd, edm::Transition::BeginRun>()),
      debug_(iConfig.getUntrackedParameter<bool>("debug", false)) {
  produces<edmNew::DetSetVector<Phase2ITChipBitStream>>();
}

void RawToBitstreamProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("fedRawDataCollection", edm::InputTag("rawDataCollector"));
  desc.addUntracked<bool>("debug", false);
  descriptions.add("phase2RawToBitstreamProducer", desc);
}

void RawToBitstreamProducer::beginRun(const edm::Run& iRun, const edm::EventSetup& iSetup) {
  cablingMap_ = &iSetup.getData(cablingMapToken_);
  if (debug_) {
    std::cout << "RawToBitstreamProducer: Loaded cabling map in beginRun" << std::endl;
  }
  buildFedToModuleMapping();
}

void RawToBitstreamProducer::buildFedToModuleMapping() {
  auto knownDTCIdsWithIndex = cablingMap_->getKnownDTCIdsWithIndex();
  fedToModuleMap_.clear();
  for (const auto& pair : knownDTCIdsWithIndex) {
    unsigned int dtcIndex = pair.first;
    unsigned int dtcId = pair.second;
    auto detIds = cablingMap_->getAllDetIdsForDTCId(dtcId);
    if (detIds.empty())
      continue;
    int base_modules_per_slink = detIds.size() / SLINKS_PER_DTC;
    int remain_modules = detIds.size() % SLINKS_PER_DTC;
    int moduleIndex = 0;
    for (int slinkId = 0; slinkId < SLINKS_PER_DTC; slinkId++) {
      int fedId = dtcIndex * SLINKS_PER_DTC + slinkId;
      int modules_for_this_slink = base_modules_per_slink + ((slinkId < remain_modules) ? 1 : 0);
      if (modules_for_this_slink == 0)
        continue;
      std::vector<uint32_t> modulesForFed;
      for (int i = 0; i < modules_for_this_slink && moduleIndex < (int)detIds.size(); i++) {
        modulesForFed.push_back(detIds[moduleIndex++]);
      }
      fedToModuleMap_[fedId] = std::map<int, std::vector<uint32_t>>();
      fedToModuleMap_[fedId][slinkId] = modulesForFed;
    }
  }
  if (debug_) {
    std::cout << "Built FED to module mapping:" << std::endl;
    for (const auto& fedEntry : fedToModuleMap_) {
      int fedId = fedEntry.first;
      for (const auto& slinkEntry : fedEntry.second) {
        int slinkId = slinkEntry.first;
        const auto& modules = slinkEntry.second;
        std::cout << "  FED " << fedId << " (SLINK " << slinkId << "): " << modules.size() << " modules" << std::endl;
      }
    }
  }
}

void RawToBitstreamProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
  auto output = std::make_unique<edmNew::DetSetVector<Phase2ITChipBitStream>>();
  edm::Handle<FEDRawDataCollection> fedRawDataCollection;
  iEvent.getByToken(fedRawDataToken_, fedRawDataCollection);
  if (!fedRawDataCollection.isValid()) {
    std::cout << "ERROR: FEDRawDataCollection not found!" << std::endl;
    iEvent.put(std::move(output));
    return;
  }
  if (debug_) {
    std::cout << "\n===========================================" << std::endl;
    std::cout << "RawToBitstreamProducer: Starting unpacking event" << std::endl;
    std::cout << "===========================================" << std::endl;
  }

  for (const auto& fedEntry : fedToModuleMap_) {
    int fedId = fedEntry.first;
    const FEDRawData& fedData = fedRawDataCollection->FEDData(fedId);

    int slinkId = fedId % SLINKS_PER_DTC;
    int dtcIndex = fedId / SLINKS_PER_DTC;
    int dtcId = ((dtcIndex / 9) + 1) * 10 + (dtcIndex % 9) + 1;
    const unsigned char* dataPtr = fedData.data();
    int fedSizeInWords = fedData.size() / 2;
    if (debug_) {
      std::cout << "\n----- Processing FED ID " << fedId << " (DTC=" << dtcId << ", SLINK=" << slinkId
                << "), size: " << fedData.size() << " bytes (" << fedSizeInWords << " words)" << std::endl;
    }
    processFED(dataPtr, fedSizeInWords, fedId, dtcId, slinkId, *output);
  }
  if (debug_) {
    std::cout << "\nProduced " << output->size() << " DetSets of Phase2ITChipBitStream" << std::endl;
  }
  iEvent.put(std::move(output));
}

void RawToBitstreamProducer::processFED(const unsigned char* dataPtr,
                                        int fedSizeInWords,
                                        int fedId,
                                        int dtcId,
                                        int slinkId,
                                        edmNew::DetSetVector<Phase2ITChipBitStream>& output) {
  const std::vector<uint32_t>& detIds = fedToModuleMap_[fedId][slinkId];
  bool validHeader = verifyHeaderTrailerPattern(dataPtr, 0);
  if (!validHeader) {
    std::cout << "ERROR: Invalid header pattern in FED " << fedId << ", skipping" << std::endl;
    return;
  }
  if (debug_) {
    std::cout << "Header section:" << std::endl;
    dumpMemorySection(dataPtr, 0, HEADER_TRAILER_LINES);
  }
  int trailerStart = findTrailerStart(dataPtr, fedSizeInWords);
  if (trailerStart < 0) {
    std::cout << "WARNING: Could not find trailer in FED " << fedId << ", assuming it ends with the FED data"
              << std::endl;
    trailerStart = fedSizeInWords;
  } else if (debug_) {
    std::cout << "Found trailer at word " << trailerStart << std::endl;
    std::cout << "Trailer section:" << std::endl;
    dumpMemorySection(dataPtr, trailerStart, std::min(HEADER_TRAILER_LINES, fedSizeInWords - trailerStart));
  }

  int offsetStart = HEADER_TRAILER_LINES;
  std::vector<uint32_t> chipOffsets = extractChipOffsets(dataPtr, offsetStart, trailerStart - offsetStart);
  if (debug_) {
    std::cout << "Found " << chipOffsets.size() << " chip offsets:" << std::endl;
    for (size_t i = 0; i < chipOffsets.size() && i < 5; i++) {
      std::cout << "  Offset " << i << ": " << chipOffsets[i] << " words" << std::endl;
    }
    if (chipOffsets.size() > 5) {
      std::cout << "  (... and " << (chipOffsets.size() - 5) << " more)" << std::endl;
    }
  }
  int offsetBlockSize = chipOffsets.size() * 2;
  int offsetBits = offsetBlockSize * BITS_PER_WORD;
  int paddingBits = (BITS_PER_CHUNK - (offsetBits % BITS_PER_CHUNK)) % BITS_PER_CHUNK;
  int paddingWords = paddingBits / BITS_PER_WORD;
  int dataBlockStart = offsetStart + offsetBlockSize + paddingWords;
  if (debug_) {
    std::cout << "Offset block: " << offsetBlockSize << " words (with " << paddingWords << " padding words)"
              << std::endl;
    std::cout << "Data block starts at word " << dataBlockStart << std::endl;
  }
  int numChips = chipOffsets.size();
  int numModules = detIds.size();
  const int chipsPerModule = 4;
  if (debug_) {
    std::cout << "Found " << numChips << " chips across " << numModules << " modules (approx. " << chipsPerModule
              << " chips per module)" << std::endl;
  }

  std::map<uint32_t, std::vector<std::pair<int, int>>> chipsByDetId;

  for (int chipIdx = 0; chipIdx < numChips; chipIdx++) {
    int moduleIdx = chipIdx / chipsPerModule;
    if (moduleIdx >= numModules)
      moduleIdx = numModules - 1;
    uint32_t detId = detIds[moduleIdx];
    int chipIdInModule = chipIdx % chipsPerModule;
    int chipStartWord = dataBlockStart + chipOffsets[chipIdx];
    int chipEndWord = (chipIdx == numChips - 1) ? trailerStart : dataBlockStart + chipOffsets[chipIdx + 1];

    // Store chip info for this detector ID
    chipsByDetId[detId].push_back(std::make_pair(chipStartWord, chipEndWord));
  }

  // FIXME splitted the top and bottom for easier debugging for now

  // Process all chips for each detector ID at once
  for (const auto& detIdEntry : chipsByDetId) {
    uint32_t detId = detIdEntry.first;
    const auto& chipInfos = detIdEntry.second;

    edmNew::DetSetVector<Phase2ITChipBitStream>::FastFiller filler(output, detId);

    for (size_t i = 0; i < chipInfos.size(); i++) {
      int chipStartWord = chipInfos[i].first;
      int chipEndWord = chipInfos[i].second;
      int chipIdInModule = i % chipsPerModule;

      processChip(dataPtr, chipStartWord, chipEndWord, detId, chipIdInModule, filler);
    }
  }
}

void RawToBitstreamProducer::processChip(const unsigned char* dataPtr,
                                         int chipStartWord,
                                         int chipEndWord,
                                         uint32_t detId,
                                         int chipId,
                                         edmNew::DetSetVector<Phase2ITChipBitStream>::FastFiller& filler) {
  bool debug = (detId == 303058948);
  if (chipEndWord <= chipStartWord) {
    std::cout << "WARNING: Invalid chip data size (start=" << chipStartWord << ", end=" << chipEndWord << "), skipping"
              << std::endl;
    return;
  }
  if (debug) {
    std::cout << "\nUNPACKER: Processing chip " << chipId << " of detector " << detId << std::endl;
    std::cout << "  Chip data: words " << chipStartWord << " to " << chipEndWord << " ("
              << (chipEndWord - chipStartWord) << " words)" << std::endl;
  }
  uint16_t header1 = readWord(dataPtr, chipStartWord);
  if ((header1 & 0xF000) != CHIP_HEADER_MARKER) {
    std::cout << "WARNING: Invalid chip header " << wordToHexString(header1) << " at word " << chipStartWord
              << ", skipping" << std::endl;
    return;
  }
  uint8_t paddingBits = header1 & 0xF;
  uint16_t bitstreamSize = readWord(dataPtr, chipStartWord + 1);
  if (debug) {
    std::cout << "  Header 1: " << wordToHexString(header1) << " (marker 0x" << std::hex << (header1 & 0xF000)
              << std::dec << " + padding " << (int)paddingBits << " bits)" << std::endl;
    std::cout << "  Header 2 (bitstream size): " << bitstreamSize << " bits" << std::endl;
  }
  std::vector<bool> bitstream = extractBitstream(dataPtr, chipStartWord + 2, bitstreamSize);
  if (debug) {
    std::cout << "UNPACKER BITSTREAM: ";
    for (bool bit : bitstream) {
      std::cout << (bit ? '1' : '0');
    }
    std::cout << std::endl;
    std::cout << "UNPACKER BITSTREAM LENGTH: " << bitstream.size() << " bits" << std::endl;
  }

  // Create the Phase2ITChipBitStream object and add it to the filler
  Phase2ITChipBitStream chipStream(chipId, bitstream);
  filler.push_back(chipStream);

  if (debug) {
    std::cout << "Added Phase2ITChipBitStream for chip " << chipId << " to filler" << std::endl;
  }
}

std::string RawToBitstreamProducer::getBitString(const std::vector<bool>& bits, size_t start, size_t len) const {
  std::string result;
  for (size_t i = 0; i < len && start + i < bits.size(); i++) {
    result += (bits[start + i] ? "1" : "0");
    if ((i + 1) % 8 == 0)
      result += " ";
  }
  return result;
}

void RawToBitstreamProducer::dumpBitstream(const std::vector<bool>& bits, size_t position) const {
  std::cout << "BITSTREAM at position " << position << ": ";
  size_t start = (position > 8) ? position - 8 : 0;
  size_t end = std::min(bits.size(), position + 16);
  for (size_t i = start; i < end; i++) {
    if (i == position)
      std::cout << "|";
    std::cout << (bits[i] ? "1" : "0");
    if ((i + 1) % 8 == 0)
      std::cout << " ";
  }
  std::cout << std::endl;
}

uint16_t RawToBitstreamProducer::readWord(const unsigned char* dataPtr, int wordIdx) const {
  int byteIdx = wordIdx * 2;
  return (static_cast<uint16_t>(dataPtr[byteIdx]) << 8) | static_cast<uint16_t>(dataPtr[byteIdx + 1]);
}

std::string RawToBitstreamProducer::wordToHexString(uint16_t word) const {
  std::stringstream ss;
  ss << "0x" << std::hex << std::setw(4) << std::setfill('0') << word;
  return ss.str();
}

std::string RawToBitstreamProducer::bitPattern(uint16_t word) const {
  std::string result;
  for (int i = 15; i >= 0; i--) {
    result += ((word >> i) & 1) ? '1' : '0';
    if (i % 4 == 0 && i > 0)
      result += ' ';
  }
  return result;
}

void RawToBitstreamProducer::dumpMemorySection(const unsigned char* dataPtr, int startWord, int numWords) const {
  std::cout << "\n=== Memory dump from word " << startWord << " to " << (startWord + numWords - 1)
            << " ===" << std::endl;
  for (int i = 0; i < numWords; i++) {
    uint16_t word = readWord(dataPtr, startWord + i);
    std::cout << "Word[" << std::setw(4) << startWord + i << "]: " << wordToHexString(word) << " (" << bitPattern(word)
              << ")" << std::endl;
    if ((i + 1) % 8 == 0) {
      std::cout << "----------------------" << std::endl;
    }
  }
}

std::vector<uint32_t> RawToBitstreamProducer::extractChipOffsets(const unsigned char* dataPtr,
                                                                 int offsetStartWord,
                                                                 int maxWords) const {
  std::vector<uint32_t> offsets;
  std::cout << "  Analyzing offset block from word " << offsetStartWord << ":" << std::endl;
  int words_per_chunk = BITS_PER_CHUNK / BITS_PER_WORD;  // 8 words per chunk
  for (int i = 0; i < maxWords - 1; i += 2) {
    if (offsetStartWord + i + 1 >= maxWords)
      break;
    uint16_t msb = readWord(dataPtr, offsetStartWord + i);
    uint16_t lsb = readWord(dataPtr, offsetStartWord + i + 1);
    uint32_t offset = (static_cast<uint32_t>(msb) << 16) | lsb;
    std::cout << "    Pair " << i / 2 << ": " << wordToHexString(msb) << " " << wordToHexString(lsb)
              << " -> Offset: " << offset << std::endl;
    offsets.push_back(offset);
    if ((i + 2) % words_per_chunk == 0) {
      if (offsetStartWord + i + 2 < maxWords) {
        uint16_t nextWord = readWord(dataPtr, offsetStartWord + i + 2);
        if ((nextWord & 0xF000) == CHIP_HEADER_MARKER) {
          std::cout << "    Found chip header marker " << wordToHexString(nextWord) << " at word "
                    << (offsetStartWord + i + 2) << ", ending offset extraction" << std::endl;
          break;
        }
      }
    }
    if (msb == HEADER_TRAILER_PATTERN && lsb == HEADER_TRAILER_PATTERN) {
      std::cout << "    Found trailer pattern at word " << (offsetStartWord + i) << ", ending offset extraction"
                << std::endl;
      break;
    }
  }
  return offsets;
}

std::vector<bool> RawToBitstreamProducer::extractBitstream(const unsigned char* dataPtr,
                                                           int startWord,
                                                           int bitstreamSize) const {
  std::vector<bool> bitstream;
  bitstream.reserve(bitstreamSize);
  int fullWords = bitstreamSize / BITS_PER_WORD;
  int remainingBits = bitstreamSize % BITS_PER_WORD;
  for (int i = 0; i < fullWords; i++) {
    uint16_t word = readWord(dataPtr, startWord + i);
    for (int j = 0; j < BITS_PER_WORD; j++) {
      bool bit = (word >> (15 - j)) & 1;
      bitstream.push_back(bit);
    }
  }
  if (remainingBits > 0) {
    uint16_t word = readWord(dataPtr, startWord + fullWords);
    for (int j = 0; j < remainingBits; j++) {
      bool bit = (word >> (15 - j)) & 1;
      bitstream.push_back(bit);
    }
  }
  if (debug_)
    std::cout << "UNPACKER: First 32 bits of extracted bitstream: " << getBitString(bitstream, 0, 32) << std::endl;
  return bitstream;
}

// FIXME for now this works because we are assuming 8 lines of 0xFFFF for both headers and trailers
// Later we have to come up with something more concrete to parse out these 8 lines
bool RawToBitstreamProducer::verifyHeaderTrailerPattern(const unsigned char* dataPtr, int wordIdx) const {
  for (int i = 0; i < HEADER_TRAILER_LINES; i++) {
    uint16_t word = readWord(dataPtr, wordIdx + i);
    if (word != HEADER_TRAILER_PATTERN) {
      return false;
    }
  }
  return true;
}

int RawToBitstreamProducer::findTrailerStart(const unsigned char* dataPtr, int fedSizeInWords) const {
  // Start searching from the end, going backwards
  for (int i = fedSizeInWords - HEADER_TRAILER_LINES; i >= HEADER_TRAILER_LINES; --i) {
    if (verifyHeaderTrailerPattern(dataPtr, i)) {
      return i;
    }
  }
  return -1;  // trailer not found
}

DEFINE_FWK_MODULE(RawToBitstreamProducer);
