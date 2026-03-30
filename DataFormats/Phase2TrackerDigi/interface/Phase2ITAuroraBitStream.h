#ifndef DataFormats_Phase2TrackerDigi_Phase2ITAuroraBitStream_H
#define DataFormats_Phase2TrackerDigi_Phase2ITAuroraBitStream_H

#include <vector>
#include <cstdint>

// Data format to emulate Phase2ITAuroraBitStream from Phase2ITChipBitStream
class Phase2ITAuroraBitStream {
public:
    Phase2ITAuroraBitStream() : chipId_(0), nEvents_(0) {}
    Phase2ITAuroraBitStream(int chipId, int nEvents)
        : chipId_(chipId), nEvents_(nEvents) {}

    void addEventBitStream(const std::vector<bool>& bits) {
        eventSizes_.push_back(bits.size());
    }

    bool operator<(const Phase2ITAuroraBitStream& rhs) const {
        return chipId_ < rhs.chipId_;
    }

private:
    int chipId_;
    int nEvents_;
    std::vector<int> eventSizes_;   // size of each event's contribution
};

// Mediator format that allows for bit-level manipulation
class BitPacker {
public:
    std::vector<uint64_t> words; // stores full 64-bit words
    BitPacker(bool isStartBlock) : isStartBlock_(isStartBlock) {}

    // Write a single bit, automatically adding header at start of each word 
    void write(bool bit) {
      if (bit_pos == 0) writeHeader(); // start of a new word -> write header
      writeBit(bit);
    }

    // Flush any partially filled word & pad to 64-bit words (orphan padding)
    void align() {
      if (bit_pos != 0) {
        words.push_back(word); // + (64 - bit_pos)); // will add this correctly
        word = 0;
        bit_pos=0;
      }
    }

private:
    uint64_t word = 0;
    int bit_pos = 0; // number of bits written in current word
    bool isStartBlock_; // true for first event in block, false otherwise

    // Write 2-bit chipId + 8/11-bit tag at the start of a word
    void writeHeader() {
      writeBit(0);  // 2-bit chipId (always 00)
      writeBit(0);

      // tag: 8 bits for start-of-block, 11 bits otherwise
      int tag_bits = isStartBlock_ ? 8 : 11;
      for (int i = tag_bits - 1; i >= 0; --i) {
        writeBit(0);  // 8-bit or 11-bit chipId (always 0's)
      }
    }

    void writeBit(bool bit) {
      word |= static_cast<uint64_t>(bit) << (63 - bit_pos);
      bit_pos++;
      if (bit_pos == 64) {
        words.push_back(word);
        word = 0;
        bit_pos = 0;
      }
    }
};

#endif
