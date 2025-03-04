// ED producer that creates FEDRawDataCollection from inner tracker quarer cores
#include <utility>
#include <unordered_map>
#include <string>
#include <iostream>

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

private:
    void produce(edm::Event&, const edm::EventSetup&) override;
    
    const edm::ESGetToken<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd> cablingMapToken_;
    const edm::EDGetTokenT<edm::DetSetVector<Phase2ITChipBitStream>> ITChipBitStreamToken_;

    unsigned int IndexingDTC(unsigned int dtc_id);
    void AddHexToPtr(unsigned char *data_ptr, int globalIndex, int localIndex, const std::vector<bool>& moduleBitStream);
    void PrintBitVectorAs16bit(const std::vector<bool>& bits, const std::string& label);

    static constexpr int SLINKS_PER_DTC = 16;
    static constexpr int MIN_DTC_ID = 11;
    static constexpr int MAX_DTC_ID = 49;
    static constexpr uint16_t HEADER_CHIP = 0xE000;
    static constexpr uint16_t HEADER_TRAILER_PATTERN = 0xFFFF;
    static constexpr int HEADER_TRAILER_LINES = 8;
    static constexpr int BITS_PER_WORD = 16;
    static constexpr int BITS_PER_CHUNK = 128;
};

PixelToRawProducer::PixelToRawProducer(const edm::ParameterSet& iConfig)
    : cablingMapToken_(esConsumes()),
      ITChipBitStreamToken_(consumes<edm::DetSetVector<Phase2ITChipBitStream>>(
          iConfig.getParameter<edm::InputTag>("Phase2ITChipBitStream"))) {
    produces<FEDRawDataCollection>();
}

PixelToRawProducer::~PixelToRawProducer() {}

void PixelToRawProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
    using namespace edm;
    using namespace std;

    const auto& cablingMap = iSetup.getData(cablingMapToken_);
    auto fedRawDataCollection = std::make_unique<FEDRawDataCollection>();
    edm::Handle<edm::DetSetVector<Phase2ITChipBitStream>> handle;
    iEvent.getByToken(ITChipBitStreamToken_, handle);

    for (int iter_dtc_id = MIN_DTC_ID; iter_dtc_id <= MAX_DTC_ID; iter_dtc_id++) {
        if (iter_dtc_id % 10 == 0) continue; // there is no 10 20 30 40 .. dtcs
        int i_dtc_id = IndexingDTC(iter_dtc_id);

        std::array<std::vector<bool>, SLINKS_PER_DTC> offset_bits;
        std::array<std::vector<bool>, SLINKS_PER_DTC> data_bits;

        auto det_ids = cablingMap.getAllDetIdsForDTCId(iter_dtc_id); // read all module ids for given dtc id
        int total_det_ids = det_ids.size();
        int base_slink_id = total_det_ids / SLINKS_PER_DTC;
        int remain_slink_id = total_det_ids % SLINKS_PER_DTC;

        int moduleIndex = 0;
        for (int i_slink_id = 0; i_slink_id < SLINKS_PER_DTC; i_slink_id++) {
            int total_slink_id = i_dtc_id * SLINKS_PER_DTC + i_slink_id;
            int n_modules_to_fill = base_slink_id + ((i_slink_id < remain_slink_id) ? 1 : 0);
            
            for (int i_module = 0; i_module < n_modules_to_fill; i_module++) {//loop over modules
                uint32_t det_id = det_ids[moduleIndex++];
                auto found_det_id = handle->find(det_id);    
                if (found_det_id != handle->end()) { 
                    const edm::DetSet<Phase2ITChipBitStream>& detSet = *found_det_id;            
                    auto& this_data_vec = data_bits[i_slink_id]; 
                    auto& this_offset_vec = offset_bits[i_slink_id];
                    std::size_t offset_chip = this_data_vec.size()/BITS_PER_WORD;
                    uint16_t msb = static_cast<uint16_t>((offset_chip >> 16) & 0xFFFF);
                    uint16_t lsb = static_cast<uint16_t>(offset_chip & 0xFFFF);
                    
                    std::vector<bool> offset_msb(BITS_PER_WORD, false); 
                    std::vector<bool> offset_lsb(BITS_PER_WORD, false);
                    for (unsigned int i = 0; i < BITS_PER_WORD; ++i) {
                        offset_msb[15 - i] = (msb >> i) & 1;
                        offset_lsb[15 - i] = (lsb >> i) & 1;
                    }
                    this_offset_vec.insert(this_offset_vec.end(), offset_msb.begin(), offset_msb.end());
                    this_offset_vec.insert(this_offset_vec.end(), offset_lsb.begin(), offset_lsb.end());

                    for (auto const& chip : detSet) {// loop over chips
                        std::vector<bool> bitstream = chip.get_bitstream();
                        unsigned int chip_data_size = 32 + bitstream.size(); // 2 headers (16b each) + bitstream
                        unsigned int padding_needed = (BITS_PER_CHUNK - (chip_data_size % BITS_PER_CHUNK)) % BITS_PER_CHUNK;

                        // fill in chip headers
                        uint16_t header1 = HEADER_CHIP | (padding_needed & 0xF);
                        std::vector<bool> data_header1(BITS_PER_WORD, false);
                        for (unsigned int i = 0; i < BITS_PER_WORD; ++i) {
                            data_header1[15 - i] = (header1 >> i) & 1;
                        }
                        
                        uint16_t header2 = bitstream.size();
                        std::vector<bool> data_header2(BITS_PER_WORD, false);
                        for (unsigned int i = 0; i < BITS_PER_WORD; ++i) {
                            data_header2[15 - i] = (header2 >> i) & 1;
                        }

                        this_data_vec.insert(this_data_vec.end(), data_header1.begin(), data_header1.end());
                        this_data_vec.insert(this_data_vec.end(), data_header2.begin(), data_header2.end());
                        this_data_vec.insert(this_data_vec.end(), bitstream.begin(), bitstream.end());

                        if (padding_needed > 0) {      
                            this_data_vec.insert(this_data_vec.end(), padding_needed, false);
                        }                                
                    }
                }
            }

            unsigned int padding_offset_bits = (BITS_PER_CHUNK - (offset_bits[i_slink_id].size() % BITS_PER_CHUNK)) % BITS_PER_CHUNK;
            if (padding_offset_bits > 0) {
                offset_bits[i_slink_id].insert(offset_bits[i_slink_id].end(), padding_offset_bits, false);
            }

            const auto& final_offset = offset_bits[i_slink_id];
            const auto& final_data = data_bits[i_slink_id];

            unsigned int offset_chunks = final_offset.size() / BITS_PER_WORD; 
            unsigned int data_chunks = final_data.size() / BITS_PER_WORD;

            std::cout << "\nDTC ID: " << iter_dtc_id << ", SLink ID: " << i_slink_id << std::endl;
            PrintBitVectorAs16bit(offset_bits[i_slink_id], "Offset Block");
            PrintBitVectorAs16bit(data_bits[i_slink_id], "Data Block");
            std::cout << "Offset block size: " << offset_bits[i_slink_id].size() 
                     << " bits (" << offset_bits[i_slink_id].size()/BITS_PER_CHUNK << " chunks)" << std::endl;
            std::cout << "Data block size: " << data_bits[i_slink_id].size() 
                     << " bits (" << data_bits[i_slink_id].size()/BITS_PER_CHUNK << " chunks)" << std::endl;

            unsigned int offset_bytes = 2 * offset_chunks; 
            unsigned int data_bytes = 2 * data_chunks;
            
            // Calculate size including headers and trailers
            unsigned int header_size = HEADER_TRAILER_LINES * 2; // 8 lines of 16-bit header (0xFFFF)
            unsigned int trailer_size = HEADER_TRAILER_LINES * 2; // 8 lines of 16-bit trailer (0xFFFF)
            unsigned int total_size = header_size + offset_bytes + data_bytes + trailer_size;

            FEDRawData combined_slink;
            combined_slink.resize(total_size);
            unsigned char* ptr = combined_slink.data();
            
            // Add header - 8 lines of 0xFFFF
            for (int h = 0; h < HEADER_TRAILER_LINES; h++) {
                ptr[h*2] = 0xFF;     // Upper byte
                ptr[h*2+1] = 0xFF;   // Lower byte
            }
            
            // Add offset data
            unsigned char* offset_ptr = ptr + header_size;
            for (unsigned int i_chunk = 0; i_chunk < offset_chunks; ++i_chunk) {
                AddHexToPtr(offset_ptr, i_chunk, i_chunk, final_offset);
            }
            
            // Add data blocks
            unsigned char* data_ptr = offset_ptr + offset_bytes;
            for (unsigned int i_chunk = 0; i_chunk < data_chunks; ++i_chunk) {
                AddHexToPtr(data_ptr, i_chunk, i_chunk, final_data);
            }
            
            // Add trailer - 8 lines of 0xFFFF
            unsigned char* trailer_ptr = data_ptr + data_bytes;
            for (int t = 0; t < HEADER_TRAILER_LINES; t++) {
                trailer_ptr[t*2] = 0xFF;     // Upper byte
                trailer_ptr[t*2+1] = 0xFF;   // Lower byte
            }

            FEDRawData& current_slink = fedRawDataCollection->FEDData(total_slink_id);
            unsigned int current_slink_size = current_slink.size();
            unsigned int new_slink_size = current_slink_size + combined_slink.size();
            current_slink.resize(new_slink_size);

            std::memcpy(current_slink.data() + current_slink_size,
                       combined_slink.data(),
                       combined_slink.size());
        }
    }
    
    iEvent.put(std::move(fedRawDataCollection));
}

