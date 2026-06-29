#include <cstdint>

#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "DataFormats/Common/interface/DetSetVector.h"
#include "DataFormats/Common/interface/Handle.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITAuroraBitStream.h"
#include "CondFormats/DataRecord/interface/TrackerDetToDTCELinkCablingMapRcd.h"
#include "CondFormats/SiPhase2TrackerObjects/interface/TrackerDetToDTCELinkCablingMap.h"
#include "FWCore/Utilities/interface/ESGetToken.h"

#include "CondFormats/DataRecord/interface/TrackerDetToDTCELinkCablingMapRcd.h"
#include "CondFormats/SiPhase2TrackerObjects/interface/TrackerDetToDTCELinkCablingMap.h"
#include "DataFormats/Common/interface/Handle.h"
#include "DataFormats/FEDRawData/interface/FEDRawData.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "DQMServices/Core/interface/DQMEDAnalyzer.h"
#include "DQMServices/Core/interface/DQMStore.h"
#include "DQMServices/Core/interface/MonitorElement.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/ESGetToken.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Utilities/interface/Transition.h"


class Phase2ITValidateELink : public DQMEDAnalyzer {
public:
  explicit Phase2ITValidateELink(const edm::ParameterSet& iConfig);
  ~Phase2ITValidateELink() override = default;
  void dqmBeginRun(const edm::Run& iRun, const edm::EventSetup& iSetup) override;
  void analyze(const edm::Event& iEvent, const edm::EventSetup& iSetup) override;
  void bookHistograms(DQMStore::IBooker& ibooker,
                      edm::Run const& iRun,
                      edm::EventSetup const& iSetup) override;
  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

private:
  const edm::EDGetTokenT<edm::DetSetVector<Phase2ITAuroraBitStream>> auroraToken_;
  const edm::ESGetToken<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd> cablingMapToken_;

  //TTree* tree_ = nullptr;
  //Int_t b_group_ = 0;    // NE-stream-group counter
  //Int_t b_ne_ = 0;       // events per stream group
  //UInt_t b_detid_ = 0;   // module DetId
  //Int_t b_elink_ = 0;    // elink index within the module (0..nElinks-1)
  //Int_t b_bits_ = 0;     // Aurora wire size of this elink for this group [bits]
  //Int_t b_section_ = 0;  // ModuleInfo.section (TBPX/TFPX/TEPX)
  //Int_t b_layer_ = 0;    // ModuleInfo.layer
  //Int_t b_ring_ = 0;     // ModuleInfo.ring
  //Int_t b_subtype_ = 0;  // ModuleInfo.subtype

  //?const int firstFed_;
  //?const int nFeds_;
  const std::string folder_;

  const TrackerDetToDTCELinkCablingMap* cablingMap_ = nullptr;

  MonitorElement* meELinkSize_ = nullptr;
};

Phase2ITValidateELink::Phase2ITValidateELink(const edm::ParameterSet& iConfig)
    : auroraToken_(
          consumes<edm::DetSetVector<Phase2ITAuroraBitStream>>(iConfig.getParameter<edm::InputTag>("auroraBitStream"))),
      cablingMapToken_(esConsumes<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd, edm::Transition::BeginRun>()),
      //?firstFed_(iConfig.getUntrackedParameter<int>("firstFed", 0)),
      //?nFeds_(iConfig.getUntrackedParameter<int>("nFeds", 576)),
      folder_(iConfig.getUntrackedParameter<std::string>("folder", "Phase2IT/RawData")) {
      edm::LogInfo("Phase2ITValidateELink") << ">>> Construct Phase2ITValidateELink";
      //usesResource("TFileService");
      //edm::Service<TFileService> fs;
      //tree_ = fs->make<TTree>("elink", "per-elink Aurora wire sizes");
      //tree_->Branch("stream_group", &b_group_, "stream_group/I");
      //tree_->Branch("ne", &b_ne_, "ne/I");
      //tree_->Branch("detid", &b_detid_, "detid/i");
      //tree_->Branch("elink", &b_elink_, "elink/I");
      //tree_->Branch("bits", &b_bits_, "bits/I");
      //tree_->Branch("section", &b_section_, "section/I");
      //tree_->Branch("layer", &b_layer_, "layer/I");
      //tree_->Branch("ring", &b_ring_, "ring/I");
      //tree_->Branch("subtype", &b_subtype_, "subtype/I");
}

void Phase2ITValidateELink::dqmBeginRun(const edm::Run&, const edm::EventSetup& iSetup) {
    cablingMap_ = &iSetup.getData(cablingMapToken_);
}

void Phase2ITValidateELink::bookHistograms(DQMStore::IBooker& ibooker,
                           edm::Run const&,
                           edm::EventSetup const&) {
  ibooker.setCurrentFolder(folder_);
  meELinkSize_ = ibooker.book1D("eLinkSize",
                              "ELink payload size;bytes;ELink entries",
                              200, 0., 16000.);
}

void Phase2ITValidateELink::analyze(const edm::Event& iEvent, const edm::EventSetup& iSetup) {
  edm::Handle<edm::DetSetVector<Phase2ITAuroraBitStream>> handle;
  iEvent.getByToken(auroraToken_, handle);
  if (!handle.isValid() || handle->empty())
    return;

  const auto& cablingMap = iSetup.getData(cablingMapToken_);

  //for (const auto& detset : *handle) {
  //  b_detid_ = detset.id;
  //  const auto& info = cablingMap.getModuleInfo(detset.id);
  //  b_section_ = static_cast<int>(info.section);
  //  b_layer_ = static_cast<int>(info.layer);
  //  b_ring_ = static_cast<int>(info.ring);
  //  b_subtype_ = static_cast<int>(info.subtype);
  //  for (const auto& aurora : detset) {
  //    b_elink_ = aurora.get_chipId();  // elink index within the module
  //    b_ne_ = aurora.get_eventsPerStream();
  //    for (const auto& stream : aurora.get_auroraStreams()) {
  //      b_bits_ = static_cast<int>(stream.size());
  //      tree_->Fill();
  //      meELinkSize_->Fill(b_bits_);
  //    }
  //  }
  //}
  //b_group_++;
  
  for (const auto& detset : *handle) {
    for (const auto& aurora : detset) {
      for (const auto& stream : aurora.get_auroraStreams()) {
        meELinkSize_->Fill(static_cast<int>(stream.size()));
      }
    }
  }
  
}

void Phase2ITValidateELink::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("auroraBitStream", edm::InputTag("BitStreamToRawProducer"));
  //?desc.addUntracked<int>("firstFed", 0);
  //?desc.addUntracked<int>("nFeds", 576);
  desc.addUntracked<std::string>("folder", "Phase2IT/RawData");
  descriptions.add("Phase2ITValidateELink", desc);
}

DEFINE_FWK_MODULE(Phase2ITValidateELink);
