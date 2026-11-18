// -*- C++ -*-
// Package:    EventFilter/Phase2PixelRawToDigi
// Class:      Phase2ITUnpackerKernels
// Description: The device side of the unpacker: the decode primitives and the
//              kernels of the two unpacking stages. Stage 1, one block per FED,
//              writes one row per module and per chip, locating their words in
//              the concatenated FED bodies. Stage 2 decodes each chip in its own
//              thread into a mediator and copies it into the digis once their
//              positions are known, since a kernel cannot grow its own output
//              collection.
// Maintainer: Si Hyun Jeon, shjeon@cern.ch

#include <alpaka/alpaka.hpp>

#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITChip.h"
#include "DataFormats/SiPixelClusterSoA/interface/ClusteringConstants.h"
#include "DataFormats/SiPixelDetId/interface/PixelChannelIdentifier.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2DAQFormatSpecification.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "HeterogeneousCore/AlpakaInterface/interface/devices.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "Phase2ITUnpackerKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::Phase2ITUnpacker {

  // ---- decode primitives ----

  using namespace Phase2DAQFormatSpecification;

  ALPAKA_FN_ACC inline uint32_t readWord(const uint8_t* bytes, int wordIdx) {
    const uint8_t* p = bytes + wordIdx * 4;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
  }

  // MSB-first reader over a byte buffer, mirroring Phase2ITBitReader.
  // next() is the unchecked primitive: it does NOT test pos against len, so callers
  // use the checked nextOr0()/bits() wrappers below. A read past the end gives 0 and
  // marks the stream as cut short (past).
  struct BitReader {
    const uint8_t* bytes;
    uint32_t len;
    uint32_t pos = 0;
    bool past = false;

    ALPAKA_FN_ACC bool next() {
      const bool b = (bytes[pos >> 3] >> (7 - (pos & 7))) & 1;
      ++pos;
      return b;
    }
    ALPAKA_FN_ACC bool nextOr0() {
      if (pos < len)
        return next();
      past = true;
      return false;
    }
    ALPAKA_FN_ACC uint32_t bits(int n) {
      uint32_t r = 0;
      for (int i = 0; i < n; ++i)
        r = (r << 1) | (nextOr0() ? 1u : 0u);
      return r;
    }
  };

  // Huffman pair: returns (first << 1 | second), mirroring decPairBits
  ALPAKA_FN_ACC inline int decPair(BitReader& br) {
    if (!br.nextOr0())  // "0 -> 01" substitute
      return 0b01;
    return br.nextOr0() ? 0b11 : 0b10;
  }

  // decChunk(n=8): fixed 8 -> 4 -> 2 tree traversal
  ALPAKA_FN_ACC inline void decChunk8(BitReader& br, bool out[8]) {
    for (int i = 0; i < 8; ++i)
      out[i] = false;
    int act1[2], n1 = 0;
    int p = decPair(br);
    if (p & 2)
      act1[n1++] = 0;
    if (p & 1)
      act1[n1++] = 4;
    int act2[4], n2 = 0;
    for (int i = 0; i < n1; ++i) {
      p = decPair(br);
      if (p & 2)
        act2[n2++] = act1[i];
      if (p & 1)
        act2[n2++] = act1[i] + 2;
    }
    for (int i = 0; i < n2; ++i) {
      p = decPair(br);
      out[act2[i]] = (p & 2);
      out[act2[i] + 1] = (p & 1);
    }
  }

  ALPAKA_FN_ACC inline void decodeHitmap(BitReader& br, bool hm[16]) {
    for (int i = 0; i < 16; ++i)
      hm[i] = false;
    const int p = decPair(br);
    if (p & 2) {
      bool row[8];
      decChunk8(br, row);
      for (int i = 0; i < 8; ++i)
        hm[i] = row[i];
    }
    if (p & 1) {
      bool row[8];
      decChunk8(br, row);
      for (int i = 0; i < 8; ++i)
        hm[8 + i] = row[i];
    }
  }

  // hitmap index + qcore address -> global row/col (getGlobalPixelCoordinate)
  ALPAKA_FN_ACC inline void hitToRowCol(
      int subtype, int chipId, int ccol, int qrow, int i, bool keepMode, int& row, int& col) {
    // ChipModuleMap::CHIP_QUADRANT flattened; index 0 unused (subtype is 1..12)
    constexpr int8_t kQuadX[13][4] = {{0},
                                      {0, 0, 0, 0},
                                      {0, 0, 0, 0},
                                      {+1, +1, -1, -1},
                                      {+1, +1, -1, -1},
                                      {0, 0, 0, 0},
                                      {+1, +1, -1, -1},
                                      {+1, +1, -1, -1},
                                      {+1, +1, -1, -1},
                                      {+1, +1, -1, -1},
                                      {+1, +1, -1, -1},
                                      {+1, +1, -1, -1},
                                      {+1, +1, -1, -1}};
    constexpr int8_t kQuadY[13][4] = {{0},
                                      {0, 0, 0, 0},
                                      {+1, -1, 0, 0},
                                      {+1, -1, -1, +1},
                                      {+1, -1, -1, +1},
                                      {-1, +1, 0, 0},
                                      {-1, +1, -1, +1},
                                      {-1, +1, -1, +1},
                                      {+1, -1, -1, +1},
                                      {+1, -1, -1, +1},
                                      {+1, -1, -1, +1},
                                      {+1, -1, -1, +1},
                                      {+1, -1, -1, +1}};
    const int rocRow = i / 8;
    const int rocCol = i % 8;
    const int shifted = (rocRow * 2 + (rocCol % 2)) * HITMAP_COL + rocCol / 2;
    const int adjusted = (shifted + 8) % 16;
    const int localRow = adjusted / 4;
    const int localCol = adjusted % 4;
    const int rowsPerChip = keepMode ? Phase2ITChip::kRowsPerChipKeep : Phase2ITChip::kRowsPerChip;
    const int largePixelNRows = keepMode ? Phase2ITChip::kLargePixelNRowsKeep : Phase2ITChip::kLargePixelNRows;
    const int colsPerChip = keepMode ? Phase2ITChip::kColsPerChipKeep : Phase2ITChip::kColsPerChip;
    const int largePixelNCols = keepMode ? Phase2ITChip::kLargePixelNColsKeep : Phase2ITChip::kLargePixelNCols;
    const int rowOffset = (kQuadX[subtype][chipId] > 0) ? (rowsPerChip + largePixelNRows) : 0;
    const int colOffset = (kQuadY[subtype][chipId] > 0) ? (colsPerChip + largePixelNCols) : 0;
    row = rowOffset + qrow * HITMAP_COL + localRow;
    col = colOffset + ccol * HITMAP_ROW + localCol;
  }

  // Word span [start, end) of one module inside a FED body (processFED navigation)
  struct ModuleSpan {
    int start;       // first word of the module
    int end;         // one past its last word: where the next module starts, or the IT trailer
    bool badOffset;  // offset outside the data block, so the module was dropped
  };

  // Same rules as the CPU forEachModule: a module starts inside the data block and ends where the next one
  // starts, or at the IT trailer; offsets are checked before they are added, so a corrupt one cannot overflow.
  ALPAKA_FN_ACC inline ModuleSpan moduleSpan(const uint8_t* fedBytes, int trailerStart, int numModules, int idxInFed) {
    const int offsetStart = HEADER_TRAILER_LINES;
    const int offsetBits = numModules * BITS_PER_WORD;
    const int padWords = ((BITS_PER_CHUNK - (offsetBits % BITS_PER_CHUNK)) % BITS_PER_CHUNK) / BITS_PER_WORD;
    const int dataBlockStart = offsetStart + numModules + padWords;
    // A malformed offset drops the module. The flag is carried out so the producer
    // can report it: device code has nothing to log to, and a silently dropped
    // module is indistinguishable from an empty one.
    ModuleSpan s{0, 0, true};
    // the offset block itself has to end before the trailer
    if (dataBlockStart > trailerStart)
      return s;
    const uint32_t dataWords = uint32_t(trailerStart - dataBlockStart);
    const uint32_t startOffset = readWord(fedBytes, offsetStart + idxInFed);
    if (startOffset >= dataWords)
      return s;
    uint32_t endOffset = dataWords;
    if (idxInFed + 1 < numModules) {
      const uint32_t next = readWord(fedBytes, offsetStart + idxInFed + 1);
      endOffset = next < dataWords ? next : dataWords;
    }
    if (endOffset < startOffset)
      endOffset = startOffset;
    s.start = dataBlockStart + int(startOffset);
    s.end = dataBlockStart + int(endOffset);
    s.badOffset = false;
    return s;
  }

  // Walk the chips of one module by the same rules as the CPU forEachChip: calls chip(chipId,
  // firstPayloadWord, bitLen, overrun). A chip's ID is its place in the module, so the walk stops after
  // maxChips chips; returns true if a further chip header follows them.
  template <typename TChip>
  ALPAKA_FN_ACC bool forEachChip(const uint8_t* bytes, ModuleSpan span, int maxChips, TChip&& chip) {
    int cursor = span.start;
    for (int chipId = 0; cursor < span.end; ++chipId) {
      const uint32_t hdr = readWord(bytes, cursor);
      if (((hdr >> 28) & 0xF) != CHIP_HEADER_MAGIC)
        break;  // end padding: module exhausted
      if (chipId >= maxChips)
        return true;
      const uint32_t endBit = (hdr >> 16) & 0x1F;
      const uint32_t sizeWords = hdr & 0xFFFF;
      // a payload past the module's end, or an end bit with no words, is malformed: the chip is read as empty
      const bool overrun = (sizeWords == 0 && endBit != 0) || cursor + 1 + int(sizeWords) > span.end;
      const uint32_t bitLen = overrun         ? 0u
                              : (endBit == 0) ? sizeWords * BITS_PER_WORD
                                              : (sizeWords - 1) * BITS_PER_WORD + endBit;
      chip(chipId, cursor + 1, bitLen, overrun);
      cursor += 1 + int(sizeWords);
    }
    return false;
  }

  // Decode one chip stream: calls hit(ccol, qrow, hitmapIndex, adc) per fired pixel.
  // Returns true if the stream ends inside a quarter core (malformed): that quarter core's hits are ignored.
  template <typename THit>
  ALPAKA_FN_ACC bool decodeChip(BitReader br, bool dropTot, THit&& hit) {
    bool previousIsLast = true;
    uint32_t ccol = 0;
    int prevRow = -1;
    while (br.pos < br.len) {
      if (previousIsLast)
        ccol = br.bits(6);
      const bool islast = br.nextOr0();
      const bool isneighbor = br.nextOr0();
      const uint32_t qrow = isneighbor ? uint32_t(prevRow + 1) : br.bits(8);

      bool hm[16];
      decodeHitmap(br, hm);
      // the ToTs follow the hitmap: the quarter core is complete only if nothing so far ran past the end
      // and all its ToTs are still in the stream, which is checked before any of its hits is handed on
      uint32_t nHits = 0;
      for (int i = 0; i < HITMAP_SIZE; ++i)
        nHits += hm[i];
      if (br.past || (!dropTot && br.pos + 4 * nHits > br.len))
        return true;
      for (int i = 0; i < HITMAP_SIZE; ++i) {
        if (!hm[i])
          continue;
        const int adc = dropTot ? 0 : int(br.bits(4));
        hit(int(ccol), int(qrow), i, adc);
      }
      previousIsLast = islast;
      prevRow = int(qrow);
    }
    return false;
  }

  // ---- kernels ----

  // Stage 1: raw FED bodies -> one row per module and kChipSlots per module for its chips,
  // locating their words in those bytes.

  namespace {
    // Rows of module m, module i of the nInFed modules of FED f
    ALPAKA_FN_ACC inline void fillModuleRows(const uint8_t* bytes,
                                             const ModuleMap& modMap,
                                             int f,
                                             int m,
                                             int i,
                                             int nInFed,
                                             Phase2ITModuleStreamSoAView modules,
                                             Phase2ITChipStreamSoAView chips) {
      // word positions are relative to the whole buffer, so stage 2 needs no FED lookup
      const int fedBase = modMap.fedWordBase[f];
      const ModuleSpan span = moduleSpan(bytes + fedBase * 4, modMap.fedTrailerStart[f], nInFed, i);
      auto r = modules[m];
      r.detId() = modMap.modDetId[m];
      r.wordBegin() = fedBase + span.start;
      r.wordEnd() = fedBase + span.end;
      r.moduleId() = modMap.modGeomIdx[m];
      r.subtype() = modMap.modSubtype[m];
      r.nChips() = modMap.modNChips[m];
      r.badOffset() = span.badOffset ? 1 : 0;
      for (uint32_t k = 0; k < kChipSlots; ++k) {
        auto c = chips[m * kChipSlots + k];
        c.payloadWord() = 0;
        c.bitLen() = 0;
        c.overrun() = 0;
      }
      const ModuleSpan words{fedBase + span.start, fedBase + span.end, span.badOffset};
      const bool extra = forEachChip(
          bytes, words, modMap.modNChips[m], [&](int chipId, int payloadWord, uint32_t bitLen, bool overrun) {
            auto c = chips[m * kChipSlots + chipId];
            c.payloadWord() = payloadWord;
            c.bitLen() = bitLen;
            c.overrun() = overrun ? 1 : 0;
          });
      r.extraChips() = extra ? 1 : 0;
    }
  }  // namespace

  struct ModuleChipTableKernel {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  const uint8_t* bytes,
                                  ModuleMap modMap,
                                  Phase2ITModuleStreamSoAView modules,
                                  Phase2ITChipStreamSoAView chips,
                                  bool perFed) const {
      if (perFed) {
        // one block per FED, whose threads take its modules
        for (auto f : cms::alpakatools::independent_groups(acc, modMap.nFeds)) {
          const int firstModule = modMap.fedModStart[f];
          const int nInFed = modMap.fedModStart[f + 1] - firstModule;
          for (auto i : cms::alpakatools::independent_group_elements(acc, nInFed))
            fillModuleRows(bytes, modMap, f, firstModule + int(i), int(i), nInFed, modules, chips);
        }
      } else {
        // one thread per module, whichever block it falls in
        for (auto m : cms::alpakatools::uniform_elements(acc, modMap.nModules)) {
          const int f = modMap.modFedIdx[m];
          const int firstModule = modMap.fedModStart[f];
          const int nInFed = modMap.fedModStart[f + 1] - firstModule;
          fillModuleRows(bytes, modMap, f, int(m), int(m) - firstModule, nInFed, modules, chips);
        }
      }
    }
  };

  void runModuleChipTableKernel(Queue& queue,
                                const uint8_t* bytes,
                                const ModuleMap& modMap,
                                Phase2ITModuleStreamSoAView modules,
                                Phase2ITChipStreamSoAView chips,
                                bool perFed,
                                uint32_t blockSize) {
    // one block per FED, or enough blocks for one thread per module
    const uint32_t blocks =
        perFed ? uint32_t(modMap.nFeds) : uint32_t(cms::alpakatools::divide_up_by(modMap.nModules, blockSize));
    const auto wd = cms::alpakatools::make_workdiv<Acc1D>(blocks, blockSize);
    alpaka::exec<Acc1D>(queue, wd, ModuleChipTableKernel{}, bytes, modMap, modules, chips, perFed);
  }

  // Stage 2: per-chip bit streams -> digis, decoding each chip in its own thread, or a module's chips in turn.

  namespace {
    // a chip's first mediator slot: its first bit divided by the fewest bits a hit takes
    ALPAKA_FN_ACC inline uint32_t firstSlot(uint32_t word, bool dropTot) {
      return uint32_t(uint64_t(word) * BITS_PER_WORD / minBitsPerHit(dropTot));
    }

    // Decode chip row c into its mediator slots, with counts[c] its hit count
    ALPAKA_FN_ACC inline void decodeChipRow(Acc1D const& acc,
                                            uint32_t c,
                                            const uint8_t* bytes,
                                            Phase2ITModuleStreamSoAConstView modules,
                                            Phase2ITChipStreamSoAConstView chips,
                                            bool dropTot,
                                            bool keepMode,
                                            DecodedHit* mediator,
                                            uint32_t* counts,
                                            uint32_t* stats) {
      const auto mod = modules[c / kChipSlots];
      const int chipId = c % kChipSlots;
      const auto chip = chips[c];
      // what stage 1 found about the module is counted once, by its first chip row
      if (chipId == 0 && mod.badOffset() != 0)
        alpaka::atomicAdd(acc, &stats[kBadOffsetModules], 1u, alpaka::hierarchy::Blocks{});
      if (chipId == 0 && mod.extraChips() != 0)
        alpaka::atomicAdd(acc, &stats[kExtraChipModules], 1u, alpaka::hierarchy::Blocks{});
      if (chip.overrun() != 0)
        alpaka::atomicAdd(acc, &stats[kOverrunChips], 1u, alpaka::hierarchy::Blocks{});
      // the chip's slots: its bits divided by the fewest bits a hit takes
      const uint32_t begin = firstSlot(chip.payloadWord(), dropTot);
      const uint32_t end = begin + chip.bitLen() / minBitsPerHit(dropTot);
      const int subtype = mod.subtype();
      uint32_t n = 0;
      uint32_t lost = 0;
      const bool cut = decodeChip(
          BitReader{bytes + chip.payloadWord() * 4, chip.bitLen()}, dropTot, [&](int ccol, int qrow, int i, int adc) {
            // complete quarter cores always fit their chip's slots; this is a safety net
            if (begin + n >= end) {
              ++lost;
              return;
            }
            int row, col;
            hitToRowCol(subtype, chipId, ccol, qrow, i, keepMode, row, col);
            mediator[begin + n++] = DecodedHit{uint16_t(row), uint16_t(col), uint16_t(adc)};
          });
      counts[c] = n;
      if (cut)
        alpaka::atomicAdd(acc, &stats[kTruncatedChips], 1u, alpaka::hierarchy::Blocks{});
      if (lost != 0)
        alpaka::atomicAdd(acc, &stats[kDroppedHits], lost, alpaka::hierarchy::Blocks{});
    }
  }  // namespace

  struct DigiDecodeKernel {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  const uint8_t* bytes,
                                  Phase2ITModuleStreamSoAConstView modules,
                                  Phase2ITChipStreamSoAConstView chips,
                                  bool perChip,
                                  bool dropTot,
                                  bool keepMode,
                                  DecodedHit* mediator,
                                  uint32_t* counts,
                                  uint32_t* stats) const {
      if (perChip) {
        for (auto c : cms::alpakatools::uniform_elements(acc, chips.metadata().size()))
          decodeChipRow(acc, c, bytes, modules, chips, dropTot, keepMode, mediator, counts, stats);
      } else {
        for (auto m : cms::alpakatools::uniform_elements(acc, modules.metadata().size()))
          for (uint32_t c = m * kChipSlots; c < (m + 1) * kChipSlots; ++c)
            decodeChipRow(acc, c, bytes, modules, chips, dropTot, keepMode, mediator, counts, stats);
      }
    }
  };

  struct DigiCopyKernel {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  Phase2ITModuleStreamSoAConstView modules,
                                  Phase2ITChipStreamSoAConstView chips,
                                  bool dropTot,
                                  const DecodedHit* mediator,
                                  const uint32_t* counts,
                                  const uint32_t* offsets,
                                  SiPixelDigisSoAView digis) const {
      constexpr int kRowShift = PixelChannelIdentifier::thePacking.row_shift;
      constexpr int kColShift = PixelChannelIdentifier::thePacking.column_shift;
      constexpr int kAdcShift = PixelChannelIdentifier::thePacking.adc_shift;
      // The collection carries one spare row past the digis; zero it once.
      if (cms::alpakatools::once_per_grid(acc)) {
        const int last = digis.metadata().size() - 1;
        digis[last].clus() = ::pixelClustering::invalidClusterId;
        digis[last].pdigi() = 0;
        digis[last].rawIdArr() = 0;
        digis[last].adc() = 0;
        digis[last].xx() = 0;
        digis[last].yy() = 0;
        digis[last].moduleId() = 0;
        digis[last].rawADC() = 0;
      }
      // one block per module, whose threads copy each of its chips' hits side by side;
      // offsets[] gives each chip a private output range, so no atomics are needed
      for (auto m : cms::alpakatools::independent_groups(acc, modules.metadata().size())) {
        const auto mod = modules[m];
        for (uint32_t c = m * kChipSlots; c < (m + 1) * kChipSlots; ++c) {
          const uint32_t first = offsets[c];
          const DecodedHit* hits = mediator + firstSlot(chips[c].payloadWord(), dropTot);
          for (auto j : cms::alpakatools::independent_group_elements(acc, counts[c])) {
            const DecodedHit h = hits[j];
            auto d = digis[first + j];
            // not yet clustered; 0 is a valid cluster id, so use the sentinel
            d.clus() = ::pixelClustering::invalidClusterId;
            d.pdigi() =
                (uint32_t(h.row) << kRowShift) | (uint32_t(h.col) << kColShift) | (uint32_t(h.adc) << kAdcShift);
            d.rawIdArr() = mod.detId();
            d.adc() = h.adc;
            d.xx() = h.row;
            d.yy() = h.col;
            d.moduleId() = mod.moduleId();
            // the adc before the clusterizer calibrates it, as the Run-3 unpacker fills it
            d.rawADC() = uint8_t(h.adc);
          }
        }
      }
    }
  };

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
                           uint32_t blockSize) {
    const int n = perChip ? chips.metadata().size() : modules.metadata().size();
    const auto wd = cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(n, blockSize), blockSize);
    alpaka::exec<Acc1D>(
        queue, wd, DigiDecodeKernel{}, bytes, modules, chips, perChip, dropTot, keepMode, mediator, counts, stats);
  }

  void runDigiCopyKernel(Queue& queue,
                         Phase2ITModuleStreamSoAConstView modules,
                         Phase2ITChipStreamSoAConstView chips,
                         bool dropTot,
                         const DecodedHit* mediator,
                         const uint32_t* counts,
                         const uint32_t* offsets,
                         SiPixelDigisSoAView digis,
                         uint32_t blockSize) {
    const int n = modules.metadata().size();
    // one block per module
    const auto wd = cms::alpakatools::make_workdiv<Acc1D>(n, blockSize);
    alpaka::exec<Acc1D>(queue, wd, DigiCopyKernel{}, modules, chips, dropTot, mediator, counts, offsets, digis);
  }

  void checkBlockSize(uint32_t blockSize, const char* who) {
    if (blockSize == 0)
      throw cms::Exception(who) << "blockSize must be greater than zero";
    // only the GPU backends cap the threads per block; check every device the module can run on
    if constexpr (not cms::alpakatools::requires_single_thread_per_block_v<Acc1D>) {
      bool warned = false;
      for (auto const& dev : cms::alpakatools::devices<Platform>()) {
        const auto maxThreads = alpaka::getAccDevProps<Acc1D>(dev).m_blockThreadCountMax;
        if (blockSize > maxThreads)
          throw cms::Exception(who) << "blockSize " << blockSize << " exceeds the device limit of " << maxThreads
                                    << " threads per block";
        const auto warpSize = alpaka::getPreferredWarpSize(dev);
        if (!warned && blockSize % warpSize != 0) {
          edm::LogWarning(who) << "blockSize " << blockSize << " is not a multiple of the warp size (" << warpSize
                               << "), which wastes threads in every block";
          warned = true;
        }
      }
    }
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::Phase2ITUnpacker
