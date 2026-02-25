#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITQCore.h"
#include <cmath>
#include <vector>
#include <iostream>
#include <functional>

//4x4 region of hits in sensor coordinates
Phase2ITQCore::Phase2ITQCore(int rocid,
                             int ccol_in,
                             int qcrow_in,
                             bool isneighbour_in,
                             bool islast_in,
                             const std::vector<int>& adcs_in,
                             const std::vector<int>& hits_in) {
  rocid_ = rocid;
  ccol_ = ccol_in;
  qcrow_ = qcrow_in;
  isneighbour_ = isneighbour_in;
  islast_ = islast_in;
  adcs_ = adcs_in;
  hits_ = hits_in;
}

//Takes a hitmap in sensor coordinates in 4x4 and converts it to readout chip coordinates with 2x8
template <typename T>
std::vector<T> Phase2ITQCore::toRocCoordinates(const std::vector<T>& input_map) {

  std::vector<T> roc_coord(16);

  for (size_t i = 0; i < input_map.size(); i++) {

    int row = i / 4;
    int col = i % 4;
    int new_row;
    int new_col;

    if (row % 2 == 0) {
      new_row = row / 2;
      new_col = 2 * col;
    } else {
      new_row = row / 2;
      new_col = 2 * col + 1;
    }

    int new_index = 8 * new_row + new_col;
    roc_coord[new_index] = input_map[i];
  }

  return roc_coord;
}

std::vector<bool> Phase2ITQCore::toSensorCoordinates(const std::vector<bool>& roc_hitmap) {
  std::vector<bool> sensor_hitmap(16, false);  // or HITMAP_SIZE
  for (int i = 0; i < 16; ++i) {
    int rocRow = i / 8;
    int rocCol = i % 8;
    int sensorRow = (rocCol % 2 == 0) ? rocRow * 2 : rocRow * 2 + 1;
    int sensorCol = (rocCol % 2 == 0) ? rocCol / 2 : (rocCol - 1) / 2;
    int sensorIndex = sensorRow * 4 + sensorCol;
    sensor_hitmap[sensorIndex] = roc_hitmap[i];
  }
  return sensor_hitmap;
}

//Returns the hitmap for the Phase2ITQCore in 4x4 sensor coordinates
std::vector<bool> Phase2ITQCore::getHitmap() {
  std::vector<bool> hitmap = {};

  hitmap.reserve(hits_.size());
  for (auto hit : hits_) {
    hitmap.push_back(hit > 0);
  }

  return (toRocCoordinates(hitmap));
}

std::vector<int> Phase2ITQCore::getADCs() {
  std::vector<int> adcmap = {};

  adcmap.reserve(adcs_.size());
  for (auto adc : adcs_) {
    adcmap.push_back(adc);
  }

  return (toRocCoordinates(adcmap));
}

//Converts an integer into a binary, and formats it with the given length
std::vector<bool> Phase2ITQCore::intToBinary(int num, int length) {
  std::vector<bool> bi_num(length, false);

  for (int i = 0; i < length; ++i) {
    // Extract the (length - 1 - i)th bit from num
    bi_num[i] = (num >> (length - 1 - i)) & 1;
  }

  return bi_num;
}

static uint32_t binaryToInt(const std::vector<bool>& binary, size_t& bitPos, int length) {
  uint32_t result = 0;
  for (int i = 0; i < length; ++i) {
    if ((bitPos + i) < binary.size() && binary[bitPos + i]) {
      result |= (1 << (length - 1 - i));
    }
  }
  bitPos += length;
  return result;
}

//Takes a hitmap and returns true if it contains any hits
bool Phase2ITQCore::containsHit(std::vector<bool>& hitmap) {
  bool foundHit = false;
  for (size_t i = 0; i < hitmap.size(); i++) {
    if (hitmap[i]) {
      foundHit = true;
      break;
    }
  }

  return foundHit;
}

