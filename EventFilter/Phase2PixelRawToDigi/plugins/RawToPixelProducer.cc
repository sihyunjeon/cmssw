#include <memory>
#include <vector>
#include <iostream>
#include <iomanip>
#include <bitset>
#include <algorithm>
#include <array>
#include <map>
#include <utility>
#include <numeric>  // For std::accumulate

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
#include "DataFormats/Common/interface/DetSetVector.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "DataFormats/SiPixelDigi/interface/PixelDigi.h"

class RawToPixelProducer : public edm::stream::EDProducer<> {
public:
    explicit RawToPixelProducer(const edm::ParameterSet&);
    ~RawToPixelProducer() override = default;

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);
    void beginRun(const edm::Run&, const edm::EventSetup&) override;

private:
    void produce(edm::Event&, const edm::EventSetup&) override;
    
    uint16_t readWord(const unsigned char* dataPtr, int wordIdx) const;
    std::string wordToHexString(uint16_t word) const;
    std::string bitPattern(uint16_t word) const;
    void dumpMemorySection(const unsigned char* dataPtr, int startWord, int numWords) const;
    
    std::string getBitString(const std::vector<bool>& bits, size_t start, size_t len) const;
    void dumpBitstream(const std::vector<bool>& bits, size_t position) const;
    
    bool verifyHeaderTrailerPattern(const unsigned char* dataPtr, int wordIdx) const;
    int findTrailerStart(const unsigned char* dataPtr, int fedSizeInWords) const;
    std::vector<uint32_t> extractChipOffsets(const unsigned char* dataPtr, int offsetStartWord, int maxWords) const;
    std::vector<bool> extractBitstream(const unsigned char* dataPtr, int startWord, int bitstreamSize) const;
    void processFED(const unsigned char* dataPtr, int fedSizeInWords, int fedId, int dtcId, int slinkId,
                   edm::DetSetVector<PixelDigi>& outputDigis);
    void processChip(const unsigned char* dataPtr, int chipStartWord, int chipEndWord, 
                    uint32_t detId, int chipId,
                    edm::DetSetVector<PixelDigi>& outputDigis);
    unsigned int calculateDTCIndex(unsigned int dtc_id) const;
    void buildFedToModuleMapping();
    
    void decodeBitstream(const std::vector<bool>& bitstream, uint32_t detId, int chipId,
                        edm::DetSetVector<PixelDigi>& outputDigis);
    std::vector<bool> decodeHuffmanHitmap(const std::vector<bool>& bitstream, size_t& bitPos);
    void decodeHuffmanRecursive(const std::vector<bool>& bitstream, size_t& bitPos, std::vector<bool>& hitmap, size_t start, size_t length);
    void convertToSensorCoordinates(std::vector<bool>& hitmap);
    void createDigisFromHitmap(const std::vector<bool>& hitmap, const std::vector<int>& adcValues,
                             int ccol, int qcrow, int chipId,
                             edm::DetSetVector<PixelDigi>& outputDigis);
    uint32_t binaryToInt(const std::vector<bool>& binary, size_t& bitPos, int length);
    
    const edm::EDGetTokenT<FEDRawDataCollection> fedRawDataToken_;
    const edm::ESGetToken<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd> cablingMapToken_;
    
    const TrackerDetToDTCELinkCablingMap* cablingMap_ = nullptr;
    
    std::map<int, std::map<int, std::vector<uint32_t>>> fedToModuleMap_;
    
    bool debug_;
    
    // Constants
    static constexpr int SLINKS_PER_DTC = 16;
    static constexpr int MIN_DTC_ID = 11;
    static constexpr int MAX_DTC_ID = 49;
    static constexpr uint16_t CHIP_HEADER_MARKER = 0xE000;
    static constexpr uint16_t HEADER_TRAILER_PATTERN = 0xFFFF;
    static constexpr int HEADER_TRAILER_LINES = 8;
    static constexpr int BITS_PER_WORD = 16;
    static constexpr int BITS_PER_CHUNK = 128;
    static constexpr int MAX_CHIPS_PER_MODULE = 16;
    
    // Constants for QCore decoding
    static constexpr int QCORE_SIZE = 16;          // 4x4 hitmap
    static constexpr int HITMAP_WIDTH = 4;         // Width of hitmap
    static constexpr int HITMAP_HEIGHT = 4;        // Height of hitmap
    static constexpr int QCORES_IN_CHIP_ROW = 672; // QCore width
    static constexpr int QCORES_IN_CHIP_COL = 216; // QCore height
    static constexpr int CHIP_ROW_GAP = 5;         // Gap between chips in row
    static constexpr int CHIP_COL_GAP = 10;        // Gap between chips in column
};

RawToPixelProducer::RawToPixelProducer(const edm::ParameterSet& iConfig) :
    fedRawDataToken_(consumes<FEDRawDataCollection>(iConfig.getParameter<edm::InputTag>("fedRawDataCollection"))),
    cablingMapToken_(esConsumes<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd, edm::Transition::BeginRun>()),
    debug_(iConfig.getUntrackedParameter<bool>("debug", false))
{
    produces<edm::DetSetVector<PixelDigi>>();
}

