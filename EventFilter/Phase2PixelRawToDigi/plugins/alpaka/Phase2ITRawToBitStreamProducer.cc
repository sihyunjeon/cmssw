// -*- C++ -*-
// Package:    EventFilter/Phase2PixelRawToDigi
// Class:      Phase2ITRawToBitStreamProducer
// Description: Portable counterpart of RawToBitStreamProducer. Copies the IT
//              FED bodies to the device once per event and builds a per-chip
//              index into that byte buffer, one thread per module.
// Maintainer: Si Hyun Jeon, shjeon@cern.ch

#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include <alpaka/alpaka.hpp>

#include "DataFormats/FEDRawData/interface/RawDataBuffer.h"
#include "DataFormats/FEDRawData/interface/SLinkRocketHeaders.h"
#include "DataFormats/Phase2ITBitStreamSoA/interface/Phase2ITModuleMapHost.h"
#include "DataFormats/Phase2ITBitStreamSoA/interface/alpaka/Phase2ITChipBitStreamSoACollection.h"
#include "DataFormats/Phase2ITBitStreamSoA/interface/alpaka/Phase2ITModuleMapDevice.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2DAQFormatSpecification.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2ITUnpacker.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2ITModuleMapRecord.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/ESGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/stream/SynchronizingEDProducer.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"

