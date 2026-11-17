#ifndef DataFormats_Phase2ITBitStreamSoA_interface_alpaka_Phase2ITModuleMapDevice_h
#define DataFormats_Phase2ITBitStreamSoA_interface_alpaka_Phase2ITModuleMapDevice_h

#include "DataFormats/Phase2ITBitStreamSoA/interface/Phase2ITModuleMapHost.h"
#include "DataFormats/Phase2ITBitStreamSoA/interface/Phase2ITModuleMapSoA.h"
#include "DataFormats/Portable/interface/alpaka/PortableCollection.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  using Phase2ITModuleMapDevice = PortableCollection<Phase2ITModuleMapSoA>;

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

ASSERT_DEVICE_MATCHES_HOST_COLLECTION(Phase2ITModuleMapDevice, Phase2ITModuleMapHost);

#endif  // DataFormats_Phase2ITBitStreamSoA_interface_alpaka_Phase2ITModuleMapDevice_h
