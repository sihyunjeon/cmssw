#ifndef EventFilter_Phase2TrackerRawToDigi_TrackerHeader_H
#define EventFilter_Phase2TrackerRawToDigi_TrackerHeader_H

#include "EventFilter/Phase2TrackerRawToDigi/interface/Phase2DAQFormatSpecification.h"

// Class to store the (outer) tracker header for each event, read the FedRawData collection
// as from the current tracker data format

using namespace Phase2DAQFormatSpecification;

class TrackerHeader {
public:
  std::vector<uint32_t> values_{std::vector<uint32_t>(4, 0)};
  bool is2S_{false};

  void setValue(std::vector<uint32_t>& newValues) { 
    values_ = newValues; 
    is2S_ = ((values_[0] >> (N_BITS_PER_WORD - MODULE_TYPE_BITS)) & ((1u << MODULE_TYPE_BITS) - 1)) == MODULE_TYPE_2S;    
  }
  
  bool is2S(){return is2S_;}

  void printValues() const {
    for (size_t i = 0; i < values_.size(); ++i) {
      std::cout << "TrackerHeader[" << i << "]: " << values_[i] << "   " << std::bitset<32>(values_[i]) << std::endl;
    }
  }
  void printValue(size_t i) const {
    std::cout << "TrackerHeader[" << i << "]: " << values_[i] << "   " << std::bitset<32>(values_[i]) << std::endl;
  }
};

#endif