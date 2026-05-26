// Helper function to distribute detIds to s-links and vice versa
#include "EventFilter/Phase2PixelRawToDigi/interface/SLinkModuleMap.h"

#include "CondFormats/SiPhase2TrackerObjects/interface/TrackerDetToDTCELinkCablingMap.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2DAQFormatSpecification.h"

#include <algorithm>

using Phase2DAQFormatSpecification::SLINKS_PER_DTC;

SLinkModuleMap::SLinkModuleMap(const TrackerDetToDTCELinkCablingMap& cablingMap) {
  auto knownDTCIdsWithIndex = cablingMap.getKnownDTCIdsWithIndex();
  for (const auto& pair : knownDTCIdsWithIndex) {
    unsigned int dtcIndex = pair.first;
    unsigned int dtcId = pair.second;

    auto detIds = cablingMap.getAllDetIdsForDTCId(dtcId);
    std::sort(detIds.begin(), detIds.end());

    int total = static_cast<int>(detIds.size());
    int base = total / SLINKS_PER_DTC;
    int remainder = total % SLINKS_PER_DTC;

    int moduleIndex = 0;
    for (int slinkId = 0; slinkId < SLINKS_PER_DTC; ++slinkId) {
      int modulesForThisSlink = base + ((slinkId < remainder) ? 1 : 0);

      int fedId = static_cast<int>(dtcIndex) * SLINKS_PER_DTC + slinkId;
      auto& vec = fedIdToDetIds_[fedId];
      vec.reserve(modulesForThisSlink);
      for (int i = 0; i < modulesForThisSlink; ++i) {
        uint32_t detId = detIds[moduleIndex++];
        vec.push_back(detId);
        detIdToFedId_[detId] = fedId;
      }
    }
  }
}

const std::vector<uint32_t>& SLinkModuleMap::detIdsForFedId(int fedId) const {
  static const std::vector<uint32_t> kEmpty;
  auto it = fedIdToDetIds_.find(fedId);
  return (it == fedIdToDetIds_.end()) ? kEmpty : it->second;
}

int SLinkModuleMap::fedIdForDetId(uint32_t detId) const {
  auto it = detIdToFedId_.find(detId);
  return (it == detIdToFedId_.end()) ? -1 : it->second;
}
