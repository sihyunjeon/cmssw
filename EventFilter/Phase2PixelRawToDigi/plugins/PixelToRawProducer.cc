#include <utility>
#include <unordered_map>
#include <string>
#include <iostream>
#include <iomanip>
#include <array>
#include <cstring>
#include <algorithm>
#include <bitset>

#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/one/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include "Geometry/CommonDetUnit/interface/PixelGeomDetUnit.h"

#include "CondFormats/SiPhase2TrackerObjects/interface/TrackerDetToDTCELinkCablingMap.h"
#include "CondFormats/SiPhase2TrackerObjects/interface/DTCELinkId.h"
#include "CondFormats/DataRecord/interface/TrackerDetToDTCELinkCablingMapRcd.h"

#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITChipBitStream.h"
#include "DataFormats/Common/interface/DetSetVector.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "DataFormats/FEDRawData/interface/FEDRawData.h"
#include "DataFormats/FEDRawData/interface/FEDHeader.h"
#include "DataFormats/FEDRawData/interface/FEDTrailer.h"

class PixelToRawProducer : public edm::one::EDProducer<> {
public:
    explicit PixelToRawProducer(const edm::ParameterSet&);
    ~PixelToRawProducer() override;
    
    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

private:
    void produce(edm::Event&, const edm::EventSetup&) override;
    
    const edm::ESGetToken<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd> cablingMapToken_;
    const edm::EDGetTokenT<edm::DetSetVector<Phase2ITChipBitStream>> ITChipBitStreamToken_;

    unsigned int calculateDTCIndex(unsigned int dtc_id);
    void addWordToBuffer(unsigned char* buffer, size_t position, uint16_t word);
    void addWordToBitVector(std::vector<bool>& vec, uint16_t word, bool debug = false);
    void printBitVectorAs16bit(const std::vector<bool>& bits, const std::string& label);
    void padToChunkBoundary(std::vector<bool>& vec);
    uint16_t calculateChipOffset(const std::vector<bool>& dataBlock);
    std::string getBitString(const std::vector<bool>& bits, size_t start, size_t len);

    static constexpr int SLINKS_PER_DTC = 16;
    static constexpr int MIN_DTC_ID = 11;
    static constexpr int MAX_DTC_ID = 49;
    static constexpr uint16_t CHIP_HEADER_MARKER = 0xE000;
    static constexpr uint16_t HEADER_TRAILER_PATTERN = 0xFFFF;
    static constexpr int HEADER_TRAILER_LINES = 8;
    static constexpr int BITS_PER_WORD = 16;
    static constexpr int BITS_PER_CHUNK = 128;
    static constexpr int BYTES_PER_WORD = 2;
};

PixelToRawProducer::PixelToRawProducer(const edm::ParameterSet& iConfig)
    : cablingMapToken_(esConsumes<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd>()),
      ITChipBitStreamToken_(consumes<edm::DetSetVector<Phase2ITChipBitStream>>(
          iConfig.getParameter<edm::InputTag>("Phase2ITChipBitStream"))) {
    
    produces<FEDRawDataCollection>();
}

PixelToRawProducer::~PixelToRawProducer() {}

std::string PixelToRawProducer::getBitString(const std::vector<bool>& bits, size_t start, size_t len) {
    std::string result;
    for (size_t i = 0; i < len && start + i < bits.size(); i++) {
        result += (bits[start + i] ? "1" : "0");
        if ((i + 1) % 8 == 0) result += " ";
    }
    return result;
}

void PixelToRawProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
    using namespace edm;
    using namespace std;

    const auto& cablingMap = iSetup.getData(cablingMapToken_);
    auto fedRawDataCollection = std::make_unique<FEDRawDataCollection>();
    edm::Handle<edm::DetSetVector<Phase2ITChipBitStream>> handle;
    iEvent.getByToken(ITChipBitStreamToken_, handle);

    std::cout << "PACKER: Received " << handle->size() << " detectors with chip bitstreams" << std::endl;

    // Loop over all DTCs
    for (int iter_dtc_id = MIN_DTC_ID; iter_dtc_id <= MAX_DTC_ID; iter_dtc_id++) {
        // Skip DTCs ending with 0 (10, 20, 30, etc.)
        if (iter_dtc_id % 10 == 0) continue;
        
        int dtc_index = calculateDTCIndex(iter_dtc_id);

        // Get all detector IDs for this DTC
        auto det_ids = cablingMap.getAllDetIdsForDTCId(iter_dtc_id);
        int total_det_ids = det_ids.size();
        
        std::cout << "PACKER: DTC " << iter_dtc_id << " has " << total_det_ids << " detectors" << std::endl;

        // Calculate modules per slink - distribute evenly as much as possible
        int base_modules_per_slink = total_det_ids / SLINKS_PER_DTC;
        int remaining_modules = total_det_ids % SLINKS_PER_DTC;

        int moduleIndex = 0;
        
        // Loop over all SLinks
        for (int slink_id = 0; slink_id < SLINKS_PER_DTC; slink_id++) {
            int global_slink_id = dtc_index * SLINKS_PER_DTC + slink_id;
            
            // Calculate how many modules should be assigned to this SLink
            int modules_for_this_slink = base_modules_per_slink + ((slink_id < remaining_modules) ? 1 : 0);
            
            if (modules_for_this_slink == 0) {
                continue;
            }
            
            // Data structure for each SLink (offset block and data block)
            std::vector<bool> offsetBlock;
            std::vector<bool> dataBlock;
            
            // Process each module assigned to this SLink
            for (int mod_idx = 0; mod_idx < modules_for_this_slink && moduleIndex < total_det_ids; mod_idx++) {
                uint32_t det_id = det_ids[moduleIndex++];
                bool isDebugModule = (det_id == 303046688);
                
                auto found_det_id = handle->find(det_id);
                if (found_det_id == handle->end()) {
                    std::cout << "WARNING: DetID " << det_id << " not found in input collection, skipping" << std::endl;
                    continue;
                }

                const edm::DetSet<Phase2ITChipBitStream>& detSet = *found_det_id;
                if (isDebugModule) {
                    std::cout << "PACKER: Found " << detSet.size() << " chips for detector " << det_id << std::endl;
                }
                
                // Process each chip in this module
                int chipId=0;
                for (auto const& chip : detSet) {
                    // Make sure dataBlock is aligned to 128-bit boundary before adding a new chip
                    size_t oldSize = dataBlock.size();
                    padToChunkBoundary(dataBlock);
                    size_t newSize = dataBlock.size();
                    
                    if (isDebugModule) {
                        if (newSize > oldSize) {
                            std::cout << "PACKER: Added " << (newSize - oldSize) 
                                     << " padding bits to align to 128-bit boundary" << std::endl;
                        }
                    }
                    
                    // Calculate the offset for this chip (word position in the data block)
                    uint16_t chipOffset = calculateChipOffset(dataBlock);
                    
                    // Add chip offset to the offset block (MSB then LSB)
                    if (isDebugModule) {
                        std::cout << "PACKER: Adding chip " << chipId
                                 << " offset: " << chipOffset << " words" << std::endl;
                    }
                    
                    addWordToBitVector(offsetBlock, (chipOffset >> 16) & 0xFFFF, isDebugModule);  // MSB
                    addWordToBitVector(offsetBlock, chipOffset & 0xFFFF, isDebugModule);          // LSB
                    
                    std::vector<bool> chipBitstream = chip.get_bitstream();
                    unsigned int bitstreamSize = chipBitstream.size();
                    
                    // Calculate padding needed to align to 128-bit boundary
                    unsigned int total_chip_size = 2 * BITS_PER_WORD + bitstreamSize; // 2 headers + bitstream
                    unsigned int padding_needed = (BITS_PER_CHUNK - (total_chip_size % BITS_PER_CHUNK)) % BITS_PER_CHUNK;
                    

                    if (isDebugModule) {
                        std::cout << "PACKER BITSTREAM FOR CHIP " << chipId++ << ": ";
                        for (bool bit : chipBitstream) {
                            std::cout << (bit ? '1' : '0');
                        }
                        std::cout << std::endl;
                        std::cout << "PACKER BITSTREAM LENGTH: " << chipBitstream.size() << " bits" << std::endl;
                    }

                    // Add chip header 1 (marker + padding info)
                    uint16_t header1 = CHIP_HEADER_MARKER | (padding_needed & 0xF);
                    addWordToBitVector(dataBlock, header1, isDebugModule);
                    
                    // Add chip header 2 (bitstream size)
                    addWordToBitVector(dataBlock, bitstreamSize, isDebugModule);
                    
                    dataBlock.insert(dataBlock.end(), chipBitstream.begin(), chipBitstream.end());
                    
                    // Add padding to align to 128-bit boundary
                    if (padding_needed > 0) {
                        dataBlock.insert(dataBlock.end(), padding_needed, false);
                    }
                    
                    chipId++;
                }
            }
            
            // Ensure offset block is padded to 128-bit boundary
            size_t oldSize = offsetBlock.size();
            padToChunkBoundary(offsetBlock);
            size_t newSize = offsetBlock.size();
            
            if (newSize > oldSize) {
                std::cout << "PACKER: Added " << (newSize - oldSize) 
                         << " padding bits to align offset block to 128-bit boundary" << std::endl;
            }
            
            // Calculate sizes in bytes
            unsigned int header_size = HEADER_TRAILER_LINES * BYTES_PER_WORD;
            unsigned int offset_size = offsetBlock.size() / BITS_PER_WORD * BYTES_PER_WORD;
            unsigned int data_size = dataBlock.size() / BITS_PER_WORD * BYTES_PER_WORD;
            unsigned int trailer_size = HEADER_TRAILER_LINES * BYTES_PER_WORD;
            unsigned int total_size = header_size + offset_size + data_size + trailer_size;
            
            std::cout << "PACKER: Created FED data with " 
                     << offsetBlock.size() / (2 * BITS_PER_WORD) << " chip offsets and "
                     << dataBlock.size() << " data bits" << std::endl;
            
            // Create FEDRawData for this SLink
            FEDRawData& slink_data = fedRawDataCollection->FEDData(global_slink_id);
            slink_data.resize(total_size);
            unsigned char* buffer = slink_data.data();
            
            // Add header (8 lines of 0xFFFF)
            for (int i = 0; i < HEADER_TRAILER_LINES; i++) {
                addWordToBuffer(buffer, i, HEADER_TRAILER_PATTERN);
            }
            
            // Add offset block
            unsigned int offset_words = offsetBlock.size() / BITS_PER_WORD;
            for (unsigned int i = 0; i < offset_words; i++) {
                // Extract 16-bit word from offset block
                uint16_t word = 0;
                for (int bit = 0; bit < BITS_PER_WORD; bit++) {
                    if (offsetBlock[i * BITS_PER_WORD + bit]) {
                        word |= (1 << (15 - bit));
                    }
                }
                addWordToBuffer(buffer, HEADER_TRAILER_LINES + i, word);
            }
            
            // Add data block
            unsigned int data_words = dataBlock.size() / BITS_PER_WORD;
            for (unsigned int i = 0; i < data_words; i++) {
                // Extract 16-bit word from data block
                uint16_t word = 0;
                for (int bit = 0; bit < BITS_PER_WORD; bit++) {
                    if (dataBlock[i * BITS_PER_WORD + bit]) {
                        word |= (1 << (15 - bit));
                    }
                }
                addWordToBuffer(buffer, HEADER_TRAILER_LINES + offset_words + i, word);
            }
            
            // Add trailer (8 lines of 0xFFFF)
            for (int i = 0; i < HEADER_TRAILER_LINES; i++) {
                addWordToBuffer(buffer, HEADER_TRAILER_LINES + offset_words + data_words + i, HEADER_TRAILER_PATTERN);
            }
        }
        
        break;
    }
    
    iEvent.put(std::move(fedRawDataCollection));
}

