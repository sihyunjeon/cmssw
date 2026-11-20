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
    int nFeds;
    int nModules;
  };

  // Threads per block on GPU; elements per thread on the CPU backends
  inline constexpr uint32_t kDefaultBlockSize = 128;

  // Chip-table rows per module: the most chips a subtype has
  inline constexpr uint32_t kChipSlots = 4;

  // Stage 1: a row per module and kChipSlots chip rows; one block per FED, or one thread per module
  void runModuleChipTableKernel(Queue& queue,
                                const uint8_t* bytes,
                                const ModuleMap& modMap,
                                Phase2ITModuleStreamSoAView modules,
                                Phase2ITChipStreamSoAView chips,
                                bool perFed,
                                uint32_t blockSize);

  // Stage 2: a decoded hit, kept in the mediator until its digi position is known
  struct DecodedHit {
    uint16_t row;
    uint16_t col;
    uint16_t adc;
  };

  // Two-pass decode: one complete quarter core, in its chip's slots
  struct alignas(16) QCoreRecord {
    uint32_t hitmapCol;  // hitmap in bits 0-15, the column of its first pixel in the module from bit 16
    uint32_t row;        // the row of its first pixel in the module
    uint32_t totBit;     // its first ToT in the chip's stream
    uint32_t hitBase;    // the chip's hits before it
  };

  // A hit takes at least 6 stream bits (2 without ToT): a quarter core with k hits has >= 2 header, >= 2k - 2 hitmap
  // and 4k ToT bits. A chip's slots start at its first bit divided by this, so chips never share a slot.
  ALPAKA_FN_HOST_ACC inline constexpr uint32_t minBitsPerHit(bool dropTot) { return dropTot ? 2u : 6u; }
  inline uint32_t mediatorSize(uint32_t nBytes, bool dropTot) {
    return uint32_t(uint64_t(nBytes) * 8 / minBitsPerHit(dropTot)) + 1;
  }

  // Malformed input summed over the event in stats[]; the first four as in ::Phase2ITUnpacker::MalformedCounts
  enum DecodeStat : uint32_t {
    kBadOffsetModules,
    kExtraChipModules,
    kOverrunChips,
    kTruncatedChips,
    kDroppedHits,
    kNumDecodeStats
  };

  // Decode each chip row into its mediator slots, a thread per chip or per module; counts[c] is chip c's hits
  void runDigiDecodeKernel(Queue& queue,
                           const uint8_t* bytes,
                           Phase2ITModuleStreamSoAConstView modules,
                           Phase2ITChipStreamSoAConstView chips,
                           bool perChip,
                           bool dropTot,
                           bool keepMode,
                           DecodedHit* mediator,
                           uint32_t* counts,
                           uint32_t* stats,
                           uint32_t blockSize);
  // One block per module copies its chips' hits from the mediator into the digis, chip c from row offsets[c] on
  void runDigiCopyKernel(Queue& queue,
                         Phase2ITModuleStreamSoAConstView modules,
                         Phase2ITChipStreamSoAConstView chips,
                         bool dropTot,
                         const DecodedHit* mediator,
                         const uint32_t* counts,
                         const uint32_t* offsets,
                         SiPixelDigisSoAView digis,
                         uint32_t blockSize);

  // Two-pass decode, pass 1: a record per quarter core; qcounts[c] and counts[c] are chip c's records and hits
  void runQCoreScanKernel(Queue& queue,
                          const uint8_t* bytes,
                          Phase2ITModuleStreamSoAConstView modules,
                          Phase2ITChipStreamSoAConstView chips,
                          bool perChip,
                          bool dropTot,
                          bool keepMode,
                          QCoreRecord* qcores,
                          uint32_t* qcounts,
                          uint32_t* counts,
                          uint32_t* stats,
                          uint32_t blockSize);
  // Pass 2, in place of the copy: a block per module and a thread per quarter core, chip c from row offsets[c] on
  void runQCoreDigiKernel(Queue& queue,
                          const uint8_t* bytes,
                          Phase2ITModuleStreamSoAConstView modules,
                          Phase2ITChipStreamSoAConstView chips,
                          bool dropTot,
                          const QCoreRecord* qcores,
                          const uint32_t* qcounts,
                          const uint32_t* counts,
                          const uint32_t* offsets,
                          SiPixelDigisSoAView digis,
                          uint32_t blockSize);

  // Throws if the device cannot launch that block size; 'who' names the module
  void checkBlockSize(uint32_t blockSize, const char* who);

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::Phase2ITUnpacker

#endif  // EventFilter_Phase2PixelRawToDigi_plugins_alpaka_Phase2ITUnpackerKernels_h
