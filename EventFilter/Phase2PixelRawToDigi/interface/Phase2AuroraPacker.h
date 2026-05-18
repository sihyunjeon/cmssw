#ifndef EventFilter_Phase2PixelRawToDigi_Phase2AuroraPacker_H
#define EventFilter_Phase2PixelRawToDigi_Phase2AuroraPacker_H

// RD53B Aurora-format helpers used by BitStreamToAuroraProducer to compute per-chip Aurora wire-size.
// Three layers
// - 1 : Chip bitstream level
// - 2 : Aurora framing
// - 3 : Aurora Service block

#include <vector>
#include <algorithm>
#include <cstddef>

#include "FWCore/Utilities/interface/Exception.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2DAQFormatSpecification.h"

namespace phase2auroratools {

  using namespace Phase2DAQFormatSpecification;

  // Convert integer to binary
  inline std::vector<bool> int_to_binary(int value, int length) {
    std::vector<bool> bin;
    for (int i = 0; value > 0; i++) {
      if (i >= length)
        break;
      bin.push_back(bool(value % 2));
      value = value / 2;
    }
    while ((int)bin.size() < length)
      bin.push_back(false);
    std::reverse(bin.begin(), bin.end());
    return bin;
  }

  // Per-event tag: 8 bits for the first event in a stream group, 11 bits for every subsequent events
  inline std::vector<bool> make_event_tag(int event, bool is_first_in_stream) {
    return int_to_binary(event, is_first_in_stream ? 8 : 11);
  }

  // Aurora data-block framing.
  //   chip_id == -1 : single-chip mode, 1-b NS + 63-b payload per block.
  //   chip_id 0..3  : multi-chip mode,  1-b NS + 2-b ID + 61-b payload per block.
  // FIXME NS (End-Stream flag) needs to be appended to the last bit (for now ignore the effect as the size won't matter from this)
  inline std::vector<bool> apply_blocking(const std::vector<bool>& stream, int chip_id) {
    if (chip_id < -1 || chip_id > 3)
      throw cms::Exception("Phase2AuroraPacker") << "apply_blocking: chip_id must be in [-1, 3], got " << chip_id;

    std::vector<bool> new_stream;
    int stream_size = stream.size();
    new_stream.reserve(stream_size + (stream_size % 60) * 3);

    std::vector<bool> binary_chip_id = int_to_binary(chip_id, AURORA_CHIP_ID_BITS);

    const int step = AURORA_BLOCK_BODY_BITS - AURORA_NS_BITS - (chip_id >= 0 ? AURORA_CHIP_ID_BITS : 0);

    const int last_index = stream_size / step;
    for (int index = 0; index <= last_index; ++index) {
      new_stream.push_back(new_stream.empty());
      if (chip_id >= 0) {
        new_stream.insert(new_stream.end(), binary_chip_id.begin(), binary_chip_id.end());
      }
      int offset_low = index * step;
      int offset_high = std::min((index + 1) * step, stream_size);
      new_stream.insert(new_stream.end(), stream.begin() + offset_low, stream.begin() + offset_high);
    }

    return new_stream;
  }

  // Orphan padding: zero-pad the trailing partial block-body to AURORA_BLOCK_BODY_BITS.
  inline std::vector<bool> orphan_pad(const std::vector<bool>& stream) {
    std::vector<bool> new_stream(stream);
    new_stream.reserve((new_stream.size() / AURORA_BLOCK_BODY_BITS + 1) * AURORA_BLOCK_BODY_BITS);
    while (new_stream.size() % AURORA_BLOCK_BODY_BITS > 0)
      new_stream.push_back(false);
    return new_stream;
  }

