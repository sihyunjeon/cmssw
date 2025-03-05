// EDProducer unpacking the raw data from innertracker pixel
#include "DataFormats/Common/interface/DetSetVectorNew.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Utilities/interface/InputTag.h"

#include <vector>
#include <map>
#include <unordered_map>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <string>

#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITChipBitStream.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "CondFormats/SiPhase2TrackerObjects/interface/TrackerDetToDTCELinkCablingMap.h"
#include "CondFormats/DataRecord/interface/TrackerDetToDTCELinkCablingMapRcd.h"

class RawToPixelProducer : public edm::stream::EDProducer<> {
public:
    explicit RawToPixelProducer(const edm::ParameterSet&);
    ~RawToPixelProducer() override = default;
    void beginRun(const edm::Run&, const edm::EventSetup&) override;

private:
    void produce(edm::Event&, const edm::EventSetup&) override;
    unsigned int indexingDTC(unsigned int dtc_id) const;
    void buildSLinkToModulesMap(const TrackerDetToDTCELinkCablingMap& cablingMap);
    void decodeFEDData(const FEDRawData& fedData, int fedId, edmNew::DetSetVector<Phase2ITChipBitStream>& output);
    void printBitVector(const std::vector<bool>& bits, const std::string& label, size_t startBit = 0) const;

    const edm::EDGetTokenT<FEDRawDataCollection> fedDataToken_;
    const edm::ESGetToken<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd> cablingMapToken_;
    std::map<int, std::vector<uint32_t>> slinkToModulesMap_;
    static constexpr int SLINKS_PER_DTC = 16;
};

RawToPixelProducer::RawToPixelProducer(const edm::ParameterSet& iConfig) :
    fedDataToken_(consumes<FEDRawDataCollection>(iConfig.getParameter<edm::InputTag>("fedData"))),
    cablingMapToken_(esConsumes<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd, edm::Transition::BeginRun>()) {
    produces<edmNew::DetSetVector<Phase2ITChipBitStream>>();
}

void RawToPixelProducer::printBitVector(const std::vector<bool>& bits, const std::string& label, size_t startBit) const {
    std::cout << "\n=== " << label << " ===" << std::endl;
    std::cout << "Total bits: " << bits.size() << std::endl;
    
    for (size_t i = 0; i < bits.size(); i += 16) {
        if (i + 15 >= bits.size()) break;
        
        uint16_t value = 0;
        for (int j = 0; j < 16; j++) {
            if (bits[i + j]) {
                value |= (1 << (15 - j));
            }
        }
        
        std::cout << "Bits[" << std::setw(4) << (startBit + i) << "]: 0x" 
                 << std::hex << std::setw(4) << std::setfill('0') << value 
                 << "  Binary: ";
        
        for (int j = 0; j < 16; j++) {
            std::cout << (bits[i + j] ? "1" : "0");
            if ((j + 1) % 4 == 0) std::cout << " ";
        }
        std::cout << std::dec << std::endl;
    }
}

void RawToPixelProducer::beginRun(const edm::Run&, const edm::EventSetup& iSetup) {
    const auto& cablingMap = iSetup.getData(cablingMapToken_);
    buildSLinkToModulesMap(cablingMap);
}

void RawToPixelProducer::buildSLinkToModulesMap(const TrackerDetToDTCELinkCablingMap& cablingMap) {
    slinkToModulesMap_.clear();
    // first building the cabling map that defines which module is stored in which slink
    for (unsigned int hwDtcId = 11; hwDtcId < 50; hwDtcId++) {
        if (hwDtcId % 10 == 0) continue;
        
        unsigned int dtc_index = indexingDTC(hwDtcId);
        auto detIds = cablingMap.getAllDetIdsForDTCId(hwDtcId);
        int totalModules = detIds.size();

        int base_slinks = totalModules / SLINKS_PER_DTC;
        int remain_slinks = totalModules % SLINKS_PER_DTC;
        int modIndex = 0;
        
        for (int i_slink = 0; i_slink < SLINKS_PER_DTC; i_slink++) {
            int slinkGlobal = dtc_index * SLINKS_PER_DTC + i_slink;
            int n_to_fill = base_slinks + ((i_slink < remain_slinks) ? 1 : 0);
            
            for (int m = 0; m < n_to_fill; m++) {
                if (modIndex >= totalModules) break;
                slinkToModulesMap_[slinkGlobal].push_back(detIds[modIndex++]);
            }
        }
    }
}

void RawToPixelProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
    auto output = std::make_unique<edmNew::DetSetVector<Phase2ITChipBitStream>>();
    
    edm::Handle<FEDRawDataCollection> fedRawDataCollection;
    iEvent.getByToken(fedDataToken_, fedRawDataCollection);

    for (const auto& [slinkGlobal, detIds] : slinkToModulesMap_) {
        const FEDRawData& slinkData = fedRawDataCollection->FEDData(slinkGlobal);
        if (slinkData.size() == 0) continue;
        decodeFEDData(slinkData, slinkGlobal, *output);
    }

    iEvent.put(std::move(output));
}

unsigned int RawToPixelProducer::indexingDTC(unsigned int dtc_id) const {
    unsigned int first = dtc_id / 10;
    unsigned int second = dtc_id % 10;
    return (10 * (first - 1) + second - first);
}

