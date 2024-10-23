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

  void AddHexToDataPtr(unsigned char *data_ptr, int word_index, const std::vector<bool>& moduleBitStream);

};

Phase2TrackerDTCAssociator::Phase2TrackerDTCAssociator(const edm::ParameterSet& iConfig)
  : cablingMapToken_(esConsumes()),
    ROCBitStreamToken_(consumes<edm::DetSetVector<ROCBitStream>>(iConfig.getParameter<edm::InputTag>("ROCBitStream"))){
    produces<FEDRawDataCollection>();
}

Phase2TrackerDTCAssociator::~Phase2TrackerDTCAssociator() {}

void Phase2TrackerDTCAssociator::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {

  // FIXME constant magic numbers, define it somewhere else
  unsigned int NSLinksPerDTC = 16;
  unsigned int NDTCs = 4*9;
  unsigned int NSLinks = NDTCs * NSLinksPerDTC;
  unsigned int MINITFEDID = 0;
  unsigned int MAXITFEDID = MINITFEDID + NSLinks - 1;

  using namespace edm;
  using namespace std;

  unsigned int EventID = iEvent.id().event();
  Phase2ITDTCCollection dtcColl(EventID);

  const auto& cablingMap = iSetup.getData(cablingMapToken_);
  auto fedRawDataCollection = std::make_unique<FEDRawDataCollection>();

  for (const auto& detSet : iEvent.get(ROCBitStreamToken_)){
      DetId detId = detSet.detId();
      auto DTCELinkId = cablingMap.detIdToDTCELinkId(detId);
      int dtc_id = (*DTCELinkId.first).second.dtc_id();
      auto dtc = dtcColl.getDTC(dtc_id);

      std::vector<bool> moduleBitStream;
    
      for (const auto& roc : detSet) {  
          std::vector<bool> bitstream = roc.get_bitstream();
          unsigned int bitstream_size = bitstream.size();   

          // TODO make proper chip header 
          std::vector<bool> DUMMY(16, false);
          moduleBitStream.insert(moduleBitStream.end(), DUMMY.begin(), DUMMY.end());

          // adding how long the bit stream is
          unsigned int num_16bit_chunks = (bitstream_size + 15) / 16;
          std::vector<bool> length_bits(16, false);
          for (int i = 0; i < 16; ++i) {
              length_bits[15 - i] = (num_16bit_chunks >> i) & 1;
          }
          moduleBitStream.insert(moduleBitStream.end(), length_bits.begin(), length_bits.end());

          unsigned int padding = 16 - (bitstream_size % 16);
          if (bitstream_size % 16 != 0) {
              bitstream.insert(bitstream.end(), padding, false);
          }
          // Adding the bitstream itself
          moduleBitStream.insert(moduleBitStream.end(), bitstream.begin(), bitstream.end());
      }

      //for (unsigned int slinkId = 0; slinkId < 16; slinkId++) {
      for (unsigned int slinkId = 0; slinkId < 1; slinkId++) { // dumping into one s-link for now
          FEDRawData& slinkRawData = dtc.getSLink(slinkId);
          unsigned char *data_ptr = slinkRawData.data();
    
          slinkRawData.resize((moduleBitStream.size() + 15) / 16 * 8);
          for (size_t word_index = 0; word_index < moduleBitStream.size() / 16; ++word_index) {
              AddHexToDataPtr(data_ptr, word_index, moduleBitStream);
          }
      }
  }

  iEvent.put(std::move(fedRawDataCollection));
}

void Phase2TrackerDTCAssociator::AddHexToDataPtr(unsigned char *data_ptr, int word_index, const std::vector<bool>& moduleBitStream) 
{
    uint16_t hex_word = 0;

    for (int i = 0; i < 16; ++i) {
        if (moduleBitStream[word_index * 16 + i]) {
            hex_word |= (1 << (15 - i));
        }
    }

    data_ptr[word_index * 4 + 0] = (hex_word >> 12) & 0xF;
    data_ptr[word_index * 4 + 1] = (hex_word >> 8) & 0xF;
    data_ptr[word_index * 4 + 2] = (hex_word >> 4) & 0xF;
    data_ptr[word_index * 4 + 3] = hex_word & 0xF;
}

void Phase2TrackerDTCAssociator::beginJob() {}

void Phase2TrackerDTCAssociator::endJob() {}

//define this as a plug-in
DEFINE_FWK_MODULE(Phase2TrackerDTCAssociator);

