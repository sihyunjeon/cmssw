#ifndef EventFilter_Phase2PixelRawToDigi_plugins_alpaka_Phase2ITUnpackerKernels_h
#define EventFilter_Phase2PixelRawToDigi_plugins_alpaka_Phase2ITUnpackerKernels_h

#include <cstdint>

#include "DataFormats/Phase2ITBitStreamSoA/interface/Phase2ITChipBitStreamSoA.h"
#include "DataFormats/SiPixelDigiSoA/interface/SiPixelDigisSoA.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::Phase2ITUnpacker {

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

  // Threads per block on GPU; elements per thread on the CPU backends
  inline constexpr uint32_t kDefaultBlockSize = 128;

  // stage 1: count chips per module, then fill the chip index rows
  void runChipCountKernel(
      Queue& queue, const uint8_t* bytes, const ModuleMap& modMap, uint32_t* chipCounts, uint32_t blockSize);
  void runChipFillKernel(Queue& queue,
                         const uint8_t* bytes,
                         const ModuleMap& modMap,
                         const uint32_t* chipOffsets,
                         Phase2ITChipBitStreamSoAView chips,
                         uint32_t blockSize);

  // stage 2: count digis per chip, then decode
  void runDigiCountKernel(Queue& queue,
                          const uint8_t* bytes,
                          Phase2ITChipBitStreamSoAConstView chips,
                          bool dropTot,
                          uint32_t* counts,
                          uint32_t blockSize);
  void runDigiFillKernel(Queue& queue,
                         const uint8_t* bytes,
                         Phase2ITChipBitStreamSoAConstView chips,
                         bool dropTot,
                         bool keepMode,
                         const uint32_t* offsets,
                         SiPixelDigisSoAView digis,
                         uint32_t blockSize);

  // Throws if the device cannot launch that block size; 'who' names the module
  void checkBlockSize(uint32_t blockSize, const char* who);

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::Phase2ITUnpacker

#endif  // EventFilter_Phase2PixelRawToDigi_plugins_alpaka_Phase2ITUnpackerKernels_h
