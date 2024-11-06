// -*- C++ -*-
//

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

#include "DataFormats/Phase2TrackerDigi/interface/ROCBitStream.h"
#include "DataFormats/Common/interface/DetSetVector.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "DataFormats/FEDRawData/interface/FEDRawData.h"
#include "DataFormats/FEDRawData/interface/FEDHeader.h"
#include "DataFormats/FEDRawData/interface/FEDTrailer.h"

#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITDTCCollection.h"

class Phase2TrackerDTCAssociator : public edm::one::EDProducer<> {
public:
  explicit Phase2TrackerDTCAssociator(const edm::ParameterSet&);
  ~Phase2TrackerDTCAssociator() override;

private:
  void beginJob() override;
  void produce(edm::Event&, const edm::EventSetup&) override;
  void endJob() override;

  const edm::ESGetToken<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd> cablingMapToken_;
  const edm::EDGetTokenT<edm::DetSetVector<ROCBitStream>> ROCBitStreamToken_;

  void AddHexToPtr(unsigned char *data_ptr, int word_index, const std::vector<bool>& moduleBitStream);

};

Phase2TrackerDTCAssociator::Phase2TrackerDTCAssociator(const edm::ParameterSet& iConfig)
  : cablingMapToken_(esConsumes()),
    ROCBitStreamToken_(consumes<edm::DetSetVector<ROCBitStream>>(iConfig.getParameter<edm::InputTag>("ROCBitStream"))){
    produces<FEDRawDataCollection>();
}

Phase2TrackerDTCAssociator::~Phase2TrackerDTCAssociator() {}

void Phase2TrackerDTCAssociator::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {

    using namespace edm;
    using namespace std;

    unsigned int EventID = iEvent.id().event();
    Phase2ITDTCCollection dtcColl(EventID);
    const auto& cablingMap = iSetup.getData(cablingMapToken_);
    auto fedRawDataCollection = std::make_unique<FEDRawDataCollection>();

    for (const auto& detSet : iEvent.get(ROCBitStreamToken_)) {
        DetId detId = detSet.detId();
        auto DTCELinkId = cablingMap.detIdToDTCELinkId(detId);
        int dtc_id = (*DTCELinkId.first).second.dtc_id();
        unsigned int slinkIndex = dtc_id % 16;  // FIXME Determine slink index properly

        auto dtc = dtcColl.getDTC(dtc_id);

        std::vector<bool> offsetWords;
        std::vector<bool> dataWords;
        unsigned int cumulativeOffset = 0;

        for (const auto& roc : detSet) {
            std::vector<bool> bitstream = roc.get_bitstream();
            unsigned int bitstream_size = bitstream.size();
            unsigned int chip_offset = cumulativeOffset / 16;

            std::vector<bool> offset_MSB(16, false);
            std::vector<bool> offset_LSB(16, false);
            for (int i = 0; i < 16; ++i) {
                offset_MSB[15 - i] = (chip_offset >> (i + 16)) & 1; // MSB
                offset_LSB[15 - i] = (chip_offset >> i) & 1;         // LSB
            }
            offsetWords.insert(offsetWords.end(), offset_MSB.begin(), offset_MSB.end());
            offsetWords.insert(offsetWords.end(), offset_LSB.begin(), offset_LSB.end());

            unsigned int num_16bit_chunks = (bitstream_size + 15) / 16;
            std::vector<bool> length_bits(16, false);
            for (int i = 0; i < 16; ++i) {
                length_bits[15 - i] = (num_16bit_chunks >> i) & 1;
            }
            offsetWords.insert(offsetWords.end(), length_bits.begin(), length_bits.end());

            unsigned int padding = 16 - (bitstream_size % 16);
            if (bitstream_size % 16 != 0) {
                bitstream.insert(bitstream.end(), padding, false);
            }

            dataWords.insert(dataWords.end(), bitstream.begin(), bitstream.end());

            cumulativeOffset += num_16bit_chunks * 16;
        }

        size_t offsetBytes = (offsetWords.size() + 7) / 8;
        size_t dataBytes = (dataWords.size() + 7) / 8;

        FEDRawData& slinkOffset = dtc.getSLinkOffset(slinkIndex);
        FEDRawData& slinkData = dtc.getSLinkData(slinkIndex);
        slinkOffset.resize(offsetBytes);
        slinkData.resize(dataBytes);

        unsigned char *offset_ptr = slinkOffset.data();
        for (size_t index = 0; index < offsetWords.size() / 16; ++index) {
            AddHexToPtr(offset_ptr, index, offsetWords);
        }
        unsigned char *data_ptr = slinkData.data();
        for (size_t index = 0; index < dataWords.size() / 16; ++index) {
            AddHexToPtr(data_ptr, index, dataWords);
        }

    }

    //FIXME hadd to decide how to push ptrs to fedRawDataCollection

    iEvent.put(std::move(fedRawDataCollection));
}

void Phase2TrackerDTCAssociator::AddHexToPtr(unsigned char *ptr, int index, const std::vector<bool>& bits)
{
    uint16_t hex_word = 0;

    for (int i = 0; i < 16; ++i) {
        if (bits[index * 16 + i]) {
            hex_word |= (1 << (15 - i));
        }
    }

    ptr[index * 4 + 0] = (hex_word >> 12) & 0xF;
    ptr[index * 4 + 1] = (hex_word >> 8) & 0xF;
    ptr[index * 4 + 2] = (hex_word >> 4) & 0xF;
    ptr[index * 4 + 3] = hex_word & 0xF;
}

void Phase2TrackerDTCAssociator::beginJob() {}

void Phase2TrackerDTCAssociator::endJob() {}

//define this as a plug-in
DEFINE_FWK_MODULE(Phase2TrackerDTCAssociator);