#include "Phase2ITUnpackerKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  using namespace Phase2ITSpec;

  class Phase2ITRawToBitStreamProducer : public stream::SynchronizingEDProducer<> {
  public:
    explicit Phase2ITRawToBitStreamProducer(const edm::ParameterSet& iConfig);
    ~Phase2ITRawToBitStreamProducer() override = default;

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

    void acquire(device::Event const& iEvent, device::EventSetup const& iSetup) override;
    void produce(device::Event& iEvent, device::EventSetup const& iSetup) override;

  private:
    Phase2ITUnpacker::ModuleMap moduleMap(const Phase2ITModuleMapDevice& esMap) const;

    const edm::EDGetTokenT<RawDataBuffer> rawToken_;
    const device::EDPutToken<Phase2ITChipBitStreamSoACollection> chipPutToken_;
    const device::EDPutToken<Phase2ITRawBytesSoACollection> bytesPutToken_;
    // Built once per IOV by Phase2ITModuleMapESProducer; the host view is only
    // needed to walk the RawDataBuffer fragments in FED order.
    const device::ESGetToken<Phase2ITModuleMapDevice, Phase2ITModuleMapRecord> mapToken_;
    const edm::ESGetToken<Phase2ITModuleMapHost, Phase2ITModuleMapRecord> mapHostToken_;

    int nModules_ = 0;
    std::optional<cms::alpakatools::host_buffer<int32_t[]>> fedWordBaseH_, fedSizeWordsH_;
    std::optional<cms::alpakatools::device_buffer<Device, int32_t[]>> fedWordBaseD_, fedSizeWordsD_;
    std::optional<cms::alpakatools::host_buffer<uint32_t[]>> countsH_, offsetsH_;
    std::optional<cms::alpakatools::device_buffer<Device, uint32_t[]>> countsD_, offsetsD_;

    // Per-event: the raw bytes product is built in acquire() and moved out in produce()
    std::optional<Phase2ITRawBytesHost> bytesH_;
    std::optional<Phase2ITRawBytesSoACollection> bytesD_;
    bool hasData_ = false;
  };

  Phase2ITRawToBitStreamProducer::Phase2ITRawToBitStreamProducer(const edm::ParameterSet& iConfig)
      : SynchronizingEDProducer(iConfig),
        rawToken_(consumes<RawDataBuffer>(iConfig.getParameter<edm::InputTag>("fedRawDataCollection"))),
        chipPutToken_(produces()),
        bytesPutToken_(produces()),
        mapToken_(esConsumes()),
        mapHostToken_(esConsumes()) {}

  void Phase2ITRawToBitStreamProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("fedRawDataCollection", edm::InputTag("BitStreamToRawProducer"));
    descriptions.addWithDefaultLabel(desc);
  }

  Phase2ITUnpacker::ModuleMap Phase2ITRawToBitStreamProducer::moduleMap(const Phase2ITModuleMapDevice& esMap) const {
    auto const mods = esMap.const_view<Phase2ITModuleMapSoA>();
    auto const feds = esMap.const_view<Phase2ITFedMapSoA>();
    return {fedWordBaseD_->data(),
            fedSizeWordsD_->data(),
            feds.modStart().data(),
            mods.fedIdx().data(),
            mods.detId().data(),
            mods.subtype().data(),
            mods.geomIdx().data(),
            mods.metadata().size()};
  }

  void Phase2ITRawToBitStreamProducer::acquire(device::Event const& iEvent, device::EventSetup const& iSetup) {
    auto& queue = iEvent.queue();

    // The map itself is already on the device; the host copy only tells us which
    // FEDs to read, and in what order, so the module rows line up with it.
    auto const& esMapHost = iSetup.getData(mapHostToken_);
    auto const fedsH = esMapHost.const_view<Phase2ITFedMapSoA>();
    const int nFeds = fedsH.metadata().size() - 1;  // trailing row closes the last range
    nModules_ = esMapHost.const_view<Phase2ITModuleMapSoA>().metadata().size();
    const int nModules = nModules_;

    if (!fedWordBaseH_) {
      fedWordBaseH_ = cms::alpakatools::make_host_buffer<int32_t[]>(queue, nFeds);
      fedSizeWordsH_ = cms::alpakatools::make_host_buffer<int32_t[]>(queue, nFeds);
      fedWordBaseD_ = cms::alpakatools::make_device_buffer<int32_t[]>(queue, nFeds);
      fedSizeWordsD_ = cms::alpakatools::make_device_buffer<int32_t[]>(queue, nFeds);
      countsH_ = cms::alpakatools::make_host_buffer<uint32_t[]>(queue, nModules);
      offsetsH_ = cms::alpakatools::make_host_buffer<uint32_t[]>(queue, nModules);
      countsD_ = cms::alpakatools::make_device_buffer<uint32_t[]>(queue, nModules);
      offsetsD_ = cms::alpakatools::make_device_buffer<uint32_t[]>(queue, nModules);
    }

    const auto rawHandle = iEvent.getHandle(rawToken_);
    hasData_ = rawHandle.isValid();
    if (!hasData_)
      return;

    // Strip the SLinkRocket wrapper and concatenate the IT bodies
    constexpr unsigned int kSlrHdrBytes = sizeof(SLinkRocketHeader_v3);
    constexpr unsigned int kSlrTrlBytes = sizeof(SLinkRocketTrailer_v3);
    int32_t totalWords = 0;
    for (int f = 0; f < nFeds; ++f) {
      auto frag = rawHandle->fragmentData(static_cast<uint32_t>(fedsH[f].fedId()));
      if (!frag.isValid())
        throw cms::Exception("Phase2ITRawToBitStreamProducer") << "Missing RawDataBuffer fragment for fed " << fedsH[f].fedId();
      const auto span = frag.data();
      int bodyWords = 0;
      ::Phase2ITUnpacker::stripSLinkWrapper(span.data(), span.size(), fedsH[f].fedId(), bodyWords);
      fedWordBaseH_->data()[f] = totalWords;
      fedSizeWordsH_->data()[f] = bodyWords;
      totalWords += bodyWords;
    }

    const int32_t totalBytes = totalWords * BYTES_PER_WORD;
    bytesH_.emplace(totalBytes, queue);
    for (int f = 0; f < nFeds; ++f) {
      const auto span = rawHandle->fragmentData(static_cast<uint32_t>(fedsH[f].fedId())).data();
      std::memcpy(bytesH_->view().byte().data() + fedWordBaseH_->data()[f] * BYTES_PER_WORD,
                  span.data() + kSlrHdrBytes,
                  fedSizeWordsH_->data()[f] * BYTES_PER_WORD);
    }
    bytesD_.emplace(totalBytes, queue);
    alpaka::memcpy(queue, bytesD_->buffer(), bytesH_->buffer());
    alpaka::memcpy(queue, *fedWordBaseD_, *fedWordBaseH_);
    alpaka::memcpy(queue, *fedSizeWordsD_, *fedSizeWordsH_);

    Phase2ITUnpacker::runChipCountKernel(queue, bytesD_->view().byte().data(), moduleMap(iSetup.getData(mapToken_)), countsD_->data());
    alpaka::memcpy(queue, *countsH_, *countsD_);
  }

  void Phase2ITRawToBitStreamProducer::produce(device::Event& iEvent, device::EventSetup const& iSetup) {
    auto& queue = iEvent.queue();

    if (!hasData_) {
      iEvent.emplace(chipPutToken_, 0, queue);
      iEvent.emplace(bytesPutToken_, 0, queue);
      return;
    }

    uint32_t nChips = 0;
    for (int m = 0; m < nModules_; ++m) {
      offsetsH_->data()[m] = nChips;
      nChips += countsH_->data()[m];
    }
    alpaka::memcpy(queue, *offsetsD_, *offsetsH_);

    Phase2ITChipBitStreamSoACollection chips(nChips, queue);
    Phase2ITUnpacker::runChipFillKernel(queue, bytesD_->view().byte().data(), moduleMap(iSetup.getData(mapToken_)), offsetsD_->data(), chips.view());

    iEvent.emplace(chipPutToken_, std::move(chips));
    iEvent.emplace(bytesPutToken_, std::move(*bytesD_));
    bytesD_.reset();
    bytesH_.reset();
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(Phase2ITRawToBitStreamProducer);
