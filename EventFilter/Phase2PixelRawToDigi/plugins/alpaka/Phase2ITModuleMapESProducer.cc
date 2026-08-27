// -*- C++ -*-
// Package:    EventFilter/Phase2PixelRawToDigi
// Class:      Phase2ITModuleMapESProducer
// Description: Flatten the cabling map and tracker geometry into the tables the
//              unpacking kernels index, once per IOV
// Maintainer: Si Hyun Jeon, shjeon@cern.ch

#include <optional>
#include <vector>

#include "CondFormats/SiPhase2TrackerObjects/interface/TrackerDetToDTCELinkCablingMap.h"
#include "DataFormats/Phase2ITBitStreamSoA/interface/Phase2ITModuleMapHost.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2ITModuleMapRecord.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/SLinkModuleMap.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/ESGetToken.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "Geometry/CommonDetUnit/interface/GeomDet.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/ESProducer.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  class Phase2ITModuleMapESProducer : public ESProducer {
  public:
    Phase2ITModuleMapESProducer(edm::ParameterSet const& iConfig) : ESProducer(iConfig) {
      auto cc = setWhatProduced(this);
      cablingToken_ = cc.consumes();
      geomToken_ = cc.consumes();
    }

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      descriptions.addWithDefaultLabel(desc);
    }

    std::optional<Phase2ITModuleMapHost> produce(const Phase2ITModuleMapRecord& iRecord) {
      auto cabling = iRecord.getTransientHandle(cablingToken_);
      auto geom = iRecord.getTransientHandle(geomToken_);

      // Walk the FEDs in the order SLinkModuleMap gives them, so a module's row
      // index and its FED's range stay consistent.
      SLinkModuleMap slinkMap(*cabling);
      std::vector<int32_t> fedIds, modStart;
      std::vector<uint16_t> modFedIdx, modGeomIdx;
      std::vector<uint32_t> modDetId;
      std::vector<uint8_t> modSubtype;

      for (const auto& [fedId, detIds] : slinkMap.fedIdToDetIds()) {
        modStart.push_back(static_cast<int32_t>(modDetId.size()));
        const uint16_t fedIdx = static_cast<uint16_t>(fedIds.size());
        fedIds.push_back(fedId);
        for (uint32_t detId : detIds) {
          if (!cabling->hasModuleInfo(detId))
            throw cms::Exception("Phase2ITModuleMapESProducer") << "No ModuleInfo in cabling map for detId " << detId;
          const auto* det = geom->idToDetUnit(DetId(detId));
          if (det == nullptr)
            throw cms::Exception("Phase2ITModuleMapESProducer") << "No GeomDetUnit for detId " << detId;
          modFedIdx.push_back(fedIdx);
          modDetId.push_back(detId);
          modSubtype.push_back(static_cast<uint8_t>(cabling->getModuleInfo(detId).subtype));
          modGeomIdx.push_back(static_cast<uint16_t>(det->index()));
        }
      }
      modStart.push_back(static_cast<int32_t>(modDetId.size()));

      const int32_t nModules = static_cast<int32_t>(modDetId.size());
      const int32_t nFeds = static_cast<int32_t>(fedIds.size());

      Phase2ITModuleMapHost product({{nModules, nFeds + 1}}, cms::alpakatools::host());
      auto mods = product.view<Phase2ITModuleMapSoA>();
      for (int32_t m = 0; m < nModules; ++m) {
        mods[m].fedIdx() = modFedIdx[m];
        mods[m].detId() = modDetId[m];
        mods[m].subtype() = modSubtype[m];
        mods[m].geomIdx() = modGeomIdx[m];
      }
      auto feds = product.view<Phase2ITFedMapSoA>();
      for (int32_t f = 0; f <= nFeds; ++f) {
        feds[f].modStart() = modStart[f];
        feds[f].fedId() = (f < nFeds) ? fedIds[f] : -1;
      }
      return product;
    }

  private:
    // Declared against the parent records; Phase2ITModuleMapRecord forwards the
    // lookup, as SiPixelCablingSoAESProducer does.
    edm::ESGetToken<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd> cablingToken_;
    edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> geomToken_;
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/ModuleFactory.h"
DEFINE_FWK_EVENTSETUP_ALPAKA_MODULE(Phase2ITModuleMapESProducer);
