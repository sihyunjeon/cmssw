#ifndef EventFilter_Phase2PixelRawToDigi_Phase2ITUnpacker_h
#define EventFilter_Phase2PixelRawToDigi_Phase2ITUnpacker_h

// Shared functions for the IT unpacking chain

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "DataFormats/FEDRawData/interface/SLinkRocketHeaders.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITBitReader.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITChip.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITQCore.h"
#include "DataFormats/SiPixelDigi/interface/PixelDigi.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2DAQFormatSpecification.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/Utilities/interface/Exception.h"

namespace Phase2ITUnpacker {

  inline uint32_t readWord(const unsigned char* dataPtr, int wordIdx) {
    const int byteIdx = wordIdx * 4;
    return (static_cast<uint32_t>(dataPtr[byteIdx]) << 24) | (static_cast<uint32_t>(dataPtr[byteIdx + 1]) << 16) |
           (static_cast<uint32_t>(dataPtr[byteIdx + 2]) << 8) | static_cast<uint32_t>(dataPtr[byteIdx + 3]);
  }

  // FIXME this later need to be properly treated in simulation
  // KEEP shifts each chip boundary to the gap midline - only for in & out validation purposes
  // DROP/AGGREGATE keep the physical extents, either removing the ToT or stacking up the ToT
  inline bool parseKeepMode(const std::string& s, const char* who) {
    if (s == "DROP" || s == "AGGREGATE")
      return false;
    if (s == "KEEP")
      return true;
    throw cms::Exception(who) << "handleGapPixels must be one of DROP/KEEP/AGGREGATE, got '" << s << "'";
  }

  // Validate the SLinkRocket wrapper and return the IT body it wraps
  inline const unsigned char* stripSLinkWrapper(const unsigned char* fragPtr,
                                                uint32_t fragSize,
                                                int fedId,
                                                int& fedSizeInWords) {
    using namespace Phase2DAQFormatSpecification;
    constexpr unsigned int kSlinkHdrBytes = sizeof(SLinkRocketHeader_v3);
    constexpr unsigned int kSlinkTrlBytes = sizeof(SLinkRocketTrailer_v3);
    auto const* sh = reinterpret_cast<const SLinkRocketHeader_v3*>(fragPtr);
    auto const* st = reinterpret_cast<const SLinkRocketTrailer_v3*>(fragPtr + fragSize - kSlinkTrlBytes);
    if (!sh->verifyMarker())
      throw cms::Exception("Phase2ITUnpacker") << "Invalid SLinkRocket BOE for fed " << fedId;
    if (!st->verifyMarker())
      throw cms::Exception("Phase2ITUnpacker") << "Invalid SLinkRocket EOE for fed " << fedId;
    if (st->eventLenBytes() != fragSize)
      throw cms::Exception("Phase2ITUnpacker") << "SLinkRocket trailer length mismatch for fed " << fedId
                                               << ": trailer says " << st->eventLenBytes() << ", actual " << fragSize;
    fedSizeInWords = static_cast<int>(fragSize / BYTES_PER_WORD) - (kSlinkHdrBytes + kSlinkTrlBytes) / BYTES_PER_WORD;
    return fragPtr + kSlinkHdrBytes;
  }

  // FIXME for now this works because we are assuming 4 lines of 0xFFFFFFFF for
  // both headers and trailers. Later we have to come up with something more
  // concrete to parse out these 4 lines.
  inline bool verifyHeaderTrailerPattern(const unsigned char* dataPtr, int wordIdx) {
    using namespace Phase2DAQFormatSpecification;
    for (int i = 0; i < HEADER_TRAILER_LINES; i++) {
      if (readWord(dataPtr, wordIdx + i) != HEADER_TRAILER_PATTERN)
        return false;
    }
    return true;
  }

  inline int findTrailerStart(const unsigned char* dataPtr, int fedSizeInWords) {
    using namespace Phase2DAQFormatSpecification;
    // Start searching from the end, going backwards
    for (int i = fedSizeInWords - HEADER_TRAILER_LINES; i >= HEADER_TRAILER_LINES; --i) {
      if (verifyHeaderTrailerPattern(dataPtr, i))
        return i;
    }
    return -1;  // trailer not found
  }

  // Malformed input the CPU and Alpaka unpackers both skip, counted over an event
  struct MalformedCounts {
    uint32_t badOffsetModules = 0;  // offset outside its FED's data block: the module is dropped
    uint32_t extraChipModules = 0;  // more chip headers than its subtype has chips: the extra ones are skipped
    uint32_t overrunChips = 0;      // payload past its module's end, or an end bit with no words: read as empty
    uint32_t truncatedChips = 0;    // stream ends inside a quarter core: that quarter core's hits are dropped
  };