void RawToPixelProducer::beginRun(const edm::Run& iRun, const edm::EventSetup& iSetup) {
    cablingMap_ = &iSetup.getData(cablingMapToken_);
    // Build FED to module mapping : from the map we know which slink holds which modules
    buildFedToModuleMapping();
}

void RawToPixelProducer::buildFedToModuleMapping() {
    fedToModuleMap_.clear();
    
    // Loop through all DTCs
    for (int dtcId = MIN_DTC_ID; dtcId <= MAX_DTC_ID; dtcId++) {
        if (dtcId % 10 == 0) continue; // Skip DTCs ending with 0
        
        // Get all detector IDs for this DTC
        auto detIds = cablingMap_->getAllDetIdsForDTCId(dtcId);
        if (detIds.empty()) {
            continue;
        }
        
        // Calculate DTC index
        int dtcIndex = calculateDTCIndex(dtcId);
        
        // Distribute modules across SLinks evenly
        int base_modules_per_slink = detIds.size() / SLINKS_PER_DTC;
        int remain_modules = detIds.size() % SLINKS_PER_DTC;
        
        // Process each SLink
        int moduleIndex = 0;
        for (int slinkId = 0; slinkId < SLINKS_PER_DTC; slinkId++) {
            // Calculate FED ID
            int fedId = dtcIndex * SLINKS_PER_DTC + slinkId;
            
            // Calculate how many modules go to this SLink
            int modules_for_this_slink = base_modules_per_slink + ((slinkId < remain_modules) ? 1 : 0);
            
            // Skip if no modules assigned
            if (modules_for_this_slink == 0) {
                continue;
            }
            
            // Assign modules to this FED
            std::vector<uint32_t> modulesForFed;
            for (int i = 0; i < modules_for_this_slink && moduleIndex < (int)detIds.size(); i++) {
                modulesForFed.push_back(detIds[moduleIndex++]);
            }
            
            // Store in the map
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
                std::cout << "  FED " << fedId << " (SLINK " << slinkId 
                        << "): " << modules.size() << " modules" << std::endl;
                
                for (size_t i = 0; i < modules.size() && i < 5; i++) {
                    std::cout << "    Module " << i << ": DetID " << modules[i];
                    if (i == 4 && modules.size() > 5) {
                        std::cout << " (+" << (modules.size() - 5) << " more)";
                    }
                    std::cout << std::endl;
                }
            }
        }
    }
}

void RawToPixelProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
    auto outputPixelDigis = std::make_unique<edm::DetSetVector<PixelDigi>>();
    
    edm::Handle<FEDRawDataCollection> fedRawDataCollection;
    iEvent.getByToken(fedRawDataToken_, fedRawDataCollection);
    
    if (!fedRawDataCollection.isValid()) {
        std::cout << "ERROR: FEDRawDataCollection not found!" << std::endl;
        iEvent.put(std::move(outputPixelDigis));
        return;
    }
    
    if (debug_) {
        std::cout << "\n===========================================" << std::endl;
        std::cout << "RawToPixelProducer: Starting unpacking event" << std::endl;
        std::cout << "===========================================" << std::endl;
    }
    
    // Process each FED for which we have a mapping
    for (const auto& fedEntry : fedToModuleMap_) {
        int fedId = fedEntry.first;
        
        const FEDRawData& fedData = fedRawDataCollection->FEDData(fedId);
        
        // Get DTC and SLink info
        int slinkId = fedId % SLINKS_PER_DTC;
        int dtcIndex = fedId / SLINKS_PER_DTC;
        
        // Calculate DTC ID from index //FIXME Hardcoded
        int dtcId = ((dtcIndex / 9) + 1) * 10 + (dtcIndex % 9) + 1;
        
        const unsigned char* dataPtr = fedData.data();
        int fedSizeInWords = fedData.size() / 2;
        
        if (debug_) {
            std::cout << "\n----- Processing FED ID " << fedId 
                    << " (DTC=" << dtcId << ", SLINK=" << slinkId 
                    << "), size: " << fedData.size() << " bytes ("
                    << fedSizeInWords << " words)" << std::endl;
            
            if (fedEntry.second.find(slinkId) != fedEntry.second.end()) {
                std::cout << "Assigned modules: " << fedEntry.second.at(slinkId).size() << std::endl;
            } else {
                std::cout << "WARNING: No module mapping found for this SLINK" << std::endl;
                continue;
            }
        }
        
        processFED(dataPtr, fedSizeInWords, fedId, dtcId, slinkId, *outputPixelDigis);
    }
    
    if (debug_) {
        std::cout << "\nProduced " << outputPixelDigis->size() 
                << " DetSets with PixelDigis" << std::endl;
    }
    
    iEvent.put(std::move(outputPixelDigis));
}

