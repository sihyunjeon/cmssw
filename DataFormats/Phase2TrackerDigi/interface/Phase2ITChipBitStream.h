#ifndef DataFormats_Phase2TrackerDigi_Phase2ITChipBitStream_H
#define DataFormats_Phase2TrackerDigi_Phase2ITChipBitStream_H
#include <cstdint>
#include <vector>

// Encoded bit stream output from one chip, packed MSB first within each byte:
// bit i is (bytes[i / 8] >> (7 - i % 8)) & 1. nBits marks the significant bits.
class Phase2ITChipBitStream {
public:
  Phase2ITChipBitStream(int rocid, std::vector<uint8_t> bytes, uint32_t nBits)
      : rocid_(rocid), bytes_(std::move(bytes)), nBits_(nBits) {}

  // Packing constructor, for producers that still build a bit vector.
  Phase2ITChipBitStream(int rocid, const std::vector<bool>& bitstream)
      : rocid_(rocid), bytes_((bitstream.size() + 7) / 8, 0), nBits_(bitstream.size()) {
    for (size_t i = 0; i < bitstream.size(); ++i)
      if (bitstream[i])
        bytes_[i / 8] |= uint8_t(1) << (7 - i % 8);
  }

  Phase2ITChipBitStream() : rocid_(-1), nBits_(0) {}

  int get_rocid() const { return rocid_; }

  const std::vector<uint8_t>& bytes() const { return bytes_; }
  uint32_t nBits() const { return nBits_; }
  bool bit(uint32_t i) const { return (bytes_[i / 8] >> (7 - i % 8)) & 1; }

  // Rebuilds a bit vector on every call; prefer bytes()/bit() on hot paths.
  std::vector<bool> get_bitstream() const {
    std::vector<bool> out(nBits_);
    for (uint32_t i = 0; i < nBits_; ++i)
      out[i] = bit(i);
    return out;
  }

  const bool operator<(const Phase2ITChipBitStream& other) { return rocid_ < other.rocid_; }

private:
  int rocid_;                   // Chip index
  std::vector<uint8_t> bytes_;  // Chip bit stream, packed MSB first
  uint32_t nBits_;              // Significant bits in bytes_
};
#endif  // DataFormats_Phase2TrackerDigi_Phase2ITChipBitStream_H
