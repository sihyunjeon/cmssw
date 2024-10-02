// -*- C++ -*-
//

#include <memory>
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

  void DigiToRaw(FEDRawData& rawData, std::vector<bool> digiData);

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

  const auto& cablingMap = iSetup.getData(cablingMapToken_);

  auto fedRawDataCollection = std::make_unique<FEDRawDataCollection>();

  for (const auto& detSet : iEvent.get(ROCBitStreamToken_)){
    DetId detId = detSet.detId();
    edm::DetSet<ROCBitStream> rocBitStreams;

    // elink_ids are dummy for now
    auto elink_ids = cablingMap.detIdToDTCELinkId(detId);
    std::cout<<(*elink_ids.first).second.elink_id()<<std::endl;

    for (const auto& roc : detSet) {
      std::vector<bool> roc_id = roc.get_bitstream();
    }
  }

  //cmssw/EventFilter/EcalDigiToRaw/src/EcalDigiToRaw.cc
  /*
  BlockFormatter::Params params;
  int counter = iEvent.id().event();
  params.counter_ = counter;
  params.orbit_number_ = iEvent.orbitNumber();
  params.bx_ = iEvent.bunchCrossing();
  params.lv1_ = counter % (0x1 << 24);
  params.runnumber_ = iEvent.id().run();

  // EventFilter/DTRawToDigi/plugins/DTDigiToRawModule.cc
  // probably need to handle the iteration using FEDNumbering ?
  int FEDIDMIN=0, FEDIDMAX=399;
  for (unsigned int fedId=0; fedId<=FEDIDMAX; fedId++){
    FEDRawData& rawData = fedRawDataCollection->FEDData(FEDID);

  }

  for (const auto& detSet : iEvent.get(ROCBitStreamToken_)){
    FEDRawData& rawData = fedRawDataCollection->FEDData(FEDID);

    DetId detId = detSet.detId();
    edm::DetSet<ROCBitStream> rocBitStreams;

    // elink_ids are dummy for now
    auto elink_ids = cablingMap.detIdToDTCELinkId(detId);
    std::cout<<(*elink_ids.first).second.elink_id()<<std::endl;

    for (const auto& roc : detSet) {
      std::vector<bool> roc_id = roc.get_bitstream();
      DigiToRaw(rawData, roc_id);
    }
  }
  */
  iEvent.put(std::move(fedRawDataCollection));
}

void Phase2TrackerDTCAssociator::DigiToRaw(FEDRawData& rawData, std::vector<bool> digiData){

}

void Phase2TrackerDTCAssociator::beginJob() {}

void Phase2TrackerDTCAssociator::endJob() {}

//define this as a plug-in
DEFINE_FWK_MODULE(Phase2TrackerDTCAssociator);

