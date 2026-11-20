// -*- C++ -*-
// Package:    EventFilter/Phase2PixelRawToDigi
// Class:      Phase2ITRawToBitStreamProducer
// Description: Alpaka unpacker stage 1: locates each module's and chip's words in the FED bodies
// Maintainer: Si Hyun Jeon, shjeon@cern.ch

#include <cstring>
#include <utility>
#include <vector>

#include <alpaka/alpaka.hpp>

#include "DataFormats/FEDRawData/interface/RawDataBuffer.h"
#include "DataFormats/FEDRawData/interface/SLinkRocketHeaders.h"
#include "DataFormats/Phase2ITBitStreamSoA/interface/Phase2ITModuleMapHost.h"
#include "DataFormats/Phase2ITBitStreamSoA/interface/alpaka/Phase2ITModuleStreamSoACollection.h"
#include "DataFormats/Phase2ITBitStreamSoA/interface/alpaka/Phase2ITModuleMapDevice.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2DAQFormatSpecification.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2ITUnpacker.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2ITModuleMapRecord.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
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
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/stream/EDProducer.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"

#include "Phase2ITUnpackerKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  using namespace Phase2DAQFormatSpecification;

  class Phase2ITRawToBitStreamProducer : public stream::EDProducer<> {
  public:
    explicit Phase2ITRawToBitStreamProducer(const edm::ParameterSet& iConfig);
    ~Phase2ITRawToBitStreamProducer() override = default;

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

    void produce(device::Event& iEvent, device::EventSetup const& iSetup) override;

  private:
    static Phase2ITUnpacker::ModuleMap moduleMap(const Phase2ITModuleMapDevice& esMap,
                                                 const int32_t* fedWordBase,
                                                 const int32_t* fedTrailerStart);

    const edm::EDGetTokenT<RawDataBuffer> rawToken_;
    const device::EDPutToken<Phase2ITModuleStreamSoACollection> modulePutToken_;
    const device::EDPutToken<Phase2ITChipStreamSoACollection> chipPutToken_;
    const device::EDPutToken<Phase2ITRawBytesSoACollection> bytesPutToken_;
    // Module map, built per IOV by Phase2ITModuleMapESProducer
    const device::ESGetToken<Phase2ITModuleMapDevice, Phase2ITModuleMapRecord> mapToken_;
    const edm::ESGetToken<Phase2ITModuleMapHost, Phase2ITModuleMapRecord> mapHostToken_;
    // One block per FED in the table kernel, or one thread per module with no FED grouping.
    const bool tablePerFed_;
    // Threads per block for the table kernel.
    const uint32_t blockSize_;
  };

  Phase2ITRawToBitStreamProducer::Phase2ITRawToBitStreamProducer(const edm::ParameterSet& iConfig)
      : EDProducer<>(iConfig),
        rawToken_(consumes<RawDataBuffer>(iConfig.getParameter<edm::InputTag>("fedRawDataCollection"))),
        modulePutToken_(produces()),
        chipPutToken_(produces()),
        bytesPutToken_(produces()),
        mapToken_(esConsumes()),
        mapHostToken_(esConsumes()),
        tablePerFed_(iConfig.getParameter<bool>("tablePerFed")),
        blockSize_(iConfig.getParameter<uint32_t>("blockSize")) {
    Phase2ITUnpacker::checkBlockSize(blockSize_, "Phase2ITRawToBitStreamProducer");
  }

  void Phase2ITRawToBitStreamProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("fedRawDataCollection", edm::InputTag("BitStreamToRawProducer"));
    desc.add<bool>("tablePerFed", true);
    desc.add<uint32_t>("blockSize", Phase2ITUnpacker::kDefaultBlockSize);
    descriptions.addWithDefaultLabel(desc);
  }

  Phase2ITUnpacker::ModuleMap Phase2ITRawToBitStreamProducer::moduleMap(const Phase2ITModuleMapDevice& esMap,
                                                                        const int32_t* fedWordBase,
                                                                        const int32_t* fedTrailerStart) {
    auto const mods = esMap.const_view().modules();
    auto const feds = esMap.const_view().feds();
    return {fedWordBase,
            fedTrailerStart,
            feds.modStart().data(),
            mods.fedIdx().data(),
            mods.detId().data(),
            mods.subtype().data(),
            mods.nChips().data(),
            mods.geomIdx().data(),
            feds.metadata().size() - 1,  // trailing row closes the last range
            mods.metadata().size()};
  }

  void Phase2ITRawToBitStreamProducer::produce(device::Event& iEvent, device::EventSetup const& iSetup) {
    auto& queue = iEvent.queue();

    auto const& esMapHost = iSetup.getData(mapHostToken_);
    auto const fedsH = esMapHost.const_view().feds();
    const int nFeds = fedsH.metadata().size() - 1;  // trailing row closes the last range
    const int nModules = esMapHost.const_view().modules().metadata().size();

    // same input checks as RawToPixelProducer
    const auto rawHandle = iEvent.getHandle(rawToken_);
    if (!rawHandle.isValid())
      throw cms::Exception("Phase2ITRawToBitStreamProducer") << "Invalid RawDataBuffer";

    // queue-ordered, so the per-event host buffers outlive the copies queued from them
    auto fedWordBaseH = cms::alpakatools::make_host_buffer<int32_t[]>(queue, nFeds);
    auto fedTrailerStartH = cms::alpakatools::make_host_buffer<int32_t[]>(queue, nFeds);
    std::vector<int32_t> fedSizeWords(nFeds);

    // Strip the SLinkRocket wrapper and concatenate the IT bodies
    constexpr unsigned int kSlrHdrBytes = sizeof(SLinkRocketHeader_v3);
    int32_t totalWords = 0;
    for (int f = 0; f < nFeds; ++f) {
      const int fedId = fedsH[f].fedId();
      auto frag = rawHandle->fragmentData(static_cast<uint32_t>(fedId));
      if (!frag.isValid())
        throw cms::Exception("Phase2ITRawToBitStreamProducer")
            << "Missing RawDataBuffer fragment for fed " << fedId
            << ": cabling map lists this FED but the buffer has no source for it.";
      const auto span = frag.data();
      int bodyWords = 0;
      const unsigned char* body = ::Phase2ITUnpacker::stripSLinkWrapper(span.data(), span.size(), fedId, bodyWords);
      if (!::Phase2ITUnpacker::verifyHeaderTrailerPattern(body, 0))
        throw cms::Exception("Phase2ITRawToBitStreamProducer") << "Invalid header in FEDRawData for fed " << fedId;
      // the last module of a FED ends at its trailer, as in RawToPixelProducer
      const int trailerStart = ::Phase2ITUnpacker::findTrailerStart(body, bodyWords);
      if (trailerStart < 0)
        throw cms::Exception("Phase2ITRawToBitStreamProducer") << "Invalid trailer in FEDRawData for fed " << fedId;
      fedWordBaseH.data()[f] = totalWords;
      fedTrailerStartH.data()[f] = trailerStart;
      fedSizeWords[f] = bodyWords;
      totalWords += bodyWords;
    }

    const int32_t totalBytes = totalWords * BYTES_PER_WORD;
    Phase2ITRawBytesHost bytesH(queue, totalBytes);
    for (int f = 0; f < nFeds; ++f) {
      const auto span = rawHandle->fragmentData(static_cast<uint32_t>(fedsH[f].fedId())).data();
      std::memcpy(bytesH.view().byte().data() + fedWordBaseH.data()[f] * BYTES_PER_WORD,
                  span.data() + kSlrHdrBytes,
                  fedSizeWords[f] * BYTES_PER_WORD);
    }
    Phase2ITRawBytesSoACollection bytesD(queue, totalBytes);
    alpaka::memcpy(queue, bytesD.buffer(), bytesH.buffer());
    auto fedWordBaseD = cms::alpakatools::make_device_buffer<int32_t[]>(queue, nFeds);
    auto fedTrailerStartD = cms::alpakatools::make_device_buffer<int32_t[]>(queue, nFeds);
    alpaka::memcpy(queue, fedWordBaseD, fedWordBaseH);
    alpaka::memcpy(queue, fedTrailerStartD, fedTrailerStartH);

    // every module in the map gets a row and kChipSlots chip rows, so the tables are sized without reading the data
    Phase2ITModuleStreamSoACollection modules(queue, nModules);
    Phase2ITChipStreamSoACollection chips(queue, Phase2ITUnpacker::kChipSlots * nModules);
    Phase2ITUnpacker::runModuleChipTableKernel(
        queue,
        bytesD.view().byte().data(),
        moduleMap(iSetup.getData(mapToken_), fedWordBaseD.data(), fedTrailerStartD.data()),
        modules.view(),
        chips.view(),
        tablePerFed_,
        blockSize_);

    iEvent.emplace(modulePutToken_, std::move(modules));
    iEvent.emplace(chipPutToken_, std::move(chips));
    iEvent.emplace(bytesPutToken_, std::move(bytesD));
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(Phase2ITRawToBitStreamProducer);
