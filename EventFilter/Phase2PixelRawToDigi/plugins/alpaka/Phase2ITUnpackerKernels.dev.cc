// -*- C++ -*-
// Package:    EventFilter/Phase2PixelRawToDigi
// Class:      Phase2ITUnpackerKernels
// Description: The device side of the unpacker: the decode primitives and the
//              count/fill kernel pairs of the two unpacking stages.
//              Each stage decodes twice: once to size the output, once to fill
//              it, since a kernel cannot grow its own output collection.
// Maintainer: Si Hyun Jeon, shjeon@cern.ch

#include <alpaka/alpaka.hpp>

#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITChip.h"
#include "DataFormats/SiPixelDetId/interface/PixelChannelIdentifier.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2DAQFormatSpecification.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "Phase2ITUnpackerKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::Phase2ITUnpacker {

  // ---- device decode primitives, bit-exact with the host Phase2ITUnpacker ----

  using namespace Phase2ITSpec;

  ALPAKA_FN_ACC inline uint32_t readWord(const uint8_t* bytes, int wordIdx) {
    const uint8_t* p = bytes + wordIdx * 4;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
  }

  // MSB-first reader over a byte buffer, clamped like binaryToInt
  struct BitReader {
    const uint8_t* bytes;
    uint32_t len;
    uint32_t pos = 0;

    ALPAKA_FN_ACC bool next() {
      const bool b = (bytes[pos >> 3] >> (7 - (pos & 7))) & 1;
      ++pos;
      return b;
    }
    ALPAKA_FN_ACC bool nextOr0() { return pos < len ? next() : false; }
    ALPAKA_FN_ACC uint32_t bits(int n) {
      uint32_t r = 0;
      for (int i = 0; i < n && pos < len; ++i)
        r = (r << 1) | (next() ? 1u : 0u);
      return r;
    }
  };

  // Huffman pair: returns (first << 1 | second), mirroring decPairBits
  ALPAKA_FN_ACC inline int decPair(BitReader& br) {
    if (br.pos >= br.len)
      return 0b00;
    if (!br.next())
      return 0b01;
    if (br.pos >= br.len)
      return 0b10;
    if (!br.next())
      return 0b10;
    return 0b11;
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
    int start;    // first word of the module
    int end;      // one past its last word
    int bodyEnd;  // FED body size; payload reads are bounded by this, as in the legacy producer
  };

  ALPAKA_FN_ACC inline ModuleSpan moduleSpan(const uint8_t* fedBytes, int fedSizeWords, int numModules, int idxInFed) {
    const int offsetStart = HEADER_TRAILER_LINES;
    const int offsetBits = numModules * BITS_PER_WORD;
    const int padWords = ((BITS_PER_CHUNK - (offsetBits % BITS_PER_CHUNK)) % BITS_PER_CHUNK) / BITS_PER_WORD;
    const int dataBlockStart = offsetStart + numModules + padWords;
    ModuleSpan s;
    s.bodyEnd = fedSizeWords;
    s.start = dataBlockStart + int(readWord(fedBytes, offsetStart + idxInFed));
    s.end = (idxInFed + 1 < numModules) ? dataBlockStart + int(readWord(fedBytes, offsetStart + idxInFed + 1))
                                        : fedSizeWords;  // magic check stops at the IT trailer
    // Malformed offsets are skipped rather than followed out of the FED body:
    // the legacy producer logs and drops such a module, a kernel can only clamp.
    // FIXME no counter is propagated back, so such modules are dropped silently.
    if (s.start < dataBlockStart || s.start >= fedSizeWords)
      s.start = s.end = 0;
    if (s.end > fedSizeWords)
      s.end = fedSizeWords;
    if (s.end < s.start)
      s.end = s.start;
    return s;
  }

  // Walk the chips of one module: calls chip(chipId, firstPayloadWord, bitLen)
  template <typename TChip>
  ALPAKA_FN_ACC void forEachChip(const uint8_t* fedBytes, ModuleSpan span, TChip&& chip) {
    int cursor = span.start;
    for (int chipId = 0; cursor < span.end; ++chipId) {
      // A module never holds more than CHIPS_PER_MODULE chips. Without this the
      // walk can be driven past that by a corrupted header, and chipId indexes
      // the per-chip quadrant tables in hitToRowCol out of bounds.
      if (chipId >= CHIPS_PER_MODULE)
        break;
      const uint32_t hdr = readWord(fedBytes, cursor);
      if (((hdr >> 28) & 0xF) != CHIP_HEADER_MAGIC)
        break;  // 128-bit end padding: module exhausted
      const uint32_t endBit = (hdr >> 16) & 0x1F;
      const uint32_t sizeWords = hdr & 0xFFFF;
      // A payload running past the FED body is malformed. The legacy producer
      // logs it, hands the chip an empty stream and keeps walking; do the same,
      // since a zero length means the reader never touches the buffer.
      // A zero sizeWords with a non-zero endBit is malformed in the same way:
      // the unsigned length below would underflow to about 4e9 bits.
      const bool overrun = (sizeWords == 0 && endBit != 0) || cursor + 1 + int(sizeWords) > span.bodyEnd;
      const uint32_t bitLen = overrun         ? 0u
                              : (endBit == 0) ? sizeWords * BITS_PER_WORD
                                              : (sizeWords - 1) * BITS_PER_WORD + endBit;
      chip(chipId, cursor + 1, bitLen);
      cursor += 1 + int(sizeWords);
    }
  }

  // Decode one chip stream: calls hit(ccol, qrow, hitmapIndex, adc) per fired pixel
  template <typename THit>
  ALPAKA_FN_ACC void decodeChip(BitReader br, bool dropTot, THit&& hit) {
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
      for (int i = 0; i < HITMAP_SIZE; ++i) {
        if (!hm[i])
          continue;
        const int adc = dropTot ? 0 : int(br.bits(4));
        hit(int(ccol), int(qrow), i, adc);
      }
      previousIsLast = islast;
      prevRow = int(qrow);
    }
  }

  // ---- the kernels: thread-per-module (stage 1) and thread-per-chip (stage 2) ----

  namespace {
    // Start of the FED body owning module m, inside the concatenated buffer.
    ALPAKA_FN_ACC inline const uint8_t* fedBytes(const uint8_t* bytes, const ModuleMap& modMap, int m) {
      return bytes + modMap.fedWordBase[modMap.modFedIdx[m]] * 4;
    }
    // Word span of module m, taken from its FED's offset block.
    ALPAKA_FN_ACC inline ModuleSpan spanOf(const uint8_t* bytes, const ModuleMap& modMap, int m) {
      const int f = modMap.modFedIdx[m];
      return moduleSpan(fedBytes(bytes, modMap, m),
                        modMap.fedSizeWords[f],
                        modMap.fedModStart[f + 1] - modMap.fedModStart[f],
                        m - modMap.fedModStart[f]);
    }
  }  // namespace

  // Stage 1: raw FED bodies -> per-chip index into those bytes.

  // FIXME chips per module is static (ModuleInfo.nChips), so this count could be
  // built once per IOV instead of every event, saving a kernel and a host sync.
  struct ChipCountKernel {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc, const uint8_t* bytes, ModuleMap modMap, uint32_t* counts) const {
      for (auto m : cms::alpakatools::uniform_elements(acc, modMap.nModules)) {
        uint32_t n = 0;
        forEachChip(fedBytes(bytes, modMap, m), spanOf(bytes, modMap, m), [&](int, int, uint32_t) { ++n; });
        counts[m] = n;
      }
    }
  };

  struct ChipFillKernel {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  const uint8_t* bytes,
                                  ModuleMap modMap,
                                  const uint32_t* offsets,
                                  Phase2ITChipBitStreamSoAView chips) const {
      for (auto m : cms::alpakatools::uniform_elements(acc, modMap.nModules)) {
        // bitOffset is relative to the whole buffer, so stage 2 needs no module lookup
        const uint8_t* fb = fedBytes(bytes, modMap, m);
        const uint32_t fedByteBase = modMap.fedWordBase[modMap.modFedIdx[m]] * 4;
        uint32_t row = offsets[m];
        forEachChip(fb, spanOf(bytes, modMap, m), [&](int chipId, int payloadWord, uint32_t bitLen) {
          auto c = chips[row++];
          c.detId() = modMap.modDetId[m];
          c.bitOffset() = (fedByteBase + payloadWord * 4) * 8;
          c.bitLen() = bitLen;
          c.moduleId() = modMap.modGeomIdx[m];
          c.chipId() = uint8_t(chipId);
          c.subtype() = modMap.modSubtype[m];
        });
      }
    }
  };

  void runChipCountKernel(Queue& queue, const uint8_t* bytes, const ModuleMap& modMap, uint32_t* counts) {
    const auto wd = cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(modMap.nModules, 128), 128);
    alpaka::exec<Acc1D>(queue, wd, ChipCountKernel{}, bytes, modMap, counts);
  }

  void runChipFillKernel(Queue& queue,
                         const uint8_t* bytes,
                         const ModuleMap& modMap,
                         const uint32_t* offsets,
                         Phase2ITChipBitStreamSoAView chips) {
    const auto wd = cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(modMap.nModules, 128), 128);
    alpaka::exec<Acc1D>(queue, wd, ChipFillKernel{}, bytes, modMap, offsets, chips);
  }

  // Stage 2: per-chip bit streams -> digis.

  struct DigiCountKernel {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  const uint8_t* bytes,
                                  Phase2ITChipBitStreamSoAConstView chips,
                                  bool dropTot,
                                  uint32_t* counts) const {
      for (auto c : cms::alpakatools::uniform_elements(acc, chips.metadata().size())) {
        uint32_t n = 0;
        decodeChip(BitReader{bytes + (chips[c].bitOffset() >> 3), chips[c].bitLen()}, dropTot, [&](int, int, int, int) {
          ++n;
        });
        counts[c] = n;
      }
    }
  };

  struct DigiFillKernel {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  const uint8_t* bytes,
                                  Phase2ITChipBitStreamSoAConstView chips,
                                  bool dropTot,
                                  bool keepMode,
                                  const uint32_t* offsets,
                                  SiPixelDigisSoAView digis) const {
      constexpr int kRowShift = PixelChannelIdentifier::thePacking.row_shift;
      constexpr int kColShift = PixelChannelIdentifier::thePacking.column_shift;
      constexpr int kAdcShift = PixelChannelIdentifier::thePacking.adc_shift;
      // The collection carries one spare row past the digis; zero it once.
      if (cms::alpakatools::once_per_grid(acc)) {
        const int last = digis.metadata().size() - 1;
        digis[last].clus() = 0;
        digis[last].pdigi() = 0;
        digis[last].rawIdArr() = 0;
        digis[last].adc() = 0;
        digis[last].xx() = 0;
        digis[last].yy() = 0;
        digis[last].moduleId() = 0;
      }
      // offsets[] gives each chip a private output range, so no atomics are needed
      for (auto c : cms::alpakatools::uniform_elements(acc, chips.metadata().size())) {
        const auto chip = chips[c];
        const int subtype = chip.subtype();
        const int chipId = chip.chipId();
        uint32_t cursor = offsets[c];
        decodeChip(BitReader{bytes + (chip.bitOffset() >> 3), chip.bitLen()},
                   dropTot,
                   [&](int ccol, int qrow, int i, int adc) {
                     int row, col;
                     hitToRowCol(subtype, chipId, ccol, qrow, i, keepMode, row, col);
                     auto d = digis[cursor++];
                     d.clus() = 0;
                     d.pdigi() =
                         (uint32_t(row) << kRowShift) | (uint32_t(col) << kColShift) | (uint32_t(adc) << kAdcShift);
                     d.rawIdArr() = chip.detId();
                     d.adc() = uint16_t(adc);
                     d.xx() = uint16_t(row);
                     d.yy() = uint16_t(col);
                     d.moduleId() = chip.moduleId();
                   });
      }
    }
  };

  void runDigiCountKernel(
      Queue& queue, const uint8_t* bytes, Phase2ITChipBitStreamSoAConstView chips, bool dropTot, uint32_t* counts) {
    const int n = chips.metadata().size();
    const auto wd = cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(n, 128), 128);
    alpaka::exec<Acc1D>(queue, wd, DigiCountKernel{}, bytes, chips, dropTot, counts);
  }

  void runDigiFillKernel(Queue& queue,
                         const uint8_t* bytes,
                         Phase2ITChipBitStreamSoAConstView chips,
                         bool dropTot,
                         bool keepMode,
                         const uint32_t* offsets,
                         SiPixelDigisSoAView digis) {
    const int n = chips.metadata().size();
    const auto wd = cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(n, 128), 128);
    alpaka::exec<Acc1D>(queue, wd, DigiFillKernel{}, bytes, chips, dropTot, keepMode, offsets, digis);
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::Phase2ITUnpacker
