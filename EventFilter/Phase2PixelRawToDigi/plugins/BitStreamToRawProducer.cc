// EDProducer that takes bitstream to FEDRawData (Packer)

#include <memory>

#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/one/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include "CondFormats/SiPhase2TrackerObjects/interface/TrackerDetToDTCELinkCablingMap.h"
#include "CondFormats/DataRecord/interface/TrackerDetToDTCELinkCablingMapRcd.h"

#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITChipBitStream.h"
#include "DataFormats/Common/interface/DetSetVector.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "DataFormats/FEDRawData/interface/FEDRawData.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2DAQFormatSpecification.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/SLinkModuleMap.h"

using namespace Phase2DAQFormatSpecification;

class BitStreamToRawProducer : public edm::one::EDProducer<edm::one::WatchRuns> {
public:
  explicit BitStreamToRawProducer(const edm::ParameterSet&);
  ~BitStreamToRawProducer() override = default;

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);
  void beginRun(const edm::Run&, const edm::EventSetup&) override;
  void endRun(const edm::Run&, const edm::EventSetup&) override {}
  void produce(edm::Event&, const edm::EventSetup&) override;

private:
  const edm::ESGetToken<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd> cablingMapToken_;
  const edm::EDGetTokenT<edm::DetSetVector<Phase2ITChipBitStream>> ITChipBitStreamToken_;

  void addWordToBuffer(unsigned char* buffer, size_t position, uint32_t word);
  void addWordToBitVector(std::vector<bool>& vec, uint32_t word);
  void padToChunkBoundary(std::vector<bool>& vec);

  std::unique_ptr<SLinkModuleMap> slinkMap_;
};

BitStreamToRawProducer::BitStreamToRawProducer(const edm::ParameterSet& iConfig)
    : cablingMapToken_(
          esConsumes<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd, edm::Transition::BeginRun>()),
      ITChipBitStreamToken_(consumes<edm::DetSetVector<Phase2ITChipBitStream>>(
          iConfig.getParameter<edm::InputTag>("Phase2ITChipBitStream"))) {
  produces<FEDRawDataCollection>();
}

void BitStreamToRawProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("Phase2ITChipBitStream", edm::InputTag("PixelQCoreProducer"));
  descriptions.add("pixelToRawProducer", desc);
}

void BitStreamToRawProducer::beginRun(const edm::Run& iRun, const edm::EventSetup& iSetup) {
  slinkMap_ = std::make_unique<SLinkModuleMap>(iSetup.getData(cablingMapToken_));
}

void BitStreamToRawProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
  using namespace edm;
  using namespace std;

  auto fedRawDataCollection = std::make_unique<FEDRawDataCollection>();
  edm::Handle<edm::DetSetVector<Phase2ITChipBitStream>> handle;
  iEvent.getByToken(ITChipBitStreamToken_, handle);

  if (!handle.isValid()) {
    throw cms::Exception("BitStreamToRawProducer") << "Invalid BitStream handle";
  }

  // Loop over slinks to fill in the bitstreams
  for (const auto& entry : slinkMap_->fedIdToDetIds()) {
    int fedId = entry.first;
    const std::vector<uint32_t>& detIds = entry.second;

    // Block 2: per-module offsets.
    // Block 3: per-module sequence of (chip header word, chip bitstream, 32-bit pad) repeated for CHIPS_PER_MODULE chips
    std::vector<bool> offsetBlock;
    std::vector<bool> dataBlock;

    for (uint32_t detId : detIds) {
      auto foundDetId = handle->find(detId);
      if (foundDetId == handle->end()) {
        throw cms::Exception("BitStreamToRawProducer") << "Could not find detId from the inputs";
      }
      const edm::DetSet<Phase2ITChipBitStream>& detSet = *foundDetId;

      // Module-level offset = dataBlock position for module in 32-bit words.
      // FIXME For the first module the value is always 0, is it necessary?
      uint32_t moduleOffset = dataBlock.size() / BITS_PER_WORD;
      addWordToBitVector(offsetBlock, moduleOffset);

      for (auto const& chip : detSet) {
        std::vector<bool> chipBitStream = chip.get_bitstream();
        unsigned int bitstreamSize = chipBitStream.size();
        unsigned int sizeWords = (bitstreamSize + BITS_PER_WORD - 1) / BITS_PER_WORD;
        unsigned int endBit = bitstreamSize % BITS_PER_WORD;  // 0 means no last word : full or empty

        // Per-chip header word:
        //   bits 31..28 : magic = 0xE
        //   bits 27..24 : error flags FIXME dummy given for now
        //   bits 23..21 : reserved
        //   bits 20..16 : end bit number (bits used in last 32-bit word)
        //   bits 15..0  : chip bitstream size in 32-bit words, NOT PER MODULE
        uint32_t chipHeader = ((CHIP_HEADER_MAGIC & 0xF) << 28) | ((0u & 0xF) << 24) | ((0u & 0x7) << 21) |
                              ((endBit & 0x1F) << 16) | (sizeWords & 0xFFFF);
        addWordToBitVector(dataBlock, chipHeader);

        dataBlock.insert(dataBlock.end(), chipBitStream.begin(), chipBitStream.end());

        // Pad chip bitstream to 32-bit boundary
        unsigned int chipPadBits = (endBit > 0) ? (BITS_PER_WORD - endBit) : 0;
        if (chipPadBits > 0) {
          dataBlock.insert(dataBlock.end(), chipPadBits, false);
        }
      }

      // Pad module to 128-bit boundary at module end
      padToChunkBoundary(dataBlock);
    }

    // Pad offset block to 128-bit boundary
    padToChunkBoundary(offsetBlock);

    // Calculate sizes in bytes
    unsigned int headerSize = HEADER_TRAILER_LINES * BYTES_PER_WORD;
    unsigned int offsetSize = offsetBlock.size() / BITS_PER_WORD * BYTES_PER_WORD;
    unsigned int dataSize = dataBlock.size() / BITS_PER_WORD * BYTES_PER_WORD;
    unsigned int trailerSize = HEADER_TRAILER_LINES * BYTES_PER_WORD;
    unsigned int totalSize = headerSize + offsetSize + dataSize + trailerSize;

    FEDRawData& slinkData = fedRawDataCollection->FEDData(fedId);
    slinkData.resize(totalSize);
    unsigned char* buffer = slinkData.data();

    // Header (4 lines of 0xFFFFFFFF) FIXME Dummy given for now
    for (int i = 0; i < HEADER_TRAILER_LINES; i++) {
      addWordToBuffer(buffer, i, HEADER_TRAILER_PATTERN);
    }

    // Offset block
    unsigned int offsetWords = offsetBlock.size() / BITS_PER_WORD;
    for (unsigned int i = 0; i < offsetWords; i++) {
      uint32_t word = 0;
      for (int bit = 0; bit < BITS_PER_WORD; bit++) {
        if (offsetBlock[i * BITS_PER_WORD + bit]) {
          word |= (1 << (31 - bit));
        }
      }
      addWordToBuffer(buffer, HEADER_TRAILER_LINES + i, word);
    }

    // Data block
    unsigned int dataWords = dataBlock.size() / BITS_PER_WORD;
    for (unsigned int i = 0; i < dataWords; i++) {
      uint32_t word = 0;
      for (int bit = 0; bit < BITS_PER_WORD; bit++) {
        if (dataBlock[i * BITS_PER_WORD + bit]) {
          word |= (1 << (31 - bit));
        }
      }
      addWordToBuffer(buffer, HEADER_TRAILER_LINES + offsetWords + i, word);
    }

    // Trailer (4 lines of 0xFFFFFFFF) FIXME dummy given for now
    for (int i = 0; i < HEADER_TRAILER_LINES; i++) {
      addWordToBuffer(buffer, HEADER_TRAILER_LINES + offsetWords + dataWords + i, HEADER_TRAILER_PATTERN);
    }
  }

  iEvent.put(std::move(fedRawDataCollection));
}

void BitStreamToRawProducer::addWordToBuffer(unsigned char* buffer, size_t position, uint32_t word) {
  buffer[position * 4]     = (word >> 24) & 0xFF;
  buffer[position * 4 + 1] = (word >> 16) & 0xFF;
  buffer[position * 4 + 2] = (word >>  8) & 0xFF;
  buffer[position * 4 + 3] =  word        & 0xFF;
}

// Append a 32-bit word to a bit vector
void BitStreamToRawProducer::addWordToBitVector(std::vector<bool>& vec, uint32_t word) {
  for (int bit = 31; bit >= 0; bit--) {
    vec.push_back((word >> bit) & 1);
  }
}

// Pad bit vector with zeros up to the next 128-bit chunk boundary
void BitStreamToRawProducer::padToChunkBoundary(std::vector<bool>& vec) {
  if (!vec.empty() && vec.size() % BITS_PER_CHUNK != 0) {
    size_t paddingNeeded = BITS_PER_CHUNK - (vec.size() % BITS_PER_CHUNK);
    vec.insert(vec.end(), paddingNeeded, false);
  }
}

DEFINE_FWK_MODULE(BitStreamToRawProducer);
