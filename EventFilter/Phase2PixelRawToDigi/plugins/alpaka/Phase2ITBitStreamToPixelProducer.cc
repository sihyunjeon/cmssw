// -*- C++ -*-
// Package:    EventFilter/Phase2PixelRawToDigi
// Class:      Phase2ITBitStreamToPixelProducer
// Description: Portable counterpart of BitStreamToPixelProducer. Decodes the
//              per-chip bit streams left on the device into SiPixelDigis,
//              one thread per chip.
// Maintainer: Si Hyun Jeon, shjeon@cern.ch

#include <optional>
#include <string>

#include <alpaka/alpaka.hpp>

#include "DataFormats/Phase2ITBitStreamSoA/interface/alpaka/Phase2ITChipBitStreamSoACollection.h"
#include "DataFormats/SiPixelDigiSoA/interface/alpaka/SiPixelDigisSoACollection.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/stream/SynchronizingEDProducer.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"

#include "Phase2ITUnpackerKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  namespace {
    // KEEP moves each chip boundary to the gap midline; DROP and AGGREGATE keep
    // the nominal chip dimensions. Mirrors BitStreamToPixelProducer.
    bool parseKeepMode(const std::string& s) {
      if (s == "KEEP")
        return true;
      if (s == "DROP" || s == "AGGREGATE")
        return false;
      throw cms::Exception("Phase2ITBitStreamToPixelProducer")
          << "handleGapPixels must be one of DROP/KEEP/AGGREGATE, got '" << s << "'";
    }
  }  // namespace

  class Phase2ITBitStreamToPixelProducer : public stream::SynchronizingEDProducer<> {
  public:
    explicit Phase2ITBitStreamToPixelProducer(const edm::ParameterSet& iConfig);
    ~Phase2ITBitStreamToPixelProducer() override = default;

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

    void acquire(device::Event const& iEvent, device::EventSetup const& iSetup) override;
    void produce(device::Event& iEvent, device::EventSetup const& iSetup) override;

  private:
    const device::EDGetToken<Phase2ITChipBitStreamSoACollection> chipToken_;
    const device::EDGetToken<Phase2ITRawBytesSoACollection> bytesToken_;
    const device::EDPutToken<SiPixelDigisSoACollection> digiPutToken_;
    // Must match the dropTot setting that produced the bitstream. When true the encoded stream omits the per-hit 4-bit ToT field.
    const bool dropTot_;
    // Must match the encoder's handleGapPixels mode.
    const bool keepMode_;

    std::optional<cms::alpakatools::host_buffer<uint32_t[]>> countsH_, offsetsH_;
    std::optional<cms::alpakatools::device_buffer<Device, uint32_t[]>> countsD_, offsetsD_;
    uint32_t nChips_ = 0;
  };

  Phase2ITBitStreamToPixelProducer::Phase2ITBitStreamToPixelProducer(const edm::ParameterSet& iConfig)
      : SynchronizingEDProducer(iConfig),
        chipToken_(consumes(iConfig.getParameter<edm::InputTag>("phase2ItChipBitStream"))),
        bytesToken_(consumes(iConfig.getParameter<edm::InputTag>("phase2ItRawBytes"))),
        digiPutToken_(produces()),
        dropTot_(iConfig.getParameter<bool>("dropTot")),
        keepMode_(parseKeepMode(iConfig.getParameter<std::string>("handleGapPixels"))) {}

  void Phase2ITBitStreamToPixelProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("phase2ItChipBitStream", edm::InputTag("phase2ITRawToBitStream"));
    desc.add<edm::InputTag>("phase2ItRawBytes", edm::InputTag("phase2ITRawToBitStream"));
    desc.add<bool>("dropTot", false);
    desc.add<std::string>("handleGapPixels", "DROP");
    descriptions.addWithDefaultLabel(desc);
  }

  void Phase2ITBitStreamToPixelProducer::acquire(device::Event const& iEvent, device::EventSetup const&) {
    auto& queue = iEvent.queue();
    const auto& chips = iEvent.get(chipToken_);
    const auto& bytes = iEvent.get(bytesToken_);
    nChips_ = chips.view().metadata().size();
    if (nChips_ == 0)
      return;

    if (!countsH_ || alpaka::getExtentProduct(*countsH_) < nChips_) {
      countsH_ = cms::alpakatools::make_host_buffer<uint32_t[]>(queue, nChips_);
      offsetsH_ = cms::alpakatools::make_host_buffer<uint32_t[]>(queue, nChips_);
      countsD_ = cms::alpakatools::make_device_buffer<uint32_t[]>(queue, nChips_);
      offsetsD_ = cms::alpakatools::make_device_buffer<uint32_t[]>(queue, nChips_);
    }
    Phase2ITUnpacker::runDigiCountKernel(
        queue, bytes.const_view().byte().data(), chips.const_view(), dropTot_, countsD_->data());
    alpaka::memcpy(queue, *countsH_, *countsD_);
  }

  void Phase2ITBitStreamToPixelProducer::produce(device::Event& iEvent, device::EventSetup const&) {
    auto& queue = iEvent.queue();

    if (nChips_ == 0) {
      SiPixelDigisSoACollection digis(0, queue);
      auto buf = digis.buffer();
      alpaka::memset(queue, buf, 0);
      iEvent.emplace(digiPutToken_, std::move(digis));
      return;
    }

    const auto& chips = iEvent.get(chipToken_);
    const auto& bytes = iEvent.get(bytesToken_);

    // Prefix sum over chips: gives the exact output size and each chip a private
    // write range, so the fill kernel needs no atomics.
    // nModules is deliberately left unset: the clusterizer overwrites it with its
    // own count before anything reads it.
    uint32_t total = 0;
    for (uint32_t c = 0; c < nChips_; ++c) {
      offsetsH_->data()[c] = total;
      total += countsH_->data()[c];
    }
    alpaka::memcpy(queue, *offsetsD_, *offsetsH_);

    SiPixelDigisSoACollection digis(total, queue);
    Phase2ITUnpacker::runDigiFillKernel(queue,
                                        bytes.const_view().byte().data(),
                                        chips.const_view(),
                                        dropTot_,
                                        keepMode_,
                                        offsetsD_->data(),
                                        digis.view());
    iEvent.emplace(digiPutToken_, std::move(digis));
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(Phase2ITBitStreamToPixelProducer);