unsigned int PixelToRawProducer::calculateDTCIndex(unsigned int dtc_id) {
    // Converts DTC ID (11-49) to index (0-35)
    unsigned int tens = dtc_id / 10;
    unsigned int units = dtc_id % 10;
    return (tens - 1) * 9 + (units - 1);
}

void PixelToRawProducer::addWordToBuffer(unsigned char* buffer, size_t position, uint16_t word) {
    // Add a 16-bit word to the buffer at the specified position
    buffer[position * BYTES_PER_WORD] = (word >> 8) & 0xFF;     // MSB
    buffer[position * BYTES_PER_WORD + 1] = word & 0xFF;        // LSB
}

void PixelToRawProducer::addWordToBitVector(std::vector<bool>& vec, uint16_t word, bool debug) {
    // Track the starting position for debugging
    size_t startPos = vec.size();
    
    if (debug) {
        std::cout << "PACKER: Converting word 0x" << std::hex << word << std::dec 
                 << " to bits at position " << startPos << std::endl;
        std::cout << "PACKER: Binary: ";
        for (int bit = 15; bit >= 0; bit--) {
            std::cout << ((word >> bit) & 1);
            if (bit % 4 == 0) std::cout << " ";
        }
        std::cout << std::endl;
    }
    
    // Convert a 16-bit word to 16 bits and add to the vector
    for (int bit = 15; bit >= 0; bit--) {
        bool bitValue = (word >> bit) & 1;
        vec.push_back(bitValue);
        
        if (debug) {
            std::cout << "PACKER: Added bit[" << bit << "] = " << bitValue 
                     << " at position " << (vec.size() - 1) << std::endl;
        }
    }
    
    if (debug) {
        std::cout << "PACKER: Word added as: " 
                 << getBitString(vec, startPos, 16) << std::endl;
    }
}