  // One warning per event, worded the same by both unpackers
  inline void reportMalformed(const char* who, const MalformedCounts& c, std::size_t nModules) {
    if (c.badOffsetModules + c.extraChipModules + c.overrunChips + c.truncatedChips == 0)
      return;
    edm::LogWarning(who) << "Malformed IT data: " << c.badOffsetModules << " of " << nModules
                         << " modules dropped for an out-of-range offset, " << c.extraChipModules
                         << " modules with more chip headers than their subtype has chips, " << c.overrunChips
                         << " chips read as empty for a payload past their module's end, and " << c.truncatedChips
                         << " chips that end inside a quarter core, whose hits were dropped.";
  }

  // Word span [start, end) of one module inside a FED body.
  struct ModuleSpan {
    int start;
    int end;
  };

  // Walk through the modules of an FED. A module starts inside the data block and ends where the next one
  // starts, or at the IT trailer; a module whose offset points elsewhere is dropped.
  template <typename ModuleF>
  void forEachModule(
      const unsigned char* dataPtr, int trailerStart, int numModules, MalformedCounts& bad, ModuleF&& module) {
    using namespace Phase2DAQFormatSpecification;
    const int offsetStart = HEADER_TRAILER_LINES;

    // The offset block is padded to a 128-bit boundary at its end
    const int offsetBits = numModules * BITS_PER_WORD;
    const int paddingBits = (BITS_PER_CHUNK - (offsetBits % BITS_PER_CHUNK)) % BITS_PER_CHUNK;
    const int dataBlockStart = offsetStart + numModules + paddingBits / BITS_PER_WORD;
    // the offset block itself has to end before the trailer
    if (dataBlockStart > trailerStart) {
      bad.badOffsetModules += numModules;
      return;
    }
    // offsets are checked before they are added, so a corrupt one cannot overflow
    const uint32_t dataWords = trailerStart - dataBlockStart;
    for (int modIdx = 0; modIdx < numModules; modIdx++) {
      const uint32_t startOffset = readWord(dataPtr, offsetStart + modIdx);
      if (startOffset >= dataWords) {
        ++bad.badOffsetModules;
        continue;
      }
      const uint32_t endOffset =
          (modIdx + 1 < numModules) ? std::min(readWord(dataPtr, offsetStart + modIdx + 1), dataWords) : dataWords;
      module(modIdx,
             ModuleSpan{dataBlockStart + static_cast<int>(startOffset),
                        dataBlockStart + static_cast<int>(std::max(endOffset, startOffset))});
    }
  }

  // Walk through the chips of a module. A chip's ID is its place in the module, so the walk stops after the
  // subtype's chips; a chip whose payload would pass the module's end is read as empty.
  template <typename ChipF>
  void forEachChip(const unsigned char* dataPtr, ModuleSpan span, int maxChips, MalformedCounts& bad, ChipF&& chip) {
    using namespace Phase2DAQFormatSpecification;
    int chipCursor = span.start;
    for (int chipId = 0; chipCursor < span.end; chipId++) {
      const uint32_t chipHeader = readWord(dataPtr, chipCursor);
      const uint32_t magic = (chipHeader >> 28) & 0xF;
      // FIXME ignoring the chip error bits with Dummy for now
      const uint32_t endBit = (chipHeader >> 16) & 0x1F;
      const uint32_t sizeWords = chipHeader & 0xFFFF;

      if (magic != CHIP_HEADER_MAGIC)
        break;
      if (chipId >= maxChips) {
        ++bad.extraChipModules;
        break;
      }

      // Create the stream length in bits.
      //   endBit == 0  -> last word is full or chip is empty: size = sizeWords * 32
      //   endBit  > 0  -> last word holds endBit real bits:   size = (sizeWords - 1) * 32 + endBit
      const bool overrun = (sizeWords == 0 && endBit != 0) || chipCursor + 1 + static_cast<int>(sizeWords) > span.end;
      if (overrun)
        ++bad.overrunChips;
      const uint32_t nBits = overrun         ? 0u
                             : (endBit == 0) ? sizeWords * BITS_PER_WORD
                                             : (sizeWords - 1) * BITS_PER_WORD + endBit;
      chip(chipId, chipCursor + 1, nBits);
      chipCursor += 1 + static_cast<int>(sizeWords);
    }
  }