  // End-of-Stream marker: Append a full block-body of zeros if the orphan tail is shorter than the spec minimum.
  inline std::vector<bool> apply_eos_marker(const std::vector<bool>& padded_stream) {
    int trailing_zeros = 0;
    for (auto it = padded_stream.rbegin(); it != padded_stream.rend(); ++it) {
      if (!*it) {
        if (++trailing_zeros >= AURORA_EOS_MIN_TRAILING_ZEROS)
          break;
      } else {
        break;
      }
    }
    if (trailing_zeros >= AURORA_EOS_MIN_TRAILING_ZEROS)
      return padded_stream;

    std::vector<bool> result;
    result.reserve(padded_stream.size() + AURORA_BLOCK_BODY_BITS);
    result.insert(result.end(), padded_stream.begin(), padded_stream.end());
    for (int i = 0; i < AURORA_BLOCK_BODY_BITS; ++i)
      result.push_back(false);
    return result;
  }

  // Aurora 64b/66b sync header: prepend AURORA_SYNC_HEADER_BITS to every block body.
  inline std::vector<bool> apply_header_blocks(const std::vector<bool>& block_stream) {
    if (block_stream.size() % AURORA_BLOCK_BODY_BITS != 0)
      throw cms::Exception("Phase2AuroraPacker")
          << "apply_header_blocks: input size (" << block_stream.size() << " bits) is not a multiple of AURORA_BLOCK_BODY_BITS ("
          << AURORA_BLOCK_BODY_BITS << ")";
    int n_blocks = block_stream.size() / AURORA_BLOCK_BODY_BITS;
    std::vector<bool> result;
    result.reserve(n_blocks * AURORA_BLOCK_TOTAL_BITS);
    for (int b = 0; b < n_blocks; ++b) {
      for (int i = 0; i < AURORA_SYNC_HEADER_BITS; ++i)
        result.push_back(false);
      result.insert(result.end(),
                    block_stream.begin() + b * AURORA_BLOCK_BODY_BITS,
                    block_stream.begin() + (b + 1) * AURORA_BLOCK_BODY_BITS);
    }
    return result;
  }

  // Aurora service block: insert one full 66-b K-block per n_d data blocks
  inline std::vector<bool> apply_service_blocks(const std::vector<bool>& headered_stream,
                                                int n_d = AURORA_SERVICE_BLOCK_INTERVAL_DEFAULT) {
    if (n_d < AURORA_SERVICE_BLOCK_INTERVAL_MIN || n_d > AURORA_SERVICE_BLOCK_INTERVAL_MAX)
      throw cms::Exception("Phase2AuroraPacker") << "apply_service_blocks: n_d=" << n_d << " out of range ["
                                                  << AURORA_SERVICE_BLOCK_INTERVAL_MIN << ", "
                                                  << AURORA_SERVICE_BLOCK_INTERVAL_MAX << "]";
    if (headered_stream.size() % AURORA_BLOCK_TOTAL_BITS != 0)
      throw cms::Exception("Phase2AuroraPacker")
          << "apply_service_blocks: input size (" << headered_stream.size() << " bits) is not a multiple of AURORA_BLOCK_TOTAL_BITS ("
          << AURORA_BLOCK_TOTAL_BITS << ")";
    int n_data_blocks = headered_stream.size() / AURORA_BLOCK_TOTAL_BITS;
    int n_service_blocks = n_data_blocks / n_d;

    std::vector<bool> result;
    result.reserve((n_data_blocks + n_service_blocks) * AURORA_BLOCK_TOTAL_BITS);
    int data_count = 0;
    for (int b = 0; b < n_data_blocks; ++b) {
      result.insert(result.end(),
                    headered_stream.begin() + b * AURORA_BLOCK_TOTAL_BITS,
                    headered_stream.begin() + (b + 1) * AURORA_BLOCK_TOTAL_BITS);
      if (++data_count == n_d) {
        for (int i = 0; i < AURORA_BLOCK_TOTAL_BITS; ++i)
          result.push_back(false);
        data_count = 0;
      }
    }
    return result;
  }

}  // namespace phase2auroratools

#endif  // EventFilter_Phase2PixelRawToDigi_Phase2AuroraPacker_H