void RawToPixelProducer::processFED(const unsigned char* dataPtr, int fedSizeInWords, 
                                   int fedId, int dtcId, int slinkId,
                                   edm::DetSetVector<PixelDigi>& outputDigis) {
    const std::vector<uint32_t>& detIds = fedToModuleMap_[fedId][slinkId];
    
    // 1. Verify header (8 words of 0xFFFF)
    bool validHeader = verifyHeaderTrailerPattern(dataPtr, 0);
    if (!validHeader) {
        std::cout << "ERROR: Invalid header pattern in FED " << fedId << ", skipping" << std::endl;
        if (debug_) {
            dumpMemorySection(dataPtr, 0, std::min(16, fedSizeInWords));
        }
        return;
    }
    
    if (debug_) {
        std::cout << "Header section:" << std::endl;
        dumpMemorySection(dataPtr, 0, HEADER_TRAILER_LINES);
    }
    
    // 2. Find trailer start FIXME need to come up with proper trailer finding algo
    int trailerStart = findTrailerStart(dataPtr, fedSizeInWords);
    if (trailerStart < 0) {
        std::cout << "WARNING: Could not find trailer in FED " << fedId 
                  << ", assuming it ends with the FED data" << std::endl;
        trailerStart = fedSizeInWords;
    } else if (debug_) {
        std::cout << "Found trailer at word " << trailerStart << std::endl;
        
        std::cout << "Trailer section:" << std::endl;
        dumpMemorySection(dataPtr, trailerStart, std::min(HEADER_TRAILER_LINES, fedSizeInWords - trailerStart));
    }
    
    // 3. Extract the offset block (starts after header)
    int offsetStart = HEADER_TRAILER_LINES;
    
    // Extract chip offsets from the offset block
    std::vector<uint32_t> chipOffsets = extractChipOffsets(dataPtr, offsetStart, trailerStart - offsetStart);
    
    if (chipOffsets.empty()) {
        std::cout << "WARNING: No valid chip offsets found in FED " << fedId << ", skipping" << std::endl;
        return;
    }
    
    if (debug_) {
        std::cout << "Found " << chipOffsets.size() << " chip offsets:" << std::endl;
        for (size_t i = 0; i < chipOffsets.size() && i < 5; i++) {
            std::cout << "  Offset " << i << ": " << chipOffsets[i] << " words" << std::endl;
        }
        if (chipOffsets.size() > 5) {
            std::cout << "  (... and " << (chipOffsets.size() - 5) << " more)" << std::endl;
        }
    }
    
    // 4. Find data block start (after offset block)
    int offsetBlockSize = chipOffsets.size() * 2; // 2 words (MSB, LSB) per offset
    
    // Calculate padding to align offset block to 128-bit boundary
    int offsetBits = offsetBlockSize * BITS_PER_WORD;
    int paddingBits = (BITS_PER_CHUNK - (offsetBits % BITS_PER_CHUNK)) % BITS_PER_CHUNK;
    int paddingWords = paddingBits / BITS_PER_WORD;
    
    int dataBlockStart = offsetStart + offsetBlockSize + paddingWords;
    
    if (debug_) {
        std::cout << "Offset block: " << offsetBlockSize << " words (with " 
                << paddingWords << " padding words)" << std::endl;
        std::cout << "Data block starts at word " << dataBlockStart << std::endl;
    }
    
    
    // 5. Process each chip based on the offsets
    int numChips = chipOffsets.size();
    int numModules = detIds.size();
    const int chipsPerModule = 4;
    
    if (debug_) {
        std::cout << "Found " << numChips << " chips across " << numModules 
                << " modules (approx. " << chipsPerModule << " chips per module)" << std::endl;
    }
    
    for (int chipIdx = 0; chipIdx < numChips; chipIdx++) {
        // Determine which module this chip belongs to
        int moduleIdx = chipIdx / chipsPerModule;
        if (moduleIdx >= numModules) {
            moduleIdx = numModules - 1; // Assign overflow chips to last module
        }
        
        uint32_t detId = detIds[moduleIdx];
        int chipIdInModule = chipIdx % chipsPerModule;
        
        // Calculate chip data location
        int chipStartWord = dataBlockStart + chipOffsets[chipIdx];
        int chipEndWord;
        
        // For the last chip, end at the trailer
        if (chipIdx == numChips - 1) {
            chipEndWord = trailerStart;
        } else {
            // Otherwise, end at the start of the next chip
            chipEndWord = dataBlockStart + chipOffsets[chipIdx + 1];
        }
        
        // Ensure we're within bounds
        if (chipStartWord >= trailerStart || chipEndWord > trailerStart) {
            std::cout << "WARNING: Chip " << chipIdx << " data location (" 
                      << chipStartWord << "-" << chipEndWord 
                      << ") is out of bounds, skipping" << std::endl;
            continue;
        }
        
        processChip(dataPtr, chipStartWord, chipEndWord, detId, chipIdInModule, outputDigis);
    }
}

