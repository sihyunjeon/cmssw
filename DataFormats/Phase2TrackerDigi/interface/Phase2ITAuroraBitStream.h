#ifndef DataFormats_Phase2TrackerDigi_Phase2ITAuroraBitStream_H
#define DataFormats_Phase2TrackerDigi_Phase2ITAuroraBitStream_H
#include <vector>

class Phase2ITAuroraBitStream {
  // Per chip bitstreams wired into Aurora formatting
public:
  Phase2ITAuroraBitStream() : chipId_(-1), eventsPerStream_(0) {}
  Phase2ITAuroraBitStream(int chipId, int eventsPerStream) : chipId_(chipId), eventsPerStream_(eventsPerStream) {}

  void addEventBitStream(const std::vector<bool>& bits) { eventSizes_.push_back(bits.size()); }

  int get_chipId() const { return chipId_; }
  int get_eventsPerStream() const { return eventsPerStream_; }
  const std::vector<int>& get_eventSizes() const { return eventSizes_; }

  const bool operator<(const Phase2ITAuroraBitStream& other) { return chipId_ < other.chipId_; }

private:
  int chipId_;                   // Chip index within the module
  int eventsPerStream_;          // NE: events per stream group
  std::vector<int> eventSizes_;  // Aurora wire size in bits per stream group
};
#endif  // DataFormats_Phase2TrackerDigi_Phase2ITAuroraBitStream_H