void RawToPixelProducer::decodeFEDData(const FEDRawData& fedData, int fedId, edmNew::DetSetVector<Phase2ITChipBitStream>& output) {
    if (fedData.size() == 0) return;
    
    const unsigned char* data = fedData.data();
    const size_t totalSize = fedData.size();
    const auto& modules = slinkToModulesMap_[fedId];
    const size_t numModules = modules.size();
    
    std::cout << "\n=== Decoding FED " << fedId << " ===" << std::endl;
    std::cout << "Total size: " << totalSize << " bytes" << std::endl;
    std::cout << "Number of modules: " << numModules << std::endl;
    
    // Calculate offset block size
    size_t rawOffsetSize = numModules * 4;  // 4 bytes per module (2x16b words)
    size_t offsetPadding = (16 - (rawOffsetSize % 16)) % 16;
    size_t totalOffsetSize = rawOffsetSize + offsetPadding;
    size_t dataSize = totalSize - totalOffsetSize;
    const unsigned char* dataBlock = data + totalOffsetSize;
    
    // Convert offset block to bits for consistent processing
    std::vector<bool> offsetBits;
    for (size_t byte = 0; byte < totalOffsetSize; byte++) {
        for (int bit = 7; bit >= 0; bit--) {
            offsetBits.push_back((data[byte] >> bit) & 0x1);
        }
    }
    
    // Read offsets
    std::vector<size_t> offsets;
    for (size_t i = 0; i < numModules; i++) {
        // Each module has 2x16 bits (MSB, LSB) in the offset block
        uint16_t msb = 0, lsb = 0;
        for (int j = 0; j < 16; j++) {
            if (offsetBits[i*32 + j]) msb |= (1 << (15-j));
            if (offsetBits[i*32 + 16 + j]) lsb |= (1 << (15-j));
        }
        
        size_t offset = static_cast<size_t>(msb) << 16 | static_cast<size_t>(lsb);
        offsets.push_back(offset);
        
        std::cout << "Module " << i << " offset block:" << std::endl;
        std::cout << "  MSB: 0x" << std::hex << std::setw(4) << std::setfill('0') << msb 
                 << ", LSB: 0x" << std::setw(4) << std::setfill('0') << lsb 
                 << ", Offset: " << std::dec << offset << " chunks" << std::endl;
    }
    
    // Process each module
    for (size_t i = 0; i < numModules; i++) {
        size_t startChunk = offsets[i];
        size_t endChunk = (i < numModules-1) ? offsets[i+1] : (dataSize / 16);
        size_t startByte = startChunk * 16;
        size_t endByte = endChunk * 16;
        
        std::cout << "\n=== Module " << i << " (DetId: " << modules[i] << ") ===" << std::endl;
        std::cout << "Starting at byte: " << startByte << std::endl;
        
        // Convert to bits
        std::vector<bool> moduleBits;
        for (size_t byte = startByte; byte < endByte && byte < dataSize; byte++) {
            uint8_t currentByte = dataBlock[byte];
            for (int bit = 7; bit >= 0; bit--) {
                moduleBits.push_back((currentByte >> bit) & 0x1);
            }
        }
        
        //printBitVector(moduleBits, "Module Raw Data");
        
        // Process chips
        size_t bitPos = 0;
        int chipCount = 0;
        edmNew::DetSetVector<Phase2ITChipBitStream>::FastFiller filler(output, modules[i]);
        size_t lastProcessedBit = 0;  // Track the last processed bit position

        while (bitPos + 32 <= moduleBits.size() && chipCount < 4) {
            uint16_t header1 = 0; // first line of chip level header
            uint16_t header2 = 0; // second line of chip level header (telling you the size)
            
            for (int j = 0; j < 16; j++) {
                header1 = (header1 << 1) | moduleBits[bitPos + j];
                header2 = (header2 << 1) | moduleBits[bitPos + 16 + j];
            }
            
            if ((header1 & 0xF000) == 0xE000) {
                uint8_t padding = header1 & 0xF;
                uint16_t dataSize = header2;
                
                std::cout << "\nFound chip " << chipCount << " at bit " << bitPos << std::endl;
                std::cout << "  Header1: 0x" << std::hex << std::setw(4) << std::setfill('0') << header1 
                         << " (padding: " << std::dec << (int)padding << " bits)" << std::endl;
                std::cout << "  Header2: 0x" << std::hex << std::setw(4) << std::setfill('0') << header2 
                         << " (size: " << std::dec << dataSize << " bits)" << std::endl;
                
                std::vector<bool> chipData;
                for (size_t k = 0; k < dataSize && (bitPos + 32 + k) < moduleBits.size(); k++) {
                    chipData.push_back(moduleBits[bitPos + 32 + k]);
                }
                
                // printBitVector(chipData, "Chip Data", bitPos + 32);
                
                size_t totalChipSize = 32 + dataSize;
                bitPos += totalChipSize + padding;
                lastProcessedBit = bitPos;  // Update last processed position
                
                Phase2ITChipBitStream chipStream(chipCount++, chipData);
                filler.push_back(chipStream);
            } else {
                bitPos += 16;
            }
        }
        
        if (chipCount >= 4) {
            std::cout << "\nFound 4 chips for module " << i << std::endl;
            std::cout << "Last processed bit: " << lastProcessedBit << std::endl;
            // Update offset for next module
            if (i < numModules - 1) {
                offsets[i + 1] = startChunk + (lastProcessedBit / 128);
            }
        } else {
            std::cout << "\nWarning: Only found " << chipCount << " chips in module " << i << std::endl;
        }
    }
}

DEFINE_FWK_MODULE(RawToPixelProducer);