void RawToPixelProducer::processChip(const unsigned char* dataPtr, int chipStartWord, int chipEndWord, 
                                    uint32_t detId, int chipId,
                                    edm::DetSetVector<PixelDigi>& outputDigis) {
    
    bool debug = (detId == 303046688);  
    
    // Check for valid chip data size
    if (chipEndWord <= chipStartWord) {
        std::cout << "WARNING: Invalid chip data size (start=" << chipStartWord 
                  << ", end=" << chipEndWord << "), skipping" << std::endl;
        return;
    }
    
    if (debug) {
        std::cout << "\nUNPACKER: Processing chip " << chipId << " of detector " << detId << std::endl;
        std::cout << "  Chip data: words " << chipStartWord << " to " << chipEndWord 
                 << " (" << (chipEndWord - chipStartWord) << " words)" << std::endl;
    }
    
    // Read chip header 1 (0xE000 + padding info)
    uint16_t header1 = readWord(dataPtr, chipStartWord);
    
    // Verify this is a valid chip header
    if ((header1 & 0xF000) != CHIP_HEADER_MARKER) {
        std::cout << "WARNING: Invalid chip header " << wordToHexString(header1) 
                  << " at word " << chipStartWord << ", skipping" << std::endl;
        return;
    }
    
    // Extract padding info
    uint8_t paddingBits = header1 & 0xF;
    
    // Read chip header 2 (bitstream size)
    uint16_t bitstreamSize = readWord(dataPtr, chipStartWord + 1);
    
    if (debug) {
        std::cout << "  Header 1: 0x" << std::hex << header1 << std::dec 
                 << " (marker 0x" << std::hex << (header1 & 0xF000) << std::dec
                 << " + padding " << (int)paddingBits << " bits)" << std::endl;
        std::cout << "  Header 2: " << bitstreamSize << " (bitstream size in bits)" << std::endl;
    }
    
    // Extract bitstream
    std::vector<bool> bitstream = extractBitstream(dataPtr, chipStartWord + 2, bitstreamSize);

    if (debug) {
        std::cout << "UNPACKER BITSTREAM: ";
        for (bool bit : bitstream) {
            std::cout << (bit ? '1' : '0');
        }
        std::cout << std::endl;
        std::cout << "UNPACKER BITSTREAM LENGTH: " << bitstream.size() << " bits" << std::endl;
    }
    
    // Calculate bitstream size in words
    int fullWords = bitstreamSize / BITS_PER_WORD;
    int remainingBits = bitstreamSize % BITS_PER_WORD;
    int bitstreamWords = fullWords + (remainingBits > 0 ? 1 : 0);
    
    // Calculate padding
    int paddingWords = (paddingBits + BITS_PER_WORD - 1) / BITS_PER_WORD;
    
    // Check if last partial word and padding can be combined
    int combinedWord = 0;
    if (remainingBits > 0 && remainingBits + paddingBits <= BITS_PER_WORD) {
        // Last partial word of bitstream and padding can fit in one word
        combinedWord = 1;
    }
    
    // Calculate expected end of chip data
    int expectedEndWord = chipStartWord + 2 + bitstreamWords + paddingWords - combinedWord;
    
    if (debug) {
        std::cout << "  Bitstream layout: " << bitstreamWords << " words (" 
                 << fullWords << " full + " << (remainingBits > 0 ? "1 partial" : "0 partial") 
                 << "), padding: " << paddingWords << " words" << std::endl;
        
        std::cout << "  Expected chip end: word " << expectedEndWord 
                 << ", actual chip end: word " << chipEndWord << std::endl;
    }
    
    // Decode the bitstream and create PixelDigi objects
    decodeBitstream(bitstream, detId, chipId, outputDigis);
    
    if (debug) {
        std::cout << "  Successfully processed chip with " << bitstreamSize << " bits" << std::endl;
    }
}

std::string RawToPixelProducer::getBitString(const std::vector<bool>& bits, size_t start, size_t len) const {
    std::string result;
    for (size_t i = 0; i < len && start + i < bits.size(); i++) {
        result += (bits[start + i] ? "1" : "0");
        if ((i + 1) % 8 == 0) result += " ";
    }
    return result;
}

void RawToPixelProducer::dumpBitstream(const std::vector<bool>& bits, size_t position) const {
    std::cout << "BITSTREAM at position " << position << ": ";
    size_t start = (position > 8) ? position - 8 : 0;
    size_t end = std::min(bits.size(), position + 16);
    
    for (size_t i = start; i < end; i++) {
        if (i == position) std::cout << "|";
        std::cout << (bits[i] ? "1" : "0");
        if ((i + 1) % 8 == 0) std::cout << " ";
    }
    std::cout << std::endl;
}

