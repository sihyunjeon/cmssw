// Recovery test: the digis handed to the packer against the digis an unpacking
// flow gives back. Fills pixel occupancy maps of both and of their difference,
// and tallies the round trip digi by digi.
//
// The output is read either as edm::DetSetVector<PixelDigi> (the legacy split
// chain and the fused producer) or as the digi SoA (the Alpaka chain), so the
// same analyzer covers all three flows.

#include <cstdint>
#include <string>
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

class Phase2ITDigiRecovery : public edm::one::EDAnalyzer<> {
public:
  explicit Phase2ITDigiRecovery(const edm::ParameterSet& iConfig)
      : digisToken_(consumes<edm::DetSetVector<PixelDigi>>(iConfig.getParameter<edm::InputTag>("digis"))),
        unpackedTag_(iConfig.getParameter<edm::InputTag>("unpacked")),
        unpackedSoATag_(iConfig.getParameter<edm::InputTag>("unpackedSoA")),
        failOnMismatch_(iConfig.getParameter<bool>("failOnMismatch")) {
    const bool hasDetSet = !unpackedTag_.label().empty();
    const bool hasSoA = !unpackedSoATag_.label().empty();
    if (hasDetSet == hasSoA)
      throw cms::Exception("Phase2ITDigiRecovery") << "set exactly one of 'unpacked' and 'unpackedSoA'";
    if (hasDetSet)
      unpackedToken_ = consumes<edm::DetSetVector<PixelDigi>>(unpackedTag_);
    else
      unpackedSoAToken_ = consumes<SiPixelDigisHost>(unpackedSoATag_);

    const int nRow = iConfig.getParameter<int>("maxRow");
    const int nCol = iConfig.getParameter<int>("maxCol");
    edm::Service<TFileService> fs;
    auto map = [&](const char* n, const char* t) {
      return fs->make<TH2D>(n, t, nCol, 0., double(nCol), nRow, 0., double(nRow));
    };
    hIn_ = map("input", "Input;col;row");
    hOut_ = map("output", "Output;col;row");
    hDelta_ = map("delta", "#Delta(ADC);col;row");
    hAdcDelta_ = fs->make<TH1D>("adcDelta", "per-pixel ADC difference;out #minus in;pixels", 511, -255.5, 255.5);
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("digis", edm::InputTag("simSiPixelDigis", "Pixel"));
    desc.add<edm::InputTag>("unpacked", edm::InputTag(""));
    desc.add<edm::InputTag>("unpackedSoA", edm::InputTag(""));
    desc.add<int>("maxRow", 1400);
    desc.add<int>("maxCol", 450);
    desc.add<bool>("failOnMismatch", false);
    descriptions.add("phase2ITDigiRecovery", desc);
  }

  void analyze(const edm::Event& iEvent, const edm::EventSetup&) override {
    // a pixel fires at most once per event, so its position is a unique key
    auto key = [](uint32_t id, uint32_t row, uint32_t col) {
      return (uint64_t(id) << 21) | (uint64_t(row) << 10) | col;
    };

    std::unordered_map<uint64_t, int> in;
    uint64_t nIn = 0;
    for (const auto& detSet : iEvent.get(digisToken_)) {
      for (const auto& digi : detSet) {
        in[key(detSet.id, digi.row(), digi.column())] = digi.adc();
        hIn_->Fill(digi.column(), digi.row(), digi.adc());
        ++nIn;
      }
    }

    // walk the unpacked digis against that map: every one either pairs with an
    // input pixel (equal or differing ADC) or has no input at all
    uint64_t nOut = 0, onlyOut = 0, adcDiff = 0;
    auto compare = [&](uint32_t id, uint32_t row, uint32_t col, int adc) {
      hOut_->Fill(col, row, adc);
      ++nOut;
      auto it = in.find(key(id, row, col));
      if (it == in.end()) {
        ++onlyOut;
        return;
      }
      hAdcDelta_->Fill(adc - it->second);
      if (adc != it->second)
        ++adcDiff;
      in.erase(it);
    };

    if (!unpackedTag_.label().empty()) {
      for (const auto& detSet : iEvent.get(unpackedToken_))
        for (const auto& digi : detSet)
          compare(detSet.id, digi.row(), digi.column(), digi.adc());
    } else {
      const auto& soa = iEvent.get(unpackedSoAToken_);
      const auto& view = soa.const_view();
      for (uint32_t i = 0; i < soa.nDigis(); ++i)
        compare(view[i].rawIdArr(), view[i].xx(), view[i].yy(), view[i].adc());
    }
    const uint64_t onlyIn = in.size();  // whatever the output never claimed

    nIn_ += nIn;
    nOut_ += nOut;
    onlyIn_ += onlyIn;
    onlyOut_ += onlyOut;
    adcDiff_ += adcDiff;
    edm::LogPrint("Phase2ITDigiRecovery") << "event " << iEvent.id().event() << ": in " << nIn << ", out " << nOut
                                          << ", only-in " << onlyIn << ", only-out " << onlyOut << ", adc-differs "
                                          << adcDiff;
  }

  void endJob() override {
    hDelta_->Add(hOut_, hIn_, 1., -1.);
    const uint64_t bad = onlyIn_ + onlyOut_ + adcDiff_;
    edm::LogPrint("Phase2ITDigiRecovery") << "TOTAL: in " << nIn_ << ", out " << nOut_ << ", only-in " << onlyIn_
                                          << ", only-out " << onlyOut_ << ", adc-differs " << adcDiff_
                                          << (bad ? "" : "  (exact round trip)");
    if (failOnMismatch_ && bad)
      throw cms::Exception("Phase2ITDigiRecovery") << "round trip not exact: " << bad << " digis";
  }

private:
  const edm::EDGetTokenT<edm::DetSetVector<PixelDigi>> digisToken_;
  const edm::InputTag unpackedTag_, unpackedSoATag_;
  const bool failOnMismatch_;
  edm::EDGetTokenT<edm::DetSetVector<PixelDigi>> unpackedToken_;
  edm::EDGetTokenT<SiPixelDigisHost> unpackedSoAToken_;
  uint64_t nIn_ = 0, nOut_ = 0, onlyIn_ = 0, onlyOut_ = 0, adcDiff_ = 0;
  TH1D* hAdcDelta_;
  TH2D *hIn_, *hOut_, *hDelta_;
};

DEFINE_FWK_MODULE(Phase2ITDigiRecovery);