void PixelToRawProducer::padToChunkBoundary(std::vector<bool>& vec) {
    // Add padding to align to 128-bit boundary if needed
    if (!vec.empty() && vec.size() % BITS_PER_CHUNK != 0) {
        size_t padding_needed = BITS_PER_CHUNK - (vec.size() % BITS_PER_CHUNK);
        vec.insert(vec.end(), padding_needed, false);
    }
}

uint16_t PixelToRawProducer::calculateChipOffset(const std::vector<bool>& dataBlock) {
    // Calculate the word offset where this chip's data will start
    return dataBlock.size() / BITS_PER_WORD;
}

void PixelToRawProducer::printBitVectorAs16bit(const std::vector<bool>& bits, const std::string& label) {
    std::cout << "\n=== " << label << " ===" << std::endl;
    
    std::cout << "Total bits: " << bits.size() << std::endl;
    
    // Print as 16-bit words
    for (size_t i = 0; i < bits.size(); i += BITS_PER_WORD) {
        if (i + BITS_PER_WORD > bits.size()) break;
        
        // Convert 16 bits to hex
        uint16_t word = 0;
        for (int j = 0; j < BITS_PER_WORD; j++) {
            if (bits[i + j]) {
                word |= (1 << (15 - j));
            }
        }
        
        std::cout << "Word[" << std::setw(4) << std::dec << i/16 << "]: 0x" 
                 << std::hex << std::setw(4) << std::setfill('0') << word 
                 << "  Binary: ";
        
        // Print binary representation
        for (int j = 0; j < BITS_PER_WORD; j++) {
            std::cout << (bits[i + j] ? "1" : "0");
            if ((j + 1) % 4 == 0) std::cout << " ";
        }
        std::cout << std::dec << std::endl;
    }
}

DEFINE_FWK_MODULE(PixelToRawProducer);
