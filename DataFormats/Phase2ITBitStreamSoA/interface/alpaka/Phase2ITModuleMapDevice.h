#ifndef DataFormats_Phase2ITBitStreamSoA_interface_alpaka_Phase2ITModuleMapDevice_h
#define DataFormats_Phase2ITBitStreamSoA_interface_alpaka_Phase2ITModuleMapDevice_h

#include "DataFormats/Phase2ITBitStreamSoA/interface/Phase2ITModuleMapSoA.h"
#include "DataFormats/Portable/interface/alpaka/PortableCollection.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  using Phase2ITModuleMapDevice = PortableCollection2<Phase2ITModuleMapSoA, Phase2ITFedMapSoA>;

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

#endif  // DataFormats_Phase2ITBitStreamSoA_interface_alpaka_Phase2ITModuleMapDevice_h