//Returns the Huffman encoded hitmap, created iteratively within this function
std::vector<bool> Phase2ITQCore::encodeHitmap(const std::vector<bool>& hitmap) {
  std::vector<bool> code = {};
  // If hitmap is a single bit, there is no need to further split the bits
  if (hitmap.size() == 1) {
    return code;
  }

  std::vector<bool> left_hitmap = std::vector<bool>(hitmap.begin(), hitmap.begin() + hitmap.size() / 2);
  std::vector<bool> right_hitmap = std::vector<bool>(hitmap.begin() + hitmap.size() / 2, hitmap.end());

  bool hit_left = containsHit(left_hitmap);
  bool hit_right = containsHit(right_hitmap);

  if (hit_left && hit_right) {
    code.push_back(true);
    code.push_back(true);

    std::vector<bool> left_code = encodeHitmap(left_hitmap);
    std::vector<bool> right_code = encodeHitmap(right_hitmap);

    code.insert(code.end(), left_code.begin(), left_code.end());
    code.insert(code.end(), right_code.begin(), right_code.end());

  } else if (hit_right) {
    //Huffman encoding compresses 01 into 0
    code.push_back(false);

    std::vector<bool> right_code = encodeHitmap(right_hitmap);
    code.insert(code.end(), right_code.begin(), right_code.end());

  } else if (hit_left) {
    code.push_back(true);
    code.push_back(false);

    std::vector<bool> left_code = encodeHitmap(left_hitmap);
    code.insert(code.end(), left_code.begin(), left_code.end());
  }

  return code;
}

std::vector<bool> Phase2ITQCore::decodeHitmap(const std::vector<bool>& bitstream, size_t& bitPos) {
  std::vector<bool> hitmap(16, false);  // or HITMAP_SIZE ?

  std::function<void(size_t, size_t)> decode = [&](size_t start, size_t length) {
    if (length == 1) {
      if (start < hitmap.size())
        hitmap[start] = true;
      return;
    }

    if (bitPos >= bitstream.size())
      return;

    bool first = bitstream[bitPos++];
    size_t mid = start + length / 2;

    if (!first) {
      decode(mid, length / 2);
    } else {
      if (bitPos >= bitstream.size())
        return;
      bool second = bitstream[bitPos++];
      if (!second) {
        decode(start, length / 2);
      } else {
        decode(start, length / 2);
        decode(mid, length / 2);
      }
    }
  };

  decode(0, 16);  // or HITMAP_SIZE ?
  return hitmap;
}

std::vector<int> Phase2ITQCore::decodeADCs(const std::vector<bool>& bitstream, size_t& bitPos, int numHits) {
  std::vector<int> adcs;
  adcs.reserve(numHits);
  for (int i = 0; i < numHits; i++) {
    adcs.push_back(::binaryToInt(bitstream, bitPos, 4));
  }
  return adcs;
}

//Returns the bit code associated with the Phase2ITQCore
std::vector<bool> Phase2ITQCore::encodeQCore(bool is_new_col) {
  std::vector<bool> code = {};

  if (is_new_col) {
    std::vector<bool> col_code = intToBinary(ccol_, 6);
    code.insert(code.end(), col_code.begin(), col_code.end());
  }

  code.push_back(islast_);
  code.push_back(isneighbour_);

  if (!isneighbour_) {
    std::vector<bool> row_code = intToBinary(qcrow_, 8);
    code.insert(code.end(), row_code.begin(), row_code.end());
  }

  std::vector<bool> hitmap = getHitmap();
  std::vector<bool> hitmap_code = encodeHitmap(hitmap);
  code.insert(code.end(), hitmap_code.begin(), hitmap_code.end());

  std::vector<int> adcs_code = getADCs();
for (int i = 0; i < 16; i++) {
    if (hitmap[i]) {  // only write ADC if there's a hit
        std::vector<bool> adc_code = intToBinary(adcs_code[i], 4);
        code.insert(code.end(), adc_code.begin(), adc_code.end());
    }
}

std::vector<int> adcs = getADCs();
std::cout << "ENCODE QCore col=" << ccol_ << " row=" << qcrow_ << std::endl;
for (int i = 0; i < 16; i++) {
    if (hitmap[i]) {
        std::cout << "  hit at rocIndex=" << i 
                  << " rocRow=" << i/8 
                  << " rocCol=" << i%8 
                  << " adc=" << adcs[i] << std::endl;  // note: adcs now only hit ADCs, adjust index
    }
}


  /*
  std::cout<<"hitmap : ";
  for (auto hit: getHitmap()){
    std::cout<<hit<<" ";
  }
  std::cout<<std::endl;

  std::cout<<"totmap : ";
  for (auto adc: adcs_code){
    std::cout<<adc<<" ";
  }
  std::cout<<std::endl;
  std::cout<<std::endl;
  */
  return code;
}