  // Decode chip bit stream, appending its digis to digis.
  // Returns true if the stream ends inside a quarter core (malformed): that quarter core's hits are ignored.
  inline bool decodeChip(
      Phase2ITBitReader& reader, int chipId, int subtype, bool dropTot, bool keepMode, std::vector<PixelDigi>& digis) {
    using namespace Phase2DAQFormatSpecification;
    int currentCol = 0;
    int currentRow = 0;
    int previousRow = -1;
    bool previousIsLast = true;
    // keep the chip position through the first hit position
    // so that we don't repeat this for all hits that live in the same chip
    std::optional<std::pair<int, int>> origin;

    while (!reader.atEnd()) {
      // Always start with a fresh ccol only at the start of a new column group
      if (previousIsLast) {
        currentCol = reader.bits(6);
      }

      const bool islast = reader.next();
      const bool isneighbor = reader.next();

      // isneighbor=1 means the previous qrow address is current_qrow - 1, so
      // the qrow field is omitted and we give previous + 1.
      if (isneighbor) {
        currentRow = previousRow + 1;
      } else {
        currentRow = reader.bits(8);
      }

      std::array<bool, 16> hitmap = Phase2ITQCore::decodeHitmap(reader);
      uint32_t mask = 0;
      for (int i = 0; i < HITMAP_SIZE; i++)
        mask |= uint32_t(hitmap[i]) << i;
      const int numHits = std::popcount(mask);
      // In dropTot mode the encoder skipped the ToT bits: emit adc=0 for every
      // hit, otherwise read 4 bits per hit as the ToT/ADC value.
      std::array<int, 16> adcValues{};
      if (!dropTot)
        adcValues = Phase2ITQCore::decodeADCs(reader, numHits);
      // a stream cut inside this quarter core leaves it incomplete, so none of its hits is kept
      if (reader.overran())
        return true;

      // Hitmap indices for 8-15 correspond to QCore rows 0-1 and hitmap indices for 0-7 correspond to QCore rows 2-3.
      // Thus in reverse order, walk on the bits that are set and collect row/col/adc values.
      for (const uint32_t half : {0xFF00u, 0x00FFu})
        for (uint32_t m = mask & half; m != 0; m &= m - 1) {
          const int i = std::countr_zero(m);
          const int adc = adcValues[std::popcount(mask & ((1u << i) - 1))];
          int rocRow = i / 8;
          int rocCol = i % 8;
          int shiftedRow = rocRow * 2 + (rocCol % 2);
          int shiftedCol = rocCol / 2;
          int shiftedIndex = shiftedRow * HITMAP_COL + shiftedCol;
          auto [localRow, localCol] = Phase2ITChip::decodeQCoreIndex(shiftedIndex);
          if (!origin)
            origin = Phase2ITChip::chipOrigin(chipId, subtype, keepMode);
          // getGlobalPixelCoordinate, with the chip origin looked up once per chip
          const int globalRow = origin->first + currentRow * HITMAP_COL + localRow;
          const int globalCol = origin->second + currentCol * HITMAP_ROW + localCol;
          digis.push_back(PixelDigi(globalRow, globalCol, adc));
        }

      previousIsLast = islast;
      previousRow = currentRow;
    }
    return false;
  }

  inline void sortModuleDigis(const std::vector<PixelDigi>& in,
                              int rowSplit,
                              std::vector<uint32_t>& count,
                              std::vector<PixelDigi>& out) {
    int lo = in.front().column(), hi = lo;
    for (auto const& d : in) {
      lo = std::min(lo, d.column());
      hi = std::max(hi, d.column());
    }
    count.assign(hi - lo + 2, 0);
    for (auto const& d : in)
      ++count[d.column() - lo + 1];
    for (size_t c = 1; c < count.size(); ++c)
      count[c] += count[c - 1];
    out.resize(in.size());
    for (auto const& d : in)
      if (d.row() < rowSplit)
        out[count[d.column() - lo]++] = d;
    for (auto const& d : in)
      if (d.row() >= rowSplit)
        out[count[d.column() - lo]++] = d;
    // Corrupt input can decode hits out of order, so the result is checked and falls back to std::sort.
    if (!std::is_sorted(out.begin(), out.end()))
      std::sort(out.begin(), out.end());
  }

}  // namespace Phase2ITUnpacker

#endif  // EventFilter_Phase2PixelRawToDigi_Phase2ITUnpacker_h
