// -*- C++ -*-
// Package:    EventFilter/Phase2PixelRawToDigi
// Class:      Phase2ITRawToBitStream
// Description: Alpaka counterpart of RawToBitStreamProducer. Copies the IT FED
//              bodies to the device once per event and builds a per-chip index
//              (Phase2ITChipBitStreamSoA) into that byte buffer, one thread per
//              module. Both stay on the device for Phase2ITBitStreamToDigi.
//
// Author: Si Hyun Jeon, shjeon@cern.ch
//

#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include <alpaka/alpaka.hpp>

#include "CondFormats/DataRecord/interface/TrackerDetToDTCELinkCablingMapRcd.h"
#include "CondFormats/SiPhase2TrackerObjects/interface/TrackerDetToDTCELinkCablingMap.h"
#include "DataFormats/FEDRawData/interface/RawDataBuffer.h"
#include "DataFormats/FEDRawData/interface/SLinkRocketHeaders.h"
#include "DataFormats/Phase2ITBitStreamSoA/interface/alpaka/Phase2ITChipBitStreamSoACollection.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2DAQFormatSpecification.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/SLinkModuleMap.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "Geometry/CommonDetUnit/interface/GeomDet.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/stream/SynchronizingEDProducer.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"

#include "Phase2ITUnpackKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  using namespace Phase2DAQFormatSpecification;

  class Phase2ITRawToBitStream : public stream::SynchronizingEDProducer<> {
  public:
    explicit Phase2ITRawToBitStream(const edm::ParameterSet& iConfig);
    ~Phase2ITRawToBitStream() override = default;

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

    void acquire(device::Event const& iEvent, device::EventSetup const& iSetup) override;
    void produce(device::Event& iEvent, device::EventSetup const& iSetup) override;

  private:
    void buildNav(Queue& queue, const TrackerDetToDTCELinkCablingMap& cabling, const TrackerGeometry& geom);
    phase2it::ModuleNav nav() const;

    const edm::EDGetTokenT<RawDataBuffer> rawToken_;
    const device::EDPutToken<Phase2ITChipBitStreamSoACollection> chipPutToken_;
    const device::EDPutToken<Phase2ITRawBytesSoACollection> bytesPutToken_;
    const edm::ESGetToken<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd> cablingToken_;
    const edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> geomToken_;

    // Host-side navigation, rebuilt when the cabling or geometry IOV changes
    const void* cablingCache_ = nullptr;
    const void* geomCache_ = nullptr;
    std::unique_ptr<SLinkModuleMap> slinkMap_;
    std::vector<int> fedIds_;
    int nModules_ = 0;

    std::optional<cms::alpakatools::device_buffer<Device, int32_t[]>> fedModStartD_;
    std::optional<cms::alpakatools::device_buffer<Device, uint16_t[]>> modFedIdxD_;
    std::optional<cms::alpakatools::device_buffer<Device, uint32_t[]>> modDetIdD_;
    std::optional<cms::alpakatools::device_buffer<Device, uint8_t[]>> modSubtypeD_;
    std::optional<cms::alpakatools::device_buffer<Device, uint16_t[]>> modGeomIdxD_;
    std::optional<cms::alpakatools::host_buffer<int32_t[]>> fedWordBaseH_, fedSizeWordsH_;
    std::optional<cms::alpakatools::device_buffer<Device, int32_t[]>> fedWordBaseD_, fedSizeWordsD_;
    std::optional<cms::alpakatools::host_buffer<uint32_t[]>> countsH_, offsetsH_;
    std::optional<cms::alpakatools::device_buffer<Device, uint32_t[]>> countsD_, offsetsD_;

    // Per-event: the raw bytes product is built in acquire() and moved out in produce()
    std::optional<Phase2ITRawBytesHost> bytesH_;
    std::optional<Phase2ITRawBytesSoACollection> bytesD_;
    bool hasData_ = false;
  };

  Phase2ITRawToBitStream::Phase2ITRawToBitStream(const edm::ParameterSet& iConfig)
      : SynchronizingEDProducer(iConfig),
        rawToken_(consumes<RawDataBuffer>(iConfig.getParameter<edm::InputTag>("fedRawDataCollection"))),
        chipPutToken_(produces()),
        bytesPutToken_(produces()),
        cablingToken_(esConsumes()),
        geomToken_(esConsumes()) {}

  void Phase2ITRawToBitStream::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("fedRawDataCollection", edm::InputTag("BitStreamToRawProducer"));
    descriptions.addWithDefaultLabel(desc);
  }

  phase2it::ModuleNav Phase2ITRawToBitStream::nav() const {
    return {fedWordBaseD_->data(),
            fedSizeWordsD_->data(),
            fedModStartD_->data(),
            modFedIdxD_->data(),
            modDetIdD_->data(),
            modSubtypeD_->data(),
            modGeomIdxD_->data(),
            nModules_};
  }

  void Phase2ITRawToBitStream::buildNav(Queue& queue,
                                        const TrackerDetToDTCELinkCablingMap& cabling,
                                        const TrackerGeometry& geom) {
    slinkMap_ = std::make_unique<SLinkModuleMap>(cabling);

    fedIds_.clear();
    std::vector<int32_t> fedModStart;
    std::vector<uint16_t> modFedIdx;
    std::vector<uint32_t> modDetId;
    std::vector<uint8_t> modSubtype;
    std::vector<uint16_t> modGeomIdx;

    for (const auto& [fedId, detIds] : slinkMap_->fedIdToDetIds()) {
      fedModStart.push_back(static_cast<int32_t>(modDetId.size()));
      const uint16_t fedIdx = static_cast<uint16_t>(fedIds_.size());
      fedIds_.push_back(fedId);
      for (uint32_t detId : detIds) {
        if (!cabling.hasModuleInfo(detId))
          throw cms::Exception("Phase2ITRawToBitStream") << "No ModuleInfo in cabling map for detId " << detId;
        const auto* det = geom.idToDetUnit(DetId(detId));
        if (det == nullptr)
          throw cms::Exception("Phase2ITRawToBitStream") << "No GeomDetUnit for detId " << detId;
        modFedIdx.push_back(fedIdx);
        modDetId.push_back(detId);
        modSubtype.push_back(static_cast<uint8_t>(cabling.getModuleInfo(detId).subtype));
        modGeomIdx.push_back(static_cast<uint16_t>(det->index()));
      }
    }
    fedModStart.push_back(static_cast<int32_t>(modDetId.size()));
    nModules_ = static_cast<int>(modDetId.size());
    const int nFeds = static_cast<int>(fedIds_.size());

    auto upload = [&queue](auto& dst, const auto& src) {
      using T = std::remove_const_t<std::remove_reference_t<decltype(src[0])>>;
      dst = cms::alpakatools::make_device_buffer<T[]>(queue, src.size());
      auto staging = cms::alpakatools::make_host_buffer<T[]>(queue, src.size());
      std::memcpy(staging.data(), src.data(), src.size() * sizeof(T));
      alpaka::memcpy(queue, *dst, staging);
      alpaka::wait(queue);
    };
    upload(fedModStartD_, fedModStart);
    upload(modFedIdxD_, modFedIdx);
    upload(modDetIdD_, modDetId);
    upload(modSubtypeD_, modSubtype);
    upload(modGeomIdxD_, modGeomIdx);

    fedWordBaseH_ = cms::alpakatools::make_host_buffer<int32_t[]>(queue, nFeds);
    fedSizeWordsH_ = cms::alpakatools::make_host_buffer<int32_t[]>(queue, nFeds);
    fedWordBaseD_ = cms::alpakatools::make_device_buffer<int32_t[]>(queue, nFeds);
    fedSizeWordsD_ = cms::alpakatools::make_device_buffer<int32_t[]>(queue, nFeds);
    countsH_ = cms::alpakatools::make_host_buffer<uint32_t[]>(queue, nModules_);
    offsetsH_ = cms::alpakatools::make_host_buffer<uint32_t[]>(queue, nModules_);
    countsD_ = cms::alpakatools::make_device_buffer<uint32_t[]>(queue, nModules_);
    offsetsD_ = cms::alpakatools::make_device_buffer<uint32_t[]>(queue, nModules_);
  }

  void Phase2ITRawToBitStream::acquire(device::Event const& iEvent, device::EventSetup const& iSetup) {
    auto& queue = iEvent.queue();

    const auto& cabling = iSetup.getData(cablingToken_);
    const auto& geom = iSetup.getData(geomToken_);
    if (&cabling != cablingCache_ || &geom != geomCache_) {
      buildNav(queue, cabling, geom);
      cablingCache_ = &cabling;
      geomCache_ = &geom;
    }

    const auto rawHandle = iEvent.getHandle(rawToken_);
    hasData_ = rawHandle.isValid();
    if (!hasData_)
      return;

    // Strip the SLinkRocket wrapper and concatenate the IT bodies
    constexpr unsigned int kSlrHdrBytes = sizeof(SLinkRocketHeader_v3);
    constexpr unsigned int kSlrTrlBytes = sizeof(SLinkRocketTrailer_v3);
    const int nFeds = static_cast<int>(fedIds_.size());
    int32_t totalWords = 0;
    for (int f = 0; f < nFeds; ++f) {
      auto frag = rawHandle->fragmentData(static_cast<uint32_t>(fedIds_[f]));
      if (!frag.isValid())
        throw cms::Exception("Phase2ITRawToBitStream") << "Missing RawDataBuffer fragment for fed " << fedIds_[f];
      const auto span = frag.data();
      const auto* sh = reinterpret_cast<const SLinkRocketHeader_v3*>(span.data());
      const auto* st = reinterpret_cast<const SLinkRocketTrailer_v3*>(span.data() + span.size() - kSlrTrlBytes);
      if (!sh->verifyMarker() || !st->verifyMarker() || st->eventLenBytes() != span.size())
        throw cms::Exception("Phase2ITRawToBitStream") << "Invalid SLinkRocket wrapper for fed " << fedIds_[f];
      const int32_t bodyWords =
          static_cast<int32_t>(span.size() / BYTES_PER_WORD) - (kSlrHdrBytes + kSlrTrlBytes) / BYTES_PER_WORD;
      fedWordBaseH_->data()[f] = totalWords;
      fedSizeWordsH_->data()[f] = bodyWords;
      totalWords += bodyWords;
    }

    const int32_t totalBytes = totalWords * BYTES_PER_WORD;
    bytesH_.emplace(totalBytes, queue);
    for (int f = 0; f < nFeds; ++f) {
      const auto span = rawHandle->fragmentData(static_cast<uint32_t>(fedIds_[f])).data();
      std::memcpy(bytesH_->view().byte().data() + fedWordBaseH_->data()[f] * BYTES_PER_WORD,
                  span.data() + kSlrHdrBytes,
                  fedSizeWordsH_->data()[f] * BYTES_PER_WORD);
    }
    bytesD_.emplace(totalBytes, queue);
    alpaka::memcpy(queue, bytesD_->buffer(), bytesH_->buffer());
    alpaka::memcpy(queue, *fedWordBaseD_, *fedWordBaseH_);
    alpaka::memcpy(queue, *fedSizeWordsD_, *fedSizeWordsH_);

    phase2it::runChipCountKernel(queue, bytesD_->view().byte().data(), nav(), countsD_->data());
    alpaka::memcpy(queue, *countsH_, *countsD_);
  }

  void Phase2ITRawToBitStream::produce(device::Event& iEvent, device::EventSetup const& iSetup) {
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
    phase2it::runChipFillKernel(queue, bytesD_->view().byte().data(), nav(), offsetsD_->data(), chips.view());

    iEvent.emplace(chipPutToken_, std::move(chips));
    iEvent.emplace(bytesPutToken_, std::move(*bytesD_));
    bytesD_.reset();
    bytesH_.reset();
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(Phase2ITRawToBitStream);
