#ifndef EventFilter_Phase2PixelRawToDigi_plugins_alpaka_Phase2ITDecode_h
#define EventFilter_Phase2PixelRawToDigi_plugins_alpaka_Phase2ITDecode_h

// Device-side primitives of the Phase-2 IT DAQ format, bit-exact with the
// legacy RawToBitStreamProducer / BitStreamToPixelProducer / Phase2ITQCore.
// Shared by the Phase2ITRawToBitStream / Phase2ITBitStreamToDigi kernels.

#include <cstdint>

#include <alpaka/alpaka.hpp>

#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITChip.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2DAQFormatSpecification.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::phase2it {

  using namespace Phase2DAQFormatSpecification;

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
    const int padWords = ((BITS_PER_CHUNK - (offsetBits % BITS_PER_CHUNK)) % BITS_PER_CHUNK) /
                         BITS_PER_WORD;
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
      const uint32_t bitLen = overrun ? 0u
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

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::phase2it

#endif  // EventFilter_Phase2PixelRawToDigi_plugins_alpaka_Phase2ITDecode_h
