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

class Dummy : public DQMEDAnalyzer {
public:
  explicit Dummy(const edm::ParameterSet& iConfig);
  ~Dummy() override = default;
  void dqmBeginRun(const edm::Run& iRun, const edm::EventSetup& iSetup) override;
  void analyze(const edm::Event& iEvent, const edm::EventSetup& iSetup) override;
  void bookHistograms(DQMStore::IBooker& ibooker,
                      edm::Run const& iRun,
                      edm::EventSetup const& iSetup) override;
  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

private:
  const edm::EDGetTokenT<FEDRawDataCollection> fedRawToken_;
  const edm::ESGetToken<TrackerDetToDTCELinkCablingMap,
                        TrackerDetToDTCELinkCablingMapRcd> cablingMapToken_;
  const int firstFed_;
  const int nFeds_;
  const std::string folder_;

  const TrackerDetToDTCELinkCablingMap* cablingMap_ = nullptr;

  MonitorElement* meFedSize_ = nullptr;
};

Dummy::Dummy(const edm::ParameterSet& iConfig)
    : fedRawToken_(consumes<FEDRawDataCollection>(iConfig.getParameter<edm::InputTag>("src"))),
      cablingMapToken_(esConsumes<TrackerDetToDTCELinkCablingMap,
                                  TrackerDetToDTCELinkCablingMapRcd,
                                  edm::Transition::BeginRun>()),
      firstFed_(iConfig.getUntrackedParameter<int>("firstFed", 0)),
      nFeds_(iConfig.getUntrackedParameter<int>("nFeds", 576)),
      folder_(iConfig.getUntrackedParameter<std::string>("folder", "Phase2IT/RawData")) {
  edm::LogInfo("Dummy") << ">>> Construct Dummy";
}

void Dummy::dqmBeginRun(const edm::Run&, const edm::EventSetup& iSetup) {
  // cablingMap_ read here, probably only need this for getting known dtcs?
  // fed id is 0-575, but keep in mind that you have to count the number of dtcs instead of using dtc id directly
  // dtc id is 11..19, 21..29, .., 41..49
  // should we map 11->0, 12->1, .., 19->8, 21->0, ..? and vice versa so that we draw slink occ per dtc
  // 16 x 36 = 576
  cablingMap_ = &iSetup.getData(cablingMapToken_);
}

void Dummy::bookHistograms(DQMStore::IBooker& ibooker,
                           edm::Run const&,
                           edm::EventSetup const&) {
  ibooker.setCurrentFolder(folder_);
  // Only have the whole fed here
  meFedSize_ = ibooker.book1D("fedSize",
                              "FED payload size;bytes;FED entries",
                              200, 0., 16000.);
}

void Dummy::analyze(const edm::Event& iEvent, const edm::EventSetup&) {
  edm::Handle<FEDRawDataCollection> raw;
  iEvent.getByToken(fedRawToken_, raw);
  if (!raw.isValid()) return;
  for (int fid = firstFed_; fid < firstFed_ + nFeds_; ++fid) {
    meFedSize_->Fill(static_cast<double>(raw->FEDData(fid).size()));
  }
}

void Dummy::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("src", edm::InputTag("BitStreamToRawProducer"));
  desc.addUntracked<int>("firstFed", 0);
  desc.addUntracked<int>("nFeds", 576);
  desc.addUntracked<std::string>("folder", "Phase2IT/RawData");
  descriptions.add("dummy", desc);
}

DEFINE_FWK_MODULE(Dummy);