unsigned int PixelToRawProducer::IndexingDTC(unsigned int dtc_id) {
    unsigned int first = dtc_id / 10;
    unsigned int second = dtc_id % 10;
    return (10 * (first - 1) + second - first);
}

void PixelToRawProducer::AddHexToPtr(unsigned char* ptr,
                                       int globalIndex,
                                       int localIndex,
                                       const std::vector<bool>& bits) {
    uint16_t hex_word = 0;
    for (int i = 0; i < BITS_PER_WORD; ++i) {
        if (bits[localIndex * BITS_PER_WORD + i]) {
            hex_word |= (1 << (15 - i));
        }
    }
    
    ptr[globalIndex * 2 + 0] = (hex_word >> 8) & 0xFF;   // upper byte
    ptr[globalIndex * 2 + 1] = hex_word & 0xFF;          // lower byte
}

void PixelToRawProducer::PrintBitVectorAs16bit(const std::vector<bool>& bits, const std::string& label) {
    std::cout << "\n=== " << label << " ===" << std::endl;
    std::cout << "Total bits: " << bits.size() << std::endl;
    
    for (size_t i = 0; i < bits.size(); i += BITS_PER_WORD) {
        if (i + BITS_PER_WORD > bits.size()) break; // Skip incomplete chunks
        
        // 16 bits to hex
        uint16_t value = 0;
        for (int j = 0; j < BITS_PER_WORD; j++) {
            if (bits[i + j]) {
                value |= (1 << (15 - j));
            }
        }
        
        std::cout << "Bits[" << std::setw(4) << std::dec << i << "]: 0x" 
                 << std::hex << std::setw(4) << std::setfill('0') << value 
                 << "  Binary: ";
        
        for (int j = 0; j < BITS_PER_WORD; j++) {
            std::cout << (bits[i + j] ? "1" : "0");
            if ((j + 1) % 4 == 0) std::cout << " ";
        }
        std::cout << std::endl;
    }
    std::cout << std::dec;
}

DEFINE_FWK_MODULE(PixelToRawProducer);