void RawToPixelProducer::decodeBitstream(const std::vector<bool>& bitstream, 
                                        uint32_t detId, int chipId,
                                        edm::DetSetVector<PixelDigi>& outputDigis) {
    bool debug = (detId == 303046688);
    
    if (bitstream.empty()) {
        if (debug) {
            std::cout << "UNPACKER: Empty bitstream, no hits to decode" << std::endl;
        }
        return;
    }
    
    // Create a DetSet for this detector ID
    edm::DetSet<PixelDigi> detSet(detId);
    
    if (debug) {
        std::cout << "\nUNPACKER: Decoding bitstream for detector " << detId << " chip " << chipId << std::endl;
        std::cout << "  Bitstream length: " << bitstream.size() << " bits" << std::endl;
        std::cout << "  First 32 bits: " << getBitString(bitstream, 0, 32) << "\n";
    }
    
    // Initialize decoder state
    size_t bitPos = 0;
    int currentCol = 0;
    int currentRow = 0;
    bool isNewCol = true;
    
    // Track the number of QCores and hits decoded
    int qcoreCount = 0;
    int hitCount = 0;
    
    // Track consumption of bits per QCore
    std::vector<int> qcoreBitCounts;
    
    // Continue decoding until we reach the end of the bitstream
    while (bitPos < bitstream.size()) {
        size_t startBitPos = bitPos;
        
        if (debug) {
            std::cout << "\nUNPACKER: Decoding QCore " << qcoreCount << " at bit " << bitPos << std::endl;
        }
        
        // If we're at a new column, extract the column code (6 bits)
        if (isNewCol) {
            if (bitPos + 6 > bitstream.size()) {
                if (debug) {
                    std::cout << "  Not enough bits left for column code" << std::endl;
                }
                break;
            }
            
            // Extract column code (6 bits)
            size_t colStartBit = bitPos;
            currentCol = binaryToInt(bitstream, bitPos, 6);
            
            if (debug) {
                std::cout << "  Column code [bits " << colStartBit << "-" << (bitPos-1) 
                         << "]: " << currentCol << std::endl;
                std::cout << "  Column bits: " << getBitString(bitstream, colStartBit, 6) << std::endl;
            }
        }
        
        // Extract islast and isneighbour flags
        if (bitPos + 2 > bitstream.size()) {
            if (debug) {
                std::cout << "  Not enough bits left for flags" << std::endl;
            }
            break;
        }
        
        size_t flagStartBit = bitPos;
        bool islast = bitstream[bitPos++];
        bool isneighbour = bitstream[bitPos++];
        
        if (debug) {
            std::cout << "  Flags [bits " << flagStartBit << "-" << (bitPos-1) 
                     << "]: islast=" << islast << ", isneighbour=" << isneighbour << std::endl;
            dumpBitstream(bitstream, flagStartBit);
        }
        
        // If not a neighbor, extract row information (8 bits)
        if (!isneighbour) {
            if (bitPos + 8 > bitstream.size()) {
                if (debug) {
                    std::cout << "  Not enough bits left for row code" << std::endl;
                }
                break;
            }
            
            // Extract row code (8 bits)
            size_t rowStartBit = bitPos;
            currentRow = binaryToInt(bitstream, bitPos, 8);
            
            if (debug) {
                std::cout << "  Row code [bits " << rowStartBit << "-" << (bitPos-1) 
                         << "]: " << currentRow << std::endl;
                std::cout << "  Row bits: " << getBitString(bitstream, rowStartBit, 8) << std::endl;
            }
        } else if (debug) {
            std::cout << "  Using previous row: " << currentRow << " (isneighbour=true)" << std::endl;
        }
        
        // Debug output for the current QCore
        if (debug) {
            std::cout << "  QCore " << qcoreCount << ": col=" << currentCol 
                     << ", row=" << currentRow << ", islast=" << islast 
                     << ", isneighbour=" << isneighbour << std::endl;
        }
        
        // Decode Huffman-encoded hitmap
        size_t hitmapStartPos = bitPos;
        std::vector<bool> hitmap = decodeHuffmanHitmap(bitstream, bitPos);
        
        if (debug) {
            std::cout << "  Hitmap code [bits " << hitmapStartPos << "-" << (bitPos-1) << "]: ("
                     << (bitPos - hitmapStartPos) << " bits)" << std::endl;
            std::cout << "  Decoded hitmap: " << getBitString(hitmap, 0, 16) << std::endl;
        }
        
        // Convert from ROC coordinates to sensor coordinates
        convertToSensorCoordinates(hitmap);
        
        if (debug) {
            std::cout << "  After coordinate conversion: " << getBitString(hitmap, 0, 16) << std::endl;
        }
        
        // Count hits in the hitmap
        int numHits = std::count(hitmap.begin(), hitmap.end(), true);
        
        // Extract ADC values for hits (4 bits per hit)
        std::vector<int> adcValues;
        adcValues.reserve(numHits);
        
        if (debug && numHits > 0) {
            std::cout << "  Extracting " << numHits << " ADC values" << std::endl;
        }
        
        for (bool hit : hitmap) {
            if (hit) {
                if (bitPos + 4 > bitstream.size()) {
                    if (debug) {
                        std::cout << "  Not enough bits left for ADC value" << std::endl;
                    }
                    break;
                }
                
                // Extract 4-bit ADC value
                size_t adcStartBit = bitPos;
                int adc = binaryToInt(bitstream, bitPos, 4);
                
                if (debug) {
                    std::cout << "  ADC value [bits " << adcStartBit << "-" << (bitPos-1) 
                             << "]: " << adc << std::endl;
                }
                
                adcValues.push_back(adc);
                hitCount++;
            }
        }
        
        // Calculate bits consumed by this QCore
        int bitsConsumed = bitPos - startBitPos;
        qcoreBitCounts.push_back(bitsConsumed);
        
        if (debug) {
            std::cout << "  QCore " << qcoreCount << " consumed " << bitsConsumed 
                     << " bits (positions " << startBitPos << "-" << (bitPos-1) << ")" << std::endl;
        }
        
        // Create PixelDigi objects for the hitmap
        int adcIndex = 0;
        for (int i = 0; i < HITMAP_HEIGHT; i++) {
            for (int j = 0; j < HITMAP_WIDTH; j++) {
                int hitIndex = i * HITMAP_WIDTH + j;
                
                // If there's a hit at this position
                if (hitIndex < (int)hitmap.size() && hitmap[hitIndex]) {
                    // Calculate global row and column
                    int globalRow = currentRow * HITMAP_HEIGHT + i;
                    int globalCol = currentCol * HITMAP_WIDTH + j;
                    
                    // Adjust for chip position
                    if (chipId == 1 || chipId == 3) {
                        // Right side chip
                        globalCol += QCORES_IN_CHIP_ROW + CHIP_ROW_GAP;
                    }
                    
                    if (chipId == 2 || chipId == 3) {
                        // Bottom chip
                        globalRow += QCORES_IN_CHIP_COL + CHIP_COL_GAP;
                    }
                    
                    // Get the ADC value for this hit
                    int adc = (adcIndex < (int)adcValues.size()) ? adcValues[adcIndex++] : 1;
                    
                    // Create and add the PixelDigi
                    detSet.push_back(PixelDigi(globalRow, globalCol, adc));
                    
                    if (debug) {
                        std::cout << "  Created PixelDigi: row=" << globalRow 
                                  << ", col=" << globalCol << ", adc=" << adc << std::endl;
                    }
                }
            }
        }
        
        // Update state for next QCore
        isNewCol = islast;
        qcoreCount++;
        
        // Print out first 3 QCores for reading purpose
        if (qcoreCount >= 3) {
            debug = false;
        }
    }
    
    outputDigis.insert(detSet);
    
    std::cout << "UNPACKER: Finished decoding " << qcoreCount << " QCores, consumed " 
             << bitPos << " of " << bitstream.size() << " bits" << std::endl;
}

