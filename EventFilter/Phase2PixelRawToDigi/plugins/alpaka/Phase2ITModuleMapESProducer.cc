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
#include "Geometry/CommonTopologies/interface/SimplePixelTopology.h"
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

  namespace {
    // Module_SubType values the chip-quadrant table in ChipModuleMap covers.
    constexpr int kMinSubtype = 1;
    constexpr int kMaxSubtype = 12;
    // The clusterizer allocates SiPixelClustersSoA with this many entries and indexes
    // it by moduleId (SiPixelPhase2DigiToCluster.cc), so this is the real bound -- not
    // pixelClustering::maxNumModules, which is an upper limit across topologies.
    constexpr int kMaxModuleId = pixelTopology::Phase2::numberOfModules;
  }  // namespace


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

      // FED order follows SLinkModuleMap
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
          // The kernels use these two as array indices with no device-side bound
          // check (an ALPAKA_ASSERT_ACC compiles out in a release build), so validate
          // here, where it is cheap and the exception is catchable.
          const int subtype = static_cast<int>(cabling->getModuleInfo(detId).subtype);
          if (subtype < kMinSubtype || subtype > kMaxSubtype)
            throw cms::Exception("Phase2ITModuleMapESProducer")
                << "Module_SubType " << subtype << " for detId " << detId << " is outside the supported range "
                << kMinSubtype << ".." << kMaxSubtype << "; it indexes the chip-quadrant table in the unpacking kernels.";
          // Checked before the narrowing cast: afterwards a truncated value is
          // indistinguishable from a valid one.
          const int geomIdx = det->index();
          if (geomIdx < 0 || geomIdx >= static_cast<int>(kMaxModuleId))
            throw cms::Exception("Phase2ITModuleMapESProducer")
                << "GeomDetUnit index " << geomIdx << " for detId " << detId << " is outside [0, " << kMaxModuleId
                << "); it is written to SiPixelDigisSoA::moduleId, which the clusterizer uses to index "
                << "SiPixelClustersSoA, allocated with that many entries.";
          modFedIdx.push_back(fedIdx);
          modDetId.push_back(detId);
          modSubtype.push_back(static_cast<uint8_t>(subtype));
          modGeomIdx.push_back(static_cast<uint16_t>(geomIdx));
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
    edm::ESGetToken<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd> cablingToken_;
    edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> geomToken_;
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/ModuleFactory.h"
DEFINE_FWK_EVENTSETUP_ALPAKA_MODULE(Phase2ITModuleMapESProducer);
