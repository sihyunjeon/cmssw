#ifndef EventFilter_Phase2PixelRawToDigi_plugins_alpaka_Phase2ITUnpackKernels_h
#define EventFilter_Phase2PixelRawToDigi_plugins_alpaka_Phase2ITUnpackKernels_h

#include <cstdint>

#include "DataFormats/Phase2ITBitStreamSoA/interface/Phase2ITChipBitStreamSoA.h"
#include "DataFormats/SiPixelDigiSoA/interface/SiPixelDigisSoA.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"

// Two-stage device unpacking, mirroring the legacy chain:
//   raw  -> chip bit streams  (Phase2ITRawToBitStreamProducer)
//   chip bit streams -> digis (Phase2ITBitStreamToPixelProducer)
namespace ALPAKA_ACCELERATOR_NAMESPACE::phase2it {

  // Per-module lookup into the concatenated FED bodies, mirroring SLinkModuleMap, one thread per module.
  // FIXME chip stream lengths vary by ~4x, so threads in a warp finish unevenly;
  // binning chips by size before the stage 2 launch would reduce the divergence.
  struct ModuleMap {
    const int32_t* fedWordBase;   // [nFeds] word offset of each FED body
    const int32_t* fedSizeWords;  // [nFeds]
    const int32_t* fedModStart;   // [nFeds+1] first module index of each FED
    const uint16_t* modFedIdx;    // [nModules]
    const uint32_t* modDetId;     // [nModules]
    const uint8_t* modSubtype;    // [nModules]
    const uint16_t* modGeomIdx;   // [nModules]
    int nModules;
  };

  // stage 1: count chips per module, then fill the chip index rows
  void runChipCountKernel(Queue& queue, const uint8_t* bytes, const ModuleMap& modMap, uint32_t* chipCounts);
  void runChipFillKernel(Queue& queue,
                         const uint8_t* bytes,
                         const ModuleMap& modMap,
                         const uint32_t* chipOffsets,
                         Phase2ITChipBitStreamSoAView chips);

  // stage 2: count digis per chip, then decode into the digi SoA
  void runDigiCountKernel(
      Queue& queue, const uint8_t* bytes, Phase2ITChipBitStreamSoAConstView chips, bool dropTot, uint32_t* counts);
  void runDigiFillKernel(Queue& queue,
                         const uint8_t* bytes,
                         Phase2ITChipBitStreamSoAConstView chips,
                         bool dropTot,
                         bool keepMode,
                         const uint32_t* offsets,
                         SiPixelDigisSoAView digis);

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::phase2it

#endif  // EventFilter_Phase2PixelRawToDigi_plugins_alpaka_Phase2ITUnpackKernels_h