std::vector<bool> RawToPixelProducer::extractBitstream(
    const unsigned char* dataPtr, int startWord, int bitstreamSize) const {
    
    bool debug = true; 
    
    if (debug) {
        std::cout << "UNPACKER: Extracting " << bitstreamSize << " bits from word " << startWord << std::endl;
    }
    
    std::vector<bool> bitstream;
    bitstream.reserve(bitstreamSize);
    
    // Calculate how many full words we need to read
    int fullWords = bitstreamSize / BITS_PER_WORD;
    int remainingBits = bitstreamSize % BITS_PER_WORD;
    
    // Extract bits from full words
    for (int i = 0; i < fullWords; i++) {
        uint16_t word = readWord(dataPtr, startWord + i);
        
        if (debug && i < 2) {  // Limit output to first two words
            std::cout << "  Word " << i << ": 0x" << std::hex << word << std::dec 
                     << " = " << bitPattern(word) << std::endl;
        }
        
        for (int j = 0; j < BITS_PER_WORD; j++) {
            bool bit = (word >> (15 - j)) & 1;
            bitstream.push_back(bit);
        }
    }
    
    // Extract remaining bits from last partial word
    if (remainingBits > 0) {
        uint16_t word = readWord(dataPtr, startWord + fullWords);
        
        if (debug) {
            std::cout << "  Last partial word: 0x" << std::hex << word << std::dec 
                     << " = " << bitPattern(word) << std::endl;
        }
        
        for (int j = 0; j < remainingBits; j++) {
            bool bit = (word >> (15 - j)) & 1;
            bitstream.push_back(bit);
        }
    }
    
    if (debug) {
        std::cout << "UNPACKER: First 32 bits of extracted bitstream: "
                 << getBitString(bitstream, 0, 32) << std::endl;
    }
    
    return bitstream;
}

std::vector<bool> RawToPixelProducer::decodeHuffmanHitmap(const std::vector<bool>& bitstream, size_t& bitPos) {
    // Initialize a 16-bit (4x4) hitmap with all bits set to false
    std::vector<bool> hitmap(16, false);

    // Define a recursive lambda function for decoding
    std::function<void(size_t, size_t)> decode_recursive = [&](size_t start, size_t length) {
        // Base case: single bit
        if (length == 1) {
            if (start < hitmap.size()) {
                // Mark this position as a hit
                hitmap[start] = true;
            }
            return;
        }

        // Not enough bits left
        if (bitPos >= bitstream.size()) {
            return;
        }

        // Read the first bit of the code
        bool firstBit = bitstream[bitPos++];

        // Calculate midpoint for splitting
        size_t half = length / 2;
        size_t mid = start + half;

        if (!firstBit) {
            // Code 0: Only right half has hits
            decode_recursive(mid, half);
        } else {
            // We need another bit to determine if it's 10 or 11
            if (bitPos >= bitstream.size()) {
                return;
            }

            bool secondBit = bitstream[bitPos++];

            if (!secondBit) {
                // Code 10: Only left half has hits
                decode_recursive(start, half);
            } else {
                // Code 11: Both halves have hits
                decode_recursive(start, half);
                decode_recursive(mid, half);
            }
        }
    };

    // Start decoding from the beginning
    decode_recursive(0, 16);

    return hitmap;
}

