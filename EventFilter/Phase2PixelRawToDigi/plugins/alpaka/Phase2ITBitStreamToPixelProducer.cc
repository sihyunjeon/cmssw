// -*- C++ -*-
// Package:    EventFilter/Phase2PixelRawToDigi
// Class:      Phase2ITBitStreamToPixelProducer
// Description: Portable counterpart of BitStreamToPixelProducer.
//              Decodes each chip's bitstream in its own thread into SiPixelDigis
// Maintainer: Si Hyun Jeon, shjeon@cern.ch

#include <optional>
#include <string>

#include <alpaka/alpaka.hpp>

#include "DataFormats/Phase2ITBitStreamSoA/interface/alpaka/Phase2ITModuleStreamSoACollection.h"
#include "DataFormats/SiPixelDigiSoA/interface/alpaka/SiPixelDigisSoACollection.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2ITUnpacker.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
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

  class Phase2ITBitStreamToPixelProducer : public stream::SynchronizingEDProducer<> {
  public:
    explicit Phase2ITBitStreamToPixelProducer(const edm::ParameterSet& iConfig);
    ~Phase2ITBitStreamToPixelProducer() override = default;

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

    void acquire(device::Event const& iEvent, device::EventSetup const& iSetup) override;
    void produce(device::Event& iEvent, device::EventSetup const& iSetup) override;

  private:
    const device::EDGetToken<Phase2ITModuleStreamSoACollection> moduleToken_;
    const device::EDGetToken<Phase2ITChipStreamSoACollection> chipToken_;
    const device::EDGetToken<Phase2ITRawBytesSoACollection> bytesToken_;
    const device::EDPutToken<SiPixelDigisSoACollection> digiPutToken_;
    // Must match the dropTot setting that produced the bitstream.
    const bool dropTot_;
    // Must match the encoder's handleGapPixels mode.
    const bool keepMode_;
    // Threads per block for the two kernels below: one chip per thread in the decode, one module per block in the copy.
    const uint32_t blockSize_;

    std::optional<cms::alpakatools::host_buffer<uint32_t[]>> countsH_, offsetsH_;
    std::optional<cms::alpakatools::device_buffer<Device, uint32_t[]>> countsD_, offsetsD_;
    // decoded hits waiting for their digi positions; grows with the raw data size
    std::optional<cms::alpakatools::device_buffer<Device, Phase2ITUnpacker::DecodedHit[]>> mediatorD_;
    // malformed input found by the decode, indexed by Phase2ITUnpacker::DecodeStat
    std::optional<cms::alpakatools::host_buffer<uint32_t[]>> statsH_;
    std::optional<cms::alpakatools::device_buffer<Device, uint32_t[]>> statsD_;
    uint32_t nModules_ = 0;
    uint32_t nChipSlots_ = 0;
  };

  Phase2ITBitStreamToPixelProducer::Phase2ITBitStreamToPixelProducer(const edm::ParameterSet& iConfig)
      : SynchronizingEDProducer(iConfig),
        moduleToken_(consumes(iConfig.getParameter<edm::InputTag>("phase2ItModuleStream"))),
        chipToken_(consumes(iConfig.getParameter<edm::InputTag>("phase2ItChipStream"))),
        bytesToken_(consumes(iConfig.getParameter<edm::InputTag>("phase2ItRawBytes"))),
        digiPutToken_(produces()),
        dropTot_(iConfig.getParameter<bool>("dropTot")),
        keepMode_(::Phase2ITUnpacker::parseKeepMode(iConfig.getParameter<std::string>("handleGapPixels"),
                                                    "Phase2ITBitStreamToPixelProducer")),
        blockSize_(iConfig.getParameter<uint32_t>("blockSize")) {
    Phase2ITUnpacker::checkBlockSize(blockSize_, "Phase2ITBitStreamToPixelProducer");
  }

  void Phase2ITBitStreamToPixelProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("phase2ItModuleStream", edm::InputTag("phase2ITRawToBitStream"));
    desc.add<edm::InputTag>("phase2ItChipStream", edm::InputTag("phase2ITRawToBitStream"));
    desc.add<edm::InputTag>("phase2ItRawBytes", edm::InputTag("phase2ITRawToBitStream"));
    desc.add<bool>("dropTot", false);
    desc.add<std::string>("handleGapPixels", "DROP");
    desc.add<uint32_t>("blockSize", Phase2ITUnpacker::kDefaultBlockSize);
    descriptions.addWithDefaultLabel(desc);
  }

  void Phase2ITBitStreamToPixelProducer::acquire(device::Event const& iEvent, device::EventSetup const&) {
    auto& queue = iEvent.queue();
    const auto& modules = iEvent.get(moduleToken_);
    const auto& chips = iEvent.get(chipToken_);
    const auto& bytes = iEvent.get(bytesToken_);
    nModules_ = modules.view().metadata().size();
    nChipSlots_ = chips.view().metadata().size();
    if (nModules_ == 0)
      return;

    if (!countsH_ || alpaka::getExtentProduct(*countsH_) < nChipSlots_) {
      countsH_ = cms::alpakatools::make_host_buffer<uint32_t[]>(queue, nChipSlots_);
      offsetsH_ = cms::alpakatools::make_host_buffer<uint32_t[]>(queue, nChipSlots_);
      countsD_ = cms::alpakatools::make_device_buffer<uint32_t[]>(queue, nChipSlots_);
      offsetsD_ = cms::alpakatools::make_device_buffer<uint32_t[]>(queue, nChipSlots_);
    }
    const uint32_t nSlots = Phase2ITUnpacker::mediatorSize(bytes.const_view().metadata().size(), dropTot_);
    if (!mediatorD_ || alpaka::getExtentProduct(*mediatorD_) < nSlots)
      mediatorD_ = cms::alpakatools::make_device_buffer<Phase2ITUnpacker::DecodedHit[]>(queue, nSlots);
    if (!statsD_) {
      statsH_ = cms::alpakatools::make_host_buffer<uint32_t[]>(queue, Phase2ITUnpacker::kNumDecodeStats);
      statsD_ = cms::alpakatools::make_device_buffer<uint32_t[]>(queue, Phase2ITUnpacker::kNumDecodeStats);
    }
    alpaka::memset(queue, *statsD_, 0);
    Phase2ITUnpacker::runDigiDecodeKernel(queue,
                                          bytes.const_view().byte().data(),
                                          modules.const_view(),
                                          chips.const_view(),
                                          dropTot_,
                                          keepMode_,
                                          mediatorD_->data(),
                                          countsD_->data(),
                                          statsD_->data(),
                                          blockSize_);
    alpaka::memcpy(queue, *countsH_, *countsD_);
    alpaka::memcpy(queue, *statsH_, *statsD_);
  }

  void Phase2ITBitStreamToPixelProducer::produce(device::Event& iEvent, device::EventSetup const&) {
    auto& queue = iEvent.queue();

    if (nModules_ == 0) {
      SiPixelDigisSoACollection digis(queue, 0);
      auto buf = digis.buffer();
      alpaka::memset(queue, buf, 0);
      iEvent.emplace(digiPutToken_, std::move(digis));
      return;
    }

    const auto& modules = iEvent.get(moduleToken_);
    const auto& chips = iEvent.get(chipToken_);

    // Sum up to get each chip's write range, in module then chip order
    uint32_t total = 0;
    for (uint32_t c = 0; c < nChipSlots_; ++c) {
      offsetsH_->data()[c] = total;
      total += countsH_->data()[c];
    }
    alpaka::memcpy(queue, *offsetsD_, *offsetsH_);
    // Without these the kernels drop malformed modules, chips and hits silently, which
    // makes corrupt input indistinguishable from an empty detector. The same counts and
    // wording as RawToPixelProducer.
    const uint32_t* stats = statsH_->data();
    ::Phase2ITUnpacker::MalformedCounts bad;
    bad.badOffsetModules = stats[Phase2ITUnpacker::kBadOffsetModules];
    bad.extraChipModules = stats[Phase2ITUnpacker::kExtraChipModules];
    bad.overrunChips = stats[Phase2ITUnpacker::kOverrunChips];
    bad.truncatedChips = stats[Phase2ITUnpacker::kTruncatedChips];
    ::Phase2ITUnpacker::reportMalformed("Phase2ITBitStreamToPixelProducer", bad, nModules_);
    // complete quarter cores inside their module always fit its slots, so this is a safety net
    if (stats[Phase2ITUnpacker::kDroppedHits] != 0)
      edm::LogWarning("Phase2ITBitStreamToPixelProducer")
          << stats[Phase2ITUnpacker::kDroppedHits]
          << " hits dropped: their modules' chip streams hold more hits than their length allows.";

    SiPixelDigisSoACollection digis(queue, total);
    Phase2ITUnpacker::runDigiCopyKernel(queue,
                                        modules.const_view(),
                                        chips.const_view(),
                                        dropTot_,
                                        mediatorD_->data(),
                                        countsD_->data(),
                                        offsetsD_->data(),
                                        digis.view(),
                                        blockSize_);
    iEvent.emplace(digiPutToken_, std::move(digis));
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(Phase2ITBitStreamToPixelProducer);
