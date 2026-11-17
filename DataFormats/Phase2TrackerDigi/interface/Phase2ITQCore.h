#ifndef DataFormats_Phase2TrackerDigi_Phase2ITQCore_H
#define DataFormats_Phase2TrackerDigi_Phase2ITQCore_H
#include <array>
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITBitBuffer.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITBitReader.h"
#include <vector>
#include <cstddef>
#include <cstdint>
#include <utility>

// Hitmap Huffman decoding, in the header so the unpacker's decode loop can inline it
namespace Phase2ITQCoreDecode {

  // Read one 2 bits from the stream; a read past its end marks the reader as overran.
  inline std::pair<bool, bool> decPairBits(Phase2ITBitReader& reader) {
    if (!reader.next())  // "0 -> 01" substitute
      return {false, true};
    return {true, reader.next()};
  }

  inline void decChunk(Phase2ITBitReader& reader, int n, bool* out) {
    for (int i = 0; i < n; ++i)
      out[i] = false;
    int active[16], nActive = 0;
    active[nActive++] = 0;
    int curSize = n;
    while (curSize > 2) {
      const int half = curSize / 2;
      int next[16], nNext = 0;
      for (int i = 0; i < nActive; ++i) {
        auto p = decPairBits(reader);
        if (p.first)
          next[nNext++] = active[i];
        if (p.second)
          next[nNext++] = active[i] + half;
      }
      for (int i = 0; i < nNext; ++i)
        active[i] = next[i];
      nActive = nNext;
      curSize = half;
    }
    for (int i = 0; i < nActive; ++i) {
      auto p = decPairBits(reader);
      out[active[i]] = p.first;
      out[active[i] + 1] = p.second;
    }
  }

}  // namespace Phase2ITQCoreDecode

class Phase2ITQCore {
  // Collects hits and creates a quarter core (16 pixel positions)

public:
  Phase2ITQCore(int rocid,
                int ccolIn,
                int qcrowIn,
                bool isneighbourIn,
                bool islastIn,
                const std::vector<int>& adcsIn,
                const std::vector<int>& hitsIn);

  Phase2ITQCore() {
    rocid_ = -1;
    islast_ = false;
    isneighbour_ = false;
    ccol_ = -1;
    qcrow_ = -1;
  }

  void setIsLast(bool islast) { islast_ = islast; }
  bool islast() const { return islast_; }

  void setIsNeighbour(bool isneighbour) { isneighbour_ = isneighbour; }
  bool isneighbour() const { return isneighbour_; }

  int rocid() const { return rocid_; }
  int getCol() const { return ccol_; }
  int getRow() const { return qcrow_; }

  std::vector<bool> getHitmap();
  std::vector<int> getADCs();
  // dropTot=true skips the per-hit 4-bit ToT field entirely (binary readout mode).
  // Appends this qcore's RD53 encoding to out.
  void encodeQCore(Phase2ITBitBuffer& out, bool isNewCol, bool dropTot = false);

  bool operator<(const Phase2ITQCore& other) const {
    if (ccol_ != other.ccol_)
      return ccol_ < other.ccol_;
    return qcrow_ < other.qcrow_;
  }

  static std::vector<bool> toSensorCoordinates(const std::vector<bool>& rocHitmap);
  template <typename T>
  static std::vector<T> toRocCoordinates(const std::vector<T>& inputMap);

  // Hitmaps and ADCs are always 16 entries
  static std::array<bool, 16> decodeHitmap(Phase2ITBitReader& reader) {
    std::array<bool, 16> hitmap{};
    auto rowOr = Phase2ITQCoreDecode::decPairBits(reader);
    if (rowOr.first)
      Phase2ITQCoreDecode::decChunk(reader, 8, hitmap.data());
    if (rowOr.second)
      Phase2ITQCoreDecode::decChunk(reader, 8, hitmap.data() + 8);
    return hitmap;
  }
  static std::array<int, 16> decodeADCs(Phase2ITBitReader& reader, int numHits) {
    std::array<int, 16> adcs{};
    for (int i = 0; i < numHits && i < 16; i++) {
      adcs[i] = reader.bits(4);
    }
    return adcs;
  }

private:
  std::vector<int> adcs_;  // Full array of adc values in a quarter core
  std::vector<int> hits_;  // Full array of hit occurrences
  bool islast_;            // RD53 chip encoding bits
  bool isneighbour_;       // RD53 chip encoding bits
  int rocid_;              // Chip index number
  int ccol_;               // QCore position column
  int qcrow_;              // QCore position row

  std::vector<bool> intToBinary(int num, int length);
  static bool containsHit(std::vector<bool>& hitmap);
};

#endif  // DataFormats_Phase2TrackerDigi_Phase2ITQCore_H