void RawToPixelProducer::convertToSensorCoordinates(std::vector<bool>& hitmap) {
    std::vector<bool> temp = hitmap;
    hitmap.assign(16, false);
    
    // Convert from ROC coordinates (2x8) to sensor coordinates (4x4)
    for (size_t i = 0; i < 16; i++) {
        int rocRow = i / 8;        // Which of the 2 rows in ROC coordinates
        int rocCol = i % 8;        // Which of the 8 columns in ROC coordinates
        
        // Calculate sensor coordinates
        int sensorRow, sensorCol;
        if (rocCol % 2 == 0) {     // Even column in ROC
            sensorRow = rocRow * 2;
            sensorCol = rocCol / 2;
        } else {                   // Odd column in ROC
            sensorRow = rocRow * 2 + 1;
            sensorCol = (rocCol - 1) / 2;
        }
        
        int sensorIndex = sensorRow * 4 + sensorCol;
        hitmap[sensorIndex] = temp[i];
    }
}

void RawToPixelProducer::createDigisFromHitmap(const std::vector<bool>& hitmap, const std::vector<int>& adcValues,
                             int ccol, int qcrow, int chipId,
                             edm::DetSetVector<PixelDigi>& outputDigis) {
    
    // Use the first available DetSet (should match our detector ID)
    auto detSet = outputDigis.begin();
    
    // Track which ADC value we're on
    int adcIndex = 0;
    
    // Loop through the hitmap and create a PixelDigi for each hit
    for (int i = 0; i < HITMAP_HEIGHT; i++) {
        for (int j = 0; j < HITMAP_WIDTH; j++) {
            int hitIndex = i * HITMAP_WIDTH + j;
            
            // If there's a hit at this position
            if (hitIndex < (int)hitmap.size() && hitmap[hitIndex]) {
                // Calculate global row and column
                int globalRow = qcrow * HITMAP_HEIGHT + i;
                int globalCol = ccol * HITMAP_WIDTH + j;
                
                // Adjust for chip position
                if (chipId == 1 || chipId == 3) {
                    // Right side chip
                    globalCol += QCORES_IN_CHIP_ROW + CHIP_ROW_GAP;
                }
                
                if (chipId == 2 || chipId == 3) {
                    // Bottom chip
                    globalRow += QCORES_IN_CHIP_COL + CHIP_COL_GAP;
                }
                
                // Get the ADC value for this hit
                int adc = (adcIndex < (int)adcValues.size()) ? adcValues[adcIndex++] : 1;
                
                // Create and add the PixelDigi
                detSet->push_back(PixelDigi(globalRow, globalCol, adc));
                
                if (debug_) {
                    std::cout << "    Created PixelDigi: row=" << globalRow 
                              << ", col=" << globalCol << ", adc=" << adc << std::endl;
                }
            }
        }
    }
}

uint32_t RawToPixelProducer::binaryToInt(const std::vector<bool>& binary, size_t& bitPos, int length) {
    uint32_t result = 0;
    
    // Read 'length' bits and convert to integer
    for (int i = 0; i < length; i++) {
        if (bitPos < binary.size()) {
            result = (result << 1) | (binary[bitPos] ? 1 : 0);
            bitPos++;
        } else {
            // Not enough bits left
            break;
        }
    }
    
    return result;
}

