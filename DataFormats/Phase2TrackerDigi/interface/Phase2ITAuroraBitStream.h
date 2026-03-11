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

#endif
