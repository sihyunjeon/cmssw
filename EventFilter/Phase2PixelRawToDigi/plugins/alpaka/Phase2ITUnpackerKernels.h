#ifndef EventFilter_Phase2PixelRawToDigi_plugins_alpaka_Phase2ITUnpackerKernels_h
#define EventFilter_Phase2PixelRawToDigi_plugins_alpaka_Phase2ITUnpackerKernels_h

#include <cstdint>

#include "DataFormats/Phase2ITBitStreamSoA/interface/Phase2ITModuleStreamSoA.h"
#include "DataFormats/SiPixelDigiSoA/interface/SiPixelDigisSoA.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::Phase2ITUnpacker {

  struct ModuleMap {
    const int32_t* fedWordBase;      // [nFeds] word offset of each FED body
    const int32_t* fedTrailerStart;  // [nFeds] word of each FED's IT trailer, inside its body
    const int32_t* fedModStart;      // [nFeds+1] first module index of each FED
    const uint16_t* modFedIdx;       // [nModules]
    const uint32_t* modDetId;        // [nModules]
    const uint8_t* modSubtype;       // [nModules]
    const uint8_t* modNChips;        // [nModules]
    const uint16_t* modGeomIdx;      // [nModules]
    int nModules;
  };

  // Threads per block on GPU; elements per thread on the CPU backends
  inline constexpr uint32_t kDefaultBlockSize = 128;

  // stage 1: one row per module, from its FED's offset block; the table has
  // one row for every module in the map, so it needs no count first
  void runModuleTableKernel(Queue& queue,
                            const uint8_t* bytes,
                            const ModuleMap& modMap,
                            Phase2ITModuleStreamSoAView modules,
                            uint32_t blockSize);

  // stage 2: decode each module once into the mediator, then copy it into the digis
  struct DecodedHit {
    uint16_t row;
    uint16_t col;
    uint16_t adc;
  };

  // A hit takes at least 6 stream bits (2 without ToT): a QCore with k hits spends
  // >= 2 header bits, >= 2k - 2 hitmap bits and 4k ToT bits. A module's mediator slots
  // start at its first bit divided by this, so modules never share a slot.
  ALPAKA_FN_HOST_ACC inline constexpr uint32_t minBitsPerHit(bool dropTot) { return dropTot ? 2u : 6u; }
  inline uint32_t mediatorSize(uint32_t nBytes, bool dropTot) {
    return uint32_t(uint64_t(nBytes) * 8 / minBitsPerHit(dropTot)) + 1;
  }

  // Malformed input found while decoding, summed over the event in stats[]; the first four are
  // ::Phase2ITUnpacker::MalformedCounts, which the CPU unpacker counts the same way
  enum DecodeStat : uint32_t {
    kBadOffsetModules,
    kExtraChipModules,
    kOverrunChips,
    kTruncatedChips,
    kDroppedHits,
    kNumDecodeStats
  };

  // One thread per module walks its chip headers and decodes every chip into the
  // module's mediator slots; counts[m] is the module's hit count.
  void runDigiDecodeKernel(Queue& queue,
                           const uint8_t* bytes,
                           Phase2ITModuleStreamSoAConstView modules,
                           bool dropTot,
                           bool keepMode,
                           DecodedHit* mediator,
                           uint32_t* counts,
                           uint32_t* stats,
                           uint32_t blockSize);
  // One block per module copies its counts[m] hits from the mediator into the
  // digis, from row offsets[m] on.
  void runDigiCopyKernel(Queue& queue,
                         Phase2ITModuleStreamSoAConstView modules,
                         bool dropTot,
                         const DecodedHit* mediator,
                         const uint32_t* counts,
                         const uint32_t* offsets,
                         SiPixelDigisSoAView digis,
                         uint32_t blockSize);

  // Throws if the device cannot launch that block size; 'who' names the module
  void checkBlockSize(uint32_t blockSize, const char* who);

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::Phase2ITUnpacker

#endif  // EventFilter_Phase2PixelRawToDigi_plugins_alpaka_Phase2ITUnpackerKernels_h