std::vector<uint32_t> RawToPixelProducer::extractChipOffsets(
    const unsigned char* dataPtr, int offsetStartWord, int maxWords) const {
    
    std::vector<uint32_t> offsets;
    
    if (debug_) {
        std::cout << "  Analyzing offset block from word " << offsetStartWord << ":" << std::endl;
    }
    
    // Determine 128-bit boundary for the offset block
    int words_per_chunk = BITS_PER_CHUNK / BITS_PER_WORD; // 8 words per 128-bit chunk
    
    // Show all words in the offset block
    if (debug_) {
        std::cout << "  Offset block words:" << std::endl;
        for (int i = 0; i < maxWords; i++) {
            uint16_t word = readWord(dataPtr, offsetStartWord + i);
            std::cout << "    Word[" << offsetStartWord + i << "]: " 
                    << wordToHexString(word) << " (" << bitPattern(word) << ")" << std::endl;
            
            // separator every 8 words (128 bits)
            if ((i + 1) % words_per_chunk == 0) {
                std::cout << "    ------------------------" << std::endl;
            }
            
            // Stop if we see a pattern that looks like a chip header or trailer
            if ((word & 0xF000) == CHIP_HEADER_MARKER || word == HEADER_TRAILER_PATTERN) {
                std::cout << "    Found header/trailer pattern, stopping dump..." << std::endl;
                break;
            }
        }
    }
    
    if (debug_) {
        std::cout << "  Extracting offsets from the offset block:" << std::endl;
    }
    
    // We expect pairs of words (MSB, LSB) representing 32-bit offsets
    for (int i = 0; i < maxWords - 1; i += 2) {
        if (offsetStartWord + i + 1 >= maxWords) break;
        
        uint16_t msb = readWord(dataPtr, offsetStartWord + i);
        uint16_t lsb = readWord(dataPtr, offsetStartWord + i + 1);
        
        // Combine into 32-bit offset
        uint32_t offset = (static_cast<uint32_t>(msb) << 16) | lsb;
        
        if (debug_) {
            std::cout << "    Pair " << i/2 << ": " << wordToHexString(msb) << " " 
                    << wordToHexString(lsb) << " -> Offset: " << offset << std::endl;
        }
        
        // Add the offset 
        offsets.push_back(offset);
        
        // Check if we're at a 128-bit boundary
        if ((i + 2) % words_per_chunk == 0) {
            // If we're at a 128-bit boundary, check if the next word looks like the start of data
            if (offsetStartWord + i + 2 < maxWords) {
                uint16_t nextWord = readWord(dataPtr, offsetStartWord + i + 2);
                if ((nextWord & 0xF000) == CHIP_HEADER_MARKER) {
                    if (debug_) {
                        std::cout << "    Found chip header marker " << wordToHexString(nextWord) 
                                << " at word " << (offsetStartWord + i + 2) 
                                << ", ending offset extraction" << std::endl;
                    }
                    break;
                }
            }
        }
        
        // Stop if we hit a pattern that looks like a trailer
        if (msb == HEADER_TRAILER_PATTERN && lsb == HEADER_TRAILER_PATTERN) {
            if (debug_) {
                std::cout << "    Found trailer pattern at word " << (offsetStartWord + i) 
                        << ", ending offset extraction" << std::endl;
            }
            break;
        }
    }
    
    return offsets;
}

bool RawToPixelProducer::verifyHeaderTrailerPattern(const unsigned char* dataPtr, int wordIdx) const {
    for (int i = 0; i < HEADER_TRAILER_LINES; i++) {
        uint16_t word = readWord(dataPtr, wordIdx + i);
        if (word != HEADER_TRAILER_PATTERN) {
            return false;
        }
    }
    return true;
}

int RawToPixelProducer::findTrailerStart(const unsigned char* dataPtr, int fedSizeInWords) const {
    // Look for 8 consecutive 0xFFFF words indicating the trailer pattern, hard coded.. should be fixed
    int minSearchPos = HEADER_TRAILER_LINES + 2; // Header size + at least 2 offset words
    
    for (int i = minSearchPos; i <= fedSizeInWords - HEADER_TRAILER_LINES; i++) {
        if (verifyHeaderTrailerPattern(dataPtr, i)) {
            return i;
        }
    }
    
    return -1;
}

uint16_t RawToPixelProducer::readWord(const unsigned char* dataPtr, int wordIdx) const {
    int byteIdx = wordIdx * 2;
    return (static_cast<uint16_t>(dataPtr[byteIdx]) << 8) | 
           static_cast<uint16_t>(dataPtr[byteIdx + 1]);
}

std::string RawToPixelProducer::wordToHexString(uint16_t word) const {
    std::stringstream ss;
    ss << "0x" << std::hex << std::setw(4) << std::setfill('0') << word;
    return ss.str();
}

std::string RawToPixelProducer::bitPattern(uint16_t word) const {
    std::string result;
    for (int i = 15; i >= 0; i--) {
        result += ((word >> i) & 1) ? '1' : '0';
        if (i % 4 == 0 && i > 0) result += ' ';
    }
    return result;
}

void RawToPixelProducer::dumpMemorySection(const unsigned char* dataPtr, int startWord, int numWords) const {
    std::cout << "\n=== Memory dump from word " << startWord 
              << " to " << (startWord + numWords - 1) << " ===" << std::endl;
    
    for (int i = 0; i < numWords; i++) {
        uint16_t word = readWord(dataPtr, startWord + i);
        std::cout << "Word[" << std::setw(4) << startWord + i << "]: " 
                  << wordToHexString(word) << " (" << bitPattern(word) << ")" << std::endl;
        
        if ((i + 1) % 8 == 0) {
            std::cout << "----------------------" << std::endl;
        }
    }
}

unsigned int RawToPixelProducer::calculateDTCIndex(unsigned int dtc_id) const {
    // Converts DTC ID (11-49) to index (0-35)
    unsigned int tens = dtc_id / 10;
    unsigned int units = dtc_id % 10;
    return (tens - 1) * 9 + (units - 1);
}

DEFINE_FWK_MODULE(RawToPixelProducer);
