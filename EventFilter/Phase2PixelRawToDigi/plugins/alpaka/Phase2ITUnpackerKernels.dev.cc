// -*- C++ -*-
// Package:    EventFilter/Phase2PixelRawToDigi
// Class:      Phase2ITUnpackerKernels
// Description: Device code of the two Alpaka unpacker stages
// Maintainer: Si Hyun Jeon, shjeon@cern.ch

#include <type_traits>

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

  using namespace Phase2DAQFormatSpecification;

  ALPAKA_FN_ACC inline uint32_t readWord(const uint8_t* bytes, int wordIdx) {
    const uint8_t* p = bytes + wordIdx * 4;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
  }

  // MSB-first reader over a byte buffer, as Phase2ITBitReader; a read past the end gives 0 and sets past
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
    ALPAKA_FN_ACC bool atEnd() const { return pos >= len; }
    // nothing has run past the end and n more bits are left
    ALPAKA_FN_ACC bool has(uint32_t n) const { return !past && pos + n <= len; }
    ALPAKA_FN_ACC uint32_t taken() const { return pos; }
    // pass n bits known to be in the stream
    ALPAKA_FN_ACC void advance(uint32_t n) { pos += n; }
  };

  // The same reads from a 64-bit window refilled one 32-bit word at a time, loading nothing past the stream
  struct WindowReader {
    const uint32_t* words;
    uint32_t len;
    uint64_t win = 0;     // unread bits, the next one at bit 63
    uint32_t avail = 0;   // unread bits in win
    uint32_t loaded = 0;  // words put into win

    // a chip's stream starts on a word, so its words are 4-byte aligned
    ALPAKA_FN_ACC WindowReader(const uint8_t* bytes, uint32_t nBits)
        : words(reinterpret_cast<const uint32_t*>(bytes)), len(nBits) {}

    // at least n <= 32 unread bits in win
    ALPAKA_FN_ACC void need(uint32_t n) {
      if (avail >= n)
        return;
      const uint32_t first = loaded * 32;
      uint32_t w = 0;
      if (first < len) {
        const uint32_t raw = words[loaded];  // stored big-endian
        w = (raw >> 24) | ((raw >> 8) & 0xff00u) | ((raw << 8) & 0xff0000u) | (raw << 24);
        if (len - first < 32)
          w &= ~0u << (32 - (len - first));
      }
      win |= uint64_t(w) << (32 - avail);
      avail += 32;
      ++loaded;
    }
    ALPAKA_FN_ACC uint32_t peek(uint32_t n) const { return uint32_t(win >> (64 - n)); }
    ALPAKA_FN_ACC void skip(uint32_t n) {
      win <<= n;
      avail -= n;
    }
    ALPAKA_FN_ACC uint32_t bits(uint32_t n) {
      need(n);
      const uint32_t r = peek(n);
      skip(n);
      return r;
    }
    ALPAKA_FN_ACC bool nextOr0() { return bits(1) != 0; }
    // bits taken so far, counting those past the end
    ALPAKA_FN_ACC uint32_t taken() const { return loaded * 32 - avail; }
    ALPAKA_FN_ACC bool atEnd() const { return taken() >= len; }
    ALPAKA_FN_ACC bool has(uint32_t n) const { return taken() + n <= len; }
    // pass n bits known to be in the stream, loading only the word the next read starts in
    ALPAKA_FN_ACC void advance(uint32_t n) {
      if (n <= avail) {
        skip(n);
        return;
      }
      n -= avail;
      win = 0;
      avail = 0;
      loaded += n / 32;
      if (n % 32 != 0) {
        need(n % 32);
        skip(n % 32);
      }
    }
  };

  // a load per bit is cheap on a CPU, but on a GPU it costs far more than the window's shifts
  using ChipReader = std::conditional_t<std::is_same_v<alpaka::Dev<Acc1D>, alpaka::DevCpu>, BitReader, WindowReader>;

  // Huffman pair: returns (first << 1 | second), mirroring decPairBits
  ALPAKA_FN_ACC inline int decPair(BitReader& br) {
    if (!br.nextOr0())  // "0 -> 01" substitute
      return 0b01;
    return br.nextOr0() ? 0b11 : 0b10;
  }
  // the same from both bits at once
  ALPAKA_FN_ACC inline int decPair(WindowReader& br) {
    br.need(2);
    const int p = int(br.peek(2));
    br.skip(p < 0b10 ? 1 : 2);
    return p < 0b10 ? 0b01 : p;
  }

  // a pair's first half -> bit k, its second -> bit k + 1
  ALPAKA_FN_ACC inline uint32_t pairBits(int p, int k) {
    return uint32_t((p >> 1) & 1) << k | uint32_t(p & 1) << (k + 1);
  }

  // decChunk(n=8): fixed 8 -> 4 -> 2 tree traversal, the pixels returned as bits 0-7
  template <typename TReader>
  ALPAKA_FN_ACC inline uint32_t decChunk8(TReader& br) {
    const int halves = decPair(br);  // pixels 0-3 and 4-7
    uint32_t quarters = 0;           // bit q: pixels 2q and 2q + 1
    if (halves & 2)
      quarters |= pairBits(decPair(br), 0);
    if (halves & 1)
      quarters |= pairBits(decPair(br), 2);
    uint32_t pixels = 0;
    for (int q = 0; q < 4; ++q)
      if (quarters & (1u << q))
        pixels |= pairBits(decPair(br), 2 * q);
    return pixels;
  }

  // the 16-pixel hitmap as bits 0-15
  template <typename TReader>
  ALPAKA_FN_ACC inline uint32_t decodeHitmap(TReader& br) {
    const int p = decPair(br);
    uint32_t hm = 0;
    if (p & 2)
      hm |= decChunk8(br);
    if (p & 1)
      hm |= decChunk8(br) << 8;
    return hm;
  }

  // row and column of the chip's first pixel in its module (Phase2ITChip::chipOrigin)
  ALPAKA_FN_ACC inline void chipOrigin(int subtype, int chipId, bool keepMode, int& rowOffset, int& colOffset) {
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
    const int rowsPerChip = keepMode ? Phase2ITChip::kRowsPerChipKeep : Phase2ITChip::kRowsPerChip;
    const int largePixelNRows = keepMode ? Phase2ITChip::kLargePixelNRowsKeep : Phase2ITChip::kLargePixelNRows;
    const int colsPerChip = keepMode ? Phase2ITChip::kColsPerChipKeep : Phase2ITChip::kColsPerChip;
    const int largePixelNCols = keepMode ? Phase2ITChip::kLargePixelNColsKeep : Phase2ITChip::kLargePixelNCols;
    rowOffset = (kQuadX[subtype][chipId] > 0) ? (rowsPerChip + largePixelNRows) : 0;
    colOffset = (kQuadY[subtype][chipId] > 0) ? (colsPerChip + largePixelNCols) : 0;
  }

  // qcore address -> row/col of its first pixel in the module, from the chip's origin
  ALPAKA_FN_ACC inline void qcoreOrigin(int rowOffset, int colOffset, int ccol, int qrow, int& row, int& col) {
    row = rowOffset + qrow * HITMAP_COL;
    col = colOffset + ccol * HITMAP_ROW;
  }

  // hitmap index -> row/col in the quarter core
  ALPAKA_FN_ACC inline void pixelInQCore(int i, int& row, int& col) {
    const int rocRow = i / 8;
    const int rocCol = i % 8;
    const int shifted = (rocRow * 2 + (rocCol % 2)) * HITMAP_COL + rocCol / 2;
    const int adjusted = (shifted + 8) % 16;
    row = adjusted / 4;
    col = adjusted % 4;
  }

  // hitmap index + qcore address -> row/col in the module, from the chip's origin (getGlobalPixelCoordinate)
  ALPAKA_FN_ACC inline void hitToRowCol(int rowOffset, int colOffset, int ccol, int qrow, int i, int& row, int& col) {
    int qcoreRow, qcoreCol, localRow, localCol;
    qcoreOrigin(rowOffset, colOffset, ccol, qrow, qcoreRow, qcoreCol);
    pixelInQCore(i, localRow, localCol);
    row = qcoreRow + localRow;
    col = qcoreCol + localCol;
  }

  // Word span [start, end) of one module inside a FED body
  struct ModuleSpan {
    int start;       // first word of the module
    int end;         // one past its last word: where the next module starts, or the IT trailer
    bool badOffset;  // offset outside the data block, so the module was dropped
  };

  // Same rules as the CPU forEachModule
  ALPAKA_FN_ACC inline ModuleSpan moduleSpan(const uint8_t* fedBytes, int trailerStart, int numModules, int idxInFed) {
    const int offsetStart = HEADER_TRAILER_LINES;
    const int offsetBits = numModules * BITS_PER_WORD;
    const int padWords = ((BITS_PER_CHUNK - (offsetBits % BITS_PER_CHUNK)) % BITS_PER_CHUNK) / BITS_PER_WORD;
    const int dataBlockStart = offsetStart + numModules + padWords;
    // a malformed offset drops the module, flagged for the producer to report
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

  // Same rules as the CPU forEachChip: calls chip(chipId, firstPayloadWord, bitLen, overrun) for the first maxChips
  // chips; returns true if a further chip header follows them.
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

  // Walk one chip stream: calls qcore(ccol, qrow, hitmap, nHits) per complete quarter core, the reader at its ToTs.
  // Returns true if the stream ends inside a quarter core (malformed): that quarter core is ignored.
  template <typename TAcc, typename TReader, typename TQCore>
  ALPAKA_FN_ACC bool walkChip(TAcc const& acc, TReader& br, bool dropTot, TQCore&& qcore) {
    bool previousIsLast = true;
    uint32_t ccol = 0;
    int prevRow = -1;
    while (!br.atEnd()) {
      if (previousIsLast)
        ccol = br.bits(6);
      const bool islast = br.nextOr0();
      const bool isneighbor = br.nextOr0();
      const uint32_t qrow = isneighbor ? uint32_t(prevRow + 1) : br.bits(8);

      const uint32_t hm = decodeHitmap(br);
      // complete only if no read ran past the end and all its ToTs are still in the stream
      const uint32_t nHits = alpaka::popcount(acc, hm);
      if (!br.has(dropTot ? 0 : 4 * nHits))
        return true;
      qcore(ccol, qrow, hm, nHits);
      previousIsLast = islast;
      prevRow = int(qrow);
    }
    return false;
  }

  // Decode one chip stream: calls hit(ccol, qrow, hitmapIndex, adc) per fired pixel, with walkChip's return value.
  template <typename TAcc, typename TReader, typename THit>
  ALPAKA_FN_ACC bool decodeChip(TAcc const& acc, TReader br, bool dropTot, THit&& hit) {
    return walkChip(acc, br, dropTot, [&](uint32_t ccol, uint32_t qrow, uint32_t hm, uint32_t) {
      // the fired pixels in ascending hitmap index, as the ToTs are ordered
      for (uint32_t m = hm; m != 0; m &= m - 1) {
        const int i = alpaka::ffs(acc, static_cast<std::int32_t>(m)) - 1;
        const int adc = dropTot ? 0 : int(br.bits(4));
        hit(int(ccol), int(qrow), i, adc);
      }
    });
  }

  // Stage 1: raw FED bodies -> a row per module and kChipSlots chip rows, locating their words in those bytes

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

    // What stage 1 found about a chip and its module; the module's flags are counted once, by its first chip
    template <typename TModule, typename TChip>
    ALPAKA_FN_ACC inline void countStage1Flags(
        Acc1D const& acc, int chipId, TModule const& mod, TChip const& chip, uint32_t* stats) {
      if (chipId == 0 && mod.badOffset() != 0)
        alpaka::atomicAdd(acc, &stats[kBadOffsetModules], 1u, alpaka::hierarchy::Blocks{});
      if (chipId == 0 && mod.extraChips() != 0)
        alpaka::atomicAdd(acc, &stats[kExtraChipModules], 1u, alpaka::hierarchy::Blocks{});
      if (chip.overrun() != 0)
        alpaka::atomicAdd(acc, &stats[kOverrunChips], 1u, alpaka::hierarchy::Blocks{});
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
      countStage1Flags(acc, chipId, mod, chip, stats);
      const uint32_t begin = firstSlot(chip.payloadWord(), dropTot);
      const uint32_t end = begin + chip.bitLen() / minBitsPerHit(dropTot);
      // the chip's place in its module, looked up once for all its hits
      int rowOffset, colOffset;
      chipOrigin(mod.subtype(), chipId, keepMode, rowOffset, colOffset);
      uint32_t n = 0;
      uint32_t lost = 0;
      auto store = [&](int ccol, int qrow, int i, int adc) {
        // complete quarter cores always fit their chip's slots; this is a safety net
        if (begin + n >= end) {
          ++lost;
          return;
        }
        int row, col;
        hitToRowCol(rowOffset, colOffset, ccol, qrow, i, row, col);
        mediator[begin + n++] = DecodedHit{uint16_t(row), uint16_t(col), uint16_t(adc)};
      };
      const bool cut = decodeChip(acc, ChipReader{bytes + chip.payloadWord() * 4, chip.bitLen()}, dropTot, store);
      counts[c] = n;
      if (cut)
        alpaka::atomicAdd(acc, &stats[kTruncatedChips], 1u, alpaka::hierarchy::Blocks{});
      if (lost != 0)
        alpaka::atomicAdd(acc, &stats[kDroppedHits], lost, alpaka::hierarchy::Blocks{});
    }

    // Two-pass decode, pass 1: record chip row c's complete quarter cores, keeping the hits decodeChipRow would keep
    ALPAKA_FN_ACC inline void scanChipRow(Acc1D const& acc,
                                          uint32_t c,
                                          const uint8_t* bytes,
                                          Phase2ITModuleStreamSoAConstView modules,
                                          Phase2ITChipStreamSoAConstView chips,
                                          bool dropTot,
                                          bool keepMode,
                                          QCoreRecord* qcores,
                                          uint32_t* qcounts,
                                          uint32_t* counts,
                                          uint32_t* stats) {
      const auto mod = modules[c / kChipSlots];
      const int chipId = c % kChipSlots;
      const auto chip = chips[c];
      countStage1Flags(acc, chipId, mod, chip, stats);
      const uint32_t begin = firstSlot(chip.payloadWord(), dropTot);
      const uint32_t slots = chip.bitLen() / minBitsPerHit(dropTot);
      int rowOffset, colOffset;
      chipOrigin(mod.subtype(), chipId, keepMode, rowOffset, colOffset);
      uint32_t nq = 0;
      uint32_t nh = 0;
      ChipReader br{bytes + chip.payloadWord() * 4, chip.bitLen()};
      const bool cut = walkChip(acc, br, dropTot, [&](uint32_t ccol, uint32_t qrow, uint32_t hm, uint32_t nHits) {
        // a quarter core starting past the chip's slots loses all its hits; with a hit each, the records fit too
        if (nh < slots) {
          int row, col;
          qcoreOrigin(rowOffset, colOffset, int(ccol), int(qrow), row, col);
          qcores[begin + nq++] = QCoreRecord{hm | uint32_t(col) << 16, uint32_t(row), br.taken(), nh};
        }
        nh += nHits;
        br.advance(dropTot ? 0 : 4 * nHits);
      });
      const uint32_t n = nh < slots ? nh : slots;
      counts[c] = n;
      qcounts[c] = nq;
      if (cut)
        alpaka::atomicAdd(acc, &stats[kTruncatedChips], 1u, alpaka::hierarchy::Blocks{});
      if (nh != n)
        alpaka::atomicAdd(acc, &stats[kDroppedHits], nh - n, alpaka::hierarchy::Blocks{});
    }

    // the spare row past the digis (nDigis() + 1 rows), as an unclustered empty digi
    ALPAKA_FN_ACC inline void clearSpareRow(SiPixelDigisSoAView digis) {
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

    // digi d from hit h of module mod
    template <typename TDigi, typename TModule>
    ALPAKA_FN_ACC inline void fillDigi(TDigi d, TModule const& mod, DecodedHit h) {
      constexpr int kRowShift = PixelChannelIdentifier::thePacking.row_shift;
      constexpr int kColShift = PixelChannelIdentifier::thePacking.column_shift;
      constexpr int kAdcShift = PixelChannelIdentifier::thePacking.adc_shift;
      // not yet clustered; 0 is a valid cluster id, so use the sentinel
      d.clus() = ::pixelClustering::invalidClusterId;
      d.pdigi() = (uint32_t(h.row) << kRowShift) | (uint32_t(h.col) << kColShift) | (uint32_t(h.adc) << kAdcShift);
      d.rawIdArr() = mod.detId();
      d.adc() = h.adc;
      d.xx() = h.row;
      d.yy() = h.col;
      d.moduleId() = mod.moduleId();
      // the adc before the clusterizer calibrates it, as the Run-3 unpacker fills it
      d.rawADC() = uint8_t(h.adc);
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
      if (cms::alpakatools::once_per_grid(acc))
        clearSpareRow(digis);
      // one block per module; offsets[] gives each chip rows of its own, so no atomics are needed
      for (auto m : cms::alpakatools::independent_groups(acc, modules.metadata().size())) {
        const auto mod = modules[m];
        for (uint32_t c = m * kChipSlots; c < (m + 1) * kChipSlots; ++c) {
          const uint32_t first = offsets[c];
          const DecodedHit* hits = mediator + firstSlot(chips[c].payloadWord(), dropTot);
          for (auto j : cms::alpakatools::independent_group_elements(acc, counts[c])) {
            const DecodedHit h = hits[j];
            fillDigi(digis[first + j], mod, h);
          }
        }
      }
    }
  };

  struct QCoreScanKernel {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  const uint8_t* bytes,
                                  Phase2ITModuleStreamSoAConstView modules,
                                  Phase2ITChipStreamSoAConstView chips,
                                  bool perChip,
                                  bool dropTot,
                                  bool keepMode,
                                  QCoreRecord* qcores,
                                  uint32_t* qcounts,
                                  uint32_t* counts,
                                  uint32_t* stats) const {
      if (perChip) {
        for (auto c : cms::alpakatools::uniform_elements(acc, chips.metadata().size()))
          scanChipRow(acc, c, bytes, modules, chips, dropTot, keepMode, qcores, qcounts, counts, stats);
      } else {
        for (auto m : cms::alpakatools::uniform_elements(acc, modules.metadata().size()))
          for (uint32_t c = m * kChipSlots; c < (m + 1) * kChipSlots; ++c)
            scanChipRow(acc, c, bytes, modules, chips, dropTot, keepMode, qcores, qcounts, counts, stats);
      }
    }
  };

  struct QCoreDigiKernel {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  const uint8_t* bytes,
                                  Phase2ITModuleStreamSoAConstView modules,
                                  Phase2ITChipStreamSoAConstView chips,
                                  bool dropTot,
                                  const QCoreRecord* qcores,
                                  const uint32_t* qcounts,
                                  const uint32_t* counts,
                                  const uint32_t* offsets,
                                  SiPixelDigisSoAView digis) const {
      static_assert(kChipSlots == 4);
      if (cms::alpakatools::once_per_grid(acc))
        clearSpareRow(digis);
      // one block per module, whose threads take its chips' quarter cores side by side, chip after chip
      for (auto m : cms::alpakatools::independent_groups(acc, modules.metadata().size())) {
        const auto mod = modules[m];
        const uint32_t c0 = m * kChipSlots;
        // the quarter cores of chips 0..k end at qk
        const uint32_t q0 = qcounts[c0];
        const uint32_t q1 = q0 + qcounts[c0 + 1];
        const uint32_t q2 = q1 + qcounts[c0 + 2];
        const uint32_t q3 = q2 + qcounts[c0 + 3];
        for (auto j : cms::alpakatools::independent_group_elements(acc, q3)) {
          const int k = (j >= q0) + (j >= q1) + (j >= q2);
          const uint32_t c = c0 + k;
          const uint32_t before = k == 3 ? q2 : k == 2 ? q1 : k == 1 ? q0 : 0;
          const auto chip = chips[c];
          const QCoreRecord r = qcores[firstSlot(chip.payloadWord(), dropTot) + j - before];
          ChipReader br{bytes + chip.payloadWord() * 4, chip.bitLen()};
          if (!dropTot)
            br.advance(r.totBit);
          const uint32_t first = offsets[c];
          const uint32_t n = counts[c];
          // the chip's hits past its slots were dropped by the scan
          uint32_t slot = r.hitBase;
          for (uint32_t hm = r.hitmapCol & 0xFFFF; hm != 0 && slot < n; hm &= hm - 1, ++slot) {
            const int i = alpaka::ffs(acc, static_cast<std::int32_t>(hm)) - 1;
            const int adc = dropTot ? 0 : int(br.bits(4));
            int localRow, localCol;
            pixelInQCore(i, localRow, localCol);
            const int row = int(r.row) + localRow;
            const int col = int(r.hitmapCol >> 16) + localCol;
            fillDigi(digis[first + slot], mod, DecodedHit{uint16_t(row), uint16_t(col), uint16_t(adc)});
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
                          uint32_t blockSize) {
    const int n = perChip ? chips.metadata().size() : modules.metadata().size();
    const auto wd = cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(n, blockSize), blockSize);
    alpaka::exec<Acc1D>(
        queue, wd, QCoreScanKernel{}, bytes, modules, chips, perChip, dropTot, keepMode, qcores, qcounts, counts, stats);
  }

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
                          uint32_t blockSize) {
    const int n = modules.metadata().size();
    // one block per module
    const auto wd = cms::alpakatools::make_workdiv<Acc1D>(n, blockSize);
    alpaka::exec<Acc1D>(
        queue, wd, QCoreDigiKernel{}, bytes, modules, chips, dropTot, qcores, qcounts, counts, offsets, digis);
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
