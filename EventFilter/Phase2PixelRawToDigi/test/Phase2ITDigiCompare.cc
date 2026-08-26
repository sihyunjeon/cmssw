// Compares the legacy unpacking chain (RawToBitStream + BitStreamToPixel)
// against the Alpaka unpacker digi SoA, digi by digi. Throws at endJob on any
// mismatch so a test job fails loudly.

#include <cstdint>
#include <unordered_map>

#include "CommonTools/UtilAlgos/interface/TFileService.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "TH1D.h"
#include "TH2D.h"

#include "DataFormats/Common/interface/DetSetVector.h"
#include "DataFormats/SiPixelDigi/interface/PixelDigi.h"
#include "DataFormats/SiPixelDigiSoA/interface/SiPixelDigisHost.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/one/EDAnalyzer.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/Exception.h"

class Phase2ITDigiCompare : public edm::one::EDAnalyzer<> {
public:
  explicit Phase2ITDigiCompare(const edm::ParameterSet& iConfig)
      : legacyToken_(consumes<edm::DetSetVector<PixelDigi>>(iConfig.getParameter<edm::InputTag>("legacy"))),
        soaToken_(consumes<SiPixelDigisHost>(iConfig.getParameter<edm::InputTag>("soa"))) {
    edm::Service<TFileService> fs;
    hAdcLegacy_ = fs->make<TH1D>("adcLegacy", "ADC spectrum;ADC;digis", 16, -0.5, 15.5);
    hAdcSoA_ = fs->make<TH1D>("adcSoA", "ADC spectrum;ADC;digis", 16, -0.5, 15.5);
    hAdcDelta_ = fs->make<TH1D>("adcDelta", "ADC difference;alpaka #minus legacy;digis", 31, -15.5, 15.5);
    hAdcCorr_ = fs->make<TH2D>(
        "adcCorr", "ADC correlation;legacy ADC;alpaka ADC", 16, -0.5, 15.5, 16, -0.5, 15.5);
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("legacy", edm::InputTag("bitstreamToPixelProducer"));
    desc.add<edm::InputTag>("soa", edm::InputTag("phase2ITRawToDigi"));
    descriptions.add("phase2ITDigiCompare", desc);
  }

  void analyze(const edm::Event& iEvent, const edm::EventSetup&) override {
    // key = detId(32b) | row(11b) | col(10b) | adc(8b)
    auto key = [](uint32_t id, uint32_t row, uint32_t col, uint32_t adc) {
      return (uint64_t(id) << 29) | (uint64_t(row) << 18) | (col << 8) | adc;
    };

    // position-only key, to pair the two collections and compare their ADCs
    auto posKey = [](uint32_t id, uint32_t row, uint32_t col) {
      return (uint64_t(id) << 21) | (uint64_t(row) << 10) | col;
    };

    std::unordered_map<uint64_t, int> tally;
    std::unordered_map<uint64_t, int> adcAt;
    uint64_t nLegacy = 0;
    const auto& legacy = iEvent.get(legacyToken_);
    for (const auto& detSet : legacy) {
      for (const auto& digi : detSet) {
        ++tally[key(detSet.id, digi.row(), digi.column(), digi.adc())];
        adcAt[posKey(detSet.id, digi.row(), digi.column())] = digi.adc();
        hAdcLegacy_->Fill(digi.adc());
        ++nLegacy;
      }
    }

    const auto& soa = iEvent.get(soaToken_);
    const auto& view = soa.const_view();
    const uint32_t nSoA = soa.nDigis();
    uint64_t nOnlySoA = 0;
    for (uint32_t i = 0; i < nSoA; ++i) {
      hAdcSoA_->Fill(view[i].adc());
      auto pos = adcAt.find(posKey(view[i].rawIdArr(), view[i].xx(), view[i].yy()));
      if (pos != adcAt.end()) {
        hAdcDelta_->Fill(int(view[i].adc()) - pos->second);
        hAdcCorr_->Fill(pos->second, view[i].adc());
      }
      auto it = tally.find(key(view[i].rawIdArr(), view[i].xx(), view[i].yy(), view[i].adc()));
      if (it == tally.end() || it->second == 0)
        ++nOnlySoA;
      else
        --it->second;
    }
    uint64_t nOnlyLegacy = 0;
    for (const auto& [k, n] : tally)
      nOnlyLegacy += n;

    nTotalLegacy_ += nLegacy;
    nTotalSoA_ += nSoA;
    nTotalOnlyLegacy_ += nOnlyLegacy;
    nTotalOnlySoA_ += nOnlySoA;
    edm::LogPrint("Phase2ITDigiCompare") << "event " << iEvent.id().event() << ": legacy " << nLegacy << ", soa "
                                         << nSoA << ", only-legacy " << nOnlyLegacy << ", only-soa " << nOnlySoA;
  }

  void endJob() override {
    edm::LogPrint("Phase2ITDigiCompare") << "TOTAL: legacy " << nTotalLegacy_ << ", soa " << nTotalSoA_
                                         << ", only-legacy " << nTotalOnlyLegacy_ << ", only-soa " << nTotalOnlySoA_;
    if (nTotalOnlyLegacy_ != 0 || nTotalOnlySoA_ != 0)
      throw cms::Exception("Phase2ITDigiCompare")
          << "Digi mismatch: only-legacy " << nTotalOnlyLegacy_ << ", only-soa " << nTotalOnlySoA_;
  }

private:
  const edm::EDGetTokenT<edm::DetSetVector<PixelDigi>> legacyToken_;
  const edm::EDGetTokenT<SiPixelDigisHost> soaToken_;
  uint64_t nTotalLegacy_ = 0, nTotalSoA_ = 0, nTotalOnlyLegacy_ = 0, nTotalOnlySoA_ = 0;
  TH1D *hAdcLegacy_, *hAdcSoA_, *hAdcDelta_;
  TH2D *hAdcCorr_;
};

DEFINE_FWK_MODULE(Phase2ITDigiCompare);
